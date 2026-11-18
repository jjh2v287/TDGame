"""실행 환경: 파이썬 3 + Pillow + numpy, 작업 폴더 C:/Project/TDGame (PYTHONUTF8=1 python -B)
실행 명령: python -B Docs/VolumeFog3D/measurements/stage1w/quality_measure.py numbers|window|clips|swingview
출력: Docs/VolumeFog3D/measurements/stage1w/quality.json(numbers·window 가 각자 칸을 갱신)·quality_3.webp·window_edge.png, 눈 확인용 그림은 Saved/AgentOps/20261114/fog3d/stage1w/s2/review_*.png
상태: 1 마무리 S2(prereg-1c-wrapup.md 2절 B). 품질 단계 숫자와 창 가장자리 밝기 차(정보 숫자, 판정선 없음, 1.0 이하는 안 보임 표시용 문턱)."""
import json
import math
import sys
from pathlib import Path

import numpy
from PIL import Image, ImageDraw

ROOT = Path("C:/Project/TDGame")
sys.path.insert(0, str(ROOT / "Tools"))
sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import fog3d_calib_clips as C
import fog3d_calib_report as R
import obstacle_measure as OM

CAL = ROOT / "Saved/FluidFX/fog3d_calib"
OUT = Path(__file__).resolve().parent
WORK = ROOT / "Saved/AgentOps/20261114/fog3d/stage1w/s2"
QUALITY_JSON = OUT / "quality.json"
TILE = (480, 270)
CELL_CM = 15.625
STEP_CELLS = 16
ROUNDING_CELLS = 8
BAND_CM = 100.0
CORNER_CM = 100.0
FOG_TOP_CM = OM.FOG_TOP_PRISM_CM
VISIBLE_THRESHOLD = 1.0
MIN_PIXELS = 200
CONTROL_OFFSETS_CM = (-900.0, -600.0, -300.0, 300.0, 600.0)
START_XY = (-1000.0, 0.0)
STILL_AT = 3.6
SWING_AFTER_STILL = 6.5
PAWN_RADIUS_CM = OM.PAWN_RADIUS_CM
PAWN_HEIGHT_CM = OM.PAWN_HEIGHT_CM
ZONE_HALF_CM = 3000.0
TIERS = {0: {"name": "낮음", "cells": 128, "rate": 30}, 1: {"name": "보통", "cells": 256, "rate": 30}, 2: {"name": "높음", "cells": 256, "rate": 60}}
WINDOW_QUALITIES = (0, 2)
ARMS = (500, 1000, 2000)
EDGE_COLORS = {"+Y": (255, 220, 0), "-Y": (255, 120, 0), "+X": (0, 230, 255), "-X": (255, 0, 200)}
OBJECT_BOXES = [spec for spec in OM.OBJECTS.values()]


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def read_quality():
    return read_json(QUALITY_JSON) if QUALITY_JSON.exists() else {}


def write_quality(key, value):
    data = read_quality()
    data[key] = value
    QUALITY_JSON.write_text(json.dumps(data, ensure_ascii=False, indent=2, default=str), encoding="utf-8")


def read_state(run):
    return read_json(WORK / f"state_{run}.json")


def luma709(path):
    rgb = numpy.asarray(Image.open(path).convert("RGB"), dtype=numpy.float64)
    return 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]


def check_dense_like(run):
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
    try:
        state = read_state(run)
        result["state"] = {key: state.get(key) for key in ("actor_location", "cam_world_location", "cam_world_rotation", "cam_fov", "state_target", "TD.Fog3D.Quality", "TD.Fog3D.Mode", "boom_target_arm_length", "error")}
    except (OSError, ValueError):
        result["reasons"].append("상태 읽기 파일 없음")
    result["valid"] = not result["reasons"]
    return result


def registered_numbers(level):
    name = f"QB_q{level}"
    options = R.parse_arguments(["--runs", "x", "--no-images", "--out-root", str(CAL)])
    report, matched, record, lumas = R.analyze_run(CAL / name, options)
    numbers = report["numbers"]
    split = R.split_walk_numbers(lumas)
    state = read_state(name)
    log = R.read_run_log(CAL / name, False, None)
    return {
        "name": name, "valid": bool(report["valid"]), "invalid_reasons": report["invalid_reasons"],
        "Wk": numbers["Wk"], "Wk_12.1": numbers["Wk"]["W12.1"], "Sk": numbers["Sk"], "Sk_2.5": numbers["Sk"]["S2.5"], "Sk2": numbers["Sk2"], "Sk2_2.5": numbers["Sk2"]["S2.5"],
        "N_W": numbers["noise_walk_N_W"], "N_S": numbers["noise_swing_N_S"], "Tx": numbers["Tx"], "Tx2": numbers["Tx2"],
        "refill_ratio_swing": numbers["refill_ratio_swing"], "refill_ratio_walk": numbers["refill_ratio_walk"],
        "Wk_L": split["Wk_L"], "Wk_R": split["Wk_R"], "asymmetry": split["asymmetry"],
        "log": {key: log.get(key) for key in ("source", "submitted", "rejected", "fog3d_warning_or_error_lines", "usable")},
        "state_target": state.get("state_target"), "console_quality_read": state.get("TD.Fog3D.Quality"), "console_mode_read": state.get("TD.Fog3D.Mode"),
    }


