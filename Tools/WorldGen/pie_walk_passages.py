"""에디터 밖에서 실행: 연속 세계(D-45) 이계 관문마다 PIE 폰을 실제 이동 입력으로 움직여 기록·요약한다. 걷는 연결로(SW·NE)는 지선 시작(없으면 연결로 필드 끝)에서 이계 도착점까지 경유점을 따라 걷고(in) 같은 길로 돌아오며(out), 승강 관문(SE)은 지선 시작에서 상단 협곡을 지나 덱을 타고 내려가 출구 슬롯 끝까지 걷고(down) 같은 길로 올라온다(up). 프레임마다 폰·카메라 이동량, 진행도 w, 준비 여부, 수평 속도·최대 속도 설정(감속), 덱 높이·링 속도(트레드밀)를 재고, 에디터 로그에서 감속 발동·해제, 게임플레이 데이터 레이어 활성화, 트레드밀, 준비 시간 줄을 모아 K1·K11~K14·K17 참고 판정과 프레임 시간을 요약한다(P3-19).
실행: python Tools/WorldGen/pie_walk_passages.py [--run 이름] [--gates SunkenStarfieldSouthwest,PaleHeights] [--speed 600] [--hitch-ms 100] [--simulate-delay 30] [--overwrite] [--offline] (PowerShell, 에디터 실행 중이고 다른 갈래가 에디터를 쓰지 않을 때, 에디터 창을 앞에 둔다 L-editor-06)
출력: Saved/WorldGen/PieWalk/<run>/ 의 <관문 ID>_in.json·_out.json(걷기)·_down.json·_up.json(승강기) 원 결과(압축 JSON, 에디터 로그 발췌 log_lines 포함)와 summary.md. 종료 코드 0 = 모든 걷기·운행이 끝까지 가고 참고 판정 통과, 1 = 결과 없음·오류·미도달·판정 실패, 2 = PIE 시작 실패
상태: 현행 (2026-10-15)

경로: Saved/WorldGen/Realms/*/layout.json realm.gates[]의 centerline_cm(월드 cm 꼭짓점) 앞에 generate_ashen_vale.SPUR_ROADS의 지선 시작점을 붙인다. 승강 관문은 중심선을 lift.field_landing_transform 축 꼭짓점에서 나눠 앞쪽을 접근 경로(path), 뒤쪽 + arrival을 출구 경로(exit_path)로 쓴다(올라올 때는 반대). 걷기 시간 한도 = 경로 길이 / --speed x 1.5 + 20초(+ 지연 모의 초), 승강 시간 한도 = 걷는 길이 / --speed x 1.5 + 호출 + 탑승 대기 + 예상 운행 + 트레드밀 최대 + 3 + 15초(+ 지연 모의 초).
경계 좌표 along(cm) = FieldReturn에서 Arrival 쪽 직선 축 투영 - 접근 길이(필드 쪽 음수, B = 0). w(B) = along이 0을 지나는 두 프레임 사이 선형 보간. 경계 근처 = 진행 방향 기준 B 앞 2500cm ~ 뒤 3000cm(시작 1초 제외). 갱도 w(20m) = 덱 깊이(상단 착지 z - 덱 윗면 z)가 2000cm를 처음 지나는 두 프레임 사이 보간.
덱 움직임(s) = 탑승 뒤 덱 수직 속도가 0.5cm/s를 처음 넘은 프레임 바로 앞 프레임부터 도착 착지 300cm 안에서 덱 수직 속도가 1cm/s 미만이 된 첫 프레임까지. 트레드밀 구간 = 탑승 중 |링 속도| > 1cm/s인 연속 프레임, 그 안에서 |덱 속도| < 1cm/s인 처음~끝 프레임이 대기, 앞이 감속, 뒤가 재개. 불변식 오차 = ||덱 속도| + |링 속도| - 순항 속도|.
--simulate-delay 초: 관문마다 걷기·운행 전에 editor_pie_probe.py console로 td.Realm.SimulatedPreloadDelaySeconds를 그 값으로 두고(다시 읽어 확인), 끝나면 실패해도 0으로 되돌린다. 0보다 크면 K13은 실행 전체 걷기에서 감속 발동 로그 1건 이상(북동은 접근 196m라 30초 지연이 감속 지점 전에 끝날 수 있음), 모든 걷기에서 최대 속도 설정 최저 비율 30% 이상을, 10초 이상이면 K11은 실행 전체에서 트레드밀 1회 이상·운행마다 1회 이하를 기대하고(올라가는 운행은 탑승 전 접근 걷기 동안 지연이 끝날 수 있음), K12 준비 시간 판정은 하지 않는다.
에디터 로그: 호출마다 Saved/Logs/TDGame.log 크기를 기억하고 결과가 온 뒤 2초 기다려 늘어난 부분의 LogTDGame 줄(Realm: pacing·gameplay data layer, RealmLift·ShaftLiner, SeamlessTravel)을 log_lines로 붙인다. 로그 파일이 없으면 로그 판정은 '로그 없음'으로 실패한다. 프레임 시간 frame_ms = 월드 실시간 틱 간 차이, slate_ms = 슬레이트 틱 인수. 에디터 창이 백그라운드면 스로틀로 끊김이 잡힌다.
"""
import argparse
import glob
import json
import math
import os
import re
import sys
import time

WORLDGEN = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, WORLDGEN)
import generate_ashen_vale as field_generator  # noqa: E402
import pie_capture_spots as spots  # noqa: E402

WALK_ROOT = os.path.join(spots.ROOT, "Saved", "WorldGen", "PieWalk")
REALM_LAYOUTS = os.path.join(spots.ROOT, "Saved", "WorldGen", "Realms", "*", "layout.json")
EDITOR_LOG = os.path.join(spots.ROOT, "Saved", "Logs", "TDGame.log")
LOG_SETTLE_SECONDS = 2.0
LOG_LINE_LIMIT = 400
LOG_KEYS = ("Realm: pacing", "gameplay data layer", "RealmLift '", "ShaftLiner '", "SeamlessTravel:")
PACING_PATTERN = re.compile(r"Realm: pacing (engaged|released) gate=(\S+) along=(-?[0-9.]+) ready=(\d) elapsed=(-?[0-9.]+)")
DATA_LAYER_PATTERN = re.compile(r"gameplay data layer '([^']+)' of realm '([^']+)' set to Activated \(([^,]*), accepted (\d)\)")
MISSING_DATA_LAYER_PATTERN = re.compile(r"realm '([^']+)' has no gameplay data layer")
READY_PATTERN = re.compile(r"SeamlessTravel: '([^']+)' ready after ([0-9.]+)s")
TREADMILL_PATTERN = re.compile(r"RealmLift '([^']+)': treadmill (engaged|timeout|released)")
CENTIMETERS_PER_METER = 100.0
START_CLEARANCE_CM = 150.0
WAYPOINT_RADIUS_CM = 150.0
WALK_TIME_FACTOR = 1.5
EXTRA_WALK_SECONDS = 20.0
RESULT_MARGIN_SECONDS = 60.0
STREAM_WAIT_SECONDS = 60.0
P95_FRACTION = 0.95
WALK_KIND = "walk"
LIFT_KIND = "lift"
WALK_DIRECTIONS = ("in", "out")
LIFT_DIRECTIONS = ("down", "up")
LIFT_WAIT_MARGIN_CM = 150.0
LIFT_AFTER_ARRIVAL_SECONDS = 3.0
LIFT_EXTRA_SECONDS = 15.0
AXIS_MATCH_CM = 1.0
SIMULATED_DELAY_VARIABLE = "td.Realm.SimulatedPreloadDelaySeconds"
CONSOLE_READ_BACK_TOLERANCE = 1e-3
LIFT_DEFAULTS = {
    "cruise_speed_cm_per_second": 300.0,
    "acceleration_cm_per_second_squared": 100.0,
    "brake_deceleration_cm_per_second_squared": 80.0,
    "board_dwell_seconds": 1.2,
    "call_speed_cm_per_second": 600.0,
    "max_treadmill_seconds": 20.0,
    "hold_depth_cm": 1912.5,
    "ring_module_height_cm": 450.0,
}
DEFAULT_DECK_HALF_EXTENT_CM = (300.0, 300.0, 15.0)
PRELOADING_STATE = "PRELOADING"
PLACED_PRELOAD_FIRST_FRAME_S = 0.1
READY_STATE = "READY_TO_TRAVEL"
FADE_EPSILON = 1e-3
WEIGHT_STEP_LIMIT = 0.01
BOUNDARY_WEIGHT = 0.5
BOUNDARY_WEIGHT_TOLERANCE = 0.02
SHAFT_WEIGHT_DEPTH_CM = 2000.0
NEAR_BOUNDARY_BEFORE_CM = 2500.0
NEAR_BOUNDARY_AFTER_CM = 3000.0
NEAR_BOUNDARY_SKIP_SECONDS = 1.0
PACING_MIN_SPEED_RATIO = 0.3
READY_LIMIT_SECONDS = 1.0
RIDE_TIME_TOLERANCE_SECONDS = 0.2
LANDING_TOLERANCE_CM = 10.0
DECK_STEP_LIMIT_CM = 100.0
DECK_MOVING_SPEED_CM = 0.5
DECK_STOPPED_SPEED_CM = 1.0
DECK_STOP_WINDOW_CM = 300.0
TREADMILL_RING_SPEED_CM = 1.0
TREADMILL_HOLD_SPEED_CM = 1.0
TREADMILL_INVARIANT_LIMIT_CM = 1.0
TREADMILL_EXPECT_DELAY_SECONDS = 10.0


