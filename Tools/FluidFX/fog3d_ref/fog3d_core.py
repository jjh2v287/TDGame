"""
실행 환경: Python 3.12 + numpy 2.2 + scipy 1.17 (추가 설치 없음), 저장소 루트에서 실행
실행 명령: 직접 실행하지 않는다. fog3d_gates.py·fog3d_selftest.py 가 import 한다(T0 조율만: python Tools/FluidFX/fog3d_ref/fog3d_core.py tune U8)
출력: 없음(tune 은 같은 폴더 tuned_solvers.json 을 고쳐 쓴다)
상태: 3D 볼륨 안개 1단계 기준 모의 정식판(명세 Docs/VolumeFog3D/stage1/spec.md v1 3절), 2026-11-13 X2a 가 Tools/scratch/fog3d/stage1/r2/fog3d_core2.py(b81b3c0db41a649f)에서 올림. 기본값 = 식 3판·U8·A1·S9·P2·부풀리기, 압력 피벗 보호·길 시점 32비트·정규 해시 켬
"""
import hashlib
import json
import math
import os
import sys
from dataclasses import dataclass, field, asdict
from fractions import Fraction

import numpy as np

F32 = np.float32
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
TUNING_PATH = os.path.join(HERE, "tuned_solvers.json")

LAYER_TABLES = {
    "U8": (30.0,) * 8,
    "S8": (15.0, 15.0, 15.0, 15.0, 20.0, 30.0, 50.0, 80.0),
    "U32": (7.5,) * 32,
}

SOLVER_DEFAULTS = {
    "P1": dict(kind="line", sweeps=4, omega=1.5),
    "P2": dict(kind="line", sweeps=8, omega=1.5),
    "P3": dict(kind="line", sweeps=12, omega=1.5),
    "P4": dict(kind="mg", levels=2, budget=8.0, nu1=1, nu2=1, nc=4, omega=1.0),
    "P5": dict(kind="mg", levels=3, budget=8.0, nu1=1, nu2=1, nc=8, omega=1.0),
    "P6": dict(kind="mg", levels=5, budget=12.0, nu1=1, nu2=1, nc=30, omega=1.0),
    "REF": dict(kind="ref", levels=99, nu1=2, nu2=2, nc=30, omega=1.0, max_cycles=200),
    "P7": dict(kind="galerkin", budget=8.0, method="mg01"),
}

T_TESTS = {
    "T0": dict(z=105.0, vel=(300.0, 200.0, -250.0), box=False),
    "T1": dict(z=75.0, vel=(500.0, 0.0, 0.0), box=False),
    "T2": dict(z=120.0, vel=(0.0, 0.0, -500.0), box=False),
    "T3": dict(z=75.0, vel=(500.0, 0.0, 0.0), box=True),
}
T3_BOX = ((100.0, -75.0, -1.0), (250.0, 75.0, 180.0))


CLIP_SELF = ("S1", "S3")
FILL_SOLIDS = ("S2", "S3", "S6")
REWEIGHT = ("S4", "S7", "S8", "S9")
PATH_CLAMP = ("S5", "S6", "S7", "S8", "S9")
CONNECTED_READ = ("S8", "S9")
CONNECTED_COUNT = ("S7", "S8", "S9")
BLOCK_CONNECTED = ("S9",)
EDGE_FACES = ((6, (4, 2)), (5, (4, 1)), (3, (2, 1)))
INFLATE_H_CM = 7.9
INFLATE_V_EXTRA_CM = 7.2
INFLATE_SPHERE_CM = 18.7
PATH_CAP_H_CM = 125.0
PATH_CAP_V_CM = 120.0
PATH_MAX_ITER = 24
PIVOT_GUARD_RATIO = 1e-12


def inflate_half(half, axis_up_dot):
    return max(float(half), INFLATE_H_CM + INFLATE_V_EXTRA_CM * abs(float(axis_up_dot)))


def layer_table(layers):
    if isinstance(layers, str):
        return np.array(LAYER_TABLES[layers], float)
    return np.array(layers, float)


@dataclass
class Params:
    nx: int = 128
    ny: int = 128
    layers: object = "U8"
    hx: float = 15.625
    hy: float = 15.625
    dt: float = 1.0 / 60.0
    base_density: float = 0.8
    thickness_cm: float = 90.0
    top_blur_cm: float = 45.0
    diff_relax_per_sec: float = 0.3
    wind_relax_per_sec: float = 0.75
    band_on: bool = True
    band_cells: float = 8.0
    band_rate_per_sec: float = 4.0
    mode: str = "full"
    advect: str = "A1"
    solid: str = "S9"
    diff_formula: int = 3
    pressure: bool = True
    solver: str = "P2"
    solver_cfg: dict = None
    warm_start: bool = True
    ref_tol: float = 1e-3
    pressure_store32: bool = True
    stain_amp: float = 0.0
    stain_spacing_cm: float = 200.0
    stain_seed: int = 0
    follow_margin: int = 12
    follow_step: int = 16
    weight8: bool = False
    half_store: bool = False
    disp_relax_h_per_sec: float = 0.1
    disp_relax_v_per_sec: float = 0.3
    fill_relax_per_sec: float = 0.3
    fine_amp: float = 0.0
    fine_spacing_cm: float = 50.0
    fine_seed: int = 1
    inflate: bool = True
    emu_gpu: bool = False
    pivot_guard: bool = True
    path_t32: bool = True
    canonical_hash: bool = True


@dataclass
class SweptSphere:
    a: tuple
    b: tuple
    radius: float
    velocity: tuple
    strength: float = 1.0
    erase_per_sec: float = 0.0


def still_sphere(center, radius, velocity, strength=1.0, erase_per_sec=0.0):
    return SweptSphere(tuple(center), tuple(center), radius, tuple(velocity), strength, erase_per_sec)


def capsule_spheres(prev_p0, prev_p1, cur_p0, cur_p1, radius, velocity=None, dt=None, strength=1.0, erase_per_sec=0.0):
    prev_p0, prev_p1 = np.asarray(prev_p0, float), np.asarray(prev_p1, float)
    cur_p0, cur_p1 = np.asarray(cur_p0, float), np.asarray(cur_p1, float)
    axis_len = max(np.linalg.norm(cur_p1 - cur_p0), np.linalg.norm(prev_p1 - prev_p0))
    count = int(math.ceil(axis_len / radius)) + 1
    out = []
    for n in range(count):
        t = n / (count - 1) if count > 1 else 0.0
        a = prev_p0 + (prev_p1 - prev_p0) * t
        b = cur_p0 + (cur_p1 - cur_p0) * t
        vel = tuple(velocity) if velocity is not None else tuple((b - a) / dt)
        out.append(SweptSphere(tuple(a), tuple(b), radius, vel, strength, erase_per_sec))
    return out


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def baseline32(params, z, thickness):
    z = np.maximum(np.asarray(z, float), 0.0)
    thickness = np.asarray(thickness, float)
    val = params.base_density * (1.0 - smoothstep(thickness - params.top_blur_cm, thickness, z))
    return np.asarray(val, F32)


def g_integral(a, u):
    return a * a * u - (2.0 / 3.0) * a * u ** 3 + u ** 5 / 5.0


def point_weight(d2, r):
    q = np.clip(1.0 - d2 / (r * r), 0.0, None)
    return q * q


def band_weight(x, y, z, dz_range, a, b, r):
    a = np.asarray(a, float)
    b = np.asarray(b, float)
    seg = b - a
    l2 = float(seg @ seg)
    px, py, pz = x - a[0], y - a[1], z - a[2]
    if l2 > 0:
        t = np.clip((px * seg[0] + py * seg[1] + pz * seg[2]) / l2, 0.0, 1.0)
        px, py, pz = px - t * seg[0], py - t * seg[1], pz - t * seg[2]
    aa = 1.0 - (px * px + py * py) / (r * r)
    pos = aa > 0
    sa = np.sqrt(np.where(pos, aa, 0.0))
    u1 = np.maximum((pz - dz_range / 2.0) / r, -sa)
    u2 = np.minimum((pz + dz_range / 2.0) / r, sa)
    ok = pos & (u2 > u1)
    w = (r / dz_range) * (g_integral(aa, u2) - g_integral(aa, u1))
    return np.where(ok, w, 0.0)


def point_band_weight(x, y, z, dz_range, cx, cy, cz, r):
    ox, oy, oz = x - cx, y - cy, z - cz
    aa = 1.0 - (ox * ox + oy * oy) / (r * r)
    pos = aa > 0
    sa = np.sqrt(np.where(pos, aa, 0.0))
    u1 = np.maximum((oz - dz_range / 2.0) / r, -sa)
    u2 = np.minimum((oz + dz_range / 2.0) / r, sa)
    ok = pos & (u2 > u1)
    return np.where(ok, (r / dz_range) * (g_integral(aa, u2) - g_integral(aa, u1)), 0.0)


GL3_NODES = (0.5 - 0.5 * math.sqrt(0.6), 0.5, 0.5 + 0.5 * math.sqrt(0.6))
GL3_WEIGHTS = (5.0 / 18.0, 8.0 / 18.0, 5.0 / 18.0)


def dwell_interval(x, y, a, b, r):
    """덧붙임 1절: 칸 가운데 가로 위치에서 c(t) 까지 가로 거리 < r 인 t 구간 [t1, t2](0~1 로 자름). 반환 (t1, t2, 있음)."""
    sx, sy = b[0] - a[0], b[1] - a[1]
    px, py = x - a[0], y - a[1]
    p2 = px * px + py * py
    l2h = sx * sx + sy * sy
    if math.sqrt(l2h) < 0.01:
        inside = p2 < r * r
        return np.zeros(np.shape(inside)), np.where(inside, 1.0, 0.0), inside
    bq = px * sx + py * sy
    disc = bq * bq - l2h * (p2 - r * r)
    sq = np.sqrt(np.maximum(disc, 0.0))
    t1 = np.clip((bq - sq) / l2h, 0.0, 1.0)
    t2 = np.clip((bq + sq) / l2h, 0.0, 1.0)
    ok = (disc > 0) & (t2 > t1)
    return np.where(ok, t1, 0.0), np.where(ok, t2, 0.0), ok


