"""
실행 환경: Python 3.12 + numpy 2.2 + scipy 1.17 (추가 설치 없음), 저장소 루트에서 실행. 잠긴 fog3d_density_fixture.py·fog3d_terrain.py 는 고치지 않고 import 만 한다
실행 명령: python -B Tools/FluidFX/fog3d_ref/fog3d_density_fixture_s2a.py [--check] [--out-dir Tools/FluidFX/fog3d_ref/fixtures]
출력: fixtures/fog3d_density_fixture_s2a.bin(땅 표본 입력·기대값, 형식은 아래 pack_fixture), fixtures/fog3d_density_fixture_s2a.json(요약·sha256·자체 점검), --check 는 다시 만들어 바이트가 같은지 비교(다르면 종료 코드 1)
상태: 3D 안개 2a 단계 6 땅 기준 그리기 시험 GroundDensityMatchesReference 의 맞대기 자료(2026-11-15). 기대 농도 = 잠긴 expected_sample 에 창 바닥 z 대신 (창 바닥 z + H) 를 넣은 값, H = 높이 그림의 가장자리 고정 쌍선형(창 안은 fog3d_terrain.sample_columns 와 맞대 확인). 표본 z 와 탐침에 주는 H 는 2^-12cm 배수로 둬서 32비트 (z − 창 z) − H 뺄셈이 정확하다(z≈500cm 에서 그 반올림만으로 무게가 1.5e-6 까지 달라져 식 대조를 가리므로)
"""
import argparse
import hashlib
import json
import math
import os
import struct
import sys
import types

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fog3d_density_fixture as flat_fixture  # noqa: E402
import fog3d_terrain as terrain_tool  # noqa: E402

F32 = np.float32
MAGIC = b"TDF3DGD1"
FORMAT_VERSION = 1
SAMPLE_FLOATS = 20
PARAMETER_FLOATS = 32
CELL_CM = flat_fixture.CELL_CM
BAND_CELLS = flat_fixture.BAND_CELLS
SAMPLES_PER_CATEGORY = 384
SELF_CHECK_LIMIT = 1.0e-9
HEIGHT_STEP_CM = 2.0 ** -12
ZERO_GROUND_CHECK_LIMIT = 0.0
RANDOM_SEED = 20261115
DEFAULT_OUT_DIR = os.path.join(HERE, "fixtures")
FIXTURE_NAME = "fog3d_density_fixture_s2a.bin"
SUMMARY_NAME = "fog3d_density_fixture_s2a.json"
CATEGORIES = ("interior", "cliff_side", "window_edge", "map_outside", "displacement", "height_weight", "below_ground", "zero_ground")
ZONE = (4500.0, 4500.0, 400.0, 500.0)
SET_LAYOUTS = (
    dict(name="ground/fine_stain", window=(-2500.0, 1250.0, -20.0, 128.0), border=16, terrain="cliff_slope", fine=flat_fixture.FINE_ON, stain=flat_fixture.STAIN_ON),
    dict(name="ground/off", window=(-2500.0, 1250.0, -20.0, 128.0), border=16, terrain="cliff_slope", fine=flat_fixture.FINE_OFF, stain=flat_fixture.STAIN_OFF),
    dict(name="ground/wide_other_shape", window=(-1000.0, -1000.0, 10.0, 256.0), border=64, terrain="rolling_ridge", base=(0.6, 120.0, 60.0, 0.0),
         fine=(0.25, 37.5, 3.0, 0.0), stain=(0.25, 150.0, 11.0, 0.0)),
    dict(name="ground/inactive", window=(-2500.0, 1250.0, -20.0, 128.0), border=16, terrain="cliff_slope", active=0.0, fine=flat_fixture.FINE_ON, stain=flat_fixture.STAIN_ON),
    dict(name="ground/zero", window=(-1800.0, -400.0, 0.0, 128.0), border=16, terrain="zero", fine=flat_fixture.FINE_ON, stain=flat_fixture.STAIN_ON),
)
GROUND_SET_INDICES = (0, 1, 2, 3)
CLIFF_SET_INDICES = (0, 1, 3)
ZERO_SET_INDEX = 4
CLIFF_X_CM = -1300.0
CLIFF_SLANT = 0.2
CLIFF_Y0_CM = 1900.0
CLIFF_RISE_CM = 180.0


