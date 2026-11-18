"""실행 환경: 시스템 Python(numpy·Pillow 필요, 언리얼 에디터 불필요). 3D 안개 1c 일괄 보정(prereg-1c.md 3절)의 실행 결과 폴더에서 숫자(걷기·휘두르기 자국 대비, 되풀이 잡음, 다시 차오름 비, 평소 무늬)·유효 조건·로그 요약·그림 띠·견줌 그림·움직이는 그림·표를 만든다. 밝기 함수와 로그 읽기는 fog3d_visible_check.py 를 가져다 쓴다.
실행 명령: python -B Tools/FluidFX/fog3d_calib_report.py --runs <실행 이름...> | --all [--baseline base_a] [--animate] [--no-images] [--compare-rows 8] [--group-compare], 실행 직후 로그 보관: --snapshot-log <실행 이름> [--log Saved/Logs/TDGame.log], 2회차 표 셋(탑다운 개념 증명 Fv·Rs·Rn·Rw, 걷기 치우침 Wk_L·Wk_R, 구름·이어짐): --round2-tables <폴더>, 점검만: --self-test (PowerShell, 한글 출력은 PYTHONUTF8=1)
출력: Saved/FluidFX/fog3d_calib/<실행 이름>/report/{report.json, strip.png, swing.webp, walk.webp}, 같은 폴더 위 summary.json·summary.md·compare_NN.png(--group-compare 면 묶음별 groupcmp_<묶음>_NN.png, 행 = 기본값 + 그 묶음 실행), 로그 보관 시 <실행 이름>/log_slice.txt, --round2-tables 는 <폴더>/poc_top·asym·cloud_trail 의 .md·.json. 종료 코드 0 모두 유효 / 1 무효 실행이나 오류가 있음 / 2 입력 문제. 숫자는 후보를 거르는 데만 쓰며 통과·실패 판정이 아니다.
상태: 에디터 없이 합성 그림 --self-test 만 확인(영역 경계·식·잡음·표·그림). 실제 PIE 촬영 그림과 실제 로그에는 아직 돌리지 않았다.
"""
import argparse
import json
import math
import re
import sys
import tempfile
import time
from pathlib import Path

import numpy
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))
import fog3d_calib_variants as variants_module  # noqa: E402
import fog3d_visible_check as visible_check  # noqa: E402

CALIBRATION_ROOT = variants_module.CALIBRATION_ROOT
PREREGISTRATION_PATH = variants_module.PREREGISTRATION_PATH
CAPTURE_SUBDIRECTORY = variants_module.CAPTURE_SUBDIRECTORY
WALK_REGION_ROW_FRACTIONS = (0.72, 0.98)
WALK_REGION_COLUMN_FRACTIONS = (0.35, 0.65)
SWING_REGION_ROW_FRACTIONS = (0.60, 0.95)
SWING_REGION_COLUMN_FRACTIONS = (0.25, 0.75)
SWING_EXCLUDED_COLUMN_FRACTIONS = (0.42, 0.58)
SWING2_REGION_ROW_FRACTIONS = (0.50, 0.76)
SWING2_REGION_COLUMN_FRACTIONS = (0.18, 0.54)
TEXTURE2_REGION_ROW_FRACTIONS = (0.55, 0.95)
TEXTURE2_REGION_COLUMN_FRACTIONS = (0.02, 0.30)
GROUP_FILE_SLUGS = {"몸 깎기": "body_erosion", "몸 크기": "body_size", "얼룩": "stain", "잔무늬": "fine", "안개 높이": "height", "안개 짙기": "density", "다시 차오름": "refill",
                    "밀린 것 되돌림": "displacement", "바람": "wind", "칼": "blade", "바람 없는 판": "mode1", "엔진 안개": "engine"}
GROUP_COMPARE_ROWS = 5
TEXTURE_BLUR_SIGMA_PIXELS = 20.0
BLUR_TRUNCATE_SIGMAS = 4.0
LUMA_TO_BYTE_SCALE = 255.0
WALK_REPRESENTATIVE_ID = "W12.1"
SWING_REPRESENTATIVE_ID = "S2.5"
WALK_STOPPED_IDS = ("W12.1", "W13.1", "W14.1", "W15.6", "W17.6")
SWING_STOPPED_IDS = ("S2.5", "S3.5", "S5.0", "S7.0")
WALK_REFERENCE_IDS = ("W19.4", "W19.9")
SWING_REFERENCE_IDS = ("S8.3", "S8.8")
WALK_REFILL_ID = "W17.6"
SWING_REFILL_ID = "S7.0"
SWING_GAP_RANGE_SECONDS = variants_module.SWING_GAP_RANGE_SECONDS
WALK_END_LATEST_SECONDS = variants_module.WALK_END_LATEST_SECONDS
EVENT_LATE_NOTE_SECONDS = 0.1
STRIP_TILE_WIDTH = 200
STRIP_COLUMNS = 10
STRIP_WIDTH_LIMIT = 2000
COMPARE_TILE_WIDTH = 400
COMPARE_WIDTH_LIMIT = 1600
COMPARE_HEADER_HEIGHT = 18
COMPARE_DIFFERENCE_GAIN = 8.0
ANIMATION_WIDTH = 640
ANIMATION_FRAME_MILLISECONDS = 500
LOG_SNAPSHOT_FILE_NAME = "log_slice.txt"
LOG_SNAPSHOT_WAIT_SECONDS = 15.0
REPORT_DIRECTORY_NAME = "report"
EXIT_ALL_VALID = 0
EXIT_SOME_INVALID = 1
EXIT_INPUT_PROBLEM = 2
FONT_CANDIDATES = ("arial.ttf", "DejaVuSans.ttf", "malgun.ttf")


class InputProblem(Exception):
    pass


def load_font(size):
    for candidate in FONT_CANDIDATES:
        try:
            return ImageFont.truetype(candidate, size)
        except OSError:
            continue
    return ImageFont.load_default()


def walk_region(height, width):
    index = visible_check.fraction_to_index
    return (index(WALK_REGION_ROW_FRACTIONS[0], height), index(WALK_REGION_ROW_FRACTIONS[1], height),
            index(WALK_REGION_COLUMN_FRACTIONS[0], width), index(WALK_REGION_COLUMN_FRACTIONS[1], width))


def swing_region(height, width):
    index = visible_check.fraction_to_index
    return (index(SWING_REGION_ROW_FRACTIONS[0], height), index(SWING_REGION_ROW_FRACTIONS[1], height),
            index(SWING_REGION_COLUMN_FRACTIONS[0], width), index(SWING_REGION_COLUMN_FRACTIONS[1], width))


def swing_excluded_columns(width):
    index = visible_check.fraction_to_index
    return (index(SWING_EXCLUDED_COLUMN_FRACTIONS[0], width), index(SWING_EXCLUDED_COLUMN_FRACTIONS[1], width))


def walk_mask(height, width):
    top, bottom, left, right = walk_region(height, width)
    mask = numpy.zeros((height, width), dtype=bool)
    mask[top:bottom, left:right] = True
    return mask


def swing_mask(height, width):
    top, bottom, left, right = swing_region(height, width)
    excluded_left, excluded_right = swing_excluded_columns(width)
    mask = numpy.zeros((height, width), dtype=bool)
    mask[top:bottom, left:right] = True
    mask[top:bottom, excluded_left:excluded_right] = False
    return mask


def box_region(height, width, row_fractions, column_fractions):
    index = visible_check.fraction_to_index
    return (index(row_fractions[0], height), index(row_fractions[1], height), index(column_fractions[0], width), index(column_fractions[1], width))


def swing2_region(height, width):
    return box_region(height, width, SWING2_REGION_ROW_FRACTIONS, SWING2_REGION_COLUMN_FRACTIONS)


def texture2_region(height, width):
    return box_region(height, width, TEXTURE2_REGION_ROW_FRACTIONS, TEXTURE2_REGION_COLUMN_FRACTIONS)


def box_mask(height, width, region):
    top, bottom, left, right = region
    mask = numpy.zeros((height, width), dtype=bool)
    mask[top:bottom, left:right] = True
    return mask


def mean_absolute_difference(first, second, mask):
    return float(numpy.abs(first - second)[mask].mean()) * LUMA_TO_BYTE_SCALE


def gaussian_kernel(sigma):
    radius = int(BLUR_TRUNCATE_SIGMAS * sigma + 0.5)
    offsets = numpy.arange(-radius, radius + 1, dtype=numpy.float64)
    weights = numpy.exp(-0.5 * (offsets / sigma) ** 2)
    return weights / weights.sum()


def blur_along_axis(array, weights, axis):
    radius = (len(weights) - 1) // 2
    pad_width = [(0, 0), (0, 0)]
    pad_width[axis] = (radius, radius)
    padded = numpy.pad(array, pad_width, mode="symmetric")
    size = array.shape[axis]
    result = numpy.zeros_like(array)
    for position, weight in enumerate(weights):
        window = padded[position:position + size, :] if axis == 0 else padded[:, position:position + size]
        result += weight * window
    return result


def gaussian_blur(array, sigma):
    weights = gaussian_kernel(sigma)
    return blur_along_axis(blur_along_axis(array, weights, 0), weights, 1)


def texture_rms(luma, mask):
    residual = luma - gaussian_blur(luma, TEXTURE_BLUR_SIGMA_PIXELS)
    return float(math.sqrt(float((residual[mask] ** 2).mean()))) * LUMA_TO_BYTE_SCALE


def read_json(path):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise InputProblem(f"JSON 을 읽을 수 없다: {path}: {error}")


def default_run_record(walk_shift):
    return {"shots": variants_module.build_shot_table(walk_shift), "walk_shift_seconds": walk_shift,
            "valid_conditions": {"swing_to_S2.5_seconds": list(SWING_GAP_RANGE_SECONDS), "walk_end_before_seconds": round(WALK_END_LATEST_SECONDS + walk_shift, 3)}}


def find_schedule_file(capture_directory):
    candidates = sorted(Path(capture_directory).glob("*_schedule.json"))
    if len(candidates) != 1:
        raise InputProblem(f"{capture_directory} 에 *_schedule.json 이 정확히 하나여야 한다(지금 {len(candidates)}개)")
    return candidates[0]


def find_walk_status(capture_directory, schedule_status, result_path):
    candidates = sorted(path for path in Path(capture_directory).glob("*_walk.json") if "event" in path.name)
    if candidates:
        return read_json(candidates[-1])
    for event in schedule_status.get("events_done", []):
        if isinstance(event.get("move"), dict) and "start_game_time" in event["move"]:
            return event["move"]
    if Path(result_path).is_file():
        result = read_json(result_path)
        for entry in result.get("variants", []):
            for event in (entry.get("schedule") or {}).get("events_done", []):
                if isinstance(event.get("move"), dict) and "start_game_time" in event["move"]:
                    return event["move"]
    return None


def resolve_shot_path(shot, capture_directory):
    path = Path(shot["path"])
    return path if path.is_file() else Path(capture_directory) / path.name


def match_shots(run_record, schedule_status, capture_directory):
    matched = {}
    for entry in run_record["shots"]:
        candidates = [shot for shot in schedule_status.get("shots", []) if abs(float(shot["time"]) - entry["time"]) < 1e-6]
        if not candidates:
            continue
        shot = candidates[0]
        path = resolve_shot_path(shot, capture_directory)
        matched[entry["id"]] = {"entry": entry, "shot": shot, "path": path, "exists": path.is_file()}
    return matched


