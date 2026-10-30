"""실행 환경: TDGame 언리얼 에디터 안(Python 원격 실행). W-6 밀집 물 C++ 통합·재시작 뒤, 먼저 editor_make_fluid_water_material.py(E1: 물 마스터와 MI_TD_FluidWater_Dense{Creek,Lake,Sea})가 끝나 있어야 한다. 장면마다 맵 하나·물 액터 하나(refute-w6 M1, cross-plan X-18)이며 0 개울(6% 하강 수로·안장 분지·1 m 홈·바위 6 중 3 장애물·상류 구역 상자) 1 호수(분지·가장자리 수원) 2 비교(DenseLake 한 액터에 구역·장애물 상자, 희소 대조는 TD.FluidFX.DenseWater 1/0 A/B) 3 바다(5도 해변·먼 쪽 해수면 구역) 4 수조(3×3 m 벽 상자 안 구역) 5 배(깊이 8 m·400 m 해수면 구역·ShipWakeSea·100×15×6 m 상자 왕복 이동·선체 부품).
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_dense_water_stage.py (PowerShell, L-repo-01. 요청 Saved/FluidFX/fluid_dense_water_stage_request.json = {"overwrite": true, "scenes": [0, 1, 2, 3, 4, 5]})
출력: /Game/FluidFX/Presets/DA_TDFluid_{DenseCreek,DenseLake,DenseSea,DenseTank,ShipWakeSea}, /Game/FluidFX/Maps/LV_TDFluidDenseWaterStage_{Creek,Lake,Compare,Sea,Tank,Ship}, Saved/FluidFX/fluid_dense_water_stage.json(preset_tuning_sha256·material_look_sha256, 맵·라벨·탐침·경계·구역·선체, 캐시 로드 기대 5 = 맵 합). 기존 에셋·맵은 overwrite 참일 때만 갱신하고 미저장 작업이 있으면 중단한다. 결과는 run_in_editor.py 출력 본문으로 판정한다(L-editor-73)
상태: 미검증(2026-10-29 파동 3 L3-S2 작성: py_compile·가짜 unreal 실행만, 에디터 실행 전). 밀집 물 액터는 모두 폰을 따라가지 않고(M4) 구역 상자가 있으며, 배 선체 부품은 SubobjectDataSubsystem 으로 붙인다(S9). 물 마스터 E1·조각 병합이 모든 무대보다 먼저다. 값은 전부 정본 JSON 에서 오고 이 스크립트는 지형 크기만 정한다.
"""
import importlib.util
import json
import math
import os

import unreal

FLUID_ROOT = "/Game/FluidFX"
MAP_DIR = f"{FLUID_ROOT}/Maps"
MATERIAL_DIR = f"{FLUID_ROOT}/Materials"
SURFACE_MESH_PATH = f"{FLUID_ROOT}/Meshes/SM_TD_FluidWaterGridUnit"
CUBE_MESH_PATH = "/Engine/BasicShapes/Cube"
SPHERE_MESH_PATH = "/Engine/BasicShapes/Sphere"
GAME_MODE_PATH = "/Game/Combat/Blueprints/BP_TDCombatGameMode"
WATER_ACTOR_CLASS_PATH = "/Script/TDGame.TDFluidWaterActor"
HULL_WAKE_CLASS_PATH = "/Script/TDGame.TDFluidHullWakeComponent"
MAP_PREFIX = f"{MAP_DIR}/LV_TDFluidDenseWaterStage_"
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_dense_water_stage_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_dense_water_stage.json")
PARTICLE_STAGE_RELATIVE_PATH = ("Tools", "FluidFX", "editor_make_fluid_particle_stage.py")
STAGE_FOLDER = "TDGen/FluidFX/DenseWaterStage"
EXPECTED_CACHE_LOADS = 5

CUBE_SIZE_CM = 100.0
SPHERE_DIAMETER_CM = 100.0
SLAB_THICKNESS_CM = 100.0
REGION_THICKNESS_CM = 20.0
DENSE_BOUNDS_SCALE = 4.0
TRACE_TOP_CM = 5000.0
TRACE_BOTTOM_CM = -5000.0
TRACE_TOLERANCE_CM = 5.0

CREEK_SLOPE = 0.06
CREEK_HALF_LENGTH_CM = 3300.0
CREEK_HALF_WIDTH_CM = 600.0
CREEK_BANK_RISE_CM = 150.0
CREEK_DAM_END_X_CM = -3000.0
CREEK_GROOVE_CENTER_X_CM = 1200.0
CREEK_GROOVE_WIDTH_CM = 100.0
CREEK_GROOVE_DEPTH_CM = 40.0
CREEK_SADDLE_X_CM = (-200.0, 600.0)
CREEK_SADDLE_CREST_CM = 60.0
CREEK_SADDLE_Y_CM = (600.0, 800.0)
CREEK_BASIN_Y_CM = (800.0, 2000.0)
CREEK_BASIN_DEPTH_CM = 60.0
CREEK_RESERVOIR_X_CM = (-3000.0, -2400.0)
CREEK_RESERVOIR_ABOVE_BED_CM = 40.0
CREEK_ROCK_CENTER_RATIO = 0.4
CREEK_ROCKS = (
    {"x": -1800.0, "y": -200.0, "radius": 70.0, "is_obstacle": True},
    {"x": -1300.0, "y": 250.0, "radius": 55.0, "is_obstacle": False},
    {"x": -800.0, "y": -100.0, "radius": 80.0, "is_obstacle": True},
    {"x": -300.0, "y": 300.0, "radius": 60.0, "is_obstacle": False},
    {"x": 200.0, "y": -250.0, "radius": 70.0, "is_obstacle": True},
    {"x": 700.0, "y": 100.0, "radius": 55.0, "is_obstacle": False},
)
CREEK_PROBE_X_CM = (-2400.0, -1000.0, 0.0, 1900.0, 2800.0)

LAKE_FLOOR_Z_CM = -200.0
LAKE_RIM_Z_CM = 0.0
LAKE_FLOOR_HALF_CM = 1500.0
LAKE_RAMP_RUN_CM = 500.0
LAKE_PLATEAU_RUN_CM = 600.0
LAKE_SOURCE_X_CM = (-1500.0, -1300.0)
LAKE_SOURCE_Y_CM = (-300.0, 300.0)
LAKE_SOURCE_SURFACE_Z_CM = -120.0

COMPARE_SLOPE = 0.10
COMPARE_HALF_LENGTH_CM = 2600.0
COMPARE_RAMP_END_X_CM = 800.0
COMPARE_POOL_END_X_CM = 2200.0
COMPARE_HALF_WIDTH_CM = 1000.0
COMPARE_OUTER_HALF_WIDTH_CM = 2600.0
COMPARE_WALL_RISE_CM = 120.0
COMPARE_RESERVOIR_X_CM = (-2600.0, -2000.0)
COMPARE_RESERVOIR_ABOVE_BED_CM = 40.0
COMPARE_BOX_CENTER_X_CM = -300.0
COMPARE_BOX_SIZE_CM = (200.0, 200.0, 100.0)
COMPARE_BOX_LIFT_CM = 30.0

