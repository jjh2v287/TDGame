"""에디터 밖 시스템 파이썬(numpy)에서 쓰는 공유 모듈: 탑다운 카메라 대비 결정론 배치 후처리 규칙 R1·R2·R3·R5와 규칙 전후 수치 계산(P3-17, 명세 3절). 난수를 쓰지 않는다.
실행: generate_ashen_vale.py·generate_realm.py·realm_passages.py 가 import 한다. 수치 계산은 python Tools/WorldGen/topdown_rules.py --measure --label <이름> [--field-dir <경로> --realms-dir <경로>] (numpy·scipy 필요, 생성기 출력이 먼저 있어야 함)
출력: --measure 는 Saved/WorldGen/TopDownMetrics/metrics_<이름>.json(--out-dir 로 변경) 과 표준 출력 표. 생성기는 출력 폴더에 topdown_rules_report.json 을 쓴다
상태: 현행
"""
import argparse
import json
import math
import os
import re
import sys
from collections import Counter
from contextlib import contextmanager

import numpy as np

ENABLED = True
PLACEMENT_LEGACY_TERRAIN = False

CAMERA_ARM_CM = 1100.0
CAMERA_PITCH_DEG = -50.0
CAMERA_YAW_DEG = 45.0
PAWN_CENTER_ABOVE_GROUND_CM = 96.0
PAWN_HEAD_ABOVE_GROUND_CM = 170.0
CAMERA_OFFSET_CM = (
    -CAMERA_ARM_CM * math.cos(math.radians(CAMERA_PITCH_DEG)) * math.cos(math.radians(CAMERA_YAW_DEG)),
    -CAMERA_ARM_CM * math.cos(math.radians(CAMERA_PITCH_DEG)) * math.sin(math.radians(CAMERA_YAW_DEG)),
    -CAMERA_ARM_CM * math.sin(math.radians(CAMERA_PITCH_DEG)),
)
CAMERA_OFFSET_M = tuple(v / 100.0 for v in CAMERA_OFFSET_CM)
CAMERA_FROM_HEAD_CM = np.array([CAMERA_OFFSET_CM[0], CAMERA_OFFSET_CM[1], CAMERA_OFFSET_CM[2] + PAWN_CENTER_ABOVE_GROUND_CM - PAWN_HEAD_ABOVE_GROUND_CM])
CAMERA_NEAR_RADIUS_CM = 150.0
ZONE_AHEAD_CM = 650.0
WALK_MAX_SLOPE_DEG = 35.0
EDGE_CELLS = 2
MIN_BLOCKER_TOP_M = 1.2
MIN_Z_SCALE = 0.6
Z_SCALE_STEP = 0.05
GAP_IGNORE_CM = 5.0
GAP_MARGIN_CM = 5.0
GAP_REMOVE_RATIO = 0.35
VISIBLE_BEHIND_CM = 2500.0
VISIBLE_BESIDE_CM = 800.0
PILLAR_TOP_CAP_CM = 600.0
ENTRANCE_PILLAR_TOP_CAP_CM = 600.0
ENTRANCE_RAM_DROP_CM = 200.0
CROWN_START = {"tree": 0.30, "mushroom": 0.45}
TRUNK_HALF_CM = 20.0
ASSET_PREFIX = "/Game/DarkFantasyTopDown/StaticMeshes/"
NO_COLLISION_ASSETS = ("VFX/SM_LightBeam",)
PILLAR_LABEL_TOKENS = ("StarPillar_", "RuinPillar_", "LookoutPillar_", "PillarSpire")
ENTRANCE_LABEL_PATTERN = re.compile(r"TDGen_(MainCrypt|HollowCave|SunkenCrypt)_(Lintel|Cap|Pillar_-1|Pillar_1|Ram)")
FIELD_PASSAGE_LABEL_PREFIX = "TDRealm_Passage_"
ROCKLIKE = ("rock", "cliff")
SOLID_CATEGORIES = ("rock", "cliff", "structure")

