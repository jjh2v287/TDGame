"""실행 환경: TDGame 언리얼 에디터 안(Python 원격 실행). FX-13r 식생 무대 LV_TDFluidFoliageStage(X축 30 m 간격 구역 4개: 0 바람만 GrassWind / 1 K1 GrassSim + 교반기 / 2 K5 GrassUnderwater + 점광 교반기 / 3 K4 PCG 대역 SM_Grass_VarB + PcgStandIn, 구역마다 ATDInstancedMeshActor 20×20 m·25 cm 격자 + 시드 고정 흔들림), 원천 라이브 1(DA_TDFluid_FoliageWalk = Smoke, 태그 TDFluidFoliageSource, 플레이어 따라가기), 흐름 액터 1을 만든다. 먼저 editor_make_fluid_foliage_material.py 와 editor_make_fluid_particle_stage.py(교반기 BP)가 끝나 있어야 하고 C++ ATDFluidFoliageFlowActor 가 빌드되어 있어야 한다.
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_foliage_stage.py [--underwater-ppv] (PowerShell, L-repo-01)
출력: /Game/FluidFX/Presets/DA_TDFluid_FoliageWalk, /Game/FluidFX/Maps/LV_TDFluidFoliageStage, Saved/FluidFX/fluid_foliage_stage.json(preset_tuning_sha256·material_look_sha256, 구역별 MI·인스턴스 수·그림자 정책·카메라 제안). 기존 에셋은 Saved/FluidFX/fluid_foliage_stage_request.json 의 {"overwrite": true} 가 있을 때만 갱신하고 미저장 작업이 있으면 중단한다.
상태: 미검증(2026-10-26 FX-13r R13 작성: py_compile·가짜 unreal(check_fluid_foliage_hlsl.py 의 validate_stage_layout)만, 에디터 실행 전). 공용 함수는 editor_make_fluid_particle_stage.py 를 importlib 로 가져온다. 프리셋은 PresetKind 만 정하고 bOverridePresetDefaults 는 끈다(골든 44 불변). 풀 WPO 끄기 거리·레이 트레이싱 WPO·그림자 무효화 정책은 무대 상수(→ TDFluidFoliageSettings, 2.7 편차). 흐름 액터는 맵당 1개를 단언한다. 2026-10-27 파동 2 에셋 단계: 에디터 실행 status ok(구역 4·인스턴스 6400×4·흐름 액터 1), stage-report stale 0, 화면 판정 전.
"""
import json
import os
import random
import sys

import unreal


