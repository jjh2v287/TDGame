"""
실행 환경: Python 3.12 + numpy 2.2 + scipy 1.17 (추가 설치 없음), 저장소 루트에서 실행. 같은 폴더의 잠긴 fog3d_core·fog3d_core_v2·fog3d_terrain·fog3d_terrain_add2·fog3d_replay·fog3d_replay_s2a·fog3d_blend_patch 를 import 만 하고 세 클래스의 _apply_inputs 와 재생 입력 함수를 입력 가림(닻 채우기) 판으로 갈아 끼운다
실행 명령: PYTHONUTF8=1 python -B Tools/FluidFX/fog3d_ref/fog3d_occlusion_ref.py unit | scenes | precheck | table | check-gates [--require-complete] | anchored --flavor r5|b4|stage1|scenes --only 이름,… [--occlusion off|on] [--shake] [--jobs N] [--no-compare] | judge-on [--jobs N] | generate --groups onoff,changed,changed-off [--jobs N] | onoff | numbers | compare-reach --ref-dir <기준 덤프 폴더> --gpu-dir <GPU 덤프 폴더> [--scenes 이름,…] [--report <json>](이어짐 비트 덤프 바이트 대조, 끝 0 같음·1 다름 또는 빠짐·2 기준 없음)
출력: 장면 재생 Tools/FluidFX/fog3d_ref/replays_occ/<scenes|r5|b4|stage1>/(<장면>.json·<장면>.ground.bin·manifest.json), 장면 해시·앞조건·손 식 집합·켬 판정·생성 기록·켬 = 끔 대조 Saved/AgentOps/20261117/fog3d-2b/occ-*.json, 닻 판 기준 덤프 Saved/FluidFX/Fog3D/ref_occ/<묶음>/<off|on>/[shake/](<장면>__<덤프>.npz·.column.bin·.frames.bin·.reach.bin·manifest.json), 숫자 파일 Docs/VolumeFog3D/measurements/stage2b-occ/prereg-2b-occ-numbers.json, 표준 출력 json
상태: 3D 안개 2b-1 입력 가림 기준 계산·B2-2 장면·가림 끔 앞조건·가림 켬 주 판정·닻 판 생성·켬 = 끔 대조·B2-3 숫자 파일·나눔 확인 도구(Docs/VolumeFog3D/stage2/prereg-2b-design.md v3.1 1.2·B2-2·B2-3·4절), 2026-11-18
"""
import os

for _thread_var in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS"):
    os.environ.setdefault(_thread_var, "1")

import argparse
import concurrent.futures
import copy
import hashlib
import json
import math
import sys
import time
import types

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import numpy as np  # noqa: E402

import fog3d_blend_patch as blend_patch  # noqa: E402
import fog3d_core as core  # noqa: E402
import fog3d_core_v2 as core2  # noqa: E402
import fog3d_replay as rp  # noqa: E402
import fog3d_replay_s2a as rs  # noqa: E402
import fog3d_terrain as terrain  # noqa: E402
import fog3d_terrain_add2 as terrain_add2  # noqa: E402
from fog3d_core import F32  # noqa: E402

ROOT = core.ROOT
TOOL_PATH = os.path.abspath(__file__)
PREREG = os.path.join(ROOT, "Docs", "VolumeFog3D", "stage2", "prereg-2b-design.md")
PREREG_SHA256 = "117cc69bacb11d679effb0da14b3820596b739bbf1116c85fef4d76d7daf7367"
OCC_REPLAYS = os.path.join(HERE, "replays_occ")
REPLAY_FOLDERS = {key: os.path.join(OCC_REPLAYS, key) for key in ("scenes", "r5", "b4", "stage1")}
REF_OCC = os.path.join(ROOT, "Saved", "FluidFX", "Fog3D", "ref_occ")
BASE_REF = dict(r5=os.path.join(ROOT, "Saved", "FluidFX", "Fog3D", "ref_s2a", "R5"), b4=os.path.join(ROOT, "Saved", "FluidFX", "Fog3D", "ref_blend_b4"),
                stage1=os.path.join(ROOT, "Saved", "FluidFX", "Fog3D", "ref_v2"))
STAGE1_REPLAYS = os.path.join(HERE, "replays")
OPS = os.path.join(ROOT, "Saved", "AgentOps", "20261117", "fog3d-2b")
SCENE_HASHES = os.path.join(OPS, "occ-scene-hashes.json")
PRECHECK_OUT = os.path.join(OPS, "occ-precheck.json")
HAND_OUT = os.path.join(OPS, "occ-hand-sets.json")

HX = rp.HX
LAYER_CM = rp.LAYER_CM
LAYERS = rp.LAYERS
DT = rs.DT
SCENE = rs.SCENE
FILL_LIMIT_CELLS = 48
REACH_SLACK = 2
DISTANCE_CAP = 255
CULL_MARGIN_CELLS = 2.0
ANCHOR_CHECK_CM = 0.01
ANCHOR_NUDGE_CM = 0.5
SHAPE_CHECK_CM = 0.01
ANCHOR_HEIGHT_ABOVE_GROUND = 105.0
BLADE_ANCHOR_BACK_CM = 60.0
EDGE_ANCHOR_OUT_CM = 20.0
SPEED_LIMIT = 3000.0
HAND_UNREACHABLE = 10 ** 6

WINDOW = (0, -64)
WALL_FRONT_X = 1095.0
WALL_THICKNESS = 10.0
WALL_HEIGHT = 260.0
WALL_LENGTH = 300.0
SHORT_WALL_LENGTH = 60.0
PAWN_END_X = 1031.0
TIP_REACH_CM = 95.6
BLADE_LENGTH_CM = 96.1
BLADE_SPHERE_COUNT = 4
SWING_TIP_HALF_Y = 50.0
W2_TIP_GAP_CM = 5.0
W4_PELVIS_GAP_CM = 34.0
W4_WALK_HALF_Y = 100.0
W4_STAND_STEPS = 30
W5_PAWN_Y = 150.0
PELVIS_HEIGHT = SCENE["rig"][2][2]
PELVIS_RADIUS = SCENE["rig"][2][3]
BLADE_HEIGHT = SCENE["blade_height"]
BLADE_RADIUS = SCENE["blade_radius"]
BLADE_WIND = SCENE["blade_wind"]
BLADE_ERASE = SCENE["blade_erase"]
BLADE_SPEED = SCENE["blade_speed"]
BODY_WIND = SCENE["body_wind"]
BODY_ERASE = SCENE["body_erase"]
WALK_SPEED = SCENE["walk_speed"]
TAIL_STEPS = SCENE["g4_tail_steps"]
PLATFORM_CENTER = (1000.0, 0.0, 125.0)
PLATFORM_HALF = (100.0, 100.0, 125.0)
LAYER7_PELVIS = (1060.0, 0.0, 2.0 * PLATFORM_HALF[2] + PELVIS_HEIGHT)
LAYER7_HAND = (1080.0, 0.0, 330.0)
LAYER7_DOWN_DEG = 60.0
LAYER7_STEPS = 8
LAYER7_TAIL = 20
TELEPORT_CM = 800.0
TELEPORT_WALK_STEPS = 6
TELEPORT_STAND_STEPS = 20

OCC_SCENE_LABELS = {"occ_w1_tip_through": "W1", "occ_w2_radius_reach": "W2", "occ_w4_body_parallel": "W4", "occ_w3_short_wall": "W3",
                    "occ_w5_end_row": "W5", "occ_edge_layer7": "가장자리-닻 층 7 위", "occ_edge_teleport": "가장자리-순간이동"}
PRECHECK_SCENES = ("occ_w1_tip_through", "occ_w2_radius_reach", "occ_w4_body_parallel")
HAND_SCENES = ("occ_w3_short_wall", "occ_w5_end_row")
W_NAMES = ("occ_w1_tip_through", "occ_w2_radius_reach", "occ_w4_body_parallel", "occ_w3_short_wall", "occ_w5_end_row")
EDGE_OUTSIDE = "s2a_frame_at_edge"
EDGE_CHANGED = ("occ_edge_layer7", "occ_edge_teleport")
ON_EQ_OFF_S2A = ("s2a_flat_offset", "s2a_slope_walk_up", "s2a_slope_walk_down", "s2a_slope_y_walk", "s2a_frame_at_edge")
CHANGED_S2A = ("s2a_box_on_slope", "s2a_cliff_at_edge", "s2a_cliff_walk_blade", "s2a_slope_curb60", "s2a_hill_run_window", "s2a_still_rough",
               "s2a_slope_curb", "s2a_curb30_cross")
STAGE1_CHANGED = ("k3_box_high", "k4_run", "k7_thin_swing", "still_600", "mixed_dt_120")
B4_SLOPE = "b4_slope_walk_up_h60_g125"
B4_FLAT_COUNTS = {"b4_walk_stand_": 5, "b4_blade_": 4, "b4_turn_stand_": 4}
PATH_SCENES = ("s2a_frame_at_edge",)
SCENE_ANCHOR_RULE = "장면 정의(구를 낸 폰의 골반)"
JUDGE_SCENES = tuple(("scenes", n) for n in W_NAMES + EDGE_CHANGED) + (("r5", EDGE_OUTSIDE),)
JUDGE_OUT = os.path.join(OPS, "occ-on-judge.json")
ONOFF_OUT = os.path.join(OPS, "occ-onoff.json")
GENERATE_GROUPS = ("onoff", "changed", "changed-off")
NUMBERS_OUT = os.path.join(ROOT, "Docs", "VolumeFog3D", "measurements", "stage2b-occ", "prereg-2b-occ-numbers.json")
NUMBERS_FORMAT = "fog3d-prereg-2b-occ-numbers-1"
NUMBER_GROUPS = ("wind", "disp", "fill")
LIMIT_FLOOR = 0.002
LIMIT_CEILING = 0.03
DEFAULT_NUMBER_LIMITS = dict(relative=dict(wind=0.002, disp=0.002, fill=0.002), slope=1.0e-6, frames_position_cm=0.001, frames_slope=1.0e-5)
NUMBERS_RULE = ("X = min(0.03, max(0.002, 3 x sensitivity)); sensitivity = fog3d_compare_v2.group_diffs(가림 켬 닻 판 기준, 흔든 가림 켬 닻 판 기준); "
                "only dumps and groups with X > 0.002 are listed under dumps, all others use default.relative (s2a 규칙, B2-3 바뀜 묶음 20장면); "
                "groups whose fields are absent from a dump (1단계 중간 덤프 u·v·w 만: disp·fill) get no sensitivity, see shake[dump].absent_groups")

PATCH_CLASSES = (core.Sim, terrain.TerrainSim, terrain_add2.TerrainSimR5)
LOCKED_APPLY = {cls: cls.__dict__["_apply_inputs"] for cls in PATCH_CLASSES}
BLENDED_APPLY = {core.Sim: blend_patch.blended_core_apply_inputs, terrain.TerrainSim: blend_patch.blended_terrain_apply_inputs,
                 terrain_add2.TerrainSimR5: blend_patch.blended_r5_apply_inputs}
LOCKED_CORE_SPHERES = rp.core_spheres
LOCKED_ERASE_ONLY = core._erase_only
RUN = dict(occlusion=None, capture_reach=False, box_check=False, region=None, sim=None, fill_sink=None)

for _cls, _fn in LOCKED_APPLY.items():
    if _fn.__module__ != _cls.__module__:
        raise RuntimeError(f"{_cls.__name__}._apply_inputs 가 이미 갈아 끼워져 있음: {_fn.__module__}")


def sha256_file(path):
    with open(path, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def write_json(path, body):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(body, fh, ensure_ascii=False, indent=1)
        fh.write("\n")


def read_json(path):
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)


def check_prereg():
    actual = sha256_file(PREREG)
    if actual != PREREG_SHA256:
        raise SystemExit(f"등록 지문이 다름: {actual} != {PREREG_SHA256}")
    return actual


def cell_center32(index):
    return np.float32((np.float32(index) + np.float32(0.5)) * np.float32(HX))


def predicate_range(start, end, margin):
    low = np.float32(min(start, end) - margin)
    high = np.float32(max(start, end) + margin)
    lo = int(math.floor(float(low) / HX - 0.5))
    while cell_center32(lo) < low:
        lo += 1
    while cell_center32(lo - 1) >= low:
        lo -= 1
    hi = int(math.floor(float(high) / HX - 0.5)) + 1
    while cell_center32(hi) > high:
        hi -= 1
    while cell_center32(hi + 1) <= high:
        hi += 1
    tie = min(float(cell_center32(lo)) - float(low), float(low) - float(cell_center32(lo - 1)), float(high) - float(cell_center32(hi)),
              float(cell_center32(hi + 1)) - float(high))
    return lo, hi, tie


def predicate_box(a_local, b_local, radius):
    margin = np.float32(np.float32(radius) + np.float32(np.float32(HX) * np.float32(CULL_MARGIN_CELLS)))
    x0, x1, tx = predicate_range(np.float32(a_local[0]), np.float32(b_local[0]), margin)
    y0, y1, ty = predicate_range(np.float32(a_local[1]), np.float32(b_local[1]), margin)
    return (x0, x1, y0, y1), min(tx, ty)


def clip_box(box, nx, ny):
    return max(box[0], 0), min(box[1], nx - 1), max(box[2], 0), min(box[3], ny - 1)


def column_ground32(sim, i, j):
    heights = getattr(sim, "column_height", None)
    if heights is None or not getattr(sim, "follows_terrain", False):
        return np.float32(0.0)
    return np.float32(heights[i, j])


def anchor_layer(height32):
    return math.floor(float(np.float32(height32 / np.float32(LAYER_CM))))


def anchor_cell(sim, anchor):
    ox, oy = sim.offset_cm()
    raw_i = math.floor((float(anchor[0]) + ox) / sim.p.hx)
    raw_j = math.floor((float(anchor[1]) + oy) / sim.p.hy)
    i = min(max(raw_i, 0), sim.nx - 1)
    j = min(max(raw_j, 0), sim.ny - 1)
    ground = column_ground32(sim, i, j)
    height = np.float32(np.float32(anchor[2]) - ground)
    raw_k = anchor_layer(height)
    k = min(max(raw_k, 0), sim.nz - 1)
    return (i, j, k), dict(raw=(raw_i, raw_j, raw_k), ground_cm=float(ground), height_cm=float(height))


