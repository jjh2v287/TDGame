"""
실행 환경: Windows Python 3.12 + numpy 2.x + matplotlib 3.10 (언리얼 에디터·빌드 불필요, 순수 계산과 소형 numpy 생성 시간 측정)
실행 명령: python Tools/InfiniteWorld/proto/budget_model.py --out Saved/AgentOps/20261023/infinite-world/proto
출력: <out>/budget.json, <out>/tables.md, <out>/fig_camera_footprint.png, <out>/fig_chunk_rate.png, <out>/fig_resident_memory.png, <out>/fig_save_size.png
상태: 실험 재현용. 무한 샌드박스 세계 가능성 검토용 스트리밍·메모리·세이브 예산 모델, 2026-10-23 claude. 게임 코드 아님.
"""
import argparse
import json
import math
import time
from pathlib import Path

import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker
from matplotlib.patches import Polygon, Circle

ARM_CM = 1100.0
ARENA_ARM_CM = 1540.0
CONSTRUCTOR_ARM_CM = 800.0
PITCH_DEG = -50.0
CONSTRUCTOR_PITCH_DEG = -60.0
YAW_DEG = 45.0
FOV_DEG = 90.0
CAMERA_COMPONENT_ASPECT = 1.777778
FOCUS_HEIGHT_CM = 90.0
WALK_MS = 6.0
NAV_TILE_M = 10.0
NAV_INVOKER_GENERATION_M = 30.0
NAV_INVOKER_REMOVAL_M = 50.0
QUAD_M = 1.0
WORLD_PARTITION_CELL_M = 252.0
WORLD_PARTITION_LOADING_M = 768.0
UE_LARGE_HALF_WORLD_MAX_CM = 8796093022208.0 * 0.5

SPRINT_MS = 9.0
VEHICLE_MS = 15.0
FAST_TRAVEL_MS = 50.0
SPEEDS_MS = [WALK_MS, SPRINT_MS, VEHICLE_MS, FAST_TRAVEL_MS]
SPEED_LABELS = {WALK_MS: "걷기 6", SPRINT_MS: "질주 9(가정)", VEHICLE_MS: "탈것 15(가정)", FAST_TRAVEL_MS: "빠른 이동 50(가정)"}
CHUNK_SIZES_M = [32.0, 64.0, 128.0]
TALL_OBJECT_HEIGHT_M = 20.0
TALL_OBJECT_CROWN_RADIUS_M = 4.0
SUN_ELEVATIONS_DEG = [20.0, 25.0, 35.0, 50.0]
DESIGN_SUN_ELEVATION_DEG = 25.0
MONSTER_ACTIVATION_M = [50.0, 100.0]
PREFETCH_LEAD_S = 1.5
UNLOAD_HYSTERESIS_M = 16.0

LARGE_INSTANCE_DENSITY_PER_M2 = 0.01
SMALL_INSTANCE_DENSITY_PER_M2 = 0.10
INSTANCE_CPU_BYTES = 64
INSTANCE_GPU_BYTES = 96
LARGE_INSTANCE_COLLISION_BYTES = 1024
HEIGHTFIELD_COLLISION_BYTES_PER_SAMPLE = 4
NAV_TILE_BYTES = 32 * 1024
CHUNK_FIXED_OVERHEAD_BYTES = 64 * 1024
MESH_TERRAIN_BYTES_PER_VERTEX = 32
MESH_TERRAIN_INDEX_BYTES_PER_QUAD = 24

RECORD_BYTES = {"place": 24, "remove": 6, "state": 8, "terrain_voxel": 96}
CHUNK_SAVE_HEADER_BYTES = 40
VOXEL_SIZE_M = 0.5
VOXEL_RUNS_PER_COLUMN = 4
VOXEL_RUN_BYTES = 3
SAVE_HOURS = 100
MOVING_TIME_FRACTION = 0.5
NEW_TERRITORY_FRACTION = 0.25
SCATTERED_EDITS_PER_TOUCHED_CHUNK = 5
EXPLORED_CHUNK_STATE_FRACTION = 0.10
EXPLORED_CHUNK_STATE_BYTES = 32
BASE_SIDE_M = 256.0
SAVE_SCENARIOS = {
    "가벼움-B": {"edits_per_hour": 500, "base_fraction": 0.5, "mix": {"place": 0.4, "remove": 0.4, "state": 0.2}},
    "건축가-B": {"edits_per_hour": 5000, "base_fraction": 0.7, "mix": {"place": 0.7, "remove": 0.2, "state": 0.1}},
    "가벼움-C": {"edits_per_hour": 500, "base_fraction": 0.5, "mix": {"place": 0.3, "remove": 0.3, "state": 0.1, "terrain_voxel": 0.3}},
    "건축가-C": {"edits_per_hour": 5000, "base_fraction": 0.7, "mix": {"place": 0.45, "remove": 0.1, "state": 0.05, "terrain_voxel": 0.4}},
}

SERIES = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100"]
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK_SECONDARY = "#52514e"
INK_MUTED = "#898781"
GRIDLINE = "#e1e0d9"
BASELINE = "#c3c2b7"


def configure_plot_style():
    plt.rcParams.update({
        "font.family": "Malgun Gothic",
        "axes.unicode_minus": False,
        "figure.facecolor": SURFACE,
        "axes.facecolor": SURFACE,
        "axes.edgecolor": BASELINE,
        "axes.labelcolor": INK_SECONDARY,
        "xtick.color": INK_MUTED,
        "ytick.color": INK_MUTED,
        "text.color": INK,
        "axes.grid": True,
        "grid.color": GRIDLINE,
        "grid.linewidth": 0.6,
        "axes.spines.top": False,
        "axes.spines.right": False,
        "legend.frameon": False,
        "lines.linewidth": 2.0,
    })


