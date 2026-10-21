"""실행 환경: 시스템 Python(언리얼 에디터가 TDGame을 열고 MCP 포트 8000·원격 실행이 켜진 상태, 에디터 앞창 유지 L-editor-06). FluidFX 무대 PIE 하나에서 변형(노출·조명·불빛·불꽃 카드/볼륨 재질 값·활성 프리셋 임시 복제본·폰 위치·카메라 거리·액터 위치·컴포넌트 숨김·콘솔 명령·깨어 있는 유체 액터·폭발 트리거)을 차례로 적용하고 정착 시간 뒤 또는 게임 시간 예약 시각마다 게임 화면을 찍어 이름표 붙은 비교 격자를 만든다.
실행 명령: python Tools/FluidFX/pie_lookdev_fluid.py --variants <변형 JSON> [--level /Game/FluidFX/Maps/LV_TDFluidFireStage] [--settle-seconds 3] [--stop-pie] (PowerShell). 변형 JSON = {"base": {적용}, "variants": [{"name": "영문", "apply": {적용}, "settle": 초}]}, 적용 키(이 순서로 적용) = awake({라벨: bool}이면 적힌 것만, 목록이면 목록 외는 재움)·pawn·arm·view([pitch, yaw] 조종 회전, 붐이 따라감)·props({라벨: {속성: 값}} 액터 속성. 값이 dict면 현재 값이 구조체일 때 필드를 고쳐 되넣고, UObject(컴포넌트)면 그 객체의 필드만 고치고 되넣지 않으며, 배열이면 정수 문자열 키의 원소를 고쳐 배열을 되넣는다 예 {"targets": {"1": {"burst_radius_scale": 1.5}}})·hide({라벨: bool} 게임 숨김)·location({라벨: [x, y, z]} 액터 위치, null 성분은 현재 값 유지 예 [null, null, 0.0], 다시 읽은 값 기록)·hide_components({라벨 또는 "pawn": [컴포넌트 이름 일부(대소문자 무시)]} 맞는 씬 컴포넌트 set_visibility(False), 예 ShieldMesh)·preset({라벨: {점 경로 또는 중첩 dict: 값}} 라이브 컴포넌트 활성 프리셋의 임시 복제본을 고쳐 active_preset에 넣음, 원본 에셋은 건드리지 않음. plane_settings·volume_render·volume_height_cm은 C++가 매 스텝 다시 읽어 그대로 반영되고, 반영 여부는 다음 촬영의 preset_effect(볼륨 MID 매개변수 값, camera_depth_bias_cm은 내부 표면 메시 이동 거리)로 보고)·preset_reinit([라벨] 복제본으로 init_system_state_from_preset 재초기화, 시뮬 상태와 앞선 sim 편집이 초기화되므로 프리셋의 시뮬 설정·출력 재질처럼 초기화 때만 읽는 필드를 바꿀 때만)·exposure_ev100·lights·fire_light·flame(액터의 모든 불꽃 카드 MID)·volume(볼륨 MID)·ember({라벨: {재질 매개변수: 값}} ember_component 0번 재질 사슬에 FluidEmber 필요, MID 아니면 만들어 설정, 부모에 없는 이름은 missing, settle 촬영마다 result.json에 ember_mid)·sim(simulation_config 필드, apply_simulation_config)·emitter(source_emitter 필드)·surface_material(라이브 컴포넌트 출력 재질 교체 후 내부 메시 재연결)·console([명령] PIE 플레이어 컨트롤러로 실행, "이름 값" 꼴 cvar는 다시 읽은 값 기록, Shot 결과는 result.json console_screenshots)·move(내비 이동 목표)·speed·trigger(같은 호출의 게임 시간을 trigger_game_time으로 기록)·time_dilation(전역 시간 배율)·schedule({"times": [기준 뒤 게임 초], "reference": "trigger"|"now", "prefix": "영문", "fixed_fps": 60(0이면 끔, 예약 동안 t.OverrideFPS 고정 간격 뒤 복구 L-editor-45), "file_wait_ticks": 30(1이면 그려진 다음 틱에 바로 다음 장), "timeout": 초} 원격 호출 하나에서 슬레이트 틱 콜백을 걸어 기준 뒤 게임 시간마다 HighResShot을 요청하고, 앞 장이 그려지고 파일이 생기거나 틱 한도가 지나야 다음 장을 건 뒤 스스로 해제, 이 변형은 settle 촬영을 하지 않음, reference trigger는 같은 apply의 trigger 필요). props·emitter·sim·preset 값 변환: 현재 값이 Vector·Vector2D면 목록 [x, y(, z)], Rotator면 [pitch, yaw, roll], LinearColor면 [r, g, b(, a)], Transform이면 {"location": [], "rotation": [pitch, yaw, roll], "scale": []}(빠진 키는 현재 값 유지)
출력: Saved/FluidFX/lookdev/<시각>/<순번>_<이름>.png, 예약 촬영은 <순번>_<prefix>_<기준 뒤 ms>ms.png와 <순번>_<prefix>_schedule.json(장마다 요청·실제 찍힌 게임 시간, 늦음, 틱 사이 벽시계 간격 real_dt(요청 틱)·real_dt_mean(앞 장 뒤 촬영 대기 아닌 틱 평균, 실제 fps 대용)·capture_real_dt(촬영 끊김 포함), world_dt, 파일 대기 틱, 고정 간격 복구, preset_effect), grid.png, result.json(변형별 적용 결과·shot_game_time·preset_effect·schedule·console_screenshots). 종료 코드 0 기대한 장 전부 찍음/1 일부 실패/2 환경 문제
상태: 검증됨(2026-10-18 모닥불·횃불·폭발 변형 9회 실행, campfire_v1~v5·torch_v1·explosion_v1. 2026-10-20 view·hide·props 추가와 flame 전 카드 적용으로 측면 룩데브 6회 실행). 2026-10-20 추가한 awake dict·game_time·time_dilation과 console·schedule·location·hide_components·preset·preset_reinit·값 변환·배열 원소·UObject 필드 편집은 미검증(PIE 미실행, 정적 검토만). PIE 런타임 값만 바꾸므로 에셋은 바뀌지 않는다. 고른 값은 재질·무대 생성 스크립트에 옮겨 적는다. 2026-10-21 FX-8c 11차 ember 키는 미검증(PIE 미실행), EmberTint* 는 재질 재생성 뒤에만 효과.
"""
import argparse
import json
import sys
import time
from datetime import datetime
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools"))
sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))
import pie_check_fluid  # noqa: E402
import pie_showcase_fluid  # noqa: E402
import run_in_editor  # noqa: E402
import uemcp  # noqa: E402

