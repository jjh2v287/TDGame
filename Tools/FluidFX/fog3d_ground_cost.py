"""실행 환경: 시스템 Python(표준 라이브러리만, 에디터 불필요). 3D 안개 2a 캡처 비용(2A-I8)·캡처 순서(2A-I9) 측정의 뼈대: (plan) 켬·끔·끔·켬 csvprofile 변형 JSON, (summarize) CSV 통계 집계표, (order) PIE 로그 조각에서 캡처 순서·보류 걸음·정본 높이 되읽기 표. 문턱 판정은 넣지 않고 값만 낸다.
실행 명령: python -B Tools/FluidFX/fog3d_ground_cost.py plan [--quality 2 0] [--frames 300] [--forced] [--out <JSON>] | summarize --csv 이름=경로 ... [--latest N --labels 이름...] [--out <JSON>] | order --log <log_slice.txt ...> [--out <JSON>], 점검만: --self-test
출력: plan → Saved/FluidFX/fog3d_ground/cost/{plan.json, tuning_on.json, tuning_off.json}(pie_lookdev_hires.py --variants 입력), summarize·order → 표(표준 출력)와 선택 JSON. 종료 코드 0 정상 / 2 입력 문제.
상태: 에디터 없이 가짜 CSV·가짜 로그 --self-test 만 확인. 읽는 CSV 통계 이름과 로그 줄(TDFog3D/…, "Fog3D ground …")은 C++ 가 아직 내지 않는다(감독 넘김 글의 필요한 C++ 변경 목록). 실제 CSV·로그에는 돌린 적 없다.
"""
import argparse
import csv
import json
import math
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
for directory in (ROOT / "Tools", ROOT / "Tools" / "FluidFX", ROOT / "Tools" / "FogScattering"):
    if str(directory) not in sys.path:
        sys.path.insert(0, str(directory))
import check_fog_scattering_gpu as gpu_csv  # noqa: E402
import fog3d_ground_stage as stage  # noqa: E402
import fog3d_ground_variants as gv  # noqa: E402

COST_DIRECTORY = gv.GROUND_ROOT / "cost"
PROFILING_CSV_DIRECTORY = ROOT / "Saved" / "Profiling" / "CSV"
DISCARD_ROWS = 60
DEFAULT_FRAMES = 300
DEFAULT_QUALITIES = (2, 0)
WALK_SPEED_CM_PER_SECOND = 520.0
SETTLE_SECONDS = 18.0
WARM_SETTLE_SECONDS = 8.0
SCREEN_PERCENTAGE_COMMAND = "r.ScreenPercentage 113.3"
BASELINE_CONSOLE = ("r.GPUCsvStatsEnabled 1", SCREEN_PERCENTAGE_COMMAND, "r.VolumetricFog.GridPixelSize 8", "r.VolumetricFog.GridSizeZ 128", "r.TDFogScattering 1")
FORCE_RECAPTURE_COMMAND_FORMAT = "TD.Fog3D.GroundForceRecapture {value}"
QUALITY_COMMAND_FORMAT = "TD.Fog3D.Quality {level}"
STANDARD_COLUMNS = ("GPUTime", "FrameTime", "GPU/VolumetricFog")
PROPOSED_COLUMNS = ("TDFog3D/GameThread/GroundCaptureList", "TDFog3D/GameThread/GroundCaptureRequest", "TDFog3D/RenderThread/GroundCaptureConvert", "GPU/TDFog3DGroundCapture", "GPU/TDFog3DGroundCaptureConvert")
BLOCK_ORDER = ("on_a", "off_a", "off_b", "on_b")
FORCED_BLOCK_ORDER = ("normal_a", "forced_a", "forced_b", "normal_b")
CAPTURE_LATENCY_FRAMES = 2
CONVERT_PATTERN = re.compile(r"converted the ground capture requested at frame (\d+) in frame (\d+)")
RECAPTURE_PATTERN = re.compile(r"Fog3D ground recapture:\s*frame=(\d+)\s+reason=(\w+)")
DEFERRED_PATTERN = re.compile(r"Fog3D ground deferred:\s*frame=(\d+)\s+steps=(\d+)\s+total=(\d+)\s+reason=(\w+)")
READBACK_PATTERN = re.compile(r"Fog3D ground readback:\s*frame=(\d+)\s+index=(\d+)\s+world=\(\s*(-?[\d.]+)\s*,\s*(-?[\d.]+)\s*\)\s+height_cm=(-?[\d.]+)\s+valid=([01])")
SUMMARY_DEFERRED_PATTERN = re.compile(r"Fog3D zone summary:\s*submitted=(\d+)\s+rejected=(\d+)(?:\s+deferred=(\d+))?")
EXIT_OK = 0
EXIT_INPUT_PROBLEM = 2


