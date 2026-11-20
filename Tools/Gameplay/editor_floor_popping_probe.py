"""실행 환경: 에디터 안(PIE 실행 중, Tools/run_in_editor.py 로 원격 실행). 시스템 Python 에서는 --self-test 만 가능(unreal 모듈 없이 import 해도 죽지 않음).
실행 명령: python Tools/run_in_editor.py Tools/Gameplay/editor_floor_popping_probe.py (먼저 Saved/Gameplay/FloorPopping/active_request.txt 에 요청 JSON 경로 한 줄, 중단은 ABORT 한 줄) / python -B Tools/Gameplay/editor_floor_popping_probe.py --self-test
출력: 요청의 run_dir 아래 probe_status.json · result.json · frames.csv · shots/shot_<0000>.png (CSV 프로파일러 파일은 Saved/Profiling/CSV)
상태: GF-07. 상태 기계·보간·촬영 명령은 --self-test 로 검증, 에디터 API 호출부는 에디터에서 실행 전까지 미검증.
"""
import bisect
import csv
import json
import math
import os
import sys
import time
import types

try:
    import unreal
except ImportError:
    unreal = None

STATE_MODULE_NAME = "td_floor_popping_state"
ACTIVE_REQUEST_PARTS = ("Saved", "Gameplay", "FloorPopping", "active_request.txt")
ABORT_TOKEN = "ABORT"
SETTLE_SHOT_LEAD_SECONDS = 0.2
STEER_LOOKAHEAD_POINTS = 6
PLAYER_KILL_DAMAGE = 1.0e9
STUCK_WINDOW_SECONDS = 2.0
STUCK_DISTANCE_CM = 50.0
REPATH_SPACING_CM = 25.0
STEER_SEARCH_POINTS = 40
INPUT_ARRIVE_CM = 60.0
INPUT_TIMEOUT_FACTOR = 2.0
PERFORMANCE_SETTINGS_PATH = "/Script/UnrealEd.Default__EditorPerformanceSettings"
THROTTLE_PROPERTY = "bThrottleCPUWhenNotForeground"
DROP_MIPS_VARIABLE = "r.Streaming.DropMips"
DROP_CACHED_AND_HIDDEN_MIPS = "2"
FINISH_GRACE_SECONDS = 1.0
STATUS_WRITE_INTERVAL_SECONDS = 2.0
FRAME_COLUMNS = (
    "frame", "real_perf", "real_epoch", "game_s", "delta", "seg",
    "pawn_x", "pawn_y", "pawn_z", "cam_x", "cam_y", "cam_z",
    "cam_pitch", "cam_yaw", "cam_roll", "fov",
)


def build_cumulative_lengths(points):
    lengths = [0.0]
    for previous, current in zip(points, points[1:]):
        lengths.append(lengths[-1] + math.hypot(current[0] - previous[0], current[1] - previous[1]))
    return lengths


def interpolate_polyline(points, lengths, distance):
    if distance <= 0.0 or len(points) == 1:
        return tuple(points[0])
    if distance >= lengths[-1]:
        return tuple(points[-1])
    index = min(bisect.bisect_right(lengths, distance) - 1, len(points) - 2)
    span = lengths[index + 1] - lengths[index]
    if span <= 0.0:
        return tuple(points[index])
    fraction = (distance - lengths[index]) / span
    start = points[index]
    end = points[index + 1]
    return tuple(start[axis] + (end[axis] - start[axis]) * fraction for axis in range(3))


def build_shot_command(run_dir, shot_number):
    return f"Shot filename={run_dir}/shots/shot_{shot_number:04d} -nosuffix"


