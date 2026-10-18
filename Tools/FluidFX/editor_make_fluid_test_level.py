"""실행 환경: TDGame 언리얼 에디터 안에서 FluidFX 마스터 재질·재질 인스턴스·프리셋 5종·전투 유체 테스트 레벨을 생성한다.
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_test_level.py
출력: /Game/FluidFX/Materials, /Game/FluidFX/Presets/DA_TDFluid_*, /Game/FluidFX/Maps/LV_TDFluidTestArena, Saved/FluidFX/fluid_test_level.json
상태: 현행(2026-10-18 첫 성공, 증거 Docs/Validation/FluidFX/D-48-*). 기존 에셋은 Saved/FluidFX/fluid_test_level_request.json 의 {"overwrite": true} 가 있을 때만 덮어쓰고, 미저장 작업이 있으면 중단한다.
"""
import json
import math
import os

import unreal


FLUID_ROOT = "/Game/FluidFX"
MATERIAL_DIR = f"{FLUID_ROOT}/Materials"
PRESET_DIR = f"{FLUID_ROOT}/Presets"
MAP_DIR = f"{FLUID_ROOT}/Maps"

FLUID_SURFACE_MAT_PATH = f"{MATERIAL_DIR}/M_TD_FluidSurface"
FLOOR_MAT_PATH = f"{MATERIAL_DIR}/M_TD_FluidArenaFloor"
WALL_MAT_PATH = f"{MATERIAL_DIR}/M_TD_FluidArenaWall"
ARENA_MAP_PATH = f"{MAP_DIR}/LV_TDFluidTestArena"
GAME_MODE_PATH = "/Game/Combat/Blueprints/BP_TDCombatGameMode"

PLANE_MESH_PATH = "/Engine/BasicShapes/Plane"
FLUID_ACTOR_CLASS_PATH = "/Script/TDGame.TDFluidLiveActor"
DAMAGE_TARGET_CLASS_PATH = "/Script/TDGame.TDDamageTarget"

REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_test_level_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_test_level.json")

DEFAULT_TEXTURE_PATHS = ("/Engine/EngineResources/Black", "/Engine/EngineResources/DefaultTexture")
GRID_RESOLUTION = 128

PRESET_SPECS = (
    {
        "name": "Water",
        "kind": "WATER",
        "channel": "CHANNEL_A_WATER",
        "extent_cm": (800.0, 640.0),
        "tints": {"BaseTint": (0.05, 0.28, 0.55)},
    },
    {
        "name": "Mud",
        "kind": "MUD",
        "channel": "CHANNEL_R_MUD",
        "extent_cm": (800.0, 640.0),
        "tints": {"BaseTint": (0.22, 0.13, 0.06)},
    },
    {
        "name": "Blood",
        "kind": "BLOOD",
        "channel": "CHANNEL_G_BLOOD",
        "extent_cm": (1000.0, 700.0),
        "tints": {"BaseTint": (0.10, 0.08, 0.07), "SelectiveTint": (0.55, 0.02, 0.03)},
    },
    {
        "name": "Fire",
        "kind": "FIRE",
        "channel": "CHANNEL_B_FIRE",
        "extent_cm": (800.0, 640.0),
        "tints": {"BaseTint": (0.08, 0.07, 0.07), "SelectiveTint": (1.0, 0.35, 0.04)},
    },
    {
        "name": "Footprint",
        "kind": "FOOTPRINT_PAINTER",
        "channel": "CHANNEL_R_MUD",
        "extent_cm": (1000.0, 1000.0),
        "tints": {"BaseTint": (0.35, 0.25, 0.14)},
    },
)

RAMP_CENTER_XY = (1000.0, 820.0)
RAMP_LENGTH_CM = 900.0
RAMP_WIDTH_CM = 720.0
RAMP_THICKNESS_CM = 30.0
RAMP_PITCH_DEGREES = 6.0
RAMP_SUPPORT_DEPTH_CM = 60.0
RAMP_MAX_PITCH_DEGREES = 15.0
FLUID_SURFACE_LIFT_CM = 3.0

MATERIAL_VECTOR_PARAMETERS = ("SimCenterWorld", "SimExtentsCm", "BaseTint", "SelectiveTint")
MATERIAL_SCALAR_PARAMETERS = ("OpacityScale", "SpeedBrightness")
MATERIAL_TEXTURE_PARAMETERS = ("DensityPressureTexture", "VelocityTexture")

PRESET_REQUIRED_PROPERTIES = (
    "preset_kind",
    "simulation_config",
    "default_channel",
    "output_material",
    "override_preset_defaults",
    "surface_mesh",
)
ACTOR_REQUIRED_PROPERTIES = (
    "should_follow_player_pawn",
    "should_use_activation_volume",
    "interaction_brush_channel",
    "internal_surface_mesh",
    "live_component",
)
COMPONENT_REQUIRED_PROPERTIES = (
    "active_preset",
    "should_init_system_state_from_preset",
    "simulation_config",
    "output_material",
)
TERRAIN_SAMPLING_PROPERTY_NAMES = ("sample_terrain_elevation",)

