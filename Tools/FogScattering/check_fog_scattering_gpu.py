"""시스템 Python 3에서 실행, 에디터 불필요: csvprofile CSV의 열(GPU/TDFogScattering·GPUTime·FrameTime 등)을 앞 60행을 버리고 p50·p95로 요약하고, 룩데브 실행 동안 생긴 CSV를 계획 JSON의 켬/끔 변형에 짝지어 켬-끔 차분을 낸다(SF-1 명세 7절).
실행: python Tools/FogScattering/check_fog_scattering_gpu.py --csv 파일.csv [...] [--columns 열 ...] | --run Saved/FluidFX/lookdev/<시각> [--plan Tools/FogScattering/lookdev_fog_scattering_v1.json] [--csv-dir Saved/Profiling/CSV] [--out 경로.json] | --self-test (PowerShell 또는 Git Bash)
출력: 표준 출력 열별 통계 줄과 JSON(--run은 Saved/FogScattering/gpu/result-<시각>.json 또는 --out). 종료 코드 0 = 완료 / 1 = 자체 시험 실패 / 2 = 입력 오류(파일 없음·CSV 수와 계획의 csvprofile 변형 수 불일치)
상태: 2026-10-23 SF-1 측정 장치 빌더 작성, 자체 시험과 기존 FluidFX CSV 열 읽기만 검증(실제 룩데브 CSV 짝짓기는 미검증, 짝은 파일 수정 시각 순서와 계획의 변형 순서가 같다고 가정)
"""
import argparse
import csv
import json
import math
import os
import sys
import tempfile
import time
from datetime import datetime
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_PLAN_PATH = ROOT / "Tools" / "FogScattering" / "lookdev_fog_scattering_v1.json"
DEFAULT_CSV_DIRECTORY = ROOT / "Saved" / "Profiling" / "CSV"
DEFAULT_OUTPUT_DIRECTORY = ROOT / "Saved" / "FogScattering" / "gpu"
DEFAULT_COLUMNS = ("GPU/TDFogScattering", "GPUTime", "FrameTime")
DEFAULT_DISCARD_ROWS = 60
HEADER_AT_END_MARKER = "[HasHeaderRowAtEnd]"
FOG_VARIABLE = "r.TDFogScattering"
CSV_PROFILE_COMMAND = "csvprofile"
RUN_END_MARGIN_SECONDS = 30.0
SAMPLE_CSV_GLOB = "Saved/FluidFX/fx7p/**/G1_r1_1A.csv"
SAMPLE_CSV_COLUMN = "GPU/TDFluidGpuSolver"
EXIT_SELF_TEST = 1
EXIT_INPUT = 2


def read_csv_columns(path, discard_rows):
    with open(path, encoding="utf-8", errors="replace", newline="") as handle:
        rows = list(csv.reader(handle))
    if not rows:
        return {}
    metadata_index = next((index for index, row in enumerate(rows) if row and row[0].startswith("[")), len(rows))
    has_header_at_end = metadata_index < len(rows) and HEADER_AT_END_MARKER in rows[metadata_index]
    header_row = rows[metadata_index - 1] if has_header_at_end else rows[0]
    data_rows = (rows[1:metadata_index - 1] if has_header_at_end else rows[1:metadata_index])[discard_rows:]
    header = [name.strip() for name in header_row]
    columns = {name: [] for name in header}
    for row in data_rows:
        if len(row) > len(header):
            continue
        for name, text in zip(header, row):
            try:
                columns[name].append(float(text))
            except ValueError:
                continue
    return columns


def percentile(values, fraction):
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = math.floor(position)
    upper = math.ceil(position)
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def summarize(values):
    if not values:
        return None
    return {"n": len(values), "p50": round(percentile(values, 0.5), 4), "p95": round(percentile(values, 0.95), 4),
            "mean": round(sum(values) / len(values), 4), "max": round(max(values), 4)}


def summarize_columns(columns, names):
    return {name: summarize(columns.get(name, [])) for name in names}


def describe_csv(path, names, discard_rows):
    columns = read_csv_columns(path, discard_rows)
    rows = max((len(values) for values in columns.values()), default=0)
    return {"path": str(path), "rows": rows, "stats": summarize_columns(columns, names)}, columns


def find_state(variant):
    state = None
    for command in variant.get("apply", {}).get("console", []):
        parts = command.split()
        if len(parts) == 2 and parts[0] == FOG_VARIABLE:
            state = "on" if parts[1] != "0" else "off"
    return state


def find_group(variant):
    parts = variant["name"].split("_")
    return "_".join(parts[:-2]) if len(parts) > 2 else variant["name"]


def find_csv_variants(plan):
    variants = []
    for variant in plan.get("variants", []):
        commands = variant.get("apply", {}).get("console", [])
        if any(command.split()[:1] == [CSV_PROFILE_COMMAND] for command in commands):
            variants.append(variant)
    return variants


