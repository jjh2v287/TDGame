"""
실행 환경: Python 3.12 + numpy 2.2 + scipy 1.17 (추가 설치 없음), 저장소 루트에서 실행. 같은 폴더의 잠긴 fog3d_core.py·fog3d_terrain.py·fog3d_terrain_add1.py 를 import 만 한다
실행 명령: 직접 실행하지 않는다. fog3d_gates_s2a_add2.py 가 import 한다
출력: 없음
상태: 2a 덧붙임 2 후보 R5(Docs/VolumeFog3D/stage2/prereg-2a-design-add2.md D절)와 E절 지형 기준 모의, 2026-11-15 잠금(공식 판정 실행에 쓴 판. 잠근 뒤에는 고치지 않는다)
"""
import math

import numpy as np

import fog3d_core as core
import fog3d_terrain_add1 as terrain_add1
from fog3d_core import F32
from fog3d_terrain import TERRAINS, WALL_STEP_CM, Terrain, _broadcast_height
from fog3d_terrain_add1 import TAN30, TerrainSimR4

BLEND_KINDS = ("full", "partial", "zero", "level_frame")


def cliff_line_distance(x, y):
    return _broadcast_height(np.asarray(x, float), x, y)


def slope_then_level(name, gradient, level):
    def height(x, y):
        x = np.asarray(x, float)
        return _broadcast_height(np.where(x < 0.0, gradient * x, level), x, y)
    return Terrain(name, height, cliff_line_distance)


def foot45_height(x, y):
    x = np.asarray(x, float)
    return _broadcast_height(np.where(x < 0.0, -x, 0.0), x, y)


def top45_height(x, y):
    x = np.asarray(x, float)
    return _broadcast_height(np.where(x < 0.0, x, 0.0), x, y)


ADD2_TERRAINS = {
    "S30DROP100": slope_then_level("S30DROP100", -TAN30, -100.0),
    "FOOT45": Terrain("FOOT45", foot45_height),
    "TOP45": Terrain("TOP45", top45_height),
    "S30CURB45": slope_then_level("S30CURB45", TAN30, 45.0),
    "S30CURB60": slope_then_level("S30CURB60", TAN30, 60.0),
    "S30CURB80": slope_then_level("S30CURB80", TAN30, 80.0),
    "S30CURB130": slope_then_level("S30CURB130", TAN30, 130.0),
    "S30DROP60": slope_then_level("S30DROP60", -TAN30, -60.0),
}


def register_terrains():
    terrain_add1.register_terrains()
    for name, terrain in ADD2_TERRAINS.items():
        TERRAINS.setdefault(name, terrain)


def blend_by_share(share, tilted, world):
    g = share[:, :, None]
    return np.where(g >= 1.0, tilted, np.where(g <= 0.0, world, g * tilted + (1.0 - g) * world))


