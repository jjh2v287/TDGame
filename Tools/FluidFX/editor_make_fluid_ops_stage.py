"""실행 환경: TDGame 언리얼 에디터 안(Python 원격 실행). 운용 장치 무대 LV_TDFluidOpsStage(구역 0 외부 렌더 타깃 Mud, 구역 1a 처리 주기 상한 비용 횃불 2, 구역 1b 거리 곡선 횃불 8, 구역 1c 카메라 표지와 등 뒤 횃불, 구역 2 레벨 시퀀스 큐브가 Mud를 4초에 가로지름)를 만든다. 먼저 Mud·횃불 프리셋(DA_TDFluid_Mud, editor_make_fluid_fire_stage.py)이 있어야 하고 FX-16a 묶음 A·B 빌드와 에디터 재시작 뒤에만 실행한다(ExternalOutputTargets 이름 필요).
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_ops_stage.py (PowerShell, L-repo-01. 기존 에셋을 다시 지을 때는 Saved/FluidFX/fluid_ops_stage_request.json 에 {"overwrite": true}, 다른 차선의 모양 조각 병합 뒤 마지막에 실행해 STALE을 피한다)
출력: /Game/FluidFX/Textures/RT_TD_FluidOps{DensityPressure,Velocity}, /Game/FluidFX/Presets/DA_TDFluid_OpsMudExternal, /Game/FluidFX/Sequences/LS_TD_FluidOpsSequencerCube, /Game/FluidFX/Maps/LV_TDFluidOpsStage, Saved/FluidFX/fluid_ops_stage.json(preset_tuning_sha256·material_look_sha256·TDGen_Ops* 좌표·건너뛴 구역, check_fluid_preset_data.py --stage-report 형식)
상태: 미검증(2026-10-29 FX-16a 묶음 D 작성: py_compile·가짜 unreal 배치 계산만, 에디터 실행 전). 시퀀스 구역은 L-editor-73·66에 따라 반환값 대신 다시 읽어 판정하고 실패하면 그 구역만 건너뛰어 보고에 이유를 남긴다. RT 지우기 색은 C++ EmptyDensityPressureColor·EmptyVelocityColor 와 같아야 한다(외부 RT는 풀 반납 때 자기 색으로 지워진다). 2026-10-30 A5 에셋 단계: 에디터 실행 성공(구역 0·1a·1b·1c·2 모두, stage-report stale 0). 고침: 렌더 타깃 크기는 상수 64 대신 Mud 유효 격자 해상도(DA_TDFluid_Mud override 128)를 따름, generate_overlap_events 는 set_editor_property, 변환 채널은 get_all_channels. 키 보간은 default(선형 열거 미탐지).
"""
import importlib.util
import json
import math
import os

import unreal

FLUID_ROOT = "/Game/FluidFX"
TEXTURE_DIR = f"{FLUID_ROOT}/Textures"
SEQUENCE_DIR = f"{FLUID_ROOT}/Sequences"
STAGE_MAP_PATH = f"{FLUID_ROOT}/Maps/LV_TDFluidOpsStage"
SOURCE_PRESET_NAME = "Mud"
EXTERNAL_PRESET_NAME = "OpsMudExternal"
TORCH_PRESET_NAME = "TorchFlame"
TORCH_ACTOR_SPEC_NAME = "TorchFire"
SEQUENCE_NAME = "LS_TD_FluidOpsSequencerCube"
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_ops_stage_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_ops_stage.json")
STAGE_FOLDER = "TDGen/FluidFX/OpsStage"

EMPTY_DENSITY_PRESSURE_CLEAR_COLOR = (0.0, 0.5, 0.0, 0.0)
EMPTY_VELOCITY_CLEAR_COLOR = (0.5, 0.5, 0.0, 0.0)
CLEAR_COLOR_TOLERANCE = 1e-6
RENDER_TARGET_SPECS = (
    {"name": "RT_TD_FluidOpsDensityPressure", "target_property": "density_pressure_target", "clear_color": EMPTY_DENSITY_PRESSURE_CLEAR_COLOR},
    {"name": "RT_TD_FluidOpsVelocity", "target_property": "velocity_target", "clear_color": EMPTY_VELOCITY_CLEAR_COLOR},
)
EXTERNAL_TARGET_PROPERTIES = ("density_pressure_target", "velocity_target", "aux_target", "water_surface_target")
PRESET_SYNC_PROPERTIES = ("preset_kind", "override_preset_defaults", "default_channel", "simulation_config", "plane_settings", "volume_render",
                          "output_material", "surface_mesh", "volume_material", "volume_mesh", "volume_height_cm", "translucency_sort_priority")

