"""
실행 환경: Python 3.12 + numpy 2.2 + scipy 1.17 (추가 설치 없음), 저장소 루트에서 실행. 같은 폴더의 잠긴 fog3d_core.py·fog3d_core_v2.py 를 import 만 한다
실행 명령: 직접 실행하지 않는다. fog3d_gates_s2a.py 가 import 한다
출력: 없음
상태: 2a 땅 높이 따라가기 후보 R0~R3(Docs/VolumeFog3D/stage2/prereg-2a-design.md 1절·3절) 기준 모의, 2026-11-15, 등록 잠금 전 시운전판
"""
import math
from dataclasses import dataclass
from typing import Callable, Optional

import numpy as np

import fog3d_core as core
import fog3d_core_v2 as core2
from fog3d_core import F32

WALL_STEP_CM = 30.0
CANDIDATE_SETTINGS = {
    "R0": dict(follows_terrain=False, uses_walls=False, uses_top_transform=False, rounds_in_ground_frame=False),
    "R1": dict(follows_terrain=True, uses_walls=False, uses_top_transform=False, rounds_in_ground_frame=False),
    "R2": dict(follows_terrain=True, uses_walls=True, uses_top_transform=False, rounds_in_ground_frame=False),
    "R3": dict(follows_terrain=True, uses_walls=True, uses_top_transform=True, rounds_in_ground_frame=False),
    "R3G": dict(follows_terrain=True, uses_walls=True, uses_top_transform=True, rounds_in_ground_frame=True),
}


@dataclass
class Terrain:
    name: str
    height: Callable
    signed_cliff_distance: Optional[Callable] = None


def _broadcast_height(value, x, y):
    return np.broadcast_to(np.asarray(value, float), np.broadcast(np.asarray(x, float), np.asarray(y, float)).shape).copy()


def value_noise(u, v, seed):
    u = np.asarray(u, float)
    v = np.asarray(v, float)
    fu, fv = np.floor(u), np.floor(v)
    iu, iv = fu.astype(np.int64), fv.astype(np.int64)
    su = core.smoothstep(0.0, 1.0, u - fu)
    sv = core.smoothstep(0.0, 1.0, v - fv)
    n00 = core._hash01(iu, iv, seed)
    n10 = core._hash01(iu + 1, iv, seed)
    n01 = core._hash01(iu, iv + 1, seed)
    n11 = core._hash01(iu + 1, iv + 1, seed)
    return (n00 * (1 - su) + n10 * su) * (1 - sv) + (n01 * (1 - su) + n11 * su) * sv


def flat(name, level):
    return Terrain(name, lambda x, y: _broadcast_height(level, x, y))


def slope(name, gradient):
    return Terrain(name, lambda x, y: _broadcast_height(gradient * np.asarray(x, float), x, y))


def step_x(name, rise):
    return Terrain(name, lambda x, y: _broadcast_height(np.where(np.asarray(x, float) >= 0.0, rise, 0.0), x, y),
                   lambda x, y: _broadcast_height(np.asarray(x, float), x, y))


def hill_height(x, y):
    x, y = np.asarray(x, float), np.asarray(y, float)
    return 150.0 * np.sin(2.0 * math.pi * x / 1000.0) * np.cos(2.0 * math.pi * y / 1400.0)


def diag_height(x, y):
    return _broadcast_height(np.where(np.asarray(x, float) + np.asarray(y, float) >= 0.0, 450.0, 0.0), x, y)


def diag_distance(x, y):
    return (np.asarray(x, float) + np.asarray(y, float)) / math.sqrt(2.0)


def mesa_height(x, y):
    return np.where(np.hypot(np.asarray(x, float), np.asarray(y, float)) < 400.0, 450.0, 0.0)


def mesa_distance(x, y):
    return 400.0 - np.hypot(np.asarray(x, float), np.asarray(y, float))


def rough_height(x, y):
    x, y = np.asarray(x, float), np.asarray(y, float)
    return 400.0 * value_noise(x / 250.0, y / 250.0, 7) + 60.0 * np.floor(3.0 * value_noise(x / 500.0, y / 500.0, 8))


