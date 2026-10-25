"""실행 환경: 시스템 Python 3(에디터 불필요, look 지표는 measure_fx_look.py 의 numpy·Pillow·scipy 필요). calib_sweep.py 의 sweep.json 을 사전 등록 잠금(prereg_lock.py) 확인 뒤 판정한다: look 장면은 단계 이미지를 영역 틀로 measure_fx_look.py --targets 에 넣어 LIVE-2 목표 범위를, 물 장면은 pie_probe_fluid_water 규칙 값(숫자·숫자 목록·숫자 사전 항목을 이름.키·이름[번호]로 펼침)을 지표로 삼는다. 계획 metrics.targets(목표 지표 목록)만 대조 판정과 점수에 쓴다(spec-wave0-data-bridge-v2 3.9 5·6단계).
실행 명령: python Tools/FluidFX/calib_judge.py --sweep Saved/FluidFX/calib/<run>/sweep.json --lock Saved/FluidFX/calib/<id>-prereg-lock.json [--output Saved/FluidFX/calib/<run>/judge.json]
출력: --output JSON(장면별 targets 표·noise_floor·drift·control_allowed·control_check·curves·candidate·live2_sheet, 기본 <run>/judge.json, Saved 밖 거부)과 장면별 measure 결과 <run>/judge/<장면>_measure.json, 표준 출력 요약. 종료 코드 0 판정 완료·대조 표류가 허용 안 / 1 스윕 무효·대조 표류 초과 또는 판정 불가 / 2 잠금 없음·불일치·입력 오류. 점수 score = 목표 지표별 가중치 × |값 − 목표 중앙| / 목표 범위 반폭의 합(작을수록 좋음), 후보는 단계 점수 중앙값이 최소인 변형이고 유의 = 대조 점수 − 최소 점수 > Σ 가중치 × 지표 잡음 바닥 / 반폭. 대조 허용 = max(2 × 잡음 바닥, 목표의 tolerance), 목표 밖 지표 표류는 진단(diagnostic_drift_over_allowed)만. rule_score(look 은 gap_bands 합, 물은 필수 규칙 실패 수)는 보조로만 남긴다.
상태: 오프라인(합성 탐침 규칙·합성 단계 점수·2026-10-25 수용 시험 산출 재판정)만 검사, 새 규칙의 실제 PIE 스윕 판정 미실행(2026-10-25). 잡음 바닥은 계획 metrics.noise_floor 가 없으면 비대조 변형 반복 간 최대 범위(반복 2 이상 필요).
"""
import argparse
import json
import statistics
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))
import prereg_lock  # noqa: E402

MEASURE_SCRIPT = ROOT / "Tools" / "FluidFX" / "measure_fx_look.py"
SAVED_DIR = ROOT / "Saved"
CONTROL_ID = "v00"
SCORE_METRIC = "score"
RULE_METRIC = "rule_score"
CONTROL_NOISE_FACTOR = 2.0
KIND_GROUP_PREFIX = "kind_metrics."
TOLERANCE = 1e-12
EXIT_REJECTED = 1
EXIT_INPUT = 2


class JudgeError(Exception):
    pass


def load_json(path):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise JudgeError(f"{path}: 읽지 못했다({error})") from error


def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def is_under(path, folder):
    resolved = Path(path).resolve()
    folder = Path(folder).resolve()
    return resolved == folder or folder in resolved.parents


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


def measure_look_scene(scene, steps, work_dir):
    selection = load_json(prereg_lock.project_path(scene["targets"]))
    live2_path = prereg_lock.project_path(selection["live2_targets"])
    regions_path = work_dir / f"{scene['name']}_regions.json"
    output_path = work_dir / f"{scene['name']}_measure.json"
    regions_path.write_text(json.dumps(build_measure_regions(scene, steps), ensure_ascii=False, indent=1) + "\n", encoding="utf-8", newline="\n")
    completed = subprocess.run([sys.executable, str(MEASURE_SCRIPT), "--regions", str(regions_path), "--output", str(output_path), "--targets", str(live2_path)], capture_output=True, text=True, encoding="utf-8", errors="replace")
    if completed.returncode != 0:
        raise JudgeError(f"measure_fx_look 종료 {completed.returncode}: {(completed.stderr or completed.stdout)[-600:]}")
    measured = {image["id"]: image for image in load_json(output_path)["images"]}
    items = []
    for step in steps:
        values = {}
        gaps = []
        for index in range(len(step["images"])):
            for region in measured[f"{step['name']}_{index}"]["regions"]:
                for check in selected_checks(region, selection):
                    values[f"{index}.{region['name']}.{check['metric']}"] = check["value"]
                    gaps.append(check["gap_bands"])
                    values.setdefault(f"band.{region['name']}.{check['metric']}", [check["group"], check["pass_low"], check["pass_high"]])
        metrics = {name: value for name, value in values.items() if not name.startswith("band.")}
        items.append({"step": step["step"], "variant": step["variant"], "metrics": metrics, RULE_METRIC: sum(gaps), "bands": {name[5:]: value for name, value in values.items() if name.startswith("band.")}})
    return items, load_json(live2_path), relative(output_path)


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


