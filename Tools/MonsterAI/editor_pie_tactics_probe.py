"""실행 환경: TDGame 언리얼 에디터 안(PythonScriptPlugin 원격 실행). LV-Cambat PIE 중 시나리오는 슬레이트 틱 콜백이 프레임마다 진행한다(원격 실행이 끝나도 콜백은 계속 돈다).
실행 명령: python Tools/run_in_editor.py Tools/MonsterAI/editor_pie_tactics_probe.py (요청 파일 Saved/MonsterAI/TacticsPie/request.json {"action": inspect|prepare|start|status|cleanup, "suite": behavior|crowd}; 한 번에 돌리려면 python Tools/MonsterAI/pie_check_tactics.py)
출력: Saved/MonsterAI/TacticsPie/probe_state.json(단계·사건·오류), samples_<suite>.jsonl(몬스터·전술 디버그 줄 표본), crowd 묶음은 Trace.File로 <suite>_<구간>.utrace, 표준 출력 TD_TACTICS_PROBE_RESULT 한 줄
상태: 현행(2026-10-18 M3-14 PIE 검증용)
"""
import json
import math
import os
import time
import traceback

import unreal

LEVEL_NAME = "LV-Cambat"
PROBE_ACTOR_PREFIX = "TDProbe_Tactics_"
PROBE_FOLDER = "TDGen/MonsterAI/ProbeTemporary"
WALL_MESH_PATH = "/Engine/BasicShapes/Cube"
WALL_HEIGHT_CM = 300.0
WALL_THICKNESS_CM = 60.0
WALL_LENGTH_CM = 2600.0
CORRIDOR_INNER_WIDTH_CM = 500.0
PLAYER_WALL_GAP_CM = 160.0
FLOOR_MAX_HALF_HEIGHT_CM = 300.0

OUTPUT_DIRECTORY = os.path.join(unreal.SystemLibrary.get_project_saved_directory(), "MonsterAI", "TacticsPie")
STATE_PATH = os.path.join(OUTPUT_DIRECTORY, "probe_state.json")
REQUEST_PATH = os.path.join(OUTPUT_DIRECTORY, "request.json")
LAYOUT_PATH = os.path.join(OUTPUT_DIRECTORY, "layout.json")
RESULT_MARKER = "TD_TACTICS_PROBE_RESULT"
TICK_HANDLE_ATTRIBUTE = "td_tactics_probe_tick_handle"

SAMPLE_EVERY_FRAMES = 3
CROWD_SAMPLE_EVERY_FRAMES = 30
HEALTH_REFILL_RATIO = 0.5
CAPTURE_WAIT_REAL_SECONDS = 60.0
REGISTER_TIMEOUT_REAL_SECONDS = 30.0
CAMERA_ARM_LENGTH_CM = 1800.0
CAMERA_PITCH_DEGREES = -88.0

SURROUND_SECONDS = 45.0
SURROUND_CAPTURE_AT_SECONDS = 14.0
PULL_STOP_SECONDS = 1.4
STAND_SECONDS = 40.0
STAND_CAPTURE_AT_SECONDS = 16.0
KITE_SECONDS = 60.0
KITE_RADIUS_CM = 1400.0
KITE_MOVE_SECONDS = 3.0
KITE_STOP_SECONDS = 1.5
KITE_LOOKAHEAD_DEGREES = 25.0
KITE_CAPTURE_AT_SECONDS = 20.0
WALL_SECONDS = 30.0
WALL_CAPTURE_AT_SECONDS = 14.0
FIGHT_SECONDS = 20.0
FIGHT_CAST_EVERY_SECONDS = 1.2
FIGHT_SPELL_SLOTS = 5

CROWD_DIRECTIONS_DEGREES = (0.0, 90.0, 180.0, 270.0)
CROWD_ROW_DISTANCES_CM = (1000.0, 1150.0, 1300.0, 1450.0, 1600.0)
CROWD_PER_ROW = 15
CROWD_SPECIES_BY_ROW = ("Goblin", "Hyena", "Goblin", "Hyena", "BookHeadCaster")
CROWD_ENGAGE_WALK_SECONDS = 8.0
CROWD_ENGAGE_WALK_RADIUS_CM = 500.0
CROWD_SETTLE_SECONDS = 4.0
CROWD_SEGMENT_SECONDS = 15.0
CROWD_SWITCH_SETTLE_SECONDS = 3.0

