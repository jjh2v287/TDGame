"""
실행 환경: Python 3.12 + numpy 2.2 + scipy 1.17 + matplotlib 3.10 (추가 설치 없음), 저장소 루트에서 실행. 같은 폴더의 잠긴 fog3d_core.py·fog3d_core_v2.py·fog3d_gates.py·fog3d_gates_v2.py 와 fog3d_terrain.py 를 import 만 한다
실행 명령: python Tools/FluidFX/fog3d_ref/fog3d_gates_s2a.py run --out-dir <폴더> [--only G4,G7,INFO] [--candidates R0,R1,R2,R3] [--jobs N] [--smoke] [--force] / collect --out-dir <폴더> [--candidates ...]
출력: <폴더>/pieces/<후보>_<판정선>_<장면>.json(+npz), collect 는 <폴더>/results.json·results.md·section_rest.png·section_cliff.png
상태: 2a 땅 높이 따라가기 판정 실행기(Docs/VolumeFog3D/stage2/prereg-2a-design.md 2절·4절·5절), 2026-11-15, 등록 잠금 전 시운전판
"""
import os

for _thread_var in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS"):
    os.environ.setdefault(_thread_var, "1")

import argparse
import concurrent.futures
import contextlib
import dataclasses
import hashlib
import json
import math
import sys
import time
import traceback

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import fog3d_core as core  # noqa: E402
import fog3d_core_v2 as core2  # noqa: E402
import fog3d_gates as gates  # noqa: E402
import fog3d_gates_v2 as gates_v2  # noqa: E402
import fog3d_terrain as terrain_model  # noqa: E402
from fog3d_terrain import TERRAINS, TerrainSim  # noqa: E402

ROOT = core.ROOT
PREREG_PATH = os.path.join(ROOT, "Docs", "VolumeFog3D", "stage2", "prereg-2a-design.md")
LOCKED_FINGERPRINTS = {
    "fog3d_core.py": "fa5564705c405769", "fog3d_core_v2.py": "3d034f9cafb7b5de", "fog3d_replay.py": "6edd3fe644f3aa9e",
    "fog3d_replay_add1.py": "aaf10ad8bdde86d7", "fog3d_replay_v2.py": "a7de750f3fe9efa8", "fog3d_compare.py": "69c021d077a315de",
    "fog3d_compare_add1.py": "9c68f1f2e4150273", "tuned_solvers.json": "6ab5094bd599daf9", "fog3d_gates.py": "8dbcc05522fae9b6",
    "fog3d_gates_v2.py": "13fced431d7083e1", "fog3d_selftest.py": "9fcd9b114e8c0cd4", "fog3d_compare_v2.py": "47afdb7b9ab31245",
    "fog3d_density_fixture.py": "0b162febe13497ce",
}
NEW_CODE = ("fog3d_terrain.py", "fog3d_gates_s2a.py")

HX = 15.625
HALF = math.sqrt(0.5)
THRESHOLDS = dict(
    G1_F0=1e-5, G1_F1=1e-4, G2_EXACT=0.0, G3_E_CM=3.0, G3_CLIFF_MARGIN_CM=23.5, G4_LEAK=1e-6, G4_FAR_CM=31.25, G5_RATIO_MIN=0.5,
    G6_WIND_FACTOR=1.5, G6_ENERGY_RATIO=0.01, G7_A_MAX=0.15, G8_LO=0.95, G8_HI=1.05, G9_RATIO_1=0.05, G9_RATIO_2=0.05,
    G9_RATIO_3=0.01, G10_TARGET_CM_S=0.5,
)
SCENE = dict(
    dt=1.0 / 60.0, walk_speed=520.0, walk_steps=70, stop_steps=78, walk_back=300.0,
    rig=((0.0, -12.0, 30.0, 45.0), (0.0, 12.0, 30.0, 45.0), (0.0, 0.0, 91.0, 60.0)), body_wind=3.0, body_erase=0.0,
    band_trim_cm=50.0, band_cm=100.0, g5_from_cm=300.0,
    g1_elevation=137.5, g1_scenes=("walk_px", "v2_low", "v2_high", "v3_free", "v3_low", "v3_high"),
    g2_steps=300, g2_move_every=30, g2_move_cells=16, g2_terrains=("S30", "HILL", "CLIFF", "ROUGH"),
    g3_edge_cells=8, g3_sub=4,
    g4_walk_points={("CLIFF", "a"): (-60.0, 0.0), ("CLIFF", "b"): (60.0, 0.0), ("DIAG", "a"): (-60.0 * HALF, -60.0 * HALF),
                    ("DIAG", "b"): (60.0 * HALF, 60.0 * HALF), ("MESA", "a"): (0.0, -460.0), ("MESA", "b"): (0.0, 0.0)},
    g4_headings={"CLIFF": (0.0, 1.0), "DIAG": (HALF, -HALF), "MESA": (1.0, 0.0)},
    g4_blades={"CLIFF": ((-200.0, 0.0), (-40.0, 0.0)), "DIAG": ((-200.0 * HALF, -200.0 * HALF), (-40.0 * HALF, -40.0 * HALF)),
               "MESA": ((0.0, -600.0), (0.0, -440.0))},
    blade_radius=35.0, blade_wind=2.5, blade_erase=30.0, blade_height=100.0, blade_speed=800.0, blade_steps=12, g4_tail_steps=30,
    info8_cliff_walk_point=(-20.0, 0.0),
    g6_slopes=("S30", "S45", "S60"), g6_height=75.0, g6_speed=2000.0, g6_capsule_half=50.0, g6_capsule_radius=20.0,
    g6_capsule_from=-250.0, g6_capsule_to=250.0, g6_sphere_radius=45.0, g6_sphere_wind=2.5, g6_input_seconds=0.25,
    g6_after_seconds=4.0, g6_s60_extra_hz=(30, 144),
    g8_slopes=("S10", "S20", "S30", "S45", "S60"), g8_radii=(45.0, 60.0), g8_height=75.0,
    g9_lines={"HILL": 0.0, "MESA": -460.0}, g9_x0=-1500.0, g9_speed=600.0, g9_seconds=5.0, g9_radius=40.0, g9_height=60.0,
    g9_strength=1.0, g9_erase=30.0, g9_fixed_nx=512,
    g10_terrain="S30", g10_radius=60.0, g10_height=91.0,
    column_heights=tuple(np.arange(1.5, 240.0, 3.0)), column_dz=3.0, source_spacings=(31.25, 62.5),
)
CANDIDATE_ORDER = ("R0", "R1", "R2", "R3")
GATE_KIND = {"G1": "required", "G2": "required", "G3": "deciding", "G4": "deciding", "G5": "deciding", "G6": "required",
             "G7": "deciding", "G8": "required", "G9": "required", "G10": "deciding"}
ALL_GATES = ("G1", "G2", "G3", "G4", "G5", "G6", "G7", "G8", "G9", "G10")
CANDIDATE_GATES = {"R0": ("G3", "G2", "G4", "G5", "G7"), "R1": ("G4",) + tuple(g for g in ALL_GATES if g != "G4"),
                   "R2": ("G10",) + tuple(g for g in ALL_GATES if g != "G10"), "R3": ALL_GATES}
PRIORITY_GATE = {"R0": "G3", "R1": "G4", "R2": "G10"}
SMOOTH_TERRAINS = ("S10", "S20", "S30", "S45", "HILL")
CLIFF_TERRAINS = ("CLIFF", "DIAG", "MESA")
REFERENCE_CANDIDATE = "REF"
VARIANT_CANDIDATE = "R3G"
SMOKE_DIVISOR = 15
STATE_NAMES = ("u", "v", "w", "dpx", "dpy", "dpz", "g")


