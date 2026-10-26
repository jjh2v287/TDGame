"""실행 환경: TDGame 언리얼 에디터 안(Python 원격 실행). 연기 셋업 무대 LV_TDFluidSmokeSetupStage(SE.0.3.7.1·.2 촛불 + 하류 연기, SE.0.3.7.3 촛불 5개, S.3.14.8 Vortex·S.3.14.9 InvertedVortex·매뉴얼 2.1·4.7 PortalVortex·S.0.3.21 SteamPuff 각 교반기)를 만든다. 기둥 3종(SE.3.14.5.1~3)·피스톤(S.0.3.18)은 K-1·K-2 계약(TDFluidTypes.h enum 이름, BuoyancyCmPerSec2 ClampMin < 0)이 없으면 짓지 않고 보고 blocked 에 남긴다. 먼저 editor_make_fluid_fire_stage.py·editor_make_fluid_volume_material.py 가 끝나 있어야 한다.
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_smoke_setup_stage.py (PowerShell, L-repo-01)
출력: /Game/FluidFX/Presets/DA_TDFluid_Setup*, /Game/FluidFX/Maps/LV_TDFluidSmokeSetupStage, Saved/FluidFX/fluid_smoke_setup_stage.json(preset_tuning_sha256·material_look_sha256, 장면마다 cd_ids·niagara_system·kind·카메라 제안, blocked, 연기 웅덩이 S.0.3.9·S.3.14.7 은 기존 LV_TDFluidSmokeStage 대응). 기존 에셋은 Saved/FluidFX/fluid_smoke_setup_stage_request.json 의 {"overwrite": true} 가 있을 때만 갱신하고 미저장 작업이 있으면 중단한다.
상태: 미검증(2026-10-25 FX-8n B4 작성: py_compile·가짜 unreal 가져오기만, 에디터 실행 전). 촛불 불꽃 MI 는 횃불 MI 를 빌려 쓴다[유추]. 소용돌이·증기 장면은 연기 볼륨 MI(MI_TD_FluidVolume_SmokeBillow)·큐브·높이 200cm 를 둔다(그 밖의 값은 JSON 종류 값). K 계약이 있어도 기둥·피스톤 장면 생성은 후속(보고 deferred)이다.
"""
import copy
import json
import os
import re

import unreal

import importlib.util

STAGE_MAP_PATH = "/Game/FluidFX/Maps/LV_TDFluidSmokeSetupStage"
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_smoke_setup_stage_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_smoke_setup_stage.json")
TYPES_HEADER_RELATIVE_PATH = ("Source", "TDGame", "FluidFX", "TDFluidTypes.h")
PRESET_KIND_ENUM = "ETDFluidPresetKind"
K2_KIND_NAMES = ("RisingSmokeColumn", "FallingSmokeColumn", "FallingThinSmokeColumn", "SmokePiston")
BUOYANCY_CLAMP_PATTERN = re.compile(r'UPROPERTY\([^)]*ClampMin\s*=\s*"(-?[0-9.]+)"[^)]*\)\s*\n\s*float\s+BuoyancyCmPerSec2\b')
SCENE_SPACING_CM = 1500.0
CANDLE_ROW_SPACING_CM = 60.0
CANDLE_ROW_COUNT = 5
VOLUME_HEIGHT_CM = 200.0
CAMERA_ARM_CM = 500.0
CAMERA_VIEW = (-15.0, 0.0)
CANDLE_PRESET = {"name": "SetupCandle", "kind": "FIRE", "cpp_kind": "CANDLE", "channel": "CHANNEL_B_FIRE", "scene": "torch", "sort_priority": 1,
                 "plane": {"orientation": "CAMERA_FACING", "depth_bias": 10.0, "interaction_depth": 100.0, "center_offset": 0.3}}
