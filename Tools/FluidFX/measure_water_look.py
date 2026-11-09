"""실행 환경: 시스템 Python(numpy·scipy·Pillow, 에디터 불필요, PowerShell 또는 Git Bash).
실행 명령: python -B Tools/FluidFX/measure_water_look.py --images <png…> [--boxes <json>] [--rows near|all] [--crop x0 y0 x1 y1] --output <json> | --self-test
출력: --output JSON(images[]: pixel_scale = 장 높이 ÷ 720·frame{빈 곳 %, 띠 휘도 far 8~30%·middle 30~60%·near 60~100% 행, 먼 띠 B/G − 가까운 띠 B/G}·region{--rows 행(near = 아래 30%, all = 전체)의 물 평균 RGB·G/R·B/R, 대역 표준편차 low σ24·mid σ3−σ24·high L−σ3, 바닥 대역 σ12−σ72, 반짝임 휘도>0.9 비율·덩어리 중앙 면적, 초승달 = 720p 기준 면적 20~400 px 부유물 덩어리의 4 px 고리 안 흰 화소(면적·고리·흰 화소 3개 문턱을 pixel_scale 로 늘리고 평균은 720p 환산)}·boxes{이름: 흰 %, 채움비 = 흰 ÷ closing 4회 뒤 구멍 메운 영역(구멍 메운 영역의 25%를 넘는 구멍 = 고리 안쪽 맑은 물은 메우지 않음), 최대 덩어리 몫}, temporal[] 같은 촬영 묶음(<묶음>_<N>ms.png)의 시각순 이웃 장 |ΔG(σ8)|·|Δ(L−G(σ2))| 평균과 step_ms(이름에 _<N>ms 가 없는 장은 쌍을 만들지 않음), summary 장 평균(temporal 은 step 250 ms 쌍만), skipped_images(grid.png 격자는 재지 않음)), --self-test 는 합성 fixture 11개 판정을 찍고 모두 맞으면 종료 0
상태: 실험(roadmap-v2 2.4 T2). 정의: sRGB 값/255, 휘도 = 0.2126R+0.7152G+0.0722B, 마스크 우선순위 빈 곳(최대 채널<0.03) > 부유물(R>1.2G 그리고 R>0.25) > 흰빛(HSV 채도<0.3 그리고 휘도>0.5) > 물. 대역·시간 필터는 물 화소만 쓰는 정규화 가우시안 G(L·물)/G(물)이라 물 아닌 화소 값이 물 쪽으로 새지 않는다. 상자 JSON = {"image_size": [w, h], "regions": [{name, box}], "variants": {파일 이름에 든 열쇠: [{name, box}]}}, box 는 image_size 기준 px [x0, y0, x1, y1] 이고 장 크기에 맞춰 늘린다(가장 긴 열쇠 하나만 고름). --crop 은 frame·region 을 잘라낸 영역에서만 내고 상자는 전체 장 기준이다."""
import argparse
import json
import math
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

LUMINANCE_WEIGHTS = np.array([0.2126, 0.7152, 0.0722])
VOID_MAX_CHANNEL = 0.03
DEBRIS_RED_OVER_GREEN = 1.2
DEBRIS_MIN_RED = 0.25
WHITE_MAX_SATURATION = 0.3
WHITE_MIN_LUMINANCE = 0.5
SPARKLE_MIN_LUMINANCE = 0.9
NEAR_ROW_START_FRACTION = 0.7
LUMINANCE_BAND_ROWS = {"far": (0.08, 0.30), "middle": (0.30, 0.60), "near": (0.60, 1.00)}
LOW_SIGMA_PX = 24.0
HIGH_SIGMA_PX = 3.0
TEMPORAL_LOW_SIGMA_PX = 8.0
TEMPORAL_HIGH_SIGMA_PX = 2.0
FLOOR_FINE_SIGMA_PX = 12.0
FLOOR_COARSE_SIGMA_PX = 72.0
MIN_FILTER_WEIGHT = 1e-12
BOX_CLOSING_ITERATIONS = 4
BOX_MAX_FILLED_HOLE_SHARE = 0.25
REFERENCE_HEIGHT_PX = 720.0
CRESCENT_MIN_AREA_PX = 20
CRESCENT_MAX_AREA_PX = 400
CRESCENT_RING_PX = 4
CRESCENT_MIN_WHITE_PIXELS = 3
TEMPORAL_STEP_MS = 250
LOOKDEV_GRID_FILE_NAME = "grid.png"
TIMED_FRAME_PATTERN = re.compile(r"^(?P<series>.+)_(?P<milliseconds>\d+)ms$")
EIGHT_CONNECTED = np.ones((3, 3), dtype=bool)
SUMMARY_SKIPPED_KEYS = ("box_px", "size", "pixel_count")
SELF_TEST_WATER_RGB = np.array([0.10, 0.35, 0.32])
SELF_TEST_DEBRIS_RGB = np.array([1.0, 0.45, 0.05])


