"""에디터 밖에서 실행: PIE 지점 촬영(PNG + 지점 JSON)에서 지역 경계선을 가로지르는 화면 휘도 계단을 재서 P3-19 K15(계단 <= 같은 단면 인접 차이 중앙값 x 2)를 판정한다.
실행: python Tools/WorldGen/check_boundary_luminance.py --run <run> [--ids B01,B02,B03] [--self-test] [--out <폴더>] [--debug-line axis,value,through_x,through_y] (PowerShell 또는 Git Bash, numpy·Pillow 필요, 에디터 불필요)
출력: Saved/WorldGen/PieShots/<run>/(또는 --out)의 boundary_luminance.json·boundary_luminance.md·<지점 ID>_boundary_overlay.png. 종료 코드 0 = 전 지점 통과, 1 = 실패 또는 판정 불가, 2 = 입력 없음
상태: 현행 (2026-10-15)
"""
import argparse
import math
import os
import sys
import time

import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "Tools", "WorldGen"))
import pie_capture_spots as capture  # noqa: E402

BOUNDARY_SPOTS = {
    "B01_sw_boundary": {"axis": "y", "value_cm": -50400.0, "through_cm": (-25000.0, -50400.0)},
    "B02_ne_boundary": {"axis": "x", "value_cm": 50400.0, "through_cm": (50400.0, 46560.0)},
    "B03_se_slot_boundary": {"axis": "y", "value_cm": -50400.0, "through_cm": (6600.0, -50400.0)},
}

SECTION_ALONG_OFFSETS_CM = np.arange(-800.0, 800.0 + 1.0, 100.0)
SAMPLE_OFFSETS_CM = np.arange(-600.0, 600.0 + 1.0, 25.0)
STEP_OFFSET_CM = 25.0
SECTION_COUNT = len(SECTION_ALONG_OFFSETS_CM)
SAMPLE_COUNT = len(SAMPLE_OFFSETS_CM)
BOUNDARY_INDEX = int(np.flatnonzero(SAMPLE_OFFSETS_CM == 0.0)[0])
INNER_STEP_INDEX = int(np.flatnonzero(SAMPLE_OFFSETS_CM == -STEP_OFFSET_CM)[0])
OUTER_STEP_INDEX = int(np.flatnonzero(SAMPLE_OFFSETS_CM == STEP_OFFSET_CM)[0])
INNER_SAMPLES = SAMPLE_OFFSETS_CM < 0.0
OUTER_SAMPLES = SAMPLE_OFFSETS_CM > 0.0
PAIR_INDICES = np.arange(SAMPLE_COUNT - 1)
PAIR_AWAY_FROM_BOUNDARY = (PAIR_INDICES != BOUNDARY_INDEX - 1) & (PAIR_INDICES != BOUNDARY_INDEX)

MIN_SAMPLES_PER_SIDE = 8
MIN_VALID_SECTIONS = 5
RATIO_LIMIT = 2.0
BASELINE_FLOOR = 0.002
SELF_TEST_FACTORS = (1.10, 1.25)
NEAR_DEPTH_CM = 10.0
CHEST_HEIGHT_CM = 120.0
PAWN_CENTER_TOLERANCE_PX = 60.0
ROLL_TOLERANCE_DEGREES = 0.01
WEIGHT_EPSILON = 1e-9

PASSED = "통과"
FAILED = "실패"
UNDECIDABLE = "판정 불가"
DARK_NOTE = "어두움(판정 약함)"
DARK_SAMPLE_LUMINANCE = 0.02

INNER_COLOR = (0, 200, 255)
OUTER_COLOR = (255, 80, 0)
BOUNDARY_COLOR = (255, 255, 0)
STEP_COLOR = (0, 255, 0)
STEP_MARKER_RADIUS_PX = 4


def log(message):
    print(message, flush=True)


