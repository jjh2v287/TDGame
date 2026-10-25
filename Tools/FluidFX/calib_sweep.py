"""실행 환경: 시스템 Python 3(언리얼 에디터가 TDGame을 열고 MCP 포트 8000·원격 실행이 켜진 상태, 에디터 앞창 유지 L-editor-06, A·C 묶음이 든 빌드). 계획 JSON(장면×파라미터)을 읽어 변형마다 fluid_tuning_patch 로 Saved 아래 조절 사본을 만들고, 장면마다 pie_lookdev_fluid.py 를 tuning_reload 변형으로 돌려(물 탐침 장면은 같은 다시 읽기 뒤 pie_probe_fluid_water.run_once) 변형 유효성(ChangedFieldCount·실효 값 다시 읽기, 대조 A/B/A)을 단언한다(spec-wave0-data-bridge-v2 3.9, M8).
실행 명령: python Tools/FluidFX/calib_sweep.py --plan Tools/FluidFX/calib/plans/<id>.json [--dry-run] [--run-dir Saved/FluidFX/calib/<run>] (PowerShell, L-repo-01). --dry-run 은 사본·룩데브 변형 JSON·sweep.json 만 만들고 PIE 를 돌리지 않는다. --assert-scene <장면>=<result.json> 은 이미 있는 룩데브 결과로 단언만 다시 한다
출력: <run-dir>/v<k>/(조절·모양 사본, variant.json), <run-dir>/<장면>/lookdev_spec.json·룩데브 출력(result.json·png)·water_probe.json, <run-dir>/sweep.json(계획 sha256, 변형·순서·단계별 유효성·이미지·탐침 규칙, 정본 sha256 전후). 종료 코드 0 모든 단계 유효·정본 불변 / 1 무효 변형·룩데브 실패·정본 바뀜 / 2 계획 입력 오류
상태: 오프라인(가짜 unreal·가짜 룩데브 결과)만 검사, 실제 PIE 스윕 미실행(2026-10-25). 물 탐침 장면의 PIE 순서(isolate 뒤 reinit)와 다시 읽기 대기 시간은 미검증.
"""
import argparse
import copy
import hashlib
import itertools
import json
import re
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools"))
sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))
import check_fluid_preset_data  # noqa: E402
import fluid_tuning_patch  # noqa: E402

LOOKDEV_SCRIPT = ROOT / "Tools" / "FluidFX" / "pie_lookdev_fluid.py"
CALIB_OUTPUT_ROOT = ROOT / "Saved" / "FluidFX" / "calib"
SWEEP_FILE_NAME = "sweep.json"
LOOKDEV_SPEC_NAME = "lookdev_spec.json"
LOOKDEV_RESULT_NAME = "result.json"
WATER_PROBE_FILE_NAME = "water_probe.json"
PLAN_KEYS = ("id", "scenes", "params", "mode", "repeats", "control_every", "reset", "metrics")
MODES = ("one_at_a_time", "grid")
RESET_MODES = ("reinit", "keep")
METRIC_SOURCES = ("look", "water_probe")
READ_BACK_STRUCTS = ("TDFluidSimulationConfig", "TDFluidSourceEmitterSettings", "TDFluidDownstreamFeedSettings", "TDFluidFlameCardLayerSettings", "TDFluidEmberSettings", "TDFluidFireLightSettings")
CONTROL_ID = "v00"
ID_PATTERN = re.compile(r"^[A-Za-z0-9_-]+$")
FLOAT_RELATIVE_TOLERANCE = 1e-6
FLOAT_ABSOLUTE_TOLERANCE = 1e-7
EXIT_INVALID = 1
EXIT_INPUT = 2


class PlanError(Exception):
    pass


def file_sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def project_path(text):
    path = Path(text)
    return path if path.is_absolute() else ROOT / path


def relative_text(path):
    resolved = Path(path).resolve()
    try:
        return resolved.relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return resolved.as_posix()


def require(condition, message):
    if not condition:
        raise PlanError(message)


