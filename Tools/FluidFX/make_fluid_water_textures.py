"""실행 환경: 시스템 Python 3 (numpy, Pillow 필요), 에디터 불필요.
실행 명령: python Tools/FluidFX/make_fluid_water_textures.py [--output-dir 폴더]
출력: Saved/FluidFX/water_textures/T_TD_FluidWaterDetailN.png(512², 법선), T_TD_FluidFoamNoise.png(512², R 레이스 그물·G 덮임 구름·B 기포), T_TD_FluidFlowPhaseNoise.png(256², 위상 오프셋), T_TD_FluidCaustics.png(512², 굴절 광자 분사 RGB, 값×4 = 평균 1 기준 밝기), T_TD_FluidSandRipplesN.png(512², 모래 물결 법선), T_TD_FluidSandDetail.png(512², R 색 변화·G 물결 높이·B 알갱이), preview.png(2×2 타일 미리보기)와 주기 경계 검사 결과(JSON, 표준 출력)
상태: 미검증(에디터 가져오기 전). 고정 시드 FFT·셀 노이즈라 같은 입력이면 같은 픽셀이며, 주기 경계 비율이 문턱을 넘으면 종료 코드 1이다. LIVE-2 원본 텍스처는 쓰지 않는다(D-49).
"""
import argparse
import hashlib
import json
import os
import sys

import numpy
from numpy.random import default_rng
from PIL import Image


DETAIL_NORMAL_SIZE = 512
FOAM_NOISE_SIZE = 512
PHASE_NOISE_SIZE = 256
CAUSTICS_SIZE = 512
SAND_SIZE = 512

DETAIL_NORMAL_SEED = 20261018
FOAM_CLOUD_SEED = 20261019
FOAM_BUBBLE_SEED = 20261020
FOAM_RIDGE_SEED = 20261021
PHASE_NOISE_SEED = 20261022
FOAM_WARP_SEED = 20261023
CAUSTICS_SEED = 20261024
SAND_RIPPLE_SEED = 20261025
SAND_PATCH_SEED = 20261026
SAND_GRAIN_SEED = 20261027

DETAIL_HEIGHT_EXPONENT = 1.8
DETAIL_FREQUENCY_BAND = (3.0, 120.0)
DETAIL_SLOPE_STANDARD_DEVIATION = 0.55
DETAIL_SLOPE_LIMIT = 1.6

FOAM_CLOUD_EXPONENT = 1.2
FOAM_CLOUD_BAND = (2.0, 64.0)
FOAM_LACE_OCTAVES = ((6, 0.12, 0.6), (14, 0.16, 1.0), (32, 0.22, 0.75))
FOAM_WARP_CELLS = 0.12
FOAM_FINE_BUBBLE_CELLS = 72
FOAM_BUBBLE_RING_RADIUS = 0.34
FOAM_BUBBLE_RING_WIDTH = 0.09

CAUSTICS_BAND = (2.0, 18.0)
CAUSTICS_EXPONENT = 2.4
CAUSTICS_FOLD_FRACTION = 0.16
CAUSTICS_SUBSAMPLES = 3
CAUSTICS_DISPERSION = (0.955, 1.0, 1.045)
CAUSTICS_BLUR_PIXELS = 0.7
CAUSTICS_STORE_MAXIMUM = 4.0

SAND_RIPPLE_PEAK_CYCLES = 22.0
SAND_RIPPLE_BANDWIDTH_CYCLES = 5.0
SAND_RIPPLE_DIRECTION = 0.35
SAND_RIPPLE_SPREAD_POWER = 6.0
SAND_RIPPLE_SLOPE_STANDARD_DEVIATION = 0.32
SAND_PATCH_BAND = (1.0, 10.0)
SAND_GRAIN_CELLS = 140

PHASE_EXPONENT = 2.0
PHASE_BAND = (1.0, 8.0)

SEAM_RATIO_LIMIT = 1.6