FLUID_ROOT = "/Game/FluidFX"
MAP_DIR = f"{FLUID_ROOT}/Maps"
MATERIAL_DIR = f"{FLUID_ROOT}/Materials"
TEXTURE_DIR = f"{FLUID_ROOT}/Textures"
STAGE_MAP_PATH = f"{MAP_DIR}/LV_TDFluidFoliageStage"
PRESET_NAME = "FoliageWalk"
PRESET_KIND = "SMOKE"
SOURCE_TAG = "TDFluidFoliageSource"
FLUID_ACTOR_CLASS_PATH = "/Script/TDGame.TDFluidLiveActor"
FLOW_ACTOR_CLASS_PATH = "/Script/TDGame.TDFluidFoliageFlowActor"
INSTANCED_ACTOR_CLASS_PATH = "/Script/TDGame.TDInstancedMeshActor"
FLOW_RENDER_TARGET_PATH = f"{TEXTURE_DIR}/RT_TD_FluidFoliageFlow"
FLOW_COPY_MATERIAL_PATH = f"{MATERIAL_DIR}/MI_TD_FluidFoliageFlowCopy"
WINDOW_COLLECTION_PATH = f"{MATERIAL_DIR}/MPC_TD_FluidFoliageWindow"
GRASS_MESH_PATH = "/Game/DarkFantasyTopDown/StaticMeshes/Nature/SM_Grass"
GRASS_VARIANT_MESH_PATH = "/Game/DarkFantasyTopDown/StaticMeshes/Nature/SM_Grass_VarB"
PARTICLE_STAGE_RELATIVE_PATH = ("Tools", "FluidFX", "editor_make_fluid_particle_stage.py")
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_foliage_stage_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_foliage_stage.json")
ZONE_SPACING_CM = 3000.0
ZONE_SIZE_CM = 2000.0
GRID_STEP_CM = 25.0
JITTER_CM = 9.0
RANDOM_SEED = 1317
FLOOR_SIZE_CM = (12000.0, 3000.0)
WPO_DISABLE_DISTANCE_CM = 4000.0
STIRRER_ORBIT_RADIUS_CM = 150.0
UNDERWATER_STIRRER_HEIGHT_CM = 150.0
UNDERWATER_DEGREES_PER_SEC = 45.0
CAMERA_ARM_CM = 450.0
CAMERA_VIEW = (-60.0, 0.0)
ZONE_SPECS = (
    {"name": "Wind", "index": 0, "instance": "MI_TD_FluidFoliage_GrassWind", "mesh": GRASS_MESH_PATH, "stirrer": "", "shadow": "Rigid", "role": "바람만"},
    {"name": "Sim", "index": 1, "instance": "MI_TD_FluidFoliage_GrassSim", "mesh": GRASS_MESH_PATH, "stirrer": "ground", "shadow": "Rigid", "role": "K1 시뮬 구동"},
    {"name": "Underwater", "index": 2, "instance": "MI_TD_FluidFoliage_GrassUnderwater", "mesh": GRASS_MESH_PATH, "stirrer": "light", "shadow": "Rigid", "role": "K5 방사형"},
    {"name": "PcgStandIn", "index": 3, "instance": "MI_TD_FluidFoliage_PcgStandIn", "mesh": GRASS_VARIANT_MESH_PATH, "stirrer": "", "shadow": "Auto", "role": "K4 PCG 대역"},
)
SHADOW_POLICY_NAMES = {"Rigid": "RIGID", "Auto": "AUTO"}
REQUIRED_UNREAL_NAMES = (
    "EditorAssetLibrary.load_asset",
    "EditorAssetLibrary.does_asset_exist",
    "EditorActorSubsystem",
    "LevelEditorSubsystem",
    "UnrealEditorSubsystem",
    "TDFluidPresetAsset",
    "TDFluidPresetKind.SMOKE",
    "PointLightComponent",
    "ShadowCacheInvalidationBehavior.RIGID",
    "ShadowCacheInvalidationBehavior.AUTO",
    "PostProcessVolume",
)


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


def zone_center(index):
    return (index * ZONE_SPACING_CM, 0.0)


def make_grid_points(zone_index):
    generator = random.Random(RANDOM_SEED + zone_index)
    center = zone_center(zone_index)
    count = int(ZONE_SIZE_CM / GRID_STEP_CM)
    points = []
    for row in range(count):
        for column in range(count):
            x = center[0] - 0.5 * ZONE_SIZE_CM + (column + 0.5) * GRID_STEP_CM + generator.uniform(-JITTER_CM, JITTER_CM)
            y = center[1] - 0.5 * ZONE_SIZE_CM + (row + 0.5) * GRID_STEP_CM + generator.uniform(-JITTER_CM, JITTER_CM)
            points.append((x, y, generator.uniform(0.0, 360.0), generator.uniform(0.85, 1.15)))
    return points