def load_gates():
    gates = []
    for layout_path in sorted(glob.glob(REALM_LAYOUTS)):
        realm = spots.read_json(layout_path)["realm"]
        for gate in realm.get("gates") or []:
            gates.append((realm["realm_id"], gate))
    return gates


def select_gates(gates, gates_text):
    tokens = spots.split_tokens(gates_text)
    unknown = [token for token in tokens if not any(gate["gate_id"].startswith(token) for _, gate in gates)]
    if unknown:
        raise ValueError(f"unknown gates: {unknown}")
    selected = [(realm_id, gate) for realm_id, gate in gates if not tokens or any(gate["gate_id"].startswith(token) for token in tokens)]
    if not selected:
        raise ValueError("no gates selected")
    return selected


def is_lift_gate(gate):
    return str(gate.get("gate_kind") or "").lower() == LIFT_KIND


def read_location(transform):
    return [float(value) for value in transform["loc_cm"]]


def read_landing(transform):
    return {"loc": read_location(transform), "yaw": float(transform["yaw_deg"])}


def planar_distance(first, second):
    return math.hypot(first[0] - second[0], first[1] - second[1])


def distance(first, second):
    return math.sqrt(sum((a - b) ** 2 for a, b in zip(first, second)))


def polyline_length(points):
    return sum(planar_distance(points[index - 1], points[index]) for index in range(1, len(points)))


def find_spur_start_cm(gate_id):
    spur = field_generator.SPUR_ROADS.get(gate_id)
    if spur is None:
        return None
    start_m = spur[0]
    return [start_m[0] * CENTIMETERS_PER_METER, start_m[1] * CENTIMETERS_PER_METER]


def read_centerline(gate):
    return [[float(point[0]), float(point[1])] for point in gate["centerline_cm"]]


def end_at(points, point):
    if planar_distance(points[-1], point) <= AXIS_MATCH_CM:
        return points
    return points + [[point[0], point[1]]]


def start_point(route, ground_at):
    ground = ground_at("field", route[0][0], route[0][1])
    return [route[0][0], route[0][1], None if ground is None else ground + START_CLEARANCE_CM]


def build_walk_plan(realm_id, gate, ground_at, speed_cm_per_second):
    try:
        field_return = read_location(gate["field_return"])
        arrival = read_location(gate["arrival"])
        approach_cm = float(gate["approach_length_cm"])
        departure_cm = float(gate["departure_length_cm"])
        centerline = read_centerline(gate)
    except (KeyError, IndexError, TypeError, ValueError) as error:
        raise ValueError(f"{gate.get('gate_id')}: passage gate data is missing or malformed: {error!r}") from error
    spur = find_spur_start_cm(gate["gate_id"])
    route = end_at(([spur] if spur else []) + centerline, arrival)
    axis_length = planar_distance(field_return, arrival)
    if axis_length <= AXIS_MATCH_CM:
        raise ValueError(f"{gate['gate_id']}: field return and arrival coincide")
    axis = [(arrival[0] - field_return[0]) / axis_length, (arrival[1] - field_return[1]) / axis_length]
    route_cm = polyline_length(route)
    return {
        "kind": WALK_KIND,
        "gate_id": gate["gate_id"],
        "realm_id": realm_id,
        "route": route,
        "route_cm": route_cm,
        "spur_cm": None if spur is None else planar_distance(spur, centerline[0]),
        "start": start_point(route, ground_at),
        "seconds": route_cm / speed_cm_per_second * WALK_TIME_FACTOR + EXTRA_WALK_SECONDS,
        "field_return": field_return,
        "arrival": arrival,
        "axis": axis,
        "approach_cm": approach_cm,
        "departure_cm": departure_cm,
    }


def estimate_ride_seconds(distance_cm, cruise, acceleration, braking):
    ramp_cm = cruise * cruise / (2.0 * acceleration) + cruise * cruise / (2.0 * braking)
    if distance_cm >= ramp_cm:
        return cruise / acceleration + cruise / braking + (distance_cm - ramp_cm) / cruise
    peak = math.sqrt(2.0 * distance_cm * acceleration * braking / (acceleration + braking))
    return peak / acceleration + peak / braking


def find_axis_index(centerline, axis):
    for index, point in enumerate(centerline):
        if planar_distance(point, axis) <= AXIS_MATCH_CM:
            return index
    raise ValueError(f"shaft axis {axis[:2]} is not a centerline_cm vertex")


def build_lift_plan(realm_id, gate, ground_at, speed_cm_per_second):
    try:
        lift = gate["lift"]
        landings = {"field": read_landing(lift["field_landing_transform"]), "realm": read_landing(lift["realm_landing_transform"])}
        numbers = {key: float(lift.get(key, default)) for key, default in LIFT_DEFAULTS.items()}
        deck_half = [float(value) for value in lift.get("deck_half_extent_cm") or DEFAULT_DECK_HALF_EXTENT_CM]
        arrival = read_location(gate["arrival"])
        centerline = read_centerline(gate)
        axis_index = find_axis_index(centerline, landings["field"]["loc"])
    except (KeyError, IndexError, TypeError, ValueError) as error:
        raise ValueError(f"{gate.get('gate_id')}: gate_kind lift but lift data is missing or malformed: {error!r}") from error
    drop_cm = landings["field"]["loc"][2] - landings["realm"]["loc"][2]
    if drop_cm <= 0.0:
        raise ValueError(f"{gate['gate_id']}: field landing must be above the realm landing (drop {drop_cm:.1f}cm)")
    spur = find_spur_start_cm(gate["gate_id"])
    field_path = ([spur] if spur else []) + centerline[:axis_index]
    realm_path = end_at(centerline[axis_index + 1:] or [arrival], arrival)
    axis = landings["field"]["loc"][:2]
    walk_cm = polyline_length(field_path + [axis]) + polyline_length([axis] + realm_path)
    cruise = numbers["cruise_speed_cm_per_second"]
    ride_s = estimate_ride_seconds(drop_cm, cruise, numbers["acceleration_cm_per_second_squared"], numbers["brake_deceleration_cm_per_second_squared"])
    seconds = (walk_cm / speed_cm_per_second * WALK_TIME_FACTOR + drop_cm / numbers["call_speed_cm_per_second"] + numbers["board_dwell_seconds"]
               + ride_s + numbers["max_treadmill_seconds"] + LIFT_AFTER_ARRIVAL_SECONDS + LIFT_EXTRA_SECONDS)
    return {
        "kind": LIFT_KIND,
        "gate_id": gate["gate_id"],
        "realm_id": realm_id,
        "landings": landings,
        "field_path": field_path,
        "realm_path": realm_path,
        "start": start_point(field_path, ground_at),
        "walk_cm": walk_cm,
        "drop_cm": drop_cm,
        "ride_s": ride_s,
        "cruise_cm_per_second": cruise,
        "ring_module_height_cm": numbers["ring_module_height_cm"],
        "max_treadmill_s": numbers["max_treadmill_seconds"],
        "seconds": seconds,
        "wait_radius_cm": max(deck_half[0], deck_half[1]) + LIFT_WAIT_MARGIN_CM,
        "board_dwell_s": numbers["board_dwell_seconds"],
    }


def build_gate_plan(realm_id, gate, ground_at, speed_cm_per_second):
    if is_lift_gate(gate):
        return build_lift_plan(realm_id, gate, ground_at, speed_cm_per_second)
    return build_walk_plan(realm_id, gate, ground_at, speed_cm_per_second)


