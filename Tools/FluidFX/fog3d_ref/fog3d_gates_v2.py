"""
실행 환경: Python 3.12 + numpy 2.2 + scipy 1.17 (추가 설치 없음), 저장소 루트에서 실행. 같은 폴더의 fog3d_core.py·fog3d_core_v2.py·fog3d_gates.py 를 import 한다
실행 명령: python Tools/FluidFX/fog3d_ref/fog3d_gates_v2.py walk <px|mx|py|my|pxpy> --advect <A1|N1|N2> --out <json> / selfcheck --out <json> / <기존 장면 이름> [하위] --advect <A1|A3|N1|N2> --out <json>
출력: --out 의 JSON(기존 장면은 fog3d_gates.py 와 같은 꼴 + code_v2)
상태: 1d 바람 옮기기 후보 판정 실험(Docs/VolumeFog3D/stage1/prereg-1d-design.md 2절), 2026-11-14
"""
import argparse
import dataclasses
import hashlib
import json
import math
import os
import sys
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import fog3d_core as core  # noqa: E402
import fog3d_core_v2 as core2  # noqa: E402
import fog3d_gates as gates  # noqa: E402

gates.Sim = core2.Sim
OUT_DIR = os.path.join(core.ROOT, "Saved", "AgentOps", "20261114", "fog3d", "stage1d")
gates.OPS_DIR = OUT_DIR
gates.PIECE_DIR = os.path.join(OUT_DIR, "pieces")

DT = 1.0 / 60.0
SPEED = 520.0
WALK_STEPS = 70
STOP_STEPS = 78
HEIGHTS = np.arange(1.5, 240.0, 3.0)
DZ_COL = 3.0
BODY_WIND = 2.0
RIG = (((0.0, -12.0, 30.0), 45.0), ((0.0, 12.0, 30.0), 45.0), ((0.0, 0.0, 91.0), 60.0))
HALF = math.sqrt(0.5)
HEADINGS = {"px": (1.0, 0.0), "mx": (-1.0, 0.0), "py": (0.0, 1.0), "my": (0.0, -1.0), "pxpy": (HALF, HALF)}


def code_v2():
    out = {}
    for name in ("fog3d_core.py", "fog3d_core_v2.py", "fog3d_gates.py", "fog3d_gates_v2.py"):
        with open(os.path.join(HERE, name), "rb") as fh:
            out[name] = hashlib.sha256(fh.read()).hexdigest()[:16]
    return out


def walk_inputs(pos, prev, c, s, vel):
    out = []
    for (f, l, z), r in RIG:
        dx, dy = f * c - l * s, f * s + l * c
        out.append(core.SweptSphere((prev[0] + dx, prev[1] + dy, z), (pos[0] + dx, pos[1] + dy, z), r, vel, BODY_WIND, 0.0))
    return out


def column(sim):
    return sim.drawn_density_v3(HEIGHTS).sum(axis=2) * DZ_COL


