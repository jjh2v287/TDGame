"""시스템 Python으로 실행(numpy·Pillow, --targets 모드는 scipy 추가, 에디터 불필요): 기본 모드는 이미지와 영역 JSON으로 불·연기 M1~M5, 구름·안개, 물, 반짝임 비율, 두 프레임 차(잔상) 지표를 결정론으로 재고 같은 구도의 조건별(R16 안티에일리어싱 A/B 등) 비교표를 낸다(FX-7a). --targets 모드는 목표 JSON(LIVE-2 합격 범위)과 영역 JSON(이미지·이름 붙은 사각형/다각형 영역·kind fire|torch|smoke|plume|background·game_time 라벨)으로 FX-8c 지표(불 색 등급·색상 중앙값·M1 G/R·테두리/코어 색·가장자리 폭/등가 반지름·번짐 @16px·배경 p20·실루엣 변동계수·구조 텐서 결맞음·색 경계 밀도·분홍 비율(pink_hi_fraction = 불 화소 중 색상<15°·휘도≥130, pink_hi145_fraction ≥145), 연기 백분위·미켈슨·따뜻함·중성 바닥·곧은 경계)를 재고 합격/불합격 격차표를 낸다(FX-8c C17). 목표 JSON 선택 키 kind_metrics {fire|torch: {지표 경로: 범위}}는 그 kind 영역에만 덧붙여 판정한다.
실행: python Tools/FluidFX/measure_fx_look.py --regions <영역 JSON> --output <결과 JSON> [--targets <목표 JSON>] (PowerShell 또는 Git Bash, 인수와 JSON 안의 상대 경로는 프로젝트 루트 기준, --targets 영역 JSON 형식은 --help)
출력: --output JSON(기본: images 이미지·영역별 지표와 comparisons 조건 비교표 / --targets: images 영역별 metrics·checks 판정(kind_metrics 범위는 group kind_metrics.<kind>)·frame_fire C8 판정)과 표준 출력(기본: 요약 / --targets: 마크다운 격차표, X = 합격 범위 밖). 종료 코드 0 성공(불합격 지표가 있어도 0) / 1 출력이 Docs/ 아래인데 입력에 LIVE-2 갤러리 이미지가 있음(RR R-14) / 2 입력 오류(--targets인데 scipy 없음 포함)
상태: 실험. 해석 선택: JPEG가 아닌 입력은 전체 프레임을 품질 95·4:2:0으로 다시 압축한 뒤 자름(LIVE-2 갤러리 JPEG 24장의 양자화표가 모두 IJG 품질 95 표·4:2:0과 같음, Pillow 11.2.1 대조), 흰 포화의 분모는 휘도 60 초과 화소(따뜻한 마스크 안에서는 정의상 0), 구름·안개 잡음과 물 국소 대비의 흐림은 8비트 내림 휘도에 Pillow GaussianBlur(반경 2·12), 미세/거친 비는 원본 대 1/4 LANCZOS 축소의 라플라시안 평균비, M3 물체 높이 기본값은 영역 높이. --targets 지표는 FX-8c 1·2차 스크립트(measure_targets·measure_r2·plume_r2)와 같은 정의이고 색 경계 밀도는 2차 정의, 플룸 = σ2 흐린 휘도 ≥6에서 불씨(따뜻함·V≥0.6·L>114.75, 3px 팽창) 제외, C8 = 프레임 전체 불 화소(V≥0.6) 최대 연결 덩어리가 200px 미만이면 연기의 따뜻함·채도·그림자 채도를 day_smoke(중성) 범위로 판정(이미지 fire_lit로 덮어씀), 중성 바닥 = 연기 화소 평균 G·B(manual 8~30), background 영역 휘도 p20은 불 주변 배경 범위(bg_luma)로 판정. FX-8c 11차(2026-10-21) 분홍 비율 2개와 kind_metrics 추가: kind_metrics 없는 목표 JSON에서 표준 출력과 기존 지표 값은 같고, 불 영역 metrics 끝에 키 2개만 붙는다(round11 torch-pink-metric run_tests 19/19).
"""
import argparse
import io
import json
import math
import os
import sys
import warnings
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

try:
    from scipy import ndimage
except ImportError:
    ndimage = None

PROJECT_ROOT = Path(__file__).resolve().parents[2]
DOCS_DIR = PROJECT_ROOT / "Docs"
LIVE2_GALLERY_DIR = DOCS_DIR / "Fluid Ninja LIVE 2.0"
JPEG_SUFFIXES = (".jpg", ".jpeg")
JPEG_QUALITY = 95
JPEG_SUBSAMPLING = "4:2:0"
FLAME_KINDS = ("fire", "explosion", "smoke")
EMBER_KINDS = ("embers",)
ATMOSPHERE_KINDS = ("cloud", "fog", "dust", "snow", "sky")
WATER_KINDS = ("water",)
KNOWN_KINDS = FLAME_KINDS + EMBER_KINDS + ATMOSPHERE_KINDS + WATER_KINDS
MINIMUM_REGION_SIDE = 16
WARM_MASK_MINIMUM_PIXELS = 50
FLAME_PERCENTILES = (10, 25, 50, 75, 95)
FLAME_STEP_HALF_WIDTH = 6
DETAIL_CYCLES_PER_HEIGHT = (16, 32, 64)
DETAIL_MINIMUM_CYCLES_PER_HEIGHT = 2
DETAIL_MAX_CYCLES_PER_PIXEL = 0.45
EMBER_BACKGROUND_SIZE = 9
EMBER_MAX_AREA = 40
GLINT_LUMINANCE = 0.92
FRAME_CHANGE_THRESHOLD = 0.03
DECIMALS = 6
TARGET_KINDS = ("fire", "torch", "smoke", "plume", "background")
FLAME_TARGET_KINDS = ("fire", "torch")
SMOKE_TARGET_KINDS = ("smoke", "plume")
TARGET_FAMILIES = (("불·횃불", FLAME_TARGET_KINDS), ("연기·플룸", SMOKE_TARGET_KINDS), ("배경", ("background",)))
FLAME_VALUE = 0.60
GLOW_VALUE = 0.30
WARM_SATURATION = 0.25
WHITE_VALUE = 0.92
CORE_LUMINANCE = 0.45
WARM_HUE_END = 70
WARM_HUE_WRAP = 330
YELLOW_HUE_START = 42
ORANGE_HUE_START = 25
RED_HUE_START = 12
FLAME_CLASSES = ("white_core", "yellow", "orange", "red", "deep_red")
WHITE_OR_YELLOW_CLASSES = (0, 1)
RED_OR_DEEP_RED_CLASSES = (3, 4)
MINIMUM_FLAME_PIXELS = 200
MINIMUM_SMOKE_PIXELS = 200
MINIMUM_BAND_PIXELS = 30
PLANE_MARGIN = 8
RIM_DEPTH = 4
CORE_MINIMUM_DEPTH = 8.0
CORE_DEPTH_FRACTION = 0.35
LARGE_COMPONENT_FRACTION = 0.02
BACKGROUND_RING = (60, 120)
HALO_DISTANCES = (1, 2, 4, 8, 16, 32, 48, 64, 96)
HALO_FALLOFF_DISTANCES = (4, 8, 16, 32, 48, 64, 96)
EDGE_STEP = 2
SMOKE_PERCENTILES = (5, 10, 25, 50, 75, 90, 95)
SMOKE_CONTRAST_BANDS = {"billow_contrast_s2_s16": (2.0, 16.0), "fine_contrast_s1_s4": (1.0, 4.0), "coarse_contrast_s8_s40": (8.0, 40.0)}
PLUME_BLUR_SIGMA = 2.0
PLUME_MINIMUM_LUMINANCE = 6.0
EMBER_DILATION = 3
FIRE_LIT_MINIMUM_COMPONENT_PIXELS = 200
FIRE_GROUP = "fire_and_fireball"
NIGHT_SMOKE_GROUP = "night_firelit_smoke"
NEUTRAL_SMOKE_GROUP = "day_smoke"
TARGET_GROUPS = (FIRE_GROUP, NIGHT_SMOKE_GROUP, NEUTRAL_SMOKE_GROUP)
FIRE_LIT_SMOKE_COLOR_METRICS = ("warmth_lit_top10", "saturation_mean", "sat_shadow_bottom10")
NEUTRAL_FLOOR_TARGET = "campfire_smoke_neutral_floor_GB_8bit"
NEUTRAL_FLOOR_METRICS = ("neutral_floor_green", "neutral_floor_blue")
BACKGROUND_TARGET_METRIC = "bg_luma"
TORCH_MANUAL_TARGET = "torch_flame_height_over_character"
KIND_TARGETS_KEY = "kind_metrics"
PINK_HUE_END = 15
PINK_LUMINANCE = 130
PINK_STRICT_LUMINANCE = 145
MANUAL_FLAME_METRICS = {
    "torch_flame_height_over_character": "flame_bbox_h_over_char",
    "fire_engulfing_pawn_bbox_height_over_character": "flame_bbox_h_over_char",
    "campfire_flame_height_over_width": "aspect_h_over_w",
}


class InputError(Exception):
    pass


def resolve_project_path(text):
    path = Path(text)
    if not path.is_absolute():
        path = PROJECT_ROOT / path
    return path.resolve()


