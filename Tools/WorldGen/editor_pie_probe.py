"""에디터 안에서 실행(PIE 실행 중): 요청 파일의 동작(move·shot·occluders·walk·ride·console·command)을 PIE 폰에 수행하고 결과 JSON을 쓴다(P3-17 탑다운 시각 점검, P3-19 연속 세계 연결로 걷기·승강기 운행).
실행: python Tools/run_in_editor.py Tools/WorldGen/editor_pie_probe.py (PowerShell, 요청은 Saved/WorldGen/pie_probe_request.json, 보통 pie_capture_spots.py·pie_walk_passages.py가 쓴다)
출력: 요청의 result_path(기본 Saved/WorldGen/pie_probe_result.json)에 결과 JSON(압축, walk·ride는 매 프레임 기록), 표준 출력에 한 줄 요약. walk·ride는 끝난 뒤에 결과를 쓴다(비동기)
상태: 현행 (2026-10-15)

요청 공통: action, request_id(결과에 그대로 돌려줌), result_path(선택). 결과 공통: ok, error, request_id, action.
move: x, y, z(없으면 지면을 추적해 clearance_cm(기본 150) 위), yaw(선택), refine(true면 이동 뒤 지면에 다시 붙임)
shot: path(PNG 경로, 공백 없음), width(1600), height(900). HighResShot은 비동기라 PNG는 잠시 뒤에 생긴다.
occluders: sweep_radius_cm(30), chest_height_cm(120), camera_overlap_radius_cm(15). 카메라→폰 가슴 구 쓸기 다중 히트(지형 제외)와 카메라 점 매몰 검사.
walk: path([[x, y], ...] 경유점) 또는 yaw(+forward, 한 방향), seconds(시간 한도), start([x, y, z|null] 또는 null = 현재 위치), gate_id, realm_id, to_entry(준비 여부를 읽는 방향, 기본 forward), waypoint_radius_cm(150), hitch_ms(100). path면 실제 이동 입력(add_movement_input)으로 다음 경유점을 향해 걷고(점 반경 안이거나 구간 끝을 지나면 다음 점) 마지막 점 반경 안에 들어오면 reached=true로 끝낸다.
매 프레임 기록(walk·ride 공통 frames): t, gt(게임 시각), pawn·cam(월드 cm), w(관문 이계 가중치), ready(IsReadyFor(gate_id, to_entry)), spd(수평 속도), max(최대 속도 설정, 감속 배율 반영), fade(카메라 관리자 fade_amount, 읽을 수 없으면 null). 이동 상태 변화는 state_events, 시작 배치(start) 뒤 폰 둘레 격자 로딩 범위 칸이 모두 Loaded 이상(WorldPartitionSubsystem.is_streaming_completed)이고 카메라가 10프레임 연속 20cm 이하로 움직이며 틱이 100ms 이하가 될 때까지(최대 30초, settle_s·settle_streaming_loaded) 기록하지 않는다. 시작 바닥은 시작점과 ±30·60cm 8점 중 처음 걸을 수 있는 면. 프레임 사이 폰·카메라 이동이 5000cm를 넘으면 jumps·camera_jumps(연속 세계에서는 실패)에 남긴다. frame_ms = 월드 실시간 차이, slate_ms = 틱 인수, hitch_ms 초과는 hitches.
ride: gate_id, realm_id, to_entry, start([x, y, z|null] 또는 null), path(덱 쪽으로 가기 전 걷는 경유점, 선택), board_landing·arrive_landing([x, y, z] 덱 윗면 중심), wait_radius_cm(450), exit_path(도착 뒤 내려서 걷는 경유점, 선택), ring_module_height_cm(450), seconds(시간 한도), after_seconds(3), hitch_ms(100). 단계 path → approach(덱 쪽으로 걷다 wait_radius 안에서 덱이 탑승 착지에 없으면 call로 기다림) → board(덱 중심 60cm 안) → ride(입력 없음, 도착 착지에서 덱이 0.3초 멈추면 도착) → exit → after. 프레임마다 공통 항목에 ph(단계)·deck(x, y, 윗면 z)·dv(덱 수직 속도)·feet(발-덱 높이차)·air·mode·base(이동 기반)·ring_v(링 수직 속도)·ring_z0(Ring0 z)를 더하고, 덱 액터의 폰을 막는 상자 목록 변화를 box_events에 남긴다.
덱 윗면 z = 시작 때 한 번 액터 축에서 액터·두 착지 높이 ±1000cm 범위 객체 추적으로 그 액터의 가장 높은 위 향 면(상자·구·캡슐 구성 요소 제외)을 맞힌 구성 요소의 월드 z + 차이(trace). 맞지 않으면 액터 z + 덱이 쉬고 있는 착지 높이와의 차(landing), 그것도 아니면 액터 z(pivot). 링 = 덱에 가장 가까운 TDRealmShaftLiner의 Ring<N> 구성 요소, ring_v = 모듈 높이 절반 미만으로 움직인 링들의 프레임 사이 z 변화 중앙값 / 게임 시간 차이(감쌈 이동 제외). 라이너가 없으면 null.
console: variable, value. 콘솔 변수를 '<variable> <value>'로 설정하고 GetConsoleVariableFloatValue로 다시 읽어 read_back으로 돌려준다(PIE가 없어도 실행한다).
command: command. 허용 목록(csvprofile start·stop, memreport -full) 콘솔 명령을 PIE 월드에서 실행한다(성능·메모리 측정 K16).
"""
import json
import math
import os
import re
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
JUMP_CM = 5000.0
GROUND_TRACE_TOP_Z_CM = 60000.0
GROUND_TRACE_BOTTOM_Z_CM = -60000.0
CAMERA_RAY_LENGTH_CM = 30000.0
INSIDE_NORMAL_Z_MIN = 0.05
WRITE_RETRIES = 5
WRITE_RETRY_SECONDS = 2.0
DEFAULT_WAYPOINT_RADIUS_CM = 150.0
DEFAULT_CAPSULE_HALF_HEIGHT_CM = 88.0
DEFAULT_WAIT_RADIUS_CM = 450.0
DEFAULT_RING_MODULE_HEIGHT_CM = 450.0
BOARD_RADIUS_CM = 60.0
DEFAULT_AFTER_ARRIVAL_SECONDS = 3.0
LIFT_FIND_SECONDS = 10.0
DECK_TRACE_RANGE_CM = 1000.0
DECK_PIVOT_GUESS_CM = 200.0
DECK_AT_LANDING_XY_CM = 300.0
DECK_AT_LANDING_Z_CM = 10.0
DECK_ARRIVED_Z_CM = 5.0
DECK_MOVING_SPEED_CM = 5.0
DECK_RESTING_SPEED_CM = 1.0
DECK_SETTLE_SECONDS = 0.3
MIN_WALK_GAP_CM = 1.0
ALLOWED_COMMANDS = ("csvprofile start", "csvprofile stop", "memreport -full")
SUPPORT_PROBE_OFFSETS_CM = ((0.0, 0.0), (30.0, 0.0), (-30.0, 0.0), (0.0, 30.0), (0.0, -30.0), (60.0, 0.0), (-60.0, 0.0), (0.0, 60.0), (0.0, -60.0))
SETTLE_CAMERA_STEP_CM = 20.0
SETTLE_FRAME_MS = 100.0
SETTLE_CALM_FRAMES = 10
SETTLE_MAX_SECONDS = 30.0
SHAPE_COMPONENT_CLASSES = ("BoxComponent", "SphereComponent", "CapsuleComponent")
RING_NAME_PATTERN = re.compile(r"^Ring(\d+)$")

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
                json.dump(payload, handle, ensure_ascii=False, separators=(",", ":"))
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


