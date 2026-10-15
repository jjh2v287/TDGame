"""연속 세계(D-45) 결정론 검사기: 필드·두 이계 생성기 출력(height.r16·layer r8·layout.json)을 한 월드 격자로 이어붙여 K2 결정론·높이/ISM 변경 범위, K3 공유 경계, K4 연결로 경사(5·10 m 창 포함)·슬롯 바닥 폭, K5 MaintainYFOV 시야 누출(16:9·21:9)·카메라 매몰(닿는 모든 쿼드, 기준 출력과 같은 자리이면서 변경 합집합 밖인 칸만 예외), K6 거리 보장, K7 능선 차단(지형만), J 연결로 먼 쪽 벽 윗면 도달 불가(도달은 쿼드 삼각형 4개 최대 경사 44° 이하 엄격 모델), K8 강 끝, 라이너 부등식, 재질 지역 혼합(6.5, 이계 지형 MI 파라미터 이름을 재질 도구 두 개의 정의와 대조하고 값은 배치에서 계산), 필드·이웃 상자 안 이계 배치물, 옛 이음매 키·관문 스키마를 검사한다(P3-19 명세 3.5).
실행: python Tools/WorldGen/check_continuous_world.py [--field-dir Saved/WorldGen/AshenVale] [--realms-dir Saved/WorldGen/Realms] [--prev-field-dir Saved/WorldGen/AshenVale_prev_p3-19] [--prev-realms-dir Saved/WorldGen/Realms_prev_p3-19] [--no-rerun](결정론 재생성 생략) [--workers N] [--report <json>] (PowerShell 또는 Git Bash, numpy·scipy 필요, generate_ashen_vale.py 와 generate_realm.py --realm all 출력이 먼저 있어야 함)
출력: --report(기본 Saved/WorldGen/ContinuousCheck/report.json)에 검사별 값·한도·세부, 표준 출력에 검사 표. 실패가 하나라도 있으면 종료 코드 1, 입력 오류 2
상태: 현행(P3-19)
"""
import argparse
import ast
import hashlib
import json
import math
import os
import re
import subprocess
import sys
import tempfile
import time
from multiprocessing import Pool

import numpy as np
from scipy import ndimage

import generate_ashen_vale as gav
import generate_realm as gr
import realm_passages as rp
import topdown_materials as tdm
import topdown_rules as tdr

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(os.path.dirname(TOOLS_DIR))
WORLDGEN_DIR = os.path.join(PROJECT_DIR, "Saved", "WorldGen")
DEFAULT_REPORT = os.path.join(WORLDGEN_DIR, "ContinuousCheck", "report.json")
LANDSCAPE_MATERIAL_SCRIPT = os.path.join(TOOLS_DIR, "editor_make_landscape_material.py")
TOPDOWN_MATERIAL_SCRIPT = os.path.join(TOOLS_DIR, "editor_make_topdown_materials.py")
REGION_FUNCTION_BUILDER = "build_region_function"
LANDSCAPE_MASTER_CONSTANT = "MASTER_PATH"
ORIGIN_OFFSET_CONSTANT = "ORIGIN_OFFSET_PARAMETER"
PARAMETER_CLASS_KINDS = {"MaterialExpressionVectorParameter": "vectors", "MaterialExpressionScalarParameter": "scalars"}
REALM_REGION_BLEND_STRENGTH = 1.0
VECTOR_PARAMETER_LENGTH = 4
CONTENT_DIR = os.path.join(PROJECT_DIR, "Content")
LAYER_NAMES = ("Soil", "Moss", "Mud", "Road", "Rock", "Bedrock")
OUTPUT_FILES = ("height.r16",) + tuple(f"layer_{name}.r8" for name in LAYER_NAMES) + ("layout.json", "topdown_rules_report.json")

CONNECTOR_GRADE_LIMITS = {"SunkenStarfieldSouthwest": 0.20, "PaleHeightsNortheast": 0.08}
RAMP_GRADE_LIMIT = math.tan(math.radians(15.0))
GRADE_SAMPLE_HALF_M = 2.0
GRADE_TOLERANCE = 2.0 / 128.0 / (2.0 * GRADE_SAMPLE_HALF_M)
GRADE_SKIP_END_M = 4.0
CONNECTOR_FLOOR_MAX_SLOPE_DEG = 35.0
FLOOR_INSET_M = 1.0
SLOT_MIN_FLOOR_WIDTH_M = 12.0
FLAT_TOLERANCE_M = 1.0 / 128.0
SHARED_STEP_TOLERANCE_M = 0.25
SHARED_SLOPE_TOLERANCE_DEG = 3.0
SHARED_LAYER_TOLERANCE = 0.05
SHARED_BAND_FLAT_CELLS = 5
STAMP_INSET_M = 2.0
FIELD_VISION_BAND_M = 2.0 * rp.FIELD_RIM_BAND_M
REALM_VISION_BAND_M = 60.0
CREST_M = 5.0
LATTICE_STEP_M = 2
RAY_STEP_M = 1.0
RAY_MAX_HORIZONTAL_M = 450.0
RAY_START_T = 0.2
CELLS_PER_JOB = 300
CORRIDOR_MARGIN_M = 2.0
FLOOR_REACH_INSET_M = 0.5
DECK_HEIGHT_COUNT = 21
WP_CELL_M = 252.0
WP_LOADING_RANGE_M = 768.0
DISTANCE_LIMIT_M = WP_LOADING_RANGE_M - WP_CELL_M
WALK_FLOOD_MAX_SLOPE_DEG = rp.WALK_MAX_SLOPE_DEG
RIM_BAND_MIN_SLOPE_DEG = 46.0
LIFT_LINK_TOLERANCE_M = 1.0
CAMERA_REACH_CELLS = int(math.ceil(math.hypot(tdr.CAMERA_OFFSET_M[0], tdr.CAMERA_OFFSET_M[1]))) + 2
GRADE_WINDOWS_M = (5.0, 10.0)
GRADE_LANES_M = (-2.5, 0.0, 2.5)
GRADE_WINDOW_STEP_M = 0.25
GRADE_KIND_MARGIN_M = 1.0
UNITS_PER_METER_FIELD = 128.0
RIM_CORRIDOR_DILATION_CELLS = 2
SOUTH_EDGE_MIN_Z_M = 30.0
RIVER_WATER_MIN_Y_M = -439.0
RIVER_CARVE_REACH_M = 11.0
UNION_ITEM_MARGIN_CELLS = 3
SPUR_REACH_MARGIN_M = 1.0
COLOR_TOLERANCE = 1e-6
PLANE_MARGIN_M = RAY_MAX_HORIZONTAL_M + 50.0
COVER_PLANE_TOKENS = ("_OuterRing", "_Cloud", "_Water_")
TRANSLUCENT_MATERIAL_TOKENS = ("VioletFog",)
LUMINANCE = np.array([0.3, 0.59, 0.11])
CLUSTER_REPORT_LIMIT = 8
FORBIDDEN_KEYS = ("passages", "old_passage_proxy", "hold_margin_cm", "max_wait_seconds")
FORBIDDEN_KEY_PATTERN = re.compile(r"seam", re.IGNORECASE)
FORBIDDEN_LABEL_PATTERN = re.compile(r"Seam|TDRealm_Passage_")
GATE_CONTRACT_KEYS = ("gate_kind", "realm_id", "arrival", "field_return", "approach_length_cm", "departure_length_cm", "passage_half_width_cm", "preload_lead_cm", "lift")
MATERIAL_REFERENCE_PATTERN = re.compile(rb"/Game/[A-Za-z0-9_/]*Materials/[A-Za-z0-9_/]+")
RERUN_REALM_CODE = ("import sys; sys.path.insert(0, sys.argv[1]); import generate_realm as gr; gr.MAIN_DIR = sys.argv[2]; gr.REALMS_DIR = sys.argv[3]; "
                    "sys.argv = [sys.argv[0]] + sys.argv[4:]; sys.exit(gr.main())")

VISION = {}


class TDInputError(Exception):
    pass


def make_check(check_id, name, passed, value, limit, detail=None):
    return {"id": check_id, "name": name, "passed": passed, "value": value, "limit": limit, "detail": detail or {}}