def safe_ratio(numerator, denominator):
    return float(numerator) / float(denominator) if denominator else None


def load_rgb(path):
    with Image.open(path) as image:
        return np.asarray(image.convert("RGB"), dtype=np.float64) / 255.0


def classify_pixels(rgb):
    red, green = rgb[..., 0], rgb[..., 1]
    maximum = rgb.max(axis=2)
    minimum = rgb.min(axis=2)
    luminance = rgb @ LUMINANCE_WEIGHTS
    saturation = np.where(maximum > 0.0, (maximum - minimum) / np.maximum(maximum, 1e-12), 0.0)
    void = maximum < VOID_MAX_CHANNEL
    debris = ~void & (red > DEBRIS_RED_OVER_GREEN * green) & (red > DEBRIS_MIN_RED)
    white = ~void & ~debris & (saturation < WHITE_MAX_SATURATION) & (luminance > WHITE_MIN_LUMINANCE)
    water = ~void & ~debris & ~white
    return {"luminance": luminance, "void": void, "debris": debris, "white": white, "water": water}


def slice_classes(classes, rows):
    return {name: mask[rows] for name, mask in classes.items()}


def masked_gaussian(luminance, water, sigma):
    weight = ndimage.gaussian_filter(water.astype(np.float64), sigma)
    blurred = ndimage.gaussian_filter(np.where(water, luminance, 0.0), sigma)
    return np.divide(blurred, weight, out=np.zeros_like(blurred), where=weight > MIN_FILTER_WEIGHT)


def water_deviation(values, water):
    return float(values[water].std()) if water.any() else None


def measure_bands(luminance, water):
    low = masked_gaussian(luminance, water, LOW_SIGMA_PX)
    fine = masked_gaussian(luminance, water, HIGH_SIGMA_PX)
    return {"low": water_deviation(low, water), "mid": water_deviation(fine - low, water), "high": water_deviation(luminance - fine, water)}


def measure_floor_band(luminance, water):
    difference = masked_gaussian(luminance, water, FLOOR_FINE_SIGMA_PX) - masked_gaussian(luminance, water, FLOOR_COARSE_SIGMA_PX)
    return water_deviation(difference, water)


def measure_sparkle(luminance, water, white):
    candidate = water | white
    sparkle = candidate & (luminance > SPARKLE_MIN_LUMINANCE)
    labels, component_count = ndimage.label(sparkle, structure=EIGHT_CONNECTED)
    areas = np.bincount(labels.ravel())[1:] if component_count else np.array([])
    ratio = safe_ratio(sparkle.sum(), candidate.sum())
    return {"ratio": ratio, "percent": None if ratio is None else 100.0 * ratio, "component_count": int(component_count),
            "median_area_px": float(np.median(areas)) if component_count else None}


def disk_structure(radius):
    offsets = np.arange(-radius, radius + 1)
    return (offsets[:, None] ** 2 + offsets[None, :] ** 2) <= radius ** 2


def padded_window(slices, shape, margin):
    rows = slice(max(slices[0].start - margin, 0), min(slices[0].stop + margin, shape[0]))
    columns = slice(max(slices[1].start - margin, 0), min(slices[1].stop + margin, shape[1]))
    return rows, columns


