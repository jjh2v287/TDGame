"""실행 환경: 밖에서는 시스템 Python(에디터가 LV_DarkFantasy_OpenWorld를 열고 있고 editor_place_field_encounters.py가 놓은 공터 조우 3개가 저장돼 있어야 함, 동료 세션이 에디터를 쓰지 않을 때, 에디터 창을 앞에), 안에서는 PIE 월드 조회·틱 기록기용으로 같은 파일이 원격 실행된다.
실행 명령: python -B Tools/Gameplay/pie_check_field_encounters.py [--run 이름] [--only West,North,East] [--skip-leash] [--overwrite] [--dry-run] [--self-check]  (PowerShell, --dry-run은 에디터 없이 계획만 출력, --self-check는 PIE에서 기록기·생존 읽기·처치 분기만 확인하고 판정하지 않음)
출력: Saved/Gameplay/FieldEncounters/<run>/result.json(공터마다·PIE 종료 호출 전 부분 저장, complete·passed, 걷기 표본·게임 시각 폭·창 변위, 기록기 기준 추적, 처치·사망 시각·부활 원값)·summary.md·recorder_<공터>.json, --self-check는 <run>/selfcheck.json. 종료 코드 0 = 통과, 1 = 판정 실패, 2 = PIE 시작 실패
상태: GP-03. 판정선은 spec-v2.md 4절·spec-v2-add1.md B절 그대로, 측정 규칙은 spec-v2-add2.md(A 처치 전 생존 확인, B 틱 기록기로 추적 기준 = 실제 스폰, C 걷기 시간·창을 게임 시각으로·측정 무효 조건, D 대체 경고 문구, E 부분 결과, F 3회 판정). 에디터 없이 검증됨(ast·dry-run·가짜 데이터), 기록기·생존 읽기 API는 --self-check 전까지 미검증. 무적·체력 치트를 쓰지 않는다.
"""
import argparse
import json
import math
import os
import statistics
import subprocess
import sys
import time
import types
import uuid

try:
    import unreal
except ImportError:
    unreal = None

PIE_DIRECTORY_PARTS = ("Saved", "Gameplay", "FieldEncounters")
READER_REQUEST_NAME = "pie_request.json"
READER_RESULT_NAME = "pie_result.json"
ACTOR_TAG_PREFIX = "TDFieldEncounter_"
NEARBY_MONSTER_RADIUS_CM = 6000.0
GROUND_TRACE_UP_CM = 1000.0
GROUND_TRACE_DOWN_CM = 3000.0
DEFAULT_HALF_HEIGHT_CM = 88.0
WALKABLE_NORMAL_Z_MIN = 0.5
HIT_IMPACT_POINT = 5
HIT_IMPACT_NORMAL = 7
HIT_COMPONENT = 10
PLAYER_KILL_DAMAGE = 1.0e9
RECORDER_MODULE_NAME = "td_field_encounter_pie_recorder"
RECORDER_MAX_ROWS = 20000
RECORDER_MAX_REAL_SECONDS = 900.0
CONTROLLED_PAWN_READERS = ("get_controlled_pawn", "k2_get_pawn")


def normalize_token(value):
    return "".join(character for character in str(getattr(value, "name", value)).lower() if character.isalnum())


def planar_distance(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def rounded_vector(vector):
    return [round(vector.x, 1), round(vector.y, 1), round(vector.z, 1)]


def is_life_dead(life):
    if not life["has_pawn"]:
        return True
    if life["controller_has_pawn"] is False:
        return True
    if life["health"] is not None and life["health"] <= 0.0:
        return True
    return life["is_alive"] is False


def read_game_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def find_encounter(world, field_id):
    tag = ACTOR_TAG_PREFIX + field_id
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDArenaEncounter):
        if tag in [str(item) for item in actor.get_editor_property("tags")]:
            return actor
    return None


def read_controlled_pawn(controller):
    for reader_name in CONTROLLED_PAWN_READERS:
        reader = getattr(controller, reader_name, None)
        if reader is not None:
            return reader(), reader_name
    return None, None


def read_player_life(world):
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    controller = unreal.GameplayStatics.get_player_controller(world, 0)
    controlled, reader_name = read_controlled_pawn(controller) if controller is not None else (None, None)
    life = {
        "has_pawn": pawn is not None,
        "pawn_name": None if pawn is None else pawn.get_name(),
        "has_controller": controller is not None,
        "controller_pawn_reader": reader_name,
        "controller_has_pawn": None if reader_name is None else controlled is not None,
        "has_combat_component": False,
        "health": None,
        "is_alive": None,
        "location": None if pawn is None else rounded_vector(pawn.get_actor_location()),
    }
    combat = None if pawn is None else unreal.TDCombatLibrary.get_combat_component(pawn)
    if combat is not None:
        life.update(has_combat_component=True, health=float(combat.get_current_health()), is_alive=bool(combat.is_alive()))
    life["dead"] = is_life_dead(life)
    return life


def read_in_editor(request):
    action = request["action"]
    if action == "recorder_status":
        return report_recorder_status()
    if action == "recorder_stop":
        return stop_recorder(request)
    world = read_game_world()
    if world is None:
        return {"ok": False, "error": "PIE world missing"}
    if action == "recorder_start":
        return start_recorder(world, request)
    if action == "recorder_read":
        return read_recorder(request)
    if action == "kill_wave":
        unreal.SystemLibrary.execute_console_command(world, "TD.Arena.KillWave")
        return {"ok": True}
    if action == "player":
        return read_player(world)
    if action == "player_life":
        return dict(read_player_life(world), ok=True, game_seconds=float(unreal.GameplayStatics.get_time_seconds(world)))
    if action == "kill_player":
        return kill_player(world)
    return read_field_state(world, request)


def read_field_state(world, request):
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    if pawn is None:
        return {"ok": False, "error": "PIE pawn missing"}
    encounter = find_encounter(world, request["field_id"])
    if encounter is None:
        return {"ok": False, "error": f"encounter not found: {request['field_id']}"}
    monsters = list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDMonsterCharacter))
    center = encounter.get_actor_location()
    ignored = monsters + [pawn]
    listed = []
    for monster in monsters:
        location = monster.get_actor_location()
        if math.hypot(location.x - center.x, location.y - center.y) > encounter.get_arena_radius_cm() + NEARBY_MONSTER_RADIUS_CM:
            continue
        species = monster.get_species()
        capsule = monster.get_component_by_class(unreal.CapsuleComponent)
        half_height = float(capsule.get_scaled_capsule_half_height()) if capsule is not None else DEFAULT_HALF_HEIGHT_CM
        listed.append({
            "name": monster.get_name(),
            "species_id": None if species is None else str(species.get_editor_property("definition_id")),
            "location": [location.x, location.y, location.z],
            "foot_z": location.z - half_height,
            "ground_z": find_ground_z(world, location.x, location.y, location.z, ignored),
        })
    pawn_location = pawn.get_actor_location()
    max_speed_reader = getattr(pawn, "get_max_move_speed", None)
    return {
        "ok": True,
        "state": normalize_token(encounter.get_encounter_state()),
        "wave_index": int(encounter.get_current_wave_index()),
        "alive": int(encounter.get_alive_monster_count()),
        "killed": int(encounter.get_killed_monster_count()),
        "attempt": int(encounter.get_attempt_count()),
        "radius_cm": float(encounter.get_arena_radius_cm()),
        "center": [center.x, center.y, center.z],
        "checkpoint": [encounter.get_checkpoint_transform().translation.x, encounter.get_checkpoint_transform().translation.y, encounter.get_checkpoint_transform().translation.z],
        "game_seconds": float(unreal.GameplayStatics.get_time_seconds(world)),
        "pawn": [pawn_location.x, pawn_location.y, pawn_location.z],
        "max_speed": None if max_speed_reader is None else float(max_speed_reader()),
        "monsters": listed,
    }