def evaluate_validity(run_record, schedule_status, walk_status, matched):
    reasons = []
    missing = [entry["id"] for entry in run_record["shots"] if entry["id"] not in matched or not matched[entry["id"]]["exists"]]
    if missing:
        reasons.append("장 파일 없음: " + ", ".join(missing))
    conditions = run_record["valid_conditions"]
    swing_event = next((event for event in schedule_status.get("events_done", []) if abs(float(event["time"]) - variants_module.SWING_START_TIME) < 1e-9), None)
    details = {"swing_to_S2.5_seconds": None, "walk_end_game_seconds": None, "walk_end_limit_seconds": conditions["walk_end_before_seconds"],
               "swing_gap_range_seconds": conditions["swing_to_S2.5_seconds"]}
    reference_shot = matched.get(SWING_REPRESENTATIVE_ID)
    if swing_event is None:
        reasons.append("휘두르기 이벤트 기록 없음")
    elif reference_shot is None or reference_shot["shot"].get("captured_game_time") is None:
        reasons.append("S 2.5초 장의 찍힌 게임 시각 없음")
    else:
        gap = float(reference_shot["shot"]["captured_game_time"]) - float(swing_event["game_time"])
        details["swing_to_S2.5_seconds"] = gap
        low, high = conditions["swing_to_S2.5_seconds"]
        if not low <= gap <= high:
            reasons.append(f"휘두르기→S 2.5초 간격 {gap:.3f}초가 {low}~{high} 밖")
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
            if end_time >= conditions["walk_end_before_seconds"]:
                reasons.append(f"걷기가 {end_time:.3f}초에 끝나 {conditions['walk_end_before_seconds']}초 전이라는 조건 위반")
    late_shots = [float(entry["shot"].get("late_seconds") or 0.0) for entry in matched.values()]
    capture_gaps = [float(entry["shot"]["captured_game_time"]) - float(entry["shot"]["time"]) for entry in matched.values() if entry["shot"].get("captured_game_time") is not None]
    event_lateness = [float(event["game_time"]) - float(event["time"]) for event in schedule_status.get("events_done", [])]
    details["shot_issue_late_max_seconds"] = max(late_shots) if late_shots else None
    details["shot_capture_delay_max_seconds"] = max(capture_gaps) if capture_gaps else None
    details["event_late_max_seconds"] = max(event_lateness) if event_lateness else None
    details["events_done"] = len(schedule_status.get("events_done", []))
    details["events_expected"] = len(run_record.get("events", [])) or None
    notes = []
    if details["event_late_max_seconds"] is not None and details["event_late_max_seconds"] > EVENT_LATE_NOTE_SECONDS:
        notes.append(f"이벤트가 최대 {details['event_late_max_seconds']:.3f}초 늦었다")
    if details["events_expected"] is not None and details["events_done"] != details["events_expected"]:
        reasons.append(f"이벤트 {details['events_done']}/{details['events_expected']}개만 실행됨")
    return {"reasons": reasons, "details": details, "notes": notes}


def compute_numbers(lumas):
    height, width = next(iter(lumas.values())).shape
    walk = walk_mask(height, width)
    swing = swing_mask(height, width)
    swing2 = box_mask(height, width, swing2_region(height, width))
    texture2 = box_mask(height, width, texture2_region(height, width))
    walk_reference = lumas[WALK_REFERENCE_IDS[0]]
    swing_reference = lumas[SWING_REFERENCE_IDS[0]]
    walk_contrast = {identifier: mean_absolute_difference(lumas[identifier], walk_reference, walk) for identifier in lumas if identifier.startswith("W") and identifier not in WALK_REFERENCE_IDS}
    swing_contrast = {identifier: mean_absolute_difference(lumas[identifier], swing_reference, swing) for identifier in lumas if identifier.startswith("S") and identifier not in SWING_REFERENCE_IDS}
    swing2_contrast = {identifier: mean_absolute_difference(lumas[identifier], swing_reference, swing2) for identifier in lumas if identifier.startswith("S") and identifier not in SWING_REFERENCE_IDS}
    noise_walk = mean_absolute_difference(lumas[WALK_REFERENCE_IDS[0]], lumas[WALK_REFERENCE_IDS[1]], walk)
    noise_swing = mean_absolute_difference(lumas[SWING_REFERENCE_IDS[0]], lumas[SWING_REFERENCE_IDS[1]], swing)

    def ratio(numerator, denominator):
        return numerator / denominator if denominator > 0.0 else None

    walk_representative = walk_contrast[WALK_REPRESENTATIVE_ID]
    swing_representative = swing_contrast[SWING_REPRESENTATIVE_ID]
    return {
        "image_size": [width, height],
        "regions": {"RW_rows_cols": list(walk_region(height, width)), "RS_rows_cols": list(swing_region(height, width)), "RS_excluded_columns": list(swing_excluded_columns(width)),
                    "RS2_rows_cols": list(swing2_region(height, width)), "RT_rows_cols": list(texture2_region(height, width))},
        "Wk": {identifier: walk_contrast[identifier] for identifier in WALK_STOPPED_IDS},
        "Sk": {identifier: swing_contrast[identifier] for identifier in SWING_STOPPED_IDS},
        "Wk_moving_information_only": {identifier: value for identifier, value in walk_contrast.items() if identifier not in WALK_STOPPED_IDS},
        "Sk_moving_information_only": {identifier: value for identifier, value in swing_contrast.items() if identifier not in SWING_STOPPED_IDS},
        "noise_walk_N_W": noise_walk, "noise_swing_N_S": noise_swing,
        "refill_ratio_swing": ratio(swing_contrast[SWING_REFILL_ID], swing_representative),
        "refill_ratio_walk": ratio(walk_contrast[WALK_REFILL_ID], walk_representative),
        "Tx": texture_rms(swing_reference, swing),
        "Sk2": {identifier: swing2_contrast[identifier] for identifier in SWING_STOPPED_IDS},
        "Sk2_moving_information_only": {identifier: value for identifier, value in swing2_contrast.items() if identifier not in SWING_STOPPED_IDS},
        "noise_swing2_N_S2": mean_absolute_difference(lumas[SWING_REFERENCE_IDS[0]], lumas[SWING_REFERENCE_IDS[1]], swing2),
        "Tx2": texture_rms(swing_reference, texture2),
        "Wk_over_noise": ratio(walk_representative, noise_walk), "Sk_over_noise": ratio(swing_representative, noise_swing),
        "Wk_12.1_against_second_reference": mean_absolute_difference(lumas[WALK_REPRESENTATIVE_ID], lumas[WALK_REFERENCE_IDS[1]], walk),
        "Sk_2.5_against_second_reference": mean_absolute_difference(lumas[SWING_REPRESENTATIVE_ID], lumas[SWING_REFERENCE_IDS[1]], swing),
    }


def summarize_log(lines, source):
    result = visible_check.check_log_lines(lines)
    usable = result["fog3d_warning_or_error_lines"] == 0 and result["rejected"] == 0 and bool(result["submitted"])
    return {"source": source, "fog3d_warning_or_error_lines": result["fog3d_warning_or_error_lines"], "submitted": result["submitted"], "rejected": result["rejected"],
            "ensure_lines": result["ensure_lines"], "rdg_error_lines": result["rdg_error_lines"], "summary_line_count": result["summary_line_count"],
            "examples": result["examples"], "usable": usable}


def read_run_log(run_directory, use_live_log, live_log_path):
    snapshot = Path(run_directory) / LOG_SNAPSHOT_FILE_NAME
    if snapshot.is_file():
        return summarize_log(snapshot.read_text(encoding="utf-8", errors="replace").splitlines(), "snapshot")
    if use_live_log:
        lines = visible_check.read_log_lines(live_log_path)
        start = visible_check.find_log_start(lines, None)
        return summarize_log(lines[start:], "live_last_pie_unverified")
    return {"source": "none", "usable": None}


def snapshot_run_log(run_directory, log_path):
    deadline = time.monotonic() + LOG_SNAPSHOT_WAIT_SECONDS
    while True:
        lines = visible_check.read_log_lines(log_path)
        section = lines[visible_check.find_log_start(lines, None):]
        has_summary = any(visible_check.SUMMARY_PATTERN.search(line) for line in section)
        if has_summary or time.monotonic() >= deadline:
            break
        time.sleep(1.0)
    (Path(run_directory) / LOG_SNAPSHOT_FILE_NAME).write_text("\n".join(section) + "\n", encoding="utf-8", newline="\n")
    return len(section), has_summary


def load_all_lumas(matched):
    return {identifier: visible_check.load_luma(entry["path"]) for identifier, entry in matched.items()}


def font_for(size):
    return load_font(size)


def label_tile(image, text, font, fill=(255, 255, 255)):
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 0, image.width, font.size + 4 if hasattr(font, "size") else 14), fill=(0, 0, 0))
    draw.text((3, 1), text, fill=fill, font=font)
    return image


def resize_to_width(image, width):
    height = max(1, int(round(image.height * width / image.width)))
    return image.resize((width, height), Image.LANCZOS)


def build_strip(matched, run_record, output_path):
    font = font_for(11)
    tile_height = None
    swing_ids = [entry["id"] for entry in run_record["shots"] if entry["group"] == "S"]
    walk_ids = [entry["id"] for entry in run_record["shots"] if entry["group"] == "W"]
    rows = [swing_ids, walk_ids]
    tiles_by_row = []
    for identifiers in rows:
        row_tiles = []
        for identifier in identifiers:
            entry = matched.get(identifier)
            if entry is None or not entry["exists"]:
                row_tiles.append((identifier, None))
                continue
            with Image.open(entry["path"]) as source:
                tile = resize_to_width(source.convert("RGB"), STRIP_TILE_WIDTH)
            tile_height = tile_height or tile.height
            row_tiles.append((f"{identifier} @{entry['entry']['time']:g}s", tile))
        tiles_by_row.append(row_tiles)
    tile_height = tile_height or int(STRIP_TILE_WIDTH * 9 / 16)
    strip = Image.new("RGB", (STRIP_TILE_WIDTH * STRIP_COLUMNS, tile_height * 2), (20, 20, 20))
    for row_index, row_tiles in enumerate(tiles_by_row):
        for column_index, (label, tile) in enumerate(row_tiles):
            if tile is None:
                tile = Image.new("RGB", (STRIP_TILE_WIDTH, tile_height), (60, 0, 0))
                label = label + " missing"
            strip.paste(label_tile(tile.copy(), label, font), (column_index * STRIP_TILE_WIDTH, row_index * tile_height))
    strip.save(output_path)
    return strip.size


def build_animation(matched, run_record, group, output_stem):
    font = font_for(16)
    frames = []
    for entry in run_record["shots"]:
        if entry["group"] != group or entry["id"] not in matched or not matched[entry["id"]]["exists"]:
            continue
        with Image.open(matched[entry["id"]]["path"]) as source:
            frame = resize_to_width(source.convert("RGB"), ANIMATION_WIDTH)
        frames.append(label_tile(frame, f"{entry['id']}  {entry['role']}", font))
    if not frames:
        return None
    webp_path = Path(str(output_stem) + ".webp")
    try:
        frames[0].save(webp_path, save_all=True, append_images=frames[1:], duration=ANIMATION_FRAME_MILLISECONDS, loop=0, format="WEBP", quality=85)
        return webp_path
    except (OSError, ValueError, KeyError):
        webp_path.unlink(missing_ok=True)
    gif_path = Path(str(output_stem) + ".gif")
    frames[0].save(gif_path, save_all=True, append_images=frames[1:], duration=ANIMATION_FRAME_MILLISECONDS, loop=0, format="GIF")
    return gif_path