def parse_debug_line(text):
    parts = [part.strip() for part in text.split(",")]
    if len(parts) != 4 or parts[0] not in ("x", "y"):
        raise ValueError("--debug-line needs axis,value,through_x,through_y with axis x or y")
    axis = parts[0]
    value_cm, through_x, through_y = (float(part) for part in parts[1:])
    perpendicular_cm = through_x if axis == "x" else through_y
    if abs(perpendicular_cm - value_cm) > 1.0:
        raise ValueError("--debug-line through point must lie on the line")
    return {"axis": axis, "value_cm": value_cm, "through_cm": (through_x, through_y)}


def find_run_directory(value):
    if os.path.isdir(value):
        return os.path.abspath(value)
    candidate = os.path.join(capture.SHOT_ROOT, value)
    return candidate if os.path.isdir(candidate) else None


def resolve_spot_ids(tokens_text, candidate_ids):
    tokens = capture.split_tokens(tokens_text)
    if not tokens:
        return list(candidate_ids)
    unknown = [token for token in tokens if not any(spot_id == token or spot_id.startswith(token + "_") for spot_id in candidate_ids)]
    if unknown:
        raise ValueError(f"unknown spot ids: {unknown}")
    return [spot_id for spot_id in candidate_ids if any(spot_id == token or spot_id.startswith(token + "_") for token in tokens)]


def list_run_spot_ids(run_dir):
    return [record["id"] for record in capture.load_run_records(run_dir) if not record.get("error")]


def find_missing_inputs(run_dir, spot_ids):
    missing = []
    for spot_id in spot_ids:
        json_path = os.path.join(run_dir, spot_id + ".json")
        if not os.path.isfile(json_path):
            missing.append(f"{spot_id}: JSON 없음 {json_path}")
            continue
        record = capture.read_json(json_path)
        if record.get("error") or "camera" not in record or "pawn" not in record:
            missing.append(f"{spot_id}: 촬영 실패 기록 {record.get('error')}")
            continue
        png_path = os.path.join(run_dir, record.get("png") or spot_id + ".png")
        if not os.path.isfile(png_path):
            missing.append(f"{spot_id}: PNG 없음 {png_path}")
    return missing


def interpolate_height_cm(grid, x_cm, y_cm):
    half = (grid["samples"] - 1) / 2.0
    cell_cm = grid["cell_cm"]
    column = (x_cm - grid["center"][0]) / cell_cm + half
    row = (y_cm - grid["center"][1]) / cell_cm + half
    column_floor = math.floor(column)
    row_floor = math.floor(row)
    column_weight = column - column_floor
    row_weight = row - row_floor
    total = 0.0
    for row_index, row_factor in ((row_floor, 1.0 - row_weight), (row_floor + 1, row_weight)):
        for column_index, column_factor in ((column_floor, 1.0 - column_weight), (column_floor + 1, column_weight)):
            weight = row_factor * column_factor
            if weight < WEIGHT_EPSILON:
                continue
            corner_x_cm = grid["center"][0] + (column_index - half) * cell_cm
            corner_y_cm = grid["center"][1] + (row_index - half) * cell_cm
            height_cm = capture.sample_grid_cm(grid, corner_x_cm, corner_y_cm)
            if height_cm is None:
                return None
            total += weight * height_cm
    return total


def build_height_lookup(regions):
    grids = {}

    def grid_for(region_name):
        if region_name not in grids:
            grids[region_name] = capture.load_height_grid(regions[region_name])
        return grids[region_name]

    def height_cm(preferred_region, x_cm, y_cm):
        region_names = list(regions)
        if preferred_region in regions:
            region_names = [preferred_region] + [name for name in regions if name != preferred_region]
        for region_name in region_names:
            grid = grid_for(region_name)
            if grid is None:
                continue
            height = interpolate_height_cm(grid, x_cm, y_cm)
            if height is not None:
                return height
        return None

    return height_cm


def build_section_points(boundary, region_name, height_cm):
    axis = boundary["axis"]
    anchor_cm = boundary["through_cm"][0] if axis == "y" else boundary["through_cm"][1]
    points = np.full((SECTION_COUNT, SAMPLE_COUNT, 3), np.nan)
    for section_index, along_offset_cm in enumerate(SECTION_ALONG_OFFSETS_CM):
        along_cm = anchor_cm + along_offset_cm
        for sample_index, sample_offset_cm in enumerate(SAMPLE_OFFSETS_CM):
            across_cm = boundary["value_cm"] + sample_offset_cm
            x_cm, y_cm = (along_cm, across_cm) if axis == "y" else (across_cm, along_cm)
            ground_cm = height_cm(region_name, x_cm, y_cm)
            points[section_index, sample_index] = (x_cm, y_cm, np.nan if ground_cm is None else ground_cm)
    return points


