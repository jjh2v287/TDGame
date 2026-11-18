"""
실행 환경: Python 3.12 + numpy 2.2 + scipy 1.17 (추가 설치 없음), 저장소 루트에서 실행. fog3d_core.py·fog3d_replay.py 와 같은 폴더(두 파일은 고치지 않고 import 해서 씀)
실행 명령: python Tools/FluidFX/fog3d_ref/fog3d_replay_add1.py [--scene all] [--out-replays Tools/FluidFX/fog3d_ref/replays_add1] [--out-ref Saved/FluidFX/Fog3D/ref_add1] [--out-shake Saved/FluidFX/Fog3D/ref_add1_shake] [--no-proof] [--list] [--check <폴더>]
출력: 덧붙임 장면 재생 파일 replays_add1/<장면>.json(형식 fog3d-replay-1 + 덧붙임 키 step.obstacles), 기준 덤프 ref_add1/<장면>__<덤프>.npz, 1e-4cm 흔든 덤프 ref_add1_shake/, 장면이 노린 결함을 가르는지 보이는 변형 실행 결과 ref_add1/proof.json
상태: 1a 독립 반박이 찾은 등록 장면의 구멍(이어짐 넷째 단계, 대칭 깬 첫 풀이, 물체 목록 변경) 세 장면. 2026-11-13 Y2 작성
"""
import argparse
import hashlib
import json
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fog3d_replay as rp  # noqa: E402

core = rp.core
F32 = np.float32
HX = rp.HX
DT = 1.0 / 60.0
DEFAULT_REPLAYS = os.path.join(HERE, "replays_add1")
DEFAULT_REF = os.path.join(rp.ROOT, "Saved", "FluidFX", "Fog3D", "ref_add1")
DEFAULT_SHAKE = os.path.join(rp.ROOT, "Saved", "FluidFX", "Fog3D", "ref_add1_shake")
STATE_NAMES = ("dpx", "dpy", "dpz", "g")

SCENE_CONNECT = "add1_connect4_wall45"
SCENE_SKEW = "add1_first_solve_skew"
SCENE_SWAP = "add1_obstacle_swap"

CONNECT_DT = 1.0 / 20.0
CONNECT_SHIFT_CM = (0.76 * HX, -0.745 * HX, -110.0)
CONNECT_WALL_CELLS = 10
SKEW_CENTER = (0.30 * HX, 0.25 * HX, 52.0)
SKEW_RADIUS = 18.0
SKEW_VELOCITY = (439.0, 234.0, -290.0)
SWAP_CHANGE_STEP = 7
SWAP_AFTER_STEPS = 18
SWAP_BOX_BEFORE = rp.box_from_corners((-55.0, -25.0, 0.0), (-25.0, 25.0, 60.0))
SWAP_WALL_AFTER = rp.box_dict((-5.0, 0.0, 60.0), (rp.THIN_HALF, 45.0, 60.0))


def code_fingerprint():
    out = {}
    for name in ("fog3d_core.py", "fog3d_replay.py", "fog3d_replay_add1.py", "tuned_solvers.json"):
        with open(os.path.join(HERE, name), "rb") as fh:
            out[name] = hashlib.sha256(fh.read()).hexdigest()[:16]
    return out


def finish(r, measure_add1=None):
    out = r.finish()
    out["source"] = dict(tool="fog3d_replay_add1.py", code=code_fingerprint(),
                         addendum="step.obstacles: 있으면 그 계산부터 물체 목록을 이것으로 바꾼다(굽기·정리를 돈다)")
    if measure_add1 is not None:
        out["measure_add1"] = measure_add1
    return out


# ---------------- 장면 ----------------