def find_support_z_at(x, y, pawn):
    hits = unreal.SystemLibrary.line_trace_multi(world, vec(x, y, GROUND_TRACE_TOP_Z_CM), vec(x, y, GROUND_TRACE_BOTTOM_Z_CM), VISIBILITY, True, [pawn], NO_DEBUG, True) or []
    for hit in hits:
        description = describe_hit(hit)
        if description["comp_class"] != "BrushComponent" and description["normal"][2] > WALKABLE_NORMAL_Z_MIN:
            return description["impact"][2]
    return None


def find_support_z(x, y, pawn):
    for offset_x, offset_y in SUPPORT_PROBE_OFFSETS_CM:
        support_z = find_support_z_at(x + offset_x, y + offset_y, pawn)
        if support_z is not None:
            return support_z
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


def read_console_float(variable):
    reader = getattr(unreal.SystemLibrary, "get_console_variable_float_value", None)
    if reader is None:
        return None
    return round(float(reader(variable)), 4)


def action_console(request, pawn):
    variable = str(request["variable"]).strip()
    if not variable or any(character.isspace() or character == ";" for character in variable):
        raise ValueError(f"invalid console variable name: {variable!r}")
    value = float(request["value"])
    command = f"{variable} {value:g}"
    unreal.SystemLibrary.execute_console_command(world, command)
    return {"command": command, "variable": variable, "value": value, "read_back": read_console_float(variable)}


def action_command(request, pawn):
    command = " ".join(str(request["command"]).split())
    if command not in ALLOWED_COMMANDS:
        raise ValueError(f"command not allowed: {command!r}")
    unreal.SystemLibrary.execute_console_command(world, command)
    return {"command": command}


def read_real_seconds():
    try:
        return float(unreal.GameplayStatics.get_real_time_seconds(world))
    except Exception:
        return time.monotonic()


def read_game_seconds():
    return float(unreal.GameplayStatics.get_time_seconds(world))


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