def compute_camera_footprint(arm_cm, pitch_deg, screen_aspect):
    tan_half_vertical = math.tan(math.radians(FOV_DEG / 2.0)) / CAMERA_COMPONENT_ASPECT
    tan_half_horizontal = tan_half_vertical * screen_aspect
    pitch = math.radians(pitch_deg)
    forward = np.array([math.cos(pitch), 0.0, math.sin(pitch)])
    right = np.array([0.0, 1.0, 0.0])
    up = np.array([-math.sin(pitch), 0.0, math.cos(pitch)])
    camera = np.array([0.0, 0.0, FOCUS_HEIGHT_CM]) - arm_cm * forward
    corners = []
    for sign_x, sign_y in [(-1, -1), (1, -1), (1, 1), (-1, 1)]:
        direction = forward + right * tan_half_horizontal * sign_x + up * tan_half_vertical * sign_y
        if direction[2] >= 0.0:
            raise ValueError("view ray does not reach the ground")
        hit = camera + (-camera[2] / direction[2]) * direction
        corners.append(hit[:2] / 100.0)
    corners = np.array(corners)
    camera_ground = camera[:2] / 100.0
    shoelace = 0.5 * abs(np.dot(corners[:, 0], np.roll(corners[:, 1], 1)) - np.dot(corners[:, 1], np.roll(corners[:, 0], 1)))
    view_radius = float(np.max(np.linalg.norm(corners, axis=1)))
    tall_radius = max(view_radius, float(np.linalg.norm(camera_ground))) + TALL_OBJECT_CROWN_RADIUS_M
    return {
        "arm_cm": arm_cm,
        "pitch_deg": pitch_deg,
        "screen_aspect": screen_aspect,
        "horizontal_fov_deg": math.degrees(2 * math.atan(tan_half_horizontal)),
        "vertical_fov_deg": math.degrees(2 * math.atan(tan_half_vertical)),
        "camera_height_m": camera[2] / 100.0,
        "camera_back_m": -camera_ground[0],
        "near_edge_behind_pawn_m": -corners[0, 0],
        "far_edge_ahead_m": corners[2, 0],
        "near_width_m": corners[1, 1] - corners[0, 1],
        "far_width_m": corners[2, 1] - corners[3, 1],
        "area_m2": shoelace,
        "view_radius_m": view_radius,
        "tall_object_radius_m": tall_radius,
        "corners_m": corners.tolist(),
        "camera_ground_m": camera_ground.tolist(),
    }


def compute_shadow_length_m(height_m, sun_elevation_deg):
    return height_m / math.tan(math.radians(sun_elevation_deg))


def chunk_distances(point_x, point_y, chunk_x0, chunk_y0, chunk_m):
    dx = np.maximum(np.maximum(chunk_x0 - point_x, 0.0), point_x - (chunk_x0 + chunk_m))
    dy = np.maximum(np.maximum(chunk_y0 - point_y, 0.0), point_y - (chunk_y0 + chunk_m))
    return np.hypot(dx, dy)


def count_resident_chunks(radius_m, chunk_m, samples=8):
    reach = int(math.ceil(radius_m / chunk_m)) + 2
    index = np.arange(-reach, reach + 1)
    grid_i, grid_j = np.meshgrid(index, index, indexing="ij")
    chunk_x0 = grid_i * chunk_m
    chunk_y0 = grid_j * chunk_m
    counts = []
    offsets = (np.arange(samples) + 0.5) / samples * chunk_m
    for point_x in offsets:
        for point_y in offsets:
            counts.append(int((chunk_distances(point_x, point_y, chunk_x0, chunk_y0, chunk_m) <= radius_m).sum()))
    return max(counts), float(np.mean(counts))


def count_new_chunks_per_meter(radius_m, chunk_m, heading_deg, step_m=0.5):
    path_length = max(1024.0, 16.0 * chunk_m)
    heading = math.radians(heading_deg)
    start = np.array([0.37 * chunk_m, 0.61 * chunk_m])
    travel = np.arange(0.0, path_length + step_m, step_m)
    points = start + travel[:, None] * np.array([math.cos(heading), math.sin(heading)])
    low = np.floor((points.min(axis=0) - radius_m) / chunk_m).astype(int) - 1
    high = np.floor((points.max(axis=0) + radius_m) / chunk_m).astype(int) + 1
    grid_i, grid_j = np.meshgrid(np.arange(low[0], high[0] + 1), np.arange(low[1], high[1] + 1), indexing="ij")
    chunk_x0 = (grid_i * chunk_m).ravel()
    chunk_y0 = (grid_j * chunk_m).ravel()
    is_touched = np.zeros(chunk_x0.shape, dtype=bool)
    for batch_start in range(0, len(points), 256):
        batch = points[batch_start:batch_start + 256]
        distances = chunk_distances(batch[:, 0][None, :], batch[:, 1][None, :], chunk_x0[:, None], chunk_y0[:, None], chunk_m)
        is_touched |= distances.min(axis=1) <= radius_m
    is_start = chunk_distances(start[0], start[1], chunk_x0, chunk_y0, chunk_m) <= radius_m
    return (int(is_touched.sum()) - int(is_start.sum())) / path_length


def compute_loading_radius_m(needed_radius_m, speed_ms):
    return needed_radius_m + speed_ms * PREFETCH_LEAD_S + UNLOAD_HYSTERESIS_M


