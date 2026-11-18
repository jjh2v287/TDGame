"""실행 환경: TDGame 언리얼 에디터 안(Python 원격 실행, PIE 꺼짐, 저장 안 된 변경 없음). 먼저 editor_make_fog3d_material.py 로 M_TD_Fog3DVolume 이 있어야 하고, 구역 액터 ATDFog3DZoneActor 와 장애물 부품이 빌드된 에디터여야 한다. 3D 안개 전용 시험 무대 LV_TDFog3DStage 하나만 만든다(기존 맵·기존 에셋은 고치지 않고, 에셋은 새로 만들지 않는다).
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fog3d_stage.py (PowerShell, L-repo-01. 맵이 이미 있으면 Saved/FluidFX/fog3d_stage_request.json 에 {"overwrite": true} 가 있을 때만 비우고 다시 만든다). 에디터 없는 점검: python -B Tools/FluidFX/editor_make_fog3d_stage.py --self-test
출력: /Game/FluidFX/Maps/LV_TDFog3DStage(바닥 윗면 z=0 평지 120 m, 구역 액터 TDGen_Fog3D_Zone 중심 (0,0,150) 반크기 (3000,3000,150), 물체 상자 둘·벽 하나, 햇빛·하늘·대기·높이 안개·고정 노출, PlayerStart), Saved/FluidFX/fog3d_stage.json(status·액터 목록·경계·preset_tuning_sha256·촬영 명령)
상태: 에디터 실행 전: 미검증(py_compile·--self-test 만. 구역 액터 속성 이름은 계약 3절 ZoneHalfExtentCm·VolumeMaterial·ZoneVolume 기준이고 TDFog3DZoneActor.h 가 생기면 --self-test 가 대조한다. 노출·조명 값은 2D 안개 무대 상수를 그대로 가져온 것이라 화면 판정 전).
"""
import hashlib
import importlib.util
import json
import math
import os
import re
import sys

try:
    import unreal
except ModuleNotFoundError:
    unreal = None


FLUID_ROOT = "/Game/FluidFX"
STAGE_MAP_PATH = f"{FLUID_ROOT}/Maps/LV_TDFog3DStage"
MAP_DIR = f"{FLUID_ROOT}/Maps"
VOLUME_MATERIAL_PATH = f"{FLUID_ROOT}/Materials/M_TD_Fog3DVolume"
FLOOR_MATERIAL_PATH = f"{FLUID_ROOT}/Materials/M_TD_FluidFogStageFloor"
WALL_MATERIAL_PATH = f"{FLUID_ROOT}/Materials/M_TD_FluidFogStageWall"
FALLBACK_MATERIAL_PATH = "/Engine/BasicShapes/BasicShapeMaterial"
GAME_MODE_PATH = "/Game/Combat/Blueprints/BP_TDCombatGameMode"
CUBE_MESH_PATH = "/Engine/BasicShapes/Cube"
ZONE_ACTOR_CLASS_PATH = "/Script/TDGame.TDFog3DZoneActor"
OBSTACLE_COMPONENT_CLASS_PATH = "/Script/TDGame.TDFluidObstacleComponent"
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fog3d_stage_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fog3d_stage.json")
PRESET_TUNING_RELATIVE_PATH = ("Content", "FluidFX", "PresetData", "TDFluidPresetTuning.json")
PRESET_CHECKER_RELATIVE_PATH = ("Tools", "check_fluid_preset_data.py")
ZONE_HEADER_RELATIVE_PATH = ("Source", "TDGame", "FluidFX", "TDFog3DZoneActor.h")
LOOKDEV_VARIANT_FILES = ("fog3d_lookdev_1b.json", "fog3d_lookdev_1b_walk.json")