def travel_state_name(travel):
    if travel is None:
        return "None"
    state = travel.get_travel_state()
    return str(getattr(state, "name", state))


def realm_weight(realm_subsystem, realm_id):
    if realm_subsystem is None or not realm_id:
        return -1.0
    return realm_subsystem.get_realm_weight(realm_id)


def read_max_move_speed(pawn):
    reader = getattr(pawn, "get_max_move_speed", None)
    if reader is None:
        return None
    return round(float(reader()), 1)


def make_fade_reader():
    manager = unreal.GameplayStatics.get_player_camera_manager(world, 0)
    try:
        float(manager.get_editor_property("fade_amount"))
    except Exception:
        return None
    return lambda: round(float(unreal.GameplayStatics.get_player_camera_manager(world, 0).get_editor_property("fade_amount")), 4)


def place_walk_start(start, facing_yaw, pawn):
    z = start[2]
    if z is None:
        ground_z = find_ground_z(start[0], start[1], pawn)
        z = pawn.get_actor_location().z if ground_z is None else ground_z + DEFAULT_CLEARANCE_CM
    pawn.set_actor_location(vec(start[0], start[1], z), False, True)
    pawn.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=facing_yaw), True)


def make_hold(start):
    return {"start": start, "elapsed": 0.0, "placed": False, "stream_s": 0.0, "settle_s": 0.0, "calm_frames": 0, "last_camera": None, "streaming_loaded": False}


def hold_at_start(hold, delta_seconds, pawn):
    if hold["placed"]:
        settle_camera(hold, delta_seconds, pawn)
        return None
    start = hold["start"]
    hold["elapsed"] += delta_seconds
    ground_z = find_support_z(start[0], start[1], pawn)
    if ground_z is None:
        if hold["elapsed"] > STREAM_WAIT_SECONDS:
            return f"ground not loaded under walk start after {STREAM_WAIT_SECONDS:.0f}s"
        held_z = start[2] if start[2] is not None else pawn.get_actor_location().z
        pawn.set_actor_location(vec(start[0], start[1], held_z), False, True)
        return None
    pawn.set_actor_location(vec(start[0], start[1], ground_z + DEFAULT_CLEARANCE_CM), False, True)
    hold["placed"] = True
    hold["stream_s"] = hold["elapsed"]
    return None


def is_streaming_loaded_around(location):
    partition = next((subsystem for subsystem in unreal.ObjectIterator(unreal.WorldPartitionSubsystem) if subsystem.get_world() == world), None)
    if partition is None:
        return True
    source = unreal.WorldPartitionStreamingQuerySource()
    source.set_editor_property("location", location)
    source.set_editor_property("use_grid_loading_range", True)
    source.set_editor_property("spatial_query", True)
    return bool(partition.is_streaming_completed(unreal.WorldPartitionRuntimeCellState.LOADED, [source], False))


def settle_camera(hold, delta_seconds, pawn):
    camera = unreal.GameplayStatics.get_player_camera_manager(world, 0).get_camera_location()
    last_camera = hold["last_camera"]
    hold["last_camera"] = camera
    hold["settle_s"] += delta_seconds
    is_calm = last_camera is not None and (camera - last_camera).length() <= SETTLE_CAMERA_STEP_CM and delta_seconds * 1000.0 <= SETTLE_FRAME_MS
    if is_calm and not hold["streaming_loaded"]:
        hold["streaming_loaded"] = is_streaming_loaded_around(pawn.get_actor_location())
    hold["calm_frames"] = hold["calm_frames"] + 1 if is_calm and hold["streaming_loaded"] else 0
    if hold["calm_frames"] >= SETTLE_CALM_FRAMES or hold["settle_s"] >= SETTLE_MAX_SECONDS:
        hold["start"] = None


def planar_gap(x, y, point):
    return math.hypot(x - point[0], y - point[1])


def yaw_toward(origin, target):
    return math.degrees(math.atan2(target[1] - origin[1], target[0] - origin[0]))


def make_route(points):
    if not points:
        return None
    return {"points": [(float(point[0]), float(point[1])) for point in points], "index": 0}


def route_facing_target(route, fallback):
    if route is None:
        return fallback
    points = route["points"]
    return points[1] if len(points) > 1 else points[0]


def has_passed_waypoint(points, index, location, radius):
    target = points[index]
    if planar_gap(location.x, location.y, target) <= radius:
        return True
    if index == 0 or index == len(points) - 1:
        return False
    previous = points[index - 1]
    segment_x = target[0] - previous[0]
    segment_y = target[1] - previous[1]
    length = math.hypot(segment_x, segment_y)
    if length < MIN_WALK_GAP_CM:
        return True
    along = ((location.x - previous[0]) * segment_x + (location.y - previous[1]) * segment_y) / length
    return along >= length


