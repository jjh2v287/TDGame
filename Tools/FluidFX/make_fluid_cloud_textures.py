"""실행 환경: 시스템 Python 3(numpy·Pillow), 에디터 불필요
실행 명령: python Tools/FluidFX/make_fluid_cloud_textures.py [--root 프로젝트경로] [--out 출력폴더] [--seed 정수]
출력: Saved/FluidFX/cloud_textures/ 아래 T_TD_FluidCloudSection_{Mushroom,VolcanoPlume,Tornado}(256², 8비트 RGB), T_TD_FluidCloudSection_SandWall(512×256), T_TD_FluidCloudCoverage_Cyclone(512², 로그 나선·눈 반경 0.06), T_TD_FluidCloudStageHeight(512² 16비트 회색, 범위 0~2.5 km 정규화), T_TD_FluidCloudBlack·White(4²), cloud_textures.json(파일 sha256·규약 검사), 표준 출력 같은 JSON
상태: 미검증(2026-10-26 FX-11r 작성, 에디터 가져오기 전). 단면 규약은 FX-9 와 같다: x = U(0 = 축), 이미지 맨 위 행 = V 0 = 바닥(refute M5·cross-plan P-04). 높이는 행 = 세계 Y, 열 = 세계 X, 발자국 (-20,-20)~(+20,+20) km, 화산 (0,0)·평지 사분면 (+8,+8)·사구 동쪽(+X). 시드가 같으면 바이트가 같다.
"""
import argparse
import hashlib
import json
import pathlib

import numpy as np
from PIL import Image

SECTION_SIZE = 256
SAND_WALL_SIZE = (512, 256)
COVERAGE_SIZE = 512
HEIGHT_SIZE = 512
FLAT_SIZE = 4
DEFAULT_SEED = 20261026
TERRAIN_ORIGIN_KM = (-20.0, -20.0)
TERRAIN_SIZE_KM = 40.0
TERRAIN_RANGE_KM = (0.0, 2.5)
VOLCANO_HEIGHT_KM = 2.5
VOLCANO_RADIUS_KM = 6.0
CRATER_RADIUS_KM = 0.6
CRATER_DEPTH_KM = 0.15
HILL_HEIGHT_KM = 0.3
FLAT_QUADRANT_START_KM = 4.0
FLAT_QUADRANT_HEIGHT_KM = 0.05
DUNE_START_KM = 12.0
DUNE_HEIGHT_KM = 0.08
DUNE_WAVELENGTH_KM = 1.6
CYCLONE_EYE_RADIUS = 0.06
CYCLONE_ARM_COUNT = 3
CYCLONE_SPIRAL_TIGHTNESS = 3.2