LABEL_PREFIX = "TDGen_"
ZONE_LABEL = "Fog3D_Zone"
ACTOR_FOLDER = "TDGen/FluidFX/Fog3DStage"
CUBE_SIZE_CM = 100.0
FLOOR_TOP_Z_CM = 0.0
FLOOR_SIZE_CM = (12000.0, 12000.0, 100.0)
FLOOR_CENTER_CM = (0.0, 0.0, FLOOR_TOP_Z_CM - 0.5 * FLOOR_SIZE_CM[2])
ZONE_CENTER_CM = (0.0, 0.0, 150.0)
ZONE_HALF_EXTENT_CM = (3000.0, 3000.0, 150.0)
PLAYER_START_CM = (-1000.0, 0.0, 100.0)
WALK_DISTANCE_CM = 400.0
WALK_CORRIDOR_HALF_WIDTH_CM = 50.0
OBSTACLE_MINIMUM_GAP_CM = 400.0
DEFAULT_ZONE_EDGE_FADE_CM = 500.0
DEFAULT_ZONE_ENTRY_VERTICAL_MARGIN_CM = 200.0
BOUNDS_TOLERANCE_CM = 1.0
OBSTACLE_SPECS = (
    {"label": "Fog3D_LowBox", "center": (500.0, -400.0), "size": (150.0, 150.0, 30.0)},
    {"label": "Fog3D_HighBox", "center": (500.0, 400.0), "size": (150.0, 150.0, 180.0)},
    {"label": "Fog3D_Wall", "center": (1100.0, 0.0), "size": (10.0, 300.0, 250.0)},
)
SUN_LOCATION_CM = (0.0, 0.0, 2000.0)
SUN_ROTATION_DEGREES = {"pitch": -35.0, "yaw": 100.0}
SUN_INTENSITY = 4.0
SKY_LIGHT_INTENSITY = 0.3
HEIGHT_FOG_PROPERTIES = (
    ("fog_density", 0.002),
    ("enable_volumetric_fog", True),
    ("volumetric_fog_scattering_distribution", 0.35),
    ("volumetric_fog_distance", 12000.0),
)
ZONE_PROPERTY_NAMES = {"half_extent": "zone_half_extent_cm", "material": "volume_material", "volume": "zone_volume"}
ZONE_HEADER_NAMES = ("ZoneHalfExtentCm", "VolumeMaterial", "ZoneVolume")
LOOKDEV_COMMAND = "python Tools/FluidFX/pie_lookdev_fluid.py --variants Tools/FluidFX/fog3d_lookdev_1b.json --level /Game/FluidFX/Maps/LV_TDFog3DStage --stop-pie"

REQUIRED_UNREAL_NAMES = (
    "get_engine_subsystem",
    "get_editor_subsystem",
    "load_class",
    "get_default_object",
    "TDFluidPresetTuningSubsystem",
    "TDFluidPresetTuningSubsystem.reload_preset_tuning",
    "TDFluidPresetTuningSubsystem.get_load_errors",
    "EditorAssetLibrary.load_blueprint_class",
    "EditorAssetLibrary.load_asset",
    "EditorAssetLibrary.does_asset_exist",
    "EditorLoadingAndSavingUtils.get_dirty_map_packages",
    "EditorLoadingAndSavingUtils.get_dirty_content_packages",
    "SystemLibrary.get_project_directory",
    "LevelEditorSubsystem",
    "EditorActorSubsystem",
    "UnrealEditorSubsystem",
    "StaticMeshComponent",
    "SubobjectDataSubsystem",
    "AddNewSubobjectParams",
    "SubobjectDataBlueprintFunctionLibrary",
    "ExponentialHeightFog",
    "ExponentialHeightFogComponent",
    "DirectionalLight",
    "SkyLight",
    "SkyAtmosphere",
    "PostProcessVolume",
    "PlayerStart",
    "WorldSettings",
    "Brush",
    "AutoExposureMethod.AEM_MANUAL",
    "ComponentMobility.STATIC",
    "ComponentMobility.MOVABLE",
)


def box_xy_bounds(center, size):
    return (center[0] - 0.5 * size[0], center[1] - 0.5 * size[1], center[0] + 0.5 * size[0], center[1] + 0.5 * size[1])


def rectangle_gap(first, second):
    gap_x = max(second[0] - first[2], first[0] - second[2], 0.0)
    gap_y = max(second[1] - first[3], first[1] - second[3], 0.0)
    return (gap_x * gap_x + gap_y * gap_y) ** 0.5


def is_inside_bounds(inner, outer):
    return inner[0] >= outer[0] and inner[1] >= outer[1] and inner[2] <= outer[2] and inner[3] <= outer[3]


def actor_label(label):
    return LABEL_PREFIX + label


def build_default_layout():
    return {
        "zone_center": ZONE_CENTER_CM,
        "zone_half_extent": ZONE_HALF_EXTENT_CM,
        "floor_center": FLOOR_CENTER_CM,
        "floor_size": FLOOR_SIZE_CM,
        "player_start": PLAYER_START_CM,
        "obstacles": OBSTACLE_SPECS,
        "walk_distance_cm": WALK_DISTANCE_CM,
    }