GROUP_CENTERS = {
    "HyenaPack_East": (1900.0, 0.0),
    "MixedPack_South": (1400.0, -2600.0),
    "GoblinBand_SouthWest": (-1700.0, -2700.0),
    "Casters_West": (-2300.0, -500.0),
    "GuardPost_NorthWest": (-1900.0, 2400.0),
    "BruteSquad_North": (0.0, 2300.0),
    "GolemLair_NorthEast": (2500.0, 2400.0),
}
PULL_APPROACH_CM = 900.0
WALK_ARRIVAL_CM = 80.0
WALK_STALL_SECONDS = 1.5
WALK_STALL_MIN_PROGRESS_CM = 40.0
WALK_HOP_CM = 250.0
WALK_TIMEOUT_SECONDS = 25.0
CORRIDOR_ENTRY_OFFSET_CM = 2200.0
WALL_PACK_COMMAND = "TDSpawnMonster Goblin 10 900"
CORRIDOR_PACK_COMMAND = "TDSpawnMonster Goblin 8 150"
CORRIDOR_PACK_ANCHOR_CM = (-900.0, -150.0)

PROBE_DATA_ATTRIBUTE = "td_tactics_probe_data"
KITE_FALLBACK_CHECK_SECONDS = 1.0
KITE_FALLBACK_MIN_MOVE_CM = 50.0
KITE_FALLBACK_SPEED_CM = 400.0

NOT_READY = object()
probe = getattr(unreal, PROBE_DATA_ATTRIBUTE, None)
if probe is None:
    probe = {}
    setattr(unreal, PROBE_DATA_ATTRIBUTE, probe)


def get_game_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def get_game_seconds(world):
    return unreal.GameplayStatics.get_time_seconds(world)


def write_json_atomically(path, payload):
    os.makedirs(OUTPUT_DIRECTORY, exist_ok=True)
    temporary_path = path + ".writing"
    with open(temporary_path, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=2, default=str)
    for _ in range(5):
        try:
            os.replace(temporary_path, path)
            return
        except OSError:
            time.sleep(0.05)


def write_state():
    write_json_atomically(STATE_PATH, probe["state"])


def mark_stage(stage, **fields):
    world = get_game_world()
    probe["state"]["stage"] = stage
    probe["state"]["events"].append({"stage": stage, "game_seconds": round(get_game_seconds(world), 2) if world else None, **fields})
    write_state()


def frames(count):
    return ("frames", count)


def seconds(duration):
    return ("seconds", duration)


def until(predicate, timeout_real_seconds):
    return ("until", predicate, timeout_real_seconds)


def arm_wait(world, specification):
    kind = specification[0]
    wait = {"kind": kind}
    if kind == "frames":
        wait["frames_left"] = specification[1]
    if kind == "seconds":
        wait["deadline_game"] = get_game_seconds(world) + specification[1]
    if kind == "until":
        wait["predicate"] = specification[1]
        wait["deadline_real"] = time.monotonic() + specification[2]
    probe["wait"] = wait


def evaluate_wait(world):
    wait = probe["wait"]
    if wait["kind"] == "frames":
        wait["frames_left"] -= 1
        return None if wait["frames_left"] <= 0 else NOT_READY
    if wait["kind"] == "seconds":
        return None if get_game_seconds(world) >= wait["deadline_game"] else NOT_READY
    if wait["predicate"]():
        return True
    if time.monotonic() >= wait["deadline_real"]:
        return False
    return NOT_READY


def unregister_tick():
    handle = getattr(unreal, TICK_HANDLE_ATTRIBUTE, None)
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
        setattr(unreal, TICK_HANDLE_ATTRIBUTE, None)


