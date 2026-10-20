"""실행 환경: TDGame 언리얼 에디터 안에서 FluidFX 물 무대 LV_TDFluidWaterStage 를 만든다. 장면 0(호수 분지 0~220cm, 물가 완경사, 잠긴 바위 14개(DarkFantasyTopDown), 물결 모래 재질, 역광 해(반짝임), 수면은 100cm 192칸 격자, 프리셋 DA_TDFluid_WaterLook0)과 장면 1(강: 폭 8m·깊이 1.2m·기울기 0.5%·굽이 1개, 폭 절반 여울 구간 수심 40cm, 바위 3개 중 1개 잠김+TDFluidObstacle 태그, 프리셋 DA_TDFluid_River). 먼저 editor_make_fluid_water_material.py 가 성공해야 하고, 장면 1 은 C++ 묶음 B1(ATDFluidWaterActor·ATDFluidRiverActor) 재빌드와 에디터 재시작 뒤에만 만든다.
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_water_stage.py (요청 파일 Saved/FluidFX/fluid_water_stage_request.json 의 {"overwrite": true, "scenes": [0, 1]}, B1 전이면 scenes 를 [0] 으로)
출력: /Game/FluidFX/Presets/DA_TDFluid_WaterLook0·DA_TDFluid_River, /Game/FluidFX/Materials/M_TD_FluidWaterMatte·M_TD_FluidWaterSandRipple·MI_TD_FluidWaterSand·MI_TD_FluidWaterStone, /Game/FluidFX/Maps/LV_TDFluidWaterStage, Saved/FluidFX/water_stage/heightmap.r16, Saved/FluidFX/fluid_water_stage.json(강 배치·탐침 좌표 포함, pie_probe_fluid_water.py 가 읽는다)
상태: 장면 0 은 실행 검증됨, 장면 1 은 미검증(C1 작성분, B1 C++ 계약은 명세 이름으로 가정하고 첫머리에서 점검). 에셋·맵이 이미 있으면 요청 파일에 overwrite true 가 있을 때만 갱신하고(맵은 지우지 않고 열어서 액터만 비움), C++ 계약 속성이 하나라도 없거나 미저장 작업이 있으면 중단한다. W-2r: 모래 젖은 띠의 수위를 호수 반경(LakeWetReachCm) 밖에서는 강 수면 고도(RiverLevelStartXCm·RiverLevelSlopePerCm·RiverLevelStartZCm, 강 시작~끝 평균 경사)로 바꿔 하류 강둑 위에 뜨던 Z=0 띠를 없앤다(무대 다시 짓기 필요).
"""
import array
import json
import math
import os
import random

import unreal


FLUID_ROOT = "/Game/FluidFX"
MATERIAL_DIR = f"{FLUID_ROOT}/Materials"
PRESET_DIR = f"{FLUID_ROOT}/Presets"
MAP_DIR = f"{FLUID_ROOT}/Maps"
MESH_DIR = f"{FLUID_ROOT}/Meshes"

STAGE_MAP_PATH = f"{MAP_DIR}/LV_TDFluidWaterStage"
PRESET_NAME = "DA_TDFluid_WaterLook0"
PRESET_PATH = f"{PRESET_DIR}/{PRESET_NAME}"
RIVER_PRESET_NAME = "DA_TDFluid_River"
RIVER_PRESET_PATH = f"{PRESET_DIR}/{RIVER_PRESET_NAME}"
MATTE_MASTER_PATH = f"{MATERIAL_DIR}/M_TD_FluidWaterMatte"
SAND_INSTANCE_PATH = f"{MATERIAL_DIR}/MI_TD_FluidWaterSand"
STONE_INSTANCE_PATH = f"{MATERIAL_DIR}/MI_TD_FluidWaterStone"
LAKE_INSTANCE_PATH = f"{MATERIAL_DIR}/MI_TD_FluidWater_Lake"
RIVER_INSTANCE_PATH = f"{MATERIAL_DIR}/MI_TD_FluidWater_River"
SURFACE_MESH_PATH = "/Game/FluidFX/Meshes/SM_TD_FluidWaterGridUnit"
RIBBON_MESH_PATH = "/Engine/BasicShapes/Plane"
SAND_MASTER_PATH = f"{MATERIAL_DIR}/M_TD_FluidWaterSandRipple"
SAND_DETAIL_TEXTURE_PATH = "/Game/FluidFX/Textures/T_TD_FluidSandDetail"
SAND_NORMAL_TEXTURE_PATH = "/Game/FluidFX/Textures/T_TD_FluidSandRipplesN"
SAND_TILING_CM = 140.0
SAND_PATCH_TILING_CM = 1100.0
SAND_WET_BAND_CM = 14.0
SAND_WET_DARKENING = 0.55
SAND_WET_ROUGHNESS = 0.3
SAND_WET_REGION_BLEND_CM = 300.0
LAKE_ROCK_MESH_PATHS = (
    "/Game/DarkFantasyTopDown/StaticMeshes/Rocks2/SM_Rock1",
    "/Game/DarkFantasyTopDown/StaticMeshes/Rocks2/SM_Rock2",
    "/Game/DarkFantasyTopDown/StaticMeshes/Rocks2/SM_Rock3",
    "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Rocks/SmallRocks/SM_Rock_4",
    "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Rocks/SmallRocks/SM_Rock_6",
    "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Rocks/SmallRocks/SM_Rock_8",
)
LAKE_ROCK_SIZE_CM = (50.0, 170.0)
LAKE_ROCK_SINK_RATIO = 0.3
SUN_ROTATION = (-40.0, 160.0)
GAME_MODE_PATH = "/Game/Combat/Blueprints/BP_TDCombatGameMode"
SPHERE_MESH_PATH = "/Engine/BasicShapes/Sphere"

REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_water_stage_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_water_stage.json")
HEIGHTMAP_RELATIVE_PATH = ("Saved", "FluidFX", "water_stage", "heightmap.r16")

ALL_SCENES = (0, 1)

LANDSCAPE_COMPONENTS = 8
LANDSCAPE_QUADS_PER_SECTION = 63
LANDSCAPE_XY_SCALE_CM = 25.0
LANDSCAPE_Z_SCALE = 20.0
HEIGHT_UNITS_PER_CM = 128.0 / LANDSCAPE_Z_SCALE
HEIGHT_ZERO_VALUE = 32768
LANDSCAPE_HALF_SPAN_CM = 0.5 * LANDSCAPE_COMPONENTS * LANDSCAPE_QUADS_PER_SECTION * LANDSCAPE_XY_SCALE_CM

WATER_SURFACE_Z_CM = 0.0
LAKE_RADIUS_CM = 1500.0
LAKE_DEPTH_CM = 220.0
LAKE_BED_EXPONENT = 1.6
BANK_HEIGHT_CM = 40.0
BANK_WIDTH_CM = 500.0
BED_NOISE_CM = 2.5

SAND_COLOR = (0.55, 0.45, 0.30)
STONE_COLOR = (0.30, 0.29, 0.27)
STONE_COUNT = 14
STONE_RING_CM = (300.0, 1450.0)
STONE_RANDOM_SEED = 20261018
PLAYER_START_XY = (-2600.0, 0.0)
PLAYER_START_CLEARANCE_CM = 100.0
NAVIGATION_SCALE = (40.0, 40.0, 8.0)
NAVIGATION_MIN_EXTENT_CM = (3500.0, 3500.0)
EXPOSURE_EV100 = 2.0
SUN_LUX = 10.0

RIVER_X_RANGE_CM = (-6200.0, 6200.0)
RIVER_BASE_Y_CM = 3400.0
RIVER_BEND_START_X_CM = -3600.0
RIVER_BEND_LENGTH_CM = 4800.0
RIVER_BEND_AMPLITUDE_CM = 700.0
RIVER_WIDTH_CM = 800.0
RIVER_DEPTH_CM = 120.0
RIVER_DROP_PER_CM = 0.005
RIVER_START_SURFACE_Z_CM = BANK_HEIGHT_CM - 40.0
RIVER_FREEBOARD_CM = 40.0
RIVER_BANK_RUN_CM = 150.0
RIVER_INFLUENCE_CM = (700.0, 1400.0)
RIVER_ARC_STEP_CM = 25.0
RIVER_SPLINE_SPACING_CM = 300.0
RIVER_FLOW_SPEED_CM_PER_SEC = 250.0
RIVER_GUIDE_SAMPLE_SPACING_CM = 50.0
RIVER_RIBBON_SEGMENT_LENGTH_CM = 400.0
RIFFLE_X_CM = (3200.0, 3900.0, 4800.0, 5500.0)
RIFFLE_WIDTH_SCALE = 0.5
RIFFLE_DEPTH_CM = 40.0
RIVER_NAVIGATION_SCALE = (63.0, 18.0, 8.0)
RIVER_NAVIGATION_CENTER_Y_CM = RIVER_BASE_Y_CM + 350.0
RIVER_NAVIGATION_MIN_EXTENT_CM = (6000.0, 1700.0)

ROCK_ABOVE_WATER_CM = 35.0
ROCK_SUBMERGED_BELOW_SURFACE_CM = 20.0
ROCK_EMBED_CM = 15.0
ROCK_SPECS = (
    {"name": "RockPrimary", "kind": "above_water", "x_cm": -4200.0, "lateral_cm": 0.0, "scale_xy": (1.6, 1.4), "yaw_degrees": 0.0},
    {"name": "RockSide", "kind": "above_water", "x_cm": -2400.0, "lateral_cm": 150.0, "scale_xy": (1.2, 1.1), "yaw_degrees": 35.0},
    {"name": "RockSubmerged", "kind": "submerged", "x_cm": -1000.0, "lateral_cm": 0.0, "scale_xy": (1.4, 1.2), "yaw_degrees": None},
)

