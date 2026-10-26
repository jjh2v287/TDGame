"""실행 환경: TDGame 언리얼 에디터 안(Python 원격 실행). 유체 구동 입자 무대 LV_TDFluidParticleStage(장면 P1~P11 = CD S.3.4.1~11, 장면마다 라이브 액터 1 + 교반기 BP_TDFluidStageStirrer 1, 로컬 장면 P6~P11 은 Yaw 30°, 그림자 받는 바닥)를 만든다. 먼저 editor_make_fluid_particle_material.py 와 editor_make_fluid_driven_niagara.py 7종 생성이 끝나 있어야 한다. 다른 무대 스크립트(불티·연기 셋업)가 importlib 로 공용 함수를 가져간다.
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_particle_stage.py (PowerShell, L-repo-01)
출력: /Game/FluidFX/Blueprints/BP_TDFluidStageStirrer, /Game/FluidFX/Presets/DA_TDFluid_Drive<장면>, /Game/FluidFX/Maps/LV_TDFluidParticleStage, Saved/FluidFX/fluid_particle_stage.json(preset_tuning_sha256·material_look_sha256, 장면마다 cd_ids·niagara_system·kind·output_material·카메라 제안). 기존 에셋은 Saved/FluidFX/fluid_particle_stage_request.json 의 {"overwrite": true} 가 있을 때만 갱신하고 미저장 작업이 있으면 중단한다.
상태: 미검증(2026-10-25 FX-8n B4 작성: py_compile·가짜 unreal 가져오기만, 에디터 실행 전). 프리셋은 PresetKind·ExternalNiagaraSystem 만 정하고 bOverridePresetDefaults 는 끈다(값은 JSON 종류 값). 출력 재질은 모든 장면에서 없음(입자만 보임, 반박 M6). 교반기는 그래프 로직 0, 궤도 값은 시험 장치 상수다.
"""
import hashlib
import importlib.util
import json
import os

import unreal

FLUID_ROOT = "/Game/FluidFX"
PRESET_DIR = f"{FLUID_ROOT}/Presets"
BLUEPRINT_DIR = f"{FLUID_ROOT}/Blueprints"
MAP_DIR = f"{FLUID_ROOT}/Maps"
NIAGARA_DIR = f"{FLUID_ROOT}/Niagara"
STAGE_MAP_PATH = f"{MAP_DIR}/LV_TDFluidParticleStage"
STIRRER_BLUEPRINT_NAME = "BP_TDFluidStageStirrer"
STIRRER_BLUEPRINT_PATH = f"{BLUEPRINT_DIR}/{STIRRER_BLUEPRINT_NAME}"
GAME_MODE_PATH = "/Game/Combat/Blueprints/BP_TDCombatGameMode"
CUBE_MESH_PATH = "/Engine/BasicShapes/Cube"
SPHERE_MESH_PATH = "/Engine/BasicShapes/Sphere"
FLUID_ACTOR_CLASS_PATH = "/Script/TDGame.TDFluidLiveActor"
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_particle_stage_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_particle_stage.json")
PRESET_TUNING_RELATIVE_PATH = ("Content", "FluidFX", "PresetData", "TDFluidPresetTuning.json")
MATERIAL_LOOK_RELATIVE_PATH = ("Content", "FluidFX", "PresetData", "TDFluidMaterialLook.json")
PRESET_CHECKER_RELATIVE_PATH = ("Tools", "check_fluid_preset_data.py")

STIRRER_COLLISION_PROFILE = "OverlapAllDynamic"
STIRRER_SCALE = 0.4
STIRRER_ORBIT_RADIUS_CM = 150.0
STIRRER_HEIGHT_CM = 20.0
STIRRER_DEGREES_PER_SEC = 90.0
SCENE_SPACING_CM = 1500.0
LOCAL_YAW_DEGREES = 30.0
CAMERA_ARM_CM = 900.0
CAMERA_VIEW = (-35.0, 0.0)
FLOOR_COLOR = (0.18, 0.18, 0.2)
PLAYER_START = (0.0, -900.0, 96.0)

