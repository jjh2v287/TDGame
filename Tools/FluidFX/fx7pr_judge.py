"""실행 환경: 시스템 Python 3(표준 라이브러리만), 에디터·빌드 불필요. 입력은 pie_profile_fluid.py --csv-gpu 실행 폴더(Saved/FluidFX/fx7p/<run>/).
실행 명령: python -B Tools/FluidFX/fx7pr_judge.py pilot|judge <run_dir> [--out <json>]
출력: 표준 출력에 사람이 읽는 요약, --out이면 같은 내용을 JSON으로 저장(pilot = 장면별 측 관문과 고른 SP, judge = REF 유효성·S6 J2·F1/F2 J1·수용 3 1차 귀결). 판정 불가는 이름 붙은 사유.
상태: 2026-10-23 FX-7p-r 1부 판정 분석기(Saved/AgentOps/20261023/fx7pr/spec-fx7pr-a.md 5~7절), 시험은 같은 폴더 judge-fixtures/run_fixtures.py.
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
GPU_TIME = "GPUTime"
SOLVER = "GPU/TDFluidGpuSolver"
TRANSLUCENCY = "GPU/Translucency"
STEP_COUNT = "TDFluid/StepCount"
BLOCK_METRICS = (FRAME, GAME_THREAD, GPU_TIME, SOLVER, TRANSLUCENCY, STEP_COUNT)
REPORT_METRICS = (FRAME, GAME_THREAD, GPU_TIME, SOLVER, TRANSLUCENCY)
GATED_COLUMNS = (FRAME, GAME_THREAD, GPU_TIME)
HEADER_AT_END_MARKER = "[HasHeaderRowAtEnd]"

EPSILON = 1e-9
REGIME_MINIMUM_MS = 1.0
STABILITY_RELATIVE = 0.10
STABILITY_SMALL_MEDIAN_MS = 0.2
STABILITY_SMALL_RANGE_MS = 0.05
STAGE_ROUNDS = (3, 5)
SCREEN_PERCENTAGE_LADDER = (100, 130, 160, 200)
PILOT_STRONG_MEDIAN_MS = 1.5
PILOT_STRONG_BLOCK_MINIMUM_MS = 0.5
J2_LIMIT_MS = 1.7
REFERENCE_RANGE_MS = (0.40, 0.60)
SMOKE_SOLVER_MINIMUM_MS = 1.40
SMOKE_MEGACELLS = 1.048576
SMOKE_MINIMUM_MS_PER_MEGACELL = 1.0
FIRE_SOLVER_MINIMUM_MS = 0.046
DEFAULT_DISCARD_ROWS = 60

SLOT_PATTERN = re.compile(r"_r(\d+)_(\d)([AB])$")
RETRY_PATTERN = re.compile(r"_retry\d+$")
PILOT_SCENE_PATTERN = re.compile(r"(S6|F1|F2)_sp(\d+)")
BACKEND_PATTERN = re.compile(r"(GPU|CPU)", re.IGNORECASE)
SCENE_DEFINITION_BACKEND = {"REF": "GPU", "S6": "GPU", "F1": "CPU", "F2": "GPU"}
PAIR_POSITIONS = ((1, 2), (4, 3))
SIDE_POSITIONS = {"A": (1, 4), "B": (2, 3)}
ZERO_STATS = {"n": 0, "p50": 0.0, "p95": 0.0, "mean": 0.0, "max": 0.0, "assumed_zero": True}
NOISE_RISK_NOTE = "이 식은 잡음 폭이 클수록 통과가 쉬워진다(understand-rules.md 4.4절)"


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


def summarize_values(values):
    if not values:
        return None
    return {"n": len(values), "p50": percentile(values, 0.5), "p95": percentile(values, 0.95), "mean": sum(values) / len(values), "max": max(values)}


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def read_csv_columns(path, discard_rows):
    with open(path, encoding="utf-8", errors="replace", newline="") as handle:
        rows = list(csv.reader(handle))
    if not rows:
        return {}
    metadata_index = next((index for index, row in enumerate(rows) if row and row[0].startswith("[")), len(rows))
    has_header_at_end = metadata_index < len(rows) and HEADER_AT_END_MARKER in rows[metadata_index]
    header = rows[metadata_index - 1] if has_header_at_end else rows[0]
    data_rows = rows[1:metadata_index - 1] if has_header_at_end else rows[1:metadata_index]
    header = [name.strip() for name in header]
    columns = {name: [] for name in header}
    for row in data_rows[discard_rows:]:
        if len(row) > len(header):
            continue
        for name, value in zip(header, row):
            try:
                columns[name].append(float(value))
            except ValueError:
                continue
    return columns


def load_context(run_dir):
    run_dir = Path(run_dir)
    session_path = run_dir / "session.json"
    if not session_path.is_file():
        return None, f"session.json이 없다: {session_path}"
    session = read_json(session_path)
    plan_path = run_dir / "plan.json"
    plan = read_json(plan_path) if plan_path.is_file() else {}
    context = {
        "run_dir": run_dir,
        "session": session,
        "plan": plan,
        "records": {record["id"]: record for record in session.get("scenes", [])},
        "plan_scenes": {scene["id"]: scene for scene in plan.get("scenes", [])},
        "discard_rows": plan.get("discard_frames", DEFAULT_DISCARD_ROWS),
        "statistics_cache": {},
        "csv_cache": {},
        "csv_fallback_blocks": set(),
        "warnings": [],
    }
    return context, None


def scene_screen_percentage(context, scene_id):
    plan_meta = (context["plan_scenes"].get(scene_id) or {}).get("meta") or {}
    record_meta = (context["records"].get(scene_id) or {}).get("meta") or {}
    value = plan_meta.get("screen_percentage", record_meta.get("screen_percentage"))
    return value


def block_csv_path(block, context):
    relative = block.get("csv")
    if not relative:
        return None
    path = context["run_dir"] / relative
    return path if path.is_file() else None


def block_available(block, context):
    return bool(block.get("stats")) or block_csv_path(block, context) is not None


def block_statistics(block, context):
    key = (block.get("scene", ""), block["block_id"])
    cached = context["statistics_cache"].get(key)
    if cached is not None:
        return cached
    stats = {name: entry for name, entry in (block.get("stats") or {}).items() if entry}
    missing = [name for name in BLOCK_METRICS if name not in stats]
    path = block_csv_path(block, context) if missing else None
    if path is not None:
        if path not in context["csv_cache"]:
            context["csv_cache"][path] = read_csv_columns(path, context["discard_rows"])
        columns = context["csv_cache"][path]
        for name in missing:
            summary = summarize_values(columns.get(name) or [])
            if summary is None:
                continue
            stats[name] = summary
            context["csv_fallback_blocks"].add(block["block_id"])
    if block.get("side") == "B":
        apply_assumed_zero(stats)
    context["statistics_cache"][key] = stats
    return stats


def apply_assumed_zero(stats):
    if STEP_COUNT not in stats:
        stats[STEP_COUNT] = dict(ZERO_STATS)
    if SOLVER not in stats and stats[STEP_COUNT]["max"] == 0:
        stats[SOLVER] = dict(ZERO_STATS)
    if TRANSLUCENCY not in stats:
        stats[TRANSLUCENCY] = dict(ZERO_STATS)


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
    return next((block for block in attempts if block.get("status", "ok") == "ok" and block_available(block, context)), None)


def build_rounds(record, context):
    rounds = {}
    for slot, attempts in group_slot_attempts(record).items():
        match = SLOT_PATTERN.search(slot)
        if match is None:
            continue
        number, position, side = int(match.group(1)), int(match.group(2)), match.group(3)
        rounds.setdefault(number, {})[position] = {"slot": slot, "side": side, "attempts": len(attempts), "block": choose_attempt(attempts, context)}
    return rounds


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
                    "a_stats": None, "b_stats": None}
            if not pair["discarded"]:
                pair["a_stats"] = block_statistics(a_block, context)
                pair["b_stats"] = block_statistics(b_block, context)
            pairs.append(pair)
    return pairs


def valid_pairs_in(pairs, round_numbers):
    allowed = set(round_numbers)
    return [pair for pair in pairs if not pair["discarded"] and pair["round"] in allowed]


def pair_difference(pair, metric, quantile):
    a_entry = pair["a_stats"].get(metric)
    b_entry = pair["b_stats"].get(metric)
    if not a_entry or not b_entry:
        return None
    a_value = a_entry.get(quantile)
    b_value = b_entry.get(quantile)
    if a_value is None or b_value is None:
        return None
    return round(a_value - b_value, 4)


def collect_differences(pairs, metric, quantile):
    values = [pair_difference(pair, metric, quantile) for pair in pairs]
    return [value for value in values if value is not None]


def side_value(pairs, side, metric, quantile="p50"):
    key = "a_stats" if side == "A" else "b_stats"
    values = [pair[key][metric][quantile] for pair in pairs if pair[key].get(metric) and pair[key][metric].get(quantile) is not None]
    return median_of(values)


def spread_of(values):
    return round_four(max(values) - min(values)) if values else None


def add_blocker(analysis, reason, detail):
    analysis["blockers"].append({"reason": reason, "detail": detail})


def collect_side_blocks(rounds):
    sides = {"A": [], "B": []}
    for number in sorted(rounds):
        for position in sorted(rounds[number]):
            slot = rounds[number][position]
            if slot["block"] is not None:
                sides[slot["side"]].append(slot["block"])
    return sides


def find_missing_columns(side_blocks, required_a, required_b, context):
    missing = []
    for side, blocks, required in (("A", side_blocks["A"], required_a), ("B", side_blocks["B"], required_b)):
        for name in required:
            if any(name not in block_statistics(block, context) for block in blocks):
                missing.append(f"{side}:{name}")
    return missing


def check_regime(side_blocks, context):
    result = {}
    for side, blocks in side_blocks.items():
        values = []
        for block in blocks:
            stats = block_statistics(block, context)
            gpu = (stats.get(GPU_TIME) or {}).get("p50")
            game = (stats.get(GAME_THREAD) or {}).get("p50")
            if gpu is None or game is None:
                continue
            values.append({"block": block["block_id"], "value": round(gpu - game, 4)})
        numbers = [entry["value"] for entry in values]
        median_value = round_four(median_of(numbers))
        minimum = min(numbers) if numbers else None
        passed = bool(numbers) and median_value >= REGIME_MINIMUM_MS - EPSILON and minimum > 0
        strong = bool(numbers) and median_value >= PILOT_STRONG_MEDIAN_MS - EPSILON and minimum > PILOT_STRONG_BLOCK_MINIMUM_MS
        result[side] = {"blocks": values, "median": median_value, "minimum": minimum, "passed": passed, "strong": strong}
    result["passed"] = result["A"]["passed"] and result["B"]["passed"]
    result["strong"] = result["A"]["strong"] and result["B"]["strong"]
    return result


def check_spread(values):
    median = median_of(values)
    spread = max(values) - min(values)
    if median < STABILITY_SMALL_MEDIAN_MS:
        return {"values": values, "median": median, "range": spread, "kind": "absolute_ms", "value": spread, "limit": STABILITY_SMALL_RANGE_MS, "passed": spread <= STABILITY_SMALL_RANGE_MS + EPSILON}
    relative = spread / median
    return {"values": values, "median": median, "range": spread, "kind": "relative", "value": relative, "limit": STABILITY_RELATIVE, "passed": relative <= STABILITY_RELATIVE + EPSILON}


def build_round_table(rounds, context):
    table = {}
    for number in sorted(rounds):
        entry = {"round": number}
        for side, positions in SIDE_POSITIONS.items():
            values = []
            for position in positions:
                slot = rounds[number].get(position)
                if not slot or slot["block"] is None or slot["side"] != side:
                    continue
                p50 = (block_statistics(slot["block"], context).get(FRAME) or {}).get("p50")
                if p50 is not None:
                    values.append(p50)
            entry[side] = sum(values) / len(values) if values else None
            entry[side + "_blocks"] = len(values)
        entry["c"] = entry["A"] - entry["B"] if entry["A"] is not None and entry["B"] is not None else None
        table[number] = entry
    return table


def build_stability_stage(round_table, round_numbers):
    trimmed = len(round_numbers) > 3
    stage = {"rounds": round_numbers, "trimmed": trimmed, "sides": {}}
    statuses = []
    for side in ("A", "B"):
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


def summarize_observed(pairs):
    valid = [pair for pair in pairs if not pair["discarded"]]
    observed = {"pairs": len(pairs), "valid_pairs": len(valid), "a": {}, "b": {}, "diff": {}}
    for metric in REPORT_METRICS:
        for side in ("A", "B"):
            p50 = side_value(valid, side, metric, "p50")
            if p50 is not None:
                observed[side.lower()][metric] = {"p50": p50, "p95": side_value(valid, side, metric, "p95"), "blocks": len(valid)}
        entry = {}
        for quantile in ("p50", "p95"):
            values = collect_differences(valid, metric, quantile)
            entry[quantile] = round_four(median_of(values))
            entry["pairs_" + quantile] = values
            entry["range_" + quantile] = spread_of(values)
        if entry["pairs_p50"]:
            observed["diff"][metric] = entry
    return observed


def normalize_backend(value):
    match = BACKEND_PATTERN.search(str(value))
    return match.group(1).upper() if match else None


def is_target_actor(actor, targets):
    if targets is None:
        return True
    return actor.get("label") in targets or ("spawned" in targets and bool(actor.get("spawned")))


def describe_actor_problems(actor, expected_backend, expected_grid):
    problems = []
    if expected_backend is not None and normalize_backend(actor.get("backend")) != expected_backend:
        problems.append(f"backend {actor.get('backend')} (기대 {expected_backend})")
    if expected_grid is not None and actor.get("grid_resolution") != expected_grid:
        problems.append(f"grid {actor.get('grid_resolution')} (기대 {expected_grid})")
    return problems


def check_actor_settings(scene_id, plan_scene, side_blocks):
    meta = plan_scene.get("meta") or {}
    expected_backend = normalize_backend(meta["backend"]) if meta.get("backend") else SCENE_DEFINITION_BACKEND.get(scene_id)
    expected_grid = meta.get("resolution")
    targets = plan_scene.get("targets")
    result = {"expected_backend": expected_backend, "expected_grid": expected_grid, "checked_blocks": 0, "unverified_blocks": 0, "mismatches": []}
    for side in ("A", "B"):
        for block in side_blocks[side]:
            actors = [actor for actor in (block.get("actors") or []) if is_target_actor(actor, targets)]
            if not actors:
                result["unverified_blocks"] += 1
                continue
            result["checked_blocks"] += 1
            for actor in actors:
                problems = describe_actor_problems(actor, expected_backend, expected_grid)
                if problems:
                    result["mismatches"].append({"block": block["block_id"], "actor": actor.get("label"), "problems": problems})
    return result


def apply_actor_settings_check(context, analysis, plan_scene):
    check = check_actor_settings(analysis["scene"], plan_scene, analysis["side_blocks"])
    analysis["actors_check"] = check
    if check["mismatches"]:
        summary = "; ".join(f"{item['block']} {item['actor']}: {', '.join(item['problems'])}" for item in check["mismatches"][:3])
        add_blocker(analysis, "유효성", f"(b) 액터 설정 {len(check['mismatches'])}건 어긋남: {summary}")
        return
    if check["unverified_blocks"]:
        context["warnings"].append(f"{analysis['scene']}: 블록 {check['unverified_blocks']}개에 대상 액터 기록이 없어 (b) 액터 설정을 확인하지 못했다")


def analyze_scene(context, scene_id, gated, extra_required=()):
    record = context["records"].get(scene_id)
    analysis = {"scene": scene_id, "present": record is not None, "gated": gated, "blockers": []}
    if record is None:
        add_blocker(analysis, "장면 없음", f"{scene_id} 장면이 session.json에 없다")
        return analysis
    plan_scene = context["plan_scenes"].get(scene_id, {})
    analysis["screen_percentage"] = scene_screen_percentage(context, scene_id)
    if record.get("error"):
        add_blocker(analysis, "장면 오류", str(record["error"])[:160])
        return analysis
    shader_gate = record.get("shader_gate") or {}
    if shader_gate.get("passed") is False:
        add_blocker(analysis, "셰이더 관문", "예열 셰이더 관문 실패(연속 p50 변화 10% 안·돌출 0)")
    rounds = build_rounds(record, context)
    analysis["rounds"] = rounds
    analysis["round_numbers"] = sorted(rounds)
    if not rounds:
        add_blocker(analysis, "쌍 부족", "측정 블록 없음")
        return analysis
    pairs = build_pairs(rounds, analysis["round_numbers"], context)
    analysis["pairs"] = pairs
    analysis["side_blocks"] = collect_side_blocks(rounds)
    analysis["observed"] = summarize_observed(pairs)
    if gated and analysis["screen_percentage"] is None:
        add_blocker(analysis, "SP 없음", f"{scene_id}의 계획·세션 meta에 screen_percentage가 없다(100으로 가정하지 않음)")
        return analysis
    apply_actor_settings_check(context, analysis, plan_scene)
    missing_rounds = [number for number in range(1, analysis["round_numbers"][-1] + 1) if number not in rounds]
    if missing_rounds:
        add_blocker(analysis, "쌍 부족", f"라운드 결측: {missing_rounds}")
    leading_rounds = 0
    while leading_rounds + 1 in rounds:
        leading_rounds += 1
    required_a = set(plan_scene.get("required_columns", [FRAME])) | set(extra_required) | (set(GATED_COLUMNS) if gated else set())
    required_b = set(GATED_COLUMNS) if gated else {FRAME}
    missing = find_missing_columns(analysis["side_blocks"], sorted(required_a), sorted(required_b), context)
    if missing:
        add_blocker(analysis, "필수 열 없음", ", ".join(missing))
        return analysis
    analysis["clock"] = stage_clock(analysis, None)
    if not gated:
        return analysis
    analysis["regime"] = check_regime(analysis["side_blocks"], context)
    if not analysis["regime"]["passed"]:
        failed = [side for side in ("A", "B") if not analysis["regime"][side]["passed"]]
        add_blocker(analysis, "체제", f"측 관문 미달: {', '.join(failed)}(중앙값 ≥ +{REGIME_MINIMUM_MS}ms 그리고 모든 블록 > 0)")
    analysis["round_table"] = build_round_table(rounds, context)
    analysis["stages"] = {count: build_stability_stage(analysis["round_table"], analysis["round_numbers"][:count]) for count in STAGE_ROUNDS if leading_rounds >= count}
    return analysis


def order_statistic_rank(pair_count):
    if pair_count < 2:
        return None
    return 1 if pair_count <= 7 else 2


def classify_interval(lower, upper):
    if upper <= J2_LIMIT_MS + EPSILON:
        return "통과"
    if lower > J2_LIMIT_MS + EPSILON:
        return "미달"
    return "걸침"


def combine_states(plain_state, scaled_state):
    if plain_state == scaled_state:
        return plain_state
    if "걸침" in (plain_state, scaled_state):
        return "걸침"
    return "SP 환산 갈림"


def make_interval(values, rank):
    ordered = sorted(values)
    lower = ordered[rank - 1]
    upper = ordered[-rank]
    return {"lower": round(lower, 6), "upper": round(upper, 6), "rank": rank, "state": classify_interval(lower, upper)}


def judge_interval_stage(analysis, round_count):
    pairs = valid_pairs_in(analysis["pairs"], analysis["round_numbers"][:round_count])
    outcome = {"pairs": len(pairs)}
    rank = order_statistic_rank(len(pairs))
    if rank is None:
        outcome["state"] = "쌍 부족"
        outcome["detail"] = f"유효 쌍 {len(pairs)}개(최소 2쌍)"
        return outcome
    plain = collect_differences(pairs, FRAME, "p50")
    screen_percentage = analysis["screen_percentage"]
    scale = 0.0 if screen_percentage == 100 else 1 - (100 / screen_percentage) ** 2
    scaled = []
    for pair in pairs:
        translucency = pair_difference(pair, TRANSLUCENCY, "p50") or 0.0
        scaled.append(round(pair_difference(pair, FRAME, "p50") - translucency * scale, 6))
    outcome["interval"] = make_interval(plain, rank)
    outcome["interval_scaled"] = make_interval(scaled, rank)
    outcome["scale"] = scale
    outcome["state"] = combine_states(outcome["interval"]["state"], outcome["interval_scaled"]["state"])
    return outcome


def stage_clock(analysis, round_count):
    numbers = set(analysis["round_numbers"][:round_count] if round_count else analysis["round_numbers"])
    pairs = [pair for pair in analysis["pairs"] if pair["round"] in numbers]
    discarded = sum(1 for pair in pairs if pair["discarded"])
    return {"pairs": len(pairs), "discarded_pairs": discarded, "exceeded": discarded * 3 > len(pairs)}


def inspect_stage(analysis, round_count):
    stage = analysis["stages"].get(round_count)
    if stage is None:
        return {"rounds": round_count, "gate": "미측정", "usable": False}
    entry = {"rounds": round_count, "gate": stage["status"], "clock": stage_clock(analysis, round_count), "usable": False}
    if stage["status"] != "pass":
        return entry
    if entry["clock"]["exceeded"]:
        entry["terminal"] = True
        entry["state"] = "클럭 폐기 초과"
        entry["detail"] = f"{round_count}라운드 쌍 {entry['clock']['pairs']}개 중 {entry['clock']['discarded_pairs']}개 폐기(1/3 초과)"
        return entry
    entry["usable"] = True
    return entry


def walk_stages(analysis, judge_stage):
    trace = []
    for count in STAGE_ROUNDS:
        entry = inspect_stage(analysis, count)
        trace.append(entry)
        if entry.get("terminal"):
            return trace, None, None
        if not entry["usable"]:
            continue
        outcome = judge_stage(analysis, count)
        entry.update(outcome)
        if outcome["state"] in ("통과", "미달", "SP 환산 갈림"):
            return trace, count, outcome
    return trace, None, None


def choose_shared_stage(analyses):
    traces = {scene_id: [] for scene_id in analyses}
    for count in STAGE_ROUNDS:
        entries = {scene_id: inspect_stage(analysis, count) for scene_id, analysis in analyses.items()}
        for scene_id, entry in entries.items():
            traces[scene_id].append(entry)
        if any(entry["gate"] != "pass" for entry in entries.values()):
            continue
        if all(entry["usable"] for entry in entries.values()):
            return count, traces
        return None, traces
    return None, traces


def unresolved_reason(trace):
    measured = [entry for entry in trace if entry["gate"] != "미측정"]
    if not measured:
        return "쌍 부족", f"측정 라운드가 {STAGE_ROUNDS[0]}개 미만"
    last = measured[-1]
    if last["gate"] == "fail":
        return "불안정", f"{last['rounds']}라운드 측 안정 관문 초과"
    if last["gate"] == "short":
        return "쌍 부족", f"{last['rounds']}라운드 값 일부 없음"
    if last.get("state") == "걸침":
        return "걸침", f"{last['rounds']}라운드({last['pairs']}쌍) 구간이 한도 {J2_LIMIT_MS}에 걸친다"
    return last.get("state", "쌍 부족"), last.get("detail", "")


def collect_shared_stage_reasons(traces):
    reasons = []
    for scene_id, trace in traces.items():
        measured = [entry for entry in trace if entry["gate"] != "미측정"]
        if measured and measured[-1]["usable"]:
            continue
        reason, detail = unresolved_reason(trace)
        reasons.append({"reason": reason, "detail": detail, "scene": scene_id})
    if not reasons:
        reasons.append({"reason": "쌍 부족", "detail": "F1·F2가 같은 라운드 수를 쓸 수 없다(5라운드 미측정)"})
    return reasons


def clean(value):
    if isinstance(value, dict):
        return {str(key): clean(item) for key, item in value.items()}
    if isinstance(value, (list, tuple, set)):
        return [clean(item) for item in value]
    if isinstance(value, float):
        return None if math.isnan(value) or math.isinf(value) else round(value, 6)
    return value


def stage_pairs_for(analysis, round_count):
    if "round_numbers" not in analysis:
        return []
    numbers = analysis["round_numbers"][:round_count] if round_count else analysis["round_numbers"]
    return valid_pairs_in(analysis.get("pairs", []), numbers)


def view_pairs(analysis, used_round_count):
    used = {pair["order"] for pair in stage_pairs_for(analysis, used_round_count)} if used_round_count else set()
    views = []
    for pair in analysis.get("pairs", []):
        view = {"order": pair["order"], "round": pair["round"], "a": pair["a"], "b": pair["b"], "discarded": pair["discarded"], "used": pair["order"] in used}
        if not pair["discarded"]:
            view.update({"ft_d_p50": pair_difference(pair, FRAME, "p50"), "ft_d_p95": pair_difference(pair, FRAME, "p95"), "gpu_time_d_p50": pair_difference(pair, GPU_TIME, "p50"),
                         "solver_d_p50": pair_difference(pair, SOLVER, "p50"), "translucency_d_p50": pair_difference(pair, TRANSLUCENCY, "p50")})
        views.append(view)
    return views


def view_round_table(analysis):
    return [analysis["round_table"][number] for number in sorted(analysis.get("round_table", {}))]


def decide_reference(context):
    analysis = analyze_scene(context, "REF", False)
    row = {"row": "REF", "scene": "REF", "range_ms": list(REFERENCE_RANGE_MS), "solver_a_p50": None, "valid": False, "reasons": list(analysis["blockers"])}
    if "pairs" in analysis:
        valid = [pair for pair in analysis["pairs"] if not pair["discarded"]]
        row["solver_a_p50"] = round_four(side_value(valid, "A", SOLVER))
        row["valid_pairs"] = len(valid)
        row["actors_check"] = analysis.get("actors_check")
        if not row["reasons"] and analysis["clock"]["exceeded"]:
            row["reasons"].append({"reason": "클럭 폐기 초과", "detail": f"REF 쌍 {analysis['clock']['pairs']}개 중 {analysis['clock']['discarded_pairs']}개 폐기(1/3 초과)"})
    first_scene = (context["session"].get("scenes") or [{}])[0].get("id")
    row["is_first_scene"] = first_scene == "REF"
    if analysis["present"] and not row["is_first_scene"]:
        context["warnings"].append(f"REF가 첫 장면이 아니다(첫 장면 {first_scene})")
    if row["reasons"]:
        row["status"] = row["reasons"][0]["reason"]
        return row
    if row["solver_a_p50"] is None:
        row["reasons"].append({"reason": "필수 열 없음", "detail": f"REF {SOLVER} A 값 없음"})
        row["status"] = "필수 열 없음"
        return row
    row["valid"] = REFERENCE_RANGE_MS[0] - EPSILON <= row["solver_a_p50"] <= REFERENCE_RANGE_MS[1] + EPSILON
    row["status"] = "유효" if row["valid"] else "범위 밖"
    return row


def describe_reference_failure(reference_row):
    if reference_row["status"] == "범위 밖":
        return f"(a) REF Sol A p50 {reference_row['solver_a_p50']}ms가 {REFERENCE_RANGE_MS[0]}~{REFERENCE_RANGE_MS[1]}ms 밖"
    details = "; ".join(item["detail"] for item in reference_row["reasons"])
    return f"(a) REF 확인 불가({reference_row['status']}): {details}"


def decide_smoke_validity(analysis, round_count, reference_row):
    reasons = []
    if not reference_row["valid"]:
        reasons.append({"reason": "유효성", "detail": describe_reference_failure(reference_row)})
    solver = side_value(stage_pairs_for(analysis, round_count), "A", SOLVER)
    validity = {"reference": {"solver_a_p50": reference_row["solver_a_p50"], "valid": reference_row["valid"]}, "solver_a_p50": round_four(solver)}
    if solver is None:
        if "stages" in analysis:
            reasons.append({"reason": "유효성", "detail": "(c)(d) 열 없음: S6 Sol A 값이 없어 유효성을 확인할 수 없다"})
        return reasons, validity
    megacell_floor = SMOKE_MINIMUM_MS_PER_MEGACELL * SMOKE_MEGACELLS
    validity["c_megacell_ok"] = solver >= megacell_floor - EPSILON
    validity["d_class_floor_ok"] = solver >= SMOKE_SOLVER_MINIMUM_MS - EPSILON
    validity["ms_per_megacell"] = round_four(solver / SMOKE_MEGACELLS)
    if not validity["c_megacell_ok"]:
        reasons.append({"reason": "유효성", "detail": f"(c) S6 Sol A p50 {round_four(solver)}ms < {megacell_floor}ms(1.0ms/Mcell x {SMOKE_MEGACELLS}Mcell)"})
    if not validity["d_class_floor_ok"]:
        reasons.append({"reason": "유효성", "detail": f"(d) S6 Sol A p50 {round_four(solver)}ms < {SMOKE_SOLVER_MINIMUM_MS}ms(4 x 0.35)"})
    return reasons, validity


def sum_pair_differences(pairs, first_metric, second_metric):
    totals = []
    for pair in pairs:
        first = pair_difference(pair, first_metric, "p50")
        second = pair_difference(pair, second_metric, "p50")
        if first is not None and second is not None:
            totals.append(first + second)
    return totals


def decide_j2(context, reference_row):
    screen_percentage = scene_screen_percentage(context, "S6")
    extra_required = [TRANSLUCENCY] if screen_percentage not in (None, 100) else []
    analysis = analyze_scene(context, "S6", True, extra_required)
    row = {"row": "J2", "scene": "S6", "metric": FRAME, "limit_ms": J2_LIMIT_MS, "screen_percentage": screen_percentage, "verdict": "판정 불가", "reason": None, "reasons": list(analysis["blockers"])}
    if screen_percentage is not None and screen_percentage <= 0:
        row["reasons"].append({"reason": "유효성", "detail": f"S6 r.ScreenPercentage {screen_percentage}가 양수가 아니라 SP100 환산 불가"})
        return finish_row(row, analysis, None)
    trace, final_count, outcome = [], None, None
    if "stages" in analysis:
        trace, final_count, outcome = walk_stages(analysis, judge_interval_stage)
        if outcome is None:
            reason, detail = unresolved_reason(trace)
            row["reasons"].append({"reason": reason, "detail": detail})
        if outcome is not None and outcome["state"] == "SP 환산 갈림":
            row["reasons"].append({"reason": "SP 환산 갈림", "detail": "d와 d′의 판정이 다르다(통과 대 미달)"})
    validity_reasons, validity = decide_smoke_validity(analysis, final_count, reference_row)
    row["reasons"].extend(validity_reasons)
    row.update({"validity": validity, "trace": trace, "stage_rounds": final_count, "outcome": outcome})
    if outcome is not None and not row["reasons"]:
        row["verdict"] = outcome["state"]
    if "pairs" in analysis:
        stage = stage_pairs_for(analysis, final_count)
        row["records"] = {"solver_a_p50": round_four(side_value(stage, "A", SOLVER)), "gpu_time_d_p50": round_four(median_of(collect_differences(stage, GPU_TIME, "p50"))),
                          "solver_translucency_d_p50": round_four(median_of(sum_pair_differences(stage, SOLVER, TRANSLUCENCY))),
                          "ft_d_p50": round_four(median_of(collect_differences(stage, FRAME, "p50"))), "occupancy_recorded": False}
    finish_row(row, analysis, final_count)
    attach_remeasure(row, row["screen_percentage"])
    return row


def next_screen_percentage(current):
    return next((step for step in SCREEN_PERCENTAGE_LADDER if step > current), None)


def attach_remeasure(row, screen_percentage):
    if not any(item["reason"] == "체제" for item in row["reasons"]):
        return
    following = next_screen_percentage(screen_percentage)
    message = "체제 관문 깨짐 → 다음 SP 재측정 필요" if following is not None else "체제 관문 깨짐, 다음 SP 단계 없음 → 판정 불가(체제)"
    row["remeasure"] = {"needed": following is not None, "message": message, "current_screen_percentage": screen_percentage, "next_screen_percentage": following}


def finish_row(row, analysis, final_count):
    if row["reasons"]:
        row["verdict"] = "판정 불가"
    row["reason"] = row["reasons"][0]["reason"] if row["reasons"] else None
    row["screen_percentage"] = analysis.get("screen_percentage", row.get("screen_percentage"))
    if "observed" in analysis:
        row["observed"] = analysis["observed"]
        row["pairs"] = view_pairs(analysis, final_count)
    if "regime" in analysis:
        row["regime"] = analysis["regime"]
        row["round_table"] = view_round_table(analysis)
        row["stability"] = {str(count): stage for count, stage in analysis["stages"].items()}
    if "clock" in analysis:
        row["clock"] = analysis["clock"]
    if "actors_check" in analysis:
        row["actors_check"] = analysis["actors_check"]
    return row


def scene_pair_statistics(analysis, round_count):
    pairs = stage_pairs_for(analysis, round_count)
    statistics_by_quantile = {}
    for quantile in ("p50", "p95"):
        values = collect_differences(pairs, FRAME, quantile)
        statistics_by_quantile[quantile] = {"d": round_four(median_of(values)), "range": spread_of(values), "pairs": values}
    return {"rounds": round_count, "valid_pairs": len(pairs), "p50": statistics_by_quantile["p50"], "p95": statistics_by_quantile["p95"]}


def decide_j1(context):
    analyses = {"F1": analyze_scene(context, "F1", True), "F2": analyze_scene(context, "F2", True)}
    row = {"row": "J1", "metric": FRAME, "verdict": "판정 불가", "reason": None, "reasons": [], "scenes": {}, "risk_note": NOISE_RISK_NOTE}
    for scene_id, analysis in analyses.items():
        row["reasons"].extend({**blocker, "scene": scene_id} for blocker in analysis["blockers"])
    ready = {scene_id: analysis for scene_id, analysis in analyses.items() if "stages" in analysis}
    chosen_count = None
    if len(ready) == len(analyses):
        chosen_count, traces = choose_shared_stage(ready)
        for scene_id, analysis in ready.items():
            row["scenes"][scene_id] = {"screen_percentage": analysis["screen_percentage"], "trace": traces[scene_id], "stage_rounds": chosen_count}
        if chosen_count is None:
            row["reasons"].extend(collect_shared_stage_reasons(traces))
    if chosen_count is not None:
        for scene_id, analysis in ready.items():
            row["scenes"][scene_id]["statistics"] = scene_pair_statistics(analysis, chosen_count)
        solver = side_value(stage_pairs_for(analyses["F2"], chosen_count), "A", SOLVER)
        row["validity"] = {"f2_solver_a_p50": round_four(solver), "minimum": FIRE_SOLVER_MINIMUM_MS, "d_class_floor_ok": solver is not None and solver >= FIRE_SOLVER_MINIMUM_MS - EPSILON}
        if not row["validity"]["d_class_floor_ok"]:
            row["reasons"].append({"reason": "유효성", "detail": f"(d) F2 Sol A p50 {round_four(solver)}ms < {FIRE_SOLVER_MINIMUM_MS}ms", "scene": "F2"})
    screen_percentages = {analysis["screen_percentage"] for analysis in analyses.values() if analysis["present"] and analysis.get("screen_percentage") is not None}
    if len(screen_percentages) > 1:
        context["warnings"].append(f"F1·F2 SP가 다르다: {sorted(screen_percentages)}")
    if not row["reasons"] and chosen_count is not None:
        evaluate_j1_formula(row)
    row["reason"] = row["reasons"][0]["reason"] if row["reasons"] else None
    for scene_id, analysis in analyses.items():
        if "pairs" in analysis:
            row["scenes"].setdefault(scene_id, {})["pairs"] = view_pairs(analysis, chosen_count)
            row["scenes"][scene_id]["observed"] = analysis["observed"]
        if "actors_check" in analysis:
            row["scenes"].setdefault(scene_id, {})["actors_check"] = analysis["actors_check"]
        if "regime" in analysis:
            row["scenes"].setdefault(scene_id, {})["regime"] = analysis["regime"]
            row["scenes"][scene_id]["round_table"] = view_round_table(analysis)
            row["scenes"][scene_id]["stability"] = {str(count): stage for count, stage in analysis["stages"].items()}
    present_percentages = [analysis["screen_percentage"] for analysis in analyses.values() if analysis.get("screen_percentage") is not None]
    if present_percentages:
        attach_remeasure(row, max(present_percentages))
    return row


def evaluate_j1_formula(row):
    first = row["scenes"]["F1"]["statistics"]
    second = row["scenes"]["F2"]["statistics"]
    values = {}
    limits = {}
    for quantile in ("p50", "p95"):
        ranges = [first[quantile]["range"], second[quantile]["range"]]
        diffs = [first[quantile]["d"], second[quantile]["d"]]
        if None in ranges or None in diffs:
            row["reasons"].append({"reason": "필수 열 없음", "detail": f"{quantile} 차분 없음"})
            return
        noise = (ranges[0] + ranges[1]) / 2.0
        values[quantile] = diffs[1]
        limits[quantile] = {"limit": round(diffs[0] + noise, 6), "noise_width": noise, "f1_d": diffs[0]}
    row["value"] = values
    row["limit"] = limits
    row["p50_ok"] = values["p50"] <= limits["p50"]["limit"] + EPSILON
    row["p95_ok"] = values["p95"] <= limits["p95"]["limit"] + EPSILON
    row["verdict"] = "통과" if row["p50_ok"] and row["p95_ok"] else "미달"


def decide_acceptance(rows):
    verdicts = [rows["J1"]["verdict"], rows["J2"]["verdict"]]
    if "미달" in verdicts:
        return "미충족"
    if "판정 불가" in verdicts:
        return "판정 불가"
    return "충족"


def collect_actions(rows):
    actions = []
    for name in ("J2", "J1"):
        remeasure = rows[name].get("remeasure")
        if remeasure:
            actions.append({"row": name, "action": remeasure["message"], "next_screen_percentage": remeasure["next_screen_percentage"]})
    if rows["REF"]["status"] == "범위 밖":
        actions.append({"row": "REF", "action": "REF 유효성(a) 실패 → 같은 계획 1회 재실행(명세 10절 4)", "next_screen_percentage": None})
    for name, row in rows.items():
        if any(item["reason"] == "장면 오류" for item in row["reasons"]):
            actions.append({"row": name, "action": "장면 오류 → 같은 계획 1회 재실행(명세 10절 4)", "next_screen_percentage": None})
    return actions


def run_judge(context):
    reference = decide_reference(context)
    rows = {"REF": reference, "J2": decide_j2(context, reference), "J1": decide_j1(context)}
    return {"mode": "judge", "run_dir": str(context["run_dir"]), "run_token": context["session"].get("run_token"), "rows": rows, "acceptance3_first": decide_acceptance(rows),
            "actions": collect_actions(rows), "csv_fallback_blocks": sorted(context["csv_fallback_blocks"]), "warnings": context["warnings"]}


def pilot_scene(context, scene_id, family, screen_percentage):
    record = context["records"][scene_id]
    entry = {"scene": scene_id, "family": family, "screen_percentage": screen_percentage, "passed": False, "strong": False, "reason": None, "sides": {}}
    if record.get("error"):
        entry["reason"] = "장면 오류"
        entry["detail"] = str(record["error"])[:160]
        return entry
    rounds = build_rounds(record, context)
    slots = [slot for number in rounds for slot in rounds[number].values()]
    if not slots:
        entry["reason"] = "쌍 부족"
        entry["detail"] = "측정 블록 없음"
        return entry
    side_blocks = collect_side_blocks(rounds)
    absent = [slot["slot"] for slot in slots if slot["block"] is None]
    missing = find_missing_columns(side_blocks, [GAME_THREAD, GPU_TIME], [GAME_THREAD, GPU_TIME], context)
    if missing:
        entry["reason"] = "필수 열 없음"
        entry["detail"] = ", ".join(missing)
        return entry
    regime = check_regime(side_blocks, context)
    entry["sides"] = {side: regime[side] for side in ("A", "B")}
    if absent:
        entry["reason"] = "쌍 부족"
        entry["detail"] = "채택 가능한 블록이 없는 슬롯: " + ", ".join(absent)
        return entry
    entry["passed"] = regime["passed"]
    entry["strong"] = regime["strong"]
    entry["reason"] = None if regime["passed"] else "체제"
    return entry


PILOT_CRITERIA = (("강", "strong", "측 중앙값 ≥ +1.5ms 그리고 모든 블록 > +0.5ms"), ("기본", "basic", "측 중앙값 ≥ +1.0ms 그리고 모든 블록 > 0"))


def select_pilot_step(entries_by_family, families):
    per_step = [set(entries_by_family[family]) for family in families]
    steps = sorted(set.intersection(*per_step)) if per_step else []
    checked = []
    for step in steps:
        entries = [entries_by_family[family][step] for family in families]
        checked.append({"screen_percentage": step, "strong": all(entry["strong"] for entry in entries), "basic": all(entry["passed"] for entry in entries),
                        "scenes": [entry["scene"] for entry in entries], "reasons": [entry["reason"] for entry in entries if entry["reason"]]})
    for label, key, rule in PILOT_CRITERIA:
        chosen = next((item for item in checked if item[key]), None)
        if chosen is not None:
            return {"screen_percentage": chosen["screen_percentage"], "criterion": label, "rule": rule, "reason": None, "checked": checked}
    skipped = any("SP 없음" in item["reasons"] for item in checked)
    reason = "SP 없음" if skipped else ("체제" if checked else "장면 없음")
    detail = "SP meta가 없는 단계가 있고 통과한 단계가 없다(판정 불가(SP 없음))" if skipped else ("어느 SP 단계도 강·기본 기준을 통과하지 못했다(PIE 판정 불가(체제))" if checked else "해당 장면의 SP 단계가 없다")
    return {"screen_percentage": None, "criterion": None, "reason": reason, "detail": detail, "checked": checked}


def run_pilot(context):
    entries = {"S6": {}, "F1": {}, "F2": {}}
    for scene_id in context["records"]:
        match = PILOT_SCENE_PATTERN.fullmatch(scene_id)
        if match is None:
            continue
        family, suffix = match.group(1), int(match.group(2))
        screen_percentage = scene_screen_percentage(context, scene_id)
        if screen_percentage is None:
            context["warnings"].append(f"{scene_id}: 계획·세션 meta에 screen_percentage가 없어 시운전 후보에서 뺀다(100으로 가정하지 않음)")
            entries[family][suffix] = {"scene": scene_id, "family": family, "screen_percentage": None, "passed": False, "strong": False, "reason": "SP 없음",
                                       "detail": "meta.screen_percentage 없음", "sides": {}}
            continue
        if screen_percentage != suffix:
            context["warnings"].append(f"{scene_id}: 장면 이름 SP {suffix}와 meta SP {screen_percentage}가 다르다(meta 사용)")
        entries[family][screen_percentage] = pilot_scene(context, scene_id, family, screen_percentage)
    selection = {"S6": select_pilot_step(entries, ("S6",)), "F1_F2": select_pilot_step(entries, ("F1", "F2"))}
    return {"mode": "pilot", "run_dir": str(context["run_dir"]), "run_token": context["session"].get("run_token"), "scenes": {family: [entries[family][step] for step in sorted(entries[family])] for family in entries},
            "selection": selection, "warnings": context["warnings"]}


def format_number(value):
    return "-" if value is None else f"{value:.4f}"


def format_pilot(result):
    lines = [f"[pilot] {result['run_dir']}"]
    for family, entries in result["scenes"].items():
        for entry in entries:
            sides = " ".join(f"{side}:중앙 {format_number(info['median'])} 최소 {format_number(info['minimum'])} 값 {[block['value'] for block in info['blocks']]}" for side, info in entry["sides"].items())
            status = ("통과(강)" if entry["strong"] else "통과(기본)") if entry["passed"] else f"미통과({entry['reason']}{': ' + entry['detail'] if entry.get('detail') else ''})"
            lines.append(f"  {entry['scene']} SP{entry['screen_percentage']} {status} | {sides}")
    for name, label in (("S6", "S6(J2용)"), ("F1_F2", "F1·F2(J1용 공통)")):
        selected = result["selection"][name]
        text = f"SP {selected['screen_percentage']} (기준 {selected['criterion']}: {selected['rule']})" if selected["screen_percentage"] is not None else f"고르지 못함: {selected['reason']} - {selected['detail']}"
        lines.append(f"고른 SP {label}: {text}")
    lines.extend(f"경고: {warning}" for warning in result["warnings"])
    return lines


def format_interval(interval):
    return f"[{format_number(interval['lower'])}, {format_number(interval['upper'])}] 순위 {interval['rank']} {interval['state']}"


def format_judge(result):
    rows = result["rows"]
    reference, j2, j1 = rows["REF"], rows["J2"], rows["J1"]
    lines = [f"[judge] {result['run_dir']}"]
    lines.append(f"REF: {reference['status']} Sol A p50 {format_number(reference['solver_a_p50'])} (범위 {reference['range_ms'][0]}~{reference['range_ms'][1]})")
    reason = f"({j2['reason']})" if j2["reason"] else ""
    lines.append(f"J2 S6 SP{j2['screen_percentage']}: {j2['verdict']}{reason} 한도 {J2_LIMIT_MS}ms 라운드 {j2.get('stage_rounds')}")
    for entry in j2.get("trace", []):
        lines.append(f"  경로 {entry['rounds']}라운드 관문 {entry['gate']} 상태 {entry.get('state', '-')} 쌍 {entry.get('pairs', '-')}")
    outcome = j2.get("outcome")
    if outcome and "interval" in outcome:
        lines.append(f"  d 구간 {format_interval(outcome['interval'])} | d′ 구간 {format_interval(outcome['interval_scaled'])}")
    if "records" in j2:
        lines.append(f"  기록 {j2['records']}")
    for item in j2["reasons"]:
        lines.append(f"  사유 {item['reason']}: {item['detail']}")
    if "remeasure" in j2:
        lines.append(f"  재측정 {j2['remeasure']['message']} (현재 SP {j2['remeasure']['current_screen_percentage']}, 다음 SP {j2['remeasure']['next_screen_percentage']})")
    reason = f"({j1['reason']})" if j1["reason"] else ""
    lines.append(f"J1 F2 대 F1: {j1['verdict']}{reason}")
    if "value" in j1:
        lines.append(f"  p50 값 {j1['value']['p50']} 한도 {j1['limit']['p50']['limit']} | p95 값 {j1['value']['p95']} 한도 {j1['limit']['p95']['limit']}")
    for scene_id, scene in j1["scenes"].items():
        lines.append(f"  {scene_id} SP{scene.get('screen_percentage')} 라운드 {scene.get('stage_rounds')} 통계 {scene.get('statistics')}")
    for item in j1["reasons"]:
        lines.append(f"  사유 {item.get('scene', '')} {item['reason']}: {item['detail']}")
    if "remeasure" in j1:
        lines.append(f"  재측정 {j1['remeasure']['message']} (현재 SP {j1['remeasure']['current_screen_percentage']}, 다음 SP {j1['remeasure']['next_screen_percentage']})")
    lines.append(f"수용 3 1차: {result['acceptance3_first']}")
    lines.extend(f"조치 {item['row']}: {item['action']}" + (f" → SP {item['next_screen_percentage']}" if item["next_screen_percentage"] is not None else "") for item in result["actions"])
    lines.extend(f"경고: {warning}" for warning in result["warnings"])
    return lines


def main():
    parser = argparse.ArgumentParser(description="FX-7p-r 1부 판정 분석기")
    parser.add_argument("mode", choices=["pilot", "judge"])
    parser.add_argument("run_dir", type=Path)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")
    context, error = load_context(args.run_dir)
    if error is not None:
        print(error, file=sys.stderr)
        return 2
    result = run_pilot(context) if args.mode == "pilot" else run_judge(context)
    print("\n".join(format_pilot(result) if args.mode == "pilot" else format_judge(result)))
    if args.out is not None:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(clean(result), ensure_ascii=False, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