WINDOW_HALF_CM = 1800.0
WINDOW_FADE_CM = 360.0
WINDOW_LOOK_AHEAD_CM = 190.0
WINDOW_CORE_HALF_CM = WINDOW_HALF_CM - WINDOW_FADE_CM - WINDOW_LOOK_AHEAD_CM
FLOW_PAWN_XY_CM = (3350.0, 2500.0)
WIDE_PROBE_X_CM = (2300.0, 2600.0, 2900.0)
RIFFLE_PROBE_X_CM = (4100.0, 4300.0, 4500.0)
BANK_PROBE_X_CM = 4300.0
BANK_WET_OFFSET_CM = 25.0
BANK_DRY_MID_OFFSET_CM = 120.0
BANK_DRY_FAR_OFFSET_CM = 500.0
CROSSING_X_CM = 4350.0
CROSSING_HALF_SPAN_CM = 600.0
CROSSING_MAX_SLOPE = 0.7
FOAM_BOX_S_CM = (-400.0, 1500.0)
FOAM_BOX_HALF_WIDTH_CM = 300.0
FOAM_BOX_STEP_CM = 50.0
ROCK_PAWN_FORWARD_CM = 200.0
ROCK_PAWN_SOUTH_CM = 900.0
ASLEEP_PAWN_XY_CM = (-5800.0, -5600.0)

TAG_WATER_SURFACE = "TDFluidWaterSurface"
TAG_WATER_REGION = "TDFluidWaterRegion"
TAG_OBSTACLE = "TDFluidObstacle"

PRESET_CONFIG = {
    "grid_resolution": 192,
    "sim_extent_cm": (3600.0, 3600.0),
    "velocity_damping": 0.02,
    "density_dissipation": 0.35,
    "pressure_iterations": 8,
    "vorticity_strength": 0.3,
    "terrain_slope_gravity": 0.0,
    "advection_speed_scale": 1.0,
    "border_fade_ratio": 0.1,
    "max_points_per_tick": 96,
}

ENUM_CONTRACTS = {
    "TDFluidPlaneOrientation": ("WORLD_FACING",),
    "TDFluidPresetKind": ("CUSTOM", "WATER"),
    "TDFluidBrushChannel": ("CHANNEL_A_WATER",),
}
STRUCT_CONTRACTS = {
    "TDFluidSimulationConfig": tuple(name for name in PRESET_CONFIG if name != "sim_extent_cm") + ("sim_extent_cm",),
    "TDFluidPlaneSettings": ("orientation", "camera_depth_bias_cm", "interaction_depth_cm", "center_offset_along_up_ratio"),
    "TDLandscapeCreateRequest": (
        "location",
        "scale",
        "component_count_x",
        "component_count_y",
        "sections_per_component",
        "quads_per_section",
        "heightmap_raw_path",
        "layer_names",
        "layer_raw_paths",
        "material",
        "world_partition_grid_size_in_components",
        "enable_nanite",
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
        "plane_settings",
        "translucency_sort_priority",
    ),
    "TDFluidLiveActor": (
        "should_follow_player_pawn",
        "should_use_activation_volume",
        "interaction_brush_channel",
        "live_component",
        "internal_surface_mesh",
        "sample_terrain_elevation",
    ),
}
FUNCTION_CONTRACTS = {
    "TDFluidLiveComponent": ("set_simulation_awake",),
    "TDLandscapeEditorLibrary": ("create_landscape_from_raw_files",),
}
RIVER_ENUM_CONTRACTS = {"TDFluidPresetKind": ("RIVER",)}
RIVER_STRUCT_CONTRACTS = {
    "TDFluidSimulationConfig": (
        "enable_water_layer",
        "open_water_borders",
        "treat_dry_cells_as_solid",
        "water_depth_reference_cm",
        "flow_guide_response_per_sec",
        "flow_guide_bank_falloff_cm",
        "flow_guide_bank_speed_ratio",
        "rapids_whitewater_per_sec",
        "whitewater_from_vorticity",
        "ripple_wave_speed_cm_per_sec",
        "ripple_damping_per_sec",
        "ripple_height_reference_cm",
        "ripple_flow_advection",
        "actor_brush_ripple_impulse_cm",
        "bank_wet_height_cm",
    ),
    "TDFluidCellSample": (
        "water_depth_cm",
        "wetness",
        "flow_guide_weight",
        "flow_target_velocity",
        "ripple_height_cm",
        "is_terrain_known",
    ),
}
RIVER_OBJECT_CONTRACTS = {
    "TDFluidWaterActor": (
        "water_surface_actor_tag",
        "water_region_actor_tag",
        "water_obstacle_actor_tag",
        "terrain_trace_ignore_tag",
        "sleep_when_no_water_in_window",
        "sleep_delay_seconds",
        "view_look_ahead_cm",
        "deep_water_fallback_depth_cm",
        "water_layer_fade_in_seconds",
        "should_follow_player_pawn",
        "sample_terrain_elevation",
        "live_component",
    ),
    "TDFluidRiverActor": (
        "river_spline",
        "river_width_cm",
        "flow_speed_cm_per_sec",
        "conserve_discharge_across_width",
        "guide_sample_spacing_cm",
        "build_ribbon_surface",
        "ribbon_mesh",
        "ribbon_material",
        "ribbon_segment_length_cm",
    ),
}
RIVER_FUNCTION_CONTRACTS = {
    "TDFluidWaterActor": ("get_driven_surface_count", "get_shared_surface_material_count", "get_water_layer_display_alpha"),
    "TDFluidRiverActor": ("build_flow_guide",),
    "TDFluidLiveComponent": ("get_water_surface_texture",),
}
CLASS_PATHS = {
    "TDFluidLiveActor": "/Script/TDGame.TDFluidLiveActor",
    "TDFluidWaterActor": "/Script/TDGame.TDFluidWaterActor",
    "TDFluidRiverActor": "/Script/TDGame.TDFluidRiverActor",
}

REQUIRED_UNREAL_NAMES = (
    "EditorAssetLibrary.load_blueprint_class",
    "EditorAssetLibrary.load_asset",
    "MaterialEditingLibrary.recompile_material",
    "EditorLoadingAndSavingUtils.get_dirty_map_packages",
    "EditorLoadingAndSavingUtils.get_dirty_content_packages",
    "SystemLibrary.get_project_directory",
    "GameplayStatics.get_all_actors_of_class",
    "Material",
    "MaterialFactoryNew",
    "DataAssetFactory",
    "MaterialExpressionConstant",
    "MaterialExpressionLength",
    "MaterialExpressionComponentMask",
    "MaterialExpressionLinearInterpolate",
    "MaterialProperty.MP_BASE_COLOR",
    "MaterialProperty.MP_ROUGHNESS",
    "MaterialProperty.MP_METALLIC",
    "TDFluidPresetAsset.build_config_for_preset_kind",
    "AutoExposureMethod.AEM_HISTOGRAM",
    "RuntimeGenerationType.DYNAMIC",
    "ComponentMobility.STATIC",
    "ComponentMobility.MOVABLE",
    "NavMeshBoundsVolume",
    "RecastNavMesh",
    "DirectionalLight",
    "SkyLight",
    "SkyAtmosphere",
    "PostProcessVolume",
    "PlayerStart",
)
RIVER_REQUIRED_UNREAL_NAMES = (
    "Name",
    "SplineComponent",
    "SplineCoordinateSpace.WORLD",
    "SplinePointType.CURVE",
)


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


def asset_disk_path(path, extension):
    return project_path("Content", *path[len("/Game/"):].split("/")) + extension


def find_missing_unreal_names(scenes):
    names = REQUIRED_UNREAL_NAMES + (RIVER_REQUIRED_UNREAL_NAMES if 1 in scenes else ())
    missing = []
    for dotted_name in names:
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


def merge_contracts(base, extra):
    merged = {name: tuple(values) for name, values in base.items()}
    for name, values in extra.items():
        merged[name] = merged.get(name, ()) + tuple(values)
    return merged


def check_cpp_contract(scenes):
    enum_contracts, struct_contracts = ENUM_CONTRACTS, STRUCT_CONTRACTS
    object_contracts, function_contracts = OBJECT_CONTRACTS, FUNCTION_CONTRACTS
    if 1 in scenes:
        enum_contracts = merge_contracts(enum_contracts, RIVER_ENUM_CONTRACTS)
        struct_contracts = merge_contracts(struct_contracts, RIVER_STRUCT_CONTRACTS)
        object_contracts = merge_contracts(object_contracts, RIVER_OBJECT_CONTRACTS)
        function_contracts = merge_contracts(function_contracts, RIVER_FUNCTION_CONTRACTS)

    problems = []
    for enum_name, value_names in enum_contracts.items():
        enum_type = getattr(unreal, enum_name, None)
        if enum_type is None:
            problems.append(enum_name)
            continue
        problems.extend(f"{enum_name}.{name}" for name in find_missing_members(enum_type, value_names))

    for struct_name, property_names in struct_contracts.items():
        struct_type = getattr(unreal, struct_name, None)
        if struct_type is None:
            problems.append(struct_name)
            continue
        problems.extend(f"{struct_name}.{name}" for name in find_missing_properties(struct_type(), property_names))

    for class_name, property_names in object_contracts.items():
        class_type = getattr(unreal, class_name, None)
        if class_type is None:
            problems.append(class_name)
            continue
        problems.extend(f"{class_name}.{name}" for name in find_missing_properties(unreal.get_default_object(class_type), property_names))

    for class_name, function_names in function_contracts.items():
        class_type = getattr(unreal, class_name, None)
        if class_type is None:
            problems.append(class_name)
            continue
        problems.extend(f"{class_name}.{name}()" for name in find_missing_members(class_type, function_names))
    return problems


