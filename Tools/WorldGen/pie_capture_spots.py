"""에디터 밖에서 실행: 지점 목록(Tools/WorldGen/topdown_spots.json)의 각 지점으로 PIE 폰을 옮겨 촬영하고 카메라 가림 목록·어두운 화소 비율을 잰다. --compare로 이전 실행과 지점별 변화 표를 만든다(P3-17, P3-19 연속 세계).
실행: python Tools/WorldGen/pie_capture_spots.py [--run 이름] [--ids F01,B01] [--regions field,PaleHeights] [--compare 이전 run 폴더 또는 이름] [--offline] [--overwrite] [--exposure-offset EV(PIE 시작 뒤 r.ExposureOffset 설정, 종료 때 0으로 복귀, run.json·summary.md에 기록)] (PowerShell, 에디터 실행 중이고 다른 갈래가 에디터를 쓰지 않을 때, 에디터 창을 앞에 둔다)
출력: Saved/WorldGen/PieShots/<run>/ 의 지점별 PNG·<지점 ID>.json·run.json·summary.md(--compare 이면 compare.md도). 종료 코드 0 = 전 지점 성공, 1 = 실패 지점 있음, 2 = PIE 시작 실패
상태: 현행 (2026-10-15)

절차: StartPIE → 지역마다 [travel이 console_gate면 콘솔 관문 이동 → 지점마다 move → settle초 대기 → shot → 가림 목록(occluders) → 어두운 비율 계산] → (console_gate면 필드 귀환) → StopPIE. 에디터 쪽 동작은 editor_pie_probe.py. 연속 세계(D-45)의 지점 목록은 모든 지역이 teleport다.
이동 높이: 지점에 z_cm이 없으면 높이맵 지면 + 150cm. 지점 지역의 격자를 먼저 보고, 그 격자 밖이면(이계 소유지만 필드 상자 안인 갱도 바닥 등) 그 점을 담는 다른 지역 격자를 쓴다. 이계 height.r16은 이계 로컬 높이라 layout.json realm.center_cm의 z(Zoff)를 더한다.
어두운 화소 = 휘도(0~1, 0.2126R+0.7152G+0.0722B) 0.06 미만의 비율, 중앙 휘도 = 화면 가로·세로 가운데 30% 사각형의 평균 휘도. 가림 수 = 카메라→폰 가슴(지면+120cm) 반지름 30cm 구 쓸기에 걸린 지형 외 (액터, 구성 요소, 인스턴스 번호) 수. 매몰 = 카메라 점 겹침·위쪽 광선 내부 판정 또는 카메라가 지형 아래.
--offline은 PIE 없이 --run 폴더의 지점 JSON으로 summary.md(와 --compare)만 다시 만든다. 이미 있는 run 폴더는 --overwrite 없이는 덮어쓰지 않는다.
"""
import argparse
import glob
import json
import math
import os
import subprocess
import sys
import time
import uuid

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOLS = os.path.join(ROOT, "Tools")
WORLDGEN = os.path.join(TOOLS, "WorldGen")
sys.path.insert(0, WORLDGEN)
sys.path.insert(0, TOOLS)
import pie_p1_checks as pie_checks  # noqa: E402

RUN_IN_EDITOR = os.path.join(TOOLS, "run_in_editor.py")
PROBE_SCRIPT = os.path.join(WORLDGEN, "editor_pie_probe.py")
GROUND_RETRY_COUNT = 10
GROUND_RETRY_SECONDS = 2.0
REQUEST_PATH = os.path.join(ROOT, "Saved", "WorldGen", "pie_probe_request.json")
RESULT_PATH = os.path.join(ROOT, "Saved", "WorldGen", "pie_probe_result.json")
SHOT_ROOT = os.path.join(ROOT, "Saved", "WorldGen", "PieShots")
DEFAULT_SPOTS = os.path.join(WORLDGEN, "topdown_spots.json")

