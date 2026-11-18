"""실행 환경: 시스템 Python(Pillow 필요, 언리얼 에디터 불필요). 3D 안개 1c 후보 묶음의 촘촘한 촬영(0.1초 또는 0.2초 간격) 점검, 움직이는 그림(애니메이션 WebP)·시간 순 그림 띠(PNG)·숫자 표 만들기, 2회차 그림 일곱 장(탑다운 개념 증명·뒤 카메라·치우침 크롭·구름·이어짐) 만들기.
실행 명령: python -B Tools/FluidFX/fog3d_calib_clips.py check <실행 이름...> | clips --out <폴더> [--camera back high] [--tile-width 640] | numbers --runs <이름=표시이름 ...> --out <폴더> [--note "글"] | round2 --out <폴더> [--tables <표 JSON 폴더>] | --self-test (PowerShell 또는 Bash, 한글 출력은 PYTHONUTF8=1). 촘촘한 실행 이름은 d_<후보>_<카메라>(후보 base weak mid strong, 카메라 back high).
출력: check 는 실행마다 장 수·늦음 최댓값·걷기 끝·로그 요약 표(종료 코드 0 모두 유효 / 1 무효 있음), clips 는 <폴더>/swing_back.webp·swing_high.webp·walk_back.webp·walk_high.webp 와 strip_swing_back.png 등 8개와 clips_report.json, numbers 는 <폴더>/package_numbers.md·.json, round2 는 <폴더>/poc_top_swing·poc_top_walk·poc_back·poc_gridz·asym_crops·cloud_trail_back·cloud_trail_top .png 와 round2_figures.json(종료 코드 0 일곱 장 모두·빠진 실행 없음).
상태: 합성 장으로 --self-test 만 확인(촬영 실물 장은 첫 실행에서 확인). 등록본의 시각표·영역·식은 건드리지 않고 fog3d_calib_report 의 숫자 보고서(report/report.json)를 읽기만 한다.
"""
import argparse
import json
import sys
import tempfile
from pathlib import Path

import numpy
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools"))
sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))
import fog3d_calib_report as report_module  # noqa: E402
import fog3d_calib_variants as variants_module  # noqa: E402

CALIBRATION_ROOT = variants_module.CALIBRATION_ROOT
KOREAN_FONT_PATH = "C:/Windows/Fonts/malgun.ttf"
CANDIDATE_ORDER = ("base", "weak", "mid", "strong")
CANDIDATE_LABELS = {"base": "기본값", "weak": "약", "mid": "중", "strong": "강"}
CAMERA_NAMES = ("back", "high")
GROUP_NAMES = {"swing": "S", "walk": "W"}
STRIP_TIMES = {"swing": (0.6, 1.2, 2.5, 4.0, 6.0), "walk": (0.5, 1.0, 2.5, 4.0, 7.0)}
STRIP_TILE_SIZE = (384, 216)
CLIP_TILE_SIZE = (640, 360)
SMALL_CLIP_TILE_SIZE = (560, 315)
CLIP_FILE_LIMIT_BYTES = 12 * 1024 * 1024
CLIP_QUALITY_STEPS = (95, 85, 70, 55, 40, 25)
FRAME_DURATION_MILLISECONDS = 100
HEADER_HEIGHT = 34
ROW_LABEL_WIDTH = 110
LATE_LIMIT_SECONDS = variants_module.DENSE_LATE_LIMIT_SECONDS
WALK_START_SECONDS = variants_module.DENSE_WALK_START_TIME
EXIT_OK = 0
EXIT_FAILED = 1
EXIT_INPUT_PROBLEM = 2


class InputProblem(Exception):
    pass


def load_korean_font(size):
    try:
        return ImageFont.truetype(KOREAN_FONT_PATH, size)
    except OSError:
        return report_module.load_font(size)


def tenths(value):
    return int(round(float(value) * 10))


def dense_run_name(candidate, camera):
    return f"d_{candidate}_{camera}"


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def load_dense_shots(run_directory):
    run_directory = Path(run_directory)
    capture_directory = run_directory / variants_module.CAPTURE_SUBDIRECTORY
    schedule_status = read_json(report_module.find_schedule_file(capture_directory))
    run_record = read_json(run_directory / "run.json")
    shots = {}
    for entry in run_record["shots"]:
        candidates = [shot for shot in schedule_status.get("shots", []) if abs(float(shot["time"]) - entry["time"]) < 1e-6]
        if not candidates:
            continue
        path = report_module.resolve_shot_path(candidates[0], capture_directory)
        shots[(entry["group"], tenths(entry["time"]))] = {"entry": entry, "shot": candidates[0], "path": path, "exists": path.is_file()}
    return run_record, schedule_status, shots


