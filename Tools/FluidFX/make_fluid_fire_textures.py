"""실행 환경: 시스템 Python 3 (numpy, Pillow 필요), 에디터 불필요.
실행 명령: python Tools/FluidFX/make_fluid_fire_textures.py [--output-dir 폴더]
출력: Saved/FluidFX/fire_textures/T_TD_FluidNoiseAtlas.png(512², 64³ 볼륨의 8×8 타일 아틀라스, R·G 격자 4칸 독립 2개·B 8칸·A 16칸 주기 그라디언트 노이즈, 값 1~255), preview.png(채널 2×2 타일·불꽃 능선·장작 균열·연기 감산·낟알 합성 미리보기)와 주기 경계·분포 검사 결과(JSON, 표준 출력)
상태: 미검증(에디터 가져오기 전). 고정 시드라 같은 입력이면 같은 픽셀이며, 경계 차이 비율이 문턱을 넘거나 분포가 범위를 벗어나면 종료 코드 1이다. 셰이더(TDFluidFlameCard·TDFluidVolumeRaymarch·TDFluidFakeVolume·TDFluidCharredWood)가 같은 채널 약속을 쓴다. LIVE-2 원본 텍스처는 쓰지 않는다(D-49).
"""
import argparse
import hashlib
import json
import os
import sys

import numpy
from PIL import Image

VOLUME_SIZE = 64
TILES_PER_ROW = 8
CHANNEL_SPECS = (
    {"channel": "R", "lattice": 4, "seed": 8101},
    {"channel": "G", "lattice": 4, "seed": 8102},
    {"channel": "B", "lattice": 8, "seed": 8103},
    {"channel": "A", "lattice": 16, "seed": 8104},
)
ATLAS_FILE_NAME = "T_TD_FluidNoiseAtlas.png"
PREVIEW_FILE_NAME = "preview.png"
NORMALIZE_PERCENTILE = 99.9
WRAP_RATIO_LIMIT = 1.25
MEAN_RANGE = (0.46, 0.54)
MINIMUM_STANDARD_DEVIATION = 0.12
PREVIEW_TILE = 256


def default_output_directory():
    project_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    return os.path.join(project_root, "Saved", "FluidFX", "fire_textures")


def fade(value):
    return value * value * value * (value * (value * 6.0 - 15.0) + 10.0)


def make_periodic_gradient_noise(size, lattice, seed):
    generator = numpy.random.default_rng(seed)
    gradients = generator.standard_normal((lattice, lattice, lattice, 3))
    gradients /= numpy.linalg.norm(gradients, axis=-1, keepdims=True)
    coordinates = (numpy.arange(size) + 0.5) * (lattice / size)
    cell = numpy.floor(coordinates).astype(numpy.int64)
    offset = coordinates - cell
    weight = fade(offset)
    z_index = cell[:, None, None]
    y_index = cell[None, :, None]
    x_index = cell[None, None, :]
    z_offset = offset[:, None, None]
    y_offset = offset[None, :, None]
    x_offset = offset[None, None, :]
    result = numpy.zeros((size, size, size))
    for corner_z in (0, 1):
        for corner_y in (0, 1):
            for corner_x in (0, 1):
                corner = gradients[(z_index + corner_z) % lattice, (y_index + corner_y) % lattice, (x_index + corner_x) % lattice]
                dot = (
                    corner[..., 0] * (x_offset - corner_x)
                    + corner[..., 1] * (y_offset - corner_y)
                    + corner[..., 2] * (z_offset - corner_z)
                )
                blend_x = weight[None, None, :] if corner_x else 1.0 - weight[None, None, :]
                blend_y = weight[None, :, None] if corner_y else 1.0 - weight[None, :, None]
                blend_z = weight[:, None, None] if corner_z else 1.0 - weight[:, None, None]
                result += dot * blend_x * blend_y * blend_z
    return result


