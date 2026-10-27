"""실행 환경: 시스템 Python 3(numpy·Pillow) 와 Windows Kits dxc.exe(ps_6_0), 에디터 불필요
실행 명령: python Tools/FluidFX/check_fluid_hvol_hlsl.py [--root 프로젝트경로] [--dxc dxc경로] [--look-fragment 조각.json] [--output 요약.json]
출력: 항목별 PASS/FAIL 줄과 마지막 줄 JSON 요약(dxc 명령·표본 수, 거울 시험 sha256), 하나라도 실패하면 종료 코드 1
상태: 미검증(2026-10-26 FX-9 묶음 E 작성). TDFluidVolumeDensityCommon.hlsl + TDFluidHeterogeneousVolume.hlsl 을 재질 스크립트와 같은 조립 함수로 조립해 텍스트·식 고정점(M4 오프셋, P-04 단면 v)·이름 계약(MI 덮어쓰기에 C++ 구동·런타임 이름 R7 없음)·안개 파일 창 UV·탭 규약 대조(위험 7·X-17)·dxc ps_6_0 O3/Od 와 역할(구동·수동)×단면 모드(0~3) 상수 접기 순열을 보고, 무대 배치·굽기 요청 초안 검사, numpy 거울 V1 방사 단면 회전 불변·V2 무효 지형 = 바닥·V3 0·극값 NaN 0·V4 대칭 두께 = T·V5 근·원 페이드 합 1·V6 흑체 R/B 단조·V7 M4 오프셋 창 중심·V8 P-04 단면 규약·V9 높이 캐시 부호화 왕복을 두 번 돌려 sha256 이 같은지 본다.
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
#define Texture3DSampleLevel(T, S, UV, L) T.SampleLevel(S, UV, L)
"""
SAMPLE_PATTERN = re.compile(r"call \S+ @dx\.op\.(?:sample|sampleLevel|sampleBias|sampleGrad)\.")
INSTRUCTION_PATTERN = re.compile(r"^\s+(?:%\S+ = |call |store |br |ret )", re.MULTILINE)
ABSOLUTE_PATH_PATTERN = re.compile(r"[A-Za-z]:[\\/]")
RUNTIME_NAME_PATTERN = re.compile(r'Set\w*ParameterValue\w*\(\s*TEXT\("([^"]+)"\)')
PLANNED_RUNTIME_NAMES = ("TerrainHeightTexture", "TerrainHeightWindow", "TerrainHeightDecode", "VolumeHeightCm", "VolumeOffsetCm")
REQUIRED_ANCHORS = (
    "- float3(VolumeOffsetCm.xy, tdVolIsSymmetric * VolumeOffsetCm.z);",
    "float3 tdVolQ = lerp(VolWorldPosition, tdVolPlanePosition, tdVolIsDriven);",
    "#define TD_VOL_WINDOW_UV(Q_XY, CENTER_XY, SIZE_XY) (((Q_XY) - (CENTER_XY)) / max((SIZE_XY), float2(1.0, 1.0)) + 0.5)",
    "#define TD_VOL_SIM_DENSITY(TAP) (saturate((TAP).r) * saturate((TAP).a))",
    "float tdVolSectionV = saturate(tdVolHeightCm / max(VolCrossSectionHeightCm, 1.0));",
    "CrossSectionTextureSampler, float2(tdVolSectionU, tdVolSectionV), 0)",
    "float tdVolHeightCm = tdVolIsSymmetric > 0.5 ? abs(tdVolQ.z - tdVolCenter.z) : max(tdVolQ.z - tdVolFloorCm, 0.0);",
    "float tdVolTopCm = tdVolIsSymmetric > 0.5 ? 0.5 * tdVolExtrudeCm : tdVolExtrudeCm;",
    "tdVolShape = tdVolSectionMode < 2.5 ? tdVolSource * tdVolSection : tdVolSection;",
)
FORBIDDEN_TOKENS = ("1.0 - tdVolSectionV", "VolumeHeightCm", "FogWorldPosition", "tdVolQ.z - VolumeOffsetCm.z", "LoadObject")
FOG_ANCHORS = ("/ fogWindowSize + 0.5", "saturate(fogSimTap.r) * saturate(fogSimTap.a)")
HVOL_SYNC_ANCHORS = ("/ max((SIZE_XY), float2(1.0, 1.0)) + 0.5", "(saturate((TAP).r) * saturate((TAP).a))")
FOLD_ROLES = ((0.0, "passive"), (256.0, "driven"))
FOLD_MODES = (0.0, 1.0, 2.0, 3.0)
MIRROR_LABELS = ("V1 방사 단면 회전 불변", "V2 무효 지형 = 바닥", "V3 0·극값 NaN 0", "V4 대칭 두께 = T", "V5 근·원 페이드 합 1", "V6 흑체 R/B 단조", "V7 M4 오프셋 창 중심", "V8 P-04 단면 v 규약", "V9 높이 캐시 부호화 왕복")


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


