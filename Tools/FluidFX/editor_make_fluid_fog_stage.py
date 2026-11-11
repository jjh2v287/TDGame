"""실행 환경: TDGame 언리얼 에디터 안에서 FluidFX 안개 무대 LV_TDFluidFogStage(구역 0~6 은 X축 30 m 간격, 구역 7A 대형 창 + 200 m 정적 상자는 Y +250 m 띠, 7B 수동 7변형 진열은 Y −150 m 띠, 구역 8 은 Y −350 m 띠: 8A 가르기 FogMist·8B 휘젓기 FogMistStir 에 교반기, 8C 감싸기 타일 FogTileSim)와 DA 7개(Smoke 대리 3·FogMist 2·FogMistStir 1·FogTileSim 1)를 만든다. 먼저 editor_make_fluid_fog_material.py 와 FX-8n 무대 스크립트(교반기 BP_TDFluidStageStirrer)가 성공해 있어야 하고, 모양 정본 병합 뒤에 돌린다.
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_fog_stage.py (기존 DA·맵을 갱신할 때는 Saved/FluidFX/fluid_fog_stage_request.json 에 {"overwrite": true})
출력: /Game/FluidFX/Presets/DA_TDFluid_FogAmbient·FogRoom·FogTrail·FogCharacter·FogCut·FogStir·FogTileSync,/Game/FluidFX/Maps/LV_TDFluidFogStage, /Game/FluidFX/Materials/M_TD_FluidFogStageFloor·M_TD_FluidFogStageWall(없을 때만 만듦), Saved/FluidFX/fluid_fog_stage.json(preset_tuning_sha256·material_look_sha256, check_fluid_preset_data.py --stage-report 형식)
상태: 미검증(2026-10-25 FX-10r 작성: py_compile·가짜 unreal·validate_stage_layout 만. 2026-10-26 에디터 실행 status ok·액터 37·DA 4·바닥·벽 재질 2, stage-report 오류 0, 화면 판정 전). 미저장 작업이 있으면 중단하고, 시작 때 프리셋 정본 검사·다시 읽기를 한다. 볼륨 상자 높이 1300 cm(돌출 범위 1000 + 윗면 페이드 300, 아래면 = 바닥)·광원·높이 안개 값은 무대 상수다(D-64 ② 편차, 보정은 룩데브 props). 시뮬 액터는 따라가기를 끄고 활성 볼륨(2000 cm)으로 가까운 것만 깨운다(따라가기를 켜면 안개판이 모든 구역으로 따라옴, 반박 M9). MPC 쓰기 액터는 FogAmbient 하나다. 2026-10-26 FX-10c: 7A FogAmbient 종류를 Smoke 대리에서 FogMist(40 m 창)로 바꾸고 구역 8(TileBandFloor)을 더함, 배치 점검을 종류별 창 × 상자 배율로 일반화(이웃 중심 거리 > 활성 거리 + 두 반폭), 종류 값(창·감싸기·상자 배율 상한)은 에셋 변경 전에 점검. 에디터 실행 전(py_compile·가짜 unreal·validate_stage_layout 만). 2026-10-27 파동 2 에셋 단계: 에디터 실행 status ok(덮어쓰기, 액터 43·DA 7, MI 15 단언 통과), stage-report stale 0, 화면 판정 전. 2026-10-31: 교반기 회전 속도가 피벗 쓰기 뒤 BP 구성 스크립트 재실행으로 유실되고(기본 90°/s) 위치 인수 Rotator(0, v, 0)가 pitch 였던 것을 particle_stage.set_component_properties(한 번에 쓰기·새 핸들로 다시 읽기, 다르면 FAIL)와 yaw 키워드로 고침, 맵 재생성 필요. 2026-11-11 Q4-1 2판: 구역 9 기복(Y +550 m, 바닥 ReliefFloor 120 m, FogRelief = FOG_MIST·MI_TD_FluidFog_Sim·40 m 창, 원뿔 언덕 ReliefHillLow 400·ReliefHillTall 1200 과 바닥에 태그 TDFluidTerrain, 태그 없는 돌출물 판 윗면 400 + 기둥 2, 평지 띠 X 7200~10800·Y 53300~54000)을 더하고 validate_stage_layout 에 바닥 간격·창 안·평지 띠 비움·돌출물-언덕 200 cm·캡처 높이·볼륨 경계 구 덮음(find_fog_sphere_coverage_problems, 창 모서리는 윗면만)을 더함, 보고서 relief 절. --self-test 는 unreal 없이 덮음 fixture 3개와 배치 점검을 돈다. 에디터 실행 전(py_compile·가짜 unreal·--self-test 만). 2026-11-11 Q4-1b: 구역 10 ReliefTile(Y +720 m, 태그 바닥 ReliefTileFloor 130 m, FogReliefTile = FOG_TILE_SIM·MI_TD_FluidFog_TileSync, 태그 원뿔 ReliefTileHillIn(창 안)·ReliefTileHillOut(창·옛 캡처 밖, 상자 안) 밑지름 1000·높이 500, 평지 띠 X 7500~13500·Y 70300~71500, 촬영 폰 (10600, 71000))을 더하고, 종류 표에 따라가기 여부, 감싸기 캡처 경계 구 덮음(find_capture_sphere_cover_problems)·다른 구역 태그 지형 캡처 섞임(find_capture_crosstalk_problems)·구역 10 판별(창 안·옛 캡처 밖·폰 X·좌우 대칭)·경계 구 기준 간격 점검과 보고서 relief_tile 절, --self-test fixture 5개를 더함. 에디터 실행 전(py_compile·--self-test 만).
"""
import hashlib
import importlib.util
import json
import math
import os
import sys

try:
    import unreal
except ModuleNotFoundError:
    unreal = None


FLUID_ROOT = "/Game/FluidFX"
MATERIAL_DIR = f"{FLUID_ROOT}/Materials"
PRESET_DIR = f"{FLUID_ROOT}/Presets"
MAP_DIR = f"{FLUID_ROOT}/Maps"
STAGE_MAP_PATH = f"{MAP_DIR}/LV_TDFluidFogStage"
FLOOR_MAT_PATH = f"{MATERIAL_DIR}/M_TD_FluidFogStageFloor"
WALL_MAT_PATH = f"{MATERIAL_DIR}/M_TD_FluidFogStageWall"
COLLECTION_PATH = f"{MATERIAL_DIR}/MPC_TD_FluidFogWindow"
STIRRER_BLUEPRINT_PATH = f"{FLUID_ROOT}/Blueprints/BP_TDFluidStageStirrer"
GAME_MODE_PATH = "/Game/Combat/Blueprints/BP_TDCombatGameMode"
CUBE_MESH_PATH = "/Engine/BasicShapes/Cube"
CONE_MESH_PATH = "/Engine/BasicShapes/Cone"
TERRAIN_ACTOR_TAG = "TDFluidTerrain"
CUBE_SIZE_CM = 100.0
FLUID_ACTOR_CLASS_PATH = "/Script/TDGame.TDFluidLiveActor"
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_fog_stage_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_fog_stage.json")
PRESET_TUNING_RELATIVE_PATH = ("Content", "FluidFX", "PresetData", "TDFluidPresetTuning.json")
MATERIAL_LOOK_RELATIVE_PATH = ("Content", "FluidFX", "PresetData", "TDFluidMaterialLook.json")
PRESET_CHECKER_RELATIVE_PATH = ("Tools", "check_fluid_preset_data.py")
PARTICLE_STAGE_RELATIVE_PATH = ("Tools", "FluidFX", "editor_make_fluid_particle_stage.py")

FLOOR_Z_CM = 0.0
ZONE_SPACING_CM = 3000.0
ZONE_COUNT_ON_AXIS = 7
ZONE_BAND_HALF_WIDTH_CM = 1500.0
ACTIVATION_DISTANCE_CM = 2000.0
FOG_EXTRUDE_RANGE_MAX_CM = 1000.0
FOG_TOP_FADE_RANGE_MAX_CM = 300.0
FOG_BOX_HEIGHT_CM = 1300.0
FOG_VIEW_DISTANCE_CM = 12000.0
HEIGHT_FOG_DENSITY = 0.002
HEIGHT_FOG_SCATTERING_DISTRIBUTION = 0.35
PRESET_KIND_LAYOUTS = {
    "SMOKE": {"window_cm": 700.0, "box_multiplier": 1.0, "wraps": False, "follows_terrain": False},
    "FOG_MIST": {"window_cm": 4000.0, "box_multiplier": 1.0, "wraps": False, "follows_terrain": True},
    "FOG_MIST_STIR": {"window_cm": 4000.0, "box_multiplier": 1.0, "wraps": False, "follows_terrain": True},
    "FOG_TILE_SIM": {"window_cm": 3000.0, "box_multiplier": 3.0, "wraps": True, "follows_terrain": True},
}
EXPECTED_STAGE_INSTANCE_COUNT = 15
ZONE7A_CENTER = (9000.0, 25000.0)
ZONE8_BAND_Y_CM = -35000.0
ZONE8_FLOOR_CENTER = (8500.0, ZONE8_BAND_Y_CM)
ZONE8_FLOOR_SIZE_CM = (32000.0, 12000.0)
ZONE9_CENTER = (9000.0, 55000.0)
ZONE9_FLOOR_SIZE_CM = (12000.0, 12000.0)
RELIEF_FLOOR_LABEL = "ReliefFloor"
RELIEF_FLOOR_MIN_GAP_CM = 1800.0
RELIEF_FLAT_BAND_BOUNDS = (7200.0, 53300.0, 10800.0, 54000.0)
RELIEF_MIN_OVERHANG_HILL_GAP_CM = 200.0
RELIEF_FOG_LAYER_HEIGHT_CM = 60.0
RELIEF_FOG_BELOW_CUT_CM = 50.0
RELIEF_CAPTURE_HEIGHT_ABOVE_CM = 2000.0
RELIEF_BOUNDS_TOLERANCE_CM = 0.5
ZONE10_CENTER = (9000.0, 72000.0)
ZONE10_FLOOR_SIZE_CM = (13000.0, 13000.0)
RELIEF_TILE_FLOOR_LABEL = "ReliefTileFloor"
RELIEF_TILE_FLAT_BAND_BOUNDS = (7500.0, 70300.0, 13500.0, 71500.0)
RELIEF_TILE_FOG_LAYER_HEIGHT_CM = 120.0
RELIEF_TILE_FOG_BELOW_CUT_CM = 20.0
RELIEF_TILE_PAWN_XY = (10600.0, 71000.0)
RELIEF_TILE_SYMMETRY_TOLERANCE_CM = 1.0
HEIGHT_CAPTURE_MARGIN_RATIO = 0.1
WRAPPED_CAPTURE_COVER_MULTIPLIER = math.sqrt(2.0)
PASSIVE_FIELD_SIZE_CM = 20000.0
PASSIVE_DISPLAY_Y_CM = -15000.0
PASSIVE_DISPLAY_SIZE_CM = 1500.0
CYLINDER_BOX_SIZE_CM = 600.0
HOLLOW_BOX_SIZE_CM = 800.0
STIRRER_SPHERE_RADIUS_CM = 50.0
STIRRER_HEIGHT_CM = 60.0
PLAYER_START = (0.0, 0.0, 96.0)