def run_numbers():
    tiers = {}
    for level in (0, 1, 2):
        row = registered_numbers(level)
        row["tier"] = TIERS[level]
        dense = check_dense_like(f"QD_q{level}")
        dense["tier"] = TIERS[level]
        row["dense_check"] = dense
        tiers[str(level)] = row
    high_submitted = tiers["2"]["log"]["submitted"]
    for level in ("0", "1", "2"):
        submitted = tiers[level]["log"]["submitted"]
        tiers[level]["submitted_ratio_to_high"] = submitted / high_submitted if high_submitted else None
        dense_submitted = tiers[level]["dense_check"]["log"]["submitted"]
        tiers[level]["dense_submitted_ratio_to_high"] = dense_submitted / tiers["2"]["dense_check"]["log"]["submitted"] if tiers["2"]["dense_check"]["log"]["submitted"] else None
    write_quality("b1", {
        "note": "정보 숫자(판정선 없음). Wk(t)·Sk(t)·Sk2(t)·좌우 치우침은 등록본 prereg-1c.md 3절·prereg-1c-r2.md·prereg-1c-add1.md 식 그대로(뒤 카메라, 옆빛, 새 기본값, 8비트 밝기). 초당 계산 횟수는 로그 제출 수의 높음 대비 비율로 확인(높음 60회/초, 낮음·보통 30회/초가 기대).",
        "tiers": tiers,
    })
    for level, row in tiers.items():
        print(level, row["valid"], round(row["Wk_12.1"], 3), round(row["Sk_2.5"], 3), round(row["Sk2_2.5"], 3), round(row["asymmetry"], 3), row["state_target"], row["log"], round(row["submitted_ratio_to_high"], 3))
        print("   dense", row["dense_check"]["valid"], row["dense_check"]["reasons"], row["dense_check"]["found_shots"], round(row["dense_submitted_ratio_to_high"], 3))


def floor_div(value, divisor):
    return value // divisor


