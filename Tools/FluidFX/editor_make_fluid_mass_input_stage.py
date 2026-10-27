"""실행 환경: TDGame 언리얼 에디터 안(Python 원격 실행). 대량 입력 무대 LV_TDFluidMassInputStage(M1 TDGen_MassBench10k·M2 TDGen_MassBench1k·M3 TDGen_MassNdc1k 벤치, M4 TDGen_FlamethrowerTurret 원기둥 포탑 + NS_TD_FluidFlamethrower + 불 액터 Flamethrower + 300cm 앞 100cm 큐브(부품, 킬 상자 = 큐브, CD S.0.3.10·IMG 17), M5 TDGen_AvalancheSlope 25° 판 + SnowAvalanche 세계 방향 + 바위 2(부품))를 만든다. 먼저 editor_make_fluid_mass_input_assets.py, 나이아가라 명세 5종 생성(editor_make_fluid_driven_niagara.py --spec), editor_make_fluid_fire_stage.py(불꽃 MI·바닥 MI)가 끝나 있어야 한다.
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_mass_input_stage.py (PowerShell, L-repo-01)
출력: /Game/FluidFX/Presets/DA_TDFluid_Mass{Bench,Flamethrower,Avalanche}, /Game/FluidFX/Maps/LV_TDFluidMassInputStage, Saved/FluidFX/fluid_mass_input_stage.json(preset_tuning_sha256·material_look_sha256, 장면마다 cd_ids·niagara_system·kind·카메라 제안·킬 상자·비탈 트레이스). 기존 에셋은 Saved/FluidFX/fluid_mass_input_stage_request.json 의 {"overwrite": true} 가 있을 때만 갱신하고 미저장 작업이 있으면 중단한다.
상태: 미검증(2026-10-26 FX-5 묶음 E 작성: py_compile·가짜 unreal 실행만, 에디터 실행 전). 프리셋은 PresetKind 정적 빌더 값만 넣고 bOverridePresetDefaults 는 끈다(값은 JSON 종류 값). Flamethrower 만 불꽃 MI·평면 메시를 붙인다(IMG 17 보정에 보이는 불 필요). 킬 상자는 무대 상수라 모양 정본이 아니라 나이아가라 컴포넌트 User 재정의로 넣는다(반박 R5 점검). 비탈 높이는 런타임 LineTrace(X-04)가 쓰므로 판 표면을 에디터 트레이스로 해석값과 대조한다.
"""
import copy
import importlib.util
import json
import math
import os

import unreal

STAGE_MAP_PATH = "/Game/FluidFX/Maps/LV_TDFluidMassInputStage"
OBSTACLE_COMPONENT_CLASS_PATH = "/Script/TDGame.TDFluidObstacleComponent"
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_mass_input_stage_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_mass_input_stage.json")
STAGE_FOLDER = "TDGen/FluidFX/MassInputStage"
SCENE_SPACING_CM = 7000.0
FLAMETHROWER_PRESET_NAME = "MassFlamethrower"
FLAMETHROWER_LIGHT_SOURCE = "CampfireFire"
FLAMETHROWER_FLAME_SCENE = "campfire"
BENCH_SYSTEM_HEIGHT_CM = 50.0
TURRET_MOUNT_SCALE = (0.6, 0.6, 0.8)
TURRET_MOUNT_HEIGHT_CM = 40.0
TURRET_NOZZLE_OFFSET = (35.0, 0.0, 70.0)
TURRET_FIRE_OFFSET_CM = 150.0
TURRET_CUBE_DISTANCE_CM = 300.0
TURRET_CUBE_SIZE_CM = 100.0
SLOPE_PITCH_DEGREES = -25.0
SLOPE_SCALE = (50.0, 30.0, 0.2)
SLOPE_CENTER_HEIGHT_CM = 1300.0
SLOPE_TRACE_TOLERANCE_CM = 5.0
SLOPE_TRACE_RANGE_CM = 3000.0
AVALANCHE_SYSTEM_OFFSET = (-1800.0, 0.0)
AVALANCHE_SYSTEM_LIFT_CM = 60.0
ROCK_SPECS = (
    {"name": "RockA", "offset": (-400.0, -500.0), "scale": 2.0},
    {"name": "RockB", "offset": (300.0, 450.0), "scale": 1.6},
)
ROCK_EMBED_RATIO = 0.6
BASIC_SHAPE_HALF_SIZE_CM = 50.0
KILL_BOX_CENTER_PARAMETER = "User.TDMassKillBoxCenter"
KILL_BOX_SIZE_PARAMETER = "User.TDMassKillBoxSizeCm"

