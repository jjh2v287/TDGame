"""실행 환경: 시스템 Python(numpy·Pillow·scipy). analyze_floor_popping.py 를 마친 run 폴더(analysis.json·profile.csv·frames.csv·grabs.csv·plan.json)를 읽는다.
실행 명령: python -B Tools/Gameplay/judge_floor_popping.py --walk-base A B --walk-final C D --tp-base E F --tp-final G H --out <폴더> [--title 제목]
출력: <폴더>/judgment.json·judgment.md — GF-07 등록 판정선(걷기 팝 사건·순간이동 정착·늦은 시간·회귀 없음)과 보조 지표(멈춤 선명도 뜀·걷기 선명도·순간이동 도착 화면 변화·도착 스트리밍·게임 스레드 범주 귀속)
상태: GF-07(2026-11-20). 판정선은 Docs/Validation/Gameplay/GF-07-floor-popping.md 3절 그대로이며 보조 지표는 판정을 바꾸지 않는다.
"""
import argparse
import csv
import json
import os
import statistics
import sys

import numpy as np
from PIL import Image
from scipy.ndimage import laplace

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze_floor_popping as floor_analyzer

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RUN_BASE = os.path.join(ROOT, "Saved", "Gameplay", "FloorPopping")
GRAB_LATENCY_SECONDS = 0.08
SHARPNESS_ROWS = (0.45, 0.88)
HUD_MASK = (0.38, 0.88, 0.62, 1.0)
ARRIVAL_CELL_PIXELS = 16
ARRIVAL_CELL_THRESHOLD = 0.15
ARRIVAL_PROBE_SECONDS = (0.1, 0.2, 0.3, 0.5, 1.0, 2.0)
PENDING_BUSY_MB = 0.5
LONG_FRAME_MS = 100.0
EVENT_RATIO_LIMIT = 0.3
SETTLE_RATIO_LIMIT = 0.5
SETTLE_ABSOLUTE_LIMIT_S = 0.5
FRAME_REGRESSION_RATIO = 0.05
FRAME_REGRESSION_MS = 1.5
MEMORY_CEILING_MIB = 5632.0
MEMORY_INCREASE_LIMIT_MIB = 1024.0
LATE_MINIMUM_EVENTS = 5
CHANGE_SITE_CATEGORIES = ("Tickables", "TickActors", "Camera", "RenderAssetStreaming", "EventWait/RenderAssetStreaming")


def run_path(run, name):
    return os.path.join(RUN_BASE, run, name)