def manhattan(shape, start):
    gi = np.abs(np.arange(shape[0]) - start[0])[:, None, None]
    gj = np.abs(np.arange(shape[1]) - start[1])[None, :, None]
    gk = np.abs(np.arange(shape[2]) - start[2])[None, None, :]
    return gi + gj + gk


def walk_distance(open_x, open_y, open_z, shape, start):
    cap = np.int16(DISTANCE_CAP)
    distance = np.full(shape, DISTANCE_CAP, np.int16)
    distance[start] = 0
    while True:
        before = distance.copy()
        step = np.minimum(distance + np.int16(1), cap).astype(np.int16)
        np.minimum(distance[1:], np.where(open_x, step[:-1], cap), out=distance[1:])
        np.minimum(distance[:-1], np.where(open_x, step[1:], cap), out=distance[:-1])
        np.minimum(distance[:, 1:], np.where(open_y, step[:, :-1], cap), out=distance[:, 1:])
        np.minimum(distance[:, :-1], np.where(open_y, step[:, 1:], cap), out=distance[:, :-1])
        np.minimum(distance[:, :, 1:], np.where(open_z, step[:, :, :-1], cap), out=distance[:, :, 1:])
        np.minimum(distance[:, :, :-1], np.where(open_z, step[:, :, 1:], cap), out=distance[:, :, :-1])
        if np.array_equal(distance, before):
            return distance


def fill_reach(sim, anchor, a_local, b_local, radius, walk=True):
    pred_raw, tie = predicate_box(a_local, b_local, radius)
    (ai, aj, ak), info = anchor_cell(sim, anchor)
    box = clip_box((min(pred_raw[0], ai), max(pred_raw[1], ai), min(pred_raw[2], aj), max(pred_raw[3], aj)), sim.nx, sim.ny)
    bx0, bx1, by0, by1 = box
    fluid = sim.fluid[bx0:bx1 + 1, by0:by1 + 1, :]
    out = dict(pred=clip_box(pred_raw, sim.nx, sim.ny), box=box, anchor_cell=(ai, aj, ak), anchor_info=info, predicate_tie_cm=tie,
               blocked_cells=int(fluid.size - np.count_nonzero(fluid)),
               oversize=(bx1 - bx0 + 1) > FILL_LIMIT_CELLS or (by1 - by0 + 1) > FILL_LIMIT_CELLS,
               anchor_blocked=not bool(sim.fluid[ai, aj, ak]), walked=False, reach=None, distance=None)
    out["allowed"] = out["oversize"] or out["anchor_blocked"]
    if out["allowed"] or not walk:
        return out
    if out["blocked_cells"] == 0:
        out["reach"] = np.ones(fluid.shape, bool)
        return out
    start = (ai - bx0, aj - by0, ak)
    distance = walk_distance(sim.ou[bx0:bx1, by0:by1 + 1, :], sim.ov[bx0:bx1 + 1, by0:by1, :], sim.ow[bx0:bx1 + 1, by0:by1 + 1, :sim.nz - 1],
                             fluid.shape, start)
    out["distance"] = distance
    out["reach"] = distance <= manhattan(fluid.shape, start) + REACH_SLACK
    out["walked"] = True
    return out


def sphere_bits(fill, shape):
    bits = np.zeros(shape, bool)
    if fill["reach"] is None:
        x0, x1, y0, y1 = fill["pred"]
        if x0 <= x1 and y0 <= y1:
            bits[x0:x1 + 1, y0:y1 + 1, :] = True
        return bits
    bx0, bx1, by0, by1 = fill["box"]
    bits[bx0:bx1 + 1, by0:by1 + 1, :] = fill["reach"]
    return bits


def new_stats():
    return dict(sphere_steps=0, no_anchor=0, allowed=0, allowed_oversize=0, allowed_anchor_blocked=0, walked=0, boxes_with_blocked=0,
                blocked_cells=0, anchor_clamped_ij=0, anchor_clamped_k=0, occluded_pred_cells=0, predicate_tie_min_cm=None)


def note_fill(sim, step_index, index, fill):
    stats = sim.occ_stats
    stats["allowed"] += int(fill["allowed"])
    stats["allowed_oversize"] += int(fill["oversize"])
    stats["allowed_anchor_blocked"] += int(fill["anchor_blocked"])
    stats["walked"] += int(fill["walked"])
    stats["boxes_with_blocked"] += int(fill["blocked_cells"] > 0)
    stats["blocked_cells"] += fill["blocked_cells"]
    raw = fill["anchor_info"]["raw"]
    cell = fill["anchor_cell"]
    stats["anchor_clamped_ij"] += int(raw[0] != cell[0] or raw[1] != cell[1])
    stats["anchor_clamped_k"] += int(raw[2] != cell[2])
    tie = stats["predicate_tie_min_cm"]
    stats["predicate_tie_min_cm"] = fill["predicate_tie_cm"] if tie is None else min(tie, fill["predicate_tie_cm"])
    if fill["reach"] is not None:
        x0, x1, y0, y1 = fill["pred"]
        bx0, _, by0, _ = fill["box"]
        if x0 <= x1 and y0 <= y1:
            part = fill["reach"][x0 - bx0:x1 - bx0 + 1, y0 - by0:y1 - by0 + 1]
            stats["occluded_pred_cells"] += int(part.size - np.count_nonzero(part))
    events = getattr(sim, "occ_events", None)
    if events is not None:
        events.append(dict(step=step_index, sphere=index, pred=list(fill["pred"]), box=list(fill["box"]), anchor_cell=list(cell), raw=list(raw),
                           allowed=bool(fill["allowed"]), oversize=bool(fill["oversize"]), anchor_blocked=bool(fill["anchor_blocked"]),
                           walked=bool(fill["walked"]), blocked_cells=fill["blocked_cells"], height_cm=fill["anchor_info"]["height_cm"]))


def occlusion_pass(sim, inputs):
    on = bool(getattr(sim, "input_occlusion", False))
    step_index = sim.steps + 1
    capture = step_index in getattr(sim, "reach_capture_steps", ())
    checking = bool(getattr(sim, "occ_box_check", False))
    if not (on or capture or checking):
        return None
    walk = on or capture
    fills = []
    packed = np.zeros((sim.nx, sim.ny, sim.nz), np.uint16) if capture else None
    for index, inp in enumerate(inputs):
        sim.occ_stats["sphere_steps"] += 1
        a_local, b_local = sim.to_local(inp.a), sim.to_local(inp.b)
        anchor = getattr(inp, "occlusion_anchor", None)
        if anchor is None:
            sim.occ_stats["no_anchor"] += 1
            pred_raw, _ = predicate_box(a_local, b_local, float(inp.radius))
            fill = dict(pred=clip_box(pred_raw, sim.nx, sim.ny), reach=None, allowed=True)
        else:
            fill = fill_reach(sim, anchor, a_local, b_local, float(inp.radius), walk)
            note_fill(sim, step_index, index, fill)
        fills.append(fill)
        if RUN["fill_sink"] is not None:
            RUN["fill_sink"](sim, step_index, index, fill)
        if capture:
            packed |= sphere_bits(fill, packed.shape).astype(np.uint16) << np.uint16(index)
    if capture:
        sim.reach_captures[step_index] = packed
    return fills if on else None


def reach_region(fills, index, si, sj, nz):
    if fills is None or fills[index]["reach"] is None:
        return None
    fill = fills[index]
    bx0, bx1, by0, by1 = fill["box"]
    region = np.zeros((si.stop - si.start, sj.stop - sj.start, nz), bool)
    x0, x1 = max(si.start, bx0), min(si.stop - 1, bx1)
    y0, y1 = max(sj.start, by0), min(sj.stop - 1, by1)
    if x0 <= x1 and y0 <= y1:
        region[x0 - si.start:x1 - si.start + 1, y0 - sj.start:y1 - sj.start + 1] = fill["reach"][x0 - bx0:x1 - bx0 + 1, y0 - by0:y1 - by0 + 1]
    return region


def influence_start(sim):
    if getattr(sim, "occ_region", None) is None:
        return None
    shape = (sim.nx, sim.ny, sim.nz)
    return dict(wind=np.zeros(shape, bool), erosion=np.zeros(shape, bool))


def mark_influence(record, key, si, sj, hit, reach, shape):
    if record is None:
        return
    hit = np.broadcast_to(hit, shape)
    if reach is not None:
        hit = hit & reach
    record[key][si, sj] |= hit


def influence_finish(sim, record, count):
    if record is None:
        return
    region = sim.occ_region(sim)
    wind = record["wind"] & region
    erosion = record["erosion"] & region
    sim.occ_rows.append(dict(step=sim.steps + 1, spheres=count, wind=int(wind.sum()), erosion=int(erosion.sum()), union=int((wind | erosion).sum()),
                             wind_all=int(record["wind"].sum()), erosion_all=int(record["erosion"].sum()),
                             union_all=int((record["wind"] | record["erosion"]).sum())))


def blend_into(sim, name, si, sj, target, k, reach, record):
    arr = getattr(sim, name)
    blk = arr[si, sj].astype(np.float64)
    new = (blk + (target - blk) * k).astype(F32)
    arr[si, sj] = new if reach is None else np.where(reach, new, arr[si, sj])
    mark_influence(record, "wind", si, sj, k > 0, reach, blk.shape)


def add_erase(erase, si, sj, term, reach, record):
    erase[si, sj] += term if reach is None else np.where(reach, term, 0.0)
    mark_influence(record, "erosion", si, sj, term > 0, reach, erase[si, sj].shape)


def formula_for(sim):
    chosen = sim.__dict__.get("blend_formula") or blend_patch.ACTIVE["formula"]
    if chosen is not None and chosen not in blend_patch.FACTORS:
        raise RuntimeError(f"섞기 식을 모름: {chosen}")
    return chosen


def occ_core_apply_inputs(self, inputs):
    p = self.p
    formula = formula_for(self)
    fills = occlusion_pass(self, inputs)
    record = influence_start(self)
    erase = np.zeros(self.D.shape)
    any_erase = False
    blends = 0
    for index, inp in enumerate(inputs):
        a = self.to_local(inp.a)
        b = self.to_local(inp.b)
        r = float(inp.radius)
        si, sj = self._bbox(a, b, r)
        if si.start >= si.stop or sj.start >= sj.stop:
            continue
        reach = reach_region(fills, index, si, sj, self.nz)
        gain = blend_patch.gain_of(inp, formula) if formula else None
        for comp, name in enumerate(("u", "v", "w")):
            X, Y, Z, R, _ = self.location(name)
            W = core.band_weight(X[si], Y[:, sj], Z, R, a, b, r) * float(inp.strength)
            if formula:
                k = blend_patch.FACTORS[formula](W, p.dt)
                target = gain * float(inp.velocity[comp])
            else:
                k = W
                target = float(inp.velocity[comp])
            blend_into(self, name, si, sj, target, k, reach, record)
            blends += 1
        if inp.erase_per_sec > 0:
            X, Y, Z, R, _ = self.location("c")
            amount = core.dwell_amount(X[si], Y[:, sj], Z, R, a, b, r, p.dt)
            add_erase(erase, si, sj, inp.erase_per_sec * amount, reach, record)
            any_erase = True
    blend_patch.apply_erase(self, erase, any_erase)
    influence_finish(self, record, len(inputs))
    if formula:
        blend_patch.count_call(formula, "core.Sim._apply_inputs", blends)


def occ_terrain_apply_inputs(self, inputs):
    p = self.p
    formula = formula_for(self)
    fills = occlusion_pass(self, inputs)
    record = influence_start(self)
    erase = np.zeros(self.D.shape)
    any_erase = False
    blends = 0
    for index, inp in enumerate(inputs):
        a, b, slope = self._input_frame(inp)
        r = float(inp.radius)
        si, sj = self._bbox(a, b, r)
        if si.start >= si.stop or sj.start >= sj.stop:
            continue
        reach = reach_region(fills, index, si, sj, self.nz)
        gain = blend_patch.gain_of(inp, formula) if formula else None
        for comp, name in enumerate(("u", "v", "w")):
            X, Y, _, R, _ = self.location(name)
            Z = self._sample_height(name, si, sj)
            W = core.band_weight(X[si], Y[:, sj], Z, R, a, b, r) * float(inp.strength)
            if formula:
                k = blend_patch.FACTORS[formula](W, p.dt)
                target = gain * self._wind_target(comp, inp.velocity, si, sj, slope)
            else:
                k = W
                target = self._wind_target(comp, inp.velocity, si, sj, slope)
            if comp == 2:
                self._record_top_target(target, k)
            blend_into(self, name, si, sj, target, k, reach, record)
            blends += 1
        if inp.erase_per_sec > 0:
            X, Y, _, R, _ = self.location("c")
            Z = self._sample_height("c", si, sj)
            amount = core.dwell_amount(X[si], Y[:, sj], Z, R, a, b, r, p.dt)
            add_erase(erase, si, sj, inp.erase_per_sec * amount, reach, record)
            any_erase = True
    blend_patch.apply_erase(self, erase, any_erase)
    influence_finish(self, record, len(inputs))
    if formula:
        blend_patch.count_call(formula, "TerrainSim._apply_inputs", blends)


def occ_r5_apply_inputs(self, inputs):
    p = self.p
    formula = formula_for(self)
    fills = occlusion_pass(self, inputs)
    record = influence_start(self)
    erase = np.zeros(self.D.shape)
    any_erase = False
    blends = 0
    for index, inp in enumerate(inputs):
        a, b, slope = self._input_frame(inp)
        r = float(inp.radius)
        si, sj = self._bbox(a, b, r)
        if si.start >= si.stop or sj.start >= sj.stop:
            continue
        reach = reach_region(fills, index, si, sj, self.nz)
        self._count_blend(si, sj, a, b, r)
        gain = blend_patch.gain_of(inp, formula) if formula else None
        for comp, name in enumerate(("u", "v", "w")):
            W = self.blended_weight(name, si, sj, a, b, r) * float(inp.strength)
            if formula:
                k = blend_patch.FACTORS[formula](W, p.dt)
                target = gain * self._wind_target(comp, inp.velocity, si, sj, slope)
            else:
                k = W
                target = self._wind_target(comp, inp.velocity, si, sj, slope)
            if comp == 2:
                self._record_top_target(target, k)
            blend_into(self, name, si, sj, target, k, reach, record)
            blends += 1
        if inp.erase_per_sec > 0:
            amount = self.blended_dwell(si, sj, a, b, r, p.dt)
            add_erase(erase, si, sj, inp.erase_per_sec * amount, reach, record)
            any_erase = True
    blend_patch.apply_erase(self, erase, any_erase)
    influence_finish(self, record, len(inputs))
    if formula:
        blend_patch.count_call(formula, "TerrainSimR5._apply_inputs", blends)