PRESET_SPECS = (
    {"name": "MassBench", "kind": "MASS_INPUT_BENCH", "channel": "CHANNEL_A_WATER", "is_fire": False},
    {"name": FLAMETHROWER_PRESET_NAME, "kind": "FLAMETHROWER", "channel": "CHANNEL_B_FIRE", "is_fire": True},
    {"name": "MassAvalanche", "kind": "SNOW_AVALANCHE", "channel": "CHANNEL_R_MUD", "is_fire": False},
)
SCENE_SPECS = (
    {"id": "M1", "label": "MassBench10k", "layout": "bench", "preset": "MassBench", "system": "NS_TD_FluidMassBench",
     "cd_ids": [], "refs": ["R13 PerformanceTest_Particles", "MC:300"], "camera": {"pawn": (0.0, -2600.0), "arm": 2500.0, "view": (-60.0, 90.0)}},
    {"id": "M2", "label": "MassBench1k", "layout": "bench", "preset": "MassBench", "system": "NS_TD_FluidMassBench1k",
     "cd_ids": [], "refs": ["R13 PerformanceTest_Particles", "MC:300"], "camera": {"pawn": (0.0, -1200.0), "arm": 1500.0, "view": (-60.0, 90.0)}},
    {"id": "M3", "label": "MassNdc1k", "layout": "bench", "preset": "MassBench", "system": "NS_TD_FluidMassNdcBench",
     "cd_ids": [], "refs": ["R13 PerformanceTest_Particles", "MC:300"], "camera": {"pawn": (0.0, -1200.0), "arm": 1500.0, "view": (-60.0, 90.0)}},
    {"id": "M4", "label": "FlamethrowerTurret", "layout": "turret", "preset": FLAMETHROWER_PRESET_NAME, "system": "NS_TD_FluidFlamethrower",
     "cd_ids": ["S.0.3.10"], "refs": ["IMG 17"], "camera": {"pawn": (TURRET_FIRE_OFFSET_CM, -450.0), "arm": 450.0, "view": (-8.0, 90.0)}},
    {"id": "M5", "label": "AvalancheSlope", "layout": "slope", "preset": "MassAvalanche", "system": "NS_TD_FluidAvalanche",
     "cd_ids": [], "refs": ["IMG 1"], "camera": {"pawn": (0.0, -1800.0), "arm": 3600.0, "view": (-30.0, 90.0)}},
)
EXTRA_REQUIRED_UNREAL_NAMES = (
    "NiagaraActor",
    "StaticMeshActor",
    "TDFluidBrushChannel.CHANNEL_A_WATER",
    "TDFluidBrushChannel.CHANNEL_B_FIRE",
    "TDFluidBrushChannel.CHANNEL_R_MUD",
    "TDFluidPresetKind.MASS_INPUT_BENCH",
    "TDFluidPresetKind.FLAMETHROWER",
    "TDFluidPresetKind.SNOW_AVALANCHE",
    "TDFluidPresetAsset.build_config_for_preset_kind",
    "TDFluidPresetAsset.build_plane_settings_for_preset_kind",
    "TDFluidPresetAsset.build_volume_render_for_preset_kind",
    "TDFluidPresetAsset.build_mass_input_for_preset_kind",
    "SubobjectDataSubsystem",
    "SystemLibrary.line_trace_single",
    "TraceTypeQuery.TRACE_TYPE_QUERY1",
    "DrawDebugTrace.NONE",
)
PRESET_EXTRA_PROPERTIES = ("default_channel", "plane_settings", "volume_render", "output_material", "surface_mesh", "translucency_sort_priority")
OBSTACLE_REQUIRED_PROPERTIES = ("use_owner_bounds",)


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


def load_required_assets(fire_stage):
    assets = unreal.EditorAssetLibrary
    paths = {
        "cube": fire_stage.CUBE_MESH_PATH,
        "plane": fire_stage.PLANE_MESH_PATH,
        "sphere": fire_stage.SPHERE_MESH_PATH,
        "cylinder": fire_stage.CYLINDER_MESH_PATH,
        "ember_material": fire_stage.EMBER_INSTANCE_PATH,
        "floor_material": fire_stage.FLOOR_INSTANCE_PATH,
        "flame_material": fire_stage.FLAME_INSTANCE_PATHS[FLAMETHROWER_FLAME_SCENE],
    }
    loaded = {name: assets.load_asset(path) if assets.does_asset_exist(path) else None for name, path in paths.items()}
    missing = [paths[name] for name, value in loaded.items() if value is None]
    if missing:
        raise RuntimeError("필수 에셋 없음(editor_make_fluid_fire_stage.py 먼저): " + ", ".join(missing))
    return loaded


