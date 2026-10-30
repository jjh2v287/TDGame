"""실행 환경: 시스템 Python 3(numpy), 에디터·빌드·GPU 불필요
실행 명령: python Tools/FluidFX/check_fluid_foliage_bend.py [--output 요약.json] | --tuning <조절 사본 JSON> --kind <종류> --json <출력.json> (조절 입력 모드, calib_sweep offline 장면 k3)
출력: 항목별 PASS/FAIL 줄과 마지막 줄 JSON 요약(통과 수, 실패 목록, 결과 sha256, 위상 지연 기록), 하나라도 실패하면 종료 코드 1. 조절 입력 모드는 --json 에 {kind, tuning_sha256, inputs(상수 → 조절 필드·실효 값), unmapped, ripple_lag_seconds.{equal_blend, analytic, asymmetric_recovery}, bend_amplitude} 를 정렬 키로 쓰고 종료 0, 입력 오류 종료 2
상태: 미검증(2026-10-29 파동 3 FX-13c 작성: 실행 전). TDFluidFoliageBendLayer 의 numpy 거울 float32 - B1 매개변수·B2 계단 응답·B3 프레임률 무관·B4 눌림 방향 유지·B5 기울기 대체 방향·B6 물결 기울기 항·B7 창 이동 상태 이월·B8 Aux 병합 R·G 보존·B9 콘솔 재정의는 spec-fx13c 4절 헤드리스 리터럴과 같고, B10 무작위 10000칸 크기 1 이하·M4 빈 입력 규칙(속도 없음은 0, 해독 금지)·M5(b) 위상 지연 부호(앞섬은 음수, 해석해 대조)를 더한다. 같은 거울을 두 번 돌려 sha256 이 같은지 본다. 2026-10-30 CAL-0 B2: 조절 입력 모드(실효 값은 fluid_tuning_patch.effective_value, CAL-0 B1 병합 필요)와 상수 → 조절 필드 대응표 TUNING_FIELD_BY_CONSTANT(근거 TDFluidFoliageBendLayer.cpp MakeParameters:159-162 BendVectorRiseSeconds·BendVectorRecoverySeconds·BendFromRippleSlope), 시험 입력 파형·격자 상수는 unmapped. 실제 게임 회복과의 대조는 미검증.
"""
import argparse
import collections
import hashlib
import json
import math
import pathlib
import sys

import numpy as np

FLOAT = np.float32
DIRECTION_EPSILON = FLOAT(1e-4)
LENGTH_FLOOR = FLOAT(1e-20)
MINIMUM_RISE_SECONDS = 0.02
MINIMUM_RECOVERY_SECONDS = 0.05
RIPPLE_GRADIENT_NUMERATOR = 50.0
SHIFT_TOLERANCE_CELLS = 0.01
RANDOM_SEED = 1329
RANDOM_RESOLUTION = 100
RANDOM_STEP_COUNT = 40
PROBE_RESOLUTION = 4
DEFAULT_CELL_SIZE_CM = (3.90625, 3.90625)
LAG_RESOLUTION = 64
LAG_DELTA_SECONDS = 1.0 / 120.0
LAG_DURATION_SECONDS = 8.0
LAG_MEASURE_START_SECONDS = 4.0
LAG_FREQUENCY_HZ = 1.0
LAG_WAVELENGTH_CM = 250.0
LAG_AMPLITUDE = 0.4
LAG_RIPPLE_WEIGHT = 0.15
LAG_RISE_SECONDS = 0.08
LAG_ASYMMETRIC_RECOVERY_SECONDS = 0.6
LAG_ANALYTIC_TOLERANCE_SECONDS = 0.01
ROOT = pathlib.Path(__file__).resolve().parents[2]
BEND_STRUCT = "TDFluidSimulationConfig"
TUNING_FIELD_BY_CONSTANT = {"LAG_RISE_SECONDS": "BendVectorRiseSeconds", "LAG_ASYMMETRIC_RECOVERY_SECONDS": "BendVectorRecoverySeconds", "LAG_RIPPLE_WEIGHT": "BendFromRippleSlope"}
UNMAPPED_CONSTANTS = ("LAG_DELTA_SECONDS", "LAG_FREQUENCY_HZ", "LAG_WAVELENGTH_CM", "LAG_AMPLITUDE", "LAG_RESOLUTION", "DEFAULT_CELL_SIZE_CM")
EXIT_INPUT = 2

