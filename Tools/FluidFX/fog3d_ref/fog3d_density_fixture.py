"""
실행 환경: Python 3.12 + numpy 2.2 + scipy 1.17 (추가 설치 없음), 저장소 루트에서 실행. fog3d_core.py·fog3d_replay.py 는 고치지 않고 import 만 한다
실행 명령: python -B Tools/FluidFX/fog3d_ref/fog3d_density_fixture.py [--check] [--out-dir Tools/FluidFX/fog3d_ref/fixtures]
출력: fixtures/fog3d_density_fixture.bin(표본 입력·기대 농도, 형식은 아래 write_fixture), fixtures/fog3d_density_fixture.json(요약·sha256), --check 는 다시 만들어 바이트가 같은지 비교(다르면 종료 코드 1)
상태: 3D 안개 1b B5 재질 식 시험의 맞대기 자료. 2026-11-13 G1 작성. 기대 농도 = 기준 모의 drawn_density_v3·base_at 에 명세 4절 무게·구역 흐림을 곱한 값
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
import fog3d_core as core  # noqa: E402
import fog3d_replay as replay_tool  # noqa: E402

F32 = np.float32
MAGIC = b"TDF3DDN1"
FORMAT_VERSION = 1
SAMPLE_FLOATS = 16
PARAMETER_FLOATS = 28
CELL_CM = 15.625
LAYER_CM = 30.0
LAYER_COUNT = 8
BAND_CELLS = 8.0
REPLAY_NAMES = ("k2_swing_low", "k1_downstrike")
REPLAY_STOP_DUMPS = {"k2_swing_low": None, "k1_downstrike": "t0p5"}
REPLAY_SAMPLES_PER_SCENE = 1024
REPLAY_DISTURBED_SHARE = 0.5
REPLAY_LAYER_LIMIT = 7
SYNTHETIC_SAMPLE_COUNT = 2048
SELF_CHECK_LIMIT = 1.0e-6
RANDOM_SEED = 20261113
DEFAULT_OUT_DIR = os.path.join(HERE, "fixtures")
FIXTURE_NAME = "fog3d_density_fixture.bin"
SUMMARY_NAME = "fog3d_density_fixture.json"
BASE_SHAPE = (0.8, 90.0, 45.0, 0.0)
GRID = (CELL_CM, LAYER_CM, float(LAYER_COUNT), BAND_CELLS)
HUGE_ZONE = (1.0e6, 1.0e6, 1.0e6, 500.0)
FINE_OFF = (0.0, 50.0, 1.0, 0.0)
FINE_ON = (0.2, 50.0, 1.0, 0.0)
STAIN_OFF = (0.0, 200.0, 0.0, 0.0)
STAIN_ON = (0.3, 200.0, 7.0, 0.0)
PATTERNS = (("off", FINE_OFF, STAIN_OFF), ("fine", FINE_ON, STAIN_OFF), ("fine_stain", FINE_ON, STAIN_ON))
CATEGORIES = ("replay_interior", "curve_blur", "fill_range", "displacement", "height_weight", "band_edge", "zone_edge", "below_floor")


def f32(value):
    return float(F32(value))


def smoothstep(e0, e1, x):
    return float(core.smoothstep(e0, e1, x))


def make_parameter_set(name, window, grid=GRID, active=1.0, base=BASE_SHAPE, zone=HUGE_ZONE, fine=FINE_OFF, stain=STAIN_OFF):
    values = tuple(f32(v) for v in (*window, *grid, active, 0.0, 0.0, 0.0, *base, *zone, *fine, *stain))
    return dict(name=name, values=values)


def unpack_set(parameter_set):
    v = parameter_set["values"]
    return dict(window=v[0:4], grid=v[4:8], active=v[8], base=v[12:16], zone=v[16:20], fine=v[20:24], stain=v[24:28])


def reference_params(unpacked):
    base, fine, stain = unpacked["base"], unpacked["fine"], unpacked["stain"]
    return core.Params(hx=CELL_CM, hy=CELL_CM, base_density=base[0], thickness_cm=base[1], top_blur_cm=base[2],
                       fine_amp=fine[0], fine_spacing_cm=fine[1], fine_seed=int(fine[2] + 0.5),
                       stain_amp=stain[0], stain_spacing_cm=stain[1], stain_seed=int(stain[2] + 0.5))


def reference_rest_density(params, origin_x, origin_y, origin_height):
    cell_x = int(math.floor(origin_x / CELL_CM))
    cell_y = int(math.floor(origin_y / CELL_CM))
    offset_x = origin_x - (cell_x + 0.5) * CELL_CM
    offset_y = origin_y - (cell_y + 0.5) * CELL_CM
    shim = types.SimpleNamespace(p=params, world_cells=lambda: (np.array([cell_x], np.int64), np.array([cell_y], np.int64)))
    value = core.Sim.base_at(shim, np.full((1, 1, 1), offset_x), np.full((1, 1, 1), offset_y), np.full((1, 1, 1), origin_height))
    return float(np.asarray(value, np.float64).reshape(-1)[0])


def expected_sample(parameter_set, position, state):
    s = unpack_set(parameter_set)
    window, grid, zone = s["window"], s["grid"], s["zone"]
    cell, layer_cm, layer_count, band = grid
    size_cm = window[3] * cell
    top_cm = layer_cm * layer_count
    pw_x, pw_y = position[0] - window[0], position[1] - window[1]
    height = position[2] - window[2]
    uvw = (pw_x / size_cm, pw_y / size_cm, height / top_cm)
    band_cells = min(pw_x, pw_y, size_cm - pw_x, size_cm - pw_y) / cell
    fade = smoothstep(0.0, band, band_cells) if band > 0 else (1.0 if band_cells >= 0 else 0.0)
    weight = fade * s["active"] * (1.0 - smoothstep(top_cm - layer_cm, top_cm, height))
    dx, dy, dz, g = (component * weight for component in state)
    rest = reference_rest_density(reference_params(s), position[0] - dx, position[1] - dy, height - dz)
    inside = min(zone[0] - abs(position[0]), zone[1] - abs(position[1]))
    zone_fade = smoothstep(0.0, zone[3], inside) if zone[3] > 0 else (1.0 if inside >= 0 else 0.0)
    density = max(0.0, 1.0 + g) * rest * zone_fade
    return dict(uvw=uvw, weight=weight, density=density, band_cells=band_cells, zone_inside=inside)


def run_replay_to_sample_point(name):
    path = os.path.join(replay_tool.DEFAULT_REPLAYS, name + ".json")
    with open(path, encoding="utf-8") as handle:
        replay = json.load(handle)
    captured = {}
    original_make_sim = replay_tool.make_sim

    def capturing_make_sim(body):
        captured["sim"] = original_make_sim(body)
        return captured["sim"]

    replay_tool.make_sim = capturing_make_sim
    try:
        replay_tool.execute(replay, stop_after_dump=REPLAY_STOP_DUMPS[name])
    finally:
        replay_tool.make_sim = original_make_sim
    return captured["sim"]


def choose_replay_cells(sim, rng):
    n = sim.nx
    index = np.arange(n)
    interior = (index >= BAND_CELLS) & (index <= n - 1 - BAND_CELLS)
    mask = interior[:, None, None] & interior[None, :, None] & (np.arange(sim.nz) < REPLAY_LAYER_LIMIT)[None, None, :] & sim.fluid
    disturbed = mask & ((np.abs(sim.dpx) + np.abs(sim.dpy) + np.abs(sim.dpz) > 0.5) | (np.abs(sim.g) > 1.0e-3))
    disturbed_cells = np.argwhere(disturbed)
    want_disturbed = min(len(disturbed_cells), int(REPLAY_SAMPLES_PER_SCENE * REPLAY_DISTURBED_SHARE))
    picked = disturbed_cells[rng.choice(len(disturbed_cells), want_disturbed, replace=False)] if want_disturbed else np.zeros((0, 3), np.int64)
    taken = set(map(tuple, picked.tolist()))
    rest_cells = np.array([c for c in np.argwhere(mask).tolist() if tuple(c) not in taken], np.int64)
    filler = rest_cells[rng.choice(len(rest_cells), REPLAY_SAMPLES_PER_SCENE - want_disturbed, replace=False)]
    cells = np.concatenate([picked, filler], axis=0)
    return cells[np.lexsort((cells[:, 2], cells[:, 1], cells[:, 0]))], int(disturbed.sum()), want_disturbed


def build_replay_samples(rng, parameter_sets, samples, notes):
    for name in REPLAY_NAMES:
        sim = run_replay_to_sample_point(name)
        window = (sim.origin[0] * CELL_CM, sim.origin[1] * CELL_CM, 0.0, float(sim.nx))
        cells, disturbed_total, disturbed_taken = choose_replay_cells(sim, rng)
        heights = sim.zc[:REPLAY_LAYER_LIMIT].astype(np.float64)
        world_x, world_y = sim.world_cells()
        scene_note = dict(scene=name, stop_dump=REPLAY_STOP_DUMPS[name], window_min_cell=list(map(int, sim.origin)), disturbed_cells=disturbed_total,
                          disturbed_samples=disturbed_taken, state_max=sim.state_max(), self_check_max=0.0)
        for pattern_name, fine, stain in PATTERNS:
            set_index = len(parameter_sets)
            parameter_sets.append(make_parameter_set(f"{name}/{pattern_name}", window, fine=fine, stain=stain))
            sim.p.fine_amp, sim.p.fine_spacing_cm, sim.p.fine_seed = fine[0], fine[1], int(fine[2] + 0.5)
            sim.p.stain_amp, sim.p.stain_spacing_cm, sim.p.stain_seed = stain[0], stain[1], int(stain[2] + 0.5)
            drawn = sim.drawn_density_v3(heights)
            for i, j, k in cells.tolist():
                position = ((world_x[i] + 0.5) * CELL_CM, (world_y[j] + 0.5) * CELL_CM, float(sim.zc[k]))
                state = (float(sim.dpx[i, j, k]), float(sim.dpy[i, j, k]), float(sim.dpz[i, j, k]), float(sim.g[i, j, k]))
                check = expected_sample(parameter_sets[set_index], position, state)
                reference = float(drawn[i, j, k])
                scene_note["self_check_max"] = max(scene_note["self_check_max"], abs(check["density"] - reference), abs(check["weight"] - 1.0))
                samples.append(dict(position=position, state=state, set_index=set_index, category=0, uvw=check["uvw"], weight=1.0, density=reference))
        notes.append(scene_note)


def synthetic_sets():
    base_window = (-3000.0, 1250.0, 0.0, 128.0)
    zone = (3500.0, 2800.0, 400.0, 500.0)
    return [
        make_parameter_set("synthetic/fine_stain", base_window, zone=zone, fine=FINE_ON, stain=STAIN_ON),
        make_parameter_set("synthetic/fine", base_window, zone=zone, fine=FINE_ON),
        make_parameter_set("synthetic/off", base_window, zone=zone),
        make_parameter_set("synthetic/wide_other_shape", (-1000.0, -1000.0, -20.0, 256.0), zone=(2600.0, 2400.0, 300.0, 400.0), base=(0.6, 120.0, 60.0, 0.0),
                           fine=(0.25, 37.5, 3.0, 0.0), stain=(0.25, 150.0, 11.0, 0.0)),
        make_parameter_set("synthetic/inactive", base_window, active=0.0, zone=zone, fine=FINE_ON, stain=STAIN_ON),
        make_parameter_set("synthetic/band_off", base_window, grid=(CELL_CM, LAYER_CM, float(LAYER_COUNT), 0.0), zone=zone, fine=FINE_ON, stain=STAIN_ON),
        make_parameter_set("synthetic/zone_hard_edge", base_window, zone=(3500.0, 2800.0, 400.0, 0.0), fine=FINE_ON, stain=STAIN_ON),
    ]


def draw_window_position(rng, size_cm, category):
    if category == "band_edge":
        near = rng.uniform(-2.0 * CELL_CM, 10.0 * CELL_CM)
        edge_axis = rng.integers(0, 2)
        far_side = rng.integers(0, 2)
        other = rng.uniform(0.0, size_cm)
        value = size_cm - near if far_side else near
        return (value, other) if edge_axis == 0 else (other, value)
    if rng.uniform() < 0.2:
        return tuple(rng.uniform(-2.0 * CELL_CM, size_cm + 2.0 * CELL_CM, 2))
    return tuple(rng.uniform(BAND_CELLS * CELL_CM, size_cm - BAND_CELLS * CELL_CM, 2))


def draw_synthetic_sample(rng, category, parameter_set):
    s = unpack_set(parameter_set)
    window, grid, zone = s["window"], s["grid"], s["zone"]
    size_cm = window[3] * grid[0]
    top_cm = grid[1] * grid[2]
    pw = draw_window_position(rng, size_cm, category)
    position = [window[0] + pw[0], window[1] + pw[1], 0.0]
    d = [0.0, 0.0, 0.0]
    g = rng.uniform(-1.0, 0.0)
    height = rng.uniform(0.0, 200.0)
    if category == "curve_blur":
        height = rng.uniform(20.0, 160.0)
        d = [rng.uniform(-30.0, 30.0), rng.uniform(-30.0, 30.0), rng.uniform(-40.0, 40.0)]
    if category == "fill_range":
        g = rng.uniform(-1.3, 0.4)
        d = list(rng.uniform(-20.0, 20.0, 3))
    if category == "displacement":
        d = [rng.uniform(-125.0, 125.0), rng.uniform(-125.0, 125.0), rng.uniform(-120.0, 120.0)]
        g = rng.uniform(-1.0, 0.2)
    if category == "height_weight":
        height = rng.uniform(top_cm - 2.0 * grid[1] + 20.0, top_cm + 10.0)
        d = [rng.uniform(-60.0, 60.0), rng.uniform(-60.0, 60.0), rng.uniform(80.0, 220.0)]
    if category == "band_edge":
        d = [rng.uniform(-80.0, 80.0), rng.uniform(-80.0, 80.0), rng.uniform(-60.0, 60.0)]
        g = rng.uniform(-1.0, 0.0)
    if category == "zone_edge":
        width = zone[3] if zone[3] > 0 else 300.0
        side = 1.0 if rng.uniform() < 0.5 else -1.0
        edge_axis = rng.integers(0, 2)
        edge_value = side * (zone[edge_axis] + rng.uniform(-1.2 * width, 0.3 * width))
        other_value = rng.uniform(-0.5, 0.5) * zone[1 - edge_axis]
        position[0], position[1] = (edge_value, other_value) if edge_axis == 0 else (other_value, edge_value)
        d = list(rng.uniform(-40.0, 40.0, 3))
    if category == "below_floor":
        height = rng.uniform(0.0, 40.0)
        d = [rng.uniform(-30.0, 30.0), rng.uniform(-30.0, 30.0), rng.uniform(height + 1.0, height + 110.0)]
    position[2] = window[2] + height
    return tuple(f32(v) for v in position), tuple(f32(v) for v in (*d, g))


def is_too_close_to_step(parameter_set, check):
    s = unpack_set(parameter_set)
    if s["grid"][3] <= 0 and abs(check["band_cells"]) < 1.0e-3:
        return True
    return s["zone"][3] <= 0 and abs(check["zone_inside"]) < 1.0e-2


def build_synthetic_samples(rng, parameter_sets, samples):
    first_set = len(parameter_sets)
    parameter_sets.extend(synthetic_sets())
    set_indices = list(range(first_set, len(parameter_sets)))
    synthetic_categories = CATEGORIES[1:]
    per_category = SYNTHETIC_SAMPLE_COUNT // len(synthetic_categories)
    for category_index, category in enumerate(synthetic_categories, start=1):
        made = 0
        while made < per_category:
            set_index = set_indices[int(rng.integers(0, len(set_indices)))]
            position, state = draw_synthetic_sample(rng, category, parameter_sets[set_index])
            check = expected_sample(parameter_sets[set_index], position, state)
            if is_too_close_to_step(parameter_sets[set_index], check):
                continue
            samples.append(dict(position=position, state=state, set_index=set_index, category=category_index, uvw=check["uvw"], weight=check["weight"],
                                density=check["density"]))
            made += 1


def pack_fixture(parameter_sets, samples):
    chunks = [MAGIC, struct.pack("<4I", FORMAT_VERSION, len(parameter_sets), len(samples), SAMPLE_FLOATS)]
    for parameter_set in parameter_sets:
        chunks.append(struct.pack("<28f", *parameter_set["values"]))
    for sample in samples:
        chunks.append(struct.pack("<3fI4f3fffI2I", *sample["position"], sample["set_index"], *sample["state"], *sample["uvw"], sample["weight"],
                                  sample["density"], sample["category"], 0, 0))
    return b"".join(chunks)


def summarize(parameter_sets, samples, notes, blob):
    density = np.array([s["density"] for s in samples])
    weight = np.array([s["weight"] for s in samples])
    categories = {}
    for index, name in enumerate(CATEGORIES):
        chosen = [s for s in samples if s["category"] == index]
        categories[name] = dict(count=len(chosen), density_min=min(s["density"] for s in chosen), density_max=max(s["density"] for s in chosen),
                                weight_min=min(s["weight"] for s in chosen), weight_max=max(s["weight"] for s in chosen))
    return dict(format="fog3d-density-fixture-1", binary=FIXTURE_NAME, sha256=hashlib.sha256(blob).hexdigest(), bytes=len(blob),
                layout=dict(header="8 bytes magic TDF3DDN1 + uint32 version, parameter_set_count, sample_count, floats_per_sample",
                            parameter_set="28 float32: window(x,y,z,N) grid(cell,layer,layers,band) active(a,0,0,0) base(density,thickness,top_blur,0) zone(half_x,half_y,half_z,fade) fine(amp,spacing,seed,0) stain(amp,spacing,seed,0)",
                            sample="position float32x3, set_index uint32, state float32x4 (dx,dy,dz,g), expected uvw float32x3, expected weight float32, expected density float32, category uint32, 2 uint32 zero"),
                parameter_sets=[dict(index=i, name=p["name"], values=list(p["values"])) for i, p in enumerate(parameter_sets)], sample_count=len(samples),
                categories=categories, density_range=[float(density.min()), float(density.max())], weight_range=[float(weight.min()), float(weight.max())],
                replay_scenes=notes, source=dict(tool="fog3d_density_fixture.py", code=tool_fingerprint()))


def tool_fingerprint():
    names = ["fog3d_density_fixture.py", "fog3d_core.py", "fog3d_replay.py", "tuned_solvers.json"] + [os.path.join("replays", n + ".json") for n in REPLAY_NAMES]
    out = {}
    for name in names:
        with open(os.path.join(HERE, name), "rb") as handle:
            out[name.replace(os.sep, "/")] = hashlib.sha256(handle.read()).hexdigest()[:16]
    return out


def build():
    rng = np.random.default_rng(RANDOM_SEED)
    parameter_sets, samples, notes = [], [], []
    build_replay_samples(rng, parameter_sets, samples, notes)
    build_synthetic_samples(rng, parameter_sets, samples)
    blob = pack_fixture(parameter_sets, samples)
    return blob, summarize(parameter_sets, samples, notes, blob)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--out-dir", default=DEFAULT_OUT_DIR)
    args = parser.parse_args()
    blob, summary = build()
    summary_text = json.dumps(summary, ensure_ascii=False, indent=1) + "\n"
    fixture_path = os.path.join(args.out_dir, FIXTURE_NAME)
    summary_path = os.path.join(args.out_dir, SUMMARY_NAME)
    worst_self_check = max(note["self_check_max"] for note in summary["replay_scenes"])
    print(f"samples {summary['sample_count']} parameter_sets {len(summary['parameter_sets'])} bytes {len(blob)} sha256 {summary['sha256']}")
    print(f"replay self check max {worst_self_check:.3e}")
    if worst_self_check > SELF_CHECK_LIMIT:
        print("FAIL 표본 자리 계산이 drawn_density_v3 와 다름")
        return 1
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