TERRAINS = {
    "F0": flat("F0", 0.0),
    "F1": flat("F1", 137.5),
    "S10": slope("S10", math.tan(math.radians(10.0))),
    "S20": slope("S20", math.tan(math.radians(20.0))),
    "S30": slope("S30", math.tan(math.radians(30.0))),
    "S45": slope("S45", math.tan(math.radians(45.0))),
    "S60": slope("S60", 29.0 / 15.625),
    "HILL": Terrain("HILL", hill_height),
    "CLIFF": step_x("CLIFF", 450.0),
    "DIAG": Terrain("DIAG", diag_height, diag_distance),
    "MESA": Terrain("MESA", mesa_height, mesa_distance),
    "CURB30": step_x("CURB30", 30.0),
    "CURB45": step_x("CURB45", 45.0),
    "CURB60": step_x("CURB60", 60.0),
    "ROUGH": Terrain("ROUGH", rough_height),
}


def sampled_terrain(terrain, spacing_cm):
    def height(x, y):
        x, y = np.asarray(x, float), np.asarray(y, float)
        fx, fy = x / spacing_cm, y / spacing_cm
        ix, iy = np.floor(fx), np.floor(fy)
        tx, ty = fx - ix, fy - iy
        h00 = terrain.height(ix * spacing_cm, iy * spacing_cm)
        h10 = terrain.height((ix + 1) * spacing_cm, iy * spacing_cm)
        h01 = terrain.height(ix * spacing_cm, (iy + 1) * spacing_cm)
        h11 = terrain.height((ix + 1) * spacing_cm, (iy + 1) * spacing_cm)
        return (h00 * (1 - tx) + h10 * tx) * (1 - ty) + (h01 * (1 - tx) + h11 * tx) * ty
    return Terrain(f"{terrain.name}@{spacing_cm:g}", height, terrain.signed_cliff_distance)


def steep_columns(padded_height, step_cm=WALL_STEP_CM):
    center = padded_height[1:-1, 1:-1]
    neighbors = (padded_height[2:, 1:-1], padded_height[:-2, 1:-1], padded_height[1:-1, 2:], padded_height[1:-1, :-2])
    steep = np.zeros(center.shape, bool)
    for neighbor in neighbors:
        steep |= center - neighbor > step_cm
    return steep


def _axis_slope(lower, center, upper, spacing, step_cm):
    rise_up = upper - center
    rise_down = center - lower
    up_ok = np.abs(rise_up) <= step_cm
    down_ok = np.abs(rise_down) <= step_cm
    both = (upper - lower) / (2.0 * spacing)
    return np.where(up_ok & down_ok, both, np.where(up_ok, rise_up / spacing, np.where(down_ok, rise_down / spacing, 0.0)))


def column_slopes(padded_height, hx, hy, step_cm=WALL_STEP_CM):
    center = padded_height[1:-1, 1:-1]
    sx = _axis_slope(padded_height[:-2, 1:-1], center, padded_height[2:, 1:-1], hx, step_cm)
    sy = _axis_slope(padded_height[1:-1, :-2], center, padded_height[1:-1, 2:], hy, step_cm)
    return sx, sy


def padded_column_centers(rx, ry, hx, hy):
    ex = np.concatenate([[rx[0] - hx], rx, [rx[-1] + hx]])
    ey = np.concatenate([[ry[0] - hy], ry, [ry[-1] + hy]])
    return ex, ey


