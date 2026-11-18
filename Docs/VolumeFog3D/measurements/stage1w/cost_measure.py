"""실행 환경: 파이썬 3 + numpy, 작업 폴더 C:/Project/TDGame (PYTHONUTF8=1 python -B), 에디터 불필요(실행 뒤 로그·CSV 만 읽음)
실행 명령: python -B Docs/VolumeFog3D/measurements/stage1w/cost_measure.py
출력: Docs/VolumeFog3D/measurements/stage1w/cost.json·cost.md (정보 표, 판정 없음)
상태: 1 마무리 S2(prereg-1c-wrapup.md 2절 E). 실행 C_<조건>_r<회차>(Saved/FluidFX/fog3d_calib)의 csvprofile CSV 둘(서 있음·되풀이)과 ProfileGPU 표 14개를 읽는다."""
import csv
import json
import re
import statistics
import sys
from pathlib import Path

import numpy

ROOT = Path("C:/Project/TDGame")
CAL = ROOT / "Saved/FluidFX/fog3d_calib"
OUT = Path(__file__).resolve().parent
WORK = ROOT / "Saved/AgentOps/20261114/fog3d/stage1w/s2"
PROFILING_DIRECTORY = ROOT / "Saved/Profiling/CSV"
CONDITION_ORDER = ["h", "m0", "q0", "q1", "q2"]
CONDITION_LABELS = {"h": "구역 숨김", "m0": "계산 끔(그리기만)", "q0": "낮음(128칸·30회/초)", "q1": "보통(256칸·30회/초)", "q2": "높음(256칸·60회/초)"}
PHASES = ["still", "loop"]
PHASE_LABELS = {"still": "서 있음(8초)", "loop": "걷기·휘두르기 되풀이(16.5초)"}
STILL_DUMP_COUNT = 6
DISCARD_FRAMES = 5
MINIMUM_FRAMES = 300
PASS_PREFIX = "TDFluidGpu.Fog3D."


def read_csv_columns(path):
    rows = list(csv.reader(open(path, encoding="utf-8", errors="replace", newline="")))
    metadata_index = next((index for index, row in enumerate(rows) if row and row[0].startswith("[")), len(rows))
    header = [name.strip() for name in rows[metadata_index - 1]]
    data = rows[1:metadata_index - 1]
    columns = {}
    for index, name in enumerate(header):
        values = []
        for row in data:
            if index < len(row) and row[index] != "":
                try:
                    values.append(float(row[index]))
                except ValueError:
                    values.append(float("nan"))
        columns[name] = values
    return columns, len(data)


def describe(values):
    array = numpy.asarray(values[DISCARD_FRAMES:], dtype=float)
    array = array[~numpy.isnan(array)]
    return {"frames": int(array.size), "median": float(numpy.median(array)), "mean": float(array.mean()), "p95": float(numpy.percentile(array, 95)), "max": float(array.max())}


def csv_stats(path):
    columns, rows = read_csv_columns(path)
    result = {"file": Path(path).name, "rows": rows}
    if "GPUTime" in columns:
        series = numpy.asarray(columns["GPUTime"][DISCARD_FRAMES:], dtype=float)
        series = series[~numpy.isnan(series)]
        even_mean, odd_mean = float(series[0::2].mean()), float(series[1::2].mean())
        result["alternation"] = {"even_mean": even_mean, "odd_mean": odd_mean, "abs_difference": abs(even_mean - odd_mean)}
    for key, name in (("gpu_time", "GPUTime"), ("frame_time", "FrameTime"), ("volumetric_fog", "GPU/VolumetricFog"), ("translucency", "GPU/Translucency"), ("fluid_solver", "GPU/TDFluidGpuSolver")):
        if name in columns:
            result[key] = describe(columns[name])
    return result


def log_lines(run):
    return (CAL / run / "log_slice.txt").read_text(encoding="utf-8", errors="replace").splitlines()


def csv_files(lines):
    names = []
    for line in lines:
        match = re.search(r"Writing CSV to file : .*?/(Profile\([0-9_]+\)\.csv)", line)
        if match:
            names.append(match.group(1))
    return names


def parse_dumps(lines):
    dumps = []
    current = None
    for line in lines:
        if "LogRHI" not in line or "┃" not in line:
            continue
        parts = line.split("┃")
        if len(parts) < 4:
            continue
        if "Exclusive" in parts[1]:
            current = {"passes": {}, "scope": None, "volumetric": None, "frame": None}
            dumps.append(current)
            continue
        if current is None:
            continue
        name = parts[3].strip()
        time_match = re.search(r"([0-9.]+) ms", parts[2])
        if not name or not time_match:
            continue
        value = float(time_match.group(1))
        if name.startswith("TDFluidGpuFog3D") and current["scope"] is None:
            current["scope"] = value
        elif name == "VolumetricFog" and current["volumetric"] is None:
            current["volumetric"] = value
        elif name.startswith("Frame ") and current["frame"] is None:
            current["frame"] = value
        elif name.startswith(PASS_PREFIX):
            current["passes"].setdefault(name, 0.0)
            current["passes"][name] += value
    return dumps