EXTERNAL_MUD_CENTER = (0.0, 0.0)
CAP_ROW_ORIGIN = (0.0, 6000.0)
CAP_ROW_COUNT = 2
CAP_ROW_SPACING_CM = 1500.0
LOD_ROW_ORIGIN = (0.0, 9000.0)
LOD_ROW_COUNT = 8
LOD_ROW_SPACING_CM = 1000.0
CAMERA_SCENE_ORIGIN = (0.0, 13000.0)
BEHIND_TORCH_OFFSET_X_CM = -2400.0
TORCH_HEIGHT_CM = 100.0
CAMERA_MARK_SCALE = 0.5
CAMERA_PLAN = {"distance_cm": 1500.0, "pitch": -10.0, "yaw": 0.0, "height_cm": 100.0}
SEQUENCER_MUD_CENTER = (0.0, -6000.0)
SEQUENCER_CUBE_SCALE = 1.0
SEQUENCER_CUBE_HEIGHT_CM = 50.0
SEQUENCER_HALF_SPAN_CM = 1400.0
SEQUENCER_CROSS_SECONDS = 4.0
SEQUENCER_COLLISION_PROFILE = "OverlapAllDynamic"
BASIC_SHAPE_SIZE_CM = 100.0
MUD_ACTOR_HEIGHT_CM = 1.0
FLOOR_X_RANGE_CM = (-4000.0, 9000.0)
FLOOR_Y_RANGE_CM = (-8000.0, 14500.0)
FLOOR_THICKNESS_SCALE = 0.5

EXTRA_REQUIRED_UNREAL_NAMES = (
    "StaticMeshActor",
    "TextureRenderTarget2D",
    "TextureRenderTargetFactoryNew",
    "TextureRenderTargetFormat.RTF_RGBA16F",
    "TextureAddress.TA_CLAMP",
    "TDFluidExternalOutputTargets",
    "EditorAssetLibrary.duplicate_asset",
)
SEQUENCER_UNREAL_NAMES = (
    "LevelSequence",
    "LevelSequenceFactoryNew",
    "LevelSequenceActor",
    "MovieScene3DTransformTrack",
    "FrameNumber",
)
EXTRA_PRESET_PROPERTIES = ("external_output_targets",)
EXTRA_ACTOR_PROPERTIES = ("activation_distance_cm",)


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


def forward_vector(pitch_degrees, yaw_degrees):
    pitch, yaw = math.radians(pitch_degrees), math.radians(yaw_degrees)
    return (math.cos(pitch) * math.cos(yaw), math.cos(pitch) * math.sin(yaw), math.sin(pitch))


def compute_camera_location(mark_location, camera_plan):
    forward = forward_vector(camera_plan["pitch"], camera_plan["yaw"])
    distance = camera_plan["distance_cm"]
    return (mark_location[0] - forward[0] * distance, mark_location[1] - forward[1] * distance,
            mark_location[2] + camera_plan["height_cm"] - forward[2] * distance)


def is_behind_camera(camera_location, camera_plan, point):
    forward = forward_vector(camera_plan["pitch"], camera_plan["yaw"])
    offset = (point[0] - camera_location[0], point[1] - camera_location[1], point[2] - camera_location[2])
    return sum(forward[axis] * offset[axis] for axis in range(3)) < 0.0


def make_row_locations(origin, count, spacing, height):
    return [(origin[0] + index * spacing, origin[1], height) for index in range(count)]


def build_layout():
    mark_location = (CAMERA_SCENE_ORIGIN[0], CAMERA_SCENE_ORIGIN[1], TORCH_HEIGHT_CM)
    behind_location = (mark_location[0] + BEHIND_TORCH_OFFSET_X_CM, mark_location[1], TORCH_HEIGHT_CM)
    camera_location = compute_camera_location(mark_location, CAMERA_PLAN)
    return {
        "external_mud": (EXTERNAL_MUD_CENTER[0], EXTERNAL_MUD_CENTER[1], MUD_ACTOR_HEIGHT_CM),
        "cap_row": make_row_locations(CAP_ROW_ORIGIN, CAP_ROW_COUNT, CAP_ROW_SPACING_CM, TORCH_HEIGHT_CM),
        "lod_row": make_row_locations(LOD_ROW_ORIGIN, LOD_ROW_COUNT, LOD_ROW_SPACING_CM, TORCH_HEIGHT_CM),
        "camera_mark": mark_location,
        "behind_torch": behind_location,
        "expected_camera": camera_location,
        "behind_is_outside_view": is_behind_camera(camera_location, CAMERA_PLAN, behind_location),
        "behind_to_mark_planar_cm": math.hypot(behind_location[0] - mark_location[0], behind_location[1] - mark_location[1]),
        "sequencer_mud": (SEQUENCER_MUD_CENTER[0], SEQUENCER_MUD_CENTER[1], MUD_ACTOR_HEIGHT_CM),
        "sequencer_cube_start": (SEQUENCER_MUD_CENTER[0] - SEQUENCER_HALF_SPAN_CM, SEQUENCER_MUD_CENTER[1], SEQUENCER_CUBE_HEIGHT_CM),
        "sequencer_cube_end": (SEQUENCER_MUD_CENTER[0] + SEQUENCER_HALF_SPAN_CM, SEQUENCER_MUD_CENTER[1], SEQUENCER_CUBE_HEIGHT_CM),
    }


