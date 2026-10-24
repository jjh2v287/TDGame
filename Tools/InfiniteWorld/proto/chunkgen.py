"""결정론 무한 청크 생성 핵심: 64비트 정수 해시 그래디언트 노이즈, fBm, 도메인 왜곡, 휘터커식 바이옴, 512 m 지역 격자 구조물. 위치는 int64 표본 단위(1 단위 = 0.5 m)로만 다루고 전역 난수를 쓰지 않는다. 실행 환경: Python 3.12 + numpy 2.x (Windows PowerShell 또는 Git Bash).
실행: import 전용 — python Tools/InfiniteWorld/proto/run_proto.py 가 불러 쓴다
출력: generate_chunk()/generate_area() → dict(height float64 m, biome uint8, struct_class uint8, struct_id int64), 배열 [iy, ix], 청크는 가장자리 표본을 이웃과 공유((n+1)x(n+1))
상태: 실험 재현용(2026-10-23 무한 샌드박스 세계 가능성 검토 갈래 01)
"""
import hashlib

import numpy as np

UNITS_PER_M = 2
MASK64 = (1 << 64) - 1

_MIX_A = np.uint64(0xBF58476D1CE4E5B9)
_MIX_B = np.uint64(0x94D049BB133111EB)
_LATTICE_KX = np.uint64(0x9E3779B97F4A7C15)
_LATTICE_KY = np.uint64(0xC2B2AE3D27D4EB4F)
_SHIFT_27 = np.uint64(27)
_SHIFT_30 = np.uint64(30)
_SHIFT_31 = np.uint64(31)
_SHIFT_61 = np.uint64(61)
_SHIFT_62 = np.uint64(62)
_DIAGONAL = 0.7071067811865476
GRADIENT_X = np.array([1.0, -1.0, 0.0, 0.0, _DIAGONAL, -_DIAGONAL, _DIAGONAL, -_DIAGONAL])
GRADIENT_Y = np.array([0.0, 0.0, 1.0, -1.0, _DIAGONAL, _DIAGONAL, -_DIAGONAL, -_DIAGONAL])
NOISE_SCALE = 1.4142135623730951

TAG_WARP_X = 11
TAG_WARP_Y = 12
TAG_HEIGHT = 13
TAG_CONTINENT = 14
TAG_TEMPERATURE = 15
TAG_HUMIDITY = 16
TAG_STRUCTURE = 17

WARP_CELL_UNITS = 1024
WARP_OCTAVES = 3
WARP_AMPLITUDE_M = 60.0
HEIGHT_CELL_UNITS = 2048
HEIGHT_OCTAVES = 8
CONTINENT_CELL_UNITS = 16384
CONTINENT_OCTAVES = 2
CLIMATE_CELL_UNITS = 16384
CLIMATE_OCTAVES = 3
HEIGHT_BASE_M = 20.0
HEIGHT_DETAIL_M = 150.0
HEIGHT_CONTINENT_M = 90.0
TEMPERATURE_MEAN_C = 14.0
TEMPERATURE_RANGE_C = 60.0
TEMPERATURE_LAPSE_C_PER_M = 0.08
HUMIDITY_RANGE = 2.0
ALPINE_HEIGHT_M = 85.0

BIOME_NAMES = [
    "Water", "Alpine", "Tundra", "Taiga", "TemperateGrassland", "TemperateForest",
    "TemperateRainforest", "Desert", "Savanna", "TropicalSeasonalForest", "TropicalRainforest",
]
BIOME_COLORS = np.array([
    [40, 90, 170], [235, 235, 240], [170, 180, 175], [60, 110, 80], [190, 190, 110], [70, 140, 60],
    [30, 110, 70], [225, 200, 130], [200, 170, 80], [120, 160, 50], [20, 120, 40],
], dtype=np.float64)
TEMPERATURE_EDGES_C = np.array([0.0, 7.0, 15.0, 22.0])
HUMIDITY_EDGES = np.array([0.2, 0.45, 0.7])
WHITTAKER_TABLE = np.array([
    [2, 2, 2, 2],
    [4, 3, 3, 3],
    [4, 4, 5, 6],
    [7, 8, 5, 6],
    [7, 8, 9, 10],
], dtype=np.uint8)

REGION_UNITS = 1024
STRUCTURE_PRESENCE_PER_65536 = 39322
STRUCTURE_MIN_RADIUS_UNITS = 80
STRUCTURE_MAX_RADIUS_UNITS = 200
STRUCTURE_SKIRT_UNITS = 40
WALL_THICKNESS_UNITS = 6
GATE_HALF_WIDTH_UNITS = 6
LOT_UNITS = 32
LOT_BUILDING_MIN = 4
LOT_BUILDING_MAX = 28
WALL_RAISE_M = 4.0
BUILDING_RAISE_M = 6.0
STRUCTURE_CLASS_PLAZA = 1
STRUCTURE_CLASS_WALL = 2
STRUCTURE_CLASS_BUILDING = 3


