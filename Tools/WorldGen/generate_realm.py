"""에디터 밖 시스템 파이썬에서 실행: 이계 지역(SunkenStarfield·PaleHeights) 504 m 지형·레이어·배치와 숨은 이음매 통로 B·통로 소품(이계·필드 사본)을 만들고 검증·미리보기까지 한다(P3-13, P3-15 명세 4절).
실행: python Tools/WorldGen/generate_realm.py --realm SunkenStarfield|PaleHeights|all [--seed N] [--no-topdown](탑다운 규칙 끄기, 필드도 같은 옵션으로 만든 출력이어야 함) (PowerShell 또는 Git Bash, numpy·scipy·Pillow 필요, 통로 A가 깎인 Saved/WorldGen/AshenVale 출력이 먼저 있어야 함)
출력: Saved/WorldGen/Realms/<RealmId>/{height.r16, layer_<Name>.r8 x6, layout.json, preview.png, topdown_rules_report.json} (검증 실패 종료 코드 1, 입력 오류 2)
상태: 현행
"""
import argparse
import hashlib
import json
import math
import os
import sys

import numpy as np
from scipy import ndimage

import generate_ashen_vale as gav
import realm_passages as rp
import topdown_rules as tdr

GENERATOR_VERSION = 2
SIZE = 505
HALF = (SIZE - 1) // 2
COMPONENTS = 8
QUADS_PER_SECTION = 63
CELL_M = 126.0
LAYER_NAMES = ("Soil", "Moss", "Mud", "Road", "Rock", "Bedrock")
PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REALMS_DIR = os.path.join(PROJECT_DIR, "Saved", "WorldGen", "Realms")
MAIN_DIR = os.path.join(PROJECT_DIR, "Saved", "WorldGen", "AshenVale")
FIELD_HALF_CM = 50400.0
LOADING_RANGE_CM = 76800.0
GRID_CELL_CM = 25200.0
REALM_HALF_EXTENT_CM = [25200.0, 25200.0, 20000.0]
ATLAS_BAND_CENTER_CM = (345000.0, 300000.0)
ATLAS_BAND_HALF_CM = (45000.0, 25200.0)
GLOW_CYAN = [0.35, 0.85, 1.0]
GLOW_VIOLET = [0.65, 0.45, 1.0]
WARM_GOLD = [1.0, 0.85, 0.6]
SUNKEN_WATER_Z_M = -2.0
SUNKEN_CHANNEL_BED_M = -2.6
GATE_FLAT_RADIUS_M = 14.0
GATE_MAX_SLOPE_DEG = 10.0
ROAD_MAX_SLOPE_DEG = 25.0
RAMP_MAX_SLOPE_DEG = 18.0
WALK_MAX_SLOPE_DEG = 35.0
NAV_HALF_CM = 24800.0
PAWN_HEIGHT_CM = 120.0
PRELOAD_LEAD_CM = 4000.0
SEAM_MIST_DENSITY = 0.6
SEAM_MIST_RADIUS_CM = 1500.0
PASSAGE_CLEAR_MARGIN_M = 2.0
PASSAGE_FLOOR_HALF_CHECK_M = 2.5
SEAM_FLOOD_START_S_M = 1.0
SHAPE_MAX_DIFF_UNITS = 1.0
PROP_MIRROR_TOLERANCE_CM = 0.15
FIELD_FLOOR_TOLERANCE_M = 2.0 / gav.UNITS_PER_METER
OUTER_SLOPE_MIN_DEG = 35.0
FIELD_PASSAGE_LABEL_PREFIX = "TDRealm_Passage_"
PALE_WATER_LEVEL_M = -50.0
CLOUD_LATTICE_ORIGIN_M = -250.0
CLOUD_TILE_M = 100.0
CLOUD_INTERIOR_COUNT = 6
STARFIELD_ABYSS_LAYERS = ((-6.0, "StarWater"), (4.0, "VioletFog"))
STARFIELD_ABYSS_INTERIOR_RANGE = range(1, CLOUD_INTERIOR_COUNT - 1)
CLOUD_RING_INDEX_RANGE = range(-2, 8)

DFT_MATERIALS = "/Game/DarkFantasyTopDown/Materials"
PARENT_ROCKS = f"{DFT_MATERIALS}/Nature/MI_Rocks"
PARENT_CLIFFS = f"{DFT_MATERIALS}/Nature/MI_Cliffs1"
PARENT_EMISSIVE = f"{DFT_MATERIALS}/Shared/MI_EmissiveRed"
PARENT_DREADPLANTS = f"{DFT_MATERIALS}/Props/MI_Dreadplants"
PARENT_WATER = f"{DFT_MATERIALS}/WaterPlane/MI_WaterPlane"
PARENT_FOG1 = f"{DFT_MATERIALS}/VFX/MI_FogPlane_Var1"
PARENT_FOG2 = f"{DFT_MATERIALS}/VFX/MI_FogPlane_Var2"
PARENT_BEAM = f"{DFT_MATERIALS}/VFX/MI_LightBeamVar2"
LANDSCAPE_PARENT = "/Game/World/Landscape/M_TD_Landscape"

SUNKEN_CONFIG = {
    "realm_id": "SunkenStarfield",
    "display_name_ko": "가라앉은 별밭",
    "center_cm": [-302400.0, 302400.0, 0.0],
    "default_seed": 11,
    "fall_recovery_height_cm": -3000.0,
    "transition_fade_color": [0.02, 0.03, 0.08],
    "nav_z_cm": [-1000.0, 2000.0],
    "plaza_pad_radius_m": 16.0,
    "gates": [
        {"gate_id": "SunkenStarfieldSouthwest", "plaza": (168.0, 73.0)},
        {"gate_id": "SunkenStarfieldSoutheast", "plaza": (-185.0, 10.0)},
    ],
    "atmosphere": {
        "sun_rotation": [-70.0, 200.0, 0.0], "sun_intensity_lux": 3.0, "sun_color": [0.55, 0.65, 1.0],
        "sky_light_intensity": 1.3, "sky_light_color": [0.55, 0.6, 1.0], "sky_luminance_factor": [0.45, 0.45, 0.75],
        "rayleigh_scattering": [0.12, 0.10, 0.35], "mie_scattering_scale": 0.006,
        "fog_density": 0.02, "fog_height_falloff": 0.25, "fog_height_cm": -200.0, "fog_inscattering_color": [0.1, 0.12, 0.3],
        "fog_max_opacity": 0.95, "fog_start_distance_cm": 2500.0,
        "second_fog_density": 0.10, "second_fog_height_falloff": 2.0, "second_fog_height_offset_cm": -300.0,
        "volumetric_fog_albedo": [0.5, 0.55, 1.0], "volumetric_fog_emissive": [0.004, 0.005, 0.02], "volumetric_fog_extinction_scale": 1.0,
    },
    "post_process": {
        "priority": 10, "z_cm": [-6000.0, 9000.0],
        "settings": {"color_saturation": [0.85, 0.9, 1.15, 1.0], "color_gain": [0.8, 0.88, 1.15, 1.0], "color_contrast": [1.08, 1.08, 1.08, 1.0], "bloom_intensity": 1.4, "vignette_intensity": 0.5, "auto_exposure_bias": 0.6},
    },
    "landscape_material": {"vectors": {"RealmTint": [0.55, 0.62, 0.90, 1.0]}, "scalars": {"RealmBrightness": 0.8}},
    "materials": {
        "DarkRock": (PARENT_ROCKS, {"NormalTopColor": [0.22, 0.26, 0.42, 1.0], "DiffuseMultiplier": [0.7, 0.75, 0.95, 1.0]}, {"Desaturation": 0.55}),
        "DarkCliff": (PARENT_CLIFFS, {"NormalTopColor": [0.2, 0.22, 0.4, 1.0], "DiffuseMultiplier": [0.6, 0.65, 0.85, 1.0]}, {"Desaturation": 0.6}),
        "GlowCyan": (PARENT_DREADPLANTS, {"AmbientColor": [0.15, 0.85, 1.2, 1.0], "ColorMultiplier": [0.3, 0.5, 0.65, 1.0]}, {}),
        "GlowViolet": (PARENT_DREADPLANTS, {"AmbientColor": [0.7, 0.3, 1.2, 1.0], "ColorMultiplier": [0.45, 0.35, 0.65, 1.0]}, {}),
        "ShardGlow": (PARENT_EMISSIVE, {"Color": [0.05, 0.02, 0.12, 1.0]}, {}),
        "StarWater": (PARENT_WATER, {"ColorGradient1": [0.01, 0.015, 0.05, 1.0], "ColorGradient2": [0.04, 0.035, 0.14, 1.0], "ShoreColor": [0.12, 0.14, 0.32, 1.0]}, {}),
        "VioletFog": (PARENT_FOG2, {"SandColor": [0.55, 0.45, 0.95, 1.0]}, {"FinalOpacityTweak": 0.1}),
        "PaleBeam": (PARENT_BEAM, {"Color": [0.55, 0.75, 1.0, 1.0]}, {}),
    },
    "lake": {"x": 10.0, "y": 30.0, "rx": 70.0, "ry": 50.0, "depth": 4.5},
    "channels": {
        "West": [(-58.0, 42.0), (-82.0, 66.0), (-108.0, 64.0), (-132.0, 86.0), (-160.0, 82.0), (-188.0, 102.0), (-222.0, 96.0), (-262.0, 112.0)],
        "Southeast": [(66.0, 8.0), (92.0, -16.0), (118.0, -8.0), (142.0, -34.0), (172.0, -30.0), (204.0, -54.0), (236.0, -50.0), (262.0, -66.0)],
    },
    "terraces": [
        {"name": "North", "x": -20.0, "y": 160.0, "r": 45.0, "height": 7.0, "ramp_angle": -50.0},
        {"name": "Southeast", "x": 140.0, "y": -120.0, "r": 40.0, "height": 5.0, "ramp_angle": 160.0},
        {"name": "Southwest", "x": -150.0, "y": -140.0, "r": 35.0, "height": 9.0, "ramp_angle": 45.0},
    ],
    "clearings": [
        {"id": "C1", "name": "BonePit", "x": -60.0, "y": -80.0},
        {"id": "C2", "name": "MushroomRing", "x": 90.0, "y": 140.0},
        {"id": "C3", "name": "SouthShore", "x": 60.0, "y": -40.0},
        {"id": "C4", "name": "StarPillarRing", "x": -20.0, "y": 160.0},
    ],
    "landmarks": [
        {"name": "StarPillarRing", "x": -20.0, "y": 160.0},
        {"name": "FallenStarCrater", "x": 140.0, "y": -120.0},
        {"name": "BoneField", "x": -60.0, "y": -110.0},
    ],
    "crater": {"x": 140.0, "y": -120.0, "r": 12.0, "depth": 3.0},
}

PALE_CONFIG = {
    "realm_id": "PaleHeights",
    "display_name_ko": "창백한 고원",
    "center_cm": [-302400.0, -302400.0, 0.0],
    "default_seed": 23,
    "fall_recovery_height_cm": -1500.0,
    "transition_fade_color": [0.92, 0.94, 0.97],
    "nav_z_cm": [-500.0, 2500.0],
    "plaza_pad_radius_m": 15.0,
    "gates": [
        {"gate_id": "PaleHeightsNortheast", "plaza": (-163.0, 39.0)},
    ],
    "atmosphere": {
        "sun_rotation": [-40.0, 120.0, 0.0], "sun_intensity_lux": 12.0, "sun_color": [1.0, 0.95, 0.85],
        "sky_light_intensity": 2.6, "sky_light_color": [0.9, 0.95, 1.0], "sky_luminance_factor": [1.0, 1.0, 1.05],
        "rayleigh_scattering": [0.175, 0.41, 1.0], "mie_scattering_scale": 0.004,
        "fog_density": 0.03, "fog_height_falloff": 0.6, "fog_height_cm": -3000.0, "fog_inscattering_color": [0.75, 0.8, 0.9],
        "fog_max_opacity": 0.9, "fog_start_distance_cm": 800.0,
        "second_fog_density": 0.25, "second_fog_height_falloff": 3.0, "second_fog_height_offset_cm": -1500.0,
        "volumetric_fog_albedo": [0.95, 0.95, 1.0], "volumetric_fog_emissive": [0.0, 0.0, 0.0], "volumetric_fog_extinction_scale": 0.8,
    },
    "post_process": {
        "priority": 10, "z_cm": [-6000.0, 8000.0],
        "settings": {"color_saturation": [0.8, 0.8, 0.8, 1.0], "color_gain": [1.06, 1.04, 1.0, 1.0], "bloom_intensity": 1.6, "auto_exposure_bias": 0.25},
    },
    "landscape_material": {"vectors": {"RealmTint": [0.92, 0.94, 0.97, 1.0], "LayerTint_Road": [0.66, 0.74, 0.82, 1.0], "LayerTint_Soil": [0.85, 0.9, 0.92, 1.0]}, "scalars": {"RealmBrightness": 1.45, "RealmDesaturation": 0.78}},
    "materials": {
        "PaleRock": (PARENT_ROCKS, {"NormalTopColor": [0.95, 0.95, 0.92, 1.0], "DiffuseMultiplier": [2.1, 2.08, 2.0, 1.0]}, {"Desaturation": 0.7}),
        "PaleCliff": (PARENT_CLIFFS, {"NormalTopColor": [1.0, 1.0, 0.97, 1.0], "DiffuseMultiplier": [1.9, 1.88, 1.85, 1.0]}, {"Desaturation": 0.7}),
        "Cloud": (PARENT_FOG1, {"SandColor": [0.95, 0.96, 1.0, 1.0]}, {"FinalOpacityTweak": 0.85}),
        "CloudLow": (PARENT_FOG2, {"SandColor": [0.86, 0.89, 0.96, 1.0]}, {"FinalOpacityTweak": 0.95}),
        "GoldBeam": (PARENT_BEAM, {"Color": [1.0, 0.82, 0.5, 1.0]}, {}),
    },
    "floor_m": -45.0,
    "mesas": [
        {"name": "P0", "x": 0.0, "y": -170.0, "r": 42.0, "height": 4.0, "bump_angle": -90.0, "bump": 20.0, "bump_width": 0.7},
        {"name": "P1", "x": 0.0, "y": 10.0, "r": 70.0, "height": 10.0},
        {"name": "P2", "x": -160.0, "y": 40.0, "r": 45.0, "height": 7.0},
        {"name": "P3", "x": 160.0, "y": -20.0, "r": 48.0, "height": 6.0},
        {"name": "P4", "x": 40.0, "y": 185.0, "r": 38.0, "height": 14.0},
    ],
    "bridges": [("P0", "P1", 9.0), ("P1", "P2", 8.0), ("P1", "P3", 8.0), ("P1", "P4", 7.0)],
    "clearings": [
        {"id": "C1", "name": "SouthMesa", "x": 0.0, "y": -170.0},
        {"id": "C2", "name": "EastMesa", "x": 160.0, "y": -20.0},
        {"id": "C3", "name": "NorthLookout", "x": 40.0, "y": 185.0},
    ],
    "landmarks": [
        {"name": "PillarSpire", "x": 0.0, "y": 10.0},
        {"name": "NorthLookout", "x": 40.0, "y": 198.0},
    ],
    "pillar_ring": {"x": 0.0, "y": 10.0, "r": 30.0, "count": 10},
    "ring_road_r": 45.0,
}