def seconds_to_display_frame(seconds, frame_rate_numerator, frame_rate_denominator):
    return int(round(seconds * frame_rate_numerator / frame_rate_denominator))


def describe_reference(value):
    if value is None:
        return ""
    path = value.get_path_name() if hasattr(value, "get_path_name") else str(value)
    return path.split(".")[0]


def assign_soft_reference(owner, property_name, asset):
    package_path = asset.get_path_name().split(".")[0]
    for candidate in (asset, unreal.SoftObjectPath(asset.get_path_name())):
        try:
            owner.set_editor_property(property_name, candidate)
        except Exception:
            continue
        if describe_reference(owner.get_editor_property(property_name)) == package_path:
            return
    raise RuntimeError(f"소프트 참조 지정 확인 실패: {property_name} → {package_path}")


def is_same_color(color, expected):
    actual = (color.r, color.g, color.b, color.a)
    return all(abs(actual[index] - expected[index]) <= CLEAR_COLOR_TOLERANCE for index in range(4))


def load_or_create_render_target(particle_stage, spec, render_target_size):
    path = f"{TEXTURE_DIR}/{spec['name']}"
    assets = unreal.EditorAssetLibrary
    was_created = not assets.does_asset_exist(path)
    if was_created:
        particle_stage.ensure_directory(TEXTURE_DIR)
        render_target = unreal.AssetToolsHelpers.get_asset_tools().create_asset(spec["name"], TEXTURE_DIR, unreal.TextureRenderTarget2D, unreal.TextureRenderTargetFactoryNew())
    else:
        render_target = assets.load_asset(path)
    if not isinstance(render_target, unreal.TextureRenderTarget2D):
        raise RuntimeError(f"렌더 타깃 생성 또는 형식 확인 실패: {path}")
    for name, value in (
        ("render_target_format", unreal.TextureRenderTargetFormat.RTF_RGBA16F),
        ("size_x", render_target_size),
        ("size_y", render_target_size),
        ("clear_color", unreal.LinearColor(*spec["clear_color"])),
        ("address_x", unreal.TextureAddress.TA_CLAMP),
        ("address_y", unreal.TextureAddress.TA_CLAMP),
        ("auto_generate_mips", False),
        ("supports_uav", True),
    ):
        render_target.set_editor_property(name, value)
    problems = []
    if render_target.get_editor_property("render_target_format") != unreal.TextureRenderTargetFormat.RTF_RGBA16F:
        problems.append("render_target_format")
    if int(render_target.get_editor_property("size_x")) != render_target_size or int(render_target.get_editor_property("size_y")) != render_target_size:
        problems.append("size")
    if not is_same_color(render_target.get_editor_property("clear_color"), spec["clear_color"]):
        problems.append("clear_color")
    if render_target.get_editor_property("address_x") != unreal.TextureAddress.TA_CLAMP or render_target.get_editor_property("address_y") != unreal.TextureAddress.TA_CLAMP:
        problems.append("address")
    if render_target.get_editor_property("auto_generate_mips"):
        problems.append("auto_generate_mips")
    if not render_target.get_editor_property("supports_uav"):
        problems.append("supports_uav")
    if problems:
        raise RuntimeError(f"렌더 타깃 설정 다시 읽기 불일치: {path} {problems}")
    particle_stage.save_asset(render_target, path)
    color = render_target.get_editor_property("clear_color")
    return render_target, {"asset": path, "created": was_created, "size": render_target_size, "format": "RTF_RGBA16F", "address": "Clamp",
                           "auto_generate_mips": False, "supports_uav": True, "clear_color": [color.r, color.g, color.b, color.a]}


def sync_preset_from_source(particle_stage, preset, source):
    synced = []
    for property_name in PRESET_SYNC_PROPERTIES:
        if not (particle_stage.has_editor_property(preset, property_name) and particle_stage.has_editor_property(source, property_name)):
            continue
        preset.set_editor_property(property_name, source.get_editor_property(property_name))
        synced.append(property_name)
    return synced


