"""실행 환경: TDGame 언리얼 에디터 Python(PIE 아님, run_in_editor.py 원격 실행, 에디터 앞창 유지 L-editor-06, FX-16b 통합 빌드와 에디터 재시작 뒤 TDFluidDebugDrawSubsystem·TD.FluidFX.SimulatePreview 노출). 요청 파일 Saved/FluidFX/fluid_preview_request.json 을 읽는다
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_preview_fluid.py (PowerShell). 요청 {"action": "start|status|stop", "level": "/Game/...", "camera": {"location": [x, y, z], "rotation": [pitch, yaw, roll]}, "auto_reload_seconds": 1}, level·camera·auto_reload_seconds 는 선택(start 에서만 쓴다). start = TD.FluidFX.SimulatePreview 1·PresetTuningAutoReloadSeconds 를 켜고 SIE(Simulate In Editor, 편집기 안 시뮬레이트) 시작을 요청하며 실제 시작은 다음 틱이라 status 로 확인, status = TDFluidDebugDrawSubsystem.build_debug_snapshot_json 과 깨어 있는 액터 수, stop = SIE 를 끝내고 두 콘솔 변수를 start 전 값으로 되돌린다
출력: Saved/FluidFX/fluid_preview.json(동작·ok·콘솔 변수 읽은 값·status 면 스냅숏), Saved/FluidFX/fluid_preview_state.json(start 전 콘솔 변수 값, stop 이 지움), 본문 TD_FLUID_PREVIEW_RESULT 줄. ok 가 거짓이면 예외를 던져 run_in_editor 가 FAILED 로 끝난다(L-editor-73)
상태: 작성만(2026-10-29 FX-16b 묶음 E①, py_compile 과 가짜 unreal 정적 검토만, 실제 SIE 미실행). unreal 이름이 하나라도 없으면 아무것도 바꾸지 않고 멈춘다. 폰 입력이 없는 SIE 라 뷰 위치가 비면(창 최소화) 활성화가 일어나지 않는다.
"""
import json
import os

import unreal

RESULT_MARKER = "TD_FLUID_PREVIEW_RESULT"
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_preview_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_preview.json")
STATE_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_preview_state.json")
SIMULATE_PREVIEW_VARIABLE = "TD.FluidFX.SimulatePreview"
AUTO_RELOAD_VARIABLE = "TD.FluidFX.PresetTuningAutoReloadSeconds"
ACTION_START = "start"
ACTION_STATUS = "status"
ACTION_STOP = "stop"
DEFAULT_AUTO_RELOAD_SECONDS = 1.0
MAXIMUM_AUTO_RELOAD_SECONDS = 3600.0
READ_BACK_TOLERANCE = 1.0e-3
CONSOLE_METHODS = ["execute_console_command", "get_console_variable_int_value", "get_console_variable_float_value", "get_project_directory"]
REQUIRED_NAMES_BY_ACTION = {
    ACTION_START: {
        "LevelEditorSubsystem": ["editor_play_simulate", "is_in_play_in_editor", "load_level"],
        "UnrealEditorSubsystem": ["get_editor_world", "get_game_world", "set_level_viewport_camera_info"],
        "SystemLibrary": CONSOLE_METHODS,
        "EditorLoadingAndSavingUtils": ["get_dirty_map_packages", "get_dirty_content_packages"],
    },
    ACTION_STATUS: {
        "UnrealEditorSubsystem": ["get_game_world"],
        "SubsystemBlueprintLibrary": ["get_world_subsystem"],
        "TDFluidDebugDrawSubsystem": ["build_debug_snapshot_json"],
        "SystemLibrary": CONSOLE_METHODS,
    },
    ACTION_STOP: {
        "LevelEditorSubsystem": ["editor_request_end_play", "is_in_play_in_editor"],
        "UnrealEditorSubsystem": ["get_editor_world", "get_game_world"],
        "SystemLibrary": CONSOLE_METHODS,
    },
}


def build_path(project_directory, relative_path):
    return os.path.join(project_directory, *relative_path)


def find_missing_names(action):
    missing = []
    for class_name, method_names in REQUIRED_NAMES_BY_ACTION[action].items():
        owner = getattr(unreal, class_name, None)
        if owner is None:
            missing.append(class_name)
            continue
        missing.extend(f"{class_name}.{method_name}" for method_name in method_names if not hasattr(owner, method_name))
    return missing