def read_request():
    request_path = project_path(*REQUEST_RELATIVE_PATH)
    if not os.path.isfile(request_path):
        return {}
    with open(request_path, encoding="utf-8") as request_file:
        request = json.load(request_file)
    return request if isinstance(request, dict) else {}


def read_requested_scenes(request):
    scenes = request.get("scenes", list(ALL_SCENES))
    if not isinstance(scenes, list) or not scenes or any(item not in ALL_SCENES for item in scenes):
        raise RuntimeError(f"요청 파일 scenes 는 {list(ALL_SCENES)} 의 부분집합이어야 합니다: {scenes}")
    if 0 not in scenes:
        raise RuntimeError("장면 0 은 지형·조명 공용이라 항상 포함해야 합니다.")
    return tuple(sorted(set(scenes)))


def collect_guarded_asset_paths(scenes):
    paths = [PRESET_PATH, MATTE_MASTER_PATH, SAND_MASTER_PATH, SAND_INSTANCE_PATH, STONE_INSTANCE_PATH, STAGE_MAP_PATH]
    if 1 in scenes:
        paths.append(RIVER_PRESET_PATH)
    return paths


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
    ensure_directory(MATERIAL_DIR)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_TD_FluidWaterMatte", MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew()
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
    roughness = add_node(material, unreal.MaterialExpressionScalarParameter, -280, 20, parameter_name="Roughness", default_value=0.9)
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


def link_nodes(source, source_output, target, target_input):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(source, source_output, target, target_input):
        raise RuntimeError(f"재질 노드 연결 실패: {source_output} -> {target_input}")


def build_sand_master():
    assets = unreal.EditorAssetLibrary
    detail_texture = assets.load_asset(SAND_DETAIL_TEXTURE_PATH)
    normal_texture = assets.load_asset(SAND_NORMAL_TEXTURE_PATH)
    if detail_texture is None or normal_texture is None:
        raise RuntimeError("모래 텍스처 없음(make_fluid_water_textures.py 와 editor_make_fluid_water_material.py 먼저)")
    if assets.does_asset_exist(SAND_MASTER_PATH):
        material = assets.load_asset(SAND_MASTER_PATH)
        unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
    else:
        ensure_directory(MATERIAL_DIR)
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            SAND_MASTER_PATH.rsplit("/", 1)[1], MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew()
        )
    if material is None:
        raise RuntimeError(f"재질 생성 실패: {SAND_MASTER_PATH}")
    world_position = add_node(material, unreal.MaterialExpressionWorldPosition, -1000, 0)
    planar_position = add_node(material, unreal.MaterialExpressionComponentMask, -850, 0, r=True, g=True, b=False, a=False)
    link_nodes(world_position, "", planar_position, "")
    tiling = add_node(material, unreal.MaterialExpressionScalarParameter, -850, 140, parameter_name="SandTilingCm", default_value=SAND_TILING_CM)
    sand_uv = add_node(material, unreal.MaterialExpressionDivide, -700, 40)
    link_nodes(planar_position, "", sand_uv, "A")
    link_nodes(tiling, "", sand_uv, "B")
    patch_tiling = add_node(material, unreal.MaterialExpressionScalarParameter, -850, -140, parameter_name="SandPatchTilingCm", default_value=SAND_PATCH_TILING_CM)
    patch_uv = add_node(material, unreal.MaterialExpressionDivide, -700, -100)
    link_nodes(planar_position, "", patch_uv, "A")
    link_nodes(patch_tiling, "", patch_uv, "B")
    detail = add_node(
        material, unreal.MaterialExpressionTextureSampleParameter2D, -500, -160,
        parameter_name="SandDetailTexture", texture=detail_texture, sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR,
    )
    ripple_normal = add_node(
        material, unreal.MaterialExpressionTextureSampleParameter2D, -500, 160,
        parameter_name="SandNormalTexture", texture=normal_texture, sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
    )
    fine_detail = add_node(
        material, unreal.MaterialExpressionTextureSampleParameter2D, -500, 0,
        parameter_name="SandDetailTexture", texture=detail_texture, sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR,
    )
    link_nodes(patch_uv, "", detail, "UVs")
    link_nodes(sand_uv, "", fine_detail, "UVs")
    link_nodes(sand_uv, "", ripple_normal, "UVs")
    color_variation = add_node(material, unreal.MaterialExpressionLinearInterpolate, -250, -220, const_a=0.78, const_b=1.12)
    link_nodes(detail, "R", color_variation, "Alpha")
    grain_variation = add_node(material, unreal.MaterialExpressionLinearInterpolate, -250, -80, const_a=0.92, const_b=1.06)
    link_nodes(fine_detail, "B", grain_variation, "Alpha")
    base_color = add_node(
        material, unreal.MaterialExpressionVectorParameter, -250, -360,
        parameter_name="BaseColor", default_value=unreal.LinearColor(SAND_COLOR[0], SAND_COLOR[1], SAND_COLOR[2], 1.0),
    )
    tinted = add_node(material, unreal.MaterialExpressionMultiply, -60, -260)
    link_nodes(base_color, "", tinted, "A")
    link_nodes(color_variation, "", tinted, "B")
    final_color = add_node(material, unreal.MaterialExpressionMultiply, 80, -180)
    link_nodes(tinted, "", final_color, "A")
    link_nodes(grain_variation, "", final_color, "B")
    roughness = add_node(material, unreal.MaterialExpressionScalarParameter, -60, 40, parameter_name="Roughness", default_value=0.9)
    height_above_water = add_node(material, unreal.MaterialExpressionComponentMask, -850, 300, r=False, g=False, b=True, a=False)
    link_nodes(world_position, "", height_above_water, "")
    water_level = add_node(material, unreal.MaterialExpressionScalarParameter, -850, 420, parameter_name="WaterLevelZCm", default_value=WATER_SURFACE_Z_CM)
    relative_height = add_node(material, unreal.MaterialExpressionSubtract, -700, 340)
    link_nodes(height_above_water, "", relative_height, "A")
    river_x = add_node(material, unreal.MaterialExpressionComponentMask, -1000, 520, r=True, g=False, b=False, a=False)
    link_nodes(world_position, "", river_x, "")
    river_start_x = add_node(material, unreal.MaterialExpressionScalarParameter, -1000, 600, parameter_name="RiverLevelStartXCm", default_value=RIVER_X_RANGE_CM[0])
    river_run = add_node(material, unreal.MaterialExpressionSubtract, -900, 540)
    link_nodes(river_x, "", river_run, "A")
    link_nodes(river_start_x, "", river_run, "B")
    river_slope = add_node(material, unreal.MaterialExpressionScalarParameter, -1000, 680, parameter_name="RiverLevelSlopePerCm", default_value=compute_river_level_slope_per_cm())
    river_change = add_node(material, unreal.MaterialExpressionMultiply, -820, 560)
    link_nodes(river_run, "", river_change, "A")
    link_nodes(river_slope, "", river_change, "B")
    river_start_z = add_node(material, unreal.MaterialExpressionScalarParameter, -1000, 760, parameter_name="RiverLevelStartZCm", default_value=RIVER_START_SURFACE_Z_CM)
    river_level = add_node(material, unreal.MaterialExpressionAdd, -740, 580)
    link_nodes(river_change, "", river_level, "A")
    link_nodes(river_start_z, "", river_level, "B")
    lake_distance = add_node(material, unreal.MaterialExpressionLength, -1000, 840)
    link_nodes(planar_position, "", lake_distance, "")
    lake_reach = add_node(material, unreal.MaterialExpressionScalarParameter, -1000, 920, parameter_name="LakeWetReachCm", default_value=LAKE_RADIUS_CM + BANK_WIDTH_CM)
    beyond_lake = add_node(material, unreal.MaterialExpressionSubtract, -900, 860)
    link_nodes(lake_distance, "", beyond_lake, "A")
    link_nodes(lake_reach, "", beyond_lake, "B")
    region_ramp = add_node(material, unreal.MaterialExpressionDivide, -820, 880, const_b=SAND_WET_REGION_BLEND_CM)
    link_nodes(beyond_lake, "", region_ramp, "A")
    river_region = add_node(material, unreal.MaterialExpressionSaturate, -740, 880)
    link_nodes(region_ramp, "", river_region, "")
    local_water_level = add_node(material, unreal.MaterialExpressionLinearInterpolate, -760, 460)
    link_nodes(water_level, "", local_water_level, "A")
    link_nodes(river_level, "", local_water_level, "B")
    link_nodes(river_region, "", local_water_level, "Alpha")
    link_nodes(local_water_level, "", relative_height, "B")
    wet_band = add_node(material, unreal.MaterialExpressionScalarParameter, -700, 460, parameter_name="WetBandHeightCm", default_value=SAND_WET_BAND_CM)
    distance_from_waterline = add_node(material, unreal.MaterialExpressionAbs, -620, 300)
    link_nodes(relative_height, "", distance_from_waterline, "")
    dry_ratio = add_node(material, unreal.MaterialExpressionDivide, -560, 360)
    link_nodes(distance_from_waterline, "", dry_ratio, "A")
    link_nodes(wet_band, "", dry_ratio, "B")
    dryness = add_node(material, unreal.MaterialExpressionSaturate, -440, 360)
    link_nodes(dry_ratio, "", dryness, "")
    wet_darkening = add_node(material, unreal.MaterialExpressionLinearInterpolate, -300, 300, const_a=SAND_WET_DARKENING, const_b=1.0)
    link_nodes(dryness, "", wet_darkening, "Alpha")
    wet_color = add_node(material, unreal.MaterialExpressionMultiply, 200, -120)
    link_nodes(final_color, "", wet_color, "A")
    link_nodes(wet_darkening, "", wet_color, "B")
    wet_roughness = add_node(material, unreal.MaterialExpressionLinearInterpolate, 80, 100, const_a=SAND_WET_ROUGHNESS)
    link_nodes(roughness, "", wet_roughness, "B")
    link_nodes(dryness, "", wet_roughness, "Alpha")
    connect_material_output(wet_color, unreal.MaterialProperty.MP_BASE_COLOR)
    connect_material_output(wet_roughness, unreal.MaterialProperty.MP_ROUGHNESS)
    connect_material_output(ripple_normal, unreal.MaterialProperty.MP_NORMAL)
    errors = [str(item) for item in unreal.MaterialEditingLibrary.recompile_material(material) or []]
    if errors:
        raise RuntimeError(f"재질 컴파일 실패: {SAND_MASTER_PATH}, {errors}")
    save_asset(material, SAND_MASTER_PATH)
    return material