def compute_chunk_memory_bytes(chunk_m):
    vertices = (chunk_m / QUAD_M + 1.0) ** 2
    quads = (chunk_m / QUAD_M) ** 2
    large_instances = LARGE_INSTANCE_DENSITY_PER_M2 * chunk_m * chunk_m
    small_instances = SMALL_INSTANCE_DENSITY_PER_M2 * chunk_m * chunk_m
    nav_tiles_area = (chunk_m / NAV_TILE_M) ** 2
    memory = {
        "height_cpu_16bit": 2.0 * vertices,
        "height_gpu_rgba8_with_mips": 4.0 * vertices * 4.0 / 3.0,
        "weight_cpu_4layers_8bit": 4.0 * vertices,
        "weight_gpu_rgba8_with_mips": 4.0 * vertices * 4.0 / 3.0,
        "heightfield_collision": HEIGHTFIELD_COLLISION_BYTES_PER_SAMPLE * vertices,
        "instance_cpu": (large_instances + small_instances) * INSTANCE_CPU_BYTES,
        "instance_gpu": (large_instances + small_instances) * INSTANCE_GPU_BYTES,
        "large_instance_collision": large_instances * LARGE_INSTANCE_COLLISION_BYTES,
        "nav_tiles": nav_tiles_area * NAV_TILE_BYTES,
        "fixed_overhead": CHUNK_FIXED_OVERHEAD_BYTES,
    }
    render_keys = list(memory.keys())
    simulation_keys = ["height_cpu_16bit", "heightfield_collision", "large_instance_collision", "nav_tiles", "fixed_overhead"]
    mesh_terrain_extra = MESH_TERRAIN_BYTES_PER_VERTEX * vertices + MESH_TERRAIN_INDEX_BYTES_PER_QUAD * quads - memory["height_gpu_rgba8_with_mips"]
    return {
        "components": memory,
        "vertices": vertices,
        "large_instances": large_instances,
        "small_instances": small_instances,
        "nav_tiles_area_equivalent": nav_tiles_area,
        "nav_tiles_touched_unaligned_worst": (math.ceil(chunk_m / NAV_TILE_M) + 1) ** 2,
        "nav_tiles_touched_800cm_aligned": (chunk_m / 8.0) ** 2,
        "full_bytes": sum(memory[key] for key in render_keys),
        "simulation_bytes": sum(memory[key] for key in simulation_keys),
        "mesh_terrain_full_bytes": sum(memory[key] for key in render_keys) + mesh_terrain_extra,
        "voxel_dense_64m_range_bytes": (chunk_m / VOXEL_SIZE_M) ** 2 * (64.0 / VOXEL_SIZE_M) * 2.0,
        "voxel_surface_band_8m_bytes": (chunk_m / VOXEL_SIZE_M) ** 2 * (8.0 / VOXEL_SIZE_M) * 2.0,
    }


def mix_hash_to_unit(lattice_x, lattice_y, seed):
    hashed = np.ascontiguousarray(lattice_x, dtype=np.int64).view(np.uint64) * np.uint64(0x9E3779B97F4A7C15)
    hashed ^= np.ascontiguousarray(lattice_y, dtype=np.int64).view(np.uint64) * np.uint64(0xC2B2AE3D27D4EB4F)
    hashed ^= np.uint64(seed & 0xFFFFFFFFFFFFFFFF)
    hashed ^= hashed >> np.uint64(33)
    hashed *= np.uint64(0xFF51AFD7ED558CCD)
    hashed ^= hashed >> np.uint64(33)
    hashed *= np.uint64(0xC4CEB9FE1A85EC53)
    hashed ^= hashed >> np.uint64(33)
    return (hashed >> np.uint64(11)).astype(np.float64) / float(1 << 53)


