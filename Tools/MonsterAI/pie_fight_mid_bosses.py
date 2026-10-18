"""실행 환경: 시스템 Python 3.12(언리얼 에디터가 TDGame을 열고 원격 실행이 켜진 상태, 에디터 앞창 유지 L-editor-06). 맵을 저장하지 않는다. 보스 체력을 스크립트로 깎지 않는다
실행 명령: python Tools/MonsterAI/pie_fight_mid_bosses.py [--boss CentaurHunter GriffonAlpha] [--mode sword sword_spells] [--cap-seconds 300] [--map /Game/Combat/Maps/LV_TDMegaMagicArena] [--out <프로젝트 안 폴더>] (PowerShell, L-repo-01)
출력: Docs/MonsterAI_CombatSim/measurements/M3-34/fight-<날짜>/ 의 report.json(판마다 처치 시간·받은 피해·기절 구간·비행 구간·졸개 처치·체력 곡선)과 <보스>_<모드>_<phase1|long_stagger|flight|kill|end>.png, 원본 표본은 Saved/MonsterAI/MidBossFight/samples_*.jsonl. 종료 코드 0 모두 처치 / 1 일부 미처치 / 2 환경 문제
상태: 현행(2026-10-18 M3-34 후속). editor_pie_mid_boss_fight.py가 에디터 안에서 자동 전투원(졸개 우선 추적·칼·마법·위험 형상 구르기)을 틱 콜백으로 돌리고, 이 스크립트는 PIE를 켜고 보스×모드 4판을 차례로 start·상태 파일 폴링·cleanup 한다
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
EDITOR_SCRIPT = ROOT / "Tools" / "MonsterAI" / "editor_pie_mid_boss_fight.py"
WORK_DIRECTORY = ROOT / "Saved" / "MonsterAI" / "MidBossFight"
REQUEST_PATH = WORK_DIRECTORY / "request.json"
STATE_PATH = WORK_DIRECTORY / "state.json"
EVIDENCE_DIRECTORY = ROOT / "Docs" / "MonsterAI_CombatSim" / "measurements" / "M3-34" / f"fight-{date.today().strftime('%Y%m%d')}"
FIGHT_MARKER = "TD_MID_BOSS_FIGHT"
CODE_MARKER = "TD_MID_BOSS_FIGHT_CODE"
BOSSES = ("CentaurHunter", "GriffonAlpha")
MODES = ("sword", "sword_spells")
PIE_START_WAIT_SECONDS = 150.0
POLL_SECONDS = 1.0
PROGRESS_PRINT_SECONDS = 20.0
FIGHT_WAIT_EXTRA_SECONDS = 400.0
BETWEEN_FIGHTS_SECONDS = 3.0
SETTLE_AFTER_PIE_SECONDS = 4.0
TERMINAL_RESULTS = ("complete", "failed", "cancelled")
EXIT_NOT_DEFEATED = 1
EXIT_ENVIRONMENT = 2

PRELUDE = '''import unreal, json
def _td_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
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

STOP_CODE = '''def _td_stop():
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    _td_print({'stopped': True})
_td_stop()
del _td_stop
'''


def run_editor_process(arguments):
    completed = subprocess.run([sys.executable, str(RUN_IN_EDITOR), *arguments], capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=ROOT)
    return completed.stdout, completed.stderr


def find_marker_payload(output, marker):
    for line in output.splitlines():
        index = line.find(marker + " ")
        if index >= 0:
            return json.loads(line[index + len(marker) + 1:])
    return None


def run_editor_code(body):
    output, error = run_editor_process(["-c", PRELUDE.format(marker=CODE_MARKER) + body])
    payload = find_marker_payload(output, CODE_MARKER)
    return payload if payload is not None else {"error": output[-600:] + error[-300:]}


def run_fight_action(request):
    WORK_DIRECTORY.mkdir(parents=True, exist_ok=True)
    REQUEST_PATH.write_text(json.dumps(request), encoding="utf-8")
    output, error = run_editor_process([str(EDITOR_SCRIPT)])
    payload = find_marker_payload(output, FIGHT_MARKER)
    return payload if payload is not None else {"error": output[-800:] + error[-300:]}


def read_state():
    for _ in range(5):
        try:
            return json.loads(STATE_PATH.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            time.sleep(0.1)
    return None


def wait_for_player(timeout_seconds):
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        status = run_fight_action({"action": "status"})
        if status.get("player"):
            return status
        time.sleep(2.0)
    return None


def format_progress(state):
    progress = state.get("progress") or {}
    return (f"  fight={progress.get('fight_seconds')}s engaged={progress.get('engaged_seconds')}s boss_hp={progress.get('boss_hp_ratio')} "
            f"damage={progress.get('player_damage_taken')} attacks={progress.get('player_attacks')} rolls={progress.get('rolls')} minions_killed={progress.get('minions_killed')}")


def wait_for_fight_end(boss, mode, cap_seconds):
    deadline = time.monotonic() + cap_seconds * 2.0 + FIGHT_WAIT_EXTRA_SECONDS
    last_print = 0.0
    while time.monotonic() < deadline:
        state = read_state()
        if state and state.get("boss") == boss and state.get("mode") == mode:
            if state.get("result") in TERMINAL_RESULTS:
                return state
            if time.monotonic() - last_print >= PROGRESS_PRINT_SECONDS:
                last_print = time.monotonic()
                print(f"[{boss}/{mode}] {state.get('stage')} {format_progress(state)}", flush=True)
        time.sleep(POLL_SECONDS)
    return None


def copy_shots(record, boss, mode):
    copied = {}
    EVIDENCE_DIRECTORY.mkdir(parents=True, exist_ok=True)
    for name, source in (record.get("shots") or {}).items():
        if not source or not Path(source).exists():
            copied[name] = None
            continue
        target = EVIDENCE_DIRECTORY / f"{boss}_{mode}_{name}.png"
        shutil.copyfile(source, target)
        copied[name] = target.relative_to(ROOT).as_posix()
    return copied


def run_fight(boss, mode, cap_seconds, origin):
    entry = {"boss": boss, "mode": mode, "environment_problem": None, "result": None, "errors": [], "notes": [], "samples_path": None, "fight": None}
    started = run_fight_action({"action": "start", "boss": boss, "mode": mode, "cap_seconds": cap_seconds, "origin": origin})
    if not started.get("started"):
        entry["environment_problem"] = started.get("error", "start failed")
        return entry
    state = wait_for_fight_end(boss, mode, cap_seconds)
    run_fight_action({"action": "cleanup"})
    if state is None:
        entry["environment_problem"] = "fight did not finish before the driver timeout"
        return entry
    record = state.get("fight") or {}
    entry.update(result=state.get("result"), errors=state.get("errors", []), notes=state.get("notes", []), samples_path=state.get("samples_path"))
    if state.get("result") != "complete":
        entry["environment_problem"] = (state.get("errors") or ["fight result " + str(state.get("result"))])[-1][-300:]
    record["screenshots"] = copy_shots(record, boss, mode)
    record.pop("shots", None)
    entry["fight"] = record
    return entry


def summarize(entry):
    record = entry["fight"] or {}
    return {"boss": entry["boss"], "mode": entry["mode"], "result": entry["result"], "ended_reason": record.get("ended_reason"),
            "boss_defeated": record.get("boss_defeated"), "kill_seconds": record.get("kill_seconds"), "phase1_seconds": record.get("phase1_seconds"),
            "player_damage_taken": record.get("player_damage_taken"), "player_damage_in_H": record.get("player_damage_in_H"),
            "player_attacks": record.get("player_attacks"), "spell_casts": record.get("spell_casts"), "rolls": record.get("rolls"),
            "stagger_intervals": len(record.get("stagger_intervals") or []), "flight_intervals": len(record.get("flight_intervals") or []),
            "minions_killed": record.get("minions_killed"), "environment_problem": entry["environment_problem"]}


def decide_exit_code(entries):
    if not entries or any(entry["environment_problem"] for entry in entries):
        return EXIT_ENVIRONMENT
    if any(not (entry["fight"] or {}).get("boss_defeated") for entry in entries):
        return EXIT_NOT_DEFEATED
    return 0


def write_report(arguments, entries, origin):
    EVIDENCE_DIRECTORY.mkdir(parents=True, exist_ok=True)
    report = {"date": date.today().isoformat(), "map": arguments.map, "cap_seconds": arguments.cap_seconds, "origin": origin,
              "time_base": "kill_seconds·phase1_seconds는 보스 교전(engaged) 순간부터, 나머지 시각은 보스 등록 순간부터의 게임 초. player_damage_in_H = 받은 피해 / 100(플레이어 최대 체력 100 기준)",
              "summary": [summarize(entry) for entry in entries], "fights": entries}
    path = EVIDENCE_DIRECTORY / "report.json"
    path.write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding="utf-8")
    return path


def main():
    global EVIDENCE_DIRECTORY
    parser = argparse.ArgumentParser()
    parser.add_argument("--boss", nargs="+", choices=BOSSES, default=list(BOSSES))
    parser.add_argument("--mode", nargs="+", choices=MODES, default=list(MODES))
    parser.add_argument("--cap-seconds", type=float, default=300.0)
    parser.add_argument("--map", default="/Game/Combat/Maps/LV_TDMegaMagicArena")
    parser.add_argument("--out", help="folder inside the project for report.json and screenshots")
    arguments = parser.parse_args()
    if arguments.out:
        EVIDENCE_DIRECTORY = (ROOT / arguments.out).resolve()

    if "error" in run_editor_code(START_CODE.format(level=arguments.map)):
        print("editor remote execution unavailable")
        return EXIT_ENVIRONMENT
    status = wait_for_player(PIE_START_WAIT_SECONDS)
    if status is None:
        print("PIE did not start")
        return EXIT_ENVIRONMENT
    time.sleep(SETTLE_AFTER_PIE_SECONDS)
    origin = dict(status["player"])
    entries = []
    try:
        for boss in dict.fromkeys(arguments.boss):
            for mode in dict.fromkeys(arguments.mode):
                entry = run_fight(boss, mode, arguments.cap_seconds, origin)
                entries.append(entry)
                print(json.dumps(summarize(entry), ensure_ascii=False), flush=True)
                time.sleep(BETWEEN_FIGHTS_SECONDS)
    finally:
        run_fight_action({"action": "cleanup"})
        run_editor_code(STOP_CODE)
    path = write_report(arguments, entries, origin)
    print(f"report -> {path.relative_to(ROOT).as_posix() if EVIDENCE_DIRECTORY.is_relative_to(ROOT) else path}")
    return decide_exit_code(entries)


if __name__ == "__main__":
    sys.exit(main())