REPLACEMENTS = {core.Sim: occ_core_apply_inputs, terrain.TerrainSim: occ_terrain_apply_inputs, terrain_add2.TerrainSimR5: occ_r5_apply_inputs}


def occ_core_spheres(spheres):
    out = LOCKED_CORE_SPHERES(spheres)
    for sphere, raw in zip(out, spheres):
        if "g" in raw:
            sphere.wind_gain = float(raw["g"])
        if "occlusion_anchor_cm" in raw:
            sphere.occlusion_anchor = tuple(float(c) for c in raw["occlusion_anchor_cm"])
    return out


def occ_erase_only(inp):
    out = LOCKED_ERASE_ONLY(inp)
    for key in ("occlusion_anchor", "wind_gain"):
        if hasattr(inp, key):
            setattr(out, key, getattr(inp, key))
    return out


def stage1_tool():
    import fog3d_replay_v2 as stage1
    install()
    return stage1


def b4_tool():
    import fog3d_blend_b4_replay as b4
    install()
    return b4


def configure_sim(sim, replay):
    switch = RUN["occlusion"]
    sim.input_occlusion = bool(replay.get("input_occlusion", False)) if switch is None else bool(switch)
    if replay.get("wind_blend"):
        sim.blend_formula = replay["wind_blend"]
    capture = RUN["capture_reach"] and sim.input_occlusion
    sim.reach_capture_steps = frozenset(rs.dump_steps(replay).values()) if capture else frozenset()
    sim.reach_captures = {}
    sim.occ_box_check = bool(RUN["box_check"])
    sim.occ_region = RUN["region"]
    sim.occ_rows = []
    sim.occ_events = [] if RUN["box_check"] else None
    sim.occ_stats = new_stats()
    RUN["sim"] = sim
    return sim


def occ_make_sim(replay):
    if "ground" in replay:
        sim = rs.make_sim_s2a(replay)
    else:
        sim = stage1_tool().make_sim_v2(replay)
    return configure_sim(sim, replay)


def install():
    for cls, replacement in REPLACEMENTS.items():
        cls._apply_inputs = replacement
    rp.core_spheres = occ_core_spheres
    rp.make_sim = occ_make_sim
    core._erase_only = occ_erase_only


def uninstall():
    for cls, original in LOCKED_APPLY.items():
        cls._apply_inputs = original
    rp.core_spheres = LOCKED_CORE_SPHERES
    rp.make_sim = rs.make_sim_s2a
    core._erase_only = LOCKED_ERASE_ONLY


def is_installed():
    return all(cls.__dict__.get("_apply_inputs") is fn for cls, fn in REPLACEMENTS.items())


def strip_occ(replay):
    stripped = copy.deepcopy(replay)
    for key in ("input_occlusion", "occ_scene", "occ_anchor_rule", "wind_blend", "b4_scene"):
        stripped.pop(key, None)
    for step in stripped["steps"]:
        for sphere in step["spheres"]:
            sphere.pop("occlusion_anchor_cm", None)
            sphere.pop("g", None)
    return stripped


def check_occ_replay(replay, image, require_anchor=True):
    stripped = strip_occ(replay)
    problems = rp.check_replay(stripped)
    if "ground" in replay:
        problems += rs.check_ground(stripped, image)
    if not isinstance(replay.get("input_occlusion"), bool):
        problems.append("input_occlusion 은 bool")
    blend = replay.get("wind_blend")
    for index, step in enumerate(replay["steps"]):
        for sphere in step["spheres"]:
            anchor = sphere.get("occlusion_anchor_cm")
            if anchor is None:
                if require_anchor:
                    problems.append(f"steps[{index}] occlusion_anchor_cm 없음")
            elif len(anchor) != 3 or not all(isinstance(c, float) and math.isfinite(c) for c in anchor):
                problems.append(f"steps[{index}] occlusion_anchor_cm 형식")
            if blend and not (isinstance(sphere.get("g"), float) and math.isfinite(sphere["g"])):
                problems.append(f"steps[{index}] g")
        if len(problems) > 20:
            break
    return problems


def ground_at(replay, image, window, i, j):
    if image is None or "ground" not in replay:
        return np.float32(0.0)
    g = replay["ground"]
    return np.float32(image[window[1] + j - g["min_cell"][1], window[0] + i - g["min_cell"][0]])


def replay_anchor_cell(replay, image, step, anchor):
    n = int(replay["grid"]["n"])
    window = step["window_min"]
    ac = replay["anchor_cell"]
    raw_i = math.floor((float(anchor[0]) + (ac[0] - window[0]) * HX) / HX)
    raw_j = math.floor((float(anchor[1]) + (ac[1] - window[1]) * HX) / HX)
    i, j = min(max(raw_i, 0), n - 1), min(max(raw_j, 0), n - 1)
    ground = ground_at(replay, image, window, i, j)
    height = np.float32(np.float32(anchor[2]) - ground)
    raw_k = anchor_layer(height)
    return dict(cell=(i, j, min(max(raw_k, 0), LAYERS - 1)), raw=(raw_i, raw_j, raw_k), ground_cm=float(ground), height_cm=float(height),
                gap_cm=abs(float(height) - LAYER_CM * round(float(height) / LAYER_CM)))


def anchor_table(replay, image, rule, nudge):
    cells, gaps, clamped_ij, clamped_k, layers, count = set(), [], 0, 0, {}, 0
    first = last = None
    for step in replay["steps"]:
        for sphere in step["spheres"]:
            anchor = sphere.get("occlusion_anchor_cm")
            if anchor is None:
                continue
            info = replay_anchor_cell(replay, image, step, anchor)
            count += 1
            cells.add(info["cell"])
            gaps.append(info["gap_cm"])
            clamped_ij += int(info["raw"][0] != info["cell"][0] or info["raw"][1] != info["cell"][1])
            clamped_k += int(info["raw"][2] != info["cell"][2])
            layers[info["cell"][2]] = layers.get(info["cell"][2], 0) + 1
            first = first or dict(anchor_cm=list(anchor), cell=list(info["cell"]), height_cm=info["height_cm"])
            last = dict(anchor_cm=list(anchor), cell=list(info["cell"]), height_cm=info["height_cm"])
    min_gap = min(gaps) if gaps else None
    return dict(rule=rule, nudge_cm=nudge, anchored_sphere_steps=count, distinct_cells=len(cells), layers={str(k): v for k, v in sorted(layers.items())},
                clamped_ij=clamped_ij, clamped_k=clamped_k, min_gap_to_30_cm=min_gap, gap_check_pass=min_gap is None or min_gap > ANCHOR_CHECK_CM,
                first=first, last=last)


def shape_margin(replay, image):
    ac = replay["anchor_cell"]
    n = int(replay["grid"]["n"])
    zc = (np.arange(LAYERS) + 0.5) * LAYER_CM
    windows = sorted({tuple(s["window_min"]) for s in replay["steps"]} | {tuple(s["prev_window_min"]) for s in replay["steps"]})
    best, skipped = None, 0
    for obstacle in replay["obstacles"]:
        if float(obstacle.get("yaw_deg", 0.0)) % 90.0 != 0.0:
            skipped += 1
            continue
        c = obstacle["center_cm"]
        yaw = round(float(obstacle.get("yaw_deg", 0.0)) / 90.0) % 2
        h = obstacle["half_cm"]
        half = [core.inflate_half(h[1] if yaw else h[0], 0.0), core.inflate_half(h[0] if yaw else h[1], 0.0), core.inflate_half(h[2], 1.0)]
        for window in windows:
            rx = (window[0] - ac[0] + np.arange(n) + 0.5) * HX
            ry = (window[1] - ac[1] + np.arange(n) + 0.5) * HX
            if image is not None and "ground" in replay:
                g = replay["ground"]
                cols = image[window[1] - g["min_cell"][1]:window[1] - g["min_cell"][1] + n, window[0] - g["min_cell"][0]:window[0] - g["min_cell"][0] + n]
                heights = (cols.T.astype(np.float64)[:, :, None] + zc[None, None, :]).ravel()
            else:
                heights = zc
            for values, center, extent in ((rx, c[0], half[0]), (ry, c[1], half[1]), (heights, c[2], half[2])):
                for bound in (center - extent, center + extent):
                    gap = float(np.min(np.abs(values - bound)))
                    best = gap if best is None else min(best, gap)
    return dict(min_gap_cm=best, pass_=best is None or best > SHAPE_CHECK_CM, yawed_skipped=skipped)


def wall_spec(length, center_y=0.0):
    return dict(front_x_cm=WALL_FRONT_X, thickness_cm=WALL_THICKNESS, height_cm=WALL_HEIGHT, length_cm=length, center_y_cm=center_y)


def wall_obstacle(spec):
    half = (spec["thickness_cm"] / 2.0, spec["length_cm"] / 2.0, spec["height_cm"] / 2.0)
    return rp.box_dict((spec["front_x_cm"] + half[0], spec["center_y_cm"], half[2]), half)


def count_region(spec):
    far = spec["front_x_cm"] + spec["thickness_cm"] / 2.0 + core.inflate_half(spec["thickness_cm"] / 2.0, 0.0)
    return dict(kind="behind_wall", x_beyond_cm=far, y_center_cm=spec["center_y_cm"], y_half_cm=spec["length_cm"] / 2.0 - 2.0 * HX,
                z_below_cm=spec["height_cm"],
                rule="칸 중심 x > 부풀린 먼 면 x(벽 가운데 + 부풀린 반두께), |칸 중심 y − 벽 가운데 y| ≤ 벽 반길이 − 2칸, 칸 중심 높이(땅 + 층 가운데) < 벽 높이, 8층·창 안 모든 칸")


def region_mask(sim, region):
    rx, ry, _ = sim.center_rel()
    shape = (sim.nx, sim.ny, sim.nz)
    heights = sim.world_height("c") if hasattr(sim, "world_height") else sim.zc[None, None, :]
    mx = rx > region["x_beyond_cm"]
    my = np.abs(ry - region["y_center_cm"]) <= region["y_half_cm"]
    return mx[:, None, None] & my[None, :, None] & (np.broadcast_to(heights, shape) < region["z_below_cm"])


def region_function(region):
    return lambda sim: region_mask(sim, region)


def new_scene(name, obstacles):
    return rs.new_replay(name, window=WINDOW, obstacles=obstacles)


def add_anchored_step(r, spheres, anchor, dump=None):
    r.step(DT, spheres, dump=dump)
    for sphere in r.steps[-1]["spheres"]:
        sphere["occlusion_anchor_cm"] = [float(c) for c in anchor]


def blade_offsets():
    spacing = BLADE_LENGTH_CM / (BLADE_SPHERE_COUNT - 1)
    return [TIP_REACH_CM - spacing * m for m in range(BLADE_SPHERE_COUNT)]


def swing_plan():
    half_angle = math.asin(SWING_TIP_HALF_Y / TIP_REACH_CM)
    steps = int(math.ceil(TIP_REACH_CM * 2.0 * half_angle / (BLADE_SPEED * DT) - 1e-9))
    return half_angle, steps


def swing_spheres(pawn, k, half_angle, steps):
    t0 = -half_angle + 2.0 * half_angle * k / steps
    t1 = -half_angle + 2.0 * half_angle * (k + 1) / steps
    out = []
    for reach in blade_offsets():
        a = np.array((pawn[0] + reach * math.cos(t0), pawn[1] + reach * math.sin(t0), BLADE_HEIGHT))
        b = np.array((pawn[0] + reach * math.cos(t1), pawn[1] + reach * math.sin(t1), BLADE_HEIGHT))
        out.append(core.SweptSphere(tuple(a), tuple(b), BLADE_RADIUS, tuple((b - a) / DT), BLADE_WIND, BLADE_ERASE))
    return out


def swing_scene(name, pawn, wall, counted):
    r = new_scene(name, [wall_obstacle(wall)])
    half_angle, steps = swing_plan()
    anchor = (pawn[0], pawn[1], PELVIS_HEIGHT)
    for k in range(steps):
        add_anchored_step(r, swing_spheres(pawn, k, half_angle, steps), anchor, dump="swing_end" if k == steps - 1 else None)
    for k in range(TAIL_STEPS):
        r.step(DT, [], dump="end" if k == TAIL_STEPS - 1 else None)
    meta = dict(kind="blade_swing", wall=wall, pawn_cm=list(pawn), anchor_cm=list(anchor), anchor_source="골반(구를 낸 폰, 높이 = 골반 구 높이)",
                tip_reach_cm=TIP_REACH_CM, blade_length_cm=BLADE_LENGTH_CM, blade_offsets_cm=blade_offsets(), blade_height_cm=BLADE_HEIGHT,
                blade_radius_cm=BLADE_RADIUS, swing_tip_half_y_cm=SWING_TIP_HALF_Y, swing_half_angle_deg=math.degrees(half_angle), swing_steps=steps,
                tail_steps=TAIL_STEPS, tip_max_x_cm=pawn[0] + TIP_REACH_CM, wind=BLADE_WIND, erase_per_sec=BLADE_ERASE)
    if counted:
        meta["count_region"] = count_region(wall)
    return r, meta


def body_spheres(prev_y, cur_y, x, velocity_cap=None):
    out = []
    for forward, lateral, height, radius in SCENE["rig"]:
        a = np.array((x - lateral, prev_y + forward, height))
        b = np.array((x - lateral, cur_y + forward, height))
        velocity = (b - a) / DT
        speed = float(np.linalg.norm(velocity))
        if velocity_cap is not None and speed > velocity_cap:
            velocity = velocity * (velocity_cap / speed)
        out.append(core.SweptSphere(tuple(a), tuple(b), radius, tuple(velocity), BODY_WIND, BODY_ERASE))
    return out