def cmd_walk(direction, advect, out):
    c, s = HEADINGS[direction]
    sim = core2.Sim(core.Params(advect=advect, solid="S4" if advect == "A3" else "S9"))
    col0 = column(sim)
    start = np.array((-300.0 * c, -300.0 * s))
    pos = start.copy()
    t0 = time.time()
    for k in range(WALK_STEPS + STOP_STEPS):
        prev = pos.copy()
        if k < WALK_STEPS:
            pos = pos + np.array((c, s)) * SPEED * DT
            vel = (SPEED * c, SPEED * s, 0.0)
        else:
            vel = (0.0, 0.0, 0.0)
        sim.step(walk_inputs(pos, prev, c, s, vel))
    delta = column(sim) - col0
    rx, ry, _ = sim.center_rel()
    X, Y = np.meshgrid(rx, ry, indexing="ij")
    relx, rely = X - start[0], Y - start[1]
    along = relx * c + rely * s
    lateral = -relx * s + rely * c
    span = WALK_STEPS * SPEED * DT
    on = (along > 50.0) & (along < span - 50.0)
    left = on & (lateral < 0) & (lateral > -100.0)
    right = on & (lateral > 0) & (lateral < 100.0)
    a_left, a_right = float(np.abs(delta[left]).mean()), float(np.abs(delta[right]).mean())
    total = float(np.abs(delta[left | right]).mean())
    res = dict(name="walk", direction=direction, advect=advect, heading=(c, s), walk_steps=WALK_STEPS, stop_steps=STOP_STEPS,
               walk_cm=span, cells_left=int(left.sum()), cells_right=int(right.sum()), abs_left=a_left, abs_right=a_right,
               side_bias=abs(a_left - a_right) / (a_left + a_right), signed_bias_left_minus_right=(a_left - a_right) / (a_left + a_right),
               total_abs_mean=total, state_hash=sim.state_hash(), max_wind=float(sim.max_wind()), path=sim.path_stats(),
               face=sim.face_stats(), seconds=round(time.time() - t0, 1), code=code_v2())
    with open(out, "w", encoding="utf-8") as fh:
        json.dump(res, fh, ensure_ascii=False, indent=1)
    np.savez_compressed(os.path.splitext(out)[0] + ".npz", delta=delta.astype(np.float32), lateral=lateral.astype(np.float32), along=along.astype(np.float32))
    print(json.dumps({k: res[k] for k in ("direction", "advect", "side_bias", "total_abs_mean", "seconds")}))
    return 0


def random_wind(sim, seed, amp):
    rng = np.random.default_rng(seed)
    for name in ("u", "v", "w"):
        setattr(sim, name, (rng.standard_normal(sim.u.shape) * amp).astype(core.F32))
    sim.zero_closed_faces()


def thin_wall_obstacles():
    return [dict(kind="box", center=(40.0, 0.0, 60.0), half=(2.0, 300.0, 200.0), yaw_deg=0.0),
            dict(kind="box", center=(-120.0, 60.0, 30.0), half=(80.0, 2.0, 120.0), yaw_deg=30.0),
            dict(kind="sphere", center=(150.0, -150.0, 60.0), radius=40.0)]