def measure_crescents(debris, white, pixel_scale):
    empty = {"blob_count": 0, "mean_white_in_ring": None, "ratio_three_or_more": None}
    labels, blob_count = ndimage.label(debris, structure=EIGHT_CONNECTED)
    if not blob_count:
        return empty
    area_scale = pixel_scale ** 2
    ring_px = max(1, int(round(CRESCENT_RING_PX * pixel_scale)))
    ring_structure = disk_structure(ring_px)
    areas = np.bincount(labels.ravel())
    white_counts = []
    for index, slices in enumerate(ndimage.find_objects(labels), start=1):
        if slices is None or not CRESCENT_MIN_AREA_PX * area_scale <= areas[index] <= CRESCENT_MAX_AREA_PX * area_scale:
            continue
        rows, columns = padded_window(slices, labels.shape, ring_px)
        blob = labels[rows, columns] == index
        ring = ndimage.binary_dilation(blob, structure=ring_structure) & ~blob
        white_counts.append(int((ring & white[rows, columns]).sum()))
    if not white_counts:
        return empty
    counts = np.array(white_counts) / area_scale
    return {"blob_count": len(white_counts), "mean_white_in_ring": float(counts.mean()), "ratio_three_or_more": float((counts >= CRESCENT_MIN_WHITE_PIXELS).mean())}


def measure_frame(rgb, classes):
    height = rgb.shape[0]
    band_luminance = {}
    blue_over_green = {}
    for name, (start, end) in LUMINANCE_BAND_ROWS.items():
        rows = slice(int(round(height * start)), int(round(height * end)))
        water = classes["water"][rows]
        if not water.any():
            band_luminance[name] = None
            blue_over_green[name] = None
            continue
        band_luminance[name] = float(classes["luminance"][rows][water].mean())
        mean_rgb = rgb[rows][water].mean(axis=0)
        blue_over_green[name] = safe_ratio(mean_rgb[2], mean_rgb[1])
    far_minus_near = None if blue_over_green["far"] is None or blue_over_green["near"] is None else blue_over_green["far"] - blue_over_green["near"]
    return {"void_percent": 100.0 * float(classes["void"].mean()), "band_luminance": band_luminance, "band_blue_over_green": blue_over_green,
            "far_minus_near_blue_over_green": far_minus_near}


def measure_region(rgb, classes, pixel_scale):
    water = classes["water"]
    result = {"pixel_count": int(water.size), "water_fraction": float(water.mean()), "white_percent": 100.0 * float(classes["white"].mean()),
              "debris_percent": 100.0 * float(classes["debris"].mean())}
    result["sparkle"] = measure_sparkle(classes["luminance"], water, classes["white"])
    result["crescent"] = measure_crescents(classes["debris"], classes["white"], pixel_scale)
    if not water.any():
        return dict(result, water_mean_rgb=None, green_over_red=None, blue_over_red=None, band_std={"low": None, "mid": None, "high": None}, floor_band_std=None)
    mean_rgb = rgb[water].mean(axis=0)
    result["water_mean_rgb"] = [float(value) for value in mean_rgb]
    result["green_over_red"] = safe_ratio(mean_rgb[1], mean_rgb[0])
    result["blue_over_red"] = safe_ratio(mean_rgb[2], mean_rgb[0])
    result["band_std"] = measure_bands(classes["luminance"], water)
    result["floor_band_std"] = measure_floor_band(classes["luminance"], water)
    return result


def fill_bounded_holes(mask):
    filled = ndimage.binary_fill_holes(mask)
    holes, hole_count = ndimage.label(filled & ~mask)
    if not hole_count:
        return filled
    oversized = np.bincount(holes.ravel()) > BOX_MAX_FILLED_HOLE_SHARE * filled.sum()
    oversized[0] = False
    return filled & ~oversized[holes]


def measure_box(white, box):
    height, width = white.shape
    x0, y0, x1, y1 = max(int(round(box[0])), 0), max(int(round(box[1])), 0), min(int(round(box[2])), width), min(int(round(box[3])), height)
    if x1 <= x0 or y1 <= y0:
        return {"box_px": [x0, y0, x1, y1], "error": "상자가 장 밖이다"}
    window = white[y0:y1, x0:x1]
    white_count = int(window.sum())
    margin = BOX_CLOSING_ITERATIONS + 1
    closed = ndimage.binary_closing(np.pad(window, margin), iterations=BOX_CLOSING_ITERATIONS)
    footprint = fill_bounded_holes(closed)[margin:-margin, margin:-margin]
    labels, blob_count = ndimage.label(window, structure=EIGHT_CONNECTED)
    largest = int(np.bincount(labels.ravel())[1:].max()) if blob_count else 0
    return {"box_px": [x0, y0, x1, y1], "white_percent": 100.0 * white_count / window.size, "fill_ratio": safe_ratio(white_count, footprint.sum()),
            "largest_blob_share": safe_ratio(largest, white_count)}