MESH_BOUNDS_CM = {
    "Birch/SM_Birch1": (-233, -231, -29, 226, 235, 955),
    "Birch/SM_Birch2": (-241, -199, -24, 155, 171, 837),
    "Birch/SM_Birch3": (-64, -61, -59, 73, 104, 509),
    "Bush/SM_Bush": (-66, -68, 0, 73, 68, 128),
    "Lanterns/SM_Lamppost": (-89, -36, -6, 18, 46, 356),
    "Nature/Cliff/SM_Cliff2": (-395, -428, -49, 453, 390, 581),
    "Nature/Cliff/SM_Cliff3": (-287, -349, -15, 439, 298, 261),
    "Nature/Cliff/SM_Cliff4": (-353, -448, -239, 209, 161, 913),
    "Nature/Cliff/SM_Cliffs1": (-192, -238, -11, 309, 208, 145),
    "Nature/Dreadplants/SM_Dreadplant1": (-75, -73, -28, 77, 77, 152),
    "Nature/Dreadplants/SM_Dreadplant2": (-62, -73, -29, 75, 61, 175),
    "Nature/Dreadplants/SM_DreadplantMushroom1": (-44, -47, -3, 47, 43, 55),
    "Nature/Dreadplants/SM_DreadplantMushroom2": (-46, -48, -5, 46, 53, 96),
    "Rocks2/SM_Rock1": (-800, -346, -10, 734, 403, 222),
    "Rocks2/SM_Rock2": (-677, -471, -14, 824, 431, 209),
    "Rocks2/SM_Rock3": (-874, -626, -20, 905, 616, 222),
    "RocksTIle/SM_RocksTile": (-157, -159, -37, 148, 160, 40),
    "SmallProps/SM_ChainCurved": (-162, -94, -2, 171, 96, 6),
    "SmallProps/SM_StickedSkull": (-10, -11, -1, 6, 13, 79),
    "VFX/SM_LightBeam": (-38, -38, -128, 39, 38, 0),
    "WoodenParts/SM_FishTable": (-49, -157, -3, 47, 126, 245),
    "WoodenParts/SM_WoodenConstruction3": (-135, -162, -9, 163, 142, 21),
    "WoodenParts/SM_WoodenConstruction6": (-48, -158, -14, 51, 153, 29),
    "WoodenParts/SM_WoodenConstruction7": (-145, -24, -8, 54, 26, 334),
    "WoodenParts/SM_WoodenConstruction9": (-313, -245, -23, 340, 229, 514),
    "WoodenParts/SM_WoodenFence1": (-5, -239, -6, 6, 226, 144),
    "WoodenParts/SM_WoodenFloorSegment": (-166, -153, -4, 148, 161, 22),
    "WoodenParts/SM_WoodenLadder": (-35, -12, -1, 33, 2, 218),
    "WoodenParts/SM_WoodenPath": (-217, -132, -4, 183, 116, 41),
    "WoodenParts/SM_WoodenPole": (-42, -20, -7, 40, 69, 332),
    "WoodenParts/SM_WoodenPole2": (-17, -13, -125, 10, 18, 80),
    "WoodenParts/SM_WoodenShelving": (-49, -157, -3, 48, 126, 245),
}
BASE_RIM_CM = {
    "Rocks2/SM_Rock1": ((-770, -124, -1), (-734, -303, -10), (-596, -309, 0), (-321, -326, 8), (-216, -316, 4), (-34, -346, 7), (17, -330, 6), (148, -319, 10), (269, -286, 5), (466, -262, 4), (702, -177, 1), (734, -144, 0), (685, 217, 0), (669, 302, 0), (511, 367, -1), (166, 237, 2), (82, 246, 2), (20, 240, 2), (-83, 255, 22), (-113, 268, 4), (-227, 261, 5), (-339, 243, 7), (-749, 233, -3), (-794, 227, -4)),
    "Rocks2/SM_Rock2": ((-629, -163, -11), (-615, -209, -11), (-524, -372, -9), (-332, -441, 4), (-145, -400, -4), (13, -305, -3), (146, -310, -1), (219, -298, 10), (325, -280, 0), (488, -262, 1), (813, -222, -8), (820, -204, -8), (817, -4, -7), (706, 162, -4), (571, 286, -6), (427, 365, -4), (284, 396, -1), (172, 387, -4), (14, 354, -5), (-130, 388, -6), (-343, 431, 3), (-512, 344, -6), (-670, 199, -11), (-674, 182, -4)),
    "Rocks2/SM_Rock3": ((-791, -141, -17), (-777, -303, -15), (-660, -415, -15), (-501, -536, -6), (-334, -623, -4), (-128, -586, -10), (67, -522, -3), (325, -538, 1), (458, -512, -1), (574, -432, -6), (703, -232, -4), (874, -49, -10), (905, 72, -9), (856, 222, -15), (700, 418, -13), (442, 426, -16), (217, 535, -15), (26, 606, -13), (-14, 614, -18), (-278, 555, -12), (-458, 528, -19), (-644, 419, -18), (-808, 225, -18), (-864, 151, -14)),
    "Nature/Cliff/SM_Cliff2": ((-234, -121, -15), (-13, -354, -17), (99, -366, -43), (170, -326, -45), (325, -339, -17), (316, -253, -12), (359, -109, -19), (371, -72, -18), (445, 79, -10), (398, 192, -27), (414, 270, -19), (318, 323, -33), (188, 328, -15), (72, 360, -17), (-18, 371, -16), (-82, 375, -15), (-338, 292, -10)),
    "Nature/Cliff/SM_Cliff3": ((-273, -49, -12), (-230, -114, -12), (-206, -246, -12), (-183, -300, -12), (-91, -334, -12), (27, -329, 3), (81, -303, -13), (149, -269, -12), (220, -200, -8), (331, -218, 14), (377, -198, -13), (418, -56, -12), (428, 51, -12), (409, 133, -13), (349, 232, -13), (321, 276, -12), (258, 295, -13), (129, 277, -13), (70, 273, -8), (-79, 256, 22), (-198, 272, -12), (-220, 244, -11), (-249, 155, 9), (-283, 36, -12)),
    "Nature/Cliff/SM_Cliff4": ((-179, -152, -210), (-187, -180, -217), (-185, -234, -203), (-144, -243, -204), (-76, -150, -187), (-79, -229, -219), (-59, -238, -201), (-40, -212, -193), (-26, -173, -185), (-41, -154, -206), (33, -143, -207), (21, -117, -216), (42, -104, -215), (40, -52, -199), (0, -42, -203), (-70, -136, -185), (-85, -47, -201), (-110, -76, -189), (-114, -115, -190), (-103, -130, -206), (-172, -137, -208)),
    "Nature/Cliff/SM_Cliffs1": ((-192, -13, -7), (-173, -101, -2), (-126, -136, 5), (-74, -214, 6), (-28, -194, 10), (3, -225, 6), (114, -190, -6), (192, -151, 11), (203, -140, -7), (239, -84, -7), (264, -47, -7), (309, 9, -6), (268, 98, -6), (188, 175, 26), (172, 202, -4), (111, 208, -6), (58, 181, -7), (-11, 141, 32), (-60, 142, 22), (-148, 127, -7), (-171, 97, -6), (-190, -7, 10)),
}