class RouteMachine:
    def __init__(self, segments, speed_cm_s, shot_hz):
        self.segments = segments
        self.speed_cm_s = float(speed_cm_s)
        self.shot_period = 1.0 / float(shot_hz) if float(shot_hz) > 0.0 else None
        self.cumulative = [build_cumulative_lengths(segment["points"]) if segment["kind"] == "glide" else None for segment in segments]
        self.index = 0
        self.segment_start = None
        self.pawn_xy = None
        self.drive_input = False
        self.pending_teleport = None
        self.steer_points = None
        self.wants_repath = False
        self.progress_mark = None
        self.reset_progress()

    def reset_progress(self):
        self.steer_index = 0
        self.steer_points = None
        self.wants_repath = True
        self.progress_mark = None
        self.next_shot_number = 1
        self.next_time_index = 0
        self.settle_shot_taken = False

    def is_finished(self):
        return self.index >= len(self.segments)

    def current_index(self):
        return min(self.index, len(self.segments) - 1)

    def tick(self, now):
        action = {"position": None, "shot": None, "console": [], "steer": None, "teleport": None, "prestream": None, "kill": False, "repath": None}
        while not self.is_finished():
            if self.segment_start is None:
                self.segment_start = now
                action["console"].extend((self.index, command) for command in self.segments[self.index].get("console", []))
                if self.segments[self.index].get("prestream_next"):
                    action["prestream"] = self.segments[self.index]["prestream_next"]
                if self.segments[self.index].get("kill_player"):
                    action["kill"] = True
            elapsed = now - self.segment_start
            segment = self.segments[self.index]
            handler = getattr(self, "handle_" + segment["kind"])
            position, wants_shot, finished = handler(segment, elapsed, action["shot"] is None)
            if position is not None:
                action["position"] = position
            if self.drive_input and segment["kind"] == "glide" and not finished:
                action["steer"] = self.steer_vector(segment)
                if self.wants_repath:
                    action["repath"] = tuple(segment["points"][-1])
            if wants_shot:
                action["shot"] = {"seg": self.index, "seg_kind": segment["kind"], "seg_time": elapsed}
            if not finished:
                break
            self.index += 1
            self.segment_start = None
            self.reset_progress()
        action["done"] = self.is_finished()
        action["teleport"], self.pending_teleport = self.pending_teleport, None
        return action

    def handle_settle(self, segment, elapsed, can_shoot):
        position = None if self.drive_input else tuple(segment["position"])
        shot_time = segment.get("shot_at", max(0.0, segment["seconds"] - SETTLE_SHOT_LEAD_SECONDS))
        wants_shot = can_shoot and not self.settle_shot_taken and elapsed >= shot_time
        if wants_shot:
            self.settle_shot_taken = True
        finished = self.settle_shot_taken and elapsed >= segment["seconds"]
        return position, wants_shot, finished

    def set_steer_points(self, points):
        self.steer_points = points
        self.steer_index = 0
        self.wants_repath = False

    def active_points(self, segment):
        return self.steer_points or segment["points"]

    def update_progress(self, elapsed):
        if self.progress_mark is None or elapsed - self.progress_mark[0] >= STUCK_WINDOW_SECONDS:
            if self.progress_mark is not None and math.hypot(self.pawn_xy[0] - self.progress_mark[1][0], self.pawn_xy[1] - self.progress_mark[1][1]) < STUCK_DISTANCE_CM:
                self.wants_repath = True
            self.progress_mark = (elapsed, self.pawn_xy)

    def steer_vector(self, segment):
        points = self.active_points(segment)
        if self.pawn_xy is None:
            return None
        target = points[min(self.steer_index + STEER_LOOKAHEAD_POINTS, len(points) - 1)]
        return (target[0] - self.pawn_xy[0], target[1] - self.pawn_xy[1])

    def advance_steer_index(self, segment):
        points = self.active_points(segment)
        best_index, best_distance = self.steer_index, None
        for index in range(self.steer_index, min(self.steer_index + STEER_SEARCH_POINTS, len(points))):
            distance = math.hypot(points[index][0] - self.pawn_xy[0], points[index][1] - self.pawn_xy[1])
            if best_distance is None or distance < best_distance:
                best_index, best_distance = index, distance
        self.steer_index = best_index

    def handle_glide_input(self, segment, elapsed, can_shoot):
        points = segment["points"]
        if self.pawn_xy is None:
            return None, False, False
        self.advance_steer_index(segment)
        self.update_progress(elapsed)
        steering = self.active_points(segment)
        if self.steer_index >= len(steering) - STEER_LOOKAHEAD_POINTS and math.hypot(points[-1][0] - steering[-1][0], points[-1][1] - steering[-1][1]) > INPUT_ARRIVE_CM:
            self.wants_repath = True
        end = points[-1]
        remaining = math.hypot(end[0] - self.pawn_xy[0], end[1] - self.pawn_xy[1])
        timed_out = elapsed > segment.get("expected_seconds", 60.0) * INPUT_TIMEOUT_FACTOR + 2.0
        return None, False, remaining <= INPUT_ARRIVE_CM or timed_out

    def handle_glide(self, segment, elapsed, can_shoot):
        if self.drive_input:
            return self.handle_glide_input(segment, elapsed, can_shoot)
        lengths = self.cumulative[self.index]
        distance = elapsed * self.speed_cm_s
        if distance >= lengths[-1]:
            return tuple(segment["points"][-1]), False, True
        position = interpolate_polyline(segment["points"], lengths, distance)
        wants_shot = can_shoot and self.shot_period is not None and elapsed >= self.next_shot_number * self.shot_period
        if wants_shot:
            self.next_shot_number += 1
        return position, wants_shot, False

    def handle_teleport(self, segment, elapsed, can_shoot):
        if segment.get("api") == "game":
            self.pending_teleport = tuple(segment["position"])
            return None, False, True
        return tuple(segment["position"]), False, True

    def handle_stare(self, segment, elapsed, can_shoot):
        position = None if self.drive_input or segment.get("free") else tuple(segment["position"])
        times = segment["shot_times"]
        wants_shot = can_shoot and self.next_time_index < len(times) and elapsed >= times[self.next_time_index]
        if wants_shot:
            self.next_time_index += 1
        finished = elapsed >= segment["seconds"] and self.next_time_index >= len(times)
        return position, wants_shot, finished