def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def validate_scene(scene, index, sources):
    path = f"scenes[{index}]"
    require(isinstance(scene, dict), f"{path}: 객체여야 한다")
    require(isinstance(scene.get("name"), str) and ID_PATTERN.match(scene["name"]), f"{path}.name: 영문·숫자·_- 이름이어야 한다")
    require(isinstance(scene.get("level"), str) and scene["level"].startswith("/Game/"), f"{path}.level: /Game/ 레벨 경로여야 한다")
    require(isinstance(scene.get("camera"), dict), f"{path}.camera: 룩데브 적용 객체(pawn·arm·view·awake·hide 등)여야 한다")
    require("tuning_reload" not in scene["camera"], f"{path}.camera: tuning_reload 는 스윕이 넣는다")
    require(is_number(scene.get("settle")) and scene["settle"] > 0, f"{path}.settle: 양수 초여야 한다")
    require(is_number(scene.get("time_dilation", 1.0)) and scene.get("time_dilation", 1.0) > 0, f"{path}.time_dilation: 양수여야 한다")
    require(scene.get("schedule") is None or isinstance(scene["schedule"], dict), f"{path}.schedule: null 또는 룩데브 schedule 객체여야 한다")
    require(isinstance(scene.get("live2_refs", []), list), f"{path}.live2_refs: 목록이어야 한다")
    if "look" in sources:
        for key in ("regions_template", "targets"):
            require(isinstance(scene.get(key), str) and project_path(scene[key]).is_file(), f"{path}.{key}: look 지표에 필요한 파일이 없다: {scene.get(key)}")


def validate_plan(plan):
    require(isinstance(plan, dict), "계획 최상위는 객체여야 한다")
    missing = [key for key in PLAN_KEYS if key not in plan]
    require(not missing, f"계획 키 없음: {', '.join(missing)}")
    require(isinstance(plan["id"], str) and ID_PATTERN.match(plan["id"]), "id: 영문·숫자·_- 이름이어야 한다")
    require(plan["mode"] in MODES, f"mode: {'|'.join(MODES)}")
    require(isinstance(plan["repeats"], int) and not isinstance(plan["repeats"], bool) and plan["repeats"] >= 1, "repeats: 1 이상 정수")
    require(isinstance(plan["control_every"], int) and not isinstance(plan["control_every"], bool) and plan["control_every"] >= 0, "control_every: 0 이상 정수(0이면 처음·중간·끝만)")
    require(plan["reset"] in RESET_MODES, f"reset: {'|'.join(RESET_MODES)}")
    metrics = plan["metrics"]
    require(isinstance(metrics, dict) and isinstance(metrics.get("sources"), list) and metrics["sources"], "metrics.sources: 지표 출처 목록(look|water_probe)이어야 한다")
    require(all(source in METRIC_SOURCES for source in metrics["sources"]), f"metrics.sources: {'|'.join(METRIC_SOURCES)} 만")
    require(isinstance(metrics.get("noise_floor", {}), dict) and all(is_number(value) and value >= 0 for value in metrics.get("noise_floor", {}).values()), "metrics.noise_floor: {지표: 0 이상 수}")
    require(isinstance(plan["scenes"], list) and plan["scenes"], "scenes: 1개 이상")
    for index, scene in enumerate(plan["scenes"]):
        validate_scene(scene, index, metrics["sources"])
    names = [scene["name"] for scene in plan["scenes"]]
    require(len(set(names)) == len(names), "scenes: name 중복")
    require(isinstance(plan["params"], list) and plan["params"], "params: 1개 이상")
    for index, param in enumerate(plan["params"]):
        path = f"params[{index}]"
        require(isinstance(param, dict) and isinstance(param.get("json_path"), str), f"{path}.json_path: 문자열")
        require(isinstance(param.get("values"), list) and param["values"], f"{path}.values: 1개 이상")
        require(isinstance(param.get("split_group", False), bool), f"{path}.split_group: 불")
    paths = [param["json_path"] for param in plan["params"]]
    require(len(set(paths)) == len(paths), "params: json_path 중복")
    return plan


def load_plan(path):
    try:
        plan = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise PlanError(f"계획을 읽지 못했다: {error}") from error
    return validate_plan(plan)


def describe_params(plan, document, schema):
    described = []
    for index, param in enumerate(plan["params"]):
        try:
            info = fluid_tuning_patch.describe_assignment(document, schema, {"json_path": param["json_path"], "value": param["values"][0], "split_group": param.get("split_group", False)})
        except fluid_tuning_patch.PatchError as error:
            raise PlanError(f"params[{index}]: {error}") from error
        require(info["struct"] in READ_BACK_STRUCTS, f"params[{index}]: {info['struct']} 은 PIE 실효 값 다시 읽기가 없어 변형 유효성을 단언할 수 없다(허용 {', '.join(READ_BACK_STRUCTS)})")
        require(info["affected_kinds"], f"params[{index}]: 이 경로를 따르는 종류가 없다")
        changed_values = [value for value in param["values"] if value != info["base_value"]]
        require(changed_values, f"params[{index}]: 기준 값 {info['base_value']!r} 과 다른 값이 없다(대조만 있는 스윕)")
        described.append({"json_path": param["json_path"], "split_group": param.get("split_group", False), "struct": info["struct"], "field": info["field"],
                          "kinds": info["affected_kinds"], "owner": info["owner"], "base_value": info["base_value"], "values": param["values"], "changed_values": changed_values})
    return described