def read_respawn_delay(world):
    try:
        game_mode = unreal.GameplayStatics.get_game_mode(world)
        return float(game_mode.get_editor_property("player_respawn_delay_seconds"))
    except Exception:
        return None


def read_player(world):
    game_seconds = float(unreal.GameplayStatics.get_time_seconds(world))
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    if pawn is None:
        return {"ok": True, "has_pawn": False, "game_seconds": game_seconds}
    location = pawn.get_actor_location()
    try:
        age_seconds = float(pawn.get_game_time_since_creation())
    except Exception:
        age_seconds = None
    return {"ok": True, "has_pawn": True, "pawn_name": pawn.get_name(), "pawn": [location.x, location.y, location.z], "age_seconds": age_seconds, "game_seconds": game_seconds}


def kill_player(world):
    life = read_player_life(world)
    result = {"ok": True, "pawn_name": life["pawn_name"], "game_seconds": float(unreal.GameplayStatics.get_time_seconds(world)), "respawn_delay": read_respawn_delay(world), "life_before": life}
    if life["dead"]:
        return dict(result, already_dead=True, applied="already_dead")
    spec = unreal.TDDamageSpec()
    spec.set_editor_property("amount", PLAYER_KILL_DAMAGE)
    spec.set_editor_property("element", unreal.TDDamageElement.PHYSICAL)
    applied = unreal.TDCombatLibrary.try_apply_damage(unreal.GameplayStatics.get_player_pawn(world, 0), spec)
    return dict(result, already_dead=False, applied=float(applied), life_after=read_player_life(world))


def find_ground_z(world, x, y, z, ignored):
    hits = unreal.SystemLibrary.line_trace_multi(world, unreal.Vector(x, y, z + GROUND_TRACE_UP_CM), unreal.Vector(x, y, z - GROUND_TRACE_DOWN_CM), unreal.TraceTypeQuery.ECC_VISIBILITY, True, ignored, unreal.DrawDebugTrace.NONE, True) or []
    for hit in hits:
        values = hit.to_tuple()
        component = values[HIT_COMPONENT]
        component_class = component.get_class().get_name() if component else ""
        if component_class != "BrushComponent" and values[HIT_IMPACT_NORMAL].z > WALKABLE_NORMAL_Z_MIN:
            return float(values[HIT_IMPACT_POINT].z)
    return None


def find_recorder_state():
    module = sys.modules.get(RECORDER_MODULE_NAME)
    return None if module is None else module.state


def release_recorder(state, reason):
    handle = state["handle"]
    state["handle"] = None
    if state["stopped"] is None:
        state["stopped"] = reason
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)


def remove_recorder(reason):
    state = find_recorder_state()
    if state is None:
        return None
    sys.modules.pop(RECORDER_MODULE_NAME, None)
    release_recorder(state, reason)
    return state


def describe_recorder(state):
    return {
        "token": state["token"], "field_id": state["field_id"], "registered": state["handle"] is not None, "stopped": state["stopped"], "error": state["error"],
        "overflow": state["overflow"], "row_count": len(state["rows"]), "player_events": list(state["player_events"]), "center": state["center"], "radius_cm": state["radius_cm"],
    }


def start_recorder(world, request):
    previous = remove_recorder("replaced")
    state = {
        "token": request["token"], "field_id": request["field_id"], "center": None, "radius_cm": None, "rows": [], "player_events": [], "last_player": None,
        "last_game_seconds": None, "overflow": False, "stopped": None, "error": None, "handle": None, "started_real": time.time(),
    }
    encounter = find_encounter(world, request["field_id"])
    if encounter is not None:
        state["center"] = rounded_vector(encounter.get_actor_location())
        state["radius_cm"] = float(encounter.get_arena_radius_cm())
    module = types.ModuleType(RECORDER_MODULE_NAME)
    module.state = state
    sys.modules[RECORDER_MODULE_NAME] = module
    state["handle"] = unreal.register_slate_post_tick_callback(make_recorder_tick(state))
    return dict(describe_recorder(state), ok=True, previous=None if previous is None else previous["token"])


def make_recorder_tick(state):
    def tick(delta_seconds):
        if state["handle"] is None:
            return
        try:
            world = read_game_world()
            if world is None:
                release_recorder(state, "pie_ended")
                return
            if time.time() - state["started_real"] > RECORDER_MAX_REAL_SECONDS:
                release_recorder(state, "lifetime")
                return
            record_recorder_row(world, state)
        except Exception as error:
            state["error"] = repr(error)
            release_recorder(state, "exception")
    return tick


def record_recorder_row(world, state):
    game_seconds = float(unreal.GameplayStatics.get_time_seconds(world))
    last_seconds = state["last_game_seconds"]
    if last_seconds is not None and game_seconds < last_seconds:
        release_recorder(state, "world_restarted")
        return
    if last_seconds is not None and game_seconds == last_seconds:
        return
    state["last_game_seconds"] = game_seconds
    life = read_player_life(world)
    player_key = (life["pawn_name"], life["dead"])
    if player_key != state["last_player"]:
        state["last_player"] = player_key
        state["player_events"].append({"gt": round(game_seconds, 4), "pawn_name": life["pawn_name"], "dead": life["dead"], "health": life["health"], "has_pawn": life["has_pawn"]})
    if len(state["rows"]) >= RECORDER_MAX_ROWS:
        state["overflow"] = True
        return
    encounter = find_encounter(world, state["field_id"])
    state["rows"].append({
        "gt": round(game_seconds, 4),
        "st": None if encounter is None else normalize_token(encounter.get_encounter_state()),
        "al": None if encounter is None else int(encounter.get_alive_monster_count()),
        "pn": life["pawn_name"], "pd": life["dead"], "hp": life["health"], "p": life["location"],
        "m": read_recorder_monsters(world, state),
    })


def read_recorder_monsters(world, state):
    center = state["center"]
    limit = None if state["radius_cm"] is None else state["radius_cm"] + NEARBY_MONSTER_RADIUS_CM
    monsters = []
    for monster in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDMonsterCharacter):
        location = monster.get_actor_location()
        if center is not None and math.hypot(location.x - center[0], location.y - center[1]) > limit:
            continue
        monsters.append([monster.get_name(), round(float(monster.get_game_time_since_creation()), 4), round(location.x, 1), round(location.y, 1), round(location.z, 1)])
    return monsters


def read_recorder(request):
    state = find_recorder_state()
    if state is None:
        return {"ok": False, "error": "recorder missing"}
    if request.get("token") != state["token"]:
        return {"ok": False, "error": f"recorder token mismatch: {state['token']}"}
    rows = [] if request.get("events_only") else state["rows"][int(request.get("since", 0)):]
    return dict(describe_recorder(state), ok=True, rows=rows, next=len(state["rows"]))


def report_recorder_status():
    state = find_recorder_state()
    return {"ok": True, "present": state is not None, "recorder": None if state is None else describe_recorder(state)}


def stop_recorder(request):
    state = remove_recorder("stopped")
    if state is None:
        return {"ok": True, "present": False}
    return {"ok": True, "present": True, "token_matched": request.get("token") in (None, state["token"]), "recorder": describe_recorder(state)}


