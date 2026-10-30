"""실행 환경: 시스템 Python 3(에디터 불필요, look 지표는 measure_fx_look.py 의 numpy·Pillow·scipy 필요). calib_sweep.py 의 sweep.json 을 사전 등록 잠금(prereg_lock.py) 확인 뒤 판정한다. 옛 스윕(stage 없음, spec-wave0-data-bridge-v2 3.9): look 장면은 단계 이미지를 영역 틀로 measure_fx_look.py --targets 에 넣어 LIVE-2 목표 범위를(지표 <n>.<영역>.<지표> 와 N장 중앙값 <영역>.<지표>), 물 장면은 pie_probe_fluid_water 규칙 값을 지표로 삼고 반복 간 범위를 잡음으로 쓴다. 단계 스윕(stage screen|confirm|verify, spec-cal0-v2 1.1·1.2·G-JUDGE·G-DERIVED): 장면 sources(look·water_probe·probe·offline) 지표를 합치고(look 은 목표 파일에 live2_targets 가 있으면 불 경로, 없으면 기본 모드 측정을 펼친 일반 경로이고 장수 = 잠긴 계획 shots.count·shots_saved 참, 단계 metrics 의 숫자 목록과 probe_samples[{time, metrics, grid_paths}](pie_lookdev_fluid probe.samples) 는 장별 표본), metrics.derived(ratio·difference·relative_step·half_life·e_fold·slope·extent, 시계열은 shots.kind series 장면만, extent 는 단계 grids{접두: 격자 JSON 경로 또는 목록} 또는 probe_samples grid_paths) 를 더하며, 목표 띠는 계획 > region_metrics > kind_metrics(영역 kind volume 은 fog 로 측정), relative_to_control 은 대조 기준 × 비, role constraint 는 점수 밖 제약이다.
실행 명령: python Tools/FluidFX/calib_judge.py --sweep Saved/FluidFX/calib/<run>/sweep.json --lock <잠금 JSON> [--output <Saved 아래 JSON>] [--screen-sweep <screen sweep.json>(confirm 필수)] [--verify-against <앞 confirm judge.json>(verify 선택)]
출력: 옛 스윕은 judge.json(장면별 targets 표·noise_floor·drift·control_check·curves·candidate·live2_sheet·accepted). screen 은 screen.json v2(plan·overlay·정본 2·lock·screen_sweep sha, noise{장면: {지표: between·shot·median_floor·noise}}·score_noise·params[effect·sensitive·reach·cost_blocked]·keep·best·second·insensitive·unreachable_targets·cost_blocked_best·scenes, adopt 거짓)이고 비용 막힌 최선은 Saved/FluidFX/calib/perf-candidates.json 에 덧붙인다. confirm 은 judge.json v2(stage·sha·scenes(candidate_medians)·candidate·adopt{decision·variant·assignments·reasons·evidence}), verify 는 judge.json v2(verify{against·passed·checks}). 종료 코드 0 판정 완료·대조 통과(verify 대조 기준 통과 포함) / 1 스윕 무효·대조 실패·판정 불가·verify 실패 / 2 잠금·입력 오류·장수 불일치·saved 거짓·sha 불일치·screen 재판정 불일치. 판정 상수(잠금 대상, 결과를 연 뒤 불변): SENSITIVITY_FACTOR 2.0·CONTROL_NOISE_FACTOR 2.0·REACH_NOISE_FACTOR 2.0·MIN_CONTROL_STEPS 3·MIN_SAMPLES 3·SECOND_GAIN_MARGIN 1.0.
상태: 오프라인만 검사(합성 탐침 규칙·합성 단계 점수·2026-10-25 수용 시험 산출 재판정, 2026-10-30 CAL-0 B4a 합성 단계 스윕·합성 PNG·격자 fixture), 단계 스윕의 실제 PIE 판정 미실행. 옛 스윕 잡음 바닥은 계획 metrics.noise_floor 가 없으면 비대조 변형 반복 간 최대 범위(반복 2 이상 필요).
"""
import argparse
import difflib
import hashlib
import json
import math
import re
import statistics
import subprocess
import sys
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))
import prereg_lock  # noqa: E402

MEASURE_SCRIPT = ROOT / "Tools" / "FluidFX" / "measure_fx_look.py"
SAVED_DIR = ROOT / "Saved"
PERF_CANDIDATES_PATH = ROOT / "Saved" / "FluidFX" / "calib" / "perf-candidates.json"
CONTROL_ID = "v00"
SCORE_METRIC = "score"
RULE_METRIC = "rule_score"
CONTROL_NOISE_FACTOR = 2.0
SENSITIVITY_FACTOR = 2.0
REACH_NOISE_FACTOR = 2.0
MIN_CONTROL_STEPS = 3
MIN_SAMPLES = 3
SECOND_GAIN_MARGIN = 1.0
KIND_GROUP_PREFIX = "kind_metrics."
TOLERANCE = 1e-12
EXIT_REJECTED = 1
EXIT_INPUT = 2
JUDGED_STAGES = ("screen", "confirm", "verify")
SOURCE_NAMES = ("look", "water_probe", "probe", "offline")
ROLE_SCORE = "score"
ROLE_CONSTRAINT = "constraint"
SERIES_KIND = "series"
MEASURE_KIND_ALIASES = {"volume": "fog"}
PAIR_OPERATIONS = ("ratio", "difference", "relative_step")
SERIES_OPERATIONS = ("half_life", "e_fold", "slope")
EXTENT_OPERATION = "extent"
EXTENT_FIELDS = ("area_fraction", "length", "width", "half_angle_deg")
COST_SIGNS = {"higher_costs_more": 1, "lower_costs_more": -1}
SHA_KEYS = ("plan_sha256", "overlay_sha256", "canonical_tuning_sha256", "canonical_look_sha256", "lock_sha256")
SCREEN_NOISE_KEYS = ("between", "shot", "median_floor", "noise")
REJUDGE_KEYS = ("keep", "best", "second", "noise", "score_noise", "params")
SHOT_PREFIX = re.compile(r"^\d+\.")
SUGGESTION_COUNT = 3
SCREEN_ONLY_REASON = "screen 단계는 채택하지 않는다(confirm 필요, 강한 효과 면제 없음 P-2 철회)"


class JudgeError(Exception):
    pass


def load_json(path):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise JudgeError(f"{path}: 읽지 못했다({error})") from error


def write_json(path, document):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")


def canonical_text(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True)


def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def is_under(path, folder):
    resolved = Path(path).resolve()
    folder = Path(folder).resolve()
    return resolved == folder or folder in resolved.parents


def strip_shot_prefix(name):
    return SHOT_PREFIX.sub("", name, count=1)


def suggest_names(name, known):
    close = difflib.get_close_matches(name, sorted(known), n=SUGGESTION_COUNT, cutoff=0.0)
    return f"(가까운 이름: {', '.join(close)})" if close else ""


def median_or_none(values):
    return statistics.median(values) if values else None


def check_lock(sweep, lock_path):
    is_matching, problems, lock = prereg_lock.verify_lock(lock_path)
    if not is_matching:
        raise JudgeError("사전 등록 잠금: " + "; ".join(problems))
    if lock["files"].get(sweep["plan"]) != sweep["plan_sha256"]:
        raise JudgeError(f"사전 등록 잠금에 이 스윕의 계획 {sweep['plan']} (sha256 {sweep['plan_sha256'][:12]}…) 이 없다")
    return lock


def build_measure_regions(scene, steps):
    template = load_json(prereg_lock.project_path(scene["regions_template"]))
    regions = template.get("regions")
    if not isinstance(regions, list) or not regions:
        raise JudgeError(f"{scene['regions_template']}: regions 목록이 없다")
    images = []
    for step in steps:
        for index, image in enumerate(step["images"]):
            entry = {"path": image, "id": f"{step['name']}_{index}", "label": step["name"], "game_time": step.get("shot_game_time"), "regions": regions}
            if "fire_lit" in template:
                entry["fire_lit"] = template["fire_lit"]
            images.append(entry)
    return {"images": images}


def selected_checks(region, selection):
    kinds = selection.get("kinds") or []
    metrics = selection.get("metrics") or []
    if kinds and region["kind"] not in kinds:
        return []
    return [check for check in region["checks"] if (not metrics or check["metric"] in metrics) and check["pass"] is not None]


def run_measure(regions_document, scene_name, work_dir, targets_path=None):
    regions_path = work_dir / f"{scene_name}_regions.json"
    output_path = work_dir / f"{scene_name}_measure.json"
    work_dir.mkdir(parents=True, exist_ok=True)
    regions_path.write_text(json.dumps(regions_document, ensure_ascii=False, indent=1) + "\n", encoding="utf-8", newline="\n")
    command = [sys.executable, str(MEASURE_SCRIPT), "--regions", str(regions_path), "--output", str(output_path)]
    if targets_path is not None:
        command += ["--targets", str(targets_path)]
    completed = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if completed.returncode != 0:
        raise JudgeError(f"measure_fx_look 종료 {completed.returncode}: {(completed.stderr or completed.stdout)[-600:]}")
    return {image["id"]: image for image in load_json(output_path)["images"]}, output_path