def scene_connect4():
    diag = math.sqrt(2.0) * HX
    wall = rp.box_dict((0.5 * HX, 0.5 * HX, 15.0), ((CONNECT_WALL_CELLS + 0.5) * diag, rp.THIN_HALF, rp.THIN_HALF), 45.0)
    r = rp.Replay(SCENE_CONNECT, obstacles=[wall])
    for _ in range(3):
        r.step(CONNECT_DT)
    far_side = rp.half((0.5 * HX, 0.5 * HX, 0.0), (-1.0 / math.sqrt(2.0), 1.0 / math.sqrt(2.0), 0.0), z_max=30.0)
    wind = dict(kind="uniform", v_cm_s=[-c / CONNECT_DT for c in CONNECT_SHIFT_CM])
    r.step(CONNECT_DT, wind=wind, ops=[rp.op("set_empty", far_side)], pressure=False, dump="end")
    return finish(r, dict(kind="step4_cells", value="mean_fill",
                          note="재는 칸 = 기준 모의의 이 계산에서 이어짐 넷째 단계가 무게 > 0 인 모서리를 이어 준 유체 칸(기준 덤프 measure_mask). 값 = 그 칸들의 1+g 평균"))


def skew_obstacles():
    cx, cy, _ = SKEW_CENTER
    r = SKEW_RADIUS
    return [rp.box_from_corners((cx + r + 3.0, cy - 100.0, 0.0), (cx + r + 80.0, cy + 100.0, 120.0)),
            rp.box_from_corners((cx - 100.0, cy + r + 3.0, 0.0), (cx + r + 3.0, cy + r + 60.0, 120.0))]


def scene_skew():
    r = rp.Replay(SCENE_SKEW, obstacles=skew_obstacles())
    r.step(DT, [core.still_sphere(SKEW_CENTER, SKEW_RADIUS, SKEW_VELOCITY)], dump="end")
    return finish(r)


def scene_swap():
    r = rp.Replay(SCENE_SWAP, obstacles=[SWAP_BOX_BEFORE])
    z, x = 45.0, -75.0
    n_in = rp.steps_for(DT, 150.0 / 800.0)
    total = SWAP_CHANGE_STEP + 1 + SWAP_AFTER_STEPS
    for s in range(total):
        spheres = []
        if s < n_in:
            x_new = min(x + 800.0 * DT, 75.0)
            spheres = core.capsule_spheres((x, -50, z), (x, 50, z), (x_new, -50, z), (x_new, 50, z), 20.0, velocity=(800.0, 0, 0), erase_per_sec=30.0)
            x = x_new
        dump = "c00" if s == SWAP_CHANGE_STEP else (f"c{SWAP_AFTER_STEPS:02d}" if s == total - 1 else None)
        r.step(DT, spheres, dump=dump)
        if s == SWAP_CHANGE_STEP:
            r.steps[-1]["obstacles"] = [SWAP_WALL_AFTER]
    return finish(r)


SCENES = {SCENE_CONNECT: scene_connect4, SCENE_SKEW: scene_skew, SCENE_SWAP: scene_swap}


# ---------------- 형식 검사 ----------------

def check_add1(replay):
    problems = rp.check_replay(replay)
    for index, s in enumerate(replay.get("steps", [])):
        if "obstacles" not in s:
            continue
        tag = f"steps[{index}]"
        obs = s["obstacles"]
        if not isinstance(obs, list) or len(obs) > rp.MAX_OBSTACLES:
            problems.append(f"{tag} obstacles 는 {rp.MAX_OBSTACLES}개 이하의 목록")
            continue
        for o in obs:
            if not (o.get("kind") == "box" and len(o.get("center_cm", [])) == 3 and len(o.get("half_cm", [])) == 3 and "yaw_deg" in o):
                problems.append(f"{tag} obstacles 물체 필드 오류")
        if int(s.get("repeat", 1)) != 1:
            problems.append(f"{tag} 물체 목록을 바꾸는 계산은 되풀이할 수 없음")
    return problems


# ---------------- 물체 목록 변경 ----------------

def core_obstacles(obstacles):
    return [dict(kind=o["kind"], center=o["center_cm"], half=o["half_cm"], yaw_deg=o["yaw_deg"]) for o in obstacles]


