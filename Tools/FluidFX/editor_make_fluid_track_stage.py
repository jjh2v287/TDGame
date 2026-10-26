"""실행 환경: TDGame 언리얼 에디터 안에서 FluidFX 자국 무대 2개(장면 0 LV_TDFluidSnowStage = 평지 60 m + 15° 오르막 20 m, 장면 1 LV_TDFluidSandStage = 평지 100 m 사구 WPO)와 DA 4개(FootprintPainter 대리 SnowTrack·SandTrack, ExplosionGroundDust 대리 SnowDust·SandDust)를 만든다. 먼저 editor_make_fluid_track_material.py 와 FX-8n 무대 스크립트(교반기 BP_TDFluidStageStirrer)가 성공해 있어야 하고, 모양 정본 병합 뒤에 돌린다.
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_track_stage.py (기존 DA·맵을 갱신할 때는 Saved/FluidFX/fluid_track_stage_request.json 에 {"overwrite": true})
출력: /Game/FluidFX/Presets/DA_TDFluid_SnowTrack·SandTrack·SnowDust·SandDust, /Game/FluidFX/Maps/LV_TDFluidSnowStage·LV_TDFluidSandStage, Saved/FluidFX/fluid_track_stage.json(preset_tuning_sha256·material_look_sha256, 장면마다 바닥 위 1 m 에서 쏜 라인 트레이스 적중 Z)
상태: 미검증(2026-10-25 FX-12r 작성: py_compile·가짜 unreal·validate_stage_layout 만, 에디터 실행 전). 보이는 바닥은 격자 메시 SM_TD_FluidTrackGridUnit256(충돌 없음)이고 같은 Z 에 숨긴 큐브 충돌 바닥을 둔다(반박 M5). 페인터 Z = 충돌면 Z, 따라가기 켬. 비탈은 창 자르기를 끈 MI_TD_FluidTrack_SnowSlope 의 오르막이라 평면 평면이 비탈 아래로 숨고, 시작 위치에서 창(정본 20 m·보정 25 m 반폭)이 닿지 않게 떨어뜨린다(반박 M4, 지형을 따르는 층은 FX-9 B8). 굴림 공은 FX-8n 교반기를 재사용한다(반박 M3). 붓 반경(홈 폭)은 액터 속성이라 JSON 밖이다(반박 M7, INFRA-BRUSH 이관). 먼지 상자 높이·광원·궤도는 무대 상수(D-64 ② 편차).
"""
import hashlib
import importlib.util
import json
import math
import os

import unreal


FLUID_ROOT = "/Game/FluidFX"
MATERIAL_DIR = f"{FLUID_ROOT}/Materials"
PRESET_DIR = f"{FLUID_ROOT}/Presets"
MAP_DIR = f"{FLUID_ROOT}/Maps"
GRID_MESH_PATH = f"{FLUID_ROOT}/Meshes/SM_TD_FluidTrackGridUnit256"
GRID_MESH_SIZE_CM = 100.0
COLLECTION_PATH = f"{MATERIAL_DIR}/MPC_TD_FluidTrackWindow"
STIRRER_BLUEPRINT_PATH = f"{FLUID_ROOT}/Blueprints/BP_TDFluidStageStirrer"
GAME_MODE_PATH = "/Game/Combat/Blueprints/BP_TDCombatGameMode"
CUBE_MESH_PATH = "/Engine/BasicShapes/Cube"
CUBE_SIZE_CM = 100.0
FLUID_ACTOR_CLASS_PATH = "/Script/TDGame.TDFluidLiveActor"
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_track_stage_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_track_stage.json")
PRESET_TUNING_RELATIVE_PATH = ("Content", "FluidFX", "PresetData", "TDFluidPresetTuning.json")
MATERIAL_LOOK_RELATIVE_PATH = ("Content", "FluidFX", "PresetData", "TDFluidMaterialLook.json")
PRESET_CHECKER_RELATIVE_PATH = ("Tools", "check_fluid_preset_data.py")

