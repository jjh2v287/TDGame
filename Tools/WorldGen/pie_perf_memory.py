"""에디터 밖에서 실행: PIE 폰을 필드 기준 지점·연결로 지점에 세워 CSV 프로파일러로 프레임·GPU 시간을 재고, 필드 중앙·남쪽 테두리·두 이계 중앙에서 memreport를 받아 P3-19 K16(연결로 GPU p99 ≤ 필드 기준 p99 x 1.2, 메모리 기준 대비 +30% 초과 표시)을 판정한다.
실행: python Tools/WorldGen/pie_perf_memory.py --run <이름> [--settle 8] [--sample 8] [--overwrite] [--offline] (PowerShell 또는 Git Bash, 에디터 실행 중이고 다른 갈래가 에디터를 쓰지 않을 때, 에디터 창을 앞에 둔다)
출력: Saved/WorldGen/PiePerf/<run>/ 의 places.json(지점별 CSV·memreport 경로와 통계)·summary.md. 종료 코드 0 = K16 통과, 1 = 실패·측정 불가, 2 = PIE 시작 실패
상태: 현행 (2026-10-15)
"""
import argparse
import csv
import glob
import json
import os
import re
import sys
import time

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WORLDGEN = os.path.join(ROOT, "Tools", "WorldGen")
sys.path.insert(0, WORLDGEN)
import pie_capture_spots as spots  # noqa: E402

PERF_ROOT = os.path.join(ROOT, "Saved", "WorldGen", "PiePerf")
CSV_DIR = os.path.join(ROOT, "Saved", "Profiling", "CSV")
MEMREPORT_DIR = os.path.join(ROOT, "Saved", "Profiling", "MemReports")
BASELINE_IDS = ("F01_village", "F02_forest_path", "F03_graveyard", "F07_bridge")
CONNECTOR_IDS = ("C01_sw_connector_mouth", "C02_ne_connector_start", "C03_se_spur_end", "B01_sw_boundary", "B02_ne_boundary", "B03_se_slot_boundary", "F12_se_lift_field_landing", "S05_se_lift_bottom")
MEMORY_PLACES = (
    {"id": "M1_field_center", "region": "field", "x_cm": 0.0, "y_cm": 0.0},
    {"id": "M2_south_rim", "region": "field", "x_cm": -25000.0, "y_cm": -50400.0},
    {"id": "M3_starfield_center", "region": "SunkenStarfield", "x_cm": -6300.0, "y_cm": -75600.0},
    {"id": "M4_pale_center", "region": "PaleHeights", "x_cm": 75600.0, "y_cm": 45200.0},
)
GPU_PASS_PREFIX = "GPU/"
HEADER_AT_END_MARKER = "[HasHeaderRowAtEnd]"
GPU_SUM_COLUMN = "GPU/* 합(패스별 + Unaccounted)"
GPU_COLUMN_CANDIDATES = ("GPUTime", "GPU/Total", GPU_SUM_COLUMN)
FRAME_COLUMN = "FrameTime"
GPU_MEMORY_COLUMN = "GPUMem/LocalUsedMB"
GPU_P99_RATIO_LIMIT = 1.2
MEMORY_GROWTH_LIMIT = 0.3
FILE_TIMEOUT_SECONDS = 60.0
MEMREPORT_TIMEOUT_SECONDS = 240.0
PHYSICAL_MEMORY_PATTERN = re.compile(r"Process Physical Memory:\s*([\d.]+)\s*MB used")
VIRTUAL_MEMORY_PATTERN = re.compile(r"Process Virtual Memory:\s*([\d.]+)\s*MB used")


def list_files(pattern):
    return set(glob.glob(pattern, recursive=True))


def wait_for_new_file(pattern, before, timeout_seconds):
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        fresh = sorted(list_files(pattern) - before, key=os.path.getmtime)
        if fresh and is_file_settled(fresh[-1]):
            return fresh[-1]
        time.sleep(1.0)
    return None


def is_file_settled(path):
    first = os.path.getsize(path)
    time.sleep(1.0)
    return first > 0 and os.path.getsize(path) == first