def dwell_amount(x, y, z, dz_range, a, b, r, dt):
    """덧붙임 1절의 머문 양 D = dt·(t2−t1)·Σ ω_q W(c(t_q)) (3점 가우스-르장드르). |AB| < 0.01cm 면 dt·W(A)."""
    a = np.asarray(a, float)
    b = np.asarray(b, float)
    seg = b - a
    if float(np.sqrt(seg @ seg)) < 0.01:
        return dt * point_band_weight(x, y, z, dz_range, a[0], a[1], a[2], r)
    t1, t2, ok = dwell_interval(x, y, a, b, r)
    span = t2 - t1
    acc = 0.0
    for tq, wq in zip(GL3_NODES, GL3_WEIGHTS):
        t = t1 + span * tq
        acc = acc + wq * point_band_weight(x, y, z, dz_range, a[0] + t * seg[0], a[1] + t * seg[1], a[2] + t * seg[2], r)
    return np.where(ok, dt * span * acc, 0.0)


def _hash01(ix, iy, seed):
    m = np.uint64(0xFFFFFFFF)
    with np.errstate(over="ignore"):
        x = np.asarray(ix, np.int64).astype(np.uint64) & m
        y = np.asarray(iy, np.int64).astype(np.uint64) & m
        h = (x * np.uint64(374761393) + y * np.uint64(668265263) + np.uint64(seed & 0xFFFFFFFF) * np.uint64(2246822519)) & m
        h = ((h ^ (h >> np.uint64(13))) * np.uint64(1274126177)) & m
        h = h ^ (h >> np.uint64(16))
    return h.astype(np.float64) / 4294967296.0


def _lattice_coord(world_cells, h, spacing):
    ratio = Fraction(h).limit_denominator(1 << 24) / Fraction(spacing).limit_denominator(1 << 24)
    num, den = ratio.numerator, ratio.denominator
    q = (2 * np.asarray(world_cells, np.int64) + 1) * num
    base = np.floor_divide(q, 2 * den)
    frac = (q - base * 2 * den).astype(np.float64) / (2 * den)
    return base, frac


def stain_noise(world_i, world_j, h, spacing, seed):
    bi, fi = _lattice_coord(world_i, h, spacing)
    bj, fj = _lattice_coord(world_j, h, spacing)
    si = smoothstep(0.0, 1.0, fi)[:, None]
    sj = smoothstep(0.0, 1.0, fj)[None, :]
    BI, BJ = bi[:, None], bj[None, :]
    n00 = _hash01(BI, BJ, seed)
    n10 = _hash01(BI + 1, BJ, seed)
    n01 = _hash01(BI, BJ + 1, seed)
    n11 = _hash01(BI + 1, BJ + 1, seed)
    return (n00 * (1 - si) + n10 * si) * (1 - sj) + (n01 * (1 - si) + n11 * si) * sj


def noise_at(world_i, world_j, offx, offy, h, spacing, seed):
    """칸 가운데 + 가로 어긋남(cm) 자리의 값 잡음. 어긋남 0 이면 stain_noise 와 비트 단위로 같다. 반환 꼴 = offx·offy 를 넓힌 꼴."""
    bi, fi = _lattice_coord(world_i, h, spacing)
    bj, fj = _lattice_coord(world_j, h, spacing)
    fx = fi[:, None, None] + np.asarray(offx, float) / spacing
    fy = fj[None, :, None] + np.asarray(offy, float) / spacing
    fx, fy = np.broadcast_arrays(fx, fy)
    flx, fly = np.floor(fx), np.floor(fy)
    BI = bi[:, None, None] + flx.astype(np.int64)
    BJ = bj[None, :, None] + fly.astype(np.int64)
    si = smoothstep(0.0, 1.0, fx - flx)
    sj = smoothstep(0.0, 1.0, fy - fly)
    n00 = _hash01(BI, BJ, seed)
    n10 = _hash01(BI + 1, BJ, seed)
    n01 = _hash01(BI, BJ + 1, seed)
    n11 = _hash01(BI + 1, BJ + 1, seed)
    return (n00 * (1 - si) + n10 * si) * (1 - sj) + (n01 * (1 - si) + n11 * si) * sj