def advance_route(route, location, radius):
    points = route["points"]
    while route["index"] < len(points) - 1 and has_passed_waypoint(points, route["index"], location, radius):
        route["index"] += 1
    return points[route["index"]]


def is_route_finished(route, location, radius):
    target = advance_route(route, location, radius)
    return route["index"] == len(route["points"]) - 1 and planar_gap(location.x, location.y, target) <= radius


def walk_toward(pawn, location, target):
    gap_x = target[0] - location.x
    gap_y = target[1] - location.y
    length = math.hypot(gap_x, gap_y)
    if length < MIN_WALK_GAP_CM:
        return
    pawn.add_movement_input(vec(gap_x / length, gap_y / length, 0.0), 1.0, False)


def make_track(gate_id, realm_id, to_entry, hitch_ms):
    return {
        "gate_id": gate_id, "realm_id": realm_id, "to_entry": to_entry, "hitch_ms": hitch_ms,
        "travel": find_travel_subsystem(), "realm": find_realm_subsystem(), "fade": make_fade_reader(),
        "frames": [], "jumps": [], "camera_jumps": [], "state_events": [], "frame_ms": [], "slate_ms": [], "hitches": [],
        "last": None, "real": None, "state_name": None, "walked_cm": 0.0,
    }


def read_ready(track):
    travel = track["travel"]
    if travel is None or not track["gate_id"]:
        return None
    return bool(travel.is_ready_for(track["gate_id"], track["to_entry"]))


def record_state(track, t):
    state_name = travel_state_name(track["travel"])
    if state_name == track["state_name"]:
        return
    track["state_name"] = state_name
    track["state_events"].append({"t": t, "state": state_name})


def record_motion(track, frame, location, camera):
    last = track["last"]
    track["last"] = (location, camera)
    if last is None:
        return
    index = len(track["frames"])
    if (camera - last[1]).length() > JUMP_CM:
        track["camera_jumps"].append({"frame": index, "t": frame["t"], "from": vec_list(last[1]), "to": frame["cam"], "state": track["state_name"]})
    if (location - last[0]).length() > JUMP_CM:
        track["jumps"].append({"frame": index, "t": frame["t"], "from": vec_list(last[0]), "to": frame["pawn"], "state": track["state_name"]})
        return
    track["walked_cm"] += math.hypot(location.x - last[0].x, location.y - last[0].y)


def sample_track(track, pawn, location, t, game_seconds):
    camera = unreal.GameplayStatics.get_player_camera_manager(world, 0).get_camera_location()
    velocity = pawn.get_velocity()
    rounded_t = round(t, 3)
    record_state(track, rounded_t)
    frame = {
        "t": rounded_t,
        "gt": round(game_seconds, 3),
        "pawn": vec_list(location),
        "cam": vec_list(camera),
        "w": round(realm_weight(track["realm"], track["realm_id"]), 4),
        "ready": read_ready(track),
        "spd": round(math.hypot(velocity.x, velocity.y), 1),
        "max": read_max_move_speed(pawn),
        "fade": None if track["fade"] is None else track["fade"](),
    }
    record_motion(track, frame, location, camera)
    return frame


def commit_frame(track, frame, delta_seconds):
    track["frames"].append(frame)
    real_seconds = read_real_seconds()
    last_real = track["real"]
    track["real"] = real_seconds
    if last_real is None:
        return
    frame_ms = (real_seconds - last_real) * 1000.0
    slate_ms = delta_seconds * 1000.0
    track["frame_ms"].append(round(frame_ms, 2))
    track["slate_ms"].append(round(slate_ms, 2))
    if frame_ms <= track["hitch_ms"]:
        return
    track["hitches"].append({
        "t": frame["t"], "frame_ms": round(frame_ms, 1), "slate_ms": round(slate_ms, 1), "loc": frame["pawn"], "walked_cm": round(track["walked_cm"], 1),
        "state": track["state_name"], "w": frame["w"], "phase": frame.get("ph"), "deck_z": frame["deck"][2] if "deck" in frame else None,
    })


def track_outputs(track):
    keys = ("frames", "jumps", "camera_jumps", "state_events", "frame_ms", "slate_ms", "hitches")
    outputs = {key: track[key] for key in keys}
    outputs.update(walked_cm=round(track["walked_cm"], 1), fade_source=None if track["fade"] is None else "fade_amount")
    return outputs