def build_camera(record, width, height):
    camera = record["camera"]
    pitch = math.radians(camera["rot"][0])
    yaw = math.radians(camera["rot"][1])
    return {
        "location": np.array(camera["loc"], dtype=np.float64),
        "forward": np.array([math.cos(pitch) * math.cos(yaw), math.cos(pitch) * math.sin(yaw), math.sin(pitch)]),
        "right": np.array([-math.sin(yaw), math.cos(yaw), 0.0]),
        "up": np.array([-math.sin(pitch) * math.cos(yaw), -math.sin(pitch) * math.sin(yaw), math.cos(pitch)]),
        "focal_px": (width / 2.0) / math.tan(math.radians(camera["fov"]) / 2.0),
        "roll_degrees": float(camera["rot"][2]) if len(camera["rot"]) > 2 else 0.0,
        "width": width,
        "height": height,
    }


def project_points(camera, points):
    delta = points - camera["location"]
    depth = delta @ camera["forward"]
    in_front = depth > NEAR_DEPTH_CM
    safe_depth = np.where(in_front, depth, 1.0)
    u = camera["width"] / 2.0 + (delta @ camera["right"]) / safe_depth * camera["focal_px"]
    v = camera["height"] / 2.0 - (delta @ camera["up"]) / safe_depth * camera["focal_px"]
    return np.where(in_front, u, np.nan), np.where(in_front, v, np.nan)


def project_sections(camera, points):
    flat_u, flat_v = project_points(camera, points.reshape(-1, 3))
    u = flat_u.reshape(SECTION_COUNT, SAMPLE_COUNT)
    v = flat_v.reshape(SECTION_COUNT, SAMPLE_COUNT)
    column = np.floor(u)
    row = np.floor(v)
    mask = np.isfinite(u) & np.isfinite(v) & (column >= 1) & (column <= camera["width"] - 2) & (row >= 1) & (row <= camera["height"] - 2)
    return {
        "u": u,
        "v": v,
        "mask": mask,
        "column_index": np.where(mask, column, 0).astype(int),
        "row_index": np.where(mask, row, 0).astype(int),
    }


def check_camera_projection(camera, record):
    points = [record["pawn"]]
    if record.get("ground_z") is not None:
        points.append([record["pawn"][0], record["pawn"][1], record["ground_z"] + CHEST_HEIGHT_CM])
    u, v = project_points(camera, np.array(points, dtype=np.float64))
    pixels = [None if not (math.isfinite(u[index]) and math.isfinite(v[index])) else [round(float(u[index]), 1), round(float(v[index]), 1)] for index in range(len(points))]
    offset_px = None
    if pixels[0] is not None:
        offset_px = round(math.hypot(pixels[0][0] - camera["width"] / 2.0, pixels[0][1] - camera["height"] / 2.0), 1)
    is_ok = offset_px is not None and offset_px <= PAWN_CENTER_TOLERANCE_PX and abs(camera["roll_degrees"]) <= ROLL_TOLERANCE_DEGREES
    return {
        "pawn_pixel": pixels[0],
        "chest_pixel": pixels[1] if len(pixels) > 1 else None,
        "pawn_center_offset_px": offset_px,
        "roll_degrees": camera["roll_degrees"],
        "is_ok": is_ok,
    }


def load_luminance(png_path):
    with Image.open(png_path) as image:
        pixels = np.asarray(image.convert("RGB"), dtype=np.float32) / 255.0
    return 0.2126 * pixels[..., 0] + 0.7152 * pixels[..., 1] + 0.0722 * pixels[..., 2]


def mean_filter_3x3(luminance):
    height, width = luminance.shape
    padded = np.pad(luminance, 1, mode="edge")
    total = np.zeros_like(luminance)
    for row_shift in range(3):
        for column_shift in range(3):
            total += padded[row_shift:row_shift + height, column_shift:column_shift + width]
    return total / 9.0


