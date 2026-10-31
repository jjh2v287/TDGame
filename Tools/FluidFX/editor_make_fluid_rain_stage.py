"""실행 환경: TDGame 언리얼 에디터 안(Python 원격 실행). W-3 비 무대 LV_TDFluidRainStage를 만든다: 평지 지형에 S1 연못(지름 10 m·깊이 60 cm, 둘레 구덩이 6개 지름 1~3 m·깊이 2~8 cm, 수면 판 태그 TDFluidWaterSurface·TDFluidWaterRegion, 물 액터 RainPond + MI_TD_FluidWater_Swamp + 젖음 데칼 + 웅덩이 데칼)과 S2 밀도 벤치(12 m 판 + RainAccumulationBench 라이브 액터), 비 액터(TDFluidRainActor, S1 중심, 빗방울 나이아가라), 프리셋 DA_TDFluid_RainPond·DA_TDFluid_RainAccumulationBench(PresetKind만, override 끔). 물 무대 상수·함수는 editor_make_fluid_water_stage.py 에서 불러오기만 한다(main 가드).
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_rain_stage.py (PowerShell, L-repo-01. editor_make_fluid_rain_assets.py 와 NS_TD_FluidRain 생성 뒤, 기존 에셋·맵은 Saved/FluidFX/fluid_rain_stage_request.json 의 {"overwrite": true} 일 때만 갱신)
출력: /Game/FluidFX/Presets/DA_TDFluid_RainPond·DA_TDFluid_RainAccumulationBench, /Game/FluidFX/Maps/LV_TDFluidRainStage, Saved/FluidFX/rain_stage/heightmap.r16, Saved/FluidFX/fluid_rain_stage.json(status·preset_tuning_sha256·material_look_sha256·카메라 탑다운 1500 cm·45도 700 cm·근접 250 cm·벤치 1200 cm·bench_default_channel, run_in_editor 가 SUCCESS 여도 출력 본문 status 로 판정 L-editor-73)
상태: 미검증(2026-10-29 파동 3 W-3 D 작성: py_compile·가짜 unreal 순수 함수 검사만, 에디터 실행 전). 계약 속성(enable_rain_puddle_decal·rain_puddle_decal_material·rain_system·rain_intensity·should_follow_view_target·열거 RAIN_POND·RAIN_ACCUMULATION_BENCH)이 하나라도 없으면 에셋을 바꾸기 전에 중단한다. Bench DA 의 DefaultChannel 은 ChannelA_Water 이고 Mud 는 아니다(결과 주 밀도는 같아 보고만 한다, 반박 B15).
"""
import array
import hashlib
import importlib.util
import json
import math
import os

import unreal

WATER_STAGE_RELATIVE_PATH = ("Tools", "FluidFX", "editor_make_fluid_water_stage.py")
MATERIAL_LOOK_MODULE_RELATIVE_PATH = ("Tools", "FluidFX", "fluid_material_look.py")
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_rain_stage_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_rain_stage.json")
HEIGHTMAP_RELATIVE_PATH = ("Saved", "FluidFX", "rain_stage", "heightmap.r16")

STAGE_FOLDER = "TDGen/FluidFX/RainStage/"
POND_PRESET_NAME = "DA_TDFluid_RainPond"
BENCH_PRESET_NAME = "DA_TDFluid_RainAccumulationBench"
RAIN_ACTOR_CLASS_PATH = "/Script/TDGame.TDFluidRainActor"
RAIN_SYSTEM_PATH = "/Game/FluidFX/Niagara/NS_TD_FluidRain"
PUDDLE_DECAL_MATERIAL_NAME = "MI_TD_FluidRainPuddleDecal_Default"
POND_WATER_MATERIAL_NAME = "MI_TD_FluidWater_Swamp"
BENCH_SURFACE_MATERIAL_NAME = "MI_TD_FluidViscous_Mud"
PLANE_MESH_PATH = "/Engine/BasicShapes/Plane"
CUBE_MESH_PATH = "/Engine/BasicShapes/Cube"