def run_reader_mode():
    directory = os.path.join(unreal.SystemLibrary.get_project_directory(), *PIE_DIRECTORY_PARTS)
    with open(os.path.join(directory, READER_REQUEST_NAME), encoding="utf-8") as handle:
        request = json.load(handle)
    try:
        result = read_in_editor(request)
    except Exception as error:
        result = {"ok": False, "error": repr(error)}
    result["request_id"] = request["request_id"]
    with open(os.path.join(directory, READER_RESULT_NAME), "w", encoding="utf-8", newline="\n") as handle:
        json.dump(result, handle, ensure_ascii=False, separators=(",", ":"))


if unreal is not None:
    run_reader_mode()


SELF_PATH = os.path.abspath(__file__) if unreal is None else None
TOOLS_ROOT = os.path.dirname(os.path.dirname(SELF_PATH)) if SELF_PATH else None

CHECKPOINT_WALK_TARGET_INSET_CM = 650.0
WALK_RADIUS_CM = 120.0
WALK_FALLBACK_SPEED_CM = 600.0
WALK_LIMIT_FACTOR = 1.5
WALK_LIMIT_EXTRA_SECONDS = 10.0
STUCK_WINDOW_SECONDS = 2.0
STUCK_DISPLACEMENT_CM = 20.0
POLL_TIMEOUT_SECONDS = 40.0
CHASE_SECONDS = 3.0
CHASE_MIN_MEDIAN_CM = 150.0
FOOT_GROUND_LIMIT_CM = 100.0
LEASH_FAR_OUTSET_CM = 3000.0
LEASH_CLEAR_RADIUS_CM = 6000.0
LEASH_EXTRA_WAIT_SECONDS = 1.5
KILL_WAIT_SECONDS = 3.5
KILL_EXTRA_ROUNDS = 2
PIE_WARMUP_SECONDS = 6.0
READER_TIMEOUT_SECONDS = 60.0
RUN_DEFAULT_PREFIX = "run-"
ACTIVE_STATES = ("loading", "wavedelay", "waveactive")
CHASING_ROLES = ("Melee", "Charger")
LOG_FILE_PARTS = ("Saved", "Logs", "TDGame.log")
SPAWN_FAILURE_MARKER = "SpawnActor failed"
FALLBACK_MARKERS = ("using player start", "spawning there anyway")
RESPAWN_FALLBACK_DELAY_SECONDS = 4.0
RESPAWN_EXTRA_SECONDS = 2.0
RESPAWN_POLL_GRACE_SECONDS = 1.0
RESPAWN_POLL_REAL_TIMEOUT_SECONDS = 40.0
RESPAWN_NEAR_CHECKPOINT_CM = 300.0
RESPAWN_WALK_DISTANCE_CM = 400.0
RESPAWN_WALK_LIMIT_SECONDS = 8.0
RESPAWN_MIN_DISPLACEMENT_CM = 200.0
POSTDEATH_IDLE_SECONDS = 15.0
PLAYER_CODE_MAX_SPEED_CM = 600.0
WALK_MIN_SAMPLES = 3
WALK_SPEED_VALID_FACTOR = 1.1
SLATE_TICK_CLAMP_MS = 125.0
SPAWN_REFERENCE_MAX_AGE_SECONDS = 0.1
TIME_EPSILON_SECONDS = 0.001
SELF_CHECK_RUN_PREFIX = "selfcheck-"
SELF_CHECK_SAMPLE_SECONDS = 2.0
SELF_CHECK_MIN_ROWS = 10
SELF_CHECK_EVENT_WAIT_SECONDS = 1.0
SELF_CHECK_RELEASE_TIMEOUT_SECONDS = 15.0
SELF_CHECK_RELEASE_POLL_SECONDS = 1.0


def log(message):
    print(message, flush=True)


def load_modules():
    sys.path.insert(0, os.path.join(TOOLS_ROOT, "Gameplay"))
    sys.path.insert(0, os.path.join(TOOLS_ROOT, "WorldGen"))
    import editor_place_field_encounters as placement_module
    import pie_capture_spots as spots_module
    return placement_module, spots_module


def read_species_role(root, species_id):
    path = os.path.join(root, "Content", "MonsterAI", "Definitions", f"{species_id}.json")
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8") as handle:
        return json.load(handle).get("role")


def wave_counts(field):
    return [sum(entry[1] for entry in wave["entries"]) for wave in field["waves"]]


def walk_target_xy(field, center_xy, checkpoint_xy):
    distance = planar_distance(center_xy, checkpoint_xy)
    ratio = (field["radius_cm"] - CHECKPOINT_WALK_TARGET_INSET_CM) / distance
    return [center_xy[0] + (checkpoint_xy[0] - center_xy[0]) * ratio, center_xy[1] + (checkpoint_xy[1] - center_xy[1]) * ratio]


def window_displacements(frames, time_key="gt"):
    displacements = []
    for index, frame in enumerate(frames):
        for later in frames[index + 1:]:
            if later[time_key] - frame[time_key] < STUCK_WINDOW_SECONDS:
                continue
            displacements.append(planar_distance(frame["pawn"], later["pawn"]))
            break
    return displacements


def smallest_window_displacement(frames, time_key="gt"):
    displacements = window_displacements(frames, time_key)
    return min(displacements) if displacements else None


def walk_path_length_cm(frames):
    return sum(planar_distance(earlier["pawn"], later["pawn"]) for earlier, later in zip(frames, frames[1:]))


def resolve_speed_cap(frames, before_max_speed):
    frame_maxima = [float(frame["max"]) for frame in frames if frame.get("max")]
    if frame_maxima:
        return max(frame_maxima), "walk_frames_max"
    if before_max_speed:
        return float(before_max_speed), "read_before_walk"
    return PLAYER_CODE_MAX_SPEED_CM, "code_default_TDGameCharacter"


def measure_walk(frames, before_max_speed):
    cap, cap_source = resolve_speed_cap(frames, before_max_speed)
    limit = cap * WALK_SPEED_VALID_FACTOR
    measure = {"valid": False, "reason": None, "samples": len(frames), "game_span_s": None, "path_cm": None, "avg_speed_cm_s": None, "speed_cap_cm_s": cap, "speed_cap_source": cap_source, "speed_limit_cm_s": limit}
    if len(frames) < WALK_MIN_SAMPLES:
        measure["reason"] = f"samples {len(frames)} < {WALK_MIN_SAMPLES}"
        return measure
    span = frames[-1]["gt"] - frames[0]["gt"]
    path = walk_path_length_cm(frames)
    measure.update(game_span_s=span, path_cm=path)
    if span <= 0.0:
        measure["reason"] = f"game span {span:.3f}s <= 0"
        return measure
    speed = path / span
    measure["avg_speed_cm_s"] = speed
    if speed > limit:
        measure["reason"] = f"avg speed {speed:.1f} > limit {limit:.1f} cm/s"
        return measure
    measure["valid"] = True
    return measure


def describe_walk_timing(walked):
    frame_ms = walked.get("frame_ms") or []
    slate_ms = walked.get("slate_ms") or []
    return {
        "probe_t_s": walked.get("t"), "frame_real_s": round(sum(frame_ms) / 1000.0, 4), "slate_s": round(sum(slate_ms) / 1000.0, 4),
        "frames_over_slate_clamp": sum(1 for value in frame_ms if value > SLATE_TICK_CLAMP_MS), "hitches": len(walked.get("hitches") or []), "jumps": len(walked.get("jumps") or []),
    }


