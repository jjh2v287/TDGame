"""실행 환경: 시스템 Python 3(표준 라이브러리만), 에디터·빌드 불필요. 입력은 pie_profile_fluid.py --csv-gpu 실행 폴더(Saved/FluidFX/fx7p/<run>/, session.json·plan.json·csv/ 블록 CSV).
실행 명령: python -B Tools/FluidFX/fx4b_budget_judge.py pilot|judge <run_dir> [--water-ref-ms <값, judge 필수>] [--out <json>]
출력: 표준 출력에 사람이 읽는 요약, --out이면 같은 내용을 JSON으로 저장(pilot = 장면별 필수 열·PD-H 통과 비율·명명 GPU와 게임 스레드 물 합 대략값, judge = WQ0 유효성·B192·B1024·G192·G1024·GW 판정 행·WQ1·WQ4 기록 행). 판정 불가는 이름 붙은 사유.
상태: 2026-10-24 FX-4b 예산 PIE 판정 분석기(명세 Saved/AgentOps/20261024/fx4b-acc/spec-fx4b-budget.md 1~5절, FX-7p-r 판정기 fx7pr_judge.py의 구간·안정·클럭 규칙 계승), 시험은 같은 폴더 기준 budget-fixtures/run_fixtures.py.
"""
import argparse
import csv
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

GPU_ROW_SPECS = (
    {"row": "B192", "scene": "WQ2", "limit": GPU_LIMIT_192_MS, "label": "명명 GPU d"},
    {"row": "B1024", "scene": "WQ3", "limit": GPU_LIMIT_1024_MS, "label": "명명 GPU d"},
)
GAME_THREAD_ROW_SPECS = (
    {"row": "G192", "scene": "WQ2", "limit": GAME_THREAD_LIMIT_MS, "label": "게임 스레드 물 합 A p50"},
    {"row": "G1024", "scene": "WQ3", "limit": GAME_THREAD_LIMIT_MS, "label": "게임 스레드 물 합 A p50"},
)
GPU_METRIC = {"name": "named_gpu", "needed": NAMED_GPU_COLUMNS + (SUBSTEPS,), "sides": ("A", "B"), "uses_pdh": True}
GAME_THREAD_METRIC = {"name": "game_thread_water", "needed": GAME_THREAD_WATER_COLUMNS, "sides": ("A",), "uses_pdh": False}


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


def read_block_table(path, discard_rows):
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
        if name in WANTED_COLUMNS:
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


def summarize_block(block, side, table):
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
    }
    return summary, {"named": named, "mask": mask, "water": water}


def load_context(run_dir):
    run_dir = Path(run_dir)
    session_path = run_dir / "session.json"
    if not session_path.is_file():
        return None, f"session.json이 없다: {session_path}"
    session = read_json(session_path)
    plan_path = run_dir / "plan.json"
    plan = read_json(plan_path) if plan_path.is_file() else {}
    context = {"run_dir": run_dir, "session": session, "plan": plan, "records": {record["id"]: record for record in session.get("scenes", [])},
               "plan_scenes": {scene["id"]: scene for scene in plan.get("scenes", [])}, "discard_rows": plan.get("discard_frames", DEFAULT_DISCARD_ROWS),
               "summaries": {}, "frames": {}, "tables": {}, "warnings": [], "analyses": {}}
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
        table = read_block_table(block_csv_path(block, context), context["discard_rows"])
        context["summaries"][key], context["frames"][key] = summarize_block(block, block["side"], table)
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
    if normalize_backend(actor.get("backend")) != expected_backend:
        problems.append(f"backend {actor.get('backend')} (기대 {expected_backend})")
    if actor.get("grid_resolution") != expected_grid:
        problems.append(f"grid {actor.get('grid_resolution')} (기대 {expected_grid})")
    if bool(actor.get("awake")) != (side == "A"):
        problems.append(f"awake {actor.get('awake')} ({side} 측 기대 {side == 'A'})")
    if "is_using_gpu_water_layer" in actor and bool(actor["is_using_gpu_water_layer"]) != (expected_backend == "GPU"):
        problems.append(f"is_using_gpu_water_layer {actor['is_using_gpu_water_layer']}")
    return problems


def check_actor_records(side_blocks, expected_backend, expected_grid):
    result = {"expected_backend": expected_backend, "expected_grid": expected_grid, "checked_blocks": 0, "unverified_blocks": 0, "others_unrecorded_blocks": 0, "mismatches": []}
    for side, blocks in side_blocks.items():
        for block in blocks:
            actors = block.get("actors") or []
            targets = [actor for actor in actors if actor.get("label") == TARGET_LABEL]
            others = [actor for actor in actors if actor.get("label") != TARGET_LABEL]
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