SCENE_SPECS = (
    {"name": "P1", "cd_ids": ["S.3.4.1"], "kind": "FOOTPRINT_PAINTER", "system": "NS_TD_FluidDriveSprite", "local": False},
    {"name": "P2", "cd_ids": ["S.3.4.2"], "kind": "SMOKE", "system": "NS_TD_FluidDriveSprite", "local": False},
    {"name": "P3", "cd_ids": ["S.3.4.3"], "kind": "SMOKE", "system": "NS_TD_FluidDriveMeshFloat", "local": False},
    {"name": "P4", "cd_ids": ["S.3.4.4"], "kind": "VORTEX", "system": "NS_TD_FluidDriveMeshFloat", "local": False},
    {"name": "P5", "cd_ids": ["S.3.4.5"], "kind": "WATER", "system": "NS_TD_FluidDriveMeshFloat", "local": False},
    {"name": "P6", "cd_ids": ["S.3.4.6"], "kind": "SMOKE", "system": "NS_TD_FluidDriveSpriteLocal", "local": True},
    {"name": "P7", "cd_ids": ["S.3.4.7"], "kind": "FOOTPRINT_PAINTER", "system": "NS_TD_FluidDriveSpriteLocal", "local": True},
    {"name": "P8", "cd_ids": ["S.3.4.8"], "kind": "VORTEX", "system": "NS_TD_FluidDriveDensityLocal", "local": True},
    {"name": "P9", "cd_ids": ["S.3.4.9"], "kind": "INVERTED_VORTEX", "system": "NS_TD_FluidDriveDensityLocal", "local": True},
    {"name": "P10", "cd_ids": ["S.3.4.10"], "kind": "VORTEX", "system": "NS_TD_FluidDriveStroke", "local": True},
    {"name": "P11", "cd_ids": ["S.3.4.11"], "kind": "VORTEX", "system": "NS_TD_FluidDriveMeshArray", "local": True},
)
PRESET_REQUIRED_PROPERTIES = ("preset_kind", "override_preset_defaults", "external_niagara_system", "simulation_config")
ACTOR_REQUIRED_PROPERTIES = ("live_component", "should_follow_player_pawn", "should_use_activation_volume", "accept_mouse_brush")
REQUIRED_UNREAL_NAMES = (
    "get_engine_subsystem",
    "TDFluidPresetTuningSubsystem",
    "TDFluidPresetAsset",
    "TDFluidPresetKind",
    "DataAssetFactory",
    "BlueprintFactory",
    "Blueprint",
    "BlueprintEditorLibrary.compile_blueprint",
    "SubobjectDataSubsystem",
    "SubobjectDataBlueprintFunctionLibrary.get_object",
    "AddNewSubobjectParams",
    "RotatingMovementComponent",
    "StaticMeshComponent",
    "ComponentMobility.MOVABLE",
    "ComponentMobility.STATIC",
    "EditorLoadingAndSavingUtils.get_dirty_map_packages",
    "EditorLoadingAndSavingUtils.get_dirty_content_packages",
    "SystemLibrary.get_project_directory",
    "NiagaraSystem",
    "DirectionalLight",
    "SkyLight",
    "PlayerStart",
)


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


def asset_disk_path(asset_path, extension):
    return project_path("Content", *asset_path[len("/Game/"):].split("/")) + extension


def load_module(module_name, *relative_parts):
    module_path = project_path(*relative_parts)
    module_spec = importlib.util.spec_from_file_location(module_name, module_path)
    if module_spec is None or module_spec.loader is None:
        raise RuntimeError(f"모듈 없음: {module_path}")
    module = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(module)
    return module


def find_missing_unreal_names(names):
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


def read_overwrite_request(relative_path):
    request_path = project_path(*relative_path)
    if not os.path.isfile(request_path):
        return False
    with open(request_path, encoding="utf-8") as request_file:
        request = json.load(request_file)
    return isinstance(request, dict) and request.get("overwrite") is True