def generate_chunk_heights(chunk_x, chunk_y, quads_per_side, seed, octaves=6, base_wavelength_vertices=512):
    local = np.arange(quads_per_side + 1, dtype=np.int64)
    vertex_x = np.int64(chunk_x) * quads_per_side + local[:, None] + np.zeros((1, quads_per_side + 1), dtype=np.int64)
    vertex_y = np.int64(chunk_y) * quads_per_side + local[None, :] + np.zeros((quads_per_side + 1, 1), dtype=np.int64)
    heights = np.zeros(vertex_x.shape, dtype=np.float64)
    amplitude = 1.0
    wavelength = base_wavelength_vertices
    for octave in range(octaves):
        cell_x = np.floor_divide(vertex_x, wavelength)
        cell_y = np.floor_divide(vertex_y, wavelength)
        fraction_x = (vertex_x - cell_x * wavelength) / wavelength
        fraction_y = (vertex_y - cell_y * wavelength) / wavelength
        fade_x = fraction_x * fraction_x * (3.0 - 2.0 * fraction_x)
        fade_y = fraction_y * fraction_y * (3.0 - 2.0 * fraction_y)
        octave_seed = seed * 1000003 + octave
        corner00 = mix_hash_to_unit(cell_x, cell_y, octave_seed)
        corner10 = mix_hash_to_unit(cell_x + 1, cell_y, octave_seed)
        corner01 = mix_hash_to_unit(cell_x, cell_y + 1, octave_seed)
        corner11 = mix_hash_to_unit(cell_x + 1, cell_y + 1, octave_seed)
        bottom = corner00 + (corner10 - corner00) * fade_x
        top = corner01 + (corner11 - corner01) * fade_x
        heights += amplitude * (bottom + (top - bottom) * fade_y)
        amplitude *= 0.5
        wavelength = max(wavelength // 2, 1)
    return heights * 60.0


def scatter_chunk_instances(chunk_x, chunk_y, chunk_m, seed, cell_m=10.0):
    cells_per_side = int(math.ceil(chunk_m / cell_m)) + 1
    cell_index = np.arange(cells_per_side, dtype=np.int64)
    cell_x = math.floor(chunk_x * chunk_m / cell_m) + cell_index[:, None] + np.zeros((1, cells_per_side), dtype=np.int64)
    cell_y = math.floor(chunk_y * chunk_m / cell_m) + cell_index[None, :] + np.zeros((cells_per_side, 1), dtype=np.int64)
    world_x = cell_x * cell_m + mix_hash_to_unit(cell_x, cell_y, seed ^ 0x51) * cell_m
    world_y = cell_y * cell_m + mix_hash_to_unit(cell_x, cell_y, seed ^ 0x73) * cell_m
    is_inside = (world_x >= chunk_x * chunk_m) & (world_x < (chunk_x + 1) * chunk_m) & (world_y >= chunk_y * chunk_m) & (world_y < (chunk_y + 1) * chunk_m)
    return int(is_inside.sum())


def measure_generation_prototype():
    results = {"seam_exact": {}, "time_ms_per_chunk": {}}
    seed = 20261023
    for offset_m in [0.0, 1.0e4, 1.0e6, 1.0e8]:
        quads = 64
        chunk_x = int(offset_m // (quads * QUAD_M))
        west = generate_chunk_heights(chunk_x, -3, quads, seed)
        east = generate_chunk_heights(chunk_x + 1, -3, quads, seed)
        results["seam_exact"][f"{offset_m:.0e}"] = bool(np.array_equal(west[-1, :], east[0, :]))
    for chunk_m in CHUNK_SIZES_M:
        quads = int(chunk_m / QUAD_M)
        repeat = 40
        start = time.perf_counter()
        for index in range(repeat):
            generate_chunk_heights(1000 + index, -77, quads, seed)
            scatter_chunk_instances(1000 + index, -77, chunk_m, seed)
        results["time_ms_per_chunk"][f"{int(chunk_m)}"] = (time.perf_counter() - start) * 1000.0 / repeat
    scatter_total = sum(scatter_chunk_instances(x, y, 32.0, seed) for x in range(4) for y in range(4))
    results["scatter_density_check_per_m2"] = scatter_total / (128.0 * 128.0)
    precision = {}
    for distance_m in [1.0e3, 1.0e4, 1.0e5, 1.0e6, 1.0e7, 4.398e10]:
        precision[f"{distance_m:.0e}"] = {
            "float32_spacing_cm": float(np.spacing(np.float32(distance_m * 100.0))),
            "float64_spacing_cm": float(np.spacing(np.float64(distance_m * 100.0))),
        }
    results["coordinate_precision"] = precision
    return results


def compute_save_model(chunk_m, loading_radius_m):
    swath_m = 2.0 * loading_radius_m
    base_chunks = math.ceil(BASE_SIDE_M / chunk_m) ** 2
    voxel_chunk_cap = (chunk_m / VOXEL_SIZE_M) ** 2 * VOXEL_RUNS_PER_COLUMN * VOXEL_RUN_BYTES
    output = {}
    hours = np.arange(0, SAVE_HOURS + 1)
    for name, scenario in SAVE_SCENARIOS.items():
        series = []
        detail = None
        for hour in hours:
            edits = scenario["edits_per_hour"] * hour
            new_path_m = hour * 3600.0 * MOVING_TIME_FRACTION * WALK_MS * NEW_TERRITORY_FRACTION
            explored_chunks = new_path_m * swath_m / (chunk_m * chunk_m)
            base_edits = edits * scenario["base_fraction"]
            scattered_edits = edits - base_edits
            scattered_chunks = min(explored_chunks, scattered_edits / SCATTERED_EDITS_PER_TOUCHED_CHUNK)
            touched_chunks = (base_chunks if edits > 0 else 0) + scattered_chunks
            record_bytes = 0.0
            for kind, share in scenario["mix"].items():
                if kind == "terrain_voxel":
                    base_voxel = min(base_edits * share * RECORD_BYTES[kind], base_chunks * voxel_chunk_cap)
                    scattered_voxel = scattered_edits * share * RECORD_BYTES[kind]
                    record_bytes += base_voxel + scattered_voxel
                    continue
                record_bytes += edits * share * RECORD_BYTES[kind]
            explored_bitmap = explored_chunks / 8.0
            explored_state = explored_chunks * EXPLORED_CHUNK_STATE_FRACTION * EXPLORED_CHUNK_STATE_BYTES
            total = touched_chunks * CHUNK_SAVE_HEADER_BYTES + record_bytes + explored_bitmap + explored_state
            series.append(total)
            detail = {
                "edits": edits,
                "explored_chunks": explored_chunks,
                "explored_area_km2": new_path_m * swath_m / 1.0e6,
                "touched_chunks": touched_chunks,
                "record_bytes": record_bytes,
                "explored_bitmap_bytes": explored_bitmap,
                "explored_state_bytes": explored_state,
                "total_bytes": total,
                "placed_objects_in_base": base_edits * scenario["mix"].get("place", 0.0),
                "placed_objects_per_base_chunk": base_edits * scenario["mix"].get("place", 0.0) / base_chunks,
            }
        output[name] = {"series_bytes": series, "at_end": detail}
    return {"hours": hours.tolist(), "scenarios": output, "base_chunks": base_chunks, "voxel_chunk_cap_bytes": voxel_chunk_cap}


def plot_camera_footprint(footprints, radii, path):
    figure, (axis_view, axis_wide) = plt.subplots(1, 2, figsize=(14, 7))
    cases = [
        ("팔 1100·16:9", footprints["1100_16:9"], SERIES[0], "-"),
        ("팔 1100·21:9", footprints["1100_21:9"], SERIES[1], "-"),
        ("팔 1540(아레나)·16:9", footprints["1540_16:9"], SERIES[2], "-"),
        ("팔 800·피치 -60(생성자 기본)·16:9", footprints["800_16:9"], SERIES[3], "-"),
    ]
    rotation = math.radians(YAW_DEG)
    for axis, extent, grid_m in [(axis_view, 32.0, 32.0), (axis_wide, 210.0, 64.0)]:
        lines = np.arange(-8 * extent, 8 * extent + grid_m, grid_m)
        for offset in lines:
            for direction in (1, -1):
                world = np.array([[offset, -4 * extent], [offset, 4 * extent]]) if direction == 1 else np.array([[-4 * extent, offset], [4 * extent, offset]])
                cos_r, sin_r = math.cos(-rotation), math.sin(-rotation)
                camera_frame = world @ np.array([[cos_r, sin_r], [-sin_r, cos_r]])
                axis.plot(camera_frame[:, 1], camera_frame[:, 0], color=GRIDLINE, linewidth=0.7, zorder=0)
        axis.set_xlim(-extent, extent)
        axis.set_ylim(-extent, extent)
        axis.set_aspect("equal")
        axis.grid(False)
        axis.set_xlabel("카메라 오른쪽 (m)")
        axis.set_ylabel("카메라 정면 지면 방향 (m)")
        axis.plot([0], [0], marker="o", markersize=8, color=INK, zorder=5)
    for label, footprint, color, style in cases:
        corners = np.array(footprint["corners_m"])
        axis_view.add_patch(Polygon(corners[:, ::-1], closed=True, fill=False, edgecolor=color, linewidth=2.0, linestyle=style, label=label))
        camera_ground = footprint["camera_ground_m"]
        axis_view.plot([camera_ground[1]], [camera_ground[0]], marker="s", markersize=8, color=color)
    axis_view.set_title("(가) 지면에 보이는 사다리꼴 (회색 격자 = 요 45도로 기운 32m 청크)", fontsize=11, color=INK)
    axis_view.legend(loc="lower left", fontsize=9)
    axis_view.annotate("폰", (0, 0), (1.2, -2.5), color=INK_SECONDARY, fontsize=9)
    base = np.array(footprints["1100_16:9"]["corners_m"])
    axis_wide.add_patch(Polygon(base[:, ::-1], closed=True, facecolor=INK_MUTED, edgecolor=INK_SECONDARY, alpha=0.5, label="화면(팔 1100·16:9)"))
    for (label, radius), color in zip(radii, SERIES):
        axis_wide.add_patch(Circle((0, 0), radius, fill=False, edgecolor=color, linewidth=2.0, label=f"{label} → {radius:.0f}m"))
    axis_wide.set_title("(나) 화면 대비 상주 반경 (회색 격자 = 64m 청크)", fontsize=11, color=INK)
    axis_wide.legend(loc="lower left", fontsize=9)
    axis_wide.text(-200, 190, f"현재 월드 파티션 로딩 범위 {WORLD_PARTITION_LOADING_M:.0f}m(추정)는 이 그림 밖", fontsize=9, color=INK_SECONDARY)
    figure.tight_layout()
    figure.savefig(path, dpi=130)
    plt.close(figure)


def plot_chunk_rate(rates, memory, path):
    figure, (axis_rate, axis_throughput) = plt.subplots(1, 2, figsize=(13, 5.5))
    for chunk_m, color in zip(CHUNK_SIZES_M, SERIES):
        values = [rates[f"{int(chunk_m)}"]["100"][f"{speed}"]["max_per_s"] for speed in SPEEDS_MS]
        axis_rate.plot(SPEEDS_MS, values, color=color, marker="o", markersize=8, label=f"청크 {int(chunk_m)}m")
        axis_rate.annotate(f"{values[-1]:.1f}", (SPEEDS_MS[-1], values[-1]), (6, 0), textcoords="offset points", color=INK_SECONDARY, fontsize=9, va="center")
        throughput = [value * memory[f"{int(chunk_m)}"]["full_bytes"] / 1.0e6 for value in values]
        axis_throughput.plot(SPEEDS_MS, throughput, color=color, marker="o", markersize=8, label=f"청크 {int(chunk_m)}m")
        axis_throughput.annotate(f"{throughput[-1]:.0f}", (SPEEDS_MS[-1], throughput[-1]), (6, 0), textcoords="offset points", color=INK_SECONDARY, fontsize=9, va="center")
    for axis in (axis_rate, axis_throughput):
        axis.set_xscale("log")
        axis.set_yscale("log")
        axis.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda value, _: f"{value:g}"))
        axis.yaxis.set_minor_formatter(matplotlib.ticker.NullFormatter())
        axis.xaxis.set_minor_formatter(matplotlib.ticker.NullFormatter())
        axis.set_xticks(SPEEDS_MS)
        axis.set_xticklabels([SPEED_LABELS[speed] for speed in SPEEDS_MS], fontsize=8)
        axis.set_xlabel("이동 속도 (m/s)")
        axis.legend(loc="upper left", fontsize=9)
    axis_rate.set_title("(가) 초당 새로 불러올 청크 수 (필요 반경 100m, 축·대각 중 큰 값)", fontsize=11)
    axis_rate.set_ylabel("청크/초")
    axis_throughput.set_title("(나) 생성 데이터 처리량 (청크당 전체 상주 메모리 기준)", fontsize=11)
    axis_throughput.set_ylabel("MB/초")
    figure.tight_layout()
    figure.savefig(path, dpi=130)
    plt.close(figure)


