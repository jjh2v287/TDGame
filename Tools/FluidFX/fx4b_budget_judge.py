"""실행 환경: 시스템 Python 3(표준 라이브러리만), 에디터·빌드 불필요. 입력은 pie_profile_fluid.py --csv-gpu 실행 폴더(Saved/FluidFX/fx7p/<run>/, session.json·plan.json·csv/ 블록 CSV)와 선택 영역 예산 표 JSON(없으면 내장 FX-4b 물 표, 같은 내용 파일 Tools/FluidFX/perf/fixtures/fx4b_water_budget.json).
실행 명령: python -B Tools/FluidFX/fx4b_budget_judge.py pilot|judge <run_dir> [--budget-table <영역 예산 표 json>] [--water-ref-ms|--ref-ms <기준 ms, 표에 reference가 있으면 judge 필수·없으면 금지>] [--out <json>]
출력: 표준 출력에 사람이 읽는 요약, --out이면 같은 내용을 JSON으로 저장(pilot = 장면별 표 지표 필수 열·PD-H 통과 비율·대략값, judge = 기준 장면 유효성·표의 판정 행(구간·걷기)·기록 행·예산 판정, --budget-table이면 budget_table{path,sha256,area} 추가). 판정 불가는 이름 붙은 사유, 예산 표 오류는 종료 2.
상태: 2026-10-30 CAL-0 B7 영역 예산 표 일반화(내장 표 = 2026-10-24 FX-4b 판정, 출력 동일. 명세 Saved/AgentOps/20261024/fx4b-acc/spec-fx4b-budget.md 1~5절, fx7pr_judge.py의 구간·안정·클럭 규칙 계승), 시험은 그 폴더 budget-fixtures/run_fixtures.py(FX-4b)와 Tools/FluidFX/perf/fixtures/check_budget_table_fixtures.py(일반 표).
"""
import argparse
import csv
import hashlib
import json
import math
import re
import sys
from pathlib import Path

FRAME = "FrameTime"
GAME_THREAD = "GameThreadTime"
STEP_COUNT = "TDFluid/StepCount"
SOLVER = "GPU/TDFluidGpuSolver"
WATER = "GPU/TDFluidGpuWater"
PRESSURE = "GPU/TDFluidGpuWaterPressure"
NAMED_GPU_COLUMNS = (SOLVER, WATER, PRESSURE)
SUBSTEPS = "TDFluidGpu/WaterRippleSubsteps"
PRESSURE_MODE = "TDFluidGpu/WaterPressureMode"
GAME_THREAD_WATER_COLUMNS = ("TDFluid/GameThread/WaterRasterDirtyCells", "TDFluid/GameThread/WaterGatherStep", "TDFluid/GameThread/WaterProbeMapping",
                             "TDFluid/GameThread/TerrainQueueApply", "TDFluid/GameThread/TerrainQueueShift", "TDFluid/GameThread/TerrainQueueSample")
TICKABLES = "Exclusive/GameThread/Tickables"
WALK_SPEED = "View/Speed2D"
WANTED_COLUMNS = (FRAME, GAME_THREAD, STEP_COUNT, SUBSTEPS, PRESSURE_MODE, TICKABLES, WALK_SPEED) + NAMED_GPU_COLUMNS + GAME_THREAD_WATER_COLUMNS
HEADER_AT_END_MARKER = "[HasHeaderRowAtEnd]"

EPSILON = 1e-9
DEFAULT_DISCARD_ROWS = 60
TARGET_LABEL = "TDGen_WaterRiverSimulation"
SCENE_DEFINITIONS = {"WQ0": ("GPU", 1024), "WQ1": ("CPU", 192), "WQ2": ("GPU", 192), "WQ3": ("GPU", 1024), "WQ4": ("GPU", 2048), "WQ5": ("GPU", 1024)}
GPU_LIMIT_192_MS = 0.10
GPU_LIMIT_1024_MS = 0.90
GAME_THREAD_LIMIT_MS = 0.30
MOVING_THRESHOLD_RATIO = 1.3
MOVING_P95_RATIO = 1.5
SUBSTEP_LIMIT = 2
SUBSTEP_MARGIN = 2
PASS_RATIO_MINIMUM = 0.5
REFERENCE_RATIO_RANGE = (0.8, 1.5)
STABILITY_RELATIVE = 0.10
STABILITY_SMALL_MEDIAN_MS = 0.2
STABILITY_SMALL_RANGE_MS = 0.01
STAGE_ROUNDS = (3, 5)
MINIMUM_WALK_SPEED = 200.0
GPU_LAYER_UNRECORDED_WARNING = f"블록 기록에 is_using_gpu_water_layer가 없다(도구가 쓰지 않음, {WATER} A p50 > 0으로 대신 확인)"
MOVING_DEFINITION = "이동 프레임 근사: PD-H 통과 프레임 가운데 GPU/TDFluidGpuWater > 1.3 x 블록 중앙값(CSV에 창 이동 표지 열이 없다, 사후 아님)"

SLOT_PATTERN = re.compile(r"_r(\d+)_(\d)([AB])$")
RETRY_PATTERN = re.compile(r"_retry\d+$")
BACKEND_PATTERN = re.compile(r"(GPU|CPU)", re.IGNORECASE)
PAIR_POSITIONS = ((1, 2), (4, 3))
SIDE_POSITIONS = {"A": (1, 4), "B": (2, 3)}

TABLE_VERSION = 1
BUDGET_VERDICTS = ("충족(proposed)", "미충족", "판정 불가")
DOCUMENT_KEYS = {"source", "basis", "note"}
TABLE_KEYS = {"version", "area", "title", "target_label", "water_path_check", "scenes", "metrics", "reference", "rows", "records", "consequences"} | DOCUMENT_KEYS
SCENE_KEYS = {"backend", "grid", "target_labels", "awake_step_count"} | DOCUMENT_KEYS
METRIC_KEYS = {"label", "columns", "pair", "pdh", "backends"} | DOCUMENT_KEYS
REFERENCE_KEYS = {"scene", "metric", "ratio_range"} | DOCUMENT_KEYS
ROW_KEYS = {"row", "scene", "kind", "metric", "limit_ms", "moving_p95_ratio", "label"} | DOCUMENT_KEYS
RECORD_KEYS = {"scene", "game_thread_focus"} | DOCUMENT_KEYS
PAIR_KINDS = ("difference", "a_only")
ROW_KINDS = ("interval", "walk")
BACKENDS = ("GPU", "CPU")
GENERIC_CONSEQUENCES = {"충족(proposed)": "영역 성능 예산 충족(proposed): 화면 승인만 남는다",
                        "미충족": "미달 행의 비용 손잡이 채택을 백업 값으로 되돌리고 그 영역을 다시 재거나 예산 개정을 결정 기록에 proposed로 올린다",
                        "판정 불가": "판정 불가 사유를 해소한 뒤 같은 계획으로 다시 잰다"}

BUILTIN_METRICS = {
    "named_gpu": {"label": "명명 GPU", "columns": list(NAMED_GPU_COLUMNS), "pair": "difference", "pdh": True, "backends": ["GPU"]},
    "game_thread_water": {"label": "게임 스레드 물 합", "columns": list(GAME_THREAD_WATER_COLUMNS), "pair": "a_only", "pdh": False, "backends": ["GPU"]},
}
FX4B_BUDGET_TABLE = {
    "version": TABLE_VERSION, "area": "water", "title": "FX-4b 물 예산", "target_label": TARGET_LABEL, "water_path_check": True,
    "scenes": {scene_id: {"backend": backend, "grid": grid} for scene_id, (backend, grid) in SCENE_DEFINITIONS.items()},
    "metrics": {},
    "reference": {"scene": "WQ0", "metric": "named_gpu", "ratio_range": list(REFERENCE_RATIO_RANGE)},
    "rows": [
        {"row": "B192", "scene": "WQ2", "metric": "named_gpu", "limit_ms": GPU_LIMIT_192_MS, "label": "명명 GPU d"},
        {"row": "B1024", "scene": "WQ3", "metric": "named_gpu", "limit_ms": GPU_LIMIT_1024_MS, "label": "명명 GPU d"},
        {"row": "G192", "scene": "WQ2", "metric": "game_thread_water", "limit_ms": GAME_THREAD_LIMIT_MS, "label": "게임 스레드 물 합 A p50"},
        {"row": "G1024", "scene": "WQ3", "metric": "game_thread_water", "limit_ms": GAME_THREAD_LIMIT_MS, "label": "게임 스레드 물 합 A p50"},
        {"row": "GW", "scene": "WQ5", "kind": "walk", "metric": "game_thread_water", "limit_ms": GAME_THREAD_LIMIT_MS, "moving_p95_ratio": MOVING_P95_RATIO,
         "label": "게임 스레드 물 합 A p50 + 이동 프레임 명명 GPU p95"},
    ],
    "records": [{"scene": "WQ1", "game_thread_focus": True}, {"scene": "WQ4", "game_thread_focus": False}],
    "consequences": {"충족(proposed)": "FX-4b 성능 예산 충족(proposed): 1024² 화면 승인(RM:533)만 남는다",
                     "미충족": "레버 잔여(L1·L3·S2·S3·S5) 효과를 실측으로 더 낸 뒤 다시 재거나 예산 개정을 결정 기록에 proposed로 올린다",
                     "판정 불가": "판정 불가 사유를 해소한 뒤 같은 계획으로 다시 잰다"},
}