def new_item(step):
    return {"step": step["step"], "variant": step["variant"], "metrics": {}, "samples": {}, RULE_METRIC: 0, "bands": {}, "shot_count": 0}


def add_shot_values(item, shot_values):
    batch = defaultdict(list)
    for index, values in enumerate(shot_values):
        for name, value in values.items():
            item["metrics"][f"{index}.{name}"] = value
            if is_number(value):
                batch[name].append(value)
    for name, values in batch.items():
        item["samples"][name] = values
        item["metrics"][name] = statistics.median(values)
    item["shot_count"] = max(item["shot_count"], len(shot_values))


def measure_fire_scene(scene, steps, work_dir):
    selection = load_json(prereg_lock.project_path(scene["targets"]))
    live2_path = prereg_lock.project_path(selection["live2_targets"])
    measured, output_path = run_measure(build_measure_regions(scene, steps), scene["name"], work_dir, live2_path)
    items = []
    for step in steps:
        item = new_item(step)
        gaps = []
        shot_values = []
        for index in range(len(step["images"])):
            values = {}
            for region in measured[f"{step['name']}_{index}"]["regions"]:
                for check in selected_checks(region, selection):
                    name = f"{region['name']}.{check['metric']}"
                    values[name] = check["value"]
                    gaps.append(check["gap_bands"])
                    item["bands"].setdefault(name, [check["group"], check["pass_low"], check["pass_high"]])
            shot_values.append(values)
        add_shot_values(item, shot_values)
        item[RULE_METRIC] = sum(gaps)
        items.append(item)
    return items, load_json(live2_path), relative(output_path)


def measure_generic_scene(scene, steps, work_dir):
    template = load_json(prereg_lock.project_path(scene["regions_template"]))
    regions = template.get("regions")
    if not isinstance(regions, list) or not regions:
        raise JudgeError(f"{scene['regions_template']}: regions 목록이 없다")
    measured_regions = [dict(region, kind=MEASURE_KIND_ALIASES.get(region.get("kind"), region.get("kind"))) for region in regions]
    images = []
    for step in steps:
        for index, image in enumerate(step["images"]):
            entry = {"path": image, "id": f"{step['name']}_{index}", "regions": measured_regions}
            if index > 0:
                entry["previous"] = step["images"][index - 1]
            images.append(entry)
    measured, output_path = run_measure({"images": images}, scene["name"], work_dir)
    items = []
    for step in steps:
        item = new_item(step)
        shot_values = []
        for index in range(len(step["images"])):
            values = {}
            for region in measured[f"{step['name']}_{index}"]["regions"]:
                flatten_numbers(region["name"], region["metrics"], values)
            shot_values.append(values)
        add_shot_values(item, shot_values)
        items.append(item)
    return items, {region.get("name"): region.get("kind") for region in regions}, relative(output_path)


def flatten_numbers(name, value, metrics):
    if is_number(value):
        metrics[name] = value
    elif isinstance(value, list):
        metrics.update({f"{name}[{index}]": item for index, item in enumerate(value) if is_number(item)})
    elif isinstance(value, dict):
        for key, item in value.items():
            flatten_numbers(f"{name}.{key}", item, metrics)


def water_rule_metrics(rules):
    metrics = {}
    for name, rule in rules.items():
        if isinstance(rule, dict):
            flatten_numbers(name, rule.get("value"), metrics)
    return metrics


def measure_water_scene(steps):
    items = []
    for step in steps:
        probe = step.get("water_probe")
        if not isinstance(probe, dict):
            raise JudgeError(f"{step['name']}: 물 탐침 결과가 없다")
        failed_required = sum(1 for rule in probe["rules"].values() if isinstance(rule, dict) and rule.get("passed") is False and rule.get("required", True))
        items.append({"step": step["step"], "variant": step["variant"], "metrics": water_rule_metrics(probe["rules"]), RULE_METRIC: failed_required, "bands": {}})
    return items


def check_step_shots(step, count):
    images = step.get("images") or []
    if len(images) != count:
        raise JudgeError(f"{step['name']}: 단계 이미지 {len(images)}장 ≠ 잠긴 계획 shots.count {count}(장 빼기로 중앙값을 바꿀 수 없다)")
    expected = step.get("shots_expected")
    if expected is not None and expected != count:
        raise JudgeError(f"{step['name']}: shots_expected {expected} ≠ 잠긴 계획 shots.count {count}")
    saved = step.get("shots_saved")
    if isinstance(saved, list):
        is_saved = len(saved) == count and all(flag is True for flag in saved)
    else:
        is_saved = isinstance(saved, int) and not isinstance(saved, bool) and saved == count
    if not is_saved:
        raise JudgeError(f"{step['name']}: 예약 촬영 saved 가 모두 참이 아니다(shots_saved {saved!r}, 잠긴 shots.count {count})")


def merge_items(items_by_step, extra_items):
    for extra in extra_items:
        item = items_by_step[extra["step"]]
        item["metrics"].update(extra["metrics"])
        item["samples"].update(extra.get("samples", {}))
        item["bands"].update(extra.get("bands", {}))
        item[RULE_METRIC] += extra.get(RULE_METRIC, 0)
        item["shot_count"] = max(item["shot_count"], extra.get("shot_count", 0))


def probe_sample_values(step):
    shots = []
    for sample in step.get("probe_samples") or []:
        metrics = sample.get("metrics") if isinstance(sample, dict) else None
        shots.append({(key if key.startswith("probe.") else f"probe.{key}"): value for key, value in (metrics or {}).items()})
    return shots


def step_grids(step):
    if isinstance(step.get("grids"), dict):
        return step["grids"]
    grids = defaultdict(list)
    for sample in step.get("probe_samples") or []:
        for prefix, grid_path in ((sample.get("grid_paths") if isinstance(sample, dict) else None) or {}).items():
            grids[prefix].append(grid_path)
    return dict(grids)


def merge_step_metrics(item, step):
    metrics = step.get("metrics")
    if not isinstance(metrics, dict):
        raise JudgeError(f"{step['name']}: 단계 metrics(probe·offline 값)가 없다")
    shots = probe_sample_values(step)
    if shots:
        add_shot_values(item, shots)
    for name, value in metrics.items():
        if name in item["samples"] and shots:
            continue
        if isinstance(value, list):
            numbers = [sample for sample in value if is_number(sample)]
            for index, sample in enumerate(value):
                item["metrics"][f"{index}.{name}"] = sample
            if numbers:
                item["samples"][name] = numbers
                item["metrics"][name] = statistics.median(numbers)
            item["shot_count"] = max(item["shot_count"], len(value))
        elif is_number(value):
            item["metrics"][name] = value
            item["samples"][name] = [value]


def load_band_tables(scene):
    if not isinstance(scene.get("targets"), str):
        return {}
    return load_json(prereg_lock.project_path(scene["targets"]))


def make_region_band_lookup(region_kinds, selection):
    region_bands = selection.get("region_metrics") or {}
    kind_bands = selection.get("kind_metrics") or {}

    def lookup(metric):
        region, _, key = strip_shot_prefix(metric).partition(".")
        band = (region_bands.get(region) or {}).get(key)
        if band:
            return band[0], band[1], "region"
        band = (kind_bands.get(region_kinds.get(region)) or {}).get(key)
        if band:
            return band[0], band[1], "kind"
        return None

    return lookup


def scene_sources(scene, plan_scene):
    sources = scene.get("sources") or plan_scene.get("sources") or (["water_probe"] if scene.get("uses_water_probe") else ["look"])
    unknown = [source for source in sources if source not in SOURCE_NAMES]
    if unknown:
        raise JudgeError(f"{scene['name']}: 모르는 sources {unknown}(허용 {', '.join(SOURCE_NAMES)})")
    return list(sources)


def measure_staged_scene(scene, plan_scene, steps, sources, work_dir):
    items = {step["step"]: new_item(step) for step in steps}
    band_lookup, live2, measure_path = None, None, None
    if "look" in sources:
        count = (plan_scene.get("shots") or {}).get("count")
        if not isinstance(count, int) or isinstance(count, bool) or count < 1:
            raise JudgeError(f"{scene['name']}: 잠긴 계획 장면에 shots.count 가 없다(단계 스윕 look 장면)")
        if count < MIN_SAMPLES:
            raise JudgeError(f"{scene['name']}: 단계 판정은 장면 표본 ≥{MIN_SAMPLES} 이 필요하다(잠긴 shots.count {count})")
        for step in steps:
            check_step_shots(step, count)
        selection = load_band_tables(scene)
        if isinstance(selection.get("live2_targets"), str):
            look_items, live2, measure_path = measure_fire_scene(scene, steps, work_dir)
        else:
            look_items, region_kinds, measure_path = measure_generic_scene(scene, steps, work_dir)
            band_lookup = make_region_band_lookup(region_kinds, selection)
        merge_items(items, look_items)
    if "water_probe" in sources:
        merge_items(items, measure_water_scene(steps))
    if "probe" in sources or "offline" in sources:
        for step in steps:
            merge_step_metrics(items[step["step"]], step)
    return [items[step["step"]] for step in steps], band_lookup, live2, measure_path