def compact_walk_samples(frames):
    return [[frame["t"], frame["gt"], round(frame["pawn"][0], 1), round(frame["pawn"][1], 1)] for frame in frames]


def describe_stuck(walked):
    measure = walked["walk_measure"]
    validity = "valid" if measure["valid"] else f"measurement_invalid({measure['reason']})"
    speed = measure["avg_speed_cm_s"]
    return f"smallest_{STUCK_WINDOW_SECONDS:g}s_window_cm={walked['smallest_window_displacement_cm']} windows={walked['stuck_window_count']} {validity} avg_speed_cm_s={None if speed is None else round(speed, 1)} limit_cm_s={measure['speed_limit_cm_s']:.1f} game_span_s={measure['game_span_s']}"


def classify_kill(killed):
    if killed.get("already_dead"):
        return "already_dead"
    applied = killed.get("applied")
    if isinstance(applied, (int, float)) and not isinstance(applied, bool) and applied > 0.0:
        return "applied"
    return "no_effect"


def find_observed_death(player_events, kill_game_seconds):
    run_start = None
    for event in player_events:
        if event["gt"] > kill_game_seconds:
            break
        if event["dead"]:
            run_start = event["gt"] if run_start is None else run_start
            continue
        run_start = None
    return run_start


def choose_death_reference(killed, player_events, new_pawn_spawn_seconds, respawn_delay):
    kill_seconds = killed["game_seconds"]
    if classify_kill(killed) != "already_dead":
        return {"source": "kill_request", "observed_s": None, "inferred_s": None, "reference_s": kill_seconds, "reference_from": "kill_request"}
    recorder_seconds = find_observed_death(player_events, kill_seconds)
    observed = kill_seconds if recorder_seconds is None else recorder_seconds
    inferred = None if new_pawn_spawn_seconds is None else new_pawn_spawn_seconds - respawn_delay
    reference = observed if inferred is None or observed <= inferred else inferred
    return {"source": "kill_call" if recorder_seconds is None else "recorder", "observed_s": observed, "inferred_s": inferred, "reference_s": reference, "reference_from": "observed" if reference == observed else "inferred"}


def collect_first_samples(rows):
    first = {}
    previous_seconds = None
    for row in rows:
        for name, age, x, y, z in row["m"]:
            if name in first:
                continue
            first[name] = {"gt": row["gt"], "age": age, "location": [x, y, z], "frame_gap_s": None if previous_seconds is None else row["gt"] - previous_seconds}
        previous_seconds = row["gt"]
    return first


def find_monster_sample_at_or_after(rows, name, game_seconds):
    for row in rows:
        if row["gt"] < game_seconds:
            continue
        for entry in row["m"]:
            if entry[0] == name:
                return {"gt": row["gt"], "location": list(entry[2:5])}
    return None


def evaluate_chase(rows, targets, polled_t0_seconds):
    first = collect_first_samples(rows)
    last_seconds = rows[-1]["gt"] if rows else None
    reasons = []
    details = {}
    displacements = []
    for target in targets:
        name = target["name"]
        reference = first.get(name)
        if reference is None:
            reasons.append(f"{name}: no recorder sample")
            continue
        spawn_seconds = reference["gt"] - reference["age"]
        tolerance = max(SPAWN_REFERENCE_MAX_AGE_SECONDS, reference["frame_gap_s"] or 0.0)
        detail = dict(target, spawn_gt=spawn_seconds, reference_age_s=reference["age"], reference_frame_gap_s=reference["frame_gap_s"], reference_location=reference["location"], first_poll_delay_s=polled_t0_seconds - spawn_seconds)
        details[name] = detail
        if reference["age"] > tolerance + TIME_EPSILON_SECONDS:
            reasons.append(f"{name}: first sample age {reference['age']:.3f}s > {tolerance:.3f}s")
            continue
        target_seconds = spawn_seconds + CHASE_SECONDS
        if last_seconds is None or last_seconds < target_seconds:
            reasons.append(f"{name}: recorder ends at {last_seconds} before {target_seconds:.3f}")
            continue
        later = find_monster_sample_at_or_after(rows, name, target_seconds)
        if later is None:
            detail["missing_at_t1"] = True
            continue
        displacement = planar_distance(reference["location"], later["location"])
        detail.update(t1_gt=later["gt"], elapsed_s=later["gt"] - spawn_seconds, t1_location=later["location"], displacement_cm=displacement)
        displacements.append(displacement)
    median = statistics.median(displacements) if displacements else None
    return {"valid": not reasons, "invalid_reasons": reasons, "requested": len(targets), "measured": len(displacements), "median_cm": median, "targets": details}


def describe_chase(chase):
    delays = [round(item["first_poll_delay_s"], 2) for item in chase["targets"].values()]
    validity = "valid" if chase["valid"] else "measurement_invalid(" + "; ".join(chase["invalid_reasons"]) + ")"
    return f"targets={chase['requested']} measured={chase['measured']} median_cm={chase['median_cm']} {validity} first_poll_delay_s={delays}"


def is_chase_passed(chase):
    return chase["valid"] and chase["median_cm"] is not None and chase["median_cm"] >= CHASE_MIN_MEDIAN_CM