def finish(result, error=None):
    state = probe.get("state", {})
    if state.get("result") != "running":
        return
    state["result"] = result
    if error:
        state["errors"].append(error)
    unregister_tick()
    close_sample_file()
    probe["iterator"] = None
    probe["hooks"] = []
    probe["wait"] = None
    write_state()


def tick(delta_seconds):
    if probe.get("state", {}).get("result") != "running":
        return
    try:
        world = get_game_world()
        if world is None:
            finish("failed", "PIE 월드가 시나리오 도중 사라졌다")
            return
        for hook in list(probe["hooks"]):
            hook(world)
        outcome = evaluate_wait(world)
        if outcome is NOT_READY:
            return
        arm_wait(world, probe["iterator"].send(outcome))
    except StopIteration:
        finish("complete")
    except Exception:
        finish("failed", traceback.format_exc())


def get_player(world):
    return unreal.GameplayStatics.get_player_pawn(world, 0)


def get_controller(world):
    return unreal.GameplayStatics.get_player_controller(world, 0)


def get_think(world):
    return unreal.TDMonsterThinkSubsystem.get_monster_think_subsystem(world)


def run_console(world, command):
    unreal.SystemLibrary.execute_console_command(world, command, get_controller(world))
    probe["state"]["events"].append({"console": command, "game_seconds": round(get_game_seconds(world), 2)})


def teleport_player(world, x, y, yaw=0.0):
    pawn = get_player(world)
    location = pawn.get_actor_location()
    pawn.teleport_pawn(unreal.Vector(x, y, location.z), unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))


def refill_player_health(world):
    pawn = get_player(world)
    combat = pawn.get_component_by_class(unreal.TDCombatComponent) if pawn else None
    if combat is None:
        return
    stats = combat.get_stats()
    maximum = stats.get_max_health() if hasattr(stats, "get_max_health") else stats.get_editor_property("base_max_health")
    if maximum > 0.0 and combat.get_current_health() < maximum * HEALTH_REFILL_RATIO:
        combat.set_stats(stats, True)
        probe["refills"] = probe.get("refills", 0) + 1


def setup_topdown_camera(world):
    pawn = get_player(world)
    arm = pawn.get_component_by_class(unreal.SpringArmComponent) if pawn else None
    if arm is None:
        probe["state"]["errors"].append("플레이어 스프링암을 찾지 못해 카메라를 올리지 못했다")
        return
    arm.set_editor_property("target_arm_length", CAMERA_ARM_LENGTH_CM)
    arm.set_editor_property("do_collision_test", False)
    arm.set_editor_property("use_pawn_control_rotation", False)
    arm.set_relative_rotation(unreal.Rotator(roll=0.0, pitch=CAMERA_PITCH_DEGREES, yaw=0.0), False, True)
    for name in ("inherit_pitch", "inherit_yaw", "inherit_roll"):
        try:
            arm.set_editor_property(name, False)
        except Exception:
            pass


def open_sample_file(suite):
    close_sample_file()
    path = os.path.join(OUTPUT_DIRECTORY, f"samples_{suite}.jsonl")
    probe["sample_handle"] = open(path, "w", encoding="utf-8")
    probe["state"]["samples_path"] = path


def close_sample_file():
    handle = probe.pop("sample_handle", None)
    if handle is not None:
        handle.close()


def make_sampler(every_frames):
    counter = {"frame": 0}

    def sample(world):
        counter["frame"] += 1
        scenario = probe.get("scenario")
        if not scenario or counter["frame"] % every_frames != 0:
            return
        think = get_think(world)
        pawn = get_player(world)
        if think is None or pawn is None:
            return
        location = pawn.get_actor_location()
        combat = pawn.get_component_by_class(unreal.TDCombatComponent)
        entry = {
            "kind": "sample",
            "scenario": scenario,
            "t": round(get_game_seconds(world), 3),
            "real": round(time.monotonic() - probe["started_real"], 3),
            "player": {"x": round(location.x, 1), "y": round(location.y, 1), "z": round(location.z, 1), "hp": round(combat.get_current_health(), 1) if combat else None},
            "active": think.get_active_monster_count(),
            "tactics": list(think.get_tactics_debug_lines()),
            "monsters": list(think.get_monster_debug_lines()),
        }
        handle = probe.get("sample_handle")
        if handle is not None:
            handle.write(json.dumps(entry, ensure_ascii=False) + "\n")

    return sample