REQUIRED_UNREAL_NAMES = (
    "EditorAssetLibrary.load_blueprint_class",
    "MaterialEditingLibrary.recompile_material",
    "MaterialEditingLibrary.get_vector_parameter_names",
    "MaterialEditingLibrary.get_scalar_parameter_names",
    "MaterialEditingLibrary.get_texture_parameter_names",
    "MaterialEditingLibrary.set_material_instance_parent",
    "MaterialEditingLibrary.set_material_instance_vector_parameter_value",
    "MaterialEditingLibrary.get_material_instance_vector_parameter_value",
    "MaterialEditingLibrary.update_material_instance",
    "EditorLoadingAndSavingUtils.get_dirty_map_packages",
    "EditorLoadingAndSavingUtils.get_dirty_content_packages",
    "EditorLoadingAndSavingUtils.new_blank_map",
    "SystemLibrary.get_project_directory",
    "GameplayStatics.get_all_actors_of_class",
    "Material",
    "MaterialFactoryNew",
    "MaterialInstanceConstant",
    "MaterialInstanceConstantFactoryNew",
    "DataAssetFactory",
    "MaterialExpressionConstant",
    "MaterialExpressionConstant3Vector",
    "MaterialExpressionTextureSampleParameter2D",
    "MaterialExpressionVectorParameter",
    "MaterialExpressionScalarParameter",
    "MaterialExpressionWorldPosition",
    "MaterialExpressionComponentMask",
    "MaterialExpressionMultiply",
    "MaterialExpressionSubtract",
    "MaterialExpressionDivide",
    "MaterialExpressionAdd",
    "MaterialExpressionMax",
    "MaterialExpressionLinearInterpolate",
    "MaterialExpressionSaturate",
    "MaterialExpressionAbs",
    "MaterialExpressionOneMinus",
    "BlendMode.BLEND_TRANSLUCENT",
    "MaterialShadingModel.MSM_UNLIT",
    "MaterialProperty.MP_BASE_COLOR",
    "MaterialProperty.MP_ROUGHNESS",
    "MaterialProperty.MP_METALLIC",
    "MaterialProperty.MP_EMISSIVE_COLOR",
    "MaterialProperty.MP_OPACITY",
    "MaterialSamplerType.SAMPLERTYPE_COLOR",
    "MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR",
    "TextureCompressionSettings.TC_DEFAULT",
    "TDFluidPresetAsset.build_config_for_preset_kind",
    "TDFluidPresetAsset.resolve_effective_config",
    "TDFluidPresetKind.WATER",
    "TDFluidPresetKind.MUD",
    "TDFluidPresetKind.BLOOD",
    "TDFluidPresetKind.FIRE",
    "TDFluidPresetKind.FOOTPRINT_PAINTER",
    "TDFluidBrushChannel.CHANNEL_R_MUD",
    "TDFluidBrushChannel.CHANNEL_G_BLOOD",
    "TDFluidBrushChannel.CHANNEL_B_FIRE",
    "TDFluidBrushChannel.CHANNEL_A_WATER",
    "AutoExposureMethod.AEM_MANUAL",
    "RuntimeGenerationType.DYNAMIC",
    "ComponentMobility.STATIC",
    "ComponentMobility.MOVABLE",
    "CollisionEnabled.NO_COLLISION",
    "NavMeshBoundsVolume",
    "RecastNavMesh",
    "PointLight",
    "DirectionalLight",
    "SkyAtmosphere",
    "SkyLight",
    "PostProcessVolume",
    "PlayerStart",
)


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


def asset_disk_path(asset_path, extension):
    relative_parts = asset_path[len("/Game/"):].split("/")
    return project_path("Content", *relative_parts) + extension


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


def check_cpp_contract(actor_class):
    default_preset = unreal.get_default_object(unreal.TDFluidPresetAsset)
    problems = [
        f"TDFluidPresetAsset.{name}" for name in find_missing_properties(default_preset, PRESET_REQUIRED_PROPERTIES)
    ]

    default_actor = unreal.get_default_object(actor_class)
    actor_missing = find_missing_properties(default_actor, ACTOR_REQUIRED_PROPERTIES)
    problems.extend(f"TDFluidLiveActor.{name}" for name in actor_missing)
    if "live_component" in actor_missing:
        return problems

    default_component = default_actor.get_editor_property("live_component")
    if default_component is None:
        problems.append("TDFluidLiveActor.live_component(None)")
        return problems
    problems.extend(
        f"TDFluidLiveComponent.{name}"
        for name in find_missing_properties(default_component, COMPONENT_REQUIRED_PROPERTIES)
    )
    return problems


def locate_terrain_sampling_property(actor_class):
    default_actor = unreal.get_default_object(actor_class)
    default_component = default_actor.get_editor_property("live_component")
    for owner_label, owner in (("actor", default_actor), ("live_component", default_component)):
        if owner is None:
            continue
        for property_name in TERRAIN_SAMPLING_PROPERTY_NAMES:
            if has_editor_property(owner, property_name):
                return owner_label, property_name
    return None


def enable_terrain_sampling(actor, live_component, location):
    owner_label, property_name = location
    owner = actor if owner_label == "actor" else live_component
    owner.set_editor_property(property_name, True)
    if not owner.get_editor_property(property_name):
        raise RuntimeError(f"지형 고도 표본 속성 설정 실패: {owner_label}.{property_name}")


def read_overwrite_request():
    request_path = project_path(*REQUEST_RELATIVE_PATH)
    if not os.path.isfile(request_path):
        return False
    with open(request_path, encoding="utf-8") as request_file:
        request = json.load(request_file)
    return isinstance(request, dict) and request.get("overwrite") is True


def preset_asset_name(short_name):
    return f"DA_TDFluid_{short_name}"


def instance_asset_name(short_name):
    return f"MI_TD_Fluid_{short_name}"


def preset_asset_paths():
    return [f"{PRESET_DIR}/{preset_asset_name(spec['name'])}" for spec in PRESET_SPECS]


def instance_asset_paths():
    return [f"{MATERIAL_DIR}/{instance_asset_name(spec['name'])}" for spec in PRESET_SPECS]