SEA_FLOOR_Z_CM = -250.0
SEA_BEACH_DEGREES = 5.0
SEA_BEACH_START_X_CM = -400.0
SEA_HALF_CM = 4200.0
SEA_LEVEL_Z_CM = 0.0
SEA_REGION_X_CM = (-4200.0, -3200.0)

TANK_FLOOR_Z_CM = -100.0
TANK_FLOOR_SIZE_CM = 700.0
TANK_INNER_HALF_CM = 150.0
TANK_WALL_THICKNESS_CM = 20.0
TANK_WALL_HEIGHT_CM = 200.0
TANK_REGION_XY_CM = (-145.0, -85.0)
TANK_REGION_SURFACE_Z_CM = -40.0

SHIP_SEABED_Z_CM = -800.0
SHIP_SEABED_SIZE_CM = 42000.0
SHIP_REGION_SIZE_CM = 40000.0
SHIP_LENGTH_CM = 10000.0
SHIP_BEAM_CM = 1500.0
SHIP_HEIGHT_CM = 600.0
SHIP_CENTER_Z_CM = 100.0
SHIP_TRAVEL_CM = 30000.0
SHIP_TRAVEL_SECONDS = 40.0

SCENE_SPECS = (
    {"index": 0, "name": "Creek", "layout": "creek", "label": "DenseCreek", "preset": "DenseCreek", "kind": "DENSE_CREEK",
     "material": "MI_TD_FluidWater_DenseCreek", "is_dense": True, "cache_label": "Creek", "coverage_half_cm": 3300.0,
     "camera": {"pawn": (0.0, 0.0), "arm": 2500.0, "view": (-70.0, 0.0)}},
    {"index": 1, "name": "Lake", "layout": "lake", "label": "DenseLake", "preset": "DenseLake", "kind": "DENSE_LAKE",
     "material": "MI_TD_FluidWater_DenseLake", "is_dense": True, "cache_label": "Lake", "coverage_half_cm": 2600.0,
     "camera": {"pawn": (0.0, -1500.0), "arm": 4500.0, "view": (-45.0, 90.0)}},
    {"index": 2, "name": "Compare", "layout": "compare", "label": "DenseCompare", "preset": "DenseLake", "kind": "DENSE_LAKE",
     "material": "MI_TD_FluidWater_DenseLake", "is_dense": True, "cache_label": "Compare", "coverage_half_cm": 2600.0,
     "camera": {"pawn": (0.0, 0.0), "arm": 4000.0, "view": (-55.0, 0.0)}},
    {"index": 3, "name": "Sea", "layout": "sea", "label": "DenseSea", "preset": "DenseSea", "kind": "DENSE_SEA",
     "material": "MI_TD_FluidWater_DenseSea", "is_dense": True, "cache_label": "Sea", "coverage_half_cm": 4200.0,
     "camera": {"pawn": (0.0, 0.0), "arm": 6000.0, "view": (-35.0, 0.0)}},
    {"index": 4, "name": "Tank", "layout": "tank", "label": "DenseTank", "preset": "DenseTank", "kind": "DENSE_TANK",
     "material": "MI_TD_FluidWater_DenseLake", "is_dense": True, "cache_label": "Tank", "coverage_half_cm": 350.0,
     "camera": {"pawn": (0.0, 0.0), "arm": 700.0, "view": (-50.0, 0.0)}},
    {"index": 5, "name": "Ship", "layout": "ship", "label": "DenseShip", "preset": "ShipWakeSea", "kind": "SHIP_WAKE_SEA",
     "material": "MI_TD_FluidWater_DenseSea", "is_dense": False, "cache_label": None, "coverage_half_cm": 20000.0,
     "camera": {"pawn": (0.0, 0.0), "arm": 25000.0, "view": (-80.0, 0.0)}},
)
ALL_SCENE_INDICES = tuple(spec["index"] for spec in SCENE_SPECS)

REQUIRED_UNREAL_NAMES = (
    "get_engine_subsystem",
    "TDFluidPresetTuningSubsystem",
    "TDFluidPresetAsset",
    "TDFluidPresetKind.DENSE_CREEK",
    "TDFluidPresetKind.DENSE_LAKE",
    "TDFluidPresetKind.DENSE_SEA",
    "TDFluidPresetKind.DENSE_TANK",
    "TDFluidPresetKind.SHIP_WAKE_SEA",
    "TDFluidDenseWaterCacheAsset",
    "TDFluidHullWakeComponent",
    "TDFluidBrushChannel.CHANNEL_A_WATER",
    "TDFluidPlaneSettings",
    "TDFluidPlaneOrientation.WORLD_FACING",
    "InterpToMovementComponent",
    "InterpToBehaviourType.PING_PONG",
    "DataAssetFactory",
    "SubobjectDataSubsystem",
    "SubobjectDataBlueprintFunctionLibrary.get_object",
    "AddNewSubobjectParams",
    "StaticMeshActor",
    "DirectionalLight",
    "SkyLight",
    "PlayerStart",
    "ComponentMobility.MOVABLE",
    "ComponentMobility.STATIC",
    "CollisionEnabled.NO_COLLISION",
    "SystemLibrary.line_trace_single_for_objects",
    "ObjectTypeQuery.OBJECT_TYPE_QUERY1",
    "DrawDebugTrace.NONE",
    "EditorAssetLibrary.load_blueprint_class",
    "EditorLoadingAndSavingUtils.get_dirty_map_packages",
    "EditorLoadingAndSavingUtils.get_dirty_content_packages",
    "SystemLibrary.get_project_directory",
)
WATER_ACTOR_PROPERTIES = (
    "live_component",
    "should_use_activation_volume",
    "should_follow_player_pawn",
    "sample_terrain_elevation",
    "interaction_brush_channel",
    "internal_surface_mesh",
    "dense_water_cache",
    "water_region_actor_tag",
    "water_obstacle_actor_tag",
    "terrain_trace_ignore_tag",
)
WATER_TAG_PROPERTIES = {"region": "water_region_actor_tag", "obstacle": "water_obstacle_actor_tag", "ignore": "terrain_trace_ignore_tag"}
HULL_WAKE_PROPERTIES = ("target_water_actor", "hull_length_cm", "hull_beam_cm", "drives_water_window")
PRESET_PROPERTIES = ("preset_kind", "override_preset_defaults", "default_channel", "plane_settings", "output_material", "surface_mesh", "translucency_sort_priority", "simulation_config")
EFFECTIVE_REPORT_NAMES = (
    "enable_water_layer",
    "enable_dense_water",
    "grid_resolution",
    "sim_extent_cm",
    "open_water_borders",
    "water_depth_reference_cm",
    "solver_backend",
    "cpu_fallback_resolution",
)
WATER_REPORT_PROPERTIES = ("should_follow_player_pawn", "should_use_activation_volume", "sample_terrain_elevation", "water_region_actor_tag", "water_obstacle_actor_tag", "terrain_trace_ignore_tag")


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