def pair_value(operation, first, second):
    if not is_number(first) or not is_number(second):
        return None
    if operation == "difference":
        return first - second
    if second == 0:
        return None
    if operation == "ratio":
        return first / second
    return (first - second) / second


def fit_line(points):
    if len(points) < MIN_SAMPLES:
        return None
    mean_x = sum(x for x, _ in points) / len(points)
    mean_y = sum(y for _, y in points) / len(points)
    spread_x = sum((x - mean_x) ** 2 for x, _ in points)
    if spread_x <= 0:
        return None
    slope = sum((x - mean_x) * (y - mean_y) for x, y in points) / spread_x
    intercept = mean_y - slope * mean_x
    total = sum((y - mean_y) ** 2 for _, y in points)
    residual = sum((y - intercept - slope * x) ** 2 for x, y in points)
    return slope, (1.0 - residual / total if total > 0 else 1.0)


def series_value(operation, points):
    if operation == "slope":
        return fit_line(points)
    fit = fit_line([(time, math.log(value)) for time, value in points if value > 0])
    if fit is None or -fit[0] <= 0:
        return None
    rate = -fit[0]
    return (math.log(2.0) / rate if operation == "half_life" else 1.0 / rate), fit[1]


def series_points(item, step, operand):
    times = step.get("shot_game_times") or []
    points = []
    for index, time in enumerate(times):
        value = item["metrics"].get(f"{index}.{operand}")
        if is_number(time) and is_number(value):
            points.append((time, value))
    return points


def extent_values(grid_path, threshold):
    document = load_json(prereg_lock.project_path(grid_path))
    rows = document.get("values")
    if not isinstance(rows, list) or not rows or not all(isinstance(row, list) and row for row in rows):
        raise JudgeError(f"{grid_path}: values 격자(행 목록)가 없다")
    limit = threshold if is_number(threshold) else document.get("threshold", 0.0)
    cells = [(x, y) for y, row in enumerate(rows) for x, value in enumerate(row) if is_number(value) and value > limit]
    result = {"area_fraction": len(cells) / sum(len(row) for row in rows), "length": None, "width": None, "half_angle_deg": None}
    center = document.get("radial_center")
    if not cells or not isinstance(center, list) or len(center) != 2:
        return result
    apex_x, apex_y = center[0] * (len(rows[0]) - 1), center[1] * (len(rows) - 1)
    vectors = [(x - apex_x, y - apex_y) for x, y in cells]
    axis_x = sum(dx for dx, _ in vectors) / len(vectors)
    axis_y = sum(dy for _, dy in vectors) / len(vectors)
    norm = math.hypot(axis_x, axis_y)
    if norm <= TOLERANCE:
        return result
    axis_x, axis_y = axis_x / norm, axis_y / norm
    along = [dx * axis_x + dy * axis_y for dx, dy in vectors]
    across = [dy * axis_x - dx * axis_y for dx, dy in vectors]
    length, width = max(along), max(across) - min(across)
    if length <= 0:
        return result
    cell_size = float(document.get("cell_size", 1.0))
    result.update(length=length * cell_size, width=width * cell_size, half_angle_deg=math.degrees(math.atan2(width / 2.0, length)))
    return result


def add_extent_metrics(item, step, spec, name):
    grids = step_grids(step).get(spec.get("grid"))
    paths = grids if isinstance(grids, list) else [grids] if isinstance(grids, str) else []
    shots = [extent_values(path, spec.get("threshold")) for path in paths]
    for field in EXTENT_FIELDS:
        values = [shot[field] for shot in shots if is_number(shot[field])]
        item["metrics"][f"{name}.{field}"] = median_or_none(values)
        if len(paths) > 1:
            for index, shot in enumerate(shots):
                item["metrics"][f"{index}.{name}.{field}"] = shot[field]
            item["samples"][f"{name}.{field}"] = values


def add_pair_metrics(item, spec, operation, name):
    item["metrics"][name] = pair_value(operation, item["metrics"].get(spec["a"]), item["metrics"].get(spec["b"]))
    samples = []
    for index in range(item["shot_count"]):
        value = pair_value(operation, item["metrics"].get(f"{index}.{spec['a']}"), item["metrics"].get(f"{index}.{spec['b']}"))
        if value is not None:
            item["metrics"][f"{index}.{name}"] = value
            samples.append(value)
    if samples:
        item["samples"][name] = samples


def add_series_metrics(item, step, spec, operation, name):
    fitted = series_value(operation, series_points(item, step, spec["a"]))
    item["metrics"][name] = fitted[0] if fitted else None
    item["metrics"][f"{name}.r2"] = fitted[1] if fitted else None


def is_derivable(spec, operation, known, steps):
    if operation in PAIR_OPERATIONS:
        return spec.get("a") in known and spec.get("b") in known
    if operation in SERIES_OPERATIONS:
        return spec.get("a") in known
    return any(spec.get("grid") in step_grids(step) for step in steps)


def add_derived_metrics(items, steps, specs, scene_name, is_series):
    steps_by_number = {step["step"]: step for step in steps}
    known = set().union(*[set(item["metrics"]) for item in items]) if items else set()
    for spec in specs:
        operation = spec.get("op")
        if operation not in PAIR_OPERATIONS + SERIES_OPERATIONS + (EXTENT_OPERATION,):
            raise JudgeError(f"파생식 {spec.get('name')}: 모르는 op {operation!r}")
        if spec.get("scene", scene_name) != scene_name or (operation in SERIES_OPERATIONS and not is_series):
            continue
        if not is_derivable(spec, operation, known, steps):
            continue
        name = f"derived.{spec['name']}"
        for item in items:
            step = steps_by_number[item["step"]]
            if operation in PAIR_OPERATIONS:
                add_pair_metrics(item, spec, operation, name)
            elif operation in SERIES_OPERATIONS:
                add_series_metrics(item, step, spec, operation, name)
            else:
                add_extent_metrics(item, step, spec, name)


def resolve_band(target, live2_bands, band_lookup):
    if target.get("low") is not None and target.get("high") is not None:
        return target["low"], target["high"], "plan"
    band = live2_bands.get(strip_shot_prefix(target["metric"]))
    if band is not None:
        return band[1], band[2], "live2"
    found = band_lookup(target["metric"]) if band_lookup else None
    return found if found else (None, None, None)


def resolve_relative_target(scene_name, entry, relative_band):
    low, high = (relative_band.get("low"), relative_band.get("high")) if isinstance(relative_band, dict) else (None, None)
    if not (is_number(low) and is_number(high) and high > low):
        raise JudgeError(f"{scene_name}: 목표 지표 {entry['metric']} 의 relative_to_control 은 {{low, high}} 숫자이고 high > low 여야 한다")
    if entry["role"] == ROLE_SCORE and low <= 1.0 <= high:
        raise JudgeError(f"{scene_name}: 목표 지표 {entry['metric']} 의 relative_to_control 띠가 1.0 을 포함한다(대조 중심 띠는 점수로 쓰면 후보가 늘 v00, role constraint 필요)")
    return dict(entry, relative_to_control={"low": low, "high": high}, band_source="relative", low=None, high=None, center=None, half_width=None)


def resolve_targets(sweep, scene_name, items, band_lookup=None):
    planned = sweep["metrics"].get("targets")
    if not isinstance(planned, list) or not planned:
        raise JudgeError("스윕의 계획에 metrics.targets(목표 지표 목록)가 없다")
    bands = {}
    known = set()
    for item in items:
        bands.update(item.get("bands", {}))
        known.update(item["metrics"])
    resolved = []
    for target in planned:
        if target.get("scene", scene_name) != scene_name:
            continue
        metric = target["metric"]
        if metric not in known:
            raise JudgeError(f"{scene_name}: 목표 지표 {metric} 이 측정값에 없다{suggest_names(metric, known)}")
        role = target.get("role", ROLE_SCORE)
        if role not in (ROLE_SCORE, ROLE_CONSTRAINT):
            raise JudgeError(f"{scene_name}: 목표 지표 {metric} 의 role {role!r} 은 score|constraint 만")
        entry = {"metric": metric, "role": role, "weight": float(target.get("weight", 1.0)), "tolerance": float(target.get("tolerance", 0.0))}
        if target.get("relative_to_control") is not None:
            resolved.append(resolve_relative_target(scene_name, entry, target["relative_to_control"]))
            continue
        low, high, source = resolve_band(target, bands, band_lookup)
        if low is None or high is None:
            raise JudgeError(f"{scene_name}: 목표 지표 {metric} 의 목표 범위(low·high)가 계획에도 LIVE-2 목표에도 없다")
        if not high > low:
            raise JudgeError(f"{scene_name}: 목표 지표 {metric} 의 low {low} 가 high {high} 보다 작지 않다")
        resolved.append(dict(entry, low=low, high=high, band_source=source, center=(low + high) / 2, half_width=(high - low) / 2))
    if not resolved:
        raise JudgeError(f"{scene_name}: 이 장면에 걸리는 목표 지표가 없다")
    return resolved