@contextmanager
def legacy_placement_terrain():
    global PLACEMENT_LEGACY_TERRAIN
    PLACEMENT_LEGACY_TERRAIN = True
    try:
        yield
    finally:
        PLACEMENT_LEGACY_TERRAIN = False


def quantize_height_m(height_m, units_per_meter=128.0):
    units = np.clip(np.round(32768.0 + np.asarray(height_m, dtype=np.float64) * units_per_meter), 0, 65535)
    return (units - 32768.0) / units_per_meter


def asset_key(mesh_path):
    if mesh_path.startswith(ASSET_PREFIX):
        return mesh_path[len(ASSET_PREFIX):]
    return mesh_path


def category_of(asset):
    if asset.startswith("Birch/"):
        return "tree"
    if "DreadplantMushroom" in asset:
        return "mushroom"
    if asset.startswith("Rocks2/"):
        return "rock"
    if asset.startswith("Nature/Cliff/"):
        return "cliff"
    if "Dreadplant" in asset or asset == "Bush/SM_Bush":
        return "plant"
    if asset in NO_COLLISION_ASSETS:
        return "beam"
    return "structure"


def rotation_axes(pitch, yaw, roll):
    pitch, yaw, roll = (np.radians(np.asarray(v, dtype=np.float64)) for v in (pitch, yaw, roll))
    sp, cp = np.sin(pitch), np.cos(pitch)
    sy, cy = np.sin(yaw), np.cos(yaw)
    sr, cr = np.sin(roll), np.cos(roll)
    ax = np.stack([cp * cy, cp * sy, sp], axis=-1)
    ay = np.stack([sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp], axis=-1)
    az = np.stack([-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp], axis=-1)
    return np.stack([ax, ay, az], axis=-2)


class TDTerrainGrid:
    def __init__(self, height_m, center_cm=(0.0, 0.0)):
        self.h = np.asarray(height_m, dtype=np.float64)
        self.size = self.h.shape[0]
        self.half = (self.size - 1) // 2
        self.center_cm = (float(center_cm[0]), float(center_cm[1]))
        gy, gx = np.gradient(self.h, 1.0)
        self.slope = np.degrees(np.arctan(np.hypot(gx, gy)))

    def sample_cm(self, x_cm, y_cm):
        fx = np.clip((np.asarray(x_cm, dtype=np.float64) - self.center_cm[0]) / 100.0 + self.half, 0.0, self.size - 1.001)
        fy = np.clip((np.asarray(y_cm, dtype=np.float64) - self.center_cm[1]) / 100.0 + self.half, 0.0, self.size - 1.001)
        x0 = np.floor(fx).astype(int)
        y0 = np.floor(fy).astype(int)
        tx = fx - x0
        ty = fy - y0
        h = self.h
        top = h[y0, x0] * (1.0 - tx) + h[y0, x0 + 1] * tx
        bottom = h[y0 + 1, x0] * (1.0 - tx) + h[y0 + 1, x0 + 1] * tx
        return (top * (1.0 - ty) + bottom * ty) * 100.0

    def cell_xy_cm(self, iy, ix):
        return (ix - self.half) * 100.0 + self.center_cm[0], (iy - self.half) * 100.0 + self.center_cm[1]

    def meter_axes(self):
        xs = np.arange(self.size, dtype=np.float64) - self.half
        return np.meshgrid(xs, xs)


class TDRouteMasks:
    def __init__(self, grid, road_mask, circles_m, water_mask, floor_masks):
        X, Y = grid.meter_axes()
        plaza = np.zeros(grid.h.shape, dtype=bool)
        for cx, cy, r in circles_m:
            plaza |= (X - cx) ** 2 + (Y - cy) ** 2 <= r * r
        floor = np.zeros(grid.h.shape, dtype=bool)
        for mask in floor_masks:
            floor |= mask
        walk = (grid.slope < WALK_MAX_SLOPE_DEG) & ~water_mask
        walk[:EDGE_CELLS, :] = False
        walk[-EDGE_CELLS:, :] = False
        walk[:, :EDGE_CELLS] = False
        walk[:, -EDGE_CELLS:] = False
        self.walk = walk
        self.floors = list(floor_masks)
        self.floor_union = floor
        self.route = walk & (road_mask | plaza | floor)
        padded = np.zeros((grid.size + 1, grid.size + 1), dtype=np.int32)
        padded[1:, 1:] = self.route.astype(np.int32).cumsum(axis=0).cumsum(axis=1)
        self.count_table = padded

    def count_in(self, iy0, iy1, ix0, ix1):
        t = self.count_table
        return t[iy1 + 1, ix1 + 1] - t[iy0, ix1 + 1] - t[iy1 + 1, ix0] + t[iy0, ix0]


