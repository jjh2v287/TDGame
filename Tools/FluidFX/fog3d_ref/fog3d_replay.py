"""
실행 환경: Python 3.12 + numpy 2.2 + scipy 1.17 (추가 설치 없음), 저장소 루트에서 실행. fog3d_core.py·fog3d_gates.py 와 같은 폴더
실행 명령: python Tools/FluidFX/fog3d_ref/fog3d_replay.py --scene all --out-replays Tools/FluidFX/fog3d_ref/replays --out-ref Saved/FluidFX/Fog3D/ref [--jobs 8] [--no-ref] [--list] [--check <폴더>] [--sensitivity --out-shake <폴더>]
출력: 장면 재생 파일 replays/<장면>.json(형식 fog3d-replay-1), 기준 덤프 ref/<장면>__<덤프>.npz, --sensitivity 는 1e-4cm 흔든 덤프, --check 는 형식 검사 결과(문제 있으면 종료 코드 1)
상태: 3D 볼륨 안개 1단계 1a 통과 기준 초안 3판의 모든 장면을 기준 모의(fog3d_core.py)로 내보낸다. 2026-11-13 X2b 작성
"""
import argparse
import concurrent.futures
import hashlib
import json
import math
import os
import re
import sys
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fog3d_core as core  # noqa: E402

F32 = np.float32
ROOT = core.ROOT
FORMAT = "fog3d-replay-1"
DUMP_FORMAT = "fog3d-dump-1"
HX = 15.625
LAYER_CM = 30.0
LAYERS = 8
DUMP_FIELDS = ("u", "v", "w", "dx", "dy", "dz", "g", "pressure", "divergence", "fluid")
NAME_RE = re.compile(r"^[A-Za-z0-9_\-]+$")
FAR = 1.0e5
THIN_HALF = 5.0
MAX_SPHERES = 16
MAX_OBSTACLES = 32
DT_CAP = 1.0 / 20.0
SENSITIVITY_SHAKE_CM = 1.0e-4
SENSITIVITY_SEED = 20261113
TUNING = dict(wind_relax_per_sec=0.75, disp_relax_h_per_sec=0.1, disp_relax_v_per_sec=0.3, fill_relax_per_sec=0.3, band_cells=8.0,
              band_rate_per_sec=4.0, pressure_sweeps=8, pressure_omega=1.5, move_cap_h_cm=125.0, move_cap_v_cm=120.0,
              inflate_h_cm=7.9, inflate_v_extra_cm=7.2)
DEFAULT_REPLAYS = os.path.join(HERE, "replays")
DEFAULT_REF = os.path.join(ROOT, "Saved", "FluidFX", "Fog3D", "ref")


def steps_for(dt, seconds):
    return max(1, int(math.ceil(seconds / dt - 1e-9)))


def sphere_dict(s):
    return dict(a_cm=[float(c) for c in s.a], b_cm=[float(c) for c in s.b], r_cm=float(s.radius), v_cm_s=[float(c) for c in s.velocity],
                s=float(s.strength), e_per_sec=float(s.erase_per_sec))


def box_dict(center, half, yaw_deg=0.0):
    return dict(kind="box", center_cm=[float(c) for c in center], half_cm=[float(c) for c in half], yaw_deg=float(yaw_deg))


def box_from_corners(lo, hi):
    lo, hi = np.asarray(lo, float), np.asarray(hi, float)
    return box_dict((lo + hi) / 2.0, (hi - lo) / 2.0)


def code_fingerprint():
    out = {}
    for name in ("fog3d_core.py", "fog3d_replay.py", "tuned_solvers.json"):
        with open(os.path.join(HERE, name), "rb") as fh:
            out[name] = hashlib.sha256(fh.read()).hexdigest()[:16]
    return out