def window_rectangle(cells):
    player_cell = (math.floor(START_XY[0] / CELL_CM), math.floor(START_XY[1] / CELL_CM))
    minimum = [floor_div(player_cell[axis] - cells // 2 + ROUNDING_CELLS, STEP_CELLS) * STEP_CELLS for axis in (0, 1)]
    return {"x": (minimum[0] * CELL_CM, (minimum[0] + cells) * CELL_CM), "y": (minimum[1] * CELL_CM, (minimum[1] + cells) * CELL_CM), "player_cell": list(player_cell), "min_cell": minimum}


def edge_definitions(rectangle, level):
    x0, x1 = rectangle["x"]
    y0, y1 = rectangle["y"]
    edges = {
        "+Y": {"axis": "y", "line": y1, "outward": 1, "along": (x0 + CORNER_CM, x1 - CORNER_CM)},
        "-Y": {"axis": "y", "line": y0, "outward": -1, "along": (x0 + CORNER_CM, x1 - CORNER_CM)},
        "+X": {"axis": "x", "line": x1, "outward": 1, "along": (y0 + CORNER_CM, y1 - CORNER_CM)},
        "-X": {"axis": "x", "line": x0, "outward": -1, "along": (y0 + CORNER_CM, y1 - CORNER_CM)},
    }
    for name, edge in edges.items():
        edge["coincides_with_zone_edge"] = abs(abs(edge["line"]) - ZONE_HALF_CM) < 1.0
    return edges


def band_prism(edge, side, floor_z):
    line, outward = edge["line"], edge["outward"]
    if side == "inner":
        low, high = (line - BAND_CM, line) if outward > 0 else (line, line + BAND_CM)
    else:
        low, high = (line, line + BAND_CM) if outward > 0 else (line - BAND_CM, line)
    along_low, along_high = edge["along"]
    if edge["axis"] == "y":
        return (along_low, low, floor_z), (along_high, high, floor_z + FOG_TOP_CM)
    return (low, along_low, floor_z), (high, along_high, floor_z + FOG_TOP_CM)


def window_camera(state, level, arm, width, height, moved_by_swing):
    pawn_end = numpy.array(state["actor_location"], dtype=float)
    camera_end = numpy.array(state["cam_world_location"], dtype=float)
    relative = camera_end - pawn_end
    pitch = math.radians(state["cam_world_rotation"][0])
    predicted = numpy.array([-arm * math.cos(pitch), 0.0, arm * -math.sin(pitch)])
    pawn_still = numpy.array([START_XY[0], START_XY[1], pawn_end[2]])
    location = camera_end if moved_by_swing else pawn_still + relative
    camera = OM.Camera(location.tolist(), state["cam_world_rotation"], state["cam_fov"], width, height)
    return camera, {"camera_minus_pawn": relative.tolist(), "predicted_from_arm": predicted.tolist(), "max_abs_difference_cm": float(numpy.abs(relative - predicted).max()),
                    "pawn_end_xy": pawn_end[:2].tolist(), "pawn_push_cm": float(numpy.linalg.norm(pawn_end[:2] - numpy.array(START_XY)))}


def edge_pixels(camera, directions, edge, floor_z, pawn_xy):
    inner_low, inner_high = band_prism(edge, "inner", floor_z)
    outer_low, outer_high = band_prism(edge, "outer", floor_z)
    inner_hit, inner_t = OM.box_hit(camera, directions, inner_low, inner_high)
    outer_hit, outer_t = OM.box_hit(camera, directions, outer_low, outer_high)
    blocked_inner = numpy.zeros_like(inner_hit)
    blocked_outer = numpy.zeros_like(outer_hit)
    for spec in OBJECT_BOXES:
        box_hit, box_t = OM.box_hit(camera, directions, (spec["x"][0], spec["y"][0], floor_z), (spec["x"][1], spec["y"][1], floor_z + spec["height"]))
        blocked_inner |= box_hit & (box_t <= inner_t)
        blocked_outer |= box_hit & (box_t <= outer_t)
    pawn_hit = OM.cylinder_hit(camera, directions, pawn_xy, PAWN_RADIUS_CM, floor_z, floor_z + PAWN_HEIGHT_CM)
    both = inner_hit & outer_hit
    inner = inner_hit & ~blocked_inner & ~pawn_hit & ~both
    outer = outer_hit & ~blocked_outer & ~pawn_hit & ~both
    return inner, outer, {"inner_raw": int(inner_hit.sum()), "outer_raw": int(outer_hit.sum()), "blocked_by_objects": int((inner_hit & blocked_inner).sum() + (outer_hit & blocked_outer).sum()), "blocked_by_pawn": int((inner_hit & pawn_hit).sum() + (outer_hit & pawn_hit).sum())}


def measure_window(level, arm):
    run = f"WQ{level}_a{arm}"
    state = read_state(run)
    frame_path = CAL / run / "capture" / f"00_c_{int(round(STILL_AT * 1000)):05d}ms.png"
    luma = luma709(frame_path)
    height, width = luma.shape
    camera, camera_check = window_camera(state, level, arm, width, height, False)
    directions = camera.ray_grid()
    floor_z = OM.floor_height(state)
    rectangle = window_rectangle(TIERS[level]["cells"])
    edges = edge_definitions(rectangle, level)
    pawn_xy = (START_XY[0], START_XY[1])
    result = {"run": run, "level": level, "tier": TIERS[level], "arm_cm": arm, "image": [width, height], "window_x_cm": rectangle["x"], "window_y_cm": rectangle["y"], "player_cell": rectangle["player_cell"],
              "min_cell": rectangle["min_cell"], "floor_z_cm": floor_z, "camera_check": camera_check, "camera_world_location": camera.loc.tolist(), "edges": {}}
    pooled_inner, pooled_outer = [], []
    for name, edge in edges.items():
        entry = {"line_cm": edge["line"], "along_cm": edge["along"], "coincides_with_zone_edge": edge["coincides_with_zone_edge"]}
        if edge["coincides_with_zone_edge"]:
            entry["used"] = False
            entry["reason"] = "구역 가장자리(±3000cm)와 겹쳐 창 가장자리만 가를 수 없어 뺌"
            result["edges"][name] = entry
            continue
        inner, outer, counts = edge_pixels(camera, directions, edge, floor_z, pawn_xy)
        entry.update(counts)
        entry["inner_pixels"] = int(inner.sum())
        entry["outer_pixels"] = int(outer.sum())
        if entry["inner_pixels"] < MIN_PIXELS or entry["outer_pixels"] < MIN_PIXELS:
            entry["used"] = False
            entry["reason"] = f"화면에 보이는 화소가 {MIN_PIXELS}개 미만"
            result["edges"][name] = entry
            continue
        entry["used"] = True
        entry["inner_mean"] = float(luma[inner].mean())
        entry["outer_mean"] = float(luma[outer].mean())
        entry["difference"] = entry["inner_mean"] - entry["outer_mean"]
        pooled_inner.append(luma[inner])
        pooled_outer.append(luma[outer])
        result["edges"][name] = entry
    controls = []
    for name, edge in edges.items():
        if edge["coincides_with_zone_edge"]:
            continue
        for offset in CONTROL_OFFSETS_CM:
            shifted = dict(edge)
            shifted["line"] = edge["line"] + edge["outward"] * offset
            inner, outer, _ = edge_pixels(camera, directions, shifted, floor_z, pawn_xy)
            if inner.sum() < MIN_PIXELS or outer.sum() < MIN_PIXELS:
                continue
            controls.append({"edge": name, "offset_outward_cm": offset, "inner_pixels": int(inner.sum()), "outer_pixels": int(outer.sum()), "difference": float(luma[inner].mean() - luma[outer].mean())})
    result["controls"] = controls
    if controls:
        magnitudes = sorted(abs(c["difference"]) for c in controls)
        result["control_abs_median"] = float(numpy.median(magnitudes))
        result["control_abs_max"] = float(magnitudes[-1])
    if pooled_inner:
        inner_all = numpy.concatenate(pooled_inner)
        outer_all = numpy.concatenate(pooled_outer)
        result["pooled"] = {"inner_pixels": int(inner_all.size), "outer_pixels": int(outer_all.size), "inner_mean": float(inner_all.mean()), "outer_mean": float(outer_all.mean()), "difference": float(inner_all.mean() - outer_all.mean())}
        used = [e["difference"] for e in result["edges"].values() if e.get("used")]
        result["worst_edge_abs"] = max(abs(v) for v in used)
        result["visible_by_pooled"] = abs(result["pooled"]["difference"]) > VISIBLE_THRESHOLD
        result["visible_by_worst_edge"] = result["worst_edge_abs"] > VISIBLE_THRESHOLD
    return result


def run_window():
    rows = {}
    for level in WINDOW_QUALITIES:
        for arm in ARMS:
            row = measure_window(level, arm)
            check = check_dense_like(f"WQ{level}_a{arm}")
            row["run_check"] = check
            rows[f"q{level}_a{arm}"] = row
            pooled = row.get("pooled", {})
            print(row["run"], "valid", check["valid"], check["reasons"], "window", [round(v) for v in row["window_x_cm"]], [round(v) for v in row["window_y_cm"]], "pooled", None if not pooled else round(pooled["difference"], 3), "ctl_med", round(row.get("control_abs_median", -1), 2), "ctl_max", round(row.get("control_abs_max", -1), 2),
                  {n: (round(e["difference"], 2) if e.get("used") else e.get("reason", "")[:20]) for n, e in row["edges"].items()}, "cam diff", round(row["camera_check"]["max_abs_difference_cm"], 2), "push", round(row["camera_check"]["pawn_push_cm"], 1))
    write_quality("b2", {"note": "창 가장자리 밝기 차(8비트, Rec.709 밝기) = 가장자리 선의 안쪽 100cm 띠 평균 − 바깥쪽 100cm 띠 평균. 띠는 바닥~135cm 높이 상자(1b 안개 높이 상자와 같은 135cm), 선 방향은 창 모서리에서 100cm 안쪽까지, 폰·물체가 앞에 찍힌 화소는 뺌. 가만히 선 장(휘두르기 전, 계산 켠 지 3.0초). 1.0 이하면 안 보임(표시용 문턱). 대표 = 쓸 수 있는 가장자리 전부의 화소를 합친 평균 차, 최악 = 가장자리별 차이의 절댓값 최댓값.", "rows": rows})


def draw_edges(image, camera, rectangle, edges, floor_z):
    draw = ImageDraw.Draw(image, "RGBA")
    scale_x = image.size[0] / camera.w
    scale_y = image.size[1] / camera.h
    for name, edge in edges.items():
        if edge["coincides_with_zone_edge"]:
            continue
        along = numpy.linspace(rectangle_along(rectangle, edge)[0], rectangle_along(rectangle, edge)[1], 200)
        points = []
        for value in along:
            point = (value, edge["line"], floor_z) if edge["axis"] == "y" else (edge["line"], value, floor_z)
            px, py, depth = camera.project(point)
            points.append((px * scale_x, py * scale_y) if depth > 1.0 else None)
        segment = []
        for p in points + [None]:
            if p is None or not (-2000 < p[0] < image.size[0] + 2000 and -2000 < p[1] < image.size[1] + 2000):
                if len(segment) > 1:
                    draw.line(segment, fill=EDGE_COLORS[name] + (230,), width=2)
                segment = []
            else:
                segment.append(p)
    return image


def rectangle_along(rectangle, edge):
    return edge["along"]


def edge_tile(level, arm, frame_time, moved_by_swing, label_right):
    run = f"WQ{level}_a{arm}"
    state = read_state(run)
    image = Image.open(CAL / run / "capture" / f"00_c_{int(round(frame_time * 1000)):05d}ms.png").convert("RGB")
    camera, _ = window_camera(state, level, arm, image.size[0], image.size[1], moved_by_swing)
    rectangle = window_rectangle(TIERS[level]["cells"])
    edges = edge_definitions(rectangle, level)
    scaled = image.resize((640, 360), Image.LANCZOS)
    draw_edges(scaled, camera, rectangle, edges, OM.floor_height(state))
    font = C.load_korean_font(22)
    C.draw_badge(scaled, f"{TIERS[level]['name']} {TIERS[level]['cells']}칸 · 카메라 팔 {arm}cm", font, "left")
    if label_right:
        C.draw_badge(scaled, label_right, font, "right")
    return scaled


def run_edge_sheet(frame_time, moved_by_swing, path, caption):
    columns = len(ARMS)
    rows = len(WINDOW_QUALITIES)
    sheet = Image.new("RGB", (640 * columns, 360 * rows + 34), (24, 24, 24))
    for row_index, level in enumerate(WINDOW_QUALITIES):
        for column_index, arm in enumerate(ARMS):
            tile = edge_tile(level, arm, frame_time, moved_by_swing, caption)
            sheet.paste(tile, (column_index * 640, row_index * 360))
    legend = ImageDraw.Draw(sheet)
    font = C.load_korean_font(20)
    legend.text((10, 360 * rows + 6), "선 = 바닥 높이의 창 가장자리 (노랑 +Y, 주황 -Y, 하늘 +X, 분홍 -X). 높음의 -X 가장자리는 구역 가장자리와 겹쳐 그리지 않음", fill=(230, 230, 230), font=font)
    sheet.save(path)
    print("saved", path, sheet.size)


def run_clips():
    runs = [f"QD_q{level}" for level in (0, 1, 2)]
    times = [shot["time"] for shot in read_json(CAL / runs[0] / "run.json")["shots"]]
    for run in runs[1:]:
        assert [shot["time"] for shot in read_json(CAL / run / "run.json")["shots"]] == times
    walk_start = 7.3
    frames = []
    for time_value in times:
        canvas = Image.new("RGB", (TILE[0] * 3, TILE[1]))
        label = f"휘두르기 {time_value:.1f}초" if time_value < walk_start - 0.05 else f"걷기 {time_value - walk_start:.1f}초"
        for index, run in enumerate(runs):
            level = index
            image = Image.open(OM.frame_file(run, time_value)).convert("RGB")
            tile = tile_sized(image, f"{TIERS[level]['name']} · {TIERS[level]['cells']}칸 · {TIERS[level]['rate']}회/초", label)
            canvas.paste(tile, (index * TILE[0], 0))
        frames.append(canvas)
    info = OM.save_within_limit(frames, OUT / "quality_3.webp")
    info["verify"] = C.verify_animation(OUT / "quality_3.webp") if hasattr(C, "verify_animation") else None
    write_quality("clip", info)
    print(json.dumps(info, ensure_ascii=False, default=str))


def tile_sized(image, left_text, right_text):
    tile = image.resize(TILE, Image.LANCZOS)
    font = C.load_korean_font(max(16, TILE[1] // 15))
    C.draw_badge(tile, left_text, font, "left")
    C.draw_badge(tile, right_text, font, "right")
    return tile


if __name__ == "__main__":
    command = sys.argv[1]
    if command == "numbers":
        run_numbers()
    elif command == "window":
        run_window()
    elif command == "clips":
        run_clips()
    elif command == "edgesheet":
        run_edge_sheet(STILL_AT, False, OUT / "window_edge.png", "가만히 선 장")
    elif command == "swingview":
        run_edge_sheet(SWING_AFTER_STILL, True, WORK / "review_window_swing.png", "휘두른 뒤 2.5초")