RUN_TOKEN = datetime.now().strftime("%Y%m%d-%H%M%S")
OUTPUT_DIRECTORY = ROOT / "Saved" / "FluidFX" / "lookdev" / RUN_TOKEN
SCREENSHOT_DIRECTORY = ROOT / "Saved" / "Screenshots" / "WindowsEditor"
DEFAULT_LEVEL_PATH = "/Game/FluidFX/Maps/LV_TDFluidFireStage"
SHOT_SIZE = "1280x720"
GRID_TILE_WIDTH = 640
GRID_COLUMNS = 2
FRAME_FILE_WAIT_SECONDS = 10.0
SCHEDULE_POLL_SECONDS = 0.5
SCHEDULE_POLL_MARGIN_SECONDS = 15.0
EXIT_PARTIAL = 1
EXIT_ENVIRONMENT = 2

LOOKDEV_ACTIONS = r'''
import inspect
import os
import time

SCHEDULE_ATTRIBUTE = "td_lookdev_schedule"
PRESET_CHECK_ATTRIBUTE = "td_lookdev_preset_checks"
FIXED_STEP_VARIABLE = "t.OverrideFPS"
DEFAULT_SCHEDULE_FIXED_FPS = 60.0
DEFAULT_SCHEDULE_FILE_WAIT_TICKS = 30
PRESET_FIELD_NAMES = ("preset_kind", "override_preset_defaults", "simulation_config", "default_channel", "output_material", "surface_mesh", "external_niagara_system", "parameter_collection", "external_renderer_actor_tag", "volume_material", "volume_mesh", "volume_height_cm", "plane_settings", "translucency_sort_priority", "volume_render")
VOLUME_PARAMETER_NAMES = {"volume_size_multiplier": "VolumeBoxScale"}
TRANSFORM_KEYS = {"location", "rotation", "scale"}


def find_actor_by_label(world, label):
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
        if actor.get_actor_label() == label:
            return actor
    return None


def set_material_values(material, values):
    applied = {}
    if not isinstance(material, unreal.MaterialInstanceDynamic):
        return {"error": "MID 아님: " + str(material)}
    for name, value in values.items():
        if isinstance(value, list):
            material.set_vector_parameter_value(name, unreal.LinearColor(*(value + [1.0] * (4 - len(value)))))
        else:
            material.set_scalar_parameter_value(name, float(value))
        applied[name] = value
    return applied


def apply_awake(world, requested):
    states = {}
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDFluidLiveActor):
        label = actor.get_actor_label()
        if isinstance(requested, dict) and label not in requested:
            continue
        is_awake = bool(requested[label]) if isinstance(requested, dict) else label in requested
        actor.get_editor_property("live_component").set_simulation_awake(is_awake)
        states[label] = is_awake
    return states


def apply_exposure(world, ev100):
    changed = []
    for volume in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PostProcessVolume):
        settings = volume.get_editor_property("settings")
        settings.set_editor_property("override_auto_exposure_min_brightness", True)
        settings.set_editor_property("override_auto_exposure_max_brightness", True)
        settings.set_editor_property("auto_exposure_min_brightness", ev100)
        settings.set_editor_property("auto_exposure_max_brightness", ev100)
        volume.set_editor_property("settings", settings)
        changed.append(volume.get_actor_label())
    return changed


def apply_component_values(world, mapping, component_property, apply_values):
    outcome = {}
    for label, values in mapping.items():
        actor = find_actor_by_label(world, label)
        if actor is None:
            outcome[label] = {"error": "액터 없음"}
            continue
        component = actor.get_editor_property(component_property)
        outcome[label] = apply_values(component, values)
    return outcome


def set_properties(component, values):
    for name, value in values.items():
        component.set_editor_property(name, value)
    return values


def is_flame_card_material(material):
    while material is not None:
        if "FluidFlame" in material.get_name():
            return True
        material = material.get_editor_property("parent") if isinstance(material, unreal.MaterialInstance) else None
    return False


def set_mesh_material_values(component, values):
    if component is None:
        return {"error": "컴포넌트 없음"}
    return set_material_values(component.get_material(0), values)


def has_material_in_chain(material, token):
    while material is not None:
        if token in material.get_name():
            return True
        material = material.get_editor_property("parent") if isinstance(material, unreal.MaterialInstance) else None
    return False


def set_ember_material_values(component, values):
    if component is None:
        return {"error": "컴포넌트 없음"}
    material = component.get_material(0)
    if not has_material_in_chain(material, "FluidEmber"):
        return {"error": "불티 재질 미로드: " + str(material)}
    if not isinstance(material, unreal.MaterialInstanceDynamic):
        material = component.create_dynamic_material_instance(0, material)
    applied = set_material_values(material, values)
    if "error" in applied:
        return applied
    base = material.get_base_material()
    known = {str(name) for name in unreal.MaterialEditingLibrary.get_scalar_parameter_names(base)} | {str(name) for name in unreal.MaterialEditingLibrary.get_vector_parameter_names(base)}
    return {"applied": applied, "mid": material.get_name(), "missing": [name for name in values if name not in known]}


def describe_ember_materials(world):
    names = {}
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDFluidFireActor):
        component = actor.get_editor_property("ember_component")
        if component is not None and component.get_material(0) is not None:
            names[actor.get_actor_label()] = component.get_material(0).get_name()
    return names


def make_rotator(value):
    padded = list(value) + [0.0] * (3 - len(value))
    return unreal.Rotator(roll=padded[2], pitch=padded[0], yaw=padded[1])


def make_transform(current, value):
    unknown = set(value) - TRANSFORM_KEYS
    if unknown:
        raise ValueError("Transform 키는 location·rotation·scale 뿐이다: " + ", ".join(sorted(unknown)))
    location = unreal.Vector(*value["location"]) if "location" in value else current.translation
    rotation = make_rotator(value["rotation"]) if "rotation" in value else current.rotation.rotator()
    scale = unreal.Vector(*value["scale"]) if "scale" in value else current.scale3d
    return unreal.Transform(location=location, rotation=rotation, scale=scale)


def convert_list(current, value):
    if isinstance(current, unreal.Vector):
        return unreal.Vector(*value)
    if isinstance(current, unreal.Vector2D):
        return unreal.Vector2D(*value)
    if isinstance(current, unreal.Rotator):
        return make_rotator(value)
    if isinstance(current, unreal.LinearColor):
        return unreal.LinearColor(*(list(value) + [1.0] * (4 - len(value))))
    return value


def resolve_value(current, value):
    if isinstance(value, list):
        return convert_list(current, value)
    if not isinstance(value, dict):
        return value
    if isinstance(current, unreal.Transform):
        return make_transform(current, value)
    if isinstance(current, unreal.Array):
        for key, item_value in value.items():
            current[int(key)] = resolve_value(current[int(key)], item_value)
        return current
    if current is None:
        raise ValueError("현재 값이 None이라 필드를 고칠 수 없다")
    for field, field_value in value.items():
        assign_field(current, field, field_value)
    return current


def assign_field(owner, name, value):
    current = owner.get_editor_property(name)
    resolved = resolve_value(current, value)
    if isinstance(current, unreal.Object) and isinstance(value, dict):
        return
    owner.set_editor_property(name, resolved)


def apply_each_label(world, apply, outcome, key, handler, *extra):
    if key not in apply:
        return
    results = outcome.setdefault(key, {})
    for label, values in apply[key].items():
        try:
            results[label] = handler(world, label, values, *extra)
        except Exception as error:
            results[label] = {"error": str(error)[-300:]}


def apply_props(world, label, properties):
    actor = find_actor_by_label(world, label)
    if actor is None:
        return "액터 없음"
    for name, value in properties.items():
        assign_field(actor, name, value)
    return list(properties)


def apply_location(world, label, coordinates):
    actor = find_actor_by_label(world, label)
    if actor is None:
        return "액터 없음"
    current = actor.get_actor_location()
    target = [old if new is None else new for old, new in zip((current.x, current.y, current.z), coordinates)]
    actor.set_actor_location(unreal.Vector(*target), False, True)
    location = actor.get_actor_location()
    return [location.x, location.y, location.z]


def apply_hidden_components(world, label, name_parts):
    actor = unreal.GameplayStatics.get_player_pawn(world, 0) if label == "pawn" else find_actor_by_label(world, label)
    if actor is None:
        return "액터 없음"
    lowered_parts = [part.lower() for part in name_parts]
    hidden = []
    for component in actor.get_components_by_class(unreal.SceneComponent):
        if any(part in component.get_name().lower() for part in lowered_parts):
            component.set_visibility(False)
            hidden.append(component.get_name())
    return hidden


def apply_sim(world, label, values):
    actor = find_actor_by_label(world, label)
    if actor is None:
        return "액터 없음"
    component = actor.get_editor_property("live_component")
    config = component.get_editor_property("simulation_config")
    for name, value in values.items():
        assign_field(config, name, value)
    component.apply_simulation_config(config)
    return values


def apply_emitter(world, label, values):
    actor = find_actor_by_label(world, label)
    if actor is None:
        return "액터 없음"
    emitter = actor.get_editor_property("source_emitter")
    for name, value in values.items():
        assign_field(emitter, name, value)
    actor.set_editor_property("source_emitter", emitter)
    return values


def flatten_paths(values, prefix=""):
    paths = {}
    for name, value in values.items():
        if isinstance(value, dict):
            paths.update(flatten_paths(value, prefix + name + "."))
        else:
            paths[prefix + name] = value
    return paths


def read_path(owner, path):
    for part in path.split("."):
        owner = owner.get_editor_property(part)
    return owner


def assign_path(owner, parts, value):
    if len(parts) == 1:
        assign_field(owner, parts[0], value)
        return
    child = owner.get_editor_property(parts[0])
    assign_path(child, parts[1:], value)
    if not isinstance(child, unreal.Object):
        owner.set_editor_property(parts[0], child)


def clone_preset(source):
    clone = unreal.new_object(type(source))
    discovered = {name for name, member in inspect.getmembers(type(source)) if inspect.isgetsetdescriptor(member) and not name.startswith("_")}
    failures = {}
    for name in sorted(set(PRESET_FIELD_NAMES) | discovered):
        try:
            clone.set_editor_property(name, source.get_editor_property(name))
        except Exception as error:
            failures[name] = str(error)[-160:]
    return clone, failures


def surface_location(actor):
    surface = actor.get_editor_property("internal_surface_mesh")
    if surface is None:
        return None
    location = surface.get_world_location()
    return [location.x, location.y, location.z]


def volume_expectations(paths):
    expectations = {}
    for path, value in paths.items():
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            continue
        if path == "volume_height_cm":
            expectations[path] = ["VolumeHeightCm", float(value)]
        elif path.startswith("volume_render.") and path.count(".") == 1:
            leaf = path.split(".")[1]
            expectations[path] = [VOLUME_PARAMETER_NAMES.get(leaf, "".join(part.capitalize() for part in leaf.split("_"))), float(value)]
    return expectations


def apply_preset(world, label, edits, reinitialized_labels):
    actor = find_actor_by_label(world, label)
    if actor is None:
        return "액터 없음"
    live = actor.get_editor_property("live_component")
    source = live.get_editor_property("active_preset")
    if source is None:
        return {"error": "활성 프리셋 없음"}
    paths = flatten_paths(edits)
    previous = {path: read_path(source, path) for path in paths}
    clone, copy_failures = clone_preset(source)
    for path, value in paths.items():
        assign_path(clone, path.split("."), value)
    bias_path = "plane_settings.camera_depth_bias_cm"
    check = {"clone": clone.get_name(), "volume": volume_expectations(paths), "surface_location": surface_location(actor),
             "depth_bias_shift_cm": abs(float(paths[bias_path]) - float(previous[bias_path])) if bias_path in paths else None}
    live.set_editor_property("active_preset", clone)
    should_reinitialize = label in reinitialized_labels
    if should_reinitialize:
        live.init_system_state_from_preset(clone)
    pending = getattr(unreal, PRESET_CHECK_ATTRIBUTE, None) or {}
    pending[label] = check
    setattr(unreal, PRESET_CHECK_ATTRIBUTE, pending)
    active = live.get_editor_property("active_preset")
    return {"source": source.get_path_name(), "clone": clone.get_name(), "assigned": active is not None and active.get_name() == clone.get_name(),
            "values": {path: {"before": previous[path], "after": read_path(clone, path)} for path in paths},
            "reinitialized": should_reinitialize, "copy_failures": copy_failures}


def measure_preset_effect(actor, check):
    active = actor.get_editor_property("live_component").get_editor_property("active_preset")
    effect = {"active_is_clone": active is not None and active.get_name() == check["clone"]}
    volume = actor.get_editor_property("volume_mesh_component") if check["volume"] else None
    material = volume.get_material(0) if volume is not None else None
    names = {str(name) for name in unreal.MaterialEditingLibrary.get_scalar_parameter_names(material)} if material is not None else set()
    missing_reason = "볼륨 메시 재질 없음" if material is None else "볼륨 재질에 이 매개변수가 없다"
    for path, (parameter, expected) in check["volume"].items():
        if parameter not in names:
            effect[path] = {"parameter": parameter, "took_effect": None, "reason": missing_reason}
            continue
        actual = material.get_scalar_parameter_value(parameter)
        effect[path] = {"parameter": parameter, "expected": expected, "actual": actual, "took_effect": abs(actual - expected) <= 0.001 * max(1.0, abs(expected))}
    expected_shift = check["depth_bias_shift_cm"]
    if expected_shift is not None:
        before, after = check["surface_location"], surface_location(actor)
        shift = math.dist(before, after) if before is not None and after is not None else None
        effect["plane_settings.camera_depth_bias_cm"] = {"expected_shift_cm": expected_shift, "surface_shift_cm": shift,
                                                         "took_effect": None if shift is None else abs(shift - expected_shift) <= max(1.0, 0.25 * expected_shift)}
    return effect


def report_preset_effects(world):
    pending = getattr(unreal, PRESET_CHECK_ATTRIBUTE, None) or {}
    setattr(unreal, PRESET_CHECK_ATTRIBUTE, {})
    report = {}
    for label, check in pending.items():
        actor = find_actor_by_label(world, label)
        try:
            report[label] = measure_preset_effect(actor, check) if actor is not None else {"error": "액터 없음"}
        except Exception as error:
            report[label] = {"error": str(error)[-300:]}
    return report


def run_console_commands(world, commands):
    controller = unreal.GameplayStatics.get_player_controller(world, 0)
    executed = []
    for command in commands:
        unreal.SystemLibrary.execute_console_command(world, command, controller)
        parts = command.split()
        if len(parts) == 2 and "." in parts[0]:
            executed.append({"command": command, "read_back": unreal.SystemLibrary.get_console_variable_float_value(parts[0])})
        else:
            executed.append({"command": command})
    return executed


def set_console_float(name, value):
    world = game_world() or unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    unreal.SystemLibrary.execute_console_command(world, f"{name} {value:g}", None)
    return unreal.SystemLibrary.get_console_variable_float_value(name)


def enable_fixed_step(state, fps):
    if fps <= 0.0:
        return {"requested_fps": 0.0}
    state["fixed_step_before"] = unreal.SystemLibrary.get_console_variable_float_value(FIXED_STEP_VARIABLE)
    return {"requested_fps": fps, "before": state["fixed_step_before"], "applied_fps": set_console_float(FIXED_STEP_VARIABLE, fps)}


def restore_fixed_step(state):
    before = state.get("fixed_step_before")
    if before is None:
        return None
    state["fixed_step_before"] = None
    return set_console_float(FIXED_STEP_VARIABLE, before)


def write_schedule_status(state, attempts=1):
    for _ in range(attempts):
        try:
            with open(state["status_path"], "w", encoding="utf-8") as status_file:
                json.dump(state["status"], status_file, ensure_ascii=False, default=str)
            return True
        except OSError:
            time.sleep(0.05)
    return False


def finish_schedule(state, error):
    if getattr(unreal, SCHEDULE_ATTRIBUTE, None) is state:
        setattr(unreal, SCHEDULE_ATTRIBUTE, None)
    if state.get("handle") is not None:
        unreal.unregister_slate_post_tick_callback(state["handle"])
        state["handle"] = None
    status = state["status"]
    try:
        status["fixed_step"]["restored_fps"] = restore_fixed_step(state)
        world = game_world()
        status["preset_effect"] = report_preset_effects(world) if world is not None else {}
    except Exception as cleanup_error:
        status["cleanup_error"] = str(cleanup_error)[-300:]
    status["error"] = error
    status["done"] = True
    write_schedule_status(state, 5)
    return status


def stop_schedule(reason):
    state = getattr(unreal, SCHEDULE_ATTRIBUTE, None)
    if state is None:
        return None
    return finish_schedule(state, reason)


def issue_scheduled_shot(state, world, elapsed, real_time, delta_seconds):
    target = state["remaining"].pop(0)
    path = f"{state['output']}/{state['tag']}_{state['prefix']}_{round(target * 1000):05d}ms.png"
    controller = unreal.GameplayStatics.get_player_controller(world, 0)
    unreal.SystemLibrary.execute_console_command(world, "HighResShot " + state["size"] + " filename=" + path, controller)
    state["pending"] = {"time": target, "path": path, "issued_game_time": elapsed, "late_seconds": elapsed - target, "issued_real_time": real_time,
                        "issued_tick": state["ticks"], "real_dt": delta_seconds,
                        "real_dt_mean": state["real_dt_sum"] / state["real_dt_count"] if state["real_dt_count"] else None,
                        "world_dt": unreal.GameplayStatics.get_world_delta_seconds(world), "captured_game_time": None, "saved": False}
    state["real_dt_sum"], state["real_dt_count"] = 0.0, 0
    state["status"]["shots"].append(state["pending"])
    write_schedule_status(state)


def settle_pending_shot(state, pending, elapsed, real_time, delta_seconds):
    if pending["captured_game_time"] is None:
        if real_time <= pending["issued_real_time"]:
            return
        pending["captured_game_time"] = elapsed
        pending["capture_real_dt"] = delta_seconds
    waited_ticks = state["ticks"] - pending["issued_tick"]
    is_saved = os.path.exists(pending["path"]) and os.path.getsize(pending["path"]) > 0
    if not is_saved and waited_ticks < state["file_wait_ticks"]:
        return
    pending["saved"] = is_saved
    pending["wait_ticks"] = waited_ticks
    state["pending"] = None
    write_schedule_status(state)


def advance_schedule(state, slate_delta_seconds):
    world = game_world()
    if world is None:
        finish_schedule(state, "PIE 게임 월드가 사라졌다")
        return
    if time.monotonic() > state["deadline"]:
        finish_schedule(state, "에디터 쪽 시간 초과")
        return
    wall_time = time.perf_counter()
    wall_delta_seconds = wall_time - state["last_wall_time"] if state["last_wall_time"] is not None else slate_delta_seconds
    state["last_wall_time"] = wall_time
    state["ticks"] += 1
    elapsed = unreal.GameplayStatics.get_time_seconds(world) - state["reference"]
    real_time = unreal.GameplayStatics.get_real_time_seconds(world)
    if state["pending"] is not None:
        settle_pending_shot(state, state["pending"], elapsed, real_time, wall_delta_seconds)
        return
    if not state["remaining"]:
        finish_schedule(state, None)
        return
    state["real_dt_sum"] += wall_delta_seconds
    state["real_dt_count"] += 1
    if elapsed >= state["remaining"][0]:
        issue_scheduled_shot(state, world, elapsed, real_time, wall_delta_seconds)


def start_schedule(schedule, reference_time):
    stop_schedule("새 예약이 이전 예약을 대체했다")
    times = sorted(float(value) for value in schedule["times"])
    prefix = schedule.get("prefix", "schedule")
    timeout = float(schedule.get("timeout", 120.0 + 10.0 * max(times + [0.0])))
    state = {"remaining": list(times), "pending": None, "ticks": 0, "real_dt_sum": 0.0, "real_dt_count": 0, "last_wall_time": None, "handle": None,
             "file_wait_ticks": int(schedule.get("file_wait_ticks", DEFAULT_SCHEDULE_FILE_WAIT_TICKS)), "deadline": time.monotonic() + timeout,
             "reference": reference_time, "size": PARAMS["size"], "output": PARAMS["output"], "tag": PARAMS["tag"], "prefix": prefix,
             "status_path": f"{PARAMS['output']}/{PARAMS['tag']}_{prefix}_schedule.json",
             "status": {"times": times, "reference": schedule.get("reference", "now"), "reference_game_time": reference_time, "prefix": prefix, "shots": [], "done": False}}
    state["status"]["fixed_step"] = enable_fixed_step(state, float(schedule.get("fixed_fps", DEFAULT_SCHEDULE_FIXED_FPS)))
    write_schedule_status(state)

    def tick(delta_seconds):
        try:
            advance_schedule(state, delta_seconds)
        except Exception:
            finish_schedule(state, traceback.format_exc()[-600:])

    state["handle"] = unreal.register_slate_post_tick_callback(tick)
    setattr(unreal, SCHEDULE_ATTRIBUTE, state)
    return {"status_path": state["status_path"], "times": times, "reference_game_time": reference_time, "timeout_seconds": timeout, "fixed_step": state["status"]["fixed_step"]}


def begin_schedule(world, schedule, outcome):
    reference = schedule.get("reference", "now")
    if reference == "now":
        return start_schedule(schedule, unreal.GameplayStatics.get_time_seconds(world))
    if reference != "trigger":
        return {"error": "reference는 trigger 또는 now: " + str(reference)}
    trigger = outcome.get("trigger")
    if not isinstance(trigger, dict) or "error" in trigger or "trigger_game_time" not in outcome:
        return {"error": "reference trigger는 같은 apply에서 trigger가 성공해야 한다"}
    return start_schedule(schedule, outcome["trigger_game_time"])


def action_lookdev_apply(world):
    apply = PARAMS["apply"]
    outcome = {}
    if "awake" in apply:
        outcome["awake"] = apply_awake(world, apply["awake"] if isinstance(apply["awake"], dict) else set(apply["awake"]))
    if "pawn" in apply:
        teleport_pawn(world, apply["pawn"][0], apply["pawn"][1])
        outcome["pawn"] = apply["pawn"]
    if "arm" in apply:
        pawn, controller = player_pawn_and_controller(world)
        boom = pawn.get_component_by_class(unreal.SpringArmComponent)
        boom.set_editor_property("target_arm_length", apply["arm"])
        outcome["arm"] = boom.get_editor_property("target_arm_length")
    if "view" in apply:
        pawn, controller = player_pawn_and_controller(world)
        controller.set_control_rotation(unreal.Rotator(roll=0.0, pitch=apply["view"][0], yaw=apply["view"][1]))
        boom = pawn.get_component_by_class(unreal.SpringArmComponent)
        boom.set_editor_property("use_pawn_control_rotation", True)
        boom.set_editor_property("do_collision_test", False)
        outcome["view"] = [controller.get_control_rotation().pitch, controller.get_control_rotation().yaw]
    apply_each_label(world, apply, outcome, "props", apply_props)
    for label, is_hidden in apply.get("hide", {}).items():
        actor = find_actor_by_label(world, label)
        if actor is None:
            outcome.setdefault("hide", {})[label] = "액터 없음"
            continue
        actor.set_actor_hidden_in_game(is_hidden)
        outcome.setdefault("hide", {})[label] = is_hidden
    apply_each_label(world, apply, outcome, "location", apply_location)
    apply_each_label(world, apply, outcome, "hide_components", apply_hidden_components)
    apply_each_label(world, apply, outcome, "preset", apply_preset, set(apply.get("preset_reinit", [])))
    if "exposure_ev100" in apply:
        outcome["exposure"] = apply_exposure(world, apply["exposure_ev100"])
    for label, intensity in apply.get("lights", {}).items():
        actor = find_actor_by_label(world, label)
        if actor is None:
            outcome.setdefault("lights", {})[label] = "액터 없음"
            continue
        actor.get_component_by_class(unreal.LightComponentBase).set_intensity(intensity)
        outcome.setdefault("lights", {})[label] = intensity
    if "fire_light" in apply:
        outcome["fire_light"] = apply_component_values(world, apply["fire_light"], "fire_light_component", set_properties)
    for label, values in apply.get("flame", {}).items():
        actor = find_actor_by_label(world, label)
        if actor is None:
            outcome.setdefault("flame", {})[label] = {"error": "액터 없음"}
            continue
        cards = [component for component in actor.get_components_by_class(unreal.StaticMeshComponent) if is_flame_card_material(component.get_material(0))]
        for card in cards:
            set_material_values(card.get_material(0), values)
        outcome.setdefault("flame", {})[label] = {"cards": len(cards)}
    if "volume" in apply:
        outcome["volume"] = apply_component_values(world, apply["volume"], "volume_mesh_component", set_mesh_material_values)
    if "ember" in apply:
        outcome["ember"] = apply_component_values(world, apply["ember"], "ember_component", set_ember_material_values)
    apply_each_label(world, apply, outcome, "sim", apply_sim)
    apply_each_label(world, apply, outcome, "emitter", apply_emitter)
    for label, material_path in apply.get("surface_material", {}).items():
        actor = find_actor_by_label(world, label)
        if actor is None:
            outcome.setdefault("surface_material", {})[label] = "액터 없음"
            continue
        live = actor.get_editor_property("live_component")
        live.set_editor_property("output_material", unreal.load_asset(material_path))
        live.bind_internal_mesh_component(actor.get_editor_property("internal_surface_mesh"))
        outcome.setdefault("surface_material", {})[label] = actor.get_editor_property("internal_surface_mesh").get_material(0).get_name()
    if "console" in apply:
        outcome["console"] = run_console_commands(world, apply["console"])
    if "move" in apply:
        PARAMS["start_x"] = None
        PARAMS["goal_x"], PARAMS["goal_y"] = apply["move"]
        outcome["move"] = action_fire_move(world)
    if "speed" in apply:
        pawn, controller = player_pawn_and_controller(world)
        pawn.set_max_move_speed(apply["speed"])
        outcome["speed"] = pawn.get_max_move_speed()
    if "trigger" in apply:
        PARAMS["hit_dx"], PARAMS["hit_dy"], PARAMS["hit_dz"] = apply["trigger"]
        outcome["trigger"] = action_fire_trigger(world)
        outcome["trigger_game_time"] = unreal.GameplayStatics.get_time_seconds(world)
    if "time_dilation" in apply:
        unreal.GameplayStatics.set_global_time_dilation(world, apply["time_dilation"])
        outcome["time_dilation"] = unreal.GameplayStatics.get_global_time_dilation(world)
    if "schedule" in apply:
        try:
            outcome["schedule"] = begin_schedule(world, apply["schedule"], outcome)
        except Exception:
            outcome["schedule"] = {"error": traceback.format_exc()[-600:]}
    outcome["game_time"] = unreal.GameplayStatics.get_time_seconds(world)
    return outcome


def action_lookdev_shot(world):
    game_time = unreal.GameplayStatics.get_time_seconds(world)
    unreal.SystemLibrary.execute_console_command(world, "HighResShot " + PARAMS["size"] + " filename=" + PARAMS["path"])
    outcome = {"game_time": game_time}
    outcome["ember_mid"] = describe_ember_materials(world)
    effects = report_preset_effects(world)
    if effects:
        outcome["preset_effect"] = effects
    return outcome


def action_lookdev_schedule_stop(world):
    return stop_schedule("바깥 Python 대기 시간 초과") or {"stopped": False}


def run():'''


