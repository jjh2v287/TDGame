"""에디터 안에서 실행(PIE 실행 중): 요청 파일의 동작(move·shot·occluders·walk)을 PIE 폰에 수행하고 결과 JSON을 쓴다(P3-17 탑다운 시각 점검).
실행: python Tools/run_in_editor.py Tools/WorldGen/editor_pie_probe.py (PowerShell, 요청은 Saved/WorldGen/pie_probe_request.json, 보통 pie_capture_spots.py·pie_walk_passages.py가 쓴다)
출력: 요청의 result_path(기본 Saved/WorldGen/pie_probe_result.json)에 결과 JSON, 표준 출력에 한 줄 요약. walk는 요청한 초가 지난 뒤에 결과를 쓴다(비동기)
상태: 현행 (2026-10-14)

요청 공통: action, request_id(결과에 그대로 돌려줌), result_path(선택). 결과 공통: ok, error, request_id, action.
move: x, y, z(없으면 지면을 추적해 clearance_cm(기본 150) 위), yaw(선택), refine(true면 이동 뒤 지면에 다시 붙임)
shot: path(PNG 경로, 공백 없음), width(1600), height(900). HighResShot은 비동기라 PNG는 잠시 뒤에 생긴다.
occluders: sweep_radius_cm(30), chest_height_cm(120), camera_overlap_radius_cm(15). 카메라→폰 가슴 구 쓸기 다중 히트(지형 제외)와 카메라 점 매몰 검사.
walk: yaw, seconds, forward(기본 true), start([x, y, z] 또는 null, z가 null이면 지면 추적), realm_id, gate_id, hitch_ms(100). 프레임 시간(frame_ms=월드 실시간 차이, slate_ms=틱 인수)을 표본마다 기록한다.
"""
import json
import math
import os
import time

import unreal

PAWN_CENTER_HEIGHT_CM = 96.0
DEFAULT_CLEARANCE_CM = 150.0
DEFAULT_SWEEP_RADIUS_CM = 30.0
DEFAULT_CHEST_HEIGHT_CM = 120.0
DEFAULT_CAMERA_OVERLAP_RADIUS_CM = 15.0
DEFAULT_SHOT_WIDTH = 1600
DEFAULT_SHOT_HEIGHT = 900
DEFAULT_HITCH_MS = 100.0
STREAM_WAIT_SECONDS = 60.0
WALKABLE_NORMAL_Z_MIN = 0.5
FRAME_LOG_INTERVAL_SECONDS = 0.5
CROSSING_JUMP_CM = 5000.0
GROUND_TRACE_TOP_Z_CM = 60000.0
GROUND_TRACE_BOTTOM_Z_CM = -60000.0
CAMERA_RAY_LENGTH_CM = 30000.0
INSIDE_NORMAL_Z_MIN = 0.05
WRITE_RETRIES = 5
WRITE_RETRY_SECONDS = 2.0

HIT_START_INSIDE = 1
HIT_DISTANCE = 3
HIT_IMPACT_POINT = 5
HIT_IMPACT_NORMAL = 7
HIT_ACTOR = 9
HIT_COMPONENT = 10
HIT_ITEM = 13

OBJECT_TYPES = [unreal.ObjectTypeQuery.ECC_WORLD_STATIC, unreal.ObjectTypeQuery.ECC_WORLD_DYNAMIC]
VISIBILITY = unreal.TraceTypeQuery.ECC_VISIBILITY
NO_DEBUG = unreal.DrawDebugTrace.NONE

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
project_dir = unreal.SystemLibrary.get_project_directory()
REQUEST_PATH = os.path.join(project_dir, "Saved", "WorldGen", "pie_probe_request.json")
DEFAULT_RESULT_PATH = os.path.join(project_dir, "Saved", "WorldGen", "pie_probe_result.json")


def resolve_path(path):
    if os.path.isabs(path):
        return path
    return os.path.join(project_dir, path)


