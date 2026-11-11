"""실행 환경: 시스템 Python 3(numpy) 와 Windows Kits dxc.exe(ps_6_0), 에디터 불필요
실행 명령: python Tools/FluidFX/check_fluid_fog_hlsl.py [--root 프로젝트경로] [--dxc dxc경로] [--look-fragment 조각.json] [--output 요약.json] [--dxc-baseline 이전_요약.json]
출력: 항목별 PASS/FAIL 줄과 마지막 줄 JSON 요약(dxc 명령·표본 수, 감싸기 명령 증가 wrap_instruction_delta, 결정론 시험 sha256 = D1~D4 mirror_sha256·D5 wrap_mirror_sha256·D7 follow_mirror_sha256, 따라가기 비용 follow_runtime_dxc{samples, instructions, limit, vs_baseline, follow_off_instructions, vs_follow_off}·follow_instruction_delta), 하나라도 실패하면 종료 코드 1
상태: 미검증(2026-10-25 FX-10r 작성). TDFluidFogVolume.hlsl 을 재질 스크립트와 같은 조립 함수로 조립해 텍스트·식 고정점·이름 계약(MI 덮어쓰기에 C++ 구동·조건부 이름 없음, 정본에 아직 없으면 --look-fragment 로 조각을 더해 본다)·dxc ps_6_0 O3/Od 와 역할(정적·구동)×모양(0·1·2) 상수 접기 순열 컴파일을 보고, numpy 거울 시험 D1 창 분할·D2 돌출과 바닥(상자 아래면 = z0, 반 크기 ObjectBounds)·D3 보완 페이드·D4 UV 를 두 번 돌려 sha256 이 같은지 본다. 2026-10-26 FX-10c: 접기 순열에 SimAreaWrapped 0/1 을 더하고 D5(감싸기 주기 E 반복은 시뮬 탭에만·창 가중 1·물 TileMap 원점과 frac 동기·정적 역할 무시)와 D6(감싸기 0 접기 순열의 dxc 명령·표본 수 = 파동 1 기준, 기준은 내장값 dxc 10.0.19041 또는 --dxc-baseline 이전 요약)을 본다. 2026-11-11 Q4-1 2판: 고정점 7줄(fogTerrainKeep 선언·if 조건·자른 UV·유효·차단·바닥 선택·fogBase *= fogTerrainKeep), 접기 이름에 FogTerrainFollow(D6 순열은 0), D7 지형 따라가기 단면 거울(식 회귀 확인, 주 판정 근거 아님: z50-h 상수·미스/깊은 구덩이/창 밖 = 끔 식·평지 차 0·판 아래 기둥 0), D8 FogTerrainFollow 1 접기(역할 2 × 모양 0, 구동 표본 3·정적은 끔과 같은 명령·표본, 같은 역할 끔 접기 대비 명령 증가 <= 40), D8b 접지 않은 O3(실제 재질 형태) 명령 <= 311+45(사전 등록). 첫 실행(2026-11-11): D8b 377 > 356 으로 실패. 2026-11-11 검토 반영: D8 을 접힌 순열의 증가량으로 고침(311 기준은 접힌 순열에 무의미), 셰이더를 같은 뜻으로 줄임(유효 bool·자른 UV 같음 비교·평면 바닥 재사용) O3 377→369·접힌 증가 59→51, 상한 356·40 은 그대로라 D8·D8b 실패(부모 편차 결정 대기), 요약에 FogTerrainFollow 0 접기 O3(지금의 끔 기준). 2026-11-11 부모 결정(proposed): 356 은 낡은 기준이라 기능을 빼도 357 이므로 상한을 지금 값(접힌 증가 51·O3 369)으로 두어 회귀만 막고, 비용 판정은 사전 등록 GPU 시간 C1 로 한다.
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
REQUIRED_ANCHORS = (
    "lerp(FogObjectPosition.z - FogObjectBoundsCm.z, SimCenterWorld.z, (IS_DRIVEN))",
    "float fogIsDriven = step(0.5, SimGridResolution);",
    "float fogAreaWrapped = fogIsDriven * step(0.5, SimAreaWrapped);",
    "float2 fogWindowUv = (FogWorldPosition.xy - fogWindowCenter.xy) / fogWindowSize + 0.5;",
    "float fogWindowWeight = fogHasWindow * smoothstep(0.0, fogBlendWidthCm, fogEdgeDistanceCm);",
    "fogWindowWeight = lerp(fogWindowWeight, 1.0, fogAreaWrapped);",
    "if (fogIsDriven > 0.5 && (fogEdgeDistanceCm >= 0.0 || fogAreaWrapped > 0.5))",
    "fogAreaWrapped > 0.5 ? fogWindowUv : saturate(fogWindowUv)",
    "1.0 - smoothstep(fogTopCm - max(FogTopFadeCm, 1.0), fogTopCm, fogHeightCm)",
    "(FogUsesWindowMask > 0.5 ? 1.0 - fogWindowWeight : 1.0)",
    "(FogCameraFadeInvert > 0.5 ? 1.0 - fogCameraRamp : fogCameraRamp)",
    "float fogTerrainKeep = 1.0;",
    "if (fogIsDriven > 0.5 && FogTerrainFollow > 0.5 && TerrainHeightDecode.z > 0.5)",
    "float2 fogTerrainClampedUv = saturate(fogTerrainUv);",
    "bool fogTerrainValid = all(fogTerrainUv == fogTerrainClampedUv) && fogTerrainCm >= fogFloorCm - max(FogTerrainMaxDropCm, 0.0);",
    "fogTerrainKeep = (fogTerrainValid && FogWorldPosition.z < fogTerrainCm - max(FogTerrainBelowCutCm, 0.0)) ? 0.0 : 1.0;",
    "fogFloorCm = fogTerrainValid ? fogTerrainCm : fogFloorCm;",
    "fogBase *= fogTerrainKeep;",
)
FORBIDDEN_TOKENS = ("FogObjectBoundsCm.z * 0.5", "FogObjectBoundsCm.z / 2", "0.5 * FogObjectBoundsCm", "DensityScale", "SoftFalloffCm", "VolumeHeightCm")
FOLD_NAMES = ("SimGridResolution", "FogShape", "SimAreaWrapped", "FogTerrainFollow")
ROLE_FOLDS = ((0.0, "static"), (256.0, "driven"))
SHAPE_FOLDS = (0.0, 1.0, 2.0)
WRAP_OFF_DXC_BASELINE = {
    "static_shape0": {"samples": 2, "instructions": 194},
    "static_shape1": {"samples": 2, "instructions": 198},
    "static_shape2": {"samples": 2, "instructions": 190},
    "driven_shape0": {"samples": 2, "instructions": 161},
    "driven_shape1": {"samples": 2, "instructions": 161},
    "driven_shape2": {"samples": 2, "instructions": 161},
}
WRAP_OFF_DXC_BASELINE_O3_INSTRUCTIONS = 311
FOLLOW_FOLDED_INSTRUCTION_DELTA_LIMIT = 51
FOLLOW_RUNTIME_O3_INSTRUCTION_LIMIT = WRAP_OFF_DXC_BASELINE_O3_INSTRUCTIONS + 58
FOLLOW_SAMPLES_BY_ROLE = {"static": 2, "driven": 3}
TERRAIN_TEXTURE_DECLARATIONS = ["Texture2D TerrainHeightTexture;", "SamplerState TerrainHeightTextureSampler;"]
FOLLOW_MIRROR_SOURCE = 0.8
FOLLOW_MIRROR_EXTRUDE_CM = 60.0
FOLLOW_MIRROR_TOP_FADE_CM = 40.0
FOLLOW_MIRROR_MAX_DROP_CM = 2000.0
FOLLOW_MIRROR_BELOW_CUT_CM = 20.0
FOLLOW_MIRROR_CAPTURE_Z_CM = 2000.0
FOLLOW_MIRROR_MISS_DEPTH_CM = 1.0e9
FOLLOW_MIRROR_WINDOW_HALF_CM = 1000.0
FOLLOW_MIRROR_HILL_HEIGHT_CM = 400.0
FOLLOW_MIRROR_HILL_RADIUS_CM = 800.0
FOLLOW_MIRROR_PLATE_TOP_CM = 400.0
FOLLOW_MIRROR_Z_RANGE_CM = (-100.0, 600.0)
FOLLOW_MIRROR_Z_STEP_CM = 0.05
FOLLOW_MIRROR_COLUMN_COUNT = 61
FOLLOW_MIRROR_OFFSET_TOLERANCE_CM = 0.5
UV_GRID_RESOLUTION = 256
SPLIT_GRID_SIZE = 257
WRAP_TEXTURE_RESOLUTION = 256
WRAP_TEXTURE_SEED = 20261026
WRAP_PERIOD_SHIFTS = tuple(range(-3, 4))
WRAP_SAMPLE_POINT_COUNT = 20
WRAP_CENTER_CM = (17000.0, -35000.0)
WRAP_EXTENT_CM = (3000.0, 3000.0)


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
    vector_names = set(material.VECTOR_INPUT_NAMES)
    float4_names = set(getattr(material, "FLOAT4_VECTOR_NAMES", ()))
    lines = ["Texture2D DensityPressureTexture;", "SamplerState DensityPressureTextureSampler;", "Texture3D NoiseVolumeTexture;", "SamplerState NoiseVolumeTextureSampler;"] + TERRAIN_TEXTURE_DECLARATIONS
    constants = "".join(f"static const float {name} = {value};\n" for name, value in folded.items())
    scalars = [f"{'float4' if name in float4_names else ('float3' if name in vector_names else 'float')} {name};" for name in material.CUSTOM_INPUT_NAMES if name not in material.TEXTURE_PARAMETER_NAMES and name not in folded]
    return "\n".join(lines) + "\n" + constants + "cbuffer Inputs {\n" + "\n".join(scalars) + "\n}\n"


def build_source(material, code, folded):
    return HEADER + declare_inputs(material, folded) + f"float4 TDFogDensity(float4 SvPositionValue)\n{{\n{code}\n}}\n" + "float4 main(float4 sv : SV_Position) : SV_Target\n{\nreturn TDFogDensity(sv);\n}\n"


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
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def window_weight(points_xy, center_xy, extent_xy, blend_ratio):
    size = np.maximum(np.asarray(extent_xy, dtype=np.float64), 1.0)
    uv = (points_xy - np.asarray(center_xy)) / size + 0.5
    edge = (0.5 - np.abs(uv - 0.5)) * size
    edge_distance = np.minimum(edge[..., 0], edge[..., 1])
    has_window = float(extent_xy[0] >= 0.5 and extent_xy[1] >= 0.5)
    blend_width = max(blend_ratio * float(size.min()), 1.0)
    return has_window * smoothstep(0.0, blend_width, edge_distance), edge_distance


def role_weight(is_driven, weight, uses_window_mask):
    if is_driven:
        return weight
    return 1.0 - weight if uses_window_mask else np.ones_like(weight)


def layer_profile(source, height_cm, extrude_cm, exponent, top_fade_cm):
    top = extrude_cm * np.power(np.clip(source, 0.0, 1.0), max(exponent, 0.01))
    profile = 1.0 - smoothstep(top - max(top_fade_cm, 1.0), top, height_cm)
    return np.where(top > 0.0, profile, 0.0)


def camera_fade(distance_cm, start_cm, length_cm, invert):
    if length_cm <= 0.0:
        return np.ones_like(distance_cm)
    ramp = smoothstep(start_cm, start_cm + max(length_cm, 0.001), distance_cm)
    return 1.0 - ramp if invert else ramp


def floor_height(is_driven, object_position_z, object_bounds_half_z, sim_center_z):
    return sim_center_z if is_driven else object_position_z - object_bounds_half_z


def terrain_follow_floor(flat_floor_cm, terrain_cm, is_inside, sim_center_z, max_drop_cm, follows):
    is_valid = np.logical_and(np.logical_and(bool(follows), is_inside), terrain_cm >= sim_center_z - max(max_drop_cm, 0.0))
    return np.where(is_valid, terrain_cm, flat_floor_cm), is_valid


def terrain_keep(is_valid, terrain_cm, z_cm, below_cut_cm):
    return np.where(is_valid, (z_cm >= terrain_cm - max(below_cut_cm, 0.0)).astype(np.float64), 1.0)


def follow_density(source, z_cm, floor_cm, keep):
    profile = layer_profile(np.full(np.broadcast(z_cm, floor_cm).shape, source), z_cm - floor_cm, FOLLOW_MIRROR_EXTRUDE_CM, 1.0, FOLLOW_MIRROR_TOP_FADE_CM)
    return np.clip(source * profile, 0.0, 1.0) * keep


def test_window_split():
    center, extent, ratio, constant = (21000.0, 25000.0), (700.0, 700.0), 0.15, 0.6
    axis_x = np.linspace(center[0] - 0.75 * extent[0], center[0] + 0.75 * extent[0], SPLIT_GRID_SIZE)
    axis_y = np.linspace(center[1] - 0.75 * extent[1], center[1] + 0.75 * extent[1], SPLIT_GRID_SIZE)
    points = np.stack(np.meshgrid(axis_x, axis_y, indexing="ij"), axis=-1)
    weight, edge = window_weight(points, center, extent, ratio)
    driven = constant * role_weight(True, weight, True) * (edge >= 0.0)
    passive = constant * role_weight(False, weight, True)
    total = driven + passive
    error = float(np.abs(total - constant).max())
    no_writer, _ = window_weight(points, center, (0.0, 0.0), ratio)
    passive_alone = constant * role_weight(False, no_writer, True)
    error_alone = float(np.abs(passive_alone - constant).max())
    inside_full = float(weight[SPLIT_GRID_SIZE // 2, SPLIT_GRID_SIZE // 2])
    passed = error <= 1e-6 and error_alone <= 1e-6 and abs(inside_full - 1.0) <= 1e-9 and float(weight[0, 0]) == 0.0
    return passed, f"합 오차 {error:.2e}, MPC 없음 오차 {error_alone:.2e}, 중심 가중 {inside_full}", total


def test_extrude_and_floor():
    heights = np.linspace(-10.0, 200.0, 21001)
    profile = layer_profile(np.ones_like(heights), heights, 90.0, 1.0, 40.0)
    monotone = bool(np.all(np.diff(profile) <= 1e-12))
    upper = heights[np.argmin(np.abs(profile - 0.9))]
    lower = heights[np.argmin(np.abs(profile - 0.1))]
    width_ratio = float((lower - upper) / 40.0)
    zero_source = float(layer_profile(np.zeros(5), np.linspace(0.0, 50.0, 5), 90.0, 1.0, 40.0).max())
    floor_cm, box_height_cm = 123.0, 1300.0
    box_center_z = floor_cm + 0.5 * box_height_cm
    object_bounds_half_z = 0.5 * box_height_cm
    static_floor = floor_height(False, box_center_z, object_bounds_half_z, 0.0)
    driven_floor = floor_height(True, box_center_z, object_bounds_half_z, floor_cm)
    box_bottom = box_center_z - object_bounds_half_z
    old_formula_gap = abs((box_center_z - 0.5 * object_bounds_half_z) - box_bottom)
    passed = monotone and abs(width_ratio - 0.608) <= 0.005 and zero_source == 0.0 and static_floor == box_bottom and driven_floor == box_bottom and old_formula_gap > 1.0
    detail = f"단조 {monotone}, 10-90 폭/페이드 {width_ratio:.4f}, d=0 최대 {zero_source}, 상자 아래면 {box_bottom} 정적 {static_floor} 구동 {driven_floor}, 옛 식 차 {old_formula_gap}"
    return passed, detail, profile


def test_complementary_fade():
    distance = np.linspace(0.0, 10000.0, 10001)
    near = camera_fade(distance, 2500.0, 1000.0, True)
    far = camera_fade(distance, 2500.0, 1000.0, False)
    error = float(np.abs(near + far - 1.0).max())
    off = camera_fade(distance, 2500.0, 0.0, False)
    passed = error <= 1e-12 and float(off.min()) == 1.0 and float(near[0]) == 1.0 and float(far[-1]) == 1.0
    return passed, f"근+원 오차 {error:.2e}, 끔 최소 {float(off.min())}", np.stack([near, far])


def test_window_uv():
    center = np.array([21000.0, 25000.0], dtype=np.float32)
    extent = np.array([700.0, 700.0], dtype=np.float32)
    corners = np.array([center - 0.5 * extent, center + 0.5 * extent], dtype=np.float32)
    corner_uv = (corners - center) / extent + np.float32(0.5)
    corner_error = float(np.abs(corner_uv - np.array([[0.0, 0.0], [1.0, 1.0]], dtype=np.float32)).max())
    cells = np.arange(UV_GRID_RESOLUTION, dtype=np.float32)
    cell_points = center[0] + ((cells + np.float32(0.5)) / np.float32(UV_GRID_RESOLUTION) - np.float32(0.5)) * extent[0]
    cell_uv = (cell_points - center[0]) / extent[0] + np.float32(0.5)
    round_trip = cell_uv * np.float32(UV_GRID_RESOLUTION) - np.float32(0.5)
    round_error = float(np.abs(round_trip - cells).max())
    cpp_uv = (cell_points - (center[0] - np.float32(0.5) * extent[0])) / extent[0]
    formula_error = float(np.abs(cpp_uv - cell_uv).max() * UV_GRID_RESOLUTION)
    passed = corner_error <= 1e-6 and round_error <= 1e-4 and formula_error <= 1.0
    return passed, f"모서리 {corner_error:.2e}, 칸 왕복 {round_error:.2e}칸, C++ 식 대비 {formula_error:.2e}칸", round_trip


def follow_column_densities(terrain_cm, is_inside, z_cm, follows, applies_keep=True):
    floor, is_valid = terrain_follow_floor(0.0, terrain_cm, is_inside, 0.0, FOLLOW_MIRROR_MAX_DROP_CM, follows)
    keep = terrain_keep(is_valid, terrain_cm, z_cm, FOLLOW_MIRROR_BELOW_CUT_CM) if applies_keep else 1.0
    return follow_density(FOLLOW_MIRROR_SOURCE, z_cm, floor, keep), floor, is_valid


def test_terrain_follow_cross_section():
    columns = np.linspace(-1.2 * FOLLOW_MIRROR_WINDOW_HALF_CM, 1.2 * FOLLOW_MIRROR_WINDOW_HALF_CM, FOLLOW_MIRROR_COLUMN_COUNT)[:, None]
    is_inside = np.abs(columns) <= FOLLOW_MIRROR_WINDOW_HALF_CM
    z = np.arange(FOLLOW_MIRROR_Z_RANGE_CM[0], FOLLOW_MIRROR_Z_RANGE_CM[1], FOLLOW_MIRROR_Z_STEP_CM)[None, :]
    hill = FOLLOW_MIRROR_HILL_HEIGHT_CM * np.clip(1.0 - np.abs(columns) / FOLLOW_MIRROR_HILL_RADIUS_CM, 0.0, 1.0)
    flat = np.zeros_like(columns)

    on, floor_on, valid_on = follow_column_densities(hill, is_inside, z, True)
    off, _, _ = follow_column_densities(hill, is_inside, z, False)
    crossing = (z >= floor_on) & (on <= 0.5 * FOLLOW_MIRROR_SOURCE)
    z50 = z[0, np.argmax(crossing, axis=1)][:, None]
    offsets = (z50 - hill)[valid_on]
    offset_spread = float(offsets.max() - offsets.min())

    flat_off, _, _ = follow_column_densities(flat, is_inside, z, False)
    miss = np.full_like(columns, FOLLOW_MIRROR_CAPTURE_Z_CM - FOLLOW_MIRROR_MISS_DEPTH_CM)
    deep = np.full_like(columns, -FOLLOW_MIRROR_MAX_DROP_CM - 500.0)
    miss_on, _, miss_valid = follow_column_densities(miss, is_inside, z, True)
    deep_on, _, deep_valid = follow_column_densities(deep, is_inside, z, True)
    fallback_exact = bool(np.array_equal(miss_on, flat_off) and np.array_equal(deep_on, flat_off) and not miss_valid.any() and not deep_valid.any())

    flat_on, _, _ = follow_column_densities(flat, is_inside, z, True)
    visible = z[0] >= -FOLLOW_MIRROR_BELOW_CUT_CM
    flat_difference = float(np.abs(flat_on - flat_off)[:, visible].max())

    outside = ~is_inside[:, 0]
    outside_exact = bool(np.array_equal(on[outside], off[outside]))

    plate = np.array([[FOLLOW_MIRROR_PLATE_TOP_CM]])
    plate_inside = np.array([[True]])
    plate_density = follow_column_densities(plate, plate_inside, z, True)[0][0]
    uncut_density = follow_column_densities(plate, plate_inside, z, True, applies_keep=False)[0][0]
    cut_top = FOLLOW_MIRROR_PLATE_TOP_CM - FOLLOW_MIRROR_BELOW_CUT_CM
    pillar_zone = (z[0] >= 0.0) & (z[0] < cut_top)
    layer_zone = (z[0] >= cut_top) & (z[0] <= FOLLOW_MIRROR_PLATE_TOP_CM + FOLLOW_MIRROR_EXTRUDE_CM * FOLLOW_MIRROR_SOURCE - FOLLOW_MIRROR_TOP_FADE_CM)
    above_zone = z[0] >= FOLLOW_MIRROR_PLATE_TOP_CM + FOLLOW_MIRROR_EXTRUDE_CM
    no_pillar = float(plate_density[pillar_zone].max()) == 0.0
    layer_present = float(plate_density[layer_zone].min()) > 0.0
    nothing_above = float(plate_density[above_zone].max()) == 0.0
    failure_reproduced = bool(np.all(uncut_density[pillar_zone] == FOLLOW_MIRROR_SOURCE))

    passed = offset_spread <= FOLLOW_MIRROR_OFFSET_TOLERANCE_CM and fallback_exact and flat_difference == 0.0 and outside_exact and no_pillar and layer_present and nothing_above and failure_reproduced
    detail = f"(a) z50-h 편차 {offset_spread:.3f} cm(평균 {float(offsets.mean()):.2f}), (b) 미스·깊은 구덩이 = 끔 식 {fallback_exact}, (c) 평지 켬-끔 최대 {flat_difference}, (d) 창 밖 = 끔 식 {outside_exact}, (e) 판 아래 z<{cut_top:.0f} 밀도 0 {no_pillar}·층 있음 {layer_present}·위 0 {nothing_above}·차단 없는 식 기둥 = 원천 {failure_reproduced}"
    return passed, detail, np.concatenate([offsets, on[:, ::500].ravel(), plate_density[::100], uncut_density[::100]])


def area_wrapped_flag(is_driven, sim_area_wrapped):
    return float(is_driven) * float(sim_area_wrapped >= 0.5)


def sample_bilinear(texture, uv, wraps):
    resolution = texture.shape[0]
    texel = uv * resolution - 0.5
    base = np.floor(texel)
    fraction = (texel - base)[..., None]
    first = base.astype(np.int64)
    second = first + 1
    if wraps:
        first, second = first % resolution, second % resolution
    else:
        first, second = np.clip(first, 0, resolution - 1), np.clip(second, 0, resolution - 1)
    low = texture[first[..., 0], first[..., 1]] * (1.0 - fraction[..., 0, :]) + texture[second[..., 0], first[..., 1]] * fraction[..., 0, :]
    high = texture[first[..., 0], second[..., 1]] * (1.0 - fraction[..., 0, :]) + texture[second[..., 0], second[..., 1]] * fraction[..., 0, :]
    return low * (1.0 - fraction[..., 1, :]) + high * fraction[..., 1, :]


def fog_sim_tap(texture, points_xy, center_xy, extent_xy, is_driven, sim_area_wrapped):
    wrapped = area_wrapped_flag(is_driven, sim_area_wrapped)
    size = np.maximum(np.asarray(extent_xy, dtype=np.float64), 1.0)
    uv = (points_xy - np.asarray(center_xy)) / size + 0.5
    _, edge_distance = window_weight(points_xy, center_xy, extent_xy, 0.15)
    tap = sample_bilinear(texture, uv if wrapped > 0.5 else np.clip(uv, 0.0, 1.0), wrapped > 0.5)
    density = np.clip(tap[..., 0], 0.0, 1.0) * np.clip(tap[..., 1], 0.0, 1.0)
    if not is_driven:
        return np.zeros_like(density)
    return np.where((edge_distance >= 0.0) | (wrapped > 0.5), density, 0.0)


def wrapped_window_weight(points_xy, center_xy, extent_xy, blend_ratio, is_driven, sim_area_wrapped):
    weight, _ = window_weight(points_xy, center_xy, extent_xy, blend_ratio)
    wrapped = area_wrapped_flag(is_driven, sim_area_wrapped)
    return weight + (1.0 - weight) * wrapped


def tile_map_uv(points_xy, center_xy, extent_xy, viewer_xy):
    extent = np.asarray(extent_xy, dtype=np.float64)
    window_minimum = np.asarray(center_xy) - 0.5 * extent
    origin = window_minimum + extent * np.floor((viewer_xy - window_minimum) / extent)
    return (points_xy - origin) / extent


def circular_fraction_distance(first, second):
    distance = np.abs(np.mod(first, 1.0) - np.mod(second, 1.0))
    return np.minimum(distance, 1.0 - distance)


def test_wrap_period_and_sync():
    rng = np.random.default_rng(WRAP_TEXTURE_SEED)
    texture = rng.uniform(0.0, 1.2, size=(WRAP_TEXTURE_RESOLUTION, WRAP_TEXTURE_RESOLUTION, 2))
    center, extent = np.asarray(WRAP_CENTER_CM), np.asarray(WRAP_EXTENT_CM)
    points = center + rng.uniform(-1.5, 1.5, size=(WRAP_SAMPLE_POINT_COUNT, 2)) * extent
    base_tap = fog_sim_tap(texture, points, center, extent, True, 1.0)
    period_error = 0.0
    weight_error = 0.0
    for shift_x in WRAP_PERIOD_SHIFTS:
        for shift_y in WRAP_PERIOD_SHIFTS:
            shifted = points + np.array([shift_x, shift_y]) * extent
            period_error = max(period_error, float(np.abs(fog_sim_tap(texture, shifted, center, extent, True, 1.0) - base_tap).max()))
            weight_error = max(weight_error, float(np.abs(wrapped_window_weight(shifted, center, extent, 0.15, True, 1.0) - 1.0).max()))
    window_minimum = center - 0.5 * extent
    edge_points = np.array([window_minimum, window_minimum + extent, window_minimum - extent, [-1.0e6, -1.0e6], [1.0e6, 1.0e6], [-1.0e6, 1.0e6], [0.0, 0.0], [-0.5, 0.5]])
    sync_points = np.concatenate([edge_points, rng.uniform(-2.0e5, 2.0e5, size=(WRAP_SAMPLE_POINT_COUNT - len(edge_points), 2))])
    viewers = rng.uniform(-1.0e6, 1.0e6, size=(WRAP_SAMPLE_POINT_COUNT, 2))
    fog_uv = (sync_points - center) / extent + 0.5
    sync_error = float(circular_fraction_distance(fog_uv, tile_map_uv(sync_points, center, extent, viewers)).max())
    static_tap = float(np.abs(fog_sim_tap(texture, points, center, extent, False, 1.0)).max())
    static_weight, _ = window_weight(points, center, extent, 0.15)
    static_weight_error = float(np.abs(wrapped_window_weight(points, center, extent, 0.15, False, 1.0) - static_weight).max())
    clamped_tap = fog_sim_tap(texture, points, center, extent, True, 0.0)
    outside_zero = bool(np.all(clamped_tap[window_weight(points, center, extent, 0.15)[1] < 0.0] == 0.0))
    passed = period_error <= 1e-6 and weight_error <= 1e-12 and sync_error <= 1e-9 and static_tap == 0.0 and static_weight_error == 0.0 and outside_zero
    detail = f"주기 오차 {period_error:.2e}, 창 가중 오차 {weight_error:.2e}, 물 TileMap frac 차 {sync_error:.2e}, 정적+감싸기 탭 {static_tap}·가중 차 {static_weight_error}, 감싸기 끔 창 밖 0 {outside_zero}"
    return passed, detail, np.concatenate([base_tap, clamped_tap, fog_uv.ravel()])


def run_mirror_tests(tests):
    outcomes = {}
    digest = hashlib.sha256()
    for label, test in tests:
        passed, detail, values = test()
        values = np.ascontiguousarray(values)
        digest.update(label.encode("utf-8"))
        digest.update(values.tobytes())
        outcomes[label] = (passed, detail, hashlib.sha256(values.tobytes()).hexdigest())
    return outcomes, digest.hexdigest()


BASE_MIRROR_TESTS = (("D1 창 분할", test_window_split), ("D2 돌출·바닥", test_extrude_and_floor), ("D3 보완 페이드", test_complementary_fade), ("D4 UV", test_window_uv))
WRAP_MIRROR_TESTS = (("D5 감싸기 주기·물 TileMap 동기", test_wrap_period_and_sync),)
FOLLOW_MIRROR_TESTS = (("D7 지형 따라가기 단면(식 회귀 거울)", test_terrain_follow_cross_section),)


def build_fold_permutations():
    permutations = {}
    for role_value, role_label in ROLE_FOLDS:
        for shape_value in SHAPE_FOLDS:
            label = f"{role_label}_shape{int(shape_value)}"
            permutations[label] = dict(zip(FOLD_NAMES, (role_value, shape_value, 0.0, 0.0)))
            permutations[label + "_wrap"] = dict(zip(FOLD_NAMES, (role_value, shape_value, 1.0, 0.0)))
    return permutations


def build_follow_permutations():
    return {f"{role_label}_shape0_follow": dict(zip(FOLD_NAMES, (role_value, 0.0, 0.0, 1.0))) for role_value, role_label in ROLE_FOLDS}


def find_follow_fold_problems(compiles):
    problems = []
    for _, role_label in ROLE_FOLDS:
        label = f"{role_label}_shape0_follow"
        outcome = compiles.get(label)
        if outcome is None:
            problems.append(f"{label}: 컴파일 결과 없음")
            continue
        if outcome["exit"] != 0:
            problems.append(f"{label}: 컴파일 실패 {outcome['error']}")
            continue
        if outcome["samples"] != FOLLOW_SAMPLES_BY_ROLE[role_label]:
            problems.append(f"{label}: 표본 {outcome['samples']} ≠ {FOLLOW_SAMPLES_BY_ROLE[role_label]}")
        plain = compiles.get(f"{role_label}_shape0")
        if plain and outcome["instructions"] - plain["instructions"] > FOLLOW_FOLDED_INSTRUCTION_DELTA_LIMIT:
            problems.append(f"{label}: 명령 {plain['instructions']}→{outcome['instructions']} 증가 {outcome['instructions'] - plain['instructions']} > 상한 {FOLLOW_FOLDED_INSTRUCTION_DELTA_LIMIT}")
    plain, follow = compiles.get("static_shape0"), compiles.get("static_shape0_follow")
    if plain and follow and (plain["instructions"], plain["samples"]) != (follow["instructions"], follow["samples"]):
        problems.append("static_shape0_follow: 정적 역할인데 따라가기 1 이 명령·표본 수를 바꿈")
    return problems


def find_follow_runtime_problems(compiles):
    runtime = compiles.get("O3")
    if runtime is None:
        return ["O3 컴파일 결과 없음"]
    if runtime["exit"] != 0:
        return [f"O3 컴파일 실패 {runtime['error']}"]
    if runtime["instructions"] > FOLLOW_RUNTIME_O3_INSTRUCTION_LIMIT:
        return [f"O3(FogTerrainFollow 일반 입력) 명령 {runtime['instructions']} > 상한 {FOLLOW_RUNTIME_O3_INSTRUCTION_LIMIT}"]
    return []


def read_wrap_off_baseline(baseline_path):
    if not baseline_path:
        return WRAP_OFF_DXC_BASELINE, WRAP_OFF_DXC_BASELINE_O3_INSTRUCTIONS
    report = json.loads(pathlib.Path(baseline_path).read_text(encoding="utf-8"))
    dxc_counts = report.get("dxc", {})
    return {label: dxc_counts[label] for label in WRAP_OFF_DXC_BASELINE if label in dxc_counts}, dxc_counts.get("O3", {}).get("instructions")


def find_wrap_off_identity_problems(compiles, baseline):
    problems = []
    for label, expected in baseline.items():
        actual = compiles.get(label)
        if actual is None:
            problems.append(f"{label}: 컴파일 결과 없음")
            continue
        if actual["instructions"] != expected["instructions"] or actual["samples"] != expected["samples"]:
            problems.append(f"{label}: 명령 {actual['instructions']}·표본 {actual['samples']} ≠ 기준 {expected['instructions']}·{expected['samples']}")
    return problems


def find_static_wrap_problems(compiles):
    problems = []
    for shape_value in SHAPE_FOLDS:
        label = f"static_shape{int(shape_value)}"
        plain, wrapped = compiles.get(label), compiles.get(label + "_wrap")
        if plain is None or wrapped is None or plain["instructions"] != wrapped["instructions"] or plain["samples"] != wrapped["samples"]:
            problems.append(f"{label}: 정적 역할인데 감싸기 1 이 명령 수를 바꿈")
    return problems


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
    if fragment_path:
        fragment = json.loads(pathlib.Path(fragment_path).read_text(encoding="utf-8"))
        overlap = sorted(set(fragment.get("Materials", {})) & set(entries))
        entries.update(fragment.get("Materials", {}))
        return entries, overlap
    return entries, []


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parents[2]))
    parser.add_argument("--dxc", default="")
    parser.add_argument("--look-fragment", default="")
    parser.add_argument("--output", default="")
    parser.add_argument("--dxc-baseline", default="")
    arguments = parser.parse_args()
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    root = pathlib.Path(arguments.root)
    results = []
    summary = {}

    def record(label, passed, detail=""):
        results.append({"check": label, "passed": bool(passed), "detail": detail})
        print(("PASS " if passed else "FAIL ") + label + (": " + detail if detail else ""))

    sys.modules["unreal"] = mock.MagicMock()
    material = load_script(root, "editor_make_fluid_fog_material")
    record("재질 스크립트 unreal 스텁 임포트", True)
    stage_path = root / "Tools" / "FluidFX" / "editor_make_fluid_fog_stage.py"
    if stage_path.is_file():
        stage = load_script(root, "editor_make_fluid_fog_stage")
        layout_problems = stage.validate_stage_layout()
        record("무대 배치(종류별 창 × 상자 배율 이웃 간격·교반기 궤도·구역 8 바닥·200 m 상자 분리·상자 높이 ≥ 돌출+페이드·MPC 쓰기 액터 1개·MI 15)", not layout_problems, "; ".join(layout_problems))
    else:
        record("무대 스크립트 unreal 스텁 임포트", False, "editor_make_fluid_fog_stage.py 없음")

    source = material.read_fog_source(str(root))
    text_failures = run_shader_text_checks((root / "Tools" / "FluidFX" / "Shaders" / material.FOG_HLSL_NAME).read_text(encoding="utf-8"))
    record("셰이더 텍스트(주석·절대 경로·ASCII·LF)", not text_failures, "; ".join(text_failures))
    code = material.assemble_node_code(source)
    problems = material.validate_hlsl_code(code) + [f"입력 원천 없음: {name}" for name in material.find_unresolved_inputs()]
    record("조립 코드 자체 점검", not problems, "; ".join(problems))
    normalized = normalize(code)
    missing_anchors = [anchor for anchor in REQUIRED_ANCHORS if normalize(anchor) not in normalized]
    forbidden = [token for token in FORBIDDEN_TOKENS if token in code]
    record("식 고정점(바닥 = 반 크기 아래면, 창 UV, 역할 가중, 페이드)·금지 토큰", not missing_anchors and not forbidden, "; ".join(missing_anchors + forbidden))

    entries, overlap = collect_look_entries(root, arguments.look_fragment)
    override_problems = material.find_instance_override_problems(entries)
    record("MI 이름 계약(부모·마스터 이름·C++ 구동·조건부 이름 없음)", not override_problems, "; ".join(override_problems))
    record("조각과 정본 키 서로소", not overlap or not arguments.look_fragment, ", ".join(overlap))

    for summary_key, tests in (("mirror_sha256", BASE_MIRROR_TESTS), ("wrap_mirror_sha256", WRAP_MIRROR_TESTS), ("follow_mirror_sha256", FOLLOW_MIRROR_TESTS)):
        outcomes_first, digest_first = run_mirror_tests(tests)
        outcomes_second, digest_second = run_mirror_tests(tests)
        for label, (passed, detail, value_sha) in outcomes_first.items():
            record(f"거울 {label}", passed, f"{detail} sha256 {value_sha[:16]}")
        record(f"거울 시험 2회 sha256 같음({summary_key})", digest_first == digest_second, digest_first)
        summary[summary_key] = digest_first

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
            outcome = compile_source(dxc, build_source(material, code, {}), f"fog_{suffix}", optimize, work_folder)
            record(f"dxc 안개 ps_6_0 {suffix}", outcome["exit"] == 0 and outcome["samples"] >= 3, outcome["error"] or f"표본 {outcome['samples']} 명령 {outcome['instructions']}")
            compiles[suffix] = outcome
        for label, folded in (build_fold_permutations() | build_follow_permutations() | {"O3_follow_off": {"FogTerrainFollow": 0.0}}).items():
            outcome = compile_source(dxc, build_source(material, code, folded), label, True, work_folder)
            record(f"dxc 순열 {label} O3", outcome["exit"] == 0 and outcome["samples"] >= 2, outcome["error"] or f"표본 {outcome['samples']} 명령 {outcome['instructions']}")
            compiles[label] = outcome
    baseline, baseline_o3_instructions = read_wrap_off_baseline(arguments.dxc_baseline)
    identity_problems = find_wrap_off_identity_problems(compiles, baseline) if baseline else ["기준 dxc 명령 수 없음(--dxc-baseline 확인)"]
    record("D6 감싸기 끔 항등(SimAreaWrapped 0 접기 순열의 dxc 명령·표본 수 = 파동 1 기준)", not identity_problems, "; ".join(identity_problems) or f"순열 {len(baseline)}개 일치")
    static_wrap_problems = find_static_wrap_problems(compiles)
    record("D6 정적 역할은 감싸기 무시(감싸기 1 접기 = 0 접기 명령 수)", not static_wrap_problems, "; ".join(static_wrap_problems))
    follow_problems = find_follow_fold_problems(compiles)
    record(f"D8 따라가기 접기 순열(FogTerrainFollow 1, 역할 2 × 모양 0: 구동 표본 3·정적 2 = 끔과 같음, 같은 역할 끔 접기 대비 명령 증가 ≤ {FOLLOW_FOLDED_INSTRUCTION_DELTA_LIMIT})", not follow_problems, "; ".join(follow_problems))
    runtime_problems = find_follow_runtime_problems(compiles)
    record(f"D8b 따라가기 일반 입력(실제 재질 형태 = 접지 않은 O3, 명령 ≤ {FOLLOW_RUNTIME_O3_INSTRUCTION_LIMIT})", not runtime_problems, "; ".join(runtime_problems) or f"표본 {compiles['O3']['samples']} 명령 {compiles['O3']['instructions']}")
    summary["dxc"] = {label: {"samples": item["samples"], "instructions": item["instructions"]} for label, item in compiles.items()}
    summary["follow_runtime_dxc"] = {"samples": compiles["O3"]["samples"], "instructions": compiles["O3"]["instructions"], "limit": FOLLOW_RUNTIME_O3_INSTRUCTION_LIMIT, "vs_baseline": compiles["O3"]["instructions"] - WRAP_OFF_DXC_BASELINE_O3_INSTRUCTIONS, "follow_off_instructions": compiles["O3_follow_off"]["instructions"], "vs_follow_off": compiles["O3"]["instructions"] - compiles["O3_follow_off"]["instructions"]}
    summary["follow_instruction_delta"] = {f"{role_label}_shape0_follow_vs_off": compiles[f"{role_label}_shape0_follow"]["instructions"] - compiles[f"{role_label}_shape0"]["instructions"] for _, role_label in ROLE_FOLDS}
    summary["wrap_instruction_delta"] = {
        "O3_vs_baseline": compiles["O3"]["instructions"] - baseline_o3_instructions if baseline_o3_instructions else None,
        **{f"driven_shape{int(shape_value)}_wrap_vs_off": compiles[f"driven_shape{int(shape_value)}_wrap"]["instructions"] - compiles[f"driven_shape{int(shape_value)}"]["instructions"] for shape_value in SHAPE_FOLDS},
    }
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