def editor_action(action, **fields):
    code = pie_showcase_fluid.EDITOR_CODE.replace("\ndef run():", LOOKDEV_ACTIONS, 1)
    params = {"action": action, **fields}
    outcome = run_in_editor.run_code(code.replace("__PARAMS__", repr(json.dumps(params))))
    if not outcome["connected"]:
        return {"error": "에디터 원격 실행에 연결하지 못했다"}
    for line in outcome["lines"]:
        index = line.find(pie_showcase_fluid.RESULT_MARKER)
        if index >= 0:
            try:
                return json.loads(line[index + len(pie_showcase_fluid.RESULT_MARKER):].strip().splitlines()[0])
            except ValueError:
                return {"error": "결과 해석 실패: " + line[-200:]}
    return {"error": "결과 표식 없음: " + " | ".join(outcome["lines"])[-300:]}


def apply_variant(apply, tag):
    return editor_action("lookdev_apply", apply=apply, tag=tag, output=OUTPUT_DIRECTORY.as_posix(), size=SHOT_SIZE)


def wait_for_file(path):
    deadline = time.monotonic() + FRAME_FILE_WAIT_SECONDS
    while time.monotonic() < deadline:
        if path.exists() and path.stat().st_size > 0:
            return True
        time.sleep(0.5)
    return False


