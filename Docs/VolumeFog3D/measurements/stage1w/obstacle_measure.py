"""실행 환경: 파이썬 3 + Pillow + numpy, 작업 폴더 C:/Project/TDGame (PYTHONUTF8=1 python -B)
실행 명령: python -B Docs/VolumeFog3D/measurements/stage1w/obstacle_measure.py numbers|stills|clips|sheets <실행> [처음초 끝초 열 행 diff]
출력: Docs/VolumeFog3D/measurements/stage1w/obstacles.json·obstacle_stills.png·obstacle_low.webp·obstacle_high.webp·obstacle_wall.webp, 눈 확인용 그림은 Saved/AgentOps/20261114/fog3d/stage1w/s1/review_*.png
상태: 1 마무리 S1(prereg-1c-wrapup.md 2절 A·D). 물체 면에서 20~120cm 띠의 먼 쪽 ÷ 가까운 쪽(정보 숫자)과 그림 만들기. 판정선 없음."""
import json
import math
import sys
from pathlib import Path

import numpy
from PIL import Image, ImageDraw

ROOT = Path("C:/Project/TDGame")
sys.path.insert(0, str(ROOT / "Tools"))
sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))
import fog3d_calib_clips as C
import fog3d_calib_report as R

CAL = ROOT / "Saved/FluidFX/fog3d_calib"
OUT = ROOT / "Docs/VolumeFog3D/measurements/stage1w"
WORK = ROOT / "Saved/AgentOps/20261114/fog3d/stage1w/s1"
LIMIT = 12 * 1024 * 1024
TILE = (640, 360)
GAIN = 8
FOG_TOP_PRISM_CM = 135.0
PAWN_RADIUS_CM = 40.0
PAWN_HEIGHT_CM = 190.0
BAND_NEAR_CM = 20.0
BAND_FAR_CM = 120.0
SWING_AT = 1.5
OBJECTS = {
    "LOW": {"name": "낮은 상자", "x": (425.0, 575.0), "y": (-475.0, -325.0), "height": 30.0, "file": "obstacle_low.webp"},
    "HIGH": {"name": "높은 상자", "x": (425.0, 575.0), "y": (325.0, 475.0), "height": 180.0, "file": "obstacle_high.webp"},
    "WALL": {"name": "얇은 벽", "x": (1095.0, 1105.0), "y": (-150.0, 150.0), "height": 250.0, "file": "obstacle_wall.webp"},
}
CAMERA_NAMES = {"b": "뒤 카메라", "s": "옆 카메라"}
SWING_RUNS = {"LOW": ("LOW_sw_b", "LOW_sw_s"), "HIGH": ("HIGH_sw_b", "HIGH_sw_s"), "WALL": ("WALL_sw_b", "WALL_sw_s")}
WALK_RUNS = {"LOW": ("LOW_wk_b", "LOW_wk_s"), "HIGH": ("HIGH_wk_b", "HIGH_wk_s")}
MEASURE_AFTER_SECONDS = 2.5
FLICKER_FROM_SECONDS = 3.0
BAND_COLORS = {"near": (0, 220, 255), "far": (255, 150, 0)}


def frame_file(run, seconds):
    return CAL / run / "capture" / f"00_c_{int(round(seconds * 1000)):05d}ms.png"


def luma(path):
    return numpy.asarray(Image.open(path).convert("L"), dtype=numpy.float64)


def rotation_axes(pitch, yaw, roll=0.0):
    cp, sp, cy, sy, cr, sr = [f(math.radians(v)) for v in (pitch, yaw, roll) for f in (math.cos, math.sin)]
    xa = numpy.array([cp * cy, cp * sy, sp])
    ya = numpy.array([sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp])
    za = numpy.array([-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp])
    return xa, ya, za


class Camera:
    def __init__(self, loc, rot, fov, width, height):
        self.loc = numpy.array(loc, dtype=float)
        self.axes = rotation_axes(*rot)
        self.w = width
        self.h = height
        self.f = (width / 2.0) / math.tan(math.radians(fov) / 2.0)

    def ray_grid(self):
        cols = numpy.arange(self.w) + 0.5
        rows = numpy.arange(self.h) + 0.5
        u, v = numpy.meshgrid(cols, rows)
        ry = (u - self.w / 2.0) / self.f
        rz = -(v - self.h / 2.0) / self.f
        xa, ya, za = self.axes
        return xa[None, None, :] + ry[..., None] * ya[None, None, :] + rz[..., None] * za[None, None, :]

    def project(self, p):
        rel = numpy.array(p, dtype=float) - self.loc
        xa, ya, za = self.axes
        depth = rel @ xa
        return [self.w / 2.0 + self.f * (rel @ ya) / depth, self.h / 2.0 - self.f * (rel @ za) / depth, depth]


