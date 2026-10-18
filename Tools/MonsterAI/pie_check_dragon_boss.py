"""실행 환경: 시스템 Python 3.12(언리얼 에디터가 TDGame을 열고 원격 실행이 켜진 상태, 에디터 앞창 유지 L-editor-06). 맵을 저장하지 않는다.
실행 명령: python Tools/MonsterAI/pie_check_dragon_boss.py [--map /Game/Combat/Maps/LV_TDMegaMagicArena] [--distance 900] [--level 30] [--out <프로젝트 안 폴더>] (PowerShell, L-repo-01)
출력: Docs/MonsterAI_CombatSim/measurements/M3-35/pie-<날짜>/ 의 report.json(단계별 통과·시각, 페이즈 전이, 비행 구간, 졸개·위험 엔티티 최대, 표본)과 {1_ground,2_sky_minions,3_sky_attack,4_landed,5_enraged,6_inferno,7_death}.png. 종료 코드 0 전 단계 통과 / 1 일부 실패 / 2 환경 문제
상태: 현행(M3-35). MountainDragon 한 마리를 TDSpawnMonster로 부르고 체력을 66·38·24%로 깎아 페이즈 1→2→3(공중·졸개, 지상 격노, 업화)과 처치를 관측한다. 체력은 TDCombatLibrary.TryApplyDamage로 깎고, 엔티티 정의 이름은 TDDamageEntity의 definition 프로퍼티로 읽으며 못 읽으면 위험 판정을 생략한다
"""
import argparse
import json
import shutil
import subprocess
import sys
import time
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUN_IN_EDITOR = ROOT / "Tools" / "run_in_editor.py"
SHOT_DIRECTORY = ROOT / "Saved" / "Screenshots" / "WindowsEditor"
EVIDENCE_DIRECTORY = ROOT / "Docs" / "MonsterAI_CombatSim" / "measurements" / "M3-35" / f"pie-{date.today().strftime('%Y%m%d')}"
RESULT_MARKER = "TD_DRAGON_PIE"
DRAGON_SPECIES = "MountainDragon"
DRAGON_SPECIES_ASSET = f"DA_TDMonster_{DRAGON_SPECIES}"
MINION_DEFINITION = "Goblin_Melee"
INFERNO_DEFINITION = "DA_TDDragonInferno"
DANGER_DEFINITIONS = frozenset({"DA_TDDragonFlameBreath", "DA_TDDragonGrabThrow", "DA_TDDragonFireRain", "DA_TDDragonDiveBreath", "DA_TDDragonInferno"})
REQUIRED_STAGES = ("spawn", "ground", "sky", "minions", "landing", "enraged", "inferno", "death")
SHOT_NAMES = ("1_ground", "2_sky_minions", "3_sky_attack", "4_landed", "5_enraged", "6_inferno", "7_death")
PIE_START_WAIT_SECONDS = 120.0
SPAWN_WAIT_SECONDS = 15.0
CAPTURE_FILE_WAIT_SECONDS = 25.0
SAMPLE_INTERVAL_SECONDS = 0.5
GROUND_OBSERVE_SECONDS = 12.0
SKY_WAIT_SECONDS = 25.0
MINION_KILL_DELAY_SECONDS = 8.0
MINION_CLEAR_WAIT_SECONDS = 10.0
LANDING_WAIT_SECONDS = 40.0
SKY_ATTACK_FALLBACK_SECONDS = 6.0
LANDED_SETTLE_SECONDS = 0.5
ENRAGE_WAIT_SECONDS = 6.0
INFERNO_WAIT_SECONDS = 6.0
DEATH_WAIT_SECONDS = 10.0
DEATH_SHOT_DELAY_SECONDS = 3.0
SKY_HEALTH_FRACTION = 0.66
ENRAGED_HEALTH_FRACTION = 0.38
INFERNO_HEALTH_FRACTION = 0.24
KILL_HEALTH_FRACTION = -1.0
HEALTH_TOLERANCE_FRACTION = 0.002
LOWER_HEALTH_ATTEMPTS = 12
LOWER_HEALTH_STEPS = 6
EXPECTED_MINIONS = 3
PLAYER_HEAL_FRACTION = 0.3
UNSUPPORTED = "미지원"
EXIT_FAILED = 1
EXIT_ENVIRONMENT = 2

