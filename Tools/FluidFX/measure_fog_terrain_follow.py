"""실행 환경: 시스템 Python 3(numpy·Pillow), 에디터 불필요(PowerShell 또는 Git Bash).
실행 명령: python -B Tools/FluidFX/measure_fog_terrain_follow.py judge --shots <촬영 폴더> --prereg <사전 등록 JSON> --output <판정 JSON> [--overlay <영역 그림 PNG>] | regions --shots <촬영 폴더> --prereg <사전 등록 JSON> --overlay <영역 그림 PNG> | write-prereg --out <사전 등록 JSON> | --self-test
출력: judge 는 판정 JSON{prereg 경로·sha256, shots 변형별 파일, regions(옆 단면: 능선 열 수·띠 px·평지 행 범위, 돌출물: 기둥 열·행, 판 행·열, 높은·낮은·모자 칸 행), noise_floor, metrics M1~M5{값, 문턱, passed}, records(모자·게임 카메라, 기록만), valid, verdict pass|fail|invalid}을 쓰고 종료 0 통과·1 실패·2 판정 불가(장 없음·크기 다름·영역 규칙 자기 점검 실패). regions 는 숨김 장 두 개로 영역만 정해 그림으로 남기고, write-prereg 는 명세 4.2 문턱과 영역 규칙 값을 새 파일로 쓴다(있으면 거부). --self-test 는 합성 장 fixture(영역 규칙과 M1~M5 각각 통과·실패, 파일 판정 경로)를 돌려 모두 기대와 같으면 종료 0.
상태: 미검증(2026-11-11 Q4-1 2판 작성, --self-test 합성 fixture 만, 실제 촬영 전). 휘도 = RGB 평균/255. 부호 있는 차(M1·M2·M4·M5·모자)는 화소별 채널 평균 차의 평균, 절댓값 차(M3·잡음 바닥)는 채널별 절댓값 평균. 영역은 숨김 장(안개 숨김)에서만 정한다: 옆 단면은 위에서부터 첫 휘도 계단(3 행 차 ≥ 문턱)을 실루엣 윗선으로 보고 ±8 px 안에서 1 px 이하로 평평한 열(판 윗면·지평선, 12 px 넓힘)을 빼고 남은 열 중 윗선 위 띠의 숨김 휘도가 위 5 % 행 중앙값 + 8/255 이하(하늘 앞)인 열을 능선 열로, 평지는 아래에서부터 행 2차 추세 잔차가 고른 행 묶음으로 정한다. 돌출물 시점은 가운데 띠에서 가장 밝은 세로 긴 줄(기둥 동쪽 면)과 그 위 넓은 밝은 줄(판 동쪽 면)을 찾아 기둥 높이 380 cm 로 행을 z 로 바꾸고 기둥 양옆 판 아래 틈을 칸으로 쓴다. 픽셀 값은 1080 행 기준이며 장 높이 비례로 바꾼다.
"""
import argparse
import hashlib
import json
import re
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

SHOT_NAME_PATTERN = re.compile(r"^\d+_(?P<name>.+)_(?P<milliseconds>\d{5})ms\.png$")
VARIANT_ROLES = (
    "side_hidden", "side_off", "side_on", "side_on_repeat",
    "gamecam_off", "gamecam_on",
    "overhang_hidden", "overhang_off", "overhang_on", "overhang_allgeom",
)
DEFAULT_PREREG = {
    "id": "q41-relief",
    "spec": "Saved/AgentOps/20261110/fog-terrain/spec-v2.md 4.2",
    "shots_spec": "Tools/FluidFX/quality/q4_fog_relief.json",
    "tool": "Tools/FluidFX/measure_fog_terrain_follow.py",
    "units": "values in 1/255 units (suffix _255); luminance = mean of R, G, B",
    "procedure": "Metric thresholds are spec-v2 4.2 verbatim and never change after the first relief shot. Regions come only from the two hidden frames by the side_regions and overhang_regions rules. If the rule self-check returns invalid on real hidden frames, revise only those rule values while looking at the hidden frames alone (never on, off, allgeom or gamecam), write a new prereg file and lock, and record the reason.",
    "revision": "v2 2026-11-11, written after the real relief run but before any on, off, allgeom or gamecam frame was viewed or measured. v1 overhang_regions failed on the real overhang hidden frame (plate not found): the lit floor (50-60/255) is above the bright threshold (0.5 x p99.5 = 43/255), so every center column had a long bright floor run and the pillar group became the floor, and the lower half of the pillar east face is in shadow, so the pillar bright run cannot reach the pillar base. Rule values alone could not fix both. v2 changes only the overhang region rule: pillar columns need dark sides (pillar_side_offset_px), the plate is searched up to plate_search_gap_px 10 above the lit pillar run, plate edges use half the plate median luminance (plate_edge_fraction), and the z scale comes from the plate face width (300 cm) with the pillar base at plate bottom + 380 cm instead of the end of the lit pillar run. Metric thresholds, side_regions and all other values are unchanged.",
    "reference_height_px": 1080,
    "variants": {role: "relief_" + role for role in VARIANT_ROLES},
    "noise_floor": {"pair": ["side_on", "side_on_repeat"], "region": "whole_frame", "statistic": "mean_abs_channel"},
    "metrics": {
        "M1": {"band_px": 12, "on_over_off_factor": 3.0, "on_minimum_255": 4.0},
        "M2": {"column_threshold_255": 2.0, "on_max_ratio": 0.1, "off_min_ratio": 0.5},
        "M3": {"noise_factor": 1.5},
        "M4": {"high_z_cm": [150.0, 360.0], "low_z_cm": [0.0, 50.0], "high_minimum_255": 1.0, "high_noise_factor": 1.5, "low_ratio": 0.5},
        "M5": {"high_maximum_255": 2.0},
        "cap_record": {"z_cm": [400.0, 460.0]},
    },
    "side_regions": {
        "smooth_radius_px": 1,
        "edge_step_px": 3,
        "edge_threshold_255": 10.0,
        "flat_half_window_px": 8,
        "flat_tolerance_px": 1,
        "flat_dilate_px": 12,
        "void_reference_rows_fraction": 0.05,
        "void_band_margin_255": 8.0,
        "border_px": 8,
        "min_ridge_columns": 60,
        "floor_trend_degree": 2,
        "floor_spread_max_255": 4.0,
        "floor_guard_px": 16,
        "floor_min_rows": 40,
    },
    "overhang_regions": {
        "smooth_radius_px": 1,
        "center_band_fraction": 0.2,
        "search_rows_fraction": [0.15, 0.85],
        "bright_percentile": 99.5,
        "bright_fraction": 0.5,
        "pillar_min_run_px": 60,
        "plate_width_factor": 3.0,
        "plate_search_gap_px": 10,
        "pillar_side_offset_px": 24,
        "plate_edge_fraction": 0.5,
        "slit_margin_fraction": 0.25,
        "min_slit_px": 3,
        "pillar_height_cm": 380.0,
        "pillar_width_cm": 40.0,
        "plate_face_width_cm": 300.0,
        "plate_thickness_cm": 20.0,
        "shape_tolerance": 0.4,
        "thickness_tolerance": 0.6,
    },
}