def load_obstacle_component_class(particle_stage):
    component_class = unreal.load_class(None, OBSTACLE_COMPONENT_CLASS_PATH)
    if component_class is None:
        raise RuntimeError("장애물 부품 클래스 없음(묶음 C 빌드·에디터 재시작 필요): " + OBSTACLE_COMPONENT_CLASS_PATH)
    missing = particle_stage.find_missing_properties(unreal.get_default_object(component_class), OBSTACLE_REQUIRED_PROPERTIES)
    if missing:
        raise RuntimeError("장애물 부품 속성 없음: " + ", ".join(missing))
    return component_class


def describe_mass_bench_channel():
    settings = unreal.TDFluidPresetAsset.build_mass_input_for_preset_kind(unreal.TDFluidPresetKind.MASS_INPUT_BENCH)
    channel = settings.get_editor_property("data_channel")
    return channel.get_path_name() if channel is not None else ""


def configure_mass_preset(particle_stage, spec, assets_by_name):
    kind = getattr(unreal.TDFluidPresetKind, spec["kind"])
    preset, path = particle_stage.load_or_create_preset(spec["name"])
    preset.set_editor_property("preset_kind", kind)
    preset.set_editor_property("override_preset_defaults", False)
    preset.set_editor_property("default_channel", getattr(unreal.TDFluidBrushChannel, spec["channel"]))
    preset.set_editor_property("simulation_config", unreal.TDFluidPresetAsset.build_config_for_preset_kind(kind))
    preset.set_editor_property("plane_settings", unreal.TDFluidPresetAsset.build_plane_settings_for_preset_kind(kind))
    preset.set_editor_property("volume_render", unreal.TDFluidPresetAsset.build_volume_render_for_preset_kind(kind))
    if spec["is_fire"]:
        preset.set_editor_property("output_material", assets_by_name["flame_material"])
        preset.set_editor_property("surface_mesh", assets_by_name["plane"])
        preset.set_editor_property("translucency_sort_priority", 1)
    if preset.get_editor_property("preset_kind") != kind:
        raise RuntimeError(f"프리셋 종류 지정 확인 실패: {path}")
    particle_stage.save_asset(preset, path)
    return preset, path, preset.resolve_effective_config()


def spawn_static_mesh(actors, particle_stage, created, mesh, location, rotation, scale, label, folder):
    actor = actors.spawn_actor_from_object(mesh, unreal.Vector(*location), rotation)
    particle_stage.finish_actor(actor, label, folder, created)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    actor.static_mesh_component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
    return actor


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
    return component


def spawn_niagara_actor(actors, particle_stage, created, system, location, rotation, label, folder):
    actor = actors.spawn_actor_from_class(unreal.NiagaraActor, unreal.Vector(*location), rotation)
    particle_stage.finish_actor(actor, label, folder, created)
    actor.niagara_component.set_asset(system)
    if actor.niagara_component.get_asset() != system:
        raise RuntimeError("나이아가라 시스템 지정 확인 실패: " + actor.get_actor_label())
    return actor


def spawn_fluid_actor(actors, particle_stage, created, actor_class, location, label, folder):
    actor = actors.spawn_actor_from_class(actor_class, unreal.Vector(*location), unreal.Rotator())
    return particle_stage.finish_actor(actor, label, folder, created)


def register_flamethrower_actor_spec(fire_stage):
    actor_spec = copy.deepcopy(fire_stage.FIRE_ACTOR_SPECS[FLAMETHROWER_LIGHT_SOURCE])
    actor_spec["preset"] = FLAMETHROWER_PRESET_NAME
    fire_stage.PRESET_KIND_BY_NAME[FLAMETHROWER_PRESET_NAME] = "FLAMETHROWER"
    return actor_spec