def find_unsaved_package_names(overwrite):
    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    dirty.extend(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    names = sorted({package.get_name() for package in dirty})
    return [name for name in names if not name.startswith(FLUID_ROOT + "/")] if overwrite else names


def guard_existing_assets(asset_paths, overwrite, request_relative_path):
    existing = [path for path in asset_paths if unreal.EditorAssetLibrary.does_asset_exist(path)]
    if existing and not overwrite:
        raise RuntimeError(f"기존 에셋 보존: {', '.join(existing)} (갱신은 {os.path.join(*request_relative_path)} 에 {{\"overwrite\": true}})")
    unsaved = find_unsaved_package_names(overwrite)
    if unsaved:
        raise RuntimeError("미저장 작업을 보존합니다: " + ", ".join(unsaved))


def ensure_directory(directory_path):
    if unreal.EditorAssetLibrary.does_directory_exist(directory_path):
        return
    if not unreal.EditorAssetLibrary.make_directory(directory_path):
        raise RuntimeError(f"폴더 생성 실패: {directory_path}")


def save_asset(asset, asset_path):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"에셋 저장 실패: {asset_path}")


def file_sha256(relative_path):
    with open(project_path(*relative_path), "rb") as source_file:
        return hashlib.sha256(source_file.read()).hexdigest()


def check_and_reload_preset_data():
    checker = load_module("check_fluid_preset_data", *PRESET_CHECKER_RELATIVE_PATH)
    tuning_sha256 = file_sha256(PRESET_TUNING_RELATIVE_PATH)
    look_sha256 = file_sha256(MATERIAL_LOOK_RELATIVE_PATH)
    errors, _ = checker.inspect_tuning_file(project_path(*PRESET_TUNING_RELATIVE_PATH))
    look_errors, _ = checker.inspect_look_file(project_path(*MATERIAL_LOOK_RELATIVE_PATH))
    if errors or look_errors:
        raise RuntimeError("프리셋·모양 정본 검사 실패(python Tools/check_fluid_preset_data.py): " + " | ".join((errors + look_errors)[:20]))
    tuning = unreal.get_engine_subsystem(unreal.TDFluidPresetTuningSubsystem)
    if tuning is None:
        raise RuntimeError("TDFluidPresetTuningSubsystem 없음(재빌드와 에디터 재시작 필요)")
    error_count = tuning.reload_preset_tuning()
    if error_count != 0:
        raise RuntimeError(f"프리셋 정본 다시 읽기 오류 {error_count}건: " + " | ".join(tuning.get_load_errors()))
    if file_sha256(PRESET_TUNING_RELATIVE_PATH) != tuning_sha256 or file_sha256(MATERIAL_LOOK_RELATIVE_PATH) != look_sha256:
        raise RuntimeError("정본 JSON 이 검사 도중 바뀌었습니다. 다시 실행하세요.")
    return tuning_sha256, look_sha256


def open_empty_level(map_path):
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    ensure_directory(map_path.rsplit("/", 1)[0])
    if not unreal.EditorAssetLibrary.does_asset_exist(map_path):
        if not levels.new_level(map_path):
            raise RuntimeError(f"무대 레벨 생성 실패: {map_path}")
        return levels, actors
    if not levels.load_level(map_path):
        raise RuntimeError(f"기존 무대 레벨 열기 실패: {map_path}")
    for actor in actors.get_all_level_actors():
        if isinstance(actor, unreal.WorldSettings) or type(actor) is unreal.Brush:
            continue
        if not actors.destroy_actor(actor):
            raise RuntimeError(f"기존 무대 레벨 액터 제거 실패: {actor.get_actor_label()}")
    return levels, actors