def walk_request(plan, direction, hitch_ms, extra_seconds):
    to_entry = direction == WALK_DIRECTIONS[0]
    return {
        "action": "walk",
        "gate_id": plan["gate_id"],
        "realm_id": plan["realm_id"],
        "path": plan["route"] if to_entry else list(reversed(plan["route"])),
        "start": plan["start"] if to_entry else None,
        "seconds": plan["seconds"] + extra_seconds,
        "to_entry": to_entry,
        "waypoint_radius_cm": WAYPOINT_RADIUS_CM,
        "hitch_ms": hitch_ms,
    }


def ride_request(plan, direction, hitch_ms, extra_seconds):
    down = direction == LIFT_DIRECTIONS[0]
    board_side, arrive_side = ("field", "realm") if down else ("realm", "field")
    return {
        "action": "ride",
        "gate_id": plan["gate_id"],
        "realm_id": plan["realm_id"],
        "to_entry": down,
        "start": plan["start"] if down else None,
        "path": plan["field_path"] if down else list(reversed(plan["realm_path"])),
        "exit_path": plan["realm_path"] if down else list(reversed(plan["field_path"])),
        "board_landing": plan["landings"][board_side]["loc"],
        "arrive_landing": plan["landings"][arrive_side]["loc"],
        "wait_radius_cm": plan["wait_radius_cm"],
        "waypoint_radius_cm": WAYPOINT_RADIUS_CM,
        "ring_module_height_cm": plan["ring_module_height_cm"],
        "seconds": plan["seconds"] + extra_seconds,
        "after_seconds": LIFT_AFTER_ARRIVAL_SECONDS,
        "hitch_ms": hitch_ms,
    }


def read_log_size():
    try:
        return os.path.getsize(EDITOR_LOG)
    except OSError:
        return None


def read_log_lines(start_size):
    if start_size is None:
        return None
    time.sleep(LOG_SETTLE_SECONDS)
    try:
        with open(EDITOR_LOG, "rb") as handle:
            size = os.fstat(handle.fileno()).st_size
            handle.seek(start_size if size >= start_size else 0)
            text = handle.read().decode("utf-8", errors="replace")
    except OSError:
        return None
    lines = [line.strip() for line in text.splitlines() if "LogTDGame" in line and any(key in line for key in LOG_KEYS)]
    return lines[-LOG_LINE_LIMIT:]


def call_with_log(request, timeout_seconds):
    mark = read_log_size()
    try:
        result = spots.call_probe(request, result_timeout_seconds=timeout_seconds)
    except Exception as error:
        result = {"ok": False, "error": repr(error)}
    result["log_lines"] = read_log_lines(mark)
    return result


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[max(0, math.ceil(fraction * len(ordered)) - 1)]


def frame_stats(frame_ms, hitch_ms):
    if not frame_ms:
        return None
    return {
        "samples": len(frame_ms),
        "mean_ms": round(sum(frame_ms) / len(frame_ms), 1),
        "p95_ms": round(percentile(frame_ms, P95_FRACTION), 1),
        "max_ms": round(max(frame_ms), 1),
        "over_hitch": sum(1 for value in frame_ms if value > hitch_ms),
    }


def interpolate_at(positions, values, target):
    for index in range(1, len(positions)):
        before, after = positions[index - 1], positions[index]
        if (before - target) * (after - target) > 0.0:
            continue
        if before == after:
            return round(values[index], 4)
        fraction = (target - before) / (after - before)
        return round(values[index - 1] + fraction * (values[index] - values[index - 1]), 4)
    return None


def along_cm(plan, point):
    offset_x = point[0] - plan["field_return"][0]
    offset_y = point[1] - plan["field_return"][1]
    return offset_x * plan["axis"][0] + offset_y * plan["axis"][1] - plan["approach_cm"]


def max_step(frames, key):
    steps = [distance(frames[index][key], frames[index - 1][key]) for index in range(1, len(frames))]
    return round(max(steps), 1) if steps else 0.0


def describe_motion(result):
    frames = result["frames"]
    fades = [frame.get("fade") for frame in frames if frame.get("fade") is not None]
    return {
        "jumps": len(result.get("jumps") or []),
        "camera_jumps": len(result.get("camera_jumps") or []),
        "max_pawn_step_cm": max_step(frames, "pawn"),
        "max_camera_step_cm": max_step(frames, "cam"),
        "fade_frames": sum(1 for value in fades if value > FADE_EPSILON),
        "fade_measured": bool(fades),
    }


def describe_state_readiness(state_events):
    preload_t = next((event["t"] for event in state_events if event["state"] == PRELOADING_STATE), None)
    if preload_t is None:
        return None
    ready_t = next((event["t"] for event in state_events if event["state"] == READY_STATE and event["t"] >= preload_t), None)
    return None if ready_t is None else round(ready_t - preload_t, 2)


def describe_log(lines, gate_id):
    if lines is None:
        return {"available": False, "pacing": [], "pacing_engaged": 0, "pacing_released": 0, "ready_s": [], "data_layers": [], "missing_data_layers": [],
                "treadmill_engaged": 0, "treadmill_timeout": 0, "treadmill_released": 0}
    pacing = [match for match in (PACING_PATTERN.search(line) for line in lines) if match and match.group(2) == gate_id]
    treadmill = [match.group(2) for match in (TREADMILL_PATTERN.search(line) for line in lines) if match and match.group(1) == gate_id]
    return {
        "available": True,
        "pacing": [{"event": match.group(1), "along_cm": float(match.group(3)), "ready": match.group(4) == "1", "elapsed_s": float(match.group(5))} for match in pacing],
        "pacing_engaged": sum(1 for match in pacing if match.group(1) == "engaged"),
        "pacing_released": sum(1 for match in pacing if match.group(1) == "released"),
        "ready_s": [float(match.group(2)) for match in (READY_PATTERN.search(line) for line in lines) if match and match.group(1) == gate_id],
        "data_layers": [{"layer": match.group(1), "realm": match.group(2), "reason": match.group(3), "accepted": match.group(4) == "1"}
                        for match in (DATA_LAYER_PATTERN.search(line) for line in lines) if match],
        "missing_data_layers": [match.group(1) for match in (MISSING_DATA_LAYER_PATTERN.search(line) for line in lines) if match],
        "treadmill_engaged": treadmill.count("engaged"),
        "treadmill_timeout": treadmill.count("timeout"),
        "treadmill_released": treadmill.count("released"),
    }


def describe_walk_weights(plan, frames):
    weights = [frame["w"] for frame in frames]
    steps = [abs(after - before) for before, after in zip(weights, weights[1:])]
    largest = max(range(len(steps)), key=lambda index: steps[index]) if steps else None
    return {
        "w_start": weights[0],
        "w_end": weights[-1],
        "w_boundary": interpolate_at([along_cm(plan, frame["pawn"]) for frame in frames], weights, 0.0),
        "max_w_step": round(steps[largest], 4) if steps else 0.0,
        "max_w_step_t": None if largest is None else frames[largest + 1]["t"],
    }


def describe_boundary_speed(plan, frames, sign):
    settings = [frame["max"] for frame in frames if frame.get("max") is not None]
    base = max(settings) if settings else None
    lowest_setting = min(settings) if settings else None
    window = [frame for frame in frames if frame["t"] >= NEAR_BOUNDARY_SKIP_SECONDS and -NEAR_BOUNDARY_BEFORE_CM <= sign * along_cm(plan, frame["pawn"]) <= NEAR_BOUNDARY_AFTER_CM]
    slowest = min(window, key=lambda frame: frame["spd"]) if window else None
    return {
        "samples": len(window),
        "min_cm_per_s": None if slowest is None else slowest["spd"],
        "boundary_cm": None if slowest is None else round(sign * along_cm(plan, slowest["pawn"])),
        "base_max_speed": base,
        "lowest_max_speed": lowest_setting,
        "ratio": None if not base or lowest_setting is None else round(lowest_setting / base, 3),
    }


def walk_checks(summary, simulate_delay):
    failures = []
    if not summary["reached"]:
        failures.append("도착점 미도달")
    failures += motion_checks(summary["motion"])
    weights = summary["weights"]
    if weights["max_w_step"] > WEIGHT_STEP_LIMIT:
        failures.append(f"K14 |Δw| {weights['max_w_step']} @{weights['max_w_step_t']}s")
    if weights["w_boundary"] is None:
        failures.append("K14 경계 통과 없음")
    elif abs(weights["w_boundary"] - BOUNDARY_WEIGHT) > BOUNDARY_WEIGHT_TOLERANCE:
        failures.append(f"K14 w(B) {weights['w_boundary']}")
    failures += log_checks(summary, simulate_delay, True)
    return failures