class Checker:
    def __init__(self, placement_module, spots_module, root, run_directory):
        self.placement = placement_module
        self.spots = spots_module
        self.root = root
        self.run_directory = run_directory
        self.request_path = os.path.join(root, *PIE_DIRECTORY_PARTS, READER_REQUEST_NAME)
        self.result_path = os.path.join(root, *PIE_DIRECTORY_PARTS, READER_RESULT_NAME)
        self.recorder_token = None
        self.log_marks = []

    def call_reader(self, request):
        request_id = uuid.uuid4().hex[:10]
        if os.path.exists(self.result_path):
            os.remove(self.result_path)
        self.spots.write_json(self.request_path, dict(request, request_id=request_id))
        completed = subprocess.run([sys.executable, os.path.join(self.root, "Tools", "run_in_editor.py"), SELF_PATH], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180)
        if completed.returncode != 0:
            raise self.spots.ProbeError(f"reader exit {completed.returncode}: {completed.stdout[-300:]}")
        deadline = time.time() + READER_TIMEOUT_SECONDS
        while time.time() < deadline:
            result = self.read_result(request_id)
            if result is not None:
                if not result.get("ok"):
                    raise self.spots.ProbeError(f"{request['action']}: {result.get('error')}")
                return result
            time.sleep(0.3)
        raise self.spots.ProbeError(f"{request['action']}: no reader result")

    def read_result(self, request_id):
        try:
            with open(self.result_path, encoding="utf-8") as handle:
                result = json.load(handle)
        except (OSError, ValueError):
            return None
        return result if result.get("request_id") == request_id else None

    def read_state(self, field_id):
        size = self.log_size()
        state = self.call_reader({"action": "read", "field_id": field_id})
        self.log_marks.append((state["game_seconds"], size))
        return state

    def log_offset_at_or_before(self, game_seconds):
        offsets = [size for seconds, size in self.log_marks if seconds <= game_seconds and size is not None]
        return offsets[-1] if offsets else None

    def start_recorder(self, field, record):
        self.recorder_token = f"{field['id']}-{uuid.uuid4().hex[:8]}"
        try:
            started = self.call_reader({"action": "recorder_start", "field_id": field["id"], "token": self.recorder_token})
        except Exception as error:
            record["recorder"] = {"token": self.recorder_token, "started": False, "error": repr(error)}
            self.recorder_token = None
            log(f"  WARN recorder start failed: {error!r}")
            return
        record["recorder"] = {"token": self.recorder_token, "started": started.get("registered"), "center": started.get("center"), "radius_cm": started.get("radius_cm"), "replaced": started.get("previous")}

    def read_recorder(self, events_only, record):
        if self.recorder_token is None:
            return None
        try:
            return self.call_reader({"action": "recorder_read", "token": self.recorder_token, "since": 0, "events_only": events_only})
        except Exception as error:
            record.setdefault("recorder_errors", []).append(repr(error))
            return None

    def read_recorder_rows(self, field_id, record):
        recorder = self.read_recorder(False, record)
        if recorder is None:
            return []
        rows = recorder.get("rows", [])
        record["recorder_read"] = {key: recorder.get(key) for key in ("registered", "stopped", "error", "overflow", "row_count")}
        try:
            self.spots.write_json(os.path.join(self.run_directory, f"recorder_{field_id}.json"), {"token": recorder.get("token"), "rows": rows, "player_events": recorder.get("player_events")})
        except OSError as error:
            log(f"  WARN recorder rows not saved: {error!r}")
        return rows

    def stop_recorder(self, record):
        token = self.recorder_token
        self.recorder_token = None
        if token is None:
            return
        try:
            stopped = self.call_reader({"action": "recorder_stop", "token": token})
        except Exception as error:
            record.setdefault("recorder", {})["stop_error"] = repr(error)
            return
        summary = stopped.get("recorder") or {}
        record.setdefault("recorder", {}).update(token_matched=stopped.get("token_matched"), stopped_reason=summary.get("stopped"), tick_error=summary.get("error"), overflow=summary.get("overflow"), row_count=summary.get("row_count"), player_events=summary.get("player_events"))

    def release_recorder_quietly(self):
        self.recorder_token = None
        try:
            self.call_reader({"action": "recorder_stop"})
        except Exception as error:
            log(f"  WARN recorder release failed: {error!r}")

    def move_pawn(self, x, y, yaw):
        return self.spots.call_probe({"action": "move", "x": x, "y": y, "yaw": yaw, "refine": True})

    def walk_to_start_ring(self, field, field_state, sink):
        center = field_state["center"]
        checkpoint = field_state["checkpoint"]
        self.move_pawn(checkpoint[0], checkpoint[1], math.degrees(math.atan2(center[1] - checkpoint[1], center[0] - checkpoint[0])))
        before = self.read_state(field["id"])
        target = walk_target_xy(field, center, checkpoint)
        speed = before["max_speed"] or WALK_FALLBACK_SPEED_CM
        distance = planar_distance(before["pawn"], target)
        limit = distance / speed * WALK_LIMIT_FACTOR + WALK_LIMIT_EXTRA_SECONDS
        sink.update(pawn_before=before["pawn"], max_speed=before["max_speed"], walk_distance_cm=distance, walk_limit_seconds=limit)
        walked = self.spots.call_probe({"action": "walk", "path": [target], "seconds": limit, "waypoint_radius_cm": WALK_RADIUS_CM, "hitch_ms": 1000}, result_timeout_seconds=limit + 90.0)
        frames = walked.get("frames", [])
        after = self.read_state(field["id"])
        measure = measure_walk(frames, before["max_speed"])
        windows = window_displacements(frames)
        sink.update(
            reached=bool(walked.get("reached")), walk_seconds=walked.get("t"), walk_game_seconds=measure["game_span_s"], walk_end_game_seconds=frames[-1]["gt"] if frames else None,
            smallest_window_displacement_cm=min(windows) if windows else None, stuck_window_count=len(windows), walk_measure=measure, walk_timing=describe_walk_timing(walked),
            start_gap_cm=planar_distance(before["pawn"], frames[0]["pawn"]) if frames else None, walk_samples=compact_walk_samples(frames),
        )
        return dict(sink, after_walk=after)

    def check_field(self, field, skip_leash, record):
        checks = record["checks"]

        def add(name, ok, detail):
            checks.append({"name": name, "ok": bool(ok), "detail": detail})
            log(f"  {'PASS' if ok else 'FAIL'} {name}: {detail}")

        self.log_marks = [(float("-inf"), self.log_size())]
        self.start_recorder(field, record)
        try:
            self.run_field_steps(field, skip_leash, record, add)
        finally:
            self.stop_recorder(record)

    def run_field_steps(self, field, skip_leash, record, add):
        initial = self.read_state(field["id"])
        record["radius_cm"] = initial["radius_cm"]
        record["attempt_initial"] = initial["attempt"]
        add("state_idle_before", initial["state"] == "idle" and initial["alive"] == 0, f"state={initial['state']} alive={initial['alive']}")
        first = self.enter_and_judge(field, initial, add, "enter", record.setdefault("enter", {}))
        if first["fight_state"] is None:
            return
        self.judge_spawn_and_chase(field, first["fight_state"], add, record.setdefault("spawn", {}))
        if field["id"] == "FieldWest" and not skip_leash:
            self.judge_leash(field, initial, first["fight_state"], add, record.setdefault("leash", {}))
        idle_state = self.judge_death_and_respawn(field, add, record.setdefault("death", {}))
        if idle_state is not None:
            self.enter_and_judge(field, idle_state, add, "postdeath", record.setdefault("postdeath", {}))
        self.judge_victory(field, add, record.setdefault("victory", {}))

    def enter_and_judge(self, field, field_state, add, label, sink):
        walked = self.walk_to_start_ring(field, field_state, sink)
        after = walked["after_walk"]
        smallest = walked["smallest_window_displacement_cm"]
        add(f"{label}_walk_reached", walked["reached"], f"seconds={walked['walk_seconds']} game_seconds={walked['walk_game_seconds']} limit={walked['walk_limit_seconds']:.1f}")
        no_overlap = after["alive"] == 0
        add(f"{label}_fight_overlap", no_overlap, f"alive_after_walk={after['alive']} state={after['state']}")
        stuck_ok = smallest is None or smallest >= STUCK_DISPLACEMENT_CM
        add(f"{label}_not_stuck", no_overlap and walked["walk_measure"]["valid"] and stuck_ok, describe_stuck(walked))
        add(f"{label}_started", after["state"] in ACTIVE_STATES, f"state={after['state']} attempt={after['attempt']}")
        result = {"record": sink, "fight_state": None}
        sink["after_walk"] = {key: after[key] for key in ("state", "alive", "attempt", "game_seconds", "pawn")}
        if after["state"] in ACTIVE_STATES:
            result["fight_state"] = after
        return result

    def poll_until(self, field_id, accepted_states, timeout_seconds):
        deadline = time.time() + timeout_seconds
        while time.time() < deadline:
            state = self.read_state(field_id)
            if state["state"] in accepted_states:
                return state
            time.sleep(0.3)
        return None

    def find_chase_targets(self, t0):
        targets = []
        for monster in t0["monsters"]:
            role = read_species_role(self.root, monster["species_id"])
            if role in CHASING_ROLES:
                targets.append({"name": monster["name"], "role": role, "species_id": monster["species_id"]})
        return targets

    def describe_polled_chase(self, t0, t1, targets):
        start = {monster["name"]: monster for monster in t0["monsters"]}
        later = {monster["name"]: monster for monster in t1["monsters"]}
        polled = {}
        for target in targets:
            if target["name"] not in later:
                continue
            polled[target["name"]] = {"role": target["role"], "species_id": target["species_id"], "displacement_cm": planar_distance(start[target["name"]]["location"], later[target["name"]]["location"])}
        median = statistics.median([item["displacement_cm"] for item in polled.values()]) if polled else None
        return {"game_seconds": t1["game_seconds"], "elapsed": t1["game_seconds"] - t0["game_seconds"], "targets": polled, "polled_median_cm_info": median}

    def judge_spawn_and_chase(self, field, entered, add, record):
        t0 = entered if entered["state"] == "waveactive" else self.poll_until(field["id"], ("waveactive",), POLL_TIMEOUT_SECONDS)
        if t0 is None:
            add("spawn_wave_active", False, "WaveActive not seen")
            return
        expected = wave_counts(field)[0]
        center = t0["center"]
        radius = t0["radius_cm"]
        inside = [monster for monster in t0["monsters"] if planar_distance(monster["location"], center) <= radius]
        grounded = [monster for monster in t0["monsters"] if monster["ground_z"] is not None and abs(monster["foot_z"] - monster["ground_z"]) <= FOOT_GROUND_LIMIT_CM]
        record["t0"] = {"game_seconds": t0["game_seconds"], "alive": t0["alive"], "monsters": t0["monsters"]}
        add("spawn_count", t0["alive"] == expected, f"alive={t0['alive']} listed={len(t0['monsters'])} expected={expected}")
        add("spawn_inside_arena", len(inside) == len(t0["monsters"]), f"inside={len(inside)}/{len(t0['monsters'])}")
        add("spawn_on_ground", len(grounded) == len(t0["monsters"]), f"grounded={len(grounded)}/{len(t0['monsters'])}")
        for monster in t0["monsters"]:
            log(f"    monster {monster['name']} species={monster['species_id']} foot_z={monster['foot_z']:.1f} ground_z={monster['ground_z']} location={[round(value, 1) for value in monster['location']]}")
        t1 = self.poll_until_game_time(field["id"], t0["game_seconds"] + CHASE_SECONDS)
        if t1 is None:
            add("chase_t1", False, "t1 not read")
            return
        targets = self.find_chase_targets(t0)
        record["t1"] = self.describe_polled_chase(t0, t1, targets)
        chase = evaluate_chase(self.read_recorder_rows(field["id"], record), targets, t0["game_seconds"])
        record["chase"] = chase
        add("chase_median", is_chase_passed(chase), describe_chase(chase))

    def poll_until_game_time(self, field_id, game_seconds):
        deadline = time.time() + POLL_TIMEOUT_SECONDS
        while time.time() < deadline:
            state = self.read_state(field_id)
            if state["game_seconds"] >= game_seconds:
                return state
            time.sleep(0.2)
        return None

    def judge_leash(self, field, initial, fight_state, add, record):
        record["attempt_before"] = fight_state["attempt"]
        center = fight_state["center"]
        checkpoint = fight_state["checkpoint"]
        distance = planar_distance(center, checkpoint)
        far = field["radius_cm"] + LEASH_FAR_OUTSET_CM
        far_xy = [center[0] + (checkpoint[0] - center[0]) / distance * far, center[1] + (checkpoint[1] - center[1]) / distance * far]
        self.move_pawn(far_xy[0], far_xy[1], 0.0)
        time.sleep(self.placement.LEASH_RESET_SECONDS + LEASH_EXTRA_WAIT_SECONDS)
        after = self.read_state(field["id"])
        near = [monster for monster in after["monsters"] if planar_distance(monster["location"], center) <= field["radius_cm"] + LEASH_CLEAR_RADIUS_CM]
        record["after_leave"] = {"state": after["state"], "alive": after["alive"], "monsters_near": len(near), "attempt": after["attempt"]}
        add("leash_reset_idle", after["state"] == "idle" and after["alive"] == 0, f"state={after['state']} alive={after['alive']}")
        add("leash_no_monsters_near", len(near) == 0, f"near={len(near)}")
        again = self.enter_and_judge(field, after, add, "reenter", record.setdefault("reenter", {}))
        attempt_after = again["record"]["after_walk"]["attempt"]
        add("leash_attempt_incremented", attempt_after == record["attempt_before"] + 1, f"before={record['attempt_before']} after={attempt_after}")

    def judge_victory(self, field, add, record):
        record["rounds"] = 0
        limit = len(field["waves"]) + KILL_EXTRA_ROUNDS
        state = self.read_state(field["id"])
        for round_index in range(limit):
            if state["state"] == "victory":
                break
            self.call_reader({"action": "kill_wave", "field_id": field["id"]})
            time.sleep(KILL_WAIT_SECONDS)
            state = self.read_state(field["id"])
            record["rounds"] = round_index + 1
        record["final_state"] = state["state"]
        add("victory", state["state"] == "victory", f"state={state['state']} rounds={record['rounds']}/{limit}")

    def read_log_since(self, offset):
        if offset is None:
            return None
        try:
            with open(os.path.join(self.root, *LOG_FILE_PARTS), "rb") as handle:
                handle.seek(offset)
                return handle.read().decode("utf-8", errors="replace")
        except OSError:
            return None

    def log_size(self):
        try:
            return os.path.getsize(os.path.join(self.root, *LOG_FILE_PARTS))
        except OSError:
            return None

    def poll_new_player(self, dead_pawn_name, give_up_game_seconds):
        deadline = time.time() + RESPAWN_POLL_REAL_TIMEOUT_SECONDS
        while time.time() < deadline:
            player = self.call_reader({"action": "player"})
            if player["has_pawn"] and player["pawn_name"] != dead_pawn_name:
                return player
            if player["game_seconds"] > give_up_game_seconds:
                return None
            time.sleep(0.3)
        return None

    def judge_kill(self, killed, add, record):
        outcome = classify_kill(killed)
        if outcome != "already_dead":
            life = killed.get("life_before") or {}
            add("death_damage_applied", outcome == "applied", f"applied={killed.get('applied')} health_before={life.get('health')} dead_before={life.get('dead')}")
            return []
        record["death_damage_applied"] = "already_dead"
        recorder = self.read_recorder(True, record)
        events = [] if recorder is None else recorder.get("player_events", [])
        record["player_events"] = events
        preview = choose_death_reference(killed, events, None, 0.0)
        add("death_observed", True, f"already_dead observed_s={preview['observed_s']:.3f} source={preview['source']} kill_request_s={killed['game_seconds']:.3f}")
        return events

    def judge_death_and_respawn(self, field, add, record):
        before = self.read_state(field["id"])
        checkpoint = before["checkpoint"]
        center = before["center"]
        kill_log_offset = self.log_size()
        killed = self.call_reader({"action": "kill_player", "field_id": field["id"]})
        record["kill"] = killed
        events = self.judge_kill(killed, add, record)
        delay = killed.get("respawn_delay") or RESPAWN_FALLBACK_DELAY_SECONDS
        limit = delay + RESPAWN_EXTRA_SECONDS
        give_up = choose_death_reference(killed, events, None, delay)["reference_s"] + limit + RESPAWN_POLL_GRACE_SECONDS
        player = self.poll_new_player(killed["pawn_name"], give_up)
        record["respawned_player"] = player
        spawn_seconds = None if player is None else player["game_seconds"] - (player["age_seconds"] or 0.0)
        death_time = choose_death_reference(killed, events, spawn_seconds, delay)
        record["death_time"] = death_time
        if player is None:
            add("respawn_pawn_present", False, f"no new pawn within {limit:.1f}s of game time (dead={killed['pawn_name']} reference_s={death_time['reference_s']:.3f})")
            return None
        elapsed = spawn_seconds - death_time["reference_s"]
        is_death_time_measured = classify_kill(killed) != "already_dead" or death_time["source"] == "recorder"
        validity = "valid" if is_death_time_measured else "measurement_invalid(death tick not observed)"
        add("respawn_pawn_present", is_death_time_measured and elapsed <= limit, f"elapsed={elapsed:.2f}s limit={limit:.1f}s reference={death_time['reference_from']} source={death_time['source']} {validity} pawn={player['pawn_name']}")
        near = planar_distance(player["pawn"], checkpoint)
        add("respawn_near_checkpoint", near <= RESPAWN_NEAR_CHECKPOINT_CM, f"planar_cm={near:.1f} limit_cm={RESPAWN_NEAR_CHECKPOINT_CM:g}")
        record["moves"] = self.walk_after_respawn(player, center, add)
        log_offset = kill_log_offset if classify_kill(killed) != "already_dead" else self.log_offset_at_or_before(death_time["reference_s"])
        text = self.read_log_since(log_offset)
        failures = None if text is None else text.count(SPAWN_FAILURE_MARKER)
        fallbacks = None if text is None else sum(text.count(marker) for marker in FALLBACK_MARKERS)
        record["log_counts"] = {"spawn_failed": failures, "fallback_warnings": fallbacks, "log_offset": log_offset}
        add("respawn_no_spawn_failure", failures == 0, f"spawn_failed_lines={failures} fallback_warning_lines={fallbacks}")
        idle = self.poll_until(field["id"], ("idle",), POSTDEATH_IDLE_SECONDS)
        add("postdeath_reset_idle", idle is not None and idle["alive"] == 0, "state not idle in time" if idle is None else f"state={idle['state']} alive={idle['alive']}")
        return idle

    def walk_after_respawn(self, player, center, add):
        start = player["pawn"]
        distance = planar_distance(start, center)
        target = [start[0] + (center[0] - start[0]) / distance * RESPAWN_WALK_DISTANCE_CM, start[1] + (center[1] - start[1]) / distance * RESPAWN_WALK_DISTANCE_CM]
        walked = self.spots.call_probe({"action": "walk", "path": [target], "seconds": RESPAWN_WALK_LIMIT_SECONDS, "waypoint_radius_cm": WALK_RADIUS_CM, "hitch_ms": 1000}, result_timeout_seconds=RESPAWN_WALK_LIMIT_SECONDS + 90.0)
        after = self.call_reader({"action": "player"})
        displacement = planar_distance(start, after["pawn"]) if after["has_pawn"] else None
        add("respawn_moves_on_input", displacement is not None and displacement >= RESPAWN_MIN_DISPLACEMENT_CM, f"displacement_cm={displacement} min_cm={RESPAWN_MIN_DISPLACEMENT_CM:g}")
        return {"target": target, "reached": bool(walked.get("reached")), "displacement_cm": displacement}