def apply_relative_bands(targets, control_reference):
    for target in targets:
        if target.get("band_source") != "relative":
            continue
        reference = control_reference.get(target["metric"])
        if reference is None:
            raise JudgeError(f"목표 지표 {target['metric']} 는 relative_to_control 인데 대조 중앙값이 없다")
        low, high = sorted((reference * target["relative_to_control"]["low"], reference * target["relative_to_control"]["high"]))
        if not high > low:
            raise JudgeError(f"목표 지표 {target['metric']} 의 대조 중앙값 {reference} 로는 relative_to_control 띠가 생기지 않는다")
        target.update(low=low, high=high, center=(low + high) / 2, half_width=(high - low) / 2)


def normalized_distance(value, target):
    return abs(value - target["center"]) / target["half_width"]


def score_item(item, targets):
    total = 0.0
    for target in targets:
        if target["role"] != ROLE_SCORE:
            continue
        value = item["metrics"].get(target["metric"])
        if not is_number(value):
            return None
        total += target["weight"] * normalized_distance(value, target)
    return total


def spread(values):
    return max(values) - min(values) if values else None


def metric_values(items, name):
    if name in (SCORE_METRIC, RULE_METRIC):
        return [item[name] for item in items if is_number(item.get(name))]
    return [item["metrics"][name] for item in items if is_number(item["metrics"].get(name))]


def score_noise_floor(targets, noise_floor):
    total = 0.0
    for target in targets:
        if target["role"] != ROLE_SCORE:
            continue
        floor = noise_floor.get(target["metric"])
        if floor is None:
            return None
        total += target["weight"] * floor / target["half_width"]
    return total


def check_controls(targets, drift, noise_floor):
    control_check = {}
    control_allowed = {}
    for target in targets:
        name = target["metric"]
        floor = noise_floor.get(name)
        if drift.get(name) is None or (floor is None and target["tolerance"] <= 0):
            control_check[name] = "undetermined"
            control_allowed[name] = None
            continue
        control_allowed[name] = max(CONTROL_NOISE_FACTOR * (floor or 0.0), target["tolerance"])
        control_check[name] = "pass" if drift[name] <= control_allowed[name] + TOLERANCE else "fail"
    return control_check, control_allowed


def aggregate(items, sweep, targets):
    for item in items:
        item[SCORE_METRIC] = score_item(item, targets)
    by_variant = defaultdict(list)
    for item in sorted(items, key=lambda entry: entry["step"]):
        by_variant[item["variant"]].append(item)
    metric_names = sorted(set().union(*[set(item["metrics"]) for item in items])) if items else []
    names = metric_names + [SCORE_METRIC, RULE_METRIC]
    planned = sweep["metrics"].get("noise_floor", {})
    noise_floor = {}
    for name in names:
        if name == SCORE_METRIC:
            continue
        if name in planned:
            noise_floor[name] = planned[name]
            continue
        spreads = [spread(metric_values(group, name)) for variant, group in by_variant.items() if variant != CONTROL_ID and len(metric_values(group, name)) >= 2]
        noise_floor[name] = max(spreads) if spreads else None
    noise_floor[SCORE_METRIC] = score_noise_floor(targets, noise_floor)
    controls = by_variant.get(CONTROL_ID, [])
    drift = {}
    for name in names:
        values = metric_values(controls, name)
        drift[name] = max(abs(value - values[0]) for value in values) if len(values) >= 2 else None
    control_check, control_allowed = check_controls(targets, drift, noise_floor)
    target_names = {target["metric"] for target in targets}
    diagnostic_over = [name for name in metric_names if name not in target_names and drift[name] is not None and noise_floor[name] is not None and drift[name] > CONTROL_NOISE_FACTOR * noise_floor[name] + TOLERANCE]
    medians = {variant: {name: statistics.median(metric_values(group, name)) for name in names if metric_values(group, name)} for variant, group in by_variant.items()}
    return {"noise_floor": noise_floor, "drift": drift, "control_check": control_check, "control_allowed": control_allowed, "control_ok": bool(control_check) and all(status == "pass" for status in control_check.values()),
            "diagnostic_drift_over_allowed": diagnostic_over, "medians": medians, "repeat_counts": {variant: len(group) for variant, group in by_variant.items()}}


def variant_values(sweep):
    return {variant["id"]: [item["value"] for item in variant["expect"]] for variant in sweep["variants"]}


def build_curves(sweep, medians):
    values_by_variant = variant_values(sweep)
    base_values = values_by_variant[CONTROL_ID]
    curves = []
    for index, param in enumerate(sweep["params"]):
        points = []
        for variant_id, values in values_by_variant.items():
            others_at_base = all(value == base for position, (value, base) in enumerate(zip(values, base_values)) if position != index)
            if others_at_base and variant_id in medians:
                points.append({"value": values[index], "variant": variant_id, SCORE_METRIC: medians[variant_id].get(SCORE_METRIC), RULE_METRIC: medians[variant_id].get(RULE_METRIC),
                               "metrics": {name: value for name, value in medians[variant_id].items() if name not in (SCORE_METRIC, RULE_METRIC)}})
        curves.append({"json_path": param["json_path"], "base_value": param["base_value"], "points": sorted(points, key=lambda point: json.dumps(point["value"]))})
    return curves


def build_target_table(targets, aggregated):
    table = []
    for target in targets:
        name = target["metric"]
        distances = {variant: normalized_distance(values[name], target) for variant, values in aggregated["medians"].items() if name in values}
        table.append({**target, "noise_floor": aggregated["noise_floor"].get(name), "drift": aggregated["drift"].get(name), "allowed": aggregated["control_allowed"].get(name),
                      "control_check": aggregated["control_check"].get(name), "distance_by_variant": distances})
    return table


def choose_candidate(sweep, aggregated):
    medians = aggregated["medians"]
    scored = {variant: values[SCORE_METRIC] for variant, values in medians.items() if SCORE_METRIC in values}
    if CONTROL_ID not in scored:
        return {"variant": None, "reason": "대조 점수가 없다"}
    best = min(scored, key=lambda variant: (scored[variant], variant != CONTROL_ID, variant))
    improvement = scored[CONTROL_ID] - scored[best]
    floor = aggregated["noise_floor"].get(SCORE_METRIC)
    is_significant = best != CONTROL_ID and floor is not None and improvement > floor + TOLERANCE
    return {"variant": best, "values": variant_values(sweep)[best], SCORE_METRIC: scored[best], "control_score": scored[CONTROL_ID], "improvement": improvement,
            "score_noise_floor": floor, "is_significant": is_significant, "scores": scored,
            RULE_METRIC: medians[best].get(RULE_METRIC), "control_rule_score": medians[CONTROL_ID].get(RULE_METRIC),
            "assignments": next(variant["assignments"] for variant in sweep["variants"] if variant["id"] == best)}


def live2_band(live2, group, metric):
    if group.startswith(KIND_GROUP_PREFIX):
        entry = live2.get("kind_metrics", {}).get(group[len(KIND_GROUP_PREFIX):], {}).get(metric) or {}
    else:
        entry = live2.get(group, {}).get("metrics", {}).get(metric) or {}
    return entry.get("live2_median"), entry.get("label")


def build_live2_sheet(items, live2, medians, candidate_variant):
    bands = {}
    for item in items:
        bands.update(item.get("bands", {}))
    sheet = []
    for key, (group, pass_low, pass_high) in sorted(bands.items()):
        region_name, metric = key.split(".", 1)
        median, label = live2_band(live2, group, metric)
        sheet.append({"region": region_name, "metric": metric, "label": label, "live2_median": median, "pass_low": pass_low, "pass_high": pass_high,
                      "control": medians.get(CONTROL_ID, {}).get(key), "candidate": medians.get(candidate_variant, {}).get(key) if candidate_variant else None})
    return sheet


def relative(path):
    return prereg_lock.relative_text(path)


