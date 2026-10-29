"""실행 환경: 시스템 Python 3(numpy) 와 Windows Kits dxc.exe(vs_6_0·ps_6_0), 에디터 불필요
실행 명령: python Tools/FluidFX/check_fluid_foliage_hlsl.py [--root 프로젝트경로] [--dxc dxc경로] [--look-fragment 조각.json] [--output 요약.json]
출력: 항목별 PASS/FAIL 줄과 마지막 줄 JSON 요약(dxc 명령·표본 수, 거울 시험 sha256), 하나라도 실패하면 종료 코드 1
상태: 미검증(2026-10-26 FX-13r R13 작성, 2026-10-29 파동 3 FX-13c: 복사 입력 AuxTexture·AuxFoliageBendActive 식 고정점과 거울 F7 스칼라 1·BA = 2t-1 이면 복사 출력 = 속도 경로, MI 이름 계약은 채널 3개 전부이므로 조각 병합 전에는 --look-fragment 필요). TDFluidFoliageFlowCopy.hlsl·TDFluidFoliageBend.hlsl 을 재질 스크립트와 같은 조립 함수로 조립해 텍스트·식 고정점(안전 정규화·θ=0 분기·M1 속도 기준)·이름 계약(MI 덮어쓰기에 C++ 구동 이름 없음, 스칼라·벡터 구획)·dxc(복사 ps_6_0, 휨 vs_6_0·ps_6_0, O3/Od, 마스터 값 접기 순열)를 보고, numpy 거울 F1 길이 보존·F2 뿌리 WPO 0·F3 휨 방향·F4 창 경계 연속·F5 복사→MF 왕복 θ·F6 마스터 값 WPO 0(NaN 없음)을 두 번 돌려 sha256 이 같은지 본다.
"""
import argparse
import glob
import hashlib
import importlib.util
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
from unittest import mock

import numpy as np

sys.dont_write_bytecode = True

HEADER = """
#define Texture2DSampleLevel(T, S, UV, L) T.SampleLevel(S, UV, L)
"""
SAMPLE_PATTERN = re.compile(r"call \S+ @dx\.op\.(?:sample|sampleLevel|sampleBias|sampleGrad)\.")
INSTRUCTION_PATTERN = re.compile(r"^\s+(?:%\S+ = |call |store |br |ret )", re.MULTILINE)
ABSOLUTE_PATH_PATTERN = re.compile(r"[A-Za-z]:[\\/]")
COPY_ANCHORS = (
    "float4 tdAuxPacked = Texture2DSampleLevel(AuxTexture, AuxTextureSampler, FlowUv, 0.0);",
    "float2 tdFlowNormalized = AuxFoliageBendActive > 0.5 ? tdAuxPacked.ba : (2.0 * tdFlowPacked.rg - 1.0);",
    "float2 tdFlowPlane = tdFlowNormalized * VelocitySpeedReferenceCmPerSec;",
    "float2 tdFlowWorld = tdFlowPlane.x * PlaneAxisU.xy + tdFlowPlane.y * PlaneAxisV.xy;",
    "float tdFlowDegrees = clamp(FoliageBendMaxDegrees, 0.0, 85.0) * tdFlowMagnitude;",
    "return float4(0.5 + 0.5 * tdFlowDirection * tdFlowDegrees / 90.0, tdFlowDegrees / 90.0, 1.0);",
)
BEND_ANCHORS = (
    "float3 tdBendAxis = tdBendAxisRaw * rsqrt(max(dot(tdBendAxisRaw, tdBendAxisRaw), 1e-12));",
    "if (tdBendAngle < 0.000001) { return float3(0.0, 0.0, 0.0); }",
    "BendAmount = tdBendDegrees * tdBendWeight / 90.0;",
    "float tdBendActive = step(0.5, FoliageFlowState.x) * step(0.001, tdBendExtent.x);",
    "float2 tdBendVector = tdWindBend * (1.0 - tdBendWeight * FoliageSimReplacesWind) + tdBendSim * tdBendWeight * FoliageBendScale;",
)
TEXTURE_INPUT_NAMES = ("VelocityTexture", "AuxTexture", "FoliageFlowTexture")
GRID = 256
MASTER_FOLDED_INSTRUCTION_LIMIT = 20
RNG_SEED = 1317