def build_metric(name, definition):
    pdh = definition.get("pdh", False)
    pair = definition.get("pair", "difference")
    columns = tuple(definition["columns"])
    return {"name": name, "label": definition.get("label", name), "columns": columns, "needed": columns + ((SUBSTEPS,) if pdh else ()),
            "sides": ("A", "B") if pair == "difference" else ("A",), "uses_pdh": pdh, "pair": pair, "backends": tuple(definition.get("backends", ()))}


GPU_METRIC = build_metric("named_gpu", BUILTIN_METRICS["named_gpu"])


def is_integer(value):
    return isinstance(value, int) and not isinstance(value, bool)


def is_positive_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value) and value > 0


def find_unknown_keys(item, allowed, where):
    return [f"{where}: 모르는 키 {key}" for key in item if key not in allowed]


def find_scene_errors(scene_id, scene, default_label):
    where = f"장면 {scene_id}"
    if not isinstance(scene, dict):
        return [f"{where}: 객체가 아니다"]
    errors = find_unknown_keys(scene, SCENE_KEYS, where)
    errors.extend(f"{where}: {key} 필요(확인하지 않으려면 null)" for key in ("backend", "grid") if key not in scene)
    if scene.get("backend") not in BACKENDS + (None,):
        errors.append(f"{where}: backend {scene.get('backend')!r}는 GPU·CPU·null 중 하나")
    if scene.get("grid") is not None and not (is_integer(scene["grid"]) and scene["grid"] > 0):
        errors.append(f"{where}: grid {scene.get('grid')!r}는 양의 정수 또는 null")
    if not (is_integer(scene.get("awake_step_count", 1)) and scene.get("awake_step_count", 1) >= 1):
        errors.append(f"{where}: awake_step_count는 1 이상 정수")
    labels = scene.get("target_labels", [default_label] if default_label else [])
    if not isinstance(labels, list) or not labels or not all(isinstance(label, str) and label for label in labels):
        errors.append(f"{where}: 대상 액터 이름이 없다(표 target_label 또는 장면 target_labels)")
    return errors


def find_metric_errors(name, metric):
    where = f"지표 {name}"
    if name in BUILTIN_METRICS:
        return [f"{where}: 내장 지표 이름과 겹친다"]
    if not isinstance(metric, dict):
        return [f"{where}: 객체가 아니다"]
    errors = find_unknown_keys(metric, METRIC_KEYS, where)
    columns = metric.get("columns")
    if not isinstance(columns, list) or not columns or not all(isinstance(column, str) and column for column in columns):
        errors.append(f"{where}: columns는 비지 않은 열 이름 목록")
    if metric.get("pair", "difference") not in PAIR_KINDS:
        errors.append(f"{where}: pair {metric.get('pair')!r}는 {PAIR_KINDS} 중 하나")
    if not isinstance(metric.get("pdh", False), bool):
        errors.append(f"{where}: pdh는 참·거짓")
    backends = metric.get("backends", [])
    if not isinstance(backends, list) or any(backend not in BACKENDS for backend in backends):
        errors.append(f"{where}: backends는 {BACKENDS} 목록")
    if not isinstance(metric.get("label", name), str):
        errors.append(f"{where}: label은 문자열")
    return errors


def find_reference_errors(reference, scenes, metrics):
    if reference is None:
        return []
    if not isinstance(reference, dict):
        return ["reference: 객체 또는 null"]
    errors = find_unknown_keys(reference, REFERENCE_KEYS, "reference")
    if reference.get("scene") not in scenes:
        errors.append(f"reference: 장면 {reference.get('scene')!r}이 scenes에 없다")
    if reference.get("metric") not in metrics:
        errors.append(f"reference: 지표 {reference.get('metric')!r}이 없다")
    ratio_range = reference.get("ratio_range")
    if not (isinstance(ratio_range, list) and len(ratio_range) == 2 and all(is_positive_number(value) for value in ratio_range) and ratio_range[0] < ratio_range[1]):
        errors.append("reference: ratio_range는 [하한, 상한] 양수이고 하한 < 상한")
    return errors


def find_row_errors(rows, scenes, metrics):
    if not isinstance(rows, list) or not rows:
        return ["rows: 판정 행이 1개 이상 필요"]
    errors = []
    seen = set()
    for index, row in enumerate(rows):
        if not isinstance(row, dict):
            errors.append(f"행 {index}: 객체가 아니다")
            continue
        where = f"행 {row.get('row', index)}"
        errors.extend(find_unknown_keys(row, ROW_KEYS, where))
        if not isinstance(row.get("row"), str) or not row.get("row") or row.get("row") in seen:
            errors.append(f"{where}: row 이름이 없거나 겹친다")
        seen.add(row.get("row"))
        if row.get("scene") not in scenes:
            errors.append(f"{where}: 장면 {row.get('scene')!r}이 scenes에 없다")
        if row.get("metric") not in metrics:
            errors.append(f"{where}: 지표 {row.get('metric')!r}이 없다")
        if not is_positive_number(row.get("limit_ms")):
            errors.append(f"{where}: limit_ms는 양수")
        kind = row.get("kind", "interval")
        if kind not in ROW_KINDS:
            errors.append(f"{where}: kind {kind!r}는 {ROW_KINDS} 중 하나")
        if kind == "walk" and not is_positive_number(row.get("moving_p95_ratio", MOVING_P95_RATIO)):
            errors.append(f"{where}: moving_p95_ratio는 양수")
        if kind != "walk" and "moving_p95_ratio" in row:
            errors.append(f"{where}: moving_p95_ratio는 walk 행에만 쓴다")
        if not isinstance(row.get("label", ""), str):
            errors.append(f"{where}: label은 문자열")
    return errors


def find_record_errors(records, scenes):
    if not isinstance(records, list):
        return ["records: 목록이 아니다"]
    errors = []
    for index, record in enumerate(records):
        if not isinstance(record, dict):
            errors.append(f"기록 {index}: 객체가 아니다")
            continue
        errors.extend(find_unknown_keys(record, RECORD_KEYS, f"기록 {index}"))
        if record.get("scene") not in scenes:
            errors.append(f"기록 {index}: 장면 {record.get('scene')!r}이 scenes에 없다")
        if not isinstance(record.get("game_thread_focus", False), bool):
            errors.append(f"기록 {index}: game_thread_focus는 참·거짓")
    if len({record.get("scene") for record in records if isinstance(record, dict)}) != len(records):
        errors.append("records: 장면이 겹친다")
    return errors


def find_table_errors(table):
    if not isinstance(table, dict):
        return ["표 최상위가 객체가 아니다"]
    errors = find_unknown_keys(table, TABLE_KEYS, "표")
    if table.get("version") != TABLE_VERSION:
        errors.append(f"version {table.get('version')!r}(기대 {TABLE_VERSION})")
    if not isinstance(table.get("area"), str) or not table.get("area"):
        errors.append("area: 비지 않은 문자열 필요")
    if not isinstance(table.get("water_path_check"), bool):
        errors.append("water_path_check: 참·거짓 필요(물 표만 참)")
    if not isinstance(table.get("target_label", ""), str):
        errors.append("target_label: 문자열")
    scenes = table.get("scenes")
    metrics = table.get("metrics", {})
    if not isinstance(scenes, dict) or not scenes:
        return errors + ["scenes: 장면이 1개 이상 필요"]
    if not isinstance(metrics, dict):
        return errors + ["metrics: 객체가 아니다"]
    for scene_id, scene in scenes.items():
        errors.extend(find_scene_errors(scene_id, scene, table.get("target_label")))
    for name, metric in metrics.items():
        errors.extend(find_metric_errors(name, metric))
    known = {**BUILTIN_METRICS, **metrics}
    labels = [definition.get("label", name) for name, definition in known.items() if isinstance(definition, dict)]
    if len(set(labels)) != len(labels):
        errors.append(f"metrics: 지표 label이 겹친다 {labels}")
    errors.extend(find_reference_errors(table.get("reference"), scenes, known))
    errors.extend(find_row_errors(table.get("rows"), scenes, known))
    errors.extend(find_record_errors(table.get("records", []), scenes))
    consequences = table.get("consequences", {})
    if not isinstance(consequences, dict) or any(key not in BUDGET_VERDICTS or not isinstance(text, str) for key, text in consequences.items()):
        errors.append(f"consequences: 키는 {BUDGET_VERDICTS} 중 하나, 값은 문자열")
    return errors


