"""실행 환경: TDGame 언리얼 에디터 안에서 FluidFX 불·폭발 무대 LV_TDFluidFireStage(장면 1 횃불을 든 폰, 장면 2 모닥불과 연기, 장면 3 폭발 블록)와 프리셋 6종·벽돌 지오메트리 컬렉션을 만든다. 먼저 editor_make_fluid_volume_material.py 와 editor_make_fluid_flame_material.py 가 성공하고 C++ 재빌드·에디터 재시작이 끝나 있어야 한다.
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_fire_stage.py
출력: /Game/FluidFX/Presets/DA_TDFluid_TorchFlame·CampfireFlame·CampfireSmoke·ExplosionFire·ExplosionSmoke·ExplosionGroundDust, /Game/FluidFX/Destruction/GC_TD_FluidExplosionCube, /Game/FluidFX/Maps/LV_TDFluidFireStage, Saved/FluidFX/fluid_fire_stage.json
상태: 검증됨(2026-10-18 FX-3 불 무대 생성·쇼케이스·탐침 성공, 2026-10-20 FX-8b C++ 빌더 프리셋 6종·지면 먼지·조각 대상 배열로 재생성 17액터, 마지막 재생성 2026-10-20 10:53 생성 액터 18개). FX-8c 5차 굽기(2026-10-20: 모닥불 depth_bias 40→10, 폭발 조각 대상 burst_radius_scale 연기 2.5→1.75·먼지 1.6→1.12, 먼지 볼륨 재질 MI_TD_FluidVolume_GroundDust 와 위치 z 50→0, 횃불 막대 Transform 을 손 소켓 축 방향 위치 (-25.15, 15.69, -4.63)·길이 60cm 로, 불씨 source_offset_cm 을 같은 축 50cm (-41.91, 26.15, -7.71) 로 넣고 다시 읽어 확인)는 에디터 재실행 전이며 C++ 굽기 값(폭발 트레일 코어 200·고리 0, 폭발 연기 열 발광색, 카드 축소 1.0)이 들어간 재빌드가 먼저다. 에셋·맵이 이미 있으면 Saved/FluidFX/fluid_fire_stage_request.json 의 {"overwrite": true} 가 있을 때만 갱신하고(맵은 지우지 않고 열어서 액터만 비움), C++ 계약 속성이 하나라도 없거나 미저장 작업이 있으면 중단한다.
"""
import json
import os

import unreal


FLUID_ROOT = "/Game/FluidFX"
MATERIAL_DIR = f"{FLUID_ROOT}/Materials"
PRESET_DIR = f"{FLUID_ROOT}/Presets"
MAP_DIR = f"{FLUID_ROOT}/Maps"
DESTRUCTION_DIR = f"{FLUID_ROOT}/Destruction"

STAGE_MAP_PATH = f"{MAP_DIR}/LV_TDFluidFireStage"
MATTE_MASTER_PATH = f"{MATERIAL_DIR}/M_TD_FluidFireMatte"
FLOOR_INSTANCE_PATH = f"{MATERIAL_DIR}/MI_TD_FluidFireFloor"
WOOD_INSTANCE_PATH = f"{MATERIAL_DIR}/MI_TD_FluidFireCharredWood"
CHARRED_WOOD_INSTANCE_PATH = f"{MATERIAL_DIR}/MI_TD_FluidCharredWood_Campfire"
GEOMETRY_COLLECTION_NAME = "GC_TD_FluidExplosionCube"
GEOMETRY_COLLECTION_PATH = f"{DESTRUCTION_DIR}/{GEOMETRY_COLLECTION_NAME}"
GAME_MODE_PATH = "/Game/Combat/Blueprints/BP_TDCombatGameMode"

CUBE_MESH_PATH = "/Engine/BasicShapes/Cube"
PLANE_MESH_PATH = "/Engine/BasicShapes/Plane"
SPHERE_MESH_PATH = "/Engine/BasicShapes/Sphere"
CYLINDER_MESH_PATH = "/Engine/BasicShapes/Cylinder"

FLAME_INSTANCE_PATHS = {
    "torch": f"{MATERIAL_DIR}/MI_TD_FluidFlame_Torch",
    "campfire": f"{MATERIAL_DIR}/MI_TD_FluidFlame_Campfire",
    "explosion": f"{MATERIAL_DIR}/MI_TD_FluidFlame_Explosion",
}
EMBER_INSTANCE_PATH = f"{MATERIAL_DIR}/MI_TD_FluidEmber_Default"
SMOKE_VOLUME_INSTANCE_PATH = f"{MATERIAL_DIR}/MI_TD_FluidVolume_SmokeBillow"
DUST_VOLUME_INSTANCE_PATH = f"{MATERIAL_DIR}/MI_TD_FluidVolume_GroundDust"

REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_fire_stage_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_fire_stage.json")

BRICK_COUNT = (4, 4, 4)
BRICK_SIZE_CM = 25.0

FLOOR_COLOR = (0.17, 0.145, 0.12)
WOOD_COLOR = (0.03, 0.024, 0.02)
PLAYER_START = (0.0, 0.0, 96.0)
NAVIGATION_SCALE = (10.0, 28.0, 4.0)
NAVIGATION_CENTER = (0.0, 1800.0, 100.0)
NAVIGATION_MIN_EXTENT_CM = (900.0, 2500.0)
EXPOSURE_EV100 = 2.0
MOON_LUX = 3.0
MOON_COLOR = (0.62, 0.68, 0.9)
STAGE_FOG_LUMINANCE = (0.165, 0.17, 0.195)
STAGE_FOG_DENSITY = 0.02
STAGE_FOG_HEIGHT_FALLOFF = 0.2
STAGE_FOG_START_DISTANCE_CM = 1500.0

TORCH_LOCATION = (0.0, 0.0, 100.0)
CAMPFIRE_FIRE_LOCATION = (0.0, 1600.0, 0.0)
CAMPFIRE_SMOKE_LOCATION = (0.0, 1600.0, 80.0)
EXPLOSION_BLOCK_LOCATION = (0.0, 3200.0, 50.0)
EXPLOSION_DUST_LOCATION = (0.0, 3200.0, 0.0)
EXPLOSION_HIT_OFFSET = (-55.0, 0.0, -35.0)
BREAK_FORCE = 650.0
BREAK_FORCE_RADIUS = 260.0
TORCH_SOCKET_NAME = "hand_l"