def build_external_preset(particle_stage, source_preset, render_targets):
    assets = unreal.EditorAssetLibrary
    source_path = f"{particle_stage.PRESET_DIR}/DA_TDFluid_{SOURCE_PRESET_NAME}"
    path = f"{particle_stage.PRESET_DIR}/DA_TDFluid_{EXTERNAL_PRESET_NAME}"
    was_created = not assets.does_asset_exist(path)
    if was_created and assets.duplicate_asset(source_path, path) is None:
        raise RuntimeError(f"프리셋 복제 실패: {source_path} → {path}")
    preset = assets.load_asset(path)
    if not isinstance(preset, unreal.TDFluidPresetAsset):
        raise RuntimeError(f"복제 프리셋 형식 확인 실패: {path}")
    synced = [] if was_created else sync_preset_from_source(particle_stage, preset, source_preset)
    targets = preset.get_editor_property("external_output_targets")
    for spec in RENDER_TARGET_SPECS:
        assign_soft_reference(targets, spec["target_property"], render_targets[spec["name"]])
    preset.set_editor_property("external_output_targets", targets)
    stored = preset.get_editor_property("external_output_targets")
    for spec in RENDER_TARGET_SPECS:
        if describe_reference(stored.get_editor_property(spec["target_property"])) != render_targets[spec["name"]].get_path_name().split(".")[0]:
            raise RuntimeError(f"프리셋 외부 렌더 타깃 다시 읽기 불일치: {path} {spec['target_property']}")
    for property_name in ("aux_target", "water_surface_target"):
        if describe_reference(stored.get_editor_property(property_name)):
            raise RuntimeError(f"프리셋 외부 렌더 타깃에 의도하지 않은 값: {path} {property_name}")
    if preset.get_editor_property("preset_kind") != source_preset.get_editor_property("preset_kind"):
        raise RuntimeError(f"복제 프리셋 종류가 원본과 다름: {path}")
    effective = preset.resolve_effective_config()
    resolution = int(effective.get_editor_property("grid_resolution"))
    render_target_size = int(next(iter(render_targets.values())).get_editor_property("size_x"))
    if resolution != render_target_size:
        raise RuntimeError(f"Mud 격자 해상도 {resolution} 가 외부 렌더 타깃 {render_target_size}² 와 달라 내부 렌더 타깃으로 떨어진다: {path}")
    particle_stage.save_asset(preset, path)
    details = {"asset": path, "source": source_path, "created": was_created, "synced_properties": synced, "grid_resolution": resolution,
               "solver_backend": str(effective.get_editor_property("solver_backend")),
               "area_address_mode": str(effective.get_editor_property("area_address_mode")) if particle_stage.has_editor_property(effective, "area_address_mode") else None,
               "external_output_targets": {spec["target_property"]: path_of(render_targets[spec["name"]]) for spec in RENDER_TARGET_SPECS}}
    return preset, effective, details


def path_of(asset):
    return asset.get_path_name().split(".")[0]


def record_location(context, actor):
    location = actor.get_actor_location()
    context["labels"][actor.get_actor_label()] = [location.x, location.y, location.z]
    return actor


def spawn_marker(context, mesh, location, scale, label, folder):
    actor = context["actors"].spawn_actor_from_object(mesh, unreal.Vector(*location), unreal.Rotator())
    context["particle_stage"].finish_actor(actor, label, folder, context["created"])
    actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
    actor.static_mesh_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    return record_location(context, actor)


def spawn_live_mud(context, preset, effective, location, label, folder):
    actor = context["actors"].spawn_actor_from_class(context["live_actor_class"], unreal.Vector(*location), unreal.Rotator())
    context["particle_stage"].finish_actor(actor, label, folder, context["created"])
    context["particle_stage"].configure_live_actor(actor, preset, effective)
    return record_location(context, actor)


def spawn_torch(context, location, label, folder, uses_activation_volume):
    fire_stage, particle_stage = context["fire_stage"], context["particle_stage"]
    actor = context["actors"].spawn_actor_from_class(context["fire_actor_class"], unreal.Vector(*location), unreal.Rotator())
    particle_stage.finish_actor(actor, label, folder, context["created"])
    fire_stage.configure_fire_actor(actor, context["torch_actor_spec"], context["torch_preset"], context["torch_effective"], context["assets"])
    actor.set_editor_property("should_use_activation_volume", uses_activation_volume)
    if bool(actor.get_editor_property("should_use_activation_volume")) != uses_activation_volume:
        raise RuntimeError(f"활성 볼륨 사용 지정 확인 실패: {actor.get_actor_label()}")
    if bool(actor.get_editor_property("should_follow_player_pawn")):
        raise RuntimeError(f"횃불이 폰을 따라가도록 남아 있음: {actor.get_actor_label()}")
    return record_location(context, actor)