POND_CENTER_XY_CM = (0.0, 0.0)
POND_RADIUS_CM = 500.0
POND_DEPTH_CM = 60.0
POND_WATER_SURFACE_Z_CM = -10.0
POND_WATER_PLATE_SIZE_CM = 1300.0
PIT_SPECS = (
    {"angle_degrees": 0.0, "ring_cm": 800.0, "diameter_cm": 100.0, "depth_cm": 2.0},
    {"angle_degrees": 60.0, "ring_cm": 950.0, "diameter_cm": 150.0, "depth_cm": 4.0},
    {"angle_degrees": 120.0, "ring_cm": 850.0, "diameter_cm": 200.0, "depth_cm": 6.0},
    {"angle_degrees": 180.0, "ring_cm": 1000.0, "diameter_cm": 300.0, "depth_cm": 8.0},
    {"angle_degrees": 240.0, "ring_cm": 900.0, "diameter_cm": 120.0, "depth_cm": 3.0},
    {"angle_degrees": 300.0, "ring_cm": 1100.0, "diameter_cm": 250.0, "depth_cm": 5.0},
)
BENCH_CENTER_XY_CM = (4200.0, 0.0)
BENCH_PLATE_SIZE_CM = 1200.0
BENCH_PLATE_THICKNESS_CM = 4.0
BENCH_LIVE_ACTOR_LIFT_CM = 5.0
BENCH_LIVE_ACTOR_FLAGS = (
    ("should_follow_player_pawn", False),
    ("should_use_activation_volume", False),
    ("sample_terrain_elevation", False),
    ("accept_mouse_brush", False),
)
PLAYER_START_XY_CM = (-1500.0, 0.0)
NAVIGATION_SCALE = (60.0, 60.0, 8.0)
NAVIGATION_HALF_EXTENT_CM = 6000.0
RAIN_ACTOR_INTENSITY = 1.0
CAMERA_POSES = (
    {"name": "pond_top", "scene": "S1", "pawn_xy_cm": POND_CENTER_XY_CM, "arm_length_cm": 1500.0, "pitch_degrees": -90.0, "yaw_degrees": 0.0},
    {"name": "pond_45", "scene": "S1", "pawn_xy_cm": POND_CENTER_XY_CM, "arm_length_cm": 700.0, "pitch_degrees": -45.0, "yaw_degrees": 0.0},
    {"name": "pond_near", "scene": "S1", "pawn_xy_cm": POND_CENTER_XY_CM, "arm_length_cm": 250.0, "pitch_degrees": -70.0, "yaw_degrees": 0.0},
    {"name": "density_bench", "scene": "S2", "pawn_xy_cm": BENCH_CENTER_XY_CM, "arm_length_cm": 1200.0, "pitch_degrees": -90.0, "yaw_degrees": 0.0},
)

ENUM_CONTRACTS = {"TDFluidPresetKind": ("RAIN_POND", "RAIN_ACCUMULATION_BENCH")}
OBJECT_CONTRACTS = {
    "TDFluidRainActor": ("rain_intensity", "rain_system", "should_follow_view_target"),
    "TDFluidWaterActor": ("enable_rain_puddle_decal", "rain_puddle_decal_material", "enable_wet_decal", "wet_decal_material"),
}
EXTRA_REQUIRED_UNREAL_NAMES = (
    "StaticMeshActor",
    "NavMeshBoundsVolume",
    "RecastNavMesh",
    "CollisionEnabled.NO_COLLISION",
    "ComponentMobility.MOVABLE",
    "TDFluidPresetAsset.build_rain_for_preset_kind",
)


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


def write_report(report):
    report_path = project_path(*REPORT_RELATIVE_PATH)
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, "w", encoding="utf-8") as report_file:
        json.dump(report, report_file, ensure_ascii=False, indent=2, default=str)
    return report_path


def find_missing_names(root, dotted_names):
    missing = []
    for dotted_name in dotted_names:
        target = root
        for part in dotted_name.split("."):
            target = getattr(target, part, None)
            if target is None:
                missing.append(dotted_name)
                break
    return missing


def find_contract_problems(water_stage):
    problems = [f"unreal.{name}" for name in find_missing_names(unreal, EXTRA_REQUIRED_UNREAL_NAMES)]
    for enum_name, value_names in ENUM_CONTRACTS.items():
        enum_type = getattr(unreal, enum_name, None)
        if enum_type is None:
            problems.append(enum_name)
            continue
        problems.extend(f"{enum_name}.{name}" for name in water_stage.find_missing_members(enum_type, value_names))
    for class_name, property_names in OBJECT_CONTRACTS.items():
        class_type = getattr(unreal, class_name, None)
        if class_type is None:
            problems.append(class_name)
            continue
        problems.extend(f"{class_name}.{name}" for name in water_stage.find_missing_properties(unreal.get_default_object(class_type), property_names))
    problems.extend(water_stage.check_cpp_contract((0, 1)))
    return problems


