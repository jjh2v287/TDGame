"""실행 환경: 시스템 Python 3.12(언리얼 에디터가 TDGame을 열고 MCP 포트 8000·원격 실행이 켜진 상태, 에디터 앞창 유지: 백그라운드 PIE는 3fps로 제한된다 L-editor-06). PIE 안 시나리오는 editor_pie_tactics_probe.py가 프레임마다 진행한다.
실행 명령: python Tools/MonsterAI/pie_check_tactics.py [--suite behavior|crowd|all] [--wall-x -3600 --corridor-x 0 --corridor-y -4000] [--offline] (PowerShell. LV-Cambat에 임시 벽 3개를 저장 없이 놓았다가 끝나면 지운다)
출력: Docs/MonsterAI_CombatSim/measurements/squad-tactics/pie-<날짜>/ 의 report.json(장면별 지표·Insights 비용)·스크린샷 01~05.png·samples_behavior_1hz.jsonl, 원본 표본·트레이스는 Saved/MonsterAI/TacticsPie/. 종료 코드 0 완료/1 시나리오 실패/2 환경 문제
상태: 현행(2026-10-18 M3-14 PIE 검증용)
"""
import argparse
import bisect
import csv
import json
import math
import shutil
import statistics
import subprocess
import sys
import time
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools"))
sys.path.insert(0, str(ROOT / "Tools" / "MonsterAI"))
import uemcp  # noqa: E402
import tactics_pie_analysis  # noqa: E402

RUN_IN_EDITOR = ROOT / "Tools" / "run_in_editor.py"
PROBE_SCRIPT = ROOT / "Tools" / "MonsterAI" / "editor_pie_tactics_probe.py"
WORK_DIRECTORY = ROOT / "Saved" / "MonsterAI" / "TacticsPie"
REQUEST_PATH = WORK_DIRECTORY / "request.json"
STATE_PATH = WORK_DIRECTORY / "probe_state.json"
EVIDENCE_DIRECTORY = ROOT / "Docs" / "MonsterAI_CombatSim" / "measurements" / "squad-tactics" / f"pie-{date.today().isoformat()}"
LEVEL_PATH = "/Game/Level/LV-Cambat"
RESULT_MARKER = "TD_TACTICS_PROBE_RESULT"
INSIGHTS = Path(r"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealInsights.exe")
SHOT_DIRECTORY = ROOT / "Saved" / "Screenshots" / "WindowsEditor"
CAPTURE_FILE_WAIT_SECONDS = 25.0
PIE_START_WAIT_SECONDS = 120.0
PIE_STOP_WAIT_SECONDS = 60.0
SUITE_TIMEOUT_SECONDS = {"behavior": 900.0, "crowd": 420.0}
POLL_SECONDS = 1.0
INSIGHTS_TIMEOUT_SECONDS = 600.0
EVIDENCE_SAMPLE_EVERY = 20
EXIT_FAILED = 1
EXIT_ENVIRONMENT = 2
OPEN_LEVEL_CODE = '''import unreal, json
def _td_open():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    pie = editor.get_game_world() is not None
    loaded = (not pie) and (editor.get_editor_world().get_path_name().startswith('{level}') or levels.load_level('{level}'))
    print('{marker}', json.dumps({{'pie_running': pie, 'loaded': bool(loaded), 'world': editor.get_editor_world().get_path_name()}}))
_td_open()
del _td_open
'''
SHOT_CODE = '''import unreal
def _td_shot():
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if world:
        unreal.SystemLibrary.execute_console_command(world, 'Shot', unreal.GameplayStatics.get_player_controller(world, 0))
_td_shot()
del _td_shot
'''


def run_editor_code(code):
    return subprocess.run([sys.executable, str(RUN_IN_EDITOR), "-c", code], capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=ROOT)


def perform_probe_action(action, **fields):
    WORK_DIRECTORY.mkdir(parents=True, exist_ok=True)
    REQUEST_PATH.write_text(json.dumps({"action": action, **fields}), encoding="utf-8")
    try:
        completed = subprocess.run([sys.executable, str(RUN_IN_EDITOR), str(PROBE_SCRIPT)], capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=ROOT)
    finally:
        REQUEST_PATH.unlink(missing_ok=True)
    for line in completed.stdout.splitlines():
        index = line.find(RESULT_MARKER)
        if index >= 0:
            return json.loads(line[index + len(RESULT_MARKER):])
    return {"error": "결과 표식이 없다", "returncode": completed.returncode, "tail": completed.stdout[-800:] + completed.stderr[-400:]}


