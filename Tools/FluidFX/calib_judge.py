"""실행 환경: 시스템 Python 3(에디터 불필요, look 지표는 measure_fx_look.py 의 numpy·Pillow·scipy 필요). calib_sweep.py 의 sweep.json 을 사전 등록 잠금(prereg_lock.py) 확인 뒤 판정한다: look 장면은 단계 이미지를 영역 틀로 measure_fx_look.py --targets 에 넣어 LIVE-2 목표 격차(gap_bands)를, 물 장면은 pie_probe_fluid_water 규칙 값을 지표로 삼아 파라미터 곡선·후보 값·잡음 바닥·대조 표류·LIVE-2 대조 시트를 낸다(spec-wave0-data-bridge-v2 3.9 5·6단계).
실행 명령: python Tools/FluidFX/calib_judge.py --sweep Saved/FluidFX/calib/<run>/sweep.json --lock Saved/FluidFX/calib/<id>-prereg-lock.json [--output Saved/FluidFX/calib/<run>/judge.json]
출력: --output JSON(장면별 noise_floor·drift·control_check·curves·candidate·live2_sheet, 기본 <run>/judge.json, Saved 밖 거부)과 장면별 measure 결과 <run>/judge/<장면>_measure.json, 표준 출력 요약. 종료 코드 0 판정 완료·대조 표류가 잡음 바닥 안 / 1 스윕 무효·대조 표류 초과 또는 판정 불가 / 2 잠금 없음·불일치·입력 오류
상태: 오프라인(합성 이미지·가짜 탐침 규칙)만 검사, 실제 스윕 결과 판정 미실행(2026-10-25). 잡음 바닥은 계획 metrics.noise_floor 가 없으면 비대조 변형 반복 간 최대 범위(반복 2 이상 필요).
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
        items.append({"step": step["step"], "variant": step["variant"], "metrics": metrics, SCORE_METRIC: sum(gaps), "bands": {name[5:]: value for name, value in values.items() if name.startswith("band.")}})
    return items, load_json(live2_path), relative(output_path)


def water_rule_metrics(rules):
    metrics = {}
    for name, rule in rules.items():
        value = rule.get("value") if isinstance(rule, dict) else None
        if is_number(value):
            metrics[name] = value
        elif isinstance(value, list):
            metrics.update({f"{name}[{index}]": item for index, item in enumerate(value) if is_number(item)})
    return metrics


def measure_water_scene(steps):
    items = []
    for step in steps:
        probe = step.get("water_probe")
        if not isinstance(probe, dict):
            raise JudgeError(f"{step['name']}: 물 탐침 결과가 없다")
        failed_required = sum(1 for rule in probe["rules"].values() if isinstance(rule, dict) and rule.get("passed") is False and rule.get("required", True))
        items.append({"step": step["step"], "variant": step["variant"], "metrics": water_rule_metrics(probe["rules"]), SCORE_METRIC: failed_required})
    return items


def spread(values):
    return max(values) - min(values) if values else None


def metric_values(items, name):
    return [item["metrics"][name] if name != SCORE_METRIC else item[SCORE_METRIC] for item in items if name == SCORE_METRIC or is_number(item["metrics"].get(name))]


def aggregate(items, sweep):
    by_variant = defaultdict(list)
    for item in sorted(items, key=lambda entry: entry["step"]):
        by_variant[item["variant"]].append(item)
    names = sorted(set().union(*[set(item["metrics"]) for item in items])) + [SCORE_METRIC] if items else [SCORE_METRIC]
    planned = sweep["metrics"].get("noise_floor", {})
    noise_floor = {}
    for name in names:
        if name in planned:
            noise_floor[name] = planned[name]
            continue
        spreads = [spread(metric_values(group, name)) for variant, group in by_variant.items() if variant != CONTROL_ID and len(metric_values(group, name)) >= 2]
        noise_floor[name] = max(spreads) if spreads else None
    controls = by_variant.get(CONTROL_ID, [])
    drift = {}
    control_check = {}
    for name in names:
        values = metric_values(controls, name)
        drift[name] = max(abs(value - values[0]) for value in values) if len(values) >= 2 else None
        if drift[name] is None or noise_floor[name] is None:
            control_check[name] = "undetermined"
        else:
            control_check[name] = "pass" if drift[name] <= noise_floor[name] + TOLERANCE else "fail"
    medians = {variant: {name: statistics.median(metric_values(group, name)) for name in names if metric_values(group, name)} for variant, group in by_variant.items()}
    return {"noise_floor": noise_floor, "drift": drift, "control_check": control_check, "control_ok": all(status == "pass" for status in control_check.values()),
            "medians": medians, "repeat_counts": {variant: len(group) for variant, group in by_variant.items()}}


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
                points.append({"value": values[index], "variant": variant_id, SCORE_METRIC: medians[variant_id].get(SCORE_METRIC), "metrics": {name: value for name, value in medians[variant_id].items() if name != SCORE_METRIC}})
        curves.append({"json_path": param["json_path"], "base_value": param["base_value"], "points": sorted(points, key=lambda point: json.dumps(point["value"]))})
    return curves


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
            "score_noise_floor": floor, "is_significant": is_significant, "assignments": next(variant["assignments"] for variant in sweep["variants"] if variant["id"] == best)}


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
    aggregated = aggregate(items, sweep)
    candidate = choose_candidate(sweep, aggregated)
    result = {"name": scene["name"], "source": source, "measure": measure_path, "noise_floor": aggregated["noise_floor"], "drift": aggregated["drift"],
              "control_check": aggregated["control_check"], "control_ok": aggregated["control_ok"], "repeat_counts": aggregated["repeat_counts"],
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
        print(f"{scene['name']}({scene['source']}): 대조 {'통과' if scene['control_ok'] else '실패 ' + ', '.join(failing[:6])}, 후보 {candidate.get('variant')} {candidate.get('values')} 점수 {candidate.get(SCORE_METRIC)} (대조 {candidate.get('control_score')}, 유의 {candidate.get('is_significant')})")
    print("판정", relative(output_path))
    return 0 if report["accepted"] else EXIT_REJECTED


if __name__ == "__main__":
    sys.exit(main())