def compute_bowl_profile(distance_cm, radius_cm):
    if distance_cm >= radius_cm:
        return 0.0
    ratio = distance_cm / radius_cm
    return 1.0 - ratio * ratio


def compute_pit_centers():
    centers = []
    for pit in PIT_SPECS:
        angle = math.radians(pit["angle_degrees"])
        centers.append((POND_CENTER_XY_CM[0] + pit["ring_cm"] * math.cos(angle), POND_CENTER_XY_CM[1] + pit["ring_cm"] * math.sin(angle)))
    return centers


PIT_CENTERS_CM = compute_pit_centers()


def compute_terrain_height_cm(x_cm, y_cm):
    pond_distance = math.hypot(x_cm - POND_CENTER_XY_CM[0], y_cm - POND_CENTER_XY_CM[1])
    height = -POND_DEPTH_CM * compute_bowl_profile(pond_distance, POND_RADIUS_CM)
    for pit, center in zip(PIT_SPECS, PIT_CENTERS_CM):
        pit_distance = math.hypot(x_cm - center[0], y_cm - center[1])
        height -= pit["depth_cm"] * compute_bowl_profile(pit_distance, 0.5 * pit["diameter_cm"])
    return height


def write_heightmap(water_stage, path):
    vertices_per_side = water_stage.LANDSCAPE_COMPONENTS * water_stage.LANDSCAPE_QUADS_PER_SECTION + 1
    origin = -0.5 * (vertices_per_side - 1) * water_stage.LANDSCAPE_XY_SCALE_CM
    values = array.array("H")
    for row in range(vertices_per_side):
        y_cm = origin + row * water_stage.LANDSCAPE_XY_SCALE_CM
        for column in range(vertices_per_side):
            x_cm = origin + column * water_stage.LANDSCAPE_XY_SCALE_CM
            encoded = round(water_stage.HEIGHT_ZERO_VALUE + compute_terrain_height_cm(x_cm, y_cm) * water_stage.HEIGHT_UNITS_PER_CM)
            values.append(min(max(encoded, 0), 65535))
    if values.itemsize != 2:
        raise RuntimeError("uint16 배열 크기 확인 실패")
    if water_stage.sys_is_big_endian():
        values.byteswap()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as raw_file:
        values.tofile(raw_file)
    with open(path, "rb") as raw_file:
        digest = hashlib.sha256(raw_file.read()).hexdigest()
    return vertices_per_side, digest


def load_resources(water_stage, assets):
    puddle_decal_path = f"{water_stage.MATERIAL_DIR}/{PUDDLE_DECAL_MATERIAL_NAME}"
    required = {
        "game_mode": assets.load_blueprint_class(water_stage.GAME_MODE_PATH),
        "ground_material": assets.load_asset(water_stage.SAND_INSTANCE_PATH),
        "pond_water_material": assets.load_asset(f"{water_stage.MATERIAL_DIR}/{POND_WATER_MATERIAL_NAME}"),
        "wet_decal_material": assets.load_asset(water_stage.COAST_WET_DECAL_MATERIAL_PATH),
        "puddle_decal_material": assets.load_asset(puddle_decal_path),
        "rain_system": assets.load_asset(RAIN_SYSTEM_PATH),
        "plane_mesh": assets.load_asset(PLANE_MESH_PATH),
        "cube_mesh": assets.load_asset(CUBE_MESH_PATH),
        "water_actor_class": unreal.load_class(None, water_stage.CLASS_PATHS["TDFluidWaterActor"]),
        "live_actor_class": unreal.load_class(None, water_stage.CLASS_PATHS["TDFluidLiveActor"]),
        "rain_actor_class": unreal.load_class(None, RAIN_ACTOR_CLASS_PATH),
    }
    missing = [name for name, value in required.items() if value is None]
    if missing:
        raise RuntimeError("필수 에셋 또는 클래스 없음(물 무대 장면 0·1, editor_make_fluid_rain_assets.py, NS_TD_FluidRain 생성, C++ 빌드 먼저): " + ", ".join(missing))
    bench_surface_path = f"{water_stage.MATERIAL_DIR}/{BENCH_SURFACE_MATERIAL_NAME}"
    required["bench_surface_material"] = assets.load_asset(bench_surface_path) if assets.does_asset_exist(bench_surface_path) else None
    return required


