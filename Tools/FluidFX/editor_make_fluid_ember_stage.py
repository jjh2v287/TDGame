"""실행 환경: TDGame 언리얼 에디터 안(Python 원격 실행). 불티 무대 LV_TDFluidEmberStage(어두운 불투명 바닥 = 깊이 버퍼 충돌, E-A 모닥불 ISM 기준 / E-B 모닥불 + 프리셋 ExternalNiagaraSystem = NS_TD_FluidEmberHeat + ISM 숨김 / E-C 고정 횃불 막대 + 나이아가라 + ISM 숨김)를 만든다. 먼저 editor_make_fluid_fire_stage.py(불꽃 MI·바닥 MI)와 NS_TD_FluidEmberHeat 생성이 끝나 있어야 한다.
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_ember_stage.py (PowerShell, L-repo-01)
출력: /Game/FluidFX/Presets/DA_TDFluid_EmberStage{CampfireIsm,CampfireNiagara,TorchNiagara}, /Game/FluidFX/Maps/LV_TDFluidEmberStage, Saved/FluidFX/fluid_ember_stage.json(preset_tuning_sha256·material_look_sha256, 장면마다 cd_ids·niagara_system·kind·카메라 제안). 기존 에셋은 Saved/FluidFX/fluid_ember_stage_request.json 의 {"overwrite": true} 가 있을 때만 갱신하고 미저장 작업이 있으면 중단한다.
상태: 미검증(2026-10-25 FX-8n B4 작성: py_compile·가짜 unreal 가져오기만, 에디터 실행 전). 프리셋 필드(출력 불꽃 MI·표면 메시·볼륨 렌더·평면·채널·정렬)는 editor_make_fluid_fire_stage.py 의 PRESET_SPECS·configure_preset·configure_fire_actor 를 그대로 불러 복제 없이 재사용하고 external_niagara_system 만 더한다(반박 M6). ISM 숨김 A/B 의 CPU 비용 비교는 K-3 뒤 다시 잰다.
"""
import copy
import json

import unreal

import importlib.util
import os