REALM_CONFIGS = {"SunkenStarfield": SUNKEN_CONFIG, "PaleHeights": PALE_CONFIG}

smoothstep = gav.smoothstep


class TDInputError(Exception):
    pass


class TDAngularNoise:
    def __init__(self, rng, harmonics=5):
        self.amplitudes = rng.uniform(0.3, 1.0, harmonics) / np.arange(1, harmonics + 1)
        self.phases = rng.uniform(0.0, 2.0 * math.pi, harmonics)
        self.norm = float(self.amplitudes.sum())

    def __call__(self, theta):
        total = 0.0
        for k, (a, p) in enumerate(zip(self.amplitudes, self.phases), start=1):
            total = total + a * np.sin(k * theta + p)
        return total / self.norm


class TDMesaShape:
    def __init__(self, rng, name, x, y, r, height, amplitude, edge_width, edge_width_jitter, bump_angle=None, bump=0.0, bump_width=0.7):
        self.name = name
        self.x = x
        self.y = y
        self.r = r
        self.height = height
        self.amplitude = amplitude
        self.edge_width = edge_width
        self.edge_width_jitter = edge_width_jitter
        self.bump_angle = math.radians(bump_angle) if bump_angle is not None else None
        self.bump = bump
        self.bump_width = bump_width
        self.radius_noise = TDAngularNoise(rng)
        self.width_noise = TDAngularNoise(rng)

    def bump_weight(self, theta):
        if self.bump_angle is None:
            return np.zeros_like(np.asarray(theta, dtype=np.float64))
        delta = (np.asarray(theta) - self.bump_angle + math.pi) % (2.0 * math.pi) - math.pi
        return np.exp(-(delta / self.bump_width) ** 2)

    def radius(self, theta):
        b = self.bump_weight(theta)
        return self.r + self.amplitude * self.radius_noise(theta) * (1.0 - b) + self.bump * b

    def rim_width(self, theta):
        b = self.bump_weight(theta)
        w = self.edge_width + self.edge_width_jitter * self.width_noise(theta)
        return w * (1.0 - b) + 6.0 * b

    def mask(self, X, Y):
        theta = np.arctan2(Y - self.y, X - self.x)
        d = np.hypot(X - self.x, Y - self.y)
        r = self.radius(theta)
        return 1.0 - smoothstep(r - self.rim_width(theta), r, d)

    def interior(self, X, Y, margin):
        theta = np.arctan2(Y - self.y, X - self.x)
        d = np.hypot(X - self.x, Y - self.y)
        inner = self.radius(theta) - self.rim_width(theta)
        return 1.0 - smoothstep(inner - margin - 4.0, inner - margin, d)

    def flat_radius(self, theta):
        return float(self.radius(theta) - self.rim_width(theta))