def find_stage_layout_problems(layout, edge_fade_cm, vertical_margin_cm):
    problems = []
    center, half = layout["zone_center"], layout["zone_half_extent"]
    zone_xy = box_xy_bounds(center, (2.0 * half[0], 2.0 * half[1]))
    zone_bottom, zone_top = center[2] - half[2], center[2] + half[2]
    if min(half[0], half[1]) < edge_fade_cm:
        problems.append(f"구역 반크기 {half[:2]} < 가장자리 흐림 폭 {edge_fade_cm}")
    floor_top = layout["floor_center"][2] + 0.5 * layout["floor_size"][2]
    if abs(floor_top - zone_bottom) > 1e-6:
        problems.append(f"바닥 윗면 {floor_top} ≠ 구역 아래면 {zone_bottom}")
    floor_xy = box_xy_bounds(layout["floor_center"], layout["floor_size"])
    if not is_inside_bounds(zone_xy, floor_xy):
        problems.append("구역이 바닥 밖으로 나감")
    fade_inner = (zone_xy[0] + edge_fade_cm, zone_xy[1] + edge_fade_cm, zone_xy[2] - edge_fade_cm, zone_xy[3] - edge_fade_cm)
    start = layout["player_start"]
    if not (zone_xy[0] <= start[0] <= zone_xy[2] and zone_xy[1] <= start[1] <= zone_xy[3] and zone_bottom - vertical_margin_cm <= start[2] <= zone_top + vertical_margin_cm):
        problems.append(f"시작점 {start} 이 구역(높이 여유 {vertical_margin_cm}) 밖")
    elif not (fade_inner[0] <= start[0] <= fade_inner[2] and fade_inner[1] <= start[1] <= fade_inner[3]):
        problems.append(f"시작점 {start} 이 가장자리 흐림 띠 안")
    walk_end = (start[0] + layout["walk_distance_cm"], start[1])
    if not (fade_inner[0] <= walk_end[0] <= fade_inner[2] and fade_inner[1] <= walk_end[1] <= fade_inner[3]):
        problems.append(f"걷기 끝 {walk_end} 이 가장자리 흐림 띠 안이거나 구역 밖")
    corridor = (min(start[0], walk_end[0]), start[1] - WALK_CORRIDOR_HALF_WIDTH_CM, max(start[0], walk_end[0]), start[1] + WALK_CORRIDOR_HALF_WIDTH_CM)
    obstacles = layout["obstacles"]
    bounds_by_label = {}
    for spec in obstacles:
        bounds = box_xy_bounds(spec["center"], spec["size"])
        bounds_by_label[spec["label"]] = bounds
        if not is_inside_bounds(bounds, fade_inner):
            problems.append(f"{spec['label']}: 구역 안 흐림 폭({edge_fade_cm}) 밖에 놓이지 않음")
        if spec["size"][2] > 2.0 * half[2] or floor_top + spec["size"][2] > zone_top:
            problems.append(f"{spec['label']}: 높이 {spec['size'][2]} 이 구역 윗면 {zone_top} 을 넘음")
        if rectangle_gap(bounds, corridor) <= 0.0:
            problems.append(f"{spec['label']}: 걷는 길과 겹침")
        if rectangle_gap(bounds, box_xy_bounds(start, (0.0, 0.0))) <= WALK_CORRIDOR_HALF_WIDTH_CM:
            problems.append(f"{spec['label']}: 시작점과 너무 가까움")
    for first_index, first in enumerate(obstacles):
        for second in obstacles[first_index + 1:]:
            gap = rectangle_gap(bounds_by_label[first["label"]], bounds_by_label[second["label"]])
            if gap < OBSTACLE_MINIMUM_GAP_CM:
                problems.append(f"{first['label']}·{second['label']} 간격 {gap:.0f} < {OBSTACLE_MINIMUM_GAP_CM}")
    labels = [actor_label(spec["label"]) for spec in obstacles] + [actor_label(ZONE_LABEL)]
    if len(set(labels)) != len(labels):
        problems.append("액터 라벨이 겹침")
    return problems


def find_missing_header_names(header_text):
    return [name for name in ZONE_HEADER_NAMES if not re.search(r"\b" + name + r"\b", header_text)]


def project_path(*parts):
    if unreal is not None:
        return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)
    return os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), *parts)


def read_file_sha256(relative_parts):
    path = project_path(*relative_parts)
    if not os.path.isfile(path):
        raise RuntimeError(f"정본 파일 없음: {path}")
    with open(path, "rb") as source_file:
        return hashlib.sha256(source_file.read()).hexdigest()