def body_walk_scene():
    wall = wall_spec(WALL_LENGTH)
    x = WALL_FRONT_X - W4_PELVIS_GAP_CM
    step_cm = WALK_SPEED * DT
    count = int(math.floor(2.0 * W4_WALK_HALF_Y / step_cm + 1e-9))
    ys = [-W4_WALK_HALF_Y + step_cm * k for k in range(count + 1)]
    r = new_scene("occ_w4_body_parallel", [wall_obstacle(wall)])
    for k in range(1, count + 1):
        add_anchored_step(r, body_spheres(ys[k - 1], ys[k], x), (x, ys[k], PELVIS_HEIGHT), dump="walk_end" if k == count else None)
    for k in range(W4_STAND_STEPS):
        add_anchored_step(r, body_spheres(ys[-1], ys[-1], x), (x, ys[-1], PELVIS_HEIGHT), dump="end" if k == W4_STAND_STEPS - 1 else None)
    meta = dict(kind="body_walk_parallel", wall=wall, pelvis_x_cm=x, walk_y_cm=[ys[0], ys[-1]], walk_steps=count, stand_steps=W4_STAND_STEPS,
                rig=[list(item) for item in SCENE["rig"]], anchor_source="골반 구(반지름 60) 중심(걸음 끝 b)", wind=BODY_WIND, erase_per_sec=BODY_ERASE,
                count_region=count_region(wall))
    return r, meta


def layer7_scene():
    platform = rp.box_dict(PLATFORM_CENTER, PLATFORM_HALF)
    r = new_scene("occ_edge_layer7", [platform])
    spacing = BLADE_LENGTH_CM / (BLADE_SPHERE_COUNT - 1)
    reaches = [BLADE_LENGTH_CM - spacing * m for m in range(BLADE_SPHERE_COUNT)]
    hand = np.asarray(LAYER7_HAND, float)
    for k in range(LAYER7_STEPS):
        p0 = -math.radians(LAYER7_DOWN_DEG) * k / LAYER7_STEPS
        p1 = -math.radians(LAYER7_DOWN_DEG) * (k + 1) / LAYER7_STEPS
        spheres = []
        for reach in reaches:
            a = hand + reach * np.array((math.cos(p0), 0.0, math.sin(p0)))
            b = hand + reach * np.array((math.cos(p1), 0.0, math.sin(p1)))
            spheres.append(core.SweptSphere(tuple(a), tuple(b), BLADE_RADIUS, tuple((b - a) / DT), BLADE_WIND, BLADE_ERASE))
        add_anchored_step(r, spheres, LAYER7_PELVIS, dump="stroke_end" if k == LAYER7_STEPS - 1 else None)
    for k in range(LAYER7_TAIL):
        r.step(DT, [], dump="end" if k == LAYER7_TAIL - 1 else None)
    meta = dict(kind="platform_layer7", platform_center_cm=list(PLATFORM_CENTER), platform_half_cm=list(PLATFORM_HALF), pelvis_cm=list(LAYER7_PELVIS),
                hand_cm=list(LAYER7_HAND), down_deg=LAYER7_DOWN_DEG, stroke_steps=LAYER7_STEPS, tail_steps=LAYER7_TAIL,
                expect="닻 z − T = 341 → 층 11 → 7 로 자름, 닻 칸이 단(높이 250) 안이라 막힘 → 허용(가림 없음), 허용 수 = 구-걸음 수")
    return r, meta


def teleport_scene():
    wall = wall_spec(WALL_LENGTH)
    x = WALL_FRONT_X - W4_PELVIS_GAP_CM
    step_cm = WALK_SPEED * DT
    ys = [-W4_WALK_HALF_Y]
    for _ in range(TELEPORT_WALK_STEPS):
        ys.append(ys[-1] + step_cm)
    ys.append(ys[-1] + TELEPORT_CM)
    teleport_index = len(ys) - 1
    for _ in range(TELEPORT_WALK_STEPS):
        ys.append(ys[-1] + step_cm)
    r = new_scene("occ_edge_teleport", [wall_obstacle(wall)])
    for k in range(1, len(ys)):
        add_anchored_step(r, body_spheres(ys[k - 1], ys[k], x, SPEED_LIMIT), (x, ys[k], PELVIS_HEIGHT), dump="teleport" if k == teleport_index else None)
    for k in range(TELEPORT_STAND_STEPS):
        add_anchored_step(r, body_spheres(ys[-1], ys[-1], x), (x, ys[-1], PELVIS_HEIGHT), dump="end" if k == TELEPORT_STAND_STEPS - 1 else None)
    meta = dict(kind="teleport", wall=wall, pelvis_x_cm=x, teleport_step=teleport_index, teleport_from_to_y_cm=[ys[teleport_index - 1], ys[teleport_index]],
                teleport_cells=TELEPORT_CM / HX, speed_cap_cm_s=SPEED_LIMIT, walk_steps_each_side=TELEPORT_WALK_STEPS, stand_steps=TELEPORT_STAND_STEPS,
                expect="순간이동 걸음의 채우기 상자 y 폭 > 48칸 → 허용, 허용 수 > 0")
    return r, meta


OCC_BUILDERS = {
    "occ_w1_tip_through": lambda: swing_scene("occ_w1_tip_through", (PAWN_END_X, 0.0), wall_spec(WALL_LENGTH), True),
    "occ_w2_radius_reach": lambda: swing_scene("occ_w2_radius_reach", (WALL_FRONT_X - W2_TIP_GAP_CM - TIP_REACH_CM, 0.0), wall_spec(WALL_LENGTH), True),
    "occ_w4_body_parallel": body_walk_scene,
    "occ_w3_short_wall": lambda: swing_scene("occ_w3_short_wall", (PAWN_END_X, 0.0), wall_spec(SHORT_WALL_LENGTH), False),
    "occ_w5_end_row": lambda: swing_scene("occ_w5_end_row", (PAWN_END_X, W5_PAWN_Y), wall_spec(WALL_LENGTH), False),
    "occ_edge_layer7": layer7_scene,
    "occ_edge_teleport": teleport_scene,
}


def build_occ_scene(name):
    r, meta = OCC_BUILDERS[name]()
    replay = r.finish()
    replay["source"] = dict(tool="fog3d_occlusion_ref.py", code=rs.code_fingerprint(rs.R5_CODE), prereg_sha256=PREREG_SHA256)
    replay["input_occlusion"] = True
    meta["label"] = OCC_SCENE_LABELS[name]
    replay["occ_scene"] = meta
    image = rs.attach_ground(replay, "F0", "R5")
    problems = check_occ_replay(replay, image)
    if problems:
        raise ValueError(f"{name}: {problems}")
    return replay, image


def anchor_rule_of(replay, name):
    if name in PATH_SCENES:
        return "③ 경로 구: 가장 가까운 창 가장자리 밖 20cm, z − T = 105"
    if any(abs(float(s["r_cm"]) - PELVIS_RADIUS) < 1e-9 for step in replay["steps"] for s in step["spheres"]):
        return "① 걷는 캐릭터: 반지름 60 구 중심(그 걸음 b), 골반 구 없는 걸음은 마지막 골반 닻 유지"
    return "② 칼만 움직임: 첫 걸음 구 중심(a 평균)에서 움직임 반대쪽 60cm, z − T = 105"


def column_anchor(replay, image, step, xy):
    info = replay_anchor_cell(replay, image, step, (xy[0], xy[1], 0.0))
    return [float(xy[0]), float(xy[1]), float(np.float32(info["ground_cm"]) + np.float32(ANCHOR_HEIGHT_ABOVE_GROUND))]


def blade_anchor_xy(replay):
    for step in replay["steps"]:
        if not step["spheres"]:
            continue
        starts = np.array([s["a_cm"] for s in step["spheres"]], float)
        moves = np.array([np.subtract(s["b_cm"], s["a_cm"]) for s in step["spheres"]], float)
        direction = moves.mean(axis=0)[:2]
        if float(np.linalg.norm(direction)) < 1e-9:
            direction = np.array([s["v_cm_s"] for s in step["spheres"]], float).mean(axis=0)[:2]
        if float(np.linalg.norm(direction)) < 1e-9:
            direction = np.array((1.0, 0.0))
        direction = direction / np.linalg.norm(direction)
        center = starts.mean(axis=0)[:2]
        return center - BLADE_ANCHOR_BACK_CM * direction
    return None


def edge_anchor_xy(replay, step, sphere):
    n = int(replay["grid"]["n"])
    ac = replay["anchor_cell"]
    lo_x = (step["window_min"][0] - ac[0]) * HX
    lo_y = (step["window_min"][1] - ac[1]) * HX
    hi_x, hi_y = lo_x + n * HX, lo_y + n * HX
    cx, cy = float(sphere["b_cm"][0]), float(sphere["b_cm"][1])
    gaps = (cx - lo_x, hi_x - cx, cy - lo_y, hi_y - cy)
    side = int(np.argmin(gaps))
    return ((lo_x - EDGE_ANCHOR_OUT_CM, cy), (hi_x + EDGE_ANCHOR_OUT_CM, cy), (cx, lo_y - EDGE_ANCHOR_OUT_CM), (cx, hi_y + EDGE_ANCHOR_OUT_CM))[side]


def attach_anchors(replay, image, name):
    rule = anchor_rule_of(replay, name)
    blade_xy = blade_anchor_xy(replay)
    carry = None
    for step in replay["steps"]:
        if not step["spheres"]:
            continue
        if rule.startswith("③"):
            anchors = [column_anchor(replay, image, step, edge_anchor_xy(replay, step, s)) for s in step["spheres"]]
        elif rule.startswith("①"):
            pelvis = [s for s in step["spheres"] if abs(float(s["r_cm"]) - PELVIS_RADIUS) < 1e-9]
            if pelvis:
                carry = [float(c) for c in pelvis[0]["b_cm"]]
            anchor = carry if carry is not None else column_anchor(replay, image, step, blade_xy)
            anchors = [list(anchor)] * len(step["spheres"])
        else:
            anchors = [column_anchor(replay, image, step, blade_xy)] * len(step["spheres"])
        for sphere, anchor in zip(step["spheres"], anchors):
            sphere["occlusion_anchor_cm"] = [float(c) for c in anchor]
    nudge = 0.0
    if not anchor_table(replay, image, rule, nudge)["gap_check_pass"]:
        nudge = ANCHOR_NUDGE_CM
        for step in replay["steps"]:
            for sphere in step["spheres"]:
                sphere["occlusion_anchor_cm"][2] = float(sphere["occlusion_anchor_cm"][2] + nudge)
    table = anchor_table(replay, image, rule, nudge)
    replay["input_occlusion"] = True
    replay["occ_anchor_rule"] = dict(rule=rule, nudge_cm=nudge, prereg="B2-2 닻 붙이는 규칙")
    return table


def base_scene(flavor, name):
    if flavor == "r5":
        replay, image = rs.build_scene(name, "R5")
        return replay, image
    if flavor == "b4":
        replay, image, _ = b4_tool().scene_for(name, "b4")
        return replay, image
    if flavor == "stage1":
        return read_json(os.path.join(STAGE1_REPLAYS, f"{name}.json")), None
    raise ValueError(flavor)


def build_anchored(flavor, name):
    replay, image = base_scene(flavor, name)
    base_source = replay.get("source", {})
    table = attach_anchors(replay, image, name)
    replay["source"] = dict(tool="fog3d_occlusion_ref.py", code=rs.code_fingerprint(rs.R5_CODE), base_tool=base_source.get("tool"), flavor=flavor,
                            prereg_sha256=PREREG_SHA256)
    problems = check_occ_replay(replay, image)
    if problems:
        raise ValueError(f"{flavor}/{name}: {problems}")
    return replay, image, table


def write_scene_replay(folder, replay, image):
    os.makedirs(folder, exist_ok=True)
    if image is not None:
        rs.write_replay(folder, replay, image)
    else:
        with open(os.path.join(folder, f"{replay['name']}.json"), "w", encoding="utf-8", newline="\n") as fh:
            fh.write(rp.dumps_text(replay))
    refresh_folder_manifest(folder)


def refresh_folder_manifest(folder):
    files = []
    for name in folder_scenes(folder):
        replay = read_json(os.path.join(folder, f"{name}.json"))
        files.append(dict(name=f"{name}.json", scene=name, input_occlusion=replay.get("input_occlusion")))
        if "ground" in replay:
            files.append(dict(name=replay["ground"]["file"]))
    write_json(os.path.join(folder, "manifest.json"), dict(files=files))


def folder_scenes(folder):
    if not os.path.isdir(folder):
        return []
    return sorted(f[:-5] for f in os.listdir(folder) if f.endswith(".json") and f != "manifest.json")


def load_scene(folder, name):
    replay = read_json(os.path.join(folder, f"{name}.json"))
    image = None
    if "ground" in replay:
        g = replay["ground"]
        image = np.fromfile(os.path.join(folder, g["file"]), "<f4").reshape(g["size"][1], g["size"][0])
        if sha256_bytes(rs.image_bytes(image)) != g["sha256"]:
            raise ValueError(f"{name}: 땅 그림 sha256 다름")
    return replay, image


def geometry_sim(replay, image):
    RUN.update(occlusion=False, capture_reach=False, box_check=False, region=None)
    rs.ACTIVE.update(image=image, analytic=None, sim=None)
    return occ_make_sim(replay)


def run_reference(replay, image, out_dir, occlusion, shake=False, region=None, box_check=True, fill_sink=None):
    RUN.update(occlusion=occlusion, capture_reach=out_dir is not None, box_check=box_check, region=region, fill_sink=fill_sink)
    started = time.time()
    if out_dir is None:
        rs.ACTIVE.update(image=image, analytic=None, sim=None)
        result = rp.execute(replay, None, shake=shake)
        files = []
    elif image is not None:
        result = rs.run_replay(replay, image, out_dir, shake=shake)
        files = list(result["files"])
    else:
        result = rp.execute(replay, out_dir, shake=shake)
        files = [os.path.basename(path) for path in result["dumps"]]
    sim = RUN["sim"]
    for dump, index in rs.dump_steps(replay).items():
        packed = sim.reach_captures.get(index)
        if packed is None or out_dir is None:
            continue
        stem = f"{replay['name']}__{dump}.reach.bin"
        with open(os.path.join(out_dir, stem), "wb") as fh:
            fh.write(np.ascontiguousarray(packed, "<u2").tobytes())
        files.append(stem)
    events = sim.occ_events or []
    return dict(name=replay["name"], occlusion=bool(sim.input_occlusion), formula=formula_for(sim), steps=result["steps"], hash=result["hash"],
                files=files, occ_stats=dict(sim.occ_stats), rows=list(sim.occ_rows),
                fill_boxes_with_blocked=sum(1 for e in events if e["blocked_cells"] > 0), seconds=round(time.time() - started, 1))


def same_npz_content(mine, theirs):
    with np.load(mine) as za, np.load(theirs) as zb:
        if sorted(za.files) != sorted(zb.files):
            return False
        return all(za[k].dtype == zb[k].dtype and za[k].shape == zb[k].shape and za[k].tobytes() == zb[k].tobytes() for k in za.files)