def normalize_row(row, metrics):
    metric = metrics[row["metric"]]
    default_label = f"{metric['label']} d" if metric["pair"] == "difference" else f"{metric['label']} A p50"
    return {"row": row["row"], "scene": row["scene"], "kind": row.get("kind", "interval"), "metric": row["metric"], "limit": row["limit_ms"],
            "moving_p95_ratio": row.get("moving_p95_ratio", MOVING_P95_RATIO), "label": row.get("label", default_label), "source": row.get("source")}


def normalize_table(table):
    metrics = {name: build_metric(name, definition) for name, definition in {**BUILTIN_METRICS, **table.get("metrics", {})}.items()}
    scenes = {scene_id: {"backend": scene["backend"], "grid": scene["grid"], "target_labels": tuple(scene.get("target_labels", [table.get("target_label")])),
                         "awake_step_count": scene.get("awake_step_count", 1)} for scene_id, scene in table["scenes"].items()}
    reference = table.get("reference")
    return {"area": table["area"], "title": table.get("title", table["area"]), "water_path_check": table["water_path_check"], "scenes": scenes, "metrics": metrics,
            "reference": None if reference is None else {"scene": reference["scene"], "metric": reference["metric"], "ratio_range": tuple(reference["ratio_range"])},
            "rows": [normalize_row(row, metrics) for row in table["rows"]], "records": [{"scene": record["scene"], "game_thread_focus": record.get("game_thread_focus", False)} for record in table.get("records", [])],
            "consequences": {**GENERIC_CONSEQUENCES, **table.get("consequences", {})}, "source_file": None}


def load_budget_table(path):
    try:
        raw = Path(path).read_bytes()
        table = json.loads(raw.decode("utf-8"))
    except (OSError, UnicodeDecodeError, ValueError) as error:
        return None, [f"읽기 실패: {error}"]
    errors = find_table_errors(table)
    if errors:
        return None, errors
    normalized = normalize_table(table)
    normalized["source_file"] = {"path": str(path), "sha256": hashlib.sha256(raw).hexdigest(), "area": table["area"]}
    return normalized, []


DEFAULT_BUDGET_TABLE = normalize_table(FX4B_BUDGET_TABLE)


def percentile(values, fraction):
    if not values:
        return None
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = math.floor(position)
    upper = math.ceil(position)
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def median_of(values):
    return percentile(values, 0.5)


def round_four(value):
    return None if value is None else round(value, 4)


def mean_of(values):
    return sum(values) / len(values) if values else None


def present_values(values):
    return [value for value in (values or []) if value is not None]


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def parse_cell(row, index):
    if index >= len(row):
        return None
    try:
        return float(row[index])
    except ValueError:
        return None


def read_block_table(path, discard_rows, wanted_columns):
    with open(path, encoding="utf-8", errors="replace", newline="") as handle:
        rows = list(csv.reader(handle))
    if not rows:
        return {"row_count": 0, "columns": {}}
    metadata_index = next((index for index, row in enumerate(rows) if row and row[0].startswith("[")), len(rows))
    has_header_at_end = metadata_index < len(rows) and HEADER_AT_END_MARKER in rows[metadata_index]
    header_row = rows[metadata_index - 1] if has_header_at_end else rows[0]
    data_rows = rows[1:metadata_index - 1] if has_header_at_end else rows[1:metadata_index]
    header = [name.strip() for name in header_row]
    kept_rows = [row for row in data_rows[discard_rows:] if len(row) <= len(header)]
    indices = {}
    for index, name in enumerate(header):
        if name in wanted_columns:
            indices.setdefault(name, index)
    return {"row_count": len(kept_rows), "columns": {name: [parse_cell(row, index) for row in kept_rows] for name, index in indices.items()}}


def read_series(table, name, fill_zero, assumed_zero):
    values = table["columns"].get(name)
    if values is not None:
        return values
    if not fill_zero:
        return None
    assumed_zero.append(name)
    return [0.0] * table["row_count"]


def sum_series(series_list):
    if any(series is None for series in series_list):
        return None
    return [None if any(value is None for value in cell) else sum(cell) for cell in zip(*series_list)]


def build_pdh_mask(substeps):
    count = len(substeps)
    within_limit = [value is not None and value <= SUBSTEP_LIMIT for value in substeps]
    mask = [False] * count
    for index in range(SUBSTEP_MARGIN, count - SUBSTEP_MARGIN):
        mask[index] = all(within_limit[index - SUBSTEP_MARGIN:index + SUBSTEP_MARGIN + 1])
    return mask


def select_metric_values(series, mask, uses_pdh):
    if series is None:
        return []
    if not uses_pdh:
        return present_values(series)
    if mask is None:
        return []
    return [value for value, passed in zip(series, mask) if passed and value is not None]


def summarize_block(block, side, table, metrics):
    columns = table["columns"]
    row_count = table["row_count"]
    step_values = present_values(columns.get(STEP_COUNT))
    step_count_max = max(step_values) if step_values else None
    idle_confirmed = side == "B" and (step_count_max is None or step_count_max == 0)
    assumed_zero = []
    named = sum_series([read_series(table, name, idle_confirmed, assumed_zero) for name in NAMED_GPU_COLUMNS])
    substeps = read_series(table, SUBSTEPS, idle_confirmed, assumed_zero)
    water = read_series(table, WATER, idle_confirmed, assumed_zero)
    game_thread_water = sum_series([read_series(table, name, idle_confirmed, assumed_zero) for name in GAME_THREAD_WATER_COLUMNS])
    metric_series = {name: sum_series([read_series(table, column, idle_confirmed, assumed_zero) for column in metric["columns"]]) for name, metric in metrics.items()}
    mask = build_pdh_mask(substeps) if substeps is not None else None
    passed_count = sum(mask) if mask is not None else 0
    named_values = [named[index] for index in range(row_count) if mask is not None and named is not None and mask[index] and named[index] is not None]
    game_thread_values = present_values(game_thread_water)
    substep_values = present_values(substeps)
    summary = {
        "block_id": block["block_id"], "side": side, "rows": row_count, "present": sorted(columns), "assumed_zero": sorted(set(assumed_zero)),
        "step_count_max": step_count_max, "pdh_passed": passed_count, "pdh_ratio": round_four(passed_count / row_count) if row_count else 0.0,
        "pdh_valid": mask is not None and row_count > 0 and passed_count >= PASS_RATIO_MINIMUM * row_count,
        "named_p50": median_of(named_values), "water_p50": median_of(present_values(water)),
        "game_thread_water_p50": median_of(game_thread_values), "game_thread_water_p95": percentile(game_thread_values, 0.95), "game_thread_water_mean": mean_of(game_thread_values),
        "substeps_p50": median_of(substep_values), "substeps_max": max(substep_values) if substep_values else None,
        "pressure_mode_max": max(present_values(columns.get(PRESSURE_MODE)), default=None),
        "frame_p50": median_of(present_values(columns.get(FRAME))), "game_thread_p50": median_of(present_values(columns.get(GAME_THREAD))),
        "tickables_p50": median_of(present_values(columns.get(TICKABLES))), "walk_speed_p50": median_of(present_values(columns.get(WALK_SPEED))),
        "metric_p50": {name: median_of(select_metric_values(metric_series[name], mask, metric["uses_pdh"])) for name, metric in metrics.items()},
    }
    return summary, {"named": named, "mask": mask, "water": water, "series": metric_series}