def make_variant(variant_id, params, chosen_values):
    assignments = []
    expect = []
    for param, value in zip(params, chosen_values):
        expect.append({"struct": param["struct"], "field": param["field"], "kinds": param["kinds"], "value": value})
        if value != param["base_value"]:
            assignments.append({"json_path": param["json_path"], "value": value, "split_group": param["split_group"]})
    return {"id": variant_id, "is_control": not assignments, "assignments": assignments, "expect": expect}


def build_variants(plan, params):
    base_values = [param["base_value"] for param in params]
    if plan["mode"] == "grid":
        combinations = [list(values) for values in itertools.product(*[param["values"] for param in params]) if list(values) != base_values]
    else:
        combinations = []
        for index, param in enumerate(params):
            for value in param["changed_values"]:
                combinations.append(base_values[:index] + [value] + base_values[index + 1:])
    unique = []
    for combination in combinations:
        if combination not in unique:
            unique.append(combination)
    variants = [make_variant(CONTROL_ID, params, base_values)]
    variants.extend(make_variant(f"v{index:02d}", params, combination) for index, combination in enumerate(unique, start=1))
    return variants


def build_sequence(variant_ids, repeats, control_every):
    items = [variant_id for _ in range(repeats) for variant_id in variant_ids]
    middle = len(items) // 2
    sequence = [CONTROL_ID]
    for index, item in enumerate(items):
        wants_control = index > 0 and (index == middle or (control_every and index % control_every == 0))
        if wants_control and sequence[-1] != CONTROL_ID:
            sequence.append(CONTROL_ID)
        sequence.append(item)
    sequence.append(CONTROL_ID)
    return sequence


def write_variant_copies(variants, run_dir, schema):
    for variant in variants:
        try:
            manifest = fluid_tuning_patch.write_variant(variant["assignments"], run_dir / variant["id"], schema=schema)
        except fluid_tuning_patch.PatchError as error:
            raise PlanError(f"{variant['id']}: {error}") from error
        variant.update({"tuning": manifest["tuning"], "look": manifest["look"], "tuning_sha256": manifest["tuning_sha256"], "look_sha256": manifest["look_sha256"], "owners": [item["owner"] for item in manifest["assignments"]]})


def read_back_requests(variant):
    return [{"struct": item["struct"], "field": item["field"], "kinds": item["kinds"]} for item in variant["expect"]]


def step_name(step, variant_id):
    return f"s{step:02d}_{variant_id}"


def tuning_reload_request(variant, reset):
    return {"tuning": variant["tuning"], "look": variant["look"], "reset": reset, "read_back": read_back_requests(variant)}


def make_lookdev_spec(scene, plan, sequence, variants_by_id):
    base = dict(copy.deepcopy(scene["camera"]), time_dilation=scene.get("time_dilation", 1.0))
    steps = []
    for step, variant_id in enumerate(sequence):
        apply = {"tuning_reload": tuning_reload_request(variants_by_id[variant_id], plan["reset"])}
        if scene.get("schedule"):
            apply["schedule"] = copy.deepcopy(scene["schedule"])
        steps.append({"name": step_name(step, variant_id), "apply": apply, "settle": scene["settle"]})
    return {"base": base, "variants": steps}


def normalized_name(value):
    return str(value).split(".")[-1].replace("_", "").upper()


def values_match(actual, expected):
    if isinstance(expected, bool) or isinstance(actual, bool):
        return actual is expected
    if is_number(expected):
        if not is_number(actual):
            return False
        if isinstance(expected, int) and isinstance(actual, int):
            return actual == expected
        return abs(actual - expected) <= max(FLOAT_RELATIVE_TOLERANCE * abs(expected), FLOAT_ABSOLUTE_TOLERANCE)
    if isinstance(expected, str):
        return isinstance(actual, str) and normalized_name(actual) == normalized_name(expected)
    if isinstance(expected, dict):
        return isinstance(actual, dict) and sorted(actual) == sorted(expected) and all(values_match(actual[key], expected[key]) for key in expected)
    return actual == expected


