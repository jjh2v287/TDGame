"""실행 환경: TDGame 언리얼 에디터 안(PythonScriptPlugin 원격 실행). 보스 아레나 PIE 중 자동 전투원이 슬레이트 틱 콜백으로 프레임마다 보스를 직접 싸워 잡는다(보스 체력을 깎지 않는다). 에디터 앞창 유지(L-editor-06), 월드·액터를 전역에 남기지 않는다(L-editor-30)
실행 명령: python Tools/run_in_editor.py Tools/MonsterAI/editor_pie_mid_boss_fight.py (요청 파일 Saved/MonsterAI/MidBossFight/request.json {"action": start|status|cleanup, "boss": CentaurHunter|GriffonAlpha, "mode": sword|sword_spells, "cap_seconds": 300, "origin": {"x","y","z","yaw"}}; 네 판을 한 번에 돌리려면 python Tools/MonsterAI/pie_fight_mid_bosses.py)
출력: Saved/MonsterAI/MidBossFight/state.json(진행·판 기록·오류), samples_<보스>_<모드>.jsonl(0.5초 표본), shots/<보스>_<모드>_<이름>.png, 표준 출력 TD_MID_BOSS_FIGHT 한 줄. 시간 기준: 교전 이후 초는 보스가 교전(engaged)한 순간부터, 나머지 사건 시각은 보스 등록 순간부터 게임 초
상태: 현행(2026-10-18 M3-34 후속 PIE 실전 측정)
"""
import json
import math
import os
import re
import shutil
import time
import traceback

import unreal

PROJECT_SAVED_DIRECTORY = unreal.SystemLibrary.get_project_saved_directory()
OUTPUT_DIRECTORY = os.path.join(PROJECT_SAVED_DIRECTORY, "MonsterAI", "MidBossFight")
SHOT_OUTPUT_DIRECTORY = os.path.join(OUTPUT_DIRECTORY, "shots")
SHOT_SOURCE_DIRECTORY = os.path.join(PROJECT_SAVED_DIRECTORY, "Screenshots", "WindowsEditor")
STATE_PATH = os.path.join(OUTPUT_DIRECTORY, "state.json")
REQUEST_PATH = os.path.join(OUTPUT_DIRECTORY, "request.json")
RESULT_MARKER = "TD_MID_BOSS_FIGHT"
TICK_HANDLE_ATTRIBUTE = "td_mid_boss_fight_tick_handle"
DATA_ATTRIBUTE = "td_mid_boss_fight_data"

BOSS_DEFINITION_IDS = {"CentaurHunter": "Centaur_Hunter", "GriffonAlpha": "Griffon_Alpha"}
FIGHT_MODES = ("sword", "sword_spells")
MINION_DEFINITION_ID = "Barghest"
DEFAULT_CAP_SECONDS = 300.0
SPAWN_COMMAND = "TDSpawnMonster {boss} 1 900"

PLAYER_TEST_MAX_HEALTH = 1000.0
PLAYER_REFERENCE_MAX_HEALTH = 100.0
MINION_PRIORITY_RADIUS_CM = 600.0
STOP_DISTANCE_CM = 170.0
ATTACK_RANGE_CM = 230.0
SPELL_INTERVAL_SECONDS = 1.0
SPELL_SLOT_COUNT = 10
DODGE_MARGIN_CM = 40.0
DODGE_CONE_MARGIN_DEGREES = 10.0
DODGE_RETRY_SECONDS = 1.0
DODGE_FAILED_RETRY_SECONDS = 0.1
DODGE_HOLD_MAX_SECONDS = 1.5
DODGE_FALLBACK_RADIUS_CM = 300.0
LONG_STAGGER_SECONDS = 1.0
FLIGHT_SHOT_DELAY_SECONDS = 0.8
TIMELINE_INTERVAL_SECONDS = 1.0
SAMPLE_INTERVAL_SECONDS = 0.5
SPAWN_WAIT_REAL_SECONDS = 25.0
ENGAGE_WAIT_GAME_SECONDS = 90.0
REAL_TIMEOUT_EXTRA_SECONDS = 120.0
STATE_WRITE_INTERVAL_REAL_SECONDS = 1.0
SHOT_WAIT_REAL_SECONDS = 12.0
SHOT_SETTLE_REAL_SECONDS = 0.6
SHOT_DRAIN_REAL_SECONDS = 40.0

LINE_FIELD = re.compile(r"(\w+)=(\S+)")
POSITION_FIELD = re.compile(r"\((-?[\d.]+),(-?[\d.]+)\)")