def start_walk(request, pawn, result_path, base):
    seconds = float(request["seconds"])
    hitch_ms = float(request.get("hitch_ms", DEFAULT_HITCH_MS))
    forward = bool(request.get("forward", True))
    to_entry = bool(request.get("to_entry", forward))
    radius = float(request.get("waypoint_radius_cm", DEFAULT_WAYPOINT_RADIUS_CM))
    route = make_route(request.get("path"))
    fixed_direction = None
    fixed_yaw = None
    if route is None:
        fixed_yaw = float(request["yaw"]) + (0.0 if forward else 180.0)
        fixed_direction = vec(math.cos(math.radians(fixed_yaw)), math.sin(math.radians(fixed_yaw)), 0.0)
    hold = make_hold(request.get("start"))
    if hold["start"]:
        facing_yaw = fixed_yaw if route is None else yaw_toward(hold["start"], route_facing_target(route, hold["start"]))
        place_walk_start(hold["start"], facing_yaw, pawn)
    track = make_track(request.get("gate_id"), request.get("realm_id"), to_entry, hitch_ms)
    state = {"t": 0.0, "reached": False, "done": False, "stream_wait_s": 0.0}
    handle_box = {}

    def finish(error=None):
        state["done"] = True
        unreal.unregister_slate_post_tick_callback(handle_box["handle"])
        payload = dict(
            base,
            ok=error is None,
            error=error,
            gate_id=request.get("gate_id"),
            realm_id=request.get("realm_id"),
            forward=forward,
            to_entry=to_entry,
            seconds=seconds,
            hitch_ms=hitch_ms,
            t=round(state["t"], 3),
            reached=state["reached"],
            route=None if route is None else [list(point) for point in route["points"]],
            waypoint_index=None if route is None else route["index"],
            waypoint_radius_cm=radius,
            stream_wait_s=round(state["stream_wait_s"], 2),
            settle_s=round(state.get("settle_s", 0.0), 2),
            settle_streaming_loaded=state.get("settle_streaming_loaded"),
            **track_outputs(track),
        )
        write_json(result_path, payload)

    def step(delta_seconds):
        if hold["start"]:
            error = hold_at_start(hold, delta_seconds, pawn)
            if error:
                finish(error)
                return
            if hold["start"] is None:
                state["stream_wait_s"] = hold["stream_s"]
                state["settle_s"] = hold["settle_s"]
                state["settle_streaming_loaded"] = hold["streaming_loaded"]
            return
        state["t"] += delta_seconds
        location = pawn.get_actor_location()
        frame = sample_track(track, pawn, location, state["t"], read_game_seconds())
        frame["wp"] = None if route is None else route["index"]
        commit_frame(track, frame, delta_seconds)
        if route is not None and is_route_finished(route, location, radius):
            state["reached"] = True
            finish()
            return
        if state["t"] >= seconds:
            finish()
            return
        if route is None:
            pawn.add_movement_input(fixed_direction, 1.0, False)
            return
        walk_toward(pawn, location, advance_route(route, location, radius))

    def tick(delta_seconds):
        if state["done"]:
            return
        try:
            step(delta_seconds)
        except Exception as error:
            finish(repr(error))

    handle_box["handle"] = unreal.register_slate_post_tick_callback(tick)
    print("walking", request.get("gate_id"), "seconds", seconds, "path points", 0 if route is None else len(route["points"]))


def find_nearest_actor(actor_class, landings):
    actors = list(unreal.GameplayStatics.get_all_actors_of_class(world, actor_class) or [])
    if not actors:
        return None

    def landing_gap(actor):
        location = actor.get_actor_location()
        return min(planar_gap(location.x, location.y, landing) for landing in landings)

    return min(actors, key=landing_gap)


def find_ring_components(liner):
    scene_class = getattr(unreal, "SceneComponent", None)
    if liner is None or scene_class is None:
        return []
    rings = []
    for component in liner.get_components_by_class(scene_class) or []:
        match = RING_NAME_PATTERN.match(component.get_name())
        if match:
            rings.append((int(match.group(1)), component))
    return [component for _, component in sorted(rings, key=lambda pair: pair[0])]


def is_deck_surface(values, label):
    component = values[HIT_COMPONENT]
    if component is None or actor_label(values[HIT_ACTOR]) != label:
        return False
    if component.get_class().get_name() in SHAPE_COMPONENT_CLASSES:
        return False
    return values[HIT_IMPACT_NORMAL].z > WALKABLE_NORMAL_Z_MIN


def find_deck_top_hit(lift, pawn, landings):
    location = lift.get_actor_location()
    heights = [location.z] + [landing[2] for landing in landings]
    top = vec(location.x, location.y, max(heights) + DECK_TRACE_RANGE_CM)
    bottom = vec(location.x, location.y, min(heights) - DECK_TRACE_RANGE_CM)
    hits = unreal.SystemLibrary.line_trace_multi_for_objects(world, top, bottom, OBJECT_TYPES, True, [pawn], NO_DEBUG, True) or []
    label = actor_label(lift)
    best = None
    for hit in hits:
        values = hit.to_tuple()
        if not is_deck_surface(values, label):
            continue
        if best is None or values[HIT_IMPACT_POINT].z > best[0]:
            best = (values[HIT_IMPACT_POINT].z, values[HIT_COMPONENT])
    return best