PRELUDE = '''import unreal, json
def _td_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
def _td_monsters(world):
    return [actor for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDMonsterCharacter)]
def _td_combat(actor):
    return actor.get_component_by_class(unreal.TDCombatComponent)
def _td_print(payload):
    print('{marker}', json.dumps(payload))
'''

START_CODE = '''def _td_start():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if editor.get_game_world() is None:
        if not editor.get_editor_world().get_path_name().startswith('{level}'):
            levels.load_level('{level}')
        levels.editor_request_begin_play()
    _td_print({{'requested': True}})
_td_start()
del _td_start
'''

STATUS_CODE = '''def _td_status():
    world = _td_world()
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
    _td_print({'pie': world is not None, 'pawn': pawn is not None})
_td_status()
del _td_status
'''

SHOT_CODE = '''def _td_shot():
    world = _td_world()
    unreal.SystemLibrary.execute_console_command(world, 'Shot', unreal.GameplayStatics.get_player_controller(world, 0))
_td_shot()
del _td_shot
'''

STOP_CODE = '''def _td_stop():
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    _td_print({'stopped': True})
_td_stop()
del _td_stop
'''

CLEAN_CODE = '''PLAYER_TEST_MAX_HEALTH = 1000.0
def _td_clean():
    world = _td_world()
    for actor in _td_monsters(world):
        actor.destroy_actor()
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDDamageEntity):
        actor.destroy_actor()
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    combat = _td_combat(pawn) if pawn else None
    if combat:
        stats = combat.get_stats()
        stats.set_editor_property('base_max_health', PLAYER_TEST_MAX_HEALTH)
        combat.set_stats(stats, True)
    _td_print({'cleaned': True})
_td_clean()
del _td_clean
'''

CONSOLE_CODE = '''def _td_console():
    world = _td_world()
    unreal.SystemLibrary.execute_console_command(world, '{command}', unreal.GameplayStatics.get_player_controller(world, 0))
    _td_print({{'executed': '{command}'}})
_td_console()
del _td_console
'''

HEAL_CODE = '''PLAYER_TEST_MAX_HEALTH = 1000.0
def _td_heal():
    world = _td_world()
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    combat = _td_combat(pawn) if pawn else None
    if combat:
        stats = combat.get_stats()
        stats.set_editor_property('base_max_health', PLAYER_TEST_MAX_HEALTH)
        combat.set_stats(stats, True)
    _td_print({'healed': combat is not None})
_td_heal()
del _td_heal
'''

DRAGON_HELPERS = '''def _td_definition_id(actor):
    species = actor.get_species()
    if species is None:
        return ''
    try:
        return str(species.get_editor_property('definition_id'))
    except Exception:
        return ''
def _td_entity_definition(entity):
    try:
        definition = entity.get_editor_property('definition')
    except Exception:
        return None
    return definition.get_name() if definition else None
'''

DRAGON_HELPERS_CLEANUP = '''del _td_definition_id
del _td_entity_definition
'''

SAMPLE_CODE = '''def _td_sample(dragon_asset, minion_definition):
    world = _td_world()
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    player = _td_combat(pawn) if pawn else None
    dragon = None
    minions = 0
    for actor in _td_monsters(world):
        species = actor.get_species()
        combat = _td_combat(actor)
        if species is None or combat is None:
            continue
        if species.get_name() == dragon_asset:
            location = actor.get_actor_location()
            dragon = {'alive': combat.is_alive(), 'phase': actor.get_monster_phase_index(), 'flying': actor.is_monster_flying(),
                      'health': combat.get_current_health(), 'max_health': combat.get_max_health(), 'z': location.z}
        elif combat.is_alive() and _td_definition_id(actor) == minion_definition:
            minions += 1
    entities = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDDamageEntity)
    by_definition = {}
    for entity in entities:
        name = _td_entity_definition(entity)
        if name:
            by_definition[name] = by_definition.get(name, 0) + 1
    _td_print({'dragon': dragon, 'minions': minions,
               'player': {'health': player.get_current_health() if player else None, 'max_health': player.get_max_health() if player else None},
               'entities': {'total': len(entities), 'by_definition': by_definition}})
'''