def judge_scene(scene, sweep, work_dir):
    steps = [step for step in scene["steps"] if step["valid"]]
    if len(steps) != len(scene["steps"]) or not steps:
        raise JudgeError(f"{scene['name']}: 무효 단계가 있거나 단계가 없다")
    if scene.get("uses_water_probe"):
        items, live2, measure_path, source = measure_water_scene(steps), None, None, "water_probe"
    else:
        (items, live2, measure_path), source = measure_fire_scene(scene, steps, work_dir), "look"
    targets = resolve_targets(sweep, scene["name"], items)
    controls = [item for item in items if item["variant"] == CONTROL_ID]
    apply_relative_bands(targets, {target["metric"]: median_or_none(metric_values(controls, target["metric"])) for target in targets})
    aggregated = aggregate(items, sweep, targets)
    candidate = choose_candidate(sweep, aggregated)
    result = {"name": scene["name"], "source": source, "measure": measure_path, "targets": build_target_table(targets, aggregated), "noise_floor": aggregated["noise_floor"], "drift": aggregated["drift"],
              "control_allowed": aggregated["control_allowed"], "control_check": aggregated["control_check"], "control_ok": aggregated["control_ok"],
              "diagnostic_drift_over_allowed": aggregated["diagnostic_drift_over_allowed"], "repeat_counts": aggregated["repeat_counts"],
              "curves": build_curves(sweep, aggregated["medians"]), "candidate": candidate}
    if live2 is not None:
        result["live2_sheet"] = build_live2_sheet(items, live2, aggregated["medians"], candidate.get("variant"))
    return result


def compute_metric_noise(controls, name, is_series, is_offline_only):
    control_values = metric_values(controls, name)
    stats = {"between": spread(control_values) if len(control_values) >= MIN_CONTROL_STEPS else None,
             "drift": max(abs(value - control_values[0]) for value in control_values) if len(control_values) >= 2 else None,
             "reference": median_or_none(control_values), "shot": None, "median_floor": None}
    if is_offline_only:
        stats.update(shot=0.0, median_floor=0.0)
        return stats
    sampled = [item["samples"][name] for item in controls if len(item["samples"].get(name, [])) >= MIN_SAMPLES]
    if is_series or not sampled:
        return stats
    shot = statistics.median([spread(values) for values in sampled])
    stats.update(shot=shot, median_floor=shot / math.sqrt(statistics.median([len(values) for values in sampled])))
    return stats


def screen_noise_value(stats, is_series):
    if stats["between"] is None:
        return None
    if is_series:
        return stats["between"]
    if stats["median_floor"] is None:
        return None
    return max(stats["between"], stats["median_floor"])


def drift_allowance(median_floor, tolerance, is_series):
    if is_series or median_floor is None:
        return tolerance if tolerance > 0 else None
    return max(CONTROL_NOISE_FACTOR * median_floor, tolerance)


def is_within_band(value, target):
    return is_number(value) and target["low"] - TOLERANCE <= value <= target["high"] + TOLERANCE


def summarize_variant(group, targets, control_score, score_noise, is_scene_ok):
    medians = {target["metric"]: median_or_none(metric_values(group, target["metric"])) for target in targets}
    score = median_or_none(metric_values(group, SCORE_METRIC))
    constraint_failures = [target["metric"] for target in targets if target["role"] == ROLE_CONSTRAINT and not is_within_band(medians[target["metric"]], target)]
    is_comparable = is_scene_ok and score is not None
    improvement = control_score - score if is_comparable else None
    return {"steps": [item["step"] for item in group], "score": score, "medians": medians, "constraint_failures": constraint_failures, "improvement": improvement,
            "gain": improvement / max(score_noise, TOLERANCE) if is_comparable else None,
            "eligible": is_comparable and not constraint_failures and score <= control_score + score_noise + TOLERANCE,
            "significant": is_comparable and improvement > score_noise + TOLERANCE}


def judge_staged_scene(scene_name, items, targets, is_series, is_offline_only, screen_table=None):
    ordered = sorted(items, key=lambda entry: entry["step"])
    controls = [item for item in ordered if item["variant"] == CONTROL_ID]
    names = sorted(set().union(*[set(item["metrics"]) for item in items])) if items else []
    stats = {name: compute_metric_noise(controls, name, is_series, is_offline_only) for name in names}
    reference = {name: entry["reference"] for name, entry in stats.items()}
    apply_relative_bands(targets, reference)
    reasons = [] if len(controls) >= MIN_CONTROL_STEPS else [f"대조 단계 {len(controls)}개 < {MIN_CONTROL_STEPS}"]
    noise, control_check, control_allowed, table = {}, {}, {}, []
    for target in targets:
        metric = target["metric"]
        entry = stats[metric]
        if screen_table is None:
            used, drift_floor = screen_noise_value(entry, is_series), entry["median_floor"]
        else:
            screen_entry = screen_table.get(metric) or {}
            screen_value = screen_entry.get("noise")
            used = max(screen_value, entry["between"]) if screen_value is not None and entry["between"] is not None else None
            drift_floor = screen_entry.get("median_floor")
        allowed = drift_allowance(drift_floor, target["tolerance"], is_series)
        noise[metric] = {"between": entry["between"], "shot": entry["shot"], "median_floor": entry["median_floor"], "noise": used, "drift_floor": drift_floor}
        control_allowed[metric] = allowed
        control_check[metric] = "undetermined" if allowed is None or entry["drift"] is None else "pass" if entry["drift"] <= allowed + TOLERANCE else "fail"
        if used is None:
            reasons.append(f"{metric}: 잡음 없음(장 표본 < {MIN_SAMPLES} 또는 대조 단계 < {MIN_CONTROL_STEPS})")
        if entry["reference"] is None:
            reasons.append(f"{metric}: 대조 값 없음")
        table.append(dict(target, noise=used, drift=entry["drift"], allowed=allowed, control_check=control_check[metric]))
    for item in items:
        item[SCORE_METRIC] = score_item(item, targets)
    score_noise = score_noise_floor(targets, {metric: entry["noise"] for metric, entry in noise.items()})
    control_score = median_or_none(metric_values(controls, SCORE_METRIC))
    if control_score is None:
        reasons.append("대조 점수 없음")
    is_determinable = not reasons
    control_ok = is_determinable and all(status == "pass" for status in control_check.values())
    by_variant = defaultdict(list)
    for item in ordered:
        by_variant[item["variant"]].append(item)
    variants = {variant: summarize_variant(group, targets, control_score, score_noise, control_ok) for variant, group in by_variant.items() if variant != CONTROL_ID}
    target_names = {target["metric"] for target in targets}
    diagnostic = [name for name in names if name not in target_names and not SHOT_PREFIX.match(name) and stats[name]["drift"] is not None
                  and screen_noise_value(stats[name], is_series) is not None and stats[name]["drift"] > CONTROL_NOISE_FACTOR * screen_noise_value(stats[name], is_series) + TOLERANCE]
    return {"name": scene_name, "series": is_series, "offline_only": is_offline_only, "determinable": is_determinable, "undetermined_reasons": reasons,
            "control_ok": control_ok, "control_check": control_check, "control_allowed": control_allowed, "control_score": control_score, "score_noise": score_noise,
            "control_reference": {metric: reference[metric] for metric in target_names}, "noise": noise, "targets": table, "variants": variants,
            "control_steps": [item["step"] for item in controls], "diagnostic_drift_over_allowed": diagnostic}


def judge_sweep_scenes(sweep, plan, work_dir, screen_noise=None):
    plan_scenes = {scene["name"]: scene for scene in plan.get("scenes", [])}
    unscoped = [str(target.get("metric")) for target in sweep["metrics"].get("targets") or [] if "scene" not in target]
    if len(plan_scenes) >= 2 and unscoped:
        raise JudgeError(f"장면이 2개 이상인 계획은 모든 target 에 scene 이 있어야 한다(없음: {', '.join(unscoped)})")
    results = []
    for scene in sweep["scenes"]:
        plan_scene = plan_scenes.get(scene["name"])
        if plan_scene is None:
            raise JudgeError(f"{scene['name']}: 잠긴 계획에 이 장면이 없다")
        steps = [step for step in scene["steps"] if step["valid"]]
        if len(steps) != len(scene["steps"]) or not steps:
            raise JudgeError(f"{scene['name']}: 무효 단계가 있거나 단계가 없다")
        sources = scene_sources(scene, plan_scene)
        is_series = (plan_scene.get("shots") or {}).get("kind") == SERIES_KIND
        items, band_lookup, live2, measure_path = measure_staged_scene(scene, plan_scene, steps, sources, work_dir)
        add_derived_metrics(items, steps, sweep["metrics"].get("derived") or [], scene["name"], is_series)
        targets = resolve_targets(sweep, scene["name"], items, band_lookup)
        screen_table = None if screen_noise is None else screen_noise.get(scene["name"], {})
        result = judge_staged_scene(scene["name"], items, targets, is_series, sources == ["offline"], screen_table)
        result.update(sources=sources, measure=measure_path)
        if live2 is not None:
            medians = {variant: {name: median_or_none(metric_values(group, name)) for name in group[0]["bands"]} for variant, group in group_by_variant(items).items()}
            result["live2_sheet"] = build_live2_sheet(items, live2, medians, None)
        results.append(result)
    return results