def offset_from_resting_landing(lift, landings):
    location = lift.get_actor_location()
    for landing in landings:
        if planar_gap(location.x, location.y, landing) <= DECK_AT_LANDING_XY_CM and abs(landing[2] - location.z) <= DECK_PIVOT_GUESS_CM:
            return landing[2] - location.z, "landing"
    return 0.0, "pivot"


def blocks_pawn(component):
    query_modes = [getattr(unreal.CollisionEnabled, name) for name in ("QUERY_ONLY", "QUERY_AND_PHYSICS", "QUERY_AND_PROBE") if hasattr(unreal.CollisionEnabled, name)]
    if component.get_collision_enabled() not in query_modes:
        return False
    return component.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN) == unreal.CollisionResponseType.ECR_BLOCK


def pawn_blocking_boxes(lift):
    box_class = getattr(unreal, "BoxComponent", None)
    if box_class is None:
        return []
    return sorted(component.get_name() for component in lift.get_components_by_class(box_class) or [] if blocks_pawn(component))


def find_mover(pawn):
    try:
        return pawn.get_editor_property("mover_component")
    except Exception:
        return None


def read_capsule_half_height(pawn):
    capsule_class = getattr(unreal, "CapsuleComponent", None)
    finder = getattr(pawn, "get_component_by_class", None)
    if capsule_class is None or finder is None:
        return DEFAULT_CAPSULE_HALF_HEIGHT_CM
    capsule = finder(capsule_class)
    if capsule is None:
        return DEFAULT_CAPSULE_HALF_HEIGHT_CM
    return float(capsule.get_scaled_capsule_half_height())


def describe_movement_base(mover):
    if mover is None:
        return None
    base_component = mover.get_movement_base()
    if base_component is None:
        return None
    return f"{actor_label(base_component.get_owner())}.{base_component.get_name()}"


def read_airborne(pawn):
    reader = getattr(pawn, "is_airborne", None)
    if reader is None:
        return None
    return bool(reader())