def find_run_csv_files(run_directory, csv_directory):
    run_directory = Path(run_directory)
    try:
        started_at = datetime.strptime(run_directory.name, "%Y%m%d-%H%M%S").timestamp()
    except ValueError:
        started_at = run_directory.stat().st_ctime
    result_path = run_directory / "result.json"
    finished_at = (result_path.stat().st_mtime if result_path.is_file() else time.time()) + RUN_END_MARGIN_SECONDS
    files = [path for path in Path(csv_directory).glob("*.csv") if started_at <= path.stat().st_mtime <= finished_at]
    return sorted(files, key=lambda path: path.stat().st_mtime)


def analyze_blocks(csv_variants, csv_files, names, discard_rows):
    blocks = []
    pooled = {}
    for variant, path in zip(csv_variants, csv_files):
        description, columns = describe_csv(path, names, discard_rows)
        state = find_state(variant)
        group = find_group(variant)
        description.update({"variant": variant["name"], "group": group, "state": state})
        blocks.append(description)
        state_columns = pooled.setdefault(group, {}).setdefault(state, {name: [] for name in names})
        for name in names:
            state_columns[name].extend(columns.get(name, []))
    groups = {}
    warnings = []
    for group, by_state in pooled.items():
        stats = {state: summarize_columns(columns, names) for state, columns in by_state.items()}
        delta = {}
        for name in names:
            on_stats = (stats.get("on") or {}).get(name)
            off_stats = (stats.get("off") or {}).get(name)
            delta[name] = None if on_stats is None or off_stats is None else {
                "p50": round(on_stats["p50"] - off_stats["p50"], 4), "p95": round(on_stats["p95"] - off_stats["p95"], 4)}
            if on_stats is None and "on" in stats:
                warnings.append(f"{group}: 켬 블록에 열 없음 {name}")
        groups[group] = {"stats": stats, "delta_on_minus_off": delta}
    return {"blocks": blocks, "groups": groups, "warnings": warnings}


def print_description(description):
    print(description["path"], f"rows={description['rows']}")
    for name, stats in description["stats"].items():
        print("  ", name, "없음" if stats is None else f"p50={stats['p50']} p95={stats['p95']} mean={stats['mean']} n={stats['n']}")