def list_editor_screenshots():
    return set(SCREENSHOT_DIRECTORY.glob("*.png")) if SCREENSHOT_DIRECTORY.exists() else set()


def wait_for_schedule(status_path, timeout_seconds):
    deadline = time.monotonic() + timeout_seconds
    status = {}
    while time.monotonic() < deadline:
        try:
            status = json.loads(status_path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            pass
        if status.get("done"):
            return status
        time.sleep(SCHEDULE_POLL_SECONDS)
    return dict(status, outer_timeout=True)


def describe_seconds(value):
    return f"{value:.3f}" if isinstance(value, (int, float)) else "?"


def collect_schedule_shots(outcome, label, shots):
    schedule = outcome.get("schedule") if isinstance(outcome, dict) else None
    if not isinstance(schedule, dict) or "status_path" not in schedule:
        return None
    status = wait_for_schedule(Path(schedule["status_path"]), schedule["timeout_seconds"] + SCHEDULE_POLL_MARGIN_SECONDS)
    if not status.get("done"):
        stopped = editor_action("lookdev_schedule_stop")
        status = dict(stopped, outer_timeout=True) if isinstance(stopped, dict) and "shots" in stopped else dict(status, stop_result=stopped)
    for shot in status.get("shots", []):
        path = Path(shot["path"])
        if wait_for_file(path):
            shots.append((f"{label} t={describe_seconds(shot['time'])} game={describe_seconds(shot.get('captured_game_time'))}", path))
    return status


def scheduled_shot_count(apply):
    return len(apply["schedule"].get("times", [])) if isinstance(apply.get("schedule"), dict) else 0


def build_grid(shots, grid_path):
    tiles = []
    for name, path in shots:
        image = Image.open(path).convert("RGB")
        tile_height = int(image.height * GRID_TILE_WIDTH / image.width)
        tile = image.resize((GRID_TILE_WIDTH, tile_height))
        draw = ImageDraw.Draw(tile)
        draw.rectangle((0, 0, GRID_TILE_WIDTH, 22), fill=(0, 0, 0))
        draw.text((6, 4), name, fill=(255, 255, 255))
        tiles.append(tile)
    if not tiles:
        return False
    rows = (len(tiles) + GRID_COLUMNS - 1) // GRID_COLUMNS
    grid = Image.new("RGB", (GRID_TILE_WIDTH * GRID_COLUMNS, tiles[0].height * rows))
    for index, tile in enumerate(tiles):
        grid.paste(tile, ((index % GRID_COLUMNS) * GRID_TILE_WIDTH, (index // GRID_COLUMNS) * tiles[0].height))
    grid.save(grid_path)
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--variants", required=True, type=Path)
    parser.add_argument("--level", default=DEFAULT_LEVEL_PATH)
    parser.add_argument("--settle-seconds", type=float, default=3.0)
    parser.add_argument("--stop-pie", action="store_true")
    arguments = parser.parse_args()
    specification = json.loads(arguments.variants.read_text(encoding="utf-8"))
    print("경고: 측정 동안 UnrealEditor 창을 앞창으로 두고 건드리지 않는다(L-editor-06).", flush=True)
    OUTPUT_DIRECTORY.mkdir(parents=True, exist_ok=True)

    session = None
    environment = {}
    if not pie_check_fluid.wait_for_pie_state(True, 0.5):
        level_status = pie_showcase_fluid.open_stage_level(arguments.level)
        if not level_status.get("loaded"):
            print("레벨을 열지 못했다:", json.dumps(level_status, ensure_ascii=False))
            return EXIT_ENVIRONMENT
        try:
            session = uemcp.connect()
        except (OSError, ValueError, KeyError) as error:
            print("MCP 서버에 연결하지 못했다:", error)
            return EXIT_ENVIRONMENT
        environment["navigation_ready"] = pie_check_fluid.wait_for_editor_navigation()
        if not pie_check_fluid.start_pie(session, environment) or not pie_check_fluid.wait_for_pie_state(True, pie_check_fluid.PIE_START_WAIT_SECONDS):
            print("PIE를 시작하지 못했다:", json.dumps(environment, ensure_ascii=False)[:300])
            return EXIT_ENVIRONMENT
        time.sleep(3.0)

    shots = []
    base_apply = specification.get("base", {})
    result = {"level": arguments.level, "base": apply_variant(base_apply, "base"), "variants": []}
    expected_shots = scheduled_shot_count(base_apply)
    base_schedule = collect_schedule_shots(result["base"], "base", shots)
    if base_schedule is not None:
        result["base_schedule"] = base_schedule
    for index, variant in enumerate(specification["variants"]):
        apply = variant.get("apply", {})
        screenshots_before = list_editor_screenshots()
        outcome = apply_variant(apply, f"{index:02d}")
        entry = {"name": variant["name"], "apply": outcome}
        if "schedule" in apply:
            expected = scheduled_shot_count(apply)
            expected_shots += expected
            shots_before = len(shots)
            entry["schedule"] = collect_schedule_shots(outcome, variant["name"], shots)
            entry["saved"] = len(shots) - shots_before == expected
            summary = f"예약 {len(shots) - shots_before}/{expected}장"
        else:
            expected_shots += 1
            time.sleep(variant.get("settle", arguments.settle_seconds))
            path = OUTPUT_DIRECTORY / f"{index:02d}_{variant['name']}.png"
            shot = editor_action("lookdev_shot", size=SHOT_SIZE, path=path.as_posix())
            outcome["shot_game_time"] = shot.get("game_time") if isinstance(shot, dict) else None
            if isinstance(shot, dict) and "preset_effect" in shot:
                outcome["preset_effect"] = shot["preset_effect"]
            if isinstance(shot, dict) and "ember_mid" in shot:
                outcome["ember_mid"] = shot["ember_mid"]
            is_saved = wait_for_file(path)
            if is_saved:
                shots.append((variant["name"], path))
            entry["saved"] = is_saved
            summary = "찍음" if is_saved else "실패"
        if "console" in apply:
            entry["console_screenshots"] = sorted(path.relative_to(ROOT).as_posix() for path in list_editor_screenshots() - screenshots_before)
        result["variants"].append(entry)
        print(variant["name"], summary, json.dumps(outcome, ensure_ascii=False)[:200], flush=True)

    has_grid = build_grid(shots, OUTPUT_DIRECTORY / "grid.png")
    (OUTPUT_DIRECTORY / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    if arguments.stop_pie:
        if session is None:
            session = uemcp.connect()
        pie_check_fluid.stop_pie(session, environment)
    print("격자", (OUTPUT_DIRECTORY / "grid.png").relative_to(ROOT).as_posix() if has_grid else "없음")
    return 0 if len(shots) == expected_shots else EXIT_PARTIAL


if __name__ == "__main__":
    sys.exit(main())