def sample_values(smoothed, projection):
    sampled = smoothed[projection["row_index"], projection["column_index"]].astype(np.float64)
    return np.where(projection["mask"], sampled, np.nan)


def measure_section(values):
    finite = np.isfinite(values)
    inner_count = int(np.count_nonzero(finite & INNER_SAMPLES))
    outer_count = int(np.count_nonzero(finite & OUTER_SAMPLES))
    section = {"inner_count": inner_count, "outer_count": outer_count, "is_valid": False, "step": None, "baseline": None, "ratio": None}
    if min(inner_count, outer_count) < MIN_SAMPLES_PER_SIDE:
        return section
    if not (finite[INNER_STEP_INDEX] and finite[OUTER_STEP_INDEX]):
        return section
    usable_pairs = finite[:-1] & finite[1:] & PAIR_AWAY_FROM_BOUNDARY
    if not usable_pairs.any():
        return section
    baseline = float(np.median(np.abs(np.diff(values))[usable_pairs]))
    step = float(abs(values[OUTER_STEP_INDEX] - values[INNER_STEP_INDEX]))
    section.update(is_valid=True, step=step, baseline=baseline, ratio=step / max(baseline, BASELINE_FLOOR))
    return section


def measure_all_sections(value_matrix):
    return [measure_section(value_matrix[section_index]) for section_index in range(SECTION_COUNT)]


def summarize_sections(sections):
    valid_sections = [section for section in sections if section["is_valid"]]
    summary = {
        "section_count": len(sections),
        "valid_section_count": len(valid_sections),
        "ratio_median": None,
        "ratio_max": None,
        "step_median": None,
        "baseline_median": None,
        "verdict": UNDECIDABLE,
    }
    if not valid_sections:
        return summary
    summary["ratio_median"] = float(np.median([section["ratio"] for section in valid_sections]))
    summary["ratio_max"] = float(max(section["ratio"] for section in valid_sections))
    summary["step_median"] = float(np.median([section["step"] for section in valid_sections]))
    summary["baseline_median"] = float(np.median([section["baseline"] for section in valid_sections]))
    if len(valid_sections) < MIN_VALID_SECTIONS:
        return summary
    summary["verdict"] = PASSED if summary["ratio_median"] <= RATIO_LIMIT else FAILED
    return summary


def describe_variant(summary):
    ratio_median = summary["ratio_median"]
    return {
        "ratio_median": None if ratio_median is None else round(ratio_median, 4),
        "valid_section_count": summary["valid_section_count"],
        "exceeds_limit": ratio_median is not None and ratio_median > RATIO_LIMIT,
    }


def median_sample_luminance(value_matrix, sections):
    valid_rows = [index for index, section in enumerate(sections) if section["is_valid"]]
    if not valid_rows:
        return None
    samples = value_matrix[valid_rows]
    return float(np.median(samples[np.isfinite(samples)]))


def run_self_test(value_matrix, original_summary):
    result = {"original": describe_variant(original_summary)}
    for factor in SELF_TEST_FACTORS:
        result[f"x{factor:.2f}"] = describe_variant(summarize_sections(measure_all_sections(widen_boundary_step(value_matrix, factor))))
    return result


def widen_boundary_step(value_matrix, factor):
    scaled = value_matrix.copy()
    outer_is_brighter = np.nan_to_num(value_matrix[:, OUTER_STEP_INDEX] - value_matrix[:, INNER_STEP_INDEX]) >= 0.0
    row_factors = np.where(outer_is_brighter, factor, 1.0 / factor)
    scaled[:, OUTER_SAMPLES] *= row_factors[:, None]
    return scaled