def resolve_targets(sweep, scene_name, items):
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
            raise JudgeError(f"{scene_name}: 목표 지표 {metric} 이 측정값에 없다")
        low, high = target.get("low"), target.get("high")
        if low is None or high is None:
            band = bands.get(metric.split(".", 1)[-1])
            if band is None:
                raise JudgeError(f"{scene_name}: 목표 지표 {metric} 의 목표 범위(low·high)가 계획에도 LIVE-2 목표에도 없다")
            low, high = band[1], band[2]
        if not high > low:
            raise JudgeError(f"{scene_name}: 목표 지표 {metric} 의 low {low} 가 high {high} 보다 작지 않다")
        resolved.append({"metric": metric, "weight": float(target.get("weight", 1.0)), "tolerance": float(target.get("tolerance", 0.0)), "low": low, "high": high,
                         "center": (low + high) / 2, "half_width": (high - low) / 2})
    if not resolved:
        raise JudgeError(f"{scene_name}: 이 장면에 걸리는 목표 지표가 없다")
    return resolved


def normalized_distance(value, target):
    return abs(value - target["center"]) / target["half_width"]


def score_item(item, targets):
    total = 0.0
    for target in targets:
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
        metric_key = f"0.{key}"
        sheet.append({"region": region_name, "metric": metric, "label": label, "live2_median": median, "pass_low": pass_low, "pass_high": pass_high,
                      "control": medians.get(CONTROL_ID, {}).get(metric_key), "candidate": medians.get(candidate_variant, {}).get(metric_key) if candidate_variant else None})
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
        (items, live2, measure_path), source = measure_look_scene(scene, steps, work_dir), "look"
    targets = resolve_targets(sweep, scene["name"], items)
    aggregated = aggregate(items, sweep, targets)
    candidate = choose_candidate(sweep, aggregated)
    result = {"name": scene["name"], "source": source, "measure": measure_path, "targets": build_target_table(targets, aggregated), "noise_floor": aggregated["noise_floor"], "drift": aggregated["drift"],
              "control_allowed": aggregated["control_allowed"], "control_check": aggregated["control_check"], "control_ok": aggregated["control_ok"],
              "diagnostic_drift_over_allowed": aggregated["diagnostic_drift_over_allowed"], "repeat_counts": aggregated["repeat_counts"],
              "curves": build_curves(sweep, aggregated["medians"]), "candidate": candidate}
    if live2 is not None:
        result["live2_sheet"] = build_live2_sheet(items, live2, aggregated["medians"], candidate.get("variant"))
    return result


def main():
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description="FluidFX 일괄 보정 판정(spec-wave0-data-bridge-v2 3.9)")
    parser.add_argument("--sweep", required=True)
    parser.add_argument("--lock", required=True)
    parser.add_argument("--output")
    arguments = parser.parse_args()
    try:
        sweep_path = prereg_lock.project_path(arguments.sweep)
        sweep = load_json(sweep_path)
        check_lock(sweep, prereg_lock.project_path(arguments.lock))
        output_path = prereg_lock.project_path(arguments.output) if arguments.output else sweep_path.parent / "judge.json"
        if not is_under(output_path, SAVED_DIR):
            raise JudgeError(f"출력 {output_path} 은 Saved 아래만 허용(LIVE-2 대조 시트는 Saved 만)")
    except JudgeError as error:
        print(f"입력 오류: {error}", file=sys.stderr)
        return EXIT_INPUT
    if sweep.get("invalid_count"):
        print(f"거부: 스윕에 무효 항목 {sweep['invalid_count']}개(calib_sweep 단언 실패)", file=sys.stderr)
        return EXIT_REJECTED
    work_dir = output_path.parent / "judge"
    work_dir.mkdir(parents=True, exist_ok=True)
    try:
        scenes = [judge_scene(scene, sweep, work_dir) for scene in sweep["scenes"]]
    except (JudgeError, KeyError) as error:
        print(f"입력 오류: {error}", file=sys.stderr)
        return EXIT_INPUT
    report = {"tool": "calib_judge", "sweep": relative(sweep_path), "lock": relative(prereg_lock.project_path(arguments.lock)), "scenes": scenes,
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