def capture(name):
    mark_stage("awaiting_capture", capture=name)
    flag_path = os.path.join(OUTPUT_DIRECTORY, f"{name}.flag")
    seen = yield until(lambda: os.path.exists(flag_path), CAPTURE_WAIT_REAL_SECONDS)
    probe["state"]["captures"][name] = bool(seen)
    mark_stage("captured", capture=name)


def run_timed(world, scenario_name, duration, capture_name=None, capture_at=None, step=None):
    probe["scenario"] = scenario_name
    mark_stage(scenario_name)
    started = get_game_seconds(world)
    captured = capture_name is None
    while True:
        world = get_game_world()
        elapsed = get_game_seconds(world) - started
        if elapsed >= duration:
            break
        if not captured and elapsed >= capture_at:
            captured = True
            yield from capture(capture_name)
            continue
        if step is not None:
            step(world, elapsed)
        yield frames(1)
    probe["scenario"] = None


def walk_to(scenario_name, waypoints):
    probe["scenario"] = scenario_name
    mark_stage(scenario_name, waypoints=[list(point) for point in waypoints])
    hops = 0
    for goal in waypoints:
        world = get_game_world()
        started = get_game_seconds(world)
        anchor = (started, None)
        while True:
            world = get_game_world()
            now = get_game_seconds(world)
            pawn = get_player(world)
            location = pawn.get_actor_location()
            remaining = math.hypot(goal[0] - location.x, goal[1] - location.y)
            if remaining <= WALK_ARRIVAL_CM or now - started > WALK_TIMEOUT_SECONDS:
                break
            if anchor[1] is None or anchor[1] - remaining >= WALK_STALL_MIN_PROGRESS_CM:
                anchor = (now, remaining)
            elif now - anchor[0] >= WALK_STALL_SECONDS:
                step = min(WALK_HOP_CM, remaining)
                teleport_player(world, location.x + (goal[0] - location.x) / remaining * step, location.y + (goal[1] - location.y) / remaining * step, math.degrees(math.atan2(goal[1] - location.y, goal[0] - location.x)))
                hops += 1
                anchor = (now, None)
                yield frames(1)
                continue
            pawn.add_movement_input(unreal.Vector((goal[0] - location.x) / remaining, (goal[1] - location.y) / remaining, 0.0), 1.0, False)
            yield frames(1)
    probe["state"].setdefault("walk_hops", {})[scenario_name] = hops
    probe["scenario"] = None


def wait_for_registered(minimum):
    def is_ready():
        world = get_game_world()
        think = get_think(world) if world else None
        return think is not None and think.get_active_monster_count() >= minimum
    return until(is_ready, REGISTER_TIMEOUT_REAL_SECONDS)


def kite_step_factory(center):
    state = {"mode": "input", "origin": None, "last_elapsed": 0.0}

    def step(world, elapsed):
        delta = max(0.0, elapsed - state["last_elapsed"])
        state["last_elapsed"] = elapsed
        period = KITE_MOVE_SECONDS + KITE_STOP_SECONDS
        if elapsed % period >= KITE_MOVE_SECONDS:
            return
        pawn = get_player(world)
        location = pawn.get_actor_location()
        if state["origin"] is None:
            state["origin"] = (location.x, location.y)
        if state["mode"] == "input" and elapsed >= KITE_FALLBACK_CHECK_SECONDS and math.hypot(location.x - state["origin"][0], location.y - state["origin"][1]) < KITE_FALLBACK_MIN_MOVE_CM:
            state["mode"] = "teleport"
            probe["state"]["kite_mode"] = "teleport"
            probe["state"]["events"].append({"kite_fallback": "add_movement_input moved the player less than 50cm in 1s", "game_seconds": round(get_game_seconds(world), 2)})
        angle = math.atan2(location.y - center[1], location.x - center[0])
        if state["mode"] == "teleport":
            angle += KITE_FALLBACK_SPEED_CM * delta / KITE_RADIUS_CM
            teleport_player(world, center[0] + KITE_RADIUS_CM * math.cos(angle), center[1] + KITE_RADIUS_CM * math.sin(angle), math.degrees(angle) + 90.0)
            return
        probe["state"]["kite_mode"] = "input"
        angle += math.radians(KITE_LOOKAHEAD_DEGREES)
        goal = (center[0] + KITE_RADIUS_CM * math.cos(angle), center[1] + KITE_RADIUS_CM * math.sin(angle))
        direction_x = goal[0] - location.x
        direction_y = goal[1] - location.y
        length = math.hypot(direction_x, direction_y)
        if length < 1.0:
            return
        pawn.add_movement_input(unreal.Vector(direction_x / length, direction_y / length, 0.0), 1.0, False)
    return step