def place_pawn(state, pawn, position):
    move_pawn(pawn, position)


def request_prestream(state, world, request, frame):
    location = request["position"]
    subsystem_class = getattr(unreal, "TDTexturePrestreamSubsystem", None)
    if subsystem_class is None:
        state["errors"].append("TDTexturePrestreamSubsystem missing (old build)")
        return
    subsystem_class.request_prestream_at_pawn_location(world, make_vector(location[0], location[1], location[2]), float(request["seconds"]))
    state["prestream_log"].append({"frame": frame, "position": list(location), "seconds": request["seconds"]})


def teleport_with_game_api(state, pawn, position, frame):
    pawn.teleport_pawn(make_vector(position[0], position[1], position[2]), unreal.Rotator(0.0, 0.0, 0.0))
    state["teleport_log"].append({"frame": frame, "position": list(position)})


def kill_player(state, pawn, frame):
    spec = unreal.TDDamageSpec()
    spec.set_editor_property("amount", PLAYER_KILL_DAMAGE)
    spec.set_editor_property("element", unreal.TDDamageElement.PHYSICAL)
    applied = unreal.TDCombatLibrary.try_apply_damage(pawn, spec)
    state["kill_log"].append({"frame": frame, "applied": float(applied)})


def densify(points, spacing_cm):
    dense = [points[0]]
    for start, end in zip(points, points[1:]):
        length = math.hypot(end[0] - start[0], end[1] - start[1])
        steps = max(1, int(length // spacing_cm))
        for step in range(1, steps + 1):
            ratio = step / steps
            dense.append(tuple(start[axis] + (end[axis] - start[axis]) * ratio for axis in range(3)))
    return dense


def repath(state, world, pawn, end, frame):
    start = pawn.get_actor_location()
    path = unreal.NavigationSystemV1.find_path_to_location_synchronously(world, start, make_vector(end[0], end[1], end[2]))
    points = [] if path is None else [(point.x, point.y, point.z) for point in path.get_editor_property("path_points")]
    machine = state["machine"]
    if len(points) < 2:
        machine.set_steer_points(None)
        state["repath_log"].append({"frame": frame, "points": 0})
        return
    machine.set_steer_points(densify(points, REPATH_SPACING_CM))
    state["repath_log"].append({"frame": frame, "points": len(points), "partial": bool(path.is_partial()), "last": list(points[-1])})


def steer_pawn(pawn, steer):
    length = math.hypot(steer[0], steer[1])
    if length < 1.0:
        return
    pawn.add_movement_input(make_vector(steer[0] / length, steer[1] / length, 0.0), 1.0, False)


def make_route_machine(request):
    machine = RouteMachine(request["segments"], request["speed_cm_s"], request["shot_hz"])
    machine.drive_input = request.get("drive") == "input"
    return machine


def make_vector(x, y, z):
    if unreal is None:
        return (x, y, z)
    return unreal.Vector(x, y, z)


def move_pawn(pawn, position):
    pawn.set_actor_location(make_vector(position[0], position[1], position[2]), False, True)


def write_json_atomic(path, value):
    temporary = path + ".tmp"
    with open(temporary, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(value, handle, ensure_ascii=False, indent=1)
    os.replace(temporary, path)


def read_project_directory():
    return unreal.SystemLibrary.get_project_directory()


def read_game_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def read_exec_world():
    world = read_game_world()
    if world is not None:
        return world
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def find_state():
    module = sys.modules.get(STATE_MODULE_NAME)
    return None if module is None else module.state


def format_cvar_value(value):
    if float(value) == int(value):
        return str(int(value))
    return repr(float(value))


def read_cvar(name):
    record = {}
    try:
        record["float"] = float(unreal.SystemLibrary.get_console_variable_float_value(name))
    except Exception as error:
        record["float_error"] = repr(error)
    try:
        record["int"] = int(unreal.SystemLibrary.get_console_variable_int_value(name))
    except Exception as error:
        record["int_error"] = repr(error)
    return record


def apply_cvars(state, world, pairs):
    for name, value in pairs:
        if name not in state["cvar_records"]:
            state["cvar_records"][name] = {"original": read_cvar(name), "applied": None, "read_back": None}
        unreal.SystemLibrary.execute_console_command(world, f"{name} {value}")
        state["cvar_records"][name]["applied"] = str(value)
        state["cvar_records"][name]["read_back"] = read_cvar(name)


def restore_cvars(state, world):
    for name, record in reversed(list(state["cvar_records"].items())):
        original = record["original"].get("float")
        if original is None:
            state["errors"].append(f"cvar not restored: {name}")
            continue
        try:
            unreal.SystemLibrary.execute_console_command(world, f"{name} {format_cvar_value(original)}")
            record["restored"] = format_cvar_value(original)
        except Exception as error:
            state["errors"].append(f"cvar restore {name}: {error!r}")


def load_performance_settings():
    return unreal.load_object(None, PERFORMANCE_SETTINGS_PATH)


def set_background_throttle(state, enabled_value):
    try:
        settings = load_performance_settings()
        if state["throttle_original"] is None:
            state["throttle_original"] = bool(settings.get_editor_property(THROTTLE_PROPERTY))
        settings.set_editor_property(THROTTLE_PROPERTY, enabled_value)
    except Exception as error:
        state["errors"].append(f"throttle not changed (keep editor in front): {error!r}")


def restore_background_throttle(state):
    if state["throttle_original"] is None:
        return
    load_performance_settings().set_editor_property(THROTTLE_PROPERTY, state["throttle_original"])


def write_status(state, status, **extra):
    payload = {"state": status, "request_id": state["request_id"], "started_real": state["started_real"], "updated_real": time.time()}
    payload.update(extra)
    write_json_atomic(os.path.join(state["run_dir"], "probe_status.json"), payload)


def write_frames_csv(state):
    with open(os.path.join(state["run_dir"], "frames.csv"), "w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(FRAME_COLUMNS)
        writer.writerows(state["rows"])


def write_result(state, outcome):
    result = {
        "request_id": state["request_id"], "state": outcome, "rows_count": len(state["rows"]), "shots": state["shots"],
        "cvars": state["cvar_records"], "viewport": state["viewport_changes"], "fov": state["fov"],
        "csv_start_frame": state["csv_start_frame"], "csv_stop_frame": state["csv_stop_frame"],
        "throttle_original": state["throttle_original"], "errors": state["errors"],
        "flush_seconds": state["flush_seconds"], "flush_end_frame": state["flush_end_frame"], "console_log": state["console_log"],
        "prestream_log": state["prestream_log"], "teleport_log": state["teleport_log"], "kill_log": state["kill_log"], "repath_log": state["repath_log"],
    }
    write_json_atomic(os.path.join(state["run_dir"], "result.json"), result)


def run_guarded(state, label, action):
    try:
        action()
    except Exception as error:
        state["errors"].append(f"{label}: {error!r}")


def release_callback(state):
    handle = state["handle"]
    state["handle"] = None
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)


def finish_probe(state, outcome):
    if state["finished"]:
        return
    state["finished"] = True
    run_guarded(state, "release_callback", lambda: release_callback(state))
    world = None
    try:
        world = read_exec_world()
    except Exception as error:
        state["errors"].append(f"world: {error!r}")
    if world is not None:
        run_guarded(state, "csvprofile stop", lambda: unreal.SystemLibrary.execute_console_command(world, "csvprofile stop"))
        run_guarded(state, "screen messages", lambda: unreal.SystemLibrary.execute_console_command(world, "EnableAllScreenMessages"))
        run_guarded(state, "restore_cvars", lambda: restore_cvars(state, world))
    run_guarded(state, "csv_stop_frame", lambda: state.__setitem__("csv_stop_frame", int(unreal.SystemLibrary.get_frame_count())))
    run_guarded(state, "restore_throttle", lambda: restore_background_throttle(state))
    run_guarded(state, "frames_csv", lambda: write_frames_csv(state))
    run_guarded(state, "result_json", lambda: write_result(state, outcome))
    run_guarded(state, "status_json", lambda: write_status(state, outcome, finished_real=time.time(), errors=state["errors"][:5]))


def read_camera_values(camera_manager):
    location = camera_manager.get_camera_location()
    rotation = camera_manager.get_camera_rotation()
    return location, rotation, float(camera_manager.get_fov_angle())


def record_viewport(state, world, frame):
    size = unreal.WidgetLayoutLibrary.get_viewport_size(world)
    current = [float(size.x), float(size.y)]
    if state["viewport_changes"] and state["viewport_changes"][-1]["size"] == current:
        return
    state["viewport_changes"].append({"frame": frame, "size": current})


def record_row(state, world, pawn, camera_manager, delta_seconds, now):
    frame = int(unreal.SystemLibrary.get_frame_count())
    location = pawn.get_actor_location()
    camera_location, camera_rotation, fov = read_camera_values(camera_manager)
    state["fov"] = fov
    record_viewport(state, world, frame)
    state["rows"].append((
        frame, now, time.time(), float(unreal.GameplayStatics.get_time_seconds(world)), float(delta_seconds),
        state["machine"].current_index(),
        location.x, location.y, location.z, camera_location.x, camera_location.y, camera_location.z,
        camera_rotation.pitch, camera_rotation.yaw, camera_rotation.roll, fov,
    ))
    return frame


def request_shot(state, world, shot, frame, now):
    shot_number = len(state["shots"])
    unreal.SystemLibrary.execute_console_command(world, build_shot_command(state["run_dir"], shot_number))
    state["shots"].append({
        "n": shot_number, "path": f"{state['run_dir']}/shots/shot_{shot_number:04d}.png", "request_frame": frame,
        "request_real_perf": now, "seg": shot["seg"], "seg_kind": shot["seg_kind"], "seg_time": shot["seg_time"],
    })


def run_segment_console(state, world, segment_index, command, frame):
    marker = f"seg{segment_index}_frame{frame}"
    unreal.log(f"GF07_CONSOLE_BEGIN {marker} {command}")
    unreal.SystemLibrary.execute_console_command(world, command)
    unreal.log(f"GF07_CONSOLE_END {marker}")
    state["console_log"].append({"seg": segment_index, "frame": frame, "command": command, "marker": marker})


def process_tick(state, delta_seconds):
    world = read_game_world()
    if world is None:
        state["errors"].append("PIE world ended")
        state["fatal"] = True
        finish_probe(state, "failed")
        return
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    camera_manager = unreal.GameplayStatics.get_player_camera_manager(world, 0)
    if pawn is None or camera_manager is None:
        state["errors"].append("pawn or camera manager missing")
        state["fatal"] = True
        finish_probe(state, "failed")
        return
    now = time.perf_counter()
    frame = record_row(state, world, pawn, camera_manager, delta_seconds, now)
    end_streaming_flush(state, world, now)
    if state["drain_until"] is not None:
        if now >= state["drain_until"]:
            finish_probe(state, "complete")
        return
    location = pawn.get_actor_location()
    state["machine"].pawn_xy = (location.x, location.y)
    action = state["machine"].tick(now)
    if action["position"] is not None:
        place_pawn(state, pawn, action["position"])
    if action["repath"] is not None:
        repath(state, world, pawn, action["repath"], frame)
    if action["steer"] is not None:
        steer_pawn(pawn, action["steer"])
    if action["prestream"] is not None:
        request_prestream(state, world, action["prestream"], frame)
    if action["teleport"] is not None:
        teleport_with_game_api(state, pawn, action["teleport"], frame)
    if action["kill"]:
        kill_player(state, pawn, frame)
    for segment_index, command in action["console"]:
        run_segment_console(state, world, segment_index, command, frame)
    if action["shot"] is not None:
        request_shot(state, world, action["shot"], frame, now)
    if action["done"]:
        state["drain_until"] = now + FINISH_GRACE_SECONDS
    if time.time() - state["last_status_real"] >= STATUS_WRITE_INTERVAL_SECONDS:
        state["last_status_real"] = time.time()
        write_status(state, "running", seg=state["machine"].current_index(), shots=len(state["shots"]), rows=len(state["rows"]))


def begin_streaming_flush(state, world, flush_seconds):
    if flush_seconds <= 0.0:
        return
    apply_cvars(state, world, [(DROP_MIPS_VARIABLE, DROP_CACHED_AND_HIDDEN_MIPS)])
    state["flush_seconds"] = flush_seconds


def end_streaming_flush(state, world, now):
    if state["flush_seconds"] <= 0.0 or state["flush_ended"]:
        return
    if state["flush_started"] is None:
        state["flush_started"] = now
        return
    if now - state["flush_started"] < state["flush_seconds"]:
        return
    original = state["cvar_records"][DROP_MIPS_VARIABLE]["original"].get("float")
    unreal.SystemLibrary.execute_console_command(world, f"{DROP_MIPS_VARIABLE} {format_cvar_value(original if original is not None else 0.0)}")
    state["flush_ended"] = True
    state["flush_end_frame"] = int(unreal.SystemLibrary.get_frame_count())


def make_tick(state):
    def tick(delta_seconds):
        if state["handle"] is None or state["finished"]:
            return
        try:
            process_tick(state, delta_seconds)
        except Exception as error:
            state["errors"].append(f"tick: {error!r}")
            state["fatal"] = True
            finish_probe(state, "failed")
    return tick


def build_state(request):
    run_dir = request["run_dir"]
    return {
        "request_id": request["request_id"], "run_dir": run_dir, "started_real": time.time(), "handle": None, "finished": False,
        "fatal": False, "errors": [], "rows": [], "shots": [], "cvar_records": {}, "viewport_changes": [], "fov": None,
        "throttle_original": None, "csv_start_frame": None, "csv_stop_frame": None, "drain_until": None, "last_status_real": 0.0,
        "flush_seconds": 0.0, "flush_started": None, "flush_ended": False, "flush_end_frame": None, "console_log": [],
        "prestream_log": [], "teleport_log": [], "kill_log": [], "repath_log": [],
        "machine": make_route_machine(request),
    }


def replace_previous_state():
    previous = find_state()
    if previous is None:
        return
    sys.modules.pop(STATE_MODULE_NAME, None)
    if previous["finished"]:
        return
    previous["errors"].append("replaced by a new probe run")
    previous["fatal"] = True
    finish_probe(previous, "failed")


def start_probe(request_path):
    with open(request_path, encoding="utf-8") as handle:
        request = json.load(handle)
    replace_previous_state()
    os.makedirs(os.path.join(request["run_dir"], "shots"), exist_ok=True)
    state = build_state(request)
    world = read_game_world()
    if world is None:
        state["errors"].append("PIE world missing")
        state["fatal"] = True
        finish_probe(state, "failed")
        print("FAILED: PIE world missing")
        return
    module = types.ModuleType(STATE_MODULE_NAME)
    module.state = state
    sys.modules[STATE_MODULE_NAME] = module
    try:
        apply_cvars(state, world, request.get("default_cvars", []) + request.get("cvars", []))
        begin_streaming_flush(state, world, float(request.get("flush_streaming_seconds", 0.0)))
        set_background_throttle(state, False)
        state["csv_start_frame"] = int(unreal.SystemLibrary.get_frame_count())
        unreal.SystemLibrary.execute_console_command(world, "csvprofile start")
        unreal.SystemLibrary.execute_console_command(world, "DisableAllScreenMessages")
        state["handle"] = unreal.register_slate_post_tick_callback(make_tick(state))
        write_status(state, "running")
    except Exception as error:
        state["errors"].append(f"start: {error!r}")
        state["fatal"] = True
        finish_probe(state, "failed")
    print(f"probe started: {request['request_id']}")


def abort_probe():
    state = find_state()
    if state is None or state["finished"]:
        print("abort: nothing running")
        return
    state["errors"].append("aborted by request")
    state["fatal"] = True
    finish_probe(state, "failed")
    print("probe aborted")


def run_probe_mode():
    active_path = os.path.join(read_project_directory(), *ACTIVE_REQUEST_PARTS)
    with open(active_path, encoding="utf-8") as handle:
        line = handle.readline().strip()
    if line == ABORT_TOKEN:
        abort_probe()
        return
    start_probe(line)


class FakePawn:
    def __init__(self):
        self.locations = []

    def set_actor_location(self, location, sweep, teleport):
        self.locations.append(location)


def build_points(start, end, spacing):
    length = math.hypot(end[0] - start[0], end[1] - start[1])
    count = max(1, int(math.ceil(length / spacing)))
    return [[start[0] + (end[0] - start[0]) * i / count, start[1] + (end[1] - start[1]) * i / count, 0.0] for i in range(count + 1)]


def simulate(segments, speed, shot_hz, tick_seconds):
    machine = RouteMachine(segments, speed, shot_hz)
    pawn = FakePawn()
    clock = 100.0
    shots = []
    shots_per_tick = []
    ticks = 0
    while not machine.is_finished() and ticks < 200000:
        action = machine.tick(clock)
        if action["position"] is not None:
            move_pawn(pawn, action["position"])
        shots_per_tick.append(0 if action["shot"] is None else 1)
        if action["shot"] is not None:
            shots.append(dict(action["shot"], tick=ticks, clock=clock - 100.0))
        clock += tick_seconds
        ticks += 1
    return shots, pawn, ticks


def self_test():
    failures = []

    def check(condition, message):
        if not condition:
            failures.append(message)

    speed = 550.0
    shot_hz = 8.0
    glide_length = 3000.0
    stare_times = [0.25, 1.0, 2.0]
    segments = [
        {"kind": "settle", "seconds": 3.0, "position": [-11000.0, 2000.0, 100.0]},
        {"kind": "glide", "points": build_points([-11000.0, 2000.0], [-11000.0 - glide_length, 2000.0], 25.0)},
        {"kind": "stare", "seconds": 2.0, "shot_times": stare_times, "position": [-14000.0, 2000.0, 100.0]},
    ]
    tick_seconds = 1.0 / 60.0
    shots, pawn, ticks = simulate(segments, speed, shot_hz, tick_seconds)
    settle_shots = [s for s in shots if s["seg_kind"] == "settle"]
    glide_shots = [s for s in shots if s["seg_kind"] == "glide"]
    stare_shots = [s for s in shots if s["seg_kind"] == "stare"]
    glide_seconds = glide_length / speed
    expected_glide = math.floor(glide_seconds * shot_hz)
    check(len(settle_shots) == 1 and 2.7 <= settle_shots[0]["seg_time"] <= 3.0, f"settle shots {len(settle_shots)}")
    check(abs(len(glide_shots) - expected_glide) <= 1, f"glide shots {len(glide_shots)} expected {expected_glide}+-1")
    check(len(stare_shots) == len(stare_times), f"stare shots {len(stare_shots)}")
    for shot, wanted in zip(stare_shots, stare_times):
        check(wanted <= shot["seg_time"] < wanted + tick_seconds + 1e-6, f"stare time {shot['seg_time']} vs {wanted}")
    check(len(set(s["tick"] for s in shots)) == len(shots), "two shots on one tick")
    xs = [location[0] for location in pawn.locations]
    check(abs(min(xs) - (-11000.0 - glide_length)) < 1e-6, f"glide end x {min(xs)}")
    glide_xs = [x for x in xs if x < -11000.0]
    check(all(a >= b - 1e-6 for a, b in zip(glide_xs, glide_xs[1:])), "glide x not monotonic")
    check(len(shots) == len(settle_shots) + len(glide_shots) + len(stare_shots), "shot total")

    coarse_segments = [{"kind": "stare", "seconds": 1.0, "shot_times": [0.05, 0.15, 0.3], "position": [0.0, 0.0, 0.0]}]
    coarse_shots, _, _ = simulate(coarse_segments, speed, shot_hz, 0.5)
    check(len(coarse_shots) == 3, f"coarse stare shots {len(coarse_shots)}")
    check(len(set(s["tick"] for s in coarse_shots)) == 3, "coarse shots share a tick")

    teleport_segments = [
        {"kind": "teleport", "position": [1.0, 2.0, 3.0]},
        {"kind": "stare", "seconds": 0.5, "shot_times": [0.5], "position": [1.0, 2.0, 3.0]},
    ]
    teleport_shots, teleport_pawn, _ = simulate(teleport_segments, speed, shot_hz, tick_seconds)
    check(len(teleport_shots) == 1 and teleport_pawn.locations[0] == (1.0, 2.0, 3.0), "teleport then stare")

    points = [[0.0, 0.0, 0.0], [100.0, 0.0, 10.0], [100.0, 100.0, 30.0]]
    lengths = build_cumulative_lengths(points)
    middle = interpolate_polyline(points, lengths, 150.0)
    check(abs(middle[0] - 100.0) < 1e-9 and abs(middle[1] - 50.0) < 1e-9 and abs(middle[2] - 20.0) < 1e-9, f"interpolation {middle}")
    check(build_shot_command("C:/x/run", 7) == "Shot filename=C:/x/run/shots/shot_0007 -nosuffix", "shot command")

    print(f"glide {glide_seconds:.3f}s shots={len(glide_shots)} expected={expected_glide} stare_shots={len(stare_shots)} ticks={ticks}")
    for failure in failures:
        print("FAIL:", failure)
    print("SELF-TEST", "FAILED" if failures else "PASSED")
    return 1 if failures else 0


if unreal is not None:
    run_probe_mode()
elif __name__ == "__main__":
    if "--self-test" not in sys.argv:
        print(__doc__)
        sys.exit(2)
    sys.exit(self_test())
