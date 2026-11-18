"""
실행 환경: Python 3.12 + numpy 2.2 + scipy 1.17 (추가 설치 없음), 저장소 루트에서 실행. fog3d_core.py·tuned_solvers.json 과 같은 폴더
실행 명령: python Tools/FluidFX/fog3d_ref/fog3d_gates.py <장면> [하위] [--solid S8 --advect A3 --solver REF --delta v2 --no-inflate --no-pivot-guard --path-t64 --raw-hash] --out <json>
출력: 판정 조각 JSON(기본 Saved/AgentOps/fog3d_ref/pieces/, 있으면 건너뜀, --force 로 다시), 오래 걸리면 <out>.ckpt.pkl(종료 코드 3 = 같은 명령 다시), scene 은 <out 이름>.npz
상태: 3D 볼륨 안개 1단계 설계 판정 장면·지표(prereg-design ~ 2e), 2026-11-13 X2a 가 Tools/scratch/fog3d/stage1/r2/gates2.py(85bb3c00670e5423)에서 올림. 기본 구성 = 명세 v1 최종(U8·A1·S9·P2·v3·부풀리기)
"""
import argparse
import copy
import glob
import hashlib
import json
import math
import os
import pickle
import sys
import time
from dataclasses import dataclass, asdict

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fog3d_core as core  # noqa: E402
from fog3d_core import Sim, SweptSphere, still_sphere, capsule_spheres, F32  # noqa: E402

ROOT = core.ROOT
TUNED_PATH = os.path.join(HERE, "tuned_solvers.json")
core.TUNING_PATH = TUNED_PATH
OPS_DIR = os.path.join(ROOT, "Saved", "AgentOps", "fog3d_ref")
PIECE_DIR = os.path.join(OPS_DIR, "pieces")
EVIDENCE_DIR = os.path.join(ROOT, "Docs", "VolumeFog3D", "measurements", "stage1a-ref")
PREREG = {"prereg-design-2e.md": "0825d1c692d2f6b5", "prereg-design-2d.md": "cfa1740a5c01a728", "prereg-design-2c.md": "b882aaafe2cc8d0f", "prereg-design-2b.md": "25f6ea62b87734de", "prereg-design-2.md": "3a0ddd47b0f5b729",
          "prereg-design.md": "84c1f178cad78ebb"}


def code_fingerprints():
    out = {}
    for name in ("fog3d_core.py", "fog3d_gates.py", "tuned_solvers.json"):
        with open(os.path.join(HERE, name), "rb") as fh:
            out[name] = hashlib.sha256(fh.read()).hexdigest()[:16]
    return out
FORMULA = {"v1": 1, "v2": 2, "v3": 3}
HX = 15.625
TIME_SCALE = 1.0
CANDIDATES = ("P1", "P2", "P3", "P4", "P5", "P6")


@dataclass
class Cfg:
    layout: str = "U8"
    advect: str = "A1"
    solid: str = "S9"
    solver: str = "P2"
    delta: str = "v3"
    dt: int = 60
    n: int = 128
    wind_relax: float = 0.75
    mode: str = "full"
    patch: float = 0.0
    emu8: bool = False
    emu16: bool = False
    inflate: bool = True
    emu_gpu: bool = False
    pivot_guard: bool = True
    path_t32: bool = True
    canonical_hash: bool = True


def steps_for(dt, seconds):
    return max(1, int(math.ceil(seconds * TIME_SCALE / dt - 1e-9)))


def tuned_cfg(layout, solver):
    if solver not in CANDIDATES:
        return None
    try:
        with open(TUNED_PATH, encoding="utf-8") as fh:
            table = json.load(fh)
    except OSError:
        raise SystemExit(f"{TUNED_PATH} 없음: 먼저 'fog3d_gates.py tune' 을 돌린다")
    entry = table.get(layout, {}).get(solver)
    if not entry:
        raise SystemExit(f"tuned_solvers.json 에 {layout}/{solver} 없음")
    base = dict(core.SOLVER_DEFAULTS[solver])
    base.update({k: v for k, v in entry.items() if k in base})
    return base


def make_params(cfg, **over):
    p = core.Params(nx=cfg.n, ny=cfg.n, layers=cfg.layout, advect=cfg.advect, solid=cfg.solid, solver=cfg.solver,
                    diff_formula=FORMULA[cfg.delta], dt=1.0 / cfg.dt, wind_relax_per_sec=cfg.wind_relax,
                    mode="nowind" if cfg.mode == "b1" else "full", stain_amp=cfg.patch, weight8=cfg.emu8,
                    half_store=cfg.emu16, solver_cfg=tuned_cfg(cfg.layout, cfg.solver), inflate=cfg.inflate,
                    emu_gpu=cfg.emu_gpu, pivot_guard=cfg.pivot_guard, path_t32=cfg.path_t32,
                    canonical_hash=cfg.canonical_hash)
    for k, v in over.items():
        setattr(p, k, v)
    return p


def relax_off(cfg, band_off):
    """판정선의 "되돌림 끔". 2판: 차이값 되돌림 0. 3판(덧붙임 2절): d·g 되돌림 0 + 가장자리 띠 감쇠 끔."""
    over = dict(diff_relax_per_sec=0.0)
    if cfg.delta == "v3":
        over.update(disp_relax_h_per_sec=0.0, disp_relax_v_per_sec=0.0, fill_relax_per_sec=0.0, band_on=False)
    elif band_off:
        over["band_on"] = False
    return over


# ---------------- 측정 도구 ----------------

def geom(sim):
    rx, ry, zc = sim.center_rel()
    return rx, ry, zc, sim.dz


def cell_volume(sim):
    return (sim.p.hx * sim.p.hy * sim.dz)[None, None, :]


def layer_overlap(sim, z0, z1):
    return np.clip(np.minimum(sim.zt, z1) - np.maximum(sim.zb, z0), 0.0, None)


def max_center_speed(sim):
    cu, cv, cw = sim.center_velocity()
    return float(np.sqrt(cu * cu + cv * cv + cw * cw).max())


def faces_max(sim, mask_xy):
    m = mask_xy[:, :, None]
    return max(float(np.abs(np.where(m, a, 0)).max()) for a in (sim.u, sim.v, sim.w))


def v1_metrics(sim):
    rx, ry, zc, dz = geom(sim)
    cu, cv, _ = sim.center_velocity()
    r = np.hypot(rx[:, None], ry[None, :])
    rs = np.maximum(r, 1e-6)
    bottom = zc < 30.0
    wb = dz[bottom] / dz[bottom].sum()
    radial = (cu[:, :, bottom] * rx[:, None, None] + cv[:, :, bottom] * ry[None, :, None]) / rs[:, :, None]
    radial = (radial * wb[None, None, :]).sum(axis=2)
    ring = (r >= 75) & (r <= 150)
    out_wind = float(radial[ring].mean())
    col = r < 30
    low = zc < 90.0
    total = sim.total_density()[:, :, low] * dz[low]
    base = sim.Bc.astype(np.float64)[:, :, low] * dz[low]
    drop = 1.0 - float(total[col].sum()) / float(base[col].sum())
    col_sum = (sim.D.astype(np.float64) * dz[None, None, :]).sum(axis=2)
    means = []
    for b0 in np.arange(60.0, 400.0, HX):
        sel = (r >= b0) & (r < b0 + HX)
        if sel.any():
            means.append((float(b0 + HX / 2), float(col_sum[sel].mean())))
    best = max(means, key=lambda x: x[1])
    return dict(t=round(sim.time, 4), ring_outward_wind=out_wind, center_column_drop=drop, best_ring_radius=best[0],
                best_ring_column_diff=best[1], passed_1=out_wind > 0, passed_2=drop >= 0.10, passed_3=best[1] > 0,
                passed=bool(out_wind > 0 and drop >= 0.10 and best[1] > 0))


def v2_measure(sim):
    _, _, zc, dz = geom(sim)
    cu, cv, cw = sim.center_velocity()
    e = (cu * cu + cv * cv + cw * cw).sum(axis=(0, 1)) * dz
    absd = float((np.abs(sim.D.astype(np.float64)) * cell_volume(sim)).sum())
    return absd, float((e * zc).sum() / max(e.sum(), 1e-300))


def removed_amount(sim):
    return float((-sim.D.astype(np.float64) * cell_volume(sim) * sim.fluid).sum())


# ---------------- 장면 ----------------

class Scene:
    total = 0

    def __init__(self, sim):
        self.sim = sim
        if sim.p.solid in core.BLOCK_CONNECTED:
            sim.prop_check = True
        self.s = 0
        self.dt = sim.p.dt

    def finished(self):
        return self.s >= self.total

    def inputs(self):
        return []

    def follow(self):
        return None

    def before(self):
        pass

    def after(self):
        pass

    def advance(self):
        self.before()
        ins = self.inputs()
        self.sim.step(ins, self.follow())
        self.s += 1
        self.after()

    def result(self):
        out = dict(state_hash=self.sim.state_hash(), state_hash_raw=self.sim.state_hash(canonical=False),
                   t=round(self.sim.time, 5), steps=self.s)
        if getattr(self.sim, "tiny_pivot_count", None) is not None:
            out["tiny_pivots"] = int(self.sim.tiny_pivot_count)
        if self.sim.v3:
            out["state_max_info"] = self.sim.state_max()
        if self.sim.p.solid in core.PATH_CLAMP:
            out["path"] = self.sim.path_stats()
        if self.sim.p.solid in core.CONNECTED_COUNT:
            out["connected_rule"] = self.sim.rule_stats()
        if getattr(self.sim, "prop_check", False):
            out["prop_check"] = dict(getattr(self.sim, "prop_totals", {}))
        return out


class Downstrike(Scene):
    """D-V1: 구 반지름 50cm, 축 (0,0), 높이 125→50cm, 초속 5m 아래(0.15초). hole_radius 면 그 기둥을 먼저 비운다."""

    def __init__(self, p, end_time, hole_radius=None, measure="v1"):
        super().__init__(Sim(p))
        self.z = 125.0
        self.n_in = steps_for(self.dt, 0.15)
        self.total = steps_for(self.dt, end_time)
        self.measure = measure
        self.trace = []
        if hole_radius:
            rx, ry, _, _ = geom(self.sim)
            col = (np.hypot(rx[:, None], ry[None, :]) < hole_radius)[:, :, None] & self.sim.fluid
            self.sim.set_empty(col)

    def inputs(self):
        if self.s >= self.n_in:
            return []
        z_new = max(self.z - 500.0 * self.dt, 50.0)
        inp = SweptSphere((0.0, 0.0, self.z), (0.0, 0.0, z_new), 50.0, (0.0, 0.0, -500.0))
        self.z = z_new
        return [inp]

    def result(self):
        out = super().result()
        if self.measure == "v1":
            out.update(v1_metrics(self.sim))
        elif self.measure == "g12b":
            rx, ry, zc, dz = geom(self.sim)
            m = (np.hypot(rx[:, None], ry[None, :]) <= 50.0)[:, :, None] & (zc < 90.0)[None, None, :] & self.sim.fluid
            vol = np.broadcast_to(cell_volume(self.sim), m.shape)
            tot = float((self.sim.total_density() * vol)[m].sum())
            base = float((self.sim.Bc.astype(np.float64) * vol)[m].sum())
            out.update(ratio=tot / base, limit=0.139 * 1.25)
        elif self.measure == "sum":
            absd, zcen = v2_measure(self.sim)
            out.update(abs_diff_vol=absd, removed=removed_amount(self.sim), energy_height_center=zcen)
        return out


class Swing(Scene):
    """D-V2: 길이 1m(y −50~50)·반지름 20cm 캡슐, 높이 z, x −75→75 를 초속 8m(1.5m), 끝 시점 end_time."""

    def __init__(self, p, z, end_time=0.3, erase=0.0, anchor=(0, 0), measure="v2"):
        super().__init__(Sim(p, anchor=anchor))
        self.zh, self.x, self.erase = z, -75.0, erase
        self.n_in = steps_for(self.dt, 150.0 / 800.0)
        self.total = steps_for(self.dt, end_time)
        self.measure = measure

    def inputs(self):
        if self.s >= self.n_in:
            return []
        x, z = self.x, self.zh
        x_new = min(x + 800.0 * self.dt, 75.0)
        self.x = x_new
        return capsule_spheres((x, -50, z), (x, 50, z), (x_new, -50, z), (x_new, 50, z), 20.0, velocity=(800.0, 0, 0),
                               erase_per_sec=self.erase)

    def result(self):
        out = super().result()
        absd, zcen = v2_measure(self.sim)
        out.update(abs_diff_vol=absd, energy_height_center=zcen, removed=removed_amount(self.sim))
        return out


class Walk(Scene):
    """I1·I2 걷기: 구 반지름 40cm·높이 60cm, x −300→300 초속 4m, 깎기 초당 30, 끝난 뒤 0.5초."""

    def __init__(self, p):
        super().__init__(Sim(p))
        self.x = -300.0
        self.n_in = steps_for(self.dt, 1.5)
        self.total = self.n_in + steps_for(self.dt, 0.5)

    def inputs(self):
        if self.s >= self.n_in:
            return []
        x_new = min(self.x + 400.0 * self.dt, 300.0)
        inp = SweptSphere((self.x, 0.0, 60.0), (x_new, 0.0, 60.0), 40.0, (400.0, 0.0, 0.0), 1.0, 30.0)
        self.x = x_new
        return [inp]

    def result(self):
        out = super().result()
        absd, zcen = v2_measure(self.sim)
        out.update(abs_diff_vol=absd, energy_height_center=zcen, removed=removed_amount(self.sim))
        return out


V3_BOX_LO = (0.0, -75.0, 0.0)


