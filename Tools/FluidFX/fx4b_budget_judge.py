"""실행 환경: 시스템 Python 3(표준 라이브러리만), 에디터·빌드 불필요. 입력은 pie_profile_fluid.py --csv-gpu 실행 폴더(Saved/FluidFX/fx7p/<run>/, session.json·plan.json·csv/ 블록 CSV)와 선택 영역 예산 표 JSON(없으면 내장 FX-4b 물 표, 같은 내용 파일 Tools/FluidFX/perf/fixtures/fx4b_water_budget.json).
실행 명령: python -B Tools/FluidFX/fx4b_budget_judge.py pilot|judge <run_dir> [--budget-table <영역 예산 표 json>] [--water-ref-ms|--ref-ms <기준 ms, 표에 reference가 있으면 judge 필수·없으면 금지>] [--out <json>]
출력: 표준 출력에 사람이 읽는 요약, --out이면 같은 내용을 JSON으로 저장(pilot = 장면별 표 지표 필수 열·PD-H 통과 비율·대략값, judge = 기준 장면 유효성·표의 판정 행(구간·걷기)·기록 행·예산 판정, --budget-table이면 budget_table{path,sha256,area} 추가). 판정 불가는 이름 붙은 사유, 예산 표 오류는 종료 2. 실행 plan.json 최상위에 _p가 있으면 P 방식(runbook-p-v3 5절 T2): --budget-table 필수, --out이 이미 있으면 종료 2, 쓰기는 임시 파일→fsync→os.replace(indent 2·끝 줄바꿈), pilot = 장면별 regime·evidence·backend_evidence·header·header_defects·viewport·sp·pairs·error, judge = 행별 verdict(통과|미달|판정 불가|측정 불가)·reason·interval·d·d_prime·rt_control·p95_record, 종료 0 = 썼음(판정과 무관).
상태: 2026-10-30 CAL-0 B7 영역 예산 표 일반화(내장 표 = 2026-10-24 FX-4b 판정, 출력 동일. 명세 Saved/AgentOps/20261024/fx4b-acc/spec-fx4b-budget.md 1~5절, fx7pr_judge.py의 구간·안정·클럭 규칙 계승), 시험은 그 폴더 budget-fixtures/run_fixtures.py(FX-4b)와 Tools/FluidFX/perf/fixtures/check_budget_table_fixtures.py(일반 표). 2026-11-06 P 방식 추가(PR-4·PR-5·PR-7·PR-8, 체제는 fx7pr_judge.check_regime 재사용), 시험 Tools/FluidFX/perf/p/fixtures_t2.py.
"""
import argparse
import csv
import functools
import hashlib
import importlib.util
import json
import math
import os
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

P_PLAN_KEY = "_p"
GPU_TIME = "GPUTime"
CORE_COLUMNS = (FRAME, GAME_THREAD, GPU_TIME)
RENDER_THREAD_PREFIX = "Exclusive/RenderThread/"
RENDER_THREAD_WAIT_PREFIX = "Exclusive/RenderThread/EventWait"
RENDER_THREAD_CORE = "Exclusive/RenderThread/*"
RENDER_THREAD_SUM_PREFIX = {"include": RENDER_THREAD_PREFIX, "exclude": RENDER_THREAD_WAIT_PREFIX}
RENDER_THREAD_CONTROL_COLUMNS = ("Exclusive/RenderThread/PrepareDistanceFieldScene", "Exclusive/RenderThread/SkyAtmosphere", "Exclusive/RenderThread/RenderPostProcessing")
SCREEN_PERCENTAGE = "r.ScreenPercentage"
OVERRIDE_PIE_SCREEN_PERCENTAGE = "r.Editor.Viewport.OverridePIEScreenPercentage"
WATER_GPU_VARIABLE = "TD.FluidFX.WaterGpu"
REFERENCE_PIXELS = 1920 * 1080
CPU_GRID_RANGE = (16, 256)
GPU_GRID_RANGE = (16, 2048)
READBACK_TOLERANCE = 1e-6
BACKEND_SETTING_PATTERN = re.compile(r"(GPU|CPU|AUTO)", re.IGNORECASE)
TOGGLE_SYMBOLS = ("L", "Ls", "Lb", "C", "X", "Xn")
SLEEP_TOGGLES = ("L", "Ls", "Lb")
ACTOR_TOGGLES = ("X", "Xn")
AWAKE_SIDES = ("A", "both", "none")
PART_NAMES = ("cards", "volumes", "embers")
P_SCENE_KEYS = SCENE_KEYS | {"toggle", "awake_sides", "water_layer", "regime", "evidence"}
P_REQUIRED_SCENE_KEYS = ("backend", "grid", "target_labels", "awake_step_count", "toggle", "awake_sides", "water_layer", "regime", "evidence")
P_METRIC_KEYS = METRIC_KEYS | {"sum_prefix", "pixel_scale", "p95_record"}
P_ROW_KEYS = ROW_KEYS | {"ledger_row"}
EVIDENCE_KEYS = ("step_count", "columns", "parts", "light", "console", "actor")
ACTOR_READBACK_KEYS = {"hidden": "hidden", "niagara": "ns_inactive"}
MEASURE_IMPOSSIBLE = "측정 불가"
UNDECIDABLE = "판정 불가"
PIXEL_SPLIT = "화소 환산 갈림"
P_FINAL_STATES = ("통과", "미달", PIXEL_SPLIT)
P_REASON_NAMES = {"클럭 폐기 초과": "클럭"}
HEADER_DEFECT_REASON = "열 결함 의심(PR-4)"
COLUMN_MISSING_REASON = "열 없음"
COLUMN_DEFECT_PREFIX = "열 결함: "

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
            "sides": ("A", "B") if pair == "difference" else ("A",), "uses_pdh": pdh, "pair": pair, "backends": tuple(definition.get("backends", ())),
            "sum_prefix": definition.get("sum_prefix"), "pixel_scale": definition.get("pixel_scale", False), "p95_record": definition.get("p95_record", False)}


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


def find_table_basic_errors(table):
    errors = find_unknown_keys(table, TABLE_KEYS, "표")
    if table.get("version") != TABLE_VERSION:
        errors.append(f"version {table.get('version')!r}(기대 {TABLE_VERSION})")
    if not isinstance(table.get("area"), str) or not table.get("area"):
        errors.append("area: 비지 않은 문자열 필요")
    if not isinstance(table.get("water_path_check"), bool):
        errors.append("water_path_check: 참·거짓 필요(물 표만 참)")
    if not isinstance(table.get("target_label", ""), str):
        errors.append("target_label: 문자열")
    return errors


def find_consequence_errors(table):
    consequences = table.get("consequences", {})
    if not isinstance(consequences, dict) or any(key not in BUDGET_VERDICTS or not isinstance(text, str) for key, text in consequences.items()):
        return [f"consequences: 키는 {BUDGET_VERDICTS} 중 하나, 값은 문자열"]
    return []


def find_table_errors(table):
    if not isinstance(table, dict):
        return ["표 최상위가 객체가 아니다"]
    errors = find_table_basic_errors(table)
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
    errors.extend(find_consequence_errors(table))
    return errors