def group_by_variant(items):
    groups = defaultdict(list)
    for item in sorted(items, key=lambda entry: entry["step"]):
        groups[item["variant"]].append(item)
    return groups


def param_keys(param):
    if param.get("json_paths"):
        return list(param["json_paths"])
    if param.get("json_path"):
        return [param["json_path"]]
    return [canonical_text(param.get("actor_prop"))]


def is_actor_prop_param(param):
    return bool(param.get("actor_prop")) and not param.get("json_path") and not param.get("json_paths")


def param_label(param):
    return "+".join(param_keys(param))


def assignment_key(assignment):
    return assignment["json_path"] if "json_path" in assignment else canonical_text(assignment.get("actor_prop"))


def variant_param_values(sweep):
    owners = {key: index for index, param in enumerate(sweep["params"]) for key in param_keys(param)}
    table = {}
    for variant in sweep["variants"]:
        if isinstance(variant.get("knobs"), list) and isinstance(variant.get("values"), list):
            table[variant["id"]] = {index: variant["values"][index] for index in variant["knobs"]}
            continue
        touched = {}
        for assignment in variant.get("assignments", []):
            index = owners.get(assignment_key(assignment))
            if index is not None:
                touched[index] = assignment.get("value")
        table[variant["id"]] = touched
    return table


def variant_signature(touched):
    return canonical_text(sorted((index, canonical_text(value)) for index, value in touched.items()))


def plan_param_assignments(sweep, variant_id):
    keys = {key for param in sweep["params"] for key in param_keys(param)}
    variant = next(entry for entry in sweep["variants"] if entry["id"] == variant_id)
    chosen = [assignment for assignment in variant.get("assignments", []) if assignment_key(assignment) in keys]
    for index, value in variant_param_values(sweep)[variant_id].items():
        param = sweep["params"][index]
        if is_actor_prop_param(param) and not any("actor_prop" in assignment for assignment in chosen):
            chosen.append({"actor_prop": param["actor_prop"], "value": value})
    return chosen


def cost_direction(cost, value, base_value):
    sign = COST_SIGNS.get(cost)
    if sign is None or not isinstance(value, (int, float)) or not isinstance(base_value, (int, float)):
        return 0
    return (sign * (value - base_value) > 0) - (sign * (value - base_value) < 0)


def param_cost(param, plan, index):
    plan_params = plan.get("params") or []
    return param.get("cost", plan_params[index].get("cost") if index < len(plan_params) else None)


def param_scenes(param, plan, index, scene_results):
    plan_params = plan.get("params") or []
    chosen = param.get("scenes") or (plan_params[index].get("scenes") if index < len(plan_params) else None)
    return chosen or [scene["name"] for scene in scene_results]


def reach_status(target, ends, reference, noise):
    if not ends or reference is None or noise is None or target["low"] is None:
        return "unknown"
    margin = REACH_NOISE_FACTOR * noise
    lowest, highest = min(ends + [reference]) - margin, max(ends + [reference]) + margin
    return "inside" if lowest <= target["high"] and highest >= target["low"] else "outside"


def merge_reach(reach_by_scene):
    merged = {}
    order = {"unknown": 0, "outside": 1, "inside": 2}
    for statuses in reach_by_scene.values():
        for metric, status in statuses.items():
            if order[status] >= order.get(merged.get(metric), -1):
                merged[metric] = status
    return merged


def describe_screen_param(index, param, endpoints, plan, scene_results):
    cost = param_cost(param, plan, index)
    chosen_scenes = param_scenes(param, plan, index, scene_results)
    effect, reach_by_scene, is_sensitive = {}, {}, False
    for scene in scene_results:
        ran = [variant for variant, _ in endpoints if variant in scene["variants"]]
        if scene["name"] not in chosen_scenes or not ran:
            continue
        effect[scene["name"]], reach_by_scene[scene["name"]] = {}, {}
        for target in scene["targets"]:
            metric = target["metric"]
            reference = scene["control_reference"].get(metric)
            noise = scene["noise"][metric]["noise"]
            ends = [scene["variants"][variant]["medians"][metric] for variant in ran if scene["variants"][variant]["medians"].get(metric) is not None]
            size = max(abs(end - reference) for end in ends) if ends and reference is not None else None
            effect[scene["name"]][metric] = size
            reach_by_scene[scene["name"]][metric] = reach_status(target, ends, reference, noise)
            if target["role"] == ROLE_SCORE and size is not None and noise is not None and size > SENSITIVITY_FACTOR * noise + TOLERANCE:
                is_sensitive = True
    blocked = [variant for variant, value in endpoints if cost_direction(cost, value, param.get("base_value")) > 0]
    return {"json_path": param_label(param), "scenes": sorted(effect), "cost": cost, "effect": effect, "sensitive": is_sensitive, "reach": merge_reach(reach_by_scene),
            "reach_by_scene": reach_by_scene, "cost_blocked": blocked}


def plan_variant_stats(scene_results, variant):
    entries = [scene["variants"][variant] for scene in scene_results if variant in scene["variants"]]
    if not entries:
        return {"eligible": False, "significant": False, "gain": None, "constraint_failures": []}
    return {"eligible": all(entry["eligible"] for entry in entries), "significant": any(entry["significant"] for entry in entries),
            "gain": sum(entry["gain"] for entry in entries) if all(entry["gain"] is not None for entry in entries) else None,
            "constraint_failures": sorted({metric for entry in entries for metric in entry["constraint_failures"]})}


def rank_plan_candidates(scene_results, variants, cost_lowering):
    ranked = []
    for variant in variants:
        stats = plan_variant_stats(scene_results, variant)
        if stats["eligible"] and stats["significant"] and stats["gain"] is not None:
            ranked.append({"variant": variant, "gain": stats["gain"]})
    return sorted(ranked, key=lambda entry: (-entry["gain"], 0 if entry["variant"] in cost_lowering else 1, entry["variant"]))


def find_unreachable(params, scene_results):
    unreachable = []
    for scene in scene_results:
        for target in scene["targets"]:
            statuses = [param["reach_by_scene"].get(scene["name"], {}).get(target["metric"]) for param in params]
            if "inside" not in statuses:
                unreachable.append({"scene": scene["name"], "metric": target["metric"], "status": "outside" if "outside" in statuses else "unknown"})
    return unreachable


def record_perf_candidate(entry):
    document = load_json(PERF_CANDIDATES_PATH) if PERF_CANDIDATES_PATH.is_file() else {"version": 1, "items": []}
    key = (entry["plan_sha256"], entry["screen_sweep_sha256"], entry["variant"])
    if any((item.get("plan_sha256"), item.get("screen_sweep_sha256"), item.get("variant")) == key for item in document["items"]):
        return
    document["items"].append(entry)
    write_json(PERF_CANDIDATES_PATH, document)


def build_screen_document(sweep, sweep_path, plan, lock_sha, scene_results, should_record_perf):
    values = variant_param_values(sweep)
    endpoints = {index: [(variant, touched[index]) for variant, touched in values.items() if variant != CONTROL_ID and set(touched) == {index}] for index in range(len(sweep["params"]))}
    params = [describe_screen_param(index, param, endpoints[index], plan, scene_results) for index, param in enumerate(sweep["params"])]
    kept = [index for index, param in enumerate(params) if param["sensitive"]]
    blocked = {variant for param in params for variant in param["cost_blocked"]}
    lowering = {variant for index, param in enumerate(params) for variant, value in endpoints[index] if cost_direction(param["cost"], value, sweep["params"][index].get("base_value")) < 0}
    ranked = rank_plan_candidates(scene_results, [variant for index in kept for variant, _ in endpoints[index]], lowering)
    open_ranked = [entry for entry in ranked if entry["variant"] not in blocked]
    best = open_ranked[0] if open_ranked else None
    second = open_ranked[1] if len(open_ranked) > 1 and best["gain"] - open_ranked[1]["gain"] < SECOND_GAIN_MARGIN else None
    blocked_best = ranked[0] if ranked and ranked[0]["variant"] in blocked else None
    screen_sweep_sha = prereg_lock.file_sha256(sweep_path)
    if blocked_best and should_record_perf:
        record_perf_candidate({"plan": sweep["plan"], "plan_sha256": sweep["plan_sha256"], "screen_sweep": relative(sweep_path), "screen_sweep_sha256": screen_sweep_sha,
                               "variant": blocked_best["variant"], "gain": blocked_best["gain"], "assignments": plan_param_assignments(sweep, blocked_best["variant"]),
                               "recorded_at": datetime.now(timezone.utc).isoformat(timespec="seconds")})
    return {"tool": "calib_judge", "version": 2, "stage": "screen", "plan": sweep["plan"], "plan_sha256": sweep["plan_sha256"], "overlay_sha256": sweep.get("overlay_sha256"),
            "canonical_tuning_sha256": sweep.get("canonical_tuning_sha256"), "canonical_look_sha256": sweep.get("canonical_look_sha256"), "lock_sha256": lock_sha,
            "screen_sweep": relative(sweep_path), "screen_sweep_sha256": screen_sweep_sha,
            "noise": {scene["name"]: {metric: {key: entry[key] for key in SCREEN_NOISE_KEYS} for metric, entry in scene["noise"].items()} for scene in scene_results},
            "score_noise": {scene["name"]: scene["score_noise"] for scene in scene_results},
            "params": [{key: value for key, value in param.items() if key != "reach_by_scene"} for param in params], "keep": [params[index]["json_path"] for index in kept],
            "best": best, "second": second, "insensitive": not kept, "unreachable_targets": find_unreachable(params, scene_results), "cost_blocked_best": blocked_best,
            "scenes": scene_results, "adopt": {"decision": False, "reasons": [SCREEN_ONLY_REASON]}}


