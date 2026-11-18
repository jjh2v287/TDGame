"""
실행 환경: Python 3.12 + numpy 2.2 + scipy 1.17 (추가 설치 없음), 저장소 루트에서 실행. 같은 폴더의 잠긴 fog3d_core·fog3d_core_v2·fog3d_replay·fog3d_terrain·fog3d_terrain_add1·fog3d_gates_s2a 를 import 만 한다(--candidate R5 일 때만 fog3d_terrain_add2)
실행 명령: PYTHONUTF8=1 python -B Tools/FluidFX/fog3d_ref/fog3d_replay_s2a.py [--scenes a,b] [--jobs N(최대 8)] [--candidate R4|R5] [--no-shake] [--check]
출력: 재생 파일 Tools/FluidFX/fog3d_ref/replays_s2a/<장면>.json(fog3d-replay-1 + ground)·<장면>.ground.bin, 기준 덤프 Saved/FluidFX/Fog3D/ref_s2a/<장면>__<덤프>.npz·.column.bin·.frames.bin, 흔든 덤프 ref_s2a/shake/, shake_diff.json·manifest.json. R5 는 각 폴더의 R5/ 아래. --check 는 다시 만들어 sha256 대조(다르면 종료 코드 1)
상태: 3D 볼륨 안개 2a 구현 계약 §8·⑪ 단계 2 의 GPU 대조용 기준 덤프 도구, 2026-11-15
"""
import os

for _thread_var in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS"):
    os.environ.setdefault(_thread_var, "1")

import argparse
import concurrent.futures
import hashlib
import io
import json
import math
import shutil
import sys
import tempfile
import time
import zipfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import fog3d_core as core  # noqa: E402
import fog3d_replay as rp  # noqa: E402
import fog3d_gates_s2a as gates_s2a  # noqa: E402
import fog3d_terrain_add1 as terrain_add1  # noqa: E402
from fog3d_terrain import TERRAINS  # noqa: E402

terrain_add1.register_terrains()

ROOT = core.ROOT
F32 = np.float32
HX = rp.HX
N = 128
DT = gates_s2a.SCENE["dt"]
SCENE = gates_s2a.SCENE
REPLAYS = os.path.join(HERE, "replays_s2a")
REF = os.path.join(ROOT, "Saved", "FluidFX", "Fog3D", "ref_s2a")
STAGE1_WALK = os.path.join(HERE, "replays_v2", "walk_px.json")
SHAKE_FOLDER = "shake"
COLUMN_FIELDS = ("ground_h", "slope_x", "slope_y", "wall")
FRAME_FIELDS = ("tilted_a_x", "tilted_a_y", "tilted_a_z", "reference_height", "tilted_b_x", "tilted_b_y", "tilted_b_z", "zero_0",
                "midpoint_x", "midpoint_y", "frame_slope_x", "frame_slope_y", "reference_x", "reference_y", "zero_1", "zero_2")
STATE_FIELDS = ("u", "v", "w", "dx", "dy", "dz", "g", "pressure", "divergence")
TUNING_S2A = dict(rp.TUNING, disp_relax_h_per_sec=0.05, disp_relax_v_per_sec=0.15)
LOCKED_CODE = ("fog3d_core.py", "fog3d_core_v2.py", "fog3d_replay.py", "fog3d_terrain.py", "fog3d_terrain_add1.py", "fog3d_gates_s2a.py",
               "tuned_solvers.json")
CANDIDATES = ("R4", "R5")
WALL_STEP_CM = 30.0
MAX_JOBS = 8
R5_CODE = LOCKED_CODE + ("fog3d_terrain_add2.py",)
ZIP_TIME = (1980, 1, 1, 0, 0, 0)
ACTIVE = dict(image=None, analytic=None, sim=None)


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    with open(path, "rb") as fh:
        return sha256_bytes(fh.read())


def code_fingerprint(names=LOCKED_CODE):
    return {name: sha256_file(os.path.join(HERE, name))[:16] for name in names}


# ---------------- 땅 그림 ----------------