def mix64_int(value):
    value &= MASK64
    value = ((value ^ (value >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    value = ((value ^ (value >> 27)) * 0x94D049BB133111EB) & MASK64
    return value ^ (value >> 31)


def derive_seed(*parts):
    state = 0x243F6A8885A308D3
    for part in parts:
        state = mix64_int(state ^ (int(part) & MASK64))
    return state


def mix64(values):
    values = (values ^ (values >> _SHIFT_30)) * _MIX_A
    values = (values ^ (values >> _SHIFT_27)) * _MIX_B
    return values ^ (values >> _SHIFT_31)


def hash_lattice(seed, cell_x, cell_y):
    seed_value = np.uint64(seed)
    hashed = mix64(np.ascontiguousarray(cell_x, dtype=np.int64).view(np.uint64) * _LATTICE_KX + seed_value)
    return mix64(hashed ^ (np.ascontiguousarray(cell_y, dtype=np.int64).view(np.uint64) * _LATTICE_KY))


def fade(t):
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0)


def _corner_dot(seed, cell_x, cell_y, offset_x, offset_y):
    gradient_index = (hash_lattice(seed, cell_x, cell_y) >> _SHIFT_61).astype(np.intp)
    return GRADIENT_X[gradient_index] * offset_x + GRADIENT_Y[gradient_index] * offset_y


def gradient_noise(seed, cell_x, cell_y, frac_x, frac_y):
    next_x = cell_x + 1
    next_y = cell_y + 1
    back_x = frac_x - 1.0
    back_y = frac_y - 1.0
    n00 = _corner_dot(seed, cell_x, cell_y, frac_x, frac_y)
    n10 = _corner_dot(seed, next_x, cell_y, back_x, frac_y)
    n01 = _corner_dot(seed, cell_x, next_y, frac_x, back_y)
    n11 = _corner_dot(seed, next_x, next_y, back_x, back_y)
    blend_x = fade(frac_x)
    blend_y = fade(frac_y)
    bottom = n00 + blend_x * (n10 - n00)
    top = n01 + blend_x * (n11 - n01)
    return (bottom + blend_y * (top - bottom)) * NOISE_SCALE


def lattice_coordinate(position_units, warp_m, cell_units):
    cell = np.floor_divide(position_units, cell_units)
    frac = (position_units - cell * cell_units).astype(np.float64) / float(cell_units)
    if warp_m is None:
        return cell, frac
    frac = frac + warp_m * (UNITS_PER_M / cell_units)
    carry = np.floor(frac)
    return cell + carry.astype(np.int64), frac - carry


def octave_offsets(octave_seed):
    return derive_seed(octave_seed, 1) % 65536, derive_seed(octave_seed, 2) % 65536


def fbm(seed, position_x, position_y, base_cell_units, octaves, warp_x=None, warp_y=None, gain=0.5):
    total = np.zeros(position_x.shape, dtype=np.float64)
    amplitude = 1.0
    amplitude_sum = 0.0
    cell_units = base_cell_units
    for octave in range(octaves):
        octave_seed = derive_seed(seed, octave)
        offset_x, offset_y = octave_offsets(octave_seed)
        cell_x, frac_x = lattice_coordinate(position_x + offset_x, warp_x, cell_units)
        cell_y, frac_y = lattice_coordinate(position_y + offset_y, warp_y, cell_units)
        total += amplitude * gradient_noise(octave_seed, cell_x, cell_y, frac_x, frac_y)
        amplitude_sum += amplitude
        amplitude *= gain
        cell_units //= 2
    return total / amplitude_sum


def evaluate_base_height(world_seed, position_x, position_y):
    warp_x = WARP_AMPLITUDE_M * fbm(derive_seed(world_seed, TAG_WARP_X), position_x, position_y, WARP_CELL_UNITS, WARP_OCTAVES)
    warp_y = WARP_AMPLITUDE_M * fbm(derive_seed(world_seed, TAG_WARP_Y), position_x, position_y, WARP_CELL_UNITS, WARP_OCTAVES)
    detail = fbm(derive_seed(world_seed, TAG_HEIGHT), position_x, position_y, HEIGHT_CELL_UNITS, HEIGHT_OCTAVES, warp_x, warp_y)
    continent = fbm(derive_seed(world_seed, TAG_CONTINENT), position_x, position_y, CONTINENT_CELL_UNITS, CONTINENT_OCTAVES)
    return HEIGHT_BASE_M + HEIGHT_DETAIL_M * detail + HEIGHT_CONTINENT_M * continent


def classify_biome(world_seed, position_x, position_y, base_height):
    temperature = TEMPERATURE_MEAN_C + TEMPERATURE_RANGE_C * fbm(
        derive_seed(world_seed, TAG_TEMPERATURE), position_x, position_y, CLIMATE_CELL_UNITS, CLIMATE_OCTAVES)
    temperature = temperature - TEMPERATURE_LAPSE_C_PER_M * np.maximum(base_height, 0.0)
    humidity = np.clip(0.5 + HUMIDITY_RANGE * fbm(
        derive_seed(world_seed, TAG_HUMIDITY), position_x, position_y, CLIMATE_CELL_UNITS, CLIMATE_OCTAVES), 0.0, 1.0)
    biome = WHITTAKER_TABLE[np.digitize(temperature, TEMPERATURE_EDGES_C), np.digitize(humidity, HUMIDITY_EDGES)]
    biome = np.where(base_height > ALPINE_HEIGHT_M, np.uint8(1), biome)
    return np.where(base_height < 0.0, np.uint8(0), biome).astype(np.uint8)


def describe_region_structure(world_seed, region_x, region_y):
    region_hash = derive_seed(world_seed, TAG_STRUCTURE, region_x, region_y)
    if (region_hash & 0xFFFF) >= STRUCTURE_PRESENCE_PER_65536:
        return None
    center_span = REGION_UNITS - 2 * STRUCTURE_MAX_RADIUS_UNITS + 1
    radius_span = STRUCTURE_MAX_RADIUS_UNITS - STRUCTURE_MIN_RADIUS_UNITS + 1
    structure_seed = derive_seed(region_hash, 7)
    return {
        "region": (region_x, region_y),
        "center": (region_x * REGION_UNITS + STRUCTURE_MAX_RADIUS_UNITS + ((region_hash >> 16) % center_span),
                   region_y * REGION_UNITS + STRUCTURE_MAX_RADIUS_UNITS + ((region_hash >> 32) % center_span)),
        "radius": STRUCTURE_MIN_RADIUS_UNITS + ((region_hash >> 48) % radius_span),
        "seed": structure_seed,
        "id": structure_seed & 0x3FFFFFFFFFFFFFFF,
    }


def structure_reach_units(structure):
    return structure["radius"] + STRUCTURE_SKIRT_UNITS


def structures_touching(world_seed, x_min, x_max, y_min, y_max):
    reach = STRUCTURE_MAX_RADIUS_UNITS + STRUCTURE_SKIRT_UNITS
    found = []
    for region_y in range((y_min - reach) // REGION_UNITS, (y_max + reach) // REGION_UNITS + 1):
        for region_x in range((x_min - reach) // REGION_UNITS, (x_max + reach) // REGION_UNITS + 1):
            structure = describe_region_structure(world_seed, region_x, region_y)
            if structure is None:
                continue
            center_x, center_y = structure["center"]
            structure_reach = structure_reach_units(structure)
            if center_x + structure_reach < x_min or center_x - structure_reach > x_max:
                continue
            if center_y + structure_reach < y_min or center_y - structure_reach > y_max:
                continue
            found.append(structure)
    return found


def evaluate_point_height(world_seed, position_x, position_y):
    return float(evaluate_base_height(world_seed, np.array([[position_x]], dtype=np.int64),
                                      np.array([[position_y]], dtype=np.int64))[0, 0])


def apply_structure(world_seed, structure, xs, ys, height, struct_class, struct_id):
    center_x, center_y = structure["center"]
    reach = structure_reach_units(structure)
    column_begin = int(np.searchsorted(xs, center_x - reach, "left"))
    column_end = int(np.searchsorted(xs, center_x + reach, "right"))
    row_begin = int(np.searchsorted(ys, center_y - reach, "left"))
    row_end = int(np.searchsorted(ys, center_y + reach, "right"))
    if column_begin >= column_end or row_begin >= row_end:
        return 0
    shape = (row_end - row_begin, column_end - column_begin)
    offset_x = np.ascontiguousarray(np.broadcast_to(xs[column_begin:column_end][None, :] - center_x, shape))
    offset_y = np.ascontiguousarray(np.broadcast_to(ys[row_begin:row_end][:, None] - center_y, shape))
    distance_squared = offset_x * offset_x + offset_y * offset_y
    influence = distance_squared <= reach * reach
    if not influence.any():
        return 0
    height_view = height[row_begin:row_end, column_begin:column_end]
    class_view = struct_class[row_begin:row_end, column_begin:column_end]
    id_view = struct_id[row_begin:row_end, column_begin:column_end]
    plaza_height = evaluate_point_height(world_seed, center_x, center_y)
    distance = np.sqrt(distance_squared[influence].astype(np.float64))
    blend = np.clip((reach - distance) / STRUCTURE_SKIRT_UNITS, 0.0, 1.0)
    selected_height = height_view[influence]
    height_view[influence] = selected_height + blend * (plaza_height - selected_height)
    radius = structure["radius"]
    inner_radius = radius - WALL_THICKNESS_UNITS
    inside = distance_squared <= radius * radius
    interior = distance_squared < inner_radius * inner_radius
    gate = (np.abs(offset_x) <= GATE_HALF_WIDTH_UNITS) | (np.abs(offset_y) <= GATE_HALF_WIDTH_UNITS)
    wall = inside & ~interior & ~gate
    lot_x = np.floor_divide(offset_x, LOT_UNITS)
    lot_y = np.floor_divide(offset_y, LOT_UNITS)
    local_x = offset_x - lot_x * LOT_UNITS
    local_y = offset_y - lot_y * LOT_UNITS
    lot_center_x = lot_x * LOT_UNITS + LOT_UNITS // 2
    lot_center_y = lot_y * LOT_UNITS + LOT_UNITS // 2
    lot_margin = max(inner_radius - LOT_UNITS, 0)
    lot_inside = lot_center_x * lot_center_x + lot_center_y * lot_center_y <= lot_margin * lot_margin
    street = (lot_x == -1) | (lot_x == 0) | (lot_y == -1) | (lot_y == 0)
    lot_occupied = (hash_lattice(structure["seed"], lot_x, lot_y) >> _SHIFT_62) != 0
    footprint = (local_x >= LOT_BUILDING_MIN) & (local_x < LOT_BUILDING_MAX) & (local_y >= LOT_BUILDING_MIN) & (local_y < LOT_BUILDING_MAX)
    building = interior & lot_inside & ~street & lot_occupied & footprint
    class_view[interior] = STRUCTURE_CLASS_PLAZA
    class_view[wall] = STRUCTURE_CLASS_WALL
    class_view[building] = STRUCTURE_CLASS_BUILDING
    id_view[inside] = structure["id"]
    height_view[wall] += WALL_RAISE_M
    height_view[building] += BUILDING_RAISE_M
    return int(inside.sum())


def generate_area(world_seed, xs, ys, with_biome=True, with_structures=True):
    xs = np.asarray(xs, dtype=np.int64)
    ys = np.asarray(ys, dtype=np.int64)
    position_x, position_y = np.meshgrid(xs, ys)
    base_height = evaluate_base_height(world_seed, position_x, position_y)
    result = {"height": base_height.copy()}
    if with_biome:
        result["biome"] = classify_biome(world_seed, position_x, position_y, base_height)
    if not with_structures:
        return result
    struct_class = np.zeros(base_height.shape, dtype=np.uint8)
    struct_id = np.full(base_height.shape, -1, dtype=np.int64)
    touched = structures_touching(world_seed, int(xs[0]), int(xs[-1]), int(ys[0]), int(ys[-1]))
    for structure in touched:
        apply_structure(world_seed, structure, xs, ys, result["height"], struct_class, struct_id)
    result["struct_class"] = struct_class
    result["struct_id"] = struct_id
    result["structures_touched"] = len(touched)
    return result


def chunk_axis_units(chunk_index, chunk_m, spacing_m):
    step_units = int(round(spacing_m * UNITS_PER_M))
    if abs(step_units - spacing_m * UNITS_PER_M) > 1e-12 or step_units < 1:
        raise ValueError("spacing must be a multiple of 0.5 m")
    samples = int(round(chunk_m / spacing_m))
    origin = chunk_index * samples * step_units
    return origin + np.arange(samples + 1, dtype=np.int64) * step_units


def generate_chunk(world_seed, chunk_x, chunk_y, chunk_m, spacing_m, with_biome=True, with_structures=True):
    return generate_area(world_seed, chunk_axis_units(chunk_x, chunk_m, spacing_m),
                         chunk_axis_units(chunk_y, chunk_m, spacing_m), with_biome, with_structures)


def chunk_digest(result):
    digest = hashlib.sha256()
    for key in ("height", "biome", "struct_class", "struct_id"):
        if key in result:
            digest.update(key.encode())
            digest.update(np.ascontiguousarray(result[key]).tobytes())
    return digest.hexdigest()