def load_module(module_name, *relative_parts):
    module_path = project_path(*relative_parts)
    module_spec = importlib.util.spec_from_file_location(module_name, module_path)
    if module_spec is None or module_spec.loader is None:
        raise RuntimeError(f"모듈 없음: {module_path}")
    module = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(module)
    return module


def read_request():
    request_path = project_path(*REQUEST_RELATIVE_PATH)
    if not os.path.isfile(request_path):
        return {}
    with open(request_path, encoding="utf-8") as request_file:
        request = json.load(request_file)
    return request if isinstance(request, dict) else {}


def select_scenes(request):
    requested = request.get("scenes", list(ALL_SCENE_INDICES))
    if not isinstance(requested, list) or not requested or any(item not in ALL_SCENE_INDICES for item in requested):
        raise RuntimeError(f"요청 파일 scenes 는 {list(ALL_SCENE_INDICES)} 의 부분집합이어야 합니다: {requested}")
    return tuple(spec for spec in SCENE_SPECS if spec["index"] in requested)


def map_path_for(scene):
    return MAP_PREFIX + scene["name"]


def preset_path_for(stage, scene):
    return f"{stage.PRESET_DIR}/DA_TDFluid_{scene['preset']}"


def material_path_for(scene):
    return f"{MATERIAL_DIR}/{scene['material']}"


def load_asset_or_raise(path, hint):
    asset = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if asset is None:
        raise RuntimeError(f"필수 에셋 없음({hint}): {path}")
    return asset


def add_actor_tag(actor, tag_name):
    tags = [str(tag) for tag in actor.get_editor_property("tags")]
    if tag_name not in tags:
        tags.append(tag_name)
    actor.set_editor_property("tags", [unreal.Name(tag) for tag in tags])
    return tags


def read_water_tag_names(water_class):
    defaults = unreal.get_default_object(water_class)
    return {role: str(defaults.get_editor_property(property_name)) for role, property_name in WATER_TAG_PROPERTIES.items()}


def describe_properties(owner, property_names):
    description = {}
    for name in property_names:
        try:
            description[name] = str(owner.get_editor_property(name))
        except Exception:
            continue
    return description


def load_or_create_preset(stage, scene, path):
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(path):
        preset = assets.load_asset(path)
        if not isinstance(preset, unreal.TDFluidPresetAsset):
            raise RuntimeError(f"기존 프리셋 에셋 형식 불일치: {path}")
        return preset
    stage.ensure_directory(stage.PRESET_DIR)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.TDFluidPresetAsset)
    preset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(f"DA_TDFluid_{scene['preset']}", stage.PRESET_DIR, unreal.TDFluidPresetAsset, factory)
    if preset is None:
        raise RuntimeError(f"프리셋 생성 실패: {path}")
    return preset


def configure_preset(stage, scene, resources):
    kind = getattr(unreal.TDFluidPresetKind, scene["kind"])
    path = preset_path_for(stage, scene)
    preset = load_or_create_preset(stage, scene, path)
    preset.set_editor_property("preset_kind", kind)
    preset.set_editor_property("override_preset_defaults", False)
    preset.set_editor_property("default_channel", unreal.TDFluidBrushChannel.CHANNEL_A_WATER)
    plane = unreal.TDFluidPlaneSettings()
    plane.set_editor_property("orientation", unreal.TDFluidPlaneOrientation.WORLD_FACING)
    plane.set_editor_property("camera_depth_bias_cm", 0.0)
    plane.set_editor_property("center_offset_along_up_ratio", 0.0)
    preset.set_editor_property("plane_settings", plane)
    preset.set_editor_property("output_material", resources["materials"][scene["material"]])
    preset.set_editor_property("surface_mesh", resources["surface_mesh"])
    preset.set_editor_property("translucency_sort_priority", 0)
    effective = preset.resolve_effective_config()
    problems = []
    if not effective.get_editor_property("enable_water_layer"):
        problems.append("enable_water_layer")
    if bool(effective.get_editor_property("enable_dense_water")) != scene["is_dense"]:
        problems.append("enable_dense_water")
    if problems:
        raise RuntimeError(f"프리셋 유효 구성이 종류 표와 다릅니다(TDFluidPresetTuning.json 확인·재빌드·재시작): {path} {problems}")
    stage.save_asset(preset, path)
    return preset, path, effective


def spawn_water_actor(context, scene):
    preset, preset_path, effective = context["presets"][scene["preset"]]
    water = context["actors"].spawn_actor_from_class(context["water_class"], unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator())
    context["stage"].finish_actor(water, scene["label"], context["folder"], context["created"])
    live_component = water.get_editor_property("live_component")
    live_component.set_editor_property("active_preset", preset)
    live_component.set_editor_property("should_init_system_state_from_preset", True)
    live_component.set_editor_property("simulation_config", effective)
    water.set_editor_property("should_use_activation_volume", False)
    water.set_editor_property("should_follow_player_pawn", False)
    water.set_editor_property("sample_terrain_elevation", True)
    water.set_editor_property("interaction_brush_channel", unreal.TDFluidBrushChannel.CHANNEL_A_WATER)
    surface = water.get_editor_property("internal_surface_mesh")
    surface.set_cast_shadow(False)
    if scene["is_dense"]:
        surface.set_editor_property("bounds_scale", DENSE_BOUNDS_SCALE)
    if water.get_editor_property("should_follow_player_pawn"):
        raise RuntimeError(f"물 액터가 폰을 따라가도록 남았습니다(M4): {scene['label']}")
    return water


def place_box(context, label, center, size, rotation, tags=(), collides=True, hidden_in_game=False, mobility=None):
    actor = context["actors"].spawn_actor_from_object(context["cube"], unreal.Vector(*center), rotation)
    context["stage"].finish_actor(actor, label, context["folder"], context["created"])
    actor.set_actor_scale3d(unreal.Vector(size[0] / CUBE_SIZE_CM, size[1] / CUBE_SIZE_CM, size[2] / CUBE_SIZE_CM))
    component = actor.static_mesh_component
    component.set_editor_property("mobility", mobility if mobility is not None else unreal.ComponentMobility.STATIC)
    if not collides:
        component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        component.set_collision_profile_name("NoCollision")
        component.set_cast_shadow(False)
    if hidden_in_game:
        actor.set_actor_hidden_in_game(True)
    for tag in tags:
        add_actor_tag(actor, tag)
    context["placed_geometry_count"] += 1
    return actor