PRESET_SPECS = (
    {
        "name": "TorchFlame",
        "kind": "FIRE",
        "cpp_kind": "TORCH",
        "channel": "CHANNEL_B_FIRE",
        "scene": "torch",
        "sort_priority": 1,
        "plane": {"orientation": "CAMERA_FACING", "depth_bias": 35.0, "interaction_depth": 150.0, "center_offset": 0.3},
    },
    {
        "name": "CampfireFlame",
        "kind": "FIRE",
        "cpp_kind": "CAMPFIRE",
        "channel": "CHANNEL_B_FIRE",
        "scene": "campfire",
        "sort_priority": 1,
        "plane": {"orientation": "CAMERA_FACING", "depth_bias": 10.0, "interaction_depth": 150.0, "center_offset": 0.3},
    },
    {
        "name": "CampfireSmoke",
        "kind": "SMOKE",
        "cpp_kind": "CAMPFIRE_SMOKE",
        "channel": "CHANNEL_R_MUD",
        "scene": "campfire",
        "sort_priority": 0,
        "volume_height_cm": 200.0,
        "plane": {"orientation": "CAMERA_FACING", "depth_bias": 0.0, "interaction_depth": 200.0, "center_offset": 0.4},
    },
    {
        "name": "ExplosionFire",
        "kind": "FIRE",
        "cpp_kind": "EXPLOSION",
        "channel": "CHANNEL_B_FIRE",
        "scene": "explosion",
        "sort_priority": 1,
        "plane": {"orientation": "CAMERA_FACING", "depth_bias": 60.0, "interaction_depth": 400.0, "center_offset": 0.45},
    },
    {
        "name": "ExplosionSmoke",
        "kind": "SMOKE",
        "cpp_kind": "EXPLOSION_SMOKE",
        "channel": "CHANNEL_R_MUD",
        "scene": "explosion",
        "sort_priority": 0,
        "volume_height_cm": 320.0,
        "plane": {"orientation": "CAMERA_FACING", "depth_bias": 0.0, "interaction_depth": 400.0, "center_offset": 0.3},
    },
    {
        "name": "ExplosionGroundDust",
        "kind": "SMOKE",
        "cpp_kind": "EXPLOSION_GROUND_DUST",
        "channel": "CHANNEL_R_MUD",
        "scene": "explosion",
        "sort_priority": 0,
        "volume_height_cm": 200.0,
        "volume_material": "dust_volume",
        "plane": {"orientation": "WORLD_FACING", "depth_bias": 0.0, "interaction_depth": 400.0, "center_offset": 0.0},
    },
)
PRESET_KIND_BY_NAME = {spec["name"]: spec["cpp_kind"] for spec in PRESET_SPECS}
EXPLOSION_TARGET_SPECS = (
    {"actor": "fire", "channel": "CHANNEL_B_FIRE", "heat_scale": 1.0, "radius_scale": 1.0, "burst_radius_scale": 1.0, "lagged": False, "height_falloff": 0.0},
    {"actor": "smoke", "channel": "CHANNEL_R_MUD", "heat_scale": 1.0, "radius_scale": 1.0, "burst_radius_scale": 1.75, "lagged": True, "height_falloff": 0.0},
    {"actor": "dust", "channel": "CHANNEL_R_MUD", "heat_scale": 0.0, "radius_scale": 1.6, "burst_radius_scale": 1.12, "lagged": False, "height_falloff": 60.0},
)

FIRE_ACTOR_SPECS = {
    "TorchFire": {
        "preset": "TorchFlame",
        "light": {"candelas": 60.0, "reference_area": 1328.0, "shadows": False, "height_offset": 15.0},
        "ember": {"max": 64, "per_second": 10.0},
    },
    "CampfireFire": {
        "preset": "CampfireFlame",
        "light": {"candelas": 160.0, "reference_area": 5265.0, "shadows": True, "height_offset": 60.0},
        "ember": {"max": 256, "per_second": 40.0},
    },
    "ExplosionFire": {
        "preset": "ExplosionFire",
        "light": {"candelas": 120.0, "reference_area": 16029.0, "shadows": True, "height_offset": 60.0},
        "ember": {"max": 256, "per_second": 30.0},
    },
}

