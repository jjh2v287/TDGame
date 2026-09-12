"""잿빛 골짜기(Ashen Vale) 메인 지역 생성기 (에디터 밖에서 실행, numpy).

입력: 시드. 출력(Saved/WorldGen/AshenVale/):
  height.r16            랜드스케이프 높이맵(uint16, 1009x1009, 리틀엔디언)
  layer_<이름>.r8       레이어 가중치(uint8, 1009x1009) — Soil/Moss/Mud/Road/Rock/Bedrock
  layout.json           배치 데이터(인스턴스 메시, 고유 액터, 물/안개 평면, 조명, 나이아가라, 마커)
  preview.png           검토용 상공 지도

실행: python Tools/WorldGen/generate_ashen_vale.py [--seed 7] [--out Saved/WorldGen/AshenVale]
그다음 에디터 안에서 Tools/WorldGen/editor_build_open_world.py 를 실행해 레벨에 반영한다.

좌표 규약: 월드 X = 지도 오른쪽(+), 월드 Y = 지도 아래쪽(+). 배열 인덱스는 [iy, ix]. 단위는 미터, 내보낼 때 cm.
"""
import argparse
import json
import math
import os
import sys

import numpy as np
from scipy import ndimage

SIZE = 1009
HALF = (SIZE - 1) // 2
QUAD_M = 1.0
Z_SCALE = 100.0
UNITS_PER_METER = 128.0 * (100.0 / Z_SCALE)
OUT_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "Saved", "WorldGen", "AshenVale")

MESH_ROOT = "/Game/DarkFantasyTopDown/StaticMeshes"
MAT_ROOT = "/Game/DarkFantasyTopDown/Materials"
ENGINE_PLANE = "/Engine/BasicShapes/Plane"
ENGINE_CUBE = "/Engine/BasicShapes/Cube"

M = {
    "Birch1": f"{MESH_ROOT}/Birch/SM_Birch1", "Birch2": f"{MESH_ROOT}/Birch/SM_Birch2", "Birch3": f"{MESH_ROOT}/Birch/SM_Birch3",
    "Bush": f"{MESH_ROOT}/Bush/SM_Bush",
    "Dread1": f"{MESH_ROOT}/Nature/Dreadplants/SM_Dreadplant1", "Dread2": f"{MESH_ROOT}/Nature/Dreadplants/SM_Dreadplant2",
    "Shroom1": f"{MESH_ROOT}/Nature/Dreadplants/SM_DreadplantMushroom1", "Shroom2": f"{MESH_ROOT}/Nature/Dreadplants/SM_DreadplantMushroom2",
    "Mycelium": f"{MESH_ROOT}/Nature/Dreadplants/SM_Mycelium1",
    "Rock1": f"{MESH_ROOT}/Rocks2/SM_Rock1", "Rock2": f"{MESH_ROOT}/Rocks2/SM_Rock2", "Rock3": f"{MESH_ROOT}/Rocks2/SM_Rock3",
    "Cliff2": f"{MESH_ROOT}/Nature/Cliff/SM_Cliff2", "Cliff3": f"{MESH_ROOT}/Nature/Cliff/SM_Cliff3", "Cliff4": f"{MESH_ROOT}/Nature/Cliff/SM_Cliff4", "Cliffs1": f"{MESH_ROOT}/Nature/Cliff/SM_Cliffs1",
    "SRock3": f"{MESH_ROOT}/Nature/Rocks/SmallRocks/SM_Rock_3", "SRock4": f"{MESH_ROOT}/Nature/Rocks/SmallRocks/SM_Rock_4", "SRock5": f"{MESH_ROOT}/Nature/Rocks/SmallRocks/SM_Rock_5",
    "SRock6": f"{MESH_ROOT}/Nature/Rocks/SmallRocks/SM_Rock_6", "SRock7": f"{MESH_ROOT}/Nature/Rocks/SmallRocks/SM_Rock_7", "SRock8": f"{MESH_ROOT}/Nature/Rocks/SmallRocks/SM_Rock_8",
    "RocksTile": f"{MESH_ROOT}/RocksTIle/SM_RocksTile",
    "LightBeam": f"{MESH_ROOT}/VFX/SM_LightBeam", "PlaneDecal": f"{MESH_ROOT}/VFX/SM_Plane",
    "Skull": f"{MESH_ROOT}/SmallProps/SM_Skull", "StickedSkull": f"{MESH_ROOT}/SmallProps/SM_StickedSkull", "RamSkull": f"{MESH_ROOT}/SmallProps/SM_RamSkull", "RamSkull2": f"{MESH_ROOT}/SmallProps/SM_RamSkull2",
    "Bone": f"{MESH_ROOT}/SmallProps/SM_Bone", "Beads1": f"{MESH_ROOT}/SmallProps/SM_Beads1", "Beads2": f"{MESH_ROOT}/SmallProps/SM_Beads2", "ChainCurved": f"{MESH_ROOT}/SmallProps/SM_ChainCurved",
    "Bell": f"{MESH_ROOT}/SmallProps/SM_Bell", "WoodenBrick": f"{MESH_ROOT}/SmallProps/SM_WoodenBrick", "WoodenStick": f"{MESH_ROOT}/SmallProps/SM_WoodenStick", "WoodenBall": f"{MESH_ROOT}/SmallProps/SM_WoodenBall",
    "Lamppost": f"{MESH_ROOT}/Lanterns/SM_Lamppost", "Lamp": f"{MESH_ROOT}/Lanterns/SM_Lamp", "Candles": f"{MESH_ROOT}/Lanterns/SM_Candles", "CandlesGroup": f"{MESH_ROOT}/Lanterns/SM_CandlesGroup", "Bow2": f"{MESH_ROOT}/Lanterns/SM_Bow2", "Bowl": f"{MESH_ROOT}/Lanterns/SM_Bowl",
    "Barrel1": f"{MESH_ROOT}/Barrels/SM_Barrel1", "Barrel2": f"{MESH_ROOT}/Barrels/SM_Barrel2", "Barrels1": f"{MESH_ROOT}/Barrels/SM_Barrels1", "BarrelEmpty": f"{MESH_ROOT}/Barrels/SM_Barrel1_Empty",
    "Crate": f"{MESH_ROOT}/Containers/SM_WoodenCrate", "Crate2": f"{MESH_ROOT}/Containers/SM_WoodenCrate2", "Crate4": f"{MESH_ROOT}/Containers/SM_WoodenCrate4", "Crate3": f"{MESH_ROOT}/Food/SM_WoodenCrate3",
    "Chest": f"{MESH_ROOT}/Containers/SM_WoodenChest", "ChestEmpty": f"{MESH_ROOT}/Containers/SM_WoodenChestEmpty",
    "Pumpkin": f"{MESH_ROOT}/Food/SM_Pumkin", "Cabbage": f"{MESH_ROOT}/Food/SM_Cabbage", "Bread": f"{MESH_ROOT}/Food/SM_Bread", "Apple": f"{MESH_ROOT}/Food/SM_Apple", "Fish": f"{MESH_ROOT}/Food/SM_Fish",
    "Bucket": f"{MESH_ROOT}/Props1/SM_Bucket", "Bottle": f"{MESH_ROOT}/Props1/SM_Bottle", "Axe": f"{MESH_ROOT}/Props1/SM_Axe", "Pot": f"{MESH_ROOT}/Props1/SM_Pot", "Cup": f"{MESH_ROOT}/Props1/SM_Cup",
    "Bag": f"{MESH_ROOT}/Props2/SM_Bag", "Cauldron": f"{MESH_ROOT}/Props2/SM_Cauldron", "Trough": f"{MESH_ROOT}/Props2/SM_Trough", "Pitchfork": f"{MESH_ROOT}/Props2/SM_Pitchfork", "Shovel": f"{MESH_ROOT}/Props2/SM_Shovel", "Pan": f"{MESH_ROOT}/Props2/SM_Pan",
    "Anvil": f"{MESH_ROOT}/Props3/SM_Anvil", "Bench": f"{MESH_ROOT}/Props3/SM_Bench", "Stool": f"{MESH_ROOT}/Props3/SM_Stool", "Table": f"{MESH_ROOT}/Props3/SM_Table", "Hammer": f"{MESH_ROOT}/Props3/SM_Hammer",
    "Sword": f"{MESH_ROOT}/Weapons/SM_Sword", "Shield": f"{MESH_ROOT}/Weapons/SM_Shield", "WAxe": f"{MESH_ROOT}/Weapons/SM_Axe",
    "Armory": f"{MESH_ROOT}/WoodenParts/SM_Armory", "Cart": f"{MESH_ROOT}/WoodenParts/SM_Cart", "FishTable": f"{MESH_ROOT}/WoodenParts/SM_FishTable", "Shelving": f"{MESH_ROOT}/WoodenParts/SM_WoodenShelving",
    "WC1": f"{MESH_ROOT}/WoodenParts/SM_WoodenConstruction1", "WC2": f"{MESH_ROOT}/WoodenParts/SM_WoodenConstruction2", "WC3": f"{MESH_ROOT}/WoodenParts/SM_WoodenConstruction3", "WC4": f"{MESH_ROOT}/WoodenParts/SM_WoodenConstruction4",
    "WC5": f"{MESH_ROOT}/WoodenParts/SM_WoodenConstruction5", "WC6": f"{MESH_ROOT}/WoodenParts/SM_WoodenConstruction6", "WC7": f"{MESH_ROOT}/WoodenParts/SM_WoodenConstruction7", "WC9": f"{MESH_ROOT}/WoodenParts/SM_WoodenConstruction9", "WC10": f"{MESH_ROOT}/WoodenParts/SM_WoodenConstruction10",
    "Fence1": f"{MESH_ROOT}/WoodenParts/SM_WoodenFence1", "Fence2": f"{MESH_ROOT}/WoodenParts/SM_WoodenFence2", "FloorSeg": f"{MESH_ROOT}/WoodenParts/SM_WoodenFloorSegment", "Flooring": f"{MESH_ROOT}/WoodenParts/SM_WoodenFlooring1",
    "Ladder": f"{MESH_ROOT}/WoodenParts/SM_WoodenLadder", "Path": f"{MESH_ROOT}/WoodenParts/SM_WoodenPath", "Pole": f"{MESH_ROOT}/WoodenParts/SM_WoodenPole", "Pole2": f"{MESH_ROOT}/WoodenParts/SM_WoodenPole2",
    "Steps": f"{MESH_ROOT}/WoodenParts/SM_WoodenSteps", "Wheel": f"{MESH_ROOT}/WoodenParts/SM_WoodenWheel", "Decor": f"{MESH_ROOT}/WoodenParts/SM_WoodenDecor",
    "Part1": f"{MESH_ROOT}/WoodenParts/SM_WoodenPart1", "Part2": f"{MESH_ROOT}/WoodenParts/SM_WoodenPart2", "Part3": f"{MESH_ROOT}/WoodenParts/SM_WoodenPart3", "Part4": f"{MESH_ROOT}/WoodenParts/SM_WoodenPart4",
    "Part5": f"{MESH_ROOT}/WoodenParts/SM_WoodenPart5", "Part6": f"{MESH_ROOT}/WoodenParts/SM_WoodenPart6", "Part7": f"{MESH_ROOT}/WoodenParts/SM_WoodenPart7", "Part8": f"{MESH_ROOT}/WoodenParts/SM_WoodenPart8",
}

# 메시 피벗 아래로 내려가는 깊이(m). 바닥이 지면에 닿게 하려면 이만큼 올리고, 자연물은 조금 묻는다.
MESH_BASE_BELOW_PIVOT = {"Birch1": 0.29, "Birch2": 0.24, "Birch3": 0.59, "Bush": 0.0, "Rock1": 0.10, "Rock2": 0.14, "Rock3": 0.20,
                         "Cliff2": 0.49, "Cliff3": 0.15, "Cliff4": 2.39, "Cliffs1": 0.11, "Lamppost": 0.06, "Pole": 0.08, "Pole2": 1.26, "WC9": 0.23, "WC7": 0.08,
                         "Fence1": 0.06, "LightBeam": 1.28, "StickedSkull": 0.01, "Path": 0.05, "Cart": 0.04, "Trough": 0.04}

MATS = {
    "Water": f"{MAT_ROOT}/WaterPlane/MI_WaterPlane",
    "Fog1": f"{MAT_ROOT}/VFX/MI_FogPlane_Var1", "Fog2": f"{MAT_ROOT}/VFX/MI_FogPlane_Var2",
    "Puddles": f"{MAT_ROOT}/Nature/Surfaces/MI_PuddlesDecal",
    "LocalFog": f"{MAT_ROOT}/VFX/MI_VolumetricLocalFog",
    "Bedrock": f"{MAT_ROOT}/Nature/Surfaces/MI_Bedrock2",
    "LightShaft": f"{MAT_ROOT}/LightFunctions/MI_LightShaft_Inst",
}
NIAGARA = {"Leaves": "/Game/DarkFantasyTopDown/VFX/FXS_Leaves", "Dust1": "/Game/DarkFantasyTopDown/VFX/FXS_DustVar1", "Dust2": "/Game/DarkFantasyTopDown/VFX/FXS_DustVar2", "Worms": "/Game/DarkFantasyTopDown/VFX/FXS_Worms"}


