"""실행 환경: 시스템 Python(numpy·Pillow 필요, 언리얼 에디터 불필요). 3D 안개 2a-3 화면 확인 촬영 결과 폴더에서 숫자(정지: 안개 켬 − 끔 밝기 차 = fog3d_visible_check.check_zone_visible 그대로, 걷기: 걷기 자국 대비 = fog3d_calib_report 의 영역·평균 절대 차 그대로)·유효 조건·로그 요약·눈 판정용 그림과 움직이는 그림을 만든다. 문턱·합격 판정은 넣지 않고 값만 낸다.
실행 명령: python -B Tools/FluidFX/fog3d_ground_report.py --runs <실행 이름...> | --all | --batch <묶음> [--no-images] [--snapshot-log <실행 이름> [--log Saved/Logs/TDGame.log]], 점검만: --self-test
출력: Saved/FluidFX/fog3d_ground/<실행>/report/{report.json, sheet.png 또는 strip.png, blink.webp·walk.webp·run.webp}, Saved/FluidFX/fog3d_ground/report/{summary.json, summary.md, pairs/*.png·*.webp}(땅 따라가기 켬 대 끔 짝), 로그 보관 시 <실행>/log_slice.txt. 종료 코드 0 모두 유효 / 1 무효 실행 있음 / 2 입력 문제.
상태: 에디터 없이 합성 그림·합성 로그 --self-test 만 확인. 실제 PIE 촬영 그림에는 아직 돌리지 않았다(걷기 영역은 1단계 뒤 카메라 화면 비율 그대로라 비탈에서 폰·땅이 같은 자리에 오는지는 첫 촬영 그림으로 확인한다).
"""
import argparse
import json
import math
import re
import sys
import tempfile
from pathlib import Path

import numpy
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
for directory in (ROOT / "Tools", ROOT / "Tools" / "FluidFX"):
    if str(directory) not in sys.path:
        sys.path.insert(0, str(directory))
import fog3d_calib_report as rep  # noqa: E402
import fog3d_ground_stage as stage  # noqa: E402
import fog3d_ground_variants as gv  # noqa: E402
import fog3d_visible_check as vc  # noqa: E402

GROUND_ROOT = gv.GROUND_ROOT
REPORT_DIRECTORY_NAME = "report"
CAPTURE_SUBDIRECTORY = "capture"
KNOWN_ENSURE_MARKERS = ("InjectionSiteTrait.cpp", "NavigationSystem.h")
QUALITY_LINE_PATTERN = re.compile(r'TD\.Fog3D\.Quality\s*=\s*"(-?\d+)"')
QUALITY_EXPECTED_VALUE = gv.QUALITY_FIXED_LEVEL
QUALITY_MINIMUM_LINES = 2
WALK_MOVED_MINIMUM_FRACTION = 0.9
RUN_LATE_SHOT_MAXIMUM_FRACTION = 0.1
EXIT_ALL_VALID = 0
EXIT_SOME_INVALID = 1
EXIT_INPUT_PROBLEM = 2
STRIP_TILE_WIDTH = 320
STRIP_COLUMNS = 5
PAIR_TILE_WIDTH = 480
ANIMATION_WIDTH = 640
BLINK_MILLISECONDS = 1000
WALK_FRAME_MILLISECONDS = 500
RUN_FRAME_MILLISECONDS = 100
DIFFERENCE_GAIN = rep.COMPARE_DIFFERENCE_GAIN


class InputProblem(Exception):
    pass


def read_json(path):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise InputProblem(f"JSON 을 읽을 수 없다: {path}: {error}")


def classify_ensure_lines(lines):
    ensure_lines = [line for line in lines if "Ensure condition failed" in line]
    known = [line for line in ensure_lines if any(marker in line for marker in KNOWN_ENSURE_MARKERS)]
    return {"known_unrelated": len(known), "new": len(ensure_lines) - len(known), "new_examples": [line[:300] for line in ensure_lines if line not in known][:3]}


def read_log_summary(run_directory):
    snapshot = Path(run_directory) / rep.LOG_SNAPSHOT_FILE_NAME
    if not snapshot.is_file():
        return {"source": "none", "usable": None}
    lines = snapshot.read_text(encoding="utf-8", errors="replace").splitlines()
    summary = rep.summarize_log(lines, "snapshot")
    summary["ensure"] = classify_ensure_lines(lines)
    summary["usable"] = bool(summary["usable"] and summary["ensure"]["new"] == 0 and summary["rdg_error_lines"] == 0)
    return summary


def read_quality_values(run_directory):
    snapshot = Path(run_directory) / rep.LOG_SNAPSHOT_FILE_NAME
    if not snapshot.is_file():
        return None
    return [int(match.group(1)) for line in snapshot.read_text(encoding="utf-8", errors="replace").splitlines() for match in QUALITY_LINE_PATTERN.finditer(line)]


def find_quality_problems(values):
    if values is None:
        return []
    if len(values) < QUALITY_MINIMUM_LINES:
        return [f"품질 변수 값 줄이 {len(values)}개뿐(시작·끝 모두 필요)"]
    wrong = sorted({value for value in values if value != QUALITY_EXPECTED_VALUE})
    return [f"품질 변수 값이 {QUALITY_EXPECTED_VALUE} 이 아님: {wrong}"] if wrong else []


def load_run(run_directory):
    run_directory = Path(run_directory)
    run_record = read_json(run_directory / "run.json")
    capture_directory = run_directory / CAPTURE_SUBDIRECTORY
    try:
        schedule_status = read_json(rep.find_schedule_file(capture_directory))
        walk_status = rep.find_walk_status(capture_directory, schedule_status, capture_directory / "result.json") if run_record["kind"] != "static" else None
        matched = rep.match_shots(run_record, schedule_status, capture_directory)
    except (rep.InputProblem, OSError, KeyError, ValueError) as error:
        raise InputProblem(str(error))
    return run_record, schedule_status, walk_status, matched