def build_external_rt_zone(context, layout):
    folder = STAGE_FOLDER + "/Zone0_ExternalRt"
    actor = spawn_live_mud(context, context["external_preset"], context["external_effective"], layout["external_mud"], "OpsMudExternal", folder)
    return {"label": actor.get_actor_label(), "preset": context["external_details"]["asset"], "render_targets": context["external_details"]["external_output_targets"]}


def build_cap_row_zone(context, layout):
    folder = STAGE_FOLDER + "/Zone1a_CapRow"
    labels = [spawn_torch(context, location, f"OpsTorchRow{index}", folder, False).get_actor_label() for index, location in enumerate(layout["cap_row"])]
    return {"labels": labels, "uses_activation_volume": False, "preset": context["torch_path"]}


def build_lod_row_zone(context, layout):
    folder = STAGE_FOLDER + "/Zone1b_LodRow"
    labels = []
    wake_distance = None
    for index, location in enumerate(layout["lod_row"]):
        torch = spawn_torch(context, location, f"OpsTorchLod{index}", folder, True)
        wake_distance = float(torch.get_editor_property("activation_distance_cm"))
        labels.append(torch.get_actor_label())
    asleep_from_origin = [label for label, location in zip(labels, layout["lod_row"]) if abs(location[0] - LOD_ROW_ORIGIN[0]) > wake_distance]
    return {"labels": labels, "uses_activation_volume": True, "activation_distance_cm": wake_distance, "spacing_cm": LOD_ROW_SPACING_CM,
            "expected_asleep_when_pawn_at_row_origin": asleep_from_origin, "preset": context["torch_path"]}


def build_camera_scene_zone(context, layout):
    folder = STAGE_FOLDER + "/Zone1c_CameraScene"
    mark = spawn_marker(context, context["assets"]["sphere"], layout["camera_mark"], CAMERA_MARK_SCALE, "OpsCameraMark", folder)
    behind = spawn_torch(context, layout["behind_torch"], "OpsTorchBehind", folder, False)
    wake_distance = float(behind.get_editor_property("activation_distance_cm"))
    if layout["behind_to_mark_planar_cm"] > wake_distance:
        raise RuntimeError(f"등 뒤 횃불이 깨움 거리 {wake_distance} 밖에 있음: {layout['behind_to_mark_planar_cm']}")
    return {"mark": mark.get_actor_label(), "behind_torch": behind.get_actor_label(), "camera_plan": dict(CAMERA_PLAN),
            "expected_camera_location_cm": list(layout["expected_camera"]), "behind_torch_outside_view": layout["behind_is_outside_view"],
            "behind_to_mark_planar_cm": layout["behind_to_mark_planar_cm"], "activation_distance_cm": wake_distance, "uses_activation_volume": False}


def add_transform_key(channel, frame, value):
    time = unreal.FrameNumber(frame)
    interpolation = getattr(getattr(unreal, "MovieSceneKeyInterpolation", None), "LINEAR", None)
    time_unit = getattr(getattr(unreal, "SequenceTimeUnit", None), "DISPLAY_RATE", None)
    if interpolation is not None and time_unit is not None:
        channel.add_key(time, value, 0.0, time_unit, interpolation)
        return "linear"
    channel.add_key(time, value)
    return "default"


def key_cube_transform(section, end_frame, start_location, end_location):
    channels = list(section.get_all_channels())
    if len(channels) < 9:
        raise RuntimeError(f"변환 섹션 채널 {len(channels)}개(9개 필요)")
    scale = SEQUENCER_CUBE_SCALE
    start_values = list(start_location) + [0.0, 0.0, 0.0, scale, scale, scale]
    end_values = list(end_location) + [0.0, 0.0, 0.0, scale, scale, scale]
    interpolation = "default"
    for channel, start_value, end_value in zip(channels[:9], start_values, end_values):
        add_transform_key(channel, 0, start_value)
        interpolation = add_transform_key(channel, end_frame, end_value)
    keys = list(channels[0].get_keys())
    if len(keys) != 2 or abs(keys[0].get_value() - start_location[0]) > 1e-3 or abs(keys[1].get_value() - end_location[0]) > 1e-3:
        raise RuntimeError(f"변환 키 다시 읽기 불일치: 키 {len(keys)}개")
    return interpolation