FLOOR_Z_CM = 0.0
COLLISION_FLOOR_THICKNESS_CM = 50.0
PAINTER_Z_CM = FLOOR_Z_CM
CANONICAL_WINDOW_HALF_CM = 2000.0
CALIBRATION_WINDOW_HALF_CM = 2500.0
WINDOW_EDGE_MARGIN_CM = 500.0
EDGE_BLEND_BAND_CM = 270.0
SLOPE_DEGREES = 15.0
SLOPE_LENGTH_CM = 2000.0
SLOPE_WIDTH_CM = 2000.0
SLOPE_THICKNESS_CM = 50.0
DUST_VOLUME_HEIGHT_CM = 200.0
ROLLER_ORBIT_RADIUS_CM = 400.0
ROLLER_SCALE = 0.8
ROLLER_RADIUS_CM = 50.0 * ROLLER_SCALE
ROLLER_DEGREES_PER_SEC = 40.0
TRACE_HEIGHT_CM = 100.0
TRACE_TOLERANCE_CM = 1.0
SUN_DIRECTION = (-0.5, -0.3, -0.81)

SCENES = (
    {
        "name": "snow",
        "map": f"{MAP_DIR}/LV_TDFluidSnowStage",
        "floor_size_cm": 6000.0,
        "floor_instance": "MI_TD_FluidTrack_Snow",
        "track_preset": "SnowTrack",
        "dust_preset": "SnowDust",
        "player_start": (-1500.0, 0.0, 96.0),
        "roller_center": (-1500.0, 600.0),
        "dust_location": (-2000.0, -1500.0),
        "slope_start_x": 3000.0,
    },
    {
        "name": "sand",
        "map": f"{MAP_DIR}/LV_TDFluidSandStage",
        "floor_size_cm": 10000.0,
        "floor_instance": "MI_TD_FluidTrack_Sand",
        "track_preset": "SandTrack",
        "dust_preset": "SandDust",
        "player_start": (0.0, 0.0, 96.0),
        "roller_center": (0.0, 700.0),
        "dust_location": (-1500.0, -1500.0),
        "slope_start_x": None,
    },
)
PRESET_SPECS = (
    {"name": "SnowTrack", "kind": "FOOTPRINT_PAINTER", "output_material": "MI_TD_FluidTrack_Snow", "writes_collection": True},
    {"name": "SandTrack", "kind": "FOOTPRINT_PAINTER", "output_material": "MI_TD_FluidTrack_Sand", "writes_collection": True},
    {"name": "SnowDust", "kind": "EXPLOSION_GROUND_DUST", "volume_material": "MI_TD_FluidVolume_SnowDust", "writes_collection": False},
    {"name": "SandDust", "kind": "EXPLOSION_GROUND_DUST", "volume_material": "MI_TD_FluidVolume_SandDust", "writes_collection": False},
)
SLOPE_INSTANCE = "MI_TD_FluidTrack_SnowSlope"
REQUIRED_UNREAL_NAMES = (
    "get_engine_subsystem",
    "TDFluidPresetTuningSubsystem",
    "TDFluidPresetTuningSubsystem.reload_preset_tuning",
    "TDFluidPresetTuningSubsystem.get_load_errors",
    "TDFluidPresetAsset",
    "TDFluidPresetAsset.build_config_for_preset_kind",
    "TDFluidPresetKind.FOOTPRINT_PAINTER",
    "TDFluidPresetKind.EXPLOSION_GROUND_DUST",
    "TDFluidBrushChannel.CHANNEL_R_MUD",
    "EditorAssetLibrary.load_blueprint_class",
    "EditorAssetLibrary.load_asset",
    "EditorLoadingAndSavingUtils.get_dirty_map_packages",
    "EditorLoadingAndSavingUtils.get_dirty_content_packages",
    "SystemLibrary.get_project_directory",
    "SystemLibrary.line_trace_single",
    "TraceTypeQuery.TRACE_TYPE_QUERY1",
    "DrawDebugTrace.NONE",
    "DataAssetFactory",
    "RotatingMovementComponent",
    "DirectionalLight",
    "SkyLight",
    "PostProcessVolume",
    "PlayerStart",
    "AutoExposureMethod.AEM_MANUAL",
    "ComponentMobility.STATIC",
    "ComponentMobility.MOVABLE",
    "CollisionEnabled.NO_COLLISION",
)
PRESET_REQUIRED_PROPERTIES = ("preset_kind", "simulation_config", "default_channel", "override_preset_defaults", "output_material", "surface_mesh", "parameter_collection", "volume_material", "volume_mesh", "volume_height_cm")
ACTOR_REQUIRED_PROPERTIES = ("should_follow_player_pawn", "should_use_activation_volume", "interaction_brush_channel", "live_component")