def read_fog3d_settings():
    path = project_path(*PRESET_TUNING_RELATIVE_PATH)
    if not os.path.isfile(path):
        return None
    with open(path, encoding="utf-8") as tuning_file:
        tuning = json.load(tuning_file)
    settings = tuning.get("Globals", {}).get("TDFog3DSettings") if isinstance(tuning, dict) else None
    return settings if isinstance(settings, dict) else None


def resolve_layout_thresholds(settings):
    if settings is None:
        return DEFAULT_ZONE_EDGE_FADE_CM, DEFAULT_ZONE_ENTRY_VERTICAL_MARGIN_CM
    return float(settings.get("ZoneEdgeFadeCm", DEFAULT_ZONE_EDGE_FADE_CM)), float(settings.get("ZoneEntryVerticalMarginCm", DEFAULT_ZONE_ENTRY_VERTICAL_MARGIN_CM))


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
    return [name for name in names if name != STAGE_MAP_PATH] if overwrite else names


def load_preset_checker():
    checker_path = project_path(*PRESET_CHECKER_RELATIVE_PATH)
    module_spec = importlib.util.spec_from_file_location("check_fluid_preset_data", checker_path)
    if module_spec is None or module_spec.loader is None:
        raise RuntimeError(f"프리셋 정본 검사기 없음: {checker_path}")
    checker = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(checker)
    return checker


def reload_preset_tuning():
    tuning_path = project_path(*PRESET_TUNING_RELATIVE_PATH)
    before = read_file_sha256(PRESET_TUNING_RELATIVE_PATH)
    if read_fog3d_settings() is None:
        raise RuntimeError("프리셋 정본에 Globals.TDFog3DSettings 가 없습니다(1b-1 데이터 칸이 먼저).")
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


def ensure_directory(directory_path):
    if unreal.EditorAssetLibrary.does_directory_exist(directory_path):
        return
    if not unreal.EditorAssetLibrary.make_directory(directory_path):
        raise RuntimeError(f"폴더 생성 실패: {directory_path}")


def open_empty_stage_level(levels, actors):
    if not unreal.EditorAssetLibrary.does_asset_exist(STAGE_MAP_PATH):
        if not levels.new_level(STAGE_MAP_PATH):
            raise RuntimeError(f"무대 레벨 생성 실패: {STAGE_MAP_PATH}")
        return
    if not levels.load_level(STAGE_MAP_PATH):
        raise RuntimeError(f"기존 무대 레벨 열기 실패: {STAGE_MAP_PATH}")
    for actor in actors.get_all_level_actors():
        if is_default_level_actor(actor):
            continue
        if not actors.destroy_actor(actor):
            raise RuntimeError(f"기존 무대 레벨 액터 제거 실패: {actor.get_actor_label()}")


def is_default_level_actor(actor):
    return isinstance(actor, unreal.WorldSettings) or type(actor) is unreal.Brush


def pick_stage_material(assets, material_path):
    if assets.does_asset_exist(material_path):
        return assets.load_asset(material_path), material_path
    return assets.load_asset(FALLBACK_MATERIAL_PATH), FALLBACK_MATERIAL_PATH


def find_zone_volume_component(actor):
    try:
        component = actor.get_editor_property(ZONE_PROPERTY_NAMES["volume"])
        if component is not None:
            return component
    except Exception:
        pass
    components = list(actor.get_components_by_class(unreal.StaticMeshComponent))
    return components[0] if len(components) == 1 else None


def add_obstacle_component(actor, component_class):
    subobjects = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    handles = list(subobjects.k2_gather_subobject_data_for_instance(actor))
    if not handles:
        raise RuntimeError("액터 서브오브젝트 목록이 비었습니다: " + actor.get_actor_label())
    params = unreal.AddNewSubobjectParams()
    params.set_editor_property("parent_handle", handles[0])
    params.set_editor_property("new_class", component_class)
    new_handle, failure_reason = subobjects.add_new_subobject(params)
    if str(failure_reason):
        raise RuntimeError(f"장애물 부품 추가 실패: {actor.get_actor_label()} {failure_reason}")
    component = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(subobjects.k2_find_subobject_data_from_handle(new_handle))
    if component is None or actor.get_component_by_class(component_class) is None:
        raise RuntimeError("장애물 부품 확인 실패: " + actor.get_actor_label())
    component.set_editor_property("use_owner_bounds", True)
    if component.get_editor_property("use_owner_bounds") is not True:
        raise RuntimeError("use_owner_bounds 지정 확인 실패: " + actor.get_actor_label())
    return component