def write_header_at_end_csv(path, header, data_rows):
    with open(path, "w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(header)
        writer.writerows(data_rows)
        writer.writerow(header)
        writer.writerow([HEADER_AT_END_MARKER, 1, "[platform]", "Windows"])


def run_self_test():
    failures = []

    def check(label, condition, detail):
        print(("PASS " if condition else "FAIL ") + label + " " + detail)
        if not condition:
            failures.append(label)

    header = ["EVENTS", "FrameTime", "GPUTime", "GPU/TDFogScattering"]
    with tempfile.TemporaryDirectory(prefix="td_fog_gpu_") as directory:
        directory = Path(directory)
        data_rows = [["", 16.0, 10.0 + index, 0.2] for index in range(DEFAULT_DISCARD_ROWS)]
        data_rows += [["", 16.0, float(value), 0.3] for value in range(1, 102)]
        sample = directory / "sample.csv"
        write_header_at_end_csv(sample, header, data_rows)
        description, _ = describe_csv(sample, DEFAULT_COLUMNS, DEFAULT_DISCARD_ROWS)
        check("앞 60행 버림·p50", description["rows"] == 101 and description["stats"]["GPUTime"]["p50"] == 51.0, f"rows={description['rows']} p50={description['stats']['GPUTime']['p50']}")
        check("p95 보간", description["stats"]["GPUTime"]["p95"] == 96.0, f"p95={description['stats']['GPUTime']['p95']}")

        plan = {"variants": [
            {"name": "warm", "apply": {"console": ["r.TDFogScattering 1"]}},
            {"name": "gpu_top_1_on", "apply": {"console": ["r.GPUCsvStatsEnabled 1", "r.TDFogScattering 1", "csvprofile frames=600"]}},
            {"name": "gpu_top_2_off", "apply": {"console": ["r.GPUCsvStatsEnabled 1", "r.TDFogScattering 0", "csvprofile frames=600"]}},
            {"name": "gpu_top_3_off", "apply": {"console": ["r.GPUCsvStatsEnabled 1", "r.TDFogScattering 0", "csvprofile frames=600"]}},
            {"name": "gpu_top_4_on", "apply": {"console": ["r.GPUCsvStatsEnabled 1", "r.TDFogScattering 1", "csvprofile frames=600"]}},
        ]}
        csv_variants = find_csv_variants(plan)
        check("csvprofile 변형만 고름", [variant["name"] for variant in csv_variants] == ["gpu_top_1_on", "gpu_top_2_off", "gpu_top_3_off", "gpu_top_4_on"], str(len(csv_variants)))
        files = []
        for index, (state, gpu_time) in enumerate((("on", 6.5), ("off", 6.0), ("off", 6.0), ("on", 6.5))):
            columns_header = header if state == "on" else header[:3]
            rows = [["", 16.0, gpu_time] + ([0.4] if state == "on" else []) for _ in range(DEFAULT_DISCARD_ROWS + 100)]
            path = directory / f"block{index}.csv"
            write_header_at_end_csv(path, columns_header, rows)
            os.utime(path, (1000.0 + index, 1000.0 + index))
            files.append(path)
        ordered = sorted(files, key=lambda path: path.stat().st_mtime)
        report = analyze_blocks(csv_variants, ordered, DEFAULT_COLUMNS, DEFAULT_DISCARD_ROWS)
        group = report["groups"]["gpu_top"]
        check("켬-끔 GPUTime 차분", group["delta_on_minus_off"]["GPUTime"] == {"p50": 0.5, "p95": 0.5}, str(group["delta_on_minus_off"]["GPUTime"]))
        check("끔 블록에 없는 열은 차분 없음", group["delta_on_minus_off"]["GPU/TDFogScattering"] is None and group["stats"]["off"]["GPU/TDFogScattering"] is None, str(group["delta_on_minus_off"]["GPU/TDFogScattering"]))
        check("켬 블록 열 p50", group["stats"]["on"]["GPU/TDFogScattering"]["p50"] == 0.4, str(group["stats"]["on"]["GPU/TDFogScattering"]))

    samples = sorted(ROOT.glob(SAMPLE_CSV_GLOB))
    if not samples:
        print("SKIP 기존 FluidFX CSV 없음:", SAMPLE_CSV_GLOB)
    else:
        description, _ = describe_csv(samples[0], (SAMPLE_CSV_COLUMN, "GPUTime"), DEFAULT_DISCARD_ROWS)
        stats = description["stats"][SAMPLE_CSV_COLUMN]
        check("기존 CSV 열 읽기", stats is not None and stats["n"] > 0 and description["rows"] > 0, f"{samples[0].name} rows={description['rows']} {SAMPLE_CSV_COLUMN} p50={None if stats is None else stats['p50']}")
    print("자체 시험", "통과" if not failures else "실패: " + ", ".join(failures))
    return 0 if not failures else EXIT_SELF_TEST


def main():
    parser = argparse.ArgumentParser(description="csvprofile CSV 열 요약과 안개 산란 켬/끔 GPU 차분")
    parser.add_argument("--csv", nargs="+")
    parser.add_argument("--columns", nargs="+", default=list(DEFAULT_COLUMNS))
    parser.add_argument("--discard-rows", type=int, default=DEFAULT_DISCARD_ROWS)
    parser.add_argument("--run")
    parser.add_argument("--plan", default=str(DEFAULT_PLAN_PATH))
    parser.add_argument("--csv-dir", default=str(DEFAULT_CSV_DIRECTORY))
    parser.add_argument("--out")
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    if arguments.self_test:
        return run_self_test()
    names = tuple(arguments.columns)

    if arguments.csv:
        descriptions = []
        for path in arguments.csv:
            if not Path(path).is_file():
                print("입력 오류: CSV 없음", path)
                return EXIT_INPUT
            description, _ = describe_csv(path, names, arguments.discard_rows)
            print_description(description)
            descriptions.append(description)
        print(json.dumps({"files": descriptions}, ensure_ascii=False))
        return 0

    if not arguments.run:
        print("입력 오류: --csv, --run, --self-test 중 하나가 필요하다")
        return EXIT_INPUT
    run_directory = Path(arguments.run)
    if not run_directory.is_dir() or not Path(arguments.plan).is_file():
        print("입력 오류: 실행 폴더 또는 계획 JSON 없음", run_directory, arguments.plan)
        return EXIT_INPUT
    csv_variants = find_csv_variants(json.loads(Path(arguments.plan).read_text(encoding="utf-8")))
    csv_files = find_run_csv_files(run_directory, arguments.csv_dir)
    if len(csv_files) != len(csv_variants):
        print(f"입력 오류: CSV {len(csv_files)}개와 계획의 csvprofile 변형 {len(csv_variants)}개가 다르다")
        for path in csv_files:
            print("  ", path)
        return EXIT_INPUT
    report = analyze_blocks(csv_variants, csv_files, names, arguments.discard_rows)
    for block in report["blocks"]:
        print(block["variant"], block["state"], Path(block["path"]).name, f"rows={block['rows']}")
    for group, data in report["groups"].items():
        for name, delta in data["delta_on_minus_off"].items():
            print(group, name, "차분 없음" if delta is None else f"켬-끔 p50={delta['p50']} p95={delta['p95']}")
    for warning in report["warnings"]:
        print("경고:", warning)
    output_path = Path(arguments.out) if arguments.out else DEFAULT_OUTPUT_DIRECTORY / f"result-{time.strftime('%Y%m%d-%H%M%S')}.json"
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print("결과 파일:", output_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
