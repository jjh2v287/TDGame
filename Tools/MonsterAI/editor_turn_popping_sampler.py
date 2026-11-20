"""실행 환경: 언리얼 에디터 안 Python(원격 실행). pie_measure_turn_popping.py가 importlib로 적재해 start_job·status·stop·restore를 부른다. 맵·에셋을 저장하지 않는다.
실행 명령: 직접 실행하지 않는다(python Tools/MonsterAI/pie_measure_turn_popping.py 가 적재, PowerShell).
출력: start_job 의 out_path JSON. 몬스터마다 매 프레임 [게임 초, 월드 델타, x, y, 논리 요(액터), 메시 요(보이는 몸), 루트 뼈 요, 골반 뼈 요, 속도 x, 속도 y, 상태, 행동, 살아 있음], 플레이어 [게임 초, 델타, x, y, 요], 프레임마다 실시간 초.
상태: 현행(M3-42). 플레이어는 경로(별 모양 꼭짓점 + 멈춤)를 실제 이동 입력으로 걷고 0.25초마다 체력을 채운다. --fixed-fps 는 작업 동안만 t.OverrideFPS 를 걸고 끝나면 되돌린다.
"""
import json
import math
import os
import re

import unreal

STATE = {"handle": None, "job": None, "fixed_fps_restore": None, "camera_restore": None}
CAMERA_PROPERTIES = ("target_arm_length", "target_offset", "enable_camera_lag")
NO_NOTIFY = unreal.PropertyAccessChangeNotifyMode.NEVER
PELVIS_CANDIDATES = ("pelvis", "Pelvis", "hips", "Hips", "Bip001-Pelvis", "Bip01-Pelvis", "spine_01", "Spine", "spine")
HEAL_INTERVAL_SECONDS = 0.25
WAYPOINT_REACHED_CM = 70.0
FIXED_FPS_VARIABLE = "t.OverrideFPS"
DEBUG_LINE_PATTERN = re.compile(r"actor=(\S+) def=(\S+) state=(\S+) action=(\S+)")


def _world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def _console_world():
    return _world() or unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def _set_console_float(name, value):
    unreal.SystemLibrary.execute_console_command(_console_world(), f"{name} {value:g}", None)
    return unreal.SystemLibrary.get_console_variable_float_value(name)


def restore_camera():
    saved = STATE["camera_restore"]
    if saved is None:
        return None
    boom, originals = saved
    STATE["camera_restore"] = None
    if not unreal.SystemLibrary.is_valid(boom):
        return False
    for name, value in originals.items():
        boom.set_editor_property(name, value, NO_NOTIFY)
    return True


def restore():
    camera = restore_camera()
    before = STATE["fixed_fps_restore"]
    if before is None:
        return {"camera": camera} if camera is not None else None
    applied = _set_console_float(FIXED_FPS_VARIABLE, before)
    STATE["fixed_fps_restore"] = None
    return {"fixed_fps": applied, "camera": camera}


def _start_capture(job, capture):
    boom = job["pawn"].get_component_by_class(unreal.SpringArmComponent)
    if boom is None:
        return {"error": "player pawn has no SpringArmComponent"}
    restore_camera()
    STATE["camera_restore"] = (boom, {name: boom.get_editor_property(name) for name in CAMERA_PROPERTIES})
    boom.set_editor_property("enable_camera_lag", False, NO_NOTIFY)
    job["capture"] = dict(capture, boom=boom, pending=None, shots=[], last_issue_frame=-10**9, frame=0)
    return {"camera_arm_cm": capture["camera_arm_cm"], "focus_index": capture["focus_index"]}


def _focus_camera(capture, entry):
    boom = capture["boom"]
    if entry is None or entry["actor"] is None or not unreal.SystemLibrary.is_valid(boom):
        return
    boom.set_editor_property("target_arm_length", float(capture["camera_arm_cm"]), NO_NOTIFY)
    boom.set_editor_property("target_offset", entry["actor"].get_actor_location() - boom.get_world_location(), NO_NOTIFY)