def follow_window(origin, anchor, target, n, margin=12, step=16):
    tc = (anchor[0] + int(math.floor(target[0] / HX)), anchor[1] + int(math.floor(target[1] / HX)))
    new = list(origin)
    for axis in (0, 1):
        if abs(tc[axis] - (origin[axis] + n // 2)) > margin:
            new[axis] = int(math.floor((tc[axis] - n // 2) / step + 0.5)) * step
    return tuple(new)


def default_window(anchor, n):
    return (core._floor_to(anchor[0] - n // 2, 16), core._floor_to(anchor[1] - n // 2, 16))


# ---------------- 재생 파일 만들기 ----------------

class Replay:
    def __init__(self, name, n=128, mode=2, anchor=(0, 0), window_min=None, obstacles=(), measure=None):
        self.name = name
        self.n = n
        self.window = tuple(window_min) if window_min is not None else default_window(anchor, n)
        self.body = dict(format=FORMAT, name=name, grid=dict(n=n, layers=LAYERS, cell_cm=HX, layer_cm=LAYER_CM), mode=mode, tuning=dict(TUNING),
                         anchor_cell=[int(anchor[0]), int(anchor[1])], obstacles=list(obstacles))
        if measure is not None:
            self.body["measure"] = measure
        self.steps = []

    def step(self, dt, spheres=(), reset=False, pressure=True, relax=True, band=True, wind=None, ops=(), dump=None, dump_fields=None,
             window=None, repeat=1):
        prev = self.window
        if window is not None:
            self.window = tuple(window)
        entry = dict(dt=dt, window_min=list(self.window), prev_window_min=list(prev), reset=bool(reset),
                     knobs=dict(pressure=bool(pressure), relax=bool(relax), band=bool(band)), wind_override=wind, state_ops=list(ops),
                     spheres=[sphere_dict(s) if hasattr(s, "radius") else s for s in spheres], dump=dump)
        if dump_fields is not None:
            entry["dump_fields"] = list(dump_fields)
        if repeat != 1:
            entry["repeat"] = int(repeat)
        self.steps.append(entry)

    def idle(self, dt, count, dump=None, dump_fields=None, **knobs):
        if count <= 0:
            return
        if dump is None:
            self.step(dt, repeat=count, **knobs)
            return
        if count > 1:
            self.step(dt, repeat=count - 1, **knobs)
        self.step(dt, dump=dump, dump_fields=dump_fields, **knobs)

    def finish(self):
        out = dict(self.body)
        out["source"] = dict(tool="fog3d_replay.py", code=code_fingerprint())
        out["steps"] = self.steps
        return out


def dumps_text(replay):
    head = {k: v for k, v in replay.items() if k != "steps"}
    text = json.dumps(head, separators=(",", ":"), ensure_ascii=False)
    lines = [json.dumps(s, separators=(",", ":"), ensure_ascii=False) for s in replay["steps"]]
    return text[:-1] + ',"steps":[\n' + ",\n".join(lines) + "\n]}\n"


# ---------------- 영역 ----------------

def region_mask(sim, reg):
    nx, ny, nz = sim.nx, sim.ny, sim.nz
    rx, ry, zc = sim.center_rel()
    X, Y, Z = rx[:, None, None], ry[None, :, None], zc[None, None, :]
    kind = reg["kind"]
    full = (nx, ny, nz)
    if kind == "all":
        return np.ones(full, bool)
    if kind == "aabb":
        lo, hi = reg["min_cm"], reg["max_cm"]
        m = (X >= lo[0]) & (X <= hi[0]) & (Y >= lo[1]) & (Y <= hi[1]) & (Z >= lo[2]) & (Z <= hi[2])
        return np.broadcast_to(m, full).copy()
    if kind in ("halfspace", "slab"):
        p, n = reg["point_cm"], reg["normal"]
        s = (X - p[0]) * n[0] + (Y - p[1]) * n[1] + (Z - p[2]) * n[2]
        m = (s > 0) if kind == "halfspace" else ((s > reg["d_min_cm"]) & (s <= reg["d_max_cm"]))
        m = m & (Z >= reg["z_min_cm"]) & (Z <= reg["z_max_cm"])
        return np.broadcast_to(m, full).copy()
    if kind == "cell_box":
        m = np.zeros(full, bool)
        m[reg["i0"]:reg["i1"] + 1, reg["j0"]:reg["j1"] + 1, reg["k0"]:reg["k1"] + 1] = True
        return m
    if kind == "not":
        return ~region_mask(sim, reg["region"])
    if kind == "union":
        m = np.zeros(full, bool)
        for sub in reg["regions"]:
            m |= region_mask(sim, sub)
        return m
    raise ValueError(f"모르는 영역 {kind}")


def measure_mask(sim, measure):
    m = sim.fluid.copy()
    margin = int(measure.get("inner_margin_cells", 0))
    if margin:
        inner = np.zeros((sim.nx, sim.ny), bool)
        inner[margin:sim.nx - margin, margin:sim.ny - margin] = True
        m &= inner[:, :, None]
    for reg in measure["regions"]:
        m &= region_mask(sim, reg)
    return m


def half(point, normal, z_min=-1.0e9, z_max=1.0e9):
    return dict(kind="halfspace", point_cm=[float(c) for c in point], normal=[float(c) for c in normal], z_min_cm=z_min, z_max_cm=z_max)


def slab(point, normal, d_min, d_max, z_min=-1.0e9, z_max=1.0e9):
    return dict(kind="slab", point_cm=[float(c) for c in point], normal=[float(c) for c in normal], d_min_cm=d_min, d_max_cm=d_max,
                z_min_cm=z_min, z_max_cm=z_max)


def op(kind, region):
    return dict(op=kind, region=region)


# ---------------- 실행기 ----------------

def build_params(replay, dt):
    t, g = replay["tuning"], replay["grid"]
    if g["layers"] != LAYERS or g["layer_cm"] != LAYER_CM or g["cell_cm"] != HX:
        raise ValueError("1단계는 8층·30cm·15.625cm 만 지원")
    if t["move_cap_h_cm"] != core.PATH_CAP_H_CM or t["move_cap_v_cm"] != core.PATH_CAP_V_CM:
        raise ValueError("이동 상한이 기준 모의 상수와 다름")
    if replay["mode"] not in (1, 2):
        raise ValueError("mode 는 1 또는 2")
    return core.Params(nx=g["n"], ny=g["n"], layers="U8", hx=HX, hy=HX, dt=dt, wind_relax_per_sec=t["wind_relax_per_sec"],
                       disp_relax_h_per_sec=t["disp_relax_h_per_sec"], disp_relax_v_per_sec=t["disp_relax_v_per_sec"],
                       fill_relax_per_sec=t["fill_relax_per_sec"], band_cells=t["band_cells"], band_rate_per_sec=t["band_rate_per_sec"],
                       mode="nowind" if replay["mode"] == 1 else "full",
                       solver_cfg=dict(kind="line", sweeps=t["pressure_sweeps"], omega=t["pressure_omega"]))


def make_sim(replay):
    first = replay["steps"][0]
    p = build_params(replay, first["dt"])
    obstacles = [dict(kind=o["kind"], center=o["center_cm"], half=o["half_cm"], yaw_deg=o["yaw_deg"]) for o in replay["obstacles"]]
    return core.Sim(p, anchor=tuple(replay["anchor_cell"]), window_min=tuple(first["prev_window_min"]), obstacles=obstacles)


def apply_knobs(sim, replay, step):
    p, t, k = sim.p, replay["tuning"], step["knobs"]
    p.pressure = bool(k["pressure"])
    p.band_on = bool(k["band"])
    scale = 1.0 if k["relax"] else 0.0
    p.wind_relax_per_sec = t["wind_relax_per_sec"] * scale
    p.disp_relax_h_per_sec = t["disp_relax_h_per_sec"] * scale
    p.disp_relax_v_per_sec = t["disp_relax_v_per_sec"] * scale
    p.fill_relax_per_sec = t["fill_relax_per_sec"] * scale
    p.diff_relax_per_sec = 0.3 * scale
    if p.dt != step["dt"]:
        p.dt = step["dt"]
        sim._geometry()


def apply_wind(sim, wind):
    if wind is None:
        return
    kind = wind["kind"]
    if kind == "uniform":
        sim.set_wind(tuple(wind["v_cm_s"]))
        return
    if kind == "vertical_osc":
        sim.set_wind((0.0, 0.0, wind["amp_cm_s"] * math.sin(2.0 * math.pi * wind["t_s"] / wind["period_s"])))
        return
    if kind == "vortex":
        big_u, big_l, sign = wind["u_cm_s"], wind["half_period_cm"] * 1.0, wind["sign"]
        xc = sim.xc - sim.nx // 2 * HX
        xf = sim.xf - sim.nx // 2 * HX
        u0 = (big_u * np.sin(np.pi * xf / big_l))[:, None, None] * np.cos(np.pi * sim.zc / big_l)[None, None, :]
        w0 = (-big_u * np.cos(np.pi * xc / big_l))[:, None, None] * np.sin(np.pi * sim.zt / big_l)[None, None, :]
        sim.u = (sign * np.broadcast_to(u0, sim.u.shape)).astype(F32)
        sim.v = np.zeros_like(sim.v)
        sim.w = (sign * np.broadcast_to(w0, sim.w.shape)).astype(F32)
        sim.zero_closed_faces()
        return
    raise ValueError(f"모르는 바람 덮어쓰기 {kind}")


def apply_state_ops(sim, ops):
    for item in ops:
        mask = region_mask(sim, item["region"]) & sim.fluid
        if item["op"] == "set_empty":
            sim.set_empty(mask)
            continue
        if item["op"] == "set_rest":
            for name in ("dpx", "dpy", "dpz", "g"):
                setattr(sim, name, np.where(mask, 0.0, getattr(sim, name)).astype(F32))
            sim.refresh_diff()
            continue
        raise ValueError(f"모르는 상태 조작 {item['op']}")


def reset_state(sim):
    sim.reset_fields()
    sim.pr[...] = 0
    sim.refresh_diff()


def shaken_spheres(spheres, rng):
    out = []
    for s in spheres:
        s = dict(s)
        for key in ("a_cm", "b_cm"):
            s[key] = [c + float(rng.choice([-1.0, 1.0])) * SENSITIVITY_SHAKE_CM for c in s[key]]
        out.append(s)
    return out


def core_spheres(spheres):
    return [core.SweptSphere(tuple(s["a_cm"]), tuple(s["b_cm"]), s["r_cm"], tuple(s["v_cm_s"]), s["s"], s["e_per_sec"]) for s in spheres]


def collect_fields(sim, div_before, names):
    zero = np.zeros(sim.u.shape, F32)
    full = dict(u=sim.u, v=sim.v, w=sim.w, dx=sim.dpx, dy=sim.dpy, dz=sim.dpz, g=sim.g, pressure=sim.pr,
                divergence=zero if div_before is None else div_before, fluid=sim.fluid.astype(F32))
    return {n: np.ascontiguousarray(full[n], F32) for n in names}


def execute(replay, out_dir=None, shake=False, stop_after_dump=None, log=None):
    sim = make_sim(replay)
    rng = np.random.default_rng(SENSITIVITY_SEED)
    captured = {}
    original_project = sim._project

    def project_and_capture():
        captured["div"] = sim.divergence().astype(F32)
        original_project()

    sim._project = project_and_capture
    done = 0
    written = []
    for step in replay["steps"]:
        for repeat_index in range(int(step.get("repeat", 1))):
            last = repeat_index == int(step.get("repeat", 1)) - 1
            if tuple(step["window_min"]) != sim.origin:
                sim.set_window(tuple(step["window_min"]))
            apply_knobs(sim, replay, step)
            if step["reset"] and repeat_index == 0:
                reset_state(sim)
            if repeat_index == 0:
                apply_state_ops(sim, step["state_ops"])
            apply_wind(sim, step["wind_override"])
            spheres = step["spheres"]
            if shake:
                spheres = shaken_spheres(spheres, rng)
            captured.pop("div", None)
            sim.step(core_spheres(spheres))
            done += 1
            if not (last and step["dump"]):
                continue
            names = step.get("dump_fields") or DUMP_FIELDS
            arrays = collect_fields(sim, captured.get("div"), names)
            meta = dict(format=DUMP_FORMAT, scene=replay["name"], dump=step["dump"], n=sim.nx, layers=sim.nz, window_min=list(sim.origin),
                        step_index=done, dt=step["dt"], fields=list(names))
            if "measure" in replay:
                arrays["measure_mask"] = measure_mask(sim, replay["measure"])
            if out_dir:
                os.makedirs(out_dir, exist_ok=True)
                path = os.path.join(out_dir, f"{replay['name']}__{step['dump']}.npz")
                np.savez_compressed(path, meta=np.array(json.dumps(meta)), **arrays)
                written.append(path)
            if stop_after_dump and step["dump"] == stop_after_dump:
                return dict(steps=done, dumps=written, hash=sim.state_hash())
    return dict(steps=done, dumps=written, hash=sim.state_hash(), last_R=sim.last.get("R"))


# ---------------- 형식 검사 ----------------

def check_replay(r):
    problems = []

    def need(cond, text):
        if not cond:
            problems.append(text)

    for key in ("format", "name", "source", "grid", "mode", "tuning", "anchor_cell", "obstacles", "steps"):
        need(key in r, f"최상위 필드 없음: {key}")
    if problems:
        return problems
    need(r["format"] == FORMAT, "format 이 다름")
    need(bool(NAME_RE.match(str(r["name"]))), "이름에 쓸 수 없는 문자")
    need(set(r["source"]) >= {"tool", "code"}, "source 에 tool·code 가 없음")
    g = r["grid"]
    need(all(k in g for k in ("n", "layers", "cell_cm", "layer_cm")), "grid 필드 누락")
    need(g.get("n", 0) in (128, 256), "n 은 128 또는 256")
    need(r["mode"] in (0, 1, 2), "mode 는 0·1·2")
    need(all(k in r["tuning"] for k in TUNING), "tuning 필드 누락")
    need(len(r["anchor_cell"]) == 2 and all(isinstance(c, int) for c in r["anchor_cell"]), "anchor_cell 은 정수 둘")
    need(len(r["obstacles"]) <= MAX_OBSTACLES, f"물체가 {MAX_OBSTACLES} 개를 넘음")
    for o in r["obstacles"]:
        need(o.get("kind") == "box" and len(o.get("center_cm", [])) == 3 and len(o.get("half_cm", [])) == 3 and "yaw_deg" in o, "물체 필드 오류")
    need(len(r["steps"]) > 0, "steps 가 비어 있음")
    names, prev_window = set(), None
    for index, s in enumerate(r["steps"]):
        tag = f"steps[{index}]"
        for key in ("dt", "window_min", "prev_window_min", "reset", "knobs", "wind_override", "state_ops", "spheres", "dump"):
            need(key in s, f"{tag} 필드 없음: {key}")
        if any(key not in s for key in ("dt", "window_min", "prev_window_min", "knobs", "spheres", "state_ops")):
            continue
        need(0.0 < s["dt"] <= DT_CAP + 1e-12, f"{tag} dt 범위")
        for key in ("window_min", "prev_window_min"):
            need(len(s[key]) == 2 and all(isinstance(c, int) and c % 16 == 0 for c in s[key]), f"{tag} {key} 는 16의 배수 정수 둘")
        if prev_window is not None:
            need(list(prev_window) == list(s["prev_window_min"]), f"{tag} prev_window_min 이 앞 계산의 window_min 과 다름")
        prev_window = s["window_min"]
        need(set(s["knobs"]) == {"pressure", "relax", "band"}, f"{tag} knobs 필드")
        need(len(s["spheres"]) <= MAX_SPHERES, f"{tag} 입력이 {MAX_SPHERES} 개를 넘음")
        for sp in s["spheres"]:
            need(set(sp) == {"a_cm", "b_cm", "r_cm", "v_cm_s", "s", "e_per_sec"} and sp["r_cm"] > 0, f"{tag} 입력 필드")
        wind = s.get("wind_override")
        need(wind is None or wind.get("kind") in ("uniform", "vertical_osc", "vortex"), f"{tag} wind_override 종류")
        for item in s["state_ops"]:
            need(item.get("op") in ("set_rest", "set_empty") and "region" in item, f"{tag} state_ops 항목")
        need(int(s.get("repeat", 1)) >= 1, f"{tag} repeat")
        if s.get("repeat", 1) > 1:
            need(not s["reset"] and not s["state_ops"], f"{tag} 되풀이 계산에는 reset·state_ops 를 둘 수 없음")
        d = s.get("dump")
        if d:
            need(bool(NAME_RE.match(str(d))) and d not in names, f"{tag} 덤프 이름 오류 또는 중복: {d}")
            names.add(d)
            need(all(f in DUMP_FIELDS for f in s.get("dump_fields", DUMP_FIELDS)), f"{tag} dump_fields 이름")
    need(r["steps"][0]["window_min"] == r["steps"][0]["prev_window_min"], "첫 계산은 창을 옮기지 않아야 함")
    return problems


# ---------------- 장면 정의 ----------------

def add_swing(r, dt, z, end_time, erase, dump_end="end", **knobs):
    x = -75.0
    n_in, total = steps_for(dt, 150.0 / 800.0), steps_for(dt, end_time)
    for s in range(total):
        spheres = []
        if s < n_in:
            x_new = min(x + 800.0 * dt, 75.0)
            spheres = core.capsule_spheres((x, -50, z), (x, 50, z), (x_new, -50, z), (x_new, 50, z), 20.0, velocity=(800.0, 0, 0),
                                           erase_per_sec=erase)
            x = x_new
        r.step(dt, spheres, dump=dump_end if s == total - 1 else None, **knobs)


def scene_downstrike():
    r = Replay("k1_downstrike")
    dt, z = 1.0 / 60.0, 125.0
    n_in, total = steps_for(dt, 0.15), steps_for(dt, 0.5)
    for s in range(total):
        spheres = []
        if s < n_in:
            z_new = max(z - 500.0 * dt, 50.0)
            spheres = [core.SweptSphere((0.0, 0.0, z), (0.0, 0.0, z_new), 50.0, (0.0, 0.0, -500.0))]
            z = z_new
        r.step(dt, spheres, dump="t0p5" if s == total - 1 else None)
    r.idle(dt, 600, dump="t10p5")
    return r


def scene_swing_low(name="k2_swing_low", dt=1.0 / 60.0, n=128, mode=2, anchor=(0, 0), window_min=None):
    r = Replay(name, n=n, mode=mode, anchor=anchor, window_min=window_min)
    add_swing(r, dt, 45.0, 0.3, 30.0)
    return r


def scene_box_high():
    r = Replay("k3_box_high", obstacles=[box_from_corners((0.0, -75.0, 0.0), (150.0, 75.0, 180.0))])
    dt = 1.0 / 60.0
    spheres = core.capsule_spheres((-150, 0, 20), (-150, 0, 160), (-150, 0, 20), (-150, 0, 160), 40.0, velocity=(400.0, 0, 0))
    for k in range(1, 10):
        r.step(dt, spheres, repeat=6, dump=f"s{k:02d}", dump_fields=["u", "v", "w", "fluid"])
    r.step(dt, spheres, repeat=6, dump="end")
    return r


RUN_X0 = -600.0
RUN_SPEED = 600.0
RUN_OBJECTS = [box_dict((850.0, -100.0, 30.0), (50.0, 50.0, 30.0)), box_dict((1000.0, 50.0, 100.0), (5.0, 150.0, 100.0))]


def scene_run(name="k4_run", mode=2):
    anchor = (0, 0)
    tc = int(math.floor(RUN_X0 / HX))
    window = (int(math.floor((tc - 64) / 16 + 0.5)) * 16, core._floor_to(-64, 16))
    r = Replay(name, mode=mode, anchor=anchor, window_min=window, obstacles=RUN_OBJECTS)
    dt = 1.0 / 60.0
    total = steps_for(dt, 2.0)
    x = RUN_X0
    moved = 0
    for s in range(total):
        x_new = x + RUN_SPEED * dt
        spheres = [core.SweptSphere((x, 0.0, 60.0), (x_new, 0.0, 60.0), 40.0, (RUN_SPEED, 0.0, 0.0), 1.0, 30.0)]
        new_window = follow_window(r.window, anchor, (x_new, 0.0), 128)
        sample = (s + 1) % 12 == 0 and s + 1 < total
        if new_window != r.window:
            moved += 1
            dump, fields = f"mv{moved}", None
        elif sample:
            dump, fields = f"s{(s + 1) // 12:02d}", ["u", "v", "w", "fluid"]
        else:
            dump, fields = None, None
        if s == total - 1:
            dump, fields = "end", None
        r.step(dt, spheres, window=new_window, dump=dump, dump_fields=fields)
        x = x_new
    return r


def scene_thin_swing():
    r = Replay("k7_thin_swing", obstacles=[box_dict((120.0, 0.0, 100.0), (THIN_HALF, 150.0, 100.0))])
    add_swing(r, 1.0 / 60.0, 45.0, 0.3, 30.0)
    return r


def scene_seam(name, corner):
    r = Replay(name)
    dt = 1.0 / 60.0
    ox = (r.body["anchor_cell"][0] - r.window[0]) * HX
    oy = (r.body["anchor_cell"][1] - r.window[1]) * HX
    center = (4 * HX - ox, (4 * HX - oy) if corner else 0.0, 75.0)
    sphere = core.still_sphere(center, 50.0, (-500.0, 0, 0))
    r.step(dt, [sphere], dump="s1", dump_fields=["u", "v", "w", "dx", "dy", "dz", "g", "fluid"])
    r.step(dt, [sphere])
    r.step(dt, [sphere], dump="s3", dump_fields=["u", "v", "w", "dx", "dy", "dz", "g", "fluid"])
    r.idle(dt, steps_for(dt, 1.0), dump="end")
    return r


def scene_still_600():
    r = Replay("still_600", obstacles=[box_dict((100.0, -100.0, 45.0), (50.0, 50.0, 45.0)), box_dict((-150.0, 0.0, 100.0), (THIN_HALF, 150.0, 100.0))])
    dt = 1.0 / 60.0
    plan = [(1, 0)] * 10 + [(-1, 0)] * 10 + [(0, 1)] * 8 + [(0, -1)] * 7
    move_steps = {16 * (k + 1): plan[k] for k in range(35)}
    run = 0
    for s in range(600):
        window = None
        if s in move_steps:
            dx, dy = move_steps[s]
            window = (r.window[0] + 16 * dx, r.window[1] + 16 * dy)
        if window is None:
            run += 1
            continue
        r.idle(dt, run)
        run = 0
        r.step(dt, window=window)
    r.idle(dt, run, dump="end")
    return r


def scene_mixed_dt():
    r = Replay("mixed_dt_120", obstacles=[box_dict((100.0, -100.0, 45.0), (50.0, 50.0, 45.0)), box_dict((-150.0, 0.0, 100.0), (THIN_HALF, 150.0, 100.0))])
    cycle = (60, 30, 45, 20)
    z, x = 125.0, -75.0
    for s in range(120):
        dt = 1.0 / cycle[s % 4]
        spheres, window = [], None
        if s < 9:
            z_new = max(z - 500.0 * dt, 50.0)
            spheres = [core.SweptSphere((0.0, 0.0, z), (0.0, 0.0, z_new), 50.0, (0.0, 0.0, -500.0))]
            z = z_new
        elif 20 <= s < 50 and x < 75.0:
            x_new = min(x + 800.0 * dt, 75.0)
            spheres = core.capsule_spheres((x, -50, 45), (x, 50, 45), (x_new, -50, 45), (x_new, 50, 45), 20.0, velocity=(800.0, 0, 0), erase_per_sec=30.0)
            x = x_new
        if s == 50:
            window = (r.window[0] + 16, r.window[1])
        if s == 90:
            window = (r.window[0], r.window[1] + 16)
        r.step(dt, spheres, window=window, dump="end" if s == 119 else None)
    return r


def scene_far_origin(name, window_min):
    anchor = (window_min[0] + 64, window_min[1] + 64)
    return scene_swing_low(name, anchor=anchor, window_min=window_min)


FIRST_SOLVE_BOX = box_from_corners(core.T3_BOX[0], core.T3_BOX[1])


def scene_first_solve(index):
    z = {1: 75.0, 2: 120.0, 3: 75.0, 4: 75.0, 5: 120.0}[index]
    vel = {1: (500.0, 0.0, 0.0), 2: (0.0, 0.0, -500.0), 3: (500.0, 0.0, 0.0), 4: (-500.0, 0.0, 0.0), 5: (0.0, 0.0, -500.0)}[index]
    pos = {1: (0.0, 0.0), 2: (0.0, 0.0), 3: (0.0, 0.0), 4: (4 * HX - 1000.0, 0.0), 5: (4 * HX - 1000.0, 4 * HX - 1000.0)}[index]
    r = Replay(f"first_solve_t{index}", obstacles=[FIRST_SOLVE_BOX] if index == 3 else [])
    r.step(1.0 / 60.0, [core.still_sphere((pos[0], pos[1], z), 50.0, vel)], dump="end")
    return r


def scene_first_solve_moving():
    r = Replay("first_solve_moving_256", n=256)
    dt = 1.0 / 60.0
    x = -150.0
    for s in range(60):
        x_new = x + 300.0 * dt
        r.step(dt, [core.SweptSphere((x, 0.0, 75.0), (x_new, 0.0, 75.0), 50.0, (300.0, 0.0, 0.0))],
               dump=f"r{s + 1}" if s >= 50 else None, dump_fields=["u", "v", "w", "divergence", "fluid"] if s >= 50 else None)
        x = x_new
    return r


def scene_brush_height():
    r = Replay("brush_height")
    for k in range(35):
        h = 30.0 + 5.0 * k
        r.step(1.0 / 60.0, [core.still_sphere((0.0, 0.0, h), 20.0, (300.0, 0.0, 0.0))], reset=True, pressure=False, dump=f"h{k:02d}", dump_fields=["u"])
    return r


def scene_dwell(moving):
    r = Replay("dwell_moving" if moving else "dwell_still")
    dt = 1.0 / 60.0
    for k in range(35):
        h = 30.0 + 5.0 * k
        speed = 800.0 if moving else 0.0
        sphere = core.SweptSphere((0.0, 0.0, h), (speed * dt, 0.0, h), 20.0, (speed, 0.0, 0.0), 0.0, 30.0)
        r.step(dt, [sphere], reset=True, pressure=False, relax=False, band=False, dump=f"h{k:02d}", dump_fields=["g"])
    return r


ERASE_CASES = {"ga": (100.0, 1.0), "na": (800.0, 0.25), "da": (0.0, 0.5)}


def scene_erase(case, denominator):
    speed, duration = ERASE_CASES[case]
    dt = 1.0 / denominator
    r = Replay(f"erase_{case}_dt{denominator}")
    x = -speed * duration / 2.0
    total = int(math.ceil(duration / dt - 1e-9))
    for s in range(total):
        frac = min(1.0, (duration - s * dt) / dt)
        x_new = x + speed * dt * frac
        sphere = core.SweptSphere((x, 0.0, 45.0), (x_new, 0.0, 45.0), 20.0, (speed, 0.0, 0.0), 0.0, 30.0 * frac)
        x = x_new
        r.step(dt, [sphere], pressure=False, relax=False, band=False, dump="end" if s == total - 1 else None, dump_fields=["g", "fluid"] if s == total - 1 else None)
    return r


def scene_empty_hold():
    r = Replay("empty_hold")
    dt = 1.0 / 60.0
    total = steps_for(dt, 0.5)
    for s in range(total):
        wind = dict(kind="vertical_osc", amp_cm_s=100.0, period_s=0.25, t_s=s * dt)
        ops = [op("set_empty", dict(kind="all"))] if s == 0 else []
        r.step(dt, wind=wind, ops=ops, pressure=False, relax=False, band=False, dump="end" if s == total - 1 else None)
    return r


def scene_vortex():
    r = Replay("vortex_return")
    dt = 1.0 / 60.0
    total, n_half = steps_for(dt, 0.5), steps_for(dt, 0.25)
    for s in range(total):
        wind = dict(kind="vortex", u_cm_s=100.0, half_period_cm=240.0, sign=1.0 if s < n_half else -1.0)
        dump = "mid" if s == n_half - 1 else ("end" if s == total - 1 else None)
        r.step(dt, wind=wind, pressure=False, relax=False, band=False, dump=dump)
    return r


def dry_sim(replay_obj):
    return make_sim(replay_obj.finish() if isinstance(replay_obj, Replay) else replay_obj)


def leak_dry(r):
    probe = Replay(r.name, n=r.n, obstacles=r.body["obstacles"])
    probe.step(1.0 / 60.0)
    return make_sim(probe.finish())


def scene_leak_thick(case):
    import fog3d_gates as gates
    vec, h = gates.LEAK_CASES[case]
    r = Replay(f"leak_thick_{case}", obstacles=[box_from_corners((-75.0, -75.0, 0.0), (75.0, 75.0, h))])
    sim = leak_dry(r)
    si = np.where(sim.solid.any(axis=(1, 2)))[0]
    sj = np.where(sim.solid.any(axis=(0, 2)))[0]
    sk = np.where(sim.solid.any(axis=(0, 1)))[0]
    i0, i1, j0, j1, k1 = int(si.min()), int(si.max()), int(sj.min()), int(sj.max()), int(sk.max())
    keep_layers = [k for k in range(sim.nz) if float(sim.Bc[0, 0, k]) >= 0.1]
    boxes = []
    if case == "top100":
        boxes.append((i0 + 2, i1 - 2, j0 + 2, j1 - 2, k1 + 1, k1 + 3))
    else:
        boxes.append((i1 + 1, i1 + 3, j0 + 2, j1 - 2, 0, k1))
        if case == "d45":
            boxes.append((i0 + 2, i1 - 2, j1 + 1, j1 + 3, 0, k1))
    regions = []
    for (a0, a1, b0, b1, c0, c1) in boxes:
        ks = [k for k in range(c0, c1 + 1) if k in keep_layers]
        regions.append(dict(kind="cell_box", i0=a0, i1=a1, j0=b0, j1=b1, k0=min(ks), k1=max(ks)))
    r.body["measure"] = dict(inner_margin_cells=0, regions=[dict(kind="union", regions=regions)], kind="mean_fill_ratio")
    return finish_leak(r, 1.0 / 60.0, vec, [op("set_empty", dict(kind="all"))])


def finish_leak(r, dt, vec, ops):
    wind = dict(kind="uniform", v_cm_s=[float(c) for c in vec])
    r.step(dt)
    r.step(dt)
    r.step(dt)
    r.step(dt, wind=wind, ops=ops, pressure=False, dump="end")
    return r


def thin_ops(full_regions, empty_regions):
    return [op("set_rest", reg) for reg in full_regions] + [op("set_empty", reg) for reg in empty_regions]


def scene_leak_thin(kind, denominator):
    dt = 1.0 / denominator
    far = FAR
    if kind in ("x3_v1000", "x3_v3000"):
        speed = 1000.0 if kind.endswith("1000") else 3000.0
        r = Replay(f"leak_thin_{kind}_dt{denominator}", obstacles=[box_from_corners((3.0 - THIN_HALF, -far, -far), (3.0 + THIN_HALF, far, far))])
        plane = ((3.0, 0.0, 0.0), (1.0, 0.0, 0.0))
        vec = (speed, 0.0, 0.0)
        full, empty = [half(plane[0], (-1.0, 0.0, 0.0))], [half(plane[0], plane[1])]
        measure = [slab(plane[0], plane[1], 0.0, 156.25)]
    elif kind == "a45_normal_v3000":
        a, b = math.cos(math.radians(45.0)), math.sin(math.radians(45.0))
        r = Replay(f"leak_thin_{kind}_dt{denominator}", obstacles=[box_dict((0.0, 0.0, 0.0), (THIN_HALF, far, far), 45.0)])
        vec = (3000.0 * a, 3000.0 * b, 0.0)
        full, empty = [half((0, 0, 0), (-a, -b, 0.0))], [half((0, 0, 0), (a, b, 0.0))]
        measure = [slab((0, 0, 0), (a, b, 0.0), 0.0, 156.25)]
    elif kind == "up_v1000":
        r = Replay(f"leak_thin_{kind}_dt{denominator}", obstacles=[box_from_corners((-far, -far, 60.0), (far, far, 70.0))])
        vec = (0.0, 0.0, 1000.0)
        full, empty = [half((0, 0, 65.0), (0.0, 0.0, -1.0))], [half((0, 0, 65.0), (0.0, 0.0, 1.0))]
        measure = [half((0, 0, 65.0), (0.0, 0.0, 1.0))]
    elif kind == "corner_v300":
        r = Replay(f"leak_thin_{kind}_dt{denominator}", obstacles=[box_from_corners((-75.0, -75.0, 0.0), (75.0, 75.0, 180.0))])
        speed = 300.0 / math.sqrt(2.0)
        vec = (speed, speed, 0.0)
        sim = leak_dry(r)
        si = np.where(sim.solid.any(axis=(1, 2)))[0]
        sj = np.where(sim.solid.any(axis=(0, 2)))[0]
        sk = np.where(sim.solid.any(axis=(0, 1)))[0]
        full, empty = [], [dict(kind="all")]
        measure = [dict(kind="cell_box", i0=int(si.max()) + 1, i1=int(si.max()) + 4, j0=int(sj.max()) + 1, j1=int(sj.max()) + 4, k0=0, k1=int(sk.max()))]
    else:
        raise ValueError(kind)
    r.body["measure"] = dict(inner_margin_cells=16, regions=measure, kind="max_fill_ratio")
    return finish_leak(r, dt, vec, thin_ops(full, empty))


def scene_leak_stacked(denominator):
    dt = 1.0 / denominator
    angle, shift_low, shift_up, lam, zsign = 45.0, 11.05, 0.0, 0.99, 1.0
    a = math.radians(angle)
    n = (math.cos(a), math.sin(a))
    plates = [box_dict((shift_low * n[0], shift_low * n[1], 120.0 - 60.0), (THIN_HALF, FAR, 60.0), angle),
              box_dict((shift_up * n[0], shift_up * n[1], 120.0 + 60.0), (THIN_HALF, FAR, 60.0), angle)]
    r = Replay(f"leak_stacked45_l099_up_dt{denominator}", obstacles=plates)
    step_cm = (7.8125, 7.8125, 15.0)
    vec = (lam * step_cm[0] * denominator, lam * step_cm[1] * denominator, zsign * lam * step_cm[2] * denominator)
    low_pt, up_pt = (shift_low * n[0], shift_low * n[1], 0.0), (shift_up * n[0], shift_up * n[1], 0.0)
    nn, neg = (n[0], n[1], 0.0), (-n[0], -n[1], 0.0)
    full = [half(low_pt, neg, z_max=120.0), half(up_pt, neg, z_min=120.0)]
    empty = [half(low_pt, nn, z_max=120.0), half(up_pt, nn, z_min=120.0)]
    r.body["measure"] = dict(inner_margin_cells=16, regions=[dict(kind="union", regions=empty)], kind="max_fill_ratio")
    return finish_leak(r, dt, vec, thin_ops(full, empty))


ROOM_INSIDE = dict(kind="aabb", min_cm=[-60.0, -60.0, 0.0], max_cm=[60.0, 60.0, 114.0])


def scene_leak_room(move_name, move, denominator):
    dt = 1.0 / denominator
    mid, half_len = 70.3125, 65.3125
    boxes = []
    for s in (-1.0, 1.0):
        boxes.append(box_from_corners((s * mid - THIN_HALF, -half_len, 0.0), (s * mid + THIN_HALF, half_len, 120.0)))
        boxes.append(box_from_corners((-half_len, s * mid - THIN_HALF, 0.0), (half_len, s * mid + THIN_HALF, 120.0)))
    boxes.append(box_from_corners((-half_len, -half_len, 125.0), (half_len, half_len, 135.0)))
    r = Replay(f"leak_room_{move_name}_dt{denominator}", obstacles=boxes)
    vec = (-move[0] * HX / dt, -move[1] * HX / dt, -move[2] * LAYER_CM / dt)
    outside = dict(kind="not", region=ROOM_INSIDE)
    r.body["measure"] = dict(inner_margin_cells=16, regions=[outside], kind="max_fill_ratio")
    return finish_leak(r, dt, vec, [op("set_rest", ROOM_INSIDE), op("set_empty", outside)])


def scene_sealed_cells():
    def cell_center(i, j):
        return (i + 0.5) * HX, (j + 0.5) * HX

    obstacles = []
    cx, cy = cell_center(-16, 0)
    for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        x, y = cell_center(-16 + di, dj)
        obstacles.append(box_dict((x, y, 120.0), (7.8, 7.8, 120.0)))
    obstacles.append(box_dict((cx, cy, 225.0), (7.8, 7.8, 14.0)))
    x_lo, x_hi = cell_center(9, 0)[0], cell_center(12, 0)[0]
    y_lo, y_hi = cell_center(0, -1)[1], cell_center(0, 2)[1]
    y_mid = (cell_center(0, 0)[1] + cell_center(0, 1)[1]) / 2.0
    x_mid = (cell_center(10, 0)[0] + cell_center(11, 0)[0]) / 2.0
    obstacles.append(box_dict((x_lo, y_mid, 120.0), (7.8, 15.6, 120.0)))
    obstacles.append(box_dict((x_hi, y_mid, 120.0), (7.8, 15.6, 120.0)))
    obstacles.append(box_dict((x_mid, y_lo, 120.0), (15.6, 7.8, 120.0)))
    obstacles.append(box_dict((x_mid, y_hi, 120.0), (15.6, 7.8, 120.0)))
    obstacles.append(box_dict((x_mid, y_mid, 225.0), (15.6, 15.6, 14.0)))
    r = Replay("sealed_cells_600", obstacles=obstacles)
    dt = 1.0 / 60.0
    for block in range(10):
        for sign in (1.0, -1.0):
            r.step(dt, [core.still_sphere((-100.0, 8.0, 75.0), 50.0, (sign * 2000.0, 0.0, 0.0))], repeat=30)
    r.steps[-1]["dump"] = "end"
    r.steps[-1]["repeat"] = 30
    return r


def scene_mode1(name):
    if name == "k2_swing_low_mode1":
        r = scene_swing_low(name, mode=1)
        return r
    r = scene_run(name, mode=1)
    return r


SCENES = {}
SCENES["k1_downstrike"] = scene_downstrike
SCENES["k2_swing_low"] = lambda: scene_swing_low()
SCENES["k3_box_high"] = scene_box_high
SCENES["k4_run"] = scene_run
SCENES["k5_swing_dt30"] = lambda: scene_swing_low("k5_swing_dt30", dt=1.0 / 30.0)
SCENES["k6_swing_256"] = lambda: scene_swing_low("k6_swing_256", n=256)
SCENES["k7_thin_swing"] = scene_thin_swing
SCENES["k8_seam_west"] = lambda: scene_seam("k8_seam_west", False)
SCENES["still_600"] = scene_still_600
SCENES["mixed_dt_120"] = scene_mixed_dt
SCENES["far_origin_0"] = lambda: scene_far_origin("far_origin_0", (0, 0))
SCENES["far_origin_p1e6"] = lambda: scene_far_origin("far_origin_p1e6", (1000000, 1000000))
SCENES["far_origin_m1e6"] = lambda: scene_far_origin("far_origin_m1e6", (-1000000, -1000000))
SCENES["far_origin_p1e6_16"] = lambda: scene_far_origin("far_origin_p1e6_16", (1000016, 1000016))
for _i in range(1, 6):
    SCENES[f"first_solve_t{_i}"] = (lambda i=_i: scene_first_solve(i))
SCENES["first_solve_moving_256"] = scene_first_solve_moving
SCENES["brush_height"] = scene_brush_height
SCENES["dwell_still"] = lambda: scene_dwell(False)
SCENES["dwell_moving"] = lambda: scene_dwell(True)
for _case in ERASE_CASES:
    for _d in (60, 30, 144):
        SCENES[f"erase_{_case}_dt{_d}"] = (lambda c=_case, d=_d: scene_erase(c, d))
SCENES["empty_hold"] = scene_empty_hold
SCENES["vortex_return"] = scene_vortex
SCENES["seam_corner"] = lambda: scene_seam("seam_corner", True)
for _case in ("n50", "n300", "n1000", "d45", "top100"):
    SCENES[f"leak_thick_{_case}"] = (lambda c=_case: scene_leak_thick(c))
for _kind in ("x3_v1000", "x3_v3000", "a45_normal_v3000", "up_v1000", "corner_v300"):
    for _d in (60, 30):
        SCENES[f"leak_thin_{_kind}_dt{_d}"] = (lambda k=_kind, d=_d: scene_leak_thin(k, d))
for _d in (60, 30):
    SCENES[f"leak_stacked45_l099_up_dt{_d}"] = (lambda d=_d: scene_leak_stacked(d))
ROOM_MOVES = (("X8_s075_xp", (8.0, 0.75, 0.75)), ("sXs_s075_yp", (0.75, 8.0, 0.75)))
for _name, _move in ROOM_MOVES:
    for _d in (20, 30):
        SCENES[f"leak_room_{_name}_dt{_d}"] = (lambda n=_name, m=_move, d=_d: scene_leak_room(n, m, d))
SCENES["sealed_cells_600"] = scene_sealed_cells
SCENES["k2_swing_low_mode1"] = lambda: scene_mode1("k2_swing_low_mode1")
SCENES["k4_run_mode1"] = lambda: scene_mode1("k4_run_mode1")

SENSITIVITY_SCENES = {"k1_downstrike": "t0p5", "k2_swing_low": "end", "k3_box_high": "end", "k4_run": "end", "k5_swing_dt30": "end",
                      "k6_swing_256": "end", "k7_thin_swing": "end", "k8_seam_west": "end", "k2_swing_low_mode1": "end", "k4_run_mode1": "end"}


# ---------------- 실행 진입 ----------------

def run_one(job):
    name, out_replays, out_ref, write_ref, shake_dir = job
    started = time.time()
    replay = SCENES[name]().finish()
    problems = check_replay(replay)
    if problems:
        return dict(name=name, problems=problems)
    os.makedirs(out_replays, exist_ok=True)
    text = dumps_text(replay)
    with open(os.path.join(out_replays, f"{name}.json"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)
    info = dict(name=name, computes=sum(int(s.get("repeat", 1)) for s in replay["steps"]), dumps=[s["dump"] for s in replay["steps"] if s["dump"]],
                bytes=len(text.encode("utf-8")), problems=[])
    if shake_dir:
        stop = SENSITIVITY_SCENES[name]
        result = execute(replay, shake_dir, shake=True, stop_after_dump=stop)
        info["hash"] = result["hash"]
    elif write_ref:
        result = execute(replay, out_ref)
        info["hash"] = result["hash"]
        info["last_R"] = result.get("last_R")
    info["seconds"] = round(time.time() - started, 1)
    return info


def check_folder(folder):
    bad = 0
    for fname in sorted(os.listdir(folder)):
        if not fname.endswith(".json"):
            continue
        with open(os.path.join(folder, fname), encoding="utf-8") as fh:
            problems = check_replay(json.load(fh))
        if problems:
            bad += 1
            print(f"{fname}: " + "; ".join(problems))
    print(f"형식 검사 끝: 문제 있는 파일 {bad}개")
    return 1 if bad else 0


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser()
    ap.add_argument("--scene", default="all", help="all 또는 쉼표로 이은 장면 이름")
    ap.add_argument("--out-replays", default=DEFAULT_REPLAYS)
    ap.add_argument("--out-ref", default=DEFAULT_REF)
    ap.add_argument("--out-shake", default=os.path.join(ROOT, "Saved", "FluidFX", "Fog3D", "ref_shake"))
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--no-ref", action="store_true")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--check", default=None)
    ap.add_argument("--sensitivity", action="store_true")
    a = ap.parse_args(argv)
    if a.list:
        print("\n".join(SCENES))
        return 0
    if a.check:
        return check_folder(a.check)
    names = list(SCENES) if a.scene == "all" else a.scene.split(",")
    if a.sensitivity:
        names = [n for n in names if n in SENSITIVITY_SCENES]
    unknown = [n for n in names if n not in SCENES]
    if unknown:
        print("모르는 장면:", unknown)
        return 2
    jobs = [(n, a.out_replays, a.out_ref, not a.no_ref, a.out_shake if a.sensitivity else None) for n in names]
    failed = 0
    with concurrent.futures.ProcessPoolExecutor(max_workers=max(1, a.jobs)) as pool:
        for info in pool.map(run_one, jobs):
            if info.get("problems"):
                failed += 1
            print(json.dumps(info, ensure_ascii=False))
            sys.stdout.flush()
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