def load_or_create_sequence(particle_stage):
    path = f"{SEQUENCE_DIR}/{SEQUENCE_NAME}"
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(path):
        sequence = assets.load_asset(path)
        if not isinstance(sequence, unreal.LevelSequence):
            raise RuntimeError(f"기존 레벨 시퀀스 형식 불일치: {path}")
        for binding in list(sequence.get_bindings()):
            binding.remove()
        if list(sequence.get_bindings()):
            raise RuntimeError(f"기존 레벨 시퀀스 바인딩 제거 확인 실패: {path}")
        return sequence, path, False
    particle_stage.ensure_directory(SEQUENCE_DIR)
    sequence = unreal.AssetToolsHelpers.get_asset_tools().create_asset(SEQUENCE_NAME, SEQUENCE_DIR, unreal.LevelSequence, unreal.LevelSequenceFactoryNew())
    if sequence is None:
        raise RuntimeError(f"레벨 시퀀스 생성 실패: {path}")
    return sequence, path, True


def configure_sequence_player(sequence_actor, sequence):
    sequence_actor.set_sequence(sequence)
    settings = sequence_actor.get_editor_property("playback_settings")
    settings.set_editor_property("auto_play", True)
    loop_count = settings.get_editor_property("loop_count")
    loop_count.set_editor_property("value", -1)
    settings.set_editor_property("loop_count", loop_count)
    sequence_actor.set_editor_property("playback_settings", settings)
    stored = sequence_actor.get_editor_property("playback_settings")
    if not stored.get_editor_property("auto_play") or int(stored.get_editor_property("loop_count").get_editor_property("value")) != -1:
        raise RuntimeError("시퀀스 재생 설정(자동 재생·무한 반복) 다시 읽기 불일치")
    if sequence_actor.get_sequence() != sequence:
        raise RuntimeError("시퀀스 지정 확인 실패")


def build_sequencer_zone(context, layout):
    particle_stage = context["particle_stage"]
    folder = STAGE_FOLDER + "/Zone2_Sequencer"
    mud = spawn_live_mud(context, context["source_preset"], context["source_effective"], layout["sequencer_mud"], "OpsSequencerMud", folder)
    cube = spawn_marker(context, context["assets"]["cube"], layout["sequencer_cube_start"], SEQUENCER_CUBE_SCALE, "OpsSequencerCube", folder)
    cube_component = cube.static_mesh_component
    cube_component.set_collision_profile_name(SEQUENCER_COLLISION_PROFILE)
    cube_component.set_editor_property("generate_overlap_events", True)
    if str(cube_component.get_collision_profile_name()) != SEQUENCER_COLLISION_PROFILE:
        raise RuntimeError("시퀀서 큐브 충돌 프로파일 다시 읽기 불일치")
    sequence, sequence_path, sequence_created = load_or_create_sequence(particle_stage)
    context["sequence_path"] = sequence_path
    frame_rate = sequence.get_display_rate()
    end_frame = seconds_to_display_frame(SEQUENCER_CROSS_SECONDS, frame_rate.numerator, frame_rate.denominator)
    binding = sequence.add_possessable(cube)
    transform_track = binding.add_track(unreal.MovieScene3DTransformTrack)
    section = transform_track.add_section()
    section.set_range(0, end_frame)
    sequence.set_playback_start(0)
    sequence.set_playback_end(end_frame)
    interpolation = key_cube_transform(section, end_frame, layout["sequencer_cube_start"], layout["sequencer_cube_end"])
    player = context["actors"].spawn_actor_from_class(unreal.LevelSequenceActor, unreal.Vector(*layout["sequencer_mud"]), unreal.Rotator())
    particle_stage.finish_actor(player, "OpsSequencerPlayer", folder, context["created"])
    configure_sequence_player(player, sequence)
    particle_stage.save_asset(sequence, sequence_path)
    return {"mud": mud.get_actor_label(), "cube": cube.get_actor_label(), "player": player.get_actor_label(), "sequence": sequence_path,
            "sequence_created": sequence_created, "cross_seconds": SEQUENCER_CROSS_SECONDS, "display_rate": [frame_rate.numerator, frame_rate.denominator],
            "end_frame": end_frame, "interpolation": interpolation, "start_cm": list(layout["sequencer_cube_start"]), "end_cm": list(layout["sequencer_cube_end"]),
            "collision_profile": SEQUENCER_COLLISION_PROFILE, "preset": context["source_path"]}