def compare_files(files, out_dir, other_dir):
    rows = {}
    for stem in files:
        if stem.endswith(".reach.bin"):
            continue
        mine, theirs = os.path.join(out_dir, stem), os.path.join(other_dir, stem)
        if not os.path.exists(theirs):
            rows[stem] = "missing_base"
        elif sha256_file(mine) == sha256_file(theirs):
            rows[stem] = "same"
        elif stem.endswith(".npz") and same_npz_content(mine, theirs):
            rows[stem] = "same_content"
        else:
            rows[stem] = "different"
    return rows


def compare_base(flavor, files, out_dir):
    base = BASE_REF[flavor]
    rows = compare_files(files, out_dir, base)
    return dict(base=base, files=rows, all_same=bool(rows) and all(v in ("same", "same_content") for v in rows.values()),
                all_bytes_same=bool(rows) and all(v == "same" for v in rows.values()))


def flavor_tool(flavor):
    if flavor == "b4":
        b4_tool()
    elif flavor == "stage1":
        stage1_tool()
    install()


def scene_anchor_table(replay, image):
    rule = replay.get("occ_anchor_rule")
    if rule is None:
        return anchor_table(replay, image, SCENE_ANCHOR_RULE, 0.0)
    return anchor_table(replay, image, rule["rule"], rule["nudge_cm"])


def total_steps(replay):
    return sum(int(step.get("repeat", 1)) for step in replay["steps"])


def recorded_row(name, folder, recorded):
    row = recorded.get(name)
    if row is None or os.path.normcase(os.path.normpath(os.path.join(ROOT, row["path"]))) != os.path.normcase(os.path.normpath(os.path.join(folder, f"{name}.json"))):
        return None
    return row


def verify_recorded(name, row):
    problems = [] if sha256_file(os.path.join(ROOT, row["path"])) == row["sha256"] else [f"{name}: 재생 json sha256 이 기록과 다름"]
    if "ground_path" in row and sha256_file(os.path.join(ROOT, row["ground_path"])) != row["ground_sha256"]:
        problems.append(f"{name}: 땅 그림 sha256 이 기록과 다름")
    return problems


def prepare_anchored(pairs):
    recorded = read_json(SCENE_HASHES)["scenes"]
    prepared, problems = {}, []
    for flavor, name in pairs:
        folder = REPLAY_FOLDERS[flavor]
        row = recorded_row(name, folder, recorded)
        if row is not None:
            problems += verify_recorded(name, row)
        elif flavor == "scenes":
            problems.append(f"{name}: 장면 해시 기록 없음")
            continue
        else:
            replay, image, _ = build_anchored(flavor, name)
            write_scene_replay(folder, replay, image)
        replay, image = load_scene(folder, name)
        prepared[(flavor, name)] = dict(anchor_table=scene_anchor_table(replay, image), shape_check=shape_margin(replay, image), steps=total_steps(replay),
                                        replay_sha256=sha256_file(os.path.join(folder, f"{name}.json")), hash_recorded=row is not None)
    if problems:
        raise SystemExit(f"장면 해시 문제: {problems}")
    return prepared


def anchored_job(job):
    flavor, name, occlusion, shake, compare = job
    flavor_tool(flavor)
    folder = REPLAY_FOLDERS[flavor]
    replay, image = load_scene(folder, name)
    out_dir = os.path.join(REF_OCC, flavor, "on" if occlusion else "off")
    target = os.path.join(out_dir, rs.SHAKE_FOLDER) if shake else out_dir
    os.makedirs(target, exist_ok=True)
    info = run_reference(replay, image, target, occlusion, shake=shake)
    info.update(flavor=flavor, shake=bool(shake), out_dir=os.path.relpath(target, ROOT).replace("\\", "/"),
                replay_sha256=sha256_file(os.path.join(folder, f"{name}.json")))
    if compare and not occlusion and not shake and flavor in BASE_REF:
        info["base_compare"] = compare_base(flavor, info["files"], target)
    return info


def run_jobs(function, jobs, workers):
    infos, failures, started = [], [], time.time()

    def note(job, info=None, error=None):
        if error is not None:
            failures.append(dict(job=list(job), error=error))
        else:
            infos.append(info)
        print(json.dumps(dict(job=list(job), ok=error is None, error=error, seconds=None if info is None else info.get("seconds"),
                              elapsed=round(time.time() - started, 1), finished=len(infos) + len(failures), total=len(jobs)), ensure_ascii=False), flush=True)

    if workers <= 1:
        for job in jobs:
            try:
                note(job, info=function(job))
            except Exception as exc:
                note(job, error=f"{type(exc).__name__}: {exc}")
        return infos, failures
    with concurrent.futures.ProcessPoolExecutor(max_workers=min(workers, rs.MAX_JOBS)) as pool:
        futures = {pool.submit(function, job): job for job in jobs}
        for future in concurrent.futures.as_completed(futures):
            try:
                note(futures[future], info=future.result())
            except Exception as exc:
                note(futures[future], error=f"{type(exc).__name__}: {exc}")
    return infos, failures


def write_manifests(infos, prepared):
    by_dir = {}
    for info in infos:
        info["anchor_table"] = prepared[(info["flavor"], info["name"])]["anchor_table"]
        by_dir.setdefault(os.path.join(ROOT, info["out_dir"]), []).append(info)
    for out_dir, items in sorted(by_dir.items()):
        update_ref_manifest(out_dir, items)


def update_ref_manifest(out_dir, infos):
    path = os.path.join(out_dir, "manifest.json")
    body = read_json(path) if os.path.exists(path) else dict(scenes={})
    body.update(format="fog3d-occ-ref-1", tool="fog3d_occlusion_ref.py", tool_sha256=sha256_file(TOOL_PATH), prereg_sha256=PREREG_SHA256,
                reach_layout="uint16 LE, [i=x][j=y][k=z] (GPU 칸 순서 (x*N+y)*8+z), 비트 s = 그 걸음 재생 구 s")
    for info in infos:
        body["scenes"][info["name"]] = {k: v for k, v in info.items() if k not in ("rows",)}
        body["scenes"][info["name"]]["file_sha256"] = {f: sha256_file(os.path.join(out_dir, f)) for f in info["files"] if os.path.exists(os.path.join(out_dir, f))}
    write_json(path, body)


def b4_flat_names():
    names = [f"b4_walk_stand_h{hz}_g{int(round(g * 100)):03d}" for hz, g in ((60, 1.0), (60, 1.25), (30, 1.0), (30, 1.25), (144, 1.0))]
    for label in ("blade", "turn_stand"):
        names += [f"b4_{label}_h{hz}_g{int(round(g * 100)):03d}" for hz, g in ((60, 1.0), (60, 1.25), (30, 1.0), (30, 1.25))]
    return names


def registered_groups():
    on = set(b4_flat_names()) | {B4_SLOPE} | set(ON_EQ_OFF_S2A)
    changed = set(W_NAMES) | set(CHANGED_S2A) | set(STAGE1_CHANGED) | set(EDGE_CHANGED)
    return dict(on_equals_off=on, changed=changed)


def scene_settings(folder, name):
    path = os.path.join(folder, f"{name}.json")
    if not os.path.exists(path):
        return dict(file="없음(다음 단계 생성)")
    replay = read_json(path)
    spheres = [s for step in replay["steps"] for s in step["spheres"]]
    kinds = sorted({o.get("kind", "box") for o in replay["obstacles"]})
    return dict(file=os.path.relpath(path, ROOT).replace("\\", "/"), input_occlusion=replay.get("input_occlusion", False),
                steps_per_second=sorted({round(1.0 / s["dt"], 3) for s in replay["steps"]}), shape_kinds=kinds or ["없음"],
                terrain=replay.get("ground", {}).get("terrain", "없음(1단계 평지)"), wind_blend=replay.get("wind_blend", "B0(지금 식)"),
                anchors="모두" if spheres and all("occlusion_anchor_cm" in s for s in spheres) else ("없음" if not any("occlusion_anchor_cm" in s for s in spheres) else "일부"))


def gate_table():
    groups = registered_groups()
    scenes_folder = REPLAY_FOLDERS["scenes"]
    rows = [dict(gate="B2-2 앞조건(가림 끔에서 세는 영역 입력 칸 > 0)", run_occlusion="끔(재생 값 무시)", scenes={n: scene_settings(scenes_folder, n) for n in PRECHECK_SCENES}),
            dict(gate="B2-2 주 판정(가림 켬: 세는 영역 0, 과다 가림 없음)", run_occlusion="켬", scenes={n: scene_settings(scenes_folder, n) for n in PRECHECK_SCENES}),
            dict(gate="B2-2 정보(이어짐 집합 = 손 식 집합)", run_occlusion="켬", scenes={n: scene_settings(scenes_folder, n) for n in HAND_SCENES}),
            dict(gate="B2-2 가장자리(정보, 허용 수 기록)", run_occlusion="켬",
                 scenes=dict({EDGE_OUTSIDE: scene_settings(REPLAY_FOLDERS["r5"], EDGE_OUTSIDE)}, **{n: scene_settings(scenes_folder, n) for n in EDGE_CHANGED})),
            dict(gate="B2-2 켬 = 끔 묶음(기준 켬 = 끔 바이트, 앞조건: 채우기 상자 막힌 칸 0)", run_occlusion="켬·끔 둘 다",
                 scenes={n: dict(r5=scene_settings(REPLAY_FOLDERS["r5"], n), b4=scene_settings(REPLAY_FOLDERS["b4"], n)) for n in sorted(groups["on_equals_off"])}),
            dict(gate="B2-3 바뀜 묶음(GPU 대 기준)", run_occlusion="켬",
                 scenes={n: dict(scenes=scene_settings(scenes_folder, n), r5=scene_settings(REPLAY_FOLDERS["r5"], n), b4=scene_settings(REPLAY_FOLDERS["b4"], n),
                                 stage1=scene_settings(REPLAY_FOLDERS["stage1"], n)) for n in sorted(groups["changed"])})]
    return dict(prereg_sha256=PREREG_SHA256, tool_sha256=sha256_file(TOOL_PATH), groups={k: sorted(v) for k, v in groups.items()}, gates=rows,
                note="occ_edge_layer7·occ_edge_teleport 는 등록 B2-2 묶음 목록에 이름이 없어 바뀜 묶음에 넣었다(해석, 감독 확인 필요)")


def check_gates(require_complete):
    groups = registered_groups()
    folders = {"replays_s2a/R5": os.path.join(HERE, "replays_s2a", "R5"), "replays_blend_b4": os.path.join(HERE, "replays_blend_b4")}
    folders.update({f"replays_occ/{k}": v for k, v in REPLAY_FOLDERS.items()})
    failures, rows = [], {}
    overlap = sorted(groups["on_equals_off"] & groups["changed"])
    if overlap:
        failures.append(f"두 묶음이 겹침: {overlap}")
    for label, folder in folders.items():
        names = folder_scenes(folder)
        if not names and not label.startswith("replays_occ"):
            failures.append(f"{label}: 장면이 없음")
        none = [n for n in names if n not in groups["on_equals_off"] and n not in groups["changed"]]
        both = [n for n in names if n in groups["on_equals_off"] and n in groups["changed"]]
        failures += [f"{label}: 어느 묶음에도 없음 {n}" for n in none] + [f"{label}: 두 묶음에 다 있음 {n}" for n in both]
        if label == "replays_occ/stage1":
            failures += [f"{label}: 1단계는 물체 다섯 장면의 닻 판만 {n}" for n in names if n not in STAGE1_CHANGED]
        if label.startswith("replays_occ"):
            for n in names:
                replay = read_json(os.path.join(folder, f"{n}.json"))
                spheres = [s for step in replay["steps"] for s in step["spheres"]]
                if replay.get("input_occlusion") is not True or not all("occlusion_anchor_cm" in s for s in spheres):
                    failures.append(f"{label}: {n} 가림 켬·닻 없음")
        rows[label] = dict(count=len(names), on_equals_off=sum(1 for n in names if n in groups["on_equals_off"]),
                           changed=sum(1 for n in names if n in groups["changed"]), none=none, both=both)
    b4_names = folder_scenes(folders["replays_blend_b4"])
    for prefix, count in B4_FLAT_COUNTS.items():
        found = [n for n in b4_names if n.startswith(prefix)]
        if len(found) != count or not set(found) <= groups["on_equals_off"]:
            failures.append(f"B4 평지 {prefix}* 가 {count} 개가 아님: {found}")
    present = {n for k in REPLAY_FOLDERS for n in folder_scenes(REPLAY_FOLDERS[k])}
    missing = sorted((groups["on_equals_off"] | groups["changed"]) - present)
    if require_complete and missing:
        failures.append(f"닻 판·새 장면 없음 {len(missing)}: {missing}")
    return dict(ok=not failures, failures=failures, folders=rows, not_generated=missing, require_complete=require_complete)


def hand_reach(fluid, wall, anchor, box, cells):
    wx0, wx1, wy0, wy1 = wall
    ai, aj, _ = anchor
    bx0, bx1, by0, by1 = box
    ci, cj, ck = cells
    plus = np.maximum(0, 2 * (wy1 + 1 - np.maximum(aj, cj))) if by0 <= wy1 + 1 <= by1 else np.full(ci.shape, HAND_UNREACHABLE)
    minus = np.maximum(0, 2 * (np.minimum(aj, cj) - (wy0 - 1))) if by0 <= wy0 - 1 <= by1 else np.full(ci.shape, HAND_UNREACHABLE)
    crosses = not (wy1 < by0 or wy0 > by1)
    behind = (((ai < wx0) & (ci > wx1)) | ((ai > wx1) & (ci < wx0))) & crosses
    excess = np.where(behind, np.minimum(plus, minus), 0)
    return fluid[ci, cj, ck] & (excess <= REACH_SLACK), excess


def wall_cells(fluid):
    solid = ~fluid
    xs = np.nonzero(solid.any(axis=(1, 2)))[0]
    ys = np.nonzero(solid.any(axis=(0, 2)))[0]
    return int(xs.min()), int(xs.max()), int(ys.min()), int(ys.max())


def pred_cells(pred, nz):
    x0, x1, y0, y1 = pred
    gi, gj, gk = np.meshgrid(np.arange(x0, x1 + 1), np.arange(y0, y1 + 1), np.arange(nz), indexing="ij")
    return gi.ravel(), gj.ravel(), gk.ravel()