def cliff_line_x(y):
    return CLIFF_X_CM + CLIFF_SLANT * (y - CLIFF_Y0_CM)


def terrain_height(kind, x, y):
    if kind == "zero":
        return np.zeros(np.broadcast(x, y).shape)
    if kind == "cliff_slope":
        return 0.10 * (x + 1500.0) + 0.05 * (y - CLIFF_Y0_CM) + 60.0 * np.sin(x / 310.0) * np.cos(y / 270.0) + np.where(x > cliff_line_x(y), CLIFF_RISE_CM, 0.0) - 60.0
    if kind == "rolling_ridge":
        return 90.0 * np.sin(x / 520.0) * np.sin(y / 610.0) + 0.08 * (y + 1000.0) - 80.0 + 40.0 * np.cos((x + y) / 230.0)
    raise ValueError(kind)


def make_ground_set(layout):
    window = layout["window"]
    border = layout["border"]
    size = int(window[3]) + 2 * border
    ground = (window[0] - border * CELL_CM, window[1] - border * CELL_CM, float(size), float(size))
    parameter_set = flat_fixture.make_parameter_set(layout["name"], window, active=layout.get("active", 1.0), base=layout.get("base", flat_fixture.BASE_SHAPE),
                                                    zone=ZONE, fine=layout["fine"], stain=layout["stain"])
    parameter_set["ground"] = tuple(flat_fixture.f32(v) for v in ground)
    centers = parameter_set["ground"][0] + (np.arange(size) + 0.5) * CELL_CM, parameter_set["ground"][1] + (np.arange(size) + 0.5) * CELL_CM
    heights = terrain_height(layout["terrain"], centers[0][:, None], centers[1][None, :]).astype(F32)
    parameter_set["map"] = heights
    parameter_set["border"] = border
    parameter_set["terrain"] = layout["terrain"]
    return parameter_set


def ground_uv(parameter_set, x, y):
    gx, gy, sx, sy = parameter_set["ground"]
    return (min(max((x - gx) / (sx * CELL_CM), 0.5 / sx), 1.0 - 0.5 / sx), min(max((y - gy) / (sy * CELL_CM), 0.5 / sy), 1.0 - 0.5 / sy))


def bilinear_clamped(heights, uv):
    size_x, size_y = heights.shape
    fx = uv[0] * size_x - 0.5
    fy = uv[1] * size_y - 0.5
    i0 = min(int(math.floor(fx)), size_x - 2)
    j0 = min(int(math.floor(fy)), size_y - 2)
    tx, ty = fx - i0, fy - j0
    h = heights.astype(np.float64)
    return float((h[i0, j0] * (1 - tx) + h[i0 + 1, j0] * tx) * (1 - ty) + (h[i0, j0 + 1] * (1 - tx) + h[i0 + 1, j0 + 1] * tx) * ty)


def reference_window_height(parameter_set, x, y):
    border = parameter_set["border"]
    n = int(parameter_set["values"][3])
    window_columns = parameter_set["map"][border:border + n, border:border + n].astype(np.float64)
    window_x, window_y = parameter_set["values"][0], parameter_set["values"][1]
    shim = types.SimpleNamespace(p=types.SimpleNamespace(hx=CELL_CM, hy=CELL_CM), nx=n, ny=n,
                                 center_rel=lambda: (window_x + (np.arange(n) + 0.5) * CELL_CM, window_y + (np.arange(n) + 0.5) * CELL_CM, None))
    return float(terrain_tool.TerrainSim.sample_columns(shim, window_columns, x, y))


def is_inside_window_columns(parameter_set, x, y):
    window_x, window_y, _, n = parameter_set["values"][0:4]
    low, high = 0.5 * CELL_CM, (n - 0.5) * CELL_CM
    return low <= x - window_x <= high and low <= y - window_y <= high


def lifted_set(parameter_set, ground_height):
    values = list(parameter_set["values"])
    values[2] = values[2] + ground_height
    return dict(values=tuple(values))


def float32_height_and_weight(parameter_set, position, ground_height):
    window, grid = parameter_set["values"][0:4], parameter_set["values"][4:8]
    z, wz, h = F32(position[2]), F32(window[2]), F32(ground_height)
    height = (z - wz) - h
    top = F32(grid[1]) * F32(grid[2])
    edge0, edge1 = top - F32(grid[1]), top
    t = np.clip((height - edge0) / (edge1 - edge0), F32(0), F32(1))
    weight = F32(1) - t * t * (F32(3) - F32(2) * t)
    return float(height / top), float(weight)