def move_to(place, ground_cm, settle_seconds):
    ground = ground_cm(place["region"], place["x_cm"], place["y_cm"])
    z_cm = None if ground is None else ground + spots.SPOT_CLEARANCE_CM
    request = {"action": "move", "x": place["x_cm"], "y": place["y_cm"], "z": z_cm, "refine": True}
    result = spots.call_probe(request)
    for attempt in range(spots.GROUND_RETRY_COUNT):
        if result.get("ground_z") is not None:
            break
        time.sleep(spots.GROUND_RETRY_SECONDS)
        result = spots.call_probe(dict(request, z=None))
    time.sleep(settle_seconds)
    return result.get("pawn")


def run_command(command):
    spots.call_probe({"action": "command", "command": command})


def profile_place(place, ground_cm, settle_seconds, sample_seconds):
    pawn = move_to(place, ground_cm, settle_seconds)
    pattern = os.path.join(CSV_DIR, "*.csv")
    before = list_files(pattern)
    run_command("csvprofile start")
    time.sleep(sample_seconds)
    run_command("csvprofile stop")
    csv_path = wait_for_new_file(pattern, before, FILE_TIMEOUT_SECONDS)
    return {"id": place["id"], "region": place["region"], "pawn": pawn, "csv": csv_path}


def report_memory(place, ground_cm, settle_seconds):
    pawn = move_to(place, ground_cm, settle_seconds)
    pattern = os.path.join(MEMREPORT_DIR, "**", "*.memreport")
    before = list_files(pattern)
    run_command("memreport -full")
    report_path = wait_for_new_file(pattern, before, MEMREPORT_TIMEOUT_SECONDS)
    return {"id": place["id"], "region": place["region"], "pawn": pawn, "memreport": report_path}


def read_csv_columns(path):
    with open(path, encoding="utf-8", errors="replace", newline="") as handle:
        rows = list(csv.reader(handle))
    if not rows:
        return {}
    metadata_index = next((index for index, row in enumerate(rows) if row and row[0].startswith("[")), len(rows))
    has_header_at_end = metadata_index < len(rows) and HEADER_AT_END_MARKER in rows[metadata_index]
    header = rows[metadata_index - 1] if has_header_at_end else rows[0]
    data_rows = rows[1:metadata_index - 1] if has_header_at_end else rows[1:metadata_index]
    header = [name.strip() for name in header]
    columns = {name: [] for name in header}
    columns[GPU_SUM_COLUMN] = []
    gpu_indices = [index for index, name in enumerate(header) if name.startswith(GPU_PASS_PREFIX)]
    for row in data_rows:
        if len(row) > len(header):
            continue
        for name, value in zip(header, row):
            try:
                columns[name].append(float(value))
            except ValueError:
                continue
        columns[GPU_SUM_COLUMN].append(sum(read_float(row[index]) for index in gpu_indices if index < len(row)))
    return columns


def read_float(text):
    try:
        return float(text)
    except ValueError:
        return 0.0


def describe_values(values):
    if not values:
        return None
    array = np.asarray(values, dtype=float)
    return {"n": int(array.size), "p50": round(float(np.percentile(array, 50)), 2), "p99": round(float(np.percentile(array, 99)), 2), "max": round(float(array.max()), 2)}


def find_gpu_column(columns):
    return next((name for name in GPU_COLUMN_CANDIDATES if columns.get(name)), None)


def describe_csv(path):
    if not path or not os.path.exists(path):
        return {"error": "csv missing"}
    columns = read_csv_columns(path)
    gpu_column = find_gpu_column(columns)
    return {
        "frame": describe_values(columns.get(FRAME_COLUMN)),
        "gpu_column": gpu_column,
        "gpu": describe_values(columns.get(gpu_column)) if gpu_column else None,
        "gpu_memory_mb": describe_values(columns.get(GPU_MEMORY_COLUMN)),
        "gpu_pass_columns": sum(1 for name in columns if name.startswith(GPU_PASS_PREFIX)),
        "raw_frame": columns.get(FRAME_COLUMN) or [],
        "raw_gpu": columns.get(gpu_column) or [] if gpu_column else [],
    }