def compute_river_level_slope_per_cm():
    low_x, high_x = RIVER_X_RANGE_CM
    return (river_surface_z_cm(high_x) - river_surface_z_cm(low_x)) / (high_x - low_x)


def spawn_lake_rocks(actors, finish, generator):
    rock_meshes = [unreal.EditorAssetLibrary.load_asset(path) for path in LAKE_ROCK_MESH_PATHS]
    missing = [path for path, mesh in zip(LAKE_ROCK_MESH_PATHS, rock_meshes) if mesh is None]
    if missing:
        raise RuntimeError("호수 바위 메시 없음: " + ", ".join(missing))
    records = []
    for index in range(STONE_COUNT):
        angle = generator.uniform(0.0, 2.0 * math.pi)
        radius = generator.uniform(STONE_RING_CM[0], STONE_RING_CM[1])
        x_cm, y_cm = radius * math.cos(angle), radius * math.sin(angle)
        mesh = rock_meshes[index % len(rock_meshes)]
        extent = mesh.get_bounds().box_extent
        scale = generator.uniform(LAKE_ROCK_SIZE_CM[0], LAKE_ROCK_SIZE_CM[1]) / max(2.0 * max(extent.x, extent.y), 1.0)
        bed_z = compute_lake_terrain_height_cm(x_cm, y_cm)
        z_cm = bed_z - LAKE_ROCK_SINK_RATIO * 2.0 * extent.z * scale
        rotation = unreal.Rotator(pitch=generator.uniform(-12.0, 12.0), yaw=generator.uniform(0.0, 360.0), roll=generator.uniform(-12.0, 12.0))
        rock = actors.spawn_actor_from_object(mesh, unreal.Vector(x_cm, y_cm, z_cm), rotation)
        finish(rock, f"WaterStageRock{index}", "Stage")
        rock.set_actor_scale3d(unreal.Vector(scale, scale, scale))
        rock.static_mesh_component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        records.append({"mesh": mesh.get_name(), "location": [round(x_cm, 1), round(y_cm, 1), round(z_cm, 1)], "scale": round(scale, 3), "bed_depth_cm": round(-bed_z, 1)})
    return records


def smoothstep(low, high, value):
    fraction = min(max((value - low) / (high - low), 0.0), 1.0)
    return fraction * fraction * (3.0 - 2.0 * fraction)


def compute_lake_terrain_height_cm(x_cm, y_cm):
    radius = math.hypot(x_cm, y_cm)
    normalized = radius / LAKE_RADIUS_CM
    bed = -LAKE_DEPTH_CM * (1.0 - normalized) ** LAKE_BED_EXPONENT if normalized < 1.0 else 0.0
    bank = BANK_HEIGHT_CM * smoothstep(LAKE_RADIUS_CM, LAKE_RADIUS_CM + BANK_WIDTH_CM, radius)
    noise = BED_NOISE_CM * (
        math.sin(x_cm * 0.013 + 1.3) * math.cos(y_cm * 0.011 + 0.7) + 0.6 * math.sin(x_cm * 0.047 + y_cm * 0.031)
    ) * (1.0 - smoothstep(LAKE_RADIUS_CM, LAKE_RADIUS_CM + BANK_WIDTH_CM, radius))
    return bed + bank + noise


def river_centerline_fraction(x_cm):
    return (x_cm - RIVER_BEND_START_X_CM) / RIVER_BEND_LENGTH_CM


def river_centerline_y(x_cm):
    fraction = river_centerline_fraction(x_cm)
    if fraction <= 0.0 or fraction >= 1.0:
        return RIVER_BASE_Y_CM
    return RIVER_BASE_Y_CM + 0.5 * RIVER_BEND_AMPLITUDE_CM * (1.0 - math.cos(2.0 * math.pi * fraction))


def river_centerline_slope(x_cm):
    fraction = river_centerline_fraction(x_cm)
    if fraction <= 0.0 or fraction >= 1.0:
        return 0.0
    return RIVER_BEND_AMPLITUDE_CM * math.pi / RIVER_BEND_LENGTH_CM * math.sin(2.0 * math.pi * fraction)


def build_river_arc_length_table():
    step_count = int(round((RIVER_X_RANGE_CM[1] - RIVER_X_RANGE_CM[0]) / RIVER_ARC_STEP_CM))
    table = [0.0]
    for index in range(step_count):
        x_middle = RIVER_X_RANGE_CM[0] + (index + 0.5) * RIVER_ARC_STEP_CM
        slope = river_centerline_slope(x_middle)
        table.append(table[-1] + RIVER_ARC_STEP_CM * math.sqrt(1.0 + slope * slope))
    return table


RIVER_ARC_LENGTH_TABLE = build_river_arc_length_table()


def river_arc_length_cm(x_cm):
    clamped = min(max(x_cm, RIVER_X_RANGE_CM[0]), RIVER_X_RANGE_CM[1])
    position = (clamped - RIVER_X_RANGE_CM[0]) / RIVER_ARC_STEP_CM
    index = min(int(position), len(RIVER_ARC_LENGTH_TABLE) - 2)
    fraction = position - index
    return RIVER_ARC_LENGTH_TABLE[index] + (RIVER_ARC_LENGTH_TABLE[index + 1] - RIVER_ARC_LENGTH_TABLE[index]) * fraction


def river_surface_z_cm(x_cm):
    return RIVER_START_SURFACE_Z_CM - RIVER_DROP_PER_CM * river_arc_length_cm(x_cm)


def riffle_blend(x_cm):
    start, full_start, full_end, end = RIFFLE_X_CM
    if x_cm <= start or x_cm >= end:
        return 0.0
    if x_cm < full_start:
        return smoothstep(start, full_start, x_cm)
    if x_cm <= full_end:
        return 1.0
    return 1.0 - smoothstep(full_end, end, x_cm)


def river_width_scale(x_cm):
    return 1.0 - (1.0 - RIFFLE_WIDTH_SCALE) * riffle_blend(x_cm)


def river_half_width_cm(x_cm):
    return 0.5 * RIVER_WIDTH_CM * river_width_scale(x_cm)


def river_center_depth_cm(x_cm):
    return RIVER_DEPTH_CM - (RIVER_DEPTH_CM - RIFFLE_DEPTH_CM) * riffle_blend(x_cm)


def river_perpendicular_distance_cm(x_cm, y_cm):
    slope = river_centerline_slope(x_cm)
    return abs(y_cm - river_centerline_y(x_cm)) / math.sqrt(1.0 + slope * slope)


def river_channel_height_cm(x_cm, distance_cm):
    surface_z = river_surface_z_cm(x_cm)
    half_width = river_half_width_cm(x_cm)
    if distance_cm < half_width:
        ratio = distance_cm / half_width
        return surface_z - river_center_depth_cm(x_cm) * (1.0 - ratio * ratio)
    bank_fraction = min((distance_cm - half_width) / RIVER_BANK_RUN_CM, 1.0)
    return surface_z + RIVER_FREEBOARD_CM * smoothstep(0.0, 1.0, bank_fraction)


def compute_river_terrain_offset_cm(x_cm, y_cm):
    distance = river_perpendicular_distance_cm(x_cm, y_cm)
    weight = 1.0 - smoothstep(RIVER_INFLUENCE_CM[0], RIVER_INFLUENCE_CM[1], distance)
    if weight <= 0.0:
        return 0.0
    return weight * (river_channel_height_cm(x_cm, distance) - BANK_HEIGHT_CM)


def compute_terrain_height_cm(x_cm, y_cm, include_river):
    height = compute_lake_terrain_height_cm(x_cm, y_cm)
    if not include_river:
        return height
    return height + compute_river_terrain_offset_cm(x_cm, y_cm)


def compute_rock_geometry(spec):
    x_cm = spec["x_cm"]
    slope = river_centerline_slope(x_cm)
    y_cm = river_centerline_y(x_cm) + spec["lateral_cm"] * math.sqrt(1.0 + slope * slope)
    surface_z = river_surface_z_cm(x_cm)
    bed_z = river_channel_height_cm(x_cm, abs(spec["lateral_cm"]))
    if spec["kind"] == "above_water":
        top_z = surface_z + ROCK_ABOVE_WATER_CM
    else:
        top_z = surface_z - ROCK_SUBMERGED_BELOW_SURFACE_CM
    bottom_z = bed_z - ROCK_EMBED_CM
    height = top_z - bottom_z
    center_z = bottom_z + 0.5 * height
    semi_x = 50.0 * spec["scale_xy"][0]
    semi_y = 50.0 * spec["scale_xy"][1]
    semi_z = 0.5 * height
    offset_ratio = (surface_z - center_z) / semi_z
    waterline_ratio = math.sqrt(max(0.0, 1.0 - offset_ratio * offset_ratio)) if abs(offset_ratio) < 1.0 else 0.0
    yaw = spec["yaw_degrees"] if spec["yaw_degrees"] is not None else math.degrees(math.atan(slope))
    norm = math.sqrt(1.0 + slope * slope)
    return {
        "name": spec["name"],
        "kind": spec["kind"],
        "x_cm": x_cm,
        "y_cm": y_cm,
        "center_z_cm": center_z,
        "bed_z_cm": bed_z,
        "surface_z_cm": surface_z,
        "top_z_cm": top_z,
        "scale": (spec["scale_xy"][0], spec["scale_xy"][1], height / 100.0),
        "yaw_degrees": yaw,
        "waterline_radius_cm": semi_x * waterline_ratio,
        "bounding_radius_cm": max(semi_x, semi_y),
        "tangent": (1.0 / norm, slope / norm),
    }