def build_scene_bench(context, scene, center, folder):
    particle_stage, actors, created = context["particle_stage"], context["actors"], context["created"]
    preset, preset_path, effective = context["presets"][scene["preset"]]
    actor = spawn_fluid_actor(actors, particle_stage, created, context["live_actor_class"], (center[0], center[1], 1.0), scene["label"], folder)
    particle_stage.configure_live_actor(actor, preset, effective)
    system_actor = spawn_niagara_actor(actors, particle_stage, created, context["systems"][scene["system"]][0],
                                       (center[0], center[1], BENCH_SYSTEM_HEIGHT_CM), unreal.Rotator(), scene["label"] + "_System", folder)
    return actor, {"system_actor": system_actor.get_actor_label(), "preset": preset_path}


def build_scene_turret(context, scene, center, folder):
    particle_stage, fire_stage, actors, created = context["particle_stage"], context["fire_stage"], context["actors"], context["created"]
    assets_by_name = context["assets"]
    preset, preset_path, effective = context["presets"][scene["preset"]]
    spawn_static_mesh(actors, particle_stage, created, assets_by_name["cylinder"], (center[0], center[1], TURRET_MOUNT_HEIGHT_CM), unreal.Rotator(),
                      TURRET_MOUNT_SCALE, scene["label"] + "_Mount", folder)
    nozzle = (center[0] + TURRET_NOZZLE_OFFSET[0], center[1] + TURRET_NOZZLE_OFFSET[1], TURRET_NOZZLE_OFFSET[2])
    system_actor = spawn_niagara_actor(actors, particle_stage, created, context["systems"][scene["system"]][0], nozzle, unreal.Rotator(),
                                       scene["label"] + "_System", folder)
    cube_center = (center[0] + TURRET_CUBE_DISTANCE_CM, center[1], TURRET_CUBE_SIZE_CM * 0.5)
    cube_scale = TURRET_CUBE_SIZE_CM / (BASIC_SHAPE_HALF_SIZE_CM * 2.0)
    cube = spawn_static_mesh(actors, particle_stage, created, assets_by_name["cube"], cube_center, unreal.Rotator(), (cube_scale, cube_scale, cube_scale),
                             scene["label"] + "_Cube", folder)
    add_obstacle_component(cube, context["obstacle_class"])
    kill_box_size = (TURRET_CUBE_SIZE_CM, TURRET_CUBE_SIZE_CM, TURRET_CUBE_SIZE_CM)
    system_actor.niagara_component.set_variable_vec3(KILL_BOX_CENTER_PARAMETER, unreal.Vector(*cube_center))
    system_actor.niagara_component.set_variable_vec3(KILL_BOX_SIZE_PARAMETER, unreal.Vector(*kill_box_size))
    actor = spawn_fluid_actor(actors, particle_stage, created, context["fire_actor_class"], (center[0] + TURRET_FIRE_OFFSET_CM, center[1], 0.0), scene["label"], folder)
    fire_stage.configure_fire_actor(actor, context["flamethrower_actor_spec"], preset, effective, assets_by_name)
    return actor, {"system_actor": system_actor.get_actor_label(), "preset": preset_path, "nozzle": list(nozzle), "cube": cube.get_actor_label(),
                   "obstacle_components": [cube.get_actor_label()],
                   "kill_box": {"center": list(cube_center), "size_cm": list(kill_box_size), "source": "NiagaraComponent User 재정의"}}


def analytic_slope_height(center, x):
    pitch = math.radians(-SLOPE_PITCH_DEGREES)
    half_thickness = BASIC_SHAPE_HALF_SIZE_CM * SLOPE_SCALE[2]
    return center[2] - math.tan(pitch) * (x - center[0]) + half_thickness / math.cos(pitch)


def trace_slope_height(world, slope_center, x, y, ignored):
    expected = analytic_slope_height(slope_center, x)
    start = unreal.Vector(x, y, expected + SLOPE_TRACE_RANGE_CM)
    end = unreal.Vector(x, y, expected - SLOPE_TRACE_RANGE_CM)
    hit = unreal.SystemLibrary.line_trace_single(world, start, end, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, ignored, unreal.DrawDebugTrace.NONE, True)
    values = hit.to_tuple() if hit is not None else None
    if not values or not values[0]:
        raise RuntimeError(f"비탈 트레이스 실패({x:.0f}, {y:.0f}): 런타임 LineTrace 지형 높이(X-04)도 실패한다")
    traced = float(values[5].z)
    if abs(traced - expected) > SLOPE_TRACE_TOLERANCE_CM:
        raise RuntimeError(f"비탈 트레이스 높이 불일치({x:.0f}, {y:.0f}): 트레이스 {traced:.1f}, 해석 {expected:.1f}")
    return {"x": x, "y": y, "traced_z": traced, "analytic_z": expected}


