"""
실행 환경: Python 3.12 + numpy 2.2 + scipy 1.17 (추가 설치 없음), 저장소 루트에서 실행. 같은 폴더의 잠긴 fog3d_core.py·fog3d_terrain.py 를 import 만 한다
실행 명령: 직접 실행하지 않는다. fog3d_gates_s2a_add1.py 가 import 한다
출력: 없음
상태: 2a 덧붙임 1 후보 R4(Docs/VolumeFog3D/stage2/prereg-2a-design-add1.md D절) 기준 모의, 2026-11-15 잠금(공식 판정 실행에 쓴 판. 잠근 뒤에는 고치지 않는다)
"""
import math

import numpy as np

import fog3d_core as core
from fog3d_terrain import TERRAINS, Terrain, TerrainSim, _broadcast_height

SLOPE_CURB_TERRAIN = "S30CURB100"


def slope_curb_height(x, y):
    x = np.asarray(x, float)
    return _broadcast_height(np.where(x < 0.0, math.tan(math.radians(30.0)) * x, 100.0), x, y)


def slope_curb_distance(x, y):
    return _broadcast_height(np.asarray(x, float), x, y)


TAN30 = math.tan(math.radians(30.0))


def slope_y_height(x, y):
    return _broadcast_height(TAN30 * np.asarray(y, float), x, y)


def foot_height(x, y):
    x = np.asarray(x, float)
    return _broadcast_height(np.where(x < 0.0, -TAN30 * x, 0.0), x, y)


def top_height(x, y):
    x = np.asarray(x, float)
    return _broadcast_height(np.where(x < 0.0, TAN30 * x, 0.0), x, y)


ADD1_TERRAINS = {
    SLOPE_CURB_TERRAIN: Terrain(SLOPE_CURB_TERRAIN, slope_curb_height, slope_curb_distance),
    "S30Y": Terrain("S30Y", slope_y_height),
    "FOOT30": Terrain("FOOT30", foot_height),
    "TOP30": Terrain("TOP30", top_height),
}


def register_terrains():
    for name, terrain in ADD1_TERRAINS.items():
        TERRAINS.setdefault(name, terrain)


class TerrainSimR4(TerrainSim):

    def __init__(self, params, terrain, anchor=(0, 0), window_min=None, boxes=(), obstacles=()):
        self.tilt_frame = (0.0, 0.0, 0.0, 0.0)
        super().__init__(params, terrain, "R3", anchor=anchor, window_min=window_min, boxes=boxes, obstacles=obstacles)
        self.candidate = "R4"

    def frame_slope(self, x, y):
        rx, ry, _ = self.center_rel()
        x = min(max(float(x), float(rx[0])), float(rx[-1]))
        y = min(max(float(y), float(ry[0])), float(ry[-1]))
        return float(self.sample_columns(self.slope_x, x, y)), float(self.sample_columns(self.slope_y, x, y))

    def _tilted_point(self, point):
        mx, my, scx, scy = self.tilt_frame
        return np.array([point[0], point[1], point[2] - scx * (point[0] - mx) - scy * (point[1] - my)], float)

    def _input_frame(self, inp):
        a = self.to_local(inp.a)
        b = self.to_local(inp.b)
        scx, scy = self.frame_slope(0.5 * (inp.a[0] + inp.b[0]), 0.5 * (inp.a[1] + inp.b[1]))
        self.tilt_frame = (0.5 * (a[0] + b[0]), 0.5 * (a[1] + b[1]), scx, scy)
        return self._tilted_point(a), self._tilted_point(b), None

    def _sample_height(self, name, si, sj):
        mx, my, scx, scy = self.tilt_frame
        X, Y = self.location(name)[:2]
        return self.world_height(name)[si, sj] - scx * (X[si] - mx) - scy * (Y[:, sj] - my)

    def input_weight_sum(self, center, radius):
        inp = core.SweptSphere(tuple(center), tuple(center), float(radius), (0.0, 0.0, 0.0))
        a, b, _ = self._input_frame(inp)
        X, Y, _, R, _ = self.location("c")
        Z = self._sample_height("c", slice(None), slice(None))
        W = core.band_weight(X, Y, Z, R, a, b, float(radius))
        return float(np.broadcast_to(W, self.D.shape).sum())
