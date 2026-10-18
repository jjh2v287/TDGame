"""실행 환경: 시스템 Python(에디터 밖). 빌드된 TDGameEditor로 헤드리스 벤치 시험 TDGame.FluidFX.Perf.StepCostByResolution을 -trace=cpu로 돌려 .utrace를 만들고, UnrealInsights -NoUI로 게임 스레드 TDFluid* 범위를 내보내 벤치 경우(모드·해상도)별 패스 비용을 집계한다. 다른 세션 빌드·에디터가 없을 때까지 기다린다(build_and_test.py와 같은 보류 규칙).
실행 명령: python Tools/FluidFX/profile_fluid_bench.py [--label fluidfx-profile] [--holder 이름] [--wait-seconds 1800] [--analyze-only]
출력: Saved/AgentOps/<YYYYMMDD>/<label>.utrace·<label>-editor.log·<label>-insights.log·<label>-events.csv·<label>-passes.json, 표준 출력에 경우별 패스 표(스텝당 ms·StepSimulation 대비 비율). 종료 코드 0 성공 / 2 실행·분석 실패 / 3 대기 시간 초과
상태: 현행
"""
import argparse
import csv
import json
import os
import re
import subprocess
import sys
from collections import defaultdict
from datetime import datetime

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from build_and_test import EDITOR_CMD_EXE, wait_for_quiet_machine
from ue_editor import ENGINE, ROOT, UPROJECT

INSIGHTS_EXE = os.path.join(ENGINE, "Engine", "Binaries", "Win64", "UnrealInsights.exe")
BENCH_TEST = "TDGame.FluidFX.Perf.StepCostByResolution"
CASE_TIMER = "TDFluidBench_Case"
STEP_TIMER = "TDFluid_StepSimulation"
BENCH_LINE = re.compile(r"TDFluidPerfBench: mode=(\w+) resolution=(\d+) avg_step_ms=([\d.]+) max_step_ms=([\d.]+) steps=(\d+)")
TEST_RESULT = re.compile(r"Test Completed\. Result=\{(\w+)\} Name=\{([^}]*)\}")


def run_traced_bench(utrace_path, editor_log_path):
    command = [
        EDITOR_CMD_EXE, UPROJECT,
        f"-ExecCmds=Automation RunTests {BENCH_TEST}",
        "-TestExit=Automation Test Queue Empty",
        "-unattended", "-NullRHI", "-NoSplash", "-NoSound",
        "-trace=cpu", f"-tracefile={utrace_path}",
        f"-abslog={editor_log_path}",
    ]
    completed = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return completed.returncode


def read_bench_lines(editor_log_path):
    cases = []
    result = None
    for line in open(editor_log_path, encoding="utf-8", errors="replace"):
        bench = BENCH_LINE.search(line)
        if bench:
            cases.append({
                "mode": bench.group(1),
                "resolution": int(bench.group(2)),
                "bench_avg_step_ms": float(bench.group(3)),
                "bench_max_step_ms": float(bench.group(4)),
            })
            continue
        tested = TEST_RESULT.search(line)
        if tested:
            result = tested.group(1)
    return cases, result


def export_timing_events(utrace_path, csv_path, insights_log_path):
    export_command = f"TimingInsights.ExportTimingEvents {csv_path} -columns=ThreadName,TimerName,StartTime,EndTime -threads=GameThread -timers=TDFluid*"
    command_line = (
        f'"{INSIGHTS_EXE}" -OpenTraceFile="{utrace_path}" -ABSLOG="{insights_log_path}" '
        f'-AutoQuit -NoUI -ExecOnAnalysisCompleteCmd="{export_command}" -log'
    )
    completed = subprocess.run(command_line, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return completed.returncode


def read_timing_events(csv_path):
    events = []
    with open(csv_path, encoding="utf-8", errors="replace", newline="") as csv_file:
        for row in csv.DictReader(csv_file):
            events.append((row["TimerName"], float(row["StartTime"]), float(row["EndTime"])))
    events.sort(key=lambda event: event[1])
    return events


def summarize_passes(events, cases):
    case_spans = [(start, end) for name, start, end in events if name == CASE_TIMER]
    summaries = []
    for case_index, (case_start, case_end) in enumerate(case_spans):
        totals = defaultdict(float)
        step_count = 0
        for name, start, end in events:
            if name == CASE_TIMER or start < case_start or end > case_end:
                continue
            totals[name] += (end - start) * 1000.0
            if name == STEP_TIMER:
                step_count += 1
        step_total = totals.get(STEP_TIMER, 0.0)
        passes = {
            name: {
                "ms_per_step": total / step_count if step_count else 0.0,
                "share_of_step": total / step_total if step_total else 0.0,
            }
            for name, total in sorted(totals.items(), key=lambda item: -item[1])
        }
        summary = {"case_index": case_index, "steps": step_count, "passes": passes}
        if case_index < len(cases):
            summary.update(cases[case_index])
        summaries.append(summary)
    return summaries


def print_summaries(summaries):
    for summary in summaries:
        print(f"[{summary.get('mode', '?')} {summary.get('resolution', '?')}] steps={summary['steps']} bench_avg_step_ms={summary.get('bench_avg_step_ms', '?')}")
        for name, values in summary["passes"].items():
            print(f"   {name:34s} {values['ms_per_step']:8.4f} ms/step  {values['share_of_step'] * 100.0:6.1f}%")


def main():
    sys.stdout.reconfigure(errors="replace")
    parser = argparse.ArgumentParser()
    parser.add_argument("--label", default="fluidfx-profile")
    parser.add_argument("--holder", default=None)
    parser.add_argument("--wait-seconds", type=int, default=1800)
    parser.add_argument("--analyze-only", action="store_true", help="이미 있는 <label>.utrace·<label>-editor.log만 분석")
    args = parser.parse_args()

    output_dir = os.path.join(ROOT, "Saved", "AgentOps", datetime.now().strftime("%Y%m%d"))
    os.makedirs(output_dir, exist_ok=True)
    utrace_path = os.path.join(output_dir, f"{args.label}.utrace")
    editor_log_path = os.path.join(output_dir, f"{args.label}-editor.log")
    insights_log_path = os.path.join(output_dir, f"{args.label}-insights.log")
    csv_path = os.path.join(output_dir, f"{args.label}-events.csv")
    json_path = os.path.join(output_dir, f"{args.label}-passes.json")

    if not args.analyze_only:
        if not wait_for_quiet_machine(args.wait_seconds, args.holder):
            return 3
        if os.path.exists(utrace_path):
            os.remove(utrace_path)
        exit_code = run_traced_bench(utrace_path, editor_log_path)
        print("BENCH editor exit", exit_code, "->", editor_log_path)

    cases, test_result = read_bench_lines(editor_log_path)
    print("BENCH test result:", test_result, "cases:", len(cases))
    if not os.path.isfile(utrace_path):
        print("trace 없음:", utrace_path)
        return 2

    if os.path.exists(csv_path):
        os.remove(csv_path)
    export_timing_events(utrace_path, csv_path, insights_log_path)
    if not os.path.isfile(csv_path):
        print("Insights 내보내기 실패 ->", insights_log_path)
        return 2

    summaries = summarize_passes(read_timing_events(csv_path), cases)
    with open(json_path, "w", encoding="utf-8") as json_file:
        json.dump({"bench_test_result": test_result, "cases": summaries}, json_file, ensure_ascii=False, indent=1)
    print_summaries(summaries)
    print("PASSES ->", json_path)
    return 0 if summaries and test_result is not None else 2


if __name__ == "__main__":
    sys.exit(main())