LOWER_CODE = '''def _td_lower_health(dragon_asset, target_fraction, steps, tolerance_fraction):
    world = _td_world()
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    for actor in _td_monsters(world):
        species = actor.get_species()
        combat = _td_combat(actor)
        if species is None or combat is None or species.get_name() != dragon_asset:
            continue
        target_health = combat.get_max_health() * target_fraction
        tolerance = combat.get_max_health() * tolerance_fraction
        efficiency = 1.0
        for _ in range(steps):
            gap = combat.get_current_health() - target_health
            if not combat.is_alive() or gap <= tolerance:
                break
            amount = gap / efficiency
            spec = unreal.TDDamageSpec()
            spec.set_editor_property('amount', amount)
            spec.set_editor_property('instigator', pawn)
            spec.set_editor_property('causer', pawn)
            applied = unreal.TDCombatLibrary.try_apply_damage(actor, spec)
            if applied <= 0.0:
                break
            efficiency = min(1.0, applied / amount)
        health = combat.get_current_health()
        _td_print({'missing': False, 'alive': combat.is_alive(), 'health': health, 'max_health': combat.get_max_health(),
                   'reached': (not combat.is_alive()) or health <= target_health + tolerance})
        return
    _td_print({'missing': True})
'''

KILL_MINIONS_CODE = '''def _td_kill_minions(minion_definition):
    world = _td_world()
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    killed = 0
    for actor in _td_monsters(world):
        combat = _td_combat(actor)
        if combat is None or not combat.is_alive() or _td_definition_id(actor) != minion_definition:
            continue
        spec = unreal.TDDamageSpec()
        spec.set_editor_property('amount', combat.get_max_health() * 10.0)
        spec.set_editor_property('instigator', pawn)
        spec.set_editor_property('causer', pawn)
        unreal.TDCombatLibrary.try_apply_damage(actor, spec)
        killed += 1
    _td_print({'killed': killed})
'''


def run_editor_code(body):
    code = PRELUDE.format(marker=RESULT_MARKER) + body
    completed = subprocess.run([sys.executable, str(RUN_IN_EDITOR), "-c", code], capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=ROOT)
    for line in completed.stdout.splitlines():
        index = line.find(RESULT_MARKER)
        if index >= 0:
            return json.loads(line[index + len(RESULT_MARKER):])
    return {"error": completed.stdout[-600:] + completed.stderr[-300:]}


def wait_until(predicate, timeout_seconds, poll_seconds=0.4):
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(poll_seconds)
    return None


def capture_screenshot(name):
    EVIDENCE_DIRECTORY.mkdir(parents=True, exist_ok=True)
    target = EVIDENCE_DIRECTORY / f"{name}.png"
    target.unlink(missing_ok=True)
    before = {path.name for path in SHOT_DIRECTORY.glob("ScreenShot*.png")} if SHOT_DIRECTORY.exists() else set()
    run_editor_code(SHOT_CODE)
    deadline = time.monotonic() + CAPTURE_FILE_WAIT_SECONDS
    while time.monotonic() < deadline:
        fresh = [path for path in SHOT_DIRECTORY.glob("ScreenShot*.png") if path.name not in before] if SHOT_DIRECTORY.exists() else []
        if fresh and fresh[0].stat().st_size > 0:
            time.sleep(0.5)
            shutil.move(str(fresh[0]), str(target))
            return target.relative_to(ROOT).as_posix()
        time.sleep(0.5)
    return None