def face_masks(cells):
    fu, fv, fw = cells.copy(), cells.copy(), cells.copy()
    fu[:-1] |= cells[1:]
    fv[:, :-1] |= cells[:, 1:]
    fw[:, :, :-1] |= cells[:, :, 1:]
    return fu, fv, fw


def drawing_values(sim):
    fl = sim.fluid
    out = {}
    for name in STATE_NAMES:
        a = getattr(sim, name).astype(np.float64) * fl
        s = np.zeros(a.shape)
        c = np.zeros(a.shape)
        for axis in range(3):
            lo = [slice(None)] * 3
            hi = [slice(None)] * 3
            lo[axis], hi[axis] = slice(0, -1), slice(1, None)
            s[tuple(hi)] += a[tuple(lo)]
            c[tuple(hi)] += fl[tuple(lo)]
            s[tuple(lo)] += a[tuple(hi)]
            c[tuple(lo)] += fl[tuple(hi)]
        out[name] = np.where(sim.solid & (c > 0), s / np.maximum(c, 1.0), 0.0).astype(F32)
    return out


def change_obstacles(sim, obstacles, variant=None):
    if variant == "no_rebake":
        return dict(variant=variant)
    before_fluid = sim.fluid.copy()
    drawn = drawing_values(sim) if variant == "keep_drawn" else None
    sim.obstacles = core_obstacles(obstacles)
    sim._geometry()
    became_fluid = sim.fluid & ~before_fluid
    became_solid = before_fluid & ~sim.fluid
    fu, fv, fw = face_masks(became_fluid)
    nonzero_before = int(sum(int(np.count_nonzero(getattr(sim, n)[became_fluid])) for n in STATE_NAMES + ("pr",))
                         + np.count_nonzero(sim.u[fu]) + np.count_nonzero(sim.v[fv]) + np.count_nonzero(sim.w[fw]))
    closed_nonzero = int(np.count_nonzero(sim.u[~sim.ou]) + np.count_nonzero(sim.v[~sim.ov]) + np.count_nonzero(sim.w[~sim.ow]))
    if variant != "keep_closed_wind":
        sim.zero_closed_faces()
    sim.u[fu] = 0
    sim.v[fv] = 0
    sim.w[fw] = 0
    for name in STATE_NAMES:
        getattr(sim, name)[became_fluid] = 0
    sim.pr[became_fluid] = 0
    sim.pr[sim.solid] = 0
    sim._zero_solid_state()
    if drawn is not None:
        for name in STATE_NAMES:
            arr = getattr(sim, name)
            arr[became_fluid] = drawn[name][became_fluid]
    sim.refresh_diff()
    return dict(variant=variant, became_fluid=int(became_fluid.sum()), became_solid=int(became_solid.sum()),
                nonzero_in_became_fluid_before=nonzero_before, closed_faces_nonzero_before=closed_nonzero)


# ---------------- 이어짐 넷째 단계 ----------------

def block_reach_without_step4(self, st):
    idx, _, _, _, base = st
    off = [self._end_cell[a] - base[a] for a in range(3)]
    inside = (off[0] >= 0) & (off[0] <= 1) & (off[1] >= 0) & (off[1] <= 1) & (off[2] >= 0) & (off[2] <= 1)
    o = [np.clip(a, 0, 1) for a in off]
    sol = np.take(self.corners8(self.solid), idx, axis=1)
    corner = [((o[0] ^ ((d >> 2) & 1)) * 4 + (o[1] ^ ((d >> 1) & 1)) * 2 + (o[2] ^ (d & 1)))[None] for d in range(8)]
    open_at = [~np.take_along_axis(sol, corner[d], axis=0)[0] for d in range(8)]
    reach = [np.ones(inside.shape, bool)] + [None] * 7
    for d in (4, 2, 1):
        reach[d] = open_at[d]
    for d, (a, b) in core.EDGE_FACES:
        reach[d] = open_at[d] & (reach[a] | reach[b])
    reach[7] = open_at[7] & (reach[6] | reach[5] | reach[3])
    out = np.zeros(sol.shape, bool)
    for d in range(8):
        np.put_along_axis(out, corner[d], reach[d][None], axis=0)
    return out, inside, sol