def parse_toggle_symbols(toggle):
    if not isinstance(toggle, str) or not toggle:
        return None
    symbols = tuple(toggle.split("+"))
    if not all(symbol in TOGGLE_SYMBOLS for symbol in symbols):
        return None
    if len(symbols) > 1 and not (symbols[0] in SLEEP_TOGGLES and all(symbol in ACTOR_TOGGLES for symbol in symbols[1:])):
        return None
    return symbols


def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def is_text_list(value):
    return isinstance(value, list) and all(isinstance(item, str) and item for item in value) and len(set(value)) == len(value)


def find_evidence_errors(where, evidence, symbols):
    where = f"{where}.evidence"
    if not isinstance(evidence, dict):
        return [f"{where}: 객체가 아니다"]
    errors = find_unknown_keys(evidence, set(EVIDENCE_KEYS), where)
    errors.extend(f"{where}: {key} 필요" for key in EVIDENCE_KEYS if key not in evidence)
    if errors:
        return errors
    if not isinstance(evidence["step_count"], bool):
        errors.append(f"{where}.step_count: 참·거짓")
    if not is_text_list(evidence["columns"]):
        errors.append(f"{where}.columns: 겹치지 않는 열 이름 목록")
    parts = evidence["parts"]
    if not isinstance(parts, dict) or not all(isinstance(label, str) and label and isinstance(names, list) and names and all(name in PART_NAMES for name in names) for label, names in parts.items()):
        errors.append(f"{where}.parts: {{라벨: [{'·'.join(PART_NAMES)}]}}")
    if not isinstance(evidence["light"], bool):
        errors.append(f"{where}.light: 참·거짓")
    console = evidence["console"]
    if not isinstance(console, dict) or not all(isinstance(values, dict) and set(values) == {"A", "B"} and all(is_number(value) for value in values.values()) for values in console.values()):
        errors.append(f"{where}.console: {{CVar: {{A: 숫자, B: 숫자}}}}")
    actor = evidence["actor"]
    if actor is not None and not (isinstance(actor, dict) and set(actor) <= set(ACTOR_READBACK_KEYS) and all(isinstance(flag, bool) for flag in actor.values())):
        errors.append(f"{where}.actor: null 또는 {{hidden: 참·거짓, niagara: 참·거짓}}")
    if errors or symbols is None:
        return errors
    has_console_toggle = "C" in symbols
    if has_console_toggle != bool(console) or has_console_toggle != bool(evidence["columns"]):
        errors.append(f"{where}: console·columns는 C 토글에만 있고 C 토글에는 둘 다 있어야 한다(PR-8 ②)")
    expected_actor = {"hidden": "X" in symbols, "niagara": "Xn" in symbols}
    if not any(expected_actor.values()):
        if actor is not None:
            errors.append(f"{where}.actor: X·Xn 토글이 없으면 null")
        return errors
    if actor is None or {key: bool(actor.get(key)) for key in expected_actor} != expected_actor:
        errors.append(f"{where}.actor: 토글 {'+'.join(symbols)}이면 {expected_actor}(PR-8 ③)")
    return errors


def find_p_scene_errors(scene_id, scene):
    where = f"장면 {scene_id}"
    if not isinstance(scene, dict):
        return [f"{where}: 객체가 아니다"]
    errors = find_unknown_keys(scene, P_SCENE_KEYS, where)
    errors.extend(f"{where}: {key} 필요(P 표)" for key in P_REQUIRED_SCENE_KEYS if key not in scene)
    if errors:
        return errors
    if scene["backend"] not in BACKENDS + (None,):
        errors.append(f"{where}: backend {scene['backend']!r}는 GPU·CPU·null 중 하나")
    labels = scene["target_labels"] if is_text_list(scene["target_labels"]) else None
    if labels is None:
        errors.append(f"{where}: target_labels는 겹치지 않는 액터 이름 목록")
    grid = scene["grid"]
    if not isinstance(grid, dict) or not all(isinstance(label, str) and label and is_integer(size) and size > 0 for label, size in grid.items()):
        errors.append(f"{where}: grid는 {{라벨: 양의 정수}}(P 표, E1)")
    elif labels is not None and not set(grid) <= set(labels):
        errors.append(f"{where}: grid 라벨 {sorted(set(grid) - set(labels))}이 target_labels에 없다")
    symbols = parse_toggle_symbols(scene["toggle"])
    if symbols is None:
        errors.append(f"{where}: toggle {scene['toggle']!r}는 {'·'.join(TOGGLE_SYMBOLS)} 하나 또는 L·Ls·Lb+X·Xn(3.4)")
    if scene["awake_sides"] not in AWAKE_SIDES:
        errors.append(f"{where}: awake_sides {scene['awake_sides']!r}는 {AWAKE_SIDES} 중 하나")
    for key in ("water_layer", "regime"):
        if not isinstance(scene[key], bool):
            errors.append(f"{where}: {key}는 참·거짓")
    count = scene["awake_step_count"]
    if labels:
        if not (is_integer(count) and count >= 1):
            errors.append(f"{where}: awake_step_count는 1 이상 정수(대상 있음)")
        if scene["awake_sides"] == "none":
            errors.append(f"{where}: 대상이 있으면 awake_sides는 A·both")
    if labels == []:
        if symbols is not None and not all(symbol in ACTOR_TOGGLES for symbol in symbols):
            errors.append(f"{where}: 대상 액터 이름이 없다(X·Xn만인 장면만 target_labels [] 허용)")
        if count != 0 or not is_integer(count):
            errors.append(f"{where}: 대상이 없으면 awake_step_count는 0")
        if scene["awake_sides"] != "none":
            errors.append(f"{where}: 대상이 없으면 awake_sides는 none")
    errors.extend(find_evidence_errors(where, scene["evidence"], symbols))
    return errors


def find_p_metric_errors(name, metric):
    where = f"지표 {name}"
    if name in BUILTIN_METRICS:
        return [f"{where}: 내장 지표 이름과 겹친다"]
    if not isinstance(metric, dict):
        return [f"{where}: 객체가 아니다"]
    errors = find_unknown_keys(metric, P_METRIC_KEYS, where)
    columns = metric.get("columns")
    sum_prefix = metric.get("sum_prefix")
    if sum_prefix is None and not (isinstance(columns, list) and columns and all(isinstance(column, str) and column for column in columns)):
        errors.append(f"{where}: columns는 비지 않은 열 이름 목록")
    if sum_prefix is not None and (sum_prefix != RENDER_THREAD_SUM_PREFIX or columns != [] or metric.get("pair", "difference") != "difference"):
        errors.append(f"{where}: sum_prefix 지표는 RTSUM(C8) {RENDER_THREAD_SUM_PREFIX}·columns []·pair difference만")
    if metric.get("pair", "difference") not in PAIR_KINDS:
        errors.append(f"{where}: pair {metric.get('pair')!r}는 {PAIR_KINDS} 중 하나")
    if metric.get("pdh", False) is not False:
        errors.append(f"{where}: P 표 지표는 pdh 거짓")
    backends = metric.get("backends", [])
    if not isinstance(backends, list) or any(backend not in BACKENDS for backend in backends):
        errors.append(f"{where}: backends는 {BACKENDS} 목록")
    if not isinstance(metric.get("label", name), str):
        errors.append(f"{where}: label은 문자열")
    for key in ("pixel_scale", "p95_record"):
        if not isinstance(metric.get(key, False), bool):
            errors.append(f"{where}: {key}는 참·거짓")
    if metric.get("pixel_scale") and (columns != [FRAME] or metric.get("pair", "difference") != "difference"):
        errors.append(f"{where}: pixel_scale은 {FRAME} 쌍 차분 지표만(A1)")
    return errors