def motion_checks(motion):
    failures = []
    if motion["jumps"]:
        failures.append(f"K1 폰 점프 {motion['jumps']}")
    if motion["camera_jumps"]:
        failures.append(f"K1 카메라 점프 {motion['camera_jumps']}")
    if motion["fade_frames"]:
        failures.append(f"K1 페이드 {motion['fade_frames']}프레임")
    return failures


def is_delayed(simulate_delay):
    return simulate_delay is not None and simulate_delay > 0.0


def log_checks(summary, simulate_delay, is_walk):
    log = summary["log"]
    if not log["available"]:
        return ["로그 없음(K12·K13 판정 불가)"]
    failures = []
    if is_walk and not is_delayed(simulate_delay) and log["pacing_engaged"]:
        failures.append(f"K13 감속 발동 {log['pacing_engaged']}(기본 설정)")
    if is_walk and is_delayed(simulate_delay):
        ratio = summary["pace"]["ratio"]
        if ratio is not None and ratio < PACING_MIN_SPEED_RATIO:
            failures.append(f"K13 최저 속도 비율 {ratio}")
    if is_delayed(simulate_delay):
        return failures
    if not log["ready_s"] and summary["ready_state_s"] is None:
        failures.append("K12 준비 기록 없음")
    if summary.get("preload_began_while_placed"):
        if summary["ready_state_s"] is not None and summary["ready_state_s"] > READY_LIMIT_SECONDS:
            failures.append(f"K12 준비(상태, 배치 중 시작) {summary['ready_state_s']:.2f}s")
        return failures
    slow = [value for value in log["ready_s"] if value > READY_LIMIT_SECONDS]
    if slow:
        failures.append(f"K12 준비 {max(slow):.2f}s")
    return failures


def did_preload_begin_while_placed(state_events):
    return bool(state_events) and state_events[0]["state"] == PRELOADING_STATE and state_events[0]["t"] <= PLACED_PRELOAD_FIRST_FRAME_S


def summarize_walk(plan, direction, result, hitch_ms, simulate_delay=None):
    summary = {"kind": WALK_KIND, "gate_id": plan["gate_id"], "realm_id": plan["realm_id"], "direction": direction, "error": None, "problem": None, "checks": [], "hitches": []}
    if result is None:
        summary["error"] = "no result"
        return summary
    if not result.get("ok") or not result.get("frames"):
        summary["error"] = result.get("error") or "no frames"
        summary["log"] = describe_log(result.get("log_lines"), plan["gate_id"])
        return summary
    frames = result["frames"]
    sign = 1.0 if direction == WALK_DIRECTIONS[0] else -1.0
    summary.update({
        "reached": bool(result.get("reached")),
        "start": frames[0]["pawn"],
        "end": frames[-1]["pawn"],
        "walked_cm": result.get("walked_cm"),
        "motion": describe_motion(result),
        "weights": describe_walk_weights(plan, frames),
        "pace": describe_boundary_speed(plan, frames, sign),
        "log": describe_log(result.get("log_lines"), plan["gate_id"]),
        "ready_state_s": describe_state_readiness(result.get("state_events") or []),
        "preload_began_while_placed": did_preload_begin_while_placed(result.get("state_events") or []),
        "frame": frame_stats(result.get("frame_ms") or [], hitch_ms),
        "slate_max_ms": round(max(result["slate_ms"]), 1) if result.get("slate_ms") else None,
        "hitches": [dict(hitch, along_cm=round(along_cm(plan, hitch["loc"]))) for hitch in result.get("hitches") or []],
    })
    summary["problem"] = None if summary["reached"] else "did not reach the route end"
    summary["checks"] = walk_checks(summary, simulate_delay)
    return summary


def empty_doors():
    return {"doors": [], "closed_t": None, "open_t": None, "open_at_end": None, "opened": []}


def find_closing_index(times, blocking_sets, closed_set, departure_t):
    closings = [index for index, blocking in enumerate(blocking_sets) if blocking == closed_set]
    if not closings:
        return None
    if departure_t is None:
        return closings[-1]
    in_effect = max((index for index, t in enumerate(times) if t <= departure_t), default=None)
    if in_effect is not None and blocking_sets[in_effect] == closed_set:
        return in_effect
    return next((index for index in closings if times[index] > departure_t), None)


def describe_doors(box_events, departure_t=None):
    if not box_events:
        return empty_doors()
    times = [event["t"] for event in box_events]
    blocking_sets = [frozenset(event["blocking"]) for event in box_events]
    closed_set = frozenset.union(*blocking_sets)
    doors = closed_set - frozenset.intersection(*blocking_sets)
    if not doors:
        return empty_doors()
    closing = find_closing_index(times, blocking_sets, closed_set, departure_t)
    opening = None
    if closing is not None:
        opening = next((index for index in range(closing + 1, len(blocking_sets)) if blocking_sets[index] < closed_set), None)
    return {
        "doors": sorted(doors),
        "closed_t": None if closing is None else times[closing],
        "open_t": None if opening is None else times[opening],
        "open_at_end": blocking_sets[-1] < closed_set,
        "opened": sorted(closed_set - blocking_sets[-1]),
    }


def value_range(values):
    present = [value for value in values if value is not None]
    if not present:
        return None
    return [min(present), max(present)]


def span(events, start_name, end_name):
    if start_name not in events or end_name not in events:
        return None
    return round(events[end_name] - events[start_name], 2)


def describe_deck(plan, direction, frames):
    arrive_z = plan["landings"]["realm" if direction == LIFT_DIRECTIONS[0] else "field"]["loc"][2]
    ride_indexes = [index for index, frame in enumerate(frames) if frame["ph"] == "ride"]
    moving = next((index for index in ride_indexes if abs(frames[index]["dv"]) > DECK_MOVING_SPEED_CM), None)
    if moving is None:
        return {"motion_start_t": None, "motion_end_t": None, "motion_s": None, "landing_error_cm": None, "travel_cm": None, "max_step_cm": None}
    start_index = max(moving - 1, 0)
    stop = next((index for index in range(moving, len(frames))
                 if abs(frames[index]["deck"][2] - arrive_z) <= DECK_STOP_WINDOW_CM and abs(frames[index]["dv"]) < DECK_STOPPED_SPEED_CM), None)
    end_index = len(frames) - 1 if stop is None else stop
    deck_z = [frames[index]["deck"][2] for index in range(start_index, end_index + 1)]
    steps = [abs(after - before) for before, after in zip(deck_z, deck_z[1:])]
    return {
        "motion_start_t": frames[start_index]["t"],
        "motion_end_t": None if stop is None else frames[stop]["t"],
        "motion_s": None if stop is None else round(frames[stop]["t"] - frames[start_index]["t"], 2),
        "landing_error_cm": None if stop is None else round(frames[stop]["deck"][2] - arrive_z, 2),
        "travel_cm": round(abs(deck_z[-1] - deck_z[0]), 1),
        "max_step_cm": round(max(steps), 2) if steps else 0.0,
    }


def split_treadmill_segments(riding):
    segments = []
    current = []
    for frame in riding:
        ring_speed = frame.get("ring_v")
        if ring_speed is not None and abs(ring_speed) > TREADMILL_RING_SPEED_CM:
            current.append(frame)
            continue
        if current:
            segments.append(current)
            current = []
    if current:
        segments.append(current)
    return segments


def describe_treadmill_segment(frames, cruise):
    holds = [index for index, frame in enumerate(frames) if abs(frame["dv"]) < TREADMILL_HOLD_SPEED_CM]
    errors = [abs(abs(frame["dv"]) + abs(frame["ring_v"]) - cruise) for frame in frames]
    start_t, end_t = frames[0]["t"], frames[-1]["t"]
    first_hold = frames[holds[0]]["t"] if holds else None
    last_hold = frames[holds[-1]]["t"] if holds else None
    return {
        "start_t": start_t,
        "seconds": round(end_t - start_t, 2),
        "brake_s": None if first_hold is None else round(first_hold - start_t, 2),
        "hold_s": None if first_hold is None else round(last_hold - first_hold, 2),
        "resume_s": None if last_hold is None else round(end_t - last_hold, 2),
        "hold_deck_z": None if not holds else frames[holds[0]]["deck"][2],
        "ring_max_cm_per_s": round(max(abs(frame["ring_v"]) for frame in frames), 1),
        "invariant_max_error": round(max(errors), 2),
        "invariant_p95_error": round(percentile(errors, P95_FRACTION), 2),
    }