def describe_bounds(actor):
    origin, extent = actor.get_actor_bounds(False)
    return [origin.x - extent.x, origin.y - extent.y, origin.z - extent.z, origin.x + extent.x, origin.y + extent.y, origin.z + extent.z]


def require_bounds(actor, label, center, size):
    bounds = describe_bounds(actor)
    expected = [center[0] - 0.5 * size[0], center[1] - 0.5 * size[1], center[2] - 0.5 * size[2], center[0] + 0.5 * size[0], center[1] + 0.5 * size[1], center[2] + 0.5 * size[2]]
    mismatches = [index for index in range(6) if abs(bounds[index] - expected[index]) > BOUNDS_TOLERANCE_CM]
    if mismatches:
        raise RuntimeError(f"경계 불일치 {label}: 실제 {bounds}, 기대 {expected}")
    return bounds


def destroy_actors(actors, created):
    for actor in created:
        try:
            actors.destroy_actor(actor)
        except Exception:
            pass


def find_unexpected_level_actors(actors, created):
    created_names = {actor.get_name() for actor in created}
    return sorted(actor.get_actor_label() for actor in actors.get_all_level_actors() if not is_default_level_actor(actor) and actor.get_name() not in created_names)


def write_report(report):
    report_path = project_path(*REPORT_RELATIVE_PATH)
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, "w", encoding="utf-8") as report_file:
        json.dump(report, report_file, ensure_ascii=False, indent=2, default=str)
    return report_path


def check_zone_actor_contract(zone_class):
    default_actor = unreal.get_default_object(zone_class)
    return [f"TDFog3DZoneActor.{name}" for name in (ZONE_PROPERTY_NAMES["half_extent"], ZONE_PROPERTY_NAMES["material"]) if not has_editor_property(default_actor, name)]