def smoothstep(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def section_grid(width, height):
    u = (np.arange(width, dtype=np.float64) + 0.5) / width
    v = (np.arange(height, dtype=np.float64) + 0.5) / height
    return np.meshgrid(u, v, indexing="xy")


def smooth_noise(rng, shape, cells):
    coarse = rng.random((cells + 1, cells + 1))
    rows = np.linspace(0.0, cells, shape[0])
    cols = np.linspace(0.0, cells, shape[1])
    row_index = np.minimum(rows.astype(int), cells - 1)
    col_index = np.minimum(cols.astype(int), cells - 1)
    row_t = smoothstep(0.0, 1.0, rows - row_index)[:, None]
    col_t = smoothstep(0.0, 1.0, cols - col_index)[None, :]
    top = coarse[row_index][:, col_index] * (1.0 - col_t) + coarse[row_index][:, col_index + 1] * col_t
    bottom = coarse[row_index + 1][:, col_index] * (1.0 - col_t) + coarse[row_index + 1][:, col_index + 1] * col_t
    return top * (1.0 - row_t) + bottom * row_t


def make_mushroom_section(rng):
    u, v = section_grid(SECTION_SIZE, SECTION_SIZE)
    stem = (1.0 - smoothstep(0.08, 0.16, u - 0.04 * v)) * (1.0 - smoothstep(0.62, 0.7, v))
    cap_center_u, cap_center_v = 0.45, 0.76
    ring_distance = np.sqrt(((u - cap_center_u) / 0.42) ** 2 + ((v - cap_center_v) / 0.16) ** 2)
    cap = 1.0 - smoothstep(0.75, 1.0, ring_distance)
    cap_core = 1.0 - smoothstep(0.0, 0.18, u) * (1.0 - smoothstep(0.55, 0.75, v)) * (1.0 - smoothstep(0.92, 1.0, v))
    skirt = (1.0 - smoothstep(0.0, 0.04, np.abs(v - 0.34))) * (1.0 - smoothstep(0.35, 0.5, u)) * smoothstep(0.1, 0.18, u) * 0.55
    texture = np.maximum(np.maximum(stem, cap * np.clip(cap_core + 0.6, 0.0, 1.0)), skirt)
    texture *= 0.85 + 0.15 * smooth_noise(rng, texture.shape, 12)
    return np.clip(texture, 0.0, 1.0)


def make_volcano_plume_section(rng):
    u, v = section_grid(SECTION_SIZE, SECTION_SIZE)
    radius = 0.12 + 0.55 * np.power(v, 1.3) + 0.25 * smoothstep(0.78, 0.95, v)
    column = 1.0 - smoothstep(radius * 0.8, radius, u)
    top_fade = 1.0 - smoothstep(0.93, 1.0, v)
    billows = 0.8 + 0.2 * smooth_noise(rng, column.shape, 16)
    return np.clip(column * top_fade * billows, 0.0, 1.0)


def make_tornado_section(rng):
    u, v = section_grid(SECTION_SIZE, SECTION_SIZE)
    radius = 0.04 + 0.5 * np.power(v, 2.2)
    funnel = 1.0 - smoothstep(radius * 0.75, radius, u)
    hollow = 0.7 + 0.3 * smoothstep(0.0, radius * 0.5 + 1e-3, u)
    texture = funnel * hollow * (0.85 + 0.15 * smooth_noise(rng, funnel.shape, 10))
    return np.clip(texture, 0.0, 1.0)


def make_sand_wall_section(rng):
    width, height = SAND_WALL_SIZE
    u, v = section_grid(width, height)
    front = 0.62 + 0.3 * smooth_noise(rng, (1, width), 9)[0][None, :]
    wall = 1.0 - smoothstep(front - 0.12, front, v)
    lumps = 0.75 + 0.25 * smooth_noise(rng, wall.shape, 24)
    ends = smoothstep(0.0, 0.05, u) * (1.0 - smoothstep(0.95, 1.0, u))
    return np.clip(wall * lumps * ends, 0.0, 1.0)


def make_cyclone_coverage(rng):
    u, v = section_grid(COVERAGE_SIZE, COVERAGE_SIZE)
    x = u - 0.5
    y = v - 0.5
    radius = np.sqrt(x * x + y * y)
    angle = np.arctan2(y, x)
    spiral_phase = CYCLONE_ARM_COUNT * (angle - CYCLONE_SPIRAL_TIGHTNESS * np.log(np.maximum(radius, 1e-4) / CYCLONE_EYE_RADIUS))
    arms = 0.5 + 0.5 * np.cos(spiral_phase)
    eye = smoothstep(CYCLONE_EYE_RADIUS, CYCLONE_EYE_RADIUS * 1.8, radius)
    wall = 1.0 - smoothstep(CYCLONE_EYE_RADIUS * 1.6, CYCLONE_EYE_RADIUS * 3.0, radius)
    outer = 1.0 - smoothstep(0.38, 0.5, radius)
    coverage = np.maximum(arms * outer, wall) * eye
    coverage *= 0.85 + 0.15 * smooth_noise(rng, coverage.shape, 20)
    return np.clip(coverage, 0.0, 1.0)


def terrain_world_grid():
    cells = (np.arange(HEIGHT_SIZE, dtype=np.float64) + 0.5) / HEIGHT_SIZE * TERRAIN_SIZE_KM
    world_x = TERRAIN_ORIGIN_KM[0] + cells
    world_y = TERRAIN_ORIGIN_KM[1] + cells
    return np.meshgrid(world_x, world_y, indexing="xy")


def make_stage_height_km(rng):
    world_x, world_y = terrain_world_grid()
    radius = np.sqrt(world_x * world_x + world_y * world_y)
    cone = VOLCANO_HEIGHT_KM * np.power(np.clip(1.0 - radius / VOLCANO_RADIUS_KM, 0.0, 1.0), 1.2)
    crater = CRATER_DEPTH_KM * (1.0 - smoothstep(0.0, CRATER_RADIUS_KM, radius))
    hills = HILL_HEIGHT_KM * smooth_noise(rng, world_x.shape, 14) * smoothstep(VOLCANO_RADIUS_KM * 0.8, VOLCANO_RADIUS_KM * 1.3, radius)
    height = np.maximum(cone - crater, hills)
    flat_blend = smoothstep(FLAT_QUADRANT_START_KM, FLAT_QUADRANT_START_KM + 1.5, world_x) * smoothstep(FLAT_QUADRANT_START_KM, FLAT_QUADRANT_START_KM + 1.5, world_y)
    height = height * (1.0 - flat_blend) + FLAT_QUADRANT_HEIGHT_KM * flat_blend
    dune_blend = smoothstep(DUNE_START_KM, DUNE_START_KM + 2.0, world_x) * (1.0 - flat_blend)
    dunes = FLAT_QUADRANT_HEIGHT_KM + DUNE_HEIGHT_KM * (0.5 + 0.5 * np.sin(2.0 * np.pi * (world_x + 0.3 * world_y) / DUNE_WAVELENGTH_KM))
    height = height * (1.0 - dune_blend) + dunes * dune_blend
    return np.clip(height, TERRAIN_RANGE_KM[0], TERRAIN_RANGE_KM[1])


def save_rgb8(path, values):
    channel = np.round(np.clip(values, 0.0, 1.0) * 255.0).astype(np.uint8)
    Image.fromarray(np.stack([channel, channel, channel], axis=-1), mode="RGB").save(path)


def save_gray16(path, values):
    Image.fromarray(np.round(np.clip(values, 0.0, 1.0) * 65535.0).astype(np.uint16)).save(path)


def file_sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def kilometer_to_pixel(x_km, y_km):
    column = int((x_km - TERRAIN_ORIGIN_KM[0]) / TERRAIN_SIZE_KM * HEIGHT_SIZE)
    row = int((y_km - TERRAIN_ORIGIN_KM[1]) / TERRAIN_SIZE_KM * HEIGHT_SIZE)
    return row, column


def check_layout(sections, height_km, cyclone):
    checks = {}
    mushroom = sections["T_TD_FluidCloudSection_Mushroom"]
    checks["section_v0_is_top_row_bottom"] = bool(mushroom[:8, :16].mean() > mushroom[:8, -64:].mean())
    checks["section_axis_is_left_column"] = bool(sections["T_TD_FluidCloudSection_VolcanoPlume"][:, 0].mean() > sections["T_TD_FluidCloudSection_VolcanoPlume"][:, -1].mean())
    tornado = sections["T_TD_FluidCloudSection_Tornado"]
    checks["tornado_widens_upward"] = bool((tornado[-16:] > 0.3).sum() > (tornado[:16] > 0.3).sum())
    volcano_row, volcano_column = kilometer_to_pixel(0.0, 2.0)
    flat_row, flat_column = kilometer_to_pixel(8.0, 8.0)
    checks["volcano_peak_km"] = float(height_km.max())
    checks["volcano_near_center"] = bool(height_km[volcano_row, volcano_column] > 1.0)
    checks["flat_quadrant_km"] = float(height_km[flat_row, flat_column])
    checks["flat_quadrant_ok"] = bool(abs(height_km[flat_row, flat_column] - FLAT_QUADRANT_HEIGHT_KM) < 1e-3)
    center = COVERAGE_SIZE // 2
    checks["cyclone_eye_clear"] = bool(cyclone[center - 2:center + 2, center - 2:center + 2].max() < 0.05)
    checks["cyclone_has_arms"] = bool(cyclone.max() > 0.8)
    checks["ok"] = all(value for key, value in checks.items() if isinstance(value, bool)) and abs(checks["volcano_peak_km"] - VOLCANO_HEIGHT_KM) < 0.2
    return checks


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parents[2]))
    parser.add_argument("--out", default="")
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    arguments = parser.parse_args()
    root = pathlib.Path(arguments.root)
    output_folder = pathlib.Path(arguments.out) if arguments.out else root / "Saved" / "FluidFX" / "cloud_textures"
    if not output_folder.is_absolute():
        output_folder = root / output_folder
    output_folder.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(arguments.seed)

    sections = {
        "T_TD_FluidCloudSection_Mushroom": make_mushroom_section(rng),
        "T_TD_FluidCloudSection_VolcanoPlume": make_volcano_plume_section(rng),
        "T_TD_FluidCloudSection_Tornado": make_tornado_section(rng),
        "T_TD_FluidCloudSection_SandWall": make_sand_wall_section(rng),
    }
    cyclone = make_cyclone_coverage(rng)
    height_km = make_stage_height_km(rng)
    files = {}
    for name, values in sections.items():
        path = output_folder / f"{name}.png"
        save_rgb8(path, values)
        files[name] = {"file": path.name, "sha256": file_sha256(path), "size": [int(values.shape[1]), int(values.shape[0])], "kind": "section", "bits": 8}
    cyclone_path = output_folder / "T_TD_FluidCloudCoverage_Cyclone.png"
    save_rgb8(cyclone_path, cyclone)
    files["T_TD_FluidCloudCoverage_Cyclone"] = {"file": cyclone_path.name, "sha256": file_sha256(cyclone_path), "size": [COVERAGE_SIZE, COVERAGE_SIZE], "kind": "coverage", "bits": 8}
    height_path = output_folder / "T_TD_FluidCloudStageHeight.png"
    save_gray16(height_path, (height_km - TERRAIN_RANGE_KM[0]) / (TERRAIN_RANGE_KM[1] - TERRAIN_RANGE_KM[0]))
    files["T_TD_FluidCloudStageHeight"] = {"file": height_path.name, "sha256": file_sha256(height_path), "size": [HEIGHT_SIZE, HEIGHT_SIZE], "kind": "height16", "bits": 16}
    for name, value in (("T_TD_FluidCloudBlack", 0.0), ("T_TD_FluidCloudWhite", 1.0)):
        path = output_folder / f"{name}.png"
        save_rgb8(path, np.full((FLAT_SIZE, FLAT_SIZE), value))
        files[name] = {"file": path.name, "sha256": file_sha256(path), "size": [FLAT_SIZE, FLAT_SIZE], "kind": "flat", "bits": 8}

    checks = check_layout(sections, height_km, cyclone)
    report = {
        "seed": arguments.seed,
        "section_convention": "x = U (0 = axis), top image row = V 0 = cloud bottom (FX-9 CS(u, v))",
        "terrain": {"origin_km": list(TERRAIN_ORIGIN_KM), "size_km": TERRAIN_SIZE_KM, "height_range_km": list(TERRAIN_RANGE_KM), "rows": "world Y", "columns": "world X"},
        "files": files,
        "checks": checks,
        "ok": checks["ok"],
    }
    (output_folder / "cloud_textures.json").write_text(json.dumps(report, ensure_ascii=False, indent=1) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps(report, ensure_ascii=False))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