fight_data = getattr(unreal, DATA_ATTRIBUTE, None)
if fight_data is None:
    fight_data = {}
    setattr(unreal, DATA_ATTRIBUTE, fight_data)


def get_game_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def get_game_seconds(world):
    return unreal.GameplayStatics.get_time_seconds(world)


def write_json_atomically(path, payload):
    os.makedirs(OUTPUT_DIRECTORY, exist_ok=True)
    temporary_path = path + ".writing"
    with open(temporary_path, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=1, default=str)
    for _ in range(5):
        try:
            os.replace(temporary_path, path)
            return
        except OSError:
            time.sleep(0.05)


def write_state():
    state = fight_data.get("state")
    if state is None:
        return
    fight_data["last_state_write_real"] = time.monotonic()
    write_json_atomically(STATE_PATH, state)


def unregister_tick():
    handle = getattr(unreal, TICK_HANDLE_ATTRIBUTE, None)
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
        setattr(unreal, TICK_HANDLE_ATTRIBUTE, None)


def close_sample_file():
    handle = fight_data.pop("sample_handle", None)
    if handle is not None:
        handle.close()


def stop_scenario(result, error=None):
    state = fight_data.get("state")
    run = fight_data.get("run")
    if state is not None and state.get("result") == "running":
        state["result"] = result
        if error:
            state["errors"].append(error)
    if run is not None:
        run["phase"] = "done"
    unregister_tick()
    close_sample_file()
    write_state()


def get_player_pawn(world):
    return unreal.GameplayStatics.get_player_pawn(world, 0)


def get_player_controller(world):
    return unreal.GameplayStatics.get_player_controller(world, 0)


def run_console(world, command):
    unreal.SystemLibrary.execute_console_command(world, command, get_player_controller(world))


def parse_monster_line(line):
    fields = {}
    for key, value in LINE_FIELD.findall(line):
        fields.setdefault(key, value)
    position = POSITION_FIELD.match(fields.get("pos", ""))
    try:
        return {"actor": fields.get("actor", "None"), "definition": fields.get("def", ""), "state": fields.get("state", ""),
                "action": fields.get("action", ""), "engaged": fields.get("engaged") == "1", "phase": int(fields.get("phase", "0")),
                "health": float(fields.get("hp", "0")), "x": float(position.group(1)) if position else 0.0, "y": float(position.group(2)) if position else 0.0}
    except ValueError:
        return None


def read_monster_lines(world):
    think = unreal.TDMonsterThinkSubsystem.get_monster_think_subsystem(world)
    if think is None:
        return []
    parsed = [parse_monster_line(line) for line in think.get_monster_debug_lines()]
    return [line for line in parsed if line is not None and line["actor"] != "None"]


def find_monster_actors(world, names):
    found = {}
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDMonsterCharacter):
        name = actor.get_name()
        if name in names:
            found[name] = actor
    return found


def is_combat_alive(actor):
    combat = actor.get_component_by_class(unreal.TDCombatComponent) if actor else None
    return combat is not None and combat.is_alive()


def clean_world(world):
    destroyed = {"monsters": 0, "entities": 0}
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDMonsterCharacter):
        actor.destroy_actor()
        destroyed["monsters"] += 1
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDDamageEntity):
        actor.destroy_actor()
        destroyed["entities"] += 1
    return destroyed


def prepare_player(world, origin):
    pawn = get_player_pawn(world)
    combat = pawn.get_component_by_class(unreal.TDCombatComponent) if pawn else None
    if combat is None:
        return None
    stats = combat.get_stats()
    stats.set_editor_property("base_max_health", PLAYER_TEST_MAX_HEALTH)
    combat.set_stats(stats, True)
    if origin:
        pawn.teleport_pawn(unreal.Vector(origin["x"], origin["y"], origin["z"]), unreal.Rotator(roll=0.0, pitch=0.0, yaw=origin.get("yaw", 0.0)))
    return {"health": combat.get_current_health(), "max_health": combat.get_max_health()}


def new_record(boss, mode, cap_seconds):
    return {"boss": boss, "mode": mode, "cap_seconds": cap_seconds, "ended_reason": None, "boss_defeated": False,
            "kill_seconds": None, "phase1_seconds": None, "engage_seconds": None, "boss_max_health": None,
            "player_damage_taken": 0.0, "player_damage_in_H": 0.0, "player_min_health": None, "player_max_health": None,
            "player_attacks": 0, "spell_attempts": 0, "spell_casts": 0, "spell_casts_by_slot": {},
            "rolls": 0, "roll_attempts": 0, "roll_triggers_by_definition": {}, "dodge_shape_counts": {}, "dodge_shape_unreadable": 0,
            "boss_hp_timeline": [], "phase_transitions": [], "stagger_intervals": [], "short_stagger_count": 0, "flight_intervals": [],
            "boss_action_counts": {}, "minions_spawned": 0, "minions_killed": 0,
            "target_seconds": {"boss": 0.0, "minion": 0.0}, "in_attack_range_seconds": 0.0,
            "game_seconds": None, "real_seconds": None, "game_per_real": None, "shots": {}}