def write_json(path, payload):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    temp_path = path + ".tmp"
    last_error = None
    for _ in range(WRITE_RETRIES):
        try:
            with open(temp_path, "w", encoding="utf-8", newline="\n") as handle:
                json.dump(payload, handle, ensure_ascii=False, indent=1)
            os.replace(temp_path, path)
            return
        except OSError as error:
            last_error = error
            time.sleep(WRITE_RETRY_SECONDS)
    raise last_error


def vec(x, y, z):
    return unreal.Vector(float(x), float(y), float(z))


def vec_list(vector):
    return [round(vector.x, 1), round(vector.y, 1), round(vector.z, 1)]


def actor_label(actor):
    if actor is None:
        return None
    try:
        return actor.get_actor_label()
    except Exception:
        return actor.get_name()


def component_mesh_name(component):
    if component is None or not hasattr(component, "static_mesh"):
        return None
    mesh = component.get_editor_property("static_mesh")
    return mesh.get_name() if mesh else None


def describe_hit(hit):
    values = hit.to_tuple()
    actor = values[HIT_ACTOR]
    component = values[HIT_COMPONENT]
    return {
        "start_inside": bool(values[HIT_START_INSIDE]),
        "dist": round(float(values[HIT_DISTANCE]), 1),
        "impact": vec_list(values[HIT_IMPACT_POINT]),
        "normal": vec_list(values[HIT_IMPACT_NORMAL]),
        "actor": actor_label(actor),
        "actor_class": actor.get_class().get_name() if actor else None,
        "comp": component.get_name() if component else None,
        "comp_class": component.get_class().get_name() if component else None,
        "item": int(values[HIT_ITEM]),
        "mesh": component_mesh_name(component),
    }


def is_landscape(description):
    return "Landscape" in (description.get("actor_class") or "") + (description.get("comp_class") or "")


def find_ground_z(x, y, pawn):
    hits = unreal.SystemLibrary.line_trace_multi(world, vec(x, y, GROUND_TRACE_TOP_Z_CM), vec(x, y, GROUND_TRACE_BOTTOM_Z_CM), VISIBILITY, True, [pawn], NO_DEBUG, True) or []
    for hit in hits:
        description = describe_hit(hit)
        if is_landscape(description):
            return description["impact"][2]
    return None


def find_support_z(x, y, pawn):
    hits = unreal.SystemLibrary.line_trace_multi(world, vec(x, y, GROUND_TRACE_TOP_Z_CM), vec(x, y, GROUND_TRACE_BOTTOM_Z_CM), VISIBILITY, True, [pawn], NO_DEBUG, True) or []
    for hit in hits:
        description = describe_hit(hit)
        if description["comp_class"] != "BrushComponent" and description["normal"][2] > WALKABLE_NORMAL_Z_MIN:
            return description["impact"][2]
    return None


def sweep_hits(start, end, radius, ignored_actors):
    hits = unreal.SystemLibrary.sphere_trace_multi_for_objects(world, start, end, radius, OBJECT_TYPES, True, ignored_actors, NO_DEBUG, True) or []
    descriptions = (describe_hit(hit) for hit in hits)
    return [description for description in descriptions if description["comp_class"] != "BrushComponent"]


def camera_state():
    manager = unreal.GameplayStatics.get_player_camera_manager(world, 0)
    location = manager.get_camera_location()
    rotation = manager.get_camera_rotation()
    return {
        "location": location,
        "report": {"loc": vec_list(location), "rot": [round(rotation.pitch, 1), round(rotation.yaw, 1), round(rotation.roll, 1)], "fov": round(manager.get_fov_angle(), 1)},
    }


def camera_ray_inside_meshes(camera_location, pawn):
    top = vec(camera_location.x, camera_location.y, camera_location.z + CAMERA_RAY_LENGTH_CM)
    hits = unreal.SystemLibrary.line_trace_multi_for_objects(world, camera_location, top, OBJECT_TYPES, True, [pawn], NO_DEBUG, True) or []
    inside = []
    for hit in hits:
        description = describe_hit(hit)
        if description["comp_class"] == "BrushComponent":
            continue
        if description["normal"][2] > INSIDE_NORMAL_Z_MIN or description["start_inside"]:
            inside.append(description)
    return inside