class InputProblem(Exception):
    pass


def build_block_variants(qualities, frames, forced, start_world, tuning_paths):
    variants = []
    labels = FORCED_BLOCK_ORDER if forced else BLOCK_ORDER
    for quality in qualities:
        quality_command = QUALITY_COMMAND_FORMAT.format(level=quality)
        warm_apply = {"tuning_reload": {"tuning": tuning_paths["on"], "reset": "reinit"}, "pawn": start_world, "console": [quality_command]}
        variants.append({"name": f"q{quality}_warm", "apply": warm_apply, "settle": WARM_SETTLE_SECONDS})
        for label in labels:
            is_on = label.startswith("on") or label.startswith("normal") or label.startswith("forced")
            tuning = tuning_paths["on"] if is_on else tuning_paths["off"]
            console = [quality_command, f"csvprofile frames={frames}"]
            apply = {"tuning_reload": {"tuning": tuning, "reset": "reinit"}, "pawn": start_world, "console": console}
            if forced:
                console.insert(1, FORCE_RECAPTURE_COMMAND_FORMAT.format(value=1 if label.startswith("forced") else 0))
            else:
                apply["move"] = {"distance_cm": 3000.0, "yaw": 0.0}
            variants.append({"name": f"q{quality}_{label}", "apply": apply, "settle": SETTLE_SECONDS})
    return variants


def build_plan(qualities, frames, forced, output_root):
    canonical = gv.base.load_canonical_tuning()
    field_types = gv.load_field_types(canonical, True)
    solids = stage.build_solids()
    line = stage.find_line("R01")
    start = stage.line_start_world(line)
    start_world = [round(start[0], 3), round(start[1], 3), round(stage.line_start_height(solids, line) + stage.PAWN_SPAWN_HEIGHT_CM, 3)]
    tuning_paths = {}
    for key, follow in (("on", True), ("off", False)):
        spec = gv.make_spec("run", "F", "R01", "back", follow, [])
        tuning, _ = gv.build_tuning(canonical, field_types, spec, [], True)
        path = Path(output_root) / f"tuning_{key}.json"
        gv.base.write_json(path, tuning)
        tuning_paths[key] = gv.base.describe_run_directory(Path(output_root).parent, Path(output_root).name) + f"/tuning_{key}.json"
    base_apply = {"console": [gv.COMPUTE_ON_COMMAND, *BASELINE_CONSOLE], "arm": 400, "view": [-12.0, 0.0], "speed": WALK_SPEED_CM_PER_SECOND}
    plan = {"note": "2A-I8 비용 촬영: " + ("다시 찍는 프레임(강제 재캡처 보통·강제·강제·보통)" if forced else "걷는 평균(켬·끔·끔·켬, 3000cm 걷기, 초속 520)") + f", csvprofile frames={frames}, 앞 {DISCARD_ROWS}행 버림. "
            "조건: 뷰포트 1749x924(에디터 창을 먼저 맞춘다), r.ScreenPercentage 113.3, 격자 8/128, r.TDFogScattering 1, 품질 " + ",".join(str(level) for level in qualities),
            "base": base_apply, "variants": build_block_variants(qualities, frames, forced, start_world, tuning_paths)}
    problems = []
    probe = {"base": {key: value for key, value in base_apply.items()}, "variants": plan["variants"]}
    problems += [f"base: {line_}" for line_ in gv.base.find_variant_key_problems({"base": probe["base"], "variants": []})]
    for entry in plan["variants"]:
        problems += [f"{entry['name']}: {line_}" for line_ in gv.base.find_variant_key_problems({"base": {}, "variants": [entry]})]
    if problems:
        raise InputProblem("변형 키 오류: " + "; ".join(problems))
    return plan