def new_run(boss, mode, cap_seconds):
    record = new_record(boss, mode, cap_seconds)
    return {"phase": "spawning", "boss": boss, "mode": mode, "cap_seconds": cap_seconds, "boss_definition_id": BOSS_DEFINITION_IDS[boss],
            "boss_name": None, "record": record, "real_started": time.monotonic(), "started_game": None, "engaged_game": None, "last_game": None,
            "player_previous_health": None, "last_phase": None, "last_action": None, "last_flying": False, "flight": None,
            "stagger": None, "last_non_stagger_action": None, "next_dodge_game": 0.0, "danger_since": None, "next_spell_game": 0.0, "spell_index": 0,
            "next_timeline_game": 0.0, "timeline_index": 0, "next_sample_game": 0.0, "minions_alive": set(), "minions_dead": set(),
            "requested_shots": set(), "shape_cache": {}, "shots_deadline_real": None}


def fight_seconds(run, now):
    return now - run["started_game"]


def engaged_seconds(run, now):
    return now - run["engaged_game"] if run["engaged_game"] is not None else None


def request_shot(run, name):
    if name in run["requested_shots"]:
        return
    run["requested_shots"].add(name)
    fight_data["shots"]["pending"].append(name)


def list_screenshot_names():
    if not os.path.isdir(SHOT_SOURCE_DIRECTORY):
        return set()
    return {name for name in os.listdir(SHOT_SOURCE_DIRECTORY) if name.startswith("ScreenShot") and name.endswith(".png")}


def start_next_shot(world, run):
    shots = fight_data["shots"]
    if not shots["pending"]:
        return
    name = shots["pending"].pop(0)
    shots["active"] = {"name": name, "before": list_screenshot_names(), "deadline_real": time.monotonic() + SHOT_WAIT_REAL_SECONDS,
                       "candidate": None, "candidate_size": -1, "candidate_since_real": 0.0}
    run_console(world, "Shot")


def finish_active_shot(run, path):
    shots = fight_data["shots"]
    active = shots["active"]
    shots["active"] = None
    run["record"]["shots"][active["name"]] = path


def poll_active_shot(run):
    active = fight_data["shots"]["active"]
    now_real = time.monotonic()
    fresh = sorted(list_screenshot_names() - active["before"])
    if fresh:
        source = os.path.join(SHOT_SOURCE_DIRECTORY, fresh[0])
        size = os.path.getsize(source)
        if active["candidate"] != fresh[0] or size != active["candidate_size"]:
            active["candidate"] = fresh[0]
            active["candidate_size"] = size
            active["candidate_since_real"] = now_real
        elif size > 0 and now_real - active["candidate_since_real"] >= SHOT_SETTLE_REAL_SECONDS:
            os.makedirs(SHOT_OUTPUT_DIRECTORY, exist_ok=True)
            target = os.path.join(SHOT_OUTPUT_DIRECTORY, f"{run['boss']}_{run['mode']}_{active['name']}.png")
            shutil.move(source, target)
            finish_active_shot(run, target)
            return
    if now_real >= active["deadline_real"]:
        finish_active_shot(run, None)


def process_shot_queue(world, run):
    if fight_data["shots"]["active"] is not None:
        poll_active_shot(run)
        return
    start_next_shot(world, run)


def is_shot_queue_empty():
    shots = fight_data["shots"]
    return shots["active"] is None and not shots["pending"]


def classify_shape(text):
    upper = text.upper()
    for kind in ("CONE", "LINE", "RING"):
        if kind in upper:
            return kind
    return "CIRCLE"


def read_entity_definition(entity):
    try:
        definition = entity.get_editor_property("definition")
    except Exception:
        definition = None
    if definition is not None:
        return definition
    try:
        return entity.get_definition()
    except Exception:
        return None