class TDPlacedMesh:
    __slots__ = ("source", "ref", "asset", "bucket", "label", "removed")

    def __init__(self, source, ref, asset, bucket, label):
        self.source = source
        self.ref = ref
        self.asset = asset
        self.bucket = bucket
        self.label = label
        self.removed = False

    def values(self):
        if self.source == "ism":
            return self.ref[0:3], self.ref[3:6], self.ref[6:9]
        return self.ref["loc"], self.ref["rot"], self.ref["scale"]

    def set_z(self, z_cm):
        if self.source == "ism":
            self.ref[2] = round(z_cm, 1)
            return
        self.ref["loc"][2] = round(z_cm, 1)

    def set_tilt_zero(self):
        if self.source == "ism":
            self.ref[3] = 0.0
            self.ref[5] = 0.0
            return
        self.ref["rot"][0] = 0.0
        self.ref["rot"][2] = 0.0

    def set_scale_z(self, scale_z):
        if self.source == "ism":
            self.ref[8] = round(scale_z, 3)
            return
        self.ref["scale"][2] = round(scale_z, 3)


def collect_placements(ism, actors, path_of_bucket):
    records = []
    for bucket_key, transforms in ism.items():
        asset = asset_key(path_of_bucket(bucket_key))
        for transform in transforms:
            records.append(TDPlacedMesh("ism", transform, asset, bucket_key[0], ""))
    for entry in actors:
        records.append(TDPlacedMesh("actor", entry, asset_key(entry["mesh"]), "", entry.get("label", "")))
    return records


def commit_placements(records, ism, actors):
    removed_ids = {id(r.ref) for r in records if r.removed}
    if not removed_ids:
        return 0
    for bucket_key in list(ism):
        kept = [t for t in ism[bucket_key] if id(t) not in removed_ids]
        if kept:
            ism[bucket_key][:] = kept
            continue
        del ism[bucket_key]
    actors[:] = [e for e in actors if id(e) not in removed_ids]
    return len(removed_ids)


class TDGeometry:
    def __init__(self, record):
        loc, rot, scale = record.values()
        self.pivot = np.array(loc, dtype=np.float64)
        self.rot = np.array(rot, dtype=np.float64)
        self.scale = np.array(scale, dtype=np.float64)
        self.asset = record.asset
        self.lo = np.array(MESH_BOUNDS_CM[record.asset][0:3], dtype=np.float64)
        self.hi = np.array(MESH_BOUNDS_CM[record.asset][3:6], dtype=np.float64)
        self.category = category_of(record.asset)
        self.axes = rotation_axes(self.rot[0], self.rot[1], self.rot[2])

    def with_scale_z(self, scale_z):
        clone = object.__new__(TDGeometry)
        clone.__dict__.update(self.__dict__)
        clone.scale = self.scale.copy()
        clone.scale[2] = scale_z
        clone.pivot = self.pivot.copy()
        clone.pivot[2] = self.pivot[2] + self.lo[2] * (self.scale[2] - scale_z)
        return clone

    def boxes(self):
        start = CROWN_START.get(self.category)
        if start is None:
            return [(self.lo, self.hi)]
        crown_lo = self.lo.copy()
        crown_lo[2] = self.lo[2] + (self.hi[2] - self.lo[2]) * start
        trunk_x = TRUNK_HALF_CM / self.scale[0]
        trunk_y = TRUNK_HALF_CM / self.scale[1]
        trunk = (np.array([-trunk_x, -trunk_y, self.lo[2]]), np.array([trunk_x, trunk_y, crown_lo[2]]))
        return [(crown_lo, self.hi), trunk]

    def bottom_cm(self):
        return self.pivot[2] + self.lo[2] * self.scale[2]

    def height_cm(self):
        return (self.hi[2] - self.lo[2]) * self.scale[2]


def build_arrays(records):
    index = [i for i, r in enumerate(records) if r.asset in MESH_BOUNDS_CM and category_of(r.asset) != "beam" and not r.removed]
    if not index:
        return None
    loc = np.array([records[i].values()[0] for i in index], dtype=np.float64)
    rot = np.array([records[i].values()[1] for i in index], dtype=np.float64)
    scale = np.array([records[i].values()[2] for i in index], dtype=np.float64)
    bounds = np.array([MESH_BOUNDS_CM[records[i].asset] for i in index], dtype=np.float64)
    lo, hi = bounds[:, 0:3], bounds[:, 3:6]
    bits = np.array([[(c >> 2) & 1, (c >> 1) & 1, c & 1] for c in range(8)], dtype=bool)
    corners = np.where(bits[None, :, :], hi[:, None, :], lo[:, None, :]) * scale[:, None, :]
    axes = rotation_axes(rot[:, 0], rot[:, 1], rot[:, 2])
    world = loc[:, None, :] + np.einsum("nck,nkj->ncj", corners, axes)
    return {"index": np.array(index), "loc": loc, "aabb_min": world.min(axis=1), "aabb_max": world.max(axis=1), "top_cm": world[:, :, 2].max(axis=1)}