def camera_from_state(state, width, height):
    return Camera(state["cam_world_location"], state["cam_world_rotation"], state["cam_fov"], width, height)


def box_hit(camera, directions, box_min, box_max):
    origin = camera.loc[None, None, :]
    safe = numpy.where(numpy.abs(directions) < 1e-9, 1e-9, directions)
    t1 = (numpy.array(box_min)[None, None, :] - origin) / safe
    t2 = (numpy.array(box_max)[None, None, :] - origin) / safe
    t_near = numpy.minimum(t1, t2).max(axis=2)
    t_far = numpy.maximum(t1, t2).min(axis=2)
    hit = (t_far >= numpy.maximum(t_near, 0.0)) & (t_far > 0)
    return hit, numpy.maximum(t_near, 0.0)


def cylinder_hit(camera, directions, center_xy, radius, z_low, z_high):
    ox = camera.loc[0] - center_xy[0]
    oy = camera.loc[1] - center_xy[1]
    dx = directions[..., 0]
    dy = directions[..., 1]
    dz = directions[..., 2]
    a = dx * dx + dy * dy
    b = 2.0 * (ox * dx + oy * dy)
    c = ox * ox + oy * oy - radius * radius
    discriminant = b * b - 4.0 * a * c
    valid = (discriminant >= 0) & (a > 1e-12)
    root = numpy.sqrt(numpy.where(valid, discriminant, 0.0))
    safe_a = numpy.where(a > 1e-12, a, 1.0)
    t1 = (-b - root) / (2.0 * safe_a)
    t2 = (-b + root) / (2.0 * safe_a)
    z1 = camera.loc[2] + t1 * dz
    z2 = camera.loc[2] + t2 * dz
    hit_side = valid & (((t1 > 0) & (z1 >= z_low) & (z1 <= z_high)) | ((t2 > 0) & (z2 >= z_low) & (z2 <= z_high)))
    safe_dz = numpy.where(numpy.abs(dz) < 1e-9, 1e-9, dz)
    cap = numpy.zeros_like(hit_side)
    for z_cap in (z_low, z_high):
        t_cap = (z_cap - camera.loc[2]) / safe_dz
        x_cap = ox + t_cap * dx
        y_cap = oy + t_cap * dy
        cap |= (t_cap > 0) & (x_cap * x_cap + y_cap * y_cap <= radius * radius)
    return hit_side | cap


def floor_height(state):
    half = state.get("capsule_half_height")
    half = half if isinstance(half, (int, float)) else 0.0
    return state["actor_location"][2] - half


def band_boxes(obj):
    spec = OBJECTS[obj]
    x_min, x_max = spec["x"]
    y_min, y_max = spec["y"]
    return {"near": ((x_min - BAND_FAR_CM, y_min), (x_min - BAND_NEAR_CM, y_max)), "far": ((x_max + BAND_NEAR_CM, y_min), (x_max + BAND_FAR_CM, y_max))}


def band_masks(state, obj, width, height):
    camera = camera_from_state(state, width, height)
    directions = camera.ray_grid()
    floor_z = floor_height(state)
    spec = OBJECTS[obj]
    object_hit, object_t = box_hit(camera, directions, (spec["x"][0], spec["y"][0], floor_z), (spec["x"][1], spec["y"][1], floor_z + spec["height"]))
    pawn_x, pawn_y = state["actor_location"][0], state["actor_location"][1]
    pawn_hit = cylinder_hit(camera, directions, (pawn_x, pawn_y), PAWN_RADIUS_CM, floor_z, floor_z + PAWN_HEIGHT_CM)
    masks = {}
    for name, (low, high) in band_boxes(obj).items():
        band_hit, band_t = box_hit(camera, directions, (low[0], low[1], floor_z), (high[0], high[1], floor_z + FOG_TOP_PRISM_CM))
        hidden_by_object = object_hit & (object_t <= band_t)
        masks[name] = {"mask": band_hit & ~hidden_by_object & ~pawn_hit, "raw": int(band_hit.sum()), "removed_object": int((band_hit & hidden_by_object).sum()), "removed_pawn": int((band_hit & pawn_hit & ~hidden_by_object).sum())}
    return camera, masks, object_hit, pawn_hit