ZONE_LABELS = ("Intro", "Cylinders", "Lab", "Shrine", "Hollow", "Trail", "Character")
FLUID_SPECS = (
    {"name": "FogRoom", "zone": "2", "center": (2 * ZONE_SPACING_CM, 0.0), "kind": "SMOKE", "instance": "MI_TD_FluidFog_Sim", "writes_collection": False},
    {"name": "FogTrail", "zone": "5", "center": (5 * ZONE_SPACING_CM, 0.0), "kind": "SMOKE", "instance": "MI_TD_FluidFog_SimTrail", "writes_collection": False},
    {"name": "FogCharacter", "zone": "6", "center": (6 * ZONE_SPACING_CM, 0.0), "kind": "SMOKE", "instance": "MI_TD_FluidFog_SimCharacter", "writes_collection": False},
    {"name": "FogAmbient", "zone": "7A", "center": ZONE7A_CENTER, "kind": "FOG_MIST", "instance": "MI_TD_FluidFog_Sim", "writes_collection": True},
    {"name": "FogCut", "zone": "8A", "center": (0.0, ZONE8_BAND_Y_CM), "kind": "FOG_MIST", "instance": "MI_TD_FluidFog_Sim", "writes_collection": False},
    {"name": "FogStir", "zone": "8B", "center": (7000.0, ZONE8_BAND_Y_CM), "kind": "FOG_MIST_STIR", "instance": "MI_TD_FluidFog_Sim", "writes_collection": False},
    {"name": "FogTileSync", "zone": "8C", "center": (17000.0, ZONE8_BAND_Y_CM), "kind": "FOG_TILE_SIM", "instance": "MI_TD_FluidFog_TileSync", "writes_collection": False},
    {"name": "FogRelief", "zone": "9", "center": ZONE9_CENTER, "kind": "FOG_MIST", "instance": "MI_TD_FluidFog_Sim", "writes_collection": False},
    {"name": "FogReliefTile", "zone": "10", "center": ZONE10_CENTER, "kind": "FOG_TILE_SIM", "instance": "MI_TD_FluidFog_TileSync", "writes_collection": False},
)
RELIEF_HILL_SPECS = (
    {"label": "ReliefHillLow", "center": (8300.0, 55000.0), "base_diameter_cm": 1600.0, "height_cm": 400.0},
    {"label": "ReliefHillTall", "center": (9800.0, 55500.0), "base_diameter_cm": 1200.0, "height_cm": 1200.0},
)
RELIEF_OVERHANG_SPECS = (
    {"label": "ReliefOverhangPlate", "center": (8000.0, 56300.0), "size": (600.0, 300.0, 20.0), "top_z_cm": 400.0},
    {"label": "ReliefOverhangPillar0", "center": (7750.0, 56300.0), "size": (40.0, 40.0, 380.0), "top_z_cm": 380.0},
    {"label": "ReliefOverhangPillar1", "center": (8250.0, 56300.0), "size": (40.0, 40.0, 380.0), "top_z_cm": 380.0},
)
RELIEF_TILE_HILL_SPECS = (
    {"label": "ReliefTileHillIn", "center": (8900.0, 72600.0), "base_diameter_cm": 1000.0, "height_cm": 500.0, "role": "in"},
    {"label": "ReliefTileHillOut", "center": (12300.0, 72600.0), "base_diameter_cm": 1000.0, "height_cm": 500.0, "role": "out"},
)
STIRRER_SPECS = (
    {"label": "FogTrailStirrer", "fluid": "FogTrail", "orbit_radius_cm": 220.0, "degrees_per_sec": 90.0},
    {"label": "FogCutStirrer", "fluid": "FogCut", "orbit_radius_cm": 600.0, "degrees_per_sec": 30.0},
    {"label": "FogStirStirrer", "fluid": "FogStir", "orbit_radius_cm": 600.0, "degrees_per_sec": 30.0},
)
STATIC_BOX_SPECS = (
    {"label": "CylinderA", "instance": "MI_TD_FluidFog_CylA", "center": (3000.0, -500.0), "size": (CYLINDER_BOX_SIZE_CM, CYLINDER_BOX_SIZE_CM, FOG_BOX_HEIGHT_CM), "shape": 1},
    {"label": "CylinderB", "instance": "MI_TD_FluidFog_CylB", "center": (3000.0, 500.0), "size": (CYLINDER_BOX_SIZE_CM, CYLINDER_BOX_SIZE_CM, FOG_BOX_HEIGHT_CM), "shape": 1},
    {"label": "ShrineGroundFog", "instance": "MI_TD_FluidFog_Passive0", "center": (9000.0, 0.0), "size": (PASSIVE_DISPLAY_SIZE_CM, PASSIVE_DISPLAY_SIZE_CM, FOG_BOX_HEIGHT_CM), "shape": 0},
    {"label": "HollowMass", "instance": "MI_TD_FluidFog_Hollow", "center": (12000.0, 0.0), "size": (HOLLOW_BOX_SIZE_CM, HOLLOW_BOX_SIZE_CM, HOLLOW_BOX_SIZE_CM), "shape": 2},
    {"label": "PassiveField", "instance": "MI_TD_FluidFog_PassiveField", "center": ZONE7A_CENTER, "size": (PASSIVE_FIELD_SIZE_CM, PASSIVE_FIELD_SIZE_CM, FOG_BOX_HEIGHT_CM), "shape": 0},
) + tuple(
    {"label": f"PassiveDisplay{index}", "instance": f"MI_TD_FluidFog_Passive{index}", "center": (index * ZONE_SPACING_CM, PASSIVE_DISPLAY_Y_CM), "size": (PASSIVE_DISPLAY_SIZE_CM, PASSIVE_DISPLAY_SIZE_CM, FOG_BOX_HEIGHT_CM), "shape": 0}
    for index in range(7)
)
LAB_WALLS = (
    ("LabBackWall", (6000.0, -700.0, 200.0), (14.0, 0.4, 4.0)),
    ("LabWestWall", (5300.0, 0.0, 200.0), (0.4, 14.0, 4.0)),
    ("LabEastWall", (6700.0, 0.0, 200.0), (0.4, 14.0, 4.0)),
)
SHRINE_PILLARS = tuple((f"ShrinePillar{index}", (9000.0 + x, y, 250.0), (0.6, 0.6, 5.0)) for index, (x, y) in enumerate(((-400.0, -400.0), (400.0, -400.0), (-400.0, 400.0), (400.0, 400.0))))
LOCAL_LIGHTS = (
    {"label": "LabGreenLight", "location": (5800.0, -300.0, 120.0), "color": (0.25, 1.0, 0.35), "intensity": 2000.0, "radius": 900.0, "volumetric_scattering_intensity": 8.0},
    {"label": "LabBlueLight", "location": (6200.0, 300.0, 120.0), "color": (0.25, 0.45, 1.0), "intensity": 2000.0, "radius": 900.0, "volumetric_scattering_intensity": 4.0},
    {"label": "ShrineWarmLight0", "location": (9000.0, 0.0, 300.0), "color": (1.0, 0.62, 0.3), "intensity": 1500.0, "radius": 1200.0, "volumetric_scattering_intensity": 3.0},
    {"label": "ShrineWarmLight1", "location": (8600.0, 400.0, 150.0), "color": (1.0, 0.5, 0.25), "intensity": 800.0, "radius": 800.0, "volumetric_scattering_intensity": 3.0},
    {"label": "ShrineWarmLight2", "location": (9400.0, -400.0, 150.0), "color": (1.0, 0.5, 0.25), "intensity": 800.0, "radius": 800.0, "volumetric_scattering_intensity": 3.0},
)
FLOOR_SPECS = (
    ("BandFloor", (9000.0, 0.0), (21000.0 + 2.0 * ZONE_BAND_HALF_WIDTH_CM, 2.0 * ZONE_BAND_HALF_WIDTH_CM)),
    ("FieldFloor", ZONE7A_CENTER, (PASSIVE_FIELD_SIZE_CM + 2000.0, PASSIVE_FIELD_SIZE_CM + 2000.0)),
    ("DisplayFloor", (9000.0, PASSIVE_DISPLAY_Y_CM), (21000.0 + PASSIVE_DISPLAY_SIZE_CM * 2.0, PASSIVE_DISPLAY_SIZE_CM * 2.0)),
    ("TileBandFloor", ZONE8_FLOOR_CENTER, ZONE8_FLOOR_SIZE_CM),
    (RELIEF_FLOOR_LABEL, ZONE9_CENTER, ZONE9_FLOOR_SIZE_CM),
    (RELIEF_TILE_FLOOR_LABEL, ZONE10_CENTER, ZONE10_FLOOR_SIZE_CM),
)
TERRAIN_TAGGED_FLOOR_ZONES = {RELIEF_FLOOR_LABEL: "9", RELIEF_TILE_FLOOR_LABEL: "10"}
TERRAIN_TAGGED_FLOOR_LABELS = tuple(TERRAIN_TAGGED_FLOOR_ZONES)
FLOOR_COLOR = (0.03, 0.032, 0.036)
WALL_COLOR = (0.06, 0.06, 0.07)