def find_p_row_errors(rows, scenes, metrics):
    if not isinstance(rows, list):
        return ["rows: 목록이 아니다(사다리 표는 [])"]
    errors = []
    seen = set()
    for index, row in enumerate(rows):
        if not isinstance(row, dict):
            errors.append(f"행 {index}: 객체가 아니다")
            continue
        where = f"행 {row.get('row', index)}"
        errors.extend(find_unknown_keys(row, P_ROW_KEYS, where))
        if not isinstance(row.get("row"), str) or not row.get("row") or row.get("row") in seen:
            errors.append(f"{where}: row 이름이 없거나 겹친다")
        seen.add(row.get("row"))
        scene = scenes.get(row.get("scene"))
        metric = metrics.get(row.get("metric"))
        if not isinstance(scene, dict):
            errors.append(f"{where}: 장면 {row.get('scene')!r}이 scenes에 없다")
        if not isinstance(metric, dict):
            errors.append(f"{where}: 지표 {row.get('metric')!r}이 표 metrics에 없다")
        if not is_positive_number(row.get("limit_ms")):
            errors.append(f"{where}: limit_ms는 양수")
        if row.get("kind", "interval") != "interval" or "moving_p95_ratio" in row:
            errors.append(f"{where}: P 표는 interval 행만")
        if not isinstance(row.get("label", ""), str) or not isinstance(row.get("ledger_row", ""), str):
            errors.append(f"{where}: label·ledger_row는 문자열")
        if not isinstance(scene, dict) or not isinstance(metric, dict):
            continue
        if metric.get("pixel_scale") and scene.get("regime") is not True:
            errors.append(f"{where}: ft 행(pixel_scale)의 장면은 regime 참(PR-5)")
        if metric.get("backends") and scene.get("backend") not in metric["backends"]:
            errors.append(f"{where}: 지표 {row.get('metric')}는 선언 backend {metric['backends']} 장면만")
    return errors


def find_p_table_errors(table):
    if not isinstance(table, dict):
        return ["표 최상위가 객체가 아니다"]
    errors = find_table_basic_errors(table)
    if table.get("reference") is not None:
        errors.append("reference: P 표는 null(기준 장면 없음)")
    scenes = table.get("scenes")
    metrics = table.get("metrics")
    if not isinstance(scenes, dict) or not scenes:
        return errors + ["scenes: 장면이 1개 이상 필요"]
    if not isinstance(metrics, dict) or not metrics:
        return errors + ["metrics: 지표가 1개 이상인 객체가 필요"]
    for scene_id, scene in scenes.items():
        errors.extend(find_p_scene_errors(scene_id, scene))
    for name, metric in metrics.items():
        errors.extend(find_p_metric_errors(name, metric))
    known = {**BUILTIN_METRICS, **metrics}
    labels = [definition.get("label", name) for name, definition in known.items() if isinstance(definition, dict)]
    if len(set(labels)) != len(labels):
        errors.append(f"metrics: 지표 label이 겹친다 {labels}")
    errors.extend(find_p_row_errors(table.get("rows"), scenes, metrics))
    errors.extend(find_record_errors(table.get("records", []), scenes))
    errors.extend(find_consequence_errors(table))
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


def normalize_p_table(table):
    normalized = normalize_table(table)
    normalized["metrics"] = {name: metric for name, metric in normalized["metrics"].items() if name in table["metrics"]}
    for scene_id, scene in table["scenes"].items():
        normalized["scenes"][scene_id].update({"grid": dict(scene["grid"]), "target_labels": tuple(scene["target_labels"]), "toggle": scene["toggle"],
                                               "symbols": parse_toggle_symbols(scene["toggle"]), "awake_sides": scene["awake_sides"], "water_layer": scene["water_layer"],
                                               "regime": scene["regime"], "evidence": scene["evidence"]})
    for row_spec, row in zip(normalized["rows"], table["rows"]):
        row_spec["ledger_row"] = row.get("ledger_row")
    normalized["p_mode"] = True
    return normalized


def load_budget_table(path, p_mode=False):
    try:
        raw = Path(path).read_bytes()
        table = json.loads(raw.decode("utf-8"))
    except (OSError, UnicodeDecodeError, ValueError) as error:
        return None, [f"읽기 실패: {error}"]
    errors = find_p_table_errors(table) if p_mode else find_table_errors(table)
    if errors:
        return None, errors
    normalized = normalize_p_table(table) if p_mode else normalize_table(table)
    normalized["source_file"] = {"path": Path(path).as_posix() if p_mode else str(path), "sha256": hashlib.sha256(raw).hexdigest(), "area": table["area"]}
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


def split_block_rows(rows):
    metadata_index = next((index for index, row in enumerate(rows) if row and row[0].startswith("[")), len(rows))
    has_header_at_end = metadata_index < len(rows) and HEADER_AT_END_MARKER in rows[metadata_index]
    header_row = rows[metadata_index - 1] if has_header_at_end else rows[0]
    data_rows = rows[1:metadata_index - 1] if has_header_at_end else rows[1:metadata_index]
    return [name.strip() for name in header_row], data_rows


def read_block_table(path, discard_rows, wanted_columns):
    with open(path, encoding="utf-8", errors="replace", newline="") as handle:
        rows = list(csv.reader(handle))
    if not rows:
        return {"row_count": 0, "columns": {}}
    header, data_rows = split_block_rows(rows)
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
               "wanted_columns": set(WANTED_COLUMNS).union(*(metric["columns"] for metric in table["metrics"].values())), "p_mode": table.get("p_mode", False)}
    return context, None


def block_csv_path(block, context):
    relative = block.get("csv")
    if not relative:
        return None
    path = context["run_dir"] / relative
    return path if path.is_file() else None


def block_summary(block, context):
    key = block["block_id"]
    if key in context["summaries"]:
        return context["summaries"][key]
    if context.get("p_mode"):
        table = read_p_block_table(block_csv_path(block, context), context["discard_rows"])
        context["summaries"][key], context["frames"][key] = summarize_p_block(block, table, context), table
        return context["summaries"][key]
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
    validity_check = apply_p_validity_checks if context.get("p_mode") else apply_validity_checks
    validity_check(context, analysis)
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


def walk_stages(analysis, metric, limit, round_table, context, stage_judge=None, final_states=("통과", "미달")):
    judge = stage_judge or judge_stage
    trace = []
    for count in STAGE_ROUNDS:
        entry = inspect_stage(analysis, metric, round_table, count)
        trace.append(entry)
        if entry.get("terminal"):
            return trace, None, None
        if not entry["usable"]:
            continue
        outcome = judge(analysis, metric, limit, count)
        entry.update(outcome)
        if outcome["state"] in final_states:
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


def read_cell_as_zero(row, index):
    value = parse_cell(row, index)
    return 0.0 if value is None else value