# ---------------------------------------------------------------- 수학 도구
def smoothstep(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def value_noise(rng, cells, size=SIZE):
    grid = rng.random((cells + 3, cells + 3))
    zoom = (size / cells)
    big = ndimage.zoom(grid, zoom, order=3, prefilter=True)
    return big[:size, :size]


def fbm(rng, base_cells, octaves, gain=0.5, lacunarity=2.0):
    out = np.zeros((SIZE, SIZE), dtype=np.float64)
    amp = 1.0
    cells = base_cells
    total = 0.0
    for _ in range(octaves):
        out += amp * (value_noise(rng, int(cells)) - 0.5) * 2.0
        total += amp
        amp *= gain
        cells *= lacunarity
    return out / total


def grid_coords():
    ix = np.arange(SIZE, dtype=np.float64)
    xs = (ix - HALF) * QUAD_M
    X, Y = np.meshgrid(xs, xs)
    return X, Y


def radial(X, Y, cx, cy, r_inner, r_outer):
    d = np.hypot(X - cx, Y - cy)
    return 1.0 - smoothstep(r_inner, r_outer, d)


def catmull_rom(points, step=0.5):
    pts = [np.array(p, dtype=np.float64) for p in points]
    pts = [pts[0]] + pts + [pts[-1]]
    out = []
    for i in range(1, len(pts) - 2):
        p0, p1, p2, p3 = pts[i - 1], pts[i], pts[i + 1], pts[i + 2]
        seg_len = np.linalg.norm(p2 - p1)
        n = max(2, int(seg_len / step))
        for k in range(n):
            t = k / n
            t2, t3 = t * t, t * t * t
            out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3))
    out.append(pts[-2])
    return np.array(out)


def rasterize_polyline(poly, values=None):
    mask = np.zeros((SIZE, SIZE), dtype=bool)
    vals = np.zeros((SIZE, SIZE), dtype=np.float64) if values is not None else None
    ix = np.clip(np.round(poly[:, 0] + HALF).astype(int), 0, SIZE - 1)
    iy = np.clip(np.round(poly[:, 1] + HALF).astype(int), 0, SIZE - 1)
    mask[iy, ix] = True
    if values is not None:
        vals[iy, ix] = values
    return mask, vals


def distance_and_nearest(mask):
    dist, (ny, nx) = ndimage.distance_transform_edt(~mask, return_indices=True)
    return dist, ny, nx


def sample_bilinear(arr, xm, ym):
    fx = np.clip(xm + HALF, 0, SIZE - 1.001)
    fy = np.clip(ym + HALF, 0, SIZE - 1.001)
    x0 = np.floor(fx).astype(int)
    y0 = np.floor(fy).astype(int)
    tx = fx - x0
    ty = fy - y0
    a = arr[y0, x0]
    b = arr[y0, x0 + 1]
    c = arr[y0 + 1, x0]
    d = arr[y0 + 1, x0 + 1]
    return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty


# ---------------------------------------------------------------- 월드 정의(수작업 뼈대)
VILLAGE = (-110.0, 20.0)
VILLAGE_R = 92.0
GRAVEYARD = (10.0, -330.0)
HIGHLAND_C = (330.0, -300.0)
MAIN_DUNGEON = (296.0, -268.0)
MAIN_DUNGEON_FACING = 135.0
SIDE_DUNGEON_NW = (-372.0, -262.0)
SIDE_DUNGEON_NW_FACING = -14.0
SIDE_DUNGEON_SE = (318.0, 412.0)
SIDE_DUNGEON_SE_FACING = 196.0
MARSH_C = (300.0, 330.0)
LAKE = (330.0, 320.0)
LAKE_R = 52.0
BRIDGE = (62.0, 26.0)

RIVER_PTS = [(128.0, -560.0), (118.0, -420.0), (92.0, -280.0), (60.0, -150.0), (55.0, -40.0), (62.0, 26.0), (85.0, 110.0), (120.0, 200.0), (180.0, 290.0), (250.0, 380.0), (300.0, 470.0), (330.0, 560.0)]

POIS = {
    "Graveyard": {"pos": GRAVEYARD, "r": 40.0},
    "Shrine": {"pos": (-250.0, -150.0), "r": 22.0},
    "HunterCamp": {"pos": (-330.0, 130.0), "r": 24.0},
    "Watchtower": {"pos": (150.0, 120.0), "r": 22.0},
    "FishingDock": {"pos": (30.0, -8.0), "r": 18.0},
    "MushroomGrove": {"pos": (-390.0, 300.0), "r": 30.0},
    "AmbushCart": {"pos": (225.0, -175.0), "r": 16.0},
    "BonePit": {"pos": (145.0, 310.0), "r": 24.0},
    "WayshrineNorth": {"pos": (-179.0, -318.0), "r": 8.0},
    "WayshrineEast": {"pos": (312.0, 140.0), "r": 8.0},
    "WayshrineSouth": {"pos": (-4.0, 250.0), "r": 8.0},
    "ArenaNorth": {"pos": (-80.0, -230.0), "r": 26.0},
    "ArenaEast": {"pos": (270.0, -60.0), "r": 28.0},
    "ArenaWest": {"pos": (-300.0, 20.0), "r": 24.0},
}

ROADS = {
    "MainEast": [(-110.0, 20.0), (-40.0, 22.0), (10.0, 24.0), BRIDGE, (110.0, 10.0), (170.0, -60.0), (225.0, -140.0), (262.0, -228.0), MAIN_DUNGEON],
    "North": [(-110.0, 20.0), (-108.0, -60.0), (-90.0, -160.0), (-60.0, -240.0), (-20.0, -300.0), GRAVEYARD],
    "GraveToNW": [GRAVEYARD, (-80.0, -350.0), (-170.0, -330.0), (-250.0, -300.0), (-320.0, -275.0), SIDE_DUNGEON_NW],
    "GraveToEast": [(40.0, -320.0), (110.0, -300.0), (170.0, -240.0), (210.0, -175.0), (225.0, -140.0)],
    "South": [(-110.0, 20.0), (-105.0, 120.0), (-70.0, 200.0), (-10.0, 260.0), (60.0, 300.0), (130.0, 330.0), (200.0, 360.0), (260.0, 395.0), SIDE_DUNGEON_SE],
    "West": [(-110.0, 20.0), (-200.0, 22.0), (-270.0, 40.0), (-330.0, 80.0), (-345.0, 160.0), (-370.0, 240.0), (-388.0, 292.0)],
    "ShrineSpur": [(-250.0, -300.0), (-252.0, -220.0), (-250.0, -150.0)],
    "TowerSpur": [(110.0, 10.0), (130.0, 60.0), (150.0, 120.0)],
    "EastLoop": [(170.0, -60.0), (230.0, -55.0), (270.0, -60.0), (330.0, -110.0), (330.0, -180.0), (296.0, -230.0), (262.0, -228.0)],
    "MarshLoop": [(130.0, 330.0), (170.0, 300.0), (210.0, 330.0), (240.0, 300.0), (270.0, 250.0), (300.0, 200.0), (330.0, 120.0), (330.0, 40.0), (300.0, -10.0), (270.0, -60.0)],
}
ROAD_WIDTH = {"MainEast": 7.0, "North": 6.0, "GraveToNW": 5.0, "GraveToEast": 4.5, "South": 6.0, "West": 5.5, "ShrineSpur": 3.5, "TowerSpur": 3.5, "EastLoop": 4.0, "MarshLoop": 4.0}


# ---------------------------------------------------------------- 지형
def build_terrain(rng):
    X, Y = grid_coords()
    h = 2.8 * fbm(rng, 4, 3) + 1.1 * fbm(rng, 16, 3) + 0.22 * fbm(rng, 80, 2)

    west = smoothstep(-120.0, -260.0, X)
    h += west * (4.5 + 5.0 * fbm(rng, 8, 3))

    d_high = np.hypot(X - HIGHLAND_C[0], Y - HIGHLAND_C[1])
    high = 1.0 - smoothstep(110.0, 280.0, d_high)
    h += high * (13.0 + 4.0 * fbm(rng, 10, 3))
    plateau = 1.0 - smoothstep(96.0, 112.0, np.hypot(X - 362.0, Y - 335.0 * -1.0))
    h += plateau * 8.0

    for (ex, ey), facing in ((SIDE_DUNGEON_NW, SIDE_DUNGEON_NW_FACING), (SIDE_DUNGEON_SE, SIDE_DUNGEON_SE_FACING), (MAIN_DUNGEON, MAIN_DUNGEON_FACING)):
        bx = ex - 30.0 * math.cos(math.radians(facing))
        by = ey - 30.0 * math.sin(math.radians(facing))
        mound = radial(X, Y, bx, by, 16.0, 29.0)
        h = h + mound * 9.5

    north_ridge = smoothstep(-330.0, -430.0, Y) * (1.0 - smoothstep(-60.0, 40.0, X)) * 6.0
    h += north_ridge

    village_m = radial(X, Y, VILLAGE[0], VILLAGE[1], VILLAGE_R - 6.0, VILLAGE_R + 22.0)
    h = h * (1.0 - village_m) + village_m * (5.2 + 0.25 * fbm(rng, 60, 2))

    grave_m = radial(X, Y, GRAVEYARD[0], GRAVEYARD[1], 44.0, 70.0)
    h = h * (1.0 - grave_m) + grave_m * (3.6 + 0.2 * fbm(rng, 60, 2))

    for name, poi in POIS.items():
        if name.startswith("Arena") or name in ("Shrine", "HunterCamp", "MushroomGrove", "BonePit", "Watchtower"):
            m = radial(X, Y, poi["pos"][0], poi["pos"][1], poi["r"] - 2.0, poi["r"] + 16.0)
            target = np.mean(h[np.hypot(X - poi["pos"][0], Y - poi["pos"][1]) < poi["r"]])
            h = h * (1.0 - m) + m * (target + 0.15 * fbm(rng, 70, 2))

    marsh_edge = 210.0 + 40.0 * fbm(rng, 6, 2)
    marsh_m = 1.0 - smoothstep(marsh_edge - 60.0, marsh_edge + 30.0, np.hypot(X - MARSH_C[0], Y - MARSH_C[1]))
    h = h * (1.0 - marsh_m) + marsh_m * (-0.7 + 0.35 * fbm(rng, 40, 2))

    river_poly = catmull_rom(RIVER_PTS, 0.5)
    river_mask, _ = rasterize_polyline(river_poly)
    river_dist, _, _ = distance_and_nearest(river_mask)
    valley = 1.0 - smoothstep(14.0, 70.0, river_dist)
    h = h * (1.0 - valley * 0.55)

    lake_m = radial(X, Y, LAKE[0], LAKE[1], LAKE_R, LAKE_R + 26.0)
    h = np.minimum(h, h * (1.0 - lake_m) + lake_m * (-3.2))

    edge_d = np.minimum.reduce([X + HALF, HALF - X, Y + HALF, HALF - Y])
    rim = smoothstep(0.0, 1.0, (1.0 - smoothstep(0.0, 100.0, edge_d)))
    rim_h = 46.0 * rim ** 1.7 + 3.0 * (1.0 - smoothstep(100.0, 170.0, edge_d))
    rim_h *= (1.0 + 0.35 * fbm(rng, 12, 3))
    h = np.maximum(h, h * (1.0 - rim) + rim_h)

    h = ndimage.gaussian_filter(h, 1.1)

    road_mask_all = np.zeros((SIZE, SIZE), dtype=bool)
    road_weight = np.zeros((SIZE, SIZE), dtype=np.float64)
    road_layer = np.zeros((SIZE, SIZE), dtype=np.float64)
    road_polys = {}
    h_base = h.copy()
    for name, pts in ROADS.items():
        poly = catmull_rom(pts, 0.5)
        road_polys[name] = poly
        prof = sample_bilinear(h_base, poly[:, 0], poly[:, 1])
        win = 61
        prof = ndimage.uniform_filter1d(prof, win, mode="nearest")
        prof = ndimage.uniform_filter1d(prof, win, mode="nearest")
        mask, vals = rasterize_polyline(poly, prof)
        dist, ny, nx = distance_and_nearest(mask)
        w_flat = ROAD_WIDTH[name] * 0.5
        w = 1.0 - smoothstep(w_flat, w_flat + 6.0, dist)
        target = vals[ny, nx]
        h = h * (1.0 - w) + w * target
        road_weight = np.maximum(road_weight, w)
        road_layer = np.maximum(road_layer, 1.0 - smoothstep(w_flat - 0.5, w_flat + 1.5, dist))
        road_mask_all |= dist < (w_flat + 1.0)

    river_w = 1.0 - smoothstep(4.5, 11.0, river_dist)
    bed = -3.1 + 0.25 * fbm(rng, 90, 2)
    h = np.minimum(h, h * (1.0 - river_w) + river_w * bed)
    h = np.minimum(h, np.where(river_dist < 4.5, bed, h))

    slope = terrain_slope_deg(h)
    fields = {
        "X": X, "Y": Y, "h": h, "slope": slope, "river_dist": river_dist, "river_poly": river_poly,
        "road_weight": road_weight, "road_layer": road_layer, "road_mask": road_mask_all, "road_polys": road_polys,
        "marsh": marsh_m, "village": village_m, "grave": grave_m, "high": high, "west": west, "edge_d": edge_d, "rim": rim, "lake": lake_m,
    }
    return fields


