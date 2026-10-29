"""실행 환경: 시스템 Python 3(numpy) 와 Windows Kits dxc.exe(vs_6_0·ps_6_0), 에디터 불필요
실행 명령: python Tools/FluidFX/check_fluid_viscous_hlsl.py [--root 프로젝트경로] [--dxc dxc경로] [--look-fragment 조각.json] [--output 요약.json]
출력: 항목별 PASS/FAIL 줄과 마지막 줄 JSON 요약(dxc 명령·표본 수, 거울 시험 sha256), 하나라도 실패하면 종료 코드 1
상태: 미검증(2026-10-29 파동 3 FX-14 작성). TDFluidViscous{Common,Surface,Offset,Decal}.hlsl·TDFluidLavaSurface.hlsl 를 재질 스크립트와 같은 조립 함수로 조립해 텍스트·식 고정점·이름 계약(조각에 C++ 구동 이름 없음, 스칼라·벡터 구획, 용암 TileMap1 6개·S2 스칼라)·dxc(오프셋 vs_6_0, 표면·데칼·용암 ps_6_0, O3/Od)를 보고, numpy 거울 V1 마스크 경계·V2 높이·V3 법선 해석해·V4 창 경계와 구동 끔·데칼 불투명도·V5 용암 껍질과 발광을 두 번 돌려 sha256 이 같은지 본다.
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
MOLTEN_LITERAL_PATTERN = re.compile(r"(?<![\w.])(?:0\.05|0\.4)(?![\d])")
TEXTURE_INPUT_NAMES = ("DensityPressureTexture", "TileMap1Texture")
FLOAT3_INPUT_NAMES = (
    "ViscousPosition", "LavaPosition", "CameraWorldPosition", "SimCenterWorld", "SimExtentsCm", "TileMap1OriginCm",
    "TileMap1PeriodCm", "ViscousBaseColor", "ViscousSelectiveColor", "LavaEmissiveColor", "LavaCrustColor",
)
LAVA_TILE_SCALAR_NAMES = ("TileMap1HeightCm", "TileMap1DensityHeightCm", "TileMap1NormalStrength", "TileMap1FadeStartCm", "TileMap1FadeLengthCm", "TileMap1FoamStrength")
COMMON_ANCHORS = (
    "float viscousIsDriven = step(0.5, SimGridResolution);",
    "float2 viscousUv = (ViscousPosition.xy - viscousCenter) / viscousSize + 0.5;",
    "float viscousEdge = max(ViscousEdgeSoftness, 0.0001);",
    "float viscousExponent = clamp(ViscousHeightExponent, 0.1, 4.0);",
    "float viscousInside = TD_VISCOUS_INSIDE(viscousUv) * viscousIsDriven;",
    "float viscousHeightCm = TD_VISCOUS_HEIGHT(viscousTap, viscousCoverage) * viscousInside;",
    "smoothstep(ViscousMaskThreshold - viscousEdge, ViscousMaskThreshold + viscousEdge, (TAP).r)",
    "pow(saturate(((TAP).r - ViscousMaskThreshold) / max(1.0 - ViscousMaskThreshold, 0.001)), viscousExponent)",
    "ViscousPressureHeightCm * (2.0 * (TAP).g - 1.0) * (COVERAGE)",
)
SURFACE_ANCHORS = (
    "float viscousCellCm = viscousSize.x / max(SimGridResolution, 2.0);",
    "float2 viscousSlope = float2(viscousEastCm - viscousWestCm, viscousNorthCm - viscousSouthCm) / (2.0 * viscousCellCm) * ViscousNormalStrength;",
    "ViscousNormalWS = normalize(float3(-viscousSlope, 1.0));",
    "float viscousSelective = saturate(viscousTap.b / max(viscousTap.r, 0.001));",
    "return float4(viscousColor, viscousCoverage * saturate(viscousTap.a) * viscousInside);",
)
DECAL_ANCHORS = SURFACE_ANCHORS[:-1] + (
    "return float4(viscousColor, viscousCoverage * saturate(viscousTap.a) * viscousInside * saturate(ViscousDecalOpacity));",
)
LAVA_ANCHORS = (
    "float3 tileSamplePositionCm = LavaPosition;",
    "float lavaCrustEdge = max(LavaCrustSoftness, 0.0001);",
    "float lavaCrustTile = TileMap1PeriodCm.x > 0.0 ? smoothstep(LavaCrustThreshold - lavaCrustEdge, LavaCrustThreshold + lavaCrustEdge, tileMapFoam) : 1.0;",
    "float lavaCrust = saturate(lavaCrustTile * (1.0 - LavaLocalRevealStrength * lavaLocal));",
    "float lavaMolten = pow(saturate(1.0 - lavaCrust), max(LavaEmissiveExponent, 0.01));",
    "LavaNormalWS = normalize(float3(-tileMapSlope, 1.0));",
    "return float4(lerp(LavaEmissiveColor * LavaMoltenColorScale, LavaCrustColor, lavaCrust), lerp(LavaMoltenRoughness, LavaCrustRoughness, lavaCrust));",
    "tileMapFoam = tileMapCenter.r * TileMap1FoamStrength * tileMapFade;",
)
RNG_SEED = 1429
TOLERANCE = 1e-9


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


def normalize_text(text):
    return re.sub(r"\s+", " ", text)


def declare_inputs(input_names):
    lines = []
    for name in input_names:
        if name in TEXTURE_INPUT_NAMES:
            lines += [f"Texture2D {name};", f"SamplerState {name}Sampler;"]
    members = [f"{'float3' if name in FLOAT3_INPUT_NAMES else 'float'} {name};" for name in input_names if name not in TEXTURE_INPUT_NAMES]
    return "\n".join(lines) + "\ncbuffer Inputs {\n" + "\n".join(members) + "\n}\n"


def build_source(input_names, function_name, return_type, output_names, code, stage):
    parameters = ", ".join(f"out float3 {name}" for name in output_names)
    body = HEADER + declare_inputs(input_names) + f"{return_type} {function_name}({parameters})\n{{\n{code}\n}}\n"
    if stage == "vs":
        return body + f"float4 main(float3 position : POSITION) : SV_Position\n{{\nfloat3 offset = {function_name}();\nreturn float4(position + offset, 1.0);\n}}\n"
    locals_block = "".join(f"float3 {name} = float3(0.0, 0.0, 0.0);\n" for name in output_names)
    outputs_sum = " + ".join(output_names) if output_names else "float3(0.0, 0.0, 0.0)"
    return body + f"float4 main(float4 sv : SV_Position) : SV_Target\n{{\n{locals_block}float4 result = {function_name}({', '.join(output_names)});\nreturn result + float4({outputs_sum}, 0.0);\n}}\n"


def compile_source(dxc, source, label, profile, optimize, work_folder):
    path = work_folder / f"{label}.hlsl"
    disassembly = work_folder / f"{label}.dxil.txt"
    path.write_text(source, encoding="utf-8")
    command = [dxc, "-T", profile, "-E", "main", str(path), "-Fc", str(disassembly), "-Fo", str(work_folder / f"{label}.bin"), "-O3" if optimize else "-Od"]
    completed = subprocess.run(command, capture_output=True, text=True)
    text = disassembly.read_text(encoding="utf-8", errors="replace") if disassembly.exists() else ""
    first_error = next((line for line in (completed.stderr + completed.stdout).splitlines() if "error" in line), "")
    return {"exit": completed.returncode, "samples": len(SAMPLE_PATTERN.findall(text)), "instructions": len(INSTRUCTION_PATTERN.findall(text)), "error": first_error}


def saturate(x):
    return np.clip(x, 0.0, 1.0)


def smoothstep(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def fetch_nearest(texture, uv):
    size = texture.shape[0]
    index = np.clip(np.floor(saturate(uv) * size).astype(np.int64), 0, size - 1)
    return texture[index[..., 1], index[..., 0]]


def fetch_bilinear_wrap(texture, uv):
    height, width = texture.shape[:2]
    x = uv[..., 0] * width - 0.5
    y = uv[..., 1] * height - 0.5
    x0 = np.floor(x)
    y0 = np.floor(y)
    fx = (x - x0)[..., None]
    fy = (y - y0)[..., None]
    x0 = x0.astype(np.int64)
    y0 = y0.astype(np.int64)

    def tap(dx, dy):
        return texture[(y0 + dy) % height, (x0 + dx) % width]

    return (tap(0, 0) * (1 - fx) + tap(1, 0) * fx) * (1 - fy) + (tap(0, 1) * (1 - fx) + tap(1, 1) * fx) * fy


def inside_window(uv):
    return (uv[..., 0] >= 0.0) * (uv[..., 0] <= 1.0) * (uv[..., 1] >= 0.0) * (uv[..., 1] <= 1.0)


def normalize_rows(vectors):
    return vectors / np.linalg.norm(vectors, axis=-1, keepdims=True)


def viscous_height(tap, params):
    edge = max(params["ViscousEdgeSoftness"], 1e-4)
    exponent = float(np.clip(params["ViscousHeightExponent"], 0.1, 4.0))
    threshold = params["ViscousMaskThreshold"]
    coverage = smoothstep(threshold - edge, threshold + edge, tap[..., 0])
    shaped = np.power(saturate((tap[..., 0] - threshold) / max(1.0 - threshold, 0.001)), exponent)
    height = (params["ViscousHeightCm"] * shaped + params["ViscousPressureHeightCm"] * (2.0 * tap[..., 1] - 1.0) * coverage) * saturate(tap[..., 3])
    return height, coverage


def viscous_pixel(texture, position_xy, params, window, decal=False):
    driven = float(window["grid"] >= 0.5)
    size = np.maximum(window["extent"], 1.0)
    uv = (position_xy - window["center"]) / size + 0.5
    inside_factor = inside_window(uv) * driven
    tap = fetch_nearest(texture, uv)
    height, coverage = viscous_height(tap, params)
    height = height * inside_factor
    cell = size[0] / max(window["grid"], 2.0)

    def neighbor_height(offset):
        neighbor_uv = (position_xy + offset - window["center"]) / size + 0.5
        neighbor_height_cm, _ = viscous_height(fetch_nearest(texture, neighbor_uv), params)
        return neighbor_height_cm * inside_window(neighbor_uv) * driven

    east = neighbor_height(np.array([cell, 0.0]))
    west = neighbor_height(np.array([-cell, 0.0]))
    north = neighbor_height(np.array([0.0, cell]))
    south = neighbor_height(np.array([0.0, -cell]))
    slope = np.stack([east - west, north - south], axis=-1) / (2.0 * cell) * params["ViscousNormalStrength"]
    normal = normalize_rows(np.concatenate([-slope, np.ones(slope.shape[:-1] + (1,))], axis=-1))
    selective = saturate(tap[..., 2] / np.maximum(tap[..., 0], 1e-3))
    base = np.asarray(params["ViscousBaseColor"][:3])
    color = base + selective[..., None] * (np.asarray(params["ViscousSelectiveColor"][:3]) - base)
    opacity = coverage * saturate(tap[..., 3]) * inside_factor
    if decal:
        opacity = opacity * float(saturate(params["ViscousDecalOpacity"]))
    return height, normal, color, opacity


def look_params(scalars, vectors):
    params = {name: value for name, value in scalars}
    params.update({name: value for name, value in vectors})
    return params


def window_state(grid=128.0, extent=1280.0):
    return {"center": np.array([0.0, 0.0]), "extent": np.array([extent, extent]), "grid": grid}


def dense_taps(rng, size):
    return np.stack([rng.uniform(0.0, 1.0, (size, size)), rng.uniform(0.0, 1.0, (size, size)), rng.uniform(0.0, 1.0, (size, size)), rng.uniform(0.0, 1.0, (size, size))], axis=-1)


def test_mask_edge(material):
    params = look_params(material.SURFACE_LOOK_SCALAR_DEFAULTS, material.VISCOUS_LOOK_VECTOR_DEFAULTS)
    threshold = params["ViscousMaskThreshold"]
    edge = params["ViscousEdgeSoftness"]
    density = np.linspace(0.0, 1.0, 100001)
    taps = np.stack([density, np.full_like(density, 0.5), np.zeros_like(density), np.ones_like(density)], axis=-1)
    _, coverage = viscous_height(taps, params)
    monotone = bool(np.all(np.diff(coverage) >= -1e-12))
    _, at_threshold = viscous_height(np.array([threshold, 0.5, 0.0, 1.0]), params)
    zero_below = float(coverage[density <= threshold - edge].max()) <= 1e-9
    one_above = float(coverage[density >= threshold + edge].min()) >= 1.0 - 1e-9
    visible_start = float(density[np.argmax(coverage >= 0.5)])
    sharp_taps = taps[::1000]
    _, sharp_coverage = viscous_height(sharp_taps, params | {"ViscousEdgeSoftness": 0.0})
    finite = bool(np.isfinite(sharp_coverage).all())
    passed = monotone and abs(float(at_threshold) - 0.5) <= 1e-6 and zero_below and one_above and abs(visible_start - threshold) <= 2e-5 and finite
    return passed, f"단조 {monotone}, r=T 피복 {float(at_threshold):.6f}, 보이기 시작 r {visible_start:.5f} (T {threshold}), 부드러움 0 유한 {finite}", np.concatenate([coverage[::500], sharp_coverage])


def test_height(material):
    params = look_params(material.SURFACE_LOOK_SCALAR_DEFAULTS, material.VISCOUS_LOOK_VECTOR_DEFAULTS)
    threshold = params["ViscousMaskThreshold"]
    density = np.linspace(0.0, 1.0, 20001)
    flat = np.stack([density, np.full_like(density, 0.5), np.zeros_like(density), np.ones_like(density)], axis=-1)
    heights, _ = viscous_height(flat, params)
    zero_below = float(np.abs(heights[density <= threshold]).max()) == 0.0
    top = float(heights[-1])
    monotone = bool(np.all(np.diff(heights) >= -1e-12))
    pressed, _ = viscous_height(np.array([1.0, 1.0, 0.0, 1.0]), params)
    masked, _ = viscous_height(np.array([1.0, 1.0, 0.0, 0.0]), params)
    rng = np.random.default_rng(RNG_SEED)
    stress = dense_taps(rng, 64).reshape(-1, 4)
    finite = True
    for override in ({"ViscousHeightExponent": 0.0}, {"ViscousMaskThreshold": 1.0}, {"ViscousHeightExponent": 9.0}, {"ViscousEdgeSoftness": 0.0}):
        stress_heights, stress_coverage = viscous_height(stress, params | override)
        finite = finite and bool(np.isfinite(stress_heights).all() and np.isfinite(stress_coverage).all())
    passed = zero_below and top == params["ViscousHeightCm"] and monotone and abs(float(pressed) - (params["ViscousHeightCm"] + params["ViscousPressureHeightCm"])) <= 1e-12 and float(masked) == 0.0 and finite
    return passed, f"문턱 이하 0 {zero_below}, r=1 높이 {top} (HeightCm {params['ViscousHeightCm']}), 단조 {monotone}, 압력 항 포함 {float(pressed):.4f}, 알파 0 높이 {float(masked)}, 극단 설정 유한 {finite}", np.concatenate([heights[::500], stress_heights[:200]])


def test_normal(material):
    params = look_params(material.SURFACE_LOOK_SCALAR_DEFAULTS, material.VISCOUS_LOOK_VECTOR_DEFAULTS)
    size = 128
    window = window_state(grid=float(size), extent=1280.0)
    flat = np.zeros((size, size, 4))
    flat[..., 0] = 0.8
    flat[..., 1] = 0.5
    flat[..., 3] = 1.0
    rng = np.random.default_rng(RNG_SEED + 1)
    points = rng.uniform(-600.0, 600.0, (4000, 2))
    _, flat_normal, _, _ = viscous_pixel(flat, points, params, window)
    flat_exact = bool(np.all(flat_normal == np.array([0.0, 0.0, 1.0])))
    ramp = np.zeros((size, size, 4))
    ramp[..., 0] = (np.arange(size) / (size - 1.0))[None, :]
    ramp[..., 1] = 0.5
    ramp[..., 3] = 1.0
    ramp_params = params | {"ViscousMaskThreshold": 0.0, "ViscousHeightExponent": 1.0, "ViscousPressureHeightCm": 0.0, "ViscousHeightCm": 3.0}
    cell = 1280.0 / size
    columns = np.arange(2, size - 2)
    xs = ((columns + 0.5) / size - 0.5) * 1280.0
    centers = np.stack([xs, np.full_like(xs, (40.5 / size - 0.5) * 1280.0)], axis=-1)
    errors = []
    tilts = []
    for strength in (1.0, 2.0):
        _, ramp_normal, _, _ = viscous_pixel(ramp, centers, ramp_params | {"ViscousNormalStrength": strength}, window)
        slope_x = 3.0 * 2.0 / (size - 1.0) / (2.0 * cell) * strength
        expected = np.array([-slope_x, 0.0, 1.0])
        expected = expected / np.linalg.norm(expected)
        errors.append(float(np.abs(ramp_normal - expected).max()))
        tilts.append(float(np.abs(ramp_normal[:, 0] / ramp_normal[:, 2]).mean()))
    ratio = tilts[1] / tilts[0]
    stress = dense_taps(rng, size)
    _, stress_normal, stress_color, stress_opacity = viscous_pixel(stress, points, params, window)
    unit = float(np.abs(np.linalg.norm(stress_normal, axis=-1) - 1.0).max())
    finite = bool(np.isfinite(stress_normal).all() and np.isfinite(stress_color).all() and np.isfinite(stress_opacity).all())
    passed = flat_exact and max(errors) <= TOLERANCE and abs(ratio - 2.0) <= 1e-9 and unit <= 1e-12 and finite
    return passed, f"평탄 법선 (0,0,1) {flat_exact}, 램프 해석해 오차 {max(errors):.2e}, 강도 2배 기울기 비 {ratio:.9f}, 단위 길이 오차 {unit:.1e}, 유한 {finite}", np.concatenate([ramp_normal.ravel(), stress_normal[:200].ravel()])


def test_window_and_driven(material):
    params = look_params(material.DECAL_LOOK_SCALAR_DEFAULTS, material.VISCOUS_LOOK_VECTOR_DEFAULTS)
    size = 64
    window = window_state(grid=float(size), extent=640.0)
    full = np.ones((size, size, 4))
    outside = np.array([[320.0 + 1e-3, 0.0], [-320.0 - 1e-3, 0.0], [0.0, 320.0 + 1e-3], [0.0, -320.0 - 1e-3], [5000.0, 5000.0]])
    inside = np.array([[320.0 - 1e-3, 0.0], [0.0, -320.0 + 1e-3], [0.0, 0.0]])
    out_height, _, _, out_opacity = viscous_pixel(full, outside, params, window)
    in_height, _, _, in_opacity = viscous_pixel(full, inside, params, window)
    outside_zero = bool(np.all(out_height == 0.0) and np.all(out_opacity == 0.0))
    inside_visible = bool(np.all(in_opacity > 0.99))
    rng = np.random.default_rng(RNG_SEED + 2)
    points = rng.uniform(-600.0, 600.0, (3000, 2))
    stress = dense_taps(rng, size)
    off_height, off_normal, _, off_opacity = viscous_pixel(stress, points, params, window_state(grid=0.0, extent=640.0))
    driven_off = bool(np.all(off_height == 0.0) and np.all(off_opacity == 0.0) and np.all(off_normal == np.array([0.0, 0.0, 1.0])))
    _, _, _, surface_opacity = viscous_pixel(stress, points, params, window)
    decal_results = {}
    for opacity_value in (0.0, 0.5, 0.9, 2.0):
        _, _, _, decal_opacity = viscous_pixel(stress, points, params | {"ViscousDecalOpacity": opacity_value}, window, decal=True)
        decal_results[opacity_value] = float(np.abs(decal_opacity - surface_opacity * min(opacity_value, 1.0)).max())
    decal_ok = max(decal_results.values()) <= 1e-12
    passed = outside_zero and inside_visible and driven_off and decal_ok
    return passed, f"창 밖 높이·불투명도 0 {outside_zero}, 창 안 경계 불투명 {inside_visible}, 구동 끔 전부 0·법선 (0,0,1) {driven_off}, 데칼 불투명도 = 표면 x min(값,1) 오차 {max(decal_results.values()):.1e}", np.concatenate([out_opacity, in_opacity, off_opacity[:100], surface_opacity[:100]])


def tile_map(texture, position, camera, params):
    period = np.asarray(params["TileMap1PeriodCm"], dtype=np.float64)
    zeros = np.zeros(position.shape[:-1])
    if not (period[0] > 0.0 and period[1] > 0.0):
        return np.stack([zeros, zeros], axis=-1), zeros
    fade = 1.0 - saturate((np.linalg.norm(position - camera, axis=-1) - params["TileMap1FadeStartCm"]) / max(params["TileMap1FadeLengthCm"], 1.0))
    origin = np.asarray(params["TileMap1OriginCm"], dtype=np.float64)
    uv = (position[..., :2] - origin[:2]) / period[:2]
    texel = 1.0 / max(period[2], 1.0)
    center = fetch_bilinear_wrap(texture, uv)
    right = fetch_bilinear_wrap(texture, uv + np.array([texel, 0.0]))
    up = fetch_bilinear_wrap(texture, uv + np.array([0.0, texel]))

    def height_of(tap):
        return (tap[..., 1] * 2.0 - 1.0) * params["TileMap1HeightCm"] + tap[..., 0] * params["TileMap1DensityHeightCm"]

    cell = np.maximum(period[:2] * texel, 1.0)
    slope = np.stack([height_of(right) - height_of(center), height_of(up) - height_of(center)], axis=-1) / cell * (params["TileMap1NormalStrength"] * fade)[..., None]
    foam = center[..., 0] * params["TileMap1FoamStrength"] * fade
    return slope, foam


def lava_shade(local_texture, tile_texture, position, camera, params, window):
    slope, foam = tile_map(tile_texture, position, camera, params)
    driven = float(window["grid"] >= 0.5)
    size = np.maximum(window["extent"], 1.0)
    uv = (position[..., :2] - window["center"]) / size + 0.5
    tap = fetch_nearest(local_texture, uv)
    local = saturate(tap[..., 0]) * saturate(tap[..., 3]) * inside_window(uv) * driven
    edge = max(params["LavaCrustSoftness"], 1e-4)
    if params["TileMap1PeriodCm"][0] > 0.0:
        crust_tile = smoothstep(params["LavaCrustThreshold"] - edge, params["LavaCrustThreshold"] + edge, foam)
    else:
        crust_tile = np.ones_like(foam)
    crust = saturate(crust_tile * (1.0 - params["LavaLocalRevealStrength"] * local))
    molten = np.power(saturate(1.0 - crust), max(params["LavaEmissiveExponent"], 0.01))
    emissive_color = np.asarray(params["LavaEmissiveColor"][:3])
    crust_color = np.asarray(params["LavaCrustColor"][:3])
    emissive = emissive_color * params["LavaEmissiveIntensity"] * molten[..., None]
    normal = normalize_rows(np.concatenate([-slope, np.ones(slope.shape[:-1] + (1,))], axis=-1))
    color = emissive_color * params["LavaMoltenColorScale"] + crust[..., None] * (crust_color - emissive_color * params["LavaMoltenColorScale"])
    roughness = params["LavaMoltenRoughness"] + crust * (params["LavaCrustRoughness"] - params["LavaMoltenRoughness"])
    return crust, emissive, normal, color, roughness


def lava_params(material, period=(800.0, 800.0, 64.0)):
    return look_params(material.LAVA_LOOK_SCALAR_DEFAULTS, material.LAVA_LOOK_VECTOR_DEFAULTS) | {"TileMap1PeriodCm": tuple(period), "TileMap1OriginCm": (0.0, 0.0, 0.0)}


def uniform_tile(density, size=64):
    texture = np.zeros((size, size, 4))
    texture[..., 0] = density
    texture[..., 1] = 0.5
    texture[..., 3] = 1.0
    return texture


def test_lava(material):
    params = lava_params(material)
    window = window_state(grid=128.0, extent=1280.0)
    empty_local = np.zeros((128, 128, 4))
    rng = np.random.default_rng(RNG_SEED + 3)
    points = np.column_stack([rng.uniform(-300.0, 300.0, 500), rng.uniform(-300.0, 300.0, 500), np.zeros(500)])
    camera = np.array([0.0, 0.0, 500.0])
    threshold = params["LavaCrustThreshold"]
    edge = params["LavaCrustSoftness"]
    crusts = []
    for density in np.linspace(0.0, 1.0, 101):
        crust, _, _, _, _ = lava_shade(empty_local, uniform_tile(density), points[:1], camera, params, window)
        crusts.append(float(crust[0]))
    crusts = np.array(crusts)
    densities = np.linspace(0.0, 1.0, 101)
    monotone = bool(np.all(np.diff(crusts) >= -1e-12))
    ends = float(crusts[densities <= threshold - edge].max()) <= 1e-9 and float(crusts[densities >= threshold + edge].min()) >= 1.0 - 1e-9
    revealed = np.ones((128, 128, 4))
    crust_revealed, emissive_revealed, _, color_revealed, roughness_revealed = lava_shade(revealed, uniform_tile(1.0), points[:50] * 0.0, camera, params, window)
    expected_emissive = np.asarray(params["LavaEmissiveColor"][:3]) * params["LavaEmissiveIntensity"]
    reveal_ok = bool(np.all(crust_revealed == 0.0)) and float(np.abs(emissive_revealed - expected_emissive).max()) == 0.0
    molten_ok = float(np.abs(color_revealed - np.asarray(params["LavaEmissiveColor"][:3]) * params["LavaMoltenColorScale"]).max()) <= 1e-12 and float(np.abs(roughness_revealed - params["LavaMoltenRoughness"]).max()) <= 1e-12
    crust_full, emissive_full, _, color_full, roughness_full = lava_shade(empty_local, uniform_tile(1.0), points[:50], camera, params, window)
    crust_ok = bool(np.all(crust_full == 1.0)) and float(np.abs(color_full - np.asarray(params["LavaCrustColor"][:3])).max()) <= 1e-12 and float(np.abs(roughness_full - params["LavaCrustRoughness"]).max()) <= 1e-12 and float(np.abs(emissive_full).max()) == 0.0
    no_tile = params | {"TileMap1PeriodCm": (0.0, 0.0, 0.0)}
    crust_none, _, normal_none, _, _ = lava_shade(empty_local, uniform_tile(0.3), points[:50], camera, no_tile, window)
    no_tile_ok = bool(np.all(crust_none == 1.0)) and bool(np.all(normal_none == np.array([0.0, 0.0, 1.0])))
    half_reveal = params | {"LavaLocalRevealStrength": 0.5}
    crust_half, _, _, _, _ = lava_shade(revealed, uniform_tile(1.0), points[:50] * 0.0, camera, half_reveal, window)
    half_ok = float(np.abs(crust_half - 0.5).max()) <= 1e-12
    undriven, _, _, _, _ = lava_shade(revealed, uniform_tile(1.0), points[:50] * 0.0, camera, params, window_state(grid=0.0))
    undriven_ok = bool(np.all(undriven == 1.0))
    tile_texture = rng.uniform(0.0, 1.0, (64, 64, 4))
    _, stress_emissive, stress_normal, stress_color, stress_roughness = lava_shade(rng.uniform(0.0, 1.0, (128, 128, 4)), tile_texture, points, camera, params | {"LavaEmissiveExponent": 0.0}, window)
    finite = bool(np.isfinite(stress_emissive).all() and np.isfinite(stress_normal).all() and np.isfinite(stress_color).all() and np.isfinite(stress_roughness).all())
    unit = float(np.abs(np.linalg.norm(stress_normal, axis=-1) - 1.0).max())
    passed = monotone and ends and reveal_ok and molten_ok and crust_ok and no_tile_ok and half_ok and undriven_ok and finite and unit <= 1e-12
    detail = f"껍질 단조 {monotone}·끝점 {ends}, 드러냄 완전 시 발광 = 색 x 세기 {reveal_ok}, 용융 색·거칠기 끝점 {molten_ok}, 껍질 끝점 {crust_ok}, 타일 없음 껍질 1·법선 (0,0,1) {no_tile_ok}, 드러냄 0.5 → 껍질 0.5 {half_ok}, 구동 끔 드러냄 없음 {undriven_ok}, 지수 0 유한 {finite}"
    return passed, detail, np.concatenate([crusts, stress_emissive[:50].ravel(), stress_normal[:50].ravel()])


def run_mirror_tests(material):
    outcomes = {}
    digest = hashlib.sha256()
    tests = (("V1 마스크 경계", test_mask_edge), ("V2 높이", test_height), ("V3 법선 해석해", test_normal), ("V4 창 경계·구동 끔·데칼 불투명도", test_window_and_driven), ("V5 용암 껍질·발광", test_lava))
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


def find_fragment_name_problems(material, entries):
    problems = []
    lava_entry = entries.get(material.asset_path("MI_TD_FluidLava"), {})
    required_scalars = set(LAVA_TILE_SCALAR_NAMES) | {"LavaMoltenColorScale", "LavaMoltenRoughness"}
    missing_scalars = sorted(required_scalars - set(lava_entry.get("Scalars", {})))
    if missing_scalars:
        problems.append("용암 MI 스칼라 없음(M3·S2): " + ", ".join(missing_scalars))
    missing_vectors = sorted({"LavaEmissiveColor", "LavaCrustColor"} - set(lava_entry.get("Vectors", {})))
    if missing_vectors:
        problems.append("용암 MI 벡터 없음(M3): " + ", ".join(missing_vectors))
    for name in material.INSTANCE_PARENTS:
        entry = entries.get(material.asset_path(name), {})
        for section in ("Scalars", "Vectors", "Switches"):
            driven = sorted(set(entry.get(section, {})) & set(material.CPP_DRIVEN_NAMES))
            if driven:
                problems.append(f"{name}.{section}: C++ 구동 MID 이름 " + ", ".join(driven))
    return problems


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
    material = load_script(root, "editor_make_fluid_viscous_material")
    record("재질 스크립트 unreal 스텁 임포트", True)

    shader_folder = root / "Tools" / "FluidFX" / "Shaders"
    shader_names = (material.COMMON_HLSL_NAME, material.SURFACE_HLSL_NAME, material.OFFSET_HLSL_NAME, material.DECAL_HLSL_NAME, material.LAVA_HLSL_NAME)
    text_failures = []
    for name in shader_names:
        text_failures += [f"{name}: {item}" for item in run_shader_text_checks((shader_folder / name).read_text(encoding="utf-8"))]
    record("셰이더 텍스트(주석·절대 경로·ASCII·LF)", not text_failures, "; ".join(text_failures))
    lava_text = (shader_folder / material.LAVA_HLSL_NAME).read_text(encoding="utf-8")
    placeholder_counts = {name: (shader_folder / name).read_text(encoding="utf-8").count(material.TILE_MAP_PLACEHOLDER) for name in shader_names}
    record("용암 타일 자리표시자 정확히 1개, 점성 파일에는 없음", placeholder_counts[material.LAVA_HLSL_NAME] == 1 and sum(placeholder_counts.values()) == 1, str(placeholder_counts))
    molten_literals = MOLTEN_LITERAL_PATTERN.findall(lava_text)
    record("용암 HLSL 에 셰이더 상수 0.05·0.4 없음(S2, 스칼라로)", not molten_literals, ", ".join(molten_literals))

    sources = material.read_shader_sources(str(root))
    codes = material.assemble_all_node_codes(sources)
    problems = material.validate_all_node_codes(codes)
    problems += [f"입력 원천 없음: {name}" for name in material.find_unresolved_inputs()]
    record("조립 코드 자체 점검(입력·샘플러·추가 출력·매크로 접두·자리표시자)", not problems, "; ".join(problems))
    missing_anchors = []
    for label, anchors in (("offset", COMMON_ANCHORS), ("surface", COMMON_ANCHORS + SURFACE_ANCHORS), ("decal", COMMON_ANCHORS + DECAL_ANCHORS), ("lava", LAVA_ANCHORS)):
        missing_anchors += [f"{label}: {anchor}" for anchor in anchors if normalize_text(anchor) not in normalize_text(codes[label])]
    record("식 고정점(창 UV 규약·피복·높이·법선·껍질·발광)", not missing_anchors, "; ".join(missing_anchors))
    offset_text = normalize_text(codes["offset"])
    record("오프셋은 높이만 반환", "return float3(0.0, 0.0, viscousHeightCm);" in offset_text)

    entries, overlap = collect_look_entries(root, arguments.look_fragment)
    override_problems = material.find_instance_override_problems(entries) + find_fragment_name_problems(material, entries)
    record("MI 이름 계약(부모·C++ 구동 이름 없음·구획·용암 TileMap1 6개·S2 스칼라)", not override_problems, "; ".join(override_problems))
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
    jobs = []
    for optimize in (False, True):
        suffix = "O3" if optimize else "Od"
        jobs.append((f"offset_vs_{suffix}", build_source(material.OFFSET_INPUT_NAMES, "TDViscousOffset", "float3", (), codes["offset"], "vs"), "vs_6_0", optimize, 1))
        jobs.append((f"surface_ps_{suffix}", build_source(material.SURFACE_INPUT_NAMES, "TDViscousSurface", "float4", [name for name, _ in material.VISCOUS_OUTPUT_SPECS], codes["surface"], "ps"), "ps_6_0", optimize, 5))
        jobs.append((f"decal_ps_{suffix}", build_source(material.DECAL_INPUT_NAMES, "TDViscousDecal", "float4", [name for name, _ in material.VISCOUS_OUTPUT_SPECS], codes["decal"], "ps"), "ps_6_0", optimize, 5))
        jobs.append((f"lava_ps_{suffix}", build_source(material.LAVA_INPUT_NAMES, "TDLavaSurface", "float4", [name for name, _ in material.LAVA_OUTPUT_SPECS], codes["lava"], "ps"), "ps_6_0", optimize, 4))
    compiles = {}
    with tempfile.TemporaryDirectory() as folder:
        work_folder = pathlib.Path(folder)
        for label, source, profile, optimize, minimum_samples in jobs:
            outcome = compile_source(dxc, source, label, profile, optimize, work_folder)
            record(f"dxc {label} {profile}", outcome["exit"] == 0 and outcome["samples"] >= minimum_samples, outcome["error"] or f"표본 {outcome['samples']} (최소 {minimum_samples}) 명령 {outcome['instructions']}")
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