def declare_inputs(material, folded):
    lines = []
    for name in material.TEXTURE_PARAMETER_NAMES:
        lines.append(f"{'Texture3D' if name in material.VOLUME_TEXTURE_NAMES else 'Texture2D'} {name};")
        lines.append(f"SamplerState {name}Sampler;")
    constants = "".join(f"static const float {name} = {value};\n" for name, value in folded.items())
    scalars = []
    for name in material.CUSTOM_INPUT_NAMES:
        if name in material.TEXTURE_PARAMETER_NAMES or name in folded:
            continue
        kind = "float4" if name in material.FLOAT4_VECTOR_NAMES else ("float3" if name in material.VECTOR_INPUT_NAMES else "float")
        scalars.append(f"{kind} {name};")
    return "\n".join(lines) + "\n" + constants + "cbuffer Inputs {\n" + "\n".join(scalars) + "\n}\n"


def build_source(material, code, folded):
    outputs = ", ".join(f"inout {'float3' if kind == 'CMOT_FLOAT3' else 'float'} {name}" for name, kind in material.OUTPUT_SPECS)
    body = f"float4 TDVolDensity({outputs})\n{{\n{code}\n}}\n"
    entry = "float4 main(float4 sv : SV_Position) : SV_Target\n{\nfloat3 albedoValue = 0.0;\nfloat3 emissiveValue = 0.0;\nfloat occlusionValue = 0.0;\nfloat4 densityValue = TDVolDensity(albedoValue, emissiveValue, occlusionValue);\nreturn densityValue + float4(albedoValue + emissiveValue, occlusionValue);\n}\n"
    return HEADER + declare_inputs(material, folded) + body + entry


def compile_source(dxc, source, label, optimize, work_folder):
    path = work_folder / f"{label}.hlsl"
    disassembly = work_folder / f"{label}.dxil.txt"
    path.write_text(source, encoding="utf-8")
    command = [dxc, "-T", "ps_6_0", "-E", "main", str(path), "-Fc", str(disassembly), "-Fo", str(work_folder / f"{label}.bin"), "-O3" if optimize else "-Od"]
    completed = subprocess.run(command, capture_output=True, text=True)
    text = disassembly.read_text(encoding="utf-8", errors="replace") if disassembly.exists() else ""
    first_error = next((line for line in (completed.stderr + completed.stdout).splitlines() if "error" in line), "")
    return {"exit": completed.returncode, "samples": len(SAMPLE_PATTERN.findall(text)), "instructions": len(INSTRUCTION_PATTERN.findall(text)), "error": first_error}


