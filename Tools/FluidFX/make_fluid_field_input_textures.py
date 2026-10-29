"""실행 환경: 시스템 Python 3(numpy·Pillow), 에디터 불필요
실행 명령: python Tools/FluidFX/make_fluid_field_input_textures.py [--root 프로젝트경로] [--seed 정수]
출력: <root>/Saved/FluidFX/field_input_textures/ 아래 T_TD_FluidFieldWindSwirl.png(256² RGBA8, R=X축 성분·G=수직축 성분 부호 인코딩, 소용돌이) · T_TD_FluidFieldDensityRings.png(256² RGBA8, R=동심 고리 밀도) · T_TD_FluidFieldMaskMaze.png(256² RGBA8, R=미로 벽 마스크, 0.5 초과가 고체) · field_input_textures.json(파일 sha256·방향 규약·검사), 표준 출력에 같은 JSON.
상태: 미검증(2026-10-29 FX-15b 에셋 도구 작성, 에디터 가져오기 전). 방향 규약은 행 0 = v 0 = 수직축(⊥X) 음의 쪽, 열 0 = u 0 = X 음의 쪽이다. 시드와 환경이 같으면 바이트가 같다(PNG 메타데이터 없음).
"""
import argparse
import hashlib
import json
import math
import pathlib
from collections import deque

import numpy as np
from PIL import Image

SIZE = 256
DEFAULT_SEED = 20261029
SWIRL_PEAK_RADIUS = 0.5
SWIRL_NOISE_AMOUNT = 0.15
SWIRL_NOISE_WAVES = 3
RING_COUNT = 5
RING_FADE_START = 0.85
RING_CONTRAST_RADIUS = 0.8
MAZE_CELLS = 8
MAZE_WALL_PIXELS = 8
MAZE_BLUR_SIGMA = 1.5
SIGNED_ZERO_BYTE = 128
EDGE_LEAK_LIMIT = 0.05
PNG_COMPRESS_LEVEL = 9
OUTPUT_RELATIVE_PATH = ("Saved", "FluidFX", "field_input_textures")
REPORT_FILE_NAME = "field_input_textures.json"
SWIRL_NAME = "T_TD_FluidFieldWindSwirl"
RINGS_NAME = "T_TD_FluidFieldDensityRings"
MAZE_NAME = "T_TD_FluidFieldMaskMaze"
STEP_OFFSETS = ((0, 1), (1, 0), (0, -1), (-1, 0))


def pixel_axes():
    centers = (np.arange(SIZE) + 0.5) / SIZE * 2.0 - 1.0
    perpendicular, along = np.meshgrid(centers, centers, indexing="ij")
    return along, perpendicular