def slab_center(top_center, yaw_degrees, pitch_degrees, thickness):
    pitch, yaw = math.radians(pitch_degrees), math.radians(yaw_degrees)
    normal = (-math.sin(pitch) * math.cos(yaw), -math.sin(pitch) * math.sin(yaw), math.cos(pitch))
    return tuple(top_center[axis] - normal[axis] * thickness * 0.5 for axis in range(3))


def place_slab(context, label, top_center, horizontal_length, width, yaw_degrees=0.0, pitch_degrees=0.0, thickness=SLAB_THICKNESS_CM):
    center = slab_center(top_center, yaw_degrees, pitch_degrees, thickness)
    length_along_slope = horizontal_length / abs(math.cos(math.radians(pitch_degrees)))
    return place_box(context, label, center, (length_along_slope, width, thickness), unreal.Rotator(pitch=pitch_degrees, yaw=yaw_degrees))


def place_region(context, name, x_range, y_range, surface_z):
    center = ((x_range[0] + x_range[1]) * 0.5, (y_range[0] + y_range[1]) * 0.5, surface_z - REGION_THICKNESS_CM * 0.5)
    size = (x_range[1] - x_range[0], y_range[1] - y_range[0], REGION_THICKNESS_CM)
    tags = (context["tags"]["region"], context["tags"]["ignore"])
    place_box(context, name, center, size, unreal.Rotator(), tags=tags, collides=False, hidden_in_game=True)
    return {"name": name, "min_xy_cm": [x_range[0], y_range[0]], "max_xy_cm": [x_range[1], y_range[1]], "surface_z_cm": surface_z}


def place_rock(context, name, x, y, radius, center_z, is_obstacle):
    actor = context["actors"].spawn_actor_from_object(context["sphere"], unreal.Vector(x, y, center_z), unreal.Rotator())
    context["stage"].finish_actor(actor, name, context["folder"], context["created"])
    scale = 2.0 * radius / SPHERE_DIAMETER_CM
    actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
    actor.static_mesh_component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
    if is_obstacle:
        add_actor_tag(actor, context["tags"]["obstacle"])
    context["placed_geometry_count"] += 1
    return {"name": name, "x": x, "y": y, "radius_cm": radius, "center_z_cm": center_z, "is_obstacle": is_obstacle}


def trace_terrain_z(context, x, y):
    start, end = unreal.Vector(x, y, TRACE_TOP_CM), unreal.Vector(x, y, TRACE_BOTTOM_CM)
    hit = unreal.SystemLibrary.line_trace_single_for_objects(context["world"], start, end, [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1], False, [], unreal.DrawDebugTrace.NONE, True)
    values = hit.to_tuple() if hit is not None else None
    if not values or not values[0]:
        return None
    return float(values[5].z)


def probe_terrain(context, name, x, y, expected_z):
    traced = trace_terrain_z(context, x, y)
    if traced is None:
        raise RuntimeError(f"지형 트레이스 실패({name}: {x:.0f}, {y:.0f}): 런타임 지형 표본도 실패한다")
    if abs(traced - expected_z) > TRACE_TOLERANCE_CM:
        raise RuntimeError(f"지형 높이 불일치({name}: {x:.0f}, {y:.0f}): 트레이스 {traced:.1f}, 해석 {expected_z:.1f}")
    probe = {"name": name, "x": x, "y": y, "traced_z_cm": traced, "analytic_z_cm": expected_z}
    context["probes"].append(probe)
    return probe


def rotate_planar(yaw_degrees, along, across):
    yaw = math.radians(yaw_degrees)
    return (along * math.cos(yaw) - across * math.sin(yaw), along * math.sin(yaw) + across * math.cos(yaw))


def place_lighting_and_start(context, start_location):
    actors, stage, created, folder = context["actors"], context["stage"], context["created"], context["folder"]
    sun_location = unreal.Vector(0.0, 0.0, 1500.0)
    sun = stage.finish_actor(actors.spawn_actor_from_class(unreal.DirectionalLight, sun_location, unreal.Rotator(pitch=-50.0, yaw=-35.0)), "StageSun", folder + "/Lighting", created)
    sun.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_cast_shadows(True)
    sky = stage.finish_actor(actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0.0, 0.0, 1000.0), unreal.Rotator()), "StageSkyLight", folder + "/Lighting", created)
    sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", True)
    stage.finish_actor(actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(*start_location), unreal.Rotator()), "StagePlayerStart", folder + "/Gameplay", created)


def add_actor_component(actor, component_class):
    subobjects = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    handles = list(subobjects.k2_gather_subobject_data_for_instance(actor))
    if not handles:
        raise RuntimeError("액터 서브오브젝트 목록이 비었습니다: " + actor.get_actor_label())
    params = unreal.AddNewSubobjectParams()
    params.set_editor_property("parent_handle", handles[0])
    params.set_editor_property("new_class", component_class)
    new_handle, failure_reason = subobjects.add_new_subobject(params)
    if str(failure_reason):
        raise RuntimeError(f"부품 추가 실패: {actor.get_actor_label()} {failure_reason}")
    component = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(subobjects.k2_find_subobject_data_from_handle(new_handle))
    if component is None or actor.get_component_by_class(component_class) is None:
        raise RuntimeError("부품 확인 실패: " + actor.get_actor_label())
    return component


def creek_bed_z_cm(x):
    return -CREEK_SLOPE * x


def place_creek_slab(context, label, x_range, y_range, top_offset):
    center_x = (x_range[0] + x_range[1]) * 0.5
    center_y = (y_range[0] + y_range[1]) * 0.5
    pitch = -math.degrees(math.atan(CREEK_SLOPE))
    return place_slab(context, label, (center_x, center_y, creek_bed_z_cm(center_x) + top_offset), x_range[1] - x_range[0], y_range[1] - y_range[0], pitch_degrees=pitch)