def reduce_hit(description):
    return {key: description[key] for key in ("actor", "actor_class", "comp", "comp_class", "mesh", "item")}


def merge_occluders(hits_with_camera_distance):
    merged = {}
    for description, camera_distance in hits_with_camera_distance:
        if is_landscape(description):
            continue
        key = (description["actor"], description["comp"], description["item"])
        entry = merged.get(key)
        if entry is None:
            entry = dict(reduce_hit(description), dist_from_camera_cm=camera_distance, start_inside=description["start_inside"])
            merged[key] = entry
            continue
        entry["dist_from_camera_cm"] = min(entry["dist_from_camera_cm"], camera_distance)
        entry["start_inside"] = entry["start_inside"] or description["start_inside"]
    return sorted(merged.values(), key=lambda entry: entry["dist_from_camera_cm"])


def action_move(request, pawn):
    x = float(request["x"])
    y = float(request["y"])
    clearance = float(request.get("clearance_cm", DEFAULT_CLEARANCE_CM))
    explicit_z = request.get("z")
    ground_z = None
    start_z = explicit_z
    if explicit_z is None:
        ground_z = find_ground_z(x, y, pawn)
        start_z = pawn.get_actor_location().z if ground_z is None else ground_z + clearance
    pawn.set_actor_location(vec(x, y, start_z), False, True)
    if request.get("refine"):
        refined_z = find_ground_z(x, y, pawn)
        if refined_z is not None:
            ground_z = refined_z
            pawn.set_actor_location(vec(x, y, refined_z + clearance), False, True)
    if request.get("yaw") is not None:
        pawn.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=float(request["yaw"])), True)
    return {"pawn": vec_list(pawn.get_actor_location()), "ground_z": None if ground_z is None else round(ground_z, 1)}


def action_shot(request, pawn):
    path = resolve_path(request["path"]).replace("\\", "/")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    width = int(request.get("width", DEFAULT_SHOT_WIDTH))
    height = int(request.get("height", DEFAULT_SHOT_HEIGHT))
    unreal.SystemLibrary.execute_console_command(world, f"HighResShot {width}x{height} filename={path}")
    return {"path": path, "pawn": vec_list(pawn.get_actor_location()), "camera": camera_state()["report"]}


def action_occluders(request, pawn):
    radius = float(request.get("sweep_radius_cm", DEFAULT_SWEEP_RADIUS_CM))
    chest_height = float(request.get("chest_height_cm", DEFAULT_CHEST_HEIGHT_CM))
    overlap_radius = float(request.get("camera_overlap_radius_cm", DEFAULT_CAMERA_OVERLAP_RADIUS_CM))
    pawn_location = pawn.get_actor_location()
    ground_z = find_ground_z(pawn_location.x, pawn_location.y, pawn)
    if ground_z is None:
        ground_z = pawn_location.z - PAWN_CENTER_HEIGHT_CM
    chest = vec(pawn_location.x, pawn_location.y, ground_z + chest_height)
    camera = camera_state()
    camera_location = camera["location"]
    line_length = (chest - camera_location).length()
    forward = [(description, description["dist"]) for description in sweep_hits(camera_location, chest, radius, [pawn])]
    backward = [(description, max(0.0, line_length - description["dist"])) for description in sweep_hits(chest, camera_location, radius, [pawn])]
    occluders = merge_occluders(forward + backward)
    overlap_probe_end = vec(camera_location.x, camera_location.y, camera_location.z + 1.0)
    overlaps = [reduce_hit(description) for description in sweep_hits(camera_location, overlap_probe_end, overlap_radius, [pawn]) if not is_landscape(description)]
    ray_inside = [reduce_hit(description) for description in camera_ray_inside_meshes(camera_location, pawn)]
    terrain_z = find_ground_z(camera_location.x, camera_location.y, pawn)
    under_terrain = terrain_z is not None and terrain_z > camera_location.z
    buried_in_mesh = bool(overlaps) or bool(ray_inside)
    return {
        "pawn": vec_list(pawn_location),
        "ground_z": round(ground_z, 1),
        "chest": vec_list(chest),
        "camera": camera["report"],
        "camera_above_ground_cm": round(camera_location.z - ground_z, 1),
        "occluder_count": len(occluders),
        "occluders": occluders,
        "camera_inside": {
            "overlap_radius_cm": overlap_radius,
            "overlap_meshes": overlaps,
            "ray_inside_meshes": ray_inside,
            "terrain_z_at_camera": None if terrain_z is None else round(terrain_z, 1),
            "under_terrain": under_terrain,
            "buried_in_mesh": buried_in_mesh,
            "buried": buried_in_mesh or under_terrain,
        },
    }