def draw_position(rng, category, parameter_set):
    window_x, window_y, _, n = parameter_set["values"][0:4]
    size_cm = n * CELL_CM
    gx, gy, sx, _ = parameter_set["ground"]
    map_cm = sx * CELL_CM
    if category == "cliff_side":
        y = window_y + rng.uniform(BAND_CELLS * CELL_CM, size_cm - BAND_CELLS * CELL_CM)
        return cliff_line_x(y) + rng.uniform(-2.0 * CELL_CM, 2.0 * CELL_CM), y
    if category == "window_edge":
        near = rng.uniform(-2.0 * CELL_CM, 10.0 * CELL_CM)
        value = size_cm - near if rng.integers(0, 2) else near
        other = rng.uniform(0.0, size_cm)
        return (window_x + value, window_y + other) if rng.integers(0, 2) == 0 else (window_x + other, window_y + value)
    if category == "map_outside":
        near = rng.uniform(-10.0 * CELL_CM, 1.0 * CELL_CM)
        value = map_cm - near if rng.integers(0, 2) else near
        other = rng.uniform(-10.0 * CELL_CM, map_cm + 10.0 * CELL_CM)
        return (gx + value, gy + other) if rng.integers(0, 2) == 0 else (gx + other, gy + value)
    if rng.uniform() < 0.2:
        return window_x + rng.uniform(-2.0 * CELL_CM, size_cm + 2.0 * CELL_CM), window_y + rng.uniform(-2.0 * CELL_CM, size_cm + 2.0 * CELL_CM)
    return window_x + rng.uniform(BAND_CELLS * CELL_CM, size_cm - BAND_CELLS * CELL_CM), window_y + rng.uniform(BAND_CELLS * CELL_CM, size_cm - BAND_CELLS * CELL_CM)


def draw_height_and_state(rng, category, top_cm, layer_cm):
    d = [rng.uniform(-40.0, 40.0), rng.uniform(-40.0, 40.0), rng.uniform(-40.0, 40.0)]
    g = rng.uniform(-1.0, 0.2)
    height = rng.uniform(0.0, 230.0)
    if category == "cliff_side":
        height = rng.uniform(-40.0, 230.0)
    if category == "displacement":
        d = [rng.uniform(-125.0, 125.0), rng.uniform(-125.0, 125.0), rng.uniform(-120.0, 120.0)]
    if category == "height_weight":
        height = rng.uniform(top_cm - 2.0 * layer_cm + 20.0, top_cm + 10.0)
        d = [rng.uniform(-60.0, 60.0), rng.uniform(-60.0, 60.0), rng.uniform(80.0, 220.0)]
    if category == "below_ground":
        height = rng.uniform(-60.0, 40.0)
        d = [rng.uniform(-30.0, 30.0), rng.uniform(-30.0, 30.0), rng.uniform(max(height, 0.0) + 1.0, max(height, 0.0) + 110.0)]
    return height, (*d, g)


def quantize_height(value):
    return round(value / HEIGHT_STEP_CM) * HEIGHT_STEP_CM