ENUM_CONTRACTS = {"TDFluidPlaneOrientation": ("WORLD_FACING", "CAMERA_FACING", "VERTICAL_BILLBOARD")}
STRUCT_CONTRACTS = {
    "TDFluidPlaneSettings": (
        "orientation",
        "billboard_lean_back_degrees",
        "camera_depth_bias_cm",
        "interaction_depth_cm",
        "center_offset_along_up_ratio",
    ),
    "TDFluidSimulationConfig": (
        "buoyancy_cm_per_sec2",
        "outflow_band_ratio",
        "outflow_rate_per_sec",
        "flow_noise_acceleration_cm_per_sec2",
        "flow_noise_scale_cm",
        "flow_noise_speed",
        "border_fade_ratio",
        "max_points_per_tick",
    ),
    "TDFluidSourceEmitterSettings": (
        "is_enabled",
        "channel",
        "radius_cm",
        "density_strength",
        "emit_speed_cm_per_sec",
        "motion_inertia_scale",
        "flicker_amplitude",
        "flicker_frequency_hz",
        "source_offset_cm",
        "source_stamp_count",
        "source_spread_cm",
        "trail_lead_seconds",
        "max_trail_lead_ratio",
        "random_seed",
    ),
    "TDFluidDownstreamFeedSettings": (
        "is_enabled",
        "band_start_ratio",
        "min_feed_density",
        "faded_density_threshold",
        "max_stamps_per_step",
        "density_scale",
        "radius_scale",
        "velocity_inheritance",
    ),
    "TDFluidFragmentTrailSettings": (
        "tracking_duration_seconds",
        "armed_poll_interval_seconds",
        "should_crumble_on_trigger",
        "min_fragment_speed_cm_per_sec",
        "max_fragment_speed_cm_per_sec",
        "fire_stamp_radius_cm",
        "fire_density_strength",
        "smoke_stamp_radius_cm",
        "smoke_density_strength",
        "smoke_trail_lag_frames",
        "max_fragments_per_step",
        "max_stamps_per_fragment",
        "velocity_inheritance",
        "burst_core_radius_cm",
        "burst_ring_radius_cm",
        "burst_ring_stamp_count",
        "burst_ring_speed_cm_per_sec",
        "ember_burst_count",
    ),
}
OBJECT_CONTRACTS = {
    "TDFluidPresetAsset": (
        "preset_kind",
        "simulation_config",
        "default_channel",
        "override_preset_defaults",
        "output_material",
        "surface_mesh",
        "volume_material",
        "volume_mesh",
        "volume_height_cm",
        "plane_settings",
        "translucency_sort_priority",
    ),
    "TDFluidLiveActor": (
        "should_follow_player_pawn",
        "should_use_activation_volume",
        "interaction_brush_channel",
        "live_component",
        "volume_mesh_component",
        "internal_surface_mesh",
        "volume_light_actor",
    ),
    "TDFluidFireActor": (
        "source_emitter",
        "follow_socket_name",
        "should_harvest_interactions",
        "downstream_smoke_actor",
        "downstream_feed",
        "source_prop_mesh_asset",
        "source_prop_relative_transform",
        "fire_light_component",
        "ember_component",
        "source_prop_mesh_component",
    ),
    "TDFluidFragmentEmitterActor": (
        "source_breakable_actor",
        "fire_target_actor",
        "smoke_target_actor",
        "trail_settings",
    ),
    "TDFluidFireLightComponent": (
        "is_driven_by_fluid",
        "uses_selective_density",
        "max_intensity_candelas",
        "reference_density_area_cm2",
        "response_per_sec",
        "flicker_noise_amplitude",
        "flicker_noise_frequency_hz",
        "min_color_temperature_kelvin",
        "max_color_temperature_kelvin",
        "height_offset_cm",
        "move_threshold_cm",
        "random_seed",
    ),
    "TDFluidEmberComponent": (
        "ember_mesh_asset",
        "ember_material_asset",
        "max_embers",
        "spawn_per_second",
        "spawn_per_density_area",
        "lifetime_range_seconds",
        "fluid_drag_per_sec",
        "buoyancy_cm_per_sec2",
        "gravity_cm_per_sec2",
        "cooling_per_sec",
        "min_speed_kill_cm_per_sec",
        "slow_kill_seconds",
        "stretch_per_cm_per_sec",
        "base_scale",
        "depth_jitter_cm",
        "random_seed",
    ),
}
FUNCTION_CONTRACTS = {
    "TDFluidLiveComponent": ("compute_density_centroid_world", "set_simulation_awake"),
    "TDFluidFireLightComponent": ("get_last_density_area_cm2", "get_last_target_intensity", "get_last_intensity_candelas"),
    "TDFluidEmberComponent": ("spawn_burst", "get_alive_ember_count", "get_mean_ember_heat", "compute_ember_state_hash"),
    "TDFluidFragmentEmitterActor": (
        "trigger_break_at",
        "is_tracking_fragments",
        "get_tracked_leaf_count",
        "get_moved_leaf_count",
        "get_stamped_leaf_count",
        "get_total_stamp_count",
    ),
    "TDFluidFractureEditorLibrary": ("create_brick_geometry_collection",),
}
CLASS_PATHS = {
    "TDFluidFireActor": "/Script/TDGame.TDFluidFireActor",
    "TDFluidFragmentEmitterActor": "/Script/TDGame.TDFluidFragmentEmitterActor",
    "TDFluidFireLightComponent": "/Script/TDGame.TDFluidFireLightComponent",
    "TDFluidEmberComponent": "/Script/TDGame.TDFluidEmberComponent",
    "TDFluidFractureEditorLibrary": "/Script/TDGameEditor.TDFluidFractureEditorLibrary",
    "TDFluidLiveActor": "/Script/TDGame.TDFluidLiveActor",
    "TDBreakableActor": "/Script/TDGame.TDBreakableActor",
}

REQUIRED_UNREAL_NAMES = (
    "EditorAssetLibrary.load_blueprint_class",
    "EditorAssetLibrary.load_asset",
    "EditorAssetLibrary.delete_asset",
    "MaterialEditingLibrary.recompile_material",
    "EditorLoadingAndSavingUtils.get_dirty_map_packages",
    "EditorLoadingAndSavingUtils.get_dirty_content_packages",
    "SystemLibrary.get_project_directory",
    "GameplayStatics.get_all_actors_of_class",
    "Material",
    "MaterialFactoryNew",
    "DataAssetFactory",
    "GeometryCollectionComponent",
    "IntVector",
    "Transform",
    "MaterialExpressionConstant",
    "MaterialExpressionConstant3Vector",
    "MaterialProperty.MP_BASE_COLOR",
    "MaterialProperty.MP_ROUGHNESS",
    "MaterialProperty.MP_METALLIC",
    "TDFluidPresetAsset.build_config_for_preset_kind",
    "TDFluidPresetAsset.build_source_emitter_for_preset_kind",
    "TDFluidPresetAsset.build_fragment_trail_for_preset_kind",
    "TDFluidPresetAsset.build_volume_render_for_preset_kind",
    "TDFluidPresetAsset.build_flame_card_layers_for_preset_kind",
    "TDFluidPresetKind.EXPLOSION_GROUND_DUST",
    "TDFluidFragmentTarget",
    "TDFluidPresetKind.CUSTOM",
    "TDFluidPresetKind.FIRE",
    "TDFluidPresetKind.SMOKE",
    "TDFluidBrushChannel.CHANNEL_B_FIRE",
    "TDFluidBrushChannel.CHANNEL_R_MUD",
    "TDFluidPlaneOrientation.CAMERA_FACING",
    "AutoExposureMethod.AEM_HISTOGRAM",
    "RuntimeGenerationType.DYNAMIC",
    "ComponentMobility.STATIC",
    "ComponentMobility.MOVABLE",
    "NavMeshBoundsVolume",
    "RecastNavMesh",
    "DirectionalLight",
    "SkyLight",
    "ExponentialHeightFog",
    "PostProcessVolume",
    "PlayerStart",
)


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