def normalize_to_unit(noise):
    scale = numpy.percentile(numpy.abs(noise), NORMALIZE_PERCENTILE)
    return numpy.clip(0.5 + 0.5 * noise / scale, 0.0, 1.0)


def quantize(unit_values):
    return numpy.round(1.0 + unit_values * 254.0).astype(numpy.uint8)


def build_volume():
    channels = []
    for spec in CHANNEL_SPECS:
        noise = make_periodic_gradient_noise(VOLUME_SIZE, spec["lattice"], spec["seed"])
        channels.append(quantize(normalize_to_unit(noise)))
    return numpy.stack(channels, axis=-1)


def build_atlas(volume):
    tile_rows = VOLUME_SIZE // TILES_PER_ROW
    atlas = numpy.zeros((tile_rows * VOLUME_SIZE, TILES_PER_ROW * VOLUME_SIZE, 4), dtype=numpy.uint8)
    for slice_index in range(VOLUME_SIZE):
        column = slice_index % TILES_PER_ROW
        row = slice_index // TILES_PER_ROW
        atlas[row * VOLUME_SIZE:(row + 1) * VOLUME_SIZE, column * VOLUME_SIZE:(column + 1) * VOLUME_SIZE] = volume[slice_index]
    return atlas


def unpack_atlas(atlas):
    volume = numpy.zeros((VOLUME_SIZE, VOLUME_SIZE, VOLUME_SIZE, 4), dtype=numpy.uint8)
    for slice_index in range(VOLUME_SIZE):
        column = slice_index % TILES_PER_ROW
        row = slice_index // TILES_PER_ROW
        volume[slice_index] = atlas[row * VOLUME_SIZE:(row + 1) * VOLUME_SIZE, column * VOLUME_SIZE:(column + 1) * VOLUME_SIZE]
    return volume


def measure_wrap_ratios(channel_values):
    values = channel_values.astype(numpy.float64)
    ratios = {}
    for axis, name in ((0, "z"), (1, "y"), (2, "x")):
        interior = numpy.abs(numpy.diff(values, axis=axis)).mean()
        first = numpy.take(values, 0, axis=axis)
        last = numpy.take(values, VOLUME_SIZE - 1, axis=axis)
        boundary = numpy.abs(first - last).mean()
        ratios[name] = round(float(boundary / max(interior, 1e-9)), 3)
    return ratios


def inspect_volume(volume):
    report = {}
    problems = []
    for index, spec in enumerate(CHANNEL_SPECS):
        unit = (volume[..., index].astype(numpy.float64) - 1.0) / 254.0
        ratios = measure_wrap_ratios(volume[..., index])
        mean = float(unit.mean())
        deviation = float(unit.std())
        report[spec["channel"]] = {
            "lattice_cells_per_tile": spec["lattice"],
            "texels_per_cell": VOLUME_SIZE // spec["lattice"],
            "mean": round(mean, 4),
            "standard_deviation": round(deviation, 4),
            "minimum_byte": int(volume[..., index].min()),
            "maximum_byte": int(volume[..., index].max()),
            "wrap_ratio": ratios,
        }
        if max(ratios.values()) > WRAP_RATIO_LIMIT:
            problems.append(f"{spec['channel']} 경계 차이 비율 {max(ratios.values())} > {WRAP_RATIO_LIMIT}")
        if not MEAN_RANGE[0] <= mean <= MEAN_RANGE[1]:
            problems.append(f"{spec['channel']} 평균 {mean:.3f} 범위 밖")
        if deviation < MINIMUM_STANDARD_DEVIATION:
            problems.append(f"{spec['channel']} 표준편차 {deviation:.3f} 부족")
        if int(volume[..., index].min()) < 1:
            problems.append(f"{spec['channel']} 0 값 존재(PNG 투명 채움 위험)")
    return report, problems