def step_fingerprints(sweep):
    fingerprints = set()
    for scene in sweep["scenes"]:
        for step in scene["steps"]:
            recorded = step.get("image_sha256")
            if isinstance(recorded, list) and recorded:
                fingerprints.update(recorded)
            elif step.get("images"):
                fingerprints.update(prereg_lock.file_sha256(prereg_lock.project_path(image)) for image in step["images"])
            else:
                fingerprints.add("metrics:" + hashlib.sha256(canonical_text(step.get("metrics")).encode("utf-8")).hexdigest())
    return fingerprints


def build_sha_table(screen_sweep, confirm_sweep, screen_result, lock_sha):
    table = {}
    for key in SHA_KEYS:
        row = {"screen_sweep": screen_sweep.get(key), "confirm_sweep": confirm_sweep.get(key), "screen_result": screen_result.get(key)}
        if key == "lock_sha256":
            row["given_lock"] = lock_sha
        row["match"] = len({canonical_text(value) for value in row.values()}) == 1
        table[key] = row
    return table


def is_offline_only_sweep(sweep):
    return all(scene.get("sources") == ["offline"] for scene in sweep["scenes"])


def has_determinism_evidence(sweep):
    records = [step.get("offline") or {} for scene in sweep["scenes"] for step in scene["steps"]]
    return bool(records) and all(record.get("output_sha256") is not None and record.get("output_sha256") == record.get("repeat_sha256") for record in records)


def choose_confirm_candidate(screen_result, screen_sweep, sweep, scene_results):
    screen_signatures = {variant: variant_signature(touched) for variant, touched in variant_param_values(screen_sweep).items()}
    confirm_by_signature = {variant_signature(touched): variant for variant, touched in variant_param_values(sweep).items() if variant != CONTROL_ID}
    options, reasons = [], []
    for label in ("best", "second"):
        entry = screen_result.get(label)
        if not entry:
            continue
        confirm_variant = confirm_by_signature.get(screen_signatures.get(entry["variant"]))
        if confirm_variant is None or not any(confirm_variant in scene["variants"] for scene in scene_results):
            if label == "best":
                reasons.append(f"confirm 스윕에 screen best {entry['variant']} 와 같은 변형이 없다(후보 다름)")
            continue
        stats = plan_variant_stats(scene_results, confirm_variant)
        options.append(dict(stats, label=label, screen_variant=entry["variant"], variant=confirm_variant, screen_gain=entry["gain"]))
    qualified = [option for option in options if option["eligible"] and option["significant"]]
    if qualified:
        return max(qualified, key=lambda option: (option["screen_gain"] + option["gain"], option["label"] == "best")), reasons
    return (options[0] if options else None), reasons


def adopt_reasons(candidate, screen_result, screen_sweep, sweep, scene_results, overlap, is_offline_only):
    reasons = []
    if screen_sweep.get("run_dir") == sweep.get("run_dir"):
        reasons.append(f"screen·confirm 이 같은 실행이다(run_dir {sweep.get('run_dir')})")
    if is_offline_only and not (has_determinism_evidence(screen_sweep) and has_determinism_evidence(sweep)):
        reasons.append("offline 단독인데 두 실행의 결정론 증거(단계 offline output_sha256 = repeat_sha256)가 없다")
    if not is_offline_only and overlap:
        reasons.append(f"screen·confirm 이미지·단계 기록 sha 교집합 {overlap}개(같은 촬영 재사용)")
    if not screen_result.get("best"):
        reasons.append("screen.json 에 best 가 없다(후보 v00: 둔감 또는 유의 후보 없음)")
    if candidate is None:
        return reasons
    failing_scenes = [scene["name"] for scene in scene_results if not scene["control_ok"]]
    if failing_scenes:
        reasons.append(f"confirm 대조 통과 아님: {', '.join(failing_scenes)}")
    if candidate["constraint_failures"]:
        reasons.append(f"confirm 제약 실패: {', '.join(candidate['constraint_failures'])}")
    if not candidate["eligible"]:
        reasons.append("confirm 장면 자격 없음(대조 실패·제약 실패·점수 악화 중 하나)")
    if not candidate["significant"]:
        reasons.append(f"confirm 에서 유의 아님(후보 {candidate['variant']} 개선 ≤ 점수 잡음, 후보 v00)")
    if any(candidate["screen_variant"] in param.get("cost_blocked", []) for param in screen_result.get("params", [])):
        reasons.append(f"cost_blocked: {candidate['screen_variant']} 는 비용이 오르는 끝")
    return reasons


def judge_confirm_document(sweep, sweep_path, plan, lock_path, lock_sha, screen_sweep_text, work_dir):
    if not screen_sweep_text:
        raise JudgeError("confirm 판정은 --screen-sweep(screen sweep.json)이 필요하다")
    if not isinstance(sweep.get("screen_result"), str):
        raise JudgeError("confirm 스윕에 screen_result(screen.json 경로)가 없다")
    screen_result_path = prereg_lock.project_path(sweep["screen_result"])
    screen_result = load_json(screen_result_path)
    if screen_result.get("version") != 2 or screen_result.get("stage") != "screen":
        raise JudgeError(f"{sweep['screen_result']}: screen.json v2 가 아니다")
    if sweep.get("screen_result_sha256") != prereg_lock.file_sha256(screen_result_path):
        raise JudgeError("confirm 스윕의 screen_result_sha256 이 screen.json 의 현재 sha256 과 다르다")
    screen_sweep_path = prereg_lock.project_path(screen_sweep_text)
    screen_sweep = load_json(screen_sweep_path)
    if screen_sweep.get("stage") != "screen":
        raise JudgeError(f"--screen-sweep {screen_sweep_text} 는 stage screen 스윕이 아니다")
    if screen_result.get("screen_sweep_sha256") != prereg_lock.file_sha256(screen_sweep_path):
        raise JudgeError("--screen-sweep 의 sha256 이 screen.json 의 screen_sweep_sha256 과 다르다")
    check_lock(screen_sweep, lock_path)
    sha_table = build_sha_table(screen_sweep, sweep, screen_result, lock_sha)
    mismatched = [key for key, row in sha_table.items() if not row["match"]]
    if mismatched:
        raise JudgeError(f"sha 불일치(screen 스윕·confirm 스윕·screen.json·잠금): {', '.join(mismatched)}")
    rejudged = build_screen_document(screen_sweep, screen_sweep_path, plan, lock_sha, judge_sweep_scenes(screen_sweep, plan, work_dir / "screen-rejudge"), False)
    differing = [key for key in REJUDGE_KEYS if canonical_text(json.loads(json.dumps(rejudged[key]))) != canonical_text(screen_result.get(key))]
    if differing:
        raise JudgeError(f"screen 재판정 불일치: {', '.join(differing)}(screen.json 손편집 또는 판정 입력 변경)")
    scene_results = judge_sweep_scenes(sweep, plan, work_dir, screen_result["noise"])
    candidate, reasons = choose_confirm_candidate(screen_result, screen_sweep, sweep, scene_results)
    is_offline_only = is_offline_only_sweep(sweep)
    overlap = len(step_fingerprints(screen_sweep) & step_fingerprints(sweep))
    reasons += adopt_reasons(candidate, screen_result, screen_sweep, sweep, scene_results, overlap, is_offline_only)
    for scene in scene_results:
        scene["candidate_medians"] = scene["variants"][candidate["variant"]]["medians"] if candidate and candidate["variant"] in scene["variants"] else None
    decision = not reasons and candidate is not None
    adopt = {"decision": decision, "variant": candidate["variant"] if candidate else None, "screen_variant": candidate["screen_variant"] if candidate else None,
             "assignments": plan_param_assignments(sweep, candidate["variant"]) if decision else [], "reasons": reasons,
             "evidence": {"screen_run": screen_sweep.get("run_dir"), "confirm_run": sweep.get("run_dir"), "image_overlap": overlap, "offline_only": is_offline_only, "sha": sha_table}}
    return {"tool": "calib_judge", "version": 2, "stage": "confirm", "sweep": relative(sweep_path), "lock": relative(lock_path), "plan": sweep["plan"], "plan_sha256": sweep["plan_sha256"],
            "overlay_sha256": sweep.get("overlay_sha256"), "canonical_tuning_sha256": sweep.get("canonical_tuning_sha256"), "canonical_look_sha256": sweep.get("canonical_look_sha256"),
            "lock_sha256": lock_sha, "screen_result": relative(screen_result_path), "screen_result_sha256": sweep["screen_result_sha256"], "screen_sweep": relative(screen_sweep_path),
            "scenes": scene_results, "candidate": candidate, "adopt": adopt}