class BoxRun(Scene):
    """D-V3: 상자 (0,−75)~(150,75)·높이 h, 세운 캡슐(x −150, 높이 20~160, 반지름 40) 바람 (400,0,0) 2초, 0~3초 흐름 적분.
    wide=True 면 I3: 같은 자리에 y −225~225 로 넓힌 구 벽."""

    def __init__(self, p, height, wide=False):
        boxes = [(V3_BOX_LO, (150.0, 75.0, height))] if height else []
        super().__init__(Sim(p, boxes=boxes))
        sim = self.sim
        rx, ry, zc, dz = geom(sim)
        face_x = rx + sim.p.hx / 2.0
        self.planes = {"back2": int(np.argmin(np.abs(face_x - (156.25 + 2 * HX))))}
        if wide:
            self.planes["side_mid"] = int(np.argmin(np.abs(face_x - 75.0)))
        self.plane_x = {k: float(face_x[i]) for k, i in self.planes.items()}
        self.area = sim.p.hy * layer_overlap(sim, 0.0, 180.0)
        self.area_above = sim.p.hy * layer_overlap(sim, 180.0, 1e9)
        self.shade = np.abs(ry) <= 75.0
        self.side = (np.abs(ry) > 75.0) & (np.abs(ry) <= 225.0)
        self.n_in = steps_for(self.dt, 2.0)
        self.total = steps_for(self.dt, 3.0)
        self.acc = {k: dict(F=0.0, S=0.0, F_above_180=0.0) for k in self.planes}
        self.top_out = 0.0
        if wide:
            ys = np.linspace(-225.0, 225.0, int(math.ceil(450.0 / 40.0)) + 1)
            self.inp = [still_sphere((-150.0, float(y), float(z)), 40.0, (400.0, 0, 0)) for y in ys for z in (20, 55, 90, 125, 160)]
        else:
            self.inp = capsule_spheres((-150, 0, 20), (-150, 0, 160), (-150, 0, 20), (-150, 0, 160), 40.0, velocity=(400.0, 0, 0))

    def inputs(self):
        return self.inp if self.s < self.n_in else []

    def after(self):
        sim, dt = self.sim, self.dt
        for k, i in self.planes.items():
            flow = np.maximum(sim.u[i].astype(np.float64), 0.0)
            a = self.acc[k]
            a["F"] += float((flow[self.shade] * self.area).sum()) * dt
            a["S"] += float((flow[self.side] * self.area).sum()) * dt
            a["F_above_180"] += float((flow[self.shade] * self.area_above).sum()) * dt
        top = np.maximum(sim.w[:, :, -1].astype(np.float64), 0.0)
        self.top_out += float(top.sum()) * sim.p.hx * sim.p.hy * dt

    def result(self):
        out = super().result()
        out.update(planes=self.acc, plane_x=self.plane_x, top_outflow=self.top_out, F=self.acc["back2"]["F"], S=self.acc["back2"]["S"])
        return out


class G4Run(Scene):
    """D-G4: 길이 1m·반지름 20cm 캡슐(y −50~50) 높이 75cm, x −250→250 초속 20m(0.25초), 그 뒤 10초."""

    def __init__(self, p):
        super().__init__(Sim(p))
        self.x = -250.0
        self.n_in = steps_for(self.dt, 0.25)
        self.total = self.n_in + steps_for(self.dt, 10.0)
        self.peak_e = 0.0
        self.max_face = 0.0
        self.max_center = 0.0
        self.finite = True

    def finished(self):
        return self.s >= self.total or not self.finite

    def inputs(self):
        if self.s >= self.n_in:
            return []
        x_new = min(self.x + 2000.0 * self.dt, 250.0)
        out = capsule_spheres((self.x, -50, 75), (self.x, 50, 75), (x_new, -50, 75), (x_new, 50, 75), 20.0, velocity=(2000.0, 0, 0))
        self.x = x_new
        return out

    def after(self):
        sim = self.sim
        if not all(np.isfinite(a).all() for a in (sim.u, sim.v, sim.w, sim.D)):
            self.finite = False
            return
        self.peak_e = max(self.peak_e, sim.wind_energy())
        self.max_face = max(self.max_face, sim.max_wind())
        self.max_center = max(self.max_center, max_center_speed(sim))

    def result(self):
        out = super().result()
        e_end = self.sim.wind_energy() if self.finite else float("nan")
        ratio = e_end / self.peak_e if self.peak_e > 0 else 0.0
        out.update(dt=self.dt, finite=self.finite, max_face_wind=self.max_face, max_center_speed=self.max_center,
                   energy_end_over_peak=ratio,
                   passed=bool(self.finite and max(self.max_face, self.max_center) <= 3000.0 and ratio <= 0.01))
        return out


def fields_of(sim):
    return dict(u=sim.u.copy(), v=sim.v.copy(), w=sim.w.copy(), D=sim.D.copy(), origin=sim.origin, n=(sim.nx, sim.ny))


class CircleRun(Scene):
    """D-G5: 세운 캡슐(높이 20~160, 반지름 40) 이 반지름 2m 원을 초속 4m 로 3초. moving 이면 1절 3 의 창 따라가기."""

    def __init__(self, p, moving):
        super().__init__(Sim(p))
        self.moving = moving
        self.total = steps_for(self.dt, 3.0)
        self.om = 400.0 / 200.0

    def pos(self, t):
        return 200.0 * math.cos(self.om * t), 200.0 * math.sin(self.om * t)

    def inputs(self):
        t0, t1 = self.s * self.dt, (self.s + 1) * self.dt
        (xa, ya), (xb, yb) = self.pos(t0), self.pos(t1)
        vel = (-400.0 * math.sin(self.om * t1), 400.0 * math.cos(self.om * t1), 0.0)
        self.cur = (xb, yb)
        return capsule_spheres((xa, ya, 20), (xa, ya, 160), (xb, yb, 20), (xb, yb, 160), 40.0, velocity=vel)

    def advance(self):
        ins = self.inputs()
        self.sim.step(ins, (self.cur[0], self.cur[1], 90.0) if self.moving else None)
        self.s += 1

    def result(self):
        out = super().result()
        out["fields"] = fields_of(self.sim)
        return out


class HoleRun(Scene):
    """D-G6: 반지름 1m 기둥을 비우고(차이값 = −평소) 바람 없이 둔다. 축에 가장 가까운 맨 아래 칸의 구멍 깊이가 절반이 되는 시간."""

    def __init__(self, p):
        super().__init__(Sim(p))
        rx, ry, _, _ = geom(self.sim)
        r = np.hypot(rx[:, None], ry[None, :])
        hole = (r < 100.0)[:, :, None] & self.sim.fluid
        self.sim.set_empty(hole)
        self.cell = np.unravel_index(int(np.argmin(r)), r.shape)
        self.depth0 = float(-self.sim.D[self.cell[0], self.cell[1], 0])
        self.prev_t, self.prev_d = 0.0, self.depth0
        self.half = None
        self.total = int(math.ceil(4.0 * TIME_SCALE / self.dt)) + 10

    def finished(self):
        return self.half is not None or self.s >= self.total

    def after(self):
        d = float(-self.sim.D[self.cell[0], self.cell[1], 0])
        if d <= self.depth0 / 2 and self.half is None:
            frac = (self.prev_d - self.depth0 / 2) / (self.prev_d - d)
            self.half = self.prev_t + frac * self.dt
        self.prev_t, self.prev_d = self.sim.time, d

    def result(self):
        out = super().result()
        out.update(half_time=self.half, max_wind=self.sim.max_wind())
        return out


class SeamRun(Scene):
    """D-G7: 구(반지름 50, 높이 75, 바람 (−500,0,0)) 를 서쪽 이음매에서 4칸(62.5cm) 안쪽(corner 면 남쪽도)에 3번, 그 뒤 1초."""

    def __init__(self, p, corner):
        super().__init__(Sim(p))
        sim = self.sim
        ox, oy = sim.offset_cm()
        self.center = (4 * HX - ox, (4 * HX - oy) if corner else 0.0, 75.0)
        self.corner = corner
        self.total = 3 + steps_for(self.dt, 1.0)
        n = sim.nx
        li = np.arange(n)
        cx, cy = self.center[0] + ox, self.center[1] + oy
        near_i = np.abs((li + 0.5) * HX - cx) <= 100.0
        near_j = np.abs((np.arange(sim.ny) + 0.5) * HX - cy) <= 100.0
        self.near = near_i[:, None] & near_j[None, :]
        bands = {}
        if corner:
            for name, (si, sj) in dict(se=(slice(n - 8, n), slice(0, 8)), nw=(slice(0, 8), slice(sim.ny - 8, sim.ny)),
                                       ne=(slice(n - 8, n), slice(sim.ny - 8, sim.ny))).items():
                m = np.zeros((n, sim.ny), bool)
                m[si, sj] = True
                bands[name] = m
        else:
            m = np.zeros((n, sim.ny), bool)
            m[n - 8:, :] = True
            bands["east"] = m
        self.bands = bands
        self.rec = {}

    def inputs(self):
        return [still_sphere(self.center, 50.0, (-500.0, 0, 0))] if self.s < 3 else []

    def after(self):
        sim = self.sim
        if self.s == 1:
            self.rec["near_first"] = faces_max(sim, self.near)
        if self.s == 3:
            self.rec["near_third"] = faces_max(sim, self.near)
            self.rec["band_wind_after3"] = {k: faces_max(sim, m) for k, m in self.bands.items()}

    def result(self):
        out = super().result()
        sim = self.sim
        r = self.rec
        bw_end = {k: faces_max(sim, m) for k, m in self.bands.items()}
        bd_end = {k: float(np.abs(np.where(m[:, :, None], sim.D, 0)).max()) for k, m in self.bands.items()}
        c1 = max(r["band_wind_after3"].values()) / max(r["near_third"], 1e-300)
        c2d = max(bd_end.values())
        c2w = max(bw_end.values()) / max(r["near_first"], 1e-300)
        out.update(center=self.center, rec=r, band_wind_end=bw_end, band_absdiff_end=bd_end, ratio_1=c1, max_absdiff_2=c2d,
                   ratio_2_wind=c2w, passed_1=c1 <= 1e-4, passed_2=bool(c2d <= 1e-6 and c2w <= 1e-4),
                   passed=bool(c1 <= 1e-4 and c2d <= 1e-6 and c2w <= 1e-4))
        return out


G8_CASES = {"ga": (100.0, 1.0), "na": (800.0, 0.25), "da": (0.0, 0.5)}


class EraseRun(Scene):
    """D-G8: 구 반지름 20·높이 45, 바람 세기 0, 되돌림 끔, 깎기 초당 30. ga 초속 1m 1초 / na 초속 8m 0.25초 / da 제자리 0.5초.
    길은 원점 가운데. 마지막 계산이 dt 보다 짧으면 깎기 비율을 그 비율만큼 줄인다(머문 시간 = 실제 시간)."""

    def __init__(self, p, case):
        super().__init__(Sim(p))
        self.speed, dur = G8_CASES[case]
        dur *= TIME_SCALE
        self.dur = dur
        self.x = -self.speed * dur / 2.0
        self.total = int(math.ceil(dur / self.dt - 1e-9))

    def inputs(self):
        t0 = self.s * self.dt
        frac = min(1.0, (self.dur - t0) / self.dt)
        x_new = self.x + self.speed * self.dt * frac
        inp = SweptSphere((self.x, 0.0, 45.0), (x_new, 0.0, 45.0), 20.0, (self.speed, 0.0, 0.0), 0.0, 30.0 * frac)
        self.x = x_new
        return [inp]

    def result(self):
        out = super().result()
        out.update(removed=removed_amount(self.sim), end_x=self.x)
        return out