def _floor_to(v, step):
    return (v // step) * step


def _shift_xy(arr, dx, dy):
    out = np.zeros_like(arr)
    nx, ny = arr.shape[0], arr.shape[1]
    if abs(dx) >= nx or abs(dy) >= ny:
        return out
    sx = slice(max(0, dx), nx + min(0, dx))
    tx = slice(max(0, -dx), nx + min(0, -dx))
    sy = slice(max(0, dy), ny + min(0, dy))
    ty = slice(max(0, -dy), ny + min(0, -dy))
    out[tx, ty] = arr[sx, sy]
    return out


def rms(v):
    return float(np.sqrt(np.mean(np.square(v, dtype=np.float64)))) if v.size else 0.0


# ---------------- 압력 단계 ----------------

class PLevel:
    """배열 꼴 (nz, nx, ny). 결합 계수 = 면 넓이 ÷ 중심 거리 × 열린 비율. diag = 결합 합(+윗면 유령)."""

    def __init__(self, hx, hy, dz, zdist, open_x, open_y, open_z, open_top, active, frac, pivot_guard=False):
        self.hx, self.hy, self.dz, self.zdist = hx, hy, dz, zdist
        self.open_x, self.open_y, self.open_z, self.open_top = open_x, open_y, open_z, open_top
        self.active = active
        self.frac = frac
        self.pivot_guard = pivot_guard
        nz, nx, ny = active.shape
        self.shape = active.shape
        dzc = dz[:, None, None]
        self.cx = (hy * dzc / hx) * open_x
        self.cy = (hx * dzc / hy) * open_y
        self.cz = np.zeros(active.shape)
        if nz > 1:
            self.cz[:-1] = (hx * hy / zdist[:, None, None]) * open_z[:-1]
        self.ctop = (hx * hy / dz[-1]) * open_top
        diag = self.cx.copy()
        diag[:, 1:, :] += self.cx[:, :-1, :]
        diag += self.cy
        diag[:, :, 1:] += self.cy[:, :, :-1]
        diag += self.cz
        diag[1:] += self.cz[:-1]
        diag[-1] += self.ctop
        live = active & (diag > 0)
        self.live = live
        self.diag = np.where(live, diag, 1.0)
        self.lower = np.zeros(active.shape)
        self.lower[1:] = -self.cz[:-1]
        self.upper = -self.cz
        cp = np.zeros(active.shape)
        inv = np.zeros(active.shape)
        pivots = np.zeros(active.shape)
        pivots[0] = self.diag[0]
        inv[0] = self._pivot_inverse(pivots[0], self.diag[0], pivot_guard)
        cp[0] = self.upper[0] * inv[0]
        for k in range(1, nz):
            pivots[k] = self.diag[k] - self.lower[k] * cp[k - 1]
            inv[k] = self._pivot_inverse(pivots[k], self.diag[k], pivot_guard)
            cp[k] = self.upper[k] * inv[k]
        self.cp, self.inv = cp, inv
        self.tiny_pivot_count = int((np.abs(pivots) < PIVOT_GUARD_RATIO * self.diag).sum())
        ii, jj = np.indices((nx, ny))
        self.colors = [((ii + jj) % 2) == c for c in range(2)]
        self.P = None

    @staticmethod
    def _pivot_inverse(pivot, diag, pivot_guard):
        if not pivot_guard:
            return 1.0 / pivot
        tiny = np.abs(pivot) < PIVOT_GUARD_RATIO * diag
        return np.where(tiny, 0.0, 1.0 / np.where(tiny, 1.0, pivot))

    def neighbor_sum(self, p):
        s = np.zeros_like(p)
        s[:, :-1, :] += self.cx[:, :-1, :] * p[:, 1:, :]
        s[:, 1:, :] += self.cx[:, :-1, :] * p[:, :-1, :]
        s[:, :, :-1] += self.cy[:, :, :-1] * p[:, :, 1:]
        s[:, :, 1:] += self.cy[:, :, :-1] * p[:, :, :-1]
        return s

    def apply(self, p):
        out = self.diag * p - self.neighbor_sum(p)
        out[:-1] -= self.cz[:-1] * p[1:]
        out[1:] -= self.cz[:-1] * p[:-1]
        return np.where(self.live, out, p)

    def tri_solve(self, r):
        nz = r.shape[0]
        y = np.empty_like(r)
        y[0] = r[0] * self.inv[0]
        for k in range(1, nz):
            y[k] = (r[k] - self.lower[k] * y[k - 1]) * self.inv[k]
        for k in range(nz - 2, -1, -1):
            y[k] -= self.cp[k] * y[k + 1]
        return y

    def sweep(self, b, p, omega, store):
        for m in self.colors:
            y = self.tri_solve(b + self.neighbor_sum(p))
            mm = m[None, :, :] & self.live
            p = np.where(mm, (1.0 - omega) * p + omega * y, p)
            p = store(p)
        return p


def coarsen_level(L):
    nz, nx, ny = L.shape
    nxc, nyc = nx // 2, ny // 2
    act = L.active.reshape(nz, nxc, 2, nyc, 2).any(axis=(2, 4))
    ox = L.open_x[:, 1::2, :].reshape(nz, nxc, nyc, 2).mean(axis=3)
    oy = L.open_y[:, :, 1::2].reshape(nz, nx, nyc)
    oy = oy.reshape(nz, nxc, 2, nyc).mean(axis=2)
    oz = L.open_z.reshape(nz, nxc, 2, nyc, 2).mean(axis=(2, 4))
    ot = L.open_top.reshape(nxc, 2, nyc, 2).mean(axis=(1, 3))
    C = PLevel(L.hx * 2, L.hy * 2, L.dz, L.zdist, ox, oy, oz, ot, act, L.frac / 4.0, L.pivot_guard)
    ii = np.arange(nx)
    jj = np.arange(ny)
    i0 = ii // 2
    i1 = i0 + np.where(ii % 2 == 1, 1, -1)
    j0 = jj // 2
    j1 = j0 + np.where(jj % 2 == 1, 1, -1)
    act_pad = np.zeros((nz, nxc + 2, nyc + 2))
    act_pad[:, 1:-1, 1:-1] = C.live
    combos = []
    den = np.zeros((nz, nx, ny))
    for ia, wi in ((i0, 0.75), (i1, 0.25)):
        for ja, wj in ((j0, 0.75), (j1, 0.25)):
            ix = (ia + 1)[:, None]
            jx = (ja + 1)[None, :]
            w = wi * wj
            combos.append((ix, jx, w))
            den += w * act_pad[:, ix, jx]
    L.P = (combos, np.where(den > 0, 1.0 / np.where(den > 0, den, 1.0), 0.0) * L.live)
    return C


def restrict(L, r):
    nz, nx, ny = r.shape
    return r.reshape(nz, nx // 2, 2, ny // 2, 2).sum(axis=(2, 4))


def prolong(L, ec):
    combos, scale = L.P
    nz, nxc, nyc = ec.shape
    pad = np.zeros((nz, nxc + 2, nyc + 2))
    pad[:, 1:-1, 1:-1] = ec
    num = 0.0
    for ix, jx, w in combos:
        num = num + w * pad[:, ix, jx]
    return num * scale


def build_levels(fine, count):
    levels = [fine]
    while len(levels) < count:
        L = levels[-1]
        nz, nx, ny = L.shape
        if nx % 2 or ny % 2 or min(nx, ny) // 2 < 4:
            break
        levels.append(coarsen_level(L))
    return levels


def cycle_cost(levels, l, nu1, nu2, nc, zero):
    L = levels[l]
    if l == len(levels) - 1:
        return nc * L.frac
    c = (nu1 + nu2) * L.frac
    c += 0.0 if (zero and nu1 == 0) else L.frac
    c += levels[l + 1].frac + L.frac
    return c + cycle_cost(levels, l + 1, nu1, nu2, nc, True)


def cycle_passes(levels, l, nu1, nu2, nc, zero):
    L = levels[l]
    if l == len(levels) - 1:
        nz, nx, ny = L.shape
        return 1 if nx * ny <= 256 else 2 * nc
    c = 2 * (nu1 + nu2) + (0 if (zero and nu1 == 0) else 1) + 2
    return c + cycle_passes(levels, l + 1, nu1, nu2, nc, True)


def vcycle(levels, l, b, x, nu1, nu2, nc, omega, store, zero):
    L = levels[l]
    if l == len(levels) - 1:
        for _ in range(nc):
            x = L.sweep(b, x, omega, store)
        return x
    for _ in range(nu1):
        x = L.sweep(b, x, omega, store)
        zero = False
    r = b if zero else store(b - L.apply(x))
    r = np.where(L.live, r, 0.0)
    rc = store(restrict(L, r))
    ec = vcycle(levels, l + 1, rc, np.zeros_like(rc), nu1, nu2, nc, omega, store, True)
    x = store(x + prolong(L, ec))
    for _ in range(nu2):
        x = L.sweep(b, x, omega, store)
    return x


def _store32(v):
    return v.astype(F32).astype(np.float64)


def _store64(v):
    return v


# ---------------- 모의 ----------------

class Sim:
    """창 순서 배열(창 최소 칸 = 지역 칸 0). 저장 순서는 to_storage(). 위치 인수는 모두 기준 칸(anchor) 기준 cm."""

    def __init__(self, params, anchor=(0, 0), window_min=None, boxes=(), obstacles=()):
        p = self.p = params
        self.dz = layer_table(p.layers)
        self.nz = nz = len(self.dz)
        self.nx, self.ny = p.nx, p.ny
        if self.nx % 16 or self.ny % 16:
            raise ValueError("nx, ny 는 16의 배수")
        self.zb = np.concatenate([[0.0], np.cumsum(self.dz)[:-1]])
        self.zt = self.zb + self.dz
        self.zc = self.zb + self.dz / 2.0
        self.zdist = np.diff(self.zc)
        self.wrange = np.concatenate([self.zdist, [self.dz[-1]]])
        self.wnodes = np.concatenate([[0.0], self.zt])
        self.anchor = (int(anchor[0]), int(anchor[1]))
        if window_min is None:
            window_min = (_floor_to(self.anchor[0] - self.nx // 2, 16), _floor_to(self.anchor[1] - self.ny // 2, 16))
        self.origin = (int(window_min[0]), int(window_min[1]))
        if self.origin[0] % 16 or self.origin[1] % 16:
            raise ValueError("창 최소 칸은 16의 배수")
        self.boxes = [(np.asarray(lo, float), np.asarray(hi, float)) for lo, hi in boxes]
        self.obstacles = [dict(o) for o in obstacles]
        self.path_hist = np.zeros(PATH_MAX_ITER + 1, np.int64)
        self.path_cap_hits = 0
        self.rule_cells = 0
        self.rule_weighted_cells = 0
        self.rule_end_outside_cells = 0
        self.rule_mask = None
        shape = (self.nx, self.ny, nz)
        self.u = np.zeros(shape, F32)
        self.v = np.zeros(shape, F32)
        self.w = np.zeros(shape, F32)
        self.D = np.zeros(shape, F32)
        self.pr = np.zeros(shape, F32)
        self.dpx = np.zeros(shape, F32)
        self.dpy = np.zeros(shape, F32)
        self.dpz = np.zeros(shape, F32)
        self.g = np.zeros(shape, F32)
        self.v3 = p.diff_formula == 3
        if p.fine_amp != 0.0 and not self.v3:
            raise ValueError("잔무늬(fine_amp)는 식 3판 그리기 전용")
        self.time = 0.0
        self.steps = 0
        self.solid = None
        self._levels = None
        self._p7 = None
        self.cfg = resolve_solver_cfg(p)
        self.last = {}
        self.overlap = (0, self.nx - 1, 0, self.ny - 1)
        self.fresh = np.zeros((self.nx, self.ny), bool)
        self._geometry()

    def __getstate__(self):
        state = self.__dict__.copy()
        state["_levels"] = None
        state["_p7"] = None
        state.pop("_face_setups", None)
        state.pop("_disconnected", None)
        state.pop("_end_cell", None)
        state.pop("_last_weights", None)
        state.pop("_reach", None)
        return state

    # ---------- 좌표·막힘 ----------
    def offset_cm(self):
        return ((self.anchor[0] - self.origin[0]) * self.p.hx, (self.anchor[1] - self.origin[1]) * self.p.hy)

    def to_local(self, pos):
        ox, oy = self.offset_cm()
        return np.array([pos[0] + ox, pos[1] + oy, pos[2]], float)

    def world_cells(self):
        return self.origin[0] + np.arange(self.nx, dtype=np.int64), self.origin[1] + np.arange(self.ny, dtype=np.int64)

    def center_rel(self):
        rx = (self.origin[0] - self.anchor[0] + np.arange(self.nx) + 0.5) * self.p.hx
        ry = (self.origin[1] - self.anchor[1] + np.arange(self.ny) + 0.5) * self.p.hy
        return rx, ry, self.zc.copy()

    def _geometry(self):
        p = self.p
        nx, ny, nz = self.nx, self.ny, self.nz
        self.xc = (np.arange(nx) + 0.5) * p.hx
        self.yc = (np.arange(ny) + 0.5) * p.hy
        self.xf = (np.arange(nx) + 1.0) * p.hx
        self.yf = (np.arange(ny) + 1.0) * p.hy
        rx, ry, _ = self.center_rel()
        solid = np.zeros((nx, ny, nz), bool)
        for lo, hi in self.boxes:
            if p.inflate:
                lo, hi = self._inflate_aabb(lo, hi)
            mx = (rx > lo[0]) & (rx < hi[0])
            my = (ry > lo[1]) & (ry < hi[1])
            mz = (self.zc > lo[2]) & (self.zc < hi[2])
            solid |= mx[:, None, None] & my[None, :, None] & mz[None, None, :]
        for ob in self.obstacles:
            solid |= self._obstacle_mask(ob, rx, ry)
        if self.solid is None or not np.array_equal(solid, self.solid):
            self.solid = solid
            self._levels = None
            self._p7 = None
        fluid = ~solid
        self.fluid = fluid
        ou = np.zeros_like(fluid)
        ou[:-1] = fluid[:-1] & fluid[1:]
        ov = np.zeros_like(fluid)
        ov[:, :-1] = fluid[:, :-1] & fluid[:, 1:]
        ow = fluid.copy()
        ow[:, :, :-1] = fluid[:, :, :-1] & fluid[:, :, 1:]
        self.ou, self.ov, self.ow = ou, ov, ow
        wi, wj = self.world_cells()
        if p.stain_amp != 0.0:
            n = stain_noise(wi, wj, p.hx, p.stain_spacing_cm, p.stain_seed)
            self.T = (p.thickness_cm * (1.0 + p.stain_amp * (2.0 * n - 1.0))).astype(F32)
        else:
            self.T = np.full((nx, ny), p.thickness_cm, F32)
        if self.v3 and (p.stain_amp != 0.0 or p.fine_amp != 0.0):
            zero = np.zeros((nx, ny, nz))
            self.Bc = self.base_at(zero, zero, np.broadcast_to(self.zc[None, None, :], (nx, ny, nz)))
        else:
            self.Bc = baseline32(p, self.zc[None, None, :], self.T[:, :, None])
        li = np.arange(nx)
        lj = np.arange(ny)
        ei = np.minimum(li + 0.5, nx - li - 0.5)
        ej = np.minimum(lj + 0.5, ny - lj - 0.5)
        fade = smoothstep(0.0, max(p.band_cells, 1e-6), np.minimum(ei[:, None], ej[None, :]))
        self.band = np.exp(-p.band_rate_per_sec * (1.0 - fade) * p.dt)[:, :, None]

    @staticmethod
    def _inflate_aabb(lo, hi):
        center = (lo + hi) / 2.0
        half = (hi - lo) / 2.0
        grown = np.array([inflate_half(half[0], 0.0), inflate_half(half[1], 0.0), inflate_half(half[2], 1.0)])
        return center - grown, center + grown

    def _obstacle_mask(self, ob, rx, ry):
        if ob["kind"] == "cells":
            mask = np.asarray(ob["mask"], bool)
            if mask.shape != (self.nx, self.ny, self.nz):
                raise ValueError(f"cells 막힘 모양 {mask.shape} != {(self.nx, self.ny, self.nz)}")
            return mask.copy()
        c = np.asarray(ob["center"], float)
        dx = (rx - c[0])[:, None, None]
        dy = (ry - c[1])[None, :, None]
        dz = (self.zc - c[2])[None, None, :]
        if ob["kind"] == "sphere":
            r = float(ob["radius"])
            if self.p.inflate:
                r = max(r, INFLATE_SPHERE_CM)
            return dx * dx + dy * dy + dz * dz < r * r
        if ob["kind"] != "box":
            raise ValueError(ob["kind"])
        half = [float(h) for h in ob["half"]]
        if self.p.inflate:
            half = [inflate_half(half[0], 0.0), inflate_half(half[1], 0.0), inflate_half(half[2], 1.0)]
        yaw = math.radians(float(ob.get("yaw_deg", 0.0)))
        ca, sa = math.cos(yaw), math.sin(yaw)
        along1 = dx * ca + dy * sa
        along2 = -dx * sa + dy * ca
        return (np.abs(along1) < half[0]) & (np.abs(along2) < half[1]) & (np.abs(dz) < half[2])

    # ---------- 창 ----------
    def follow(self, target_rel):
        p = self.p
        tc = (self.anchor[0] + int(math.floor(target_rel[0] / p.hx)), self.anchor[1] + int(math.floor(target_rel[1] / p.hy)))
        new = list(self.origin)
        for a, n in ((0, self.nx), (1, self.ny)):
            center = self.origin[a] + n // 2
            if abs(tc[a] - center) > p.follow_margin:
                new[a] = int(math.floor((tc[a] - n // 2) / p.follow_step + 0.5)) * p.follow_step
        self.set_window(tuple(new))

    def set_window(self, new_origin):
        dx = int(new_origin[0]) - self.origin[0]
        dy = int(new_origin[1]) - self.origin[1]
        if dx == 0 and dy == 0:
            return
        if new_origin[0] % 16 or new_origin[1] % 16:
            raise ValueError("창 최소 칸은 16의 배수")
        for name in ("u", "v", "w", "D", "pr", "dpx", "dpy", "dpz", "g"):
            setattr(self, name, _shift_xy(getattr(self, name), dx, dy))
        self.origin = (int(new_origin[0]), int(new_origin[1]))
        self.fresh = self.fresh | self.new_band_mask(dx, dy)
        lo_x, hi_x = max(0, -dx), min(self.nx, self.nx - dx) - 1
        lo_y, hi_y = max(0, -dy), min(self.ny, self.ny - dy) - 1
        if lo_x > hi_x or lo_y > hi_y:
            self.overlap = (0, self.nx - 1, 0, self.ny - 1)
        else:
            self.overlap = (lo_x, hi_x, lo_y, hi_y)
        self._geometry()
        self.u *= self.ou
        self.v *= self.ov
        self.w *= self.ow
        self.D[self.solid] = 0
        self.pr[self.solid] = 0
        if self.v3:
            self._zero_solid_state()
            self.refresh_diff()

    def new_band_mask(self, dx, dy):
        m = np.zeros((self.nx, self.ny), bool)
        dx = max(-self.nx, min(self.nx, dx))
        dy = max(-self.ny, min(self.ny, dy))
        if dx > 0:
            m[self.nx - dx:, :] = True
        elif dx < 0:
            m[:-dx, :] = True
        if dy > 0:
            m[:, self.ny - dy:] = True
        elif dy < 0:
            m[:, :-dy] = True
        return m

    def to_storage(self, arr):
        return np.roll(np.roll(arr, self.origin[0] % self.nx, axis=0), self.origin[1] % self.ny, axis=1)

    # ---------- 읽기 ----------
    def _axis(self, pos, h, off, n_nodes, horizontal):
        f = pos / h - off
        i0 = np.clip(np.floor(f).astype(np.int64), 0, n_nodes - 2)
        t = f - i0
        if horizontal and self.p.weight8:
            t = np.floor(t * 256.0) / 256.0
        return i0, t

    def _axis_z(self, z, nodes):
        k0 = np.clip(np.searchsorted(nodes, z, side="right") - 1, 0, len(nodes) - 2)
        t = (z - nodes[k0]) / (nodes[k0 + 1] - nodes[k0])
        return k0, t

    def _setup(self, kind, x, y, z):
        p = self.p
        nx, ny, nz = self.nx, self.ny, self.nz
        Nx, Ny, Nz = nx, ny, nz
        offx, offy = 0.5, 0.5
        znodes = self.zc
        if kind == "u":
            Nx, offx = nx + 1, 0.0
        elif kind == "v":
            Ny, offy = ny + 1, 0.0
        elif kind == "w":
            Nz, znodes = nz + 1, self.wnodes
        i0, tx = self._axis(x, p.hx, offx, Nx, True)
        j0, ty = self._axis(y, p.hy, offy, Ny, True)
        k0, tz = self._axis_z(z, znodes)
        idx = (i0 * (Ny - 1) + j0) * (Nz - 1) + k0
        return idx, tx.astype(F32), ty.astype(F32), tz.astype(F32), (i0, j0, k0)

    @staticmethod
    def corners8(a):
        """a (Nx,Ny,Nz) → (8, 칸 수) 표. 행 순서 (di,dj,dk) = 000,001,010,011,100,101,110,111."""
        parts = [a[di:a.shape[0] - 1 + di, dj:a.shape[1] - 1 + dj, dk:a.shape[2] - 1 + dk]
                 for di in (0, 1) for dj in (0, 1) for dk in (0, 1)]
        return np.stack(parts, axis=0).reshape(8, -1)

    @staticmethod
    def _gather(Q, st, zero=None):
        idx, tx, ty, tz, _ = st
        g = np.take(Q, idx, axis=1)
        if zero is not None:
            g = np.where(zero, F32(0), g)
        one = F32(1)
        sz, sy = one - tz, one - ty
        z = [g[2 * n] * sz + g[2 * n + 1] * tz for n in range(4)]
        y0 = z[0] * sy + z[1] * ty
        y1 = z[2] * sy + z[3] * ty
        return (y0 * (one - tx) + y1 * tx).astype(np.float64)

    def _padded(self):
        nx, ny, nz = self.nx, self.ny, self.nz
        up = np.zeros((nx + 1, ny, nz), F32)
        up[1:] = self.u
        vp = np.zeros((nx, ny + 1, nz), F32)
        vp[:, 1:] = self.v
        wp = np.zeros((nx, ny, nz + 1), F32)
        wp[:, :, 1:] = self.w
        return self.corners8(up), self.corners8(vp), self.corners8(wp)

    def _clamp(self, x, y, z):
        lo_x, hi_x, lo_y, hi_y = self.overlap
        p = self.p
        x = np.clip(x, (lo_x + 0.5) * p.hx, (hi_x + 0.5) * p.hx)
        y = np.clip(y, (lo_y + 0.5) * p.hy, (hi_y + 0.5) * p.hy)
        z = np.clip(z, self.zc[0], self.zc[-1])
        return x, y, z

    def _clip_closed(self, dx, dy, dz):
        if self.p.solid not in CLIP_SELF:
            return dx, dy, dz
        lo_x = np.ones_like(self.ou)
        lo_x[1:] = ~self.ou[:-1]
        hi_x = ~self.ou
        lo_y = np.ones_like(self.ov)
        lo_y[:, 1:] = ~self.ov[:, :-1]
        hi_y = ~self.ov
        lo_z = np.ones_like(self.ow)
        lo_z[:, :, 1:] = ~self.ow[:, :, :-1]
        hi_z = ~self.ow
        dx = np.where(((dx < 0) & lo_x) | ((dx > 0) & hi_x), 0.0, dx)
        dy = np.where(((dy < 0) & lo_y) | ((dy > 0) & hi_y), 0.0, dy)
        dz = np.where(((dz < 0) & lo_z) | ((dz > 0) & hi_z), 0.0, dz)
        return dx, dy, dz

    def fill_solids(self, D):
        solid = self.solid
        if not solid.any():
            return D
        fl = self.fluid.astype(np.float64)
        Df = D.astype(np.float64)
        s = np.zeros(D.shape)
        c = np.zeros(D.shape)
        for src, dst in _neighbor_pairs(D.shape):
            s[dst] += (Df * fl)[src]
            c[dst] += fl[src]
        F = Df.copy()
        t1 = solid & (c > 0)
        F[t1] = s[t1] / c[t1]
        s2 = np.zeros(D.shape)
        c2 = np.zeros(D.shape)
        for src, dst in _neighbor_pairs(D.shape):
            s2[dst] += F[src]
            c2[dst] += 1.0
        t2 = solid & (c == 0) & (c2 > 0)
        F[t2] = s2[t2] / c2[t2]
        return F.astype(F32)

    def _center_weights(self, x, y, z):
        idx, tx, ty, tz, _ = self._setup("c", x, y, z)
        one = F32(1)
        wx = (one - tx, tx)
        wy = (one - ty, ty)
        wz = (one - tz, tz)
        W = np.stack([wx[di] * wy[dj] * wz[dk] for di in (0, 1) for dj in (0, 1) for dk in (0, 1)], axis=0)
        if self.p.solid in REWEIGHT:
            sol = np.take(self.corners8(self.solid), idx, axis=1)
            if self.p.solid in CONNECTED_READ:
                sol = sol | self._disconnected
            kept = np.where(sol, 0.0, W)
            tot = kept.sum(axis=0)
            use = sol.any(axis=0) & (tot > 0)
            W = np.where(use[None], kept / np.where(use, tot, one)[None], W).astype(F32)
        if getattr(self, "prop_check", False):
            self._last_weights = W
        return idx, W

    def _sample_state(self, x, y, z, arrays):
        idx, W = self._center_weights(x, y, z)
        out = [(np.take(self.corners8(np.asarray(a, F32)), idx, axis=1) * W).sum(axis=0).astype(np.float64) for a in arrays]
        if self.p.solid not in REWEIGHT:
            return out
        all_blocked = np.take(self.corners8(self.solid), idx, axis=1).all(axis=0)
        self.s4_all_blocked_count = getattr(self, "s4_all_blocked_count", 0) + int(all_blocked.sum())
        if not all_blocked.any() or np.shape(all_blocked) != self.solid.shape:
            return out
        return [np.where(all_blocked, np.asarray(a, np.float64), o) for a, o in zip(arrays, out)]

    def _sample_diff(self, x, y, z, Dread):
        idx, W = self._center_weights(x, y, z)

        def wsum(a):
            return (np.take(self.corners8(np.asarray(a, F32)), idx, axis=1) * W).sum(axis=0).astype(np.float64)

        d_til = wsum(Dread)
        b_bar = wsum(self.Bc)
        rho = wsum(Dread + self.Bc)
        if self.p.stain_amp != 0.0:
            t_bar = wsum(np.broadcast_to(self.T[:, :, None], Dread.shape))
        else:
            t_bar = np.full(d_til.shape, float(self.p.thickness_cm))
        return d_til, b_bar, rho, t_bar

    def center_velocity(self):
        um = np.zeros_like(self.u)
        um[1:] = self.u[:-1]
        vm = np.zeros_like(self.v)
        vm[:, 1:] = self.v[:, :-1]
        wm = np.zeros_like(self.w)
        wm[:, :, 1:] = self.w[:, :, :-1]
        return (0.5 * (um.astype(np.float64) + self.u), 0.5 * (vm.astype(np.float64) + self.v),
                0.5 * (wm.astype(np.float64) + self.w))

    # ---------- 옮기기 ----------
    def _advect(self):
        p = self.p
        dt = p.dt
        X = self.xc[:, None, None]
        Y = self.yc[None, :, None]
        Z = self.zc[None, None, :]
        shape = self.u.shape
        if p.solid in PATH_CLAMP and p.advect != "A1":
            raise ValueError("S5~S7(길 따라 자르기)은 A1 에서만 지원")
        Dread = self.fill_solids(self.D) if p.solid in FILL_SOLIDS else self.D
        up, vp, wp = self._padded()
        newu = newv = neww = None
        st = zero = None
        if p.advect in ("A1", "A2"):
            vel = (self.u.astype(np.float64), self.v.astype(np.float64), self.w.astype(np.float64))
            if p.solid in PATH_CLAMP:
                dep = self._path_clamp(vel, dt)
            else:
                d = self._clip_closed(-vel[0] * dt, -vel[1] * dt, -vel[2] * dt)
                dep = (X + d[0], Y + d[1], Z + d[2])
            st = self._setup("c", *self._clamp(*dep))
            if p.solid in CONNECTED_COUNT:
                self._disconnected = self._disconnected_corner(st)
                if p.solid in CONNECTED_READ:
                    zero = self._disconnected
            newu = self._gather(self.corners8(self.u), st, zero)
            newv = self._gather(self.corners8(self.v), st, zero)
            neww = self._gather(self.corners8(self.w), st, zero)
            if p.advect == "A1":
                ddep = dep
            else:
                cv = self.center_velocity()
                d = self._clip_closed(-cv[0] * dt, -cv[1] * dt, -cv[2] * dt)
                ddep = (X + d[0], Y + d[1], Z + d[2])
        elif p.advect == "A3":
            faces = (
                ("u", up, (self.xf[:, None, None], Y, Z)),
                ("v", vp, (X, self.yf[None, :, None], Z)),
                ("w", wp, (X, Y, self.zt[None, None, :])),
            )
            res = []
            cache = self.__dict__.setdefault("_face_setups", {})
            for kind, flat, pos in faces:
                pos = tuple(np.broadcast_to(c, shape) for c in pos)
                key = (kind, self.overlap)
                if key not in cache:
                    cpos = self._clamp(*pos)
                    cache[key] = (self._setup("u", *cpos), self._setup("v", *cpos), self._setup("w", *cpos))
                su, sv, sw = cache[key]
                vel = [self._gather(up, su), self._gather(vp, sv), self._gather(wp, sw)]
                dep = self._clamp(pos[0] - vel[0] * dt, pos[1] - vel[1] * dt, pos[2] - vel[2] * dt)
                res.append(self._gather(flat, self._setup(kind, *dep)))
            newu, newv, neww = res
            cv = self.center_velocity()
            d = self._clip_closed(-cv[0] * dt, -cv[1] * dt, -cv[2] * dt)
            ddep = (X + d[0], Y + d[1], Z + d[2])
        else:
            raise ValueError(p.advect)
        ddep = tuple(np.broadcast_to(c, shape) for c in ddep)
        cx, cy, cz = self._clamp(*ddep)
        wd = math.exp(-p.wind_relax_per_sec * dt)
        band = self.band if p.band_on else 1.0
        if self.v3:
            self._advect_state(newu, newv, neww, (X, Y, Z), (cx, cy, cz), wd, band)
            self._count_props(st, zero)
            return
        d_til, b_bar, rho, t_bar = self._sample_diff(cx, cy, cz, Dread)
        h_dep = np.maximum(ddep[2], 0.0)
        b_dep = baseline32(p, h_dep, t_bar).astype(np.float64)
        bc = self.Bc.astype(np.float64)
        decay = math.exp(-p.diff_relax_per_sec * dt)
        if p.diff_formula == 2:
            rho = np.maximum(rho, 0.0)
            e = b_dep - b_bar
            fill = b_bar > 1e-6
            phi = np.where(fill, np.minimum(1.0, rho / np.where(fill, b_bar, 1.0)), 0.0)
            total = np.maximum(rho + phi * e, 0.0)
            newD = (total - bc) * decay
        else:
            newD = (d_til + b_dep - bc) * decay
        if self.fresh.any():
            keep = ~self.fresh[:, :, None]
            newu, newv, neww, newD = newu * keep, newv * keep, neww * keep, newD * keep
        self.u = (newu * wd * band).astype(F32)
        self.v = (newv * wd * band).astype(F32)
        self.w = (neww * wd * band).astype(F32)
        self.D = (newD * band).astype(F32)
        self._count_props(st, zero)

    def _face_closed(self, axis, c, sign):
        open_mask = (self.ou, self.ov, self.ow)[axis]
        ci, cj, ck = c
        plus = ~open_mask[ci, cj, ck]
        idx = [ci, cj, ck]
        low = idx[axis] == 0
        idx[axis] = np.maximum(idx[axis] - 1, 0)
        minus = low | ~open_mask[idx[0], idx[1], idx[2]]
        return np.where(sign > 0, plus, minus)

    def _cell_center(self, axis, ci):
        if axis == 0:
            return (ci + 0.5) * self.p.hx
        if axis == 1:
            return (ci + 0.5) * self.p.hy
        return self.zc[ci]

    def _cell_boundary(self, axis, ci, sign):
        if axis == 0:
            return np.where(sign > 0, ci + 1.0, ci * 1.0) * self.p.hx
        if axis == 1:
            return np.where(sign > 0, ci + 1.0, ci * 1.0) * self.p.hy
        return np.where(sign > 0, self.zt[ci], self.zb[ci])

    def _path_clamp(self, vel, dt):
        """덧붙임 2(prereg-design-2c) 1.2 길 따라 자르기. 반환: 표본 자리(지역 cm) 3성분. 칸 옮김 횟수는 path_hist 에 센다."""
        shape = self.u.shape
        start = [np.broadcast_to(self.xc[:, None, None], shape), np.broadcast_to(self.yc[None, :, None], shape),
                 np.broadcast_to(self.zc[None, None, :], shape)]
        move = [-vel[0] * dt, -vel[1] * dt, -vel[2] * dt]
        caps = (PATH_CAP_H_CM, PATH_CAP_H_CM, PATH_CAP_V_CM)
        scale = np.ones(shape)
        for axis in range(3):
            size = np.abs(move[axis])
            nonzero = size > 0
            scale = np.minimum(scale, np.where(nonzero, caps[axis] / np.where(nonzero, size, 1.0), 1.0))
        move = [m * scale for m in move]
        target = self._clamp(start[0] + move[0], start[1] + move[1], start[2] + move[2])
        target = [np.broadcast_to(t, shape) for t in target]
        sign = [np.sign(target[a] - start[a]).astype(np.int64) for a in range(3)]
        sample = [t.copy() for t in target]
        cell = [np.broadcast_to(np.arange(self.nx)[:, None, None], shape).astype(np.int64),
                np.broadcast_to(np.arange(self.ny)[None, :, None], shape).astype(np.int64),
                np.broadcast_to(np.arange(self.nz)[None, None, :], shape).astype(np.int64)]
        bound = [sign[a] == 0 for a in range(3)]
        local_span = self._local_target_span32(start, move) if self.p.path_t32 else None
        active = np.ones(shape, bool)
        moves = np.zeros(shape, np.int64)
        for _ in range(PATH_MAX_ITER):
            for axis in range(3):
                candidate = active & ~bound[axis]
                if not candidate.any():
                    continue
                hit = candidate & self._face_closed(axis, cell, sign[axis])
                if not hit.any():
                    continue
                center = self._cell_center(axis, cell[axis])
                passed = (target[axis] - center) * sign[axis] > 0
                sample[axis] = np.where(hit, np.where(passed, center, target[axis]), sample[axis])
                bound[axis] = bound[axis] | hit
            t_min = np.full(shape, np.inf)
            first_axis = np.full(shape, -1, np.int64)
            for axis in range(3):
                free = active & ~bound[axis]
                edge = self._cell_boundary(axis, cell[axis], sign[axis])
                if local_span is None:
                    span = target[axis] - start[axis]
                    t = np.where(free, (edge - start[axis]) / np.where(free, span, 1.0), np.inf)
                else:
                    t = self._crossing_time32(edge - start[axis], local_span[axis], free)
                earlier = t < t_min
                t_min = np.where(earlier, t, t_min)
                first_axis = np.where(earlier, axis, first_axis)
            active = active & (t_min <= 1.0)
            if not active.any():
                break
            for axis in range(3):
                cell[axis] = cell[axis] + np.where(active & (first_axis == axis), sign[axis], 0)
            moves += active
        self.path_cap_hits += int((active & self.fluid).sum())
        self.path_hist += np.bincount(moves[self.fluid].ravel(), minlength=PATH_MAX_ITER + 1)[:PATH_MAX_ITER + 1]
        self._end_cell = cell
        return tuple(sample)

    def _local_target_span32(self, start, move):
        lo_x, hi_x, lo_y, hi_y = self.overlap
        p = self.p
        bounds = (((lo_x + 0.5) * p.hx, (hi_x + 0.5) * p.hx), ((lo_y + 0.5) * p.hy, (hi_y + 0.5) * p.hy),
                  (self.zc[0], self.zc[-1]))
        return [np.clip(move[a], bounds[a][0] - start[a], bounds[a][1] - start[a]).astype(F32) for a in range(3)]

    @staticmethod
    def _crossing_time32(edge_offset, span32, free):
        usable = free & (span32 != 0)
        with np.errstate(over="ignore", divide="ignore"):
            t = np.asarray(edge_offset, F32) / np.where(usable, span32, F32(1))
        return np.where(usable, t, np.inf)

    @staticmethod
    def _base_weights(st):
        _, tx, ty, tz, _ = st
        one = F32(1)
        wx, wy, wz = (one - tx, tx), (one - ty, ty), (one - tz, tz)
        return np.stack([wx[di] * wy[dj] * wz[dk] for di in (0, 1) for dj in (0, 1) for dk in (0, 1)], axis=0)

    def _block_reach(self, st):
        """덧붙임 4(prereg-design-2e) 1절 이어짐. 반환 (닿음 (8,…), 끝 칸이 묶음 안, 막힘 (8,…)). 차이 비트 d = 4dx+2dy+dz, 모서리 번호 = 4(ox^dx)+2(oy^dy)+(oz^dz)."""
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
        for d, (a, b) in EDGE_FACES:
            reach[d] = open_at[d] & (reach[a] | reach[b])
        reach[7] = open_at[7] & (reach[6] | reach[5] | reach[3])
        for d, _ in EDGE_FACES:
            reach[d] = reach[d] | (open_at[d] & reach[7])
        out = np.zeros(sol.shape, bool)
        for d in range(8):
            np.put_along_axis(out, corner[d], reach[d][None], axis=0)
        return out, inside, sol

    def _disconnected_block(self, st):
        reach, inside, sol = self._block_reach(st)
        disconnected = inside[None] & ~reach & ~sol
        weighted = (disconnected & (self._base_weights(st) > 0)).any(axis=0)
        self.rule_mask = disconnected.any(axis=0) & self.fluid
        self.rule_cells += int(self.rule_mask.sum())
        self.rule_weighted_cells += int((self.rule_mask & weighted).sum())
        self.rule_end_outside_cells += int((~inside & self.fluid).sum())
        return disconnected

    def _count_props(self, st, zero):
        """덧붙임 4 4절 성질 점검(읽기만, 값은 바꾸지 않음): 칸마다 무게 > 0 인 모서리 가운데 묶음 안 이어짐 밖 / 끝 칸과 다른 덩어리 수."""
        if not getattr(self, "prop_check", False) or st is None or self.p.solid not in PATH_CLAMP:
            return
        from scipy import ndimage
        reach, inside, sol = self._block_reach(st)
        labels, _ = ndimage.label(self.fluid)
        end = self._end_cell
        other = (np.take(self.corners8(labels), st[0], axis=1) != labels[end[0], end[1], end[2]][None]) & ~sol
        w_dg = self._last_weights if self.p.solid in REWEIGHT else self._base_weights(st)
        w_wind = self._base_weights(st)
        if zero is not None:
            w_wind = np.where(zero, F32(0), w_wind)
        keep = self.fluid & inside
        last = dict(dg_unconnected=((w_dg > 0) & ~reach).sum(axis=0) * keep,
                    dg_other_component=((w_dg > 0) & other).sum(axis=0) * self.fluid,
                    wind_unconnected=((w_wind > 0) & ~reach & ~sol).sum(axis=0) * keep,
                    wind_other_component=((w_wind > 0) & other).sum(axis=0) * self.fluid)
        self.prop_last = last
        totals = self.__dict__.setdefault("prop_totals", {k: 0 for k in last})
        for k, v in last.items():
            totals[k] += int(v.sum())
        totals["end_outside_8"] = totals.get("end_outside_8", 0) + int((self.fluid & ~inside).sum())

    def _disconnected_corner(self, st):
        """덧붙임 3(prereg-design-2d) 1절. 반환 (8, nx, ny, nz): 끝 칸 c 기준 몸통 대각 칸이 유체이고 모서리 대각 세 칸이 모두 막혔으면 참."""
        if self.p.solid in BLOCK_CONNECTED:
            return self._disconnected_block(st)
        idx, tx, ty, tz, base = st
        off = [self._end_cell[a] - base[a] for a in range(3)]
        inside = (off[0] >= 0) & (off[0] <= 1) & (off[1] >= 0) & (off[1] <= 1) & (off[2] >= 0) & (off[2] <= 1)
        o = [np.clip(a, 0, 1) for a in off]
        sol = np.take(self.corners8(self.solid), idx, axis=1)

        def corner(a, b, c):
            return (a * 4 + b * 2 + c)[None]

        def blocked(n):
            return np.take_along_axis(sol, n, axis=0)[0]

        body = corner(1 - o[0], 1 - o[1], 1 - o[2])
        rule = (inside & blocked(corner(1 - o[0], 1 - o[1], o[2])) & blocked(corner(1 - o[0], o[1], 1 - o[2]))
                & blocked(corner(o[0], 1 - o[1], 1 - o[2])) & ~blocked(body))
        disconnected = np.zeros(sol.shape, bool)
        np.put_along_axis(disconnected, body, rule[None], axis=0)
        one = F32(1)
        weight = (np.where(o[0] == 0, tx, one - tx) * np.where(o[1] == 0, ty, one - ty) * np.where(o[2] == 0, tz, one - tz))
        self.rule_mask = rule & self.fluid
        self.rule_cells += int(self.rule_mask.sum())
        self.rule_weighted_cells += int((self.rule_mask & (weight > 0)).sum())
        self.rule_end_outside_cells += int((~inside & self.fluid).sum())
        return disconnected

    def rule_stats(self):
        return dict(cells=int(self.rule_cells), weighted_cells=int(self.rule_weighted_cells),
                    end_cell_outside_8=int(self.rule_end_outside_cells))

    def path_stats(self):
        hist = self.path_hist
        total = int(hist.sum())
        used = np.nonzero(hist)[0]
        top = int(used.max()) if len(used) else 0
        return dict(cells=total, moved_ge1=int(hist[1:].sum()), frac_moved_ge1=float(hist[1:].sum() / total) if total else 0.0,
                    max_moves=top, cap_hits=int(self.path_cap_hits), hist=[int(v) for v in hist[:top + 1]])

    def _round_wind16(self):
        for name in ("u", "v", "w"):
            setattr(self, name, getattr(self, name).astype(np.float16).astype(F32))

    def _advect_state(self, newu, newv, neww, centers, dep, wd, band):
        """식 3판 옮기기(덧붙임 2절): d_새 = [d(x_s) + (x − x_s)]·되돌림(가로 0.1, 높이 0.3), g_새 = g(x_s)·exp(−0.3 dt)."""
        p = self.p
        dt = p.dt
        state = (self.dpx, self.dpy, self.dpz, self.g)
        if p.solid in FILL_SOLIDS:
            state = tuple(self.fill_solids(a) for a in state)
        sx, sy, sz, sg = self._sample_state(*dep, state)
        eh = math.exp(-p.disp_relax_h_per_sec * dt)
        ev = math.exp(-p.disp_relax_v_per_sec * dt)
        eg = math.exp(-p.fill_relax_per_sec * dt)
        X, Y, Z = centers
        new = [(sx + (X - dep[0])) * eh, (sy + (Y - dep[1])) * eh, (sz + (Z - dep[2])) * ev, sg * eg]
        if self.fresh.any():
            keep = ~self.fresh[:, :, None]
            newu, newv, neww = newu * keep, newv * keep, neww * keep
            new = [a * keep for a in new]
        self.u = (newu * wd * band).astype(F32)
        self.v = (newv * wd * band).astype(F32)
        self.w = (neww * wd * band).astype(F32)
        self.dpx, self.dpy, self.dpz, self.g = [(a * band).astype(F32) for a in new]

    def _zero_solid_state(self):
        for name in ("dpx", "dpy", "dpz", "g"):
            getattr(self, name)[self.solid] = 0

    def base_at(self, offx, offy, z):
        """평소 농도 B(칸 가운데 + 가로 어긋남, 높이 z). 얼룩(두께)·잔무늬(농도 ±)는 그 자리에서 읽는다. 32비트."""
        p = self.p
        wi, wj = self.world_cells()
        if p.stain_amp != 0.0:
            n = noise_at(wi, wj, offx, offy, p.hx, p.stain_spacing_cm, p.stain_seed)
            T = (p.thickness_cm * (1.0 + p.stain_amp * (2.0 * n - 1.0))).astype(F32)
        else:
            T = F32(p.thickness_cm)
        B = baseline32(p, z, T)
        if p.fine_amp != 0.0:
            nf = noise_at(wi, wj, offx, offy, p.hx, p.fine_spacing_cm, p.fine_seed)
            B = (B.astype(np.float64) * (1.0 + p.fine_amp * (2.0 * nf - 1.0))).astype(F32)
        return B

    def center_total_v3(self):
        """3판 칸 가운데의 그려지는 농도 max(0, 1+g)·B(칸 가운데 − d)."""
        p = self.p
        z = self.zc[None, None, :] - self.dpz.astype(np.float64)
        if p.stain_amp != 0.0 or p.fine_amp != 0.0:
            B = self.base_at(-self.dpx.astype(np.float64), -self.dpy.astype(np.float64), z)
        else:
            B = baseline32(p, z, self.T[:, :, None])
        return np.maximum(0.0, 1.0 + self.g.astype(np.float64)) * B.astype(np.float64)

    def refresh_diff(self):
        """3판의 판정용 차이값 D = max(0,1+g)·B(칸 가운데 − d) − B(칸 가운데) (덧붙임 2절). 막힌 칸 0."""
        if not self.v3:
            return
        tot = self.center_total_v3()
        self.D = np.where(self.fluid, tot - self.Bc.astype(np.float64), 0.0).astype(F32)

    # ---------- 입력 ----------
    def _bbox(self, a, b, r):
        p = self.p
        lo = np.minimum(a, b) - r
        hi = np.maximum(a, b) + r
        i0 = max(0, int(math.floor(lo[0] / p.hx)) - 1)
        i1 = min(self.nx, int(math.ceil(hi[0] / p.hx)) + 1)
        j0 = max(0, int(math.floor(lo[1] / p.hy)) - 1)
        j1 = min(self.ny, int(math.ceil(hi[1] / p.hy)) + 1)
        return slice(i0, max(i0, i1)), slice(j0, max(j0, j1))

    def location(self, kind):
        """kind: 'u','v','w','c'. 반환 (x, y, z, 세로 범위, 부피) 지역 cm, 각 (nx,ny,nz) 로 넓혀 쓸 수 있는 꼴."""
        p = self.p
        X, Y = self.xc[:, None, None], self.yc[None, :, None]
        Z, R = self.zc[None, None, :], self.dz[None, None, :]
        if kind == "u":
            X = self.xf[:, None, None]
        elif kind == "v":
            Y = self.yf[None, :, None]
        elif kind == "w":
            Z, R = self.zt[None, None, :], self.wrange[None, None, :]
        return X, Y, Z, R, p.hx * p.hy * R

    def _apply_inputs(self, inputs):
        p = self.p
        erase = np.zeros(self.D.shape)
        any_erase = False
        for inp in inputs:
            a = self.to_local(inp.a)
            b = self.to_local(inp.b)
            r = float(inp.radius)
            si, sj = self._bbox(a, b, r)
            if si.start >= si.stop or sj.start >= sj.stop:
                continue
            for comp, name in enumerate(("u", "v", "w")):
                X, Y, Z, R, _ = self.location(name)
                W = band_weight(X[si], Y[:, sj], Z, R, a, b, r) * float(inp.strength)
                arr = getattr(self, name)
                blk = arr[si, sj].astype(np.float64)
                arr[si, sj] = (blk + (float(inp.velocity[comp]) - blk) * W).astype(F32)
            if inp.erase_per_sec > 0:
                X, Y, Z, R, _ = self.location("c")
                amount = dwell_amount(X[si], Y[:, sj], Z, R, a, b, r, p.dt)
                erase[si, sj] += inp.erase_per_sec * amount
                any_erase = True
        if any_erase and self.v3:
            g = self.g.astype(np.float64)
            hit = erase > 0
            self.g = np.where(hit, np.maximum(0.0, 1.0 + g) * np.exp(-erase) - 1.0, g).astype(F32)
        elif any_erase:
            bc = self.Bc.astype(np.float64)
            total = np.maximum(bc + self.D, 0.0) * np.exp(-erase)
            self.D = (total - bc).astype(F32)

    # ---------- 압력 ----------
    def divergence(self, u=None, v=None, w=None):
        u = self.u if u is None else u
        v = self.v if v is None else v
        w = self.w if w is None else w
        p = self.p
        u = u.astype(np.float64)
        v = v.astype(np.float64)
        w = w.astype(np.float64)
        um = np.zeros_like(u)
        um[1:] = u[:-1]
        vm = np.zeros_like(v)
        vm[:, 1:] = v[:, :-1]
        wm = np.zeros_like(w)
        wm[:, :, 1:] = w[:, :, :-1]
        div = (u - um) / p.hx + (v - vm) / p.hy + (w - wm) / self.dz[None, None, :]
        return np.where(self.fluid, div, 0.0)

    def levels(self, count):
        if self._levels is None:
            p = self.p
            t = lambda a: np.ascontiguousarray(np.transpose(a, (2, 0, 1))).astype(np.float64)
            fine = PLevel(p.hx, p.hy, self.dz, self.zdist, t(self.ou), t(self.ov), t(self.ow), self.ow[:, :, -1].astype(np.float64),
                          np.ascontiguousarray(np.transpose(self.fluid, (2, 0, 1))), 1.0, p.pivot_guard)
            self.tiny_pivot_count = fine.tiny_pivot_count
            self._levels = build_levels(fine, 99)
        return self._levels[:count]

    def solve_pressure(self, b, x0, cfg):
        """b, x0: (nx,ny,nz). 반환 (압력, 비용, 패스 수, 순환 수)."""
        store = _store32 if self.p.pressure_store32 else _store64
        kind = cfg["kind"]
        if kind == "galerkin":
            return self._solve_p7(b, x0, cfg, store)
        tb = np.ascontiguousarray(np.transpose(b, (2, 0, 1)))
        x = np.ascontiguousarray(np.transpose(x0, (2, 0, 1))).astype(np.float64)
        zero = not np.any(x)
        cost = 0.0
        passes = 0
        cycles = 0
        if kind == "line":
            L = self.levels(1)[0]
            for _ in range(int(cfg["sweeps"])):
                x = L.sweep(tb, x, cfg["omega"], store)
                cost += 1.0
                passes += 2
        elif kind == "mg":
            levels = self.levels(int(cfg["levels"]))
            nu1, nu2, nc, om = int(cfg["nu1"]), int(cfg["nu2"]), int(cfg["nc"]), float(cfg["omega"])
            budget = float(cfg["budget"])
            while True:
                cc = cycle_cost(levels, 0, nu1, nu2, nc, zero)
                if cost + cc > budget + 1e-9:
                    break
                passes += cycle_passes(levels, 0, nu1, nu2, nc, zero)
                x = vcycle(levels, 0, tb, x, nu1, nu2, nc, om, store, zero)
                cost += cc
                cycles += 1
                zero = False
            while cost + 1.0 <= budget + 1e-9:
                x = levels[0].sweep(tb, x, om, store)
                cost += 1.0
                passes += 2
        elif kind == "ref":
            levels = self.levels(int(cfg["levels"]))
            L0 = levels[0]
            nu1, nu2, nc, om = int(cfg["nu1"]), int(cfg["nu2"]), int(cfg["nc"]), float(cfg["omega"])
            vol = (self.p.hx * self.p.hy * self.dz)[:, None, None]
            live = L0.live
            nb = rms((tb / vol)[live]) + 1e-300
            target = 0.5 * self.p.ref_tol
            for _ in range(int(cfg["max_cycles"])):
                res = (tb - L0.apply(x)) / vol
                if rms(res[live]) / nb <= target:
                    break
                passes += cycle_passes(levels, 0, nu1, nu2, nc, zero)
                cost += cycle_cost(levels, 0, nu1, nu2, nc, zero)
                x = vcycle(levels, 0, tb, x, nu1, nu2, nc, om, store, zero)
                cycles += 1
                zero = False
        else:
            raise ValueError(kind)
        return np.transpose(x, (1, 2, 0)), cost, passes, cycles

    def sparse_matrix(self):
        import scipy.sparse as sp
        L = self.levels(1)[0]
        nz, nx, ny = L.shape
        fl = self.fluid
        idx = -np.ones(fl.shape, np.int64)
        idx[fl] = np.arange(int(fl.sum()))
        cx = np.transpose(L.cx, (1, 2, 0))
        cy = np.transpose(L.cy, (1, 2, 0))
        cz = np.transpose(L.cz, (1, 2, 0))
        diag = np.transpose(L.diag, (1, 2, 0))
        rows, cols, vals = [idx[fl]], [idx[fl]], [diag[fl]]
        for c, a, b in ((cx[:-1], idx[:-1], idx[1:]), (cy[:, :-1], idx[:, :-1], idx[:, 1:]), (cz[:, :, :-1], idx[:, :, :-1], idx[:, :, 1:])):
            m = (c > 0) & (a >= 0) & (b >= 0)
            rows += [a[m], b[m]]
            cols += [b[m], a[m]]
            vals += [-c[m], -c[m]]
        n = int(fl.sum())
        return sp.csr_matrix((np.concatenate(vals), (np.concatenate(rows), np.concatenate(cols))), shape=(n, n))

    def _solve_p7(self, b, x0, cfg, store):
        if not np.allclose(self.dz, 30.0) or self.nz != 8:
            raise ValueError("P7 는 U8 에서만")
        if self._p7 is None:
            sys.path.insert(0, os.path.join(ROOT, "Tools", "scratch", "fog3d", "stage1"))
            import fog3d_ref_ops as ops1
            g = ops1.Grid(self.nx, self.ny, self.nz, (self.p.hx, self.p.hy, 30.0), "open", "wrap")
            g.type[:, :, :self.nz] = np.where(self.solid, ops1.SOLID, ops1.FLUID)
            g.refresh()
            self._p7 = ops1.Solver(self.sparse_matrix(), g)
        fl = self.fluid
        xin = x0[fl].astype(np.float64)
        x, cost = self._p7.solve(cfg["method"], b[fl].astype(np.float64), xin if np.any(xin) else None, float(cfg["budget"]), 1.0, store)
        out = np.zeros(b.shape)
        out[fl] = x
        return out, cost, None, None

    def _project(self):
        p = self.p
        div0 = self.divergence()
        vol = (p.hx * p.hy * self.dz)[None, None, :]
        b = -vol * div0
        x0 = self.pr.astype(np.float64) if p.warm_start else np.zeros(b.shape)
        pr, cost, passes, cycles = self.solve_pressure(b, x0, self.cfg)
        pr = np.where(self.fluid, pr, 0.0)
        self.pr = pr.astype(F32)
        self.subtract_gradient(self.pr.astype(np.float64))
        div1 = self.divergence()
        before = rms(div0[self.fluid])
        after = rms(div1[self.fluid])
        self.last = dict(R=after / before if before > 0 else 0.0, div_before=before, div_after=after, cost=cost,
                         passes=passes, cycles=cycles)

    def subtract_gradient(self, pr):
        p = self.p
        u = self.u.astype(np.float64)
        v = self.v.astype(np.float64)
        w = self.w.astype(np.float64)
        u[:-1] -= (pr[1:] - pr[:-1]) / p.hx * self.ou[:-1]
        v[:, :-1] -= (pr[:, 1:] - pr[:, :-1]) / p.hy * self.ov[:, :-1]
        w[:, :, :-1] -= (pr[:, :, 1:] - pr[:, :, :-1]) / self.zdist[None, None, :] * self.ow[:, :, :-1]
        w[:, :, -1] -= (0.0 - pr[:, :, -1]) / self.dz[-1] * self.ow[:, :, -1]
        self.u = (u * self.ou).astype(F32)
        self.v = (v * self.ov).astype(F32)
        self.w = (w * self.ow).astype(F32)

    def zero_closed_faces(self):
        self.u *= self.ou
        self.v *= self.ov
        self.w *= self.ow

    # ---------- 한 번 계산 ----------
    def step(self, inputs=(), follow=None):
        p = self.p
        if follow is not None:
            self.follow(follow)
        if p.mode == "nowind":
            if self.v3:
                self.g = (self.g.astype(np.float64) * math.exp(-p.fill_relax_per_sec * p.dt)).astype(F32)
            else:
                self.D = (self.D.astype(np.float64) * math.exp(-p.diff_relax_per_sec * p.dt)).astype(F32)
            self._apply_inputs([_erase_only(i) for i in inputs])
            self.D[self.solid] = 0
            self._finish()
            return
        self._advect()
        self._apply_inputs(inputs)
        self.zero_closed_faces()
        if p.emu_gpu:
            self._round_wind16()
        if p.diff_formula == 1:
            self.D = np.maximum(self.D, -self.Bc)
        if p.pressure:
            self._project()
        if p.emu_gpu:
            self._round_wind16()
        self.D[self.solid] = 0
        self._finish()

    def _finish(self):
        p = self.p
        names = ("u", "v", "w", "dpx", "dpy", "dpz", "g") if self.v3 else ("u", "v", "w", "D")
        if self.v3:
            self._zero_solid_state()
        if p.half_store:
            for name in names:
                setattr(self, name, getattr(self, name).astype(np.float16).astype(F32))
            self.pr = ((self.pr / F32(p.hx)).astype(np.float16).astype(F32) * F32(p.hx)).astype(F32)
        self.refresh_diff()
        self.overlap = (0, self.nx - 1, 0, self.ny - 1)
        self.fresh = np.zeros((self.nx, self.ny), bool)
        self.time += p.dt
        self.steps += 1

    # ---------- 출력·측정 ----------
    def total_density(self):
        if self.v3:
            return self.center_total_v3()
        return np.maximum(self.Bc.astype(np.float64) + self.D, 0.0)

    def interp_state_z(self, heights):
        """3판: d·g 를 층 가운데 사이 높이로 선형 보간(양 끝은 끝값). 반환 (dpx, dpy, dpz, g) 각 (nx,ny,len)."""
        h = np.clip(np.asarray(heights, float), self.zc[0], self.zc[-1])
        out = []
        for a in (self.dpx, self.dpy, self.dpz, self.g):
            a = a.astype(np.float64)
            if self.nz == 1:
                out.append(np.repeat(a, len(h), axis=2))
                continue
            k0 = np.clip(np.searchsorted(self.zc, h, side="right") - 1, 0, self.nz - 2)
            t = (h - self.zc[k0]) / (self.zc[k0 + 1] - self.zc[k0])
            out.append(a[:, :, k0] * (1 - t) + a[:, :, k0 + 1] * t)
        return out

    def drawn_density_v3(self, heights):
        """덧붙임 2절: ρ = max(0, 1 + g̃)·B(x − d̃), 높이는 0 이상으로 자름, 얼룩·잔무늬는 원래 자리 X 에서."""
        h = np.asarray(heights, float)
        px, py, pz, g = self.interp_state_z(h)
        z = h[None, None, :] - pz
        p = self.p
        if p.stain_amp != 0.0 or p.fine_amp != 0.0:
            B = self.base_at(-px, -py, z)
        else:
            B = baseline32(p, z, self.T[:, :, None])
        return np.maximum(0.0, 1.0 + g) * B.astype(np.float64)

    def set_empty(self, mask):
        """판정용 비우기: 2판 D = −B, 3판 g = −1·d = 0 (mask 의 유체 칸)."""
        m = mask & self.fluid
        if self.v3:
            self.g = np.where(m, -1.0, self.g).astype(F32)
            for name in ("dpx", "dpy", "dpz"):
                setattr(self, name, np.where(m, 0.0, getattr(self, name)).astype(F32))
            self.refresh_diff()
        else:
            self.D = np.where(m, -self.Bc, self.D).astype(F32)

    def reset_fields(self):
        for name in ("u", "v", "w", "D", "dpx", "dpy", "dpz", "g"):
            getattr(self, name)[...] = 0

    def state_max(self):
        """3판 상태 d·g 의 절댓값 최대(정보)."""
        return dict(d=max(float(np.abs(a).max()) for a in (self.dpx, self.dpy, self.dpz)), g=float(np.abs(self.g).max()))

    def interp_diff_z(self, heights):
        h = np.clip(np.asarray(heights, float), self.zc[0], self.zc[-1])
        k0 = np.clip(np.searchsorted(self.zc, h, side="right") - 1, 0, max(self.nz - 2, 0))
        if self.nz == 1:
            return np.repeat(self.D.astype(np.float64), len(h), axis=2)
        t = (h - self.zc[k0]) / (self.zc[k0 + 1] - self.zc[k0])
        D = self.D.astype(np.float64)
        return D[:, :, k0] * (1 - t) + D[:, :, k0 + 1] * t

    def drawn_density(self, heights):
        h = np.asarray(heights, float)
        base = baseline32(self.p, h[None, None, :], self.T[:, :, None]).astype(np.float64)
        return base + self.interp_diff_z(h)

    def blocked_face_max(self):
        return max(float(np.abs(self.u[~self.ou]).max(initial=0.0)), float(np.abs(self.v[~self.ov]).max(initial=0.0)),
                   float(np.abs(self.w[~self.ow]).max(initial=0.0)))

    def seam_face_max(self):
        return max(float(np.abs(self.u[-1]).max()), float(np.abs(self.v[:, -1]).max()))

    def wind_energy(self):
        p = self.p
        e = 0.0
        for name in ("u", "v", "w"):
            *_, vol = self.location(name)
            e += float((0.5 * getattr(self, name).astype(np.float64) ** 2 * vol).sum())
        return e

    def max_wind(self):
        return max(float(np.abs(a).max()) for a in (self.u, self.v, self.w))

    def state_hash(self, with_pressure=False, canonical=None):
        if canonical is None:
            canonical = self.p.canonical_hash
        m = hashlib.sha256()
        state = (self.u, self.v, self.w, self.dpx, self.dpy, self.dpz, self.g) if self.v3 else (self.u, self.v, self.w, self.D)
        for a in state + ((self.pr,) if with_pressure else ()):
            a = np.asarray(a, F32)
            if canonical:
                a = np.where(self.solid, F32(0), a) + F32(0)
            m.update(np.ascontiguousarray(a, F32).tobytes())
        return m.hexdigest()[:16]

    def dump(self, path):
        os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
        meta = json.dumps(dict(params=asdict(self.p), origin=self.origin, anchor=self.anchor, time=self.time, steps=self.steps,
                               cfg=self.cfg), ensure_ascii=False, default=str)
        extra = dict(disp=np.stack([self.dpx, self.dpy, self.dpz]), g=self.g) if self.v3 else {}
        np.savez_compressed(path, u=self.u, v=self.v, w=self.w, D=self.D, p=self.pr, solid=self.solid, dz=self.dz,
                            T=self.T, meta=np.array(meta), **extra)

    def set_wind(self, vec):
        for comp, name in enumerate(("u", "v", "w")):
            arr = getattr(self, name)
            arr[...] = F32(vec[comp])
        self.zero_closed_faces()

    def empty_all(self):
        if self.v3:
            self.set_empty(np.ones(self.D.shape, bool))
            return
        self.D = np.where(self.fluid, -self.Bc, 0.0).astype(F32)


def _erase_only(inp):
    return SweptSphere(inp.a, inp.b, inp.radius, (0.0, 0.0, 0.0), 0.0, inp.erase_per_sec)


def _neighbor_pairs(shape):
    nx, ny, nz = shape
    full = slice(None)
    out = []
    for axis, n in ((0, nx), (1, ny), (2, nz)):
        for s in (1, -1):
            src = [full, full, full]
            dst = [full, full, full]
            if s == 1:
                src[axis] = slice(1, n)
                dst[axis] = slice(0, n - 1)
            else:
                src[axis] = slice(0, n - 1)
                dst[axis] = slice(1, n)
            out.append((tuple(src), tuple(dst)))
    return out


# ---------------- 풀이 설정 ----------------

def layers_name(layers):
    if isinstance(layers, str):
        return layers
    for k, v in LAYER_TABLES.items():
        if tuple(float(x) for x in layers) == v:
            return k
    return "custom"


def load_tuning():
    try:
        with open(TUNING_PATH, encoding="utf-8") as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return {}


def resolve_solver_cfg(params):
    name = params.solver
    cfg = dict(SOLVER_DEFAULTS[name])
    tuned = load_tuning().get(layers_name(params.layers), {}).get(name)
    if tuned:
        cfg.update({k: v for k, v in tuned.items() if k in cfg})
    if params.solver_cfg:
        cfg.update(params.solver_cfg)
    return cfg


def t_test_sim(layers, n, test, **kw):
    spec = T_TESTS[test]
    boxes = [T3_BOX] if spec["box"] else []
    params = Params(nx=n, ny=n, layers=layers, pressure=False, **kw)
    sim = Sim(params, boxes=boxes)
    sim._apply_inputs([still_sphere((0.0, 0.0, spec["z"]), 50.0, spec["vel"])])
    sim.zero_closed_faces()
    return sim


def t_test_R(sim_template, cfg):
    sim = sim_template
    u0, v0, w0 = sim.u.copy(), sim.v.copy(), sim.w.copy()
    div0 = sim.divergence()
    vol = (sim.p.hx * sim.p.hy * sim.dz)[None, None, :]
    pr, cost, passes, cycles = sim.solve_pressure(-vol * div0, np.zeros(div0.shape), cfg)
    pr = np.where(sim.fluid, pr, 0.0).astype(F32)
    sim.subtract_gradient(pr.astype(np.float64))
    R = rms(sim.divergence()[sim.fluid]) / rms(div0[sim.fluid])
    blocked = sim.blocked_face_max()
    sim.u, sim.v, sim.w = u0, v0, w0
    return dict(R=R, cost=cost, passes=passes, cycles=cycles, blocked=blocked)


def tune_on_T0(layers, n=128):
    sim = t_test_sim(layers, n, "T0")
    out = {}
    for name in ("P1", "P2", "P3"):
        base = dict(SOLVER_DEFAULTS[name])
        scores = {}
        for om in [round(1.0 + 0.1 * i, 1) for i in range(10)]:
            scores[om] = t_test_R(sim, dict(base, omega=om))["R"]
        best = min(scores, key=lambda k: (scores[k], k))
        out[name] = dict(omega=best, R_T0=scores[best], all={str(k): v for k, v in scores.items()})
    grids = {
        "P4": dict(nu=[(0, 1), (1, 1), (0, 2), (1, 2), (2, 1), (2, 2)], nc=[1, 2, 4, 8], omega=[1.0, 1.2]),
        "P5": dict(nu=[(0, 1), (1, 1), (0, 2), (1, 2), (2, 1), (2, 2)], nc=[2, 4, 8, 16], omega=[1.0, 1.2]),
        "P6": dict(nu=[(1, 1)], nc=[4, 8, 16, 30], omega=[1.0, 1.2]),
    }
    for name, g in grids.items():
        base = dict(SOLVER_DEFAULTS[name])
        rows = []
        for nu1, nu2 in g["nu"]:
            for nc in g["nc"]:
                for om in g["omega"]:
                    cfg = dict(base, nu1=nu1, nu2=nu2, nc=nc, omega=om)
                    res = t_test_R(sim, cfg)
                    if res["cycles"] == 0:
                        continue
                    rows.append(dict(nu1=nu1, nu2=nu2, nc=nc, omega=om, R_T0=res["R"], cost=res["cost"], cycles=res["cycles"],
                                     passes=res["passes"]))
        rows.sort(key=lambda r: (r["R_T0"], r["cost"], r["nu1"] + r["nu2"], r["nc"], r["omega"]))
        best = rows[0]
        out[name] = dict(nu1=best["nu1"], nu2=best["nu2"], nc=best["nc"], omega=best["omega"], R_T0=best["R_T0"],
                         cost=best["cost"], cycles=best["cycles"], passes=best["passes"], tried=len(rows))
    return out


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "tune":
        data = load_tuning()
        for lay in sys.argv[2:]:
            data[lay] = tune_on_T0(lay)
            data[lay]["_note"] = "T0(높이 105cm, 바람 (300,200,-250), 구 50cm, 128x128) 처음 풀기 R 최소. 판정 입력 아님."
        os.makedirs(os.path.dirname(TUNING_PATH), exist_ok=True)
        with open(TUNING_PATH, "w", encoding="utf-8") as fh:
            json.dump(data, fh, indent=1, ensure_ascii=False)
        print(json.dumps({k: {n: {kk: vv for kk, vv in c.items() if kk != "all"} for n, c in v.items() if not n.startswith("_")} for k, v in data.items()}, ensure_ascii=False))