def terrain_slope_deg(h):
    gy, gx = np.gradient(h, QUAD_M)
    return np.degrees(np.arctan(np.hypot(gx, gy)))


def build_layers(rng, f):
    h, slope = f["h"], f["slope"]
    n1 = value_noise(rng, 24)
    n2 = value_noise(rng, 90)
    bedrock = smoothstep(33.0, 46.0, slope)
    rock = smoothstep(17.0, 32.0, slope) * (1.0 - bedrock)
    rock = np.maximum(rock, f["high"] * smoothstep(0.45, 0.75, n1) * 0.7)
    rock = np.maximum(rock, (1.0 - smoothstep(60.0, 130.0, f["edge_d"])) * smoothstep(0.35, 0.7, n2) * 0.6)
    mud = np.maximum(f["marsh"] * (0.55 + 0.45 * smoothstep(0.3, 0.7, n2)), 1.0 - smoothstep(6.0, 16.0, f["river_dist"]))
    mud = np.maximum(mud, f["lake"])
    mud = np.where(h < -0.2, np.maximum(mud, 0.8), mud)
    road = f["road_layer"]
    road = np.maximum(road, f["village"] * 0.25 * smoothstep(0.4, 0.8, n2))
    mud = np.maximum(mud, f["village"] * (0.45 + 0.35 * smoothstep(0.3, 0.7, n1)))
    road = np.maximum(road, f["grave"] * 0.35 * smoothstep(0.4, 0.7, n1))
    forest = f["forest_density"]
    moss = np.clip(forest * 1.3, 0, 1) * (0.5 + 0.5 * smoothstep(0.3, 0.75, n1)) * (1.0 - smoothstep(12.0, 24.0, slope))
    moss = np.maximum(moss, f["grove"] * 0.9)

    # 경계 얼룩: 고주파 노이즈로 가중치를 흔들어 레이어 경계가 직선·원형으로 보이지 않게 한다
    n_fine = value_noise(rng, 220)
    n_mid = value_noise(rng, 70)
    mottle = 0.55 + 0.9 * smoothstep(0.35, 0.7, 0.5 * n_fine + 0.5 * n_mid)
    moss = np.clip(moss * mottle, 0, 1)
    mud = np.clip(mud * (0.6 + 0.8 * smoothstep(0.3, 0.7, n_mid)), 0, 1)
    rock = np.clip(rock * mottle, 0, 1)
    # 바탕 흙 안에도 진흙·바위 얼룩을 조금 섞어 단색 넓은 면을 없앤다
    mud = np.maximum(mud, 0.28 * smoothstep(0.55, 0.8, n_mid) * (1.0 - road))
    rock = np.maximum(rock, 0.10 * smoothstep(0.6, 0.85, n_mid) * (1.0 - road))
    layers = {}
    remaining = np.ones((SIZE, SIZE))
    for name, w in (("Road", road), ("Bedrock", bedrock), ("Rock", rock), ("Mud", mud), ("Moss", moss)):
        w = np.clip(w, 0.0, 1.0) * remaining
        layers[name] = w
        remaining = remaining - w
    layers["Soil"] = np.clip(remaining, 0.0, 1.0)
    total = sum(layers.values())
    for k in layers:
        layers[k] = layers[k] / total
    return layers