def sum_cells_as_zero(rows, indices):
    if not indices:
        return None
    return [sum(read_cell_as_zero(row, index) for index in indices) for row in rows]


def read_p_block_table(path, discard_rows):
    empty = {"row_count": 0, "header": [], "columns": {}, "render_thread_columns": [], "render_thread_sum": None, "render_thread_control": None}
    if path is None:
        return empty
    with open(path, encoding="utf-8", errors="replace", newline="") as handle:
        rows = list(csv.reader(handle))
    if not rows:
        return empty
    header, data_rows = split_block_rows(rows)
    kept_rows = [row for row in data_rows[discard_rows:] if len(row) <= len(header)]
    indices = {}
    for index, name in enumerate(header):
        indices.setdefault(name, index)
    render_indices = [index for index, name in enumerate(header) if name.startswith(RENDER_THREAD_PREFIX) and not name.startswith(RENDER_THREAD_WAIT_PREFIX)]
    control_indices = [index for index, name in enumerate(header) if name in RENDER_THREAD_CONTROL_COLUMNS]
    return {"row_count": len(kept_rows), "header": header, "columns": {name: [parse_cell(row, index) for row in kept_rows] for name, index in indices.items()},
            "render_thread_columns": [header[index] for index in render_indices], "render_thread_sum": sum_cells_as_zero(kept_rows, render_indices),
            "render_thread_control": sum_cells_as_zero(kept_rows, control_indices)}


def summarize_p_block(block, table, context):
    side = block["side"]
    columns = table["columns"]
    step_values = present_values(columns.get(STEP_COUNT))
    step_count_max = max(step_values) if step_values else None
    idle_confirmed = side == "B" and (step_count_max is None or step_count_max == 0)
    assumed_zero = []
    series = {}
    for name, metric in context["metrics"].items():
        if metric["sum_prefix"] is not None:
            series[name] = table["render_thread_sum"]
            continue
        series[name] = sum_series([read_series(table, column, idle_confirmed, assumed_zero) for column in metric["columns"]])
    return {"block_id": block["block_id"], "side": side, "rows": table["row_count"], "header": table["header"], "present": sorted(columns),
            "assumed_zero": sorted(set(assumed_zero)), "step_count_max": step_count_max, "pdh_valid": True, "pdh_ratio": None,
            "render_thread_columns": len(table["render_thread_columns"]), "solver_p50": median_of(present_values(columns.get(SOLVER))),
            "rt_control_p50": None if table["render_thread_control"] is None else median_of(present_values(table["render_thread_control"])),
            "metric_p50": {name: median_of(present_values(values)) for name, values in series.items()},
            "metric_p95": {name: percentile(present_values(values), 0.95) for name, values in series.items()}}


def end_name(column):
    return column.rsplit("/", 1)[-1]


def has_render_thread_columns(header):
    return any(name.startswith(RENDER_THREAD_PREFIX) and not name.startswith(RENDER_THREAD_WAIT_PREFIX) for name in header)


def find_core_column_problems(side_blocks, context):
    defects = []
    missing = []
    for side in ("A", "B"):
        for block in side_blocks[side]:
            names = set(block_summary(block, context)["header"])
            for column in CORE_COLUMNS:
                if column in names:
                    continue
                observed = sorted(name for name in names if end_name(name) == column)
                (defects if observed else missing).append({"block": block["block_id"], "side": side, "column": column, "observed": observed})
            if not has_render_thread_columns(names):
                defects.append({"block": block["block_id"], "side": side, "column": RENDER_THREAD_CORE, "observed": sorted(name for name in names if "RenderThread" in name)})
    return defects, missing


def group_evidence_columns(candidates):
    groups = {}
    for column in candidates:
        groups.setdefault(end_name(column), []).append(column)
    return groups


def sum_present_columns(table, names):
    present = [name for name in names if name in table["columns"]]
    if not present:
        return None
    return sum_series([table["columns"][name] for name in present])


def block_table(block, context):
    block_summary(block, context)
    return context["frames"][block["block_id"]]


def inspect_evidence_statistic(side_blocks, context, statistic, names):
    entry = {"candidates": names, "defects": [], "missing_blocks": [], "a_p50": [], "b_max": [], "problems": []}
    for block in side_blocks["A"]:
        table = block_table(block, context)
        for name in table["header"]:
            if end_name(name) == statistic and name not in names and name not in entry["defects"]:
                entry["defects"].append(name)
        values = sum_present_columns(table, names)
        if values is None:
            entry["missing_blocks"].append(block["block_id"])
            continue
        p50 = median_of(present_values(values))
        entry["a_p50"].append({"block": block["block_id"], "p50": p50})
        if p50 is None or p50 <= 0:
            entry["problems"].append(f"{block['block_id']} A측 {statistic} p50 {p50}(> 0 기대)")
    for block in side_blocks["B"]:
        values = sum_present_columns(block_table(block, context), names)
        maximum = 0.0 if values is None else max(present_values(values), default=0.0)
        entry["b_max"].append({"block": block["block_id"], "max": maximum})
        if maximum > 0:
            entry["problems"].append(f"{block['block_id']} B측 {statistic} 최대 {maximum:g}(0 기대)")
    entry["defects"].sort()
    return entry


def inspect_evidence_columns(side_blocks, context, candidates):
    return {statistic: inspect_evidence_statistic(side_blocks, context, statistic, names) for statistic, names in group_evidence_columns(candidates).items()}


def inspect_step_counts(side_blocks, context, definition):
    cap = definition["awake_step_count"]
    rule = definition["awake_sides"] if definition["symbols"][0] in SLEEP_TOGGLES or definition["evidence"]["step_count"] else None
    entry = {"cap": cap, "rule": rule, "maxima": {"A": [], "B": []}, "problems": []}
    for side in ("A", "B"):
        for block in side_blocks[side]:
            step_max = block_summary(block, context)["step_count_max"]
            value = 0 if step_max is None else step_max
            entry["maxima"][side].append({"block": block["block_id"], "max": step_max})
            if value > cap:
                entry["problems"].append(f"{block['block_id']} {side}측 {STEP_COUNT} 최대 {value:g} > awake_step_count {cap}(대상 밖 깨움)")
            if rule is None:
                continue
            if side == "A" and step_max is None:
                entry["problems"].append(f"{block['block_id']} A측 {STEP_COUNT} 열 없음")
                continue
            awake = rule == "both" or (rule == "A" and side == "A")
            if awake and value < 1:
                entry["problems"].append(f"{block['block_id']} 깨운 {side}측 {STEP_COUNT} 최대 {value:g}(1 이상 기대, aw {rule})")
            if not awake and value > 0:
                entry["problems"].append(f"{block['block_id']} 재운 {side}측 {STEP_COUNT} 최대 {value:g}(0 기대, aw {rule})")
    return entry


def find_target_records(block, label):
    return [actor for actor in (block.get("actors") or []) if isinstance(actor, dict) and actor.get("label") == label]


def find_missing_target_records(side_blocks, labels):
    problems = []
    for side in ("A", "B"):
        for block in side_blocks[side]:
            absent = [label for label in labels if not find_target_records(block, label)]
            if absent:
                problems.append(f"{block['block_id']} 대상 기록 없음: {', '.join(absent)}")
    return problems