def load_json(path):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def load_frame_rows(run):
    with open(run_path(run, "frames.csv"), encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    return np.array([float(row["real_epoch"]) for row in rows]), np.array([int(row["seg"]) for row in rows])


def load_grab_rows(run):
    with open(run_path(run, "grabs.csv"), encoding="utf-8") as handle:
        grabs = list(csv.DictReader(handle))
    middles = np.array([(float(grab["epoch_start"]) + float(grab["epoch_end"])) / 2.0 for grab in grabs]) - GRAB_LATENCY_SECONDS
    return [grab["path"] for grab in grabs], middles


def mean_or_none(values):
    present = [value for value in values if value is not None]
    return float(np.mean(present)) if present else None


def profile_columns_by_kind(run, analysis, kind):
    header, rows = floor_analyzer.read_profile_csv(run_path(run, "profile.csv"))
    frames = floor_analyzer.load_frames_table(run_path(run, "frames.csv"))
    plan = load_json(run_path(run, "plan.json"))
    kind_of_segment = {segment["index"]: segment["kind"] for segment in plan["segments"]}
    frame_rows = np.arange(len(rows)) + analysis["profile"]["align_offset_rows"]
    inside = (frame_rows >= 0) & (frame_rows < len(frames["seg"]))
    segments = np.where(inside, frames["seg"][np.clip(frame_rows, 0, len(frames["seg"]) - 1)], -1)
    selected = np.array([kind_of_segment.get(int(segment)) == kind for segment in segments])
    columns = {}
    for name in header:
        if not (name.startswith("Exclusive/GameThread/") or name in ("GameThreadTime", "FrameTime", "GPUTime")):
            continue
        values = floor_analyzer.column_values(header, rows, name)
        if values is not None:
            columns[name] = np.nan_to_num(values[selected])
    return columns


def summarize_game_thread(columns):
    game_thread = columns["GameThreadTime"]
    categories = [name for name in columns if name.startswith("Exclusive/GameThread/")]
    unaccounted = game_thread - sum(columns[name] for name in categories)
    change_sites = {name: float(np.mean(columns["Exclusive/GameThread/" + name])) for name in CHANGE_SITE_CATEGORIES if "Exclusive/GameThread/" + name in columns}
    return {
        "game_thread_p50": float(np.percentile(game_thread, 50)),
        "game_thread_p95": float(np.percentile(game_thread, 95)),
        "unaccounted_p50": float(np.percentile(unaccounted, 50)),
        "unaccounted_p95": float(np.percentile(unaccounted, 95)),
        "change_site_means_ms": change_sites,
    }


def summarize_run(run, kind):
    analysis = load_json(run_path(run, "analysis.json"))
    columns = profile_columns_by_kind(run, analysis, kind)
    events = analysis["events"]
    settle_times = [segment["settle_time_s"] for segment in analysis["stare"]]
    profile_kind = analysis["profile"]["by_kind"].get(kind, {})
    return {
        "run": run,
        "event_count": analysis["event_summary"]["count"],
        "events_per_minute": analysis["event_summary"]["per_minute"],
        "late_seconds": [event["late_s"] for event in events],
        "late_p90_s": analysis["event_summary"]["late_p90_s"],
        "settle_times_s": settle_times,
        "settle_median_s": statistics.median(settle_times) if settle_times else None,
        "frame_time_p95_ms": profile_kind.get("FrameTime", {}).get("p95"),
        "gpu_time_p95_ms": profile_kind.get("GPUTime", {}).get("p95"),
        "gpu_memory_max_mib": analysis["gpu"]["memory_used_mib_max"],
        "long_frames": int((columns["FrameTime"] > LONG_FRAME_MS).sum()),
        "pending_busy_ratio": analysis["profile"].get("pending_busy_time_ratio"),
        "pending_busy_longest_s": analysis["profile"].get("pending_busy_longest_s"),
        "game_thread": summarize_game_thread(columns),
    }


def judge_events(walk_base, walk_final):
    base = mean_or_none([run["events_per_minute"] for run in walk_base])
    final = mean_or_none([run["events_per_minute"] for run in walk_final])
    passed = base is not None and final is not None and final <= EVENT_RATIO_LIMIT * base
    return {"line": "걷는 1분당 팝 사건 ≤ 기준선 평균의 30%", "base": base, "final": final, "passed": passed, "event_counts": {"base": [run["event_count"] for run in walk_base], "final": [run["event_count"] for run in walk_final]}}


def judge_settle(tp_base, tp_final):
    base = mean_or_none([run["settle_median_s"] for run in tp_base])
    final = mean_or_none([run["settle_median_s"] for run in tp_final])
    passed = final is not None and base is not None and (final <= SETTLE_RATIO_LIMIT * base or final <= SETTLE_ABSOLUTE_LIMIT_S)
    return {"line": "순간이동 응시 정착 시간 중앙값 ≤ 기준선의 50% 또는 0.5초", "base": base, "final": final, "passed": passed}


def judge_late(walk_base, walk_final):
    base_runs = [run["late_p90_s"] for run in walk_base]
    final_runs = [run["late_p90_s"] for run in walk_final]
    base_events = sum(len(run["late_seconds"]) for run in walk_base)
    final_events = sum(len(run["late_seconds"]) for run in walk_final)
    base = mean_or_none(base_runs)
    final = mean_or_none(final_runs)
    enough = base_events >= LATE_MINIMUM_EVENTS and final_events >= LATE_MINIMUM_EVENTS
    verdict = "판정 불가(사건 부족)" if not enough else ("통과" if final is not None and base is not None and final < base else "미충족")
    return {"line": "늦은 시간 90분위가 줄어듦", "base": base, "final": final, "base_events": base_events, "final_events": final_events, "verdict": verdict}


def judge_regression(walk_base, walk_final):
    def mean_of(runs, key):
        return mean_or_none([run[key] for run in runs])

    base_frame, final_frame = mean_of(walk_base, "frame_time_p95_ms"), mean_of(walk_final, "frame_time_p95_ms")
    base_gpu, final_gpu = mean_of(walk_base, "gpu_time_p95_ms"), mean_of(walk_final, "gpu_time_p95_ms")
    base_memory, final_memory = mean_of(walk_base, "gpu_memory_max_mib"), mean_of(walk_final, "gpu_memory_max_mib")
    final_memory_peak = max(run["gpu_memory_max_mib"] for run in walk_final)
    base_long, final_long = sum(run["long_frames"] for run in walk_base), sum(run["long_frames"] for run in walk_final)
    frame_limit = max(FRAME_REGRESSION_RATIO * base_frame, FRAME_REGRESSION_MS)
    gpu_limit = max(FRAME_REGRESSION_RATIO * base_gpu, FRAME_REGRESSION_MS)
    return {
        "frame_time_p95": {"base": base_frame, "final": final_frame, "limit_increase": frame_limit, "passed": final_frame - base_frame <= frame_limit},
        "gpu_time_p95": {"base": base_gpu, "final": final_gpu, "limit_increase": gpu_limit, "passed": final_gpu - base_gpu <= gpu_limit},
        "gpu_memory": {"base": base_memory, "final": final_memory, "final_peak": final_memory_peak, "passed": final_memory_peak <= MEMORY_CEILING_MIB and final_memory - base_memory <= MEMORY_INCREASE_LIMIT_MIB},
        "long_frames": {"base": base_long, "final": final_long, "passed": final_long <= base_long},
    }


def sharpness_of(path):
    gray = np.asarray(Image.open(path).convert("L"), dtype=np.float32) / 255.0
    height = gray.shape[0]
    return float(np.mean(np.abs(laplace(gray[int(height * SHARPNESS_ROWS[0]):int(height * SHARPNESS_ROWS[1]), :]))))


def measure_walk_sharpness(run):
    epochs, segments = load_frame_rows(run)
    paths, middles = load_grab_rows(run)
    rows = np.clip(np.searchsorted(epochs, middles) - 1, 0, len(epochs) - 1)
    walking = [sharpness_of(paths[index]) for index in range(len(paths)) if segments[rows[index]] == 1]
    stop_epoch = epochs[np.argmax(segments == 2)]
    moving = [sharpness_of(paths[index]) for index in range(len(paths)) if -1.0 <= middles[index] - stop_epoch < -0.1]
    stopped = [sharpness_of(paths[index]) for index in range(len(paths)) if 0.6 <= middles[index] - stop_epoch <= 1.5]
    if not walking or not moving or not stopped:
        return None
    return {"walking_sharpness_median": float(np.median(walking)), "stop_jump_ratio": float(np.mean(stopped) / np.mean(moving))}


def load_masked_image(path):
    image = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32) / 255.0
    height, width = image.shape[:2]
    image[int(HUD_MASK[1] * height):, int(HUD_MASK[0] * width):int(HUD_MASK[2] * width)] = 0.0
    return image