def check_step_counts(side_blocks, context):
    mismatches = []
    for side, blocks in side_blocks.items():
        for block in blocks:
            step_max = block_summary(block, context)["step_count_max"]
            if step_max is None:
                continue
            if side == "B" and step_max > 0:
                mismatches.append(f"{block['block_id']} 재운 측 {STEP_COUNT} 최대 {step_max:g}(다른 유체가 깨어 있거나 물이 재워지지 않음)")
            if side == "A" and step_max > 1:
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
    backend, grid = SCENE_DEFINITIONS[scene_id]
    analysis = {"scene": scene_id, "present": record is not None, "blockers": [], "backend": backend, "grid": grid, "round_numbers": [], "leading_rounds": 0, "pairs": [], "rounds": {}}
    if record is None:
        add_blocker(analysis, "장면 없음", f"{scene_id} 장면이 session.json에 없다")
        return analysis
    plan_meta = (context["plan_scenes"].get(scene_id) or {}).get("meta") or {}
    plan_grid = plan_meta.get("resolution")
    if plan_grid is not None and plan_grid != grid:
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
    actors = check_actor_records(side_blocks, analysis["backend"], analysis["grid"])
    analysis["actors_check"] = actors
    if actors["mismatches"]:
        summary = "; ".join(f"{item['block']} {item['actor']}: {', '.join(item['problems'])}" for item in actors["mismatches"][:3])
        add_blocker(analysis, "유효성", f"(b) 액터 기록 {len(actors['mismatches'])}건 어긋남: {summary}")
    if actors["unverified_blocks"]:
        context["warnings"].append(f"{analysis['scene']}: 블록 {actors['unverified_blocks']}개에 대상 액터 기록이 없어 backend·grid·깨움을 확인하지 못했다")
    if actors["others_unrecorded_blocks"]:
        context["warnings"].append(f"{analysis['scene']}: 블록 {actors['others_unrecorded_blocks']}개에 다른 유체 액터 기록이 없어 {STEP_COUNT}로만 재움을 확인했다(계획 targets를 all로 두면 기록된다)")
    step_problems = check_step_counts(side_blocks, context)
    if step_problems:
        add_blocker(analysis, "유효성", f"(c) 스텝 수 어긋남 {len(step_problems)}건: {'; '.join(step_problems[:3])}")
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
    if metric["uses_pdh"]:
        return summary["named_p50"] if summary["pdh_valid"] else None
    return summary["game_thread_water_p50"]


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
    if metric["uses_pdh"]:
        a_value, b_value = pair["a_summary"]["named_p50"], pair["b_summary"]["named_p50"]
        return None if a_value is None or b_value is None else round(a_value - b_value, 6)
    value = pair["a_summary"]["game_thread_water_p50"]
    return None if value is None else round(value, 6)


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
    if reference is not None and not reference["valid"]:
        row["reasons"].append({"reason": "유효성", "detail": f"(a) WQ0 세션 무효({reference['status']}): {reference['detail']}"})
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


def evaluate_moving_frames(context, analysis, round_count):
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
              "moving_p95": percentile(moving_values, 0.95), "scene_mean": mean_of(all_values), "ratio_limit": MOVING_P95_RATIO}
    if not moving_values or not all_values:
        result.update({"state": "판정 불가", "detail": "이동 프레임 0개(근사 정의로 표본 없음)" if all_values else "PD-H 통과 A 블록 없음"})
        return result
    result["limit_ms"] = MOVING_P95_RATIO * result["scene_mean"]
    result["state"] = "통과" if result["moving_p95"] <= result["limit_ms"] + EPSILON else "미달"
    return result


def check_walk_evidence(context, analysis):
    speeds = [block_summary(block, context)["walk_speed_p50"] for block in analysis["side_blocks"]["A"]]
    known = [speed for speed in speeds if speed is not None]
    if not known:
        context["warnings"].append(f"WQ5: 블록에 {WALK_SPEED} 열이 없어 걷기를 확인하지 못했다")
        return None
    slow = [speed for speed in known if speed < MINIMUM_WALK_SPEED]
    return None if not slow else f"WQ5 A 블록 {WALK_SPEED} p50 {min(slow):.1f}cm/s < {MINIMUM_WALK_SPEED:g}(폰이 걷지 않음)"