def run_dragon_code(body):
    return run_editor_code(DRAGON_HELPERS + body + DRAGON_HELPERS_CLEANUP)


def new_run():
    return {"started": time.monotonic(), "samples": [], "stages": {}, "shots": {}, "phase_first_seen": {}, "phase_transitions": [],
            "last_phase": None, "flight_segments": [], "ground_z": None, "minions_max": 0, "danger_max": 0, "entity_total_seen": 0,
            "entity_named_seen": 0, "definition_observations": {}, "player_min_health": None, "player_max_health": None,
            "player_heal_count": 0, "sample_errors": 0, "error": None}


def elapsed_seconds(run):
    return round(time.monotonic() - run["started"], 2)


def build_sample(run, payload):
    dragon = payload["dragon"]
    definitions = payload["entities"]["by_definition"]
    return {"t": elapsed_seconds(run), "dragon_found": dragon is not None, "alive": bool(dragon and dragon["alive"]),
            "phase": dragon["phase"] if dragon else None, "flying": dragon["flying"] if dragon else None,
            "health_ratio": round(dragon["health"] / dragon["max_health"], 3) if dragon and dragon["max_health"] else None,
            "z": round(dragon["z"], 1) if dragon else None, "player_health": payload["player"]["health"],
            "player_max_health": payload["player"]["max_health"], "minions": payload["minions"],
            "entity_total": payload["entities"]["total"], "definitions": definitions,
            "danger": sum(count for name, count in definitions.items() if name in DANGER_DEFINITIONS)}


def track_player(run, sample):
    health, max_health = sample["player_health"], sample["player_max_health"]
    if health is None or not max_health:
        return
    if run["player_min_health"] is None or health < run["player_min_health"]:
        run["player_min_health"] = health
        run["player_max_health"] = max_health


def track_flight(run, sample):
    segments = run["flight_segments"]
    is_open = bool(segments) and segments[-1]["end"] is None
    if not sample["flying"]:
        if is_open:
            segments[-1]["end"] = sample["t"]
        return
    if not is_open:
        segments.append({"start": sample["t"], "end": None, "max_altitude_cm": 0.0})
    segments[-1]["max_altitude_cm"] = round(max(segments[-1]["max_altitude_cm"], sample["z"] - run["ground_z"]), 1)


def track_dragon(run, sample):
    phase = sample["phase"]
    run["phase_first_seen"].setdefault(phase, sample["t"])
    previous = run["last_phase"]
    if previous is not None and phase != previous:
        run["phase_transitions"].append({"from": previous, "to": phase, "t": sample["t"]})
    run["last_phase"] = phase
    if run["ground_z"] is None:
        run["ground_z"] = sample["z"]
    track_flight(run, sample)


def record_sample(run, sample):
    run["samples"].append(sample)
    run["minions_max"] = max(run["minions_max"], sample["minions"])
    run["danger_max"] = max(run["danger_max"], sample["danger"])
    run["entity_total_seen"] += sample["entity_total"]
    run["entity_named_seen"] += sum(sample["definitions"].values())
    for name, count in sample["definitions"].items():
        entry = run["definition_observations"].setdefault(name, {"samples": 0, "max_concurrent": 0})
        entry["samples"] += 1
        entry["max_concurrent"] = max(entry["max_concurrent"], count)
    track_player(run, sample)
    if sample["alive"]:
        track_dragon(run, sample)


def heal_player_if_low(run, sample):
    health, max_health = sample["player_health"], sample["player_max_health"]
    if health is None or not max_health or health >= max_health * PLAYER_HEAL_FRACTION:
        return
    run_editor_code(HEAL_CODE)
    run["player_heal_count"] += 1


def observe(run):
    payload = run_dragon_code(SAMPLE_CODE + f"_td_sample({DRAGON_SPECIES_ASSET!r}, {MINION_DEFINITION!r})\ndel _td_sample\n")
    if "error" in payload:
        run["sample_errors"] += 1
        return None
    sample = build_sample(run, payload)
    record_sample(run, sample)
    heal_player_if_low(run, sample)
    return sample