def check_recorder_and_kill(checker, field_id, expect):
    token = SELF_CHECK_RUN_PREFIX + uuid.uuid4().hex[:8]
    started = checker.call_reader({"action": "recorder_start", "field_id": field_id, "token": token})
    expect("recorder_start", started.get("registered") is True, f"center={started.get('center')} radius_cm={started.get('radius_cm')} replaced={started.get('previous')}")
    time.sleep(SELF_CHECK_SAMPLE_SECONDS)
    sampled = checker.call_reader({"action": "recorder_read", "token": token, "since": 0, "events_only": False})
    rows = sampled.get("rows", [])
    increasing = all(later["gt"] > earlier["gt"] for earlier, later in zip(rows, rows[1:]))
    expect("recorder_samples", len(rows) >= SELF_CHECK_MIN_ROWS and increasing and sampled.get("registered") and sampled.get("error") is None, f"rows={len(rows)} increasing={increasing} registered={sampled.get('registered')} error={sampled.get('error')}")
    last = rows[-1] if rows else {}
    expect("recorder_player_alive", last.get("pn") is not None and last.get("pd") is False and (last.get("hp") or 0.0) > 0.0, f"pawn={last.get('pn')} dead={last.get('pd')} health={last.get('hp')} state={last.get('st')} monsters={len(last.get('m') or [])}")
    life = checker.call_reader({"action": "player_life"})
    expect("player_life_api", life.get("has_combat_component") and life.get("controller_pawn_reader") is not None and life.get("dead") is False, f"controller_pawn_reader={life.get('controller_pawn_reader')} health={life.get('health')} is_alive={life.get('is_alive')}")
    first_kill = checker.call_reader({"action": "kill_player"})
    after = first_kill.get("life_after") or {}
    expect("kill_alive_branch", classify_kill(first_kill) == "applied" and after.get("dead") is True, f"applied={first_kill.get('applied')} life_after_dead={after.get('dead')} health_after={after.get('health')}")
    second_kill = checker.call_reader({"action": "kill_player"})
    expect("kill_already_dead_branch", classify_kill(second_kill) == "already_dead", f"applied={second_kill.get('applied')} dead_before={(second_kill.get('life_before') or {}).get('dead')}")
    time.sleep(SELF_CHECK_EVENT_WAIT_SECONDS)
    events = checker.call_reader({"action": "recorder_read", "token": token, "since": 0, "events_only": True}).get("player_events", [])
    observed = find_observed_death(events, second_kill["game_seconds"])
    gaps = [later["gt"] - earlier["gt"] for earlier, later in zip(rows, rows[1:])]
    tolerance = max([SPAWN_REFERENCE_MAX_AGE_SECONDS] + gaps)
    gap = None if observed is None else observed - first_kill["game_seconds"]
    expect("recorder_death_frame", gap is not None and -TIME_EPSILON_SECONDS <= gap <= tolerance + TIME_EPSILON_SECONDS, f"observed_s={observed} kill_s={first_kill['game_seconds']} gap_s={gap} tolerance_s={tolerance:.3f}")
    delay = first_kill.get("respawn_delay") or RESPAWN_FALLBACK_DELAY_SECONDS
    player = checker.poll_new_player(first_kill.get("pawn_name"), first_kill["game_seconds"] + delay + RESPAWN_EXTRA_SECONDS + RESPAWN_POLL_GRACE_SECONDS)
    expect("respawn_after_kill", player is not None, f"new_pawn={None if player is None else player.get('pawn_name')}")
    stopped = checker.call_reader({"action": "recorder_stop", "token": token})
    status = checker.call_reader({"action": "recorder_status"})
    summary = stopped.get("recorder") or {}
    expect("recorder_stop_releases", stopped.get("present") and summary.get("registered") is False and stopped.get("token_matched") and not status.get("present"), f"stopped={summary.get('stopped')} rows={summary.get('row_count')} tick_error={summary.get('error')} state_present_after={status.get('present')}")