def read_shape(entity, run, record):
    definition = read_entity_definition(entity)
    key = definition.get_path_name() if definition is not None else "unreadable"
    cache = run["shape_cache"]
    if key in cache:
        return cache[key]
    shape = None
    if definition is not None:
        try:
            area = definition.get_editor_property("area_shape")
            mode = definition.get_editor_property("mode")
            shape = {"name": definition.get_name(), "kind": classify_shape(getattr(area, "name", str(area))),
                     "is_projectile": "PROJECTILE" in getattr(mode, "name", str(mode)).upper(),
                     "radius": float(definition.get_editor_property("radius")),
                     "cone_half_angle": float(definition.get_editor_property("cone_half_angle_degrees")),
                     "line_half_width": float(definition.get_editor_property("line_half_width")),
                     "inner_radius": float(definition.get_editor_property("inner_radius"))}
        except Exception:
            shape = None
    if shape is None:
        shape = {"name": definition.get_name() if definition is not None else "unreadable", "kind": "CIRCLE", "is_projectile": False,
                 "radius": DODGE_FALLBACK_RADIUS_CM, "cone_half_angle": 0.0, "line_half_width": 0.0, "inner_radius": 0.0}
        record["dodge_shape_unreadable"] += 1
        fight_data["state"]["notes"].append(f"형상 속성을 못 읽어 원 {DODGE_FALLBACK_RADIUS_CM:g}cm로 취급: {shape['name']}")
    record["dodge_shape_counts"][shape["name"]] = shape["kind"] + ("/projectile-ignored" if shape["is_projectile"] else "")
    cache[key] = shape
    return shape


def normalize_planar(x, y, fallback):
    length = math.hypot(x, y)
    if length < 1.0:
        return fallback
    return (x / length, y / length)


def find_escape_direction(shape, relative_x, relative_y, forward_x, forward_y, away_fallback):
    distance = math.hypot(relative_x, relative_y)
    kind = shape["kind"]
    outward = normalize_planar(relative_x, relative_y, away_fallback)
    if kind == "CIRCLE":
        return outward if distance <= shape["radius"] + DODGE_MARGIN_CM else None
    side_value = forward_x * relative_y - forward_y * relative_x
    side = -1.0 if side_value < 0.0 else 1.0
    sideways = (-forward_y * side, forward_x * side)
    if kind == "CONE":
        if distance > shape["radius"] + DODGE_MARGIN_CM:
            return None
        if distance >= 1.0:
            angle = abs(math.degrees(math.atan2(side_value, forward_x * relative_x + forward_y * relative_y)))
            if angle > shape["cone_half_angle"] + DODGE_CONE_MARGIN_DEGREES:
                return None
        return sideways
    if kind == "LINE":
        projection = forward_x * relative_x + forward_y * relative_y
        if projection < 0.0 or projection > shape["radius"] or abs(side_value) > shape["line_half_width"] + DODGE_MARGIN_CM:
            return None
        return sideways
    if distance < shape["inner_radius"] - DODGE_MARGIN_CM or distance > shape["radius"] + DODGE_MARGIN_CM:
        return None
    if distance >= (shape["inner_radius"] + shape["radius"]) * 0.5:
        return outward
    return (-outward[0], -outward[1])


def find_danger(world, ploc, boss_actor, run, record):
    boss_name = run["boss_name"]
    boss_location = boss_actor.get_actor_location()
    away_fallback = normalize_planar(ploc.x - boss_location.x, ploc.y - boss_location.y, (1.0, 0.0))
    for entity in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDDamageEntity):
        owner = entity.get_owner()
        if owner is None or owner.get_name() != boss_name:
            continue
        shape = read_shape(entity, run, record)
        if shape["is_projectile"]:
            continue
        origin = entity.get_actor_location()
        forward = entity.get_actor_forward_vector()
        forward_x, forward_y = normalize_planar(forward.x, forward.y, (1.0, 0.0))
        direction = find_escape_direction(shape, ploc.x - origin.x, ploc.y - origin.y, forward_x, forward_y, away_fallback)
        if direction is not None:
            return {"direction": direction, "definition": shape["name"]}
    return None


def hold_for_danger(world, pawn, ploc, boss_actor, now, run, record):
    danger = find_danger(world, ploc, boss_actor, run, record)
    if danger is None:
        run["danger_since"] = None
        return False
    if run["danger_since"] is None:
        run["danger_since"] = now
    if now >= run["next_dodge_game"]:
        run["next_dodge_game"] = now + DODGE_FAILED_RETRY_SECONDS
        record["roll_attempts"] += 1
        if pawn.try_start_roll(unreal.Vector(danger["direction"][0], danger["direction"][1], 0.0)):
            run["next_dodge_game"] = now + DODGE_RETRY_SECONDS
            record["rolls"] += 1
            triggers = record["roll_triggers_by_definition"]
            triggers[danger["definition"]] = triggers.get(danger["definition"], 0) + 1
            return True
    return now - run["danger_since"] <= DODGE_HOLD_MAX_SECONDS