def ground_position(x_cm, y_cm):
    return [x_cm, y_cm, compute_terrain_height_cm(x_cm, y_cm, True) + PLAYER_START_CLEARANCE_CM]


def point_on_centerline(x_cm, lateral_cm=0.0):
    slope = river_centerline_slope(x_cm)
    return [x_cm, river_centerline_y(x_cm) + lateral_cm * math.sqrt(1.0 + slope * slope)]


def build_probe_points():
    rocks = {geometry["name"]: geometry for geometry in (compute_rock_geometry(spec) for spec in ROCK_SPECS)}
    primary = rocks["RockPrimary"]
    submerged = rocks["RockSubmerged"]
    bank_half_width = river_half_width_cm(BANK_PROBE_X_CM)
    bank_dry_far_offset = bank_half_width + BANK_DRY_FAR_OFFSET_CM
    ford_center = point_on_centerline(CROSSING_X_CM)
    return {
        "asleep_pawn": ground_position(*ASLEEP_PAWN_XY_CM),
        "flow_pawn": ground_position(*FLOW_PAWN_XY_CM),
        "wide_center": [point_on_centerline(x_cm) for x_cm in WIDE_PROBE_X_CM],
        "riffle_center": [point_on_centerline(x_cm) for x_cm in RIFFLE_PROBE_X_CM],
        "bank": {
            "wet_band_south": point_on_centerline(BANK_PROBE_X_CM, -(bank_half_width + BANK_WET_OFFSET_CM)),
            "wet_band_north": point_on_centerline(BANK_PROBE_X_CM, bank_half_width + BANK_WET_OFFSET_CM),
            "dry_mid_south": point_on_centerline(BANK_PROBE_X_CM, -(bank_half_width + BANK_DRY_MID_OFFSET_CM)),
            "dry_far_south": point_on_centerline(BANK_PROBE_X_CM, -bank_dry_far_offset),
        },
        "rock_primary": {
            "center": [primary["x_cm"], primary["y_cm"]],
            "waterline_radius_cm": primary["waterline_radius_cm"],
            "tangent": list(primary["tangent"]),
            "pawn": ground_position(primary["x_cm"] + ROCK_PAWN_FORWARD_CM, primary["y_cm"] - ROCK_PAWN_SOUTH_CM),
        },
        "rock_submerged": {
            "center": [submerged["x_cm"], submerged["y_cm"]],
            "bounding_radius_cm": submerged["bounding_radius_cm"],
            "tangent": list(submerged["tangent"]),
            "pawn": ground_position(submerged["x_cm"], submerged["y_cm"] - ROCK_PAWN_SOUTH_CM),
        },
        "crossing": {
            "start": ground_position(ford_center[0], ford_center[1] - CROSSING_HALF_SPAN_CM),
            "goal": ground_position(ford_center[0], ford_center[1] + CROSSING_HALF_SPAN_CM),
            "ford_center": ford_center,
            "tangent": [1.0, 0.0],
            "foam_box": {
                "origin": ford_center,
                "s_range_cm": list(FOAM_BOX_S_CM),
                "half_width_cm": FOAM_BOX_HALF_WIDTH_CM,
                "step_cm": FOAM_BOX_STEP_CM,
            },
        },
    }


def measure_crossing_max_slope():
    start_y = RIVER_BASE_Y_CM - CROSSING_HALF_SPAN_CM
    steps = int(2.0 * CROSSING_HALF_SPAN_CM / 10.0)
    heights = [compute_terrain_height_cm(CROSSING_X_CM, start_y + 10.0 * index, True) for index in range(steps + 1)]
    return max(abs(heights[index + 1] - heights[index]) / 10.0 for index in range(steps))


def validate_river_layout():
    problems = []
    low_x, high_x = RIVER_X_RANGE_CM
    ford_x = CROSSING_X_CM
    wide_x = WIDE_PROBE_X_CM[1]
    if abs(river_half_width_cm(wide_x) - 0.5 * RIVER_WIDTH_CM) > 0.5 or abs(river_center_depth_cm(wide_x) - RIVER_DEPTH_CM) > 0.5:
        problems.append("넓은 구간 폭·깊이가 8m·1.2m 가 아님")
    if abs(river_half_width_cm(ford_x) - 0.25 * RIVER_WIDTH_CM) > 0.5 or abs(river_center_depth_cm(ford_x) - RIFFLE_DEPTH_CM) > 0.5:
        problems.append("여울 구간이 폭 절반·수심 40cm 가 아님")
    drop = river_surface_z_cm(low_x) - river_surface_z_cm(high_x)
    slope = drop / (river_arc_length_cm(high_x) - river_arc_length_cm(low_x))
    if abs(slope - RIVER_DROP_PER_CM) > 0.0001:
        problems.append(f"강 기울기 {slope:.5f} 가 0.5% 가 아님")
    bend_shift = max(abs(river_centerline_y(low_x + index * 10.0) - RIVER_BASE_Y_CM) for index in range(int((high_x - low_x) / 10.0)))
    if abs(bend_shift - RIVER_BEND_AMPLITUDE_CM) > 2.0:
        problems.append("굽이 진폭이 설정과 다름")
    if RIVER_BEND_START_X_CM + RIVER_BEND_LENGTH_CM >= RIFFLE_X_CM[0]:
        problems.append("굽이가 여울과 겹침")
    if RIVER_SPLINE_SPACING_CM > RIVER_RIBBON_SEGMENT_LENGTH_CM:
        problems.append("스플라인 점 간격이 리본 구간 길이보다 큼")

    geometries = [compute_rock_geometry(spec) for spec in ROCK_SPECS]
    kinds = sorted(geometry["kind"] for geometry in geometries)
    if kinds != ["above_water", "above_water", "submerged"]:
        problems.append("바위가 수면 위 2개·잠김 1개가 아님")
    for geometry in geometries:
        half_width = river_half_width_cm(geometry["x_cm"])
        lateral = river_perpendicular_distance_cm(geometry["x_cm"], geometry["y_cm"])
        if lateral + geometry["bounding_radius_cm"] > half_width - 50.0:
            problems.append(f"{geometry['name']} 이(가) 수로 폭에 비해 기슭에 너무 가까움")
        if geometry["kind"] == "above_water" and geometry["waterline_radius_cm"] < 40.0:
            problems.append(f"{geometry['name']} 수면 단면이 너무 작아 고체 셀이 안 생김")
        if geometry["kind"] == "submerged" and geometry["top_z_cm"] >= geometry["surface_z_cm"]:
            problems.append(f"{geometry['name']} 이(가) 수면 아래가 아님")
    positions = sorted(geometry["x_cm"] for geometry in geometries)
    if any(positions[index + 1] - positions[index] < 1000.0 for index in range(len(positions) - 1)):
        problems.append("바위 사이 간격이 10m 미만")

    points = build_probe_points()
    flow_pawn = points["flow_pawn"]
    checks = [(flow_pawn, point) for point in points["wide_center"] + points["riffle_center"] + list(points["bank"].values())]
    checks.append((points["rock_primary"]["pawn"], points["rock_primary"]["center"]))
    checks.append((points["rock_submerged"]["pawn"], points["rock_submerged"]["center"]))
    wake_end = points["rock_primary"]["center"][0] + points["rock_primary"]["waterline_radius_cm"] + 300.0
    checks.append((points["rock_primary"]["pawn"], [wake_end, points["rock_primary"]["center"][1]]))
    crossing = points["crossing"]
    box = crossing["foam_box"]
    foam_checks = []
    for pawn_point in (crossing["start"], crossing["goal"]):
        foam_checks.append((pawn_point, [box["origin"][0] + box["s_range_cm"][1], box["origin"][1] + box["half_width_cm"]]))
        foam_checks.append((pawn_point, [box["origin"][0] + box["s_range_cm"][0], box["origin"][1] - box["half_width_cm"]]))
    for pawn_point, target in checks:
        if abs(target[0] - pawn_point[0]) > WINDOW_CORE_HALF_CM or abs(target[1] - pawn_point[1]) > WINDOW_CORE_HALF_CM:
            problems.append(f"탐침 점 {target[0]:.0f},{target[1]:.0f} 이(가) 폰 {pawn_point[0]:.0f},{pawn_point[1]:.0f} 의 시뮬레이션 창 안쪽({WINDOW_CORE_HALF_CM:.0f}cm)에 안 들어감")
    foam_reach = WINDOW_HALF_CM - WINDOW_LOOK_AHEAD_CM
    for pawn_point, target in foam_checks:
        if abs(target[0] - pawn_point[0]) > foam_reach or abs(target[1] - pawn_point[1]) > foam_reach:
            problems.append(f"거품 상자 모서리 {target[0]:.0f},{target[1]:.0f} 이(가) 폰 {pawn_point[0]:.0f},{pawn_point[1]:.0f} 의 시뮬레이션 창({foam_reach:.0f}cm) 밖")

    limit = LANDSCAPE_HALF_SPAN_CM - 100.0
    outside = [point for point in (flow_pawn, points["asleep_pawn"], crossing["start"], crossing["goal"]) if abs(point[0]) > limit or abs(point[1]) > limit]
    if outside:
        problems.append("폰 위치가 지형 밖")
    river_top = RIVER_BASE_Y_CM + RIVER_BEND_AMPLITUDE_CM + RIVER_INFLUENCE_CM[1]
    if river_top > LANDSCAPE_HALF_SPAN_CM or low_x < -LANDSCAPE_HALF_SPAN_CM or high_x > LANDSCAPE_HALF_SPAN_CM:
        problems.append("강 영향 범위가 지형 밖")

    lake_reach = LAKE_RADIUS_CM + BANK_WIDTH_CM
    grid_steps = int(2.0 * lake_reach / 100.0) + 1
    for row in range(grid_steps):
        for column in range(grid_steps):
            x_cm = -lake_reach + 100.0 * column
            y_cm = -lake_reach + 100.0 * row
            if x_cm * x_cm + y_cm * y_cm <= lake_reach * lake_reach and compute_river_terrain_offset_cm(x_cm, y_cm) != 0.0:
                problems.append("강 지형이 장면 0 호수 영역을 바꿈")
                return problems

    crossing_slope = measure_crossing_max_slope()
    if crossing_slope > CROSSING_MAX_SLOPE:
        problems.append(f"여울 건너는 경로 최대 경사 {crossing_slope:.2f} 가 걸을 수 없음")
    return problems