def find_awake_record_problems(side_blocks, definition):
    problems = []
    for side in ("A", "B"):
        expected = definition["awake_sides"] == "both" or (definition["awake_sides"] == "A" and side == "A")
        for block in side_blocks[side]:
            for label in definition["target_labels"]:
                problems.extend(f"{block['block_id']} {label} awake {actor.get('awake')}({side}측 기대 {expected}, aw {definition['awake_sides']})"
                                for actor in find_target_records(block, label) if actor.get("awake") is not expected)
    return problems


def find_light_labels(plan_scene, definition):
    labels = []
    for step in plan_scene.get("b") or []:
        if not isinstance(step, dict) or step.get("action") != "light":
            continue
        targets = step.get("targets", "scene")
        labels.extend(targets if isinstance(targets, list) else definition["target_labels"])
    return list(dict.fromkeys(labels or definition["target_labels"]))


def find_block_part_problems(block, side, parts_by_label):
    expected = side == "A"
    problems = []
    for label, parts in parts_by_label.items():
        actors = find_target_records(block, label)
        if not actors:
            problems.append(f"{block['block_id']} {label} 부품 기록 없음")
        for actor in actors:
            visible = actor.get("visible") if isinstance(actor.get("visible"), dict) else {}
            problems.extend(f"{block['block_id']} {label} {part} visible {visible.get(part)}({side}측 기대 {expected})" for part in parts if visible.get(part) is not expected)
    return problems


def find_block_light_problems(block, side, light_labels):
    expected = side == "A"
    states = [state for label in light_labels for actor in find_target_records(block, label) for state in (actor.get("lights_visible") or [])]
    if not states:
        return [f"{block['block_id']} 광원 기록 0개({', '.join(light_labels)}, 1개 이상 기대)"]
    if any(state is not expected for state in states):
        return [f"{block['block_id']} lights_visible {states}({side}측 기대 전부 {expected})"]
    return []


def find_part_problems(side_blocks, definition, plan_scene):
    evidence = definition["evidence"]
    light_labels = find_light_labels(plan_scene, definition)
    problems = []
    for side in ("A", "B"):
        for block in side_blocks[side]:
            problems.extend(find_block_part_problems(block, side, evidence["parts"]))
            if evidence["light"]:
                problems.extend(find_block_light_problems(block, side, light_labels))
    return problems


def find_actor_entry_problems(block, side, entry, keys):
    result = entry.get("result") if isinstance(entry.get("result"), dict) else {}
    matched = result.get("matched")
    if "error" in result or not (is_integer(matched) and matched >= 1):
        return [f"{block['block_id']} actor 되읽기 matched {matched}(1 이상 기대){' 오류 ' + str(result['error'])[:80] if 'error' in result else ''}"]
    expected = matched if side == "B" else 0
    return [f"{block['block_id']} actor {key} {result.get(key)}({side}측 기대 {expected}, matched {matched})" for key in keys
            if not is_integer(result.get(key)) or result.get(key) != expected]


def find_actor_toggle_problems(side_blocks, actor_evidence):
    if actor_evidence is None:
        return []
    keys = [readback for key, readback in ACTOR_READBACK_KEYS.items() if actor_evidence.get(key)]
    problems = []
    for side in ("A", "B"):
        for block in side_blocks[side]:
            entries = [step for step in (block.get("side_steps") or []) if isinstance(step, dict) and step.get("action") == "actor"]
            if not entries:
                problems.append(f"{block['block_id']} actor 되읽기 없음(side_steps)")
            for entry in entries:
                problems.extend(find_actor_entry_problems(block, side, entry, keys))
    return problems


def parse_console_values(steps):
    values = {}
    for step in steps or []:
        if not isinstance(step, dict) or step.get("action") != "console":
            continue
        for command in step.get("commands") or []:
            parts = str(command).split()
            if len(parts) != 2:
                continue
            try:
                values[parts[0]] = float(parts[1])
            except ValueError:
                continue
    return values


def same_readback(actual, expected):
    return is_number(actual) and abs(actual - expected) <= READBACK_TOLERANCE


def read_step_readback(entry):
    result = entry.get("result") if isinstance(entry, dict) else None
    readback = result.get("readback") if isinstance(result, dict) else None
    return readback if isinstance(readback, dict) else {}


def find_toggle_readback_problems(side_blocks, plan_scene, console_evidence):
    problems = []
    for side in ("A", "B"):
        declared = parse_console_values(plan_scene.get(side.lower()))
        declared.update({name: float(values[side]) for name, values in console_evidence.items()})
        if not declared:
            continue
        for block in side_blocks[side]:
            readback = {}
            for entry in block.get("side_steps") or []:
                if isinstance(entry, dict) and entry.get("action") == "console":
                    readback.update(read_step_readback(entry))
            problems.extend(f"{block['block_id']} 토글 {name} 되읽기 {readback.get(name)}(선언 {value:g})" for name, value in declared.items() if not same_readback(readback.get(name), value))
    return problems


def find_setup_readback_problems(plan_scene, record):
    planned = [step for step in plan_scene.get("setup") or [] if isinstance(step, dict) and step.get("action") != "wait"]
    results = record.get("steps") or []
    problems = [] if len(results) >= len(planned) else [f"setup 단계 {len(planned)}개 중 기록 {len(results)}개"]
    for step, entry in zip(planned, results):
        if step.get("action") != "console":
            continue
        if not isinstance(entry, dict) or entry.get("action") != "console":
            problems.append(f"setup console 단계 자리에 다른 기록 {entry.get('action') if isinstance(entry, dict) else entry!r}")
            continue
        readback = read_step_readback(entry)
        problems.extend(f"setup {name} 되읽기 {readback.get(name)}(선언 {value:g})" for name, value in parse_console_values([step]).items()
                        if name != SCREEN_PERCENTAGE and not same_readback(readback.get(name), value))
    return problems


def read_backend_setting(value):
    match = BACKEND_SETTING_PATTERN.search(str(value)) if value is not None else None
    return match.group(1).upper() if match else None


def effective_grid(setting, backend):
    if not is_number(setting) or setting != int(setting):
        return None
    low, high = GPU_GRID_RANGE if backend == "GPU" else CPU_GRID_RANGE
    clamped = min(max(int(setting), low), high)
    return 1 << (clamped - 1).bit_length() if backend == "GPU" else clamped


def check_actor_backend(entry, block, label, actor, water_forces_cpu, declared_grid):
    declared = entry["declared"]
    setting = read_backend_setting(actor.get("backend"))
    effective = "CPU" if water_forces_cpu else (declared if setting == "AUTO" else setting)
    observed = {"label": label, "backend": actor.get("backend"), "effective": effective, "grid_resolution": actor.get("grid_resolution")}
    if observed not in entry["observed"]:
        entry["observed"].append(observed)
    if effective != declared:
        entry["problems"].append(f"{block['block_id']} {label} backend {actor.get('backend')} → 실효 {effective}(선언 {declared}, B4 (i))")
    if declared_grid is None:
        return
    grid = effective_grid(actor.get("grid_resolution"), declared)
    if grid != declared_grid:
        entry["problems"].append(f"{block['block_id']} {label} grid {actor.get('grid_resolution')} → 실효 {grid}(선언 {declared_grid}, BS:86-99)")