Parameters = collections.namedtuple(
    "Parameters",
    ("rise_blend", "recovery_blend", "painter_density_weight", "ripple_slope_weight", "ripple_gradient_scale", "uses_velocity", "has_water_surface", "wraps_area"),
)


def make_parameters(rise_seconds, recovery_seconds, delta_seconds, cell_size_cm, is_simple_painter=False, writes_painter_velocity=False, bend_from_painter_density=0.0, ripple_slope_weight=0.0, has_water_surface=False, wraps_area=False):
    rise_blend = FLOAT(1.0) - np.exp(FLOAT(-delta_seconds) / FLOAT(max(rise_seconds, MINIMUM_RISE_SECONDS)))
    recovery_blend = FLOAT(1.0) - np.exp(FLOAT(-delta_seconds) / FLOAT(max(recovery_seconds, MINIMUM_RECOVERY_SECONDS)))
    return Parameters(
        FLOAT(rise_blend),
        FLOAT(recovery_blend),
        FLOAT(bend_from_painter_density if is_simple_painter else 0.0),
        FLOAT(ripple_slope_weight),
        (FLOAT(RIPPLE_GRADIENT_NUMERATOR / cell_size_cm[0]), FLOAT(RIPPLE_GRADIENT_NUMERATOR / cell_size_cm[1])),
        (not is_simple_painter) or bool(writes_painter_velocity),
        bool(has_water_surface),
        bool(wraps_area),
    )


def squared_length(vectors):
    return vectors[..., 0] * vectors[..., 0] + vectors[..., 1] * vectors[..., 1]


def vector_length(vectors):
    return np.sqrt(squared_length(vectors))


def limit_to_unit_length(vectors):
    lengths = vector_length(vectors)
    scale = np.where(lengths > FLOAT(1.0), FLOAT(1.0) / np.maximum(lengths, LENGTH_FLOOR), FLOAT(1.0)).astype(FLOAT)
    return (vectors * scale[..., None]).astype(FLOAT)


def unit_direction(vectors, lengths):
    return (vectors / np.maximum(lengths, LENGTH_FLOOR)[..., None]).astype(FLOAT)


def compute_target(velocity, density, density_gradient, ripple_gradient, previous_bend, parameters):
    target = limit_to_unit_length(velocity.astype(FLOAT))
    painter_strength = (parameters.painter_density_weight * density).astype(FLOAT)
    target_length = vector_length(target)
    previous_length = vector_length(previous_bend)
    gradient_length = vector_length(density_gradient)
    uses_previous = previous_length > DIRECTION_EPSILON
    uses_target = ~uses_previous & (target_length > DIRECTION_EPSILON)
    uses_gradient = ~uses_previous & ~uses_target & (gradient_length > DIRECTION_EPSILON)
    direction = np.zeros_like(target)
    direction = np.where(uses_previous[..., None], unit_direction(previous_bend, previous_length), direction)
    direction = np.where(uses_target[..., None], unit_direction(target, target_length), direction)
    direction = np.where(uses_gradient[..., None], -unit_direction(density_gradient, gradient_length), direction)
    applies_press = (painter_strength > target_length) & (uses_previous | uses_target | uses_gradient)
    pressed = (direction * painter_strength[..., None]).astype(FLOAT)
    target = np.where(applies_press[..., None], pressed, target)
    target = (target - parameters.ripple_slope_weight * ripple_gradient.astype(FLOAT)).astype(FLOAT)
    return limit_to_unit_length(target)


def advance_bend(previous_bend, target, parameters):
    rising = squared_length(target) > squared_length(previous_bend)
    blend = np.where(rising, parameters.rise_blend, parameters.recovery_blend).astype(FLOAT)
    return (previous_bend + (target - previous_bend) * blend[..., None]).astype(FLOAT)


def resolve_shift(previous_center, current_center, cell_size_cm, resolution):
    shift = []
    for axis in range(2):
        cells = (current_center[axis] - previous_center[axis]) / cell_size_cm[axis]
        rounded = math.floor(cells + 0.5)
        if abs(cells - rounded) > SHIFT_TOLERANCE_CELLS or abs(rounded) >= resolution:
            return False, (0, 0)
        shift.append(int(rounded))
    return True, (shift[0], shift[1])


def resolve_enabled(console_value, preset_enabled):
    return console_value != 0 and bool(preset_enabled)


def resolve_input_rule(resolution, velocity_side, density_side, water_side, aux_side, uses_velocity, has_water_surface):
    uses = bool(uses_velocity) and velocity_side == resolution
    water = bool(has_water_surface) and water_side == resolution
    skips_pass = density_side != resolution or aux_side != resolution
    return uses, water, skips_pass