def validate_stage_layout():
    problems = []
    centers = [zone_center(spec["index"]) for spec in ZONE_SPECS]
    for first, second in zip(centers, centers[1:]):
        if abs(second[0] - first[0]) - ZONE_SIZE_CM < 500.0:
            problems.append(f"구역 간격 부족: {first} {second}")
    floor_min_x = 0.5 * (centers[0][0] + centers[-1][0]) - 0.5 * FLOOR_SIZE_CM[0]
    floor_max_x = floor_min_x + FLOOR_SIZE_CM[0]
    for spec, center in zip(ZONE_SPECS, centers):
        if center[0] - 0.5 * ZONE_SIZE_CM < floor_min_x or center[0] + 0.5 * ZONE_SIZE_CM > floor_max_x or 0.5 * ZONE_SIZE_CM > 0.5 * FLOOR_SIZE_CM[1]:
            problems.append(f"구역이 바닥 밖: {spec['name']}")
        points = make_grid_points(spec["index"])
        if points != make_grid_points(spec["index"]):
            problems.append(f"격자 흔들림이 결정론이 아님: {spec['name']}")
        if len(points) != int(ZONE_SIZE_CM / GRID_STEP_CM) ** 2:
            problems.append(f"인스턴스 수 어긋남: {spec['name']}")
    if sum(1 for spec in ZONE_SPECS if spec["shadow"] not in SHADOW_POLICY_NAMES) != 0:
        problems.append("그림자 정책 이름 어긋남")
    if [spec["index"] for spec in ZONE_SPECS] != list(range(len(ZONE_SPECS))):
        problems.append("구역 번호 순서 어긋남")
    if not SOURCE_TAG:
        problems.append("원천 태그 없음")
    return problems


def configure_source_actor(actor, preset, effective):
    actor.set_editor_property("should_follow_player_pawn", True)
    actor.set_editor_property("should_use_activation_volume", False)
    actor.set_editor_property("accept_mouse_brush", False)
    actor.tags = [unreal.Name(SOURCE_TAG)]
    live_component = actor.get_editor_property("live_component")
    live_component.set_editor_property("active_preset", preset)
    live_component.set_editor_property("should_init_system_state_from_preset", True)
    live_component.set_editor_property("simulation_config", effective)


def configure_flow_actor(actor):
    actor.set_editor_property("source_live_actor_tag", unreal.Name(SOURCE_TAG))
    actor.set_editor_property("flow_render_target", unreal.EditorAssetLibrary.load_asset(FLOW_RENDER_TARGET_PATH))
    actor.set_editor_property("flow_copy_material", unreal.EditorAssetLibrary.load_asset(FLOW_COPY_MATERIAL_PATH))
    actor.set_editor_property("foliage_window_collection", unreal.EditorAssetLibrary.load_asset(WINDOW_COLLECTION_PATH))
    actor.set_editor_property("writes_flow", True)


def place_zone_grass(actors, instanced_class, spec, created, folder, particle_stage):
    center = zone_center(spec["index"])
    actor = actors.spawn_actor_from_class(instanced_class, unreal.Vector(center[0], center[1], 0.0), unreal.Rotator())
    particle_stage.finish_actor(actor, f"Foliage{spec['name']}_Grass", f"{folder}/{spec['name']}", created)
    component = actor.get_editor_property("instanced_mesh_component")
    component.set_static_mesh(unreal.EditorAssetLibrary.load_asset(spec["mesh"]))
    component.set_material(0, unreal.EditorAssetLibrary.load_asset(f"{MATERIAL_DIR}/{spec['instance']}"))
    component.set_editor_property("world_position_offset_disable_distance", int(WPO_DISABLE_DISTANCE_CM))
    component.set_editor_property("evaluate_world_position_offset_in_ray_tracing", False)
    component.set_editor_property("shadow_cache_invalidation_behavior", getattr(unreal.ShadowCacheInvalidationBehavior, SHADOW_POLICY_NAMES[spec["shadow"]]))
    transforms = []
    for x, y, yaw, scale in make_grid_points(spec["index"]):
        transforms.append(unreal.Transform(unreal.Vector(x - center[0], y - center[1], 0.0), unreal.Rotator(yaw=yaw), unreal.Vector(scale, scale, scale)))
    component.add_instances(transforms, False)
    return actor, len(transforms)