def build_creek_terrain(context):
    half_length, half_width = CREEK_HALF_LENGTH_CM, CREEK_HALF_WIDTH_CM
    groove_min = CREEK_GROOVE_CENTER_X_CM - CREEK_GROOVE_WIDTH_CM * 0.5
    groove_max = CREEK_GROOVE_CENTER_X_CM + CREEK_GROOVE_WIDTH_CM * 0.5
    channel_y = (-half_width, half_width)
    place_creek_slab(context, "CreekBedUpper", (-half_length, groove_min), channel_y, 0.0)
    place_creek_slab(context, "CreekGroove", (groove_min, groove_max), channel_y, -CREEK_GROOVE_DEPTH_CM)
    place_creek_slab(context, "CreekBedLower", (groove_max, half_length), channel_y, 0.0)
    place_creek_slab(context, "CreekDam", (-half_length, CREEK_DAM_END_X_CM), channel_y, CREEK_BANK_RISE_CM)
    place_creek_slab(context, "CreekBankSouth", (-half_length, half_length), (-half_length, -half_width), CREEK_BANK_RISE_CM)
    saddle_min, saddle_max = CREEK_SADDLE_X_CM
    place_creek_slab(context, "CreekBankNorthUpstream", (-half_length, saddle_min), (half_width, half_length), CREEK_BANK_RISE_CM)
    place_creek_slab(context, "CreekBankNorthDownstream", (saddle_max, half_length), (half_width, half_length), CREEK_BANK_RISE_CM)
    place_creek_slab(context, "CreekSaddle", CREEK_SADDLE_X_CM, CREEK_SADDLE_Y_CM, CREEK_SADDLE_CREST_CM)
    place_creek_slab(context, "CreekSideBasin", CREEK_SADDLE_X_CM, CREEK_BASIN_Y_CM, -CREEK_BASIN_DEPTH_CM)
    place_creek_slab(context, "CreekBankNorthBehindBasin", CREEK_SADDLE_X_CM, (CREEK_BASIN_Y_CM[1], half_length), CREEK_BANK_RISE_CM)
    obstacle_count = 0
    rocks = []
    for index, rock in enumerate(CREEK_ROCKS):
        center_z = creek_bed_z_cm(rock["x"]) + rock["radius"] * CREEK_ROCK_CENTER_RATIO
        rocks.append(place_rock(context, f"CreekRock{index}", rock["x"], rock["y"], rock["radius"], center_z, rock["is_obstacle"]))
        obstacle_count += 1 if rock["is_obstacle"] else 0
    return {"groove": {"center_x_cm": CREEK_GROOVE_CENTER_X_CM, "width_cm": CREEK_GROOVE_WIDTH_CM, "depth_cm": CREEK_GROOVE_DEPTH_CM},
            "saddle": {"x_cm": list(CREEK_SADDLE_X_CM), "y_cm": list(CREEK_SADDLE_Y_CM), "crest_above_bed_cm": CREEK_SADDLE_CREST_CM,
                       "basin_y_cm": list(CREEK_BASIN_Y_CM), "basin_below_bed_cm": CREEK_BASIN_DEPTH_CM},
            "rocks": rocks, "obstacle_rock_count": obstacle_count}


def probe_creek(context):
    for x in CREEK_PROBE_X_CM:
        probe_terrain(context, "bed", x, 0.0, creek_bed_z_cm(x))
    groove_x = CREEK_GROOVE_CENTER_X_CM
    probe_terrain(context, "groove", groove_x, 0.0, creek_bed_z_cm(groove_x) - CREEK_GROOVE_DEPTH_CM)
    saddle_x = sum(CREEK_SADDLE_X_CM) * 0.5
    probe_terrain(context, "saddle_crest", saddle_x, sum(CREEK_SADDLE_Y_CM) * 0.5, creek_bed_z_cm(saddle_x) + CREEK_SADDLE_CREST_CM)
    probe_terrain(context, "side_basin", saddle_x, sum(CREEK_BASIN_Y_CM) * 0.5, creek_bed_z_cm(saddle_x) - CREEK_BASIN_DEPTH_CM)
    probe_terrain(context, "bank", 1000.0, 2500.0, creek_bed_z_cm(1000.0) + CREEK_BANK_RISE_CM)


def build_creek(context, scene):
    terrain = build_creek_terrain(context)
    reservoir_center_x = sum(CREEK_RESERVOIR_X_CM) * 0.5
    reservoir_surface = creek_bed_z_cm(reservoir_center_x) + CREEK_RESERVOIR_ABOVE_BED_CM
    regions = [place_region(context, "CreekUpstreamReservoir", CREEK_RESERVOIR_X_CM, (-CREEK_HALF_WIDTH_CM, CREEK_HALF_WIDTH_CM), reservoir_surface)]
    water = spawn_water_actor(context, scene)
    probe_creek(context)
    terrain.update({"slope": CREEK_SLOPE, "half_length_cm": CREEK_HALF_LENGTH_CM, "half_width_cm": CREEK_HALF_WIDTH_CM,
                    "reservoir_above_bed_cm": CREEK_RESERVOIR_ABOVE_BED_CM})
    start = (0.0, -1500.0, creek_bed_z_cm(0.0) + CREEK_BANK_RISE_CM + 100.0)
    return water, terrain, regions, start


def lake_surface_z_cm(x, y):
    fraction = (max(abs(x), abs(y)) - LAKE_FLOOR_HALF_CM) / LAKE_RAMP_RUN_CM
    return LAKE_FLOOR_Z_CM + (LAKE_RIM_Z_CM - LAKE_FLOOR_Z_CM) * min(max(fraction, 0.0), 1.0)


def build_lake_terrain(context):
    floor_size = 2.0 * LAKE_FLOOR_HALF_CM
    outer_width = 2.0 * (LAKE_FLOOR_HALF_CM + LAKE_RAMP_RUN_CM + LAKE_PLATEAU_RUN_CM)
    place_slab(context, "LakeFloor", (0.0, 0.0, LAKE_FLOOR_Z_CM), floor_size, floor_size)
    ramp_pitch = math.degrees(math.atan2(LAKE_RIM_Z_CM - LAKE_FLOOR_Z_CM, LAKE_RAMP_RUN_CM))
    ramp_along = LAKE_FLOOR_HALF_CM + LAKE_RAMP_RUN_CM * 0.5
    plateau_along = LAKE_FLOOR_HALF_CM + LAKE_RAMP_RUN_CM + LAKE_PLATEAU_RUN_CM * 0.5
    ramp_middle_z = (LAKE_FLOOR_Z_CM + LAKE_RIM_Z_CM) * 0.5
    for yaw in (0.0, 90.0, 180.0, 270.0):
        ramp_x, ramp_y = rotate_planar(yaw, ramp_along, 0.0)
        place_slab(context, f"LakeRamp{int(yaw)}", (ramp_x, ramp_y, ramp_middle_z), LAKE_RAMP_RUN_CM, outer_width, yaw_degrees=yaw, pitch_degrees=ramp_pitch)
        plateau_x, plateau_y = rotate_planar(yaw, plateau_along, 0.0)
        place_slab(context, f"LakePlateau{int(yaw)}", (plateau_x, plateau_y, LAKE_RIM_Z_CM), LAKE_PLATEAU_RUN_CM, outer_width, yaw_degrees=yaw)
    return {"floor_z_cm": LAKE_FLOOR_Z_CM, "rim_z_cm": LAKE_RIM_Z_CM, "floor_half_cm": LAKE_FLOOR_HALF_CM, "ramp_run_cm": LAKE_RAMP_RUN_CM,
            "basin_width_cm": 2.0 * (LAKE_FLOOR_HALF_CM + LAKE_RAMP_RUN_CM)}