def sample_trilinear(volume_unit, coordinates):
    scaled = coordinates * VOLUME_SIZE - 0.5
    base = numpy.floor(scaled).astype(numpy.int64)
    fraction = scaled - base
    result = 0.0
    for corner_z in (0, 1):
        for corner_y in (0, 1):
            for corner_x in (0, 1):
                index_z = (base[..., 2] + corner_z) % VOLUME_SIZE
                index_y = (base[..., 1] + corner_y) % VOLUME_SIZE
                index_x = (base[..., 0] + corner_x) % VOLUME_SIZE
                weight = (
                    (fraction[..., 0] if corner_x else 1.0 - fraction[..., 0])
                    * (fraction[..., 1] if corner_y else 1.0 - fraction[..., 1])
                    * (fraction[..., 2] if corner_z else 1.0 - fraction[..., 2])
                )
                result = result + volume_unit[index_z, index_y, index_x] * weight[..., None]
    return result


def plane_coordinates(width, height, units_wide, units_high, depth, vertical_stretch=1.0, scale=1.0, shift=0.0):
    grid_y, grid_x = numpy.mgrid[0:height, 0:width]
    noise_x = (grid_x + 0.5) / width * units_wide * scale + shift
    noise_y = (1.0 - (grid_y + 0.5) / height) * units_high * vertical_stretch * scale + shift
    noise_z = numpy.full_like(noise_x, depth * scale + shift)
    return numpy.stack((noise_x, noise_y, noise_z), axis=-1) / 4.0


def ridge(value, sharpness):
    return numpy.power(1.0 - numpy.abs(2.0 * value - 1.0), sharpness)


def to_image(values):
    return Image.fromarray(numpy.round(numpy.clip(values, 0.0, 1.0) * 255.0).astype(numpy.uint8))


def make_channel_tiles(volume_unit):
    tiles = []
    for index in range(4):
        slice_values = volume_unit[0, :, :, index]
        tiled = numpy.tile(slice_values, (2, 2))
        tiles.append(to_image(tiled).resize((PREVIEW_TILE, PREVIEW_TILE), Image.NEAREST).convert("RGB"))
    return tiles


def make_flame_preview(volume_unit):
    coordinates = plane_coordinates(PREVIEW_TILE, PREVIEW_TILE, 4.0, 4.0, 0.37, vertical_stretch=0.5)
    first = sample_trilinear(volume_unit, coordinates)
    second = sample_trilinear(volume_unit, plane_coordinates(PREVIEW_TILE, PREVIEW_TILE, 4.0, 4.0, 0.37, 0.5, 2.9, 0.31))
    ridge_sum = 0.4 * ridge(first[..., 2], 2.0) + 0.3 * ridge(first[..., 3], 2.0) + 0.2 * ridge(second[..., 2], 2.0) + 0.1 * ridge(second[..., 3], 2.0)
    grid_y, grid_x = numpy.mgrid[0:PREVIEW_TILE, 0:PREVIEW_TILE] / (PREVIEW_TILE - 1.0)
    warp = 0.08 * (first[..., 0] - 0.5)
    width = 0.24 * (1.0 - 0.7 * (1.0 - grid_y)) + 0.02
    envelope = numpy.exp(-(((grid_x + warp) - 0.5) / width) ** 2) * numpy.clip(grid_y, 0.0, 1.0) ** 0.7
    erosion = 0.4
    density = numpy.clip((envelope - erosion * (1.0 - ridge_sum)) / (1.0 - erosion), 0.0, 1.0)
    heat = numpy.clip(density ** 1.4, 0.0, 1.0)
    color = numpy.stack((numpy.clip(heat * 2.2, 0, 1), numpy.clip(heat * 1.4 - 0.25, 0, 1) * 0.85, numpy.clip(heat * 1.6 - 0.9, 0, 1) * 0.6), axis=-1)
    return to_image(color)