def slope_rotation_pitch():
    return SLOPE_DEGREES


def slope_center(start_x):
    pitch = math.radians(SLOPE_DEGREES)
    axis_x = (math.cos(pitch), math.sin(pitch))
    axis_z = (-math.sin(pitch), math.cos(pitch))
    lower_top_edge_offset = (-0.5 * SLOPE_LENGTH_CM * axis_x[0] + 0.5 * SLOPE_THICKNESS_CM * axis_z[0], -0.5 * SLOPE_LENGTH_CM * axis_x[1] + 0.5 * SLOPE_THICKNESS_CM * axis_z[1])
    return (start_x - lower_top_edge_offset[0], FLOOR_Z_CM - lower_top_edge_offset[1])


def slope_top_height(center, local_x):
    pitch = math.radians(SLOPE_DEGREES)
    return center[1] + local_x * math.sin(pitch) + 0.5 * SLOPE_THICKNESS_CM * math.cos(pitch)


def validate_stage_layout():
    problems = []
    collision_top = FLOOR_Z_CM - 0.5 * COLLISION_FLOOR_THICKNESS_CM + 0.5 * COLLISION_FLOOR_THICKNESS_CM
    if abs(PAINTER_Z_CM - collision_top) > 1e-6:
        problems.append(f"페인터 Z {PAINTER_Z_CM} ≠ 충돌면 Z {collision_top}")
    writers = [spec["name"] for spec in PRESET_SPECS if spec["writes_collection"]]
    for scene in SCENES:
        scene_writers = [name for name in writers if name == scene["track_preset"]]
        if len(scene_writers) != 1:
            problems.append(f"{scene['name']}: MPC 쓰기 액터 {len(scene_writers)}개")
        start_x, start_y = scene["player_start"][:2]
        half_floor = 0.5 * scene["floor_size_cm"]
        reach = CALIBRATION_WINDOW_HALF_CM
        roller_x, roller_y = scene["roller_center"]
        roller_far = math.hypot(roller_x - start_x, roller_y - start_y) + ROLLER_ORBIT_RADIUS_CM + ROLLER_RADIUS_CM
        if roller_far > CANONICAL_WINDOW_HALF_CM - EDGE_BLEND_BAND_CM:
            problems.append(f"{scene['name']}: 굴림 공 궤도 바깥 {roller_far:.0f} cm 가 시작 창 안쪽 {CANONICAL_WINDOW_HALF_CM - EDGE_BLEND_BAND_CM} 밖")
        if abs(start_x) + reach > half_floor + 1e-6 and scene["slope_start_x"] is None:
            problems.append(f"{scene['name']}: 시작 위치 보정 창({reach} cm 반폭)이 바닥 밖으로 나감")
        if scene["slope_start_x"] is not None:
            gap = scene["slope_start_x"] - (start_x + reach)
            if gap < WINDOW_EDGE_MARGIN_CM:
                problems.append(f"{scene['name']}: 비탈 시작이 시작 창 끝에서 {gap:.0f} cm(< 여유 {WINDOW_EDGE_MARGIN_CM})")
            if abs(scene["slope_start_x"] - half_floor) > 1e-6:
                problems.append(f"{scene['name']}: 비탈 시작 X {scene['slope_start_x']} ≠ 평지 끝 {half_floor}")
            center = slope_center(scene["slope_start_x"])
            lower = slope_top_height(center, -0.5 * SLOPE_LENGTH_CM)
            upper = slope_top_height(center, 0.5 * SLOPE_LENGTH_CM)
            if abs(lower - FLOOR_Z_CM) > 0.01 or upper <= lower:
                problems.append(f"{scene['name']}: 비탈 윗면 아래끝 {lower:.3f}·위끝 {upper:.1f}(오르막·아래끝 = 바닥 아님)")
    return problems


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