def write_heightmap(path, include_river):
    vertices_per_side = LANDSCAPE_COMPONENTS * LANDSCAPE_QUADS_PER_SECTION + 1
    origin = -0.5 * (vertices_per_side - 1) * LANDSCAPE_XY_SCALE_CM
    values = array.array("H")
    for row in range(vertices_per_side):
        y_cm = origin + row * LANDSCAPE_XY_SCALE_CM
        for column in range(vertices_per_side):
            x_cm = origin + column * LANDSCAPE_XY_SCALE_CM
            encoded = round(HEIGHT_ZERO_VALUE + compute_terrain_height_cm(x_cm, y_cm, include_river) * HEIGHT_UNITS_PER_CM)
            values.append(min(max(encoded, 0), 65535))
    if values.itemsize != 2:
        raise RuntimeError("uint16 배열 크기 확인 실패")
    if sys_is_big_endian():
        values.byteswap()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as raw_file:
        values.tofile(raw_file)
    return vertices_per_side


def sys_is_big_endian():
    return array.array("H", [1]).tobytes()[0] == 0


def create_landscape(world, material, heightmap_path):
    request = unreal.TDLandscapeCreateRequest()
    request.set_editor_property("location", unreal.Vector(0.0, 0.0, 0.0))
    request.set_editor_property("scale", unreal.Vector(LANDSCAPE_XY_SCALE_CM, LANDSCAPE_XY_SCALE_CM, LANDSCAPE_Z_SCALE))
    request.set_editor_property("component_count_x", LANDSCAPE_COMPONENTS)
    request.set_editor_property("component_count_y", LANDSCAPE_COMPONENTS)
    request.set_editor_property("sections_per_component", 1)
    request.set_editor_property("quads_per_section", LANDSCAPE_QUADS_PER_SECTION)
    request.set_editor_property("heightmap_raw_path", heightmap_path)
    request.set_editor_property("layer_names", [])
    request.set_editor_property("layer_raw_paths", [])
    request.set_editor_property("material", material)
    request.set_editor_property("world_partition_grid_size_in_components", 0)
    request.set_editor_property("enable_nanite", False)
    landscape = unreal.TDLandscapeEditorLibrary.create_landscape_from_raw_files(world, request)
    if landscape is None:
        raise RuntimeError("지형 생성 실패(Saved/Logs/TDGame.log 의 LogTDLandscape 확인)")
    return landscape


def load_or_create_preset(preset_name=PRESET_NAME, preset_path=PRESET_PATH):
    if unreal.EditorAssetLibrary.does_asset_exist(preset_path):
        preset = unreal.EditorAssetLibrary.load_asset(preset_path)
        if not isinstance(preset, unreal.TDFluidPresetAsset):
            raise RuntimeError(f"기존 프리셋 에셋 형식 불일치: {preset_path}")
        return preset
    ensure_directory(PRESET_DIR)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.TDFluidPresetAsset)
    preset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(preset_name, PRESET_DIR, unreal.TDFluidPresetAsset, factory)
    if preset is None:
        raise RuntimeError(f"프리셋 생성 실패: {preset_path}")
    return preset


def build_simulation_config():
    config = unreal.TDFluidPresetAsset.build_config_for_preset_kind(unreal.TDFluidPresetKind.WATER)
    for name, value in PRESET_CONFIG.items():
        if name == "sim_extent_cm":
            config.set_editor_property(name, unreal.Vector2D(value[0], value[1]))
            continue
        config.set_editor_property(name, value)
    return config


def configure_preset(surface_material, surface_mesh):
    preset = load_or_create_preset()
    preset.set_editor_property("preset_kind", unreal.TDFluidPresetKind.CUSTOM)
    preset.set_editor_property("override_preset_defaults", True)
    preset.set_editor_property("default_channel", unreal.TDFluidBrushChannel.CHANNEL_A_WATER)
    preset.set_editor_property("simulation_config", build_simulation_config())
    plane = unreal.TDFluidPlaneSettings()
    plane.set_editor_property("orientation", unreal.TDFluidPlaneOrientation.WORLD_FACING)
    plane.set_editor_property("camera_depth_bias_cm", 0.0)
    plane.set_editor_property("center_offset_along_up_ratio", 0.0)
    preset.set_editor_property("plane_settings", plane)
    preset.set_editor_property("output_material", surface_material)
    preset.set_editor_property("surface_mesh", surface_mesh)
    preset.set_editor_property("translucency_sort_priority", 0)

    effective = preset.resolve_effective_config()
    mismatches = []
    extent = effective.get_editor_property("sim_extent_cm")
    expected_extent = PRESET_CONFIG["sim_extent_cm"]
    if abs(extent.x - expected_extent[0]) > 0.01 or abs(extent.y - expected_extent[1]) > 0.01:
        mismatches.append("sim_extent_cm")
    for name, value in PRESET_CONFIG.items():
        if name == "sim_extent_cm":
            continue
        if abs(float(effective.get_editor_property(name)) - float(value)) > 0.001:
            mismatches.append(name)
    if mismatches:
        raise RuntimeError(f"프리셋 유효 구성이 명세와 다릅니다: {PRESET_PATH} {mismatches}")
    save_asset(preset, PRESET_PATH)
    return preset, effective


RIVER_EFFECTIVE_REPORT_NAMES = (
    "enable_water_layer",
    "open_water_borders",
    "treat_dry_cells_as_solid",
    "grid_resolution",
    "pressure_iterations",
    "velocity_damping",
    "density_dissipation",
    "advection_speed_scale",
    "terrain_slope_gravity",
    "vorticity_strength",
    "whitewater_from_vorticity",
    "rapids_whitewater_per_sec",
    "ripple_wave_speed_cm_per_sec",
    "actor_brush_ripple_impulse_cm",
    "actor_brush_density_strength",
    "bank_wet_height_cm",
)


def configure_river_preset(surface_material, surface_mesh):
    preset = load_or_create_preset(RIVER_PRESET_NAME, RIVER_PRESET_PATH)
    preset.set_editor_property("preset_kind", unreal.TDFluidPresetKind.RIVER)
    preset.set_editor_property("override_preset_defaults", False)
    preset.set_editor_property("default_channel", unreal.TDFluidBrushChannel.CHANNEL_A_WATER)
    plane = unreal.TDFluidPlaneSettings()
    plane.set_editor_property("orientation", unreal.TDFluidPlaneOrientation.WORLD_FACING)
    plane.set_editor_property("camera_depth_bias_cm", 0.0)
    plane.set_editor_property("center_offset_along_up_ratio", 0.0)
    preset.set_editor_property("plane_settings", plane)
    preset.set_editor_property("output_material", surface_material)
    preset.set_editor_property("surface_mesh", surface_mesh)
    preset.set_editor_property("translucency_sort_priority", 0)

    effective = preset.resolve_effective_config()
    extent = effective.get_editor_property("sim_extent_cm")
    problems = []
    if not effective.get_editor_property("enable_water_layer"):
        problems.append("enable_water_layer")
    if int(effective.get_editor_property("grid_resolution")) != 192:
        problems.append("grid_resolution")
    if abs(extent.x - 3600.0) > 0.01 or abs(extent.y - 3600.0) > 0.01:
        problems.append("sim_extent_cm")
    if problems:
        raise RuntimeError(f"강 프리셋 유효 구성이 명세(B1-6)와 다릅니다: {RIVER_PRESET_PATH} {problems}")
    save_asset(preset, RIVER_PRESET_PATH)
    summary = {name: effective.get_editor_property(name) for name in RIVER_EFFECTIVE_REPORT_NAMES if has_editor_property(effective, name)}
    return preset, effective, summary


def configure_post_process(volume):
    volume.set_editor_property("unbound", True)
    settings = volume.settings
    for name, value in (
        ("auto_exposure_method", unreal.AutoExposureMethod.AEM_HISTOGRAM),
        ("auto_exposure_min_brightness", EXPOSURE_EV100),
        ("auto_exposure_max_brightness", EXPOSURE_EV100),
        ("auto_exposure_bias", 0.0),
        ("bloom_intensity", 0.3),
        ("vignette_intensity", 0.15),
    ):
        settings.set_editor_property("override_" + name, True)
        settings.set_editor_property(name, value)
    volume.set_editor_property("settings", settings)