def same_path(left, right):
    return bool(left) and bool(right) and Path(left).resolve() == Path(right).resolve()


def judge_read_back(expect, read):
    if not isinstance(read, dict):
        return [f"{expect['struct']}.{expect['field']}: 다시 읽기 결과 없음"]
    if "error" in read:
        return [f"{expect['struct']}.{expect['field']}: {read['error']}"]
    actors = read.get("actors") or {}
    if not actors:
        return [f"{expect['struct']}.{expect['field']}: 종류 {', '.join(expect['kinds'])} 액터가 장면에 없다(변형이 아무것도 바꾸지 않음)"]
    errors = []
    for label, actor in sorted(actors.items()):
        if "error" in actor:
            errors.append(f"{label}: {actor['error']}")
        elif actor.get("uses_own_copies"):
            errors.append(f"{label}: Custom 또는 override 프리셋이라 표 값이 쓰이지 않는다")
        elif not values_match(actor.get("value"), expect["value"]):
            errors.append(f"{label}: {expect['struct']}.{expect['field']} 실효 값 {actor.get('value')!r} ≠ 변형 값 {expect['value']!r}")
    return errors


def judge_step(variant, apply_outcome):
    reload_outcome = apply_outcome.get("tuning_reload") if isinstance(apply_outcome, dict) else None
    if not isinstance(reload_outcome, dict):
        return ["tuning_reload 결과 없음"]
    if "error" in reload_outcome:
        return ["tuning_reload: " + str(reload_outcome["error"])]
    report = reload_outcome.get("report") or {}
    errors = []
    changed = report.get("changed_field_count")
    if not isinstance(changed, int):
        errors.append("보고서에 changed_field_count 없음")
    elif variant["is_control"] and changed != 0:
        errors.append(f"대조 변형인데 ChangedFieldCount {changed} ≠ 0")
    elif not variant["is_control"] and changed < 1:
        errors.append(f"ChangedFieldCount {changed} < 1(변형이 아무 필드도 바꾸지 않음)")
    if not same_path(report.get("tuning_source_path"), variant["tuning"]):
        errors.append(f"보고서 조절 출처 {report.get('tuning_source_path')!r} ≠ 사본 {variant['tuning']}")
    read_back = reload_outcome.get("read_back")
    if not isinstance(read_back, list) or len(read_back) != len(variant["expect"]):
        return errors + ["실효 값 다시 읽기 결과 수가 기대와 다르다"]
    for expect, read in zip(variant["expect"], read_back):
        errors.extend(judge_read_back(expect, read))
    return errors


def lookdev_images(output_dir, index, entry):
    schedule = entry.get("schedule")
    if isinstance(schedule, dict):
        return [relative_text(shot["path"]) for shot in schedule.get("shots", []) if "path" in shot]
    path = Path(output_dir) / f"{index:02d}_{entry.get('name')}.png"
    return [relative_text(path)] if entry.get("saved") else []


def assert_lookdev_result(result, sequence, variants_by_id, output_dir):
    entries = result.get("variants", []) if isinstance(result, dict) else []
    steps = []
    for index, variant_id in enumerate(sequence):
        entry = entries[index] if index < len(entries) else None
        expected_name = step_name(index, variant_id)
        if entry is None or entry.get("name") != expected_name:
            steps.append({"step": index, "name": expected_name, "variant": variant_id, "valid": False, "errors": ["룩데브 결과에 이 단계가 없다"], "images": []})
            continue
        errors = judge_step(variants_by_id[variant_id], entry.get("apply"))
        apply = entry.get("apply") if isinstance(entry.get("apply"), dict) else {}
        steps.append({"step": index, "name": expected_name, "variant": variant_id, "valid": not errors, "errors": errors,
                      "images": lookdev_images(output_dir, index, entry), "shot_game_time": apply.get("shot_game_time"),
                      "report": (apply.get("tuning_reload") or {}).get("report")})
    return steps


def run_lookdev(spec_path, level, output_dir):
    command = [sys.executable, str(LOOKDEV_SCRIPT), "--variants", str(spec_path), "--level", level, "--output-dir", str(output_dir), "--stop-pie"]
    return subprocess.run(command).returncode