class Step4Probe:
    def __init__(self, sim):
        self.sim = sim
        self.armed = False
        self.last = None
        original = sim._block_reach

        def wrapped(st):
            out = original(st)
            if self.armed:
                self.record(st, out)
            return out

        sim._block_reach = wrapped

    def record(self, st, out):
        sim = self.sim
        reach, inside, sol = out
        no4, _, _ = block_reach_without_step4(sim, st)
        base = sim._base_weights(st)
        added = inside[None] & reach & ~no4
        weighted = added & (base > 0)
        weight = (np.where(weighted, base, 0.0)).sum(axis=0)
        cells = weighted.any(axis=0) & sim.fluid
        self.last = dict(mask=cells, cells=int(cells.sum()), cells_any_weight=int((added.any(axis=0) & sim.fluid).sum()),
                         weight_sum=float(weight[cells].sum()), weight_max=float(weight[cells].max(initial=0.0)),
                         weight_mean=float(weight[cells].mean()) if cells.any() else 0.0)


# ---------------- 실행기 ----------------

def shaken_wind(wind, rng, dt):
    if wind is None or wind["kind"] != "uniform":
        return wind
    out = dict(wind)
    out["v_cm_s"] = [c + float(rng.choice([-1.0, 1.0])) * rp.SENSITIVITY_SHAKE_CM / dt for c in wind["v_cm_s"]]
    return out


def execute_add1(replay, out_dir=None, shake=False, variant=None):
    sim = rp.make_sim(replay)
    rng = np.random.default_rng(rp.SENSITIVITY_SEED)
    captured = {}
    original_project = sim._project

    def project_and_capture():
        captured["div"] = sim.divergence().astype(F32)
        original_project()

    sim._project = project_and_capture
    probe = Step4Probe(sim) if replay.get("measure_add1", {}).get("kind") == "step4_cells" else None
    done = 0
    written, changes, dumps = [], [], {}
    for step in replay["steps"]:
        count = int(step.get("repeat", 1))
        for repeat_index in range(count):
            last = repeat_index == count - 1
            if tuple(step["window_min"]) != sim.origin:
                sim.set_window(tuple(step["window_min"]))
            if repeat_index == 0 and "obstacles" in step:
                info = change_obstacles(sim, step["obstacles"], variant)
                info["step_index"] = done + 1
                changes.append(info)
            rp.apply_knobs(sim, replay, step)
            if step["reset"] and repeat_index == 0:
                rp.reset_state(sim)
            if repeat_index == 0:
                rp.apply_state_ops(sim, step["state_ops"])
            wind = shaken_wind(step["wind_override"], rng, step["dt"]) if shake else step["wind_override"]
            rp.apply_wind(sim, wind)
            spheres = rp.shaken_spheres(step["spheres"], rng) if shake else step["spheres"]
            captured.pop("div", None)
            if probe:
                probe.armed = bool(last and step["dump"])
            sim.step(rp.core_spheres(spheres))
            done += 1
            if not (last and step["dump"]):
                continue
            names = step.get("dump_fields") or rp.DUMP_FIELDS
            arrays = rp.collect_fields(sim, captured.get("div"), names)
            meta = dict(format=rp.DUMP_FORMAT, scene=replay["name"], dump=step["dump"], n=sim.nx, layers=sim.nz, window_min=list(sim.origin),
                        step_index=done, dt=step["dt"], fields=list(names))
            if probe and probe.last is not None:
                arrays["measure_mask"] = probe.last["mask"]
            dumps[step["dump"]] = {k: np.array(v) for k, v in arrays.items()}
            if out_dir:
                os.makedirs(out_dir, exist_ok=True)
                path = os.path.join(out_dir, f"{replay['name']}__{step['dump']}.npz")
                np.savez_compressed(path, meta=np.array(json.dumps(meta)), **arrays)
                written.append(path)
    result = dict(steps=done, dumps=written, hash=sim.state_hash(), last_R=sim.last.get("R"), changes=changes, arrays=dumps)
    if probe and probe.last is not None:
        result["step4"] = {k: v for k, v in probe.last.items() if k != "mask"}
    return result