def format_number(value, digits=2):
    return "-" if value is None else f"{value:.{digits}f}"


def analyze_run(run_directory, options):
    run_directory = Path(run_directory)
    name = run_directory.name
    capture_directory = run_directory / options.capture_subdirectory
    run_file = run_directory / "run.json"
    run_record = read_json(run_file) if run_file.is_file() else default_run_record(options.walk_shift)
    schedule_file = find_schedule_file(capture_directory)
    schedule_status = read_json(schedule_file)
    walk_status = find_walk_status(capture_directory, schedule_status, capture_directory / "result.json")
    matched = match_shots(run_record, schedule_status, capture_directory)
    validity = evaluate_validity(run_record, schedule_status, walk_status, matched)
    report = {"name": name, "description": run_record.get("description", ""), "group": run_record.get("group", ""), "camera": run_record.get("camera"), "light": run_record.get("light"),
              "changed_fields": run_record.get("changed_fields", {}), "console_extra": run_record.get("console_extra", []), "on_mode": run_record.get("on_mode"),
              "valid": not validity["reasons"], "invalid_reasons": validity["reasons"], "validity": validity["details"], "notes": validity["notes"]}
    report["log"] = read_run_log(run_directory, options.live_log, options.log)
    log_usable = report["log"]["usable"]
    report["usable"] = bool(report["valid"] and log_usable) if log_usable is not None else None
    if log_usable is False:
        report["invalid_reasons"] = report["invalid_reasons"] + ["로그: Fog3D 경고·오류 또는 거부가 있거나 요약 줄이 없다"]
    report_directory = run_directory / REPORT_DIRECTORY_NAME
    report_directory.mkdir(parents=True, exist_ok=True)
    if any(identifier not in matched or not matched[identifier]["exists"] for identifier in [entry["id"] for entry in run_record["shots"]]):
        report["numbers"] = None
        (report_directory / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
        return report, matched, run_record, None
    lumas = load_all_lumas(matched)
    report["numbers"] = compute_numbers(lumas)
    if not options.no_images:
        report["images"] = {"strip": (report_directory / "strip.png").as_posix(), "strip_size": list(build_strip(matched, run_record, report_directory / "strip.png"))}
        if options.animate:
            report["images"]["swing_animation"] = str(build_animation(matched, run_record, "S", report_directory / "swing"))
            report["images"]["walk_animation"] = str(build_animation(matched, run_record, "W", report_directory / "walk"))
    (report_directory / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    return report, matched, run_record, lumas


def representative_values(numbers):
    return {"Wk_12.1": numbers["Wk"][WALK_REPRESENTATIVE_ID], "Sk_2.5": numbers["Sk"][SWING_REPRESENTATIVE_ID], "Sk2_2.5": numbers["Sk2"][SWING_REPRESENTATIVE_ID],
            "Tx": numbers["Tx"], "Tx2": numbers["Tx2"]}


def multiples_against(reports, baseline_name):
    baseline = reports.get(baseline_name)
    if baseline is None or not baseline.get("numbers"):
        return {}
    base_numbers = baseline["numbers"]
    base_values = representative_values(base_numbers)
    multiples = {}
    for name, report in reports.items():
        if not report.get("numbers"):
            continue
        numbers = report["numbers"]
        values = representative_values(numbers)
        multiples[name] = {key: (values[key] / base_values[key] if base_values[key] > 0.0 else None) for key in values}
    return multiples


def build_summary(reports, baseline_name):
    multiples = multiples_against(reports, baseline_name)
    rows = {}
    for name, report in reports.items():
        numbers = report.get("numbers")
        row = {"camera": report.get("camera"), "valid": report["valid"], "usable": report.get("usable"), "invalid_reasons": report["invalid_reasons"], "group": report.get("group"), "description": report.get("description"),
               "log": {key: report["log"].get(key) for key in ("source", "fog3d_warning_or_error_lines", "submitted", "rejected", "usable")}}
        if numbers:
            row.update({"Wk_12.1": numbers["Wk"][WALK_REPRESENTATIVE_ID], "Sk_2.5": numbers["Sk"][SWING_REPRESENTATIVE_ID], "Tx": numbers["Tx"], "Wk": numbers["Wk"], "Sk": numbers["Sk"],
                        "Sk2_2.5": numbers["Sk2"][SWING_REPRESENTATIVE_ID], "Tx2": numbers["Tx2"], "Sk2": numbers["Sk2"],
                        "N_W": numbers["noise_walk_N_W"], "N_S": numbers["noise_swing_N_S"], "Wk_over_N_W": numbers["Wk_over_noise"], "Sk_over_N_S": numbers["Sk_over_noise"],
                        "refill_ratio_swing": numbers["refill_ratio_swing"], "refill_ratio_walk": numbers["refill_ratio_walk"], "multiples_against_baseline": multiples.get(name, {})})
        rows[name] = row
    summary = {"baseline": baseline_name, "run_count": len(rows), "runs": rows}
    first, second = reports.get("base_a"), reports.get("base_b")
    if first and second and first.get("numbers") and second.get("numbers"):
        pairs = {"Wk_12.1": (first["numbers"]["Wk"][WALK_REPRESENTATIVE_ID], second["numbers"]["Wk"][WALK_REPRESENTATIVE_ID]),
                 "Sk_2.5": (first["numbers"]["Sk"][SWING_REPRESENTATIVE_ID], second["numbers"]["Sk"][SWING_REPRESENTATIVE_ID]),
                 "Sk2_2.5": (first["numbers"]["Sk2"][SWING_REPRESENTATIVE_ID], second["numbers"]["Sk2"][SWING_REPRESENTATIVE_ID]),
                 "Tx": (first["numbers"]["Tx"], second["numbers"]["Tx"]),
                 "Tx2": (first["numbers"]["Tx2"], second["numbers"]["Tx2"])}
        summary["baseline_reproducibility"] = {key: (later / earlier if earlier > 0.0 else None) for key, (earlier, later) in pairs.items()}
    return summary


def build_markdown(summary):
    header = ["실행", "묶음", "유효", "Wk(12.1)", "×기준", "Wk/N_W", "Sk(2.5)", "×기준", "Sk/N_S", "Sk2(2.5)", "×기준", "Tx", "×기준", "Tx2", "×기준", "되차오름 S", "되차오름 W", "N_W", "N_S", "로그 경고·오류/거부"]
    lines = [f"# 1c 일괄 보정 숫자 요약 (기준 {summary['baseline']})", "",
             "숫자는 후보를 거르는 데만 쓴다(통과·실패 판정이 아님). Wk·Sk·N·Tx 는 8비트 밝기 단위(0~255), ×기준은 기본값 실행 대비 배수. Sk2·Tx2 는 덧붙임 3절의 정보용 숫자(영역 RS2·RT).", "",
             "| " + " | ".join(header) + " |", "|" + "|".join(["---"] * len(header)) + "|"]
    for name, row in summary["runs"].items():
        validity = "예" if row["valid"] and row.get("usable") is not False else "아니오" if not row["valid"] or row.get("usable") is False else "?"
        if row["valid"] and row.get("usable") is None:
            validity = "예(로그 미확인)"
        multiples = row.get("multiples_against_baseline", {})
        log = row["log"]
        log_text = "-" if log.get("source") == "none" else f"{log.get('fog3d_warning_or_error_lines')}/{log.get('rejected')}"
        if "Wk_12.1" not in row or row.get("camera") not in (None, "back"):
            lines.append(f"| {name} | {row.get('group') or ''} | {validity} |" + " - |" * (len(header) - 3))
            continue
        cells = [name, row.get("group") or "", validity, format_number(row["Wk_12.1"]), format_number(multiples.get("Wk_12.1")), format_number(row["Wk_over_N_W"], 1),
                 format_number(row["Sk_2.5"]), format_number(multiples.get("Sk_2.5")), format_number(row["Sk_over_N_S"], 1), format_number(row["Sk2_2.5"]), format_number(multiples.get("Sk2_2.5")),
                 format_number(row["Tx"]), format_number(multiples.get("Tx")), format_number(row["Tx2"]), format_number(multiples.get("Tx2")),
                 format_number(row["refill_ratio_swing"]), format_number(row["refill_ratio_walk"]), format_number(row["N_W"]), format_number(row["N_S"]), log_text]
        lines.append("| " + " | ".join(cells) + " |")
    visual_only = [name for name, row in summary["runs"].items() if row.get("camera") not in (None, "back")]
    if visual_only:
        lines += ["", "숫자는 뒤 카메라에서만 계산한다(등록본 3절). 눈으로만 보는 실행: " + ", ".join(visual_only)]
    invalid = [(name, row["invalid_reasons"]) for name, row in summary["runs"].items() if row["invalid_reasons"]]
    if invalid:
        lines += ["", "## 무효·주의 실행", ""] + [f"- {name}: {'; '.join(reasons)}" for name, reasons in invalid]
    if "baseline_reproducibility" in summary:
        lines += ["", "## 기본값 실행 재현성(base_b ÷ base_a)", "", "- " + ", ".join(f"{key} {format_number(value)}" for key, value in summary["baseline_reproducibility"].items())]
    return "\n".join(lines) + "\n"


def compare_label(entry):
    numbers = entry["numbers"]
    multiples = entry["multiples"]
    suffix = "" if entry["valid"] else "  INVALID"
    if entry.get("camera") not in (None, "back"):
        return f"{entry['name']}   camera={entry['camera']}: numbers only for back camera (visual only){suffix}"
    return (f"{entry['name']}   Wk(12.1)={numbers['Wk'][WALK_REPRESENTATIVE_ID]:.2f} (x{format_number(multiples.get('Wk_12.1'))})   "
            f"Sk(2.5)={numbers['Sk'][SWING_REPRESENTATIVE_ID]:.2f} (x{format_number(multiples.get('Sk_2.5'))})   "
            f"Sk2(2.5)={numbers['Sk2'][SWING_REPRESENTATIVE_ID]:.2f} (x{format_number(multiples.get('Sk2_2.5'))})   "
            f"Tx={numbers['Tx']:.2f} (x{format_number(multiples.get('Tx'))})   Tx2={numbers['Tx2']:.2f} (x{format_number(multiples.get('Tx2'))}){suffix}")


def build_compare_rows(page):
    rows = []
    for entry in page:
        lumas, matched = entry["lumas"], entry["matched"]
        difference = numpy.clip(numpy.abs(lumas[WALK_REPRESENTATIVE_ID] - lumas[WALK_REFERENCE_IDS[0]]) * COMPARE_DIFFERENCE_GAIN, 0.0, 1.0)
        difference_image = Image.fromarray((difference * 255.0).astype(numpy.uint8)).convert("RGB")
        tiles = []
        for source in (matched[SWING_REFERENCE_IDS[0]]["path"], matched[SWING_REPRESENTATIVE_ID]["path"], matched[WALK_REPRESENTATIVE_ID]["path"]):
            with Image.open(source) as image:
                tiles.append(resize_to_width(image.convert("RGB"), COMPARE_TILE_WIDTH))
        tiles.append(resize_to_width(difference_image, COMPARE_TILE_WIDTH))
        rows.append((entry, tiles))
    return rows


def render_compare_page(page, path):
    font = font_for(12)
    tile_width = COMPARE_TILE_WIDTH
    rows = build_compare_rows(page)
    tile_height = rows[0][1][0].height
    canvas = Image.new("RGB", (tile_width * 4, (tile_height + COMPARE_HEADER_HEIGHT) * len(rows)), (20, 20, 20))
    draw = ImageDraw.Draw(canvas)
    for row_index, (entry, tiles) in enumerate(rows):
        top = row_index * (tile_height + COMPARE_HEADER_HEIGHT)
        draw.text((4, top + 2), compare_label(entry), fill=(255, 120, 120) if not entry["valid"] else (255, 255, 255), font=font)
        for column_index, tile in enumerate(tiles):
            canvas.paste(tile, (column_index * tile_width, top + COMPARE_HEADER_HEIGHT))
    canvas.save(path)
    return path


def build_compare_images(entries, output_directory, rows_per_image):
    paths = []
    for page_start in range(0, len(entries), rows_per_image):
        path = Path(output_directory) / f"compare_{page_start // rows_per_image + 1:02d}.png"
        paths.append(render_compare_page(entries[page_start:page_start + rows_per_image], path))
    return paths


def group_file_slug(group):
    return GROUP_FILE_SLUGS.get(group, re.sub(r"[^0-9A-Za-z_]+", "_", group).strip("_") or "group")


def build_group_compare_images(entries, output_directory, baseline_name, rows_per_image):
    baseline = next((entry for entry in entries if entry["name"] == baseline_name), None)
    if baseline is None:
        return []
    group_order, grouped = [], {}
    for entry in entries:
        group = entry.get("group") or ""
        if entry["name"] == baseline_name or group == "기본값":
            continue
        if group not in grouped:
            group_order.append(group)
            grouped[group] = []
        grouped[group].append(entry)
    paths = []
    members_per_page = max(1, rows_per_image - 1)
    for group in group_order:
        members = grouped[group]
        page_count = (len(members) + members_per_page - 1) // members_per_page
        for page_index in range(page_count):
            page = [baseline] + members[page_index * members_per_page:(page_index + 1) * members_per_page]
            suffix = f"_{page_index + 1:02d}" if page_count > 1 else ""
            path = Path(output_directory) / f"groupcmp_{group_file_slug(group)}{suffix}.png"
            paths.append(render_compare_page(page, path))
    return paths


def list_run_names(root):
    index_path = Path(root) / "index.json"
    ordered = [entry["name"] for entry in read_json(index_path)["runs"]] if index_path.is_file() else []
    present = {path.parent.parent.name for path in Path(root).glob(f"*/{CAPTURE_SUBDIRECTORY}/result.json")}
    return [name for name in ordered if name in present] + sorted(present - set(ordered))


def run_reports(options):
    root = Path(options.out_root)
    names = list_run_names(root) if options.all else list(options.runs or [])
    if not names:
        raise InputProblem("--runs 또는 --all 로 대상 실행을 줘야 한다(결과가 있는 실행 없음)")
    reports, entries, errors = {}, [], {}
    for name in names:
        try:
            report, matched, run_record, lumas = analyze_run(root / name, options)
        except (InputProblem, OSError, KeyError, ValueError) as error:
            errors[name] = str(error)
            print(f"{name}: 오류 {error}")
            continue
        reports[name] = report
        numbers = report.get("numbers")
        if numbers is None:
            print(f"{name}: 장이 모자라 숫자 계산 불가 ({'; '.join(report['invalid_reasons'])})")
            continue
        print(f"{name}: {'유효' if report['valid'] else '무효'} Wk(12.1)={numbers['Wk'][WALK_REPRESENTATIVE_ID]:.2f} Sk(2.5)={numbers['Sk'][SWING_REPRESENTATIVE_ID]:.2f} Tx={numbers['Tx']:.2f} N_W={numbers['noise_walk_N_W']:.2f} N_S={numbers['noise_swing_N_S']:.2f}"
              + ("" if report["valid"] else " / " + "; ".join(report["invalid_reasons"])))
        entries.append({"name": name, "numbers": numbers, "matched": matched, "lumas": lumas, "valid": report["valid"] and report.get("usable") is not False, "camera": report.get("camera"), "group": report.get("group")})
    summary = build_summary(reports, options.baseline)
    summary["errors"] = errors
    multiples = summary["runs"]
    for entry in entries:
        entry["multiples"] = multiples[entry["name"]].get("multiples_against_baseline", {})
    root.mkdir(parents=True, exist_ok=True)
    (root / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    (root / "summary.md").write_text(build_markdown(summary), encoding="utf-8", newline="\n")
    print("표:", (root / "summary.md").as_posix(), "/", (root / "summary.json").as_posix())
    if not options.no_images and entries:
        compare_paths = build_compare_images(entries, root, options.compare_rows)
        print("견줌 그림:", ", ".join(path.as_posix() for path in compare_paths))
        if options.group_compare:
            group_paths = build_group_compare_images(entries, root, options.baseline, options.group_rows)
            print("묶음별 견줌 그림:", ", ".join(path.as_posix() for path in group_paths))
    has_problem = bool(errors) or any(not report["valid"] or report.get("usable") is False or report.get("numbers") is None for report in reports.values())
    return EXIT_SOME_INVALID if has_problem else EXIT_ALL_VALID


POC_REGION_ROW_FRACTIONS = (0.25, 0.75)
POC_REGION_COLUMN_FRACTIONS = (0.25, 0.75)
POC_TOP_PIXEL_FRACTION = 0.02
POC_FOG_VISIBLE_AT_LEAST = 10.0
POC_RESPONSE_AT_LEAST = 12.0
POC_RESPONSE_OVER_NOISE_AT_LEAST = 3.0
WALK_SPLIT_COLUMN_FRACTION = 0.50
HIDDEN_RUN_NAME = "top_hidden"
POC_TOP_NAMES = ("T0", "T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8", "T7_gridz")
POC_BACK_NAMES = ("T4_back", "T7_back", "base_d")
ASYMMETRY_NAMES = ("A0", "A1", "A2", "A3", "A4")
CLOUD_TRAIL_NAMES = ("s3", "C1", "C2", "C3", "C4", "R1", "R2")
CLOUD_TEXTURE_THRESHOLD = 0.45
CLOUD_SWING_KEEP_RATIO = 0.8
TRAIL_REFILL_AT_LEAST = 0.4
TRAIL_WALK_RANGE = (8.0, 14.0)
ASYMMETRY_HALVED_RATIO = 0.5
ASYMMETRY_NEGLIGIBLE = 0.1
ROUND2_REFERENCE_SWING_IDS = ("S8.3", "S8.8")
ROUND2_REFERENCE_WALK_IDS = ("W19.4", "W19.9")
ROUND2_TABLE_FILES = {"poc": "poc_top", "asym": "asym", "cloud": "cloud_trail"}


def poc_mask(height, width):
    return box_mask(height, width, box_region(height, width, POC_REGION_ROW_FRACTIONS, POC_REGION_COLUMN_FRACTIONS))


def top_fraction_mean(first, second, mask, fraction=POC_TOP_PIXEL_FRACTION):
    differences = numpy.abs(first - second)[mask]
    count = max(1, int(math.ceil(fraction * differences.size - 1e-9)))
    largest = numpy.partition(differences, differences.size - count)[differences.size - count:]
    return float(largest.mean()) * LUMA_TO_BYTE_SCALE


def poc_numbers(lumas, hidden_luma):
    swing_reference, swing_second = lumas[ROUND2_REFERENCE_SWING_IDS[0]], lumas[ROUND2_REFERENCE_SWING_IDS[1]]
    height, width = swing_reference.shape
    mask = poc_mask(height, width)
    fog_visible = mean_absolute_difference(swing_reference, hidden_luma, mask)
    swing_response = top_fraction_mean(lumas[SWING_REPRESENTATIVE_ID], swing_reference, mask)
    swing_noise = top_fraction_mean(swing_reference, swing_second, mask)
    walk_response = top_fraction_mean(lumas[WALK_REPRESENTATIVE_ID], lumas[ROUND2_REFERENCE_WALK_IDS[0]], mask)
    walk_noise = top_fraction_mean(lumas[ROUND2_REFERENCE_WALK_IDS[0]], lumas[ROUND2_REFERENCE_WALK_IDS[1]], mask)
    swing_ratio = swing_response / swing_noise if swing_noise > 0.0 else None
    meets = {"Fv": fog_visible >= POC_FOG_VISIBLE_AT_LEAST, "Rs": swing_response >= POC_RESPONSE_AT_LEAST, "Rs_over_Rn": swing_response >= POC_RESPONSE_OVER_NOISE_AT_LEAST * swing_noise}
    return {"region_C_rows_cols": list(box_region(height, width, POC_REGION_ROW_FRACTIONS, POC_REGION_COLUMN_FRACTIONS)), "Fv": fog_visible,
            "Fv_with_second_reference": mean_absolute_difference(swing_second, hidden_luma, mask), "Rs": swing_response, "Rn": swing_noise, "Rs_over_Rn": swing_ratio,
            "Rw": walk_response, "Rn_walk": walk_noise, "Rw_over_Rn_walk": walk_response / walk_noise if walk_noise > 0.0 else None, "meets": meets, "meets_all": all(meets.values())}


def split_walk_numbers(lumas):
    walk_stopped, walk_reference = lumas[WALK_REPRESENTATIVE_ID], lumas[ROUND2_REFERENCE_WALK_IDS[0]]
    height, width = walk_stopped.shape
    top, bottom, left, right = walk_region(height, width)
    middle = visible_check.fraction_to_index(WALK_SPLIT_COLUMN_FRACTION, width)
    difference = numpy.abs(walk_stopped - walk_reference) * LUMA_TO_BYTE_SCALE
    left_value = float(difference[top:bottom, left:middle].mean())
    right_value = float(difference[top:bottom, middle:right].mean())
    total = left_value + right_value
    return {"Wk_L": left_value, "Wk_R": right_value, "Wk_all": float(difference[top:bottom, left:right].mean()), "asymmetry": (left_value - right_value) / total if total > 0.0 else None,
            "split_column": middle, "RW_rows_cols": [top, bottom, left, right]}


def world_side_axis(camera_yaw_degrees, left=True):
    radians = math.radians(camera_yaw_degrees)
    vector = (math.sin(radians), -math.cos(radians)) if left else (-math.sin(radians), math.cos(radians))
    if abs(vector[0]) > abs(vector[1]):
        return "+X" if vector[0] > 0 else "-X"
    return "+Y" if vector[1] > 0 else "-Y"


def apply_asymmetry_reading(asymmetry, camera_yaws):
    first = asymmetry.get("A0")
    if first is None or abs(first) < ASYMMETRY_NEGLIGIBLE:
        return {"lines": [f"A0 치우침 {format_number(first, 3)} 이(가) {ASYMMETRY_NEGLIGIBLE} 미만이라 읽는 법을 적용하지 않는다"], "verdict": {}}
    biased_left = first > 0
    side_text = "폰의 왼쪽(화면 왼쪽)" if biased_left else "폰의 오른쪽(화면 오른쪽)"
    lines = [f"A0 치우침 {first:+.3f}: 걷기 자국이 {side_text}으로 치우침(세계 축으로는 {world_side_axis(camera_yaws['A0'], biased_left)} 쪽)"]
    verdict = {}
    halved = ASYMMETRY_HALVED_RATIO * abs(first)
    value = asymmetry.get("A1")
    if value is not None:
        reduced = abs(value) < halved
        verdict["A1"] = "knife_input" if reduced else "not_knife_input"
        lines.append(f"A1(칼 바람 0·칼 깎기 0) {value:+.3f}: A0 의 절반({halved:.3f}) " + ("아래로 줄었다 → 칼 쪽 입력이 원인" if reduced else "아래로 줄지 않았다 → 칼 쪽 입력이 원인이 아님"))
    for name in ("A2", "A3"):
        value = asymmetry.get(name)
        if value is None:
            continue
        strong = abs(value) >= halved
        if strong and value * first > 0:
            verdict[name] = "pawn_side"
            lines.append(f"{name} {value:+.3f}: 폰 기준 A0 와 같은 쪽({side_text})에 남음 → 폰 쪽 원인")
        elif strong and name == "A2":
            verdict[name] = "grid_side"
            lines.append(f"{name} {value:+.3f}: 폰 기준 반대쪽이고 세계 축으로는 A0 와 같은 쪽({world_side_axis(camera_yaws['A0'], biased_left)})에 남음(A2 는 걷는 방향·카메라가 180도 돌아 있다) → 계산 격자 쪽 원인")
        elif strong:
            verdict[name] = "undetermined"
            lines.append(f"{name} {value:+.3f}: 폰 기준 반대쪽. A3 는 걷는 길이 A0 의 치우침 축(세계 Y)과 나란해 세계 축 같은 쪽을 정할 수 없다 → 읽는 법의 두 갈래로 가를 수 없다")
        else:
            verdict[name] = "weakened"
            lines.append(f"{name} {value:+.3f}: A0 의 절반({halved:.3f}) 아래로 약해짐 → 읽는 법의 두 갈래(폰 쪽·격자 쪽) 어느 쪽도 해당하지 않음")
    value = asymmetry.get("A4")
    if value is not None:
        flipped = value * first < 0 and abs(value) >= ASYMMETRY_NEGLIGIBLE
        verdict["A4"] = "light" if flipped else "not_light"
        lines.append(f"A4(빛 반대쪽) {value:+.3f}: " + ("부호가 뒤집힘 → 빛 때문에 그렇게 보이는 것" if flipped else "뒤집히지 않음 → 빛 때문만은 아님"))
    return {"lines": lines, "verdict": verdict}


def cloud_trail_row(name, numbers, mid_numbers):
    texture = numbers["Tx2"]
    swing = numbers["Sk2"][SWING_REPRESENTATIVE_ID]
    walk_stopped = numbers["Wk"][WALK_REPRESENTATIVE_ID]
    walk_later = numbers["Wk"][WALK_REFILL_ID]
    refill = walk_later / walk_stopped if walk_stopped > 0.0 else None
    mid_swing = mid_numbers["Sk2"][SWING_REPRESENTATIVE_ID]
    cloud = {"Tx2_over_0.45": texture > CLOUD_TEXTURE_THRESHOLD, "Sk2_at_least_0.8_of_mid": swing >= CLOUD_SWING_KEEP_RATIO * mid_swing}
    trail = {"Wk17.6_over_Wk12.1_at_least_0.4": refill is not None and refill >= TRAIL_REFILL_AT_LEAST, "Wk12.1_in_8_14": TRAIL_WALK_RANGE[0] <= walk_stopped <= TRAIL_WALK_RANGE[1]}
    return {"name": name, "Tx2": texture, "Sk2_2.5": swing, "Sk2_over_mid": swing / mid_swing if mid_swing > 0.0 else None, "Wk_12.1": walk_stopped, "Wk_17.6": walk_later, "Wk_refill_ratio": refill,
            "cloud_conditions": cloud, "cloud_ok": all(cloud.values()), "trail_conditions": trail, "trail_ok": all(trail.values())}


def log_cell(report):
    log = report["log"]
    return f"{log.get('submitted')}/{log.get('rejected')}/{log.get('fog3d_warning_or_error_lines')}"


def usable_cell(report):
    return "예" if report.get("usable") else "아니오"


def collect_round2_runs(root, names, options):
    collected = {}
    for name in names:
        try:
            report, matched, run_record, lumas = analyze_run(Path(root) / name, options)
        except (InputProblem, OSError, KeyError, ValueError) as error:
            collected[name] = {"error": str(error)}
            continue
        collected[name] = {"report": report, "matched": matched, "run_record": run_record, "lumas": lumas}
    return collected


def changed_text(report):
    changes = report.get("changed_fields") or {}
    return ", ".join(f"{field} {change['to']:g}" if isinstance(change["to"], (int, float)) and not isinstance(change["to"], bool) else f"{field} {change['to']}" for field, change in changes.items()) or "기본값"


def build_poc_table(collected):
    hidden = collected.get(HIDDEN_RUN_NAME)
    if not hidden or "lumas" not in hidden or hidden["lumas"] is None:
        raise InputProblem(f"{HIDDEN_RUN_NAME}(구역 숨김 장) 실행 결과가 없다")
    hidden_luma = hidden["lumas"][ROUND2_REFERENCE_SWING_IDS[0]]
    height, width = hidden_luma.shape
    hidden_mask = poc_mask(height, width)
    rows, back_rows = [], []
    for name in POC_TOP_NAMES:
        entry = collected.get(name)
        if not entry or entry.get("lumas") is None:
            rows.append({"name": name, "missing": entry.get("error") if entry else "실행 없음"})
            continue
        report = entry["report"]
        rows.append({"name": name, "changed": changed_text(report), "valid": bool(report["valid"]), "usable": report.get("usable"), "invalid_reasons": report["invalid_reasons"],
                     "log": {"submitted": report["log"].get("submitted"), "rejected": report["log"].get("rejected"), "fog3d_warning_or_error_lines": report["log"].get("fog3d_warning_or_error_lines")},
                     "console_extra": report.get("console_extra"), **poc_numbers(entry["lumas"], hidden_luma)})
    for name in POC_BACK_NAMES:
        entry = collected.get(name)
        if not entry or entry.get("lumas") is None:
            back_rows.append({"name": name, "missing": entry.get("error") if entry else "실행 없음"})
            continue
        report, numbers = entry["report"], entry["report"]["numbers"]
        back_rows.append({"name": name, "changed": changed_text(report), "valid": bool(report["valid"]), "usable": report.get("usable"), "Wk_12.1": numbers["Wk"][WALK_REPRESENTATIVE_ID], "Sk_2.5": numbers["Sk"][SWING_REPRESENTATIVE_ID],
                          "Sk2_2.5": numbers["Sk2"][SWING_REPRESENTATIVE_ID], "Tx2": numbers["Tx2"], "N_W": numbers["noise_walk_N_W"], "N_S": numbers["noise_swing_N_S"],
                          "log": {"submitted": report["log"].get("submitted"), "rejected": report["log"].get("rejected"), "fog3d_warning_or_error_lines": report["log"].get("fog3d_warning_or_error_lines")}})
    hidden_report = hidden["report"]
    return {"criteria": {"Fv_at_least": POC_FOG_VISIBLE_AT_LEAST, "Rs_at_least": POC_RESPONSE_AT_LEAST, "Rs_over_Rn_at_least": POC_RESPONSE_OVER_NOISE_AT_LEAST},
            "formulas": "C = 세로 [0.25H,0.75H) x 가로 [0.25W,0.75W). Fv = C 안 |Y(S 기준 장 S8.3) - Y(top_hidden 의 S8.3)| 평균. Rs = C 안 |Y(S2.5) - Y(S8.3)| 가운데 큰 쪽 2% 화소(올림) 평균. Rn = 같은 식, S8.3 과 S8.8. Rw = 같은 식, W12.1 과 W19.4, 걷기 잡음 Rn_walk = W19.4 와 W19.9. 8비트 밝기 단위",
            "hidden_run": {"valid": bool(hidden_report["valid"]), "usable": hidden_report.get("usable"), "log": {"submitted": hidden_report["log"].get("submitted"), "rejected": hidden_report["log"].get("rejected"), "fog3d_warning_or_error_lines": hidden_report["log"].get("fog3d_warning_or_error_lines")},
                           "self_noise_S8.3_vs_S8.8_in_C": mean_absolute_difference(hidden["lumas"][ROUND2_REFERENCE_SWING_IDS[0]], hidden["lumas"][ROUND2_REFERENCE_SWING_IDS[1]], hidden_mask)},
            "rows": rows, "back_rows": back_rows, "any_config_meets_all": any(row.get("meets_all") for row in rows)}


def build_asymmetry_table(collected):
    rows, asymmetry, camera_yaws = [], {}, {}
    for name in ASYMMETRY_NAMES:
        entry = collected.get(name)
        if not entry or entry.get("lumas") is None:
            rows.append({"name": name, "missing": entry.get("error") if entry else "실행 없음"})
            continue
        report, record = entry["report"], entry["run_record"]
        split = split_walk_numbers(entry["lumas"])
        camera_yaw = record.get("camera_yaw_degrees")
        camera_yaw = 0.0 if camera_yaw is None else camera_yaw
        asymmetry[name] = split["asymmetry"]
        camera_yaws[name] = camera_yaw
        rows.append({"name": name, "valid": bool(report["valid"]), "usable": report.get("usable"), "walk_yaw": 0.0 if record.get("walk_yaw_degrees") is None else record.get("walk_yaw_degrees"), "camera_yaw": camera_yaw, "light": record.get("light"), "changed": changed_text(report),
                     "screen_left_is_world": world_side_axis(camera_yaw, True), "description": record.get("description", ""), "log": {"submitted": report["log"].get("submitted"), "rejected": report["log"].get("rejected"), "fog3d_warning_or_error_lines": report["log"].get("fog3d_warning_or_error_lines")},
                     "walk_end_cm": record.get("walk_end_cm"), **split})
    reading = apply_asymmetry_reading(asymmetry, camera_yaws) if "A0" in asymmetry else {"lines": ["A0 결과가 없어 읽는 법을 적용하지 못했다"], "verdict": {}}
    return {"formulas": "Wk_L·Wk_R = 걷기 영역 RW(세로 [0.72H,0.98H) x 가로 [0.35W,0.65W))를 가로 0.50W 에서 둘로 나눈 왼쪽·오른쪽에서 |Y(W12.1) - Y(W19.4)| 평균(8비트 단위). 치우침 = (Wk_L - Wk_R) / (Wk_L + Wk_R), 양수면 화면 왼쪽(폰의 왼쪽)이 크다",
            "thresholds": {"halved_ratio": ASYMMETRY_HALVED_RATIO, "negligible": ASYMMETRY_NEGLIGIBLE}, "rows": rows, "reading": reading}


def build_cloud_trail_table(collected):
    mid = collected.get("s3")
    if not mid or mid.get("lumas") is None:
        raise InputProblem("s3(조합 탐색의 중 값) 실행 결과가 없다")
    mid_numbers = mid["report"]["numbers"]
    rows = []
    for name in CLOUD_TRAIL_NAMES:
        entry = collected.get(name)
        if not entry or entry.get("lumas") is None:
            rows.append({"name": name, "missing": entry.get("error") if entry else "실행 없음"})
            continue
        report = entry["report"]
        rows.append({"changed": changed_text(report), "valid": bool(report["valid"]), "usable": report.get("usable"), "log": {"submitted": report["log"].get("submitted"), "rejected": report["log"].get("rejected"), "fog3d_warning_or_error_lines": report["log"].get("fog3d_warning_or_error_lines")},
                     **cloud_trail_row(name, report["numbers"], mid_numbers)})
    return {"conditions": "구름 무늬: Tx2 > 0.45 이고 Sk2(2.5) >= 0.8 x s3 의 Sk2(2.5). 걷기 자국 이어짐: Wk(17.6) / Wk(12.1) >= 0.4 이고 8 <= Wk(12.1) <= 14. 고르는 것은 감독이 그림을 보고 한다",
            "mid_reference": {"name": "s3", "Tx2": mid_numbers["Tx2"], "Sk2_2.5": mid_numbers["Sk2"][SWING_REPRESENTATIVE_ID], "Wk_12.1": mid_numbers["Wk"][WALK_REPRESENTATIVE_ID], "Wk_17.6": mid_numbers["Wk"][WALK_REFILL_ID]}, "rows": rows}


def poc_markdown(table):
    lines = ["# 탑다운 개념 증명 (카메라 top, 옆빛) — 숫자", "", table["formulas"], "",
             f"성공 기준: Fv >= {table['criteria']['Fv_at_least']:g} 이고 Rs >= {table['criteria']['Rs_at_least']:g} 이며 Rs >= {table['criteria']['Rs_over_Rn_at_least']:g} x Rn. 어느 한 구성이라도 충족: **{'예' if table['any_config_meets_all'] else '아니오'}**", "",
             f"구역 숨김 장(top_hidden): 유효 {'예' if table['hidden_run']['valid'] else '아니오'}, 로그 사용 {'예' if table['hidden_run']['usable'] else '아니오'}, 제출/거부/경고 {table['hidden_run']['log']['submitted']}/{table['hidden_run']['log']['rejected']}/{table['hidden_run']['log']['fog3d_warning_or_error_lines']}, C 안 자체 잡음(S8.3 대 S8.8) {table['hidden_run']['self_noise_S8.3_vs_S8.8_in_C']:.3f}", "",
             "| 구성 | 바꾼 값 | Fv | Rs | Rn | Rs ÷ Rn | Rw | 걷기 잡음 | Rw ÷ 걷기 잡음 | Fv≥10 | Rs≥12 | Rs≥3Rn | 모두 충족 | 유효 | 로그 사용 | 로그(제출/거부/경고) |", "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    yes = lambda value: "예" if value else "아니오"
    for row in table["rows"]:
        if "missing" in row:
            lines.append(f"| {row['name']} | (결과 없음: {row['missing']}) |" + " |" * 14)
            continue
        log = row["log"]
        lines.append(f"| {row['name']} | {row['changed']} | {row['Fv']:.2f} | {row['Rs']:.2f} | {row['Rn']:.2f} | {format_number(row['Rs_over_Rn'])} | {row['Rw']:.2f} | {row['Rn_walk']:.2f} | {format_number(row['Rw_over_Rn_walk'])} | "
                     f"{yes(row['meets']['Fv'])} | {yes(row['meets']['Rs'])} | {yes(row['meets']['Rs_over_Rn'])} | **{yes(row['meets_all'])}** | {yes(row['valid'])} | {yes(row['usable'])} | {log['submitted']}/{log['rejected']}/{log['fog3d_warning_or_error_lines']} |")
    lines += ["", "## 뒤 카메라 실행(눈으로 보는 용도, 등록본 1c 의 뒤 카메라 숫자)", "", "| 실행 | 바꾼 값 | Wk(12.1) | Sk(2.5) | Sk2(2.5) | Tx2 | N_W | N_S | 유효 | 로그 사용 | 로그(제출/거부/경고) |", "|---|---|---|---|---|---|---|---|---|---|---|"]
    for row in table["back_rows"]:
        if "missing" in row:
            lines.append(f"| {row['name']} | (결과 없음: {row['missing']}) |" + " |" * 9)
            continue
        log = row["log"]
        lines.append(f"| {row['name']} | {row['changed']} | {row['Wk_12.1']:.2f} | {row['Sk_2.5']:.2f} | {row['Sk2_2.5']:.2f} | {row['Tx2']:.2f} | {row['N_W']:.2f} | {row['N_S']:.2f} | {yes(row['valid'])} | {yes(row['usable'])} | {log['submitted']}/{log['rejected']}/{log['fog3d_warning_or_error_lines']} |")
    return "\n".join(lines) + "\n"


def asymmetry_markdown(table):
    lines = ["# 걷기 자국 치우침 진단 (중 값, 뒤 카메라) — 숫자", "", table["formulas"], "",
             "| 실행 | 바꾼 것 | 걷는 방향 | 카메라 방향 | 화면 왼쪽 = 세계 | 빛 | Wk_L | Wk_R | 치우침 | Wk 전체 | 유효 | 로그 사용 | 로그(제출/거부/경고) |", "|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    yes = lambda value: "예" if value else "아니오"
    for row in table["rows"]:
        if "missing" in row:
            lines.append(f"| {row['name']} | (결과 없음: {row['missing']}) |" + " |" * 11)
            continue
        log = row["log"]
        lines.append(f"| {row['name']} | {row['description']} | {row['walk_yaw']:g} | {row['camera_yaw']:g} | {row['screen_left_is_world']} | {row['light']} | {row['Wk_L']:.2f} | {row['Wk_R']:.2f} | {format_number(row['asymmetry'], 3)} | {row['Wk_all']:.2f} | {yes(row['valid'])} | {yes(row['usable'])} | {log['submitted']}/{log['rejected']}/{log['fog3d_warning_or_error_lines']} |")
    lines += ["", f"읽는 법(등록본 2절)을 글자대로 적용한 결과. 기준: '절반 아래로 줄면'은 |값| < {table['thresholds']['halved_ratio']} x |A0|, '뒤집히면'은 부호가 반대이고 |값| >= {table['thresholds']['negligible']}.", ""]
    lines += [f"- {line}" for line in table["reading"]["lines"]]
    return "\n".join(lines) + "\n"


def cloud_trail_markdown(table):
    reference = table["mid_reference"]
    lines = ["# 구름 무늬와 걷기 자국 이어짐 (중 값 위, 뒤 카메라) — 숫자", "", table["conditions"], "",
             f"중 값 기준(s3, 조합 탐색): Tx2 {reference['Tx2']:.4f}, Sk2(2.5) {reference['Sk2_2.5']:.2f}, Wk(12.1) {reference['Wk_12.1']:.2f}, Wk(17.6) {reference['Wk_17.6']:.2f}", "",
             "| 실행 | 정본 대비 바꾼 값 | Tx2 | Sk2(2.5) | Sk2 ÷ s3 | Wk(12.1) | Wk(17.6) | Wk(17.6) ÷ Wk(12.1) | Tx2>0.45 | Sk2≥0.8×s3 | 구름 조건 | 이어짐 비≥0.4 | Wk(12.1) 8~14 | 이어짐 조건 | 유효 | 로그 사용 | 로그 |", "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    yes = lambda value: "예" if value else "아니오"
    for row in table["rows"]:
        if "missing" in row:
            lines.append(f"| {row['name']} | (결과 없음: {row['missing']}) |" + " |" * 15)
            continue
        log, cloud, trail = row["log"], row["cloud_conditions"], row["trail_conditions"]
        lines.append(f"| {row['name']} | {row['changed']} | {row['Tx2']:.3f} | {row['Sk2_2.5']:.2f} | {format_number(row['Sk2_over_mid'])} | {row['Wk_12.1']:.2f} | {row['Wk_17.6']:.2f} | {format_number(row['Wk_refill_ratio'])} | "
                     f"{yes(cloud['Tx2_over_0.45'])} | {yes(cloud['Sk2_at_least_0.8_of_mid'])} | **{yes(row['cloud_ok'])}** | {yes(trail['Wk17.6_over_Wk12.1_at_least_0.4'])} | {yes(trail['Wk12.1_in_8_14'])} | **{yes(row['trail_ok'])}** | {yes(row['valid'])} | {yes(row['usable'])} | {log['submitted']}/{log['rejected']}/{log['fog3d_warning_or_error_lines']} |")
    return "\n".join(lines) + "\n"


def write_round2_tables(options):
    out_directory = Path(options.round2_tables)
    out_directory.mkdir(parents=True, exist_ok=True)
    plain_options = parse_arguments(["--runs", "x", "--no-images", "--out-root", options.out_root])
    names = [HIDDEN_RUN_NAME] + list(POC_TOP_NAMES) + list(POC_BACK_NAMES) + list(ASYMMETRY_NAMES) + list(CLOUD_TRAIL_NAMES)
    collected = collect_round2_runs(options.out_root, names, plain_options)
    tables = {"poc": build_poc_table(collected), "asym": build_asymmetry_table(collected), "cloud": build_cloud_trail_table(collected)}
    writers = {"poc": poc_markdown, "asym": asymmetry_markdown, "cloud": cloud_trail_markdown}
    for key, table in tables.items():
        stem = ROUND2_TABLE_FILES[key]
        (out_directory / f"{stem}.json").write_text(json.dumps(table, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
        (out_directory / f"{stem}.md").write_text(writers[key](table), encoding="utf-8", newline="\n")
        print("표:", (out_directory / f"{stem}.md").as_posix())
    unusable = [name for name, entry in collected.items() if "report" in entry and (not entry["report"]["valid"] or entry["report"].get("usable") is False)]
    missing = [name for name, entry in collected.items() if "error" in entry or entry.get("lumas") is None]
    if unusable or missing:
        print("무효·로그 사용 불가:", ", ".join(unusable) or "-", "/ 결과 없음:", ", ".join(missing) or "-")
    return EXIT_SOME_INVALID if unusable or missing else EXIT_ALL_VALID


def check(condition, label, failures):
    if not condition:
        failures.append(label)
    print(("통과 " if condition else "실패 ") + label)


def make_gray_image(width, height, value):
    return numpy.full((height, width), value, dtype=numpy.float64)


def write_gray(path, gray):
    gray8 = numpy.clip(gray, 0, 255).astype(numpy.uint8)
    Image.fromarray(numpy.stack([gray8] * 3, axis=-1)).save(path)


def build_synthetic_capture(run_directory, walk_end_seconds, width=320, height=180, drop_shot=None, with_log=True):
    run_directory = Path(run_directory)
    capture = run_directory / CAPTURE_SUBDIRECTORY
    capture.mkdir(parents=True, exist_ok=True)
    walk = walk_mask(height, width)
    swing = swing_mask(height, width)
    walk_gain = {"W10.0": 5, "W10.4": 5, "W10.8": 5, "W12.1": 20, "W13.1": 16, "W14.1": 12, "W15.6": 8, "W17.6": 10, "W19.4": 0, "W19.9": 2}
    swing_gain = {"S0.6": 30, "S1.2": 30, "S1.9": 20, "S2.5": 8, "S3.5": 7, "S5.0": 6, "S7.0": 4, "S8.3": 0, "S8.8": 1}
    shots, reference_game_time = [], 15.0
    for entry in variants_module.build_shot_table(0.0):
        identifier, gain = entry["id"], (walk_gain if entry["group"] == "W" else swing_gain)[entry["id"]]
        mask = walk if entry["group"] == "W" else swing
        gray = make_gray_image(width, height, 100.0)
        gray = gray + numpy.where(mask, gain, 0.0 if entry["role"] == "reference" else 40.0)
        path = capture / f"00_c_{round(entry['time'] * 1000):05d}ms.png"
        if identifier != drop_shot:
            write_gray(path, gray)
        shots.append({"time": entry["time"], "path": path.as_posix(), "late_seconds": 0.0, "captured_game_time": entry["time"] + 0.0167, "saved": True})
    events_done = [{"time": 0.0, "game_time": 0.0167, "console": []}, {"time": 7.3, "game_time": 7.3, "console": []}, {"time": 8.9, "game_time": 8.9, "console": []},
                   {"time": 9.6, "game_time": 9.6, "move": {"mode": "input"}}, {"time": 18.4, "game_time": 18.4, "console": []}]
    schedule = {"times": [shot["time"] for shot in shots], "reference": "now", "reference_game_time": reference_game_time, "prefix": "c", "shots": shots, "events_done": events_done, "done": True}
    (capture / "00_c_schedule.json").write_text(json.dumps(schedule), encoding="utf-8")
    walk_status = {"mode": "input", "start_game_time": reference_game_time + 9.6, "elapsed_game_seconds": walk_end_seconds - 9.6, "moved_cm": 601.0, "timed_out": False, "done": True}
    (capture / "00_event09600ms_walk.json").write_text(json.dumps(walk_status), encoding="utf-8")
    (capture / "result.json").write_text("{}", encoding="utf-8")
    if with_log:
        (run_directory / LOG_SNAPSHOT_FILE_NAME).write_text("LogTemp: start\nLogTDFog3D: Fog3D zone summary: submitted=120 rejected=0\n", encoding="utf-8")


def run_self_test():
    failures = []
    check(walk_region(720, 1280) == (519, 706, 448, 832), "1280x720 에서 RW = 세로 519~706·가로 448~832 (ceil(비율×크기−1e-9))", failures)
    check(swing_region(720, 1280) == (432, 684, 320, 960) and swing_excluded_columns(1280) == (538, 743), "1280x720 에서 RS = 세로 432~684·가로 320~960, 가운데 538~743 제외", failures)
    check(walk_region(100, 160) == (72, 98, 56, 104), "작은 그림(160x100) RW 경계", failures)
    prereg = PREREGISTRATION_PATH.read_text(encoding="utf-8")
    section = prereg.split("## 3.")[1].split("## 4.")[0]
    walk_match = re.search(r"RW = 화면 세로 \[([\d.]+) H, ([\d.]+) H\) × 가로 \[([\d.]+) W, ([\d.]+) W\)", section)
    swing_match = re.search(r"RS = 세로 \[([\d.]+) H, ([\d.]+) H\) × 가로 \[([\d.]+) W, ([\d.]+) W\) 에서 가운데 \[([\d.]+) W, ([\d.]+) W\)", section)
    check(walk_match is not None and tuple(map(float, walk_match.groups())) == WALK_REGION_ROW_FRACTIONS + WALK_REGION_COLUMN_FRACTIONS, "RW 비율이 등록본 3절과 같다", failures)
    check(swing_match is not None and tuple(map(float, swing_match.groups())) == SWING_REGION_ROW_FRACTIONS + SWING_REGION_COLUMN_FRACTIONS + SWING_EXCLUDED_COLUMN_FRACTIONS, "RS 비율이 등록본 3절과 같다", failures)
    check("σ 20화소" in section and TEXTURE_BLUR_SIGMA_PIXELS == 20.0, "평소 무늬 흐림 σ 20화소가 등록본 3절과 같다", failures)
    mask = walk_mask(720, 1280)
    check(int(mask.sum()) == (706 - 519) * (832 - 448), "RW 마스크 화소 수", failures)
    swing = swing_mask(720, 1280)
    check(int(swing.sum()) == (684 - 432) * ((960 - 320) - (743 - 538)), "RS 마스크 화소 수(가운데 제외)", failures)

    random_generator = numpy.random.default_rng(1)
    small = random_generator.random((12, 14))
    sigma = 1.5
    radius = int(BLUR_TRUNCATE_SIGMAS * sigma + 0.5)
    offsets = numpy.arange(-radius, radius + 1)
    weights = numpy.exp(-0.5 * (offsets / sigma) ** 2)
    weights /= weights.sum()
    direct = numpy.zeros_like(small)
    padded = numpy.pad(small, radius, mode="symmetric")
    for row in range(small.shape[0]):
        for column in range(small.shape[1]):
            window = padded[row:row + 2 * radius + 1, column:column + 2 * radius + 1]
            direct[row, column] = (weights[:, None] * weights[None, :] * window).sum()
    check(float(numpy.abs(gaussian_blur(small, sigma) - direct).max()) < 1e-12, "가우스 흐림이 직접 2차원 합성곱과 같다", failures)
    try:
        from scipy.ndimage import gaussian_filter
        check(float(numpy.abs(gaussian_blur(small, sigma) - gaussian_filter(small, sigma, mode="reflect", truncate=BLUR_TRUNCATE_SIGMAS)).max()) < 1e-12, "scipy 가우스 필터와 일치(있을 때만 확인)", failures)
    except ImportError:
        print("건너뜀 scipy 없음")
    constant = make_gray_image(160, 90, 0.4)
    check(texture_rms(constant, swing_mask(90, 160)) < 1e-9, "평소 무늬: 같은 밝기 그림은 0", failures)
    checker = (numpy.indices((90, 160)).sum(axis=0) % 2).astype(numpy.float64) * 0.2
    check(texture_rms(checker, swing_mask(90, 160)) > 0.05 * 255 * 0.5, "평소 무늬: 바둑판 그림은 양수(표준편차 0.1 근처)", failures)

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        field_types = variants_module.parse_settings_header(variants_module.SETTINGS_HEADER_PATH.read_text(encoding="utf-8"))
        canonical = variants_module.load_canonical_tuning()
        canonical_sha256 = variants_module.sha256_of_file(variants_module.CANONICAL_TUNING_PATH)
        names = ("base_a", "body12", "bad_walk", "missing")
        for name in names:
            specification = variants_module.make_specification(name, "back", "orig", 2, 0.0, {"BodyErosionPerSec": 12.0} if name == "body12" else {}, group="시험", description="합성")
            outcome = variants_module.materialize_run(specification, root, canonical, field_types, canonical_sha256)
            check(outcome["status"] == "written", f"합성 실행 {name} 의 run.json 만들기", failures)
        build_synthetic_capture(root / "base_a", 11.0)
        build_synthetic_capture(root / "body12", 11.2)
        build_synthetic_capture(root / "bad_walk", 12.0)
        build_synthetic_capture(root / "missing", 11.0, drop_shot="W13.1")
        options = argparse.Namespace(out_root=root, capture_subdirectory=CAPTURE_SUBDIRECTORY, walk_shift=0.0, live_log=False, log=None, no_images=False, animate=True, baseline="base_a",
                                     all=False, runs=names, compare_rows=8, group_compare=True, group_rows=2)
        report, matched, run_record, lumas = analyze_run(root / "base_a", options)
        numbers = report["numbers"]
        check(report["valid"] and report["usable"] and not report["invalid_reasons"], "합성 base_a: 유효 조건·로그 통과", failures)
        check(abs(numbers["Wk"]["W12.1"] - 20.0) < 1e-6 and abs(numbers["Wk"]["W17.6"] - 10.0) < 1e-6, "Wk(12.1)=20, Wk(17.6)=10 (영역 밖 변화는 안 센다)", failures)
        check(abs(numbers["Sk"]["S2.5"] - 8.0) < 1e-6 and abs(numbers["Sk"]["S7.0"] - 4.0) < 1e-6, "Sk(2.5)=8, Sk(7.0)=4 (가운데 제외)", failures)
        check(abs(numbers["noise_walk_N_W"] - 2.0) < 1e-6 and abs(numbers["noise_swing_N_S"] - 1.0) < 1e-6, "되풀이 잡음 N_W=2, N_S=1", failures)
        check(abs(numbers["refill_ratio_swing"] - 0.5) < 1e-9 and abs(numbers["refill_ratio_walk"] - 0.5) < 1e-9, "다시 차오름 비 둘 = 0.5", failures)
        check(abs(numbers["Wk_over_noise"] - 10.0) < 1e-6 and abs(numbers["Sk_over_noise"] - 8.0) < 1e-6, "자국 대비 ÷ 잡음", failures)
        check(abs(numbers["Tx"]) < 1e-6, "평소 무늬 Tx = 0 (같은 밝기 기준 장)", failures)
        second_region_mask = box_mask(180, 320, swing2_region(180, 320))
        expected_second = float(numpy.where(swing_mask(180, 320), 8.0, 40.0)[second_region_mask].mean())
        check(abs(numbers["Sk2"]["S2.5"] - expected_second) < 1e-6 and abs(numbers["Sk2"]["S2.5"] - numbers["Sk"]["S2.5"]) > 1.0, f"Sk2(2.5) 가 RS2 영역 평균 {expected_second:.3f} 와 같고 Sk 와 다르다", failures)
        check(abs(numbers["Tx2"]) < 1e-6 and numbers["regions"]["RS2_rows_cols"] == [90, 137, 58, 173] and numbers["regions"]["RT_rows_cols"] == [99, 171, 7, 96], f"Tx2 = 0 이고 RS2·RT 경계 {numbers['regions']['RS2_rows_cols']}·{numbers['regions']['RT_rows_cols']}", failures)
        check(swing2_region(720, 1280) == (360, 548, 231, 692) and texture2_region(720, 1280) == (396, 684, 26, 384), "1280x720 에서 RS2 = 세로 360~548·가로 231~692, RT = 세로 396~684·가로 26~384", failures)
        check(abs(numbers["Wk_moving_information_only"]["W10.0"] - 5.0) < 1e-6, "걷는 중 장 값은 참고용으로 따로 나온다", failures)
        check(abs(report["validity"]["swing_to_S2.5_seconds"] - 2.5) < 1e-6 and abs(report["validity"]["walk_end_game_seconds"] - 11.0) < 1e-6, "유효 조건 값: 휘두르기→2.5초 장 2.5초, 걷기 끝 11.0초", failures)
        strip_path = root / "base_a" / REPORT_DIRECTORY_NAME / "strip.png"
        with Image.open(strip_path) as strip:
            check(strip.width <= STRIP_WIDTH_LIMIT and strip.width == 2000, f"그림 띠 가로 {strip.width} ≤ 2000", failures)
        for stem, frame_count in (("swing", 9), ("walk", 10)):
            candidates = list((root / "base_a" / REPORT_DIRECTORY_NAME).glob(stem + ".*"))
            check(len(candidates) == 1, f"움직이는 그림 {stem} 하나 만들어짐 {[path.suffix for path in candidates]}", failures)
            if candidates:
                with Image.open(candidates[0]) as animation:
                    check(animation.width == ANIMATION_WIDTH and getattr(animation, "n_frames", 1) == frame_count, f"{stem} 움직이는 그림 가로 {ANIMATION_WIDTH}·{frame_count}장", failures)
        bad_report, _, _, _ = analyze_run(root / "bad_walk", options)
        check(not bad_report["valid"] and any("걷기가" in reason for reason in bad_report["invalid_reasons"]), "걷기가 11.6초 뒤에 끝난 실행은 무효", failures)
        missing_report, _, _, _ = analyze_run(root / "missing", options)
        check(missing_report["numbers"] is None and not missing_report["valid"], "장이 빠진 실행은 숫자 없이 무효", failures)
        exit_code = run_reports(options)
        check(exit_code == EXIT_SOME_INVALID, "무효 실행이 있으면 종료 코드 1", failures)
        summary = json.loads((root / "summary.json").read_text(encoding="utf-8"))
        check(abs(summary["runs"]["body12"]["multiples_against_baseline"]["Wk_12.1"] - 1.0) < 1e-6, "기본값 대비 배수(같은 합성 값이면 1.0)", failures)
        markdown = (root / "summary.md").read_text(encoding="utf-8")
        check("| body12 |" in markdown and "아니오" in markdown, "마크다운 표에 실행 행과 무효 표시", failures)
        check("Sk2(2.5)" in markdown and "Tx2" in markdown, "마크다운 표에 Sk2·Tx2 열", failures)
        group_files = sorted(path.name for path in root.glob("groupcmp_*.png"))
        check(len(group_files) == 2 and "Sk2(2.5)" in compare_label({"name": "x", "numbers": numbers, "multiples": {}, "valid": True, "camera": "back"}) and "Tx2" in compare_label({"name": "x", "numbers": numbers, "multiples": {}, "valid": True, "camera": "back"}), f"묶음별 견줌 그림 {group_files} (행 2개씩 둘로 나뉨), 이름표에 Sk2", failures)
        compare_files = sorted(root.glob("compare_*.png"))
        check(len(compare_files) == 1 and all(Image.open(path).width <= COMPARE_WIDTH_LIMIT for path in compare_files), f"견줌 그림 {len(compare_files)}장, 가로 ≤ 1600", failures)
        snapshot_log = root / "fake.log"
        snapshot_log.write_text("old\n" + visible_check.PIE_START_MARKER + "\nLogX: Fog3D zone summary: submitted=5 rejected=0\n", encoding="utf-8")
        (root / "snap_run").mkdir()
        count, has_summary = snapshot_run_log(root / "snap_run", snapshot_log)
        check(count == 2 and has_summary and "submitted=5" in (root / "snap_run" / LOG_SNAPSHOT_FILE_NAME).read_text(encoding="utf-8"), "로그 보관: 마지막 PIE 시작 표식부터 요약 줄까지", failures)
        warning_summary = summarize_log(["LogTDFog3D: Warning: Fog3D zone broke", "Fog3D zone summary: submitted=5 rejected=2"], "snapshot")
        check(warning_summary["usable"] is False and warning_summary["fog3d_warning_or_error_lines"] == 1 and warning_summary["rejected"] == 2, "로그 경고·거부 있으면 사용 불가", failures)
    synthetic_height, synthetic_width = 100, 160
    base_luma = numpy.full((synthetic_height, synthetic_width), 0.4)
    check(abs(top_fraction_mean(base_luma + 0.0, base_luma, poc_mask(synthetic_height, synthetic_width))) < 1e-12, "큰 쪽 2% 평균: 같은 그림이면 0", failures)
    poc_box = box_region(synthetic_height, synthetic_width, POC_REGION_ROW_FRACTIONS, POC_REGION_COLUMN_FRACTIONS)
    check(poc_box == (25, 75, 40, 120), "C 영역 160x100 에서 세로 25~75·가로 40~120", failures)
    spiked = base_luma.copy()
    spiked[25:27, 40:120] += 0.1
    expected_spike = 0.1 * LUMA_TO_BYTE_SCALE
    check(abs(top_fraction_mean(spiked, base_luma, poc_mask(synthetic_height, synthetic_width)) - expected_spike) < 1e-6, "큰 쪽 2% 화소(C 4000화소 중 80)가 정확히 상위 80화소 평균", failures)
    lumas_synthetic = {"S8.3": base_luma, "S8.8": base_luma + 0.001, "S2.5": spiked, "W12.1": base_luma + 0.05, "W19.4": base_luma, "W19.9": base_luma + 0.002}
    numbers_synthetic = poc_numbers(lumas_synthetic, base_luma - 0.1)
    check(abs(numbers_synthetic["Fv"] - 0.1 * LUMA_TO_BYTE_SCALE) < 1e-6 and abs(numbers_synthetic["Rs"] - expected_spike) < 1e-6 and abs(numbers_synthetic["Rn"] - 0.001 * LUMA_TO_BYTE_SCALE) < 1e-6, "개념 증명 숫자: Fv·Rs·Rn 이 합성 값과 같다", failures)
    check(abs(numbers_synthetic["Rw"] - 0.05 * LUMA_TO_BYTE_SCALE) < 1e-6 and numbers_synthetic["meets_all"] is True and numbers_synthetic["meets"]["Rs_over_Rn"] is True, "개념 증명 숫자: Rw 와 성공 기준 충족 판정(Fv≥10, Rs≥12, Rs≥3Rn)", failures)
    weak_numbers = poc_numbers(dict(lumas_synthetic, **{"S2.5": base_luma + 0.02}), base_luma - 0.1)
    check(weak_numbers["Rs"] < POC_RESPONSE_AT_LEAST and weak_numbers["meets_all"] is False, "Rs 12 미만이면 성공 기준 미충족", failures)
    walk_luma = base_luma.copy()
    walk_top, walk_bottom, walk_left, walk_right = walk_region(synthetic_height, synthetic_width)
    split_column = visible_check.fraction_to_index(WALK_SPLIT_COLUMN_FRACTION, synthetic_width)
    walk_luma[walk_top:walk_bottom, walk_left:split_column] += 0.06
    walk_luma[walk_top:walk_bottom, split_column:walk_right] += 0.02
    split = split_walk_numbers({"W12.1": walk_luma, "W19.4": base_luma})
    check(split_column == 80 and abs(split["Wk_L"] - 0.06 * LUMA_TO_BYTE_SCALE) < 1e-6 and abs(split["Wk_R"] - 0.02 * LUMA_TO_BYTE_SCALE) < 1e-6 and abs(split["asymmetry"] - 0.5) < 1e-9, "걷기 좌우: 0.50W 에서 둘로 나눠 Wk_L·Wk_R, 치우침 = (L-R)/(L+R) = 0.5", failures)
    check(world_side_axis(0.0) == "-Y" and world_side_axis(90.0) == "+X" and world_side_axis(180.0) == "+Y" and world_side_axis(0.0, False) == "+Y", "화면 왼쪽의 세계 축: 방향 0 → -Y, 90 → +X, 180 → +Y", failures)
    reading_yaws = {"A0": 0.0, "A1": 0.0, "A2": 180.0, "A3": 90.0, "A4": 0.0}
    reading = apply_asymmetry_reading({"A0": 0.4, "A1": 0.1, "A2": 0.35, "A3": 0.3, "A4": -0.3}, reading_yaws)["verdict"]
    check(reading == {"A1": "knife_input", "A2": "pawn_side", "A3": "pawn_side", "A4": "light"}, f"읽는 법 적용: A1 절반 아래 → 칼, A2·A3 폰 쪽, A4 뒤집힘 → 빛 {reading}", failures)
    reading_grid = apply_asymmetry_reading({"A0": 0.4, "A1": 0.38, "A2": -0.3, "A3": -0.3, "A4": 0.35}, reading_yaws)["verdict"]
    check(reading_grid == {"A1": "not_knife_input", "A2": "grid_side", "A3": "undetermined", "A4": "not_light"}, f"읽는 법 적용: A2 반대 부호 → 격자 쪽, A3 세계 축 판정 불가 {reading_grid}", failures)
    check(apply_asymmetry_reading({"A0": 0.05}, reading_yaws)["verdict"] == {}, "A0 치우침이 0.1 미만이면 읽는 법을 적용하지 않는다", failures)
    mid_numbers_synthetic = {"Sk2": {"S2.5": 20.0}}
    cloud_row = cloud_trail_row("x", {"Tx2": 0.5, "Sk2": {"S2.5": 17.0}, "Wk": {"W12.1": 10.0, "W17.6": 4.0}}, mid_numbers_synthetic)
    check(cloud_row["cloud_ok"] is True and cloud_row["trail_ok"] is True and abs(cloud_row["Wk_refill_ratio"] - 0.4) < 1e-12, "구름·이어짐 조건: Tx2>0.45, Sk2≥0.8×중, 비≥0.4, Wk 8~14", failures)
    cloud_row_bad = cloud_trail_row("y", {"Tx2": 0.45, "Sk2": {"S2.5": 15.9}, "Wk": {"W12.1": 15.0, "W17.6": 4.0}}, mid_numbers_synthetic)
    check(cloud_row_bad["cloud_ok"] is False and cloud_row_bad["trail_ok"] is False, "구름·이어짐 조건 경계: Tx2 0.45 는 '보다 큼'이 아니다, Sk2 15.9 < 16, Wk 15 > 14", failures)
    prereg_round2 = (ROOT / "Docs" / "VolumeFog3D" / "stage1" / "prereg-1c-r2.md").read_text(encoding="utf-8")
    check("[0.25 H, 0.75 H) × 가로 [0.25 W, 0.75 W)" in prereg_round2 and "큰 쪽 2% 화소" in prereg_round2 and "Fv ≥ 10 이고 Rs ≥ 12 이며 Rs ≥ 3 × Rn" in prereg_round2 and "0.50 W" in prereg_round2 and "Wk(17.6) ÷ Wk(12.1) 가 0.4 이상이면서 Wk(12.1) 가 8~14" in prereg_round2, "2회차 등록본의 영역·식·기준 문장이 코드 상수와 같은 값이다", failures)
    print("자체 점검", "통과" if not failures else "실패 " + str(len(failures)) + "건")
    return 0 if not failures else EXIT_SOME_INVALID


def parse_arguments(argument_list):
    parser = argparse.ArgumentParser(description="3D 안개 1c 실행 결과 숫자·그림·표")
    parser.add_argument("--runs", nargs="+", help="실행 이름들(Saved/FluidFX/fog3d_calib/<이름>)")
    parser.add_argument("--all", action="store_true", help="결과가 있는 모든 실행(index.json 순서)")
    parser.add_argument("--out-root", default=str(CALIBRATION_ROOT))
    parser.add_argument("--capture-subdirectory", default=CAPTURE_SUBDIRECTORY)
    parser.add_argument("--baseline", default="base_a", help="배수의 기준이 되는 기본값 실행 이름")
    parser.add_argument("--animate", action="store_true", help="실행마다 S·W 움직이는 그림 둘")
    parser.add_argument("--no-images", action="store_true", help="그림을 만들지 않고 숫자·표만")
    parser.add_argument("--compare-rows", type=int, default=8, help="견줌 그림 한 장의 행 수")
    parser.add_argument("--group-compare", action="store_true", help="묶음별 견줌 그림(행 = 기본값 + 그 묶음 실행)도 만든다")
    parser.add_argument("--group-rows", type=int, default=GROUP_COMPARE_ROWS, help="묶음별 견줌 그림 한 장의 행 수(기본값 행 포함)")
    parser.add_argument("--walk-shift", type=float, default=0.0, help="run.json 이 없을 때 쓰는 걷기 미룸(초)")
    parser.add_argument("--round2-tables", metavar="DIR", help="2회차 등록본의 개념 증명·치우침·구름 이어짐 표(.md·.json)를 이 폴더에 쓴다")
    parser.add_argument("--snapshot-log", metavar="NAME", help="촬영 직후 이 실행의 PIE 로그 구간을 보관")
    parser.add_argument("--log", default=str(visible_check.DEFAULT_LOG_PATH))
    parser.add_argument("--live-log", action="store_true", help="보관본이 없을 때 로그의 마지막 PIE 구간을 대신 쓴다(다른 실행과 섞일 수 있다)")
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args(argument_list)


def main(argument_list=None):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    options = parse_arguments(argument_list)
    if options.self_test:
        return run_self_test()
    try:
        if options.round2_tables:
            return write_round2_tables(options)
        if options.snapshot_log:
            run_directory = Path(options.out_root) / options.snapshot_log
            count, has_summary = snapshot_run_log(run_directory, options.log)
            print(f"{options.snapshot_log}: 로그 {count}줄 보관({'요약 줄 있음' if has_summary else '요약 줄 없음 — PIE 가 아직 안 끝났을 수 있다'})")
            return EXIT_ALL_VALID if has_summary else EXIT_SOME_INVALID
        return run_reports(options)
    except (InputProblem, visible_check.InputProblem, OSError) as error:
        print("입력 문제:", error)
        return EXIT_INPUT_PROBLEM


if __name__ == "__main__":
    sys.exit(main())