def decide_walk_row(context, reference):
    analysis = analyze_scene(context, "WQ5")
    spec = {"row": "GW", "scene": "WQ5", "limit": GAME_THREAD_LIMIT_MS, "label": "게임 스레드 물 합 A p50 + 이동 프레임 명명 GPU p95"}
    game_thread = decide_interval_row(context, analysis, GAME_THREAD_METRIC, spec, reference)
    row = {**game_thread, "metric": "game_thread_water_and_moving_gpu", "game_thread_verdict": game_thread["verdict"], "reasons": list(game_thread["reasons"]), "moving": None}
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
    row["moving"] = evaluate_moving_frames(context, analysis, row["stage_rounds"])
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
    analysis = analyze_scene(context, "WQ0")
    row = {"row": "WQ0", "scene": "WQ0", "water_ref_ms": water_reference_ms, "range_ratio": list(REFERENCE_RATIO_RANGE), "range_ms": [round(water_reference_ms * ratio, 6) for ratio in REFERENCE_RATIO_RANGE],
           "named_a_p50": None, "ratio": None, "valid": False, "status": None, "detail": ""}
    first_scene = (context["session"].get("scenes") or [{}])[0].get("id")
    row["is_first_scene"] = first_scene == "WQ0"
    if analysis["present"] and not row["is_first_scene"]:
        context["warnings"].append(f"WQ0가 첫 장면이 아니다(첫 장면 {first_scene})")
    if analysis["blockers"]:
        row.update({"status": analysis["blockers"][0]["reason"], "detail": "; ".join(item["detail"] for item in analysis["blockers"])})
        return row
    missing = find_missing_columns(analysis, GPU_METRIC, context)
    if missing:
        row.update({"status": "필수 열 없음", "detail": ", ".join(missing)})
        return row
    values = [block_summary(block, context) for block in analysis["side_blocks"]["A"]]
    valid_values = [summary["named_p50"] for summary in values if summary["pdh_valid"] and summary["named_p50"] is not None]
    row["pdh_valid_a_blocks"] = f"{len(valid_values)}/{len(values)}"
    if not valid_values:
        row.update({"status": "하위 스텝", "detail": "PD-H 유효 A 블록이 없다"})
        return row
    row["named_a_p50"] = median_of(valid_values)
    row["ratio"] = row["named_a_p50"] / water_reference_ms
    row["valid"] = REFERENCE_RATIO_RANGE[0] - EPSILON <= row["ratio"] <= REFERENCE_RATIO_RANGE[1] + EPSILON
    row["status"] = "유효" if row["valid"] else "범위 밖"
    row["detail"] = "" if row["valid"] else f"WQ0 명명 GPU A p50 {round_four(row['named_a_p50'])}ms가 기준 {water_reference_ms}ms의 {REFERENCE_RATIO_RANGE[0]}~{REFERENCE_RATIO_RANGE[1]}배({row['range_ms'][0]}~{row['range_ms'][1]}ms) 밖"
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
    if "판정 불가" in verdicts or not reference["valid"]:
        return "판정 불가"
    return "충족(proposed)"


def describe_consequence(budget_verdict):
    if budget_verdict == "충족(proposed)":
        return "FX-4b 성능 예산 충족(proposed): 1024² 화면 승인(RM:533)만 남는다"
    if budget_verdict == "미충족":
        return "레버 잔여(L1·L3·S2·S3·S5) 효과를 실측으로 더 낸 뒤 다시 재거나 예산 개정을 결정 기록에 proposed로 올린다"
    return "판정 불가 사유를 해소한 뒤 같은 계획으로 다시 잰다"


BLOCK_TABLE_KEYS = ("block_id", "side", "rows", "pdh_ratio", "pdh_valid", "named_p50", "water_p50", "game_thread_water_p50", "game_thread_water_p95", "substeps_p50", "substeps_max", "pressure_mode_max",
                    "step_count_max", "walk_speed_p50", "assumed_zero")


def describe_block(summary, backend):
    entry = {key: summary[key] for key in BLOCK_TABLE_KEYS}
    if backend == "CPU":
        entry.update({"pdh_ratio": None, "pdh_valid": None})
    return entry


def build_block_table(context):
    table = {}
    for scene_id, analysis in context["analyses"].items():
        if not analysis["pairs"]:
            continue
        table[scene_id] = [describe_block(block_summary(block, context), analysis["backend"]) for side in ("A", "B") for block in analysis["side_blocks"][side]]
    return table