def add_actor_tag(actor, tag_name):
    tags = [str(tag) for tag in actor.get_editor_property("tags")]
    if tag_name not in tags:
        tags.append(tag_name)
    actor.set_editor_property("tags", [unreal.Name(tag) for tag in tags])
    return tags


def apply_river_spline_points(spline, points):
    spline.clear_spline_points(False)
    for x_cm, y_cm, z_cm, _ in points:
        spline.add_spline_point(unreal.Vector(x_cm, y_cm, z_cm), unreal.SplineCoordinateSpace.WORLD, False)
    last_index = len(points) - 1
    for index, point in enumerate(points):
        spline.set_scale_at_spline_point(index, unreal.Vector(1.0, point[3], 1.0), False)
        spline.set_spline_point_type(index, unreal.SplinePointType.CURVE, index == last_index)


def build_river_spline_points():
    low_x, high_x = RIVER_X_RANGE_CM
    point_count = int(round((high_x - low_x) / RIVER_SPLINE_SPACING_CM)) + 1
    points = []
    for index in range(point_count):
        x_cm = low_x + (high_x - low_x) * index / (point_count - 1)
        points.append((x_cm, river_centerline_y(x_cm), river_surface_z_cm(x_cm), river_width_scale(x_cm)))
    return points


def configure_river_actor(river, points, river_material, ribbon_mesh):
    spline = river.get_editor_property("river_spline")
    apply_river_spline_points(spline, points)
    river.set_editor_property("river_width_cm", RIVER_WIDTH_CM)
    river.set_editor_property("flow_speed_cm_per_sec", RIVER_FLOW_SPEED_CM_PER_SEC)
    river.set_editor_property("conserve_discharge_across_width", True)
    river.set_editor_property("guide_sample_spacing_cm", RIVER_GUIDE_SAMPLE_SPACING_CM)
    river.set_editor_property("build_ribbon_surface", True)
    river.set_editor_property("ribbon_mesh", ribbon_mesh)
    river.set_editor_property("ribbon_material", river_material)
    river.set_editor_property("ribbon_segment_length_cm", RIVER_RIBBON_SEGMENT_LENGTH_CM)
    tags = add_actor_tag(river, TAG_WATER_SURFACE)
    if TAG_WATER_REGION in tags:
        raise RuntimeError("강 리본 액터에 수역 태그가 붙으면 안 됩니다(명세 5절)")
    expected_length = RIVER_ARC_LENGTH_TABLE[-1]
    actual_length = float(spline.get_spline_length())
    if abs(actual_length - expected_length) > 0.03 * expected_length:
        raise RuntimeError(f"스플라인 길이 불일치: 기대 {expected_length:.0f} 실제 {actual_length:.0f}")
    first = spline.get_location_at_spline_point(0, unreal.SplineCoordinateSpace.WORLD)
    last = spline.get_location_at_spline_point(len(points) - 1, unreal.SplineCoordinateSpace.WORLD)
    return {
        "point_count": int(spline.get_number_of_spline_points()),
        "length_cm": actual_length,
        "expected_length_cm": expected_length,
        "first": [first.x, first.y, first.z],
        "last": [last.x, last.y, last.z],
        "tags": tags,
    }


def configure_water_actor(water, river_preset, river_effective):
    live_component = water.get_editor_property("live_component")
    live_component.set_editor_property("active_preset", river_preset)
    live_component.set_editor_property("should_init_system_state_from_preset", True)
    live_component.set_editor_property("simulation_config", river_effective)
    water.set_editor_property("should_use_activation_volume", False)
    water.set_editor_property("should_follow_player_pawn", True)
    water.set_editor_property("sample_terrain_elevation", True)
    water.set_editor_property("interaction_brush_channel", unreal.TDFluidBrushChannel.CHANNEL_A_WATER)
    property_names = (
        "water_surface_actor_tag",
        "water_region_actor_tag",
        "water_obstacle_actor_tag",
        "terrain_trace_ignore_tag",
        "sleep_when_no_water_in_window",
        "sleep_delay_seconds",
        "view_look_ahead_cm",
        "deep_water_fallback_depth_cm",
        "water_layer_fade_in_seconds",
        "max_terrain_traces_per_tick",
    )
    return {name: str(water.get_editor_property(name)) for name in property_names if has_editor_property(water, name)}


def spawn_river_rocks(actors, finish, sphere_mesh, stone_material):
    records = []
    for spec in ROCK_SPECS:
        geometry = compute_rock_geometry(spec)
        rock = actors.spawn_actor_from_object(
            sphere_mesh,
            unreal.Vector(geometry["x_cm"], geometry["y_cm"], geometry["center_z_cm"]),
            unreal.Rotator(yaw=geometry["yaw_degrees"]),
        )
        finish(rock, "WaterStageRiver" + spec["name"], "Scene1_River")
        rock.set_actor_scale3d(unreal.Vector(*geometry["scale"]))
        component = rock.static_mesh_component
        component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        component.set_material(0, stone_material)
        tags = add_actor_tag(rock, TAG_OBSTACLE) if spec["kind"] == "submerged" else []
        records.append(
            {
                "name": spec["name"],
                "kind": spec["kind"],
                "location": [geometry["x_cm"], geometry["y_cm"], geometry["center_z_cm"]],
                "scale": list(geometry["scale"]),
                "yaw_degrees": geometry["yaw_degrees"],
                "top_z_cm": geometry["top_z_cm"],
                "surface_z_cm": geometry["surface_z_cm"],
                "waterline_radius_cm": geometry["waterline_radius_cm"],
                "bounding_radius_cm": geometry["bounding_radius_cm"],
                "tags": tags,
            }
        )
    return records


def build_river_scene(spawn, finish, actors, resources):
    folder = "Scene1_River"
    points = build_river_spline_points()
    river = spawn(resources["river_actor_class"], (0.0, 0.0, 0.0), "WaterRiver", folder)
    spline_report = configure_river_actor(river, points, resources["river_material"], resources["ribbon_mesh"])
    water = spawn(resources["water_actor_class"], (0.0, RIVER_BASE_Y_CM, 0.0), "WaterRiverSimulation", folder)
    water_report = configure_water_actor(water, resources["river_preset"], resources["river_effective"])
    rock_records = spawn_river_rocks(actors, finish, resources["sphere_mesh"], resources["stone_material"])

    navigation = spawn(unreal.NavMeshBoundsVolume, (0.0, RIVER_NAVIGATION_CENTER_Y_CM, 0.0), "WaterStageRiverNavigation", "Gameplay")
    navigation.set_actor_scale3d(unreal.Vector(*RIVER_NAVIGATION_SCALE))
    nav_origin, nav_extent = navigation.get_actor_bounds(False)
    if nav_extent.x < RIVER_NAVIGATION_MIN_EXTENT_CM[0] or nav_extent.y < RIVER_NAVIGATION_MIN_EXTENT_CM[1]:
        raise RuntimeError(f"강 내비게이션 브러시 범위 검증 실패: {nav_extent}")

    internal_surface = water.get_editor_property("internal_surface_mesh")
    return {
        "layout": {
            "x_range_cm": list(RIVER_X_RANGE_CM),
            "base_y_cm": RIVER_BASE_Y_CM,
            "bend": {"start_x_cm": RIVER_BEND_START_X_CM, "length_cm": RIVER_BEND_LENGTH_CM, "amplitude_cm": RIVER_BEND_AMPLITUDE_CM},
            "width_cm": RIVER_WIDTH_CM,
            "depth_cm": RIVER_DEPTH_CM,
            "drop_per_cm": RIVER_DROP_PER_CM,
            "start_surface_z_cm": RIVER_START_SURFACE_Z_CM,
            "freeboard_cm": RIVER_FREEBOARD_CM,
            "bank_run_cm": RIVER_BANK_RUN_CM,
            "riffle": {
                "x_cm": list(RIFFLE_X_CM),
                "width_scale": RIFFLE_WIDTH_SCALE,
                "depth_cm": RIFFLE_DEPTH_CM,
                "half_width_cm": river_half_width_cm(CROSSING_X_CM),
            },
            "flow_speed_cm_per_sec": RIVER_FLOW_SPEED_CM_PER_SEC,
            "expected_riffle_speed_cm_per_sec": RIVER_FLOW_SPEED_CM_PER_SEC / RIFFLE_WIDTH_SCALE,
        },
        "spline": spline_report,
        "river_actor": river.get_actor_label(),
        "water_actor": water.get_actor_label(),
        "water_actor_properties": water_report,
        "internal_surface_visible": bool(internal_surface.is_visible()) if internal_surface is not None else None,
        "rocks": rock_records,
        "navigation_extent": [nav_extent.x, nav_extent.y, nav_extent.z],
        "probe_points": build_probe_points(),
        "ribbon_mesh": RIBBON_MESH_PATH,
    }


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