def _advance_capture(job, world, elapsed):
    capture = job.get("capture")
    if not capture:
        return
    capture["frame"] += 1
    focus = job["monsters"][capture["focus_index"]] if capture["focus_index"] < len(job["monsters"]) else None
    _focus_camera(capture, focus)
    pending = capture["pending"]
    if pending is not None:
        if pending["captured_seconds"] is None and capture["frame"] > pending["issued_frame"]:
            pending["captured_seconds"] = round(elapsed, 6)
        written = os.path.exists(pending["path"]) and os.path.getsize(pending["path"]) > 0
        if not written and capture["frame"] - pending["issued_frame"] < capture["file_wait_frames"]:
            return
        pending["saved"] = written
        capture["pending"] = None
    in_window = capture["start"] <= elapsed <= capture["start"] + capture["seconds"]
    if not in_window or capture["frame"] - capture["last_issue_frame"] < capture["every_frames"]:
        return
    path = f"{capture['out_dir']}/{capture['prefix']}_{len(capture['shots']):04d}.png"
    controller = unreal.GameplayStatics.get_player_controller(world, 0)
    unreal.SystemLibrary.execute_console_command(world, f"HighResShot {capture['size']} filename={path}", controller)
    capture["pending"] = {"path": path, "issued_frame": capture["frame"], "issued_seconds": round(elapsed, 6), "captured_seconds": None, "saved": False}
    capture["shots"].append(capture["pending"])
    capture["last_issue_frame"] = capture["frame"]


def _find_pelvis(mesh):
    for name in PELVIS_CANDIDATES:
        if mesh.get_bone_index(name) >= 0:
            return name
    return None


def _monster_info(actor):
    mesh = actor.get_editor_property("mesh")
    species = actor.get_species()
    root_bone = str(mesh.get_bone_name(0)) if mesh.get_num_bones() > 0 else None
    return {
        "name": actor.get_name(),
        "species": species.get_name() if species else None,
        "mesh_yaw": float(species.get_editor_property("mesh_yaw")) if species else 0.0,
        "turning_rate": float(actor.get_editor_property("turning_rate")),
        "root_bone": root_bone,
        "pelvis_bone": _find_pelvis(mesh),
    }


def _waypoints(origin, legs):
    points = []
    for leg in legs:
        if leg[0] == "hold":
            points.append(("hold", float(leg[1])))
            continue
        points.append(("move", unreal.Vector(origin.x + leg[1], origin.y + leg[2], origin.z)))
    return points


def start_job(config):
    world = _world()
    if world is None:
        return {"error": "no PIE world"}
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    if pawn is None:
        return {"error": "no player pawn"}
    monsters = [actor for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDMonsterCharacter)
                if actor.get_component_by_class(unreal.TDCombatComponent).is_alive()]
    if not monsters:
        return {"error": "no monsters"}
    fixed_fps = float(config.get("fixed_fps") or 0.0)
    if fixed_fps > 0.0 and STATE["fixed_fps_restore"] is None:
        STATE["fixed_fps_restore"] = unreal.SystemLibrary.get_console_variable_float_value(FIXED_FPS_VARIABLE)
    applied_fps = _set_console_float(FIXED_FPS_VARIABLE, fixed_fps) if fixed_fps > 0.0 else None
    now = unreal.GameplayStatics.get_time_seconds(world)
    job = {
        "label": config["label"], "out_path": config["out_path"], "duration": float(config["seconds"]), "t_start": now,
        "pawn": pawn, "combat": pawn.get_component_by_class(unreal.TDCombatComponent), "next_heal": now,
        "waypoints": _waypoints(pawn.get_actor_location(), config["legs"]), "leg_index": 0, "leg_started": now,
        "monsters": [{"actor": actor, "mesh": actor.get_editor_property("mesh"), "info": _monster_info(actor), "frames": []} for actor in monsters],
        "player": [], "clock": [], "done": False, "result": None, "game_seconds": now,
        "config": {key: value for key, value in config.items() if key != "out_path"}, "applied_fps": applied_fps,
    }
    STATE["job"] = job
    capture = _start_capture(job, config["capture"]) if config.get("capture") else None
    if STATE["handle"] is None:
        STATE["handle"] = unreal.register_slate_post_tick_callback(_tick)
    return {"started": job["label"], "monsters": [entry["info"] for entry in job["monsters"]], "applied_fps": applied_fps, "capture": capture}


def _drive_player(job, now):
    pawn = job["pawn"]
    while job["leg_index"] < len(job["waypoints"]):
        kind, value = job["waypoints"][job["leg_index"]]
        if kind == "hold":
            if now - job["leg_started"] < value:
                return
        else:
            location = pawn.get_actor_location()
            offset = unreal.Vector(value.x - location.x, value.y - location.y, 0.0)
            distance = math.hypot(offset.x, offset.y)
            if distance > WAYPOINT_REACHED_CM:
                pawn.add_movement_input(unreal.Vector(offset.x / distance, offset.y / distance, 0.0), 1.0, True)
                return
        job["leg_index"] += 1
        job["leg_started"] = now
    job["leg_index"] = 0
    job["leg_started"] = now