def measure_ring_speed(rings, last, module_height_cm, game_seconds):
    heights = [component.get_world_location().z for component in rings]
    if last is None or game_seconds <= last[1]:
        return heights, None
    steps = sorted(now - before for now, before in zip(heights, last[0]) if abs(now - before) < 0.5 * module_height_cm)
    if not steps:
        return heights, None
    return heights, steps[len(steps) // 2] / (game_seconds - last[1])


def start_ride(request, pawn, result_path, base):
    lift_class = getattr(unreal, "TDRealmLiftDeck", None)
    if lift_class is None:
        raise RuntimeError("TDRealmLiftDeck class not loaded (C++ module not built?)")
    seconds = float(request["seconds"])
    after_seconds = float(request.get("after_seconds", DEFAULT_AFTER_ARRIVAL_SECONDS))
    hitch_ms = float(request.get("hitch_ms", DEFAULT_HITCH_MS))
    gate_id = request.get("gate_id")
    realm_id = request.get("realm_id")
    to_entry = bool(request.get("to_entry", True))
    board = [float(value) for value in request["board_landing"]]
    arrive = [float(value) for value in request["arrive_landing"]]
    wait_radius = float(request.get("wait_radius_cm", DEFAULT_WAIT_RADIUS_CM))
    radius = float(request.get("waypoint_radius_cm", DEFAULT_WAYPOINT_RADIUS_CM))
    module_height = float(request.get("ring_module_height_cm", DEFAULT_RING_MODULE_HEIGHT_CM))
    approach_route = make_route(request.get("path"))
    exit_route = make_route(request.get("exit_path"))
    hold = make_hold(request.get("start"))
    if hold["start"]:
        place_walk_start(hold["start"], yaw_toward(hold["start"], route_facing_target(approach_route, board)), pawn)
    track = make_track(gate_id, realm_id, to_entry, hitch_ms)
    mover = find_mover(pawn)
    half_height = read_capsule_half_height(pawn)
    state = {
        "t": 0.0, "phase": "approach" if approach_route is None else "path", "lift": None, "lift_label": None, "deck_component": None, "deck_offset": 0.0,
        "deck_top_source": None, "find_elapsed": 0.0, "deck_speed": 0.0, "deck_last": None, "settle": 0.0, "boxes": None, "events": {}, "arrived": False,
        "timeout_phase": None, "after_start": None, "liner_label": None, "rings": [], "ring_last": None, "box_events": [], "stream_wait_s": 0.0, "done": False,
    }
    handle_box = {}

    def finish(error=None):
        state["done"] = True
        unreal.unregister_slate_post_tick_callback(handle_box["handle"])
        payload = dict(
            base,
            ok=error is None,
            error=error,
            gate_id=gate_id,
            realm_id=realm_id,
            to_entry=to_entry,
            seconds=seconds,
            hitch_ms=hitch_ms,
            board_landing=board,
            arrive_landing=arrive,
            path=None if approach_route is None else [list(point) for point in approach_route["points"]],
            exit_path=None if exit_route is None else [list(point) for point in exit_route["points"]],
            waypoint_radius_cm=radius,
            t=round(state["t"], 3),
            arrived=state["arrived"],
            exit_reached="exit_end" in state["events"],
            timeout_phase=state["timeout_phase"],
            lift_actor=state["lift_label"],
            liner_actor=state["liner_label"],
            ring_count=len(state["rings"]),
            ring_module_height_cm=module_height,
            deck_top_source=state["deck_top_source"],
            deck_component=None if state["deck_component"] is None else state["deck_component"].get_name(),
            deck_top_offset_cm=round(state["deck_offset"], 2),
            capsule_half_height_cm=round(half_height, 2),
            stream_wait_s=round(state["stream_wait_s"], 2),
            settle_s=round(state.get("settle_s", 0.0), 2),
            settle_streaming_loaded=state.get("settle_streaming_loaded"),
            events={name: round(value, 3) for name, value in state["events"].items()},
            box_events=state["box_events"],
            **track_outputs(track),
        )
        write_json(result_path, payload)

    def mark(event_name):
        state["events"].setdefault(event_name, state["t"])

    def attach_lift(delta_seconds):
        lift = find_nearest_actor(lift_class, (board, arrive))
        if lift is None:
            state["find_elapsed"] += delta_seconds
            if state["find_elapsed"] > LIFT_FIND_SECONDS:
                finish(f"no TDRealmLiftDeck actor after {LIFT_FIND_SECONDS:.0f}s")
            return False
        state.update(lift=lift, lift_label=actor_label(lift))
        liner_class = getattr(unreal, "TDRealmShaftLiner", None)
        liner = None if liner_class is None else find_nearest_actor(liner_class, (board, arrive))
        state.update(liner_label=actor_label(liner), rings=find_ring_components(liner))
        deck_hit = find_deck_top_hit(lift, pawn, (board, arrive))
        if deck_hit is None:
            offset, source = offset_from_resting_landing(lift, (board, arrive))
            state.update(deck_offset=offset, deck_top_source=source)
            return True
        top_z, component = deck_hit
        state.update(deck_component=component, deck_offset=top_z - component.get_world_location().z, deck_top_source="trace")
        return True

    def deck_top_z(lift_location):
        component = state["deck_component"]
        base_z = lift_location.z if component is None else component.get_world_location().z
        return base_z + state["deck_offset"]

    def track_deck_speed(deck, game_seconds):
        last = state["deck_last"]
        state["deck_last"] = (deck, game_seconds)
        if last is None or game_seconds <= last[1]:
            return
        if planar_gap(deck[0], deck[1], last[0]) > JUMP_CM:
            return
        state["deck_speed"] = (deck[2] - last[0][2]) / (game_seconds - last[1])

    def track_rings(game_seconds):
        if not state["rings"]:
            return None, None
        heights, speed = measure_ring_speed(state["rings"], state["ring_last"], module_height, game_seconds)
        state["ring_last"] = (heights, game_seconds)
        return None if speed is None else round(speed, 2), round(heights[0], 1)

    def track_boxes():
        blocking = pawn_blocking_boxes(state["lift"])
        if blocking == state["boxes"]:
            return
        state["boxes"] = blocking
        state["box_events"].append({"t": round(state["t"], 3), "blocking": blocking})

    def sample(delta_seconds):
        location = pawn.get_actor_location()
        lift_location = state["lift"].get_actor_location()
        deck = (lift_location.x, lift_location.y, deck_top_z(lift_location))
        game_seconds = read_game_seconds()
        track_deck_speed(deck, game_seconds)
        ring_speed, ring_z = track_rings(game_seconds)
        frame = sample_track(track, pawn, location, state["t"], game_seconds)
        frame.update(
            ph=state["phase"],
            deck=[round(deck[0], 1), round(deck[1], 1), round(deck[2], 2)],
            dv=round(state["deck_speed"], 2),
            feet=round(location.z - half_height - deck[2], 2),
            air=read_airborne(pawn),
            mode=None if mover is None else str(mover.get_movement_mode_name()),
            base=describe_movement_base(mover),
            ring_v=ring_speed,
            ring_z0=ring_z,
        )
        commit_frame(track, frame, delta_seconds)
        track_boxes()
        return location

    def deck_is_at(landing, z_tolerance, speed_limit):
        deck = state["deck_last"][0]
        if planar_gap(deck[0], deck[1], landing) > DECK_AT_LANDING_XY_CM:
            return False
        return abs(deck[2] - landing[2]) <= z_tolerance and abs(state["deck_speed"]) < speed_limit

    def advance_path(location):
        if is_route_finished(approach_route, location, radius):
            mark("path_end")
            state["phase"] = "approach"
            return
        walk_toward(pawn, location, advance_route(approach_route, location, radius))

    def advance_approach(location, gap):
        if gap > wait_radius:
            walk_toward(pawn, location, board)
            return
        if deck_is_at(board, DECK_AT_LANDING_Z_CM, DECK_MOVING_SPEED_CM):
            state["phase"] = "board"
            walk_toward(pawn, location, board)
            return
        mark("call_start")
        state["phase"] = "call"

    def advance_call():
        if not deck_is_at(board, DECK_AT_LANDING_Z_CM, DECK_MOVING_SPEED_CM):
            return
        mark("call_end")
        state["phase"] = "board"

    def advance_board(location, gap):
        if gap > BOARD_RADIUS_CM:
            walk_toward(pawn, location, board)
            return
        mark("board")
        state["phase"] = "ride"

    def begin_after():
        state["phase"] = "after"
        state["after_start"] = state["t"]

    def arrive_now():
        mark("arrival")
        state["arrived"] = True
        if exit_route is None:
            begin_after()
            return
        state["phase"] = "exit"

    def advance_ride(delta_seconds):
        if "departure" not in state["events"] and abs(state["deck_speed"]) > DECK_MOVING_SPEED_CM:
            mark("departure")
        if "departure" not in state["events"] or not deck_is_at(arrive, DECK_ARRIVED_Z_CM, DECK_RESTING_SPEED_CM):
            state["settle"] = 0.0
            return
        state["settle"] += delta_seconds
        if state["settle"] >= DECK_SETTLE_SECONDS:
            arrive_now()

    def advance_exit(location):
        if is_route_finished(exit_route, location, radius):
            mark("exit_end")
            begin_after()
            return
        walk_toward(pawn, location, advance_route(exit_route, location, radius))

    def advance(location, delta_seconds):
        gap = planar_gap(location.x, location.y, board)
        phase = state["phase"]
        if phase == "path":
            advance_path(location)
            return
        if phase == "approach":
            advance_approach(location, gap)
            return
        if phase == "call":
            advance_call()
            return
        if phase == "board":
            advance_board(location, gap)
            return
        if phase == "ride":
            advance_ride(delta_seconds)
            return
        if phase == "exit":
            advance_exit(location)
            return
        if state["t"] - state["after_start"] >= after_seconds:
            finish()

    def step(delta_seconds):
        if hold["start"]:
            error = hold_at_start(hold, delta_seconds, pawn)
            if error:
                finish(error)
                return
            if hold["start"] is None:
                state["stream_wait_s"] = hold["stream_s"]
                state["settle_s"] = hold["settle_s"]
                state["settle_streaming_loaded"] = hold["streaming_loaded"]
            return
        state["t"] += delta_seconds
        if state["lift"] is None and not attach_lift(delta_seconds):
            return
        location = sample(delta_seconds)
        if state["t"] >= seconds:
            state["timeout_phase"] = state["phase"]
            finish()
            return
        advance(location, delta_seconds)

    def tick(delta_seconds):
        if state["done"]:
            return
        try:
            step(delta_seconds)
        except Exception as error:
            finish(repr(error))

    handle_box["handle"] = unreal.register_slate_post_tick_callback(tick)
    print("riding", gate_id, "to_entry", to_entry, "seconds", seconds)


ACTIONS = {"move": action_move, "shot": action_shot, "occluders": action_occluders}
ASYNC_ACTIONS = {"walk": start_walk, "ride": start_ride}


def get_pawn():
    if world is None:
        return None
    return unreal.GameplayStatics.get_player_pawn(world, 0)


def report_failure(result_path, base, message):
    print("pie_probe failed:", message)
    write_json(result_path, dict(base, ok=False, error=message))


def run_action(handler, request, pawn, result_path, base):
    try:
        payload = handler(request, pawn)
    except Exception as error:
        report_failure(result_path, base, repr(error))
        return
    write_json(result_path, dict(base, ok=True, error=None, **payload))
    print("pie_probe", base["action"], "ok", payload.get("pawn"))


def main():
    with open(REQUEST_PATH, encoding="utf-8") as handle:
        request = json.load(handle)
    result_path = resolve_path(request.get("result_path") or DEFAULT_RESULT_PATH)
    action = request.get("action")
    base = {"request_id": request.get("request_id"), "action": action}
    if action == "console":
        run_action(action_console, request, None, result_path, base)
        return
    if action == "command":
        run_action(action_command, request, None, result_path, base)
        return
    pawn = get_pawn()
    if pawn is None:
        report_failure(result_path, base, "PIE world or pawn missing")
        return
    starter = ASYNC_ACTIONS.get(action)
    if starter is not None:
        try:
            starter(request, pawn, result_path, base)
        except Exception as error:
            report_failure(result_path, base, repr(error))
        return
    handler = ACTIONS.get(action)
    if handler is None:
        report_failure(result_path, base, f"unknown action: {action}")
        return
    run_action(handler, request, pawn, result_path, base)


main()
