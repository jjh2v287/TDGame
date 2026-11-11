"""실행 환경: TDGame 언리얼 에디터 안에서 HVOL 비교 무대 LV_TDFluidHvolStage(구역 0~8, X축 30 m 간격: 0 SVOL 대 HVOL·1 소용돌이 쌍·2 카메라 정렬·3 바닥 연기·4 경사+높이 캐시·5 단면·6 태그 대상·7 동굴·8 블록 그림자)와 DA_TDFluid_Hvol* 10개를 만든다. 먼저 editor_make_fluid_heterogeneous_volume_material.py(마스터·MI 7)와 FX-8n 무대(교반기 BP_TDFluidStageStirrer)가 성공해 있어야 하고, FX-9 C++(구동 컴포넌트·DA 슬롯·액터 TerrainHeightCache) 통합·재시작 뒤에 돌린다.
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_hvol_stage.py (기존 DA·맵을 갱신할 때는 Saved/FluidFX/fluid_hvol_stage_request.json 에 {"overwrite": true})
출력: /Game/FluidFX/Presets/DA_TDFluid_Hvol*(10), /Game/FluidFX/Maps/LV_TDFluidHvolStage, Saved/FluidFX/fluid_hvol_stage.json(preset_tuning_sha256·material_look_sha256 = check_fluid_preset_data.py --stage-report 형식, 구역 중심·보정 지표, 6c 굽기 요청 초안, JSON 의존 값 대조)
상태: 2026-10-27 에디터 실행 통과(파동 2 A5-a: DA 10·액터 38·미저장 0, json_dependencies_differ 2건 기록). 구역 4 높이 캐시는 비워 두고 6c(editor_bake_fluid_height_cache.py, assign_actor)가 채운다(cross-plan A-02). 새 조절 구조체는 DA 사본이 없어 구역 0 오프셋(+900 cm)·HVOL 켬·장면 캡처·충돌 마스크는 조절 정본 종류 값에 의존하고, 무대는 빌더 값을 읽어 보고만 한다(DEV-FX9-11). 태그 구역은 Smoke 종류(내부 HVOL 끔)로 태그 HVOL·메시만 구동한다. 블루프린트는 구성 전용. 2026-10-31: 교반기 회전 속도가 피벗 쓰기 뒤 BP 구성 스크립트 재실행으로 유실되고 위치 인수 Rotator(0, v, 0)가 pitch 였던 것을 particle_stage.set_component_properties(한 번에 쓰기·새 핸들로 다시 읽기, 다르면 FAIL)와 yaw 키워드로 고침(py_compile·가짜 unreal 핸들 재생성 시험만), 맵 재생성 필요. 2026-11-11: FloorSmokeCapture 가 TerrainOnly 캡처(랜드스케이프 + TDFluidTerrain 태그 액터만)로 바뀌어 BandFloor 에 TDFluidTerrain 태그를 달고(상자·교반기·벽은 태그 없음) 기대값 scene_capture_content TERRAIN_ONLY 를 더했다. 재생성 뒤 구역 4 높이 캐시를 다시 지정한다.
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
STAGE_MAP_PATH = f"{MAP_DIR}/LV_TDFluidHvolStage"
FLOOR_MAT_PATH = f"{MATERIAL_DIR}/M_TD_FluidFogStageFloor"
WALL_MAT_PATH = f"{MATERIAL_DIR}/M_TD_FluidFogStageWall"
STIRRER_BLUEPRINT_PATH = f"{FLUID_ROOT}/Blueprints/BP_TDFluidStageStirrer"
OUTPUT_MATERIAL_SOURCE_PRESET_PATH = f"{PRESET_DIR}/DA_TDFluid_SmokeSoft"
GAME_MODE_PATH = "/Game/Combat/Blueprints/BP_TDCombatGameMode"
CUBE_MESH_PATH = "/Engine/BasicShapes/Cube"
CUBE_SIZE_CM = 100.0
FLUID_ACTOR_CLASS_PATH = "/Script/TDGame.TDFluidLiveActor"
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_hvol_stage_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_hvol_stage.json")
PRESET_TUNING_RELATIVE_PATH = ("Content", "FluidFX", "PresetData", "TDFluidPresetTuning.json")
MATERIAL_LOOK_RELATIVE_PATH = ("Content", "FluidFX", "PresetData", "TDFluidMaterialLook.json")
PRESET_CHECKER_RELATIVE_PATH = ("Tools", "check_fluid_preset_data.py")
PARTICLE_STAGE_RELATIVE_PATH = ("Tools", "FluidFX", "editor_make_fluid_particle_stage.py")

FLOOR_Z_CM = 0.0
ZONE_SPACING_CM = 3000.0
ZONE_COUNT = 9
ZONE_BAND_HALF_WIDTH_CM = 1500.0
ACTIVATION_DISTANCE_CM = 2000.0
SMOKE_WINDOW_CM = 700.0
HVOL_HEIGHT_CM = 300.0
PAIR_OFFSET_Y_CM = 450.0
HVOL_PAIR_OFFSET_CM = 900.0
STIRRER_ORBIT_RADIUS_CM = 220.0
STIRRER_HEIGHT_CM = 60.0
STIRRER_DEGREES_PER_SEC = 90.0
SLOPE_PITCH_DEGREES = 12.0
SLOPE_SIZE_CM = 1000.0
TERRAIN_MAX_DROP_CM = 2000.0
CAVE_CORRIDOR_WIDTH_CM = 300.0
CAVE_WALL_HEIGHT_CM = 500.0
CAVE_WALL_THICKNESS_CM = 60.0
COLLISION_HEIGHT_ABOVE_BASE_CM = 300.0
SHADOW_BLOCK_Z_CM = 700.0
SHADOW_BLOCK_SIZE_CM = (300.0, 300.0, 40.0)
TAGGED_HVOL_RESOLUTION = 128
PASSIVE_HVOL_OFFSET_Y_CM = 1100.0
PASSIVE_HVOL_SIZE_CM = (600.0, 600.0, 300.0)
HEIGHT_CACHE_TEXEL_CM = 100.0
VIEW_PITCH_DEGREES = -60.0
PLAYER_START = (0.0, -1200.0, 96.0)
TAG_HVOL = "TDFluidHvolTaggedVolume"
TAG_MESH = "TDFluidHvolTaggedMesh"
TAG_TERRAIN = "TDFluidTerrain"
SLOPE_ACTOR_LABEL = "TDGen_HvolSlope"

HVOL_MI = {
    "Smoke": "MI_TD_FluidHeterogeneousVolume_Smoke",
    "Vortex": "MI_TD_FluidHeterogeneousVolume_Vortex",
    "CameraFacing": "MI_TD_FluidHeterogeneousVolume_CameraFacing",
    "FloorSmoke": "MI_TD_FluidHeterogeneousVolume_FloorSmoke",
    "CrossRadial": "MI_TD_FluidHeterogeneousVolume_CrossSectionRadial",
    "CrossRing": "MI_TD_FluidHeterogeneousVolume_CrossSectionRing",
    "Passive": "MI_TD_FluidHeterogeneousVolume_Passive",
}
SVOL_MI = "MI_TD_FluidVolume_SmokeSoft"
PRESET_SPECS = (
    {"name": "HvolPair", "kind": "SMOKE_HETEROGENEOUS", "secondary": "Smoke", "svol": True},
    {"name": "HvolVortex", "kind": "VORTEX_HETEROGENEOUS", "secondary": "Vortex"},
    {"name": "HvolCameraFacing", "kind": "SMOKE_CAMERA_FACING", "secondary": "CameraFacing"},
    {"name": "HvolFloorSmoke", "kind": "FLOOR_SMOKE_CAPTURE", "secondary": "FloorSmoke"},
    {"name": "HvolSlope", "kind": "SMOKE_HETEROGENEOUS", "secondary": "Smoke"},
    {"name": "HvolCrossRadial", "kind": "SMOKE_HETEROGENEOUS", "secondary": "CrossRadial"},
    {"name": "HvolCrossRing", "kind": "SMOKE_HETEROGENEOUS", "secondary": "CrossRing"},
    {"name": "HvolTagged", "kind": "SMOKE", "secondary": "Smoke", "secondary_tag": TAG_HVOL, "primary_tag": TAG_MESH},
    {"name": "HvolCave", "kind": "SMOKE_HETEROGENEOUS", "secondary": "Smoke"},
    {"name": "HvolShadow", "kind": "SMOKE_HETEROGENEOUS", "secondary": "Smoke"},
)
ZONES = (
    {"index": 0, "label": "SvolVsHvol", "metrics": ["HV1", "HV2", "HV3", "HV7"], "actors": (("HvolPair", "HvolPair", (0.0, 0.0)),)},
    {"index": 1, "label": "VortexPair", "metrics": ["HV2"], "actors": (("HvolVortexA", "HvolVortex", (0.0, -PAIR_OFFSET_Y_CM)), ("HvolVortexB", "HvolVortex", (0.0, PAIR_OFFSET_Y_CM)))},
    {"index": 2, "label": "CameraFacing", "metrics": ["HV4"], "actors": (("HvolCameraFacing", "HvolCameraFacing", (0.0, 0.0)),)},
    {"index": 3, "label": "FloorSmoke", "metrics": ["HV5"], "actors": (("HvolFloorSmoke", "HvolFloorSmoke", (0.0, 0.0)),)},
    {"index": 4, "label": "SlopeCache", "metrics": ["HV6"], "actors": (("HvolSlope", "HvolSlope", (0.0, 0.0)),)},
    {"index": 5, "label": "CrossSection", "metrics": ["HV8"], "actors": (("HvolCrossRadial", "HvolCrossRadial", (0.0, -PAIR_OFFSET_Y_CM)), ("HvolCrossRing", "HvolCrossRing", (0.0, PAIR_OFFSET_Y_CM)))},
    {"index": 6, "label": "TaggedTargets", "metrics": ["HV9"], "actors": (("HvolTagged", "HvolTagged", (0.0, 0.0)),)},
    {"index": 7, "label": "Cave", "metrics": ["HV10"], "actors": (("HvolCave", "HvolCave", (0.0, 0.0)),)},
    {"index": 8, "label": "BlockShadow", "metrics": ["HV11"], "actors": (("HvolShadow", "HvolShadow", (0.0, 0.0)),)},
)
FLOOR_BUMPS = ((-200.0, -150.0, 40.0), (100.0, 150.0, 80.0), (250.0, -100.0, 120.0))
FLOOR_COLOR = (0.03, 0.032, 0.036)
WALL_COLOR = (0.06, 0.06, 0.07)
EXPECTED_DRIVER_VALUES = (
    ("HvolPair", "volume_driver", "render_heterogeneous_volume", True),
    ("HvolPair", "volume_driver", "heterogeneous_volume_offset_cm", (0.0, HVOL_PAIR_OFFSET_CM, 0.0)),
    ("HvolFloorSmoke", "terrain_height", "volume_height_source", "SCENE_CAPTURE"),
    ("HvolFloorSmoke", "terrain_height", "scene_capture_content", "TERRAIN_ONLY"),
    ("HvolCave", "terrain_height", "use_height_as_collision_mask", True),
    ("HvolTagged", "volume_driver", "render_heterogeneous_volume", False),
)
BUILDER_FUNCTIONS = {"volume_driver": "build_volume_driver_for_preset_kind", "terrain_height": "build_terrain_height_for_preset_kind"}

REQUIRED_UNREAL_NAMES = (
    "get_engine_subsystem",
    "TDFluidPresetTuningSubsystem",
    "TDFluidPresetTuningSubsystem.reload_preset_tuning",
    "TDFluidPresetTuningSubsystem.get_load_errors",
    "TDFluidPresetAsset",
    "TDFluidPresetAsset.build_config_for_preset_kind",
    "TDFluidPresetKind.SMOKE",
    "TDFluidPresetKind.SMOKE_HETEROGENEOUS",
    "TDFluidPresetKind.VORTEX_HETEROGENEOUS",
    "TDFluidPresetKind.SMOKE_CAMERA_FACING",
    "TDFluidPresetKind.FLOOR_SMOKE_CAPTURE",
    "TDFluidBrushChannel.CHANNEL_R_MUD",
    "EditorAssetLibrary.load_blueprint_class",
    "EditorAssetLibrary.load_asset",
    "EditorLoadingAndSavingUtils.get_dirty_map_packages",
    "EditorLoadingAndSavingUtils.get_dirty_content_packages",
    "SystemLibrary.get_project_directory",
    "DataAssetFactory",
    "RotatingMovementComponent",
    "HeterogeneousVolume",
    "HeterogeneousVolumeComponent",
    "DirectionalLight",
    "SkyLight",
    "PostProcessVolume",
    "PlayerStart",
    "AutoExposureMethod.AEM_MANUAL",
    "ComponentMobility.STATIC",
    "ComponentMobility.MOVABLE",
    "CollisionEnabled.NO_COLLISION",
)
PRESET_REQUIRED_PROPERTIES = (
    "preset_kind", "simulation_config", "default_channel", "override_preset_defaults", "volume_material", "volume_mesh", "volume_height_cm",
    "primary_output_materials", "secondary_output_materials", "tertiary_output_materials", "primary_output_actor_tag", "secondary_output_actor_tag",
)
ACTOR_REQUIRED_PROPERTIES = ("should_follow_player_pawn", "should_use_activation_volume", "activation_distance_cm", "interaction_brush_channel", "live_component", "terrain_height_cache")


def zone_center(index):
    return (index * ZONE_SPACING_CM, 0.0)


def actor_placements():
    placements = []
    for zone in ZONES:
        center = zone_center(zone["index"])
        for label, preset_name, offset in zone["actors"]:
            placements.append({"label": label, "preset": preset_name, "zone": zone["index"], "location": (center[0] + offset[0], center[1] + offset[1], FLOOR_Z_CM)})
    return placements


def window_bounds(center_xy, size_cm=SMOKE_WINDOW_CM):
    half = 0.5 * size_cm
    return (center_xy[0] - half, center_xy[1] - half, center_xy[0] + half, center_xy[1] + half)


def rectangle_gap(first, second):
    gap_x = max(second[0] - first[2], first[0] - second[2], 0.0)
    gap_y = max(second[1] - first[3], first[1] - second[3], 0.0)
    return (gap_x * gap_x + gap_y * gap_y) ** 0.5


def preset_by_name(name):
    return next(spec for spec in PRESET_SPECS if spec["name"] == name)


def validate_stage_layout():
    problems = []
    if ZONE_SPACING_CM <= ACTIVATION_DISTANCE_CM + 0.5 * SMOKE_WINDOW_CM:
        problems.append(f"구역 간격 {ZONE_SPACING_CM} ≤ 활성 거리 {ACTIVATION_DISTANCE_CM} + 창 반폭(이웃 구역도 깨어남)")
    if len(ZONES) != ZONE_COUNT or [zone["index"] for zone in ZONES] != list(range(ZONE_COUNT)):
        problems.append("구역 번호가 0~8 연속이 아님")
    placements = actor_placements()
    windows = [(item["label"], window_bounds(item["location"])) for item in placements]
    for first_index, (first_label, first_bounds) in enumerate(windows):
        if first_bounds[1] < -ZONE_BAND_HALF_WIDTH_CM or first_bounds[3] > ZONE_BAND_HALF_WIDTH_CM:
            problems.append(f"{first_label}: 창이 구역 띠 밖")
        for second_label, second_bounds in windows[first_index + 1:]:
            if rectangle_gap(first_bounds, second_bounds) < 100.0:
                problems.append(f"{first_label}·{second_label}: 창 간격 100 cm 미만")
    pair_center = zone_center(0)
    hvol_window = window_bounds((pair_center[0], pair_center[1] + HVOL_PAIR_OFFSET_CM))
    if rectangle_gap(window_bounds(pair_center), hvol_window) < 100.0:
        problems.append(f"구역 0 HVOL 오프셋 {HVOL_PAIR_OFFSET_CM} 이 SVOL 창과 겹침")
    if hvol_window[3] > ZONE_BAND_HALF_WIDTH_CM:
        problems.append("구역 0 HVOL 오프셋 창이 구역 띠 밖")
    if STIRRER_ORBIT_RADIUS_CM + 50.0 > 0.5 * SMOKE_WINDOW_CM:
        problems.append("교반기 궤도가 창 밖")
    if CAVE_CORRIDOR_WIDTH_CM >= SMOKE_WINDOW_CM or CAVE_WALL_HEIGHT_CM <= COLLISION_HEIGHT_ABOVE_BASE_CM:
        problems.append("동굴 통로가 창보다 넓거나 벽이 충돌 문턱보다 낮음")
    slope_rise_cm = math.tan(math.radians(SLOPE_PITCH_DEGREES)) * 0.5 * SLOPE_SIZE_CM
    if slope_rise_cm >= TERRAIN_MAX_DROP_CM or slope_rise_cm >= HVOL_HEIGHT_CM:
        problems.append(f"경사 높이차 {slope_rise_cm:.0f} cm 가 최대 낙차 또는 HVOL 높이 이상")
    if SHADOW_BLOCK_Z_CM - 0.5 * SHADOW_BLOCK_SIZE_CM[2] <= FLOOR_Z_CM + HVOL_HEIGHT_CM:
        problems.append("그림자 블록이 HVOL 위가 아님")
    if PASSIVE_HVOL_OFFSET_Y_CM - 0.5 * PASSIVE_HVOL_SIZE_CM[1] < 0.5 * SMOKE_WINDOW_CM + 100.0:
        problems.append("수동 HVOL 이 태그 구역 창과 겹침")
    used_instances = {HVOL_MI[spec["secondary"]] for spec in PRESET_SPECS} | {HVOL_MI["Passive"]}
    if used_instances != set(HVOL_MI.values()):
        problems.append("무대가 HVOL MI 7개를 모두 쓰지 않음: " + ", ".join(sorted(set(HVOL_MI.values()) - used_instances)))
    names = [spec["name"] for spec in PRESET_SPECS]
    if len(set(names)) != len(names) or not all(name.startswith("Hvol") for name in names):
        problems.append("DA 이름 중복 또는 Hvol 접두 없음")
    used_presets = {item["preset"] for item in placements}
    if used_presets != set(names):
        problems.append("배치하지 않은 DA: " + ", ".join(sorted(set(names) - used_presets)))
    if [item["label"] for item in placements if item["preset"] == "HvolSlope"] != [SLOPE_ACTOR_LABEL[len("TDGen_"):]]:
        problems.append("경사 구역 액터 이름이 굽기 요청 assign_actor 와 다름")
    return problems


def height_cache_request_draft():
    center = zone_center(4)
    return {"level": STAGE_MAP_PATH, "name": "Slope", "center": [center[0], center[1]], "extent": [SLOPE_SIZE_CM, SLOPE_SIZE_CM], "texel_cm": HEIGHT_CACHE_TEXEL_CM, "top_z": 2000.0, "bottom_z": -2000.0, "overwrite": False, "verify": False, "assign_actor": SLOPE_ACTOR_LABEL}


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


def asset_disk_path(asset_path, extension):
    return project_path("Content", *asset_path[len("/Game/"):].split("/")) + extension


def preset_asset_name(name):
    return f"DA_TDFluid_{name}"


def preset_asset_path(name):
    return f"{PRESET_DIR}/{preset_asset_name(name)}"


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


def object_path_text(value):
    if value is None:
        return ""
    if hasattr(value, "get_path_name"):
        return value.get_path_name()
    if hasattr(value, "export_text"):
        return str(value.export_text())
    return str(value)


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


def load_module(module_name, relative_parts):
    module_path = project_path(*relative_parts)
    module_spec = importlib.util.spec_from_file_location(module_name, module_path)
    if module_spec is None or module_spec.loader is None:
        raise RuntimeError(f"모듈 없음: {module_path}")
    module = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(module)
    return module


def read_file_sha256(relative_parts):
    path = project_path(*relative_parts)
    if not os.path.isfile(path):
        raise RuntimeError(f"정본 파일 없음: {path}")
    with open(path, "rb") as source_file:
        return hashlib.sha256(source_file.read()).hexdigest()


def reload_preset_tuning():
    tuning_path = project_path(*PRESET_TUNING_RELATIVE_PATH)
    before = read_file_sha256(PRESET_TUNING_RELATIVE_PATH)
    problems = load_module("check_fluid_preset_data", PRESET_CHECKER_RELATIVE_PATH).check_tuning_file(tuning_path)
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


def normalize_value(value):
    if hasattr(value, "x") and hasattr(value, "y") and hasattr(value, "z"):
        return (round(float(value.x), 3), round(float(value.y), 3), round(float(value.z), 3))
    if isinstance(value, (tuple, list)):
        return tuple(round(float(item), 3) for item in value)
    if hasattr(value, "name") and not isinstance(value, (bool, int, float, str)):
        return str(value.name)
    return value


def compare_builder_values():
    records = []
    for preset_name, builder_key, field, expected in EXPECTED_DRIVER_VALUES:
        kind = getattr(unreal.TDFluidPresetKind, preset_by_name(preset_name)["kind"])
        builder = getattr(unreal.TDFluidPresetAsset, BUILDER_FUNCTIONS[builder_key], None)
        if builder is None:
            records.append({"preset": preset_name, "field": field, "expected": expected, "actual": None, "status": "builder_missing"})
            continue
        try:
            actual = normalize_value(builder(kind).get_editor_property(field))
        except Exception as error:
            records.append({"preset": preset_name, "field": field, "expected": expected, "actual": None, "status": f"read_failed: {error}"})
            continue
        expected_normalized = normalize_value(expected) if not isinstance(expected, str) else expected
        matches = expected_normalized in str(actual).upper() if isinstance(expected, str) else actual == expected_normalized
        records.append({"preset": preset_name, "field": field, "expected": expected, "actual": actual, "status": "ok" if matches else "json_differs"})
    return records


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


def load_or_create_preset(name):
    asset_path = preset_asset_path(name)
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        preset = unreal.EditorAssetLibrary.load_asset(asset_path)
        if not isinstance(preset, unreal.TDFluidPresetAsset):
            raise RuntimeError(f"기존 프리셋 에셋 형식 불일치: {asset_path}")
        return preset
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.TDFluidPresetAsset)
    preset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(preset_asset_name(name), PRESET_DIR, unreal.TDFluidPresetAsset, factory)
    if preset is None:
        raise RuntimeError(f"프리셋 생성 실패: {asset_path}")
    return preset


def configure_preset(spec, instances, svol_material, cube, output_material):
    preset_kind = getattr(unreal.TDFluidPresetKind, spec["kind"])
    preset = load_or_create_preset(spec["name"])
    preset.set_editor_property("preset_kind", preset_kind)
    preset.set_editor_property("default_channel", unreal.TDFluidBrushChannel.CHANNEL_R_MUD)
    preset.set_editor_property("override_preset_defaults", False)
    preset.set_editor_property("simulation_config", unreal.TDFluidPresetAsset.build_config_for_preset_kind(preset_kind))
    preset.set_editor_property("volume_material", svol_material if spec.get("svol") else None)
    preset.set_editor_property("volume_mesh", cube)
    preset.set_editor_property("volume_height_cm", HVOL_HEIGHT_CM)
    if output_material is not None and has_editor_property(preset, "output_material"):
        preset.set_editor_property("output_material", output_material)
    secondary = instances[HVOL_MI[spec["secondary"]]]
    preset.set_editor_property("primary_output_materials", [])
    preset.set_editor_property("secondary_output_materials", [secondary])
    preset.set_editor_property("tertiary_output_materials", [])
    preset.set_editor_property("primary_output_actor_tag", spec.get("primary_tag", "None"))
    preset.set_editor_property("secondary_output_actor_tag", spec.get("secondary_tag", "None"))
    stored = list(preset.get_editor_property("secondary_output_materials"))
    if len(stored) != 1 or secondary.get_path_name() not in object_path_text(stored[0]):
        raise RuntimeError(f"Secondary 슬롯 재질 다시 읽기 불일치: {preset_asset_path(spec['name'])}")
    save_asset(preset, preset_asset_path(spec["name"]))
    return preset, preset.resolve_effective_config()


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
        raise RuntimeError("언리얼 Python 이름 없음(FX-9 C++ 통합·재시작 확인): " + ", ".join(missing_names))
    layout_problems = validate_stage_layout()
    if layout_problems:
        raise RuntimeError("무대 배치 자체 점검 실패: " + " | ".join(layout_problems))
    if editor.get_game_world() is not None:
        raise RuntimeError("PIE를 종료한 뒤 실행하세요.")

    overwrite = read_overwrite_request()
    guarded = [preset_asset_path(spec["name"]) for spec in PRESET_SPECS] + [STAGE_MAP_PATH]
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

    game_mode = assets.load_blueprint_class(GAME_MODE_PATH)
    cube = assets.load_asset(CUBE_MESH_PATH)
    stirrer_class = assets.load_blueprint_class(STIRRER_BLUEPRINT_PATH)
    particle_stage = load_module("td_fluid_particle_stage", PARTICLE_STAGE_RELATIVE_PATH)
    fluid_actor_class = unreal.load_class(None, FLUID_ACTOR_CLASS_PATH)
    instances = {name: assets.load_asset(f"{MATERIAL_DIR}/{name}") for name in HVOL_MI.values()}
    svol_material = assets.load_asset(f"{MATERIAL_DIR}/{SVOL_MI}")
    output_source = assets.load_asset(OUTPUT_MATERIAL_SOURCE_PRESET_PATH)
    output_material = output_source.get_editor_property("output_material") if output_source is not None and has_editor_property(output_source, "output_material") else None
    required = {"게임 모드": game_mode, "큐브": cube, "교반기(FX-8n 무대 먼저)": stirrer_class, "유체 액터 클래스": fluid_actor_class, f"SVOL MI {SVOL_MI}": svol_material}
    required.update({f"HVOL MI {name}": value for name, value in instances.items()})
    missing = [name for name, value in required.items() if value is None]
    if missing:
        raise RuntimeError("필수 에셋 또는 클래스 없음(editor_make_fluid_heterogeneous_volume_material.py·FX-8n 무대 먼저): " + ", ".join(missing))
    contract_problems = check_cpp_contract(fluid_actor_class)
    if contract_problems:
        raise RuntimeError("C++ 계약 속성 없음(FX-9 묶음 A·B·C 통합 확인): " + ", ".join(contract_problems))

    print("단계: 재질·프리셋")
    floor_mat, floor_created = load_or_create_matte_material(FLOOR_MAT_PATH, FLOOR_COLOR)
    wall_mat, wall_created = load_or_create_matte_material(WALL_MAT_PATH, WALL_COLOR)
    ensure_directory(PRESET_DIR)
    presets = {spec["name"]: configure_preset(spec, instances, svol_material, cube, output_material) for spec in PRESET_SPECS}
    builder_records = compare_builder_values()

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
        actor.set_folder_path("TDGen/FluidFX/HvolStage/" + folder)
        created.append(actor)
        return actor

    def spawn(actor_class, location, label, folder, rotation=None):
        return finish(actors.spawn_actor_from_class(actor_class, unreal.Vector(*location), rotation or unreal.Rotator()), label, folder)

    def place_cube(location, size_cm, material, label, folder, rotation=None, has_collision=True):
        actor = finish(actors.spawn_actor_from_object(cube, unreal.Vector(*location), rotation or unreal.Rotator()), label, folder)
        actor.set_actor_scale3d(unreal.Vector(*(value / CUBE_SIZE_CM for value in size_cm)))
        component = actor.static_mesh_component
        component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        component.set_material(0, material)
        if not has_collision:
            component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            component.set_collision_profile_name("NoCollision")
            component.set_cast_shadow(False)
        return actor

    def place_heterogeneous_volume(center_xy, size_cm, material, label, tags):
        actor = spawn(unreal.HeterogeneousVolume, (center_xy[0], center_xy[1], FLOOR_Z_CM + 0.5 * size_cm[2]), label, "Targets")
        component = actor.get_component_by_class(unreal.HeterogeneousVolumeComponent)
        if component is None:
            raise RuntimeError(f"HVOL 컴포넌트 없음: {label}")
        component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        component.set_editor_property("volume_resolution", unreal.IntVector(TAGGED_HVOL_RESOLUTION, TAGGED_HVOL_RESOLUTION, TAGGED_HVOL_RESOLUTION))
        component.set_editor_property("pivot_at_centroid", True)
        actor.set_actor_scale3d(unreal.Vector(*(value / TAGGED_HVOL_RESOLUTION for value in size_cm)))
        component.set_material(0, material)
        actor.set_editor_property("tags", list(tags))
        return actor

    band_length = (ZONE_COUNT - 1) * ZONE_SPACING_CM + 2.0 * ZONE_BAND_HALF_WIDTH_CM
    band_floor = place_cube((0.5 * (ZONE_COUNT - 1) * ZONE_SPACING_CM, 0.0, FLOOR_Z_CM - 25.0), (band_length, 2.0 * ZONE_BAND_HALF_WIDTH_CM, 50.0), floor_mat, "BandFloor", "Stage")
    band_floor.set_editor_property("tags", [TAG_TERRAIN])
    sun = spawn(unreal.DirectionalLight, (0, 0, 2000), "HvolStageSun", "Lighting", unreal.Rotator(pitch=-50, yaw=150))
    sun.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_intensity(4.0)
    sky = spawn(unreal.SkyLight, (0, 0, 1500), "HvolStageSkyLight", "Lighting")
    sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", True)
    sky.light_component.set_intensity(0.3)
    post_process = spawn(unreal.PostProcessVolume, (0, 0, 0), "HvolStageExposure", "Lighting")
    post_process.set_editor_property("unbound", True)
    settings = post_process.settings
    for name, value in (("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL), ("auto_exposure_apply_physical_camera_exposure", False), ("auto_exposure_bias", 0.0)):
        settings.set_editor_property("override_" + name, True)
        settings.set_editor_property(name, value)
    post_process.set_editor_property("settings", settings)
    spawn(unreal.PlayerStart, PLAYER_START, "HvolStagePlayerStart", "Gameplay")

    zone3 = zone_center(3)
    for index, (offset_x, offset_y, height_cm) in enumerate(FLOOR_BUMPS):
        place_cube((zone3[0] + offset_x, zone3[1] + offset_y, FLOOR_Z_CM + 0.5 * height_cm), (150.0, 150.0, height_cm), wall_mat, f"FloorBump{index}", "Terrain")
    zone4 = zone_center(4)
    slope_rise_cm = math.tan(math.radians(SLOPE_PITCH_DEGREES)) * 0.5 * SLOPE_SIZE_CM
    place_cube((zone4[0], zone4[1], FLOOR_Z_CM + 0.5 * slope_rise_cm), (SLOPE_SIZE_CM, SLOPE_SIZE_CM, 40.0), wall_mat, "SlopeRamp", "Terrain", unreal.Rotator(pitch=SLOPE_PITCH_DEGREES))
    zone7 = zone_center(7)
    wall_offset = 0.5 * (CAVE_CORRIDOR_WIDTH_CM + CAVE_WALL_THICKNESS_CM)
    for label, location, size in (
        ("CaveWallNorth", (zone7[0], zone7[1] + wall_offset, 0.5 * CAVE_WALL_HEIGHT_CM), (900.0, CAVE_WALL_THICKNESS_CM, CAVE_WALL_HEIGHT_CM)),
        ("CaveWallSouth", (zone7[0], zone7[1] - wall_offset, 0.5 * CAVE_WALL_HEIGHT_CM), (900.0, CAVE_WALL_THICKNESS_CM, CAVE_WALL_HEIGHT_CM)),
        ("CaveWallEnd", (zone7[0] + 200.0, zone7[1], 0.5 * CAVE_WALL_HEIGHT_CM), (CAVE_WALL_THICKNESS_CM, CAVE_CORRIDOR_WIDTH_CM, CAVE_WALL_HEIGHT_CM)),
    ):
        place_cube(location, size, wall_mat, label, "Terrain")
    zone8 = zone_center(8)
    place_cube((zone8[0], zone8[1], SHADOW_BLOCK_Z_CM), SHADOW_BLOCK_SIZE_CM, wall_mat, "ShadowBlock", "Terrain")

    fluid_records = []
    for placement in actor_placements():
        preset, effective = presets[placement["preset"]]
        spec = preset_by_name(placement["preset"])
        actor = spawn(fluid_actor_class, placement["location"], placement["label"], "Sim")
        actor.set_editor_property("should_follow_player_pawn", False)
        actor.set_editor_property("should_use_activation_volume", True)
        actor.set_editor_property("activation_distance_cm", ACTIVATION_DISTANCE_CM)
        actor.set_editor_property("interaction_brush_channel", unreal.TDFluidBrushChannel.CHANNEL_R_MUD)
        actor.set_editor_property("terrain_height_cache", None)
        live_component = actor.get_editor_property("live_component")
        live_component.set_editor_property("active_preset", preset)
        live_component.set_editor_property("should_init_system_state_from_preset", True)
        live_component.set_editor_property("simulation_config", effective)
        location = placement["location"]
        stirrer = spawn(stirrer_class, (location[0] - STIRRER_ORBIT_RADIUS_CM, location[1], FLOOR_Z_CM + STIRRER_HEIGHT_CM), placement["label"] + "Stirrer", "Sim")
        motion = {"pivot_translation": unreal.Vector(STIRRER_ORBIT_RADIUS_CM, 0.0, 0.0), "rotation_rate": unreal.Rotator(roll=0.0, pitch=0.0, yaw=STIRRER_DEGREES_PER_SEC)}
        particle_stage.set_component_properties(stirrer, unreal.RotatingMovementComponent, motion)
        fluid_records.append({"label": actor.get_actor_label(), "preset": preset_asset_name(spec["name"]), "kind": spec["kind"], "zone": placement["zone"], "location": list(location), "secondary_material": HVOL_MI[spec["secondary"]], "svol": bool(spec.get("svol")), "stirrer": stirrer.get_actor_label()})

    zone6 = zone_center(6)
    tagged_records = [
        place_heterogeneous_volume(zone6, (SMOKE_WINDOW_CM, SMOKE_WINDOW_CM, HVOL_HEIGHT_CM), instances[HVOL_MI["Smoke"]], "TaggedHvol", (TAG_HVOL,)).get_actor_label(),
        place_cube((zone6[0], zone6[1], FLOOR_Z_CM + 2.0), (SMOKE_WINDOW_CM, SMOKE_WINDOW_CM, 2.0), floor_mat, "TaggedMesh", "Targets", has_collision=False).get_actor_label(),
    ]
    tagged_mesh = created[-1]
    tagged_mesh.set_editor_property("tags", [TAG_MESH])
    passive = place_heterogeneous_volume((zone6[0], zone6[1] + PASSIVE_HVOL_OFFSET_Y_CM), PASSIVE_HVOL_SIZE_CM, instances[HVOL_MI["Passive"]], "PassiveHvol", ())

    print("단계: 레벨 저장")
    if not levels.save_current_level():
        raise RuntimeError(f"무대 레벨 저장 실패: {STAGE_MAP_PATH}")
    missing_files = find_missing_saved_files([preset_asset_path(spec["name"]) for spec in PRESET_SPECS])
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
        "view_pitch_degrees": VIEW_PITCH_DEGREES,
        "zones": {str(zone["index"]): {"label": zone["label"], "center": list(zone_center(zone["index"])), "metrics": zone["metrics"]} for zone in ZONES},
        "fluid_actors": fluid_records,
        "tagged_targets": {"actors": tagged_records, "volume_tag": TAG_HVOL, "mesh_tag": TAG_MESH},
        "passive_hvol": passive.get_actor_label(),
        "terrain_tagged_actors": [band_floor.get_actor_label()],
        "builder_values": builder_records,
        "json_dependencies_differ": [record for record in builder_records if record["status"] != "ok"],
        "height_cache_request_draft": height_cache_request_draft(),
        "deviations": ["광원·지형·교반기·HVOL 높이 300 cm 는 무대 상수(D-64 ②)", "구역 0 오프셋·HVOL 켬·장면 캡처·충돌 마스크는 조절 정본 종류 값(DEV-FX9-11), 다르면 json_dependencies_differ 에 기록만"],
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