def make_wood_preview(volume_unit):
    coordinates = plane_coordinates(PREVIEW_TILE, PREVIEW_TILE, 3.0, 3.0, 0.53)
    sample = sample_trilinear(volume_unit, coordinates)
    cracks = numpy.maximum(ridge(sample[..., 2], 6.0), 0.8 * ridge(sample[..., 3], 8.0))
    glow = numpy.clip((cracks - 0.55) / 0.45, 0.0, 1.0) ** 2
    char_level = 0.04 + 0.05 * sample[..., 0]
    color = numpy.stack((char_level + glow * 1.0, char_level * 0.9 + glow * 0.28, char_level * 0.85 + glow * 0.05), axis=-1)
    return to_image(color)


def make_smoke_preview(volume_unit):
    coordinates = plane_coordinates(PREVIEW_TILE, PREVIEW_TILE, 4.0, 4.0, 0.11)
    sample = sample_trilinear(volume_unit, coordinates)
    billow = 0.78 * (2.0 * numpy.abs(2.0 * sample[..., 0] - 1.0) - 0.6) + 0.22 * (2.0 * numpy.abs(2.0 * sample[..., 2] - 1.0) - 0.6)
    erosion = numpy.maximum(0.0, 2.0 * sample[..., 1] - 1.0)
    height = numpy.clip(0.55 + 0.35 * billow - 0.6 * erosion, 0.0, 1.0)
    return to_image(numpy.repeat(height[..., None], 3, axis=-1))


def make_grain_preview(volume_unit):
    coordinates = plane_coordinates(PREVIEW_TILE, PREVIEW_TILE, 4.0, 4.0, 0.29, scale=6.0)
    sample = sample_trilinear(volume_unit, coordinates)
    grain = 0.5 + 0.6 * (sample[..., 2] - 0.5) + 0.4 * (sample[..., 3] - 0.5)
    return to_image(numpy.repeat(grain[..., None], 3, axis=-1))


def build_preview(volume):
    volume_unit = (volume.astype(numpy.float64) - 1.0) / 254.0
    tiles = make_channel_tiles(volume_unit) + [
        make_flame_preview(volume_unit),
        make_wood_preview(volume_unit),
        make_smoke_preview(volume_unit),
        make_grain_preview(volume_unit),
    ]
    sheet = Image.new("RGB", (PREVIEW_TILE * 4, PREVIEW_TILE * 2))
    for index, tile in enumerate(tiles):
        sheet.paste(tile, ((index % 4) * PREVIEW_TILE, (index // 4) * PREVIEW_TILE))
    return sheet


def file_sha256(path):
    with open(path, "rb") as source:
        return hashlib.sha256(source.read()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", default=default_output_directory())
    arguments = parser.parse_args()
    os.makedirs(arguments.output_dir, exist_ok=True)

    volume = build_volume()
    atlas = build_atlas(volume)
    atlas_path = os.path.join(arguments.output_dir, ATLAS_FILE_NAME)
    Image.fromarray(atlas, "RGBA").save(atlas_path, optimize=True)

    reloaded = numpy.asarray(Image.open(atlas_path).convert("RGBA"))
    round_trip_exact = bool(numpy.array_equal(unpack_atlas(reloaded), volume))
    channel_report, problems = inspect_volume(volume)
    if not round_trip_exact:
        problems.append("PNG 다시 읽기 결과가 볼륨과 다름")

    preview_path = os.path.join(arguments.output_dir, PREVIEW_FILE_NAME)
    build_preview(volume).save(preview_path)

    summary = {
        "status": "ok" if not problems else "failed",
        "atlas": atlas_path,
        "atlas_size": [int(atlas.shape[1]), int(atlas.shape[0])],
        "volume_size": VOLUME_SIZE,
        "tile_size": VOLUME_SIZE,
        "tiles_per_row": TILES_PER_ROW,
        "atlas_sha256": file_sha256(atlas_path),
        "round_trip_exact": round_trip_exact,
        "channels": channel_report,
        "preview": preview_path,
        "problems": problems,
    }
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