def asset_disk_path(asset_path, extension):
    return project_path("Content", *asset_path[len("/Game/"):].split("/")) + extension


def preset_asset_name(spec_name):
    return f"DA_TDFluid_{spec_name}"


def preset_asset_path(spec_name):
    return f"{PRESET_DIR}/{preset_asset_name(spec_name)}"


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


def check_cpp_contract(actor_class):
    default_preset = unreal.get_default_object(unreal.TDFluidPresetAsset)
    problems = [f"TDFluidPresetAsset.{name}" for name in PRESET_REQUIRED_PROPERTIES if not has_editor_property(default_preset, name)]
    default_actor = unreal.get_default_object(actor_class)
    problems.extend(f"TDFluidLiveActor.{name}" for name in ACTOR_REQUIRED_PROPERTIES if not has_editor_property(default_actor, name))
    return problems


def read_overwrite_request():
    request_path = project_path(*REQUEST_RELATIVE_PATH)
    if not os.path.isfile(request_path):
        return False
    with open(request_path, encoding="utf-8") as request_file:
        request = json.load(request_file)
    return isinstance(request, dict) and request.get("overwrite") is True


def find_unsaved_package_names(overwrite):
    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    dirty.extend(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    names = sorted({package.get_name() for package in dirty})
    return names if not overwrite else [name for name in names if not name.startswith(FLUID_ROOT + "/")]


def ensure_directory(directory_path):
    if unreal.EditorAssetLibrary.does_directory_exist(directory_path):
        return
    if not unreal.EditorAssetLibrary.make_directory(directory_path):
        raise RuntimeError(f"폴더 생성 실패: {directory_path}")


def save_asset(asset, asset_path):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"에셋 저장 실패: {asset_path}")


def read_file_sha256(relative_parts):
    path = project_path(*relative_parts)
    if not os.path.isfile(path):
        raise RuntimeError(f"정본 파일 없음: {path}")
    with open(path, "rb") as source_file:
        return hashlib.sha256(source_file.read()).hexdigest()


def reload_preset_tuning():
    checker_path = project_path(*PRESET_CHECKER_RELATIVE_PATH)
    module_spec = importlib.util.spec_from_file_location("check_fluid_preset_data", checker_path)
    if module_spec is None or module_spec.loader is None:
        raise RuntimeError(f"프리셋 정본 검사기 없음: {checker_path}")
    checker = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(checker)
    before = read_file_sha256(PRESET_TUNING_RELATIVE_PATH)
    problems = checker.check_tuning_file(project_path(*PRESET_TUNING_RELATIVE_PATH))
    if problems:
        raise RuntimeError("프리셋 정본 JSON 검사 실패(python Tools/check_fluid_preset_data.py): " + " | ".join(problems))
    tuning = unreal.get_engine_subsystem(unreal.TDFluidPresetTuningSubsystem)
    if tuning is None:
        raise RuntimeError("TDFluidPresetTuningSubsystem 없음(재빌드와 에디터 재시작 필요)")
    error_count = tuning.reload_preset_tuning()
    if error_count != 0:
        raise RuntimeError(f"프리셋 정본 다시 읽기 오류 {error_count}건: " + " | ".join(tuning.get_load_errors()))
    if read_file_sha256(PRESET_TUNING_RELATIVE_PATH) != before:
        raise RuntimeError("프리셋 정본 JSON 이 검사·다시 읽기 도중 바뀌었습니다. 다시 실행하세요.")
    return before