def smoothstep(edge0, edge1, x):
    t = np.clip((np.asarray(x, dtype=np.float64) - edge0) / np.maximum(edge1 - edge0, 1e-30), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def window_uv(q_xy, center_xy, size_xy):
    return (np.asarray(q_xy) - np.asarray(center_xy)) / np.maximum(np.asarray(size_xy, dtype=np.float64), 1.0) + 0.5


def edge_distance(uv, size_xy):
    size = np.maximum(np.asarray(size_xy, dtype=np.float64), 1.0)
    edge = (0.5 - np.abs(uv - 0.5)) * size
    return np.minimum(edge[..., 0], edge[..., 1])


def layer_profile(top, fade, height):
    top = np.asarray(top, dtype=np.float64)
    profile = 1.0 - smoothstep(top - max(fade, 1.0), top, height)
    return np.where(top > 0.0, profile, 0.0)


def radial_u(radius, limit, exponent):
    return np.power(np.clip(radius / max(limit, 1.0), 0.0, 1.0), max(exponent, 0.01))


def camera_fade(distance, start, length, invert):
    if length <= 0.0:
        return np.ones_like(distance)
    ramp = smoothstep(start, start + max(length, 0.001), distance)
    return 1.0 - ramp if invert else ramp


def fog_camera_fade(distance, start, length, invert):
    ramp = smoothstep(start, start + max(length, 0.001), distance)
    return (1.0 - ramp if invert > 0.5 else ramp) if length > 0.0 else np.ones_like(distance)


def blackbody(temperature_k):
    temperature_k = np.asarray(temperature_k, dtype=np.float64)
    t = np.maximum(temperature_k * 0.01, 1.0)
    red = np.where(t <= 66.0, 1.0, np.clip(1.29293618606 * np.power(np.maximum(t - 60.0, 0.001), -0.1332047592), 0.0, 1.0))
    green = np.where(t <= 66.0, np.clip(0.39008157876 * np.log(np.maximum(t, 1.0)) - 0.63184144378, 0.0, 1.0), np.clip(1.12989086089 * np.power(np.maximum(t - 60.0, 0.001), -0.0755148492), 0.0, 1.0))
    blue = np.where(t >= 66.0, 1.0, np.where(t <= 19.0, 0.0, np.clip(0.54320678911 * np.log(np.maximum(t - 10.0, 0.001)) - 1.19625408914, 0.0, 1.0)))
    color = np.power(np.stack([red, green, blue], axis=-1), 2.2)
    return (temperature_k >= 1000.0)[..., None] * color


def sample_bilinear(image, u, v):
    size_v, size_u = image.shape
    x = np.clip(np.asarray(u) * size_u - 0.5, 0.0, size_u - 1.0)
    y = np.clip(np.asarray(v) * size_v - 0.5, 0.0, size_v - 1.0)
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    x1, y1 = np.minimum(x0 + 1, size_u - 1), np.minimum(y0 + 1, size_v - 1)
    fx, fy = x - x0, y - y0
    top = image[y0, x0] * (1.0 - fx) + image[y0, x1] * fx
    bottom = image[y1, x0] * (1.0 - fx) + image[y1, x1] * fx
    return top * (1.0 - fy) + bottom * fy


def plane_position(p, axis_u, axis_v, axis_n, coordinate_offset, volume_offset, is_symmetric):
    projected = np.stack([p @ axis_u, p @ axis_v, p @ axis_n], axis=-1)
    return projected + np.array([coordinate_offset[0], coordinate_offset[1], 0.0]) - np.array([volume_offset[0], volume_offset[1], volume_offset[2] if is_symmetric else 0.0])


def terrain_floor(floor_cm, terrain_uv, terrain_cm, decode_valid, blend, max_drop_cm):
    if decode_valid <= 0.5 or blend <= 0.0:
        return np.full_like(np.asarray(terrain_cm, dtype=np.float64), floor_cm)
    inside = (np.minimum(np.minimum(terrain_uv[..., 0], terrain_uv[..., 1]), np.minimum(1.0 - terrain_uv[..., 0], 1.0 - terrain_uv[..., 1])) >= 0.0).astype(np.float64)
    valid = inside * (np.asarray(terrain_cm) >= floor_cm - max(max_drop_cm, 0.0))
    return floor_cm + (np.asarray(terrain_cm) - floor_cm) * np.clip(blend, 0.0, 1.0) * valid


def full_density(source, height_cm, size_xy, edge_fade_cm, extrude_cm, exponent, top_fade_cm, symmetric, distance_cm, fade_start, fade_length):
    uv = np.full(np.shape(source) + (2,), 0.5)
    edge = edge_distance(uv, size_xy)
    extrude = extrude_cm * np.power(np.clip(source, 0.0, 1.0), max(exponent, 0.01))
    top = 0.5 * extrude if symmetric else extrude
    profile = layer_profile(top, top_fade_cm, height_cm)
    occlusion = 0.35 + 0.65 * np.clip(height_cm / np.maximum(top, 1.0), 0.0, 1.0)
    return source * profile * smoothstep(0.0, max(edge_fade_cm, 1.0), edge) * camera_fade(distance_cm, fade_start, fade_length, True), occlusion


def test_radial_rotation(sections):
    mushroom = sections["Mushroom"]
    radii = np.linspace(0.0, 400.0, 81)
    heights = np.linspace(0.0, 320.0, 33)
    angles = np.arange(8) * (np.pi / 4.0) + 0.3
    values = []
    for angle in angles:
        rotation = np.array([[np.cos(angle), -np.sin(angle)], [np.sin(angle), np.cos(angle)]])
        offsets = np.stack([radii, np.zeros_like(radii)], axis=-1) @ rotation.T
        radius = np.linalg.norm(offsets, axis=-1)
        u = radial_u(radius, 350.0, 1.7)[None, :]
        v = np.clip(heights / 300.0, 0.0, 1.0)[:, None]
        values.append(sample_bilinear(mushroom, np.broadcast_to(u, (heights.size, radii.size)), np.broadcast_to(v, (heights.size, radii.size))))
    values = np.stack(values)
    error = float(np.abs(values - values[0]).max())
    return error <= 1e-3, f"8각 최대 차 {error:.2e}", values


def test_invalid_terrain():
    floor_cm = 100.0
    uv_inside = np.array([[0.5, 0.5]])
    uv_outside = np.array([[1.2, 0.5]])
    miss = float(terrain_floor(floor_cm, uv_inside, np.array([-1e9]), 1.0, 1.0, 2000.0)[0])
    valid = float(terrain_floor(floor_cm, uv_inside, np.array([40.0]), 1.0, 1.0, 2000.0)[0])
    outside = float(terrain_floor(floor_cm, uv_outside, np.array([40.0]), 1.0, 1.0, 2000.0)[0])
    sky = float(terrain_floor(floor_cm, uv_inside, np.array([-1e6 * 1.0 + 2000.0]), 1.0, 1.0, 2000.0)[0])
    off = float(terrain_floor(floor_cm, uv_inside, np.array([40.0]), 0.0, 1.0, 2000.0)[0])
    passed = miss == floor_cm and valid == 40.0 and outside == floor_cm and sky == floor_cm and off == floor_cm
    return passed, f"미스 {miss}, 유효 {valid}, 창 밖 {outside}, 하늘 깊이 {sky}, 해독 끔 {off}", np.array([miss, valid, outside, sky, off])


def test_extremes_finite():
    outputs = []
    for size in ((0.0, 0.0), (1.0, 1.0), (700.0, 700.0), (1e6, 1e6)):
        for edge_fade in (0.0, 40.0):
            for exponent in (0.0, 1.0, 8.0):
                for extrude in (0.0, 1e5):
                    for symmetric in (False, True):
                        source = np.array([0.0, 1e-7, 0.5, 1.0])
                        density, occlusion = full_density(source, np.array([0.0, 0.0, 50.0, 1e6]), size, edge_fade, extrude, exponent, 0.0, symmetric, np.array([0.0, 1.0, 1e6, 0.0]), 0.0, 0.0)
                        outputs.extend([density, occlusion])
    radial = radial_u(np.array([0.0, 1e-9, 1e9]), 0.0, 0.0)
    polar_angle = np.arctan2(0.0, 0.0)
    heat = blackbody(np.array([0.0, 999.0, 1000.0, 1e6])) * np.power(np.array([0.0, 0.0, 1.0, 1.0]), 0.01)[:, None]
    stacked = np.concatenate([np.concatenate(outputs), radial, heat.ravel(), [polar_angle]])
    finite = bool(np.isfinite(stacked).all())
    return finite, f"값 {stacked.size}개 유한 {finite}", stacked


def test_symmetric_thickness():
    heights = np.linspace(-300.0, 300.0, 60001)
    extrude = 200.0
    profile = layer_profile(0.5 * extrude, 1.0, np.abs(heights))
    support = heights[profile > 0.0]
    thickness = float(support.max() - support.min())
    step = float(heights[1] - heights[0])
    return abs(thickness - extrude) <= 2.0 * step, f"두께 {thickness:.3f} cm vs T {extrude}", profile


def test_complementary_fade():
    distance = np.linspace(0.0, 10000.0, 10001)
    hvol = camera_fade(distance, 3200.0, 1800.0, True)
    fog = fog_camera_fade(distance, 3200.0, 1800.0, 0.0)
    error = float(np.abs(hvol + fog - 1.0).max())
    return error <= 1e-12, f"HVOL(반전 1)+안개(반전 0) 오차 {error:.2e}", np.stack([hvol, fog])


def test_blackbody():
    temperatures = np.linspace(2000.0, 40000.0, 3801)
    color = blackbody(temperatures)
    ratio = color[:, 0] / np.maximum(color[:, 2], 1e-9)
    monotone = bool(np.all(np.diff(ratio) <= 1e-12))
    below = float(np.abs(blackbody(np.array([0.0, 500.0, 999.0]))).max())
    in_range = bool(((color >= 0.0) & (color <= 1.0)).all())
    return monotone and below == 0.0 and in_range, f"R/B 비증가 {monotone}, 1000 K 미만 최대 {below}, 범위 {in_range}", color


def unproject(center_plane, axis_u, axis_v, axis_n, coordinate_offset):
    return axis_u * (center_plane[0] - coordinate_offset[0]) + axis_v * (center_plane[1] - coordinate_offset[1]) + axis_n * center_plane[2]


def test_offset_window_center():
    identity = np.eye(3)
    owner_z, offset, height = 50.0, np.array([0.0, 900.0, 30.0]), 300.0
    center = np.array([1200.0, -400.0, owner_z + offset[2]])
    world_point = np.array([[center[0] + offset[0], center[1] + offset[1], owner_z + offset[2] + 0.5 * height]])
    q = plane_position(world_point, identity[0], identity[1], identity[2], (0.0, 0.0), offset, False)[0]
    uv = window_uv(q[:2], center[:2], (700.0, 700.0))
    world_height = float(max(q[2] - center[2], 0.0))
    angle = 0.7
    axis_u = np.array([np.cos(angle), np.sin(angle), 0.0])
    axis_n = np.array([-np.sin(angle) * 0.6, np.cos(angle) * 0.6, 0.8])
    axis_n /= np.linalg.norm(axis_n)
    axis_u = axis_u - axis_n * (axis_u @ axis_n)
    axis_u /= np.linalg.norm(axis_u)
    axis_v = np.cross(axis_n, axis_u)
    coordinate_offset = (12.0, -7.0)
    plane_center = np.array([310.0, 95.0, 640.0])
    plane_point = unproject(plane_center, axis_u, axis_v, axis_n, coordinate_offset) + axis_u * offset[0] + axis_v * offset[1] + axis_n * offset[2]
    plane_q = plane_position(plane_point[None, :], axis_u, axis_v, axis_n, coordinate_offset, offset, True)[0]
    plane_uv = window_uv(plane_q[:2], plane_center[:2], (700.0, 700.0))
    plane_height = float(abs(plane_q[2] - plane_center[2]))
    errors = np.array([np.abs(uv - 0.5).max(), abs(world_height - 0.5 * height), np.abs(plane_uv - 0.5).max(), plane_height])
    passed = float(errors.max()) <= 1e-6
    return passed, f"세계 uv 차 {errors[0]:.1e}·높이 차 {errors[1]:.1e}(Q.z 이중 차감이면 {offset[2]}), 평면 uv 차 {errors[2]:.1e}·두께 중심 차 {errors[3]:.1e}", errors


def test_cross_section_convention(sections):
    mushroom, ring = sections["Mushroom"], sections["Ring"]
    floor_axis = float(sample_bilinear(mushroom, np.array([0.0]), np.array([0.0]))[0])
    above_top = float(sample_bilinear(mushroom, np.array([0.0]), np.array([1.0]))[0])
    ring_empty = float(sample_bilinear(ring, np.array([0.0]), np.linspace(0.0, 1.0, 11)).max())
    radial_edge = float(sample_bilinear(mushroom, np.array([1.0]), np.linspace(0.0, 1.0, 11)).max())
    passed = floor_axis > 0.5 and above_top == 0.0 and ring_empty == 0.0 and radial_edge <= 1e-3
    return passed, f"바닥 축 {floor_axis:.3f}(위 행 = 바닥), v=1 {above_top}, 링 u=0 최대 {ring_empty}, 반경 끝 최대 {radial_edge:.1e}", np.array([floor_axis, above_top, ring_empty, radial_edge])


def test_height_encoding(bake):
    rng = np.random.default_rng(9)
    min_z, range_cm = -1234.5, 100000.0
    heights = min_z + rng.random(4096) * range_cm
    decoded = np.array([bake.decode_height(bake.encode_height(float(z), min_z, range_cm), min_z, range_cm) for z in heights])
    error = float(np.abs(decoded - heights).max())
    bound = bake.quantization_bound_cm(range_cm)
    ends = (bake.decode_height(1, min_z, range_cm), bake.decode_height(65535, min_z, range_cm))
    passed = error <= bound + 1e-9 and bake.encode_height(None, min_z, range_cm) == 0 and bake.decode_height(0, min_z, range_cm) is None and abs(ends[0] - min_z) < 1e-9 and abs(ends[1] - (min_z + range_cm)) < 1e-6
    return passed, f"왕복 최대 {error:.4f} cm ≤ 한계 {bound:.4f} cm(1000 m), 미스 0, 양 끝 {ends}", decoded


def run_mirror_tests(sections, bake):
    tests = (
        lambda: test_radial_rotation(sections), test_invalid_terrain, test_extremes_finite, test_symmetric_thickness, test_complementary_fade,
        test_blackbody, test_offset_window_center, lambda: test_cross_section_convention(sections), lambda: test_height_encoding(bake),
    )
    outcomes = {}
    digest = hashlib.sha256()
    for label, test in zip(MIRROR_LABELS, tests):
        passed, detail, values = test()
        values = np.ascontiguousarray(np.asarray(values, dtype=np.float64))
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


def collect_runtime_names(root):
    names = {name.casefold(): "계획(spec-fx9 2.5)" for name in PLANNED_RUNTIME_NAMES}
    source_root = root / "Source" / "TDGame" / "FluidFX"
    for path in sorted(source_root.rglob("*")):
        if path.suffix not in (".cpp", ".h") or "Tests" in path.parts:
            continue
        for name in RUNTIME_NAME_PATTERN.findall(path.read_text(encoding="utf-8", errors="replace")):
            names.setdefault(name.casefold(), path.relative_to(root).as_posix())
    return names


def collect_look_entries(root, fragment_path):
    canonical_entries = {}
    canonical = root / "Content" / "FluidFX" / "PresetData" / "TDFluidMaterialLook.json"
    if canonical.is_file():
        canonical_entries = json.loads(canonical.read_text(encoding="utf-8")).get("Materials", {})
    fragment_entries = {}
    if fragment_path and pathlib.Path(fragment_path).is_file():
        fragment_entries = json.loads(pathlib.Path(fragment_path).read_text(encoding="utf-8")).get("Materials", {})
    entries = dict(canonical_entries)
    entries.update(fragment_entries)
    return entries, fragment_entries, sorted(set(fragment_entries) & set(canonical_entries))


def find_runtime_name_hits(fragment_entries, runtime_names):
    hits = []
    for path, entry in fragment_entries.items():
        for section in ("Scalars", "Vectors", "Switches"):
            for name in entry.get(section, {}):
                source = runtime_names.get(name.casefold())
                if source:
                    hits.append(f"{path.rsplit('/', 1)[-1]}.{section}.{name}({source})")
    return hits


def default_fragment_path(root):
    candidate = root.parent / "TDGame" / "Saved" / "AgentOps" / "20261026" / "wave2" / "look-fragment-fx9.json"
    return str(candidate) if candidate.is_file() else ""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parents[2]))
    parser.add_argument("--dxc", default="")
    parser.add_argument("--look-fragment", default="")
    parser.add_argument("--output", default="")
    arguments = parser.parse_args()
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    root = pathlib.Path(arguments.root)
    fragment_path = arguments.look_fragment or default_fragment_path(root)
    results = []
    summary = {"look_fragment": fragment_path}

    def record(label, passed, detail=""):
        results.append({"check": label, "passed": bool(passed), "detail": detail})
        print(("PASS " if passed else "FAIL ") + label + (": " + detail if detail else ""))

    sys.modules["unreal"] = mock.MagicMock()
    material = load_script(root, "editor_make_fluid_heterogeneous_volume_material")
    record("재질 스크립트 unreal 스텁 임포트", True)
    stage = load_script(root, "editor_make_fluid_hvol_stage")
    bake = load_script(root, "editor_bake_fluid_height_cache")
    layout_problems = stage.validate_stage_layout()
    record("무대 배치(구역 9·간격·창 겹침·오프셋·동굴·경사·그림자 블록·MI 7개 사용)", not layout_problems, "; ".join(layout_problems))
    draft, draft_problems = bake.validate_request(stage.height_cache_request_draft())
    record("무대 보고의 6c 굽기 요청 초안이 굽기 요청 검사 통과(A-02)", not draft_problems, "; ".join(draft_problems) or f"해상도 {draft['resolution']}")
    bad_requests = (
        {},
        {**stage.height_cache_request_draft(), "top_z": -5000.0},
        {**stage.height_cache_request_draft(), "texel_cm": 1.0},
        {**stage.height_cache_request_draft(), "name": "bad name"},
        {**stage.height_cache_request_draft(), "extent": [1e6, 1e6]},
        {**stage.height_cache_request_draft(), "verify": "yes"},
    )
    rejected = [bool(bake.validate_request(request)[1]) for request in bad_requests]
    record("굽기 요청 잘못된 입력 6건 거부", all(rejected), str(rejected))
    generator = load_script(root, "make_fluid_cross_section_textures")
    sections = generator.make_sections(generator.DEFAULT_SEED)
    section_problems = generator.check_sections(sections)
    record("단면 생성기 자체 검사(P-04 규약·높이 초과 행 0·링 0열 0)", not section_problems, "; ".join(section_problems))

    shader_root = root / "Tools" / "FluidFX" / "Shaders"
    for name in (material.COMMON_HLSL_NAME, material.BODY_HLSL_NAME):
        failures = run_shader_text_checks((shader_root / name).read_text(encoding="utf-8"))
        record(f"셰이더 텍스트 {name}(주석·절대 경로·ASCII·LF)", not failures, "; ".join(failures))
    code = material.assemble_node_code(*material.read_shader_sources(str(root)))
    problems = material.validate_hlsl_code(code) + [f"입력 원천 없음: {name}" for name in material.find_unresolved_inputs()]
    record("조립 코드 자체 점검(입력·샘플러·추가 출력 사용, 접두 TD_VOL_·tdVol)", not problems, "; ".join(problems))
    normalized = normalize(code)
    missing_anchors = [anchor for anchor in REQUIRED_ANCHORS if normalize(anchor) not in normalized]
    forbidden = [token for token in FORBIDDEN_TOKENS if token in code]
    record("식 고정점(M4 O = VolumeOffsetCm·Q.z 이중 차감 없음, P-04 단면 v, 대칭 두께)·금지 토큰", not missing_anchors and not forbidden, "; ".join(missing_anchors + forbidden))

    fog_path = shader_root / "TDFluidFogVolume.hlsl"
    fog_text = normalize(fog_path.read_text(encoding="utf-8")) if fog_path.is_file() else ""
    fog_missing = [anchor for anchor in FOG_ANCHORS if normalize(anchor) not in fog_text]
    hvol_missing = [anchor for anchor in HVOL_SYNC_ANCHORS if normalize(anchor) not in normalized]
    record("안개 파일과 창 UV·밀도 탭 규약 같음(위험 7·X-17, 다르면 HVOL 동기화 필요)", not fog_missing and not hvol_missing, "; ".join([f"안개 없음: {item}" for item in fog_missing] + [f"HVOL 없음: {item}" for item in hvol_missing]))

    entries, fragment_entries, overlap = collect_look_entries(root, fragment_path)
    override_problems = material.find_instance_override_problems(entries)
    record("MI 이름 계약(부모·마스터 이름·C++ 구동·조건부 이름 없음, MI 7개)", not override_problems, "; ".join(override_problems))
    record("조각과 정본 키 서로소", not overlap, ", ".join(overlap))
    runtime_hits = find_runtime_name_hits(fragment_entries, collect_runtime_names(root))
    record("조각에 C++ 런타임 이름 없음(R7, TerrainHeight* 계획 이름 포함)", not runtime_hits, "; ".join(runtime_hits))
    extra_entries = sorted(path for path in fragment_entries if path.rsplit("/", 1)[-1] not in material.INSTANCE_NAMES)
    record("조각 항목 = HVOL MI 7개만", not extra_entries and len(fragment_entries) == len(material.INSTANCE_NAMES), ", ".join(extra_entries) or f"{len(fragment_entries)}개")

    outcomes_first, digest_first = run_mirror_tests(sections, bake)
    outcomes_second, digest_second = run_mirror_tests(generator.make_sections(generator.DEFAULT_SEED), bake)
    for label, (passed, detail, value_sha) in outcomes_first.items():
        record(f"거울 {label}", passed, f"{detail} sha256 {value_sha[:16]}")
    record("거울 시험 2회 sha256 같음", digest_first == digest_second, digest_first)
    summary["mirror_sha256"] = digest_first
    summary["hlsl_sha256"] = hashlib.sha256(code.encode("utf-8")).hexdigest()

    dxc = find_dxc(arguments.dxc)
    if not dxc:
        record("dxc 찾기", False, "Windows Kits dxc.exe 없음(--dxc 로 지정)")
        return finish(results, summary, arguments.output)
    record("dxc 찾기", True, dxc)
    compiles = {}
    with tempfile.TemporaryDirectory() as folder:
        work_folder = pathlib.Path(folder)
        for optimize in (False, True):
            suffix = "O3" if optimize else "Od"
            outcome = compile_source(dxc, build_source(material, code, {}), f"hvol_{suffix}", optimize, work_folder)
            record(f"dxc HVOL ps_6_0 {suffix}", outcome["exit"] == 0 and outcome["samples"] >= 5, outcome["error"] or f"표본 {outcome['samples']} 명령 {outcome['instructions']}")
            compiles[suffix] = outcome
        for role_value, role_label in FOLD_ROLES:
            for mode_value in FOLD_MODES:
                label = f"{role_label}_section{int(mode_value)}"
                outcome = compile_source(dxc, build_source(material, code, {"SimGridResolution": role_value, "VolCrossSectionMode": mode_value}), label, True, work_folder)
                record(f"dxc 순열 {label} O3", outcome["exit"] == 0, outcome["error"] or f"표본 {outcome['samples']} 명령 {outcome['instructions']}")
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