def cast_spell(pawn, target_location, now, run, record):
    if now < run["next_spell_game"]:
        return
    run["next_spell_game"] = now + SPELL_INTERVAL_SECONDS
    slot = run["spell_index"] % SPELL_SLOT_COUNT
    run["spell_index"] += 1
    record["spell_attempts"] += 1
    try:
        cast = pawn.cast_damage_spell(slot, target_location)
    except Exception:
        return
    if not cast:
        return
    record["spell_casts"] += 1
    by_slot = record["spell_casts_by_slot"]
    by_slot[str(slot)] = by_slot.get(str(slot), 0) + 1


def act_on_target(world, pawn, ploc, target, kind, delta, now, run, record):
    target_location = target.get_actor_location()
    delta_x = target_location.x - ploc.x
    delta_y = target_location.y - ploc.y
    distance = math.hypot(delta_x, delta_y)
    record["target_seconds"][kind] += delta
    if distance > STOP_DISTANCE_CM:
        pawn.add_movement_input(unreal.Vector(delta_x / distance, delta_y / distance, 0.0), 1.0, False)
    if distance > ATTACK_RANGE_CM:
        return
    record["in_attack_range_seconds"] += delta
    if pawn.is_attack_ready() and pawn.execute_primary_attack(target):
        record["player_attacks"] += 1
    if run["mode"] == "sword_spells":
        cast_spell(pawn, target_location, now, run, record)


def find_nearest_minion(minion_lines, ploc):
    best = None
    for line in minion_lines:
        distance = math.hypot(line["x"] - ploc.x, line["y"] - ploc.y)
        if distance <= MINION_PRIORITY_RADIUS_CM and (best is None or distance < best[0]):
            best = (distance, line)
    return best[1] if best else None


def note_minions(run, record, minion_lines):
    alive_now = {line["actor"] for line in minion_lines}
    record["minions_spawned"] += len(alive_now - run["minions_alive"] - run["minions_dead"])
    for name in run["minions_alive"] - alive_now - run["minions_dead"]:
        record["minions_killed"] += 1
        run["minions_dead"].add(name)
    run["minions_alive"] = alive_now


def track_player(run, record, player_combat):
    health = player_combat.get_current_health()
    previous = run["player_previous_health"]
    if previous is not None and health < previous:
        record["player_damage_taken"] += previous - health
    run["player_previous_health"] = health
    if record["player_min_health"] is None or health < record["player_min_health"]:
        record["player_min_health"] = health
    record["player_max_health"] = player_combat.get_max_health()


def close_stagger(run, record, now):
    stagger = run["stagger"]
    if stagger is None:
        return
    run["stagger"] = None
    length = now - stagger["start_game"]
    if length < LONG_STAGGER_SECONDS:
        record["short_stagger_count"] += 1
        return
    record["stagger_intervals"].append({"start_seconds": round(stagger["start_game"] - run["started_game"], 2), "length_seconds": round(length, 2),
                                        "action_before": stagger["action_before"], "stagger_action": stagger["stagger_action"],
                                        "flying": stagger["flying"], "phase": stagger["phase"], "boss_hp_ratio": stagger["boss_hp_ratio"]})


def track_stagger(run, record, now, boss_line, flying, phase, hp_ratio):
    in_stagger = boss_line is not None and boss_line["state"] == "Stagger"
    if not in_stagger:
        close_stagger(run, record, now)
        if boss_line is not None:
            run["last_non_stagger_action"] = boss_line["action"]
        return
    if run["stagger"] is None:
        run["stagger"] = {"start_game": now, "action_before": run["last_non_stagger_action"], "stagger_action": boss_line["action"],
                          "flying": flying, "phase": phase, "boss_hp_ratio": hp_ratio}
    if now - run["stagger"]["start_game"] >= LONG_STAGGER_SECONDS:
        request_shot(run, "long_stagger")


def close_flight(run, record, now):
    flight = run["flight"]
    if flight is None:
        return
    run["flight"] = None
    record["flight_intervals"].append({"start_seconds": round(flight["start_game"] - run["started_game"], 2),
                                       "end_seconds": round(now - run["started_game"], 2), "length_seconds": round(now - flight["start_game"], 2),
                                       "boss_hp_ratio_at_start": flight["boss_hp_ratio"]})


def track_flight(run, record, now, flying, hp_ratio):
    if not flying:
        close_flight(run, record, now)
        return
    if run["flight"] is None:
        run["flight"] = {"start_game": now, "boss_hp_ratio": hp_ratio}
    if now - run["flight"]["start_game"] >= FLIGHT_SHOT_DELAY_SECONDS:
        request_shot(run, "flight")