class TDRealmLayout(gav.Layout):
    def __init__(self, f, rng, realm_id):
        super().__init__(f, rng)
        self.realm_id = realm_id
        self.prefix = f"TDRealm_{realm_id}_"
        self.ism_meta = {}

    def label(self, suffix):
        return self.prefix + suffix

    def height(self, x, y):
        return float(sample_bilinear(self.f["h"], np.array([x]), np.array([y]))[0])

    def slope(self, x, y):
        return float(sample_bilinear(self.f["slope"], np.array([x]), np.array([y]))[0])

    def add_ism(self, key, x, y, yaw, scale, roll=0.0, pitch=0.0, z_off=0.0, sink=0.0, z=None, scale3=None, material_name=None, variant=None):
        s3 = scale3 if scale3 else [scale, scale, scale]
        if z is None:
            z = self.height(x, y)
        z = z + gav.MESH_BASE_BELOW_PIVOT.get(key, 0.0) * s3[2] - sink + z_off
        bucket_key = variant or (f"{key}_{material_name}" if material_name else key)
        self.ism_meta[bucket_key] = (key, material_name)
        cx = int((x + HALF) // self.cell_m)
        cy = int((y + HALF) // self.cell_m)
        self.ism.setdefault((bucket_key, cx, cy), []).append(
            [round(x * 100, 1), round(y * 100, 1), round(z * 100, 1), round(pitch, 2), round(yaw, 2), round(roll, 2), round(s3[0], 3), round(s3[1], 3), round(s3[2], 3)])

    def add_actor(self, key, x, y, yaw, scale=1.0, roll=0.0, pitch=0.0, z_off=0.0, label=None, z=None, scale3=None, mesh_path=None, materials=None, tags=None):
        s3 = scale3 if scale3 else [scale, scale, scale]
        if z is None:
            z = self.height(x, y)
        z = z + gav.MESH_BASE_BELOW_PIVOT.get(key, 0.0) * s3[2] + z_off
        e = {"mesh": mesh_path or gav.M[key], "loc": [round(x * 100, 1), round(y * 100, 1), round(z * 100, 1)], "rot": [round(pitch, 2), round(yaw, 2), round(roll, 2)],
             "scale": [round(v, 3) for v in s3], "label": label}
        if materials:
            e["material"] = materials[0]
            e["materials"] = materials
        if tags:
            e["tags"] = tags
        self.actors.append(e)
        return e

    def add_material_plane(self, material_path, x, y, z, yaw, sx, sy, label, sz=1.0):
        self.planes.append({"mesh": gav.ENGINE_PLANE, "material": material_path, "loc": [round(x * 100, 1), round(y * 100, 1), round(z * 100, 1)],
                            "rot": [0.0, round(yaw, 2), 0.0], "scale": [round(sx, 3), round(sy, 3), round(sz, 3)], "label": label})


def stage_rng(seed, stage):
    return np.random.default_rng([seed, stage, 0])


def realm_material_path(realm_id, name):
    return f"/Game/World/Realms/{realm_id}/Materials/MI_TDRealm_{realm_id}_{name}"


def make_grid():
    xs = np.arange(SIZE, dtype=np.float64) - HALF
    return np.meshgrid(xs, xs)


def make_value_noise(rng, cells, size=SIZE):
    grid = rng.random((cells + 3, cells + 3))
    big = ndimage.zoom(grid, size / cells, order=3, prefilter=True)
    return big[:size, :size]


def make_fbm(rng, base_cells, octaves, size=SIZE, gain=0.5, lacunarity=2.0):
    out = np.zeros((size, size), dtype=np.float64)
    amp = 1.0
    cells = base_cells
    total = 0.0
    for _ in range(octaves):
        out += amp * (make_value_noise(rng, int(cells), size) - 0.5) * 2.0
        total += amp
        amp *= gain
        cells *= lacunarity
    return out / total


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


def sample_at(arr, x, y):
    return float(sample_bilinear(arr, np.array([x]), np.array([y]))[0])


def rasterize_polyline(poly, values=None):
    mask = np.zeros((SIZE, SIZE), dtype=bool)
    vals = np.zeros((SIZE, SIZE), dtype=np.float64) if values is not None else None
    ix = np.clip(np.round(poly[:, 0] + HALF).astype(int), 0, SIZE - 1)
    iy = np.clip(np.round(poly[:, 1] + HALF).astype(int), 0, SIZE - 1)
    mask[iy, ix] = True
    if values is not None:
        vals[iy, ix] = values
    return mask, vals


def distance_to_polyline(poly):
    mask, _ = rasterize_polyline(poly)
    return ndimage.distance_transform_edt(~mask)


def scatter_points(rng, density, cell, min_dist, count_cap=None):
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
    accepted = []
    buckets = {}
    for x, y in zip(px[order], py[order]):
        kx, ky = int((x + HALF) // min_dist), int((y + HALF) // min_dist)
        neighbours = (q for dx in (-1, 0, 1) for dy in (-1, 0, 1) for q in buckets.get((kx + dx, ky + dy), ()))
        if any((ox - x) ** 2 + (oy - y) ** 2 < min_dist * min_dist for ox, oy in neighbours):
            continue
        buckets.setdefault((kx, ky), []).append((x, y))
        accepted.append((float(x), float(y)))
        if count_cap and len(accepted) >= count_cap:
            break
    return accepted


def edge_distance(X, Y):
    return np.minimum.reduce([X + HALF, HALF - X, Y + HALF, HALF - Y])


def flatten_pads(h, X, Y, pad_groups, falloff):
    for circles in pad_groups:
        inner = np.zeros(h.shape, dtype=bool)
        weight = np.zeros(h.shape)
        for (cx, cy, r) in circles:
            d = np.hypot(X - cx, Y - cy)
            inner |= d < r
            weight = np.maximum(weight, 1.0 - smoothstep(r, r + falloff, d))
        target = float(np.mean(h[inner]))
        h = h * (1.0 - weight) + weight * target
    return h


def apply_roads(h, roads, half_width, falloff, window):
    road_weight = np.zeros(h.shape)
    road_layer = np.zeros(h.shape)
    polys = {}
    h_base = h.copy()
    for name, pts in roads.items():
        poly = gav.catmull_rom(pts, 0.5)
        polys[name] = poly
        prof = sample_bilinear(h_base, poly[:, 0], poly[:, 1])
        prof = ndimage.uniform_filter1d(prof, window, mode="nearest")
        prof = ndimage.uniform_filter1d(prof, window, mode="nearest")
        mask, vals = rasterize_polyline(poly, prof)
        dist, (ny, nx) = ndimage.distance_transform_edt(~mask, return_indices=True)
        w = 1.0 - smoothstep(half_width, half_width + falloff, dist)
        h = h * (1.0 - w) + w * vals[ny, nx]
        road_weight = np.maximum(road_weight, w)
        road_layer = np.maximum(road_layer, 1.0 - smoothstep(half_width - 0.5, half_width + 1.5, dist))
    return h, road_weight, road_layer, polys


def ramp_geometry(t):
    a = math.radians(t["ramp_angle"])
    u = (math.cos(a), math.sin(a))
    start_d = t["r"] - 10.0
    length = (t["height"] + 2.5) / math.tan(math.radians(15.0))
    start = (t["x"] + u[0] * start_d, t["y"] + u[1] * start_d)
    foot = (start[0] + u[0] * length, start[1] + u[1] * length)
    return {"name": t["name"], "start": start, "foot": foot, "u": u, "length": length}


def apply_ramp(h, X, Y, ramp):
    ux, uy = ramp["u"]
    sx, sy = ramp["start"]
    length = ramp["length"]
    h_start = sample_at(h, sx, sy)
    h_foot = sample_at(h, *ramp["foot"])
    along = (X - sx) * ux + (Y - sy) * uy
    lateral = np.abs(-(X - sx) * uy + (Y - sy) * ux)
    t = along / length
    lateral_mask = 1.0 - smoothstep(6.0, 9.0, lateral)
    along_mask = smoothstep(-8.0 / length, -4.0 / length, t) * (1.0 - smoothstep(1.0, 1.0 + 6.0 / length, t))
    m = lateral_mask * along_mask
    ramp_h = h_start + (h_foot - h_start) * np.clip(t, 0.0, 1.0)
    return h * (1.0 - m) + m * ramp_h


def sunken_roads(cfg):
    ramps = {t["name"]: ramp_geometry(t) for t in cfg["terraces"]}
    n_foot, n_start = ramps["North"]["foot"], ramps["North"]["start"]
    se_foot, se_start = ramps["Southeast"]["foot"], ramps["Southeast"]["start"]
    se_u = ramps["Southeast"]["u"]
    se_turn = (se_foot[0] + se_u[0] * 9.0, se_foot[1] + se_u[1] * 9.0)
    n_u = ramps["North"]["u"]
    n_turn = (n_foot[0] + n_u[0] * 9.0, n_foot[1] + n_u[1] * 9.0)
    return {
        "North": [(-172.0, 14.0), (-140.0, 45.0), (-95.0, 86.0), (-45.0, 100.0), (0.0, 102.0), n_turn, (60.0, 106.0), (115.0, 95.0), (150.0, 80.0), (168.0, 72.0)],
        "South": [(-172.0, 6.0), (-140.0, -25.0), (-90.0, -42.0), (-30.0, -37.0), (25.0, -60.0), (58.0, -84.0), se_turn, (100.0, -70.0), (130.0, -58.0), (160.0, -46.0), (178.0, -20.0), (185.0, 22.0), (163.9, 42.0), (162.4, 61.0)],
        "BranchC1": [(-75.0, -40.5), (-68.0, -58.0), (-62.0, -70.0)],
        "BranchC2": [(60.0, 106.0), (78.0, 120.0), (86.0, 132.0)],
        "BranchC3": [(37.0, -69.0), (48.0, -55.0), (55.0, -48.0)],
        "BranchC4": [n_turn, n_foot, n_start, (-14.0, 152.0)],
        "BranchCrater": [se_turn, se_foot, se_start, (128.0, -115.0)],
    }, list(ramps.values())


def build_sunken_terrain(cfg, rng):
    X, Y = make_grid()
    ed = edge_distance(X, Y)
    h = 1.6 * make_fbm(rng, 8, 3) + 0.3 * make_fbm(rng, 40, 2)
    h = np.maximum(h, -1.4)
    lake = cfg["lake"]
    shore_noise = TDAngularNoise(rng)
    e = np.hypot((X - lake["x"]) / lake["rx"], (Y - lake["y"]) / lake["ry"])
    e = e * (1.0 + 0.07 * shore_noise(np.arctan2((Y - lake["y"]) / lake["ry"], (X - lake["x"]) / lake["rx"])))
    lake_w = 1.0 - smoothstep(0.9, 1.35, e)
    lake_bed = -lake["depth"] * (1.0 - smoothstep(0.6, 1.15, e))
    h = h * (1.0 - lake_w) + lake_w * np.minimum(h, lake_bed)
    channel_w = np.zeros(h.shape)
    channel_polys = {}
    for name, pts in cfg["channels"].items():
        poly = gav.catmull_rom(pts, 0.5)
        channel_polys[name] = poly
        w = (1.0 - smoothstep(3.0, 8.0, distance_to_polyline(poly))) * smoothstep(38.0, 46.0, ed)
        h = h * (1.0 - w) + w * np.minimum(h, SUNKEN_CHANNEL_BED_M)
        channel_w = np.maximum(channel_w, w)
    top_noise = 0.35 * make_fbm(rng, 12, 2)
    for t in cfg["terraces"]:
        noise = TDAngularNoise(rng)
        theta = np.arctan2(Y - t["y"], X - t["x"])
        r = t["r"] + 4.0 * noise(theta)
        m = 1.0 - smoothstep(r - 4.0, r, np.hypot(X - t["x"], Y - t["y"]))
        h = h * (1.0 - m) + m * np.maximum(h, t["height"] + top_noise)
    roads, ramps = sunken_roads(cfg)
    for ramp in ramps:
        h = apply_ramp(h, X, Y, ramp)
    c = cfg["crater"]
    dc = np.hypot(X - c["x"], Y - c["y"])
    h = h - c["depth"] * (1.0 - smoothstep(0.0, c["r"], dc)) + 0.6 * np.exp(-((dc - c["r"] - 1.0) / 2.5) ** 2)
    band = 31.0 + 5.0 * np.clip(make_value_noise(rng, 10), 0.0, 1.0)
    wall_top = 38.0 + 17.0 * np.clip(make_value_noise(rng, 6), 0.0, 1.0) + 2.0 * make_fbm(rng, 30, 2)
    t_wall = np.clip((band - ed) / (band - 5.0), 0.0, 1.0)
    wall_p = smoothstep(0.0, 1.0, smoothstep(0.0, 1.0, t_wall))
    h = h * (1.0 - wall_p) + wall_p * wall_top
    pads = plaza_pads(cfg) + [[(cl["x"], cl["y"], 20.0)] for cl in cfg["clearings"]]
    h = flatten_pads(h, X, Y, pads, 14.0)
    h = ndimage.gaussian_filter(h, 0.8)
    h, road_weight, road_layer, road_polys = apply_roads(h, roads, 2.5, 6.0, 61)
    h, passage_fields = carve_realm_passages(h, X, Y, cfg)
    water = (h < SUNKEN_WATER_Z_M) & ((lake_w > 0.5) | (channel_w > 0.5))
    fields = {"X": X, "Y": Y, "h": h, "slope": gav.terrain_slope_deg(h), "edge_d": ed, "wall": wall_p, "lake_e": e, "channel_w": channel_w,
              "channel_polys": channel_polys, "water": water, "road_polys": road_polys, "ramps": ramps}
    fields.update(merge_passage_roads(road_weight, road_layer, passage_fields))
    return fields


def pale_mesa_shapes(cfg, rng):
    shapes = {}
    for m in cfg["mesas"]:
        shapes[m["name"]] = TDMesaShape(rng, m["name"], m["x"], m["y"], m["r"], m["height"], 8.0, 8.0, 2.0,
                                        m.get("bump_angle"), m.get("bump", 0.0), m.get("bump_width", 0.7))
    return shapes


def segment_distance(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def pale_bridge_segments(cfg, shapes):
    segs = []
    for a_name, b_name, width in cfg["bridges"]:
        a, b = shapes[a_name], shapes[b_name]
        ang_ab = math.atan2(b.y - a.y, b.x - a.x)
        ang_ba = ang_ab + math.pi
        ux, uy = math.cos(ang_ab), math.sin(ang_ab)
        da = a.flat_radius(ang_ab) - 3.0
        db = b.flat_radius(ang_ba) - 3.0
        segs.append({"name": f"{a_name}{b_name}", "a": (a.x + ux * da, a.y + uy * da), "b": (b.x - ux * db, b.y - uy * db), "width": width,
                     "center_a": (a.x, a.y), "center_b": (b.x, b.y)})
    return segs


def pale_islands(cfg, rng, shapes, bridges):
    islands = []
    for _ in range(600):
        if len(islands) >= 5:
            break
        r = float(rng.uniform(10.0, 18.0))
        x = float(rng.uniform(-252.0 + r + 18.0, 252.0 - r - 18.0))
        y = float(rng.uniform(-252.0 + r + 18.0, 252.0 - r - 18.0))
        height = float(rng.uniform(2.0, 12.0))
        if any(math.hypot(x - s.x, y - s.y) < r + s.r + s.bump + 30.0 for s in shapes.values()):
            continue
        if any(segment_distance(x, y, *bseg["center_a"], *bseg["center_b"]) < r + 30.0 for bseg in bridges):
            continue
        if any(math.hypot(x - o.x, y - o.y) < r + o.r + 18.0 for o in islands):
            continue
        islands.append(TDMesaShape(rng, f"Island{len(islands)}", x, y, r, height, 3.0, 5.0, 1.0))
    return islands


def pale_roads(cfg):
    ring_r = cfg["ring_road_r"]
    cx, cy = cfg["pillar_ring"]["x"], cfg["pillar_ring"]["y"]

    def ring_point(target):
        a = math.atan2(target[1] - cy, target[0] - cx)
        return (cx + ring_r * math.cos(a), cy + ring_r * math.sin(a))

    def along(a, b, t):
        return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)

    p2, p3, p4 = (-160.0, 40.0), (160.0, -20.0), (40.0, 185.0)
    ring = [(cx + ring_r * math.cos(math.radians(a)), cy + ring_r * math.sin(math.radians(a))) for a in range(-90, 271, 30)]
    south = ring_point((0.0, -170.0))
    return {
        "Main": [(0.0, -170.0), (0.0, -130.0), (0.0, -90.0), (0.0, -58.0), south],
        "Ring": ring,
        "Spur": [south, (0.0, -20.0), (0.0, -6.0)],
        "West": [ring_point(p2), along((cx, cy), p2, 0.5), along((cx, cy), p2, 0.72), along((cx, cy), p2, 0.9)],
        "East": [ring_point(p3), along((cx, cy), p3, 0.5), along((cx, cy), p3, 0.72), along((cx, cy), p3, 0.9)],
        "North": [ring_point(p4), along((cx, cy), p4, 0.5), along((cx, cy), p4, 0.75), along((cx, cy), p4, 0.9)],
    }


def build_pale_terrain(cfg, rng, shapes, islands, bridges):
    X, Y = make_grid()
    floor = cfg["floor_m"] + 3.0 * make_fbm(rng, 6, 3)
    top_noise = 1.5 * make_fbm(rng, 5, 2)
    h = floor.copy()
    for s in list(shapes.values()) + islands:
        m = s.mask(X, Y)
        h = np.maximum(h, floor + (s.height + top_noise - floor) * m)
    h = ndimage.gaussian_filter(h, 0.6)
    for b in bridges:
        ax, ay = b["a"]
        bx, by = b["b"]
        length = math.hypot(bx - ax, by - ay)
        ux, uy = (bx - ax) / length, (by - ay) / length
        za, zb = sample_at(h, ax, ay), sample_at(h, bx, by)
        s = ((X - ax) * ux + (Y - ay) * uy) / length
        lateral = np.abs(-(X - ax) * uy + (Y - ay) * ux)
        sc = np.clip(s, 0.0, 1.0)
        half = b["width"] * 0.5
        deck = za + (zb - za) * sc - 1.0 * np.sin(math.pi * sc) - 0.25 * smoothstep(half, half + 1.2, lateral)
        lat_m = 1.0 - smoothstep(half + 1.2, half + 7.2, lateral)
        along_m = smoothstep(-3.0 / length, 0.0, s) * (1.0 - smoothstep(1.0, 1.0 + 3.0 / length, s))
        h = np.maximum(h, floor + (deck - floor) * lat_m * along_m)
    pads = plaza_pads(cfg) + [[(cl["x"], cl["y"], 20.0)] for cl in cfg["clearings"]]
    h = flatten_pads(h, X, Y, pads, 6.0)
    h, road_weight, road_layer, road_polys = apply_roads(h, pale_roads(cfg), 2.5, 2.5, 41)
    h, passage_fields = carve_realm_passages(h, X, Y, cfg)
    top = np.zeros(h.shape)
    for s in list(shapes.values()) + islands:
        top = np.maximum(top, s.interior(X, Y, 0.0))
    fields = {"X": X, "Y": Y, "h": h, "slope": gav.terrain_slope_deg(h), "edge_d": edge_distance(X, Y), "floor": floor, "top": top,
              "road_polys": road_polys, "ramps": []}
    fields.update(merge_passage_roads(road_weight, road_layer, passage_fields))
    return fields


def gate_passage(cfg, gate):
    passage = rp.PASSAGES_BY_GATE.get(gate["gate_id"])
    if passage is None:
        raise TDInputError(f"{gate['gate_id']}: not in realm_passages.PASSAGES")
    if passage["realm_id"] != cfg["realm_id"]:
        raise TDInputError(f"{gate['gate_id']}: realm_passages realm {passage['realm_id']} != {cfg['realm_id']}")
    return passage


def plaza_pads(cfg):
    return [[(g["plaza"][0], g["plaza"][1], cfg["plaza_pad_radius_m"])] for g in cfg["gates"]]


def carve_realm_passages(h, X, Y, cfg):
    floor_total = np.zeros_like(h)
    footprint_total = np.zeros_like(h)
    carved = []
    for gate in cfg["gates"]:
        passage = gate_passage(cfg, gate)
        plaza_z = sample_at(h, *gate["plaza"])
        z0 = rp.seam_floor_z(passage, "realm", plaza_z)
        s_start, s_end = rp.default_span(passage, "realm")
        h, floor_mask, wall_mask = rp.stamp_passage(h, X, Y, passage, "realm", z0, s_start, s_end, plaza_z=plaza_z)
        floor_total = np.maximum(floor_total, floor_mask)
        footprint_total = np.maximum(footprint_total, floor_mask + wall_mask)
        carved.append({"gate_id": gate["gate_id"], "passage": passage, "plaza": tuple(gate["plaza"]), "plaza_z": plaza_z, "z0": z0, "s_start": s_start, "s_end": s_end})
    return h, {"passage_floor": floor_total, "passage_footprint": footprint_total, "passages": carved}


def merge_passage_roads(road_weight, road_layer, passage_fields):
    merged = dict(passage_fields)
    merged["road_only_layer"] = road_layer
    merged["road_weight"] = np.maximum(road_weight, passage_fields["passage_floor"])
    merged["road_layer"] = np.maximum(road_layer, passage_fields["passage_floor"])
    return merged


def is_in_passage_band(passages, x, y, margin=PASSAGE_CLEAR_MARGIN_M):
    inside = np.zeros(np.shape(x), dtype=bool)
    for c in passages:
        s, l = rp.passage_coords(c["passage"], "realm", x, y)
        inside |= (np.abs(l) <= rp.BAND_OUTER_M + margin) & (s >= c["s_start"] - rp.END_CAP_M - margin) & (s <= c["s_end"] + margin)
    return inside


def seam_start_local(passage):
    return rp.to_grid_m(passage, "realm", SEAM_FLOOD_START_S_M, 0.0)


def normalize_layers(ordered, fill_name):
    layers = {}
    remaining = np.ones((SIZE, SIZE))
    for name, w in ordered:
        w = np.clip(w, 0.0, 1.0) * remaining
        layers[name] = w
        remaining = remaining - w
    layers[fill_name] = np.clip(remaining, 0.0, 1.0)
    total = sum(layers.values())
    return {k: layers[k] / total for k in LAYER_NAMES}


def build_sunken_layers(f, rng):
    slope = f["slope"]
    n_rough = make_fbm(rng, 18, 3)
    n_mid = make_value_noise(rng, 60)
    water_dist = ndimage.distance_transform_edt(~f["water"])
    bedrock = np.maximum(smoothstep(30.0, 42.0, slope), smoothstep(0.1, 0.3, f["wall"]))
    mud = (1.0 - smoothstep(4.0, 12.0, water_dist)) * (0.65 + 0.35 * smoothstep(0.3, 0.7, n_mid))
    moss = f["forest"] * (1.0 - smoothstep(15.0, 25.0, slope)) * (0.55 + 0.6 * smoothstep(0.3, 0.7, n_mid))
    rock = np.maximum(smoothstep(0.15, 0.45, n_rough) * 0.8, smoothstep(18.0, 30.0, slope) * 0.6) * (1.0 - f["road_layer"])
    return normalize_layers((("Road", f["road_layer"]), ("Bedrock", bedrock), ("Mud", mud), ("Moss", moss), ("Rock", rock)), "Soil")


def build_pale_layers(f, rng):
    slope, h = f["slope"], f["h"]
    n_grass = make_fbm(rng, 10, 3)
    n_moss = make_value_noise(rng, 40)
    bedrock = np.maximum(smoothstep(30.0, 45.0, slope), 1.0 - smoothstep(-28.0, -22.0, h))
    soil = f["top"] * smoothstep(-0.15, 0.2, n_grass) * (0.8 + 0.2 * smoothstep(0.3, 0.7, n_moss))
    moss = f["top"] * smoothstep(0.72, 0.85, n_moss) * 0.8
    mud = np.zeros(h.shape)
    return normalize_layers((("Road", f["road_layer"]), ("Bedrock", bedrock), ("Soil", soil), ("Moss", moss), ("Mud", mud)), "Rock")


def topdown_route_masks(cfg, grid, road_mask):
    X, Y = grid.meter_axes()
    rid = cfg["realm_id"]
    floors = [rp.floor_cells(passage, "realm", X, Y) for passage in rp.PASSAGES if passage["realm_id"] == rid]
    circles = [(c["x"], c["y"], 20.0) for c in cfg["clearings"]]
    circles += [(g["plaza"][0], g["plaza"][1], cfg["plaza_pad_radius_m"]) for g in cfg["gates"]]
    if cfg.get("pillar_ring"):
        ring = cfg["pillar_ring"]
        circles.append((ring["x"], ring["y"], ring["r"] - 2.0))
    water_level = SUNKEN_WATER_Z_M if rid == "SunkenStarfield" else PALE_WATER_LEVEL_M
    water = (grid.h < water_level) & ~np.logical_or.reduce(floors)
    return tdr.TDRouteMasks(grid, road_mask, circles, water, floors)


def apply_topdown_rules(L, f, cfg):
    if not tdr.ENABLED:
        return {}
    grid = tdr.TDTerrainGrid(tdr.quantize_height_m(f["h"], gav.UNITS_PER_METER))
    masks = topdown_route_masks(cfg, grid, f["road_layer"] >= 0.5)
    return tdr.apply_placement_rules(L.ism, L.actors, lambda bucket_key: gav.M[L.ism_meta[bucket_key[0]][0]], grid, masks)


def build_exclusion(f, circles):
    X, Y = f["X"], f["Y"]
    excl = np.maximum(smoothstep(0.02, 0.6, f["road_weight"]), (f["passage_footprint"] > 0.0).astype(np.float64))
    for (cx, cy, r) in circles:
        excl = np.maximum(excl, gav.radial(X, Y, cx, cy, r, r + 4.0))
    return np.clip(excl, 0.0, 1.0)


def place_common_markers(L, cfg):
    for g in cfg["gates"]:
        passage = gate_passage(cfg, g)
        sx, sy = passage["realm_seam_local_m"]
        L.add_marker("RealmArrival", sx, sy, passage["yaw_deg"], L.label(f"Arrival_{g['gate_id']}"), tags=["TDRealmArrival", g["gate_id"]])
    for cl in cfg["clearings"]:
        L.add_marker("CombatClearing", cl["x"], cl["y"], 0.0, L.label(f"Clearing_{cl['id']}_{cl['name']}"), tags=["TDRealmCombatClearing", cl["id"]])
    for lm in cfg["landmarks"]:
        L.add_marker("Landmark", lm["x"], lm["y"], 0.0, L.label(f"Landmark_{lm['name']}"), tags=["TDRealmLandmark", lm["name"]])


def pick_spaced(points, count, min_dist):
    picked = []
    for p in points:
        if all((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 >= min_dist * min_dist for q in picked):
            picked.append(p)
        if len(picked) >= count:
            break
    return picked


def place_sunken(L, rng, f, cfg):
    X, Y, h, slope, ed = f["X"], f["Y"], f["h"], f["slope"], f["edge_d"]
    rid = cfg["realm_id"]
    mat = {name: realm_material_path(rid, name) for name in cfg["materials"]}
    c2 = next(c for c in cfg["clearings"] if c["id"] == "C2")
    c1 = next(c for c in cfg["clearings"] if c["id"] == "C1")
    c4 = next(c for c in cfg["clearings"] if c["id"] == "C4")
    crater = cfg["crater"]
    circles = [(g["plaza"][0], g["plaza"][1], 20.0) for g in cfg["gates"]]
    circles += [(c["x"], c["y"], 23.0) for c in cfg["clearings"]]
    circles += [(crater["x"], crater["y"], 15.0), (c4["x"], c4["y"], 31.0)]
    excl = np.maximum(build_exclusion(f, circles), 1.0 - smoothstep(-1.6, -1.0, h))
    interior = smoothstep(40.0, 46.0, ed)
    flat_ok = 1.0 - smoothstep(18.0, 28.0, slope)
    patch = make_fbm(rng, 7, 3)
    forest = smoothstep(-0.02, 0.22, patch) * interior * flat_ok * (1.0 - excl)
    forest = np.maximum(forest, gav.radial(X, Y, c2["x"], c2["y"], 30.0, 60.0) * 0.9 * flat_ok * (1.0 - excl))
    f["forest"] = forest
    rough = smoothstep(0.1, 0.4, make_fbm(rng, 18, 2))

    giants = []
    for i, (x, y) in enumerate(scatter_points(rng, forest * 0.9, 5.0, 9.0, 234)):
        material = "GlowCyan" if i % 2 == 0 else "GlowViolet"
        L.add_ism("Shroom1" if rng.random() < 0.55 else "Shroom2", x, y, rng.uniform(0, 360), rng.uniform(2.5, 5.0), roll=rng.uniform(-6, 6), pitch=rng.uniform(-6, 6), material_name=material)
        giants.append((x, y, material))
    for k in range(16):
        a = 2.0 * math.pi * k / 16 + rng.uniform(-0.08, 0.08)
        r = rng.uniform(22.0, 25.0)
        x, y = c2["x"] + r * math.cos(a), c2["y"] + r * math.sin(a)
        L.add_ism("Shroom1" if k % 3 else "Shroom2", x, y, rng.uniform(0, 360), rng.uniform(2.8, 4.5), material_name="GlowCyan" if k % 2 == 0 else "GlowViolet")
    for x, y in scatter_points(rng, np.clip(forest * 0.9 + 0.12 * interior * flat_ok * (1.0 - excl), 0, 1), 3.0, 2.2, 900):
        L.add_ism("Shroom1" if rng.random() < 0.6 else "Shroom2", x, y, rng.uniform(0, 360), rng.uniform(0.5, 1.4))
    for i, (x, y) in enumerate(scatter_points(rng, np.clip(0.45 * interior * flat_ok * (1.0 - excl) + 0.4 * forest, 0, 1), 2.5, 1.8, 700)):
        L.add_ism("Shroom1" if i % 2 else "Shroom2", x, y, rng.uniform(0, 360), rng.uniform(0.25, 0.5), material_name="GlowCyan" if i % 3 else "GlowViolet")
    for x, y in scatter_points(rng, forest, 3.0, 3.0, 600):
        L.add_ism("Mycelium", x, y, rng.uniform(0, 360), rng.uniform(0.8, 1.8), sink=0.03)
    dread_d = np.clip(forest * 0.7 + 0.25 * interior * flat_ok * (1.0 - excl) * smoothstep(0.3, 0.7, make_value_noise(rng, 20)), 0, 1)
    for x, y in scatter_points(rng, dread_d, 3.5, 3.0, 500):
        L.add_ism("Dread1" if rng.random() < 0.5 else "Dread2", x, y, rng.uniform(0, 360), rng.uniform(0.3, 0.8), sink=0.03)
    rock_d = np.clip(0.25 + 0.5 * rough + 0.5 * smoothstep(14.0, 30.0, slope), 0, 1) * interior * (1.0 - excl)
    for x, y in scatter_points(rng, rock_d, 5.0, 6.0, 400):
        L.add_ism(["Rock1", "Rock2", "Rock3"][int(rng.integers(0, 3))], x, y, rng.uniform(0, 360), rng.uniform(0.4, 1.3), roll=rng.uniform(-8, 8), pitch=rng.uniform(-8, 8), sink=0.25, material_name="DarkRock")
    for x, y in scatter_points(rng, 0.5 * interior * (1.0 - excl * 0.7), 3.0, 2.0, 900):
        L.add_ism(f"SRock{int(rng.integers(3, 9))}", x, y, rng.uniform(0, 360), rng.uniform(0.6, 1.6), roll=rng.uniform(-10, 10), sink=0.04, material_name="DarkRock")
    pillar_d = interior * (1.0 - excl) * (h > -1.2) * (1.0 - smoothstep(12.0, 20.0, slope)) * 0.8
    for x, y in scatter_points(rng, pillar_d, 10.0, 26.0, 60):
        key = "Cliff4" if rng.random() < 0.6 else "Cliff3"
        sxy = rng.uniform(0.45, 0.8)
        L.add_ism(key, x, y, rng.uniform(0, 360), sxy, roll=rng.uniform(-4, 4), pitch=rng.uniform(-4, 4), sink=1.0, scale3=[sxy, sxy, rng.uniform(4.0, 7.0)], material_name="DarkCliff", variant=f"{key}_StarPillar")
    for x, y in scatter_points(rng, smoothstep(28.0, 40.0, slope) * (1.0 - smoothstep(40.0, 46.0, ed)), 4.0, 8.0, 200):
        s = rng.uniform(1.6, 3.5)
        L.add_ism(["Cliff2", "Cliff3", "Cliffs1"][int(rng.integers(0, 3))], x, y, rng.uniform(0, 360), s, roll=rng.uniform(-15, 15), pitch=rng.uniform(-15, 15), sink=0.6 * s, material_name="DarkCliff")
    bone_keys = ["Skull", "Bone", "Bone", "RamSkull", "RamSkull2", "StickedSkull"]
    n_bones = 0
    for _ in range(400):
        if n_bones >= 85:
            break
        a, r = rng.uniform(0, 2 * math.pi), rng.uniform(21.0, 48.0)
        x, y = c1["x"] + r * math.cos(a), c1["y"] + r * math.sin(a)
        if L.height(x, y) < -1.2 or L.slope(x, y) > 25.0:
            continue
        L.add_ism(bone_keys[int(rng.integers(0, len(bone_keys)))], x, y, rng.uniform(0, 360), rng.uniform(1.0, 2.2), roll=rng.uniform(-20, 20), sink=0.03)
        n_bones += 1
    road_pts = np.concatenate([f["road_polys"][k] for k in ("North", "South")])
    for _ in range(400):
        if n_bones >= 120:
            break
        p = road_pts[int(rng.integers(0, len(road_pts)))]
        a, r = rng.uniform(0, 2 * math.pi), rng.uniform(4.0, 7.0)
        x, y = float(p[0]) + r * math.cos(a), float(p[1]) + r * math.sin(a)
        if L.height(x, y) < -1.2 or L.slope(x, y) > 25.0:
            continue
        L.add_ism(bone_keys[int(rng.integers(0, len(bone_keys)))], x, y, rng.uniform(0, 360), rng.uniform(1.0, 1.8), roll=rng.uniform(-20, 20), sink=0.03)
        n_bones += 1

    for k in range(8):
        a = 2.0 * math.pi * k / 8 + math.radians(22.5)
        x, y = c4["x"] + 26.0 * math.cos(a), c4["y"] + 26.0 * math.sin(a)
        sxy = rng.uniform(0.6, 0.75)
        L.add_actor("Cliff4", x, y, math.degrees(a) + rng.uniform(-10, 10), scale3=[sxy, sxy, rng.uniform(6.0, 8.0)], z_off=-1.0, materials=[mat["DarkCliff"]], label=L.label(f"StarPillar_{k}"))
    L.add_actor("LightBeam", c4["x"], c4["y"], 0.0, 9.0, z_off=9.0 * 0.62 + 2.5, materials=[mat["PaleBeam"]], label=L.label("StarPillarRing_Beam"))
    L.add_actor("LightBeam", crater["x"], crater["y"], 0.0, 12.0, z_off=12.0 * 0.62 + 2.5, materials=[mat["PaleBeam"]], label=L.label("Crater_Beam"))
    for k in range(5):
        a, r = rng.uniform(0, 2 * math.pi), rng.uniform(0.0, 5.0)
        x, y = crater["x"] + r * math.cos(a), crater["y"] + r * math.sin(a)
        L.add_actor(["Rock1", "Rock2", "Rock3"][k % 3], x, y, rng.uniform(0, 360), rng.uniform(1.5, 2.5), roll=rng.uniform(-25, 25), pitch=rng.uniform(-25, 25), z_off=-0.6, materials=[mat["ShardGlow"]], label=L.label(f"Crater_StarShard_{k}"))
    L.add_actor("LightBeam", c2["x"], c2["y"], 0.0, 7.0, z_off=7.0 * 0.62 + 2.5, materials=[mat["PaleBeam"]], label=L.label("MushroomRing_Beam"))
    for x, y, _ in pick_spaced(giants[::-1], 9, 45.0):
        s = rng.uniform(5.0, 8.0)
        L.add_ism("LightBeam", x + 2.0, y, rng.uniform(0, 360), s, pitch=rng.uniform(-6, 6), roll=rng.uniform(-6, 6), z_off=s * 0.62 + 2.5, material_name="PaleBeam")

    lake = cfg["lake"]
    L.add_material_plane(mat["StarWater"], lake["x"], lake["y"], SUNKEN_WATER_Z_M, 0.0, 2.0 * lake["rx"] + 6.0, 2.0 * lake["ry"] + 6.0, L.label("Water_Lake"))
    for name, poly in f["channel_polys"].items():
        idx = list(range(0, len(poly) - 1, 24)) + [len(poly) - 1]
        for i in range(len(idx) - 1):
            a, b = poly[idx[i]], poly[idx[i + 1]]
            mx, my = float((a[0] + b[0]) * 0.5), float((a[1] + b[1]) * 0.5)
            if sample_at(ed, mx, my) < 44.0 or sample_at(f["lake_e"], mx, my) < 0.85 or sample_at(f["road_weight"], mx, my) > 0.3:
                continue
            length = float(np.hypot(b[0] - a[0], b[1] - a[1]))
            L.add_material_plane(mat["StarWater"], mx, my, SUNKEN_WATER_Z_M, math.degrees(math.atan2(b[1] - a[1], b[0] - a[0])), length + 2.0, 8.0, L.label(f"Water_{name}_{i}"))
    fog_i = 0
    for k in range(14):
        a = 2.0 * math.pi * k / 14 + rng.uniform(-0.1, 0.1)
        x, y = lake["x"] + lake["rx"] * 1.02 * math.cos(a), lake["y"] + lake["ry"] * 1.02 * math.sin(a)
        s = rng.uniform(24.0, 40.0)
        L.add_material_plane(mat["VioletFog"], x, y, max(L.height(x, y), SUNKEN_WATER_Z_M) + rng.uniform(0.6, 1.4), rng.uniform(0, 360), s, s, L.label(f"Fog_{fog_i}"), sz=s)
        fog_i += 1
    for name, poly in f["channel_polys"].items():
        for j in (len(poly) // 6, len(poly) // 3, len(poly) // 2):
            x, y = float(poly[j][0]), float(poly[j][1])
            s = rng.uniform(22.0, 34.0)
            L.add_material_plane(mat["VioletFog"], x, y, max(L.height(x, y), SUNKEN_WATER_Z_M) + rng.uniform(0.6, 1.2), rng.uniform(0, 360), s, s, L.label(f"Fog_{fog_i}"), sz=s)
            fog_i += 1
    for x, y in scatter_points(rng, forest * 0.6, 20.0, 26.0, 20):
        s = rng.uniform(22.0, 42.0)
        L.add_material_plane(mat["VioletFog"], x, y, L.height(x, y) + rng.uniform(0.6, 1.4), rng.uniform(0, 360), s, s, L.label(f"Fog_{fog_i}"), sz=s)
        fog_i += 1

    light_i = 0

    def light(x, y, z_off, intensity, color, radius_m):
        nonlocal light_i
        L.add_light("Point", x, y, z_off, intensity, color, radius_m, L.label(f"Light_{light_i}"), source_radius=30.0, volumetric=2.5, cast_shadows=False)
        light_i += 1

    for g in cfg["gates"]:
        px, py = g["plaza"]
        light(px, py + 9.0, 3.0, 60.0, GLOW_CYAN, 16.0)
        light(px, py - 9.0, 3.0, 60.0, GLOW_VIOLET, 16.0)
    for k in range(4):
        a = 2.0 * math.pi * k / 4
        light(crater["x"] + 6.0 * math.cos(a), crater["y"] + 6.0 * math.sin(a), 2.0, 80.0, GLOW_VIOLET, 18.0)
    for k in range(3):
        a = 2.0 * math.pi * k / 3 + 0.4
        light(c4["x"] + 18.0 * math.cos(a), c4["y"] + 18.0 * math.sin(a), 4.0, 55.0, GLOW_CYAN, 18.0)
    for k in range(3):
        a = 2.0 * math.pi * k / 3 + 1.1
        light(c2["x"] + 23.0 * math.cos(a), c2["y"] + 23.0 * math.sin(a), 2.5, 50.0, GLOW_CYAN if k % 2 else GLOW_VIOLET, 15.0)
    for k in range(6):
        a = 2.0 * math.pi * k / 6 + 0.3
        light(lake["x"] + lake["rx"] * 0.95 * math.cos(a), lake["y"] + lake["ry"] * 0.95 * math.sin(a), 1.5, 45.0, GLOW_CYAN, 18.0)
    for k in (-1, 1):
        light(c1["x"] + 10.0 * k, c1["y"] - 6.0, 2.0, 50.0, GLOW_VIOLET, 14.0)
    for x, y, material in pick_spaced(giants, 31, 20.0):
        light(x + 1.0, y, 2.5, 45.0, GLOW_CYAN if material == "GlowCyan" else GLOW_VIOLET, round(float(rng.uniform(12.0, 18.0)), 1))

    fx_i = 0

    def fx(key, x, y, z_off):
        nonlocal fx_i
        L.add_niagara(key, x, y, z_off, L.label(f"FX_Dust_{fx_i}"))
        fx_i += 1

    for g in cfg["gates"]:
        fx("Dust1", g["plaza"][0], g["plaza"][1], 1.5)
    fx("Dust2", crater["x"] + 3.0, crater["y"], 1.0)
    fx("Dust1", crater["x"] - 3.0, crater["y"] + 2.0, 2.0)
    fx("Dust1", c4["x"], c4["y"], 2.0)
    fx("Dust2", c2["x"], c2["y"], 1.5)
    fx("Dust2", c1["x"], c1["y"], 1.2)
    for k in range(3):
        a = 2.0 * math.pi * k / 3
        fx("Dust1", lake["x"] + lake["rx"] * 1.05 * math.cos(a), lake["y"] + lake["ry"] * 1.05 * math.sin(a), 1.5)
    for x, y, _ in pick_spaced(giants[len(giants) // 2:], 2, 80.0):
        fx("Dust2", x, y, 2.0)

    if tdr.ENABLED:
        add_outer_ring(L, mat, STARFIELD_ABYSS_LAYERS, STARFIELD_ABYSS_INTERIOR_RANGE)

    place_common_markers(L, cfg)


def outer_ring_prefix(L):
    return L.label("OuterRing_")


def add_outer_ring(L, mat, ring_layers, interior_range):
    for layer_i, (z_m, material) in enumerate(ring_layers):
        for i in CLOUD_RING_INDEX_RANGE:
            for j in CLOUD_RING_INDEX_RANGE:
                if i in interior_range and j in interior_range:
                    continue
                x, y = CLOUD_LATTICE_ORIGIN_M + CLOUD_TILE_M * i, CLOUD_LATTICE_ORIGIN_M + CLOUD_TILE_M * j
                L.add_material_plane(mat[material], x, y, z_m, 90.0 * ((3 * i + 5 * j) % 4), CLOUD_TILE_M, CLOUD_TILE_M, f"{outer_ring_prefix(L)}{layer_i}_{i}_{j}")


def place_pale(L, rng, f, cfg, shapes, islands, bridges):
    X, Y, h, slope = f["X"], f["Y"], f["h"], f["slope"]
    rid = cfg["realm_id"]
    mat = {name: realm_material_path(rid, name) for name in cfg["materials"]}
    ring = cfg["pillar_ring"]
    c3 = next(c for c in cfg["clearings"] if c["id"] == "C3")
    circles = [(c["x"], c["y"], 20.0) for c in cfg["clearings"]]
    circles += [(g["plaza"][0], g["plaza"][1], 20.0) for g in cfg["gates"]]
    circles += [(ring["x"], ring["y"], 34.0)]
    excl = build_exclusion(f, circles)
    top = f["top"] * (1.0 - smoothstep(20.0, 30.0, slope))
    patch = make_fbm(rng, 9, 3)
    edge_wood = np.zeros(h.shape)
    for s in list(shapes.values()) + islands:
        edge_wood = np.maximum(edge_wood, s.interior(X, Y, 0.0) * (1.0 - s.interior(X, Y, 9.0)))
    forest = np.maximum(smoothstep(-0.2, 0.1, patch), 0.75 * edge_wood) * top * (1.0 - excl)
    f["forest"] = forest
    rough = smoothstep(0.1, 0.4, make_fbm(rng, 18, 2))
    for x, y in scatter_points(rng, forest, 2.5, 3.6, 350):
        k = rng.random()
        key = "Birch1" if k < 0.4 else ("Birch2" if k < 0.75 else "Birch3")
        L.add_ism(key, x, y, rng.uniform(0, 360), rng.uniform(0.9, 1.4), roll=rng.uniform(-3, 3), pitch=rng.uniform(-3, 3), sink=0.12)
    for x, y in scatter_points(rng, np.clip(0.55 + 0.45 * rough, 0, 1) * top * (1.0 - excl), 3.5, 5.0, 250):
        L.add_ism(["Rock1", "Rock2", "Rock3"][int(rng.integers(0, 3))], x, y, rng.uniform(0, 360), rng.uniform(0.4, 1.2), roll=rng.uniform(-8, 8), pitch=rng.uniform(-8, 8), sink=0.25, material_name="PaleRock")
    for x, y in scatter_points(rng, 0.7 * top * (1.0 - excl * 0.6), 2.5, 2.0, 700):
        L.add_ism(f"SRock{int(rng.integers(3, 9))}", x, y, rng.uniform(0, 360), rng.uniform(0.6, 1.6), roll=rng.uniform(-10, 10), sink=0.04, material_name="PaleRock")

    all_shapes = list(shapes.values()) + islands
    perimeter = sum(2.0 * math.pi * s.r for s in all_shapes)
    spacing = perimeter / 285.0
    keys = ["Cliff2", "Cliff3", "Cliff4"]
    for s in all_shapes:
        n = max(6, int(2.0 * math.pi * s.r / spacing))
        for k in range(n):
            theta = 2.0 * math.pi * k / n + rng.uniform(-0.3, 0.3) / n * 2.0 * math.pi
            r, we = float(s.radius(theta)), float(s.rim_width(theta))
            d = r - we * rng.uniform(0.3, 0.7)
            x, y = s.x + d * math.cos(theta), s.y + d * math.sin(theta)
            if any(segment_distance(x, y, *b["a"], *b["b"]) < b["width"] * 0.5 + 5.0 for b in bridges):
                continue
            if is_in_passage_band(f["passages"], x, y):
                continue
            sc = rng.uniform(1.0, 2.5)
            L.add_ism(keys[int(rng.integers(0, 3))], x, y, math.degrees(theta) + rng.uniform(-20, 20), sc, roll=rng.uniform(-12, 12), pitch=rng.uniform(-12, 12), sink=0.8 * sc + rng.uniform(0.0, 1.0), material_name="PaleCliff")

    for k in range(ring["count"]):
        a = 2.0 * math.pi * k / ring["count"]
        x, y = ring["x"] + ring["r"] * math.cos(a), ring["y"] + ring["r"] * math.sin(a)
        L.add_actor("Cliff4", x, y, math.degrees(a) + rng.uniform(-8, 8), scale3=[rng.uniform(0.38, 0.55), rng.uniform(0.38, 0.55), rng.uniform(2.6, 3.8)], z_off=-0.8, materials=[mat["PaleCliff"]], label=L.label(f"RuinPillar_{k}"))
    L.add_actor("Cliff4", ring["x"], ring["y"], 15.0, scale3=[0.9, 0.9, 5.5], z_off=-1.5, materials=[mat["PaleCliff"]], label=L.label("PillarSpire"))
    p4_bridge_angle = math.atan2(10.0 - c3["y"], 0.0 - c3["x"])
    placed = 0
    for k in range(12):
        if placed >= 6:
            break
        a = p4_bridge_angle + math.radians(40.0 + k * 280.0 / 11.0)
        r = min(24.0, shapes["P4"].flat_radius(a) - 3.0)
        x, y = c3["x"] + r * math.cos(a), c3["y"] + r * math.sin(a)
        if k % 2:
            continue
        L.add_actor("Cliff4", x, y, math.degrees(a), scale3=[rng.uniform(0.38, 0.55), rng.uniform(0.38, 0.55), rng.uniform(2.6, 3.8)], z_off=-0.8, materials=[mat["PaleCliff"]], label=L.label(f"LookoutPillar_{placed}"))
        placed += 1
    for x, y in scatter_points(rng, top * (1.0 - excl) * 0.5, 10.0, 15.0, 20):
        L.add_ism("Cliff4", x, y, rng.uniform(0, 360), 1.0, roll=rng.uniform(-10, 10), pitch=rng.uniform(-10, 10), sink=0.5, scale3=[rng.uniform(0.8, 1.2), rng.uniform(0.8, 1.2), rng.uniform(1.0, 2.5)], material_name="PaleCliff", variant="Cliff4_BrokenRuin")

    beam_spots = [(ring["x"], ring["y"] - 6.0, 10.0), (c3["x"], c3["y"], 8.0)] + [(c["x"], c["y"], 7.0) for c in cfg["clearings"] if c["id"] != "C3"]
    for i, (x, y, s) in enumerate(beam_spots):
        L.add_actor("LightBeam", x, y, 0.0, s, z_off=s * 0.62 + 2.5, materials=[mat["GoldBeam"]], label=L.label(f"Beam_{i}"))
    for x, y in scatter_points(rng, forest * 0.8, 18.0, 40.0, 7):
        s = rng.uniform(5.0, 8.0)
        L.add_ism("LightBeam", x, y, rng.uniform(0, 360), s, pitch=rng.uniform(-6, 6), roll=rng.uniform(-6, 6), z_off=s * 0.62 + 2.5, material_name="GoldBeam")

    light_i = 0

    def light(x, y, z_off, intensity, radius_m):
        nonlocal light_i
        L.add_light("Point", x, y, z_off, intensity, WARM_GOLD, radius_m, L.label(f"Light_{light_i}"), source_radius=30.0, volumetric=1.5, cast_shadows=False)
        light_i += 1

    light(ring["x"] + 6.0, ring["y"], 6.0, 70.0, 18.0)
    light(ring["x"] - 6.0, ring["y"], 6.0, 70.0, 18.0)
    for k in (1, 6):
        a = 2.0 * math.pi * (k + 0.5) / ring["count"]
        light(ring["x"] + ring["r"] * math.cos(a), ring["y"] + ring["r"] * math.sin(a), 3.0, 50.0, 14.0)
    light(c3["x"] + 8.0, c3["y"], 3.0, 50.0, 15.0)
    light(c3["x"] - 8.0, c3["y"], 3.0, 50.0, 15.0)
    for c in cfg["clearings"]:
        if c["id"] != "C3":
            light(c["x"], c["y"] + 10.0, 3.0, 50.0, 15.0)
    for b in bridges[:3]:
        mx, my = (b["a"][0] + b["b"][0]) * 0.5, (b["a"][1] + b["b"][1]) * 0.5
        light(mx, my, 2.5, 40.0, 14.0)

    fx_i = 0
    for b in bridges:
        for t in (0.3, 0.7):
            x, y = b["a"][0] + (b["b"][0] - b["a"][0]) * t, b["a"][1] + (b["b"][1] - b["a"][1]) * t
            L.add_niagara("Leaves", x, y, 6.0, L.label(f"FX_Leaves_{fx_i}"))
            fx_i += 1
    for s in [shapes[n] for n in ("P1", "P2", "P3", "P4", "P0")]:
        theta = rng.uniform(0, 2 * math.pi)
        d = s.flat_radius(theta) - 2.0
        L.add_niagara("Leaves", s.x + d * math.cos(theta), s.y + d * math.sin(theta), 6.0, L.label(f"FX_Leaves_{fx_i}"))
        fx_i += 1

    cloud_layers = ((-18.0, "Cloud"), (-28.0, "CloudLow"))
    for layer_i, (z_m, material) in enumerate(cloud_layers):
        for i in range(6):
            for j in range(6):
                x, y = -250.0 + 100.0 * i, -250.0 + 100.0 * j
                L.add_material_plane(mat[material], x, y, z_m, 90.0 * int(rng.integers(0, 4)), 100.0, 100.0, L.label(f"Cloud_{layer_i}_{i}_{j}"))
    if tdr.ENABLED:
        add_outer_ring(L, mat, cloud_layers, range(0, CLOUD_INTERIOR_COUNT))

    place_common_markers(L, cfg)


def load_field(main_dir):
    layout_path = os.path.join(main_dir, "layout.json")
    height_path = os.path.join(main_dir, "height.r16")
    if not os.path.isfile(layout_path) or not os.path.isfile(height_path):
        raise TDInputError(f"main outputs missing in {main_dir} (run generate_ashen_vale.py first)")
    with open(layout_path, encoding="utf-8") as fp:
        layout = json.load(fp)
    raw = np.fromfile(height_path, dtype="<u2")
    if raw.size != gav.SIZE * gav.SIZE:
        raise TDInputError(f"main height.r16 size {raw.size} != {gav.SIZE}^2")
    units = raw.reshape(gav.SIZE, gav.SIZE).astype(np.float64)
    entries = {p["gate_id"]: p for p in layout.get("passages", [])}
    missing = [p["gate_id"] for p in rp.PASSAGES if p["gate_id"] not in entries]
    if missing:
        raise TDInputError(f"main layout.json has no passages for {missing} (rerun generate_ashen_vale.py with realm_passages)")
    field = {"markers": {m["label"]: m for m in layout["markers"]}, "units": units, "height_m": (units - 32768.0) / gav.UNITS_PER_METER}
    field["passages"] = {gate_id: field_passage_info(field, entry) for gate_id, entry in entries.items()}
    return field


def field_passage_info(field, entry):
    passage = rp.PASSAGES_BY_GATE.get(entry["gate_id"])
    if passage is None:
        raise TDInputError(f"main layout passage {entry['gate_id']} is not in realm_passages.PASSAGES")
    seam_x, seam_y = passage["field_seam_m"]
    z0 = float(field["height_m"][int(seam_y) + gav.HALF, int(seam_x) + gav.HALF])
    if abs(z0 - float(entry["seam_floor_z_m"])) > 0.5 / gav.UNITS_PER_METER:
        raise TDInputError(f"{entry['gate_id']}: field height.r16 seam floor {z0} != layout {entry['seam_floor_z_m']} (field outputs out of sync)")
    if abs(float(entry["approach_length_cm"]) - passage["approach_m"] * 100.0) > 0.5:
        raise TDInputError(f"{entry['gate_id']}: field approach {entry['approach_length_cm']} cm != realm_passages {passage['approach_m']} m")
    return {"z0": z0, "mouth_z": float(entry["mouth_z_m"]), "approach_length_cm": float(entry["approach_length_cm"])}


def cm_point(x_m, y_m, z_m):
    return [round(float(x_m) * 100.0, 1), round(float(y_m) * 100.0, 1), round(float(z_m) * 100.0, 1)]


def passage_prop_entries(props, passage, side, z0, label_prefix, mouth_z=None, plaza_z=None):
    s_start, s_end = rp.default_span(passage, side)
    out = {"actors": [], "planes": [], "lights": []}
    for prop in props:
        x_m, y_m = rp.to_grid_m(passage, side, prop["s"], prop["l"])
        z_m = float(rp.floor_height(passage, prop["s"], z0, s_start, s_end, mouth_z, plaza_z)) + prop["z_rel"]
        yaw = round(passage["yaw_deg"] + prop["yaw_offset"], 2)
        label = f"{label_prefix}{prop['name']}"
        scale = [round(v, 3) for v in prop["scale"]]
        if prop["kind"] == "mesh":
            z_m += gav.MESH_BASE_BELOW_PIVOT.get(prop["key"], 0.0) * prop["scale"][2]
            out["actors"].append({"mesh": prop["mesh"], "loc": cm_point(x_m, y_m, z_m), "rot": [round(prop["pitch"], 3), yaw, round(prop["roll"], 3)], "scale": scale, "label": label})
            continue
        if prop["kind"] == "plane":
            out["planes"].append({"mesh": prop["mesh"], "material": prop["material"], "loc": cm_point(x_m, y_m, z_m), "rot": [round(prop["pitch"], 3), yaw, round(prop["roll"], 3)], "scale": scale, "label": label})
            continue
        if prop["kind"] != "light":
            raise TDInputError(f"{label}: unknown passage prop kind {prop['kind']}")
        out["lights"].append({"type": prop["light_type"], "loc": cm_point(x_m, y_m, z_m), "rot": [-90.0, 0.0, 0.0], "intensity": prop["intensity"], "color": prop["color"],
                              "radius": round(prop["radius_m"] * 100.0, 1), "label": label, "source_radius": prop["source_radius"], "volumetric": prop["volumetric"],
                              "cast_shadows": bool(prop["cast_shadows"])})
    return out


def clear_passage_items(L, carved):
    removed = 0
    for bucket_key in list(L.ism):
        bucket = L.ism[bucket_key]
        inside = is_in_passage_band(carved, np.array([t[0] for t in bucket]) / 100.0, np.array([t[1] for t in bucket]) / 100.0)
        kept = [t for t, hit in zip(bucket, inside) if not hit]
        removed += len(bucket) - len(kept)
        if kept:
            L.ism[bucket_key] = kept
            continue
        del L.ism[bucket_key]
    for name in ("actors", "planes", "lights", "niagara"):
        items = getattr(L, name)
        kept = [e for e in items if not bool(is_in_passage_band(carved, e["loc"][0] / 100.0, e["loc"][1] / 100.0))]
        removed += len(items) - len(kept)
        setattr(L, name, kept)
    return removed


def add_realm_passage_props(L, carved):
    for c in carved:
        entries = passage_prop_entries(rp.seam_zone_props(c["passage"]), c["passage"], "realm", c["z0"], L.label(f"Passage_{c['gate_id']}_"), plaza_z=c["plaza_z"])
        L.actors.extend(entries["actors"])
        L.planes.extend(entries["planes"])
        L.lights.extend(entries["lights"])


def build_field_passage_props(carved, field):
    out = {"actors": [], "planes": [], "lights": []}
    for c in carved:
        info = field["passages"][c["gate_id"]]
        props = rp.seam_zone_props(c["passage"]) + rp.field_passage_dressing(c["passage"])
        entries = passage_prop_entries(props, c["passage"], "field", info["z0"], f"{FIELD_PASSAGE_LABEL_PREFIX}{c['gate_id']}_", mouth_z=info["mouth_z"])
        for key, items in out.items():
            items.extend(entries[key])
    return out


def build_gate_entries(cfg, carved, field):
    cx, cy, cz = cfg["center_cm"]
    entries = []
    for c in carved:
        passage = c["passage"]
        info = field["passages"][c["gate_id"]]
        yaw = round(passage["yaw_deg"], 4)
        rx, ry = passage["realm_seam_local_m"]
        fx, fy = passage["field_seam_m"]
        arrival = [cx + rx * 100.0, cy + ry * 100.0, cz + c["z0"] * 100.0 + PAWN_HEIGHT_CM]
        field_return = [fx * 100.0, fy * 100.0, info["z0"] * 100.0 + PAWN_HEIGHT_CM]
        entries.append({
            "gate_id": c["gate_id"],
            "field_marker_label": passage["field_marker_label"],
            "kind": passage["kind"],
            "grade": passage["grade"],
            "arrival": {"loc": arrival, "rot": [0.0, yaw, 0.0]},
            "field_return": {"loc": field_return, "rot": [0.0, yaw, 0.0]},
            "approach_length_cm": info["approach_length_cm"],
            "departure_length_cm": round(passage["front_m"] * 100.0, 1),
            "seam_buffer_cm": rp.SEAM_BUFFER_M * 100.0,
            "passage_half_width_cm": rp.WALL_TOP_OUTER_M * 100.0,
            "preload_lead_cm": PRELOAD_LEAD_CM,
            "seam_offset_cm": [arrival[i] - field_return[i] for i in range(3)],
            "realm_seam_floor_z_m": c["z0"],
            "field_seam_floor_z_m": info["z0"],
            "plaza_local_m": list(c["plaza"]),
            "plaza_z_m": round(c["plaza_z"], 4),
        })
    return entries


def disk_indices(x, y, r):
    ix0, ix1 = max(0, int(math.floor(x + HALF - r))), min(SIZE - 1, int(math.ceil(x + HALF + r)))
    iy0, iy1 = max(0, int(math.floor(y + HALF - r))), min(SIZE - 1, int(math.ceil(y + HALF + r)))
    ys, xs = np.mgrid[iy0:iy1 + 1, ix0:ix1 + 1]
    inside = (xs - HALF - x) ** 2 + (ys - HALF - y) ** 2 <= r * r
    return ys[inside], xs[inside]


def make_check(check_id, name, passed, value, limit, detail=None):
    return {"id": check_id, "name": name, "passed": bool(passed), "value": value, "limit": limit, "detail": detail or {}}


def flood_component(walkable, x, y):
    labels, _ = ndimage.label(walkable)
    iy, ix = int(round(y + HALF)), int(round(x + HALF))
    return labels, int(labels[iy, ix])


def reached(labels, comp, x, y, r=2.5):
    if comp == 0:
        return False
    ys, xs = disk_indices(x, y, r)
    return bool(np.any(labels[ys, xs] == comp))


def box_gap_cm(center_a, half_a, center_b, half_b):
    gaps = [abs(center_a[i] - center_b[i]) - (half_a[i] + half_b[i]) for i in range(2)]
    return max(gaps), gaps


def yaw_difference_deg(a, b):
    d = (a - b) % 360.0
    return min(d, 360.0 - d)


def check_flatness(f, carved):
    X, Y, slope = f["X"], f["Y"], f["slope"]
    wall_zone = ndimage.binary_dilation((f["passage_footprint"] - f["passage_floor"]) > 0.01, iterations=1)
    detail = {}
    for c in carved:
        s, l = rp.passage_coords(c["passage"], "realm", X, Y)
        floor_cells = (np.abs(l) <= PASSAGE_FLOOR_HALF_CHECK_M) & (s >= c["s_start"] + 1.5) & (s <= c["s_end"] - 1.0)
        detail[f"Floor_{c['gate_id']}"] = round(float(slope[floor_cells].max()), 2)
        ys, xs = disk_indices(c["plaza"][0], c["plaza"][1], GATE_FLAT_RADIUS_M)
        open_cells = ~wall_zone[ys, xs]
        detail[f"Plaza_{c['gate_id']}"] = round(float(slope[ys[open_cells], xs[open_cells]].max()), 2)
    worst = max(detail.values())
    return make_check("a", "passage_floor_and_plaza_r14m_flatness_deg", worst <= GATE_MAX_SLOPE_DEG, worst, f"<= {GATE_MAX_SLOPE_DEG} (plaza excludes passage walls)", detail)


def check_seam_floods(cfg, f, walkable, carved):
    targets = {}
    for c in carved:
        targets[f"Seam_{c['gate_id']}"] = seam_start_local(c["passage"])
        targets[f"Plaza_{c['gate_id']}"] = c["plaza"]
    targets.update({f"Clearing_{c['id']}": (c["x"], c["y"]) for c in cfg["clearings"]})
    targets.update({f"Landmark_{lm['name']}": (lm["x"], lm["y"]) for lm in cfg["landmarks"]})
    detail = {}
    n_ok = 0
    for c in carved:
        labels, comp = flood_component(walkable, *seam_start_local(c["passage"]))
        missing = [name for name, (x, y) in targets.items() if not reached(labels, comp, x, y)]
        detail[c["gate_id"]] = {"reached": len(targets) - len(missing), "missing": missing}
        n_ok += len(targets) - len(missing)
    n_total = len(targets) * len(carved)
    return make_check("c", "flood_from_each_seam_s+1m_reaches_targets", n_ok == n_total, f"{n_ok}/{n_total}", "all", detail)


def check_seam_geometry(f, carved_by_gate, field, gates):
    detail = {}
    passed = True
    for g in gates:
        passage = rp.PASSAGES_BY_GATE[g["gate_id"]]
        c = carved_by_gate[g["gate_id"]]
        marker = field["markers"].get(passage["field_marker_label"])
        marker_outward = rp.normalize_yaw(float(marker["rot"][1]) + 180.0) if marker else None
        plaza_s, plaza_l = rp.passage_coords(passage, "realm", c["plaza"][0], c["plaza"][1])
        yaw_errors = [yaw_difference_deg(g["arrival"]["rot"][1], passage["yaw_deg"]), yaw_difference_deg(g["field_return"]["rot"][1], passage["yaw_deg"])]
        if marker_outward is not None:
            yaw_errors.append(yaw_difference_deg(marker_outward, passage["yaw_deg"]))
        item = {"realm_seam_m": list(passage["realm_seam_local_m"]), "field_seam_m": list(passage["field_seam_m"]),
                "integers": rp.is_integer_point(passage["realm_seam_local_m"]) and rp.is_integer_point(passage["field_seam_m"]),
                "edge_distance_m": passage["realm_edge_distance_m"], "marker_found": marker is not None, "max_yaw_error_deg": round(max(yaw_errors), 4),
                "plaza_s_m": round(float(plaza_s), 2), "plaza_l_m": round(float(plaza_l), 2), "front_m": passage["front_m"]}
        ok = item["integers"] and item["edge_distance_m"] >= rp.MIN_REALM_EDGE_DISTANCE_M and item["marker_found"] and item["max_yaw_error_deg"] <= 0.01
        ok = ok and plaza_s >= passage["front_m"] and abs(plaza_l) <= 2.0
        detail[g["gate_id"]] = item
        passed = passed and ok
    return make_check("g", "seam_integer_edge_ge35m_yaw_match_plaza_on_axis", passed, len(detail), "all gates", detail)


def check_core_shape(f, carved, field):
    X, Y = f["X"], f["Y"]
    realm_units = np.round(32768.0 + f["h"] * gav.UNITS_PER_METER)
    detail = {}
    worst_all = 0.0
    for c in carved:
        passage = c["passage"]
        s, l = rp.passage_coords(passage, "realm", X, Y)
        iy, ix = np.nonzero((np.abs(s) <= rp.SEAM_BUFFER_M) & (np.abs(l) <= rp.WALL_TOP_OUTER_M))
        rx, ry = (int(v) for v in passage["realm_seam_local_m"])
        fx, fy = (int(v) for v in passage["field_seam_m"])
        field_ix = ix - HALF - rx + fx + gav.HALF
        field_iy = iy - HALF - ry + fy + gav.HALF
        dz_units = round((c["z0"] - field["passages"][c["gate_id"]]["z0"]) * gav.UNITS_PER_METER)
        diff = realm_units[iy, ix] - field["units"][field_iy, field_ix] - dz_units
        worst = float(np.max(np.abs(diff)))
        worst_all = max(worst_all, worst)
        detail[c["gate_id"]] = {"core_cells": int(len(iy)), "max_diff_units": worst, "dz_m": dz_units / gav.UNITS_PER_METER}
    return make_check("h", "core_shape_matches_field_height_r16_units", worst_all <= SHAPE_MAX_DIFF_UNITS, worst_all, f"<= {SHAPE_MAX_DIFF_UNITS} (1/128 m)", detail)


def is_in_any_core(carved, x, y):
    inside = np.zeros(np.shape(x), dtype=bool)
    for c in carved:
        s, l = rp.passage_coords(c["passage"], "realm", x, y)
        inside |= (np.abs(s) <= rp.SEAM_BUFFER_M) & (np.abs(l) <= rp.BAND_OUTER_M)
    return inside


def check_core_clear(L, carved):
    passage_tag = L.label("Passage_")
    count = 0
    for tr in L.ism.values():
        count += int(np.sum(is_in_any_core(carved, np.array([t[0] for t in tr]) / 100.0, np.array([t[1] for t in tr]) / 100.0)))
    for e in L.actors + L.planes + L.lights + L.niagara:
        if e["label"].startswith(passage_tag):
            continue
        count += int(bool(is_in_any_core(carved, e["loc"][0] / 100.0, e["loc"][1] / 100.0)))
    return make_check("i", "core_scatter_count_excluding_passage_props", count == 0, count, "0")


def check_outer_slopes(f, walkable, carved):
    slope = f["slope"]
    detail = {}
    passed = True
    stations = np.arange(-rp.SEAM_BUFFER_M, rp.SEAM_BUFFER_M + 1.0)
    lateral = np.arange(rp.WALL_TOP_OUTER_M + 0.5, rp.BAND_OUTER_M, 0.5)
    S, Lat = np.meshgrid(stations, lateral, indexing="ij")
    for c in carved:
        passage = c["passage"]
        side_minimum = []
        for sign in (-1.0, 1.0):
            gx, gy = rp.to_grid_m(passage, "realm", S, sign * Lat)
            band = sample_bilinear(slope, gx.ravel(), gy.ravel()).reshape(S.shape)
            side_minimum.append(float(band.max(axis=1).min()))
        labels, comp = flood_component(walkable, *seam_start_local(passage))
        s, l = rp.passage_coords(passage, "realm", f["X"], f["Y"])
        wall_top = (np.abs(s) <= rp.SEAM_BUFFER_M) & (np.abs(l) >= rp.WALL_OUTER_M + 0.5) & (np.abs(l) <= rp.WALL_TOP_OUTER_M - 0.5)
        reachable_top = int(np.sum(labels[wall_top] == comp)) if comp else 0
        minimum = min(side_minimum)
        detail[c["gate_id"]] = {"outer_band_min_of_max_slope_deg": round(minimum, 2), "wall_top_cells": int(wall_top.sum()), "wall_top_reachable_from_floor": reachable_top}
        passed = passed and minimum >= OUTER_SLOPE_MIN_DEG and reachable_top == 0
    worst = min(v["outer_band_min_of_max_slope_deg"] for v in detail.values())
    return make_check("j", "core_outer_band_slope_and_wall_top_unreachable", passed, worst, f">= {OUTER_SLOPE_MIN_DEG} deg and 0 wall-top cells reachable", detail)


def check_prop_mirror(cfg, L, gates, field_props):
    cx, cy, cz = cfg["center_cm"]
    realm_tag = L.label("Passage_")
    realm_props = {e["label"][len(realm_tag):]: e for e in L.actors + L.planes + L.lights if e["label"].startswith(realm_tag)}
    field_items = field_props["actors"] + field_props["planes"] + field_props["lights"]
    field_by_key = {e["label"][len(FIELD_PASSAGE_LABEL_PREFIX):]: e for e in field_items}
    detail = {}
    worst = 0.0
    for g in gates:
        offset = g["seam_offset_cm"]
        gate_pairs = 0
        gate_missing = 0
        gate_worst = 0.0
        for key, realm_entry in realm_props.items():
            if not key.startswith(g["gate_id"] + "_"):
                continue
            field_entry = field_by_key.get(key)
            if field_entry is None:
                gate_missing += 1
                continue
            realm_world = (realm_entry["loc"][0] + cx, realm_entry["loc"][1] + cy, realm_entry["loc"][2] + cz)
            gate_worst = max(gate_worst, max(abs(realm_world[i] - field_entry["loc"][i] - offset[i]) for i in range(3)))
            gate_worst = max(gate_worst, max(abs(realm_entry["rot"][i] - field_entry["rot"][i]) for i in range(3)))
            gate_pairs += 1
        detail[g["gate_id"]] = {"pairs": gate_pairs, "missing_field_copies": gate_missing, "max_error_cm_or_deg": round(gate_worst, 4)}
        worst = max(worst, gate_worst)
    passed = all(v["pairs"] > 0 and v["missing_field_copies"] == 0 for v in detail.values()) and worst <= PROP_MIRROR_TOLERANCE_CM
    return make_check("k", "seam_props_realm_copy_equals_field_copy_plus_offset", passed, round(worst, 4), f"<= {PROP_MIRROR_TOLERANCE_CM}", detail)


def check_field_copy_floor(carved, field):
    detail = {}
    worst = 0.0
    for c in carved:
        passage = c["passage"]
        info = field["passages"][c["gate_id"]]
        s_start, s_end = rp.default_span(passage, "field")
        s_values = np.array([p["s"] for p in rp.seam_zone_props(passage) + rp.field_passage_dressing(passage)])
        gx, gy = rp.to_grid_m(passage, "field", s_values, np.zeros_like(s_values))
        sampled = gav.sample_bilinear(field["height_m"], gx, gy)
        analytic = rp.floor_height(passage, s_values, info["z0"], s_start, s_end, mouth_z=info["mouth_z"])
        gate_worst = float(np.max(np.abs(sampled - analytic)))
        detail[c["gate_id"]] = {"props": int(len(s_values)), "max_floor_diff_m": round(gate_worst, 4)}
        worst = max(worst, gate_worst)
    return make_check("l", "field_copy_floor_matches_field_height_r16_m", worst <= FIELD_FLOOR_TOLERANCE_M, round(worst, 4), f"<= {FIELD_FLOOR_TOLERANCE_M}", detail)


def validate_realm(cfg, f, L, islands, field, gates, field_props):
    rid = cfg["realm_id"]
    h, slope = f["h"], f["slope"]
    carved = f["passages"]
    carved_by_gate = {c["gate_id"]: c for c in carved}
    checks = [check_flatness(f, carved)]

    road_mask = f["road_only_layer"] >= 0.5
    road_max = round(float(slope[road_mask].max()), 2)
    iy, ix = np.unravel_index(int(np.argmax(np.where(road_mask, slope, -1.0))), slope.shape)
    checks.append(make_check("b", "road_max_slope_deg", road_max <= ROAD_MAX_SLOPE_DEG, road_max, f"<= {ROAD_MAX_SLOPE_DEG}",
                             {"road_cells": int(road_mask.sum()), "worst_local_m": [int(ix) - HALF, int(iy) - HALF]}))
    if f["ramps"]:
        ramp_detail = {}
        for ramp in f["ramps"]:
            ts = np.linspace(0.05, 0.95, 40)
            xs_ = ramp["start"][0] + ramp["u"][0] * ramp["length"] * ts
            ys_ = ramp["start"][1] + ramp["u"][1] * ramp["length"] * ts
            ramp_detail[ramp["name"]] = round(float(sample_bilinear(slope, xs_, ys_).max()), 2)
        ramp_worst = max(ramp_detail.values())
        checks.append(make_check("b2", "terrace_ramp_centerline_max_slope_deg", ramp_worst <= RAMP_MAX_SLOPE_DEG, ramp_worst, f"<= {RAMP_MAX_SLOPE_DEG}", ramp_detail))

    walkable = slope < WALK_MAX_SLOPE_DEG
    if rid == "SunkenStarfield":
        walkable &= h > SUNKEN_WATER_Z_M - 1.2
    checks.append(check_seam_floods(cfg, f, walkable, carved))
    start = seam_start_local(carved[0]["passage"])
    labels, comp = flood_component(walkable, *start)
    if islands:
        island_detail = {s.name: reached(labels, comp, s.x, s.y, 3.0) for s in islands}
        checks.append(make_check("c2", "islands_unreachable", not any(island_detail.values()), sum(island_detail.values()), "0", island_detail))

    if rid == "SunkenStarfield":
        barrier_labels, barrier_comp = flood_component(slope <= 50.0, *start)
        leak = bool(np.any(barrier_labels[f["edge_d"] <= 3.0] == barrier_comp))
        band = f["wall"] > 0.05
        checks.append(make_check("d", "outer_wall_blocks_slope_le_50deg_flood", not leak, int(leak), "no rim cell reachable",
                                 {"wall_band_cells_over_50deg": int((band & (slope > 50.0)).sum()), "wall_band_max_slope_deg": round(float(slope[band].max()), 2)}))
    else:
        barrier_labels, barrier_comp = flood_component(slope <= 55.0, *start)
        leak = bool(np.any(barrier_labels[h < -30.0] == barrier_comp))
        rim = (f["top"] < 0.5) & (h > -30.0)
        checks.append(make_check("d", "mesa_edges_block_slope_le_55deg_flood", not leak, int(leak), "no floor cell reachable",
                                 {"rim_cells_over_55deg": int((rim & (slope > 55.0)).sum())}))

    min_gap = LOADING_RANGE_CM + GRID_CELL_CM
    others = {"Field": ((0.0, 0.0), (FIELD_HALF_CM, FIELD_HALF_CM)), "AtlasBand": (ATLAS_BAND_CENTER_CM, ATLAS_BAND_HALF_CM)}
    for other_id, other_cfg in REALM_CONFIGS.items():
        if other_id != rid:
            others[other_id] = (tuple(other_cfg["center_cm"][:2]), tuple(REALM_HALF_EXTENT_CM[:2]))
    spacing_detail = {}
    spacing_ok = True
    for name, (oc, oh) in others.items():
        gap, gaps = box_gap_cm(cfg["center_cm"], REALM_HALF_EXTENT_CM, oc, oh)
        spacing_detail[name] = {"best_axis_gap_cm": round(gap, 1), "axis_gaps_cm": [round(v, 1) for v in gaps]}
        spacing_ok = spacing_ok and gap >= min_gap
    checks.append(make_check("e", "realm_box_spacing_cm", spacing_ok, min(v["best_axis_gap_cm"] for v in spacing_detail.values()), f">= {min_gap}", spacing_detail))

    outside = 0
    for e in L.actors + L.planes + L.lights + L.niagara + L.markers:
        if e.get("label", "").startswith(outer_ring_prefix(L)):
            continue
        outside += int(abs(e["loc"][0]) > HALF * 100.0 or abs(e["loc"][1]) > HALF * 100.0)
    for tr in L.ism.values():
        outside += sum(int(abs(t[0]) > HALF * 100.0 or abs(t[1]) > HALF * 100.0) for t in tr)
    checks.append(make_check("f", "placements_inside_realm_box", outside == 0, outside, "0"))
    checks.append(check_seam_geometry(f, carved_by_gate, field, gates))
    checks.append(check_core_shape(f, carved, field))
    checks.append(check_core_clear(L, carved))
    checks.append(check_outer_slopes(f, walkable, carved))
    checks.append(check_prop_mirror(cfg, L, gates, field_props))
    checks.append(check_field_copy_floor(carved, field))
    return {"passed": all(c["passed"] for c in checks), "checks": checks}


def shift_to_world(L, center_cm):
    cx, cy, cz = center_cm
    for e in L.actors + L.planes + L.lights + L.niagara + L.markers:
        e["loc"] = [round(e["loc"][0] + cx, 1), round(e["loc"][1] + cy, 1), round(e["loc"][2] + cz, 1)]
    for tr in L.ism.values():
        for t in tr:
            t[0] = round(t[0] + cx, 1)
            t[1] = round(t[1] + cy, 1)
            t[2] = round(t[2] + cz, 1)


def summarize(L, layers, f, field_props, cleared):
    by_key = {}
    for (key, _, _), tr in L.ism.items():
        by_key[key] = by_key.get(key, 0) + len(tr)
    return {
        "ism_instances_by_key": dict(sorted(by_key.items())),
        "ism_instances_total": sum(by_key.values()),
        "ism_cells": len(L.ism),
        "actors": len(L.actors), "planes": len(L.planes), "lights": len(L.lights), "niagara": len(L.niagara), "markers": len(L.markers),
        "field_passage_props": {key: len(items) for key, items in field_props.items()},
        "passage_items_cleared": cleared,
        "layer_ratio_percent": {name: round(float(layers[name].mean()) * 100.0, 2) for name in LAYER_NAMES},
        "height_range_m": [round(float(f["h"].min()), 2), round(float(f["h"].max()), 2)],
    }


def build_layout(cfg, seed, out_dir, L, gates, field_props, validation, summary):
    rid = cfg["realm_id"]
    cx, cy, cz = cfg["center_cm"]
    hx, hy, _ = REALM_HALF_EXTENT_CM
    ism_out = []
    for (key, ix, iy), tr in L.ism.items():
        mesh_key, material_name = L.ism_meta[key]
        entry = {"mesh": gav.M[mesh_key], "key": key, "cell": [ix, iy], "transforms": tr}
        if material_name:
            entry["material"] = realm_material_path(rid, material_name)
        if tdr.ENABLED and tdr.collision_for_asset(gav.M[mesh_key]):
            entry["collision"] = "none"
        ism_out.append(entry)
    pp = cfg["post_process"]
    lm = cfg["landscape_material"]
    return {
        "generator_version": GENERATOR_VERSION,
        "seed": seed,
        "landscape": {
            "location": [cx, cy, cz], "scale": [100.0, 100.0, gav.Z_SCALE], "components_x": COMPONENTS, "components_y": COMPONENTS,
            "sections_per_component": 1, "quads_per_section": QUADS_PER_SECTION,
            "heightmap": os.path.join(out_dir, "height.r16"),
            "layers": [{"name": n, "path": os.path.join(out_dir, f"layer_{n}.r8")} for n in LAYER_NAMES],
            "material": f"/Game/World/Landscape/MI_TD_Landscape_{rid}", "world_partition_grid_size": 2,
        },
        "ism": ism_out, "actors": L.actors + field_props["actors"], "planes": L.planes + field_props["planes"], "lights": L.lights + field_props["lights"],
        "niagara": L.niagara, "markers": L.markers,
        "realm": {
            "realm_id": rid,
            "display_name_ko": cfg["display_name_ko"],
            "center_cm": list(cfg["center_cm"]),
            "half_extent_cm": list(REALM_HALF_EXTENT_CM),
            "fall_recovery_height_cm": cfg["fall_recovery_height_cm"],
            "transition_fade_color": cfg["transition_fade_color"],
            "atmosphere": cfg["atmosphere"],
            "post_process": {"priority": pp["priority"], "box_min_cm": [cx - hx, cy - hy, pp["z_cm"][0]], "box_max_cm": [cx + hx, cy + hy, pp["z_cm"][1]], "settings": pp["settings"]},
            "landscape_material": {"asset": f"/Game/World/Landscape/MI_TD_Landscape_{rid}", "parent": LANDSCAPE_PARENT, "vectors": lm["vectors"], "scalars": lm["scalars"]},
            "materials": [{"asset": realm_material_path(rid, name), "parent": parent, "vectors": vectors, "scalars": scalars} for name, (parent, vectors, scalars) in cfg["materials"].items()],
            "gates": gates,
            "seam_mist_density": SEAM_MIST_DENSITY,
            "seam_mist_radius_cm": SEAM_MIST_RADIUS_CM,
            "nav_bounds": {"box_min_cm": [cx - NAV_HALF_CM, cy - NAV_HALF_CM, cfg["nav_z_cm"][0]], "box_max_cm": [cx + NAV_HALF_CM, cy + NAV_HALF_CM, cfg["nav_z_cm"][1]]},
            "validation": validation,
        },
        "summary": summary,
    }


def write_rasters(out_dir, h, layers):
    hu = np.clip(np.round(32768.0 + h * gav.UNITS_PER_METER), 0, 65535).astype("<u2")
    hu.tofile(os.path.join(out_dir, "height.r16"))
    for name in LAYER_NAMES:
        np.clip(np.round(layers[name] * 255.0), 0, 255).astype(np.uint8).tofile(os.path.join(out_dir, f"layer_{name}.r8"))


PREVIEW_PALETTES = {
    "SunkenStarfield": {"Soil": (62, 64, 92), "Moss": (40, 104, 118), "Mud": (48, 42, 66), "Road": (132, 122, 156), "Rock": (80, 80, 102), "Bedrock": (34, 34, 50)},
    "PaleHeights": {"Soil": (168, 186, 138), "Moss": (116, 150, 98), "Mud": (120, 110, 100), "Road": (222, 210, 182), "Rock": (202, 200, 192), "Bedrock": (118, 116, 124)},
}


def write_preview(out_path, cfg, f, layers, L, islands):
    from PIL import Image, ImageDraw, ImageFont
    rid = cfg["realm_id"]
    h = f["h"]
    gy, gx = np.gradient(h)
    shade = np.clip(0.6 + 0.4 * (-gx * 0.7 + gy * 0.7) * 1.5, 0.25, 1.15)
    col = np.zeros((SIZE, SIZE, 3))
    for name, c in PREVIEW_PALETTES[rid].items():
        for i in range(3):
            col[:, :, i] += layers[name] * c[i]
    col *= shade[:, :, None]
    if rid == "SunkenStarfield":
        col[f["water"]] = col[f["water"]] * 0.3 + np.array([18.0, 30.0, 95.0]) * 0.7
    else:
        cloud = h < -18.0
        col[cloud] = col[cloud] * 0.25 + np.array([228.0, 232.0, 242.0]) * 0.75
    img = Image.fromarray(np.clip(col[::-1], 0, 255).astype(np.uint8), "RGB").resize((1024, 1024), Image.BILINEAR)
    s = 1024.0 / SIZE
    dr = ImageDraw.Draw(img)
    try:
        font = ImageFont.load_default(size=15)
        title_font = ImageFont.load_default(size=20)
    except TypeError:
        font = title_font = ImageFont.load_default()

    def px(x, y):
        return ((x + HALF + 0.5) * s, (HALF - y + 0.5) * s)

    for poly in f["road_polys"].values():
        dr.line([px(float(p[0]), float(p[1])) for p in poly[::4]], fill=(250, 226, 150), width=2)
    ism_colors = {"GlowCyan": (90, 240, 255), "GlowViolet": (200, 130, 255)}
    for (key, _, _), tr in L.ism.items():
        mesh_key, material_name = L.ism_meta[key]
        if mesh_key.startswith("Birch"):
            c, rad = (245, 245, 235), 1.2
        elif material_name in ism_colors and mesh_key.startswith("Shroom"):
            c, rad = ism_colors[material_name], 2.2
        elif key.endswith("StarPillar") or key == "Cliff4_BrokenRuin":
            c, rad = (150, 150, 165) if rid == "SunkenStarfield" else (250, 250, 245), 2.6
        else:
            continue
        for t in tr:
            x, y = px(t[0] / 100.0, t[1] / 100.0)
            dr.ellipse((x - rad, y - rad, x + rad, y + rad), fill=c)
    for a in L.actors:
        if "Pillar" in a["label"] or "Spire" in a["label"]:
            x, y = px(a["loc"][0] / 100.0, a["loc"][1] / 100.0)
            dr.rectangle((x - 3, y - 3, x + 3, y + 3), fill=(235, 235, 245), outline=(20, 20, 30))
    for e in L.lights:
        x, y = px(e["loc"][0] / 100.0, e["loc"][1] / 100.0)
        dr.ellipse((x - 3, y - 3, x + 3, y + 3), outline=(255, 230, 90))
    for cl in cfg["clearings"]:
        x, y = px(cl["x"], cl["y"])
        r = 20.0 * s
        dr.ellipse((x - r, y - r, x + r, y + r), outline=(255, 70, 70), width=2)
        dr.text((x + r * 0.72, y - r * 0.95), f"{cl['id']} {cl['name']}", fill=(255, 200, 200), font=font, stroke_width=2, stroke_fill=(0, 0, 0))
    for lm in cfg["landmarks"]:
        x, y = px(lm["x"], lm["y"])
        dr.polygon([(x, y - 7), (x + 7, y), (x, y + 7), (x - 7, y)], outline=(255, 220, 60), fill=(255, 200, 40))
        dr.text((x + 9, y + 4), f"LM {lm['name']}", fill=(255, 235, 140), font=font, stroke_width=2, stroke_fill=(0, 0, 0))
    for c in f["passages"]:
        draw_passage(dr, px, s, c, font)
    for isl in islands:
        x, y = px(isl.x, isl.y)
        dr.text((x - 12, y - 6), "isl", fill=(90, 90, 110), font=font)
    dr.text((10, 8), f"{rid} seed {L.seed}  504m  +X right / +Y up", fill=(255, 255, 255), font=title_font, stroke_width=2, stroke_fill=(0, 0, 0))
    dr.text((10, 34), "magenta=passage B (seam line, walls 16m)  green=plaza(r14)  red=clearing r20  yellow=landmark  lines=road  rings=lights", fill=(230, 230, 230), font=font, stroke_width=2, stroke_fill=(0, 0, 0))
    img.save(out_path)


def draw_passage(dr, px, s, c, font):
    passage = c["passage"]
    half = rp.WALL_TOP_OUTER_M
    corners = [(c["s_start"], -half), (c["s_end"], -half), (c["s_end"], half), (c["s_start"], half)]
    dr.polygon([px(*rp.to_grid_m(passage, "realm", cs, cl)) for cs, cl in corners], outline=(255, 60, 220))
    floor = [(c["s_start"], -rp.FLOOR_HALF_WIDTH_M), (c["s_end"], -rp.FLOOR_HALF_WIDTH_M), (c["s_end"], rp.FLOOR_HALF_WIDTH_M), (c["s_start"], rp.FLOOR_HALF_WIDTH_M)]
    dr.polygon([px(*rp.to_grid_m(passage, "realm", cs, cl)) for cs, cl in floor], outline=(255, 170, 240))
    dr.line([px(*rp.to_grid_m(passage, "realm", 0.0, -half)), px(*rp.to_grid_m(passage, "realm", 0.0, half))], fill=(255, 60, 220), width=4)
    r14 = GATE_FLAT_RADIUS_M * s
    bx, by = px(*c["plaza"])
    dr.ellipse((bx - r14, by - r14, bx + r14, by + r14), outline=(120, 255, 140), width=2)
    dr.ellipse((bx - 4, by - 4, bx + 4, by + 4), fill=(120, 255, 140))
    dr.text((bx - 90, by + r14 + 2), f"SEAM {c['gate_id']}", fill=(255, 170, 240), font=font, stroke_width=2, stroke_fill=(0, 0, 0))


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as fp:
        for chunk in iter(lambda: fp.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def generate_realm(realm_id, seed, field):
    cfg = REALM_CONFIGS[realm_id]
    out_dir = os.path.join(REALMS_DIR, realm_id)
    os.makedirs(out_dir, exist_ok=True)
    islands = []
    if realm_id == "SunkenStarfield":
        def build_terrain():
            return build_sunken_terrain(cfg, stage_rng(seed, 1))
    else:
        shape_rng = stage_rng(seed, 4)
        shapes = pale_mesa_shapes(cfg, shape_rng)
        bridges = pale_bridge_segments(cfg, shapes)
        islands = pale_islands(cfg, shape_rng, shapes, bridges)

        def build_terrain():
            return build_pale_terrain(cfg, stage_rng(seed, 1), shapes, islands, bridges)
    f = build_terrain()
    f_place = f
    if tdr.ENABLED:
        with tdr.legacy_placement_terrain():
            f_place = build_terrain()
    L = TDRealmLayout(f_place, stage_rng(seed, 3), realm_id)
    if realm_id == "SunkenStarfield":
        place_sunken(L, stage_rng(seed, 3), f_place, cfg)
    else:
        place_pale(L, stage_rng(seed, 3), f_place, cfg, shapes, islands, bridges)
    f["forest"] = f_place["forest"]
    L.f = f
    layers = build_sunken_layers(f, stage_rng(seed, 2)) if realm_id == "SunkenStarfield" else build_pale_layers(f, stage_rng(seed, 2))
    L.seed = seed
    cleared = clear_passage_items(L, f["passages"])
    topdown_report = apply_topdown_rules(L, f, cfg)
    add_realm_passage_props(L, f["passages"])
    gates = build_gate_entries(cfg, f["passages"], field)
    field_props = build_field_passage_props(f["passages"], field)
    validation = validate_realm(cfg, f, L, islands, field, gates, field_props)
    summary = summarize(L, layers, f, field_props, cleared)
    write_rasters(out_dir, f["h"], layers)
    write_preview(os.path.join(out_dir, "preview.png"), cfg, f, layers, L, islands)
    shift_to_world(L, cfg["center_cm"])
    layout = build_layout(cfg, seed, out_dir, L, gates, field_props, validation, summary)
    with open(os.path.join(out_dir, "layout.json"), "w", encoding="utf-8") as fp:
        json.dump(layout, fp, ensure_ascii=False)
    report_realm(realm_id, seed, out_dir, layout)
    write_topdown_report(out_dir, topdown_report)
    return validation["passed"]


def write_topdown_report(out_dir, report):
    if not report:
        return
    with open(os.path.join(out_dir, "topdown_rules_report.json"), "w", encoding="utf-8") as fp:
        json.dump(report, fp, ensure_ascii=False, indent=1, sort_keys=True)
    print("topdown rules:", json.dumps(report, ensure_ascii=False, sort_keys=True))


def report_realm(realm_id, seed, out_dir, layout):
    summary = layout["summary"]
    validation = layout["realm"]["validation"]
    print(f"== {realm_id} seed {seed} -> {out_dir}")
    print(f"height m {summary['height_range_m']}  ism {summary['ism_instances_total']} in {summary['ism_cells']} cells  actors {summary['actors']}  planes {summary['planes']}  lights {summary['lights']}  niagara {summary['niagara']}  markers {summary['markers']}  field_passage_props {summary['field_passage_props']}  passage_items_cleared {summary['passage_items_cleared']}")
    print("ism by key: " + ", ".join(f"{k}={v}" for k, v in summary["ism_instances_by_key"].items()))
    print("layers %: " + ", ".join(f"{k}={v}" for k, v in summary["layer_ratio_percent"].items()))
    for c in validation["checks"]:
        print(f"  [{'PASS' if c['passed'] else 'FAIL'}] {c['id']} {c['name']}: {c['value']} (limit {c['limit']}) {json.dumps(c['detail'], ensure_ascii=False)}")
    for g in layout["realm"]["gates"]:
        print(f"  gate {g['gate_id']}: yaw {g['arrival']['rot'][1]} field_return {g['field_return']['loc']} arrival {g['arrival']['loc']} offset {g['seam_offset_cm']} approach {g['approach_length_cm']} departure {g['departure_length_cm']} plaza {g['plaza_local_m']}")
    names = ["height.r16"] + [f"layer_{n}.r8" for n in LAYER_NAMES] + ["layout.json"]
    for n in names:
        print(f"  sha256 {n} {sha256_of(os.path.join(out_dir, n))}")
    print(f"validation: {'PASS' if validation['passed'] else 'FAIL'}")


def main():
    ap = argparse.ArgumentParser(description="Generate realm regions (SunkenStarfield, PaleHeights).")
    ap.add_argument("--realm", default="all", choices=["SunkenStarfield", "PaleHeights", "all"])
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--no-topdown", action="store_true")
    args = ap.parse_args()
    tdr.ENABLED = not args.no_topdown
    if args.seed is not None and args.seed < 0:
        print(f"input error: --seed must be >= 0 (got {args.seed})", file=sys.stderr)
        return 2
    try:
        field = load_field(MAIN_DIR)
    except TDInputError as e:
        print(f"input error: {e}", file=sys.stderr)
        return 2
    realm_ids = list(REALM_CONFIGS) if args.realm == "all" else [args.realm]
    all_passed = True
    for rid in realm_ids:
        seed = args.seed if args.seed is not None else REALM_CONFIGS[rid]["default_seed"]
        try:
            passed = generate_realm(rid, seed, field)
        except TDInputError as e:
            print(f"input error: {e}", file=sys.stderr)
            return 2
        all_passed = all_passed and passed
    return 0 if all_passed else 1


if __name__ == "__main__":
    sys.exit(main())