def asset_disk_path(path, extension):
    return project_path("Content", *path[len("/Game/"):].split("/")) + extension


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


def find_missing_properties(owner, property_names):
    return [name for name in property_names if not has_editor_property(owner, name)]


def find_missing_members(owner, member_names):
    return [name for name in member_names if not hasattr(owner, name)]


def check_cpp_contract():
    problems = []
    for enum_name, value_names in ENUM_CONTRACTS.items():
        enum_type = getattr(unreal, enum_name, None)
        if enum_type is None:
            problems.append(enum_name)
            continue
        problems.extend(f"{enum_name}.{name}" for name in find_missing_members(enum_type, value_names))

    for struct_name, property_names in STRUCT_CONTRACTS.items():
        struct_type = getattr(unreal, struct_name, None)
        if struct_type is None:
            problems.append(struct_name)
            continue
        problems.extend(f"{struct_name}.{name}" for name in find_missing_properties(struct_type(), property_names))

    default_objects = {}
    for class_name, property_names in OBJECT_CONTRACTS.items():
        class_type = getattr(unreal, class_name, None)
        if class_type is None:
            problems.append(class_name)
            continue
        default_objects[class_name] = unreal.get_default_object(class_type)
        problems.extend(f"{class_name}.{name}" for name in find_missing_properties(default_objects[class_name], property_names))

    for class_name in ("TDFluidFractureEditorLibrary", "TDBreakableActor"):
        if getattr(unreal, class_name, None) is None:
            problems.append(class_name)

    for class_name, function_names in FUNCTION_CONTRACTS.items():
        class_type = getattr(unreal, class_name, None)
        if class_type is None:
            problems.append(class_name)
            continue
        problems.extend(f"{class_name}.{name}()" for name in find_missing_members(class_type, function_names))

    fire_defaults = default_objects.get("TDFluidFireActor")
    if fire_defaults is not None:
        for component_property in ("fire_light_component", "ember_component"):
            if has_editor_property(fire_defaults, component_property) and fire_defaults.get_editor_property(component_property) is None:
                problems.append(f"TDFluidFireActor.{component_property}(None)")
    return problems


def read_overwrite_request():
    request_path = project_path(*REQUEST_RELATIVE_PATH)
    if not os.path.isfile(request_path):
        return False
    with open(request_path, encoding="utf-8") as request_file:
        request = json.load(request_file)
    return isinstance(request, dict) and request.get("overwrite") is True


def preset_asset_name(spec):
    return f"DA_TDFluid_{spec['name']}"


def preset_asset_path(spec):
    return f"{PRESET_DIR}/{preset_asset_name(spec)}"


def collect_guarded_asset_paths():
    return [preset_asset_path(spec) for spec in PRESET_SPECS] + [GEOMETRY_COLLECTION_PATH, MATTE_MASTER_PATH, FLOOR_INSTANCE_PATH, WOOD_INSTANCE_PATH, STAGE_MAP_PATH]


def find_unsaved_package_names(overwrite):
    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    dirty.extend(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    names = sorted({package.get_name() for package in dirty})
    if not overwrite:
        return names
    return [name for name in names if not name.startswith(FLUID_ROOT + "/")]


def ensure_directory(directory_path):
    assets = unreal.EditorAssetLibrary
    if assets.does_directory_exist(directory_path):
        return
    if not assets.make_directory(directory_path):
        raise RuntimeError(f"폴더 생성 실패: {directory_path}")


def save_asset(asset, path):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"에셋 저장 실패: {path}")


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


def add_node(material, expression_class, x, y, **properties):
    expression = unreal.MaterialEditingLibrary.create_material_expression(material, expression_class, x, y)
    if expression is None:
        raise RuntimeError(f"재질 노드 생성 실패: {expression_class.__name__}")
    for name, value in properties.items():
        expression.set_editor_property(name, value)
    return expression


def connect_material_output(expression, material_property):
    if not unreal.MaterialEditingLibrary.connect_material_property(expression, "", material_property):
        raise RuntimeError(f"재질 출력 연결 실패: {material_property}")


def load_or_create_matte_master():
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(MATTE_MASTER_PATH):
        return assets.load_asset(MATTE_MASTER_PATH), False

    asset_dir, asset_name = MATTE_MASTER_PATH.rsplit("/", 1)
    ensure_directory(asset_dir)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name, asset_dir, unreal.Material, unreal.MaterialFactoryNew()
    )
    if material is None:
        raise RuntimeError(f"재질 생성 실패: {MATTE_MASTER_PATH}")
    base_color = add_node(
        material,
        unreal.MaterialExpressionVectorParameter,
        -280,
        -120,
        parameter_name="BaseColor",
        default_value=unreal.LinearColor(0.5, 0.5, 0.5, 1.0),
    )
    roughness = add_node(material, unreal.MaterialExpressionScalarParameter, -280, 20, parameter_name="Roughness", default_value=0.95)
    metallic = add_node(material, unreal.MaterialExpressionConstant, -280, 120, r=0.0)
    connect_material_output(base_color, unreal.MaterialProperty.MP_BASE_COLOR)
    connect_material_output(roughness, unreal.MaterialProperty.MP_ROUGHNESS)
    connect_material_output(metallic, unreal.MaterialProperty.MP_METALLIC)
    errors = [str(item) for item in unreal.MaterialEditingLibrary.recompile_material(material) or []]
    if errors:
        raise RuntimeError(f"재질 컴파일 실패: {MATTE_MASTER_PATH}, {errors}")
    save_asset(material, MATTE_MASTER_PATH)
    return material, True


def write_matte_instance(path, master, color_rgb, roughness_value):
    assets = unreal.EditorAssetLibrary
    editing = unreal.MaterialEditingLibrary
    if assets.does_asset_exist(path):
        instance = assets.load_asset(path)
    else:
        asset_dir, asset_name = path.rsplit("/", 1)
        instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name, asset_dir, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew()
        )
    if not isinstance(instance, unreal.MaterialInstanceConstant):
        raise RuntimeError(f"재질 인스턴스 생성 실패: {path}")
    editing.set_material_instance_parent(instance, master)
    editing.set_material_instance_vector_parameter_value(instance, "BaseColor", unreal.LinearColor(color_rgb[0], color_rgb[1], color_rgb[2], 1.0))
    editing.set_material_instance_scalar_parameter_value(instance, "Roughness", roughness_value)
    editing.update_material_instance(instance)
    save_asset(instance, path)
    return instance