def inspect_backend_evidence(context, side_blocks, definition, record):
    declared = definition["backend"]
    base = definition["symbols"][0]
    applies = declared is not None and bool(definition["target_labels"]) and (base in SLEEP_TOGGLES or base == "C")
    water_gpu = ((record.get("conditions") or {}).get("variables") or {}).get(WATER_GPU_VARIABLE)
    entry = {"applies": applies, "declared": declared, "grid": dict(definition["grid"]), "water_layer": definition["water_layer"], "water_gpu": water_gpu, "observed": [], "solver_a_p50": [], "problems": []}
    if not applies:
        return entry
    water_forces_cpu = definition["water_layer"] and (water_gpu is None or water_gpu == 0)
    for side in ("A", "B"):
        for block in side_blocks[side]:
            for label in definition["target_labels"]:
                for actor in find_target_records(block, label):
                    check_actor_backend(entry, block, label, actor, water_forces_cpu, definition["grid"].get(label))
    for block in side_blocks["A"]:
        solver = block_summary(block, context)["solver_p50"]
        entry["solver_a_p50"].append({"block": block["block_id"], "p50": solver})
        if declared == "GPU" and not (solver is not None and solver > 0):
            entry["problems"].append(f"{block['block_id']} GPU 선언인데 A측 {SOLVER} p50 {solver}(> 0 기대, B4 (ii))")
        if declared == "CPU" and solver is not None and solver > 0:
            entry["problems"].append(f"{block['block_id']} CPU 선언인데 A측 {SOLVER} p50 {solver}(없음 또는 0 기대, B4 (ii))")
    entry["ok"] = not entry["problems"]
    return entry


def is_viewport(value):
    return isinstance(value, list) and len(value) == 2 and all(is_positive_number(size) for size in value)


def inspect_pixel_record(plan_scene, record):
    meta = (plan_scene.get("meta") or {}).get("screen_percentage", (record.get("meta") or {}).get("screen_percentage"))
    readback = None
    for entry in record.get("steps") or []:
        value = read_step_readback(entry).get(SCREEN_PERCENTAGE)
        readback = value if value is not None else readback
    conditions = record.get("conditions") or {}
    viewport = conditions.get("viewport")
    override = (conditions.get("variables") or {}).get(OVERRIDE_PIE_SCREEN_PERCENTAGE)
    entry = {"meta": meta, "readback": readback, "viewport": viewport, "override": override, "scale": None, "state": None, "detail": ""}
    checks = (("뷰포트(conditions.viewport)", is_viewport(viewport)), ("meta.screen_percentage", is_positive_number(meta)), ("setup SP 되읽기", is_number(readback)), ("OPSP", is_number(override)))
    absent = [name for name, present in checks if not present]
    if absent:
        entry.update({"state": "화소 기록 없음", "detail": f"{', '.join(absent)} 기록 없음(PR-8 ⑧)"})
        return entry
    if not same_readback(readback, meta):
        entry.update({"state": "화소 기록", "detail": f"setup SP 되읽기 {readback} ≠ meta {meta}(PR-8 ⑧)"})
        return entry
    entry["scale"] = REFERENCE_PIXELS / (viewport[0] * viewport[1] * (meta / 100.0) ** 2)
    return entry


def apply_p_validity_checks(context, analysis):
    scene_id = analysis["scene"]
    definition = analysis["definition"]
    record = context["records"][scene_id]
    plan_scene = context["plan_scenes"].get(scene_id) or {}
    side_blocks = analysis["side_blocks"]
    header = set()
    for side in ("A", "B"):
        for block in side_blocks[side]:
            header.update(block_summary(block, context)["header"])
    header_defects, core_missing = find_core_column_problems(side_blocks, context)
    analysis["p"] = {"header": sorted(header), "header_defects": header_defects, "core_missing": core_missing,
                     "evidence_columns": inspect_evidence_columns(side_blocks, context, definition["evidence"]["columns"]),
                     "step_counts": inspect_step_counts(side_blocks, context, definition),
                     "target_records": find_missing_target_records(side_blocks, definition["target_labels"]),
                     "awake": find_awake_record_problems(side_blocks, definition),
                     "parts": find_part_problems(side_blocks, definition, plan_scene),
                     "actor": find_actor_toggle_problems(side_blocks, definition["evidence"]["actor"]),
                     "toggle_readback": find_toggle_readback_problems(side_blocks, plan_scene, definition["evidence"]["console"]),
                     "setup_readback": find_setup_readback_problems(plan_scene, record),
                     "backend": inspect_backend_evidence(context, side_blocks, definition, record),
                     "pixel": inspect_pixel_record(plan_scene, record)}


def make_problem(verdict, reason, detail):
    return {"verdict": verdict, "reason": reason, "detail": detail}


def join_details(items):
    return "; ".join(items[:4]) + (f" 외 {len(items) - 4}건" if len(items) > 4 else "")


def describe_column_items(items):
    return join_details([f"{item['block']} {item['column']}" + (f" → 관측 {', '.join(item['observed'])}" if item["observed"] else "") for item in items])


def row_core_columns(metric, definition):
    if metric["sum_prefix"] is not None:
        return (RENDER_THREAD_CORE,)
    if metric["pixel_scale"]:
        return CORE_COLUMNS if definition["regime"] else (FRAME,)
    return tuple(column for column in metric["columns"] if column in CORE_COLUMNS)


def list_p_validity_problems(p):
    evidence = p["evidence_columns"]
    problems = []
    defects = sorted({name for entry in evidence.values() for name in entry["defects"]})
    if defects:
        problems.append(make_problem(UNDECIDABLE, COLUMN_DEFECT_PREFIX + ", ".join(defects), "증거 열 후보 목록 밖에 끝 이름이 같은 A측 열(PR-4·F2)"))
    missing = [f"{statistic}: {', '.join(entry['missing_blocks'])}" for statistic, entry in evidence.items() if entry["missing_blocks"]]
    if missing:
        problems.append(make_problem(MEASURE_IMPOSSIBLE, COLUMN_MISSING_REASON, "A측 증거 열 후보 전부 없음 " + join_details(missing)))
    toggle_items = p["step_counts"]["problems"] + p["awake"] + p["parts"] + p["actor"] + [item for entry in evidence.values() for item in entry["problems"]]
    checks = (("대상 기록", p["target_records"]), ("백엔드 증거", p["backend"]["problems"]), ("토글 증거", toggle_items), ("설정 되읽기", p["toggle_readback"] + p["setup_readback"]))
    problems.extend(make_problem(UNDECIDABLE, reason, join_details(items)) for reason, items in checks if items)
    return problems


def find_p_metric_missing(context, analysis, metric):
    columns = [column for column in metric["columns"] if column not in CORE_COLUMNS]
    absent = []
    for block in analysis["side_blocks"]["A"]:
        present = set(block_summary(block, context)["present"])
        absent.extend(f"{block['block_id']}:{column}" for column in columns if column not in present)
    return absent