OUTPUT_FILE_NAMES = {
    "detail_normal": "T_TD_FluidWaterDetailN.png",
    "foam_noise": "T_TD_FluidFoamNoise.png",
    "phase_noise": "T_TD_FluidFlowPhaseNoise.png",
    "caustics": "T_TD_FluidCaustics.png",
    "sand_normal": "T_TD_FluidSandRipplesN.png",
    "sand_detail": "T_TD_FluidSandDetail.png",
}
PREVIEW_FILE_NAME = "preview.png"
PREVIEW_TILE_SIZE = 256


def project_root():
    return os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def default_output_dir():
    return os.path.join(project_root(), "Saved", "FluidFX", "water_textures")


def make_spectral_field(size, seed, exponent, frequency_band):
    generator = default_rng(seed)
    spectrum = numpy.fft.fft2(generator.standard_normal((size, size)))
    frequency_x = numpy.fft.fftfreq(size)[:, None] * size
    frequency_y = numpy.fft.fftfreq(size)[None, :] * size
    radius = numpy.sqrt(frequency_x * frequency_x + frequency_y * frequency_y)
    safe_radius = numpy.maximum(radius, 1.0)
    low_edge, high_edge = frequency_band
    band_weight = numpy.clip((radius - low_edge * 0.5) / (low_edge * 0.5), 0.0, 1.0)
    band_weight = band_weight * numpy.clip((high_edge * 1.5 - radius) / (high_edge * 0.5), 0.0, 1.0)
    shaped = spectrum * (safe_radius ** (-exponent)) * band_weight
    shaped[0, 0] = 0.0
    field = numpy.real(numpy.fft.ifft2(shaped))
    return (field - field.mean()) / max(float(field.std()), 1e-12)


def equalize_to_unit_range(field):
    flat = field.reshape(-1)
    order = numpy.argsort(flat, kind="stable")
    ranks = numpy.empty(flat.shape[0], dtype=numpy.float64)
    ranks[order] = numpy.arange(flat.shape[0], dtype=numpy.float64)
    return (ranks / float(flat.shape[0] - 1)).reshape(field.shape)


def make_detail_normal_map():
    size = DETAIL_NORMAL_SIZE
    height = make_spectral_field(size, DETAIL_NORMAL_SEED, DETAIL_HEIGHT_EXPONENT, DETAIL_FREQUENCY_BAND)
    slope_x = 0.5 * (numpy.roll(height, -1, axis=1) - numpy.roll(height, 1, axis=1))
    slope_y = 0.5 * (numpy.roll(height, -1, axis=0) - numpy.roll(height, 1, axis=0))
    deviation = max(float(numpy.sqrt(0.5 * (slope_x.var() + slope_y.var()))), 1e-12)
    scale = DETAIL_SLOPE_STANDARD_DEVIATION / deviation
    slope_x = numpy.clip(slope_x * scale, -DETAIL_SLOPE_LIMIT, DETAIL_SLOPE_LIMIT)
    slope_y = numpy.clip(slope_y * scale, -DETAIL_SLOPE_LIMIT, DETAIL_SLOPE_LIMIT)
    normal = numpy.stack([-slope_x, -slope_y, numpy.ones_like(slope_x)], axis=-1)
    normal = normal / numpy.linalg.norm(normal, axis=-1, keepdims=True)
    return numpy.clip(numpy.rint((normal * 0.5 + 0.5) * 255.0), 0, 255).astype(numpy.uint8)


