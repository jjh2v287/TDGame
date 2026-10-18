"""실행 환경: TDGame 언리얼 에디터 안에서 FluidFX 볼륨 연기 무대(HARD·SOFT 나란히, 연기 위를 도는 포인트 라이트)를 만든다. 먼저 editor_make_fluid_volume_material.py 가 성공해 있어야 한다.
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_smoke_stage.py
출력: /Game/FluidFX/Presets/DA_TDFluid_SmokeHard·SmokeSoft, /Game/FluidFX/Blueprints/BP_TDFluidLightPivot, /Game/FluidFX/Maps/LV_TDFluidSmokeStage, Saved/FluidFX/fluid_smoke_stage.json
상태: 검증됨(2026-10-18 FX-1 LV_TDFluidSmokeStage 생성·쇼케이스 성공, D-49). 프리셋·맵이 이미 있으면 Saved/FluidFX/fluid_smoke_stage_request.json 의 {"overwrite": true} 가 있을 때만 갱신하고(맵은 지우지 않고 열어서 액터만 비움), 미저장 작업이 있으면 중단한다.
"""
import json
import os

import unreal


FLUID_ROOT = "/Game/FluidFX"
MATERIAL_DIR = f"{FLUID_ROOT}/Materials"
PRESET_DIR = f"{FLUID_ROOT}/Presets"
BLUEPRINT_DIR = f"{FLUID_ROOT}/Blueprints"
MAP_DIR = f"{FLUID_ROOT}/Maps"

STAGE_MAP_PATH = f"{MAP_DIR}/LV_TDFluidSmokeStage"
FLOOR_MAT_PATH = f"{MATERIAL_DIR}/M_TD_FluidSmokeFloor"
WALL_MAT_PATH = f"{MATERIAL_DIR}/M_TD_FluidSmokeWall"
PIVOT_BLUEPRINT_NAME = "BP_TDFluidLightPivot"
PIVOT_BLUEPRINT_PATH = f"{BLUEPRINT_DIR}/{PIVOT_BLUEPRINT_NAME}"
GAME_MODE_PATH = "/Game/Combat/Blueprints/BP_TDCombatGameMode"

CUBE_MESH_PATH = "/Engine/BasicShapes/Cube"
FLUID_ACTOR_CLASS_PATH = "/Script/TDGame.TDFluidLiveActor"

REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_smoke_stage_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_smoke_stage.json")

SMOKE_SPECS = (
    {
        "name": "SmokeHard",
        "label": "SmokeHard",
        "instance_path": f"{MATERIAL_DIR}/MI_TD_FluidVolume_SmokeHard",
        "center_xy": (-650.0, 0.0),
    },
    {
        "name": "SmokeSoft",
        "label": "SmokeSoft",
        "instance_path": f"{MATERIAL_DIR}/MI_TD_FluidVolume_SmokeSoft",
        "center_xy": (650.0, 0.0),
    },
)

SMOKE_ACTOR_Z_CM = 1.0
VOLUME_HEIGHT_CM = 140.0
MOUSE_BRUSH_RADIUS_CM = 120.0
EXPECTED_GRID_RESOLUTION = 256
EXPECTED_EXTENT_CM = 700.0

LIGHT_ORBIT_RADIUS_CM = 250.0
LIGHT_HEIGHT_CM = 180.0
LIGHT_ROTATION_DEGREES_PER_SEC = 50.0
LIGHT_COLOR = (1.0, 0.42, 0.12)
LIGHT_INTENSITY = 900.0
LIGHT_ATTENUATION_RADIUS_CM = 800.0

FLOOR_COLOR = (0.025, 0.027, 0.032)
WALL_COLOR = (0.055, 0.055, 0.065)
PLAYER_START = (0.0, 0.0, 96.0)
NAVIGATION_SCALE = (13.0, 8.0, 4.0)
NAVIGATION_MIN_EXTENT_CM = (1250.0, 750.0)