def fight_step_factory():
    state = {"next": 0.0}

    def step(world, elapsed):
        if elapsed < state["next"]:
            return
        state["next"] = elapsed + FIGHT_CAST_EVERY_SECONDS
        pawn = get_player(world)
        nearest = None
        for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDMonsterCharacter):
            combat = actor.get_component_by_class(unreal.TDCombatComponent)
            if combat is None or not combat.is_alive():
                continue
            distance = actor.get_distance_to(pawn)
            if nearest is None or distance < nearest[0]:
                nearest = (distance, actor)
        if nearest is not None:
            slot = probe.get("fight_casts", 0) % FIGHT_SPELL_SLOTS
            probe["fight_casts"] = probe.get("fight_casts", 0) + 1
            pawn.cast_damage_spell(slot, nearest[1].get_actor_location())
    return step


def read_layout():
    if not os.path.exists(LAYOUT_PATH):
        return None
    with open(LAYOUT_PATH, encoding="utf-8") as handle:
        return json.load(handle)


def behavior_scenario():
    world = get_game_world()
    layout = read_layout()
    registered = yield wait_for_registered(20)
    probe["state"]["registered_at_start"] = get_think(get_game_world()).get_active_monster_count()
    if not registered:
        probe["state"]["errors"].append("배치 몬스터 20마리 이상이 30초 안에 등록되지 않았다")
    world = get_game_world()
    run_console(world, "TD.MonsterAI.Tactics 1")
    run_console(world, "TD.MonsterAI.Tactics.Debug 1")
    run_console(world, "TDSetCasterLevel 10")
    setup_topdown_camera(world)
    probe["hooks"].append(refill_player_health)
    teleport_player(world, 0.0, 0.0, 0.0)
    yield frames(2)
    run_console(world, "TDSpawnMonster Goblin 10 900")
    yield frames(2)
    yield from run_timed(get_game_world(), "surround10", SURROUND_SECONDS, "01_surround10", SURROUND_CAPTURE_AT_SECONDS)

    mark_stage("pull25")
    for name, (x, y) in GROUP_CENTERS.items():
        length = math.hypot(x, y)
        approach = (x - x / length * PULL_APPROACH_CM, y - y / length * PULL_APPROACH_CM)
        teleport_player(get_game_world(), approach[0], approach[1], math.degrees(math.atan2(y, x)))
        yield seconds(PULL_STOP_SECONDS)
    teleport_player(get_game_world(), 0.0, 0.0, 0.0)
    yield from run_timed(get_game_world(), "stand35", STAND_SECONDS, "02_stand35", STAND_CAPTURE_AT_SECONDS)

    teleport_player(get_game_world(), KITE_RADIUS_CM, 0.0, 90.0)
    yield from run_timed(get_game_world(), "kite35", KITE_SECONDS, "03_kite35", KITE_CAPTURE_AT_SECONDS, kite_step_factory((0.0, 0.0)))

    if layout is None:
        probe["state"]["errors"].append("layout.json이 없어 벽·복도 장면을 건너뛰었다(prepare 미실행)")
    else:
        wall_stand = layout["wall_stand"]
        yield from walk_to("walk_to_wall", [(wall_stand[0] + 800.0, wall_stand[1]), tuple(wall_stand)])
        teleport_player(get_game_world(), wall_stand[0], wall_stand[1], 0.0)
        yield frames(1)
        run_console(get_game_world(), WALL_PACK_COMMAND)
        yield frames(2)
        yield from run_timed(get_game_world(), "wall", WALL_SECONDS, "04_wall", WALL_CAPTURE_AT_SECONDS)
        corridor_stand = layout["corridor_stand"]
        yield from walk_to("walk_to_corridor", [(corridor_stand[0] - CORRIDOR_ENTRY_OFFSET_CM, corridor_stand[1]), tuple(corridor_stand)])
        teleport_player(get_game_world(), corridor_stand[0] + CORRIDOR_PACK_ANCHOR_CM[0], corridor_stand[1] + CORRIDOR_PACK_ANCHOR_CM[1], 90.0)
        yield frames(1)
        run_console(get_game_world(), CORRIDOR_PACK_COMMAND)
        yield frames(2)
        teleport_player(get_game_world(), corridor_stand[0], corridor_stand[1], 0.0)
        yield from run_timed(get_game_world(), "corridor", WALL_SECONDS, "05_corridor", WALL_CAPTURE_AT_SECONDS)

    yield from walk_to("walk_to_center", [(-CORRIDOR_ENTRY_OFFSET_CM, layout["corridor_stand"][1] if layout else 0.0), (0.0, 0.0)])
    yield from run_timed(get_game_world(), "fight", FIGHT_SECONDS, step=fight_step_factory())
    probe["state"]["refills"] = probe.get("refills", 0)
    probe["state"]["fight_casts"] = probe.get("fight_casts", 0)