def is_inside(path, folder):
    path_text = os.path.normcase(str(path))
    folder_text = os.path.normcase(str(folder.resolve()))
    return path_text == folder_text or path_text.startswith(folder_text + os.sep)


def load_json(path):
    try:
        with open(path, encoding="utf-8-sig") as handle:
            return json.load(handle)
    except OSError as error:
        raise InputError(f"JSON을 열 수 없다: {path} ({error})")
    except json.JSONDecodeError as error:
        raise InputError(f"JSON 형식 오류: {path} ({error})")


def read_text_field(entry, key, where, required=False):
    value = entry.get(key)
    if value is None and not required:
        return None
    if not isinstance(value, str) or not value:
        raise InputError(f"{where}: '{key}' 는 비어 있지 않은 문자열이어야 한다")
    return value


def is_plain_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def read_region(entry, where):
    if not isinstance(entry, dict):
        raise InputError(f"{where}: 영역은 객체여야 한다")
    name = read_text_field(entry, "name", where, required=True)
    kind = read_text_field(entry, "kind", where, required=True)
    if kind not in KNOWN_KINDS:
        raise InputError(f"{where}: kind '{kind}' 는 {', '.join(KNOWN_KINDS)} 중 하나여야 한다")
    box = entry.get("box")
    if not isinstance(box, list) or len(box) != 4 or not all(isinstance(v, int) and not isinstance(v, bool) for v in box):
        raise InputError(f"{where}: box 는 정수 4개 [x0, y0, x1, y1] 이어야 한다")
    x0, y0, x1, y1 = box
    if x0 < 0 or y0 < 0 or x1 - x0 < MINIMUM_REGION_SIDE or y1 - y0 < MINIMUM_REGION_SIDE:
        raise InputError(f"{where}: box 는 음수가 없고 가로·세로 {MINIMUM_REGION_SIDE}px 이상이어야 한다")
    object_height = entry.get("object_height_px", y1 - y0)
    if not is_plain_number(object_height) or object_height <= 0:
        raise InputError(f"{where}: object_height_px 는 양수여야 한다")
    return {"name": name, "kind": kind, "box": box, "object_height_px": object_height}


def read_image_entry(entry, where):
    if not isinstance(entry, dict):
        raise InputError(f"{where}: 이미지 항목은 객체여야 한다")
    path_text = read_text_field(entry, "path", where, required=True)
    previous_text = read_text_field(entry, "previous", where)
    view = read_text_field(entry, "view", where)
    condition = read_text_field(entry, "condition", where)
    if condition and not view:
        raise InputError(f"{where}: condition 을 쓰려면 view 가 필요하다")
    raw_regions = entry.get("regions")
    if not isinstance(raw_regions, list) or not raw_regions:
        raise InputError(f"{where}: regions 는 비어 있지 않은 목록이어야 한다")
    regions = [read_region(region, f"{where}.regions[{index}]") for index, region in enumerate(raw_regions)]
    names = [region["name"] for region in regions]
    if len(set(names)) != len(names):
        raise InputError(f"{where}: 영역 이름이 겹친다")
    return {
        "id": read_text_field(entry, "id", where) or path_text,
        "path_text": path_text,
        "path": resolve_project_path(path_text),
        "previous_text": previous_text,
        "previous": resolve_project_path(previous_text) if previous_text else None,
        "view": view,
        "condition": condition,
        "regions": regions,
    }


def read_plan(document):
    if not isinstance(document, dict) or not isinstance(document.get("images"), list) or not document["images"]:
        raise InputError("영역 JSON 최상위는 비어 있지 않은 'images' 목록을 가진 객체여야 한다")
    plan = [read_image_entry(entry, f"images[{index}]") for index, entry in enumerate(document["images"])]
    identifiers = [entry["id"] for entry in plan]
    if len(set(identifiers)) != len(identifiers):
        raise InputError("이미지 id 가 겹친다")
    return plan


def uses_live2_gallery(plan):
    for entry in plan:
        if is_inside(entry["path"], LIVE2_GALLERY_DIR):
            return True
        if entry["previous"] is not None and is_inside(entry["previous"], LIVE2_GALLERY_DIR):
            return True
    return False


def load_measured_pixels(path):
    if not path.is_file():
        raise InputError(f"이미지가 없다: {path}")
    try:
        with Image.open(path) as source:
            image = source.convert("RGB")
    except OSError as error:
        raise InputError(f"이미지를 읽을 수 없다: {path} ({error})")
    if path.suffix.lower() in JPEG_SUFFIXES:
        return np.asarray(image), False
    buffer = io.BytesIO()
    image.save(buffer, "JPEG", quality=JPEG_QUALITY, subsampling=JPEG_SUBSAMPLING)
    buffer.seek(0)
    with Image.open(buffer) as recompressed:
        return np.asarray(recompressed.convert("RGB")), True


def luminance(rgb):
    return 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]


def blur_quantized_luminance(unit_luminance, radius):
    quantized = (unit_luminance * 255).astype(np.uint8)
    blurred = Image.fromarray(quantized).filter(ImageFilter.GaussianBlur(radius))
    return np.asarray(blurred, dtype=np.float64) / 255.0


def mean_absolute_laplacian(values):
    center = values[1:-1, 1:-1]
    neighbors = values[:-2, 1:-1] + values[2:, 1:-1] + values[1:-1, :-2] + values[1:-1, 2:]
    return float(np.abs(4 * center - neighbors).mean())


def ratio_or_none(numerator, denominator):
    if numerator is None or denominator is None or denominator == 0:
        return None
    return numerator / denominator


def median_or_none(values):
    if not values:
        return None
    return float(np.median(values))


def measure_color_step(level, red, green, blue):
    if red.size == 0:
        return {"luminance": level, "green_over_red": None, "blue_over_red": None}
    red_mean = red.mean()
    return {"luminance": level, "green_over_red": green.mean() / red_mean, "blue_over_red": blue.mean() / red_mean}


def measure_flame(crop):
    rgb = crop.astype(np.float64)
    red, green, blue = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    lum = luminance(rgb)
    warm = (red > 70) & (red > 1.4 * blue) & (red >= green)
    glow = lum > 60
    result = {"warm_fraction": warm.mean(), "white_clip_fraction": None}
    if glow.sum() > WARM_MASK_MINIMUM_PIXELS:
        result["white_clip_fraction"] = (rgb.min(axis=-1) >= 245)[glow].mean()
    if warm.sum() < WARM_MASK_MINIMUM_PIXELS:
        return result
    warm_luminance = lum[warm]
    steps = {}
    for percent in FLAME_PERCENTILES:
        level = np.percentile(warm_luminance, percent)
        near = warm & (np.abs(lum - level) < FLAME_STEP_HALF_WIDTH)
        steps[f"p{percent}"] = measure_color_step(level, red[near], green[near], blue[near])
    result["color_steps"] = steps
    result["hot_fraction"] = (warm_luminance > 200).mean()
    result["red_clip_fraction"] = (red[warm] >= 250).mean()
    result["contrast_p90_over_p10"] = np.percentile(warm_luminance, 90) / max(np.percentile(warm_luminance, 10), 1.0)
    result["median_luminance"] = np.percentile(warm_luminance, 50)
    return result


def measure_detail(lum, object_height):
    height, width = lum.shape
    window = np.outer(np.hanning(height), np.hanning(width))
    power = np.abs(np.fft.fft2((lum - lum.mean()) * window)) ** 2
    radius = np.hypot(np.fft.fftfreq(height)[:, None], np.fft.fftfreq(width)[None, :])
    cycles = radius * object_height
    inside_band = radius < DETAIL_MAX_CYCLES_PER_PIXEL
    total = power[(cycles >= DETAIL_MINIMUM_CYCLES_PER_HEIGHT) & inside_band].sum()
    result = {"object_height_px": object_height}
    for threshold in DETAIL_CYCLES_PER_HEIGHT:
        result[f"above_{threshold}_cycles_per_height"] = ratio_or_none(power[(cycles >= threshold) & inside_band].sum(), total)
    return result


def median_filter(values, size):
    margin = size // 2
    windows = np.lib.stride_tricks.sliding_window_view(np.pad(values, margin, mode="symmetric"), (size, size))
    filtered = np.empty_like(values)
    for row in range(0, values.shape[0], 32):
        filtered[row:row + 32] = np.median(windows[row:row + 32], axis=(2, 3))
    return filtered


def find_root(parent, label):
    root = label
    while parent[root] != root:
        root = parent[root]
    while parent[label] != root:
        next_label = parent[label]
        parent[label] = root
        label = next_label
    return root


def join_labels(parent, first, second):
    first_root = find_root(parent, first)
    second_root = find_root(parent, second)
    if first_root != second_root:
        parent[max(first_root, second_root)] = min(first_root, second_root)


def find_row_runs(line):
    edges = np.flatnonzero(np.diff(np.concatenate(([0], line.astype(np.int8), [0]))))
    return list(zip(edges[0::2].tolist(), edges[1::2].tolist()))