def describe_treadmill(riding, cruise):
    return {
        "ring_measured": any(frame.get("ring_v") is not None for frame in riding),
        "segments": [describe_treadmill_segment(segment, cruise) for segment in split_treadmill_segments(riding)],
    }


def shaft_weight_at_depth(plan, riding, depth_cm):
    top_z = plan["landings"]["field"]["loc"][2]
    return interpolate_at([top_z - frame["deck"][2] for frame in riding], [frame["w"] for frame in riding], depth_cm)


def ride_checks(summary, plan, simulate_delay):
    failures = []
    if not summary["arrived"]:
        failures.append("덱 도착 아님")
    if summary["path_reached"] is False:
        failures.append("접근 경로 미도달")
    if summary["arrived"] and not summary["exit_reached"]:
        failures.append("출구 경로 미도달")
    failures += motion_checks(summary["motion"])
    if summary["airborne_frames"]:
        failures.append(f"공중 {summary['airborne_frames']}")
    if summary["off_deck_frames"]:
        failures.append(f"기반 이탈 {summary['off_deck_frames']}")
    failures += deck_checks(summary["deck"], plan)
    failures += treadmill_checks(summary, simulate_delay)
    shaft_w = summary["shaft_w"]
    if shaft_w is None:
        failures.append("K14 갱도 w(20m) 없음")
    elif abs(shaft_w - BOUNDARY_WEIGHT) > BOUNDARY_WEIGHT_TOLERANCE:
        failures.append(f"K14 갱도 w(20m) {shaft_w}")
    if summary["arrived"] and not summary["doors"]["open_at_end"]:
        failures.append("문 열림 없음")
    failures += log_checks(summary, simulate_delay, False)
    return failures


def deck_checks(deck, plan):
    failures = []
    if deck["landing_error_cm"] is None:
        return ["K11 덱 정지 없음"]
    if abs(deck["landing_error_cm"]) > LANDING_TOLERANCE_CM:
        failures.append(f"K11 착지 오차 {deck['landing_error_cm']}cm")
    if deck["max_step_cm"] > DECK_STEP_LIMIT_CM:
        failures.append(f"K11 덱 프레임 이동 {deck['max_step_cm']}cm")
    if abs(deck["travel_cm"] - plan["drop_cm"]) > 2.0 * LANDING_TOLERANCE_CM:
        failures.append(f"K11 덱 이동 {deck['travel_cm']}cm")
    return failures


def treadmill_checks(summary, simulate_delay):
    treadmill = summary["treadmill"]
    segments = treadmill["segments"]
    failures = []
    if not treadmill["ring_measured"]:
        failures.append("링 측정 없음(라이너 없음)")
    for segment in segments:
        if segment["invariant_max_error"] > TREADMILL_INVARIANT_LIMIT_CM:
            failures.append(f"K11 불변식 오차 {segment['invariant_max_error']}cm/s")
    if not is_delayed(simulate_delay):
        if segments:
            failures.append(f"K11 트레드밀 {len(segments)}회(기본 설정)")
        motion_s = summary["deck"]["motion_s"]
        if motion_s is not None and abs(motion_s - summary["expected_ride_s"]) > RIDE_TIME_TOLERANCE_SECONDS:
            failures.append(f"K11 운행 {motion_s}s(예상 {summary['expected_ride_s']})")
        return failures
    if len(segments) > 1:
        failures.append(f"K11 트레드밀 {len(segments)}회(지연 모의, 운행마다 기대 0~1)")
    log = summary["log"]
    if log["available"] and log["treadmill_engaged"] > 1:
        failures.append(f"K11 트레드밀 진입 로그 {log['treadmill_engaged']}회")
    return failures


def summarize_ride(plan, direction, result, hitch_ms, simulate_delay=None):
    summary = {"kind": LIFT_KIND, "gate_id": plan["gate_id"], "realm_id": plan["realm_id"], "direction": direction, "error": None, "problem": None, "checks": [], "hitches": []}
    if result is None:
        summary["error"] = "no result"
        return summary
    if not result.get("ok") or not result.get("frames"):
        summary["error"] = result.get("error") or "no frames"
        summary["log"] = describe_log(result.get("log_lines"), plan["gate_id"])
        return summary
    frames = result["frames"]
    events = result.get("events") or {}
    departure = events.get("departure")
    deck_prefix = (result.get("lift_actor") or "") + "."
    riding = [frame for frame in frames if frame["ph"] == "ride" and departure is not None and frame["t"] >= departure]
    on_deck = [frame for frame in frames if (frame.get("base") or "").startswith(deck_prefix)]
    summary.update({
        "arrived": bool(result.get("arrived")),
        "exit_reached": bool(result.get("exit_reached")),
        "path_reached": None if not result.get("path") else "path_end" in events,
        "timeout_phase": result.get("timeout_phase"),
        "lift_actor": result.get("lift_actor"),
        "liner_actor": result.get("liner_actor"),
        "ring_count": result.get("ring_count"),
        "deck_top_source": result.get("deck_top_source"),
        "stream_wait_s": result.get("stream_wait_s"),
        "settle_s": result.get("settle_s"),
        "settle_streaming_loaded": result.get("settle_streaming_loaded"),
        "call_wait_s": span(events, "call_start", "call_end"),
        "call_unfinished": "call_start" in events and "call_end" not in events,
        "first_on_deck_t": on_deck[0]["t"] if on_deck else None,
        "board_t": events.get("board"),
        "departure_t": departure,
        "arrival_t": events.get("arrival"),
        "exit_end_t": events.get("exit_end"),
        "expected_ride_s": round(plan["ride_s"], 2),
        "deck": describe_deck(plan, direction, frames),
        "treadmill": describe_treadmill(riding, plan["cruise_cm_per_second"]),
        "shaft_w": shaft_weight_at_depth(plan, riding, SHAFT_WEIGHT_DEPTH_CM),
        "riding_frames": len(riding),
        "airborne_frames": sum(1 for frame in riding if frame.get("air")),
        "off_deck_frames": sum(1 for frame in riding if not (frame.get("base") or "").startswith(deck_prefix)),
        "feet_range": value_range(frame["feet"] for frame in riding),
        "modes": sorted({str(frame.get("mode")) for frame in riding}),
        "motion": describe_motion(result),
        "doors": describe_doors(result.get("box_events") or [], departure),
        "log": describe_log(result.get("log_lines"), plan["gate_id"]),
        "ready_state_s": describe_state_readiness(result.get("state_events") or []),
        "preload_began_while_placed": did_preload_begin_while_placed(result.get("state_events") or []),
        "frame": frame_stats(result.get("frame_ms") or [], hitch_ms),
        "slate_max_ms": round(max(result["slate_ms"]), 1) if result.get("slate_ms") else None,
        "hitches": list(result.get("hitches") or []),
    })
    summary["problem"] = ride_problem(summary)
    summary["checks"] = ride_checks(summary, plan, simulate_delay)
    return summary


def ride_problem(summary):
    if not summary["arrived"]:
        return f"no arrival (timeout in phase {summary['timeout_phase']})"
    if not summary["exit_reached"]:
        return f"exit path not finished (timeout in phase {summary['timeout_phase']})"
    return None


def collect_data_layer_activations(summaries):
    activations = {}
    for summary in summaries:
        for entry in (summary.get("log") or {}).get("data_layers") or []:
            activations.setdefault(entry["realm"], []).append(entry)
    return activations


def run_checks(plans, summaries, simulate_delay=None):
    if not any((summary.get("log") or {}).get("available") for summary in summaries):
        return ["K17 로그 없음"]
    activations = collect_data_layer_activations(summaries)
    failures = []
    if is_delayed(simulate_delay):
        walk_summaries = [summary for summary in summaries if summary.get("kind") == WALK_KIND]
        engaged = sum((summary.get("log") or {}).get("pacing_engaged", 0) for summary in walk_summaries)
        if walk_summaries and engaged == 0:
            failures.append("K13 감속 발동 로그 0건(지연 모의, 기대 1건 이상)")
    if is_delayed(simulate_delay) and simulate_delay >= TREADMILL_EXPECT_DELAY_SECONDS:
        ride_summaries = [summary for summary in summaries if summary.get("kind") == LIFT_KIND]
        segments = sum(len(((summary.get("treadmill") or {}).get("segments")) or []) for summary in ride_summaries)
        engaged = sum((summary.get("log") or {}).get("treadmill_engaged", 0) for summary in ride_summaries)
        if ride_summaries and (segments == 0 or engaged == 0):
            failures.append(f"K11 트레드밀 구간 {segments}·진입 로그 {engaged}(지연 모의, 실행 전체 기대 1 이상)")
    for realm_id in sorted({plan["realm_id"] for plan in plans}):
        count = len(activations.get(realm_id, []))
        if count != 1:
            failures.append(f"K17 {realm_id} 게임플레이 DL 활성화 로그 {count}회(기대 1)")
        if any(not entry["accepted"] for entry in activations.get(realm_id, [])):
            failures.append(f"K17 {realm_id} 활성화 거부(accepted 0)")
    return failures