HEIGHT_RAW_ZERO = 32768.0
HEIGHT_RAW_PER_100CM = 128.0
DARK_LUMINANCE = 0.06
CENTER_FRACTION = 0.30
SPOT_CLEARANCE_CM = 150.0
SHOT_WIDTH = 1600
SHOT_HEIGHT = 900
PIE_START_TIMEOUT_SECONDS = 150
PROBE_RESULT_TIMEOUT_SECONDS = 30.0
PROBE_PROCESS_TIMEOUT_SECONDS = 180
SHOT_FILE_TIMEOUT_SECONDS = 30.0
TRAVEL_WAIT_SECONDS = 120
AFTER_ENTER_REALM_SECONDS = 8.0
AFTER_RETURN_FIELD_SECONDS = 5.0
WRITE_RETRIES = 5
WRITE_RETRY_SECONDS = 2.0
TRAVEL_METHODS = ("teleport", "console_gate")
MAIN_OCCLUDER_COUNT = 3
OFF_TARGET_WARN_CM = 1000.0
EXPOSURE_VARIABLE = "r.ExposureOffset"
EXPOSURE_RESET_VALUE = 0.0


class ProbeError(RuntimeError):
    pass


class PieStartError(RuntimeError):
    pass


def log(message):
    print(message, flush=True)