def make_periodic_cellular_distances(size, seed, cell_count, warp_x=None, warp_y=None):
    generator = default_rng(seed)
    jitter = generator.random((cell_count, cell_count, 2))
    cell_size = size / float(cell_count)
    pixel_axis = (numpy.arange(size, dtype=numpy.float64) + 0.5) / cell_size
    pixel_x, pixel_y = numpy.meshgrid(pixel_axis, pixel_axis, indexing="xy")
    if warp_x is not None:
        pixel_x = pixel_x + warp_x
        pixel_y = pixel_y + warp_y
    cell_x = numpy.floor(pixel_x).astype(numpy.int64)
    cell_y = numpy.floor(pixel_y).astype(numpy.int64)
    nearest = numpy.full((size, size), 1.0e9)
    second = numpy.full((size, size), 1.0e9)
    for offset_y in (-1, 0, 1):
        for offset_x in (-1, 0, 1):
            neighbor_x = cell_x + offset_x
            neighbor_y = cell_y + offset_y
            point_x = neighbor_x + jitter[neighbor_y % cell_count, neighbor_x % cell_count, 0]
            point_y = neighbor_y + jitter[neighbor_y % cell_count, neighbor_x % cell_count, 1]
            distance = numpy.sqrt((point_x - pixel_x) ** 2 + (point_y - pixel_y) ** 2)
            second = numpy.where(distance < nearest, nearest, numpy.minimum(second, distance))
            nearest = numpy.minimum(nearest, distance)
    return nearest, second


def smoothstep(edge_low, edge_high, value):
    ratio = numpy.clip((value - edge_low) / (edge_high - edge_low), 0.0, 1.0)
    return ratio * ratio * (3.0 - 2.0 * ratio)


def make_foam_lace(size):
    warp_x = make_spectral_field(size, FOAM_WARP_SEED, 1.6, (1.0, 12.0)) * FOAM_WARP_CELLS
    warp_y = make_spectral_field(size, FOAM_WARP_SEED + 1, 1.6, (1.0, 12.0)) * FOAM_WARP_CELLS
    lace = numpy.zeros((size, size))
    for octave_index, (cell_count, line_width, weight) in enumerate(FOAM_LACE_OCTAVES):
        warp_scale = cell_count / float(FOAM_LACE_OCTAVES[0][0])
        nearest, second = make_periodic_cellular_distances(size, FOAM_BUBBLE_SEED + 7 * octave_index, cell_count, warp_x * warp_scale, warp_y * warp_scale)
        lines = 1.0 - smoothstep(0.0, line_width, second - nearest)
        lace = numpy.maximum(lace, lines * weight)
    return lace


def make_foam_noise_map():
    size = FOAM_NOISE_SIZE
    lace = make_foam_lace(size)
    cloud = make_spectral_field(size, FOAM_CLOUD_SEED, FOAM_CLOUD_EXPONENT, FOAM_CLOUD_BAND)
    bubble_nearest, _ = make_periodic_cellular_distances(size, FOAM_RIDGE_SEED, FOAM_FINE_BUBBLE_CELLS)
    bubbles = numpy.exp(-((bubble_nearest - FOAM_BUBBLE_RING_RADIUS) / FOAM_BUBBLE_RING_WIDTH) ** 2)
    channels = [equalize_to_unit_range(lace + 0.3 * cloud), equalize_to_unit_range(cloud), equalize_to_unit_range(bubbles + 0.3 * lace)]
    stacked = numpy.stack(channels, axis=-1)
    return numpy.clip(numpy.rint(stacked * 255.0), 0, 255).astype(numpy.uint8)


def make_phase_noise_map():
    field = make_spectral_field(PHASE_NOISE_SIZE, PHASE_NOISE_SEED, PHASE_EXPONENT, PHASE_BAND)
    gray = numpy.clip(numpy.rint(equalize_to_unit_range(field) * 255.0), 0, 255).astype(numpy.uint8)
    return numpy.stack([gray, gray, gray], axis=-1)


def frequency_grid(size):
    frequency_x = numpy.fft.fftfreq(size)[None, :]
    frequency_y = numpy.fft.fftfreq(size)[:, None]
    return frequency_x, frequency_y