def open_empty_level(levels, actors, map_path):
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
            raise RuntimeError(f"기존 무대 레벨 액터 제거 실패: {actor.get_actor_label()}")


def load_or_create_preset(spec_name):
    asset_path = preset_asset_path(spec_name)
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        preset = unreal.EditorAssetLibrary.load_asset(asset_path)
        if not isinstance(preset, unreal.TDFluidPresetAsset):
            raise RuntimeError(f"기존 프리셋 에셋 형식 불일치: {asset_path}")
        return preset
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.TDFluidPresetAsset)
    preset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(preset_asset_name(spec_name), PRESET_DIR, unreal.TDFluidPresetAsset, factory)
    if preset is None:
        raise RuntimeError(f"프리셋 생성 실패: {asset_path}")
    return preset


def configure_preset(spec, loaded):
    preset_kind = getattr(unreal.TDFluidPresetKind, spec["kind"])
    preset = load_or_create_preset(spec["name"])
    preset.set_editor_property("preset_kind", preset_kind)
    preset.set_editor_property("default_channel", unreal.TDFluidBrushChannel.CHANNEL_R_MUD)
    preset.set_editor_property("override_preset_defaults", False)
    preset.set_editor_property("simulation_config", unreal.TDFluidPresetAsset.build_config_for_preset_kind(preset_kind))
    if "output_material" in spec:
        preset.set_editor_property("output_material", loaded[spec["output_material"]])
        preset.set_editor_property("surface_mesh", loaded["grid"])
        preset.set_editor_property("parameter_collection", loaded["collection"])
    else:
        preset.set_editor_property("volume_material", loaded[spec["volume_material"]])
        preset.set_editor_property("volume_mesh", loaded["cube"])
        preset.set_editor_property("volume_height_cm", DUST_VOLUME_HEIGHT_CM)
        preset.set_editor_property("parameter_collection", None)
    effective = preset.resolve_effective_config()
    extent = effective.get_editor_property("sim_extent_cm")
    if extent.x <= 0.0 or extent.y <= 0.0:
        raise RuntimeError(f"유효 구성 창이 0: {preset_asset_path(spec['name'])}")
    save_asset(preset, preset_asset_path(spec["name"]))
    return preset, effective


def trace_floor_height(world, x, y, expected_z):
    start = unreal.Vector(x, y, expected_z + TRACE_HEIGHT_CM)
    end = unreal.Vector(x, y, expected_z - TRACE_HEIGHT_CM)
    hit = unreal.SystemLibrary.line_trace_single(world, start, end, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE, True)
    if hit is None:
        return None
    values = hit.to_tuple()
    return values[5].z if values[0] else None


def write_report(report):
    report_path = project_path(*REPORT_RELATIVE_PATH)
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, "w", encoding="utf-8") as report_file:
        json.dump(report, report_file, ensure_ascii=False, indent=2, default=str)
    return report_path