def changed_cell_fraction(image, reference):
    height, width = reference.shape[:2]
    rows, columns = height // ARRIVAL_CELL_PIXELS, width // ARRIVAL_CELL_PIXELS
    difference = np.abs(image - reference).mean(axis=2)[:rows * ARRIVAL_CELL_PIXELS, :columns * ARRIVAL_CELL_PIXELS]
    cells = difference.reshape(rows, ARRIVAL_CELL_PIXELS, columns, ARRIVAL_CELL_PIXELS).mean(axis=(1, 3))
    return float((cells > ARRIVAL_CELL_THRESHOLD).mean())


def measure_teleport_arrivals(run):
    analysis = load_json(run_path(run, "analysis.json"))
    plan = load_json(run_path(run, "plan.json"))
    epochs, segments = load_frame_rows(run)
    paths, middles = load_grab_rows(run)
    header, rows = floor_analyzer.read_profile_csv(run_path(run, "profile.csv"))
    pending = np.nan_to_num(floor_analyzer.column_values(header, rows, "TextureStreaming/PendingStreamInData"))
    offset = analysis["profile"]["align_offset_rows"]
    kind_of_segment = {segment["index"]: segment["kind"] for segment in plan["segments"]}
    arrivals = []
    for segment in plan["segments"]:
        if segment["kind"] != "stare" or kind_of_segment.get(segment["index"] - 1) != "teleport":
            continue
        frame_rows = np.flatnonzero(segments == segment["index"])
        if frame_rows.size == 0:
            continue
        start, end = epochs[frame_rows[0]], epochs[frame_rows[-1]]
        picked = [index for index in range(len(paths)) if start <= middles[index] <= end]
        if len(picked) < 6:
            continue
        reference = np.mean([load_masked_image(paths[index]) for index in picked[-4:]], axis=0)
        curve = {}
        for probe in ARRIVAL_PROBE_SECONDS:
            nearest = min(picked, key=lambda index: abs(middles[index] - start - probe))
            curve[str(probe)] = changed_cell_fraction(load_masked_image(paths[nearest]), reference)
        late = min(picked, key=lambda index: abs(middles[index] - (end - 1.0)))
        profile_rows = np.arange(frame_rows[0], frame_rows[-1] + 1) - offset
        profile_rows = profile_rows[(profile_rows >= 0) & (profile_rows < len(pending))]
        window = pending[profile_rows]
        busy = np.flatnonzero(window > PENDING_BUSY_MB)
        pending_until = float(epochs[min(frame_rows[0] + busy[-1], frame_rows[-1])] - start) if busy.size else 0.0
        arrivals.append({
            "seg": segment["index"],
            "changed_at": curve,
            "noise_end_minus_1s": changed_cell_fraction(load_masked_image(paths[late]), reference),
            "pending_peak_mb": float(window.max()) if window.size else 0.0,
            "pending_until_s": pending_until,
        })
    return arrivals