def hand_sets(name):
    replay, image = load_scene(REPLAY_FOLDERS["scenes"], name)
    sim = geometry_sim(replay, image)
    wall = wall_cells(sim.fluid)
    rows = []
    for index, step in enumerate(replay["steps"]):
        for s, sphere in enumerate(step["spheres"]):
            pred_raw, _ = predicate_box(sim.to_local(sphere["a_cm"]), sim.to_local(sphere["b_cm"]), float(sphere["r_cm"]))
            (ai, aj, ak), info = anchor_cell(sim, sphere["occlusion_anchor_cm"])
            box = clip_box((min(pred_raw[0], ai), max(pred_raw[1], ai), min(pred_raw[2], aj), max(pred_raw[3], aj)), sim.nx, sim.ny)
            pred = clip_box(pred_raw, sim.nx, sim.ny)
            cells = pred_cells(pred, sim.nz)
            oversize = (box[1] - box[0] + 1) > FILL_LIMIT_CELLS or (box[3] - box[2] + 1) > FILL_LIMIT_CELLS
            allowed = oversize or not bool(sim.fluid[ai, aj, ak])
            connected = np.ones(cells[0].shape, bool) if allowed else hand_reach(sim.fluid, wall, (ai, aj, ak), box, cells)[0]
            rows.append(dict(step=index + 1, sphere=s, pred=list(pred), box=list(box), anchor_cell=[ai, aj, ak], allowed=allowed, cells=int(connected.size),
                             connected=int(connected.sum()), not_connected=int(connected.size - connected.sum()),
                             bits_hex=np.packbits(connected.astype(np.uint8)).tobytes().hex(), bits_sha256=sha256_bytes(np.packbits(connected.astype(np.uint8)).tobytes())))
    return dict(scene=name, wall_cells=dict(x=[wall[0], wall[1]], y=[wall[2], wall[3]]), window=list(sim.origin), rows=rows,
                layout="술어 상자(창으로 자름) 안 칸을 [i][j][k] 순서로 펴서 np.packbits(큰 비트 먼저)", total_cells=sum(r["cells"] for r in rows),
                total_connected=sum(r["connected"] for r in rows), set_sha256=sha256_bytes("".join(r["bits_sha256"] for r in rows).encode()))


def unit_params(n):
    fake = dict(grid=dict(n=n, layers=LAYERS, cell_cm=HX, layer_cm=LAYER_CM), tuning=dict(rs.TUNING_S2A), mode=2)
    p = rp.build_params(fake, DT)
    p.advect = "N2"
    return p


def unit_sim(kind):
    p = unit_params(32)
    if kind == "r5":
        return terrain_add2.TerrainSimR5(p, terrain.TERRAINS["F0"], anchor=(0, 0), window_min=(-16, -16)), terrain_add2.TerrainSimR5
    return core2.Sim(p, anchor=(0, 0), window_min=(-16, -16)), core.Sim


def unit_inputs(k):
    out = [core.SweptSphere((-80.0 + 10.0 * k, -20.0, 30.0), (-70.0 + 10.0 * k, -20.0, 30.0), 45.0, (600.0, 0.0, 0.0), 3.0, 0.0),
           core.SweptSphere((-80.0 + 10.0 * k, 0.0, 91.0), (-70.0 + 10.0 * k, 0.0, 91.0), 60.0, (600.0, 0.0, 0.0), 3.0, 0.0),
           core.SweptSphere((40.0, -60.0 + 15.0 * k, 100.0), (40.0, -45.0 + 15.0 * k, 100.0), 35.0, (0.0, 900.0, 0.0), 2.5, 30.0)]
    for sphere in out:
        sphere.occlusion_anchor = (-70.0 + 10.0 * k, 0.0, 91.0)
    return out


def unit_run(kind, method, occlusion, formula, steps=6):
    sim, cls = unit_sim(kind)
    if method == "locked":
        fn = LOCKED_APPLY[cls] if formula is None else BLENDED_APPLY[cls]
    else:
        fn = REPLACEMENTS[cls]
    sim._apply_inputs = types.MethodType(fn, sim)
    if formula:
        sim.blend_formula = formula
    sim.input_occlusion = occlusion
    sim.reach_capture_steps = frozenset(range(1, steps + 1)) if method == "occ" else frozenset()
    sim.reach_captures = {}
    sim.occ_box_check = method == "occ"
    sim.occ_region = None
    sim.occ_rows = []
    sim.occ_events = []
    sim.occ_stats = new_stats()
    for k in range(steps):
        sim.step(unit_inputs(k))
    state = {name: np.ascontiguousarray(getattr(sim, name)).tobytes() for name in ("u", "v", "w", "dpx", "dpy", "dpz", "g", "D", "pr")}
    return sim, state


def unit_identical():
    out = {}
    ok = True
    for kind in ("core", "r5"):
        for formula in (None, "B4"):
            _, base = unit_run(kind, "locked", False, formula)
            sim_off, off = unit_run(kind, "occ", False, formula)
            sim_on, on = unit_run(kind, "occ", True, formula)
            bits_ok = True
            walk_ok = True
            for event in sim_on.occ_events:
                bx0, bx1, by0, by1 = event["box"]
                start = (event["anchor_cell"][0] - bx0, event["anchor_cell"][1] - by0, event["anchor_cell"][2])
                shape = (bx1 - bx0 + 1, by1 - by0 + 1, sim_on.nz)
                forced = walk_distance(sim_on.ou[bx0:bx1, by0:by1 + 1, :], sim_on.ov[bx0:bx1 + 1, by0:by1, :], sim_on.ow[bx0:bx1 + 1, by0:by1 + 1, :sim_on.nz - 1],
                                       shape, start)
                walk_ok &= bool(np.array_equal(forced, manhattan(shape, start)))
                packed = sim_on.reach_captures[event["step"]]
                x0, x1, y0, y1 = event["pred"]
                part = (packed[x0:x1 + 1, y0:y1 + 1, :] >> np.uint16(event["sphere"])) & np.uint16(1)
                bx0, bx1, by0, by1 = event["box"]
                whole = (packed[bx0:bx1 + 1, by0:by1 + 1, :] >> np.uint16(event["sphere"])) & np.uint16(1)
                bits_ok &= bool(part.all()) and bool(whole.all())
            row = dict(off_same=all(off[k] == base[k] for k in base), on_same=all(on[k] == base[k] for k in base), bits_all_one=bits_ok,
                       forced_walk_equals_manhattan=walk_ok, events=len(sim_on.occ_events), walked=sim_on.occ_stats["walked"],
                       allowed=sim_on.occ_stats["allowed"], boxes_with_blocked=sim_on.occ_stats["boxes_with_blocked"])
            row["pass"] = (row["off_same"] and row["on_same"] and row["bits_all_one"] and walk_ok and row["events"] > 0 and row["allowed"] == 0
                           and row["boxes_with_blocked"] == 0)
            ok &= row["pass"]
            out[f"{kind}|{formula or 'B0(지금 식)'}"] = row
    return ok, out


def synthetic_sim(n, wall):
    fluid = np.ones((n, n, LAYERS), bool)
    wx0, wx1, wy0, wy1 = wall
    fluid[wx0:wx1 + 1, wy0:wy1 + 1, :] = False
    ou = np.zeros_like(fluid)
    ou[:-1] = fluid[:-1] & fluid[1:]
    ov = np.zeros_like(fluid)
    ov[:, :-1] = fluid[:, :-1] & fluid[:, 1:]
    ow = fluid.copy()
    ow[:, :, :-1] = fluid[:, :, :-1] & fluid[:, :, 1:]
    return types.SimpleNamespace(nx=n, ny=n, nz=LAYERS, fluid=fluid, ou=ou, ov=ov, ow=ow, p=types.SimpleNamespace(hx=HX, hy=HX), offset_cm=lambda: (0.0, 0.0))


def unit_hand_cases():
    rng = np.random.default_rng(20261117)
    cases = [dict(wall=(30, 30, 18, 21), anchor=(26.5, 19.2), a=(31.5, 18.0), b=(32.5, 21.0), r=35.0),
             dict(wall=(30, 30, 10, 29), anchor=(26.5, 29.4), a=(31.5, 28.0), b=(32.5, 31.0), r=35.0),
             dict(wall=(30, 30, 10, 29), anchor=(26.5, 20.4), a=(31.0, 19.0), b=(31.5, 22.0), r=35.0),
             dict(wall=(30, 31, 12, 27), anchor=(25.5, 26.4), a=(33.0, 25.0), b=(33.5, 28.0), r=60.0)]
    for _ in range(300):
        wx0 = int(rng.integers(18, 36))
        wy0 = int(rng.integers(6, 40))
        wall = (wx0, wx0 + int(rng.integers(0, 2)), wy0, wy0 + int(rng.integers(0, 14)))
        anchor = (float(rng.uniform(wx0 - 9, wx0 - 0.5)), float(rng.uniform(wall[2] - 4, wall[3] + 5)))
        a = (float(rng.uniform(wall[1] + 0.5, wall[1] + 6)), float(rng.uniform(wall[2] - 3, wall[3] + 3)))
        b = (a[0] + float(rng.uniform(-1.5, 1.5)), a[1] + float(rng.uniform(-2.0, 2.0)))
        cases.append(dict(wall=wall, anchor=anchor, a=a, b=b, r=float(rng.choice([35.0, 45.0, 60.0]))))
    return cases


def unit_hand_vs_walk():
    n = 64
    tested = walked = mismatched = excess_mismatch = 0
    examples = []
    for case in unit_hand_cases():
        sim = synthetic_sim(n, case["wall"])
        z = PELVIS_HEIGHT
        anchor = (case["anchor"][0] * HX, case["anchor"][1] * HX, z)
        a_local = (case["a"][0] * HX, case["a"][1] * HX, BLADE_HEIGHT)
        b_local = (case["b"][0] * HX, case["b"][1] * HX, BLADE_HEIGHT)
        fill = fill_reach(sim, anchor, a_local, b_local, case["r"])
        if fill["allowed"]:
            continue
        tested += 1
        walked += int(fill["walked"])
        cells = pred_cells(fill["pred"], LAYERS)
        hand, excess = hand_reach(sim.fluid, case["wall"], fill["anchor_cell"], fill["box"], cells)
        bx0, _, by0, _ = fill["box"]
        walk = fill["reach"][cells[0] - bx0, cells[1] - by0, cells[2]]
        bad = int((hand != walk).sum())
        mismatched += bad
        if fill["distance"] is not None:
            start = (fill["anchor_cell"][0] - bx0, fill["anchor_cell"][1] - by0, fill["anchor_cell"][2])
            man = manhattan(fill["reach"].shape, start)[cells[0] - bx0, cells[1] - by0, cells[2]]
            dist = fill["distance"][cells[0] - bx0, cells[1] - by0, cells[2]].astype(np.int64)
            open_cell = sim.fluid[cells]
            walk_excess = np.where(dist >= DISTANCE_CAP, HAND_UNREACHABLE, dist - man)
            hand_excess = np.where(excess >= HAND_UNREACHABLE, HAND_UNREACHABLE, excess)
            diff = int(((walk_excess != hand_excess) & open_cell).sum())
            excess_mismatch += diff
            bad += diff
        if bad and len(examples) < 5:
            examples.append(dict(case=case, mismatched=bad, box=fill["box"], anchor_cell=fill["anchor_cell"]))
    return dict(pass_=tested >= 50 and walked == tested and mismatched == 0 and excess_mismatch == 0, cases_tested=tested, walked=walked,
                set_mismatch_cells=mismatched - excess_mismatch, excess_mismatch_cells=excess_mismatch, examples=examples)


def command_unit():
    ok_a, rows_a = unit_identical()
    rows_b = unit_hand_vs_walk()
    result = dict(unit_a_no_blocked_identical=dict(pass_=ok_a, rows=rows_a), unit_b_hand_vs_walk=rows_b, pass_=ok_a and rows_b["pass_"],
                  tool_sha256=sha256_file(TOOL_PATH))
    print(json.dumps(result, ensure_ascii=False, indent=1))
    return 0 if result["pass_"] else 1


def command_scenes():
    check_prereg()
    hashes, tables = {}, {}
    for name in OCC_BUILDERS:
        replay, image = build_occ_scene(name)
        write_scene_replay(REPLAY_FOLDERS["scenes"], replay, image)
        tables[name] = dict(label=OCC_SCENE_LABELS[name], anchor=anchor_table(replay, image, "장면 정의(구를 낸 폰의 골반)", 0.0),
                            shape_check=shape_margin(replay, image), steps=len(replay["steps"]), dumps=rs.dump_steps(replay),
                            count_region=replay["occ_scene"].get("count_region"))
    replay, image, table = build_anchored("r5", EDGE_OUTSIDE)
    write_scene_replay(REPLAY_FOLDERS["r5"], replay, image)
    tables[EDGE_OUTSIDE] = dict(label="가장자리-닻 창 밖(s2a_frame_at_edge 닻 판, R5)", anchor=table, shape_check=shape_margin(replay, image),
                                steps=len(replay["steps"]), dumps=rs.dump_steps(replay))
    for name in OCC_BUILDERS:
        hashes[name] = scene_hash_row(REPLAY_FOLDERS["scenes"], name)
    hashes[EDGE_OUTSIDE] = scene_hash_row(REPLAY_FOLDERS["r5"], EDGE_OUTSIDE)
    body = dict(format="fog3d-occ-scene-hashes-1", prereg_sha256=PREREG_SHA256, tool_sha256=sha256_file(TOOL_PATH), scenes=hashes)
    write_json(SCENE_HASHES, body)
    failures = [n for n, t in tables.items() if not t["anchor"]["gap_check_pass"] or not t["shape_check"]["pass_"]]
    print(json.dumps(dict(scene_hashes=SCENE_HASHES, tables=tables, check_failures=failures), ensure_ascii=False, indent=1))
    return 1 if failures else 0


def scene_hash_row(folder, name):
    path = os.path.join(folder, f"{name}.json")
    replay = read_json(path)
    row = dict(path=os.path.relpath(path, ROOT).replace("\\", "/"), sha256=sha256_file(path))
    if "ground" in replay:
        ground = os.path.join(folder, replay["ground"]["file"])
        row.update(ground_path=os.path.relpath(ground, ROOT).replace("\\", "/"), ground_sha256=sha256_file(ground))
    return row