def blur_periodic(field, sigma_pixels):
    frequency_x, frequency_y = frequency_grid(field.shape[0])
    kernel = numpy.exp(-2.0 * numpy.pi * numpy.pi * sigma_pixels * sigma_pixels * (frequency_x * frequency_x + frequency_y * frequency_y))
    return numpy.real(numpy.fft.ifft2(numpy.fft.fft2(field) * kernel))


def choose_caustic_focus(height_spectrum, frequency_x, frequency_y):
    second_xx = numpy.real(numpy.fft.ifft2(height_spectrum * (2j * numpy.pi * frequency_x) ** 2))
    second_yy = numpy.real(numpy.fft.ifft2(height_spectrum * (2j * numpy.pi * frequency_y) ** 2))
    second_xy = numpy.real(numpy.fft.ifft2(height_spectrum * (2j * numpy.pi * frequency_x) * (2j * numpy.pi * frequency_y)))
    half_trace = 0.5 * (second_xx + second_yy)
    radius = numpy.sqrt(0.25 * (second_xx - second_yy) ** 2 + second_xy ** 2)
    most_negative_curvature = numpy.maximum(-(half_trace - radius), 1e-12)
    return 1.0 / float(numpy.percentile(most_negative_curvature, 100.0 * (1.0 - CAUSTICS_FOLD_FRACTION)))


def splat_caustic_channel(height_spectrum, frequency_x, frequency_y, focus):
    size = height_spectrum.shape[0]
    accumulated = numpy.zeros(size * size)
    row_index, column_index = numpy.meshgrid(numpy.arange(size, dtype=numpy.float64), numpy.arange(size, dtype=numpy.float64), indexing="ij")
    sample_weight = 1.0 / (CAUSTICS_SUBSAMPLES * CAUSTICS_SUBSAMPLES)
    for sub_y in range(CAUSTICS_SUBSAMPLES):
        for sub_x in range(CAUSTICS_SUBSAMPLES):
            shift_x = (sub_x + 0.5) / CAUSTICS_SUBSAMPLES - 0.5
            shift_y = (sub_y + 0.5) / CAUSTICS_SUBSAMPLES - 0.5
            shifted = height_spectrum * numpy.exp(2j * numpy.pi * (frequency_x * shift_x + frequency_y * shift_y))
            slope_x = numpy.real(numpy.fft.ifft2(shifted * (2j * numpy.pi * frequency_x)))
            slope_y = numpy.real(numpy.fft.ifft2(shifted * (2j * numpy.pi * frequency_y)))
            target_x = column_index + shift_x - focus * slope_x
            target_y = row_index + shift_y - focus * slope_y
            floor_x = numpy.floor(target_x)
            floor_y = numpy.floor(target_y)
            fraction_x = target_x - floor_x
            fraction_y = target_y - floor_y
            base_x = floor_x.astype(numpy.int64)
            base_y = floor_y.astype(numpy.int64)
            corners = (
                (0, 0, (1.0 - fraction_x) * (1.0 - fraction_y)),
                (1, 0, fraction_x * (1.0 - fraction_y)),
                (0, 1, (1.0 - fraction_x) * fraction_y),
                (1, 1, fraction_x * fraction_y),
            )
            for corner_x, corner_y, weight in corners:
                index = ((base_y + corner_y) % size) * size + (base_x + corner_x) % size
                accumulated += numpy.bincount(index.reshape(-1), weights=(weight * sample_weight).reshape(-1), minlength=size * size)
    return blur_periodic(accumulated.reshape(size, size), CAUSTICS_BLUR_PIXELS)


def make_caustics_map():
    size = CAUSTICS_SIZE
    height = make_spectral_field(size, CAUSTICS_SEED, CAUSTICS_EXPONENT, CAUSTICS_BAND)
    height_spectrum = numpy.fft.fft2(height)
    frequency_x, frequency_y = frequency_grid(size)
    focus = choose_caustic_focus(height_spectrum, frequency_x, frequency_y)
    channels = [splat_caustic_channel(height_spectrum, frequency_x, frequency_y, focus * dispersion) for dispersion in CAUSTICS_DISPERSION]
    stacked = numpy.stack(channels, axis=-1) / CAUSTICS_STORE_MAXIMUM
    return numpy.clip(numpy.rint(stacked * 255.0), 0, 255).astype(numpy.uint8), focus