def summarize_dumps(dumps):
    result = {"dumps": len(dumps)}
    for key in ("frame", "scope", "volumetric"):
        values = [dump[key] for dump in dumps if dump[key] is not None]
        result[key] = {"present": len(values), "median": float(numpy.median(values)) if values else None, "values": values}
    with_scope = [dump["frame"] for dump in dumps if dump["scope"] is not None and dump["frame"] is not None]
    without_scope = [dump["frame"] for dump in dumps if dump["scope"] is None and dump["frame"] is not None]
    result["frame_with_scope_median"] = float(numpy.median(with_scope)) if with_scope else None
    result["frame_without_scope_median"] = float(numpy.median(without_scope)) if without_scope else None
    names = sorted({name for dump in dumps for name in dump["passes"]})
    result["passes_median"] = {name: float(numpy.median([dump["passes"].get(name, 0.0) for dump in dumps if dump["scope"] is not None] or [0.0])) for name in names}
    result["passes_sum_median"] = float(numpy.median([sum(dump["passes"].values()) for dump in dumps if dump["scope"] is not None] or [0.0]))
    return result


def check_run(run, condition):
    directory = CAL / run
    problems = []
    status = json.loads((directory / "capture/00_c_schedule.json").read_text(encoding="utf-8"))
    record = json.loads((directory / "run.json").read_text(encoding="utf-8"))
    events_done = len(status.get("events_done", []))
    if events_done != len(record["events"]):
        problems.append(f"이벤트 {events_done}/{len(record['events'])}")
    if not status.get("done") or status.get("error"):
        problems.append(f"예약 미완료 또는 오류 {status.get('error')}")
    walks = [event for event in status["events_done"] if "move" in event and event["move"].get("mode") == "input"]
    if any(event["move"].get("timed_out") for event in walks):
        problems.append("걷기 시한 초과")
    state_path = WORK / f"state_{run}.json"
    state = json.loads(state_path.read_text(encoding="utf-8")) if state_path.exists() else {}
    expected_quality = {"h": 2, "m0": 2, "q0": 0, "q1": 1, "q2": 2}[condition]
    expected_mode = {"h": 0, "m0": 0}.get(condition, 2)
    if state.get("TD.Fog3D.Quality") != expected_quality:
        problems.append(f"품질 읽기값 {state.get('TD.Fog3D.Quality')} (기대 {expected_quality})")
    if state.get("TD.Fog3D.Mode") != expected_mode:
        problems.append(f"모드 읽기값 {state.get('TD.Fog3D.Mode')} (기대 {expected_mode})")
    lines = log_lines(run)
    summary = [re.search(r"submitted=(\d+) rejected=(\d+)", line) for line in lines if "Fog3D zone summary" in line]
    submitted = int(summary[-1].group(1)) if summary and summary[-1] else None
    rejected = int(summary[-1].group(2)) if summary and summary[-1] else None
    warnings = [line for line in lines if re.search(r"LogTDFog3D\w*: (Warning|Error)", line)]
    if rejected:
        problems.append(f"제출 거부 {rejected}")
    if warnings:
        problems.append(f"3D 안개 경고·오류 {len(warnings)}줄")
    return {"valid": not problems, "problems": problems, "events_done": events_done, "submitted": submitted, "rejected": rejected, "state_target": state.get("state_target"), "quality_read": state.get("TD.Fog3D.Quality"), "mode_read": state.get("TD.Fog3D.Mode"),
            "viewport_size": state.get("viewport_size")}


def measure_run(run, condition):
    lines = log_lines(run)
    names = csv_files(lines)
    row = {"run": run, "condition": condition, "check": check_run(run, condition), "csv_files": names}
    if len(names) != 2:
        row["check"]["valid"] = False
        row["check"]["problems"].append(f"CSV {len(names)}개 (기대 2)")
        return row
    dumps = parse_dumps(lines)
    still_dumps, loop_dumps = dumps[:STILL_DUMP_COUNT], dumps[STILL_DUMP_COUNT:]
    for phase, name, phase_dumps in (("still", names[0], still_dumps), ("loop", names[1], loop_dumps)):
        stats = csv_stats(PROFILING_DIRECTORY / name)
        stats["profile_gpu"] = summarize_dumps(phase_dumps)
        if stats["gpu_time"]["frames"] < MINIMUM_FRAMES:
            row["check"]["valid"] = False
            row["check"]["problems"].append(f"{phase} 프레임 {stats['gpu_time']['frames']} < {MINIMUM_FRAMES}")
        row[phase] = stats
    return row