def call_editor_tool(session, name, arguments):
    response = uemcp.call_tool(session, "call_tool", {"toolset_name": "EditorToolset.EditorAppToolset", "tool_name": name, "arguments": arguments})
    if not response or "error" in response or response.get("result", {}).get("isError"):
        raise RuntimeError(f"{name}: {str(response)[:400]}")
    return response["result"]


def open_level():
    code = OPEN_LEVEL_CODE.format(level=LEVEL_PATH, marker=RESULT_MARKER)
    completed = run_editor_code(code)
    for line in completed.stdout.splitlines():
        index = line.find(RESULT_MARKER)
        if index >= 0:
            return json.loads(line[index + len(RESULT_MARKER):])
    return {"error": completed.stdout[-600:] + completed.stderr[-300:]}


def wait_for_pie(should_run, timeout_seconds):
    started = time.monotonic()
    while time.monotonic() - started < timeout_seconds:
        if perform_probe_action("status").get("pie_running") is should_run:
            return True
        time.sleep(2.0)
    return False


def read_state():
    try:
        return json.loads(STATE_PATH.read_text(encoding="utf-8"))
    except (OSError, ValueError):
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
            return {"saved": True, "path": target.relative_to(ROOT).as_posix(), "bytes": target.stat().st_size, "source": "PIE Shot(뷰포트, 디버그 선 포함)"}
        time.sleep(0.5)
    return {"saved": False}


def drive_suite(session, suite, environment):
    try:
        call_editor_tool(session, "StartPIE", {"options": {"bSimulate": False, "warmupSeconds": 2.0}})
    except RuntimeError as error:
        environment[f"{suite}_start_pie_error"] = str(error)
        return None, {}
    captures = {}
    state = None
    try:
        if not wait_for_pie(True, PIE_START_WAIT_SECONDS):
            environment[f"{suite}_pie_error"] = "PIE 게임 월드가 나타나지 않았다"
            return None, captures
        STATE_PATH.unlink(missing_ok=True)
        started = perform_probe_action("start", suite=suite)
        environment[f"{suite}_start"] = started
        print(suite, "start", json.dumps(started, ensure_ascii=False), flush=True)
        if started.get("started") is not True:
            return read_state(), captures
        deadline = time.monotonic() + SUITE_TIMEOUT_SECONDS[suite]
        last_stage = None
        while time.monotonic() < deadline:
            state = read_state()
            if state is not None:
                if state.get("stage") != last_stage:
                    last_stage = state.get("stage")
                    print(suite, "stage", last_stage, flush=True)
                if state.get("stage") == "awaiting_capture":
                    name = state["events"][-1].get("capture")
                    if name and name not in captures:
                        captures[name] = capture_screenshot(name)
                        (WORK_DIRECTORY / f"{name}.flag").write_text("done", encoding="utf-8")
                        print(suite, "capture", name, json.dumps(captures[name], ensure_ascii=False), flush=True)
                if state.get("result") in ("complete", "failed"):
                    break
            time.sleep(POLL_SECONDS)
        else:
            environment[f"{suite}_timeout"] = True
    finally:
        try:
            call_editor_tool(session, "StopPIE", {})
        except RuntimeError as error:
            environment[f"{suite}_stop_pie_error"] = str(error)
        environment[f"{suite}_pie_ended"] = wait_for_pie(False, PIE_STOP_WAIT_SECONDS)
    return read_state(), captures


def choose_layout(floor, arguments):
    wall_x = arguments.wall_x if arguments.wall_x is not None else max(floor["min_x"] + 300.0, -3600.0)
    corridor_x = arguments.corridor_x if arguments.corridor_x is not None else 0.0
    corridor_y = arguments.corridor_y if arguments.corridor_y is not None else max(floor["min_y"] + 700.0, -4000.0)
    return wall_x, [corridor_x, corridor_y]