def summarize_rows(rows, keys=("wind", "erosion", "union")):
    out = {}
    for key in keys:
        values = [row[key] for row in rows]
        out[key] = dict(max_per_step=max(values) if values else 0, sum=int(sum(values)), steps_with_hits=sum(1 for v in values if v > 0))
    return out


def command_precheck():
    check_prereg()
    recorded = read_json(SCENE_HASHES)["scenes"]
    hash_problems = [n for n, row in recorded.items() if sha256_file(os.path.join(ROOT, row["path"])) != row["sha256"]]
    if hash_problems:
        raise SystemExit(f"장면 해시가 기록과 다름: {hash_problems}")
    install()
    results = {}
    for name in PRECHECK_SCENES:
        replay, image = load_scene(REPLAY_FOLDERS["scenes"], name)
        region = replay["occ_scene"]["count_region"]
        info = run_reference(replay, image, None, False, region=region_function(region), box_check=False)
        summary = summarize_rows(info["rows"])
        results[name] = dict(label=OCC_SCENE_LABELS[name], occlusion="끔", count_region=region, steps=info["steps"], seconds=info["seconds"],
                             region_cells=int(region_mask(RUN["sim"], region).sum()), summary=summary, per_step=info["rows"],
                             precondition_pass=summary["union"]["sum"] > 0, scene_sha256=recorded[name]["sha256"])
    hands = {name: hand_sets(name) for name in HAND_SCENES}
    write_json(HAND_OUT, dict(format="fog3d-occ-hand-sets-1", prereg_sha256=PREREG_SHA256, tool_sha256=sha256_file(TOOL_PATH),
                              rule="B2-2 손 식: e = 두 끝 값 가운데 작은 것, 끝 값 = 끝 너머 줄이 채우기 상자 안이면 max(0, 2·(끝 줄 + 1 − max(닻 줄, 대상 줄)))(반대 끝 거울) 아니면 무한, 이어짐 = 막히지 않음 ∧ e ≤ 2",
                              scenes=hands))
    body = dict(format="fog3d-occ-precheck-1", prereg_sha256=PREREG_SHA256, tool_sha256=sha256_file(TOOL_PATH), scene_hashes=SCENE_HASHES,
                precondition={n: dict(pass_=r["precondition_pass"], wind=r["summary"]["wind"], erosion=r["summary"]["erosion"], union=r["summary"]["union"])
                              for n, r in results.items()},
                scenes=results, hand_sets=dict(path=HAND_OUT, summary={n: dict(rows=len(h["rows"]), cells=h["total_cells"], connected=h["total_connected"],
                                                                                 wall_cells=h["wall_cells"], set_sha256=h["set_sha256"]) for n, h in hands.items()}))
    write_json(PRECHECK_OUT, body)
    print(json.dumps(dict(precondition=body["precondition"], hand_sets=body["hand_sets"], out=PRECHECK_OUT), ensure_ascii=False, indent=1))
    return 0 if all(r["precondition_pass"] for r in results.values()) else 1


def command_anchored(args):
    check_prereg()
    names = [n for n in args.only.split(",") if n]
    if not names:
        raise SystemExit("--only 가 비었음")
    occlusion = args.occlusion == "on"
    install()
    prepared = prepare_anchored([(args.flavor, name) for name in names])
    jobs = [(args.flavor, name, occlusion, args.shake, not args.no_compare) for name in names]
    infos, failures = run_jobs(anchored_job, jobs, args.jobs)
    write_manifests(infos, prepared)
    for info in infos:
        print(json.dumps({k: v for k, v in info.items() if k not in ("rows", "files")}, ensure_ascii=False), flush=True)
    bad = [i["name"] for i in infos if "base_compare" in i and not i["base_compare"]["all_same"]]
    return 1 if bad or failures else 0


def plan_pairs():
    onoff = [("b4", n) for n in b4_flat_names() + [B4_SLOPE]] + [("r5", n) for n in ON_EQ_OFF_S2A]
    changed = [("scenes", n) for n in W_NAMES + EDGE_CHANGED] + [("r5", n) for n in CHANGED_S2A] + [("stage1", n) for n in STAGE1_CHANGED]
    return onoff, changed


def generate_jobs(groups):
    onoff, changed = plan_pairs()
    jobs = []
    if "onoff" in groups:
        jobs += [(f, n, False, False, True) for f, n in onoff] + [(f, n, True, False, False) for f, n in onoff]
    if "changed" in groups:
        jobs += [(f, n, True, False, False) for f, n in changed] + [(f, n, True, True, False) for f, n in changed]
    if "changed-off" in groups:
        jobs += [(f, n, False, False, True) for f, n in changed if f in BASE_REF]
    return jobs


def command_generate(args):
    check_prereg()
    groups = [g for g in args.groups.split(",") if g]
    if not groups or any(g not in GENERATE_GROUPS for g in groups):
        raise SystemExit(f"--groups 는 {GENERATE_GROUPS} 가운데에서")
    jobs = generate_jobs(groups)
    install()
    prepared = prepare_anchored(sorted({(j[0], j[1]) for j in jobs}))
    jobs.sort(key=lambda j: (-prepared[(j[0], j[1])]["steps"] * (2 if j[2] else 1), j[1], j[2], j[3]))
    started = time.time()
    infos, failures = run_jobs(anchored_job, jobs, args.jobs)
    write_manifests(infos, prepared)
    runs = sorted(infos, key=lambda i: (i["flavor"], i["name"], i["occlusion"], i["shake"]))
    base_bad = [f"{i['flavor']}/{i['name']}" for i in infos if "base_compare" in i and not i["base_compare"]["all_same"]]
    body = dict(format="fog3d-occ-generate-1", prereg_sha256=PREREG_SHA256, tool_sha256=sha256_file(TOOL_PATH), groups=groups, jobs=len(jobs), done=len(infos),
                failures=failures, base_compare_bad=base_bad, seconds=round(time.time() - started, 1),
                prepared={f"{f}/{n}": v for (f, n), v in sorted(prepared.items())},
                runs=[{k: v for k, v in i.items() if k not in ("rows", "anchor_table")} for i in runs])
    out = os.path.join(OPS, f"occ-generate-{'-'.join(groups)}.json")
    write_json(out, body)
    print(json.dumps(dict(out=out, jobs=len(jobs), done=len(infos), failures=failures, base_compare_bad=base_bad, seconds=body["seconds"]), ensure_ascii=False, indent=1))
    return 1 if failures or base_bad else 0


def manifest_scene(out_dir, name):
    path = os.path.join(out_dir, "manifest.json")
    return read_json(path)["scenes"].get(name) if os.path.exists(path) else None


def command_onoff(args):
    check_prereg()
    onoff, _ = plan_pairs()
    rows, failures = {}, []
    for flavor, name in onoff:
        off_dir, on_dir = (os.path.join(REF_OCC, flavor, mode) for mode in ("off", "on"))
        off, on = manifest_scene(off_dir, name), manifest_scene(on_dir, name)
        if off is None or on is None:
            failures.append(f"{flavor}/{name}: 끔 또는 켬 기준 없음")
            continue
        off_files = sorted(f for f in off["files"] if not f.endswith(".reach.bin"))
        on_files = sorted(f for f in on["files"] if not f.endswith(".reach.bin"))
        compared = compare_files(off_files, off_dir, on_dir)
        bytes_same = off_files == on_files and bool(compared) and all(v == "same" for v in compared.values())
        pre = dict(off_fill_boxes_with_blocked=off["fill_boxes_with_blocked"], on_fill_boxes_with_blocked=on["fill_boxes_with_blocked"],
                   off_blocked_cells=off["occ_stats"]["blocked_cells"], on_blocked_cells=on["occ_stats"]["blocked_cells"],
                   no_anchor=off["occ_stats"]["no_anchor"] + on["occ_stats"]["no_anchor"], sphere_steps=on["occ_stats"]["sphere_steps"])
        pre["pass_"] = pre["off_fill_boxes_with_blocked"] == 0 and pre["on_fill_boxes_with_blocked"] == 0 and pre["no_anchor"] == 0
        base = off.get("base_compare", {})
        table = on.get("anchor_table", {})
        rows[name] = dict(flavor=flavor, steps=on["steps"], precondition=pre, bytes_same=bytes_same, files=len(off_files),
                          reach_files=sorted(f for f in on["files"] if f.endswith(".reach.bin")), state_hash_same=on["hash"] == off["hash"],
                          off_vs_base_bytes_same=base.get("all_bytes_same"), allowed=on["occ_stats"]["allowed"], walked=on["occ_stats"]["walked"],
                          anchor_rule=table.get("rule"), nudge_cm=table.get("nudge_cm"), anchor_gap_min_cm=table.get("min_gap_to_30_cm"),
                          anchor_gap_pass=table.get("gap_check_pass"))
        if not pre["pass_"]:
            failures.append(f"{flavor}/{name}: 앞조건(채우기 상자 막힌 칸 0) 실패")
        if not bytes_same:
            failures.append(f"{flavor}/{name}: 켬 ≠ 끔 바이트")
        if not base.get("all_bytes_same"):
            failures.append(f"{flavor}/{name}: 끔 ≠ 기존 기준 바이트")
    body = dict(format="fog3d-occ-onoff-1", prereg_sha256=PREREG_SHA256, tool_sha256=sha256_file(TOOL_PATH), scenes=rows, failures=failures,
                pass_=not failures and len(rows) == len(onoff))
    write_json(ONOFF_OUT, body)
    print(json.dumps(dict(out=ONOFF_OUT, pass_=body["pass_"], scenes=len(rows), failures=failures), ensure_ascii=False, indent=1))
    return 0 if body["pass_"] else 1


def number_limit(sensitivity):
    return min(LIMIT_CEILING, max(LIMIT_FLOOR, 3.0 * sensitivity))


def reach_difference(path_a, path_b):
    if not (os.path.exists(path_a) and os.path.exists(path_b)):
        return None
    a, b = np.fromfile(path_a, "<u2"), np.fromfile(path_b, "<u2")
    return int(np.count_nonzero(a != b)) if a.size == b.size else -1


def command_numbers(args):
    check_prereg()
    import fog3d_compare as dump_base
    import fog3d_compare_v2 as dump_v2
    _, changed = plan_pairs()
    sensitivity, dumps, sources, shake_rows, problems, manifests = {}, {}, {}, {}, [], {}
    group_max = {g: 0.0 for g in NUMBER_GROUPS}
    for flavor, name in changed:
        ref_dir = os.path.join(REF_OCC, flavor, "on")
        shake_dir = os.path.join(ref_dir, rs.SHAKE_FOLDER)
        replay_path = os.path.join(REPLAY_FOLDERS[flavor], f"{name}.json")
        replay = read_json(replay_path)
        sources[name] = dict(flavor=flavor, reference=os.path.relpath(ref_dir, ROOT).replace("\\", "/"), replay_sha256=sha256_file(replay_path))
        for dump in rs.dump_steps(replay):
            key = f"{name}__{dump}"
            try:
                ref, shaken = dump_base.load_ref(ref_dir, name, dump), dump_base.load_ref(shake_dir, name, dump)
                groups = dump_v2.group_diffs(ref, shaken)
                mismatch = dump_v2.fluid_mismatch(ref, shaken)
            except Exception as exc:
                problems.append(f"{key}: {type(exc).__name__}: {exc}")
                continue
            present = [g for g in NUMBER_GROUPS if g in groups]
            if not present:
                problems.append(f"{key}: 비교할 묶음 없음")
                continue
            sensitivity[key] = {g: float(groups[g]) for g in present}
            relative = {g: number_limit(sensitivity[key][g]) for g in present if number_limit(sensitivity[key][g]) > LIMIT_FLOOR}
            for g in present:
                group_max[g] = max(group_max[g], sensitivity[key][g])
            if relative:
                dumps[key] = dict(relative=relative)
            shake_rows[key] = dict(fluid_mismatch=mismatch, absent_groups=[g for g in NUMBER_GROUPS if g not in present],
                                   reach_bits_diff=reach_difference(os.path.join(ref_dir, key + ".reach.bin"), os.path.join(shake_dir, key + ".reach.bin")))
    for flavor in sorted({f for f, _ in changed}):
        for folder in (os.path.join(REF_OCC, flavor, "on"), os.path.join(REF_OCC, flavor, "on", rs.SHAKE_FOLDER)):
            path = os.path.join(folder, "manifest.json")
            manifests[os.path.relpath(path, ROOT).replace("\\", "/")] = sha256_file(path) if os.path.exists(path) else None
    if problems:
        print(json.dumps(dict(problems=problems), ensure_ascii=False, indent=1))
        return 1
    body = dict(version=2, format=NUMBERS_FORMAT, candidate="R5", formula="B0(지금 식, wind_blend 없음)", rule=NUMBERS_RULE, default=DEFAULT_NUMBER_LIMITS,
                dumps=dumps, sensitivity=sensitivity, shake=shake_rows, sources=sources, shake_cm=rp.SENSITIVITY_SHAKE_CM, seed=rp.SENSITIVITY_SEED,
                reference_manifests=manifests, prereg_sha256=PREREG_SHA256, tool_sha256=sha256_file(TOOL_PATH))
    write_json(NUMBERS_OUT, body)
    print(json.dumps(dict(out=NUMBERS_OUT, sha256=sha256_file(NUMBERS_OUT), scenes=len(sources), dumps_total=len(sensitivity), dumps_with_wider_limit=len(dumps),
                          sensitivity_max=group_max, reach_bits_diff_total=sum(r["reach_bits_diff"] or 0 for r in shake_rows.values()),
                          reach_missing=sum(1 for r in shake_rows.values() if r["reach_bits_diff"] is None),
                          fluid_mismatch_total=sum(r["fluid_mismatch"] for r in shake_rows.values())), ensure_ascii=False, indent=1))
    return 0


def predicate_bits(fill, nz):
    x0, x1, y0, y1 = fill["pred"]
    if fill.get("reach") is None:
        return np.ones((x1 - x0 + 1, y1 - y0 + 1, nz), bool)
    bx0, _, by0, _ = fill["box"]
    return fill["reach"][x0 - bx0:x1 - bx0 + 1, y0 - by0:y1 - by0 + 1, :]


def guard_columns(wall, rx, ry, anchor_x_cm):
    center_x = wall["front_x_cm"] + wall["thickness_cm"] / 2.0
    half_x = core.inflate_half(wall["thickness_cm"] / 2.0, 0.0)
    front = rx < center_x - half_x if anchor_x_cm < center_x else rx > center_x + half_x
    beyond = np.abs(ry - wall["center_y_cm"]) > core.inflate_half(wall["length_cm"] / 2.0, 0.0) + 2.0 * HX
    return front[:, None] | beyond[None, :]