# ---------------------------------------------------------------- 배치
class Layout:
    def __init__(self, f, rng):
        self.f = f
        self.rng = rng
        self.ism = {}
        self.actors = []
        self.planes = []
        self.lights = []
        self.niagara = []
        self.markers = []
        self.occupied = []
        self.cell_m = 126.0

    def height(self, x, y):
        return float(sample_bilinear(self.f["h"], np.array([x]), np.array([y]))[0])

    def slope(self, x, y):
        return float(sample_bilinear(self.f["slope"], np.array([x]), np.array([y]))[0])

    def add_ism(self, key, x, y, yaw, scale, roll=0.0, pitch=0.0, z_off=0.0, sink=0.0, z=None):
        if z is None:
            z = self.height(x, y)
        z = z + MESH_BASE_BELOW_PIVOT.get(key, 0.0) * scale - sink + z_off
        cx = int((x + HALF) // self.cell_m)
        cy = int((y + HALF) // self.cell_m)
        bucket = self.ism.setdefault((key, cx, cy), [])
        bucket.append([round(x * 100, 1), round(y * 100, 1), round(z * 100, 1), round(pitch, 2), round(yaw, 2), round(roll, 2), round(scale, 3), round(scale, 3), round(scale, 3)])

    def add_actor(self, key, x, y, yaw, scale=1.0, roll=0.0, pitch=0.0, z_off=0.0, label=None, z=None, scale3=None, mesh_path=None, materials=None, tags=None):
        if z is None:
            z = self.height(x, y)
        z = z + MESH_BASE_BELOW_PIVOT.get(key, 0.0) * scale + z_off
        s3 = scale3 if scale3 else [scale, scale, scale]
        e = {"mesh": mesh_path or M[key], "loc": [round(x * 100, 1), round(y * 100, 1), round(z * 100, 1)], "rot": [round(pitch, 2), round(yaw, 2), round(roll, 2)], "scale": [round(v, 3) for v in s3], "label": label or f"TDGen_{key}"}
        if materials:
            e["materials"] = materials
        if tags:
            e["tags"] = tags
        self.actors.append(e)
        return e

    def add_plane(self, mat, x, y, z, yaw, sx, sy, label, mesh=ENGINE_PLANE, pitch=0.0, roll=0.0, sz=1.0):
        self.planes.append({"mesh": mesh, "material": MATS[mat], "loc": [round(x * 100, 1), round(y * 100, 1), round(z * 100, 1)], "rot": [pitch, round(yaw, 2), roll], "scale": [round(sx, 3), round(sy, 3), sz], "label": label})

    def add_light(self, kind, x, y, z_off, intensity, color, radius, label, temperature=None, yaw=0.0, pitch=-90.0, cone=40.0, inner=15.0, source_radius=8.0, light_function=None, volumetric=1.0, cast_shadows=True, z=None):
        if z is None:
            z = self.height(x, y)
        e = {"type": kind, "loc": [round(x * 100, 1), round(y * 100, 1), round((z + z_off) * 100, 1)], "rot": [pitch, yaw, 0.0], "intensity": intensity, "color": color, "radius": radius * 100, "label": label,
             "source_radius": source_radius, "volumetric": volumetric, "cast_shadows": cast_shadows}
        if temperature:
            e["temperature"] = temperature
        if kind == "Spot":
            e["cone"] = cone
            e["inner"] = inner
        if light_function:
            e["light_function"] = MATS[light_function]
        self.lights.append(e)

    def add_niagara(self, key, x, y, z_off, label):
        self.niagara.append({"asset": NIAGARA[key], "loc": [round(x * 100, 1), round(y * 100, 1), round((self.height(x, y) + z_off) * 100, 1)], "label": label})

    def add_marker(self, kind, x, y, yaw, label, z_off=0.0, tags=None, scale=None):
        e = {"kind": kind, "loc": [round(x * 100, 1), round(y * 100, 1), round((self.height(x, y) + z_off) * 100, 1)], "rot": [0.0, yaw, 0.0], "label": label}
        if tags:
            e["tags"] = tags
        if scale:
            e["scale"] = scale
        self.markers.append(e)

    def reserve(self, x, y, r):
        self.occupied.append((x, y, r))


def local_to_world(cx, cy, yaw_deg, lx, ly):
    a = math.radians(yaw_deg)
    return cx + lx * math.cos(a) - ly * math.sin(a), cy + lx * math.sin(a) + ly * math.cos(a)


def build_exclusion(f, extra_circles):
    X, Y = f["X"], f["Y"]
    excl = np.zeros((SIZE, SIZE), dtype=np.float64)
    excl = np.maximum(excl, 1.0 - smoothstep(0.0, 4.0, np.maximum(0.0, f["river_dist"] - 7.5)))
    excl = np.maximum(excl, smoothstep(0.02, 0.6, f["road_weight"]))
    excl = np.maximum(excl, radial(X, Y, VILLAGE[0], VILLAGE[1], VILLAGE_R - 8.0, VILLAGE_R + 2.0))
    excl = np.maximum(excl, f["lake"])
    for (cx, cy, r) in extra_circles:
        excl = np.maximum(excl, radial(X, Y, cx, cy, r, r + 5.0))
    excl = np.maximum(excl, smoothstep(-2.6, -1.4, -f["h"]) * (f["h"] < -1.2))
    return np.clip(excl, 0.0, 1.0)


def scatter(rng, density, cell, min_dist, count_cap=None):
    """격자+지터 다트 던지기와 공간 해시 최소 간격 검사. density는 0~1 확률장."""
    n = int(SIZE // cell)
    xs = (np.arange(n) + 0.5) * cell - HALF
    gx, gy = np.meshgrid(xs, xs)
    gx = gx + rng.uniform(-0.5, 0.5, gx.shape) * cell
    gy = gy + rng.uniform(-0.5, 0.5, gy.shape) * cell
    p = sample_bilinear(density, gx.ravel(), gy.ravel())
    keep = rng.random(p.shape) < p
    px = gx.ravel()[keep]
    py = gy.ravel()[keep]
    order = rng.permutation(len(px))
    px, py = px[order], py[order]
    if min_dist <= 0:
        return px, py
    hashsz = min_dist
    grid = {}
    out_x, out_y = [], []
    for x, y in zip(px, py):
        kx, ky = int((x + HALF) // hashsz), int((y + HALF) // hashsz)
        ok = True
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for (ox, oy) in grid.get((kx + dx, ky + dy), ()):
                    if (ox - x) ** 2 + (oy - y) ** 2 < min_dist * min_dist:
                        ok = False
                        break
                if not ok:
                    break
            if not ok:
                break
        if ok:
            grid.setdefault((kx, ky), []).append((x, y))
            out_x.append(x)
            out_y.append(y)
            if count_cap and len(out_x) >= count_cap:
                break
    return np.array(out_x), np.array(out_y)


def build_forest_density(rng, f, excl):
    X, Y = f["X"], f["Y"]
    n_clear = fbm(rng, 5, 3)
    n_var = fbm(rng, 14, 2)
    base = 0.80 + 0.15 * f["west"]
    base = base * (1.0 - f["high"]) + f["high"] * 0.30
    base = base * (1.0 - f["marsh"]) + f["marsh"] * 0.34
    clearing = smoothstep(0.22, 0.42, n_clear)
    base = base * (1.0 - 0.9 * clearing)
    base = base * (0.8 + 0.4 * (n_var + 0.5))
    base = base * (1.0 - smoothstep(26.0, 36.0, f["slope"]))
    rim_forest = (1.0 - smoothstep(30.0, 90.0, f["edge_d"])) * 0.55 * (1.0 - smoothstep(32.0, 42.0, f["slope"]))
    base = np.maximum(base, rim_forest)
    base = base * (1.0 - excl)
    base = np.where(f["h"] < -1.0, 0.0, base)
    return np.clip(base, 0.0, 1.0)


def place_forest(L, rng, f, excl):
    dens = f["forest_density"]
    px, py = scatter(rng, dens, 3.0, 3.7)
    kinds = rng.random(len(px))
    for x, y, k in zip(px, py, kinds):
        d = float(sample_bilinear(dens, np.array([x]), np.array([y]))[0])
        marsh = float(sample_bilinear(f["marsh"], np.array([x]), np.array([y]))[0])
        if k < 0.40:
            key, s = "Birch1", rng.uniform(0.85, 1.35)
        elif k < 0.75:
            key, s = "Birch2", rng.uniform(0.8, 1.3)
        else:
            key, s = "Birch3", rng.uniform(0.7, 1.15)
        if marsh > 0.5:
            s *= 0.85
        L.add_ism(key, x, y, rng.uniform(0, 360), s, roll=rng.uniform(-3, 3), pitch=rng.uniform(-3, 3), sink=0.12)
    ux, uy = scatter(rng, np.clip(dens - 0.45, 0, 1) * 0.8, 3.0, 2.3)
    for x, y in zip(ux, uy):
        L.add_ism("Birch3", x, y, rng.uniform(0, 360), rng.uniform(0.6, 1.0), roll=rng.uniform(-4, 4), pitch=rng.uniform(-4, 4), sink=0.15)
    bush_d = np.clip(dens * 0.55 + 0.12 * (1.0 - excl) * (dens > 0.05), 0, 1) * (1.0 - f["marsh"] * 0.5)
    bx, by = scatter(rng, bush_d, 4.0, 2.6)
    for x, y in zip(bx, by):
        L.add_ism("Bush", x, y, rng.uniform(0, 360), rng.uniform(0.7, 1.35), sink=0.05)
    dread_d = np.clip(f["marsh"] * 0.5 + f["grove"] * 0.7 + dens * 0.18, 0, 1) * (1.0 - excl)
    dx, dy = scatter(rng, dread_d, 5.0, 3.0)
    for x, y in zip(dx, dy):
        key = "Dread1" if rng.random() < 0.5 else "Dread2"
        L.add_ism(key, x, y, rng.uniform(0, 360), rng.uniform(0.28, 0.62), sink=0.03)
    sx, sy = scatter(rng, np.clip(f["grove"] * 0.9 + f["marsh"] * 0.25 + dens * 0.15, 0, 1) * (1.0 - excl), 4.5, 2.0)
    for x, y in zip(sx, sy):
        key = "Shroom1" if rng.random() < 0.6 else "Shroom2"
        L.add_ism(key, x, y, rng.uniform(0, 360), rng.uniform(0.5, 1.4))
    beam_d = np.clip(dens - 0.35, 0, 1) * 0.5 * (1.0 - f["marsh"])
    beam_d = np.maximum(beam_d, f["grove"] * 0.6)
    lx, ly = scatter(rng, beam_d, 22.0, 26.0)
    for x, y in zip(lx, ly):
        s = rng.uniform(4.0, 9.0)
        L.add_ism("LightBeam", x, y, rng.uniform(0, 360), s, pitch=rng.uniform(-8, 8), roll=rng.uniform(-8, 8), z_off=s * 0.62 + 2.5)
    return len(px) + len(ux), len(bx), len(dx)


def place_rocks(L, rng, f, excl):
    X, Y = f["X"], f["Y"]
    slope = f["slope"]
    steep = smoothstep(26.0, 38.0, slope) * (1.0 - smoothstep(48.0, 60.0, slope))
    rim = 1.0 - smoothstep(35.0, 95.0, f["edge_d"])
    big_d = np.clip(steep * 0.55 + rim * 0.5 + f["high"] * 0.16 + (1.0 - smoothstep(9.0, 20.0, f["river_dist"])) * 0.10, 0, 1) * (1.0 - excl * 0.9)
    big_d = np.where(f["h"] < -1.0, 0.0, big_d)
    bx, by = scatter(rng, big_d, 6.0, 7.5)
    for x, y in zip(bx, by):
        sl = L.slope(x, y)
        ed = float(sample_bilinear(f["edge_d"], np.array([x]), np.array([y]))[0])
        if ed < 95.0 or sl > 30.0:
            key = ["Cliff4", "Cliff2", "Cliff3", "Cliffs1"][int(rng.integers(0, 4))]
            s = rng.uniform(1.1, 2.4) if ed < 95.0 else rng.uniform(0.7, 1.5)
            L.add_ism(key, x, y, rng.uniform(0, 360), s, roll=rng.uniform(-14, 14), pitch=rng.uniform(-14, 14), sink=0.6 * s)
        else:
            key = ["Rock1", "Rock2", "Rock3"][int(rng.integers(0, 3))]
            L.add_ism(key, x, y, rng.uniform(0, 360), rng.uniform(0.35, 0.85), roll=rng.uniform(-6, 6), pitch=rng.uniform(-6, 6), sink=0.25)
    small_d = np.clip((1.0 - smoothstep(6.0, 15.0, f["river_dist"])) * 0.55 + smoothstep(14.0, 26.0, slope) * 0.35 + f["high"] * 0.22 + f["road_weight"] * 0.10, 0, 1)
    small_d = np.where(f["h"] < -1.2, 0.0, small_d) * (1.0 - excl * 0.6)
    sx, sy = scatter(rng, small_d, 3.0, 1.6)
    keys = ["SRock3", "SRock4", "SRock5", "SRock6", "SRock7", "SRock8"]
    for x, y in zip(sx, sy):
        L.add_ism(keys[int(rng.integers(0, 6))], x, y, rng.uniform(0, 360), rng.uniform(0.6, 1.6), roll=rng.uniform(-10, 10), sink=0.04)
    return len(bx), len(sx)


def place_ground_decals(L, rng, f, excl):
    puddle_d = np.clip(f["marsh"] * 0.45 + f["road_weight"] * 0.10 + (1.0 - smoothstep(8.0, 18.0, f["river_dist"])) * 0.25, 0, 1)
    puddle_d = np.where(f["h"] < -1.0, 0.0, puddle_d) * (1.0 - smoothstep(6.0, 12.0, f["slope"]))
    px, py = scatter(rng, puddle_d, 9.0, 9.0)
    for i, (x, y) in enumerate(zip(px, py)):
        s = rng.uniform(6.0, 20.0)
        L.add_plane("Puddles", x, y, L.height(x, y) + 0.03, rng.uniform(0, 360), s, s * rng.uniform(0.7, 1.0), f"TDGen_Puddle_{i}", mesh=M["PlaneDecal"], sz=40.0)
    fog_d = np.clip(f["marsh"] * 0.5 + f["grove"] * 0.5, 0, 1)
    fx, fy = scatter(rng, fog_d, 26.0, 24.0)
    for i, (x, y) in enumerate(zip(fx, fy)):
        s = rng.uniform(22.0, 42.0)
        L.add_plane("Fog1" if rng.random() < 0.5 else "Fog2", x, y, L.height(x, y) + rng.uniform(0.6, 1.4), rng.uniform(0, 360), s, s, f"TDGen_FogPlane_{i}", sz=s)
    return len(px), len(fx)


def place_water(L, f):
    poly = f["river_poly"]
    step = 30
    idx = list(range(0, len(poly) - 1, step)) + [len(poly) - 1]
    for i in range(len(idx) - 1):
        a, b = poly[idx[i]], poly[idx[i + 1]]
        mid = (a + b) * 0.5
        d = b - a
        length = float(np.hypot(d[0], d[1]))
        yaw = math.degrees(math.atan2(d[1], d[0]))
        L.add_plane("Water", float(mid[0]), float(mid[1]), -1.75, yaw, (length + 6.0) / 100.0 * 1.0 + 0.02, 0.40, f"TDGen_River_{i}")
    L.add_plane("Water", LAKE[0], LAKE[1], -1.75, 0.0, 1.75, 1.75, "TDGen_Lake")


def place_village(L, rng):
    cx, cy = VILLAGE
    yaw0 = 0.0

    def W(lx, ly):
        return local_to_world(cx, cy, yaw0, lx, ly)

    def A(key, lx, ly, yaw, scale=1.0, **kw):
        x, y = W(lx, ly)
        return L.add_actor(key, x, y, yaw + yaw0, scale, **kw)

    # 광장: 큰 가마솥 화덕 + 바위 원형 + 촛불
    A("Cauldron", 0, 0, 20, 1.6, z_off=0.35)
    for i in range(9):
        a = i * 40.0 + rng.uniform(-8, 8)
        r = rng.uniform(1.7, 2.2)
        rx, ry = r * math.cos(math.radians(a)), r * math.sin(math.radians(a))
        A(["SRock4", "SRock6", "SRock3", "SRock7"][i % 4], rx, ry, a + rng.uniform(-30, 30), rng.uniform(0.8, 1.15), roll=rng.uniform(-10, 10))
    for i in range(6):
        a = i * 60.0 + 20
        A("RocksTile", 1.1 * math.cos(math.radians(a)), 1.1 * math.sin(math.radians(a)), a, 0.6, z_off=0.03)
    A("CandlesGroup", 2.6, 0.4, 0, 1.3)
    A("CandlesGroup", -2.2, 1.4, 90, 1.1)
    L.add_light("Point", cx, cy, 2.2, 260.0, [1.0, 0.62, 0.30], 24.0, "TDGen_Light_PlazaFire", source_radius=60.0, volumetric=6.0)
    L.add_niagara("Dust1", cx + 3.0, cy - 2.0, 1.2, "TDGen_FX_PlazaDust")
    for i in range(4):
        a = 45 + i * 90
        lx, ly = 11.5 * math.cos(math.radians(a)), 11.5 * math.sin(math.radians(a))
        A("Lamppost", lx, ly, a + 180)
        wx, wy = W(lx, ly)
        L.add_light("Point", wx - 0.3 * math.cos(math.radians(a)), wy - 0.3 * math.sin(math.radians(a)), 3.4, 55.0, [1.0, 0.72, 0.42], 11.0, f"TDGen_Light_PlazaLamp_{i}", source_radius=12.0, volumetric=2.5)
    for i in range(6):
        a = 15 + i * 60
        A("Bench", 8.0 * math.cos(math.radians(a)), 8.0 * math.sin(math.radians(a)), a + 90)

    # 오두막(쉼터) 5채: 바닥 판 + 구조물 + 살림살이
    huts = [(-34, -22, 35), (-30, 26, -35), (28, -34, 145), (36, 22, -145), (4, -44, 95), (-56, 4, 85), (12, 60, -95), (52, -42, 210)]
    for i, (hx, hy, hyaw) in enumerate(huts):
        for fx in (-1.5, 0, 1.5):
            for fy in (-1.5, 0, 1.5):
                px, py = local_to_world(hx, hy, hyaw, fx, fy)
                A("WC3", px, py, hyaw, 1.0, z_off=0.02)
        A("WC9", hx, hy, hyaw, 1.0, z_off=0.16)
        px, py = local_to_world(hx, hy, hyaw, 3.2, -1.6)
        A("Barrels1", px, py, hyaw + 20)
        px, py = local_to_world(hx, hy, hyaw, 3.0, 1.7)
        A("Crate", px, py, hyaw + 70)
        px, py = local_to_world(hx, hy, hyaw, 3.1, 1.2)
        A("Crate2", px, py, hyaw + 30, z_off=0.42)
        px, py = local_to_world(hx, hy, hyaw, -3.1, 1.4)
        A("Stool", px, py, hyaw + 200)
        px, py = local_to_world(hx, hy, hyaw, -3.3, -1.5)
        A("Bucket", px, py, hyaw)
        px, py = local_to_world(hx, hy, hyaw, 0.0, 2.9)
        A("Lamp", px, py, hyaw, z_off=2.1)
        wx, wy = W(px, py)
        L.add_light("Point", wx, wy, 2.2, 22.0, [1.0, 0.7, 0.4], 8.0, f"TDGen_Light_Hut_{i}", source_radius=6.0, volumetric=1.5)
        px, py = local_to_world(hx, hy, hyaw, -1.0, -3.4)
        A("Beads1", px, py, hyaw + 90, z_off=2.4)
        for wy_ in (-2.0, 0.0, 2.0):
            px, py = local_to_world(hx, hy, hyaw, -4.6, wy_)
            A("WC7", px, py, hyaw + 90, rng.uniform(0.98, 1.05), roll=rng.uniform(-3, 3))
        px, py = local_to_world(hx, hy, hyaw, 5.5, -3.5)
        A("CandlesGroup", px, py, hyaw, 1.3)
        wx, wy = W(px, py)
        L.add_light("Point", wx, wy, 0.6, 14.0, [1.0, 0.6, 0.3], 6.0, f"TDGen_Light_HutCandles_{i}", source_radius=5.0, cast_shadows=False)
        px, py = local_to_world(hx, hy, hyaw, 4.8, 3.6)
        A("Pole", px, py, hyaw)
        A("Beads1", px, py, hyaw + 180, z_off=2.6)
        A("Lamp", px, py, hyaw, z_off=3.0)
        wx, wy = W(px, py)
        L.add_light("Point", wx, wy, 3.1, 20.0, [1.0, 0.7, 0.42], 8.0, f"TDGen_Light_HutPole_{i}", source_radius=6.0, cast_shadows=False)
    # 시장(동쪽 문 쪽)
    market = [(46, 4, 90), (46, 11, 90), (52, -4, 0)]
    for i, (mx, my, myaw) in enumerate(market):
        A("Table", mx, my, myaw)
        A(["Pumpkin", "Cabbage", "Bread"][i], mx + 0.2, my - 0.2, myaw + 30, 1.0, z_off=0.92)
        A(["Cabbage", "Apple", "Bottle"][i], mx - 0.35, my + 0.25, myaw + 80, 1.0, z_off=0.92)
        A("Crate3", mx + 1.4, my + 0.4, myaw + 15)
        A("Crate4", mx - 1.4, my - 0.5, myaw - 30)
    A("FishTable", 55, 10, 180)
    A("Shelving", 55, 14, 180)
    A("Cart", 40, 18, 120)
    A("Wheel", 43, 21, 0, roll=80)
    A("Bag", 41, 15, 40)
    A("Bag", 42, 15.6, 100)
    A("Pole", 50, -8, 0)
    A("Pole", 50, 16, 0)
    A("Lamp", 50, -8, 0, z_off=3.0)
    A("Lamp", 50, 16, 0, z_off=3.0)
    wx, wy = W(50, -8)
    L.add_light("Point", wx, wy, 3.1, 30.0, [1.0, 0.7, 0.42], 9.0, "TDGen_Light_Market_0", source_radius=8.0)
    wx, wy = W(50, 16)
    L.add_light("Point", wx, wy, 3.1, 30.0, [1.0, 0.7, 0.42], 9.0, "TDGen_Light_Market_1", source_radius=8.0)
    # 대장간(북쪽)
    A("Anvil", -10, -40, 0)
    A("Hammer", -9.7, -40.3, 60, z_off=0.5)
    A("Armory", -14, -42, 90)
    A("Trough", -6, -43, 10)
    A("Barrel2", -12, -37, 0)
    A("Barrel1", -11.2, -36.6, 45, z_off=0.0)
    A("WC7", -8, -46, 0)
    A("WC7", -10, -46, 0)
    A("WC7", -12, -46, 0)
    A("Cauldron", -7, -38.5, 0, 1.0)
    L.add_light("Point", *W(-7, -38.5), 1.4, 90.0, [1.0, 0.5, 0.2], 12.0, "TDGen_Light_Forge", source_radius=25.0, volumetric=3.0)
    A("Shield", -14.6, -42, 90, z_off=0.9)
    A("Sword", -13.4, -42, 90, z_off=0.9, roll=90)
    # 마구간/창고(남쪽)
    A("Cart", -20, 40, 200)
    A("Barrels1", -24, 42, 0)
    A("Barrel1", -26, 40, 30)
    A("Pitchfork", -23, 38, 0, roll=70, z_off=0.3)
    A("Shovel", -22, 38.5, 20, roll=70, z_off=0.3)
    A("Trough", -18, 44, 90)
    A("Ladder", -26, 46, 90, roll=-15)
    A("Steps", 8, 36, 180)
    for i in range(6):
        px, py = -30 + i * 2.3, 48
        A("Fence1", px, py, 90)
    # 우물 대용: 돌 우물(바위 타일 + 통)
    A("RocksTile", 16, -12, 0, 1.0, z_off=0.05)
    A("Bucket", 16.4, -12.2, 30, z_off=0.45)
    A("Trough", 18.5, -12, 90, z_off=0.0)
    # 울타리 링 + 4개 문
    gates = {0: 12.0, 90: 12.0, 180: 12.0, 270: 12.0}
    ring_r = VILLAGE_R - 12.0
    n_seg = int(2 * math.pi * ring_r / 2.32)
    for i in range(n_seg):
        a = i * 360.0 / n_seg
        skip = False
        for ga, gw in gates.items():
            da = (a - ga + 180) % 360 - 180
            if abs(da) < math.degrees(gw / ring_r):
                skip = True
        if skip:
            continue
        lx, ly = ring_r * math.cos(math.radians(a)), ring_r * math.sin(math.radians(a))
        key = "Fence1" if rng.random() < 0.85 else "Fence2"
        A(key, lx, ly, a + 90 + rng.uniform(-3, 3), rng.uniform(0.95, 1.05), z_off=-0.05)
    for ga in gates:
        for side in (-1, 1):
            a = ga + side * math.degrees(12.0 / ring_r)
            lx, ly = ring_r * math.cos(math.radians(a)), ring_r * math.sin(math.radians(a))
            A("Pole", lx, ly, ga)
            A("StickedSkull", lx * 1.03, ly * 1.03, ga + 180, 1.2)
            A("Lamp", lx, ly, ga, z_off=3.0)
            wx, wy = W(lx, ly)
            L.add_light("Point", wx, wy, 3.1, 28.0, [1.0, 0.66, 0.36], 10.0, f"TDGen_Light_Gate_{ga}_{side}", source_radius=8.0, volumetric=2.0)
        lx, ly = (ring_r + 0.2) * math.cos(math.radians(ga)), (ring_r + 0.2) * math.sin(math.radians(ga))
        A("ChainCurved", lx, ly, ga + 90, 1.0, z_off=3.2)
    # 상자·통 군집
    for c in range(9):
        r = rng.uniform(16.0, ring_r - 10.0)
        a = rng.uniform(0, 360)
        gx, gy = r * math.cos(math.radians(a)), r * math.sin(math.radians(a))
        for k in range(int(rng.integers(3, 6))):
            key = ["Crate", "Barrel1", "Barrels1", "Crate2", "Bag", "BarrelEmpty", "Crate4"][int(rng.integers(0, 7))]
            A(key, gx + rng.uniform(-1.4, 1.4), gy + rng.uniform(-1.4, 1.4), rng.uniform(0, 360), rng.uniform(0.9, 1.1))
        if rng.random() < 0.5:
            A("Crate", gx, gy, rng.uniform(0, 360), 1.0, z_off=0.42)
    for (tx, ty) in ((20, -25), (-40, 42), (34, 44), (-22, -58), (58, 20), (-60, -30)):
        L.add_ism("Birch1", *W(tx, ty), rng.uniform(0, 360), rng.uniform(1.1, 1.35), sink=0.2)
    # 잡동사니 흩뿌리기
    for i in range(70):
        r = rng.uniform(14.0, ring_r - 4.0)
        a = rng.uniform(0, 360)
        lx, ly = r * math.cos(math.radians(a)), r * math.sin(math.radians(a))
        key = ["Crate", "Barrel1", "Barrel2", "Bag", "Bucket", "Bottle", "WoodenStick", "WoodenBrick", "Pot", "Cup", "Skull", "Bone", "BarrelEmpty", "Crate2"][int(rng.integers(0, 14))]
        A(key, lx, ly, rng.uniform(0, 360), rng.uniform(0.8, 1.1))
    for i in range(12):
        r = rng.uniform(20.0, ring_r - 6.0)
        a = rng.uniform(0, 360)
        lx, ly = r * math.cos(math.radians(a)), r * math.sin(math.radians(a))
        L.add_ism("Birch3", *W(lx, ly), rng.uniform(0, 360), rng.uniform(0.8, 1.1), sink=0.2)
    L.add_niagara("Leaves", cx, cy, 6.0, "TDGen_FX_VillageLeaves")
    L.add_niagara("Dust2", cx + 40, cy + 5, 1.0, "TDGen_FX_MarketDust")
    L.add_marker("PlayerStart", cx + 4.0, cy + 6.0, 0.0, "PlayerStart", z_off=1.0)
    L.add_light("Spot", cx, cy, 34.0, 1200.0, [0.86, 0.90, 1.0], 80.0, "TDGen_Godray_Village", pitch=-62.0, yaw=145.0, cone=34.0, inner=12.0, source_radius=100.0, light_function="LightShaft", volumetric=8.0, temperature=None, cast_shadows=True)
    L.reserve(cx, cy, VILLAGE_R + 6.0)


def place_graveyard(L, rng):
    cx, cy = GRAVEYARD
    rows, cols = 5, 7
    for r in range(rows):
        for c in range(cols):
            lx, ly = -15 + c * 5.0 + rng.uniform(-0.6, 0.6), -12 + r * 6.0 + rng.uniform(-0.6, 0.6)
            x, y = local_to_world(cx, cy, 15.0, lx, ly)
            if rng.random() < 0.8:
                L.add_actor("StickedSkull", x, y, 15 + rng.uniform(-25, 25), rng.uniform(1.1, 1.5), roll=rng.uniform(-8, 8), pitch=rng.uniform(-8, 8), label=f"TDGen_Grave_{r}_{c}")
            if rng.random() < 0.45:
                L.add_ism("Bone", x + rng.uniform(-1, 1), y + rng.uniform(-1, 1), rng.uniform(0, 360), rng.uniform(1.0, 1.6))
            if rng.random() < 0.4:
                L.add_ism("Skull", x + rng.uniform(-1, 1), y + rng.uniform(-1, 1), rng.uniform(0, 360), rng.uniform(1.0, 1.5))
            if rng.random() < 0.3:
                L.add_actor("Candles", x + 0.4, y + 0.3, rng.uniform(0, 360), 1.2, label=f"TDGen_GraveCandle_{r}_{c}")
                L.add_light("Point", x + 0.4, y + 0.3, 0.5, 6.0, [1.0, 0.6, 0.3], 4.5, f"TDGen_Light_GraveCandle_{r}_{c}", source_radius=4.0, cast_shadows=False)
    for i in range(4):
        a = 45 + 90 * i
        x, y = local_to_world(cx, cy, 15.0, 24 * math.cos(math.radians(a)), 22 * math.sin(math.radians(a)))
        L.add_actor("Pole", x, y, a, 1.0, label=f"TDGen_GravePole_{i}")
        L.add_actor("RamSkull", x, y, a + 180, 1.4, z_off=3.3, pitch=-20, label=f"TDGen_GraveRam_{i}")
        L.add_actor("ChainCurved", x, y, a, 1.0, z_off=2.4, label=f"TDGen_GraveChain_{i}")
    ring_r = 32.0
    n = int(2 * math.pi * ring_r / 2.32)
    for i in range(n):
        a = i * 360.0 / n
        if abs(((a - 150 + 180) % 360) - 180) < 12 or abs(((a - 345 + 180) % 360) - 180) < 10:
            continue
        if rng.random() < 0.12:
            continue
        L.add_actor("Fence1", cx + ring_r * math.cos(math.radians(a)), cy + ring_r * math.sin(math.radians(a)), a + 90, rng.uniform(0.9, 1.05), roll=rng.uniform(-12, 12), label=f"TDGen_GraveFence_{i}")
    L.add_actor("WC9", cx + 26, cy - 8, 200, 1.0, z_off=0.15, label="TDGen_GraveChapel")
    L.add_actor("Cauldron", cx + 22, cy - 4, 0, 1.2, label="TDGen_GraveCauldron")
    L.add_actor("CandlesGroup", cx + 21.4, cy - 3.2, 40, 1.4, label="TDGen_GraveCandles")
    L.add_light("Point", cx + 22, cy - 4, 1.6, 60.0, [0.55, 0.85, 1.0], 14.0, "TDGen_Light_GraveGhost", source_radius=30.0, volumetric=5.0)
    for i in range(8):
        a = rng.uniform(0, 360)
        r = rng.uniform(26.0, 34.0)
        L.add_ism("Birch3", cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a)), rng.uniform(0, 360), rng.uniform(0.9, 1.2), sink=0.2)
    L.add_plane("Fog2", cx, cy, L.height(cx, cy) + 0.9, 20.0, 60.0, 55.0, "TDGen_GraveFog", sz=40.0)
    L.add_niagara("Dust2", cx, cy, 1.5, "TDGen_FX_GraveDust")
    L.add_marker("Poi", cx, cy, 0.0, "TDPoi_Graveyard", tags=["TDPoi", "Graveyard"])
    L.reserve(cx, cy, 36.0)


def place_shrine(L, rng):
    cx, cy = POIS["Shrine"]["pos"]
    for i in range(9):
        a = i * 40.0
        x, y = cx + 7.0 * math.cos(math.radians(a)), cy + 7.0 * math.sin(math.radians(a))
        L.add_actor("RocksTile", x, y, a, rng.uniform(0.9, 1.3), z_off=0.02, label=f"TDGen_ShrineTile_{i}")
    for i in range(7):
        a = i * 360.0 / 7 + 10
        x, y = cx + 11.0 * math.cos(math.radians(a)), cy + 11.0 * math.sin(math.radians(a))
        L.add_actor(["Cliff4", "Cliff2"][i % 2], x, y, a + 90, rng.uniform(0.35, 0.55), pitch=rng.uniform(-10, 10), label=f"TDGen_ShrineStone_{i}")
        L.add_actor("Candles", x * 0.999 + 0.9 * math.cos(math.radians(a + 180)), y + 0.9 * math.sin(math.radians(a + 180)), a, 1.3, label=f"TDGen_ShrineCandle_{i}")
        L.add_light("Point", x + 0.9 * math.cos(math.radians(a + 180)), y + 0.9 * math.sin(math.radians(a + 180)), 0.5, 7.0, [1.0, 0.55, 0.25], 5.0, f"TDGen_Light_ShrineCandle_{i}", source_radius=4.0, cast_shadows=False)
    L.add_actor("Cliff3", cx, cy, 30, 0.6, z_off=0.0, label="TDGen_ShrineAltar")
    L.add_actor("RamSkull2", cx, cy, 200, 2.2, z_off=1.4, pitch=15, label="TDGen_ShrineRamSkull")
    L.add_actor("Bowl", cx + 1.0, cy - 0.6, 0, 1.5, z_off=1.4, label="TDGen_ShrineBowl")
    L.add_actor("CandlesGroup", cx - 0.9, cy + 0.7, 0, 1.6, z_off=1.4, label="TDGen_ShrineCandles")
    L.add_light("Point", cx, cy, 2.4, 140.0, [1.0, 0.25, 0.15], 18.0, "TDGen_Light_ShrineRed", source_radius=40.0, volumetric=7.0)
    L.add_light("Spot", cx, cy, 30.0, 900.0, [0.9, 0.92, 1.0], 70.0, "TDGen_Godray_Shrine", pitch=-70.0, yaw=60.0, cone=22.0, inner=8.0, source_radius=60.0, light_function="LightShaft", volumetric=10.0)
    for i in range(14):
        a = rng.uniform(0, 360)
        r = rng.uniform(3.0, 10.0)
        L.add_ism(["Skull", "Bone", "Skull"][i % 3], cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a)), rng.uniform(0, 360), rng.uniform(1.0, 1.6))
    for i in range(6):
        a = 25 + i * 60
        L.add_actor("StickedSkull", cx + 15.0 * math.cos(math.radians(a)), cy + 15.0 * math.sin(math.radians(a)), a + 180, 1.4, label=f"TDGen_ShrineStake_{i}")
    L.add_plane("Fog1", cx, cy, L.height(cx, cy) + 0.7, 0.0, 32.0, 32.0, "TDGen_ShrineFog", sz=30.0)
    L.add_marker("Poi", cx, cy, 0.0, "TDPoi_Shrine", tags=["TDPoi", "Shrine"])
    L.reserve(cx, cy, 20.0)