def read_json(path):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def write_text(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    temp_path = path + ".tmp"
    last_error = None
    for _ in range(WRITE_RETRIES):
        try:
            with open(temp_path, "w", encoding="utf-8", newline="\n") as handle:
                handle.write(text)
            os.replace(temp_path, path)
            return
        except OSError as error:
            last_error = error
            time.sleep(WRITE_RETRY_SECONDS)
    raise last_error


def write_json(path, payload):
    write_text(path, json.dumps(payload, ensure_ascii=False, indent=1) + "\n")


def resolve_project_path(path):
    if os.path.isabs(path):
        return path
    return os.path.join(ROOT, path)


def load_spots(path):
    data = read_json(path)
    regions = data["regions"]
    spots = data["spots"]
    for region_name, region in regions.items():
        method = region.get("travel", {}).get("method")
        if method not in TRAVEL_METHODS:
            raise ValueError(f"region {region_name}: travel.method must be one of {TRAVEL_METHODS}")
        if method == "console_gate" and not region["travel"].get("gate_id"):
            raise ValueError(f"region {region_name}: console_gate needs gate_id")
    seen = set()
    for spot in spots:
        for key in ("id", "region", "x_cm", "y_cm"):
            if key not in spot:
                raise ValueError(f"spot missing {key}: {spot}")
        if spot["region"] not in regions:
            raise ValueError(f"spot {spot['id']}: unknown region {spot['region']}")
        if spot["id"] in seen:
            raise ValueError(f"duplicate spot id {spot['id']}")
        seen.add(spot["id"])
    return regions, spots


def describe_project_path(path):
    normalized = path.replace("\\", "/")
    root = ROOT.replace("\\", "/") + "/"
    return normalized[len(root):] if normalized.startswith(root) else normalized


def split_tokens(text):
    if not text:
        return []
    return [token.strip() for token in text.split(",") if token.strip()]


def spot_matches_token(spot, token):
    return spot["id"] == token or spot["id"].startswith(token + "_")


def select_spots(spots, ids_text, regions_text):
    id_tokens = split_tokens(ids_text)
    region_tokens = split_tokens(regions_text)
    unknown = [token for token in id_tokens if not any(spot_matches_token(spot, token) for spot in spots)]
    if unknown:
        raise ValueError(f"unknown spot ids: {unknown}")
    selected = []
    for spot in spots:
        if id_tokens and not any(spot_matches_token(spot, token) for token in id_tokens):
            continue
        if region_tokens and spot["region"] not in region_tokens:
            continue
        selected.append(spot)
    if not selected:
        raise ValueError("no spots selected")
    return selected


def load_height_grid(region):
    height_path = resolve_project_path(region["height_file"])
    if not os.path.exists(height_path):
        log(f"height file missing, editor will trace ground: {height_path}")
        return None
    samples = int(region["samples"])
    raw = np.fromfile(height_path, dtype="<u2")
    if raw.size != samples * samples:
        raise ValueError(f"{height_path}: {raw.size} values, expected {samples}x{samples}")
    center = (0.0, 0.0, 0.0)
    if region.get("layout_file"):
        layout_center = read_json(resolve_project_path(region["layout_file"]))["realm"]["center_cm"]
        center = (float(layout_center[0]), float(layout_center[1]), float(layout_center[2]) if len(layout_center) > 2 else 0.0)
    return {"raw": raw.reshape(samples, samples), "samples": samples, "center": center, "cell_cm": float(region.get("cell_cm", 100.0))}


def sample_grid_cm(grid, x_cm, y_cm):
    half = (grid["samples"] - 1) / 2.0
    row = int(round((y_cm - grid["center"][1]) / grid["cell_cm"] + half))
    column = int(round((x_cm - grid["center"][0]) / grid["cell_cm"] + half))
    if not (0 <= row < grid["samples"] and 0 <= column < grid["samples"]):
        return None
    return (float(grid["raw"][row, column]) - HEIGHT_RAW_ZERO) / HEIGHT_RAW_PER_100CM * 100.0 + grid["center"][2]


def build_ground_lookup(regions):
    grids = {}

    def grid_for(region_name):
        if region_name not in grids:
            grids[region_name] = load_height_grid(regions[region_name])
        return grids[region_name]

    def ground_cm(region_name, x_cm, y_cm):
        for name in [region_name] + [other for other in regions if other != region_name]:
            grid = grid_for(name)
            height = None if grid is None else sample_grid_cm(grid, x_cm, y_cm)
            if height is not None:
                return height
        return None

    return ground_cm


def read_probe_result(request_id):
    if not os.path.exists(RESULT_PATH):
        return None
    try:
        result = read_json(RESULT_PATH)
    except (OSError, ValueError):
        return None
    if result.get("request_id") != request_id:
        return None
    return result


def call_probe(request, result_timeout_seconds=PROBE_RESULT_TIMEOUT_SECONDS):
    request_id = uuid.uuid4().hex[:10]
    if os.path.exists(RESULT_PATH):
        os.remove(RESULT_PATH)
    write_json(REQUEST_PATH, dict(request, request_id=request_id))
    completed = subprocess.run([sys.executable, RUN_IN_EDITOR, PROBE_SCRIPT], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=PROBE_PROCESS_TIMEOUT_SECONDS)
    if completed.returncode != 0:
        tail = " | ".join(line.strip() for line in completed.stdout.splitlines() if line.strip())[-300:]
        raise ProbeError(f"run_in_editor exit {completed.returncode}: {tail}")
    deadline = time.time() + result_timeout_seconds
    while time.time() < deadline:
        result = read_probe_result(request_id)
        if result is not None:
            if not result.get("ok"):
                raise ProbeError(f"{request.get('action')}: {result.get('error')}")
            return result
        time.sleep(0.5)
    raise ProbeError(f"{request.get('action')}: no result within {result_timeout_seconds}s")


def start_pie(warmup_seconds):
    log(f"StartPIE {str(pie_checks.pie('StartPIE'))[:100]}")
    if not pie_checks.wait_for_pie(PIE_START_TIMEOUT_SECONDS):
        stop_pie()
        raise PieStartError("PIE did not start")
    time.sleep(warmup_seconds)


def stop_pie():
    log(f"StopPIE {str(pie_checks.pie('StopPIE'))[:100]}")


def set_exposure_offset(exposure_offset):
    if exposure_offset is None:
        return
    result = call_probe({"action": "console", "variable": EXPOSURE_VARIABLE, "value": exposure_offset})
    log(f"{EXPOSURE_VARIABLE} {exposure_offset:g} read_back={result.get('read_back')}")


def reset_exposure_offset(exposure_offset):
    if exposure_offset is None:
        return
    try:
        result = call_probe({"action": "console", "variable": EXPOSURE_VARIABLE, "value": EXPOSURE_RESET_VALUE})
        log(f"{EXPOSURE_VARIABLE} reset read_back={result.get('read_back')}")
    except Exception as error:
        log(f"{EXPOSURE_VARIABLE} reset failed, set it to {EXPOSURE_RESET_VALUE:g} by hand: {error!r}")


def wait_for_file(path, timeout_seconds):
    deadline = time.time() + timeout_seconds
    last_size = -1
    while time.time() < deadline:
        size = os.path.getsize(path) if os.path.exists(path) else -1
        if size > 0 and size == last_size:
            return True
        last_size = size
        time.sleep(0.7)
    return False


def measure_darkness(png_path):
    with Image.open(png_path) as image:
        pixels = np.asarray(image.convert("RGB"), dtype=np.float32) / 255.0
    luminance = 0.2126 * pixels[..., 0] + 0.7152 * pixels[..., 1] + 0.0722 * pixels[..., 2]
    height, width = luminance.shape
    margin = (1.0 - CENTER_FRACTION) / 2.0
    center = luminance[int(height * margin):int(height * (1.0 - margin)), int(width * margin):int(width * (1.0 - margin))]
    return {
        "dark_ratio": round(float((luminance < DARK_LUMINANCE).mean()), 4),
        "center_mean_luminance": round(float(center.mean()), 4),
        "mean_luminance": round(float(luminance.mean()), 4),
    }


def offset_from_target_cm(spot, pawn):
    return round(math.hypot(pawn[0] - spot["x_cm"], pawn[1] - spot["y_cm"]), 1)


def build_spot_record(spot, move_result, occluder_result, darkness):
    inside = occluder_result["camera_inside"]
    record = {
        "id": spot["id"],
        "region": spot["region"],
        "description": spot.get("description", ""),
        "requested_cm": [spot["x_cm"], spot["y_cm"]],
        "error": None,
        "png": spot["id"] + ".png",
        "pawn": occluder_result["pawn"],
        "offset_cm": offset_from_target_cm(spot, occluder_result["pawn"]),
        "ground_z": occluder_result["ground_z"],
        "camera": occluder_result["camera"],
        "camera_above_ground_cm": occluder_result["camera_above_ground_cm"],
        "occluder_count": occluder_result["occluder_count"],
        "occluders": occluder_result["occluders"],
        "camera_inside": inside,
        "buried": bool(inside["buried"]),
        "buried_in_mesh": bool(inside["buried_in_mesh"]),
        "under_terrain": bool(inside["under_terrain"]),
        "moved_to": move_result["pawn"],
    }
    record.update(darkness)
    return record


def build_failure_record(spot, message):
    return {"id": spot["id"], "region": spot["region"], "description": spot.get("description", ""), "requested_cm": [spot["x_cm"], spot["y_cm"]], "error": message}


def capture_spot(spot, ground_cm, out_dir, settle_seconds, refine):
    z_cm = spot.get("z_cm")
    if z_cm is None:
        ground = ground_cm(spot["region"], spot["x_cm"], spot["y_cm"])
        z_cm = None if ground is None else ground + SPOT_CLEARANCE_CM
    move_request = {"action": "move", "x": spot["x_cm"], "y": spot["y_cm"], "z": z_cm, "refine": refine}
    move_result = call_probe(move_request)
    for attempt in range(GROUND_RETRY_COUNT):
        if move_result.get("ground_z") is not None:
            break
        if attempt:
            time.sleep(GROUND_RETRY_SECONDS)
        move_result = call_probe(dict(move_request, z=None, refine=True))
    time.sleep(settle_seconds)
    png_path = os.path.join(out_dir, spot["id"] + ".png")
    if os.path.exists(png_path):
        os.remove(png_path)
    call_probe({"action": "shot", "path": png_path, "width": SHOT_WIDTH, "height": SHOT_HEIGHT})
    if not wait_for_file(png_path, SHOT_FILE_TIMEOUT_SECONDS):
        raise ProbeError(f"PNG not written: {png_path}")
    occluder_result = call_probe({"action": "occluders"})
    return build_spot_record(spot, move_result, occluder_result, measure_darkness(png_path))


def enter_region(travel, boxes):
    if travel["method"] == "teleport":
        return True
    gate_id = travel["gate_id"]
    log(pie_checks.run("pie_walkin_check.py", f"to {gate_id}", quiet=True).replace("\n", " | "))
    arrived = pie_checks.wait_for_arrival(lambda pawn: pie_checks.is_inside_destination(gate_id, pawn, boxes), TRAVEL_WAIT_SECONDS)
    log(f"travel {gate_id} ok={arrived}")
    time.sleep(AFTER_ENTER_REALM_SECONDS)
    return arrived


def leave_region(travel, boxes):
    if travel["method"] == "teleport":
        return
    log(pie_checks.run("pie_walkin_check.py", "field", quiet=True).replace("\n", " | "))
    pie_checks.wait_for_arrival(lambda pawn: pie_checks.is_inside_field(pawn, boxes), TRAVEL_WAIT_SECONDS)
    time.sleep(AFTER_RETURN_FIELD_SECONDS)


def capture_region(region_name, region, group, ground_cm, boxes, out_dir, options):
    travel = region["travel"]
    entered = enter_region(travel, boxes)
    records = []
    for spot in group:
        if not entered:
            records.append(build_failure_record(spot, f"region travel failed: {region_name}"))
            continue
        try:
            record = capture_spot(spot, ground_cm, out_dir, options.settle, options.refine)
        except Exception as error:
            record = build_failure_record(spot, repr(error))
        write_json(os.path.join(out_dir, spot["id"] + ".json"), record)
        log(describe_record(record))
        records.append(record)
    leave_region(travel, boxes)
    return records


def capture_all(regions, spots, out_dir, options):
    ground_cm = build_ground_lookup(regions)
    boxes = pie_checks.load_realm_gate_boxes()
    os.makedirs(out_dir, exist_ok=True)
    records = []
    start_pie(options.warmup)
    try:
        set_exposure_offset(options.exposure_offset)
        for region_name, region in regions.items():
            group = [spot for spot in spots if spot["region"] == region_name]
            if not group:
                continue
            records += capture_region(region_name, region, group, ground_cm, boxes, out_dir, options)
    finally:
        reset_exposure_offset(options.exposure_offset)
        stop_pie()
    return records


def describe_record(record):
    if record.get("error"):
        return f"{record['id']}: FAIL {record['error']}"
    return f"{record['id']}: dark={record['dark_ratio']:.3f} center_lum={record['center_mean_luminance']:.3f} occluders={record['occluder_count']} buried={record['buried']}"


def load_run_records(run_dir):
    records = []
    for path in sorted(glob.glob(os.path.join(run_dir, "*.json"))):
        if os.path.basename(path) == "run.json":
            continue
        data = read_json(path)
        if "id" in data:
            records.append(data)
    return records


def order_records(records, spots):
    order = {spot["id"]: index for index, spot in enumerate(spots)}
    return sorted(records, key=lambda record: order.get(record["id"], len(order)))


def configure_output():
    reconfigure = getattr(sys.stdout, "reconfigure", None)
    if reconfigure is not None:
        reconfigure(errors="replace")


def resolve_run_dir(value):
    if os.path.isdir(value):
        return os.path.abspath(value)
    candidate = os.path.join(SHOT_ROOT, value)
    if os.path.isdir(candidate):
        return candidate
    raise SystemExit(f"run folder not found: {value}")


def table_cell(text):
    return str(text).replace("|", "/").replace("\n", " ")


def is_off_target(record):
    return record.get("offset_cm", 0.0) > OFF_TARGET_WARN_CM


def format_occluders(record):
    names = [f"{entry['mesh'] or entry['comp_class']}:{entry['actor']}#{entry['item']}@{entry['dist_from_camera_cm']:.0f}" for entry in record["occluders"][:MAIN_OCCLUDER_COUNT]]
    return table_cell(", ".join(names) if names else "-")


def render_off_target_lines(records):
    off_target = [record["id"] for record in records if not record.get("error") and is_off_target(record)]
    if not off_target:
        return []
    return [f"- 폰이 목표 좌표에서 {OFF_TARGET_WARN_CM:.0f}cm 넘게 벗어난 지점(낙하 복구·이동 거부 의심, 수치 신뢰 불가): {', '.join(off_target)}"]


def describe_exposure_offset(exposure_offset):
    return "엔진 기본" if exposure_offset is None else f"{exposure_offset:+g} EV"


def render_summary(run_name, meta, records):
    failed = [record for record in records if record.get("error")]
    lines = [
        f"# PIE 지점 촬영 {run_name}",
        "",
        f"- 실행: {meta.get('created', '-')}, 지점 {len(records)}개(성공 {len(records) - len(failed)}, 실패 {len(failed)}), 목록 `{meta.get('spots_file', '-')}`",
        f"- 노출 오프셋({EXPOSURE_VARIABLE}): {describe_exposure_offset(meta.get('exposure_offset'))}",
        f"- 어두운 화소 = 휘도 {DARK_LUMINANCE} 미만, 중앙 휘도 = 가운데 가로·세로 {int(CENTER_FRACTION * 100)}% 사각형 평균(둘 다 0~1)",
        "- 가림 수 = 카메라→폰 가슴(지면+120cm) 구 쓸기(반지름 30cm)에 걸린 지형 외 (액터, 구성 요소, 인스턴스) 수, 매몰 = 카메라가 메시 안이거나 지형 아래",
        *render_off_target_lines(records),
        "",
        "| 지점 | 지역 | 폰 좌표(cm) | 목표와 거리(cm) | 어두운 비율 | 중앙 휘도 | 가림 수 | 매몰 | 가까운 가림 3개(메시:액터#번호@카메라 거리cm) |",
        "|---|---|---|---|---|---|---|---|---|",
    ]
    for record in records:
        if record.get("error"):
            lines.append(f"| {record['id']} | {record['region']} | - | - | - | - | - | - | 실패: {table_cell(record['error'])} |")
            continue
        pawn = ", ".join(f"{value:.0f}" for value in record["pawn"])
        lines.append(f"| {record['id']} | {record['region']} | {pawn} | {record['offset_cm']:.0f} | {record['dark_ratio']:.3f} | {record['center_mean_luminance']:.3f} | {record['occluder_count']} | {'예' if record['buried'] else '아니오'} | {format_occluders(record)} |")
    return "\n".join(lines) + "\n"


def format_signed(delta, digits):
    return f"{delta:+.{digits}f}"


def describe_off_target(old, new):
    notes = []
    if is_off_target(old):
        notes.append("전 목표 이탈")
    if is_off_target(new):
        notes.append("후 목표 이탈")
    return ", ".join(notes) if notes else "-"


def render_compare(before_name, before_records, after_name, after_records):
    before = {record["id"]: record for record in before_records}
    after = {record["id"]: record for record in after_records}
    ids = [record["id"] for record in after_records]
    lines = [
        f"# PIE 전후 비교: {before_name} -> {after_name}",
        "",
        "| 지점 | 어두운 비율 전 | 후 | 변화 | 가림 수 전 | 후 | 변화 | 매몰 전 | 후 | 중앙 휘도 전 | 후 | 비고 |",
        "|---|---|---|---|---|---|---|---|---|---|---|---|",
    ]
    totals = {"dark_before": [], "dark_after": [], "occ_before": 0, "occ_after": 0, "buried_before": 0, "buried_after": 0}
    for record_id in ids:
        old, new = before.get(record_id), after.get(record_id)
        old_ok = old is not None and not old.get("error")
        new_ok = new is not None and not new.get("error")
        if not (old_ok and new_ok):
            note = "전 없음" if old is None else "후 없음" if new is None else "실패 포함"
            lines.append(f"| {record_id} | - | - | - | - | - | - | - | - | - | - | {note} |")
            continue
        totals["dark_before"].append(old["dark_ratio"])
        totals["dark_after"].append(new["dark_ratio"])
        totals["occ_before"] += old["occluder_count"]
        totals["occ_after"] += new["occluder_count"]
        totals["buried_before"] += int(old["buried"])
        totals["buried_after"] += int(new["buried"])
        lines.append(
            f"| {record_id} | {old['dark_ratio']:.3f} | {new['dark_ratio']:.3f} | {format_signed(new['dark_ratio'] - old['dark_ratio'], 3)} "
            f"| {old['occluder_count']} | {new['occluder_count']} | {format_signed(new['occluder_count'] - old['occluder_count'], 0)} "
            f"| {'예' if old['buried'] else '아니오'} | {'예' if new['buried'] else '아니오'} | {old['center_mean_luminance']:.3f} | {new['center_mean_luminance']:.3f} | {describe_off_target(old, new)} |"
        )
    compared = len(totals["dark_before"])
    if compared:
        lines += [
            "",
            f"- 비교 지점 {compared}개: 어두운 비율 평균 {sum(totals['dark_before']) / compared:.3f} -> {sum(totals['dark_after']) / compared:.3f}, "
            f"가림 수 합 {totals['occ_before']} -> {totals['occ_after']}, 매몰 지점 {totals['buried_before']} -> {totals['buried_after']}",
        ]
    return "\n".join(lines) + "\n"


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description="PIE 지점 촬영 + 카메라 가림·어두운 비율 측정 + 전후 비교")
    parser.add_argument("--spots", default=DEFAULT_SPOTS, help="지점 목록 JSON(기본 Tools/WorldGen/topdown_spots.json)")
    parser.add_argument("--run", default=None, help="결과 폴더 이름(기본 현재 시각), Saved/WorldGen/PieShots/<run>/")
    parser.add_argument("--ids", default="", help="쉼표로 구분한 지점 ID 또는 접두어(F01, F08_west_outer_forest 등), 비우면 전부")
    parser.add_argument("--regions", default="", help="쉼표로 구분한 지역(field, SunkenStarfield, PaleHeights), 비우면 전부")
    parser.add_argument("--compare", default=None, help="이전 run 폴더 경로 또는 PieShots 아래 이름")
    parser.add_argument("--offline", action="store_true", help="PIE 없이 --run 폴더의 기존 지점 JSON으로 요약만 다시 만든다")
    parser.add_argument("--overwrite", action="store_true", help="이미 있는 run 폴더에 덮어쓴다")
    parser.add_argument("--settle", type=float, default=6.0, help="이동 뒤 촬영 전 대기 초(카메라 지연 수렴)")
    parser.add_argument("--warmup", type=float, default=12.0, help="PIE 시작 뒤 대기 초")
    parser.add_argument("--refine", action="store_true", help="이동 뒤 지면을 다시 추적해 폰을 붙인다")
    parser.add_argument("--exposure-offset", type=float, default=None, help=f"PIE 시작 뒤 {EXPOSURE_VARIABLE}(EV)를 이 값으로 설정하고 종료 때 {EXPOSURE_RESET_VALUE:g}으로 되돌린다(기본 건드리지 않음)")
    return parser.parse_args(argv)