def load_context(run_dir, table=None):
    table = table or DEFAULT_BUDGET_TABLE
    run_dir = Path(run_dir)
    session_path = run_dir / "session.json"
    if not session_path.is_file():
        return None, f"session.json이 없다: {session_path}"
    session = read_json(session_path)
    plan_path = run_dir / "plan.json"
    plan = read_json(plan_path) if plan_path.is_file() else {}
    context = {"run_dir": run_dir, "session": session, "plan": plan, "records": {record["id"]: record for record in session.get("scenes", [])},
               "plan_scenes": {scene["id"]: scene for scene in plan.get("scenes", [])}, "discard_rows": plan.get("discard_frames", DEFAULT_DISCARD_ROWS),
               "summaries": {}, "frames": {}, "tables": {}, "warnings": [], "analyses": {}, "table": table, "metrics": table["metrics"],
               "wanted_columns": set(WANTED_COLUMNS).union(*(metric["columns"] for metric in table["metrics"].values()))}
    return context, None


def block_csv_path(block, context):
    relative = block.get("csv")
    if not relative:
        return None
    path = context["run_dir"] / relative
    return path if path.is_file() else None


def block_summary(block, context):
    key = block["block_id"]
    if key not in context["summaries"]:
        table = read_block_table(block_csv_path(block, context), context["discard_rows"], context["wanted_columns"])
        context["summaries"][key], context["frames"][key] = summarize_block(block, block["side"], table, context["metrics"])
    return context["summaries"][key]


def slot_name(block):
    return block.get("slot") or RETRY_PATTERN.sub("", block["block_id"])


def group_slot_attempts(record):
    slots = {}
    for block in record.get("blocks", []):
        if block.get("kind", "block") != "block":
            continue
        slots.setdefault(slot_name(block), []).append(block)
    for attempts in slots.values():
        attempts.sort(key=lambda block: block.get("attempt", 0))
    return slots


def choose_attempt(attempts, context):
    return next((block for block in attempts if block.get("status", "ok") == "ok" and block_csv_path(block, context) is not None), None)


def build_rounds(record, context):
    rounds = {}
    missing_csv = []
    for slot, attempts in group_slot_attempts(record).items():
        match = SLOT_PATTERN.search(slot)
        if match is None:
            continue
        number, position, side = int(match.group(1)), int(match.group(2)), match.group(3)
        chosen = choose_attempt(attempts, context)
        if chosen is None and any(block.get("status", "ok") == "ok" for block in attempts):
            missing_csv.append(slot)
        if chosen is not None:
            chosen = {**chosen, "side": side}
        rounds.setdefault(number, {})[position] = {"slot": slot, "side": side, "attempts": len(attempts), "block": chosen}
    return rounds, missing_csv


def collect_side_blocks(rounds):
    sides = {"A": [], "B": []}
    for number in sorted(rounds):
        for position in sorted(rounds[number]):
            slot = rounds[number][position]
            if slot["block"] is not None:
                sides[slot["side"]].append(slot["block"])
    return sides


def normalize_backend(value):
    match = BACKEND_PATTERN.search(str(value))
    return match.group(1).upper() if match else None


def describe_target_problems(actor, side, expected_backend, expected_grid):
    problems = []
    if expected_backend is not None and normalize_backend(actor.get("backend")) != expected_backend:
        problems.append(f"backend {actor.get('backend')} (기대 {expected_backend})")
    if expected_grid is not None and actor.get("grid_resolution") != expected_grid:
        problems.append(f"grid {actor.get('grid_resolution')} (기대 {expected_grid})")
    if bool(actor.get("awake")) != (side == "A"):
        problems.append(f"awake {actor.get('awake')} ({side} 측 기대 {side == 'A'})")
    if "is_using_gpu_water_layer" in actor and expected_backend is not None and bool(actor["is_using_gpu_water_layer"]) != (expected_backend == "GPU"):
        problems.append(f"is_using_gpu_water_layer {actor['is_using_gpu_water_layer']}")
    return problems


def check_actor_records(side_blocks, scene_definition):
    expected_backend, expected_grid, labels = scene_definition["backend"], scene_definition["grid"], scene_definition["target_labels"]
    result = {"expected_backend": expected_backend, "expected_grid": expected_grid, "checked_blocks": 0, "unverified_blocks": 0, "others_unrecorded_blocks": 0, "mismatches": []}
    for side, blocks in side_blocks.items():
        for block in blocks:
            actors = block.get("actors") or []
            targets = [actor for actor in actors if actor.get("label") in labels]
            others = [actor for actor in actors if actor.get("label") not in labels]
            if not targets:
                result["unverified_blocks"] += 1
                continue
            result["checked_blocks"] += 1
            result["others_unrecorded_blocks"] += 0 if others else 1
            for actor in targets:
                problems = describe_target_problems(actor, side, expected_backend, expected_grid)
                if problems:
                    result["mismatches"].append({"block": block["block_id"], "actor": actor.get("label"), "problems": problems})
            for actor in others:
                if actor.get("awake"):
                    result["mismatches"].append({"block": block["block_id"], "actor": actor.get("label"), "problems": ["다른 유체 깨움"]})
    return result


def check_step_counts(side_blocks, context, awake_step_count):
    mismatches = []
    for side, blocks in side_blocks.items():
        for block in blocks:
            step_max = block_summary(block, context)["step_count_max"]
            if step_max is None:
                continue
            if side == "B" and step_max > 0:
                mismatches.append(f"{block['block_id']} 재운 측 {STEP_COUNT} 최대 {step_max:g}(다른 유체가 깨어 있거나 물이 재워지지 않음)")
            if side == "A" and step_max > awake_step_count:
                mismatches.append(f"{block['block_id']} 깨운 측 {STEP_COUNT} 최대 {step_max:g}(다른 유체 깨움)")
            if side == "A" and step_max < 1:
                mismatches.append(f"{block['block_id']} 깨운 측 {STEP_COUNT} 최대 {step_max:g}(물 액터가 스텝하지 않음)")
    return mismatches


def check_water_path(side_blocks, expected_backend, context):
    problems = []
    for block in side_blocks["A"]:
        water = block_summary(block, context)["water_p50"]
        if expected_backend == "GPU" and not (water is not None and water > 0):
            problems.append(f"{block['block_id']} GPU 물 경로 증거 없음({WATER} A p50 {water}, is_using_gpu_water_layer 대용)")
        if expected_backend == "CPU" and water is not None and water > 0:
            problems.append(f"{block['block_id']} CPU 장면인데 {WATER} A p50 {water}")
    return problems


def add_blocker(analysis, reason, detail):
    analysis["blockers"].append({"reason": reason, "detail": detail})


def analyze_scene(context, scene_id):
    if scene_id in context["analyses"]:
        return context["analyses"][scene_id]
    analysis = build_scene_analysis(context, scene_id)
    context["analyses"][scene_id] = analysis
    return analysis


def build_scene_analysis(context, scene_id):
    record = context["records"].get(scene_id)
    definition = context["table"]["scenes"][scene_id]
    backend, grid = definition["backend"], definition["grid"]
    analysis = {"scene": scene_id, "present": record is not None, "blockers": [], "backend": backend, "grid": grid, "definition": definition, "round_numbers": [], "leading_rounds": 0, "pairs": [], "rounds": {}}
    if record is None:
        add_blocker(analysis, "장면 없음", f"{scene_id} 장면이 session.json에 없다")
        return analysis
    plan_meta = (context["plan_scenes"].get(scene_id) or {}).get("meta") or {}
    plan_grid = plan_meta.get("resolution")
    if plan_grid is not None and grid is not None and plan_grid != grid:
        context["warnings"].append(f"{scene_id}: 계획 meta resolution {plan_grid}가 명세 {grid}와 다르다(명세 값 사용)")
    if record.get("error"):
        add_blocker(analysis, "장면 오류", str(record["error"])[:160])
        return analysis
    if (record.get("shader_gate") or {}).get("passed") is False:
        add_blocker(analysis, "셰이더 관문", "예열 셰이더 관문 실패(연속 p50 변화 10% 안·돌출 0)")
    rounds, missing_csv = build_rounds(record, context)
    analysis["rounds"] = rounds
    analysis["round_numbers"] = sorted(rounds)
    if not rounds:
        add_blocker(analysis, "쌍 부족", "측정 블록 없음")
        return analysis
    if missing_csv:
        add_blocker(analysis, "CSV 없음", ", ".join(missing_csv))
        return analysis
    missing_rounds = [number for number in range(1, analysis["round_numbers"][-1] + 1) if number not in rounds]
    if missing_rounds:
        add_blocker(analysis, "쌍 부족", f"라운드 결측: {missing_rounds}")
    while analysis["leading_rounds"] + 1 in rounds:
        analysis["leading_rounds"] += 1
    analysis["side_blocks"] = collect_side_blocks(rounds)
    analysis["pairs"] = build_pairs(rounds, analysis["round_numbers"], context)
    apply_validity_checks(context, analysis)
    return analysis