class RegionError(Exception):
    pass


def scaled_pixels(value, scale):
    return max(1, int(round(value * scale)))


def luminance(image):
    return image.mean(axis=2)


def box_blur(values, radius):
    if radius <= 0:
        return values
    padded = np.pad(values, radius, mode="edge")
    summed = padded.cumsum(axis=0).cumsum(axis=1)
    summed = np.pad(summed, ((1, 0), (1, 0)))
    size = 2 * radius + 1
    window = summed[size:, size:] - summed[:-size, size:] - summed[size:, :-size] + summed[:-size, :-size]
    return window / float(size * size)


def contiguous_runs(mask):
    padded = np.concatenate([[False], mask, [False]])
    changes = np.flatnonzero(padded[1:] != padded[:-1])
    return list(zip(changes[0::2], changes[1::2] - 1))


def dilate_columns(mask, radius):
    if radius <= 0 or not mask.any():
        return mask.copy()
    padded = np.pad(mask, radius)
    return np.array([padded[index:index + 2 * radius + 1].any() for index in range(mask.size)])


def find_side_regions(hidden, prereg):
    parameters = prereg["side_regions"]
    height, width = hidden.shape[:2]
    scale = height / prereg["reference_height_px"]
    band = scaled_pixels(prereg["metrics"]["M1"]["band_px"], scale)
    step = scaled_pixels(parameters["edge_step_px"], scale)
    border = scaled_pixels(parameters["border_px"], scale)
    smooth = box_blur(luminance(hidden), parameters["smooth_radius_px"])
    edge = np.zeros_like(smooth, dtype=bool)
    edge[step:] = np.abs(smooth[step:] - smooth[:-step]) >= parameters["edge_threshold_255"] / 255.0
    edge[:band + step + 1] = False
    has_edge = edge.any(axis=0)
    top = np.where(has_edge, edge.argmax(axis=0), -1)
    half = scaled_pixels(parameters["flat_half_window_px"], scale)
    flat = np.zeros(width, dtype=bool)
    for column in range(half, width - half):
        window = top[column - half:column + half + 1]
        if (window >= 0).all() and window.max() - window.min() <= parameters["flat_tolerance_px"]:
            flat[column] = True
    excluded = dilate_columns(flat, scaled_pixels(parameters["flat_dilate_px"], scale))
    void_rows = max(1, int(round(parameters["void_reference_rows_fraction"] * height)))
    void_level = float(np.median(smooth[:void_rows]))
    cumulative = np.vstack([np.zeros((1, width)), np.cumsum(smooth, axis=0)])
    safe_top = np.clip(top, band, height)
    band_mean = (cumulative[safe_top, np.arange(width)] - cumulative[safe_top - band, np.arange(width)]) / band
    is_void_band = band_mean <= void_level + parameters["void_band_margin_255"] / 255.0
    ridge = has_edge & ~excluded & is_void_band
    ridge[:border] = False
    ridge[width - border:] = False
    ridge_columns = np.flatnonzero(ridge)
    floor_top, floor_bottom = find_floor_rows(smooth, parameters, scale, border)
    problems = []
    if ridge_columns.size < parameters["min_ridge_columns"] * scale:
        problems.append(f"능선 열 {ridge_columns.size} < {parameters['min_ridge_columns'] * scale:.0f}")
    if floor_bottom - floor_top < parameters["floor_min_rows"] * scale:
        problems.append(f"평지 행 {floor_bottom - floor_top} < {parameters['floor_min_rows'] * scale:.0f}")
    return {
        "band_px": band,
        "ridge_columns": ridge_columns,
        "top_rows": top,
        "flat_columns": int(flat.sum()),
        "void_level_255": void_level * 255.0,
        "non_void_band_columns": int((has_edge & ~excluded & ~is_void_band).sum()),
        "floor_rows": (int(floor_top), int(floor_bottom)),
        "border_px": border,
        "problems": problems,
    }