def pooled_stats(results, hitch_ms):
    pooled = []
    for result in results:
        if result and result.get("ok"):
            pooled += result.get("frame_ms") or []
    return frame_stats(pooled, hitch_ms)


def format_number(value, digits=1):
    if value is None:
        return "-"
    return f"{value:.{digits}f}"


def format_stats_row(label_cells, stats, slate_max):
    if stats is None:
        return f"| {label_cells} | - | - | - | - | - | - |"
    return f"| {label_cells} | {stats['samples']} | {stats['mean_ms']:.1f} | {stats['p95_ms']:.1f} | {stats['max_ms']:.1f} | {format_number(slate_max)} | {stats['over_hitch']} |"


def describe_walk_plan(plan):
    spur = "없음" if plan["spur_cm"] is None else f"{plan['spur_cm']:.0f}cm"
    return (
        f"- {plan['gate_id']}(걷기): 경로 {len(plan['route'])}점 {plan['route_cm']:.0f}cm(지선 {spur}), 시작 {[round(value) if value is not None else None for value in plan['start']]}, "
        f"경계 B = FieldReturn + 축 x {plan['approach_cm']:.0f}cm, 이탈 {plan['departure_cm']:.0f}cm, 시간 한도 {plan['seconds']:.0f}초(+지연 모의)"
    )


def describe_lift_plan(plan):
    field = plan["landings"]["field"]
    realm = plan["landings"]["realm"]
    return (
        f"- {plan['gate_id']}(승강기): 상단 착지 {[round(value, 1) for value in field['loc']]} 출구 yaw {field['yaw']:.0f}, 하단 착지 {[round(value, 1) for value in realm['loc']]} 출구 yaw {realm['yaw']:.0f}, "
        f"낙차 {plan['drop_cm']:.0f}cm, 예상 운행 {plan['ride_s']:.2f}초, 걷는 길 {plan['walk_cm']:.0f}cm(접근 {len(plan['field_path'])}점·출구 {len(plan['realm_path'])}점), "
        f"트레드밀 최대 {plan['max_treadmill_s']:.0f}초, 시간 한도 {plan['seconds']:.0f}초(+지연 모의)"
    )


def describe_result(summary):
    if summary["error"]:
        return f"실패: {spots.table_cell(summary['error'])}"
    if summary["problem"]:
        return spots.table_cell(summary["problem"])
    return "도착"


def describe_motion_cells(motion):
    fade = motion["fade_frames"] if motion["fade_measured"] else "측정 불가"
    return f"{motion['jumps']} / {motion['camera_jumps']} / {fade} | {motion['max_pawn_step_cm']:.1f} / {motion['max_camera_step_cm']:.1f}"


def describe_ready(summary):
    log_text = ", ".join(f"{value:.2f}" for value in summary["log"]["ready_s"]) or "-"
    placed_note = " (배치 중 시작)" if summary.get("preload_began_while_placed") else ""
    return f"{log_text} / {format_number(summary['ready_state_s'], 2)}{placed_note}"


def describe_pace(pace):
    if pace["min_cm_per_s"] is None:
        speed = "-"
    else:
        speed = f"{pace['min_cm_per_s']:.0f} @{pace['boundary_cm']:+d}"
    ratio = "-" if pace["ratio"] is None else f"{format_number(pace['lowest_max_speed'], 0)}/{format_number(pace['base_max_speed'], 0)} ({pace['ratio']:.2f})"
    return f"{speed} | {ratio}"


def describe_checks(summary):
    return spots.table_cell(", ".join(summary["checks"]) or "통과")


def render_walk_rows(walks):
    lines = [
        "",
        "## 연결로 걷기",
        "",
        "- 점프 = 프레임 사이 이동 5000cm 초과(폰 / 카메라), 페이드 = 카메라 fade_amount > 0 프레임. 경계 근처 최소 속도 @진행 방향 B 기준 위치(cm, 음수 = B 전), 최대 속도 설정 = 걷는 동안 최솟값/최댓값(감속 배율)",
        "- 감속·준비는 에디터 로그(Realm: pacing engaged·released, SeamlessTravel 'ready after') / 상태 = 이동 상태 PRELOADING→READY_TO_TRAVEL 시간",
        "",
        "| 관문 | 방향 | 결과 | 걸은 거리(cm) | 점프 폰/카메라/페이드 | 프레임 최대 이동 폰/카메라(cm) | w 시작 -> 끝 | w(B) | 최대 Δw | 경계 근처 최소 속도(cm/s) | 최대 속도 설정 최소/기본 | 감속 발동/해제 | 준비 로그/상태(s) | 참고 판정 |",
        "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|",
    ]
    for summary in walks:
        if summary["error"]:
            lines.append(f"| {summary['gate_id']} | {summary['direction']} | {describe_result(summary)} | - | - | - | - | - | - | - | - | - | - | - |")
            continue
        weights = summary["weights"]
        log = summary["log"]
        pacing = f"{log['pacing_engaged']} / {log['pacing_released']}" if log["available"] else "로그 없음"
        lines.append(
            f"| {summary['gate_id']} | {summary['direction']} | {describe_result(summary)} | {format_number(summary['walked_cm'], 0)} | {describe_motion_cells(summary['motion'])} "
            f"| {weights['w_start']} -> {weights['w_end']} | {format_number(weights['w_boundary'], 3)} | {weights['max_w_step']} | {describe_pace(summary['pace'])} "
            f"| {pacing} | {describe_ready(summary)} | {describe_checks(summary)} |"
        )
    return lines


def describe_call_wait(ride):
    if ride["call_unfinished"]:
        return "미완"
    return format_number(ride["call_wait_s"], 2)


def describe_door_times(doors):
    if not doors["doors"]:
        return "문 상자 변화 없음"
    end_text = f"열림 {', '.join(doors['opened'])}" if doors["open_at_end"] else "닫힘"
    return f"{format_number(doors['closed_t'], 2)} -> {format_number(doors['open_t'], 2)} (끝 {end_text})"


def describe_treadmill_cell(treadmill):
    if not treadmill["ring_measured"]:
        return "링 측정 없음"
    segments = treadmill["segments"]
    if not segments:
        return "없음"
    return f"{len(segments)}회 " + ", ".join(f"{segment['start_t']}s·{segment['seconds']}s" for segment in segments)