def select_boxes(box_specification, image_name, size):
    if not box_specification:
        return []
    width, height = size
    reference_width, reference_height = box_specification.get("image_size") or [width, height]
    entries = list(box_specification.get("regions", []))
    variants = box_specification.get("variants", {})
    matches = [key for key in variants if key in image_name]
    if matches:
        entries += variants[max(matches, key=len)]
    scale_x, scale_y = width / reference_width, height / reference_height
    return [(entry["name"], [entry["box"][0] * scale_x, entry["box"][1] * scale_y, entry["box"][2] * scale_x, entry["box"][3] * scale_y]) for entry in entries]


def crop_frame(rgb, crop):
    if crop is None:
        return rgb
    height, width = rgb.shape[:2]
    x0, y0, x1, y1 = max(crop[0], 0), max(crop[1], 0), min(crop[2], width), min(crop[3], height)
    if x1 <= x0 or y1 <= y0:
        raise ValueError(f"--crop 이 장 밖이다: {crop} (장 {width}x{height})")
    return rgb[y0:y1, x0:x1]


def region_rows(height, rows):
    return slice(int(round(height * NEAR_ROW_START_FRACTION)) if rows == "near" else 0, height)


def analyze_array(rgb, rows, crop=None, boxes=()):
    pixel_scale = rgb.shape[0] / REFERENCE_HEIGHT_PX
    full_classes = classify_pixels(rgb) if boxes else None
    frame_rgb = crop_frame(rgb, crop)
    classes = classify_pixels(frame_rgb)
    selected_rows = region_rows(frame_rgb.shape[0], rows)
    region_classes = slice_classes(classes, selected_rows)
    analysis = {"size": [int(rgb.shape[1]), int(rgb.shape[0])], "pixel_scale": pixel_scale, "frame": measure_frame(frame_rgb, classes),
                "region": dict(measure_region(frame_rgb[selected_rows], region_classes, pixel_scale), rows=rows)}
    analysis["boxes"] = {name: measure_box(full_classes["white"], box) for name, box in boxes}
    return analysis, region_classes


def measure_temporal(previous_classes, current_classes):
    if previous_classes["water"].shape != current_classes["water"].shape:
        return {"error": "두 장의 크기가 다르다"}
    both = previous_classes["water"] & current_classes["water"]
    if not both.any():
        return {"low": None, "high": None, "low_over_high": None, "pixel_count": 0}
    previous_luminance, previous_water = previous_classes["luminance"], previous_classes["water"]
    current_luminance, current_water = current_classes["luminance"], current_classes["water"]
    low = np.abs(masked_gaussian(current_luminance, current_water, TEMPORAL_LOW_SIGMA_PX) - masked_gaussian(previous_luminance, previous_water, TEMPORAL_LOW_SIGMA_PX))
    current_high = current_luminance - masked_gaussian(current_luminance, current_water, TEMPORAL_HIGH_SIGMA_PX)
    previous_high = previous_luminance - masked_gaussian(previous_luminance, previous_water, TEMPORAL_HIGH_SIGMA_PX)
    low_mean = float(low[both].mean())
    high_mean = float(np.abs(current_high - previous_high)[both].mean())
    return {"low": low_mean, "high": high_mean, "low_over_high": safe_ratio(low_mean, high_mean), "pixel_count": int(both.sum())}


def flatten_numbers(value, prefix, collected):
    if isinstance(value, dict):
        for key, child in value.items():
            if key not in SUMMARY_SKIPPED_KEYS:
                flatten_numbers(child, f"{prefix}.{key}" if prefix else key, collected)
    elif isinstance(value, list):
        for index, child in enumerate(value):
            flatten_numbers(child, f"{prefix}[{index}]", collected)
    elif isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value):
        collected.setdefault(prefix, []).append(float(value))


def summarize(images, temporal):
    collected = {}
    for image in images:
        flatten_numbers({key: image[key] for key in ("frame", "region", "boxes")}, "", collected)
    flatten_numbers({"temporal": [{key: pair.get(key) for key in ("low", "high", "low_over_high")} for pair in temporal]}, "", collected)
    summary = {}
    for key, values in collected.items():
        summary_key = key.split("[", 1)[0] + key.split("]", 1)[1] if key.startswith("temporal[") else key
        summary.setdefault(summary_key, []).extend(values)
    return {key: float(np.mean(values)) for key, values in summary.items()}