def draw_overlay(png_path, projection, output_path):
    with Image.open(png_path) as source:
        image = source.convert("RGB")
    draw = ImageDraw.Draw(image)
    for section_index in range(SECTION_COUNT):
        for sample_index in range(SAMPLE_COUNT):
            if not projection["mask"][section_index, sample_index]:
                continue
            u = float(projection["u"][section_index, sample_index])
            v = float(projection["v"][section_index, sample_index])
            offset_cm = SAMPLE_OFFSETS_CM[sample_index]
            color = BOUNDARY_COLOR if offset_cm == 0.0 else INNER_COLOR if offset_cm < 0.0 else OUTER_COLOR
            draw.rectangle([u - 1, v - 1, u + 1, v + 1], fill=color)
            if sample_index in (INNER_STEP_INDEX, OUTER_STEP_INDEX):
                radius = STEP_MARKER_RADIUS_PX
                draw.ellipse([u - radius, v - radius, u + radius, v + radius], outline=STEP_COLOR)
    image.save(output_path)


def round_or_none(value, digits):
    return None if value is None else round(float(value), digits)


def describe_sections(sections, anchor_cm):
    return [
        {
            "along_cm": float(anchor_cm + SECTION_ALONG_OFFSETS_CM[index]),
            "is_valid": section["is_valid"],
            "inner_count": section["inner_count"],
            "outer_count": section["outer_count"],
            "step": round_or_none(section["step"], 5),
            "baseline": round_or_none(section["baseline"], 5),
            "ratio": round_or_none(section["ratio"], 3),
        }
        for index, section in enumerate(sections)
    ]


def analyze_spot(spot_id, boundary, record, run_dir, out_dir, height_cm, should_self_test):
    png_path = os.path.join(run_dir, record.get("png") or spot_id + ".png")
    luminance = load_luminance(png_path)
    height, width = luminance.shape
    camera = build_camera(record, width, height)
    camera_check = check_camera_projection(camera, record)
    points = build_section_points(boundary, record.get("region", ""), height_cm)
    projection = project_sections(camera, points)
    value_matrix = sample_values(mean_filter_3x3(luminance), projection)
    sections = measure_all_sections(value_matrix)
    summary = summarize_sections(sections)
    luminance_median = median_sample_luminance(value_matrix, sections)
    reason = ""
    if not camera_check["is_ok"]:
        summary["verdict"] = UNDECIDABLE
        reason = f"카메라 투영 점검 실패(폰 화면 중앙 거리 {camera_check['pawn_center_offset_px']}px, 롤 {camera_check['roll_degrees']})"
    elif summary["verdict"] == UNDECIDABLE:
        reason = f"유효 단면 {summary['valid_section_count']}개(최소 {MIN_VALID_SECTIONS})"
    if luminance_median is not None and luminance_median < DARK_SAMPLE_LUMINANCE:
        reason = "; ".join(part for part in (reason, DARK_NOTE) if part)
    overlay_name = spot_id + "_boundary_overlay.png"
    draw_overlay(png_path, projection, os.path.join(out_dir, overlay_name))
    anchor_cm = boundary["through_cm"][0] if boundary["axis"] == "y" else boundary["through_cm"][1]
    result = {
        "id": spot_id,
        "boundary": {"axis": boundary["axis"], "value_cm": boundary["value_cm"], "through_cm": list(boundary["through_cm"])},
        "camera_check": camera_check,
        "section_count": summary["section_count"],
        "valid_section_count": summary["valid_section_count"],
        "ratio_median": round_or_none(summary["ratio_median"], 4),
        "ratio_max": round_or_none(summary["ratio_max"], 4),
        "step_median": round_or_none(summary["step_median"], 5),
        "baseline_median": round_or_none(summary["baseline_median"], 5),
        "sample_luminance_median": round_or_none(luminance_median, 5),
        "verdict": summary["verdict"],
        "reason": reason,
        "overlay": overlay_name,
        "sections": describe_sections(sections, anchor_cm),
        "self_test": None,
    }
    if should_self_test:
        result["self_test"] = run_self_test(value_matrix, summary)
    return result


def format_ratio(value):
    return "-" if value is None else f"{value:.2f}"


def format_variant(variant):
    if variant["ratio_median"] is None:
        return "-"
    return f"{variant['ratio_median']:.2f} ({'검출' if variant['exceeds_limit'] else '미검출'})"