class GridTerrain:
    signed_cliff_distance = None

    def __init__(self, ground, image, anchor):
        self.name = "grid:" + ground["terrain"]
        self.min_cell = (int(ground["min_cell"][0]), int(ground["min_cell"][1]))
        self.size = (int(ground["size"][0]), int(ground["size"][1]))
        self.anchor = (int(anchor[0]), int(anchor[1]))
        self.image = np.asarray(image, F32)
        if self.image.shape != (self.size[1], self.size[0]):
            raise ValueError(f"땅 그림 모양 {self.image.shape} != (size_y, size_x) {(self.size[1], self.size[0])}")

    def _cells(self, value, axis):
        position = np.asarray(value, float) / HX - 0.5 + self.anchor[axis]
        if not np.all(np.floor(position) == position):
            raise ValueError("GridTerrain 은 기둥 가운데에서만 높이를 돌려준다")
        index = position.astype(np.int64) - self.min_cell[axis]
        if index.min() < 0 or index.max() >= self.size[axis]:
            raise ValueError(f"땅 그림 밖의 기둥을 물음: 축 {axis}, 칸 {index.min() + self.min_cell[axis]}..{index.max() + self.min_cell[axis]}")
        return index

    def height(self, x, y):
        ix, iy = self._cells(x, 0), self._cells(y, 1)
        ix, iy = np.broadcast_arrays(ix, iy)
        return self.image[iy, ix].astype(np.float64)


def ground_rect(replay):
    n = int(replay["grid"]["n"])
    xs, ys = [], []
    for step in replay["steps"]:
        for key in ("window_min", "prev_window_min"):
            xs.append(int(step[key][0]))
            ys.append(int(step[key][1]))
    lo = (min(xs) - 1, min(ys) - 1)
    hi = (max(xs) + n, max(ys) + n)
    return lo, (hi[0] - lo[0] + 1, hi[1] - lo[1] + 1)


def ground_image(terrain, lo, size, anchor):
    x = (lo[0] + np.arange(size[0]) - anchor[0] + 0.5) * HX
    y = (lo[1] + np.arange(size[1]) - anchor[1] + 0.5) * HX
    heights = np.broadcast_to(np.asarray(terrain.height(x[None, :], y[:, None]), float), (size[1], size[0]))
    return np.ascontiguousarray(heights.astype(F32))


def image_bytes(image):
    return np.ascontiguousarray(image, "<f4").tobytes()


def attach_ground(replay, terrain_name, candidate, terrain=None):
    terrain = TERRAINS[terrain_name] if terrain is None else terrain
    lo, size = ground_rect(replay)
    image = ground_image(terrain, lo, size, replay["anchor_cell"])
    replay["ground"] = dict(terrain=terrain_name, candidate=candidate, min_cell=[int(lo[0]), int(lo[1])], size=[int(size[0]), int(size[1])],
                            reference_z_cm=0.0, file=f"{replay['name']}.ground.bin", sha256=sha256_bytes(image_bytes(image)))
    return image


def tie_stats(image):
    h = image.astype(np.float64)
    diffs = np.concatenate([np.abs(h[:, 1:] - h[:, :-1]).ravel(), np.abs(h[1:, :] - h[:-1, :]).ravel()])
    gap = np.abs(diffs - WALL_STEP_CM)
    near = gap[gap > 0]
    return dict(exact_ties=int((gap == 0).sum()), nearest_gap_cm=float(near.min()) if near.size else None,
                gaps_below_1e_4=int((near < 1e-4).sum()))


# ---------------- 모의 ----------------

def column_arrays(sim):
    return np.stack([sim.column_height, sim.slope_x, sim.slope_y, sim.wall_columns.astype(np.float64)]).astype(F32)


def frame_row(sim, a, b):
    mx, my, scx, scy = sim.tilt_frame
    ri = min(max(int(math.floor((mx - 0.5 * HX) / HX + 0.5)), 0), sim.nx - 1)
    rj = min(max(int(math.floor((my - 0.5 * HX) / HX + 0.5)), 0), sim.ny - 1)
    reference = (float(sim.column_height[ri, rj]), (ri + 0.5) * HX, (rj + 0.5) * HX)
    if hasattr(sim, "reference_frame") and tuple(float(v) for v in sim.reference_frame) != reference:
        raise RuntimeError(f"틀 기준 기둥 불일치: 모의 {sim.reference_frame} != 계약 4.6 식 {reference}")
    return [a[0], a[1], a[2], reference[0], b[0], b[1], b[2], 0.0, mx, my, scx, scy, reference[1], reference[2], 0.0, 0.0]