def shifted_state(state, shift_cells, resets):
    previous = np.zeros_like(state)
    if resets:
        return previous
    resolution = state.shape[0]
    shift_x, shift_y = shift_cells
    destination_x = slice(max(0, -shift_x), min(resolution, resolution - shift_x))
    source_x = slice(max(0, shift_x), min(resolution, resolution + shift_x))
    destination_y = slice(max(0, -shift_y), min(resolution, resolution - shift_y))
    source_y = slice(max(0, shift_y), min(resolution, resolution + shift_y))
    previous[destination_y, destination_x] = state[source_y, source_x]
    return previous


def neighbor_difference(field, axis, wraps_area):
    count = field.shape[axis]
    if wraps_area:
        return np.roll(field, -1, axis=axis) - np.roll(field, 1, axis=axis)
    widths = [(1, 1) if index == axis else (0, 0) for index in range(field.ndim)]
    padded = np.pad(field, widths, mode="edge")
    return np.take(padded, np.arange(2, count + 2), axis=axis) - np.take(padded, np.arange(0, count), axis=axis)


def advance_grid(bend_state, shift_cells, resets, velocity_pixels, density_pixels, water_pixels, parameters):
    resolution = bend_state.shape[0]
    previous = shifted_state(bend_state, shift_cells, resets)
    zero_scalar = np.zeros((resolution, resolution), dtype=FLOAT)
    if parameters.uses_velocity and velocity_pixels is not None:
        velocity = (FLOAT(2.0) * velocity_pixels[..., :2].astype(FLOAT) - FLOAT(1.0)).astype(FLOAT)
    else:
        velocity = np.zeros((resolution, resolution, 2), dtype=FLOAT)
    density = density_pixels[..., 0].astype(FLOAT) if density_pixels is not None else zero_scalar
    if parameters.has_water_surface and water_pixels is not None:
        ripple_height = (FLOAT(2.0) * water_pixels[..., 2].astype(FLOAT) - FLOAT(1.0)).astype(FLOAT)
        ripple_gradient = np.stack(
            [
                neighbor_difference(ripple_height, 1, parameters.wraps_area) * parameters.ripple_gradient_scale[0],
                neighbor_difference(ripple_height, 0, parameters.wraps_area) * parameters.ripple_gradient_scale[1],
            ],
            axis=-1,
        ).astype(FLOAT)
    else:
        ripple_gradient = np.zeros((resolution, resolution, 2), dtype=FLOAT)
    density_gradient = np.stack([neighbor_difference(density, 1, parameters.wraps_area), neighbor_difference(density, 0, parameters.wraps_area)], axis=-1).astype(FLOAT)
    target = compute_target(velocity, density, density_gradient, ripple_gradient, previous, parameters)
    return advance_bend(previous, target, parameters)


def write_cpu_bend_to_aux(aux_pixels, bend_state, resolution):
    output = np.zeros((resolution, resolution, 4), dtype=np.float16) if aux_pixels is None else aux_pixels.copy()
    if bend_state is None:
        output[..., 2:4] = 0
        return output
    output[..., 2:4] = bend_state.astype(np.float16)
    return output


def pack_velocity(vector, resolution):
    pixels = np.zeros((resolution, resolution, 4), dtype=np.float16)
    pixels[..., 0] = np.float16(0.5 + 0.5 * vector[0])
    pixels[..., 1] = np.float16(0.5 + 0.5 * vector[1])
    return pixels


def pack_density(value, resolution):
    pixels = np.zeros((resolution, resolution, 4), dtype=np.float16)
    pixels[..., 0] = np.float16(value)
    return pixels


def run_uniform_steps(state, velocity_vector, density_value, step_count, parameters):
    resolution = state.shape[0]
    velocity_pixels = pack_velocity(velocity_vector, resolution)
    density_pixels = pack_density(density_value, resolution)
    for _ in range(step_count):
        state = advance_grid(state, (0, 0), False, velocity_pixels, density_pixels, None, parameters)
    return state


def zero_state(resolution=PROBE_RESOLUTION):
    return np.zeros((resolution, resolution, 2), dtype=FLOAT)


def probe_length(state):
    return float(vector_length(state[1, 1]))