def run_water_probe_scene(scene, plan, sequence, variants_by_id, scene_dir):
    import pie_lookdev_fluid
    import pie_probe_fluid_water as probe

    stage_report = probe.read_stage_report()
    contract = probe.read_material_contract()
    if stage_report is None or sorted(contract) != ["scalar_names", "texture_names", "vector_names"]:
        return {"error": "물 무대 보고서 또는 재질 계약이 없다(pie_probe_fluid_water.py 전제 조건)"}, []
    points, layout = stage_report["river"]["probe_points"], stage_report["river"]["layout"]
    if not probe.open_stage_level().get("loaded"):
        return {"error": "물 무대 레벨을 열지 못했다"}, []
    session = probe.uemcp.connect()
    environment = {"navigation_ready": probe.pie_check_fluid.wait_for_editor_navigation()}
    if not probe.pie_check_fluid.start_pie(session, environment) or not probe.pie_check_fluid.wait_for_pie_state(True, probe.pie_check_fluid.PIE_START_WAIT_SECONDS):
        return {"error": "PIE를 시작하지 못했다", "environment": environment}, []
    time.sleep(3.0)
    setup = {"requested_backend": scene.get("backend", "cpu"), "requested_grid": None}
    steps = []
    try:
        status = probe.editor_action("status")
        if "error" in status or not probe.prepare_backend(setup, setup["requested_backend"], None):
            return {"error": "물 탐침 환경·백엔드 확인 실패", "status": status}, []
        probe.editor_action("isolate")
        probe.editor_action("console", command=f"{probe.PERF_CONSOLE_VARIABLE} 1")
        for index, variant_id in enumerate(sequence):
            variant = variants_by_id[variant_id]
            reload_outcome = pie_lookdev_fluid.run_tuning_reload(tuning_reload_request(variant, plan["reset"]))
            errors = judge_step(variant, {"tuning_reload": reload_outcome})
            run = None
            if not errors:
                time.sleep(scene["settle"])
                run = probe.run_once(points, layout, contract, status["sleep_delay_seconds"], setup["requested_backend"])
            steps.append({"step": index, "name": step_name(index, variant_id), "variant": variant_id, "valid": not errors, "errors": errors, "images": [],
                          "report": reload_outcome.get("report"), "water_probe": None if run is None else {"rules": run["rules"], "failed": run["failed"]}})
    finally:
        canonical = pie_lookdev_fluid.editor_action("lookdev_tuning_load", tuning="", look="")
        probe.editor_action("console", command=f"{probe.PERF_CONSOLE_VARIABLE} 0")
        probe.editor_action("sampler_stop")
        setup["restored"] = probe.restore_backend(setup)
        probe.pie_check_fluid.stop_pie(session, environment)
    return {"canonical_reload": canonical, "backend": setup}, steps


def canonical_hashes():
    return {"tuning": file_sha256(fluid_tuning_patch.canonical_tuning_path()), "look": file_sha256(fluid_tuning_patch.canonical_look_path())}