def read_vector_components(value, key):
    if not isinstance(value, list) or len(value) != 3 or not all(isinstance(item, (int, float)) and not isinstance(item, bool) for item in value):
        raise ValueError(f"camera.{key}: 숫자 3개 배열이어야 한다")
    return [float(item) for item in value]


def read_request(project_directory):
    with open(build_path(project_directory, REQUEST_RELATIVE_PATH), encoding="utf-8") as request_file:
        raw = json.load(request_file)
    action = raw.get("action")
    if action not in REQUIRED_NAMES_BY_ACTION:
        raise ValueError("action: start, status, stop 중 하나여야 한다")
    level = raw.get("level")
    if level is not None and (not isinstance(level, str) or not level.startswith("/Game/")):
        raise ValueError("level: /Game/... 레벨 패키지 경로여야 한다")
    auto_reload_seconds = raw.get("auto_reload_seconds", DEFAULT_AUTO_RELOAD_SECONDS)
    if isinstance(auto_reload_seconds, bool) or not isinstance(auto_reload_seconds, (int, float)) or not 0.0 <= auto_reload_seconds <= MAXIMUM_AUTO_RELOAD_SECONDS:
        raise ValueError(f"auto_reload_seconds: 0~{MAXIMUM_AUTO_RELOAD_SECONDS} 숫자여야 한다")
    camera = raw.get("camera")
    if camera is not None:
        if not isinstance(camera, dict):
            raise ValueError("camera: {location, rotation} 객체여야 한다")
        camera = {"location": read_vector_components(camera.get("location"), "location"), "rotation": read_vector_components(camera.get("rotation"), "rotation")}
    return {"action": action, "level": level, "camera": camera, "auto_reload_seconds": float(auto_reload_seconds)}


def read_console_values():
    return {
        SIMULATE_PREVIEW_VARIABLE: int(unreal.SystemLibrary.get_console_variable_int_value(SIMULATE_PREVIEW_VARIABLE)),
        AUTO_RELOAD_VARIABLE: float(unreal.SystemLibrary.get_console_variable_float_value(AUTO_RELOAD_VARIABLE)),
    }


def set_console_value(world, name, value):
    unreal.SystemLibrary.execute_console_command(world, f"{name} {value}")


def read_game_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def find_dirty_package_names():
    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()) + list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    return sorted({package.get_name() for package in dirty})


def switch_level_if_needed(level):
    if level is None:
        return None
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if editor.get_editor_world().get_path_name().split(".")[0] == level:
        return None
    dirty_names = find_dirty_package_names()
    if dirty_names:
        return {"ok": False, "error": "미저장 작업을 보존하려고 레벨을 바꾸지 않는다", "dirty": dirty_names}
    if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(level):
        return {"ok": False, "error": "레벨 열기 실패", "level": level}
    return None


def apply_camera(camera):
    if camera is None:
        return
    location = camera["location"]
    rotation = camera["rotation"]
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
        unreal.Vector(x=location[0], y=location[1], z=location[2]),
        unreal.Rotator(roll=rotation[2], pitch=rotation[0], yaw=rotation[1]),
    )


def save_original_values(project_directory, original_values):
    state_path = build_path(project_directory, STATE_RELATIVE_PATH)
    if os.path.isfile(state_path):
        return False
    with open(state_path, "w", encoding="utf-8", newline="\n") as state_file:
        json.dump(original_values, state_file, indent=2)
    return True


def start_preview(request, project_directory):
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if levels.is_in_play_in_editor() or read_game_world() is not None:
        return {"ok": False, "error": "이미 PIE 또는 SIE 가 실행 중이다. stop 을 먼저 한다"}
    level_failure = switch_level_if_needed(request["level"])
    if level_failure is not None:
        return level_failure
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    original_values = read_console_values()
    saved_originals = save_original_values(project_directory, original_values)
    set_console_value(world, SIMULATE_PREVIEW_VARIABLE, 1)
    set_console_value(world, AUTO_RELOAD_VARIABLE, request["auto_reload_seconds"])
    read_back = read_console_values()
    if read_back[SIMULATE_PREVIEW_VARIABLE] != 1 or abs(read_back[AUTO_RELOAD_VARIABLE] - request["auto_reload_seconds"]) > READ_BACK_TOLERANCE:
        return {"ok": False, "error": "콘솔 변수를 다시 읽은 값이 요청과 다르다", "console_read_back": read_back, "console_before": original_values}
    apply_camera(request["camera"])
    levels.editor_play_simulate()
    return {"ok": True, "simulate_requested": True, "console_before": original_values, "console_read_back": read_back, "original_values_saved": saved_originals, "camera": request["camera"], "next": "status 로 깨어 있는 액터 수를 확인한다"}