def evaluate_validity(run_record, schedule_status, walk_status, matched):
    reasons, notes = [], []
    missing = [entry["id"] for entry in run_record["shots"] if entry["id"] not in matched or not matched[entry["id"]]["exists"]]
    if missing:
        reasons.append("장 파일 없음: " + ", ".join(missing[:8]) + (f" 외 {len(missing) - 8}장" if len(missing) > 8 else ""))
    events_done = schedule_status.get("events_done", [])
    if len(events_done) != len(run_record["events"]):
        reasons.append(f"이벤트 {len(events_done)}/{len(run_record['events'])}개만 실행됨")
    event_late = [float(event["game_time"]) - float(event["time"]) for event in events_done]
    capture_gaps = [float(entry["shot"]["captured_game_time"]) - float(entry["shot"]["time"]) for entry in matched.values() if entry["shot"].get("captured_game_time") is not None]
    details = {"event_late_max_seconds": max(event_late) if event_late else None, "shot_capture_delay_max_seconds": max(capture_gaps) if capture_gaps else None, "events_done": len(events_done)}
    if details["event_late_max_seconds"] is not None and details["event_late_max_seconds"] > rep.EVENT_LATE_NOTE_SECONDS:
        notes.append(f"이벤트가 최대 {details['event_late_max_seconds']:.3f}초 늦었다")
    errors = [event for event in events_done if isinstance(event.get("move"), dict) and event["move"].get("error")]
    if errors:
        reasons.append("이동 이벤트 오류: " + str(errors[0]["move"]["error"])[:120])
    relocate = [event for event in events_done if isinstance(event.get("move"), dict) and event["move"].get("mode") in ("teleport", "navigation")]
    if relocate and relocate[0]["move"]["mode"] != "teleport":
        notes.append("시작점 이동이 순간이동이 아니라 내비 이동이었다(출발 자세가 달라질 수 있다)")
    conditions = run_record["valid_conditions"]
    if run_record["kind"] in ("walk", "run"):
        if walk_status is None:
            reasons.append("걷기 기록 없음")
        else:
            details["walk_moved_cm"] = walk_status.get("moved_cm")
            details["walk_timed_out"] = walk_status.get("timed_out")
            if not walk_status.get("done") or walk_status.get("timed_out") or walk_status.get("error"):
                reasons.append(f"걷기가 끝나지 않았거나 오류: done={walk_status.get('done')} timed_out={walk_status.get('timed_out')} error={walk_status.get('error')}")
            else:
                end_time = float(walk_status["start_game_time"]) + float(walk_status["elapsed_game_seconds"]) - float(schedule_status["reference_game_time"])
                details["walk_end_game_seconds"] = end_time
                limit = conditions.get("walk_end_before_seconds", conditions.get("run_end_latest_seconds"))
                if limit is not None and end_time >= limit:
                    reasons.append(f"걷기가 {end_time:.3f}초에 끝나 {limit}초 전이라는 조건 위반")
                moved = float(walk_status.get("moved_cm") or 0.0)
                if moved < WALK_MOVED_MINIMUM_FRACTION * run_record["walk_distance_cm"]:
                    reasons.append(f"걸은 거리 {moved:.0f}cm 가 요청 {run_record['walk_distance_cm']:.0f}cm 의 {WALK_MOVED_MINIMUM_FRACTION:.0%} 미만(벽에 막혔을 수 있다)")
    if run_record["kind"] == "run":
        late = sum(1 for gap_entry in matched.values() if gap_entry["shot"].get("captured_game_time") is not None and float(gap_entry["shot"]["captured_game_time"]) - float(gap_entry["shot"]["time"]) > conditions["late_limit_seconds"])
        details["shots_over_late_limit"] = late
        if matched and late > RUN_LATE_SHOT_MAXIMUM_FRACTION * len(matched):
            reasons.append(f"찍힌 게임 시각이 {conditions['late_limit_seconds']}초 넘게 늦은 장 {late}/{len(matched)}")
    return {"reasons": reasons, "details": details, "notes": notes}


def luma_region_mask(shape, region):
    return rep.box_mask(shape[0], shape[1], region)


def compute_static_numbers(lumas):
    zone = vc.check_zone_visible({"shown_a": lumas["A1"], "shown_b": lumas["A2"], "hidden_a": lumas["H1"], "hidden_b": lumas["H2"]})
    return {"zone_visible": zone, "difference_255": zone["difference"] * 255.0, "noise_255": zone["noise"] * 255.0}


def compute_walk_numbers(lumas):
    height, width = lumas[gv.WALK_STOPPED_IDS[0]].shape
    mask = rep.walk_mask(height, width)
    reference = lumas[gv.WALK_REFERENCE_IDS[0]]
    contrast = {identifier: rep.mean_absolute_difference(lumas[identifier], reference, mask) for identifier in lumas if identifier not in gv.WALK_REFERENCE_IDS and identifier != gv.WALK_HIDDEN_ID}
    hidden = lumas.get(gv.WALK_HIDDEN_ID)
    visible = rep.mean_absolute_difference(reference, hidden, mask) if hidden is not None else None
    noise = rep.mean_absolute_difference(lumas[gv.WALK_REFERENCE_IDS[0]], lumas[gv.WALK_REFERENCE_IDS[1]], mask)
    representative = contrast[gv.WALK_REPRESENTATIVE_ID]
    return {
        "region_rows_cols": list(rep.walk_region(height, width)),
        "Wk": {identifier: contrast[identifier] for identifier in gv.WALK_STOPPED_IDS},
        "Wk_moving_information_only": {identifier: value for identifier, value in contrast.items() if identifier not in gv.WALK_STOPPED_IDS},
        "noise_W": noise, "Wk_representative": representative, "Wk_over_noise": representative / noise if noise > 0.0 else None,
        "visible_V": visible, "Wk_over_V": representative / visible if visible else None,
        "reference_vs_stopped_mean_luma_255": (float(lumas[gv.WALK_STOPPED_IDS[-1]][mask].mean()) - float(reference[mask].mean())) * rep.LUMA_TO_BYTE_SCALE,
    }