def observe_until(run, predicate, timeout_seconds, on_sample=None):
    deadline = time.monotonic() + timeout_seconds
    while True:
        loop_started = time.monotonic()
        sample = observe(run)
        if sample:
            if on_sample:
                on_sample(sample)
            if predicate(sample):
                return sample
        if time.monotonic() >= deadline:
            return None
        time.sleep(max(0.0, SAMPLE_INTERVAL_SECONDS - (time.monotonic() - loop_started)))


def never(sample):
    return False


def take_shot(run, name):
    if name not in run["shots"]:
        run["shots"][name] = capture_screenshot(name)
    return run["shots"][name]


def shot_when(run, name, condition):
    def on_sample(sample):
        if condition(sample):
            take_shot(run, name)
    return on_sample


def samples_since(run, start):
    return [sample for sample in run["samples"] if sample["t"] >= start]


def finish_stage(run, name, start, passed, **details):
    run["stages"][name] = {"passed": bool(passed), "start": start, "end": elapsed_seconds(run), **details}
    print(json.dumps({"stage": name, **run["stages"][name]}, ensure_ascii=False), flush=True)


def definitions_supported(run):
    return run["entity_total_seen"] == 0 or run["entity_named_seen"] > 0


def is_flying_with_minions(sample):
    return bool(sample["flying"]) and sample["minions"] >= 1


def sky_reached(run, start):
    window = [sample for sample in samples_since(run, start) if sample["alive"]]
    return (any(sample["phase"] >= 1 for sample in window)
            and max((sample["minions"] for sample in window), default=0) >= EXPECTED_MINIONS
            and any(sample["flying"] for sample in window))


def has_landed(run):
    return any(segment["end"] is not None for segment in run["flight_segments"])


def is_phase_reached(sample, phase):
    return sample["alive"] and sample["phase"] >= phase


def inferno_reached(run, start):
    window = [sample for sample in samples_since(run, start) if sample["alive"]]
    if not any(sample["phase"] >= 3 for sample in window):
        return False
    return not definitions_supported(run) or any(INFERNO_DEFINITION in sample["definitions"] for sample in window)


def lower_dragon_health(run, target_fraction):
    result = {"missing": True}
    for _ in range(LOWER_HEALTH_ATTEMPTS):
        call = f"_td_lower_health({DRAGON_SPECIES_ASSET!r}, {target_fraction!r}, {LOWER_HEALTH_STEPS}, {HEALTH_TOLERANCE_FRACTION!r})\ndel _td_lower_health\n"
        result = run_dragon_code(LOWER_CODE + call)
        if result.get("reached") or result.get("missing"):
            break
        observe(run)
        time.sleep(SAMPLE_INTERVAL_SECONDS)
    return result


def run_spawn_stage(run):
    start = elapsed_seconds(run)
    spawned = observe_until(run, lambda sample: sample["dragon_found"], SPAWN_WAIT_SECONDS)
    finish_stage(run, "spawn", start, spawned)
    return spawned is not None


def run_ground_stage(run):
    start = elapsed_seconds(run)
    observe_until(run, never, GROUND_OBSERVE_SECONDS, shot_when(run, "1_ground", lambda sample: sample["entity_total"] > 0))
    take_shot(run, "1_ground")
    window = samples_since(run, start)
    entity_peak = max((sample["entity_total"] for sample in window), default=0)
    finish_stage(run, "ground", start, entity_peak >= 1, entity_peak=entity_peak, phases=sorted({sample["phase"] for sample in window if sample["alive"]}))


