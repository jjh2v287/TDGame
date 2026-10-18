"""실행 환경: 시스템 Python 3.12(언리얼 에디터가 TDGame을 열고 원격 실행이 켜진 상태, 에디터 앞창 유지 L-editor-06). 맵을 저장하지 않는다.
실행 명령: python Tools/MonsterAI/pie_check_mid_bosses.py [--boss CentaurHunter GriffonAlpha] [--map /Game/Combat/Maps/LV_TDMegaMagicArena] [--distance 900] [--level 30] [--out <프로젝트 안 폴더>] (PowerShell, L-repo-01)
출력: Docs/MonsterAI_CombatSim/measurements/M3-34/pie-<날짜>/ 의 report.json(보스별 단계 통과·경과 초·표본 수, 페이즈 전이 시각, 관측된 엔티티 정의 이름)과 <보스>_<단계>.png(CentaurHunter 1_bow·2_draw_sword·3_sword·4_death, GriffonAlpha 1_ground·2_takeoff·3_air_attack·4_landed·5_death). 종료 코드 0 전 단계 통과 / 1 일부 실패 / 2 환경 문제
상태: 현행(M3-34), 2026-10-18 PIE 통과(두 보스 전 단계·점검 4개). 보스를 한 마리씩 TDSpawnMonster로 부르고 체력을 TDCombatLibrary.TryApplyDamage로 깎아(켄타우로스 48%, 그리폰 58%, 처치) 페이즈 1 전이와 처치를 관측한다. 공격은 TDDamageEntity의 definition 이름으로, 비행은 보스 Z(시작 Z 대비 +80cm 이상, 복귀는 +30cm 이하)로 판정한다
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
EVIDENCE_DIRECTORY = ROOT / "Docs" / "MonsterAI_CombatSim" / "measurements" / "M3-34" / f"pie-{date.today().strftime('%Y%m%d')}"
RESULT_MARKER = "TD_MID_BOSS_PIE"
MINION_DEFINITION = "Barghest"
BOW_DEFINITIONS = frozenset({"DA_TDCentaurHunterArrow", "DA_TDCentaurHunterVolleyArrow"})
SWORD_DEFINITIONS = frozenset({"DA_TDCentaurHunterCharge", "DA_TDCentaurHunterSwordCombo", "DA_TDCentaurHunterSwordSlash"})
GROUND_DEFINITIONS = frozenset({"DA_TDGriffonAlphaBite", "DA_TDGriffonAlphaClaw", "DA_TDGriffonAlphaRakeTwo"})
AIR_DEFINITIONS = frozenset({"DA_TDGriffonAlphaDiveClaw", "DA_TDGriffonAlphaGlideSwoop"})
PIE_START_WAIT_SECONDS = 120.0
SPAWN_WAIT_SECONDS = 15.0
CAPTURE_FILE_WAIT_SECONDS = 25.0
SAMPLE_INTERVAL_SECONDS = 0.5
BOW_WAIT_SECONDS = 12.0
DRAW_SWORD_WAIT_SECONDS = 8.0
MINION_CLEAR_WAIT_SECONDS = 10.0
SWORD_WAIT_SECONDS = 12.0
GROUND_WAIT_SECONDS = 12.0
TAKEOFF_WAIT_SECONDS = 8.0
AIR_ATTACK_WAIT_SECONDS = 15.0
LANDING_WAIT_SECONDS = 25.0
LANDED_SETTLE_SECONDS = 0.5
DEATH_WAIT_SECONDS = 10.0
DEATH_SHOT_DELAY_SECONDS = 3.0
SWORD_HEALTH_FRACTION = 0.48
SKY_HEALTH_FRACTION = 0.58
KILL_HEALTH_FRACTION = -1.0
HEALTH_TOLERANCE_FRACTION = 0.002
LOWER_HEALTH_ATTEMPTS = 12
LOWER_HEALTH_STEPS = 6
EXPECTED_MINIONS = 2
TAKEOFF_ALTITUDE_CM = 80.0
LANDED_ALTITUDE_CM = 30.0
PLAYER_HEAL_FRACTION = 0.3
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

BOSS_HELPERS = '''def _td_definition_id(actor):
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

BOSS_HELPERS_CLEANUP = '''del _td_definition_id
del _td_entity_definition
'''

SAMPLE_CODE = '''def _td_sample(boss_asset, minion_definition):
    world = _td_world()
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    player = _td_combat(pawn) if pawn else None
    boss = None
    minions = 0
    for actor in _td_monsters(world):
        species = actor.get_species()
        combat = _td_combat(actor)
        if species is None or combat is None:
            continue
        if species.get_name() == boss_asset:
            location = actor.get_actor_location()
            boss = {'alive': combat.is_alive(), 'phase': actor.get_monster_phase_index(), 'flying': actor.is_monster_flying(),
                    'health': combat.get_current_health(), 'max_health': combat.get_max_health(), 'z': location.z,
                    'definition': _td_definition_id(actor)}
        elif minion_definition and combat.is_alive() and _td_definition_id(actor) == minion_definition:
            minions += 1
    entities = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDDamageEntity)
    by_definition = {}
    for entity in entities:
        name = _td_entity_definition(entity)
        if name:
            by_definition[name] = by_definition.get(name, 0) + 1
    _td_print({'boss': boss, 'minions': minions,
               'player': {'health': player.get_current_health() if player else None, 'max_health': player.get_max_health() if player else None},
               'entities': {'total': len(entities), 'by_definition': by_definition}})
'''

LOWER_CODE = '''def _td_lower_health(boss_asset, target_fraction, steps, tolerance_fraction):
    world = _td_world()
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    for actor in _td_monsters(world):
        species = actor.get_species()
        combat = _td_combat(actor)
        if species is None or combat is None or species.get_name() != boss_asset:
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


def run_boss_code(body):
    return run_editor_code(BOSS_HELPERS + body + BOSS_HELPERS_CLEANUP)


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


def new_run(boss):
    return {"boss": boss, "species_asset": f"DA_TDMonster_{boss}", "plan": BOSS_PLANS[boss], "started": time.monotonic(), "samples": [],
            "stages": {}, "shots": {}, "phase_first_seen": {}, "phase_transitions": [], "last_phase": None, "start_z": None,
            "max_altitude_cm": 0.0, "minions_max": 0, "entity_total_seen": 0, "entity_named_seen": 0, "definition_observations": {},
            "definition_seen": None, "player_min_health": None, "player_max_health": None, "player_heal_count": 0, "sample_errors": 0,
            "error": None}


def elapsed_seconds(run):
    return round(time.monotonic() - run["started"], 2)


def note_boss_identity(run, boss):
    if not boss:
        return
    if run["start_z"] is None:
        run["start_z"] = boss["z"]
    if boss["definition"]:
        run["definition_seen"] = boss["definition"]


def build_sample(run, payload):
    boss = payload["boss"]
    definitions = payload["entities"]["by_definition"]
    return {"t": elapsed_seconds(run), "boss_found": boss is not None, "alive": bool(boss and boss["alive"]),
            "phase": boss["phase"] if boss else None, "flying": boss["flying"] if boss else None,
            "health_ratio": round(boss["health"] / boss["max_health"], 3) if boss and boss["max_health"] else None,
            "z": round(boss["z"], 1) if boss else None, "altitude_cm": round(boss["z"] - run["start_z"], 1) if boss else None,
            "player_health": payload["player"]["health"], "player_max_health": payload["player"]["max_health"],
            "minions": payload["minions"], "entity_total": payload["entities"]["total"], "definitions": definitions}


def track_player(run, sample):
    health, max_health = sample["player_health"], sample["player_max_health"]
    if health is None or not max_health:
        return
    if run["player_min_health"] is None or health < run["player_min_health"]:
        run["player_min_health"] = health
        run["player_max_health"] = max_health


def track_boss(run, sample):
    phase = sample["phase"]
    run["phase_first_seen"].setdefault(phase, sample["t"])
    previous = run["last_phase"]
    if previous is not None and phase != previous:
        run["phase_transitions"].append({"from": previous, "to": phase, "t": sample["t"]})
    run["last_phase"] = phase
    run["max_altitude_cm"] = max(run["max_altitude_cm"], sample["altitude_cm"])


def record_sample(run, sample):
    run["samples"].append(sample)
    run["minions_max"] = max(run["minions_max"], sample["minions"])
    run["entity_total_seen"] += sample["entity_total"]
    run["entity_named_seen"] += sum(sample["definitions"].values())
    for name, count in sample["definitions"].items():
        entry = run["definition_observations"].setdefault(name, {"samples": 0, "max_concurrent": 0})
        entry["samples"] += 1
        entry["max_concurrent"] = max(entry["max_concurrent"], count)
    track_player(run, sample)
    if sample["alive"]:
        track_boss(run, sample)


def heal_player_if_low(run, sample):
    health, max_health = sample["player_health"], sample["player_max_health"]
    if health is None or not max_health or health >= max_health * PLAYER_HEAL_FRACTION:
        return
    run_editor_code(HEAL_CODE)
    run["player_heal_count"] += 1


def observe(run):
    sample_call = f"_td_sample({run['species_asset']!r}, {run['plan']['minion_definition']!r})\ndel _td_sample\n"
    payload = run_boss_code(SAMPLE_CODE + sample_call)
    if "error" in payload:
        run["sample_errors"] += 1
        return None
    note_boss_identity(run, payload["boss"])
    sample = build_sample(run, payload)
    record_sample(run, sample)
    heal_player_if_low(run, sample)
    return sample


def observe_until(run, predicate, timeout_seconds):
    deadline = time.monotonic() + timeout_seconds
    while True:
        loop_started = time.monotonic()
        sample = observe(run)
        if sample and predicate(sample):
            return sample
        if time.monotonic() >= deadline:
            return None
        time.sleep(max(0.0, SAMPLE_INTERVAL_SECONDS - (time.monotonic() - loop_started)))


def take_shot(run, name):
    if name not in run["shots"]:
        run["shots"][name] = capture_screenshot(f"{run['boss']}_{name}")
    return run["shots"][name]


def observe_and_shoot(run, shot_name, condition, timeout_seconds):
    matched = observe_until(run, condition, timeout_seconds)
    if matched:
        take_shot(run, shot_name)
    return matched


def begin_stage(run):
    return {"start": elapsed_seconds(run), "first_index": len(run["samples"])}


def stage_window(run, stage):
    return run["samples"][stage["first_index"]:]


def finish_stage(run, name, stage, passed, **details):
    end = elapsed_seconds(run)
    run["stages"][name] = {"passed": bool(passed), "start": stage["start"], "end": end, "elapsed": round(end - stage["start"], 2),
                           "samples": len(run["samples"]) - stage["first_index"], **details}
    print(json.dumps({"boss": run["boss"], "stage": name, **run["stages"][name]}, ensure_ascii=False), flush=True)


def has_definition(sample, names):
    return any(name in sample["definitions"] for name in names)


def matched_definitions(window, names):
    return sorted({name for sample in window for name in sample["definitions"] if name in names})


def phases_seen(window):
    return sorted({sample["phase"] for sample in window if sample["alive"]})


def is_phase_reached(sample, phase):
    return sample["alive"] and sample["phase"] >= phase


def definition_condition(names, minimum_phase=0):
    def condition(sample):
        return is_phase_reached(sample, minimum_phase) and has_definition(sample, names)
    return condition


def is_draw_sword_reached(sample):
    return is_phase_reached(sample, 1) and sample["minions"] >= EXPECTED_MINIONS


def is_takeoff_reached(sample):
    return is_phase_reached(sample, 1) and sample["altitude_cm"] >= TAKEOFF_ALTITUDE_CM


def is_landed(sample):
    return sample["alive"] and sample["altitude_cm"] <= LANDED_ALTITUDE_CM


def lower_boss_health(run, target_fraction):
    result = {"missing": True}
    for _ in range(LOWER_HEALTH_ATTEMPTS):
        call = f"_td_lower_health({run['species_asset']!r}, {target_fraction!r}, {LOWER_HEALTH_STEPS}, {HEALTH_TOLERANCE_FRACTION!r})\ndel _td_lower_health\n"
        result = run_boss_code(LOWER_CODE + call)
        if result.get("reached") or result.get("missing"):
            break
        observe(run)
        time.sleep(SAMPLE_INTERVAL_SECONDS)
    return result


def run_spawn_stage(run):
    stage = begin_stage(run)
    spawned = observe_until(run, lambda sample: sample["boss_found"], SPAWN_WAIT_SECONDS)
    finish_stage(run, "spawn", stage, spawned)
    return spawned is not None


def run_wait_stage(run, name, condition, timeout_seconds, expected_definitions=None, lower_fraction=None):
    stage = begin_stage(run)
    details = {}
    if lower_fraction is not None:
        lowered = lower_boss_health(run, lower_fraction)
        details.update(lowered_health=lowered.get("health"), lowered_reached=bool(lowered.get("reached")))
    matched = observe_and_shoot(run, name, condition, timeout_seconds)
    window = stage_window(run, stage)
    if expected_definitions:
        details["matched_definitions"] = matched_definitions(window, expected_definitions)
    finish_stage(run, name, stage, matched, phases=phases_seen(window), minions_peak=max((sample["minions"] for sample in window), default=0),
                 peak_altitude_cm=max((sample["altitude_cm"] for sample in window if sample["alive"]), default=None), **details)


def run_minion_stage(run):
    stage = begin_stage(run)
    killed = run_boss_code(KILL_MINIONS_CODE + f"_td_kill_minions({MINION_DEFINITION!r})\ndel _td_kill_minions\n").get("killed")
    cleared = observe_until(run, lambda sample: sample["minions"] == 0, MINION_CLEAR_WAIT_SECONDS)
    finish_stage(run, "minions_cleared", stage, cleared, killed=killed)


def run_landed_stage(run):
    stage = begin_stage(run)
    if run["max_altitude_cm"] < TAKEOFF_ALTITUDE_CM:
        finish_stage(run, "4_landed", stage, False, reason="flight not observed")
        return
    landed = observe_until(run, is_landed, LANDING_WAIT_SECONDS)
    if landed:
        time.sleep(LANDED_SETTLE_SECONDS)
        take_shot(run, "4_landed")
    finish_stage(run, "4_landed", stage, landed, landed_at=landed["t"] if landed else None,
                 flying_flag_at_landing=landed["flying"] if landed else None)


def run_death_stage(run, name):
    stage = begin_stage(run)
    lower_boss_health(run, KILL_HEALTH_FRACTION)
    died = observe_until(run, lambda sample: not sample["alive"], DEATH_WAIT_SECONDS)
    time.sleep(DEATH_SHOT_DELAY_SECONDS)
    take_shot(run, name)
    observe(run)
    finish_stage(run, name, stage, died)


def run_centaur_scenario(run):
    run_wait_stage(run, "1_bow", definition_condition(BOW_DEFINITIONS), BOW_WAIT_SECONDS, expected_definitions=BOW_DEFINITIONS)
    run_wait_stage(run, "2_draw_sword", is_draw_sword_reached, DRAW_SWORD_WAIT_SECONDS, lower_fraction=SWORD_HEALTH_FRACTION)
    run_minion_stage(run)
    run_wait_stage(run, "3_sword", definition_condition(SWORD_DEFINITIONS, 1), SWORD_WAIT_SECONDS, expected_definitions=SWORD_DEFINITIONS)
    run_death_stage(run, "4_death")


def run_griffon_scenario(run):
    run_wait_stage(run, "1_ground", definition_condition(GROUND_DEFINITIONS), GROUND_WAIT_SECONDS, expected_definitions=GROUND_DEFINITIONS)
    run_wait_stage(run, "2_takeoff", is_takeoff_reached, TAKEOFF_WAIT_SECONDS, lower_fraction=SKY_HEALTH_FRACTION)
    run_wait_stage(run, "3_air_attack", definition_condition(AIR_DEFINITIONS), AIR_ATTACK_WAIT_SECONDS, expected_definitions=AIR_DEFINITIONS)
    run_landed_stage(run)
    run_death_stage(run, "5_death")


BOSS_PLANS = {
    "CentaurHunter": {"definition_id": "Centaur_Hunter", "minion_definition": MINION_DEFINITION, "scenario": run_centaur_scenario,
                      "required_stages": ("spawn", "1_bow", "2_draw_sword", "minions_cleared", "3_sword", "4_death"),
                      "shot_names": ("1_bow", "2_draw_sword", "3_sword", "4_death")},
    "GriffonAlpha": {"definition_id": "Griffon_Alpha", "minion_definition": None, "scenario": run_griffon_scenario,
                     "required_stages": ("spawn", "1_ground", "2_takeoff", "3_air_attack", "4_landed", "5_death"),
                     "shot_names": ("1_ground", "2_takeoff", "3_air_attack", "4_landed", "5_death")},
}


def run_scenario(run, arguments):
    run_editor_code(CLEAN_CODE)
    run_editor_code(CONSOLE_CODE.format(command=f"TDSetCasterLevel {arguments.level}"))
    run_editor_code(CONSOLE_CODE.format(command=f"TDSpawnMonster {run['boss']} 1 {arguments.distance:g}"))
    if not run_spawn_stage(run):
        run["error"] = f"{run['species_asset']} not found within {SPAWN_WAIT_SECONDS:g}s"
        return
    run["plan"]["scenario"](run)


def phases_in_order(run):
    first_seen = run["phase_first_seen"]
    if 0 not in first_seen or 1 not in first_seen:
        return False
    return first_seen[0] <= first_seen[1] and all(change["to"] > change["from"] for change in run["phase_transitions"])


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


def build_report(run):
    plan = run["plan"]
    checks = {"phase_order": phases_in_order(run), "definition_id": run["definition_seen"] == plan["definition_id"],
              "entity_names_readable": run["entity_named_seen"] > 0,
              "screenshots": all(run["shots"].get(name) for name in plan["shot_names"])}
    stages_passed = all(run["stages"].get(name, {}).get("passed") for name in plan["required_stages"])
    passed = stages_passed and not run["error"] and all(checks.values())
    return {"boss": run["boss"], "species_asset": run["species_asset"], "definition_id": plan["definition_id"], "definition_seen": run["definition_seen"],
            "passed": passed, "error": run["error"], "checks": checks, "stages": run["stages"], "phase_first_seen": run["phase_first_seen"],
            "phase_transitions": run["phase_transitions"], "start_z": run["start_z"], "max_altitude_cm": run["max_altitude_cm"],
            "minions_max": run["minions_max"], "observed_definitions": sorted(run["definition_observations"]),
            "definition_breakdown": run["definition_observations"], "player_min_health": run["player_min_health"],
            "player_max_health": run["player_max_health"], "player_heal_count": run["player_heal_count"], "sample_errors": run["sample_errors"],
            "screenshots": {name: run["shots"].get(name) for name in plan["shot_names"]}, "samples": downsample_samples(run["samples"])}


def check_boss(boss, arguments):
    run = new_run(boss)
    try:
        run_scenario(run, arguments)
    except Exception as error:
        run["error"] = f"{type(error).__name__}: {error}"
    return build_report(run)


def main():
    global EVIDENCE_DIRECTORY
    parser = argparse.ArgumentParser()
    parser.add_argument("--boss", nargs="+", choices=list(BOSS_PLANS), default=list(BOSS_PLANS), help="mid bosses to check, run one by one in the given order")
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
    reports = {}
    try:
        for boss in dict.fromkeys(arguments.boss):
            reports[boss] = check_boss(boss, arguments)
    finally:
        run_editor_code(STOP_CODE)
    EVIDENCE_DIRECTORY.mkdir(parents=True, exist_ok=True)
    passed = all(report["passed"] for report in reports.values())
    report = {"map": arguments.map, "distance": arguments.distance, "level": arguments.level, "passed": passed, "bosses": reports}
    (EVIDENCE_DIRECTORY / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding="utf-8")
    summary = {boss: {"passed": item["passed"], "checks": item["checks"], "error": item["error"]} for boss, item in reports.items()}
    print(json.dumps({"passed": passed, "bosses": summary}, ensure_ascii=False))
    print(f"report -> {EVIDENCE_DIRECTORY.relative_to(ROOT).as_posix()}/report.json")
    if any(item["error"] for item in reports.values()):
        return EXIT_ENVIRONMENT
    return 0 if passed else EXIT_FAILED


if __name__ == "__main__":
    sys.exit(main())