def make_directional_field(size, seed, peak_cycles, bandwidth_cycles, direction, spread_power):
    generator = default_rng(seed)
    spectrum = numpy.fft.fft2(generator.standard_normal((size, size)))
    frequency_x, frequency_y = frequency_grid(size)
    cycles_x = frequency_x * size
    cycles_y = frequency_y * size
    radius = numpy.sqrt(cycles_x * cycles_x + cycles_y * cycles_y)
    alignment = numpy.abs(numpy.cos(numpy.arctan2(cycles_y, cycles_x) - direction))
    shaped = spectrum * numpy.exp(-0.5 * ((radius - peak_cycles) / bandwidth_cycles) ** 2) * alignment ** spread_power
    shaped[0, 0] = 0.0
    field = numpy.real(numpy.fft.ifft2(shaped))
    return (field - field.mean()) / max(float(field.std()), 1e-12)


def height_to_normal_pixels(height, slope_standard_deviation):
    frequency_x, frequency_y = frequency_grid(height.shape[0])
    spectrum = numpy.fft.fft2(height)
    slope_x = numpy.real(numpy.fft.ifft2(spectrum * (2j * numpy.pi * frequency_x)))
    slope_y = numpy.real(numpy.fft.ifft2(spectrum * (2j * numpy.pi * frequency_y)))
    deviation = max(float(numpy.sqrt(0.5 * (slope_x.var() + slope_y.var()))), 1e-12)
    scale = slope_standard_deviation / deviation
    normal = numpy.stack([-slope_x * scale, -slope_y * scale, numpy.ones_like(slope_x)], axis=-1)
    normal = normal / numpy.linalg.norm(normal, axis=-1, keepdims=True)
    return numpy.clip(numpy.rint((normal * 0.5 + 0.5) * 255.0), 0, 255).astype(numpy.uint8)


def make_sand_maps():
    size = SAND_SIZE
    ripple = make_directional_field(size, SAND_RIPPLE_SEED, SAND_RIPPLE_PEAK_CYCLES, SAND_RIPPLE_BANDWIDTH_CYCLES, SAND_RIPPLE_DIRECTION, SAND_RIPPLE_SPREAD_POWER)
    sharpened_ripple = numpy.sign(ripple) * numpy.abs(ripple) ** 0.8 - 0.25 * numpy.maximum(ripple, 0.0) ** 2
    patch = make_spectral_field(size, SAND_PATCH_SEED, 1.4, SAND_PATCH_BAND)
    grain_nearest, _ = make_periodic_cellular_distances(size, SAND_GRAIN_SEED, SAND_GRAIN_CELLS)
    grains = numpy.exp(-(grain_nearest / 0.22) ** 2)
    color_variation = equalize_to_unit_range(0.65 * patch + 0.35 * sharpened_ripple)
    detail = numpy.stack([color_variation, equalize_to_unit_range(sharpened_ripple), equalize_to_unit_range(grains)], axis=-1)
    detail_pixels = numpy.clip(numpy.rint(detail * 255.0), 0, 255).astype(numpy.uint8)
    return height_to_normal_pixels(sharpened_ripple, SAND_RIPPLE_SLOPE_STANDARD_DEVIATION), detail_pixels