def read_real_seconds():
    try:
        return float(unreal.GameplayStatics.get_real_time_seconds(world))
    except Exception:
        return time.monotonic()


def find_travel_subsystem():
    travel_class = getattr(unreal, "TDSeamlessTravelSubsystem", None)
    if travel_class is None:
        return None
    return travel_class.get_seamless_travel_subsystem(world)


def find_realm_subsystem():
    realm_class = getattr(unreal, "TDRealmSubsystem", None)
    if realm_class is None:
        return None
    return realm_class.get_realm_subsystem(world)


def place_walk_start(start, yaw_degrees, sign, pawn):
    z = start[2]
    if z is None:
        ground_z = find_ground_z(start[0], start[1], pawn)
        z = pawn.get_actor_location().z if ground_z is None else ground_z + DEFAULT_CLEARANCE_CM
    pawn.set_actor_location(vec(start[0], start[1], z), False, True)
    facing_yaw = yaw_degrees + (0.0 if sign > 0 else 180.0)
    pawn.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=facing_yaw), True)


def start_walk(request, pawn, result_path, base):
    yaw_degrees = float(request["yaw"])
    seconds = float(request["seconds"])
    sign = 1.0 if request.get("forward", True) else -1.0
    hitch_ms = float(request.get("hitch_ms", DEFAULT_HITCH_MS))
    realm_id = request.get("realm_id")
    yaw_radians = math.radians(yaw_degrees)
    direction = vec(math.cos(yaw_radians) * sign, math.sin(yaw_radians) * sign, 0.0)
    hold = {"start": request.get("start"), "elapsed": 0.0}
    if hold["start"]:
        place_walk_start(hold["start"], yaw_degrees, sign, pawn)
    travel = find_travel_subsystem()
    realm_subsystem = find_realm_subsystem()
    state = {"t": 0.0, "walked_cm": 0.0, "frames": [], "crossings": [], "frame_ms": [], "slate_ms": [], "hitches": [], "done": False, "stream_wait_s": 0.0}
    previous = {"loc": None, "real": None}
    handle_box = {}

    def travel_state_name():
        return str(travel.get_travel_state()) if travel else "None"

    def realm_weight():
        if realm_subsystem is None or not realm_id:
            return -1.0
        return realm_subsystem.get_realm_weight(realm_id)

    def finish(error=None):
        state["done"] = True
        unreal.unregister_slate_post_tick_callback(handle_box["handle"])
        payload = dict(
            base,
            ok=error is None,
            error=error,
            gate_id=request.get("gate_id"),
            realm_id=realm_id,
            forward=sign > 0,
            seconds=seconds,
            hitch_ms=hitch_ms,
            t=round(state["t"], 3),
            walked_cm=round(state["walked_cm"], 1),
            frames=state["frames"],
            crossings=state["crossings"],
            frame_ms=state["frame_ms"],
            slate_ms=state["slate_ms"],
            hitches=state["hitches"],
            stream_wait_s=round(state["stream_wait_s"], 2),
        )
        write_json(result_path, payload)

    def record_movement(location):
        last_location = previous["loc"]
        previous["loc"] = location
        if last_location is None:
            return
        if (location - last_location).length() > CROSSING_JUMP_CM:
            state["crossings"].append({"t": round(state["t"], 3), "from": vec_list(last_location), "to": vec_list(location), "state": travel_state_name()})
            return
        state["walked_cm"] += math.hypot(location.x - last_location.x, location.y - last_location.y)

    def record_frame_time(delta_seconds, location):
        real_seconds = read_real_seconds()
        last_real = previous["real"]
        previous["real"] = real_seconds
        if last_real is None:
            return
        frame_ms = (real_seconds - last_real) * 1000.0
        slate_ms = delta_seconds * 1000.0
        state["frame_ms"].append(round(frame_ms, 2))
        state["slate_ms"].append(round(slate_ms, 2))
        if frame_ms <= hitch_ms:
            return
        state["hitches"].append({
            "t": round(state["t"], 2),
            "frame_ms": round(frame_ms, 1),
            "slate_ms": round(slate_ms, 1),
            "loc": vec_list(location),
            "walked_cm": round(state["walked_cm"], 1),
            "state": travel_state_name(),
            "w": round(realm_weight(), 3),
        })

    def record_progress_frame(location, pawn_velocity):
        frames = state["frames"]
        if frames and state["t"] - frames[-1]["t"] < FRAME_LOG_INTERVAL_SECONDS:
            return
        frames.append({"t": round(state["t"], 2), "loc": [round(location.x), round(location.y), round(location.z)], "state": travel_state_name(), "w": round(realm_weight(), 3), "speed": round(pawn_velocity.length(), 1)})

    def hold_until_ground_loaded(delta_seconds):
        start = hold["start"]
        hold["elapsed"] += delta_seconds
        ground_z = find_support_z(start[0], start[1], pawn)
        if ground_z is None:
            if hold["elapsed"] > STREAM_WAIT_SECONDS:
                finish(f"ground not loaded under walk start after {STREAM_WAIT_SECONDS:.0f}s")
                return True
            held_z = start[2] if start[2] is not None else pawn.get_actor_location().z
            pawn.set_actor_location(vec(start[0], start[1], held_z), False, True)
            return True
        pawn.set_actor_location(vec(start[0], start[1], ground_z + DEFAULT_CLEARANCE_CM), False, True)
        state["stream_wait_s"] = hold["elapsed"]
        hold["start"] = None
        return True

    def step(delta_seconds):
        if hold["start"] and hold_until_ground_loaded(delta_seconds):
            return
        state["t"] += delta_seconds
        location = pawn.get_actor_location()
        record_movement(location)
        record_frame_time(delta_seconds, location)
        record_progress_frame(location, pawn.get_velocity())
        if state["t"] >= seconds:
            finish()
            return
        pawn.add_movement_input(direction, 1.0, False)

    def tick(delta_seconds):
        if state["done"]:
            return
        try:
            step(delta_seconds)
        except Exception as error:
            finish(repr(error))

    handle_box["handle"] = unreal.register_slate_post_tick_callback(tick)
    print("walking", request.get("gate_id"), "seconds", seconds, "forward", sign > 0)