class OverreachSink:
    def __init__(self, wall, hand):
        self.wall = wall
        self.hand = None if hand is None else {(r["step"], r["sphere"]): r for r in hand["rows"]}
        self.rows = []

    def __call__(self, sim, step_index, index, fill):
        x0, x1, y0, y1 = fill["pred"]
        row = dict(step=step_index, sphere=index, pred=[int(x0), int(x1), int(y0), int(y1)], allowed=bool(fill.get("allowed")),
                   oversize=bool(fill.get("oversize")), anchor_blocked=bool(fill.get("anchor_blocked")), walked=bool(fill.get("walked")),
                   box_blocked_cells=int(fill.get("blocked_cells", 0)))
        if x0 > x1 or y0 > y1:
            row.update(cells=0, unreached=0)
            self.rows.append(row)
            return
        bits = predicate_bits(fill, sim.nz)
        row.update(cells=int(bits.size), unreached=int(bits.size - np.count_nonzero(bits)))
        if self.wall is not None and "anchor_cell" in fill:
            rx, ry, _ = sim.center_rel()
            guard = np.broadcast_to(guard_columns(self.wall, rx, ry, float(rx[fill["anchor_cell"][0]]))[x0:x1 + 1, y0:y1 + 1, None], bits.shape)
            row.update(guard_cells=int(guard.sum()), over_occluded=int((guard & ~bits).sum()),
                       guard_blocked=int((guard & ~sim.fluid[x0:x1 + 1, y0:y1 + 1, :]).sum()))
        if self.hand is not None:
            ref = self.hand.get((step_index, index))
            if ref is None or list(ref["pred"]) != row["pred"]:
                row.update(hand_diff=None, hand_problem="손 식 행 없음 또는 술어 상자 다름")
            else:
                expected = np.unpackbits(np.frombuffer(bytes.fromhex(ref["bits_hex"]), np.uint8))[:ref["cells"]].astype(bool)
                row["hand_diff"] = int(np.count_nonzero(expected != bits.ravel())) if expected.size == bits.size else -1
        self.rows.append(row)

    def summary(self):
        rows = self.rows
        guarded = [r for r in rows if "over_occluded" in r]
        hands = [r for r in rows if "hand_diff" in r]
        return dict(sphere_steps=len(rows), allowed=sum(r["allowed"] for r in rows), allowed_oversize=sum(r["oversize"] for r in rows),
                    allowed_anchor_blocked=sum(r["anchor_blocked"] for r in rows), walked=sum(r["walked"] for r in rows),
                    predicate_cells=sum(r["cells"] for r in rows), unreached_predicate_cells=sum(r["unreached"] for r in rows),
                    guard_sphere_steps=len(guarded), guard_cells=sum(r["guard_cells"] for r in guarded), guard_blocked=sum(r["guard_blocked"] for r in guarded),
                    over_occluded=sum(r["over_occluded"] for r in guarded), over_sphere_steps=sum(1 for r in guarded if r["over_occluded"] > 0),
                    hand_rows=len(hands), hand_expected_rows=None if self.hand is None else len(self.hand),
                    hand_diff=sum(r["hand_diff"] for r in hands if r["hand_diff"] is not None and r["hand_diff"] >= 0),
                    hand_problems=sum(1 for r in hands if r["hand_diff"] is None or r["hand_diff"] < 0))


def whole_window(sim):
    return np.ones((sim.nx, sim.ny, sim.nz), bool)


def judge_job(job):
    flavor, name, occlusion = job
    flavor_tool(flavor)
    replay, image = load_scene(REPLAY_FOLDERS[flavor], name)
    occ = replay.get("occ_scene", {})
    region = occ.get("count_region")
    hand = read_json(HAND_OUT)["scenes"][name] if occlusion and name in HAND_SCENES else None
    sink = OverreachSink(occ.get("wall"), hand) if occlusion else None
    info = run_reference(replay, image, None, occlusion, region=region_function(region) if region else whole_window, box_check=False, fill_sink=sink)
    rows = info["rows"]
    out = dict(name=name, flavor=flavor, occlusion=occlusion, steps=info["steps"], seconds=info["seconds"], state_hash=info["hash"],
               region="세는 영역 '벽 뒤'" if region else "창 전체", region_cells=int(region_mask(RUN["sim"], region).sum()) if region else None,
               summary=summarize_rows(rows), summary_window=summarize_rows(rows, ("wind_all", "erosion_all", "union_all")), occ_stats=info["occ_stats"],
               per_step=rows)
    if sink is not None:
        out.update(sink=sink.summary(), sphere_rows=sink.rows)
    return out


def command_judge_on(args):
    check_prereg()
    recorded = read_json(SCENE_HASHES)["scenes"]
    problems = []
    for flavor, name in JUDGE_SCENES:
        row = recorded_row(name, REPLAY_FOLDERS[flavor], recorded)
        problems += [f"{name}: 장면 해시 기록 없음"] if row is None else verify_recorded(name, row)
    if problems:
        raise SystemExit(f"장면 해시 문제: {problems}")
    precheck = read_json(PRECHECK_OUT)["precondition"]
    jobs = [(f, n, True) for f, n in JUDGE_SCENES] + [(f, n, False) for f, n in JUDGE_SCENES]
    infos, failures = run_jobs(judge_job, jobs, args.jobs)
    runs = {}
    for info in infos:
        runs.setdefault(info["name"], {})["on" if info["occlusion"] else "off"] = info
    main_rows = {}
    for name in PRECHECK_SCENES:
        on, off = runs[name]["on"], runs[name]["off"]
        main_rows[name] = dict(label=OCC_SCENE_LABELS[name], region_cells=on["region_cells"],
                               judgment_1=dict(wind=on["summary"]["wind"], erosion=on["summary"]["erosion"],
                                               pass_=on["summary"]["wind"]["sum"] == 0 and on["summary"]["erosion"]["sum"] == 0),
                               judgment_2=dict(over_occluded=on["sink"]["over_occluded"], over_sphere_steps=on["sink"]["over_sphere_steps"],
                                               guard_cells=on["sink"]["guard_cells"], guard_sphere_steps=on["sink"]["guard_sphere_steps"],
                                               guard_blocked=on["sink"]["guard_blocked"], pass_=on["sink"]["over_occluded"] == 0),
                               off_info=dict(wind=off["summary"]["wind"], erosion=off["summary"]["erosion"]),
                               off_matches_precheck=all(off["summary"][k] == precheck[name][k] for k in ("wind", "erosion", "union")))
    hand_rows = {name: dict(label=OCC_SCENE_LABELS[name], hand_diff=runs[name]["on"]["sink"]["hand_diff"], hand_rows=runs[name]["on"]["sink"]["hand_rows"],
                            hand_expected_rows=runs[name]["on"]["sink"]["hand_expected_rows"], hand_problems=runs[name]["on"]["sink"]["hand_problems"],
                            over_occluded=runs[name]["on"]["sink"]["over_occluded"], guard_cells=runs[name]["on"]["sink"]["guard_cells"])
                 for name in HAND_SCENES}
    allowed = {name: dict(label=OCC_SCENE_LABELS.get(name, "가장자리-닻 창 밖(s2a_frame_at_edge 닻 판, R5)"),
                          **{k: runs[name]["on"]["occ_stats"][k] for k in ("sphere_steps", "allowed", "allowed_anchor_blocked", "allowed_oversize", "no_anchor",
                                                                           "walked", "anchor_clamped_ij", "anchor_clamped_k", "boxes_with_blocked")},
                          allowed_steps=sorted({r["step"] for r in runs[name]["on"]["sphere_rows"] if r["allowed"]}))
               for _, name in JUDGE_SCENES}
    side = {name: dict(on=dict(region=runs[name]["on"]["summary"], window=runs[name]["on"]["summary_window"]),
                       off=dict(region=runs[name]["off"]["summary"], window=runs[name]["off"]["summary_window"]), region=runs[name]["on"]["region"])
            for _, name in JUDGE_SCENES}
    body = dict(format="fog3d-occ-on-judge-1", prereg_sha256=PREREG_SHA256, tool_sha256=sha256_file(TOOL_PATH), scene_hashes=SCENE_HASHES,
                scene_hashes_sha256=sha256_file(SCENE_HASHES), hand_sets_sha256=sha256_file(HAND_OUT), failures=failures,
                rules=dict(judgment_1="세는 영역 '벽 뒤' 입력 영향 칸(바람: u·v·w 가운데 하나라도 k > 0 이고 이어짐, 깎기: 깎는 양 > 0 이고 이어짐) 걸음별 수",
                           judgment_2="구-걸음마다 술어 상자(창으로 자름) 안 칸 가운데 지킴 칸(칸 중심 x 가 부풀린 앞면보다 닻 쪽, 또는 |칸 중심 y − 벽 가운데 y| > 부풀린 반길이 + 2칸)의 이어짐 비트 0 수",
                           hand="구-걸음마다 술어 상자 안 이어짐 비트 대 occ-hand-sets.json 손 식 비트의 다른 칸 수"),
                main=main_rows, hand=hand_rows, allowed=allowed, side_by_side=side,
                runs={f"{n}/{m}": {k: v for k, v in r.items() if k not in ("per_step", "sphere_rows")} for n, d in runs.items() for m, r in d.items()},
                per_step={f"{n}/{m}": r["per_step"] for n, d in runs.items() for m, r in d.items()},
                sphere_rows={n: d["on"]["sphere_rows"] for n, d in runs.items() if "on" in d})
    write_json(JUDGE_OUT, body)
    ok = not failures and all(r["judgment_1"]["pass_"] and r["judgment_2"]["pass_"] for r in main_rows.values())
    print(json.dumps(dict(out=JUDGE_OUT, failures=failures, main={n: dict(j1=r["judgment_1"], j2=r["judgment_2"], off_matches_precheck=r["off_matches_precheck"])
                                                               for n, r in main_rows.items()}, hand=hand_rows, allowed=allowed), ensure_ascii=False, indent=1))
    return 0 if ok else 1


def compare_reach_row(ref_dir, gpu_dir, stem):
    scene, dump = stem[:-len(".reach.bin")].split("__", 1)
    ref = np.fromfile(os.path.join(ref_dir, stem), "<u2")
    row = dict(scene=scene, dump=dump, cells=int(ref.size), ref_set_bits=int(np.unpackbits(ref.view(np.uint8)).sum()), different_cells=None, passed=False, info="")
    mine = os.path.join(gpu_dir, stem)
    if not os.path.isfile(mine):
        row["info"] = "GPU 파일 없음"
        return row
    gpu = np.fromfile(mine, "<u2")
    if gpu.size != ref.size:
        row["info"] = f"칸 수 다름: GPU {gpu.size}, 기준 {ref.size}"
        return row
    row["different_cells"] = int(np.count_nonzero(gpu != ref))
    row["passed"] = row["different_cells"] == 0
    return row


def command_compare_reach(args):
    if not args.ref_dir or not args.gpu_dir:
        raise SystemExit("compare-reach 에는 --ref-dir 과 --gpu-dir 이 필요함")
    chosen = {s for s in args.scenes.split(",") if s} if args.scenes else None
    in_scope = lambda f: f.endswith(".reach.bin") and "__" in f and (chosen is None or f.split("__", 1)[0] in chosen)
    stems = sorted(f for f in os.listdir(args.ref_dir) if in_scope(f))
    rows = [compare_reach_row(args.ref_dir, args.gpu_dir, stem) for stem in stems]
    missing_scenes = sorted(chosen - {r["scene"] for r in rows}) if chosen else []
    extra = sorted(f for f in os.listdir(args.gpu_dir) if in_scope(f) and f not in stems) if os.path.isdir(args.gpu_dir) else []
    failed = [r for r in rows if not r["passed"]]
    body = dict(format="fog3d-occ-compare-reach-1", tool_sha256=sha256_file(TOOL_PATH), ref_dir=os.path.abspath(args.ref_dir), gpu_dir=os.path.abspath(args.gpu_dir),
                scenes=sorted(chosen) if chosen else None, dumps=len(rows), failed=len(failed), missing_scenes=missing_scenes, extra_gpu_files=extra,
                different_cells=sum(r["different_cells"] or 0 for r in rows), rows=rows)
    if args.report:
        write_json(args.report, body)
    print(json.dumps(dict(dumps=len(rows), failed=len(failed), different_cells=body["different_cells"], missing_scenes=missing_scenes, extra_gpu_files=extra,
                          failures=[f"{r['scene']}__{r['dump']}: {r['different_cells']} {r['info']}" for r in failed]), ensure_ascii=False, indent=1))
    if not rows:
        return 2
    return 0 if not failed and not missing_scenes else 1


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("unit", "scenes", "precheck", "table", "check-gates", "anchored", "judge-on", "generate", "onoff", "numbers", "compare-reach"))
    parser.add_argument("--ref-dir")
    parser.add_argument("--gpu-dir")
    parser.add_argument("--scenes", default="")
    parser.add_argument("--report")
    parser.add_argument("--require-complete", action="store_true")
    parser.add_argument("--flavor", choices=("r5", "b4", "stage1", "scenes"), default="b4")
    parser.add_argument("--groups", default="onoff,changed")
    parser.add_argument("--only", default="")
    parser.add_argument("--occlusion", choices=("off", "on"), default="off")
    parser.add_argument("--shake", action="store_true")
    parser.add_argument("--jobs", type=int, default=1)
    parser.add_argument("--no-compare", action="store_true")
    args = parser.parse_args(argv)
    if args.command == "unit":
        return command_unit()
    if args.command == "scenes":
        install()
        return command_scenes()
    if args.command == "precheck":
        return command_precheck()
    if args.command == "table":
        print(json.dumps(gate_table(), ensure_ascii=False, indent=1))
        return 0
    if args.command == "check-gates":
        result = check_gates(args.require_complete)
        print(json.dumps(result, ensure_ascii=False, indent=1))
        return 0 if result["ok"] else 1
    if args.command == "judge-on":
        return command_judge_on(args)
    if args.command == "generate":
        return command_generate(args)
    if args.command == "onoff":
        return command_onoff(args)
    if args.command == "numbers":
        return command_numbers(args)
    if args.command == "compare-reach":
        return command_compare_reach(args)
    return command_anchored(args)


if __name__ == "__main__":
    sys.exit(main())
