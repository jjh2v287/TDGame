"""에디터 밖 시스템 파이썬(numpy)에서 쓰는 공유 모듈: 연속 세계(D-45)의 이계 배치 정본(REALM_PLACEMENT), 걷는 연결로(CONNECTOR_ROWS, 월드 m 다각선)와 실제 40 m 갱도 승강 관문(SHAFT_ROWS)의 단면(먼 쪽 벽은 바깥 지형 기준 높이·볼록 바깥 띠, 카메라가 안쪽을 보는 끝은 절개 또는 낮춘 벽)·바닥·지형 스탬프·풋프린트·지선 도로·소품 상대 좌표·관문 출력 스키마(gates[])와 엄격 쿼드 걷기 판정·먼 쪽 벽 윗면 칸을 한곳에 둔다(P3-19 명세 3.1).
실행: generate_ashen_vale.py·generate_realm.py 가 import 한다. 자체 점검은 python Tools/WorldGen/realm_passages.py (PowerShell 또는 Git Bash, numpy·scipy 필요)
출력: 자체 점검 시 표준 출력에 연결로 조각별 꼭짓점·구간 경사·방위·경계 통과점·능선 구간, 갱도 축·착지 높이·슬롯 바닥 폭, 라이너 보이는 범위 4개, 합성 지형 스탬프 바닥 일치(실패 시 종료 코드 1)
상태: 현행(P3-19)
"""
import math
import sys

import numpy as np
from scipy import ndimage

import topdown_materials as tdm
import topdown_rules as tdr

FLOOR_HALF_WIDTH_M = 4.0
WALL_OUTER_M = 9.0
WALL_TOP_OUTER_M = 16.0
BAND_OUTER_M = 24.0
WALL_HEIGHT_M = 22.0
FLOOR_GRADE = 0.08
RIDGE_LIFT_M = 6.0
RIDGE_FADE_M = 6.0
OPEN_FLOOR_BLEND_M = 2.0
OPEN_WALL_BLEND_M = 10.0
FLOOR_END_RAMP_M = 0.01
END_CAP_M = BAND_OUTER_M - FLOOR_HALF_WIDTH_M
UNITS_PER_METER = 128.0
MARKER_FORWARD_M = 3.0
NEAR_LEDGE_HEIGHT_M = 2.5
NEAR_STEP_SLOPE = math.tan(math.radians(45.0))
NEAR_LEDGE_OUTER_M = 12.0
NEAR_RISE_SLOPE = math.tan(math.radians(45.0))
NEAR_RIDGE_RISE_SLOPE = math.tan(math.radians(50.0))
NEAR_OPEN_BLEND_M = 3.0
NEAR_CAP_MIN_OFFSET_M = 1.0
NEAR_SEAL_FULL_S_M = 34.0
NEAR_SEAL_LEDGE_S_M = 40.0
WALL_PROP_Z_REL_M = 11.0
FAR_WALL_PROP_Z_REL_M = 13.0
WALL_FACE_SLOPE = WALL_HEIGHT_M / (WALL_OUTER_M - FLOOR_HALF_WIDTH_M)
FAR_WALL_MIN_RELIEF_M = 7.0
FAR_WALL_CAMERA_MARGIN_M = 1.5
CAMERA_CLEARANCE_M = tdr.PAWN_CENTER_ABOVE_GROUND_CM / 100.0 + tdr.CAMERA_OFFSET_M[2]
END_RELIEF_M = CAMERA_CLEARANCE_M - FAR_WALL_CAMERA_MARGIN_M
END_RELIEF_MARGIN_M = 2.0
END_RELIEF_FADE_M = 6.0

FIELD_HALF_M = 504.0
REALM_SIZE_M = 504.0
FIELD_RIM_BAND_M = 30.0
REALM_EDGE_BAND_M = 45.0
RAMP_MAX_GRADE = math.tan(math.radians(15.0))
MAX_BEARING_STEP_DEG = 1.0
VERTEX_STEP_M = 0.01
CENTERLINE_SAMPLE_M = 0.5
SPUR_PROFILE_WINDOW = 61
SPUR_BLEND_M = 6.0
BOUNDARY_COVER_HALF_M = 30.0
WALK_MAX_SLOPE_DEG = 44.0
GATE_PAWN_HEIGHT_CM = 120.0
PRELOAD_LEAD_CM = 4000.0
PASSAGE_HALF_WIDTH_CM = WALL_TOP_OUTER_M * 100.0
SECTION_WIDEN_M = {"canyon": 0.0, "slot": 2.0}
SEGMENT_KINDS = ("canyon", "terrace", "ramp")

REALM_PLACEMENT = {
    "SunkenStarfield": {"center_m": (-63.0, -756.0), "z_offset_m": -34.0},
    "PaleHeights": {"center_m": (756.0, 452.0), "z_offset_m": 18.0},
}
DATA_LAYER_ROOT = "/Game/World/DataLayers"
REALM_GAMEPLAY_DATA_LAYER = {realm_id: f"{DATA_LAYER_ROOT}/DL_TDGameplay_{realm_id}" for realm_id in REALM_PLACEMENT}

MESH_ROOT = "/Game/DarkFantasyTopDown/StaticMeshes"
MAT_ROOT = "/Game/DarkFantasyTopDown/Materials"
ENGINE_PLANE = "/Engine/BasicShapes/Plane"
BEDROCK_MATERIAL = f"{MAT_ROOT}/Nature/Surfaces/MI_Bedrock2"
FOG_MATERIALS = {"Fog1": f"{MAT_ROOT}/VFX/MI_FogPlane_Var1", "Fog2": f"{MAT_ROOT}/VFX/MI_FogPlane_Var2"}
PROP_MESHES = {
    "Cliff2": f"{MESH_ROOT}/Nature/Cliff/SM_Cliff2", "Cliff3": f"{MESH_ROOT}/Nature/Cliff/SM_Cliff3", "Cliff4": f"{MESH_ROOT}/Nature/Cliff/SM_Cliff4",
    "StickedSkull": f"{MESH_ROOT}/SmallProps/SM_StickedSkull", "Lamppost": f"{MESH_ROOT}/Lanterns/SM_Lamppost",
    "SRock5": f"{MESH_ROOT}/Nature/Rocks/SmallRocks/SM_Rock_5", "SRock7": f"{MESH_ROOT}/Nature/Rocks/SmallRocks/SM_Rock_7",
}
COLD_LIGHT = [0.55, 0.68, 1.0]
WARM_LIGHT = [1.0, 0.62, 0.32]
FLOOR_TILE_LENGTH_M = 10.0
FLOOR_TILE_WIDTH_M = 2.0 * FLOOR_HALF_WIDTH_M
FLOOR_TILE_LIFT_M = 0.03
WALL_CLIFF_STATIONS_M = (-26.0, -17.0, -8.0, 1.0, 10.0, 19.0, 28.0)
WALL_CLIFF_KEYS = ("Cliff2", "Cliff3", "Cliff4")

CONNECTOR_ROWS = (
    ("SunkenStarfieldSouthwest", "SunkenStarfield", "walk",
     ((-250.0, -440.0, 12.4453125), (-250.0, -562.23, -12.0), (-250.0, -590.23, -12.0), (-250.0, -672.34, -34.0)),
     ("canyon", "terrace", "ramp"), 0.20, -1.0, "canyon"),
    ("PaleHeightsNortheast", "PaleHeights", "walk",
     ((315.08, 411.31, 2.4921875), (450.0, 450.0, 13.71875), (540.0, 476.0, 20.78125), (593.0, 491.0, 24.3828125)),
     ("canyon", "canyon", "canyon"), FLOOR_GRADE, -1.0, "canyon"),
)

SHAFT_ROWS = (
    ("SunkenStarfieldSoutheast", "SunkenStarfield", (92.0, -478.0), 45.0, 6.0, -34.0, (9.0, 53.0), 225.0, (11.0, 117.48), 12.0),
)

LIFT_FRAME_YAW_DEG = 45.0
LIFT_FACE_V_M = 4.5
LIFT_HALF_WIDTH_M = 8.0
LIFT_RING_M = 1.0
LIFT_RING_ABOVE_SEAM_M = 12.0
LIFT_FIELD_BACK_V_M = -13.0
LIFT_FIELD_OUTER_RING_M = 1.0
LIFT_WINDOW_BACK_V_M = -11.0
LIFT_MAX_AXIS_LATERAL_ERROR_M = 1.0
LIFT_DECK_HALF_EXTENT_CM = (300.0, 300.0, 15.0)
LIFT_LANDING_START_V_M = -2.0
LIFT_PROP_FLOOR_LIFT_M = 0.03
LIFT_WALL_FACE_V_M = 3.45
LIFT_WALL_SIDE_W_M = 7.0
LIFT_WALL_TOP_ABOVE_SEAM_M = 8.0
LIFT_WALL_ROWS = 4
LIFT_WALL_COLUMNS = 2
LIFT_RAIL_V_M = 2.4
LIFT_RAIL_W_M = 3.65
LIFT_RAIL_SCALE = (0.6, 0.6, 8.3)
LIFT_LIGHT_V_M = 2.6
LIFT_ASYMMETRIC_LIGHT_CLEARANCE_M = 1.0
LIFT_ASYMMETRIC_LIGHT_MIN_RADIUS_CM = 200.0
LIFT_LIGHT_W_M = 5.0
LIFT_LIGHT_Z_REL_M = (-15.0, -5.0)
LIFT_BRIDGE_STATIONS_W_M = (5.22, 7.22, 9.22)
LIFT_BRIDGE_Z_REL_M = -0.43
LIFT_BRIDGE_FENCE_V_M = 1.45
LIFT_BRIDGE_FENCE_STATIONS_W_M = (5.5, 9.1)
LIFT_RIM_FENCE_W_M = 9.8
LIFT_RIM_FENCE_STATIONS_V_M = (-8.0, -3.8, 3.8, 8.0)
LIFT_FENCE_Z_REL_M = 0.03
LIFT_DECK_MESH = f"{MESH_ROOT}/WoodenParts/SM_WoodenFloorSegment"
LIFT_RAIL_MESH = f"{MESH_ROOT}/WoodenParts/SM_WoodenFence1"
LIFT_DECK_PACK_MATERIAL = f"{MAT_ROOT}/Props/MI_WoodenParts_Var2"
LIFT_LINER_PANEL_MESH = ENGINE_PLANE
LIFT_ABYSS_SCREEN_MATERIAL = "/Engine/EngineDebugMaterials/BlackUnlitMaterial"
LIFT_LINER_FACE_WIDTH_M = 2.0 * LIFT_HALF_WIDTH_M
LIFT_VISIBLE_ABOVE_DECK_CM = 750.0
LIFT_MOTION = {
    "cruise_speed_cm_per_second": 300.0, "acceleration_cm_per_second_squared": 100.0, "brake_deceleration_cm_per_second_squared": 80.0,
    "board_dwell_seconds": 1.2, "approach_radius_cm": 4000.0, "call_speed_cm_per_second": 600.0,
    "hold_depth_cm": 1912.5, "max_treadmill_seconds": 20.0, "ring_module_height_cm": 450.0, "ring_module_count": 7,
    "ring_top_depth_cm": 600.0, "abyss_screen_depth_cm": 600.0,
}
LIFT_PROP_MESHES = {
    "Pole": f"{MESH_ROOT}/WoodenParts/SM_WoodenPole", "Path": f"{MESH_ROOT}/WoodenParts/SM_WoodenPath", "Fence1": f"{MESH_ROOT}/WoodenParts/SM_WoodenFence1",
}
LIFT_MESH_BASE_BELOW_PIVOT_M = {"Pole": 0.08, "Path": 0.05, "Fence1": 0.06}