def collect_target_asset_paths():
    return (
        [FLUID_SURFACE_MAT_PATH, FLOOR_MAT_PATH, WALL_MAT_PATH]
        + instance_asset_paths()
        + preset_asset_paths()
        + [ARENA_MAP_PATH]
    )


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


def delete_asset_if_exists(asset_path):
    assets = unreal.EditorAssetLibrary
    if not assets.does_asset_exist(asset_path):
        return False
    if not assets.delete_asset(asset_path) or assets.does_asset_exist(asset_path):
        raise RuntimeError(f"기존 에셋 삭제 실패: {asset_path}")
    return True


def delete_replaceable_assets(editor):
    world = editor.get_editor_world()
    if world.get_outermost().get_name() == ARENA_MAP_PATH:
        if unreal.EditorLoadingAndSavingUtils.new_blank_map(False) is None:
            raise RuntimeError("빈 맵으로 전환하지 못했습니다.")
        unreal.SystemLibrary.collect_garbage()

    deleted = []
    deletion_order = instance_asset_paths() + [FLOOR_MAT_PATH, WALL_MAT_PATH, FLUID_SURFACE_MAT_PATH]
    for asset_path in deletion_order:
        if delete_asset_if_exists(asset_path):
            deleted.append(asset_path)
    return deleted


def open_empty_arena_level(levels, actors):
    if not unreal.EditorAssetLibrary.does_asset_exist(ARENA_MAP_PATH):
        if not levels.new_level(ARENA_MAP_PATH):
            raise RuntimeError(f"테스트 레벨 생성 실패: {ARENA_MAP_PATH}")
        return
    if not levels.load_level(ARENA_MAP_PATH):
        raise RuntimeError(f"기존 테스트 레벨 열기 실패: {ARENA_MAP_PATH}")
    for actor in actors.get_all_level_actors():
        if isinstance(actor, (unreal.WorldSettings, unreal.Brush)):
            continue
        if not actors.destroy_actor(actor):
            raise RuntimeError(f"기존 테스트 레벨 액터 제거 실패: {actor.get_actor_label()}")


def save_asset(asset, asset_path):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"에셋 저장 실패: {asset_path}")


def add_node(material, expression_class, x, y, **properties):
    expression = unreal.MaterialEditingLibrary.create_material_expression(material, expression_class, x, y)
    if expression is None:
        raise RuntimeError(f"재질 노드 생성 실패: {expression_class.__name__}")
    for name, value in properties.items():
        expression.set_editor_property(name, value)
    return expression


def link(source, target, target_input="", source_output=""):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(source, source_output, target, target_input):
        raise RuntimeError(
            f"재질 노드 연결 실패: {source.get_name()}[{source_output}] -> {target.get_name()}.{target_input}"
        )


def connect_material_output(expression, material_property):
    if not unreal.MaterialEditingLibrary.connect_material_property(expression, "", material_property):
        raise RuntimeError(f"재질 출력 연결 실패: {material_property}")


def create_matte_material(asset_path, color_rgb, roughness_value=0.88):
    editing = unreal.MaterialEditingLibrary
    asset_dir, asset_name = asset_path.rsplit("/", 1)
    ensure_directory(asset_dir)

    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name, asset_dir, unreal.Material, unreal.MaterialFactoryNew()
    )
    if material is None:
        raise RuntimeError(f"재질 생성 실패: {asset_path}")

    base_color = add_node(
        material,
        unreal.MaterialExpressionConstant3Vector,
        -280,
        -120,
        constant=unreal.LinearColor(color_rgb[0], color_rgb[1], color_rgb[2], 1.0),
    )
    roughness = add_node(material, unreal.MaterialExpressionConstant, -280, 20, r=roughness_value)
    metallic = add_node(material, unreal.MaterialExpressionConstant, -280, 120, r=0.0)
    connect_material_output(base_color, unreal.MaterialProperty.MP_BASE_COLOR)
    connect_material_output(roughness, unreal.MaterialProperty.MP_ROUGHNESS)
    connect_material_output(metallic, unreal.MaterialProperty.MP_METALLIC)

    errors = list(editing.recompile_material(material))
    if errors:
        raise RuntimeError(f"재질 컴파일 실패: {asset_path}, {errors}")
    save_asset(material, asset_path)
    return material


def choose_sampler_type(texture):
    if texture is None:
        return None
    try:
        compression = texture.get_editor_property("compression_settings")
        is_srgb = bool(texture.get_editor_property("srgb"))
    except Exception:
        return None
    if compression != unreal.TextureCompressionSettings.TC_DEFAULT:
        return None
    if is_srgb:
        return unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
    return unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR


def build_surface_material(default_texture, sampler_type):
    delete_asset_if_exists(FLUID_SURFACE_MAT_PATH)
    ensure_directory(MATERIAL_DIR)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_TD_FluidSurface", MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew()
    )
    if material is None:
        raise RuntimeError(f"유체 표면 재질 생성 실패: {FLUID_SURFACE_MAT_PATH}")

    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)

    texture_class = unreal.MaterialExpressionTextureSampleParameter2D
    vector_class = unreal.MaterialExpressionVectorParameter
    scalar_class = unreal.MaterialExpressionScalarParameter

    density_texture = add_node(
        material,
        texture_class,
        -1100,
        -700,
        parameter_name="DensityPressureTexture",
        texture=default_texture,
        sampler_type=sampler_type,
    )
    velocity_texture = add_node(
        material,
        texture_class,
        -1100,
        -300,
        parameter_name="VelocityTexture",
        texture=default_texture,
        sampler_type=sampler_type,
    )

    world_position = add_node(material, unreal.MaterialExpressionWorldPosition, -2600, 0)
    sim_center = add_node(
        material,
        vector_class,
        -2600,
        260,
        parameter_name="SimCenterWorld",
        default_value=unreal.LinearColor(0.0, 0.0, 0.0, 1.0),
    )
    sim_extents = add_node(
        material,
        vector_class,
        -2600,
        460,
        parameter_name="SimExtentsCm",
        default_value=unreal.LinearColor(2000.0, 2000.0, 0.1, 0.0),
    )
    center_xy = add_node(material, unreal.MaterialExpressionComponentMask, -2350, 260, r=True, g=True, b=False, a=False)
    extents_xy = add_node(material, unreal.MaterialExpressionComponentMask, -2350, 460, r=True, g=True, b=False, a=False)
    half_extents = add_node(material, unreal.MaterialExpressionMultiply, -2100, 460, const_b=0.5)
    region_origin = add_node(material, unreal.MaterialExpressionSubtract, -1850, 320)
    relative_xy = add_node(material, unreal.MaterialExpressionSubtract, -1600, 120)
    link(sim_center, center_xy, source_output="RGB")
    link(sim_extents, extents_xy, source_output="RGB")
    link(extents_xy, half_extents, "A")
    link(center_xy, region_origin, "A")
    link(half_extents, region_origin, "B")
    link(world_position, relative_xy, "A", source_output="XY")
    link(region_origin, relative_xy, "B")

    relative_source = relative_xy
    truncate_class = getattr(unreal, "MaterialExpressionTruncateLWC", None)
    if truncate_class is not None:
        truncated_xy = add_node(material, truncate_class, -1450, 120)
        link(relative_xy, truncated_xy)
        relative_source = truncated_xy

    sample_uv = add_node(material, unreal.MaterialExpressionDivide, -1300, 200)
    link(relative_source, sample_uv, "A")
    link(extents_xy, sample_uv, "B")
    link(sample_uv, density_texture, "UVs")
    link(sample_uv, velocity_texture, "UVs")

    uv_from_center = add_node(material, unreal.MaterialExpressionSubtract, -1300, 520, const_b=0.5)
    uv_distance = add_node(material, unreal.MaterialExpressionAbs, -1100, 520)
    uv_distance_doubled = add_node(material, unreal.MaterialExpressionMultiply, -900, 520, const_b=2.0)
    uv_margin = add_node(material, unreal.MaterialExpressionOneMinus, -700, 520)
    uv_margin_sharpened = add_node(material, unreal.MaterialExpressionMultiply, -500, 520, const_b=500.0)
    uv_inside_pair = add_node(material, unreal.MaterialExpressionSaturate, -300, 520)
    uv_inside_x = add_node(material, unreal.MaterialExpressionComponentMask, -100, 460, r=True, g=False, b=False, a=False)
    uv_inside_y = add_node(material, unreal.MaterialExpressionComponentMask, -100, 580, r=False, g=True, b=False, a=False)
    inside_region = add_node(material, unreal.MaterialExpressionMultiply, 100, 520)
    link(sample_uv, uv_from_center, "A")
    link(uv_from_center, uv_distance)
    link(uv_distance, uv_distance_doubled, "A")
    link(uv_distance_doubled, uv_margin)
    link(uv_margin, uv_margin_sharpened, "A")
    link(uv_margin_sharpened, uv_inside_pair)
    link(uv_inside_pair, uv_inside_x)
    link(uv_inside_pair, uv_inside_y)
    link(uv_inside_x, inside_region, "A")
    link(uv_inside_y, inside_region, "B")

    total_density = add_node(material, unreal.MaterialExpressionAdd, -700, -500)
    link(density_texture, total_density, "A", source_output="R")
    link(density_texture, total_density, "B", source_output="B")
    safe_total_density = add_node(material, unreal.MaterialExpressionMax, -500, -500, const_b=0.0001)
    link(total_density, safe_total_density, "A")
    selective_weight = add_node(material, unreal.MaterialExpressionDivide, -300, -500)
    link(density_texture, selective_weight, "A", source_output="B")
    link(safe_total_density, selective_weight, "B")

    base_tint = add_node(
        material,
        vector_class,
        -700,
        -900,
        parameter_name="BaseTint",
        default_value=unreal.LinearColor(0.05, 0.28, 0.55, 1.0),
    )
    selective_tint = add_node(
        material,
        vector_class,
        -700,
        -760,
        parameter_name="SelectiveTint",
        default_value=unreal.LinearColor(0.55, 0.02, 0.03, 1.0),
    )
    tint_mix = add_node(material, unreal.MaterialExpressionLinearInterpolate, -100, -700)
    link(base_tint, tint_mix, "A", source_output="RGB")
    link(selective_tint, tint_mix, "B", source_output="RGB")
    link(selective_weight, tint_mix, "Alpha")

    speed_brightness = add_node(material, scalar_class, -700, -100, parameter_name="SpeedBrightness", default_value=0.6)
    speed_gain = add_node(material, unreal.MaterialExpressionMultiply, -500, -200)
    brightness = add_node(material, unreal.MaterialExpressionAdd, -300, -200, const_b=1.0)
    emissive_color = add_node(material, unreal.MaterialExpressionMultiply, 100, -400)
    link(velocity_texture, speed_gain, "A", source_output="B")
    link(speed_brightness, speed_gain, "B")
    link(speed_gain, brightness, "A")
    link(tint_mix, emissive_color, "A")
    link(brightness, emissive_color, "B")

    density_presence = add_node(material, unreal.MaterialExpressionSaturate, -300, 100)
    opacity_density = add_node(material, unreal.MaterialExpressionMultiply, -100, 100)
    opacity_scale = add_node(material, scalar_class, -100, 240, parameter_name="OpacityScale", default_value=0.9)
    opacity_scaled = add_node(material, unreal.MaterialExpressionMultiply, 100, 140)
    opacity_final = add_node(material, unreal.MaterialExpressionMultiply, 300, 200)
    link(total_density, density_presence)
    link(density_presence, opacity_density, "A")
    link(density_texture, opacity_density, "B", source_output="A")
    link(opacity_density, opacity_scaled, "A")
    link(opacity_scale, opacity_scaled, "B")
    link(opacity_scaled, opacity_final, "A")
    link(inside_region, opacity_final, "B")

    connect_material_output(emissive_color, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    connect_material_output(opacity_final, unreal.MaterialProperty.MP_OPACITY)

    errors = [str(message) for message in unreal.MaterialEditingLibrary.recompile_material(material)]
    return material, errors


def verify_surface_material_parameters(material):
    editing = unreal.MaterialEditingLibrary
    found = {
        "vector": {str(name) for name in editing.get_vector_parameter_names(material)},
        "scalar": {str(name) for name in editing.get_scalar_parameter_names(material)},
        "texture": {str(name) for name in editing.get_texture_parameter_names(material)},
    }
    expected = {
        "vector": MATERIAL_VECTOR_PARAMETERS,
        "scalar": MATERIAL_SCALAR_PARAMETERS,
        "texture": MATERIAL_TEXTURE_PARAMETERS,
    }
    missing = [f"{kind}:{name}" for kind, names in expected.items() for name in names if name not in found[kind]]
    if missing:
        raise RuntimeError("유체 표면 재질 파라미터 없음: " + ", ".join(missing))


def read_material_statistics(material):
    try:
        statistics = unreal.MaterialEditingLibrary.get_statistics(material)
        return {
            name: statistics.get_editor_property(name)
            for name in ("num_vertex_shader_instructions", "num_pixel_shader_instructions", "num_samplers")
        }
    except Exception:
        return {}


def create_fluid_surface_material():
    attempts = []
    for texture_path in DEFAULT_TEXTURE_PATHS:
        texture = unreal.EditorAssetLibrary.load_asset(texture_path)
        sampler_type = choose_sampler_type(texture)
        if sampler_type is None:
            attempts.append(f"{texture_path}: 기본 텍스처로 쓸 수 없음")
            continue

        material, errors = build_surface_material(texture, sampler_type)
        if errors:
            attempts.append(f"{texture_path}: {errors}")
            continue

        verify_surface_material_parameters(material)
        save_asset(material, FLUID_SURFACE_MAT_PATH)
        summary = {
            "default_texture": texture_path,
            "sampler_type": str(sampler_type),
            "blend_mode": str(material.get_editor_property("blend_mode")),
            "shading_model": str(material.get_editor_property("shading_model")),
            "compile_errors": [],
            "statistics": read_material_statistics(material),
        }
        return material, summary
    raise RuntimeError("유체 표면 재질 컴파일 실패: " + " | ".join(attempts))


def create_material_instance(surface_material, spec):
    editing = unreal.MaterialEditingLibrary
    asset_name = instance_asset_name(spec["name"])
    asset_path = f"{MATERIAL_DIR}/{asset_name}"
    instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name, MATERIAL_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew()
    )
    if instance is None:
        raise RuntimeError(f"재질 인스턴스 생성 실패: {asset_path}")

    editing.set_material_instance_parent(instance, surface_material)
    for parameter_name, rgb in spec["tints"].items():
        editing.set_material_instance_vector_parameter_value(
            instance, parameter_name, unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0)
        )
    editing.update_material_instance(instance)

    for parameter_name, rgb in spec["tints"].items():
        stored = editing.get_material_instance_vector_parameter_value(instance, parameter_name)
        error = max(abs(stored.r - rgb[0]), abs(stored.g - rgb[1]), abs(stored.b - rgb[2]))
        if error > 0.001:
            raise RuntimeError(f"재질 인스턴스 색 설정 확인 실패: {asset_path}.{parameter_name}")
    save_asset(instance, asset_path)
    return instance