def find_floor_rows(smooth, parameters, scale, border):
    height, width = smooth.shape
    columns = np.arange(border, width - border)
    positions = (columns - 0.5 * width) / (0.5 * width)
    design = np.vander(positions, parameters["floor_trend_degree"] + 1)
    values = smooth[:, border:width - border]
    fitted = values @ np.linalg.pinv(design).T @ design.T
    residual = values - fitted
    spread = np.percentile(residual, 95, axis=1) - np.percentile(residual, 5, axis=1)
    is_smooth = spread <= parameters["floor_spread_max_255"] / 255.0
    bottom = height - border
    row = bottom - 1
    while row >= 0 and is_smooth[row]:
        row -= 1
    top = min(bottom, row + 1 + scaled_pixels(parameters["floor_guard_px"], scale))
    return top, bottom


def side_band_mask(regions, shape):
    mask = np.zeros(shape[:2], dtype=bool)
    for column in regions["ridge_columns"]:
        top = regions["top_rows"][column]
        mask[top - regions["band_px"]:top, column] = True
    return mask


def side_floor_mask(regions, shape):
    mask = np.zeros(shape[:2], dtype=bool)
    top, bottom = regions["floor_rows"]
    border = regions["border_px"]
    mask[top:bottom, border:shape[1] - border] = True
    return mask


def longest_run(column_mask):
    runs = contiguous_runs(column_mask)
    if not runs:
        return 0, -1, -1
    start, end = max(runs, key=lambda run: run[1] - run[0])
    return end - start + 1, start, end


def bright_segment(bright_row, column):
    if not bright_row[column]:
        return 0, column, column - 1
    left = column
    while left > 0 and bright_row[left - 1]:
        left -= 1
    right = column
    while right < bright_row.size - 1 and bright_row[right + 1]:
        right += 1
    return right - left + 1, left, right


def is_ratio_close(actual, expected, tolerance):
    return abs(actual / expected - 1.0) <= tolerance