def smoothstep(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def quantize_height(z):
    return np.round(np.asarray(z, dtype=np.float64) * UNITS_PER_METER) / UNITS_PER_METER


def quantized_units(h):
    return np.round(32768.0 + h * UNITS_PER_METER)


def normalize_yaw(yaw_deg):
    return yaw_deg % 360.0


def is_integer_point(point):
    return all(float(v) == float(int(v)) for v in point)


def is_on_step(value, step):
    return abs(value / step - round(value / step)) < 1e-6


def bearing_deg(x0, y0, x1, y1):
    return normalize_yaw(math.degrees(math.atan2(y1 - y0, x1 - x0)))


def bearing_step_deg(a, b):
    return abs((b - a + 180.0) % 360.0 - 180.0)


def camera_offset_in_passage(yaw_deg):
    yaw_rad = math.radians(yaw_deg)
    dx, dy = tdr.CAMERA_OFFSET_M[0], tdr.CAMERA_OFFSET_M[1]
    return dx * math.cos(yaw_rad) + dy * math.sin(yaw_rad), -dx * math.sin(yaw_rad) + dy * math.cos(yaw_rad)


def near_sign_of(camera_l_m):
    if abs(camera_l_m) < NEAR_CAP_MIN_OFFSET_M:
        return 0.0
    return 1.0 if camera_l_m > 0.0 else -1.0


def outer_band_share(u):
    return np.clip(np.asarray(u, dtype=np.float64), 0.0, 1.0) ** 2


def far_relief_max_m(camera_l_m):
    band = BAND_OUTER_M - WALL_TOP_OUTER_M
    share = 1.0 - float(outer_band_share(1.0 - abs(camera_l_m) / band))
    if share <= 0.0:
        return WALL_HEIGHT_M
    return min(WALL_HEIGHT_M, (CAMERA_CLEARANCE_M - FAR_WALL_CAMERA_MARGIN_M) / share)


def realm_center_cm(realm_id):
    placement = REALM_PLACEMENT[realm_id]
    return placement["center_m"][0] * 100.0, placement["center_m"][1] * 100.0, placement["z_offset_m"] * 100.0


def realm_box_m(realm_id):
    cx, cy = REALM_PLACEMENT[realm_id]["center_m"]
    half = 0.5 * REALM_SIZE_M
    return cx - half, cx + half, cy - half, cy + half


def field_edge_distance_m(x, y):
    x = np.abs(np.asarray(x, dtype=np.float64))
    y = np.abs(np.asarray(y, dtype=np.float64))
    inside = np.maximum(x, y) <= FIELD_HALF_M
    outside_distance = np.hypot(np.maximum(x - FIELD_HALF_M, 0.0), np.maximum(y - FIELD_HALF_M, 0.0))
    return np.where(inside, FIELD_HALF_M - np.maximum(x, y), -outside_distance)


def shared_edge(realm_id):
    x0, x1, y0, y1 = realm_box_m(realm_id)
    if y1 == -FIELD_HALF_M:
        return {"name": "south", "axis": "y", "value": -FIELD_HALF_M, "range": (max(x0, -FIELD_HALF_M), min(x1, FIELD_HALF_M))}
    if y0 == FIELD_HALF_M:
        return {"name": "north", "axis": "y", "value": FIELD_HALF_M, "range": (max(x0, -FIELD_HALF_M), min(x1, FIELD_HALF_M))}
    if x0 == FIELD_HALF_M:
        return {"name": "east", "axis": "x", "value": FIELD_HALF_M, "range": (max(y0, -FIELD_HALF_M), min(y1, FIELD_HALF_M))}
    if x1 == -FIELD_HALF_M:
        return {"name": "west", "axis": "x", "value": -FIELD_HALF_M, "range": (max(y0, -FIELD_HALF_M), min(y1, FIELD_HALF_M))}
    raise ValueError(f"{realm_id}: realm box {realm_box_m(realm_id)} does not touch a field edge")


def boxes_overlap(a, b):
    return a[0] < b[1] and b[0] < a[1] and a[2] < b[3] and b[2] < a[3]


def require_realm_placement():
    field_box = (-FIELD_HALF_M, FIELD_HALF_M, -FIELD_HALF_M, FIELD_HALF_M)
    boxes = {}
    for realm_id, placement in REALM_PLACEMENT.items():
        if not is_integer_point(placement["center_m"]):
            raise ValueError(f"{realm_id}: center {placement['center_m']} is not integer meters")
        if not is_on_step(placement["z_offset_m"], 1.0 / UNITS_PER_METER):
            raise ValueError(f"{realm_id}: z offset {placement['z_offset_m']} is not a 1/{UNITS_PER_METER:.0f} m multiple")
        box = realm_box_m(realm_id)
        if boxes_overlap(box, field_box):
            raise ValueError(f"{realm_id}: realm box {box} overlaps the field box")
        edge = shared_edge(realm_id)
        if edge["range"][1] - edge["range"][0] <= 0.0:
            raise ValueError(f"{realm_id}: shared edge {edge} has no length")
        for other_id, other_box in boxes.items():
            if boxes_overlap(box, other_box):
                raise ValueError(f"{realm_id}: realm box {box} overlaps {other_id} {other_box}")
        boxes[realm_id] = box


def build_piece(gate_id, realm_id, name, vertices, segment_kinds, max_grade, section, declared_near_sign=None):
    if section not in SECTION_WIDEN_M:
        raise ValueError(f"{gate_id}/{name}: section {section} not in {sorted(SECTION_WIDEN_M)}")
    points = np.array(vertices, dtype=np.float64)
    if points.ndim != 2 or points.shape[1] != 3 or len(points) < 2:
        raise ValueError(f"{gate_id}/{name}: vertices must be at least two (x, y, floor_z) points")
    if len(segment_kinds) != len(points) - 1:
        raise ValueError(f"{gate_id}/{name}: {len(segment_kinds)} segment kinds for {len(points) - 1} segments")
    for x, y, z in points:
        if not (is_on_step(x, VERTEX_STEP_M) and is_on_step(y, VERTEX_STEP_M)):
            raise ValueError(f"{gate_id}/{name}: vertex ({x}, {y}) is not on the {VERTEX_STEP_M} m step")
        if not is_on_step(z, 1.0 / UNITS_PER_METER):
            raise ValueError(f"{gate_id}/{name}: floor z {z} is not a 1/{UNITS_PER_METER:.0f} m multiple")
    deltas = points[1:, :2] - points[:-1, :2]
    lengths = np.hypot(deltas[:, 0], deltas[:, 1])
    if np.any(lengths <= 0.0):
        raise ValueError(f"{gate_id}/{name}: zero-length segment")
    directions = deltas / lengths[:, None]
    bearings = [bearing_deg(0.0, 0.0, d[0], d[1]) for d in directions]
    grades = (points[1:, 2] - points[:-1, 2]) / lengths
    for k, (kind, grade) in enumerate(zip(segment_kinds, grades)):
        if kind not in SEGMENT_KINDS:
            raise ValueError(f"{gate_id}/{name}: segment {k} kind {kind} not in {SEGMENT_KINDS}")
        limit = {"canyon": max_grade, "terrace": 0.0, "ramp": RAMP_MAX_GRADE}[kind]
        if abs(grade) > limit + 1e-9:
            raise ValueError(f"{gate_id}/{name}: segment {k} ({kind}) grade {grade:.5f} exceeds {limit:.5f}")
    for k in range(1, len(bearings)):
        step = bearing_step_deg(bearings[k - 1], bearings[k])
        if step > MAX_BEARING_STEP_DEG:
            raise ValueError(f"{gate_id}/{name}: bearing step {step:.3f} deg at vertex {k} exceeds {MAX_BEARING_STEP_DEG}")
    camera = [camera_offset_in_passage(b) for b in bearings]
    signs = {near_sign_of(l_m) for _, l_m in camera}
    if len(signs) != 1:
        raise ValueError(f"{gate_id}/{name}: camera side changes along the piece {camera}")
    near_sign = signs.pop()
    if declared_near_sign is not None and near_sign != declared_near_sign:
        raise ValueError(f"{gate_id}/{name}: declared camera side {declared_near_sign} != computed {near_sign}")
    seg_start = np.concatenate([[0.0], np.cumsum(lengths)[:-1]])
    vertex_s = np.concatenate([[0.0], np.cumsum(lengths)])
    canyon_end = round(max(float(vertex_s[k + 1]) for k, kind in enumerate(segment_kinds) if kind == "canyon"), 6)
    camera_s_m, camera_l_m = camera[0]
    piece = {
        "gate_id": gate_id, "realm_id": realm_id, "name": name, "section": section, "widen_m": SECTION_WIDEN_M[section],
        "vertices": points, "segment_kinds": tuple(segment_kinds), "seg_start_s": seg_start, "seg_length": lengths, "seg_dir": directions,
        "vertex_s": vertex_s, "length": float(vertex_s[-1]), "bearings_deg": bearings, "grades": [float(g) for g in grades], "max_grade": float(max_grade),
        "stamp_s_m": (0.0, float(canyon_end)), "near_lateral_sign": near_sign,
        "camera_offset_s_m": round(camera_s_m, 3), "camera_offset_l_m": round(camera_l_m, 3), "far_relief_max_m": far_relief_max_m(camera_l_m),
        "near_cap_low": camera_s_m <= -NEAR_CAP_MIN_OFFSET_M, "near_cap_high": camera_s_m >= NEAR_CAP_MIN_OFFSET_M,
    }
    piece["ridge_zone_s_m"] = ridge_zone(piece)
    piece["boundary_crossing"] = boundary_crossing(piece)
    return piece


def centerline_samples(piece):
    s = np.arange(0.0, piece["length"] + CENTERLINE_SAMPLE_M, CENTERLINE_SAMPLE_M)
    s = np.minimum(s, piece["length"])
    x, y = to_grid_m(piece, s, np.zeros_like(s))
    return s, x, y


def ridge_zone(piece):
    s, x, y = centerline_samples(piece)
    d = field_edge_distance_m(x, y)
    zone = np.where(d >= 0.0, d < FIELD_RIM_BAND_M, -d < REALM_EDGE_BAND_M)
    if not zone.any():
        return None
    return float(s[zone].min()), float(s[zone].max())


def segment_box_exit(x0, y0, x1, y1):
    t_exit = 1.0
    for p, q in ((-(x1 - x0), x0 + FIELD_HALF_M), (x1 - x0, FIELD_HALF_M - x0), (-(y1 - y0), y0 + FIELD_HALF_M), (y1 - y0, FIELD_HALF_M - y0)):
        if p > 0.0:
            t_exit = min(t_exit, q / p)
    return t_exit


def boundary_crossing(piece):
    points = piece["vertices"]
    if float(field_edge_distance_m(points[0, 0], points[0, 1])) <= 0.0:
        raise ValueError(f"{piece['gate_id']}/{piece['name']}: first vertex {tuple(points[0, :2])} is not inside the field box")
    for k in range(len(points) - 1):
        x0, y0 = points[k, :2]
        x1, y1 = points[k + 1, :2]
        t = segment_box_exit(x0, y0, x1, y1)
        if t >= 1.0:
            continue
        s = float(piece["seg_start_s"][k] + t * piece["seg_length"][k])
        x, y = x0 + t * (x1 - x0), y0 + t * (y1 - y0)
        return {"s_m": round(s, 4), "point_m": (round(float(x), 4), round(float(y), 4)), "floor_z_m": float(floor_height(piece, s))}
    return None


def build_connector(gate_id, realm_id, kind, vertices, segment_kinds, max_grade, near_sign, section):
    if kind != "walk":
        raise ValueError(f"{gate_id}: connector kind {kind} must be walk")
    if realm_id not in REALM_PLACEMENT:
        raise ValueError(f"{gate_id}: realm {realm_id} has no placement")
    piece = build_piece(gate_id, realm_id, "connector", vertices, segment_kinds, max_grade, section, near_sign)
    piece["kind"] = kind
    require_piece_crosses_shared_edge(piece)
    end_x, end_y = piece["vertices"][-1, :2]
    x0, x1, y0, y1 = realm_box_m(realm_id)
    if not (x0 < end_x < x1 and y0 < end_y < y1):
        raise ValueError(f"{gate_id}: last vertex {(end_x, end_y)} is not inside the {realm_id} box")
    return piece


def require_piece_crosses_shared_edge(piece):
    crossing = piece["boundary_crossing"]
    if crossing is None:
        raise ValueError(f"{piece['gate_id']}/{piece['name']}: piece never leaves the field box")
    edge = shared_edge(piece["realm_id"])
    x, y = crossing["point_m"]
    along, across = (x, y) if edge["axis"] == "y" else (y, x)
    if abs(across - edge["value"]) > 1e-6:
        raise ValueError(f"{piece['gate_id']}/{piece['name']}: crossing {crossing['point_m']} is not on the {edge['name']} shared edge")
    margin = min(along - edge["range"][0], edge["range"][1] - along)
    reach = BAND_OUTER_M + piece["widen_m"]
    if margin < reach:
        raise ValueError(f"{piece['gate_id']}/{piece['name']}: crossing {crossing['point_m']} is {margin:.2f} m from the shared edge end < {reach}")
    crossing["edge"] = edge["name"]
    crossing["edge_margin_m"] = round(float(margin), 3)


def frame_coords(origin_m, yaw_deg, X, Y):
    yaw_rad = math.radians(yaw_deg)
    dx = np.asarray(X, dtype=np.float64) - origin_m[0]
    dy = np.asarray(Y, dtype=np.float64) - origin_m[1]
    return dx * math.cos(yaw_rad) + dy * math.sin(yaw_rad), -dx * math.sin(yaw_rad) + dy * math.cos(yaw_rad)


def frame_point(origin_m, yaw_deg, s, l):
    yaw_rad = math.radians(yaw_deg)
    return origin_m[0] + s * math.cos(yaw_rad) - l * math.sin(yaw_rad), origin_m[1] + s * math.sin(yaw_rad) + l * math.cos(yaw_rad)


def rounded_vertex(x, y, z):
    return round(x / VERTEX_STEP_M) * VERTEX_STEP_M, round(y / VERTEX_STEP_M) * VERTEX_STEP_M, z


def build_shaft(gate_id, realm_id, axis_m, frame_yaw_deg, top_z_m, bottom_z_m, top_canyon_s_m, slot_yaw_deg, slot_s_m, slot_floor_width_m):
    if realm_id not in REALM_PLACEMENT:
        raise ValueError(f"{gate_id}: realm {realm_id} has no placement")
    if not is_integer_point(axis_m):
        raise ValueError(f"{gate_id}: shaft axis {axis_m} is not integer meters")
    if frame_yaw_deg != LIFT_FRAME_YAW_DEG:
        raise ValueError(f"{gate_id}: frame yaw {frame_yaw_deg} != LIFT_FRAME_YAW_DEG {LIFT_FRAME_YAW_DEG}")
    if not top_z_m > bottom_z_m:
        raise ValueError(f"{gate_id}: top landing {top_z_m} must be above bottom {bottom_z_m}")
    for z in (top_z_m, bottom_z_m):
        if not is_on_step(z, 1.0 / UNITS_PER_METER):
            raise ValueError(f"{gate_id}: landing z {z} is not a 1/{UNITS_PER_METER:.0f} m multiple")
    top_exit_yaw = normalize_yaw(frame_yaw_deg + 90.0)
    if normalize_yaw(slot_yaw_deg) != normalize_yaw(frame_yaw_deg + 180.0):
        raise ValueError(f"{gate_id}: slot yaw {slot_yaw_deg} must leave the shaft along -v ({normalize_yaw(frame_yaw_deg + 180.0)})")
    if top_canyon_s_m[0] > LIFT_HALF_WIDTH_M + LIFT_RING_M + LIFT_FIELD_OUTER_RING_M or slot_s_m[0] > -LIFT_FIELD_BACK_V_M:
        raise ValueError(f"{gate_id}: top canyon {top_canyon_s_m} or slot {slot_s_m} does not start inside the landing band")
    mouth = frame_point(axis_m, top_exit_yaw, top_canyon_s_m[1], 0.0)
    landing_edge = frame_point(axis_m, top_exit_yaw, top_canyon_s_m[0], 0.0)
    top_canyon = build_piece(gate_id, realm_id, "top_canyon", (rounded_vertex(*mouth, top_z_m), rounded_vertex(*landing_edge, top_z_m)), ("canyon",), FLOOR_GRADE, "canyon")
    slot_start = frame_point(axis_m, slot_yaw_deg, slot_s_m[0], 0.0)
    slot_end = frame_point(axis_m, slot_yaw_deg, slot_s_m[1], 0.0)
    slot = build_piece(gate_id, realm_id, "slot", (rounded_vertex(*slot_start, bottom_z_m), rounded_vertex(*slot_end, bottom_z_m)), ("canyon",), FLOOR_GRADE, "slot")
    floor_width = 2.0 * (FLOOR_HALF_WIDTH_M + slot["widen_m"])
    if floor_width < slot_floor_width_m:
        raise ValueError(f"{gate_id}: slot floor width {floor_width} < {slot_floor_width_m}")
    require_piece_crosses_shared_edge(slot)
    x0, x1, y0, y1 = realm_box_m(realm_id)
    end_x, end_y = slot["vertices"][-1, :2]
    if not (x0 < end_x < x1 and y0 < end_y < y1):
        raise ValueError(f"{gate_id}: slot end {(end_x, end_y)} is not inside the {realm_id} box")
    shaft = {
        "gate_id": gate_id, "realm_id": realm_id, "axis_m": (float(axis_m[0]), float(axis_m[1])), "frame_yaw_deg": float(frame_yaw_deg),
        "top_z_m": float(top_z_m), "bottom_z_m": float(bottom_z_m), "drop_m": float(top_z_m - bottom_z_m),
        "top_exit_yaw_deg": top_exit_yaw, "bottom_exit_yaw_deg": normalize_yaw(slot_yaw_deg),
        "top_canyon_s_m": tuple(float(v) for v in top_canyon_s_m), "slot_s_m": tuple(float(v) for v in slot_s_m), "slot_floor_width_m": floor_width,
        "top_canyon": top_canyon, "slot": slot,
    }
    low_v, high_v, half_w = lift_band_extent()
    for corner_v in (low_v, high_v):
        for corner_w in (-half_w, half_w):
            cx, cy = shaft_to_grid_m(shaft, corner_v, corner_w)
            if float(field_edge_distance_m(cx, cy)) <= 0.0:
                raise ValueError(f"{gate_id}: landing band corner {(round(cx, 2), round(cy, 2))} is outside the field box")
    return shaft


def shaft_coords(shaft, X, Y):
    return frame_coords(shaft["axis_m"], shaft["frame_yaw_deg"], X, Y)


def shaft_to_grid_m(shaft, v, w):
    return frame_point(shaft["axis_m"], shaft["frame_yaw_deg"], v, w)


def passage_coords(piece, X, Y):
    X = np.asarray(X, dtype=np.float64)
    Y = np.asarray(Y, dtype=np.float64)
    best = np.full(X.shape, np.inf)
    s_out = np.zeros(X.shape)
    l_out = np.zeros(X.shape)
    last = len(piece["seg_length"]) - 1
    for k in range(last + 1):
        x0, y0 = piece["vertices"][k, :2]
        ux, uy = piece["seg_dir"][k]
        dx = X - x0
        dy = Y - y0
        t = dx * ux + dy * uy
        lateral = -dx * uy + dy * ux
        low = -np.inf if k == 0 else 0.0
        high = np.inf if k == last else piece["seg_length"][k]
        distance = np.hypot(t - np.clip(t, low, high), lateral)
        better = distance < best
        best = np.where(better, distance, best)
        s_out = np.where(better, piece["seg_start_s"][k] + t, s_out)
        l_out = np.where(better, lateral, l_out)
    return s_out, l_out


def segment_index(piece, s):
    return np.clip(np.searchsorted(piece["seg_start_s"], np.asarray(s, dtype=np.float64), side="right") - 1, 0, len(piece["seg_length"]) - 1)


def to_grid_m(piece, s, l):
    s = np.asarray(s, dtype=np.float64)
    l = np.asarray(l, dtype=np.float64)
    k = segment_index(piece, s)
    ux = piece["seg_dir"][k, 0]
    uy = piece["seg_dir"][k, 1]
    t = s - piece["seg_start_s"][k]
    return piece["vertices"][k, 0] + t * ux - l * uy, piece["vertices"][k, 1] + t * uy + l * ux


def connector_yaw_deg(piece, s):
    return float(piece["bearings_deg"][int(segment_index(piece, float(s)))])


def floor_height(piece, s):
    return np.interp(np.asarray(s, dtype=np.float64), piece["vertex_s"], piece["vertices"][:, 2])


def connector_to_world(piece, s, l, z_rel):
    x_m, y_m = to_grid_m(piece, s, l)
    return float(x_m) * 100.0, float(y_m) * 100.0, (float(floor_height(piece, s)) + z_rel) * 100.0


def ridge_weight(piece, s):
    zone = piece["ridge_zone_s_m"]
    s = np.asarray(s, dtype=np.float64)
    if zone is None:
        return np.zeros(s.shape)
    return smoothstep(zone[0] - RIDGE_FADE_M, zone[0], s) * (1.0 - smoothstep(zone[1], zone[1] + RIDGE_FADE_M, s))


def far_section_height(a, far_height):
    return np.minimum(WALL_FACE_SLOPE * np.maximum(np.asarray(a, dtype=np.float64) - FLOOR_HALF_WIDTH_M, 0.0), far_height)


def sample_grid(h, X, Y, x, y):
    origin_x, origin_y = X[0, 0], Y[0, 0]
    fx = np.clip((x - origin_x) / (X[0, 1] - origin_x), 0.0, h.shape[1] - 1.001)
    fy = np.clip((y - origin_y) / (Y[1, 0] - origin_y), 0.0, h.shape[0] - 1.001)
    ix = np.floor(fx).astype(int)
    iy = np.floor(fy).astype(int)
    tx = fx - ix
    ty = fy - iy
    top_row = h[iy, ix] * (1.0 - tx) + h[iy, ix + 1] * tx
    bottom_row = h[iy + 1, ix] * (1.0 - tx) + h[iy + 1, ix + 1] * tx
    return top_row * (1.0 - ty) + bottom_row * ty


def near_section_height(a, rise_slope=NEAR_RISE_SLOPE):
    reach = np.clip(np.asarray(a, dtype=np.float64), FLOOR_HALF_WIDTH_M, BAND_OUTER_M)
    step = np.minimum(NEAR_LEDGE_HEIGHT_M, (reach - FLOOR_HALF_WIDTH_M) * NEAR_STEP_SLOPE)
    rise = np.maximum(0.0, reach - NEAR_LEDGE_OUTER_M) * rise_slope
    return step + rise


def near_sealed_height(a):
    reach = np.clip(np.asarray(a, dtype=np.float64), FLOOR_HALF_WIDTH_M, WALL_TOP_OUTER_M)
    return (reach - FLOOR_HALF_WIDTH_M) * NEAR_STEP_SLOPE


def near_lateral_height(lateral_reach, s, rise_slope=NEAR_RISE_SLOPE):
    ledge = near_section_height(lateral_reach, rise_slope)
    sealed_share = 1.0 - smoothstep(NEAR_SEAL_FULL_S_M, NEAR_SEAL_LEDGE_S_M, s)
    return ledge + (np.maximum(ledge, near_sealed_height(lateral_reach)) - ledge) * sealed_share


def band_height(top, terrain, edge_terrain, a, ridge_weight_share):
    u = np.clip((a - WALL_TOP_OUTER_M) / (BAND_OUTER_M - WALL_TOP_OUTER_M), 0.0, 1.0)
    crest = np.maximum(top, edge_terrain) + RIDGE_LIFT_M
    ridge = np.where(u <= 0.5, top + (crest - top) * (2.0 * u), crest + (edge_terrain - crest) * (2.0 * u - 1.0))
    plain = top + (terrain - top) * outer_band_share(u)
    return plain + (ridge - plain) * ridge_weight_share


def section_reach(piece, l):
    return np.maximum(np.abs(l) - piece["widen_m"], 0.0)


def near_lateral_mask(piece, l):
    if piece["near_lateral_sign"] == 0.0:
        return np.zeros(np.shape(l), dtype=bool)
    return np.sign(l) == piece["near_lateral_sign"]


def stamp_span(piece, s_start, s_end):
    default_start, default_end = piece["stamp_s_m"]
    return (default_start if s_start is None else float(s_start)), (default_end if s_end is None else float(s_end))


def band_edge_terrain(h, X, Y, piece, s, l, cells):
    terrain = np.full(np.shape(s), np.nan)
    if cells.any():
        outward = np.where(l[cells] < 0.0, -1.0, 1.0)
        edge_x, edge_y = to_grid_m(piece, s[cells], outward * (BAND_OUTER_M + piece["widen_m"]))
        terrain[cells] = sample_grid(h, X, Y, edge_x, edge_y)
    return terrain


def inward_end_share(piece, s, s_start, s_end, to_ridge=False):
    s = np.asarray(s, dtype=np.float64)
    camera_s = piece["camera_offset_s_m"]
    if abs(camera_s) < NEAR_CAP_MIN_OFFSET_M:
        return np.zeros(s.shape)
    zone = piece["ridge_zone_s_m"]
    full = OPEN_WALL_BLEND_M + abs(camera_s) + END_RELIEF_MARGIN_M
    if camera_s > 0.0:
        distance = s - s_start
        ridge_distance = None if zone is None else zone[0] - RIDGE_FADE_M - s_start
    else:
        distance = s_end - s
        ridge_distance = None if zone is None else s_end - zone[1] - RIDGE_FADE_M
    if to_ridge and ridge_distance is not None:
        full = max(full, ridge_distance)
    return 1.0 - smoothstep(full, full + END_RELIEF_FADE_M, distance)


def end_climb_m(h, X, Y, piece, s, l, cells):
    outward = np.where(l[cells] < 0.0, -1.0, 1.0) * (BAND_OUTER_M + piece["widen_m"])
    camera_x, camera_y = to_grid_m(piece, s[cells], outward)
    pawn_x, pawn_y = to_grid_m(piece, s[cells] - piece["camera_offset_s_m"], outward)
    return sample_grid(h, X, Y, camera_x, camera_y) - sample_grid(h, X, Y, pawn_x, pawn_y)


def far_end_is_cut(h, X, Y, piece, s, l, end_share, far_cells):
    zone = far_cells & (end_share >= 0.5)
    if not zone.any():
        return False
    return bool(np.max(end_climb_m(h, X, Y, piece, s, l, zone)) > END_RELIEF_M - FAR_WALL_MIN_RELIEF_M)


def far_wall_height(terrain_rel, relief_max, ridge, end_share, cells, end_cut):
    height = np.full(np.shape(terrain_rel), WALL_HEIGHT_M)
    if not cells.any():
        return height
    terrain = terrain_rel[cells]
    interior = np.maximum(np.clip(terrain + relief_max, NEAR_LEDGE_HEIGHT_M, WALL_HEIGHT_M), terrain + FAR_WALL_MIN_RELIEF_M)
    if end_cut:
        end = np.where(terrain >= FAR_WALL_MIN_RELIEF_M + NEAR_LEDGE_HEIGHT_M, terrain - FAR_WALL_MIN_RELIEF_M, np.maximum(terrain, 0.0))
    else:
        end = np.maximum(np.clip(terrain + END_RELIEF_M, NEAR_LEDGE_HEIGHT_M, WALL_HEIGHT_M), terrain + FAR_WALL_MIN_RELIEF_M)
    relief = interior + (end - interior) * end_share[cells]
    height[cells] = relief + (WALL_HEIGHT_M - relief) * ridge[cells]
    return height


def shadow_terrain(h, X, Y, cells):
    return sample_grid(h, X, Y, X[cells] - tdr.CAMERA_OFFSET_M[0], Y[cells] - tdr.CAMERA_OFFSET_M[1])


def open_ramp_m(a, near_lateral):
    wall_ramp = OPEN_FLOOR_BLEND_M + (OPEN_WALL_BLEND_M - OPEN_FLOOR_BLEND_M) * np.clip((a - FLOOR_HALF_WIDTH_M) / (WALL_OUTER_M - FLOOR_HALF_WIDTH_M), 0.0, 1.0)
    near_ramp = np.where(a <= WALL_TOP_OUTER_M,
                         OPEN_FLOOR_BLEND_M + (NEAR_OPEN_BLEND_M - OPEN_FLOOR_BLEND_M) * np.clip((a - FLOOR_HALF_WIDTH_M) / (NEAR_LEDGE_HEIGHT_M / NEAR_STEP_SLOPE), 0.0, 1.0),
                         NEAR_OPEN_BLEND_M + (OPEN_WALL_BLEND_M - NEAR_OPEN_BLEND_M) * np.clip((a - WALL_TOP_OUTER_M) / (BAND_OUTER_M - WALL_TOP_OUTER_M), 0.0, 1.0))
    floor_zone = 1.0 - smoothstep(FLOOR_HALF_WIDTH_M, FLOOR_HALF_WIDTH_M + 1.0, a)
    return FLOOR_END_RAMP_M + (np.where(near_lateral, near_ramp, wall_ramp) - FLOOR_END_RAMP_M) * (1.0 - floor_zone)


def stamp_passage(h, X, Y, piece, s_start=None, s_end=None, z_offset_m=0.0):
    s_start, s_end = stamp_span(piece, s_start, s_end)
    h = np.asarray(h, dtype=np.float64)
    s, l = passage_coords(piece, X, Y)
    a = section_reach(piece, l)
    near_lateral = near_lateral_mask(piece, l)
    ridge = ridge_weight(piece, s)
    rise = NEAR_RISE_SLOPE + (NEAR_RIDGE_RISE_SLOPE - NEAR_RISE_SLOPE) * ridge
    ramp = open_ramp_m(a, near_lateral)
    weight = (a <= BAND_OUTER_M).astype(np.float64) * smoothstep(s_start, s_start + ramp, s) * (1.0 - smoothstep(s_end - ramp, s_end, s))
    floor = floor_height(piece, np.clip(s, s_start, s_end)) - z_offset_m
    cells = weight > 0.0
    edge_terrain = band_edge_terrain(h, X, Y, piece, s, l, cells)
    terrain_rel = edge_terrain - floor
    far_cells = cells & (ridge < 1.0) & ~near_lateral
    far_end = inward_end_share(piece, s, s_start, s_end, to_ridge=True)
    end_cut = far_end_is_cut(h, X, Y, piece, s, l, far_end, far_cells)
    far_height = far_wall_height(terrain_rel, piece["far_relief_max_m"], ridge, far_end, far_cells, end_cut)
    lateral_height = np.where(near_lateral, near_lateral_height(a, s, rise), far_section_height(a, far_height))
    top = floor + np.where(near_lateral, near_lateral_height(WALL_TOP_OUTER_M, s, rise), far_height)
    target = floor + lateral_height
    band = cells & (a > WALL_TOP_OUTER_M)
    if band.any():
        crest_share = np.where(near_lateral[band], 1.0, ridge[band])
        target[band] = band_height(top[band], h[band], edge_terrain[band], a[band], crest_share)
    follow = far_cells & (far_end > 0.0) & (a > FLOOR_HALF_WIDTH_M) & (terrain_rel < FAR_WALL_MIN_RELIEF_M + NEAR_LEDGE_HEIGHT_M) if end_cut else np.zeros(np.shape(s), dtype=bool)
    if follow.any():
        natural = np.minimum(floor[follow] + WALL_FACE_SLOPE * (a[follow] - FLOOR_HALF_WIDTH_M), h[follow])
        target[follow] += (natural - target[follow]) * far_end[follow]
    near_end = inward_end_share(piece, s, s_start, s_end)
    capped = cells & near_lateral & (near_end > 0.0) & (a > FLOOR_HALF_WIDTH_M)
    if capped.any():
        cap = np.maximum(shadow_terrain(h, X, Y, capped) + END_RELIEF_M, floor[capped] + NEAR_LEDGE_HEIGHT_M)
        target[capped] += (np.minimum(target[capped], cap) - target[capped]) * near_end[capped]
    stamped = np.where(weight > 0.0, h * (1.0 - weight) + target * weight, h)
    floor_share = 1.0 - smoothstep(FLOOR_HALF_WIDTH_M - 0.5, FLOOR_HALF_WIDTH_M + 0.5, a)
    return stamped, floor_share * weight, (1.0 - floor_share) * weight


def piece_footprint(piece, X, Y, s_start=None, s_end=None):
    s_start, s_end = stamp_span(piece, s_start, s_end)
    s, l = passage_coords(piece, X, Y)
    return (section_reach(piece, l) <= BAND_OUTER_M) & (s > s_start) & (s < s_end)


def floor_cells(piece, X, Y, s_start=None, s_end=None):
    s_start, s_end = stamp_span(piece, s_start, s_end)
    s, l = passage_coords(piece, X, Y)
    return (s >= s_start) & (s <= s_end) & (section_reach(piece, l) <= FLOOR_HALF_WIDTH_M)


def inner_floor_cells(piece, X, Y):
    s_start, s_end = piece["stamp_s_m"]
    s, l = passage_coords(piece, X, Y)
    return (s >= s_start + OPEN_FLOOR_BLEND_M) & (s <= s_end - OPEN_FLOOR_BLEND_M) & (section_reach(piece, l) <= FLOOR_HALF_WIDTH_M - 1.0)


def lift_interior_mask(v, w, back_v):
    return (v >= back_v) & (v <= LIFT_FACE_V_M) & (np.abs(w) <= LIFT_HALF_WIDTH_M)


def lift_ring_mask(v, w, back_v):
    outer = (v >= back_v - LIFT_RING_M) & (v <= LIFT_FACE_V_M + LIFT_RING_M) & (np.abs(w) <= LIFT_HALF_WIDTH_M + LIFT_RING_M)
    return outer & ~lift_interior_mask(v, w, back_v)


def lift_band_extent():
    reach = LIFT_RING_M + LIFT_FIELD_OUTER_RING_M
    return LIFT_FIELD_BACK_V_M - reach, LIFT_FACE_V_M + reach, LIFT_HALF_WIDTH_M + reach


def landing_band_mask(shaft, X, Y, margin=0.0):
    v, w = shaft_coords(shaft, X, Y)
    low_v, high_v, half_w = lift_band_extent()
    return (v >= low_v - margin) & (v <= high_v + margin) & (np.abs(w) <= half_w + margin)


def shaft_interior_mask(shaft, X, Y):
    v, w = shaft_coords(shaft, X, Y)
    return lift_interior_mask(v, w, LIFT_FIELD_BACK_V_M)


def lift_window_mask(shaft, X, Y, margin=0.0):
    v, w = shaft_coords(shaft, X, Y)
    return (v >= LIFT_FIELD_BACK_V_M - LIFT_RING_M - margin) & (v <= LIFT_FACE_V_M + LIFT_RING_M + margin) & (np.abs(w) <= LIFT_HALF_WIDTH_M + LIFT_RING_M + margin)


def lift_window_distance_m(shaft, x_m, y_m, z_m, floor_z_m, top_z_m):
    v, w = shaft_coords(shaft, np.array([x_m]), np.array([y_m]))
    v, w = float(v[0]), abs(float(w[0]))
    dv = max(LIFT_FIELD_BACK_V_M - LIFT_RING_M - v, 0.0, v - (LIFT_FACE_V_M + LIFT_RING_M))
    dw = max(w - (LIFT_HALF_WIDTH_M + LIFT_RING_M), 0.0)
    dz = max(floor_z_m - z_m, 0.0, z_m - top_z_m)
    return math.sqrt(dv * dv + dw * dw + dz * dz)


def clamp_lights_off_lift_window(lights, shaft, floor_z_m, top_z_m):
    kept, clamped, removed = [], [], []
    for e in lights:
        distance_m = lift_window_distance_m(shaft, e["loc"][0] / 100.0, e["loc"][1] / 100.0, e["loc"][2] / 100.0, floor_z_m, top_z_m)
        limit_cm = (distance_m - LIFT_ASYMMETRIC_LIGHT_CLEARANCE_M) * 100.0
        if e["radius"] <= limit_cm:
            kept.append(e)
            continue
        if limit_cm < LIFT_ASYMMETRIC_LIGHT_MIN_RADIUS_CM:
            removed.append(e["label"])
            continue
        e["radius"] = round(limit_cm, 1)
        clamped.append(e["label"])
        kept.append(e)
    lights[:] = kept
    return {"clamped": clamped, "removed": removed}


def top_canyon_stamp_end(shaft):
    return shaft["top_canyon"]["stamp_s_m"][1] + OPEN_WALL_BLEND_M


def stamp_shaft(h, X, Y, shaft, z_offset_m=0.0):
    stamped, top_floor, top_wall = stamp_passage(h, X, Y, shaft["top_canyon"], s_end=top_canyon_stamp_end(shaft), z_offset_m=z_offset_m)
    landing = landing_band_mask(shaft, X, Y)
    interior = shaft_interior_mask(shaft, X, Y)
    stamped[landing] = shaft["top_z_m"] - z_offset_m
    stamped[interior] = shaft["bottom_z_m"] - z_offset_m
    stamped, slot_floor, slot_wall = stamp_passage(stamped, X, Y, shaft["slot"], z_offset_m=z_offset_m)
    stamped[interior] = shaft["bottom_z_m"] - z_offset_m
    pad = landing.astype(np.float64)
    floor = np.maximum.reduce([top_floor * (1.0 - pad), pad * (1.0 - slot_wall), slot_floor])
    wall = np.maximum(top_wall * (1.0 - pad), slot_wall)
    return stamped, floor, wall


def shaft_footprint(shaft, X, Y):
    return piece_footprint(shaft["top_canyon"], X, Y, s_end=top_canyon_stamp_end(shaft)) | landing_band_mask(shaft, X, Y) | piece_footprint(shaft["slot"], X, Y)


def shaft_floor_cells(shaft, X, Y):
    return floor_cells(shaft["top_canyon"], X, Y) | landing_band_mask(shaft, X, Y) | floor_cells(shaft["slot"], X, Y)


def pieces_of_gate(gate_id):
    if gate_id in SHAFTS_BY_GATE:
        shaft = SHAFTS_BY_GATE[gate_id]
        return [shaft["top_canyon"], shaft["slot"]]
    return [CONNECTORS_BY_GATE[gate_id]]


def gate_footprint(gate_id, X, Y):
    if gate_id in SHAFTS_BY_GATE:
        return shaft_footprint(SHAFTS_BY_GATE[gate_id], X, Y)
    return piece_footprint(CONNECTORS_BY_GATE[gate_id], X, Y)


def gate_floor_cells(gate_id, X, Y):
    if gate_id in SHAFTS_BY_GATE:
        return shaft_floor_cells(SHAFTS_BY_GATE[gate_id], X, Y)
    return floor_cells(CONNECTORS_BY_GATE[gate_id], X, Y)


def connector_footprint(X, Y, gate_ids=None):
    mask = np.zeros(np.shape(X), dtype=bool)
    for gate_id in (GATE_IDS if gate_ids is None else gate_ids):
        mask |= gate_footprint(gate_id, X, Y)
    return mask


def quad_max_slope_tan(h):
    h = np.asarray(h, dtype=np.float64)
    bottom = h[:-1, 1:] - h[:-1, :-1]
    top = h[1:, 1:] - h[1:, :-1]
    left = h[1:, :-1] - h[:-1, :-1]
    right = h[1:, 1:] - h[:-1, 1:]
    return np.maximum.reduce([np.hypot(bottom, right), np.hypot(top, left), np.hypot(bottom, left), np.hypot(top, right)])


def quad_center_heights(h):
    h = np.asarray(h, dtype=np.float64)
    return 0.25 * (h[:-1, :-1] + h[:-1, 1:] + h[1:, :-1] + h[1:, 1:])


def walkable_quads(h, max_slope_deg=WALK_MAX_SLOPE_DEG):
    return np.nan_to_num(quad_max_slope_tan(h), nan=np.inf) <= math.tan(math.radians(max_slope_deg))


def quad_components(walkable):
    labels, _ = ndimage.label(walkable, structure=np.array([[0, 1, 0], [1, 1, 1], [0, 1, 0]], dtype=bool))
    return labels


def wall_top_cells(piece, X, Y, h, z_offset_m=0.0):
    s, l = passage_coords(piece, X, Y)
    s_start, s_end = piece["stamp_s_m"]
    a = section_reach(piece, l)
    far = ~near_lateral_mask(piece, l)
    floor = floor_height(piece, np.clip(s, s_start, s_end)) - z_offset_m
    span = (s >= s_start + OPEN_WALL_BLEND_M) & (s <= s_end - OPEN_WALL_BLEND_M)
    far_end = inward_end_share(piece, s, s_start, s_end, to_ridge=True)
    if far_end_is_cut(h, X, Y, piece, s, l, far_end, far & (a <= BAND_OUTER_M) & (s >= s_start) & (s <= s_end) & (ridge_weight(piece, s) < 1.0)):
        span &= far_end <= 0.0
    top = (a >= WALL_OUTER_M + 0.5) & (a <= WALL_TOP_OUTER_M - 0.5) & far
    return span & top & (np.asarray(h) >= floor + NEAR_LEDGE_HEIGHT_M - 0.5)


def polyline_samples(points, step_m):
    points = np.asarray(points, dtype=np.float64)
    out = [points[0]]
    for start, end in zip(points[:-1], points[1:]):
        count = max(2, int(np.hypot(*(end - start)) / step_m))
        t = np.arange(1, count + 1, dtype=np.float64) / count
        out.extend(start + (end - start) * t[:, None])
    return np.array(out)


def grid_index(X, Y, x, y):
    ix = np.clip(np.round((np.asarray(x) - X[0, 0]) / (X[0, 1] - X[0, 0])).astype(int), 0, X.shape[1] - 1)
    iy = np.clip(np.round((np.asarray(y) - Y[0, 0]) / (Y[1, 0] - Y[0, 0])).astype(int), 0, Y.shape[0] - 1)
    return ix, iy


def stamp_spur_road(h, X, Y, polyline, width_m, end_z_m=None):
    h = np.asarray(h, dtype=np.float64)
    samples = polyline_samples(polyline, CENTERLINE_SAMPLE_M)
    profile = sample_grid(h, X, Y, samples[:, 0], samples[:, 1])
    start_z = float(profile[0])
    end_z = float(profile[-1]) if end_z_m is None else float(end_z_m)
    for _ in range(2):
        profile = ndimage.uniform_filter1d(profile, SPUR_PROFILE_WINDOW, mode="nearest")
    t = np.linspace(0.0, 1.0, len(profile))
    profile = profile + (start_z - profile[0]) * (1.0 - t) + (end_z - profile[-1]) * t
    ix, iy = grid_index(X, Y, samples[:, 0], samples[:, 1])
    mask = np.zeros(h.shape, dtype=bool)
    values = np.zeros(h.shape, dtype=np.float64)
    mask[iy, ix] = True
    values[iy, ix] = profile
    distance, (near_y, near_x) = ndimage.distance_transform_edt(~mask, return_indices=True)
    flat = 0.5 * width_m
    weight = 1.0 - smoothstep(flat, flat + SPUR_BLEND_M, distance)
    stamped = h * (1.0 - weight) + weight * values[near_y, near_x]
    road_layer = 1.0 - smoothstep(flat - 0.5, flat + 1.5, distance)
    return stamped, road_layer, weight


def make_prop(name, kind, s, l, z_rel, yaw_offset=0.0, pitch=0.0, roll=0.0, scale=(1.0, 1.0, 1.0), **extra):
    prop = {"name": name, "kind": kind, "s": float(s), "l": float(l), "z_rel": float(z_rel), "yaw_offset": float(yaw_offset), "pitch": float(pitch), "roll": float(roll), "scale": [float(v) for v in scale]}
    prop.update(extra)
    return prop


def mesh_prop(name, key, s, l, z_rel, yaw_offset, scale, pitch=0.0, roll=0.0):
    return make_prop(name, "mesh", s, l, z_rel, yaw_offset, pitch, roll, scale, key=key, mesh=PROP_MESHES[key], materials=[])


def light_prop(name, s, l, z_rel, intensity, color, radius_m, source_radius=20.0, volumetric=1.5):
    return make_prop(name, "light", s, l, z_rel, light_type="Point", intensity=float(intensity), color=list(color), radius_m=float(radius_m), source_radius=float(source_radius), volumetric=float(volumetric), cast_shadows=False)


def plane_prop(name, material, s, l, z_rel, scale, pitch=0.0):
    return make_prop(name, "plane", s, l, z_rel, 0.0, pitch, 0.0, scale, mesh=ENGINE_PLANE, material=material, collision=False)


def lift_mesh_prop(name, key, v, w, z_rel, yaw_offset, scale):
    return make_prop(name, "mesh", v, w, z_rel, yaw_offset, 0.0, 0.0, scale, key=key, mesh=LIFT_PROP_MESHES[key], materials=[])


def liner_panel_prop(name, v, w, z_rel, yaw_offset, height_m, width_m):
    return make_prop(name, "plane", v, w, z_rel, yaw_offset, 90.0, 0.0, (height_m, width_m, 1.0), mesh=LIFT_LINER_PANEL_MESH, material=tdm.SHAFT_LINER_MATERIAL_PATH, collision=False)


def static_ring_depths_m(shaft):
    ring_top_m = LIFT_MOTION["ring_top_depth_cm"] / 100.0
    ring_bottom_m = (LIFT_MOTION["ring_top_depth_cm"] + LIFT_MOTION["ring_module_count"] * LIFT_MOTION["ring_module_height_cm"]) / 100.0
    return (0.0, ring_top_m), (ring_bottom_m, shaft["drop_m"])


def lift_zone_props(shaft):
    side_length = LIFT_WALL_FACE_V_M - LIFT_WINDOW_BACK_V_M
    side_center_v = 0.5 * (LIFT_WALL_FACE_V_M + LIFT_WINDOW_BACK_V_M)
    face_width = 2.0 * LIFT_WALL_SIDE_W_M / LIFT_WALL_COLUMNS
    props = []
    for i in range(LIFT_WALL_COLUMNS):
        v = LIFT_WINDOW_BACK_V_M + (i + 0.5) * side_length / LIFT_WALL_COLUMNS
        for j in range(LIFT_WALL_COLUMNS):
            w = -LIFT_WALL_SIDE_W_M + (j + 0.5) * face_width
            props.append(plane_prop(f"Floor_{i}_{j}", BEDROCK_MATERIAL, v, w, LIFT_PROP_FLOOR_LIFT_M, (side_length / LIFT_WALL_COLUMNS, face_width, 1.0)))
    for ring_name, (depth_top, depth_bottom) in zip(("Top", "Bottom"), static_ring_depths_m(shaft)):
        height = depth_bottom - depth_top
        z_rel = shaft["drop_m"] - 0.5 * (depth_top + depth_bottom)
        props.append(liner_panel_prop(f"Ring{ring_name}_Face", LIFT_WALL_FACE_V_M, 0.0, z_rel, 0.0, height, LIFT_LINER_FACE_WIDTH_M))
        for side_sign in (-1, 1):
            props.append(liner_panel_prop(f"Ring{ring_name}_Side_{'L' if side_sign < 0 else 'R'}", side_center_v, side_sign * LIFT_WALL_SIDE_W_M, z_rel, side_sign * 90.0, height, side_length))
    props.append(plane_prop("Fog_0", FOG_MATERIALS["Fog2"], -3.5, 0.0, 0.7, (12.0, 9.0, 12.0)))
    return props


def lift_landing_props(shaft):
    props = []
    for k, w in enumerate(LIFT_BRIDGE_STATIONS_W_M):
        props.append(lift_mesh_prop(f"Landing_Bridge_{k}", "Path", 0.0, w, LIFT_BRIDGE_Z_REL_M, 90.0, (1.0, 1.0, 1.0)))
    for side_sign in (-1, 1):
        for k, w in enumerate(LIFT_BRIDGE_FENCE_STATIONS_W_M):
            props.append(lift_mesh_prop(f"Landing_BridgeFence_{'L' if side_sign < 0 else 'R'}_{k}", "Fence1", side_sign * LIFT_BRIDGE_FENCE_V_M, w, LIFT_FENCE_Z_REL_M, 0.0, (1.0, 1.0, 1.0)))
    for k, v in enumerate(LIFT_RIM_FENCE_STATIONS_V_M):
        props.append(lift_mesh_prop(f"Landing_RimFence_{k}", "Fence1", v, LIFT_RIM_FENCE_W_M, LIFT_FENCE_Z_REL_M, -90.0, (1.0, 1.0, 1.0)))
    return props


def lift_prop_position_m(shaft, prop, base_z_m):
    x_m, y_m = shaft_to_grid_m(shaft, prop["s"], prop["l"])
    z_m = base_z_m + prop["z_rel"]
    if prop["kind"] == "mesh":
        z_m += LIFT_MESH_BASE_BELOW_PIVOT_M.get(prop["key"], 0.0) * prop["scale"][2]
    return x_m, y_m, z_m


def lift_deck_material_path():
    return tdm.topdown_material_path(LIFT_DECK_PACK_MATERIAL)


def seam_zone_props(piece):
    crossing = piece["boundary_crossing"]
    if crossing is None:
        return []
    widen = piece["widen_m"]
    props = []
    tile_count = int(round(2.0 * BOUNDARY_COVER_HALF_M / FLOOR_TILE_LENGTH_M))
    s_start, s_end = piece["stamp_s_m"]
    for i in range(tile_count):
        s = crossing["s_m"] - BOUNDARY_COVER_HALF_M + (i + 0.5) * FLOOR_TILE_LENGTH_M
        if s - 0.5 * FLOOR_TILE_LENGTH_M < s_start or s + 0.5 * FLOOR_TILE_LENGTH_M > s_end:
            continue
        grade = piece["grades"][int(segment_index(piece, s))]
        pitch = math.degrees(math.atan(grade))
        tile_length = FLOOR_TILE_LENGTH_M / math.cos(math.radians(pitch))
        props.append(plane_prop(f"Floor_{i}", BEDROCK_MATERIAL, s, 0.0, FLOOR_TILE_LIFT_M, (tile_length, FLOOR_TILE_WIDTH_M + 2.0 * widen, 1.0), pitch))
    for side_sign in (-1, 1):
        if side_sign == piece["near_lateral_sign"]:
            continue
        for k, station in enumerate(WALL_CLIFF_STATIONS_M):
            key = WALL_CLIFF_KEYS[(k + (1 if side_sign > 0 else 0)) % len(WALL_CLIFF_KEYS)]
            yaw_offset = side_sign * 90.0 + ((k * 53) % 31 - 15)
            scale = 1.6 + 0.2 * (k % 3)
            props.append(mesh_prop(f"Wall_{'L' if side_sign < 0 else 'R'}_{k}", key, crossing["s_m"] + station, side_sign * (7.0 + widen), FAR_WALL_PROP_Z_REL_M, yaw_offset, (scale, scale, scale), roll=float((k * 29) % 13 - 6)))
    return props


def field_passage_dressing(piece):
    length = piece["stamp_s_m"][1]
    s_limit = length - 8.0
    widen = piece["widen_m"]
    props = []
    k = 0
    s = 12.0
    while s <= s_limit:
        side_sign = -1 if k % 2 == 0 else 1
        props.append(mesh_prop(f"Stake_{k}", "StickedSkull", s, side_sign * (3.3 + widen), 0.0, side_sign * 90.0 + 180.0, (1.3, 1.3, 1.3)))
        k += 1
        s += 13.0
    k = 0
    s = 18.0
    while s <= s_limit:
        side_sign = 1 if k % 2 == 0 else -1
        t = s / length
        color = [WARM_LIGHT[i] + (COLD_LIGHT[i] - WARM_LIGHT[i]) * t for i in range(3)]
        props.append(mesh_prop(f"Lamppost_{k}", "Lamppost", s, side_sign * (3.4 + widen), 0.0, -side_sign * 90.0, (1.0, 1.0, 1.0)))
        props.append(light_prop(f"LampLight_{k}", s, side_sign * (2.9 + widen), 3.4, 30.0, [round(c, 3) for c in color], 11.0, source_radius=10.0, volumetric=2.0))
        k += 1
        s += 30.0
    k = 0
    s = 8.0
    while s <= s_limit + 4.0:
        side_sign = 1 if k % 2 == 0 else -1
        key = "SRock5" if k % 3 else "SRock7"
        scale = 1.0 + 0.25 * (k % 3)
        props.append(mesh_prop(f"Rock_{k}", key, s, side_sign * (3.7 + widen), 0.0, float((k * 47) % 360), (scale, scale, scale)))
        k += 1
        s += 6.0
    if length - 12.0 >= 12.0:
        props.append(plane_prop("Fog_Approach", FOG_MATERIALS["Fog1"], length - 12.0, 0.0, 0.6, (14.0, 10.0, 14.0)))
    return props


def transform_entry(x_m, y_m, z_m, yaw_deg, height_cm=0.0):
    return {"loc_cm": [round(float(x_m) * 100.0, 2), round(float(y_m) * 100.0, 2), round(float(z_m) * 100.0 + height_cm, 4)], "yaw_deg": round(normalize_yaw(float(yaw_deg)), 4)}


def projected_length_m(x0, y0, x1, y1, yaw_deg):
    yaw_rad = math.radians(yaw_deg)
    return (x1 - x0) * math.cos(yaw_rad) + (y1 - y0) * math.sin(yaw_rad)


def passage_gate_entry(connector):
    start = connector["vertices"][0]
    end = connector["vertices"][-1]
    yaw = bearing_deg(start[0], start[1], end[0], end[1])
    bx, by = connector["boundary_crossing"]["point_m"]
    return {
        "gate_id": connector["gate_id"], "realm_id": connector["realm_id"], "gate_kind": "Passage",
        "arrival": transform_entry(end[0], end[1], end[2], yaw, GATE_PAWN_HEIGHT_CM),
        "field_return": transform_entry(start[0], start[1], start[2], yaw, GATE_PAWN_HEIGHT_CM),
        "approach_length_cm": round(projected_length_m(start[0], start[1], bx, by, yaw) * 100.0, 1),
        "departure_length_cm": round(projected_length_m(bx, by, end[0], end[1], yaw) * 100.0, 1),
        "passage_half_width_cm": PASSAGE_HALF_WIDTH_CM, "preload_lead_cm": PRELOAD_LEAD_CM,
    }


def lift_gate_entry(shaft):
    mouth = shaft["top_canyon"]["vertices"][0]
    slot_end = shaft["slot"]["vertices"][-1]
    axis_x, axis_y = shaft["axis_m"]
    lift = {
        "field_landing_transform": transform_entry(axis_x, axis_y, shaft["top_z_m"], shaft["top_exit_yaw_deg"]),
        "realm_landing_transform": transform_entry(axis_x, axis_y, shaft["bottom_z_m"], shaft["bottom_exit_yaw_deg"]),
    }
    lift.update(LIFT_MOTION)
    lift.update({
        "deck_half_extent_cm": list(LIFT_DECK_HALF_EXTENT_CM), "deck_mesh": LIFT_DECK_MESH, "rail_mesh": LIFT_RAIL_MESH, "deck_material": lift_deck_material_path(),
        "liner_panel_mesh": LIFT_LINER_PANEL_MESH, "liner_material": tdm.SHAFT_LINER_MATERIAL_PATH, "abyss_screen_material": LIFT_ABYSS_SCREEN_MATERIAL,
    })
    return {
        "gate_id": shaft["gate_id"], "realm_id": shaft["realm_id"], "gate_kind": "Lift",
        "arrival": transform_entry(slot_end[0], slot_end[1], slot_end[2], shaft["slot"]["bearings_deg"][0], GATE_PAWN_HEIGHT_CM),
        "field_return": transform_entry(mouth[0], mouth[1], mouth[2], shaft["top_canyon"]["bearings_deg"][0], GATE_PAWN_HEIGHT_CM),
        "approach_length_cm": round(math.hypot(axis_x - mouth[0], axis_y - mouth[1]) * 100.0, 1),
        "departure_length_cm": round(math.hypot(slot_end[0] - axis_x, slot_end[1] - axis_y) * 100.0, 1),
        "passage_half_width_cm": PASSAGE_HALF_WIDTH_CM, "preload_lead_cm": PRELOAD_LEAD_CM,
        "lift": lift,
    }


def gate_entry(gate_id):
    if gate_id in SHAFTS_BY_GATE:
        return lift_gate_entry(SHAFTS_BY_GATE[gate_id])
    return passage_gate_entry(CONNECTORS_BY_GATE[gate_id])


def gate_kind(gate_id):
    return "Lift" if gate_id in SHAFTS_BY_GATE else "Passage"


def piece_geometry(piece):
    crossing = piece["boundary_crossing"]
    return {
        "name": piece["name"], "section": piece["section"], "floor_width_m": 2.0 * (FLOOR_HALF_WIDTH_M + piece["widen_m"]),
        "centerline_cm": [[round(float(x) * 100.0, 2), round(float(y) * 100.0, 2), round(float(z) * 100.0, 4)] for x, y, z in piece["vertices"]],
        "segment_kinds": list(piece["segment_kinds"]), "grades": [round(g, 6) for g in piece["grades"]], "bearings_deg": [round(b, 4) for b in piece["bearings_deg"]],
        "length_m": round(piece["length"], 4), "stamp_s_m": [round(v, 4) for v in piece["stamp_s_m"]], "end_floor_z_m": [float(piece["vertices"][0, 2]), float(piece["vertices"][-1, 2])],
        "ridge_zone_s_m": None if piece["ridge_zone_s_m"] is None else [round(v, 3) for v in piece["ridge_zone_s_m"]], "near_lateral_sign": piece["near_lateral_sign"],
        "boundary_crossing": None if crossing is None else {"s_m": crossing["s_m"], "loc_cm": [round(crossing["point_m"][0] * 100.0, 2), round(crossing["point_m"][1] * 100.0, 2), round(crossing["floor_z_m"] * 100.0, 4)], "edge": crossing.get("edge")},
    }


def gate_geometry(gate_id):
    geometry = {"pieces": [piece_geometry(piece) for piece in pieces_of_gate(gate_id)]}
    if gate_id not in SHAFTS_BY_GATE:
        return geometry
    shaft = SHAFTS_BY_GATE[gate_id]
    low_v, high_v, half_w = lift_band_extent()
    geometry["shaft"] = {
        "axis_cm": [shaft["axis_m"][0] * 100.0, shaft["axis_m"][1] * 100.0], "frame_yaw_deg": shaft["frame_yaw_deg"],
        "top_landing_z_cm": shaft["top_z_m"] * 100.0, "bottom_landing_z_cm": shaft["bottom_z_m"] * 100.0, "drop_cm": shaft["drop_m"] * 100.0,
        "interior_v_m": [LIFT_FIELD_BACK_V_M, LIFT_FACE_V_M], "interior_half_width_m": LIFT_HALF_WIDTH_M, "landing_band_v_m": [low_v, high_v], "landing_band_half_width_m": half_w,
        "top_exit_yaw_deg": shaft["top_exit_yaw_deg"], "bottom_exit_yaw_deg": shaft["bottom_exit_yaw_deg"], "slot_floor_width_m": shaft["slot_floor_width_m"],
        "static_ring_depths_m": [list(r) for r in static_ring_depths_m(shaft)],
    }
    return geometry


def liner_visible_ranges_cm(shaft):
    motion = LIFT_MOTION
    cruise = motion["cruise_speed_cm_per_second"]
    brake_distance = cruise * cruise / (2.0 * motion["brake_deceleration_cm_per_second_squared"])
    resume_distance = cruise * cruise / (2.0 * motion["acceleration_cm_per_second_squared"])
    travel = shaft["drop_m"] * 100.0
    above = LIFT_VISIBLE_ABOVE_DECK_CM
    below = motion["abyss_screen_depth_cm"]
    descend_brake = motion["hold_depth_cm"] - brake_distance
    descend_resume = motion["hold_depth_cm"] + resume_distance
    return {
        "descend_brake_start": (descend_brake - above, descend_brake + below),
        "descend_resume_end": (descend_resume - above, descend_resume + below),
        "ascend_brake_start": (travel - descend_brake - above, travel - descend_brake + below),
        "ascend_resume_end": (travel - descend_resume - above, travel - descend_resume + below),
    }


def check_liner(shaft):
    ring_top = LIFT_MOTION["ring_top_depth_cm"]
    ring_bottom = ring_top + LIFT_MOTION["ring_module_count"] * LIFT_MOTION["ring_module_height_cm"]
    ok = ring_bottom <= shaft["drop_m"] * 100.0
    lines = [f"  liner rings [{ring_top:.1f}, {ring_bottom:.1f}] cm inside drop {shaft['drop_m'] * 100.0:.1f} cm {'PASS' if ok else 'FAIL'}"]
    for name, (low, high) in liner_visible_ranges_cm(shaft).items():
        inside = low >= ring_top - 1e-6 and high <= ring_bottom + 1e-6
        ok = ok and inside
        lines.append(f"  liner visible {name} [{low:.1f}, {high:.1f}] cm {'PASS' if inside else 'FAIL'}")
    return ok, lines


def check_synthetic_stamps(rng):
    field_axis = np.arange(-FIELD_HALF_M, FIELD_HALF_M + 1.0)
    FX, FY = np.meshgrid(field_axis, field_axis)
    grids = {"field": (FX, FY, 0.0)}
    for realm_id, placement in REALM_PLACEMENT.items():
        x0, x1, y0, y1 = realm_box_m(realm_id)
        RX, RY = np.meshgrid(np.arange(x0, x1 + 1.0), np.arange(y0, y1 + 1.0))
        grids[realm_id] = (RX, RY, placement["z_offset_m"])
    failures = 0
    for gate_id in GATE_IDS:
        for piece in pieces_of_gate(gate_id):
            for grid_name in ("field", piece["realm_id"]):
                X, Y, z_offset = grids[grid_name]
                h = rng.normal(0.0, 6.0, X.shape)
                stamped, floor, _ = stamp_passage(h, X, Y, piece, z_offset_m=z_offset)
                s, l = passage_coords(piece, X, Y)
                s0, s1 = piece["stamp_s_m"]
                inner = (s >= s0 + OPEN_FLOOR_BLEND_M) & (s <= s1 - OPEN_FLOOR_BLEND_M) & (section_reach(piece, l) <= FLOOR_HALF_WIDTH_M - 0.5)
                if not inner.any():
                    continue
                expected = floor_height(piece, s[inner]) - z_offset
                worst = float(np.max(np.abs(quantized_units(stamped[inner]) - quantized_units(expected))))
                ok = worst <= 0.0 and float(floor[inner].min()) == 1.0
                failures += 0 if ok else 1
                print(f"  stamp {gate_id}/{piece['name']} on {grid_name}: floor cells {int(inner.sum())} max diff units {worst:.0f} {'PASS' if ok else 'FAIL'}")
    return failures


def run_self_check():
    failures = 0
    for connector in CONNECTORS:
        crossing = connector["boundary_crossing"]
        print(f"{connector['gate_id']}: {connector['kind']} {connector['realm_id']} vertices {[tuple(round(float(v), 4) for v in p) for p in connector['vertices']]}")
        print(f"  kinds {connector['segment_kinds']} grades {[round(g, 5) for g in connector['grades']]} bearings {[round(b, 3) for b in connector['bearings_deg']]} length {connector['length']:.3f} stamp {connector['stamp_s_m']}")
        print(f"  camera side {connector['near_lateral_sign']} crossing s {crossing['s_m']} at {crossing['point_m']} floor {crossing['floor_z_m']:.4f} edge {crossing['edge']} margin {crossing['edge_margin_m']} ridge zone {connector['ridge_zone_s_m']}")
        entry = gate_entry(connector["gate_id"])
        print(f"  gate approach {entry['approach_length_cm']} departure {entry['departure_length_cm']} arrival {entry['arrival']} field return {entry['field_return']}")
    for shaft in SHAFTS:
        print(f"{shaft['gate_id']}: shaft axis {shaft['axis_m']} yaw {shaft['frame_yaw_deg']} top {shaft['top_z_m']} bottom {shaft['bottom_z_m']} slot floor width {shaft['slot_floor_width_m']}")
        for piece in (shaft["top_canyon"], shaft["slot"]):
            crossing = piece["boundary_crossing"]
            print(f"  {piece['name']}: vertices {[tuple(round(float(v), 4) for v in p) for p in piece['vertices']]} bearing {piece['bearings_deg'][0]:.3f} length {piece['length']:.3f} camera side {piece['near_lateral_sign']} "
                  f"crossing {None if crossing is None else (crossing['s_m'], crossing['point_m'], crossing.get('edge'))} ridge zone {piece['ridge_zone_s_m']}")
        entry = gate_entry(shaft["gate_id"])
        print(f"  gate approach {entry['approach_length_cm']} departure {entry['departure_length_cm']} landings {entry['lift']['field_landing_transform']} {entry['lift']['realm_landing_transform']}")
        ok, lines = check_liner(shaft)
        failures += 0 if ok else 1
        print("\n".join(lines))
        slot_ok = shaft["slot_floor_width_m"] >= 12.0
        failures += 0 if slot_ok else 1
        print(f"  slot floor width {shaft['slot_floor_width_m']} m >= 12 {'PASS' if slot_ok else 'FAIL'}")
    failures += check_synthetic_stamps(np.random.default_rng(5))
    return failures


require_realm_placement()
CONNECTORS = tuple(build_connector(*row) for row in CONNECTOR_ROWS)
CONNECTORS_BY_GATE = {c["gate_id"]: c for c in CONNECTORS}
SHAFTS = tuple(build_shaft(*row) for row in SHAFT_ROWS)
SHAFTS_BY_GATE = {s["gate_id"]: s for s in SHAFTS}
GATE_IDS = tuple(c["gate_id"] for c in CONNECTORS) + tuple(s["gate_id"] for s in SHAFTS)


if __name__ == "__main__":
    sys.exit(1 if run_self_check() else 0)