def apply_validity_checks(context, analysis):
    side_blocks = analysis["side_blocks"]
    actors = check_actor_records(side_blocks, analysis["definition"])
    analysis["actors_check"] = actors
    if actors["mismatches"]:
        summary = "; ".join(f"{item['block']} {item['actor']}: {', '.join(item['problems'])}" for item in actors["mismatches"][:3])
        add_blocker(analysis, "유효성", f"(b) 액터 기록 {len(actors['mismatches'])}건 어긋남: {summary}")
    if actors["unverified_blocks"]:
        context["warnings"].append(f"{analysis['scene']}: 블록 {actors['unverified_blocks']}개에 대상 액터 기록이 없어 backend·grid·깨움을 확인하지 못했다")
    if actors["others_unrecorded_blocks"]:
        context["warnings"].append(f"{analysis['scene']}: 블록 {actors['others_unrecorded_blocks']}개에 다른 유체 액터 기록이 없어 {STEP_COUNT}로만 재움을 확인했다(계획 targets를 all로 두면 기록된다)")
    step_problems = check_step_counts(side_blocks, context, analysis["definition"]["awake_step_count"])
    if step_problems:
        add_blocker(analysis, "유효성", f"(c) 스텝 수 어긋남 {len(step_problems)}건: {'; '.join(step_problems[:3])}")
    if not context["table"]["water_path_check"]:
        return
    water_problems = check_water_path(side_blocks, analysis["backend"], context)
    if water_problems:
        add_blocker(analysis, "유효성", f"(d) 물 경로 어긋남 {len(water_problems)}건: {'; '.join(water_problems[:3])}")
    layer_recorded = any("is_using_gpu_water_layer" in actor for block in side_blocks["A"] for actor in (block.get("actors") or []))
    if analysis["backend"] == "GPU" and not layer_recorded and GPU_LAYER_UNRECORDED_WARNING not in context["warnings"]:
        context["warnings"].append(GPU_LAYER_UNRECORDED_WARNING)


def build_pairs(rounds, round_numbers, context):
    pairs = []
    for number in round_numbers:
        for a_position, b_position in PAIR_POSITIONS:
            a_slot = rounds[number].get(a_position)
            b_slot = rounds[number].get(b_position)
            a_block = a_slot["block"] if a_slot and a_slot["side"] == "A" else None
            b_block = b_slot["block"] if b_slot and b_slot["side"] == "B" else None
            pair = {"order": len(pairs) + 1, "round": number, "discarded": a_block is None or b_block is None,
                    "a": a_block["block_id"] if a_block else (a_slot or {}).get("slot"), "b": b_block["block_id"] if b_block else (b_slot or {}).get("slot"),
                    "a_summary": None, "b_summary": None, "pdh_invalid": False}
            if not pair["discarded"]:
                pair["a_summary"] = block_summary(a_block, context)
                pair["b_summary"] = block_summary(b_block, context)
                pair["pdh_invalid"] = not (pair["a_summary"]["pdh_valid"] and pair["b_summary"]["pdh_valid"])
            pairs.append(pair)
    return pairs


def find_missing_columns(analysis, metric, context):
    if not analysis["pairs"]:
        return []
    missing = []
    for block in analysis["side_blocks"]["A"]:
        present = set(block_summary(block, context)["present"])
        missing.extend(f"A:{name}" for name in metric["needed"] if name not in present and f"A:{name}" not in missing)
    return missing


def check_spread(values):
    median = median_of(values)
    spread = max(values) - min(values)
    if median < STABILITY_SMALL_MEDIAN_MS:
        return {"values": values, "median": median, "range": spread, "kind": "absolute_ms", "value": spread, "limit": STABILITY_SMALL_RANGE_MS, "passed": spread <= STABILITY_SMALL_RANGE_MS + EPSILON}
    relative = spread / median
    return {"values": values, "median": median, "range": spread, "kind": "relative", "value": relative, "limit": STABILITY_RELATIVE, "passed": relative <= STABILITY_RELATIVE + EPSILON}


def block_value(block, metric, context):
    summary = block_summary(block, context)
    if metric["uses_pdh"] and not summary["pdh_valid"]:
        return None
    return summary["metric_p50"][metric["name"]]


def build_round_table(analysis, metric, context):
    table = {}
    for number in analysis["round_numbers"]:
        entry = {"round": number}
        for side in metric["sides"]:
            values = []
            for position in SIDE_POSITIONS[side]:
                slot = analysis["rounds"][number].get(position)
                if not slot or slot["block"] is None or slot["side"] != side:
                    continue
                value = block_value(slot["block"], metric, context)
                if value is not None:
                    values.append(value)
            entry[side] = mean_of(values)
        table[number] = entry
    return table


def build_stability_stage(round_table, round_numbers, sides):
    trimmed = len(round_numbers) > 3
    stage = {"rounds": round_numbers, "trimmed": trimmed, "sides": {}}
    statuses = []
    for side in sides:
        values = [round_table[number][side] for number in round_numbers]
        if None in values:
            stage["sides"][side] = {"values": values, "status": "short"}
            statuses.append("short")
            continue
        used = sorted(values)[1:-1] if trimmed else values
        check = check_spread(used)
        check["all_values"] = values
        check["status"] = "pass" if check["passed"] else "fail"
        stage["sides"][side] = check
        statuses.append(check["status"])
    stage["status"] = "short" if "short" in statuses else ("fail" if "fail" in statuses else "pass")
    return stage


def stage_pairs(analysis, round_count):
    numbers = set(analysis["round_numbers"][:round_count])
    return [pair for pair in analysis["pairs"] if pair["round"] in numbers]


def stage_exclusion_counts(pairs):
    discarded = sum(1 for pair in pairs if pair["discarded"])
    pdh_invalid = sum(1 for pair in pairs if pair["pdh_invalid"])
    return {"pairs": len(pairs), "discarded_pairs": discarded, "clock_exceeded": discarded * 3 > len(pairs),
            "pdh_invalid_pairs": pdh_invalid, "pdh_exceeded": pdh_invalid * 3 > len(pairs)}


def inspect_stage(analysis, metric, round_table, round_count):
    if analysis["leading_rounds"] < round_count:
        return {"rounds": round_count, "gate": "미측정", "usable": False}
    stage = build_stability_stage(round_table, analysis["round_numbers"][:round_count], metric["sides"])
    entry = {"rounds": round_count, "gate": stage["status"], "usable": False, "stability": stage}
    if stage["status"] == "fail":
        return entry
    counts = stage_exclusion_counts(stage_pairs(analysis, round_count))
    entry["exclusions"] = counts
    if counts["clock_exceeded"]:
        entry.update({"terminal": True, "state": "클럭 폐기 초과", "detail": f"{round_count}라운드 쌍 {counts['pairs']}개 중 {counts['discarded_pairs']}개 폐기(1/3 초과)"})
        return entry
    if metric["uses_pdh"] and counts["pdh_exceeded"]:
        entry.update({"terminal": True, "state": "하위 스텝", "detail": f"{round_count}라운드 쌍 {counts['pairs']}개 중 PD-H 무효 블록 쌍 {counts['pdh_invalid_pairs']}개(1/3 초과)"})
        return entry
    entry["usable"] = stage["status"] == "pass"
    return entry


def order_statistic_rank(pair_count):
    if pair_count < 2:
        return None
    return 1 if pair_count <= 7 else 2


def classify_interval(lower, upper, limit):
    if upper <= limit + EPSILON:
        return "통과"
    if lower > limit + EPSILON:
        return "미달"
    return "걸침"


def make_interval(values, rank, limit):
    ordered = sorted(values)
    lower = ordered[rank - 1]
    upper = ordered[-rank]
    return {"lower": round(lower, 6), "upper": round(upper, 6), "rank": rank, "state": classify_interval(round(lower, 6), round(upper, 6), limit)}


def pair_value(pair, metric):
    if pair["discarded"]:
        return None
    a_value = pair["a_summary"]["metric_p50"][metric["name"]]
    if metric["pair"] == "a_only":
        return None if a_value is None else round(a_value, 6)
    b_value = pair["b_summary"]["metric_p50"][metric["name"]]
    return None if a_value is None or b_value is None else round(a_value - b_value, 6)


def usable_pairs(analysis, metric, round_count):
    pairs = stage_pairs(analysis, round_count)
    return [pair for pair in pairs if not (metric["uses_pdh"] and pair["pdh_invalid"]) and pair_value(pair, metric) is not None]