def trace_path(suite, segment):
    return os.path.join(OUTPUT_DIRECTORY, f"{suite}_{segment}.utrace").replace("\\", "/")


def run_trace_segment(suite, segment, duration):
    world = get_game_world()
    path = trace_path(suite, segment)
    if os.path.exists(path):
        os.remove(path)
    run_console(world, f"Trace.File {path} cpu,frame")
    started_real = time.monotonic()
    yield from run_timed(world, f"crowd_{segment}", duration)
    run_console(get_game_world(), "Trace.Stop")
    probe["state"]["traces"][segment] = {"path": path, "real_seconds": round(time.monotonic() - started_real, 2)}


def crowd_scenario():
    registered = yield wait_for_registered(20)
    world = get_game_world()
    placed = get_think(world).get_active_monster_count()
    probe["state"]["registered_at_start"] = placed
    if not registered:
        probe["state"]["errors"].append("배치 몬스터 20마리 이상이 30초 안에 등록되지 않았다")
    run_console(world, "TD.MonsterAI.Tactics 1")
    run_console(world, "TD.MonsterAI.Tactics.Debug 0")
    run_console(world, "TD.MonsterAI.DebugDraw 0")
    run_console(world, "Trace.Stop")
    setup_topdown_camera(world)
    probe["hooks"].append(refill_player_health)
    row = 0
    for direction in CROWD_DIRECTIONS_DEGREES:
        for distance in CROWD_ROW_DISTANCES_CM:
            species = CROWD_SPECIES_BY_ROW[row % len(CROWD_SPECIES_BY_ROW)]
            teleport_player(get_game_world(), 0.0, 0.0, direction)
            yield frames(1)
            run_console(get_game_world(), f"TDSpawnMonster {species} {CROWD_PER_ROW} {distance:.0f}")
            yield frames(2)
            row += 1
    expected = placed + row * CROWD_PER_ROW
    spawned = yield wait_for_registered(expected - 5)
    probe["state"]["registered_after_spawn"] = get_think(get_game_world()).get_active_monster_count()
    if not spawned:
        probe["state"]["errors"].append(f"300마리 스폰 뒤 등록 수가 {expected - 5} 미만이다")
    probe["scenario"] = "crowd_engage"
    started = get_game_seconds(get_game_world())
    while get_game_seconds(get_game_world()) - started < CROWD_ENGAGE_WALK_SECONDS:
        elapsed = get_game_seconds(get_game_world()) - started
        angle = elapsed / CROWD_ENGAGE_WALK_SECONDS * 2.0 * math.pi
        teleport_player(get_game_world(), CROWD_ENGAGE_WALK_RADIUS_CM * math.cos(angle), CROWD_ENGAGE_WALK_RADIUS_CM * math.sin(angle), math.degrees(angle))
        yield frames(1)
    teleport_player(get_game_world(), 0.0, 0.0, 0.0)
    yield seconds(CROWD_SETTLE_SECONDS)
    yield from run_trace_segment("crowd", "tactics_on", CROWD_SEGMENT_SECONDS)
    run_console(get_game_world(), "TD.MonsterAI.Tactics 0")
    yield seconds(CROWD_SWITCH_SETTLE_SECONDS)
    yield from run_trace_segment("crowd", "tactics_off", CROWD_SEGMENT_SECONDS)
    run_console(get_game_world(), "TD.MonsterAI.Tactics 1")
    probe["state"]["refills"] = probe.get("refills", 0)