def render_markdown(run_name, meta, results):
    lines = [
        f"# K15 경계 휘도 계단 {run_name}",
        "",
        f"- 실행: {meta['created']}, 지점 {len(results)}개, 합격선: 지점별 단면 비율 중앙값 <= {RATIO_LIMIT}",
        f"- 단면 {SECTION_COUNT}개(경계 통과점 기준 +-{SECTION_ALONG_OFFSETS_CM[-1]:.0f}cm, {SECTION_ALONG_OFFSETS_CM[1] - SECTION_ALONG_OFFSETS_CM[0]:.0f}cm 간격) x 표본 {SAMPLE_COUNT}개(경계 양쪽 +-{SAMPLE_OFFSETS_CM[-1]:.0f}cm, {SAMPLE_OFFSETS_CM[1] - SAMPLE_OFFSETS_CM[0]:.0f}cm 간격, 높이맵 지면), 표본 휘도 = 3x3 평균(0~1)",
        f"- 단면 비율 = 계단 / max(기준, {BASELINE_FLOOR}), 계단 = |표본(-{STEP_OFFSET_CM:.0f}cm) - 표본(+{STEP_OFFSET_CM:.0f}cm)|, 기준 = 경계 표본(0cm)을 건드리지 않는 인접 표본 쌍({SAMPLE_OFFSETS_CM[1] - SAMPLE_OFFSETS_CM[0]:.0f}cm 간격) 차이의 중앙값",
        f"- 유효 단면 = 경계 양쪽 표본이 각각 {MIN_SAMPLES_PER_SIDE}개 이상 화면 안이고 +-{STEP_OFFSET_CM:.0f}cm 표본이 모두 화면 안, 유효 단면이 {MIN_VALID_SECTIONS}개 미만이면 판정 불가, 카메라 투영 점검(폰이 화면 중앙 +-{PAWN_CENTER_TOLERANCE_PX:.0f}px, 롤 0) 실패도 판정 불가",
        f"- 표본 휘도 중앙값 = 유효 단면의 유효 표본 휘도 중앙값, {DARK_SAMPLE_LUMINANCE} 미만이면 비고에 {DARK_NOTE}(판정 논리는 그대로, 배율 점검이 기준 하한 {BASELINE_FLOOR}에 눌릴 수 있음)",
        "- WorldNormal: 기하 검사 K3(check_continuous_world.py)로 대체",
    ]
    if meta.get("debug_line"):
        lines.append(f"- 시험용 경계(--debug-line {meta['debug_line']}): 실제 지역 경계가 아니므로 판정은 도구 점검용")
    lines += [
        "",
        "| 지점 | 경계 | 유효 단면 | 비율 중앙값 | 비율 최대 | 계단 중앙값 | 기준 중앙값 | 표본 휘도 중앙값 | 폰 중앙 거리(px) | 판정 | 비고 |",
        "|---|---|---|---|---|---|---|---|---|---|---|",
    ]
    for result in results:
        boundary = result["boundary"]
        step_median = "-" if result["step_median"] is None else f"{result['step_median']:.4f}"
        baseline_median = "-" if result["baseline_median"] is None else f"{result['baseline_median']:.4f}"
        luminance_median = "-" if result["sample_luminance_median"] is None else f"{result['sample_luminance_median']:.4f}"
        offset = result["camera_check"]["pawn_center_offset_px"]
        lines.append(
            f"| {result['id']} | {boundary['axis']}={boundary['value_cm']:.0f} | {result['valid_section_count']}/{result['section_count']} | {format_ratio(result['ratio_median'])} | {format_ratio(result['ratio_max'])} "
            f"| {step_median} | {baseline_median} | {luminance_median} | {'-' if offset is None else offset} | {result['verdict']} | {capture.table_cell(result['reason']) or '-'} |"
        )
    if meta["should_self_test"]:
        lines += [
            "",
            f"## 자체 점검(모든 유효 단면에서 경계 +쪽(축 좌표가 큰 쪽) 표본 휘도에 배율을 곱해(그 단면에서 +쪽이 더 밝으면 xf, 어두우면 x1/f로 계단을 키우는 쪽) 비율을 다시 계산, 검출 = 비율 중앙값 > {RATIO_LIMIT})",
            "",
            "| 지점 | 원본 | x1.10 | x1.25 |",
            "|---|---|---|---|",
        ]
        for result in results:
            test = result["self_test"]
            lines.append(f"| {result['id']} | {format_variant(test['original'])} | {format_variant(test['x1.10'])} | {format_variant(test['x1.25'])} |")
    return "\n".join(lines) + "\n"


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description="PIE 지점 촬영에서 지역 경계선 휘도 계단을 재서 K15를 판정한다")
    parser.add_argument("--run", required=True, help="PieShots 아래 run 이름 또는 폴더 경로")
    parser.add_argument("--ids", default="", help="쉼표로 구분한 지점 ID 또는 접두어(기본 B01,B02,B03 전부, --debug-line이면 필수)")
    parser.add_argument("--self-test", action="store_true", help="경계 +쪽 표본 휘도에 단면마다 계단을 키우는 쪽으로 x1.10·x1.25(또는 그 역수)를 곱해 비율이 합격선을 넘는지 민감도 점검")
    parser.add_argument("--out", default=None, help="출력 폴더(기본 run 폴더)")
    parser.add_argument("--debug-line", default=None, help="시험용 경계 axis,value,through_x,through_y(예 y,2000,-11000,2000), 지정한 모든 지점에 적용")
    parser.add_argument("--spots", default=capture.DEFAULT_SPOTS, help="지역 정의가 있는 지점 목록 JSON(기본 Tools/WorldGen/topdown_spots.json)")
    args = parser.parse_args(argv)
    if args.debug_line and not args.ids:
        parser.error("--debug-line needs --ids")
    return args