REQUIRED_UNREAL_NAMES = (
    "get_engine_subsystem",
    "TDFluidPresetTuningSubsystem",
    "TDFluidPresetTuningSubsystem.reload_preset_tuning",
    "TDFluidPresetTuningSubsystem.get_load_errors",
    "TDFluidPresetAsset",
    "TDFluidPresetAsset.build_config_for_preset_kind",
    "TDFluidPresetAsset.resolve_effective_volume_render",
    "TDFluidPresetAsset.build_volume_render_for_preset_kind",
    "TDFluidPresetKind.SMOKE",
    "TDFluidPresetKind.FOG_MIST",
    "TDFluidPresetKind.FOG_MIST_STIR",
    "TDFluidPresetKind.FOG_TILE_SIM",
    "TDFluidAreaAddressMode.WRAP",
    "TDFluidBrushChannel.CHANNEL_R_MUD",
    "EditorAssetLibrary.load_blueprint_class",
    "EditorAssetLibrary.load_asset",
    "EditorLoadingAndSavingUtils.get_dirty_map_packages",
    "EditorLoadingAndSavingUtils.get_dirty_content_packages",
    "SystemLibrary.get_project_directory",
    "MaterialEditingLibrary.recompile_material",
    "MaterialParameterCollection",
    "DataAssetFactory",
    "Material",
    "MaterialFactoryNew",
    "MaterialExpressionConstant",
    "MaterialExpressionConstant3Vector",
    "MaterialProperty.MP_BASE_COLOR",
    "MaterialProperty.MP_ROUGHNESS",
    "RotatingMovementComponent",
    "ExponentialHeightFog",
    "ExponentialHeightFogComponent",
    "DirectionalLight",
    "SkyLight",
    "PointLight",
    "PostProcessVolume",
    "PlayerStart",
    "AutoExposureMethod.AEM_MANUAL",
    "ComponentMobility.STATIC",
    "ComponentMobility.MOVABLE",
    "CollisionEnabled.NO_COLLISION",
)
PRESET_REQUIRED_PROPERTIES = ("preset_kind", "simulation_config", "default_channel", "override_preset_defaults", "volume_material", "volume_mesh", "volume_height_cm", "parameter_collection")
ACTOR_REQUIRED_PROPERTIES = ("should_follow_player_pawn", "should_use_activation_volume", "activation_distance_cm", "interaction_brush_channel", "live_component")
HEIGHT_FOG_PROPERTIES = (
    ("fog_density", HEIGHT_FOG_DENSITY),
    ("enable_volumetric_fog", True),
    ("volumetric_fog_scattering_distribution", HEIGHT_FOG_SCATTERING_DISTRIBUTION),
    ("volumetric_fog_distance", FOG_VIEW_DISTANCE_CM),
)


def zone_center(zone):
    return (zone * ZONE_SPACING_CM, 0.0)


def box_xy_bounds(center, size):
    return (center[0] - 0.5 * size[0], center[1] - 0.5 * size[1], center[0] + 0.5 * size[0], center[1] + 0.5 * size[1])


def rectangle_gap(first, second):
    gap_x = max(second[0] - first[2], first[0] - second[2], 0.0)
    gap_y = max(second[1] - first[3], first[1] - second[3], 0.0)
    return (gap_x * gap_x + gap_y * gap_y) ** 0.5


def is_inside_bounds(inner, outer):
    return inner[0] >= outer[0] and inner[1] >= outer[1] and inner[2] <= outer[2] and inner[3] <= outer[3]


def fluid_visible_half_width(spec):
    layout = PRESET_KIND_LAYOUTS[spec["kind"]]
    return 0.5 * layout["window_cm"] * layout["box_multiplier"]


def fluid_visible_bounds(spec):
    half_width = fluid_visible_half_width(spec)
    return box_xy_bounds(spec["center"], (2.0 * half_width, 2.0 * half_width))


def find_fluid_spacing_problems():
    problems = []
    for first_index, first in enumerate(FLUID_SPECS):
        for second in FLUID_SPECS[first_index + 1:]:
            distance = math.dist(first["center"], second["center"])
            required = ACTIVATION_DISTANCE_CM + fluid_visible_half_width(first) + fluid_visible_half_width(second)
            if distance <= required:
                problems.append(f"{first['name']}·{second['name']} 중심 거리 {distance:.0f} ≤ 활성 거리 + 두 반폭 {required:.0f}(이웃 시뮬도 깨어남)")
    return problems


def find_zone8_problems(band, display_bounds):
    problems = []
    floor = box_xy_bounds(ZONE8_FLOOR_CENTER, ZONE8_FLOOR_SIZE_CM)
    for spec in FLUID_SPECS:
        if spec["zone"].startswith("8") and not is_inside_bounds(fluid_visible_bounds(spec), floor):
            problems.append(f"{spec['name']}: 보이는 범위가 TileBandFloor 밖")
    for label, bounds in [("구역 띠", band)] + display_bounds:
        if rectangle_gap(floor, bounds) < FOG_VIEW_DISTANCE_CM:
            problems.append(f"TileBandFloor·{label} 간격 {rectangle_gap(floor, bounds):.0f} < 보이는 거리 {FOG_VIEW_DISTANCE_CM}")
    return problems


def find_stirrer_problems():
    problems = []
    fluids_by_name = {spec["name"]: spec for spec in FLUID_SPECS}
    for stirrer in STIRRER_SPECS:
        half_window = 0.5 * PRESET_KIND_LAYOUTS[fluids_by_name[stirrer["fluid"]]["kind"]]["window_cm"]
        if stirrer["orbit_radius_cm"] + STIRRER_SPHERE_RADIUS_CM > half_window:
            problems.append(f"{stirrer['label']}: 궤도 반경 {stirrer['orbit_radius_cm']} + 구 반경이 창 반폭 {half_window} 밖")
    return problems


def rectangle_corners(bounds):
    return ((bounds[0], bounds[1]), (bounds[2], bounds[1]), (bounds[0], bounds[3]), (bounds[2], bounds[3]))


def intersect_bounds(first, second):
    bounds = (max(first[0], second[0]), max(first[1], second[1]), min(first[2], second[2]), min(first[3], second[3]))
    return bounds if bounds[0] <= bounds[2] and bounds[1] <= bounds[3] else None


def rectangle_circle_gap(bounds, center, radius):
    nearest = (min(max(center[0], bounds[0]), bounds[2]), min(max(center[1], bounds[1]), bounds[3]))
    return math.dist(nearest, center) - radius


def relief_terrain_features():
    hills = [{"name": spec["label"], "shape": "cone", "center": spec["center"], "radius_cm": 0.5 * spec["base_diameter_cm"], "base_z_cm": FLOOR_Z_CM, "top_z_cm": FLOOR_Z_CM + spec["height_cm"]} for spec in RELIEF_HILL_SPECS]
    boxes = [{"name": spec["label"], "shape": "box", "center": spec["center"], "size_xy": spec["size"][:2], "base_z_cm": FLOOR_Z_CM, "top_z_cm": FLOOR_Z_CM + spec["top_z_cm"]} for spec in RELIEF_OVERHANG_SPECS]
    return hills + boxes


def feature_xy_bounds(feature):
    if feature["shape"] == "cone":
        return box_xy_bounds(feature["center"], (2.0 * feature["radius_cm"], 2.0 * feature["radius_cm"]))
    return box_xy_bounds(feature["center"], feature["size_xy"])


def farthest_feature_distance(feature, window_center_xy, window_bounds):
    clipped = intersect_bounds(feature_xy_bounds(feature), window_bounds)
    if clipped is None:
        return None
    farthest_clipped_corner = max(math.dist(corner, window_center_xy) for corner in rectangle_corners(clipped))
    if feature["shape"] == "cone":
        return min(math.dist(feature["center"], window_center_xy) + feature["radius_cm"], farthest_clipped_corner)
    return farthest_clipped_corner


def sphere_half_chord(radius, distance):
    if distance > radius:
        return None
    return math.sqrt(radius * radius - distance * distance)


def find_fog_sphere_coverage_problems(window_center_xy, window_size_xy, box_multiplier, box_height_cm, owner_z_cm, layer_height_cm, below_cut_cm, terrain_features):
    radius = math.sqrt((0.5 * box_multiplier * window_size_xy[0]) ** 2 + (0.5 * box_multiplier * window_size_xy[1]) ** 2 + (0.5 * box_height_cm) ** 2)
    center_z = owner_z_cm + 0.5 * box_height_cm
    window_bounds = box_xy_bounds(window_center_xy, window_size_xy)
    problems = []
    corner_distance = max(math.dist(corner, window_center_xy) for corner in rectangle_corners(window_bounds))
    corner_chord = sphere_half_chord(radius, corner_distance)
    corner_top = center_z + corner_chord if corner_chord is not None else None
    if corner_top is None or corner_top < owner_z_cm + layer_height_cm:
        problems.append(f"창 모서리(거리 {corner_distance:.0f}): 구 윗면 {corner_top} < 바닥 + 층 {owner_z_cm + layer_height_cm:.0f}")
    for feature in terrain_features:
        distance = farthest_feature_distance(feature, window_center_xy, window_bounds)
        if distance is None:
            continue
        half_chord = sphere_half_chord(radius, distance)
        if half_chord is None:
            problems.append(f"{feature['name']}: 먼 밑면 점 거리 {distance:.0f} > 구 반경 {radius:.0f}")
            continue
        failures = []
        if center_z + half_chord < feature["top_z_cm"] + layer_height_cm:
            failures.append(f"구 윗면 {center_z + half_chord:.0f} < 높이 + 층 {feature['top_z_cm'] + layer_height_cm:.0f}")
        if center_z - half_chord > feature["base_z_cm"] - below_cut_cm:
            failures.append(f"구 아랫면 {center_z - half_chord:.0f} > 밑면 − 차단 {feature['base_z_cm'] - below_cut_cm:.0f}")
        if failures:
            problems.append(f"{feature['name']}(먼 밑면 점 거리 {distance:.0f}, 구 반경 {radius:.0f}): " + ", ".join(failures))
    return problems