def build_lake(context, scene):
    terrain = build_lake_terrain(context)
    regions = [place_region(context, "LakeEdgeSource", LAKE_SOURCE_X_CM, LAKE_SOURCE_Y_CM, LAKE_SOURCE_SURFACE_Z_CM)]
    water = spawn_water_actor(context, scene)
    probe_terrain(context, "basin_center", 0.0, 0.0, lake_surface_z_cm(0.0, 0.0))
    for name, x, y in (("ramp_east", 1750.0, 0.0), ("ramp_west", -1750.0, 0.0), ("ramp_north", 0.0, 1750.0), ("ramp_south", 0.0, -1750.0), ("plateau", 2300.0, 0.0)):
        probe_terrain(context, name, x, y, lake_surface_z_cm(x, y))
    return water, terrain, regions, (0.0, -2200.0, LAKE_RIM_Z_CM + 100.0)


def compare_bed_z_cm(x):
    return -COMPARE_SLOPE * min(x, COMPARE_RAMP_END_X_CM)


def build_compare_terrain(context):
    half_length, half_width, outer = COMPARE_HALF_LENGTH_CM, COMPARE_HALF_WIDTH_CM, COMPARE_OUTER_HALF_WIDTH_CM
    pitch = -math.degrees(math.atan(COMPARE_SLOPE))
    ramp_center_x = (-half_length + COMPARE_RAMP_END_X_CM) * 0.5
    ramp_length = COMPARE_RAMP_END_X_CM + half_length
    pool_z = compare_bed_z_cm(COMPARE_RAMP_END_X_CM)
    wall_z = pool_z + COMPARE_WALL_RISE_CM
    place_slab(context, "CompareRamp", (ramp_center_x, 0.0, compare_bed_z_cm(ramp_center_x)), ramp_length, 2.0 * half_width, pitch_degrees=pitch)
    place_slab(context, "ComparePool", ((COMPARE_RAMP_END_X_CM + COMPARE_POOL_END_X_CM) * 0.5, 0.0, pool_z), COMPARE_POOL_END_X_CM - COMPARE_RAMP_END_X_CM, 2.0 * half_width)
    place_slab(context, "CompareEndWall", ((COMPARE_POOL_END_X_CM + half_length) * 0.5, 0.0, wall_z), half_length - COMPARE_POOL_END_X_CM, 2.0 * half_width)
    side_width = outer - half_width
    for sign, side in ((1.0, "North"), (-1.0, "South")):
        side_y = sign * (half_width + side_width * 0.5)
        place_slab(context, f"Compare{side}WallRamp", (ramp_center_x, side_y, compare_bed_z_cm(ramp_center_x) + COMPARE_WALL_RISE_CM), ramp_length, side_width, pitch_degrees=pitch)
        pool_wall_x = (COMPARE_RAMP_END_X_CM + half_length) * 0.5
        place_slab(context, f"Compare{side}WallPool", (pool_wall_x, side_y, wall_z), half_length - COMPARE_RAMP_END_X_CM, side_width)
    box_center_z = compare_bed_z_cm(COMPARE_BOX_CENTER_X_CM) + COMPARE_BOX_LIFT_CM
    box = place_box(context, "CompareObstacleBox", (COMPARE_BOX_CENTER_X_CM, 0.0, box_center_z), COMPARE_BOX_SIZE_CM, unreal.Rotator(), tags=(context["tags"]["obstacle"],))
    box_top = box_center_z + COMPARE_BOX_SIZE_CM[2] * 0.5
    return {"slope": COMPARE_SLOPE, "pool_z_cm": pool_z, "wall_top_z_cm": wall_z, "ramp_end_x_cm": COMPARE_RAMP_END_X_CM,
            "obstacle_box": {"label": box.get_actor_label(), "center_x_cm": COMPARE_BOX_CENTER_X_CM, "size_cm": list(COMPARE_BOX_SIZE_CM), "top_z_cm": box_top}}


def build_compare(context, scene):
    terrain = build_compare_terrain(context)
    reservoir_center_x = sum(COMPARE_RESERVOIR_X_CM) * 0.5
    reservoir_surface = compare_bed_z_cm(reservoir_center_x) + COMPARE_RESERVOIR_ABOVE_BED_CM
    regions = [place_region(context, "CompareUpstreamReservoir", COMPARE_RESERVOIR_X_CM, (-COMPARE_HALF_WIDTH_CM, COMPARE_HALF_WIDTH_CM), reservoir_surface)]
    water = spawn_water_actor(context, scene)
    for name, x in (("ramp_upper", -1500.0), ("ramp_origin", 0.0), ("pool", 1500.0)):
        probe_terrain(context, name, x, 0.0, compare_bed_z_cm(x))
    probe_terrain(context, "obstacle_box_top", COMPARE_BOX_CENTER_X_CM, 0.0, terrain["obstacle_box"]["top_z_cm"])
    return water, terrain, regions, (0.0, -1800.0, compare_bed_z_cm(0.0) + COMPARE_WALL_RISE_CM + 100.0)


def sea_bed_z_cm(x):
    if x <= SEA_BEACH_START_X_CM:
        return SEA_FLOOR_Z_CM
    return SEA_FLOOR_Z_CM + math.tan(math.radians(SEA_BEACH_DEGREES)) * (x - SEA_BEACH_START_X_CM)


def build_sea_terrain(context):
    width = 2.0 * SEA_HALF_CM
    floor_center_x = (-SEA_HALF_CM + SEA_BEACH_START_X_CM) * 0.5
    place_slab(context, "SeaFloor", (floor_center_x, 0.0, SEA_FLOOR_Z_CM), SEA_BEACH_START_X_CM + SEA_HALF_CM, width)
    beach_center_x = (SEA_BEACH_START_X_CM + SEA_HALF_CM) * 0.5
    place_slab(context, "SeaBeach", (beach_center_x, 0.0, sea_bed_z_cm(beach_center_x)), SEA_HALF_CM - SEA_BEACH_START_X_CM, width, pitch_degrees=SEA_BEACH_DEGREES)
    shoreline_x = SEA_BEACH_START_X_CM + (SEA_LEVEL_Z_CM - SEA_FLOOR_Z_CM) / math.tan(math.radians(SEA_BEACH_DEGREES))
    return {"floor_z_cm": SEA_FLOOR_Z_CM, "beach_degrees": SEA_BEACH_DEGREES, "beach_start_x_cm": SEA_BEACH_START_X_CM, "shoreline_x_cm": shoreline_x, "sea_level_z_cm": SEA_LEVEL_Z_CM}