def configure_pond_preset(water_stage):
    path = f"{water_stage.PRESET_DIR}/{POND_PRESET_NAME}"
    preset = water_stage.load_or_create_preset(POND_PRESET_NAME, path)
    preset.set_editor_property("preset_kind", unreal.TDFluidPresetKind.RAIN_POND)
    preset.set_editor_property("override_preset_defaults", False)
    effective = preset.resolve_effective_config()
    water_stage.save_asset(preset, path)
    return preset, effective, path


def configure_bench_preset(water_stage, resources):
    path = f"{water_stage.PRESET_DIR}/{BENCH_PRESET_NAME}"
    preset = water_stage.load_or_create_preset(BENCH_PRESET_NAME, path)
    preset.set_editor_property("preset_kind", unreal.TDFluidPresetKind.RAIN_ACCUMULATION_BENCH)
    preset.set_editor_property("override_preset_defaults", False)
    preset.set_editor_property("surface_mesh", resources["plane_mesh"])
    if resources["bench_surface_material"] is not None:
        preset.set_editor_property("output_material", resources["bench_surface_material"])
    default_channel = str(preset.get_editor_property("default_channel"))
    water_stage.save_asset(preset, path)
    return preset, default_channel, path


def open_empty_stage_level(levels, actors, map_path):
    if not unreal.EditorAssetLibrary.does_asset_exist(map_path):
        if not levels.new_level(map_path):
            raise RuntimeError(f"무대 레벨 생성 실패: {map_path}")
        return
    if not levels.load_level(map_path):
        raise RuntimeError(f"기존 무대 레벨 열기 실패: {map_path}")
    for actor in actors.get_all_level_actors():
        if isinstance(actor, unreal.WorldSettings) or type(actor) is unreal.Brush:
            continue
        if not actors.destroy_actor(actor):
            raise RuntimeError(f"기존 액터 제거 실패: {actor.get_name()}")


def create_spawners(actors, created):
    def finish(actor, label, folder):
        if actor is None:
            raise RuntimeError(f"액터 생성 실패: {label}")
        actor.set_actor_label("TDGen_" + label)
        actor.set_folder_path(STAGE_FOLDER + folder)
        created.append(actor)
        return actor

    def spawn(actor_class, location, label, folder, rotation=None):
        actor = actors.spawn_actor_from_class(actor_class, unreal.Vector(*location), rotation or unreal.Rotator())
        return finish(actor, label, folder)

    return finish, spawn


def spawn_lighting(water_stage, spawn):
    sun = spawn(unreal.DirectionalLight, (0, 0, 1500), "RainStageSun", "Lighting", unreal.Rotator(pitch=water_stage.SUN_ROTATION[0], yaw=water_stage.SUN_ROTATION[1]))
    sun.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_intensity(water_stage.SUN_LUX)
    spawn(unreal.SkyAtmosphere, (0, 0, 0), "RainStageSkyAtmosphere", "Lighting")
    sky = spawn(unreal.SkyLight, (0, 0, 1000), "RainStageSkyLight", "Lighting")
    sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", False)
    sky.light_component.set_intensity(1.0)
    water_stage.configure_post_process(spawn(unreal.PostProcessVolume, (0, 0, 0), "RainStageExposure", "Lighting"))
    return water_stage.EXPOSURE_EV100


def spawn_navigation(spawn, world):
    navigation = spawn(unreal.NavMeshBoundsVolume, (0.0, 0.0, 0.0), "RainStageNavigation", "Gameplay")
    navigation.set_actor_scale3d(unreal.Vector(*NAVIGATION_SCALE))
    nav_origin, nav_extent = navigation.get_actor_bounds(False)
    if nav_extent.x < NAVIGATION_HALF_EXTENT_CM - 1.0 or nav_extent.y < NAVIGATION_HALF_EXTENT_CM - 1.0:
        raise RuntimeError(f"내비게이션 브러시 범위 검증 실패: {nav_extent}")
    for nav_mesh in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RecastNavMesh):
        nav_mesh.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC)
        nav_mesh.set_editor_property("is_world_partitioned", False)
    return [nav_extent.x, nav_extent.y, nav_extent.z]