def check_dense_run(run_directory):
    run_directory = Path(run_directory)
    result = {"name": run_directory.name, "valid": False, "reasons": []}
    try:
        run_record, schedule_status, shots = load_dense_shots(run_directory)
    except (OSError, ValueError, KeyError, report_module.InputProblem) as error:
        result["reasons"].append(f"읽기 오류: {error}")
        return result
    expected = len(run_record["shots"])
    found = sum(1 for entry in shots.values() if entry["exists"])
    result.update({"expected_shots": expected, "found_shots": found, "step_seconds": run_record.get("dense_step_seconds")})
    if found != expected:
        result["reasons"].append(f"장 파일 {found}/{expected}")
    delays = [float(entry["shot"]["captured_game_time"]) - float(entry["entry"]["time"]) for entry in shots.values() if entry["shot"].get("captured_game_time") is not None]
    issue_lates = [float(entry["shot"].get("late_seconds", 0.0)) for entry in shots.values()]
    result["capture_delay_max_seconds"] = max(delays) if delays else None
    result["issue_late_max_seconds"] = max(issue_lates) if issue_lates else None
    result["shots_over_limit"] = sum(1 for value in delays if value > LATE_LIMIT_SECONDS)
    result["late_limit_seconds"] = LATE_LIMIT_SECONDS
    events_done = schedule_status.get("events_done", [])
    result["events_done"] = f"{len(events_done)}/{len(run_record['events'])}"
    if len(events_done) != len(run_record["events"]):
        result["reasons"].append("이벤트가 다 실행되지 않음")
    walk_status = report_module.find_walk_status(run_directory / variants_module.CAPTURE_SUBDIRECTORY, schedule_status, run_directory / variants_module.CAPTURE_SUBDIRECTORY / "result.json")
    if walk_status is None:
        result["reasons"].append("걷기 기록 없음")
    else:
        end_time = float(walk_status["start_game_time"]) + float(walk_status["elapsed_game_seconds"]) - float(schedule_status["reference_game_time"])
        result.update({"walk_start_seconds": float(walk_status["start_game_time"]) - float(schedule_status["reference_game_time"]), "walk_end_seconds": end_time, "walk_moved_cm": walk_status.get("moved_cm")})
        if not walk_status.get("done") or walk_status.get("timed_out") or walk_status.get("error"):
            result["reasons"].append("걷기가 끝나지 않았거나 오류")
        if end_time >= variants_module.DENSE_WALK_LAST_SECONDS:
            result["reasons"].append(f"걷기 끝 {end_time:.2f}초가 마지막 장 안에 끝나지 않음")
    if not schedule_status.get("done") or schedule_status.get("error"):
        result["reasons"].append(f"예약 미완료 또는 오류: {schedule_status.get('error')}")
    log = report_module.read_run_log(run_directory, False, None)
    result["log"] = {key: log.get(key) for key in ("source", "submitted", "rejected", "fog3d_warning_or_error_lines", "usable")}
    if log.get("usable") is False:
        result["reasons"].append("로그: 경고·오류 또는 거부가 있거나 요약 줄이 없다")
    result["valid"] = not result["reasons"]
    return result


def print_check_table(results):
    print("| 실행 | 장 | 간격 | 늦음 최댓값(초) | 한도 초과 장 | 걷기 끝(초) | 이벤트 | 제출/거부 | Fog3D 경고 | 유효 |")
    print("|---|---|---|---|---|---|---|---|---|---|")
    for result in results:
        log = result.get("log", {})
        delay = result.get("capture_delay_max_seconds")
        walk_end = result.get("walk_end_seconds")
        print(f"| {result['name']} | {result.get('found_shots')}/{result.get('expected_shots')} | {result.get('step_seconds')} | {delay:.3f} | {result.get('shots_over_limit')} | {walk_end:.2f} | {result.get('events_done')} | {log.get('submitted')}/{log.get('rejected')} | {log.get('fog3d_warning_or_error_lines')} | {'예' if result['valid'] else '아니오: ' + '; '.join(result['reasons'])} |"
              if delay is not None and walk_end is not None else f"| {result['name']} | 읽기 실패: {'; '.join(result['reasons'])} |")


def fit_tile(path, size):
    image = Image.open(path).convert("RGB")
    return image.resize(size, Image.LANCZOS)


def draw_badge(image, text, font, corner):
    draw = ImageDraw.Draw(image)
    left, top, right, bottom = draw.textbbox((0, 0), text, font=font)
    width, height = right - left + 12, bottom - top + 10
    x0 = 0 if corner == "left" else image.width - width
    draw.rectangle((x0, 0, x0 + width, height), fill=(0, 0, 0))
    draw.text((x0 + 6 - left, 4 - top), text, fill=(255, 255, 255), font=font)


def group_times(shots, group):
    return sorted(key[1] for key in shots if key[0] == group)


def time_label(group_key, tenth_value):
    seconds = tenth_value / 10.0
    if group_key == "W":
        seconds = seconds - WALK_START_SECONDS
    return f"{max(seconds, 0.0):.1f}초"