def smoothstep(edge0, edge1, x):
    t = np.clip((np.asarray(x, dtype=np.float64) - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as fp:
        for chunk in iter(lambda: fp.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_json(path):
    with open(path, encoding="utf-8") as fp:
        return json.load(fp)


def sample_heights(height, x0, y0, x, y):
    ny, nx = height.shape
    fx = np.asarray(x, dtype=np.float64) - x0
    fy = np.asarray(y, dtype=np.float64) - y0
    out = np.full(fx.shape, np.nan)
    ok = (fx >= 0.0) & (fy >= 0.0) & (fx <= nx - 1.001) & (fy <= ny - 1.001)
    ix = np.floor(fx[ok]).astype(np.int64)
    iy = np.floor(fy[ok]).astype(np.int64)
    tx = fx[ok] - ix
    ty = fy[ok] - iy
    out[ok] = (height[iy, ix] * (1.0 - tx) + height[iy, ix + 1] * tx) * (1.0 - ty) + (height[iy + 1, ix] * (1.0 - tx) + height[iy + 1, ix + 1] * tx) * ty
    return out


def slope_deg(height):
    gy, gx = np.gradient(height, 1.0)
    return np.degrees(np.arctan(np.hypot(gx, gy)))


def segment_distance(px, py, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    length2 = dx * dx + dy * dy
    t = np.clip(((px - a[0]) * dx + (py - a[1]) * dy) / length2, 0.0, 1.0) if length2 > 0.0 else np.zeros(np.shape(px))
    return np.hypot(px - (a[0] + t * dx), py - (a[1] + t * dy))


def realm_ids():
    return tuple(rp.REALM_PLACEMENT)


def realm_box(land):
    cx, cy = land["center_m"]
    half = land["half"]
    return cx - half, cx + half, cy - half, cy + half


def edge_segment(edge):
    r0, r1 = edge["range"]
    if edge["axis"] == "y":
        return (r0, edge["value"]), (r1, edge["value"])
    return (edge["value"], r0), (edge["value"], r1)


def field_axes(field):
    axis = np.arange(field["size"], dtype=np.float64) - field["half"]
    return np.meshgrid(axis + field["center_m"][0], axis + field["center_m"][1])


def field_index(field, x, y):
    return int(round(y)) - (field["center_m"][1] - field["half"]), int(round(x)) - (field["center_m"][0] - field["half"])


def load_inputs(field_dir, realms_dir):
    for folder in [field_dir] + [os.path.join(realms_dir, rid) for rid in realm_ids()]:
        for name in OUTPUT_FILES[:-1]:
            if not os.path.isfile(os.path.join(folder, name)):
                raise TDInputError(f"missing generator output {os.path.join(folder, name)}")
    world = tdr.stitch_world(field_dir, realms_dir, realm_ids())
    world["X"], world["Y"] = tdr.world_axes(world)
    return world


def normalized_layout_digest(path, folder):
    prefix = os.path.normcase(os.path.abspath(folder))

    def normalize(value):
        if isinstance(value, dict):
            return {k: normalize(v) for k, v in value.items()}
        if isinstance(value, list):
            return [normalize(v) for v in value]
        if isinstance(value, str) and os.path.normcase(os.path.abspath(value)).startswith(prefix) and os.path.isabs(value):
            return os.path.relpath(os.path.abspath(value), os.path.abspath(folder)).replace("\\", "/")
        return value

    text = json.dumps(normalize(read_json(path)), sort_keys=True, ensure_ascii=False)
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def output_digests(folder):
    digests = {}
    for name in OUTPUT_FILES:
        path = os.path.join(folder, name)
        if not os.path.isfile(path):
            continue
        digests[name] = normalized_layout_digest(path, folder) if name == "layout.json" else sha256_of(path)
    return digests


def run_generator(command, log_path):
    with open(log_path, "w", encoding="utf-8") as log:
        result = subprocess.run(command, cwd=PROJECT_DIR, stdout=log, stderr=subprocess.STDOUT)
    return result.returncode


def check_determinism(world, field_dir, realms_dir, rerun):
    if not rerun:
        return make_check("K2.determinism", "rerun field and realms into a temp folder, outputs identical", None, "skipped", "identical")
    field_layout = world["landscapes"][tdr.FIELD_REGION_NAME]["layout"]
    topdown_off = not os.path.isfile(os.path.join(field_dir, "topdown_rules_report.json"))
    started = time.time()
    detail = {"mismatched": [], "compared": 0, "return_codes": {}}
    with tempfile.TemporaryDirectory(prefix="td_continuous_check_") as temp:
        field_out = os.path.join(temp, "AshenVale")
        realms_out = os.path.join(temp, "Realms")
        command = [sys.executable, os.path.join(TOOLS_DIR, "generate_ashen_vale.py"), "--seed", str(field_layout["seed"]), "--out", field_out]
        detail["return_codes"]["field"] = run_generator(command + (["--no-topdown"] if topdown_off else []), os.path.join(temp, "field.log"))
        pairs = [(field_dir, field_out)]
        for rid in realm_ids():
            seed = world["landscapes"][rid]["layout"]["seed"]
            command = [sys.executable, "-c", RERUN_REALM_CODE, TOOLS_DIR, field_out, realms_out, "--realm", rid, "--seed", str(seed)]
            detail["return_codes"][rid] = run_generator(command + (["--no-topdown"] if topdown_off else []), os.path.join(temp, f"{rid}.log"))
            pairs.append((os.path.join(realms_dir, rid), os.path.join(realms_out, rid)))
        for original, rerun_dir in pairs:
            first = output_digests(original)
            second = output_digests(rerun_dir) if os.path.isdir(rerun_dir) else {}
            for name, digest in first.items():
                detail["compared"] += 1
                if second.get(name) != digest:
                    detail["mismatched"].append(os.path.join(os.path.basename(original), name))
            detail.setdefault("sha256", {})[os.path.basename(original)] = first
    detail["seconds"] = round(time.time() - started, 1)
    failed_runs = {k: v for k, v in detail["return_codes"].items() if v != 0}
    passed = not detail["mismatched"] and not failed_runs and detail["compared"] > 0
    return make_check("K2.determinism", "rerun field and realms into a temp folder, outputs identical", passed, len(detail["mismatched"]) + len(failed_runs), 0, detail)


def field_change_union(field, terrain):
    X, Y = field_axes(field)
    regions = {"connector": rp.connector_footprint(X, Y), "rim_band": gav.rim_band_depth_m(X, Y) > 0.0,
               "river_refill": (Y + field["half"] < gav.RIVER_REFILL_END_M) & (terrain["river_dist"] < RIVER_CARVE_REACH_M)}
    spur = np.zeros(X.shape, dtype=bool)
    for gate_id, (start, width) in gav.SPUR_ROADS.items():
        mouth = rp.pieces_of_gate(gate_id)[0]["vertices"][0]
        spur |= segment_distance(X, Y, start, (float(mouth[0]), float(mouth[1]))) < 0.5 * width + rp.SPUR_BLEND_M + SPUR_REACH_MARGIN_M
    regions["spur_road"] = spur
    return regions


def check_field_heights(field, terrain, regions):
    upm = field["units_per_meter"]
    pre_units = np.round(32768.0 + terrain["pre_passage_h"] * upm).astype(np.int64)
    new_units = field["units"].astype(np.int64)
    union = np.logical_or.reduce(list(regions.values()))
    changed = new_units != pre_units
    detail = {"changed_cells": int(changed.sum()), "union_cells": int(union.sum()), "changed_by_region": {name: int((changed & mask).sum()) for name, mask in regions.items()}}
    outside = changed & ~union
    if outside.any():
        iy, ix = np.nonzero(outside)
        detail["outside_samples_m"] = [[int(ix[k] - field["half"]), int(iy[k] - field["half"])] for k in range(min(10, len(ix)))]
    return make_check("K2.field_height", "field height == pre_passage_h outside (connector | spur | rim band | river refill)", not outside.any(), int(outside.sum()), 0, detail)


def ism_items(layout):
    items = {}
    for entry in layout["ism"]:
        for transform in entry["transforms"]:
            items.setdefault((entry["key"], transform[0], transform[1]), []).append(tuple(transform))
    return items


def topdown_rule_reach_m():
    extent = max(max(abs(b[0]), abs(b[1]), abs(b[3]), abs(b[4])) for b in tdr.MESH_BOUNDS_CM.values())
    return (tdr.VISIBLE_BEHIND_CM + extent) / 100.0


def check_field_ism(field, terrain, regions, prev_dir):
    if not prev_dir or not os.path.isfile(os.path.join(prev_dir, "layout.json")):
        return make_check("K2.field_ism", "field ISM set == previous outside the change union", None, "skipped (no previous output)", 0)
    prev = tdr.read_landscape(prev_dir)
    old_passages = prev["units"].astype(np.int64) != np.round(32768.0 + terrain["pre_passage_h"] * field["units_per_meter"]).astype(np.int64)
    union = np.logical_or.reduce(list(regions.values()) + [old_passages])
    inside = ndimage.binary_dilation(union, iterations=UNION_ITEM_MARGIN_CELLS)
    reach = topdown_rule_reach_m()
    influence = ndimage.distance_transform_edt(~union) <= reach
    new_items = ism_items(field["layout"])
    prev_items = ism_items(prev["layout"])
    counts = {"inside_union": {}, "topdown_reach": {}, "elsewhere": {}}
    samples = []
    for key in set(new_items) | set(prev_items):
        a = sorted(prev_items.get(key, []))
        b = sorted(new_items.get(key, []))
        if a == b:
            continue
        kind = "modified" if a and b else ("added" if b else "removed")
        iy, ix = field_index(field, key[1] / 100.0, key[2] / 100.0)
        iy, ix = min(max(iy, 0), field["size"] - 1), min(max(ix, 0), field["size"] - 1)
        where = "inside_union" if inside[iy, ix] else ("topdown_reach" if influence[iy, ix] else "elsewhere")
        counts[where][kind] = counts[where].get(kind, 0) + max(len(a), len(b))
        if where == "elsewhere" and len(samples) < 10:
            samples.append({"key": key[0], "loc_cm": [key[1], key[2]], "kind": kind})
    elsewhere = sum(counts["elsewhere"].values())
    detail = {"counts": counts, "topdown_rule_reach_m": round(reach, 2), "old_passage_cells": int(old_passages.sum()), "elsewhere_samples": samples,
              "new_instances": sum(len(v) for v in new_items.values()), "prev_instances": sum(len(v) for v in prev_items.values())}
    return make_check("K2.field_ism", "field ISM set == previous outside (change union + old passages) and the topdown rule reach", elsewhere == 0, elsewhere, 0, detail)


def shared_line(world, rid):
    edge = rp.shared_edge(rid)
    land = world["landscapes"][rid]
    field = world["landscapes"][tdr.FIELD_REGION_NAME]
    along = np.arange(int(round(edge["range"][0])), int(round(edge["range"][1])) + 1)
    value = int(round(edge["value"]))
    centre = land["center_m"][1] if edge["axis"] == "y" else land["center_m"][0]
    inward = 1 if centre > value else -1

    def land_index(target, depth):
        fixed = value + inward * depth - ((target["center_m"][1] if edge["axis"] == "y" else target["center_m"][0]) - target["half"])
        moving = along - ((target["center_m"][0] if edge["axis"] == "y" else target["center_m"][1]) - target["half"])
        return (np.full(along.shape, fixed), moving) if edge["axis"] == "y" else (moving, np.full(along.shape, fixed))

    def world_points(depth):
        fixed = np.full(along.shape, float(value + inward * depth))
        return (along.astype(np.float64), fixed) if edge["axis"] == "y" else (fixed, along.astype(np.float64))

    return {"edge": edge, "along": along, "inward": inward, "land": land, "field": field, "land_index": land_index, "world_points": world_points}


def check_shared_edges(world):
    checks = []
    for rid in realm_ids():
        line = shared_line(world, rid)
        land, field = line["land"], line["field"]
        realm_rows = line["land_index"](land, 0)
        field_rows = line["land_index"](field, 0)
        offset_units = int(round((field["z_offset_m"] - land["z_offset_m"]) * field["units_per_meter"]))
        u16_diff = np.abs(land["units"][realm_rows].astype(np.int64) - field["units"][field_rows].astype(np.int64) - offset_units)
        edge_layer_diff = max(int(np.abs(land["layers"][n][realm_rows].astype(int) - field["layers"][n][field_rows].astype(int)).max()) for n in LAYER_NAMES)
        band_diff = 0.0
        for depth in range(1, SHARED_BAND_FLAT_CELLS + 1):
            rows = line["land_index"](land, depth)
            band_diff = max(band_diff, max(float(np.abs(land["layers"][n][rows].astype(float) - field["layers"][n][field_rows].astype(float)).max()) / 255.0 for n in LAYER_NAMES))
        detail = {"shared_vertices": int(len(line["along"])), "u16_offset": offset_units, "u16_max_diff": int(u16_diff.max()), "edge_layer_r8_max_diff": edge_layer_diff,
                  "band_d1_5_layer_max_diff": round(band_diff, 4)}
        passed = int(u16_diff.max()) == 0 and edge_layer_diff == 0 and band_diff <= SHARED_LAYER_TOLERANCE
        checks.append(make_check(f"K3.shared_{rid}", f"{rid} shared edge u16 diff 0, edge layers equal, band layers <= {SHARED_LAYER_TOLERANCE}", passed, int(u16_diff.max()), 0, detail))
        checks.append(check_connector_seam_continuity(world, rid, line))
    return checks


def stencil_grid(line):
    edge = line["edge"]
    along = line["along"].astype(np.float64)
    offsets = np.arange(-1.0, 2.0)
    if edge["axis"] == "y":
        X, Y = np.meshgrid(along, edge["value"] + offsets)
        return X, Y, lambda array, depth: array[line["inward"] * depth + 1, :]
    X, Y = np.meshgrid(edge["value"] + offsets, along)
    return X, Y, lambda array, depth: array[:, line["inward"] * depth + 1]


def stencil_profile(field_side, edge, realm_side):
    step = (realm_side - edge) - (edge - field_side)
    kink = np.degrees(np.arctan(realm_side - edge)) - np.degrees(np.arctan(edge - field_side))
    return step, kink


def check_connector_seam_continuity(world, rid, line):
    edge = line["edge"]
    X, Y, pick = stencil_grid(line)
    measured = sample_heights(world["h"], world["x0"], world["y0"], X, Y)
    detail = {}
    worst_step, worst_slope = 0.0, 0.0
    for gate_id in rp.GATE_IDS:
        for piece in rp.pieces_of_gate(gate_id):
            crossing = piece["boundary_crossing"]
            if piece["realm_id"] != rid or crossing is None or crossing.get("edge") != edge["name"]:
                continue
            analytic, floor_weight, wall_weight = rp.stamp_passage(np.zeros(X.shape), X, Y, piece)
            s, l = rp.passage_coords(piece, X, Y)
            reach = rp.section_reach(piece, l)
            exact = ((floor_weight + wall_weight) >= 1.0 - 1e-9) & (s >= piece["stamp_s_m"][0] + STAMP_INSET_M) & (s <= piece["stamp_s_m"][1] - STAMP_INSET_M)
            floor = np.ones(len(line["along"]), dtype=bool)
            walls = np.ones(len(line["along"]), dtype=bool)
            for depth in (-1, 0, 1):
                floor &= pick(exact & (reach <= rp.FLOOR_HALF_WIDTH_M), depth)
                walls &= pick(exact & (reach <= rp.WALL_TOP_OUTER_M), depth)
            measured_step, measured_kink = stencil_profile(*(pick(measured, d) for d in (-1, 0, 1)))
            analytic_step, analytic_kink = stencil_profile(*(pick(analytic, d) for d in (-1, 0, 1)))
            residual = np.max(np.abs(np.stack([pick(measured, d) - pick(analytic, d) for d in (-1, 0, 1)])), axis=0)
            step = np.abs(measured_step - analytic_step)
            slope = np.abs(measured_kink - analytic_kink)
            piece_step = float(step[floor].max()) if floor.any() else float("nan")
            piece_slope = float(slope[walls].max()) if walls.any() else float("nan")
            detail[f"{gate_id}/{piece['name']}"] = {"floor_stencils": int(floor.sum()), "floor_wall_stencils": int(walls.sum()),
                                                    "floor_step_seam_diff_m": round(piece_step, 5), "perpendicular_slope_seam_diff_deg": round(piece_slope, 4),
                                                    "raw_floor_step_max_m": round(float(np.abs(measured_step)[floor].max()), 5) if floor.any() else None,
                                                    "raw_slope_kink_max_deg": round(float(np.abs(measured_kink)[walls].max()), 4) if walls.any() else None,
                                                    "max_residual_to_analytic_m": round(float(residual[walls].max()), 5) if walls.any() else None}
            if not floor.any() or not walls.any():
                worst_step = float("inf")
                continue
            worst_step = max(worst_step, piece_step)
            worst_slope = max(worst_slope, piece_slope)
    passed = bool(detail) and worst_step <= SHARED_STEP_TOLERANCE_M and worst_slope <= SHARED_SLOPE_TOLERANCE_DEG
    return make_check(f"K3.connector_{rid}", f"{rid} connector floor step <= {SHARED_STEP_TOLERANCE_M} m and perpendicular slope diff <= {SHARED_SLOPE_TOLERANCE_DEG} deg across the shared edge beyond the designed section (3-vertex stencils inside the exact stamp)",
                      passed, {"step_m": round(worst_step, 5), "slope_deg": round(worst_slope, 4)}, {"step_m": SHARED_STEP_TOLERANCE_M, "slope_deg": SHARED_SLOPE_TOLERANCE_DEG}, detail)


def segment_limit(gate_id, piece, kind):
    if kind == "ramp":
        return RAMP_GRADE_LIMIT
    if kind == "terrace":
        return 0.0
    return CONNECTOR_GRADE_LIMITS.get(gate_id, piece["max_grade"])


def check_grades(world):
    detail = {}
    passed = True
    for gate_id in rp.GATE_IDS:
        for piece in rp.pieces_of_gate(gate_id):
            analytic = []
            for kind, grade in zip(piece["segment_kinds"], piece["grades"]):
                limit = segment_limit(gate_id, piece, kind)
                analytic.append({"kind": kind, "grade": round(abs(grade), 5), "limit": round(limit, 5)})
                passed = passed and abs(grade) <= limit + 1e-9
            s = np.arange(GRADE_SKIP_END_M, piece["length"] - GRADE_SKIP_END_M + 1e-9, 1.0)
            low_x, low_y = rp.to_grid_m(piece, s - GRADE_SAMPLE_HALF_M, np.zeros_like(s))
            high_x, high_y = rp.to_grid_m(piece, s + GRADE_SAMPLE_HALF_M, np.zeros_like(s))
            grade = np.abs(sample_heights(world["h"], world["x0"], world["y0"], high_x, high_y) - sample_heights(world["h"], world["x0"], world["y0"], low_x, low_y)) / (2.0 * GRADE_SAMPLE_HALF_M)
            kinds = np.array(piece["segment_kinds"])
            limit = np.maximum([segment_limit(gate_id, piece, k) for k in kinds[rp.segment_index(piece, s - GRADE_SAMPLE_HALF_M)]],
                               [segment_limit(gate_id, piece, k) for k in kinds[rp.segment_index(piece, s + GRADE_SAMPLE_HALF_M)]])
            excess = grade - (limit + GRADE_TOLERANCE)
            measured = {kind: round(float(grade[kinds[rp.segment_index(piece, s)] == kind].max()), 5) for kind in set(piece["segment_kinds"]) if (kinds[rp.segment_index(piece, s)] == kind).any()}
            ok = bool(np.all(np.isfinite(grade))) and float(excess.max()) <= 0.0
            passed = passed and ok
            detail[f"{gate_id}/{piece['name']}"] = {"analytic": analytic, "measured_max_grade_by_kind": measured, "worst_excess": round(float(np.nanmax(excess)), 5), "pass": ok}
    return make_check("K4.grades", "connector grades: SW canyon <= 20%, NE <= 8%, ramp <= 15 deg, terrace flat (analytic and sampled centerline)", passed, "see detail", dict(CONNECTOR_GRADE_LIMITS, ramp=round(RAMP_GRADE_LIMIT, 5)), detail)


def window_limits(gate_id, piece, s):
    kinds = np.array(piece["segment_kinds"])[rp.segment_index(piece, s)]
    return np.array([segment_limit(gate_id, piece, kind) for kind in kinds])


def lift_window_points(x, y):
    hit = np.zeros(np.shape(x), dtype=bool)
    for shaft in rp.SHAFTS:
        hit |= rp.lift_window_mask(shaft, x, y, STAMP_INSET_M)
    return hit


def check_grade_windows(world):
    detail = {}
    passed = True
    for gate_id in rp.GATE_IDS:
        for piece in rp.pieces_of_gate(gate_id):
            s = np.arange(0.0, piece["length"] + 1e-9, GRADE_WINDOW_STEP_M)
            entry = {}
            for lane in GRADE_LANES_M:
                x, y = rp.to_grid_m(piece, s, np.full(s.shape, lane))
                z = sample_heights(world["h"], world["x0"], world["y0"], x, y)
                usable = np.isfinite(z) & ~lift_window_points(x, y)
                for window_m in GRADE_WINDOWS_M:
                    n = int(round(window_m / GRADE_WINDOW_STEP_M))
                    if n >= len(s):
                        continue
                    low, high = s[:-n], s[n:]
                    span = np.ones(len(low), dtype=bool)
                    for k in range(n + 1):
                        span &= usable[k:len(s) - n + k]
                    grade = np.abs(z[n:] - z[:-n]) / (high - low)
                    limit = np.maximum(window_limits(gate_id, piece, low - GRADE_KIND_MARGIN_M), window_limits(gate_id, piece, high + GRADE_KIND_MARGIN_M))
                    excess = np.where(span, grade - limit - 2.0 / UNITS_PER_METER_FIELD / window_m, -np.inf)
                    k = int(np.argmax(excess))
                    worst = float(excess[k])
                    entry[f"lane{lane:+.1f}_w{window_m:g}"] = {"max_grade": round(float(np.max(np.where(span, grade, 0.0))), 5), "worst_excess": round(worst, 5),
                                                               "at_s_m": round(float(low[k]), 2), "windows": int(span.sum())}
                    passed = passed and worst <= 0.0
            detail[f"{gate_id}/{piece['name']}"] = entry
    return make_check("K4.grade_windows", f"floor grade over {'/'.join(f'{w:g}' for w in GRADE_WINDOWS_M)} m windows from s = 0 (lanes {GRADE_LANES_M} m) within the segment limit (SW 20%, NE 8%, ramp 15 deg, terrace flat)",
                      passed, "see detail", dict(CONNECTOR_GRADE_LIMITS, ramp=round(RAMP_GRADE_LIMIT, 5)), detail)


def full_floor_cells(piece, X, Y, inset):
    s, l = rp.passage_coords(piece, X, Y)
    return (s >= inset) & (s <= piece["length"] - inset) & (rp.section_reach(piece, l) <= rp.FLOOR_HALF_WIDTH_M - FLOOR_INSET_M)


def check_floor_slopes(world, slope):
    X, Y = world["X"], world["Y"]
    window = np.zeros(X.shape, dtype=bool)
    for shaft in rp.SHAFTS:
        window |= rp.lift_window_mask(shaft, X, Y, STAMP_INSET_M)
    detail = {}
    worst = 0.0
    for gate_id in rp.GATE_IDS:
        for piece in rp.pieces_of_gate(gate_id):
            cells = full_floor_cells(piece, X, Y, STAMP_INSET_M) & ~window
            value = float(np.nanmax(slope[cells])) if cells.any() else float("nan")
            detail[f"{gate_id}/{piece['name']}"] = {"cells": int(cells.sum()), "max_slope_deg": round(value, 3)}
            worst = max(worst, value) if np.isfinite(value) else float("inf")
    return make_check("K4.floor_slope", f"connector inner floor slope <= {CONNECTOR_FLOOR_MAX_SLOPE_DEG} deg", worst <= CONNECTOR_FLOOR_MAX_SLOPE_DEG, round(worst, 3), CONNECTOR_FLOOR_MAX_SLOPE_DEG, detail)


def check_slot_width(world):
    X, Y = world["X"], world["Y"]
    detail = {}
    passed = True
    for shaft in rp.SHAFTS:
        slot = shaft["slot"]
        s, l = rp.passage_coords(slot, X, Y)
        window = rp.lift_window_mask(shaft, X, Y, STAMP_INSET_M)
        cells = (s >= slot["stamp_s_m"][0] + STAMP_INSET_M) & (s <= slot["stamp_s_m"][1] - STAMP_INSET_M) & (np.abs(l) <= 0.5 * SLOT_MIN_FLOOR_WIDTH_M) & ~window
        flat = np.abs(world["h"][cells] - shaft["bottom_z_m"]) <= FLAT_TOLERANCE_M
        declared = 2.0 * (rp.FLOOR_HALF_WIDTH_M + slot["widen_m"])
        ok = bool(cells.any()) and bool(flat.all()) and declared >= SLOT_MIN_FLOOR_WIDTH_M
        passed = passed and ok
        detail[shaft["gate_id"]] = {"declared_floor_width_m": declared, "vertices_within_half_width": int(cells.sum()), "flat_vertices": int(flat.sum())}
    return make_check("K4.slot_width", f"slot floor flat across >= {SLOT_MIN_FLOOR_WIDTH_M} m (every vertex with |l| <= 6 m at the bottom landing z)", passed, detail, SLOT_MIN_FLOOR_WIDTH_M)


def quad_axes(world):
    return world["X"][:-1, :-1] + 0.5, world["Y"][:-1, :-1] + 0.5


def quad_corners_all(mask):
    return mask[:-1, :-1] & mask[:-1, 1:] & mask[1:, :-1] & mask[1:, 1:]


def quad_corners_any(mask):
    return mask[:-1, :-1] | mask[:-1, 1:] | mask[1:, :-1] | mask[1:, 1:]


def quad_regions(region):
    return np.maximum.reduce([region[:-1, :-1], region[:-1, 1:], region[1:, :-1], region[1:, 1:]])


def quad_vertices(quads):
    vertices = np.zeros((quads.shape[0] + 1, quads.shape[1] + 1), dtype=bool)
    vertices[:-1, :-1] |= quads
    vertices[:-1, 1:] |= quads
    vertices[1:, :-1] |= quads
    vertices[1:, 1:] |= quads
    return vertices


def walkable_quads(height, regions, recovery_by_region):
    walk = rp.walkable_quads(height, WALK_FLOOD_MAX_SLOPE_DEG)
    centers = rp.quad_center_heights(height)
    for index, recovery_m in recovery_by_region.items():
        walk &= ~((regions == index) & (centers < recovery_m))
    return walk


def realm_recovery_m(layout):
    return layout["realm"]["fall_recovery_height_cm"] / 100.0


def world_walkable_quads(world):
    recovery = {index: realm_recovery_m(world["landscapes"][name]["layout"]) for index, name in enumerate(world["names"][1:], start=2)}
    return walkable_quads(world["h"], quad_regions(world["region"]), recovery)


def player_start_m(world):
    markers = world["landscapes"][tdr.FIELD_REGION_NAME]["layout"]["markers"]
    start = next((m for m in markers if m.get("kind") == "PlayerStart"), None)
    if start is None:
        raise TDInputError("field layout has no PlayerStart marker")
    return start["loc"][0] / 100.0, start["loc"][1] / 100.0


def quad_label(labels, x0, y0, x, y):
    iy, ix = int(math.floor(y - y0)), int(math.floor(x - x0))
    if iy < 0 or ix < 0 or iy >= labels.shape[0] or ix >= labels.shape[1]:
        return 0
    return int(labels[iy, ix])


def lift_link_labels(labels, walk, QX, QY, centers):
    links = []
    half = rp.LIFT_DECK_HALF_EXTENT_CM[0] / 100.0
    for shaft in rp.SHAFTS:
        v, w = rp.shaft_coords(shaft, QX, QY)
        top = walk & rp.landing_band_mask(shaft, QX, QY) & (np.abs(centers - shaft["top_z_m"]) < LIFT_LINK_TOLERANCE_M)
        deck = walk & (np.abs(v) <= half) & (np.abs(w) <= half) & (np.abs(centers - shaft["bottom_z_m"]) < LIFT_LINK_TOLERANCE_M)
        links.append((set(np.unique(labels[top]).tolist()) - {0}, set(np.unique(labels[deck]).tolist()) - {0}))
    return links


def reachable_quads(world, walk):
    labels = rp.quad_components(walk)
    start = quad_label(labels, world["x0"], world["y0"], *player_start_m(world))
    if start == 0:
        raise TDInputError("PlayerStart quad is not walkable in the stitched world")
    QX, QY = quad_axes(world)
    links = lift_link_labels(labels, walk, QX, QY, rp.quad_center_heights(world["h"]))
    keep = {start}
    for _ in range(len(links) + 1):
        for top, deck in links:
            if keep & (top | deck):
                keep |= top | deck
    return np.isin(labels, sorted(keep)), {"components_kept": len(keep), "lift_links": [[sorted(t), sorted(d)] for t, d in links]}


def rim_block_mask(field):
    X, Y = field_axes(field)
    band = gav.rim_band_depth_m(X, Y) > 0.0
    corridor = ndimage.binary_dilation(rp.connector_footprint(X, Y), iterations=RIM_CORRIDOR_DILATION_CELLS)
    return band & ~corridor


def landscape_window(world, land, extra=0):
    iy0 = land["center_m"][1] - land["half"] - world["y0"]
    ix0 = land["center_m"][0] - land["half"] - world["x0"]
    return slice(iy0, iy0 + land["size"] + extra), slice(ix0, ix0 + land["size"] + extra)


def check_ridge_block(world, quad_reach):
    field = world["landscapes"][tdr.FIELD_REGION_NAME]
    block = rim_block_mask(field)
    field_slope = slope_deg(field["height_m"])
    block_quads = quad_corners_all(block)
    hit = quad_reach[landscape_window(world, field, -1)] & block_quads
    detail = {"block_cells": int(block.sum()), "block_quads": int(block_quads.sum()), "block_min_slope_deg_field_only": round(float(field_slope[block].min()), 3),
              "reached_quads_world": int(quad_reach.sum()), "reached_in_block": int(hit.sum())}
    if hit.any():
        iy, ix = np.nonzero(hit)
        detail["reached_samples_m"] = [[int(ix[k] - field["half"]) + 0.5, int(iy[k] - field["half"]) + 0.5] for k in range(min(10, len(ix)))]
    passed = not hit.any() and float(field_slope[block].min()) >= RIM_BAND_MIN_SLOPE_DEG
    return make_check("K7.ridge_block", f"strict quad flood (every triangle <= {WALK_FLOOD_MAX_SLOPE_DEG} deg) from PlayerStart over the stitched terrain never enters a B_mask quad; B_mask slope >= {RIM_BAND_MIN_SLOPE_DEG} deg",
                      passed, int(hit.sum()), 0, detail)


def check_wall_tops(world, quad_reach):
    QX, QY = quad_axes(world)
    centers = rp.quad_center_heights(world["h"])
    detail = {}
    total = 0
    for gate_id in rp.GATE_IDS:
        for piece in rp.pieces_of_gate(gate_id):
            cells = rp.wall_top_cells(piece, QX, QY, centers)
            hit = cells & quad_reach
            entry = {"wall_top_quads": int(cells.sum()), "reached": int(hit.sum())}
            if hit.any():
                points = np.stack([QX[hit], QY[hit], centers[hit]], axis=1)
                entry["clusters"] = leak_clusters(points)
            detail[f"{gate_id}/{piece['name']}"] = entry
            total += int(hit.sum())
    return make_check("J.wall_top", f"no far wall-top quad of any connector piece (whole length, field and realm) is reachable by the strict quad flood ({WALK_FLOOD_MAX_SLOPE_DEG} deg)", total == 0, total, 0, detail)


def plane_rect_extent_m(plane):
    half_x = plane["scale"][0] * 0.5
    half_y = plane["scale"][1] * 0.5
    yaw = math.radians(plane["rot"][1])
    ex = abs(half_x * math.cos(yaw)) + abs(half_y * math.sin(yaw))
    ey = abs(half_x * math.sin(yaw)) + abs(half_y * math.cos(yaw))
    x, y = plane["loc"][0] / 100.0, plane["loc"][1] / 100.0
    return x - ex, x + ex, y - ey, y + ey


def check_river(world):
    field = world["landscapes"][tdr.FIELD_REGION_NAME]
    X, Y = field_axes(field)
    row = field["height_m"][0, :]
    cut = rp.connector_footprint(X[0:1, :], Y[0:1, :])[0]
    half = float(field["half"])
    planes = [p for p in field["layout"]["planes"] if p["label"].startswith("TDGen_River_")]
    outside = []
    for plane in planes:
        x0, x1, y0, y1 = plane_rect_extent_m(plane)
        if x0 < -half - 1e-6 or x1 > half + 1e-6 or y0 < RIVER_WATER_MIN_Y_M - 1e-6 or y1 > half + 1e-6:
            outside.append(plane["label"])
    low = float(row[~cut].min())
    detail = {"south_edge_min_z_outside_cuts_m": round(low, 3), "river_planes": len(planes), "river_planes_outside_region": outside}
    passed = low >= SOUTH_EDGE_MIN_Z_M and not outside and len(planes) > 0
    return make_check("K8.river", f"south shared edge min z outside connector cuts >= {SOUTH_EDGE_MIN_Z_M} m and river planes inside the field box with Y >= {RIVER_WATER_MIN_Y_M}", passed, round(low, 3), SOUTH_EDGE_MIN_Z_M, detail)


def realm_gates(world):
    gates = {}
    for rid in realm_ids():
        for gate in world["landscapes"][rid]["layout"]["realm"]["gates"]:
            gates[gate["gate_id"]] = gate
    return gates


def check_liner(world):
    detail = {}
    passed = True
    lift_gates = [g for g in realm_gates(world).values() if g["gate_kind"] == "Lift"]
    for gate in lift_gates:
        lift = gate["lift"]
        drop = lift["field_landing_transform"]["loc_cm"][2] - lift["realm_landing_transform"]["loc_cm"][2]
        ring_top = lift["ring_top_depth_cm"]
        ring_bottom = ring_top + lift["ring_module_count"] * lift["ring_module_height_cm"]
        cruise = lift["cruise_speed_cm_per_second"]
        brake = cruise * cruise / (2.0 * lift["brake_deceleration_cm_per_second_squared"])
        resume = cruise * cruise / (2.0 * lift["acceleration_cm_per_second_squared"])
        above = rp.LIFT_VISIBLE_ABOVE_DECK_CM
        below = lift["abyss_screen_depth_cm"]
        hold = lift["hold_depth_cm"]
        ranges = {"descend_brake_start": (hold - brake - above, hold - brake + below), "descend_resume_end": (hold + resume - above, hold + resume + below),
                  "ascend_brake_start": (drop - (hold - brake) - above, drop - (hold - brake) + below), "ascend_resume_end": (drop - (hold + resume) - above, drop - (hold + resume) + below)}
        inside = {name: low >= ring_top - 1e-6 and high <= ring_bottom + 1e-6 for name, (low, high) in ranges.items()}
        ok = 0.0 <= ring_top and ring_bottom <= drop and all(inside.values()) and hold - brake > 0.0
        passed = passed and ok
        detail[gate["gate_id"]] = {"drop_cm": drop, "rings_cm": [ring_top, ring_bottom], "visible_ranges_cm": {k: [round(a, 1), round(b, 1)] for k, (a, b) in ranges.items()}, "inside": inside}
    return make_check("L.liner", "liner rings inside the drop and every treadmill visible wall range inside the rings", passed and bool(lift_gates), len(lift_gates), ">= 1 lift gate", detail)


def region_weight(x_cm, y_cm, vectors, scalars):
    center = vectors.get(tdm.REGION_CENTER_PARAMETER, list(tdm.REGION_DEFAULT_CENTER_CM))
    half = vectors.get(tdm.REGION_HALF_EXTENT_PARAMETER, list(tdm.REGION_DEFAULT_HALF_EXTENT_CM))
    fade = scalars.get(tdm.REGION_FADE_WIDTH_PARAMETER, tdm.REGION_DEFAULT_FADE_WIDTH_CM)
    strength = scalars.get(tdm.REGION_STRENGTH_PARAMETER, 1.0)
    dx = np.maximum(np.abs(np.asarray(x_cm, dtype=np.float64) - center[0]) - half[0], 0.0)
    dy = np.maximum(np.abs(np.asarray(y_cm, dtype=np.float64) - center[1]) - half[1], 0.0)
    distance = np.hypot(dx, dy)
    return np.clip(smoothstep(0.0, max(fade, 1.0), distance) * strength, 0.0, 1.0), distance


def landscape_layer_tints():
    tree = ast.parse(open(LANDSCAPE_MATERIAL_SCRIPT, encoding="utf-8").read())
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == "LAYERS" for t in node.targets):
            return {row[0]: np.array(row[4], dtype=np.float64) for row in ast.literal_eval(node.value)}
    raise TDInputError(f"{LANDSCAPE_MATERIAL_SCRIPT}: LAYERS table not found")


def script_string_constants(tree):
    constants = {}
    for node in tree.body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
            values = resolve_strings(node.value, constants, ())
            if len(values) == 1:
                constants[node.targets[0].id] = values[0]
    return constants


def resolve_strings(node, constants, loop_values):
    if isinstance(node, ast.Constant) and isinstance(node.value, str):
        return [node.value]
    if isinstance(node, ast.Name):
        return [constants[node.id]] if node.id in constants else list(loop_values)
    if isinstance(node, ast.Attribute) and isinstance(node.value, ast.Name) and node.value.id == "td":
        value = getattr(tdm, node.attr, None)
        return [value] if isinstance(value, str) else []
    if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
        return [a + b for a in resolve_strings(node.left, constants, loop_values) for b in resolve_strings(node.right, constants, loop_values)]
    if isinstance(node, ast.JoinedStr):
        parts = [resolve_strings(v.value if isinstance(v, ast.FormattedValue) else v, constants, loop_values) for v in node.values]
        if all(len(part) == 1 for part in parts):
            return ["".join(part[0] for part in parts)]
    return []


def parameter_kind(call):
    for arg in call.args:
        if isinstance(arg, ast.Attribute) and arg.attr in PARAMETER_CLASS_KINDS:
            return PARAMETER_CLASS_KINDS[arg.attr]
    return None


def is_parameter_name_call(node, kinds):
    return (isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and node.func.attr == "set_editor_property" and isinstance(node.func.value, ast.Name)
            and node.func.value.id in kinds and len(node.args) == 2 and isinstance(node.args[0], ast.Constant) and node.args[0].value == "parameter_name")


def material_script_parameters(path, loop_values, function_name=None):
    tree = ast.parse(open(path, encoding="utf-8").read())
    constants = script_string_constants(tree)
    scope = tree if function_name is None else next((n for n in ast.walk(tree) if isinstance(n, ast.FunctionDef) and n.name == function_name), None)
    if scope is None:
        raise TDInputError(f"{path}: function {function_name} not found")
    kinds = {}
    parameters = {"vectors": set(), "scalars": set()}
    for node in ast.walk(scope):
        if not (isinstance(node, ast.Assign) and isinstance(node.value, ast.Call)):
            continue
        kind = parameter_kind(node.value)
        if kind is None:
            continue
        kinds.update({target.id: kind for target in node.targets if isinstance(target, ast.Name)})
        for keyword in node.value.keywords:
            if keyword.arg == "parameter_name":
                parameters[kind].update(resolve_strings(keyword.value, constants, loop_values))
    for node in ast.walk(scope):
        if is_parameter_name_call(node, kinds):
            parameters[kinds[node.func.value.id]].update(resolve_strings(node.args[1], constants, loop_values))
    return parameters, constants


def landscape_material_contract(layer_names):
    landscape, constants = material_script_parameters(LANDSCAPE_MATERIAL_SCRIPT, layer_names)
    region, _ = material_script_parameters(TOPDOWN_MATERIAL_SCRIPT, (), REGION_FUNCTION_BUILDER)
    for name in (LANDSCAPE_MASTER_CONSTANT, ORIGIN_OFFSET_CONSTANT):
        if name not in constants:
            raise TDInputError(f"{LANDSCAPE_MATERIAL_SCRIPT}: constant {name} not found")
    origin_offset = constants[ORIGIN_OFFSET_CONSTANT]
    required = {"vectors": region["vectors"] | {origin_offset}, "scalars": region["scalars"] | {tdm.REGION_STRENGTH_PARAMETER}}
    known = {kind: landscape[kind] | region[kind] for kind in landscape}
    missing = {kind: sorted(required[kind] - known[kind]) for kind in required if required[kind] - known[kind]}
    if missing or not region["vectors"] or not region["scalars"]:
        raise TDInputError(f"material tools do not expose the region blend parameters: missing {missing}, region function {region}")
    return {"known": known, "required": required, "origin_offset": origin_offset, "parent": constants[LANDSCAPE_MASTER_CONSTANT].split(".")[0]}


def expected_region_values(field, land, contract):
    field_origin = (field["center_m"][0] - field["half"], field["center_m"][1] - field["half"])
    origin = (land["center_m"][0] - land["half"], land["center_m"][1] - land["half"])
    half_cm = field["half"] * 100.0
    return {"vectors": {tdm.REGION_CENTER_PARAMETER: [field["center_m"][0] * 100.0, field["center_m"][1] * 100.0, 0.0, 0.0],
                        tdm.REGION_HALF_EXTENT_PARAMETER: [half_cm, half_cm, 0.0, 0.0],
                        contract["origin_offset"]: [float(origin[0] - field_origin[0]), float(origin[1] - field_origin[1]), 0.0, 0.0]},
            "scalars": {tdm.REGION_FADE_WIDTH_PARAMETER: tdm.REGION_DEFAULT_FADE_WIDTH_CM, tdm.REGION_STRENGTH_PARAMETER: REALM_REGION_BLEND_STRENGTH}}


def check_landscape_material_parameters(material, expected, contract):
    problems = []
    if material.get("parent") != contract["parent"]:
        problems.append({"parent": material.get("parent"), "expected": contract["parent"]})
    for kind in ("vectors", "scalars"):
        values = material.get(kind, {})
        unknown = sorted(set(values) - contract["known"][kind])
        missing = sorted(contract["required"][kind] - set(values))
        if unknown:
            problems.append({kind: "unknown", "names": unknown})
        if missing:
            problems.append({kind: "missing", "names": missing})
        for name, value in values.items():
            if kind == "vectors" and len(value) != VECTOR_PARAMETER_LENGTH:
                problems.append({kind: "length", "name": name, "value": value})
        for name, value in expected[kind].items():
            actual = values.get(name)
            if actual is None:
                continue
            if not np.allclose(np.asarray(actual, dtype=np.float64), np.asarray(value, dtype=np.float64), rtol=0.0, atol=1e-9):
                problems.append({kind: "value", "name": name, "value": actual, "expected": value})
    return problems


def landscape_color(weights, w, vectors, scalars, layer_tints):
    color = np.zeros(w.shape + (3,))
    for name in LAYER_NAMES:
        realm_layer = np.array(vectors.get(f"RealmLayerTint_{name}", [1.0, 1.0, 1.0])[:3])
        color += weights[name][..., None] * layer_tints[name] * (1.0 + (realm_layer - 1.0) * w[..., None])
    tint = np.array(vectors.get("RealmTint", [1.0, 1.0, 1.0])[:3])
    color = color * (1.0 + (tint - 1.0) * w[..., None]) * (1.0 + (scalars.get("RealmBrightness", 1.0) - 1.0) * w[..., None])
    fraction = scalars.get("RealmDesaturation", 0.0) * w
    grey = (color * LUMINANCE).sum(axis=-1)
    return color + (grey[..., None] - color) * fraction[..., None]


def mesh_default_materials(mesh_path):
    path = os.path.join(CONTENT_DIR, mesh_path.replace("/Game/", "", 1) + ".uasset")
    if not os.path.isfile(path):
        return None
    with open(path, "rb") as fp:
        return sorted({m.decode("ascii") for m in MATERIAL_REFERENCE_PATTERN.findall(fp.read())})


def check_material_blend(world):
    field = world["landscapes"][tdr.FIELD_REGION_NAME]
    layer_tints = landscape_layer_tints()
    contract = landscape_material_contract(tuple(layer_tints))
    detail = {"material_tool_parameters": {kind: sorted(names) for kind, names in contract["known"].items()},
              "required_parameters": {kind: sorted(names) for kind, names in contract["required"].items()}}
    passed = True
    field_origin = (field["center_m"][0] - field["half"], field["center_m"][1] - field["half"])
    for rid in realm_ids():
        land = world["landscapes"][rid]
        realm = land["layout"]["realm"]
        material = realm["landscape_material"]
        vectors, scalars = material["vectors"], material["scalars"]
        axis = np.arange(land["size"], dtype=np.float64) - land["half"]
        RX, RY = np.meshgrid(axis + land["center_m"][0], axis + land["center_m"][1])
        w, distance = region_weight(RX * 100.0, RY * 100.0, vectors, scalars)
        line = shared_line(world, rid)
        edge_rows = line["land_index"](land, 0)
        field_rows = line["land_index"](field, 0)
        fade = scalars.get(tdm.REGION_FADE_WIDTH_PARAMETER, tdm.REGION_DEFAULT_FADE_WIDTH_CM)
        origin = (land["center_m"][0] - land["half"], land["center_m"][1] - land["half"])
        offset = vectors.get(contract["origin_offset"], [0.0, 0.0])
        expected_offset = [origin[0] - field_origin[0], origin[1] - field_origin[1]]
        offset_ok = all(abs(offset[k] - expected_offset[k]) < 1e-9 and abs(offset[k] - round(offset[k])) < 1e-9 for k in range(2))
        partial = (w > 0.0) & (w < 1.0)
        layer_overrides = sorted(k for k in vectors if k.startswith("LayerTint_"))
        realm_weights = {n: land["layers"][n][edge_rows].astype(np.float64) / 255.0 for n in LAYER_NAMES}
        field_weights = {n: field["layers"][n][field_rows].astype(np.float64) / 255.0 for n in LAYER_NAMES}
        realm_color = landscape_color(realm_weights, w[edge_rows], vectors, scalars, layer_tints)
        field_color = landscape_color(field_weights, np.zeros(len(line["along"])), {}, {}, layer_tints)
        color_diff = float(np.abs(realm_color - field_color).max())
        realm_detail = {"edge_w_max": float(w[edge_rows].max()), "far_w_min": float(w[distance >= fade].min()) if (distance >= fade).any() else None,
                        "partial_w_outside_band": int((partial & (distance >= fade)).sum()), "origin_offset_m": offset[:2], "expected_offset_m": expected_offset,
                        "layer_tint_overrides": layer_overrides, "shared_vertex_color_max_diff": color_diff}
        ok = (realm_detail["edge_w_max"] == 0.0 and (realm_detail["far_w_min"] is None or realm_detail["far_w_min"] == 1.0) and realm_detail["partial_w_outside_band"] == 0
              and offset_ok and not layer_overrides and color_diff <= COLOR_TOLERANCE)
        realm_detail["parameter_problems"] = check_landscape_material_parameters(material, expected_region_values(field, land, contract), contract)
        ok = ok and not realm_detail["parameter_problems"]
        realm_detail.update(check_connector_mesh_materials(land))
        ok = ok and not realm_detail["connector_material_problems"]
        realm_detail.update(check_liner_weight(land, vectors, scalars))
        ok = ok and not realm_detail["liner_problems"]
        detail[rid] = realm_detail
        passed = passed and ok
    return make_check("M.region_blend", "region blend w: 0 on the shared edge, 1 beyond the fade, integer UV offset, connector MI parents = mesh mirrors, liner at w = 0, shared vertex |dRGB| <= 1e-6", passed, "see detail", None, detail)


def check_connector_mesh_materials(land):
    parents = {m["asset"]: m["parent"] for m in land["layout"]["realm"]["materials"]}
    problems = []
    checked = 0
    cache = {}
    for actor in land["layout"]["actors"]:
        if not actor["label"].startswith(gr.CONNECTOR_LABEL_PREFIX) or "material" not in actor:
            continue
        checked += 1
        defaults = cache.setdefault(actor["mesh"], mesh_default_materials(actor["mesh"]))
        parent = parents.get(actor["material"])
        if defaults is None or len(defaults) != 1:
            problems.append({"label": actor["label"], "mesh_materials": defaults})
            continue
        if parent != tdm.topdown_material_path(defaults[0]):
            problems.append({"label": actor["label"], "parent": parent, "expected": tdm.topdown_material_path(defaults[0])})
    return {"connector_material_overrides": checked, "connector_material_problems": problems[:10]}


def check_liner_weight(land, vectors, scalars):
    problems = []
    liners = [p for p in land["layout"]["planes"] if p.get("material") == tdm.SHAFT_LINER_MATERIAL_PATH]
    for plane in liners:
        x0, x1, y0, y1 = plane_rect_extent_m(plane)
        w, _ = region_weight(np.array([x0, x0, x1, x1]) * 100.0, np.array([y0, y1, y0, y1]) * 100.0, vectors, scalars)
        if float(w.max()) > 0.0:
            problems.append(plane["label"])
    for gate in land["layout"]["realm"]["gates"]:
        if gate["gate_kind"] != "Lift":
            continue
        loc = gate["lift"]["field_landing_transform"]["loc_cm"]
        w, _ = region_weight(np.array([loc[0]]), np.array([loc[1]]), vectors, scalars)
        if gate["lift"]["liner_material"] != tdm.SHAFT_LINER_MATERIAL_PATH or float(w[0]) > 0.0:
            problems.append(gate["gate_id"])
    return {"liner_planes": len(liners), "liner_problems": problems}


def item_points(layout):
    for name in ("actors", "lights", "niagara", "markers"):
        for entry in layout[name]:
            yield name, entry.get("label", ""), entry["loc"][0] / 100.0, entry["loc"][1] / 100.0
    for entry in layout["ism"]:
        for transform in entry["transforms"]:
            yield "ism", entry["key"], transform[0] / 100.0, transform[1] / 100.0


def inside_box(x, y, box):
    return box[0] < x < box[1] and box[2] < y < box[3]


def overlap_area(a, b):
    return max(0.0, min(a[1], b[1]) - max(a[0], b[0])) * max(0.0, min(a[3], b[3]) - max(a[2], b[2]))


def check_placements(world):
    field = world["landscapes"][tdr.FIELD_REGION_NAME]
    boxes = {name: realm_box(land) for name, land in world["landscapes"].items()}
    detail = {}
    total = 0
    for rid in realm_ids():
        layout = world["landscapes"][rid]["layout"]
        others = {name: box for name, box in boxes.items() if name != rid}
        bad = []
        for kind, label, x, y in item_points(layout):
            if label.startswith(gr.CONNECTOR_LABEL_PREFIX):
                continue
            hit = [name for name, box in others.items() if inside_box(x, y, box)]
            if hit:
                bad.append(f"{kind}:{label}@{round(x, 1)},{round(y, 1)}->{hit[0]}")
        for plane in layout["planes"]:
            if plane["label"].startswith(gr.CONNECTOR_LABEL_PREFIX):
                continue
            rect = plane_rect_extent_m(plane)
            hit = [name for name, box in others.items() if overlap_area(rect, box) > 1e-4]
            if hit:
                bad.append(f"plane:{plane['label']}->{hit[0]}")
        detail[rid] = {"count": len(bad), "samples": bad[:10]}
        total += len(bad)
    stray = [label for _, label, _, _ in item_points(field["layout"]) if label.startswith("TDRealm_")]
    detail[tdr.FIELD_REGION_NAME] = {"realm_labels_in_field_layout": len(stray), "samples": stray[:10]}
    total += len(stray)
    return make_check("P.placements", "realm items inside the field box or the other realm box are connector-owned only; field layout holds no realm items", total == 0, total, 0, detail)


def forbidden_keys(value, path, found):
    if isinstance(value, dict):
        for key, child in value.items():
            if key in FORBIDDEN_KEYS or FORBIDDEN_KEY_PATTERN.search(key):
                found.append(".".join(path + [key]))
            forbidden_keys(child, path + [key], found)
    if isinstance(value, list):
        for child in value:
            forbidden_keys(child, path, found)
    return found


def check_schema(world):
    detail = {}
    problems = 0
    for name, land in world["landscapes"].items():
        layout = land["layout"]
        keys = sorted(set(forbidden_keys(layout, [], [])))
        labels = sorted({e.get("label", "") for k in ("actors", "planes", "lights", "niagara", "markers") for e in layout[k] if FORBIDDEN_LABEL_PATTERN.search(e.get("label", ""))})
        detail[name] = {"forbidden_keys": keys[:10], "forbidden_labels": labels[:10]}
        problems += len(keys) + len(labels)
        if name == tdr.FIELD_REGION_NAME:
            continue
        placement = rp.REALM_PLACEMENT[name]
        expected = (int(placement["center_m"][0]), int(placement["center_m"][1]), placement["z_offset_m"])
        actual = (land["center_m"][0], land["center_m"][1], land["z_offset_m"])
        detail[name]["placement"] = {"expected": expected, "actual": actual}
        detail[name]["gameplay_data_layer"] = layout["realm"].get("gameplay_data_layer")
        problems += int(expected != actual) + int(layout["realm"].get("gameplay_data_layer") != rp.REALM_GAMEPLAY_DATA_LAYER[name])
    field_gates = {g["gate_id"]: g for g in world["landscapes"][tdr.FIELD_REGION_NAME]["layout"]["connectors"]}
    realm_gate_map = realm_gates(world)
    mismatched = sorted(gate_id for gate_id in set(field_gates) | set(realm_gate_map)
                        if gate_id not in field_gates or gate_id not in realm_gate_map
                        or any(field_gates[gate_id].get(k) != realm_gate_map[gate_id].get(k) for k in GATE_CONTRACT_KEYS))
    detail["gates"] = {"field": sorted(field_gates), "realms": sorted(realm_gate_map), "mismatched": mismatched, "expected": list(rp.GATE_IDS)}
    problems += len(mismatched) + int(sorted(realm_gate_map) != sorted(rp.GATE_IDS))
    return make_check("S.schema", "no old seam keys or labels, layout placement == REALM_PLACEMENT, field connectors[] == realm gates[] contract", problems == 0, problems, 0, detail)


def corridor_mask(world):
    X, Y = world["X"], world["Y"]
    mask = np.zeros(X.shape, dtype=bool)
    for gate_id in rp.GATE_IDS:
        for piece in rp.pieces_of_gate(gate_id):
            s, l = rp.passage_coords(piece, X, Y)
            mask |= (rp.section_reach(piece, l) <= rp.BAND_OUTER_M + CORRIDOR_MARGIN_M) & (s >= -CORRIDOR_MARGIN_M) & (s <= piece["length"] + CORRIDOR_MARGIN_M)
    for shaft in rp.SHAFTS:
        mask |= rp.landing_band_mask(shaft, X, Y, CORRIDOR_MARGIN_M)
    for rid in realm_ids():
        cx, cy = world["landscapes"][rid]["center_m"]
        for plaza in gr.gate_plazas(rid):
            mask |= np.hypot(X - (plaza["plaza"][0] + cx), Y - (plaza["plaza"][1] + cy)) <= plaza["radius_m"] + CORRIDOR_MARGIN_M
    return mask


def class_layout(count):
    names = ["field", "field_crest"]
    for rid in realm_ids()[:count]:
        names += [f"{rid}_crest", f"{rid}_band", f"{rid}_interior", f"{rid}_backdrop"]
    names += ["void", "edge_side"]
    return {name: index for index, name in enumerate(names)}


def build_plane_cover(world):
    margin = int(PLANE_MARGIN_M)
    ny, nx = world["h"].shape
    px0, py0 = world["x0"] - margin, world["y0"] - margin
    shape = (ny + 2 * margin, nx + 2 * margin)
    top = np.full(shape, -np.inf, dtype=np.float32)
    owner = np.zeros(shape, dtype=np.int8)
    count = 0
    for index, rid in enumerate(realm_ids(), start=1):
        for plane in world["landscapes"][rid]["layout"]["planes"]:
            if not any(token in plane["label"] for token in COVER_PLANE_TOKENS):
                continue
            if any(token in plane.get("material", "") for token in TRANSLUCENT_MATERIAL_TOKENS):
                continue
            if abs(plane["rot"][0]) > 1.0 or abs(plane["rot"][2]) > 1.0:
                continue
            x0, x1, y0, y1 = plane_rect_extent_m(plane)
            ix0, ix1 = max(int(math.floor(x0 - px0)), 0), min(int(math.ceil(x1 - px0)), shape[1] - 1)
            iy0, iy1 = max(int(math.floor(y0 - py0)), 0), min(int(math.ceil(y1 - py0)), shape[0] - 1)
            if ix0 > ix1 or iy0 > iy1:
                continue
            gx, gy = np.meshgrid(np.arange(ix0, ix1 + 1) + px0, np.arange(iy0, iy1 + 1) + py0)
            yaw = math.radians(plane["rot"][1])
            dx, dy = gx - plane["loc"][0] / 100.0, gy - plane["loc"][1] / 100.0
            along = dx * math.cos(yaw) + dy * math.sin(yaw)
            across = -dx * math.sin(yaw) + dy * math.cos(yaw)
            inside = (np.abs(along) <= plane["scale"][0] * 0.5) & (np.abs(across) <= plane["scale"][1] * 0.5)
            z = plane["loc"][2] / 100.0
            window = top[iy0:iy1 + 1, ix0:ix1 + 1]
            higher = inside & (z > window)
            window[higher] = z
            owner[iy0:iy1 + 1, ix0:ix1 + 1][higher] = index
            count += 1
    return {"top": top, "owner": owner, "x0": px0, "y0": py0, "planes": count}


def vision_state(world, classes):
    realms = []
    for index, rid in enumerate(realm_ids(), start=1):
        edge = rp.shared_edge(rid)
        realms.append({"index": index, "box": realm_box(world["landscapes"][rid]), "axis": edge["axis"], "value": edge["value"], "segment": edge_segment(edge),
                       "crest": classes[f"{rid}_crest"], "band": classes[f"{rid}_band"], "interior": classes[f"{rid}_interior"], "backdrop": classes[f"{rid}_backdrop"]})
    cover = build_plane_cover(world)
    floor_z = min(float(np.nanmin(world["h"])), float(cover["top"][np.isfinite(cover["top"])].min()) if np.isfinite(cover["top"]).any() else 0.0) - 1.0
    field = world["landscapes"][tdr.FIELD_REGION_NAME]
    return {"h": world["h"], "x0": world["x0"], "y0": world["y0"], "cover": cover, "realms": realms, "field_box": realm_box(field), "classes": classes, "floor_z": floor_z,
            "camera_offset": np.array(tdr.CAMERA_OFFSET_M), "pawn_center_m": tdr.PAWN_CENTER_ABOVE_GROUND_CM / 100.0}


def init_vision(state):
    VISION.update(state)


def classify_points(x, y):
    v = VISION
    classes = v["classes"]
    out = np.full(x.shape, classes["field"], dtype=np.int8)
    fb = v["field_box"]
    in_field = (x >= fb[0]) & (x <= fb[1]) & (y >= fb[2]) & (y <= fb[3])
    crest = np.zeros(x.shape, dtype=bool)
    for realm in v["realms"]:
        crest |= segment_distance(x, y, *realm["segment"]) <= CREST_M
        box = realm["box"]
        inside = ~in_field & (x >= box[0]) & (x <= box[1]) & (y >= box[2]) & (y <= box[3])
        depth = np.abs((y if realm["axis"] == "y" else x) - realm["value"])
        out[inside] = np.where(depth[inside] <= CREST_M, realm["crest"], np.where(depth[inside] <= rp.REALM_EDGE_BAND_M, realm["band"], realm["interior"]))
    out[in_field & crest] = classes["field_crest"]
    return out


def cover_at(x, y):
    cover = VISION["cover"]
    ix = np.floor(x - cover["x0"] + 0.5).astype(np.int64)
    iy = np.floor(y - cover["y0"] + 0.5).astype(np.int64)
    ok = (ix >= 0) & (iy >= 0) & (ix < cover["top"].shape[1]) & (iy < cover["top"].shape[0])
    top = np.full(x.shape, -np.inf)
    owner = np.zeros(x.shape, dtype=np.int8)
    top[ok] = cover["top"][iy[ok], ix[ok]]
    owner[ok] = cover["owner"][iy[ok], ix[ok]]
    return top, owner


def cast_rays(job):
    cells, directions = job
    v = VISION
    classes = v["classes"]
    backdrop = {realm["index"]: realm["backdrop"] for realm in v["realms"]}
    count, rays = cells.shape[0], directions.shape[0]
    camera = np.stack([cells[:, 0] + v["camera_offset"][0], cells[:, 1] + v["camera_offset"][1], cells[:, 2] + v["pawn_center_m"] + v["camera_offset"][2]], axis=1)
    camera_ground = sample_heights(v["h"], v["x0"], v["y0"], camera[:, 0], camera[:, 1])
    camera_under = np.isfinite(camera_ground) & (camera_ground > camera[:, 2])
    ox, oy, oz = (np.repeat(camera[:, k], rays) for k in range(3))
    dx, dy, dz = (np.tile(directions[:, k], count) for k in range(3))
    horizontal = np.hypot(dx, dy)
    step = RAY_STEP_M / horizontal
    total = ox.size
    result = np.full(total, classes["void"], dtype=np.int8)
    previous_void = np.zeros(total, dtype=bool)
    first_void = np.full((total, 2), np.nan, dtype=np.float32)
    t = np.full(total, RAY_START_T)
    active = np.arange(total)
    while active.size:
        ta = t[active]
        x = ox[active] + dx[active] * ta
        y = oy[active] + dy[active] * ta
        z = oz[active] + dz[active] * ta
        h = sample_heights(v["h"], v["x0"], v["y0"], x, y)
        void = ~np.isfinite(h)
        entering = void & np.isnan(first_void[active, 0])
        if entering.any():
            first_void[active[entering], 0] = x[entering]
            first_void[active[entering], 1] = y[entering]
        top, owner = cover_at(x, y)
        plane = (z <= top) & (void | (h < top))
        below = ~void & (z <= h) & ~plane
        edge_side = below & previous_void[active]
        surface = (below & ~edge_side) | (plane & ~void)
        backdrop_hit = plane & void
        if surface.any():
            result[active[surface]] = classify_points(x[surface], y[surface])
        if backdrop_hit.any():
            result[active[backdrop_hit]] = np.array([backdrop.get(int(o), classes["void"]) for o in owner[backdrop_hit]], dtype=np.int8)
        if edge_side.any():
            result[active[edge_side]] = classes["edge_side"]
        done = below | plane | (z < v["floor_z"]) | (ta * horizontal[active] > RAY_MAX_HORIZONTAL_M)
        previous_void[active] = void
        t[active] = ta + step[active]
        active = active[~done]
    return result.reshape(count, rays), camera_under, first_void.reshape(count, rays, 2)


def lattice(mask, world, step):
    iy, ix = np.nonzero(mask)
    keep = ((ix + world["x0"]) % step == 0) & ((iy + world["y0"]) % step == 0)
    iy, ix = iy[keep], ix[keep]
    return np.stack([ix + world["x0"], iy + world["y0"], world["h"][iy, ix]], axis=1).astype(np.float64), iy, ix


def vision_sets(world, reach, corridor):
    X, Y = world["X"], world["Y"]
    region = world["region"]
    sets = {}
    field_box = realm_box(world["landscapes"][tdr.FIELD_REGION_NAME])
    rim = (region == 1) & ((Y - field_box[2] <= FIELD_VISION_BAND_M) | (field_box[1] - X <= FIELD_VISION_BAND_M))
    cells, iy, ix = lattice(rim & reach, world, LATTICE_STEP_M)
    sets["field_rim"] = {"cells": cells, "region": region[iy, ix], "corridor": corridor[iy, ix]}
    for index, rid in enumerate(realm_ids(), start=2):
        band = (region == index) & (segment_distance(X, Y, *edge_segment(rp.shared_edge(rid))) <= REALM_VISION_BAND_M)
        cells, iy, ix = lattice(band & reach, world, LATTICE_STEP_M)
        sets[f"realm_edge:{rid}"] = {"cells": cells, "region": region[iy, ix], "corridor": corridor[iy, ix]}
    for gate_id in rp.GATE_IDS:
        floor = np.zeros(X.shape, dtype=bool)
        for piece in rp.pieces_of_gate(gate_id):
            s, l = rp.passage_coords(piece, X, Y)
            floor |= (s >= 0.0) & (s <= piece["length"]) & (rp.section_reach(piece, l) <= rp.FLOOR_HALF_WIDTH_M - FLOOR_REACH_INSET_M)
        if gate_id in rp.SHAFTS_BY_GATE:
            shaft = rp.SHAFTS_BY_GATE[gate_id]
            floor |= rp.landing_band_mask(shaft, X, Y) & ~rp.shaft_interior_mask(shaft, X, Y)
        cells, iy, ix = lattice(floor & reach, world, 1)
        sets[f"floor:{gate_id}"] = {"cells": cells, "region": region[iy, ix], "corridor": np.ones(len(cells), dtype=bool)}
    for shaft in rp.SHAFTS:
        half = rp.LIFT_DECK_HALF_EXTENT_CM[0] / 100.0
        points = [rp.shaft_to_grid_m(shaft, v, w) for v, w in ((0.0, 0.0), (-half, -half), (-half, half), (half, -half), (half, half))]
        heights = np.linspace(shaft["top_z_m"], shaft["bottom_z_m"], DECK_HEIGHT_COUNT)
        cells = np.array([[x, y, z] for z in heights for x, y in points], dtype=np.float64)
        sets[f"deck:{shaft['gate_id']}"] = {"cells": cells, "region": np.ones(len(cells), dtype=np.int8), "corridor": np.ones(len(cells), dtype=bool)}
    return sets


def allowed_classes(classes, region_index):
    if region_index == 1:
        return [classes["field"], classes["field_crest"]] + [classes[f"{rid}_crest"] for rid in realm_ids()]
    rid = realm_ids()[region_index - 2]
    return [classes[f"{rid}_{part}"] for part in ("crest", "band", "interior", "backdrop")] + [classes["field_crest"]]


def leak_clusters(cells):
    if len(cells) == 0:
        return []
    ix = ((cells[:, 0] - cells[:, 0].min()) // LATTICE_STEP_M).astype(int)
    iy = ((cells[:, 1] - cells[:, 1].min()) // LATTICE_STEP_M).astype(int)
    image = np.zeros((iy.max() + 1, ix.max() + 1), dtype=bool)
    image[iy, ix] = True
    labels, count = ndimage.label(ndimage.binary_dilation(image, iterations=2))
    clusters = []
    for label in range(1, count + 1):
        member = labels[iy, ix] == label
        if not member.any():
            continue
        xs, ys = cells[member, 0], cells[member, 1]
        clusters.append({"cells": int(member.sum()), "x_m": [float(xs.min()), float(xs.max())], "y_m": [float(ys.min()), float(ys.max())],
                         "ground_z_m": [round(float(cells[member, 2].min()), 2), round(float(cells[member, 2].max()), 2)]})
    return sorted(clusters, key=lambda c: -c["cells"])[:CLUSTER_REPORT_LIMIT]


def outside_field_scope(first_void, field_box):
    x, y = first_void[..., 0], first_void[..., 1]
    beyond_west_north = (x < field_box[0]) | (y > field_box[3])
    beyond_south_east = (y < field_box[2]) | (x > field_box[1])
    return np.isfinite(x) & beyond_west_north & ~beyond_south_east


def evaluate_vision(sets, results, classes, field_box):
    names = {index: name for name, index in classes.items()}
    bad = np.array([classes["void"], classes["edge_side"]])
    summary = {}
    for set_name, data in sets.items():
        for aspect, (cls, camera_under, first_void) in results[set_name].items():
            corridor = data["corridor"]
            allowed = np.ones(cls.shape, dtype=bool)
            for region_index in np.unique(data["region"]):
                rows = data["region"] == region_index
                allowed[rows] = np.isin(cls[rows], allowed_classes(classes, int(region_index)))
            bad_rays = np.isin(cls, bad)
            disallowed_rays = ~allowed & ~corridor[:, None]
            field_cells = (data["region"] == 1)[:, None]
            out_of_scope = field_cells & outside_field_scope(first_void, field_box)
            void_edge = (bad_rays & ~out_of_scope).any(axis=1)
            disallowed = (disallowed_rays & ~bad_rays & ~out_of_scope).any(axis=1)
            u12 = ((bad_rays | disallowed_rays) & out_of_scope).any(axis=1) & ~void_edge & ~disallowed
            under = camera_under & ~corridor
            seen = {names[c]: int((cls == c).any(axis=1).sum()) for c in np.unique(cls)}
            entry = {"cells": int(len(cls)), "rays_per_cell": int(cls.shape[1]) if len(cls) else 0, "corridor_cells": int(corridor.sum()),
                     "void_or_edge_cells": int(void_edge.sum()), "disallowed_outside_corridor_cells": int(disallowed.sum()),
                     "out_of_scope_west_north_edge_cells_U12": int(u12.sum()), "camera_under_outside_corridor_cells": int(under.sum()),
                     "camera_under_in_corridor_cells": int((camera_under & corridor).sum()), "cells_seeing_class": seen}
            if (void_edge | disallowed).any():
                entry["leak_clusters"] = leak_clusters(data["cells"][void_edge | disallowed])
            if u12.any():
                entry["u12_clusters"] = leak_clusters(data["cells"][u12])
            summary.setdefault(set_name, {})[aspect] = entry
    return summary


def run_vision(world, reach, workers):
    corridor = corridor_mask(world)
    classes = class_layout(len(realm_ids()))
    sets = vision_sets(world, reach, corridor)
    state = vision_state(world, classes)
    results = {}
    started = time.time()
    with Pool(workers, initializer=init_vision, initargs=(state,)) as pool:
        for aspect, ratio in tdr.VIEW_ASPECT_RATIOS.items():
            directions = tdr.view_ray_directions(ratio)
            for set_name, data in sets.items():
                cells = data["cells"]
                if len(cells) == 0:
                    results.setdefault(set_name, {})[aspect] = (np.zeros((0, len(directions)), dtype=np.int8), np.zeros(0, dtype=bool), np.zeros((0, len(directions), 2), dtype=np.float32))
                    continue
                jobs = [(cells[i:i + CELLS_PER_JOB], directions) for i in range(0, len(cells), CELLS_PER_JOB)]
                parts = pool.map(cast_rays, jobs)
                results.setdefault(set_name, {})[aspect] = tuple(np.concatenate([p[k] for p in parts]) for k in range(3))
    summary = evaluate_vision(sets, results, classes, realm_box(world["landscapes"][tdr.FIELD_REGION_NAME]))
    meta = {"seconds": round(time.time() - started, 1), "cover_planes": state["cover"]["planes"], "ray_grids": {a: list(tdr.view_ray_grid(r)) for a, r in tdr.VIEW_ASPECT_RATIOS.items()},
            "half_vertical_fov_deg": round(math.degrees(tdr.CAMERA_HALF_VERTICAL_FOV_RAD), 3),
            "half_horizontal_fov_deg": {a: round(math.degrees(tdr.half_horizontal_fov_rad(r)), 3) for a, r in tdr.VIEW_ASPECT_RATIOS.items()}, "classes": list(classes)}
    return sets, summary, meta, corridor


def check_vision(summary, meta):
    checks = []
    for aspect in tdr.VIEW_ASPECT_RATIOS:
        per_set = {name: entries[aspect] for name, entries in summary.items()}
        void_edge = sum(e["void_or_edge_cells"] for e in per_set.values())
        disallowed = sum(e["disallowed_outside_corridor_cells"] for e in per_set.values())
        checks.append(make_check(f"K5.vision_{aspect}", f"{aspect} MaintainYFOV: no void or landscape edge side from any target cell; outside connector corridors only own region and crests visible",
                                 void_edge == 0 and disallowed == 0, {"void_or_edge": void_edge, "disallowed_outside_corridor": disallowed}, 0, dict(per_set, meta=meta)))
    return checks


def camera_burial_m(height, x0, y0, QX, QY):
    offset = tdr.CAMERA_OFFSET_M
    camera_z = rp.quad_center_heights(height) + tdr.PAWN_CENTER_ABOVE_GROUND_CM / 100.0 + offset[2]
    ground = sample_heights(height, x0, y0, QX + offset[0], QY + offset[1])
    return np.where(np.isfinite(ground) & np.isfinite(camera_z), ground - camera_z, -np.inf)


def landscape_seeds_m(land, name):
    kind = "PlayerStart" if name == tdr.FIELD_REGION_NAME else "RealmArrival"
    return [(m["loc"][0] / 100.0 - land["center_m"][0], m["loc"][1] / 100.0 - land["center_m"][1]) for m in land["layout"]["markers"] if m.get("kind") == kind]


def standalone_buried_reached(land, name):
    local = land["height_m"] - land["z_offset_m"]
    axis = np.arange(land["size"], dtype=np.float64) - land["half"]
    PX, PY = np.meshgrid(axis, axis)
    QX, QY = PX[:-1, :-1] + 0.5, PY[:-1, :-1] + 0.5
    regions = np.full(QX.shape, 1 if name == tdr.FIELD_REGION_NAME else 2, dtype=np.int8)
    recovery = {} if name == tdr.FIELD_REGION_NAME else {2: realm_recovery_m(land["layout"]) - land["z_offset_m"]}
    labels = rp.quad_components(walkable_quads(local, regions, recovery))
    seeds = {quad_label(labels, -land["half"], -land["half"], x, y) for x, y in landscape_seeds_m(land, name)} - {0}
    reached = np.isin(labels, sorted(seeds))
    return reached & (camera_burial_m(local, -land["half"], -land["half"], QX, QY) > 0.0), local, len(seeds)


def baseline_burial(world, field_regions, prev_field_dir, prev_realms_dir):
    buried = np.zeros((world["h"].shape[0] - 1, world["h"].shape[1] - 1), dtype=bool)
    changed = np.isfinite(world["h"])
    sources = {tdr.FIELD_REGION_NAME: prev_field_dir}
    sources.update({rid: os.path.join(prev_realms_dir, rid) if prev_realms_dir else None for rid in realm_ids()})
    used = {}
    for index, name in enumerate(world["names"], start=1):
        folder = sources.get(name)
        if not folder or not os.path.isfile(os.path.join(folder, "layout.json")):
            continue
        land = tdr.read_landscape(folder)
        current = world["landscapes"][name]
        if land["size"] != current["size"]:
            continue
        prev_buried, prev_local, seeds = standalone_buried_reached(land, name)
        window = landscape_window(world, current)
        quad_window = landscape_window(world, current, -1)
        current_local = current["height_m"] - current["z_offset_m"]
        diff = np.round(prev_local * current["units_per_meter"]) != np.round(current_local * current["units_per_meter"])
        if name == tdr.FIELD_REGION_NAME:
            diff |= np.logical_or.reduce(list(field_regions.values()))
        own = world["region"][window] == index
        changed[window] &= ~own | diff
        buried[quad_window] |= prev_buried & (quad_regions(world["region"])[quad_window] == index)
        used[name] = {"seeds": seeds, "baseline_buried_reached_quads": int(prev_buried.sum()), "changed_vertices": int((diff & own).sum())}
    return buried, changed, used


def check_camera_under(world, quad_reach, field_regions, prev_field_dir, prev_realms_dir):
    QX, QY = quad_axes(world)
    depth = camera_burial_m(world["h"], world["x0"], world["y0"], QX, QY)
    under = quad_reach & (depth > 0.0)
    buried_before, changed, used = baseline_burial(world, field_regions, prev_field_dir, prev_realms_dir)
    changed_quads = quad_corners_any(ndimage.binary_dilation(changed, iterations=CAMERA_REACH_CELLS))
    inherited = under & buried_before & ~changed_quads
    failing = under & ~inherited
    regions = quad_regions(world["region"])
    detail = {"model": f"strict quad flood, every triangle <= {WALK_FLOOD_MAX_SLOPE_DEG} deg, pawn at quad centre, camera {tuple(round(v, 3) for v in tdr.CAMERA_OFFSET_M)} m + {tdr.PAWN_CENTER_ABOVE_GROUND_CM / 100.0} m",
              "baseline": used, "changed_dilation_cells": CAMERA_REACH_CELLS, "reached_quads": int(quad_reach.sum()), "under_quads": int(under.sum()),
              "inherited_unchanged_baseline_quads": int(inherited.sum()), "failing_by_region": {name: int((failing & (regions == i)).sum()) for i, name in enumerate(world["names"], start=1)}}
    for key, mask in (("failing_clusters", failing), ("inherited_clusters", inherited)):
        if not mask.any():
            continue
        points = np.stack([QX[mask], QY[mask], depth[mask]], axis=1)
        clusters = leak_clusters(points)
        for cluster in clusters:
            member = (points[:, 0] >= cluster["x_m"][0]) & (points[:, 0] <= cluster["x_m"][1]) & (points[:, 1] >= cluster["y_m"][0]) & (points[:, 1] <= cluster["y_m"][1])
            cluster["max_burial_m"] = round(float(points[member, 2].max()), 2)
            cluster.pop("ground_z_m", None)
        detail[key] = clusters
    return make_check("K5.camera_under", "no reachable quad (inside or outside corridors) has the camera below terrain, except quads buried and reachable at the same position in the baseline (*_prev_p3-19) and outside the changed union dilated by the camera reach",
                      not failing.any(), int(failing.sum()), 0, detail)


def wp_cells_of_box(box):
    eps = 1e-6
    xs = range(int(math.floor(box[0] / WP_CELL_M)), int(math.floor((box[1] - eps) / WP_CELL_M)) + 1)
    ys = range(int(math.floor(box[2] / WP_CELL_M)), int(math.floor((box[3] - eps) / WP_CELL_M)) + 1)
    return [(i, j) for i in xs for j in ys]


def distance_to_wp_cell(px, py, cell):
    x0, y0 = cell[0] * WP_CELL_M, cell[1] * WP_CELL_M
    dx = np.maximum(np.maximum(x0 - px, 0.0), px - (x0 + WP_CELL_M))
    dy = np.maximum(np.maximum(y0 - py, 0.0), py - (y0 + WP_CELL_M))
    return np.hypot(dx, dy)


def check_distance(world, sets):
    detail = {}
    worst = 0.0
    for gate_id in rp.GATE_IDS:
        rid = rp.SHAFTS_BY_GATE[gate_id]["realm_id"] if gate_id in rp.SHAFTS_BY_GATE else rp.CONNECTORS_BY_GATE[gate_id]["realm_id"]
        needed = wp_cells_of_box(realm_box(world["landscapes"][rid]))
        points = [sets[f"floor:{gate_id}"]["cells"]]
        if f"deck:{gate_id}" in sets:
            points.append(sets[f"deck:{gate_id}"]["cells"])
        points = np.concatenate(points)
        distance = np.zeros(len(points))
        for cell in needed:
            distance = np.maximum(distance, distance_to_wp_cell(points[:, 0], points[:, 1], cell))
        k = int(np.argmax(distance))
        detail[gate_id] = {"realm": rid, "needed_cells": [list(c) for c in needed], "points": int(len(points)), "max_distance_m": round(float(distance[k]), 1),
                           "worst_point_m": [round(float(points[k, 0]), 2), round(float(points[k, 1]), 2)]}
        worst = max(worst, float(distance[k]))
    return make_check("K6.distance", f"every connector floor/deck point is within {DISTANCE_LIMIT_M:.0f} m of every WP cell of its destination realm", worst <= DISTANCE_LIMIT_M, round(worst, 1), DISTANCE_LIMIT_M, detail)


def json_default(value):
    if isinstance(value, np.bool_):
        return bool(value)
    if isinstance(value, np.integer):
        return int(value)
    if isinstance(value, (np.floating, float)):
        return float(value)
    if isinstance(value, (tuple, set)):
        return list(value)
    raise TypeError(f"not serializable: {type(value)}")


def format_value(value):
    text = json.dumps(value, ensure_ascii=False, default=json_default)
    return text if len(text) <= 120 else text[:117] + "..."


def run_checks(args):
    world = load_inputs(args.field_dir, args.realms_dir)
    checks = [check_determinism(world, args.field_dir, args.realms_dir, not args.no_rerun)]
    field = world["landscapes"][tdr.FIELD_REGION_NAME]
    terrain = gav.build_terrain(np.random.default_rng(field["layout"]["seed"]))
    regions = field_change_union(field, terrain)
    checks.append(check_field_heights(field, terrain, regions))
    checks.append(check_field_ism(field, terrain, regions, args.prev_field_dir))
    checks += check_shared_edges(world)
    slope = slope_deg(world["h"])
    checks.append(check_grades(world))
    checks.append(check_grade_windows(world))
    checks.append(check_floor_slopes(world, slope))
    checks.append(check_slot_width(world))
    quad_reach, reach_meta = reachable_quads(world, world_walkable_quads(world))
    sets, summary, meta, corridor = run_vision(world, quad_vertices(quad_reach), args.workers)
    meta["reach"] = reach_meta
    checks += check_vision(summary, meta)
    checks.append(check_camera_under(world, quad_reach, regions, args.prev_field_dir, args.prev_realms_dir))
    checks.append(check_distance(world, sets))
    checks.append(check_ridge_block(world, quad_reach))
    checks.append(check_wall_tops(world, quad_reach))
    checks.append(check_river(world))
    checks.append(check_liner(world))
    checks.append(check_material_blend(world))
    checks.append(check_placements(world))
    checks.append(check_schema(world))
    return checks


def main():
    ap = argparse.ArgumentParser(description="Deterministic checks of the continuous world (field + realms) generator outputs.")
    ap.add_argument("--field-dir", default=os.path.join(WORLDGEN_DIR, "AshenVale"))
    ap.add_argument("--realms-dir", default=os.path.join(WORLDGEN_DIR, "Realms"))
    ap.add_argument("--prev-field-dir", default=os.path.join(WORLDGEN_DIR, "AshenVale_prev_p3-19"))
    ap.add_argument("--prev-realms-dir", default=os.path.join(WORLDGEN_DIR, "Realms_prev_p3-19"))
    ap.add_argument("--no-rerun", action="store_true")
    ap.add_argument("--workers", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    ap.add_argument("--report", default=DEFAULT_REPORT)
    args = ap.parse_args()
    started = time.time()
    try:
        checks = run_checks(args)
    except TDInputError as e:
        print(f"input error: {e}", file=sys.stderr)
        return 2
    failed = [c for c in checks if c["passed"] is False]
    report = {"passed": not failed, "seconds": round(time.time() - started, 1), "checks": checks}
    os.makedirs(os.path.dirname(os.path.abspath(args.report)), exist_ok=True)
    temp_path = os.path.abspath(args.report) + ".tmp"
    with open(temp_path, "w", encoding="utf-8", newline="\n") as fp:
        json.dump(report, fp, ensure_ascii=False, indent=1, default=json_default)
    os.replace(temp_path, os.path.abspath(args.report))
    for c in checks:
        state = "SKIP" if c["passed"] is None else ("PASS" if c["passed"] else "FAIL")
        print(f"[{state}] {c['id']} {c['name']}: {format_value(c['value'])} (limit {format_value(c['limit'])})")
    print(f"result: {'PASS' if not failed else 'FAIL'} ({len(failed)} failed, {sum(1 for c in checks if c['passed'] is None)} skipped) in {report['seconds']} s")
    print("report:", os.path.abspath(args.report))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