def band_polygon(camera, obj, name, floor_z):
    low, high = band_boxes(obj)[name]
    points = [camera.project((x, y, z))[:2] for x in (low[0], high[0]) for y in (low[1], high[1]) for z in (floor_z, floor_z + FOG_TOP_PRISM_CM)]
    points = sorted(set((round(px, 1), round(py, 1)) for px, py in points))

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    lower = []
    for p in points:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    upper = []
    for p in reversed(points):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def read_state(run):
    return json.loads((WORK / f"state_{run}.json").read_text(encoding="utf-8"))


def check_run(run):
    directory = CAL / run
    result = {"name": run, "valid": False, "reasons": []}
    try:
        run_record, schedule_status, shots = C.load_dense_shots(directory)
    except (OSError, ValueError, KeyError, R.InputProblem) as error:
        result["reasons"].append(f"읽기 오류: {error}")
        return result
    expected = len(run_record["shots"])
    found = sum(1 for entry in shots.values() if entry["exists"])
    delays = [float(entry["shot"]["captured_game_time"]) - float(entry["entry"]["time"]) for entry in shots.values() if entry["shot"].get("captured_game_time") is not None]
    reference_game_time = float(schedule_status.get("reference_game_time", 0.0))
    result.update({"expected_shots": expected, "found_shots": found, "late_max_seconds": max(delays) if delays else None, "shots_over_0.05": sum(1 for v in delays if v > 0.05)})
    if found != expected:
        result["reasons"].append(f"장 파일 {found}/{expected}")
    if delays and max(delays) > 0.05:
        result["reasons"].append(f"늦은 장 {sum(1 for v in delays if v > 0.05)}개")
    events_done = schedule_status.get("events_done", [])
    result["events_done"] = f"{len(events_done)}/{len(run_record['events'])}"
    if len(events_done) != len(run_record["events"]):
        result["reasons"].append("이벤트가 다 실행되지 않음")
    if not schedule_status.get("done") or schedule_status.get("error"):
        result["reasons"].append(f"예약 미완료 또는 오류: {schedule_status.get('error')}")
    log = R.read_run_log(directory, False, None)
    result["log"] = {key: log.get(key) for key in ("source", "submitted", "rejected", "fog3d_warning_or_error_lines", "usable")}
    if log.get("usable") is False:
        result["reasons"].append("로그: 경고·오류 또는 거부가 있거나 요약 줄이 없다")
    teleports = [event for event in events_done if event.get("mode") == "teleport" or "teleport" in json.dumps(event)]
    result["teleport_events"] = teleports[:2]
    try:
        state = read_state(run)
        result["state"] = {key: state.get(key) for key in ("actor_location", "actor_rotation", "sun_rotation", "cam_world_location", "cam_world_rotation", "cam_fov", "capsule_half_height", "state_target", "TD.Fog3D.Quality", "TD.Fog3D.Mode", "error")}
    except (OSError, ValueError):
        result["reasons"].append("상태 읽기 파일 없음")
    result["valid"] = not result["reasons"]
    return result


def band_mean(diff, mask):
    return float(diff[mask].mean()) if mask.any() else None


def swing_numbers(run, obj):
    state = read_state(run)
    stamp = SWING_AT + MEASURE_AFTER_SECONDS
    record = json.loads((CAL / run / "run.json").read_text(encoding="utf-8"))
    references = [shot["time"] for shot in record["shots"] if shot["group"] == "R"]
    swing = luma(frame_file(run, stamp))
    ref1 = luma(frame_file(run, references[0]))
    ref2 = luma(frame_file(run, references[1]))
    height, width = swing.shape
    camera, masks, object_hit, pawn_hit = band_masks(state, obj, width, height)
    diff = numpy.abs(swing - ref1)
    noise = numpy.abs(ref1 - ref2)
    entry = {"run": run, "image": [width, height], "frame_seconds_after_swing": MEASURE_AFTER_SECONDS, "reference_times": references}
    floor_z = floor_height(state)
    for name, info in masks.items():
        entry[name] = {"pixels": int(info["mask"].sum()), "pixels_raw": info["raw"], "removed_by_object": info["removed_object"], "removed_by_pawn": info["removed_pawn"],
                       "mean_abs_diff": band_mean(diff, info["mask"]), "noise_mean_abs_diff": band_mean(noise, info["mask"]),
                       "polygon_px": [[round(a, 1), round(b, 1)] for a, b in band_polygon(camera, obj, name, floor_z)]}
    near = entry["near"]["mean_abs_diff"]
    far = entry["far"]["mean_abs_diff"]
    entry["far_over_near"] = (far / near) if near and far is not None and near > 0 else None
    near_net = max((near or 0.0) - (entry["near"]["noise_mean_abs_diff"] or 0.0), 0.0)
    far_net = max((far or 0.0) - (entry["far"]["noise_mean_abs_diff"] or 0.0), 0.0)
    entry["near_net"] = near_net
    entry["far_net"] = far_net
    entry["far_over_near_net"] = (far_net / near_net) if near_net > 0.05 else None
    entry["both_bands_visible"] = bool(entry["near"]["pixels"] > 500 and entry["far"]["pixels"] > 500)
    centers = {}
    for name, info in masks.items():
        ys, xs = numpy.nonzero(info["mask"])
        centers[name] = [float(xs.mean()), float(ys.mean())] if xs.size else None
    entry["band_centers_px"] = centers
    entry["bands_split_left_right"] = bool(centers["near"] and centers["far"] and abs(centers["far"][0] - centers["near"][0]) > 0.15 * width)
    return entry