def make_sample(parameter_sets, set_index, category_index, position_xy, height, state, notes):
    parameter_set = parameter_sets[set_index]
    uv = ground_uv(parameter_set, position_xy[0], position_xy[1])
    exact_height = bilinear_clamped(parameter_set["map"], uv)
    position = (flat_fixture.f32(position_xy[0]), flat_fixture.f32(position_xy[1]), flat_fixture.f32(quantize_height(parameter_set["values"][2] + exact_height + height)))
    state = tuple(flat_fixture.f32(v) for v in state)
    uv = ground_uv(parameter_set, position[0], position[1])
    exact_height = bilinear_clamped(parameter_set["map"], uv)
    ground_height = flat_fixture.f32(quantize_height(exact_height))
    notes["height_quantization_max"] = max(notes["height_quantization_max"], abs(ground_height - exact_height))
    if is_inside_window_columns(parameter_set, position[0], position[1]):
        notes["window_reference_checked"] += 1
        notes["window_reference_max"] = max(notes["window_reference_max"], abs(exact_height - reference_window_height(parameter_set, position[0], position[1])))
    check = flat_fixture.expected_sample(lifted_set(parameter_set, ground_height), position, state)
    if set_index == ZERO_SET_INDEX:
        flat_check = flat_fixture.expected_sample(parameter_set, position, state)
        notes["zero_ground_max"] = max(notes["zero_ground_max"], abs(flat_check["density"] - check["density"]), abs(flat_check["weight"] - check["weight"]),
                                       max(abs(a - b) for a, b in zip(flat_check["uvw"], check["uvw"])))
    emulated_w, emulated_weight = float32_height_and_weight(parameter_set, position, ground_height)
    notes["float32_uvw_z_max"] = max(notes["float32_uvw_z_max"], abs(emulated_w - check["uvw"][2]))
    if parameter_set["values"][8] > 0 and check["band_cells"] >= parameter_set["values"][7]:
        notes["float32_weight_max"] = max(notes["float32_weight_max"], abs(emulated_weight - check["weight"]))
    return dict(position=position, state=state, set_index=set_index, category=category_index, ground_height=ground_height, uv=uv, uvw=check["uvw"],
                weight=check["weight"], density=check["density"], check=check)


def build_samples(rng, parameter_sets):
    samples = []
    notes = dict(height_quantization_max=0.0, window_reference_checked=0, window_reference_max=0.0, zero_ground_max=0.0, float32_uvw_z_max=0.0, float32_weight_max=0.0, outside_map=0)
    for category_index, category in enumerate(CATEGORIES):
        made = 0
        while made < SAMPLES_PER_CATEGORY:
            if category == "zero_ground":
                set_index = ZERO_SET_INDEX
            elif category == "cliff_side":
                set_index = CLIFF_SET_INDICES[int(rng.integers(0, len(CLIFF_SET_INDICES)))]
            else:
                set_index = GROUND_SET_INDICES[int(rng.integers(0, len(GROUND_SET_INDICES)))]
            parameter_set = parameter_sets[set_index]
            grid = parameter_set["values"][4:8]
            draw_category = CATEGORIES[int(rng.integers(0, 7))] if category == "zero_ground" else category
            position_xy = draw_position(rng, draw_category, parameter_set)
            height, state = draw_height_and_state(rng, draw_category, grid[1] * grid[2], grid[1])
            sample = make_sample(parameter_sets, set_index, category_index, position_xy, height, state, notes)
            if flat_fixture.is_too_close_to_step(parameter_set, sample["check"]):
                continue
            gx, gy, sx, sy = parameter_set["ground"]
            if not (0.0 <= sample["position"][0] - gx <= sx * CELL_CM and 0.0 <= sample["position"][1] - gy <= sy * CELL_CM):
                notes["outside_map"] += 1
            samples.append(sample)
            made += 1
    return samples, notes


def pack_fixture(parameter_sets, samples):
    chunks = [MAGIC, struct.pack("<5I", FORMAT_VERSION, len(parameter_sets), len(samples), SAMPLE_FLOATS, PARAMETER_FLOATS)]
    for parameter_set in parameter_sets:
        chunks.append(struct.pack("<32f", *parameter_set["values"], *parameter_set["ground"]))
    for sample in samples:
        chunks.append(struct.pack("<3fI4f3fI4f4f", *sample["position"], sample["set_index"], *sample["state"], sample["ground_height"], *sample["uv"],
                                  sample["category"], *sample["uvw"], sample["weight"], sample["density"], 0.0, 0.0, 0.0))
    return b"".join(chunks)