def configure_plate_component(actor, material, has_collision):
    component = actor.static_mesh_component
    component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    if not has_collision:
        component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        component.set_collision_profile_name("NoCollision")
        component.set_cast_shadow(False)
    component.set_material(0, material)
    return component


def spawn_pond_water_plate(water_stage, actors, finish, resources):
    location = unreal.Vector(POND_CENTER_XY_CM[0], POND_CENTER_XY_CM[1], POND_WATER_SURFACE_Z_CM)
    plate = actors.spawn_actor_from_object(resources["plane_mesh"], location, unreal.Rotator())
    finish(plate, "RainStageWaterPlate", "S1")
    plate.set_actor_scale3d(unreal.Vector(POND_WATER_PLATE_SIZE_CM / 100.0, POND_WATER_PLATE_SIZE_CM / 100.0, 1.0))
    configure_plate_component(plate, resources["pond_water_material"], False)
    water_stage.add_actor_tag(plate, water_stage.TAG_WATER_SURFACE)
    water_stage.add_actor_tag(plate, water_stage.TAG_WATER_REGION)
    origin, extent = plate.get_actor_bounds(False)
    expected_half = 0.5 * POND_WATER_PLATE_SIZE_CM
    if abs(extent.x - expected_half) > 1.0 or abs(extent.y - expected_half) > 1.0 or abs(origin.z - POND_WATER_SURFACE_Z_CM) > 1.0:
        raise RuntimeError(f"수면 판 범위 검증 실패: 중심 {origin} 반폭 {extent}")
    return {"half_extent_cm": [extent.x, extent.y], "surface_z_cm": origin.z}


def spawn_bench_plate(finish, actors, resources):
    location = unreal.Vector(BENCH_CENTER_XY_CM[0], BENCH_CENTER_XY_CM[1], 0.0)
    plate = actors.spawn_actor_from_object(resources["cube_mesh"], location, unreal.Rotator())
    finish(plate, "RainStageBenchPlate", "S2")
    plate.set_actor_scale3d(unreal.Vector(BENCH_PLATE_SIZE_CM / 100.0, BENCH_PLATE_SIZE_CM / 100.0, BENCH_PLATE_THICKNESS_CM / 100.0))
    configure_plate_component(plate, resources["ground_material"], True)
    origin, extent = plate.get_actor_bounds(False)
    return {"half_extent_cm": [extent.x, extent.y, extent.z], "top_z_cm": origin.z + extent.z}


def require_property_text(actor, name, expected_text):
    actual = str(actor.get_editor_property(name))
    if expected_text not in actual:
        raise RuntimeError(f"{actor.get_actor_label()} {name} 설정 확인 실패: 기대 {expected_text} 실제 {actual}")


def require_property_flag(actor, name, expected):
    if bool(actor.get_editor_property(name)) is not expected:
        raise RuntimeError(f"{actor.get_actor_label()} {name} 설정 확인 실패: 기대 {expected}")


def configure_pond_water_actor(water_stage, water, preset, effective, resources):
    properties = water_stage.configure_water_actor(water, preset, effective)
    water.set_editor_property("enable_wet_decal", True)
    water.set_editor_property("wet_decal_material", resources["wet_decal_material"])
    water.set_editor_property("enable_rain_puddle_decal", True)
    water.set_editor_property("rain_puddle_decal_material", resources["puddle_decal_material"])
    require_property_flag(water, "enable_wet_decal", True)
    require_property_flag(water, "enable_rain_puddle_decal", True)
    require_property_text(water, "wet_decal_material", "M_TD_FluidWetnessDecal")
    require_property_text(water, "rain_puddle_decal_material", PUDDLE_DECAL_MATERIAL_NAME)
    properties["enable_wet_decal"] = "True"
    properties["enable_rain_puddle_decal"] = "True"
    properties["wet_decal_material"] = water_stage.COAST_WET_DECAL_MATERIAL_PATH
    properties["rain_puddle_decal_material"] = f"{water_stage.MATERIAL_DIR}/{PUDDLE_DECAL_MATERIAL_NAME}"
    return properties