def main(argv=None):
    configure_output()
    args = parse_args(argv)
    spots_path = os.path.abspath(args.spots)
    regions, spots = load_spots(spots_path)
    selected = select_spots(spots, args.ids, args.regions)
    selected_ids = {spot["id"] for spot in selected}
    run_name = args.run or time.strftime("%Y%m%d_%H%M%S")
    out_dir = os.path.join(SHOT_ROOT, run_name)
    run_file = os.path.join(out_dir, "run.json")
    previous_dir = resolve_run_dir(args.compare) if args.compare else None
    meta = {"run": run_name, "created": time.strftime("%Y-%m-%d %H:%M:%S"), "spots_file": describe_project_path(spots_path), "spot_ids": [spot["id"] for spot in selected], "exposure_offset": args.exposure_offset}
    if args.offline:
        if not os.path.isfile(run_file):
            raise SystemExit(f"--offline needs an existing run folder with run.json: {out_dir}")
        meta = read_json(run_file)
        records = [record for record in order_records(load_run_records(out_dir), spots) if record["id"] in selected_ids]
    else:
        if os.path.exists(run_file) and not args.overwrite:
            raise SystemExit(f"run folder already exists (use another --run or --overwrite): {out_dir}")
        try:
            records = capture_all(regions, selected, out_dir, args)
        except PieStartError as error:
            log(str(error))
            return 2
        write_json(run_file, meta)
    write_text(os.path.join(out_dir, "summary.md"), render_summary(run_name, meta, records))
    log(f"summary: {os.path.join(out_dir, 'summary.md')}")
    if previous_dir:
        previous_records = order_records(load_run_records(previous_dir), spots)
        write_text(os.path.join(out_dir, "compare.md"), render_compare(os.path.basename(previous_dir), previous_records, run_name, records))
        log(f"compare: {os.path.join(out_dir, 'compare.md')}")
    return 1 if any(record.get("error") for record in records) else 0


if __name__ == "__main__":
    sys.exit(main())