def render_ride_rows(rides):
    lines = [
        "",
        "## 승강기 운행",
        "",
        "- t(s) = 폰을 시작점에 내려놓고 지면이 잡힌 뒤부터. 출발 = 덱 수직 속도 5cm/s 초과, 도착 = 도착 착지에서 덱이 0.3초 멈춤(정지 + 0.3초). 덱 움직임 = 움직이기 직전 프레임 ~ 정지 프레임(예상 = 사다리꼴 운행 시간)",
        "- 착지 오차 = 정지 프레임 덱 윗면 z - 도착 착지 z. 덱 이동 = 움직임 구간 덱 z 변화, 프레임 최대 = 프레임 사이 덱 z 변화 최댓값",
        "",
        "| 관문 | 방향 | 결과 | 호출 대기(s) | 출발(s) | 도착(s) | 덱 움직임(s, 예상) | 착지 오차(cm) | 덱 이동 / 프레임 최대(cm) | 트레드밀 | 갱도 w(20m) | 문 닫힘 -> 열림(s) | 출구 끝(s) |",
        "|---|---|---|---|---|---|---|---|---|---|---|---|---|",
    ]
    for ride in rides:
        if ride["error"]:
            lines.append(f"| {ride['gate_id']} | {ride['direction']} | {describe_result(ride)} | - | - | - | - | - | - | - | - | - | - |")
            continue
        deck = ride["deck"]
        lines.append(
            f"| {ride['gate_id']} | {ride['direction']} | {describe_result(ride)} | {describe_call_wait(ride)} | {format_number(ride['departure_t'], 2)} | {format_number(ride['arrival_t'], 2)} "
            f"| {format_number(deck['motion_s'], 2)} ({ride['expected_ride_s']:.2f}) | {format_number(deck['landing_error_cm'], 2)} | {format_number(deck['travel_cm'], 1)} / {format_number(deck['max_step_cm'], 2)} "
            f"| {spots.table_cell(describe_treadmill_cell(ride['treadmill']))} | {format_number(ride['shaft_w'], 3)} | {spots.table_cell(describe_door_times(ride['doors']))} | {format_number(ride['exit_end_t'], 2)} |"
        )
    lines += [
        "",
        "### 트레드밀 구간(링이 움직인 프레임)",
        "",
        "| 관문 | 방향 | 시작(s) | 길이(s) | 감속 / 대기 / 재개(s) | 대기 덱 z(cm) | 링 최대(cm/s) | 불변식 오차 최대 / 95%(cm/s) |",
        "|---|---|---|---|---|---|---|---|",
    ]
    segment_rows = [(ride, segment) for ride in rides if not ride["error"] for segment in ride["treadmill"]["segments"]]
    if not segment_rows:
        lines.append("| 없음 | - | - | - | - | - | - | - |")
    for ride, segment in segment_rows:
        lines.append(
            f"| {ride['gate_id']} | {ride['direction']} | {segment['start_t']} | {segment['seconds']} | {format_number(segment['brake_s'], 2)} / {format_number(segment['hold_s'], 2)} / {format_number(segment['resume_s'], 2)} "
            f"| {format_number(segment['hold_deck_z'], 1)} | {segment['ring_max_cm_per_s']} | {segment['invariant_max_error']} / {segment['invariant_p95_error']} |"
        )
    lines += [
        "",
        "### 탑승 중 지표(출발~도착)와 참고 판정",
        "",
        "| 관문 | 방향 | 프레임 | 발-덱 최소~최대(cm) | 공중 | 기반 이탈 | 이동 모드 | 덱 윗면 측정 | 라이너(링 수) | 점프 폰/카메라/페이드 | 프레임 최대 이동 폰/카메라(cm) | 준비 로그/상태(s) | 트레드밀 로그 진입/시간 초과/해제 | 참고 판정 |",
        "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|",
    ]
    for ride in rides:
        if ride["error"]:
            continue
        feet = "-" if ride["feet_range"] is None else f"{ride['feet_range'][0]} ~ {ride['feet_range'][1]}"
        log = ride["log"]
        treadmill_log = f"{log['treadmill_engaged']} / {log['treadmill_timeout']} / {log['treadmill_released']}" if log["available"] else "로그 없음"
        lines.append(
            f"| {ride['gate_id']} | {ride['direction']} | {ride['riding_frames']} | {feet} | {ride['airborne_frames']} | {ride['off_deck_frames']} | {spots.table_cell(', '.join(ride['modes']) or '-')} "
            f"| {ride['deck_top_source']} | {spots.table_cell(ride['liner_actor'] or '-')}({ride['ring_count']}) | {describe_motion_cells(ride['motion'])} | {describe_ready(ride)} | {treadmill_log} | {describe_checks(ride)} |"
        )
    return lines


def render_log_rows(summaries):
    activations = collect_data_layer_activations(summaries)
    lines = ["", "## 런타임 로그 발췌", ""]
    if not activations:
        lines.append("- 게임플레이 DL 활성화: 없음")
    for realm_id, entries in sorted(activations.items()):
        described = ", ".join(f"{entry['layer']}({entry['reason']}, accepted {int(entry['accepted'])})" for entry in entries)
        lines.append(f"- 게임플레이 DL 활성화 {realm_id}: {len(entries)}회 {described}")
    for summary in summaries:
        log = summary.get("log")
        if not log or not log["available"]:
            continue
        missing = f", DL 없음 {log['missing_data_layers']}" if log["missing_data_layers"] else ""
        pacing = "; ".join(f"{entry['event']} along {entry['along_cm']:.0f} ready {int(entry['ready'])} {entry['elapsed_s']:.2f}s" for entry in log["pacing"][:6]) or "없음"
        lines.append(f"- {summary['gate_id']} {summary['direction']}: 감속 {pacing}{missing}")
    return lines


def render_hitch_rows(summaries, hitch_ms):
    lines = ["", f"## 끊김 순간(프레임 시간 {hitch_ms:.0f}ms 초과)", ""]
    hitch_rows = [(summary, hitch) for summary in summaries for hitch in summary["hitches"]]
    if not hitch_rows:
        return lines + ["없음"]
    lines += ["| 관문 | 방향 | t(초) | 프레임 ms | slate ms | 경계 좌표 along(cm) | 월드 좌표(cm) | 단계 | 이동 상태 | w |", "|---|---|---|---|---|---|---|---|---|---|"]
    for summary, hitch in hitch_rows:
        along = hitch.get("along_cm")
        lines.append(
            f"| {summary['gate_id']} | {summary['direction']} | {hitch['t']} | {hitch['frame_ms']} | {hitch['slate_ms']} | {'-' if along is None else along} | {hitch['loc']} "
            f"| {hitch.get('phase') or '-'} | {hitch.get('state')} | {hitch.get('w')} |"
        )
    return lines


def render_summary(run_name, created, plans, summaries, pooled, hitch_ms, speed_cm_per_second, notes=(), simulate_delay=None, run_failures=()):
    lines = [
        f"# PIE 연결로 걷기·승강기 운행 {run_name}",
        "",
        f"- 실행: {created}, 이동 속도 가정 {speed_cm_per_second:.0f}cm/s, 끊김 기준 {hitch_ms:.0f}ms 초과",
        "- 참고 판정: K1 폰·카메라 프레임 이동 5000cm 이하·페이드 0 / K14 걷기 |Δw| ≤ 0.01·w(B) 0.5±0.02, 갱도 w(20m) 0.5±0.02 / K13 기본 설정 감속 발동 0, 지연 모의면 발동 ≥ 1·최대 속도 설정 최소 ≥ 기본의 30%",
        "- K12 준비 ≤ 1.0s(로그 'ready after', 기본 설정만. 첫 프레임이 이미 PRELOADING이면 시작점이 미리 불러오기 구역 안이라 배치 중에 시작된 것이므로 걷기 시작부터의 상태 시간으로 판정) / K11 덱 움직임 예상 ±0.2s(기본 설정)·착지 ±10cm·덱 프레임 이동 ≤ 100cm·트레드밀 불변식 ±1cm/s·지연 모의 10s 이상이면 실행 전체 트레드밀 1회 이상(운행마다 1회 이하) / K17 관문 이계마다 게임플레이 DL 활성화 로그 1회",
        "- 경계 좌표 along(cm) = FieldReturn→Arrival 직선 축 투영 - 접근 길이(B = 0, 필드 쪽 음수)",
    ]
    if simulate_delay is not None:
        lines.append(f"- 지연 모의: {SIMULATED_DELAY_VARIABLE} = {simulate_delay:g}초(관문마다 걷기·운행 동안, 끝나면 0)")
    for plan in plans:
        lines.append(describe_lift_plan(plan) if plan["kind"] == LIFT_KIND else describe_walk_plan(plan))
    walks = [summary for summary in summaries if summary["kind"] == WALK_KIND]
    rides = [summary for summary in summaries if summary["kind"] == LIFT_KIND]
    if walks:
        lines += render_walk_rows(walks)
    if rides:
        lines += render_ride_rows(rides)
    lines += render_log_rows(summaries)
    lines += ["", "## 실행 단위 판정", ""] + [f"- {failure}" for failure in run_failures or ["통과"]]
    lines += ["", "## 프레임 시간(ms)", "", "| 관문 | 방향 | 표본 | 평균 | 95백분위 | 최대 | 최대(slate) | 끊김 횟수 |", "|---|---|---|---|---|---|---|---|"]
    for summary in summaries:
        if summary["error"]:
            continue
        lines.append(format_stats_row(f"{summary['gate_id']} | {summary['direction']}", summary["frame"], summary["slate_max_ms"]))
    lines.append(format_stats_row("전체 | -", pooled, None))
    if notes:
        lines += ["", "## 주의", ""] + [f"- {note}" for note in notes]
    lines += render_hitch_rows(summaries, hitch_ms)
    return "\n".join(lines) + "\n"


def write_result(out_dir, name, result):
    spots.write_text(os.path.join(out_dir, name), json.dumps(result, ensure_ascii=False, separators=(",", ":")) + "\n")