def relief_fluid_spec():
    return next(spec for spec in FLUID_SPECS if spec["zone"] == "9")


def find_relief_sphere_coverage_problems(terrain_features):
    relief = relief_fluid_spec()
    layout = PRESET_KIND_LAYOUTS[relief["kind"]]
    return find_fog_sphere_coverage_problems(relief["center"], (layout["window_cm"], layout["window_cm"]), layout["box_multiplier"], FOG_BOX_HEIGHT_CM, FLOOR_Z_CM, RELIEF_FOG_LAYER_HEIGHT_CM, RELIEF_FOG_BELOW_CUT_CM, terrain_features)


def find_relief_problems():
    problems = []
    window = fluid_visible_bounds(relief_fluid_spec())
    relief_floor = box_xy_bounds(ZONE9_CENTER, ZONE9_FLOOR_SIZE_CM)
    for label, center, size in FLOOR_SPECS:
        if label == RELIEF_FLOOR_LABEL:
            continue
        gap = rectangle_gap(relief_floor, box_xy_bounds(center, size))
        if gap < RELIEF_FLOOR_MIN_GAP_CM:
            problems.append(f"{RELIEF_FLOOR_LABEL}·{label} 간격 {gap:.0f} < {RELIEF_FLOOR_MIN_GAP_CM}")
    features = relief_terrain_features()
    for feature in features:
        if not is_inside_bounds(feature_xy_bounds(feature), window):
            problems.append(f"{feature['name']}: 기복 창 밖")
        band_gap = rectangle_circle_gap(RELIEF_FLAT_BAND_BOUNDS, feature["center"], feature["radius_cm"]) if feature["shape"] == "cone" else rectangle_gap(feature_xy_bounds(feature), RELIEF_FLAT_BAND_BOUNDS)
        if band_gap <= 0.0:
            problems.append(f"{feature['name']}: 평지 띠와 겹침")
    for overhang in RELIEF_OVERHANG_SPECS:
        bounds = box_xy_bounds(overhang["center"], overhang["size"][:2])
        for hill in RELIEF_HILL_SPECS:
            gap = rectangle_circle_gap(bounds, hill["center"], 0.5 * hill["base_diameter_cm"])
            if gap < RELIEF_MIN_OVERHANG_HILL_GAP_CM:
                problems.append(f"{overhang['label']}·{hill['label']} 밑면 간격 {gap:.0f} < {RELIEF_MIN_OVERHANG_HILL_GAP_CM}")
    tallest = max(feature["top_z_cm"] for feature in features)
    if tallest + RELIEF_FOG_LAYER_HEIGHT_CM >= FLOOR_Z_CM + RELIEF_CAPTURE_HEIGHT_ABOVE_CM:
        problems.append(f"가장 높은 요소 {tallest} + 층 {RELIEF_FOG_LAYER_HEIGHT_CM} ≥ 캡처 높이 {RELIEF_CAPTURE_HEIGHT_ABOVE_CM}")
    problems.extend(find_relief_sphere_coverage_problems(features))
    return problems


def fluid_spec_in_zone(zone):
    return next(spec for spec in FLUID_SPECS if spec["zone"] == zone)


def fluid_window_bounds(spec, scale=1.0):
    window = PRESET_KIND_LAYOUTS[spec["kind"]]["window_cm"] * scale
    return box_xy_bounds(spec["center"], (window, window))


def fog_bounding_sphere_radius(spec):
    half_box = fluid_visible_half_width(spec)
    return math.sqrt(2.0 * half_box * half_box + (0.5 * FOG_BOX_HEIGHT_CM) ** 2)


def height_capture_width(spec, cover_multiplier=WRAPPED_CAPTURE_COVER_MULTIPLIER):
    layout = PRESET_KIND_LAYOUTS[spec["kind"]]
    margin_scale = 1.0 + 2.0 * HEIGHT_CAPTURE_MARGIN_RATIO
    if not layout["wraps"]:
        return layout["window_cm"] * margin_scale
    return layout["window_cm"] * max(layout["box_multiplier"], 1.0) * cover_multiplier * margin_scale


def height_capture_bounds(spec, cover_multiplier=WRAPPED_CAPTURE_COVER_MULTIPLIER):
    width = height_capture_width(spec, cover_multiplier)
    return box_xy_bounds(spec["center"], (width, width))


def relief_tile_terrain_features(hill_specs=RELIEF_TILE_HILL_SPECS):
    return [{"name": spec["label"], "shape": "cone", "center": spec["center"], "radius_cm": 0.5 * spec["base_diameter_cm"], "base_z_cm": FLOOR_Z_CM, "top_z_cm": FLOOR_Z_CM + spec["height_cm"]} for spec in hill_specs]


def tagged_terrain_rectangles():
    floors = {label: box_xy_bounds(center, size) for label, center, size in FLOOR_SPECS}
    rectangles = [{"zone": zone, "name": label, "bounds": floors[label]} for label, zone in TERRAIN_TAGGED_FLOOR_ZONES.items()]
    rectangles += [{"zone": "9", "name": feature["name"], "bounds": feature_xy_bounds(feature)} for feature in relief_terrain_features() if feature["shape"] == "cone"]
    rectangles += [{"zone": "10", "name": feature["name"], "bounds": feature_xy_bounds(feature)} for feature in relief_tile_terrain_features()]
    return rectangles


def untagged_geometry_rectangles():
    rectangles = [(label, box_xy_bounds(center, size)) for label, center, size in FLOOR_SPECS if label not in TERRAIN_TAGGED_FLOOR_ZONES]
    rectangles += [(label, box_xy_bounds(location[:2], (scale[0] * CUBE_SIZE_CM, scale[1] * CUBE_SIZE_CM))) for label, location, scale in LAB_WALLS + SHRINE_PILLARS]
    rectangles += [(spec["label"], box_xy_bounds(spec["center"], spec["size"][:2])) for spec in RELIEF_OVERHANG_SPECS + STATIC_BOX_SPECS]
    return rectangles


def find_capture_sphere_cover_problems(specs, cover_multiplier=WRAPPED_CAPTURE_COVER_MULTIPLIER):
    problems = []
    for spec in specs:
        layout = PRESET_KIND_LAYOUTS[spec["kind"]]
        if not (layout["follows_terrain"] and layout["wraps"]):
            continue
        half_width = 0.5 * height_capture_width(spec, cover_multiplier)
        radius = fog_bounding_sphere_radius(spec)
        if half_width < radius:
            problems.append(f"{spec['name']}: 캡처 반폭 {half_width:.0f} < 볼륨 경계 구 반경 {radius:.0f}")
    return problems


def find_capture_crosstalk_problems(specs, terrain_rectangles):
    problems = []
    for spec in specs:
        if not PRESET_KIND_LAYOUTS[spec["kind"]]["follows_terrain"]:
            continue
        capture = height_capture_bounds(spec)
        for rectangle in terrain_rectangles:
            if rectangle["zone"] != spec["zone"] and intersect_bounds(capture, rectangle["bounds"]) is not None:
                problems.append(f"{spec['name']}(구역 {spec['zone']}) 캡처 정사각형에 구역 {rectangle['zone']} 태그 지형 {rectangle['name']}")
    return problems


def find_relief_tile_hill_problems(hill_specs):
    tile = fluid_spec_in_zone("10")
    window = fluid_window_bounds(tile)
    old_capture = fluid_window_bounds(tile, 1.0 + 2.0 * HEIGHT_CAPTURE_MARGIN_RATIO)
    box = fluid_visible_bounds(tile)
    floor = box_xy_bounds(ZONE10_CENTER, ZONE10_FLOOR_SIZE_CM)
    problems = []
    if sorted(spec["role"] for spec in hill_specs) != ["in", "out"]:
        problems.append("구역 10 언덕 역할은 in·out 하나씩이어야 함")
    for spec, feature in zip(hill_specs, relief_tile_terrain_features(hill_specs)):
        bounds = feature_xy_bounds(feature)
        if not is_inside_bounds(bounds, box):
            problems.append(f"{spec['label']}: 감싸기 상자 밖")
        if not is_inside_bounds(bounds, floor):
            problems.append(f"{spec['label']}: {RELIEF_TILE_FLOOR_LABEL} 밖")
        if spec["role"] == "in" and not is_inside_bounds(bounds, window):
            problems.append(f"{spec['label']}: 시뮬 창 안 전체가 아님")
        if spec["role"] == "out" and rectangle_gap(bounds, old_capture) <= 0.0:
            problems.append(f"{spec['label']}: 옛 캡처 정사각형 안(판별력 없음)")
        if rectangle_circle_gap(RELIEF_TILE_FLAT_BAND_BOUNDS, feature["center"], feature["radius_cm"]) <= 0.0:
            problems.append(f"{spec['label']}: 평지 띠와 겹침")
    return problems


def find_relief_tile_guard_column_problems(pawn_xy):
    window_edge_x = fluid_window_bounds(fluid_spec_in_zone("10"))[2]
    if pawn_xy[0] <= window_edge_x:
        return [f"폰 X {pawn_xy[0]:.0f} ≤ 창 +X 가장자리 {window_edge_x:.0f}(판별 열에 창 안 안개가 보임)"]
    return []