def prepare_sweep(plan_path, run_dir):
    plan = load_plan(plan_path)
    schema = check_fluid_preset_data.load_schema(ROOT)
    document = fluid_tuning_patch.load_canonical_document(ROOT)
    params = describe_params(plan, document, schema)
    variants = build_variants(plan, params)
    sequence = build_sequence([variant["id"] for variant in variants if not variant["is_control"]], plan["repeats"], plan["control_every"])
    write_variant_copies(variants, run_dir, schema)
    sweep = {"tool": "calib_sweep", "plan": relative_text(plan_path), "plan_sha256": file_sha256(plan_path), "plan_id": plan["id"], "run_dir": relative_text(run_dir),
             "params": params, "variants": variants, "sequence": sequence, "metrics": plan["metrics"], "scenes": []}
    for scene in plan["scenes"]:
        scene_dir = run_dir / scene["name"]
        scene_dir.mkdir(parents=True, exist_ok=True)
        spec_path = scene_dir / LOOKDEV_SPEC_NAME
        spec_path.write_text(json.dumps(make_lookdev_spec(scene, plan, sequence, {variant["id"]: variant for variant in variants}), ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
        sweep["scenes"].append({"name": scene["name"], "level": scene["level"], "spec": relative_text(spec_path), "output_dir": relative_text(scene_dir), "regions_template": scene.get("regions_template"),
                                "targets": scene.get("targets"), "live2_refs": scene.get("live2_refs", []), "uses_water_probe": "water_probe" in plan["metrics"]["sources"], "steps": []})
    return plan, sweep


def run_scenes(plan, sweep, assert_results):
    variants_by_id = {variant["id"]: variant for variant in sweep["variants"]}
    for scene, entry in zip(plan["scenes"], sweep["scenes"]):
        scene_dir = project_path(entry["output_dir"])
        if entry["name"] in assert_results:
            result_path = project_path(assert_results[entry["name"]])
            entry["lookdev_exit"] = None
        elif entry["uses_water_probe"]:
            entry["water_probe"], entry["steps"] = run_water_probe_scene(scene, plan, sweep["sequence"], variants_by_id, scene_dir)
            (scene_dir / WATER_PROBE_FILE_NAME).write_text(json.dumps(entry, ensure_ascii=False, indent=2, default=str) + "\n", encoding="utf-8", newline="\n")
            continue
        else:
            entry["lookdev_exit"] = run_lookdev(project_path(entry["spec"]), scene["level"], scene_dir)
            result_path = scene_dir / LOOKDEV_RESULT_NAME
        try:
            result = json.loads(result_path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            entry["error"] = f"룩데브 결과를 읽지 못했다: {error}"
            continue
        entry["lookdev_result"] = relative_text(result_path)
        entry["key_errors"] = result.get("key_errors", [])
        entry["steps"] = assert_lookdev_result(result, sweep["sequence"], variants_by_id, result_path.parent)


def count_problems(sweep):
    problems = []
    for entry in sweep["scenes"]:
        if entry.get("error"):
            problems.append(f"{entry['name']}: {entry['error']}")
        if isinstance(entry.get("water_probe"), dict) and "error" in entry["water_probe"]:
            problems.append(f"{entry['name']}: {entry['water_probe']['error']}")
        if entry.get("lookdev_exit") not in (None, 0):
            problems.append(f"{entry['name']}: 룩데브 종료 {entry['lookdev_exit']}")
        problems.extend(f"{entry['name']}: {line}" for line in entry.get("key_errors", []))
        problems.extend(f"{entry['name']}/{step['name']}: {error}" for step in entry["steps"] for error in step["errors"])
        if not entry["steps"] and not entry.get("dry_run"):
            problems.append(f"{entry['name']}: 단계 결과 없음")
    if sweep["canonical_sha256_after"] != sweep["canonical_sha256_before"]:
        problems.append("실행 전후 정본 sha256 이 다르다")
    return problems


def parse_assert_results(texts):
    results = {}
    for text in texts:
        name, separator, path = text.partition("=")
        if not separator:
            raise PlanError(f"--assert-scene {text!r}: <장면>=<result.json>")
        results[name] = path
    return results


def main():
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description="FluidFX 장면×파라미터 일괄 보정 스윕(spec-wave0-data-bridge-v2 3.9)")
    parser.add_argument("--plan", required=True, type=Path)
    parser.add_argument("--run-dir", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--assert-scene", action="append", default=[])
    arguments = parser.parse_args()
    try:
        plan_path = project_path(arguments.plan)
        assert_results = parse_assert_results(arguments.assert_scene)
        before = canonical_hashes()
        plan = load_plan(plan_path)
        run_dir = project_path(arguments.run_dir) if arguments.run_dir else CALIB_OUTPUT_ROOT / f"{plan['id']}-{datetime.now().strftime('%Y%m%d-%H%M%S')}"
        fluid_tuning_patch.require_under_saved(run_dir, ROOT)
        plan, sweep = prepare_sweep(plan_path, run_dir)
    except (PlanError, fluid_tuning_patch.PatchError, check_fluid_preset_data.InputError, OSError) as error:
        print(f"계획 오류: {error}", file=sys.stderr)
        return EXIT_INPUT
    sweep["canonical_sha256_before"] = before
    try:
        if arguments.dry_run:
            for entry in sweep["scenes"]:
                entry["dry_run"] = True
        else:
            run_scenes(plan, sweep, assert_results)
    finally:
        sweep["canonical_sha256_after"] = canonical_hashes()
        problems = count_problems(sweep)
        sweep["problems"] = problems
        sweep["invalid_count"] = len(problems)
        (run_dir / SWEEP_FILE_NAME).write_text(json.dumps(sweep, ensure_ascii=False, indent=2, default=str) + "\n", encoding="utf-8", newline="\n")
    print("스윕", relative_text(run_dir / SWEEP_FILE_NAME), f"변형 {len(sweep['variants'])}개, 단계 {len(sweep['sequence'])}개, 장면 {len(sweep['scenes'])}개")
    for line in problems:
        print("무효:", line)
    return EXIT_INVALID if problems else 0


if __name__ == "__main__":
    sys.exit(main())