def wait_for_recorder_release(checker):
    deadline = time.time() + SELF_CHECK_RELEASE_TIMEOUT_SECONDS
    while True:
        status = checker.call_reader({"action": "recorder_status"})
        if not (status.get("recorder") or {}).get("registered") or time.time() > deadline:
            return status
        time.sleep(SELF_CHECK_RELEASE_POLL_SECONDS)


def run_self_check(checker, spots_module, run_directory, run_name, field_id):
    items = []

    def expect(name, ok, detail):
        items.append({"name": name, "ok": bool(ok), "detail": detail})
        log(f"  {'PASS' if ok else 'FAIL'} {name}: {detail}")

    try:
        spots_module.start_pie(PIE_WARMUP_SECONDS)
    except spots_module.PieStartError as error:
        log(f"PIE 시작 실패: {error}")
        return 2
    pie_running = True
    try:
        check_recorder_and_kill(checker, field_id, expect)
        checker.call_reader({"action": "recorder_start", "field_id": field_id, "token": SELF_CHECK_RUN_PREFIX + uuid.uuid4().hex[:8]})
        time.sleep(SELF_CHECK_EVENT_WAIT_SECONDS)
        pie_running = False
        spots_module.stop_pie()
        status = wait_for_recorder_release(checker)
        recorder = status.get("recorder") or {}
        expect("recorder_released_on_pie_end", status.get("present") and recorder.get("registered") is False and recorder.get("stopped") == "pie_ended", f"present={status.get('present')} registered={recorder.get('registered')} stopped={recorder.get('stopped')} error={recorder.get('error')}")
        checker.call_reader({"action": "recorder_stop"})
        status = checker.call_reader({"action": "recorder_status"})
        expect("recorder_state_removed", not status.get("present"), f"present={status.get('present')}")
    except Exception as error:
        expect("exception", False, repr(error))
    finally:
        if pie_running:
            checker.release_recorder_quietly()
            write_self_check(spots_module, run_directory, run_name, field_id, items, False)
            spots_module.stop_pie()
    passed = write_self_check(spots_module, run_directory, run_name, field_id, items, True)
    log(f"{'PASS' if passed else 'FAIL'} self-check {run_directory}")
    return 0 if passed else 1