def build_clip_frames(candidate_shots, group_key, tile_size, labels):
    reference_times = group_times(candidate_shots[CANDIDATE_ORDER[0]], group_key)
    for candidate in CANDIDATE_ORDER:
        if group_times(candidate_shots[candidate], group_key) != reference_times:
            raise InputProblem(f"{candidate} 의 {group_key} 장 시각이 다른 후보와 다르다")
    name_font = load_korean_font(max(18, tile_size[1] // 14))
    time_font = load_korean_font(max(16, tile_size[1] // 16))
    frames = []
    for tenth_value in reference_times:
        canvas = Image.new("RGB", (tile_size[0] * 2, tile_size[1] * 2))
        for index, candidate in enumerate(CANDIDATE_ORDER):
            tile = fit_tile(candidate_shots[candidate][(group_key, tenth_value)]["path"], tile_size)
            draw_badge(tile, labels[candidate], name_font, "left")
            draw_badge(tile, time_label(group_key, tenth_value), time_font, "right")
            canvas.paste(tile, ((index % 2) * tile_size[0], (index // 2) * tile_size[1]))
        frames.append(canvas)
    return frames


def save_animation(frames, path, quality):
    frames[0].save(path, format="WEBP", save_all=True, append_images=frames[1:], duration=FRAME_DURATION_MILLISECONDS, loop=0, quality=quality, method=4)


def write_clip_within_limit(candidate_shots, group_key, output_path, labels, tile_sizes=(CLIP_TILE_SIZE, SMALL_CLIP_TILE_SIZE)):
    attempts = []
    for tile_size in tile_sizes:
        frames = build_clip_frames(candidate_shots, group_key, tile_size, labels)
        for quality in CLIP_QUALITY_STEPS:
            save_animation(frames, output_path, quality)
            size = Path(output_path).stat().st_size
            attempts.append({"tile": list(tile_size), "quality": quality, "bytes": size})
            if size <= CLIP_FILE_LIMIT_BYTES:
                return {"tile": list(tile_size), "quality": quality, "bytes": size, "frames": len(frames), "attempts": attempts}
    return {"tile": list(tile_sizes[-1]), "quality": CLIP_QUALITY_STEPS[-1], "bytes": Path(output_path).stat().st_size, "frames": len(frames), "attempts": attempts, "over_limit": True}


def verify_animation(path):
    with Image.open(path) as image:
        count = getattr(image, "n_frames", 1)
        total = 0
        for index in range(count):
            image.seek(index)
            image.load()
            total += image.info.get("duration", 0)
        return {"frames": count, "size": list(image.size), "total_milliseconds": total}


def nearest_time(available, target_seconds, group_key):
    target_tenths = tenths(target_seconds + (WALK_START_SECONDS if group_key == "W" else 0.0))
    return min(available, key=lambda value: abs(value - target_tenths))


def build_strip(candidate_shots, group_key, output_path, labels):
    column_targets = STRIP_TIMES["swing" if group_key == "S" else "walk"]
    available = group_times(candidate_shots[CANDIDATE_ORDER[0]], group_key)
    chosen = [nearest_time(available, target, group_key) for target in column_targets]
    width = ROW_LABEL_WIDTH + STRIP_TILE_SIZE[0] * len(chosen)
    height = HEADER_HEIGHT + STRIP_TILE_SIZE[1] * len(CANDIDATE_ORDER)
    canvas = Image.new("RGB", (width, height), (20, 20, 20))
    draw = ImageDraw.Draw(canvas)
    font = load_korean_font(20)
    for column, tenth_value in enumerate(chosen):
        draw.text((ROW_LABEL_WIDTH + column * STRIP_TILE_SIZE[0] + 8, 6), time_label(group_key, tenth_value), fill=(255, 255, 255), font=font)
    for row, candidate in enumerate(CANDIDATE_ORDER):
        top = HEADER_HEIGHT + row * STRIP_TILE_SIZE[1]
        draw.text((10, top + STRIP_TILE_SIZE[1] // 2 - 12), labels[candidate], fill=(255, 255, 255), font=font)
        for column, tenth_value in enumerate(chosen):
            canvas.paste(fit_tile(candidate_shots[candidate][(group_key, tenth_value)]["path"], STRIP_TILE_SIZE), (ROW_LABEL_WIDTH + column * STRIP_TILE_SIZE[0], top))
    canvas.save(output_path)
    return {"columns_tenths": chosen, "size": [width, height]}


def run_clips(arguments):
    output_directory = Path(arguments.out)
    output_directory.mkdir(parents=True, exist_ok=True)
    report = {"files": {}}
    for camera in arguments.camera:
        candidate_shots = {}
        for candidate in CANDIDATE_ORDER:
            run_directory = Path(arguments.root) / dense_run_name(candidate, camera)
            _, _, shots = load_dense_shots(run_directory)
            missing = [key for key, entry in shots.items() if not entry["exists"]]
            if missing:
                raise InputProblem(f"{run_directory.name}: 장 파일 없음 {len(missing)}개")
            candidate_shots[candidate] = shots
        for group_name, group_key in GROUP_NAMES.items():
            clip_path = output_directory / f"{group_name}_{camera}.webp"
            entry = write_clip_within_limit(candidate_shots, group_key, clip_path, CANDIDATE_LABELS)
            entry["verify"] = verify_animation(clip_path)
            report["files"][clip_path.name] = entry
            strip_path = output_directory / f"strip_{group_name}_{camera}.png"
            strip_entry = build_strip(candidate_shots, group_key, strip_path, CANDIDATE_LABELS)
            strip_entry["bytes"] = strip_path.stat().st_size
            report["files"][strip_path.name] = strip_entry
            print(clip_path.name, entry["bytes"], entry["verify"], "| ", strip_path.name, strip_entry["size"])
    (output_directory / "clips_report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    return EXIT_OK


def format_number(value):
    return "-" if value is None else f"{value:.2f}"


def collect_number_row(name, display_name, root):
    report = read_json(Path(root) / name / "report" / "report.json")
    numbers = report["numbers"]
    if numbers is None:
        raise InputProblem(f"{name}: 숫자 없음(장 파일 부족)")
    log = report["log"]
    return {"name": name, "display_name": display_name, "valid": report["valid"], "Wk": numbers["Wk"], "Sk": numbers["Sk"], "Sk2_2.5": numbers["Sk2"]["S2.5"], "Tx": numbers["Tx"], "Tx2": numbers["Tx2"],
            "N_W": numbers["noise_walk_N_W"], "N_S": numbers["noise_swing_N_S"], "refill_ratio_swing": numbers["refill_ratio_swing"], "refill_ratio_walk": numbers["refill_ratio_walk"],
            "log": {"submitted": log.get("submitted"), "rejected": log.get("rejected"), "fog3d_warning_or_error_lines": log.get("fog3d_warning_or_error_lines"), "usable": log.get("usable")}}


def build_numbers_markdown(rows, note):
    wk_ids = list(rows[0]["Wk"])
    sk_ids = list(rows[0]["Sk"])
    lines = ["# 1c 후보 묶음 숫자 (옆빛·뒤 카메라, 등록본 시각표, 8비트 밝기 단위)", ""]
    if note:
        lines += [note, ""]
    lines += ["| 실행(표시) | 유효 | " + " | ".join(f"Wk({value[1:]})" for value in wk_ids) + " | " + " | ".join(f"Sk({value[1:]})" for value in sk_ids) + " | Sk2(2.5) | Tx | Tx2 | N_W | N_S | 되차오름 S | 되차오름 W | 제출/거부 | Fog3D 경고 |",
              "|" + "---|" * (2 + len(wk_ids) + len(sk_ids) + 9)]
    for row in rows:
        lines.append("| " + f"{row['name']} ({row['display_name']})" + " | " + ("예" if row["valid"] else "아니오") + " | " + " | ".join(format_number(row["Wk"][value]) for value in wk_ids) + " | " + " | ".join(format_number(row["Sk"][value]) for value in sk_ids)
                     + f" | {format_number(row['Sk2_2.5'])} | {format_number(row['Tx'])} | {format_number(row['Tx2'])} | {format_number(row['N_W'])} | {format_number(row['N_S'])} | {format_number(row['refill_ratio_swing'])} | {format_number(row['refill_ratio_walk'])} | {row['log']['submitted']}/{row['log']['rejected']} | {row['log']['fog3d_warning_or_error_lines']} |")
    return "\n".join(lines) + "\n"


def run_numbers(arguments):
    rows = []
    for item in arguments.runs:
        name, _, display_name = item.partition("=")
        rows.append(collect_number_row(name, display_name or name, arguments.root))
    output_directory = Path(arguments.out)
    output_directory.mkdir(parents=True, exist_ok=True)
    (output_directory / "package_numbers.json").write_text(json.dumps({"note": arguments.note, "rows": rows}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    (output_directory / "package_numbers.md").write_text(build_numbers_markdown(rows, arguments.note), encoding="utf-8", newline="\n")
    print("숫자 표", (output_directory / "package_numbers.md").as_posix())
    return EXIT_OK


def run_check(arguments):
    results = [check_dense_run(Path(arguments.root) / name) for name in arguments.runs]
    print_check_table(results)
    if arguments.json:
        Path(arguments.json).write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    return EXIT_OK if all(result["valid"] for result in results) else EXIT_FAILED


ROUND2_TILE_SIZE = (320, 180)
ROUND2_WIDE_TILE_SIZE = (480, 270)
ROUND2_CLOUD_TOP_TILE_SIZE = (400, 225)
ROUND2_HEADER_HEIGHT = 26
ROUND2_GRID_BACKGROUND = (24, 24, 24)
ROUND2_DIFFERENCE_GAIN_GRID = 4.0
ROUND2_DIFFERENCE_GAIN_CROPS = 8.0
ROUND2_LUMA_WEIGHTS = (0.2126, 0.7152, 0.0722)
ROUND2_POC_SWING_COLUMNS = (("S 기준 (S8.3)", "S8.3"), ("S 1.2초", "S1.2"), ("S 2.5초", "S2.5"), ("S 5.0초", "S5.0"))
ROUND2_POC_WALK_COLUMNS = (("W 기준 (W19.4)", "W19.4"), ("W 10.8초", "W10.8"), ("W 12.1초", "W12.1"), ("W 15.6초", "W15.6"))
ROUND2_THREE_COLUMNS = (("S 기준 (S8.3)", "S8.3"), ("S 2.5초", "S2.5"), ("W 12.1초", "W12.1"))
ROUND2_CLOUD_BACK_COLUMNS = (("S 기준 (S8.3)", "S8.3"), ("S 2.5초", "S2.5"), ("W 12.1초", "W12.1"), ("W 17.6초", "W17.6"))
ROUND2_CLOUD_TOP_COLUMNS = (("S 기준 (S8.3)", "S8.3"), ("S 2.5초", "S2.5"), ("W 12.1초", "W12.1"), ("W 15.6초", "W15.6"))
ROUND2_POC_ROWS = ("T0", "T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8")
ROUND2_BACK_ROWS = ("T4_back", "T7_back", "base_d")
ROUND2_GRIDZ_ROWS = ("T7", "T7_gridz")
ROUND2_ASYMMETRY_ROWS = ("A0", "A1", "A2", "A3", "A4")
ROUND2_CLOUD_BACK_ROWS = ("s3", "C1", "C2", "C3", "C4", "R1", "R2")
ROUND2_CLOUD_TOP_ROWS = ("C1_top", "C3_top", "R1_top")
ROUND2_FIGURE_FILES = ("poc_top_swing.png", "poc_top_walk.png", "poc_back.png", "poc_gridz.png", "asym_crops.png", "cloud_trail_back.png", "cloud_trail_top.png")


def load_round2_shot_paths(name, root):
    directory = Path(root) / name
    run_file = directory / "run.json"
    capture_directory = directory / variants_module.CAPTURE_SUBDIRECTORY
    if not run_file.is_file() or not capture_directory.is_dir():
        return None
    matched = report_module.match_shots(read_json(run_file), read_json(report_module.find_schedule_file(capture_directory)), capture_directory)
    return {identifier: entry["path"] for identifier, entry in matched.items() if entry["exists"]}


def round2_luma(path):
    rgb = numpy.asarray(Image.open(path).convert("RGB"), dtype=numpy.float64)
    return (rgb @ numpy.array(ROUND2_LUMA_WEIGHTS)) / 255.0


def round2_difference_image(path_first, path_second, gain):
    difference = numpy.abs(round2_luma(path_first) - round2_luma(path_second)) * gain
    gray = numpy.clip(difference * 255.0, 0, 255).astype(numpy.uint8)
    return Image.fromarray(numpy.stack([gray] * 3, axis=-1))


def write_round2_grid(rows, column_titles, tile_size, output_path):
    tile_width, tile_height = tile_size
    canvas = Image.new("RGB", (tile_width * len(column_titles), ROUND2_HEADER_HEIGHT + tile_height * len(rows)), ROUND2_GRID_BACKGROUND)
    draw = ImageDraw.Draw(canvas)
    header_font = load_korean_font(14)
    label_font = load_korean_font(13)
    for column, title in enumerate(column_titles):
        draw.text((column * tile_width + 6, 4), title, fill=(255, 255, 255), font=header_font)
    for row_index, row in enumerate(rows):
        top = ROUND2_HEADER_HEIGHT + row_index * tile_height
        for column, tile in enumerate(row["tiles"]):
            canvas.paste(tile.convert("RGB").resize(tile_size, Image.LANCZOS), (column * tile_width, top))
        for line_index, line in enumerate((row["label"], row["detail"])):
            if not line:
                continue
            left, line_top, right, bottom = draw.textbbox((0, 0), line, font=label_font)
            y = top + 2 + line_index * (bottom - line_top + 6)
            draw.rectangle((0, y, right - left + 10, y + bottom - line_top + 4), fill=(0, 0, 0))
            draw.text((4 - left, y + 2 - line_top), line, fill=(255, 255, 255), font=label_font)
    canvas.save(output_path)
    return list(canvas.size)


def round2_tile_row(paths, columns, difference_pair=None, gain=ROUND2_DIFFERENCE_GAIN_GRID):
    tiles = [Image.open(paths[identifier]).convert("RGB") for _, identifier in columns]
    if difference_pair is not None:
        tiles.append(round2_difference_image(paths[difference_pair[0]], paths[difference_pair[1]], gain))
    return tiles


def round2_rows_for(names, root, columns, detail_for, difference_pair=None, gain=ROUND2_DIFFERENCE_GAIN_GRID):
    rows, missing = [], []
    for name in names:
        paths = load_round2_shot_paths(name, root)
        needed = [identifier for _, identifier in columns] + list(difference_pair or ())
        if paths is None or any(identifier not in paths for identifier in needed):
            missing.append(name)
            continue
        rows.append({"label": name, "detail": detail_for(name), "tiles": round2_tile_row(paths, columns, difference_pair, gain)})
    return rows, missing


def table_rows_by_name(tables_directory, file_name, key="rows"):
    path = Path(tables_directory) / file_name
    if not path.is_file():
        return {}
    return {row["name"]: row for row in read_json(path)[key] if "missing" not in row}


def round2_figure_poc(tables_directory, root, out_directory):
    poc_rows = table_rows_by_name(tables_directory, "poc_top.json")
    swing_detail = lambda name: f"Fv {poc_rows[name]['Fv']:.1f}  Rs {poc_rows[name]['Rs']:.1f}  Rn {poc_rows[name]['Rn']:.2f}  Rs÷Rn {poc_rows[name]['Rs_over_Rn']:.1f}" if name in poc_rows and poc_rows[name]["Rs_over_Rn"] is not None else ""
    walk_detail = lambda name: f"Rw {poc_rows[name]['Rw']:.1f}  (걷기 잡음 {poc_rows[name]['Rn_walk']:.2f})" if name in poc_rows else ""
    swing_columns = ROUND2_POC_SWING_COLUMNS
    walk_columns = ROUND2_POC_WALK_COLUMNS
    swing_rows, swing_missing = round2_rows_for(ROUND2_POC_ROWS, root, swing_columns, swing_detail, ("S2.5", "S8.3"))
    walk_rows, walk_missing = round2_rows_for(ROUND2_POC_ROWS, root, walk_columns, walk_detail, ("W12.1", "W19.4"))
    written = {}
    if swing_rows:
        written["poc_top_swing.png"] = write_round2_grid(swing_rows, [title for title, _ in swing_columns] + ["|S 2.5초 - 기준| x 4"], ROUND2_TILE_SIZE, Path(out_directory) / "poc_top_swing.png")
    if walk_rows:
        written["poc_top_walk.png"] = write_round2_grid(walk_rows, [title for title, _ in walk_columns] + ["|W 12.1초 - 기준| x 4"], ROUND2_TILE_SIZE, Path(out_directory) / "poc_top_walk.png")
    return written, sorted(set(swing_missing + walk_missing))


def round2_figure_poc_back(tables_directory, root, out_directory):
    back_rows = table_rows_by_name(tables_directory, "poc_top.json", "back_rows")
    detail = lambda name: f"Wk {back_rows[name]['Wk_12.1']:.2f}  Sk {back_rows[name]['Sk_2.5']:.2f}  Sk2 {back_rows[name]['Sk2_2.5']:.2f}" if name in back_rows else ""
    written, missing = {}, []
    rows, absent = round2_rows_for(ROUND2_BACK_ROWS, root, ROUND2_THREE_COLUMNS, detail)
    missing += absent
    if rows:
        written["poc_back.png"] = write_round2_grid(rows, [title for title, _ in ROUND2_THREE_COLUMNS], ROUND2_WIDE_TILE_SIZE, Path(out_directory) / "poc_back.png")
    poc_rows = table_rows_by_name(tables_directory, "poc_top.json")
    gridz_detail = lambda name: f"Fv {poc_rows[name]['Fv']:.1f}  Rs {poc_rows[name]['Rs']:.1f}  Rn {poc_rows[name]['Rn']:.2f}" if name in poc_rows else ""
    rows, absent = round2_rows_for(ROUND2_GRIDZ_ROWS, root, ROUND2_THREE_COLUMNS, gridz_detail)
    missing += absent
    if rows:
        written["poc_gridz.png"] = write_round2_grid(rows, [title for title, _ in ROUND2_THREE_COLUMNS], ROUND2_WIDE_TILE_SIZE, Path(out_directory) / "poc_gridz.png")
    return written, sorted(set(missing))


def round2_figure_asymmetry(tables_directory, root, out_directory):
    asymmetry_rows = table_rows_by_name(tables_directory, "asym.json")
    pieces, missing = [], []
    for name in ROUND2_ASYMMETRY_ROWS:
        paths = load_round2_shot_paths(name, root)
        if paths is None or "W12.1" not in paths or "W19.4" not in paths:
            missing.append(name)
            continue
        stopped = Image.open(paths["W12.1"]).convert("RGB")
        width, height = stopped.size
        top, bottom, left, right = report_module.walk_region(height, width)
        split = asymmetry_rows[name]["split_column"] - left if name in asymmetry_rows else (right - left) // 2
        difference = round2_difference_image(paths["W12.1"], paths["W19.4"], ROUND2_DIFFERENCE_GAIN_CROPS)
        crops = [stopped.crop((left, top, right, bottom)), difference.crop((left, top, right, bottom))]
        for crop in crops:
            ImageDraw.Draw(crop).line((split, 0, split, 7), fill=(255, 64, 64), width=2)
        detail = f"Wk_L {asymmetry_rows[name]['Wk_L']:.2f}  Wk_R {asymmetry_rows[name]['Wk_R']:.2f}  치우침 {asymmetry_rows[name]['asymmetry']:+.3f}" if name in asymmetry_rows else ""
        pieces.append({"label": name, "detail": detail, "crops": crops})
    if not pieces:
        return {}, missing
    crop_width, crop_height = pieces[0]["crops"][0].size
    rows = [{"label": piece["label"], "detail": piece["detail"], "tiles": piece["crops"]} for piece in pieces]
    path = Path(out_directory) / "asym_crops.png"
    return {"asym_crops.png": write_round2_grid(rows, ["W 12.1초 장의 걷기 영역 1:1 (빨간 표시 = 좌우 가름선)", "|W 12.1초 - W 기준| x 8"], (crop_width, crop_height), path)}, missing


def round2_figure_cloud(tables_directory, root, out_directory):
    cloud_rows = table_rows_by_name(tables_directory, "cloud_trail.json")
    back_detail = lambda name: f"Tx2 {cloud_rows[name]['Tx2']:.3f}  Sk2 {cloud_rows[name]['Sk2_2.5']:.1f}  Wk {cloud_rows[name]['Wk_12.1']:.1f}→{cloud_rows[name]['Wk_17.6']:.1f}" if name in cloud_rows else ""
    written, missing = {}, []
    rows, absent = round2_rows_for(ROUND2_CLOUD_BACK_ROWS, root, ROUND2_CLOUD_BACK_COLUMNS, back_detail)
    missing += absent
    if rows:
        written["cloud_trail_back.png"] = write_round2_grid(rows, [title for title, _ in ROUND2_CLOUD_BACK_COLUMNS], ROUND2_TILE_SIZE, Path(out_directory) / "cloud_trail_back.png")
    top_detail = lambda name: (read_json(Path(root) / name / "run.json").get("description", "") if (Path(root) / name / "run.json").is_file() else "")
    rows, absent = round2_rows_for(ROUND2_CLOUD_TOP_ROWS, root, ROUND2_CLOUD_TOP_COLUMNS, top_detail)
    missing += absent
    if rows:
        written["cloud_trail_top.png"] = write_round2_grid(rows, [title for title, _ in ROUND2_CLOUD_TOP_COLUMNS], ROUND2_CLOUD_TOP_TILE_SIZE, Path(out_directory) / "cloud_trail_top.png")
    return written, sorted(set(missing))


def run_round2(arguments):
    out_directory = Path(arguments.out)
    out_directory.mkdir(parents=True, exist_ok=True)
    tables_directory = Path(arguments.tables) if arguments.tables else out_directory
    written, missing = {}, []
    for figure in (round2_figure_poc, round2_figure_poc_back, round2_figure_asymmetry, round2_figure_cloud):
        figure_written, figure_missing = figure(tables_directory, arguments.root, out_directory)
        written.update(figure_written)
        missing += figure_missing
    report = {"files": written, "missing_runs": sorted(set(missing))}
    (out_directory / "round2_figures.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    for name, size in written.items():
        print(f"{name}: {size[0]}x{size[1]}")
    print("빠진 실행:", ", ".join(report["missing_runs"]) or "-")
    return EXIT_OK if len(written) == len(ROUND2_FIGURE_FILES) and not report["missing_runs"] else EXIT_FAILED


def check(condition, label, failures):
    if not condition:
        failures.append(label)
    print(("통과 " if condition else "실패 ") + label)


def make_fake_run(root, candidate, camera, step, late=0.0):
    name = dense_run_name(candidate, camera)
    run_directory = Path(root) / name
    capture_directory = run_directory / variants_module.CAPTURE_SUBDIRECTORY
    capture_directory.mkdir(parents=True)
    table = variants_module.build_dense_shot_table(step)
    events = variants_module.build_dense_event_records(2, [])
    (run_directory / "run.json").write_text(json.dumps({"name": name, "dense_step_seconds": step, "shots": table, "events": events, "valid_conditions": {"dense": True}}), encoding="utf-8")
    shots = []
    shade = CANDIDATE_ORDER.index(candidate) * 40 + 20
    for entry in table:
        path = capture_directory / f"00_c_{int(round(entry['time'] * 1000)):05d}ms.png"
        Image.new("RGBA", (320, 180), (shade, shade + int(entry["time"] * 5), 90, 255)).save(path)
        shots.append({"time": entry["time"], "path": path.as_posix(), "issued_game_time": entry["time"], "late_seconds": 0.0, "captured_game_time": entry["time"] + 1 / 60 + late, "saved": True})
    status = {"times": [entry["time"] for entry in table], "reference": "now", "reference_game_time": 10.0, "prefix": "c", "shots": shots,
              "events_done": [{"time": record["time"], "game_time": 10.0 + record["time"]} for record in events], "done": True, "error": None}
    (capture_directory / "00_c_schedule.json").write_text(json.dumps(status), encoding="utf-8")
    (capture_directory / "00_event07300ms_walk.json").write_text(json.dumps({"start_game_time": 17.3, "elapsed_game_seconds": 1.2, "moved_cm": 607.0, "timed_out": False, "done": True}), encoding="utf-8")
    log_lines = ["LogTDFog3D: Fog3D zone summary: submitted=100 rejected=0"]
    (run_directory / report_module.LOG_SNAPSHOT_FILE_NAME).write_text("\n".join(log_lines) + "\n", encoding="utf-8")
    return run_directory


def run_self_test():
    failures = []
    with tempfile.TemporaryDirectory() as directory:
        for step in (0.1, 0.2):
            sub = Path(directory) / f"step{int(step * 10)}"
            sub.mkdir()
            for candidate in CANDIDATE_ORDER:
                for camera in CAMERA_NAMES:
                    make_fake_run(sub, candidate, camera, step, late=0.0 if step == 0.1 else 0.07)
            result = check_dense_run(sub / "d_base_back")
            expected_count = 132 if step == 0.1 else 67
            check(result.get("found_shots") == expected_count and result.get("expected_shots") == expected_count, f"간격 {step}: 장 수 {expected_count}", failures)
            check(result.get("events_done") == "4/4" and abs(result.get("walk_end_seconds", 0) - 8.5) < 1e-6, f"간격 {step}: 이벤트 4/4, 걷기 끝 8.5초", failures)
            check((result.get("shots_over_limit") == 0) == (step == 0.1), f"간격 {step}: 한도 초과 장 판별", failures)
        sub = Path(directory) / "step1"
        output = Path(directory) / "out"
        arguments = argparse.Namespace(out=str(output), camera=["back", "high"], root=str(sub))
        check(run_clips(arguments) == EXIT_OK, "움직이는 그림·띠 만들기", failures)
        report = read_json(output / "clips_report.json")
        check(sorted(report["files"]) == sorted([f"{group}_{camera}.webp" for group in GROUP_NAMES for camera in CAMERA_NAMES] + [f"strip_{group}_{camera}.png" for group in GROUP_NAMES for camera in CAMERA_NAMES]), "파일 8개 이름", failures)
        swing_verify = report["files"]["swing_back.webp"]["verify"]
        walk_verify = report["files"]["walk_back.webp"]["verify"]
        check(swing_verify["frames"] == 61 and walk_verify["frames"] == 71 and swing_verify["size"] == [1280, 720] and swing_verify["total_milliseconds"] == 6100, "swing 61장·walk 71장·1280x720·장마다 0.1초", failures)
        strip_size = Image.open(output / "strip_swing_back.png").size
        check(strip_size == (ROW_LABEL_WIDTH + 384 * 5, HEADER_HEIGHT + 216 * 4), "그림 띠 크기(칸 384x216, 5열 4행)", failures)
        check(report["files"]["strip_walk_back.png"]["columns_tenths"] == [78, 83, 98, 113, 143], "걷기 띠 시각 7.8 8.3 9.8 11.3 14.3", failures)
        check(abs(Image.open(output / "swing_back.webp").size[0] - 1280) == 0, "그림 열기", failures)
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory) / "runs"
        for name in ("T0", "T1", "A0", "A1", "T4_back"):
            report_module.build_synthetic_capture(root / name, 11.0, width=640, height=360)
            (root / name / "run.json").write_text(json.dumps({"name": name, "description": "합성 " + name, "shots": variants_module.build_shot_table(0.0)}), encoding="utf-8")
        tables = Path(directory) / "tables"
        tables.mkdir()
        (tables / "poc_top.json").write_text(json.dumps({"rows": [{"name": "T0", "Fv": 12.0, "Rs": 15.0, "Rn": 0.5, "Rs_over_Rn": 30.0, "Rw": 3.0, "Rn_walk": 0.4}, {"name": "T1", "missing": "없음"}],
                                                         "back_rows": [{"name": "T4_back", "Wk_12.1": 5.0, "Sk_2.5": 6.0, "Sk2_2.5": 7.0}]}), encoding="utf-8")
        (tables / "asym.json").write_text(json.dumps({"rows": [{"name": "A0", "Wk_L": 9.0, "Wk_R": 3.0, "asymmetry": 0.5, "split_column": 320}]}), encoding="utf-8")
        out = Path(directory) / "figures"
        round2_arguments = argparse.Namespace(out=str(out), tables=str(tables), root=str(root))
        check(run_round2(round2_arguments) == EXIT_FAILED, "2회차 그림: 빠진 실행이 있으면 종료 코드 1", failures)
        figures = read_json(out / "round2_figures.json")
        check(figures["files"]["poc_top_swing.png"] == [5 * 320, ROUND2_HEADER_HEIGHT + 2 * 180] and figures["files"]["poc_top_walk.png"] == [5 * 320, ROUND2_HEADER_HEIGHT + 2 * 180], "개념 증명 그림: 칸 320x180, 5열(기준·셋·차이), 있는 행(T0·T1) 2개", failures)
        check(figures["files"]["poc_back.png"] == [3 * 480, ROUND2_HEADER_HEIGHT + 270], "뒤 카메라 견줌: 칸 480x270 3열 1행(T4_back 만 있음)", failures)
        walk_top, walk_bottom, walk_left, walk_right = report_module.walk_region(360, 640)
        check(figures["files"]["asym_crops.png"] == [2 * (walk_right - walk_left), ROUND2_HEADER_HEIGHT + 2 * (walk_bottom - walk_top)], "치우침 크롭: 걷기 영역 1:1 두 열(장·차이×8) 2행(A0·A1)", failures)
        expected_missing = ["A2", "A3", "A4", "C1", "C1_top", "C2", "C3", "C3_top", "C4", "R1", "R1_top", "R2", "T2", "T3", "T4", "T5", "T6", "T7", "T7_back", "T7_gridz", "T8", "base_d", "s3"]
        check("cloud_trail_back.png" not in figures["files"] and sorted(figures["missing_runs"]) == expected_missing, "빠진 실행 목록을 기록하고 그림을 못 만든 쪽은 파일에서 뺀다", failures)
        difference_image = round2_difference_image(root / "T0" / "capture" / "00_c_02500ms.png", root / "T0" / "capture" / "00_c_08300ms.png", 4.0)
        check(difference_image.size == (640, 360) and int(numpy.asarray(difference_image)[300, 400, 0]) == 32, "차이 그림: 합성 장 밝기 차이 8/255 를 4배로 키워 32", failures)
    print("자체 점검", "통과" if not failures else "실패 " + str(len(failures)) + "건")
    return EXIT_OK if not failures else EXIT_FAILED


def parse_arguments(argument_list):
    parser = argparse.ArgumentParser(description="3D 안개 1c 촘촘한 촬영 점검·그림·숫자 표")
    parser.add_argument("--self-test", action="store_true")
    sub = parser.add_subparsers(dest="command")
    check_parser = sub.add_parser("check")
    check_parser.add_argument("runs", nargs="+")
    check_parser.add_argument("--root", default=str(CALIBRATION_ROOT))
    check_parser.add_argument("--json")
    clips_parser = sub.add_parser("clips")
    clips_parser.add_argument("--out", required=True)
    clips_parser.add_argument("--camera", nargs="+", default=list(CAMERA_NAMES), choices=list(CAMERA_NAMES))
    clips_parser.add_argument("--root", default=str(CALIBRATION_ROOT))
    round2_parser = sub.add_parser("round2")
    round2_parser.add_argument("--out", required=True)
    round2_parser.add_argument("--tables", default="")
    round2_parser.add_argument("--root", default=str(CALIBRATION_ROOT))
    numbers_parser = sub.add_parser("numbers")
    numbers_parser.add_argument("--runs", nargs="+", required=True, help="실행이름=표시이름")
    numbers_parser.add_argument("--out", required=True)
    numbers_parser.add_argument("--note", default="")
    numbers_parser.add_argument("--root", default=str(CALIBRATION_ROOT))
    return parser.parse_args(argument_list)


def main(argument_list=None):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    arguments = parse_arguments(argument_list)
    if arguments.self_test:
        return run_self_test()
    try:
        if arguments.command == "check":
            return run_check(arguments)
        if arguments.command == "clips":
            return run_clips(arguments)
        if arguments.command == "numbers":
            return run_numbers(arguments)
        if arguments.command == "round2":
            return run_round2(arguments)
    except (InputProblem, OSError, KeyError, ValueError, report_module.InputProblem) as error:
        print("입력 문제:", error)
        return EXIT_INPUT_PROBLEM
    print("하위 명령(check|clips|numbers|round2) 또는 --self-test 가 필요하다")
    return EXIT_INPUT_PROBLEM


if __name__ == "__main__":
    sys.exit(main())