def describe_memreport(path):
    if not path or not os.path.exists(path):
        return {"error": "memreport missing"}
    with open(path, encoding="utf-8", errors="replace") as handle:
        text = handle.read()
    physical = PHYSICAL_MEMORY_PATTERN.search(text)
    virtual = VIRTUAL_MEMORY_PATTERN.search(text)
    return {"physical_mb": float(physical.group(1)) if physical else None, "virtual_mb": float(virtual.group(1)) if virtual else None}


def measure(args, out_dir):
    regions, spot_list = spots.load_spots(os.path.abspath(spots.DEFAULT_SPOTS))
    ground_cm = spots.build_ground_lookup(regions)
    by_id = {spot["id"]: spot for spot in spot_list}
    places = {"baseline": [], "connector": [], "memory": []}
    spots.start_pie(args.warmup)
    try:
        spots.call_probe({"action": "console", "variable": "r.GPUCsvStatsEnabled", "value": 1})
        for group, ids in (("baseline", BASELINE_IDS), ("connector", CONNECTOR_IDS)):
            for spot_id in ids:
                places[group].append(profile_place(by_id[spot_id], ground_cm, args.settle, args.sample))
                spots.log(f"{group} {spot_id}: {places[group][-1]['csv']}")
        for place in MEMORY_PLACES:
            places["memory"].append(report_memory(place, ground_cm, args.settle))
            spots.log(f"memory {place['id']}: {places['memory'][-1]['memreport']}")
    finally:
        spots.stop_pie()
    spots.write_json(os.path.join(out_dir, "places.json"), places)
    return places


def pooled(entries, key):
    values = []
    for entry in entries:
        values += entry["stats"].get(key) or []
    return describe_values(values)


def judge(places):
    for group in ("baseline", "connector"):
        for entry in places[group]:
            entry["stats"] = describe_csv(entry["csv"])
    for entry in places["memory"]:
        entry["stats"] = describe_memreport(entry["memreport"])
    baseline_gpu = pooled(places["baseline"], "raw_gpu")
    connector_gpu = pooled(places["connector"], "raw_gpu")
    baseline_frame = pooled(places["baseline"], "raw_frame")
    connector_frame = pooled(places["connector"], "raw_frame")
    failures = []
    ratio = None
    if baseline_gpu is None or connector_gpu is None:
        failures.append("K16 GPU 열 없음(측정 불가)")
    else:
        ratio = round(connector_gpu["p99"] / baseline_gpu["p99"], 3)
        if ratio > GPU_P99_RATIO_LIMIT:
            failures.append(f"K16 연결로 GPU p99 비율 {ratio}")
    memory = {entry["id"]: entry["stats"].get("physical_mb") for entry in places["memory"]}
    reference = memory.get(MEMORY_PLACES[0]["id"])
    growth = {}
    for place_id, value in memory.items():
        if reference and value:
            growth[place_id] = round(value / reference - 1.0, 3)
    flagged = [place_id for place_id, value in growth.items() if value > MEMORY_GROWTH_LIMIT]
    return {"baseline_gpu": baseline_gpu, "connector_gpu": connector_gpu, "baseline_frame": baseline_frame, "connector_frame": connector_frame,
            "ratio": ratio, "memory_mb": memory, "memory_growth": growth, "memory_flagged": flagged, "failures": failures}


def format_stats(stats):
    if not stats:
        return "-"
    return f"{stats['p50']} / {stats['p99']} / {stats['max']} (n {stats['n']})"