def spawn_rain_actor(spawn, resources):
    rain = spawn(resources["rain_actor_class"], (POND_CENTER_XY_CM[0], POND_CENTER_XY_CM[1], 0.0), "RainStageRainActor", "Rain")
    rain.set_editor_property("rain_system", resources["rain_system"])
    rain.set_editor_property("rain_intensity", RAIN_ACTOR_INTENSITY)
    rain.set_editor_property("should_follow_view_target", True)
    require_property_text(rain, "rain_system", "NS_TD_FluidRain")
    require_property_flag(rain, "should_follow_view_target", True)
    if abs(float(rain.get_editor_property("rain_intensity")) - RAIN_ACTOR_INTENSITY) > 1e-6:
        raise RuntimeError("비 액터 rain_intensity 설정 확인 실패")
    return {
        "rain_system": RAIN_SYSTEM_PATH,
        "rain_intensity": RAIN_ACTOR_INTENSITY,
        "should_follow_view_target": True,
        "location_cm": [POND_CENTER_XY_CM[0], POND_CENTER_XY_CM[1], 0.0],
    }


def spawn_bench_live_actor(spawn, resources, bench_preset):
    location = (BENCH_CENTER_XY_CM[0], BENCH_CENTER_XY_CM[1], BENCH_LIVE_ACTOR_LIFT_CM + 0.5 * BENCH_PLATE_THICKNESS_CM)
    bench = spawn(resources["live_actor_class"], location, "RainStageBench", "S2")
    for name, value in BENCH_LIVE_ACTOR_FLAGS:
        bench.set_editor_property(name, value)
    live_component = bench.get_editor_property("live_component")
    live_component.set_editor_property("active_preset", bench_preset)
    live_component.set_editor_property("should_init_system_state_from_preset", True)
    return {"label": bench.get_actor_label(), "location_cm": list(location)}