def run_plan(arguments):
    output_root = Path(arguments.out).parent if arguments.out else COST_DIRECTORY
    output_root.mkdir(parents=True, exist_ok=True)
    plan = build_plan(arguments.quality, arguments.frames, arguments.forced, output_root)
    plan_path = Path(arguments.out) if arguments.out else output_root / ("plan_forced.json" if arguments.forced else "plan.json")
    gv.base.write_json(plan_path, plan)
    print(f"계획 {len(plan['variants'])}블록 → {plan_path.as_posix()}")
    print("촬영:", f"python -B Tools/FluidFX/pie_lookdev_hires.py --variants {gv.base.describe_run_directory(plan_path.parent, plan_path.name)} --level {stage.STAGE_MAP_PATH} --fresh-pie --stop-pie (PowerShell)")
    print("집계: python -B Tools/FluidFX/fog3d_ground_cost.py summarize --latest", sum(1 for entry in plan["variants"] if "warm" not in entry["name"]), "--labels", " ".join(entry["name"] for entry in plan["variants"] if "warm" not in entry["name"]))
    return EXIT_OK


def describe_column(values):
    if not values:
        return None
    ordered = sorted(values)
    nonzero = [value for value in values if value > 0.0]
    return {"count": len(values), "mean": sum(values) / len(values), "p50": gpu_csv.percentile(ordered, 0.5), "p95": gpu_csv.percentile(ordered, 0.95), "max": ordered[-1], "nonzero": len(nonzero)}


def summarize_csv(path, discard_rows=DISCARD_ROWS):
    columns = gpu_csv.read_csv_columns(path, discard_rows)
    wanted = [name for name in (*STANDARD_COLUMNS, *PROPOSED_COLUMNS) if name in columns]
    wanted += sorted(name for name in columns if (name.startswith("TDFog3D/") or name.startswith("GPU/TDFog3D")) and name not in wanted)
    return {"csv": str(path), "columns": {name: describe_column(columns[name]) for name in wanted}, "missing_proposed": [name for name in PROPOSED_COLUMNS if name not in columns]}


def pair_differences(summaries):
    results = {}
    groups = {}
    for label in summaries:
        match = re.fullmatch(r"(q\d+)_(on|off|normal|forced)_([ab])", label)
        if match:
            groups.setdefault(match.group(1), {}).setdefault(match.group(2), []).append(label)
    for group, sides in groups.items():
        first, second = ("on", "off") if "on" in sides else ("forced", "normal")
        if first not in sides or second not in sides or len(sides[first]) != 2 or len(sides[second]) != 2:
            continue
        columns = set.intersection(*[set(summaries[label]["columns"]) for label in sides[first] + sides[second]])
        rows = {}
        for column in sorted(columns):
            stats = [summaries[label]["columns"][column] for label in sides[first] + sides[second]]
            if any(stat is None for stat in stats):
                continue
            first_mean = sum(summaries[label]["columns"][column]["mean"] for label in sides[first]) / 2.0
            second_mean = sum(summaries[label]["columns"][column]["mean"] for label in sides[second]) / 2.0
            first_p95 = sum(summaries[label]["columns"][column]["p95"] for label in sides[first]) / 2.0
            second_p95 = sum(summaries[label]["columns"][column]["p95"] for label in sides[second]) / 2.0
            rows[column] = {"first": first, "second": second, "mean_difference": first_mean - second_mean, "p95_difference": first_p95 - second_p95, "block_mean_spread_first": abs(summaries[sides[first][0]]["columns"][column]["mean"] - summaries[sides[first][1]]["columns"][column]["mean"]),
                            "block_mean_spread_second": abs(summaries[sides[second][0]]["columns"][column]["mean"] - summaries[sides[second][1]]["columns"][column]["mean"])}
        results[group] = rows
    return results