def render_summary(run_name, places, verdict):
    lines = [f"# PIE 성능·메모리 {run_name}", "", f"- 실행: {time.strftime('%Y-%m-%d %H:%M:%S')}. 지점마다 이동 → 대기 → csvprofile start → 표본 → stop, r.GPUCsvStatsEnabled 1. 서 있는 상태의 시야 비용이다(걷기 스트리밍 끊김은 pie_walk_passages.py).",
             f"- K16: 연결로 GPU p99 ≤ 필드 기준 p99 x {GPU_P99_RATIO_LIMIT}. 메모리는 필드 중앙 대비 +{int(MEMORY_GROWTH_LIMIT * 100)}% 초과를 표시(초과 시 별도 결정)", "",
             "| 묶음 | 지점 | GPU 열 | 프레임 p50/p99/최대(ms) | GPU p50/p99/최대(ms) | GPU 메모리 p50(MB) |", "|---|---|---|---|---|---|"]
    for group in ("baseline", "connector"):
        for entry in places[group]:
            stats = entry["stats"]
            memory = stats.get("gpu_memory_mb") or {}
            lines.append(f"| {group} | {entry['id']} | {stats.get('gpu_column') or stats.get('error') or '-'} | {format_stats(stats.get('frame'))} | {format_stats(stats.get('gpu'))} | {memory.get('p50', '-')} |")
    lines += ["", f"- 필드 기준 GPU: {format_stats(verdict['baseline_gpu'])}, 연결로 GPU: {format_stats(verdict['connector_gpu'])}, p99 비율 {verdict['ratio']}",
              f"- 필드 기준 프레임: {format_stats(verdict['baseline_frame'])}, 연결로 프레임: {format_stats(verdict['connector_frame'])}", "",
              "| memreport 지점 | 물리 메모리(MB) | 필드 중앙 대비 | 파일 |", "|---|---|---|---|"]
    for entry in places["memory"]:
        lines.append(f"| {entry['id']} | {entry['stats'].get('physical_mb') or entry['stats'].get('error')} | {verdict['memory_growth'].get(entry['id'], '-')} | {spots.describe_project_path(entry['memreport']) if entry['memreport'] else '-'} |")
    pass_counts = sorted({entry["stats"].get("gpu_pass_columns", 0) for group in ("baseline", "connector") for entry in places[group]})
    lines += ["", f"- GPU 시간 = CSV `GPUTime` 열(없으면 `GPU/*` 패스 열 합, 패스 열 수 {pass_counts}). CSV는 열이 늘면 행 길이가 바뀌고 최종 헤더가 파일 끝([HasHeaderRowAtEnd])에 있다", "", "## 판정", ""]
    lines += [f"- {failure}" for failure in verdict["failures"]] or ["- 통과"]
    if verdict["memory_flagged"]:
        lines.append(f"- 메모리 +30% 초과(결정 필요): {', '.join(verdict['memory_flagged'])}")
    return "\n".join(lines) + "\n"


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description="PIE 지점별 CSV 프로파일(GPU 시간)·memreport로 K16 판정")
    parser.add_argument("--run", default=None, help="결과 폴더 이름(기본 현재 시각), Saved/WorldGen/PiePerf/<run>/")
    parser.add_argument("--settle", type=float, default=8.0, help="이동 뒤 측정 전 대기 초")
    parser.add_argument("--sample", type=float, default=8.0, help="지점마다 CSV 표본 초")
    parser.add_argument("--warmup", type=float, default=15.0, help="PIE 시작 뒤 대기 초")
    parser.add_argument("--overwrite", action="store_true", help="이미 있는 run 폴더에 덮어쓴다")
    parser.add_argument("--offline", action="store_true", help="PIE 없이 --run 폴더의 places.json으로 summary.md만 다시 만든다")
    args = parser.parse_args(argv)
    if args.offline and not args.run:
        parser.error("--offline needs --run")
    return args


def main(argv=None):
    spots.configure_output()
    args = parse_args(argv)
    run_name = args.run or time.strftime("%Y%m%d_%H%M%S")
    out_dir = os.path.join(PERF_ROOT, run_name)
    places_path = os.path.join(out_dir, "places.json")
    if args.offline:
        places = spots.read_json(places_path)
    else:
        if os.path.exists(places_path) and not args.overwrite:
            raise SystemExit(f"run folder already exists (use another --run or --overwrite): {out_dir}")
        os.makedirs(out_dir, exist_ok=True)
        try:
            places = measure(args, out_dir)
        except spots.PieStartError as error:
            spots.log(str(error))
            return 2
    verdict = judge(places)
    for group in ("baseline", "connector"):
        for entry in places[group]:
            entry["stats"].pop("raw_frame", None)
            entry["stats"].pop("raw_gpu", None)
    spots.write_json(os.path.join(out_dir, "verdict.json"), verdict)
    spots.write_text(os.path.join(out_dir, "summary.md"), render_summary(run_name, places, verdict))
    spots.log(f"summary: {os.path.join(out_dir, 'summary.md')}")
    return 1 if verdict["failures"] else 0


if __name__ == "__main__":
    sys.exit(main())