def add_subobject(subobjects, blueprint, parent_handle, component_class):
    params = unreal.AddNewSubobjectParams()
    params.set_editor_property("parent_handle", parent_handle)
    params.set_editor_property("new_class", component_class)
    params.set_editor_property("blueprint_context", blueprint)
    new_handle, failure_reason = subobjects.add_new_subobject(params)
    if str(failure_reason):
        raise RuntimeError(f"{component_class.__name__} 추가 실패: {failure_reason}")
    return unreal.SubobjectDataBlueprintFunctionLibrary.get_object(subobjects.k2_find_subobject_data_from_handle(new_handle))


def load_or_create_stirrer_blueprint():
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(STIRRER_BLUEPRINT_PATH):
        return assets.load_blueprint_class(STIRRER_BLUEPRINT_PATH), False
    ensure_directory(BLUEPRINT_DIR)
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.Actor)
    blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(STIRRER_BLUEPRINT_NAME, BLUEPRINT_DIR, unreal.Blueprint, factory)
    if blueprint is None:
        raise RuntimeError(f"교반기 블루프린트 생성 실패: {STIRRER_BLUEPRINT_PATH}")
    subobjects = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    handles = list(subobjects.k2_gather_subobject_data_for_blueprint(blueprint))
    if not handles:
        raise RuntimeError("교반기 블루프린트 서브오브젝트 목록이 비었습니다.")
    mesh = add_subobject(subobjects, blueprint, handles[0], unreal.StaticMeshComponent)
    mesh.set_editor_property("static_mesh", assets.load_asset(SPHERE_MESH_PATH))
    mesh.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    mesh.set_editor_property("generate_overlap_events", True)
    mesh.set_collision_profile_name(STIRRER_COLLISION_PROFILE)
    mesh.set_editor_property("relative_scale3d", unreal.Vector(STIRRER_SCALE, STIRRER_SCALE, STIRRER_SCALE))
    rotating = add_subobject(subobjects, blueprint, handles[0], unreal.RotatingMovementComponent)
    rotating.set_editor_property("rotation_rate", unreal.Rotator(roll=0.0, pitch=0.0, yaw=STIRRER_DEGREES_PER_SEC))
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    save_asset(blueprint, STIRRER_BLUEPRINT_PATH)
    return assets.load_blueprint_class(STIRRER_BLUEPRINT_PATH), True


def load_or_create_preset(name):
    path = f"{PRESET_DIR}/DA_TDFluid_{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        preset = unreal.EditorAssetLibrary.load_asset(path)
        if not isinstance(preset, unreal.TDFluidPresetAsset):
            raise RuntimeError(f"기존 프리셋 에셋 형식 불일치: {path}")
        return preset, path
    ensure_directory(PRESET_DIR)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.TDFluidPresetAsset)
    preset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(f"DA_TDFluid_{name}", PRESET_DIR, unreal.TDFluidPresetAsset, factory)
    if preset is None:
        raise RuntimeError(f"프리셋 생성 실패: {path}")
    return preset, path


def load_niagara_system(system_name):
    path = f"{NIAGARA_DIR}/{system_name}"
    system = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if system is None:
        raise RuntimeError(f"나이아가라 시스템 없음: {path}(editor_make_fluid_driven_niagara.py 먼저)")
    return system, path


def configure_kind_preset(name, kind_name, niagara_system):
    kind = getattr(unreal.TDFluidPresetKind, kind_name)
    preset, path = load_or_create_preset(name)
    preset.set_editor_property("preset_kind", kind)
    preset.set_editor_property("override_preset_defaults", False)
    preset.set_editor_property("simulation_config", unreal.TDFluidPresetAsset.build_config_for_preset_kind(kind))
    preset.set_editor_property("external_niagara_system", niagara_system)
    if niagara_system is not None and preset.get_editor_property("external_niagara_system") is None:
        raise RuntimeError(f"프리셋 외부 나이아가라 지정 확인 실패: {path}")
    save_asset(preset, path)
    return preset, path, preset.resolve_effective_config()


def configure_live_actor(actor, preset, effective):
    actor.set_editor_property("should_follow_player_pawn", False)
    actor.set_editor_property("should_use_activation_volume", False)
    actor.set_editor_property("accept_mouse_brush", True)
    live_component = actor.get_editor_property("live_component")
    live_component.set_editor_property("active_preset", preset)
    live_component.set_editor_property("should_init_system_state_from_preset", True)
    live_component.set_editor_property("simulation_config", effective)
    return live_component