def load_script(root, name):
    spec = importlib.util.spec_from_file_location(name, root / "Tools" / "FluidFX" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def find_dxc(explicit_path):
    candidates = [explicit_path, os.environ.get("DXC_PATH")]
    program_files = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    candidates.extend(reversed(sorted(glob.glob(os.path.join(program_files, "Windows Kits", "10", "bin", "*", "x64", "dxc.exe")))))
    return next((candidate for candidate in candidates if candidate and os.path.isfile(candidate)), "")


def normalize(text):
    return re.sub(r"\s+", " ", text)


def declare_inputs(input_names, vector_names, float2_names, float4_names, folded):
    lines = []
    for name in TEXTURE_INPUT_NAMES:
        if name in input_names:
            lines += [f"Texture2D {name};", f"SamplerState {name}Sampler;"]
    constants = "".join(f"static const float {name} = {value};\n" for name, value in folded.items() if not isinstance(value, tuple))
    constants += "".join(f"static const float4 {name} = float4{value};\n" for name, value in folded.items() if isinstance(value, tuple))
    members = []
    for name in input_names:
        if name in TEXTURE_INPUT_NAMES or name in folded:
            continue
        kind = "float3" if name in vector_names else "float2" if name in float2_names else "float4" if name in float4_names else "float"
        members.append(f"{kind} {name};")
    return "\n".join(lines) + "\n" + constants + "cbuffer Inputs {\n" + "\n".join(members) + "\n}\n"


def build_copy_source(material, code, folded):
    declarations = declare_inputs(material.COPY_INPUT_NAMES, material.COPY_VECTOR_INPUT_NAMES, material.COPY_FLOAT2_INPUT_NAMES, (), folded)
    return HEADER + declarations + f"float4 TDFoliageFlowCopy()\n{{\n{code}\n}}\n" + "float4 main(float4 sv : SV_Position) : SV_Target\n{\nreturn TDFoliageFlowCopy();\n}\n"


def build_bend_source(material, code, folded, stage):
    declarations = declare_inputs(material.BEND_INPUT_NAMES, material.BEND_VECTOR_INPUT_NAMES, (), material.BEND_FLOAT4_INPUT_NAMES, folded)
    body = HEADER + declarations + f"float3 TDFoliageBend(inout float BendAmount)\n{{\n{code}\n}}\n"
    if stage == "vs":
        return body + "float4 main(float3 position : POSITION, out float bendAmount : TEXCOORD0) : SV_Position\n{\nbendAmount = 0.0;\nfloat3 offset = TDFoliageBend(bendAmount);\nreturn float4(position + offset, 1.0);\n}\n"
    return body + "float4 main(float4 sv : SV_Position) : SV_Target\n{\nfloat bendAmount = 0.0;\nfloat3 offset = TDFoliageBend(bendAmount);\nreturn float4(offset, bendAmount);\n}\n"


def compile_source(dxc, source, label, profile, optimize, work_folder):
    path = work_folder / f"{label}.hlsl"
    disassembly = work_folder / f"{label}.dxil.txt"
    path.write_text(source, encoding="utf-8")
    command = [dxc, "-T", profile, "-E", "main", str(path), "-Fc", str(disassembly), "-Fo", str(work_folder / f"{label}.bin"), "-O3" if optimize else "-Od"]
    completed = subprocess.run(command, capture_output=True, text=True)
    text = disassembly.read_text(encoding="utf-8", errors="replace") if disassembly.exists() else ""
    first_error = next((line for line in (completed.stderr + completed.stdout).splitlines() if "error" in line), "")
    return {"exit": completed.returncode, "samples": len(SAMPLE_PATTERN.findall(text)), "instructions": len(INSTRUCTION_PATTERN.findall(text)), "error": first_error}


def smoothstep(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def saturate(x):
    return np.clip(x, 0.0, 1.0)


def copy_flow(packed_rg, axis_u, axis_v, speed_reference, max_degrees, bend_speed_reference, response, aux_packed_ba=None, aux_active=0.0):
    normalized = aux_packed_ba if aux_packed_ba is not None and aux_active > 0.5 else 2.0 * packed_rg - 1.0
    plane = normalized * speed_reference
    world = plane[..., :1] * np.asarray(axis_u[:2]) + plane[..., 1:2] * np.asarray(axis_v[:2])
    speed = np.linalg.norm(world, axis=-1)
    magnitude = np.power(saturate(speed / max(bend_speed_reference, 1.0)), max(response, 0.01))
    degrees = min(max(max_degrees, 0.0), 85.0) * magnitude
    direction = world / np.maximum(speed, 0.001)[..., None]
    rg = 0.5 + 0.5 * direction * (degrees / 90.0)[..., None]
    return np.concatenate([rg, (degrees / 90.0)[..., None], np.ones_like(degrees)[..., None]], axis=-1)


def sample_nearest(texture, uv):
    size = texture.shape[0]
    index = np.clip(np.floor(saturate(uv) * size).astype(np.int64), 0, size - 1)
    return texture[index[..., 1], index[..., 0]]


def window_weight(root_xy, center, extent, state_x, fade_ratio):
    uv = (root_xy - center[:2]) / np.maximum(extent[:2], 1.0) + 0.5
    edge = 0.5 - np.abs(uv - 0.5)
    min_extent = min(extent[0], extent[1])
    edge_cm = np.minimum(edge[..., 0], edge[..., 1]) * min_extent
    active = float(state_x >= 0.5) * float(extent[0] >= 0.001)
    return active * smoothstep(0.0, max(fade_ratio * min_extent, 0.001), edge_cm), uv


def bend(world, root, local_z, bounds_min_z, bounds_max_z, seconds, flow_texture, window, params):
    arm = world - root
    bounds_height = max(bounds_max_z - bounds_min_z, 0.001)
    if params["FoliageBladeHeightCm"] > 0.0:
        height = saturate(np.linalg.norm(arm, axis=-1) / params["FoliageBladeHeightCm"])
    else:
        height = saturate((local_z - bounds_min_z) / bounds_height)
    weight, uv = window_weight(root[..., :2], window["center"], window["extent"], window["state"][0], params["FoliageWindowEdgeFadeRatio"])
    simulated = (2.0 * sample_nearest(flow_texture, uv)[..., :2] - 1.0) * 90.0
    direction = np.asarray(params["FoliageWindDirection"][:2], dtype=np.float64)
    direction = direction / max(float(np.linalg.norm(direction)), 0.0001)
    side = np.array([-direction[1], direction[0]])
    gust_scale = max(params["FoliageWindGustScaleCm"], 1.0)
    along = root[..., :2] @ direction / gust_scale
    across = root[..., :2] @ side / gust_scale
    phase = params["FoliageWindSpeed"] * seconds
    gust = 0.5 + 0.25 * np.sin(2.0 * np.pi * (along - 0.37 * phase)) + 0.25 * np.sin(2.0 * np.pi * (1.7 * across + 0.23 * phase))
    amount = params["FoliageWindAmplitude"] * (0.5 + 0.5 * np.sin(2.0 * np.pi * phase + 0.013 * root[..., 0] + 0.021 * root[..., 1])) * (1.0 + (gust - 1.0) * params["FoliageWindGustStrength"])
    wind = direction * (params["FoliageWindMaxDegrees"] * amount)[..., None]
    vector = wind * (1.0 - weight * params["FoliageSimReplacesWind"])[..., None] + simulated * (weight * params["FoliageBendScale"])[..., None]
    length = np.linalg.norm(vector, axis=-1)
    degrees = np.minimum(length, 85.0)
    unit = vector / np.maximum(length, 0.0001)[..., None]
    angle = np.radians(degrees) * np.power(height, max(params["FoliageBendHeightExponent"], 0.01))
    amount_out = degrees * weight / 90.0
    axis_raw = np.stack([-unit[..., 1], unit[..., 0], np.zeros_like(unit[..., 0])], axis=-1)
    axis = axis_raw / np.sqrt(np.maximum(np.sum(axis_raw * axis_raw, axis=-1), 1e-12))[..., None]
    cos_a = np.cos(angle)[..., None]
    sin_a = np.sin(angle)[..., None]
    rotated = arm * cos_a + np.cross(axis, arm) * sin_a + axis * np.sum(axis * arm, axis=-1)[..., None] * (1.0 - cos_a)
    offset = np.where((angle < 0.000001)[..., None], 0.0, rotated - arm)
    return offset, amount_out, unit, degrees


def master_params(material):
    params = {name: value for name, value in material.BEND_LOOK_SCALAR_DEFAULTS}
    params.update({name: value for name, value in material.BEND_LOOK_VECTOR_DEFAULTS})
    return params


def make_scene(rng, count):
    roots = np.column_stack([rng.uniform(-400.0, 400.0, count), rng.uniform(-400.0, 400.0, count), rng.uniform(-5.0, 5.0, count)])
    heights = rng.uniform(0.0, 60.0, count)
    lateral = rng.normal(0.0, 3.0, (count, 2))
    world = roots + np.column_stack([lateral, heights])
    return roots, world, heights


def flow_texture_from_velocity(material, rng, max_degrees):
    velocity = rng.uniform(-1.0, 1.0, (GRID, GRID, 2)) * rng.uniform(0.0, 1.0, (GRID, GRID, 1))
    packed = saturate(velocity * 0.5 + 0.5)
    copy = copy_flow(packed, (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), 600.0, max_degrees, 300.0, 1.0)
    return copy, packed


def test_length_preservation(material):
    rng = np.random.default_rng(RNG_SEED)
    roots, world, heights = make_scene(rng, 20000)
    texture, _ = flow_texture_from_velocity(material, rng, 70.0)
    params = master_params(material) | {"FoliageWindAmplitude": 0.6, "FoliageBendHeightExponent": 1.5}
    window = {"center": np.array([0.0, 0.0, 0.0, 1.0]), "extent": np.array([700.0, 700.0, 0.1, 0.0]), "state": (1.0, 600.0, 0.0, 0.0)}
    offset, _, _, _ = bend(world, roots, heights, 0.0, 60.0, 3.7, texture, window, params)
    arm = world - roots
    error = np.abs(np.linalg.norm(arm + offset, axis=-1) - np.linalg.norm(arm, axis=-1))
    relative = float((error / np.maximum(np.linalg.norm(arm, axis=-1), 1e-6)).max())
    moved = float(np.linalg.norm(offset, axis=-1).max())
    return relative <= 1e-4 and moved > 1.0 and bool(np.isfinite(offset).all()), f"상대 길이 오차 최대 {relative:.2e}, 최대 변위 {moved:.1f} cm", offset


def test_root_zero(material):
    rng = np.random.default_rng(RNG_SEED + 1)
    roots, _, _ = make_scene(rng, 5000)
    texture, _ = flow_texture_from_velocity(material, rng, 80.0)
    params = master_params(material) | {"FoliageWindAmplitude": 1.0}
    window = {"center": np.zeros(4), "extent": np.array([700.0, 700.0, 0.1, 0.0]), "state": (1.0, 600.0, 0.0, 0.0)}
    offset, _, _, _ = bend(roots, roots, np.zeros(len(roots)), 0.0, 60.0, 1.3, texture, window, params)
    return bool(np.all(offset == 0.0)), f"뿌리 WPO 최대 {float(np.abs(offset).max())}", offset


def test_bend_direction(material):
    rng = np.random.default_rng(RNG_SEED + 2)
    count = 4000
    roots = np.column_stack([rng.uniform(-300.0, 300.0, count), rng.uniform(-300.0, 300.0, count), np.zeros(count)])
    world = roots + np.array([0.0, 0.0, 50.0])
    texture, _ = flow_texture_from_velocity(material, rng, 70.0)
    params = master_params(material) | {"FoliageWindAmplitude": 0.4, "FoliageWindDirection": (0.6, 0.8, 0.0, 0.0)}
    window = {"center": np.zeros(4), "extent": np.array([700.0, 700.0, 0.1, 0.0]), "state": (1.0, 600.0, 0.0, 0.0)}
    offset, _, unit, degrees = bend(world, roots, np.full(count, 50.0), 0.0, 50.0, 2.1, texture, window, params)
    bent = degrees > 1.0
    horizontal = offset[bent][:, :2] / np.linalg.norm(offset[bent][:, :2], axis=-1, keepdims=True)
    alignment = float(np.sum(horizontal * unit[bent], axis=-1).min())
    lowered = bool(np.all(offset[bent][:, 2] <= 1e-9))
    return alignment >= 0.999 and lowered and int(bent.sum()) > 100, f"휨 정렬 최소 {alignment:.6f}, 휜 정점 {int(bent.sum())}, 높이 감소 {lowered}", offset


def test_window_boundary(material):
    center = np.array([1200.0, -300.0, 0.0, 1.0])
    extent = np.array([700.0, 500.0, 0.1, 0.0])
    epsilon = 1e-3
    xs = np.array([center[0] + 0.5 * extent[0] - epsilon, center[0] + 0.5 * extent[0], center[0] + 0.5 * extent[0] + epsilon])
    points = np.column_stack([xs, np.full(3, center[1])])
    weight, _ = window_weight(points, center, extent, 1.0, 0.1)
    jump = float(np.abs(np.diff(weight)).max())
    inside, _ = window_weight(center[None, :2], center, extent, 1.0, 0.1)
    off, _ = window_weight(center[None, :2], center, extent, 0.0, 0.1)
    passed = jump <= 1e-6 and float(weight[1]) == 0.0 and float(inside[0]) == 1.0 and float(off[0]) == 0.0
    return passed, f"경계 점프 {jump:.2e}, 경계 가중 {float(weight[1])}, 중심 {float(inside[0])}, 꺼짐 {float(off[0])}", weight


def test_round_trip(material):
    rng = np.random.default_rng(RNG_SEED + 3)
    texture, packed = flow_texture_from_velocity(material, rng, 70.0)
    plane = (2.0 * packed - 1.0) * 600.0
    speed = np.linalg.norm(plane, axis=-1)
    expected = 70.0 * saturate(speed / 300.0)
    cells = (np.arange(GRID) + 0.5) / GRID
    uv = np.stack(np.meshgrid(cells, cells, indexing="xy"), axis=-1)
    results = {}
    for label, stored in (("float32", texture.astype(np.float32)), ("float16", texture.astype(np.float16))):
        decoded = (2.0 * sample_nearest(stored.astype(np.float64), uv)[..., :2] - 1.0) * 90.0
        degrees = np.linalg.norm(decoded, axis=-1)
        error = float(np.abs(degrees - expected).max())
        bent = expected > 1.0
        alignment = float(np.sum(decoded[bent] / degrees[bent][:, None] * plane[bent] / speed[bent][:, None], axis=-1).min())
        results[label] = (error, alignment)
    passed = results["float32"][0] <= 0.05 and results["float16"][0] <= 0.07 and results["float32"][1] >= 0.9999
    detail = f"θ 오차 float32 {results['float32'][0]:.4f}°·RGBA16F {results['float16'][0]:.4f}°, 방향 최소 {results['float32'][1]:.6f}"
    return passed, detail, np.array([results["float32"][0], results["float16"][0]])


def test_master_zero(material):
    rng = np.random.default_rng(RNG_SEED + 4)
    roots, world, heights = make_scene(rng, 10000)
    copy_scalars = dict(material.COPY_LOOK_SCALAR_DEFAULTS)
    texture, _ = flow_texture_from_velocity(material, rng, copy_scalars["FoliageBendMaxDegrees"])
    params = master_params(material)
    outcomes = []
    for state in ((1.0, 600.0, 0.0, 0.0), (0.0, 0.0, 0.0, 0.0)):
        window = {"center": np.zeros(4), "extent": np.array([700.0, 700.0, 0.1, 0.0]), "state": state}
        offset, amount, _, _ = bend(world, roots, heights, 0.0, 60.0, 5.0, texture, window, params)
        outcomes.append((bool(np.all(offset == 0.0)), bool(np.isfinite(offset).all() and np.isfinite(amount).all()), float(np.abs(amount).max())))
    passed = all(zero and finite and amount == 0.0 for zero, finite, amount in outcomes) and float(np.abs(texture[..., :2] - 0.5).max()) == 0.0
    return passed, f"마스터 값 WPO 0·유한·BendAmount 0 (창 켬, 끔) = {outcomes}", np.array([[float(a), float(b), c] for a, b, c in outcomes])


def test_aux_source_equals_velocity(material):
    rng = np.random.default_rng(RNG_SEED + 5)
    packed = saturate(rng.uniform(-1.0, 1.0, (GRID, GRID, 2)) * rng.uniform(0.0, 1.0, (GRID, GRID, 1)) * 0.5 + 0.5)
    garbage = rng.uniform(-1.0, 1.0, (GRID, GRID, 2))
    reference = copy_flow(packed, (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), 600.0, 70.0, 300.0, 1.0)
    aux_ba = 2.0 * packed - 1.0
    active = copy_flow(np.full_like(packed, 0.5), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), 600.0, 70.0, 300.0, 1.0, aux_ba, 1.0)
    inactive = copy_flow(packed, (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), 600.0, 70.0, 300.0, 1.0, garbage, 0.0)
    boundary = copy_flow(packed, (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), 600.0, 70.0, 300.0, 1.0, garbage, 0.5)
    active_error = float(np.abs(active - reference).max())
    inactive_error = float(np.abs(inactive - reference).max())
    boundary_error = float(np.abs(boundary - reference).max())
    default_active = dict(material.COPY_DRIVEN_SCALAR_DEFAULTS).get("AuxFoliageBendActive")
    passed = active_error <= 1e-6 and inactive_error == 0.0 and boundary_error == 0.0 and default_active == 0.0
    detail = f"스칼라 1·BA=2t-1 오차 {active_error:.2e}, 스칼라 0 오차 {inactive_error:.2e}, 스칼라 0.5 오차 {boundary_error:.2e}, 마스터 기본 {default_active}"
    return passed, detail, np.array([active_error, inactive_error, boundary_error])


def run_mirror_tests(material):
    outcomes = {}
    digest = hashlib.sha256()
    tests = (("F1 길이 보존", test_length_preservation), ("F2 뿌리 WPO 0", test_root_zero), ("F3 휨 방향", test_bend_direction),
             ("F4 창 경계 연속", test_window_boundary), ("F5 복사→MF 왕복", test_round_trip), ("F6 마스터 값 WPO 0", test_master_zero),
             ("F7 Aux 원천 = 속도 경로", test_aux_source_equals_velocity))
    for label, test in tests:
        passed, detail, values = test(material)
        values = np.ascontiguousarray(values, dtype=np.float64)
        digest.update(label.encode("utf-8"))
        digest.update(values.tobytes())
        outcomes[label] = (passed, detail, hashlib.sha256(values.tobytes()).hexdigest())
    return outcomes, digest.hexdigest()


def run_shader_text_checks(text):
    failures = []
    if "//" in text or "/*" in text:
        failures.append("주석이 있음")
    if ABSOLUTE_PATH_PATTERN.search(text):
        failures.append("절대 경로가 있음")
    if not text.isascii():
        failures.append("ASCII 가 아닌 문자가 있음")
    if "\r" in text:
        failures.append("CRLF 줄바꿈")
    return failures


def collect_look_entries(root, fragment_path):
    entries = {}
    canonical = root / "Content" / "FluidFX" / "PresetData" / "TDFluidMaterialLook.json"
    if canonical.is_file():
        entries.update(json.loads(canonical.read_text(encoding="utf-8")).get("Materials", {}))
    if not fragment_path:
        return entries, []
    fragment = json.loads(pathlib.Path(fragment_path).read_text(encoding="utf-8"))
    overlap = sorted(set(fragment.get("Materials", {})) & set(entries))
    entries.update(fragment.get("Materials", {}))
    return entries, overlap


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parents[2]))
    parser.add_argument("--dxc", default="")
    parser.add_argument("--look-fragment", default="")
    parser.add_argument("--output", default="")
    arguments = parser.parse_args()
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    root = pathlib.Path(arguments.root)
    results = []
    summary = {}

    def record(label, passed, detail=""):
        results.append({"check": label, "passed": bool(passed), "detail": detail})
        print(("PASS " if passed else "FAIL ") + label + (": " + detail if detail else ""))

    sys.modules["unreal"] = mock.MagicMock()
    material = load_script(root, "editor_make_fluid_foliage_material")
    record("재질 스크립트 unreal 스텁 임포트", True)
    stage_path = root / "Tools" / "FluidFX" / "editor_make_fluid_foliage_stage.py"
    if stage_path.is_file():
        stage = load_script(root, "editor_make_fluid_foliage_stage")
        layout_problems = stage.validate_stage_layout()
        record("무대 배치(구역 간격·바닥 안·흐름 액터 1·원천 태그)", not layout_problems, "; ".join(layout_problems))
    else:
        record("무대 스크립트 unreal 스텁 임포트", False, "editor_make_fluid_foliage_stage.py 없음")

    shader_folder = root / "Tools" / "FluidFX" / "Shaders"
    text_failures = []
    for name in (material.COPY_HLSL_NAME, material.BEND_HLSL_NAME):
        text_failures += [f"{name}: {item}" for item in run_shader_text_checks((shader_folder / name).read_text(encoding="utf-8"))]
    record("셰이더 텍스트(주석·절대 경로·ASCII·LF)", not text_failures, "; ".join(text_failures))
    copy_code = material.assemble_node_code(material.read_shader_source(str(root), material.COPY_HLSL_NAME))
    bend_code = material.assemble_node_code(material.read_shader_source(str(root), material.BEND_HLSL_NAME))
    problems = material.validate_hlsl_code(copy_code, material.COPY_INPUT_NAMES, material.COPY_SAMPLER_NAMES)
    problems += material.validate_hlsl_code(bend_code, material.BEND_INPUT_NAMES, material.BEND_SAMPLER_NAMES, material.BEND_ADDITIONAL_OUTPUT_NAMES)
    problems += [f"입력 원천 없음: {name}" for name in material.find_unresolved_inputs()]
    record("조립 코드 자체 점검(입력·샘플러·추가 출력·normalize( 금지)", not problems, "; ".join(problems))
    missing_anchors = [anchor for anchor in COPY_ANCHORS if normalize(anchor) not in normalize(copy_code)]
    missing_anchors += [anchor for anchor in BEND_ANCHORS if normalize(anchor) not in normalize(bend_code)]
    record("식 고정점(M1 속도 기준·M2 안전 정규화·θ=0 분기·창 관문)", not missing_anchors, "; ".join(missing_anchors))
    speed_default = dict(material.COPY_DRIVEN_SCALAR_DEFAULTS).get("VelocitySpeedReferenceCmPerSec")
    record("복사 마스터 VelocitySpeedReferenceCmPerSec = 600(M1)", speed_default == 600.0, str(speed_default))

    entries, overlap = collect_look_entries(root, arguments.look_fragment)
    override_problems = material.find_instance_override_problems(entries)
    record("MI 이름 계약(부모·C++ 구동 이름 없음·구획)", not override_problems, "; ".join(override_problems))
    record("조각과 정본 키 서로소", not overlap or not arguments.look_fragment, ", ".join(overlap))

    outcomes_first, digest_first = run_mirror_tests(material)
    _, digest_second = run_mirror_tests(material)
    for label, (passed, detail, value_sha) in outcomes_first.items():
        record(f"거울 {label}", passed, f"{detail} sha256 {value_sha[:16]}")
    record("거울 시험 2회 sha256 같음", digest_first == digest_second, digest_first)
    summary["mirror_sha256"] = digest_first

    dxc = find_dxc(arguments.dxc)
    if not dxc:
        record("dxc 찾기", False, "Windows Kits dxc.exe 없음(--dxc 로 지정)")
        return finish(results, summary, arguments.output)
    record("dxc 찾기", True, dxc)
    master_folded = dict(material.BEND_LOOK_SCALAR_DEFAULTS) | {"FoliageFlowState": (0.0, 0.0, 0.0, 0.0)}
    jobs = []
    for optimize in (False, True):
        suffix = "O3" if optimize else "Od"
        jobs.append((f"copy_ps_{suffix}", build_copy_source(material, copy_code, {}), "ps_6_0", optimize, 2, None))
        jobs.append((f"bend_vs_{suffix}", build_bend_source(material, bend_code, {}, "vs"), "vs_6_0", optimize, 1, None))
        jobs.append((f"bend_ps_{suffix}", build_bend_source(material, bend_code, {}, "ps"), "ps_6_0", optimize, 1, None))
    jobs.append(("copy_ps_master_O3", build_copy_source(material, copy_code, dict(material.COPY_LOOK_SCALAR_DEFAULTS)), "ps_6_0", True, 0, MASTER_FOLDED_INSTRUCTION_LIMIT))
    jobs.append(("bend_vs_master_O3", build_bend_source(material, bend_code, master_folded, "vs"), "vs_6_0", True, 0, MASTER_FOLDED_INSTRUCTION_LIMIT))
    compiles = {}
    with tempfile.TemporaryDirectory() as folder:
        work_folder = pathlib.Path(folder)
        for label, source, profile, optimize, minimum_samples, instruction_limit in jobs:
            outcome = compile_source(dxc, source, label, profile, optimize, work_folder)
            folded = instruction_limit is None or (outcome["samples"] == 0 and outcome["instructions"] <= instruction_limit)
            suffix_label = " (마스터 값 접기 = 상수 출력)" if instruction_limit is not None else ""
            record(f"dxc {label} {profile}{suffix_label}", outcome["exit"] == 0 and outcome["samples"] >= minimum_samples and folded, outcome["error"] or f"표본 {outcome['samples']} 명령 {outcome['instructions']}")
            compiles[label] = outcome
    summary["dxc"] = {label: {"samples": item["samples"], "instructions": item["instructions"]} for label, item in compiles.items()}
    return finish(results, summary, arguments.output)


def finish(results, summary, output_path):
    failed = [item["check"] for item in results if not item["passed"]]
    report = {"passed": len(results) - len(failed), "failed": failed, **summary}
    if output_path:
        pathlib.Path(output_path).write_text(json.dumps(report, ensure_ascii=False, indent=1) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps(report, ensure_ascii=False))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