PRESET_REQUIRED_PROPERTIES = (
    "preset_kind",
    "simulation_config",
    "default_channel",
    "override_preset_defaults",
    "volume_material",
    "volume_mesh",
    "volume_height_cm",
)
ACTOR_REQUIRED_PROPERTIES = (
    "should_follow_player_pawn",
    "should_use_activation_volume",
    "interaction_brush_channel",
    "live_component",
    "volume_mesh_component",
    "volume_light_actor",
    "accept_mouse_brush",
    "mouse_brush_radius_cm",
)
COMPONENT_REQUIRED_PROPERTIES = (
    "active_preset",
    "should_init_system_state_from_preset",
    "simulation_config",
)
CONFIG_REQUIRED_PROPERTIES = (
    "ambient_density",
    "ambient_refill_per_sec",
    "actor_brush_density_strength",
    "actor_brush_puncture_cm_per_sec",
)

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
    "BlueprintFactory",
    "Blueprint",
    "BlueprintEditorLibrary.compile_blueprint",
    "SubobjectDataSubsystem",
    "SubobjectDataBlueprintFunctionLibrary.get_object",
    "AddNewSubobjectParams",
    "RotatingMovementComponent",
    "AttachmentRule.KEEP_WORLD",
    "MaterialExpressionConstant",
    "MaterialExpressionConstant3Vector",
    "BlendMode.BLEND_TRANSLUCENT",
    "MaterialProperty.MP_BASE_COLOR",
    "MaterialProperty.MP_ROUGHNESS",
    "MaterialProperty.MP_METALLIC",
    "TDFluidPresetAsset.build_config_for_preset_kind",
    "TDFluidPresetKind.SMOKE",
    "TDFluidBrushChannel.CHANNEL_R_MUD",
    "AutoExposureMethod.AEM_MANUAL",
    "RuntimeGenerationType.DYNAMIC",
    "ComponentMobility.STATIC",
    "ComponentMobility.MOVABLE",
    "NavMeshBoundsVolume",
    "RecastNavMesh",
    "PointLight",
    "DirectionalLight",
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

    default_config = unreal.TDFluidPresetAsset.build_config_for_preset_kind(unreal.TDFluidPresetKind.SMOKE)
    problems.extend(
        f"TDFluidSimulationConfig.{name}" for name in find_missing_properties(default_config, CONFIG_REQUIRED_PROPERTIES)
    )

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
    return [preset_asset_path(spec) for spec in SMOKE_SPECS] + [STAGE_MAP_PATH]


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


def save_asset(asset, asset_path):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"에셋 저장 실패: {asset_path}")


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


def load_or_create_matte_material(asset_path, color_rgb, roughness_value=0.9):
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(asset_path):
        return assets.load_asset(asset_path), False

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

    errors = [str(item) for item in editing.recompile_material(material) or []]
    if errors:
        raise RuntimeError(f"재질 컴파일 실패: {asset_path}, {errors}")
    save_asset(material, asset_path)
    return material, True


def add_rotating_movement(blueprint):
    subobjects = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    handles = list(subobjects.k2_gather_subobject_data_for_blueprint(blueprint))
    if not handles:
        raise RuntimeError("피벗 블루프린트 서브오브젝트 목록이 비었습니다.")
    params = unreal.AddNewSubobjectParams()
    params.set_editor_property("parent_handle", handles[0])
    params.set_editor_property("new_class", unreal.RotatingMovementComponent)
    params.set_editor_property("blueprint_context", blueprint)
    new_handle, failure_reason = subobjects.add_new_subobject(params)
    if str(failure_reason):
        raise RuntimeError("회전 이동 컴포넌트 추가 실패: " + str(failure_reason))
    data = subobjects.k2_find_subobject_data_from_handle(new_handle)
    component = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(data)
    if not isinstance(component, unreal.RotatingMovementComponent):
        raise RuntimeError("회전 이동 컴포넌트 템플릿을 찾지 못했습니다.")
    component.set_editor_property("rotation_rate", unreal.Rotator(0.0, 0.0, LIGHT_ROTATION_DEGREES_PER_SEC))