def candidate_rows(arrays, grid, masks):
    ground = grid.sample_cm(arrays["loc"][:, 0], arrays["loc"][:, 1])
    tall = (arrays["top_cm"] - ground) / 100.0 >= MIN_BLOCKER_TOP_M
    zone = zone_bounds(grid, arrays["aabb_min"], arrays["aabb_max"])
    ix0, ix1, iy0, iy1 = zone
    valid = tall & (ix0 <= ix1) & (iy0 <= iy1)
    rows = np.nonzero(valid)[0]
    counts = masks.count_in(iy0[rows], iy1[rows], ix0[rows], ix1[rows])
    return rows[counts > 0], zone


def zone_bounds(grid, aabb_min, aabb_max):
    cx, cy = grid.center_cm
    ix0 = np.floor((aabb_min[..., 0] - cx) / 100.0 + grid.half).astype(int)
    ix1 = np.ceil((aabb_max[..., 0] + ZONE_AHEAD_CM - cx) / 100.0 + grid.half).astype(int)
    iy0 = np.floor((aabb_min[..., 1] - cy) / 100.0 + grid.half).astype(int)
    iy1 = np.ceil((aabb_max[..., 1] + ZONE_AHEAD_CM - cy) / 100.0 + grid.half).astype(int)
    n = grid.size - 1
    return np.clip(ix0, 0, n), np.clip(ix1, 0, n), np.clip(iy0, 0, n), np.clip(iy1, 0, n)


def zone_route_cells(masks, zone, row):
    ix0, ix1, iy0, iy1 = (int(z[row]) for z in zone)
    sub = masks.route[iy0:iy1 + 1, ix0:ix1 + 1]
    jj, ii = np.nonzero(sub)
    return jj + iy0, ii + ix0


def segment_hits(origin_local, direction_local, lo, hi):
    t_min = np.zeros(len(origin_local))
    t_max = np.ones(len(origin_local))
    for k in range(3):
        if abs(direction_local[k]) < 1e-9:
            t_max = np.where((origin_local[:, k] < lo[k]) | (origin_local[:, k] > hi[k]), -1.0, t_max)
            continue
        t1 = (lo[k] - origin_local[:, k]) / direction_local[k]
        t2 = (hi[k] - origin_local[:, k]) / direction_local[k]
        t_min = np.maximum(t_min, np.minimum(t1, t2))
        t_max = np.minimum(t_max, np.maximum(t1, t2))
    return t_max >= t_min


def inside_box(points_local, lo, hi, pad=0.0):
    return np.all((points_local >= lo - pad) & (points_local <= hi + pad), axis=1)


def evaluate_cells(grid, geometry, iy, ix):
    x, y = grid.cell_xy_cm(iy, ix)
    ground = grid.h[iy, ix] * 100.0
    head = np.stack([x, y, ground + PAWN_HEAD_ABOVE_GROUND_CM], axis=1)
    center = np.stack([x, y, ground + PAWN_CENTER_ABOVE_GROUND_CM], axis=1)
    head_local = ((head - geometry.pivot) @ geometry.axes.T) / geometry.scale
    center_local = ((center - geometry.pivot) @ geometry.axes.T) / geometry.scale
    direction_local = (CAMERA_FROM_HEAD_CM @ geometry.axes.T) / geometry.scale
    camera_local = head_local + direction_local
    pad = CAMERA_NEAR_RADIUS_CM / geometry.scale
    shape = len(iy)
    hit = np.zeros(shape, dtype=bool)
    camera_inside = np.zeros(shape, dtype=bool)
    camera_near = np.zeros(shape, dtype=bool)
    occupied = np.zeros(shape, dtype=bool)
    solid = geometry.category in SOLID_CATEGORIES
    for lo, hi in geometry.boxes():
        hit |= segment_hits(head_local, direction_local, lo, hi)
        camera_inside |= inside_box(camera_local, lo, hi)
        camera_near |= inside_box(camera_local, lo, hi, pad)
        if solid:
            occupied |= inside_box(center_local, lo, hi)
    keep = ~occupied
    return hit & keep, camera_inside & keep, camera_near & keep, occupied


def rim_world_cm(geometry):
    rim = np.array(BASE_RIM_CM[geometry.asset], dtype=np.float64)
    axes = rotation_axes(0.0, geometry.rot[1], 0.0)
    return geometry.pivot + (rim * geometry.scale) @ axes


def camera_side_gap_cm(grid, geometry):
    world = rim_world_cm(geometry)
    gap = world[:, 2] - grid.sample_cm(world[:, 0], world[:, 1])
    projection = -world[:, 0] - world[:, 1]
    return float(gap[projection >= np.median(projection)].max())