def find_components(mask):
    parent = []
    runs = []
    previous_runs = []
    for row in range(mask.shape[0]):
        current_runs = []
        pointer = 0
        for start, end in find_row_runs(mask[row]):
            label = len(parent)
            parent.append(label)
            while pointer < len(previous_runs) and previous_runs[pointer][1] <= start:
                pointer += 1
            scan = pointer
            while scan < len(previous_runs) and previous_runs[scan][0] < end:
                join_labels(parent, label, previous_runs[scan][2])
                scan += 1
            current_runs.append((start, end, label))
            runs.append((row, start, end, label))
        previous_runs = current_runs
    components = {}
    for row, start, end, label in runs:
        components.setdefault(find_root(parent, label), []).append((row, start, end))
    return list(components.values())


def measure_elongation(component_runs):
    columns = np.concatenate([np.arange(start, end) for _, start, end in component_runs]).astype(np.float64)
    rows = np.concatenate([np.full(end - start, row) for row, start, end in component_runs]).astype(np.float64)
    eigenvalues = np.linalg.eigvalsh(np.cov(np.vstack([columns, rows])))
    return float(np.sqrt(max(eigenvalues[1], 1e-3) / max(eigenvalues[0], 1e-3)))


def measure_embers(crop):
    rgb = crop.astype(np.float64)
    lum = luminance(rgb)
    mask = (lum - median_filter(lum, EMBER_BACKGROUND_SIZE) > 25) & (rgb[..., 0] > 110) & (rgb[..., 0] > 1.3 * rgb[..., 2])
    small_components = []
    for component_runs in find_components(mask):
        area = sum(end - start for _, start, end in component_runs)
        if area <= EMBER_MAX_AREA:
            small_components.append((area, component_runs))
    elongations = [measure_elongation(component_runs) for area, component_runs in small_components if area >= 3]
    return {
        "count": len(small_components),
        "median_area_px": median_or_none([area for area, _ in small_components]),
        "median_elongation": median_or_none(elongations),
        "candidate_fraction": mask.mean(),
    }