def fingerprint(path):
    with open(path, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()[:16]


def locked_check():
    found = {name: fingerprint(os.path.join(HERE, name)) for name in LOCKED_FINGERPRINTS}
    mismatched = {k: v for k, v in found.items() if v != LOCKED_FINGERPRINTS[k]}
    return dict(ok=not mismatched, mismatched=mismatched, found=found)


def code_fingerprints():
    out = {name: fingerprint(os.path.join(HERE, name)) for name in NEW_CODE}
    out["prereg-2a-design.md"] = fingerprint(PREREG_PATH)
    return out


def count(n, smoke):
    return max(2, int(math.ceil(n / SMOKE_DIVISOR))) if smoke else n


def steps_for(dt, seconds):
    return max(1, int(math.ceil(seconds / dt - 1e-9)))


def game_params(dt=SCENE["dt"], nx=128, ny=128):
    return core.Params(nx=nx, ny=ny, dt=dt, advect="N2", wind_relax_per_sec=0.75, disp_relax_h_per_sec=0.05,
                       disp_relax_v_per_sec=0.15, fill_relax_per_sec=0.3)


def candidate_settings(cand):
    return "R3" if cand == REFERENCE_CANDIDATE else cand


def make_sim(cand, terrain, params, **kw):
    return TerrainSim(params, terrain, candidate_settings(cand), **kw)


def ground(terrain, x, y):
    return float(terrain.height(np.asarray(float(x)), np.asarray(float(y))))


def state_abs_max(sim, mask=None, with_diff=False):
    arrays = tuple(getattr(sim, k) for k in STATE_NAMES) + ((sim.D,) if with_diff else ())
    if mask is None:
        return max(float(np.abs(a).max()) for a in arrays)
    return max(float(np.abs(a[mask]).max(initial=0.0)) for a in arrays)


def column(sim):
    return sim.drawn_density_v3(np.asarray(SCENE["column_heights"])).sum(axis=2) * SCENE["column_dz"]


def finite(sim):
    return all(np.isfinite(getattr(sim, k)).all() for k in STATE_NAMES + ("D",))


def raised(inputs, elevation):
    return [core.SweptSphere((i.a[0], i.a[1], i.a[2] + elevation), (i.b[0], i.b[1], i.b[2] + elevation), i.radius, i.velocity,
                             i.strength, i.erase_per_sec) for i in inputs]


class Walker:
    def __init__(self, terrain, start, heading, smoke, rig=None, strength=None, dt=SCENE["dt"], stops=True):
        self.terrain = terrain
        self.heading = np.asarray(heading, float)
        self.pos = np.asarray(start, float).copy()
        self.start = self.pos.copy()
        self.dt = dt
        self.rig = SCENE["rig"] if rig is None else rig
        self.strength = SCENE["body_wind"] if strength is None else strength
        self.walk_steps = count(SCENE["walk_steps"], smoke)
        self.total = self.walk_steps + (count(SCENE["stop_steps"], smoke) if stops else 0)
        self.k = 0
        self.span = self.walk_steps * SCENE["walk_speed"] * dt

    def sphere_center(self, pos, forward, lateral, height):
        c, s = self.heading
        x = pos[0] + forward * c - lateral * s
        y = pos[1] + forward * s + lateral * c
        return np.array((x, y, ground(self.terrain, x, y) + height))

    def next_inputs(self):
        prev = self.pos.copy()
        if self.k < self.walk_steps:
            self.pos = self.pos + self.heading * SCENE["walk_speed"] * self.dt
        self.k += 1
        out = []
        for forward, lateral, height, radius in self.rig:
            a = self.sphere_center(prev, forward, lateral, height)
            b = self.sphere_center(self.pos, forward, lateral, height)
            out.append(core.SweptSphere(tuple(a), tuple(b), radius, tuple((b - a) / self.dt), self.strength, SCENE["body_erase"]))
        return out


def walk_frame(sim, start, heading):
    rx, ry, _ = sim.center_rel()
    X, Y = np.meshgrid(rx, ry, indexing="ij")
    c, s = heading
    relx, rely = X - start[0], Y - start[1]
    return relx * c + rely * s, -relx * s + rely * c


def band_stats(sim, delta, walker):
    along, lateral = walk_frame(sim, walker.start, walker.heading)
    trim, band = SCENE["band_trim_cm"], SCENE["band_cm"]
    side_left = (lateral < 0) & (lateral > -band)
    side_right = (lateral > 0) & (lateral < band)
    on = (along > trim) & (along < walker.span - trim)
    left, right = on & side_left, on & side_right
    cross = (along > SCENE["g5_from_cm"]) & (along < walker.span - trim) & (side_left | side_right)

    def mean_abs(mask):
        return float(np.abs(delta[mask]).mean()) if mask.any() else float("nan")
    a_left, a_right = mean_abs(left), mean_abs(right)
    return dict(S=mean_abs(left | right), abs_left=a_left, abs_right=a_right, signed_sum=float(delta[left | right].sum()),
                side_bias=abs(a_left - a_right) / (a_left + a_right) if a_left + a_right > 0 else float("nan"),
                crossing_abs_mean=mean_abs(cross), crossing_cells=int(cross.sum()), band_cells=int((left | right).sum()),
                span_cm=walker.span)


WALK_PATHS = {"up": ((-300.0, 0.0), (1.0, 0.0)), "down": ((300.0, 0.0), (-1.0, 0.0)), "contour": ((0.0, -300.0), (0.0, 1.0))}


def piece_walk(spec):
    terrain = TERRAINS[spec["terrain"]]
    sim = make_sim(spec["cand"], terrain, game_params())
    start, heading = WALK_PATHS[spec["path"]]
    walker = Walker(terrain, start, heading, spec["smoke"])
    col0 = column(sim)
    for _ in range(walker.total):
        sim.step(walker.next_inputs())
    delta = column(sim) - col0
    out = band_stats(sim, delta, walker)
    out.update(steps=walker.total, max_wind=float(sim.max_wind()), state_hash=sim.state_hash(), finite=finite(sim))
    if spec.get("npz"):
        np.savez_compressed(spec["npz"], **{k: getattr(sim, k) for k in STATE_NAMES})
    return out


class WalkPx(gates.Scene):
    def __init__(self, smoke):
        super().__init__(gates.Sim(core.Params(advect="N2", solid="S9")))
        self.walk_steps = count(gates_v2.WALK_STEPS, smoke)
        self.total = self.walk_steps + count(gates_v2.STOP_STEPS, smoke)
        self.pos = np.array((-300.0, 0.0))

    def inputs(self):
        prev = self.pos.copy()
        if self.s < self.walk_steps:
            self.pos = self.pos + np.array((1.0, 0.0)) * gates_v2.SPEED * gates_v2.DT
            vel = (gates_v2.SPEED, 0.0, 0.0)
        else:
            vel = (0.0, 0.0, 0.0)
        return gates_v2.walk_inputs(self.pos, prev, 1.0, 0.0, vel)


def g1_locked_params():
    return gates.make_params(dataclasses.replace(gates.Cfg(), advect="N2"))


def g1_scene(name, smoke):
    if name == "walk_px":
        return WalkPx(smoke)
    p = g1_locked_params()
    scene = {"v2_low": lambda: gates.Swing(p, 45.0), "v2_high": lambda: gates.Swing(p, 135.0),
             "v3_free": lambda: gates.BoxRun(p, None), "v3_low": lambda: gates.BoxRun(p, 30.0),
             "v3_high": lambda: gates.BoxRun(p, 180.0)}[name]()
    scene.total = count(scene.total, smoke)
    return scene


@contextlib.contextmanager
def swapped_sim(factory):
    previous = gates.Sim
    gates.Sim = factory
    try:
        yield
    finally:
        gates.Sim = previous


def terrain_factory(cand, terrain, elevation):
    def factory(params, anchor=(0, 0), window_min=None, boxes=(), obstacles=()):
        lifted = [((lo[0], lo[1], lo[2] + elevation), (hi[0], hi[1], hi[2] + elevation)) for lo, hi in boxes]
        return make_sim(cand, terrain, params, anchor=anchor, window_min=window_min, boxes=lifted, obstacles=obstacles)
    return factory


def run_scene(scene):
    while not scene.finished():
        scene.advance()
    return scene.sim


def piece_g1(spec):
    level = spec["level"]
    elevation = SCENE["g1_elevation"] if level == "F1" else 0.0
    with swapped_sim(core2.Sim):
        locked = run_scene(g1_scene(spec["scene"], spec["smoke"]))
    with swapped_sim(terrain_factory(spec["cand"], TERRAINS[level], elevation)):
        scene = g1_scene(spec["scene"], spec["smoke"])
        original = scene.inputs
        scene.inputs = lambda: raised(original(), elevation)
        sim = run_scene(scene)
    by_field = {k: float(np.abs(getattr(sim, k).astype(np.float64) - getattr(locked, k)).max()) for k in STATE_NAMES}
    bits = all(np.array_equal(getattr(sim, k), getattr(locked, k)) for k in STATE_NAMES)
    return dict(scene=spec["scene"], level=level, steps=sim.steps, max_abs_diff=max(by_field.values()), by_field=by_field,
                bit_identical=bool(bits), solid_same=bool(np.array_equal(sim.solid, locked.solid)),
                locked_max_wind=float(locked.max_wind()), locked_hash=locked.state_hash())


def piece_g2(spec):
    terrain = TERRAINS[spec["terrain"]]
    sim = make_sim(spec["cand"], terrain, game_params())
    every = count(SCENE["g2_move_every"], spec["smoke"])
    total = every * (SCENE["g2_steps"] // SCENE["g2_move_every"])
    worst = 0.0
    moves = 0
    for k in range(total):
        sim.step([])
        worst = max(worst, state_abs_max(sim))
        if (k + 1) % every == 0:
            sim.set_window((sim.origin[0] + SCENE["g2_move_cells"], sim.origin[1]))
            moves += 1
            worst = max(worst, state_abs_max(sim))
    return dict(steps=total, moves=moves, state_abs_max=worst, walls=int(sim.wall_columns.sum()))


def g3_errors(sim, truth):
    rx, ry, _ = sim.center_rel()
    edge, sub = SCENE["g3_edge_cells"], SCENE["g3_sub"]
    offsets = ((np.arange(sub) + 0.5) / sub - 0.5) * HX
    xs = (rx[edge:sim.nx - edge][:, None] + offsets[None, :]).ravel()
    ys = (ry[edge:sim.ny - edge][:, None] + offsets[None, :]).ravel()
    X, Y = np.meshgrid(xs, ys, indexing="ij")
    err = np.abs(sim.drawn_ground(X, Y) - truth.height(X, Y))
    if truth.signed_cliff_distance is not None:
        err = err[np.abs(truth.signed_cliff_distance(X, Y)) > THRESHOLDS["G3_CLIFF_MARGIN_CM"]]
    return err


def piece_g3(spec):
    truth = TERRAINS[spec["terrain"]]
    source = truth if not spec.get("spacing") else terrain_model.sampled_terrain(truth, spec["spacing"])
    sim = make_sim(spec["cand"], source, game_params())
    err = g3_errors(sim, truth)
    return dict(max_e=float(err.max()), mean_e=float(err.mean()), samples=int(err.size), spacing=spec.get("spacing"))


def opposite_mask(sim, terrain, side):
    rx, ry, _ = sim.center_rel()
    dist = terrain.signed_cliff_distance(rx[:, None], ry[None, :])
    far = THRESHOLDS["G4_FAR_CM"]
    cols = dist > far if side == "a" else dist < -far
    return cols[:, :, None] & sim.fluid


def blade_inputs(terrain, start, end, k, dt):
    start, end = np.asarray(start, float), np.asarray(end, float)
    direction = (end - start) / np.linalg.norm(end - start)
    total = float(np.linalg.norm(end - start))
    step = SCENE["blade_speed"] * dt

    def point(s):
        x, y = start + direction * min(s, total)
        return np.array((x, y, ground(terrain, x, y) + SCENE["blade_height"]))
    a, b = point(step * k), point(step * (k + 1))
    return [core.SweptSphere(tuple(a), tuple(b), SCENE["blade_radius"], tuple((b - a) / dt), SCENE["blade_wind"], SCENE["blade_erase"])]


def piece_g4(spec):
    name, side = spec["terrain"], spec["side"]
    terrain = TERRAINS[name]
    sim = make_sim(spec["cand"], terrain, game_params())
    heading = np.asarray(SCENE["g4_headings"][name])
    point = np.asarray(spec.get("walk_point") or SCENE["g4_walk_points"][(name, side)], float)
    walker = Walker(terrain, point - heading * SCENE["walk_back"], heading, spec["smoke"])
    mask = opposite_mask(sim, terrain, side)
    dt = sim.p.dt
    worst = 0.0
    steps = 0
    for _ in range(walker.total):
        sim.step(walker.next_inputs())
        worst = max(worst, state_abs_max(sim, mask, with_diff=True))
        steps += 1
    if side == "a":
        start, end = SCENE["g4_blades"][name]
        for k in range(SCENE["blade_steps"]):
            sim.step(blade_inputs(terrain, start, end, k, dt))
            worst = max(worst, state_abs_max(sim, mask, with_diff=True))
            steps += 1
        for _ in range(count(SCENE["g4_tail_steps"], spec["smoke"])):
            sim.step([])
            worst = max(worst, state_abs_max(sim, mask, with_diff=True))
            steps += 1
    out = dict(opposite_max=worst, opposite_cells=int(mask.sum()), steps=steps, walls=int(sim.wall_columns.sum()),
               max_wind=float(sim.max_wind()), walk_point=point.tolist(), finite=finite(sim))
    if spec.get("npz"):
        rx, ry, _ = sim.center_rel()
        row = int(np.argmin(np.abs(ry - 0.0)))
        heights = np.asarray(SCENE["column_heights"])
        np.savez_compressed(spec["npz"], rx=rx, row_y=ry[row], heights=heights, drawn=sim.drawn_density_v3(heights)[:, row],
                            ground=sim.column_height[:, row], wall=sim.wall_columns[:, row])
    return out


def g6_velocity(kind):
    speed = SCENE["g6_speed"]
    return {"k1": (0.0, speed, 0.0), "k2": (speed, 0.0, 0.0), "k3": (-speed, 0.0, 0.0)}[kind]


def g6_inputs(terrain, kind, k, dt, velocity=None):
    h = SCENE["g6_height"]
    speed = SCENE["g6_speed"]
    velocity = g6_velocity(kind) if velocity is None else tuple(velocity)

    def lift(x, y):
        return h + (ground(terrain, x, y) if terrain is not None else 0.0)
    if kind == "k1":
        half = SCENE["g6_capsule_half"]
        y0 = min(SCENE["g6_capsule_from"] + speed * dt * k, SCENE["g6_capsule_to"])
        y1 = min(SCENE["g6_capsule_from"] + speed * dt * (k + 1), SCENE["g6_capsule_to"])

        def end(x, y):
            return (x, y, lift(x, y))
        return core.capsule_spheres(end(-half, y0), end(half, y0), end(-half, y1), end(half, y1), SCENE["g6_capsule_radius"],
                                    velocity=velocity)
    return [core.still_sphere((0.0, 0.0, lift(0.0, 0.0)), SCENE["g6_sphere_radius"], velocity, SCENE["g6_sphere_wind"])]


def converted_speed(sim, touched, velocity):
    vx, vy, vz = (float(c) for c in velocity)
    mag = np.sqrt(vx * vx + vy * vy + (vz - vx * sim.slope_x - vy * sim.slope_y) ** 2)
    if not touched.any():
        return float("nan"), float("nan"), float("nan")
    i, j = np.unravel_index(np.argmax(np.where(touched, mag, -1.0)), mag.shape)
    return float(mag[i, j]), float(sim.slope_x[i, j]), float(sim.slope_y[i, j])


def run_g6_scene(sim, make_inputs, n_in, n_after, track_touched=False, track_world=False):
    peak, wind_max, world_max = 0.0, 0.0, 0.0
    touched = np.zeros((sim.nx, sim.ny), bool)
    ok = True
    for k in range(n_in + n_after):
        ins = make_inputs(k) if k < n_in else []
        if ins and track_touched:
            touched |= sim.touched_columns(ins)
        sim.step(ins)
        if not finite(sim):
            ok = False
            break
        peak = max(peak, sim.wind_energy())
        wind_max = max(wind_max, sim.max_wind())
        if track_world:
            world_max = max(world_max, float(sim.world_wind_speed().max()))
    end_ratio = sim.wind_energy() / peak if ok and peak > 0 else float("nan")
    return dict(finite=ok, max_wind=wind_max, peak_energy=peak, energy_end_over_peak=end_ratio, world_wind_max=world_max, touched=touched,
                state_hash=sim.state_hash())


def flat_control_g6(kind, dt, velocity, n_in, n_after):
    sim = core2.Sim(game_params(dt=dt))
    r = run_g6_scene(sim, lambda k: g6_inputs(None, kind, k, dt, velocity), n_in, n_after)
    return dict(velocity=list(velocity), finite=r["finite"], max_wind=r["max_wind"], peak_energy=r["peak_energy"],
                energy_end_over_peak=r["energy_end_over_peak"], state_hash=r["state_hash"])


def piece_g6(spec):
    terrain = TERRAINS[spec["terrain"]]
    kind = spec["motion"]
    dt = 1.0 / spec["hz"]
    sim = make_sim(spec["cand"], terrain, game_params(dt=dt))
    n_in = count(steps_for(dt, SCENE["g6_input_seconds"]), spec["smoke"])
    n_after = count(steps_for(dt, SCENE["g6_after_seconds"]), spec["smoke"])
    r = run_g6_scene(sim, lambda k: g6_inputs(terrain, kind, k, dt), n_in, n_after, track_touched=True, track_world=True)
    ok, wind_max, end_ratio = r["finite"], r["max_wind"], r["energy_end_over_peak"]
    vx, vy, vz = g6_velocity(kind)
    speed, sx, sy = converted_speed(sim, r["touched"], (vx, vy, vz))
    flat = None
    if math.isfinite(speed):
        flat = flat_control_g6(kind, dt, (vx, vy, vz - vx * sx - vy * sy), n_in, n_after)
    big_m = max(speed, flat["max_wind"]) if flat else float("nan")
    limit = THRESHOLDS["G6_WIND_FACTOR"] * big_m
    energy_ok = bool(end_ratio <= THRESHOLDS["G6_ENERGY_RATIO"])
    passed = bool(ok and flat is not None and wind_max <= limit and energy_ok)
    original_limit = THRESHOLDS["G6_WIND_FACTOR"] * speed
    original = dict(limit=original_limit, wind_over_limit=wind_max / original_limit if original_limit > 0 else float("nan"),
                    wind_passed=bool(ok and wind_max <= original_limit), gate_passed=bool(ok and wind_max <= original_limit and energy_ok),
                    flat_over_converted=flat["max_wind"] / speed if flat and speed > 0 else float("nan"))
    return dict(finite=ok, max_wind=wind_max, converted_input_speed=speed, slope_x=sx, slope_y=sy, flat_control=flat, M=big_m,
                limit=limit, wind_over_limit=wind_max / limit if limit > 0 else float("nan"), info9_original=original,
                energy_end_over_peak=end_ratio, peak_energy=r["peak_energy"], world_wind_max=r["world_wind_max"], input_steps=n_in,
                after_steps=n_after, passed=passed)


def piece_g8(spec):
    values = {}
    offsets = (np.arange(SCENE["g3_sub"]) + 0.5) / SCENE["g3_sub"] * HX
    for name in ("F0",) + SCENE["g8_slopes"]:
        terrain = TERRAINS[name]
        sim = make_sim(spec["cand"], terrain, game_params())
        for radius in SCENE["g8_radii"]:
            sums = [sim.input_weight_sum((x, y, ground(terrain, x, y) + SCENE["g8_height"]), radius) for x in offsets for y in offsets]
            values[f"{name}_r{radius:g}"] = float(np.mean(sums))
    ratios = {f"{name}_r{r:g}": values[f"{name}_r{r:g}"] / values[f"F0_r{r:g}"] for name in SCENE["g8_slopes"] for r in SCENE["g8_radii"]}
    return dict(weight_sums=values, ratios=ratios)


def snapped_origin(value_cm, n):
    cell = int(math.floor(value_cm / HX))
    return int(math.floor((cell - n // 2) / 16 + 0.5)) * 16


def overlap_same(prev_origin, prev_h, prev_wall, sim):
    dx = sim.origin[0] - prev_origin[0]
    dy = sim.origin[1] - prev_origin[1]
    nx, ny = sim.nx, sim.ny
    xs_old, xs_new = slice(max(0, dx), min(nx, nx + dx)), slice(max(0, -dx), min(nx, nx - dx))
    ys_old, ys_new = slice(max(0, dy), min(ny, ny + dy)), slice(max(0, -dy), min(ny, ny - dy))
    same_h = bool(np.array_equal(prev_h[xs_old, ys_old], sim.column_height[xs_new, ys_new]))
    same_wall = bool(np.array_equal(prev_wall[xs_old, ys_old], sim.wall_columns[xs_new, ys_new]))
    return same_h, same_wall


def piece_g9(spec):
    terrain = TERRAINS[spec["terrain"]]
    y = SCENE["g9_lines"][spec["terrain"]]
    follow = spec["mode"] == "follow"
    y_origin = snapped_origin(y, 128)
    if follow:
        sim = make_sim(spec["cand"], terrain, game_params(), window_min=(snapped_origin(SCENE["g9_x0"], 128), y_origin))
    else:
        nx = SCENE["g9_fixed_nx"]
        sim = make_sim(spec["cand"], terrain, game_params(nx=nx), window_min=(core._floor_to(-(nx // 2), 16), y_origin))
    dt = sim.p.dt
    total = count(int(math.ceil(SCENE["g9_seconds"] / dt - 1e-9)), spec["smoke"])
    x = SCENE["g9_x0"]
    h = SCENE["g9_height"]
    moves, same_h, same_wall = 0, True, True
    for _ in range(total):
        x_new = x + SCENE["g9_speed"] * dt
        a = (x, y, ground(terrain, x, y) + h)
        b = (x_new, y, ground(terrain, x_new, y) + h)
        inp = core.SweptSphere(a, b, SCENE["g9_radius"], tuple((np.asarray(b) - np.asarray(a)) / dt), SCENE["g9_strength"], SCENE["g9_erase"])
        x = x_new
        prev = (sim.origin, sim.column_height.copy(), sim.wall_columns.copy())
        sim.step([inp], (x, y, h) if follow else None)
        if sim.origin != prev[0]:
            moves += 1
            h_ok, w_ok = overlap_same(prev[0], prev[1], prev[2], sim)
            same_h &= h_ok
            same_wall &= w_ok
    np.savez_compressed(spec["npz"], u=sim.u, v=sim.v, w=sim.w, D=sim.D, origin=np.asarray(sim.origin), n=np.asarray((sim.nx, sim.ny)),
                        column_height=sim.column_height, wall=sim.wall_columns)
    return dict(mode=spec["mode"], moves=moves, same_height_on_move=same_h, same_wall_on_move=same_wall, origin=list(sim.origin),
                steps=total, finite=finite(sim))


def piece_g10(spec):
    terrain = TERRAINS[SCENE["g10_terrain"]]
    sim = make_sim(spec["cand"], terrain, game_params())
    start, heading = WALK_PATHS["up"]
    rig = ((0.0, 0.0, SCENE["g10_height"], SCENE["g10_radius"]),)
    walker = Walker(terrain, start, heading, spec["smoke"], rig=rig, stops=False)
    for _ in range(walker.total):
        sim.step(walker.next_inputs())
    return dict(top_target_max=sim.top_target_max, steps=walker.total, finite=finite(sim))


PIECE_RUNNERS = {"walk": piece_walk, "g1": piece_g1, "g2": piece_g2, "g3": piece_g3, "g4": piece_g4, "g6": piece_g6, "g8": piece_g8,
                 "g9": piece_g9, "g10": piece_g10}
PIECE_COST = {"walk": 148, "g1": 400, "g2": 300, "g3": 1, "g4": 190, "g6": 510, "g8": 5, "g9": 600, "g10": 70}


def spec(cand, gate, variant, kind, smoke, **kw):
    out = dict(id=f"{cand}_{gate}_{variant}", cand=cand, gate=gate, kind=kind, smoke=smoke)
    out.update(kw)
    return out


def walk_spec(cand, terrain, path, smoke, save_fields=False):
    return spec(cand, "WALK", f"{terrain}_{path}", "walk", smoke, terrain=terrain, path=path, save_fields=save_fields)


def g6_variants():
    out = [(t, k, 60) for t in SCENE["g6_slopes"] for k in ("k1", "k2", "k3")]
    out += [("S60", "k2", hz) for hz in SCENE["g6_s60_extra_hz"]]
    out.append(("ROUGH", "k2", 60))
    return out


def gate_specs(cand, gate, smoke):
    if gate == "G1":
        return [spec(cand, gate, f"{name}_{level}", "g1", smoke, scene=name, level=level) for name in SCENE["g1_scenes"] for level in ("F0", "F1")]
    if gate == "G2":
        return [spec(cand, gate, t, "g2", smoke, terrain=t) for t in SCENE["g2_terrains"]]
    if gate == "G3":
        return [spec(cand, gate, t, "g3", smoke, terrain=t) for t in SMOOTH_TERRAINS + CLIFF_TERRAINS]
    if gate == "G4":
        return [spec(cand, gate, f"{t}_{s}", "g4", smoke, terrain=t, side=s) for t in CLIFF_TERRAINS for s in ("a", "b")]
    if gate == "G5":
        return [walk_spec(cand, "CURB30", p, smoke) for p in ("up", "down")] + [walk_spec(REFERENCE_CANDIDATE, "F0", p, smoke) for p in ("up", "down")]
    if gate == "G6":
        out = [spec(cand, gate, f"{t}_{k}_{hz}", "g6", smoke, terrain=t, motion=k, hz=hz) for t, k, hz in g6_variants()]
        flat = sorted({(k, hz) for _, k, hz in g6_variants()})
        out += [spec(REFERENCE_CANDIDATE, gate, f"F0_{k}_{hz}", "g6", smoke, terrain="F0", motion=k, hz=hz) for k, hz in flat]
        return out
    if gate == "G7":
        return [walk_spec(cand, t, p, smoke, save_fields=cand in ("R2", "R3")) for t in ("S20", "S30") for p in ("up", "down")]
    if gate == "G8":
        return [spec(cand, gate, "all", "g8", smoke)]
    if gate == "G9":
        return [spec(cand, gate, f"{t}_{m}", "g9", smoke, terrain=t, mode=m) for t in SCENE["g9_lines"] for m in ("fixed", "follow")]
    if gate == "G10":
        return [spec(cand, gate, SCENE["g10_terrain"], "g10", smoke)]
    raise ValueError(gate)


def info_specs(chosen, smoke):
    out = [walk_spec(chosen, t, p, smoke) for t in ("CURB45", "CURB60") for p in ("up", "down")]
    out += [walk_spec(REFERENCE_CANDIDATE, "F0", p, smoke) for p in ("up", "down", "contour")]
    out += [walk_spec(chosen, t, p, smoke) for t in ("S10", "S45") for p in ("up", "down")]
    out += [walk_spec(chosen, t, "contour", smoke) for t in ("S10", "S20", "S30", "S45")]
    out += [spec(chosen, "G3", f"{t}_src{s:g}", "g3", smoke, terrain=t, spacing=s) for t in SMOOTH_TERRAINS + CLIFF_TERRAINS for s in SCENE["source_spacings"]]
    for cand in ("R2", "R3", VARIANT_CANDIDATE):
        out += gate_specs(cand, "G7", smoke)
    for cand in ("R1", "R2"):
        out.append(spec(cand, "G4", "CLIFF_a", "g4", smoke, terrain="CLIFF", side="a"))
    for cand in ("R3", VARIANT_CANDIDATE):
        out.append(spec(cand, "G4", "CLIFF_a_x-20", "g4", smoke, terrain="CLIFF", side="a", walk_point=SCENE["info8_cliff_walk_point"]))
    out += gate_specs(chosen, "G6", smoke)
    return out


def piece_paths(out_dir, piece_id):
    base = os.path.join(out_dir, "pieces", piece_id)
    return base + ".json", base + ".npz"


def run_piece(item):
    s, out_dir = item
    path, npz = piece_paths(out_dir, s["id"])
    s = dict(s)
    if s["kind"] == "g9" or (s["kind"] == "g4" and s["terrain"] == "CLIFF" and s["side"] == "a" and not s.get("walk_point")) or s.get("save_fields"):
        s["npz"] = npz
    t0 = time.time()
    try:
        values = PIECE_RUNNERS[s["kind"]](s)
        error = None
    except Exception:
        values, error = None, traceback.format_exc()
    res = dict(spec={k: v for k, v in s.items() if k != "npz"}, values=values, error=error, elapsed_sec=round(time.time() - t0, 2),
               code=code_fingerprints(), locked=locked_check()["ok"])
    if error is None:
        tmp = path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as fh:
            json.dump(res, fh, ensure_ascii=False, indent=1)
        os.replace(tmp, path)
    return s["id"], error, res["elapsed_sec"]


def execute(specs, out_dir, jobs, force):
    os.makedirs(os.path.join(out_dir, "pieces"), exist_ok=True)
    unique = {}
    for s in specs:
        unique.setdefault(s["id"], s)
    todo = [s for s in unique.values() if force or not os.path.exists(piece_paths(out_dir, s["id"])[0])]
    todo.sort(key=lambda s: -PIECE_COST[s["kind"]])
    if not todo:
        return []
    print(f"[run] 조각 {len(todo)}개, 프로세스 {jobs}개", flush=True)
    failures = []
    with concurrent.futures.ProcessPoolExecutor(max_workers=jobs) as pool:
        for piece_id, error, elapsed in pool.map(run_piece, [(s, out_dir) for s in todo]):
            print(f"[run] {piece_id} {'실패' if error else '끝'} {elapsed}s", flush=True)
            if error:
                failures.append((piece_id, error))
                print(error, flush=True)
    return failures


def load_pieces(out_dir):
    out = {}
    folder = os.path.join(out_dir, "pieces")
    if not os.path.isdir(folder):
        return out
    for name in os.listdir(folder):
        if name.endswith(".json"):
            with open(os.path.join(folder, name), encoding="utf-8") as fh:
                out[name[:-5]] = json.load(fh)
    return out


def values_of(pieces, piece_id):
    p = pieces.get(piece_id)
    return None if p is None else p["values"]


def need(pieces, ids):
    vals = [values_of(pieces, i) for i in ids]
    return None if any(v is None for v in vals) else vals


def gate_result(value, threshold, passed, **detail):
    return dict(value=value, threshold=threshold, passed=passed, detail=detail)


def eval_g1(pieces, c):
    ids = [s["id"] for s in gate_specs(c, "G1", False)]
    vals = need(pieces, ids)
    if vals is None:
        return None
    f0 = max(v["max_abs_diff"] for v in vals if v["level"] == "F0")
    f1 = max(v["max_abs_diff"] for v in vals if v["level"] == "F1")
    return gate_result(dict(F0=f0, F1=f1), dict(F0=THRESHOLDS["G1_F0"], F1=THRESHOLDS["G1_F1"]),
                       bool(f0 <= THRESHOLDS["G1_F0"] and f1 <= THRESHOLDS["G1_F1"]),
                       by_scene={f"{v['scene']}_{v['level']}": v["max_abs_diff"] for v in vals},
                       bit_identical={f"{v['scene']}_{v['level']}": v["bit_identical"] for v in vals})


def eval_g2(pieces, c):
    names = SCENE["g2_terrains"]
    vals = need(pieces, [f"{c}_G2_{t}" for t in names])
    if vals is None:
        return None
    worst = max(v["state_abs_max"] for v in vals)
    return gate_result(worst, THRESHOLDS["G2_EXACT"], bool(worst == THRESHOLDS["G2_EXACT"]), by_terrain={t: v["state_abs_max"] for t, v in zip(names, vals)})


def eval_g3(pieces, c):
    names = SMOOTH_TERRAINS + CLIFF_TERRAINS
    vals = need(pieces, [f"{c}_G3_{t}" for t in names])
    if vals is None:
        return None
    by = {t: v["max_e"] for t, v in zip(names, vals)}
    smooth = max(by[t] for t in SMOOTH_TERRAINS)
    cliff = max(by[t] for t in CLIFF_TERRAINS)
    lim = THRESHOLDS["G3_E_CM"]
    return gate_result(dict(smooth=smooth, cliff=cliff), lim, bool(smooth <= lim and cliff <= lim), by_terrain=by)


def eval_g4(pieces, c):
    ids = [s["id"] for s in gate_specs(c, "G4", False)]
    vals = need(pieces, ids)
    if vals is None:
        return None
    worst = max(v["opposite_max"] for v in vals)
    return gate_result(worst, THRESHOLDS["G4_LEAK"], bool(worst <= THRESHOLDS["G4_LEAK"]),
                       by_run={i.split("_G4_")[1]: v["opposite_max"] for i, v in zip(ids, vals)})


def curb_ratios(pieces, c, terrain):
    vals = need(pieces, [f"{c}_WALK_{terrain}_up", f"{c}_WALK_{terrain}_down", f"{REFERENCE_CANDIDATE}_WALK_F0_up", f"{REFERENCE_CANDIDATE}_WALK_F0_down"])
    if vals is None:
        return None

    def ratio(a, b):
        return a / b if b > 0 else float("nan")
    return dict(up=ratio(vals[0]["crossing_abs_mean"], vals[2]["crossing_abs_mean"]), down=ratio(vals[1]["crossing_abs_mean"], vals[3]["crossing_abs_mean"]))


def eval_g5(pieces, c):
    r = curb_ratios(pieces, c, "CURB30")
    if r is None:
        return None
    lim = THRESHOLDS["G5_RATIO_MIN"]
    return gate_result(r, lim, bool(r["up"] >= lim and r["down"] >= lim))


def eval_g6(pieces, c):
    ids = [f"{c}_G6_{t}_{k}_{hz}" for t, k, hz in g6_variants()]
    vals = need(pieces, ids)
    if vals is None:
        return None
    runs = {i.split("_G6_")[1]: dict(max_wind=v["max_wind"], converted_input_speed=v["converted_input_speed"],
                                     flat_control_max_wind=(v.get("flat_control") or {}).get("max_wind"), M=v.get("M"), limit=v["limit"],
                                     wind_over_limit=v.get("wind_over_limit"), energy=v["energy_end_over_peak"], finite=v["finite"],
                                     passed=v["passed"], info9_original=v.get("info9_original"))
            for i, v in zip(ids, vals)}
    return gate_result({k: r["wind_over_limit"] for k, r in runs.items()}, dict(wind_over_limit=1.0, energy=THRESHOLDS["G6_ENERGY_RATIO"]),
                       bool(all(r["passed"] for r in runs.values())), runs=runs)


def asymmetry(pieces, c, terrain):
    vals = need(pieces, [f"{c}_WALK_{terrain}_up", f"{c}_WALK_{terrain}_down"])
    if vals is None:
        return None
    su, sd = vals[0]["S"], vals[1]["S"]
    a = abs(su - sd) / (su + sd) if su + sd > 0 else float("inf")
    return dict(S_up=su, S_down=sd, A=a, signed_up=vals[0]["signed_sum"], signed_down=vals[1]["signed_sum"])


def eval_g7(pieces, c):
    rs = {t: asymmetry(pieces, c, t) for t in ("S20", "S30")}
    if any(v is None for v in rs.values()):
        return None
    lim = THRESHOLDS["G7_A_MAX"]
    return gate_result({t: v["A"] for t, v in rs.items()}, lim, bool(all(v["A"] <= lim for v in rs.values())), runs=rs)


def eval_g8(pieces, c):
    v = values_of(pieces, f"{c}_G8_all")
    if v is None:
        return None
    lo, hi = THRESHOLDS["G8_LO"], THRESHOLDS["G8_HI"]
    return gate_result(v["ratios"], [lo, hi], bool(all(lo <= r <= hi for r in v["ratios"].values())))


def fields_from_npz(path):
    d = np.load(path)
    out = {k: d[k] for k in ("u", "v", "w", "D", "column_height", "wall")}
    out["origin"] = tuple(int(x) for x in d["origin"])
    out["n"] = tuple(int(x) for x in d["n"])
    return out


def eval_g9(pieces, c, out_dir):
    runs = {}
    for t in SCENE["g9_lines"]:
        vals = need(pieces, [f"{c}_G9_{t}_follow", f"{c}_G9_{t}_fixed"])
        if vals is None:
            return None
        fa = fields_from_npz(piece_paths(out_dir, f"{c}_G9_{t}_follow")[1])
        fb = fields_from_npz(piece_paths(out_dir, f"{c}_G9_{t}_fixed")[1])
        try:
            cmp = gates.g9_compare(fa, fb)["values"]
        except SystemExit as exc:
            runs[t] = dict(error=str(exc), passed=False)
            continue
        oa, ob, n = fa["origin"], fb["origin"], fa["n"]
        fx = slice(oa[0] - ob[0], oa[0] + n[0] - ob[0])
        fy = slice(oa[1] - ob[1], oa[1] + n[1] - ob[1])
        cmp.update(moves=vals[0]["moves"], same_height_on_move=vals[0]["same_height_on_move"], same_wall_on_move=vals[0]["same_wall_on_move"],
                   same_height_as_fixed=bool(np.array_equal(fa["column_height"], fb["column_height"][fx, fy])),
                   same_wall_as_fixed=bool(np.array_equal(fa["wall"], fb["wall"][fx, fy])))
        cmp["passed"] = bool(cmp["ratio_1"] <= THRESHOLDS["G9_RATIO_1"] and cmp["ratio_2"] <= THRESHOLDS["G9_RATIO_2"] and cmp["ratio_3"] <= THRESHOLDS["G9_RATIO_3"]
                             and cmp["same_height_on_move"] and cmp["same_wall_on_move"] and cmp["same_height_as_fixed"] and cmp["same_wall_as_fixed"])
        runs[t] = cmp
    return gate_result({t: {k: r.get(k) for k in ("ratio_1", "ratio_2", "ratio_3")} for t, r in runs.items()},
                       [THRESHOLDS["G9_RATIO_1"], THRESHOLDS["G9_RATIO_2"], THRESHOLDS["G9_RATIO_3"]], bool(all(r["passed"] for r in runs.values())), runs=runs)


def eval_g10(pieces, c):
    v = values_of(pieces, f"{c}_G10_{SCENE['g10_terrain']}")
    if v is None:
        return None
    lim = THRESHOLDS["G10_TARGET_CM_S"]
    return gate_result(v["top_target_max"], lim, bool(v["top_target_max"] <= lim))


def evaluate_candidate(pieces, c, out_dir):
    table = {"G1": lambda: eval_g1(pieces, c), "G2": lambda: eval_g2(pieces, c), "G3": lambda: eval_g3(pieces, c),
             "G4": lambda: eval_g4(pieces, c), "G5": lambda: eval_g5(pieces, c), "G6": lambda: eval_g6(pieces, c),
             "G7": lambda: eval_g7(pieces, c), "G8": lambda: eval_g8(pieces, c), "G9": lambda: eval_g9(pieces, c, out_dir),
             "G10": lambda: eval_g10(pieces, c)}
    out = {}
    for g in ALL_GATES:
        r = table[g]()
        if r is not None:
            r["kind"] = GATE_KIND[g]
            r["applies"] = g in CANDIDATE_GATES[c]
        out[g] = r
    return out


def candidate_status(c, gates_done):
    applies = CANDIDATE_GATES[c]
    deciding_failed = [g for g in applies if gates_done.get(g) and GATE_KIND[g] == "deciding" and not gates_done[g]["passed"]]
    if deciding_failed:
        return dict(status="eliminated", failed=deciding_failed)
    required_failed = [g for g in applies if gates_done.get(g) and GATE_KIND[g] == "required" and not gates_done[g]["passed"]]
    if required_failed:
        return dict(status="required_failed", failed=required_failed)
    missing = [g for g in applies if not gates_done.get(g)]
    if missing:
        return dict(status="incomplete", missing=missing)
    return dict(status="all_passed")


def select(statuses, order):
    for c in order:
        st = statuses[c]["status"]
        if st == "eliminated":
            continue
        if st == "all_passed":
            return dict(chosen=c, reason="이 후보에 해당하는 선을 모두 만족한 첫 후보")
        return dict(chosen=None, reason=f"{c} 이(가) {st} 상태라 고르기를 멈춤")
    return dict(chosen=None, reason="모든 후보 탈락")


def run_stage_order(args):
    cands = [c for c in CANDIDATE_ORDER if c in args.candidates]
    if args.only:
        specs = []
        for c in cands:
            for g in args.only:
                if g != "INFO" and g in CANDIDATE_GATES[c]:
                    specs += gate_specs(c, g, args.smoke)
        if "INFO" in args.only:
            specs += info_specs(cands[-1], args.smoke)
        return execute(specs, args.out_dir, args.jobs, args.force)
    failures = execute([s for c in cands if c in PRIORITY_GATE for s in gate_specs(c, PRIORITY_GATE[c], args.smoke)], args.out_dir, args.jobs, args.force)
    chosen = None
    for c in cands:
        st = candidate_status(c, evaluate_candidate(load_pieces(args.out_dir), c, args.out_dir))
        if st["status"] == "eliminated":
            print(f"[run] {c} 탈락 확정({st['failed']}), 나머지 선 건너뜀", flush=True)
            continue
        failures += execute([s for g in CANDIDATE_GATES[c] for s in gate_specs(c, g, args.smoke)], args.out_dir, args.jobs, args.force)
        st = candidate_status(c, evaluate_candidate(load_pieces(args.out_dir), c, args.out_dir))
        print(f"[run] {c}: {st}", flush=True)
        if st["status"] == "all_passed":
            chosen = c
            break
        if st["status"] != "eliminated":
            print(f"[run] {c} 이(가) {st['status']} 상태라 다음 후보로 넘어가지 않음(5절 필수 선 규칙은 사람이 판정)", flush=True)
            break
    failures += execute(info_specs(chosen or "R3", args.smoke), args.out_dir, args.jobs, args.force)
    return failures


def cmd_run(args):
    check = locked_check()
    if not check["ok"]:
        print(f"[run] 잠긴 파일 지문 불일치: {check['mismatched']}", flush=True)
        return 2
    t0 = time.time()
    failures = run_stage_order(args)
    print(f"[run] 끝 {round(time.time() - t0, 1)}s, 실패 조각 {len(failures)}", flush=True)
    return 1 if failures else 0


def step_time(pieces):
    per = []
    for p in pieces.values():
        s, v = p["spec"], p["values"]
        if s["kind"] == "walk" and v and v.get("steps"):
            per.append(p["elapsed_sec"] / v["steps"])
    return float(np.median(per)) if per else None


def info_block(pieces, c, out_dir):
    info = {}
    if c is None:
        return info
    info["1_curb"] = {t: curb_ratios(pieces, c, t) for t in ("CURB45", "CURB60")}
    flat = {p: values_of(pieces, f"{REFERENCE_CANDIDATE}_WALK_F0_{p}") for p in ("up", "down", "contour")}
    ratios = {}
    for t in ("S10", "S20", "S30", "S45"):
        for p in ("up", "down", "contour"):
            v = values_of(pieces, f"{c}_WALK_{t}_{p}")
            if v is not None and flat[p] is not None and flat[p]["S"] > 0:
                ratios[f"{t}_{p}"] = dict(S=v["S"], over_flat=v["S"] / flat[p]["S"], signed_sum=v["signed_sum"])
    info["2_slope_walks"] = dict(ratios=ratios, A={t: asymmetry(pieces, c, t) for t in ("S10", "S20", "S30", "S45")})
    diffs = {}
    for t in ("S20", "S30"):
        for p in ("up", "down"):
            a, b = piece_paths(out_dir, f"R2_WALK_{t}_{p}")[1], piece_paths(out_dir, f"R3_WALK_{t}_{p}")[1]
            if os.path.exists(a) and os.path.exists(b):
                fa, fb = np.load(a), np.load(b)
                diffs[f"{t}_{p}"] = {k: float(np.abs(fa[k].astype(np.float64) - fb[k]).max()) for k in STATE_NAMES}
    info["3_R2_vs_R3_field_diff"] = diffs
    info["4_source_resolution"] = {f"{t}_src{s:g}": (values_of(pieces, f"{c}_G3_{t}_src{s:g}") or {}).get("max_e")
                                   for t in SMOOTH_TERRAINS + CLIFF_TERRAINS for s in SCENE["source_spacings"]}
    info["5_cost"] = cost_estimate()
    world = {}
    for t, k, hz in g6_variants():
        v, f = values_of(pieces, f"{c}_G6_{t}_{k}_{hz}"), values_of(pieces, f"{REFERENCE_CANDIDATE}_G6_F0_{k}_{hz}")
        if v and f and f["world_wind_max"] > 0:
            world[f"{t}_{k}_{hz}"] = dict(world_wind_max=v["world_wind_max"], over_flat=v["world_wind_max"] / f["world_wind_max"])
    info["7_world_wind"] = world
    variant = {}
    for cand in ("R3", VARIANT_CANDIDATE):
        variant[cand] = dict(G7={t: asymmetry(pieces, cand, t) for t in ("S20", "S30")},
                             G4_cliff_a_x_minus_20=(values_of(pieces, f"{cand}_G4_CLIFF_a_x-20") or {}).get("opposite_max"))
    info["8_ground_frame_sphere"] = variant
    info["9_g6_original_limit"] = {f"{t}_{k}_{hz}": (values_of(pieces, f"{c}_G6_{t}_{k}_{hz}") or {}).get("info9_original") for t, k, hz in g6_variants()}
    return info


def cost_estimate(n=256):
    cells = n * n * 8
    return dict(extra_reads_per_cell=dict(input_spheres="u·v 면은 양쪽 기둥 높이 2번, w 면·칸 가운데는 1번(입력 구 상자 안 칸만)",
                                          top_target_transform="기둥 기울기 2개(Sx, Sy) 읽기 1번(입력 구 상자 안 w 면만)",
                                          drawing="재질 표본마다 높이 그림 가로 선형 보간 4번"),
                extra_memory_bytes=dict(column_height_f32=n * n * 4, slopes_f32x2=n * n * 8, total=n * n * 12, state_bytes_ref=cells * 74),
                bake_reads=dict(per_column="높이 5번(자기+이웃 4, 창 밖 한 칸 고리 포함) → 벽·기울기", per_bake=n * n * 5,
                                when="창 이동·다시 시작·물체 목록 변경 때만"))


def pyplot():
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib import font_manager
    names = {f.name for f in font_manager.fontManager.ttflist}
    for family in ("Malgun Gothic", "NanumGothic", "AppleGothic"):
        if family in names:
            plt.rcParams["font.family"] = family
            break
    plt.rcParams["axes.unicode_minus"] = False
    return plt


def figure_rest(out_path, chosen):
    plt = pyplot()
    terrain = TERRAINS["HILL"]
    params = game_params()
    xs = np.linspace(-900.0, 900.0, 721)
    zs = np.linspace(-200.0, 450.0, 261)
    fig, axes = plt.subplots(1, 2, figsize=(12, 4), sharey=True)
    for ax, cand in zip(axes, ("R0", chosen or "R3")):
        sim = make_sim(cand, terrain, params)
        g_draw = sim.drawn_ground(xs, np.zeros_like(xs))
        g_true = terrain.height(xs, np.zeros_like(xs))
        height = zs[:, None] - g_draw[None, :]
        dens = core.baseline32(params, height, np.full(height.shape, params.thickness_cm)).astype(float)
        dens = np.where(height >= 0, dens, 0.0) if cand != "R0" else np.where(zs[:, None] >= 0, dens, 0.0)
        ax.pcolormesh(xs, zs, dens, shading="auto", cmap="Blues", vmin=0, vmax=params.base_density)
        ax.plot(xs, g_true, color="saddlebrown", lw=1.5, label="땅(해석식)")
        ax.fill_between(xs, -200, g_true, color="saddlebrown", alpha=0.25)
        ax.set_title(f"{cand} 정지 안개 (HILL, y=0)")
        ax.set_xlabel("x (cm)")
    axes[0].set_ylabel("세계 높이 z (cm)")
    fig.tight_layout()
    fig.savefig(out_path, dpi=110)
    plt.close(fig)


def figure_cliff(out_path, out_dir):
    plt = pyplot()
    fig, axes = plt.subplots(1, 2, figsize=(12, 4), sharey=True)
    drawn_any = False
    for ax, cand in zip(axes, ("R1", "R2")):
        path = piece_paths(out_dir, f"{cand}_G4_CLIFF_a")[1]
        if not os.path.exists(path):
            ax.set_title(f"{cand}: 자료 없음")
            continue
        drawn_any = True
        d = np.load(path)
        rx, heights, drawn, ground_row = d["rx"], d["heights"], d["drawn"], d["ground"]
        keep = np.abs(rx) <= 500.0
        for i in np.nonzero(keep)[0]:
            z = ground_row[i] + heights
            ax.scatter(np.full(z.shape, rx[i]), z, c=drawn[i], cmap="Blues", vmin=0, vmax=0.8, s=4, marker="s")
            if d["wall"][i]:
                ax.axvspan(rx[i] - HX / 2, rx[i] + HX / 2, color="gray", alpha=0.4)
        ax.plot(rx[keep], ground_row[keep], color="saddlebrown", lw=1.5)
        ax.set_title(f"{cand} 절벽 아래 걷기+칼 뒤 그리는 농도 (CLIFF, y={float(d['row_y']):.1f})")
        ax.set_xlabel("x (cm)")
    axes[0].set_ylabel("세계 높이 z (cm)")
    fig.tight_layout()
    fig.savefig(out_path, dpi=110)
    plt.close(fig)
    return drawn_any


def fmt(v):
    if isinstance(v, bool) or v is None:
        return str(v)
    if isinstance(v, float):
        return f"{v:.4g}"
    if isinstance(v, dict):
        return ", ".join(f"{k}={fmt(x)}" for k, x in v.items())
    if isinstance(v, list):
        return "[" + ", ".join(fmt(x) for x in v) + "]"
    return str(v)


def g6_markdown_row(c, name, run):
    original = run.get("info9_original") or {}
    value = dict(max_wind=run["max_wind"], converted=run["converted_input_speed"], flat_control=run.get("flat_control_max_wind"), M=run.get("M"),
                 ratio=run.get("wind_over_limit"), energy=run["energy"], finite=run["finite"],
                 info9_original_ratio=original.get("wind_over_limit"), info9_original_wind_passed=original.get("wind_passed"),
                 info9_original_gate_passed=original.get("gate_passed"), info9_flat_over_converted=original.get("flat_over_converted"))
    return f"| {c} | G6 · {name} | 장면 | {fmt(value)} | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ {THRESHOLDS['G6_ENERGY_RATIO']:g} | {run['passed']} |"


def write_markdown(path, res):
    lines = ["# 2a 판정 결과", "", f"- 시운전(단계 줄임): {res['smoke']}", f"- 잠긴 파일 지문 일치: {res['locked']['ok']}",
             f"- 코드·등록 지문: {fmt(res['code'])}", f"- 고른 후보: {res['selection']['chosen']} ({res['selection']['reason']})",
             f"- 한 단계 시간(128칸, 걷기 조각 중앙값): {fmt(res['step_seconds_128'])}초", "", "| 후보 | 판정선 | 종류 | 값 | 문턱 | 통과 |", "|---|---|---|---|---|---|"]
    for c, gates in res["candidates"].items():
        for g in ALL_GATES:
            r = gates.get(g)
            if r is None:
                continue
            lines.append(f"| {c} | {g} | {r['kind']}{'' if r['applies'] else '(정보)'} | {fmt(r['value'])} | {fmt(r['threshold'])} | {r['passed']} |")
            if g == "G6":
                lines += [g6_markdown_row(c, name, run) for name, run in r["detail"]["runs"].items()]
    lines += ["", "| 후보 | 상태 |", "|---|---|"] + [f"| {c} | {fmt(s)} |" for c, s in res["statuses"].items()]
    lines += ["", "## 정보", ""] + [f"- {k}: {fmt(v)}" for k, v in res["info"].items()]
    if res["missing_or_failed"]:
        lines += ["", "## 오류 조각", ""] + [f"- {x}" for x in res["missing_or_failed"]]
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")


def json_safe(obj):
    if isinstance(obj, dict):
        return {("_".join(str(x) for x in k) if isinstance(k, tuple) else k): json_safe(v) for k, v in obj.items()}
    if isinstance(obj, (list, tuple)):
        return [json_safe(v) for v in obj]
    return obj


def cmd_collect(args):
    pieces = load_pieces(args.out_dir)
    cands = [c for c in CANDIDATE_ORDER if c in args.candidates]
    gates = {c: evaluate_candidate(pieces, c, args.out_dir) for c in cands}
    statuses = {c: candidate_status(c, gates[c]) for c in cands}
    selection = select(statuses, cands)
    smoke = any(p["spec"].get("smoke") for p in pieces.values())
    res = dict(prereg="Docs/VolumeFog3D/stage2/prereg-2a-design.md", smoke=smoke, locked=locked_check(), code=code_fingerprints(),
               thresholds=THRESHOLDS, scene=json_safe(SCENE), candidates={c: {g: r for g, r in gates[c].items() if r is not None} for c in cands},
               statuses=statuses, selection=selection, info=info_block(pieces, selection["chosen"] or "R3", args.out_dir),
               step_seconds_128=step_time(pieces), pieces=len(pieces),
               missing_or_failed=[k for k, p in pieces.items() if p.get("error")])
    figure_rest(os.path.join(args.out_dir, "section_rest.png"), selection["chosen"])
    res["figure_cliff_drawn"] = figure_cliff(os.path.join(args.out_dir, "section_cliff.png"), args.out_dir)
    with open(os.path.join(args.out_dir, "results.json"), "w", encoding="utf-8") as fh:
        json.dump(res, fh, ensure_ascii=False, indent=1, default=lambda o: o.tolist() if hasattr(o, "tolist") else str(o))
    write_markdown(os.path.join(args.out_dir, "results.md"), res)
    print(json.dumps(dict(selection=selection, statuses=statuses, locked=res["locked"]["ok"]), ensure_ascii=False))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("command", choices=("run", "collect"))
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--only", default="")
    ap.add_argument("--candidates", default=",".join(CANDIDATE_ORDER))
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) - 2))
    ap.add_argument("--smoke", action="store_true")
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args(argv)
    args.only = [g.strip().upper() for g in args.only.split(",") if g.strip()]
    args.candidates = [c.strip().upper() for c in args.candidates.split(",") if c.strip()]
    args.out_dir = os.path.abspath(args.out_dir)
    return cmd_run(args) if args.command == "run" else cmd_collect(args)


if __name__ == "__main__":
    sys.exit(main())