def in_visible_zone(grid, masks, geometry):
    world = rim_world_cm(geometry)
    low = world.min(axis=0)[:2] - VISIBLE_BEHIND_CM
    high = world.max(axis=0)[:2] + VISIBLE_BESIDE_CM
    cx, cy = grid.center_cm
    n = grid.size - 1
    ix0, iy0 = (int(np.clip(math.floor((low[k] - c) / 100.0 + grid.half), 0, n)) for k, c in enumerate((cx, cy)))
    ix1, iy1 = (int(np.clip(math.ceil((high[k] - c) / 100.0 + grid.half), 0, n)) for k, c in enumerate((cx, cy)))
    return masks.count_in(iy0, iy1, ix0, ix1) > 0


def apply_base_rules(records, grid, masks, report):
    stats = Counter()
    removed = Counter()
    sunk_total_cm = 0.0
    for record in records:
        if record.removed or record.asset not in BASE_RIM_CM:
            continue
        if not in_visible_zone(grid, masks, TDGeometry(record)):
            stats["outside_visible_zone"] += 1
            continue
        rotation = record.values()[1]
        if rotation[0] != 0.0 or rotation[2] != 0.0:
            record.set_tilt_zero()
            stats["tilt_zeroed"] += 1
        geometry = TDGeometry(record)
        gap = camera_side_gap_cm(grid, geometry)
        if gap <= GAP_IGNORE_CM:
            continue
        if gap > GAP_REMOVE_RATIO * geometry.height_cm():
            record.removed = True
            removed[record.asset] += 1
            continue
        sink = gap + GAP_MARGIN_CM
        record.set_z(geometry.pivot[2] - sink)
        stats["sunk"] += 1
        sunk_total_cm += sink
    report["r2"] = {"outside_visible_zone_untouched": stats["outside_visible_zone"], "tilt_zeroed": stats["tilt_zeroed"], "sunk": stats["sunk"], "sunk_total_m": round(sunk_total_cm / 100.0, 2),
                    "removed_edge": sum(removed.values()), "removed_edge_by_asset": dict(sorted(removed.items()))}


def is_pillar_record(record):
    if record.source == "ism":
        return record.bucket.endswith("_StarPillar")
    return any(token in record.label for token in PILLAR_LABEL_TOKENS)


def capped_scale_z(geometry, ground_cm, top_cap_cm):
    return (ground_cm + top_cap_cm - geometry.bottom_cm()) / (geometry.hi[2] - geometry.lo[2])


def apply_scale_z(record, geometry, scale_z):
    shifted = geometry.with_scale_z(scale_z)
    record.set_scale_z(scale_z)
    record.set_z(shifted.pivot[2])


def apply_pillar_rules(records, grid, masks, report):
    arrays = build_arrays(records)
    capped = Counter()
    if arrays is not None:
        rows, zone = candidate_rows(arrays, grid, masks)
        for row in rows:
            record = records[int(arrays["index"][row])]
            if record.removed or not is_pillar_record(record):
                continue
            geometry = TDGeometry(record)
            iy, ix = zone_route_cells(masks, zone, row)
            hit, _, camera_near, _ = evaluate_cells(grid, geometry, iy, ix)
            if not (hit | camera_near).any():
                continue
            ground = float(grid.sample_cm(geometry.pivot[0], geometry.pivot[1]))
            scale_z = capped_scale_z(geometry, ground, PILLAR_TOP_CAP_CM)
            if scale_z >= geometry.scale[2]:
                continue
            apply_scale_z(record, geometry, scale_z)
            capped[record.label.rsplit("_", 1)[0] if record.source == "actor" else record.bucket] += 1
    beam_actors = 0
    for record in records:
        if record.source == "actor" and record.asset in NO_COLLISION_ASSETS and not record.removed:
            record.ref["collision"] = "none"
            beam_actors += 1
    report["r3"] = {"pillars_capped": sum(capped.values()), "pillars_capped_by_group": dict(sorted(capped.items())), "lightbeam_actors_collision_none": beam_actors}


def largest_clear_scale(grid, geometry, iy, ix):
    steps = int(round((1.0 - MIN_Z_SCALE) / Z_SCALE_STEP))
    for k in range(1, steps + 1):
        factor = round(1.0 - Z_SCALE_STEP * k, 4)
        trial = geometry.with_scale_z(geometry.scale[2] * factor)
        hit, _, camera_near, _ = evaluate_cells(grid, trial, iy, ix)
        if not (hit | camera_near).any():
            return geometry.scale[2] * factor
    return None


def apply_visibility_rules(records, grid, masks, report):
    arrays = build_arrays(records)
    removed = Counter()
    scaled = Counter()
    kept_authored = Counter()
    if arrays is not None:
        rows, zone = candidate_rows(arrays, grid, masks)
        for row in rows:
            record = records[int(arrays["index"][row])]
            if record.removed:
                continue
            geometry = TDGeometry(record)
            iy, ix = zone_route_cells(masks, zone, row)
            hit, _, camera_near, _ = evaluate_cells(grid, geometry, iy, ix)
            if not (hit | camera_near).any():
                continue
            if record.source == "actor":
                kept_authored[record.asset] += 1
                continue
            if geometry.category in ROCKLIKE:
                scale_z = largest_clear_scale(grid, geometry, iy, ix)
                if scale_z is not None:
                    apply_scale_z(record, geometry, scale_z)
                    scaled[record.asset] += 1
                    continue
            record.removed = True
            removed[record.asset] += 1
    report["r1"] = {"removed": sum(removed.values()), "removed_by_asset": dict(sorted(removed.items())),
                    "z_scaled": sum(scaled.values()), "z_scaled_by_asset": dict(sorted(scaled.items())),
                    "authored_blocking_kept": sum(kept_authored.values()), "authored_blocking_kept_by_asset": dict(sorted(kept_authored.items()))}