def place_hunter_camp(L, rng):
    cx, cy = POIS["HunterCamp"]["pos"]
    yaw = 70.0
    for fx in (-1.5, 0, 1.5):
        for fy in (-1.5, 0, 1.5):
            x, y = local_to_world(cx, cy, yaw, fx - 4.0, fy)
            L.add_actor("WC3", x, y, yaw, 1.0, z_off=0.02, label="TDGen_CampFloor")
    x, y = local_to_world(cx, cy, yaw, -4.0, 0.0)
    L.add_actor("WC9", x, y, yaw, 1.0, z_off=0.16, label="TDGen_CampShelter")
    L.add_actor("Cauldron", cx + 3.0, cy, 0, 1.2, label="TDGen_CampFire")
    for i in range(7):
        a = i * 51.4
        L.add_ism("SRock6", cx + 3.0 + 1.3 * math.cos(math.radians(a)), cy + 1.3 * math.sin(math.radians(a)), a, rng.uniform(1.2, 1.7))
    L.add_light("Point", cx + 3.0, cy, 1.4, 120.0, [1.0, 0.55, 0.25], 16.0, "TDGen_Light_CampFire", source_radius=35.0, volumetric=4.0)
    L.add_niagara("Dust1", cx + 3.0, cy, 1.0, "TDGen_FX_CampSmoke")
    L.add_actor("Armory", cx + 1.0, cy - 6.0, yaw + 90, 1.0, label="TDGen_CampArmory")
    L.add_actor("Bow2", cx + 1.2, cy - 6.4, 0, 1.0, z_off=0.9, label="TDGen_CampBow")
    L.add_actor("FishTable", cx + 7.0, cy + 3.0, yaw + 180, 1.0, label="TDGen_CampFishTable")
    L.add_actor("Cart", cx - 2.0, cy + 8.0, yaw + 40, 1.0, label="TDGen_CampCart")
    L.add_actor("Barrels1", cx + 5.0, cy - 3.0, 10, 1.0, label="TDGen_CampBarrels")
    L.add_actor("Crate", cx + 6.0, cy - 1.5, 40, 1.0, label="TDGen_CampCrate")
    L.add_actor("Stool", cx + 5.2, cy + 1.0, 0, 1.0, label="TDGen_CampStool")
    L.add_actor("Stool", cx + 1.0, cy + 2.4, 60, 1.0, label="TDGen_CampStool2")
    L.add_actor("Bench", cx + 3.0, cy + 3.6, 0, 1.0, label="TDGen_CampBench")
    L.add_actor("Pole", cx + 9.0, cy - 5.0, 0, 1.0, label="TDGen_CampPole")
    L.add_actor("Lamp", cx + 9.0, cy - 5.0, 0, 1.0, z_off=3.0, label="TDGen_CampLamp")
    L.add_light("Point", cx + 9.0, cy - 5.0, 3.1, 25.0, [1.0, 0.7, 0.4], 9.0, "TDGen_Light_CampLamp", source_radius=8.0)
    for i in range(5):
        L.add_actor("StickedSkull", cx + rng.uniform(-12, 12), cy + rng.uniform(-12, 12), rng.uniform(0, 360), 1.2, label=f"TDGen_CampStake_{i}")
    for i in range(5):
        L.add_actor("Fence1", cx - 12.0, cy - 6.0 + i * 2.35, 90, 1.0, label=f"TDGen_CampFence_{i}")
    L.add_marker("Poi", cx, cy, 0.0, "TDPoi_HunterCamp", tags=["TDPoi", "Camp"])
    L.reserve(cx, cy, 22.0)