def summarize_arrivals(runs_arrivals):
    means = {}
    for probe in ARRIVAL_PROBE_SECONDS:
        values = [arrival["changed_at"][str(probe)] for arrivals in runs_arrivals for arrival in arrivals]
        means[str(probe)] = float(np.mean(values)) if values else None
    noise = [arrival["noise_end_minus_1s"] for arrivals in runs_arrivals for arrival in arrivals]
    peaks = [arrival["pending_peak_mb"] for arrivals in runs_arrivals for arrival in arrivals]
    until = [arrival["pending_until_s"] for arrivals in runs_arrivals for arrival in arrivals]
    peak_sums = [sum(arrival["pending_peak_mb"] for arrival in arrivals) for arrivals in runs_arrivals]
    waiting_spots = [sum(1 for arrival in arrivals if arrival["pending_peak_mb"] > PENDING_BUSY_MB) for arrivals in runs_arrivals]
    return {"changed_at_mean": means, "noise_mean": float(np.mean(noise)) if noise else None, "pending_peak_mb_max": max(peaks) if peaks else None, "pending_until_s_max": max(until) if until else None,
            "pending_peak_mb_sum_mean": float(np.mean(peak_sums)) if peak_sums else None, "waiting_spots_mean": float(np.mean(waiting_spots)) if waiting_spots else None}


def format_value(value, digits=3):
    if value is None:
        return "–"
    if isinstance(value, bool):
        return "통과" if value else "미충족"
    return f"{value:.{digits}f}"