def fmt(value, digits=2):
    return "-" if value is None else f"{value:.{digits}f}"


def build_markdown(data):
    lines = ["# 3D 안개 전체 비용 (정보, 판정 없음)", "",
             "측정 조건(등록본 prereg-1c-wrapup.md 2절 E): 에디터 안 PIE, 무대 LV_TDFog3DStage, 옆빛, 새 기본값, 카메라 팔 400cm·내려봄 12도. `t.MaxFPS 60`, `r.VSync 0`, `r.ScreenPercentage 100`, 동적 해상도 끔. "
             f"화면 크기와 그래픽 카드는 아래 환경 줄 참고. 값은 에디터 안에서 잰 것이라 실제 게임 빌드보다 클 수 있다.", "",
             f"환경: {data['environment']}", ""]
    for phase in PHASES:
        lines += [f"## {PHASE_LABELS[phase]}", "",
                  "| 조건 | GPU 프레임 시간 중앙값 ms (1회/2회) | 평균 ms (1회/2회) | 95번째 백분위 ms (1회/2회) | 프레임 수 (1회/2회) | 두 번 중앙값 차이 ms | 엔진 볼륨 안개 CSV 중앙값 ms (1회/2회) |",
                  "|---|---|---|---|---|---|---|"]
        for condition in CONDITION_ORDER:
            runs = [data["runs"].get(f"C_{condition}_r{repetition}") for repetition in (1, 2)]
            cells = [[fmt(r[phase]["gpu_time"][key]) if r and phase in r else "-" for r in runs] for key in ("median", "mean", "p95")]
            frames = [str(r[phase]["gpu_time"]["frames"]) if r and phase in r else "-" for r in runs]
            medians = [r[phase]["gpu_time"]["median"] for r in runs if r and phase in r]
            difference = fmt(abs(medians[0] - medians[1])) if len(medians) == 2 else "-"
            volumetric = [fmt(r[phase]["volumetric_fog"]["median"], 3) if r and phase in r and "volumetric_fog" in r[phase] else "-" for r in runs]
            valid = "" if all(r and r["check"]["valid"] for r in runs) else " (무효 실행 있음)"
            lines.append(f"| {CONDITION_LABELS[condition]}{valid} | {' / '.join(cells[0])} | {' / '.join(cells[1])} | {' / '.join(cells[2])} | {' / '.join(frames)} | {difference} | {' / '.join(volumetric)} |")
        lines.append("")
        lines += [f"### {PHASE_LABELS[phase]}: ProfileGPU 표(프레임 하나씩, 6~8개 중앙값)", "",
                  "| 조건 | 3D 안개 계산 범위 합(포함 시간) ms (1회/2회) | 그 범위가 찍힌 표 수 | 패스 이름 합 ms (1회/2회) | 엔진 볼륨 안개 ms (1회/2회) | 표의 Frame ms (1회/2회) |", "|---|---|---|---|---|---|"]
        for condition in CONDITION_ORDER:
            runs = [data["runs"].get(f"C_{condition}_r{repetition}") for repetition in (1, 2)]
            def pick(r, key, sub="median"):
                return fmt(r[phase]["profile_gpu"][key][sub], 3) if r and phase in r and r[phase]["profile_gpu"][key][sub] is not None else "-"
            present = [f"{r[phase]['profile_gpu']['scope']['present']}/{r[phase]['profile_gpu']['dumps']}" if r and phase in r else "-" for r in runs]
            passes = [fmt(r[phase]["profile_gpu"]["passes_sum_median"], 3) if r and phase in r else "-" for r in runs]
            lines.append(f"| {CONDITION_LABELS[condition]} | {' / '.join(pick(r, 'scope') for r in runs)} | {' / '.join(present)} | {' / '.join(passes)} | {' / '.join(pick(r, 'volumetric') for r in runs)} | {' / '.join(pick(r, 'frame') for r in runs)} |")
        lines.append("")
    lines += ["## 조건 사이 차이(두 번의 평균 GPU 프레임 시간끼리 뺌)", "",
              "| 구간 | 그리기(계산 끔 − 구역 숨김) ms | 낮음 − 계산 끔 ms | 보통 − 계산 끔 ms | 높음 − 계산 끔 ms |", "|---|---|---|---|---|"]
    for phase in PHASES:
        def mean_of(condition):
            values = [data["runs"][f"C_{condition}_r{r}"][phase]["gpu_time"]["mean"] for r in (1, 2) if f"C_{condition}_r{r}" in data["runs"]]
            return sum(values) / len(values) if values else None
        base = mean_of("m0")
        cells = [fmt(mean_of("m0") - mean_of("h"))] + [fmt(mean_of(c) - base) for c in ("q0", "q1", "q2")]
        lines.append(f"| {PHASE_LABELS[phase]} | " + " | ".join(cells) + " |")
    lines += ["", "## 계산이 도는 프레임과 안 도는 프레임의 차이(짝수·홀수 프레임 평균 GPU 시간 차이의 절댓값, 초당 30회 조건은 한 프레임 걸러 계산)", "",
              "| 조건 | 서 있음 ms (1회/2회) | 되풀이 ms (1회/2회) |", "|---|---|---|"]
    for condition in CONDITION_ORDER:
        cells = []
        for phase in PHASES:
            cells.append(" / ".join(fmt(data["runs"][f"C_{condition}_r{r}"][phase]["alternation"]["abs_difference"], 3) if f"C_{condition}_r{r}" in data["runs"] else "-" for r in (1, 2)))
        lines.append(f"| {CONDITION_LABELS[condition]} | {cells[0]} | {cells[1]} |")
    lines.append("")
    extras = [data["runs"][f"C_{c}_r3"] for c in ("q0", "q1") if f"C_{c}_r3" in data["runs"]]
    if extras:
        lines += ["## 보강 실행(낮음·보통, ProfileGPU 표 시각을 한 프레임씩 엇갈림, 계산이 도는 프레임을 잡으려는 것)", "",
                  "| 조건 | 구간 | 표 수 | 계산 범위가 찍힌 표 | 계산 범위 합 ms(찍힌 표 중앙값) | 패스 이름 합 ms | 계산 도는 표의 Frame ms 중앙값 | 안 도는 표의 Frame ms 중앙값 |", "|---|---|---|---|---|---|---|---|"]
        for row in extras:
            for phase in PHASES:
                summary = row[phase]["profile_gpu"]
                hit_frames = [dump_value for dump_value in summary["scope"]["values"]]
                lines.append(f"| {CONDITION_LABELS[row['condition']]} | {PHASE_LABELS[phase]} | {summary['dumps']} | {summary['scope']['present']} | {fmt(summary['scope']['median'], 3)} | {fmt(summary['passes_sum_median'], 3)} | {fmt(summary['frame_with_scope_median'], 3)} | {fmt(summary['frame_without_scope_median'], 3)} |")
        lines.append("")
    lines += ["## 계획 3단계 목표와 나란히(판정하지 않음)", "",
              "- 계획 문서 7절 3단계 줄: 걷기만 0.3 ms, 보통 싸움 0.8 ms, 최악 1.5 ms 이하. 계획 3절 예산 제안: 계산 평상시 0.3 ms, 최악 1.0 ms, 그리기 추가 0.1 ms(엔진 안개를 쓰는 동안).",
              "- 줄이는 일은 3단계 몫이라 이 문서는 어느 것도 통과·실패로 적지 않는다.", ""]
    lines += data["notes"]
    return "\n".join(lines) + "\n"


