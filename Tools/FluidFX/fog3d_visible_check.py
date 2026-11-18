"""실행 환경: 시스템 Python(numpy·Pillow 필요, 언리얼 에디터 불필요). 3D 안개 1b 통과 기준 B8(prereg-1b.md)을 PIE 촬영 여덟 장과 로그에서 계산한다. 밝기·영역·합격선 정의는 촬영 전에 감독이 정한 값이라 고치지 않는다.
실행 명령: python Tools/FluidFX/fog3d_visible_check.py --result <pie_lookdev_fluid.py 결과 폴더 또는 result.json> [--log Saved/Logs/TDGame.log] [--log-from <줄 번호 또는 표식 문자열>] [--output-dir <폴더>] (여덟 장을 직접 줄 때는 --hidden-a ... --mode0-b), 점검만: --self-test, 변형 JSON 키만: --check-variants <JSON...>
출력: <output-dir>/fog3d_visible_check.json(값·잡음·비·통과 여부·입력 sha256), fog3d_diff_zone.png·fog3d_diff_reaction.png(차이를 16배 키운 그림, 정보용), 종료 코드 0 모두 통과 / 1 하나라도 실패 / 2 입력 문제(그림 크기 다름·파일 없음·결과의 saved 거짓·로그 시작 표식 없음).
상태: 에디터 실행 전: 미검증(합성 그림·합성 로그 --self-test 만. 실제 PIE 촬영 그림과 실제 로그에는 아직 돌리지 않았다).
"""
import argparse
import hashlib
import json
import math
import re
import sys
import tempfile
from pathlib import Path

import numpy
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
IMAGE_NAMES = ("hidden_a", "hidden_b", "shown_a", "shown_b", "mode2_a", "mode2_b", "mode0_a", "mode0_b")
WALK_VARIANT_NAME = "walk"
LUMA_WEIGHTS = (0.2126, 0.7152, 0.0722)
ZONE_ROW_FRACTIONS = (0.40, 1.00)
PAWN_ROW_FRACTIONS = (0.30, 0.95)
PAWN_COLUMN_FRACTIONS = (0.25, 0.75)
NOISE_RATIO_MINIMUM = 3.0
DIFFERENCE_MINIMUM = 1.0 / 255.0
DIFFERENCE_GAIN = 16.0
DEFAULT_LOG_PATH = ROOT / "Saved" / "Logs" / "TDGame.log"
DEFAULT_OUTPUT_DIRECTORY = ROOT / "Saved" / "FluidFX" / "lookdev" / "fog3d_visible_check"
PIE_START_MARKER = "LogPlayLevel: PIE: Created PIE world by copying editor world"
SUMMARY_PATTERN = re.compile(r"Fog3D zone summary:\s*submitted=(\d+)\s+rejected=(\d+)")
LEVEL_PATTERN = re.compile(r"\b(?:Warning|Error):")
REPORT_FILE_NAME = "fog3d_visible_check.json"
EXIT_PASSED = 0
EXIT_FAILED = 1
EXIT_INPUT_PROBLEM = 2
EXAMPLE_LINE_LIMIT = 5
EXAMPLE_LINE_LENGTH = 300
LOOKDEV_VARIANT_FILES = (("fog3d_lookdev_1b.json", True), ("fog3d_lookdev_1b_walk.json", False))
STAGE_ZONE_LABEL = "TDGen_Fog3D_Zone"
SWING_CONSOLE_COMMAND = "TDPlayMeleeAction"


class InputProblem(Exception):
    pass