def verify_checks(scene_results, prior):
    prior_scenes = {scene["name"]: scene for scene in prior["scenes"]}
    checks = []
    for scene in scene_results:
        prior_scene = prior_scenes.get(scene["name"])
        if prior_scene is None or not prior_scene.get("candidate_medians"):
            raise JudgeError(f"{scene['name']}: --verify-against 판정에 이 장면의 candidate_medians 가 없다")
        for target in scene["targets"]:
            metric = target["metric"]
            value = scene["control_reference"].get(metric)
            expected = prior_scene["candidate_medians"].get(metric)
            allowed = drift_allowance((prior_scene["noise"].get(metric) or {}).get("drift_floor"), target["tolerance"], scene["series"])
            is_passed = None not in (value, expected, allowed) and abs(value - expected) <= allowed + TOLERANCE
            checks.append({"scene": scene["name"], "metric": metric, "value": value, "expected": expected, "allowed": allowed, "passed": is_passed})
    return checks


def judge_verify_document(sweep, sweep_path, plan, lock_path, lock_sha, verify_against_text, work_dir):
    prior, prior_table = None, None
    if verify_against_text:
        prior = load_json(prereg_lock.project_path(verify_against_text))
        if prior.get("stage") != "confirm" or not (prior.get("adopt") or {}).get("decision"):
            raise JudgeError("--verify-against 는 채택(adopt.decision 참)한 confirm judge.json 이어야 한다")
        prior_table = {scene["name"]: {metric: {"noise": entry.get("noise"), "median_floor": entry.get("drift_floor")} for metric, entry in scene["noise"].items()} for scene in prior["scenes"]}
    scene_results = judge_sweep_scenes(sweep, plan, work_dir, prior_table)
    report = {"tool": "calib_judge", "version": 2, "stage": "verify", "sweep": relative(sweep_path), "lock": relative(lock_path), "plan": sweep["plan"], "plan_sha256": sweep["plan_sha256"],
              "overlay_sha256": sweep.get("overlay_sha256"), "lock_sha256": lock_sha, "scenes": scene_results}
    if prior is not None:
        checks = verify_checks(scene_results, prior)
        report["verify"] = {"against": verify_against_text, "passed": all(check["passed"] for check in checks), "checks": checks}
    return report


def judge_staged(sweep, sweep_path, lock_path, output_path, arguments):
    stage = sweep["stage"]
    if stage not in JUDGED_STAGES:
        raise JudgeError(f"stage {stage!r} 는 판정하지 않는다(frame 은 영역 틀용, 판정 대상 {'|'.join(JUDGED_STAGES)})")
    plan = load_json(prereg_lock.project_path(sweep["plan"]))
    work_dir = output_path.parent / "judge"
    lock_sha = prereg_lock.file_sha256(lock_path)
    if stage == "screen":
        return build_screen_document(sweep, sweep_path, plan, lock_sha, judge_sweep_scenes(sweep, plan, work_dir), True)
    if stage == "confirm":
        return judge_confirm_document(sweep, sweep_path, plan, lock_path, lock_sha, arguments.screen_sweep, work_dir)
    return judge_verify_document(sweep, sweep_path, plan, lock_path, lock_sha, arguments.verify_against, work_dir)


def staged_exit_code(report):
    if not all(scene["control_ok"] for scene in report["scenes"]):
        return EXIT_REJECTED
    if "verify" in report and not report["verify"]["passed"]:
        return EXIT_REJECTED
    return 0


def print_staged_summary(report, output_path):
    for scene in report["scenes"]:
        failing = [name for name, status in scene["control_check"].items() if status != "pass"]
        state = "통과" if scene["control_ok"] else "판정 불가 " + "; ".join(scene["undetermined_reasons"][:3]) if not scene["determinable"] else "실패 " + ", ".join(failing[:6])
        print(f"{scene['name']}({'+'.join(scene['sources'])}, {report['stage']}): 대조 {state}, 대조 점수 {scene['control_score']} 점수 잡음 {scene['score_noise']}")
    if report["stage"] == "screen":
        print(f"keep {report['keep']} best {report['best']} second {report['second']} insensitive {report['insensitive']} 비용 막힌 최선 {report['cost_blocked_best']}")
    if report["stage"] == "confirm":
        print(f"adopt {report['adopt']['decision']} 후보 {report['adopt']['variant']} 사유 {report['adopt']['reasons']}")
    if "verify" in report:
        print(f"verify {'통과' if report['verify']['passed'] else '실패'} ({report['verify']['against']})")
    print("판정", relative(output_path))


def run_staged(sweep, sweep_path, lock_path, output_path, arguments):
    try:
        report = judge_staged(sweep, sweep_path, lock_path, output_path, arguments)
    except (JudgeError, KeyError) as error:
        print(f"입력 오류: {error}", file=sys.stderr)
        return EXIT_INPUT
    write_json(output_path, report)
    print_staged_summary(report, output_path)
    return staged_exit_code(report)


def main():
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description="FluidFX 일괄 보정 판정(spec-wave0-data-bridge-v2 3.9, spec-cal0-v2 단계 판정)")
    parser.add_argument("--sweep", required=True)
    parser.add_argument("--lock", required=True)
    parser.add_argument("--output")
    parser.add_argument("--screen-sweep")
    parser.add_argument("--verify-against")
    arguments = parser.parse_args()
    try:
        sweep_path = prereg_lock.project_path(arguments.sweep)
        sweep = load_json(sweep_path)
        lock_path = prereg_lock.project_path(arguments.lock)
        check_lock(sweep, lock_path)
        default_name = "screen.json" if sweep.get("stage") == "screen" else "judge.json"
        output_path = prereg_lock.project_path(arguments.output) if arguments.output else sweep_path.parent / default_name
        if not is_under(output_path, SAVED_DIR):
            raise JudgeError(f"출력 {output_path} 은 Saved 아래만 허용(LIVE-2 대조 시트는 Saved 만)")
    except JudgeError as error:
        print(f"입력 오류: {error}", file=sys.stderr)
        return EXIT_INPUT
    if sweep.get("invalid_count"):
        print(f"거부: 스윕에 무효 항목 {sweep['invalid_count']}개(calib_sweep 단언 실패)", file=sys.stderr)
        return EXIT_REJECTED
    if sweep.get("stage"):
        return run_staged(sweep, sweep_path, lock_path, output_path, arguments)
    work_dir = output_path.parent / "judge"
    work_dir.mkdir(parents=True, exist_ok=True)
    try:
        scenes = [judge_scene(scene, sweep, work_dir) for scene in sweep["scenes"]]
    except (JudgeError, KeyError) as error:
        print(f"입력 오류: {error}", file=sys.stderr)
        return EXIT_INPUT
    report = {"tool": "calib_judge", "sweep": relative(sweep_path), "lock": relative(lock_path), "scenes": scenes,
              "accepted": all(scene["control_ok"] for scene in scenes)}
    output_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    for scene in scenes:
        candidate = scene["candidate"]
        failing = [name for name, status in scene["control_check"].items() if status != "pass"]
        print(f"{scene['name']}({scene['source']}): 대조 {'통과' if scene['control_ok'] else '실패 ' + ', '.join(failing[:6])}, 후보 {candidate.get('variant')} {candidate.get('values')} 점수 {candidate.get(SCORE_METRIC)} (대조 {candidate.get('control_score')}, 잡음 {candidate.get('score_noise_floor')}, 유의 {candidate.get('is_significant')})")
    print("판정", relative(output_path))
    return 0 if report["accepted"] else EXIT_REJECTED


if __name__ == "__main__":
    sys.exit(main())