CANDLE_SMOKE_PRESET_SOURCE = "CampfireSmoke"
STIRRED_SCENES = (
    {"name": "SetupVortex", "cd_ids": ["S.3.14.8"], "kind": "VORTEX"},
    {"name": "SetupInvertedVortex", "cd_ids": ["S.3.14.9"], "kind": "INVERTED_VORTEX"},
    {"name": "SetupPortalVortex", "cd_ids": ["manual-2.1", "manual-4.7"], "kind": "PORTAL_VORTEX"},
    {"name": "SetupSteamPuff", "cd_ids": ["S.0.3.21"], "kind": "STEAM_PUFF"},
)
CONTRACT_SCENES = (
    {"cd_ids": ["SE.3.14.5.1"], "kind": "RisingSmokeColumn", "needs": ("K-2",)},
    {"cd_ids": ["SE.3.14.5.2"], "kind": "FallingSmokeColumn", "needs": ("K-1", "K-2")},
    {"cd_ids": ["SE.3.14.5.3"], "kind": "FallingThinSmokeColumn", "needs": ("K-1", "K-2")},
    {"cd_ids": ["S.0.3.18"], "kind": "SmokePiston", "needs": ("K-2",)},
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


def read_contracts(particle_stage):
    checker = particle_stage.load_module("check_fluid_preset_data", *particle_stage.PRESET_CHECKER_RELATIVE_PATH)
    schema = checker.load_schema()
    kind_names = set(getattr(schema, "enums", {}).get(PRESET_KIND_ENUM, ()))
    with open(project_path(*TYPES_HEADER_RELATIVE_PATH), encoding="utf-8") as header_file:
        clamp = BUOYANCY_CLAMP_PATTERN.search(header_file.read())
    return {"K-1": clamp is not None and float(clamp.group(1)) < 0.0, "K-2": all(name in kind_names for name in K2_KIND_NAMES)}


def classify_contract_scenes(contracts):
    blocked = []
    deferred = []
    for scene in CONTRACT_SCENES:
        missing = [name for name in scene["needs"] if not contracts[name]]
        record = {"cd_ids": scene["cd_ids"], "kind": scene["kind"]}
        if missing:
            blocked.append(dict(record, blocked=missing))
        else:
            deferred.append(dict(record, note="계약 확인됨, 장면 생성은 후속"))
    return blocked, deferred


def configure_candle(fire_stage, assets_by_name):
    fire_stage.PRESET_KIND_BY_NAME[CANDLE_PRESET["name"]] = CANDLE_PRESET["cpp_kind"]
    preset, effective = fire_stage.configure_preset(copy.deepcopy(CANDLE_PRESET), assets_by_name)
    smoke_spec = copy.deepcopy(next(spec for spec in fire_stage.PRESET_SPECS if spec["name"] == CANDLE_SMOKE_PRESET_SOURCE))
    smoke_spec["name"] = "SetupCandleSmoke"
    smoke_preset, smoke_effective = fire_stage.configure_preset(smoke_spec, assets_by_name)
    actor_spec = copy.deepcopy(fire_stage.FIRE_ACTOR_SPECS["TorchFire"])
    actor_spec["preset"] = CANDLE_PRESET["name"]
    return {"preset": preset, "effective": effective, "smoke_preset": smoke_preset, "smoke_effective": smoke_effective, "actor_spec": actor_spec,
            "paths": [fire_stage.preset_asset_path(CANDLE_PRESET), fire_stage.preset_asset_path(smoke_spec)]}


def configure_stirred_preset(particle_stage, scene, assets_by_name):
    preset, path, effective = particle_stage.configure_kind_preset(scene["name"], scene["kind"], None)
    preset.set_editor_property("volume_material", assets_by_name["smoke_volume"])
    preset.set_editor_property("volume_mesh", assets_by_name["cube"])
    preset.set_editor_property("volume_height_cm", VOLUME_HEIGHT_CM)
    particle_stage.save_asset(preset, path)
    return preset, path, effective


def spawn_candle(actors, fire_actor_class, fire_stage, particle_stage, candle, location, label, folder, created, assets_by_name):
    actor = actors.spawn_actor_from_class(fire_actor_class, unreal.Vector(*location), unreal.Rotator())
    particle_stage.finish_actor(actor, label, folder, created)
    fire_stage.configure_fire_actor(actor, candle["actor_spec"], candle["preset"], candle["effective"], assets_by_name)
    return actor


def camera_for(center, label):
    return {"pawn": [center[0], center[1] - CAMERA_ARM_CM], "arm": CAMERA_ARM_CM, "view": list(CAMERA_VIEW), "awake": {label: True}}


def main():
    particle_stage = load_module("td_fluid_particle_stage", "Tools", "FluidFX", "editor_make_fluid_particle_stage.py")
    fire_stage = load_module("td_fluid_fire_stage", "Tools", "FluidFX", "editor_make_fluid_fire_stage.py")
    editor = particle_stage.require_editor_ready(particle_stage.REQUIRED_UNREAL_NAMES)
    overwrite = particle_stage.read_overwrite_request(REQUEST_RELATIVE_PATH)
    preset_paths = [f"{particle_stage.PRESET_DIR}/DA_TDFluid_{name}" for name in ("SetupCandle", "SetupCandleSmoke")]
    preset_paths += [f"{particle_stage.PRESET_DIR}/DA_TDFluid_{scene['name']}" for scene in STIRRED_SCENES]
    particle_stage.guard_existing_assets(preset_paths + [STAGE_MAP_PATH], overwrite, REQUEST_RELATIVE_PATH)
    print("단계: 프리셋·모양 정본 검사·다시 읽기, K 계약 확인")
    tuning_sha256, look_sha256 = particle_stage.check_and_reload_preset_data()
    contracts = read_contracts(particle_stage)
    blocked, deferred = classify_contract_scenes(contracts)
    classes = {name: unreal.load_class(None, path) for name, path in fire_stage.CLASS_PATHS.items()}
    assets = unreal.EditorAssetLibrary
    assets_by_name = {"cube": assets.load_asset(fire_stage.CUBE_MESH_PATH), "plane": assets.load_asset(fire_stage.PLANE_MESH_PATH),
                      "sphere": assets.load_asset(fire_stage.SPHERE_MESH_PATH), "ember_material": assets.load_asset(fire_stage.EMBER_INSTANCE_PATH),
                      "smoke_volume": assets.load_asset(fire_stage.SMOKE_VOLUME_INSTANCE_PATH)}
    for scene_name, instance_path in fire_stage.FLAME_INSTANCE_PATHS.items():
        assets_by_name["flame_" + scene_name] = assets.load_asset(instance_path)
    missing = [name for name, value in list(assets_by_name.items()) + list(classes.items()) if value is None]
    if missing:
        raise RuntimeError("필수 에셋 또는 클래스 없음(불 무대·볼륨 재질 먼저): " + ", ".join(missing))

    print("단계: 교반기·프리셋")
    stirrer_class, stirrer_created = particle_stage.load_or_create_stirrer_blueprint()
    candle = configure_candle(fire_stage, assets_by_name)
    stirred_presets = {scene["name"]: configure_stirred_preset(particle_stage, scene, assets_by_name) for scene in STIRRED_SCENES}

    print("단계: 레벨")
    levels, actors = particle_stage.open_empty_level(STAGE_MAP_PATH)
    world = editor.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", assets.load_blueprint_class(fire_stage.GAME_MODE_PATH))
    created = []
    folder = "TDGen/FluidFX/SmokeSetupStage"
    scene_count = 2 + len(STIRRED_SCENES)
    particle_stage.place_floor_and_lights(actors, created, folder, ((scene_count - 1) * SCENE_SPACING_CM * 0.5, 0.0), (scene_count * SCENE_SPACING_CM / 100.0 + 4.0, 16.0, 0.5))
    scene_records = []

    candle_actor = spawn_candle(actors, classes["TDFluidFireActor"], fire_stage, particle_stage, candle, (0.0, 0.0, 0.0), "SetupCandle", f"{folder}/Candle", created, assets_by_name)
    candle_smoke = actors.spawn_actor_from_class(classes["TDFluidLiveActor"], unreal.Vector(0.0, 0.0, 40.0), unreal.Rotator())
    particle_stage.finish_actor(candle_smoke, "SetupCandleSmoke", f"{folder}/Candle", created)
    fire_stage.configure_smoke_actor(candle_smoke, candle["smoke_preset"], candle["smoke_effective"], candle_actor)
    feed = unreal.TDFluidDownstreamFeedSettings()
    feed.set_editor_property("is_enabled", True)
    candle_actor.set_editor_property("downstream_smoke_actor", candle_smoke)
    candle_actor.set_editor_property("downstream_feed", feed)
    scene_records.append({"scene": "candle_smoke", "cd_ids": ["SE.0.3.7.1", "SE.0.3.7.2"], "kind": "Candle+CampfireSmoke", "niagara_system": None,
                          "labels": [candle_actor.get_actor_label(), candle_smoke.get_actor_label()], "camera": camera_for((0.0, 0.0), candle_actor.get_actor_label())})

    row_labels = []
    for index in range(CANDLE_ROW_COUNT):
        offset = (index - (CANDLE_ROW_COUNT - 1) * 0.5) * CANDLE_ROW_SPACING_CM
        row_actor = spawn_candle(actors, classes["TDFluidFireActor"], fire_stage, particle_stage, candle, (SCENE_SPACING_CM + offset, 0.0, 0.0),
                                 f"SetupCandleRow{index + 1}", f"{folder}/CandleRow", created, assets_by_name)
        row_labels.append(row_actor.get_actor_label())
    scene_records.append({"scene": "candle_row", "cd_ids": ["SE.0.3.7.3"], "kind": "Candle×5", "niagara_system": None, "labels": row_labels,
                          "camera": camera_for((SCENE_SPACING_CM, 0.0), row_labels[0])})

    for index, scene in enumerate(STIRRED_SCENES):
        center = ((index + 2) * SCENE_SPACING_CM, 0.0)
        preset, preset_path, effective = stirred_presets[scene["name"]]
        actor = actors.spawn_actor_from_class(classes["TDFluidLiveActor"], unreal.Vector(center[0], center[1], 1.0), unreal.Rotator())
        particle_stage.finish_actor(actor, scene["name"], f"{folder}/{scene['name']}", created)
        particle_stage.configure_live_actor(actor, preset, effective)
        stirrer = particle_stage.place_stirrer(actors, stirrer_class, center, scene["name"] + "_Stirrer", f"{folder}/{scene['name']}", created)
        scene_records.append({"scene": scene["name"], "cd_ids": scene["cd_ids"], "kind": scene["kind"], "niagara_system": None, "preset": preset_path,
                              "labels": [actor.get_actor_label(), stirrer.get_actor_label()], "camera": camera_for(center, actor.get_actor_label())})

    print("단계: 레벨 저장")
    if not levels.save_current_level():
        raise RuntimeError(f"무대 레벨 저장 실패: {STAGE_MAP_PATH}")
    particle_stage.require_saved_files(preset_paths, STAGE_MAP_PATH)
    report = {"preset_tuning_sha256": tuning_sha256, "material_look_sha256": look_sha256, "map": STAGE_MAP_PATH, "overwrite": overwrite,
              "created_stirrer_blueprint": stirrer_created, "contracts": contracts, "blocked": blocked, "deferred": deferred,
              "smoke_pool_scenes": {"cd_ids": ["S.0.3.9", "S.3.14.7"], "covered_by": "/Game/FluidFX/Maps/LV_TDFluidSmokeStage"},
              "scenes": scene_records, "created_actors": len(created), "saved": True}
    report_path = particle_stage.write_report(report, REPORT_RELATIVE_PATH)
    print(json.dumps(report, ensure_ascii=False, default=str))
    print("보고서: " + report_path)


if __name__ == "__main__":
    main()