def write_self_check(spots_module, run_directory, run_name, field_id, items, complete):
    passed = complete and bool(items) and all(item["ok"] for item in items)
    try:
        spots_module.write_json(os.path.join(run_directory, "selfcheck.json"), {"run": run_name, "field_id": field_id, "complete": complete, "passed": passed, "items": items})
    except Exception as error:
        log(f"  WARN self-check write failed: {error!r}")
    return passed


def select_fields(placement_module, only_text):
    if not only_text:
        return list(placement_module.FIELDS)
    wanted = {("Field" + token.strip()) if not token.strip().startswith("Field") else token.strip() for token in only_text.split(",") if token.strip()}
    return [field for field in placement_module.FIELDS if field["id"] in wanted]


def load_placement(root):
    path = os.path.join(root, *PIE_DIRECTORY_PARTS, "placement.json")
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def render_summary(run_name, records, complete, passed):
    lines = [f"# 공터 조우 PIE 확인 {run_name}", "", f"complete={complete} passed={passed}", "", "| 공터 | 확인 | 결과 | 값 |", "|---|---|---|---|"]
    for record in records:
        for check in record["checks"]:
            lines.append(f"| {record['id']} | {check['name']} | {'통과' if check['ok'] else '실패'} | {check['detail']} |")
    return "\n".join(lines) + "\n"


def write_results(spots_module, run_directory, run_name, records, complete):
    passed = complete and bool(records) and all(check["ok"] for record in records for check in record["checks"])
    payload = {"run": run_name, "complete": complete, "passed": passed, "written_at": time.strftime("%Y-%m-%d %H:%M:%S"), "fields": records}
    try:
        spots_module.write_json(os.path.join(run_directory, "result.json"), payload)
        spots_module.write_text(os.path.join(run_directory, "summary.md"), render_summary(run_name, records, complete, passed))
    except Exception as error:
        log(f"  WARN result write failed: {error!r}")
    return passed


def print_plan(fields, placement):
    by_id = {item["id"]: item for item in placement["fields"]}
    for field in fields:
        item = by_id.get(field["id"])
        if item is None:
            print(f"{field['id']}: placement.json에 없음")
            continue
        target = walk_target_xy(field, item["center_cm"], item["checkpoint_cm"])
        print(json.dumps({"id": field["id"], "checkpoint_cm": item["checkpoint_cm"], "walk_target_cm": [round(target[0], 1), round(target[1], 1)], "wave1_count": wave_counts(field)[0], "wave_counts": wave_counts(field), "start_radius_cm": item["start_radius_cm"], "leash_radius_cm": item["leash_radius_cm"]}, ensure_ascii=False))


def parse_arguments(argv):
    parser = argparse.ArgumentParser(description="공터 조우 PIE 확인(시작·스폰·끼임·추적·이탈 리셋·사망 부활·승리).")
    parser.add_argument("--run", default=None, help="결과 폴더 이름(기본 run-<시각>, --self-check는 selfcheck-<시각>)")
    parser.add_argument("--only", default=None, help="West,North,East 중 일부")
    parser.add_argument("--skip-leash", action="store_true", help="서쪽 이탈 리셋 확인을 건너뛴다")
    parser.add_argument("--overwrite", action="store_true", help="이미 있는 결과 폴더를 덮어쓴다")
    parser.add_argument("--dry-run", action="store_true", help="PIE 없이 placement.json으로 계획만 출력한다")
    parser.add_argument("--self-check", action="store_true", help="PIE에서 기록기 등록·표본·해제, 생존 읽기, 처치 두 분기만 확인한다(판정 아님)")
    return parser.parse_args(argv)


def main(argv):
    options = parse_arguments(argv)
    placement_module, spots_module = load_modules()
    root = spots_module.ROOT
    fields = select_fields(placement_module, options.only)
    if not fields:
        log("선택된 공터가 없음")
        return 1
    placement = load_placement(root)
    if placement is None or not placement.get("ok") or placement.get("dry_run"):
        log("placement.json이 없거나 실제 배치 결과가 아님: editor_place_field_encounters.py를 먼저 실행")
        return 1
    if options.dry_run:
        print_plan(fields, placement)
        return 0
    prefix = SELF_CHECK_RUN_PREFIX if options.self_check else RUN_DEFAULT_PREFIX
    run_name = options.run or (prefix + time.strftime("%Y%m%d-%H%M%S"))
    run_directory = os.path.join(root, *PIE_DIRECTORY_PARTS, run_name)
    if os.path.exists(run_directory) and not options.overwrite:
        log(f"이미 있는 결과 폴더(--overwrite 필요): {run_directory}")
        return 1
    os.makedirs(run_directory, exist_ok=True)
    checker = Checker(placement_module, spots_module, root, run_directory)
    if options.self_check:
        return run_self_check(checker, spots_module, run_directory, run_name, fields[0]["id"])
    records = []
    try:
        spots_module.start_pie(PIE_WARMUP_SECONDS)
    except spots_module.PieStartError as error:
        log(f"PIE 시작 실패: {error}")
        return 2
    try:
        for field in fields:
            log(field["id"])
            record = {"id": field["id"], "checks": []}
            records.append(record)
            try:
                checker.check_field(field, options.skip_leash, record)
            except Exception as error:
                log(f"  FAIL exception: {error!r}")
                record["checks"].append({"name": "exception", "ok": False, "detail": repr(error)})
            write_results(spots_module, run_directory, run_name, records, False)
    finally:
        checker.release_recorder_quietly()
        write_results(spots_module, run_directory, run_name, records, False)
        spots_module.stop_pie()
    passed = write_results(spots_module, run_directory, run_name, records, True)
    log(f"{'PASS' if passed else 'FAIL'} {run_directory}")
    return 0 if passed else 1


if unreal is None and __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