def measure_atmosphere(crop):
    unit = crop.astype(np.float64) / 255.0
    lum = luminance(unit)
    height, width = lum.shape
    lum_mean = float(lum.mean())
    p5, p95 = np.percentile(lum, 5), np.percentile(lum, 95)
    reduced = Image.fromarray(crop).resize((width // 4, height // 4), Image.Resampling.LANCZOS)
    fine_detail = mean_absolute_laplacian(lum) / max(lum_mean, 1e-3)
    coarse_detail = mean_absolute_laplacian(luminance(np.asarray(reduced, dtype=np.float64) / 255.0)) / max(lum_mean, 1e-3)
    warmth = (unit[..., 0] - unit[..., 2]) / (unit.sum(axis=-1) + 1e-6)
    peak, floor = unit.max(axis=-1), unit.min(axis=-1)
    return {
        "luminance_mean": lum_mean,
        "luminance_std": lum.std(),
        "p5": p5,
        "p95": p95,
        "p95_over_p5": p95 / max(p5, 1e-3),
        "fine_detail": fine_detail,
        "fine_over_coarse": fine_detail / max(coarse_detail, 1e-6),
        "warmth_bright": warmth[lum >= np.percentile(lum, 90)].mean(),
        "warmth_dark": warmth[lum <= np.percentile(lum, 10)].mean(),
        "saturation": ((peak - floor) / (peak + 1e-6)).mean(),
        "grain": np.std(lum - blur_quantized_luminance(lum, 2)),
    }


def measure_water(crop):
    unit = crop.astype(np.float64) / 255.0
    lum = luminance(unit)
    return {
        "mean_red": unit[..., 0].mean(),
        "mean_green": unit[..., 1].mean(),
        "mean_blue": unit[..., 2].mean(),
        "local_contrast": np.std(lum - blur_quantized_luminance(lum, 12)),
    }


def measure_frame_difference(crop, previous_crop):
    difference = np.abs(luminance(crop.astype(np.float64)) - luminance(previous_crop.astype(np.float64))) / 255.0
    return {
        "mean": difference.mean(),
        "p95": np.percentile(difference, 95),
        "changed_fraction": (difference > FRAME_CHANGE_THRESHOLD).mean(),
    }


def measure_region(pixels, previous_pixels, region):
    x0, y0, x1, y1 = region["box"]
    crop = pixels[y0:y1, x0:x1]
    lum = luminance(crop.astype(np.float64))
    metrics = {
        "luminance_mean": lum.mean() / 255.0,
        "glint_fraction": (lum / 255.0 > GLINT_LUMINANCE).mean(),
        "detail": measure_detail(lum, region["object_height_px"]),
    }
    if previous_pixels is not None:
        metrics["frame_difference"] = measure_frame_difference(crop, previous_pixels[y0:y1, x0:x1])
    if region["kind"] in FLAME_KINDS:
        metrics["flame"] = measure_flame(crop)
    if region["kind"] in EMBER_KINDS:
        metrics["embers"] = measure_embers(crop)
    if region["kind"] in ATMOSPHERE_KINDS:
        metrics["atmosphere"] = measure_atmosphere(crop)
    if region["kind"] in WATER_KINDS:
        metrics["water"] = measure_water(crop)
    return {"name": region["name"], "kind": region["kind"], "box": region["box"], "metrics": metrics}


def measure_image(entry):
    pixels, recompressed = load_measured_pixels(entry["path"])
    height, width = pixels.shape[:2]
    previous_pixels = None
    if entry["previous"] is not None:
        previous_pixels, _ = load_measured_pixels(entry["previous"])
        if previous_pixels.shape != pixels.shape:
            raise InputError(f"{entry['id']}: previous 이미지 크기가 다르다")
    for region in entry["regions"]:
        x0, y0, x1, y1 = region["box"]
        if x1 > width or y1 > height:
            raise InputError(f"{entry['id']}/{region['name']}: box {region['box']} 가 이미지 {width}x{height} 밖이다")
    return {
        "id": entry["id"],
        "path": entry["path_text"],
        "previous": entry["previous_text"],
        "view": entry["view"],
        "condition": entry["condition"],
        "size": [width, height],
        "jpeg_recompressed": recompressed,
        "regions": [measure_region(pixels, previous_pixels, region) for region in entry["regions"]],
    }


def build_comparison_row(image, metrics, baseline):
    detail = metrics["detail"]
    frame_difference = metrics.get("frame_difference")
    return {
        "condition": image["condition"],
        "image": image["id"],
        "luminance_mean": metrics["luminance_mean"],
        "detail_above_32": detail["above_32_cycles_per_height"],
        "detail_above_64": detail["above_64_cycles_per_height"],
        "glint_fraction": metrics["glint_fraction"],
        "frame_difference_mean": frame_difference["mean"] if frame_difference else None,
        "frame_changed_fraction": frame_difference["changed_fraction"] if frame_difference else None,
        "detail_above_64_vs_first": ratio_or_none(detail["above_64_cycles_per_height"], baseline["detail"]["above_64_cycles_per_height"]),
        "glint_vs_first": ratio_or_none(metrics["glint_fraction"], baseline["glint_fraction"]),
    }


def build_comparisons(measured_images):
    groups = {}
    for image in measured_images:
        if not image["condition"]:
            continue
        for region in image["regions"]:
            groups.setdefault((image["view"], region["name"]), []).append((image, region["metrics"]))
    comparisons = []
    for (view, region_name), members in groups.items():
        if len({image["condition"] for image, _ in members}) < 2:
            continue
        baseline = members[0][1]
        rows = [build_comparison_row(image, metrics, baseline) for image, metrics in members]
        comparisons.append({"view": view, "region": region_name, "rows": rows})
    return comparisons


def rounded(value):
    if isinstance(value, dict):
        return {key: rounded(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [rounded(item) for item in value]
    if isinstance(value, (bool, np.bool_)):
        return bool(value)
    if isinstance(value, (int, np.integer)):
        return int(value)
    if isinstance(value, (float, np.floating)):
        number = float(value)
        return round(number, DECIMALS) if np.isfinite(number) else None
    return value


def format_number(value):
    return "-" if value is None else f"{value:.4g}"


def format_percent(value):
    return "-" if value is None else f"{value * 100:.3g}%"


def describe_flame(flame):
    steps = flame.get("color_steps")
    if not steps:
        return f"따뜻한 마스크 부족({format_percent(flame['warm_fraction'])})"
    green_ratios = "/".join(format_number(steps[f"p{percent}"]["green_over_red"]) for percent in FLAME_PERCENTILES)
    return (
        f"M1 G/R p10~p95 {green_ratios}, p95 B/R {format_number(steps['p95']['blue_over_red'])}, p95 휘도 {format_number(steps['p95']['luminance'])}"
        f" | M2 고온 {format_percent(flame['hot_fraction'])} 빨강포화 {format_percent(flame['red_clip_fraction'])} 흰포화 {format_percent(flame['white_clip_fraction'])}"
        f" | M5 p90/p10 {format_number(flame['contrast_p90_over_p10'])} 중앙 휘도 {format_number(flame['median_luminance'])}"
    )


def describe_atmosphere(atmosphere):
    return (
        f"L {format_number(atmosphere['luminance_mean'])}±{format_number(atmosphere['luminance_std'])} p95/p5 {format_number(atmosphere['p95_over_p5'])}"
        f" 미세 {format_number(atmosphere['fine_detail'])} 미세/거친 {format_number(atmosphere['fine_over_coarse'])}"
        f" 따뜻함 {format_number(atmosphere['warmth_bright'])}/{format_number(atmosphere['warmth_dark'])}"
        f" 채도 {format_number(atmosphere['saturation'])} 잡음 {format_number(atmosphere['grain'])}"
    )


def describe_region(region):
    metrics = region["metrics"]
    detail = metrics["detail"]
    parts = [
        f"L평균 {format_number(metrics['luminance_mean'])} 반짝임 {format_percent(metrics['glint_fraction'])}"
        f" M3 32/64 {format_number(detail['above_32_cycles_per_height'])}/{format_number(detail['above_64_cycles_per_height'])}"
    ]
    if "flame" in metrics:
        parts.append(describe_flame(metrics["flame"]))
    if "embers" in metrics:
        embers = metrics["embers"]
        parts.append(f"M4 불티 {embers['count']}개 중앙 면적 {format_number(embers['median_area_px'])}px 늘어남 {format_number(embers['median_elongation'])}")
    if "atmosphere" in metrics:
        parts.append(describe_atmosphere(metrics["atmosphere"]))
    if "water" in metrics:
        water = metrics["water"]
        parts.append(
            f"물 RGB {format_number(water['mean_red'])}/{format_number(water['mean_green'])}/{format_number(water['mean_blue'])}"
            f" 국소 대비 {format_number(water['local_contrast'])}"
        )
    if "frame_difference" in metrics:
        difference = metrics["frame_difference"]
        parts.append(f"프레임 차 평균 {format_number(difference['mean'])} 변화 {format_percent(difference['changed_fraction'])}")
    return " | ".join(parts)


def print_summary(measured_images, comparisons):
    for image in measured_images:
        recompressed = f" (JPEG {JPEG_QUALITY} 재압축)" if image["jpeg_recompressed"] else ""
        print(f"[{image['id']}] {image['size'][0]}x{image['size'][1]}{recompressed}")
        for region in image["regions"]:
            print(f"  {region['name']} ({region['kind']}): {describe_region(region)}")
    for comparison in comparisons:
        print(f"[비교] 구도 {comparison['view']} / 영역 {comparison['region']}")
        print("  조건 | M3 32 | M3 64 | 64 대비 첫 조건 | 반짝임 | 반짝임 대비 첫 조건 | 프레임 차 평균")
        for row in comparison["rows"]:
            print(
                f"  {row['condition']} | {format_number(row['detail_above_32'])} | {format_number(row['detail_above_64'])}"
                f" | {format_number(row['detail_above_64_vs_first'])} | {format_percent(row['glint_fraction'])}"
                f" | {format_number(row['glint_vs_first'])} | {format_number(row['frame_difference_mean'])}"
            )
    region_count = sum(len(image["regions"]) for image in measured_images)
    print(f"SUMMARY images={len(measured_images)} regions={region_count} comparisons={len(comparisons)}")


def read_number(entry, key, where):
    value = entry.get(key)
    if value is None:
        return None
    if not is_plain_number(value):
        raise InputError(f"{where}: '{key}' 는 숫자여야 한다")
    return float(value)


def read_list_field(entry, key, where):
    value = entry.get(key, [])
    if not isinstance(value, list):
        raise InputError(f"{where}: '{key}' 는 목록이어야 한다")
    return value


def is_integer_box(value):
    return isinstance(value, list) and len(value) == 4 and all(isinstance(v, int) and not isinstance(v, bool) for v in value)


def read_polygon(value, where):
    if not isinstance(value, list) or len(value) < 3:
        raise InputError(f"{where}: 다각형은 [x, y] 점 3개 이상의 목록이어야 한다")
    points = []
    for point in value:
        if not isinstance(point, list) or len(point) != 2 or not all(is_plain_number(v) for v in point):
            raise InputError(f"{where}: 다각형 점은 숫자 2개 [x, y] 여야 한다")
        points.append((float(point[0]), float(point[1])))
    return points


def read_exclusion(value, where):
    if is_integer_box(value):
        return {"box": value, "polygon": None}
    return {"box": None, "polygon": read_polygon(value, where)}


def read_plane(value, where):
    if not isinstance(value, list) or len(value) != 3 or not is_plain_number(value[0]) or not is_plain_number(value[1]) or value[2] not in ("L", "R"):
        raise InputError(f"{where}: planes 원소는 [a, b, \"L\"|\"R\"] 이어야 한다")
    return float(value[0]), float(value[1]), value[2]


def bound_polygon(points):
    columns = [x for x, _ in points]
    rows = [y for _, y in points]
    return [math.floor(min(columns)), math.floor(min(rows)), math.ceil(max(columns)), math.ceil(max(rows))]


def read_target_region(entry, where):
    if not isinstance(entry, dict):
        raise InputError(f"{where}: 영역은 객체여야 한다")
    name = read_text_field(entry, "name", where, required=True)
    kind = read_text_field(entry, "kind", where, required=True)
    if kind not in TARGET_KINDS:
        raise InputError(f"{where}: kind '{kind}' 는 {', '.join(TARGET_KINDS)} 중 하나여야 한다")
    polygon = read_polygon(entry["polygon"], f"{where}.polygon") if "polygon" in entry else None
    box = entry.get("box")
    if box is None and polygon is None:
        raise InputError(f"{where}: box 또는 polygon 이 필요하다")
    if box is not None and not is_integer_box(box):
        raise InputError(f"{where}: box 는 정수 4개 [x0, y0, x1, y1] 이어야 한다")
    box = box if box is not None else bound_polygon(polygon)
    x0, y0, x1, y1 = box
    if x0 < 0 or y0 < 0 or x1 - x0 < MINIMUM_REGION_SIDE or y1 - y0 < MINIMUM_REGION_SIDE:
        raise InputError(f"{where}: 영역은 음수가 없고 가로·세로 {MINIMUM_REGION_SIDE}px 이상이어야 한다")
    character_px = read_number(entry, "char_px", where)
    if character_px is not None and character_px <= 0:
        raise InputError(f"{where}: char_px 는 양수여야 한다")
    sky = entry.get("sky", False)
    if not isinstance(sky, bool) or (sky and kind != "smoke"):
        raise InputError(f"{where}: sky 는 불리언이고 kind smoke 에만 쓴다")
    manual = read_list_field(entry, "manual", where)
    for key in manual:
        if kind not in FLAME_TARGET_KINDS or key not in MANUAL_FLAME_METRICS:
            raise InputError(f"{where}: manual 은 fire·torch 영역에서 {', '.join(MANUAL_FLAME_METRICS)} 중에서만 고른다")
    if kind == "torch" and TORCH_MANUAL_TARGET not in manual:
        manual = manual + [TORCH_MANUAL_TARGET]
    return {
        "name": name,
        "kind": kind,
        "box": box,
        "polygon": polygon,
        "planes": [read_plane(plane, f"{where}.planes") for plane in read_list_field(entry, "planes", where)],
        "exclusions": [read_exclusion(item, f"{where}.exclude") for item in read_list_field(entry, "exclude", where)],
        "character_px": character_px,
        "sky": sky,
        "manual": manual,
    }


def read_target_image_entry(entry, where):
    if not isinstance(entry, dict):
        raise InputError(f"{where}: 이미지 항목은 객체여야 한다")
    path_text = read_text_field(entry, "path", where, required=True)
    fire_lit = entry.get("fire_lit")
    if fire_lit is not None and not isinstance(fire_lit, bool):
        raise InputError(f"{where}: fire_lit 은 true/false 여야 한다")
    raw_regions = entry.get("regions")
    if not isinstance(raw_regions, list) or not raw_regions:
        raise InputError(f"{where}: regions 는 비어 있지 않은 목록이어야 한다")
    regions = [read_target_region(region, f"{where}.regions[{index}]") for index, region in enumerate(raw_regions)]
    names = [region["name"] for region in regions]
    if len(set(names)) != len(names):
        raise InputError(f"{where}: 영역 이름이 겹친다")
    return {
        "id": read_text_field(entry, "id", where) or path_text,
        "path_text": path_text,
        "path": resolve_project_path(path_text),
        "previous": None,
        "label": read_text_field(entry, "label", where),
        "game_time": read_number(entry, "game_time", where),
        "fire_lit": fire_lit,
        "regions": regions,
    }


def read_target_plan(document):
    if not isinstance(document, dict) or not isinstance(document.get("images"), list) or not document["images"]:
        raise InputError("영역 JSON 최상위는 비어 있지 않은 'images' 목록을 가진 객체여야 한다")
    plan = [read_target_image_entry(entry, f"images[{index}]") for index, entry in enumerate(document["images"])]
    identifiers = [entry["id"] for entry in plan]
    if len(set(identifiers)) != len(identifiers):
        raise InputError("이미지 id 가 겹친다")
    return plan


def read_target_band(entry, where):
    if not isinstance(entry, dict) or not is_plain_number(entry.get("pass_low")) or not is_plain_number(entry.get("pass_high")):
        raise InputError(f"목표 JSON {where}: pass_low·pass_high 숫자가 필요하다")
    label = entry.get("label")
    return {"label": label if isinstance(label, str) and label else where, "pass_low": float(entry["pass_low"]), "pass_high": float(entry["pass_high"])}


def read_manual_band(entry, key):
    band = entry.get("pass") if isinstance(entry, dict) else None
    if not isinstance(band, list) or len(band) != 2 or not all(is_plain_number(v) for v in band):
        raise InputError(f"목표 JSON manual.{key}: pass 는 숫자 2개 [하한, 상한] 이어야 한다")
    return {"label": key, "pass_low": float(band[0]), "pass_high": float(band[1])}


def read_targets(document):
    if not isinstance(document, dict):
        raise InputError("목표 JSON 최상위는 객체여야 한다")
    groups = {}
    for group_name in TARGET_GROUPS:
        group = document.get(group_name)
        metrics = group.get("metrics") if isinstance(group, dict) else None
        if not isinstance(metrics, dict) or not metrics:
            raise InputError(f"목표 JSON: '{group_name}.metrics' 가 없다")
        groups[group_name] = {path: read_target_band(entry, f"{group_name}.metrics.{path}") for path, entry in metrics.items()}
    missing_neutral = [path for path in FIRE_LIT_SMOKE_COLOR_METRICS if path not in groups[NEUTRAL_SMOKE_GROUP]]
    if missing_neutral:
        raise InputError(f"목표 JSON: C8 중성 연기 판정에 {NEUTRAL_SMOKE_GROUP}.metrics 의 {', '.join(missing_neutral)} 가 필요하다")
    if BACKGROUND_TARGET_METRIC not in groups[FIRE_GROUP]:
        raise InputError(f"목표 JSON: background 판정에 {FIRE_GROUP}.metrics.{BACKGROUND_TARGET_METRIC} 가 필요하다")
    manual_entries = document.get("manual", {})
    if not isinstance(manual_entries, dict):
        raise InputError("목표 JSON: manual 은 객체여야 한다")
    manual = {key: read_manual_band(entry, key) for key, entry in manual_entries.items()}
    if NEUTRAL_FLOOR_TARGET not in manual:
        raise InputError(f"목표 JSON: manual.{NEUTRAL_FLOOR_TARGET} 가 필요하다")
    return {"groups": groups, "manual": manual, "kinds": read_kind_targets(document.get(KIND_TARGETS_KEY, {}))}


def read_kind_targets(entries):
    if not isinstance(entries, dict) or any(kind not in FLAME_TARGET_KINDS or not isinstance(bands, dict) for kind, bands in entries.items()):
        raise InputError(f"목표 JSON: {KIND_TARGETS_KEY} 는 {{{'|'.join(FLAME_TARGET_KINDS)}: {{지표 경로: 범위}}}} 객체여야 한다")
    return {kind: {path: read_target_band(entry, f"{KIND_TARGETS_KEY}.{kind}.{path}") for path, entry in bands.items()} for kind, bands in entries.items()}


def check_manual_targets(plan, targets):
    for entry in plan:
        for region in entry["regions"]:
            missing = [key for key in region["manual"] if key not in targets["manual"]]
            if missing:
                raise InputError(f"{entry['id']}/{region['name']}: 목표 JSON manual 에 {', '.join(missing)} 가 없다")


def find_polygon_pixels(points, columns, rows):
    center_columns = columns + 0.5
    center_rows = rows + 0.5
    inside = np.zeros(columns.shape, dtype=bool)
    for index, (start_x, start_y) in enumerate(points):
        end_x, end_y = points[(index + 1) % len(points)]
        if start_y == end_y:
            continue
        crosses = (start_y > center_rows) != (end_y > center_rows)
        crossing_x = start_x + (center_rows - start_y) * (end_x - start_x) / (end_y - start_y)
        inside ^= crosses & (center_columns < crossing_x)
    return inside


def build_region_mask(region):
    x0, y0, x1, y1 = region["box"]
    rows, columns = np.mgrid[y0:y1, x0:x1]
    mask = np.ones((y1 - y0, x1 - x0), dtype=bool)
    for offset, slope, side in region["planes"]:
        line = offset + slope * rows
        mask &= (columns < line - PLANE_MARGIN) if side == "L" else (columns > line + PLANE_MARGIN)
    for exclusion in region["exclusions"]:
        if exclusion["box"] is not None:
            ex0, ey0, ex1, ey1 = exclusion["box"]
            mask &= ~((columns >= ex0) & (columns < ex1) & (rows >= ey0) & (rows < ey1))
        else:
            mask &= ~find_polygon_pixels(exclusion["polygon"], columns, rows)
    if region["polygon"] is not None:
        mask &= find_polygon_pixels(region["polygon"], columns, rows)
    return mask


def convert_to_hsv(rgb):
    red, green, blue = (rgb[..., channel] / 255.0 for channel in range(3))
    peak = np.maximum(np.maximum(red, green), blue)
    floor = np.minimum(np.minimum(red, green), blue)
    spread = peak - floor
    hue = np.zeros_like(peak)
    colored = spread > 1e-6
    red_peak = colored & (peak == red)
    green_peak = colored & (peak == green) & ~red_peak
    blue_peak = colored & ~red_peak & ~green_peak
    hue[red_peak] = ((green - blue)[red_peak] / spread[red_peak]) % 6
    hue[green_peak] = (blue - red)[green_peak] / spread[green_peak] + 2
    hue[blue_peak] = (red - green)[blue_peak] / spread[blue_peak] + 4
    saturation = np.where(peak > 1e-6, spread / np.maximum(peak, 1e-6), 0)
    return hue * 60.0, saturation, peak


def is_warm_hue(hue):
    return (hue < WARM_HUE_END) | (hue >= WARM_HUE_WRAP)


def find_warm_pixels(hue, saturation, value):
    return is_warm_hue(hue) & ((saturation >= WARM_SATURATION) | (value >= WHITE_VALUE))


def convert_to_signed_hue(hue):
    return np.where(hue >= WARM_HUE_WRAP, hue - 360, hue)


def classify_flame_colors(hue, saturation, value):
    classes = np.full(hue.shape, -1)
    colored = (saturation >= WARM_SATURATION) & is_warm_hue(hue)
    classes[colored & (hue >= YELLOW_HUE_START) & (hue < WARM_HUE_END)] = 1
    classes[colored & (hue >= ORANGE_HUE_START) & (hue < YELLOW_HUE_START)] = 2
    classes[colored & (hue >= RED_HUE_START) & (hue < ORANGE_HUE_START)] = 3
    classes[colored & ((hue < RED_HUE_START) | (hue >= WARM_HUE_WRAP))] = 4
    classes[(value >= WHITE_VALUE) & (saturation < WARM_SATURATION)] = 0
    return classes


def measure_percentiles(values, percents):
    return {f"p{percent}": (float(np.percentile(values, percent)) if values.size else None) for percent in percents}


def measure_class_fractions(classes, mask):
    count = int(mask.sum())
    fractions = {name: (float(((classes == index) & mask).sum() / count) if count else None) for index, name in enumerate(FLAME_CLASSES)}
    fractions["other"] = float(((classes < 0) & mask).sum() / count) if count else None
    fractions["n"] = count
    return fractions


def measure_straight_edges(luminance_levels, mask):
    masked = np.where(mask, luminance_levels, np.nan)
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", category=RuntimeWarning)
        value_range = np.nanpercentile(masked, 95) - np.nanpercentile(masked, 5) + 1e-6
        column_steps = masked[:, 2 * EDGE_STEP:] - masked[:, :-2 * EDGE_STEP]
        row_steps = masked[2 * EDGE_STEP:, :] - masked[:-2 * EDGE_STEP, :]
        column_means = np.nanmean(column_steps, axis=0)
        row_means = np.nanmean(row_steps, axis=1)
    column_counts = np.sum(~np.isnan(column_steps), axis=0)
    row_counts = np.sum(~np.isnan(row_steps), axis=1)
    column_means = np.where(column_counts < 0.5 * mask.shape[0], 0, np.nan_to_num(column_means))
    row_means = np.where(row_counts < 0.5 * mask.shape[1], 0, np.nan_to_num(row_means))
    best_column = int(np.argmax(np.abs(column_means)))
    best_row = int(np.argmax(np.abs(row_means)))
    return {
        "vertical_edge_score": float(abs(column_means[best_column]) / value_range),
        "vertical_edge_col": best_column + EDGE_STEP,
        "horizontal_edge_score": float(abs(row_means[best_row]) / value_range),
        "horizontal_edge_row": best_row + EDGE_STEP,
        "range_p95_p5": float(value_range),
    }


def blur_inside_mask(values, mask, sigma):
    numerator = ndimage.gaussian_filter(np.where(mask, values, 0.0), sigma)
    denominator = ndimage.gaussian_filter(mask.astype(float), sigma)
    return numerator / np.maximum(denominator, 1e-6)


def measure_coherence(unit_luminance, mask):
    gradient_x = ndimage.gaussian_filter(unit_luminance, 1.0, order=(0, 1))
    gradient_y = ndimage.gaussian_filter(unit_luminance, 1.0, order=(1, 0))
    tensor_xx = ndimage.gaussian_filter(gradient_x * gradient_x, 4.0)
    tensor_yy = ndimage.gaussian_filter(gradient_y * gradient_y, 4.0)
    tensor_xy = ndimage.gaussian_filter(gradient_x * gradient_y, 4.0)
    trace = tensor_xx + tensor_yy
    coherence = np.sqrt((tensor_xx - tensor_yy) ** 2 + 4 * tensor_xy ** 2) / np.maximum(trace, 1e-9)
    weight = trace * mask
    return float((coherence * weight).sum() / max(weight.sum(), 1e-9))


def keep_large_components(mask):
    labels, count = ndimage.label(mask)
    if count == 0:
        return mask
    sizes = ndimage.sum(mask, labels, range(1, count + 1))
    return np.isin(labels, 1 + np.where(sizes >= LARGE_COMPONENT_FRACTION * sizes.max())[0])


def measure_color_band(band, hue, unit_luminance, classes):
    if band.sum() < MINIMUM_BAND_PIXELS:
        return None
    return {
        "n": int(band.sum()),
        "hue_median": float(np.median(convert_to_signed_hue(hue[band]))),
        "luma_median": float(np.median(unit_luminance[band] * 255)),
        "white_or_yellow": float(np.isin(classes[band], WHITE_OR_YELLOW_CLASSES).mean()),
        "red_or_deep": float(np.isin(classes[band], RED_OR_DEEP_RED_CLASSES).mean()),
    }


def measure_silhouette_shape(silhouette, flame, character_px):
    rows, columns = np.where(silhouette)
    box = [int(columns.min()), int(rows.min()), int(columns.max()) + 1, int(rows.max()) + 1]
    width, height = box[2] - box[0], box[3] - box[1]
    shape = {
        "flame_bbox_in_region": box,
        "flame_equiv_radius_px": float(np.sqrt(silhouette.sum() / np.pi)),
        "flame_fill_of_bbox": float(silhouette.sum() / (width * height)),
        "flame_solidity_in_sil": float(flame.sum() / max(silhouette.sum(), 1)),
        "sil_w_h_px": [width, height],
        "aspect_h_over_w": height / width,
    }
    if character_px:
        shape["flame_bbox_h_over_char"] = height / character_px
        shape["flame_bbox_w_over_char"] = width / character_px
    return shape


def measure_rim_and_core(body, hue, unit_luminance, classes):
    depth = ndimage.distance_transform_edt(body)
    rim = body & (depth <= RIM_DEPTH)
    core = body & (depth >= max(CORE_MINIMUM_DEPTH, CORE_DEPTH_FRACTION * float(depth.max())))
    return {"rim_band": measure_color_band(rim, hue, unit_luminance, classes), "core_band": measure_color_band(core, hue, unit_luminance, classes)}


def measure_edge_and_halo(unit_luminance, mask, silhouette, radius):
    outside = mask & ~silhouette
    distance_outside = ndimage.distance_transform_edt(~silhouette)
    far = outside & (distance_outside >= BACKGROUND_RING[0]) & (distance_outside <= BACKGROUND_RING[1])
    if far.sum() > 200:
        background = float(np.percentile(unit_luminance[far], 20))
    elif outside.sum():
        background = float(np.percentile(unit_luminance[outside], 5))
    else:
        background = 0.0
    flame_median = float(np.median(unit_luminance[silhouette]))
    contrast = flame_median - background
    result = {"bg_luma": background * 255, "flame_luma_median_in_sil": flame_median * 255}
    smoothed = ndimage.gaussian_filter(unit_luminance, 1.0)
    gradient = np.hypot(ndimage.sobel(smoothed, 1) / 8.0, ndimage.sobel(smoothed, 0) / 8.0)
    edge = silhouette & ~ndimage.binary_erosion(silhouette) & ndimage.binary_erosion(mask, iterations=3)
    if edge.sum() > 30 and contrast > 0.05:
        widths = np.minimum(0.8 * contrast / np.maximum(gradient[edge], 1e-4), 200)
        result["rim_width_px"] = measure_percentiles(widths, (25, 50, 75))
        result["rim_width_over_req"] = result["rim_width_px"]["p50"] / radius
    profile = {}
    for distance in HALO_DISTANCES:
        ring = outside & (distance_outside > distance - 0.5) & (distance_outside <= distance + 0.5)
        profile[distance] = float(np.median(unit_luminance[ring]) - background) if ring.sum() > 20 else None
    result["halo_excess_luma_by_dist"] = {distance: (None if excess is None else excess * 255) for distance, excess in profile.items()}
    near_excess = profile[2]
    if near_excess and near_excess > 0.004:
        result["halo_r50_px"] = next((d for d in HALO_FALLOFF_DISTANCES if profile[d] is not None and profile[d] <= 0.5 * near_excess), ">96")
        result["halo_r10_px"] = next((d for d in HALO_FALLOFF_DISTANCES if profile[d] is not None and profile[d] <= 0.1 * near_excess), ">96")
        result["halo_excess_at2_over_dL"] = near_excess / max(contrast, 1e-6)
        result["halo_excess_at16_over_dL"] = (profile[16] or 0) / max(contrast, 1e-6)
    return result


def measure_flame_interior(unit_luminance, silhouette):
    inner = silhouette & ndimage.binary_erosion(silhouette, iterations=3)
    silhouette_luminance = unit_luminance[silhouette]
    interior = {
        "interior_coherence_s1w4": measure_coherence(unit_luminance, inner),
        "sil_luma_cv": float(silhouette_luminance.std() / max(silhouette_luminance.mean(), 1e-6)),
        "sil_hot_frac_L200": float((silhouette_luminance * 255 > 200).mean()),
    }
    if inner.sum() > 100:
        inner_mean = max(np.mean(unit_luminance[inner]), 1e-6)
        fine = blur_inside_mask(unit_luminance, silhouette, 1.0) - blur_inside_mask(unit_luminance, silhouette, 6.0)
        billow = blur_inside_mask(unit_luminance, silhouette, 4.0) - blur_inside_mask(unit_luminance, silhouette, 24.0)
        interior["interior_fine_contrast_s1_s6"] = float(np.std(fine[inner]) / inner_mean)
        interior["interior_billow_contrast_s4_s24"] = float(np.std(billow[inner]) / inner_mean)
    return interior


def measure_color_interface(classes, flame, radius):
    flame_classes = np.where(flame, classes, -9)
    horizontal = (flame_classes[:, 1:] != flame_classes[:, :-1]) & flame[:, 1:] & flame[:, :-1]
    vertical = (flame_classes[1:, :] != flame_classes[:-1, :]) & flame[1:, :] & flame[:-1, :]
    changes = (horizontal.sum() + vertical.sum()) / (2.0 * flame.sum())
    return {"interface_r2": float(changes), "interface_x_req": float(changes * radius)}


def measure_target_flame(crop, mask, character_px):
    rgb = crop.astype(np.float64)
    hue, saturation, value = convert_to_hsv(rgb)
    unit_luminance = luminance(rgb) / 255.0
    warm = find_warm_pixels(hue, saturation, value)
    flame = mask & warm & (value >= FLAME_VALUE)
    glow = mask & warm & (value >= GLOW_VALUE) & (value < FLAME_VALUE)
    metrics = {"valid_px": int(mask.sum()), "flame_px": int(flame.sum()), "glow_px": int(glow.sum())}
    if flame.sum() < MINIMUM_FLAME_PIXELS:
        return metrics
    classes = classify_flame_colors(hue, saturation, value)
    metrics["flame_class_fractions"] = measure_class_fractions(classes, flame)
    metrics["glow_class_fractions"] = measure_class_fractions(classes, glow)
    metrics["glow_over_flame_area"] = float(glow.sum() / flame.sum())
    metrics["flame_luma_pct"] = measure_percentiles(unit_luminance[flame] * 255, (5, 25, 50, 75, 95))
    metrics["flame_hue_pct"] = measure_percentiles(convert_to_signed_hue(hue[flame]), (10, 50, 90))
    body = ndimage.binary_fill_holes(ndimage.binary_closing(flame | glow, iterations=2)) & mask
    silhouette = keep_large_components(ndimage.binary_fill_holes(ndimage.binary_closing(flame, iterations=2)) & mask)
    metrics.update(measure_silhouette_shape(silhouette, flame, character_px))
    radius = metrics["flame_equiv_radius_px"]
    metrics.update(measure_rim_and_core(body, hue, unit_luminance, classes))
    metrics.update(measure_edge_and_halo(unit_luminance, mask, silhouette, radius))
    metrics.update(measure_flame_interior(unit_luminance, silhouette))
    metrics.update(measure_color_interface(classes, flame, radius))
    metrics["edges"] = measure_straight_edges(unit_luminance * 255, mask)
    metrics["tool_flame"] = measure_flame(np.where(mask[..., None], crop, 0).astype(np.uint8))
    metrics.update(measure_pink_fractions(hue, luminance(rgb), flame))
    return metrics


def measure_pink_fractions(hue, luma, flame):
    red = flame & (convert_to_signed_hue(hue) < PINK_HUE_END)
    count = flame.sum()
    return {
        "pink_hi_fraction": float((red & (luma >= PINK_LUMINANCE)).sum() / count),
        "pink_hi145_fraction": float((red & (luma >= PINK_STRICT_LUMINANCE)).sum() / count),
    }


def measure_warmth(crop, mask):
    values = crop[mask].astype(np.float64)
    return float(((values[:, 0] - values[:, 2]) / (values.sum(axis=1) + 1e-6)).mean())


def measure_smoke_statistics(crop, hue, saturation, unit_luminance, smoke):
    percentiles = {percent: float(np.percentile(unit_luminance[smoke], percent)) for percent in SMOKE_PERCENTILES}
    shadow_level, lit_level = percentiles[10], percentiles[90]
    lit = smoke & (unit_luminance >= lit_level)
    shadow = smoke & (unit_luminance <= shadow_level)
    mean_rgb = crop[smoke].astype(np.float64).mean(axis=0)
    statistics = {
        "luma_pct": {f"p{percent}": level * 255 for percent, level in percentiles.items()},
        "michelson_p90_p10": (lit_level - shadow_level) / max(lit_level + shadow_level, 1e-6),
        "ratio_p90_p10": lit_level / max(shadow_level, 1 / 255),
        "saturation_mean": float(saturation[smoke].mean()),
        "hue_median": float(np.median(convert_to_signed_hue(hue[smoke]))),
        "warmth_mean": measure_warmth(crop, smoke),
        "warmth_lit_top10": measure_warmth(crop, lit),
        "warmth_shadow_bottom10": measure_warmth(crop, shadow),
        "sat_lit_top10": float(saturation[lit].mean()),
        "sat_shadow_bottom10": float(saturation[shadow].mean()),
        "mean_rgb": [float(channel) for channel in mean_rgb],
        "G_over_R_mean": float(mean_rgb[1] / max(mean_rgb[0], 1e-6)),
        "neutral_floor_green": float(mean_rgb[1]),
        "neutral_floor_blue": float(mean_rgb[2]),
    }
    inner = smoke & ndimage.binary_erosion(smoke, iterations=4)
    if inner.sum() > 200:
        inner_mean = max(np.mean(unit_luminance[inner]), 1e-6)
        for name, (narrow, wide) in SMOKE_CONTRAST_BANDS.items():
            difference = blur_inside_mask(unit_luminance, smoke, narrow) - blur_inside_mask(unit_luminance, smoke, wide)
            statistics[name] = float(np.std(difference[inner]) / inner_mean)
        statistics["coherence_s1w4"] = measure_coherence(unit_luminance, inner)
    return statistics


def find_sky_pixels(crop):
    red, green, blue = (crop[..., channel].astype(float) for channel in range(3))
    return (blue > red + 12) & (blue >= green - 5)


def measure_target_smoke(crop, mask, sky):
    rgb = crop.astype(np.float64)
    hue, saturation, value = convert_to_hsv(rgb)
    unit_luminance = luminance(rgb) / 255.0
    flame_core = find_warm_pixels(hue, saturation, value) & (value >= FLAME_VALUE) & (unit_luminance > CORE_LUMINANCE)
    smoke = mask & ~flame_core
    metrics = {"valid_px": int(mask.sum()), "flame_px_excluded": int((mask & flame_core).sum())}
    if sky:
        smoke &= ~ndimage.binary_opening(find_sky_pixels(crop), iterations=1)
        metrics["coverage_nonsky"] = float(smoke.sum() / max(mask.sum(), 1))
    if smoke.sum() < MINIMUM_SMOKE_PIXELS:
        return metrics
    metrics.update(measure_smoke_statistics(crop, hue, saturation, unit_luminance, smoke))
    metrics["edges"] = measure_straight_edges(unit_luminance * 255, mask)
    return metrics


def measure_target_plume(crop, mask):
    rgb = crop.astype(np.float64)
    hue, saturation, value = convert_to_hsv(rgb)
    luminance_levels = luminance(rgb)
    flame_core = find_warm_pixels(hue, saturation, value) & (value >= FLAME_VALUE) & (luminance_levels > CORE_LUMINANCE * 255)
    ember = ndimage.binary_dilation(flame_core, iterations=EMBER_DILATION)
    plume = mask & ~ember & (ndimage.gaussian_filter(luminance_levels, PLUME_BLUR_SIGMA) >= PLUME_MINIMUM_LUMINANCE)
    metrics = {"valid_px": int(mask.sum()), "plume_px": int(plume.sum()), "plume_frac_of_region": float(plume.sum() / max(mask.sum(), 1))}
    if plume.sum() >= MINIMUM_SMOKE_PIXELS:
        metrics.update(measure_smoke_statistics(crop, hue, saturation, luminance_levels / 255.0, plume))
    metrics["edges"] = measure_straight_edges(luminance_levels, mask & ~ember)
    return metrics


def measure_target_background(crop, mask):
    levels = luminance(crop.astype(np.float64))[mask]
    return {
        "valid_px": int(mask.sum()),
        "luma_p20": float(np.percentile(levels, 20)),
        "luma_p50": float(np.percentile(levels, 50)),
        "mean_rgb": [float(channel) for channel in crop[mask].astype(np.float64).mean(axis=0)],
    }


def measure_target_metrics(crop, mask, region):
    kind = region["kind"]
    if kind in FLAME_TARGET_KINDS:
        return measure_target_flame(crop, mask, region["character_px"])
    if kind == "smoke":
        return measure_target_smoke(crop, mask, region["sky"])
    if kind == "plume":
        return measure_target_plume(crop, mask)
    return measure_target_background(crop, mask)


def measure_frame_fire(pixels):
    hue, saturation, value = convert_to_hsv(pixels.astype(np.float64))
    flame = find_warm_pixels(hue, saturation, value) & (value >= FLAME_VALUE)
    labels, count = ndimage.label(flame)
    largest = int(np.bincount(labels.ravel())[1:].max()) if count else 0
    return {"flame_px": int(flame.sum()), "largest_component_px": largest}


def find_metric_value(metrics, path):
    value = metrics
    for key in path.split("."):
        if not isinstance(value, dict) or key not in value:
            return None
        value = value[key]
    if isinstance(value, (bool, np.bool_)) or not isinstance(value, (int, float, np.integer, np.floating)):
        return None
    number = float(value)
    return number if np.isfinite(number) else None


def make_check(group, metric_path, label, band, value):
    check = {"group": group, "metric": metric_path, "label": label, "value": value, "pass_low": band["pass_low"], "pass_high": band["pass_high"], "pass": None, "gap_bands": None}
    if value is None:
        return check
    width = max(band["pass_high"] - band["pass_low"], 1e-9)
    if value < band["pass_low"]:
        check.update({"pass": False, "gap_bands": (band["pass_low"] - value) / width})
    elif value > band["pass_high"]:
        check.update({"pass": False, "gap_bands": (value - band["pass_high"]) / width})
    else:
        check.update({"pass": True, "gap_bands": 0.0})
    return check


def build_smoke_checks(targets, metrics, fire_lit):
    checks = []
    for path, band in targets["groups"][NIGHT_SMOKE_GROUP].items():
        value = find_metric_value(metrics, path)
        if not fire_lit and path in FIRE_LIT_SMOKE_COLOR_METRICS:
            neutral_band = targets["groups"][NEUTRAL_SMOKE_GROUP][path]
            checks.append(make_check(NEUTRAL_SMOKE_GROUP, path, f"{neutral_band['label']} (C8 중성)", neutral_band, value))
            continue
        checks.append(make_check(NIGHT_SMOKE_GROUP, path, band["label"], band, value))
    floor_band = targets["manual"][NEUTRAL_FLOOR_TARGET]
    for path in NEUTRAL_FLOOR_METRICS:
        checks.append(make_check("manual", path, f"{NEUTRAL_FLOOR_TARGET} ({path})", floor_band, find_metric_value(metrics, path)))
    return checks


def build_region_checks(targets, region, metrics, fire_lit):
    kind = region["kind"]
    if kind in SMOKE_TARGET_KINDS:
        return build_smoke_checks(targets, metrics, fire_lit)
    if kind == "background":
        band = targets["groups"][FIRE_GROUP][BACKGROUND_TARGET_METRIC]
        return [make_check("background", "luma_p20", f"background region luma p20 (band: {band['label']})", band, find_metric_value(metrics, "luma_p20"))]
    checks = [make_check(FIRE_GROUP, path, band["label"], band, find_metric_value(metrics, path)) for path, band in targets["groups"][FIRE_GROUP].items()]
    for key in region["manual"]:
        path = MANUAL_FLAME_METRICS[key]
        checks.append(make_check("manual", path, key, targets["manual"][key], find_metric_value(metrics, path)))
    for path, band in targets["kinds"].get(kind, {}).items():
        checks.append(make_check(f"{KIND_TARGETS_KEY}.{kind}", path, band["label"], band, find_metric_value(metrics, path)))
    return checks


def measure_target_region(pixels, region, targets, fire_lit, image_id):
    x0, y0, x1, y1 = region["box"]
    mask = build_region_mask(region)
    if not mask.any():
        raise InputError(f"{image_id}/{region['name']}: 마스크 안에 화소가 없다")
    metrics = measure_target_metrics(pixels[y0:y1, x0:x1], mask, region)
    checks = build_region_checks(targets, region, metrics, fire_lit)
    measured = [check for check in checks if check["pass"] is not None]
    return {
        "name": region["name"],
        "kind": region["kind"],
        "box": region["box"],
        "metrics": metrics,
        "checks": checks,
        "pass_count": sum(1 for check in measured if check["pass"]),
        "measured_count": len(measured),
    }


def measure_target_image(entry, targets):
    pixels, recompressed = load_measured_pixels(entry["path"])
    height, width = pixels.shape[:2]
    for region in entry["regions"]:
        x0, y0, x1, y1 = region["box"]
        if x1 > width or y1 > height:
            raise InputError(f"{entry['id']}/{region['name']}: 영역 {region['box']} 가 이미지 {width}x{height} 밖이다")
    frame_fire = measure_frame_fire(pixels)
    detected = frame_fire["largest_component_px"] >= FIRE_LIT_MINIMUM_COMPONENT_PIXELS
    fire_lit = detected if entry["fire_lit"] is None else entry["fire_lit"]
    frame_fire.update({"fire_lit": fire_lit, "fire_lit_source": "auto" if entry["fire_lit"] is None else "override"})
    return {
        "id": entry["id"],
        "path": entry["path_text"],
        "label": entry["label"],
        "game_time": entry["game_time"],
        "size": [width, height],
        "jpeg_recompressed": recompressed,
        "frame_fire": frame_fire,
        "regions": [measure_target_region(pixels, region, targets, fire_lit, entry["id"]) for region in entry["regions"]],
    }


def format_target_number(value):
    if value is None:
        return "-"
    return f"{value:.3f}" if abs(value) < 10 else f"{value:.1f}"


def format_check_cell(check):
    if check is None:
        return ""
    if check["value"] is None:
        return "-"
    return format_target_number(check["value"]) + ("" if check["pass"] else " X")


def describe_target_column(image, region):
    time_text = "" if image["game_time"] is None else f" t={image['game_time']:g}s"
    return f"{image['id']}/{region['name']}{time_text}"


def describe_fire_regime(image):
    frame_fire = image["frame_fire"]
    regime = "불빛" if frame_fire["fire_lit"] else "중성(C8)"
    source = " 지정" if frame_fire["fire_lit_source"] == "override" else ""
    return f"{regime}{source} {frame_fire['largest_component_px']}px"


def print_target_family(title, columns, is_smoke_family):
    rows = {}
    for _, region in columns:
        for check in region["checks"]:
            rows.setdefault((check["group"], check["metric"]), check)
    print(f"### {title}")
    print("| 지표 | 합격 범위 | " + " | ".join(describe_target_column(image, region) for image, region in columns) + " |")
    print("|" + "---|" * (2 + len(columns)))
    for key, first in rows.items():
        cells = [format_check_cell(next((check for check in region["checks"] if (check["group"], check["metric"]) == key), None)) for _, region in columns]
        band_text = f"{format_target_number(first['pass_low'])}..{format_target_number(first['pass_high'])}"
        print(f"| {first['label']} | {band_text} | " + " | ".join(cells) + " |")
    print("| 합격 수 | | " + " | ".join(f"{region['pass_count']}/{region['measured_count']}" for _, region in columns) + " |")
    if is_smoke_family:
        print(f"| C8 판정(프레임 최대 불 덩어리, 기준 {FIRE_LIT_MINIMUM_COMPONENT_PIXELS}px) | | " + " | ".join(describe_fire_regime(image) for image, _ in columns) + " |")
    print()


def print_target_report(measured_images):
    for image in measured_images:
        recompressed = f" (JPEG {JPEG_QUALITY} 재압축)" if image["jpeg_recompressed"] else ""
        time_text = "" if image["game_time"] is None else f" game_time={image['game_time']:g}s"
        print(f"[{image['id']}] {image['size'][0]}x{image['size'][1]}{recompressed}{time_text} {describe_fire_regime(image)}")
    print()
    for title, kinds in TARGET_FAMILIES:
        columns = [(image, region) for image in measured_images for region in image["regions"] if region["kind"] in kinds]
        if columns:
            print_target_family(title, columns, kinds == SMOKE_TARGET_KINDS)
    regions = [region for image in measured_images for region in image["regions"]]
    passed = sum(region["pass_count"] for region in regions)
    measured = sum(region["measured_count"] for region in regions)
    unmeasured = sum(len(region["checks"]) - region["measured_count"] for region in regions)
    print(f"SUMMARY mode=targets images={len(measured_images)} regions={len(regions)} passed={passed}/{measured} unmeasured={unmeasured}")


def run_target_mode(arguments, output_path):
    if ndimage is None:
        print("입력 오류: --targets 는 scipy 가 필요하다(python -m pip install scipy)", file=sys.stderr)
        return 2
    try:
        targets = read_targets(load_json(resolve_project_path(arguments.targets)))
        plan = read_target_plan(load_json(resolve_project_path(arguments.regions)))
        check_manual_targets(plan, targets)
    except InputError as error:
        print(f"입력 오류: {error}", file=sys.stderr)
        return 2
    if is_inside(output_path, DOCS_DIR) and uses_live2_gallery(plan):
        print(f"거부(RR R-14): 출력 {output_path} 이 Docs/ 아래인데 입력에 LIVE-2 갤러리 이미지가 있다. Saved/ 아래로 출력한다", file=sys.stderr)
        return 1
    try:
        measured_images = [measure_target_image(entry, targets) for entry in plan]
    except InputError as error:
        print(f"입력 오류: {error}", file=sys.stderr)
        return 2
    report = {
        "tool": "measure_fx_look",
        "mode": "targets",
        "regions_file": arguments.regions,
        "targets_file": arguments.targets,
        "rules": {
            "jpeg_quality": JPEG_QUALITY,
            "jpeg_subsampling": JPEG_SUBSAMPLING,
            "fire_lit_minimum_component_px": FIRE_LIT_MINIMUM_COMPONENT_PIXELS,
            "c8_neutral_group": NEUTRAL_SMOKE_GROUP,
            "c8_neutral_metrics": list(FIRE_LIT_SMOKE_COLOR_METRICS),
            "neutral_floor_target": NEUTRAL_FLOOR_TARGET,
        },
        "images": measured_images,
    }
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(rounded(report), ensure_ascii=False, indent=1) + "\n", encoding="utf-8", newline="\n")
    print_target_report(measured_images)
    return 0


def parse_arguments():
    parser = argparse.ArgumentParser(
        description="FX-7a 이미지 지표: 영역 JSON의 이미지·영역을 재서 결과 JSON과 요약을 낸다. --targets 를 주면 FX-8c 지표를 재서 목표 합격 범위와 대조한다",
        epilog=(
            "--targets 영역 JSON: {\"images\": [{\"path\", \"id\"?, \"game_time\"? 초, \"label\"?, \"fire_lit\"? true|false(C8 자동 판정 덮어쓰기), "
            "\"regions\": [{\"name\", \"kind\": fire|torch|smoke|plume|background, \"box\": [x0,y0,x1,y1] 또는 \"polygon\": [[x,y],...](화소 중심 판정), "
            "\"exclude\"?: [사각형 [x0,y0,x1,y1] 또는 다각형], \"planes\"?: [[a,b,\"L\"|\"R\"]](L은 x<a+b·y-8, R은 x>a+b·y+8만 남김), "
            "\"char_px\"? 캐릭터 키 화소(torch 높이 판정), \"sky\"? smoke 의 하늘 화소 제외, \"manual\"?: [campfire_flame_height_over_width 등]}]}]}. "
            "smoke 는 영역 전체(1차 상자 방식), plume 은 플룸 마스크(2차 방식) 안에서 잰다. 목표 JSON 은 FX-8c live2-targets 의 targets.json 형식. "
            "목표 JSON 선택 키 kind_metrics: {fire|torch: {지표 경로: {pass_low, pass_high, label?}}}, 그 kind 영역에만 덧붙여 판정"
        ),
    )
    parser.add_argument("--regions", required=True, help="영역 JSON 경로(상대 경로는 프로젝트 루트 기준)")
    parser.add_argument("--output", required=True, help="결과 JSON 경로(상대 경로는 프로젝트 루트 기준, Docs/ 아래면 LIVE-2 입력 금지)")
    parser.add_argument("--targets", help="목표 JSON 경로(상대 경로는 프로젝트 루트 기준). 주면 FX-8c 격차 판정 모드")
    return parser.parse_args()


def main():
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    arguments = parse_arguments()
    output_path = resolve_project_path(arguments.output)
    if arguments.targets:
        return run_target_mode(arguments, output_path)
    try:
        plan = read_plan(load_json(resolve_project_path(arguments.regions)))
    except InputError as error:
        print(f"입력 오류: {error}", file=sys.stderr)
        return 2
    if is_inside(output_path, DOCS_DIR) and uses_live2_gallery(plan):
        print(f"거부(RR R-14): 출력 {output_path} 이 Docs/ 아래인데 입력에 LIVE-2 갤러리 이미지가 있다. Saved/ 아래로 출력한다", file=sys.stderr)
        return 1
    try:
        measured_images = [measure_image(entry) for entry in plan]
    except InputError as error:
        print(f"입력 오류: {error}", file=sys.stderr)
        return 2
    comparisons = build_comparisons(measured_images)
    report = {"tool": "measure_fx_look", "regions_file": arguments.regions, "images": measured_images, "comparisons": comparisons}
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(rounded(report), ensure_ascii=False, indent=1) + "\n", encoding="utf-8", newline="\n")
    print_summary(measured_images, comparisons)
    return 0


if __name__ == "__main__":
    sys.exit(main())