def judge_stage(analysis, metric, limit, round_count):
    pairs = usable_pairs(analysis, metric, round_count)
    outcome = {"pairs": len(pairs)}
    rank = order_statistic_rank(len(pairs))
    if rank is None:
        outcome.update({"state": "쌍 부족", "detail": f"유효 쌍 {len(pairs)}개(최소 2쌍)"})
        return outcome
    values = [pair_value(pair, metric) for pair in pairs]
    outcome["values"] = values
    outcome["interval"] = make_interval(values, rank, limit)
    outcome["state"] = outcome["interval"]["state"]
    return outcome


def walk_stages(analysis, metric, limit, round_table, context):
    trace = []
    for count in STAGE_ROUNDS:
        entry = inspect_stage(analysis, metric, round_table, count)
        trace.append(entry)
        if entry.get("terminal"):
            return trace, None, None
        if not entry["usable"]:
            continue
        outcome = judge_stage(analysis, metric, limit, count)
        entry.update(outcome)
        if outcome["state"] in ("통과", "미달"):
            return trace, count, outcome
    return trace, None, None


def unresolved_reason(trace, limit):
    measured = [entry for entry in trace if entry["gate"] != "미측정"]
    if not measured:
        return "쌍 부족", f"측정 라운드가 {STAGE_ROUNDS[0]}개 미만"
    last = measured[-1]
    if last.get("terminal"):
        return last["state"], last["detail"]
    if last["gate"] == "fail":
        return "불안정", f"{last['rounds']}라운드 측 안정 관문 초과"
    if last["gate"] == "short":
        return "쌍 부족", f"{last['rounds']}라운드 값 일부 없음"
    if last.get("state") == "걸침":
        return "걸침", f"{last['rounds']}라운드({last['pairs']}쌍) 구간이 한도 {limit}에 걸친다"
    return last.get("state", "쌍 부족"), last.get("detail", "")


def collect_gpu_records(analysis, context, round_count):
    pairs = [pair for pair in stage_pairs(analysis, round_count or len(analysis["round_numbers"])) if not pair["discarded"]]
    valid = [pair for pair in pairs if not pair["pdh_invalid"]]
    a_summaries = [pair["a_summary"] for pair in pairs]
    b_summaries = [pair["b_summary"] for pair in pairs]
    differences = [value for value in (pair_value(pair, GPU_METRIC) for pair in valid) if value is not None]
    return {"pairs": len(pairs), "valid_pairs": len(valid), "pdh_ratio_min": min((s["pdh_ratio"] for s in a_summaries + b_summaries), default=None),
            "pdh_ratio_a": [s["pdh_ratio"] for s in a_summaries], "named_a_p50": round_four(median_of([s["named_p50"] for s in a_summaries if s["pdh_valid"] and s["named_p50"] is not None])),
            "named_b_p50": round_four(median_of([s["named_p50"] for s in b_summaries if s["pdh_valid"] and s["named_p50"] is not None])),
            "named_d_p50": round_four(median_of(differences)), "named_d_range": round_four(max(differences) - min(differences)) if differences else None,
            "substeps_a_p50": median_of([s["substeps_p50"] for s in a_summaries if s["substeps_p50"] is not None]), "substeps_a_max": max((s["substeps_max"] for s in a_summaries if s["substeps_max"] is not None), default=None),
            "pressure_mode_max": max((s["pressure_mode_max"] for s in a_summaries if s["pressure_mode_max"] is not None), default=None),
            "game_thread_water_a_p50": round_four(median_of([s["game_thread_water_p50"] for s in a_summaries if s["game_thread_water_p50"] is not None])),
            "game_thread_water_a_p95": round_four(median_of([s["game_thread_water_p95"] for s in a_summaries if s["game_thread_water_p95"] is not None]))}


def decide_interval_row(context, analysis, metric, row_spec, reference):
    row = {"row": row_spec["row"], "scene": row_spec["scene"], "metric": metric["name"], "label": row_spec["label"], "limit_ms": row_spec["limit"], "verdict": "판정 불가", "reason": None,
           "reasons": [dict(item) for item in analysis["blockers"]], "stage_rounds": None, "trace": [], "outcome": None}
    if row_spec.get("source") is not None:
        row["source"] = row_spec["source"]
    if reference is not None and not reference["valid"]:
        row["reasons"].append({"reason": "유효성", "detail": f"(a) {reference['scene']} 세션 무효({reference['status']}): {reference['detail']}"})
    missing = find_missing_columns(analysis, metric, context)
    if missing:
        row["reasons"].insert(0, {"reason": "필수 열 없음", "detail": ", ".join(missing)})
        return finish_row(row, analysis, context, None, metric)
    if not analysis["pairs"]:
        return finish_row(row, analysis, context, None, metric)
    round_table = build_round_table(analysis, metric, context)
    trace, final_count, outcome = walk_stages(analysis, metric, row_spec["limit"], round_table, context)
    row.update({"trace": trace, "stage_rounds": final_count, "outcome": outcome, "round_table": [round_table[number] for number in sorted(round_table)]})
    if outcome is None:
        reason, detail = unresolved_reason(trace, row_spec["limit"])
        row["reasons"].append({"reason": reason, "detail": detail, "kind": "unresolved"})
    if outcome is not None and not row["reasons"]:
        row["verdict"] = outcome["state"]
    return finish_row(row, analysis, context, final_count, metric)


def view_pairs(analysis, metric, final_count):
    used = {pair["order"] for pair in usable_pairs(analysis, metric, final_count)} if final_count else set()
    return [{"order": pair["order"], "round": pair["round"], "a": pair["a"], "b": pair["b"], "discarded": pair["discarded"], "pdh_invalid": pair["pdh_invalid"],
             "used": pair["order"] in used, "value": pair_value(pair, metric)} for pair in analysis["pairs"]]


def finish_row(row, analysis, context, final_count, metric):
    if row["reasons"]:
        row["verdict"] = "판정 불가"
    row["reason"] = row["reasons"][0]["reason"] if row["reasons"] else None
    if analysis["pairs"]:
        row["records"] = collect_gpu_records(analysis, context, final_count)
        row["pair_view"] = view_pairs(analysis, metric, final_count)
    return row


def evaluate_moving_frames(context, analysis, round_count, ratio_limit):
    blocks = [pair for pair in stage_pairs(analysis, round_count or len(analysis["round_numbers"])) if not pair["discarded"] and not pair["pdh_invalid"]]
    moving_values = []
    all_values = []
    skipped_blocks = []
    for pair in blocks:
        frames = context["frames"][pair["a"]]
        indices = [index for index, passed in enumerate(frames["mask"]) if passed and frames["named"][index] is not None and frames["water"][index] is not None]
        water_median = median_of([frames["water"][index] for index in indices])
        if not water_median or water_median <= 0:
            skipped_blocks.append(pair["a"])
            continue
        threshold = MOVING_THRESHOLD_RATIO * water_median
        all_values.extend(frames["named"][index] for index in indices)
        moving_values.extend(frames["named"][index] for index in indices if frames["water"][index] > threshold)
    result = {"definition": MOVING_DEFINITION, "blocks": len(blocks), "skipped_blocks": skipped_blocks, "frames": len(all_values), "moving_frames": len(moving_values),
              "moving_p95": percentile(moving_values, 0.95), "scene_mean": mean_of(all_values), "ratio_limit": ratio_limit}
    if not moving_values or not all_values:
        result.update({"state": "판정 불가", "detail": "이동 프레임 0개(근사 정의로 표본 없음)" if all_values else "PD-H 통과 A 블록 없음"})
        return result
    result["limit_ms"] = ratio_limit * result["scene_mean"]
    result["state"] = "통과" if result["moving_p95"] <= result["limit_ms"] + EPSILON else "미달"
    return result


def check_walk_evidence(context, analysis):
    speeds = [block_summary(block, context)["walk_speed_p50"] for block in analysis["side_blocks"]["A"]]
    known = [speed for speed in speeds if speed is not None]
    if not known:
        context["warnings"].append(f"{analysis['scene']}: 블록에 {WALK_SPEED} 열이 없어 걷기를 확인하지 못했다")
        return None
    slow = [speed for speed in known if speed < MINIMUM_WALK_SPEED]
    return None if not slow else f"{analysis['scene']} A 블록 {WALK_SPEED} p50 {min(slow):.1f}cm/s < {MINIMUM_WALK_SPEED:g}(폰이 걷지 않음)"


