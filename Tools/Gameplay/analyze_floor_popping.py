"""실행 환경: 시스템 Python(numpy·Pillow·scipy 필요, cv2·skimage 불필요), 언리얼 에디터·PIE 필요 없음.
실행 명령: python -B Tools/Gameplay/analyze_floor_popping.py --run <폴더|이름> | --compare <A> <B> | --self-test  (선택: --config 값.json)
출력: 실행 폴더에 analysis.json(noise 절 포함)·summary.md·sheets/pop_*.png·sheets/pop_map.png(위에서 본 사건 지도), 비교는 Saved/Gameplay/FloorPopping/compare_<A>__<B>/ 에 compare.md·compare_glide.webp·compare_stare_<구간>.png, 자체 시험은 기대·실제 표와 종료 코드(0 통과, 1 어긋남)
상태: v2(GF-07, 2026-11-20), 합성 자체 시험(상자·바람·상자 무늬 교체 포함)과 실제 실행 smoke2·base_a 로 확인, 판정 기준값은 noise 절 제안값으로 자료에서 보정할 것.
"""
import argparse
import csv
import json
import math
import shutil
import sys
import time
from collections import namedtuple
from concurrent.futures import ProcessPoolExecutor, ThreadPoolExecutor
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

PROJECT_ROOT = Path(__file__).resolve().parents[2]
FLOOR_POPPING_ROOT = PROJECT_ROOT / "Saved" / "Gameplay" / "FloorPopping"
SELF_TEST_DEFAULT_DIR = PROJECT_ROOT / "Saved" / "AgentOps" / "20261120" / "gf07" / "selftest"
DEFAULT_HEIGHT_PATH = PROJECT_ROOT / "Saved" / "WorldGen" / "AshenVale" / "height.r16"

FRAMES_COLUMNS = {
    "frame": ["frame"],
    "real_perf": ["real_perf"],
    "real_epoch": ["real_epoch"],
    "game_s": ["game_s"],
    "delta": ["delta"],
    "seg": ["seg"],
    "pawn_x": ["pawn_x"],
    "pawn_y": ["pawn_y"],
    "pawn_z": ["pawn_z"],
    "cam_x": ["cam_x"],
    "cam_y": ["cam_y"],
    "cam_z": ["cam_z"],
    "cam_pitch": ["cam_pitch"],
    "cam_yaw": ["cam_yaw"],
    "cam_roll": ["cam_roll"],
    "cam_fov": ["cam_fov", "fov"],
}
REQUIRED_FRAMES_COLUMNS = [
    "frame", "real_perf", "delta", "seg", "pawn_x", "pawn_y", "pawn_z",
    "cam_x", "cam_y", "cam_z", "cam_pitch", "cam_yaw", "cam_fov",
]
SHOT_KEYS = {
    "number": "n",
    "path": "path",
    "request_frame": "request_frame",
    "request_real": "request_real_perf",
    "seg": "seg",
    "kind": "seg_kind",
    "seg_time": "seg_time",
    "missing": "missing",
}
PROFILE_TIME_COLUMNS = ["FrameTime", "GameThreadTime", "RenderThreadTime", "GPUTime"]
PROFILE_STREAMING_COLUMNS = [
    "StreamingPool", "WantedMips", "CachedMips", "PendingStreamInData", "DesiredDataLoadedPercent",
]
PROFILE_PENDING_COLUMN = "PendingStreamInData"
PROFILE_FRAME_TIME_COLUMN = "FrameTime"

CHAIN_KINDS = ("settle", "glide")
LUMA_WEIGHTS = np.array([0.299, 0.587, 0.114], dtype=np.float32)

DEFAULT_PARAMS = {
    "ortho_cm": 10.0,
    "block_cm": 50.0,
    "block_valid_fraction": 0.8,
    "t_pop": 0.08,
    "r_noise": 2.5,
    "r_persist": 0.7,
    "t_pop_floor": 0.06,
    "t_pop_suggest_factor": 2.0,
    "min_event_blocks": 2,
    "max_registration_cm": 15.0,
    "registration_every": 4,
    "edge_margin_px": 8,
    "depth_min_cm": 10.0,
    "pawn_radius_cm": 60.0,
    "pawn_height_cm": 200.0,
    "pawn_ground_offset_cm": 88.0,
    "pawn_margin_px": 20,
    "footprint_height_span_cm": 250.0,
    "footprint_max_range_cm": 3500.0,
    "stare_settle_fraction": 0.01,
    "stare_noise_percentile": 95.0,
    "noise_percentiles": [50, 90, 95, 99],
    "noise_values_per_pair": 1500,
    "offset_pairs_max": 16,
    "offset_search_ortho_cm": 25.0,
    "pose_artifact_rows": [-3, -2, -1, 1, 2, 3],
    "pose_artifact_ratio": 0.5,
    "late_visible_fraction": 0.5,
    "pending_threshold": 0.5,
    "profile_align_search_rows": 300,
    "sheet_photo_width": 480,
    "sheet_event_limit": 40,
    "map_margin_cm": 1500.0,
    "map_cell_cm": 100.0,
    "map_pixel_cm": 10.0,
    "map_max_pixels": 6000,
    "compare_frame_width": 1280,
    "compare_fps": 8,
    "compare_max_frames": 400,
    "compare_stare_seconds": 1.0,
    "analysis_workers": 4,
    "height_path": str(DEFAULT_HEIGHT_PATH),
    "pose_offset_candidates": [1, 0, 2],
    "grab_offset_candidates": list(range(0, 13)),
    "grab_latency_base_s": 0.24,
    "screen_masks": [[0.40, 0.90, 0.60, 1.0]],
}

ZONE_ROWS = ("위", "가운데", "아래")
ZONE_COLUMNS = ("왼", "가운데", "오른")
DISTANCE_BINS = (
    (0.0, 1000.0, "0~10m"),
    (1000.0, 1500.0, "10~15m"),
    (1500.0, 2000.0, "15~20m"),
    (2000.0, 2500.0, "20~25m"),
    (2500.0, float("inf"), "25m 이상"),
)

Pose = namedtuple("Pose", "cam pitch yaw fov pawn")
BlockGrid = namedtuple("BlockGrid", "bx0 by0 values")
PairView = namedtuple("PairView", "bx_lo by_lo nbx nby rgb_a rgb_b mask")


def to_float(text):
    try:
        return float(text)
    except (TypeError, ValueError):
        return float("nan")


def json_default(value):
    if isinstance(value, (np.integer,)):
        return int(value)
    if isinstance(value, (np.floating,)):
        return None if not math.isfinite(float(value)) else float(value)
    if isinstance(value, np.ndarray):
        return value.tolist()
    if isinstance(value, Path):
        return str(value)
    raise TypeError(type(value).__name__)