def find_overhang_regions(hidden, prereg):
    parameters = prereg["overhang_regions"]
    height, width = hidden.shape[:2]
    scale = height / prereg["reference_height_px"]
    smooth = box_blur(luminance(hidden), parameters["smooth_radius_px"])
    column_start = int(width * (0.5 - parameters["center_band_fraction"]))
    column_end = int(width * (0.5 + parameters["center_band_fraction"]))
    row_start = int(height * parameters["search_rows_fraction"][0])
    row_end = int(height * parameters["search_rows_fraction"][1])
    window = smooth[row_start:row_end, column_start:column_end]
    threshold = parameters["bright_fraction"] * float(np.percentile(window, parameters["bright_percentile"]))
    bright = smooth >= threshold
    side_offset = scaled_pixels(parameters["pillar_side_offset_px"], scale)
    isolated = bright.copy()
    isolated[:, side_offset:] &= ~bright[:, :-side_offset]
    isolated[:, :-side_offset] &= ~bright[:, side_offset:]
    isolated[:, :side_offset] = False
    isolated[:, -side_offset:] = False
    run_lengths = []
    for column in range(column_start, column_end):
        length, _, _ = longest_run(isolated[row_start:row_end, column])
        run_lengths.append(length)
    is_pillar = np.array(run_lengths) >= parameters["pillar_min_run_px"] * scale
    groups = [(column_start + start, column_start + end) for start, end in contiguous_runs(is_pillar)]
    if not groups:
        raise RegionError("기둥(가운데 띠의 양옆이 어두운 세로 밝은 줄) 없음")
    pillar_left, pillar_right = min(groups, key=lambda group: abs(0.5 * (group[0] + group[1]) - 0.5 * width))
    pillar_column = (pillar_left + pillar_right) // 2
    _, run_start, run_end = longest_run(isolated[row_start:row_end, pillar_column])
    run_start += row_start
    run_end += row_start
    pillar_width = pillar_right - pillar_left + 1
    def is_plate_row(row):
        return max(bright_segment(bright[row], column)[0] for column in range(pillar_left, pillar_right + 1)) >= parameters["plate_width_factor"] * pillar_width

    plate_rows = []
    for row in range(run_start, run_end + 1):
        if not is_plate_row(row):
            break
        plate_rows.append(row)
    pillar_top = plate_rows[-1] + 1 if plate_rows else run_start
    if not plate_rows:
        gap_rows = scaled_pixels(parameters["plate_search_gap_px"], scale)
        first_plate_row = next((row for row in range(run_start - 1, max(run_start - 1 - gap_rows, 0) - 1, -1) if is_plate_row(row)), None)
        if first_plate_row is None:
            raise RegionError("판(기둥 위 넓은 밝은 줄) 없음")
        row = first_plate_row
        while row >= 0 and is_plate_row(row):
            plate_rows.insert(0, row)
            row -= 1
    middle_row = plate_rows[len(plate_rows) // 2]
    candidates = [bright_segment(bright[middle_row], column) for column in range(pillar_left, pillar_right + 1)]
    _, plate_left, plate_right = max(candidates, key=lambda segment: segment[0])
    raw = luminance(hidden)
    plate_level = float(np.median(raw[plate_rows[0]:plate_rows[-1] + 1, plate_left:plate_right + 1]))
    is_plate_pixel = raw >= parameters["plate_edge_fraction"] * plate_level
    _, plate_left, plate_right = max((bright_segment(is_plate_pixel[middle_row], column) for column in range(pillar_left, pillar_right + 1)), key=lambda segment: segment[0])
    slit_column = (plate_left + pillar_left) // 2
    plate_top = middle_row
    while plate_top > 0 and is_plate_pixel[plate_top - 1, slit_column]:
        plate_top -= 1
    plate_bottom = middle_row
    while plate_bottom < height - 1 and is_plate_pixel[plate_bottom + 1, slit_column]:
        plate_bottom += 1
    plate_rows = [plate_top, plate_bottom]
    plate_width = plate_right - plate_left + 1
    plate_thickness = plate_rows[-1] - plate_rows[0] + 1
    pixels_per_cm = plate_width / parameters["plate_face_width_cm"]
    pillar_bottom = plate_rows[-1] + int(round(parameters["pillar_height_cm"] * pixels_per_cm))
    problems = []
    if not is_ratio_close(pillar_width / plate_width, parameters["pillar_width_cm"] / parameters["plate_face_width_cm"], parameters["shape_tolerance"]):
        problems.append(f"기둥 폭/판 폭 {pillar_width}/{plate_width}")
    if not is_ratio_close(plate_thickness / plate_width, parameters["plate_thickness_cm"] / parameters["plate_face_width_cm"], parameters["thickness_tolerance"]):
        problems.append(f"판 두께/판 폭 {plate_thickness}/{plate_width}")
    if run_end > pillar_bottom + scaled_pixels(parameters["plate_search_gap_px"], scale):
        problems.append(f"기둥 밝은 줄이 판 폭 축척의 기둥 밑 아래로 내려감 {run_end}>{pillar_bottom}")
    margin = max(1, int(round(parameters["slit_margin_fraction"] * pillar_width)))
    slits = [(plate_left + margin, pillar_left - margin), (pillar_right + margin, plate_right - margin)]
    minimum_slit = scaled_pixels(parameters["min_slit_px"], scale)
    if any(right - left + 1 < minimum_slit for left, right in slits):
        problems.append(f"틈 폭 부족 {slits}")
    metrics = prereg["metrics"]

    def rows_for(z_range):
        return (int(round(pillar_bottom + 1 - z_range[1] * pixels_per_cm)), int(round(pillar_bottom + 1 - z_range[0] * pixels_per_cm)))

    return {
        "pillar_columns": (int(pillar_left), int(pillar_right)),
        "pillar_rows": (int(pillar_top), int(pillar_bottom)),
        "plate_rows": (int(plate_rows[0]), int(plate_rows[-1])),
        "plate_columns": (int(plate_left), int(plate_right)),
        "slit_columns": [(int(left), int(right)) for left, right in slits],
        "pixels_per_cm": pixels_per_cm,
        "high_rows": rows_for(metrics["M4"]["high_z_cm"]),
        "low_rows": rows_for(metrics["M4"]["low_z_cm"]),
        "cap_rows": rows_for(metrics["cap_record"]["z_cm"]),
        "bright_threshold_255": threshold * 255.0,
        "problems": problems,
    }


def overhang_cell_mask(regions, shape, rows):
    mask = np.zeros(shape[:2], dtype=bool)
    top, bottom = max(rows[0], 0), min(rows[1], shape[0])
    for left, right in regions["slit_columns"]:
        mask[top:bottom, left:right + 1] = True
    return mask


def overhang_cap_mask(regions, shape):
    mask = np.zeros(shape[:2], dtype=bool)
    top, bottom = max(regions["cap_rows"][0], 0), min(regions["cap_rows"][1], shape[0])
    left, right = regions["plate_columns"]
    mask[top:bottom, left:right + 1] = True
    return mask


def signed_mean(frame, reference, mask):
    return float((luminance(frame) - luminance(reference))[mask].mean())


def absolute_mean(frame, reference, mask=None):
    difference = np.abs(frame - reference).mean(axis=2)
    return float(difference[mask].mean() if mask is not None else difference.mean())


def column_contributions(frame, hidden, regions):
    difference = luminance(frame) - luminance(hidden)
    band = regions["band_px"]
    return np.array([difference[regions["top_rows"][column] - band:regions["top_rows"][column], column].mean() for column in regions["ridge_columns"]])


def measure_frames(frames, prereg):
    metrics = prereg["metrics"]
    shapes = {frame.shape for frame in frames.values()}
    if len(shapes) != 1:
        return {"valid": False, "verdict": "invalid", "problems": [f"장 크기 다름 {sorted(shapes)}"]}
    result = {"valid": True, "problems": [], "regions": {}, "metrics": {}, "records": {}}
    try:
        side = find_side_regions(frames["side_hidden"], prereg)
    except RegionError as error:
        side = {"problems": [str(error)]}
    try:
        overhang = find_overhang_regions(frames["overhang_hidden"], prereg)
    except RegionError as error:
        overhang = {"problems": [str(error)]}
    result["problems"] = [f"옆 단면: {problem}" for problem in side["problems"]] + [f"돌출물: {problem}" for problem in overhang["problems"]]
    result["regions"] = {"side": summarize_side_regions(side), "overhang": {key: value for key, value in overhang.items() if key != "problems"}}
    if result["problems"]:
        result["valid"] = False
        result["verdict"] = "invalid"
        return result

    noise = absolute_mean(frames[prereg["noise_floor"]["pair"][0]], frames[prereg["noise_floor"]["pair"][1]])
    result["noise_floor"] = noise
    shape = frames["side_hidden"].shape
    band_mask = side_band_mask(side, shape)
    on_band = signed_mean(frames["side_on"], frames["side_hidden"], band_mask)
    off_band = signed_mean(frames["side_off"], frames["side_hidden"], band_mask)
    m1 = metrics["M1"]
    result["metrics"]["M1"] = {
        "on_minus_hidden_255": on_band * 255.0,
        "off_minus_hidden_255": off_band * 255.0,
        "rule": f"on - hidden >= {m1['on_over_off_factor']} x (off - hidden) and >= {m1['on_minimum_255']}/255",
        "passed": on_band >= m1["on_over_off_factor"] * off_band and on_band >= m1["on_minimum_255"] / 255.0,
    }
    m2 = metrics["M2"]
    on_ratio = float((column_contributions(frames["side_on"], frames["side_hidden"], side) < m2["column_threshold_255"] / 255.0).mean())
    off_ratio = float((column_contributions(frames["side_off"], frames["side_hidden"], side) < m2["column_threshold_255"] / 255.0).mean())
    result["metrics"]["M2"] = {
        "on_buried_ratio": on_ratio,
        "off_buried_ratio": off_ratio,
        "rule": f"buried column = band contribution < {m2['column_threshold_255']}/255; on <= {m2['on_max_ratio']}, off >= {m2['off_min_ratio']}",
        "passed": on_ratio <= m2["on_max_ratio"] and off_ratio >= m2["off_min_ratio"],
    }
    floor_mask = side_floor_mask(side, shape)
    on_off = absolute_mean(frames["side_on"], frames["side_off"], floor_mask)
    on_repeat = absolute_mean(frames["side_on"], frames["side_on_repeat"], floor_mask)
    m3 = metrics["M3"]
    result["metrics"]["M3"] = {
        "on_off_abs_255": on_off * 255.0,
        "on_repeat_abs_255": on_repeat * 255.0,
        "rule": f"|on - off| <= {m3['noise_factor']} x |on - on_repeat| (flat rows)",
        "passed": on_off <= m3["noise_factor"] * on_repeat,
    }
    m4 = metrics["M4"]
    high_mask = overhang_cell_mask(overhang, shape, overhang["high_rows"])
    low_mask = overhang_cell_mask(overhang, shape, overhang["low_rows"])
    high_on_off = signed_mean(frames["overhang_on"], frames["overhang_off"], high_mask)
    high_limit = max(m4["high_minimum_255"] / 255.0, m4["high_noise_factor"] * noise)
    low_on = signed_mean(frames["overhang_on"], frames["overhang_hidden"], low_mask)
    low_off = signed_mean(frames["overhang_off"], frames["overhang_hidden"], low_mask)
    result["metrics"]["M4"] = {
        "high_on_minus_off_255": high_on_off * 255.0,
        "high_limit_255": high_limit * 255.0,
        "low_on_minus_hidden_255": low_on * 255.0,
        "low_off_minus_hidden_255": low_off * 255.0,
        "low_reference_visible": low_off >= 1.0 / 255.0,
        "rule": f"high: mean(on - off) <= max({m4['high_minimum_255']}/255, {m4['high_noise_factor']} x noise); low: (on - hidden) >= {m4['low_ratio']} x (off - hidden)",
        "passed": high_on_off <= high_limit and low_on >= m4["low_ratio"] * low_off,
    }
    m5 = metrics["M5"]
    high_allgeom = signed_mean(frames["overhang_allgeom"], frames["overhang_hidden"], high_mask)
    result["metrics"]["M5"] = {
        "high_allgeom_minus_hidden_255": high_allgeom * 255.0,
        "rule": f"high: mean(allgeom - hidden) <= {m5['high_maximum_255']}/255",
        "passed": high_allgeom <= m5["high_maximum_255"] / 255.0,
    }
    cap_mask = overhang_cap_mask(overhang, shape)
    result["records"]["cap"] = {
        "allgeom_minus_hidden_255": signed_mean(frames["overhang_allgeom"], frames["overhang_hidden"], cap_mask) * 255.0 if cap_mask.any() else None,
        "on_minus_hidden_255": signed_mean(frames["overhang_on"], frames["overhang_hidden"], cap_mask) * 255.0 if cap_mask.any() else None,
    }
    result["records"]["gamecam_on_off_abs_255"] = absolute_mean(frames["gamecam_on"], frames["gamecam_off"]) * 255.0
    result["verdict"] = "pass" if all(item["passed"] for item in result["metrics"].values()) else "fail"
    return result


def summarize_side_regions(side):
    if "ridge_columns" not in side:
        return {}
    columns = side["ridge_columns"]
    return {
        "band_px": side["band_px"],
        "ridge_column_count": int(columns.size),
        "ridge_column_runs": [[int(start), int(end)] for start, end in group_index_runs(columns)],
        "flat_column_count": side["flat_columns"],
        "void_level_255": side["void_level_255"],
        "non_void_band_columns": side["non_void_band_columns"],
        "floor_rows": list(side["floor_rows"]),
    }


def group_index_runs(indices):
    if indices.size == 0:
        return []
    breaks = np.flatnonzero(np.diff(indices) > 1)
    starts = np.concatenate([[indices[0]], indices[breaks + 1]])
    ends = np.concatenate([indices[breaks], [indices[-1]]])
    return list(zip(starts, ends))


def find_shot_files(folder, variants):
    names = {}
    for path in sorted(Path(folder).glob("*.png")):
        match = SHOT_NAME_PATTERN.match(path.name)
        if match:
            names.setdefault(match.group("name"), []).append(path)
    found, problems = {}, []
    for role, name in variants.items():
        paths = names.get(name, [])
        if len(paths) != 1:
            problems.append(f"{name}: 장 {len(paths)}개(1개여야 함)")
            continue
        found[role] = paths[0]
    return found, problems


def load_frame(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float64) / 255.0


def read_prereg(path):
    raw = Path(path).read_bytes()
    return json.loads(raw.decode("utf-8")), hashlib.sha256(raw).hexdigest()


def draw_overlay(frames, result, overlay_path):
    panels = []
    side = result["regions"].get("side", {})
    side_frame = (np.clip(frames["side_hidden"] * 4.0, 0.0, 1.0) * 255.0).astype(np.uint8)
    if side.get("ridge_column_count"):
        regions = find_side_regions(frames["side_hidden"], result["prereg_values"])
        side_frame[side_band_mask(regions, side_frame.shape)] = (255, 60, 60)
        floor = side_floor_mask(regions, side_frame.shape)
        side_frame[floor] = (side_frame[floor] * 0.5 + np.array([0, 120, 0])).astype(np.uint8)
    panels.append(side_frame)
    overhang_frame = (np.clip(frames["overhang_hidden"] * 2.0, 0.0, 1.0) * 255.0).astype(np.uint8)
    overhang = result["regions"].get("overhang", {})
    if "high_rows" in overhang:
        for rows, color in ((overhang["high_rows"], (255, 60, 60)), (overhang["low_rows"], (60, 120, 255))):
            overhang_frame[overhang_cell_mask(overhang, overhang_frame.shape, rows)] = color
        overhang_frame[overhang_cap_mask(overhang, overhang_frame.shape)] = (255, 0, 255)
    panels.append(overhang_frame)
    Path(overlay_path).parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(np.concatenate(panels, axis=1)).save(overlay_path)


def judge_folder(shots_folder, prereg_path, output_path, overlay_path, regions_only=False):
    prereg, prereg_sha = read_prereg(prereg_path)
    variants = prereg["variants"]
    if regions_only:
        variants = {role: variants[role] for role in ("side_hidden", "overhang_hidden")}
    files, problems = find_shot_files(shots_folder, variants)
    if problems:
        print(json.dumps({"verdict": "invalid", "problems": problems}, ensure_ascii=False))
        return 2
    frames = {role: load_frame(path) for role, path in files.items()}
    if regions_only:
        result = {"regions": {}, "problems": []}
        for role, finder, summarize in (("side_hidden", find_side_regions, summarize_side_regions), ("overhang_hidden", find_overhang_regions, None)):
            try:
                regions = finder(frames[role], prereg)
            except RegionError as error:
                result["problems"].append(f"{role}: {error}")
                continue
            result["problems"].extend(f"{role}: {problem}" for problem in regions["problems"])
            result["regions"][role.split("_")[0]] = summarize(regions) if summarize else {key: value for key, value in regions.items() if key != "problems"}
    else:
        result = measure_frames(frames, prereg)
    result["prereg"] = Path(prereg_path).as_posix()
    result["prereg_sha256"] = prereg_sha
    result["shots"] = {role: path.as_posix() for role, path in files.items()}
    if overlay_path:
        result["prereg_values"] = prereg
        draw_overlay(frames, result, overlay_path)
        result.pop("prereg_values")
        result["overlay"] = Path(overlay_path).as_posix()
    if output_path:
        Path(output_path).parent.mkdir(parents=True, exist_ok=True)
        Path(output_path).write_text(json.dumps(result, ensure_ascii=False, indent=1, default=int) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps({key: result.get(key) for key in ("verdict", "problems")} | {name: item.get("passed") for name, item in result.get("metrics", {}).items()}, ensure_ascii=False))
    if regions_only:
        return 0 if not result["problems"] else 2
    return {"pass": 0, "fail": 1}.get(result["verdict"], 2)


def write_prereg(out_path):
    path = Path(out_path)
    if path.exists():
        print(f"이미 있음(사전 등록은 덮어쓰지 않는다): {path.as_posix()}")
        return 2
    path.parent.mkdir(parents=True, exist_ok=True)
    data = (json.dumps(DEFAULT_PREREG, ensure_ascii=False, indent=1) + "\n").encode("utf-8")
    path.write_bytes(data)
    print(json.dumps({"written": path.as_posix(), "sha256": hashlib.sha256(data).hexdigest()}, ensure_ascii=False))
    return 0


def gray_frame(values):
    channel = np.round(np.clip(values, 0.0, 1.0) * 255.0) / 255.0
    return np.repeat(channel[:, :, None], 3, axis=2)


def make_side_scene(height, width):
    rows = np.arange(height, dtype=np.float64)[:, None]
    columns = np.arange(width, dtype=np.float64)[None, :]
    horizon = int(0.42 * height)
    hill_base = horizon + int(0.12 * height)
    values = np.where(rows < horizon, 0.004 + 0.01 * rows / horizon, (0.05 + 0.03 * (rows - horizon) / (height - horizon)) * (1.0 - 0.25 * ((columns - 0.5 * width) / (0.5 * width)) ** 2))
    tops = np.full(width, -1.0)
    hills = ((0.3 * width, 0.18 * height, 0.15 * width, 0.20), (0.62 * width, 0.08 * height, 0.11 * width, 0.24))
    for apex_column, apex_row, half_width, value in hills:
        outline = apex_row + (hill_base - apex_row) / half_width * np.abs(columns[0] - apex_column)
        inside_columns = np.abs(columns[0] - apex_column) <= half_width
        mask = (rows >= outline[None, :]) & (rows <= hill_base) & inside_columns[None, :]
        values = np.where(mask, value, values)
        tops = np.where(inside_columns & ((tops < 0) | (outline < tops)), outline, tops)
    plate = (int(0.30 * height), int(0.30 * height) + 6, int(0.80 * width), int(0.86 * width))
    values[plate[0]:plate[1], plate[2]:plate[3]] = 0.22
    return values, tops, horizon, hill_base, plate


def add_ridge_fog(values, tops, columns_mask, thickness, amount):
    fogged = values.copy()
    for column in np.flatnonzero(columns_mask & (tops > 0)):
        top = int(np.ceil(tops[column]))
        fogged[max(top - thickness, 0):top, column] += amount
    return fogged


def make_side_frames(height=540, width=960, on_amount=0.05, on_columns="all", off_amount=0.0, off_floor_shift=0.0):
    hidden, tops, horizon, _, _ = make_side_scene(height, width)
    floor_fog = np.zeros_like(hidden)
    floor_fog[horizon:] = 0.06
    columns = np.arange(width)
    first_hill = np.abs(columns - 0.3 * width) <= 0.15 * width
    on_mask = np.ones(width, dtype=bool) if on_columns == "all" else first_hill
    thickness = max(1, int(round(8 * height / 1080)))
    on = add_ridge_fog(hidden + floor_fog, tops, on_mask, thickness, on_amount)
    off = add_ridge_fog(hidden + floor_fog, tops, np.ones(width, dtype=bool), thickness, off_amount)
    off[horizon:] += off_floor_shift
    noise = np.random.default_rng(20261111).normal(0.0, 0.002, size=hidden.shape)
    return {"side_hidden": gray_frame(hidden), "side_on": gray_frame(on), "side_off": gray_frame(off), "side_on_repeat": gray_frame(on + noise)}


def make_overhang_scene(height=540, width=960, has_pillar=True):
    values = np.full((height, width), 0.004)
    values[int(0.56 * height):] = 0.03
    plate_top, plate_bottom = 244, 248
    pillar_top, pillar_bottom = 250, 344
    pillar_left, pillar_right = 475, 484
    plate_left, plate_right = 442, 516
    values[plate_top:plate_bottom + 1, plate_left:plate_right + 1] = 0.55
    if has_pillar:
        values[pillar_top:pillar_bottom + 1, pillar_left:pillar_right + 1] = 0.5
    values[200:330, 300:341] = 0.6
    geometry = {"plate_rows": (plate_top, plate_bottom), "pillar_rows": (pillar_top, pillar_bottom), "pillar_columns": (pillar_left, pillar_right), "plate_columns": (plate_left, plate_right)}
    return values, geometry


def make_overhang_shadow_scene(height=540, width=960):
    values = np.full((height, width), 0.004)
    values[230:249] = 0.06
    values[249:256] = 0.23
    values[256:261] = 0.0
    for row in range(261, height):
        values[row, :min(width, int(440 + (row - 261) * 1.6))] = 0.22
    for column in range(620, 811):
        values[int(270 - (column - 620) * 0.47):271, column] = 0.25
    plate_top, plate_bottom = 169, 173
    plate_left, plate_right = 438, 507
    pillar_left, pillar_right = 466, 475
    pillar_lit_top, pillar_lit_bottom = 178, 222
    pillar_bottom = plate_bottom + int(round(380.0 * (plate_right - plate_left + 1) / 300.0))
    values[plate_top:plate_bottom + 1, plate_left:plate_right + 1] = 0.42
    values[pillar_lit_top:pillar_lit_bottom + 1, pillar_left:pillar_right + 1] = 0.41
    values[pillar_lit_bottom + 1:pillar_bottom + 1, pillar_left:pillar_right + 1] = 0.0
    geometry = {"plate_rows": (plate_top, plate_bottom), "pillar_rows": (pillar_lit_top, pillar_bottom), "pillar_columns": (pillar_left, pillar_right), "plate_columns": (plate_left, plate_right)}
    return values, geometry


def make_overhang_frames(on_high=0.0, on_low=0.06, allgeom_high=0.0):
    hidden, geometry = make_overhang_scene()
    pixels_per_cm = (geometry["pillar_rows"][1] - geometry["pillar_rows"][0] + 1) / 380.0
    bottom = geometry["pillar_rows"][1] + 1

    def with_cells(high, low):
        fogged = hidden.copy()
        for left, right in ((geometry["plate_columns"][0], geometry["pillar_columns"][0] - 1), (geometry["pillar_columns"][1] + 1, geometry["plate_columns"][1])):
            fogged[int(bottom - 360 * pixels_per_cm):int(bottom - 150 * pixels_per_cm), left:right + 1] += high
            fogged[int(bottom - 50 * pixels_per_cm):bottom, left:right + 1] += low
        return fogged

    return {
        "overhang_hidden": gray_frame(hidden),
        "overhang_off": gray_frame(with_cells(0.0, 0.06)),
        "overhang_on": gray_frame(with_cells(on_high, on_low)),
        "overhang_allgeom": gray_frame(with_cells(allgeom_high, 0.0)),
    }, geometry


def make_gamecam_frames():
    base = np.full((540, 960), 0.1)
    return {"gamecam_off": gray_frame(base), "gamecam_on": gray_frame(base + 0.01)}


def fixture_frames(side=None, overhang=None):
    frames = make_side_frames(**(side or {}))
    overhang_frames, _ = make_overhang_frames(**(overhang or {}))
    return frames | overhang_frames | make_gamecam_frames()


def run_self_test():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    prereg = DEFAULT_PREREG
    outcomes = []

    def record(label, passed, detail):
        outcomes.append(passed)
        print(("PASS " if passed else "FAIL ") + label + ": " + json.dumps(detail, ensure_ascii=False, default=str))

    hidden, tops, horizon, hill_base, plate = make_side_scene(540, 960)
    side = find_side_regions(gray_frame(hidden), prereg)
    ridge = side["ridge_columns"]
    plate_columns = set(range(plate[2], plate[3]))
    sky_hill_columns = set(np.flatnonzero((tops >= 0) & (tops < horizon - 2)).tolist())
    ground_hill_columns = set(np.flatnonzero((tops >= 0) & (tops > horizon + side["band_px"])).tolist())
    ridge_set = set(ridge.tolist())
    record("R1 옆 단면 능선 열: 하늘 앞 언덕 열 대부분 포함·판 윗면 열과 바닥 앞 언덕 열 제외·평지 행은 언덕 밑 아래", not plate_columns & ridge_set and not ground_hill_columns & ridge_set and len(ridge_set & sky_hill_columns) >= 0.8 * len(sky_hill_columns) and side["floor_rows"][0] > hill_base and not side["problems"], {"ridge": int(ridge.size), "sky_hill_columns": len(sky_hill_columns), "ground_hill_columns": len(ground_hill_columns), "floor_rows": side["floor_rows"], "problems": side["problems"]})
    overhang_frames, geometry = make_overhang_frames()
    overhang = find_overhang_regions(overhang_frames["overhang_hidden"], prereg)
    found = {key: overhang[key] for key in geometry}
    close = all(abs(found[key][index] - geometry[key][index]) <= 2 for key in geometry for index in (0, 1))
    record("R2 돌출물 기둥·판 찾기(왼쪽 밝은 언덕 덩어리 무시)", close and not overhang["problems"], {"found": found, "drawn": geometry, "problems": overhang["problems"]})
    shadow_scene, shadow_geometry = make_overhang_shadow_scene()
    try:
        shadow = find_overhang_regions(gray_frame(shadow_scene), prereg)
        shadow_found = {key: shadow[key] for key in shadow_geometry}
        shadow_close = all(abs(shadow_found[key][index] - shadow_geometry[key][index]) <= 2 for key in shadow_geometry for index in (0, 1))
        shadow_detail = {"found": shadow_found, "drawn": shadow_geometry, "problems": shadow["problems"]}
        shadow_passed = shadow_close and not shadow["problems"]
    except RegionError as error:
        shadow_detail = {"error": str(error), "drawn": shadow_geometry}
        shadow_passed = False
    record("R2b 밝은 바닥·기둥 아래쪽 그림자(실제 숨김 장 형태): 기둥·판 찾기, 기둥 밑은 판 폭 축척", shadow_passed, shadow_detail)
    no_pillar, _ = make_overhang_scene(has_pillar=False)
    invalid = measure_frames(fixture_frames() | {"overhang_hidden": gray_frame(no_pillar)}, prereg)
    record("R3 기둥 없는 숨김 장 → 판정 불가", invalid["verdict"] == "invalid", invalid["problems"])

    passing = measure_frames(fixture_frames(), prereg)
    record("기준 통과 세트 M1~M5 모두 통과", passing["verdict"] == "pass", {name: {key: value for key, value in item.items() if key != "rule"} for name, item in passing["metrics"].items()})
    cases = (
        ("M1 실패: 켬 능선 안개 0.01(2.6/255 < 4/255)", {"side": {"on_amount": 0.01}}, "M1"),
        ("M2 실패: 켬 능선 안개가 언덕 하나에만", {"side": {"on_columns": "first", "on_amount": 0.1}}, "M2"),
        ("M3 실패: 끔 평지만 +0.02 밝음", {"side": {"off_floor_shift": 0.02}}, "M3"),
        ("M4 실패(기둥): 켬 판 아래 높은 칸 +0.05", {"overhang": {"on_high": 0.05}}, "M4"),
        ("M4 실패(판 밑 흐름 없음): 켬 낮은 칸 안개 0", {"overhang": {"on_low": 0.0}}, "M4"),
        ("M5 실패: allgeom 높은 칸 +0.05", {"overhang": {"allgeom_high": 0.05}}, "M5"),
    )
    for label, overrides, metric in cases:
        outcome = measure_frames(fixture_frames(overrides.get("side"), overrides.get("overhang")), prereg)
        others = [name for name, item in outcome["metrics"].items() if name != metric and not item["passed"]]
        record(label, outcome["verdict"] == "fail" and not outcome["metrics"][metric]["passed"], {"metric": outcome["metrics"][metric], "other_failed": others})

    with tempfile.TemporaryDirectory() as folder:
        work = Path(folder)
        prereg_path = work / "prereg.json"
        write_prereg(prereg_path)
        for index, (role, frame) in enumerate(fixture_frames().items()):
            Image.fromarray((frame * 255.0).round().astype(np.uint8)).save(work / f"{index:02d}_{DEFAULT_PREREG['variants'][role]}_08000ms.png")
        Image.fromarray(np.zeros((540, 960, 3), dtype=np.uint8)).save(work / "grid.png")
        code = judge_folder(work, prereg_path, work / "judge.json", work / "overlay.png")
        record("파일 판정 경로(이름 규칙 relief_side_on 과 relief_side_on_repeat 구분, 사전 등록 sha256 기록)", code == 0 and json.loads((work / "judge.json").read_text(encoding="utf-8"))["prereg_sha256"] == hashlib.sha256(prereg_path.read_bytes()).hexdigest(), {"exit": code})
        next(work.glob("*_relief_side_off_08000ms.png")).unlink()
        record("장 하나 없음 → 종료 2", judge_folder(work, prereg_path, None, None) == 2, {})
    print(json.dumps({"passed": sum(outcomes), "failed": len(outcomes) - sum(outcomes)}, ensure_ascii=False))
    return 0 if all(outcomes) else 1


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if "--self-test" in sys.argv[1:]:
        return run_self_test()
    parser = argparse.ArgumentParser(description="Q4-1 지형 따라가기 안개 바닥 화면 판정(spec-v2 4.2 M1~M5)")
    commands = parser.add_subparsers(dest="command", required=True)
    judge_parser = commands.add_parser("judge")
    judge_parser.add_argument("--shots", required=True)
    judge_parser.add_argument("--prereg", required=True)
    judge_parser.add_argument("--output", required=True)
    judge_parser.add_argument("--overlay")
    regions_parser = commands.add_parser("regions")
    regions_parser.add_argument("--shots", required=True)
    regions_parser.add_argument("--prereg", required=True)
    regions_parser.add_argument("--overlay", required=True)
    prereg_parser = commands.add_parser("write-prereg")
    prereg_parser.add_argument("--out", required=True)
    arguments = parser.parse_args()
    if arguments.command == "write-prereg":
        return write_prereg(arguments.out)
    if arguments.command == "regions":
        return judge_folder(arguments.shots, arguments.prereg, None, arguments.overlay, regions_only=True)
    return judge_folder(arguments.shots, arguments.prereg, arguments.output, arguments.overlay)


if __name__ == "__main__":
    sys.exit(main())