def run_judge(context, water_reference_ms):
    reference = decide_reference(context, water_reference_ms)
    rows = {}
    for spec in GPU_ROW_SPECS:
        rows[spec["row"]] = decide_interval_row(context, analyze_scene(context, spec["scene"]), GPU_METRIC, spec, reference)
    for spec in GAME_THREAD_ROW_SPECS:
        rows[spec["row"]] = decide_interval_row(context, analyze_scene(context, spec["scene"]), GAME_THREAD_METRIC, spec, reference)
    rows["GW"] = decide_walk_row(context, reference)
    records = {"WQ1": build_record_row(context, "WQ1", True), "WQ4": build_record_row(context, "WQ4", False)}
    budget_verdict = decide_budget(reference, rows)
    return {"mode": "judge", "run_dir": str(context["run_dir"]), "run_token": context["session"].get("run_token"), "water_ref_ms": water_reference_ms, "reference": reference, "rows": rows,
            "records": records, "blocks": build_block_table(context), "budget_verdict": budget_verdict, "consequence": describe_consequence(budget_verdict), "asset_hashes": context["session"].get("asset_hashes"),
            "build_gate": context["session"].get("build_gate"), "warnings": context["warnings"]}


def pilot_scene(context, scene_id):
    analysis = analyze_scene(context, scene_id)
    entry = {"scene": scene_id, "present": analysis["present"], "backend": analysis["backend"], "grid": analysis["grid"], "blockers": analysis["blockers"], "blocks": [], "missing_columns": {}}
    if not analysis["pairs"]:
        return entry
    for side in ("A", "B"):
        for block in analysis["side_blocks"][side]:
            entry["blocks"].append(describe_block(block_summary(block, context), analysis["backend"]))
    if analysis["backend"] == "GPU":
        needed = {"명명 GPU": GPU_METRIC["needed"], "게임 스레드 물 합": GAME_THREAD_METRIC["needed"]}
        for label, names in needed.items():
            entry["missing_columns"][label] = find_missing_columns(analysis, {"needed": names}, context)
    entry["records"] = collect_gpu_records(analysis, context, None) if analysis["backend"] == "GPU" and analysis["pairs"] else None
    if scene_id == "WQ5":
        entry["moving"] = evaluate_moving_frames(context, analysis, None) if not entry["missing_columns"].get("명명 GPU") else None
        entry["walk_problem"] = check_walk_evidence(context, analysis)
    return entry


def run_pilot(context, water_reference_ms):
    scenes = {scene_id: pilot_scene(context, scene_id) for scene_id in SCENE_DEFINITIONS if scene_id in context["records"]}
    for scene_id in SCENE_DEFINITIONS:
        if scene_id not in context["records"]:
            context["warnings"].append(f"{scene_id}: session.json에 장면이 없다")
    ready = bool(scenes) and all(not entry["blockers"] and not any(entry["missing_columns"].values()) and not entry.get("walk_problem") for entry in scenes.values())
    reference = decide_reference(context, water_reference_ms) if water_reference_ms and "WQ0" in scenes else None
    invalid_blocks = [block["block_id"] for entry in scenes.values() for block in entry["blocks"] if block["side"] == "A" and block["pdh_valid"] is False]
    return {"mode": "pilot", "run_dir": str(context["run_dir"]), "run_token": context["session"].get("run_token"), "scenes": scenes, "reference": reference, "ready": ready,
            "pdh_invalid_blocks": invalid_blocks, "warnings": context["warnings"]}


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
    lines = [f"[pilot] {result['run_dir']}"]
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
        lines.append(f"WQ0 기준: {result['reference']['status']} 비율 {format_number(result['reference']['ratio'])}")
    lines.append(f"PD-H 무효 A 블록: {result['pdh_invalid_blocks'] or '없음'}")
    lines.append(f"시운전 준비: {'예' if result['ready'] else '아니오'}")
    lines.extend(f"경고: {warning}" for warning in result["warnings"])
    return lines


def format_interval(outcome):
    if not outcome or "interval" not in outcome:
        return "구간 없음"
    interval = outcome["interval"]
    return f"[{format_number(interval['lower'])}, {format_number(interval['upper'])}] 순위 {interval['rank']} 쌍 {outcome['pairs']}"


def format_judge(result):
    reference = result["reference"]
    lines = [f"[judge] {result['run_dir']}", f"WQ0 기준: {reference['status']} A p50 {format_number(reference['named_a_p50'])} 기준 {reference['water_ref_ms']} 범위 {reference['range_ms']} {reference['detail']}"]
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
    parser = argparse.ArgumentParser(description="FX-4b 예산 PIE 판정 분석기")
    parser.add_argument("mode", choices=["pilot", "judge"])
    parser.add_argument("run_dir", type=Path)
    parser.add_argument("--water-ref-ms", type=float)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    if args.mode == "judge" and (args.water_ref_ms is None or args.water_ref_ms <= 0):
        parser.error("judge에는 양수 --water-ref-ms가 필요하다")
    context, error = load_context(args.run_dir)
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