def apply_entrance_rules(actors, grid, report):
    records = [TDPlacedMesh("actor", e, asset_key(e["mesh"]), "", e.get("label", "")) for e in actors if ENTRANCE_LABEL_PATTERN.fullmatch(e.get("label", ""))]
    stats = Counter()
    for record in records:
        part = ENTRANCE_LABEL_PATTERN.fullmatch(record.label).group(2)
        if part in ("Lintel", "Cap"):
            record.removed = True
            stats["removed_" + part.lower()] += 1
            continue
        if part == "Ram":
            record.set_z(record.values()[0][2] - ENTRANCE_RAM_DROP_CM)
            stats["ram_lowered"] += 1
            continue
        geometry = TDGeometry(record)
        ground = float(grid.sample_cm(geometry.pivot[0], geometry.pivot[1]))
        scale_z = capped_scale_z(geometry, ground, ENTRANCE_PILLAR_TOP_CAP_CM)
        if scale_z < geometry.scale[2]:
            apply_scale_z(record, geometry, scale_z)
            stats["pillars_capped"] += 1
    removed_ids = {id(r.ref) for r in records if r.removed}
    actors[:] = [e for e in actors if id(e) not in removed_ids]
    report["r5"] = dict(sorted(stats.items()))


def apply_placement_rules(ism, actors, path_of_bucket, grid, masks):
    report = {}
    apply_entrance_rules(actors, grid, report)
    records = collect_placements(ism, actors, path_of_bucket)
    apply_base_rules(records, grid, masks, report)
    apply_pillar_rules(records, grid, masks, report)
    apply_visibility_rules(records, grid, masks, report)
    report["ism_actors_removed"] = commit_placements(records, ism, actors)
    return report


def collision_for_asset(asset_path):
    return "none" if asset_key(asset_path) in NO_COLLISION_ASSETS else None


def terrain_camera_masks(grid):
    iy, ix = np.mgrid[0:grid.size, 0:grid.size]
    xs, ys = grid.cell_xy_cm(iy, ix)
    ground = grid.h * 100.0
    camera_z = ground + PAWN_CENTER_ABOVE_GROUND_CM + CAMERA_OFFSET_CM[2]
    camera_ground = grid.sample_cm(xs + CAMERA_OFFSET_CM[0], ys + CAMERA_OFFSET_CM[1])
    camera_under = camera_ground > camera_z
    head_z = ground + PAWN_HEAD_ABOVE_GROUND_CM
    sight_blocked = np.zeros(grid.h.shape, dtype=bool)
    for k in range(1, 9):
        t = k / 8.0
        line_z = head_z + CAMERA_FROM_HEAD_CM[2] * t
        terrain_z = grid.sample_cm(xs + CAMERA_FROM_HEAD_CM[0] * t, ys + CAMERA_FROM_HEAD_CM[1] * t)
        sight_blocked |= terrain_z > line_z
    return camera_under, sight_blocked


def measure_region(records, grid, masks):
    shape = grid.h.shape
    head_hit = np.zeros(shape, dtype=bool)
    camera_inside = np.zeros(shape, dtype=bool)
    camera_near = np.zeros(shape, dtype=bool)
    occupied = np.zeros(shape, dtype=bool)
    by_asset = Counter()
    arrays = build_arrays(records)
    if arrays is not None:
        rows, zone = candidate_rows(arrays, grid, masks)
        for row in rows:
            record = records[int(arrays["index"][row])]
            geometry = TDGeometry(record)
            iy, ix = zone_route_cells(masks, zone, row)
            hit, inside, near, occ = evaluate_cells(grid, geometry, iy, ix)
            head_hit[iy[hit], ix[hit]] = True
            camera_inside[iy[inside], ix[inside]] = True
            camera_near[iy[near], ix[near]] = True
            occupied[iy[occ], ix[occ]] = True
            by_asset.update({record.asset.split("/")[-1]: int(hit.sum())})
    camera_under, sight_blocked = terrain_camera_masks(grid)
    return {"head_hit": head_hit, "camera_inside": camera_inside, "camera_near": camera_near, "occupied": occupied,
            "camera_under": camera_under, "sight_blocked": sight_blocked, "by_asset": by_asset}