def build_scene_slope(context, scene, center, folder):
    particle_stage, actors, created, world = context["particle_stage"], context["actors"], context["created"], context["world"]
    assets_by_name = context["assets"]
    preset, preset_path, effective = context["presets"][scene["preset"]]
    slope_center = (center[0], center[1], SLOPE_CENTER_HEIGHT_CM)
    slope_actor = spawn_static_mesh(actors, particle_stage, created, assets_by_name["cube"], slope_center, unreal.Rotator(pitch=SLOPE_PITCH_DEGREES),
                                    SLOPE_SCALE, scene["label"] + "_Slope", folder)
    probes = [trace_slope_height(world, slope_center, center[0], center[1], [])]
    rock_labels = []
    for rock in ROCK_SPECS:
        x, y = center[0] + rock["offset"][0], center[1] + rock["offset"][1]
        probe = trace_slope_height(world, slope_center, x, y, [])
        probes.append(probe)
        radius = BASIC_SHAPE_HALF_SIZE_CM * rock["scale"]
        rock_actor = spawn_static_mesh(actors, particle_stage, created, assets_by_name["sphere"], (x, y, probe["traced_z"] + radius * ROCK_EMBED_RATIO),
                                       unreal.Rotator(), (rock["scale"], rock["scale"], rock["scale"]), scene["label"] + "_" + rock["name"], folder)
        add_obstacle_component(rock_actor, context["obstacle_class"])
        rock_labels.append(rock_actor.get_actor_label())
    system_x, system_y = center[0] + AVALANCHE_SYSTEM_OFFSET[0], center[1] + AVALANCHE_SYSTEM_OFFSET[1]
    system_probe = trace_slope_height(world, slope_center, system_x, system_y, [])
    probes.append(system_probe)
    system_location = (system_x, system_y, system_probe["traced_z"] + AVALANCHE_SYSTEM_LIFT_CM)
    system_actor = spawn_niagara_actor(actors, particle_stage, created, context["systems"][scene["system"]][0], system_location, unreal.Rotator(),
                                       scene["label"] + "_System", folder)
    actor = spawn_fluid_actor(actors, particle_stage, created, context["live_actor_class"], (center[0], center[1], probes[0]["traced_z"]), scene["label"], folder)
    particle_stage.configure_live_actor(actor, preset, effective)
    return actor, {"system_actor": system_actor.get_actor_label(), "preset": preset_path, "slope": slope_actor.get_actor_label(),
                   "slope_pitch_degrees": SLOPE_PITCH_DEGREES, "slope_trace": probes, "obstacle_components": rock_labels,
                   "height_source": "LineTrace(X-04, 런타임 FX-9 지형 높이 기본값)"}


SCENE_BUILDERS = {"bench": build_scene_bench, "turret": build_scene_turret, "slope": build_scene_slope}


def build_camera(scene, center):
    camera = scene["camera"]
    awake = {"TDGen_" + other["label"]: other["label"] == scene["label"] for other in SCENE_SPECS}
    return {"pawn": [center[0] + camera["pawn"][0], center[1] + camera["pawn"][1]], "arm": camera["arm"], "view": list(camera["view"]), "awake": awake}