def find_relief_tile_symmetry_problems(pawn_xy, hill_specs):
    hills = {spec["role"]: spec for spec in hill_specs}
    if set(hills) != {"in", "out"}:
        return ["구역 10 언덕 역할은 in·out 하나씩이어야 함"]
    in_offset = pawn_xy[0] - hills["in"]["center"][0]
    out_offset = hills["out"]["center"][0] - pawn_xy[0]
    problems = []
    if in_offset <= 0.0 or out_offset <= 0.0:
        problems.append(f"언덕이 폰 X 양쪽에 있지 않음(in {in_offset:.0f}, out {out_offset:.0f})")
    if abs(in_offset - out_offset) > RELIEF_TILE_SYMMETRY_TOLERANCE_CM:
        problems.append(f"두 언덕이 폰 X 에 대칭이 아님(in {in_offset:.0f}, out {out_offset:.0f})")
    if abs(hills["in"]["center"][1] - hills["out"]["center"][1]) > RELIEF_TILE_SYMMETRY_TOLERANCE_CM:
        problems.append("두 언덕 Y 가 다름")
    return problems


def find_relief_tile_sphere_coverage_problems(terrain_features):
    tile = fluid_spec_in_zone("10")
    box_width = 2.0 * fluid_visible_half_width(tile)
    return find_fog_sphere_coverage_problems(tile["center"], (box_width, box_width), 1.0, FOG_BOX_HEIGHT_CM, FLOOR_Z_CM, RELIEF_TILE_FOG_LAYER_HEIGHT_CM, RELIEF_TILE_FOG_BELOW_CUT_CM, terrain_features)


def find_relief_tile_sphere_spacing_problems():
    tile = fluid_spec_in_zone("10")
    radius = fog_bounding_sphere_radius(tile)
    problems = []
    for spec in FLUID_SPECS:
        if spec is tile:
            continue
        distance = math.dist(spec["center"], tile["center"])
        required = ACTIVATION_DISTANCE_CM + radius + fluid_visible_half_width(spec)
        if distance <= required:
            problems.append(f"{tile['name']}·{spec['name']} 중심 거리 {distance:.0f} ≤ 활성 거리 + 경계 구 반경 + 이웃 반폭 {required:.0f}")
    return problems


def wrapped_sphere_neighbor_distances():
    records = {}
    for wrapped in FLUID_SPECS:
        if not PRESET_KIND_LAYOUTS[wrapped["kind"]]["wraps"]:
            continue
        radius = fog_bounding_sphere_radius(wrapped)
        records[wrapped["name"]] = {spec["name"]: {"distance_cm": round(math.dist(spec["center"], wrapped["center"]), 1), "sphere_rule_cm": round(ACTIVATION_DISTANCE_CM + radius + fluid_visible_half_width(spec), 1)} for spec in FLUID_SPECS if spec is not wrapped}
    return records


def find_relief_tile_problems():
    problems = []
    floor = box_xy_bounds(ZONE10_CENTER, ZONE10_FLOOR_SIZE_CM)
    for label, center, size in FLOOR_SPECS:
        if label == RELIEF_TILE_FLOOR_LABEL:
            continue
        gap = rectangle_gap(floor, box_xy_bounds(center, size))
        if gap < RELIEF_FLOOR_MIN_GAP_CM:
            problems.append(f"{RELIEF_TILE_FLOOR_LABEL}·{label} 간격 {gap:.0f} < {RELIEF_FLOOR_MIN_GAP_CM}")
    for label, bounds in untagged_geometry_rectangles():
        if intersect_bounds(bounds, floor) is not None:
            problems.append(f"{label}: 구역 10 바닥 위 태그 없는 물체")
    problems.extend(find_relief_tile_hill_problems(RELIEF_TILE_HILL_SPECS))
    problems.extend(find_relief_tile_guard_column_problems(RELIEF_TILE_PAWN_XY))
    problems.extend(find_relief_tile_symmetry_problems(RELIEF_TILE_PAWN_XY, RELIEF_TILE_HILL_SPECS))
    features = relief_tile_terrain_features()
    tallest = max(feature["top_z_cm"] for feature in features)
    if tallest + RELIEF_TILE_FOG_LAYER_HEIGHT_CM >= FLOOR_Z_CM + RELIEF_CAPTURE_HEIGHT_ABOVE_CM:
        problems.append(f"구역 10 가장 높은 요소 {tallest} + 층 {RELIEF_TILE_FOG_LAYER_HEIGHT_CM} ≥ 캡처 높이 {RELIEF_CAPTURE_HEIGHT_ABOVE_CM}")
    problems.extend(find_relief_tile_sphere_coverage_problems(features))
    problems.extend(find_relief_tile_sphere_spacing_problems())
    problems.extend(find_capture_sphere_cover_problems(FLUID_SPECS))
    problems.extend(find_capture_crosstalk_problems(FLUID_SPECS, tagged_terrain_rectangles()))
    return problems


def validate_stage_layout():
    problems = find_fluid_spacing_problems() + find_stirrer_problems() + find_relief_problems() + find_relief_tile_problems()
    band = (-ZONE_BAND_HALF_WIDTH_CM, -ZONE_BAND_HALF_WIDTH_CM, (ZONE_COUNT_ON_AXIS - 1) * ZONE_SPACING_CM + ZONE_BAND_HALF_WIDTH_CM, ZONE_BAND_HALF_WIDTH_CM)
    if FOG_BOX_HEIGHT_CM < FOG_EXTRUDE_RANGE_MAX_CM + FOG_TOP_FADE_RANGE_MAX_CM:
        problems.append(f"상자 높이 {FOG_BOX_HEIGHT_CM} < 돌출 범위 {FOG_EXTRUDE_RANGE_MAX_CM} + 윗면 페이드 {FOG_TOP_FADE_RANGE_MAX_CM}")
    for spec in STATIC_BOX_SPECS:
        if spec["shape"] < 2 and spec["size"][2] < FOG_EXTRUDE_RANGE_MAX_CM + FOG_TOP_FADE_RANGE_MAX_CM:
            problems.append(f"{spec['label']}: 높이 {spec['size'][2]} 가 돌출+페이드 범위보다 낮음")
    field = next(spec for spec in STATIC_BOX_SPECS if spec["label"] == "PassiveField")
    field_bounds = box_xy_bounds(field["center"], field["size"])
    if rectangle_gap(field_bounds, band) < FOG_VIEW_DISTANCE_CM:
        problems.append(f"200 m 상자와 구역 띠 간격 {rectangle_gap(field_bounds, band):.0f} < 보이는 거리 {FOG_VIEW_DISTANCE_CM}")
    displays = [spec for spec in STATIC_BOX_SPECS if spec["label"].startswith("PassiveDisplay")]
    for spec in displays:
        bounds = box_xy_bounds(spec["center"], spec["size"])
        if rectangle_gap(bounds, band) < FOG_VIEW_DISTANCE_CM:
            problems.append(f"{spec['label']}: 구역 띠와 간격 {rectangle_gap(bounds, band):.0f} < 보이는 거리")
        if rectangle_gap(bounds, field_bounds) <= 0.0:
            problems.append(f"{spec['label']}: 200 m 상자와 겹침")
    for first_index, first in enumerate(displays):
        for second in displays[first_index + 1:]:
            if rectangle_gap(box_xy_bounds(first["center"], first["size"]), box_xy_bounds(second["center"], second["size"])) <= 0.0:
                problems.append(f"{first['label']}·{second['label']} 진열 상자 겹침")
    band_boxes = [spec for spec in STATIC_BOX_SPECS if spec not in displays and spec is not field]
    for spec in band_boxes:
        bounds = box_xy_bounds(spec["center"], spec["size"])
        if bounds[0] < band[0] or bounds[1] < band[1] or bounds[2] > band[2] or bounds[3] > band[3]:
            problems.append(f"{spec['label']}: 구역 띠 밖")
    writers = [spec["name"] for spec in FLUID_SPECS if spec["writes_collection"]]
    if writers != ["FogAmbient"]:
        problems.append("MPC 쓰기 액터는 FogAmbient 하나여야 함: " + ", ".join(writers))
    problems.extend(find_zone8_problems(band, [(spec["label"], box_xy_bounds(spec["center"], spec["size"])) for spec in displays]))
    instances = {spec["instance"] for spec in STATIC_BOX_SPECS} | {spec["instance"] for spec in FLUID_SPECS}
    if len(instances) != EXPECTED_STAGE_INSTANCE_COUNT:
        problems.append(f"무대가 쓰는 MI 수 {len(instances)} ≠ {EXPECTED_STAGE_INSTANCE_COUNT}")
    return problems


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


def asset_disk_path(asset_path, extension):
    return project_path("Content", *asset_path[len("/Game/"):].split("/")) + extension


def preset_asset_name(spec):
    return f"DA_TDFluid_{spec['name']}"


def preset_asset_path(spec):
    return f"{PRESET_DIR}/{preset_asset_name(spec)}"


def find_missing_unreal_names():
    missing = []
    for dotted_name in REQUIRED_UNREAL_NAMES:
        target = unreal
        for part in dotted_name.split("."):
            target = getattr(target, part, None)
            if target is None:
                missing.append(dotted_name)
                break
    return missing


def has_editor_property(owner, property_name):
    try:
        owner.get_editor_property(property_name)
    except Exception:
        return False
    return True


def check_cpp_contract(actor_class):
    default_preset = unreal.get_default_object(unreal.TDFluidPresetAsset)
    problems = [f"TDFluidPresetAsset.{name}" for name in PRESET_REQUIRED_PROPERTIES if not has_editor_property(default_preset, name)]
    default_actor = unreal.get_default_object(actor_class)
    problems.extend(f"TDFluidLiveActor.{name}" for name in ACTOR_REQUIRED_PROPERTIES if not has_editor_property(default_actor, name))
    return problems


def read_overwrite_request():
    request_path = project_path(*REQUEST_RELATIVE_PATH)
    if not os.path.isfile(request_path):
        return False
    with open(request_path, encoding="utf-8") as request_file:
        request = json.load(request_file)
    return isinstance(request, dict) and request.get("overwrite") is True


def find_unsaved_package_names(overwrite):
    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    dirty.extend(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    names = sorted({package.get_name() for package in dirty})
    return names if not overwrite else [name for name in names if not name.startswith(FLUID_ROOT + "/")]


def ensure_directory(directory_path):
    if unreal.EditorAssetLibrary.does_directory_exist(directory_path):
        return
    if not unreal.EditorAssetLibrary.make_directory(directory_path):
        raise RuntimeError(f"폴더 생성 실패: {directory_path}")


def save_asset(asset, asset_path):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"에셋 저장 실패: {asset_path}")