def compute_run_numbers(lumas, run_record):
    ordered = [entry["id"] for entry in run_record["shots"] if entry["id"] in lumas]
    height, width = lumas[ordered[0]].shape
    mask = rep.box_mask(height, width, vc.zone_region(height, width))
    series = [rep.mean_absolute_difference(lumas[ordered[index + 1]], lumas[ordered[index]], mask) for index in range(len(ordered) - 1)]
    median = float(numpy.median(series)) if series else 0.0
    return {"frame_to_frame_mean_abs_diff_255": series, "information_only": True, "median": median, "max": max(series) if series else None,
            "p95": float(numpy.percentile(series, 95)) if series else None, "max_over_median": (max(series) / median) if series and median > 0.0 else None}


def label_frame(image, text, size=14):
    return rep.label_tile(image, text, rep.font_for(size))


def load_rgb(path, width):
    with Image.open(path) as source:
        return rep.resize_to_width(source.convert("RGB"), width)


def save_animation(frames, path_stem, duration):
    if not frames:
        return None
    webp_path = Path(str(path_stem) + ".webp")
    try:
        frames[0].save(webp_path, save_all=True, append_images=frames[1:], duration=duration, loop=0, format="WEBP", quality=85)
        return webp_path
    except (OSError, ValueError, KeyError):
        webp_path.unlink(missing_ok=True)
    gif_path = Path(str(path_stem) + ".gif")
    frames[0].save(gif_path, save_all=True, append_images=frames[1:], duration=duration, loop=0, format="GIF")
    return gif_path