def load_or_create_preset(spec):
    path = preset_asset_path(spec)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        preset = unreal.EditorAssetLibrary.load_asset(path)
        if not isinstance(preset, unreal.TDFluidPresetAsset):
            raise RuntimeError(f"기존 프리셋 에셋 형식 불일치: {path}")
        return preset
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.TDFluidPresetAsset)
    preset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        preset_asset_name(spec), PRESET_DIR, unreal.TDFluidPresetAsset, factory
    )
    if preset is None:
        raise RuntimeError(f"프리셋 생성 실패: {path}")
    return preset


def build_plane_settings(spec):
    settings = unreal.TDFluidPlaneSettings()
    settings.set_editor_property("orientation", getattr(unreal.TDFluidPlaneOrientation, spec["plane"]["orientation"]))
    settings.set_editor_property("camera_depth_bias_cm", spec["plane"]["depth_bias"])
    settings.set_editor_property("interaction_depth_cm", spec["plane"]["interaction_depth"])
    settings.set_editor_property("center_offset_along_up_ratio", spec["plane"]["center_offset"])
    return settings


def resolve_preset_kind(spec):
    return getattr(unreal.TDFluidPresetKind, spec["cpp_kind"])


def configure_preset(spec, assets_by_name):
    preset = load_or_create_preset(spec)
    is_fire = spec["kind"] == "FIRE"
    kind = resolve_preset_kind(spec)
    built_config = unreal.TDFluidPresetAsset.build_config_for_preset_kind(kind)
    preset.set_editor_property("preset_kind", kind)
    preset.set_editor_property("override_preset_defaults", False)
    preset.set_editor_property("default_channel", getattr(unreal.TDFluidBrushChannel, spec["channel"]))
    preset.set_editor_property("simulation_config", built_config)
    preset.set_editor_property("volume_render", unreal.TDFluidPresetAsset.build_volume_render_for_preset_kind(kind))
    preset.set_editor_property("plane_settings", build_plane_settings(spec))
    preset.set_editor_property("translucency_sort_priority", spec["sort_priority"])
    if is_fire:
        preset.set_editor_property("output_material", assets_by_name["flame_" + spec["scene"]])
        preset.set_editor_property("surface_mesh", assets_by_name["plane"])
    else:
        preset.set_editor_property("volume_material", assets_by_name[spec.get("volume_material", "smoke_volume")])
        preset.set_editor_property("volume_mesh", assets_by_name["cube"])
        preset.set_editor_property("volume_height_cm", spec["volume_height_cm"])

    effective = preset.resolve_effective_config()
    checked_names = ("grid_resolution", "cpu_fallback_resolution", "density_dissipation", "enable_heat_channel", "heat_cooling_per_sec", "vortex_radial_drift_cm_per_sec")
    mismatches = [name for name in checked_names if effective.get_editor_property(name) != built_config.get_editor_property(name)]
    if mismatches:
        raise RuntimeError(f"프리셋 유효 구성이 C++ 정적 빌더와 다릅니다: {preset_asset_path(spec)} {mismatches}")
    stored_plane = preset.get_editor_property("plane_settings")
    if stored_plane.get_editor_property("orientation") != getattr(unreal.TDFluidPlaneOrientation, spec["plane"]["orientation"]):
        raise RuntimeError(f"프리셋 평면 방향 설정 확인 실패: {preset_asset_path(spec)}")
    save_asset(preset, preset_asset_path(spec))
    return preset, effective


def create_geometry_collection(cube):
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(GEOMETRY_COLLECTION_PATH) and not assets.delete_asset(GEOMETRY_COLLECTION_PATH):
        raise RuntimeError(f"기존 지오메트리 컬렉션 삭제 실패: {GEOMETRY_COLLECTION_PATH}")
    ensure_directory(DESTRUCTION_DIR)
    brick_count = unreal.IntVector(BRICK_COUNT[0], BRICK_COUNT[1], BRICK_COUNT[2])
    collection = unreal.TDFluidFractureEditorLibrary.create_brick_geometry_collection(
        cube, DESTRUCTION_DIR, GEOMETRY_COLLECTION_NAME, brick_count, BRICK_SIZE_CM
    )
    if collection is None:
        raise RuntimeError(f"벽돌 지오메트리 컬렉션 생성 실패: {GEOMETRY_COLLECTION_PATH}")
    save_asset(collection, GEOMETRY_COLLECTION_PATH)
    return collection


def write_report(report):
    report_path = project_path(*REPORT_RELATIVE_PATH)
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, "w", encoding="utf-8") as report_file:
        json.dump(report, report_file, ensure_ascii=False, indent=2, default=str)
    return report_path


def find_missing_saved_files(asset_paths, map_path):
    expected = [asset_disk_path(path, ".uasset") for path in asset_paths]
    expected.append(asset_disk_path(map_path, ".umap"))
    return [path for path in expected if not os.path.isfile(path)]


def configure_live_component(actor, preset, effective):
    live_component = actor.get_editor_property("live_component")
    live_component.set_editor_property("active_preset", preset)
    live_component.set_editor_property("should_init_system_state_from_preset", True)
    live_component.set_editor_property("simulation_config", effective)
    actor.set_editor_property("should_use_activation_volume", False)
    return live_component