def parse_timed_frame(path):
    match = TIMED_FRAME_PATTERN.match(Path(path).stem)
    return (match["series"], int(match["milliseconds"])) if match else (None, None)


def split_frames(paths):
    frames = [path for path in paths if Path(path).name != LOOKDEV_GRID_FILE_NAME]
    skipped = [Path(path).as_posix() for path in paths if Path(path).name == LOOKDEV_GRID_FILE_NAME]
    return frames, skipped


def plan_frames(paths):
    timing = [parse_timed_frame(path) for path in paths]
    order = sorted(range(len(paths)), key=lambda index: (timing[index][0] is None, timing[index][0] or "", timing[index][1] or 0, index))
    steps = {current: timing[current][1] - timing[previous][1] for previous, current in zip(order, order[1:])
             if timing[current][0] is not None and timing[current][0] == timing[previous][0]}
    return order, steps


def measure_images(paths, rows, crop, box_specification):
    frames, skipped = split_frames(paths)
    order, steps = plan_frames(frames)
    images = [None] * len(frames)
    temporal = []
    previous_classes = None
    previous_path = None
    for index in order:
        path = frames[index]
        rgb = load_rgb(path)
        boxes = select_boxes(box_specification, Path(path).name, (rgb.shape[1], rgb.shape[0]))
        analysis, region_classes = analyze_array(rgb, rows, crop, boxes)
        images[index] = dict(path=Path(path).as_posix(), **analysis)
        if index in steps:
            temporal.append(dict(measure_temporal(previous_classes, region_classes), previous=Path(previous_path).as_posix(), current=Path(path).as_posix(), step_ms=steps[index]))
        previous_classes, previous_path = region_classes, path
    summarized_pairs = [pair for pair in temporal if pair["step_ms"] == TEMPORAL_STEP_MS]
    return {"tool": "measure_water_look", "rows": rows, "crop": crop, "images": images, "skipped_images": skipped, "temporal": temporal,
            "temporal_step_ms": TEMPORAL_STEP_MS, "summary": summarize(images, summarized_pairs)}


def water_canvas(height, width):
    return np.broadcast_to(SELF_TEST_WATER_RGB, (height, width, 3)).copy()


def modulated_water(height, width, pattern, amplitude=0.3):
    return water_canvas(height, width) * (1.0 + amplitude * pattern)[..., None]


def cosine_pattern(height, width, wavelength_px):
    columns = np.arange(width)[None, :].repeat(height, axis=0)
    return np.cos(2.0 * np.pi * (columns + 0.5) / wavelength_px)