def smoothstep(low, high, values):
    t = np.clip((values - low) / (high - low), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def encode_unit(values):
    return np.clip(np.floor(values * 255.0 + 0.5), 0, 255).astype(np.uint8)


def encode_signed(values):
    return encode_unit(values * 0.5 + 0.5)


def decode_signed(bytes_):
    return bytes_.astype(np.float64) / 255.0 * 2.0 - 1.0


def pack_rgba(red, green=None, blue=None):
    rgba = np.zeros((SIZE, SIZE, 4), dtype=np.uint8)
    rgba[..., 0] = red
    if green is not None:
        rgba[..., 1] = green
    if blue is not None:
        rgba[..., 2] = blue
    rgba[..., 3] = 255
    return rgba


def make_wind_swirl(seed):
    along, perpendicular = pixel_axes()
    radius = np.hypot(along, perpendicular)
    safe_radius = np.maximum(radius, 1e-6)
    profile = np.where(radius < 1.0, np.clip(radius * (1.0 - radius) / (SWIRL_PEAK_RADIUS * (1.0 - SWIRL_PEAK_RADIUS)), 0.0, 1.0), 0.0)
    velocity_x = -perpendicular / safe_radius * profile
    velocity_y = along / safe_radius * profile
    rng = np.random.default_rng([seed, 1])
    noise_x = np.zeros_like(radius)
    noise_y = np.zeros_like(radius)
    for wave_index in range(SWIRL_NOISE_WAVES):
        amplitude = 1.0 / (wave_index + 1)
        for target in (noise_x, noise_y):
            angle = rng.uniform(0.0, 2.0 * math.pi)
            frequency = rng.uniform(1.0, 3.0)
            phase = rng.uniform(0.0, 2.0 * math.pi)
            target += amplitude * np.sin(math.pi * frequency * (np.cos(angle) * along + np.sin(angle) * perpendicular) + phase)
    normalizer = sum(1.0 / (index + 1) for index in range(SWIRL_NOISE_WAVES))
    velocity_x = velocity_x + SWIRL_NOISE_AMOUNT * profile * noise_x / normalizer
    velocity_y = velocity_y + SWIRL_NOISE_AMOUNT * profile * noise_y / normalizer
    magnitude = np.hypot(velocity_x, velocity_y)
    scale = np.where(magnitude > 1.0, 1.0 / np.maximum(magnitude, 1e-9), 1.0)
    return pack_rgba(encode_signed(velocity_x * scale), encode_signed(velocity_y * scale))


def make_density_rings():
    along, perpendicular = pixel_axes()
    radius = np.hypot(along, perpendicular)
    rings = 0.5 + 0.5 * np.cos(2.0 * math.pi * RING_COUNT * radius)
    fade = 1.0 - smoothstep(RING_FADE_START, 1.0, radius)
    return pack_rgba(encode_unit(rings * fade))


def carve_maze(rng):
    visited = np.zeros((MAZE_CELLS, MAZE_CELLS), dtype=bool)
    opened_right = np.zeros((MAZE_CELLS, MAZE_CELLS), dtype=bool)
    opened_down = np.zeros((MAZE_CELLS, MAZE_CELLS), dtype=bool)
    stack = [(0, 0)]
    visited[0, 0] = True
    while stack:
        row, column = stack[-1]
        moved = False
        for direction in rng.permutation(4):
            row_step, column_step = STEP_OFFSETS[int(direction)]
            next_row, next_column = row + row_step, column + column_step
            if not (0 <= next_row < MAZE_CELLS and 0 <= next_column < MAZE_CELLS) or visited[next_row, next_column]:
                continue
            if column_step == 1:
                opened_right[row, column] = True
            elif column_step == -1:
                opened_right[next_row, next_column] = True
            elif row_step == 1:
                opened_down[row, column] = True
            else:
                opened_down[next_row, next_column] = True
            visited[next_row, next_column] = True
            stack.append((next_row, next_column))
            moved = True
            break
        if not moved:
            stack.pop()
    return opened_right, opened_down


def rasterize_maze(opened_right, opened_down):
    cell = SIZE // MAZE_CELLS
    half = MAZE_WALL_PIXELS // 2
    walls = np.zeros((SIZE, SIZE), dtype=bool)

    def fill(row_low, row_high, column_low, column_high):
        walls[max(row_low, 0):min(row_high, SIZE), max(column_low, 0):min(column_high, SIZE)] = True

    for line_row in range(MAZE_CELLS + 1):
        for line_column in range(MAZE_CELLS + 1):
            fill(line_row * cell - half, line_row * cell + half, line_column * cell - half, line_column * cell + half)
    for cell_row in range(MAZE_CELLS):
        for cell_column in range(MAZE_CELLS):
            row_low, column_low = cell_row * cell, cell_column * cell
            is_entrance_side = cell_row == 0 and cell_column == 0
            is_exit_side = cell_row == MAZE_CELLS - 1 and cell_column == MAZE_CELLS - 1
            if cell_column < MAZE_CELLS - 1 and not opened_right[cell_row, cell_column]:
                fill(row_low, row_low + cell, column_low + cell - half, column_low + cell + half)
            if cell_row < MAZE_CELLS - 1 and not opened_down[cell_row, cell_column]:
                fill(row_low + cell - half, row_low + cell + half, column_low, column_low + cell)
            if cell_row == 0 and not is_entrance_side:
                fill(-half, half, column_low, column_low + cell)
            if cell_row == MAZE_CELLS - 1 and not is_exit_side:
                fill(SIZE - half, SIZE + half, column_low, column_low + cell)
            if cell_column == 0:
                fill(row_low, row_low + cell, -half, half)
            if cell_column == MAZE_CELLS - 1:
                fill(row_low, row_low + cell, SIZE - half, SIZE + half)
    return walls


def gaussian_blur(image, sigma):
    radius = int(math.ceil(sigma * 3.0))
    offsets = np.arange(-radius, radius + 1)
    weights = np.exp(-0.5 * (offsets / sigma) ** 2)
    weights /= weights.sum()
    result = image.astype(np.float64)
    for axis in (0, 1):
        pad_widths = [(0, 0), (0, 0)]
        pad_widths[axis] = (radius, radius)
        padded = np.pad(result, pad_widths, mode="edge")
        accumulated = np.zeros_like(result)
        for offset_index, weight in enumerate(weights):
            slicer = [slice(None), slice(None)]
            slicer[axis] = slice(offset_index, offset_index + SIZE)
            accumulated += weight * padded[tuple(slicer)]
        result = accumulated
    return result


def maze_is_connected(walls):
    cell = SIZE // MAZE_CELLS
    start = (0, cell // 2)
    goal = (SIZE - 1, SIZE - cell // 2 - 1)
    if walls[start] or walls[goal]:
        return False
    seen = np.zeros_like(walls)
    seen[start] = True
    queue = deque([start])
    while queue:
        row, column = queue.popleft()
        if (row, column) == goal:
            return True
        for row_step, column_step in STEP_OFFSETS:
            next_row, next_column = row + row_step, column + column_step
            if 0 <= next_row < SIZE and 0 <= next_column < SIZE and not walls[next_row, next_column] and not seen[next_row, next_column]:
                seen[next_row, next_column] = True
                queue.append((next_row, next_column))
    return False


def make_mask_maze(seed):
    rng = np.random.default_rng([seed, 2])
    opened_right, opened_down = carve_maze(rng)
    walls = rasterize_maze(opened_right, opened_down)
    mask = np.clip(gaussian_blur(walls.astype(np.float64), MAZE_BLUR_SIGMA), 0.0, 1.0)
    passage_count = int(opened_right.sum() + opened_down.sum())
    return pack_rgba(encode_unit(mask)), walls, passage_count


def file_sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save_png(rgba, path):
    Image.fromarray(rgba).save(path, format="PNG", compress_level=PNG_COMPRESS_LEVEL)


def measure_swirl(rgba):
    along, perpendicular = pixel_axes()
    velocity_x = decode_signed(rgba[..., 0])
    velocity_y = decode_signed(rgba[..., 1])
    circulation = float(np.sum(along * velocity_y - perpendicular * velocity_x))
    quarter = int((0.5 + 1.0) * 0.5 * SIZE)
    center = SIZE // 2
    right_sample = [float(velocity_x[center, quarter]), float(velocity_y[center, quarter])]
    bottom_sample = [float(velocity_x[quarter, center]), float(velocity_y[quarter, center])]
    edge_peak = float(max(np.abs(velocity_x[0]).max(), np.abs(velocity_x[-1]).max(), np.abs(velocity_y[:, 0]).max(), np.abs(velocity_y[:, -1]).max()))
    return {
        "circulation": circulation,
        "counter_clockwise_in_x_perpendicular_frame": circulation > 0.0,
        "sample_plus_x_axis_expect_positive_y": right_sample,
        "sample_plus_perpendicular_axis_expect_negative_x": bottom_sample,
        "orientation_ok": right_sample[1] > 0.3 and bottom_sample[0] < -0.3,
        "peak_speed": float(np.hypot(velocity_x, velocity_y).max()),
        "edge_leak_peak": edge_peak,
        "zero_byte": SIGNED_ZERO_BYTE,
        "zero_bias_decoded": float(SIGNED_ZERO_BYTE) / 255.0 * 2.0 - 1.0,
    }


def measure_rings(rgba):
    along, perpendicular = pixel_axes()
    inside = np.hypot(along, perpendicular) < RING_CONTRAST_RADIUS
    values = rgba[..., 0].astype(np.float64)[inside] / 255.0
    contrast = float((values.max() - values.min()) / max(values.max() + values.min(), 1e-9))
    return {"michelson_contrast_inside_radius": contrast, "contrast_radius": RING_CONTRAST_RADIUS, "ring_count": RING_COUNT, "center_value": float(rgba[SIZE // 2, SIZE // 2, 0]) / 255.0, "corner_value": float(rgba[0, 0, 0]) / 255.0}


def measure_maze(rgba, walls, passage_count):
    mask = rgba[..., 0].astype(np.float64) / 255.0
    cell = SIZE // MAZE_CELLS
    return {
        "cells": [MAZE_CELLS, MAZE_CELLS],
        "passages_expect_cells_minus_one": passage_count,
        "perfect_maze": passage_count == MAZE_CELLS * MAZE_CELLS - 1,
        "connected_entrance_to_exit": maze_is_connected(walls),
        "entrance": {"side": "row 0 (-perpendicular)", "column_center_pixel": cell // 2},
        "exit": {"side": "last row (+perpendicular)", "column_center_pixel": SIZE - cell // 2 - 1},
        "wall_fraction_binary": float(walls.mean()),
        "solid_fraction_above_half": float((mask > 0.5).mean()),
        "wall_pixels": MAZE_WALL_PIXELS,
        "blur_sigma_pixels": MAZE_BLUR_SIGMA,
    }


def build_orientation_contract():
    return {
        "rule": "행 0 = v 0 = 수직축(⊥X) 음의 쪽, 열 0 = u 0 = X 음의 쪽, 필드 로컬 (a,b) = (X축 좌표, ⊥X축 좌표)/반폭, uv = (a,b)/2 + 0.5",
        "pixel_to_local": "a = (열 + 0.5) / 256 * 2 - 1, b = (행 + 0.5) / 256 * 2 - 1",
        "signed_velocity": "q = 2t - 1 (바이트 / 255), 목표 속도 = q.x * X + q.y * ⊥X, R = q.x, G = q.y, 영 바이트 128(편향 +0.0039)",
        "swirl_direction": "(X, ⊥X) 틀에서 반시계: q = (-b, a) / r * 프로파일",
        "png_row_zero_is_top": True,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parents[2]))
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    arguments = parser.parse_args()
    output_folder = pathlib.Path(arguments.root).joinpath(*OUTPUT_RELATIVE_PATH)
    output_folder.mkdir(parents=True, exist_ok=True)

    swirl = make_wind_swirl(arguments.seed)
    rings = make_density_rings()
    maze, walls, passage_count = make_mask_maze(arguments.seed)
    textures = (
        (SWIRL_NAME, swirl, "VELOCITY", True, measure_swirl(swirl)),
        (RINGS_NAME, rings, "DENSITY", False, measure_rings(rings)),
        (MAZE_NAME, maze, "COLLISION_MASK", False, measure_maze(maze, walls, passage_count)),
    )
    entries = {}
    for name, rgba, layout, uses_signed_encoding, measures in textures:
        path = output_folder / (name + ".png")
        save_png(rgba, path)
        entries[name] = {"file": path.name, "sha256": file_sha256(path), "size": [SIZE, SIZE], "layout": layout, "uses_signed_velocity_encoding": uses_signed_encoding, "checks": measures}

    swirl_checks = entries[SWIRL_NAME]["checks"]
    ring_checks = entries[RINGS_NAME]["checks"]
    maze_checks = entries[MAZE_NAME]["checks"]
    checks_ok = (
        swirl_checks["orientation_ok"]
        and swirl_checks["counter_clockwise_in_x_perpendicular_frame"]
        and swirl_checks["edge_leak_peak"] < EDGE_LEAK_LIMIT
        and ring_checks["michelson_contrast_inside_radius"] > 0.9
        and maze_checks["perfect_maze"]
        and maze_checks["connected_entrance_to_exit"]
    )
    report = {"seed": arguments.seed, "size": SIZE, "orientation": build_orientation_contract(), "textures": entries, "ok": bool(checks_ok)}
    (output_folder / REPORT_FILE_NAME).write_text(json.dumps(report, ensure_ascii=False, indent=1) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps(report, ensure_ascii=False))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
