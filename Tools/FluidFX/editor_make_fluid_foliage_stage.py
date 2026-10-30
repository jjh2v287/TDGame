"""실행 환경: TDGame 언리얼 에디터 안(Python 원격 실행). FX-13r·FX-13c 식생 무대 LV_TDFluidFoliageStage(X축 30 m 간격 구역 6개: 0 바람만 GrassWind / 1 K1 GrassSim + 교반기 / 2 K5 GrassUnderwater + 점광 교반기 / 3 K4 PCG 대역 SM_Grass_VarB + PcgStandIn / 4 K2 눌림 유지 GrassPress + 반경 600 cm 궤도 교반기 / 5 K3 물가 갈대 Reed + 수면 +40 cm 물 영역 + 물속 궤도 교반기, 구역마다 ATDInstancedMeshActor 20×20 m·25 cm 격자 + 시드 고정 흔들림(구역 5는 30 cm 격자 띠 20×4 m), 바닥 180×30 m), 원천 라이브 3(DA_TDFluid_FoliageWalk = FoliageWalk 태그 TDFluidFoliageSource / DA_TDFluid_FoliagePress = FoliagePainter 태그 TDFluidFoliagePressSource / DA_TDFluid_ReedShallows = ReedShallows 물 액터 태그 TDFluidReedSource, 모두 플레이어 따라가기), 흐름 액터 3(RT마다 1개)을 만든다. 먼저 editor_make_fluid_foliage_material.py(targets copy·press·reed)와 editor_make_fluid_particle_stage.py(교반기 BP)·editor_make_fluid_water_material.py(물 MI)가 끝나 있어야 하고 C++ ATDFluidFoliageFlowActor·ATDFluidWaterActor 와 새 종류 3(FoliageWalk·FoliagePainter·ReedShallows)이 빌드되어 있어야 한다.
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_foliage_stage.py [--underwater-ppv] (PowerShell, L-repo-01)
출력: /Game/FluidFX/Presets/DA_TDFluid_FoliageWalk·DA_TDFluid_FoliagePress·DA_TDFluid_ReedShallows, /Game/FluidFX/Maps/LV_TDFluidFoliageStage, Saved/FluidFX/fluid_foliage_stage.json(preset_tuning_sha256·material_look_sha256, 구역별 MI·인스턴스 수·그림자 정책·카메라 제안, 채널·원천·흐름 액터·물 영역 기록). 기존 에셋은 Saved/FluidFX/fluid_foliage_stage_request.json 의 {"overwrite": true} 가 있을 때만 갱신하고 미저장 작업이 있으면 중단한다. 성공 여부는 run_in_editor.py 출력 본문(보고 JSON 줄과 마지막 보고서 줄)으로 판정한다.
상태: 미검증(2026-10-29 파동 3 FX-13c 구역 4·5 추가: py_compile·가짜 unreal(check_fluid_foliage_hlsl.py 의 validate_stage_layout)만, 에디터 실행 전). 이전 상태: 2026-10-27 파동 2 에셋 단계 에디터 실행 status ok(구역 4·인스턴스 6400×4·흐름 액터 1). 공용 함수는 editor_make_fluid_particle_stage.py 를 importlib 로 가져온다. 프리셋은 PresetKind 만 정하고 bOverridePresetDefaults 는 끈다(골든 44 불변). 시작 점검(새 종류 Python 이름·휨 켬·물 층 켬·선행 에셋)이 실패하면 에셋을 바꾸기 전에 멈춘다. 갈대 메시는 SM_GrassTall_01A 가 없으면 SM_Grass Z 3배로 대신한다. 구역 4 교반기는 직선 왕복이 아니라 기존 궤도 회전의 반경만 키운다(S3). 풀 WPO 끄기 거리·레이 트레이싱 WPO·그림자 무효화 정책은 무대 상수(→ TDFluidFoliageSettings, 2.7 편차). 2026-10-31: 궤도·물속 교반기 회전 속도가 피벗 쓰기 뒤 BP 구성 스크립트 재실행으로 유실되던(기본 90°/s) 것을 particle_stage.set_component_properties(한 번에 쓰기·새 핸들로 다시 읽기, 다르면 FAIL)로 고침, 맵 재생성 필요.
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
MESH_DIR = f"{FLUID_ROOT}/Meshes"
STAGE_MAP_PATH = f"{MAP_DIR}/LV_TDFluidFoliageStage"
FLUID_ACTOR_CLASS_PATH = "/Script/TDGame.TDFluidLiveActor"
WATER_ACTOR_CLASS_PATH = "/Script/TDGame.TDFluidWaterActor"
FLOW_ACTOR_CLASS_PATH = "/Script/TDGame.TDFluidFoliageFlowActor"
INSTANCED_ACTOR_CLASS_PATH = "/Script/TDGame.TDInstancedMeshActor"
GRASS_MESH_PATH = "/Game/DarkFantasyTopDown/StaticMeshes/Nature/SM_Grass"
GRASS_VARIANT_MESH_PATH = "/Game/DarkFantasyTopDown/StaticMeshes/Nature/SM_Grass_VarB"
REED_MESH_PATH = "/Game/UnrealNormandy/StaticMeshes/SM_GrassTall_01A"
REED_FALLBACK_SCALE_Z = 3.0
WATER_MATERIAL_PATH = f"{MATERIAL_DIR}/MI_TD_FluidWater_Lake"
WATER_SURFACE_MESH_PATH = f"{MESH_DIR}/SM_TD_FluidWaterGridUnit"
WATER_REGION_MESH_PATH = "/Engine/BasicShapes/Plane"
WATER_REGION_TAG = "TDFluidWaterRegion"
WATER_SURFACE_Z_CM = 40.0
PARTICLE_STAGE_RELATIVE_PATH = ("Tools", "FluidFX", "editor_make_fluid_particle_stage.py")
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_foliage_stage_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_foliage_stage.json")
ZONE_SPACING_CM = 3000.0
ZONE_SIZE_CM = 2000.0
GRID_STEP_CM = 25.0
JITTER_CM = 9.0
RANDOM_SEED = 1317
FLOOR_SIZE_CM = (18000.0, 3000.0)
WPO_DISABLE_DISTANCE_CM = 4000.0
STIRRER_ORBIT_RADIUS_CM = 150.0
STIRRER_HEIGHT_CM = 20.0
STIRRER_DEGREES_PER_SEC = 90.0
UNDERWATER_STIRRER_HEIGHT_CM = 150.0
UNDERWATER_DEGREES_PER_SEC = 45.0
PRESS_ORBIT_RADIUS_CM = 600.0
PRESS_DEGREES_PER_SEC = 20.0
REED_ORBIT_RADIUS_CM = 500.0
REED_STIRRER_HEIGHT_CM = 20.0
REED_DEGREES_PER_SEC = 25.0
REED_GRID_STEP_CM = 30.0
REED_BAND_SIZE_CM = (2000.0, 400.0)
FOLIAGE_PAINTER_EXTENT_CM = 2560.0
CAMERA_ARM_CM = 450.0
CAMERA_VIEW = (-60.0, 0.0)
CHANNEL_SPECS = (
    {"name": "Walk", "source_tag": "TDFluidFoliageSource", "render_target": "RT_TD_FluidFoliageFlow", "copy_instance": "MI_TD_FluidFoliageFlowCopy", "collection": "MPC_TD_FluidFoliageWindow"},
    {"name": "Press", "source_tag": "TDFluidFoliagePressSource", "render_target": "RT_TD_FluidFoliagePressFlow", "copy_instance": "MI_TD_FluidFoliageFlowCopy_Press", "collection": "MPC_TD_FluidFoliagePressWindow"},
    {"name": "Reed", "source_tag": "TDFluidReedSource", "render_target": "RT_TD_FluidReedFlow", "copy_instance": "MI_TD_FluidFoliageFlowCopy_Reed", "collection": "MPC_TD_FluidReedWindow"},
)
SOURCE_SPECS = (
    {"name": "Walk", "channel": "Walk", "zone": 1, "preset": "FoliageWalk", "kind": "FOLIAGE_WALK", "label": "FoliageSource_Walk", "is_water": False},
    {"name": "Press", "channel": "Press", "zone": 4, "preset": "FoliagePress", "kind": "FOLIAGE_PAINTER", "label": "FoliageSource_Press", "is_water": False},
    {"name": "Reed", "channel": "Reed", "zone": 5, "preset": "ReedShallows", "kind": "REED_SHALLOWS", "label": "FoliageSource_Reed", "is_water": True},
)
ZONE_DEFAULTS = {
    "channel": "Walk",
    "grid_step_cm": GRID_STEP_CM,
    "band_cm": (ZONE_SIZE_CM, ZONE_SIZE_CM),
    "scale_z": 1.0,
    "collision": True,
    "orbit_radius_cm": STIRRER_ORBIT_RADIUS_CM,
    "stirrer_height_cm": STIRRER_HEIGHT_CM,
    "degrees_per_sec": STIRRER_DEGREES_PER_SEC,
    "uses_reed_mesh": False,
    "console": (),
}
ZONE_SPECS = (
    {"name": "Wind", "index": 0, "instance": "MI_TD_FluidFoliage_GrassWind", "mesh": GRASS_MESH_PATH, "stirrer": "", "shadow": "Rigid", "role": "바람만"},
    {"name": "Sim", "index": 1, "instance": "MI_TD_FluidFoliage_GrassSim", "mesh": GRASS_MESH_PATH, "stirrer": "ground", "shadow": "Rigid", "role": "K1 시뮬 구동"},
    {"name": "Underwater", "index": 2, "instance": "MI_TD_FluidFoliage_GrassUnderwater", "mesh": GRASS_MESH_PATH, "stirrer": "light", "shadow": "Rigid", "role": "K5 방사형"},
    {"name": "PcgStandIn", "index": 3, "instance": "MI_TD_FluidFoliage_PcgStandIn", "mesh": GRASS_VARIANT_MESH_PATH, "stirrer": "", "shadow": "Auto", "role": "K4 PCG 대역"},
    {
        "name": "Press", "index": 4, "instance": "MI_TD_FluidFoliage_GrassPress", "mesh": GRASS_MESH_PATH, "stirrer": "orbit", "shadow": "Rigid", "role": "K2 눌림 유지",
        "channel": "Press", "orbit_radius_cm": PRESS_ORBIT_RADIUS_CM, "degrees_per_sec": PRESS_DEGREES_PER_SEC, "console": ("TD.FluidFX.FoliageBend -1",),
    },
    {
        "name": "Reed", "index": 5, "instance": "MI_TD_FluidFoliage_Reed", "mesh": REED_MESH_PATH, "stirrer": "water", "shadow": "Rigid", "role": "K3 물가 갈대",
        "channel": "Reed", "grid_step_cm": REED_GRID_STEP_CM, "band_cm": REED_BAND_SIZE_CM, "collision": False, "orbit_radius_cm": REED_ORBIT_RADIUS_CM,
        "stirrer_height_cm": REED_STIRRER_HEIGHT_CM, "degrees_per_sec": REED_DEGREES_PER_SEC, "uses_reed_mesh": True, "console": ("TD.FluidFX.FoliageBend -1",),
    },
)
SHADOW_POLICY_NAMES = {"Rigid": "RIGID", "Auto": "AUTO"}
STIRRER_KINDS = ("", "ground", "light", "orbit", "water")
REQUIRED_UNREAL_NAMES = (
    "EditorAssetLibrary.load_asset",
    "EditorAssetLibrary.does_asset_exist",
    "EditorActorSubsystem",
    "LevelEditorSubsystem",
    "UnrealEditorSubsystem",
    "TDFluidPresetAsset",
    "TDFluidPresetKind.FOLIAGE_WALK",
    "TDFluidPresetKind.FOLIAGE_PAINTER",
    "TDFluidPresetKind.REED_SHALLOWS",
    "TDFluidBrushChannel.CHANNEL_A_WATER",
    "TDFluidPlaneSettings",
    "TDFluidPlaneOrientation.WORLD_FACING",
    "RotatingMovementComponent",
    "CollisionEnabled.NO_COLLISION",
    "PointLightComponent",
    "ShadowCacheInvalidationBehavior.RIGID",
    "ShadowCacheInvalidationBehavior.AUTO",
    "PostProcessVolume",
)


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


def zone_center(index):
    return (index * ZONE_SPACING_CM, 0.0)


def zone_value(spec, key):
    return spec.get(key, ZONE_DEFAULTS[key])


def find_channel(name):
    return next((channel for channel in CHANNEL_SPECS if channel["name"] == name), None)


def render_target_path(channel):
    return f"{TEXTURE_DIR}/{channel['render_target']}"


def copy_material_path(channel):
    return f"{MATERIAL_DIR}/{channel['copy_instance']}"


def collection_path(channel):
    return f"{MATERIAL_DIR}/{channel['collection']}"


def preset_asset_path(particle_stage, source_spec):
    return f"{particle_stage.PRESET_DIR}/DA_TDFluid_{source_spec['preset']}"


def make_grid_points(zone_index):
    spec = next(item for item in ZONE_SPECS if item["index"] == zone_index)
    generator = random.Random(RANDOM_SEED + zone_index)
    center = zone_center(zone_index)
    step = zone_value(spec, "grid_step_cm")
    band = zone_value(spec, "band_cm")
    column_count = int(band[0] / step)
    row_count = int(band[1] / step)
    points = []
    for row in range(row_count):
        for column in range(column_count):
            x = center[0] - 0.5 * band[0] + (column + 0.5) * step + generator.uniform(-JITTER_CM, JITTER_CM)
            y = center[1] - 0.5 * band[1] + (row + 0.5) * step + generator.uniform(-JITTER_CM, JITTER_CM)
            points.append((x, y, generator.uniform(0.0, 360.0), generator.uniform(0.85, 1.15)))
    return points


def validate_zone_layout(problems):
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
        band = zone_value(spec, "band_cm")
        step = zone_value(spec, "grid_step_cm")
        if len(points) != int(band[0] / step) * int(band[1] / step):
            problems.append(f"인스턴스 수 어긋남: {spec['name']}")
        if band[0] > ZONE_SIZE_CM or band[1] > ZONE_SIZE_CM:
            problems.append(f"풀 띠가 구역 밖: {spec['name']}")
        if spec["stirrer"] not in STIRRER_KINDS:
            problems.append(f"교반기 종류 이름 어긋남: {spec['name']}")
        if spec["stirrer"] in ("orbit", "water") and zone_value(spec, "orbit_radius_cm") > 0.5 * ZONE_SIZE_CM - 100.0:
            problems.append(f"교반기 궤도가 구역 밖: {spec['name']}")
        if find_channel(zone_value(spec, "channel")) is None:
            problems.append(f"구역 채널 없음: {spec['name']}")
    if sum(1 for spec in ZONE_SPECS if spec["shadow"] not in SHADOW_POLICY_NAMES) != 0:
        problems.append("그림자 정책 이름 어긋남")
    if [spec["index"] for spec in ZONE_SPECS] != list(range(len(ZONE_SPECS))):
        problems.append("구역 번호 순서 어긋남")
    if sum(1 for spec in ZONE_SPECS if spec.get("uses_reed_mesh")) != 1:
        problems.append("갈대 메시 구역은 정확히 1개여야 함")


def validate_channel_layout(problems):
    for key in ("source_tag", "render_target", "copy_instance", "collection"):
        values = [channel[key] for channel in CHANNEL_SPECS]
        if not all(values):
            problems.append(f"채널 {key} 비어 있음")
        if len(set(values)) != len(values):
            problems.append(f"채널 {key} 중복(RT마다 흐름 액터 1개·원천 태그 1개)")
    for source in SOURCE_SPECS:
        channel = find_channel(source["channel"])
        zone = next((spec for spec in ZONE_SPECS if spec["index"] == source["zone"]), None)
        if channel is None or zone is None:
            problems.append(f"원천 채널·구역 없음: {source['name']}")
            continue
        if zone_value(zone, "channel") != channel["name"]:
            problems.append(f"원천 구역 채널 어긋남: {source['name']}")
    if sorted(source["channel"] for source in SOURCE_SPECS) != sorted(channel["name"] for channel in CHANNEL_SPECS):
        problems.append("채널마다 원천 1개여야 함")
    water_sources = [source for source in SOURCE_SPECS if source["is_water"]]
    if len(water_sources) != 1:
        problems.append("물 원천은 정확히 1개여야 함")
    reed_zone = next((spec for spec in ZONE_SPECS if spec["stirrer"] == "water"), None)
    if reed_zone is None or WATER_SURFACE_Z_CM <= zone_value(reed_zone, "stirrer_height_cm"):
        problems.append("물속 교반기는 수면보다 낮아야 함")
    press_zone = next((spec for spec in ZONE_SPECS if spec["stirrer"] == "orbit"), None)
    if press_zone is None or zone_value(press_zone, "orbit_radius_cm") >= 0.5 * FOLIAGE_PAINTER_EXTENT_CM:
        problems.append("눌림 교반기 궤도가 페인터 창 밖")


def validate_stage_layout():
    problems = []
    validate_zone_layout(problems)
    validate_channel_layout(problems)
    return problems


def resolve_reed_mesh():
    if unreal.EditorAssetLibrary.does_asset_exist(REED_MESH_PATH):
        return REED_MESH_PATH, 1.0
    return GRASS_MESH_PATH, REED_FALLBACK_SCALE_Z


def resolve_zone_specs():
    reed_mesh_path, reed_scale_z = resolve_reed_mesh()
    return tuple(dict(spec, mesh=reed_mesh_path, scale_z=reed_scale_z) if spec.get("uses_reed_mesh") else spec for spec in ZONE_SPECS)


def find_missing_stage_assets(zone_specs, particle_stage):
    required = [GRASS_MESH_PATH, GRASS_VARIANT_MESH_PATH, particle_stage.STIRRER_BLUEPRINT_PATH, WATER_MATERIAL_PATH, WATER_SURFACE_MESH_PATH, WATER_REGION_MESH_PATH]
    for channel in CHANNEL_SPECS:
        required += [render_target_path(channel), copy_material_path(channel), collection_path(channel)]
    required += [f"{MATERIAL_DIR}/{spec['instance']}" for spec in zone_specs]
    required += [spec["mesh"] for spec in zone_specs]
    return [path for path in required if not unreal.EditorAssetLibrary.does_asset_exist(path)]


def require_kind_defaults(particle_stage):
    problems = []
    for source in SOURCE_SPECS:
        config = unreal.TDFluidPresetAsset.build_config_for_preset_kind(getattr(unreal.TDFluidPresetKind, source["kind"]))
        if not particle_stage.has_editor_property(config, "enable_bend_vector_channel") or not config.get_editor_property("enable_bend_vector_channel"):
            problems.append(f"{source['kind']}: enable_bend_vector_channel 꺼짐 또는 없음(FX-13c 빌드·JSON 확인)")
        if source["is_water"] and not config.get_editor_property("enable_water_layer"):
            problems.append(f"{source['kind']}: enable_water_layer 꺼짐(WaterLayer 그룹 가입 확인)")
    if problems:
        raise RuntimeError("새 종류 기본값 점검 실패: " + " | ".join(problems))


def configure_source_actor(actor, preset, effective, channel, source):
    actor.set_editor_property("should_follow_player_pawn", True)
    actor.set_editor_property("should_use_activation_volume", False)
    actor.set_editor_property("accept_mouse_brush", False)
    actor.tags = [unreal.Name(channel["source_tag"])]
    live_component = actor.get_editor_property("live_component")
    live_component.set_editor_property("active_preset", preset)
    live_component.set_editor_property("should_init_system_state_from_preset", True)
    live_component.set_editor_property("simulation_config", effective)
    if not source["is_water"]:
        return
    actor.set_editor_property("sample_terrain_elevation", True)
    actor.set_editor_property("interaction_brush_channel", unreal.TDFluidBrushChannel.CHANNEL_A_WATER)
    internal_surface = actor.get_editor_property("internal_surface_mesh")
    if internal_surface is not None:
        internal_surface.set_cast_shadow(False)


def configure_flow_actor(actor, channel):
    actor.set_editor_property("source_live_actor_tag", unreal.Name(channel["source_tag"]))
    actor.set_editor_property("flow_render_target", unreal.EditorAssetLibrary.load_asset(render_target_path(channel)))
    actor.set_editor_property("flow_copy_material", unreal.EditorAssetLibrary.load_asset(copy_material_path(channel)))
    actor.set_editor_property("foliage_window_collection", unreal.EditorAssetLibrary.load_asset(collection_path(channel)))
    actor.set_editor_property("writes_flow", True)


def configure_reed_preset(particle_stage, source, water_material, surface_mesh):
    kind = getattr(unreal.TDFluidPresetKind, source["kind"])
    preset, path = particle_stage.load_or_create_preset(source["preset"])
    preset.set_editor_property("preset_kind", kind)
    preset.set_editor_property("override_preset_defaults", False)
    preset.set_editor_property("simulation_config", unreal.TDFluidPresetAsset.build_config_for_preset_kind(kind))
    preset.set_editor_property("default_channel", unreal.TDFluidBrushChannel.CHANNEL_A_WATER)
    plane = unreal.TDFluidPlaneSettings()
    plane.set_editor_property("orientation", unreal.TDFluidPlaneOrientation.WORLD_FACING)
    plane.set_editor_property("camera_depth_bias_cm", 0.0)
    plane.set_editor_property("center_offset_along_up_ratio", 0.0)
    preset.set_editor_property("plane_settings", plane)
    preset.set_editor_property("output_material", water_material)
    preset.set_editor_property("surface_mesh", surface_mesh)
    preset.set_editor_property("translucency_sort_priority", 0)
    effective = preset.resolve_effective_config()
    if not effective.get_editor_property("enable_water_layer"):
        raise RuntimeError(f"갈대 얕은 물 프리셋 유효 구성에 물 층이 꺼져 있음: {path}")
    particle_stage.save_asset(preset, path)
    return preset, path, effective


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
    if not zone_value(spec, "collision"):
        component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    scale_z = zone_value(spec, "scale_z")
    transforms = []
    for x, y, yaw, scale in make_grid_points(spec["index"]):
        transforms.append(unreal.Transform(unreal.Vector(x - center[0], y - center[1], 0.0), unreal.Rotator(yaw=yaw), unreal.Vector(scale, scale, scale * scale_z)))
    component.add_instances(transforms, False)
    return actor, len(transforms)


def place_orbit_stirrer(actors, stirrer_class, spec, created, folder, particle_stage):
    center = zone_center(spec["index"])
    radius = zone_value(spec, "orbit_radius_cm")
    stirrer = actors.spawn_actor_from_class(stirrer_class, unreal.Vector(center[0] + radius, center[1], zone_value(spec, "stirrer_height_cm")), unreal.Rotator())
    particle_stage.finish_actor(stirrer, f"Foliage{spec['name']}_Stirrer", f"{folder}/{spec['name']}", created)
    motion = {"pivot_translation": unreal.Vector(-radius, 0.0, 0.0), "rotation_rate": unreal.Rotator(yaw=zone_value(spec, "degrees_per_sec"))}
    particle_stage.set_component_properties(stirrer, unreal.RotatingMovementComponent, motion)
    return stirrer


def place_zone_stirrer(actors, stirrer_class, spec, created, folder, particle_stage):
    if spec["stirrer"] in ("orbit", "water"):
        return place_orbit_stirrer(actors, stirrer_class, spec, created, folder, particle_stage)
    center = zone_center(spec["index"])
    stirrer = particle_stage.place_stirrer(actors, stirrer_class, center, f"Foliage{spec['name']}_Stirrer", f"{folder}/{spec['name']}", created)
    if spec["stirrer"] != "light":
        return stirrer
    location = stirrer.get_actor_location()
    stirrer.set_actor_location(unreal.Vector(location.x, location.y, UNDERWATER_STIRRER_HEIGHT_CM), False, False)
    particle_stage.set_component_properties(stirrer, unreal.RotatingMovementComponent, {"rotation_rate": unreal.Rotator(yaw=UNDERWATER_DEGREES_PER_SEC)})
    particle_stage.require_component_properties(stirrer, unreal.RotatingMovementComponent, {"pivot_translation": unreal.Vector(-particle_stage.STIRRER_ORBIT_RADIUS_CM, 0.0, 0.0)})
    light = actors.spawn_actor_from_class(unreal.PointLight, location, unreal.Rotator())
    particle_stage.finish_actor(light, f"Foliage{spec['name']}_StirrerLight", f"{folder}/{spec['name']}", created)
    light.attach_to_actor(stirrer, unreal.Name(""), unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, False)
    return stirrer


def place_water_region(actors, spec, created, folder, particle_stage):
    center = zone_center(spec["index"])
    plane_mesh = unreal.EditorAssetLibrary.load_asset(WATER_REGION_MESH_PATH)
    region = actors.spawn_actor_from_object(plane_mesh, unreal.Vector(center[0], center[1], WATER_SURFACE_Z_CM), unreal.Rotator())
    particle_stage.finish_actor(region, f"Foliage{spec['name']}_WaterRegion", f"{folder}/{spec['name']}", created)
    region.set_actor_scale3d(unreal.Vector(ZONE_SIZE_CM / 100.0, ZONE_SIZE_CM / 100.0, 1.0))
    component = region.static_mesh_component
    component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    component.set_cast_shadow(False)
    component.set_visibility(False)
    region.tags = [unreal.Name(WATER_REGION_TAG)]
    origin, extent = region.get_actor_bounds(False)
    surface_z = origin.z + extent.z
    half_size = 0.5 * ZONE_SIZE_CM
    if abs(surface_z - WATER_SURFACE_Z_CM) > 1.0 or abs(extent.x - half_size) > 1.0 or abs(extent.y - half_size) > 1.0:
        raise RuntimeError(f"물 영역 상자 검증 실패: 수면 {surface_z} 반폭 ({extent.x}, {extent.y})")
    return {"actor": region.get_actor_label(), "tag": WATER_REGION_TAG, "surface_z_cm": surface_z, "half_extent_cm": [extent.x, extent.y]}


def count_actors_with_tag(actors, tag_name):
    return sum(1 for actor in actors.get_all_level_actors() if tag_name in [str(tag) for tag in actor.tags])


def require_one_flow_actor_per_render_target(actors, flow_class):
    counts = {}
    for actor in actors.get_all_level_actors():
        if actor.get_class() != flow_class:
            continue
        target = actor.get_editor_property("flow_render_target")
        key = target.get_path_name().split(".")[0] if target is not None else "None"
        counts[key] = counts.get(key, 0) + 1
    expected = {render_target_path(channel): 1 for channel in CHANNEL_SPECS}
    if counts != expected:
        raise RuntimeError(f"흐름 액터는 RT마다 1개여야 함: 실제 {counts} 기대 {expected}")
    return counts


def require_one_source_per_tag(actors):
    counts = {channel["source_tag"]: count_actors_with_tag(actors, channel["source_tag"]) for channel in CHANNEL_SPECS}
    if any(count != 1 for count in counts.values()):
        raise RuntimeError(f"원천 태그마다 액터 1개여야 함: {counts}")
    return counts


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
    preset_paths = [preset_asset_path(particle_stage, source) for source in SOURCE_SPECS]
    particle_stage.guard_existing_assets(preset_paths + [STAGE_MAP_PATH], overwrite, REQUEST_RELATIVE_PATH)
    zone_specs = resolve_zone_specs()
    missing_assets = find_missing_stage_assets(zone_specs, particle_stage)
    if missing_assets:
        raise RuntimeError("선행 에셋 없음(editor_make_fluid_foliage_material.py targets copy·press·reed·editor_make_fluid_water_material.py·editor_make_fluid_particle_stage.py 먼저): " + ", ".join(missing_assets))

    print("단계: 프리셋·모양 정본 검사·다시 읽기")
    tuning_sha256, look_sha256 = particle_stage.check_and_reload_preset_data()
    require_kind_defaults(particle_stage)
    class_paths = (FLUID_ACTOR_CLASS_PATH, WATER_ACTOR_CLASS_PATH, FLOW_ACTOR_CLASS_PATH, INSTANCED_ACTOR_CLASS_PATH)
    classes = {path: unreal.load_class(None, path) for path in class_paths}
    missing_classes = [path for path, loaded in classes.items() if loaded is None]
    if missing_classes:
        raise RuntimeError("C++ 클래스 없음(재빌드와 에디터 재시작 필요): " + ", ".join(missing_classes))
    flow_problems = particle_stage.find_missing_properties(unreal.get_default_object(classes[FLOW_ACTOR_CLASS_PATH]), ("source_live_actor_tag", "flow_render_target", "flow_copy_material", "foliage_window_collection", "writes_flow"))
    if flow_problems:
        raise RuntimeError("흐름 액터 계약 속성 없음: " + ", ".join(flow_problems))
    water_problems = particle_stage.find_missing_properties(unreal.get_default_object(classes[WATER_ACTOR_CLASS_PATH]), ("sample_terrain_elevation", "interaction_brush_channel", "internal_surface_mesh", "water_region_actor_tag"))
    if water_problems:
        raise RuntimeError("물 액터 계약 속성 없음: " + ", ".join(water_problems))

    print("단계: 교반기·프리셋")
    stirrer_class, stirrer_created = particle_stage.load_or_create_stirrer_blueprint()
    presets = {}
    for source in SOURCE_SPECS:
        if source["is_water"]:
            water_material = unreal.EditorAssetLibrary.load_asset(WATER_MATERIAL_PATH)
            surface_mesh = unreal.EditorAssetLibrary.load_asset(WATER_SURFACE_MESH_PATH)
            presets[source["name"]] = configure_reed_preset(particle_stage, source, water_material, surface_mesh)
            continue
        presets[source["name"]] = particle_stage.configure_kind_preset(source["preset"], source["kind"], None)

    print("단계: 레벨")
    levels, actors = particle_stage.open_empty_level(STAGE_MAP_PATH)
    created = []
    folder = "TDGen/FluidFX/FoliageStage"
    floor_center = (0.5 * (zone_center(0)[0] + zone_center(len(zone_specs) - 1)[0]), 0.0)
    particle_stage.place_floor_and_lights(actors, created, folder, floor_center, (FLOOR_SIZE_CM[0] / 100.0, FLOOR_SIZE_CM[1] / 100.0, 0.5))

    source_records = []
    flow_records = []
    sources = {}
    for source in SOURCE_SPECS:
        channel = find_channel(source["channel"])
        preset, preset_path, effective = presets[source["name"]]
        center = zone_center(source["zone"])
        height = WATER_SURFACE_Z_CM if source["is_water"] else 1.0
        actor_class = classes[WATER_ACTOR_CLASS_PATH] if source["is_water"] else classes[FLUID_ACTOR_CLASS_PATH]
        source_actor = actors.spawn_actor_from_class(actor_class, unreal.Vector(center[0], center[1], height), unreal.Rotator())
        particle_stage.finish_actor(source_actor, source["label"], folder + "/Source", created)
        configure_source_actor(source_actor, preset, effective, channel, source)
        flow_actor = actors.spawn_actor_from_class(classes[FLOW_ACTOR_CLASS_PATH], unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator())
        particle_stage.finish_actor(flow_actor, f"FoliageFlow_{channel['name']}", folder + "/Source", created)
        configure_flow_actor(flow_actor, channel)
        sources[source["name"]] = source_actor
        source_records.append({"name": source["name"], "channel": channel["name"], "zone": source["zone"], "preset": preset_path, "preset_kind": source["kind"], "source_tag": channel["source_tag"], "actor": source_actor.get_actor_label(), "is_water": source["is_water"]})
        flow_records.append({"channel": channel["name"], "actor": flow_actor.get_actor_label(), "render_target": render_target_path(channel), "copy_material": copy_material_path(channel), "collection": collection_path(channel)})

    zone_records = []
    water_region_record = None
    for spec in zone_specs:
        grass, instance_count = place_zone_grass(actors, classes[INSTANCED_ACTOR_CLASS_PATH], spec, created, folder, particle_stage)
        stirrer = place_zone_stirrer(actors, stirrer_class, spec, created, folder, particle_stage) if spec["stirrer"] else None
        if spec["stirrer"] == "water":
            water_region_record = place_water_region(actors, spec, created, folder, particle_stage)
        center = zone_center(spec["index"])
        zone_records.append({
            "zone": spec["index"], "name": spec["name"], "role": spec["role"], "channel": zone_value(spec, "channel"), "material_instance": f"{MATERIAL_DIR}/{spec['instance']}",
            "mesh": spec["mesh"], "mesh_scale_z": zone_value(spec, "scale_z"), "grid_step_cm": zone_value(spec, "grid_step_cm"), "instances": instance_count,
            "shadow_cache_invalidation": spec["shadow"], "grass_actor": grass.get_actor_label(), "stirrer": stirrer.get_actor_label() if stirrer else None,
            "camera": {"pawn": [center[0], center[1]], "arm": CAMERA_ARM_CM, "view": list(CAMERA_VIEW), "console": ["TD.FluidFX.FoliageFlow 1"] + list(zone_value(spec, "console"))},
        })
    if "--underwater-ppv" in sys.argv:
        volume = actors.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(zone_center(2)[0], 0.0, 200.0), unreal.Rotator())
        particle_stage.finish_actor(volume, "FoliageUnderwater_PPV", folder + "/Underwater", created)
        volume.set_actor_scale3d(unreal.Vector(ZONE_SIZE_CM / 200.0, ZONE_SIZE_CM / 200.0, 4.0))

    flow_counts = require_one_flow_actor_per_render_target(actors, classes[FLOW_ACTOR_CLASS_PATH])
    source_counts = require_one_source_per_tag(actors)
    if water_region_record is None:
        raise RuntimeError("물 영역 태그 액터가 만들어지지 않음")

    print("단계: 레벨 저장")
    if not levels.save_current_level():
        raise RuntimeError(f"무대 레벨 저장 실패: {STAGE_MAP_PATH}")
    particle_stage.require_saved_files(preset_paths, STAGE_MAP_PATH)
    walk_channel = find_channel("Walk")
    reed_mesh_path, reed_scale_z = resolve_reed_mesh()
    report = {
        "status": "ok", "preset_tuning_sha256": tuning_sha256, "material_look_sha256": look_sha256, "map": STAGE_MAP_PATH, "overwrite": overwrite,
        "preset": preset_paths[0], "preset_kind": SOURCE_SPECS[0]["kind"], "override_preset_defaults": False, "source_tag": walk_channel["source_tag"],
        "source_actor": sources["Walk"].get_actor_label(), "flow_actor": flow_records[0]["actor"], "created_stirrer_blueprint": stirrer_created,
        "foliage_settings_constants": {"world_position_offset_disable_distance_cm": WPO_DISABLE_DISTANCE_CM, "evaluate_world_position_offset_in_ray_tracing": False},
        "sources": source_records, "flow_actors": flow_records, "flow_render_target_counts": flow_counts, "source_tag_counts": source_counts,
        "water": {"material": WATER_MATERIAL_PATH, "surface_mesh": WATER_SURFACE_MESH_PATH, "region": water_region_record},
        "reed_mesh": {"path": reed_mesh_path, "scale_z": reed_scale_z, "is_fallback": reed_mesh_path != REED_MESH_PATH},
        "underwater_ppv": "--underwater-ppv" in sys.argv, "zones": zone_records, "created_actors": len(created), "saved": True,
    }
    report_path = particle_stage.write_report(report, REPORT_RELATIVE_PATH)
    print(json.dumps(report, ensure_ascii=False, default=str))
    print("보고서: " + report_path)


if __name__ == "__main__":
    main()