# ---------------- 측정(변형과의 차를 셈) ----------------

def mean_fill(arrays, mask):
    return float((1.0 + arrays["g"].astype(np.float64))[mask].mean())


def field_diff(ref, test, band=8):
    fluid = ref["fluid"] > 0.5
    si = slice(band, -band) if band else slice(None)
    mask = fluid[si, si]
    out = {}
    for label, names in (("wind", ("u", "v", "w")), ("disp", ("dx", "dy", "dz")), ("fill", ("g",))):
        num = den = 0.0
        for name in names:
            r = np.where(mask, ref[name][si, si].astype(np.float64), 0.0)
            t = np.where(mask, test[name][si, si].astype(np.float64), 0.0)
            num += float(np.sum((t - r) ** 2))
            den += float(np.sum(r ** 2))
        out[label] = (math.sqrt(num) / math.sqrt(den)) if den > 0 else (0.0 if num == 0 else float("inf"))
    return out


def residual_ratio(arrays):
    fluid = arrays["fluid"] > 0.5
    u, v, w = (arrays[k].astype(np.float64) for k in ("u", "v", "w"))
    um, vm, wm = np.zeros_like(u), np.zeros_like(v), np.zeros_like(w)
    um[1:], vm[:, 1:], wm[:, :, 1:] = u[:-1], v[:, :-1], w[:, :, :-1]
    div = (u - um) / HX + (v - vm) / HX + (w - wm) / rp.LAYER_CM
    before = float(np.sqrt(np.mean(np.square(arrays["divergence"].astype(np.float64)[fluid]))))
    after = float(np.sqrt(np.mean(np.square(div[fluid]))))
    return after / before if before > 0 else 0.0


def swapped_sweep(self, b, p, omega, store):
    for m in self.colors[::-1]:
        y = self.tri_solve(b + self.neighbor_sum(p))
        mm = m[None, :, :] & self.live
        p = np.where(mm, (1.0 - omega) * p + omega * y, p)
        p = store(p)
    return p


def run_patched(replay, owner, attr, replacement, **kw):
    original = getattr(owner, attr)
    setattr(owner, attr, replacement)
    try:
        return execute_add1(replay, **kw)
    finally:
        setattr(owner, attr, original)