def place_stage(context):
    actors, cube, created = context["actors"], context["cube"], context["created"]
    records = {}

    def finish(actor, label):
        if actor is None:
            raise RuntimeError(f"액터 생성 실패: {label}")
        actor.set_actor_label(actor_label(label))
        actor.set_folder_path(ACTOR_FOLDER)
        created.append(actor)
        return actor

    def spawn(actor_class, location, label, rotation=None):
        return finish(actors.spawn_actor_from_class(actor_class, unreal.Vector(*location), rotation or unreal.Rotator()), label)

    def place_cube(center, size, material, label):
        actor = finish(actors.spawn_actor_from_object(cube, unreal.Vector(*center), unreal.Rotator()), label)
        actor.set_actor_scale3d(unreal.Vector(size[0] / CUBE_SIZE_CM, size[1] / CUBE_SIZE_CM, size[2] / CUBE_SIZE_CM))
        component = actor.static_mesh_component
        component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        component.set_material(0, material)
        return actor

    floor = place_cube(FLOOR_CENTER_CM, FLOOR_SIZE_CM, context["floor_material"], "Fog3D_Floor")
    records["floor"] = {"label": floor.get_actor_label(), "bounds": require_bounds(floor, "Fog3D_Floor", FLOOR_CENTER_CM, FLOOR_SIZE_CM), "material": context["floor_material_path"]}

    obstacle_records = []
    for spec in OBSTACLE_SPECS:
        center = (spec["center"][0], spec["center"][1], FLOOR_TOP_Z_CM + 0.5 * spec["size"][2])
        actor = place_cube(center, spec["size"], context["wall_material"], spec["label"])
        add_obstacle_component(actor, context["obstacle_class"])
        obstacle_records.append({"label": actor.get_actor_label(), "center": list(center), "size_cm": list(spec["size"]), "bounds": require_bounds(actor, spec["label"], center, spec["size"]), "material": context["wall_material_path"]})
    records["obstacles"] = obstacle_records

    sun = spawn(unreal.DirectionalLight, SUN_LOCATION_CM, "Fog3D_Sun", unreal.Rotator(pitch=SUN_ROTATION_DEGREES["pitch"], yaw=SUN_ROTATION_DEGREES["yaw"]))
    sun.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_intensity(SUN_INTENSITY)
    sky = spawn(unreal.SkyLight, (0.0, 0.0, 1500.0), "Fog3D_SkyLight")
    sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", True)
    sky.light_component.set_intensity(SKY_LIGHT_INTENSITY)
    spawn(unreal.SkyAtmosphere, (0.0, 0.0, 0.0), "Fog3D_SkyAtmosphere")
    height_fog = spawn(unreal.ExponentialHeightFog, (0.0, 0.0, FLOOR_TOP_Z_CM), "Fog3D_HeightFog")
    fog_component = height_fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    applied_fog = {}
    for name, value in HEIGHT_FOG_PROPERTIES:
        fog_component.set_editor_property(name, value)
        applied_fog[name] = value
    records["height_fog"] = applied_fog

    post_process = spawn(unreal.PostProcessVolume, (0.0, 0.0, 0.0), "Fog3D_Exposure")
    post_process.set_editor_property("unbound", True)
    settings = post_process.settings
    for name, value in (("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL), ("auto_exposure_apply_physical_camera_exposure", False), ("auto_exposure_bias", 0.0)):
        settings.set_editor_property("override_" + name, True)
        settings.set_editor_property(name, value)
    post_process.set_editor_property("settings", settings)

    spawn(unreal.PlayerStart, PLAYER_START_CM, "Fog3D_PlayerStart")

    zone = spawn(context["zone_class"], ZONE_CENTER_CM, ZONE_LABEL)
    volume_component = find_zone_volume_component(zone)
    if volume_component is None:
        raise RuntimeError("구역 액터의 ZoneVolume 컴포넌트를 찾지 못했습니다(속성 이름 zone_volume 또는 정적 메시 컴포넌트 하나 필요).")
    volume_component.set_editor_property("static_mesh", cube)
    if volume_component.get_editor_property("static_mesh") != cube:
        raise RuntimeError("ZoneVolume 메시 지정 확인 실패")
    zone.set_editor_property(ZONE_PROPERTY_NAMES["material"], context["volume_material"])
    zone.set_editor_property(ZONE_PROPERTY_NAMES["half_extent"], unreal.Vector(*ZONE_HALF_EXTENT_CM))
    if hasattr(zone, "rerun_construction_scripts"):
        zone.rerun_construction_scripts()
    read_extent = zone.get_editor_property(ZONE_PROPERTY_NAMES["half_extent"])
    if max(abs(read_extent.x - ZONE_HALF_EXTENT_CM[0]), abs(read_extent.y - ZONE_HALF_EXTENT_CM[1]), abs(read_extent.z - ZONE_HALF_EXTENT_CM[2])) > 1e-3:
        raise RuntimeError(f"구역 반크기 다시 읽기 불일치: {read_extent}")
    read_material = zone.get_editor_property(ZONE_PROPERTY_NAMES["material"])
    if read_material is None or not read_material.get_path_name().startswith(VOLUME_MATERIAL_PATH):
        raise RuntimeError(f"구역 재질 다시 읽기 불일치: {read_material}")
    records["zone"] = {
        "label": zone.get_actor_label(), "center": list(ZONE_CENTER_CM), "half_extent_cm": list(ZONE_HALF_EXTENT_CM), "volume_material": VOLUME_MATERIAL_PATH,
        "bounds": require_bounds(zone, ZONE_LABEL, ZONE_CENTER_CM, tuple(2.0 * value for value in ZONE_HALF_EXTENT_CM)),
    }
    records["player_start"] = list(PLAYER_START_CM)
    return records


def main():
    assets = unreal.EditorAssetLibrary
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    missing_names = find_missing_unreal_names()
    if missing_names:
        raise RuntimeError("언리얼 Python 이름 없음: " + ", ".join(missing_names))
    edge_fade_cm, vertical_margin_cm = resolve_layout_thresholds(read_fog3d_settings())
    layout_problems = find_stage_layout_problems(build_default_layout(), edge_fade_cm, vertical_margin_cm)
    if layout_problems:
        raise RuntimeError("무대 배치 자체 점검 실패: " + " | ".join(layout_problems))
    if editor.get_game_world() is not None:
        raise RuntimeError("PIE를 종료한 뒤 실행하세요.")

    overwrite = read_overwrite_request()
    if assets.does_asset_exist(STAGE_MAP_PATH) and not overwrite:
        print(json.dumps({"status": "기존 맵이 있어 아무것도 바꾸지 않았습니다", "existing_assets": [STAGE_MAP_PATH], "overwrite_request_file": project_path(*REQUEST_RELATIVE_PATH), "overwrite_request_content": {"overwrite": True}}, ensure_ascii=False))
        raise RuntimeError("기존 맵 보존: overwrite 요청 파일이 없어 중단합니다.")
    unsaved_names = find_unsaved_package_names(overwrite)
    if unsaved_names:
        raise RuntimeError("미저장 작업을 보존합니다: " + ", ".join(unsaved_names))

    print("단계: 프리셋 정본 검사·다시 읽기")
    preset_tuning_sha256 = reload_preset_tuning()

    game_mode = assets.load_blueprint_class(GAME_MODE_PATH)
    cube = assets.load_asset(CUBE_MESH_PATH)
    volume_material = assets.load_asset(VOLUME_MATERIAL_PATH) if assets.does_asset_exist(VOLUME_MATERIAL_PATH) else None
    zone_class = unreal.load_class(None, ZONE_ACTOR_CLASS_PATH)
    obstacle_class = unreal.load_class(None, OBSTACLE_COMPONENT_CLASS_PATH)
    required = {"게임 모드": game_mode, "큐브": cube, "3D 안개 재질(editor_make_fog3d_material.py 먼저)": volume_material, "구역 액터 클래스(빌드·에디터 재시작 필요)": zone_class, "장애물 부품 클래스": obstacle_class}
    missing = [name for name, value in required.items() if value is None]
    if missing:
        raise RuntimeError("필수 에셋 또는 클래스 없음: " + ", ".join(missing))
    contract_problems = check_zone_actor_contract(zone_class)
    if contract_problems:
        raise RuntimeError("C++ 계약 속성 없음: " + ", ".join(contract_problems))
    floor_material, floor_material_path = pick_stage_material(assets, FLOOR_MATERIAL_PATH)
    wall_material, wall_material_path = pick_stage_material(assets, WALL_MATERIAL_PATH)

    print("단계: 레벨")
    ensure_directory(MAP_DIR)
    open_empty_stage_level(levels, actors)
    world = editor.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", game_mode)
    created = []
    context = {"actors": actors, "cube": cube, "created": created, "zone_class": zone_class, "obstacle_class": obstacle_class, "volume_material": volume_material,
               "floor_material": floor_material, "floor_material_path": floor_material_path, "wall_material": wall_material, "wall_material_path": wall_material_path}
    try:
        records = place_stage(context)
        unexpected = find_unexpected_level_actors(actors, created)
        if unexpected:
            raise RuntimeError("저장 전에 무대에 예상 밖 액터가 있습니다(임시 물체 금지): " + ", ".join(unexpected))
    except Exception:
        destroy_actors(actors, created)
        raise

    print("단계: 레벨 저장")
    if not levels.save_current_level():
        raise RuntimeError(f"무대 레벨 저장 실패: {STAGE_MAP_PATH}")
    saved_map_path = project_path("Content", "FluidFX", "Maps", "LV_TDFog3DStage.umap")
    if not os.path.isfile(saved_map_path):
        raise RuntimeError("저장 파일 확인 실패: " + saved_map_path)
    unsaved_after = find_unsaved_package_names(False)
    if unsaved_after:
        raise RuntimeError("저장 뒤에도 미저장 패키지가 있습니다: " + ", ".join(unsaved_after))

    report = {
        "status": "ok",
        "map": STAGE_MAP_PATH,
        "overwrite": overwrite,
        "preset_tuning_sha256": preset_tuning_sha256,
        "volume_material": VOLUME_MATERIAL_PATH,
        "zone_label": actor_label(ZONE_LABEL),
        "actor_folder": ACTOR_FOLDER,
        "records": records,
        "created_actors": len(created),
        "actors": [{"label": actor.get_actor_label(), "class": actor.get_class().get_name(), "bounds": describe_bounds(actor)} for actor in created],
        "lookdev_command": LOOKDEV_COMMAND,
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
    results = []

    def record(label, passed, detail=""):
        results.append(passed)
        print(("PASS " if passed else "FAIL ") + label + (": " + detail if detail else ""))

    settings = read_fog3d_settings()
    edge_fade_cm, vertical_margin_cm = resolve_layout_thresholds(settings)
    record("정본 JSON Globals.TDFog3DSettings 읽음(없으면 기본 흐림 500·높이 여유 200 으로 점검)", True, "있음" if settings is not None else "없음(1b-1 전)")
    base_layout = build_default_layout()
    problems = find_stage_layout_problems(base_layout, edge_fade_cm, vertical_margin_cm)
    record("① 무대 배치(상자 구역 안·흐림 폭 밖, 반크기 ≥ 흐림 폭, 시작점·걷기 끝 구역 안, 바닥 높이 차 0, 물체 간격 ≥ 4 m)", not problems, "; ".join(problems))

    near_edge = dict(base_layout, obstacles=base_layout["obstacles"][:2] + ({"label": "Fog3D_Wall", "center": (2700.0, 0.0), "size": (10.0, 300.0, 250.0)},))
    record("② 벽을 (2700, 0)으로 옮김 → 흐림 폭 안 문제", len(find_stage_layout_problems(near_edge, edge_fade_cm, vertical_margin_cm)) == 1)
    record("③ 시작점을 (-3500, 0, 100)으로 옮김 → 구역 밖 문제", any("구역" in line for line in find_stage_layout_problems(dict(base_layout, player_start=(-3500.0, 0.0, 100.0)), edge_fade_cm, vertical_margin_cm)))
    record("④ 바닥을 5 cm 낮춤 → 바닥 높이 차 문제", any("바닥 윗면" in line for line in find_stage_layout_problems(dict(base_layout, floor_center=(0.0, 0.0, -55.0)), edge_fade_cm, vertical_margin_cm)))
    record("⑤ 구역 반크기 (400, 400, 150) → 흐림 폭 문제", any("흐림 폭" in line for line in find_stage_layout_problems(dict(base_layout, zone_half_extent=(400.0, 400.0, 150.0)), edge_fade_cm, vertical_margin_cm)))
    crowded = dict(base_layout, obstacles=base_layout["obstacles"][:2] + ({"label": "Fog3D_Wall", "center": (500.0, 0.0), "size": (10.0, 300.0, 250.0)},))
    record("⑥ 벽을 (500, 0)으로 옮김 → 물체 간격·걷는 길 문제", len(find_stage_layout_problems(crowded, edge_fade_cm, vertical_margin_cm)) >= 2)
    record("⑦ 물체 셋·라벨 접두어", len(OBSTACLE_SPECS) == 3 and all(actor_label(spec["label"]).startswith("TDGen_Fog3D_") for spec in OBSTACLE_SPECS) and actor_label(ZONE_LABEL) == "TDGen_Fog3D_Zone")

    header_path = project_path(*ZONE_HEADER_RELATIVE_PATH)
    if os.path.isfile(header_path):
        with open(header_path, encoding="utf-8", errors="replace") as header_file:
            missing_header_names = find_missing_header_names(header_file.read())
        record("⑧ TDFog3DZoneActor.h 에 ZoneHalfExtentCm·VolumeMaterial·ZoneVolume 선언", not missing_header_names, ", ".join(missing_header_names))
    else:
        record("⑧ TDFog3DZoneActor.h 아직 없음 → 계약 3절 이름 사용", True, "헤더 대조 건너뜀")
    record("⑨ 헤더 대조 함수: 이름이 빠진 글은 문제로 잡음", find_missing_header_names("FVector ZoneHalfExtentCm; UStaticMeshComponent* ZoneVolume;") == ["VolumeMaterial"])

    tools_directory = os.path.dirname(os.path.abspath(__file__))
    for file_name in LOOKDEV_VARIANT_FILES:
        variant_path = os.path.join(tools_directory, file_name)
        if not os.path.isfile(variant_path):
            record(f"⑩ 촬영 변형 {file_name} 있음", False)
            continue
        with open(variant_path, encoding="utf-8") as variant_file:
            specification = json.load(variant_file)
        hide_labels = {label for variant in specification["variants"] for label in variant.get("apply", {}).get("hide", {})}
        record(f"⑩ 촬영 변형 {file_name} 의 hide 라벨 = 무대 구역 라벨", hide_labels == {actor_label(ZONE_LABEL)}, str(sorted(hide_labels)))

    report_sample = {"status": "ok", "records": {"zone": {"center": list(ZONE_CENTER_CM)}}, "lookdev_command": LOOKDEV_COMMAND}
    record("⑪ 보고서 JSON 직렬화", json.loads(json.dumps(report_sample, ensure_ascii=False))["status"] == "ok")
    record("⑫ 맵 경로·재질 경로 상수", STAGE_MAP_PATH == "/Game/FluidFX/Maps/LV_TDFog3DStage" and VOLUME_MATERIAL_PATH == "/Game/FluidFX/Materials/M_TD_Fog3DVolume")
    record("⑬ 해 방향 = 옆빛(높이 35도·방향 100)", SUN_ROTATION_DEGREES == {"pitch": -35.0, "yaw": 100.0}, str(SUN_ROTATION_DEGREES))
    return 0 if all(results) else 1


if __name__ == "__main__":
    if "--self-test" in sys.argv[1:]:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.exit(run_self_test())
    run_with_failure_report()