def load_or_create_preset(spec):
    asset_name = preset_asset_name(spec["name"])
    asset_path = f"{PRESET_DIR}/{asset_name}"
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        preset = unreal.EditorAssetLibrary.load_asset(asset_path)
        if not isinstance(preset, unreal.TDFluidPresetAsset):
            raise RuntimeError(f"기존 프리셋 에셋 형식 불일치: {asset_path}")
        return preset, asset_path

    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.TDFluidPresetAsset)
    preset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name, PRESET_DIR, unreal.TDFluidPresetAsset, factory
    )
    if preset is None:
        raise RuntimeError(f"프리셋 생성 실패: {asset_path}")
    return preset, asset_path


def create_fluid_presets(output_materials, plane_mesh):
    ensure_directory(PRESET_DIR)
    created = {}
    for spec in PRESET_SPECS:
        preset_kind = getattr(unreal.TDFluidPresetKind, spec["kind"])
        brush_channel = getattr(unreal.TDFluidBrushChannel, spec["channel"])
        preset, asset_path = load_or_create_preset(spec)

        config = unreal.TDFluidPresetAsset.build_config_for_preset_kind(preset_kind)
        config.set_editor_property("sim_extent_cm", unreal.Vector2D(*spec["extent_cm"]))
        config.set_editor_property("grid_resolution", GRID_RESOLUTION)

        preset.set_editor_property("preset_kind", preset_kind)
        preset.set_editor_property("default_channel", brush_channel)
        preset.set_editor_property("simulation_config", config)
        preset.set_editor_property("override_preset_defaults", True)
        preset.set_editor_property("output_material", output_materials[spec["name"]])
        preset.set_editor_property("surface_mesh", plane_mesh)

        effective = preset.resolve_effective_config()
        effective_extent = effective.get_editor_property("sim_extent_cm")
        if (
            abs(effective_extent.x - spec["extent_cm"][0]) > 0.01
            or abs(effective_extent.y - spec["extent_cm"][1]) > 0.01
            or effective.get_editor_property("grid_resolution") != GRID_RESOLUTION
        ):
            raise RuntimeError(f"프리셋 구성 덮어쓰기가 유효 구성에 반영되지 않았습니다: {asset_path}")
        save_asset(preset, asset_path)
        created[spec["name"]] = preset
    return created