def start_probe(suite):
    unregister_tick()
    world = get_game_world()
    map_path = world.get_path_name() if world else ""
    probe.clear()
    probe.update({"hooks": [], "scenario": None, "started_real": time.monotonic()})
    probe["state"] = {"tool": "editor_pie_tactics_probe", "suite": suite, "map": map_path, "result": "running", "stage": "starting",
                      "started": time.strftime("%Y-%m-%d %H:%M:%S"), "events": [], "errors": [], "captures": {}, "traces": {}}
    if world is None or LEVEL_NAME not in map_path:
        finish("failed", f"{LEVEL_NAME} PIE가 켜져 있어야 한다(현재: {map_path or '없음'})")
        return {"started": False, "map": map_path}
    for name in os.listdir(OUTPUT_DIRECTORY) if os.path.isdir(OUTPUT_DIRECTORY) else []:
        if name.endswith(".flag"):
            os.remove(os.path.join(OUTPUT_DIRECTORY, name))
    open_sample_file(suite)
    probe["hooks"].append(make_sampler(CROWD_SAMPLE_EVERY_FRAMES if suite == "crowd" else SAMPLE_EVERY_FRAMES))
    probe["iterator"] = crowd_scenario() if suite == "crowd" else behavior_scenario()
    probe["wait"] = {"kind": "frames", "frames_left": 1}
    write_state()
    setattr(unreal, TICK_HANDLE_ATTRIBUTE, unreal.register_slate_post_tick_callback(tick))
    return {"started": True, "map": map_path, "suite": suite}


def find_floor_bounds(world):
    best = None
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.StaticMeshActor):
        origin, extent = actor.get_actor_bounds(False)
        area = extent.x * extent.y
        if extent.z <= FLOOR_MAX_HALF_HEIGHT_CM and (best is None or area > best[0]):
            best = (area, actor.get_actor_label(), origin, extent)
    if best is None:
        return None
    _, label, origin, extent = best
    return {"label": label, "min_x": origin.x - extent.x, "max_x": origin.x + extent.x, "min_y": origin.y - extent.y, "max_y": origin.y + extent.y, "top_z": origin.z + extent.z}


def inspect_level():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor.get_editor_world()
    floor = find_floor_bounds(world)
    starts = [actor.get_actor_location() for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PlayerStart)]
    monsters = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDMonsterCharacter)
    return {"world": world.get_path_name(), "floor": floor, "player_starts": [[round(v.x), round(v.y), round(v.z)] for v in starts], "placed_monsters": len(monsters),
            "probe_actors": [actor.get_actor_label() for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if actor.get_actor_label().startswith(PROBE_ACTOR_PREFIX)]}


def remove_probe_actors(actor_subsystem):
    removed = []
    for actor in list(actor_subsystem.get_all_level_actors()):
        label = actor.get_actor_label()
        if label.startswith(PROBE_ACTOR_PREFIX) and actor_subsystem.destroy_actor(actor):
            removed.append(label)
    return removed