def summarize(parameter_sets, samples, notes, blob):
    categories = {}
    for index, name in enumerate(CATEGORIES):
        chosen = [s for s in samples if s["category"] == index]
        categories[name] = dict(count=len(chosen), density_min=min(s["density"] for s in chosen), density_max=max(s["density"] for s in chosen),
                                ground_height_min=min(s["ground_height"] for s in chosen), ground_height_max=max(s["ground_height"] for s in chosen),
                                non_zero_density=sum(1 for s in chosen if s["density"] > 0.0))
    sets = []
    for index, parameter_set in enumerate(parameter_sets):
        heights = parameter_set["map"]
        sets.append(dict(index=index, name=parameter_set["name"], values=list(parameter_set["values"]), ground=list(parameter_set["ground"]), border_cells=parameter_set["border"],
                         terrain=parameter_set["terrain"], map_sha256=hashlib.sha256(heights.astype("<f4").tobytes()).hexdigest(),
                         map_height_range=[float(heights.min()), float(heights.max())]))
    return dict(format="fog3d-density-fixture-s2a-1", binary=FIXTURE_NAME, sha256=hashlib.sha256(blob).hexdigest(), bytes=len(blob),
                layout=dict(header="8 bytes magic TDF3DGD1 + uint32 version, parameter_set_count, sample_count, floats_per_sample (20), floats_per_parameter_set (32)",
                            parameter_set="28 float32 as fog3d_density_fixture.bin + ground(map min x, map min y, S, S) relative to the zone reference, cm and texels",
                            sample="v0 position float32x3 + set_index uint32, v1 state (dx,dy,dz,g), v2 ground height cm + expected ground uv float32x2 + category uint32, v3 expected uvw float32x3 + expected weight, v4 expected density + 3 zero"),
                expected="density/uvw/weight = fog3d_density_fixture.expected_sample with window z replaced by window z + H (H = float32 ground height given to the probe); uv = clamp((xy - ground.xy) / (S h), 0.5/S, 1 - 0.5/S)",
                parameter_sets=sets, sample_count=len(samples), categories=categories, self_checks=notes,
                source=dict(tool="fog3d_density_fixture_s2a.py", code=tool_fingerprint()))


def tool_fingerprint():
    out = {}
    for name in ("fog3d_density_fixture_s2a.py", "fog3d_density_fixture.py", "fog3d_terrain.py", "fog3d_core.py"):
        with open(os.path.join(HERE, name), "rb") as handle:
            out[name] = hashlib.sha256(handle.read()).hexdigest()[:16]
    return out


def build():
    rng = np.random.default_rng(RANDOM_SEED)
    parameter_sets = [make_ground_set(layout) for layout in SET_LAYOUTS]
    samples, notes = build_samples(rng, parameter_sets)
    blob = pack_fixture(parameter_sets, samples)
    return blob, summarize(parameter_sets, samples, notes, blob)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--out-dir", default=DEFAULT_OUT_DIR)
    args = parser.parse_args()
    blob, summary = build()
    notes = summary["self_checks"]
    summary_text = json.dumps(summary, ensure_ascii=False, indent=1) + "\n"
    print(f"samples {summary['sample_count']} parameter_sets {len(summary['parameter_sets'])} bytes {len(blob)} sha256 {summary['sha256']}")
    print(f"window columns vs sample_columns: checked {notes['window_reference_checked']} max {notes['window_reference_max']:.3e}; zero ground vs flat max {notes['zero_ground_max']:.3e}; "
          f"float32 emulation uvw.z max {notes['float32_uvw_z_max']:.3e} weight max {notes['float32_weight_max']:.3e}; outside map {notes['outside_map']}")
    empty = [name for name, values in summary["categories"].items() if values["non_zero_density"] == 0]
    if notes["window_reference_max"] > SELF_CHECK_LIMIT or notes["window_reference_checked"] == 0:
        print("FAIL 높이 그림 쌍선형이 창 안에서 fog3d_terrain.sample_columns 와 다름")
        return 1
    if notes["zero_ground_max"] > ZERO_GROUND_CHECK_LIMIT:
        print("FAIL 높이 0 표본이 1단계 기대값과 다름")
        return 1
    if empty or notes["outside_map"] == 0:
        print(f"FAIL 표본 범주 부족: 농도 0 뿐인 범주 {empty}, 그림 밖 표본 {notes['outside_map']}")
        return 1
    fixture_path = os.path.join(args.out_dir, FIXTURE_NAME)
    summary_path = os.path.join(args.out_dir, SUMMARY_NAME)
    if args.check:
        with open(fixture_path, "rb") as handle:
            same_blob = handle.read() == blob
        with open(summary_path, encoding="utf-8") as handle:
            same_summary = handle.read() == summary_text
        print(f"check binary {'same' if same_blob else 'DIFFERENT'} summary {'same' if same_summary else 'DIFFERENT'}")
        return 0 if same_blob and same_summary else 1
    os.makedirs(args.out_dir, exist_ok=True)
    with open(fixture_path, "wb") as handle:
        handle.write(blob)
    with open(summary_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(summary_text)
    print(f"wrote {fixture_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