def build_ramp_layout():
    if not 0.0 < RAMP_PITCH_DEGREES <= RAMP_MAX_PITCH_DEGREES:
        raise RuntimeError(f"경사로 각도 범위 초과: {RAMP_PITCH_DEGREES}")

    pitch = math.radians(RAMP_PITCH_DEGREES)
    sin_pitch = math.sin(pitch)
    cos_pitch = math.cos(pitch)
    slope_ratio = math.tan(pitch)
    half_length = RAMP_LENGTH_CM * 0.5
    half_thickness = RAMP_THICKNESS_CM * 0.5
    center_x, center_y = RAMP_CENTER_XY

    center_z = half_length * sin_pitch - half_thickness * cos_pitch
    top_center_x = center_x - half_thickness * sin_pitch
    top_center_z = center_z + half_thickness * cos_pitch

    def top_height_at(world_x):
        return top_center_z + (world_x - top_center_x) * slope_ratio

    low_end_top_x = center_x - half_length * cos_pitch - half_thickness * sin_pitch
    high_end_top_x = center_x + half_length * cos_pitch - half_thickness * sin_pitch
    high_end_bottom_x = center_x + half_length * cos_pitch + half_thickness * sin_pitch
    support_height = top_height_at(high_end_bottom_x) - RAMP_THICKNESS_CM / cos_pitch

    return {
        "center": (center_x, center_y, center_z),
        "pitch_degrees": RAMP_PITCH_DEGREES,
        "slope_ratio": slope_ratio,
        "cos_pitch": cos_pitch,
        "top_height_at": top_height_at,
        "top_height_low_end_cm": top_height_at(low_end_top_x),
        "top_height_high_end_cm": top_height_at(high_end_top_x),
        "support_height_cm": support_height,
        "support_center": (high_end_bottom_x - RAMP_SUPPORT_DEPTH_CM * 0.5, center_y, support_height * 0.5),
        "x_range_cm": (low_end_top_x, high_end_bottom_x),
        "y_range_cm": (center_y - RAMP_WIDTH_CM * 0.5, center_y + RAMP_WIDTH_CM * 0.5),
    }


def footprints_overlap(first, second):
    for axis in (0, 1):
        reach = (first["extent_cm"][axis] + second["extent_cm"][axis]) * 0.5
        if abs(first["location"][axis] - second["location"][axis]) >= reach:
            return False
    return True