class TerrainSim(core2.Sim):

    def __init__(self, params, terrain, candidate="R3", anchor=(0, 0), window_min=None, boxes=(), obstacles=()):
        settings = CANDIDATE_SETTINGS[candidate]
        self.terrain = terrain
        self.candidate = candidate
        self.follows_terrain = settings["follows_terrain"]
        self.uses_walls = settings["uses_walls"]
        self.uses_top_transform = settings["uses_top_transform"]
        self.rounds_in_ground_frame = settings["rounds_in_ground_frame"]
        self.terrain_boxes = [(np.asarray(lo, float), np.asarray(hi, float)) for lo, hi in boxes]
        self.terrain_obstacles = [dict(o) for o in obstacles]
        self.top_target_max = 0.0
        super().__init__(params, anchor=anchor, window_min=window_min, boxes=(), obstacles=(dict(kind="terrain"),))

    def _geometry(self):
        self._build_columns()
        super()._geometry()

    def _build_columns(self):
        p = self.p
        rx, ry, _ = self.center_rel()
        ex, ey = padded_column_centers(rx, ry, p.hx, p.hy)
        if self.follows_terrain:
            padded = np.broadcast_to(np.asarray(self.terrain.height(ex[:, None], ey[None, :]), float), (len(ex), len(ey))).copy()
        else:
            padded = np.zeros((len(ex), len(ey)))
        self.padded_height = padded
        self.column_height = padded[1:-1, 1:-1]
        self.u_face_height = 0.5 * (padded[1:-1, 1:-1] + padded[2:, 1:-1])
        self.v_face_height = 0.5 * (padded[1:-1, 1:-1] + padded[1:-1, 2:])
        self.slope_x, self.slope_y = column_slopes(padded, p.hx, p.hy)
        self.wall_columns = steep_columns(padded) if self.uses_walls else np.zeros(self.column_height.shape, bool)

    def world_height(self, kind):
        if kind == "u":
            return self.u_face_height[:, :, None] + self.zc[None, None, :]
        if kind == "v":
            return self.v_face_height[:, :, None] + self.zc[None, None, :]
        if kind == "w":
            return self.column_height[:, :, None] + self.zt[None, None, :]
        return self.column_height[:, :, None] + self.zc[None, None, :]

    def _obstacle_mask(self, ob, rx, ry):
        if ob["kind"] != "terrain":
            return super()._obstacle_mask(ob, rx, ry)
        shape = (self.nx, self.ny, self.nz)
        zc = self.world_height("c")
        mask = np.broadcast_to(self.wall_columns[:, :, None], shape).copy()
        for lo, hi in self.terrain_boxes:
            if self.p.inflate:
                lo, hi = self._inflate_aabb(lo, hi)
            mx = (rx > lo[0]) & (rx < hi[0])
            my = (ry > lo[1]) & (ry < hi[1])
            mz = (zc > lo[2]) & (zc < hi[2])
            mask |= mx[:, None, None] & my[None, :, None] & mz
        for other in self.terrain_obstacles:
            mask |= self._world_obstacle_mask(other, rx, ry, zc)
        return mask

    def _world_obstacle_mask(self, ob, rx, ry, zc):
        if ob["kind"] == "cells":
            return super()._obstacle_mask(ob, rx, ry)
        c = np.asarray(ob["center"], float)
        dx = (rx - c[0])[:, None, None]
        dy = (ry - c[1])[None, :, None]
        dz = zc - c[2]
        if ob["kind"] == "sphere":
            r = float(ob["radius"])
            if self.p.inflate:
                r = max(r, core.INFLATE_SPHERE_CM)
            return dx * dx + dy * dy + dz * dz < r * r
        if ob["kind"] != "box":
            raise ValueError(ob["kind"])
        half = [float(h) for h in ob["half"]]
        if self.p.inflate:
            half = [core.inflate_half(half[0], 0.0), core.inflate_half(half[1], 0.0), core.inflate_half(half[2], 1.0)]
        yaw = math.radians(float(ob.get("yaw_deg", 0.0)))
        ca, sa = math.cos(yaw), math.sin(yaw)
        along1 = dx * ca + dy * sa
        along2 = -dx * sa + dy * ca
        return (np.abs(along1) < half[0]) & (np.abs(along2) < half[1]) & (np.abs(dz) < half[2])

    def _wind_target(self, comp, velocity, si, sj, slope_at_center=None):
        if comp < 2 or not self.uses_top_transform:
            return float(velocity[comp])
        vx, vy, vz = float(velocity[0]), float(velocity[1]), float(velocity[2])
        if slope_at_center is not None:
            return vz - vx * slope_at_center[0] - vy * slope_at_center[1]
        return (vz - vx * self.slope_x[si, sj] - vy * self.slope_y[si, sj])[:, :, None]

    def _record_top_target(self, target, weight):
        touched = weight > 0
        if not touched.any():
            return
        values = np.abs(np.broadcast_to(np.asarray(target, float), weight.shape))[touched]
        self.top_target_max = max(self.top_target_max, float(values.max()))

    def _input_frame(self, inp):
        if not self.rounds_in_ground_frame:
            return self.to_local(inp.a), self.to_local(inp.b), None
        ga = float(self.drawn_ground(inp.a[0], inp.a[1]))
        gb = float(self.drawn_ground(inp.b[0], inp.b[1]))
        a = self.to_local((inp.a[0], inp.a[1], inp.a[2] - ga))
        b = self.to_local((inp.b[0], inp.b[1], inp.b[2] - gb))
        slope = (float(self.sample_columns(self.slope_x, inp.b[0], inp.b[1])), float(self.sample_columns(self.slope_y, inp.b[0], inp.b[1])))
        return a, b, slope

    def _sample_height(self, name, si, sj):
        if self.rounds_in_ground_frame:
            return self.location(name)[2]
        return self.world_height(name)[si, sj]

    def _apply_inputs(self, inputs):
        p = self.p
        erase = np.zeros(self.D.shape)
        any_erase = False
        for inp in inputs:
            a, b, slope = self._input_frame(inp)
            r = float(inp.radius)
            si, sj = self._bbox(a, b, r)
            if si.start >= si.stop or sj.start >= sj.stop:
                continue
            for comp, name in enumerate(("u", "v", "w")):
                X, Y, _, R, _ = self.location(name)
                Z = self._sample_height(name, si, sj)
                W = core.band_weight(X[si], Y[:, sj], Z, R, a, b, r) * float(inp.strength)
                target = self._wind_target(comp, inp.velocity, si, sj, slope)
                if comp == 2:
                    self._record_top_target(target, W)
                arr = getattr(self, name)
                blk = arr[si, sj].astype(np.float64)
                arr[si, sj] = (blk + (target - blk) * W).astype(F32)
            if inp.erase_per_sec > 0:
                X, Y, _, R, _ = self.location("c")
                Z = self._sample_height("c", si, sj)
                amount = core.dwell_amount(X[si], Y[:, sj], Z, R, a, b, r, p.dt)
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

    def input_weight_sum(self, center, radius):
        inp = core.SweptSphere(tuple(center), tuple(center), float(radius), (0.0, 0.0, 0.0))
        a, b, _ = self._input_frame(inp)
        X, Y, _, R, _ = self.location("c")
        Z = self.location("c")[2] if self.rounds_in_ground_frame else self.world_height("c")
        W = core.band_weight(X, Y, Z, R, a, b, float(radius))
        return float(np.broadcast_to(W, self.D.shape).sum())

    def touched_columns(self, inputs):
        touched = np.zeros((self.nx, self.ny), bool)
        X, Y, _, R, _ = self.location("c")
        for inp in inputs:
            a, b, _ = self._input_frame(inp)
            r = float(inp.radius)
            si, sj = self._bbox(a, b, r)
            if si.start >= si.stop or sj.start >= sj.stop:
                continue
            Z = self._sample_height("c", si, sj)
            W = np.broadcast_to(core.band_weight(X[si], Y[:, sj], Z, R, a, b, r), (si.stop - si.start, sj.stop - sj.start, self.nz))
            touched[si, sj] |= (W > 0).any(axis=2)
        return touched & ~self.wall_columns

    def sample_columns(self, field, x, y):
        x, y = np.asarray(x, float), np.asarray(y, float)
        rx, ry, _ = self.center_rel()
        fx = (x - rx[0]) / self.p.hx
        fy = (y - ry[0]) / self.p.hy
        i0 = np.clip(np.floor(fx).astype(np.int64), 0, self.nx - 2)
        j0 = np.clip(np.floor(fy).astype(np.int64), 0, self.ny - 2)
        tx, ty = fx - i0, fy - j0
        return (field[i0, j0] * (1 - tx) + field[i0 + 1, j0] * tx) * (1 - ty) + (field[i0, j0 + 1] * (1 - tx) + field[i0 + 1, j0 + 1] * tx) * ty

    def drawn_ground(self, x, y):
        x, y = np.asarray(x, float), np.asarray(y, float)
        if not self.follows_terrain:
            return np.zeros(np.broadcast(x, y).shape)
        return self.sample_columns(self.column_height, x, y)

    def world_wind_speed(self):
        cu, cv, cw = self.center_velocity()
        sx = self.slope_x[:, :, None] if self.follows_terrain else 0.0
        sy = self.slope_y[:, :, None] if self.follows_terrain else 0.0
        return np.sqrt(cu * cu + cv * cv + (cw + cu * sx + cv * sy) ** 2)