def plot_resident_memory(resident, scenario_labels, path):
    figure, axis = plt.subplots(figsize=(11, 5.5))
    width = 0.2
    positions = np.arange(len(CHUNK_SIZES_M))
    for index, (label, color) in enumerate(zip(scenario_labels, SERIES)):
        values = [resident[f"{int(chunk_m)}"][label]["full_mb"] for chunk_m in CHUNK_SIZES_M]
        bars = axis.bar(positions + (index - 1.5) * width, values, width * 0.92, color=color, label=label, edgecolor=SURFACE, linewidth=1)
        for bar, value in zip(bars, values):
            axis.annotate(f"{value:.0f}", (bar.get_x() + bar.get_width() / 2, value), (0, 3), textcoords="offset points", ha="center", fontsize=8, color=INK_SECONDARY)
    axis.set_xticks(positions)
    axis.set_xticklabels([f"청크 {int(chunk_m)}m" for chunk_m in CHUNK_SIZES_M])
    axis.set_ylabel("최악 위치 상주 메모리 (MB)")
    axis.set_title("상주 반경별 지형·인스턴스·충돌·내비 메모리 (걷기, 선행 1.5초 + 이력 16m 포함)", fontsize=11)
    axis.legend(loc="upper left", fontsize=9)
    figure.tight_layout()
    figure.savefig(path, dpi=130)
    plt.close(figure)