def read_snapshot_actors(snapshot):
    if isinstance(snapshot, list):
        return snapshot
    if isinstance(snapshot, dict) and isinstance(snapshot.get("actors"), list):
        return snapshot["actors"]
    return None


def read_status(request, project_directory):
    world = read_game_world()
    if world is None:
        return {"ok": False, "error": "SIE 또는 PIE 월드가 없다(start 직후면 한 틱 뒤에 다시 status)", "console": read_console_values()}
    subsystem = unreal.SubsystemBlueprintLibrary.get_world_subsystem(world, unreal.TDFluidDebugDrawSubsystem)
    if subsystem is None:
        return {"ok": False, "error": "TDFluidDebugDrawSubsystem 이 이 월드에 없다(ENABLE_DRAW_DEBUG 빌드와 게임 월드 필요)", "console": read_console_values()}
    snapshot_text = subsystem.build_debug_snapshot_json()
    if not snapshot_text:
        return {"ok": False, "error": "build_debug_snapshot_json 이 빈 값을 돌려줬다", "console": read_console_values()}
    snapshot = json.loads(snapshot_text)
    actors = read_snapshot_actors(snapshot)
    if actors is None:
        return {"ok": False, "error": "스냅숏 모양을 읽지 못했다(배열 또는 actors 배열 기대)", "snapshot": snapshot, "console": read_console_values()}
    awake_count = sum(1 for actor in actors if isinstance(actor, dict) and actor.get("awake"))
    return {"ok": True, "actor_count": len(actors), "awake_count": awake_count, "snapshot": snapshot, "console": read_console_values()}


def read_saved_originals(project_directory):
    state_path = build_path(project_directory, STATE_RELATIVE_PATH)
    if not os.path.isfile(state_path):
        return None
    with open(state_path, encoding="utf-8") as state_file:
        return json.load(state_file)


def stop_preview(request, project_directory):
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    was_running = bool(levels.is_in_play_in_editor() or read_game_world() is not None)
    if was_running:
        levels.editor_request_end_play()
    originals = read_saved_originals(project_directory)
    if originals is None:
        return {"ok": True, "end_play_requested": was_running, "restored": False, "note": "start 전 값 기록이 없어 콘솔 변수는 그대로 둔다", "console": read_console_values()}
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    for name, value in originals.items():
        set_console_value(world, name, value)
    read_back = read_console_values()
    restored = read_back[SIMULATE_PREVIEW_VARIABLE] == originals[SIMULATE_PREVIEW_VARIABLE] and abs(read_back[AUTO_RELOAD_VARIABLE] - originals[AUTO_RELOAD_VARIABLE]) <= READ_BACK_TOLERANCE
    if restored:
        os.remove(build_path(project_directory, STATE_RELATIVE_PATH))
    return {"ok": restored, "end_play_requested": was_running, "restored": restored, "console_before_restore_target": originals, "console_read_back": read_back}


ACTION_HANDLERS = {ACTION_START: start_preview, ACTION_STATUS: read_status, ACTION_STOP: stop_preview}


def run_action(project_directory):
    try:
        request = read_request(project_directory)
    except (OSError, ValueError) as error:
        return {"ok": False, "action": None, "error": f"요청 오류: {error}"}
    missing = find_missing_names(request["action"])
    if missing:
        return {"ok": False, "action": request["action"], "error": "unreal 이름 없음(통합 빌드·에디터 재시작 전이거나 바이너리가 헤더보다 오래됨), 아무것도 바꾸지 않았다", "missing": missing}
    result = ACTION_HANDLERS[request["action"]](request, project_directory)
    result["action"] = request["action"]
    return result


def write_report(project_directory, result):
    report_path = build_path(project_directory, REPORT_RELATIVE_PATH)
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, "w", encoding="utf-8", newline="\n") as report_file:
        json.dump(result, report_file, ensure_ascii=False, indent=2, sort_keys=True)
    return report_path


def main():
    project_directory = unreal.SystemLibrary.get_project_directory()
    result = run_action(project_directory)
    report_path = write_report(project_directory, result)
    print(RESULT_MARKER, json.dumps({"ok": result["ok"], "action": result.get("action"), "error": result.get("error"), "report": report_path}, ensure_ascii=False))
    if not result["ok"]:
        raise RuntimeError(result.get("error", "실패"))


if __name__ == "__main__":
    main()