def test_parameters():
    parameters = make_parameters(0.25, 3.0, 1.0 / 60.0, DEFAULT_CELL_SIZE_CM)
    painter_off = make_parameters(0.25, 3.0, 1.0 / 60.0, DEFAULT_CELL_SIZE_CM, bend_from_painter_density=0.7)
    painter_on = make_parameters(0.25, 3.0, 1.0 / 60.0, DEFAULT_CELL_SIZE_CM, is_simple_painter=True, writes_painter_velocity=True, bend_from_painter_density=0.7)
    painter_silent = make_parameters(0.25, 3.0, 1.0 / 60.0, DEFAULT_CELL_SIZE_CM, is_simple_painter=True, writes_painter_velocity=False, bend_from_painter_density=0.7)
    checks = (
        abs(float(parameters.rise_blend) - 0.064493) <= 1e-6,
        abs(float(parameters.recovery_blend) - 0.0055402) <= 1e-6,
        float(painter_off.painter_density_weight) == 0.0,
        float(painter_on.painter_density_weight) == FLOAT(0.7) and painter_on.uses_velocity,
        not painter_silent.uses_velocity,
        abs(float(parameters.ripple_gradient_scale[0]) - 12.8) <= 1e-4 and abs(float(parameters.ripple_gradient_scale[1]) - 12.8) <= 1e-4,
    )
    detail = f"Rise {float(parameters.rise_blend):.6f} Recovery {float(parameters.recovery_blend):.7f} 경사 배율 {float(parameters.ripple_gradient_scale[0]):.4f}"
    values = [float(parameters.rise_blend), float(parameters.recovery_blend), float(painter_on.painter_density_weight), float(parameters.ripple_gradient_scale[0])]
    return all(checks), detail, values


def test_step_response():
    parameters = make_parameters(0.25, 3.0, 1.0 / 60.0, DEFAULT_CELL_SIZE_CM)
    risen = run_uniform_steps(zero_state(), (0.5, 0.0), 0.0, 15, parameters)
    risen_length = probe_length(risen)
    recovered = run_uniform_steps(risen, (0.0, 0.0), 0.0, 180, parameters)
    recovered_length = probe_length(recovered)
    passed = abs(risen_length - 0.31606) <= 1e-4 and abs(recovered_length - risen_length * math.exp(-1.0)) <= 1e-4
    return passed, f"상승 {risen_length:.5f} 복원 {recovered_length:.5f} 기대 {risen_length * math.exp(-1.0):.5f}", [risen_length, recovered_length]


def test_frame_rate_independence():
    lengths = []
    states = []
    for step_count in (15, 60):
        parameters = make_parameters(0.25, 3.0, 0.5 / step_count, DEFAULT_CELL_SIZE_CM)
        state = run_uniform_steps(zero_state(), (0.3, 0.4), 0.0, step_count, parameters)
        states.append(state[1, 1].astype(np.float64))
        lengths.append(probe_length(state))
    difference = float(np.max(np.abs(states[0] - states[1])))
    return difference <= 1e-5, f"Δt 1/30 대 1/120 최대 차 {difference:.2e}", [difference] + lengths


def test_press_holds_direction():
    parameters = make_parameters(0.1, 0.8, 1.0 / 60.0, DEFAULT_CELL_SIZE_CM, is_simple_painter=True, writes_painter_velocity=True, bend_from_painter_density=0.7)
    pressed = run_uniform_steps(zero_state(), (0.0, 0.5), 1.0, 6, parameters)
    held = run_uniform_steps(pressed, (0.0, 0.0), 1.0, 300, parameters)
    held_vector = held[1, 1].astype(np.float64)
    held_length = float(np.linalg.norm(held_vector))
    alignment = float(held_vector @ np.array([0.0, 1.0]) / max(held_length, 1e-12))
    released = run_uniform_steps(held, (0.0, 0.0), 0.0, 300, parameters)
    released_length = probe_length(released)
    passed = abs(held_vector[0]) <= 1e-3 and abs(held_vector[1] - 0.7) <= 1e-3 and alignment >= 0.999 and released_length <= 0.0015
    return passed, f"유지 ({held_vector[0]:.5f}, {held_vector[1]:.5f}) 정렬 {alignment:.5f} 놓은 뒤 {released_length:.6f}", [float(pressed[1, 1, 0]), float(pressed[1, 1, 1]), held_vector[0], held_vector[1], alignment, released_length]