def place_zone_stirrer(actors, stirrer_class, spec, created, folder, particle_stage):
    center = zone_center(spec["index"])
    stirrer = particle_stage.place_stirrer(actors, stirrer_class, center, f"Foliage{spec['name']}_Stirrer", f"{folder}/{spec['name']}", created)
    if spec["stirrer"] != "light":
        return stirrer
    location = stirrer.get_actor_location()
    stirrer.set_actor_location(unreal.Vector(location.x, location.y, UNDERWATER_STIRRER_HEIGHT_CM), False, False)
    rotating = stirrer.get_component_by_class(unreal.RotatingMovementComponent)
    rotating.set_editor_property("rotation_rate", unreal.Rotator(yaw=UNDERWATER_DEGREES_PER_SEC))
    light = actors.spawn_actor_from_class(unreal.PointLight, location, unreal.Rotator())
    particle_stage.finish_actor(light, f"Foliage{spec['name']}_StirrerLight", f"{folder}/{spec['name']}", created)
    light.attach_to_actor(stirrer, unreal.Name(""), unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, False)
    return stirrer


def main():
    particle_stage = None
    module_path = project_path(*PARTICLE_STAGE_RELATIVE_PATH)
    import importlib.util
    module_spec = importlib.util.spec_from_file_location("editor_make_fluid_particle_stage", module_path)
    particle_stage = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(particle_stage)

    editor = particle_stage.require_editor_ready(REQUIRED_UNREAL_NAMES)
    layout_problems = validate_stage_layout()
    if layout_problems:
        raise RuntimeError("무대 배치 점검 실패: " + " | ".join(layout_problems))
    overwrite = particle_stage.read_overwrite_request(REQUEST_RELATIVE_PATH)
    preset_path = f"{particle_stage.PRESET_DIR}/DA_TDFluid_{PRESET_NAME}"
    particle_stage.guard_existing_assets([preset_path, STAGE_MAP_PATH], overwrite, REQUEST_RELATIVE_PATH)
    missing_assets = [path for path in (FLOW_RENDER_TARGET_PATH, FLOW_COPY_MATERIAL_PATH, WINDOW_COLLECTION_PATH, GRASS_MESH_PATH, GRASS_VARIANT_MESH_PATH, particle_stage.STIRRER_BLUEPRINT_PATH) + tuple(f"{MATERIAL_DIR}/{spec['instance']}" for spec in ZONE_SPECS) if not unreal.EditorAssetLibrary.does_asset_exist(path)]
    if missing_assets:
        raise RuntimeError("선행 에셋 없음(editor_make_fluid_foliage_material.py·editor_make_fluid_particle_stage.py 먼저): " + ", ".join(missing_assets))

    print("단계: 프리셋·모양 정본 검사·다시 읽기")
    tuning_sha256, look_sha256 = particle_stage.check_and_reload_preset_data()
    classes = {path: unreal.load_class(None, path) for path in (FLUID_ACTOR_CLASS_PATH, FLOW_ACTOR_CLASS_PATH, INSTANCED_ACTOR_CLASS_PATH)}
    missing_classes = [path for path, loaded in classes.items() if loaded is None]
    if missing_classes:
        raise RuntimeError("C++ 클래스 없음(재빌드와 에디터 재시작 필요): " + ", ".join(missing_classes))
    flow_problems = particle_stage.find_missing_properties(unreal.get_default_object(classes[FLOW_ACTOR_CLASS_PATH]), ("source_live_actor_tag", "flow_render_target", "flow_copy_material", "foliage_window_collection", "writes_flow"))
    if flow_problems:
        raise RuntimeError("흐름 액터 계약 속성 없음: " + ", ".join(flow_problems))

    print("단계: 교반기·프리셋")
    stirrer_class, stirrer_created = particle_stage.load_or_create_stirrer_blueprint()
    preset, preset_path, effective = particle_stage.configure_kind_preset(PRESET_NAME, PRESET_KIND, None)

    print("단계: 레벨")
    levels, actors = particle_stage.open_empty_level(STAGE_MAP_PATH)
    created = []
    folder = "TDGen/FluidFX/FoliageStage"
    floor_center = (0.5 * (zone_center(0)[0] + zone_center(len(ZONE_SPECS) - 1)[0]), 0.0)
    particle_stage.place_floor_and_lights(actors, created, folder, floor_center, (FLOOR_SIZE_CM[0] / 100.0, FLOOR_SIZE_CM[1] / 100.0, 0.5))
    source = actors.spawn_actor_from_class(classes[FLUID_ACTOR_CLASS_PATH], unreal.Vector(zone_center(1)[0], zone_center(1)[1], 1.0), unreal.Rotator())
    particle_stage.finish_actor(source, "FoliageSource_Fluid", folder + "/Source", created)
    configure_source_actor(source, preset, effective)
    flow = actors.spawn_actor_from_class(classes[FLOW_ACTOR_CLASS_PATH], unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator())
    particle_stage.finish_actor(flow, "FoliageFlow", folder + "/Source", created)
    configure_flow_actor(flow)

    zone_records = []
    for spec in ZONE_SPECS:
        grass, instance_count = place_zone_grass(actors, classes[INSTANCED_ACTOR_CLASS_PATH], spec, created, folder, particle_stage)
        stirrer = place_zone_stirrer(actors, stirrer_class, spec, created, folder, particle_stage) if spec["stirrer"] else None
        center = zone_center(spec["index"])
        zone_records.append({
            "zone": spec["index"], "name": spec["name"], "role": spec["role"], "material_instance": f"{MATERIAL_DIR}/{spec['instance']}",
            "mesh": spec["mesh"], "instances": instance_count, "shadow_cache_invalidation": spec["shadow"], "grass_actor": grass.get_actor_label(),
            "stirrer": stirrer.get_actor_label() if stirrer else None,
            "camera": {"pawn": [center[0], center[1]], "arm": CAMERA_ARM_CM, "view": list(CAMERA_VIEW), "console": ["TD.FluidFX.FoliageFlow 1"]},
        })
    if "--underwater-ppv" in sys.argv:
        volume = actors.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(zone_center(2)[0], 0.0, 200.0), unreal.Rotator())
        particle_stage.finish_actor(volume, "FoliageUnderwater_PPV", folder + "/Underwater", created)
        volume.set_actor_scale3d(unreal.Vector(ZONE_SIZE_CM / 200.0, ZONE_SIZE_CM / 200.0, 4.0))

    flow_actors = [actor for actor in actors.get_all_level_actors() if actor.get_class() == classes[FLOW_ACTOR_CLASS_PATH]]
    if len(flow_actors) != 1:
        raise RuntimeError(f"흐름 액터는 맵당 1개여야 함: {len(flow_actors)}")

    print("단계: 레벨 저장")
    if not levels.save_current_level():
        raise RuntimeError(f"무대 레벨 저장 실패: {STAGE_MAP_PATH}")
    particle_stage.require_saved_files([preset_path], STAGE_MAP_PATH)
    report = {
        "status": "ok", "preset_tuning_sha256": tuning_sha256, "material_look_sha256": look_sha256, "map": STAGE_MAP_PATH, "overwrite": overwrite,
        "preset": preset_path, "preset_kind": PRESET_KIND, "override_preset_defaults": False, "source_tag": SOURCE_TAG,
        "source_actor": source.get_actor_label(), "flow_actor": flow.get_actor_label(), "created_stirrer_blueprint": stirrer_created,
        "foliage_settings_constants": {"world_position_offset_disable_distance_cm": WPO_DISABLE_DISTANCE_CM, "evaluate_world_position_offset_in_ray_tracing": False},
        "underwater_ppv": "--underwater-ppv" in sys.argv, "zones": zone_records, "created_actors": len(created), "saved": True,
    }
    report_path = particle_stage.write_report(report, REPORT_RELATIVE_PATH)
    print(json.dumps(report, ensure_ascii=False, default=str))
    print("보고서: " + report_path)


if __name__ == "__main__":
    main()