def checker_pattern(height, width, cell_px):
    rows, columns = np.indices((height, width))
    return np.where(((rows // cell_px) + (columns // cell_px)) % 2 == 0, 1.0, -1.0)


def disk_mask(height, width, center, radius):
    rows, columns = np.indices((height, width))
    return (rows - center[1]) ** 2 + (columns - center[0]) ** 2 <= radius ** 2


def voronoi_lace_mask(height, width, center, radius, seed_count, edge_px, random_seed):
    generator = np.random.default_rng(random_seed)
    angles = generator.uniform(0.0, 2.0 * np.pi, seed_count)
    distances = radius * np.sqrt(generator.uniform(0.0, 1.0, seed_count))
    seeds = np.stack([center[0] + distances * np.cos(angles), center[1] + distances * np.sin(angles)], axis=1)
    rows, columns = np.indices((height, width))
    seed_distances = np.sqrt((columns[..., None] - seeds[:, 0]) ** 2 + (rows[..., None] - seeds[:, 1]) ** 2)
    nearest_two = np.sort(seed_distances, axis=2)[..., :2]
    edges = (nearest_two[..., 1] - nearest_two[..., 0]) < edge_px
    radial = np.sqrt((columns - center[0]) ** 2 + (rows - center[1]) ** 2)
    rim = np.abs(radial - radius) < edge_px * 0.75
    return (edges & (radial <= radius)) | rim


def segmented_ring_mask(size, center, inner_radius, outer_radius, segment_count, gap_px):
    rows, columns = np.indices((size, size))
    radial = np.sqrt((columns - center[0]) ** 2 + (rows - center[1]) ** 2)
    angle = np.degrees(np.arctan2(rows - center[1], columns - center[0])) % 360.0
    in_gap = (angle % (360.0 / segment_count)) < np.degrees(gap_px / ((inner_radius + outer_radius) / 2.0))
    return (radial >= inner_radius) & (radial <= outer_radius) & ~in_gap


def crescent_scene(scale):
    height, width = int(round(REFERENCE_HEIGHT_PX * scale)), int(round(240 * scale))
    image = water_canvas(height, width)
    rows, columns = np.indices((height, width))
    for blob_x, blob_y in ((40, 40), (110, 50), (70, 115)):
        radial = np.sqrt((columns - blob_x * scale) ** 2 + (rows - blob_y * scale) ** 2)
        image[radial <= 6 * scale] = SELF_TEST_DEBRIS_RGB
        image[(radial >= 7 * scale) & (radial <= 9 * scale) & (columns >= (blob_x + 3) * scale)] = 1.0
    return image


def run_self_test():
    checks = []

    def record(name, passed, details):
        checks.append(passed)
        print(("통과" if passed else "실패"), name, json.dumps(details, ensure_ascii=False), flush=True)

    flat, _ = analyze_array(water_canvas(256, 256), "all")
    flat_values = [flat["region"]["band_std"][key] for key in ("low", "mid", "high")] + [flat["region"]["floor_band_std"]]
    record("1 단색: 모든 대역 ≈ 0", all(value < 1e-6 for value in flat_values), {"band_std": flat["region"]["band_std"], "floor": flat["region"]["floor_band_std"]})

    sine, _ = analyze_array(modulated_water(256, 512, cosine_pattern(256, 512, 128.0)), "all")
    bands = sine["region"]["band_std"]
    record("2 파장 128 px 사인: 저역 우세", bands["low"] > bands["mid"] and bands["low"] > bands["high"], bands)

    checker, _ = analyze_array(modulated_water(256, 256, checker_pattern(256, 256, 2)), "all")
    bands = checker["region"]["band_std"]
    record("3 2 px 체커: 고역 우세", bands["high"] > bands["low"] and bands["high"] > bands["mid"], bands)

    center, radius = (100, 100), 60
    solid = water_canvas(200, 200)
    solid[disk_mask(200, 200, center, radius)] = 1.0
    lace = water_canvas(200, 200)
    lace[voronoi_lace_mask(200, 200, center, radius, 16, 2.0, 7)] = 1.0
    box = [("foam", [30, 30, 170, 170])]
    solid_box = analyze_array(solid, "all", boxes=box)[0]["boxes"]["foam"]
    lace_box = analyze_array(lace, "all", boxes=box)[0]["boxes"]["foam"]
    record("4 흰 원판 대 보로노이 레이스 원판: 채움비 ≥0.95 대 ≤0.5", solid_box["fill_ratio"] >= 0.95 and lace_box["fill_ratio"] <= 0.5,
           {"solid": solid_box, "lace": lace_box})

    texture = modulated_water(256, 512, cosine_pattern(256, 512, 64.0) * 0.5 + checker_pattern(256, 512, 8) * 0.5)
    _, first_classes = analyze_array(texture, "all")
    _, second_classes = analyze_array(np.roll(texture, 1, axis=1), "all")
    temporal = measure_temporal(first_classes, second_classes)
    record("5 1 px 이동한 두 장: 시간 저역 > 0", temporal["low"] is not None and temporal["low"] > 1e-4, temporal)

    crescent = analyze_array(crescent_scene(1.0), "all")[0]["region"]["crescent"]
    record("6 주황 원 + 한쪽 흰 테(높이 720 px): 초승달 비율 1", crescent["blob_count"] == 3 and crescent["ratio_three_or_more"] == 1.0, crescent)

    long_sine, _ = analyze_array(modulated_water(200, 800, cosine_pattern(200, 800, 200.0)), "all")
    region = long_sine["region"]
    record("7 파장 200 px 사인: 바닥 대역 > 고역", region["floor_band_std"] > region["band_std"]["high"], {"floor": region["floor_band_std"], "high": region["band_std"]["high"]})

    crescent_1080 = analyze_array(crescent_scene(1.5), "all")[0]["region"]["crescent"]
    same_scene = (crescent_1080["blob_count"] == 3 and crescent_1080["ratio_three_or_more"] == 1.0 and crescent["mean_white_in_ring"] is not None
                  and abs(crescent_1080["mean_white_in_ring"] / crescent["mean_white_in_ring"] - 1.0) <= 0.25)
    record("8 같은 초승달 장면을 높이 1080 px 로: 720 px 와 같은 개수·비율, 720p 환산 평균 ±25%", same_scene, {"720": crescent, "1080": crescent_1080})

    ring = water_canvas(200, 200)
    ring[segmented_ring_mask(200, (100, 100), 60, 75, 8, 4.0)] = 1.0
    ring_box = analyze_array(ring, "all", boxes=[("ring", [10, 10, 190, 190])])[0]["boxes"]["ring"]
    record("9 4 px 틈 8토막 흰 고리 띠: 고리 안쪽 맑은 물은 안 메워 채움비 ≥0.9", ring_box["fill_ratio"] >= 0.9, ring_box)

    rows_index = np.indices((256, 512))[0]
    leak_water = modulated_water(256, 512, 1.2 * (rows_index / 256.0 - 0.5) + 0.5 * cosine_pattern(256, 512, 96.0) + 0.05 * checker_pattern(256, 512, 2))
    leak_foam = leak_water.copy()
    leak_foam[voronoi_lace_mask(256, 512, (256, 128), 110, 40, 2.0, 11)] = 1.0
    clean_bands = analyze_array(leak_water, "all")[0]["region"]["band_std"]
    foam_bands = analyze_array(leak_foam, "all")[0]["region"]["band_std"]
    high_change = foam_bands["high"] / clean_bands["high"]
    balance_change = (foam_bands["high"] / (foam_bands["low"] + foam_bands["mid"])) / (clean_bands["high"] / (clean_bands["low"] + clean_bands["mid"]))
    record("10 같은 물에 레이스 거품만 얹음: 고역·고역/(저역+중역) 변화 ≤10%", abs(high_change - 1.0) <= 0.1 and abs(balance_change - 1.0) <= 0.1,
           {"clean": clean_bands, "foam": foam_bands, "high_change": high_change, "balance_change": balance_change})

    names = [f"00_sea_img16_{milliseconds:05d}ms.png" for milliseconds in (6000, 6250, 6500, 6750, 7000)]
    names += [f"01_sea_img16_clip_{milliseconds:05d}ms.png" for milliseconds in (6000, 6100)] + ["02_gamecam_graze.png", "grid.png"]
    frames, skipped = split_frames(names)
    _, steps = plan_frames(frames)
    record("11 촬영 폴더째: grid.png 빼고 같은 묶음 안 이웃만 쌍(250 ms 4개·100 ms 1개)", skipped == ["grid.png"] and sorted(steps.values()) == [100, 250, 250, 250, 250],
           {"skipped": skipped, "steps": {frames[index]: step for index, step in steps.items()}})

    print(f"self-test {sum(checks)}/{len(checks)} 통과", flush=True)
    return 0 if all(checks) else 1


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description="Q1 물 화질 지표(roadmap-v2 2.4 T2)")
    parser.add_argument("--images", nargs="+", type=Path)
    parser.add_argument("--boxes", type=Path)
    parser.add_argument("--rows", choices=("near", "all"), default="near")
    parser.add_argument("--crop", nargs=4, type=int, metavar=("X0", "Y0", "X1", "Y1"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    if arguments.self_test:
        return run_self_test()
    if not arguments.images or arguments.output is None:
        parser.error("--images 와 --output 이 필요하다(또는 --self-test)")
    missing = [path.as_posix() for path in arguments.images if not path.is_file()]
    if missing:
        print("이미지가 없다:", ", ".join(missing))
        return 1
    box_specification = json.loads(arguments.boxes.read_text(encoding="utf-8")) if arguments.boxes else None
    result = measure_images(arguments.images, arguments.rows, arguments.crop, box_specification)
    result["boxes_file"] = arguments.boxes.as_posix() if arguments.boxes else None
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print("저장", arguments.output.as_posix(), json.dumps({key: round(value, 5) for key, value in result["summary"].items() if key.startswith("region.band_std")}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