def build_sea(context, scene):
    terrain = build_sea_terrain(context)
    regions = [place_region(context, "SeaFarSurface", SEA_REGION_X_CM, (-SEA_HALF_CM, SEA_HALF_CM), SEA_LEVEL_Z_CM)]
    water = spawn_water_actor(context, scene)
    for name, x in (("sea_floor", -3000.0), ("beach_middle", 1900.0), ("shoreline", terrain["shoreline_x_cm"]), ("beach_high", 3800.0)):
        probe_terrain(context, name, x, 0.0, sea_bed_z_cm(x))
    return water, terrain, regions, (3000.0, 0.0, sea_bed_z_cm(3000.0) + 100.0)


def build_tank_terrain(context):
    wall_center_z = TANK_FLOOR_Z_CM + TANK_WALL_HEIGHT_CM * 0.5
    offset = TANK_INNER_HALF_CM + TANK_WALL_THICKNESS_CM * 0.5
    place_slab(context, "TankFloor", (0.0, 0.0, TANK_FLOOR_Z_CM), TANK_FLOOR_SIZE_CM, TANK_FLOOR_SIZE_CM)
    wall_span_x = 2.0 * TANK_INNER_HALF_CM
    wall_span_y = 2.0 * offset + TANK_WALL_THICKNESS_CM
    for sign, side in ((1.0, "East"), (-1.0, "West")):
        place_box(context, f"Tank{side}Wall", (sign * offset, 0.0, wall_center_z), (TANK_WALL_THICKNESS_CM, wall_span_y, TANK_WALL_HEIGHT_CM), unreal.Rotator())
    for sign, side in ((1.0, "North"), (-1.0, "South")):
        place_box(context, f"Tank{side}Wall", (0.0, sign * offset, wall_center_z), (wall_span_x, TANK_WALL_THICKNESS_CM, TANK_WALL_HEIGHT_CM), unreal.Rotator())
    return {"floor_z_cm": TANK_FLOOR_Z_CM, "inner_half_cm": TANK_INNER_HALF_CM, "wall_thickness_cm": TANK_WALL_THICKNESS_CM,
            "wall_top_z_cm": TANK_FLOOR_Z_CM + TANK_WALL_HEIGHT_CM}


def build_tank(context, scene):
    terrain = build_tank_terrain(context)
    regions = [place_region(context, "TankReservoir", TANK_REGION_XY_CM, TANK_REGION_XY_CM, TANK_REGION_SURFACE_Z_CM)]
    water = spawn_water_actor(context, scene)
    probe_terrain(context, "tank_floor", 0.0, 0.0, TANK_FLOOR_Z_CM)
    probe_terrain(context, "tank_wall_top", TANK_INNER_HALF_CM + TANK_WALL_THICKNESS_CM * 0.5, 0.0, terrain["wall_top_z_cm"])
    return water, terrain, regions, (0.0, -300.0, TANK_FLOOR_Z_CM + 100.0)


def configure_ship_hull(context, hull, water):
    interp = add_actor_component(hull, unreal.InterpToMovementComponent)
    interp.set_editor_property("behaviour_type", unreal.InterpToBehaviourType.PING_PONG)
    interp.set_editor_property("duration", SHIP_TRAVEL_SECONDS)
    interp.add_control_point_position(unreal.Vector(SHIP_TRAVEL_CM, 0.0, 0.0), True)
    wake = add_actor_component(hull, context["hull_wake_class"])
    wake.set_editor_property("target_water_actor", water)
    wake.set_editor_property("hull_length_cm", SHIP_LENGTH_CM)
    wake.set_editor_property("hull_beam_cm", SHIP_BEAM_CM)
    wake.set_editor_property("drives_water_window", True)
    if wake.get_editor_property("target_water_actor") != water:
        raise RuntimeError("선체 부품 target_water_actor 지정 확인 실패")
    if not wake.get_editor_property("drives_water_window"):
        raise RuntimeError("선체 부품 drives_water_window 지정 확인 실패")
    return {"hull_length_cm": SHIP_LENGTH_CM, "hull_beam_cm": SHIP_BEAM_CM, "hull_height_cm": SHIP_HEIGHT_CM, "start_x_cm": -SHIP_TRAVEL_CM * 0.5,
            "end_x_cm": SHIP_TRAVEL_CM * 0.5, "travel_cm": SHIP_TRAVEL_CM, "one_way_seconds": SHIP_TRAVEL_SECONDS,
            "speed_cm_per_sec": SHIP_TRAVEL_CM / SHIP_TRAVEL_SECONDS, "behaviour": "PING_PONG", "components": ["TDFluidHullWakeComponent", "InterpToMovementComponent"]}


def build_ship(context, scene):
    place_slab(context, "ShipSeabed", (0.0, 0.0, SHIP_SEABED_Z_CM), SHIP_SEABED_SIZE_CM, SHIP_SEABED_SIZE_CM)
    half_region = SHIP_REGION_SIZE_CM * 0.5
    regions = [place_region(context, "ShipSeaSurface", (-half_region, half_region), (-half_region, half_region), 0.0)]
    water = spawn_water_actor(context, scene)
    hull = place_box(context, "ShipHull", (-SHIP_TRAVEL_CM * 0.5, 0.0, SHIP_CENTER_Z_CM), (SHIP_LENGTH_CM, SHIP_BEAM_CM, SHIP_HEIGHT_CM), unreal.Rotator(),
                     tags=(context["tags"]["ignore"],), mobility=unreal.ComponentMobility.MOVABLE)
    terrain = {"seabed_z_cm": SHIP_SEABED_Z_CM, "sea_level_z_cm": 0.0, "region_size_cm": SHIP_REGION_SIZE_CM, "hull": configure_ship_hull(context, hull, water)}
    terrain["hull"]["label"] = hull.get_actor_label()
    for name, x in (("seabed_center", 0.0), ("seabed_far", 12000.0)):
        probe_terrain(context, name, x, 0.0, SHIP_SEABED_Z_CM)
    return water, terrain, regions, (0.0, 0.0, 200.0)


SCENE_BUILDERS = {"creek": build_creek, "lake": build_lake, "compare": build_compare, "sea": build_sea, "tank": build_tank, "ship": build_ship}


def describe_window(effective):
    extent = effective.get_editor_property("sim_extent_cm")
    resolution = int(effective.get_editor_property("grid_resolution"))
    return {"resolution": resolution, "extent_cm": [extent.x, extent.y], "center_cm": [0.0, 0.0], "cell_cm": extent.x / resolution,
            "bounds_min_cm": [-extent.x * 0.5, -extent.y * 0.5], "bounds_max_cm": [extent.x * 0.5, extent.y * 0.5]}