def export_trace(trace_path, window_seconds):
    trace_path = Path(trace_path)
    if not trace_path.exists():
        return {"error": f"트레이스 파일이 없다: {trace_path}"}
    stem = trace_path.with_suffix("")
    statistics_csv = stem.with_name(stem.name + "_timers.csv")
    events_csv = stem.with_name(stem.name + "_events.csv")
    response_file = stem.with_name(stem.name + ".rsp")
    response_file.write_text(
        f"TimingInsights.ExportTimerStatistics {statistics_csv.as_posix()} -threads=GameThread\n"
        f"TimingInsights.ExportTimingEvents {events_csv.as_posix()} -columns=ThreadName,TimerName,StartTime,EndTime,Duration -threads=GameThread,Game?Frames -timers=TDMonsterAI_*\n",
        encoding="utf-8")
    command = [str(INSIGHTS), f"-OpenTraceFile={trace_path}", "-AutoQuit", "-NoUI", "-unattended", "-nullrhi",
               f"-ABSLOG={stem.with_name(stem.name + '_insights.log')}", f"-ExecOnAnalysisCompleteCmd=@={response_file.as_posix()}"]
    if not events_csv.exists() or events_csv.stat().st_mtime < trace_path.stat().st_mtime:
        try:
            subprocess.run(command, timeout=INSIGHTS_TIMEOUT_SECONDS, capture_output=True)
        except subprocess.TimeoutExpired:
            return {"error": "UnrealInsights 분석이 제한 시간을 넘었다"}
    return {"whole_file_timers": summarize_timer_statistics(statistics_csv), "segment": summarize_timing_events(events_csv, window_seconds), "trace_bytes": trace_path.stat().st_size}


def summarize_timer_statistics(path):
    if not path.exists():
        return {"error": "타이머 통계 CSV가 없다"}
    rows = {}
    with path.open(encoding="utf-8-sig", newline="") as handle:
        for row in csv.DictReader(handle):
            name = row.get("Name", "")
            if name.startswith("TDMonsterAI_"):
                rows[name] = {"count": int(float(row["Count"])), "total_ms": round(float(row["Incl"]) * 1000.0, 3),
                              "avg_us": round(float(row["I.Avg"]) * 1e6, 2), "median_us": round(float(row["I.Med"]) * 1e6, 2), "max_us": round(float(row["I.Max"]) * 1e6, 2)}
    return rows


def percentile(values, ratio):
    if not values:
        return None
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(ratio * (len(ordered) - 1) + 0.5))]


def summarize_timing_events(path, window_seconds):
    if not path.exists():
        return {"error": "타이밍 이벤트 CSV가 없다"}
    frames = []
    timers = []
    with path.open(encoding="utf-8-sig", newline="") as handle:
        for row in csv.DictReader(handle):
            start = float(row["StartTime"])
            end = float(row["EndTime"])
            if not math.isfinite(start) or not math.isfinite(end):
                continue
            if "Frame" in row.get("ThreadName", ""):
                frames.append((start, end))
            elif row.get("TimerName", "").startswith("TDMonsterAI_"):
                timers.append((row["TimerName"], start, end - start))
    frames.sort()
    if not frames:
        return {"error": "게임 프레임 이벤트가 없다", "timer_events": len(timers)}
    window_begin = frames[-1][1] - window_seconds
    frames = [frame for frame in frames if frame[0] >= window_begin]
    frame_starts = [frame[0] for frame in frames]
    per_frame = {}
    per_call = {}
    for name, start, duration in timers:
        if start < window_begin:
            continue
        per_call.setdefault(name, []).append(duration * 1e6)
        index = bisect.bisect_right(frame_starts, start) - 1
        if index < 0 or start > frames[index][1]:
            continue
        per_frame.setdefault(name, [0.0] * len(frames))[index] += duration
    frame_ms = [(end - start) * 1000.0 for start, end in frames]
    summary = {"window_seconds": round(window_seconds, 2), "frames": len(frames), "frame_ms_mean": round(statistics.mean(frame_ms), 2),
               "frame_ms_p50": round(percentile(frame_ms, 0.5), 2), "frame_ms_p95": round(percentile(frame_ms, 0.95), 2)}
    for name in sorted(per_call):
        calls = per_call[name]
        values_ms = [value * 1000.0 for value in per_frame.get(name, [0.0])]
        summary[name] = {"calls": len(calls), "calls_per_frame": round(len(calls) / len(frames), 2), "per_call_mean_us": round(statistics.mean(calls), 1),
                         "per_call_p95_us": round(percentile(calls, 0.95), 1), "per_call_max_us": round(max(calls), 1),
                         "per_frame_mean_ms": round(statistics.mean(values_ms), 4), "per_frame_p95_ms": round(percentile(values_ms, 0.95), 4), "per_frame_max_ms": round(max(values_ms), 4)}
    return summary