def flicker_series(run, obj, kind):
    state = read_state(run)
    record = json.loads((CAL / run / "run.json").read_text(encoding="utf-8"))
    dense = [shot for shot in record["shots"] if shot["group"] == "S"]
    first = luma(frame_file(run, dense[0]["time"]))
    height, width = first.shape
    _, masks, object_hit, pawn_hit = band_masks(state, obj, width, height)
    region = (masks["near"]["mask"] | masks["far"]["mask"]) & ~object_hit & ~pawn_hit
    values = []
    for shot in dense:
        image = luma(frame_file(run, shot["time"]))
        values.append(float(image[region].mean()) if region.any() else float("nan"))
    values = numpy.array(values)
    second = numpy.abs(values[1:-1] - 0.5 * (values[:-2] + values[2:]))
    start_index = int(round(FLICKER_FROM_SECONDS / 0.1))
    late = second[start_index - 1:]
    return {"run": run, "frames": len(values), "region_pixels": int(region.sum()), "mean_luma": float(numpy.nanmean(values)), "range_luma_all": float(numpy.nanmax(values) - numpy.nanmin(values)),
            "from_seconds": FLICKER_FROM_SECONDS, "range_luma_settled": float(numpy.nanmax(values[start_index:]) - numpy.nanmin(values[start_index:])),
            "second_difference_mean_settled": float(numpy.nanmean(late)), "second_difference_max_settled": float(numpy.nanmax(late)), "second_difference_max_at_seconds": float((numpy.nanargmax(late) + start_index) * 0.1),
            "second_difference_max_all": float(numpy.nanmax(second))}