def load_preset_checker():
    checker_path = project_path(*PRESET_CHECKER_RELATIVE_PATH)
    module_spec = importlib.util.spec_from_file_location("check_fluid_preset_data", checker_path)
    if module_spec is None or module_spec.loader is None:
        raise RuntimeError(f"프리셋 정본 검사기 없음: {checker_path}")
    checker = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(checker)
    return checker


def load_particle_stage():
    module_path = project_path(*PARTICLE_STAGE_RELATIVE_PATH)
    module_spec = importlib.util.spec_from_file_location("td_fluid_particle_stage", module_path)
    if module_spec is None or module_spec.loader is None:
        raise RuntimeError(f"입자 무대 공용 함수 모듈 없음: {module_path}")
    particle_stage = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(particle_stage)
    return particle_stage


def read_file_sha256(relative_parts):
    path = project_path(*relative_parts)
    if not os.path.isfile(path):
        raise RuntimeError(f"정본 파일 없음: {path}")
    with open(path, "rb") as source_file:
        return hashlib.sha256(source_file.read()).hexdigest()


def reload_preset_tuning():
    tuning_path = project_path(*PRESET_TUNING_RELATIVE_PATH)
    before = read_file_sha256(PRESET_TUNING_RELATIVE_PATH)
    problems = load_preset_checker().check_tuning_file(tuning_path)
    if problems:
        raise RuntimeError("프리셋 정본 JSON 검사 실패(python Tools/check_fluid_preset_data.py): " + " | ".join(problems))
    tuning = unreal.get_engine_subsystem(unreal.TDFluidPresetTuningSubsystem)
    if tuning is None:
        raise RuntimeError("TDFluidPresetTuningSubsystem 없음(재빌드와 에디터 재시작 필요)")
    error_count = tuning.reload_preset_tuning()
    if error_count != 0:
        raise RuntimeError(f"프리셋 정본 다시 읽기 오류 {error_count}건: " + " | ".join(tuning.get_load_errors()))
    if read_file_sha256(PRESET_TUNING_RELATIVE_PATH) != before:
        raise RuntimeError("프리셋 정본 JSON 이 검사·다시 읽기 도중 바뀌었습니다. 다시 실행하세요.")
    return before


def open_empty_stage_level(levels, actors):
    if not unreal.EditorAssetLibrary.does_asset_exist(STAGE_MAP_PATH):
        if not levels.new_level(STAGE_MAP_PATH):
            raise RuntimeError(f"무대 레벨 생성 실패: {STAGE_MAP_PATH}")
        return
    if not levels.load_level(STAGE_MAP_PATH):
        raise RuntimeError(f"기존 무대 레벨 열기 실패: {STAGE_MAP_PATH}")
    for actor in actors.get_all_level_actors():
        if isinstance(actor, unreal.WorldSettings) or type(actor) is unreal.Brush:
            continue
        if not actors.destroy_actor(actor):
            raise RuntimeError(f"기존 무대 레벨 액터 제거 실패: {actor.get_actor_label()}")


def load_or_create_matte_material(asset_path, color_rgb):
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(asset_path):
        return assets.load_asset(asset_path), False
    asset_dir, asset_name = asset_path.rsplit("/", 1)
    ensure_directory(asset_dir)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(asset_name, asset_dir, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError(f"재질 생성 실패: {asset_path}")
    editing = unreal.MaterialEditingLibrary
    base_color = editing.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -280, -120)
    base_color.set_editor_property("constant", unreal.LinearColor(color_rgb[0], color_rgb[1], color_rgb[2], 1.0))
    roughness = editing.create_material_expression(material, unreal.MaterialExpressionConstant, -280, 20)
    roughness.set_editor_property("r", 0.9)
    editing.connect_material_property(base_color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    editing.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    errors = [str(item) for item in editing.recompile_material(material) or []]
    if errors:
        raise RuntimeError(f"재질 컴파일 실패: {asset_path}, {errors}")
    save_asset(material, asset_path)
    return material, True


def load_or_create_preset(spec):
    asset_path = preset_asset_path(spec)
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        preset = unreal.EditorAssetLibrary.load_asset(asset_path)
        if not isinstance(preset, unreal.TDFluidPresetAsset):
            raise RuntimeError(f"기존 프리셋 에셋 형식 불일치: {asset_path}")
        return preset
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.TDFluidPresetAsset)
    preset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(preset_asset_name(spec), PRESET_DIR, unreal.TDFluidPresetAsset, factory)
    if preset is None:
        raise RuntimeError(f"프리셋 생성 실패: {asset_path}")
    return preset


def find_effective_layout_problems(spec, effective, volume_render):
    layout = PRESET_KIND_LAYOUTS[spec["kind"]]
    extent = effective.get_editor_property("sim_extent_cm")
    problems = []
    if abs(extent.x - layout["window_cm"]) > 0.01 or abs(extent.y - layout["window_cm"]) > 0.01:
        problems.append(f"{spec['kind']} 창 {extent.x}x{extent.y} ≠ {layout['window_cm']}")
    is_wrapped = effective.get_editor_property("area_address_mode") == unreal.TDFluidAreaAddressMode.WRAP
    if is_wrapped != layout["wraps"]:
        problems.append(f"{spec['kind']} 감싸기 {is_wrapped} ≠ {layout['wraps']}")
    box_multiplier = volume_render.get_editor_property("volume_size_multiplier")
    if box_multiplier > layout["box_multiplier"] + 0.01:
        problems.append(f"{spec['kind']} 상자 배율 {box_multiplier} > 배치 가정 {layout['box_multiplier']}")
    return problems


def find_preset_kind_problems():
    problems = []
    for spec in FLUID_SPECS:
        preset_kind = getattr(unreal.TDFluidPresetKind, spec["kind"])
        config = unreal.TDFluidPresetAsset.build_config_for_preset_kind(preset_kind)
        volume_render = unreal.TDFluidPresetAsset.build_volume_render_for_preset_kind(preset_kind)
        problems.extend(f"{spec['name']}: {problem}" for problem in find_effective_layout_problems(spec, config, volume_render))
    return problems


def configure_fog_preset(spec, volume_material, cube, collection):
    preset_kind = getattr(unreal.TDFluidPresetKind, spec["kind"])
    preset = load_or_create_preset(spec)
    preset.set_editor_property("preset_kind", preset_kind)
    preset.set_editor_property("default_channel", unreal.TDFluidBrushChannel.CHANNEL_R_MUD)
    preset.set_editor_property("override_preset_defaults", False)
    preset.set_editor_property("simulation_config", unreal.TDFluidPresetAsset.build_config_for_preset_kind(preset_kind))
    preset.set_editor_property("volume_material", volume_material)
    preset.set_editor_property("volume_mesh", cube)
    preset.set_editor_property("volume_height_cm", FOG_BOX_HEIGHT_CM)
    preset.set_editor_property("parameter_collection", collection if spec["writes_collection"] else None)
    effective = preset.resolve_effective_config()
    layout_problems = find_effective_layout_problems(spec, effective, preset.resolve_effective_volume_render())
    if layout_problems:
        raise RuntimeError("종류 값이 무대 배치 가정과 다름(조절 정본 확인): " + preset_asset_path(spec) + ", " + "; ".join(layout_problems))
    save_asset(preset, preset_asset_path(spec))
    return preset, effective


def find_missing_saved_files(asset_paths):
    expected = [asset_disk_path(path, ".uasset") for path in asset_paths] + [asset_disk_path(STAGE_MAP_PATH, ".umap")]
    return [path for path in expected if not os.path.isfile(path)]


def write_report(report):
    report_path = project_path(*REPORT_RELATIVE_PATH)
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, "w", encoding="utf-8") as report_file:
        json.dump(report, report_file, ensure_ascii=False, indent=2, default=str)
    return report_path