def summarize_crowd_samples(path):
    counts = {}
    for line in path.read_text(encoding="utf-8").splitlines() if path.exists() else []:
        entry = json.loads(line)
        monsters, records = tactics_pie_analysis.parse_sample(entry)
        bucket = counts.setdefault(entry["scenario"], {"samples": 0, "active": [], "engaged": [], "casting": [], "records": [], "melee_budget_tokens": []})
        bucket["samples"] += 1
        bucket["active"].append(entry["active"])
        bucket["engaged"].append(sum(1 for m in monsters if m["engaged"]))
        bucket["casting"].append(sum(1 for m in monsters if m["engaged"] and m["state"] == "Cast"))
        bucket["records"].append(len(records))
        bucket["melee_budget_tokens"].extend(record["budget_m"] // 4 for record in records)
    return {name: {"samples": b["samples"], "active_max": max(b["active"]), "engaged_median": statistics.median(b["engaged"]), "engaged_max": max(b["engaged"]),
                   "casting_max": max(b["casting"]), "records_max": max(b["records"]), "melee_token_budget_max": max(b["melee_budget_tokens"]) if b["melee_budget_tokens"] else None}
            for name, b in counts.items()}


def write_evidence_samples(source):
    if not source.exists():
        return None
    target = EVIDENCE_DIRECTORY / "samples_behavior_1hz.jsonl"
    lines = [line for index, line in enumerate(source.read_text(encoding="utf-8").splitlines()) if index % EVIDENCE_SAMPLE_EVERY == 0]
    target.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return target.relative_to(ROOT).as_posix()


def analyze(report):
    behavior_samples = WORK_DIRECTORY / "samples_behavior.jsonl"
    if behavior_samples.exists():
        report["behavior"] = tactics_pie_analysis.summarize_file(behavior_samples)
        report["behavior_evidence_samples"] = write_evidence_samples(behavior_samples)
    crowd_samples = WORK_DIRECTORY / "samples_crowd.jsonl"
    if crowd_samples.exists():
        report["crowd_counts"] = summarize_crowd_samples(crowd_samples)
    traces = (report.get("crowd_state") or {}).get("traces", {})
    if traces:
        report["crowd_cost"] = {segment: export_trace(info["path"], info["real_seconds"]) for segment, info in traces.items()}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--suite", choices=("behavior", "crowd", "all"), default="all")
    parser.add_argument("--wall-x", type=float)
    parser.add_argument("--corridor-x", type=float)
    parser.add_argument("--corridor-y", type=float)
    parser.add_argument("--offline", action="store_true", help="저장된 표본·트레이스로 분석만 다시 한다")
    arguments = parser.parse_args()
    EVIDENCE_DIRECTORY.mkdir(parents=True, exist_ok=True)
    report_path = EVIDENCE_DIRECTORY / "report.json"
    if arguments.offline:
        report = json.loads(report_path.read_text(encoding="utf-8")) if report_path.exists() else {}
        analyze(report)
        report_path.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
        print(json.dumps({key: report.get(key) for key in ("crowd_counts", "crowd_cost")}, indent=2, ensure_ascii=False)[:4000])
        return 0
    environment = {}
    report = {"tool": "pie_check_tactics", "date": date.today().isoformat()}
    if arguments.suite != "all" and report_path.exists():
        report = json.loads(report_path.read_text(encoding="utf-8"))
    report.setdefault("environment_by_run", []).append(environment)
    level = open_level()
    environment["level"] = level
    if not level.get("loaded"):
        print("LV-Cambat을 열지 못했다:", json.dumps(level, ensure_ascii=False))
        return EXIT_ENVIRONMENT
    try:
        session = uemcp.connect()
    except OSError as error:
        print("MCP 서버(127.0.0.1:8000)에 연결하지 못했다:", error)
        return EXIT_ENVIRONMENT
    inspection = perform_probe_action("inspect")
    environment["inspect"] = inspection
    print("inspect", json.dumps(inspection, ensure_ascii=False), flush=True)
    floor = inspection.get("floor")
    if not floor:
        return EXIT_ENVIRONMENT
    suites = ["behavior", "crowd"] if arguments.suite == "all" else [arguments.suite]
    failed = False
    try:
        if "behavior" in suites:
            wall_x, corridor_center = choose_layout(floor, arguments)
            environment["prepare"] = perform_probe_action("prepare", wall_x=wall_x, corridor_center=corridor_center)
            print("prepare", json.dumps(environment["prepare"], ensure_ascii=False), flush=True)
        for suite in suites:
            state, captures = drive_suite(session, suite, environment)
            report[f"{suite}_state"] = state
            report[f"{suite}_captures"] = captures
            failed = failed or not state or state.get("result") != "complete"
    finally:
        environment["cleanup"] = perform_probe_action("cleanup")
        print("cleanup", json.dumps(environment["cleanup"], ensure_ascii=False), flush=True)
    analyze(report)
    report_path.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print("report", report_path.relative_to(ROOT).as_posix())
    return EXIT_FAILED if failed else 0


if __name__ == "__main__":
    sys.exit(main())