def subset_stats(measured, masks, subset):
    cells = subset & masks.route & ~measured["occupied"]
    n = int(cells.sum())
    occluded = int((cells & measured["head_hit"]).sum())
    return {"cells": n, "occluded_cells": occluded, "occluded_percent": round(100.0 * occluded / n, 2) if n else 0.0,
            "camera_inside_cells": int((cells & measured["camera_inside"]).sum()), "camera_near_1p5m_cells": int((cells & measured["camera_near"]).sum()),
            "camera_under_terrain_cells": int((cells & measured["camera_under"]).sum()), "terrain_sightline_blocked_cells": int((cells & measured["sight_blocked"]).sum())}


def read_height_grid(folder, size, center_cm):
    raw = np.fromfile(os.path.join(folder, "height.r16"), dtype="<u2")
    return TDTerrainGrid((raw.reshape(size, size).astype(np.float64) - 32768.0) / 128.0, center_cm)


def read_road_mask(folder, size):
    return np.fromfile(os.path.join(folder, "layer_Road.r8"), dtype=np.uint8).reshape(size, size) >= 128


def load_layout_records(layout, extra_actors=(), skip_actor_prefix=None):
    ism = {}
    paths = {}
    for index, entry in enumerate(layout["ism"]):
        bucket = (entry["key"], index)
        ism[bucket] = entry["transforms"]
        paths[bucket] = entry["mesh"]
    actors = [e for e in layout["actors"] if not (skip_actor_prefix and e.get("label", "").startswith(skip_actor_prefix))]
    actors = actors + list(extra_actors)
    return collect_placements(ism, actors, lambda bucket: paths[bucket])


def measure_all(field_dir, realms_dir):
    import generate_ashen_vale as gav
    import generate_realm as gr
    import realm_passages as rp
    results = {}
    realm_layouts = {rid: json.load(open(os.path.join(realms_dir, rid, "layout.json"), encoding="utf-8")) for rid in gr.REALM_CONFIGS}
    field_layout = json.load(open(os.path.join(field_dir, "layout.json"), encoding="utf-8"))
    extras = [e for layout in realm_layouts.values() for e in layout["actors"] if e.get("label", "").startswith(FIELD_PASSAGE_LABEL_PREFIX)]
    grid = read_height_grid(field_dir, gav.SIZE, (0.0, 0.0))
    masks = gav.topdown_route_masks(grid, read_road_mask(field_dir, gav.SIZE))
    records = load_layout_records(field_layout, extras)
    measured = measure_region(records, grid, masks)
    subsets = {"route_excluding_passages": ~masks.floor_union}
    for passage, floor in zip(rp.PASSAGES, masks.floors):
        subsets[passage["gate_id"]] = floor
    results["field"] = {name: subset_stats(measured, masks, mask) for name, mask in subsets.items()}
    results["field"]["top_blockers_cells"] = dict(measured["by_asset"].most_common(8))
    for rid, cfg in gr.REALM_CONFIGS.items():
        folder = os.path.join(realms_dir, rid)
        layout = realm_layouts[rid]
        grid = read_height_grid(folder, gr.SIZE, tuple(layout["realm"]["center_cm"][:2]))
        masks = gr.topdown_route_masks(cfg, grid, read_road_mask(folder, gr.SIZE))
        records = load_layout_records(layout, skip_actor_prefix=FIELD_PASSAGE_LABEL_PREFIX)
        measured = measure_region(records, grid, masks)
        subsets = {"route_excluding_passages": ~masks.floor_union}
        gate_ids = [p["gate_id"] for p in rp.PASSAGES if p["realm_id"] == rid]
        for gate_id, floor in zip(gate_ids, masks.floors):
            subsets[gate_id] = floor
        results[rid] = {name: subset_stats(measured, masks, mask) for name, mask in subsets.items()}
        results[rid]["top_blockers_cells"] = dict(measured["by_asset"].most_common(8))
    return results


def format_table(results):
    lines = ["region/part | cells | occluded cells (%) | camera inside mesh | camera within 1.5m of mesh | camera below terrain | terrain blocks sightline"]
    for region, parts in results.items():
        for name, s in parts.items():
            if name == "top_blockers_cells":
                continue
            lines.append(f"{region}/{name} | {s['cells']} | {s['occluded_cells']} ({s['occluded_percent']}%) | {s['camera_inside_cells']} | {s['camera_near_1p5m_cells']} | {s['camera_under_terrain_cells']} | {s['terrain_sightline_blocked_cells']}")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description="Measure walkable-cell sightline occlusion and camera burial for generated layouts.")
    ap.add_argument("--measure", action="store_true")
    ap.add_argument("--label", required=True)
    project = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ap.add_argument("--field-dir", default=os.path.join(project, "Saved", "WorldGen", "AshenVale"))
    ap.add_argument("--realms-dir", default=os.path.join(project, "Saved", "WorldGen", "Realms"))
    ap.add_argument("--out-dir", default=os.path.join(project, "Saved", "WorldGen", "TopDownMetrics"))
    args = ap.parse_args()
    if not args.measure:
        ap.print_help()
        return 2
    results = measure_all(args.field_dir, args.realms_dir)
    os.makedirs(args.out_dir, exist_ok=True)
    out_path = os.path.join(args.out_dir, f"metrics_{args.label}.json")
    with open(out_path, "w", encoding="utf-8") as fp:
        json.dump(results, fp, ensure_ascii=False, indent=1)
    print(format_table(results))
    print("out:", out_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