def main():
    assets = unreal.EditorAssetLibrary
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    missing_names = find_missing_unreal_names()
    if missing_names:
        raise RuntimeError("언리얼 Python 이름 없음: " + ", ".join(missing_names))
    layout_problems = validate_stage_layout()
    if layout_problems:
        raise RuntimeError("무대 배치 자체 점검 실패: " + " | ".join(layout_problems))
    if editor.get_game_world() is not None:
        raise RuntimeError("PIE를 종료한 뒤 실행하세요.")

    overwrite = read_overwrite_request()
    guarded = [preset_asset_path(spec) for spec in FLUID_SPECS] + [STAGE_MAP_PATH]
    existing_paths = [path for path in guarded if assets.does_asset_exist(path)]
    if existing_paths and not overwrite:
        print(json.dumps({"status": "기존 에셋이 있어 아무것도 바꾸지 않았습니다", "existing_assets": existing_paths, "overwrite_request_file": project_path(*REQUEST_RELATIVE_PATH), "overwrite_request_content": {"overwrite": True}}, ensure_ascii=False))
        raise RuntimeError("기존 에셋 보존: overwrite 요청 파일이 없어 중단합니다.")
    unsaved_names = find_unsaved_package_names(overwrite)
    if unsaved_names:
        raise RuntimeError("미저장 작업을 보존합니다: " + ", ".join(unsaved_names))

    print("단계: 프리셋 정본 검사·다시 읽기")
    preset_tuning_sha256 = reload_preset_tuning()
    material_look_sha256 = read_file_sha256(MATERIAL_LOOK_RELATIVE_PATH)
    kind_problems = find_preset_kind_problems()
    if kind_problems:
        raise RuntimeError("종류 값이 무대 배치 가정과 다름(조절 정본 Kinds 확인, 에셋 변경 전 중단): " + " | ".join(kind_problems))

    game_mode = assets.load_blueprint_class(GAME_MODE_PATH)
    cube = assets.load_asset(CUBE_MESH_PATH)
    collection = assets.load_asset(COLLECTION_PATH)
    stirrer_class = assets.load_blueprint_class(STIRRER_BLUEPRINT_PATH)
    particle_stage = load_particle_stage()
    fluid_actor_class = unreal.load_class(None, FLUID_ACTOR_CLASS_PATH)
    cone = assets.load_asset(CONE_MESH_PATH)
    instance_names = sorted({spec["instance"] for spec in STATIC_BOX_SPECS} | {spec["instance"] for spec in FLUID_SPECS})
    instances = {name: assets.load_asset(f"{MATERIAL_DIR}/{name}") for name in instance_names}
    required = {"게임 모드": game_mode, "큐브": cube, "원뿔": cone, "MPC": collection, "교반기(FX-8n 무대 먼저)": stirrer_class, "유체 액터 클래스": fluid_actor_class}
    required.update({f"MI {name}": value for name, value in instances.items()})
    missing = [name for name, value in required.items() if value is None]
    if missing:
        raise RuntimeError("필수 에셋 또는 클래스 없음(editor_make_fluid_fog_material.py·FX-8n 무대 먼저): " + ", ".join(missing))
    contract_problems = check_cpp_contract(fluid_actor_class)
    if contract_problems:
        raise RuntimeError("C++ 계약 속성 없음: " + ", ".join(contract_problems))

    print("단계: 재질·프리셋")
    floor_mat, floor_created = load_or_create_matte_material(FLOOR_MAT_PATH, FLOOR_COLOR)
    wall_mat, wall_created = load_or_create_matte_material(WALL_MAT_PATH, WALL_COLOR)
    ensure_directory(PRESET_DIR)
    presets = {spec["name"]: configure_fog_preset(spec, instances[spec["instance"]], cube, collection) for spec in FLUID_SPECS}

    print("단계: 레벨")
    ensure_directory(MAP_DIR)
    open_empty_stage_level(levels, actors)
    world = editor.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", game_mode)
    created = []

    def finish(actor, label, folder):
        if actor is None:
            raise RuntimeError(f"액터 생성 실패: {label}")
        actor.set_actor_label("TDGen_" + label)
        actor.set_folder_path("TDGen/FluidFX/FogStage/" + folder)
        created.append(actor)
        return actor

    def spawn(actor_class, location, label, folder, rotation=None):
        return finish(actors.spawn_actor_from_class(actor_class, unreal.Vector(*location), rotation or unreal.Rotator()), label, folder)

    def place_cube(location, scale, material, label, folder, has_collision):
        actor = finish(actors.spawn_actor_from_object(cube, unreal.Vector(*location), unreal.Rotator()), label, folder)
        actor.set_actor_scale3d(unreal.Vector(*scale))
        component = actor.static_mesh_component
        component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        component.set_material(0, material)
        if not has_collision:
            component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            component.set_collision_profile_name("NoCollision")
            component.set_cast_shadow(False)
        return actor

    def tag_as_terrain(actor):
        actor.set_editor_property("tags", [unreal.Name(TERRAIN_ACTOR_TAG)])
        tagged_labels.append(actor.get_actor_label())

    def require_bounds(actor, label, center_xy, size, bottom_z):
        origin, extent = actor.get_actor_bounds(False)
        expected = (center_xy[0], center_xy[1], bottom_z + 0.5 * size[2])
        mismatches = [axis for axis, actual, wanted in (("x", origin.x, expected[0]), ("y", origin.y, expected[1]), ("z", origin.z, expected[2]), ("sx", 2.0 * extent.x, size[0]), ("sy", 2.0 * extent.y, size[1]), ("sz", 2.0 * extent.z, size[2])) if abs(actual - wanted) > RELIEF_BOUNDS_TOLERANCE_CM]
        if mismatches:
            raise RuntimeError(f"기복 요소 경계 불일치 {label}: {mismatches}, 원점 {origin}, 반 크기 {extent}")
        return [origin.x - extent.x, origin.y - extent.y, origin.z - extent.z, origin.x + extent.x, origin.y + extent.y, origin.z + extent.z]

    def place_relief_hill(spec, folder="Relief"):
        actor = finish(actors.spawn_actor_from_object(cone, unreal.Vector(spec["center"][0], spec["center"][1], FLOOR_Z_CM), unreal.Rotator()), spec["label"], folder)
        _, unit_extent = actor.get_actor_bounds(False)
        actor.set_actor_scale3d(unreal.Vector(0.5 * spec["base_diameter_cm"] / unit_extent.x, 0.5 * spec["base_diameter_cm"] / unit_extent.y, 0.5 * spec["height_cm"] / unit_extent.z))
        origin, extent = actor.get_actor_bounds(False)
        location = actor.get_actor_location()
        actor.set_actor_location(unreal.Vector(location.x + spec["center"][0] - origin.x, location.y + spec["center"][1] - origin.y, location.z + FLOOR_Z_CM - (origin.z - extent.z)), False, False)
        component = actor.static_mesh_component
        component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        component.set_material(0, wall_mat)
        tag_as_terrain(actor)
        size = (spec["base_diameter_cm"], spec["base_diameter_cm"], spec["height_cm"])
        return {"label": actor.get_actor_label(), "center": list(spec["center"]), "base_diameter_cm": spec["base_diameter_cm"], "height_cm": spec["height_cm"], "bounds": require_bounds(actor, spec["label"], spec["center"], size, FLOOR_Z_CM), "tags": [TERRAIN_ACTOR_TAG]}

    def place_relief_overhang(spec):
        size = spec["size"]
        location = (spec["center"][0], spec["center"][1], FLOOR_Z_CM + spec["top_z_cm"] - 0.5 * size[2])
        actor = place_cube(location, tuple(value / CUBE_SIZE_CM for value in size), wall_mat, spec["label"], "Relief", True)
        return {"label": actor.get_actor_label(), "center": list(spec["center"]), "size_cm": list(size), "top_z_cm": spec["top_z_cm"], "bounds": require_bounds(actor, spec["label"], spec["center"], size, FLOOR_Z_CM + spec["top_z_cm"] - size[2]), "tags": []}

    tagged_labels = []
    for label, center, size in FLOOR_SPECS:
        floor_actor = place_cube((center[0], center[1], FLOOR_Z_CM - 25.0), (size[0] / CUBE_SIZE_CM, size[1] / CUBE_SIZE_CM, 0.5), floor_mat, label, "Stage", True)
        if label in TERRAIN_TAGGED_FLOOR_LABELS:
            tag_as_terrain(floor_actor)
    relief_hill_records = [place_relief_hill(spec) for spec in RELIEF_HILL_SPECS]
    relief_overhang_records = [place_relief_overhang(spec) for spec in RELIEF_OVERHANG_SPECS]
    relief_tile_hill_records = [place_relief_hill(spec, "ReliefTile") for spec in RELIEF_TILE_HILL_SPECS]
    for label, location, scale in LAB_WALLS + SHRINE_PILLARS:
        place_cube(location, scale, wall_mat, label, "Stage", True)

    sun = spawn(unreal.DirectionalLight, (0, 0, 2000), "FogStageSun", "Lighting", unreal.Rotator(pitch=-18, yaw=160))
    sun.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_intensity(4.0)
    sky = spawn(unreal.SkyLight, (0, 0, 1500), "FogStageSkyLight", "Lighting")
    sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", True)
    sky.light_component.set_intensity(0.3)
    height_fog = spawn(unreal.ExponentialHeightFog, (0, 0, FLOOR_Z_CM), "FogStageHeightFog", "Lighting")
    fog_component = height_fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    applied_fog = {}
    for name, value in HEIGHT_FOG_PROPERTIES:
        fog_component.set_editor_property(name, value)
        applied_fog[name] = value
    post_process = spawn(unreal.PostProcessVolume, (0, 0, 0), "FogStageExposure", "Lighting")
    post_process.set_editor_property("unbound", True)
    settings = post_process.settings
    for name, value in (("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL), ("auto_exposure_apply_physical_camera_exposure", False), ("auto_exposure_bias", 0.0)):
        settings.set_editor_property("override_" + name, True)
        settings.set_editor_property(name, value)
    post_process.set_editor_property("settings", settings)
    spawn(unreal.PlayerStart, PLAYER_START, "FogStagePlayerStart", "Gameplay")

    light_records = []
    for spec in LOCAL_LIGHTS:
        lamp = spawn(unreal.PointLight, spec["location"], spec["label"], "Lighting")
        component = lamp.light_component
        component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        component.set_intensity(spec["intensity"])
        component.set_attenuation_radius(spec["radius"])
        component.set_light_color(unreal.LinearColor(*spec["color"], 1.0))
        component.set_editor_property("volumetric_scattering_intensity", spec["volumetric_scattering_intensity"])
        light_records.append({"label": lamp.get_actor_label(), **spec})

    box_records = []
    for spec in STATIC_BOX_SPECS:
        size = spec["size"]
        location = (spec["center"][0], spec["center"][1], FLOOR_Z_CM + 0.5 * size[2])
        actor = place_cube(location, tuple(value / CUBE_SIZE_CM for value in size), instances[spec["instance"]], f"Fog{spec['label']}", "PassiveFog", False)
        origin, extent = actor.get_actor_bounds(False)
        if abs((origin.z - extent.z) - FLOOR_Z_CM) > 0.5:
            raise RuntimeError(f"정적 안개 상자 아래면 {origin.z - extent.z} ≠ 바닥 {FLOOR_Z_CM}: {spec['label']}")
        box_records.append({"label": actor.get_actor_label(), "instance": spec["instance"], "location": list(location), "size_cm": list(size), "bounds_half_cm": [extent.x, extent.y, extent.z]})

    fluid_records = []
    for spec in FLUID_SPECS:
        preset, effective = presets[spec["name"]]
        center = spec["center"]
        actor = spawn(fluid_actor_class, (center[0], center[1], FLOOR_Z_CM), spec["name"], "SimFog")
        actor.set_editor_property("should_follow_player_pawn", False)
        actor.set_editor_property("should_use_activation_volume", True)
        actor.set_editor_property("activation_distance_cm", ACTIVATION_DISTANCE_CM)
        actor.set_editor_property("interaction_brush_channel", unreal.TDFluidBrushChannel.CHANNEL_R_MUD)
        live_component = actor.get_editor_property("live_component")
        live_component.set_editor_property("active_preset", preset)
        live_component.set_editor_property("should_init_system_state_from_preset", True)
        live_component.set_editor_property("simulation_config", effective)
        fluid_records.append({"label": actor.get_actor_label(), "preset": preset_asset_name(spec), "preset_kind": spec["kind"], "instance": spec["instance"], "zone": spec["zone"], "location": [center[0], center[1], FLOOR_Z_CM], "writes_collection": spec["writes_collection"], "volume_height_cm": FOG_BOX_HEIGHT_CM})

    fluids_by_name = {spec["name"]: spec for spec in FLUID_SPECS}
    stirrer_records = []
    for stirrer_spec in STIRRER_SPECS:
        orbit_center = fluids_by_name[stirrer_spec["fluid"]]["center"]
        radius = stirrer_spec["orbit_radius_cm"]
        stirrer = spawn(stirrer_class, (orbit_center[0] - radius, orbit_center[1], FLOOR_Z_CM + STIRRER_HEIGHT_CM), stirrer_spec["label"], "SimFog")
        motion = {"pivot_translation": unreal.Vector(radius, 0.0, 0.0), "rotation_rate": unreal.Rotator(roll=0.0, pitch=0.0, yaw=stirrer_spec["degrees_per_sec"])}
        particle_stage.set_component_properties(stirrer, unreal.RotatingMovementComponent, motion)
        stirrer_records.append({"label": stirrer.get_actor_label(), "fluid": stirrer_spec["fluid"], "orbit_radius_cm": radius, "degrees_per_sec": stirrer_spec["degrees_per_sec"]})

    print("단계: 레벨 저장")
    if not levels.save_current_level():
        raise RuntimeError(f"무대 레벨 저장 실패: {STAGE_MAP_PATH}")
    missing_files = find_missing_saved_files([FLOOR_MAT_PATH, WALL_MAT_PATH] + [preset_asset_path(spec) for spec in FLUID_SPECS])
    if missing_files:
        raise RuntimeError("저장 파일 확인 실패: " + ", ".join(missing_files))

    report = {
        "status": "ok",
        "preset_tuning_sha256": preset_tuning_sha256,
        "material_look_sha256": material_look_sha256,
        "map": STAGE_MAP_PATH,
        "overwrite": overwrite,
        "created_floor_material": floor_created,
        "created_wall_material": wall_created,
        "zones": {str(index): {"label": label, "center": list(zone_center(index))} for index, label in enumerate(ZONE_LABELS)} | {"7A": {"label": "AmbientWindowAndField", "center": list(ZONE7A_CENTER)}, "7B": {"label": "PassiveDisplays", "y": PASSIVE_DISPLAY_Y_CM}} | {spec["zone"]: {"label": spec["name"], "center": list(spec["center"])} for spec in FLUID_SPECS if spec["zone"].startswith("8") or spec["zone"] in ("9", "10")},
        "relief": {
            "zone": "9",
            "fluid_actor": "TDGen_" + relief_fluid_spec()["name"],
            "window_bounds_xy": list(fluid_visible_bounds(relief_fluid_spec())),
            "floor_bounds_xy": list(box_xy_bounds(ZONE9_CENTER, ZONE9_FLOOR_SIZE_CM)),
            "flat_band_bounds_xy": list(RELIEF_FLAT_BAND_BOUNDS),
            "terrain_tag": TERRAIN_ACTOR_TAG,
            "tagged_actors": tagged_labels,
            "hills": relief_hill_records,
            "overhang": relief_overhang_records,
            "fog_layer_height_cm": RELIEF_FOG_LAYER_HEIGHT_CM,
            "sphere_coverage_problems": find_relief_sphere_coverage_problems(relief_terrain_features()),
        },
        "relief_tile": {
            "zone": "10",
            "fluid_actor": "TDGen_" + fluid_spec_in_zone("10")["name"],
            "window_bounds_xy": list(fluid_window_bounds(fluid_spec_in_zone("10"))),
            "box_bounds_xy": list(fluid_visible_bounds(fluid_spec_in_zone("10"))),
            "bounding_sphere_radius_cm": fog_bounding_sphere_radius(fluid_spec_in_zone("10")),
            "old_capture_bounds_xy": list(fluid_window_bounds(fluid_spec_in_zone("10"), 1.0 + 2.0 * HEIGHT_CAPTURE_MARGIN_RATIO)),
            "capture_bounds_xy": list(height_capture_bounds(fluid_spec_in_zone("10"))),
            "floor_bounds_xy": list(box_xy_bounds(ZONE10_CENTER, ZONE10_FLOOR_SIZE_CM)),
            "flat_band_bounds_xy": list(RELIEF_TILE_FLAT_BAND_BOUNDS),
            "pawn_xy": list(RELIEF_TILE_PAWN_XY),
            "hills": relief_tile_hill_records,
            "fog_layer_height_cm": RELIEF_TILE_FOG_LAYER_HEIGHT_CM,
            "sphere_coverage_problems": find_relief_tile_sphere_coverage_problems(relief_tile_terrain_features()),
            "capture_cover_problems": find_capture_sphere_cover_problems(FLUID_SPECS),
            "capture_crosstalk_problems": find_capture_crosstalk_problems(FLUID_SPECS, tagged_terrain_rectangles()),
            "wrapped_sphere_neighbor_distances": wrapped_sphere_neighbor_distances(),
        },
        "fluid_actors": fluid_records,
        "static_boxes": box_records,
        "local_lights": light_records,
        "height_fog": applied_fog,
        "stirrers": stirrer_records,
        "collection_writer": "TDGen_FogAmbient",
        "deviations": ["광원·높이 안개·상자 높이·교반기 궤도는 무대 상수(D-64 ②), 보정은 룩데브 props", "시뮬 액터 따라가기 끔(D14 이동 시험은 props 로 FogAmbient 만 켬)"],
        "created_actors": len(created),
        "saved": True,
    }
    report_path = write_report(report)
    print(json.dumps(report, ensure_ascii=False, default=str))
    print("보고서: " + report_path)


def run_with_failure_report():
    try:
        main()
    except Exception as error:
        write_report({"status": "failed", "error": str(error)})
        raise


def run_self_test():
    relief = relief_fluid_spec()
    half_window = 0.5 * PRESET_KIND_LAYOUTS[relief["kind"]]["window_cm"]
    corner = (relief["center"][0] + half_window, relief["center"][1] + half_window)
    corner_hill = {"name": "CornerHill", "shape": "cone", "center": (corner[0] - 300.0, corner[1] - 300.0), "radius_cm": 600.0, "base_z_cm": FLOOR_Z_CM, "top_z_cm": FLOOR_Z_CM + 1250.0}
    corner_pit = {"name": "CornerPit", "shape": "box", "center": (corner[0] - 100.0, corner[1] - 100.0), "size_xy": (200.0, 200.0), "base_z_cm": FLOOR_Z_CM - 200.0, "top_z_cm": FLOOR_Z_CM - 200.0}
    cases = (
        ("① 무대 기복 요소(언덕 2·돌출물 3)", relief_terrain_features(), 0),
        ("② 중심이 창 모서리에서 300 cm 안쪽인 1250 cm 언덕(밑반지름 600, H 1300, 층 60)", [corner_hill], 1),
        ("③ 창 모서리에 닿는 OwnerZ − 200 구덩이", [corner_pit], 1),
    )
    all_passed = True
    for label, features, expected in cases:
        problems = find_relief_sphere_coverage_problems(features)
        passed = len(problems) == expected
        all_passed = all_passed and passed
        print(("PASS " if passed else "FAIL ") + label + f": 문제 {len(problems)}(기대 {expected}) " + json.dumps(problems, ensure_ascii=False))
    tile = fluid_spec_in_zone("10")
    moved_out_hill = tuple(dict(spec, center=(10400.0, spec["center"][1])) if spec["role"] == "out" else spec for spec in RELIEF_TILE_HILL_SPECS)
    crosstalk_hill = {"zone": "test", "name": "TestHillIn8CCapture", "bounds": box_xy_bounds((22000.0, ZONE8_BAND_Y_CM), (1000.0, 1000.0))}
    tile_cases = (
        ("구역 10 ① 무대 배치(find_relief_tile_problems)", find_relief_tile_problems(), 0),
        ("구역 10 ② HillOut 을 (10400, 72600)으로 옮김 → 옛 캡처 안", find_relief_tile_hill_problems(moved_out_hill), 1),
        ("구역 10 ③ 태그 언덕을 8C 새 캡처 안 (22000, -35000)에 둠 → 섞임", find_capture_crosstalk_problems(FLUID_SPECS, tagged_terrain_rectangles() + [crosstalk_hill]), 1),
        ("구역 10 ④ 덮음 배수에서 √2 를 뺌(반폭 5400) → 구 덮음", find_capture_sphere_cover_problems([tile], cover_multiplier=1.0), 1),
        ("구역 10 ⑤ 폰 X 10100 → 판별 열", find_relief_tile_guard_column_problems((10100.0, RELIEF_TILE_PAWN_XY[1])), 1),
    )
    for label, problems, expected in tile_cases:
        passed = len(problems) == expected
        all_passed = all_passed and passed
        print(("PASS " if passed else "FAIL ") + label + f": 문제 {len(problems)}(기대 {expected}) " + json.dumps(problems, ensure_ascii=False))
    layout_problems = validate_stage_layout()
    all_passed = all_passed and not layout_problems
    print(("PASS " if not layout_problems else "FAIL ") + "validate_stage_layout(구역 9·10 포함): " + json.dumps(layout_problems, ensure_ascii=False))
    return 0 if all_passed else 1


if __name__ == "__main__":
    if "--self-test" in sys.argv[1:]:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.exit(run_self_test())
    run_with_failure_report()