def main():
    particle_stage = load_module("td_fluid_particle_stage", "Tools", "FluidFX", "editor_make_fluid_particle_stage.py")
    fire_stage = load_module("td_fluid_fire_stage", "Tools", "FluidFX", "editor_make_fluid_fire_stage.py")
    editor = particle_stage.require_editor_ready(tuple(particle_stage.REQUIRED_UNREAL_NAMES) + EXTRA_REQUIRED_UNREAL_NAMES)
    overwrite = particle_stage.read_overwrite_request(REQUEST_RELATIVE_PATH)
    preset_paths = [f"{particle_stage.PRESET_DIR}/DA_TDFluid_{spec['name']}" for spec in PRESET_SPECS]
    particle_stage.guard_existing_assets(preset_paths + [STAGE_MAP_PATH], overwrite, REQUEST_RELATIVE_PATH)
    print("단계: 프리셋·모양 정본 검사·다시 읽기")
    tuning_sha256, look_sha256 = particle_stage.check_and_reload_preset_data()
    live_actor_class = unreal.load_class(None, particle_stage.FLUID_ACTOR_CLASS_PATH)
    fire_actor_class = unreal.load_class(None, fire_stage.CLASS_PATHS["TDFluidFireActor"])
    if live_actor_class is None or fire_actor_class is None:
        raise RuntimeError("유체·불 액터 클래스 없음(재빌드와 에디터 재시작 필요)")
    contract_problems = particle_stage.find_missing_properties(unreal.TDFluidPresetAsset.get_default_object(), particle_stage.PRESET_REQUIRED_PROPERTIES + PRESET_EXTRA_PROPERTIES)
    contract_problems += particle_stage.find_missing_properties(unreal.get_default_object(live_actor_class), particle_stage.ACTOR_REQUIRED_PROPERTIES)
    if contract_problems:
        raise RuntimeError("C++ 계약 속성 없음(재빌드와 에디터 재시작 필요): " + ", ".join(contract_problems))
    obstacle_class = load_obstacle_component_class(particle_stage)
    systems = {scene["system"]: particle_stage.load_niagara_system(scene["system"]) for scene in SCENE_SPECS}
    assets_by_name = load_required_assets(fire_stage)
    warnings = []
    mass_bench_channel = describe_mass_bench_channel()
    if not mass_bench_channel:
        warnings.append("Kinds.MassInputBench.TDFluidMassInputSettings.DataChannel 이 비었거나 아직 로드되지 않음: M3 NDC 벤치는 부모가 6.1 뒤 경로를 넣을 때까지 판독 0(R5-M4)")

    print("단계: 프리셋")
    presets = {spec["name"]: configure_mass_preset(particle_stage, spec, assets_by_name) for spec in PRESET_SPECS}

    print("단계: 레벨")
    levels, actors = particle_stage.open_empty_level(STAGE_MAP_PATH)
    world = editor.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", unreal.EditorAssetLibrary.load_blueprint_class(fire_stage.GAME_MODE_PATH))
    created = []
    floor_center = ((len(SCENE_SPECS) - 1) * SCENE_SPACING_CM * 0.5, 0.0)
    floor_scale = (len(SCENE_SPECS) * SCENE_SPACING_CM / 100.0 + 10.0, SCENE_SPACING_CM / 100.0 + 10.0, 0.5)
    particle_stage.place_floor_and_lights(actors, created, STAGE_FOLDER, floor_center, floor_scale, assets_by_name["floor_material"])
    context = {"particle_stage": particle_stage, "fire_stage": fire_stage, "actors": actors, "created": created, "world": world,
               "assets": assets_by_name, "presets": presets, "systems": systems, "obstacle_class": obstacle_class,
               "live_actor_class": live_actor_class, "fire_actor_class": fire_actor_class,
               "flamethrower_actor_spec": register_flamethrower_actor_spec(fire_stage)}
    scene_records = []
    for index, scene in enumerate(SCENE_SPECS):
        center = (index * SCENE_SPACING_CM, 0.0)
        folder = f"{STAGE_FOLDER}/{scene['id']}"
        actor, details = SCENE_BUILDERS[scene["layout"]](context, scene, center, folder)
        record = {"scene": scene["id"], "label": actor.get_actor_label(), "cd_ids": scene["cd_ids"], "refs": scene["refs"],
                  "kind": next(spec["kind"] for spec in PRESET_SPECS if spec["name"] == scene["preset"]),
                  "niagara_system": systems[scene["system"]][1], "override_preset_defaults": False, "camera": build_camera(scene, center)}
        record.update(details)
        scene_records.append(record)

    print("단계: 레벨 저장")
    if not levels.save_current_level():
        raise RuntimeError(f"무대 레벨 저장 실패: {STAGE_MAP_PATH}")
    particle_stage.require_saved_files(preset_paths, STAGE_MAP_PATH)
    report = {"preset_tuning_sha256": tuning_sha256, "material_look_sha256": look_sha256, "map": STAGE_MAP_PATH, "overwrite": overwrite,
              "presets": {spec["name"]: {"path": presets[spec["name"]][1], "kind": spec["kind"], "default_channel": spec["channel"]} for spec in PRESET_SPECS},
              "mass_bench_data_channel": mass_bench_channel, "scenes": scene_records, "created_actors": len(created), "warnings": warnings, "saved": True}
    report_path = particle_stage.write_report(report, REPORT_RELATIVE_PATH)
    print(json.dumps(report, ensure_ascii=False, default=str))
    print("보고서: " + report_path)


if __name__ == "__main__":
    main()