def build_sequencer_zone_or_skip(context, layout):
    missing = context["particle_stage"].find_missing_unreal_names(SEQUENCER_UNREAL_NAMES)
    if missing:
        return None, "언리얼 Python 이름 없음: " + ", ".join(missing)
    created_before = len(context["created"])
    try:
        return build_sequencer_zone(context, layout), None
    except Exception as error:
        for actor in context["created"][created_before:]:
            context["labels"].pop(actor.get_actor_label(), None)
            context["actors"].destroy_actor(actor)
        del context["created"][created_before:]
        leftover = context.get("sequence_path")
        return None, f"{type(error).__name__}: {error}" + (f" (남은 에셋 가능: {leftover})" if leftover else "")


def load_stage_assets(fire_stage):
    assets = unreal.EditorAssetLibrary
    paths = {"cube": fire_stage.CUBE_MESH_PATH, "sphere": fire_stage.SPHERE_MESH_PATH, "ember_material": fire_stage.EMBER_INSTANCE_PATH}
    loaded = {name: assets.load_asset(path) if assets.does_asset_exist(path) else None for name, path in paths.items()}
    missing = [paths[name] for name, value in loaded.items() if value is None]
    if missing:
        raise RuntimeError("필수 에셋 없음(editor_make_fluid_fire_stage.py 먼저): " + ", ".join(missing))
    loaded["floor_material"] = assets.load_asset(fire_stage.FLOOR_INSTANCE_PATH) if assets.does_asset_exist(fire_stage.FLOOR_INSTANCE_PATH) else None
    return loaded


def load_required_preset(particle_stage, name):
    path = f"{particle_stage.PRESET_DIR}/DA_TDFluid_{name}"
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        raise RuntimeError(f"원본 프리셋 없음: {path} (해당 무대 스크립트 먼저)")
    preset = unreal.EditorAssetLibrary.load_asset(path)
    if not isinstance(preset, unreal.TDFluidPresetAsset):
        raise RuntimeError(f"프리셋 형식 불일치: {path}")
    return preset, path, preset.resolve_effective_config()


def check_contracts(particle_stage, fire_stage):
    problems = list(fire_stage.find_missing_unreal_names()) + list(fire_stage.check_cpp_contract())
    problems += particle_stage.find_missing_properties(unreal.TDFluidPresetAsset.get_default_object(), particle_stage.PRESET_REQUIRED_PROPERTIES + EXTRA_PRESET_PROPERTIES)
    problems += particle_stage.find_missing_properties(unreal.TDFluidExternalOutputTargets(), EXTERNAL_TARGET_PROPERTIES)
    live_actor_class = unreal.load_class(None, particle_stage.FLUID_ACTOR_CLASS_PATH)
    fire_actor_class = unreal.load_class(None, fire_stage.CLASS_PATHS["TDFluidFireActor"])
    if live_actor_class is None or fire_actor_class is None:
        raise RuntimeError("유체·불 액터 클래스 없음(재빌드와 에디터 재시작 필요)")
    problems += particle_stage.find_missing_properties(unreal.get_default_object(live_actor_class), particle_stage.ACTOR_REQUIRED_PROPERTIES + EXTRA_ACTOR_PROPERTIES)
    if problems:
        raise RuntimeError("C++ 계약 이름 없음(FX-16a 묶음 A·B 빌드와 에디터 재시작 필요): " + ", ".join(problems))
    return live_actor_class, fire_actor_class