def spawn_wall(actor_subsystem, cube, label, center, size, top_z):
    wall = actor_subsystem.spawn_actor_from_object(cube, unreal.Vector(center[0], center[1], top_z + WALL_HEIGHT_CM * 0.5), unreal.Rotator())
    wall.set_actor_label(PROBE_ACTOR_PREFIX + label)
    wall.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, WALL_HEIGHT_CM / 100.0))
    wall.set_folder_path(PROBE_FOLDER)
    return wall.get_actor_label()


def prepare_walls(request):
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if editor.get_game_world() is not None:
        return {"error": "PIE가 켜져 있다. prepare는 PIE 시작 전에 실행해야 한다"}
    world = editor.get_editor_world()
    if LEVEL_NAME not in world.get_path_name():
        return {"error": f"에디터 월드가 {LEVEL_NAME}이 아니다: {world.get_path_name()}"}
    removed = remove_probe_actors(actor_subsystem)
    floor = find_floor_bounds(world)
    if floor is None:
        return {"error": "바닥 메시를 찾지 못했다"}
    cube = unreal.EditorAssetLibrary.load_asset(WALL_MESH_PATH)
    wall_x = request.get("wall_x")
    corridor_center = request.get("corridor_center")
    if wall_x is None or corridor_center is None:
        return {"error": "wall_x·corridor_center 요청 필드가 필요하다", "floor": floor}
    labels = [
        spawn_wall(actor_subsystem, cube, "Wall", (wall_x - WALL_THICKNESS_CM * 0.5, 0.0), (WALL_THICKNESS_CM, WALL_LENGTH_CM), floor["top_z"]),
        spawn_wall(actor_subsystem, cube, "CorridorNorth", (corridor_center[0], corridor_center[1] + (CORRIDOR_INNER_WIDTH_CM + WALL_THICKNESS_CM) * 0.5), (WALL_LENGTH_CM, WALL_THICKNESS_CM), floor["top_z"]),
        spawn_wall(actor_subsystem, cube, "CorridorSouth", (corridor_center[0], corridor_center[1] - (CORRIDOR_INNER_WIDTH_CM + WALL_THICKNESS_CM) * 0.5), (WALL_LENGTH_CM, WALL_THICKNESS_CM), floor["top_z"]),
    ]
    layout = {"wall_x": wall_x, "wall_stand": [wall_x + PLAYER_WALL_GAP_CM, 0.0], "corridor_center": corridor_center, "corridor_stand": list(corridor_center),
              "corridor_inner_width_cm": CORRIDOR_INNER_WIDTH_CM, "wall_length_cm": WALL_LENGTH_CM, "floor": floor}
    write_json_atomically(LAYOUT_PATH, layout)
    return {"walls": labels, "removed_stale": removed, "layout": layout}


def cleanup_walls():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if editor.get_game_world() is not None:
        return {"error": "PIE가 켜져 있어 정리하지 않았다"}
    removed = remove_probe_actors(unreal.get_editor_subsystem(unreal.EditorActorSubsystem))
    if os.path.exists(LAYOUT_PATH):
        os.remove(LAYOUT_PATH)
    return {"removed": removed}


def report_status():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    game_world = editor.get_game_world()
    return {"pie_running": game_world is not None, "pie_world": game_world.get_path_name() if game_world else None,
            "editor_world": editor.get_editor_world().get_path_name() if game_world is None else None}


def read_request():
    if not os.path.exists(REQUEST_PATH):
        return {"action": "status"}
    with open(REQUEST_PATH, encoding="utf-8") as handle:
        request = json.load(handle)
    os.remove(REQUEST_PATH)
    return request


def main():
    os.makedirs(OUTPUT_DIRECTORY, exist_ok=True)
    request = read_request()
    action = request.get("action", "status")
    handlers = {
        "inspect": inspect_level,
        "prepare": lambda: prepare_walls(request),
        "start": lambda: start_probe(request.get("suite", "behavior")),
        "cleanup": cleanup_walls,
        "status": report_status,
    }
    handler = handlers.get(action)
    result = handler() if handler else {"error": f"알 수 없는 action: {action}"}
    print(RESULT_MARKER, json.dumps(result, ensure_ascii=False, default=str))


main()