class RunG9(Scene):
    """D-G9: 구(반지름 40, 높이 60, 세기 1, 깎기 30) 가 x −1500→1500 을 초속 6m 로 5초. follow 면 창 따라가기."""

    def __init__(self, p, follow):
        dur = 5.0 * TIME_SCALE
        self.x0 = -600.0 * dur / 2.0
        if follow:
            n = p.nx
            tc = int(math.floor(self.x0 / HX))
            wmin = (int(math.floor((tc - n // 2) / 16 + 0.5)) * 16, core._floor_to(-(p.ny // 2), 16))
            sim = Sim(p, window_min=wmin)
        else:
            sim = Sim(p)
        super().__init__(sim)
        self.follow_on = follow
        self.x = self.x0
        self.total = int(math.ceil(dur / self.dt - 1e-9))
        self.cur = self.x

    def inputs(self):
        x_new = self.x + 600.0 * self.dt
        inp = SweptSphere((self.x, 0.0, 60.0), (x_new, 0.0, 60.0), 40.0, (600.0, 0.0, 0.0), 1.0, 30.0)
        self.x = x_new
        return [inp]

    def follow(self):
        return (self.x, 0.0, 60.0) if self.follow_on else None

    def advance(self):
        ins = self.inputs()
        self.sim.step(ins, self.follow())
        self.s += 1

    def result(self):
        out = super().result()
        out["fields"] = fields_of(self.sim)
        return out


class G10Run(Scene):
    """D-G10: 멈춘 구(반지름 20, 바람 (300,0,0)) 를 축 (0,0) 높이 30~200cm 5cm 간격으로 정지 상태에 한 번 넣는다(압력 끔)."""

    def __init__(self, p):
        super().__init__(Sim(p))
        self.heights = [30.0 + 5.0 * k for k in range(35)]
        self.total = len(self.heights)
        self.P = []

    def advance(self):
        sim = self.sim
        sim.reset_fields()
        sim._apply_inputs([still_sphere((0.0, 0.0, self.heights[self.s]), 20.0, (300.0, 0.0, 0.0))])
        sim.zero_closed_faces()
        *_, vol = sim.location("u")
        self.P.append(float((sim.u.astype(np.float64) * vol).sum()))
        self.s += 1

    def result(self):
        out = super().result()
        P = np.array(self.P)
        mean = float(P.mean())
        formula = 300.0 * 32.0 * math.pi * 20.0 ** 3 / 105.0
        dev = float(np.abs(P / mean - 1).max())
        out.update(heights=self.heights, P=self.P, mean=mean, formula=formula, max_rel_dev=dev, mean_over_formula=mean / formula,
                   passed_1=dev <= 0.10, passed_2=abs(mean / formula - 1) <= 0.15,
                   passed=bool(dev <= 0.10 and abs(mean / formula - 1) <= 0.15))
        return out


class WindProg(Scene):
    """D-G12 a·c: 압력 끔, 매 계산 전 모든 열린 면 바람 = (0,0,100·sin(2π t/0.25)), t = 계산 시작 시각."""

    def __init__(self, p, empty, total_time, record_at=()):
        super().__init__(Sim(p))
        if empty:
            self.sim.empty_all()
        self.total = steps_for(self.dt, total_time)
        self.record_at = {steps_for(self.dt, t): t for t in record_at}
        self.rec = {}

    def before(self):
        t = self.s * self.dt
        self.sim.set_wind((0.0, 0.0, 100.0 * math.sin(2.0 * math.pi * t / (0.25 * TIME_SCALE))))

    def after(self):
        if self.s in self.record_at:
            self.rec[str(self.record_at[self.s])] = float(np.abs(self.sim.D).max())

    def result(self):
        out = super().result()
        sim = self.sim
        tot = sim.total_density() * sim.fluid
        out.update(rec=self.rec, max_total=float(tot.max()), max_total_inner8=float(tot[8:-8, 8:-8].max()),
                   max_absdiff=float(np.abs(sim.D).max()))
        return out


class VortexRun(Scene):
    """D-G12 d(덧붙임 4절): 압력·되돌림·띠 끔, 정지에서. u = +U sin(πx/240) cos(πz/240), w = −U cos(πx/240) sin(πz/240), U = 100,
    x 는 창 가운데 기준 가로 cm. 앞 0.25초 이 바람, 뒤 0.25초 부호를 뒤집은 바람(매 계산 옮기기 전에 덮어씀)."""

    def __init__(self, p):
        super().__init__(Sim(p))
        sim = self.sim
        U, L = 100.0, 240.0
        xc_rel = sim.xc - sim.nx // 2 * sim.p.hx
        xf_rel = sim.xf - sim.nx // 2 * sim.p.hx
        self.u0 = np.broadcast_to((U * np.sin(np.pi * xf_rel / L))[:, None, None] * np.cos(np.pi * sim.zc / L)[None, None, :], sim.u.shape)
        self.w0 = np.broadcast_to((-U * np.cos(np.pi * xc_rel / L))[:, None, None] * np.sin(np.pi * sim.zt / L)[None, None, :], sim.w.shape)
        self.n_half = steps_for(self.dt, 0.25)
        self.total = steps_for(self.dt, 0.5)
        self.mid = None

    def before(self):
        sign = 1.0 if self.s < self.n_half else -1.0
        sim = self.sim
        sim.u = (sign * self.u0).astype(F32)
        sim.v = np.zeros_like(sim.v)
        sim.w = (sign * self.w0).astype(F32)
        sim.zero_closed_faces()

    def after(self):
        if self.s == self.n_half:
            self.mid = float(np.abs(self.sim.D).max())

    def result(self):
        out = super().result()
        out.update(mid_absdiff=self.mid, end_absdiff=float(np.abs(self.sim.D).max()), n_half=self.n_half,
                   div_max_info=float(np.abs(self.sim.divergence(self.u0.astype(F32), np.zeros_like(self.sim.v),
                                                                  self.w0.astype(F32))).max()))
        return out


class G13Run(Scene):
    """D-G13(덧붙임 4절): 구 반지름 20cm 를 높이 30~200cm(5cm 간격), 한 번 계산(dt)의 머문 양. 가 멈춤 / 나 가로 초속 8m(A=(0,0,h)→B=(8m·dt,0,h)).
    Q = Σ(머문 양 × 칸 부피). 계산 식과 무관(깎기 정의만)."""

    def __init__(self, p):
        super().__init__(Sim(p))
        self.heights = [30.0 + 5.0 * k for k in range(35)]
        self.total = 1
        self.Q = {}

    def advance(self):
        sim = self.sim
        X, Y, Z, R, vol = sim.location("c")
        for case, speed in (("still", 0.0), ("moving", 800.0)):
            qs = []
            for h in self.heights:
                a = sim.to_local((0.0, 0.0, h))
                b = sim.to_local((speed * self.dt, 0.0, h))
                qs.append(float((core.dwell_amount(X, Y, Z, R, a, b, 20.0, self.dt) * vol).sum()))
            self.Q[case] = qs
        self.s = 1

    def result(self):
        out = super().result()
        target = self.dt * 32.0 * math.pi * 20.0 ** 3 / 105.0
        cases, ok = {}, True
        for case, qs in self.Q.items():
            Q = np.array(qs)
            mean = float(Q.mean())
            dev = float(np.abs(Q / mean - 1).max())
            good = dev <= 0.10 and abs(mean / target - 1) <= 0.15
            cases[case] = dict(Q=qs, mean=mean, max_rel_dev=dev, mean_over_formula=mean / target, passed_1=dev <= 0.10,
                               passed_2=abs(mean / target - 1) <= 0.15, passed=bool(good))
            ok &= good
        out.update(heights=self.heights, formula=target, cases=cases, passed=bool(ok))
        return out


LEAK_CASES = {
    "n50": ((50.0, 0.0, 0.0), 180.0), "n300": ((300.0, 0.0, 0.0), 180.0), "n1000": ((1000.0, 0.0, 0.0), 180.0),
    "d45": ((300.0 / math.sqrt(2), 300.0 / math.sqrt(2), 0.0), 180.0), "top100": ((0.0, 0.0, 100.0), 30.0),
}


class LeakRun(Scene):
    """R2-S LEAK: 상자 (−75,−75,0)~(75,75,h), 압력 끔, 매 계산 전 바람 덮어쓰기 + 모든 유체 칸 비우기. 20~30번째 계산 평균."""

    def __init__(self, p, case):
        vec, h = LEAK_CASES[case]
        super().__init__(Sim(p, boxes=[((-75.0, -75.0, 0.0), (75.0, 75.0, h))]))
        self.vec = vec
        sim = self.sim
        si = np.where(sim.solid.any(axis=(1, 2)))[0]
        sj = np.where(sim.solid.any(axis=(0, 2)))[0]
        sk = np.where(sim.solid.any(axis=(0, 1)))[0]
        i0, i1, j0, j1, k1 = si.min(), si.max(), sj.min(), sj.max(), sk.max()
        m = np.zeros(sim.solid.shape, bool)
        first = np.zeros(sim.solid.shape, bool)
        if case == "top100":
            m[i0 + 2:i1 - 1, j0 + 2:j1 - 1, k1 + 1:k1 + 4] = True
            first[i0 + 2:i1 - 1, j0 + 2:j1 - 1, k1 + 1] = True
        else:
            m[i1 + 1:i1 + 4, j0 + 2:j1 - 1, :k1 + 1] = True
            first[i1 + 1, j0 + 2:j1 - 1, :k1 + 1] = True
            if case == "d45":
                m[i0 + 2:i1 - 1, j1 + 1:j1 + 4, :k1 + 1] = True
                first[i0 + 2:i1 - 1, j1 + 1, :k1 + 1] = True
        keep = sim.fluid & (sim.Bc >= 0.1)
        self.mask, self.first = m & keep, first & keep
        self.total = 30
        self.vals, self.vals_first = [], []
        self.design = 1.0 - math.exp(-sim.p.diff_relax_per_sec * self.dt)

    def before(self):
        self.sim.set_wind(self.vec)
        self.sim.empty_all()

    def after(self):
        if 20 <= self.s <= 30:
            B = np.maximum(self.sim.Bc.astype(np.float64), 1e-30)
            ratio = (self.sim.D.astype(np.float64) + B) / B
            self.vals.append(float(ratio[self.mask].mean()))
            self.vals_first.append(float(ratio[self.first].mean()))

    def result(self):
        out = super().result()
        refill = float(np.mean(self.vals))
        out.update(cells=int(self.mask.sum()), refill=refill, design=self.design, ratio=refill / self.design,
                   ratio_first_layer_info=float(np.mean(self.vals_first)) / self.design, passed=refill / self.design <= 1.15)
        return out


G14_HALF_THIN = 5.0
G14_REACH = 156.25
G14_FAR = 1.0e5
G14_DTS = (60, 30)


class ThinWallCase(Scene):
    """D-G14(prereg-design-2c 2절) 한 경우. 압력 끔, 매 계산 전 바람 덮어쓰기 + 불어오는 쪽 정지·가는 쪽 비움, 한 번 계산 뒤 max(1+g) ÷ 설계값."""

    def __init__(self, p, group, spec):
        self.group = group
        sim = Sim(p, boxes=spec.get("boxes", ()), obstacles=spec.get("obstacles", ()))
        super().__init__(sim)
        self.total = 1
        self.vec = tuple(float(c) for c in spec["vec"])
        rx, ry, zc, _ = geom(sim)
        n_in = 16
        inner = np.zeros((sim.nx, sim.ny), bool)
        inner[n_in:sim.nx - n_in, n_in:sim.ny - n_in] = True
        X = np.broadcast_to(rx[:, None, None], sim.solid.shape)
        Y = np.broadcast_to(ry[None, :, None], sim.solid.shape)
        Z = np.broadcast_to(zc[None, None, :], sim.solid.shape)
        side = spec["side"](X, Y, Z)
        self.full = sim.fluid & (side < 0)
        self.empty = sim.fluid & (side > 0)
        if spec.get("all_empty"):
            self.full = np.zeros_like(sim.fluid)
            self.empty = sim.fluid.copy()
        self.mask = sim.fluid & inner[:, :, None] & spec["measure"](sim, X, Y, Z, side)
        self.design = 1.0 - math.exp(-sim.p.fill_relax_per_sec * self.dt)

    def before(self):
        sim = self.sim
        sim.set_wind(self.vec)
        g = np.where(self.full, 0.0, sim.g)
        g = np.where(self.empty, -1.0, g)
        sim.g = g.astype(F32)
        for name in ("dpx", "dpy", "dpz"):
            setattr(sim, name, np.where(sim.fluid, 0.0, getattr(sim, name)).astype(F32))
        sim.refresh_diff()

    def result(self):
        out = super().result()
        fill = 1.0 + self.sim.g.astype(np.float64)
        top = float(fill[self.mask].max()) if self.mask.any() else None
        out.update(value=(top / self.design) if top is not None else None, max_fill=top, design=self.design,
                   cells=int(self.mask.sum()), solid_cells=int(self.sim.solid.sum()), vec=list(self.vec))
        return out


def _g14_specs():
    specs = []
    far = G14_FAR

    def plate_x(xp, half):
        return dict(boxes=[((xp - half, -far, -far), (xp + half, far, far))],
                    side=lambda X, Y, Z, xp=xp: X - xp,
                    measure=lambda sim, X, Y, Z, s: (s > 0) & (s <= G14_REACH))

    for xp in (0.0, 3.0, 7.8, 11.0, 15.0):
        for sp in (300.0, 1000.0, 2000.0, 3000.0):
            for d in G14_DTS:
                specs.append(("ga", f"ga_x{xp:g}_v{sp:g}_dt{d}", d, dict(plate_x(xp, G14_HALF_THIN), vec=(sp, 0.0, 0.0))))
    for sp in (300.0, 1000.0, 2000.0, 3000.0):
        for d in G14_DTS:
            specs.append(("gb", f"gb_x0_v{sp:g}_dt{d}", d, dict(plate_x(0.0, 31.25 / 2.0), vec=(sp, 0.0, 0.0))))
    for ang in (26.565, 45.0):
        nx_, ny_ = math.cos(math.radians(ang)), math.sin(math.radians(ang))
        for kind in ("normal", "x"):
            for sp in (1000.0, 3000.0):
                vec = (sp * nx_, sp * ny_, 0.0) if kind == "normal" else (sp, 0.0, 0.0)
                sgn = 1.0 if vec[0] * nx_ + vec[1] * ny_ > 0 else -1.0
                for d in G14_DTS:
                    specs.append(("gc", f"gc_a{ang:g}_{kind}_v{sp:g}_dt{d}", d, dict(
                        obstacles=[dict(kind="box", center=(0.0, 0.0, 0.0), half=(G14_HALF_THIN, far, far), yaw_deg=ang)],
                        vec=vec, side=lambda X, Y, Z, a=nx_, b=ny_, s=sgn: (X * a + Y * b) * s,
                        measure=lambda sim, X, Y, Z, s: (s > 0) & (s <= G14_REACH))))
    for kind, sgn in (("up", 1.0), ("down", -1.0)):
        for sp in (100.0, 300.0, 1000.0, 3000.0):
            for d in G14_DTS:
                specs.append(("gd", f"gd_{kind}_v{sp:g}_dt{d}", d, dict(
                    boxes=[((-far, -far, 60.0), (far, far, 70.0))], vec=(0.0, 0.0, sgn * sp),
                    side=lambda X, Y, Z, s=sgn: (Z - 65.0) * s, measure=lambda sim, X, Y, Z, s: s > 0)))

    def corner(sim, X, Y, Z, s):
        si = np.where(sim.solid.any(axis=(1, 2)))[0]
        sj = np.where(sim.solid.any(axis=(0, 2)))[0]
        sk = np.where(sim.solid.any(axis=(0, 1)))[0]
        m = np.zeros(sim.solid.shape, bool)
        m[si.max() + 1:si.max() + 5, sj.max() + 1:sj.max() + 5, :sk.max() + 1] = True
        return m

    for sp in (300.0, 1000.0):
        for d in G14_DTS:
            r = sp / math.sqrt(2.0)
            specs.append(("ge", f"ge_v{sp:g}_dt{d}", d, dict(
                boxes=[((-75.0, -75.0, 0.0), (75.0, 75.0, 180.0))], vec=(r, r, 0.0), all_empty=True,
                side=lambda X, Y, Z: np.ones_like(X), measure=corner)))
    for sp in (300.0, 1000.0, 3000.0):
        for d in G14_DTS:
            specs.append(("info_end", f"end_v{sp:g}_dt{d}", d, dict(
                boxes=[((3.0 - G14_HALF_THIN, -100.0, -far), (3.0 + G14_HALF_THIN, 100.0, far))], vec=(sp, 0.0, 0.0),
                side=lambda X, Y, Z: X - 3.0,
                measure=lambda sim, X, Y, Z, s: (s > 0) & (s <= G14_REACH) & (np.abs(Y) <= 100.0) & (np.abs(Y) >= 100.0 - 3 * HX))))
            specs.append(("info_end_out", f"endout_v{sp:g}_dt{d}", d, dict(
                boxes=[((3.0 - G14_HALF_THIN, -100.0, -far), (3.0 + G14_HALF_THIN, 100.0, far))], vec=(sp, 0.0, 0.0),
                side=lambda X, Y, Z: X - 3.0,
                measure=lambda sim, X, Y, Z, s: (s > 0) & (s <= G14_REACH) & (np.abs(Y) > 100.0) & (np.abs(Y) <= 100.0 + 3 * HX))))
            pc = HX / 2.0
            specs.append(("info_pillar", f"pillar_v{sp:g}_dt{d}", d, dict(
                boxes=[((pc - G14_HALF_THIN, pc - G14_HALF_THIN, -far), (pc + G14_HALF_THIN, pc + G14_HALF_THIN, far))],
                vec=(sp, 0.0, 0.0), side=lambda X, Y, Z, c=pc: X - c,
                measure=lambda sim, X, Y, Z, s, c=pc: (s > 0) & (s <= G14_REACH) & (np.abs(Y - c) < 1.0))))
    return specs


G14_JUDGED = ("ga", "gc", "gd", "ge")


def g14_combine(parts, groups):
    table, ok = {}, True
    for grp, names in groups.items():
        rows = {k: dict(value=parts[k]["value"], cells=parts[k]["cells"], solid_cells=parts[k]["solid_cells"]) for k in names}
        vals = [r["value"] for r in rows.values() if r["value"] is not None]
        worst = max(vals) if vals else None
        good = worst is not None and worst <= 1.15
        failed = sorted(k for k, r in rows.items() if r["value"] is None or r["value"] > 1.15)
        table[grp] = dict(worst=worst, passed=good if grp in G14_JUDGED else None, failed=failed, rows=rows)
        if grp in G14_JUDGED:
            ok &= good
    paths = [parts[k]["path"] for k in parts if "path" in parts[k]]
    path = None
    if paths:
        hist = np.zeros(max(len(p["hist"]) for p in paths), np.int64)
        for p in paths:
            hist[:len(p["hist"])] += np.asarray(p["hist"], np.int64)
        path = dict(cells=int(hist.sum()), frac_moved_ge1=float(hist[1:].sum() / hist.sum()), max_moves=len(hist) - 1,
                    cap_hits=sum(p["cap_hits"] for p in paths), hist=hist.tolist())
    return dict(values=dict(groups=table, path_all_cases=path),
                criterion="D-G14 가·다·라·마 모든 경우 max(1+g) ÷ (1 − exp(−0.3 dt)) ≤ 1.15 (나·정보는 판정 없음)",
                passed=dict({g: table[g]["passed"] for g in G14_JUDGED if g in table}, all=bool(ok)))


G15_SPLIT_CM = 120.0
G15_HALF_Z = 60.0
G15_STEP = (7.8125, 7.8125, 15.0)
G15_PLANE_SUM = 131
G15_LIMIT = 1.15


def _g15_wind(lam, d, zsign):
    return (lam * G15_STEP[0] * d, lam * G15_STEP[1] * d, zsign * lam * G15_STEP[2] * d)


def _g15_stacked(angle, shift_low, shift_up):
    a = math.radians(angle)
    n = (math.cos(a), math.sin(a))
    plates = [dict(kind="box", center=(shift_low * n[0], shift_low * n[1], G15_SPLIT_CM - G15_HALF_Z),
                   half=(G14_HALF_THIN, G14_FAR, G15_HALF_Z), yaw_deg=angle),
              dict(kind="box", center=(shift_up * n[0], shift_up * n[1], G15_SPLIT_CM + G15_HALF_Z),
                   half=(G14_HALF_THIN, G14_FAR, G15_HALF_Z), yaw_deg=angle)]

    def side(sim, X, Y, Z):
        return X * n[0] + Y * n[1] - np.where(Z < G15_SPLIT_CM, shift_low, shift_up)
    return dict(obstacles=plates, side=side, normal=n)


def _g15_index_plate(nx, ny, nz):
    I, J, K = np.meshgrid(np.arange(nx), np.arange(ny), np.arange(nz), indexing="ij")
    total = I + J + K

    def side(sim, X, Y, Z):
        return (total - G15_PLANE_SUM).astype(np.float64)
    return dict(obstacles=[dict(kind="cells", mask=total == G15_PLANE_SUM)], side=side)


def _g15_specs(n, nz):
    specs = []
    for lam in (0.25, 0.5, 0.99):
        for zs in (1.0, -1.0):
            for d in G14_DTS:
                specs.append(("ga", f"ga_l{lam:g}_z{zs:+g}_dt{d}", d, dict(_g15_stacked(45.0, 11.05, 0.0), vec=_g15_wind(lam, d, zs))))
    for lam in (0.5, 0.99):
        for zs in (1.0, -1.0):
            for d in G14_DTS:
                vec = _g15_wind(lam, d, zs)
                specs.append(("gb", f"gb_low9_l{lam:g}_z{zs:+g}_dt{d}", d, dict(_g15_stacked(45.0, 9.0, 0.0), vec=vec)))
                specs.append(("gb", f"gb_up11.05_l{lam:g}_z{zs:+g}_dt{d}", d, dict(_g15_stacked(45.0, 0.0, 11.05), vec=vec)))
                for sh in (5.0, 7.0, 9.0, 11.0, 14.0):
                    specs.append(("gc", f"gc_a26.565_low{sh:g}_l{lam:g}_z{zs:+g}_dt{d}", d, dict(_g15_stacked(26.565, sh, 0.0), vec=vec)))
                specs.append(("gd", f"gd_ijk{G15_PLANE_SUM}_l{lam:g}_z{zs:+g}_dt{d}", d, dict(_g15_index_plate(n, n, nz), vec=vec)))
    return specs


class StackedWallCase(Scene):
    """D-G15(prereg-design-2d 2절) 한 경우. D-G14 공통과 같음: 압력 끔, 매 계산 전 바람 덮어쓰기 + 불어오는 쪽 정지·가는 쪽 비움,
    한 번 계산 뒤 비운 쪽 유체 칸(창 가장자리 16칸 안쪽)의 max(1+g) ÷ 설계값. 규칙이 걸린 칸 수도 적는다."""

    def __init__(self, p, group, spec):
        self.group = group
        sim = Sim(p, obstacles=spec["obstacles"])
        super().__init__(sim)
        self.total = 1
        self.vec = tuple(float(c) for c in spec["vec"])
        rx, ry, zc, _ = geom(sim)
        X = np.broadcast_to(rx[:, None, None], sim.solid.shape)
        Y = np.broadcast_to(ry[None, :, None], sim.solid.shape)
        Z = np.broadcast_to(zc[None, None, :], sim.solid.shape)
        side = spec["side"](sim, X, Y, Z)
        self.full = sim.fluid & (side < 0)
        self.empty = sim.fluid & (side > 0)
        inner = np.zeros((sim.nx, sim.ny), bool)
        inner[16:sim.nx - 16, 16:sim.ny - 16] = True
        self.mask = self.empty & inner[:, :, None]
        self.design = 1.0 - math.exp(-sim.p.fill_relax_per_sec * self.dt)

    before = ThinWallCase.before

    def result(self):
        out = super().result()
        sim = self.sim
        fill = 1.0 + sim.g.astype(np.float64)
        top = float(fill[self.mask].max()) if self.mask.any() else None
        out.update(value=(top / self.design) if top is not None else None, max_fill=top, design=self.design,
                   cells=int(self.mask.sum()), solid_cells=int(sim.solid.sum()), vec=list(self.vec))
        if sim.rule_mask is not None:
            out["rule_cells"] = dict(all_fluid=int(sim.rule_mask.sum()), measured=int((sim.rule_mask & self.mask).sum()),
                                     empty_side=int((sim.rule_mask & self.empty).sum()), weighted_all=sim.rule_weighted_cells,
                                     end_cell_outside_8=sim.rule_end_outside_cells)
        if self.mask.any():
            arg = np.unravel_index(int(np.argmax(np.where(self.mask, fill, -np.inf))), fill.shape)
            out["max_cell"] = [int(c) for c in arg]
        return out


def g15_combine(parts, groups):
    table, ok = {}, True
    for grp, names in groups.items():
        rows = {k: dict(value=parts[k]["value"], cells=parts[k]["cells"], solid_cells=parts[k]["solid_cells"],
                        rule_cells=parts[k].get("rule_cells"), max_cell=parts[k].get("max_cell")) for k in names}
        vals = [r["value"] for r in rows.values() if r["value"] is not None]
        worst = max(vals) if vals else None
        failed = sorted(k for k, r in rows.items() if r["value"] is None or r["value"] > G15_LIMIT)
        good = worst is not None and not failed
        table[grp] = dict(worst=worst, passed=good, failed=failed, rows=rows)
        ok &= good
    return dict(values=dict(groups=table), criterion="D-G15 가·나·다·라 모든 경우 비운 쪽 max(1+g) ÷ (1 − exp(−0.3 dt)) ≤ 1.15",
                passed=dict({g: table[g]["passed"] for g in table}, all=bool(ok)))


G16_X = (4.2, 6.2, 7.6, 8.0)
G16_S = (0.6, 0.75, 0.9)
G16_DTS = (20, 30)
G16_SCENES = ("ga", "gb26", "gb45", "gc", "gd")
G16_ROOM = (60, 68)
G16_PROBE_IN = (64, 64, 2)
G16_PROBE_OUT = (0, 0, 7)
_G16_TEMPLATES = {}


def _g16_rotated_room(yaw):
    walls = [dict(kind="box", center=(70.0 * math.cos(math.radians(yaw + 90.0 * q)), 70.0 * math.sin(math.radians(yaw + 90.0 * q)), 60.0),
                  half=(G14_HALF_THIN, 65.0, 60.0), yaw_deg=yaw + 90.0 * q) for q in range(4)]
    return walls + [dict(kind="box", center=(0.0, 0.0, 130.0), half=(65.0, 65.0, G14_HALF_THIN), yaw_deg=yaw)]


def _g16_cell_room(nx, ny, nz, floating):
    lo, hi = G16_ROOM
    m = np.zeros((nx, ny, nz), bool)
    k0 = 1 if floating else 0
    m[lo - 1, lo:hi, k0:4] = True
    m[hi, lo:hi, k0:4] = True
    m[lo:hi, lo - 1, k0:4] = True
    m[lo:hi, hi, k0:4] = True
    m[lo:hi, lo:hi, 4] = True
    if floating:
        m[lo:hi, lo:hi, 0] = True
    return [dict(kind="cells", mask=m)]


def _g16_geometry(scene, nx, ny, nz):
    mid, half_len = 70.3125, 65.3125
    if scene == "ga":
        boxes = []
        for s in (-1.0, 1.0):
            boxes.append(((s * mid - G14_HALF_THIN, -half_len, 0.0), (s * mid + G14_HALF_THIN, half_len, 120.0)))
            boxes.append(((-half_len, s * mid - G14_HALF_THIN, 0.0), (half_len, s * mid + G14_HALF_THIN, 120.0)))
        boxes.append(((-half_len, -half_len, 125.0), (half_len, half_len, 135.0)))
        return dict(boxes=boxes)
    if scene in ("gb26", "gb45"):
        return dict(obstacles=_g16_rotated_room(26.565 if scene == "gb26" else 45.0))
    if scene in ("gc", "gd"):
        return dict(obstacles=_g16_cell_room(nx, ny, nz, scene == "gd"))
    raise ValueError(scene)


def _g16_template(P, scene, d, nz, n):
    key = (scene, d)
    if key not in _G16_TEMPLATES:
        from scipy import ndimage
        sim = Sim(P(pressure=False, dt=1.0 / d), **_g16_geometry(scene, n, n, nz))
        labels, count = ndimage.label(sim.fluid)
        lab_in, lab_out = int(labels[G16_PROBE_IN]), int(labels[G16_PROBE_OUT])
        inside = labels == lab_in
        info = dict(components=int(count), split=bool(lab_in != 0 and lab_in != lab_out), probe_in_solid=lab_in == 0,
                    inside=inside, inside_cells=int(inside.sum()) if lab_in else 0)
        _G16_TEMPLATES[key] = (sim, info)
    return _G16_TEMPLATES[key]


def _g16_moves():
    out = []
    for form in ("Xss", "sXs"):
        for X in G16_X:
            for s in G16_S:
                base = (X, s, s) if form == "Xss" else (s, X, s)
                for sx in (1, -1):
                    for sy in (1, -1):
                        for sz in (1, -1):
                            sign = "".join("+" if v > 0 else "-" for v in (sx, sy, sz))
                            out.append((f"{form}_X{X:g}_s{s:g}_{sign}", (sx * base[0], sy * base[1], sz * base[2])))
    return out


class BlockedRoomCase(Scene):
    """D-G16(prereg-design-2e 2절) 한 경우. 장면 틀을 깊은 복사해 상태만 다시 채운다. 압력 끔, 바람 덮어쓰기, 한 번 계산,
    재는 칸(창 가장자리 16칸 안쪽) max(1+g) ÷ (1 − exp(−0.3 dt)). 성질 점검 세기를 켠다."""

    def __init__(self, template, info, fill, move):
        sim = copy.deepcopy(template)
        super().__init__(sim)
        sim.prop_check = True
        self.info, self.fill, self.move = info, fill, move
        self.total = 1 if info["split"] else 0
        layer = float(sim.dz[0])
        if not np.all(sim.dz == layer):
            raise ValueError("D-G16 은 고른 층에서만")
        self.vec = (-move[0] * sim.p.hx / sim.p.dt, -move[1] * sim.p.hy / sim.p.dt, -move[2] * layer / sim.p.dt)
        inside = info["inside"] & sim.fluid
        outside = sim.fluid & ~inside
        self.full, self.empty = (inside, outside) if fill == "a" else (outside, inside)
        inner = np.zeros((sim.nx, sim.ny), bool)
        inner[16:sim.nx - 16, 16:sim.ny - 16] = True
        self.mask = self.empty & inner[:, :, None]
        self.design = 1.0 - math.exp(-sim.p.fill_relax_per_sec * self.dt)

    before = ThinWallCase.before

    def result(self):
        out = super().result()
        sim = self.sim
        out.update(split=self.info["split"], components=self.info["components"], inside_cells=self.info["inside_cells"],
                   cells=int(self.mask.sum()), vec=list(self.vec), move=list(self.move), design=self.design)
        if not self.info["split"]:
            out["value"] = None
            return out
        fill = 1.0 + sim.g.astype(np.float64)
        top = float(fill[self.mask].max())
        out.update(value=top / self.design, max_fill=top)
        arg = np.unravel_index(int(np.argmax(np.where(self.mask, fill, -np.inf))), fill.shape)
        out["max_cell"] = [int(c) for c in arg]
        last = getattr(sim, "prop_last", None)
        if last is not None:
            out["prop_measured"] = {k: int(v[self.mask].sum()) for k, v in last.items()}
        if sim.rule_mask is not None:
            out["rule_cells"] = dict(all_fluid=int(sim.rule_mask.sum()), measured=int((sim.rule_mask & self.mask).sum()),
                                     weighted_all=sim.rule_weighted_cells, end_cell_outside_8=sim.rule_end_outside_cells)
        return out


def g16_combine(parts, groups):
    table, ok, broken = {}, True, []
    for grp, names in groups.items():
        rows = {k: dict(value=parts[k].get("value"), cells=parts[k]["cells"], max_cell=parts[k].get("max_cell"),
                        prop=parts[k].get("prop_check"), prop_measured=parts[k].get("prop_measured"),
                        rule_cells=parts[k].get("rule_cells")) for k in names}
        split = all(parts[k]["split"] for k in names)
        info = dict(components=parts[names[0]]["components"], inside_cells=parts[names[0]]["inside_cells"], split=split)
        if not split:
            table[grp] = dict(info, worst=None, passed=None, failed=[], excluded="나뉘지 않음", rows=rows)
            if grp.split("_")[0] in ("ga", "gc"):
                broken.append(grp)
            continue
        vals = [r["value"] for r in rows.values()]
        failed = sorted(k for k, r in rows.items() if r["value"] > G15_LIMIT)
        prop = {}
        for r in rows.values():
            for k, v in (r["prop"] or {}).items():
                prop[k] = prop.get(k, 0) + v
            for k, v in (r["prop_measured"] or {}).items():
                prop["measured_" + k] = prop.get("measured_" + k, 0) + v
        table[grp] = dict(info, worst=max(vals), passed=not failed, failed=failed, failed_count=len(failed), cases=len(rows),
                          prop_totals=prop, rule_cells_all_total=sum((r["rule_cells"] or {}).get("all_fluid", 0) for r in rows.values()),
                          rows=rows)
        ok &= not failed
    ok &= not broken
    return dict(values=dict(groups=table, broken_scenes=broken),
                criterion="D-G16 가·나·다·라 × 채우기 ㄱ·ㄴ × 바람 전부 재는 칸 max(1+g) ÷ (1 − exp(−0.3 dt)) ≤ 1.15 (나뉘지 않은 장면은 뺌, 가·다는 반드시 나뉨)",
                passed=dict({g: table[g]["passed"] for g in table}, all=bool(ok)))

class StackedSwing(Scene):
    """덧붙임 3 4절 정보: 가의 쌓은 벽(45°, 아래 장 11.05cm), 압력 켬. 벽 앞 30cm(위 장 가운데 면 기준, 높이 120cm)에
    길이 1m(벽 방향)·반지름 20cm 캡슐을 두고 벽 쪽(법선)으로 초속 20m 바람 + 깎기 초당 30 을 0.5초 동안 넣는다.
    벽 이쪽·너머 1m 안(층마다 그 층 판의 가운데 면 기준)의 바람 최대·밀린 거리 최대·채움 변화 최대를 매 계산 갱신."""

    def __init__(self, p):
        spec = _g15_stacked(45.0, 11.05, 0.0)
        super().__init__(Sim(p, obstacles=spec["obstacles"]))
        sim = self.sim
        n = spec["normal"]
        self.n = n
        t = (-n[1], n[0])
        c = (-30.0 * n[0], -30.0 * n[1], G15_SPLIT_CM)
        self.p0 = (c[0] - 50.0 * t[0], c[1] - 50.0 * t[1], c[2])
        self.p1 = (c[0] + 50.0 * t[0], c[1] + 50.0 * t[1], c[2])
        self.total = steps_for(self.dt, 0.5)
        rx, ry, zc, _ = geom(sim)
        X = np.broadcast_to(rx[:, None, None], sim.solid.shape)
        Y = np.broadcast_to(ry[None, :, None], sim.solid.shape)
        Z = np.broadcast_to(zc[None, None, :], sim.solid.shape)
        s = spec["side"](sim, X, Y, Z)
        inner = np.zeros((sim.nx, sim.ny), bool)
        inner[16:sim.nx - 16, 16:sim.ny - 16] = True
        base = sim.fluid & inner[:, :, None]
        self.regions = dict(near=base & (s < 0) & (s >= -100.0), far=base & (s > 0) & (s <= 100.0))
        self.g0 = sim.g.astype(np.float64).copy()
        self.peak = {r: dict(wind=0.0, disp=0.0, fill_change=0.0) for r in self.regions}

    def inputs(self):
        v = (2000.0 * self.n[0], 2000.0 * self.n[1], 0.0)
        return capsule_spheres(self.p0, self.p1, self.p0, self.p1, 20.0, velocity=v, erase_per_sec=30.0)

    def after(self):
        sim = self.sim
        cu, cv, cw = sim.center_velocity()
        speed = np.sqrt(cu ** 2 + cv ** 2 + cw ** 2)
        disp = np.sqrt(sim.dpx.astype(np.float64) ** 2 + sim.dpy.astype(np.float64) ** 2 + sim.dpz.astype(np.float64) ** 2)
        dfill = np.abs(sim.g.astype(np.float64) - self.g0)
        for r, m in self.regions.items():
            pk = self.peak[r]
            pk["wind"] = max(pk["wind"], float(speed[m].max()))
            pk["disp"] = max(pk["disp"], float(disp[m].max()))
            pk["fill_change"] = max(pk["fill_change"], float(dfill[m].max()))

    def result(self):
        out = super().result()
        near, far = self.peak["near"], self.peak["far"]
        out.update(peak=self.peak, far_over_near={k: far[k] / near[k] if near[k] > 0 else None for k in near},
                   cells={r: int(m.sum()) for r, m in self.regions.items()})
        return out


K5_ROOM_WINDS = [((300.0, 1000.0, 2000.0, 3000.0)[k % 4], 37.0 * k, 0.5 if k % 2 == 0 else -0.5) for k in range(20)]


class SealedRoom(Scene):
    """덧붙임 3 4절 정보: 얇은 벽 넷(두께 10cm, 가운데 면이 방 가운데에서 150cm, 반길이 160cm, 높이 0~200cm)과 누운 판
    (200~210cm, 반폭 160cm)으로 막은 방, yaw 만큼 돌림. 안쪽 비움·바깥 가득으로 한 번 두고, 압력 끔, 매 계산 전 고른 바람
    (K5_ROOM_WINDS: 빠르기·방위각·위아래 비율)을 덮어쓰며 20번. 방 안 채움 최댓값 ÷ (1 − exp(−0.3·20·dt))."""

    def __init__(self, p, yaw):
        a = math.radians(yaw)
        ax1, ax2 = (math.cos(a), math.sin(a)), (-math.sin(a), math.cos(a))
        walls = []
        for sgn in (1.0, -1.0):
            walls.append(dict(kind="box", center=(sgn * 150.0 * ax1[0], sgn * 150.0 * ax1[1], 100.0), half=(5.0, 160.0, 100.0), yaw_deg=yaw))
            walls.append(dict(kind="box", center=(sgn * 150.0 * ax2[0], sgn * 150.0 * ax2[1], 100.0), half=(160.0, 5.0, 100.0), yaw_deg=yaw))
        walls.append(dict(kind="box", center=(0.0, 0.0, 205.0), half=(160.0, 160.0, 5.0), yaw_deg=yaw))
        super().__init__(Sim(p, obstacles=walls))
        sim = self.sim
        rx, ry, zc, _ = geom(sim)
        X = np.broadcast_to(rx[:, None, None], sim.solid.shape)
        Y = np.broadcast_to(ry[None, :, None], sim.solid.shape)
        Z = np.broadcast_to(zc[None, None, :], sim.solid.shape)
        a1 = X * ax1[0] + Y * ax1[1]
        a2 = X * ax2[0] + Y * ax2[1]
        self.inside = sim.fluid & (np.abs(a1) < 150.0) & (np.abs(a2) < 150.0) & (Z < 200.0)
        sim.g = np.where(self.inside, -1.0, 0.0).astype(F32)
        sim.refresh_diff()
        self.total = len(K5_ROOM_WINDS)
        self.design = 1.0 - math.exp(-sim.p.fill_relax_per_sec * self.dt * self.total)

    def before(self):
        sp, az, up = K5_ROOM_WINDS[self.s]
        r = math.radians(az)
        self.sim.set_wind((sp * math.cos(r), sp * math.sin(r), up * sp))

    def result(self):
        out = super().result()
        fill = 1.0 + self.sim.g.astype(np.float64)
        top = float(fill[self.inside].max())
        out.update(inside_cells=int(self.inside.sum()), max_fill=top, design=self.design, value=top / self.design,
                   solid_cells=int(self.sim.solid.sum()))
        return out


T_POS = {"T0": "c", "T1": "c", "T2": "c", "T3": "c", "T4": "west", "T5": "corner"}
T_BASE = {"T0": "T0", "T1": "T1", "T2": "T2", "T3": "T3", "T4": "T1", "T5": "T2"}


def t_input_sim(cfg, test):
    spec = core.T_TESTS[T_BASE[test]]
    sim = Sim(make_params(cfg, pressure=False), boxes=[core.T3_BOX] if spec["box"] else [])
    ox, oy = sim.offset_cm()
    x = y = 0.0
    vel = spec["vel"]
    if T_POS[test] in ("west", "corner"):
        x = 4 * HX - ox
    if T_POS[test] == "corner":
        y = 4 * HX - oy
    if test == "T4":
        vel = (-500.0, 0.0, 0.0)
    sim._apply_inputs([still_sphere((x, y, spec["z"]), 50.0, vel)])
    sim.zero_closed_faces()
    return sim, (x, y, spec["z"]), vel


class TTest(Scene):
    """R2-P (a): T0~T5 처음 풀기 R(압력 0 에서, 공통 자). T4 = T1 을 서쪽 이음매 4칸 안쪽, 이음매 쪽(−x). T5 = T2 를 남서 모서리 4칸 안쪽."""

    def __init__(self, cfg):
        self.cfg = cfg
        self.s = 0
        self.total = 6
        self.rows = {}
        self.sim = None

    def advance(self):
        test = "T%d" % self.s
        sim, pos, vel = t_input_sim(self.cfg, test)
        res = core.t_test_R(sim, sim.cfg)
        res.update(pos=pos, vel=vel)
        self.rows[test] = res
        self.cfg_used = sim.cfg
        self.s += 1

    def result(self):
        worst = max(self.rows[t]["R"] for t in ("T1", "T2", "T3", "T4", "T5"))
        return dict(rows=self.rows, worst_T1_T5=worst, solver_cfg=self.cfg_used, passed=worst <= 0.25)


class MoveRun(Scene):
    """R2-P 기록: 구(반지름 50, 높이 75) 가 x −150 에서 +x 초속 3m 로 60번 움직인 뒤 입력 없는 계산 1번(멈춤)."""

    def __init__(self, p):
        super().__init__(Sim(p))
        self.x = -150.0
        self.n_move = 60
        self.total = 61
        self.R, self.cost, self.passes = [], [], []

    def inputs(self):
        if self.s >= self.n_move:
            return []
        x_new = self.x + 300.0 * self.dt
        inp = SweptSphere((self.x, 0.0, 75.0), (x_new, 0.0, 75.0), 50.0, (300.0, 0.0, 0.0))
        self.x = x_new
        return [inp]

    def before(self):
        if self.s == self.n_move:
            alt = copy.deepcopy(self.sim)
            alt.step([still_sphere((self.x, 0.0, 75.0), 50.0, (0.0, 0.0, 0.0))])
            self.R_stop_still_info = alt.last.get("R")

    def after(self):
        last = self.sim.last
        self.R.append(last.get("R"))
        self.cost.append(last.get("cost"))
        self.passes.append(last.get("passes"))

    def result(self):
        out = super().result()
        out.update(R_last10=float(np.mean(self.R[50:60])), R_max_moving=float(np.max(self.R[:60])), R_stop=self.R[60],
                   R_stop_still_sphere_info=self.R_stop_still_info, cost=self.cost[-1], passes=self.passes[-1],
                   R_all=self.R)
        return out


class RestRun(Scene):
    """I6: 내려찍기(D-V1 입력) 뒤 바람 최댓값이 정확히 0 이 되는 계산 횟수. cap 번까지."""

    def __init__(self, p, cap):
        super().__init__(Sim(p))
        self.z = 125.0
        self.n_in = steps_for(p.dt, 0.15)
        self.total = cap
        self.first = {}
        self.zero_at = None

    def finished(self):
        return self.zero_at is not None or self.s >= self.total

    def inputs(self):
        if self.s >= self.n_in:
            return []
        z_new = max(self.z - 500.0 * self.dt, 50.0)
        inp = SweptSphere((0.0, 0.0, self.z), (0.0, 0.0, z_new), 50.0, (0.0, 0.0, -500.0))
        self.z = z_new
        return [inp]

    def after(self):
        if self.s <= self.n_in:
            return
        m = self.sim.max_wind()
        d = float(np.abs(self.sim.D).max())
        for th in (1.0, 1e-2, 1e-4, 1e-6):
            if m < th and str(th) not in self.first:
                self.first[str(th)] = self.s
        if m == 0.0:
            self.zero_at = self.s
        if d == 0.0 and "diff_zero" not in self.first:
            self.first["diff_zero"] = self.s
        self.last_max = (m, d)

    def result(self):
        out = super().result()
        out.update(steps_to_zero=self.zero_at, first_below=self.first, max_wind_and_diff_at_end=self.last_max, cap=self.total)
        return out


# ---------------- 판정 계획(이름 → 하위 장면) 과 합치기 ----------------

def p_of(cfg, **over):
    return make_params(cfg, **over)


def plan_and_combine(name, which, cfg, args):
    """반환: (하위 장면 목록 [(이름, 만들기)], 합치기 함수(parts) → dict(values, criterion, passed))."""
    P = lambda **o: p_of(cfg, **o)  # noqa: E731

    if name == "g2":
        def make():
            sc = Scene(Sim(P(), boxes=[((-75.0, -75.0, 0.0), (75.0, 75.0, 180.0))]))
            sc.total = max(2, int(round(600 * TIME_SCALE)))
            return sc

        def comb(parts):
            r = parts["run"]
            return dict(values=r, criterion="600번 뒤 바람·차이값 최댓값 정확히 0", passed=r["max_wind"] == 0 and r["max_diff"] == 0)
        return [("run", lambda: G2Wrap(make()))], comb

    if name == "g3":
        def comb(parts):
            r = parts["run"]
            ok = r["blocked_max"] == 0 and r["seam_max"] == 0
            return dict(values=r, criterion="압력 풀이 뒤 막힌 면(상자·이음매·바닥) 바람 정확히 0", passed=ok)
        return [("run", lambda: G3Run(P()))], comb

    if name == "g4":
        def comb(parts):
            r = parts["run"]
            return dict(values=r, criterion="깨짐 없음, 바람 최대 ≤ 3000cm/s, 10초 뒤 에너지 ≤ 최고점 1%. D-G4 통과 = 1/30·1/60·1/144 조각 모두 통과",
                        passed=r["passed"])
        return [("run", lambda: G4Run(P()))], comb

    if name == "g5":
        subs = [(f"{tag}_{'move' if mv else 'fixed'}", (lambda mv=mv, pr=pr: CircleRun(P(pressure=pr), mv)))
                for tag, pr in (("a", False), ("b", True)) for mv in (False, True)]

        def comb(parts):
            a = compare_windows(parts["a_fixed"]["fields"], parts["a_move"]["fields"], 8)
            b = compare_windows(parts["b_fixed"]["fields"], parts["b_move"]["fields"], 8, region_radius=200.0)
            b["whole_overlap_worst"] = compare_windows(parts["b_fixed"]["fields"], parts["b_move"]["fields"], 8)["worst_ratio"]
            a["passed"] = a["worst_ratio"] <= 1e-5
            b["passed"] = b["worst_ratio"] <= 0.05
            return dict(values=dict(a=a, b=b, final_origin_move=parts["a_move"]["fields"]["origin"]),
                        criterion="a: 압력 끔, 겹침(가장자리 8칸 뺌) 차 ≤ 최댓값 1e-5 / b: 압력 켬, 원 중심 2m 안 차 ≤ 5%",
                        passed=dict(D_G5a=a["passed"], D_G5b=b["passed"]))
        return subs, comb

    if name == "g6":
        subs = [(f"dt{d}", (lambda d=d: HoleRun(P(dt=1.0 / d)))) for d in (60, 30, 144)]

        def comb(parts):
            target = math.log(2) / 0.3
            h = {k: parts[k]["half_time"] for k in parts}
            if any(v is None for v in h.values()):
                return dict(values=h, criterion="반감 시간 못 찾음", passed=False)
            spread = max(abs(h[k] / h["dt60"] - 1) for k in h)
            err = h["dt60"] / target - 1
            return dict(values=dict(half_time=h, target=target, error_60=err, spread_vs_60=spread,
                                    max_wind={k: parts[k]["max_wind"] for k in parts}),
                        criterion="1/60 반감 2.31초 ±5%, 1/30·1/144 가 1/60 과 2% 이내", passed=bool(abs(err) <= 0.05 and spread <= 0.02))
        return subs, comb

    if name == "g7":
        subs = [("west", lambda: SeamRun(P(), False)), ("corner", lambda: SeamRun(P(), True))]

        def comb(parts):
            return dict(values=parts, criterion="① 3번째 뒤 반대 띠 바람 ÷ 입력 근처 ≤ 1e-4 ② 1초 뒤 띠 |차이값| ≤ 1e-6, 바람 ≤ 처음 입력 근처의 1e-4",
                        passed=bool(parts["west"]["passed"] and parts["corner"]["passed"]))
        return subs, comb

    if name == "g8":
        subs = [(f"{c}_dt{d}", (lambda c=c, d=d: EraseRun(P(dt=1.0 / d, **relax_off(cfg, False)), c)))
                for c in G8_CASES for d in (60, 30, 144)]

        def comb(parts):
            vals, ok = {}, True
            for c in G8_CASES:
                ref = parts[f"{c}_dt60"]["removed"]
                rel = {d: parts[f"{c}_dt{d}"]["removed"] / ref - 1 for d in (30, 144)}
                good = all(abs(v) <= 0.05 for v in rel.values())
                vals[c] = dict(removed={d: parts[f"{c}_dt{d}"]["removed"] for d in (60, 30, 144)}, rel_vs_60=rel, passed=good)
                ok &= good
            return dict(values=vals, criterion="없어진 양이 1/30·1/144 에서 1/60 값의 ±5%", passed=bool(ok))
        return subs, comb

    if name == "g9":
        subs = [("follow", lambda: RunG9(P(), True)), ("fixed512", lambda: RunG9(P(nx=4 * cfg.n), False))]

        def comb(parts):
            return g9_compare(parts["follow"]["fields"], parts["fixed512"]["fields"])
        return subs, comb

    if name == "g10":
        def comb(parts):
            r = parts["run"]
            return dict(values=r, criterion="① P 가 높이 평균 ±10% ② 평균이 V·32πr³/105 의 ±15%", passed=r["passed"])
        return [("run", lambda: G10Run(P(pressure=False)))], comb

    if name == "g11":
        subs = [(f"o{k}", (lambda a=a: Swing(P(), 45.0, 0.3, anchor=a)))
                for k, a in (("0", (0, 0)), ("p1e6", (1000000, 1000000)), ("m1e6", (-1000000, -1000000)))]

        def comb(parts):
            hs = {k: parts[k]["state_hash"] for k in parts}
            return dict(values=hs, criterion="원점 0·+100만·−100만 칸의 0.3초 상태 해시(창 순서) 같음", passed=len(set(hs.values())) == 1)
        return subs, comb

    if name == "g12":
        off = relax_off(cfg, True)
        band_info = dict(off, band_on=True)
        subs = [("a", lambda: WindProg(P(pressure=False, **off), True, 0.5)),
                ("a_band_info", lambda: WindProg(P(pressure=False, **band_info), True, 0.5)),
                ("c", lambda: WindProg(P(pressure=False, **off), False, 0.5, (0.25, 0.5))),
                ("b", lambda: Downstrike(P(), 0.5, hole_radius=100.0, measure="g12b")),
                ("d", lambda: VortexRun(P(pressure=False, **off)))]

        def comb(parts):
            a = parts["a"]["max_total"]
            c = parts["c"]["rec"]
            b = parts["b"]["ratio"]
            dm, de = parts["d"]["mid_absdiff"], parts["d"]["end_absdiff"]
            pa, pc, pb = a <= 1e-6, max(c.values()) <= 0.04, b <= 0.139 * 1.25
            d_undecidable = dm < 0.05
            pd = bool(de <= 0.04 and not d_undecidable)
            return dict(values=dict(a_max_total=a, a_band_on_inner8_info=parts["a_band_info"]["max_total_inner8"],
                                    a_band_on_whole_info=parts["a_band_info"]["max_total"], c_absdiff=c, b_ratio=b,
                                    b_limit=0.139 * 1.25, d_mid_absdiff_info=dm, d_end_absdiff=de, d_undecidable=d_undecidable,
                                    d_div_max_info=parts["d"]["div_max_info"]),
                        criterion="a 전체 농도 최대 ≤ 1e-6(띠 끔, 창 전체) / c 1·2주기 뒤 |차이값| ≤ 0.04 / b 비율 ≤ 0.17375 / "
                                  "d 되돌리는 소용돌이 끝 |차이값| ≤ 0.04(가운데 시점 < 0.05 면 판정 불가)",
                        passed=dict(a=bool(pa), c=bool(pc), b=bool(pb), d=pd, d_undecidable=bool(d_undecidable),
                                    all=bool(pa and pc and pb and pd)))
        return subs, comb

    if name == "g13":
        def comb(parts):
            r = parts["run"]
            return dict(values=r, criterion="가·나 모두 ① Q 가 높이 평균 ±10% ② 평균이 dt·32πr³/105 의 ±15%", passed=r["passed"])
        return [("run", lambda: G13Run(P(pressure=False)))], comb

    if name == "v1":
        def comb(parts):
            r = parts["run"]
            return dict(values=r, criterion="고리 바깥 바람 > 0, 기둥(0~90cm) 농도 10% 이상 낮음, 0.6m 밖 둘레 차이값 > 0", passed=r["passed"])
        return [("run", lambda: Downstrike(P(), 0.5))], comb

    if name == "v2":
        subs = [("low", lambda: Swing(P(), 45.0)), ("high", lambda: Swing(P(), 135.0))]

        def comb(parts):
            lo, hi = parts["low"], parts["high"]
            ratio = lo["abs_diff_vol"] / max(hi["abs_diff_vol"], 1e-30)
            gap = abs(lo["energy_height_center"] - hi["energy_height_center"])
            return dict(values=dict(low=lo, high=hi, diff_ratio_low_over_high=ratio, energy_center_gap_cm=gap),
                        criterion="Σ|차이값|·부피 낮음 ÷ 높음 ≥ 2, 바람 에너지 높이 중심 차 ≥ 50cm",
                        passed=dict(p1=bool(ratio >= 2), p2=bool(gap >= 50), all=bool(ratio >= 2 and gap >= 50)))
        return subs, comb

    if name == "v3":
        subs = [(k, (lambda h=h: BoxRun(P(), h))) for k, h in (("free", None), ("low", 30.0), ("high", 180.0))]
        return subs, v3_combine

    if name == "t":
        def comb(parts):
            r = parts["run"]
            return dict(values=r, criterion="R2-P (a): T1~T5 처음 풀기 R 가장 나쁜 값 ≤ 0.25", passed=r["passed"])
        return [("run", lambda: TTest(cfg))], comb

    if name == "move":
        def comb(parts):
            return dict(values=parts["run"], criterion="기록만(R2-P 후보마다 적을 것)", passed=None)
        return [("run", lambda: MoveRun(P()))], comb

    if name == "g14":
        specs = _g14_specs()
        if which:
            specs = [s for s in specs if s[0] == which]
        groups = {}
        for grp, sub, d, spec in specs:
            groups.setdefault(grp, []).append(sub)
        subs = [(sub, (lambda grp=grp, d=d, spec=spec: ThinWallCase(P(pressure=False, dt=1.0 / d), grp, spec)))
                for grp, sub, d, spec in specs]
        return subs, (lambda parts: g14_combine(parts, groups))

    if name == "g15":
        nz = len(core.layer_table(cfg.layout))
        specs = _g15_specs(cfg.n, nz)
        if which:
            specs = [s for s in specs if s[0] == which]
        groups = {}
        for grp, sub, d, spec in specs:
            groups.setdefault(grp, []).append(sub)
        subs = [(sub, (lambda grp=grp, d=d, spec=spec: StackedWallCase(P(pressure=False, dt=1.0 / d), grp, spec)))
                for grp, sub, d, spec in specs]
        return subs, (lambda parts: g15_combine(parts, groups))

    if name == "g16":
        nz = len(core.layer_table(cfg.layout))
        scenes = [which] if which else list(G16_SCENES)
        if any(sc not in G16_SCENES for sc in scenes):
            raise SystemExit(f"g16 장면은 {G16_SCENES}")
        groups, subs = {}, []
        for sc in scenes:
            for d in G16_DTS:
                for fill in ("a", "b"):
                    for tag, mv in _g16_moves():
                        sub = f"{sc}_{fill}_{tag}_dt{d}"
                        groups.setdefault(f"{sc}_{fill}", []).append(sub)
                        subs.append((sub, (lambda sc=sc, d=d, fill=fill, mv=mv:
                                           BlockedRoomCase(*_g16_template(P, sc, d, nz, cfg.n), fill, mv))))
        return subs, (lambda parts: g16_combine(parts, groups))

    if name == "k5info":
        subs = [("swing", lambda: StackedSwing(P())), ("room_yaw0", lambda: SealedRoom(P(pressure=False), 0.0)),
                ("room_yaw30", lambda: SealedRoom(P(pressure=False), 30.0))]
        return subs, info_combine("정보(prereg-design-2d 4절): 압력 켠 휘두르기(벽 너머 ÷ 벽 이쪽), 밀폐된 방 20번(방 안 채움 최댓값 ÷ 설계값)")

    if name == "k4hash":
        off = relax_off(cfg, True)
        subs = [("v1", lambda: Downstrike(P(), 0.5)), ("v2_low", lambda: Swing(P(), 45.0)), ("v2_high", lambda: Swing(P(), 135.0)),
                ("g12_d", lambda: VortexRun(P(pressure=False, **off))), ("g9_follow", lambda: RunG9(P(), True))]

        def comb(parts):
            return dict(values={k: parts[k]["state_hash"] for k in parts},
                        criterion="정보: 물체 없는 장면 상태 해시(prereg-design-2c 3절 1 비교용)", passed=None)
        return subs, comb

    if name == "leak":
        subs = [(k, (lambda k=k: LeakRun(P(pressure=False), k))) for k in LEAK_CASES]

        def comb(parts):
            ratios = {k: parts[k]["ratio"] for k in parts}
            return dict(values=dict(cases=parts, ratios=ratios, worst=max(ratios.values())),
                        criterion="모든 경우 다시 찬 비율 ÷ 설계값 ≤ 1.15", passed=bool(max(ratios.values()) <= 1.15))
        return subs, comb

    if name == "scene":
        if which not in ("L1", "L2", "L3", "L4"):
            raise SystemExit("scene 은 L1|L2|L3|L4")
        make = {"L1": lambda: Downstrike(P(), 0.5, measure="sum"), "L2": lambda: Swing(P(), 45.0),
                "L3": lambda: Swing(P(), 135.0), "L4": lambda: Swing(P(), 45.0, erase=30.0)}[which]
        npz = os.path.splitext(args.out)[0] + ".npz"

        def comb(parts):
            r = dict(parts["run"])
            r["dump"] = os.path.relpath(npz, ROOT).replace("\\", "/")
            return dict(values=r, criterion="상태 덤프(R2-L 입력)", passed=None)
        return [("run", lambda: DumpWrap(make(), npz))], comb

    if name == "i1":
        subs = [("downstrike", lambda: Downstrike(P(), 0.5, measure="sum")),
                ("swing_low_erase", lambda: Swing(P(), 45.0, erase=30.0)), ("walk", lambda: Walk(P()))]
        return subs, info_combine("I1 바람 없는 판(--mode b1) 대 완전판(--mode full), 같은 입력")

    if name == "i2":
        amp = cfg.patch if cfg.patch else 0.3
        subs = [("swing_low", lambda: Swing(P(stain_amp=amp), 45.0)), ("walk", lambda: Walk(P(stain_amp=amp)))]
        text = f"I2 얼룩 진폭 {amp} 덤프(그림용)"
        if cfg.delta == "v3":
            subs.append(("walk_fine", lambda: Walk(P(stain_amp=0.0, fine_amp=0.2))))
            text = (f"I2(3판, 덧붙임 5절): 그리는 쪽에서 원래 자리 X 의 얼룩(진폭 {amp}, 2m)을 읽음, 계산 쪽은 얼룩 없음. "
                    "walk_fine = 0.5m 간격 잔무늬 ±20%(얼룩 없음). 덤프(그림용)")
        stem = os.path.splitext(args.out)[0]
        subs = [(k, (lambda f=f, k=k: DumpWrap(f(), f"{stem}_{k}.npz"))) for k, f in subs]
        return subs, info_combine(text)

    if name == "i3":
        subs = [(k, (lambda h=h: BoxRun(P(), h, wide=True))) for k, h in (("free", None), ("high", 180.0))]

        def comb(parts):
            v = {pl: parts["high"]["planes"][pl]["S"] / max(parts["free"]["planes"][pl]["S"], 1e-30) for pl in parts["free"]["planes"]}
            return dict(values=dict(S_high_over_free=v, runs=parts), criterion="정보(기대: 넓은 바람이면 ≥ 1.0)", passed=None)
        return subs, comb

    if name == "i4":
        wr = 0.4
        subs = [("v1", lambda: Downstrike(P(wind_relax_per_sec=wr), 0.5))]
        subs += [(f"v3_{k}", (lambda h=h: BoxRun(P(wind_relax_per_sec=wr), h))) for k, h in (("free", None), ("low", 30.0), ("high", 180.0))]
        subs += [("g4", lambda: G4Run(P(wind_relax_per_sec=wr)))]

        def comb(parts):
            v3 = v3_combine({k[3:]: parts[k] for k in parts if k.startswith("v3_")})
            return dict(values=dict(v1=parts["v1"], v3=v3["values"], v3_passed=v3["passed"], g4=parts["g4"]),
                        criterion="정보(바람 되돌림 0.4)", passed=None)
        return subs, comb

    if name == "i5":
        subs = [(f"dt{d}", (lambda d=d: G4Run(P(dt=1.0 / d)))) for d in (20, 10)]
        return subs, info_combine("I5 D-G4 를 1/20·1/10 초에서")

    if name == "i6":
        return [("run", lambda: RestRun(P(), args.cap))], info_combine("I6 교란 뒤 바람 최댓값이 0 이 되는 계산 횟수(--emu16 이면 16비트 흉내)")

    raise SystemExit(f"모르는 이름 {name}")


def info_combine(text):
    def comb(parts):
        return dict(values=parts, criterion=text, passed=None)
    return comb


def v3_combine(parts):
    floor = 80 * 140 * 400 * 2 * 0.05
    fr, lo, hi = parts["free"], parts["low"], parts["high"]
    res = dict(F_free=fr["F"], F_low=lo["F"], F_high=hi["F"], S_free=fr["S"], S_high=hi["S"],
               F_low_over_free=lo["F"] / fr["F"], F_high_over_free=hi["F"] / fr["F"], S_high_over_free=hi["S"] / fr["S"],
               F_free_over_floor5pct=fr["F"] / floor, undecidable=fr["F"] < floor, plane_x=fr["plane_x"],
               top_outflow={k: parts[k]["top_outflow"] for k in parts}, F_above_180={k: parts[k]["planes"]["back2"]["F_above_180"] for k in parts},
               hashes={k: parts[k]["state_hash"] for k in parts})
    p1, p2, p3 = res["F_low_over_free"] >= 0.5, res["F_high_over_free"] <= 0.2, res["S_high_over_free"] >= 1.0
    return dict(values=res, criterion="① F(낮음)/F(자유) ≥ 0.5 ② F(높음)/F(자유) ≤ 0.2 ③ S(높음)/S(자유) ≥ 1.0(2차: 값만)",
                passed=dict(p1=bool(p1), p2=bool(p2), p3_info=bool(p3), undecidable=bool(res["undecidable"]),
                            p12=bool(p1 and p2 and not res["undecidable"])))


class G2Wrap(Scene):
    def __init__(self, inner):
        self.inner = inner
        self.sim = inner.sim
        self.s = 0
        self.total = inner.total

    def advance(self):
        self.inner.advance()
        self.s = self.inner.s

    def result(self):
        sim = self.sim
        return dict(steps=self.s, max_wind=sim.max_wind(), max_diff=float(np.abs(sim.D).max()),
                    max_pressure=float(np.abs(sim.pr).max()), state_hash=sim.state_hash(), state_hash_raw=sim.state_hash(canonical=False))


class G3Run(Scene):
    """D-G3: D-V3 높은 상자 장면 30번. 매 계산(압력 뒤) 막힌 면·이음매 면 바람 최대, 풀이 전 최대 바람과 함께."""

    def __init__(self, p):
        self.inner = BoxRun(p, 180.0)
        self.sim = self.inner.sim
        self.s = 0
        self.total = steps_for(self.sim.p.dt, 0.5)
        self.blocked = 0.0
        self.seam = 0.0
        self.peak = 0.0

    def advance(self):
        self.inner.advance()
        self.s = self.inner.s
        self.blocked = max(self.blocked, self.sim.blocked_face_max())
        self.seam = max(self.seam, self.sim.seam_face_max())
        self.peak = max(self.peak, self.sim.max_wind())

    def result(self):
        return dict(steps=self.s, blocked_max=self.blocked, seam_max=self.seam, peak_wind=self.peak, state_hash=self.sim.state_hash(), state_hash_raw=self.sim.state_hash(canonical=False))


class DumpWrap(Scene):
    def __init__(self, inner, path):
        self.inner, self.path = inner, path
        self.sim = inner.sim
        self.s = 0
        self.total = inner.total

    def finished(self):
        return self.inner.finished()

    def advance(self):
        self.inner.advance()
        self.s = self.inner.s

    def result(self):
        self.sim.dump(self.path)
        out = self.inner.result()
        out["dump"] = os.path.relpath(self.path, ROOT).replace("\\", "/")
        return out


def window_slices(fa, fb, band):
    out = []
    for ax in (0, 1):
        oa, ob = fa["origin"][ax], fb["origin"][ax]
        na, nb = fa["n"][ax], fb["n"][ax]
        lo = max(oa + band, ob + band)
        hi = min(oa + na - band, ob + nb - band)
        out.append((slice(lo - oa, hi - oa), slice(lo - ob, hi - ob), lo, hi))
    return out


def compare_windows(fa, fb, band, region_radius=None):
    (sa_x, sb_x, lx, hx_), (sa_y, sb_y, ly, hy_) = window_slices(fa, fb, band)
    mask = None
    if region_radius is not None:
        cx = (np.arange(lx, hx_) + 0.5) * HX
        cy = (np.arange(ly, hy_) + 0.5) * HX
        mask = (np.hypot(cx[:, None], cy[None, :]) <= region_radius)[:, :, None]
    out, worst = {}, 0.0
    for k in ("u", "v", "w", "D"):
        a = fa[k][sa_x, sa_y].astype(np.float64)
        b = fb[k][sb_x, sb_y].astype(np.float64)
        if mask is not None:
            a, b = np.where(mask, a, 0), np.where(mask, b, 0)
        ref = float(np.abs(a).max())
        d = float(np.abs(a - b).max())
        rel = d / ref if ref > 0 else 0.0
        out[k] = dict(max_abs_diff=d, max_value=ref, ratio=rel)
        worst = max(worst, rel)
    out["worst_ratio"] = worst
    out["cells_per_layer"] = int((hx_ - lx) * (hy_ - ly)) if mask is None else int(mask.sum())
    return out


def g9_compare(fa, fb):
    n = fa["n"][0]
    (sa_x, sb_x, _, _), (sa_y, sb_y, _, _) = window_slices(fa, fb, 0)
    inner = (slice(16, n - 16), slice(16, fa["n"][1] - 16))
    oa, ob = fa["origin"], fb["origin"]
    bx = slice(oa[0] + 16 - ob[0], oa[0] + n - 16 - ob[0])
    by = slice(oa[1] + 16 - ob[1], oa[1] + fa["n"][1] - 16 - ob[1])
    if bx.start < 0 or by.start < 0 or bx.stop > fb["n"][0] or by.stop > fb["n"][1]:
        raise SystemExit("D-G9: 따라간 창이 고정 창 밖으로 나감")
    dD = float(np.abs(fa["D"][inner].astype(np.float64) - fb["D"][bx, by]).max())
    refD = float(np.abs(fb["D"]).max())
    dW = max(float(np.abs(fa[k][inner].astype(np.float64) - fb[k][bx, by]).max()) for k in ("u", "v", "w"))
    refW = max(float(np.abs(fb[k]).max()) for k in ("u", "v", "w"))
    front = float(np.abs(fa["D"][n - 16:]).max())
    allD = float(np.abs(fa["D"]).max())
    c1, c2, c3 = dD / refD, dW / refW, front / max(allD, 1e-300)
    return dict(values=dict(diff_D_max=dD, ref_D_max=refD, ratio_1=c1, diff_wind_max=dW, ref_wind_max=refW, ratio_2=c2,
                            front_band_D_max=front, follow_D_max=allD, ratio_3=c3, follow_origin=oa, fixed_origin=ob),
                criterion="① 차이값 차 ≤ (나) |차이값| 최대 5% ② 바람 차 ≤ (나) 바람 최대 5% ③ 앞쪽 16칸 띠 |차이값| ≤ (가) 최대 1%",
                passed=dict(p1=bool(c1 <= 0.05), p2=bool(c2 <= 0.05), p3=bool(c3 <= 0.01), all=bool(c1 <= 0.05 and c2 <= 0.05 and c3 <= 0.01)))


# ---------------- 실행기(이어 하기) ----------------

def config_tag(cfg, name, which):
    tag = f"{name}{'_' + which if which else ''}_{cfg.layout}_{cfg.advect}_{cfg.solid}_{cfg.solver}_{cfg.delta}_dt{cfg.dt}_n{cfg.n}"
    if cfg.wind_relax != 0.75:
        tag += f"_wr{cfg.wind_relax}"
    if cfg.mode != "full":
        tag += f"_{cfg.mode}"
    if cfg.patch:
        tag += f"_patch{cfg.patch}"
    if cfg.emu8:
        tag += "_emu8"
    if cfg.emu16:
        tag += "_emu16"
    if cfg.inflate:
        tag += "_inflate"
    if cfg.emu_gpu:
        tag += "_emugpu"
    if not cfg.pivot_guard:
        tag += "_nopivotguard"
    if not cfg.path_t32:
        tag += "_patht64"
    if not cfg.canonical_hash:
        tag += "_rawhash"
    return tag


def strip_arrays(obj):
    if isinstance(obj, dict):
        return {str(k): strip_arrays(v) for k, v in obj.items() if k != "fields"}
    if isinstance(obj, (list, tuple)):
        return [strip_arrays(v) for v in obj]
    if isinstance(obj, (np.floating,)):
        return float(obj)
    if isinstance(obj, (np.integer,)):
        return int(obj)
    if isinstance(obj, np.bool_):
        return bool(obj)
    if isinstance(obj, np.ndarray):
        return obj.tolist()
    return obj


def run_gate(name, which, cfg, args):
    out = args.out
    if os.path.exists(out) and not args.force:
        print("skip (있음):", out)
        return 0
    ck = out + ".ckpt.pkl"
    if args.force and os.path.exists(ck) and not args.resume:
        os.remove(ck)
    state = dict(parts={}, cur=None, curname=None, elapsed=0.0)
    if os.path.exists(ck):
        with open(ck, "rb") as fh:
            state = pickle.load(fh)
        print("이어 하기:", ck, "끝난 하위:", list(state["parts"]))
    subs, comb = plan_and_combine(name, which, cfg, args)
    t0 = time.perf_counter()
    base_elapsed = state["elapsed"]

    def save_ckpt():
        state["elapsed"] = base_elapsed + time.perf_counter() - t0
        tmp = ck + ".tmp"
        with open(tmp, "wb") as fh:
            pickle.dump(state, fh, protocol=pickle.HIGHEST_PROTOCOL)
        os.replace(tmp, ck)

    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    for sub, factory in subs:
        if sub in state["parts"]:
            continue
        sc = state["cur"] if state["curname"] == sub and state["cur"] is not None else factory()
        while not sc.finished():
            sc.advance()
            if time.perf_counter() - t0 > args.budget_sec:
                state["cur"], state["curname"] = sc, sub
                save_ckpt()
                print(f"PARTIAL {sub} {sc.s}/{sc.total} → 같은 명령을 다시 실행하면 이어 한다")
                return 3
        state["parts"][sub] = sc.result()
        state["cur"], state["curname"] = None, None
        if len(subs) > 1:
            save_ckpt()
    res = comb(state["parts"])
    elapsed = base_elapsed + time.perf_counter() - t0
    hashes = {k: v.get("state_hash") for k, v in state["parts"].items() if isinstance(v, dict) and "state_hash" in v}
    hashes_raw = {k: v.get("state_hash_raw") for k, v in state["parts"].items() if isinstance(v, dict) and "state_hash_raw" in v}
    tiny_pivots = sum(v.get("tiny_pivots", 0) for v in state["parts"].values() if isinstance(v, dict))
    piece = dict(name=name, which=which, config=asdict(cfg), prereg=PREREG, script="Tools/FluidFX/fog3d_ref/fog3d_gates.py",
                 time_scale=TIME_SCALE, values=strip_arrays(res["values"]), criterion=res["criterion"],
                 passed=strip_arrays(res["passed"]), elapsed_sec=round(elapsed, 1), state_hash=hashes,
                 state_hash_raw=hashes_raw, tiny_pivots=tiny_pivots,
                 finished=time.strftime("%Y-%m-%d %H:%M:%S"), code=code_fingerprints())
    if cfg.solver in CANDIDATES:
        piece["solver_cfg"] = tuned_cfg(cfg.layout, cfg.solver)
    with open(out, "w", encoding="utf-8") as fh:
        json.dump(piece, fh, ensure_ascii=False, indent=1)
    if os.path.exists(ck):
        os.remove(ck)
    print(json.dumps(dict(name=name, which=which, passed=piece["passed"], elapsed=piece["elapsed_sec"]), ensure_ascii=False))
    return 0


# ---------------- 도구: tune, layout_error, fidelity ----------------

def cmd_tune(args):
    data = {}
    if os.path.exists(TUNED_PATH):
        with open(TUNED_PATH, encoding="utf-8") as fh:
            data = json.load(fh)
    for lay in args.layouts:
        t0 = time.perf_counter()
        res = core.tune_on_T0(lay, n=args.n)
        res["_note"] = (f"T0(구 50cm, 높이 105cm, 바람 (300,200,-250), {args.n}x{args.n}, 압력 0 에서 처음 풀기) R 최소, 같으면 비용 작은 쪽. "
                        "그물: 줄 ω 1.0~1.9(0.1), P4·P5 ν∈{(0,1),(1,1),(0,2),(1,2),(2,1),(2,2)}, nc P4{1,2,4,8} P5{2,4,8,16}, P6 V(1,1) nc{4,8,16,30}, 다중 격자 ω{1.0,1.2}. 판정 입력 아님")
        res["_seconds"] = round(time.perf_counter() - t0, 1)
        data[lay] = res
        print(lay, json.dumps({k: {kk: vv for kk, vv in v.items() if kk != "all"} if isinstance(v, dict) else v for k, v in res.items()}, ensure_ascii=False))
    for path in (TUNED_PATH, os.path.join(EVIDENCE_DIR, "tuned_solvers.json")):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(data, fh, ensure_ascii=False, indent=1)
    return 0


def load_pieces(folder):
    out = []
    for path in glob.glob(os.path.join(folder, "*.json")):
        try:
            with open(path, encoding="utf-8") as fh:
                d = json.load(fh)
        except (OSError, ValueError):
            continue
        if isinstance(d, dict) and "name" in d and "config" in d:
            d["_path"] = path
            out.append(d)
    return out


def interp_z(D, dz_src, z_dst):
    zb = np.concatenate([[0.0], np.cumsum(dz_src)[:-1]])
    zc = zb + dz_src / 2.0
    h = np.clip(z_dst, zc[0], zc[-1])
    k0 = np.clip(np.searchsorted(zc, h, side="right") - 1, 0, len(zc) - 2)
    t = (h - zc[k0]) / (zc[k0 + 1] - zc[k0])
    return D[:, :, k0] * (1 - t) + D[:, :, k0 + 1] * t


def drawn_from_dump(npz, z_dst):
    """3판 그려지는 농도(덧붙임 2절): d·g 를 높이로 보간(층 가운데 사이, 양 끝 끝값) → max(0, 1+g̃)·B(z − d̃z). 얼룩 없는 장면 전용."""
    meta = json.loads(str(npz["meta"]))
    prm = meta["params"]
    if prm.get("stain_amp", 0.0) or prm.get("fine_amp", 0.0):
        raise SystemExit("layout_error 3판: 얼룩 있는 덤프는 다루지 않음")
    dz = npz["dz"]
    disp = npz["disp"].astype(np.float64)
    g = interp_z(npz["g"].astype(np.float64), dz, z_dst)
    pz = interp_z(disp[2], dz, z_dst)
    p = core.Params(base_density=prm["base_density"], thickness_cm=prm["thickness_cm"], top_blur_cm=prm["top_blur_cm"])
    B = core.baseline32(p, np.asarray(z_dst, float)[None, None, :] - pz, F32(prm["thickness_cm"])).astype(np.float64)
    return np.maximum(0.0, 1.0 + g) * B


def cmd_layout_error(args):
    pieces = [d for d in load_pieces(args.dir) if d["name"] == "scene" and d["config"].get("delta") == args.delta]
    table, used = {}, {}
    for L in ("L1", "L2", "L3", "L4"):
        dumps = {}
        for d in pieces:
            if d["which"] == L and d["config"]["layout"] in ("U8", "S8", "U32"):
                dumps[d["config"]["layout"]] = d
        if set(dumps) != {"U8", "S8", "U32"}:
            print("모자람:", L, sorted(dumps))
            continue
        cfgs = {k: {kk: vv for kk, vv in v["config"].items() if kk != "layout"} for k, v in dumps.items()}
        if len({json.dumps(c, sort_keys=True) for c in cfgs.values()}) != 1:
            raise SystemExit(f"{L}: 세 배치의 나머지 구성이 다름 {cfgs}")
        arr = {k: np.load(os.path.join(ROOT, v["values"]["dump"])) for k, v in dumps.items()}
        ref = arr["U32"]
        dz32 = ref["dz"]
        zc32 = np.cumsum(dz32) - dz32 / 2
        n = ref["D"].shape[0]
        meta = json.loads(str(ref["meta"]))
        origin, anchor = meta["origin"], meta["anchor"]
        cx = (origin[0] - anchor[0] + np.arange(n) + 0.5) * HX
        cy = (origin[1] - anchor[1] + np.arange(ref["D"].shape[1]) + 0.5) * HX
        mxy = (np.abs(cx)[:, None] <= 300.0) & (np.abs(cy)[None, :] <= 300.0)
        kz = zc32 <= 150.0
        v3 = cfgs["U32"]["delta"] == "v3"
        if v3:
            D32 = drawn_from_dump(ref, zc32[kz])
        else:
            D32 = ref["D"].astype(np.float64)[:, :, kz]
        E = {}
        for lay in ("U8", "S8"):
            if v3:
                Dl = drawn_from_dump(arr[lay], zc32[kz])
            else:
                Dl = interp_z(arr[lay]["D"].astype(np.float64), arr[lay]["dz"], zc32[kz])
            diff = (Dl - D32)[mxy]
            E[lay] = float(np.sqrt(np.mean(diff ** 2)))
        table[L] = dict(E_U8=E["U8"], E_S8=E["S8"], counted=E["U8"] >= 1e-4, S8_over_U8=E["S8"] / E["U8"] if E["U8"] > 0 else None,
                        points=int(mxy.sum() * kz.sum()))
        used[L] = {k: os.path.relpath(v["_path"], ROOT).replace("\\", "/") for k, v in dumps.items()}
    counted = [L for L, r in table.items() if r["counted"]]
    better = [L for L in counted if table[L]["E_S8"] <= 0.75 * table[L]["E_U8"]]
    not_worse = all(table[L]["E_S8"] <= 1.10 * table[L]["E_U8"] for L in counted)
    choose = "S8" if (len(table) == 4 and len(counted) >= 3 and len(better) >= 3 and not_worse) else "U8"
    out = dict(name="layout_error", prereg=PREREG, region="입력 경로 가운데 (0,0) ±300cm, U32 칸 가운데 높이 ≤ 150cm",
               measure="3판이면 그려지는 농도 차(덧붙임 2절), 2판이면 차이값 차(prereg-design-2.md 2절)", table=table,
               counted=counted, better_075=better, all_counted_within_110=not_worse,
               choice_before_extra_condition=choose, note="추가 조건(S8 압력 후보 P1~P5 통과)·4절 되돌림은 따로 본다", sources=used)
    with open(args.out, "w", encoding="utf-8") as fh:
        json.dump(out, fh, ensure_ascii=False, indent=1)
    print(json.dumps(dict(table=table, choice=choose), ensure_ascii=False))
    return 0


FIDELITY = [
    ("v1", ("ring_outward_wind",), "rel", 0.05), ("v1", ("best_ring_column_diff",), "rel", 0.05),
    ("v1", ("center_column_drop",), "abs", 0.02), ("v2", ("diff_ratio_low_over_high",), "rel", 0.05),
    ("v2", ("energy_center_gap_cm",), "abs", 5.0), ("v3", ("F_free",), "rel", 0.05),
    ("v3", ("F_low_over_free",), "abs", 0.02), ("v3", ("F_high_over_free",), "abs", 0.02), ("v3", ("S_high_over_free",), "abs", 0.02),
]


def cmd_fidelity(args):
    pieces = load_pieces(args.dir)
    want = asdict(cfg_from_args(args))

    def find(name, solver):
        key = dict(want, solver=solver)
        for d in pieces:
            if d["name"] == name and d["config"] == key:
                return d
        return None

    rows, ok, missing = [], True, []
    for name, path, kind, tol in FIDELITY:
        a, r = find(name, args.solver), find(name, "REF")
        if a is None or r is None:
            missing.append(f"{name}({'후보' if a is None else ''}{'REF' if r is None else ''})")
            ok = False
            continue
        va, vr = a["values"], r["values"]
        for k in path:
            va, vr = va[k], vr[k]
        d = abs(va - vr) / abs(vr) if kind == "rel" else abs(va - vr)
        good = d <= tol
        ok &= good
        rows.append(dict(gate=name, metric=path[-1], candidate=va, ref=vr, kind=kind, diff=d, tol=tol, passed=bool(good)))
    out = dict(name="fidelity", prereg=PREREG, config=want, rows=rows, missing=missing, passed=bool(ok and not missing))
    with open(args.out, "w", encoding="utf-8") as fh:
        json.dump(out, fh, ensure_ascii=False, indent=1)
    print(json.dumps(dict(passed=out["passed"], missing=missing, worst=[(r["metric"], round(r["diff"], 4)) for r in rows]), ensure_ascii=False))
    return 0


def cfg_from_args(a):
    return Cfg(layout=a.layout, advect=a.advect, solid=a.solid, solver=a.solver, delta=a.delta, dt=int(a.dt), n=int(a.n),
               wind_relax=float(a.wind_relax), mode=a.mode, patch=float(a.patch), emu8=bool(a.emu8), emu16=bool(a.emu16),
               inflate=bool(a.inflate), emu_gpu=bool(a.emu_gpu), pivot_guard=bool(a.pivot_guard),
               path_t32=bool(a.path_t32), canonical_hash=bool(a.canonical_hash))


def main(argv=None):
    global TIME_SCALE
    ap = argparse.ArgumentParser()
    ap.add_argument("name")
    ap.add_argument("which", nargs="?")
    ap.add_argument("--layout", default="U8", choices=["U8", "S8", "U32"])
    ap.add_argument("--advect", default="A1", choices=["A1", "A2", "A3"])
    ap.add_argument("--solid", default="S9", choices=["S0", "S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8", "S9"])
    ap.add_argument("--solver", default="P2", choices=list(CANDIDATES) + ["P7", "REF"])
    ap.add_argument("--delta", default="v3", choices=["v3", "v2", "v1"])
    ap.add_argument("--dt", default=60, type=int)
    ap.add_argument("--n", default=128, type=int)
    ap.add_argument("--wind-relax", dest="wind_relax", default=0.75, type=float)
    ap.add_argument("--mode", default="full", choices=["full", "b1"])
    ap.add_argument("--patch", default=0.0, type=float)
    ap.add_argument("--emu8", action="store_true")
    ap.add_argument("--emu16", action="store_true")
    ap.add_argument("--inflate", dest="inflate", action="store_true", default=True, help="prereg-design-2c 1.1 얇은 물체 부풀리기(기본 켬)")
    ap.add_argument("--no-inflate", dest="inflate", action="store_false")
    ap.add_argument("--no-pivot-guard", dest="pivot_guard", action="store_false", default=True, help="명세 3.5 압력 피벗 보호 끔")
    ap.add_argument("--path-t64", dest="path_t32", action="store_false", default=True, help="명세 3.6-4 길 시점을 옛 절대 좌표 64비트로")
    ap.add_argument("--raw-hash", dest="canonical_hash", action="store_false", default=True, help="상태 해시를 옛 꼴(막힌 칸·-0 그대로)로")
    ap.add_argument("--emu-gpu", dest="emu_gpu", action="store_true", help="prereg-design-2c 4절 새 저장 형식 흉내(바람만 16비트, 두 번)")
    ap.add_argument("--out")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--resume", action="store_true", help="--force 와 함께 써도 이어 하기 상태를 지우지 않음")
    ap.add_argument("--budget-sec", dest="budget_sec", default=480.0, type=float)
    ap.add_argument("--time-scale", dest="time_scale", default=1.0, type=float, help="연기 시험 전용(판정에 쓰지 않음)")
    ap.add_argument("--cap", default=3600, type=int, help="i6 최대 계산 횟수")
    ap.add_argument("--layouts", nargs="*", default=["U8", "S8"])
    ap.add_argument("--dir", default=PIECE_DIR)
    a = ap.parse_args(argv)
    TIME_SCALE = a.time_scale
    if a.name == "tune":
        return cmd_tune(a)
    if a.name in ("layout_error", "fidelity"):
        a.out = a.out or os.path.join(OPS_DIR, f"{a.name}.json")
        return cmd_layout_error(a) if a.name == "layout_error" else cmd_fidelity(a)
    cfg = cfg_from_args(a)
    if a.solver == "P7" and a.layout != "U8":
        raise SystemExit("P7 은 U8 에서만")
    a.out = a.out or os.path.join(PIECE_DIR, config_tag(cfg, a.name, a.which) + ".json")
    return run_gate(a.name, a.which, cfg, a)


if __name__ == "__main__":
    import fog3d_gates as _module_gates
    sys.exit(_module_gates.main())