def place_stirrer(actors, stirrer_class, center, label, folder, created):
    stirrer = actors.spawn_actor_from_class(stirrer_class, unreal.Vector(center[0] + STIRRER_ORBIT_RADIUS_CM, center[1], STIRRER_HEIGHT_CM), unreal.Rotator())
    finish_actor(stirrer, label, folder, created)
    rotating = stirrer.get_component_by_class(unreal.RotatingMovementComponent)
    if rotating is None:
        raise RuntimeError("교반기에 회전 이동 컴포넌트가 없습니다: " + STIRRER_BLUEPRINT_PATH)
    rotating.set_editor_property("pivot_translation", unreal.Vector(-STIRRER_ORBIT_RADIUS_CM, 0.0, 0.0))
    return stirrer


def finish_actor(actor, label, folder, created):
    if actor is None:
        raise RuntimeError(f"액터 생성 실패: {label}")
    actor.set_actor_label("TDGen_" + label)
    actor.set_folder_path(folder)
    created.append(actor)
    return actor


def place_floor_and_lights(actors, created, folder, center, floor_scale, floor_material=None):
    cube = unreal.EditorAssetLibrary.load_asset(CUBE_MESH_PATH)
    floor = actors.spawn_actor_from_object(cube, unreal.Vector(center[0], center[1], -25.0), unreal.Rotator())
    finish_actor(floor, "StageFloor", folder + "/Stage", created)
    floor.set_actor_scale3d(unreal.Vector(*floor_scale))
    floor.static_mesh_component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
    if floor_material is not None:
        floor.static_mesh_component.set_material(0, floor_material)
    sun = finish_actor(actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1500), unreal.Rotator(pitch=-50, yaw=-35)), "StageSun", folder + "/Lighting", created)
    sun.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_cast_shadows(True)
    sky = finish_actor(actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 1000), unreal.Rotator()), "StageSkyLight", folder + "/Lighting", created)
    sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", True)
    finish_actor(actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(*PLAYER_START), unreal.Rotator()), "StagePlayerStart", folder + "/Gameplay", created)


def write_report(report, relative_path):
    report_path = project_path(*relative_path)
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, "w", encoding="utf-8") as report_file:
        json.dump(report, report_file, ensure_ascii=False, indent=2, default=str)
    return report_path


def require_saved_files(asset_paths, map_path):
    expected = [asset_disk_path(path, ".uasset") for path in asset_paths] + [asset_disk_path(map_path, ".umap")]
    missing = [path for path in expected if not os.path.isfile(path)]
    if missing:
        raise RuntimeError("저장 파일 확인 실패: " + ", ".join(missing))


def require_editor_ready(required_names):
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    missing_names = find_missing_unreal_names(required_names)
    if missing_names:
        raise RuntimeError("언리얼 Python 이름 없음: " + ", ".join(missing_names))
    if editor.get_game_world() is not None:
        raise RuntimeError("PIE를 종료한 뒤 실행하세요.")
    if editor.get_editor_world() is None:
        raise RuntimeError("에디터 월드가 없습니다.")
    return editor


def scene_center(index):
    return (index * SCENE_SPACING_CM, 0.0)