def track_boss(run, record, now, boss_actor, boss_combat, boss_line):
    maximum = boss_combat.get_max_health()
    hp_ratio = round(boss_combat.get_current_health() / maximum, 4) if maximum > 0.0 else None
    record["boss_max_health"] = maximum
    phase = boss_actor.get_monster_phase_index()
    flying = bool(boss_actor.is_monster_flying())
    if run["engaged_game"] is None and boss_line is not None and boss_line["engaged"]:
        run["engaged_game"] = now
        record["engage_seconds"] = round(fight_seconds(run, now), 2)
    if run["last_phase"] is not None and phase != run["last_phase"]:
        record["phase_transitions"].append({"from": run["last_phase"], "to": phase, "seconds": round(fight_seconds(run, now), 2), "boss_hp_ratio": hp_ratio})
    run["last_phase"] = phase
    if phase >= 1 and record["phase1_seconds"] is None and run["engaged_game"] is not None:
        record["phase1_seconds"] = round(now - run["engaged_game"], 2)
    if phase >= 1:
        request_shot(run, "phase1")
    action = boss_line["action"] if boss_line is not None else None
    if action and action != run["last_action"]:
        counts = record["boss_action_counts"]
        counts[action] = counts.get(action, 0) + 1
    run["last_action"] = action
    track_stagger(run, record, now, boss_line, flying, phase, hp_ratio)
    track_flight(run, record, now, flying, hp_ratio)
    if fight_seconds(run, now) >= run["next_timeline_game"]:
        record["boss_hp_timeline"].append({"t": run["timeline_index"], "boss_hp_ratio": hp_ratio})
        run["timeline_index"] += 1
        run["next_timeline_game"] = run["timeline_index"] * TIMELINE_INTERVAL_SECONDS
    return hp_ratio, phase, flying


def write_sample(run, now, ploc, boss_actor, boss_line, hp_ratio, phase, flying, kind, minion_count, player_health, rolling):
    handle = fight_data.get("sample_handle")
    if handle is None or fight_seconds(run, now) < run["next_sample_game"]:
        return
    run["next_sample_game"] = fight_seconds(run, now) + SAMPLE_INTERVAL_SECONDS
    location = boss_actor.get_actor_location()
    entry = {"t": round(fight_seconds(run, now), 2), "engaged": run["engaged_game"] is not None,
             "boss": {"x": round(location.x), "y": round(location.y), "z": round(location.z), "hp_ratio": hp_ratio, "phase": phase, "flying": flying,
                      "state": boss_line["state"] if boss_line else None, "action": boss_line["action"] if boss_line else None},
             "player": {"x": round(ploc.x), "y": round(ploc.y), "hp": round(player_health, 1), "rolling": rolling},
             "target": kind, "minions": minion_count}
    handle.write(json.dumps(entry, ensure_ascii=False) + "\n")


def end_fight(run, record, reason, now):
    close_stagger(run, record, now)
    close_flight(run, record, now)
    record["ended_reason"] = reason
    record["boss_defeated"] = reason in ("boss_dead", "boss_missing")
    if record["boss_defeated"]:
        record["kill_seconds"] = round(now - run["engaged_game"], 2) if run["engaged_game"] is not None else None
        request_shot(run, "kill")
    else:
        request_shot(run, "end")
    record["player_damage_taken"] = round(record["player_damage_taken"], 1)
    record["player_damage_in_H"] = round(record["player_damage_taken"] / PLAYER_REFERENCE_MAX_HEALTH, 2)
    record["game_seconds"] = round(fight_seconds(run, now), 2)
    record["real_seconds"] = round(time.monotonic() - run["real_started"], 2)
    record["game_per_real"] = round(record["game_seconds"] / record["real_seconds"], 3) if record["real_seconds"] > 0.0 else None
    record["target_seconds"] = {key: round(value, 2) for key, value in record["target_seconds"].items()}
    record["in_attack_range_seconds"] = round(record["in_attack_range_seconds"], 2)
    run["phase"] = "shots"
    run["shots_deadline_real"] = time.monotonic() + SHOT_DRAIN_REAL_SECONDS
    close_sample_file()


def decide_end(run, record, now, boss_alive, boss_actor_exists, player_combat):
    if not boss_alive:
        return "boss_dead" if boss_actor_exists else "boss_missing"
    if not player_combat.is_alive():
        return "player_dead"
    if run["engaged_game"] is None and fight_seconds(run, now) >= ENGAGE_WAIT_GAME_SECONDS:
        return "never_engaged"
    if run["engaged_game"] is not None and now - run["engaged_game"] >= run["cap_seconds"]:
        return "cap"
    if time.monotonic() - run["real_started"] >= run["cap_seconds"] * 2.0 + REAL_TIMEOUT_EXTRA_SECONDS:
        return "real_timeout"
    return None


