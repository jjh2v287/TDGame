"""에디터 밖 시스템 파이썬에서 실행: 연속 세계(D-45)의 이계 지역(SunkenStarfield·PaleHeights) 504 m 지형·레이어·배치를 realm_passages.REALM_PLACEMENT 자리(필드 옆)에 만든다. 공유 변은 필드 height.r16·레이어를 그대로 복사하고, 45 m 경계 띠·연결로 이계 구간(SW 협곡·도착 단·분지 경사로, NE 바위 다리, SE 출구 슬롯)·연결로 소품(월드 좌표 TDRealm_Connector_<GateId>_*)·관문 gates[]·지형 MI 지역 혼합 값을 쓰고, 필드·다른 이계 상자와 겹치는 내부 구름 판은 자기 상자 안으로 잘라 두고 외곽 고리 판은 생략하며, 검증(j는 엄격 쿼드 홍수 채움)·미리보기까지 한다(P3-19 명세 3.3·6.3·10절).
실행: python Tools/WorldGen/generate_realm.py --realm SunkenStarfield|PaleHeights|all [--seed N] [--no-topdown](탑다운 규칙 끄기, 필드도 같은 옵션으로 만든 출력이어야 함) (PowerShell 또는 Git Bash, numpy·scipy·Pillow 필요, generate_ashen_vale.py v3 출력이 Saved/WorldGen/AshenVale 에 먼저 있어야 함)
출력: Saved/WorldGen/Realms/<RealmId>/{height.r16, layer_<Name>.r8 x6, layout.json(v3 월드 좌표, realm.gates[]·gameplay_data_layer·validation, 검사 h 에 공유 변 K3 수치), preview.png, topdown_rules_report.json} (검증 실패 종료 코드 1, 입력 오류 2)
상태: 현행(P3-19)
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
import topdown_materials as tdm
import topdown_rules as tdr

GENERATOR_VERSION = 3
SIZE = 505
HALF = (SIZE - 1) // 2
COMPONENTS = 8
QUADS_PER_SECTION = 63
LAYER_NAMES = ("Soil", "Moss", "Mud", "Road", "Rock", "Bedrock")
PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REALMS_DIR = os.path.join(PROJECT_DIR, "Saved", "WorldGen", "Realms")
MAIN_DIR = os.path.join(PROJECT_DIR, "Saved", "WorldGen", "AshenVale")
FIELD_HALF_M = rp.FIELD_HALF_M
FIELD_HALF_CM = FIELD_HALF_M * 100.0
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
SUNKEN_WALL_TOE_SLOPE = math.tan(math.radians(56.0))
SUNKEN_WALL_TOE_INSET_M = 2.5
TILE_OVERLAP_TOLERANCE_M2 = 1e-4
GATE_FLAT_RADIUS_M = 14.0
GATE_MAX_SLOPE_DEG = 10.0
ROAD_MAX_SLOPE_DEG = 25.0
RAMP_MAX_SLOPE_DEG = 18.0
WALK_MAX_SLOPE_DEG = 35.0
CONNECTOR_FLOOR_MAX_SLOPE_DEG = 35.0
CONNECTOR_GRADE_TOLERANCE_DEG = 1.0
NAV_HALF_CM = 24800.0
CLEAR_MARGIN_CELLS = 2
PASSAGE_FLOOR_HALF_CHECK_M = 2.5
ENTRY_INSET_M = 1.0
WALL_CHECK_INSET_M = 2.0
FIELD_FLOOR_TOLERANCE_M = 2.0 / gav.UNITS_PER_METER
OUTER_SLOPE_MIN_DEG = 35.0
SUNKEN_RIM_FLOOD_MAX_DEG = 50.0
PALE_RIM_FLOOD_MAX_DEG = 55.0
PALE_FALL_FLOOR_M = -30.0
RIM_CELL_DISTANCE_M = 3.0
OPEN_START_RANGE_M = (6.0, 30.0)
SHARED_BAND_FLAT_M = 5.0
SHARED_TAPER_M = 40.0
SHARED_CHECK_DEPTH = 5
SHARED_STEP_TOLERANCE_M = 0.25
SHARED_SLOPE_TOLERANCE_DEG = 3.0
SHARED_LAYER_TOLERANCE = 0.05
SHARED_ANALYTIC_TOLERANCE_UNITS = 1.0
PROMONTORY_HALF_WIDTH_M = 6.0
PROMONTORY_SIDE_SLOPE = 1.0
PROMONTORY_SIDE_CHECK_M = 3.0
PROMONTORY_MIN_LENGTH_M = 0.01
TERRACE_RADIUS_M = 16.0
PLAZA_LIGHT_FORWARD_M = 6.0
PLAZA_LIGHT_LATERAL_M = 9.0
PLAZA_RADIUS_BY_GATE_M = {"SunkenStarfieldSouthwest": 16.0, "SunkenStarfieldSoutheast": 15.0, "PaleHeightsNortheast": 15.0}
PALE_WATER_LEVEL_M = -50.0
CLOUD_LATTICE_ORIGIN_M = -250.0
CLOUD_TILE_M = 100.0
CLOUD_INTERIOR_COUNT = 6
STARFIELD_ABYSS_LAYERS = ((-6.0, "StarWater"), (4.0, "VioletFog"))
STARFIELD_ABYSS_INTERIOR_RANGE = range(1, CLOUD_INTERIOR_COUNT - 1)
CLOUD_RING_INDEX_RANGE = range(-2, 8)
CONNECTOR_LABEL_PREFIX = "TDRealm_Connector_"
CONNECTOR_MESH_MATERIALS = {"Cliff2": "ConnectorRocks", "Cliff3": "ConnectorRocks", "Cliffs1": "ConnectorRocks", "Cliff4": "ConnectorCliffs2",
                            "SRock5": "ConnectorSmallRocks", "SRock7": "ConnectorSmallRocks"}
LANDSCAPE_ORIGIN_OFFSET_PARAMETER = "LandscapeOriginOffsetMeters"

DFT_MATERIALS = "/Game/DarkFantasyTopDown/Materials"
PARENT_ROCKS = f"{DFT_MATERIALS}/Nature/MI_Rocks"
PARENT_CLIFFS = f"{DFT_MATERIALS}/Nature/MI_Cliffs1"
PARENT_CLIFFS2 = f"{DFT_MATERIALS}/Nature/MI_Cliffs2"
PARENT_SMALL_ROCKS = f"{DFT_MATERIALS}/Nature/MI_SmallRocks"
PARENT_EMISSIVE = f"{DFT_MATERIALS}/Shared/MI_EmissiveRed"
PARENT_DREADPLANTS = f"{DFT_MATERIALS}/Props/MI_Dreadplants"
PARENT_WATER = f"{DFT_MATERIALS}/WaterPlane/MI_WaterPlane"
PARENT_FOG1 = f"{DFT_MATERIALS}/VFX/MI_FogPlane_Var1"
PARENT_FOG2 = f"{DFT_MATERIALS}/VFX/MI_FogPlane_Var2"
PARENT_BEAM = f"{DFT_MATERIALS}/VFX/MI_LightBeamVar2"
MIRROR_ROCKS = tdm.topdown_material_path(PARENT_ROCKS)
MIRROR_CLIFFS1 = tdm.topdown_material_path(PARENT_CLIFFS)
MIRROR_CLIFFS2 = tdm.topdown_material_path(PARENT_CLIFFS2)
MIRROR_SMALL_ROCKS = tdm.topdown_material_path(PARENT_SMALL_ROCKS)
LANDSCAPE_PARENT = "/Game/World/Landscape/M_TD_Landscape"

SUNKEN_CLIFF_TONE = ([0.2, 0.22, 0.4, 1.0], [0.6, 0.65, 0.85, 1.0], 0.6)
SUNKEN_ROCK_TONE = ([0.22, 0.26, 0.42, 1.0], [0.7, 0.75, 0.95, 1.0], 0.55)
PALE_CLIFF_TONE = ([1.0, 1.0, 0.97, 1.0], [1.9, 1.88, 1.85, 1.0], 0.7)
PALE_ROCK_TONE = ([0.95, 0.95, 0.92, 1.0], [2.1, 2.08, 2.0, 1.0], 0.7)


def region_blend_parameters():
    vectors = {tdm.REGION_CENTER_PARAMETER: list(tdm.REGION_DEFAULT_CENTER_CM), tdm.REGION_HALF_EXTENT_PARAMETER: [FIELD_HALF_CM, FIELD_HALF_CM, 0.0, 0.0]}
    scalars = {tdm.REGION_FADE_WIDTH_PARAMETER: tdm.REGION_DEFAULT_FADE_WIDTH_CM, tdm.REGION_STRENGTH_PARAMETER: 1.0}
    return vectors, scalars


def region_rock_material(parent_mirror, tone):
    normal_top, diffuse, desaturation = tone
    vectors, scalars = region_blend_parameters()
    vectors[tdm.realm_twin_parameter("NormalTopColor")] = list(normal_top)
    vectors[tdm.realm_twin_parameter("DiffuseMultiplier")] = list(diffuse)
    scalars[tdm.realm_twin_parameter("Desaturation")] = desaturation
    return parent_mirror, vectors, scalars


def realm_rock_materials(prefix, cliff_tone, rock_tone):
    return {
        f"{prefix}Rock": region_rock_material(MIRROR_ROCKS, rock_tone),
        f"{prefix}Cliff": region_rock_material(MIRROR_CLIFFS1, cliff_tone),
        "ConnectorRocks": region_rock_material(MIRROR_ROCKS, cliff_tone),
        "ConnectorCliffs2": region_rock_material(MIRROR_CLIFFS2, cliff_tone),
        "ConnectorSmallRocks": region_rock_material(MIRROR_SMALL_ROCKS, rock_tone),
    }


SUNKEN_CONFIG = {
    "realm_id": "SunkenStarfield",
    "display_name_ko": "가라앉은 별밭",
    "default_seed": 11,
    "fall_recovery_height_local_cm": -3000.0,
    "transition_fade_color": [0.02, 0.03, 0.08],
    "nav_z_local_cm": [-1000.0, 5000.0],
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
        "priority": 10, "z_local_cm": [-6000.0, 9000.0],
        "settings": {"color_saturation": [0.85, 0.9, 1.15, 1.0], "color_gain": [0.8, 0.88, 1.15, 1.0], "color_contrast": [1.08, 1.08, 1.08, 1.0], "bloom_intensity": 1.4, "vignette_intensity": 0.5, "auto_exposure_bias": 0.6},
    },
    "landscape_material": {"vectors": {"RealmTint": [0.55, 0.62, 0.90, 1.0]}, "scalars": {"RealmBrightness": 0.8}},
    "materials": dict(realm_rock_materials("Dark", SUNKEN_CLIFF_TONE, SUNKEN_ROCK_TONE), **{
        "GlowCyan": (PARENT_DREADPLANTS, {"AmbientColor": [0.15, 0.85, 1.2, 1.0], "ColorMultiplier": [0.3, 0.5, 0.65, 1.0]}, {}),
        "GlowViolet": (PARENT_DREADPLANTS, {"AmbientColor": [0.7, 0.3, 1.2, 1.0], "ColorMultiplier": [0.45, 0.35, 0.65, 1.0]}, {}),
        "ShardGlow": (PARENT_EMISSIVE, {"Color": [0.05, 0.02, 0.12, 1.0]}, {}),
        "StarWater": (PARENT_WATER, {"ColorGradient1": [0.01, 0.015, 0.05, 1.0], "ColorGradient2": [0.04, 0.035, 0.14, 1.0], "ShoreColor": [0.12, 0.14, 0.32, 1.0]}, {}),
        "VioletFog": (PARENT_FOG2, {"SandColor": [0.55, 0.45, 0.95, 1.0]}, {"FinalOpacityTweak": 0.1}),
        "PaleBeam": (PARENT_BEAM, {"Color": [0.55, 0.75, 1.0, 1.0]}, {}),
    }),
    "lake": {"x": 10.0, "y": 30.0, "rx": 70.0, "ry": 50.0, "depth": 4.5},
    "channels": {
        "West": [(-58.0, 42.0), (-82.0, 66.0), (-108.0, 64.0), (-132.0, 86.0), (-160.0, 82.0)],
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
    "default_seed": 23,
    "fall_recovery_height_local_cm": -1500.0,
    "transition_fade_color": [0.92, 0.94, 0.97],
    "nav_z_local_cm": [-500.0, 2500.0],
    "atmosphere": {
        "sun_rotation": [-55.0, 210.0, 0.0], "sun_intensity_lux": 12.0, "sun_color": [1.0, 0.95, 0.85],
        "sky_light_intensity": 2.6, "sky_light_color": [0.9, 0.95, 1.0], "sky_luminance_factor": [1.0, 1.0, 1.05],
        "rayleigh_scattering": [0.175, 0.41, 1.0], "mie_scattering_scale": 0.004,
        "fog_density": 0.03, "fog_height_falloff": 0.6, "fog_height_cm": -3000.0, "fog_inscattering_color": [0.75, 0.8, 0.9],
        "fog_max_opacity": 0.9, "fog_start_distance_cm": 800.0,
        "second_fog_density": 0.25, "second_fog_height_falloff": 3.0, "second_fog_height_offset_cm": -1500.0,
        "volumetric_fog_albedo": [0.95, 0.95, 1.0], "volumetric_fog_emissive": [0.0, 0.0, 0.0], "volumetric_fog_extinction_scale": 0.8,
    },
    "post_process": {
        "priority": 10, "z_local_cm": [-6000.0, 8000.0],
        "settings": {"color_saturation": [0.8, 0.8, 0.8, 1.0], "color_gain": [1.06, 1.04, 1.0, 1.0], "bloom_intensity": 1.6, "auto_exposure_bias": 0.25},
    },
    "landscape_material": {
        "vectors": {"RealmTint": [0.92, 0.94, 0.97, 1.0], "RealmLayerTint_Road": [0.62857, 0.82222, 1.13889, 1.0], "RealmLayerTint_Soil": [0.92391, 1.0, 1.04545, 1.0]},
        "scalars": {"RealmBrightness": 1.45, "RealmDesaturation": 0.78},
    },
    "materials": dict(realm_rock_materials("Pale", PALE_CLIFF_TONE, PALE_ROCK_TONE), **{
        "Cloud": (PARENT_FOG1, {"SandColor": [0.95, 0.96, 1.0, 1.0]}, {"FinalOpacityTweak": 0.85}),
        "CloudLow": (PARENT_FOG2, {"SandColor": [0.86, 0.89, 0.96, 1.0]}, {"FinalOpacityTweak": 0.95}),
        "GoldBeam": (PARENT_BEAM, {"Color": [1.0, 0.82, 0.5, 1.0]}, {}),
    }),
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
        self.skipped_tiles = 0
        self.clipped_tiles = 0

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


def realm_frame(realm_id):
    placement = rp.REALM_PLACEMENT[realm_id]
    return float(placement["center_m"][0]), float(placement["center_m"][1]), float(placement["z_offset_m"])


def realm_center_cm(realm_id):
    return [float(v) for v in rp.realm_center_cm(realm_id)]


def gate_realm(gate_id):
    if gate_id in rp.SHAFTS_BY_GATE:
        return rp.SHAFTS_BY_GATE[gate_id]["realm_id"]
    return rp.CONNECTORS_BY_GATE[gate_id]["realm_id"]


def realm_gate_ids(realm_id):
    return [gate_id for gate_id in rp.GATE_IDS if gate_realm(gate_id) == realm_id]


def gate_end_piece(gate_id):
    if gate_id in rp.SHAFTS_BY_GATE:
        return rp.SHAFTS_BY_GATE[gate_id]["slot"]
    return rp.CONNECTORS_BY_GATE[gate_id]


def crossing_piece(gate_id):
    return next(piece for piece in rp.pieces_of_gate(gate_id) if piece["boundary_crossing"] is not None)


def gate_plazas(realm_id):
    cx, cy, z_offset = realm_frame(realm_id)
    plazas = []
    for gate_id in realm_gate_ids(realm_id):
        piece = gate_end_piece(gate_id)
        end = piece["vertices"][-1]
        plazas.append({"gate_id": gate_id, "plaza": (float(end[0]) - cx, float(end[1]) - cy), "floor": float(end[2]) - z_offset,
                       "yaw_deg": float(piece["bearings_deg"][-1]), "radius_m": PLAZA_RADIUS_BY_GATE_M[gate_id], "piece": piece})
    return plazas


def plaza_offset(plaza, forward_m, lateral_m):
    yaw = math.radians(plaza["yaw_deg"])
    x, y = plaza["plaza"]
    return x + forward_m * math.cos(yaw) - lateral_m * math.sin(yaw), y + forward_m * math.sin(yaw) + lateral_m * math.cos(yaw)


def gate_entry_local(realm_id, gate_id):
    piece = crossing_piece(gate_id)
    x, y = rp.to_grid_m(piece, piece["boundary_crossing"]["s_m"] + ENTRY_INSET_M, 0.0)
    cx, cy, _ = realm_frame(realm_id)
    return float(x) - cx, float(y) - cy


def make_grid():
    xs = np.arange(SIZE, dtype=np.float64) - HALF
    return np.meshgrid(xs, xs)


def world_grid(realm_id, X, Y):
    cx, cy, _ = realm_frame(realm_id)
    return X + cx, Y + cy


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


def is_in_mask(mask, x_m, y_m):
    x_m = np.asarray(x_m, dtype=np.float64)
    y_m = np.asarray(y_m, dtype=np.float64)
    ix = np.round(x_m + HALF).astype(int)
    iy = np.round(y_m + HALF).astype(int)
    inside = (ix >= 0) & (ix < SIZE) & (iy >= 0) & (iy < SIZE)
    return inside & mask[np.clip(iy, 0, SIZE - 1), np.clip(ix, 0, SIZE - 1)]


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


def shared_side(realm_id):
    edge = rp.shared_edge(realm_id)
    cx, cy, _ = realm_frame(realm_id)
    local = edge["value"] - (cy if edge["axis"] == "y" else cx)
    return edge["axis"] + ("+" if local > 0.0 else "-")


def outer_edge_distance(X, Y, realm_id):
    distances = {"x-": X + HALF, "x+": HALF - X, "y-": Y + HALF, "y+": HALF - Y}
    side = shared_side(realm_id)
    return np.minimum.reduce([value for key, value in distances.items() if key != side])


def shared_edge_frame(realm_id, Xw, Yw):
    edge = rp.shared_edge(realm_id)
    if edge["axis"] == "y":
        return edge, np.abs(Yw - edge["value"]), Xw
    return edge, np.abs(Xw - edge["value"]), Yw


def shared_band_share(realm_id, Xw, Yw):
    edge, distance, along = shared_edge_frame(realm_id, Xw, Yw)
    r0, r1 = edge["range"]
    t = np.clip((rp.REALM_EDGE_BAND_M - distance) / (rp.REALM_EDGE_BAND_M - SHARED_BAND_FLAT_M), 0.0, 1.0)
    taper = smoothstep(r0 - SHARED_TAPER_M, r0, along) * (1.0 - smoothstep(r1, r1 + SHARED_TAPER_M, along))
    return smoothstep(0.0, 1.0, smoothstep(0.0, 1.0, t)) * taper


def field_edge_sample(realm_id, Xw, Yw, array):
    edge, _, along = shared_edge_frame(realm_id, Xw, Yw)
    r0, r1 = edge["range"]
    index = np.round(np.clip(along, r0, r1) + gav.HALF).astype(int)
    fixed = int(round(edge["value"] + gav.HALF))
    if edge["axis"] == "y":
        return array[fixed, index]
    return array[index, fixed]


def shared_edge_cells(realm_id, Xw, Yw):
    edge, distance, along = shared_edge_frame(realm_id, Xw, Yw)
    r0, r1 = edge["range"]
    return (distance == 0.0) & (along >= r0) & (along <= r1)


def apply_shared_band(h, Xw, Yw, realm_id, field):
    _, _, z_offset = realm_frame(realm_id)
    share = shared_band_share(realm_id, Xw, Yw)
    edge_height = field_edge_sample(realm_id, Xw, Yw, field["height_m"]) - z_offset
    return h * (1.0 - share) + share * edge_height


def copy_shared_edge(h, Xw, Yw, realm_id, field):
    _, _, z_offset = realm_frame(realm_id)
    cells = shared_edge_cells(realm_id, Xw, Yw)
    edge_height = field_edge_sample(realm_id, Xw, Yw, field["height_m"]) - z_offset
    copied = h.copy()
    copied[cells] = edge_height[cells]
    return copied


def blend_shared_layers(layers, f, realm_id, field):
    share = shared_band_share(realm_id, f["Xw"], f["Yw"])
    cells = shared_edge_cells(realm_id, f["Xw"], f["Yw"])
    blended = {}
    for name in LAYER_NAMES:
        edge_layer = field_edge_sample(realm_id, f["Xw"], f["Yw"], field["layers"][name])
        layer = layers[name] * (1.0 - share) + share * edge_layer
        layer[cells] = edge_layer[cells]
        blended[name] = layer
    return blended


def flatten_pads(h, X, Y, pads, falloff):
    for circles, target in pads:
        inner = np.zeros(h.shape, dtype=bool)
        weight = np.zeros(h.shape)
        for (cx, cy, r) in circles:
            d = np.hypot(X - cx, Y - cy)
            inner |= d < r
            weight = np.maximum(weight, 1.0 - smoothstep(r, r + falloff, d))
        level = float(np.mean(h[inner])) if target is None else float(target)
        h = h * (1.0 - weight) + weight * level
    return h


def plaza_pads(plazas):
    return [([(p["plaza"][0], p["plaza"][1], p["radius_m"])], p["floor"]) for p in plazas]


def clearing_pads(cfg):
    return [([(cl["x"], cl["y"], 20.0)], None) for cl in cfg["clearings"]]


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


def sunken_roads(cfg, plazas):
    ramps = {t["name"]: ramp_geometry(t) for t in cfg["terraces"]}
    n_foot, n_start = ramps["North"]["foot"], ramps["North"]["start"]
    se_foot, se_start = ramps["Southeast"]["foot"], ramps["Southeast"]["start"]
    se_u = ramps["Southeast"]["u"]
    se_turn = (se_foot[0] + se_u[0] * 9.0, se_foot[1] + se_u[1] * 9.0)
    n_u = ramps["North"]["u"]
    n_turn = (n_foot[0] + n_u[0] * 9.0, n_foot[1] + n_u[1] * 9.0)
    by_gate = {p["gate_id"]: p["plaza"] for p in plazas}
    southwest = by_gate["SunkenStarfieldSouthwest"]
    southeast = by_gate["SunkenStarfieldSoutheast"]
    return {
        "North": [southwest, (-140.0, 45.0), (-95.0, 86.0), (-45.0, 100.0), (0.0, 102.0), n_turn, (60.0, 106.0), (115.0, 95.0), (150.0, 80.0), (168.0, 72.0)],
        "South": [(-140.0, 45.0), (-140.0, -25.0), (-90.0, -42.0), (-30.0, -37.0), (25.0, -60.0), (58.0, -84.0), se_turn, (100.0, -70.0), (130.0, -58.0), (160.0, -46.0), (178.0, -20.0), (185.0, 22.0), (163.9, 42.0), (162.4, 61.0)],
        "SoutheastGate": [southeast, (62.0, 160.0), (48.0, 128.0), (60.0, 106.0)],
        "BranchC1": [(-75.0, -40.5), (-68.0, -58.0), (-62.0, -70.0)],
        "BranchC2": [(60.0, 106.0), (78.0, 120.0), (86.0, 132.0)],
        "BranchC3": [(37.0, -69.0), (48.0, -55.0), (55.0, -48.0)],
        "BranchC4": [n_turn, n_foot, n_start, (-14.0, 152.0)],
        "BranchCrater": [se_turn, se_foot, se_start, (128.0, -115.0)],
    }, list(ramps.values())


def wall_toe_share(h, wall_top, band, wall_ed):
    rise = SUNKEN_WALL_TOE_SLOPE * np.maximum(band - SUNKEN_WALL_TOE_INSET_M - wall_ed, 0.0)
    headroom = wall_top - h
    return np.where(headroom > 0.0, np.clip(rise / np.maximum(headroom, 1e-6), 0.0, 1.0), 0.0)


def build_sunken_terrain(cfg, rng, realm_id, field, plazas):
    X, Y = make_grid()
    Xw, Yw = world_grid(realm_id, X, Y)
    ed = edge_distance(X, Y)
    wall_ed = outer_edge_distance(X, Y, realm_id)
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
    roads, ramps = sunken_roads(cfg, plazas)
    for ramp in ramps:
        h = apply_ramp(h, X, Y, ramp)
    c = cfg["crater"]
    dc = np.hypot(X - c["x"], Y - c["y"])
    h = h - c["depth"] * (1.0 - smoothstep(0.0, c["r"], dc)) + 0.6 * np.exp(-((dc - c["r"] - 1.0) / 2.5) ** 2)
    band = 31.0 + 5.0 * np.clip(make_value_noise(rng, 10), 0.0, 1.0)
    wall_top = 38.0 + 17.0 * np.clip(make_value_noise(rng, 6), 0.0, 1.0) + 2.0 * make_fbm(rng, 30, 2)
    t_wall = np.clip((band - wall_ed) / (band - 5.0), 0.0, 1.0)
    wall_p = np.maximum(smoothstep(0.0, 1.0, smoothstep(0.0, 1.0, t_wall)), wall_toe_share(h, wall_top, band, wall_ed))
    h = h * (1.0 - wall_p) + wall_p * wall_top
    h = flatten_pads(h, X, Y, clearing_pads(cfg), 14.0)
    h = ndimage.gaussian_filter(h, 0.8)
    h, road_weight, road_layer, road_polys = apply_roads(h, roads, 2.5, 6.0, 61)
    h = flatten_pads(h, X, Y, plaza_pads(plazas), 14.0)
    h = apply_shared_band(h, Xw, Yw, realm_id, field)
    water = (h < SUNKEN_WATER_Z_M) & ((lake_w > 0.5) | (channel_w > 0.5))
    return {"X": X, "Y": Y, "Xw": Xw, "Yw": Yw, "h": h, "slope": gav.terrain_slope_deg(h), "edge_d": ed, "wall": wall_p, "lake_e": e, "lake_w": lake_w,
            "channel_w": channel_w, "channel_polys": channel_polys, "water": water, "road_polys": road_polys, "ramps": ramps,
            "road_weight": road_weight, "road_layer": road_layer, "road_only_layer": road_layer}


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


def build_pale_terrain(cfg, rng, shapes, islands, bridges, realm_id, field, plazas):
    X, Y = make_grid()
    Xw, Yw = world_grid(realm_id, X, Y)
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
    h = flatten_pads(h, X, Y, clearing_pads(cfg), 6.0)
    h, road_weight, road_layer, road_polys = apply_roads(h, pale_roads(cfg), 2.5, 2.5, 41)
    h = flatten_pads(h, X, Y, plaza_pads(plazas), 6.0)
    h = apply_shared_band(h, Xw, Yw, realm_id, field)
    top = np.zeros(h.shape)
    for s in list(shapes.values()) + islands:
        top = np.maximum(top, s.interior(X, Y, 0.0))
    return {"X": X, "Y": Y, "Xw": Xw, "Yw": Yw, "h": h, "slope": gav.terrain_slope_deg(h), "edge_d": edge_distance(X, Y), "floor": floor, "top": top,
            "road_polys": road_polys, "ramps": [], "road_weight": road_weight, "road_layer": road_layer, "road_only_layer": road_layer}


def raise_promontory(h, Xw, Yw, piece, z_offset_m):
    empty = np.zeros(h.shape, dtype=bool)
    s_top = piece["stamp_s_m"][1]
    if not has_promontory(piece):
        return h, empty, empty, empty
    s, l = rp.passage_coords(piece, Xw, Yw)
    strip_half = np.where(s < s_top, rp.FLOOR_HALF_WIDTH_M, PROMONTORY_HALF_WIDTH_M)
    strip = (s >= s_top - rp.OPEN_WALL_BLEND_M) & (s <= piece["length"]) & (np.abs(l) <= strip_half)
    top = np.full(h.shape, -np.inf)
    top[strip] = rp.floor_height(piece, s[strip]) - z_offset_m
    for k, kind in enumerate(piece["segment_kinds"]):
        if kind != "terrace":
            continue
        seg_end = float(piece["seg_start_s"][k] + piece["seg_length"][k])
        mid = float(piece["seg_start_s"][k]) + 0.5 * float(piece["seg_length"][k])
        center_x, center_y = (float(v) for v in rp.to_grid_m(piece, mid, 0.0))
        disk = (np.hypot(Xw - center_x, Yw - center_y) <= TERRACE_RADIUS_M) & (s <= seg_end) & ~strip
        top[disk] = float(rp.floor_height(piece, mid)) - z_offset_m
    region = np.isfinite(top)
    if not region.any():
        return h, empty, empty, empty
    distance, (near_y, near_x) = ndimage.distance_transform_edt(~region, return_indices=True)
    face = top[near_y, near_x] - PROMONTORY_SIDE_SLOPE * distance
    raised = np.where(region, top, np.maximum(h, face))
    side = ~region & (s >= s_top) & (distance >= PROMONTORY_SIDE_CHECK_M) & (face - h >= PROMONTORY_SIDE_CHECK_M)
    raised, flank_side = raise_far_flank(raised, s, l, piece)
    road = strip & (np.abs(l) <= rp.FLOOR_HALF_WIDTH_M)
    return raised, region, side | flank_side, road


def raise_far_flank(h, s, l, piece):
    far = ~rp.near_lateral_mask(piece, l)
    s_start, s_end = piece["stamp_s_m"]
    wall_top = far & (s >= s_start) & (s <= s_end) & (rp.section_reach(piece, l) <= rp.WALL_TOP_OUTER_M)
    if not wall_top.any():
        return h, np.zeros(h.shape, dtype=bool)
    distance, (near_y, near_x) = ndimage.distance_transform_edt(~wall_top, return_indices=True)
    face = h[near_y, near_x] - PROMONTORY_SIDE_SLOPE * distance
    flank = far & ~wall_top & (face > h)
    side = flank & (distance >= PROMONTORY_SIDE_CHECK_M) & (face - h >= PROMONTORY_SIDE_CHECK_M)
    return np.where(flank, face, h), side


def carve_realm_connectors(h, Xw, Yw, realm_id):
    _, _, z_offset = realm_frame(realm_id)
    stamped = h
    floor = np.zeros(h.shape)
    wall = np.zeros(h.shape)
    promontory = np.zeros(h.shape, dtype=bool)
    promontory_side = np.zeros(h.shape, dtype=bool)
    gate_floors = {}
    for gate_id in realm_gate_ids(realm_id):
        if gate_id in rp.SHAFTS_BY_GATE:
            stamped, piece_floor, piece_wall = rp.stamp_shaft(stamped, Xw, Yw, rp.SHAFTS_BY_GATE[gate_id], z_offset)
            gate_floors[gate_id] = rp.gate_floor_cells(gate_id, Xw, Yw)
        else:
            piece = rp.CONNECTORS_BY_GATE[gate_id]
            stamped, piece_floor, piece_wall = rp.stamp_passage(stamped, Xw, Yw, piece, z_offset_m=z_offset)
            stamped, region, side, road = raise_promontory(stamped, Xw, Yw, piece, z_offset)
            piece_floor = np.maximum(piece_floor, road.astype(np.float64))
            promontory |= region
            promontory_side |= side
            gate_floors[gate_id] = rp.gate_floor_cells(gate_id, Xw, Yw) | road
        floor = np.maximum(floor, piece_floor)
        wall = np.maximum(wall, piece_wall)
    changed = rp.quantized_units(stamped) != rp.quantized_units(h)
    footprint = np.maximum.reduce([floor, wall, changed.astype(np.float64), promontory.astype(np.float64)])
    return {"h": stamped, "floor": floor, "wall": wall, "footprint": footprint, "promontory": promontory, "promontory_side": promontory_side, "gate_floors": gate_floors}


def finish_realm_terrain(f_pre, carved, realm_id, field):
    h = copy_shared_edge(carved["h"], f_pre["Xw"], f_pre["Yw"], realm_id, field)
    f = dict(f_pre)
    f.update({"h": h, "slope": gav.terrain_slope_deg(h), "passage_floor": carved["floor"], "passage_footprint": carved["footprint"],
              "promontory": carved["promontory"], "promontory_side": carved["promontory_side"], "gate_floors": carved["gate_floors"],
              "road_weight": np.maximum(f_pre["road_weight"], carved["floor"]), "road_layer": np.maximum(f_pre["road_layer"], carved["floor"])})
    if "lake_w" in f:
        f["water"] = (h < SUNKEN_WATER_Z_M) & ((f["lake_w"] > 0.5) | (f["channel_w"] > 0.5))
    return f


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


def topdown_route_masks(cfg, grid, road_mask, f, plazas):
    rid = cfg["realm_id"]
    floors = [f["gate_floors"][p["gate_id"]] for p in plazas]
    circles = [(c["x"], c["y"], 20.0) for c in cfg["clearings"]]
    circles += [(p["plaza"][0], p["plaza"][1], p["radius_m"]) for p in plazas]
    if cfg.get("pillar_ring"):
        ring = cfg["pillar_ring"]
        circles.append((ring["x"], ring["y"], ring["r"] - 2.0))
    water_level = SUNKEN_WATER_Z_M if rid == "SunkenStarfield" else PALE_WATER_LEVEL_M
    water = (grid.h < water_level) & ~np.logical_or.reduce(floors)
    return tdr.TDRouteMasks(grid, road_mask, circles, water, floors)


def apply_topdown_rules(L, f, cfg, plazas):
    if not tdr.ENABLED:
        return {}
    grid = tdr.TDTerrainGrid(tdr.quantize_height_m(f["h"], gav.UNITS_PER_METER))
    masks = topdown_route_masks(cfg, grid, f["road_layer"] >= 0.5, f, plazas)
    return tdr.apply_placement_rules(L.ism, L.actors, lambda bucket_key: gav.M[L.ism_meta[bucket_key[0]][0]], grid, masks)


def build_exclusion(f, circles):
    X, Y = f["X"], f["Y"]
    excl = np.maximum(smoothstep(0.02, 0.6, f["road_weight"]), (f["passage_footprint"] > 0.0).astype(np.float64))
    for (cx, cy, r) in circles:
        excl = np.maximum(excl, gav.radial(X, Y, cx, cy, r, r + 4.0))
    return np.clip(excl, 0.0, 1.0)


def place_common_markers(L, cfg):
    for cl in cfg["clearings"]:
        L.add_marker("CombatClearing", cl["x"], cl["y"], 0.0, L.label(f"Clearing_{cl['id']}_{cl['name']}"), tags=["TDRealmCombatClearing", cl["id"]])
    for lm in cfg["landmarks"]:
        L.add_marker("Landmark", lm["x"], lm["y"], 0.0, L.label(f"Landmark_{lm['name']}"), tags=["TDRealmLandmark", lm["name"]])


def place_arrival_markers(L, plazas):
    for p in plazas:
        x, y = p["plaza"]
        L.add_marker("RealmArrival", x, y, round(p["yaw_deg"], 4), L.label(f"Arrival_{p['gate_id']}"), z_off=p["floor"] - L.height(x, y), tags=["TDRealmArrival", p["gate_id"]])


def pick_spaced(points, count, min_dist):
    picked = []
    for p in points:
        if all((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 >= min_dist * min_dist for q in picked):
            picked.append(p)
        if len(picked) >= count:
            break
    return picked


def place_sunken(L, rng, f, cfg, plazas):
    X, Y, h, slope, ed = f["X"], f["Y"], f["h"], f["slope"], f["edge_d"]
    rid = cfg["realm_id"]
    mat = {name: realm_material_path(rid, name) for name in cfg["materials"]}
    c2 = next(c for c in cfg["clearings"] if c["id"] == "C2")
    c1 = next(c for c in cfg["clearings"] if c["id"] == "C1")
    c4 = next(c for c in cfg["clearings"] if c["id"] == "C4")
    crater = cfg["crater"]
    circles = [(p["plaza"][0], p["plaza"][1], 20.0) for p in plazas]
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

    for p in plazas:
        light(*plaza_offset(p, PLAZA_LIGHT_FORWARD_M, PLAZA_LIGHT_LATERAL_M), 3.0, 60.0, GLOW_CYAN, 16.0)
        light(*plaza_offset(p, PLAZA_LIGHT_FORWARD_M, -PLAZA_LIGHT_LATERAL_M), 3.0, 60.0, GLOW_VIOLET, 16.0)
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

    for p in plazas:
        fx("Dust1", *plaza_offset(p, PLAZA_LIGHT_FORWARD_M, 0.0), 1.5)
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


def blocked_boxes_m(realm_id):
    boxes = [(-FIELD_HALF_M, FIELD_HALF_M, -FIELD_HALF_M, FIELD_HALF_M)]
    return boxes + [rp.realm_box_m(other) for other in rp.REALM_PLACEMENT if other != realm_id]


def is_tile_blocked(realm_id, x_local, y_local, size_m):
    cx, cy, _ = realm_frame(realm_id)
    half = 0.5 * size_m
    tile = (x_local + cx - half, x_local + cx + half, y_local + cy - half, y_local + cy + half)
    return any(rp.boxes_overlap(tile, box) for box in blocked_boxes_m(realm_id))


def add_cover_tile(L, material, x, y, z_m, yaw, label, clip_to_box):
    if not is_tile_blocked(L.realm_id, x, y, CLOUD_TILE_M):
        L.add_material_plane(material, x, y, z_m, yaw, CLOUD_TILE_M, CLOUD_TILE_M, label)
        return
    if not clip_to_box:
        L.skipped_tiles += 1
        return
    half = 0.5 * CLOUD_TILE_M
    x0, x1 = max(x - half, -HALF), min(x + half, HALF)
    y0, y1 = max(y - half, -HALF), min(y + half, HALF)
    if x1 <= x0 or y1 <= y0:
        L.skipped_tiles += 1
        return
    L.clipped_tiles += 1
    L.add_material_plane(material, 0.5 * (x0 + x1), 0.5 * (y0 + y1), z_m, 0.0, x1 - x0, y1 - y0, label)


def add_outer_ring(L, mat, ring_layers, interior_range):
    for layer_i, (z_m, material) in enumerate(ring_layers):
        for i in CLOUD_RING_INDEX_RANGE:
            for j in CLOUD_RING_INDEX_RANGE:
                if i in interior_range and j in interior_range:
                    continue
                x, y = CLOUD_LATTICE_ORIGIN_M + CLOUD_TILE_M * i, CLOUD_LATTICE_ORIGIN_M + CLOUD_TILE_M * j
                add_cover_tile(L, mat[material], x, y, z_m, 90.0 * ((3 * i + 5 * j) % 4), f"{outer_ring_prefix(L)}{layer_i}_{i}_{j}", False)


def place_pale(L, rng, f, cfg, shapes, islands, bridges, plazas):
    X, Y, h, slope = f["X"], f["Y"], f["h"], f["slope"]
    rid = cfg["realm_id"]
    mat = {name: realm_material_path(rid, name) for name in cfg["materials"]}
    ring = cfg["pillar_ring"]
    c3 = next(c for c in cfg["clearings"] if c["id"] == "C3")
    circles = [(c["x"], c["y"], 20.0) for c in cfg["clearings"]]
    circles += [(p["plaza"][0], p["plaza"][1], 20.0) for p in plazas]
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
            if bool(is_in_mask(f["clear"], x, y)):
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
        for i in range(CLOUD_INTERIOR_COUNT):
            for j in range(CLOUD_INTERIOR_COUNT):
                x, y = CLOUD_LATTICE_ORIGIN_M + CLOUD_TILE_M * i, CLOUD_LATTICE_ORIGIN_M + CLOUD_TILE_M * j
                yaw_turns = int(rng.integers(0, 4))
                add_cover_tile(L, mat[material], x, y, z_m, 90.0 * yaw_turns, L.label(f"Cloud_{layer_i}_{i}_{j}"), True)
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
    if layout.get("generator_version", 0) < GENERATOR_VERSION:
        raise TDInputError(f"main layout.json generator_version {layout.get('generator_version')} < {GENERATOR_VERSION} (rerun generate_ashen_vale.py for the continuous world)")
    connectors = {c["gate_id"]: c for c in layout.get("connectors", [])}
    missing = [gate_id for gate_id in rp.GATE_IDS if gate_id not in connectors]
    if missing:
        raise TDInputError(f"main layout.json has no connectors for {missing} (rerun generate_ashen_vale.py with realm_passages)")
    raw = np.fromfile(height_path, dtype="<u2")
    if raw.size != gav.SIZE * gav.SIZE:
        raise TDInputError(f"main height.r16 size {raw.size} != {gav.SIZE}^2")
    units = raw.reshape(gav.SIZE, gav.SIZE).astype(np.float64)
    layers_r8 = {}
    for name in LAYER_NAMES:
        path = os.path.join(main_dir, f"layer_{name}.r8")
        if not os.path.isfile(path):
            raise TDInputError(f"main layer {path} missing (run generate_ashen_vale.py first)")
        data = np.fromfile(path, dtype=np.uint8)
        if data.size != gav.SIZE * gav.SIZE:
            raise TDInputError(f"main layer_{name}.r8 size {data.size} != {gav.SIZE}^2")
        layers_r8[name] = data.reshape(gav.SIZE, gav.SIZE).astype(np.float64)
    field = {"units": units, "height_m": (units - 32768.0) / gav.UNITS_PER_METER, "layers_r8": layers_r8,
             "layers": {name: value / 255.0 for name, value in layers_r8.items()}, "connectors": connectors}
    require_field_in_sync(field)
    return field


def require_field_in_sync(field):
    for gate_id in rp.GATE_IDS:
        entry = rp.gate_entry(gate_id)
        recorded = field["connectors"][gate_id]
        for key in ("approach_length_cm", "departure_length_cm"):
            if abs(float(recorded[key]) - float(entry[key])) > 0.5:
                raise TDInputError(f"{gate_id}: field {key} {recorded[key]} != realm_passages {entry[key]} (field outputs out of sync)")
        piece = crossing_piece(gate_id)
        x, y = piece["boundary_crossing"]["point_m"]
        terrain = float(gav.sample_bilinear(field["height_m"], np.array([x]), np.array([y]))[0])
        floor = float(rp.floor_height(piece, piece["boundary_crossing"]["s_m"]))
        if abs(terrain - floor) > FIELD_FLOOR_TOLERANCE_M:
            raise TDInputError(f"{gate_id}: field height.r16 at boundary crossing {terrain:.4f} != connector floor {floor:.4f} (field outputs out of sync)")


def cm_point(x_m, y_m, z_m):
    return [round(float(x_m) * 100.0, 1), round(float(y_m) * 100.0, 1), round(float(z_m) * 100.0, 1)]


def clear_connector_items(L, clear):
    removed = 0
    for bucket_key in list(L.ism):
        bucket = L.ism[bucket_key]
        inside = is_in_mask(clear, np.array([t[0] for t in bucket]) / 100.0, np.array([t[1] for t in bucket]) / 100.0)
        kept = [t for t, hit in zip(bucket, inside) if not hit]
        removed += len(bucket) - len(kept)
        if kept:
            L.ism[bucket_key] = kept
            continue
        del L.ism[bucket_key]
    for name in ("actors", "planes", "lights", "niagara"):
        items = getattr(L, name)
        kept = [e for e in items if not bool(is_in_mask(clear, e["loc"][0] / 100.0, e["loc"][1] / 100.0))]
        removed += len(items) - len(kept)
        setattr(L, name, kept)
    return removed


def connector_prop_groups(gate_id):
    if gate_id in rp.SHAFTS_BY_GATE:
        shaft = rp.SHAFTS_BY_GATE[gate_id]
        return [
            ("Shaft_", shaft, None, rp.lift_zone_props(shaft), shaft["bottom_z_m"]),
            ("", shaft, None, rp.lift_landing_props(shaft), shaft["top_z_m"]),
            ("TopCanyon_", None, shaft["top_canyon"], rp.field_passage_dressing(shaft["top_canyon"]), None),
            ("Slot_", None, shaft["slot"], rp.seam_zone_props(shaft["slot"]) + rp.field_passage_dressing(shaft["slot"]), None),
        ]
    piece = rp.CONNECTORS_BY_GATE[gate_id]
    return [("", None, piece, rp.seam_zone_props(piece) + rp.field_passage_dressing(piece), None)]


def connector_prop_position(prop, shaft, piece, base_z_m):
    if shaft is not None:
        x_m, y_m, z_m = rp.lift_prop_position_m(shaft, prop, base_z_m)
        return x_m, y_m, z_m, shaft["frame_yaw_deg"] + prop["yaw_offset"]
    x_cm, y_cm, z_cm = rp.connector_to_world(piece, prop["s"], prop["l"], prop["z_rel"])
    z_m = z_cm / 100.0
    if prop["kind"] == "mesh":
        z_m += gav.MESH_BASE_BELOW_PIVOT.get(prop["key"], 0.0) * prop["scale"][2]
    return x_cm / 100.0, y_cm / 100.0, z_m, rp.connector_yaw_deg(piece, prop["s"]) + prop["yaw_offset"]


def connector_prop_entry(prop, position, label, realm_id):
    x_m, y_m, z_m, yaw = position
    loc = cm_point(x_m, y_m, z_m)
    rot = [round(prop["pitch"], 3), round(float(yaw), 3), round(prop["roll"], 3)]
    scale = [round(v, 3) for v in prop["scale"]]
    if prop["kind"] == "mesh":
        entry = {"mesh": prop["mesh"], "loc": loc, "rot": rot, "scale": scale, "label": label}
        material = CONNECTOR_MESH_MATERIALS.get(prop.get("key"))
        if material:
            entry["material"] = realm_material_path(realm_id, material)
            entry["materials"] = [entry["material"]]
        return "actors", entry
    if prop["kind"] == "plane":
        return "planes", {"mesh": prop["mesh"], "material": prop["material"], "loc": loc, "rot": rot, "scale": scale, "label": label}
    if prop["kind"] != "light":
        raise TDInputError(f"{label}: unknown connector prop kind {prop['kind']}")
    return "lights", {"type": prop["light_type"], "loc": loc, "rot": [-90.0, 0.0, 0.0], "intensity": prop["intensity"], "color": prop["color"],
                      "radius": round(prop["radius_m"] * 100.0, 1), "label": label, "source_radius": prop["source_radius"], "volumetric": prop["volumetric"],
                      "cast_shadows": bool(prop["cast_shadows"])}


def build_connector_props(realm_id):
    out = {"actors": [], "planes": [], "lights": []}
    light_report = {}
    for gate_id in realm_gate_ids(realm_id):
        gate_lights = []
        for name_prefix, shaft, piece, props, base_z_m in connector_prop_groups(gate_id):
            for prop in props:
                label = f"{CONNECTOR_LABEL_PREFIX}{gate_id}_{name_prefix}{prop['name']}"
                key, entry = connector_prop_entry(prop, connector_prop_position(prop, shaft, piece, base_z_m), label, realm_id)
                (gate_lights if key == "lights" else out[key]).append(entry)
        if gate_id in rp.SHAFTS_BY_GATE:
            shaft = rp.SHAFTS_BY_GATE[gate_id]
            light_report[gate_id] = rp.clamp_lights_off_lift_window(gate_lights, shaft, shaft["bottom_z_m"], shaft["top_z_m"])
        out["lights"].extend(gate_lights)
    labels = [e["label"] for items in out.values() for e in items]
    duplicates = sorted({label for label in labels if labels.count(label) > 1})
    if duplicates:
        raise ValueError(f"{realm_id}: duplicate connector prop labels {duplicates[:5]}")
    return out, light_report


def gate_centerline_cm(gate_id):
    if gate_id in rp.SHAFTS_BY_GATE:
        shaft = rp.SHAFTS_BY_GATE[gate_id]
        axis = (shaft["axis_m"][0], shaft["axis_m"][1], shaft["top_z_m"])
        points = list(shaft["top_canyon"]["vertices"]) + [axis] + list(shaft["slot"]["vertices"])
    else:
        points = list(rp.CONNECTORS_BY_GATE[gate_id]["vertices"])
    return [[round(float(x) * 100.0, 2), round(float(y) * 100.0, 2), round(float(z) * 100.0, 4)] for x, y, z in points]


def build_gate_entries(realm_id):
    entries = []
    for gate_id in realm_gate_ids(realm_id):
        entry = rp.gate_entry(gate_id)
        entry["centerline_cm"] = gate_centerline_cm(gate_id)
        entry["geometry"] = rp.gate_geometry(gate_id)
        entries.append(entry)
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


def overlap_area_m2(a, b):
    return max(0.0, min(a[1], b[1]) - max(a[0], b[0])) * max(0.0, min(a[3], b[3]) - max(a[2], b[2]))


def segment_grade_limit_deg(piece, kind):
    grade = {"canyon": piece["max_grade"], "terrace": 0.0, "ramp": rp.RAMP_MAX_GRADE}[kind]
    return math.degrees(math.atan(grade))


def has_promontory(piece):
    return piece["length"] - piece["stamp_s_m"][1] > PROMONTORY_MIN_LENGTH_M


def piece_floor_end(piece):
    return piece["length"] if has_promontory(piece) else piece["stamp_s_m"][1]


def check_plaza_flatness(f, plazas):
    connector_zone = ndimage.binary_dilation(f["passage_footprint"] > 0.0, iterations=1)
    detail = {}
    for p in plazas:
        ys, xs = disk_indices(p["plaza"][0], p["plaza"][1], GATE_FLAT_RADIUS_M)
        open_cells = ~connector_zone[ys, xs]
        detail[f"Plaza_{p['gate_id']}"] = round(float(f["slope"][ys[open_cells], xs[open_cells]].max()), 2)
    worst = max(detail.values())
    return make_check("a", "plaza_r14m_flatness_deg", worst <= GATE_MAX_SLOPE_DEG, worst, f"<= {GATE_MAX_SLOPE_DEG} (connector cells excluded, see a2)", detail)


def check_connector_floors(f, plazas):
    detail = {}
    passed = True
    worst_all = 0.0
    for p in plazas:
        for piece in rp.pieces_of_gate(p["gate_id"]):
            s, l = rp.passage_coords(piece, f["Xw"], f["Yw"])
            a = rp.section_reach(piece, l)
            low = piece["stamp_s_m"][0] + rp.OPEN_FLOOR_BLEND_M
            high = piece_floor_end(piece) - rp.OPEN_FLOOR_BLEND_M
            inner = (s >= low) & (s <= high) & (a <= rp.FLOOR_HALF_WIDTH_M - 1.0)
            if not inner.any():
                continue
            item = {"inner_floor_cells": int(inner.sum()), "inner_floor_max_slope_deg": round(float(f["slope"][inner].max()), 2), "segments": []}
            passed = passed and item["inner_floor_max_slope_deg"] <= CONNECTOR_FLOOR_MAX_SLOPE_DEG
            worst_all = max(worst_all, item["inner_floor_max_slope_deg"])
            for k, kind in enumerate(piece["segment_kinds"]):
                seg_low = max(low, float(piece["seg_start_s"][k]) + 1.0)
                seg_high = min(high, float(piece["seg_start_s"][k] + piece["seg_length"][k]) - 1.0)
                cells = (s >= seg_low) & (s <= seg_high) & (np.abs(l) <= PASSAGE_FLOOR_HALF_CHECK_M)
                if not cells.any():
                    continue
                limit = segment_grade_limit_deg(piece, kind) + CONNECTOR_GRADE_TOLERANCE_DEG
                value = round(float(f["slope"][cells].max()), 2)
                item["segments"].append({"kind": kind, "s_m": [round(seg_low, 2), round(seg_high, 2)], "cells": int(cells.sum()), "max_slope_deg": value, "limit_deg": round(limit, 2)})
                passed = passed and value <= limit
            detail[f"{p['gate_id']}/{piece['name']}"] = item
    for gate_id in (p["gate_id"] for p in plazas if p["gate_id"] in rp.SHAFTS_BY_GATE):
        width = rp.SHAFTS_BY_GATE[gate_id]["slot_floor_width_m"]
        detail[f"{gate_id}/slot_floor_width_m"] = width
        passed = passed and width >= 12.0
    return make_check("a2", "connector_floor_slope_deg_by_segment", passed, worst_all, f"inner floor <= {CONNECTOR_FLOOR_MAX_SLOPE_DEG}, segment <= grade limit + {CONNECTOR_GRADE_TOLERANCE_DEG}, slot width >= 12", detail)


def check_floods(cfg, f, walkable, plazas, realm_id):
    targets = {}
    for p in plazas:
        targets[f"Entry_{p['gate_id']}"] = gate_entry_local(realm_id, p["gate_id"])
        targets[f"Plaza_{p['gate_id']}"] = p["plaza"]
    targets.update({f"Clearing_{c['id']}": (c["x"], c["y"]) for c in cfg["clearings"]})
    targets.update({f"Landmark_{lm['name']}": (lm["x"], lm["y"]) for lm in cfg["landmarks"]})
    detail = {}
    n_ok = 0
    for p in plazas:
        labels, comp = flood_component(walkable, *gate_entry_local(realm_id, p["gate_id"]))
        missing = [name for name, (x, y) in targets.items() if not reached(labels, comp, x, y)]
        detail[p["gate_id"]] = {"reached": len(targets) - len(missing), "missing": missing}
        n_ok += len(targets) - len(missing)
    n_total = len(targets) * len(plazas)
    return make_check("c", "flood_from_each_connector_entry_reaches_targets", n_ok == n_total, f"{n_ok}/{n_total}", "all", detail)


def open_start_point(f, plaza, max_slope_deg):
    for step in np.arange(OPEN_START_RANGE_M[0], OPEN_START_RANGE_M[1] + 0.5, 1.0):
        x, y = plaza_offset(plaza, float(step), 0.0)
        iy, ix = int(round(y + HALF)), int(round(x + HALF))
        if not (0 <= ix < SIZE and 0 <= iy < SIZE):
            return None
        if not f["clear"][iy, ix] and f["slope"][iy, ix] <= max_slope_deg:
            return x, y
    return None


def check_rim_leak(f, realm_id, plazas):
    _, distance, _ = shared_edge_frame(realm_id, f["Xw"], f["Yw"])
    if realm_id == "SunkenStarfield":
        threshold = SUNKEN_RIM_FLOOD_MAX_DEG
        rim = (f["edge_d"] <= RIM_CELL_DISTANCE_M) & ~f["clear"]
        name = "outer_wall_and_shared_band_block_slope_le_50deg_flood"
    else:
        threshold = PALE_RIM_FLOOD_MAX_DEG
        rim = ((f["h"] < PALE_FALL_FLOOR_M) | (distance <= RIM_CELL_DISTANCE_M)) & ~f["clear"]
        name = "mesa_edges_and_shared_band_block_slope_le_55deg_flood"
    domain = (f["slope"] <= threshold) & ~f["clear"]
    detail = {"rim_cells": int(rim.sum())}
    passed = True
    for p in plazas:
        start = open_start_point(f, p, threshold)
        if start is None:
            detail[p["gate_id"]] = {"start": None}
            passed = False
            continue
        labels, comp = flood_component(domain, *start)
        leak = int(np.sum(labels[rim] == comp)) if comp else 0
        detail[p["gate_id"]] = {"start_local_m": [round(start[0], 2), round(start[1], 2)], "component_cells": int(np.sum(labels == comp)) if comp else 0, "rim_cells_reached": leak}
        passed = passed and comp > 0 and leak == 0
    if "wall" in f:
        band = f["wall"] > 0.05
        detail["wall_band_cells_over_50deg"] = int((band & (f["slope"] > 50.0)).sum())
    share = shared_band_share(realm_id, f["Xw"], f["Yw"])
    band = (share > 0.05) & (share < 0.95) & ~f["clear"]
    detail["shared_band_slope_p50_deg"] = round(float(np.percentile(f["slope"][band], 50)), 2) if band.any() else None
    return make_check("d", name, passed, int(not passed), "no rim cell reachable (connector clear zone excluded)", detail)


def check_adjacency(realm_id):
    box = rp.realm_box_m(realm_id)
    field_box = (-FIELD_HALF_M, FIELD_HALF_M, -FIELD_HALF_M, FIELD_HALF_M)
    edge = rp.shared_edge(realm_id)
    detail = {"realm_box_m": list(box), "field_overlap_m2": overlap_area_m2(box, field_box), "shared_edge": {"name": edge["name"], "axis": edge["axis"], "value_m": edge["value"], "range_m": list(edge["range"])}}
    passed = detail["field_overlap_m2"] == 0.0 and edge["range"][1] - edge["range"][0] > 0.0
    for other in rp.REALM_PLACEMENT:
        if other == realm_id:
            continue
        detail[f"{other}_overlap_m2"] = overlap_area_m2(box, rp.realm_box_m(other))
        passed = passed and detail[f"{other}_overlap_m2"] == 0.0
    min_gap = LOADING_RANGE_CM + GRID_CELL_CM
    gap, gaps = box_gap_cm(realm_center_cm(realm_id), REALM_HALF_EXTENT_CM, ATLAS_BAND_CENTER_CM, ATLAS_BAND_HALF_CM)
    detail["atlas_band_gap_cm"] = {"best_axis_gap_cm": round(gap, 1), "axis_gaps_cm": [round(v, 1) for v in gaps], "limit_cm": min_gap}
    passed = passed and gap >= min_gap
    return make_check("e", "realm_adjacent_to_field_no_overlap_atlas_gap", passed, detail["field_overlap_m2"], f"field/realm overlap 0 m2, shared edge exact, atlas gap >= {min_gap}", detail)


def plane_tile_blocked(realm_id, e):
    cx, cy, _ = realm_frame(realm_id)
    yaw = math.radians(e["rot"][1])
    half_x, half_y = 0.5 * abs(e["scale"][0]), 0.5 * abs(e["scale"][1])
    extent_x = abs(half_x * math.cos(yaw)) + abs(half_y * math.sin(yaw))
    extent_y = abs(half_x * math.sin(yaw)) + abs(half_y * math.cos(yaw))
    x, y = e["loc"][0] / 100.0 + cx, e["loc"][1] / 100.0 + cy
    tile = (x - extent_x, x + extent_x, y - extent_y, y + extent_y)
    return any(overlap_area_m2(tile, box) > TILE_OVERLAP_TOLERANCE_M2 for box in blocked_boxes_m(realm_id))


def check_placements(L, realm_id):
    outside = 0
    for e in L.actors + L.planes + L.lights + L.niagara + L.markers:
        if e.get("label", "").startswith(outer_ring_prefix(L)):
            continue
        outside += int(abs(e["loc"][0]) > HALF * 100.0 or abs(e["loc"][1]) > HALF * 100.0)
    for tr in L.ism.values():
        outside += sum(int(abs(t[0]) > HALF * 100.0 or abs(t[1]) > HALF * 100.0) for t in tr)
    tiles = [e for e in L.planes if e["label"].startswith(outer_ring_prefix(L)) or e["label"].startswith(L.label("Cloud_"))]
    blocked = sum(int(plane_tile_blocked(realm_id, e)) for e in tiles)
    detail = {"outside_realm_box": outside, "tiles_kept": len(tiles), "tiles_clipped_to_box": L.clipped_tiles, "tiles_skipped_overlap": L.skipped_tiles, "tiles_overlapping_field_or_realm": blocked}
    return make_check("f", "placements_inside_realm_box_and_tiles_clear_of_neighbours", outside == 0 and blocked == 0, outside + blocked, "0", detail)


def arrival_node_error_m(f, plaza, z_offset):
    x, y = plaza["plaza"]
    ix0, iy0 = int(math.floor(x + HALF)), int(math.floor(y + HALF))
    piece = plaza["piece"]
    worst = 0.0
    for iy in (iy0, iy0 + 1):
        for ix in (ix0, ix0 + 1):
            s, _ = rp.passage_coords(piece, np.array([f["Xw"][iy, ix]]), np.array([f["Yw"][iy, ix]]))
            expected = float(rp.floor_height(piece, np.clip(s, 0.0, piece["length"]))[0]) - z_offset
            worst = max(worst, abs(float(f["h"][iy, ix]) - expected))
    return worst


def check_connector_geometry(realm_id, L, f, plazas):
    cx, cy, z_offset = realm_frame(realm_id)
    edge = rp.shared_edge(realm_id)
    markers = {m["label"]: m for m in L.markers}
    detail = {}
    passed = True
    for p in plazas:
        gate_id = p["gate_id"]
        entry = rp.gate_entry(gate_id)
        arrival = entry["arrival"]["loc_cm"]
        field_return = entry["field_return"]["loc_cm"]
        ax, ay = arrival[0] / 100.0 - cx, arrival[1] / 100.0 - cy
        floor_local = (arrival[2] - rp.GATE_PAWN_HEIGHT_CM) / 100.0 - z_offset
        crossings = [piece["boundary_crossing"] for piece in rp.pieces_of_gate(gate_id) if piece["boundary_crossing"] is not None]
        bearing_step = max((rp.bearing_step_deg(piece["bearings_deg"][k - 1], piece["bearings_deg"][k]) for piece in rp.pieces_of_gate(gate_id) for k in range(1, len(piece["bearings_deg"]))), default=0.0)
        if entry["gate_kind"] == "Passage":
            yaw_error = yaw_difference_deg(entry["arrival"]["yaw_deg"], entry["field_return"]["yaw_deg"])
        else:
            shaft = rp.SHAFTS_BY_GATE[gate_id]
            yaw_error = max(yaw_difference_deg(shaft["top_exit_yaw_deg"], shaft["frame_yaw_deg"] + 90.0), yaw_difference_deg(shaft["bottom_exit_yaw_deg"], shaft["frame_yaw_deg"] + 180.0))
        marker = markers.get(L.label(f"Arrival_{gate_id}"))
        marker_error_cm = None
        if marker:
            marker_error_cm = round(max(abs(marker["loc"][0] - ax * 100.0), abs(marker["loc"][1] - ay * 100.0), abs(marker["loc"][2] - floor_local * 100.0)), 3)
        terrain_error_m = arrival_node_error_m(f, p, z_offset)
        item = {
            "arrival_local_m": [round(ax, 3), round(ay, 3)], "arrival_inside_realm": bool(abs(ax) < HALF and abs(ay) < HALF),
            "field_return_inside_field": bool(abs(field_return[0]) <= FIELD_HALF_CM and abs(field_return[1]) <= FIELD_HALF_CM),
            "crossings": [{"s_m": c["s_m"], "point_m": list(c["point_m"]), "edge": c.get("edge"), "edge_margin_m": c.get("edge_margin_m")} for c in crossings],
            "max_bearing_step_deg": round(bearing_step, 4), "yaw_error_deg": round(yaw_error, 4), "marker_error_cm": marker_error_cm, "arrival_terrain_error_m": round(terrain_error_m, 4),
        }
        ok = item["arrival_inside_realm"] and item["field_return_inside_field"] and len(crossings) == 1 and crossings[0].get("edge") == edge["name"]
        ok = ok and bearing_step <= rp.MAX_BEARING_STEP_DEG and yaw_error <= 0.01 and marker_error_cm is not None and marker_error_cm <= 1.0 and terrain_error_m <= FIELD_FLOOR_TOLERANCE_M
        detail[gate_id] = item
        passed = passed and ok
    return make_check("g", "connector_ends_crossing_and_bearing_continuity", passed, len(detail), "all gates", detail)


def shared_edge_stencil(realm_id, depth):
    edge = rp.shared_edge(realm_id)
    cx, cy, _ = realm_frame(realm_id)
    r0, r1 = edge["range"]
    along = np.arange(r0, r1 + 1.0)
    field_step = 1.0 if edge["value"] < 0.0 else -1.0
    k = np.arange(depth + 1, dtype=np.float64)[:, None]
    along_grid = np.broadcast_to(along[None, :], (depth + 1, len(along)))
    field_normal = np.broadcast_to(edge["value"] + field_step * k, along_grid.shape)
    realm_normal = np.broadcast_to(edge["value"] - field_step * k, along_grid.shape)
    if edge["axis"] == "y":
        field_x, field_y, realm_x, realm_y = along_grid, field_normal, along_grid, realm_normal
    else:
        field_x, field_y, realm_x, realm_y = field_normal, along_grid, realm_normal, along_grid
    return {
        "field": (field_x, field_y, np.round(field_y + gav.HALF).astype(int), np.round(field_x + gav.HALF).astype(int)),
        "realm": (realm_x, realm_y, np.round(realm_y - cy + HALF).astype(int), np.round(realm_x - cx + HALF).astype(int)),
    }


def analytic_section_world(piece, xs, ys):
    x0, x1 = int(math.floor(float(xs.min()))) - 2, int(math.ceil(float(xs.max()))) + 2
    y0, y1 = int(math.floor(float(ys.min()))) - 2, int(math.ceil(float(ys.max()))) + 2
    PX, PY = np.meshgrid(np.arange(x0, x1 + 1.0), np.arange(y0, y1 + 1.0))
    stamped, _, _ = rp.stamp_passage(np.zeros(PX.shape), PX, PY, piece)
    return stamped[np.round(ys - y0).astype(int), np.round(xs - x0).astype(int)]


def full_section_cells(piece, xs, ys, reach_limit_m):
    s, l = rp.passage_coords(piece, xs, ys)
    s0, s1 = piece["stamp_s_m"]
    return (s >= s0 + rp.OPEN_WALL_BLEND_M) & (s <= s1 - rp.OPEN_WALL_BLEND_M) & (rp.section_reach(piece, l) <= reach_limit_m)


def check_shared_edge(realm_id, f, layers, field, plazas):
    _, _, z_offset = realm_frame(realm_id)
    stencil = shared_edge_stencil(realm_id, SHARED_CHECK_DEPTH)
    fx, fy, fiy, fix = stencil["field"]
    rx, ry, riy, rix = stencil["realm"]
    realm_units = rp.quantized_units(f["h"])[riy, rix]
    field_units = field["units"][fiy, fix]
    unit_diff = float(np.max(np.abs(realm_units[0] - (field_units[0] - gav.UNITS_PER_METER * z_offset))))
    realm_world = (realm_units - 32768.0) / gav.UNITS_PER_METER + z_offset
    field_world = (field_units - 32768.0) / gav.UNITS_PER_METER
    edge_layer_diff = 0.0
    band_layer_diff = 0.0
    for name in LAYER_NAMES:
        realm_r8 = np.clip(np.round(layers[name] * 255.0), 0, 255)[riy, rix]
        field_r8 = field["layers_r8"][name][fiy, fix]
        edge_layer_diff = max(edge_layer_diff, float(np.max(np.abs(realm_r8[0] - field_r8[0]))))
        band_layer_diff = max(band_layer_diff, float(np.max(np.abs(realm_r8[1:] - field_r8[0][None, :]))) / 255.0)
    detail = {"shared_vertices": int(fx.shape[1]), "u16_max_diff": unit_diff, "edge_layer_r8_max_diff": edge_layer_diff,
              "band_d1_5_layer_max_diff": round(band_layer_diff, 4), "gates": {}}
    passed = unit_diff == 0.0 and edge_layer_diff == 0.0 and band_layer_diff <= SHARED_LAYER_TOLERANCE
    field_step = field_world[0] - field_world[1]
    realm_step = realm_world[1] - realm_world[0]
    for p in plazas:
        piece = crossing_piece(p["gate_id"])
        floor = full_section_cells(piece, rx[0], ry[0], rp.FLOOR_HALF_WIDTH_M - 1.0)
        walls = full_section_cells(piece, rx[0], ry[0], rp.WALL_TOP_OUTER_M)
        if not floor.any():
            detail["gates"][p["gate_id"]] = {"floor_cells": 0}
            passed = False
            continue
        columns = np.nonzero(walls)[0]
        analytic_field = analytic_section_world(piece, fx[:, columns], fy[:, columns])
        analytic_realm = analytic_section_world(piece, rx[:, columns], ry[:, columns])
        linear = np.abs(analytic_field[1] - 2.0 * analytic_realm[0] + analytic_realm[1]) <= 1.0 / gav.UNITS_PER_METER
        slope_field = np.degrees(np.arctan(field_step[columns]))
        slope_realm = np.degrees(np.arctan(realm_step[columns]))
        slope_diff = np.abs(slope_realm - slope_field)[linear]
        step_diff = np.abs(realm_step - field_step)[floor]
        field_ok = full_section_cells(piece, fx[:, columns], fy[:, columns], rp.WALL_TOP_OUTER_M)
        realm_ok = full_section_cells(piece, rx[:, columns], ry[:, columns], rp.WALL_TOP_OUTER_M)
        field_residual = np.abs(rp.quantized_units(field_world[:, columns]) - rp.quantized_units(analytic_field))[field_ok]
        realm_residual = np.abs(rp.quantized_units(realm_world[:, columns]) - rp.quantized_units(analytic_realm))[realm_ok]
        item = {
            "floor_cells": int(floor.sum()), "floor_wall_cells": int(walls.sum()), "linear_stencil_cells": int(linear.sum()),
            "floor_step_max_diff_m": round(float(step_diff.max()), 5),
            "perpendicular_slope_max_diff_deg": round(float(slope_diff.max()), 4) if slope_diff.size else None,
            "analytic_residual_units": {"field_d0_5": float(field_residual.max()) if field_residual.size else None, "realm_d0_5": float(realm_residual.max()) if realm_residual.size else None},
        }
        ok = item["floor_step_max_diff_m"] <= SHARED_STEP_TOLERANCE_M and slope_diff.size > 0 and item["perpendicular_slope_max_diff_deg"] <= SHARED_SLOPE_TOLERANCE_DEG
        ok = ok and field_residual.size > 0 and realm_residual.size > 0 and max(field_residual.max(), realm_residual.max()) <= SHARED_ANALYTIC_TOLERANCE_UNITS
        detail["gates"][p["gate_id"]] = item
        passed = passed and ok
    return make_check("h", "shared_edge_u16_layers_and_connector_continuity", passed, unit_diff,
                      f"u16 0, edge layer 0, band layer <= {SHARED_LAYER_TOLERANCE}, floor step <= {SHARED_STEP_TOLERANCE_M} m, slope <= {SHARED_SLOPE_TOLERANCE_DEG} deg, analytic <= {SHARED_ANALYTIC_TOLERANCE_UNITS} unit", detail)


def check_clear(L, f):
    count = 0
    for tr in L.ism.values():
        count += int(np.sum(is_in_mask(f["clear"], np.array([t[0] for t in tr]) / 100.0, np.array([t[1] for t in tr]) / 100.0)))
    for e in L.actors + L.planes + L.lights + L.niagara:
        count += int(bool(is_in_mask(f["clear"], e["loc"][0] / 100.0, e["loc"][1] / 100.0)))
    return make_check("i", "realm_scatter_inside_connector_clear_zone", count == 0, count, "0")


def walk_quad_labels(f, cfg):
    walkable = rp.walkable_quads(f["h"]) & (rp.quad_center_heights(f["h"]) >= cfg["fall_recovery_height_local_cm"] / 100.0)
    return rp.quad_components(walkable)


def quad_label_at(labels, x, y):
    return int(labels[int(math.floor(y + HALF)), int(math.floor(x + HALF))])


def check_connector_walls(f, realm_id, cfg, plazas):
    cx, cy, z_offset = realm_frame(realm_id)
    labels = walk_quad_labels(f, cfg)
    QX, QY = f["Xw"][:-1, :-1] + 0.5, f["Yw"][:-1, :-1] + 0.5
    centers = rp.quad_center_heights(f["h"])
    detail = {}
    passed = True
    outer_values = []
    for p in plazas:
        piece = crossing_piece(p["gate_id"])
        s_start, s_end = piece["stamp_s_m"]
        s_low = max(piece["boundary_crossing"]["s_m"] + WALL_CHECK_INSET_M, s_start + rp.OPEN_WALL_BLEND_M)
        s_high = s_end - rp.OPEN_WALL_BLEND_M
        stations = np.arange(s_low, s_high + 1e-6, 1.0)
        stations = stations[rp.inward_end_share(piece, stations, s_start, s_end) <= 0.0]
        lateral = np.arange(rp.WALL_TOP_OUTER_M + 0.5, rp.BAND_OUTER_M, 0.5) + piece["widen_m"]
        S, Lat = np.meshgrid(stations, lateral, indexing="ij")
        side_minimum = []
        for sign in (-1.0, 1.0):
            gx, gy = rp.to_grid_m(piece, S, sign * Lat)
            lx, ly = gx - cx, gy - cy
            inside = np.all((np.abs(lx) <= HALF) & (np.abs(ly) <= HALF), axis=1)
            band = sample_bilinear(f["slope"], lx.ravel(), ly.ravel()).reshape(S.shape)
            if inside.any():
                side_minimum.append(float(band.max(axis=1)[inside].min()))
        s, _ = rp.passage_coords(piece, QX, QY)
        wall_top = rp.wall_top_cells(piece, QX, QY, centers, z_offset) & (s >= s_low)
        comp = quad_label_at(labels, *gate_entry_local(realm_id, p["gate_id"]))
        reachable = int(np.sum(labels[wall_top] == comp)) if comp else 0
        minimum = min(side_minimum) if side_minimum else 0.0
        outer_values.append(minimum)
        detail[f"{p['gate_id']}/{piece['name']}"] = {"stations_s_m": [round(float(stations.min()), 2), round(float(stations.max()), 2)] if stations.size else None,
                                                     "outer_band_min_of_max_slope_deg": round(minimum, 2), "wall_top_quads": int(wall_top.sum()),
                                                     "wall_top_reachable_from_entry": reachable, "entry_component": comp}
        passed = passed and minimum >= OUTER_SLOPE_MIN_DEG and reachable == 0 and comp > 0
    side = f["promontory_side"]
    if side.any():
        side_min = round(float(f["slope"][side].min()), 2)
        detail["promontory_side"] = {"cells": int(side.sum()), "min_slope_deg": side_min, "p50_slope_deg": round(float(np.percentile(f["slope"][side], 50)), 2)}
        outer_values.append(side_min)
        passed = passed and side_min >= OUTER_SLOPE_MIN_DEG
    worst = min(outer_values) if outer_values else 0.0
    return make_check("j", "connector_outer_band_slope_and_wall_top_unreachable", passed, round(worst, 2), f">= {OUTER_SLOPE_MIN_DEG} deg and 0 far wall-top quads reachable (quad triangle slope <= {rp.WALK_MAX_SLOPE_DEG} deg flood, camera-inward end cut excluded)", detail)


def world_terrain_m(field, f, realm_id, x_m, y_m):
    cx, cy, z_offset = realm_frame(realm_id)
    x_m = np.asarray(x_m, dtype=np.float64)
    y_m = np.asarray(y_m, dtype=np.float64)
    inside_field = (np.abs(x_m) <= FIELD_HALF_M) & (np.abs(y_m) <= FIELD_HALF_M)
    field_height = gav.sample_bilinear(field["height_m"], x_m, y_m)
    realm_height = sample_bilinear(f["h"], x_m - cx, y_m - cy) + z_offset
    return np.where(inside_field, field_height, realm_height)


def check_connector_prop_floors(realm_id, f, field, plazas):
    detail = {}
    worst = 0.0
    for p in plazas:
        gate_id = p["gate_id"]
        for name_prefix, shaft, piece, props, base_z_m in connector_prop_groups(gate_id):
            if shaft is not None:
                floors = [prop for prop in props if prop["name"].startswith("Floor_")]
                if not floors:
                    continue
                points = np.array([rp.lift_prop_position_m(shaft, prop, base_z_m) for prop in floors])
                error = np.abs(world_terrain_m(field, f, realm_id, points[:, 0], points[:, 1]) - shaft["bottom_z_m"])
                error = np.maximum(error, np.abs(points[:, 2] - rp.LIFT_PROP_FLOOR_LIFT_M - shaft["bottom_z_m"]))
            else:
                s_values = np.array([prop["s"] for prop in props])
                low = piece["stamp_s_m"][0] + rp.OPEN_FLOOR_BLEND_M
                high = piece_floor_end(piece) - rp.OPEN_FLOOR_BLEND_M
                s_values = s_values[(s_values >= low) & (s_values <= high)]
                if not s_values.size:
                    continue
                gx, gy = rp.to_grid_m(piece, s_values, np.zeros_like(s_values))
                error = np.abs(world_terrain_m(field, f, realm_id, gx, gy) - rp.floor_height(piece, s_values))
            key = f"{gate_id}/{name_prefix or 'Main_'}{'shaft_floor' if shaft is not None else piece['name']}"
            detail[key] = {"props": int(error.size), "max_floor_diff_m": round(float(error.max()), 5)}
            worst = max(worst, float(error.max()))
    return make_check("l", "connector_prop_floor_matches_landscape_m", worst <= FIELD_FLOOR_TOLERANCE_M, round(worst, 5), f"<= {FIELD_FLOOR_TOLERANCE_M}", detail)


def validate_realm(cfg, f, L, islands, field, plazas, layers, realm_id):
    rid = cfg["realm_id"]
    h, slope = f["h"], f["slope"]
    checks = [check_plaza_flatness(f, plazas), check_connector_floors(f, plazas)]

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
    checks.append(check_floods(cfg, f, walkable, plazas, realm_id))
    if islands:
        labels, comp = flood_component(walkable, *gate_entry_local(realm_id, plazas[0]["gate_id"]))
        island_detail = {s.name: reached(labels, comp, s.x, s.y, 3.0) for s in islands}
        checks.append(make_check("c2", "islands_unreachable", not any(island_detail.values()), sum(island_detail.values()), "0", island_detail))
    checks.append(check_rim_leak(f, realm_id, plazas))
    checks.append(check_adjacency(realm_id))
    checks.append(check_placements(L, realm_id))
    checks.append(check_connector_geometry(realm_id, L, f, plazas))
    checks.append(check_shared_edge(realm_id, f, layers, field, plazas))
    checks.append(check_clear(L, f))
    checks.append(check_connector_walls(f, realm_id, cfg, plazas))
    checks.append(check_connector_prop_floors(realm_id, f, field, plazas))
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


def summarize(L, layers, f, connector_props, cleared, light_report):
    by_key = {}
    for (key, _, _), tr in L.ism.items():
        by_key[key] = by_key.get(key, 0) + len(tr)
    return {
        "ism_instances_by_key": dict(sorted(by_key.items())),
        "ism_instances_total": sum(by_key.values()),
        "ism_cells": len(L.ism),
        "actors": len(L.actors), "planes": len(L.planes), "lights": len(L.lights), "niagara": len(L.niagara), "markers": len(L.markers),
        "connector_props": {key: len(items) for key, items in connector_props.items()},
        "connector_lights_off_lift_window": light_report,
        "connector_items_cleared": cleared,
        "tiles_skipped_overlap": L.skipped_tiles,
        "tiles_clipped_to_box": L.clipped_tiles,
        "layer_ratio_percent": {name: round(float(layers[name].mean()) * 100.0, 2) for name in LAYER_NAMES},
        "height_range_m": [round(float(f["h"].min()), 2), round(float(f["h"].max()), 2)],
    }


def landscape_origin_offset_m(realm_id):
    cx_cm, cy_cm, _ = realm_center_cm(realm_id)
    offset = (cx_cm / 100.0 - HALF + gav.HALF, cy_cm / 100.0 - HALF + gav.HALF)
    if not rp.is_integer_point(offset):
        raise ValueError(f"{realm_id}: landscape origin offset {offset} is not integer meters")
    return [offset[0], offset[1], 0.0, 0.0]


def landscape_material_entry(cfg, realm_id):
    vectors, scalars = region_blend_parameters()
    vectors.update(cfg["landscape_material"]["vectors"])
    vectors[LANDSCAPE_ORIGIN_OFFSET_PARAMETER] = landscape_origin_offset_m(realm_id)
    scalars.update(cfg["landscape_material"]["scalars"])
    asset = f"/Game/World/Landscape/MI_TD_Landscape_{realm_id}"
    return {"asset": asset, "parent": LANDSCAPE_PARENT, "vectors": vectors, "scalars": scalars}


def build_layout(cfg, seed, out_dir, L, gates, connector_props, validation, summary):
    rid = cfg["realm_id"]
    cx, cy, cz = realm_center_cm(rid)
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
    nav_z = cfg["nav_z_local_cm"]
    edge = rp.shared_edge(rid)
    atmosphere = dict(cfg["atmosphere"], fog_height_cm=cfg["atmosphere"]["fog_height_cm"] + cz)
    landscape_material = landscape_material_entry(cfg, rid)
    return {
        "generator_version": GENERATOR_VERSION,
        "seed": seed,
        "landscape": {
            "location": [cx, cy, cz], "scale": [100.0, 100.0, gav.Z_SCALE], "components_x": COMPONENTS, "components_y": COMPONENTS,
            "sections_per_component": 1, "quads_per_section": QUADS_PER_SECTION,
            "heightmap": os.path.join(out_dir, "height.r16"),
            "layers": [{"name": n, "path": os.path.join(out_dir, f"layer_{n}.r8")} for n in LAYER_NAMES],
            "material": landscape_material["asset"], "world_partition_grid_size": 2,
        },
        "ism": ism_out, "actors": L.actors + connector_props["actors"], "planes": L.planes + connector_props["planes"], "lights": L.lights + connector_props["lights"],
        "niagara": L.niagara, "markers": L.markers,
        "realm": {
            "realm_id": rid,
            "display_name_ko": cfg["display_name_ko"],
            "center_cm": [cx, cy, cz],
            "half_extent_cm": list(REALM_HALF_EXTENT_CM),
            "fall_recovery_height_cm": cfg["fall_recovery_height_local_cm"] + cz,
            "transition_fade_color": cfg["transition_fade_color"],
            "atmosphere": atmosphere,
            "post_process": {"priority": pp["priority"], "box_min_cm": [cx - hx, cy - hy, pp["z_local_cm"][0] + cz], "box_max_cm": [cx + hx, cy + hy, pp["z_local_cm"][1] + cz], "settings": pp["settings"]},
            "landscape_material": landscape_material,
            "materials": [{"asset": realm_material_path(rid, name), "parent": parent, "vectors": vectors, "scalars": scalars} for name, (parent, vectors, scalars) in cfg["materials"].items()],
            "gates": gates,
            "gameplay_data_layer": rp.REALM_GAMEPLAY_DATA_LAYER[rid],
            "shared_edge": {"field_edge": edge["name"], "axis": edge["axis"], "value_cm": edge["value"] * 100.0, "range_cm": [v * 100.0 for v in edge["range"]], "band_m": rp.REALM_EDGE_BAND_M},
            "nav_bounds": {"box_min_cm": [cx - NAV_HALF_CM, cy - NAV_HALF_CM, nav_z[0] + cz], "box_max_cm": [cx + NAV_HALF_CM, cy + NAV_HALF_CM, nav_z[1] + cz]},
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


def write_preview(out_path, cfg, f, layers, L, islands, plazas):
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
    draw_shared_edge(dr, px, rid)
    for p in plazas:
        draw_connector(dr, px, s, rid, p, font)
    for isl in islands:
        x, y = px(isl.x, isl.y)
        dr.text((x - 12, y - 6), "isl", fill=(90, 90, 110), font=font)
    dr.text((10, 8), f"{rid} seed {L.seed}  504m  +X right / +Y up  center {rp.REALM_PLACEMENT[rid]['center_m']} z {rp.REALM_PLACEMENT[rid]['z_offset_m']}", fill=(255, 255, 255), font=title_font, stroke_width=2, stroke_fill=(0, 0, 0))
    dr.text((10, 34), "cyan=shared field edge  magenta=connector floor/walls  green=plaza(r14)  red=clearing r20  yellow=landmark  lines=road", fill=(230, 230, 230), font=font, stroke_width=2, stroke_fill=(0, 0, 0))
    img.save(out_path)


def draw_shared_edge(dr, px, realm_id):
    edge = rp.shared_edge(realm_id)
    cx, cy, _ = realm_frame(realm_id)
    r0, r1 = edge["range"]
    if edge["axis"] == "y":
        points = [px(r0 - cx, edge["value"] - cy), px(r1 - cx, edge["value"] - cy)]
    else:
        points = [px(edge["value"] - cx, r0 - cy), px(edge["value"] - cx, r1 - cy)]
    dr.line(points, fill=(90, 240, 255), width=5)


def draw_connector(dr, px, s, realm_id, plaza, font):
    cx, cy, _ = realm_frame(realm_id)
    for piece in rp.pieces_of_gate(plaza["gate_id"]):
        stations = np.arange(0.0, piece_floor_end(piece) + 0.5, 1.0)
        for lateral, color, width in ((0.0, (255, 60, 220), 3), (rp.FLOOR_HALF_WIDTH_M + piece["widen_m"], (255, 170, 240), 1), (-rp.FLOOR_HALF_WIDTH_M - piece["widen_m"], (255, 170, 240), 1),
                                      (rp.WALL_TOP_OUTER_M + piece["widen_m"], (255, 60, 220), 1), (-rp.WALL_TOP_OUTER_M - piece["widen_m"], (255, 60, 220), 1)):
            gx, gy = rp.to_grid_m(piece, stations, np.full_like(stations, lateral))
            dr.line([px(float(x) - cx, float(y) - cy) for x, y in zip(gx, gy)], fill=color, width=width)
    r14 = GATE_FLAT_RADIUS_M * s
    bx, by = px(*plaza["plaza"])
    dr.ellipse((bx - r14, by - r14, bx + r14, by + r14), outline=(120, 255, 140), width=2)
    dr.ellipse((bx - 4, by - 4, bx + 4, by + 4), fill=(120, 255, 140))
    dr.text((bx - 90, by + r14 + 2), f"GATE {plaza['gate_id']}", fill=(255, 170, 240), font=font, stroke_width=2, stroke_fill=(0, 0, 0))


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
    plazas = gate_plazas(realm_id)
    islands = []
    if realm_id == "SunkenStarfield":
        f_pre = build_sunken_terrain(cfg, stage_rng(seed, 1), realm_id, field, plazas)
    else:
        shape_rng = stage_rng(seed, 4)
        shapes = pale_mesa_shapes(cfg, shape_rng)
        bridges = pale_bridge_segments(cfg, shapes)
        islands = pale_islands(cfg, shape_rng, shapes, bridges)
        f_pre = build_pale_terrain(cfg, stage_rng(seed, 1), shapes, islands, bridges, realm_id, field, plazas)
    carved = carve_realm_connectors(f_pre["h"], f_pre["Xw"], f_pre["Yw"], realm_id)
    f_pre["passage_footprint"] = carved["footprint"]
    f_pre["clear"] = ndimage.binary_dilation(carved["footprint"] > 0.0, iterations=CLEAR_MARGIN_CELLS)
    f = finish_realm_terrain(f_pre, carved, realm_id, field)
    L = TDRealmLayout(f_pre, stage_rng(seed, 3), realm_id)
    if realm_id == "SunkenStarfield":
        place_sunken(L, stage_rng(seed, 3), f_pre, cfg, plazas)
    else:
        place_pale(L, stage_rng(seed, 3), f_pre, cfg, shapes, islands, bridges, plazas)
    f["forest"] = f_pre["forest"]
    L.f = f
    L.seed = seed
    place_arrival_markers(L, plazas)
    layers = build_sunken_layers(f, stage_rng(seed, 2)) if realm_id == "SunkenStarfield" else build_pale_layers(f, stage_rng(seed, 2))
    layers = blend_shared_layers(layers, f, realm_id, field)
    cleared = clear_connector_items(L, f["clear"])
    topdown_report = apply_topdown_rules(L, f, cfg, plazas)
    connector_props, light_report = build_connector_props(realm_id)
    gates = build_gate_entries(realm_id)
    validation = validate_realm(cfg, f, L, islands, field, plazas, layers, realm_id)
    summary = summarize(L, layers, f, connector_props, cleared, light_report)
    write_rasters(out_dir, f["h"], layers)
    write_preview(os.path.join(out_dir, "preview.png"), cfg, f, layers, L, islands, plazas)
    shift_to_world(L, realm_center_cm(realm_id))
    layout = build_layout(cfg, seed, out_dir, L, gates, connector_props, validation, summary)
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
    print(f"height m {summary['height_range_m']}  ism {summary['ism_instances_total']} in {summary['ism_cells']} cells  actors {summary['actors']}  planes {summary['planes']}  lights {summary['lights']}  niagara {summary['niagara']}  markers {summary['markers']}  connector_props {summary['connector_props']}  connector_items_cleared {summary['connector_items_cleared']}  tiles_skipped {summary['tiles_skipped_overlap']}")
    print("ism by key: " + ", ".join(f"{k}={v}" for k, v in summary["ism_instances_by_key"].items()))
    print("layers %: " + ", ".join(f"{k}={v}" for k, v in summary["layer_ratio_percent"].items()))
    for c in validation["checks"]:
        print(f"  [{'PASS' if c['passed'] else 'FAIL'}] {c['id']} {c['name']}: {c['value']} (limit {c['limit']}) {json.dumps(c['detail'], ensure_ascii=False)}")
    for g in layout["realm"]["gates"]:
        print(f"  gate {g['gate_id']} {g['gate_kind']}: field_return {g['field_return']} arrival {g['arrival']} approach {g['approach_length_cm']} departure {g['departure_length_cm']}")
    names = ["height.r16"] + [f"layer_{n}.r8" for n in LAYER_NAMES] + ["layout.json"]
    for n in names:
        print(f"  sha256 {n} {sha256_of(os.path.join(out_dir, n))}")
    print(f"validation: {'PASS' if validation['passed'] else 'FAIL'}")


def main():
    ap = argparse.ArgumentParser(description="Generate realm regions (SunkenStarfield, PaleHeights) beside the field for the continuous world.")
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