def run_all():
    runs = {}
    for condition in CONDITION_ORDER:
        for repetition in (1, 2, 3):
            name = f"C_{condition}_r{repetition}"
            if not (CAL / name / "log_slice.txt").exists():
                print(name, "없음")
                continue
            runs[name] = measure_run(name, condition)
            row = runs[name]
            print(name, "valid", row["check"]["valid"], row["check"]["problems"], {phase: (round(row[phase]["gpu_time"]["median"], 2), row[phase]["gpu_time"]["frames"], row[phase]["profile_gpu"]["scope"]["median"]) for phase in PHASES if phase in row})
    environment = json.loads((WORK / "environment.json").read_text(encoding="utf-8")) if (WORK / "environment.json").exists() else {}
    notes = json.loads((WORK / "cost_notes.json").read_text(encoding="utf-8")) if (WORK / "cost_notes.json").exists() else []
    data = {"runs": runs, "environment": environment.get("summary", "(환경 정보 파일 없음)"), "environment_detail": environment, "notes": notes}
    (OUT / "cost.json").write_text(json.dumps({key: value for key, value in data.items() if key != "notes"} | {"notes": notes}, ensure_ascii=False, indent=2, default=str), encoding="utf-8")
    (OUT / "cost.md").write_text(build_markdown(data), encoding="utf-8")


if __name__ == "__main__":
    run_all()