def build_markdown(title, report):
    registered = report["registered"]
    regression = registered["regression"]
    lines = [f"# {title}", "", "## 등록 판정선 (기준선 2회 평균 대 수정 2회 평균)", "", "| 판정선 | 기준선 | 수정 | 결과 |", "|---|---|---|---|"]
    events = registered["events"]
    lines.append(f"| {events['line']} (사건 수 {events['event_counts']['base']} → {events['event_counts']['final']}) | {format_value(events['base'])} | {format_value(events['final'])} | {format_value(events['passed'])} |")
    settle = registered["settle"]
    lines.append(f"| {settle['line']} | {format_value(settle['base'], 2)} | {format_value(settle['final'], 2)} | {format_value(settle['passed'])} |")
    late = registered["late"]
    lines.append(f"| {late['line']} (사건 {late['base_events']} → {late['final_events']}) | {format_value(late['base'], 2)} | {format_value(late['final'], 2)} | {late['verdict']} |")
    lines.append(f"| 걷기 FrameTime p95 증가 ≤ {format_value(regression['frame_time_p95']['limit_increase'], 2)}ms | {format_value(regression['frame_time_p95']['base'], 2)} | {format_value(regression['frame_time_p95']['final'], 2)} | {format_value(regression['frame_time_p95']['passed'])} |")
    lines.append(f"| 걷기 GPUTime p95 증가 ≤ {format_value(regression['gpu_time_p95']['limit_increase'], 2)}ms | {format_value(regression['gpu_time_p95']['base'], 2)} | {format_value(regression['gpu_time_p95']['final'], 2)} | {format_value(regression['gpu_time_p95']['passed'])} |")
    lines.append(f"| GPU 메모리 최대 ≤ 5,632MiB·증가 ≤ 1,024MiB | {format_value(regression['gpu_memory']['base'], 0)} | {format_value(regression['gpu_memory']['final'], 0)} (최대 {format_value(regression['gpu_memory']['final_peak'], 0)}) | {format_value(regression['gpu_memory']['passed'])} |")
    lines.append(f"| 100ms 넘는 걷기 프레임 수 늘지 않음 | {regression['long_frames']['base']} | {regression['long_frames']['final']} | {format_value(regression['long_frames']['passed'])} |")
    lines += ["", "## 실행별", "", "| 실행 | 팝/분 | 정착 중앙값 | 정착 시간(지점별) | FrameTime p95 | GPUTime p95 | GT p95 | 범주 밖 GT p95 | 변경 코드 범주 합(ms) | GPU 메모리 최대 |", "|---|---|---|---|---|---|---|---|---|---|"]
    for group in ("walk_base", "walk_final", "tp_base", "tp_final"):
        for run in report["runs"][group]:
            game_thread = run["game_thread"]
            change_sum = sum(game_thread["change_site_means_ms"].values())
            settle_list = "·".join(f"{value:.2f}" for value in run["settle_times_s"])
            lines.append(f"| {run['run']} | {format_value(run['events_per_minute'], 2)} | {format_value(run['settle_median_s'], 2)} | {settle_list} | {format_value(run['frame_time_p95_ms'], 2)} | {format_value(run['gpu_time_p95_ms'], 2)} | {format_value(game_thread['game_thread_p95'], 2)} | {format_value(game_thread['unaccounted_p95'], 2)} | {change_sum:.2f} | {format_value(run['gpu_memory_max_mib'], 0)} |")
    auxiliary = report["auxiliary"]
    lines += ["", "## 보조 지표 (판정을 바꾸지 않음)", "", "| 지표 | 기준선 | 수정 |", "|---|---|---|"]
    lines.append(f"| 멈춤 선명도 뜀(멈춘 뒤 0.6~1.5초 ÷ 직전 1초) | {'·'.join(format_value(value, 2) for value in auxiliary['stop_jump']['base'])} | {'·'.join(format_value(value, 2) for value in auxiliary['stop_jump']['final'])} |")
    lines.append(f"| 가는 길 걷기 바닥 선명도 중앙값 | {'·'.join(format_value(value, 4) for value in auxiliary['walking_sharpness']['base'])} | {'·'.join(format_value(value, 4) for value in auxiliary['walking_sharpness']['final'])} |")
    for probe in ARRIVAL_PROBE_SECONDS:
        lines.append(f"| 순간이동 도착 {probe}초 화면 칸 변화 비율(지점 평균) | {format_value(auxiliary['arrival']['base']['changed_at_mean'][str(probe)])} | {format_value(auxiliary['arrival']['final']['changed_at_mean'][str(probe)])} |")
    lines.append(f"| 같은 값의 끝−1초 잡음 | {format_value(auxiliary['arrival']['base']['noise_mean'])} | {format_value(auxiliary['arrival']['final']['noise_mean'])} |")
    lines.append(f"| 도착 뒤 스트리밍 대기 최대(MB)·지속 최대(초) | {format_value(auxiliary['arrival']['base']['pending_peak_mb_max'], 1)}·{format_value(auxiliary['arrival']['base']['pending_until_s_max'], 2)} | {format_value(auxiliary['arrival']['final']['pending_peak_mb_max'], 1)}·{format_value(auxiliary['arrival']['final']['pending_until_s_max'], 2)} |")
    lines.append(f"| 도착 뒤 스트리밍 대기 합(지점 6곳, MB, 실행 평균)·대기 난 지점 수 | {format_value(auxiliary['arrival']['base']['pending_peak_mb_sum_mean'], 1)}·{format_value(auxiliary['arrival']['base']['waiting_spots_mean'], 1)} | {format_value(auxiliary['arrival']['final']['pending_peak_mb_sum_mean'], 1)}·{format_value(auxiliary['arrival']['final']['waiting_spots_mean'], 1)} |")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--walk-base", nargs="+", required=True)
    parser.add_argument("--walk-final", nargs="+", required=True)
    parser.add_argument("--tp-base", nargs="+", required=True)
    parser.add_argument("--tp-final", nargs="+", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--title", default="GF-07 등록 판정")
    args = parser.parse_args()
    runs = {
        "walk_base": [summarize_run(run, "glide") for run in args.walk_base],
        "walk_final": [summarize_run(run, "glide") for run in args.walk_final],
        "tp_base": [summarize_run(run, "stare") for run in args.tp_base],
        "tp_final": [summarize_run(run, "stare") for run in args.tp_final],
    }
    walk_metrics = {group: [measure_walk_sharpness(run) for run in getattr(args, group)] for group in ("walk_base", "walk_final")}
    arrivals = {group: [measure_teleport_arrivals(run) for run in getattr(args, group)] for group in ("tp_base", "tp_final")}
    report = {
        "runs": runs,
        "registered": {
            "events": judge_events(runs["walk_base"], runs["walk_final"]),
            "settle": judge_settle(runs["tp_base"], runs["tp_final"]),
            "late": judge_late(runs["walk_base"], runs["walk_final"]),
            "regression": judge_regression(runs["walk_base"], runs["walk_final"]),
        },
        "auxiliary": {
            "stop_jump": {"base": [metric and metric["stop_jump_ratio"] for metric in walk_metrics["walk_base"]], "final": [metric and metric["stop_jump_ratio"] for metric in walk_metrics["walk_final"]]},
            "walking_sharpness": {"base": [metric and metric["walking_sharpness_median"] for metric in walk_metrics["walk_base"]], "final": [metric and metric["walking_sharpness_median"] for metric in walk_metrics["walk_final"]]},
            "arrival": {"base": summarize_arrivals(arrivals["tp_base"]), "final": summarize_arrivals(arrivals["tp_final"])},
            "arrival_runs": arrivals,
        },
    }
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, "judgment.json"), "w", encoding="utf-8") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=1)
    markdown = build_markdown(args.title, report)
    with open(os.path.join(args.out, "judgment.md"), "w", encoding="utf-8") as handle:
        handle.write(markdown)
    sys.stdout.reconfigure(encoding="utf-8")
    print(markdown)
    return 0


if __name__ == "__main__":
    sys.exit(main())