class CaptureMixin:
    capture_steps = frozenset()
    recorded_frames = None

    def step(self, inputs=(), follow=None):
        recording = self.steps + 1 in self.capture_steps
        self.recorded_frames = [] if recording else None
        super().step(inputs, follow)
        if recording:
            frames = np.asarray(self.recorded_frames, np.float64).reshape(-1, len(FRAME_FIELDS)).astype(F32)
            self.captures[self.steps] = dict(origin=tuple(self.origin), columns=column_arrays(self), frames=frames)
        self.recorded_frames = None

    def _input_frame(self, inp):
        a, b, slope = super()._input_frame(inp)
        if self.recorded_frames is not None:
            self.recorded_frames.append(frame_row(self, a, b))
        return a, b, slope


def sim_class(candidate):
    if candidate == "R5":
        import fog3d_terrain_add2 as terrain_add2
        return type("CaptureR5", (CaptureMixin, terrain_add2.TerrainSimR5), {})
    if candidate != "R4":
        raise ValueError(f"후보는 {CANDIDATES}")
    return type("CaptureR4", (CaptureMixin, terrain_add1.TerrainSimR4), {})


def dump_steps(replay):
    done, out = 0, {}
    for step in replay["steps"]:
        done += int(step.get("repeat", 1))
        if step.get("dump"):
            out[step["dump"]] = done
    return out


def make_sim_s2a(replay):
    first = replay["steps"][0]
    p = rp.build_params(replay, first["dt"])
    p.advect = "N2"
    obstacles = [dict(kind=o["kind"], center=o["center_cm"], half=o["half_cm"], yaw_deg=o["yaw_deg"]) for o in replay["obstacles"]]
    ground = replay["ground"]
    terrain = ACTIVE["analytic"] if ACTIVE["analytic"] is not None else GridTerrain(ground, ACTIVE["image"], replay["anchor_cell"])
    sim = sim_class(ground["candidate"])(p, terrain, anchor=tuple(replay["anchor_cell"]), window_min=tuple(first["prev_window_min"]),
                                          obstacles=obstacles)
    sim.captures = {}
    sim.capture_steps = frozenset(dump_steps(replay).values())
    ACTIVE["sim"] = sim
    return sim


rp.make_sim = make_sim_s2a


# ---------------- 덤프 쓰기 ----------------