def configure_fire_actor(actor, actor_spec, preset, effective, assets_by_name):
    configure_live_component(actor, preset, effective)
    actor.set_editor_property("interaction_brush_channel", unreal.TDFluidBrushChannel.CHANNEL_B_FIRE)
    actor.set_editor_property("should_follow_player_pawn", False)
    actor.set_editor_property("should_harvest_interactions", False)
    kind = getattr(unreal.TDFluidPresetKind, PRESET_KIND_BY_NAME[actor_spec["preset"]])
    emitter = unreal.TDFluidPresetAsset.build_source_emitter_for_preset_kind(kind)
    if emitter.get_editor_property("is_enabled"):
        actor.set_editor_property("source_emitter", emitter)
    actor.set_editor_property("flame_card_layers", unreal.TDFluidPresetAsset.build_flame_card_layers_for_preset_kind(kind))

    light = actor.get_editor_property("fire_light_component")
    light.set_editor_property("is_driven_by_fluid", True)
    light.set_editor_property("max_intensity_candelas", actor_spec["light"]["candelas"])
    light.set_editor_property("reference_density_area_cm2", actor_spec["light"]["reference_area"])
    light.set_editor_property("height_offset_cm", actor_spec["light"]["height_offset"])
    light.set_cast_shadows(actor_spec["light"]["shadows"])

    ember = actor.get_editor_property("ember_component")
    ember.set_editor_property("ember_mesh_asset", assets_by_name["sphere"])
    ember.set_editor_property("ember_material_asset", assets_by_name["ember_material"])
    ember.set_editor_property("max_embers", actor_spec["ember"]["max"])
    ember.set_editor_property("spawn_per_second", actor_spec["ember"]["per_second"])


def configure_smoke_actor(actor, preset, effective, light_actor):
    configure_live_component(actor, preset, effective)
    actor.set_editor_property("should_follow_player_pawn", False)
    actor.set_editor_property("interaction_brush_channel", unreal.TDFluidBrushChannel.CHANNEL_R_MUD)
    actor.set_editor_property("volume_light_actor", light_actor)
    if actor.get_editor_property("volume_light_actor") != light_actor:
        raise RuntimeError(f"볼륨 라이트 액터 지정 확인 실패: {actor.get_actor_label()}")


def make_fragment_target(target_spec, target_actor):
    target = unreal.TDFluidFragmentTarget()
    target.set_editor_property("target_actor", target_actor)
    target.set_editor_property("channel", getattr(unreal.TDFluidBrushChannel, target_spec["channel"]))
    target.set_editor_property("heat_scale", target_spec["heat_scale"])
    target.set_editor_property("radius_scale", target_spec["radius_scale"])
    target.set_editor_property("burst_radius_scale", target_spec["burst_radius_scale"])
    target.set_editor_property("uses_lagged_positions", target_spec["lagged"])
    target.set_editor_property("height_falloff_cm", target_spec["height_falloff"])
    return target


def configure_post_process(volume):
    volume.set_editor_property("unbound", True)
    settings = volume.settings
    for name, value in (
        ("auto_exposure_method", unreal.AutoExposureMethod.AEM_HISTOGRAM),
        ("auto_exposure_min_brightness", EXPOSURE_EV100),
        ("auto_exposure_max_brightness", EXPOSURE_EV100),
        ("auto_exposure_bias", 0.0),
        ("bloom_intensity", 0.5),
        ("vignette_intensity", 0.25),
    ):
        settings.set_editor_property("override_" + name, True)
        settings.set_editor_property(name, value)
    volume.set_editor_property("settings", settings)