def load_or_create_pivot_blueprint():
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(PIVOT_BLUEPRINT_PATH):
        return assets.load_blueprint_class(PIVOT_BLUEPRINT_PATH), False

    ensure_directory(BLUEPRINT_DIR)
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.Actor)
    blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        PIVOT_BLUEPRINT_NAME, BLUEPRINT_DIR, unreal.Blueprint, factory
    )
    if blueprint is None:
        raise RuntimeError(f"피벗 블루프린트 생성 실패: {PIVOT_BLUEPRINT_PATH}")
    add_rotating_movement(blueprint)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    save_asset(blueprint, PIVOT_BLUEPRINT_PATH)
    return assets.load_blueprint_class(PIVOT_BLUEPRINT_PATH), True


def load_or_create_preset(spec):
    asset_path = preset_asset_path(spec)
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        preset = unreal.EditorAssetLibrary.load_asset(asset_path)
        if not isinstance(preset, unreal.TDFluidPresetAsset):
            raise RuntimeError(f"기존 프리셋 에셋 형식 불일치: {asset_path}")
        return preset

    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.TDFluidPresetAsset)
    preset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        preset_asset_name(spec), PRESET_DIR, unreal.TDFluidPresetAsset, factory
    )
    if preset is None:
        raise RuntimeError(f"프리셋 생성 실패: {asset_path}")
    return preset