def build_sheet(matched, ordered_ids, columns, tile_width, output_path):
    tiles = []
    for identifier in ordered_ids:
        entry = matched.get(identifier)
        if entry is None or not entry["exists"]:
            tiles.append(Image.new("RGB", (tile_width, int(tile_width * 9 / 16)), (60, 0, 0)))
            continue
        tiles.append(label_frame(load_rgb(entry["path"], tile_width), f"{identifier} @{entry['entry']['time']:g}s {entry['entry']['role']}", 12))
    tile_height = max(tile.height for tile in tiles)
    rows = math.ceil(len(tiles) / columns)
    sheet = Image.new("RGB", (tile_width * columns, tile_height * rows), (20, 20, 20))
    for index, tile in enumerate(tiles):
        sheet.paste(tile, ((index % columns) * tile_width, (index // columns) * tile_height))
    sheet.save(output_path)
    return sheet.size


def write_run_images(run_record, matched, report_directory):
    images = {}
    kind = run_record["kind"]
    if kind == "static":
        images["sheet"] = report_directory.joinpath("sheet.png").as_posix()
        build_sheet(matched, ["A1", "A2", "H1", "H2"], 2, PAIR_TILE_WIDTH, report_directory / "sheet.png")
        frames = [label_frame(load_rgb(matched[identifier]["path"], ANIMATION_WIDTH), text, 16) for identifier, text in (("A1", "안개 켬 A1"), ("H1", "안개 끔(구역 숨김) H1"))]
        images["blink"] = str(save_animation(frames, report_directory / "blink", BLINK_MILLISECONDS))
    elif kind == "walk":
        ordered = [entry["id"] for entry in run_record["shots"]]
        images["strip"] = report_directory.joinpath("strip.png").as_posix()
        build_sheet(matched, ordered, STRIP_COLUMNS, STRIP_TILE_WIDTH, report_directory / "strip.png")
        frames = [label_frame(load_rgb(matched[identifier]["path"], ANIMATION_WIDTH), f"{identifier} {matched[identifier]['entry']['role']}", 16) for identifier in ordered]
        images["walk"] = str(save_animation(frames, report_directory / "walk", WALK_FRAME_MILLISECONDS))
    else:
        ordered = [entry["id"] for entry in run_record["shots"]]
        frames = [label_frame(load_rgb(matched[identifier]["path"], ANIMATION_WIDTH), f"{identifier}s", 14) for identifier in ordered]
        images["run"] = str(save_animation(frames, report_directory / "run", RUN_FRAME_MILLISECONDS))
    return images


def analyze_run(run_directory, no_images):
    run_directory = Path(run_directory)
    name = run_directory.name
    report_directory = run_directory / REPORT_DIRECTORY_NAME
    report_directory.mkdir(parents=True, exist_ok=True)
    try:
        run_record, schedule_status, walk_status, matched = load_run(run_directory)
    except InputProblem as error:
        report = {"name": name, "valid": False, "invalid_reasons": [f"촬영 결과를 읽을 수 없다: {error}"], "numbers": None, "usable": None}
        rep_write(report_directory / "report.json", report)
        return report, None, None
    validity = evaluate_validity(run_record, schedule_status, walk_status, matched)
    log = read_log_summary(run_directory)
    quality_values = read_quality_values(run_directory)
    validity["reasons"] = validity["reasons"] + find_quality_problems(quality_values)
    report = {"name": name, "kind": run_record["kind"], "station": run_record["station"], "line": run_record["line"], "camera": run_record["camera"], "follow": run_record["follow"],
              "description": run_record.get("description", ""), "valid": not validity["reasons"], "invalid_reasons": validity["reasons"], "validity": validity["details"], "notes": validity["notes"], "log": log,
              "canonical_tuning_sha256": run_record.get("canonical_tuning_sha256"),
              "quality_cvar_log_values": quality_values, "requested_distance_cm": run_record.get("walk_distance_cm")}
    log_usable = log["usable"]
    report["usable"] = bool(report["valid"] and log_usable) if log_usable is not None else None
    if log_usable is False:
        report["invalid_reasons"] = report["invalid_reasons"] + ["로그: Fog3D 경고·오류·거부, 새 ensure, RDG 오류가 있거나 요약 줄이 없다"]
    lumas = None
    if not report["valid"] and any("장 파일 없음" in reason for reason in report["invalid_reasons"]):
        report["numbers"] = None
    else:
        lumas = {identifier: vc.load_luma(entry["path"]) for identifier, entry in matched.items()}
        kind = run_record["kind"]
        report["numbers"] = compute_static_numbers(lumas) if kind == "static" else compute_walk_numbers(lumas) if kind == "walk" else compute_run_numbers(lumas, run_record)
        if not no_images:
            report["images"] = write_run_images(run_record, matched, report_directory)
    rep_write(report_directory / "report.json", report)
    return report, matched, lumas


def rep_write(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")


def follow_pair_name(name):
    if name.endswith("_on"):
        return name[:-3] + "_off"
    if name.endswith("_off"):
        return name[:-4] + "_on"
    return None


def pair_numbers(on_report, off_report, on_lumas, off_lumas):
    if on_report["kind"] == "static":
        shape = on_lumas["A1"].shape
        mask = rep.box_mask(shape[0], shape[1], vc.zone_region(*shape))
        difference = rep.mean_absolute_difference(on_lumas["A1"], off_lumas["A1"], mask)
        noise = max(rep.mean_absolute_difference(on_lumas["A1"], on_lumas["A2"], mask), rep.mean_absolute_difference(off_lumas["A1"], off_lumas["A2"], mask))
        return {"on_vs_off_difference_255": difference, "noise_255": noise, "ratio": difference / noise if noise > 0.0 else None}
    if on_report["kind"] == "walk":
        on_value, off_value = on_report["numbers"]["Wk_representative"], off_report["numbers"]["Wk_representative"]
        return {"Wk_on": on_value, "Wk_off": off_value, "on_over_off": on_value / off_value if off_value > 0.0 else None}
    return {}


def write_pair_images(name_on, on_matched, off_matched, output_directory, ids):
    output_directory.mkdir(parents=True, exist_ok=True)
    first = ids[0]
    on_image = load_rgb(on_matched[first]["path"], PAIR_TILE_WIDTH)
    off_image = load_rgb(off_matched[first]["path"], PAIR_TILE_WIDTH)
    difference = numpy.clip(numpy.abs(numpy.asarray(on_image, dtype=numpy.float64) - numpy.asarray(off_image, dtype=numpy.float64)) * DIFFERENCE_GAIN, 0.0, 255.0).astype(numpy.uint8)
    difference_image = Image.fromarray(difference)
    sheet = Image.new("RGB", (PAIR_TILE_WIDTH * 3, on_image.height), (20, 20, 20))
    for index, (image, text) in enumerate(((on_image, "땅 따라가기 켬"), (off_image, "땅 따라가기 끔"), (difference_image, f"차이 x{DIFFERENCE_GAIN:g}"))):
        sheet.paste(label_frame(image.copy(), f"{first} {text}", 14), (index * PAIR_TILE_WIDTH, 0))
    sheet_path = output_directory / f"{name_on}_vs_off.png"
    sheet.save(sheet_path)
    frames = []
    for identifier in ids:
        left = load_rgb(on_matched[identifier]["path"], ANIMATION_WIDTH // 2)
        right = load_rgb(off_matched[identifier]["path"], ANIMATION_WIDTH // 2)
        joined = Image.new("RGB", (left.width * 2, left.height))
        joined.paste(label_frame(left.copy(), f"켬 {identifier}", 12), (0, 0))
        joined.paste(label_frame(right.copy(), f"끔 {identifier}", 12), (left.width, 0))
        frames.append(joined)
    return {"sheet": sheet_path.as_posix(), "animation": str(save_animation(frames, output_directory / f"{name_on}_vs_off", RUN_FRAME_MILLISECONDS if len(ids) > 20 else WALK_FRAME_MILLISECONDS))}


def format_number(value, digits=3):
    return "-" if value is None else f"{value:.{digits}f}"


def build_markdown(reports, pairs):
    lines = ["# 3D 안개 2a-3 화면 확인 숫자 (문턱 없음, 값만)", ""]
    static = [report for report in reports if report.get("kind") == "static"]
    walk = [report for report in reports if report.get("kind") == "walk"]
    runs = [report for report in reports if report.get("kind") == "run"]
    lines += ["## 실행 유효·품질 확인(걸은 거리 백분율의 분모는 실행의 요청 거리)", "", "| 실행 | 유효 | 품질 변수 값(로그 줄) | 걸은 거리(cm) | 요청(cm) | 백분율 | 시간 초과 |", "|---|---|---|---|---|---|---|"]
    for report in reports:
        details = report.get("validity") or {}
        moved, requested = details.get("walk_moved_cm"), report.get("requested_distance_cm")
        percent = f"{100.0 * moved / requested:.1f}%" if moved is not None and requested else "-"
        values = report.get("quality_cvar_log_values")
        lines.append(f"| {report['name']} | {'예' if report['valid'] else '무효'} | {'줄 없음' if values is None else ' '.join(str(value) for value in values) or '없음'} | {format_number(moved, 0)} | {format_number(requested, 0)} | {percent} | {details.get('walk_timed_out', '-')} |")
    static_differences = {report["name"]: report["numbers"]["difference_255"] for report in static if report.get("numbers")}

    def flat_static_denominator(report, camera):
        return static_differences.get(f"s_F_{camera}_{'on' if report['follow'] else 'off'}")

    lines += ["", "## 정지: 안개 켬 − 끔 밝기 차(255 기준)와 평지 칸(F) 대비 비(등록 식 분모 = 평지 옆 카메라, 켬·끔 같은 쪽)", "",
              "| 실행 | 칸 | 카메라 | 땅 따라가기 | 차 | 잡음 | 비 | 평지 옆 분모 비 | 같은 카메라 분모 비 | 유효 |", "|---|---|---|---|---|---|---|---|---|---|"]
    for report in static:
        numbers = report.get("numbers")
        zone = numbers["zone_visible"] if numbers else None
        difference = numbers["difference_255"] if numbers else None
        side_denominator = flat_static_denominator(report, "side")
        same_denominator = flat_static_denominator(report, report["camera"])
        side_ratio = difference / side_denominator if difference is not None and side_denominator else None
        same_ratio = difference / same_denominator if difference is not None and same_denominator else None
        lines.append(f"| {report['name']} | {report['station']} | {report['camera']} | {'켬' if report['follow'] else '끔'} | {format_number(difference)} | {format_number(numbers['noise_255'] if numbers else None)} | {format_number(zone['ratio'] if zone else None, 1)} | {format_number(side_ratio)} | {format_number(same_ratio)} | {'예' if report['usable'] else '아니오: ' + '; '.join(report['invalid_reasons'])[:80]} |")
    lines += ["", "## 걷기: 걷기 자국 대비 Wk(255 기준, 대표 장 " + gv.WALK_REPRESENTATIVE_ID + ")", "", "| 실행 | 선 | 칸 | 카메라 | 땅 따라가기 | Wk 대표 | 정지 장 다섯 | 잡음 N_W | Wk/잡음 | 평지(W01) 대비 | 기준 장 차 | 안개 보임 V | Wk÷V | Wk÷V 평지(W01) 대비 | 유효 |", "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    flat_values = {(report["camera"], report["follow"]): report["numbers"]["Wk_representative"] for report in walk if report["line"] == "W01" and report.get("numbers")}
    flat_over_visible = {(report["camera"], report["follow"]): report["numbers"].get("Wk_over_V") for report in walk if report["line"] == "W01" and report.get("numbers")}
    for report in walk:
        numbers = report.get("numbers")
        flat = flat_values.get((report["camera"], report["follow"]))
        stopped = " / ".join(format_number(numbers["Wk"][identifier], 2) for identifier in gv.WALK_STOPPED_IDS) if numbers else "-"
        versus = format_number(numbers["Wk_representative"] / flat, 2) if numbers and flat else "-"
        flat_quotient = flat_over_visible.get((report["camera"], report["follow"]))
        quotient = numbers.get("Wk_over_V") if numbers else None
        quotient_versus = format_number(quotient / flat_quotient, 2) if quotient is not None and flat_quotient else "-"
        lines.append(f"| {report['name']} | {report['line']} | {report['station']} | {report['camera']} | {'켬' if report['follow'] else '끔'} | {format_number(numbers['Wk_representative'] if numbers else None, 2)} | {stopped} | {format_number(numbers['noise_W'] if numbers else None, 2)} | {format_number(numbers['Wk_over_noise'] if numbers else None, 1)} | {versus} | {format_number(numbers['reference_vs_stopped_mean_luma_255'] if numbers else None, 2)} | {format_number(numbers.get('visible_V') if numbers else None, 2)} | {format_number(quotient, 3)} | {quotient_versus} | {'예' if report['usable'] else '아니오: ' + '; '.join(report['invalid_reasons'])[:80]} |")
    lines += ["", "## 창 이동: 앞뒤 장 평균 절대 차(정보, 255 기준)", "", "| 실행 | 장 수 | 중앙값 | 95% | 최대 | 최대/중앙값 | 유효 |", "|---|---|---|---|---|---|---|"]
    for report in runs:
        numbers = report.get("numbers")
        lines.append(f"| {report['name']} | {len(numbers['frame_to_frame_mean_abs_diff_255']) + 1 if numbers else '-'} | {format_number(numbers['median'] if numbers else None, 2)} | {format_number(numbers['p95'] if numbers else None, 2)} | {format_number(numbers['max'] if numbers else None, 2)} | {format_number(numbers['max_over_median'] if numbers else None, 1)} | {'예' if report['usable'] else '아니오: ' + '; '.join(report['invalid_reasons'])[:80]} |")
    lines += ["", "## 땅 따라가기 켬 대 끔 짝", "", "| 켬 실행 | 값 |", "|---|---|"]
    for name, values in pairs.items():
        lines.append(f"| {name} | " + ", ".join(f"{key} {format_number(value, 3)}" for key, value in values.items() if key != "images") + " |")
    lines += ["", "## 로그(실행마다)", "", "| 실행 | Fog3D 경고·오류 | 거부 | 제출 | 새 ensure | 알려진 ensure | RDG 오류 |", "|---|---|---|---|---|---|---|"]
    for report in reports:
        log = report.get("log") or {}
        ensure = log.get("ensure") or {}
        lines.append(f"| {report['name']} | {log.get('fog3d_warning_or_error_lines', '-')} | {log.get('rejected', '-')} | {log.get('submitted', '-')} | {ensure.get('new', '-')} | {ensure.get('known_unrelated', '-')} | {log.get('rdg_error_lines', '-')} |")
    return "\n".join(lines) + "\n"


def list_run_names(root):
    root = Path(root)
    index_path = root / gv.INDEX_FILE_NAME
    if index_path.is_file():
        return [entry["name"] for entry in read_json(index_path)["runs"] if (root / entry["name"] / CAPTURE_SUBDIRECTORY).is_dir()]
    return sorted(path.name for path in root.iterdir() if path.is_dir() and (path / CAPTURE_SUBDIRECTORY).is_dir())


def run_reports(options):
    root = Path(options.out_root)
    names = list(options.runs or [])
    if options.batch:
        index = read_json(root / gv.INDEX_FILE_NAME)
        for batch in options.batch:
            names += [name for name in index["batches"].get(batch, []) if name not in names]
    if options.all:
        names += [name for name in list_run_names(root) if name not in names]
    if not names:
        raise InputProblem("--runs, --batch 또는 --all 이 필요하다")
    reports, collected = [], {}
    for name in names:
        report, matched, lumas = analyze_run(root / name, options.no_images)
        reports.append(report)
        if report.get("numbers") and lumas is not None:
            collected[name] = (report, matched, lumas)
        status = "무효" if not report["valid"] else "사용 불가" if report["usable"] is False else "유효"
        print(f"{name:<24} {status} {'; '.join(report['invalid_reasons'])[:100]}")
    pairs = {}
    for name, (report, matched, lumas) in collected.items():
        if not report["follow"] or report["kind"] == "run":
            continue
        other = follow_pair_name(name)
        if other not in collected:
            continue
        off_report, off_matched, off_lumas = collected[other]
        values = pair_numbers(report, off_report, lumas, off_lumas)
        if not options.no_images:
            ids = ["A1", "H1"] if report["kind"] == "static" else [entry for entry in gv.WALK_STOPPED_IDS]
            values["images"] = write_pair_images(name, matched, off_matched, root / REPORT_DIRECTORY_NAME / "pairs", ids if report["kind"] == "static" else [gv.WALK_REPRESENTATIVE_ID] + [i for i in ids if i != gv.WALK_REPRESENTATIVE_ID])
        pairs[name] = values
    summary_directory = root / REPORT_DIRECTORY_NAME
    summary_directory.mkdir(parents=True, exist_ok=True)
    rep_write(summary_directory / "summary.json", {"reports": reports, "pairs": pairs})
    (summary_directory / "summary.md").write_text(build_markdown(reports, pairs), encoding="utf-8", newline="\n")
    print("표", (summary_directory / "summary.md").as_posix())
    return EXIT_ALL_VALID if all(report["valid"] for report in reports) else EXIT_SOME_INVALID


def snapshot_log(options):
    run_directory = Path(options.out_root) / options.snapshot_log
    count, has_summary = rep.snapshot_run_log(run_directory, options.log)
    print(f"{options.snapshot_log}: 로그 {count}줄 보관({'요약 줄 있음' if has_summary else '요약 줄 없음 — PIE 가 아직 안 끝났을 수 있다'})")
    return EXIT_ALL_VALID if has_summary else EXIT_SOME_INVALID


def write_gray(path, gray):
    gray8 = numpy.clip(gray, 0, 255).astype(numpy.uint8)
    Image.fromarray(numpy.stack([gray8] * 3, axis=-1)).save(path)


def build_synthetic_run(root, spec, canonical, field_types, solids, level_gain, with_log=True, ensure_line=None, quality_values=(-1, -1, -1)):
    outcome = gv.materialize(spec, root, canonical, field_types, "0" * 64, solids, [], False, True)
    run_directory = Path(root) / spec["name"]
    capture = run_directory / CAPTURE_SUBDIRECTORY
    capture.mkdir(parents=True, exist_ok=True)
    record = outcome["record"]
    width, height = 320, 180
    walk_mask = rep.walk_mask(height, width)
    shots = []
    for index, entry in enumerate(record["shots"]):
        gray = numpy.full((height, width), 100.0)
        if record["kind"] == "static":
            gray = gray + (level_gain if entry["role"] == "shown" else 0.0) + (1.0 if entry["id"] in ("A2", "H2") else 0.0)
        elif record["kind"] == "walk":
            gain = {"G4.0": 5, "G4.4": 5, "G4.8": 5, "G6.1": 20, "G7.1": 16, "G8.1": 12, "G9.6": 8, "G11.6": 10, "G13.4": 0, "G13.9": 2, "G15.0": -10}[entry["id"]]
            gray = gray + numpy.where(walk_mask, gain * level_gain, 0.0)
        else:
            gray = gray + (index % 4 == 0) * 6.0
        path = capture / f"00_g_{round(entry['time'] * 1000):05d}ms.png"
        write_gray(path, gray)
        shots.append({"time": entry["time"], "path": path.as_posix(), "late_seconds": 0.0, "captured_game_time": entry["time"] + 0.0167, "saved": True})
    events_done = []
    for event in record["events"]:
        done = {"time": event["time"], "game_time": event["time"] + 0.01}
        if "move" in event and isinstance(event["move"], list):
            done["move"] = {"mode": "teleport", "goal": event["move"]}
        events_done.append(done)
    schedule = {"times": [shot["time"] for shot in shots], "reference": "now", "reference_game_time": 15.0, "prefix": "g", "shots": shots, "events_done": events_done, "done": True}
    (capture / "00_g_schedule.json").write_text(json.dumps(schedule), encoding="utf-8")
    if record["kind"] != "static":
        distance = record["walk_distance_cm"]
        start_time = next(event["time"] for event in record["events"] if event["kind"] == "run") if record["kind"] == "run" else gv.WALK_TIMELINE["walk"]
        walk_status = {"mode": "input", "start_game_time": 15.0 + start_time, "elapsed_game_seconds": distance / 600.0 + 0.4, "moved_cm": distance + 1.0, "timed_out": False, "done": True}
        (capture / f"00_event{round(start_time * 1000):05d}ms_walk.json").write_text(json.dumps(walk_status), encoding="utf-8")
    (capture / "result.json").write_text("{}", encoding="utf-8")
    if with_log:
        lines = ["LogTemp: start", "LogTDFog3D: Fog3D zone summary: submitted=120 rejected=0"] + [f'LogConsoleResponse: TD.Fog3D.Quality = "{value}"  LastSetBy: Console' for value in quality_values]
        if ensure_line:
            lines.append(ensure_line)
        (run_directory / rep.LOG_SNAPSHOT_FILE_NAME).write_text("\n".join(lines) + "\n", encoding="utf-8")
    return run_directory


def run_self_test():
    failures = []

    def check(condition, label, detail=""):
        if not condition:
            failures.append(label)
        print(("통과 " if condition else "실패 ") + label + (": " + detail if detail else ""))

    canonical = gv.base.load_canonical_tuning()
    field_types = gv.infer_field_types(canonical)
    solids = stage.build_solids()
    specs = {spec["name"]: spec for spec in gv.all_specifications()}
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        build_synthetic_run(root, specs["s_S30_side_on"], canonical, field_types, solids, 20.0)
        build_synthetic_run(root, specs["s_S30_side_off"], canonical, field_types, solids, 10.0)
        build_synthetic_run(root, specs["w_W01_back_on"], canonical, field_types, solids, 1.0)
        build_synthetic_run(root, specs["w_W02_back_on"], canonical, field_types, solids, 2.0)
        build_synthetic_run(root, specs["w_W02_back_off"], canonical, field_types, solids, 1.0)
        build_synthetic_run(root, specs["r_R01_side_on"], canonical, field_types, solids, 1.0)
        build_synthetic_run(root, specs["w_W03_back_on"], canonical, field_types, solids, 1.0, with_log=False)
        build_synthetic_run(root, specs["w_W04_back_on"], canonical, field_types, solids, 1.0, ensure_line="LogOutputDevice: Error: Ensure condition failed: Foo [File:Bar.cpp] [Line: 1]")
        build_synthetic_run(root, specs["w_W05_back_on"], canonical, field_types, solids, 1.0, ensure_line="LogOutputDevice: Error: Ensure condition failed: NavigationSystem.h:293 x")
        build_synthetic_run(root, specs["w_W07_back_on"], canonical, field_types, solids, 1.0, quality_values=(-1, 0, -1))
        build_synthetic_run(root, specs["w_W10_back_on"], canonical, field_types, solids, 1.0, quality_values=(-1,))
        build_synthetic_run(root, specs["s_F_side_on"], canonical, field_types, solids, 20.0)
        build_synthetic_run(root, specs["s_F_side_off"], canonical, field_types, solids, 10.0)
        build_synthetic_run(root, specs["s_ST_side_off"], canonical, field_types, solids, 5.0)
        missing_run = build_synthetic_run(root, specs["w_W06_back_on"], canonical, field_types, solids, 1.0)
        next(missing_run.joinpath(CAPTURE_SUBDIRECTORY).glob("00_g_06100ms.png")).unlink()
        names = ["s_S30_side_on", "s_S30_side_off", "w_W01_back_on", "w_W02_back_on", "w_W02_back_off", "r_R01_side_on", "w_W03_back_on", "w_W04_back_on", "w_W05_back_on", "w_W06_back_on", "w_W07_back_on", "w_W10_back_on", "s_F_side_on", "s_F_side_off", "s_ST_side_off"]
        options = parse_arguments(["--runs", *names, "--out-root", str(root)])
        exit_code = run_reports(options)
        reports = {report["name"]: report for report in read_json(root / REPORT_DIRECTORY_NAME / "summary.json")["reports"]}
        static_on = reports["s_S30_side_on"]["numbers"]
        check(abs(static_on["difference_255"] - 19.5) < 0.6 or abs(static_on["difference_255"] - 20.0) < 1.1, "정지 켬 − 끔 밝기 차 ≈ 합성 20/255", format_number(static_on["difference_255"], 2))
        check(reports["s_S30_side_on"]["usable"] is True and reports["s_S30_side_on"]["images"]["blink"].endswith(".webp") and Path(reports["s_S30_side_on"]["images"]["sheet"]).is_file(), "정지 실행: 유효·깜박임 그림·네 장 그림")
        walk_flat = reports["w_W01_back_on"]["numbers"]
        walk_up = reports["w_W02_back_on"]["numbers"]
        check(abs(walk_flat["Wk_representative"] - 20.0) < 1.0 and abs(walk_up["Wk_representative"] - 40.0) < 2.0, "걷기 자국 대비 Wk(G6.1)가 합성 20·40 에 맞음", f"{walk_flat['Wk_representative']:.2f} / {walk_up['Wk_representative']:.2f}")
        check(abs(walk_flat["noise_W"] - 2.0) < 0.2, "기준 두 장 잡음 N_W ≈ 2", format_number(walk_flat["noise_W"], 2))
        check(Path(reports["w_W01_back_on"]["images"]["strip"]).is_file() and reports["w_W01_back_on"]["images"]["walk"].endswith(".webp"), "걷기 실행: 띠 그림·움직이는 그림")
        check(reports["r_R01_side_on"]["numbers"]["information_only"] and len(reports["r_R01_side_on"]["numbers"]["frame_to_frame_mean_abs_diff_255"]) == gv.RUN_SHOT_COUNT - 1, "달리기: 앞뒤 장 차 70개(정보)")
        summary = read_json(root / REPORT_DIRECTORY_NAME / "summary.json")
        check("w_W02_back_on" in summary["pairs"] and abs(summary["pairs"]["w_W02_back_on"]["on_over_off"] - 2.0) < 0.2, "켬 대 끔 짝: Wk 비 ≈ 2", str(summary["pairs"].get("w_W02_back_on", {}).get("on_over_off")))
        check("s_S30_side_on" in summary["pairs"] and summary["pairs"]["s_S30_side_on"]["on_vs_off_difference_255"] > 0.0 and Path(summary["pairs"]["s_S30_side_on"]["images"]["sheet"]).is_file(), "켬 대 끔 짝: 정지 차이 그림")
        check(reports["w_W03_back_on"]["usable"] is None and reports["w_W03_back_on"]["valid"], "로그 보관본이 없으면 usable 이 None(촬영 유효는 유지)")
        check(reports["w_W04_back_on"]["log"]["ensure"]["new"] == 1 and reports["w_W04_back_on"]["usable"] is False, "새 ensure 가 있으면 사용 불가")
        check(reports["w_W05_back_on"]["log"]["ensure"]["known_unrelated"] == 1 and reports["w_W05_back_on"]["usable"] is True, "알려진 ensure(NavigationSystem.h)는 갈라 적고 사용 가능")
        check(not reports["w_W06_back_on"]["valid"] and reports["w_W06_back_on"]["numbers"] is None, "장 파일이 빠지면 무효·숫자 없음")
        check(abs(walk_up["visible_V"] - 20.0) < 1.5 and abs(walk_up["Wk_over_V"] - 2.0) < 0.3, "안개 보임 V(기준 장 − 구역 숨김 장)와 Wk÷V", f"{walk_up['visible_V']:.2f} / {walk_up['Wk_over_V']:.2f}")
        check("Wk_moving_information_only" in walk_up and "G15.0" not in walk_up["Wk_moving_information_only"] and "G15.0" not in walk_up["Wk"], "구역 숨김 장은 자국 대비 Wk 에 섞이지 않음")
        check(reports["w_W07_back_on"]["valid"] is False and any("0" in reason and "품질" in reason for reason in reports["w_W07_back_on"]["invalid_reasons"]), "품질 변수 값이 −1 이 아니면 무효")
        check(reports["w_W10_back_on"]["valid"] is False and any("품질" in reason for reason in reports["w_W10_back_on"]["invalid_reasons"]), "품질 변수 값 줄이 하나뿐이면 무효")
        check(reports["w_W01_back_on"]["quality_cvar_log_values"] == [-1, -1, -1] and reports["w_W03_back_on"]["quality_cvar_log_values"] is None, "품질 값 기록, 로그 보관본이 없으면 None")
        check(reports["w_W02_back_on"]["requested_distance_cm"] == 600.0 and reports["r_R01_side_on"]["requested_distance_cm"] == 3000.0, "요청 거리가 실행 기록에서 옴(달리기 3000)")
        markdown_early = (root / REPORT_DIRECTORY_NAME / "summary.md").read_text(encoding="utf-8")
        check("| r_R01_side_on | 예 | -1 -1 -1 | 3001 | 3000 | 100.0% |" in markdown_early, "걸은 거리 백분율의 분모가 요청 거리(달리기 3000)")
        check("같은 카메라 분모 비" in markdown_early and "| s_F_side_off | F | side | 끔 |" in markdown_early and "Wk÷V 평지(W01) 대비" in markdown_early, "정지 같은 카메라 분모 열·걷기 V 열")
        check(exit_code == EXIT_SOME_INVALID, "무효 실행이 있으면 종료 코드 1")
        markdown = (root / REPORT_DIRECTORY_NAME / "summary.md").read_text(encoding="utf-8")
        check("## 정지" in markdown and "## 걷기" in markdown and "## 창 이동" in markdown and "평지(W01) 대비" in markdown, "표 마크다운 구성")
        check("문턱" in markdown.splitlines()[0] and "합격" not in markdown, "표에는 문턱·합격 판정이 없다")
    print("자체 점검", "통과" if not failures else "실패 " + str(len(failures)) + "건")
    return 0 if not failures else EXIT_SOME_INVALID


def parse_arguments(argument_list):
    parser = argparse.ArgumentParser(description="3D 안개 2a-3 화면 확인 숫자·그림·표")
    parser.add_argument("--runs", nargs="+", help="실행 이름들")
    parser.add_argument("--batch", nargs="+", help="색인의 묶음 이름들")
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--out-root", default=str(GROUND_ROOT))
    parser.add_argument("--no-images", action="store_true")
    parser.add_argument("--snapshot-log", metavar="NAME", help="촬영 직후 이 실행의 PIE 로그 구간을 보관")
    parser.add_argument("--log", default=str(vc.DEFAULT_LOG_PATH))
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args(argument_list)


def main(argument_list=None):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    options = parse_arguments(argument_list)
    if options.self_test:
        return run_self_test()
    try:
        if options.snapshot_log:
            return snapshot_log(options)
        return run_reports(options)
    except (InputProblem, rep.InputProblem, vc.InputProblem, OSError, KeyError) as error:
        print("입력 문제:", error)
        return EXIT_INPUT_PROBLEM


if __name__ == "__main__":
    sys.exit(main())