def test_press_falls_back_to_outward_gradient():
    parameters = make_parameters(0.1, 0.8, 1.0 / 60.0, DEFAULT_CELL_SIZE_CM, is_simple_painter=True, writes_painter_velocity=True, bend_from_painter_density=0.7)
    target = compute_target(
        np.zeros((1, 2), dtype=FLOAT),
        np.array([0.8], dtype=FLOAT),
        np.array([[0.2, 0.0]], dtype=FLOAT),
        np.zeros((1, 2), dtype=FLOAT),
        np.zeros((1, 2), dtype=FLOAT),
        parameters,
    )
    passed = abs(float(target[0, 0]) + 0.56) <= 1e-5 and abs(float(target[0, 1])) <= 1e-6
    return passed, f"T ({float(target[0, 0]):.5f}, {float(target[0, 1]):.5f}) 기대 (-0.56, 0)", [float(target[0, 0]), float(target[0, 1])]


def test_ripple_slope_opposes_gradient():
    weak = make_parameters(0.1, 0.8, 1.0 / 60.0, DEFAULT_CELL_SIZE_CM, ripple_slope_weight=0.15, has_water_surface=True)
    strong = make_parameters(0.1, 0.8, 1.0 / 60.0, DEFAULT_CELL_SIZE_CM, ripple_slope_weight=1.0, has_water_surface=True)
    zero = np.zeros((1, 2), dtype=FLOAT)
    first = compute_target(zero, np.zeros(1, dtype=FLOAT), zero, np.array([[2.0, 0.0]], dtype=FLOAT), zero, weak)
    second = compute_target(zero, np.zeros(1, dtype=FLOAT), zero, np.array([[3.0, 4.0]], dtype=FLOAT), zero, strong)
    passed = abs(float(first[0, 0]) + 0.3) <= 1e-6 and abs(float(second[0, 0]) + 0.6) <= 1e-6 and abs(float(second[0, 1]) + 0.8) <= 1e-6
    return passed, f"T.x {float(first[0, 0]):.5f} 정규화 ({float(second[0, 0]):.5f}, {float(second[0, 1]):.5f})", [float(first[0, 0]), float(second[0, 0]), float(second[0, 1])]


def test_shift_carries_state():
    resolution = 8
    parameters = make_parameters(0.25, 3.0, 1.0 / 60.0, DEFAULT_CELL_SIZE_CM)
    state = zero_state(resolution)
    state[5, 5] = (0.4, 0.0)
    neutral_velocity = pack_velocity((0.0, 0.0), resolution)
    neutral_density = pack_density(0.0, resolution)
    cell = DEFAULT_CELL_SIZE_CM
    carried_ok, carried_shift = resolve_shift((0.0, 0.0), (2.0 * cell[0], 0.0), cell, resolution)
    moved = advance_grid(state, carried_shift, False, neutral_velocity, neutral_density, None, parameters)
    expected = 0.4 * (1.0 - float(parameters.recovery_blend))
    far_ok, _ = resolve_shift((0.0, 0.0), (8.0 * cell[0], 0.0), cell, resolution)
    half_ok, _ = resolve_shift((0.0, 0.0), (0.5 * cell[0], 0.0), cell, resolution)
    reset_state = advance_grid(state, (0, 0), not far_ok, neutral_velocity, neutral_density, None, parameters)
    outside_columns = float(np.max(np.abs(moved[:, 6:, :])))
    passed = (
        carried_ok
        and carried_shift == (2, 0)
        and abs(float(moved[5, 3, 0]) - expected) <= 1e-6
        and outside_columns == 0.0
        and not far_ok
        and not half_ok
        and float(np.max(np.abs(reset_state))) == 0.0
    )
    return passed, f"이월 b(3,5) {float(moved[5, 3, 0]):.6f} 기대 {expected:.6f} 바깥 열 {outside_columns} 큰 이동 거짓 {not far_ok} 반 칸 거짓 {not half_ok}", [float(moved[5, 3, 0]), expected, outside_columns]


def test_aux_merge_keeps_red_green():
    resolution = 8
    aux = np.zeros((resolution, resolution, 4), dtype=np.float16)
    aux[..., 0] = np.float16(0.25)
    aux[..., 1] = np.float16(-0.5)
    state = np.random.default_rng(RANDOM_SEED).uniform(-1.0, 1.0, (resolution, resolution, 2)).astype(FLOAT)
    merged = write_cpu_bend_to_aux(aux, state, resolution)
    empty_merged = write_cpu_bend_to_aux(None, state, resolution)
    cleared = write_cpu_bend_to_aux(aux, None, resolution)
    red_green_kept = np.array_equal(merged[..., :2].view(np.uint16), aux[..., :2].view(np.uint16))
    bend_matches = np.array_equal(merged[..., 2:4].view(np.uint16), state.astype(np.float16).view(np.uint16))
    empty_zero = float(np.max(np.abs(empty_merged[..., :2].astype(np.float32)))) == 0.0 and np.array_equal(empty_merged[..., 2:4], merged[..., 2:4])
    cleared_zero = float(np.max(np.abs(cleared[..., 2:4].astype(np.float32)))) == 0.0
    passed = red_green_kept and bend_matches and empty_zero and cleared_zero
    return passed, f"R·G 비트 보존 {red_green_kept} B·A 일치 {bend_matches} 빈 Aux R·G 0 {empty_zero} 상태 없음 B·A 0 {cleared_zero}", [float(np.sum(merged[..., 2:4].astype(np.float64)))]