def set_console_variable(value):
    result = spots.call_probe({"action": "console", "variable": SIMULATED_DELAY_VARIABLE, "value": value})
    read_back = result.get("read_back")
    if read_back is not None and abs(read_back - value) > CONSOLE_READ_BACK_TOLERANCE:
        raise spots.ProbeError(f"{SIMULATED_DELAY_VARIABLE} read back {read_back} after setting {value:g} (variable not registered?)")
    return read_back


def apply_simulated_delay(seconds):
    if seconds is None:
        return None
    try:
        read_back = set_console_variable(seconds)
    except Exception as error:
        return f"simulated delay not applied: {error!r}"
    spots.log(f"{SIMULATED_DELAY_VARIABLE} = {seconds:g} (read back {read_back})")
    return None


def reset_simulated_delay(seconds):
    if seconds is None:
        return None
    try:
        set_console_variable(0.0)
    except Exception as error:
        message = f"{SIMULATED_DELAY_VARIABLE}를 0으로 되돌리지 못함, 에디터 콘솔에서 직접 0으로 둘 것: {error!r}"
        spots.log(message)
        return message
    spots.log(f"{SIMULATED_DELAY_VARIABLE} = 0")
    return None


def is_finished_walk(result):
    return bool(result and result.get("ok") and result.get("reached"))


def is_finished_ride(result):
    return bool(result and result.get("ok") and result.get("arrived") and result.get("exit_reached"))


def run_directions(plan, out_dir, hitch_ms, extra_seconds, delay_error):
    is_lift = plan["kind"] == LIFT_KIND
    directions = LIFT_DIRECTIONS if is_lift else WALK_DIRECTIONS
    make_request = ride_request if is_lift else walk_request
    is_finished = is_finished_ride if is_lift else is_finished_walk
    outputs = {}
    for direction in directions:
        reason = delay_error
        if not reason and direction == directions[1] and not is_finished(outputs.get(directions[0])):
            reason = f"skipped: {directions[0]} did not reach its end"
        if reason:
            result = {"ok": False, "error": reason}
        else:
            request = make_request(plan, direction, hitch_ms, extra_seconds)
            result = call_with_log(request, request["seconds"] + STREAM_WAIT_SECONDS + RESULT_MARGIN_SECONDS)
        write_result(out_dir, f"{plan['gate_id']}_{direction}.json", result)
        spots.log(f"{plan['gate_id']} {direction}: ok={result.get('ok')} reached={result.get('reached', result.get('exit_reached'))} frames={len(result.get('frames') or [])} jumps={len(result.get('jumps') or [])} error={result.get('error')}")
        outputs[direction] = result
    return outputs


def run_gate(plan, out_dir, hitch_ms, simulate_delay):
    notes = []
    try:
        delay_error = apply_simulated_delay(simulate_delay)
        if delay_error:
            notes.append(f"{plan['gate_id']}: {delay_error}")
        outputs = run_directions(plan, out_dir, hitch_ms, simulate_delay or 0.0, delay_error)
    finally:
        reset_error = reset_simulated_delay(simulate_delay)
        if reset_error:
            notes.append(reset_error)
    return outputs, notes


def summarize_gate(plan, outputs, hitch_ms, simulate_delay):
    if plan["kind"] == LIFT_KIND:
        return [summarize_ride(plan, direction, outputs[direction], hitch_ms, simulate_delay) for direction in LIFT_DIRECTIONS]
    return [summarize_walk(plan, direction, outputs[direction], hitch_ms, simulate_delay) for direction in WALK_DIRECTIONS]


def build_ground_reader(regions):
    ground_cm = spots.build_ground_lookup(regions)

    def ground_at(region_name, x_cm, y_cm):
        if region_name not in regions:
            return None
        return ground_cm(region_name, x_cm, y_cm)

    return ground_at


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description="PIE 연결로 왕복 걷기·승강기 왕복 운행(실제 이동 입력, 연속 세계) + 프레임·로그 요약")
    parser.add_argument("--spots", default=spots.DEFAULT_SPOTS, help="필드·이계 높이맵 위치를 읽을 지점 목록 JSON(기본 Tools/WorldGen/topdown_spots.json)")
    parser.add_argument("--run", default=None, help="결과 폴더 이름(기본 현재 시각), Saved/WorldGen/PieWalk/<run>/")
    parser.add_argument("--gates", default="", help="쉼표로 구분한 관문 ID 또는 접두어(SunkenStarfield 등), 비우면 전부")
    parser.add_argument("--speed", type=float, default=600.0, help="시간 한도 산정에 쓰는 이동 속도 cm/s")
    parser.add_argument("--hitch-ms", type=float, default=100.0, help="이 값을 넘는 프레임을 끊김으로 센다")
    parser.add_argument("--warmup", type=float, default=15.0, help="PIE 시작 뒤 대기 초")
    parser.add_argument("--simulate-delay", type=float, default=None, metavar="SECONDS", help=f"관문마다 걷기·운행 동안 {SIMULATED_DELAY_VARIABLE}를 이 초로 두고 끝나면 0으로 되돌린다(준비 지연 모의, K13은 30, K11은 15)")
    parser.add_argument("--overwrite", action="store_true", help="이미 있는 run 폴더에 덮어쓴다")
    parser.add_argument("--offline", action="store_true", help="PIE 없이 --run 폴더의 관문별 결과 JSON으로 summary.md만 다시 만든다(--simulate-delay는 그 실행과 같게 준다)")
    args = parser.parse_args(argv)
    if args.offline and not args.run:
        parser.error("--offline needs --run")
    if args.simulate_delay is not None and args.simulate_delay < 0:
        parser.error("--simulate-delay must be >= 0")
    if args.speed <= 0:
        parser.error("--speed must be > 0")
    return args


def main(argv=None):
    spots.configure_output()
    args = parse_args(argv)
    regions, _ = spots.load_spots(os.path.abspath(args.spots))
    ground_at = build_ground_reader(regions)
    selected = select_gates(load_gates(), args.gates)
    run_name = args.run or time.strftime("%Y%m%d_%H%M%S")
    out_dir = os.path.join(WALK_ROOT, run_name)
    summary_path = os.path.join(out_dir, "summary.md")
    if os.path.exists(summary_path) and not args.overwrite and not args.offline:
        raise SystemExit(f"run folder already exists (use another --run or --overwrite): {out_dir}")
    plans = [build_gate_plan(realm_id, gate, ground_at, args.speed) for realm_id, gate in selected]
    os.makedirs(out_dir, exist_ok=True)
    if read_log_size() is None:
        spots.log(f"editor log not found, log checks will fail: {EDITOR_LOG}")
    summaries = []
    raw_results = []
    notes = []
    if args.offline:
        for plan in plans:
            outputs = load_saved_outputs(plan, out_dir)
            summaries += summarize_gate(plan, outputs, args.hitch_ms, args.simulate_delay)
            raw_results += list(outputs.values())
        return finish_run(run_name, out_dir, summary_path, plans, summaries, raw_results, notes, args)
    try:
        spots.start_pie(args.warmup)
    except spots.PieStartError as error:
        spots.log(str(error))
        return 2
    try:
        for plan in plans:
            outputs, gate_notes = run_gate(plan, out_dir, args.hitch_ms, args.simulate_delay)
            notes += gate_notes
            summaries += summarize_gate(plan, outputs, args.hitch_ms, args.simulate_delay)
            raw_results += list(outputs.values())
    finally:
        spots.stop_pie()
    return finish_run(run_name, out_dir, summary_path, plans, summaries, raw_results, notes, args)


def load_saved_outputs(plan, out_dir):
    directions = LIFT_DIRECTIONS if plan["kind"] == LIFT_KIND else WALK_DIRECTIONS
    outputs = {}
    for direction in directions:
        result_path = os.path.join(out_dir, f"{plan['gate_id']}_{direction}.json")
        outputs[direction] = spots.read_json(result_path) if os.path.exists(result_path) else {"ok": False, "error": f"missing {result_path}"}
    return outputs


def finish_run(run_name, out_dir, summary_path, plans, summaries, raw_results, notes, args):
    failures = run_checks(plans, summaries, args.simulate_delay)
    text = render_summary(run_name, time.strftime("%Y-%m-%d %H:%M:%S"), plans, summaries, pooled_stats(raw_results, args.hitch_ms), args.hitch_ms, args.speed, notes, args.simulate_delay, failures)
    spots.write_text(summary_path, text)
    spots.log(f"summary: {summary_path}")
    has_failure = failures or any(summary["error"] or summary["problem"] or summary["checks"] for summary in summaries)
    return 1 if has_failure else 0


if __name__ == "__main__":
    sys.exit(main())