def decide_walk_row(context, row_spec, reference):
    analysis = analyze_scene(context, row_spec["scene"])
    metric = context["metrics"][row_spec["metric"]]
    game_thread = decide_interval_row(context, analysis, metric, row_spec, reference)
    row = {**game_thread, "metric": f"{metric['name']}_and_moving_gpu", "game_thread_verdict": game_thread["verdict"], "reasons": list(game_thread["reasons"]), "moving": None}
    if not analysis["pairs"]:
        return combine_walk_verdict(row)
    gpu_missing = find_missing_columns(analysis, GPU_METRIC, context)
    if gpu_missing:
        row["reasons"].append({"reason": "필수 열 없음", "detail": ", ".join(gpu_missing)})
        return combine_walk_verdict(row)
    walk_problem = check_walk_evidence(context, analysis)
    if walk_problem is not None:
        row["reasons"].append({"reason": "유효성", "detail": walk_problem})
        return combine_walk_verdict(row)
    row["moving"] = evaluate_moving_frames(context, analysis, row["stage_rounds"], row_spec["moving_p95_ratio"])
    if row["moving"]["state"] == "판정 불가":
        row["reasons"].append({"reason": "이동 프레임", "detail": row["moving"]["detail"], "kind": "unresolved"})
    return combine_walk_verdict(row)


def combine_walk_verdict(row):
    moving_state = (row.get("moving") or {}).get("state")
    states = [row["game_thread_verdict"], moving_state]
    row["reason"] = row["reasons"][0]["reason"] if row["reasons"] else None
    if any(item.get("kind") != "unresolved" for item in row["reasons"]):
        row["verdict"] = "판정 불가"
        return row
    if "미달" in states:
        row["verdict"] = "미달"
        return row
    row["verdict"] = "통과" if states == ["통과", "통과"] else "판정 불가"
    return row


def decide_reference(context, water_reference_ms):
    settings = context["table"]["reference"]
    if settings is None:
        return None
    scene_id, metric, ratio_range = settings["scene"], context["metrics"][settings["metric"]], settings["ratio_range"]
    analysis = analyze_scene(context, scene_id)
    row = {"row": scene_id, "scene": scene_id, "water_ref_ms": water_reference_ms, "range_ratio": list(ratio_range), "range_ms": [round(water_reference_ms * ratio, 6) for ratio in ratio_range],
           "named_a_p50": None, "ratio": None, "valid": False, "status": None, "detail": ""}
    first_scene = (context["session"].get("scenes") or [{}])[0].get("id")
    row["is_first_scene"] = first_scene == scene_id
    if analysis["present"] and not row["is_first_scene"]:
        context["warnings"].append(f"{scene_id}가 첫 장면이 아니다(첫 장면 {first_scene})")
    if analysis["blockers"]:
        row.update({"status": analysis["blockers"][0]["reason"], "detail": "; ".join(item["detail"] for item in analysis["blockers"])})
        return row
    missing = find_missing_columns(analysis, metric, context)
    if missing:
        row.update({"status": "필수 열 없음", "detail": ", ".join(missing)})
        return row
    values = [block_value(block, metric, context) for block in analysis["side_blocks"]["A"]]
    valid_values = [value for value in values if value is not None]
    row["pdh_valid_a_blocks"] = f"{len(valid_values)}/{len(values)}"
    if not valid_values:
        status, detail = ("하위 스텝", "PD-H 유효 A 블록이 없다") if metric["uses_pdh"] else ("값 없음", f"A 블록 {metric['label']} 값이 없다")
        row.update({"status": status, "detail": detail})
        return row
    row["named_a_p50"] = median_of(valid_values)
    row["ratio"] = row["named_a_p50"] / water_reference_ms
    row["valid"] = ratio_range[0] - EPSILON <= row["ratio"] <= ratio_range[1] + EPSILON
    row["status"] = "유효" if row["valid"] else "범위 밖"
    row["detail"] = "" if row["valid"] else f"{scene_id} {metric['label']} A p50 {round_four(row['named_a_p50'])}ms가 기준 {water_reference_ms}ms의 {ratio_range[0]}~{ratio_range[1]}배({row['range_ms'][0]}~{row['range_ms'][1]}ms) 밖"
    return row


def build_record_row(context, scene_id, game_thread_focus):
    analysis = analyze_scene(context, scene_id)
    row = {"row": f"{scene_id}(기록)", "scene": scene_id, "blockers": analysis["blockers"], "records": None}
    if not analysis["pairs"]:
        return row
    pairs = [pair for pair in analysis["pairs"] if not pair["discarded"]]
    a_summaries = [pair["a_summary"] for pair in pairs]
    b_summaries = [pair["b_summary"] for pair in pairs]
    differences = {name: [pair["a_summary"][key] - pair["b_summary"][key] for pair in pairs if pair["a_summary"][key] is not None and pair["b_summary"][key] is not None]
                   for name, key in (("frame_d_p50", "frame_p50"), ("game_thread_d_p50", "game_thread_p50"), ("tickables_d_p50", "tickables_p50"))}
    row["records"] = {"pairs": len(pairs), "game_thread_d_p50": round_four(median_of(differences["game_thread_d_p50"])), "tickables_d_p50": round_four(median_of(differences["tickables_d_p50"])),
                      "frame_d_p50": round_four(median_of(differences["frame_d_p50"])),
                      "game_thread_water_a_p50": round_four(median_of([s["game_thread_water_p50"] for s in a_summaries if s["game_thread_water_p50"] is not None]))}
    if not game_thread_focus:
        row["records"].update(collect_gpu_records(analysis, context, None))
    return row


def decide_budget(reference, rows):
    verdicts = [row["verdict"] for row in rows.values()]
    if "미달" in verdicts:
        return "미충족"
    if "판정 불가" in verdicts or (reference is not None and not reference["valid"]):
        return "판정 불가"
    return "충족(proposed)"


BLOCK_TABLE_KEYS = ("block_id", "side", "rows", "pdh_ratio", "pdh_valid", "named_p50", "water_p50", "game_thread_water_p50", "game_thread_water_p95", "substeps_p50", "substeps_max", "pressure_mode_max",
                    "step_count_max", "walk_speed_p50", "assumed_zero")


def describe_block(summary, backend, context):
    entry = {key: summary[key] for key in BLOCK_TABLE_KEYS}
    if backend == "CPU" or not any(metric["uses_pdh"] for metric in list_pilot_metrics(context)):
        entry.update({"pdh_ratio": None, "pdh_valid": None})
    return entry


def build_block_table(context):
    table = {}
    for scene_id, analysis in context["analyses"].items():
        if not analysis["pairs"]:
            continue
        table[scene_id] = [describe_block(block_summary(block, context), analysis["backend"], context) for side in ("A", "B") for block in analysis["side_blocks"][side]]
    return table


def decide_table_row(context, row_spec, reference):
    if row_spec["kind"] == "walk":
        return decide_walk_row(context, row_spec, reference)
    return decide_interval_row(context, analyze_scene(context, row_spec["scene"]), context["metrics"][row_spec["metric"]], row_spec, reference)


def attach_table_source(result, context):
    if context["table"]["source_file"] is not None:
        result["budget_table"] = context["table"]["source_file"]
    return result


def run_judge(context, water_reference_ms):
    table = context["table"]
    reference = decide_reference(context, water_reference_ms)
    rows = {row_spec["row"]: decide_table_row(context, row_spec, reference) for row_spec in table["rows"]}
    records = {record["scene"]: build_record_row(context, record["scene"], record["game_thread_focus"]) for record in table["records"]}
    budget_verdict = decide_budget(reference, rows)
    return attach_table_source({"mode": "judge", "run_dir": str(context["run_dir"]), "run_token": context["session"].get("run_token"), "water_ref_ms": water_reference_ms, "reference": reference, "rows": rows,
                                "records": records, "blocks": build_block_table(context), "budget_verdict": budget_verdict, "consequence": table["consequences"][budget_verdict],
                                "asset_hashes": context["session"].get("asset_hashes"), "build_gate": context["session"].get("build_gate"), "warnings": context["warnings"]}, context)


def list_pilot_metrics(context):
    table = context["table"]
    names = [table["reference"]["metric"]] if table["reference"] is not None else []
    for row_spec in table["rows"]:
        names.append(row_spec["metric"])
        if row_spec["kind"] == "walk":
            names.append(GPU_METRIC["name"])
    return [context["metrics"][name] for name in dict.fromkeys(names)]


def find_walk_row(context, scene_id):
    return next((row_spec for row_spec in context["table"]["rows"] if row_spec["kind"] == "walk" and row_spec["scene"] == scene_id), None)