def run_numbers():
    result = {"note": "정보 숫자(판정선 없음). 띠: 물체 면에서 20~120cm, 물체 면의 폭, 안개 바닥에서 135cm 까지의 상자. 화소는 카메라 값으로 세계 좌표에서 계산해 잡고 폰 몸(반지름 40cm 원기둥, 높이 190cm)과 물체 자체가 찍힌 화소는 뺐다.",
              "measure": "|휘두른 뒤 2.5초 장 − 기준 장(계산 끔)| 의 8비트 밝기 평균(띠 안), 잡음 = 기준 장 두 장의 차이 평균", "runs": {}, "swing": {}, "flicker": {}}
    for obj, runs in SWING_RUNS.items():
        for run in runs:
            result["runs"][run] = check_run(run)
            result["swing"][run] = swing_numbers(run, obj)
            result["flicker"][run] = flicker_series(run, obj, "swing")
    for obj, runs in WALK_RUNS.items():
        for run in runs:
            result["runs"][run] = check_run(run)
            result["flicker"][run] = flicker_series(run, obj, "walk")
    result["control_no_swing"] = {}
    for obj, run in (("LOW", "NS_LOW_s"), ("HIGH", "NS_HIGH_s"), ("WALL", "NS_WALL_s"), ("LOW", "NS_LOW_b")):
        result["runs"][run] = check_run(run)
        result["control_no_swing"][run] = swing_numbers(run, obj)
    for obj, run, control in (("LOW", "LOW_sw_s", "NS_LOW_s"), ("HIGH", "HIGH_sw_s", "NS_HIGH_s"), ("WALL", "WALL_sw_s", "NS_WALL_s")):
        entry = result["swing"][run]
        base = result["control_no_swing"][control]
        near_extra = entry["near"]["mean_abs_diff"] - base["near"]["mean_abs_diff"]
        far_extra = entry["far"]["mean_abs_diff"] - base["far"]["mean_abs_diff"]
        entry["control_run"] = control
        entry["near_minus_control"] = near_extra
        entry["far_minus_control"] = far_extra
        entry["far_over_near_minus_control"] = (far_extra / near_extra) if near_extra > 0.05 else None
        print("minus-control", run, round(near_extra, 3), round(far_extra, 3), entry["far_over_near_minus_control"])
    (OUT / "obstacles.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    for run, entry in result["swing"].items():
        print(run, "near", entry["near"]["pixels"], round(entry["near"]["mean_abs_diff"] or -1, 3), "noise", round(entry["near"]["noise_mean_abs_diff"] or -1, 3), "far", entry["far"]["pixels"], round(entry["far"]["mean_abs_diff"] or -1, 3), "noise", round(entry["far"]["noise_mean_abs_diff"] or -1, 3),
              "ratio", entry["far_over_near"], "net", entry["far_over_near_net"], "split", entry["bands_split_left_right"], "visible", entry["both_bands_visible"])
    for run, entry in result["control_no_swing"].items():
        print("control", run, "near", entry["near"]["pixels"], round(entry["near"]["mean_abs_diff"], 3), "far", entry["far"]["pixels"], round(entry["far"]["mean_abs_diff"], 3), "ratio", entry["far_over_near"])
    for run, entry in result["runs"].items():
        print(run, "valid", entry["valid"], entry["reasons"], entry.get("log"), entry.get("late_max_seconds"), entry.get("events_done"))
    for run, entry in result["flicker"].items():
        print("flicker", run, round(entry["range_luma_settled"], 3), round(entry["second_difference_mean_settled"], 4), round(entry["second_difference_max_settled"], 4), entry["second_difference_max_at_seconds"])


def with_bands(image, run, obj, scale):
    state = read_state(run)
    width, height = image.size
    camera = camera_from_state(state, 1280, 720)
    floor_z = floor_height(state)
    draw = ImageDraw.Draw(image)
    for name in ("near", "far"):
        polygon = [(px * scale, py * scale) for px, py in band_polygon(camera, obj, name, floor_z)]
        draw.line(polygon + [polygon[0]], fill=BAND_COLORS[name], width=2)
    return image


def tile_with_text(image, left_text, right_text=None):
    tile = image.resize(TILE, Image.LANCZOS)
    font = C.load_korean_font(max(18, TILE[1] // 15))
    if left_text:
        C.draw_badge(tile, left_text, font, "left")
    if right_text:
        C.draw_badge(tile, right_text, font, "right")
    return tile


def diff_image(path_a, path_b, gain):
    a = luma(path_a)
    b = luma(path_b)
    value = numpy.clip(numpy.abs(a - b) * gain, 0, 255).astype(numpy.uint8)
    return Image.fromarray(value).convert("RGB")


def run_stills():
    rows = []
    for obj, runs in SWING_RUNS.items():
        record = json.loads((CAL / runs[0] / "run.json").read_text(encoding="utf-8"))
        references = [shot["time"] for shot in record["shots"] if shot["group"] == "R"]
        tiles = []
        for run in (runs[1], runs[0]):
            camera_name = CAMERA_NAMES[run[-1]]
            still = Image.open(frame_file(run, SWING_AT + MEASURE_AFTER_SECONDS)).convert("RGB")
            diff = diff_image(frame_file(run, SWING_AT + MEASURE_AFTER_SECONDS), frame_file(run, references[0]), GAIN)
            tiles.append(tile_with_text(with_bands(still, run, obj, 1.0), f"{OBJECTS[obj]['name']} · {camera_name} · 휘두른 뒤 2.5초"))
            tiles.append(tile_with_text(with_bands(diff, run, obj, 1.0), f"{OBJECTS[obj]['name']} · {camera_name} · 차이 × {GAIN}"))
        rows.append(tiles)
    canvas = Image.new("RGB", (TILE[0] * 4, TILE[1] * len(rows) + 30), (0, 0, 0))
    for r, tiles in enumerate(rows):
        for c, tile in enumerate(tiles):
            canvas.paste(tile, (c * TILE[0], r * TILE[1] + 30))
    draw = ImageDraw.Draw(canvas)
    draw.text((8, 4), "선: 가까운 쪽 띠(하늘색), 먼 쪽 띠(주황). 기준 장(계산 끔)과의 차이", fill=(255, 255, 255), font=C.load_korean_font(20))
    canvas.save(OUT / "obstacle_stills.png")
    print("stills", (OUT / "obstacle_stills.png").stat().st_size)


def shot_times(run, group="S"):
    record = json.loads((CAL / run / "run.json").read_text(encoding="utf-8"))
    return [shot["time"] for shot in record["shots"] if shot["group"] == group]


def save_within_limit(frames, path):
    for quality in C.CLIP_QUALITY_STEPS:
        C.save_animation(frames, path, quality)
        size = Path(path).stat().st_size
        if size <= LIMIT:
            return {"bytes": size, "quality": quality, "frames": len(frames), "size": list(frames[0].size)}
    return {"bytes": Path(path).stat().st_size, "quality": C.CLIP_QUALITY_STEPS[-1], "frames": len(frames), "size": list(frames[0].size), "over_limit": True}


def clip_frames(obj, runs, label):
    back_run, side_run = runs
    times = shot_times(back_run)
    assert times == shot_times(side_run)
    frames = []
    for time_value in times:
        canvas = Image.new("RGB", (TILE[0] * 2, TILE[1]))
        seconds = f"{label} {time_value - times[0]:.1f}초"
        for index, run in enumerate((back_run, side_run)):
            image = Image.open(frame_file(run, time_value)).convert("RGB")
            canvas.paste(tile_with_text(image, f"{OBJECTS[obj]['name']} · {CAMERA_NAMES[run[-1]]}", seconds), (index * TILE[0], 0))
        frames.append(canvas)
    return frames


def run_clips():
    report = {}
    for obj in OBJECTS:
        frames = clip_frames(obj, SWING_RUNS[obj], "휘두르기")
        if obj in WALK_RUNS:
            frames += clip_frames(obj, WALK_RUNS[obj], "걷기")
        report[OBJECTS[obj]["file"]] = save_within_limit(frames, OUT / OBJECTS[obj]["file"])
        report[OBJECTS[obj]["file"]]["verify"] = C.verify_animation(OUT / OBJECTS[obj]["file"]) if hasattr(C, "verify_animation") else None
    (WORK / "clips_report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2, default=str), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, default=str))


def run_sheets(run, start, end, columns, rows, with_diff):
    times = shot_times(run)
    base = times[0]
    count = columns * rows
    chosen = [min(times, key=lambda t: abs((t - base) - (start + (end - start) * i / max(count - 1, 1)))) for i in range(count)]
    record = json.loads((CAL / run / "run.json").read_text(encoding="utf-8"))
    reference = [shot["time"] for shot in record["shots"] if shot["group"] == "R"][0]
    canvas = Image.new("RGB", (TILE[0] * columns, TILE[1] * rows))
    if with_diff == "both":
        canvas = Image.new("RGB", (TILE[0] * columns, TILE[1] * 2))
        for index, time_value in enumerate(chosen[:columns]):
            frame = Image.open(frame_file(run, time_value)).convert("RGB")
            diff = diff_image(frame_file(run, time_value), frame_file(run, reference), GAIN)
            canvas.paste(tile_with_text(frame, f"{run}", f"{time_value - base:.1f}초"), (index * TILE[0], 0))
            canvas.paste(tile_with_text(diff, f"{run}", f"{time_value - base:.1f}초 차이×{GAIN}"), (index * TILE[0], TILE[1]))
        path = WORK / f"review_{run}_c.png"
        canvas.save(path)
        print(path)
        return
    for index, time_value in enumerate(chosen):
        if with_diff:
            image = diff_image(frame_file(run, time_value), frame_file(run, reference), GAIN)
        else:
            image = Image.open(frame_file(run, time_value)).convert("RGB")
        canvas.paste(tile_with_text(image, f"{run}", f"{time_value - base:.1f}초" + (f" 차이×{GAIN}" if with_diff else "")), ((index % columns) * TILE[0], (index // columns) * TILE[1]))
    path = WORK / f"review_{run}_{'d' if with_diff else 'f'}_{start:g}_{end:g}.png"
    canvas.save(path)
    print(path)


if __name__ == "__main__":
    command = sys.argv[1]
    if command == "numbers":
        run_numbers()
    elif command == "stills":
        run_stills()
    elif command == "clips":
        run_clips()
    elif command == "sheets":
        mode = sys.argv[7] if len(sys.argv) > 7 else ""
        run_sheets(sys.argv[2], float(sys.argv[3]), float(sys.argv[4]), int(sys.argv[5]), int(sys.argv[6]), "both" if mode == "both" else mode == "diff")