def format_value(value, digits=3):
    return "-" if value is None else f"{value:.{digits}f}"


def build_summary_text(summaries, differences):
    lines = ["| 블록 | 열 | 평균 | p50 | p95 | 최대 | 0 아닌 행 / 전체 |", "|---|---|---|---|---|---|---|"]
    for label, summary in summaries.items():
        for column, stats in summary["columns"].items():
            lines.append(f"| {label} | {column} | " + (f"{stats['mean']:.3f} | {stats['p50']:.3f} | {stats['p95']:.3f} | {stats['max']:.3f} | {stats['nonzero']}/{stats['count']} |" if stats else "- | - | - | - | - |"))
        if summary["missing_proposed"]:
            lines.append(f"| {label} | (CSV 에 없는 새 통계) | " + ", ".join(summary["missing_proposed"]) + " | | | | |")
    lines += ["", "| 묶음 | 열 | 앞쪽 − 뒤쪽 평균 | p95 차 | 앞쪽 두 블록 평균 간격 | 뒤쪽 두 블록 평균 간격 |", "|---|---|---|---|---|---|"]
    for group, rows in differences.items():
        for column, row in rows.items():
            lines.append(f"| {group} {row['first']}−{row['second']} | {column} | {format_value(row['mean_difference'], 4)} | {format_value(row['p95_difference'], 4)} | {format_value(row['block_mean_spread_first'], 4)} | {format_value(row['block_mean_spread_second'], 4)} |")
    return "\n".join(lines)


def run_summarize(arguments):
    paths = {}
    for item in arguments.csv or []:
        label, separator, path = item.partition("=")
        if not separator or not Path(path).is_file():
            raise InputProblem(f"--csv 는 이름=경로(있는 파일): {item}")
        paths[label] = path
    if arguments.latest:
        files = sorted(PROFILING_CSV_DIRECTORY.glob("*.csv"), key=lambda path: path.stat().st_mtime)[-arguments.latest:]
        if len(files) != arguments.latest or not arguments.labels or len(arguments.labels) != arguments.latest:
            raise InputProblem("--latest N 은 --labels 가 정확히 N 개이고 최근 CSV 가 N 개 있어야 한다")
        paths.update(dict(zip(arguments.labels, (str(path) for path in files))))
    if not paths:
        raise InputProblem("--csv 또는 --latest 가 필요하다")
    summaries = {label: summarize_csv(path) for label, path in paths.items()}
    differences = pair_differences(summaries)
    print(build_summary_text(summaries, differences))
    if arguments.out:
        gv.base.write_json(arguments.out, {"summaries": summaries, "differences": differences})
    return EXIT_OK


def percentile(values, fraction):
    return gpu_csv.percentile(sorted(values), fraction) if values else None


def parse_log_lines(lines):
    conversions = [(int(match.group(1)), int(match.group(2))) for match in (CONVERT_PATTERN.search(line) for line in lines) if match]
    recaptures = [(int(match.group(1)), match.group(2)) for match in (RECAPTURE_PATTERN.search(line) for line in lines) if match]
    deferred = [(int(match.group(1)), int(match.group(2)), int(match.group(3)), match.group(4)) for match in (DEFERRED_PATTERN.search(line) for line in lines) if match]
    readbacks = [(int(match.group(1)), int(match.group(2)), float(match.group(3)), float(match.group(4)), float(match.group(5)), match.group(6) == "1") for match in (READBACK_PATTERN.search(line) for line in lines) if match]
    summaries = [match for match in (SUMMARY_DEFERRED_PATTERN.search(line) for line in lines) if match]
    return {"conversions": conversions, "recaptures": recaptures, "deferred": deferred, "readbacks": readbacks, "summary": summaries[-1].groups() if summaries else None}