def run_sky_stage(run):
    start = elapsed_seconds(run)
    lowered = lower_dragon_health(run, SKY_HEALTH_FRACTION)
    observe_until(run, lambda sample: sky_reached(run, start), SKY_WAIT_SECONDS, shot_when(run, "2_sky_minions", is_flying_with_minions))
    window = [sample for sample in samples_since(run, start) if sample["alive"]]
    flight_start = next((sample["t"] for sample in window if sample["flying"]), None)
    finish_stage(run, "sky", start, sky_reached(run, start), lowered_health=lowered.get("health"), flight_start=flight_start,
                 max_phase=max((sample["phase"] for sample in window), default=None),
                 minions_peak=max((sample["minions"] for sample in window), default=0))


def run_minion_stage(run):
    start = elapsed_seconds(run)
    flight_start = run["stages"]["sky"]["flight_start"]
    remaining = (flight_start + MINION_KILL_DELAY_SECONDS - start) if flight_start is not None else 0.0
    if remaining > 0:
        observe_until(run, never, remaining, shot_when(run, "2_sky_minions", is_flying_with_minions))
    killed = run_dragon_code(KILL_MINIONS_CODE + f"_td_kill_minions({MINION_DEFINITION!r})\ndel _td_kill_minions\n").get("killed")
    cleared = observe_until(run, lambda sample: sample["minions"] == 0, MINION_CLEAR_WAIT_SECONDS)
    finish_stage(run, "minions", start, cleared, killed=killed)


def run_landing_stage(run):
    start = elapsed_seconds(run)
    if not run["flight_segments"]:
        finish_stage(run, "landing", start, False, reason="flight not observed")
        return
    clear_time = run["stages"]["minions"]["end"]
    on_sample = shot_when(run, "3_sky_attack", lambda sample: bool(sample["flying"]) and (sample["danger"] > 0 or sample["t"] - clear_time >= SKY_ATTACK_FALLBACK_SECONDS))
    landed = observe_until(run, lambda sample: has_landed(run), LANDING_WAIT_SECONDS, on_sample)
    if landed:
        time.sleep(LANDED_SETTLE_SECONDS)
        take_shot(run, "4_landed")
    finish_stage(run, "landing", start, landed, landed_at=landed["t"] if landed else None)


def run_enraged_stage(run):
    start = elapsed_seconds(run)
    lowered = lower_dragon_health(run, ENRAGED_HEALTH_FRACTION)
    reached = observe_until(run, lambda sample: is_phase_reached(sample, 2) and not sample["flying"], ENRAGE_WAIT_SECONDS)
    if reached:
        take_shot(run, "5_enraged")
    finish_stage(run, "enraged", start, reached, lowered_health=lowered.get("health"))


def run_inferno_stage(run):
    start = elapsed_seconds(run)
    lowered = lower_dragon_health(run, INFERNO_HEALTH_FRACTION)
    on_sample = shot_when(run, "6_inferno", lambda sample: INFERNO_DEFINITION in sample["definitions"] or (not definitions_supported(run) and is_phase_reached(sample, 3)))
    observe_until(run, lambda sample: inferno_reached(run, start), INFERNO_WAIT_SECONDS, on_sample)
    judged_by = "entity" if definitions_supported(run) else "phase"
    finish_stage(run, "inferno", start, inferno_reached(run, start), lowered_health=lowered.get("health"), judged_by=judged_by)


def run_death_stage(run):
    start = elapsed_seconds(run)
    lower_dragon_health(run, KILL_HEALTH_FRACTION)
    died = observe_until(run, lambda sample: not sample["alive"], DEATH_WAIT_SECONDS)
    time.sleep(DEATH_SHOT_DELAY_SECONDS)
    take_shot(run, "7_death")
    observe(run)
    finish_stage(run, "death", start, died)


def run_scenario(run, arguments):
    run_editor_code(CLEAN_CODE)
    run_editor_code(CONSOLE_CODE.format(command=f"TDSetCasterLevel {arguments.level}"))
    run_editor_code(CONSOLE_CODE.format(command=f"TDSpawnMonster {DRAGON_SPECIES} 1 {arguments.distance:g}"))
    if not run_spawn_stage(run):
        run["error"] = f"{DRAGON_SPECIES_ASSET} not found within {SPAWN_WAIT_SECONDS:g}s"
        return
    run_ground_stage(run)
    run_sky_stage(run)
    run_minion_stage(run)
    run_landing_stage(run)
    run_enraged_stage(run)
    run_inferno_stage(run)
    run_death_stage(run)