def plot_save_size(save_model, path):
    figure, axis = plt.subplots(figsize=(11, 5.5))
    hours = save_model["hours"]
    for (name, data), color in zip(save_model["scenarios"].items(), SERIES):
        values = np.array(data["series_bytes"]) / 1.0e6
        axis.plot(hours[1:], values[1:], color=color, label=name)
        axis.annotate(f"{name} {values[-1]:.1f} MB", (hours[-1], values[-1]), (6, 0), textcoords="offset points", fontsize=9, color=INK_SECONDARY, va="center")
    axis.set_yscale("log")
    axis.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda value, _: f"{value:g}"))
    axis.yaxis.set_minor_formatter(matplotlib.ticker.NullFormatter())
    axis.set_xlim(0, SAVE_HOURS * 1.22)
    axis.set_xlabel("플레이 시간 (시간)")
    axis.set_ylabel("세이브 크기 (MB, 압축 전)")
    axis.set_title("청크 변경분 세이브 크기 (청크 64m, 변경 없는 청크 0바이트)", fontsize=11)
    axis.legend(loc="upper left", fontsize=9)
    figure.tight_layout()
    figure.savefig(path, dpi=130)
    plt.close(figure)


def format_markdown_table(header, rows):
    lines = ["| " + " | ".join(header) + " |", "|" + "---|" * len(header)]
    lines += ["| " + " | ".join(str(cell) for cell in row) + " |" for row in rows]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default="Saved/AgentOps/20261023/infinite-world/proto")
    output_dir = Path(parser.parse_args().out)
    output_dir.mkdir(parents=True, exist_ok=True)
    configure_plot_style()

    footprints = {
        "1100_16:9": compute_camera_footprint(ARM_CM, PITCH_DEG, 16 / 9),
        "1100_21:9": compute_camera_footprint(ARM_CM, PITCH_DEG, 21 / 9),
        "1540_16:9": compute_camera_footprint(ARENA_ARM_CM, PITCH_DEG, 16 / 9),
        "1540_21:9": compute_camera_footprint(ARENA_ARM_CM, PITCH_DEG, 21 / 9),
        "800_16:9": compute_camera_footprint(CONSTRUCTOR_ARM_CM, CONSTRUCTOR_PITCH_DEG, 16 / 9),
    }
    worst_view = max(footprint["view_radius_m"] for key, footprint in footprints.items() if key.startswith("1540") or key.startswith("1100"))
    worst_tall = max(footprint["tall_object_radius_m"] for key, footprint in footprints.items() if key.startswith("1540") or key.startswith("1100"))
    base_view = footprints["1100_21:9"]["view_radius_m"]
    shadow_lengths = {f"{elevation:.0f}": compute_shadow_length_m(TALL_OBJECT_HEIGHT_M, elevation) for elevation in SUN_ELEVATIONS_DEG}
    shadow_radius = base_view + compute_shadow_length_m(TALL_OBJECT_HEIGHT_M, DESIGN_SUN_ELEVATION_DEG)
    needed_radii = [
        ("화면+키 큰 물체", footprints["1100_21:9"]["tall_object_radius_m"]),
        ("화면+그림자(20m·태양 25도)", shadow_radius),
        ("몬스터 활성 50m", MONSTER_ACTIVATION_M[0]),
        ("몬스터 활성 100m", MONSTER_ACTIVATION_M[1]),
    ]

    memory = {f"{int(chunk_m)}": compute_chunk_memory_bytes(chunk_m) for chunk_m in CHUNK_SIZES_M}

    resident = {}
    for chunk_m in CHUNK_SIZES_M:
        per_chunk = memory[f"{int(chunk_m)}"]
        resident[f"{int(chunk_m)}"] = {}
        for label, radius in needed_radii:
            loading = compute_loading_radius_m(radius, WALK_MS)
            worst, mean = count_resident_chunks(loading, chunk_m)
            resident[f"{int(chunk_m)}"][label] = {
                "needed_radius_m": radius,
                "loading_radius_m": loading,
                "chunks_worst": worst,
                "chunks_mean": mean,
                "full_mb": worst * per_chunk["full_bytes"] / 1.0e6,
                "mesh_terrain_full_mb": worst * per_chunk["mesh_terrain_full_bytes"] / 1.0e6,
                "voxel_band_extra_mb": worst * per_chunk["voxel_surface_band_8m_bytes"] / 1.0e6,
                "voxel_dense_extra_mb": worst * per_chunk["voxel_dense_64m_range_bytes"] / 1.0e6,
            }
        render_loading = compute_loading_radius_m(shadow_radius, WALK_MS)
        simulation_loading = compute_loading_radius_m(MONSTER_ACTIVATION_M[1], WALK_MS)
        render_chunks, _ = count_resident_chunks(render_loading, chunk_m)
        simulation_chunks, _ = count_resident_chunks(simulation_loading, chunk_m)
        resident[f"{int(chunk_m)}"]["tiered"] = {
            "render_chunks": render_chunks,
            "simulation_only_chunks": simulation_chunks - render_chunks,
            "total_mb": (render_chunks * per_chunk["full_bytes"] + (simulation_chunks - render_chunks) * per_chunk["simulation_bytes"]) / 1.0e6,
        }
        current_chunks, _ = count_resident_chunks(WORLD_PARTITION_LOADING_M, chunk_m)
        resident[f"{int(chunk_m)}"]["current_world_partition_768m"] = {
            "chunks_worst": current_chunks,
            "full_mb": current_chunks * per_chunk["full_bytes"] / 1.0e6,
        }

    rates = {}
    for chunk_m in CHUNK_SIZES_M:
        rates[f"{int(chunk_m)}"] = {}
        for label, radius in [("65", shadow_radius), ("100", MONSTER_ACTIVATION_M[1])]:
            rates[f"{int(chunk_m)}"][label] = {}
            for speed in SPEEDS_MS:
                loading = compute_loading_radius_m(radius, speed)
                per_meter_axis = count_new_chunks_per_meter(loading, chunk_m, 0.0)
                per_meter_diagonal = count_new_chunks_per_meter(loading, chunk_m, 45.0)
                per_second = max(per_meter_axis, per_meter_diagonal) * speed
                rates[f"{int(chunk_m)}"][label][f"{speed}"] = {
                    "loading_radius_m": loading,
                    "axis_per_s": per_meter_axis * speed,
                    "diagonal_per_s": per_meter_diagonal * speed,
                    "max_per_s": per_second,
                    "analytic_per_s": speed * (2.0 * loading + chunk_m * math.sqrt(2.0)) / (chunk_m * chunk_m),
                    "single_worker_budget_ms_per_chunk": 1000.0 / per_second if per_second > 0 else None,
                    "vertices_per_s": per_second * memory[f"{int(chunk_m)}"]["vertices"],
                    "instances_per_s": per_second * (memory[f"{int(chunk_m)}"]["large_instances"] + memory[f"{int(chunk_m)}"]["small_instances"]),
                    "nav_tiles_per_s_area": speed * 2.0 * loading / (NAV_TILE_M * NAV_TILE_M),
                    "data_mb_per_s": per_second * memory[f"{int(chunk_m)}"]["full_bytes"] / 1.0e6,
                }

    nav_invoker = {}
    for radius in [NAV_INVOKER_GENERATION_M, MONSTER_ACTIVATION_M[1]]:
        nav_invoker[f"{radius:.0f}"] = {
            "resident_tiles": math.pi * (radius + NAV_TILE_M) ** 2 / (NAV_TILE_M * NAV_TILE_M),
            "tiles_per_s": {f"{speed}": speed * 2.0 * radius / (NAV_TILE_M * NAV_TILE_M) for speed in SPEEDS_MS},
        }

    prototype = measure_generation_prototype()
    save_model = compute_save_model(64.0, compute_loading_radius_m(MONSTER_ACTIVATION_M[1], WALK_MS))
    save_by_chunk = {f"{int(chunk_m)}": {name: data["at_end"] for name, data in compute_save_model(chunk_m, compute_loading_radius_m(MONSTER_ACTIVATION_M[1], WALK_MS))["scenarios"].items()} for chunk_m in CHUNK_SIZES_M}
    world_limit = {
        "half_world_km": UE_LARGE_HALF_WORLD_MAX_CM / 1.0e5,
        "hours_to_edge_walk": UE_LARGE_HALF_WORLD_MAX_CM / 100.0 / WALK_MS / 3600.0,
        "hours_to_edge_fast_travel": UE_LARGE_HALF_WORLD_MAX_CM / 100.0 / FAST_TRAVEL_MS / 3600.0,
        "int32_chunk_index_limit_km_at_32m": (2 ** 31) * 32.0 / 1000.0,
    }

    plot_camera_footprint(footprints, needed_radii, output_dir / "fig_camera_footprint.png")
    plot_chunk_rate(rates, memory, output_dir / "fig_chunk_rate.png")
    plot_resident_memory(resident, [label for label, _ in needed_radii], output_dir / "fig_resident_memory.png")
    plot_save_size(save_model, output_dir / "fig_save_size.png")

    budget = {
        "footprints": footprints,
        "worst_view_radius_m": worst_view,
        "worst_tall_radius_m": worst_tall,
        "shadow_lengths_m": shadow_lengths,
        "needed_radii": needed_radii,
        "chunk_memory": memory,
        "resident": resident,
        "rates": rates,
        "nav_invoker": nav_invoker,
        "prototype": prototype,
        "save_64m": {name: data["at_end"] for name, data in save_model["scenarios"].items()},
        "save_by_chunk": save_by_chunk,
        "save_base_chunks_64m": save_model["base_chunks"],
        "voxel_chunk_cap_bytes_64m": save_model["voxel_chunk_cap_bytes"],
        "world_limit": world_limit,
    }
    (output_dir / "budget.json").write_text(json.dumps(budget, ensure_ascii=False, indent=1, default=lambda value: value.item() if hasattr(value, "item") else str(value)), encoding="utf-8")

    sections = []
    sections.append("## 카메라 지면 투영\n" + format_markdown_table(
        ["경우", "수평 시야각", "수직 시야각", "카메라 높이 m", "뒤 가장자리 m", "앞 가장자리 m", "앞쪽 폭 m", "뒤쪽 폭 m", "면적 m²", "폰→꼭짓점 최대 m", "키 큰 물체 포함 m"],
        [[key, f"{f['horizontal_fov_deg']:.1f}", f"{f['vertical_fov_deg']:.1f}", f"{f['camera_height_m']:.2f}", f"{f['near_edge_behind_pawn_m']:.1f}", f"{f['far_edge_ahead_m']:.1f}", f"{f['far_width_m']:.1f}", f"{f['near_width_m']:.1f}", f"{f['area_m2']:.0f}", f"{f['view_radius_m']:.1f}", f"{f['tall_object_radius_m']:.1f}"] for key, f in footprints.items()]))
    sections.append("## 20m 물체 그림자 길이\n" + format_markdown_table(["태양 고도(도)", "그림자 m"], [[key, f"{value:.1f}"] for key, value in shadow_lengths.items()]))
    sections.append("## 청크당 메모리(바이트)\n" + format_markdown_table(
        ["청크", "정점", "큰 인스턴스", "작은 인스턴스", "전체(렌더 포함) KB", "시뮬 전용 KB", "메시 지형 전체 KB", "내비 타일(면적)", "내비 타일(10m 비정렬 최악)", "내비 타일(8m 정렬)"],
        [[f"{key}m", f"{m['vertices']:.0f}", f"{m['large_instances']:.0f}", f"{m['small_instances']:.0f}", f"{m['full_bytes'] / 1024:.0f}", f"{m['simulation_bytes'] / 1024:.0f}", f"{m['mesh_terrain_full_bytes'] / 1024:.0f}", f"{m['nav_tiles_area_equivalent']:.1f}", m["nav_tiles_touched_unaligned_worst"], f"{m['nav_tiles_touched_800cm_aligned']:.0f}"] for key, m in memory.items()]))
    sections.append("### 청크당 구성 요소 KB\n" + format_markdown_table(
        ["구성"] + [f"{key}m" for key in memory], [[component] + [f"{memory[key]['components'][component] / 1024:.1f}" for key in memory] for component in memory["32"]["components"]]))
    resident_rows = []
    for chunk_key, data in resident.items():
        for label, _ in needed_radii:
            entry = data[label]
            resident_rows.append([f"{chunk_key}m", label, f"{entry['needed_radius_m']:.0f}", f"{entry['loading_radius_m']:.0f}", entry["chunks_worst"], f"{entry['chunks_mean']:.0f}", f"{entry['full_mb']:.0f}", f"{entry['mesh_terrain_full_mb']:.0f}", f"{entry['voxel_band_extra_mb']:.0f}", f"{entry['voxel_dense_extra_mb']:.0f}"])
        resident_rows.append([f"{chunk_key}m", "계층형(렌더 65m급 + 시뮬 100m)", "-", "-", f"{data['tiered']['render_chunks']}+{data['tiered']['simulation_only_chunks']}", "-", f"{data['tiered']['total_mb']:.0f}", "-", "-", "-"])
        resident_rows.append([f"{chunk_key}m", "현재 월드 파티션 로딩 768m(추정)", "768", "768", data["current_world_partition_768m"]["chunks_worst"], "-", f"{data['current_world_partition_768m']['full_mb']:.0f}", "-", "-", "-"])
    sections.append("## 상주 청크·메모리(걷기)\n" + format_markdown_table(["청크", "반경 기준", "필요 m", "로딩 m", "최악 청크 수", "평균", "전체 MB", "메시 지형 MB", "C 복셀 표면띠 8m 추가 MB", "C 복셀 조밀 64m 추가 MB"], resident_rows))
    rate_rows = []
    for chunk_key, by_radius in rates.items():
        for radius_key, by_speed in by_radius.items():
            for speed_key, entry in by_speed.items():
                rate_rows.append([f"{chunk_key}m", radius_key, speed_key, f"{entry['loading_radius_m']:.0f}", f"{entry['axis_per_s']:.2f}", f"{entry['diagonal_per_s']:.2f}", f"{entry['analytic_per_s']:.2f}", f"{entry['single_worker_budget_ms_per_chunk']:.0f}", f"{entry['vertices_per_s'] / 1000:.0f}", f"{entry['instances_per_s']:.0f}", f"{entry['nav_tiles_per_s_area']:.1f}", f"{entry['data_mb_per_s']:.1f}"])
    sections.append("## 초당 새 청크와 생성 처리량\n" + format_markdown_table(["청크", "필요 반경", "속도 m/s", "로딩 m", "축 이동 청크/s", "대각 청크/s", "해석식", "작업자 1개 청크당 ms", "정점 천/s", "인스턴스/s", "내비 타일/s", "MB/s"], rate_rows))
    sections.append("## 내비 인보커(타일 10m)\n" + format_markdown_table(["반경 m", "상주 타일", "6", "9", "15", "50 m/s 타일/s"], [[key, f"{value['resident_tiles']:.0f}"] + [f"{value['tiles_per_s'][f'{speed}']:.1f}" for speed in SPEEDS_MS] for key, value in nav_invoker.items()]))
    sections.append("## 생성 원형 측정(Python numpy, 단일 스레드)\n" + json.dumps(prototype, ensure_ascii=False, indent=1))
    save_rows = []
    for chunk_key, by_scenario in save_by_chunk.items():
        for name, entry in by_scenario.items():
            save_rows.append([f"{chunk_key}m", name, entry["edits"], f"{entry['explored_area_km2']:.1f}", f"{entry['explored_chunks']:.0f}", f"{entry['touched_chunks']:.0f}", f"{entry['record_bytes'] / 1e6:.2f}", f"{entry['explored_bitmap_bytes'] / 1e3:.1f}", f"{entry['explored_state_bytes'] / 1e6:.2f}", f"{entry['total_bytes'] / 1e6:.2f}", f"{entry['placed_objects_per_base_chunk']:.0f}"])
    sections.append("## 100시간 세이브\n" + format_markdown_table(["청크", "시나리오", "변경 수", "탐험 km²", "탐험 청크", "변경 청크", "레코드 MB", "탐험 비트맵 KB", "탐험 상태 MB", "합계 MB", "기지 청크당 설치물"], save_rows))
    sections.append("## 좌표 한계\n" + json.dumps(world_limit, ensure_ascii=False, indent=1))
    (output_dir / "tables.md").write_text("# 무한 세계 예산 모델 결과\n\n" + "\n\n".join(sections) + "\n", encoding="utf-8")
    print(json.dumps({"worst_view_radius_m": worst_view, "worst_tall_radius_m": worst_tall, "shadow_radius_m": shadow_radius, "prototype": prototype["time_ms_per_chunk"], "seam": prototype["seam_exact"]}, ensure_ascii=False))


if __name__ == "__main__":
    main()