def place_watchtower(L, rng):
    cx, cy = POIS["Watchtower"]["pos"]
    yaw = -30.0
    for fx in (-1.5, 0, 1.5):
        for fy in (-1.5, 0, 1.5):
            x, y = local_to_world(cx, cy, yaw, fx, fy)
            L.add_actor("WC3", x, y, yaw, 1.0, z_off=0.02, label="TDGen_TowerFloor")
    for i in range(4):
        a = 45 + i * 90
        x, y = local_to_world(cx, cy, yaw, 2.6 * math.cos(math.radians(a)), 2.6 * math.sin(math.radians(a)))
        L.add_actor("Pole", x, y, yaw, 1.35, label=f"TDGen_TowerPole_{i}")
    for fx in (-1.5, 0, 1.5):
        for fy in (-1.5, 0, 1.5):
            x, y = local_to_world(cx, cy, yaw, fx, fy)
            L.add_actor("FloorSeg", x, y, yaw, 1.0, z_off=4.4, label="TDGen_TowerDeck")
    for i in range(3):
        x, y = local_to_world(cx, cy, yaw, -2.7 + i * 2.0, 3.1)
        L.add_actor("WC7", x, y, yaw, 1.0, z_off=4.5, label="TDGen_TowerRail")
    x, y = local_to_world(cx, cy, yaw, 3.3, 0.0)
    L.add_actor("Ladder", x, y, yaw + 90, 1.0, roll=-12, label="TDGen_TowerLadder")
    x, y = local_to_world(cx, cy, yaw, 3.3, 0.0)
    L.add_actor("Ladder", x, y, yaw + 90, 1.0, roll=-12, z_off=2.2, label="TDGen_TowerLadder2")
    L.add_actor("Lamp", cx, cy, yaw, 1.0, z_off=7.0, label="TDGen_TowerLamp")
    L.add_light("Point", cx, cy, 7.2, 70.0, [1.0, 0.72, 0.42], 18.0, "TDGen_Light_Tower", source_radius=10.0, volumetric=3.0)
    L.add_actor("Barrel2", cx + 4.0, cy + 3.0, 0, 1.0, label="TDGen_TowerBarrel")
    L.add_actor("Crate", cx + 4.6, cy + 1.6, 20, 1.0, label="TDGen_TowerCrate")
    L.add_actor("Armory", cx - 5.0, cy + 2.0, yaw, 1.0, label="TDGen_TowerArmory")
    for i in range(10):
        key = ["Part1", "Part2", "Part3", "Part7", "Part8", "WoodenBrick"][i % 6]
        L.add_actor(key, cx + rng.uniform(-8, 8), cy + rng.uniform(-8, 8), rng.uniform(0, 360), 1.0, roll=rng.uniform(0, 30), label=f"TDGen_TowerDebris_{i}")
    L.add_marker("Poi", cx, cy, 0.0, "TDPoi_Watchtower", tags=["TDPoi", "Ruin"])
    L.reserve(cx, cy, 14.0)


def place_fishing_dock(L, rng, f):
    cx, cy = POIS["FishingDock"]["pos"]
    # 강둑에서 강 쪽(+X)으로 널판 부두를 낸다
    yaw = 0.0
    bank_z = L.height(cx, cy)
    for i in range(6):
        x = cx + i * 2.0
        L.add_actor("Path", x, cy, yaw, 1.0, z=max(bank_z, -1.2) + 0.15, label=f"TDGen_DockPlank_{i}")
        if i % 2 == 0:
            L.add_actor("Pole2", x, cy - 1.4, 0, 1.0, z=-1.7, z_off=0.0, label=f"TDGen_DockPost_{i}a")
            L.add_actor("Pole2", x, cy + 1.4, 0, 1.0, z=-1.7, label=f"TDGen_DockPost_{i}b")
    L.add_actor("Lamp", cx + 10.0, cy - 1.4, 0, 1.0, z=-1.7, z_off=2.3, label="TDGen_DockLamp")
    L.add_light("Point", cx + 10.0, cy - 1.4, 0.9, 30.0, [1.0, 0.72, 0.42], 9.0, "TDGen_Light_Dock", z=0.0, source_radius=8.0)
    L.add_actor("FishTable", cx - 3.0, cy + 3.0, 90, 1.0, label="TDGen_DockFishTable")
    L.add_actor("Barrel1", cx - 2.0, cy - 3.0, 0, 1.0, label="TDGen_DockBarrel")
    L.add_actor("Bucket", cx - 1.0, cy - 2.4, 40, 1.0, label="TDGen_DockBucket")
    L.add_actor("Fish", cx - 2.6, cy + 3.2, 0, 1.0, z_off=1.05, label="TDGen_DockFish")
    L.add_actor("Crate", cx - 4.5, cy - 1.0, 70, 1.0, label="TDGen_DockCrate")
    L.add_actor("Stool", cx - 1.5, cy + 0.8, 0, 1.0, label="TDGen_DockStool")
    L.add_actor("Beads1", cx - 3.0, cy + 3.0, 90, 1.0, z_off=2.3, label="TDGen_DockBeads")
    L.add_marker("Poi", cx, cy, 0.0, "TDPoi_FishingDock", tags=["TDPoi", "Dock"])
    L.reserve(cx - 2.0, cy, 9.0)


def place_mushroom_grove(L, rng):
    cx, cy = POIS["MushroomGrove"]["pos"]
    for i in range(18):
        a = rng.uniform(0, 360)
        r = rng.uniform(2.0, 26.0)
        x, y = cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a))
        L.add_actor("Mycelium", x, y, rng.uniform(0, 360), rng.uniform(2.0, 4.5), label=f"TDGen_GroveMycelium_{i}")
    for i in range(10):
        a = rng.uniform(0, 360)
        r = rng.uniform(3.0, 24.0)
        x, y = cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a))
        key = "Shroom2" if rng.random() < 0.5 else "Shroom1"
        L.add_actor(key, x, y, rng.uniform(0, 360), rng.uniform(1.4, 2.6), label=f"TDGen_GroveShroom_{i}")
        L.add_light("Point", x, y, 1.6, 12.0, [0.55, 1.0, 0.75], 7.0, f"TDGen_Light_Shroom_{i}", source_radius=20.0, volumetric=3.0, cast_shadows=False)
    for i in range(6):
        a = rng.uniform(0, 360)
        r = rng.uniform(4.0, 20.0)
        s = rng.uniform(7.0, 12.0)
        L.add_ism("LightBeam", cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a)), rng.uniform(0, 360), s, pitch=rng.uniform(-10, 10), z_off=s * 0.62 + 2.0)
    L.add_light("Spot", cx, cy, 32.0, 700.0, [0.75, 1.0, 0.85], 70.0, "TDGen_Godray_Grove", pitch=-72.0, yaw=200.0, cone=26.0, inner=10.0, source_radius=60.0, light_function="LightShaft", volumetric=12.0)
    L.add_plane("Fog1", cx, cy, L.height(cx, cy) + 0.8, 0.0, 48.0, 48.0, "TDGen_GroveFog", sz=40.0)
    L.add_niagara("Leaves", cx, cy, 7.0, "TDGen_FX_GroveLeaves")
    L.add_marker("Poi", cx, cy, 0.0, "TDPoi_MushroomGrove", tags=["TDPoi", "Grove"])