def main():
    layout = build_layout()
    if not layout["behind_is_outside_view"]:
        raise RuntimeError("등 뒤 횃불이 카메라 앞쪽에 있음: 배치 상수 점검")
    particle_stage = load_module("td_fluid_particle_stage", "Tools", "FluidFX", "editor_make_fluid_particle_stage.py")
    fire_stage = load_module("td_fluid_fire_stage", "Tools", "FluidFX", "editor_make_fluid_fire_stage.py")
    editor = particle_stage.require_editor_ready(tuple(particle_stage.REQUIRED_UNREAL_NAMES) + EXTRA_REQUIRED_UNREAL_NAMES)
    live_actor_class, fire_actor_class = check_contracts(particle_stage, fire_stage)
    overwrite = particle_stage.read_overwrite_request(REQUEST_RELATIVE_PATH)
    render_target_paths = [f"{TEXTURE_DIR}/{spec['name']}" for spec in RENDER_TARGET_SPECS]
    external_preset_path = f"{particle_stage.PRESET_DIR}/DA_TDFluid_{EXTERNAL_PRESET_NAME}"
    particle_stage.guard_existing_assets(render_target_paths + [external_preset_path, f"{SEQUENCE_DIR}/{SEQUENCE_NAME}", STAGE_MAP_PATH], overwrite, REQUEST_RELATIVE_PATH)
    print("단계: 프리셋·모양 정본 검사·다시 읽기")
    tuning_sha256, look_sha256 = particle_stage.check_and_reload_preset_data()
    source_preset, source_path, source_effective = load_required_preset(particle_stage, SOURCE_PRESET_NAME)
    torch_preset, torch_path, torch_effective = load_required_preset(particle_stage, TORCH_PRESET_NAME)
    assets_by_name = load_stage_assets(fire_stage)

    print("단계: 외부 렌더 타깃 2개")
    render_targets = {}
    render_target_details = {}
    render_target_size = int(source_effective.get_editor_property("grid_resolution"))
    for spec in RENDER_TARGET_SPECS:
        render_targets[spec["name"]], render_target_details[spec["name"]] = load_or_create_render_target(particle_stage, spec, render_target_size)

    print("단계: 외부 렌더 타깃 Mud 프리셋")
    external_preset, external_effective, external_details = build_external_preset(particle_stage, source_preset, render_targets)

    print("단계: 레벨")
    levels, actors = particle_stage.open_empty_level(STAGE_MAP_PATH)
    world = editor.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", unreal.EditorAssetLibrary.load_blueprint_class(fire_stage.GAME_MODE_PATH))
    created = []
    floor_center = ((FLOOR_X_RANGE_CM[0] + FLOOR_X_RANGE_CM[1]) * 0.5, (FLOOR_Y_RANGE_CM[0] + FLOOR_Y_RANGE_CM[1]) * 0.5)
    floor_scale = ((FLOOR_X_RANGE_CM[1] - FLOOR_X_RANGE_CM[0]) / BASIC_SHAPE_SIZE_CM, (FLOOR_Y_RANGE_CM[1] - FLOOR_Y_RANGE_CM[0]) / BASIC_SHAPE_SIZE_CM, FLOOR_THICKNESS_SCALE)
    particle_stage.place_floor_and_lights(actors, created, STAGE_FOLDER, floor_center, floor_scale, assets_by_name["floor_material"])
    context = {"particle_stage": particle_stage, "fire_stage": fire_stage, "actors": actors, "created": created, "assets": assets_by_name,
               "live_actor_class": live_actor_class, "fire_actor_class": fire_actor_class, "labels": {},
               "source_preset": source_preset, "source_path": source_path, "source_effective": source_effective,
               "external_preset": external_preset, "external_effective": external_effective, "external_details": external_details,
               "torch_preset": torch_preset, "torch_path": torch_path, "torch_effective": torch_effective,
               "torch_actor_spec": fire_stage.FIRE_ACTOR_SPECS[TORCH_ACTOR_SPEC_NAME]}
    zones = {}
    print("단계: 구역 0 외부 렌더 타깃")
    zones["external_rt"] = build_external_rt_zone(context, layout)
    print("단계: 구역 1a 처리 주기 상한 비용 횃불")
    zones["cap_row"] = build_cap_row_zone(context, layout)
    print("단계: 구역 1b 거리 곡선 횃불")
    zones["lod_row"] = build_lod_row_zone(context, layout)
    print("단계: 구역 1c 카메라 표지와 등 뒤 횃불")
    zones["camera_scene"] = build_camera_scene_zone(context, layout)
    print("단계: 구역 2 시퀀서 큐브")
    sequencer_record, skip_reason = build_sequencer_zone_or_skip(context, layout)
    skipped_zones = []
    if sequencer_record is None:
        skipped_zones.append({"zone": "sequencer", "reason": skip_reason})
        print("구역 2 건너뜀: " + skip_reason)
    else:
        zones["sequencer"] = sequencer_record

    print("단계: 레벨 저장")
    if not levels.save_current_level():
        raise RuntimeError(f"무대 레벨 저장 실패: {STAGE_MAP_PATH}")
    saved_asset_paths = render_target_paths + [external_preset_path] + ([sequencer_record["sequence"]] if sequencer_record else [])
    particle_stage.require_saved_files(saved_asset_paths, STAGE_MAP_PATH)
    report = {"preset_tuning_sha256": tuning_sha256, "material_look_sha256": look_sha256, "map": STAGE_MAP_PATH, "overwrite": overwrite,
              "render_targets": render_target_details, "external_preset": external_details, "torch_preset": torch_path,
              "zones": zones, "skipped_zones": skipped_zones, "labels": context["labels"],
              "camera_plan": dict(CAMERA_PLAN), "created_actors": len(created), "saved": True}
    report_path = particle_stage.write_report(report, REPORT_RELATIVE_PATH)
    print(json.dumps(report, ensure_ascii=False, default=str))
    print("보고서: " + report_path)


if __name__ == "__main__":
    main()