def list_p_row_problems(context, analysis, metric):
    problems = [make_problem(UNDECIDABLE, item["reason"], item["detail"]) for item in analysis["blockers"]]
    p = analysis.get("p")
    if p is None:
        return problems
    used = row_core_columns(metric, analysis["definition"])
    core_defects = [item for item in p["header_defects"] if item["column"] in used]
    core_missing = [item for item in p["core_missing"] if item["column"] in used]
    validity = list_p_validity_problems(p)
    column_defects = [item for item in validity if item["reason"].startswith(COLUMN_DEFECT_PREFIX)]
    if core_defects:
        problems.append(make_problem(UNDECIDABLE, HEADER_DEFECT_REASON, "핵심 열 " + describe_column_items(core_defects)))
    problems.extend(column_defects)
    if core_missing:
        problems.append(make_problem(MEASURE_IMPOSSIBLE, COLUMN_MISSING_REASON, "핵심 열 " + describe_column_items(core_missing)))
    problems.extend(item for item in validity if item not in column_defects)
    if metric["pixel_scale"] and p["pixel"]["state"] is not None:
        problems.append(make_problem(UNDECIDABLE, p["pixel"]["state"], p["pixel"]["detail"]))
    absent = find_p_metric_missing(context, analysis, metric)
    if absent:
        problems.append(make_problem(MEASURE_IMPOSSIBLE, COLUMN_MISSING_REASON, "A측 지표 열 없음 " + join_details(absent)))
    return problems


def load_regime_module():
    module = sys.modules.get("fx7pr_judge")
    if module is not None:
        return module
    spec = importlib.util.spec_from_file_location("fx7pr_judge", Path(__file__).resolve().with_name("fx7pr_judge.py"))
    module = importlib.util.module_from_spec(spec)
    sys.modules["fx7pr_judge"] = module
    spec.loader.exec_module(module)
    return module


def describe_regime_side(side):
    return {"blocks": side["blocks"], "median": side["median"], "minimum": side["minimum"], "basic": side["passed"], "strong": side["strong"]}


def compute_regime(context, analysis, round_count):
    module = load_regime_module()
    if context.get("regime_context") is None:
        context["regime_context"], _ = module.load_context(context["run_dir"])
    numbers = [number for number in analysis["round_numbers"] if round_count is None or number <= round_count]
    side_blocks = collect_side_blocks({number: analysis["rounds"][number] for number in numbers})
    result = module.check_regime(side_blocks, context["regime_context"])
    return {"rounds": numbers, "A": describe_regime_side(result["A"]), "B": describe_regime_side(result["B"]), "basic": result["passed"], "strong": result["strong"]}


def combine_pixel_states(plain_state, scaled_state):
    if plain_state == scaled_state:
        return plain_state
    if "걸침" in (plain_state, scaled_state):
        return "걸침"
    return PIXEL_SPLIT


def judge_p_stage(analysis, metric, limit, round_count, scale):
    outcome = judge_stage(analysis, metric, limit, round_count)
    if scale is None or "interval" not in outcome:
        return outcome
    outcome["scaled_values"] = [round(value * scale, 6) for value in outcome["values"]]
    outcome["interval_scaled"] = make_interval(outcome["scaled_values"], outcome["interval"]["rank"], limit)
    outcome["state"] = combine_pixel_states(outcome["interval"]["state"], outcome["interval_scaled"]["state"])
    return outcome


def last_judged_entry(trace):
    return next((entry for entry in reversed(trace) if "interval" in entry), None)


def regime_round_count(trace, final_count):
    if final_count is not None:
        return final_count
    measured = [entry["rounds"] for entry in trace if entry["gate"] != "미측정"]
    return measured[-1] if measured else None


def summarize_differences(values):
    return {"values": values, "median": round_four(median_of(values))} if values else None


def collect_pair_differences(pairs, read_value):
    values = []
    for pair in pairs:
        a_value, b_value = read_value(pair["a_summary"]), read_value(pair["b_summary"])
        if a_value is not None and b_value is not None:
            values.append(round(a_value - b_value, 6))
    return summarize_differences(values)


def judge_p_row_pairs(context, analysis, metric, row_spec, row, pixel):
    scale = pixel["scale"] if metric["pixel_scale"] and pixel else None
    round_table = build_round_table(analysis, metric, context)
    trace, final_count, outcome = walk_stages(analysis, metric, row_spec["limit"], round_table, context, functools.partial(judge_p_stage, scale=scale), P_FINAL_STATES)
    used = usable_pairs(analysis, metric, final_count) if final_count else [pair for pair in analysis["pairs"] if not pair["discarded"]]
    row.update({"trace": trace, "rounds": final_count, "pairs": len(used), "round_table": [round_table[number] for number in sorted(round_table)], "pair_view": view_pairs(analysis, metric, final_count)})
    judged = outcome or last_judged_entry(trace)
    if judged is not None:
        row["interval"] = judged["interval"]
        row["d"] = summarize_differences(judged["values"])
    if judged is not None and "interval_scaled" in judged:
        row["d_prime"] = {"scale": scale, **summarize_differences(judged["scaled_values"]), "interval": judged["interval_scaled"]}
    if metric["p95_record"]:
        row["p95_record"] = collect_pair_differences(used, lambda summary: summary["metric_p95"][metric["name"]])
    if metric["sum_prefix"] is not None:
        row["rt_control"] = {"columns": list(RENDER_THREAD_CONTROL_COLUMNS), **(collect_pair_differences(used, lambda summary: summary["rt_control_p50"]) or {"values": [], "median": None})}
    problems = []
    if metric["pixel_scale"] and analysis["definition"]["regime"]:
        row["regime"] = compute_regime(context, analysis, regime_round_count(trace, final_count))
        if not row["regime"]["basic"]:
            problems.append(make_problem(UNDECIDABLE, "체제", f"라운드 {row['regime']['rounds']} 블록 전부로 본 체제 기본 미달(PR-5 재확인)"))
    if outcome is None:
        reason, detail = unresolved_reason(trace, row_spec["limit"])
        return problems + [make_problem(UNDECIDABLE, P_REASON_NAMES.get(reason, reason), detail)], None
    if outcome["state"] == PIXEL_SPLIT:
        return problems + [make_problem(UNDECIDABLE, PIXEL_SPLIT, f"{final_count}라운드 d {outcome['interval']['state']}·d′ {outcome['interval_scaled']['state']}(A1)")], None
    return problems, outcome["state"]


def decide_p_row(context, row_spec):
    analysis = analyze_scene(context, row_spec["scene"])
    metric = context["metrics"][row_spec["metric"]]
    pixel = (analysis.get("p") or {}).get("pixel")
    row = {"row": row_spec["row"], "ledger_row": row_spec["ledger_row"], "scene": row_spec["scene"], "metric": metric["name"], "label": row_spec["label"],
           "verdict": None, "reason": None, "reasons": [], "interval": None, "d": None, "d_prime": None,
           "sp": {"meta": pixel["meta"], "readback": pixel["readback"]} if pixel else None, "viewport": pixel["viewport"] if pixel else None,
           "rounds": None, "pairs": 0, "p95_record": None, "backend": analysis["definition"]["backend"], "limit_ms": row_spec["limit"], "rt_control": None,
           "regime": None, "trace": [], "pair_view": []}
    problems = list_p_row_problems(context, analysis, metric)
    state = None
    if analysis["pairs"]:
        extra, state = judge_p_row_pairs(context, analysis, metric, row_spec, row, pixel)
        problems.extend(extra)
    if not problems and state is None:
        problems.append(make_problem(UNDECIDABLE, "쌍 부족", "측정 쌍 없음"))
    row["reasons"] = problems
    row["reason"] = problems[0]["reason"] if problems else None
    row["verdict"] = problems[0]["verdict"] if problems else state
    return row