def pilot_scene(context, scene_id):
    analysis = analyze_scene(context, scene_id)
    entry = {"scene": scene_id, "present": analysis["present"], "backend": analysis["backend"], "grid": analysis["grid"], "blockers": analysis["blockers"], "blocks": [], "missing_columns": {}}
    if not analysis["pairs"]:
        return entry
    for side in ("A", "B"):
        for block in analysis["side_blocks"][side]:
            entry["blocks"].append(describe_block(block_summary(block, context), analysis["backend"], context))
    for metric in list_pilot_metrics(context):
        if not metric["backends"] or analysis["backend"] in metric["backends"]:
            entry["missing_columns"][metric["label"]] = find_missing_columns(analysis, metric, context)
    entry["records"] = collect_gpu_records(analysis, context, None) if analysis["backend"] == "GPU" and analysis["pairs"] else None
    walk_row = find_walk_row(context, scene_id)
    if walk_row is not None:
        entry["moving"] = evaluate_moving_frames(context, analysis, None, walk_row["moving_p95_ratio"]) if not entry["missing_columns"].get(GPU_METRIC["label"]) else None
        entry["walk_problem"] = check_walk_evidence(context, analysis)
    return entry


def run_pilot(context, water_reference_ms):
    table = context["table"]
    scenes = {scene_id: pilot_scene(context, scene_id) for scene_id in table["scenes"] if scene_id in context["records"]}
    for scene_id in table["scenes"]:
        if scene_id not in context["records"]:
            context["warnings"].append(f"{scene_id}: session.json에 장면이 없다")
    ready = bool(scenes) and all(not entry["blockers"] and not any(entry["missing_columns"].values()) and not entry.get("walk_problem") for entry in scenes.values())
    reference_scene = table["reference"]["scene"] if table["reference"] is not None else None
    reference = decide_reference(context, water_reference_ms) if water_reference_ms and reference_scene in scenes else None
    invalid_blocks = [block["block_id"] for entry in scenes.values() for block in entry["blocks"] if block["side"] == "A" and block["pdh_valid"] is False]
    return attach_table_source({"mode": "pilot", "run_dir": str(context["run_dir"]), "run_token": context["session"].get("run_token"), "scenes": scenes, "reference": reference, "ready": ready,
                                "pdh_invalid_blocks": invalid_blocks, "warnings": context["warnings"]}, context)


def clean(value):
    if isinstance(value, dict):
        return {str(key): clean(item) for key, item in value.items() if not str(key).endswith("_summary")}
    if isinstance(value, (list, tuple, set)):
        return [clean(item) for item in value]
    if isinstance(value, float):
        return None if math.isnan(value) or math.isinf(value) else round(value, 6)
    return value


def format_number(value):
    return "-" if value is None else f"{value:.4f}"


def format_pilot(result):
    lines = [f"[pilot] {result['run_dir']}"] + describe_table_source(result)
    for scene_id, entry in result["scenes"].items():
        lines.append(f"{scene_id} {entry['backend']} {entry['grid']}: 사유 {[item['reason'] for item in entry['blockers']] or '없음'}")
        for label, names in entry["missing_columns"].items():
            lines.append(f"  열 {label}: {'누락 ' + ', '.join(names) if names else '있음'}")
        for block in entry["blocks"]:
            pdh_text = "해당 없음(CPU)" if block["pdh_valid"] is None else f"{block['pdh_ratio']} {'유효' if block['pdh_valid'] else '무효'}"
            lines.append(f"  {block['block_id']} PD-H {pdh_text} 명명GPU p50 {format_number(block['named_p50'])} GT물합 p50 {format_number(block['game_thread_water_p50'])} 하위스텝 최대 {block['substeps_max']} 스텝수 최대 {block['step_count_max']}")
        if entry.get("records"):
            lines.append(f"  대략 A {format_number(entry['records']['named_a_p50'])} B {format_number(entry['records']['named_b_p50'])} d {format_number(entry['records']['named_d_p50'])} GT물합 A {format_number(entry['records']['game_thread_water_a_p50'])}")
        if entry.get("moving"):
            lines.append(f"  이동 프레임 {entry['moving']['moving_frames']}/{entry['moving']['frames']} p95 {format_number(entry['moving']['moving_p95'])} 평균 {format_number(entry['moving']['scene_mean'])}")
    if result["reference"]:
        lines.append(f"{result['reference']['scene']} 기준: {result['reference']['status']} 비율 {format_number(result['reference']['ratio'])}")
    lines.append(f"PD-H 무효 A 블록: {result['pdh_invalid_blocks'] or '없음'}")
    lines.append(f"시운전 준비: {'예' if result['ready'] else '아니오'}")
    lines.extend(f"경고: {warning}" for warning in result["warnings"])
    return lines


def format_interval(outcome):
    if not outcome or "interval" not in outcome:
        return "구간 없음"
    interval = outcome["interval"]
    return f"[{format_number(interval['lower'])}, {format_number(interval['upper'])}] 순위 {interval['rank']} 쌍 {outcome['pairs']}"


def describe_table_source(result):
    source = result.get("budget_table")
    return [] if source is None else [f"예산 표: {source['area']} {source['path']} sha256 {source['sha256']}"]


def describe_reference(reference):
    if reference is None:
        return "기준 장면: 없음(예산 표 reference null)"
    return f"{reference['scene']} 기준: {reference['status']} A p50 {format_number(reference['named_a_p50'])} 기준 {reference['water_ref_ms']} 범위 {reference['range_ms']} {reference['detail']}"


def format_judge(result):
    lines = [f"[judge] {result['run_dir']}"] + describe_table_source(result) + [describe_reference(result["reference"])]
    for name, row in result["rows"].items():
        reason = f"({row['reason']})" if row["reason"] else ""
        lines.append(f"{name} {row['scene']}: {row['verdict']}{reason} {row['label']} 한도 {row['limit_ms']}ms 라운드 {row['stage_rounds']} {format_interval(row['outcome'])}")
        for entry in row["trace"]:
            lines.append(f"  경로 {entry['rounds']}라운드 관문 {entry['gate']} 상태 {entry.get('state', '-')} 쌍 {entry.get('pairs', '-')}")
        if row.get("moving"):
            lines.append(f"  이동 프레임 {row['moving']['moving_frames']}개 p95 {format_number(row['moving']['moving_p95'])} 한도 {format_number(row['moving'].get('limit_ms'))} {row['moving']['state']}")
        lines.extend(f"  사유 {item['reason']}: {item['detail']}" for item in row["reasons"])
    for name, record in result["records"].items():
        lines.append(f"기록 {name}: {record['records']} 사유 {[item['reason'] for item in record['blockers']]}")
    for scene_id, blocks in result["blocks"].items():
        ratios = [block["pdh_ratio"] for block in blocks if block["pdh_ratio"] is not None]
        invalid = [block["block_id"] for block in blocks if block["pdh_valid"] is False]
        if ratios:
            lines.append(f"PD-H 통과 비율 {scene_id}: 최소 {format_number(min(ratios))} 무효 블록 {invalid or '없음'}")
    lines.append(f"예산 판정: {result['budget_verdict']} - {result['consequence']}")
    lines.extend(f"경고: {warning}" for warning in result["warnings"])
    return lines


def main():
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description="영역 예산 PIE 판정 분석기(기본 표 FX-4b 물)")
    parser.add_argument("mode", choices=["pilot", "judge"])
    parser.add_argument("run_dir", type=Path)
    parser.add_argument("--budget-table", type=Path)
    parser.add_argument("--water-ref-ms", "--ref-ms", dest="water_ref_ms", type=float)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    table = DEFAULT_BUDGET_TABLE
    if args.budget_table is not None:
        table, errors = load_budget_table(args.budget_table)
        if errors:
            print(f"예산 표 오류({args.budget_table}): " + "; ".join(errors), file=sys.stderr)
            return 2
    if table["reference"] is None and args.water_ref_ms is not None:
        parser.error("예산 표에 reference가 없으니 --water-ref-ms를 주지 않는다")
    if args.mode == "judge" and table["reference"] is not None and (args.water_ref_ms is None or args.water_ref_ms <= 0):
        parser.error("judge에는 양수 --water-ref-ms가 필요하다")
    context, error = load_context(args.run_dir, table)
    if error is not None:
        print(error, file=sys.stderr)
        return 2
    result = run_pilot(context, args.water_ref_ms) if args.mode == "pilot" else run_judge(context, args.water_ref_ms)
    cleaned = clean(result)
    print("\n".join(format_pilot(cleaned) if args.mode == "pilot" else format_judge(cleaned)))
    if args.out is not None:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(cleaned, ensure_ascii=False, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