def find_overlapping_static_footprints(records):
    static_records = [record for record in records if not record["follows_player"]]
    overlaps = []
    for index, first in enumerate(static_records):
        for second in static_records[index + 1:]:
            if footprints_overlap(first, second):
                overlaps.append([first["label"], second["label"]])
    return overlaps


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

    missing_names = find_missing_unreal_names()
    if missing_names:
        raise RuntimeError("언리얼 Python 이름 없음: " + ", ".join(missing_names))
    if editor.get_game_world() is not None:
        raise RuntimeError("PIE를 종료한 뒤 실행하세요.")
    if editor.get_editor_world() is None:
        raise RuntimeError("에디터 월드가 없습니다.")

    overwrite = read_overwrite_request()
    existing_paths = [path for path in collect_target_asset_paths() if assets.does_asset_exist(path)]
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
    cube = assets.load_asset("/Engine/BasicShapes/Cube")
    cylinder = assets.load_asset("/Engine/BasicShapes/Cylinder")
    plane = assets.load_asset(PLANE_MESH_PATH)
    target_class = unreal.load_class(None, DAMAGE_TARGET_CLASS_PATH)
    fluid_actor_class = unreal.load_class(None, FLUID_ACTOR_CLASS_PATH)
    required = {
        "게임 모드": game_mode,
        "큐브": cube,
        "기둥": cylinder,
        "평면": plane,
        "데미지 대상 클래스": target_class,
        "유체 액터 클래스": fluid_actor_class,
    }
    missing = [name for name, value in required.items() if value is None]
    if missing:
        raise RuntimeError("필수 에셋 또는 클래스 없음: " + ", ".join(missing))

    contract_problems = check_cpp_contract(fluid_actor_class)
    if contract_problems:
        raise RuntimeError("C++ 계약 속성 없음(재빌드와 에디터 재시작 필요): " + ", ".join(contract_problems))
    terrain_property_location = locate_terrain_sampling_property(fluid_actor_class)
    if terrain_property_location is None:
        raise RuntimeError(
            "지형 고도 표본 속성 없음(재빌드와 에디터 재시작 필요): " + ", ".join(TERRAIN_SAMPLING_PROPERTY_NAMES)
        )

    ramp = build_ramp_layout()
    deleted_assets = delete_replaceable_assets(editor) if overwrite else []

    print("단계: 재질 생성")
    floor_mat = create_matte_material(FLOOR_MAT_PATH, (0.06, 0.07, 0.08))
    wall_mat = create_matte_material(WALL_MAT_PATH, (0.10, 0.11, 0.13))
    fluid_surface_mat, surface_summary = create_fluid_surface_material()
    material_instances = {spec["name"]: create_material_instance(fluid_surface_mat, spec) for spec in PRESET_SPECS}

    print("단계: 프리셋 생성")
    presets = create_fluid_presets(material_instances, plane)

    print("단계: 레벨 생성")
    ensure_directory(MAP_DIR)
    open_empty_arena_level(levels, actors)

    world = editor.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", game_mode)
    created = []

    def finish(actor, label, folder):
        if actor is None:
            raise RuntimeError(f"액터 생성 실패: {label}")
        actor.set_actor_label("TDGen_" + label)
        actor.set_folder_path("TDGen/FluidFX/" + folder)
        created.append(actor)
        return actor

    def spawn(actor_class, location, label, folder, rotation=None):
        actor = actors.spawn_actor_from_class(actor_class, unreal.Vector(*location), rotation or unreal.Rotator())
        return finish(actor, label, folder)

    def place_mesh(mesh, location, scale, material, label, rotation=None):
        actor = actors.spawn_actor_from_object(mesh, unreal.Vector(*location), rotation or unreal.Rotator())
        finish(actor, label, "Arena")
        actor.set_actor_scale3d(unreal.Vector(*scale))
        component = actor.static_mesh_component
        component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        component.set_material(0, material)
        return actor

    place_mesh(cube, (150, 0, -25), (32, 26, 0.5), floor_mat, "FluidArenaFloor")
    for label, location, scale in (
        ("NorthRail", (150, -1250, 40), (32, 0.6, 0.8)),
        ("SouthRail", (150, 1250, 40), (32, 0.6, 0.8)),
        ("WestRail", (-1400, 0, 40), (0.6, 26, 0.8)),
        ("EastRail", (1700, 0, 40), (0.6, 26, 0.8)),
    ):
        place_mesh(cube, location, scale, wall_mat, label)

    for index, (x, y) in enumerate(((-1250, -1100), (-1250, 1100), (1550, -1100), (1550, 1100))):
        place_mesh(cube, (x, y, 30), (1.6, 1.6, 0.6), wall_mat, f"PillarBase_{index}")
        place_mesh(cylinder, (x, y, 165), (0.9, 0.9, 2.1), wall_mat, f"Pillar_{index}")
        lamp = spawn(unreal.PointLight, (x, y, 330), f"ArenaLight_{index}", "Lighting")
        lamp.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        lamp.light_component.set_intensity(180.0)
        lamp.light_component.set_attenuation_radius(650.0)
        lamp.light_component.set_cast_shadows(False)
        lamp.light_component.set_light_color(unreal.LinearColor(0.25, 0.65, 1.0, 1.0))

    sun = spawn(unreal.DirectionalLight, (0, 0, 1500), "FluidArenaSun", "Lighting", unreal.Rotator(pitch=-55, yaw=-35))
    sun.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_intensity(5.5)
    sun.light_component.set_light_color(unreal.LinearColor(0.85, 0.90, 1.0, 1.0))
    sun.light_component.set_editor_property("atmosphere_sun_light", True)
    spawn(unreal.SkyAtmosphere, (0, 0, 0), "FluidArenaAtmosphere", "Lighting")
    sky = spawn(unreal.SkyLight, (0, 0, 1000), "FluidArenaSkyLight", "Lighting")
    sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", True)
    sky.light_component.set_intensity(0.9)

    post_process = spawn(unreal.PostProcessVolume, (0, 0, 0), "FluidArenaExposure", "Lighting")
    post_process.set_editor_property("unbound", True)
    settings = post_process.settings
    for name, value in (
        ("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL),
        ("auto_exposure_apply_physical_camera_exposure", False),
        ("auto_exposure_bias", 0.0),
        ("bloom_intensity", 0.35),
        ("vignette_intensity", 0.2),
    ):
        settings.set_editor_property("override_" + name, True)
        settings.set_editor_property(name, value)
    post_process.set_editor_property("settings", settings)

    spawn(unreal.PlayerStart, (-450, 0, 96), "FluidArenaPlayerStart", "Gameplay")

    target_locations = [(x, y, 65) for x in (150, 400, 650) for y in (-180, 180)]
    spawned_targets = [
        spawn(target_class, location, f"DamageTarget_{index + 1}", "Gameplay")
        for index, location in enumerate(target_locations)
    ]

    ramp_center = ramp["center"]
    place_mesh(
        cube,
        ramp_center,
        (RAMP_LENGTH_CM / 100.0, RAMP_WIDTH_CM / 100.0, RAMP_THICKNESS_CM / 100.0),
        wall_mat,
        "TerrainRamp",
        unreal.Rotator(pitch=ramp["pitch_degrees"], yaw=0.0, roll=0.0),
    )
    place_mesh(
        cube,
        ramp["support_center"],
        (RAMP_SUPPORT_DEPTH_CM / 100.0, RAMP_WIDTH_CM / 100.0, ramp["support_height_cm"] / 100.0),
        wall_mat,
        "TerrainRampSupport",
    )

    fluid_records = []

    def spawn_fluid_actor(label, location, preset_name, rotation=None, follow_player=False, sample_terrain=False):
        actor = spawn(fluid_actor_class, location, label, "FluidContainers", rotation)
        preset = presets[preset_name]
        material = material_instances[preset_name]
        effective_config = preset.resolve_effective_config()
        extent = effective_config.get_editor_property("sim_extent_cm")

        actor.set_editor_property("should_follow_player_pawn", follow_player)
        actor.set_editor_property("should_use_activation_volume", False)
        actor.set_editor_property("interaction_brush_channel", preset.get_editor_property("default_channel"))

        surface_mesh = actor.get_editor_property("internal_surface_mesh")
        surface_mesh.set_static_mesh(plane)
        surface_mesh.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        surface_mesh.set_collision_profile_name("NoCollision")
        surface_mesh.set_relative_scale3d(unreal.Vector(extent.x / 100.0, extent.y / 100.0, 1.0))
        surface_mesh.set_material(0, material)

        live_component = actor.get_editor_property("live_component")
        live_component.set_editor_property("active_preset", preset)
        live_component.set_editor_property("should_init_system_state_from_preset", True)
        live_component.set_editor_property("simulation_config", effective_config)
        live_component.set_editor_property("output_material", material)

        if sample_terrain:
            enable_terrain_sampling(actor, live_component, terrain_property_location)

        fluid_records.append({
            "label": actor.get_actor_label(),
            "preset": preset_asset_name(preset_name),
            "extent_cm": [extent.x, extent.y],
            "grid_resolution": effective_config.get_editor_property("grid_resolution"),
            "location": list(location),
            "pitch_degrees": rotation.pitch if rotation is not None else 0.0,
            "follows_player": follow_player,
            "samples_terrain_elevation": sample_terrain,
            "output_material": material.get_path_name(),
        })
        return actor

    spawn_fluid_actor("FluidArena_CombatPool", (400, 0, 3), "Blood")
    spawn_fluid_actor("FluidArena_MudZone", (-750, -760, 6), "Mud")
    spawn_fluid_actor("FluidArena_PlayerTrail", (-450, 0, 9), "Water", follow_player=True)

    ramp_fluid_x, ramp_fluid_y = RAMP_CENTER_XY
    ramp_fluid_z = ramp["top_height_at"](ramp_fluid_x) + FLUID_SURFACE_LIFT_CM / ramp["cos_pitch"]
    spawn_fluid_actor(
        "FluidArena_RampWater",
        (ramp_fluid_x, ramp_fluid_y, ramp_fluid_z),
        "Water",
        rotation=unreal.Rotator(pitch=ramp["pitch_degrees"], yaw=0.0, roll=0.0),
        sample_terrain=True,
    )

    overlaps = find_overlapping_static_footprints(fluid_records)
    if overlaps:
        raise RuntimeError(f"고정 유체 영역이 겹칩니다: {overlaps}")

    navigation = spawn(unreal.NavMeshBoundsVolume, (150, 0, 200), "FluidArenaNavigation", "Gameplay")
    navigation.set_actor_scale3d(unreal.Vector(16, 13, 4))
    nav_origin, nav_extent = navigation.get_actor_bounds(False)
    if nav_extent.x < 1500 or nav_extent.y < 1200:
        raise RuntimeError(f"내비게이션 브러시 범위 검증 실패: {nav_extent}")
    for nav_mesh in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RecastNavMesh):
        nav_mesh.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC)
        nav_mesh.set_editor_property("is_world_partitioned", False)

    print("단계: 레벨 저장")
    if not levels.save_current_level():
        raise RuntimeError(f"테스트 레벨 저장 실패: {ARENA_MAP_PATH}")

    saved_asset_paths = (
        [FLUID_SURFACE_MAT_PATH, FLOOR_MAT_PATH, WALL_MAT_PATH]
        + instance_asset_paths()
        + preset_asset_paths()
    )
    missing_files = find_missing_saved_files(saved_asset_paths, ARENA_MAP_PATH)
    if missing_files:
        raise RuntimeError("저장 파일 확인 실패: " + ", ".join(missing_files))

    report = {
        "map": ARENA_MAP_PATH,
        "overwrite": overwrite,
        "deleted_before_create": deleted_assets,
        "created_assets": saved_asset_paths + [ARENA_MAP_PATH],
        "surface_material": surface_summary,
        "presets": [
            {
                "asset": path,
                "extent_cm": list(spec["extent_cm"]),
                "grid_resolution": GRID_RESOLUTION,
                "output_material": f"{MATERIAL_DIR}/{instance_asset_name(spec['name'])}",
            }
            for path, spec in zip(preset_asset_paths(), PRESET_SPECS)
        ],
        "fluid_actors": fluid_records,
        "terrain_sampling_property": {
            "owner": terrain_property_location[0],
            "name": terrain_property_location[1],
        },
        "ramp": {
            "center": list(ramp["center"]),
            "size_cm": [RAMP_LENGTH_CM, RAMP_WIDTH_CM, RAMP_THICKNESS_CM],
            "pitch_degrees": ramp["pitch_degrees"],
            "slope_ratio": ramp["slope_ratio"],
            "top_height_low_end_cm": ramp["top_height_low_end_cm"],
            "top_height_high_end_cm": ramp["top_height_high_end_cm"],
            "support_height_cm": ramp["support_height_cm"],
            "x_range_cm": list(ramp["x_range_cm"]),
            "y_range_cm": list(ramp["y_range_cm"]),
            "fluid_actor": "TDGen_FluidArena_RampWater",
        },
        "damage_targets": len(spawned_targets),
        "created_actors": len(created),
        "navigation_extent": [nav_extent.x, nav_extent.y, nav_extent.z],
        "saved": True,
    }
    report_path = write_report(report)
    print(json.dumps(report, ensure_ascii=False, default=str))
    print("보고서: " + report_path)


if __name__ == "__main__":
    main()