class TerrainSimR5(TerrainSimR4):

    def __init__(self, params, terrain, anchor=(0, 0), window_min=None, boxes=(), obstacles=()):
        self.reference_frame = (0.0, 0.0, 0.0)
        self.world_ends = (np.zeros(3), np.zeros(3))
        self.blend_counts = dict.fromkeys(BLEND_KINDS, 0)
        super().__init__(params, terrain, anchor=anchor, window_min=window_min, boxes=boxes, obstacles=obstacles)
        self.candidate = "R5"

    def reference_column(self, local_x, local_y):
        i = int(math.floor((float(local_x) - float(self.xc[0])) / self.p.hx + 0.5))
        j = int(math.floor((float(local_y) - float(self.yc[0])) / self.p.hy + 0.5))
        return min(max(i, 0), self.nx - 1), min(max(j, 0), self.ny - 1)

    def _input_frame(self, inp):
        a, b, slope = super()._input_frame(inp)
        mx, my, _, _ = self.tilt_frame
        i, j = self.reference_column(mx, my)
        self.reference_frame = (float(self.column_height[i, j]), float(self.xc[i]), float(self.yc[j]))
        self.world_ends = (self.to_local(inp.a), self.to_local(inp.b))
        return a, b, slope

    def sample_ground_height(self, name):
        if name == "u":
            return self.u_face_height
        if name == "v":
            return self.v_face_height
        return self.column_height

    def tilt_share(self, name, si, sj):
        reference_height, rx, ry = self.reference_frame
        _, _, scx, scy = self.tilt_frame
        X, Y = self.location(name)[:2]
        deviation = self.sample_ground_height(name)[si, sj] - reference_height - scx * (X[si][:, :, 0] - rx) - scy * (Y[:, sj][:, :, 0] - ry)
        return np.clip((2.0 * WALL_STEP_CM - np.abs(deviation)) / WALL_STEP_CM, 0.0, 1.0)

    def frame_is_level(self):
        _, _, scx, scy = self.tilt_frame
        return scx == 0.0 and scy == 0.0

    def _blend_frames(self, name, si, sj, a, b, measure):
        share = self.tilt_share(name, si, sj)
        X, Y, _, R, _ = self.location(name)
        if self.frame_is_level() or (share >= 1.0).all():
            return measure(X[si], Y[:, sj], self._sample_height(name, si, sj), R, a, b), share
        wa, wb = self.world_ends
        world = measure(X[si], Y[:, sj], self.world_height(name)[si, sj], R, wa, wb)
        if (share <= 0.0).all():
            return world, share
        tilted = measure(X[si], Y[:, sj], self._sample_height(name, si, sj), R, a, b)
        return blend_by_share(share, tilted, world), share

    def blended_weight(self, name, si, sj, a, b, r):
        return self._blend_frames(name, si, sj, a, b, lambda X, Y, Z, R, pa, pb: core.band_weight(X, Y, Z, R, pa, pb, r))[0]

    def blended_dwell(self, si, sj, a, b, r, dt):
        return self._blend_frames("c", si, sj, a, b, lambda X, Y, Z, R, pa, pb: core.dwell_amount(X, Y, Z, R, pa, pb, r, dt))[0]

    def _count_blend(self, si, sj, a, b, r):
        weight, share = self._blend_frames("c", si, sj, a, b, lambda X, Y, Z, R, pa, pb: core.band_weight(X, Y, Z, R, pa, pb, r))
        hit = (np.broadcast_to(weight, (si.stop - si.start, sj.stop - sj.start, self.nz)) > 0) & self.fluid[si, sj]
        full = share >= 1.0
        zero = share <= 0.0
        for key, columns in (("full", full), ("partial", ~full & ~zero), ("zero", zero)):
            self.blend_counts[key] += int((hit & columns[:, :, None]).sum())
        if self.frame_is_level():
            self.blend_counts["level_frame"] += int(hit.sum())

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
            self._count_blend(si, sj, a, b, r)
            for comp, name in enumerate(("u", "v", "w")):
                W = self.blended_weight(name, si, sj, a, b, r) * float(inp.strength)
                target = self._wind_target(comp, inp.velocity, si, sj, slope)
                if comp == 2:
                    self._record_top_target(target, W)
                arr = getattr(self, name)
                blk = arr[si, sj].astype(np.float64)
                arr[si, sj] = (blk + (target - blk) * W).astype(F32)
            if inp.erase_per_sec > 0:
                amount = self.blended_dwell(si, sj, a, b, r, p.dt)
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
        whole = slice(None)
        W = self.blended_weight("c", whole, whole, a, b, float(radius))
        return float(np.broadcast_to(W, self.D.shape).sum())

    def touched_columns(self, inputs):
        touched = np.zeros((self.nx, self.ny), bool)
        for inp in inputs:
            a, b, _ = self._input_frame(inp)
            r = float(inp.radius)
            si, sj = self._bbox(a, b, r)
            if si.start >= si.stop or sj.start >= sj.stop:
                continue
            W = np.broadcast_to(self.blended_weight("c", si, sj, a, b, r), (si.stop - si.start, sj.stop - sj.start, self.nz))
            touched[si, sj] |= (W > 0).any(axis=2)
        return touched & ~self.wall_columns