def analyze_order(parsed, solids):
    lags = [converted - requested for requested, converted in parsed["conversions"]]
    histogram = {}
    for lag in lags:
        histogram[lag] = histogram.get(lag, 0) + 1
    deferred_by_reason = {}
    for _, steps, _, reason in parsed["deferred"]:
        deferred_by_reason[reason] = deferred_by_reason.get(reason, 0) + steps
    recapture_by_reason = {}
    for _, reason in parsed["recaptures"]:
        recapture_by_reason[reason] = recapture_by_reason.get(reason, 0) + 1
    window_frames = [frame for frame, reason in parsed["recaptures"] if reason == "window"]
    window_gaps = [second - first for first, second in zip(window_frames, window_frames[1:])]
    entry_deferred = [(frame, steps, reason) for frame, steps, _, reason in parsed["deferred"] if reason in ("entering", "teleport")]
    readback_rows = []
    for frame, index, x, y, height, valid in parsed["readbacks"]:
        expected = stage.ground_height(solids, x, y, tagged_only=True)
        readback_rows.append({"frame": frame, "index": index, "world": [x, y], "read_height_cm": height, "valid": valid, "expected_height_cm": expected, "difference_cm": None if expected is None or not valid else height - expected})
    differences = [abs(row["difference_cm"]) for row in readback_rows if row["difference_cm"] is not None]
    return {
        "lag_frames": {"count": len(lags), "min": min(lags) if lags else None, "mean": sum(lags) / len(lags) if lags else None, "p95": percentile(lags, 0.95), "max": max(lags) if lags else None, "histogram": dict(sorted(histogram.items())), "latency_setting_frames": CAPTURE_LATENCY_FRAMES},
        "recaptures": {"total": len(parsed["recaptures"]), "by_reason": recapture_by_reason, "window_gap_frames": {"count": len(window_gaps), "min": min(window_gaps) if window_gaps else None, "median": percentile(window_gaps, 0.5), "max": max(window_gaps) if window_gaps else None}},
        "deferred_steps": {"total": sum(deferred_by_reason.values()), "by_reason": deferred_by_reason, "episodes_entering_or_teleport": entry_deferred, "summary_line_deferred": parsed["summary"][2] if parsed["summary"] else None},
        "readback": {"rows": readback_rows, "count": len(readback_rows), "invalid_count": sum(1 for row in readback_rows if not row["valid"]), "max_abs_difference_cm": max(differences) if differences else None, "mean_abs_difference_cm": sum(differences) / len(differences) if differences else None},
    }


def build_order_text(label, analysis):
    lag, recapture, deferred, readback = analysis["lag_frames"], analysis["recaptures"], analysis["deferred_steps"], analysis["readback"]
    lines = [f"### {label}", "",
             f"- 캡처 요청 → 변환 지연(프레임): {lag['count']}건, 최소 {lag['min']} 평균 {format_value(lag['mean'], 2)} p95 {format_value(lag['p95'], 1)} 최대 {lag['max']} (설정 지연 {lag['latency_setting_frames']}) 분포 {lag['histogram']}",
             f"- 재캡처 {recapture['total']}회 {recapture['by_reason']}, 창 이동 재캡처 사이 프레임 간격 {recapture['window_gap_frames']}",
             f"- 보류한 걸음 합계 {deferred['total']} {deferred['by_reason']}, 끝 요약 줄의 deferred= {deferred['summary_line_deferred']}, 첫 진입·순간이동 보류 {deferred['episodes_entering_or_teleport']}",
             f"- 정본 높이 되읽기 {readback['count']}텍셀(무효 {readback['invalid_count']}), 기대(무대 배치표)와 차 평균 {format_value(readback['mean_abs_difference_cm'], 2)}cm 최대 {format_value(readback['max_abs_difference_cm'], 2)}cm"]
    for row in readback["rows"][:24]:
        lines.append(f"  - 프레임 {row['frame']} #{row['index']} ({row['world'][0]:.0f},{row['world'][1]:.0f}) 읽음 {row['read_height_cm']:.1f} 기대 {row['expected_height_cm'] if row['expected_height_cm'] is None else round(row['expected_height_cm'], 1)} 차 {format_value(row['difference_cm'], 2)}{'' if row['valid'] else ' (무효 텍셀)'}")
    return "\n".join(lines)


