"""에디터 안에서 실행(PIE 중): 플레이어 폰에 클릭 이동·점프·콘솔 명령을 내린 뒤, 에디터 틱 콜백으로 지정한 시간 동안 매 프레임 위치·회전·속도·Mover 이동 모드·발 뼈 상대 위치·경로 추종 상태를 기록하는 탐침. pie_check_player_movement.py가 단계별로 호출한다.
실행: python Tools/run_in_editor.py -c "exec(open(r'<이 파일>', encoding='utf-8').read()); probe('snapshot', 'tag')"
출력: Saved/Movement/player_probe.jsonl (한 줄 한 기록), Saved/Screenshots/WindowsEditor/<이름>.png
상태: 현행 (2026-09-30)
"""
import json
import os

import unreal

PROBE_FILE = os.path.join(unreal.SystemLibrary.get_project_saved_directory(), "Movement", "player_probe.jsonl")
FOOT_BONES = ("foot_l", "foot_r")


def _world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def _record(entry):
    os.makedirs(os.path.dirname(PROBE_FILE), exist_ok=True)
    with open(PROBE_FILE, "a", encoding="utf-8") as handle:
        handle.write(json.dumps(entry, ensure_ascii=False) + "\n")


def _round_vector(vector):
    return [round(vector.x, 2), round(vector.y, 2), round(vector.z, 2)]


def _path_status(controller):
    path_following = controller.get_component_by_class(unreal.PathFollowingComponent) if controller else None
    if path_following is None:
        return None
    try:
        return str(path_following.get_path_action_type())
    except AttributeError:
        return "unknown"


def _player_state(world, pawn, controller):
    if pawn is None:
        return {"missing": True}
    transform = pawn.get_actor_transform()
    mesh = pawn.get_editor_property("mesh")
    mover = pawn.get_editor_property("mover_component")
    feet = {}
    for bone in FOOT_BONES:
        feet[bone] = _round_vector(unreal.MathLibrary.inverse_transform_location(transform, mesh.get_socket_location(bone)))
    return {
        "class": pawn.get_class().get_name(),
        "location": _round_vector(pawn.get_actor_location()),
        "yaw": round(pawn.get_actor_rotation().yaw, 2),
        "velocity": _round_vector(pawn.get_velocity()),
        "mode": str(mover.get_movement_mode_name()) if mover else None,
        "airborne": pawn.is_airborne(),
        "on_ground": pawn.is_moving_on_ground(),
        "feet": feet,
        "path": _path_status(controller),
        "mesh_animation_enabled": mesh.get_editor_property("enable_animation"),
    }


def _start_recording(tag, seconds):
    world = _world()
    end_time = unreal.GameplayStatics.get_time_seconds(world) + seconds
    state = {"handle": None, "frame": 0}

    def on_tick(delta_seconds):
        game_world = _world()
        if game_world is None:
            unreal.unregister_slate_post_tick_callback(state["handle"])
            return
        now = unreal.GameplayStatics.get_time_seconds(game_world)
        pawn = unreal.GameplayStatics.get_player_pawn(game_world, 0)
        controller = unreal.GameplayStatics.get_player_controller(game_world, 0)
        _record({"tag": f"{tag}_{state['frame']}", "time": round(now, 3), "player": _player_state(game_world, pawn, controller)})
        state["frame"] += 1
        if now >= end_time:
            unreal.unregister_slate_post_tick_callback(state["handle"])

    state["handle"] = unreal.register_slate_post_tick_callback(on_tick)


def probe(action, tag="", x=0.0, y=0.0, shot="", record_seconds=0.0, command=""):
    world = _world()
    if world is None:
        _record({"tag": tag, "error": "no PIE world"})
        return
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    controller = unreal.GameplayStatics.get_player_controller(world, 0)
    result = None
    if action == "path_ready" and pawn is not None:
        start = pawn.get_actor_location()
        path = unreal.NavigationSystemV1.find_path_to_location_synchronously(world, start, unreal.Vector(x, y, start.z), pawn)
        result = bool(path) and path.is_valid() and not path.is_partial()
    if action == "move_to" and controller is not None and pawn is not None:
        unreal.AIHelperLibrary.simple_move_to_location(controller, unreal.Vector(x, y, pawn.get_actor_location().z))
    if action == "jump" and pawn is not None:
        result = pawn.jump()
    if action == "console":
        unreal.SystemLibrary.execute_console_command(world, command or tag)
    if shot:
        unreal.AutomationLibrary.take_high_res_screenshot(1280, 720, shot)
    _record({"tag": tag, "action": action, "result": result, "level": world.get_path_name(),
             "time": round(unreal.GameplayStatics.get_time_seconds(world), 3), "player": _player_state(world, pawn, controller)})
    if record_seconds > 0.0:
        _start_recording(tag, record_seconds)