def main():
    assets = unreal.EditorAssetLibrary
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    request = read_request()
    scenes = read_requested_scenes(request)
    include_river = 1 in scenes

    missing_names = find_missing_unreal_names(scenes)
    if missing_names:
        raise RuntimeError("언리얼 Python 이름 없음: " + ", ".join(missing_names))
    if editor.get_game_world() is not None:
        raise RuntimeError("PIE를 종료한 뒤 실행하세요.")
    if editor.get_editor_world() is None:
        raise RuntimeError("에디터 월드가 없습니다.")

    contract_problems = check_cpp_contract(scenes)
    if contract_problems:
        raise RuntimeError("C++ 계약 이름 없음(재빌드와 에디터 재시작 필요): " + ", ".join(contract_problems))
    if include_river:
        layout_problems = validate_river_layout()
        if layout_problems:
            raise RuntimeError("강 배치 자체 점검 실패: " + " | ".join(layout_problems))

    overwrite = request.get("overwrite") is True
    existing_paths = [path for path in collect_guarded_asset_paths(scenes) if assets.does_asset_exist(path)]
    if existing_paths and not overwrite:
        print(json.dumps({
            "status": "기존 에셋이 있어 아무것도 바꾸지 않았습니다",
            "existing_assets": existing_paths,
            "overwrite_request_file": project_path(*REQUEST_RELATIVE_PATH),
            "overwrite_request_content": {"overwrite": True, "scenes": list(scenes)},
        }, ensure_ascii=False))
        raise RuntimeError("기존 에셋 보존: overwrite 요청 파일이 없어 중단합니다.")

    unsaved_names = find_unsaved_package_names(overwrite)
    if unsaved_names:
        raise RuntimeError("미저장 작업을 보존합니다: " + ", ".join(unsaved_names))

    game_mode = assets.load_blueprint_class(GAME_MODE_PATH)
    lake_material = assets.load_asset(LAKE_INSTANCE_PATH)
    surface_mesh = assets.load_asset(SURFACE_MESH_PATH)
    sphere_mesh = assets.load_asset(SPHERE_MESH_PATH)
    live_actor_class = unreal.load_class(None, CLASS_PATHS["TDFluidLiveActor"])
    required = {
        "게임 모드": game_mode,
        "호수 수면 재질": lake_material,
        "수면 격자 메시": surface_mesh,
        "구 메시": sphere_mesh,
        "라이브 액터 클래스": live_actor_class,
    }
    river_material = None
    ribbon_mesh = None
    water_actor_class = None
    river_actor_class = None
    if include_river:
        river_material = assets.load_asset(RIVER_INSTANCE_PATH)
        ribbon_mesh = assets.load_asset(RIBBON_MESH_PATH)
        water_actor_class = unreal.load_class(None, CLASS_PATHS["TDFluidWaterActor"])
        river_actor_class = unreal.load_class(None, CLASS_PATHS["TDFluidRiverActor"])
        required["강 수면 재질"] = river_material
        required["리본 메시"] = ribbon_mesh
        required["물 액터 클래스"] = water_actor_class
        required["강 액터 클래스"] = river_actor_class
    missing = [name for name, value in required.items() if value is None]
    if missing:
        raise RuntimeError("필수 에셋 또는 클래스 없음(editor_make_fluid_water_material.py 먼저): " + ", ".join(missing))

    print("단계: 재질 준비")
    matte_master, matte_created = load_or_create_matte_master()
    sand_material = write_matte_instance(SAND_INSTANCE_PATH, build_sand_master(), SAND_COLOR, 0.9)
    stone_material = write_matte_instance(STONE_INSTANCE_PATH, matte_master, STONE_COLOR, 0.75)

    print("단계: 프리셋")
    preset, effective = configure_preset(lake_material, surface_mesh)
    river_preset = None
    river_effective = None
    river_preset_summary = None
    if include_river:
        river_preset, river_effective, river_preset_summary = configure_river_preset(river_material, surface_mesh)

    print("단계: 지형 높이 파일")
    heightmap_path = project_path(*HEIGHTMAP_RELATIVE_PATH)
    vertices_per_side = write_heightmap(heightmap_path, include_river)

    print("단계: 레벨 비우기")
    ensure_directory(MAP_DIR)
    open_empty_stage_level(levels, actors)

    world = editor.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", game_mode)
    created = []

    def finish(actor, label, folder):
        if actor is None:
            raise RuntimeError(f"액터 생성 실패: {label}")
        actor.set_actor_label("TDGen_" + label)
        actor.set_folder_path("TDGen/FluidFX/WaterStage/" + folder)
        created.append(actor)
        return actor

    def spawn(actor_class, location, label, folder, rotation=None):
        actor = actors.spawn_actor_from_class(actor_class, unreal.Vector(*location), rotation or unreal.Rotator())
        return finish(actor, label, folder)

    print("단계: 지형")
    landscape = finish(create_landscape(world, sand_material, heightmap_path), "WaterStageLandscape", "Stage")

    sun = spawn(unreal.DirectionalLight, (0, 0, 1500), "WaterStageSun", "Lighting", unreal.Rotator(pitch=SUN_ROTATION[0], yaw=SUN_ROTATION[1]))
    sun.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_intensity(SUN_LUX)
    spawn(unreal.SkyAtmosphere, (0, 0, 0), "WaterStageSkyAtmosphere", "Lighting")
    sky = spawn(unreal.SkyLight, (0, 0, 1000), "WaterStageSkyLight", "Lighting")
    sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", True)
    sky.light_component.set_intensity(1.0)
    configure_post_process(spawn(unreal.PostProcessVolume, (0, 0, 0), "WaterStageExposure", "Lighting"))

    start_height = compute_lake_terrain_height_cm(PLAYER_START_XY[0], PLAYER_START_XY[1])
    player_start_location = (PLAYER_START_XY[0], PLAYER_START_XY[1], start_height + PLAYER_START_CLEARANCE_CM)
    spawn(unreal.PlayerStart, player_start_location, "WaterStagePlayerStart", "Gameplay")

    print("단계: 호수 바닥 돌")
    stone_records = spawn_lake_rocks(actors, finish, random.Random(STONE_RANDOM_SEED))

    print("단계: 장면 0 물")
    water = spawn(live_actor_class, (0.0, 0.0, WATER_SURFACE_Z_CM), "WaterLook0", "Scene0_Lake")
    water.get_editor_property("internal_surface_mesh").set_cast_shadow(False)
    live_component = water.get_editor_property("live_component")
    live_component.set_editor_property("active_preset", preset)
    live_component.set_editor_property("should_init_system_state_from_preset", True)
    live_component.set_editor_property("simulation_config", effective)
    water.set_editor_property("should_use_activation_volume", False)
    water.set_editor_property("should_follow_player_pawn", False)
    water.set_editor_property("sample_terrain_elevation", False)
    water.set_editor_property("interaction_brush_channel", unreal.TDFluidBrushChannel.CHANNEL_A_WATER)

    navigation = spawn(unreal.NavMeshBoundsVolume, (0.0, 0.0, 0.0), "WaterStageNavigation", "Gameplay")
    navigation.set_actor_scale3d(unreal.Vector(*NAVIGATION_SCALE))
    nav_origin, nav_extent = navigation.get_actor_bounds(False)
    if nav_extent.x < NAVIGATION_MIN_EXTENT_CM[0] or nav_extent.y < NAVIGATION_MIN_EXTENT_CM[1]:
        raise RuntimeError(f"내비게이션 브러시 범위 검증 실패: {nav_extent}")
    for nav_mesh in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RecastNavMesh):
        nav_mesh.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC)
        nav_mesh.set_editor_property("is_world_partitioned", False)

    river_report = None
    if include_river:
        print("단계: 장면 1 강")
        river_report = build_river_scene(
            spawn,
            finish,
            actors,
            {
                "river_actor_class": river_actor_class,
                "water_actor_class": water_actor_class,
                "river_material": river_material,
                "ribbon_mesh": ribbon_mesh,
                "river_preset": river_preset,
                "river_effective": river_effective,
                "sphere_mesh": sphere_mesh,
                "stone_material": stone_material,
            },
        )
        river_report["preset"] = RIVER_PRESET_PATH
        river_report["preset_effective"] = river_preset_summary

    landscape_origin, landscape_extent = landscape.get_actor_bounds(False)
    expected_half_extent = 0.5 * (vertices_per_side - 1) * LANDSCAPE_XY_SCALE_CM
    if abs(landscape_extent.x - expected_half_extent) > LANDSCAPE_XY_SCALE_CM * 2.0:
        raise RuntimeError(f"지형 범위 검증 실패: 기대 반폭 {expected_half_extent} 실제 {landscape_extent.x}")

    print("단계: 레벨 저장")
    if not levels.save_current_level():
        raise RuntimeError(f"무대 레벨 저장 실패: {STAGE_MAP_PATH}")

    saved_assets = [PRESET_PATH, MATTE_MASTER_PATH, SAND_INSTANCE_PATH, STONE_INSTANCE_PATH]
    if include_river:
        saved_assets.append(RIVER_PRESET_PATH)
    missing_files = find_missing_saved_files(saved_assets, STAGE_MAP_PATH)
    if missing_files:
        raise RuntimeError("저장 파일 확인 실패: " + ", ".join(missing_files))

    report = {
        "status": "ok",
        "map": STAGE_MAP_PATH,
        "scenes": list(scenes),
        "overwrite": overwrite,
        "created_matte_master": matte_created,
        "preset": PRESET_PATH,
        "grid_resolution": effective.get_editor_property("grid_resolution"),
        "water_surface_z_cm": WATER_SURFACE_Z_CM,
        "lake_radius_cm": LAKE_RADIUS_CM,
        "lake_depth_cm": LAKE_DEPTH_CM,
        "bank_height_cm": BANK_HEIGHT_CM,
        "landscape_vertices_per_side": vertices_per_side,
        "landscape_half_extent_cm": [landscape_extent.x, landscape_extent.y, landscape_extent.z],
        "player_start": list(player_start_location),
        "navigation_extent": [nav_extent.x, nav_extent.y, nav_extent.z],
        "stones": stone_records,
        "exposure_ev100": EXPOSURE_EV100,
        "sun_lux": SUN_LUX,
        "created_actors": len(created),
        "river": river_report,
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