def test_console_override():
    results = (resolve_enabled(0, True), resolve_enabled(-1, True), resolve_enabled(1, False), resolve_enabled(1, True))
    return results == (False, True, False, True), f"(0,참) {results[0]} (-1,참) {results[1]} (1,거짓) {results[2]} (1,참) {results[3]}", [float(item) for item in results]


def test_random_cells_bounded():
    generator = np.random.default_rng(RANDOM_SEED)
    resolution = RANDOM_RESOLUTION
    parameters = make_parameters(0.1, 0.8, 1.0 / 30.0, DEFAULT_CELL_SIZE_CM, is_simple_painter=True, writes_painter_velocity=True, bend_from_painter_density=1.0, ripple_slope_weight=2.0, has_water_surface=True, wraps_area=False)
    state = zero_state(resolution)
    largest = 0.0
    for _ in range(RANDOM_STEP_COUNT):
        velocity_pixels = generator.uniform(0.0, 1.0, (resolution, resolution, 4)).astype(np.float16)
        density_pixels = generator.uniform(0.0, 1.0, (resolution, resolution, 4)).astype(np.float16)
        water_pixels = generator.uniform(0.0, 1.0, (resolution, resolution, 4)).astype(np.float16)
        shift = (int(generator.integers(-2, 3)), int(generator.integers(-2, 3)))
        state = advance_grid(state, shift, False, velocity_pixels, density_pixels, water_pixels, parameters)
        largest = max(largest, float(np.max(vector_length(state))))
    finite = bool(np.all(np.isfinite(state)))
    return largest <= 1.0 + 1e-6 and finite, f"{resolution * resolution}칸 {RANDOM_STEP_COUNT}스텝 최대 |b| {largest:.7f} 유한 {finite}", [largest, float(np.sum(state.astype(np.float64)))]


def test_empty_input_rule():
    resolution = 256
    painter_velocity_side = 4
    silent_uses, _, silent_skips = resolve_input_rule(resolution, painter_velocity_side, resolution, None, resolution, False, False)
    missing_uses, missing_water, _ = resolve_input_rule(resolution, None, resolution, None, resolution, True, True)
    small_uses, small_water, _ = resolve_input_rule(resolution, painter_velocity_side, resolution, painter_velocity_side, resolution, True, True)
    _, _, density_skips = resolve_input_rule(resolution, resolution, 128, resolution, resolution, True, False)
    _, _, aux_skips = resolve_input_rule(resolution, resolution, resolution, resolution, 128, True, False)
    kept_uses, kept_water, kept_skips = resolve_input_rule(resolution, resolution, resolution, resolution, resolution, True, True)
    parameters = make_parameters(0.25, 3.0, 1.0 / 60.0, DEFAULT_CELL_SIZE_CM)
    state = zero_state()
    for _ in range(30):
        state = advance_grid(state, (0, 0), False, None, pack_density(0.0, PROBE_RESOLUTION), None, parameters)
    diagonal_leak = float(np.max(np.abs(state)))
    passed = (
        not silent_uses
        and not silent_skips
        and not missing_uses
        and not missing_water
        and not small_uses
        and not small_water
        and density_skips
        and aux_skips
        and kept_uses
        and kept_water
        and not kept_skips
        and diagonal_leak == 0.0
    )
    return passed, f"속도 없음·작은 크기 UsesVelocity 거짓, 밀도·Aux 불일치만 건너뜀, 속도 없는 단계 결과 {diagonal_leak}", [float(item) for item in (silent_uses, missing_uses, small_uses, density_skips, aux_skips, kept_skips)] + [diagonal_leak]