def advance_spawning(world, now):
    run = fight_data["run"]
    lines = read_monster_lines(world)
    boss_line = next((line for line in lines if line["definition"] == run["boss_definition_id"]), None)
    if boss_line is not None and is_combat_alive(find_monster_actors(world, {boss_line["actor"]}).get(boss_line["actor"])):
        run["boss_name"] = boss_line["actor"]
        run["started_game"] = now
        run["last_game"] = now
        run["phase"] = "fighting"
        fight_data["state"]["stage"] = "fighting"
        return
    if time.monotonic() - run["real_started"] >= SPAWN_WAIT_REAL_SECONDS:
        stop_scenario("failed", f"{run['boss']} 보스를 {SPAWN_WAIT_REAL_SECONDS:g}초 안에 찾지 못했다(디버그 줄 def={run['boss_definition_id']})")


def advance_fighting(world, now):
    run = fight_data["run"]
    record = run["record"]
    delta = max(0.0, now - run["last_game"])
    run["last_game"] = now
    pawn = get_player_pawn(world)
    player_combat = pawn.get_component_by_class(unreal.TDCombatComponent) if pawn else None
    if player_combat is None:
        raise RuntimeError("플레이어 폰 또는 전투 컴포넌트를 찾지 못했다")
    lines = read_monster_lines(world)
    boss_line = next((line for line in lines if line["actor"] == run["boss_name"]), None)
    minion_lines = [line for line in lines if line["definition"] == MINION_DEFINITION_ID and line["health"] > 0.0]
    note_minions(run, record, minion_lines)
    ploc = pawn.get_actor_location()
    nearest_minion = find_nearest_minion(minion_lines, ploc)
    wanted = {run["boss_name"]}
    if nearest_minion is not None:
        wanted.add(nearest_minion["actor"])
    actors = find_monster_actors(world, wanted)
    boss_actor = actors.get(run["boss_name"])
    boss_combat = boss_actor.get_component_by_class(unreal.TDCombatComponent) if boss_actor else None
    boss_alive = boss_combat is not None and boss_combat.is_alive()
    track_player(run, record, player_combat)
    hp_ratio = phase = flying = None
    if boss_alive:
        hp_ratio, phase, flying = track_boss(run, record, now, boss_actor, boss_combat, boss_line)
    reason = decide_end(run, record, now, boss_alive, boss_actor is not None, player_combat)
    if reason is not None:
        end_fight(run, record, reason, now)
        return
    rolling = bool(pawn.is_roll_playing())
    write_sample(run, now, ploc, boss_actor, boss_line, hp_ratio, phase, flying, "minion" if nearest_minion else "boss", len(minion_lines),
                 player_combat.get_current_health(), rolling)
    if rolling:
        return
    if hold_for_danger(world, pawn, ploc, boss_actor, now, run, record):
        return
    minion_actor = actors.get(nearest_minion["actor"]) if nearest_minion else None
    if minion_actor is not None and is_combat_alive(minion_actor):
        act_on_target(world, pawn, ploc, minion_actor, "minion", delta, now, run, record)
        return
    act_on_target(world, pawn, ploc, boss_actor, "boss", delta, now, run, record)


def update_progress(world, now):
    run = fight_data["run"]
    state = fight_data["state"]
    record = run["record"]
    state["game_seconds"] = round(now, 2)
    state["stage"] = run["phase"]
    state["progress"] = {"fight_seconds": round(fight_seconds(run, now), 1) if run["started_game"] is not None else None,
                         "engaged_seconds": round(engaged_seconds(run, now), 1) if run["engaged_game"] is not None else None,
                         "boss_hp_ratio": record["boss_hp_timeline"][-1]["boss_hp_ratio"] if record["boss_hp_timeline"] else None,
                         "player_damage_taken": round(record["player_damage_taken"], 1), "player_attacks": record["player_attacks"],
                         "rolls": record["rolls"], "minions_killed": record["minions_killed"]}
    if time.monotonic() - fight_data.get("last_state_write_real", 0.0) >= STATE_WRITE_INTERVAL_REAL_SECONDS:
        write_state()