ACTIONS = {"move": action_move, "shot": action_shot, "occluders": action_occluders}


def get_pawn():
    if world is None:
        return None
    return unreal.GameplayStatics.get_player_pawn(world, 0)


def report_failure(result_path, base, message):
    print("pie_probe failed:", message)
    write_json(result_path, dict(base, ok=False, error=message))


def main():
    with open(REQUEST_PATH, encoding="utf-8") as handle:
        request = json.load(handle)
    result_path = resolve_path(request.get("result_path") or DEFAULT_RESULT_PATH)
    action = request.get("action")
    base = {"request_id": request.get("request_id"), "action": action}
    pawn = get_pawn()
    if pawn is None:
        report_failure(result_path, base, "PIE world or pawn missing")
        return
    if action == "walk":
        try:
            start_walk(request, pawn, result_path, base)
        except Exception as error:
            report_failure(result_path, base, repr(error))
        return
    handler = ACTIONS.get(action)
    if handler is None:
        report_failure(result_path, base, f"unknown action: {action}")
        return
    try:
        payload = handler(request, pawn)
    except Exception as error:
        report_failure(result_path, base, repr(error))
        return
    write_json(result_path, dict(base, ok=True, error=None, **payload))
    print("pie_probe", action, "ok", payload.get("pawn"))


main()