def measure_ripple_lag_seconds(rise_seconds, recovery_seconds, ripple_slope_weight=LAG_RIPPLE_WEIGHT):
    resolution = LAG_RESOLUTION
    cell_size = DEFAULT_CELL_SIZE_CM
    parameters = make_parameters(rise_seconds, recovery_seconds, LAG_DELTA_SECONDS, cell_size, ripple_slope_weight=ripple_slope_weight, has_water_surface=True)
    angular_frequency = 2.0 * math.pi * LAG_FREQUENCY_HZ
    wave_number = 2.0 * math.pi / LAG_WAVELENGTH_CM
    column_positions = np.arange(resolution) * cell_size[0]
    probe_column = resolution // 2
    velocity_pixels = pack_velocity((0.0, 0.0), resolution)
    density_pixels = pack_density(0.0, resolution)
    step_count = int(round(LAG_DURATION_SECONDS / LAG_DELTA_SECONDS))
    state = zero_state(resolution)
    times = np.zeros(step_count)
    bend_samples = np.zeros(step_count)
    height_samples = np.zeros(step_count)
    for step in range(step_count):
        time = step * LAG_DELTA_SECONDS
        height_row = LAG_AMPLITUDE * np.sin(wave_number * column_positions - angular_frequency * time)
        water_pixels = np.zeros((resolution, resolution, 4), dtype=np.float16)
        water_pixels[..., 2] = (0.5 + 0.5 * height_row)[None, :].astype(np.float16)
        state = advance_grid(state, (0, 0), False, velocity_pixels, density_pixels, water_pixels, parameters)
        sample_time = (step + 1) * LAG_DELTA_SECONDS
        times[step] = sample_time
        bend_samples[step] = float(state[resolution // 2, probe_column, 0])
        height_samples[step] = LAG_AMPLITUDE * math.sin(wave_number * column_positions[probe_column] - angular_frequency * sample_time)
    window = times > LAG_MEASURE_START_SECONDS
    carrier = np.exp(-1j * angular_frequency * times[window])
    bend_phase = np.angle(np.sum(bend_samples[window] * carrier))
    height_phase = np.angle(np.sum(height_samples[window] * carrier))
    period = 1.0 / LAG_FREQUENCY_HZ
    lag = (height_phase - bend_phase) / angular_frequency
    lag -= period * round(lag / period)
    return float(lag), float(np.max(np.abs(bend_samples[window])))


def test_phase_lag_sign():
    symmetric_lag, symmetric_amplitude = measure_ripple_lag_seconds(LAG_RISE_SECONDS, LAG_RISE_SECONDS)
    angular_frequency = 2.0 * math.pi * LAG_FREQUENCY_HZ
    quarter_period = 0.25 / LAG_FREQUENCY_HZ
    analytic = -quarter_period + math.atan(angular_frequency * LAG_RISE_SECONDS) / angular_frequency
    asymmetric_lag, asymmetric_amplitude = measure_ripple_lag_seconds(LAG_RISE_SECONDS, LAG_ASYMMETRIC_RECOVERY_SECONDS)
    passed = symmetric_lag < 0.0 and abs(symmetric_lag - analytic) <= LAG_ANALYTIC_TOLERANCE_SECONDS and symmetric_amplitude > 0.05
    detail = f"Recovery=Rise {LAG_RISE_SECONDS} s 위상 지연 {symmetric_lag:.4f} s(앞섬 음수, 해석해 {analytic:.4f}) 비대칭 Recovery {LAG_ASYMMETRIC_RECOVERY_SECONDS} s 기록 {asymmetric_lag:.4f} s"
    return passed, detail, [symmetric_lag, analytic, asymmetric_lag, symmetric_amplitude, asymmetric_amplitude]


TESTS = (
    ("B1 매개변수", test_parameters),
    ("B2 계단 응답", test_step_response),
    ("B3 프레임률 무관", test_frame_rate_independence),
    ("B4 눌림 방향 유지", test_press_holds_direction),
    ("B5 기울기 대체 방향", test_press_falls_back_to_outward_gradient),
    ("B6 물결 기울기 항", test_ripple_slope_opposes_gradient),
    ("B7 창 이동 상태 이월", test_shift_carries_state),
    ("B8 Aux 병합 R·G 보존", test_aux_merge_keeps_red_green),
    ("B9 콘솔 재정의", test_console_override),
    ("B10 무작위 칸 크기 1 이하", test_random_cells_bounded),
    ("M4 빈 입력 규칙", test_empty_input_rule),
    ("M5b 위상 지연 부호", test_phase_lag_sign),
)


def run_mirror_tests():
    outcomes = {}
    digest = hashlib.sha256()
    for label, test in TESTS:
        passed, detail, values = test()
        packed = np.ascontiguousarray(values, dtype=np.float64)
        digest.update(label.encode("utf-8"))
        digest.update(packed.tobytes())
        outcomes[label] = (passed, detail, packed.tolist())
    return outcomes, digest.hexdigest()


class InputError(Exception):
    pass


def load_effective_inputs(tuning_path, kind):
    sys.path.insert(0, str(ROOT / "Tools"))
    sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))
    import fluid_tuning_patch

    effective_value = getattr(fluid_tuning_patch, "effective_value", None)
    if effective_value is None:
        raise InputError("fluid_tuning_patch.effective_value(document, kind, struct, field) 가 없다(CAL-0 B1 병합 필요)")
    try:
        document = json.loads(pathlib.Path(tuning_path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise InputError(f"조절 사본을 읽지 못했다: {error}") from error
    if kind not in document.get("Kinds", {}):
        raise InputError(f"조절 사본에 종류 {kind} 가 없다")
    inputs = {}
    for constant, field in TUNING_FIELD_BY_CONSTANT.items():
        value = effective_value(document, kind, BEND_STRUCT, field)
        if not isinstance(value, (int, float)) or isinstance(value, bool):
            raise InputError(f"{kind} {BEND_STRUCT}.{field} 실효 값이 수가 아니다: {value!r}")
        inputs[constant] = {"json_field": f"{BEND_STRUCT}.{field}", "value": float(value)}
    return inputs


def measure_tuning_lag(tuning_path, kind):
    inputs = load_effective_inputs(tuning_path, kind)
    rise = inputs["LAG_RISE_SECONDS"]["value"]
    recovery = inputs["LAG_ASYMMETRIC_RECOVERY_SECONDS"]["value"]
    ripple_weight = inputs["LAG_RIPPLE_WEIGHT"]["value"]
    angular_frequency = 2.0 * math.pi * LAG_FREQUENCY_HZ
    equal_lag, equal_amplitude = measure_ripple_lag_seconds(rise, rise, ripple_weight)
    asymmetric_lag, asymmetric_amplitude = measure_ripple_lag_seconds(rise, recovery, ripple_weight)
    analytic = -0.25 / LAG_FREQUENCY_HZ + math.atan(angular_frequency * max(rise, MINIMUM_RISE_SECONDS)) / angular_frequency
    return {"kind": kind, "tuning_sha256": hashlib.sha256(pathlib.Path(tuning_path).read_bytes()).hexdigest(), "inputs": inputs, "unmapped": list(UNMAPPED_CONSTANTS),
            "ripple_lag_seconds": {"equal_blend": equal_lag, "analytic": analytic, "asymmetric_recovery": asymmetric_lag},
            "bend_amplitude": {"equal_blend": equal_amplitude, "asymmetric_recovery": asymmetric_amplitude}}


def run_tuning_mode(arguments):
    if not arguments.kind or not arguments.json:
        print("--tuning 은 --kind 와 --json 이 함께 필요하다", file=sys.stderr)
        return EXIT_INPUT
    try:
        report = measure_tuning_lag(arguments.tuning, arguments.kind)
    except InputError as error:
        print(f"입력 오류: {error}", file=sys.stderr)
        return EXIT_INPUT
    pathlib.Path(arguments.json).write_text(json.dumps(report, ensure_ascii=False, indent=1, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps(report["ripple_lag_seconds"], sort_keys=True))
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="")
    parser.add_argument("--tuning", default="")
    parser.add_argument("--kind", default="")
    parser.add_argument("--json", default="")
    arguments = parser.parse_args()
    if arguments.tuning:
        return run_tuning_mode(arguments)
    outcomes_first, digest_first = run_mirror_tests()
    _, digest_second = run_mirror_tests()
    failed = []
    for label, (passed, detail, _) in outcomes_first.items():
        print(("PASS " if passed else "FAIL ") + label + ": " + detail)
        if not passed:
            failed.append(label)
    deterministic = digest_first == digest_second
    print(("PASS " if deterministic else "FAIL ") + "거울 시험 2회 sha256 같음: " + digest_first)
    if not deterministic:
        failed.append("거울 시험 2회 sha256 같음")
    lag_values = outcomes_first["M5b 위상 지연 부호"][2]
    report = {"passed": len(outcomes_first) + 1 - len(failed), "failed": failed, "sha256": digest_first, "ripple_lag_seconds": {"equal_blend": lag_values[0], "analytic": lag_values[1], "asymmetric_recovery": lag_values[2]}}
    if arguments.output:
        pathlib.Path(arguments.output).write_text(json.dumps(report, ensure_ascii=False, indent=1) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps(report, ensure_ascii=False))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