def build_stage(request):
    water_stage = load_module("fluid_water_stage_constants", *WATER_STAGE_RELATIVE_PATH)
    assets = unreal.EditorAssetLibrary
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if editor.get_game_world() is not None:
        raise RuntimeError("PIE를 종료한 뒤 실행하세요.")
    if editor.get_editor_world() is None:
        raise RuntimeError("에디터 월드가 없습니다.")
    contract_problems = find_contract_problems(water_stage)
    if contract_problems:
        raise RuntimeError("C++ 계약 이름 없음(재빌드와 에디터 재시작 필요): " + ", ".join(contract_problems))

    map_path = f"{water_stage.MAP_DIR}/LV_TDFluidRainStage"
    guarded_paths = [f"{water_stage.PRESET_DIR}/{POND_PRESET_NAME}", f"{water_stage.PRESET_DIR}/{BENCH_PRESET_NAME}", map_path]
    overwrite = request.get("overwrite") is True
    existing_paths = [path for path in guarded_paths if assets.does_asset_exist(path)]
    if existing_paths and not overwrite:
        raise RuntimeError("기존 에셋 보존(덮어쓰려면 " + project_path(*REQUEST_RELATIVE_PATH) + ' 에 {"overwrite": true}): ' + ", ".join(existing_paths))
    unsaved_names = water_stage.find_unsaved_package_names(overwrite)
    if unsaved_names:
        raise RuntimeError("미저장 작업을 보존합니다: " + ", ".join(unsaved_names))

    print("단계: 프리셋 정본 검사·다시 읽기")
    preset_tuning_sha256 = water_stage.reload_preset_tuning()
    material_look = load_module("fluid_material_look", *MATERIAL_LOOK_MODULE_RELATIVE_PATH)
    look_table = material_look.TDMaterialLookTable.read_project(unreal.SystemLibrary.get_project_directory())
    resources = load_resources(water_stage, assets)

    print("단계: 프리셋 DA")
    water_stage.ensure_directory(water_stage.PRESET_DIR)
    pond_preset, pond_effective, pond_preset_path = configure_pond_preset(water_stage)
    bench_preset, bench_default_channel, bench_preset_path = configure_bench_preset(water_stage, resources)

    print("단계: 지형 높이 파일")
    heightmap_path = project_path(*HEIGHTMAP_RELATIVE_PATH)
    vertices_per_side, heightmap_sha256 = write_heightmap(water_stage, heightmap_path)

    print("단계: 레벨 비우기")
    water_stage.ensure_directory(water_stage.MAP_DIR)
    open_empty_stage_level(levels, actors, map_path)
    world = editor.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", resources["game_mode"])
    created = []
    finish, spawn = create_spawners(actors, created)

    print("단계: 지형·조명")
    landscape = finish(water_stage.create_landscape(world, resources["ground_material"], heightmap_path), "RainStageLandscape", "Stage")
    fixed_ev100 = spawn_lighting(water_stage, spawn)
    spawn(unreal.PlayerStart, (PLAYER_START_XY_CM[0], PLAYER_START_XY_CM[1], water_stage.PLAYER_START_CLEARANCE_CM), "RainStagePlayerStart", "Gameplay")

    print("단계: S1 연못")
    water_plate = spawn_pond_water_plate(water_stage, actors, finish, resources)
    water = spawn(resources["water_actor_class"], (POND_CENTER_XY_CM[0], POND_CENTER_XY_CM[1], POND_WATER_SURFACE_Z_CM), "RainStageWaterPond", "S1")
    water_properties = configure_pond_water_actor(water_stage, water, pond_preset, pond_effective, resources)

    print("단계: S2 밀도 벤치")
    bench_plate = spawn_bench_plate(finish, actors, resources)
    bench_actor = spawn_bench_live_actor(spawn, resources, bench_preset)

    print("단계: 비 액터")
    rain_actor = spawn_rain_actor(spawn, resources)

    print("단계: 내비게이션")
    navigation_extent = spawn_navigation(spawn, world)

    landscape_origin, landscape_extent = landscape.get_actor_bounds(False)
    expected_half_extent = 0.5 * (vertices_per_side - 1) * water_stage.LANDSCAPE_XY_SCALE_CM
    if abs(landscape_extent.x - expected_half_extent) > water_stage.LANDSCAPE_XY_SCALE_CM * 2.0:
        raise RuntimeError(f"지형 범위 검증 실패: 기대 반폭 {expected_half_extent} 실제 {landscape_extent.x}")

    print("단계: 레벨 저장")
    if not levels.save_current_level():
        raise RuntimeError(f"비 무대 레벨 저장 실패: {map_path}")
    missing_files = water_stage.find_missing_saved_files([pond_preset_path, bench_preset_path], map_path)
    if missing_files:
        raise RuntimeError("저장 파일 확인 실패: " + ", ".join(missing_files))

    report = {
        "status": "ok",
        "map": map_path,
        "overwrite": overwrite,
        "preset_tuning_sha256": preset_tuning_sha256,
        "material_look_sha256": look_table.source_sha256(),
        "presets": {"pond": pond_preset_path, "bench": bench_preset_path},
        "bench_default_channel": bench_default_channel,
        "heightmap": {
            "path": heightmap_path,
            "sha256": heightmap_sha256,
            "vertices_per_side": vertices_per_side,
            "pond_depth_cm": POND_DEPTH_CM,
            "pond_radius_cm": POND_RADIUS_CM,
            "pits": [dict(pit, center_cm=list(center)) for pit, center in zip(PIT_SPECS, PIT_CENTERS_CM)],
        },
        "landscape_half_extent_cm": [landscape_extent.x, landscape_extent.y, landscape_extent.z],
        "water_plate": water_plate,
        "water_actor": water.get_actor_label(),
        "water_actor_properties": water_properties,
        "bench_plate": bench_plate,
        "bench_actor": bench_actor,
        "rain_actor": rain_actor,
        "scenes": [
            {"name": pose["name"], "scene": pose["scene"], "camera": {"pawn": list(pose["pawn_xy_cm"]), "arm": pose["arm_length_cm"], "view": [pose["pitch_degrees"], pose["yaw_degrees"]]}}
            for pose in CAMERA_POSES
        ],
        "poses": [dict(pose, pawn_xy_cm=list(pose["pawn_xy_cm"])) for pose in CAMERA_POSES],
        "navigation_extent": navigation_extent,
        "exposure_ev100": fixed_ev100,
        "created_actors": len(created),
        "saved": True,
    }
    report_path = write_report(report)
    print(json.dumps(report, ensure_ascii=False, default=str))
    print("보고서: " + report_path)


def run_with_failure_report():
    try:
        build_stage(read_request())
    except Exception as error:
        write_report({"status": "failed", "error": str(error)})
        print("실패: " + str(error))
        raise


if __name__ == "__main__":
    run_with_failure_report()