def _heal_player(job, now):
    combat = job["combat"]
    if combat is None or now < job["next_heal"]:
        return
    job["next_heal"] = now + HEAL_INTERVAL_SECONDS
    combat.set_stats(combat.get_stats(), True)


def _debug_states(world):
    subsystem = unreal.TDMonsterThinkSubsystem.get_monster_think_subsystem(world)
    states = {}
    if subsystem is None:
        return states
    for line in subsystem.get_monster_debug_lines():
        match = DEBUG_LINE_PATTERN.search(str(line))
        if match:
            states[match.group(1)] = (match.group(3), match.group(4))
    return states


def _bone_yaw(mesh, bone):
    if not bone:
        return None
    return round(mesh.get_socket_rotation(bone).yaw, 4)


def _sample_monsters(job, world, now, delta):
    states = _debug_states(world)
    for entry in job["monsters"]:
        actor = entry["actor"]
        if actor is None:
            continue
        if not unreal.SystemLibrary.is_valid(actor):
            entry["actor"] = entry["mesh"] = None
            continue
        mesh = entry["mesh"]
        location = actor.get_actor_location()
        velocity = actor.get_velocity()
        state, action = states.get(entry["info"]["name"], ("None", "None"))
        alive = actor.get_component_by_class(unreal.TDCombatComponent).is_alive()
        entry["frames"].append([
            round(now, 6), round(delta, 6), round(location.x, 2), round(location.y, 2),
            round(actor.get_actor_rotation().yaw, 4), round(mesh.get_world_rotation().yaw, 4),
            _bone_yaw(mesh, entry["info"]["root_bone"]), _bone_yaw(mesh, entry["info"]["pelvis_bone"]),
            round(velocity.x, 2), round(velocity.y, 2), state, action, 1 if alive else 0,
        ])


def _tick(delta_seconds):
    job = STATE["job"]
    if not job or job["done"]:
        return
    try:
        world = _world()
        if world is None:
            _finish(job, "pie ended")
            return
        now = unreal.GameplayStatics.get_time_seconds(world)
        if now <= job["game_seconds"] and job["clock"]:
            return
        delta = unreal.GameplayStatics.get_world_delta_seconds(world)
        job["game_seconds"] = now
        job["clock"].append(round(unreal.GameplayStatics.get_real_time_seconds(world), 6))
        if not unreal.SystemLibrary.is_valid(job["pawn"]):
            _finish(job, "player destroyed")
            return
        player_location = job["pawn"].get_actor_location()
        job["player"].append([round(now, 6), round(delta, 6), round(player_location.x, 2), round(player_location.y, 2),
                              round(job["pawn"].get_actor_rotation().yaw, 4)])
        _sample_monsters(job, world, now, delta)
        _advance_capture(job, world, now - job["t_start"])
        _heal_player(job, now)
        if now - job["t_start"] >= job["duration"]:
            _finish(job, "complete")
            return
        _drive_player(job, now)
    except Exception as error:
        unreal.log_error(f"td_turn_popping_sampler: {error!r}")
        _finish(job, f"error: {error!r}")


def _finish(job, reason):
    job["done"] = True
    job["result"] = reason
    restored_fps = restore()
    payload = {"label": job["label"], "result": reason, "config": job["config"], "applied_fps": job["applied_fps"], "restored_fps": restored_fps,
               "clock": job["clock"], "player": job["player"],
               "capture": [dict(shot) for shot in (job.get("capture") or {}).get("shots", [])],
               "monsters": [{"info": entry["info"], "frames": entry["frames"]} for entry in job["monsters"]]}
    with open(job["out_path"], "w", encoding="utf-8") as stream:
        json.dump(payload, stream)
    job["pawn"] = job["combat"] = job["capture"] = None
    for entry in job["monsters"]:
        entry["actor"] = entry["mesh"] = None
    if STATE["handle"] is not None:
        unreal.unregister_slate_post_tick_callback(STATE["handle"])
        STATE["handle"] = None


def stop():
    job = STATE["job"]
    if job and not job["done"]:
        _finish(job, "stopped")
    return {"stopped": True, "restored_fps": restore()}


def status():
    job = STATE["job"]
    if not job:
        return {"job": None}
    return {"label": job["label"], "done": job["done"], "result": job["result"], "frames": len(job["clock"]), "game_seconds": job["game_seconds"],
            "shots": len((job.get("capture") or {}).get("shots", []))}