def run_order(arguments):
    solids = stage.build_solids()
    result = {}
    for item in arguments.log:
        path = Path(item)
        if not path.is_file():
            raise InputProblem(f"로그 조각이 없다: {path}")
        parsed = parse_log_lines(path.read_text(encoding="utf-8", errors="replace").splitlines())
        result[path.parent.name if path.name == "log_slice.txt" else path.name] = analyze_order(parsed, solids)
    for label, analysis in result.items():
        print(build_order_text(label, analysis))
        print()
    if arguments.out:
        gv.base.write_json(arguments.out, result)
    return EXIT_OK


def write_fake_csv(path, rows, columns):
    with open(path, "w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["stub"])
        for row in rows:
            writer.writerow(row)
        writer.writerow(columns)
        writer.writerow([gpu_csv.HEADER_AT_END_MARKER, "1"])


def run_self_test():
    failures = []

    def check(condition, label, detail=""):
        if not condition:
            failures.append(label)
        print(("통과 " if condition else "실패 ") + label + (": " + detail if detail else ""))

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        plan = build_plan(DEFAULT_QUALITIES, DEFAULT_FRAMES, False, root / "cost")
        names = [entry["name"] for entry in plan["variants"]]
        check(names == ["q2_warm", "q2_on_a", "q2_off_a", "q2_off_b", "q2_on_b", "q0_warm", "q0_on_a", "q0_off_a", "q0_off_b", "q0_on_b"], "걷는 평균 계획: 품질 2·0 각각 예열 + 켬·끔·끔·켬", ",".join(names))
        on_block = next(entry for entry in plan["variants"] if entry["name"] == "q2_on_a")
        off_block = next(entry for entry in plan["variants"] if entry["name"] == "q2_off_a")
        check(on_block["apply"]["move"]["distance_cm"] == 3000.0 and plan["base"]["speed"] == 520.0 and "csvprofile frames=300" in on_block["apply"]["console"], "걷기 3000cm·초속 520·csvprofile frames=300")
        check(on_block["apply"]["tuning_reload"]["tuning"] != off_block["apply"]["tuning_reload"]["tuning"] and (root / "cost" / "tuning_on.json").is_file() and (root / "cost" / "tuning_off.json").is_file(), "켬·끔 조절 표 사본이 다름")
        on_tuning = json.loads((root / "cost" / "tuning_on.json").read_text(encoding="utf-8"))["Globals"][gv.base.SETTINGS_GROUP_NAME]
        off_tuning = json.loads((root / "cost" / "tuning_off.json").read_text(encoding="utf-8"))["Globals"][gv.base.SETTINGS_GROUP_NAME]
        check(on_tuning[gv.FOLLOW_FIELD] is True and on_tuning[gv.GROUND_MATERIAL_FIELD] == gv.GROUND_MATERIAL_SOFT_PATH and off_tuning[gv.FOLLOW_FIELD] is False, "켬 사본은 땅 따라가기+재질 경로, 끔 사본은 끔")
        forced = build_plan((2,), 120, True, root / "forced")
        forced_names = [entry["name"] for entry in forced["variants"]]
        check(forced_names == ["q2_warm", "q2_normal_a", "q2_forced_a", "q2_forced_b", "q2_normal_b"] and "TD.Fog3D.GroundForceRecapture 1" in next(entry for entry in forced["variants"] if entry["name"] == "q2_forced_a")["apply"]["console"], "다시 찍는 프레임 계획: 보통·강제·강제·보통, 강제 콘솔 변수")
        check(all("move" not in entry["apply"] for entry in forced["variants"]), "다시 찍는 프레임 계획은 서 있는 폰(걷지 않음)")

        columns = ["Frame", "GPUTime", "FrameTime", "GPU/VolumetricFog", "TDFog3D/GameThread/GroundCaptureRequest"]
        rows_a = [[index, 5.05, 8.0, 1.0, 0.02] for index in range(160)]
        rows_b = [[index, 5.0, 8.0, 1.0, 0.0] for index in range(160)]
        labels = {"q2_on_a": rows_a, "q2_off_a": rows_b, "q2_off_b": rows_b, "q2_on_b": rows_a}
        paths = {}
        for label, rows in labels.items():
            paths[label] = root / f"{label}.csv"
            write_fake_csv(paths[label], rows, columns)
        summaries = {label: summarize_csv(path) for label, path in paths.items()}
        check(summaries["q2_on_a"]["columns"]["GPUTime"]["count"] == 100 and summaries["q2_on_a"]["missing_proposed"][0] == "TDFog3D/GameThread/GroundCaptureList", "CSV 집계: 앞 60행 버림(160→100행), CSV 에 없는 새 통계를 표시")
        differences = pair_differences(summaries)
        check(abs(differences["q2"]["GPUTime"]["mean_difference"] - 0.05) < 1e-9 and abs(differences["q2"]["TDFog3D/GameThread/GroundCaptureRequest"]["mean_difference"] - 0.02) < 1e-9, "켬 − 끔 평균 차 0.05 / 0.02")
        text = build_summary_text(summaries, differences)
        check("| q2_on_a | GPUTime |" in text and "q2 on−off" in text, "집계 표 구성")

        solids = stage.build_solids()
        stair = stage.STATION_CENTERS_CM["ST"]
        lines = [
            "LogTDFog3DZone: Verbose: converted the ground capture requested at frame 100 in frame 102 (min cell 1,2, size 384)",
            "LogTDFog3DZone: Verbose: converted the ground capture requested at frame 140 in frame 143 (min cell 3,2, size 384)",
            "LogTDFog3DZone: Verbose: converted the ground capture requested at frame 180 in frame 182 (min cell 5,2, size 384)",
            "LogTDFog3D: Log: Fog3D ground recapture: frame=100 reason=enter windowMinCell=(1,2)",
            "LogTDFog3D: Log: Fog3D ground recapture: frame=140 reason=window windowMinCell=(3,2)",
            "LogTDFog3D: Log: Fog3D ground recapture: frame=180 reason=window windowMinCell=(5,2)",
            "LogTDFog3D: Verbose: Fog3D ground deferred: frame=101 steps=1 total=1 reason=entering",
            "LogTDFog3D: Verbose: Fog3D ground deferred: frame=141 steps=2 total=3 reason=waiting_capture",
            f"LogTDFog3D: Log: Fog3D ground readback: frame=200 index=0 world=({stair[0] - 380.0:.1f},{stair[1]:.1f}) height_cm=20.0 valid=1",
            f"LogTDFog3D: Log: Fog3D ground readback: frame=200 index=1 world=({stair[0] - 220.0:.1f},{stair[1]:.1f}) height_cm=105.0 valid=1",
            f"LogTDFog3D: Log: Fog3D ground readback: frame=200 index=2 world=({stair[0] + 600.0:.1f},{stair[1]:.1f}) height_cm=-9999.0 valid=0",
            "LogTDFog3D: Log: Fog3D zone summary: submitted=500 rejected=0 deferred=3",
        ]
        parsed = parse_log_lines(lines)
        analysis = analyze_order(parsed, solids)
        check(analysis["lag_frames"]["histogram"] == {2: 2, 3: 1} and analysis["lag_frames"]["max"] == 3, "캡처 순서: 지연 분포 {2:2, 3:1}", str(analysis["lag_frames"]["histogram"]))
        check(analysis["recaptures"]["by_reason"] == {"enter": 1, "window": 2} and analysis["recaptures"]["window_gap_frames"]["median"] == 40, "재캡처 사유별 횟수와 창 이동 간격 40프레임")
        check(analysis["deferred_steps"]["total"] == 3 and analysis["deferred_steps"]["episodes_entering_or_teleport"] == [(101, 1, "entering")] and analysis["deferred_steps"]["summary_line_deferred"] == "3", "보류 걸음 합계 3, 첫 진입 보류, 끝 요약 deferred=3")
        rows = analysis["readback"]["rows"]
        check(rows[0]["expected_height_cm"] == 20.0 and abs(rows[0]["difference_cm"]) < 1e-9 and abs(rows[1]["difference_cm"] - 5.0) < 1e-9 and rows[2]["difference_cm"] is None and analysis["readback"]["invalid_count"] == 1, "정본 높이 되읽기: 계단 알려진 높이(20·100)와 비교, 무효 텍셀 분리", f"{rows[0]['expected_height_cm']} {rows[1]['expected_height_cm']}")
        log_file = root / "log_slice.txt"
        log_file.write_text("\n".join(lines) + "\n", encoding="utf-8")
        exit_code = run_order(parse_arguments(["order", "--log", str(log_file), "--out", str(root / "order.json")]))
        check(exit_code == EXIT_OK and (root / "order.json").is_file(), "order 명령이 표와 JSON 을 냄")
        empty = analyze_order(parse_log_lines(["아무 줄 없음"]), solids)
        check(empty["lag_frames"]["count"] == 0 and empty["deferred_steps"]["total"] == 0 and empty["readback"]["max_abs_difference_cm"] is None, "해당 줄이 없는 로그는 빈 표(C++ 가 아직 안 낸 경우)")
    print("자체 점검", "통과" if not failures else "실패 " + str(len(failures)) + "건")
    return 0 if not failures else 1


def parse_arguments(argument_list):
    parser = argparse.ArgumentParser(description="3D 안개 2a 캡처 비용·순서 보고 뼈대")
    parser.add_argument("--self-test", action="store_true")
    commands = parser.add_subparsers(dest="command")
    plan_parser = commands.add_parser("plan")
    plan_parser.add_argument("--quality", nargs="+", type=int, default=list(DEFAULT_QUALITIES))
    plan_parser.add_argument("--frames", type=int, default=DEFAULT_FRAMES)
    plan_parser.add_argument("--forced", action="store_true", help="다시 찍는 프레임 비용 계획(필요한 C++: TD.Fog3D.GroundForceRecapture)")
    plan_parser.add_argument("--out")
    summarize_parser = commands.add_parser("summarize")
    summarize_parser.add_argument("--csv", nargs="+")
    summarize_parser.add_argument("--latest", type=int)
    summarize_parser.add_argument("--labels", nargs="+")
    summarize_parser.add_argument("--out")
    order_parser = commands.add_parser("order")
    order_parser.add_argument("--log", nargs="+", required=True)
    order_parser.add_argument("--out")
    return parser.parse_args(argument_list)


def main(argument_list=None):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    arguments = parse_arguments(argument_list)
    if arguments.self_test:
        return run_self_test()
    try:
        if arguments.command == "plan":
            return run_plan(arguments)
        if arguments.command == "summarize":
            return run_summarize(arguments)
        if arguments.command == "order":
            return run_order(arguments)
    except (InputProblem, gv.InputProblem, gv.base.InputProblem, OSError, ValueError) as error:
        print("입력 문제:", error)
        return EXIT_INPUT_PROBLEM
    print("명령이 필요하다: plan | summarize | order | --self-test")
    return EXIT_INPUT_PROBLEM


if __name__ == "__main__":
    sys.exit(main())