def phases_in_order(run):
    first_seen = run["phase_first_seen"]
    if not all(phase in first_seen for phase in (1, 2, 3)):
        return False
    times = [first_seen[phase] for phase in (1, 2, 3)]
    return times == sorted(times) and all(change["to"] > change["from"] for change in run["phase_transitions"])


def downsample_samples(samples):
    kept = []
    last_second = None
    for sample in samples:
        second = int(sample["t"])
        if second == last_second:
            continue
        kept.append(sample)
        last_second = second
    return kept


def build_report(run, arguments):
    supported = definitions_supported(run)
    checks = {"phase_order": phases_in_order(run), "minions_three": run["minions_max"] >= EXPECTED_MINIONS,
              "danger_concurrent_at_most_one": run["danger_max"] <= 1 if supported else None}
    stages_passed = all(run["stages"].get(name, {}).get("passed") for name in REQUIRED_STAGES)
    passed = stages_passed and not run["error"] and all(value for value in checks.values() if value is not None)
    return {"map": arguments.map, "distance": arguments.distance, "level": arguments.level, "passed": passed, "error": run["error"],
            "checks": checks, "stages": run["stages"], "phase_first_seen": run["phase_first_seen"], "phase_transitions": run["phase_transitions"],
            "flight_segments": run["flight_segments"], "ground_z": run["ground_z"], "minions_max": run["minions_max"],
            "danger_concurrent_max": run["danger_max"] if supported else UNSUPPORTED,
            "definition_breakdown": run["definition_observations"] if supported else UNSUPPORTED,
            "player_min_health": run["player_min_health"], "player_max_health": run["player_max_health"], "player_heal_count": run["player_heal_count"],
            "sample_errors": run["sample_errors"], "screenshots": {name: run["shots"].get(name) for name in SHOT_NAMES},
            "samples": downsample_samples(run["samples"])}


def main():
    global EVIDENCE_DIRECTORY
    parser = argparse.ArgumentParser()
    parser.add_argument("--map", default="/Game/Combat/Maps/LV_TDMegaMagicArena")
    parser.add_argument("--distance", type=float, default=900.0)
    parser.add_argument("--level", type=int, default=30)
    parser.add_argument("--out", help="folder inside the project for report.json and screenshots")
    arguments = parser.parse_args()
    if arguments.out:
        EVIDENCE_DIRECTORY = (ROOT / arguments.out).resolve()

    if "error" in run_editor_code(START_CODE.format(level=arguments.map)):
        print("editor remote execution unavailable")
        return EXIT_ENVIRONMENT
    if not wait_until(lambda: run_editor_code(STATUS_CODE).get("pawn"), PIE_START_WAIT_SECONDS, 2.0):
        print("PIE did not start")
        return EXIT_ENVIRONMENT
    time.sleep(3.0)
    run = new_run()
    try:
        run_scenario(run, arguments)
    except Exception as error:
        run["error"] = f"{type(error).__name__}: {error}"
    finally:
        run_editor_code(STOP_CODE)
    EVIDENCE_DIRECTORY.mkdir(parents=True, exist_ok=True)
    report = build_report(run, arguments)
    (EVIDENCE_DIRECTORY / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding="utf-8")
    print(json.dumps({"passed": report["passed"], "checks": report["checks"], "error": report["error"]}, ensure_ascii=False))
    print(f"report -> {EVIDENCE_DIRECTORY.relative_to(ROOT).as_posix()}/report.json")
    if run["error"]:
        return EXIT_ENVIRONMENT
    return 0 if report["passed"] else EXIT_FAILED


if __name__ == "__main__":
    sys.exit(main())