def configure_smoke_preset(spec, volume_material, cube):
    preset_kind = unreal.TDFluidPresetKind.SMOKE
    preset = load_or_create_preset(spec)
    preset.set_editor_property("preset_kind", preset_kind)
    preset.set_editor_property("default_channel", unreal.TDFluidBrushChannel.CHANNEL_R_MUD)
    preset.set_editor_property("override_preset_defaults", False)
    preset.set_editor_property("simulation_config", unreal.TDFluidPresetAsset.build_config_for_preset_kind(preset_kind))
    preset.set_editor_property("volume_material", volume_material)
    preset.set_editor_property("volume_mesh", cube)
    preset.set_editor_property("volume_height_cm", VOLUME_HEIGHT_CM)

    effective = preset.resolve_effective_config()
    extent = effective.get_editor_property("sim_extent_cm")
    if (
        effective.get_editor_property("grid_resolution") != EXPECTED_GRID_RESOLUTION
        or abs(extent.x - EXPECTED_EXTENT_CM) > 0.01
        or abs(extent.y - EXPECTED_EXTENT_CM) > 0.01
        or effective.get_editor_property("ambient_density") <= 0.0
    ):
        raise RuntimeError(f"연기 프리셋 공장값이 유효 구성에 반영되지 않았습니다: {preset_asset_path(spec)}")
    stored_material = preset.get_editor_property("volume_material")
    if stored_material is None or stored_material.get_path_name() != volume_material.get_path_name():
        raise RuntimeError(f"프리셋 볼륨 재질 설정 확인 실패: {preset_asset_path(spec)}")
    save_asset(preset, preset_asset_path(spec))
    return preset, effective


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
    cube = assets.load_asset(CUBE_MESH_PATH)
    fluid_actor_class = unreal.load_class(None, FLUID_ACTOR_CLASS_PATH)
    volume_materials = {spec["name"]: assets.load_asset(spec["instance_path"]) for spec in SMOKE_SPECS}
    required = {"게임 모드": game_mode, "큐브": cube, "유체 액터 클래스": fluid_actor_class}
    required.update({f"볼륨 재질 인스턴스 {name}": material for name, material in volume_materials.items()})
    missing = [name for name, value in required.items() if value is None]
    if missing:
        raise RuntimeError(
            "필수 에셋 또는 클래스 없음(볼륨 재질은 editor_make_fluid_volume_material.py 먼저): " + ", ".join(missing)
        )

    contract_problems = check_cpp_contract(fluid_actor_class)
    if contract_problems:
        raise RuntimeError("C++ 계약 속성 없음(재빌드와 에디터 재시작 필요): " + ", ".join(contract_problems))

    print("단계: 재질·블루프린트 준비")
    floor_mat, floor_created = load_or_create_matte_material(FLOOR_MAT_PATH, FLOOR_COLOR)
    wall_mat, wall_created = load_or_create_matte_material(WALL_MAT_PATH, WALL_COLOR)
    pivot_class, pivot_created = load_or_create_pivot_blueprint()
    if pivot_class is None:
        raise RuntimeError(f"피벗 블루프린트 클래스 로드 실패: {PIVOT_BLUEPRINT_PATH}")

    print("단계: 프리셋 생성")
    ensure_directory(PRESET_DIR)
    presets = {}
    effective_configs = {}
    for spec in SMOKE_SPECS:
        presets[spec["name"]], effective_configs[spec["name"]] = configure_smoke_preset(
            spec, volume_materials[spec["name"]], cube
        )

    print("단계: 레벨 생성")
    ensure_directory(MAP_DIR)
    open_empty_stage_level(levels, actors)

    world = editor.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", game_mode)
    created = []

    def finish(actor, label, folder):
        if actor is None:
            raise RuntimeError(f"액터 생성 실패: {label}")
        actor.set_actor_label("TDGen_" + label)
        actor.set_folder_path("TDGen/FluidFX/SmokeStage/" + folder)
        created.append(actor)
        return actor

    def spawn(actor_class, location, label, folder, rotation=None):
        actor = actors.spawn_actor_from_class(actor_class, unreal.Vector(*location), rotation or unreal.Rotator())
        return finish(actor, label, folder)

    def place_mesh(location, scale, material, label):
        actor = actors.spawn_actor_from_object(cube, unreal.Vector(*location), unreal.Rotator())
        finish(actor, label, "Stage")
        actor.set_actor_scale3d(unreal.Vector(*scale))
        component = actor.static_mesh_component
        component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        component.set_material(0, material)
        return actor

    place_mesh((0, 0, -25), (24, 14, 0.5), floor_mat, "SmokeStageFloor")
    for label, location, scale in (
        ("NorthRail", (0, -715, 30), (24, 0.4, 0.6)),
        ("SouthRail", (0, 715, 30), (24, 0.4, 0.6)),
        ("WestRail", (-1215, 0, 30), (0.4, 14, 0.6)),
        ("EastRail", (1215, 0, 30), (0.4, 14, 0.6)),
    ):
        place_mesh(location, scale, wall_mat, label)

    sun = spawn(unreal.DirectionalLight, (0, 0, 1500), "SmokeStageSun", "Lighting", unreal.Rotator(pitch=-55, yaw=-35))
    sun.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_intensity(0.9)
    sun.light_component.set_light_color(unreal.LinearColor(0.45, 0.55, 1.0, 1.0))
    sky = spawn(unreal.SkyLight, (0, 0, 1000), "SmokeStageSkyLight", "Lighting")
    sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", True)
    sky.light_component.set_intensity(0.25)

    post_process = spawn(unreal.PostProcessVolume, (0, 0, 0), "SmokeStageExposure", "Lighting")
    post_process.set_editor_property("unbound", True)
    settings = post_process.settings
    for name, value in (
        ("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL),
        ("auto_exposure_apply_physical_camera_exposure", False),
        ("auto_exposure_bias", 0.0),
        ("bloom_intensity", 0.5),
        ("vignette_intensity", 0.25),
    ):
        settings.set_editor_property("override_" + name, True)
        settings.set_editor_property(name, value)
    post_process.set_editor_property("settings", settings)

    spawn(unreal.PlayerStart, PLAYER_START, "SmokeStagePlayerStart", "Gameplay")

    smoke_records = []
    for spec in SMOKE_SPECS:
        center_x, center_y = spec["center_xy"]
        preset = presets[spec["name"]]
        effective = effective_configs[spec["name"]]

        pivot = actors.spawn_actor_from_class(pivot_class, unreal.Vector(center_x, center_y, 0.0), unreal.Rotator())
        finish(pivot, f"{spec['label']}_LightPivot", "SmokeLights")
        if pivot.get_component_by_class(unreal.RotatingMovementComponent) is None:
            raise RuntimeError("피벗 액터에 회전 이동 컴포넌트가 없습니다: " + PIVOT_BLUEPRINT_PATH)
        pivot_root = pivot.get_editor_property("root_component")
        if pivot_root is None:
            raise RuntimeError("피벗 액터에 루트 컴포넌트가 없습니다: " + PIVOT_BLUEPRINT_PATH)
        pivot_root.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)

        lamp = spawn(
            unreal.PointLight,
            (center_x + LIGHT_ORBIT_RADIUS_CM, center_y, LIGHT_HEIGHT_CM),
            f"{spec['label']}_OrbitLight",
            "SmokeLights",
        )
        lamp.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        lamp.light_component.set_intensity(LIGHT_INTENSITY)
        lamp.light_component.set_attenuation_radius(LIGHT_ATTENUATION_RADIUS_CM)
        lamp.light_component.set_cast_shadows(False)
        lamp.light_component.set_light_color(unreal.LinearColor(*LIGHT_COLOR, 1.0))
        lamp.attach_to_actor(
            pivot,
            "",
            unreal.AttachmentRule.KEEP_WORLD,
            unreal.AttachmentRule.KEEP_WORLD,
            unreal.AttachmentRule.KEEP_WORLD,
            False,
        )

        smoke = spawn(fluid_actor_class, (center_x, center_y, SMOKE_ACTOR_Z_CM), spec["label"], "Smoke")
        smoke.set_editor_property("should_follow_player_pawn", False)
        smoke.set_editor_property("should_use_activation_volume", False)
        smoke.set_editor_property("interaction_brush_channel", unreal.TDFluidBrushChannel.CHANNEL_R_MUD)
        smoke.set_editor_property("volume_light_actor", lamp)
        smoke.set_editor_property("accept_mouse_brush", True)
        smoke.set_editor_property("mouse_brush_radius_cm", MOUSE_BRUSH_RADIUS_CM)

        live_component = smoke.get_editor_property("live_component")
        live_component.set_editor_property("active_preset", preset)
        live_component.set_editor_property("should_init_system_state_from_preset", True)
        live_component.set_editor_property("simulation_config", effective)

        if smoke.get_editor_property("volume_light_actor") != lamp:
            raise RuntimeError(f"볼륨 라이트 액터 지정 확인 실패: {spec['label']}")
        extent = effective.get_editor_property("sim_extent_cm")
        smoke_records.append({
            "label": smoke.get_actor_label(),
            "preset": preset_asset_name(spec),
            "volume_material": spec["instance_path"],
            "location": [center_x, center_y, SMOKE_ACTOR_Z_CM],
            "extent_cm": [extent.x, extent.y],
            "grid_resolution": effective.get_editor_property("grid_resolution"),
            "light_actor": lamp.get_actor_label(),
            "light_pivot": pivot.get_actor_label(),
            "accept_mouse_brush": True,
        })

    navigation = spawn(unreal.NavMeshBoundsVolume, (0, 0, 100), "SmokeStageNavigation", "Gameplay")
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

    saved_asset_paths = (
        [FLOOR_MAT_PATH, WALL_MAT_PATH, PIVOT_BLUEPRINT_PATH]
        + [preset_asset_path(spec) for spec in SMOKE_SPECS]
    )
    missing_files = find_missing_saved_files(saved_asset_paths, STAGE_MAP_PATH)
    if missing_files:
        raise RuntimeError("저장 파일 확인 실패: " + ", ".join(missing_files))

    report = {
        "map": STAGE_MAP_PATH,
        "overwrite": overwrite,
        "created_floor_material": floor_created,
        "created_wall_material": wall_created,
        "created_pivot_blueprint": pivot_created,
        "smoke_actors": smoke_records,
        "light": {
            "orbit_radius_cm": LIGHT_ORBIT_RADIUS_CM,
            "height_cm": LIGHT_HEIGHT_CM,
            "rotation_degrees_per_sec": LIGHT_ROTATION_DEGREES_PER_SEC,
            "color": list(LIGHT_COLOR),
        },
        "player_start": list(PLAYER_START),
        "navigation_extent": [nav_extent.x, nav_extent.y, nav_extent.z],
        "created_actors": len(created),
        "saved": True,
    }
    report_path = write_report(report)
    print(json.dumps(report, ensure_ascii=False, default=str))
    print("보고서: " + report_path)


if __name__ == "__main__":
    main()
