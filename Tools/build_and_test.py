"""실행 환경: 시스템 Python(에디터 밖). 다른 세션의 UnrealEditor·UnrealEditor-Cmd·UBT·UAT가 없을 때까지 기다린 뒤 TDGameEditor를 빌드하고 헤드리스 자동화 시험을 돌려 실패만 요약한다(L-build-05·08, L-repo-12).
실행 명령: python Tools/build_and_test.py --filter TDGame.FluidFX [--filter TDGame.Combat] [--label 이름] [--no-build] [--no-test] [--wait-seconds 1800] [--holder 이름] | --acquire-hold 이름 --reason 사유 | --release-hold 이름 (Saved/AgentOps/build_hold.json {"owner","reason"}이 있으면 owner가 아닌 실행은 대기, 3시간 지나면 무시)
출력: 표준 출력 요약, Saved/AgentOps/<YYYYMMDD>/<label>-build.log·<label>-tests.log·<label>-tests.json. 종료 코드 0 성공 / 1 빌드 실패 / 2 시험 실패·시험 없음 / 3 대기 시간 초과 / 4 보류 획득·해제 거부
상태: 현행
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time
from datetime import datetime

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ue_editor import BUILD_BAT, ENGINE, ROOT, UPROJECT

EDITOR_CMD_EXE = os.path.join(ENGINE, "Engine", "Binaries", "Win64", "UnrealEditor-Cmd.exe")
BLOCKING_IMAGE_NAMES = {"unrealeditor.exe", "unrealeditor-cmd.exe"}
BLOCKING_COMMAND_MARKERS = ("unrealbuildtool", "automationtool")
HOLD_FILE = os.path.join(ROOT, "Saved", "AgentOps", "build_hold.json")
HOLD_EXPIRY_SECONDS = 3 * 3600
POLL_SECONDS = 15
QUIET_CHECKS_REQUIRED = 2
TEST_COMPLETED = re.compile(r"Test Completed\. Result=\{(\w+)\} Name=\{([^}]*)\} Path=\{([^}]*)\}")
TESTS_FOUND = re.compile(r"Found (\d+) automation tests")
ENGINE_CRASH = re.compile(r"Critical error:|Fatal error!|Unhandled Exception:|EXCEPTION_ACCESS_VIOLATION|Assertion failed:")
EVENTS_BEGIN = re.compile(r"BeginEvents: (\S+)")
EVENTS_END = re.compile(r"EndEvents: (\S+)")
BUILD_ERROR = re.compile(r"(: error |: fatal error |error LNK|LNK1104|Unable to build)", re.IGNORECASE)


def list_blocking_processes():
    command = [
        "powershell", "-NoProfile", "-Command",
        "Get-CimInstance Win32_Process | Select-Object ProcessId,Name,CommandLine | ConvertTo-Json -Compress",
    ]
    output = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace").stdout.strip()
    if not output:
        return []
    processes = json.loads(output)
    if isinstance(processes, dict):
        processes = [processes]
    blocking = []
    for process in processes:
        name = (process.get("Name") or "").lower()
        command_line = (process.get("CommandLine") or "").lower()
        is_engine_process = name in BLOCKING_IMAGE_NAMES
        is_build_tool = name.startswith("dotnet") and any(marker in command_line for marker in BLOCKING_COMMAND_MARKERS)
        if is_engine_process or is_build_tool:
            blocking.append(f"{process.get('ProcessId')} {process.get('Name')}")
    return blocking


def read_active_hold(holder):
    if not os.path.isfile(HOLD_FILE):
        return None
    if time.time() - os.path.getmtime(HOLD_FILE) > HOLD_EXPIRY_SECONDS:
        return None
    try:
        hold = json.load(open(HOLD_FILE, encoding="utf-8"))
    except (OSError, ValueError):
        return "보류 파일 읽기 실패"
    if holder and hold.get("owner") == holder:
        return None
    return f"보류 {hold.get('owner')}: {hold.get('reason')}"


def acquire_hold(owner, reason):
    os.makedirs(os.path.dirname(HOLD_FILE), exist_ok=True)
    if os.path.isfile(HOLD_FILE) and time.time() - os.path.getmtime(HOLD_FILE) > HOLD_EXPIRY_SECONDS:
        os.remove(HOLD_FILE)
    try:
        descriptor = os.open(HOLD_FILE, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError:
        current = read_active_hold(None)
        print("보류 획득 실패: 이미 있음 ->", current)
        return False
    with os.fdopen(descriptor, "w", encoding="utf-8") as hold_file:
        json.dump({"owner": owner, "reason": reason, "since": time.strftime("%Y-%m-%d %H:%M")}, hold_file, ensure_ascii=False)
    print("보류 획득:", owner)
    return True


def release_hold(owner):
    if not os.path.isfile(HOLD_FILE):
        print("보류 없음")
        return True
    try:
        hold = json.load(open(HOLD_FILE, encoding="utf-8"))
    except (OSError, ValueError):
        print("보류 파일 읽기 실패: 지우지 않음")
        return False
    if hold.get("owner") != owner:
        print("보류 해제 거부: 주인이 다름 ->", hold.get("owner"))
        return False
    os.remove(HOLD_FILE)
    print("보류 해제:", owner)
    return True


def wait_for_quiet_machine(wait_seconds, holder=None):
    deadline = time.time() + wait_seconds
    quiet_checks = 0
    last_report = None
    while time.time() < deadline:
        blocking = list_blocking_processes()
        active_hold = read_active_hold(holder)
        if active_hold:
            blocking.append(active_hold)
        if not blocking:
            quiet_checks += 1
            if quiet_checks >= QUIET_CHECKS_REQUIRED:
                return True
        else:
            quiet_checks = 0
            if blocking != last_report:
                print("대기: 다른 프로세스 실행 중 ->", ", ".join(blocking), flush=True)
                last_report = blocking
        time.sleep(POLL_SECONDS)
    print("대기 시간 초과:", ", ".join(list_blocking_processes()), flush=True)
    return False


def run_build(log_path):
    command = [BUILD_BAT, "TDGameEditor", "Win64", "Development", f"-Project={UPROJECT}", "-WaitMutex", "-NoHotReload"]
    with open(log_path, "w", encoding="utf-8", errors="replace") as log_file:
        result = subprocess.run(command, stdout=log_file, stderr=subprocess.STDOUT)
    log_text = open(log_path, encoding="utf-8", errors="replace").read()
    error_lines = [line.strip() for line in log_text.splitlines() if BUILD_ERROR.search(line)]
    succeeded = result.returncode == 0 and "Result: Succeeded" in log_text
    return succeeded, error_lines[:15]


def run_tests(test_filters, log_path):
    test_command = "Automation RunTests " + "+".join(test_filters)
    command = [
        EDITOR_CMD_EXE, UPROJECT,
        f"-ExecCmds={test_command}",
        "-TestExit=Automation Test Queue Empty",
        "-unattended", "-NullRHI", "-NoSplash", "-NoSound",
        f"-abslog={log_path}",
    ]
    completed_process = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    run_summary = parse_test_log(log_path)
    run_summary["engine_exit_code"] = completed_process.returncode
    return run_summary


def parse_test_log(log_path):
    run_summary = {"results": [], "found_count": None, "crash_lines": []}
    if not os.path.exists(log_path):
        return run_summary
    results = {}
    events_by_path = {}
    current_events_path = None
    for line in open(log_path, encoding="utf-8", errors="replace"):
        found = TESTS_FOUND.search(line)
        if found:
            run_summary["found_count"] = int(found.group(1))
            continue
        if ENGINE_CRASH.search(line) and len(run_summary["crash_lines"]) < 5:
            run_summary["crash_lines"].append(line.strip()[:300])
        completed = TEST_COMPLETED.search(line)
        if completed:
            results[completed.group(3)] = completed.group(1)
            continue
        begin = EVENTS_BEGIN.search(line)
        if begin:
            current_events_path = begin.group(1)
            continue
        if EVENTS_END.search(line):
            current_events_path = None
            continue
        if current_events_path and ("Error:" in line or "Warning:" in line):
            events_by_path.setdefault(current_events_path, []).append(line.split("LogAutomationController:", 1)[-1].strip())
    run_summary["results"] = [
        {"path": path, "result": result, "events": events_by_path.get(path, [])[:5]}
        for path, result in sorted(results.items())
    ]
    return run_summary


def main():
    sys.stdout.reconfigure(errors="replace")
    parser = argparse.ArgumentParser()
    parser.add_argument("--filter", action="append", default=[], help="자동화 시험 이름 접두사(여러 번 지정 가능)")
    parser.add_argument("--label", default="build-and-test")
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--no-test", action="store_true")
    parser.add_argument("--wait-seconds", type=int, default=1800)
    parser.add_argument("--holder", default=None, help="Saved/AgentOps/build_hold.json 의 owner와 같으면 보류를 통과")
    parser.add_argument("--acquire-hold", metavar="OWNER", default=None, help="보류 파일이 없을 때만 만든다(원자적), 종료 0 성공/4 이미 있음")
    parser.add_argument("--release-hold", metavar="OWNER", default=None, help="owner가 같을 때만 보류 파일을 지운다, 종료 0 성공/4 거부")
    parser.add_argument("--reason", default="", help="--acquire-hold 사유")
    args = parser.parse_args()

    if args.acquire_hold:
        return 0 if acquire_hold(args.acquire_hold, args.reason) else 4
    if args.release_hold:
        return 0 if release_hold(args.release_hold) else 4

    if not args.no_test and not args.filter:
        print("--filter가 필요하다(또는 --no-test)")
        return 2

    output_dir = os.path.join(ROOT, "Saved", "AgentOps", datetime.now().strftime("%Y%m%d"))
    os.makedirs(output_dir, exist_ok=True)
    build_log = os.path.join(output_dir, f"{args.label}-build.log")
    test_log = os.path.join(output_dir, f"{args.label}-tests.log")
    test_json = os.path.join(output_dir, f"{args.label}-tests.json")

    if not wait_for_quiet_machine(args.wait_seconds, args.holder):
        return 3

    if not args.no_build:
        succeeded, error_lines = run_build(build_log)
        print("BUILD", "Succeeded" if succeeded else "Failed", "->", build_log)
        for line in error_lines:
            print("  ", line)
        if not succeeded:
            return 1

    if args.no_test:
        return 0

    if not wait_for_quiet_machine(args.wait_seconds, args.holder):
        return 3

    run_summary = run_tests(args.filter, test_log)
    results = run_summary["results"]
    passed = [entry for entry in results if entry["result"] == "Success"]
    failed = [entry for entry in results if entry["result"] != "Success"]
    found_count = run_summary["found_count"]
    is_incomplete = found_count is None or len(results) < found_count
    has_crashed = bool(run_summary["crash_lines"]) or run_summary["engine_exit_code"] != 0
    run_summary.update({"filters": args.filter, "log": test_log, "incomplete": is_incomplete, "crashed": has_crashed})
    with open(test_json, "w", encoding="utf-8") as json_file:
        json.dump(run_summary, json_file, ensure_ascii=False, indent=1)
    print(f"TESTS {len(passed)}/{found_count if found_count is not None else '?'} passed (completed {len(results)}, engine exit {run_summary['engine_exit_code']}) -> {test_json}")
    for entry in failed:
        print("  FAIL", entry["path"], entry["result"])
        for event in entry["events"]:
            print("     ", event[:300])
    if is_incomplete:
        print("  INCOMPLETE: 발견한 시험 수보다 완료한 시험이 적다(중간 종료)")
    for crash_line in run_summary["crash_lines"]:
        print("  CRASH", crash_line)
    if not results or failed or is_incomplete or has_crashed:
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