def main():
    editor = require_editor_ready(REQUIRED_UNREAL_NAMES)
    overwrite = read_overwrite_request(REQUEST_RELATIVE_PATH)
    preset_paths = [f"{PRESET_DIR}/DA_TDFluid_Drive{spec['name']}" for spec in SCENE_SPECS]
    guard_existing_assets(preset_paths + [STAGE_MAP_PATH, STIRRER_BLUEPRINT_PATH], overwrite, REQUEST_RELATIVE_PATH)
    print("단계: 프리셋·모양 정본 검사·다시 읽기")
    tuning_sha256, look_sha256 = check_and_reload_preset_data()
    fluid_actor_class = unreal.load_class(None, FLUID_ACTOR_CLASS_PATH)
    if fluid_actor_class is None:
        raise RuntimeError("유체 액터 클래스 없음: " + FLUID_ACTOR_CLASS_PATH)
    contract_problems = find_missing_properties(unreal.TDFluidPresetAsset.get_default_object(), PRESET_REQUIRED_PROPERTIES)
    contract_problems += find_missing_properties(unreal.get_default_object(fluid_actor_class), ACTOR_REQUIRED_PROPERTIES)
    if contract_problems:
        raise RuntimeError("C++ 계약 속성 없음(재빌드와 에디터 재시작 필요): " + ", ".join(contract_problems))
    systems = {spec["system"]: load_niagara_system(spec["system"]) for spec in SCENE_SPECS}

    print("단계: 교반기·프리셋")
    stirrer_class, stirrer_created = load_or_create_stirrer_blueprint()
    presets = {spec["name"]: configure_kind_preset("Drive" + spec["name"], spec["kind"], systems[spec["system"]][0]) for spec in SCENE_SPECS}

    print("단계: 레벨")
    levels, actors = open_empty_level(STAGE_MAP_PATH)
    world = editor.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", unreal.EditorAssetLibrary.load_blueprint_class(GAME_MODE_PATH))
    created = []
    folder = "TDGen/FluidFX/ParticleStage"
    place_floor_and_lights(actors, created, folder, ((len(SCENE_SPECS) - 1) * SCENE_SPACING_CM * 0.5, 0.0), (len(SCENE_SPECS) * SCENE_SPACING_CM / 100.0 + 4.0, 16.0, 0.5))
    scene_records = []
    for index, spec in enumerate(SCENE_SPECS):
        center = scene_center(index)
        preset, preset_path, effective = presets[spec["name"]]
        yaw = LOCAL_YAW_DEGREES if spec["local"] else 0.0
        actor = actors.spawn_actor_from_class(fluid_actor_class, unreal.Vector(center[0], center[1], 1.0), unreal.Rotator(yaw=yaw))
        finish_actor(actor, f"Particle{spec['name']}_Fluid", f"{folder}/{spec['name']}", created)
        configure_live_actor(actor, preset, effective)
        stirrer = place_stirrer(actors, stirrer_class, center, f"Particle{spec['name']}_Stirrer", f"{folder}/{spec['name']}", created)
        scene_records.append({
            "scene": spec["name"], "cd_ids": spec["cd_ids"], "kind": spec["kind"], "niagara_system": systems[spec["system"]][1],
            "preset": preset_path, "output_material": None, "override_preset_defaults": False, "yaw_degrees": yaw,
            "fluid_actor": actor.get_actor_label(), "stirrer": stirrer.get_actor_label(),
            "camera": {"pawn": [center[0], center[1] - CAMERA_ARM_CM], "arm": CAMERA_ARM_CM, "view": list(CAMERA_VIEW),
                       "awake": {actor.get_actor_label(): True}},
        })

    print("단계: 레벨 저장")
    if not levels.save_current_level():
        raise RuntimeError(f"무대 레벨 저장 실패: {STAGE_MAP_PATH}")
    require_saved_files(preset_paths + [STIRRER_BLUEPRINT_PATH], STAGE_MAP_PATH)
    report = {"preset_tuning_sha256": tuning_sha256, "material_look_sha256": look_sha256, "map": STAGE_MAP_PATH, "overwrite": overwrite,
              "created_stirrer_blueprint": stirrer_created, "stirrer": {"orbit_radius_cm": STIRRER_ORBIT_RADIUS_CM, "degrees_per_sec": STIRRER_DEGREES_PER_SEC},
              "scenes": scene_records, "created_actors": len(created), "saved": True}
    report_path = write_report(report, REPORT_RELATIVE_PATH)
    print(json.dumps(report, ensure_ascii=False, default=str))
    print("보고서: " + report_path)


if __name__ == "__main__":
    main()