STAGE_MAP_PATH = "/Game/FluidFX/Maps/LV_TDFluidEmberStage"
EMBER_SYSTEM_NAME = "NS_TD_FluidEmberHeat"
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_ember_stage_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_ember_stage.json")
SCENE_SPACING_CM = 1200.0
CAMERA_ARM_CM = 300.0
CAMERA_VIEW = (-3.0, 80.0)
SCENE_SPECS = (
    {"name": "EmberA_Campfire", "scene": "E-A", "fire_preset": "CampfireFlame", "fire_actor": "CampfireFire", "preset_name": "EmberStageCampfireIsm",
     "uses_niagara": False, "hides_ism": False, "is_torch": False, "use": "기준(ISM 불티)"},
    {"name": "EmberB_Campfire", "scene": "E-B", "fire_preset": "CampfireFlame", "fire_actor": "CampfireFire", "preset_name": "EmberStageCampfireNiagara",
     "uses_niagara": True, "hides_ism": True, "is_torch": False, "use": "ISM 은 돌지만 숨김, 나이아가라 불티만 보임"},
    {"name": "EmberC_Torch", "scene": "E-C", "fire_preset": "TorchFlame", "fire_actor": "TorchFire", "preset_name": "EmberStageTorchNiagara",
     "uses_niagara": True, "hides_ism": True, "is_torch": True, "use": "고정 횃불 막대 + 나이아가라"},
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


def find_fire_preset_spec(fire_stage, name):
    for spec in fire_stage.PRESET_SPECS:
        if spec["name"] == name:
            return spec
    raise RuntimeError(f"불 무대 프리셋 사양 없음: {name}")


def load_fire_assets(fire_stage):
    assets = unreal.EditorAssetLibrary
    assets_by_name = {
        "cube": assets.load_asset(fire_stage.CUBE_MESH_PATH),
        "plane": assets.load_asset(fire_stage.PLANE_MESH_PATH),
        "sphere": assets.load_asset(fire_stage.SPHERE_MESH_PATH),
        "cylinder": assets.load_asset(fire_stage.CYLINDER_MESH_PATH),
        "ember_material": assets.load_asset(fire_stage.EMBER_INSTANCE_PATH),
        "floor_material": assets.load_asset(fire_stage.FLOOR_INSTANCE_PATH),
    }
    for scene_name, instance_path in fire_stage.FLAME_INSTANCE_PATHS.items():
        assets_by_name["flame_" + scene_name] = assets.load_asset(instance_path)
    missing = [name for name, value in assets_by_name.items() if value is None]
    if missing:
        raise RuntimeError("필수 에셋 없음(editor_make_fluid_fire_stage.py 먼저): " + ", ".join(missing))
    return assets_by_name


def configure_scene_preset(fire_stage, scene, assets_by_name, ember_system):
    preset_spec = copy.deepcopy(find_fire_preset_spec(fire_stage, scene["fire_preset"]))
    preset_spec["name"] = scene["preset_name"]
    preset, effective = fire_stage.configure_preset(preset_spec, assets_by_name)
    preset.set_editor_property("external_niagara_system", ember_system if scene["uses_niagara"] else None)
    fire_stage.save_asset(preset, fire_stage.preset_asset_path(preset_spec))
    return preset, effective, fire_stage.preset_asset_path(preset_spec)


def main():
    particle_stage = load_module("td_fluid_particle_stage", "Tools", "FluidFX", "editor_make_fluid_particle_stage.py")
    fire_stage = load_module("td_fluid_fire_stage", "Tools", "FluidFX", "editor_make_fluid_fire_stage.py")
    editor = particle_stage.require_editor_ready(particle_stage.REQUIRED_UNREAL_NAMES)
    overwrite = particle_stage.read_overwrite_request(REQUEST_RELATIVE_PATH)
    preset_paths = [f"{particle_stage.PRESET_DIR}/DA_TDFluid_{scene['preset_name']}" for scene in SCENE_SPECS]
    particle_stage.guard_existing_assets(preset_paths + [STAGE_MAP_PATH], overwrite, REQUEST_RELATIVE_PATH)
    print("단계: 프리셋·모양 정본 검사·다시 읽기")
    tuning_sha256, look_sha256 = particle_stage.check_and_reload_preset_data()
    ember_system, ember_system_path = particle_stage.load_niagara_system(EMBER_SYSTEM_NAME)
    fire_actor_class = unreal.load_class(None, fire_stage.CLASS_PATHS["TDFluidFireActor"])
    if fire_actor_class is None:
        raise RuntimeError("불 액터 클래스 없음: " + fire_stage.CLASS_PATHS["TDFluidFireActor"])
    assets_by_name = load_fire_assets(fire_stage)

    print("단계: 프리셋(불 무대 사양 재사용 + 외부 나이아가라)")
    presets = {scene["name"]: configure_scene_preset(fire_stage, scene, assets_by_name, ember_system) for scene in SCENE_SPECS}

    print("단계: 레벨")
    levels, actors = particle_stage.open_empty_level(STAGE_MAP_PATH)
    world = editor.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", unreal.EditorAssetLibrary.load_blueprint_class(fire_stage.GAME_MODE_PATH))
    created = []
    folder = "TDGen/FluidFX/EmberStage"
    particle_stage.place_floor_and_lights(actors, created, folder, (SCENE_SPACING_CM, 0.0), (40.0, 14.0, 0.5), assets_by_name["floor_material"])
    scene_records = []
    for index, scene in enumerate(SCENE_SPECS):
        location = (index * SCENE_SPACING_CM, 0.0, 100.0 if scene["is_torch"] else 0.0)
        preset, effective, preset_path = presets[scene["name"]]
        actor = actors.spawn_actor_from_class(fire_actor_class, unreal.Vector(*location), unreal.Rotator())
        particle_stage.finish_actor(actor, scene["name"], f"{folder}/{scene['scene']}", created)
        fire_stage.configure_fire_actor(actor, fire_stage.FIRE_ACTOR_SPECS[scene["fire_actor"]], preset, effective, assets_by_name)
        if scene["is_torch"]:
            actor.set_editor_property("should_follow_player_pawn", False)
            actor.set_editor_property("source_prop_mesh_asset", assets_by_name["cylinder"])
        ember_component = actor.get_editor_property("ember_component")
        ember_component.set_editor_property("hidden_in_game", scene["hides_ism"])
        scene_records.append({
            "scene": scene["scene"], "label": actor.get_actor_label(), "cd_ids": [], "kind": scene["fire_preset"], "use": scene["use"],
            "niagara_system": ember_system_path if scene["uses_niagara"] else None, "preset": preset_path, "ism_hidden": scene["hides_ism"],
            "camera": {"pawn": [location[0] + 120.0, location[1] - 450.0], "arm": CAMERA_ARM_CM, "view": list(CAMERA_VIEW),
                       "awake": {actor.get_actor_label(): True}},
        })

    print("단계: 레벨 저장")
    if not levels.save_current_level():
        raise RuntimeError(f"무대 레벨 저장 실패: {STAGE_MAP_PATH}")
    particle_stage.require_saved_files(preset_paths, STAGE_MAP_PATH)
    report = {"preset_tuning_sha256": tuning_sha256, "material_look_sha256": look_sha256, "map": STAGE_MAP_PATH, "overwrite": overwrite,
              "scenes": scene_records, "created_actors": len(created), "saved": True}
    report_path = particle_stage.write_report(report, REPORT_RELATIVE_PATH)
    print(json.dumps(report, ensure_ascii=False, default=str))
    print("보고서: " + report_path)


if __name__ == "__main__":
    main()