def prove(replays, refs, shakes):
    out = {}
    rc, ref = replays[SCENE_CONNECT], refs[SCENE_CONNECT]
    mask = ref["arrays"]["end"]["measure_mask"]
    value = mean_fill(ref["arrays"]["end"], mask)
    sens = abs(mean_fill(shakes[SCENE_CONNECT]["arrays"]["end"], mask) - value)
    tol = max(0.002, 3.0 * sens)
    no4 = run_patched(rc, core.Sim, "_block_reach", block_reach_without_step4)
    v4 = mean_fill(no4["arrays"]["end"], mask)
    out[SCENE_CONNECT] = dict(value=value, sensitivity=sens, tolerance=tol, step4=ref["step4"], variant_no_step4=v4,
                              diff=abs(v4 - value), diff_over_tolerance=abs(v4 - value) / tol,
                              g_change_max_on_mask=float(np.abs(no4["arrays"]["end"]["g"].astype(np.float64) - ref["arrays"]["end"]["g"])[mask].max()),
                              field_diff_vs_no_step4=field_diff(ref["arrays"]["end"], no4["arrays"]["end"]))
    rs, ref = replays[SCENE_SKEW], refs[SCENE_SKEW]
    value = residual_ratio(ref["arrays"]["end"])
    sens = abs(residual_ratio(shakes[SCENE_SKEW]["arrays"]["end"]) - value)
    tol = max(0.02 * value, 0.002)
    sw = run_patched(rs, core.PLevel, "sweep", swapped_sweep)
    r1 = residual_ratio(sw["arrays"]["end"])
    out[SCENE_SKEW] = dict(value=value, core_last_R=ref["last_R"], sensitivity=sens, tolerance=tol, variant_color1_first=r1, diff=abs(r1 - value),
                           diff_over_tolerance=abs(r1 - value) / tol)
    rw, ref = replays[SCENE_SWAP], refs[SCENE_SWAP]
    swap = dict(changes=ref["changes"], dumps={}, variants={})
    for dump, arr in ref["arrays"].items():
        sens = field_diff(arr, shakes[SCENE_SWAP]["arrays"][dump])
        swap["dumps"][dump] = dict(sensitivity=sens, x=max(0.002, 3.0 * max(sens.values())))
    for variant in ("no_rebake", "keep_drawn", "keep_closed_wind"):
        res = execute_add1(rw, variant=variant)
        swap["variants"][variant] = {dump: dict(field_diff=field_diff(ref["arrays"][dump], res["arrays"][dump]),
                                                worst_over_x=max(field_diff(ref["arrays"][dump], res["arrays"][dump]).values()) / swap["dumps"][dump]["x"])
                                     for dump in ref["arrays"]}
    out[SCENE_SWAP] = swap
    return out


# ---------------- 실행 진입 ----------------

def write_replay(replay, folder):
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, f"{replay['name']}.json"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write(rp.dumps_text(replay))


def check_folder(folder):
    bad = 0
    for fname in sorted(os.listdir(folder)):
        if not fname.endswith(".json"):
            continue
        with open(os.path.join(folder, fname), encoding="utf-8") as fh:
            problems = check_add1(json.load(fh))
        if problems:
            bad += 1
            print(f"{fname}: " + "; ".join(problems))
    print(f"형식 검사 끝: 문제 있는 파일 {bad}개")
    return 1 if bad else 0


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser()
    ap.add_argument("--scene", default="all")
    ap.add_argument("--out-replays", default=DEFAULT_REPLAYS)
    ap.add_argument("--out-ref", default=DEFAULT_REF)
    ap.add_argument("--out-shake", default=DEFAULT_SHAKE)
    ap.add_argument("--no-proof", action="store_true")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--check", default=None)
    a = ap.parse_args(argv)
    if a.list:
        print("\n".join(SCENES))
        return 0
    if a.check:
        return check_folder(a.check)
    names = list(SCENES) if a.scene == "all" else a.scene.split(",")
    unknown = [n for n in names if n not in SCENES]
    if unknown:
        print("모르는 장면:", unknown)
        return 2
    replays, refs, shakes = {}, {}, {}
    for name in names:
        replay = SCENES[name]()
        problems = check_add1(replay)
        if problems:
            print(name, "형식 문제:", problems)
            return 1
        write_replay(replay, a.out_replays)
        replays[name] = replay
        refs[name] = execute_add1(replay, a.out_ref)
        shakes[name] = execute_add1(replay, a.out_shake, shake=True)
        info = dict(name=name, computes=refs[name]["steps"], dumps=[os.path.basename(p) for p in refs[name]["dumps"]], hash=refs[name]["hash"],
                    last_R=refs[name]["last_R"], changes=refs[name]["changes"], step4=refs[name].get("step4"))
        print(json.dumps(info, ensure_ascii=False))
    if a.no_proof or set(names) != set(SCENES):
        return 0
    proof = prove(replays, refs, shakes)
    proof["code"] = code_fingerprint()
    path = os.path.join(a.out_ref, "proof.json")
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(proof, fh, ensure_ascii=False, indent=1)
        fh.write("\n")
    print(json.dumps(proof, ensure_ascii=False, indent=1))
    print(f"증명 결과: {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