def place_ambush_cart(L, rng):
    cx, cy = POIS["AmbushCart"]["pos"]
    L.add_actor("Cart", cx, cy, 130, 1.0, roll=75, z_off=0.9, label="TDGen_AmbushCart")
    L.add_actor("Wheel", cx + 2.5, cy + 1.0, 40, 1.0, roll=85, label="TDGen_AmbushWheel")
    L.add_actor("Wheel", cx - 3.0, cy - 2.0, 0, 1.0, roll=10, label="TDGen_AmbushWheel2")
    for i in range(6):
        key = ["Barrel1", "Crate", "Bag", "Crate2", "Pumpkin", "Barrel2"][i]
        L.add_actor(key, cx + rng.uniform(-4, 4), cy + rng.uniform(-4, 4), rng.uniform(0, 360), 1.0, roll=rng.uniform(0, 90), z_off=0.15, label=f"TDGen_AmbushLoot_{i}")
    for i in range(6):
        key = ["Part1", "Part4", "Part5", "Part2", "WoodenStick", "Part6"][i]
        L.add_actor(key, cx + rng.uniform(-5, 5), cy + rng.uniform(-5, 5), rng.uniform(0, 360), 1.0, roll=rng.uniform(0, 20), label=f"TDGen_AmbushDebris_{i}")
    for i in range(4):
        L.add_ism(["Skull", "Bone"][i % 2], cx + rng.uniform(-3, 3), cy + rng.uniform(-3, 3), rng.uniform(0, 360), 1.3)
    L.add_actor("Sword", cx + 1.0, cy - 1.5, 30, 1.0, roll=80, label="TDGen_AmbushSword")
    L.add_actor("WAxe", cx - 1.5, cy + 1.2, 200, 1.0, roll=70, label="TDGen_AmbushAxe")
    L.add_marker("Poi", cx, cy, 0.0, "TDPoi_AmbushCart", tags=["TDPoi", "Event"])
    L.reserve(cx, cy, 10.0)


def place_bone_pit(L, rng):
    cx, cy = POIS["BonePit"]["pos"]
    for i in range(12):
        a = i * 30.0
        L.add_actor("RocksTile", cx + 6.0 * math.cos(math.radians(a)), cy + 6.0 * math.sin(math.radians(a)), a, rng.uniform(0.9, 1.2), z_off=0.02, label=f"TDGen_PitTile_{i}")
    for i in range(40):
        a = rng.uniform(0, 360)
        r = rng.uniform(0.5, 9.0)
        L.add_ism(["Bone", "Skull", "Bone", "RamSkull"][i % 4], cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a)), rng.uniform(0, 360), rng.uniform(1.0, 1.8), roll=rng.uniform(-20, 20))
    for i in range(8):
        a = i * 45.0 + 10
        L.add_actor("StickedSkull", cx + 10.5 * math.cos(math.radians(a)), cy + 10.5 * math.sin(math.radians(a)), a + 180, 1.5, pitch=rng.uniform(-6, 6), label=f"TDGen_PitStake_{i}")
        L.add_actor("Candles", cx + 10.5 * math.cos(math.radians(a)) + 0.5, cy + 10.5 * math.sin(math.radians(a)), 0, 1.3, label=f"TDGen_PitCandle_{i}")
        L.add_light("Point", cx + 10.5 * math.cos(math.radians(a)) + 0.5, cy + 10.5 * math.sin(math.radians(a)), 0.5, 6.0, [1.0, 0.5, 0.25], 4.5, f"TDGen_Light_PitCandle_{i}", source_radius=4.0, cast_shadows=False)
    L.add_actor("Cauldron", cx, cy, 0, 1.8, label="TDGen_PitCauldron")
    L.add_actor("RamSkull2", cx, cy, 90, 2.0, z_off=1.0, pitch=20, label="TDGen_PitRam")
    L.add_light("Point", cx, cy, 2.0, 160.0, [0.9, 0.2, 0.1], 20.0, "TDGen_Light_PitRed", source_radius=50.0, volumetric=8.0)
    for i in range(6):
        L.add_niagara("Worms", cx + rng.uniform(-6, 6), cy + rng.uniform(-6, 6), 0.1, f"TDGen_FX_PitWorms_{i}")
    L.add_plane("Fog2", cx, cy, L.height(cx, cy) + 0.7, 30.0, 40.0, 40.0, "TDGen_PitFog", sz=30.0)
    L.add_marker("Poi", cx, cy, 0.0, "TDPoi_BonePit", tags=["TDPoi", "Ritual"])
    L.reserve(cx, cy, 14.0)


def place_wayshrine(L, rng, name):
    cx, cy = POIS[name]["pos"]
    L.add_actor("RocksTile", cx, cy, rng.uniform(0, 360), 0.9, z_off=0.02, label=f"TDGen_{name}_Tile")
    L.add_actor("Cliffs1", cx, cy, rng.uniform(0, 360), 0.45, z_off=-0.1, label=f"TDGen_{name}_Stone")
    L.add_actor("RamSkull", cx, cy, rng.uniform(0, 360), 1.3, z_off=0.8, pitch=-10, label=f"TDGen_{name}_Skull")
    L.add_actor("CandlesGroup", cx + 0.7, cy + 0.4, rng.uniform(0, 360), 1.2, z_off=0.05, label=f"TDGen_{name}_Candles")
    L.add_light("Point", cx + 0.7, cy + 0.4, 0.6, 12.0, [1.0, 0.6, 0.3], 6.0, f"TDGen_Light_{name}", source_radius=5.0, cast_shadows=False)
    for i in range(3):
        a = i * 120.0 + rng.uniform(-20, 20)
        L.add_actor("StickedSkull", cx + 2.6 * math.cos(math.radians(a)), cy + 2.6 * math.sin(math.radians(a)), a + 180, 1.3, label=f"TDGen_{name}_Stake_{i}")
    for i in range(5):
        L.add_ism(["Skull", "Bone", "SRock4"][i % 3], cx + rng.uniform(-3, 3), cy + rng.uniform(-3, 3), rng.uniform(0, 360), rng.uniform(1.0, 1.4))
    L.add_marker("Poi", cx, cy, 0.0, f"TDPoi_{name}", tags=["TDPoi", "Wayshrine"])
    L.reserve(cx, cy, 6.0)


def place_arena(L, rng, name):
    cx, cy = POIS[name]["pos"]
    r = POIS[name]["r"]
    n = int(2 * math.pi * r / 6.0)
    for i in range(n):
        a = i * 360.0 / n + rng.uniform(-6, 6)
        if rng.random() < 0.25:
            continue
        rr = r + rng.uniform(-1.5, 1.5)
        key = ["Cliffs1", "Cliff3", "Rock1", "Rock2", "Cliff2"][int(rng.integers(0, 5))]
        s = rng.uniform(0.45, 0.8)
        L.add_actor(key, cx + rr * math.cos(math.radians(a)), cy + rr * math.sin(math.radians(a)), a + 90 + rng.uniform(-20, 20), s, roll=rng.uniform(-10, 10), pitch=rng.uniform(-10, 10), z_off=-0.2 * s, label=f"TDGen_{name}_Rock_{i}")
    for i in range(10):
        a = rng.uniform(0, 360)
        rr = rng.uniform(1.0, r * 0.7)
        L.add_ism(["Skull", "Bone", "SRock4", "SRock3"][i % 4], cx + rr * math.cos(math.radians(a)), cy + rr * math.sin(math.radians(a)), rng.uniform(0, 360), rng.uniform(1.0, 1.5))
    for i in range(3):
        a = rng.uniform(0, 360)
        L.add_actor("StickedSkull", cx + (r * 0.85) * math.cos(math.radians(a)), cy + (r * 0.85) * math.sin(math.radians(a)), a + 180, 1.3, label=f"TDGen_{name}_Stake_{i}")
    L.add_marker("Arena", cx, cy, 0.0, f"TDArena_{name}", tags=["TDArena", "Arena"])
    L.reserve(cx, cy, r + 2.0)


def place_dungeon_entrance(L, rng, name, pos, facing, kind):
    cx, cy = pos
    yaw = facing
    # 동굴 입구: 절벽 기둥 2 + 상인방 + 어두운 안쪽 상자 + 해골 장식 + 붉은 빛
    for side in (-1, 1):
        x, y = local_to_world(cx, cy, yaw, 0.0, side * 4.2)
        L.add_actor("Cliff4", x, y, yaw + 90 + side * 15, 1.25, roll=side * 8, z_off=-0.6, label=f"TDGen_{name}_Pillar_{side}")
    x, y = local_to_world(cx, cy, yaw, -1.0, 0.0)
    L.add_actor("Cliff2", x, y, yaw, 1.6, z_off=4.6, pitch=8, label=f"TDGen_{name}_Lintel")
    x, y = local_to_world(cx, cy, yaw, -3.5, 0.0)
    L.add_actor("Cliff3", x, y, yaw + 180, 1.8, z_off=7.2, label=f"TDGen_{name}_Cap")
    x, y = local_to_world(cx, cy, yaw, -8.0, 0.0)
    L.add_actor("Cube", x, y, yaw, 1.0, z_off=2.6, scale3=[14.0, 7.5, 5.4], mesh_path=ENGINE_CUBE, materials=[MATS["Bedrock"]], label=f"TDGen_{name}_Tunnel")
    x, y = local_to_world(cx, cy, yaw, -1.2, 0.0)
    L.add_actor("Cube", x, y, yaw, 1.0, z_off=2.4, scale3=[0.2, 5.6, 4.6], mesh_path=ENGINE_CUBE, materials=["/Engine/EngineMaterials/BlackUnlitMaterial"], label=f"TDGen_{name}_Darkness")
    for side in (-1, 1):
        x, y = local_to_world(cx, cy, yaw, 2.2, side * 3.4)
        L.add_actor("Pole", x, y, yaw, 1.1, label=f"TDGen_{name}_Post_{side}")
        L.add_actor("Lamp", x, y, yaw, 1.0, z_off=3.2, label=f"TDGen_{name}_Lamp_{side}")
        L.add_light("Point", x, y, 3.3, 45.0, [1.0, 0.62, 0.32], 11.0, f"TDGen_Light_{name}_{side}", source_radius=8.0, volumetric=3.0)
        x, y = local_to_world(cx, cy, yaw, 4.5, side * 5.0)
        L.add_actor("StickedSkull", x, y, yaw + 180, 1.5, label=f"TDGen_{name}_Stake_{side}")
    x, y = local_to_world(cx, cy, yaw, 2.2, 0.0)
    L.add_actor("ChainCurved", x, y, yaw + 90, 1.0, z_off=3.4, label=f"TDGen_{name}_Chain")
    L.add_actor("RamSkull", *local_to_world(cx, cy, yaw, 0.2, 0.0), yaw, 2.0, z_off=6.0, pitch=-15, label=f"TDGen_{name}_Ram")
    for i in range(10):
        lx, ly = rng.uniform(1.0, 8.0), rng.uniform(-5.0, 5.0)
        x, y = local_to_world(cx, cy, yaw, lx, ly)
        L.add_ism(["Skull", "Bone", "SRock5", "SRock7"][i % 4], x, y, rng.uniform(0, 360), rng.uniform(1.0, 1.6))
    x, y = local_to_world(cx, cy, yaw, -2.0, 0.0)
    L.add_light("Point", x, y, 1.5, 120.0, [1.0, 0.3, 0.12], 16.0, f"TDGen_Light_{name}_Inner", source_radius=40.0, volumetric=10.0)
    L.add_plane("Fog2", *local_to_world(cx, cy, yaw, 3.0, 0.0), L.height(cx, cy) + 0.6, yaw, 16.0, 14.0, f"TDGen_{name}_Fog", sz=16.0)
    L.add_niagara("Dust2", *local_to_world(cx, cy, yaw, 1.0, 0.0), 1.2, f"TDGen_FX_{name}_Dust")
    L.add_marker("DungeonEntrance", *local_to_world(cx, cy, yaw, 3.0, 0.0), yaw, f"TDDungeonEntrance_{name}", tags=["TDDungeonEntrance", kind])
    L.reserve(cx, cy, 14.0)