def write_preview(textures, path):
    tile = PREVIEW_TILE_SIZE
    keys = list(textures.keys())
    sheet = Image.new("RGB", (tile * 2 * len(keys), tile * 2))
    for key_index, key in enumerate(keys):
        pixels = textures[key]
        if key == "caustics":
            pixels = numpy.clip(pixels.astype(numpy.float64) * 1.6, 0, 255).astype(numpy.uint8)
        single = Image.fromarray(pixels).convert("RGB").resize((tile, tile), Image.LANCZOS)
        for corner_y in range(2):
            for corner_x in range(2):
                sheet.paste(single, (key_index * tile * 2 + corner_x * tile, corner_y * tile))
    sheet.save(path, format="PNG", optimize=True)


def measure_seam_ratio(pixels):
    values = pixels.astype(numpy.float64)
    horizontal_interior = numpy.abs(values[:, 1:, :] - values[:, :-1, :]).mean()
    vertical_interior = numpy.abs(values[1:, :, :] - values[:-1, :, :]).mean()
    horizontal_seam = numpy.abs(values[:, 0, :] - values[:, -1, :]).mean()
    vertical_seam = numpy.abs(values[0, :, :] - values[-1, :, :]).mean()
    return {
        "horizontal": float(horizontal_seam / max(horizontal_interior, 1e-9)),
        "vertical": float(vertical_seam / max(vertical_interior, 1e-9)),
        "horizontal_seam_mean_difference": float(horizontal_seam),
        "horizontal_interior_mean_difference": float(horizontal_interior),
    }


def write_png(pixels, path):
    Image.fromarray(pixels).save(path, format="PNG", optimize=True)
    with open(path, "rb") as written:
        return hashlib.sha256(written.read()).hexdigest()


def parse_arguments(argv):
    parser = argparse.ArgumentParser(description="FluidFX 물 텍스처 3종을 고정 시드로 만든다.")
    parser.add_argument("--output-dir", default=default_output_dir())
    return parser.parse_args(argv)


def main(argv):
    arguments = parse_arguments(argv)
    os.makedirs(arguments.output_dir, exist_ok=True)

    caustics_pixels, caustic_focus = make_caustics_map()
    sand_normal_pixels, sand_detail_pixels = make_sand_maps()
    textures = {
        "detail_normal": make_detail_normal_map(),
        "foam_noise": make_foam_noise_map(),
        "phase_noise": make_phase_noise_map(),
        "caustics": caustics_pixels,
        "sand_normal": sand_normal_pixels,
        "sand_detail": sand_detail_pixels,
    }

    report = {"output_dir": arguments.output_dir, "textures": {}, "seam_ratio_limit": SEAM_RATIO_LIMIT}
    failures = []
    for key, pixels in textures.items():
        path = os.path.join(arguments.output_dir, OUTPUT_FILE_NAMES[key])
        digest = write_png(pixels, path)
        seam = measure_seam_ratio(pixels)
        report["textures"][key] = {
            "path": path,
            "width": int(pixels.shape[1]),
            "height": int(pixels.shape[0]),
            "sha256": digest,
            "seam": seam,
        }
        if seam["horizontal"] > SEAM_RATIO_LIMIT or seam["vertical"] > SEAM_RATIO_LIMIT:
            failures.append(key)

    normal_pixels = textures["detail_normal"].astype(numpy.float64) / 255.0 * 2.0 - 1.0
    report["detail_normal_mean_slope_magnitude"] = float(numpy.hypot(normal_pixels[..., 0], normal_pixels[..., 1]).mean())
    caustic_values = textures["caustics"].astype(numpy.float64) / 255.0 * CAUSTICS_STORE_MAXIMUM
    report["caustics"] = {
        "focus_pixels_per_slope": caustic_focus,
        "mean": float(caustic_values.mean()),
        "clipped_fraction": float((textures["caustics"] == 255).mean()),
        "bright_fraction_over_2": float((caustic_values[..., 1] > 2.0).mean()),
    }
    preview_path = os.path.join(arguments.output_dir, PREVIEW_FILE_NAME)
    write_preview(textures, preview_path)
    report["preview"] = preview_path
    report["failures"] = failures
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