def tick(delta_seconds):
    run = fight_data.get("run")
    if run is None or run["phase"] == "done":
        return
    try:
        world = get_game_world()
        if world is None:
            stop_scenario("failed", "PIE 월드가 판 도중 사라졌다")
            return
        now = get_game_seconds(world)
        if run["phase"] == "spawning":
            advance_spawning(world, now)
        elif run["phase"] == "fighting":
            advance_fighting(world, now)
        if run["phase"] == "shots":
            process_shot_queue(world, run)
            if is_shot_queue_empty() or time.monotonic() >= run["shots_deadline_real"]:
                if not is_shot_queue_empty():
                    fight_data["state"]["errors"].append("스크린샷 대기열이 시간 안에 비지 않았다")
                stop_scenario("complete")
                return
        elif run["phase"] == "fighting":
            process_shot_queue(world, run)
        update_progress(world, now)
    except Exception:
        stop_scenario("failed", traceback.format_exc())


def new_state(boss, mode, cap_seconds, map_path):
    return {"tool": "editor_pie_mid_boss_fight", "result": "running", "stage": "starting", "boss": boss, "mode": mode, "cap_seconds": cap_seconds,
            "map": map_path, "started": time.strftime("%Y-%m-%d %H:%M:%S"), "errors": [], "notes": [], "progress": {}, "game_seconds": None,
            "samples_path": None, "fight": None}


def start_fight(request):
    unregister_tick()
    close_sample_file()
    world = get_game_world()
    map_path = world.get_path_name() if world else ""
    boss = request.get("boss", "")
    mode = request.get("mode", "sword")
    cap_seconds = float(request.get("cap_seconds", DEFAULT_CAP_SECONDS))
    fight_data.clear()
    if boss not in BOSS_DEFINITION_IDS or mode not in FIGHT_MODES:
        return {"started": False, "error": f"boss는 {list(BOSS_DEFINITION_IDS)}, mode는 {list(FIGHT_MODES)} 중 하나여야 한다: {boss}/{mode}"}
    if world is None:
        return {"started": False, "error": "PIE가 켜져 있지 않다"}
    run = new_run(boss, mode, cap_seconds)
    state = new_state(boss, mode, cap_seconds, map_path)
    state["fight"] = run["record"]
    fight_data.update({"run": run, "state": state, "shots": {"pending": [], "active": None}})
    destroyed = clean_world(world)
    player = prepare_player(world, request.get("origin"))
    if player is None or player["max_health"] < PLAYER_TEST_MAX_HEALTH:
        stop_scenario("failed", f"플레이어 체력을 {PLAYER_TEST_MAX_HEALTH:g}로 올리지 못했다: {player}")
        return {"started": False, "error": state["errors"][-1]}
    os.makedirs(OUTPUT_DIRECTORY, exist_ok=True)
    samples_path = os.path.join(OUTPUT_DIRECTORY, f"samples_{boss}_{mode}.jsonl")
    fight_data["sample_handle"] = open(samples_path, "w", encoding="utf-8", buffering=1)
    state["samples_path"] = samples_path
    run["record"]["player_max_health"] = player["max_health"]
    run_console(world, SPAWN_COMMAND.format(boss=boss))
    write_state()
    setattr(unreal, TICK_HANDLE_ATTRIBUTE, unreal.register_slate_post_tick_callback(tick))
    return {"started": True, "boss": boss, "mode": mode, "cap_seconds": cap_seconds, "destroyed": destroyed, "player": player, "map": map_path}


def report_status():
    world = get_game_world()
    state = fight_data.get("state")
    payload = {"pie_running": world is not None, "pie_world": world.get_path_name() if world else None,
               "result": state.get("result") if state else None, "stage": state.get("stage") if state else None}
    pawn = get_player_pawn(world) if world else None
    if pawn is not None:
        location = pawn.get_actor_location()
        payload["player"] = {"x": round(location.x, 1), "y": round(location.y, 1), "z": round(location.z, 1), "yaw": round(pawn.get_actor_rotation().yaw, 1)}
    return payload


def cleanup_fight():
    unregister_tick()
    close_sample_file()
    world = get_game_world()
    destroyed = clean_world(world) if world else None
    state = fight_data.get("state")
    if state is not None:
        if state.get("result") == "running":
            state["result"] = "cancelled"
        state["stage"] = "cleaned"
        write_state()
    run = fight_data.get("run")
    if run is not None:
        run["phase"] = "done"
    fight_data.pop("run", None)
    fight_data.pop("shots", None)
    return {"cleaned": True, "destroyed": destroyed}


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
    handlers = {"start": lambda: start_fight(request), "status": report_status, "cleanup": cleanup_fight}
    handler = handlers.get(action)
    result = handler() if handler else {"error": f"알 수 없는 action: {action}"}
    print(RESULT_MARKER, json.dumps(result, ensure_ascii=False, default=str))


main()