def place_bridge_and_boardwalk(L, rng, f):
    bx, by = BRIDGE
    poly = f["river_poly"]
    d = np.hypot(poly[:, 0] - bx, poly[:, 1] - by)
    i = int(np.argmin(d))
    t = poly[min(i + 4, len(poly) - 1)] - poly[max(i - 4, 0)]
    river_yaw = math.degrees(math.atan2(t[1], t[0]))
    yaw = river_yaw + 90.0
    deck_z = max(L.height(bx + 16 * math.cos(math.radians(yaw)), by + 16 * math.sin(math.radians(yaw))), L.height(bx - 16 * math.cos(math.radians(yaw)), by - 16 * math.sin(math.radians(yaw)))) + 0.25
    n = 17
    for k in range(n):
        lx = (k - (n - 1) / 2) * 2.0
        x, y = local_to_world(bx, by, yaw, lx, 0.0)
        L.add_actor("Path", x, y, yaw, 1.0, z=deck_z, label=f"TDGen_Bridge_Plank_{k}")
        if k % 2 == 0:
            for side in (-1, 1):
                px, py = local_to_world(bx, by, yaw, lx, side * 1.35)
                ground = min(L.height(px, py), deck_z - 0.3)
                L.add_actor("Pole2", px, py, yaw, 1.0, z=ground - 0.3, label=f"TDGen_Bridge_Post_{k}_{side}")
                if k % 4 == 0:
                    L.add_actor("Rope", px, py, yaw, 1.0, z=deck_z + 0.9, mesh_path=f"{MESH_ROOT}/Ropes/SM_Rope", label=f"TDGen_Bridge_Rope_{k}_{side}")
    for side in (-1, 1):
        x, y = local_to_world(bx, by, yaw, side * (n + 1), 0.0)
        L.add_actor("Lamppost", x, y - 2.0, yaw + 90, 1.0, label=f"TDGen_Bridge_Lamppost_{side}")
        L.add_light("Point", x, y - 2.0, 3.4, 50.0, [1.0, 0.7, 0.4], 12.0, f"TDGen_Light_Bridge_{side}", source_radius=12.0, volumetric=2.5)
    # 습지 널길: South 도로의 습지 구간(0 < x < 260)에 널판을 깐다
    road = f["road_polys"]["South"]
    marsh = f["marsh"]
    k = 0
    for j in range(0, len(road) - 1, 4):
        x, y = road[j]
        mv = float(sample_bilinear(marsh, np.array([x]), np.array([y]))[0])
        if mv < 0.55:
            continue
        t = road[min(j + 4, len(road) - 1)] - road[max(j - 4, 0)]
        L.add_actor("Path", float(x), float(y), math.degrees(math.atan2(t[1], t[0])) + rng.uniform(-4, 4), 1.0, z_off=0.06, label=f"TDGen_Boardwalk_{k}")
        if k % 5 == 0:
            n_ = np.array([-t[1], t[0]])
            n_ = n_ / (np.linalg.norm(n_) + 1e-6)
            L.add_actor("Pole2", float(x + n_[0] * 1.5), float(y + n_[1] * 1.5), 0.0, 1.0, z_off=-0.6, label=f"TDGen_Boardwalk_Post_{k}")
            if k % 15 == 0:
                L.add_actor("Lamp", float(x + n_[0] * 1.5), float(y + n_[1] * 1.5), 0.0, 1.0, z_off=0.7, label=f"TDGen_Boardwalk_Lamp_{k}")
                L.add_light("Point", float(x + n_[0] * 1.5), float(y + n_[1] * 1.5), 0.9, 18.0, [1.0, 0.7, 0.4], 8.0, f"TDGen_Light_Boardwalk_{k}", source_radius=6.0, cast_shadows=False)
        k += 1


def place_roadside(L, rng, f):
    for name, poly in f["road_polys"].items():
        if name in ("MainEast", "North", "South", "West"):
            for j in range(0, len(poly), 90):
                x, y = poly[j]
                if math.hypot(x - VILLAGE[0], y - VILLAGE[1]) < VILLAGE_R + 6.0:
                    continue
                if float(sample_bilinear(f["river_dist"], np.array([x]), np.array([y]))[0]) < 14.0:
                    continue
                t = poly[min(j + 3, len(poly) - 1)] - poly[max(j - 3, 0)]
                n_ = np.array([-t[1], t[0]])
                n_ = n_ / (np.linalg.norm(n_) + 1e-6)
                side = 1 if (j // 90) % 2 == 0 else -1
                px, py = float(x + n_[0] * 4.2 * side), float(y + n_[1] * 4.2 * side)
                if L.slope(px, py) > 25.0:
                    continue
                r = rng.random()
                if r < 0.45:
                    L.add_actor("Lamppost", px, py, math.degrees(math.atan2(t[1], t[0])) + (90 if side > 0 else -90), 1.0, label=f"TDGen_RoadLamp_{name}_{j}")
                    L.add_light("Point", px, py, 3.4, 40.0, [1.0, 0.7, 0.4], 11.0, f"TDGen_Light_Road_{name}_{j}", source_radius=12.0, volumetric=2.0)
                elif r < 0.75:
                    L.add_actor("StickedSkull", px, py, rng.uniform(0, 360), 1.3, label=f"TDGen_RoadStake_{name}_{j}")
                else:
                    L.add_actor("Cart" if rng.random() < 0.3 else "Barrel1", px, py, rng.uniform(0, 360), 1.0, label=f"TDGen_RoadProp_{name}_{j}")


def build_grove_mask(f):
    X, Y = f["X"], f["Y"]
    cx, cy = POIS["MushroomGrove"]["pos"]
    return radial(X, Y, cx, cy, 24.0, 60.0)


def write_outputs(f, layers, L, seed):
    os.makedirs(OUT_DIR, exist_ok=True)
    h = f["h"]
    hu = np.clip(np.round(32768.0 + h * UNITS_PER_METER), 0, 65535).astype("<u2")
    hu.tofile(os.path.join(OUT_DIR, "height.r16"))
    layer_files = []
    for name in ("Soil", "Moss", "Mud", "Road", "Rock", "Bedrock"):
        arr = np.clip(np.round(layers[name] * 255.0), 0, 255).astype(np.uint8)
        p = os.path.join(OUT_DIR, f"layer_{name}.r8")
        arr.tofile(p)
        layer_files.append({"name": name, "path": p})
    ism_out = []
    for (key, cx, cy), tr in L.ism.items():
        ism_out.append({"mesh": M[key], "key": key, "cell": [cx, cy], "transforms": tr})
    layout = {
        "seed": seed,
        "landscape": {
            "location": [0.0, 0.0, 0.0], "scale": [100.0, 100.0, Z_SCALE], "components_x": 16, "components_y": 16, "sections_per_component": 1, "quads_per_section": 63,
            "heightmap": os.path.join(OUT_DIR, "height.r16"), "layers": layer_files,
            "material": "/Game/World/Landscape/MI_TD_Landscape", "world_partition_grid_size": 2,
        },
        "ism": ism_out, "actors": L.actors, "planes": L.planes, "lights": L.lights, "niagara": L.niagara, "markers": L.markers,
        "region": {"name": "AshenVale", "size_m": SIZE - 1, "village": VILLAGE, "water_z_cm": -175.0},
    }
    with open(os.path.join(OUT_DIR, "layout.json"), "w", encoding="utf-8") as fp:
        json.dump(layout, fp)
    return layout


def write_preview(f, layers, L):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return
    h = f["h"]
    shade_y, shade_x = np.gradient(h)
    light = np.clip(0.55 + 0.45 * (-(shade_x) * 0.7 - shade_y * 0.7) * 3.0, 0.2, 1.0)
    col = np.zeros((SIZE, SIZE, 3))
    palette = {"Soil": (150, 146, 138), "Moss": (78, 105, 62), "Mud": (95, 80, 62), "Road": (160, 130, 95), "Rock": (120, 118, 116), "Bedrock": (80, 80, 84)}
    for name, c in palette.items():
        for i in range(3):
            col[:, :, i] += layers[name] * c[i]
    col *= light[:, :, None]
    water = h < -1.75
    col[water] = (40, 70, 110)
    img = Image.fromarray(np.clip(col, 0, 255).astype(np.uint8), "RGB")
    dr = ImageDraw.Draw(img)
    for (key, cx, cy), tr in L.ism.items():
        c = {"Birch1": (30, 70, 30), "Birch2": (40, 80, 35), "Birch3": (60, 95, 45)}.get(key)
        if not c:
            continue
        for t in tr:
            x, y = t[0] / 100 + HALF, t[1] / 100 + HALF
            dr.point((x, y), fill=c)
    for a in L.actors:
        x, y = a["loc"][0] / 100 + HALF, a["loc"][1] / 100 + HALF
        dr.point((x, y), fill=(230, 200, 80))
    for m in L.markers:
        x, y = m["loc"][0] / 100 + HALF, m["loc"][1] / 100 + HALF
        dr.ellipse((x - 4, y - 4, x + 4, y + 4), outline=(255, 40, 40))
        dr.text((x + 5, y - 5), m["label"].replace("TDPoi_", "").replace("TDDungeonEntrance_", "D:"), fill=(255, 255, 255))
    img.save(os.path.join(OUT_DIR, "preview.png"))
    hm = Image.fromarray(np.clip((h + 10.0) / 70.0 * 255.0, 0, 255).astype(np.uint8), "L")
    hm.save(os.path.join(OUT_DIR, "preview_height.png"))


def main():
    global OUT_DIR
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--out", default=OUT_DIR)
    args = ap.parse_args()
    OUT_DIR = os.path.abspath(args.out)
    rng = np.random.default_rng(args.seed)
    f = build_terrain(rng)
    f["grove"] = build_grove_mask(f)
    poi_circles = [(p["pos"][0], p["pos"][1], p["r"]) for p in POIS.values()]
    poi_circles += [(MAIN_DUNGEON[0], MAIN_DUNGEON[1], 16.0), (SIDE_DUNGEON_NW[0], SIDE_DUNGEON_NW[1], 16.0), (SIDE_DUNGEON_SE[0], SIDE_DUNGEON_SE[1], 16.0), (BRIDGE[0], BRIDGE[1], 24.0)]
    excl = build_exclusion(f, poi_circles)
    f["forest_density"] = build_forest_density(rng, f, excl)
    layers = build_layers(rng, f)
    L = Layout(f, rng)
    place_village(L, rng)
    place_graveyard(L, rng)
    place_shrine(L, rng)
    place_hunter_camp(L, rng)
    place_watchtower(L, rng)
    place_fishing_dock(L, rng, f)
    place_mushroom_grove(L, rng)
    place_ambush_cart(L, rng)
    place_bone_pit(L, rng)
    for name in ("ArenaNorth", "ArenaEast", "ArenaWest"):
        place_arena(L, rng, name)
    for name in ("WayshrineNorth", "WayshrineEast", "WayshrineSouth"):
        place_wayshrine(L, rng, name)
    place_dungeon_entrance(L, rng, "MainCrypt", MAIN_DUNGEON, MAIN_DUNGEON_FACING, "Main")
    place_dungeon_entrance(L, rng, "HollowCave", SIDE_DUNGEON_NW, SIDE_DUNGEON_NW_FACING, "Side")
    place_dungeon_entrance(L, rng, "SunkenCrypt", SIDE_DUNGEON_SE, SIDE_DUNGEON_SE_FACING, "Side")
    place_bridge_and_boardwalk(L, rng, f)
    place_roadside(L, rng, f)
    n_tree, n_bush, n_dread = place_forest(L, rng, f, excl)
    n_big, n_small = place_rocks(L, rng, f, excl)
    n_pud, n_fog = place_ground_decals(L, rng, f, excl)
    place_water(L, f)
    for i in range(24):
        x, y = rng.uniform(-450, 450), rng.uniform(-450, 450)
        if float(sample_bilinear(f["forest_density"], np.array([x]), np.array([y]))[0]) > 0.45:
            L.add_niagara("Leaves", x, y, 7.0, f"TDGen_FX_Leaves_{i}")
    L.add_marker("NavMeshBounds", 0.0, 0.0, 0.0, "TDGen_NavMeshBounds", z_off=20.0, scale=[520.0, 520.0, 60.0])
    layout = write_outputs(f, layers, L, args.seed)
    write_preview(f, layers, L)
    n_inst = sum(len(v) for v in L.ism.values())
    print(f"height range m: {f['h'].min():.1f} .. {f['h'].max():.1f}; trees {n_tree} bushes {n_bush} dread {n_dread} bigrocks {n_big} smallrocks {n_small} puddles {n_pud} fog {n_fog}")
    print(f"ism buckets {len(L.ism)} instances {n_inst}; actors {len(L.actors)}; planes {len(L.planes)}; lights {len(L.lights)}; niagara {len(L.niagara)}; markers {len(L.markers)}")
    print("out:", OUT_DIR)


if __name__ == "__main__":
    main()
