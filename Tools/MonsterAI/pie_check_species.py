"""실행 환경: 시스템 Python 3.12(언리얼 에디터가 TDGame을 열고 원격 실행이 켜진 상태, 에디터 앞창 유지 L-editor-06). 맵을 저장하지 않는다.
실행 명령: python Tools/MonsterAI/pie_check_species.py [--species Barghest Griffon ...] [--map /Game/Combat/Maps/LV_TDMegaMagicArena] [--distance 1000] [--ranged-distance 1300] (PowerShell, L-repo-01. 기본 종 목록은 Tools/MonsterAI/species_table.json 전부)
출력: Docs/MonsterAI_CombatSim/measurements/monsterize/pie-<날짜>/ 의 report.json(종마다 이동 거리·공격 관측·피격 뒤 체력·사망 여부)과 <종>_{1_move,2_attack,3_hit,4_death}.png. 종료 코드 0 전 종 통과 / 1 일부 실패 / 2 환경 문제
상태: 현행(M3-33). 종마다 TDSpawnMonster로 1마리를 부르고(정의 role이 Ranged·Support면 --ranged-distance), --species로 일부만 다시 돌리면 기존 report.json에 합치고, 공격은 피해 엔티티 생성으로 감지하며, 피격·사망은 TDCombatLibrary.TryApplyDamage로 낸다
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
TABLE_PATH = ROOT / "Tools" / "MonsterAI" / "species_table.json"
DEFINITIONS_DIRECTORY = ROOT / "Content" / "MonsterAI" / "Definitions"
RANGED_ROLES = ("Ranged", "Support")
SHOT_DIRECTORY = ROOT / "Saved" / "Screenshots" / "WindowsEditor"
EVIDENCE_DIRECTORY = ROOT / "Docs" / "MonsterAI_CombatSim" / "measurements" / "monsterize" / f"pie-{date.today().isoformat()}"
RESULT_MARKER = "TD_SPECIES_PIE"
PIE_START_WAIT_SECONDS = 120.0
SPAWN_WAIT_SECONDS = 15.0
ATTACK_WAIT_SECONDS = 20.0
CAPTURE_FILE_WAIT_SECONDS = 25.0
MOVE_OBSERVE_SECONDS = 1.6
HIT_HEALTH_FRACTION = 0.3
DEATH_SETTLE_SECONDS = 2.5
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

RESET_CODE = '''def _td_reset():
    world = _td_world()
    for actor in _td_monsters(world):
        actor.destroy_actor()
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDDamageEntity):
        actor.destroy_actor()
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    combat = _td_combat(pawn) if pawn else None
    if combat:
        combat.set_stats(combat.get_stats(), True)
    unreal.SystemLibrary.execute_console_command(world, 'TDSpawnMonster {species} 1 {distance}', unreal.GameplayStatics.get_player_controller(world, 0))
    _td_print({{'reset': True}})
_td_reset()
del _td_reset
'''

PROBE_CODE = '''def _td_probe():
    world = _td_world()
    rows = []
    for actor in _td_monsters(world):
        species = actor.get_species()
        combat = _td_combat(actor)
        location = actor.get_actor_location()
        rows.append({'species': species.get_name() if species else None, 'location': [location.x, location.y, location.z],
                      'health': combat.get_current_health() if combat else None, 'max_health': combat.get_max_health() if combat else None,
                      'alive': combat.is_alive() if combat else None, 'brain': actor.has_monster_brain(),
                      'followers': len([c for c in actor.get_components_by_class(unreal.SkeletalMeshComponent)]) - 1})
    entities = len(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDDamageEntity))
    _td_print({'monsters': rows, 'damage_entities': entities})
_td_probe()
del _td_probe
'''

DAMAGE_CODE = '''def _td_damage():
    world = _td_world()
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    applied = []
    for actor in _td_monsters(world):
        combat = _td_combat(actor)
        if not combat or not combat.is_alive():
            continue
        spec = unreal.TDDamageSpec()
        spec.set_editor_property('amount', combat.get_max_health() * {fraction})
        spec.set_editor_property('instigator', pawn)
        spec.set_editor_property('causer', pawn)
        applied.append(unreal.TDCombatLibrary.try_apply_damage(actor, spec))
    _td_print({{'applied': applied}})
_td_damage()
del _td_damage
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


def find_monster(probe, species_name):
    for row in probe.get("monsters", []):
        if row["species"] == f"DA_TDMonster_{species_name}":
            return row
    return None


def measure_distance(first, second):
    return sum((a - b) ** 2 for a, b in zip(first[:2], second[:2])) ** 0.5


def resolve_role(definition_id):
    for _ in range(4):
        path = DEFINITIONS_DIRECTORY / f"{definition_id}.json"
        if not path.exists():
            return None
        definition = json.loads(path.read_text(encoding="utf-8"))
        if "role" in definition or "extends" not in definition:
            return definition.get("role")
        definition_id = definition["extends"]
    return None


def merge_previous_results(results, report_path):
    if not report_path.exists():
        return results
    rerun = {result["species"] for result in results}
    previous = json.loads(report_path.read_text(encoding="utf-8")).get("species", [])
    return [result for result in previous if result["species"] not in rerun] + results


def check_species(species_name, distance):
    result = {"species": species_name, "shots": {}}
    run_editor_code(RESET_CODE.format(species=species_name, distance=distance))
    spawned = wait_until(lambda: find_monster(run_editor_code(PROBE_CODE), species_name), SPAWN_WAIT_SECONDS)
    if not spawned:
        result["error"] = "spawn not observed"
        return result
    result.update({"brain": spawned["brain"], "max_health": spawned["max_health"], "followers": spawned["followers"]})
    time.sleep(MOVE_OBSERVE_SECONDS)
    moved = find_monster(run_editor_code(PROBE_CODE), species_name) or spawned
    result["moved_cm"] = round(measure_distance(spawned["location"], moved["location"]), 1)
    result["shots"]["move"] = capture_screenshot(f"{species_name}_1_move")
    attack = wait_until(lambda: run_editor_code(PROBE_CODE).get("damage_entities", 0) > 0, ATTACK_WAIT_SECONDS, 0.2)
    result["attack_seen"] = bool(attack)
    time.sleep(0.25)
    result["shots"]["attack"] = capture_screenshot(f"{species_name}_2_attack")
    run_editor_code(DAMAGE_CODE.format(fraction=HIT_HEALTH_FRACTION))
    time.sleep(0.15)
    result["shots"]["hit"] = capture_screenshot(f"{species_name}_3_hit")
    after_hit = find_monster(run_editor_code(PROBE_CODE), species_name)
    result["health_after_hit"] = after_hit["health"] if after_hit else None
    run_editor_code(DAMAGE_CODE.format(fraction=3.0))
    time.sleep(DEATH_SETTLE_SECONDS)
    result["shots"]["death"] = capture_screenshot(f"{species_name}_4_death")
    after_death = find_monster(run_editor_code(PROBE_CODE), species_name)
    result["alive_after_death"] = after_death["alive"] if after_death else False
    result["passed"] = bool(result["brain"] and result["moved_cm"] > 30.0 and result["attack_seen"]
                            and result["health_after_hit"] is not None and result["health_after_hit"] < result["max_health"]
                            and not result["alive_after_death"] and all(result["shots"].values()))
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--species", nargs="*")
    parser.add_argument("--map", default="/Game/Combat/Maps/LV_TDMegaMagicArena")
    parser.add_argument("--distance", type=float, default=1000.0)
    parser.add_argument("--ranged-distance", type=float, default=1300.0)
    arguments = parser.parse_args()
    table = json.loads(TABLE_PATH.read_text(encoding="utf-8"))
    species_names = arguments.species or [entry["name"] for entry in table["species"]]
    definitions = {entry["name"]: entry["definition"] for entry in table["species"]}

    if "error" in run_editor_code(START_CODE.format(level=arguments.map)):
        print("editor remote execution unavailable")
        return EXIT_ENVIRONMENT
    if not wait_until(lambda: run_editor_code(STATUS_CODE).get("pawn"), PIE_START_WAIT_SECONDS, 2.0):
        print("PIE did not start")
        return EXIT_ENVIRONMENT
    time.sleep(3.0)
    results = []
    for species_name in species_names:
        is_ranged = resolve_role(definitions.get(species_name, species_name)) in RANGED_ROLES
        result = check_species(species_name, arguments.ranged_distance if is_ranged else arguments.distance)
        result["spawn_distance"] = arguments.ranged_distance if is_ranged else arguments.distance
        results.append(result)
        print(json.dumps({key: value for key, value in result.items() if key != "shots"}, ensure_ascii=False))
    run_editor_code(RESET_CODE.format(species="None", distance=0).replace("unreal.SystemLibrary.execute_console_command", "(lambda *args: None)"))
    run_editor_code(STOP_CODE)
    EVIDENCE_DIRECTORY.mkdir(parents=True, exist_ok=True)
    results = merge_previous_results(results, EVIDENCE_DIRECTORY / "report.json") if arguments.species else results
    report = {"map": arguments.map, "distance": arguments.distance, "ranged_distance": arguments.ranged_distance, "species": results, "passed": all(result.get("passed") for result in results)}
    (EVIDENCE_DIRECTORY / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"passed {sum(1 for result in results if result.get('passed'))}/{len(results)} -> {EVIDENCE_DIRECTORY.relative_to(ROOT).as_posix()}")
    return 0 if report["passed"] else EXIT_FAILED


if __name__ == "__main__":
    sys.exit(main())