def build_scene(context, scene):
    stage = context["stage"]
    map_path = map_path_for(scene)
    print(f"단계: 장면 {scene['index']} {scene['name']} {map_path}")
    levels, actors = stage.open_empty_level(map_path)
    world = context["editor"].get_editor_world()
    game_mode = unreal.EditorAssetLibrary.load_blueprint_class(GAME_MODE_PATH)
    if game_mode is None:
        raise RuntimeError("게임 모드 블루프린트 클래스를 불러오지 못했습니다: " + GAME_MODE_PATH)
    world.get_world_settings().set_editor_property("default_game_mode", game_mode)
    context.update({"levels": levels, "actors": actors, "world": world, "created": [], "probes": [], "placed_geometry_count": 0})
    water, terrain, regions, start = SCENE_BUILDERS[scene["layout"]](context, scene)
    place_lighting_and_start(context, start)
    effective = context["presets"][scene["preset"]][2]
    window = describe_window(effective)
    warnings = []
    if max(window["extent_cm"]) * 0.5 > scene["coverage_half_cm"]:
        warnings.append(f"{scene['name']}: 창 반폭 {max(window['extent_cm']) * 0.5:.0f} cm 가 지형 덮개 {scene['coverage_half_cm']:.0f} cm 보다 큼(밖은 지형 미스 = 액터 높이)")
    if not levels.save_current_level():
        raise RuntimeError(f"무대 레벨 저장 실패: {map_path}")
    cache_expected = 1 if scene["cache_label"] else 0
    record = {"scene": scene["index"], "name": scene["name"], "map": map_path, "label": water.get_actor_label(), "kind": scene["kind"],
              "preset": context["presets"][scene["preset"]][1], "output_material": material_path_for(scene), "is_dense": scene["is_dense"],
              "cache_label": scene["cache_label"], "cache_asset": f"/Game/FluidFX/DenseWaterCaches/DWC_TD_DenseWaterStage_{scene['cache_label']}" if scene["cache_label"] else "",
              "cache_load_expected": cache_expected, "window": window, "preset_effective": describe_properties(effective, EFFECTIVE_REPORT_NAMES),
              "water_actor_properties": describe_properties(water, WATER_REPORT_PROPERTIES), "terrain": terrain, "placed_geometry_count": context["placed_geometry_count"],
              "regions": regions, "probes": context["probes"], "player_start": list(start), "camera": scene["camera"], "created_actors": len(context["created"])}
    return record, warnings


def main():
    stage = load_module("td_fluid_particle_stage", *PARTICLE_STAGE_RELATIVE_PATH)
    editor = stage.require_editor_ready(REQUIRED_UNREAL_NAMES)
    request = read_request()
    scenes = select_scenes(request)
    overwrite = request.get("overwrite") is True
    water_class = unreal.load_class(None, WATER_ACTOR_CLASS_PATH)
    hull_wake_class = unreal.load_class(None, HULL_WAKE_CLASS_PATH)
    if water_class is None or hull_wake_class is None:
        raise RuntimeError("물 액터·선체 부품 클래스 없음(W-6 통합 재빌드와 에디터 재시작 필요): " + WATER_ACTOR_CLASS_PATH + ", " + HULL_WAKE_CLASS_PATH)
    contract_problems = stage.find_missing_properties(unreal.get_default_object(water_class), WATER_ACTOR_PROPERTIES)
    contract_problems += stage.find_missing_properties(unreal.get_default_object(hull_wake_class), HULL_WAKE_PROPERTIES)
    contract_problems += stage.find_missing_properties(unreal.TDFluidPresetAsset.get_default_object(), PRESET_PROPERTIES)
    if contract_problems:
        raise RuntimeError("C++ 계약 속성 없음(재빌드와 에디터 재시작 필요): " + ", ".join(contract_problems))
    resources = {
        "surface_mesh": load_asset_or_raise(SURFACE_MESH_PATH, "editor_make_fluid_water_material.py 먼저"),
        "materials": {name: load_asset_or_raise(f"{MATERIAL_DIR}/{name}", "E1 물 마스터·조각 병합 먼저") for name in sorted({scene["material"] for scene in scenes})},
    }
    cube = load_asset_or_raise(CUBE_MESH_PATH, "엔진 기본 도형")
    sphere = load_asset_or_raise(SPHERE_MESH_PATH, "엔진 기본 도형")
    preset_paths = sorted({preset_path_for(stage, scene) for scene in scenes})
    map_paths = [map_path_for(scene) for scene in scenes]
    stage.guard_existing_assets(preset_paths + map_paths, overwrite, REQUEST_RELATIVE_PATH)
    print("단계: 프리셋·모양 정본 검사·다시 읽기")
    tuning_sha256, look_sha256 = stage.check_and_reload_preset_data()
    print("단계: 프리셋")
    presets = {}
    for scene in scenes:
        if scene["preset"] not in presets:
            presets[scene["preset"]] = configure_preset(stage, scene, resources)
    context = {"stage": stage, "editor": editor, "folder": STAGE_FOLDER, "cube": cube, "sphere": sphere, "presets": presets, "water_class": water_class,
               "hull_wake_class": hull_wake_class, "tags": read_water_tag_names(water_class), "levels": None}
    records, warnings = [], []
    for scene in scenes:
        record, scene_warnings = build_scene(context, scene)
        records.append(record)
        warnings.extend(scene_warnings)
    stage.require_saved_files(preset_paths, map_paths[0])
    for map_path in map_paths[1:]:
        stage.require_saved_files([], map_path)
    cache_load_expected = sum(record["cache_load_expected"] for record in records)
    is_complete = len(records) == len(SCENE_SPECS)
    if is_complete and cache_load_expected != EXPECTED_CACHE_LOADS:
        raise RuntimeError(f"캐시 로드 기대 합 {cache_load_expected} != {EXPECTED_CACHE_LOADS}")
    report = {"status": "ok", "preset_tuning_sha256": tuning_sha256, "material_look_sha256": look_sha256, "overwrite": overwrite,
              "complete": is_complete, "scenes_requested": [scene["index"] for scene in scenes], "cache_load_expected": cache_load_expected,
              "water_tags": context["tags"], "presets": {name: value[1] for name, value in presets.items()}, "scenes": records,
              "warnings": warnings, "saved": True}
    report_path = stage.write_report(report, REPORT_RELATIVE_PATH)
    print(json.dumps(report, ensure_ascii=False, default=str))
    print("보고서: " + report_path)


def run_with_failure_report():
    try:
        main()
    except Exception as error:
        failure_path = project_path(*REPORT_RELATIVE_PATH)
        os.makedirs(os.path.dirname(failure_path), exist_ok=True)
        with open(failure_path, "w", encoding="utf-8") as failure_file:
            json.dump({"status": "failed", "error": str(error)}, failure_file, ensure_ascii=False, indent=2)
        raise


if __name__ == "__main__":
    run_with_failure_report()