def sha256_of_file(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load_luma(path):
    try:
        with Image.open(path) as image:
            rgb = numpy.asarray(image.convert("RGB"), dtype=numpy.float64)
    except OSError as error:
        raise InputProblem(f"그림을 열 수 없다: {path}: {error}")
    return (rgb @ numpy.array(LUMA_WEIGHTS)) / 255.0


def fraction_to_index(fraction, size):
    return math.ceil(fraction * size - 1e-9)


def zone_region(height, width):
    return (fraction_to_index(ZONE_ROW_FRACTIONS[0], height), fraction_to_index(ZONE_ROW_FRACTIONS[1], height), 0, width)


def pawn_region(height, width):
    return (fraction_to_index(PAWN_ROW_FRACTIONS[0], height), fraction_to_index(PAWN_ROW_FRACTIONS[1], height),
            fraction_to_index(PAWN_COLUMN_FRACTIONS[0], width), fraction_to_index(PAWN_COLUMN_FRACTIONS[1], width))


def crop(luma, region):
    return luma[region[0]:region[1], region[2]:region[3]]


def judge_difference(difference, noise):
    return {
        "difference": float(difference),
        "noise": float(noise),
        "ratio": float(difference / noise) if noise > 0.0 else None,
        "passes_noise_ratio": bool(difference >= NOISE_RATIO_MINIMUM * noise),
        "passes_minimum": bool(difference >= DIFFERENCE_MINIMUM),
        "passed": bool(difference >= NOISE_RATIO_MINIMUM * noise and difference >= DIFFERENCE_MINIMUM),
    }


def check_zone_visible(lumas):
    region = zone_region(*lumas["hidden_a"].shape)
    means = {name: float(crop(lumas[name], region).mean()) for name in ("hidden_a", "hidden_b", "shown_a", "shown_b")}
    difference = abs(means["shown_a"] - means["hidden_a"])
    noise = max(abs(means["shown_a"] - means["shown_b"]), abs(means["hidden_a"] - means["hidden_b"]))
    return dict(judge_difference(difference, noise), region_rows_cols=list(region), region_means=means)


def check_reaction_visible(lumas):
    region = pawn_region(*lumas["mode2_a"].shape)
    cropped = {name: crop(lumas[name], region) for name in ("mode2_a", "mode2_b", "mode0_a", "mode0_b")}
    difference = float(numpy.abs(cropped["mode2_a"] - cropped["mode0_a"]).mean())
    noise = max(float(numpy.abs(cropped["mode2_a"] - cropped["mode2_b"]).mean()), float(numpy.abs(cropped["mode0_a"] - cropped["mode0_b"]).mean()))
    return dict(judge_difference(difference, noise), region_rows_cols=list(region))


def read_log_lines(path):
    path = Path(path)
    if not path.is_file():
        raise InputProblem(f"로그 파일이 없다: {path}")
    return path.read_text(encoding="utf-8", errors="replace").splitlines()


def find_log_start(lines, log_from):
    if log_from is not None and str(log_from).isdigit():
        line_number = int(log_from)
        if not 1 <= line_number <= len(lines):
            raise InputProblem(f"--log-from 줄 번호가 로그 범위 밖이다: {line_number} (줄 수 {len(lines)})")
        return line_number - 1
    marker = str(log_from) if log_from is not None else PIE_START_MARKER
    for index in range(len(lines) - 1, -1, -1):
        if marker in lines[index]:
            return index
    raise InputProblem(f"로그에서 PIE 시작 표식을 찾지 못했다: {marker}")


def example_lines(lines):
    return [line[:EXAMPLE_LINE_LENGTH] for line in lines[:EXAMPLE_LINE_LIMIT]]


def check_log_lines(lines):
    fog3d_level_lines = [line for line in lines if "Fog3D" in line and LEVEL_PATTERN.search(line)]
    ensure_lines = [line for line in lines if "Ensure condition failed" in line]
    rdg_error_lines = [line for line in lines if "RDG" in line and "Error" in line]
    summaries = [match for match in (SUMMARY_PATTERN.search(line) for line in lines) if match]
    submitted = int(summaries[-1].group(1)) if summaries else None
    rejected = int(summaries[-1].group(2)) if summaries else None
    has_good_summary = bool(summaries) and submitted > 0 and rejected == 0
    return {
        "fog3d_warning_or_error_lines": len(fog3d_level_lines),
        "ensure_lines": len(ensure_lines),
        "rdg_error_lines": len(rdg_error_lines),
        "summary_line_count": len(summaries),
        "submitted": submitted,
        "rejected": rejected,
        "examples": {"fog3d_warning_or_error": example_lines(fog3d_level_lines), "ensure": example_lines(ensure_lines), "rdg_error": example_lines(rdg_error_lines)},
        "passed": not fog3d_level_lines and not ensure_lines and not rdg_error_lines and has_good_summary,
    }


def check_log_file(path, log_from):
    lines = read_log_lines(path)
    start = find_log_start(lines, log_from)
    result = check_log_lines(lines[start:])
    return dict(result, log_path=str(path), start_line_number=start + 1, line_count_checked=len(lines) - start)


def resolve_images_from_result(result_argument):
    result_path = Path(result_argument)
    if result_path.is_dir():
        result_path = result_path / "result.json"
    if not result_path.is_file():
        raise InputProblem(f"result.json 이 없다: {result_path}")
    result = json.loads(result_path.read_text(encoding="utf-8"))
    paths = {}
    for index, entry in enumerate(result.get("variants", [])):
        if entry.get("name") not in IMAGE_NAMES:
            continue
        if entry["name"] in paths:
            raise InputProblem(f"result.json 에 같은 이름의 변형이 둘 이상이다: {entry['name']}")
        if entry.get("saved") is not True:
            raise InputProblem(f"변형 {entry['name']} 의 촬영이 저장되지 않았다(saved 가 참이 아니다)")
        paths[entry["name"]] = result_path.parent / f"{index:02d}_{entry['name']}.png"
    return paths, result_path.parent, result.get("key_errors", [])


def resolve_image_paths(arguments):
    paths, result_directory, key_errors = {}, None, []
    if arguments.result:
        paths, result_directory, key_errors = resolve_images_from_result(arguments.result)
    for name in IMAGE_NAMES:
        explicit = getattr(arguments, name)
        if explicit:
            paths[name] = Path(explicit)
    missing = [name for name in IMAGE_NAMES if name not in paths]
    if missing:
        raise InputProblem("그림 경로가 없다: " + ", ".join(missing))
    absent = [str(paths[name]) for name in IMAGE_NAMES if not Path(paths[name]).is_file()]
    if absent:
        raise InputProblem("그림 파일이 없다: " + ", ".join(absent))
    return {name: Path(paths[name]) for name in IMAGE_NAMES}, result_directory, key_errors


def load_all_lumas(paths):
    lumas = {name: load_luma(paths[name]) for name in IMAGE_NAMES}
    sizes = {name: list(lumas[name].shape) for name in IMAGE_NAMES}
    if len({tuple(size) for size in sizes.values()}) != 1:
        raise InputProblem("그림 크기가 서로 다르다: " + json.dumps(sizes))
    return lumas


def write_difference_image(first, second, region, output_path):
    difference = numpy.clip(numpy.abs(first - second) * DIFFERENCE_GAIN, 0.0, 1.0)
    gray = (difference * 255.0 + 0.5).astype(numpy.uint8)
    image = Image.fromarray(numpy.stack([gray, gray, gray], axis=-1))
    ImageDraw.Draw(image).rectangle([region[2], region[0], max(region[3] - 1, region[2]), max(region[1] - 1, region[0])], outline=(255, 64, 64))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    image.save(output_path)
    return str(output_path)


def run_check(arguments):
    paths, result_directory, key_errors = resolve_image_paths(arguments)
    lumas = load_all_lumas(paths)
    height, width = lumas["hidden_a"].shape
    log_path = Path(arguments.log) if arguments.log else DEFAULT_LOG_PATH
    log_result = check_log_file(log_path, arguments.log_from)
    zone = check_zone_visible(lumas)
    reaction = check_reaction_visible(lumas)
    output_directory = Path(arguments.output_dir) if arguments.output_dir else (result_directory if result_directory else DEFAULT_OUTPUT_DIRECTORY)
    diff_images = {
        "zone": write_difference_image(lumas["shown_a"], lumas["hidden_a"], zone_region(height, width), output_directory / "fog3d_diff_zone.png"),
        "reaction": write_difference_image(lumas["mode2_a"], lumas["mode0_a"], pawn_region(height, width), output_directory / "fog3d_diff_reaction.png"),
    }
    report = {
        "inputs": {name: {"path": str(paths[name]), "sha256": sha256_of_file(paths[name]), "height_width": [height, width]} for name in IMAGE_NAMES},
        "definition": {
            "luma": "Y = (0.2126 R + 0.7152 G + 0.0722 B) / 255, 저장된 8비트 값 그대로",
            "zone_rows_fraction": list(ZONE_ROW_FRACTIONS), "pawn_rows_fraction": list(PAWN_ROW_FRACTIONS), "pawn_columns_fraction": list(PAWN_COLUMN_FRACTIONS),
            "noise_ratio_minimum": NOISE_RATIO_MINIMUM, "difference_minimum": DIFFERENCE_MINIMUM, "difference_image_gain": DIFFERENCE_GAIN,
        },
        "zone_visible": zone,
        "reaction_visible": reaction,
        "log": log_result,
        "lookdev_key_errors": key_errors,
        "difference_images": diff_images,
        "passed": bool(zone["passed"] and reaction["passed"] and log_result["passed"]),
    }
    output_directory.mkdir(parents=True, exist_ok=True)
    (output_directory / REPORT_FILE_NAME).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    report["report_path"] = str(output_directory / REPORT_FILE_NAME)
    return report


def describe_report(report):
    zone, reaction, log_result = report["zone_visible"], report["reaction_visible"], report["log"]
    lines = [
        f"(1) 구역 보임: 차 {zone['difference']:.6f} 잡음 {zone['noise']:.6f} 비 {zone['ratio']} → {'통과' if zone['passed'] else '실패'}",
        f"(2) 반응 보임: 차 {reaction['difference']:.6f} 잡음 {reaction['noise']:.6f} 비 {reaction['ratio']} → {'통과' if reaction['passed'] else '실패'}",
        f"(3) 로그: Fog3D 경고·오류 {log_result['fog3d_warning_or_error_lines']} ensure {log_result['ensure_lines']} RDG 오류 {log_result['rdg_error_lines']} "
        f"요약 submitted={log_result['submitted']} rejected={log_result['rejected']} → {'통과' if log_result['passed'] else '실패'}",
        "B8 " + ("통과" if report["passed"] else "실패") + " (" + report["report_path"] + ")",
    ]
    return "\n".join(lines)


def load_find_key_errors():
    for directory in (ROOT / "Tools", ROOT / "Tools" / "FluidFX"):
        if str(directory) not in sys.path:
            sys.path.insert(0, str(directory))
    import pie_lookdev_fluid
    return pie_lookdev_fluid.find_key_errors


def find_variant_file_problems(path, expects_swing):
    problems = []
    try:
        specification = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        return [f"{path}: JSON 을 읽을 수 없다: {error}"]
    find_key_errors = load_find_key_errors()
    problems += [f"{path}: base: {line}" for line in find_key_errors(specification.get("base", {}))]
    variants = specification.get("variants", [])
    for index, variant in enumerate(variants):
        problems += [f"{path}: variants[{index}] {variant.get('name')}: {line}" for line in find_key_errors(variant.get("apply", {}))]
    names = [variant.get("name") for variant in variants]
    if len(set(names)) != len(names):
        problems.append(f"{path}: 변형 이름이 겹친다")
    expected_order = [name for name in names if name in IMAGE_NAMES]
    if expected_order != list(IMAGE_NAMES):
        problems.append(f"{path}: 판정용 여덟 이름의 차례가 고정 차례와 다르다: {expected_order}")
    extras = [name for name in names if name not in IMAGE_NAMES]
    if extras != [WALK_VARIANT_NAME]:
        problems.append(f"{path}: 판정용 여덟 이름 말고 walk 하나만 더 있어야 한다: {extras}")
    elif names.index(WALK_VARIANT_NAME) != names.index("mode2_a") - 1:
        problems.append(f"{path}: walk 는 mode2_a 바로 앞이어야 한다")
    by_name = {variant.get("name"): variant for variant in variants}
    walk_move = by_name.get(WALK_VARIANT_NAME, {}).get("apply", {}).get("move")
    if not (isinstance(walk_move, dict) and walk_move.get("distance_cm") == 400 and walk_move.get("yaw") == 0):
        problems.append(f"{path}: walk 는 move {{distance_cm: 400, yaw: 0}} 이어야 한다")
    mode2_console = by_name.get("mode2_a", {}).get("apply", {}).get("console", [])
    if (SWING_CONSOLE_COMMAND in mode2_console) != expects_swing:
        problems.append(f"{path}: mode2_a 의 {SWING_CONSOLE_COMMAND} 유무가 파일 종류와 다르다(휘두르기 파일 {expects_swing})")
    mode0_console = by_name.get("mode0_a", {}).get("apply", {}).get("console", [])
    if mode0_console != ["TD.Fog3D.Mode 0"]:
        problems.append(f"{path}: mode0_a 는 콘솔 TD.Fog3D.Mode 0 이어야 한다: {mode0_console}")
    if specification.get("base", {}).get("console") != ["TD.Fog3D.Mode 2"] or specification.get("base", {}).get("arm") != 400 or specification.get("base", {}).get("view") != [-12, 0]:
        problems.append(f"{path}: base 는 arm 400, view [-12, 0], 콘솔 TD.Fog3D.Mode 2 이어야 한다")
    hide_labels = {label for variant in variants for label in variant.get("apply", {}).get("hide", {})}
    if hide_labels != {STAGE_ZONE_LABEL}:
        problems.append(f"{path}: hide 라벨이 무대 구역 액터 라벨({STAGE_ZONE_LABEL})과 다르다: {sorted(hide_labels)}")
    return problems


def write_gray_png(path, gray_rows):
    gray = numpy.asarray(gray_rows, dtype=numpy.uint8)
    Image.fromarray(numpy.stack([gray, gray, gray], axis=-1)).save(path)


def build_synthetic_images(directory, zone_delta, zone_noise_count, zone_noise_amount, pawn_delta, pawn_noise_count, pawn_noise_amount, zone_changed_fraction=1.0):
    height, width = 100, 160
    zone = zone_region(height, width)
    pawn = pawn_region(height, width)
    base = numpy.full((height, width), 100, dtype=numpy.int32)

    def with_delta(array, region, delta, fraction=1.0):
        result = array.copy()
        block = result[region[0]:region[1], region[2]:region[3]]
        flat = block.reshape(-1)
        flat[:int(round(flat.size * fraction))] += delta
        result[region[0]:region[1], region[2]:region[3]] = flat.reshape(block.shape)
        return result

    def with_noise(array, region, count, amount):
        result = array.copy()
        block = result[region[0]:region[1], region[2]:region[3]]
        flat = block.reshape(-1)
        flat[:count] += amount
        result[region[0]:region[1], region[2]:region[3]] = flat.reshape(block.shape)
        return result

    hidden_a = base
    hidden_b = with_noise(hidden_a, zone, zone_noise_count, zone_noise_amount)
    shown_a = with_delta(base, zone, zone_delta, zone_changed_fraction)
    shown_b = with_noise(shown_a, zone, zone_noise_count, zone_noise_amount)
    mode0_a = base
    mode0_b = with_noise(mode0_a, pawn, pawn_noise_count, pawn_noise_amount)
    mode2_a = with_delta(base, pawn, pawn_delta)
    mode2_b = with_noise(mode2_a, pawn, pawn_noise_count, pawn_noise_amount)
    images = {"hidden_a": hidden_a, "hidden_b": hidden_b, "shown_a": shown_a, "shown_b": shown_b, "mode2_a": mode2_a, "mode2_b": mode2_b, "mode0_a": mode0_a, "mode0_b": mode0_b}
    paths = {}
    for name, array in images.items():
        paths[name] = Path(directory) / f"{name}.png"
        write_gray_png(paths[name], numpy.clip(array, 0, 255))
    return paths


GOOD_LOG_BODY = [
    "[2026.11.13-10.00.01:000][  1]LogPlayLevel: PIE: Created PIE world by copying editor world from /Game/FluidFX/Maps/LV_TDFog3DStage",
    "[2026.11.13-10.00.02:000][  2]LogTemp: Display: some line",
    "[2026.11.13-10.00.30:000][900]LogTDFog3D: Display: Fog3D zone summary: submitted=1500 rejected=0",
]


def write_log(path, lines):
    Path(path).write_text("\n".join(lines) + "\n", encoding="utf-8")
    return path


def parse_arguments(argument_list):
    parser = argparse.ArgumentParser(description="3D 안개 1b 화면 판정 B8")
    parser.add_argument("--result", help="pie_lookdev_fluid.py 결과 폴더 또는 result.json")
    for name in IMAGE_NAMES:
        parser.add_argument("--" + name.replace("_", "-"), dest=name, help=f"{name} 그림 경로(--result 의 값을 덮어씀)")
    parser.add_argument("--log", help="로그 파일(기본 Saved/Logs/TDGame.log)")
    parser.add_argument("--log-from", help="로그 검사 시작: 줄 번호(1부터) 또는 표식 문자열의 마지막 줄(기본 마지막 PIE 시작 표식)")
    parser.add_argument("--output-dir", help="출력 폴더(기본 결과 폴더, 직접 줄 때는 Saved/FluidFX/lookdev/fog3d_visible_check)")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--check-variants", nargs="+", metavar="JSON")
    return parser.parse_args(argument_list)


def main(argument_list=None):
    arguments = parse_arguments(sys.argv[1:] if argument_list is None else argument_list)
    if arguments.self_test:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        return run_self_test()
    if arguments.check_variants:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        problems = [problem for path in arguments.check_variants for problem in find_variant_file_problems(path, "walk" not in Path(path).stem)]
        print("\n".join(problems) if problems else "변형 JSON 키 오류 0")
        return EXIT_FAILED if problems else EXIT_PASSED
    try:
        report = run_check(arguments)
    except InputProblem as error:
        print("입력 문제: " + str(error))
        return EXIT_INPUT_PROBLEM
    print(describe_report(report))
    return EXIT_PASSED if report["passed"] else EXIT_FAILED


def run_self_test():
    results = []

    def record(label, passed, detail=""):
        results.append(passed)
        print(("PASS " if passed else "FAIL ") + label + (": " + detail if detail else ""))

    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        good_log = write_log(root / "good.log", GOOD_LOG_BODY)

        def run_case(name, image_arguments, log_path=good_log, log_from=None):
            case_directory = root / name
            case_directory.mkdir()
            paths = build_synthetic_images(case_directory, **image_arguments)
            arguments = parse_arguments([f"--{key.replace('_', '-')}={value}" for key, value in paths.items()] + ["--log", str(log_path), "--output-dir", str(case_directory / "out")] + (["--log-from", str(log_from)] if log_from else []))
            return run_check(arguments), case_directory

        passing = dict(zone_delta=20, zone_noise_count=96, zone_noise_amount=1, pawn_delta=12, pawn_noise_count=52, pawn_noise_amount=1)
        report, case_directory = run_case("pass", passing)
        record("통과 경우: 세 항목 모두 통과", report["passed"], f"구역 비 {report['zone_visible']['ratio']:.1f} 반응 비 {report['reaction_visible']['ratio']:.1f}")
        record("통과 경우: 입력 sha256 8개와 차이 그림 2장", len(report["inputs"]) == 8 and all(len(item["sha256"]) == 64 for item in report["inputs"].values()) and (case_directory / "out" / "fog3d_diff_zone.png").is_file() and (case_directory / "out" / "fog3d_diff_reaction.png").is_file())
        exact = (case_directory / "out" / REPORT_FILE_NAME).is_file() and json.loads((case_directory / "out" / REPORT_FILE_NAME).read_text(encoding="utf-8"))["passed"] is True
        record("통과 경우: JSON 보고서 기록", exact)
        record("영역 정의: 100x160 에서 구역 세로 40~100, 폰 둘레 세로 30~95 가로 40~120", zone_region(100, 160) == (40, 100, 0, 160) and pawn_region(100, 160) == (30, 95, 40, 120))
        record("영역 정의: 720x1280 에서 구역 288~720, 폰 둘레 216~684 가로 320~960", zone_region(720, 1280) == (288, 720, 0, 1280) and pawn_region(720, 1280) == (216, 684, 320, 960))

        report, _ = run_case("zone_ratio", dict(passing, zone_delta=4, zone_noise_count=9600, zone_noise_amount=2))
        record("3배 미만 실패: 구역 비 2 → 구역 실패, 반응은 통과", not report["zone_visible"]["passed"] and not report["zone_visible"]["passes_noise_ratio"] and report["zone_visible"]["passes_minimum"] and report["reaction_visible"]["passed"] and not report["passed"], f"비 {report['zone_visible']['ratio']}")
        report, _ = run_case("zone_floor", dict(passing, zone_delta=1, zone_changed_fraction=0.25, zone_noise_count=0))
        record("1/255 미만 실패: 구역 차 0.25/255, 잡음 0 → 구역 실패", not report["zone_visible"]["passed"] and report["zone_visible"]["passes_noise_ratio"] and not report["zone_visible"]["passes_minimum"] and report["zone_visible"]["noise"] == 0.0)
        report, _ = run_case("reaction_ratio", dict(passing, pawn_delta=4, pawn_noise_count=5200, pawn_noise_amount=2))
        record("3배 미만 실패: 반응 비 2 → 반응 실패, 구역은 통과", not report["reaction_visible"]["passed"] and report["zone_visible"]["passed"] and not report["passed"], f"비 {report['reaction_visible']['ratio']}")
        report, _ = run_case("reaction_floor", dict(passing, pawn_delta=0, pawn_noise_count=0))
        record("1/255 미만 실패: 반응 차 0 → 반응 실패", not report["reaction_visible"]["passed"] and report["reaction_visible"]["difference"] == 0.0)

        log_cases = (
            ("요약 줄 없음 실패", GOOD_LOG_BODY[:2], False),
            ("Fog3D 경고 줄 있음 실패", GOOD_LOG_BODY[:2] + ["[x]LogTDFog3D: Warning: zone material not loaded"] + GOOD_LOG_BODY[2:], False),
            ("Fog3D 오류 줄 있음 실패", GOOD_LOG_BODY[:2] + ["[x]LogTemp: Error: Fog3D bad"] + GOOD_LOG_BODY[2:], False),
            ("ensure 줄 있음 실패", GOOD_LOG_BODY[:2] + ["[x]LogOutputDevice: Error: Ensure condition failed: x"] + GOOD_LOG_BODY[2:], False),
            ("RDG 와 Error 가 함께 든 줄 실패", GOOD_LOG_BODY[:2] + ["[x]LogRHI: RDG pass failed Error code 3"] + GOOD_LOG_BODY[2:], False),
            ("rejected 1 실패", GOOD_LOG_BODY[:2] + ["Fog3D zone summary: submitted=10 rejected=1"], False),
            ("submitted 0 실패", GOOD_LOG_BODY[:2] + ["Fog3D zone summary: submitted=0 rejected=0"], False),
            ("표식 앞의 경고는 무시하고 통과", ["[x]LogTDFog3D: Warning: old session"] + GOOD_LOG_BODY, True),
            ("Fog3D 와 무관한 경고는 통과", GOOD_LOG_BODY[:2] + ["[x]LogTemp: Warning: unrelated"] + GOOD_LOG_BODY[2:], True),
        )
        for label, lines, expected in log_cases:
            outcome = check_log_lines(find_slice(lines))
            record("로그 " + label, outcome["passed"] is expected, f"fog3d {outcome['fog3d_warning_or_error_lines']} ensure {outcome['ensure_lines']} rdg {outcome['rdg_error_lines']} 요약 {outcome['submitted']}/{outcome['rejected']}")

        numbered = write_log(root / "numbered.log", ["[x]LogTDFog3D: Warning: before", "start here"] + GOOD_LOG_BODY[2:])
        record("--log-from 줄 번호 2 부터 검사 → 통과", check_log_file(numbered, "2")["passed"])
        record("--log-from 줄 번호 1 부터 검사 → 경고 포함 실패", not check_log_file(numbered, "1")["passed"])
        record("--log-from 표식 문자열", check_log_file(numbered, "start here")["start_line_number"] == 2)
        try:
            check_log_file(write_log(root / "nomarker.log", ["nothing"]), None)
            record("표식 없는 로그 → 입력 문제", False)
        except InputProblem:
            record("표식 없는 로그 → 입력 문제", True)

        mismatch_directory = root / "mismatch"
        mismatch_directory.mkdir()
        mismatch_paths = build_synthetic_images(mismatch_directory, **passing)
        write_gray_png(mismatch_paths["mode0_b"], numpy.full((50, 80), 100))
        exit_code = main([f"--{key.replace('_', '-')}={value}" for key, value in mismatch_paths.items()] + ["--log", str(good_log)])
        record("그림 크기 다름 → 종료 2", exit_code == EXIT_INPUT_PROBLEM)
        exit_code = main(["--hidden-a", str(root / "none.png"), "--log", str(good_log)])
        record("그림 경로 모자람 → 종료 2", exit_code == EXIT_INPUT_PROBLEM)

        pass_directory = root / "pass"
        pass_paths = {name: pass_directory / f"{name}.png" for name in IMAGE_NAMES}
        exit_code = main([f"--{key.replace('_', '-')}={value}" for key, value in pass_paths.items()] + ["--log", str(good_log), "--output-dir", str(root / "out_main")])
        record("main: 모두 통과 → 종료 0", exit_code == EXIT_PASSED)
        exit_code = main([f"--{key.replace('_', '-')}={value}" for key, value in pass_paths.items()] + ["--log", str(write_log(root / "nosummary.log", GOOD_LOG_BODY[:2])), "--output-dir", str(root / "out_main2")])
        record("main: 로그 실패 → 종료 1", exit_code == EXIT_FAILED)

        result_directory = root / "lookdev_result"
        result_directory.mkdir()
        variants = []
        for index, name in enumerate(("hidden_a", "hidden_b", "shown_a", "shown_b", WALK_VARIANT_NAME, "mode2_a", "mode2_b", "mode0_a", "mode0_b")):
            variants.append({"name": name, "apply": {}, "saved": True})
            source = pass_paths.get(name, pass_paths["hidden_a"])
            (result_directory / f"{index:02d}_{name}.png").write_bytes(source.read_bytes())
        (result_directory / "result.json").write_text(json.dumps({"variants": variants, "key_errors": []}), encoding="utf-8")
        paths_from_result, _, _ = resolve_images_from_result(str(result_directory))
        record("result.json 에서 변형 이름으로 찾기(walk 는 제외, 순번은 전체 차례)", sorted(paths_from_result) == sorted(IMAGE_NAMES) and paths_from_result["mode2_a"].name == "05_mode2_a.png" and paths_from_result["mode0_b"].name == "08_mode0_b.png")
        exit_code = main(["--result", str(result_directory), "--log", str(good_log)])
        record("main: --result 로 입력 → 판정이 돌고 결과 폴더에 보고서", exit_code in (EXIT_PASSED, EXIT_FAILED) and (result_directory / REPORT_FILE_NAME).is_file())
        variants[2]["saved"] = False
        (result_directory / "result.json").write_text(json.dumps({"variants": variants}), encoding="utf-8")
        record("result.json saved 거짓 → 입력 문제", main(["--result", str(result_directory), "--log", str(good_log)]) == EXIT_INPUT_PROBLEM)

    tools_directory = Path(__file__).resolve().parent
    for file_name, expects_swing in LOOKDEV_VARIANT_FILES:
        problems = find_variant_file_problems(tools_directory / file_name, expects_swing)
        record(f"변형 JSON {file_name}: find_key_errors·차례·이름·콘솔 점검", not problems, "; ".join(problems)[:600])
    return EXIT_PASSED if all(results) else EXIT_FAILED


def find_slice(lines):
    return lines[find_log_start(lines, None):]


if __name__ == "__main__":
    sys.exit(main())
