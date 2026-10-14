"""에디터 밖 시스템 파이썬(numpy)에서 쓰는 공유 모듈: 숨은 이음매 통로(필드 통로 A·이계 통로 B)의 관문 표·단면·바닥 프로필·소품 상대 좌표를 한곳에 둔다(P3-15, 명세 1절).
실행: generate_ashen_vale.py·generate_realm.py 가 import 한다. 자체 점검은 python Tools/WorldGen/realm_passages.py (PowerShell 또는 Git Bash, numpy 필요)
출력: 자체 점검 시 표준 출력에 관문별 마커·이음매·접근 길이·횡 오차와 합성 지형 핵심 구간 일치 결과(실패 시 종료 코드 1)
상태: 현행
"""
import math
import sys

import numpy as np

import topdown_rules as tdr

SEAM_BUFFER_M = 30.0
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
END_CAP_M = BAND_OUTER_M - FLOOR_HALF_WIDTH_M
UNITS_PER_METER = 128.0
MARKER_FORWARD_M = 3.0
MAX_SEAM_LATERAL_ERROR_M = 1.0
NEAR_LEDGE_HEIGHT_M = 2.5
NEAR_STEP_SLOPE = math.tan(math.radians(45.0))
NEAR_LEDGE_OUTER_M = 12.0
NEAR_RISE_SLOPE = math.tan(math.radians(45.0))
NEAR_OPEN_BLEND_M = 3.0
NEAR_CAP_MIN_OFFSET_M = 1.0
NEAR_SEAL_FULL_S_M = -40.0
NEAR_SEAL_LEDGE_S_M = -34.0
WALL_PROP_Z_REL_M = 11.0
FAR_WALL_PROP_Z_REL_M = 13.0
REALM_HALF_M = 252.0
MIN_REALM_EDGE_DISTANCE_M = 35.0
MIN_FRONT_MARGIN_M = 1.0
SIDES = ("field", "realm")
GRADES = {"descend": -FLOOR_GRADE, "ascend": FLOOR_GRADE}

REALM_CENTER_CM = {"SunkenStarfield": (-302400.0, 302400.0), "PaleHeights": (-302400.0, -302400.0)}

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
SEAM_LIGHT_STATIONS_M = (-20.0, 0.0, 20.0)
SEAM_FOG_STATIONS_M = (-18.0, 0.0, 18.0)

PASSAGE_ROWS = (
    ("SunkenStarfieldSouthwest", "SunkenStarfield", "HollowCave", (-372.0, -262.0), -14.0, (-456, -241), (216, 61), 40.0, "descend"),
    ("SunkenStarfieldSoutheast", "SunkenStarfield", "MainCrypt", (296.0, -268.0), 135.0, (365, -337), (-216, 41), 32.0, "descend"),
    ("PaleHeightsNortheast", "PaleHeights", "SunkenCrypt", (318.0, 412.0), 196.0, (450, 450), (-216, 24), 45.0, "ascend"),
)