def cmd_selfcheck(out):
    res = {}
    sim = core2.Sim(core.Params(nx=64, ny=64, advect="N2"), obstacles=thin_wall_obstacles())
    random_wind(sim, 1, 4000.0)
    vel = (sim.u.astype(np.float64), sim.v.astype(np.float64), sim.w.astype(np.float64))
    ref = sim._path_clamp(vel, sim.p.dt)
    ref_end = [c.copy() for c in sim._end_cell]
    shape = sim.u.shape
    start = [np.broadcast_to(sim.xc[:, None, None], shape), np.broadcast_to(sim.yc[None, :, None], shape), np.broadcast_to(sim.zc[None, None, :], shape)]
    move = [-vel[0] * sim.p.dt, -vel[1] * sim.p.dt, -vel[2] * sim.p.dt]
    scale = np.ones(shape)
    for a, cap in enumerate((core.PATH_CAP_H_CM, core.PATH_CAP_H_CM, core.PATH_CAP_V_CM)):
        size = np.abs(move[a])
        nz = size > 0
        scale = np.minimum(scale, np.where(nz, cap / np.where(nz, size, 1.0), 1.0))
    move = [m * scale for m in move]
    target = [np.broadcast_to(t, shape) for t in sim._clamp(start[0] + move[0], start[1] + move[1], start[2] + move[2])]
    cell = [np.broadcast_to(np.arange(sim.nx)[:, None, None], shape).astype(np.int64),
            np.broadcast_to(np.arange(sim.ny)[None, :, None], shape).astype(np.int64),
            np.broadcast_to(np.arange(sim.nz)[None, None, :], shape).astype(np.int64)]
    sample, end, _, _ = sim._walk(start, cell, target, move)
    res["walk_equals_core_path_clamp"] = bool(all(np.array_equal(a, b) for a, b in zip(sample, ref)) and
                                              all(np.array_equal(a, b) for a, b in zip(end, ref_end)))
    rows = {}
    for scene, obstacles in (("no_object", ()), ("objects", thin_wall_obstacles())):
        for seed in (2, 3):
            s2 = core2.Sim(core.Params(nx=64, ny=64, advect="N2", pressure=False), obstacles=obstacles)
            s2.prop_check = True
            random_wind(s2, seed, 1500.0)
            s2.g = (np.random.default_rng(seed + 10).standard_normal(s2.u.shape) * 0.3).astype(core.F32)
            s2._zero_solid_state()
            s3 = core2.Sim(core.Params(nx=64, ny=64, advect="A3", solid="S4", pressure=False), obstacles=obstacles)
            for name in ("u", "v", "w", "g"):
                setattr(s3, name, getattr(s2, name).copy())
            for _ in range(5):
                s2.step()
                s3.step()
            diffs = {name: float(np.abs(getattr(s2, name).astype(np.float64) - getattr(s3, name)).max()) for name in ("u", "v", "w", "dpx", "dpy", "dpz", "g")}
            rows[f"{scene}_seed{seed}"] = dict(max_abs_diff_vs_A3_S4=diffs, identical=bool(all(v == 0.0 for v in diffs.values())),
                                               face=s2.face_stats(), center_prop=getattr(s2, "prop_totals", None))
    res["n2_vs_a3_s4"] = rows
    s1 = core2.Sim(core.Params(nx=64, ny=64, advect="N1", pressure=False))
    sa = core2.Sim(core.Params(nx=64, ny=64, advect="A1", pressure=False))
    random_wind(s1, 4, 0.0)
    for name in ("u", "v", "w"):
        setattr(sa, name, getattr(s1, name).copy())
    s1.step()
    sa.step()
    res["n1_equals_a1_at_zero_wind"] = bool(s1.state_hash() == sa.state_hash())
    res["code"] = code_v2()
    with open(out, "w", encoding="utf-8") as fh:
        json.dump(res, fh, ensure_ascii=False, indent=1, default=lambda o: o.tolist() if hasattr(o, "tolist") else str(o))
    print(json.dumps(dict(walk_equals=res["walk_equals_core_path_clamp"], rows={k: (v["identical"], v["face"]["face_prop_other_component"]) for k, v in rows.items()},
                          n1_zero=res["n1_equals_a1_at_zero_wind"])))
    return 0


def run_gate(argv):
    advect = "A1"
    if "--advect" in argv:
        i = argv.index("--advect")
        advect = argv[i + 1]
        argv = argv[:i] + argv[i + 2:]
    if advect not in ("A1", "A2", "A3", "N1", "N2"):
        raise SystemExit("advect 는 A1|A2|A3|N1|N2")
    original = gates.cfg_from_args
    gates.cfg_from_args = lambda a: dataclasses.replace(original(a), advect=advect)
    code = gates.main(argv)
    out = argv[argv.index("--out") + 1] if "--out" in argv else None
    if out and os.path.exists(out):
        with open(out, encoding="utf-8") as fh:
            piece = json.load(fh)
        piece["script_v2"] = "Tools/FluidFX/fog3d_ref/fog3d_gates_v2.py"
        piece["code_v2"] = code_v2()
        with open(out, "w", encoding="utf-8") as fh:
            json.dump(piece, fh, ensure_ascii=False, indent=1)
    return code


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if argv and argv[0] in ("walk", "selfcheck"):
        ap = argparse.ArgumentParser()
        ap.add_argument("name")
        ap.add_argument("which", nargs="?")
        ap.add_argument("--advect", default="A1", choices=["A1", "A3", "N1", "N2"])
        ap.add_argument("--out", required=True)
        a = ap.parse_args(argv)
        os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
        if a.name == "walk":
            return cmd_walk(a.which, a.advect, a.out)
        return cmd_selfcheck(a.out)
    return run_gate(argv)


if __name__ == "__main__":
    import fog3d_gates_v2 as _module_v2
    sys.exit(_module_v2.main())