def build_scene(scene, levels, actors, editor, loaded, presets, fluid_actor_class, stirrer_class):
    ensure_directory(MAP_DIR)
    open_empty_level(levels, actors, scene["map"])
    world = editor.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", loaded["game_mode"])
    created = []
    folder_root = f"TDGen/FluidFX/{scene['name'].capitalize()}Stage/"

    def finish(actor, label, folder):
        if actor is None:
            raise RuntimeError(f"액터 생성 실패: {label}")
        actor.set_actor_label("TDGen_" + label)
        actor.set_folder_path(folder_root + folder)
        created.append(actor)
        return actor

    def spawn(actor_class, location, label, folder, rotation=None):
        return finish(actors.spawn_actor_from_class(actor_class, unreal.Vector(*location), rotation or unreal.Rotator()), label, folder)

    def place_mesh(mesh, location, scale, material, label, folder, rotation=None):
        actor = finish(actors.spawn_actor_from_object(mesh, unreal.Vector(*location), rotation or unreal.Rotator()), label, folder)
        actor.set_actor_scale3d(unreal.Vector(*scale))
        component = actor.static_mesh_component
        component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        if material is not None:
            component.set_material(0, material)
        return actor, component

    size = scene["floor_size_cm"]
    _, floor_component = place_mesh(loaded["grid"], (0.0, 0.0, FLOOR_Z_CM), (size / GRID_MESH_SIZE_CM, size / GRID_MESH_SIZE_CM, 1.0), loaded[scene["floor_instance"]], "VisibleFloor", "Stage")
    floor_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    collision_actor, collision_component = place_mesh(loaded["cube"], (0.0, 0.0, FLOOR_Z_CM - 0.5 * COLLISION_FLOOR_THICKNESS_CM), (size / CUBE_SIZE_CM, size / CUBE_SIZE_CM, COLLISION_FLOOR_THICKNESS_CM / CUBE_SIZE_CM), None, "CollisionFloor", "Stage")
    collision_component.set_visibility(False)
    collision_actor.set_actor_hidden_in_game(True)

    slope_record = None
    if scene["slope_start_x"] is not None:
        center = slope_center(scene["slope_start_x"])
        slope_actor, _ = place_mesh(
            loaded["cube"], (center[0], 0.0, center[1]), (SLOPE_LENGTH_CM / CUBE_SIZE_CM, SLOPE_WIDTH_CM / CUBE_SIZE_CM, SLOPE_THICKNESS_CM / CUBE_SIZE_CM),
            loaded[SLOPE_INSTANCE], "Slope15", "Stage", unreal.Rotator(pitch=slope_rotation_pitch(), yaw=0.0, roll=0.0),
        )
        slope_record = {"label": slope_actor.get_actor_label(), "instance": SLOPE_INSTANCE, "start_x": scene["slope_start_x"], "degrees": SLOPE_DEGREES, "length_cm": SLOPE_LENGTH_CM, "rise_cm": SLOPE_LENGTH_CM * math.sin(math.radians(SLOPE_DEGREES))}

    pitch = math.degrees(math.asin(SUN_DIRECTION[2] / math.sqrt(sum(value * value for value in SUN_DIRECTION))))
    yaw = math.degrees(math.atan2(SUN_DIRECTION[1], SUN_DIRECTION[0]))
    sun = spawn(unreal.DirectionalLight, (0, 0, 2000), "TrackStageSun", "Lighting", unreal.Rotator(pitch=pitch, yaw=yaw, roll=0.0))
    sun.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_intensity(6.0)
    sky = spawn(unreal.SkyLight, (0, 0, 1500), "TrackStageSkyLight", "Lighting")
    sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", True)
    sky.light_component.set_intensity(0.5)
    post_process = spawn(unreal.PostProcessVolume, (0, 0, 0), "TrackStageExposure", "Lighting")
    post_process.set_editor_property("unbound", True)
    settings = post_process.settings
    for name, value in (("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL), ("auto_exposure_apply_physical_camera_exposure", False), ("auto_exposure_bias", 0.0)):
        settings.set_editor_property("override_" + name, True)
        settings.set_editor_property(name, value)
    post_process.set_editor_property("settings", settings)
    spawn(unreal.PlayerStart, scene["player_start"], "TrackStagePlayerStart", "Gameplay")

    track_preset, track_effective = presets[scene["track_preset"]]
    painter = spawn(fluid_actor_class, (scene["player_start"][0], scene["player_start"][1], PAINTER_Z_CM), scene["track_preset"], "Fluid")
    painter.set_editor_property("should_follow_player_pawn", True)
    painter.set_editor_property("should_use_activation_volume", False)
    painter.set_editor_property("interaction_brush_channel", unreal.TDFluidBrushChannel.CHANNEL_R_MUD)
    live_component = painter.get_editor_property("live_component")
    live_component.set_editor_property("active_preset", track_preset)
    live_component.set_editor_property("should_init_system_state_from_preset", True)
    live_component.set_editor_property("simulation_config", track_effective)

    dust_preset, dust_effective = presets[scene["dust_preset"]]
    dust = spawn(fluid_actor_class, (scene["dust_location"][0], scene["dust_location"][1], FLOOR_Z_CM), scene["dust_preset"], "Fluid")
    dust.set_editor_property("should_follow_player_pawn", False)
    dust.set_editor_property("should_use_activation_volume", True)
    dust.set_editor_property("interaction_brush_channel", unreal.TDFluidBrushChannel.CHANNEL_R_MUD)
    dust_component = dust.get_editor_property("live_component")
    dust_component.set_editor_property("active_preset", dust_preset)
    dust_component.set_editor_property("should_init_system_state_from_preset", True)
    dust_component.set_editor_property("simulation_config", dust_effective)

    roller_x, roller_y = scene["roller_center"]
    roller = spawn(stirrer_class, (roller_x - ROLLER_ORBIT_RADIUS_CM, roller_y, FLOOR_Z_CM + ROLLER_RADIUS_CM), "TrackRoller", "Fluid")
    roller.set_actor_scale3d(unreal.Vector(ROLLER_SCALE, ROLLER_SCALE, ROLLER_SCALE))
    rotating = roller.get_component_by_class(unreal.RotatingMovementComponent)
    if rotating is None:
        raise RuntimeError("교반기에 회전 이동 컴포넌트가 없음: " + STIRRER_BLUEPRINT_PATH)
    rotating.set_editor_property("pivot_translation", unreal.Vector(ROLLER_ORBIT_RADIUS_CM, 0.0, 0.0))
    rotating.set_editor_property("rotation_rate", unreal.Rotator(0.0, ROLLER_DEGREES_PER_SEC, 0.0))

    trace_points = [(scene["player_start"][0], scene["player_start"][1]), (roller_x, roller_y), (0.25 * size, -0.25 * size)]
    traces = [{"xy": [x, y], "hit_z": trace_floor_height(world, x, y, FLOOR_Z_CM)} for x, y in trace_points]
    bad = [item for item in traces if item["hit_z"] is None or abs(item["hit_z"] - FLOOR_Z_CM) > TRACE_TOLERANCE_CM]
    if bad:
        raise RuntimeError(f"{scene['name']}: 바닥 위 1 m 에서 아래로 쏜 라인 트레이스가 충돌면을 못 찾음: {bad}")

    if not levels.save_current_level():
        raise RuntimeError(f"무대 레벨 저장 실패: {scene['map']}")
    extent = track_effective.get_editor_property("sim_extent_cm")
    return {
        "name": scene["name"],
        "map": scene["map"],
        "painter": {"label": painter.get_actor_label(), "preset": preset_asset_name(scene["track_preset"]), "z_cm": PAINTER_Z_CM, "follow": True, "extent_cm": [extent.x, extent.y], "grid_resolution": track_effective.get_editor_property("grid_resolution")},
        "dust": {"label": dust.get_actor_label(), "preset": preset_asset_name(scene["dust_preset"]), "volume_height_cm": DUST_VOLUME_HEIGHT_CM},
        "roller": {"label": roller.get_actor_label(), "orbit_radius_cm": ROLLER_ORBIT_RADIUS_CM, "degrees_per_sec": ROLLER_DEGREES_PER_SEC, "blueprint": STIRRER_BLUEPRINT_PATH},
        "floor": {"visible_instance": scene["floor_instance"], "size_cm": size, "collision": "숨긴 큐브, 윗면 Z = 바닥", "shadow_cache": "기본(Rigid 아님)"},
        "slope": slope_record,
        "floor_traces": traces,
        "created_actors": len(created),
    }


def main():
    assets = unreal.EditorAssetLibrary
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    missing_names = find_missing_unreal_names()
    if missing_names:
        raise RuntimeError("언리얼 Python 이름 없음: " + ", ".join(missing_names))
    layout_problems = validate_stage_layout()
    if layout_problems:
        raise RuntimeError("무대 배치 자체 점검 실패: " + " | ".join(layout_problems))
    if editor.get_game_world() is not None:
        raise RuntimeError("PIE를 종료한 뒤 실행하세요.")

    overwrite = read_overwrite_request()
    guarded = [preset_asset_path(spec["name"]) for spec in PRESET_SPECS] + [scene["map"] for scene in SCENES]
    existing_paths = [path for path in guarded if assets.does_asset_exist(path)]
    if existing_paths and not overwrite:
        print(json.dumps({"status": "기존 에셋이 있어 아무것도 바꾸지 않았습니다", "existing_assets": existing_paths, "overwrite_request_file": project_path(*REQUEST_RELATIVE_PATH), "overwrite_request_content": {"overwrite": True}}, ensure_ascii=False))
        raise RuntimeError("기존 에셋 보존: overwrite 요청 파일이 없어 중단합니다.")
    unsaved_names = find_unsaved_package_names(overwrite)
    if unsaved_names:
        raise RuntimeError("미저장 작업을 보존합니다: " + ", ".join(unsaved_names))

    print("단계: 프리셋 정본 검사·다시 읽기")
    preset_tuning_sha256 = reload_preset_tuning()
    material_look_sha256 = read_file_sha256(MATERIAL_LOOK_RELATIVE_PATH)

    material_names = {spec.get("output_material") or spec.get("volume_material") for spec in PRESET_SPECS} | {scene["floor_instance"] for scene in SCENES} | {SLOPE_INSTANCE}
    loaded = {name: assets.load_asset(f"{MATERIAL_DIR}/{name}") for name in material_names}
    loaded.update({"grid": assets.load_asset(GRID_MESH_PATH), "cube": assets.load_asset(CUBE_MESH_PATH), "collection": assets.load_asset(COLLECTION_PATH), "game_mode": assets.load_blueprint_class(GAME_MODE_PATH)})
    stirrer_class = assets.load_blueprint_class(STIRRER_BLUEPRINT_PATH)
    fluid_actor_class = unreal.load_class(None, FLUID_ACTOR_CLASS_PATH)
    missing = [name for name, value in loaded.items() if value is None] + [name for name, value in (("교반기(FX-8n 무대 먼저)", stirrer_class), ("유체 액터 클래스", fluid_actor_class)) if value is None]
    if missing:
        raise RuntimeError("필수 에셋 또는 클래스 없음(editor_make_fluid_track_material.py·FX-8n 무대 먼저): " + ", ".join(missing))
    contract_problems = check_cpp_contract(fluid_actor_class)
    if contract_problems:
        raise RuntimeError("C++ 계약 속성 없음: " + ", ".join(contract_problems))

    print("단계: 프리셋")
    ensure_directory(PRESET_DIR)
    presets = {spec["name"]: configure_preset(spec, loaded) for spec in PRESET_SPECS}

    scene_records = []
    for scene in SCENES:
        print(f"단계: 장면 {scene['name']}")
        scene_records.append(build_scene(scene, levels, actors, editor, loaded, presets, fluid_actor_class, stirrer_class))

    expected_files = [asset_disk_path(preset_asset_path(spec["name"]), ".uasset") for spec in PRESET_SPECS] + [asset_disk_path(scene["map"], ".umap") for scene in SCENES]
    missing_files = [path for path in expected_files if not os.path.isfile(path)]
    if missing_files:
        raise RuntimeError("저장 파일 확인 실패: " + ", ".join(missing_files))

    report = {
        "status": "ok",
        "preset_tuning_sha256": preset_tuning_sha256,
        "material_look_sha256": material_look_sha256,
        "overwrite": overwrite,
        "scenes": scene_records,
        "deviations": [
            "먼지 상자 높이·광원·굴림 궤도·비탈 치수는 무대 상수(D-64 ②)",
            "홈 폭은 액터 붓 반경(BoneBrushRadiusCm 45·ActorPivotBrushRadiusCm 65) 몫, JSON 밖(INFRA-BRUSH 이관), 보정은 룩데브 props",
            "ExplosionGroundDust 대리 종류는 TerrainSlopeGravity 0 이라 비탈 흐름 없음(FireFamily), 조절 사본은 폭발 무대에도 걸림",
        ],
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