def main(argv=None):
    capture.configure_output()
    args = parse_args(argv)
    run_dir = find_run_directory(args.run)
    if run_dir is None:
        log(f"run folder not found: {args.run}")
        return 2
    if not os.path.isfile(args.spots):
        log(f"spots file not found: {args.spots}")
        return 2
    regions = capture.read_json(args.spots)["regions"]
    try:
        debug_boundary = parse_debug_line(args.debug_line) if args.debug_line else None
        candidate_ids = list_run_spot_ids(run_dir) if debug_boundary else list(BOUNDARY_SPOTS)
        spot_ids = resolve_spot_ids(args.ids, candidate_ids)
    except ValueError as error:
        log(str(error))
        return 2
    missing = find_missing_inputs(run_dir, spot_ids)
    if missing:
        for line in missing:
            log(line)
        return 2
    out_dir = capture.resolve_project_path(args.out) if args.out else run_dir
    os.makedirs(out_dir, exist_ok=True)
    height_cm = build_height_lookup(regions)
    results = []
    for spot_id in spot_ids:
        record = capture.read_json(os.path.join(run_dir, spot_id + ".json"))
        boundary = debug_boundary or BOUNDARY_SPOTS[spot_id]
        result = analyze_spot(spot_id, boundary, record, run_dir, out_dir, height_cm, args.self_test)
        results.append(result)
        log(f"{spot_id}: {result['verdict']} valid={result['valid_section_count']}/{result['section_count']} ratio_median={format_ratio(result['ratio_median'])} ratio_max={format_ratio(result['ratio_max'])} {result['reason']}")
    meta = {
        "run": os.path.basename(run_dir),
        "created": time.strftime("%Y-%m-%d %H:%M:%S"),
        "debug_line": args.debug_line,
        "should_self_test": args.self_test,
        "limits": {"ratio_limit": RATIO_LIMIT, "min_valid_sections": MIN_VALID_SECTIONS, "min_samples_per_side": MIN_SAMPLES_PER_SIDE},
    }
    capture.write_json(os.path.join(out_dir, "boundary_luminance.json"), dict(meta, spots=results))
    capture.write_text(os.path.join(out_dir, "boundary_luminance.md"), render_markdown(meta["run"], meta, results))
    log(f"report: {os.path.join(out_dir, 'boundary_luminance.md')}")
    return 0 if all(result["verdict"] == PASSED for result in results) else 1


if __name__ == "__main__":
    sys.exit(main())