def main():
    assets = unreal.EditorAssetLibrary
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    missing_names = find_missing_unreal_names()
    if missing_names:
        raise RuntimeError("언리얼 Python 이름 없음: " + ", ".join(missing_names))
    if editor.get_game_world() is not None:
        raise RuntimeError("PIE를 종료한 뒤 실행하세요.")
    if editor.get_editor_world() is None:
        raise RuntimeError("에디터 월드가 없습니다.")

    contract_problems = check_cpp_contract()
    if contract_problems:
        raise RuntimeError("C++ 계약 이름 없음(재빌드와 에디터 재시작 필요): " + ", ".join(contract_problems))

    overwrite = read_overwrite_request()
    existing_paths = [path for path in collect_guarded_asset_paths() if assets.does_asset_exist(path)]
    if existing_paths and not overwrite:
        print(json.dumps({
            "status": "기존 에셋이 있어 아무것도 바꾸지 않았습니다",
            "existing_assets": existing_paths,
            "overwrite_request_file": project_path(*REQUEST_RELATIVE_PATH),
            "overwrite_request_content": {"overwrite": True},
        }, ensure_ascii=False))
        raise RuntimeError("기존 에셋 보존: overwrite 요청 파일이 없어 중단합니다.")

    unsaved_names = find_unsaved_package_names(overwrite)
    if unsaved_names:
        raise RuntimeError("미저장 작업을 보존합니다: " + ", ".join(unsaved_names))

    game_mode = assets.load_blueprint_class(GAME_MODE_PATH)
    assets_by_name = {
        "cube": assets.load_asset(CUBE_MESH_PATH),
        "plane": assets.load_asset(PLANE_MESH_PATH),
        "sphere": assets.load_asset(SPHERE_MESH_PATH),
        "cylinder": assets.load_asset(CYLINDER_MESH_PATH),
        "ember_material": assets.load_asset(EMBER_INSTANCE_PATH),
        "smoke_volume": assets.load_asset(SMOKE_VOLUME_INSTANCE_PATH),
        "dust_volume": assets.load_asset(DUST_VOLUME_INSTANCE_PATH),
    }
    for scene_name, instance_path in FLAME_INSTANCE_PATHS.items():
        assets_by_name["flame_" + scene_name] = assets.load_asset(instance_path)
    classes = {name: unreal.load_class(None, path) for name, path in CLASS_PATHS.items()}
    required = {"게임 모드": game_mode}
    required.update({f"에셋 {name}": value for name, value in assets_by_name.items()})
    required.update({f"클래스 {name}": value for name, value in classes.items()})
    missing = [name for name, value in required.items() if value is None]
    if missing:
        raise RuntimeError(
            "필수 에셋 또는 클래스 없음(재질은 editor_make_fluid_volume_material.py·editor_make_fluid_flame_material.py 먼저): "
            + ", ".join(missing)
        )

    print("단계: 재질 준비")
    matte_master, matte_created = load_or_create_matte_master()
    floor_material = write_matte_instance(FLOOR_INSTANCE_PATH, matte_master, FLOOR_COLOR, 0.95)
    wood_material = write_matte_instance(WOOD_INSTANCE_PATH, matte_master, WOOD_COLOR, 0.9)
    charred_wood_material = unreal.EditorAssetLibrary.load_asset(CHARRED_WOOD_INSTANCE_PATH) if unreal.EditorAssetLibrary.does_asset_exist(CHARRED_WOOD_INSTANCE_PATH) else None
    if charred_wood_material is not None:
        wood_material = charred_wood_material

    print("단계: 프리셋 생성")
    ensure_directory(PRESET_DIR)
    presets = {}
    effective_configs = {}
    for spec in PRESET_SPECS:
        presets[spec["name"]], effective_configs[spec["name"]] = configure_preset(spec, assets_by_name)

    print("단계: 레벨 비우기")
    ensure_directory(MAP_DIR)
    open_empty_stage_level(levels, actors)

    print("단계: 벽돌 지오메트리 컬렉션")
    collection = create_geometry_collection(assets_by_name["cube"])

    world = editor.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", game_mode)
    created = []

    def finish(actor, label, folder):
        if actor is None:
            raise RuntimeError(f"액터 생성 실패: {label}")
        actor.set_actor_label("TDGen_" + label)
        actor.set_folder_path("TDGen/FluidFX/FireStage/" + folder)
        created.append(actor)
        return actor

    def spawn(actor_class, location, label, folder, rotation=None):
        actor = actors.spawn_actor_from_class(actor_class, unreal.Vector(*location), rotation or unreal.Rotator())
        return finish(actor, label, folder)

    def place_mesh(mesh, location, scale, label, material=None, rotation=None):
        actor = actors.spawn_actor_from_object(mesh, unreal.Vector(*location), rotation or unreal.Rotator())
        finish(actor, label, "Stage")
        actor.set_actor_scale3d(unreal.Vector(*scale))
        component = actor.static_mesh_component
        component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        if material is not None:
            component.set_material(0, material)
        return actor

    place_mesh(assets_by_name["cube"], (0, 1800, -25), (24, 50, 0.5), "FireStageFloor", floor_material)

    sun = spawn(unreal.DirectionalLight, (0, 0, 1500), "FireStageMoon", "Lighting", unreal.Rotator(pitch=-50, yaw=-35))
    sun.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_intensity(MOON_LUX)
    sun.light_component.set_light_color(unreal.LinearColor(*MOON_COLOR, 1.0))
    sky = spawn(unreal.SkyLight, (0, 0, 1000), "FireStageSkyLight", "Lighting")
    sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", True)
    sky.light_component.set_intensity(0.25)
    fog = spawn(unreal.ExponentialHeightFog, (0, 1800, 0), "FireStageFog", "Lighting")
    fog_component = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    fog_component.set_editor_property("fog_density", STAGE_FOG_DENSITY)
    fog_component.set_editor_property("fog_height_falloff", STAGE_FOG_HEIGHT_FALLOFF)
    fog_component.set_editor_property("fog_max_opacity", 1.0)
    fog_component.set_editor_property("start_distance", STAGE_FOG_START_DISTANCE_CM)
    fog_component.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(*STAGE_FOG_LUMINANCE, 1.0))
    fog_component.set_editor_property("enable_volumetric_fog", False)
    configure_post_process(spawn(unreal.PostProcessVolume, (0, 0, 0), "FireStageExposure", "Lighting"))
    spawn(unreal.PlayerStart, PLAYER_START, "FireStagePlayerStart", "Gameplay")

    print("단계: 장면 1 횃불")
    torch_spec = FIRE_ACTOR_SPECS["TorchFire"]
    torch = spawn(classes["TDFluidFireActor"], TORCH_LOCATION, "TorchFire", "Scene1_Torch")
    configure_fire_actor(torch, torch_spec, presets["TorchFlame"], effective_configs["TorchFlame"], assets_by_name)
    torch.set_editor_property("should_follow_player_pawn", True)
    torch.set_editor_property("follow_socket_name", TORCH_SOCKET_NAME)
    torch.set_editor_property("source_prop_mesh_asset", assets_by_name["cylinder"])
    torch.set_editor_property(
        "source_prop_relative_transform",
        unreal.Transform(
            location=unreal.Vector(-25.15, 15.69, -4.63),
            rotation=unreal.Rotator(roll=-98.87, pitch=0.0, yaw=-121.965),
            scale=unreal.Vector(0.04, 0.04, 0.60),
        ),
    )
    torch_source_offset = unreal.Vector(-41.91, 26.15, -7.71)
    torch_emitter = torch.get_editor_property("source_emitter")
    torch_emitter.set_editor_property("source_offset_cm", torch_source_offset)
    torch.set_editor_property("source_emitter", torch_emitter)
    stored_offset = torch.get_editor_property("source_emitter").get_editor_property("source_offset_cm")
    offset_error = max(abs(stored_offset.x - torch_source_offset.x), abs(stored_offset.y - torch_source_offset.y), abs(stored_offset.z - torch_source_offset.z))
    if offset_error > 0.001:
        raise RuntimeError(f"횃불 불씨 오프셋 지정 확인 실패: {stored_offset}")

    print("단계: 장면 2 모닥불")
    for label, location, rotation in (
        ("CampfireLogA", (-18, 1600, 14), unreal.Rotator(roll=90, pitch=0, yaw=20)),
        ("CampfireLogB", (18, 1600, 14), unreal.Rotator(roll=90, pitch=0, yaw=-20)),
        ("CampfireLogC", (0, 1620, 30), unreal.Rotator(roll=90, pitch=0, yaw=90)),
    ):
        place_mesh(assets_by_name["cylinder"], location, (0.18, 0.18, 0.7), label, wood_material, rotation)
    campfire = spawn(classes["TDFluidFireActor"], CAMPFIRE_FIRE_LOCATION, "CampfireFire", "Scene2_Campfire")
    configure_fire_actor(
        campfire, FIRE_ACTOR_SPECS["CampfireFire"], presets["CampfireFlame"], effective_configs["CampfireFlame"], assets_by_name
    )
    campfire_smoke = spawn(classes["TDFluidLiveActor"], CAMPFIRE_SMOKE_LOCATION, "CampfireSmoke", "Scene2_Campfire")
    configure_smoke_actor(campfire_smoke, presets["CampfireSmoke"], effective_configs["CampfireSmoke"], campfire)
    campfire_smoke.set_editor_property("cast_pawn_capsule_shadows", True)
    feed = unreal.TDFluidDownstreamFeedSettings()
    feed.set_editor_property("is_enabled", True)
    campfire.set_editor_property("downstream_smoke_actor", campfire_smoke)
    campfire.set_editor_property("downstream_feed", feed)

    print("단계: 장면 3 폭발")
    breakable = spawn(classes["TDBreakableActor"], EXPLOSION_BLOCK_LOCATION, "ExplosionBlock", "Scene3_Explosion")
    geometry_component = breakable.get_component_by_class(unreal.GeometryCollectionComponent)
    if geometry_component is None:
        raise RuntimeError("파괴 액터에 지오메트리 컬렉션 컴포넌트가 없습니다.")
    geometry_component.set_rest_collection(collection)
    breakable.set_editor_property("break_force", BREAK_FORCE)
    breakable.set_editor_property("break_force_radius", BREAK_FORCE_RADIUS)
    explosion_fire = spawn(classes["TDFluidFireActor"], EXPLOSION_BLOCK_LOCATION, "ExplosionFire", "Scene3_Explosion")
    configure_fire_actor(
        explosion_fire,
        FIRE_ACTOR_SPECS["ExplosionFire"],
        presets["ExplosionFire"],
        effective_configs["ExplosionFire"],
        assets_by_name,
    )
    explosion_smoke = spawn(classes["TDFluidLiveActor"], EXPLOSION_BLOCK_LOCATION, "ExplosionSmoke", "Scene3_Explosion")
    configure_smoke_actor(explosion_smoke, presets["ExplosionSmoke"], effective_configs["ExplosionSmoke"], explosion_fire)
    explosion_dust = spawn(classes["TDFluidLiveActor"], EXPLOSION_DUST_LOCATION, "ExplosionGroundDust", "Scene3_Explosion")
    configure_smoke_actor(explosion_dust, presets["ExplosionGroundDust"], effective_configs["ExplosionGroundDust"], explosion_fire)
    fragment_emitter = spawn(classes["TDFluidFragmentEmitterActor"], EXPLOSION_BLOCK_LOCATION, "ExplosionFragments", "Scene3_Explosion")
    fragment_emitter.set_editor_property("source_breakable_actor", breakable)
    fragment_emitter.set_editor_property("fire_target_actor", explosion_fire)
    fragment_emitter.set_editor_property("smoke_target_actor", explosion_smoke)
    fragment_emitter.set_editor_property("trail_settings", unreal.TDFluidPresetAsset.build_fragment_trail_for_preset_kind(unreal.TDFluidPresetKind.EXPLOSION))
    explosion_target_actors = {"fire": explosion_fire, "smoke": explosion_smoke, "dust": explosion_dust}
    fragment_emitter.set_editor_property("targets", [make_fragment_target(target_spec, explosion_target_actors[target_spec["actor"]]) for target_spec in EXPLOSION_TARGET_SPECS])
    if len(fragment_emitter.get_editor_property("targets")) != len(EXPLOSION_TARGET_SPECS):
        raise RuntimeError("조각 방출 액터의 대상 배열 지정 확인 실패")
    if fragment_emitter.get_editor_property("fire_target_actor") != explosion_fire:
        raise RuntimeError("조각 방출 액터의 불 대상 지정 확인 실패")

    navigation = spawn(unreal.NavMeshBoundsVolume, NAVIGATION_CENTER, "FireStageNavigation", "Gameplay")
    navigation.set_actor_scale3d(unreal.Vector(*NAVIGATION_SCALE))
    nav_origin, nav_extent = navigation.get_actor_bounds(False)
    if nav_extent.x < NAVIGATION_MIN_EXTENT_CM[0] or nav_extent.y < NAVIGATION_MIN_EXTENT_CM[1]:
        raise RuntimeError(f"내비게이션 브러시 범위 검증 실패: {nav_extent}")
    for nav_mesh in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RecastNavMesh):
        nav_mesh.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC)
        nav_mesh.set_editor_property("is_world_partitioned", False)

    print("단계: 레벨 저장")
    if not levels.save_current_level():
        raise RuntimeError(f"무대 레벨 저장 실패: {STAGE_MAP_PATH}")

    saved_asset_paths = [MATTE_MASTER_PATH, FLOOR_INSTANCE_PATH, WOOD_INSTANCE_PATH, GEOMETRY_COLLECTION_PATH] + [preset_asset_path(spec) for spec in PRESET_SPECS]
    missing_files = find_missing_saved_files(saved_asset_paths, STAGE_MAP_PATH)
    if missing_files:
        raise RuntimeError("저장 파일 확인 실패: " + ", ".join(missing_files))

    report = {
        "status": "ok",
        "map": STAGE_MAP_PATH,
        "overwrite": overwrite,
        "created_matte_master": matte_created,
        "geometry_collection": GEOMETRY_COLLECTION_PATH,
        "brick_count": list(BRICK_COUNT),
        "brick_size_cm": BRICK_SIZE_CM,
        "presets": {
            spec["name"]: {
                "asset": preset_asset_name(spec),
                "grid_resolution": effective_configs[spec["name"]].get_editor_property("grid_resolution"),
                "buoyancy_cm_per_sec2": effective_configs[spec["name"]].get_editor_property("buoyancy_cm_per_sec2"),
            }
            for spec in PRESET_SPECS
        },
        "scene_actors": {
            "torch": torch.get_actor_label(),
            "campfire_fire": campfire.get_actor_label(),
            "campfire_smoke": campfire_smoke.get_actor_label(),
            "explosion_block": breakable.get_actor_label(),
            "explosion_fire": explosion_fire.get_actor_label(),
            "explosion_smoke": explosion_smoke.get_actor_label(),
            "fragment_emitter": fragment_emitter.get_actor_label(),
        },
        "torch_socket": TORCH_SOCKET_NAME,
        "explosion_hit_offset": list(EXPLOSION_HIT_OFFSET),
        "exposure_ev100": EXPOSURE_EV100,
        "navigation_extent": [nav_extent.x, nav_extent.y, nav_extent.z],
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