def decide_p_budget(rows):
    verdicts = [row["verdict"] for row in rows.values()]
    if "미달" in verdicts:
        return "미충족"
    if not verdicts or any(verdict != "통과" for verdict in verdicts):
        return "판정 불가"
    return "충족(proposed)"


def describe_p_run(context, mode):
    return {"mode": mode, "p_mode": True, "run_dir": context["run_dir"].as_posix(), "run_token": context["session"].get("run_token"),
            "budget_table": context["table"]["source_file"], "plan_p": context["plan"].get(P_PLAN_KEY)}


def run_p_judge(context):
    table = context["table"]
    rows = {row_spec["row"]: decide_p_row(context, row_spec) for row_spec in table["rows"]}
    verdict = decide_p_budget(rows)
    return {**describe_p_run(context, "judge"), "rows": rows, "budget_verdict": verdict, "consequence": table["consequences"][verdict], "warnings": context["warnings"]}


def build_p_pilot_scene(context, scene_id):
    analysis = analyze_scene(context, scene_id)
    definition = analysis["definition"]
    p = analysis.get("p")
    valid_pairs = sum(1 for pair in analysis["pairs"] if not pair["discarded"])
    error = dict(analysis["blockers"][0]) if analysis["blockers"] else None
    if error is None and valid_pairs == 0:
        error = {"reason": "쌍 부족", "detail": "유효 쌍 0개"}
    entry = {"scene": scene_id, "present": analysis["present"], "toggle": definition["toggle"], "awake_sides": definition["awake_sides"], "backend": definition["backend"],
             "grid": dict(definition["grid"]), "error": error, "pairs": valid_pairs, "header": [], "header_defects": [], "core_missing": [], "regime": None,
             "evidence": None, "backend_evidence": None, "viewport": None, "sp": None, "pixel": None, "problems": []}
    if p is None:
        return entry
    pixel = p["pixel"]
    entry.update({"header": p["header"], "header_defects": p["header_defects"], "core_missing": p["core_missing"],
                  "evidence": {key: p[key] for key in ("step_counts", "evidence_columns", "target_records", "awake", "parts", "actor", "toggle_readback", "setup_readback")},
                  "backend_evidence": p["backend"], "viewport": pixel["viewport"], "sp": {"meta": pixel["meta"], "readback": pixel["readback"]},
                  "pixel": {key: pixel[key] for key in ("state", "detail", "scale", "override")}, "problems": list_p_validity_problems(p)})
    regime_broken = any(item["column"] in (GAME_THREAD, GPU_TIME) for item in p["header_defects"] + p["core_missing"])
    if definition["regime"] and error is None and not regime_broken:
        entry["regime"] = compute_regime(context, analysis, None)
    return entry


def run_p_pilot(context):
    scenes = {scene_id: build_p_pilot_scene(context, scene_id) for scene_id in context["table"]["scenes"]}
    return {**describe_p_run(context, "pilot"), "scenes": scenes, "warnings": context["warnings"]}


def format_interval_bounds(interval):
    if not interval:
        return "-"
    return f"[{format_number(interval['lower'])}, {format_number(interval['upper'])}] {interval['state']}"


def describe_p_problems(problems):
    return [f"  {item['verdict']}({item['reason']}): {item['detail']}" for item in problems]


def format_p_pilot(result):
    lines = [f"[pilot P] {result['run_dir']}"] + describe_table_source(result)
    for scene_id, entry in result["scenes"].items():
        regime = entry["regime"]
        regime_text = "체제 -" if regime is None else f"체제 기본 {'예' if regime['basic'] else '아니오'}·강 {'예' if regime['strong'] else '아니오'}"
        error_text = f"{entry['error']['reason']}" if entry["error"] else "없음"
        lines.append(f"{scene_id} {entry['toggle']} aw {entry['awake_sides']} {entry['backend']}: 오류 {error_text} 쌍 {entry['pairs']} {regime_text} 핵심 열 결함 {len(entry['header_defects'])} SP {entry['sp']}")
        lines.extend(describe_p_problems(entry["problems"]))
    lines.extend(f"경고: {warning}" for warning in result["warnings"])
    return lines


def format_p_judge(result):
    lines = [f"[judge P] {result['run_dir']}"] + describe_table_source(result)
    for name, row in result["rows"].items():
        verdict = row["verdict"] + (f"({row['reason']})" if row["reason"] else "")
        prime = "" if row["d_prime"] is None else f" d′ {format_interval_bounds(row['d_prime']['interval'])}"
        lines.append(f"{name} {row['scene']}: {verdict} 한도 {row['limit_ms']}ms 라운드 {row['rounds']} 쌍 {row['pairs']} d {format_interval_bounds(row['interval'])}{prime}")
        lines.extend(describe_p_problems(row["reasons"]))
    lines.append(f"예산 판정(기록, 종합은 T3.9): {result['budget_verdict']}")
    lines.extend(f"경고: {warning}" for warning in result["warnings"])
    return lines


def write_json_atomic(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp-{os.getpid()}")
    payload = (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    try:
        with open(temporary, "wb") as handle:
            handle.write(payload)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def is_p_run(run_dir):
    try:
        plan = json.loads((Path(run_dir) / "plan.json").read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, ValueError):
        return False
    return isinstance(plan, dict) and P_PLAN_KEY in plan


def run_p_command(parser, args):
    if args.out is not None and os.path.lexists(args.out):
        print(f"--out이 이미 있다(덮어쓰지 않음, 5절 공통): {args.out}", file=sys.stderr)
        return 2
    if args.budget_table is None:
        print("P 계획(_p)에는 --budget-table이 필요하다", file=sys.stderr)
        return 2
    table, errors = load_budget_table(args.budget_table, p_mode=True)
    if errors:
        print(f"P 예산 표 오류({args.budget_table}): " + "; ".join(errors), file=sys.stderr)
        return 2
    if args.water_ref_ms is not None:
        parser.error("P 표는 reference null이라 --water-ref-ms를 주지 않는다")
    try:
        context, error = load_context(args.run_dir, table)
    except (OSError, UnicodeDecodeError, ValueError, KeyError, TypeError, AttributeError) as failure:
        context, error = None, f"실행 폴더 읽기 실패: {type(failure).__name__}: {failure}"
    if error is not None:
        print(error, file=sys.stderr)
        return 2
    result = run_p_pilot(context) if args.mode == "pilot" else run_p_judge(context)
    cleaned = clean(result)
    print("\n".join(format_p_pilot(cleaned) if args.mode == "pilot" else format_p_judge(cleaned)))
    if args.out is not None:
        write_json_atomic(args.out, cleaned)
    return 0


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
    if is_p_run(args.run_dir):
        return run_p_command(parser, args)
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