def write_npz(path, meta, arrays):
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for name, value in [("meta", np.array(json.dumps(meta)))] + list(arrays.items()):
            buf = io.BytesIO()
            np.lib.format.write_array(buf, np.asanyarray(value), allow_pickle=False)
            info = zipfile.ZipInfo(name + ".npy", date_time=ZIP_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o600 << 16
            zf.writestr(info, buf.getvalue())


def finalize_dump(tmp, out_dir, replay, dump, capture):
    stem = f"{replay['name']}__{dump}"
    with np.load(os.path.join(tmp, stem + ".npz")) as z:
        meta = json.loads(str(z["meta"]))
        arrays = {k: np.array(z[k]) for k in z.files if k != "meta"}
    if tuple(meta["window_min"]) != capture["origin"]:
        raise RuntimeError(f"{stem}: 덤프 창 {meta['window_min']} != 기록 창 {capture['origin']}")
    ground = replay["ground"]
    meta.update(candidate=ground["candidate"], ground_sha256=ground["sha256"], column_fields=list(COLUMN_FIELDS), column_file=stem + ".column.bin",
                column_layout="float32 LE, [field][i=x][j=y], N*N per field", frames_file=stem + ".frames.bin",
                frames_count=int(capture["frames"].shape[0]), frame_fields=list(FRAME_FIELDS))
    os.makedirs(out_dir, exist_ok=True)
    write_npz(os.path.join(out_dir, stem + ".npz"), meta, arrays)
    with open(os.path.join(out_dir, stem + ".column.bin"), "wb") as fh:
        fh.write(np.ascontiguousarray(capture["columns"], "<f4").tobytes())
    with open(os.path.join(out_dir, stem + ".frames.bin"), "wb") as fh:
        fh.write(np.ascontiguousarray(capture["frames"], "<f4").tobytes())
    return [stem + ext for ext in (".npz", ".column.bin", ".frames.bin")]


def run_replay(replay, image, out_dir, shake=False, analytic=None):
    ACTIVE.update(image=image, analytic=analytic, sim=None)
    tmp = tempfile.mkdtemp(prefix="fog3d_s2a_")
    try:
        result = rp.execute(replay, tmp, shake=shake)
        sim = ACTIVE["sim"]
        written = []
        for dump, index in dump_steps(replay).items():
            written += finalize_dump(tmp, out_dir, replay, dump, sim.captures[index])
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
        ACTIVE.update(image=None, analytic=None)
    blend = dict(getattr(sim, "blend_counts", {}))
    return dict(steps=result["steps"], hash=result["hash"], files=written, walls=int(sim.wall_columns.sum()), blend_counts=blend)


# ---------------- 장면 ----------------

def new_replay(name, window=None, obstacles=(), tuning=TUNING_S2A):
    r = rp.Replay(name, n=N, window_min=window, obstacles=obstacles)
    r.body["tuning"] = dict(tuning)
    return r


def add_walk(r, walker, dumps):
    for k in range(1, walker.total + 1):
        r.step(DT, walker.next_inputs(), dump=dumps.get(k))


def add_blade(r, terrain, start, end, dump_blade, dump_end, steps=SCENE["blade_steps"]):
    for k in range(steps):
        r.step(DT, gates_s2a.blade_inputs(terrain, start, end, k, DT), dump=dump_blade if k == steps - 1 else None)
    tail = SCENE["g4_tail_steps"]
    for k in range(tail):
        r.step(DT, [], dump=dump_end if k == tail - 1 else None)


def walker_for(terrain, point, heading):
    heading = np.asarray(heading, float)
    return gates_s2a.Walker(terrain, np.asarray(point, float) - heading * SCENE["walk_back"], heading, False)


def walk_info(walker):
    return dict(start_cm=[float(c) for c in walker.start], heading=[float(c) for c in walker.heading], walk_steps=walker.walk_steps,
                stop_steps=walker.total - walker.walk_steps, speed_cm_s=SCENE["walk_speed"], strength=SCENE["body_wind"])


def scene_walk(name, terrain_name, point, heading, obstacles=(), window=None):
    terrain = TERRAINS[terrain_name]
    r = new_replay(name, window=window, obstacles=obstacles)
    walker = walker_for(terrain, point, heading)
    add_walk(r, walker, {35: "w35", walker.walk_steps: "w70", walker.total: "end"})
    r.body["walk"] = walk_info(walker)
    return r.finish(), terrain_name


def blade_steps_to_end(blade):
    length = float(np.linalg.norm(np.asarray(blade[1], float) - np.asarray(blade[0], float)))
    return int(math.ceil(length / (SCENE["blade_speed"] * DT) - 1e-9))


def scene_walk_blade(name, terrain_name, point, heading, blade, window=None, blade_steps=SCENE["blade_steps"]):
    terrain = TERRAINS[terrain_name]
    r = new_replay(name, window=window)
    walker = walker_for(terrain, point, heading)
    add_walk(r, walker, {walker.walk_steps: "w70"})
    add_blade(r, terrain, blade[0], blade[1], "blade", "end", blade_steps)
    r.body["walk"] = walk_info(walker)
    r.body["blade"] = dict(start_cm=list(blade[0]), end_cm=list(blade[1]), radius_cm=SCENE["blade_radius"], strength=SCENE["blade_wind"],
                           erase_per_sec=SCENE["blade_erase"], height_cm=SCENE["blade_height"], speed_cm_s=SCENE["blade_speed"],
                           steps=blade_steps, tail_steps=SCENE["g4_tail_steps"])
    return r.finish(), terrain_name


def scene_hill_run():
    terrain = TERRAINS["HILL"]
    y, h, speed = SCENE["g9_lines"]["HILL"], SCENE["g9_height"], SCENE["g9_speed"]
    anchor = (0, 0)
    r = new_replay("s2a_hill_run_window", window=(gates_s2a.snapped_origin(SCENE["g9_x0"], N), gates_s2a.snapped_origin(y, N)))
    total = gates_s2a.steps_for(DT, 2.0)
    x, moved = SCENE["g9_x0"], 0
    for s in range(total):
        x_new = x + speed * DT
        a = np.array((x, y, gates_s2a.ground(terrain, x, y) + h))
        b = np.array((x_new, y, gates_s2a.ground(terrain, x_new, y) + h))
        sphere = core.SweptSphere(tuple(a), tuple(b), SCENE["g9_radius"], tuple((b - a) / DT), SCENE["g9_strength"], SCENE["g9_erase"])
        window = rp.follow_window(r.window, anchor, (x_new, y), N)
        dump = None
        if window != r.window:
            moved += 1
            dump = f"mv{moved}"
        if s == total - 1:
            dump = "end"
        r.step(DT, [sphere], window=window, dump=dump)
        x = x_new
    r.body["run"] = dict(x0_cm=SCENE["g9_x0"], y_cm=y, speed_cm_s=speed, seconds=2.0, radius_cm=SCENE["g9_radius"], height_cm=h,
                         strength=SCENE["g9_strength"], erase_per_sec=SCENE["g9_erase"], follow="rp.follow_window margin 12 step 16")
    return r.finish(), "HILL"


def scene_still_rough():
    r = new_replay("s2a_still_rough")
    every, total = SCENE["g2_move_every"], 120
    moved = 0
    for s in range(total):
        window = r.window
        dump = None
        if s and s % every == 0:
            moved += 1
            window = (r.window[0] + SCENE["g2_move_cells"], r.window[1])
            dump = f"m{moved}"
        if s == total - 1:
            dump = "end"
        r.step(DT, [], window=window, dump=dump)
    return r.finish(), "ROUGH"


def scene_flat_offset():
    with open(STAGE1_WALK, encoding="utf-8") as fh:
        base = json.load(fh)
    lift = SCENE["g1_elevation"]
    r = new_replay("s2a_flat_offset", tuning=base["tuning"])
    for step in base["steps"]:
        spheres = []
        for sphere in step["spheres"]:
            sphere = dict(sphere)
            sphere["a_cm"] = [sphere["a_cm"][0], sphere["a_cm"][1], sphere["a_cm"][2] + lift]
            sphere["b_cm"] = [sphere["b_cm"][0], sphere["b_cm"][1], sphere["b_cm"][2] + lift]
            spheres.append(sphere)
        r.step(step["dt"], spheres, window=tuple(step["window_min"]), dump=step["dump"])
    r.body["walk"] = dict(base["walk"], source="replays_v2/walk_px.json", lift_cm=lift)
    return r.finish(), "F1"


def scene_box_on_slope():
    terrain = TERRAINS["S30"]
    cx, cy, half = 150.0, 75.0, (40.0, 30.0, 60.0)
    box = rp.box_dict((cx, cy, gates_s2a.ground(terrain, cx, cy) + half[2]), half)
    return scene_walk("s2a_box_on_slope", "S30", (0.0, 0.0), (1.0, 0.0), obstacles=[box])


def scene_frame_at_edge():
    terrain = TERRAINS["HILL"]
    window = (-48, -48)
    lo, hi = window[0] * HX, (window[0] + N) * HX
    inset = 4.0
    paths = (((lo + inset, -300.0), (0.0, 1.0)), ((0.0, lo + inset), (1.0, 0.0)), ((hi - inset, 300.0), (0.0, -1.0)))
    radius, height, strength, erase = 45.0, 60.0, SCENE["body_wind"], 30.0
    moving, still = SCENE["walk_steps"], 30
    r = new_replay("s2a_frame_at_edge", window=window)
    dumps = {35: "w35", moving: "w70", moving + still: "end"}
    for k in range(1, moving + still + 1):
        spheres = []
        if k <= moving:
            for start, heading in paths:
                pa = np.asarray(start) + np.asarray(heading) * SCENE["walk_speed"] * DT * (k - 1)
                pb = np.asarray(start) + np.asarray(heading) * SCENE["walk_speed"] * DT * k
                a = np.array((pa[0], pa[1], gates_s2a.ground(terrain, pa[0], pa[1]) + height))
                b = np.array((pb[0], pb[1], gates_s2a.ground(terrain, pb[0], pb[1]) + height))
                spheres.append(core.SweptSphere(tuple(a), tuple(b), radius, tuple((b - a) / DT), strength, erase))
        r.step(DT, spheres, dump=dumps.get(k))
    r.body["paths"] = [dict(start_cm=list(s), heading=list(h)) for s, h in paths]
    return r.finish(), "HILL"


SCENES = {
    "s2a_slope_walk_up": lambda: scene_walk("s2a_slope_walk_up", "S30", (0.0, 0.0), (1.0, 0.0)),
    "s2a_slope_walk_down": lambda: scene_walk("s2a_slope_walk_down", "S30", (0.0, 0.0), (-1.0, 0.0)),
    "s2a_slope_y_walk": lambda: scene_walk("s2a_slope_y_walk", "S30Y", (0.0, 0.0), (0.0, 1.0)),
    "s2a_cliff_walk_blade": lambda: scene_walk_blade("s2a_cliff_walk_blade", "CLIFF", SCENE["g4_walk_points"][("CLIFF", "a")],
                                                     SCENE["g4_headings"]["CLIFF"], SCENE["g4_blades"]["CLIFF"]),
    "s2a_curb30_cross": lambda: scene_walk("s2a_curb30_cross", "CURB30", (0.0, 0.0), (1.0, 0.0)),
    "s2a_hill_run_window": scene_hill_run,
    "s2a_still_rough": scene_still_rough,
    "s2a_slope_curb": lambda: scene_walk_blade("s2a_slope_curb", terrain_add1.SLOPE_CURB_TERRAIN, (-20.0, 0.0), SCENE["g4_headings"]["CLIFF"],
                                               SCENE["g4_blades"]["CLIFF"]),
    "s2a_flat_offset": scene_flat_offset,
    "s2a_box_on_slope": scene_box_on_slope,
    "s2a_cliff_at_edge": lambda: scene_walk_blade("s2a_cliff_at_edge", "CLIFF", (60.0, 0.0), (0.0, 1.0), ((200.0, 0.0), (40.0, 0.0)),
                                                  window=(0, -64)),
    "s2a_frame_at_edge": scene_frame_at_edge,
}


def scene_slope_curb60():
    import fog3d_terrain_add2 as terrain_add2
    terrain_add2.register_terrains()
    blade = ((-120.0, 0.0), (60.0, 0.0))
    return scene_walk_blade("s2a_slope_curb60", "S30CURB60", (-20.0, 0.0), SCENE["g4_headings"]["CLIFF"], blade,
                            blade_steps=blade_steps_to_end(blade))


R5_ONLY_SCENES = {"s2a_slope_curb60": scene_slope_curb60}
ALL_SCENES = dict(SCENES, **R5_ONLY_SCENES)


def scenes_for(candidate):
    return list(SCENES) + (list(R5_ONLY_SCENES) if candidate == "R5" else [])


def build_scene(name, candidate):
    replay, terrain_name = ALL_SCENES[name]()
    replay["source"] = dict(tool="fog3d_replay_s2a.py", code=code_fingerprint(R5_CODE if candidate == "R5" else LOCKED_CODE),
                            note="입력 생성 도구 자체의 지문은 ref_s2a/manifest.json 의 tool_sha256")
    image = attach_ground(replay, terrain_name, candidate)
    problems = rp.check_replay(replay) + check_ground(replay, image)
    if problems:
        raise ValueError(f"{name} 형식 문제: {problems}")
    return replay, image


def check_ground(replay, image):
    g = replay["ground"]
    problems = []
    if g["candidate"] not in CANDIDATES:
        problems.append("ground.candidate")
    if list(image.shape) != [g["size"][1], g["size"][0]]:
        problems.append("ground 크기")
    if sha256_bytes(image_bytes(image)) != g["sha256"]:
        problems.append("ground sha256")
    if not np.isfinite(image).all():
        problems.append("ground 유한")
    n = int(replay["grid"]["n"])
    for s in replay["steps"]:
        for key in ("window_min", "prev_window_min"):
            wx, wy = s[key]
            if wx - 1 < g["min_cell"][0] or wy - 1 < g["min_cell"][1] or wx + n > g["min_cell"][0] + g["size"][0] - 1 \
                    or wy + n > g["min_cell"][1] + g["size"][1] - 1:
                problems.append(f"창+고리가 땅 그림 밖: {key} {s[key]}")
                return problems
    return problems


def scene_summary(replay, image):
    windows = [tuple(s["window_min"]) for s in replay["steps"]]
    return dict(terrain=replay["ground"]["terrain"], steps=sum(int(s.get("repeat", 1)) for s in replay["steps"]), dumps=dump_steps(replay),
                spheres_max=max(len(s["spheres"]) for s in replay["steps"]), window_first=list(windows[0]), window_last=list(windows[-1]),
                window_moves=sum(1 for a, b in zip(windows, windows[1:]) if a != b), obstacles=len(replay["obstacles"]),
                ground_min_cell=replay["ground"]["min_cell"], ground_size=replay["ground"]["size"], ground_sha256=replay["ground"]["sha256"],
                ground_min_cm=float(image.min()), ground_max_cm=float(image.max()), ties=tie_stats(image))


def folders(candidate, replays_root=REPLAYS, ref_root=REF):
    if candidate == "R4":
        return replays_root, ref_root
    return os.path.join(replays_root, candidate), os.path.join(ref_root, candidate)


def write_replay(folder, replay, image):
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, f"{replay['name']}.json"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write(rp.dumps_text(replay))
    with open(os.path.join(folder, replay["ground"]["file"]), "wb") as fh:
        fh.write(image_bytes(image))


def run_job(job):
    kind, name, candidate, replays_dir, ref_dir = job
    started = time.time()
    replay, image = build_scene(name, candidate)
    if kind == "ref":
        result = run_replay(replay, image, ref_dir)
    else:
        result = run_replay(replay, image, os.path.join(ref_dir, SHAKE_FOLDER), shake=True)
    return dict(kind=kind, name=name, summary=scene_summary(replay, image), steps=result["steps"], hash=result["hash"], walls_last=result["walls"],
                blend_counts=result["blend_counts"], files=result["files"], seconds=round(time.time() - started, 1))


def scene_cost(name):
    replay, _ = ALL_SCENES[name]()
    return sum(int(s.get("repeat", 1)) for s in replay["steps"])


def run_pool(jobs, workers):
    costs = {name: scene_cost(name) for name in {j[1] for j in jobs}}
    jobs = sorted(jobs, key=lambda j: -costs[j[1]])
    infos = []
    with concurrent.futures.ProcessPoolExecutor(max_workers=max(1, min(workers, MAX_JOBS))) as pool:
        for info in pool.map(run_job, jobs):
            print(json.dumps({k: v for k, v in info.items() if k != "summary"}, ensure_ascii=False))
            sys.stdout.flush()
            infos.append(info)
    return infos


# ---------------- 흔든 덤프와의 차 ----------------

def load_npz(path):
    with np.load(path) as z:
        return json.loads(str(z["meta"])), {k: np.array(z[k]) for k in z.files if k != "meta"}


def load_columns(path, n):
    return np.fromfile(path, dtype="<f4").reshape(len(COLUMN_FIELDS), n, n)


def shake_table(ref_dir, names, replays_dir):
    out = {}
    for name in names:
        with open(os.path.join(replays_dir, f"{name}.json"), encoding="utf-8") as fh:
            replay = json.load(fh)
        rows = {}
        for dump in dump_steps(replay):
            stem = f"{name}__{dump}"
            meta, ref = load_npz(os.path.join(ref_dir, stem + ".npz"))
            _, shaken = load_npz(os.path.join(ref_dir, SHAKE_FOLDER, stem + ".npz"))
            fluid = (ref["fluid"] > 0.5) & (shaken["fluid"] > 0.5)
            row = {f: float(np.abs(ref[f].astype(np.float64) - shaken[f])[fluid].max(initial=0.0)) for f in STATE_FIELDS}
            row["fluid_mismatch"] = int(((ref["fluid"] > 0.5) != (shaken["fluid"] > 0.5)).sum())
            n = int(meta["n"])
            ca = load_columns(os.path.join(ref_dir, stem + ".column.bin"), n)
            cb = load_columns(os.path.join(ref_dir, SHAKE_FOLDER, stem + ".column.bin"), n)
            row["columns_max"] = float(np.abs(ca.astype(np.float64) - cb).max())
            rows[dump] = row
        out[name] = dict(dumps=rows, max={f: max(r[f] for r in rows.values()) for f in STATE_FIELDS + ("fluid_mismatch", "columns_max")})
    return out


def stored_shake_scenes(ref_dir):
    folder = os.path.join(ref_dir, SHAKE_FOLDER)
    if not os.path.isdir(folder):
        return set()
    return {f.split("__")[0] for f in os.listdir(folder) if f.endswith(".npz") and f.split("__")[0] in ALL_SCENES}


def write_manifest(folder, extra):
    hashes = tree_hashes(folder)
    rows = [dict(name=k, bytes=os.path.getsize(os.path.join(folder, k)), sha256=hashes[k]) for k in sorted(hashes)]
    old_path = os.path.join(folder, "manifest.json")
    if os.path.isfile(old_path) and "scenes" in extra:
        with open(old_path, encoding="utf-8") as fh:
            old = json.load(fh)
        if old.get("candidate") == extra.get("candidate"):
            extra = dict(extra, scenes=dict(sorted(dict(old.get("scenes", {}), **extra["scenes"]).items())))
    listing = "".join(f"{r['name']} {r['bytes']} {r['sha256']}\n" for r in rows)
    body = dict(format="fog3d-ref-manifest-1", folder=os.path.relpath(folder, ROOT).replace("\\", "/"), advect="N2", solid="S9",
                code=code_fingerprint(), tool_sha256=sha256_file(os.path.abspath(__file__)), files=rows,
                listing_sha256=sha256_bytes(listing.encode()), listing_rule="각 줄 '이름 바이트수 sha256' 을 이름순으로 \\n 으로 잇고 끝에 \\n, 그 sha256",
                **extra)
    with open(os.path.join(folder, "manifest.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump(body, fh, ensure_ascii=False, indent=1)
        fh.write("\n")
    return body["listing_sha256"]


def produce(names, candidate, jobs, shake, replays_dir, ref_dir):
    for name in names:
        replay, image = build_scene(name, candidate)
        write_replay(replays_dir, replay, image)
    write_manifest(replays_dir, dict(candidate=candidate))
    kinds = ("ref", "shake") if shake else ("ref",)
    infos = run_pool([(k, n, candidate, replays_dir, ref_dir) for n in names for k in kinds], jobs)
    summaries = {i["name"]: dict(i["summary"], state_hash=i["hash"], walls_last=i["walls_last"], **({"blend_counts": i["blend_counts"]} if i["blend_counts"] else {}))
                 for i in infos if i["kind"] == "ref"}
    extra = dict(candidate=candidate, scenes={n: summaries[n] for n in sorted(summaries)})
    if shake:
        table = shake_table(ref_dir, sorted(set(names) | stored_shake_scenes(ref_dir)), replays_dir)
        with open(os.path.join(ref_dir, "shake_diff.json"), "w", encoding="utf-8", newline="\n") as fh:
            json.dump(dict(shake_cm=rp.SENSITIVITY_SHAKE_CM, seed=rp.SENSITIVITY_SEED, rule="기준 대 흔든 것, 두 쪽 모두 유체인 칸의 최대 |차|", scenes=table),
                      fh, ensure_ascii=False, indent=1)
            fh.write("\n")
        print_shake(table)
    write_manifest(ref_dir, extra)
    return infos


def print_shake(table):
    head = ("scene",) + STATE_FIELDS + ("fluid_mismatch", "columns_max")
    print(" | ".join(head))
    for name, entry in table.items():
        print(" | ".join([name] + [f"{entry['max'][f]:.3g}" for f in head[1:]]))


def tree_hashes(folder):
    out = {}
    for base, dirs, files in os.walk(folder):
        dirs[:] = sorted(d for d in dirs if d not in CANDIDATES)
        for f in files:
            if f == "manifest.json":
                continue
            path = os.path.join(base, f)
            out[os.path.relpath(path, folder).replace("\\", "/")] = sha256_file(path)
    return out


def check(names, candidate, jobs, shake, replays_dir, ref_dir):
    tmp = tempfile.mkdtemp(prefix="fog3d_s2a_check_")
    try:
        t_replays, t_ref = os.path.join(tmp, "replays"), os.path.join(tmp, "ref")
        produce(names, candidate, jobs, shake, t_replays, t_ref)
        bad = 0
        for stored, fresh in ((replays_dir, t_replays), (ref_dir, t_ref)):
            a, b = tree_hashes(stored), tree_hashes(fresh)
            wanted = {k for k in b if k.split("/")[-1].split("__")[0].split(".")[0] in names}
            if set(names) == set(scenes_for(candidate)) and "shake_diff.json" in b:
                wanted.add("shake_diff.json")
            for key in sorted(wanted):
                if a.get(key) != b[key]:
                    bad += 1
                    print("다름:", os.path.join(stored, key))
            print(f"{stored}: 대조 {len(wanted)}개, 다른 것 {sum(1 for k in wanted if a.get(k) != b[k])}개")
        return 1 if bad else 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser()
    ap.add_argument("--scenes", default="all")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--candidate", default="R4", choices=CANDIDATES)
    ap.add_argument("--no-shake", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args(argv)
    if a.list:
        print("\n".join(scenes_for(a.candidate)))
        return 0
    names = scenes_for(a.candidate) if a.scenes == "all" else a.scenes.split(",")
    unknown = [n for n in names if n not in scenes_for(a.candidate)]
    if unknown:
        print("모르는 장면:", unknown)
        return 2
    replays_dir, ref_dir = folders(a.candidate)
    if a.check:
        return check(names, a.candidate, a.jobs, not a.no_shake, replays_dir, ref_dir)
    produce(names, a.candidate, a.jobs, not a.no_shake, replays_dir, ref_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