def clean_for_json(value):
    if isinstance(value, dict):
        return {str(key): clean_for_json(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [clean_for_json(item) for item in value]
    if isinstance(value, (float, np.floating)):
        return float(value) if math.isfinite(float(value)) else None
    return value


def write_json(path, payload):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(clean_for_json(payload), handle, ensure_ascii=False, indent=1, default=json_default)


def percentile_or_none(values, percent):
    array = np.asarray(values, dtype=float)
    array = array[np.isfinite(array)]
    if array.size == 0:
        return None
    return float(np.percentile(array, percent))


def camera_basis(pitch_deg, yaw_deg):
    pitch = math.radians(pitch_deg)
    yaw = math.radians(yaw_deg)
    forward = np.array([math.cos(pitch) * math.cos(yaw), math.cos(pitch) * math.sin(yaw), math.sin(pitch)])
    right = np.array([-math.sin(yaw), math.cos(yaw), 0.0])
    up = np.cross(forward, right)
    return forward, right, up


def project_points(pose, x, y, z, width, height):
    forward, right, up = camera_basis(pose.pitch, pose.yaw)
    dx = x - pose.cam[0]
    dy = y - pose.cam[1]
    dz = z - pose.cam[2]
    depth = dx * forward[0] + dy * forward[1] + dz * forward[2]
    lateral = dx * right[0] + dy * right[1] + dz * right[2]
    vertical = dx * up[0] + dy * up[1] + dz * up[2]
    focal = (width / 2.0) / math.tan(math.radians(pose.fov) / 2.0)
    safe_depth = np.where(depth > 1e-6, depth, 1e-6)
    u = width / 2.0 + focal * lateral / safe_depth
    v = height / 2.0 - focal * vertical / safe_depth
    return u, v, depth


class HeightField:
    def __init__(self, heights_cm, cell_cm=100.0):
        self.heights = np.asarray(heights_cm, dtype=np.float32)
        self.size = self.heights.shape[0]
        self.cell_cm = cell_cm
        self.center = (self.size - 1) / 2.0

    @classmethod
    def load(cls, path):
        raw = np.fromfile(path, dtype="<u2")
        size = int(round(math.sqrt(raw.size)))
        if size * size != raw.size:
            raise ValueError(f"높이 지도 크기가 정사각이 아님: {path}")
        heights = (raw.astype(np.float32) - 32768.0) / 128.0 * 100.0
        return cls(heights.reshape(size, size))

    def sample(self, x, y):
        limit = self.size - 1.000001
        gx = np.clip(np.asarray(x, dtype=np.float64) / self.cell_cm + self.center, 0.0, limit)
        gy = np.clip(np.asarray(y, dtype=np.float64) / self.cell_cm + self.center, 0.0, limit)
        x0 = gx.astype(np.int64)
        y0 = gy.astype(np.int64)
        fx = gx - x0
        fy = gy - y0
        grid = self.heights
        top = grid[y0, x0] * (1.0 - fx) + grid[y0, x0 + 1] * fx
        bottom = grid[y0 + 1, x0] * (1.0 - fx) + grid[y0 + 1, x0 + 1] * fx
        return top * (1.0 - fy) + bottom * fy


def load_frames_table(path):
    with open(path, newline="", encoding="utf-8-sig") as handle:
        reader = csv.reader(handle)
        header = next(reader)
        rows = [row for row in reader if row]
    index = {name.strip().lower(): position for position, name in enumerate(header)}
    table = {}
    for logical, aliases in FRAMES_COLUMNS.items():
        position = next((index[name] for name in aliases if name in index), None)
        if position is None:
            if logical in REQUIRED_FRAMES_COLUMNS:
                raise ValueError(f"frames.csv에 열이 없음: {aliases}")
            continue
        table[logical] = np.array([to_float(row[position]) if position < len(row) else float("nan") for row in rows])
    return table


def resolve_shot_path(run_dir, entry):
    raw = entry.get(SHOT_KEYS["path"]) or ""
    candidates = [Path(raw), Path(str(raw) + ".png"), run_dir / "shots" / Path(raw).name, run_dir / "shots" / (Path(raw).name + ".png")]
    return next((candidate for candidate in candidates if raw and candidate.is_file()), None)


def build_photo_list(run_dir, result, frames):
    frame_to_row = {int(value): position for position, value in enumerate(frames["frame"]) if math.isfinite(value)}
    real_perf = frames["real_perf"]
    photos = []
    missing = []
    for entry in result.get("shots", []):
        number = entry.get(SHOT_KEYS["number"])
        path = None if entry.get(SHOT_KEYS["missing"]) else resolve_shot_path(run_dir, entry)
        if path is None:
            missing.append(number)
            continue
        request_frame = entry.get(SHOT_KEYS["request_frame"])
        request_real = entry.get(SHOT_KEYS["request_real"])
        row = frame_to_row.get(int(request_frame)) if request_frame is not None else None
        if row is None and request_real is not None:
            row = int(np.argmin(np.abs(real_perf - request_real)))
        if row is None:
            missing.append(number)
            continue
        photos.append({
            "n": number,
            "path": path,
            "row": row,
            "real": float(request_real) if request_real is not None else float(real_perf[row]),
            "seg": entry.get(SHOT_KEYS["seg"]),
            "kind": entry.get(SHOT_KEYS["kind"]) or "other",
            "seg_time": entry.get(SHOT_KEYS["seg_time"]),
        })
    photos.sort(key=lambda photo: photo["n"])
    return photos, missing


def pose_at_row(frames, row, offset):
    index = int(min(max(row + offset, 0), len(frames["frame"]) - 1))
    return Pose(
        cam=np.array([frames["cam_x"][index], frames["cam_y"][index], frames["cam_z"][index]]),
        pitch=float(frames["cam_pitch"][index]),
        yaw=float(frames["cam_yaw"][index]),
        fov=float(frames["cam_fov"][index]),
        pawn=np.array([frames["pawn_x"][index], frames["pawn_y"][index], frames["pawn_z"][index]]),
    )


def load_image_array(path):
    with Image.open(path) as image:
        return np.asarray(image.convert("RGB"), dtype=np.float32) / 255.0


def bilinear_sample(image, u, v):
    height, width = image.shape[:2]
    x = np.clip(u - 0.5, 0.0, width - 1.000001)
    y = np.clip(v - 0.5, 0.0, height - 1.000001)
    x0 = x.astype(np.int64)
    y0 = y.astype(np.int64)
    fx = (x - x0)[:, None].astype(np.float32)
    fy = (y - y0)[:, None].astype(np.float32)
    flat = image.reshape(-1, image.shape[2])
    base = y0 * width + x0
    top = flat[base] * (1.0 - fx) + flat[base + 1] * fx
    bottom = flat[base + width] * (1.0 - fx) + flat[base + width + 1] * fx
    return top * (1.0 - fy) + bottom * fy


def footprint_blocks(pose, width, height, ground_z, params):
    margin = params["edge_margin_px"]
    us = np.linspace(margin, width - margin, 33)
    vs = np.linspace(margin, height - margin, 33)
    border_u = np.concatenate([us, us, np.full_like(vs, margin), np.full_like(vs, width - margin)])
    border_v = np.concatenate([np.full_like(us, margin), np.full_like(us, height - margin), vs, vs])
    forward, right, up = camera_basis(pose.pitch, pose.yaw)
    focal = (width / 2.0) / math.tan(math.radians(pose.fov) / 2.0)
    directions = (
        forward[None, :]
        + right[None, :] * ((border_u - width / 2.0) / focal)[:, None]
        + up[None, :] * ((height / 2.0 - border_v) / focal)[:, None]
    )
    horizontal = np.maximum(np.hypot(directions[:, 0], directions[:, 1]), 1e-3)
    max_t = params["footprint_max_range_cm"] / horizontal
    xs = []
    ys = []
    for plane in (ground_z - params["footprint_height_span_cm"], ground_z + params["footprint_height_span_cm"]):
        slope = directions[:, 2]
        hit_t = np.where(slope < -1e-4, (plane - pose.cam[2]) / np.where(slope < -1e-4, slope, -1.0), max_t)
        hit_t = np.clip(hit_t, 0.0, max_t)
        xs.append(pose.cam[0] + hit_t * directions[:, 0])
        ys.append(pose.cam[1] + hit_t * directions[:, 1])
    all_x = np.concatenate(xs)
    all_y = np.concatenate(ys)
    block = params["block_cm"]
    return (
        int(math.floor(all_x.min() / block)) - 1,
        int(math.ceil(all_x.max() / block)) + 1,
        int(math.floor(all_y.min() / block)) - 1,
        int(math.ceil(all_y.max() / block)) + 1,
    )


def pawn_screen_box(pose, width, height, params):
    angles = np.linspace(0.0, 2.0 * math.pi, 16, endpoint=False)
    ring_x = pose.pawn[0] + params["pawn_radius_cm"] * np.cos(angles)
    ring_y = pose.pawn[1] + params["pawn_radius_cm"] * np.sin(angles)
    foot_z = pose.pawn[2] - params["pawn_ground_offset_cm"]
    points_x = np.concatenate([ring_x, ring_x])
    points_y = np.concatenate([ring_y, ring_y])
    points_z = np.concatenate([np.full(16, foot_z), np.full(16, foot_z + params["pawn_height_cm"])])
    u, v, depth = project_points(pose, points_x, points_y, points_z, width, height)
    visible = depth > params["depth_min_cm"]
    if not visible.any():
        return None
    margin = params["pawn_margin_px"]
    return u[visible].min() - margin, u[visible].max() + margin, v[visible].min() - margin, v[visible].max() + margin


class Ortho:
    def __init__(self, bx0, by0, nbx, nby, rgb, valid, block_valid, width, height, pose):
        self.bx0 = bx0
        self.by0 = by0
        self.nbx = nbx
        self.nby = nby
        self.rgb = rgb
        self.valid = valid
        self.block_valid = block_valid
        self.width = width
        self.height = height
        self.pose = pose


def block_samples(params):
    return int(round(params["block_cm"] / params["ortho_cm"]))


def build_ortho(image, pose, height_field, params):
    image_height, image_width = image.shape[:2]
    samples = block_samples(params)
    ortho_cm = params["ortho_cm"]
    ground_z = float(height_field.sample(np.array([pose.pawn[0]]), np.array([pose.pawn[1]]))[0])
    bx_lo, bx_hi, by_lo, by_hi = footprint_blocks(pose, image_width, image_height, ground_z, params)
    nbx = bx_hi - bx_lo
    nby = by_hi - by_lo
    xs = (bx_lo * samples + np.arange(nbx * samples) + 0.5) * ortho_cm
    ys = (by_lo * samples + np.arange(nby * samples) + 0.5) * ortho_cm
    grid_x = np.broadcast_to(xs[:, None], (xs.size, ys.size)).ravel()
    grid_y = np.broadcast_to(ys[None, :], (xs.size, ys.size)).ravel()
    grid_z = height_field.sample(grid_x, grid_y)
    u, v, depth = project_points(pose, grid_x, grid_y, grid_z, image_width, image_height)
    margin = params["edge_margin_px"]
    valid = (
        (depth > params["depth_min_cm"])
        & (u >= margin) & (u <= image_width - margin)
        & (v >= margin) & (v <= image_height - margin)
    )
    box = pawn_screen_box(pose, image_width, image_height, params)
    if box is not None:
        valid &= ~((u >= box[0]) & (u <= box[1]) & (v >= box[2]) & (v <= box[3]))
    for left, top, right, bottom in params.get("screen_masks", []):
        valid &= ~((u >= left * image_width) & (u <= right * image_width) & (v >= top * image_height) & (v <= bottom * image_height))
    rgb = np.zeros((grid_x.size, 3), dtype=np.float32)
    indices = np.flatnonzero(valid)
    rgb[indices] = bilinear_sample(image, u[indices], v[indices])
    rgb = rgb.reshape(xs.size, ys.size, 3)
    valid = valid.reshape(xs.size, ys.size)
    counts = valid.reshape(nbx, samples, nby, samples).sum(axis=(1, 3))
    block_valid = counts >= params["block_valid_fraction"] * samples * samples
    return Ortho(bx_lo, by_lo, nbx, nby, rgb, valid, block_valid, image_width, image_height, pose)


def make_pair_view(first, second, params):
    samples = block_samples(params)
    bx_lo = max(first.bx0, second.bx0)
    bx_hi = min(first.bx0 + first.nbx, second.bx0 + second.nbx)
    by_lo = max(first.by0, second.by0)
    by_hi = min(first.by0 + first.nby, second.by0 + second.nby)
    if bx_hi <= bx_lo or by_hi <= by_lo:
        return None
    slice_a = (slice((bx_lo - first.bx0) * samples, (bx_hi - first.bx0) * samples),
               slice((by_lo - first.by0) * samples, (by_hi - first.by0) * samples))
    slice_b = (slice((bx_lo - second.bx0) * samples, (bx_hi - second.bx0) * samples),
               slice((by_lo - second.by0) * samples, (by_hi - second.by0) * samples))
    mask = first.valid[slice_a] & second.valid[slice_b]
    return PairView(bx_lo, by_lo, bx_hi - bx_lo, by_hi - by_lo, first.rgb[slice_a], second.rgb[slice_b], mask)


def block_difference(view, params):
    if view is None:
        return None
    samples = block_samples(params)
    difference = np.abs(view.rgb_a - view.rgb_b).mean(axis=2)
    shape = (view.nbx, samples, view.nby, samples)
    counts = view.mask.reshape(shape).sum(axis=(1, 3))
    sums = (difference * view.mask).reshape(shape).sum(axis=(1, 3))
    enough = counts >= params["block_valid_fraction"] * samples * samples
    values = np.full(counts.shape, np.nan, dtype=np.float32)
    values[enough] = sums[enough] / counts[enough]
    return BlockGrid(view.bx_lo, view.by_lo, values)


def estimate_registration_cm(view, params):
    if view is None or int(view.mask.sum()) < 400:
        return None
    mask = view.mask
    luma_a = view.rgb_a @ LUMA_WEIGHTS
    luma_b = view.rgb_b @ LUMA_WEIGHTS
    luma_a = (luma_a - luma_a[mask].mean()) * mask
    luma_b = (luma_b - luma_b[mask].mean()) * mask
    size_x, size_y = mask.shape
    window = np.outer(np.hanning(size_x), np.hanning(size_y)).astype(np.float32)
    fft_shape = (scipy_fast_len(size_x), scipy_fast_len(size_y))
    spectrum_a = np.fft.rfft2(luma_a * window, s=fft_shape)
    spectrum_b = np.fft.rfft2(luma_b * window, s=fft_shape)
    cross = spectrum_a * np.conj(spectrum_b)
    magnitude = np.abs(cross)
    cross = cross / (magnitude + 1e-3 * magnitude.mean() + 1e-12)
    freq_x = np.fft.fftfreq(fft_shape[0])[:, None]
    freq_y = np.fft.rfftfreq(fft_shape[1])[None, :]
    sigma_samples = 1.5
    cross = cross * np.exp(-(freq_x ** 2 + freq_y ** 2) * (2.0 * math.pi * sigma_samples) ** 2 / 2.0)
    surface = np.fft.irfft2(cross, s=fft_shape)
    peak_x, peak_y = np.unravel_index(int(np.argmax(surface)), surface.shape)
    shift = []
    for axis, peak in ((0, peak_x), (1, peak_y)):
        length = surface.shape[axis]
        if axis == 0:
            before, center, after = surface[(peak - 1) % length, peak_y], surface[peak, peak_y], surface[(peak + 1) % length, peak_y]
        else:
            before, center, after = surface[peak_x, (peak - 1) % length], surface[peak_x, peak_y], surface[peak_x, (peak + 1) % length]
        denominator = before - 2.0 * center + after
        refine = 0.5 * (before - after) / denominator if abs(denominator) > 1e-12 else 0.0
        position = peak + refine
        shift.append(position - length if position > length / 2.0 else position)
    return float(math.hypot(shift[0], shift[1]) * params["ortho_cm"])


def scipy_fast_len(length):
    from scipy import fft as scipy_fft
    return scipy_fft.next_fast_len(int(length), real=True)


def crop_block_grid(grid, bx_lo, bx_hi, by_lo, by_hi):
    return grid.values[bx_lo - grid.bx0:bx_hi - grid.bx0, by_lo - grid.by0:by_hi - grid.by0]


def crop_ortho_samples(ortho, bx_lo, bx_hi, by_lo, by_hi, params):
    samples = block_samples(params)
    region = (slice((bx_lo - ortho.bx0) * samples, (bx_hi - ortho.bx0) * samples),
              slice((by_lo - ortho.by0) * samples, (by_hi - ortho.by0) * samples))
    return ortho.rgb[region], ortho.valid[region]


def laplacian_magnitude(luma):
    result = np.zeros_like(luma)
    result[1:-1, 1:-1] = np.abs(
        4.0 * luma[1:-1, 1:-1] - luma[:-2, 1:-1] - luma[2:, 1:-1] - luma[1:-1, :-2] - luma[1:-1, 2:]
    )
    return result


def block_visible_fraction(history_entry, block_xs, block_ys):
    bx0, by0, flags = history_entry
    ix = block_xs - bx0
    iy = block_ys - by0
    inside = (ix >= 0) & (ix < flags.shape[0]) & (iy >= 0) & (iy < flags.shape[1])
    seen = np.zeros(block_xs.shape, dtype=bool)
    seen[inside] = flags[ix[inside], iy[inside]]
    return float(seen.mean())


def common_block_bounds(grids):
    if any(grid is None for grid in grids):
        return None
    bx_lo = max(grid.bx0 for grid in grids)
    bx_hi = min(grid.bx0 + grid.values.shape[0] for grid in grids)
    by_lo = max(grid.by0 for grid in grids)
    by_hi = min(grid.by0 + grid.values.shape[1] for grid in grids)
    if bx_hi <= bx_lo or by_hi <= by_lo:
        return None
    return bx_lo, bx_hi, by_lo, by_hi


def pop_block_mask(steady, noise_before, noise_after, persist, threshold, params):
    with np.errstate(invalid="ignore"):
        noise = np.maximum(noise_before, noise_after)
        return (
            np.isfinite(steady) & np.isfinite(noise) & np.isfinite(persist)
            & (steady >= threshold)
            & (steady >= params["r_noise"] * noise)
            & (persist >= params["r_persist"] * steady)
        )


def label_pop_blocks(mask):
    return ndimage.label(mask, structure=np.ones((3, 3), dtype=int))


def count_large_components(mask, params):
    labels, count = label_pop_blocks(mask)
    if count == 0:
        return 0
    sizes = np.bincount(labels.ravel(), minlength=count + 1)[1:]
    return int((sizes >= params["min_event_blocks"]).sum())


def zone_labels():
    return [f"{row}-{column}" for row in ZONE_ROWS for column in ZONE_COLUMNS]


def screen_zone(screen):
    if screen is None:
        return None
    column = min(2, max(0, int(screen[0] * 3.0)))
    row = min(2, max(0, int(screen[1] * 3.0)))
    return f"{ZONE_ROWS[row]}-{ZONE_COLUMNS[column]}"


def distance_label(distance_cm):
    for low, high, label in DISTANCE_BINS:
        if low <= distance_cm < high:
            return label
    return DISTANCE_BINS[-1][2]


def make_pose_shift_builder(photo, frames, height_field, params, offset):
    cache = {}

    def build(delta):
        if "image" not in cache:
            cache["image"] = load_image_array(photo["path"])
        return build_ortho(cache["image"], pose_at_row(frames, photo["row"], offset + delta), height_field, params)

    return build


def pose_shift_floor_ratios(first, build_shifted, bounds, steady, members, params):
    bx_lo, bx_hi, by_lo, by_hi = bounds
    shifted_values = []
    for delta in params["pose_artifact_rows"]:
        grid = block_difference(make_pair_view(first, build_shifted(delta), params), params)
        if grid is None:
            continue
        window = np.full(steady.shape, np.nan, dtype=np.float32)
        lo_x, hi_x = max(bx_lo, grid.bx0), min(bx_hi, grid.bx0 + grid.values.shape[0])
        lo_y, hi_y = max(by_lo, grid.by0), min(by_hi, grid.by0 + grid.values.shape[1])
        if hi_x > lo_x and hi_y > lo_y:
            window[lo_x - bx_lo:hi_x - bx_lo, lo_y - by_lo:hi_y - by_lo] = grid.values[lo_x - grid.bx0:hi_x - grid.bx0, lo_y - grid.by0:hi_y - grid.by0]
        shifted_values.append(window)
    ratios = []
    for member in members:
        base = float(steady[member].mean())
        means = [float(np.nanmean(window[member])) for window in shifted_values if np.isfinite(window[member]).any()]
        ratios.append(min(means) / base if means and base > 0 else None)
    return ratios


def detect_pop_events(center_index, photos, orthos, grids, history, chain_start, height_field, params, extra_thresholds, build_shifted):
    first, middle = orthos
    thresholds = dict(extra_thresholds)
    thresholds["main"] = params["t_pop"]
    bounds = common_block_bounds(grids)
    if bounds is None:
        return [], {name: 0 for name in extra_thresholds}, 0
    bx_lo, bx_hi, by_lo, by_hi = bounds
    steady, noise_before, noise_after, persist = [crop_block_grid(grid, bx_lo, bx_hi, by_lo, by_hi) for grid in grids]
    members_by_name = {}
    for name, threshold in thresholds.items():
        labels, count = label_pop_blocks(pop_block_mask(steady, noise_before, noise_after, persist, threshold, params))
        sizes = np.bincount(labels.ravel(), minlength=count + 1)
        members_by_name[name] = [labels == label for label in range(1, count + 1) if sizes[label] >= params["min_event_blocks"]]
    extra_counts = {name: 0 for name in extra_thresholds}
    if not any(members_by_name.values()):
        return [], extra_counts, 0
    all_members = [member for members in members_by_name.values() for member in members]
    ratios = pose_shift_floor_ratios(first, build_shifted, bounds, steady, all_members, params)
    ratio_of = {id(member): ratio for member, ratio in zip(all_members, ratios)}
    kept_by_name = {}
    for name, members in members_by_name.items():
        kept_by_name[name] = [
            member for member in members
            if ratio_of[id(member)] is None or ratio_of[id(member)] >= params["pose_artifact_ratio"]
        ]
    for name in extra_thresholds:
        extra_counts[name] = len(kept_by_name[name])
    rejected = len(members_by_name["main"]) - len(kept_by_name["main"])
    wanted = kept_by_name["main"]
    if not wanted:
        return [], extra_counts, rejected
    samples = block_samples(params)
    rgb_first, valid_first = crop_ortho_samples(first, bx_lo, bx_hi, by_lo, by_hi, params)
    rgb_middle, valid_middle = crop_ortho_samples(middle, bx_lo, bx_hi, by_lo, by_hi, params)
    cross = ndimage.generate_binary_structure(2, 1)
    inner = ndimage.binary_erosion(valid_first & valid_middle, structure=cross)
    laplacian_first = laplacian_magnitude(rgb_first @ LUMA_WEIGHTS)
    laplacian_middle = laplacian_magnitude(rgb_middle @ LUMA_WEIGHTS)
    photo = photos[center_index]
    camera = middle.pose.cam
    events = []
    for member in wanted:
        block_index = np.argwhere(member)
        block_xs = block_index[:, 0] + bx_lo
        block_ys = block_index[:, 1] + by_lo
        center_x = float(((block_xs + 0.5) * params["block_cm"]).mean())
        center_y = float(((block_ys + 0.5) * params["block_cm"]).mean())
        sample_mask = np.repeat(np.repeat(member, samples, axis=0), samples, axis=1) & inner
        detail_ratio = None
        if sample_mask.any():
            detail_ratio = float(laplacian_middle[sample_mask].mean() / max(float(laplacian_first[sample_mask].mean()), 1e-6))
        center_z = float(height_field.sample(np.array([center_x]), np.array([center_y]))[0])
        u, v, depth = project_points(middle.pose, np.array([center_x]), np.array([center_y]), np.array([center_z]), middle.width, middle.height)
        screen = [float(u[0] / middle.width), float(v[0] / middle.height)] if depth[0] > 0 else None
        distance_cm = float(math.hypot(center_x - camera[0], center_y - camera[1]))
        first_visible = center_index
        while first_visible - 1 >= chain_start[center_index]:
            previous = first_visible - 1
            if previous not in history:
                break
            if block_visible_fraction(history[previous], block_xs, block_ys) < params["late_visible_fraction"]:
                break
            first_visible = previous
        events.append({
            "photo": photo["n"],
            "real_s": photo["real"],
            "seg": photo["seg"],
            "seg_time_s": photo["seg_time"],
            "center_cm": [center_x, center_y],
            "blocks": int(len(block_index)),
            "area_m2": float(len(block_index) * (params["block_cm"] / 100.0) ** 2),
            "mean_d": float(steady[member].mean()),
            "persist_ratio": float((persist[member] / steady[member]).mean()),
            "noise_ratio": float((steady[member] / np.maximum(np.maximum(noise_before[member], noise_after[member]), 1e-6)).mean()),
            "pose_shift_ratio": ratio_of[id(member)],
            "screen": screen,
            "zone": screen_zone(screen),
            "distance_cm": distance_cm,
            "detail_ratio": detail_ratio,
            "late_s": float(photo["real"] - photos[first_visible]["real"]),
            "late_is_lower_bound": bool(first_visible == chain_start[center_index]),
            "block_cells": [[int(x), int(y)] for x, y in zip(block_xs, block_ys)],
        })
    return events, extra_counts, rejected


class OrthoProvider:
    def __init__(self, photos, frames, height_field, params, offset, workers):
        self.photos = photos
        self.frames = frames
        self.height_field = height_field
        self.params = params
        self.offset = offset
        self.executor = ThreadPoolExecutor(max_workers=max(1, workers))
        self.futures = {}

    def build(self, index):
        photo = self.photos[index]
        pose = pose_at_row(self.frames, photo["row"], self.offset)
        return build_ortho(load_image_array(photo["path"]), pose, self.height_field, self.params)

    def request(self, index):
        if 0 <= index < len(self.photos) and index not in self.futures:
            self.futures[index] = self.executor.submit(self.build, index)

    def get(self, index):
        self.request(index)
        return self.futures[index].result()

    def release_before(self, index):
        for key in [key for key in self.futures if key < index]:
            del self.futures[key]

    def close(self):
        self.executor.shutdown(wait=True)


def link_flags(photos):
    linked = [False] * len(photos)
    chain_start = list(range(len(photos)))
    for index in range(1, len(photos)):
        both_chain = photos[index]["kind"] in CHAIN_KINDS and photos[index - 1]["kind"] in CHAIN_KINDS
        linked[index] = both_chain
        chain_start[index] = chain_start[index - 1] if both_chain else index
    return linked, chain_start


def search_params(params):
    reduced = dict(params)
    reduced["ortho_cm"] = params["offset_search_ortho_cm"]
    return reduced


def choose_pose_offset(photos, frames, height_field, params, workers):
    pairs = [
        (index - 1, index) for index in range(1, len(photos))
        if photos[index]["kind"] == "glide" and photos[index - 1]["kind"] == "glide"
        and photos[index]["seg"] == photos[index - 1]["seg"]
    ]
    candidates = tuple(params["pose_offset_candidates"])
    result = {"candidates": {}, "chosen": candidates[0], "pairs_used": 0}
    if not pairs:
        return result
    limit = params["offset_pairs_max"]
    if len(pairs) > limit:
        picks = np.linspace(0, len(pairs) - 1, limit).round().astype(int)
        pairs = [pairs[position] for position in picks]
    reduced = search_params(params)

    def score_pair(pair):
        image_a = load_image_array(photos[pair[0]]["path"])
        image_b = load_image_array(photos[pair[1]]["path"])
        scores = {}
        for offset in candidates:
            ortho_a = build_ortho(image_a, pose_at_row(frames, photos[pair[0]]["row"], offset), height_field, reduced)
            ortho_b = build_ortho(image_b, pose_at_row(frames, photos[pair[1]]["row"], offset), height_field, reduced)
            grid = block_difference(make_pair_view(ortho_a, ortho_b, reduced), reduced)
            values = None if grid is None else grid.values[np.isfinite(grid.values)]
            scores[offset] = float(np.median(values)) if values is not None and values.size else float("nan")
        return scores

    with ThreadPoolExecutor(max_workers=max(1, workers)) as executor:
        all_scores = list(executor.map(score_pair, pairs))
    medians = {}
    for offset in candidates:
        column = np.array([scores[offset] for scores in all_scores])
        column = column[np.isfinite(column)]
        medians[offset] = float(np.median(column)) if column.size else float("nan")
    finite = {offset: value for offset, value in medians.items() if math.isfinite(value)}
    chosen = min(sorted(finite), key=lambda offset: finite[offset]) if finite else candidates[0]
    result["candidates"] = {str(offset): medians[offset] for offset in candidates}
    result["chosen"] = int(chosen)
    result["pairs_used"] = len(pairs)
    return result


def sample_finite_values(grid, params):
    if grid is None:
        return None
    values = grid.values[np.isfinite(grid.values)]
    limit = params["noise_values_per_pair"]
    if values.size > limit:
        values = values[np.linspace(0, values.size - 1, limit).astype(int)]
    return values


def quantile_table(arrays, params):
    values = np.concatenate(arrays) if arrays else np.array([])
    table = {"blocks": int(values.size)}
    for percent in params["noise_percentiles"]:
        table[f"p{int(percent)}"] = percentile_or_none(values, percent)
    return table


def analyze_pop_chain(photos, frames, height_field, params, offset, workers, extra_thresholds):
    linked, chain_start = link_flags(photos)
    provider = OrthoProvider(photos, frames, height_field, params, offset, workers)
    history = {}
    adjacent = {}
    registration = []
    walk_values = []
    events = []
    extra_totals = {name: 0 for name in extra_thresholds}
    rejected_total = 0
    ahead = max(2, workers)
    pair_count = 0
    try:
        for index in range(len(photos)):
            for future_index in range(index, index + ahead + 1):
                if photos[min(future_index, len(photos) - 1)]["kind"] in CHAIN_KINDS:
                    provider.request(future_index)
            if photos[index]["kind"] not in CHAIN_KINDS:
                continue
            current = provider.get(index)
            history[index] = (current.bx0, current.by0, current.block_valid)
            if linked[index]:
                previous = provider.get(index - 1)
                view = make_pair_view(previous, current, params)
                adjacent[index] = block_difference(view, params)
                pair_count += 1
                if photos[index]["kind"] == "glide" and photos[index - 1]["kind"] == "glide" and photos[index]["seg"] == photos[index - 1]["seg"]:
                    values = sample_finite_values(adjacent[index], params)
                    if values is not None and values.size:
                        walk_values.append(values)
                if pair_count % max(1, int(params["registration_every"])) == 0:
                    shift = estimate_registration_cm(view, params)
                    if shift is not None:
                        registration.append(shift)
            if index >= 3 and linked[index] and linked[index - 1] and linked[index - 2]:
                skip_grid = block_difference(make_pair_view(provider.get(index - 3), current, params), params)
                found, extra_counts, rejected = detect_pop_events(
                    index - 1, photos, (provider.get(index - 2), provider.get(index - 1)),
                    (adjacent.get(index - 1), adjacent.get(index - 2), adjacent.get(index), skip_grid),
                    history, chain_start, height_field, params, extra_thresholds,
                    make_pose_shift_builder(photos[index - 1], frames, height_field, params, offset),
                )
                events += found
                rejected_total += rejected
                for name, count in extra_counts.items():
                    extra_totals[name] += count
            for key in [key for key in adjacent if key < index - 3]:
                del adjacent[key]
            provider.release_before(index - 2)
    finally:
        provider.close()
    return events, registration, walk_values, extra_totals, rejected_total


def analyze_stare_segments(photos, frames, height_field, params, offset, workers):
    groups = {}
    for index, photo in enumerate(photos):
        if photo["kind"] == "stare":
            groups.setdefault(photo["seg"], []).append(index)
    results = []
    noise_values = []
    ahead = max(2, workers)
    provider = OrthoProvider(photos, frames, height_field, params, offset, workers)
    try:
        for seg, indices in groups.items():
            reference_index = indices[-1]
            reference = provider.get(reference_index)
            per_photo = []
            settle_time = 0.0
            valid_blocks = 0
            noise_floor = None
            previous = None
            for position, index in enumerate(indices):
                for future_index in indices[position:position + ahead + 1]:
                    provider.request(future_index)
                ortho = provider.get(index)
                if previous is not None:
                    values = sample_finite_values(block_difference(make_pair_view(previous, ortho, params), params), params)
                    if values is not None and values.size:
                        noise_values.append(values)
                previous = ortho
                provider.release_before(index)
                if index == reference_index:
                    continue
                grid = block_difference(make_pair_view(ortho, reference, params), params)
                values = np.array([]) if grid is None else grid.values[np.isfinite(grid.values)]
                valid_blocks = max(valid_blocks, int(values.size))
                fraction = float((values >= params["t_pop"]).mean()) if values.size else 0.0
                seg_time = photos[index]["seg_time"]
                per_photo.append({
                    "photo": photos[index]["n"],
                    "seg_time_s": seg_time,
                    "changed_fraction": fraction,
                    "median_d": float(np.median(values)) if values.size else None,
                })
                if values.size and fraction >= params["stare_settle_fraction"] and seg_time is not None:
                    settle_time = max(settle_time, float(seg_time))
                if position == len(indices) - 2:
                    noise_floor = percentile_or_none(values, params["stare_noise_percentile"])
            results.append({
                "seg": seg,
                "photos": per_photo,
                "reference_photo": photos[reference_index]["n"],
                "valid_blocks": valid_blocks,
                "settle_time_s": settle_time,
                "noise_floor_d": noise_floor,
                "photo_numbers": [photos[index]["n"] for index in indices],
            })
            provider.release_before(len(photos))
    finally:
        provider.close()
    return results, noise_values


def find_profile_column(header_names, wanted):
    lowered = [name.strip().lower() for name in header_names]
    target = wanted.lower()
    for position, name in enumerate(lowered):
        if name == target:
            return position
    candidates = [position for position, name in enumerate(lowered) if name.endswith("/" + target)]
    preferred = [position for position in candidates if "texturestreaming" in lowered[position]]
    if preferred:
        return preferred[0]
    return candidates[0] if candidates else None


def read_profile_csv(path):
    with open(path, newline="", encoding="utf-8-sig") as handle:
        reader = csv.reader(handle)
        header = next(reader)
        rows = []
        for row in reader:
            if not row or row[0].strip().startswith("["):
                continue
            rows.append(row)
    return header, rows


def column_values(header, rows, wanted):
    position = find_profile_column(header, wanted)
    if position is None:
        return None
    return np.array([to_float(row[position]) if position < len(row) else float("nan") for row in rows])


def align_profile_rows(frame_time_ms, frames, search_rows):
    delta_ms = frames["delta"] * 1000.0
    best = (0, -2.0)
    for offset in range(-search_rows, search_rows + 1):
        start_profile = max(0, -offset)
        start_frames = start_profile + offset
        length = min(len(frame_time_ms) - start_profile, len(delta_ms) - start_frames)
        if length < 50:
            continue
        left = frame_time_ms[start_profile:start_profile + length]
        right = delta_ms[start_frames:start_frames + length]
        if np.std(left) < 1e-9 or np.std(right) < 1e-9:
            continue
        correlation = float(np.corrcoef(left, right)[0, 1])
        if correlation > best[1]:
            best = (offset, correlation)
    return best


def longest_run_seconds(flags, weights_ms):
    longest = 0.0
    current = 0.0
    for flag, weight in zip(flags, weights_ms):
        current = current + weight / 1000.0 if flag else 0.0
        longest = max(longest, current)
    return longest


def summarize_profile(path, frames, photos, params):
    header, rows = read_profile_csv(path)
    columns = {name: column_values(header, rows, name) for name in PROFILE_TIME_COLUMNS + PROFILE_STREAMING_COLUMNS}
    frame_time = columns.get(PROFILE_FRAME_TIME_COLUMN)
    summary = {"rows": len(rows), "missing_columns": [name for name, values in columns.items() if values is None]}
    if frame_time is None:
        return summary
    offset, correlation = align_profile_rows(frame_time, frames, params["profile_align_search_rows"])
    summary["align_offset_rows"] = offset
    summary["align_correlation"] = correlation
    seg_kind = {}
    for photo in photos:
        seg_kind.setdefault(photo["seg"], photo["kind"])
    frame_rows = np.arange(len(rows)) + offset
    inside = (frame_rows >= 0) & (frame_rows < len(frames["seg"]))
    row_seg = np.where(inside, frames["seg"][np.clip(frame_rows, 0, len(frames["seg"]) - 1)], np.nan)
    row_kind = np.array([seg_kind.get(int(seg), "other") if math.isfinite(seg) else "outside" for seg in row_seg])
    per_kind = {}
    for kind in ("glide", "stare", "settle"):
        selector = row_kind == kind
        per_kind[kind] = {"rows": int(selector.sum())}
        for name in PROFILE_TIME_COLUMNS:
            values = columns.get(name)
            if values is None or not selector.any():
                continue
            per_kind[kind][name] = {
                "p50": percentile_or_none(values[selector], 50),
                "p95": percentile_or_none(values[selector], 95),
                "p99": percentile_or_none(values[selector], 99),
            }
    summary["by_kind"] = per_kind
    streaming = {}
    for name in PROFILE_STREAMING_COLUMNS:
        values = columns.get(name)
        if values is None:
            continue
        finite = values[np.isfinite(values)]
        streaming[name] = {
            "min": float(finite.min()) if finite.size else None,
            "mean": float(finite.mean()) if finite.size else None,
            "max": float(finite.max()) if finite.size else None,
        }
    summary["streaming"] = streaming
    pending = columns.get(PROFILE_PENDING_COLUMN)
    if pending is not None:
        weights = np.where(np.isfinite(frame_time), frame_time, 0.0)
        busy = np.nan_to_num(pending, nan=0.0) > params["pending_threshold"]
        total = float(weights.sum())
        summary["pending_busy_time_ratio"] = float(weights[busy].sum() / total) if total > 0 else None
        summary["pending_busy_longest_s"] = longest_run_seconds(busy, weights)
        settle_by_seg = {}
        for seg in sorted({int(s) for s in row_seg[np.isfinite(row_seg)] if seg_kind.get(int(s)) == "stare"}):
            indices = np.flatnonzero(row_seg == seg)
            elapsed = 0.0
            found = None
            for position in indices:
                if not busy[position]:
                    found = elapsed
                    break
                elapsed += weights[position] / 1000.0
            settle_by_seg[str(seg)] = found
        summary["pending_clear_after_stare_start_s"] = settle_by_seg
    return summary


def summarize_gpu(path):
    memory = []
    utilization = []
    with open(path, encoding="utf-8-sig") as handle:
        for line in handle:
            parts = [part.strip() for part in line.split(",")]
            if len(parts) < 3:
                continue
            used = to_float(parts[-2])
            busy = to_float(parts[-1])
            if math.isfinite(used):
                memory.append(used)
            if math.isfinite(busy):
                utilization.append(busy)
    return {
        "samples": len(memory),
        "memory_used_mib_max": float(max(memory)) if memory else None,
        "memory_used_mib_mean": float(np.mean(memory)) if memory else None,
        "utilization_mean": float(np.mean(utilization)) if utilization else None,
    }


def summarize_events(events, photos, params):
    areas = [event["area_m2"] for event in events]
    lates = [event["late_s"] for event in events]
    glide_seconds = 0.0
    for seg in {photo["seg"] for photo in photos if photo["kind"] == "glide"}:
        reals = [photo["real"] for photo in photos if photo["kind"] == "glide" and photo["seg"] == seg]
        glide_seconds += max(reals) - min(reals)
    by_zone = {label: 0 for label in zone_labels() + ["화면 밖"]}
    by_distance = {label: 0 for _, _, label in DISTANCE_BINS}
    for event in events:
        by_zone[event.get("zone") or "화면 밖"] += 1
        by_distance[distance_label(event["distance_cm"])] += 1
    return {
        "count": len(events),
        "glide_seconds": glide_seconds,
        "per_minute": float(len(events) / (glide_seconds / 60.0)) if glide_seconds > 0 else None,
        "area_m2_total": float(sum(areas)),
        "late_median_s": percentile_or_none(lates, 50),
        "late_p90_s": percentile_or_none(lates, 90),
        "by_zone": by_zone,
        "by_distance": by_distance,
    }


def load_segment_kinds(run_dir):
    with open(run_dir / "plan.json", encoding="utf-8") as handle:
        plan = json.load(handle)
    return {int(segment["index"]): segment["kind"] for segment in plan.get("segments", [])}


def build_grab_photo_list(run_dir, frames, params):
    kinds = load_segment_kinds(run_dir)
    epochs = frames["real_epoch"]
    segments = frames["seg"]
    segment_starts = {}
    for row, segment in enumerate(segments):
        if math.isfinite(segment) and int(segment) not in segment_starts:
            segment_starts[int(segment)] = float(epochs[row])
    photos = []
    missing = []
    with open(run_dir / "grabs.csv", newline="", encoding="utf-8") as handle:
        for entry in csv.DictReader(handle):
            number = int(entry["n"])
            path = Path(entry["path"])
            if not path.is_file():
                missing.append(number)
                continue
            middle = 0.5 * (float(entry["epoch_start"]) + float(entry["epoch_end"]))
            row = int(np.searchsorted(epochs, middle - params["grab_latency_base_s"], side="right")) - 1
            if row < 0 or middle > epochs[-1]:
                continue
            segment = int(segments[row])
            photos.append({
                "n": number, "path": path, "row": row, "real": middle, "seg": segment,
                "kind": kinds.get(segment, "other"), "seg_time": middle - segment_starts.get(segment, middle),
            })
    return photos, missing


def load_run_inputs(run_dir, params):
    frames = load_frames_table(run_dir / "frames.csv")
    with open(run_dir / "result.json", encoding="utf-8") as handle:
        result = json.load(handle)
    if (run_dir / "grabs.csv").is_file():
        photos, missing = build_grab_photo_list(run_dir, frames, params)
        params["pose_offset_candidates"] = list(params["grab_offset_candidates"])
        params["capture_mode"] = "external"
    else:
        photos, missing = build_photo_list(run_dir, result, frames)
        params["capture_mode"] = "engine"
    height_path = run_dir / "height.r16"
    if not height_path.is_file():
        height_path = Path(params["height_path"])
    height_field = HeightField.load(height_path)
    return frames, result, photos, missing, height_field, height_path


def build_noise_summary(walk_values, stare_values, suggested_threshold, suggested_count, params):
    return {
        "walk": quantile_table(walk_values, params),
        "stare": quantile_table(stare_values, params),
        "t_pop_used": params["t_pop"],
        "t_pop_suggested": suggested_threshold,
        "events_at_suggested": suggested_count,
    }


def suggest_pop_threshold(stare_values, params):
    table = quantile_table(stare_values, params)
    stare_p95 = table.get("p95")
    if stare_p95 is None:
        return None
    return float(max(params["t_pop_floor"], params["t_pop_suggest_factor"] * stare_p95))


def analyze_run(run_dir, params):
    started = time.perf_counter()
    frames, result, photos, missing, height_field, height_path = load_run_inputs(run_dir, params)
    workers = int(params["analysis_workers"])
    offset_info = choose_pose_offset(photos, frames, height_field, params, workers)
    offset = offset_info["chosen"]
    stare, stare_values = analyze_stare_segments(photos, frames, height_field, params, offset, workers)
    suggested_threshold = suggest_pop_threshold(stare_values, params)
    extra_thresholds = {} if suggested_threshold is None else {"suggested": suggested_threshold}
    events, registration, walk_values, extra_totals, rejected_total = analyze_pop_chain(photos, frames, height_field, params, offset, workers, extra_thresholds)
    for number, event in enumerate(events, start=1):
        event["number"] = number
    noise = build_noise_summary(walk_values, stare_values, suggested_threshold, extra_totals.get("suggested"), params)
    warnings = []
    registration_stats = {
        "pairs": len(registration),
        "median_cm": percentile_or_none(registration, 50),
        "p90_cm": percentile_or_none(registration, 90),
        "max_cm": float(max(registration)) if registration else None,
    }
    if registration_stats["median_cm"] is not None and registration_stats["median_cm"] > params["max_registration_cm"]:
        warnings.append(f"등록 잔차 중앙값 {registration_stats['median_cm']:.1f}cm 가 한도 {params['max_registration_cm']}cm 를 넘음: 사진↔자세 대응이 틀렸을 수 있음")
    if missing:
        warnings.append(f"없는 사진 {len(missing)}장: {missing[:10]}")
    if not photos:
        warnings.append("읽을 수 있는 사진이 없음")
    analysis = {
        "run": run_dir.name,
        "run_dir": str(run_dir),
        "height_path": str(height_path),
        "cvars": result.get("cvars"),
        "photo_count": len(photos),
        "missing_photos": missing,
        "pose_offset": offset_info,
        "pose_artifacts_rejected": rejected_total,
        "registration": registration_stats,
        "events": events,
        "event_summary": summarize_events(events, photos, params),
        "noise": noise,
        "stare": stare,
        "warnings": warnings,
        "params": params,
    }
    profile_path = run_dir / "profile.csv"
    if profile_path.is_file():
        analysis["profile"] = summarize_profile(profile_path, frames, photos, params)
    gpu_path = run_dir / "gpu.csv"
    if gpu_path.is_file():
        analysis["gpu"] = summarize_gpu(gpu_path)
    write_sheets(run_dir, photos, frames, offset, events, stare, params)
    write_pop_map(run_dir / "sheets" / "pop_map.png", frames, photos, events, params)
    analysis["analysis_seconds"] = time.perf_counter() - started
    analysis["seconds_per_photo"] = analysis["analysis_seconds"] / max(1, len(photos))
    write_json(run_dir / "analysis.json", analysis)
    write_summary_markdown(run_dir / "summary.md", analysis)
    return analysis


def get_font():
    try:
        return ImageFont.load_default()
    except OSError:
        return None


def fit_width(image, width):
    height = max(1, int(round(image.height * width / image.width)))
    return image.resize((width, height), Image.BILINEAR)


def draw_label(image, text):
    draw = ImageDraw.Draw(image)
    draw.rectangle([0, 0, image.width, 14], fill=(0, 0, 0))
    draw.text((3, 1), text, fill=(255, 255, 255), font=get_font())


def event_screen_rectangle(event, pose, image_size, height_field, params):
    half = params["block_cm"] / 2.0
    xs = [(cell[0] + 0.5) * params["block_cm"] for cell in event["block_cells"]]
    ys = [(cell[1] + 0.5) * params["block_cm"] for cell in event["block_cells"]]
    corners_x = []
    corners_y = []
    for x, y in zip(xs, ys):
        for dx, dy in ((-half, -half), (half, -half), (-half, half), (half, half)):
            corners_x.append(x + dx)
            corners_y.append(y + dy)
    corner_x = np.array(corners_x)
    corner_y = np.array(corners_y)
    corner_z = height_field.sample(corner_x, corner_y)
    u, v, depth = project_points(pose, corner_x, corner_y, corner_z, image_size[0], image_size[1])
    visible = depth > params["depth_min_cm"]
    if not visible.any():
        return None
    return u[visible].min(), v[visible].min(), u[visible].max(), v[visible].max()


def write_sheets(run_dir, photos, frames, offset, events, stare, params):
    sheet_dir = run_dir / "sheets"
    sheet_dir.mkdir(parents=True, exist_ok=True)
    for stale in sheet_dir.glob("pop_[0-9]*.png"):
        stale.unlink()
    height_field = HeightField.load(run_dir / "height.r16") if (run_dir / "height.r16").is_file() else HeightField.load(Path(params["height_path"]))
    index_of_number = {photo["n"]: index for index, photo in enumerate(photos)}
    width = params["sheet_photo_width"]
    for event in events[: params["sheet_event_limit"]]:
        center = index_of_number[event["photo"]]
        tiles = []
        for index in (center - 1, center, center + 1):
            photo = photos[index]
            image = Image.open(photo["path"]).convert("RGB")
            pose = pose_at_row(frames, photo["row"], offset)
            rectangle = event_screen_rectangle(event, pose, image.size, height_field, params)
            if rectangle is not None:
                ImageDraw.Draw(image).rectangle(rectangle, outline=(255, 0, 0), width=3)
            tile = fit_width(image, width)
            scale = width / image.width
            if rectangle is not None:
                ImageDraw.Draw(tile).rectangle([rectangle[0] * scale, rectangle[1] * scale, rectangle[2] * scale, rectangle[3] * scale], outline=(255, 0, 0), width=2)
            draw_label(tile, f"shot {photo['n']}")
            tiles.append(tile)
        sheet = Image.new("RGB", (width * 3, tiles[0].height + 16), (0, 0, 0))
        for column, tile in enumerate(tiles):
            sheet.paste(tile, (column * width, 16))
        detail = event["detail_ratio"]
        draw_label(sheet, f"event {event['number']} | shot {event['photo']} | t {event['real_s']:.2f}s | {event['area_m2']:.2f} m2 | D {event['mean_d']:.3f} | detail {detail if detail is None else round(detail, 2)} | late {event['late_s']:.2f}s")
        sheet.save(sheet_dir / f"pop_{event['number']:03d}.png")
    for segment in stare:
        numbers = segment["photo_numbers"]
        tiles = []
        for number in numbers:
            photo = photos[index_of_number[number]]
            tile = fit_width(Image.open(photo["path"]).convert("RGB"), 320)
            draw_label(tile, f"shot {number} t {photo['seg_time']}")
            tiles.append(tile)
        if not tiles:
            continue
        per_row = 4
        rows = math.ceil(len(tiles) / per_row)
        sheet = Image.new("RGB", (320 * min(per_row, len(tiles)), tiles[0].height * rows), (0, 0, 0))
        for position, tile in enumerate(tiles):
            sheet.paste(tile, ((position % per_row) * 320, (position // per_row) * tile.height))
        sheet.save(sheet_dir / f"stare_{segment['seg']}.png")


def write_pop_map(path, frames, photos, events, params):
    if not photos:
        return None
    first_row = min(photo["row"] for photo in photos)
    last_row = max(photo["row"] for photo in photos)
    path_x = frames["pawn_x"][first_row:last_row + 1]
    path_y = frames["pawn_y"][first_row:last_row + 1]
    finite = np.isfinite(path_x) & np.isfinite(path_y)
    if not finite.any():
        return None
    path_x = path_x[finite]
    path_y = path_y[finite]
    block = params["block_cm"]
    cells = np.array([cell for event in events for cell in event["block_cells"]], dtype=np.float64).reshape(-1, 2)
    cell_x = (cells[:, 0] + 0.5) * block
    cell_y = (cells[:, 1] + 0.5) * block
    all_x = np.concatenate([path_x, cell_x])
    all_y = np.concatenate([path_y, cell_y])
    margin = params["map_margin_cm"]
    x_min = float(all_x.min() - margin)
    y_min = float(all_y.min() - margin)
    x_max = float(all_x.max() + margin)
    y_max = float(all_y.max() + margin)
    pixel_cm = float(params["map_pixel_cm"])
    extent = max(x_max - x_min, y_max - y_min)
    if extent / pixel_cm > params["map_max_pixels"]:
        pixel_cm = extent / params["map_max_pixels"]
    width = int(math.ceil((x_max - x_min) / pixel_cm))
    height = int(math.ceil((y_max - y_min) / pixel_cm))
    image = np.full((height, width, 3), 28, dtype=np.uint8)
    grid_cm = 1000.0
    for x in np.arange(math.ceil(x_min / grid_cm) * grid_cm, x_max, grid_cm):
        image[:, min(width - 1, int((x - x_min) / pixel_cm))] = (58, 58, 58)
    for y in np.arange(math.ceil(y_min / grid_cm) * grid_cm, y_max, grid_cm):
        image[min(height - 1, int((y - y_min) / pixel_cm)), :] = (58, 58, 58)
    top_count = 0
    if len(cells):
        cell_cm = params["map_cell_cm"]
        keys = np.stack([np.floor(cell_x / cell_cm), np.floor(cell_y / cell_cm)], axis=1).astype(np.int64)
        unique, counts = np.unique(keys, axis=0, return_counts=True)
        top_count = int(counts.max())
        scale = max(top_count, 4)
        for (key_x, key_y), count in zip(unique, counts):
            left = int((key_x * cell_cm - x_min) / pixel_cm)
            right = max(left + 1, int(((key_x + 1) * cell_cm - x_min) / pixel_cm))
            upper = int((key_y * cell_cm - y_min) / pixel_cm)
            lower = max(upper + 1, int(((key_y + 1) * cell_cm - y_min) / pixel_cm))
            heat = min(1.0, count / scale)
            image[upper:lower, left:right] = (255, int(230 * (1.0 - heat)), 0)
    picture = Image.fromarray(image)
    draw = ImageDraw.Draw(picture)
    points = [((x - x_min) / pixel_cm, (y - y_min) / pixel_cm) for x, y in zip(path_x, path_y)]
    if len(points) >= 2:
        draw.line(points, fill=(0, 200, 255), width=2)
    draw.ellipse([points[0][0] - 5, points[0][1] - 5, points[0][0] + 5, points[0][1] + 5], outline=(0, 255, 0), width=2)
    draw.ellipse([points[-1][0] - 5, points[-1][1] - 5, points[-1][0] + 5, points[-1][1] + 5], outline=(255, 255, 255), width=2)
    draw_label(picture, f"x {x_min / 100:.0f}..{x_max / 100:.0f}m (right), y {y_min / 100:.0f}..{y_max / 100:.0f}m (down) | 1px={pixel_cm:.0f}cm | cell {params['map_cell_cm'] / 100:.0f}m | events {len(events)} | max cell {top_count} blocks | green=start white=end cyan=path")
    path.parent.mkdir(parents=True, exist_ok=True)
    picture.save(path)
    return path


def format_number(value, digits=2):
    return "-" if value is None else f"{value:.{digits}f}"


def write_summary_markdown(path, analysis):
    events = analysis["events"]
    event_summary = analysis["event_summary"]
    lines = [f"# 바닥 팝핑 분석: {analysis['run']}", ""]
    lines.append(f"- 사진 {analysis['photo_count']}장을 분석했고, 사진 한 장당 분석 시간은 {analysis['seconds_per_photo']:.2f}초였습니다.")
    chosen = analysis["pose_offset"]["chosen"]
    lines.append(f"- 사진과 카메라 자세를 맞추는 어긋남(행)은 {chosen:+d}이 가장 잘 맞았습니다(후보별 차이 중앙값: {analysis['pose_offset']['candidates']}).")
    registration = analysis["registration"]
    lines.append(f"- 같은 땅을 두 사진에서 겹쳐 볼 때 남는 어긋남 중앙값은 {format_number(registration['median_cm'], 1)}cm입니다(작을수록 자세가 맞음).")
    if analysis.get("cvars"):
        lines.append(f"- 바꾼 콘솔 변수: {analysis['cvars']}")
    for warning in analysis["warnings"]:
        lines.append(f"- 주의: {warning}")
    lines += ["", "## 한눈에 보기 (같은 땅이 갑자기 바뀐 순간 = 팝)", "", "| 항목 | 값 |", "|---|---|"]
    lines.append(f"| 팝 사건 수 | {event_summary['count']} |")
    lines.append(f"| 걷는 동안 분당 사건 수 | {format_number(event_summary['per_minute'], 1)} |")
    lines.append(f"| 바뀐 땅 넓이 합(m²) | {format_number(event_summary['area_m2_total'])} |")
    lines.append(f"| 보이기 시작한 뒤 바뀌기까지 걸린 시간 중앙값(초) | {format_number(event_summary['late_median_s'])} |")
    lines.append(f"| 같은 시간 90% 지점(초) | {format_number(event_summary['late_p90_s'])} |")
    lines += ["", "## 화면 구역별·카메라 거리별 사건 수", "", "| 화면 구역(사진 기준) | 사건 수 |", "|---|---|"]
    for label, count in event_summary["by_zone"].items():
        lines.append(f"| {label} | {count} |")
    lines += ["", "| 카메라에서 수평 거리 | 사건 수 |", "|---|---|"]
    for label, count in event_summary["by_distance"].items():
        lines.append(f"| {label} | {count} |")
    noise = analysis.get("noise")
    if noise:
        lines += ["", "## 잡음 보정 자료(이웃한 두 사진의 블록 차이 D 분위수)", "", "| 상황 | 블록 수 | 50% | 90% | 95% | 99% |", "|---|---|---|---|---|---|"]
        for label, key in (("걷는 중 연속 쌍", "walk"), ("멈춰 선 중 연속 쌍", "stare")):
            table = noise[key]
            lines.append(f"| {label} | {table['blocks']} | {format_number(table.get('p50'), 4)} | {format_number(table.get('p90'), 4)} | {format_number(table.get('p95'), 4)} | {format_number(table.get('p99'), 4)} |")
        lines.append("")
        lines.append(f"- 지금 쓴 팝 기준 T_pop = {format_number(noise['t_pop_used'], 3)}, 제안값 t_pop_suggested = max(0.06, 2 × 멈춰 선 중 95%) = {format_number(noise['t_pop_suggested'], 3)}")
        lines.append(f"- 제안값으로 다시 센 사건 수(참고만, 판정은 그대로): {noise['events_at_suggested'] if noise['events_at_suggested'] is not None else '-'}")
    if events:
        lines += ["", "## 팝 사건 목록(앞 20개)", "", "| 번호 | 사진 | 시각(초) | 땅 위치 x,y(cm) | 넓이(m²) | 평균 차이 | 화면 위치(가로,세로) | 구역 | 거리(m) | 선명해진 정도(1보다 크면 선명) | 보인 지 얼마 뒤(초) |", "|---|---|---|---|---|---|---|---|---|---|---|"]
        for event in events[:20]:
            screen = "-" if event["screen"] is None else f"{event['screen'][0]:.2f},{event['screen'][1]:.2f}"
            lines.append(f"| {event['number']} | {event['photo']} | {event['real_s']:.2f} | {event['center_cm'][0]:.0f},{event['center_cm'][1]:.0f} | {event['area_m2']:.2f} | {event['mean_d']:.3f} | {screen} | {event.get('zone') or '-'} | {event['distance_cm'] / 100.0:.1f} | {format_number(event['detail_ratio'])} | {event['late_s']:.2f}{'+' if event['late_is_lower_bound'] else ''} |")
    if analysis["stare"]:
        lines += ["", "## 멈춰 서서 본 뒤 그림이 다 자리 잡기까지(응시 정착 시간)", "", "| 구간 | 정착 시간(초) | 바닥 잡음(차이) | 비교한 사진 수 |", "|---|---|---|---|"]
        for segment in analysis["stare"]:
            lines.append(f"| {segment['seg']} | {format_number(segment['settle_time_s'])} | {format_number(segment['noise_floor_d'], 4)} | {len(segment['photos'])} |")
    profile = analysis.get("profile")
    if profile and profile.get("by_kind"):
        lines += ["", "## 프레임·GPU 시간(밀리초)", "", "| 구간 종류 | 항목 | 중앙 | 95% | 99% |", "|---|---|---|---|---|"]
        for kind, stats in profile["by_kind"].items():
            for name in PROFILE_TIME_COLUMNS:
                if name in stats:
                    lines.append(f"| {kind} | {name} | {format_number(stats[name]['p50'])} | {format_number(stats[name]['p95'])} | {format_number(stats[name]['p99'])} |")
    if analysis.get("gpu"):
        gpu = analysis["gpu"]
        lines += ["", "## 그래픽 메모리(MiB)", "", "| 최대 | 평균 |", "|---|---|", f"| {format_number(gpu['memory_used_mib_max'], 0)} | {format_number(gpu['memory_used_mib_mean'], 0)} |"]
    if profile and profile.get("streaming"):
        lines += ["", "## 텍스처 스트리밍", "", "| 항목 | 최소 | 평균 | 최대 |", "|---|---|---|---|"]
        for name, stats in profile["streaming"].items():
            lines.append(f"| {name} | {format_number(stats['min'])} | {format_number(stats['mean'])} | {format_number(stats['max'])} |")
        lines.append("")
        lines.append(f"- 대기 중인 스트리밍 자료가 있던 시간 비율: {format_number(profile.get('pending_busy_time_ratio'), 3)}, 가장 길게 이어진 시간: {format_number(profile.get('pending_busy_longest_s'))}초")
        lines.append(f"- 응시 시작부터 대기 자료가 비기까지(초, 구간별): {profile.get('pending_clear_after_stare_start_s')}")
        lines.append(f"- 측정표와 프레임 기록 맞춤: 행 어긋남 {profile.get('align_offset_rows')}, 상관 {format_number(profile.get('align_correlation'), 3)}")
    if analysis["events"]:
        lines += ["", "그림은 sheets 폴더의 pop_*.png(사진 앞·사건·뒤를 나란히, 빨간 네모가 바뀐 땅)와 pop_map.png(위에서 본 사건 지도)에 있습니다."]
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")


def resolve_run_dir(value):
    candidate = Path(value)
    if candidate.is_dir():
        return candidate.resolve()
    named = FLOOR_POPPING_ROOT / value
    if named.is_dir():
        return named
    raise FileNotFoundError(f"실행 폴더를 찾지 못함: {value}")


def load_analysis(run_dir, params):
    path = run_dir / "analysis.json"
    if path.is_file():
        with open(path, encoding="utf-8") as handle:
            return json.load(handle)
    return json.loads(json.dumps(clean_for_json(analyze_run(run_dir, params)), default=json_default))


def load_run_photo_set(run_dir, analysis, params):
    run_params = dict(params)
    frames, result, photos, missing, height_field, height_path = load_run_inputs(run_dir, run_params)
    events_by_photo = {}
    for event in analysis.get("events", []):
        events_by_photo.setdefault(event["photo"], []).append(event)
    return {
        "name": run_dir.name,
        "photos": photos,
        "frames": frames,
        "height_field": height_field,
        "offset": int(nested_get(analysis, ["pose_offset", "chosen"]) or 0),
        "events_by_photo": events_by_photo,
        "params": run_params,
    }


def nested_get(payload, keys):
    for key in keys:
        if not isinstance(payload, dict) or key not in payload:
            return None
        payload = payload[key]
    return payload


def photo_ground_position(run_set, photo):
    row = photo["row"]
    return np.array([run_set["frames"]["pawn_x"][row], run_set["frames"]["pawn_y"][row]])


def pair_photos_by_path(run_a, run_b):
    candidates_b = {}
    for photo in run_b["photos"]:
        if photo["kind"] == "glide":
            candidates_b.setdefault(photo["seg"], []).append((photo, photo_ground_position(run_b, photo)))
    pairs = []
    for photo in run_a["photos"]:
        if photo["kind"] != "glide":
            continue
        candidates = candidates_b.get(photo["seg"])
        if not candidates:
            continue
        position = photo_ground_position(run_a, photo)
        best_photo, best_position = min(candidates, key=lambda item: float(np.hypot(*(item[1] - position))))
        pairs.append((photo, best_photo, float(np.hypot(*(best_position - position)))))
    return pairs


def load_marked_photo(run_set, photo, params):
    image = Image.open(photo["path"]).convert("RGB")
    pose = pose_at_row(run_set["frames"], photo["row"], run_set["offset"])
    draw = ImageDraw.Draw(image)
    for event in run_set["events_by_photo"].get(photo["n"], []):
        rectangle = event_screen_rectangle(event, pose, image.size, run_set["height_field"], params)
        if rectangle is not None:
            draw.rectangle(rectangle, outline=(255, 0, 0), width=6)
    return image


def write_compare_stare(output_dir, run_a, run_b, params):
    def stare_groups(run_set):
        groups = {}
        for photo in run_set["photos"]:
            if photo["kind"] == "stare":
                groups.setdefault(photo["seg"], []).append(photo)
        return [groups[key] for key in sorted(groups)]

    written = []
    for position, (group_a, group_b) in enumerate(zip(stare_groups(run_a), stare_groups(run_b))):
        early_a = [p for p in group_a if p["seg_time"] is None or p["seg_time"] <= params["compare_stare_seconds"]]
        early_b = [p for p in group_b if p["seg_time"] is None or p["seg_time"] <= params["compare_stare_seconds"]]
        count = min(len(early_a), len(early_b), 8)
        if count == 0:
            continue
        tile_width = 320
        tiles_a = [fit_width(Image.open(p["path"]).convert("RGB"), tile_width) for p in early_a[:count]]
        tiles_b = [fit_width(Image.open(p["path"]).convert("RGB"), tile_width) for p in early_b[:count]]
        sheet = Image.new("RGB", (tile_width * count, tiles_a[0].height * 2), (0, 0, 0))
        for column in range(count):
            draw_label(tiles_a[column], f"A t={early_a[column]['seg_time']:.2f}")
            draw_label(tiles_b[column], f"B t={early_b[column]['seg_time']:.2f}")
            sheet.paste(tiles_a[column], (column * tile_width, 0))
            sheet.paste(tiles_b[column], (column * tile_width, tiles_a[0].height))
        target = output_dir / f"compare_stare_{position}.png"
        sheet.save(target)
        written.append(target)
    return written


def write_compare_glide(output_dir, run_a, run_b, params):
    pairs = pair_photos_by_path(run_a, run_b)
    if not pairs:
        return None, 0, None
    limit = params["compare_max_frames"]
    if len(pairs) > limit:
        picks = np.linspace(0, len(pairs) - 1, limit).round().astype(int)
        pairs = [pairs[position] for position in picks]
    half_width = params["compare_frame_width"] // 2
    frames = []
    for photo_a, photo_b, gap_cm in pairs:
        left = fit_width(load_marked_photo(run_a, photo_a, params), half_width)
        right = fit_width(load_marked_photo(run_b, photo_b, params), half_width)
        frame = Image.new("RGB", (half_width * 2, left.height))
        frame.paste(left, (0, 0))
        frame.paste(right.resize(left.size), (half_width, 0))
        draw_label(frame, f"A shot {photo_a['n']} (left) | B shot {photo_b['n']} (right) | path gap {gap_cm:.0f} cm | red = pop blocks")
        frames.append(frame)
    target = output_dir / "compare_glide.webp"
    frames[0].save(target, save_all=True, append_images=frames[1:], duration=int(round(1000 / params["compare_fps"])), loop=0, quality=70, method=4)
    gaps = [gap for _, _, gap in pairs]
    return target, len(pairs), float(np.median(gaps))


def append_compare_table(lines, title, headers, rows, analysis_a, analysis_b):
    lines += ["", f"## {title}", "", f"| {headers} | A | B | B − A |", "|---|---|---|---|"]
    for label, keys, digits in rows:
        value_a = nested_get(analysis_a, keys)
        value_b = nested_get(analysis_b, keys)
        gap = None if value_a is None or value_b is None else value_b - value_a
        lines.append(f"| {label} | {format_number(value_a, digits)} | {format_number(value_b, digits)} | {format_number(gap, digits)} |")


def run_compare(name_a, name_b, params, output_root=None):
    dir_a = resolve_run_dir(name_a)
    dir_b = resolve_run_dir(name_b)
    analysis_a = load_analysis(dir_a, params)
    analysis_b = load_analysis(dir_b, params)
    root = output_root if output_root is not None else FLOOR_POPPING_ROOT
    output_dir = root / f"compare_{dir_a.name}__{dir_b.name}"
    output_dir.mkdir(parents=True, exist_ok=True)
    lines = [f"# 비교: A = {dir_a.name}, B = {dir_b.name}"]
    append_compare_table(lines, "팝 사건", "항목", [
        ("팝 사건 수", ["event_summary", "count"], 0),
        ("분당 사건 수", ["event_summary", "per_minute"], 1),
        ("바뀐 땅 넓이 합(m²)", ["event_summary", "area_m2_total"], 2),
        ("보인 지 얼마 뒤 중앙값(초)", ["event_summary", "late_median_s"], 2),
        ("보인 지 얼마 뒤 90%(초)", ["event_summary", "late_p90_s"], 2),
        ("자세 어긋남으로 걸러낸 후보 수", ["pose_artifacts_rejected"], 0),
        ("사진↔자세 어긋남(행)", ["pose_offset", "chosen"], 0),
        ("등록 잔차 중앙값(cm)", ["registration", "median_cm"], 1),
    ], analysis_a, analysis_b)
    append_compare_table(lines, "화면 구역별 사건 수(사진 기준)", "구역", [(label, ["event_summary", "by_zone", label], 0) for label in zone_labels() + ["화면 밖"]], analysis_a, analysis_b)
    append_compare_table(lines, "카메라 거리별 사건 수", "수평 거리", [(label, ["event_summary", "by_distance", label], 0) for _, _, label in DISTANCE_BINS], analysis_a, analysis_b)
    append_compare_table(lines, "잡음 분위수(블록 차이 D)", "항목", [
        (f"{title} {percent}%", ["noise", key, f"p{percent}"], 4)
        for title, key in (("걷는 중", "walk"), ("멈춰 선 중", "stare")) for percent in (50, 90, 95, 99)
    ] + [("제안 T_pop", ["noise", "t_pop_suggested"], 3), ("제안값으로 센 사건 수", ["noise", "events_at_suggested"], 0)], analysis_a, analysis_b)
    stare_a = {str(item["seg"]): item for item in analysis_a.get("stare", [])}
    stare_b = {str(item["seg"]): item for item in analysis_b.get("stare", [])}
    lines += ["", "## 응시 정착 시간(초)", "", "| 구간 | A | B | B − A |", "|---|---|---|---|"]
    for seg in sorted(set(stare_a) | set(stare_b)):
        value_a = stare_a.get(seg, {}).get("settle_time_s")
        value_b = stare_b.get(seg, {}).get("settle_time_s")
        gap = None if value_a is None or value_b is None else value_b - value_a
        lines.append(f"| {seg} | {format_number(value_a)} | {format_number(value_b)} | {format_number(gap)} |")
    frame_rows = [
        (f"{kind} {name} {stat}(ms)", ["profile", "by_kind", kind, name, stat], 2)
        for kind in ("glide", "stare") for name in ("FrameTime", "GPUTime") for stat in ("p50", "p95", "p99")
    ]
    append_compare_table(lines, "프레임·GPU 시간", "항목", frame_rows + [
        ("그래픽 메모리 최대(MiB)", ["gpu", "memory_used_mib_max"], 0),
        ("그래픽 메모리 평균(MiB)", ["gpu", "memory_used_mib_mean"], 0),
    ], analysis_a, analysis_b)
    append_compare_table(lines, "텍스처 스트리밍", "항목", [
        ("스트리밍 대기 시간 비율", ["profile", "pending_busy_time_ratio"], 3),
        ("스트리밍 대기 가장 긴 시간(초)", ["profile", "pending_busy_longest_s"], 2),
        ("StreamingPool 평균", ["profile", "streaming", "StreamingPool", "mean"], 1),
        ("StreamingPool 최대", ["profile", "streaming", "StreamingPool", "max"], 1),
        ("WantedMips 평균", ["profile", "streaming", "WantedMips", "mean"], 1),
        ("CachedMips 평균", ["profile", "streaming", "CachedMips", "mean"], 1),
        ("PendingStreamInData 최대", ["profile", "streaming", "PendingStreamInData", "max"], 2),
        ("DesiredDataLoadedPercent 최소", ["profile", "streaming", "DesiredDataLoadedPercent", "min"], 2),
    ], analysis_a, analysis_b)
    run_set_a = load_run_photo_set(dir_a, analysis_a, params)
    run_set_b = load_run_photo_set(dir_b, analysis_b, params)
    webp, pair_count, median_gap = write_compare_glide(output_dir, run_set_a, run_set_b, params)
    stare_images = write_compare_stare(output_dir, run_set_a, run_set_b, params)
    lines += [
        "",
        f"- 움직이는 그림: {webp.name if webp else '없음'} (같은 경로 위치끼리 짝지음 {pair_count}쌍, 짝 사이 폰 위치 차이 중앙값 {format_number(median_gap, 0)}cm, 빨간 테두리 = 그 사진에서 팝으로 판정된 블록)",
        f"- 응시 첫 1초 나란히: {[p.name for p in stare_images]}",
    ]
    with open(output_dir / "compare.md", "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    return output_dir


SYNTH_FPS = 60.0
SYNTH_SPEED_CM_S = 550.0
SYNTH_SHOT_HZ = 8.0
SYNTH_GLIDE_START_S = 1.0
SYNTH_GLIDE_LENGTH_CM = 3000.0
SYNTH_STARE_SHOT_TIMES = (0.25, 1.0, 2.0, 4.0)
SYNTH_ARM_CM = 800.0
SYNTH_PITCH_DEG = -60.0
SYNTH_FOV_DEG = 90.0
SYNTH_PAWN_OFFSET_CM = 88.0
SYNTH_PAWN_RADIUS_CM = 40.0
SYNTH_PAWN_HEIGHT_CM = 180.0
SYNTH_Z_BOUND_CM = 75.0
SYNTH_SCENE = {"x0": -3800.0, "y0": -1700.0, "texel": 4.0, "nx": 1300, "ny": 850}
SYNTH_TEXTURE_OCTAVES = ((400.0, 0.10), (150.0, 0.09), (60.0, 0.08), (28.0, 0.10), (16.0, 0.17), (10.0, 0.18))
SYNTH_BLUR_SIGMA_CM = 20.0
SYNTH_TINTS = {"green": (0.15, 0.55, 0.12), "purple": (0.65, 0.25, 0.75)}
SYNTH_TINT_WEIGHT = 0.55
SYNTH_CELL_BLUR = (-1500.0, -1300.0, 200.0, 400.0)
SYNTH_CELL_COLOR = (-2300.0, -2100.0, -400.0, -200.0)
SYNTH_CELL_STARE = (-3000.0, -2700.0, -500.0, -200.0)
SYNTH_BOX_COUNT = 12
SYNTH_BOX_REGION = (-2800.0, -400.0, 150.0, 700.0)
SYNTH_BOX_SINK_CM = 80.0
SYNTH_BOX_LOD_CENTER = (-1100.0, -450.0)
SYNTH_BOX_LOD_HEIGHT_CM = 90.0
SYNTH_BOX_LOD_SHOT = 20
SYNTH_WIND_CELL_COUNT = 6
SYNTH_WIND_CELL_SIZE_CM = 200.0
SYNTH_WIND_PIXEL_CM = 2.5
SYNTH_WIND_GAIN = 0.04
SYNTH_NOISE_GLIDE_SHOTS = 26
SYNTH_FRAME_BASE = 1000
SYNTH_PROFILE_ROW_OFFSET = 7


def synthetic_terrain(x, y):
    return 60.0 * np.sin(x / 1700.0) * np.cos(y / 2300.0)


def build_scene_files(scene_dir):
    scene_dir.mkdir(parents=True, exist_ok=True)
    nx, ny, texel = SYNTH_SCENE["nx"], SYNTH_SCENE["ny"], SYNTH_SCENE["texel"]
    rng = np.random.default_rng(20261120)
    texture = np.zeros((3, nx, ny), dtype=np.float32)
    for sigma_cm, weight in SYNTH_TEXTURE_OCTAVES:
        for channel in range(3):
            noise = ndimage.gaussian_filter(rng.standard_normal((nx, ny)).astype(np.float32), sigma_cm / texel)
            texture[channel] += weight * noise / noise.std()
    texture = np.clip(0.45 + texture, 0.0, 1.0)
    blurred = np.stack([ndimage.gaussian_filter(texture[channel], SYNTH_BLUR_SIGMA_CM / texel) for channel in range(3)])
    np.save(scene_dir / "texture.npy", texture)
    np.save(scene_dir / "texture_blur.npy", blurred)


_SCENE_CACHE = {}


def load_scene(scene_dir):
    key = str(scene_dir)
    if key not in _SCENE_CACHE:
        _SCENE_CACHE[key] = (np.load(Path(scene_dir) / "texture.npy"), np.load(Path(scene_dir) / "texture_blur.npy"))
    return _SCENE_CACHE[key]


def lookup_texture(texture, x, y):
    coordinates = np.vstack([(x - SYNTH_SCENE["x0"]) / SYNTH_SCENE["texel"], (y - SYNTH_SCENE["y0"]) / SYNTH_SCENE["texel"]])
    return np.stack([ndimage.map_coordinates(texture[channel], coordinates, order=1, mode="nearest") for channel in range(3)], axis=1)


def trace_ground_hits(origin, directions, steps=10, bisections=14):
    count = directions.shape[0]
    slope = directions[:, 2]
    falling = slope < -1e-6
    safe_slope = np.where(falling, slope, -1.0)
    t_top = (SYNTH_Z_BOUND_CM - origin[2]) / safe_slope
    t_bottom = (-SYNTH_Z_BOUND_CM - origin[2]) / safe_slope
    fractions = np.linspace(0.0, 1.0, steps + 1)[None, :]
    ts = t_top[:, None] + (t_bottom - t_top)[:, None] * fractions
    px = origin[0] + ts * directions[:, 0:1]
    py = origin[1] + ts * directions[:, 1:2]
    pz = origin[2] + ts * directions[:, 2:3]
    below = pz <= synthetic_terrain(px, py)
    first = np.argmax(below, axis=1)
    rows = np.arange(count)
    hit = falling & below[rows, first]
    first = np.maximum(first, 1)
    low = ts[rows, first - 1]
    high = ts[rows, first]
    for _ in range(bisections):
        middle = 0.5 * (low + high)
        mx = origin[0] + middle * directions[:, 0]
        my = origin[1] + middle * directions[:, 1]
        mz = origin[2] + middle * directions[:, 2]
        under = mz <= synthetic_terrain(mx, my)
        high = np.where(under, middle, high)
        low = np.where(under, low, middle)
    return high, hit


def trace_pawn_hits(origin, directions, pawn_xy, foot_z):
    ox = origin[0] - pawn_xy[0]
    oy = origin[1] - pawn_xy[1]
    dx = directions[:, 0]
    dy = directions[:, 1]
    dz = directions[:, 2]
    radius = SYNTH_PAWN_RADIUS_CM
    top_z = foot_z + SYNTH_PAWN_HEIGHT_CM
    a = dx * dx + dy * dy
    b = 2.0 * (ox * dx + oy * dy)
    c = ox * ox + oy * oy - radius * radius
    discriminant = b * b - 4.0 * a * c
    usable = (discriminant >= 0.0) & (a > 1e-12)
    root = np.sqrt(np.where(usable, discriminant, 0.0))
    side_t = np.where(usable, (-b - root) / np.where(usable, 2.0 * a, 1.0), np.inf)
    side_z = origin[2] + side_t * dz
    side_ok = usable & (side_t > 0.0) & (side_z >= foot_z) & (side_z <= top_z)
    cap_t = np.where(dz < -1e-9, (top_z - origin[2]) / np.where(dz < -1e-9, dz, -1.0), np.inf)
    cap_x = ox + cap_t * dx
    cap_y = oy + cap_t * dy
    cap_ok = (cap_t > 0.0) & (cap_x * cap_x + cap_y * cap_y <= radius * radius)
    t = np.full(dx.shape, np.inf)
    shade = np.zeros(dx.shape)
    t = np.where(side_ok, side_t, t)
    shade = np.where(side_ok, 0.65, shade)
    cap_wins = cap_ok & (cap_t < t)
    t = np.where(cap_wins, cap_t, t)
    shade = np.where(cap_wins, 0.95, shade)
    return t, shade


def synthetic_boxes():
    rng = np.random.default_rng(777)
    lod_x, lod_y = SYNTH_BOX_LOD_CENTER
    boxes = [(lod_x - 50.0, lod_x + 50.0, lod_y - 50.0, lod_y + 50.0, SYNTH_BOX_LOD_HEIGHT_CM)]
    x_low, x_high, y_low, y_high = SYNTH_BOX_REGION
    while len(boxes) < SYNTH_BOX_COUNT:
        center_x = rng.uniform(x_low, x_high)
        center_y = rng.uniform(y_low, y_high) * rng.choice([-1.0, 1.0])
        half_x = rng.uniform(40.0, 60.0)
        half_y = rng.uniform(40.0, 60.0)
        if any(abs(center_x - (box[0] + box[1]) / 2.0) < 260.0 and abs(center_y - (box[2] + box[3]) / 2.0) < 260.0 for box in boxes):
            continue
        boxes.append((center_x - half_x, center_x + half_x, center_y - half_y, center_y + half_y, rng.uniform(30.0, 120.0)))
    return [
        (x0, x1, y0, y1, float(synthetic_terrain((x0 + x1) / 2.0, (y0 + y1) / 2.0)) - SYNTH_BOX_SINK_CM,
         float(synthetic_terrain((x0 + x1) / 2.0, (y0 + y1) / 2.0)) + height)
        for x0, x1, y0, y1, height in boxes
    ]


def synthetic_wind_cells():
    rng = np.random.default_rng(555)
    x_low, x_high, y_low, y_high = SYNTH_BOX_REGION
    cells = []
    while len(cells) < SYNTH_WIND_CELL_COUNT:
        x0 = rng.uniform(x_low, x_high)
        y0 = rng.uniform(-y_high, y_high)
        if abs(y0 + SYNTH_WIND_CELL_SIZE_CM / 2.0) < 100.0:
            continue
        cells.append((x0, x0 + SYNTH_WIND_CELL_SIZE_CM, y0, y0 + SYNTH_WIND_CELL_SIZE_CM))
    return cells


def wind_job_cells(number, seed):
    rng = np.random.default_rng(seed * 1000 + number)
    jobs = []
    for cell in synthetic_wind_cells():
        angle = rng.uniform(0.0, 2.0 * math.pi)
        distance = rng.uniform(1.0, 3.0) * SYNTH_WIND_PIXEL_CM
        gain = 1.0 + rng.uniform(-SYNTH_WIND_GAIN, SYNTH_WIND_GAIN)
        jobs.append([list(cell), distance * math.cos(angle), distance * math.sin(angle), gain])
    return jobs


def trace_box_hits(origin, directions, box):
    low = np.array([box[0], box[2], box[4]])
    high = np.array([box[1], box[3], box[5]])
    with np.errstate(divide="ignore", invalid="ignore"):
        inverse = 1.0 / directions
        first = (low[None, :] - origin[None, :]) * inverse
        second = (high[None, :] - origin[None, :]) * inverse
    near_per_axis = np.minimum(first, second)
    far_per_axis = np.maximum(first, second)
    t_near = near_per_axis.max(axis=1)
    t_far = far_per_axis.min(axis=1)
    face_axis = near_per_axis.argmax(axis=1)
    hit = (t_far >= t_near) & (t_near > 0.0)
    return np.where(hit, t_near, np.inf), face_axis


def box_surface_color(texture, points, face_axis, mode):
    pattern = lookup_texture(texture, points[:, 0] + 700.0 + 2.5 * points[:, 2], points[:, 1] + 2.5 * points[:, 2])
    shade = np.where(face_axis == 2, 1.0, np.where(face_axis == 0, 0.75, 0.55))
    color = pattern * shade[:, None]
    if mode == "purple":
        color = color * (1.0 - SYNTH_TINT_WEIGHT) + np.array(SYNTH_TINTS["purple"])[None, :] * SYNTH_TINT_WEIGHT
    return color


def shade_rays(scene_dir, job, directions):
    texture, blurred = load_scene(scene_dir)
    origin = np.array(job["cam"])
    t_ground, hit = trace_ground_hits(origin, directions)
    hit_x = origin[0] + t_ground * directions[:, 0]
    hit_y = origin[1] + t_ground * directions[:, 1]
    color = np.tile(np.array([0.55, 0.72, 0.92]), (directions.shape[0], 1))
    ground = lookup_texture(texture, hit_x[hit], hit_y[hit])
    for box, shift_x, shift_y, gain in job.get("wind", []):
        inside = hit & (hit_x >= box[0]) & (hit_x < box[1]) & (hit_y >= box[2]) & (hit_y < box[3])
        if not inside.any():
            continue
        ground[inside[hit]] = np.clip(lookup_texture(texture, hit_x[inside] + shift_x, hit_y[inside] + shift_y) * gain, 0.0, 1.0)
    for box, mode in job["cells"]:
        inside = hit & (hit_x >= box[0]) & (hit_x < box[1]) & (hit_y >= box[2]) & (hit_y < box[3])
        if not inside.any():
            continue
        local = inside[hit]
        if mode == "blur":
            ground[local] = lookup_texture(blurred, hit_x[inside], hit_y[inside])
        else:
            tint = np.array(SYNTH_TINTS[mode])
            ground[local] = ground[local] * (1.0 - SYNTH_TINT_WEIGHT) + tint * SYNTH_TINT_WEIGHT
    color[hit] = ground
    nearest = np.where(hit, t_ground, np.inf)
    for box in job.get("boxes", []):
        t_box, face_axis = trace_box_hits(origin, directions, box[:6])
        closer = t_box < nearest
        if not closer.any():
            continue
        points = origin[None, :] + t_box[closer][:, None] * directions[closer]
        color[closer] = box_surface_color(texture, points, face_axis[closer], box[6])
        nearest[closer] = t_box[closer]
    pawn_t, pawn_shade = trace_pawn_hits(origin, directions, job["pawn"][:2], job["pawn"][2] - SYNTH_PAWN_OFFSET_CM)
    pawn_front = np.isfinite(pawn_t) & (pawn_t < nearest)
    color[pawn_front] = np.array([0.62, 0.55, 0.45])[None, :] * pawn_shade[pawn_front][:, None]
    return color


def render_synthetic_photo(scene_dir, job):
    width, height = job["size"]
    supersample = job["supersample"]
    pitch = math.radians(job["pitch"])
    yaw = math.radians(job["yaw"])
    yaw_matrix = np.array([[math.cos(yaw), -math.sin(yaw), 0.0], [math.sin(yaw), math.cos(yaw), 0.0], [0.0, 0.0, 1.0]])
    pitch_matrix = np.array([[math.cos(pitch), 0.0, -math.sin(pitch)], [0.0, 1.0, 0.0], [math.sin(pitch), 0.0, math.cos(pitch)]])
    rotation = yaw_matrix @ pitch_matrix
    focal = (width / 2.0) / math.tan(math.radians(job["fov"]) / 2.0)
    accumulated = np.zeros((height * width, 3))
    columns, rows = np.meshgrid(np.arange(width), np.arange(height))
    columns = columns.ravel().astype(np.float64)
    rows = rows.ravel().astype(np.float64)
    for sub_y in range(supersample):
        for sub_x in range(supersample):
            screen_u = columns + (sub_x + 0.5) / supersample
            screen_v = rows + (sub_y + 0.5) / supersample
            local = np.stack([np.ones_like(screen_u), (screen_u - width / 2.0) / focal, (height / 2.0 - screen_v) / focal], axis=1)
            directions = local @ rotation.T
            for start in range(0, directions.shape[0], 60000):
                chunk = slice(start, start + 60000)
                accumulated[chunk] += shade_rays(scene_dir, job, directions[chunk])
    accumulated /= supersample * supersample
    return (np.clip(accumulated, 0.0, 1.0).reshape(height, width, 3) * 255.0 + 0.5).astype(np.uint8)


def render_job(arguments):
    scene_dir, job = arguments
    image = render_synthetic_photo(scene_dir, job)
    Image.fromarray(image).save(job["out"])
    return job["out"]


def write_synthetic_height(run_dir):
    size = 1009
    axis = (np.arange(size) - (size - 1) / 2.0) * 100.0
    heights = synthetic_terrain(axis[None, :], axis[:, None])
    raw = np.clip(np.rint(heights * 1.28 + 32768.0), 0, 65535).astype("<u2")
    raw.tofile(run_dir / "height.r16")


def build_synthetic_timeline(seed):
    rng = np.random.default_rng(seed)
    steps = (1.0 / SYNTH_FPS) * (1.0 + 0.4 * rng.uniform(-1.0, 1.0, 1200))
    times = np.concatenate([[0.0], np.cumsum(steps)])[:1200]
    glide_end = SYNTH_GLIDE_START_S + SYNTH_GLIDE_LENGTH_CM / SYNTH_SPEED_CM_S
    end_time = glide_end + SYNTH_STARE_SHOT_TIMES[-1] + 0.3
    times = times[times <= end_time]
    travelled = np.clip((times - SYNTH_GLIDE_START_S) * SYNTH_SPEED_CM_S, 0.0, SYNTH_GLIDE_LENGTH_CM)
    segments = np.where(times < SYNTH_GLIDE_START_S, 0, np.where(travelled < SYNTH_GLIDE_LENGTH_CM, 1, 2))
    return times, travelled, segments


def synthetic_run_spec(glide_shots, defects, boxes=False, wind=False, box_change_shot=None):
    return {"glide_shots": glide_shots, "defects": defects, "boxes": boxes, "wind": wind, "box_change_shot": box_change_shot}


def write_synthetic_run(run_dir, scene_dir, spec, pose_shift, size, supersample, executor, seed):
    shots_dir = run_dir / "shots"
    shots_dir.mkdir(parents=True, exist_ok=True)
    times, travelled, segments = build_synthetic_timeline(seed)
    rows = len(times)
    pawn_x = -travelled
    pawn_y = np.zeros(rows)
    pawn_z = synthetic_terrain(pawn_x, pawn_y) + SYNTH_PAWN_OFFSET_CM
    forward, _, _ = camera_basis(SYNTH_PITCH_DEG, 0.0)
    cam = np.stack([pawn_x, pawn_y, pawn_z], axis=1) - SYNTH_ARM_CM * forward[None, :]
    deltas = np.concatenate([[times[1] - times[0]], np.diff(times)])
    real_perf = 5000.0 + times
    header = [FRAMES_COLUMNS[name][0] for name in FRAMES_COLUMNS]
    with open(run_dir / "frames.csv", "w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(header)
        for row in range(rows):
            values = {
                "frame": SYNTH_FRAME_BASE + row, "real_perf": real_perf[row], "real_epoch": 1.76e9 + times[row],
                "game_s": times[row], "delta": deltas[row], "seg": int(segments[row]),
                "pawn_x": pawn_x[row], "pawn_y": pawn_y[row], "pawn_z": pawn_z[row],
                "cam_x": cam[row, 0], "cam_y": cam[row, 1], "cam_z": cam[row, 2],
                "cam_pitch": SYNTH_PITCH_DEG, "cam_yaw": 0.0, "cam_roll": 0.0, "cam_fov": SYNTH_FOV_DEG,
            }
            writer.writerow([repr(float(values[name])) if isinstance(values[name], float) else values[name] for name in FRAMES_COLUMNS])
    stare_start_row = int(np.argmax(segments == 2))
    stare_start_time = times[stare_start_row]
    glide_shots = spec["glide_shots"]
    plan = [(0, int(np.flatnonzero(segments == 0)[-1]))]
    for shot in range(1, glide_shots + 1):
        plan.append((shot, int(np.argmax(times >= SYNTH_GLIDE_START_S + shot / SYNTH_SHOT_HZ))))
    for position, shot_time in enumerate(SYNTH_STARE_SHOT_TIMES):
        plan.append((glide_shots + 1 + position, int(np.argmax(times >= stare_start_time + shot_time))))
    defects = spec["defects"]
    box_list = synthetic_boxes() if spec.get("boxes") else []
    jobs = []
    shots = []
    for number, row in plan:
        kind = ["settle", "glide", "stare"][int(segments[row])]
        pose_row = min(max(row + pose_shift, 0), rows - 1)
        cells = []
        if defects:
            cells.append((SYNTH_CELL_BLUR, "blur") if number < defects["blur_shot"] else None)
            cells.append((SYNTH_CELL_COLOR, "green") if number >= defects["color_shot"] else None)
            cells.append((SYNTH_CELL_STARE, "purple") if number == glide_shots + 1 + defects["stare_index"] else None)
        out = shots_dir / f"shot_{number:04d}.png"
        change_shot = spec.get("box_change_shot")
        job_boxes = [
            list(box) + ["purple" if index == 0 and change_shot is not None and number >= change_shot else "plain"]
            for index, box in enumerate(box_list)
        ]
        jobs.append((str(scene_dir), {
            "boxes": job_boxes, "wind": wind_job_cells(number, seed) if spec.get("wind") else [],
            "out": str(out), "cam": cam[pose_row].tolist(), "pitch": SYNTH_PITCH_DEG, "yaw": 0.0, "fov": SYNTH_FOV_DEG,
            "pawn": [pawn_x[pose_row], pawn_y[pose_row], pawn_z[pose_row]], "size": size, "supersample": supersample,
            "cells": [cell for cell in cells if cell is not None],
        }))
        seg_start = {0: 0.0, 1: SYNTH_GLIDE_START_S, 2: stare_start_time}[int(segments[row])]
        shots.append({
            SHOT_KEYS["number"]: number, SHOT_KEYS["path"]: str(out).replace("\\", "/"),
            SHOT_KEYS["request_frame"]: SYNTH_FRAME_BASE + row, SHOT_KEYS["request_real"]: float(real_perf[row]),
            SHOT_KEYS["seg"]: int(segments[row]), SHOT_KEYS["kind"]: kind, SHOT_KEYS["seg_time"]: float(times[row] - seg_start),
        })
    list(executor.map(render_job, jobs))
    write_json(run_dir / "result.json", {
        "request_id": run_dir.name, "state": "complete", "rows_count": rows, "shots": shots, "cvars": {},
        "viewport": list(size), "fov": SYNTH_FOV_DEG, "csv_start_frame": SYNTH_FRAME_BASE + SYNTH_PROFILE_ROW_OFFSET,
        "csv_stop_frame": SYNTH_FRAME_BASE + rows - 1, "errors": [],
    })
    write_synthetic_height(run_dir)
    write_synthetic_profile(run_dir, times, deltas, segments, seed)
    return plan


def write_synthetic_profile(run_dir, times, deltas, segments, seed):
    rng = np.random.default_rng(seed + 1)
    names = [
        "FrameTime", "GameThreadTime", "RenderThreadTime", "GPUTime",
        "TextureStreaming/StreamingPool", "TextureStreaming/WantedMips", "TextureStreaming/CachedMips",
        "TextureStreaming/PendingStreamInData", "TextureStreaming/DesiredDataLoadedPercent",
    ]
    start = SYNTH_PROFILE_ROW_OFFSET
    with open(run_dir / "profile.csv", "w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(names)
        for row in range(start, len(times)):
            frame_time = deltas[row] * 1000.0 + rng.normal(0.0, 0.15)
            pending = 3.0 if times[row] < 2.0 else 0.0
            writer.writerow([
                f"{frame_time:.4f}", f"{frame_time * 0.5:.4f}", f"{frame_time * 0.4:.4f}", f"{6.0 + rng.uniform(0.0, 3.0):.4f}",
                "1200.0", "300.0", "800.0", f"{pending:.4f}", "99.0",
            ])
        writer.writerow(["[HasHeaderRowAtEnd]", "0"])
    with open(run_dir / "gpu.csv", "w", encoding="utf-8") as handle:
        for position in range(40):
            handle.write(f"2026/11/20 12:00:{position % 60:02d}.000, {4000 + position * 5}, {40 + position % 10}\n")


def make_check_rows(analysis_defect, analysis_clean, analysis_shift, analysis_boxes, analysis_wind, analysis_lod, plan_info, compare_dir):
    rows = []

    def add(name, expected, actual, passed):
        rows.append((name, expected, actual, bool(passed)))

    events = analysis_defect["events"]
    wanted = {plan_info["blur_shot"], plan_info["color_shot"]}
    add("결함 실행 팝 사건 수", 2, len(events), len(events) == 2)
    add("사건이 난 사진 번호", sorted(wanted), sorted(event["photo"] for event in events), {event["photo"] for event in events} == wanted)
    expected_centers = {plan_info["blur_shot"]: (-1400.0, 300.0), plan_info["color_shot"]: (-2200.0, -300.0)}
    for event in events:
        expected = expected_centers.get(event["photo"])
        if expected is None:
            continue
        error = math.hypot(event["center_cm"][0] - expected[0], event["center_cm"][1] - expected[1])
        add(f"사진 {event['photo']} 사건 중심 오차(cm) ≤ 50", f"≤ 50 (기대 {expected})", f"{error:.1f}", error <= 50.0)
    blur_events = [event for event in events if event["photo"] == plan_info["blur_shot"]]
    ratio = blur_events[0]["detail_ratio"] if blur_events else None
    add("흐림→선명 세부 비율 > 1.2", "> 1.2", format_number(ratio), ratio is not None and ratio > 1.2)
    stare = analysis_defect["stare"][0] if analysis_defect["stare"] else None
    expected_settle = stare["photos"][plan_info["stare_index"]]["seg_time_s"] if stare else None
    actual_settle = stare["settle_time_s"] if stare else None
    add("응시 정착 시간 = 둘째 장 시각(초)", format_number(expected_settle, 3), format_number(actual_settle, 3), stare is not None and abs(actual_settle - expected_settle) < 1e-6)
    registration = analysis_defect["registration"]["median_cm"]
    add("등록 잔차 중앙값(cm) ≤ 5", "≤ 5", format_number(registration), registration is not None and registration <= 5.0)
    add("자세 오프셋 선택(결함 실행)", 0, analysis_defect["pose_offset"]["chosen"], analysis_defect["pose_offset"]["chosen"] == 0)
    profile = analysis_defect.get("profile", {})
    add("측정표↔프레임 행 맞춤 오프셋", SYNTH_PROFILE_ROW_OFFSET, profile.get("align_offset_rows"), profile.get("align_offset_rows") == SYNTH_PROFILE_ROW_OFFSET)
    gpu = analysis_defect.get("gpu", {})
    add("GPU 메모리 최대(MiB)", 4195, format_number(gpu.get("memory_used_mib_max"), 0), gpu.get("memory_used_mib_max") == 4195)
    add("반례 (i) 결함 없는 실행 사건 수", 0, len(analysis_clean["events"]), len(analysis_clean["events"]) == 0)
    clean_stare = analysis_clean["stare"][0]["settle_time_s"] if analysis_clean["stare"] else None
    add("반례 (i) 결함 없는 실행 응시 정착", 0, format_number(clean_stare, 3), clean_stare == 0.0)
    add("반례 (ii) 자세 한 행 밀림 → 선택 오프셋", 1, analysis_shift["pose_offset"]["chosen"], analysis_shift["pose_offset"]["chosen"] == 1)
    add("반례 (ii) 밀림 보정 뒤 사건 수", 0, len(analysis_shift["events"]), len(analysis_shift["events"]) == 0)
    add("반례 (d) 솟은 상자 12개 거짓 사건 수", 0, len(analysis_boxes["events"]), len(analysis_boxes["events"]) == 0)
    add("반례 (e) 바람 흔들림·밝기 거짓 사건 수", 0, len(analysis_wind["events"]), len(analysis_wind["events"]) == 0)
    lod_events = analysis_lod["events"]
    add("반례 (f) 상자 무늬 교체 사건 수", 1, len(lod_events), len(lod_events) == 1)
    lod_photo = lod_events[0]["photo"] if lod_events else None
    add("반례 (f) 사건 사진 번호", SYNTH_BOX_LOD_SHOT, lod_photo, lod_photo == SYNTH_BOX_LOD_SHOT)
    lod_error = math.hypot(lod_events[0]["center_cm"][0] - SYNTH_BOX_LOD_CENTER[0], lod_events[0]["center_cm"][1] - SYNTH_BOX_LOD_CENTER[1]) if lod_events else None
    add("반례 (f) 사건 중심과 상자 거리(cm) ≤ 200", "≤ 200", format_number(lod_error, 1), lod_error is not None and lod_error <= 200.0)
    noise = analysis_clean.get("noise", {})
    add("잡음 절 걷기·응시 분위수 기록", "있음", "있음" if noise.get("walk", {}).get("p95") is not None and noise.get("stare", {}).get("p95") is not None else "없음", noise.get("walk", {}).get("p95") is not None and noise.get("stare", {}).get("p95") is not None)
    add("사건 지도 pop_map.png", "있음", "있음" if (Path(analysis_defect["run_dir"]) / "sheets" / "pop_map.png").is_file() else "없음", (Path(analysis_defect["run_dir"]) / "sheets" / "pop_map.png").is_file())
    add("비교 자료 compare.md", "있음", "있음" if (compare_dir / "compare.md").is_file() else "없음", (compare_dir / "compare.md").is_file())
    webp = compare_dir / "compare_glide.webp"
    add("비교 자료 compare_glide.webp", "있음", f"{webp.stat().st_size} 바이트" if webp.is_file() else "없음", webp.is_file() and webp.stat().st_size > 0)
    stare_png = list(compare_dir.glob("compare_stare_*.png"))
    add("비교 자료 compare_stare_*.png", "≥ 1", len(stare_png), len(stare_png) >= 1)
    return rows


def print_check_table(rows):
    print(f"{'항목':<44} | {'기대':<24} | {'실제':<24} | 판정")
    print("-" * 110)
    for name, expected, actual, passed in rows:
        print(f"{name:<44} | {str(expected):<24} | {str(actual):<24} | {'통과' if passed else '어긋남'}")


def run_self_test(args, params):
    base = Path(args.self_test_dir)
    base.mkdir(parents=True, exist_ok=True)
    for name in ("run_defect", "run_clean", "run_shift", "run_boxes", "run_wind", "run_lod", "_scene"):
        if (base / name).exists():
            shutil.rmtree(base / name)
    for stale in base.glob("compare_*"):
        shutil.rmtree(stale)
    size = tuple(int(part) for part in args.self_test_size.lower().split("x"))
    supersample = args.self_test_supersample
    started = time.perf_counter()
    scene_dir = base / "_scene"
    build_scene_files(scene_dir)
    plan_info = {"blur_shot": 22, "color_shot": 34, "stare_index": 1}
    specs = [
        ("run_defect", synthetic_run_spec(43, plan_info), 0, 11),
        ("run_clean", synthetic_run_spec(24, None), 0, 12),
        ("run_shift", synthetic_run_spec(24, None), 1, 13),
        ("run_boxes", synthetic_run_spec(SYNTH_NOISE_GLIDE_SHOTS, None, boxes=True), 0, 14),
        ("run_wind", synthetic_run_spec(SYNTH_NOISE_GLIDE_SHOTS, None, wind=True), 0, 15),
        ("run_lod", synthetic_run_spec(SYNTH_NOISE_GLIDE_SHOTS, None, boxes=True, box_change_shot=SYNTH_BOX_LOD_SHOT), 0, 16),
    ]
    workers = max(1, min(8, (args.workers or 8)))
    with ProcessPoolExecutor(max_workers=workers) as executor:
        for name, spec, shift, seed in specs:
            (base / name).mkdir(parents=True, exist_ok=True)
            write_synthetic_run(base / name, scene_dir, spec, shift, size, supersample, executor, seed)
    print(f"합성 사진 생성 {time.perf_counter() - started:.1f}초 (크기 {size[0]}x{size[1]}, 부표본 {supersample}x{supersample})")
    analyses = {}
    for name, _, _, _ in specs:
        analyses[name] = analyze_run(base / name, params)
        print(f"{name}: 사진 {analyses[name]['photo_count']}장, 분석 {analyses[name]['analysis_seconds']:.1f}초, 사진당 {analyses[name]['seconds_per_photo']:.3f}초, 사건 {len(analyses[name]['events'])}개(자세 어긋남으로 걸러낸 후보 {analyses[name]['pose_artifacts_rejected']}개), 오프셋 {analyses[name]['pose_offset']['chosen']} {analyses[name]['pose_offset']['candidates']}")
    for name, _, _, _ in specs:
        noise = analyses[name]["noise"]
        print(f"{name} 잡음: 걷기 {[format_number(noise['walk'].get('p' + str(q)), 4) for q in (50, 90, 95, 99)]} 응시 {[format_number(noise['stare'].get('p' + str(q)), 4) for q in (50, 90, 95, 99)]} 제안 T_pop {format_number(noise['t_pop_suggested'], 3)} 제안값 사건 {noise['events_at_suggested']}")
    compare_dir = run_compare(str(base / "run_defect"), str(base / "run_clean"), params, output_root=base)
    rows = make_check_rows(analyses["run_defect"], analyses["run_clean"], analyses["run_shift"], analyses["run_boxes"], analyses["run_wind"], analyses["run_lod"], plan_info, compare_dir)
    print_check_table(rows)
    for event in analyses["run_defect"]["events"]:
        print(f"사건 {event['number']}: 사진 {event['photo']} 중심 {event['center_cm']} 블록 {event['blocks']} 평균D {event['mean_d']:.3f} 세부비율 {event['detail_ratio']} 늦음 {event['late_s']:.2f}초 화면 {event['screen']}")
    failed = [row for row in rows if not row[3]]
    if failed:
        print("어긋난 항목: " + ", ".join(row[0] for row in failed))
        return 1
    print("자체 시험 통과")
    return 0


def load_params(config_path):
    params = dict(DEFAULT_PARAMS)
    if config_path:
        with open(config_path, encoding="utf-8") as handle:
            params.update(json.load(handle))
    return params


def build_parser():
    parser = argparse.ArgumentParser(description="바닥 팝핑 분석기")
    parser.add_argument("--run")
    parser.add_argument("--compare", nargs=2, metavar=("A", "B"))
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--config")
    parser.add_argument("--workers", type=int)
    parser.add_argument("--self-test-dir", default=str(SELF_TEST_DEFAULT_DIR))
    parser.add_argument("--self-test-size", default="640x360")
    parser.add_argument("--self-test-supersample", type=int, default=2)
    return parser


def main(argv=None):
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")
    args = build_parser().parse_args(argv)
    params = load_params(args.config)
    if args.workers:
        params["analysis_workers"] = args.workers
    if args.self_test:
        return run_self_test(args, params)
    if args.compare:
        print(run_compare(args.compare[0], args.compare[1], params))
        return 0
    if args.run:
        analysis = analyze_run(resolve_run_dir(args.run), params)
        print(f"사건 {len(analysis['events'])}개, 사진당 {analysis['seconds_per_photo']:.3f}초")
        return 0
    build_parser().print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