def smoothstep(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def quantize_height(z):
    return np.round(np.asarray(z, dtype=np.float64) * UNITS_PER_METER) / UNITS_PER_METER


def normalize_yaw(yaw_deg):
    return yaw_deg % 360.0


def is_integer_point(point):
    return all(float(v) == float(int(v)) for v in point)


def camera_offset_in_passage(yaw_deg):
    yaw_rad = math.radians(yaw_deg)
    dx, dy = tdr.CAMERA_OFFSET_M[0], tdr.CAMERA_OFFSET_M[1]
    return dx * math.cos(yaw_rad) + dy * math.sin(yaw_rad), -dx * math.sin(yaw_rad) + dy * math.cos(yaw_rad)


def build_passage(gate_id, realm_id, entrance_name, entrance_m, marker_yaw_deg, field_seam_m, realm_seam_local_m, front_m, kind):
    if kind not in GRADES:
        raise ValueError(f"{gate_id}: kind {kind} not in {sorted(GRADES)}")
    if realm_id not in REALM_CENTER_CM:
        raise ValueError(f"{gate_id}: realm {realm_id} has no center")
    if not is_integer_point(field_seam_m) or not is_integer_point(realm_seam_local_m):
        raise ValueError(f"{gate_id}: seams must be integer meters {field_seam_m} {realm_seam_local_m}")
    yaw_deg = normalize_yaw(marker_yaw_deg + 180.0)
    marker_rad = math.radians(marker_yaw_deg)
    marker_m = (entrance_m[0] + MARKER_FORWARD_M * math.cos(marker_rad), entrance_m[1] + MARKER_FORWARD_M * math.sin(marker_rad))
    yaw_rad = math.radians(yaw_deg)
    cos_yaw, sin_yaw = math.cos(yaw_rad), math.sin(yaw_rad)
    dx, dy = field_seam_m[0] - marker_m[0], field_seam_m[1] - marker_m[1]
    approach_m = dx * cos_yaw + dy * sin_yaw
    lateral_error_m = -dx * sin_yaw + dy * cos_yaw
    if abs(lateral_error_m) > MAX_SEAM_LATERAL_ERROR_M:
        raise ValueError(f"{gate_id}: field seam lateral error {lateral_error_m:.3f} m > {MAX_SEAM_LATERAL_ERROR_M}")
    if approach_m < SEAM_BUFFER_M + OPEN_WALL_BLEND_M:
        raise ValueError(f"{gate_id}: approach {approach_m:.2f} m too short")
    edge_distance_m = REALM_HALF_M - max(abs(realm_seam_local_m[0]), abs(realm_seam_local_m[1]))
    if edge_distance_m < MIN_REALM_EDGE_DISTANCE_M:
        raise ValueError(f"{gate_id}: realm seam {edge_distance_m} m from edge < {MIN_REALM_EDGE_DISTANCE_M}")
    if front_m < SEAM_BUFFER_M + MIN_FRONT_MARGIN_M:
        raise ValueError(f"{gate_id}: front {front_m} m must exceed {SEAM_BUFFER_M + MIN_FRONT_MARGIN_M}")
    camera_s_m, camera_l_m = camera_offset_in_passage(yaw_deg)
    return {
        "gate_id": gate_id, "realm_id": realm_id, "field_marker_label": f"TDDungeonEntrance_{entrance_name}", "entrance_name": entrance_name,
        "field_entrance_m": (float(entrance_m[0]), float(entrance_m[1])), "marker_yaw_deg": float(marker_yaw_deg), "field_marker_m": marker_m,
        "yaw_deg": yaw_deg, "field_seam_m": (float(field_seam_m[0]), float(field_seam_m[1])), "realm_seam_local_m": (float(realm_seam_local_m[0]), float(realm_seam_local_m[1])),
        "approach_m": round(approach_m, 2), "front_m": float(front_m), "kind": kind, "grade": GRADES[kind],
        "seam_lateral_error_m": round(lateral_error_m, 3), "realm_edge_distance_m": edge_distance_m,
        "camera_offset_s_m": round(camera_s_m, 3), "camera_offset_l_m": round(camera_l_m, 3), "near_lateral_sign": 1.0 if camera_l_m >= 0.0 else -1.0,
        "near_cap_low": camera_s_m <= -NEAR_CAP_MIN_OFFSET_M, "near_cap_high": camera_s_m >= NEAR_CAP_MIN_OFFSET_M,
    }


PASSAGES = tuple(build_passage(*row) for row in PASSAGE_ROWS)
PASSAGES_BY_GATE = {p["gate_id"]: p for p in PASSAGES}


def require_side(side):
    if side not in SIDES:
        raise ValueError(f"side {side} not in {SIDES}")


def direction_and_lateral(passage):
    yaw_rad = math.radians(passage["yaw_deg"])
    cos_yaw, sin_yaw = math.cos(yaw_rad), math.sin(yaw_rad)
    return (cos_yaw, sin_yaw), (-sin_yaw, cos_yaw)


def seam_grid_m(passage, side):
    require_side(side)
    return passage["field_seam_m"] if side == "field" else passage["realm_seam_local_m"]


def grid_origin_cm(passage, side):
    require_side(side)
    return (0.0, 0.0) if side == "field" else REALM_CENTER_CM[passage["realm_id"]]


def passage_frame(passage, side):
    seam_x, seam_y = seam_grid_m(passage, side)
    origin_x, origin_y = grid_origin_cm(passage, side)
    direction, lateral = direction_and_lateral(passage)
    return (origin_x + seam_x * 100.0, origin_y + seam_y * 100.0), direction, lateral


def default_span(passage, side):
    require_side(side)
    if side == "field":
        return -passage["approach_m"], SEAM_BUFFER_M
    return -SEAM_BUFFER_M, passage["front_m"]


def passage_coords(passage, side, X, Y):
    seam_x, seam_y = seam_grid_m(passage, side)
    (cos_yaw, sin_yaw), _ = direction_and_lateral(passage)
    dx = np.asarray(X, dtype=np.float64) - seam_x
    dy = np.asarray(Y, dtype=np.float64) - seam_y
    return dx * cos_yaw + dy * sin_yaw, -dx * sin_yaw + dy * cos_yaw


def to_grid_m(passage, side, s, l):
    seam_x, seam_y = seam_grid_m(passage, side)
    (cos_yaw, sin_yaw), _ = direction_and_lateral(passage)
    return seam_x + s * cos_yaw - l * sin_yaw, seam_y + s * sin_yaw + l * cos_yaw


def seam_floor_z(passage, side, open_end_z):
    s_start, s_end = default_span(passage, side)
    open_s = s_start if side == "field" else s_end
    return float(quantize_height(open_end_z - passage["grade"] * open_s))


def floor_height(passage, s, z0, s_start, s_end, mouth_z=None, plaza_z=None):
    grade = passage["grade"]
    s = np.clip(np.asarray(s, dtype=np.float64), s_start, s_end)
    z = z0 + grade * s
    if mouth_z is not None and s_start < -SEAM_BUFFER_M:
        low_core_z = z0 - grade * SEAM_BUFFER_M
        t = np.clip((s - s_start) / (-SEAM_BUFFER_M - s_start), 0.0, 1.0)
        z = np.where(s < -SEAM_BUFFER_M, mouth_z + (low_core_z - mouth_z) * t, z)
    if plaza_z is not None and s_end > SEAM_BUFFER_M:
        high_core_z = z0 + grade * SEAM_BUFFER_M
        t = np.clip((s - SEAM_BUFFER_M) / (s_end - SEAM_BUFFER_M), 0.0, 1.0)
        z = np.where(s > SEAM_BUFFER_M, high_core_z + (plaza_z - high_core_z) * t, z)
    return z


def inner_section_height(a):
    wall = WALL_HEIGHT_M * (a - FLOOR_HALF_WIDTH_M) / (WALL_OUTER_M - FLOOR_HALF_WIDTH_M)
    return np.where(a <= FLOOR_HALF_WIDTH_M, 0.0, np.where(a <= WALL_OUTER_M, wall, WALL_HEIGHT_M))


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


def near_section_height(a):
    reach = np.clip(np.asarray(a, dtype=np.float64), FLOOR_HALF_WIDTH_M, BAND_OUTER_M)
    step = np.minimum(NEAR_LEDGE_HEIGHT_M, (reach - FLOOR_HALF_WIDTH_M) * NEAR_STEP_SLOPE)
    rise = np.maximum(0.0, reach - NEAR_LEDGE_OUTER_M) * NEAR_RISE_SLOPE
    return step + rise


NEAR_TOP_M = float(near_section_height(WALL_TOP_OUTER_M))


def near_sealed_height(a):
    reach = np.clip(np.asarray(a, dtype=np.float64), FLOOR_HALF_WIDTH_M, WALL_TOP_OUTER_M)
    return np.minimum((reach - FLOOR_HALF_WIDTH_M) * NEAR_STEP_SLOPE, NEAR_TOP_M)


def near_lateral_height(lateral_reach, s, is_low_open):
    ledge = near_section_height(lateral_reach)
    if not is_low_open:
        return ledge
    sealed_share = 1.0 - smoothstep(NEAR_SEAL_FULL_S_M, NEAR_SEAL_LEDGE_S_M, s)
    return ledge + (np.maximum(ledge, near_sealed_height(lateral_reach)) - ledge) * sealed_share


def dominant_caps(lateral_reach, low_cap_reach, high_cap_reach):
    is_low_cap = (low_cap_reach > lateral_reach) & (low_cap_reach >= high_cap_reach)
    is_high_cap = (high_cap_reach > lateral_reach) & (high_cap_reach > low_cap_reach)
    return is_low_cap, is_high_cap


def band_edge_terrain(h, X, Y, passage, side, s, l, lateral_reach, low_cap_reach, high_cap_reach):
    reach = np.maximum(lateral_reach, np.maximum(low_cap_reach, high_cap_reach))
    distance_out = BAND_OUTER_M - reach
    is_low_cap, is_high_cap = dominant_caps(lateral_reach, low_cap_reach, high_cap_reach)
    lateral_sign = np.where(l < 0.0, -1.0, 1.0)
    edge_s = s + np.where(is_low_cap, -distance_out, np.where(is_high_cap, distance_out, 0.0))
    edge_l = l + np.where(is_low_cap | is_high_cap, 0.0, lateral_sign * distance_out)
    edge_x, edge_y = to_grid_m(passage, side, edge_s, edge_l)
    return sample_grid(h, X, Y, edge_x, edge_y)


def band_height(top, terrain, edge_terrain, a, ridge_weight):
    u = np.clip((a - WALL_TOP_OUTER_M) / (BAND_OUTER_M - WALL_TOP_OUTER_M), 0.0, 1.0)
    crest = np.maximum(top, edge_terrain) + RIDGE_LIFT_M
    ridge = np.where(u <= 0.5, top + (crest - top) * (2.0 * u), crest + (edge_terrain - crest) * (2.0 * u - 1.0))
    plain = top + (terrain - top) * smoothstep(0.0, 1.0, u)
    return plain + (ridge - plain) * ridge_weight


def stamp_passage(h, X, Y, passage, side, z0, s_start, s_end, mouth_z=None, plaza_z=None):
    require_side(side)
    is_low_open = side == "field"
    is_high_open = side == "realm"
    s, l = passage_coords(passage, side, X, Y)
    lateral_reach = np.abs(l)
    low_cap_reach = np.full_like(s, -np.inf) if is_low_open else FLOOR_HALF_WIDTH_M + (s_start - s)
    high_cap_reach = np.full_like(s, -np.inf) if is_high_open else FLOOR_HALF_WIDTH_M + (s - s_end)
    a = np.maximum(lateral_reach, np.maximum(low_cap_reach, high_cap_reach))
    is_low_cap, is_high_cap = dominant_caps(lateral_reach, low_cap_reach, high_cap_reach)
    topdown_profile = tdr.ENABLED and not tdr.PLACEMENT_LEGACY_TERRAIN
    near_lateral = (np.sign(l) == passage["near_lateral_sign"]) if topdown_profile else np.zeros(l.shape, dtype=bool)
    near_low = bool(passage["near_cap_low"]) and topdown_profile
    near_high = bool(passage["near_cap_high"]) and topdown_profile
    near_dominant = np.where(is_low_cap, near_low, np.where(is_high_cap, near_high, near_lateral))
    ramp = OPEN_FLOOR_BLEND_M + (OPEN_WALL_BLEND_M - OPEN_FLOOR_BLEND_M) * np.clip((a - FLOOR_HALF_WIDTH_M) / (WALL_OUTER_M - FLOOR_HALF_WIDTH_M), 0.0, 1.0)
    near_ramp = np.where(a <= WALL_TOP_OUTER_M,
                         OPEN_FLOOR_BLEND_M + (NEAR_OPEN_BLEND_M - OPEN_FLOOR_BLEND_M) * np.clip((a - FLOOR_HALF_WIDTH_M) / (NEAR_LEDGE_HEIGHT_M / NEAR_STEP_SLOPE), 0.0, 1.0),
                         NEAR_OPEN_BLEND_M + (OPEN_WALL_BLEND_M - NEAR_OPEN_BLEND_M) * np.clip((a - WALL_TOP_OUTER_M) / (BAND_OUTER_M - WALL_TOP_OUTER_M), 0.0, 1.0))
    ramp = np.where(near_dominant, near_ramp, ramp)
    weight = (a <= BAND_OUTER_M).astype(np.float64)
    ridge_weight = np.ones_like(weight)
    if is_low_open:
        weight = weight * smoothstep(s_start, s_start + ramp, s)
        ridge_weight = ridge_weight * smoothstep(-SEAM_BUFFER_M - RIDGE_FADE_M, -SEAM_BUFFER_M, s)
    if is_high_open:
        weight = weight * (1.0 - smoothstep(np.maximum(SEAM_BUFFER_M, s_end - ramp), s_end, s))
        ridge_weight = ridge_weight * (1.0 - smoothstep(SEAM_BUFFER_M, SEAM_BUFFER_M + RIDGE_FADE_M, s))
    floor = floor_height(passage, s, z0, s_start, s_end, mouth_z, plaza_z)
    lateral_height = np.where(near_lateral, near_lateral_height(lateral_reach, s, is_low_open), inner_section_height(lateral_reach))
    low_height = near_section_height(low_cap_reach) if near_low else inner_section_height(low_cap_reach)
    high_height = near_section_height(high_cap_reach) if near_high else inner_section_height(high_cap_reach)
    lateral_top = np.where(near_lateral, NEAR_TOP_M, WALL_HEIGHT_M)
    top = floor + np.where(is_low_cap, NEAR_TOP_M if near_low else WALL_HEIGHT_M, np.where(is_high_cap, NEAR_TOP_M if near_high else WALL_HEIGHT_M, lateral_top))
    target = floor + np.maximum(lateral_height, np.maximum(low_height, high_height))
    band = (weight > 0.0) & (a > WALL_TOP_OUTER_M)
    edge_terrain = band_edge_terrain(h, X, Y, passage, side, s[band], l[band], lateral_reach[band], low_cap_reach[band], high_cap_reach[band])
    target[band] = band_height(top[band], h[band], edge_terrain, a[band], ridge_weight[band])
    if topdown_profile:
        other_height = np.maximum.reduce([np.where(is_low_cap, 0.0, low_height), np.where(is_high_cap, 0.0, high_height), np.where(is_low_cap | is_high_cap, 0.0, lateral_height)])
        keep_other = band & near_dominant
        target[keep_other] = np.maximum(target[keep_other], (floor + other_height)[keep_other])
    stamped = np.where(weight > 0.0, h * (1.0 - weight) + target * weight, h)
    floor_share = 1.0 - smoothstep(FLOOR_HALF_WIDTH_M - 0.5, FLOOR_HALF_WIDTH_M + 0.5, a)
    return stamped, floor_share * weight, (1.0 - floor_share) * weight


def floor_cells(passage, side, X, Y):
    s, l = passage_coords(passage, side, X, Y)
    s_start, s_end = default_span(passage, side)
    return (s >= s_start) & (s <= s_end) & (np.abs(l) <= FLOOR_HALF_WIDTH_M)


def core_mask(passage, side, X, Y, margin=0.0):
    s, l = passage_coords(passage, side, X, Y)
    return (np.abs(s) <= SEAM_BUFFER_M + margin) & (np.abs(l) <= BAND_OUTER_M + margin)


def to_world(passage, side, s, l, z_rel, z0, mouth_z=None, plaza_z=None):
    x_m, y_m = to_grid_m(passage, side, s, l)
    origin_x, origin_y = grid_origin_cm(passage, side)
    s_start, s_end = default_span(passage, side)
    floor = float(floor_height(passage, s, z0, s_start, s_end, mouth_z, plaza_z))
    return origin_x + x_m * 100.0, origin_y + y_m * 100.0, (floor + z_rel) * 100.0


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


def seam_zone_props(passage):
    pitch = math.degrees(math.atan(passage["grade"]))
    tile_length = FLOOR_TILE_LENGTH_M / math.cos(math.radians(pitch))
    props = []
    tile_count = int(round(2.0 * SEAM_BUFFER_M / FLOOR_TILE_LENGTH_M))
    for i in range(tile_count):
        s = -SEAM_BUFFER_M + (i + 0.5) * FLOOR_TILE_LENGTH_M
        props.append(plane_prop(f"Floor_{i}", BEDROCK_MATERIAL, s, 0.0, FLOOR_TILE_LIFT_M, (tile_length, FLOOR_TILE_WIDTH_M, 1.0), pitch))
    for side_sign in (-1, 1):
        if tdr.ENABLED and side_sign == passage["near_lateral_sign"]:
            continue
        for k, s in enumerate(WALL_CLIFF_STATIONS_M):
            key = WALL_CLIFF_KEYS[(k + (1 if side_sign > 0 else 0)) % len(WALL_CLIFF_KEYS)]
            yaw_offset = side_sign * 90.0 + ((k * 53) % 31 - 15)
            scale = 1.6 + 0.2 * (k % 3)
            props.append(mesh_prop(f"Wall_{'L' if side_sign < 0 else 'R'}_{k}", key, s, side_sign * 7.0, FAR_WALL_PROP_Z_REL_M if tdr.ENABLED else WALL_PROP_Z_REL_M, yaw_offset, (scale, scale, scale), roll=float((k * 29) % 13 - 6)))
    for i, s in enumerate(SEAM_LIGHT_STATIONS_M):
        props.append(light_prop(f"Light_{i}", s, 0.0, 7.0, 22.0, COLD_LIGHT, 14.0))
    for i, s in enumerate(SEAM_FOG_STATIONS_M):
        props.append(plane_prop(f"Fog_{i}", FOG_MATERIALS["Fog2"], s, 0.0, 0.7, (12.0, 9.0, 12.0)))
    return props


def field_passage_dressing(passage):
    s_start = -passage["approach_m"]
    s_limit = -SEAM_BUFFER_M - 8.0
    props = []
    k = 0
    s = s_start + 12.0
    while s <= s_limit:
        side_sign = -1 if k % 2 == 0 else 1
        props.append(mesh_prop(f"Stake_{k}", "StickedSkull", s, side_sign * 3.3, 0.0, side_sign * 90.0 + 180.0, (1.3, 1.3, 1.3)))
        k += 1
        s += 13.0
    k = 0
    s = s_start + 18.0
    while s <= s_limit:
        side_sign = 1 if k % 2 == 0 else -1
        t = (s - s_start) / (-SEAM_BUFFER_M - s_start)
        color = [WARM_LIGHT[i] + (COLD_LIGHT[i] - WARM_LIGHT[i]) * t for i in range(3)]
        props.append(mesh_prop(f"Lamppost_{k}", "Lamppost", s, side_sign * 3.4, 0.0, -side_sign * 90.0, (1.0, 1.0, 1.0)))
        props.append(light_prop(f"LampLight_{k}", s, side_sign * 2.9, 3.4, 30.0, [round(c, 3) for c in color], 11.0, source_radius=10.0, volumetric=2.0))
        k += 1
        s += 30.0
    k = 0
    s = s_start + 8.0
    while s <= s_limit + 4.0:
        side_sign = 1 if k % 2 == 0 else -1
        key = "SRock5" if k % 3 else "SRock7"
        scale = 1.0 + 0.25 * (k % 3)
        props.append(mesh_prop(f"Rock_{k}", key, s, side_sign * 3.7, 0.0, float((k * 47) % 360), (scale, scale, scale)))
        k += 1
        s += 6.0
    props.append(plane_prop("Fog_Approach", FOG_MATERIALS["Fog1"], -SEAM_BUFFER_M - 12.0, 0.0, 0.6, (14.0, 10.0, 14.0)))
    return props


def quantized_units(h):
    return np.round(32768.0 + h * UNITS_PER_METER)


def run_self_check():
    rng = np.random.default_rng(5)
    field_axis = np.arange(-504.0, 505.0)
    realm_axis = np.arange(-REALM_HALF_M, REALM_HALF_M + 1.0)
    FX, FY = np.meshgrid(field_axis, field_axis)
    RX, RY = np.meshgrid(realm_axis, realm_axis)
    failures = 0
    for p in PASSAGES:
        field_h = rng.normal(0.0, 6.0, FX.shape)
        realm_h = rng.normal(0.0, 6.0, RX.shape)
        mx, my = p["field_marker_m"]
        mouth_z = float(field_h[int(round(my)) + 504, int(round(mx)) + 504])
        z0_field = seam_floor_z(p, "field", mouth_z)
        z0_realm = seam_floor_z(p, "realm", 0.37)
        field_out, field_floor, _ = stamp_passage(field_h, FX, FY, p, "field", z0_field, *default_span(p, "field"), mouth_z=mouth_z)
        realm_out, _, _ = stamp_passage(realm_h, RX, RY, p, "realm", z0_realm, *default_span(p, "realm"), plaza_z=0.37)
        s_f, l_f = passage_coords(p, "field", FX, FY)
        core = (np.abs(s_f) <= SEAM_BUFFER_M) & (np.abs(l_f) <= WALL_TOP_OUTER_M)
        iy, ix = np.nonzero(core)
        fx, fy = p["field_seam_m"]
        rx, ry = p["realm_seam_local_m"]
        rix = (ix - 504 - int(fx) + int(rx) + int(REALM_HALF_M)).astype(int)
        riy = (iy - 504 - int(fy) + int(ry) + int(REALM_HALF_M)).astype(int)
        dz_units = round((z0_realm - z0_field) * UNITS_PER_METER)
        diff = quantized_units(realm_out[riy, rix]) - quantized_units(field_out[iy, ix]) - dz_units
        worst = float(np.max(np.abs(diff)))
        ok = worst <= 1.0 and core.sum() > 0
        failures += 0 if ok else 1
        print(f"{p['gate_id']}: marker {tuple(round(v, 3) for v in p['field_marker_m'])} yaw {p['yaw_deg']:.0f} field seam {p['field_seam_m']} approach {p['approach_m']} lateral {p['seam_lateral_error_m']} "
              f"realm seam {p['realm_seam_local_m']} edge {p['realm_edge_distance_m']} front {p['front_m']} {p['kind']} core points {int(core.sum())} max diff units {worst:.0f} floor cells {int((field_floor > 0.5).sum())} {'PASS' if ok else 'FAIL'}")
    return failures


if __name__ == "__main__":
    sys.exit(1 if run_self_check() else 0)
