"""실행 환경: TDGame 언리얼 에디터 안에서 FluidFX 안개 무대 LV_TDFluidFogStage(구역 0~6 은 X축 30 m 간격, 구역 7A 대형 창 + 200 m 정적 상자는 Y +250 m 띠, 7B 수동 7변형 진열은 Y −150 m 띠, 구역 8 은 Y −350 m 띠: 8A 가르기 FogMist·8B 휘젓기 FogMistStir 에 교반기, 8C 감싸기 타일 FogTileSim)와 DA 7개(Smoke 대리 3·FogMist 2·FogMistStir 1·FogTileSim 1)를 만든다. 먼저 editor_make_fluid_fog_material.py 와 FX-8n 무대 스크립트(교반기 BP_TDFluidStageStirrer)가 성공해 있어야 하고, 모양 정본 병합 뒤에 돌린다.
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_fog_stage.py (기존 DA·맵을 갱신할 때는 Saved/FluidFX/fluid_fog_stage_request.json 에 {"overwrite": true})
출력: /Game/FluidFX/Presets/DA_TDFluid_FogAmbient·FogRoom·FogTrail·FogCharacter·FogCut·FogStir·FogTileSync,/Game/FluidFX/Maps/LV_TDFluidFogStage, /Game/FluidFX/Materials/M_TD_FluidFogStageFloor·M_TD_FluidFogStageWall(없을 때만 만듦), Saved/FluidFX/fluid_fog_stage.json(preset_tuning_sha256·material_look_sha256, check_fluid_preset_data.py --stage-report 형식)
상태: 미검증(2026-10-25 FX-10r 작성: py_compile·가짜 unreal·validate_stage_layout 만. 2026-10-26 에디터 실행 status ok·액터 37·DA 4·바닥·벽 재질 2, stage-report 오류 0, 화면 판정 전). 미저장 작업이 있으면 중단하고, 시작 때 프리셋 정본 검사·다시 읽기를 한다. 볼륨 상자 높이 1300 cm(돌출 범위 1000 + 윗면 페이드 300, 아래면 = 바닥)·광원·높이 안개 값은 무대 상수다(D-64 ② 편차, 보정은 룩데브 props). 시뮬 액터는 따라가기를 끄고 활성 볼륨(2000 cm)으로 가까운 것만 깨운다(따라가기를 켜면 안개판이 모든 구역으로 따라옴, 반박 M9). MPC 쓰기 액터는 FogAmbient 하나다. 2026-10-26 FX-10c: 7A FogAmbient 종류를 Smoke 대리에서 FogMist(40 m 창)로 바꾸고 구역 8(TileBandFloor)을 더함, 배치 점검을 종류별 창 × 상자 배율로 일반화(이웃 중심 거리 > 활성 거리 + 두 반폭), 종류 값(창·감싸기·상자 배율 상한)은 에셋 변경 전에 점검. 에디터 실행 전(py_compile·가짜 unreal·validate_stage_layout 만). 2026-10-27 파동 2 에셋 단계: 에디터 실행 status ok(덮어쓰기, 액터 43·DA 7, MI 15 단언 통과), stage-report stale 0, 화면 판정 전. 2026-10-31: 교반기 회전 속도가 피벗 쓰기 뒤 BP 구성 스크립트 재실행으로 유실되고(기본 90°/s) 위치 인수 Rotator(0, v, 0)가 pitch 였던 것을 particle_stage.set_component_properties(한 번에 쓰기·새 핸들로 다시 읽기, 다르면 FAIL)와 yaw 키워드로 고침, 맵 재생성 필요.
"""
import hashlib
import importlib.util
import json
import math
import os

import unreal


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
    "SMOKE": {"window_cm": 700.0, "box_multiplier": 1.0, "wraps": False},
    "FOG_MIST": {"window_cm": 4000.0, "box_multiplier": 1.0, "wraps": False},
    "FOG_MIST_STIR": {"window_cm": 4000.0, "box_multiplier": 1.0, "wraps": False},
    "FOG_TILE_SIM": {"window_cm": 3000.0, "box_multiplier": 3.0, "wraps": True},
}
EXPECTED_STAGE_INSTANCE_COUNT = 15
ZONE7A_CENTER = (9000.0, 25000.0)
ZONE8_BAND_Y_CM = -35000.0
ZONE8_FLOOR_CENTER = (8500.0, ZONE8_BAND_Y_CM)
ZONE8_FLOOR_SIZE_CM = (32000.0, 12000.0)
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
    {"label": "LabGreenLight", "location": (5800.0, -300.0, 120.0), "color": (0.25, 1.0, 0.35), "intensity": 2000.0, "radius": 900.0, "volumetric_scattering_intensity": 4.0},
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
)
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


def validate_stage_layout():
    problems = find_fluid_spacing_problems() + find_stirrer_problems()
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
    instance_names = sorted({spec["instance"] for spec in STATIC_BOX_SPECS} | {spec["instance"] for spec in FLUID_SPECS})
    instances = {name: assets.load_asset(f"{MATERIAL_DIR}/{name}") for name in instance_names}
    required = {"게임 모드": game_mode, "큐브": cube, "MPC": collection, "교반기(FX-8n 무대 먼저)": stirrer_class, "유체 액터 클래스": fluid_actor_class}
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

    for label, center, size in FLOOR_SPECS:
        place_cube((center[0], center[1], FLOOR_Z_CM - 25.0), (size[0] / CUBE_SIZE_CM, size[1] / CUBE_SIZE_CM, 0.5), floor_mat, label, "Stage", True)
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
        "zones": {str(index): {"label": label, "center": list(zone_center(index))} for index, label in enumerate(ZONE_LABELS)} | {"7A": {"label": "AmbientWindowAndField", "center": list(ZONE7A_CENTER)}, "7B": {"label": "PassiveDisplays", "y": PASSIVE_DISPLAY_Y_CM}} | {spec["zone"]: {"label": spec["name"], "center": list(spec["center"])} for spec in FLUID_SPECS if spec["zone"].startswith("8")},
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


if __name__ == "__main__":
    run_with_failure_report()
