"""실행 환경: 시스템 Python(라이브 포착은 언리얼 에디터가 TDGame을 열고 PIE 가 실행 중이며 원격 실행이 켜진 상태, 에디터 앞창 유지 L-editor-06, TDGame 빌드가 TDFluidPresetAsset 의 build_*_for_preset_kind 를 노출). --offline-capture 는 에디터 없이 고정 포착 JSON 으로 할당 계산과 variant 사본 만들기만 한다.
실행 명령: python Tools/FluidFX/pie_capture_fluid_preset.py [--out-dir Saved/FluidFX/preset_capture/<시각>] [--skip-variant] | python Tools/FluidFX/pie_capture_fluid_preset.py --offline-capture <포착 JSON> [--out-dir <Saved 아래 폴더>] [--skip-variant] (PowerShell). 종료 코드 0 성공(차이 0 포함) / 1 소유 규칙 위반·사본 검사 실패 / 2 환경 문제(PIE 없음·unreal 이름 없음·입력 오류)
출력: <out-dir>/capture.json(PIE 액터별 UPROPERTY 값과 build_*_for_preset_kind 값의 필드 차이, Custom·override·TDFluidRuntimeControlComponent 액터는 excluded 이유만), assignments.json(Kinds·Groups 경로 할당, --set-split 경고, 충돌, 건너뜀), variant/(fluid_tuning_patch.py variant 사본 TDFluidPresetTuning.json·TDFluidMaterialLook.json·variant.json). 정본 TDFluidPresetTuning.json 은 고치지 않는다(되쓰기는 부모가 write-canonical)
상태: 작성만(2026-10-29 FX-16b 묶음 E①). 오프라인 할당 계산과 variant 종료 0 은 고정 포착 JSON 으로 확인, 에디터 쪽 포착 코드는 py_compile 과 정적 검토만이며 실제 PIE 미실행(첫 실행에서 unreal 이름 점검이 막으면 멈춘다).
"""
import argparse
import json
import subprocess
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools"))
sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))
import check_fluid_preset_data
import fluid_tuning_patch

RESULT_MARKER = "TD_FLUID_PRESET_CAPTURE_RESULT"
CAPTURE_FORMAT = "td_fluid_preset_capture/1"
OUTPUT_ROOT = ROOT / "Saved" / "FluidFX" / "preset_capture"
PATCH_SCRIPT = ROOT / "Tools" / "FluidFX" / "fluid_tuning_patch.py"
SKIPPED_KIND = "Custom"
REQUIRED_UNREAL_NAMES = ("UnrealEditorSubsystem", "GameplayStatics", "SystemLibrary", "TDFluidPresetAsset", "TDFluidLiveActor", "TDFluidFragmentEmitterActor")
EXCLUDED_RUNTIME_CONTROL = "runtime_control"
EXCLUDED_NO_PRESET = "no_preset_state"
EXCLUDED_CUSTOM = "custom"
EXCLUDED_OVERRIDE = "override"
MODE_SET = "set"
MODE_SET_SPLIT = "set-split"
EXIT_FAILED = 1
EXIT_ENVIRONMENT = 2
EDITOR_TIMEOUT_SECONDS = 30

EDITOR_CODE = r'''
import importlib.util
import json
import os
import traceback
import unreal

PARAMS = json.loads(__PARAMS__)
MARKER = PARAMS["marker"]


def emit(payload):
    print(MARKER, json.dumps(payload, ensure_ascii=False))


def load_dump_module(project_directory):
    module_path = os.path.join(project_directory, "Tools", "FluidFX", "editor_dump_fluid_preset_builders.py")
    module_spec = importlib.util.spec_from_file_location("td_fluid_dump_builders", module_path)
    module = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(module)
    return module


def read_property(owner, property_name):
    try:
        return True, owner.get_editor_property(property_name)
    except Exception:
        return False, None


def describe_preset(binding, preset):
    if preset is None:
        return {"kind": "Custom", "override": False}
    return {
        "kind": binding.enum_name("ETDFluidPresetKind", preset.get_editor_property("preset_kind")),
        "override": bool(preset.get_editor_property("override_preset_defaults")),
    }


def resolve_live_state(binding, actor):
    found, live_component = read_property(actor, "live_component")
    if not found or live_component is None:
        return None
    return describe_preset(binding, live_component.get_editor_property("active_preset"))


def resolve_fire_target_state(binding, actor):
    found, target = read_property(actor, "fire_target_actor")
    if not found:
        return None
    if target is None:
        return {"kind": "Custom", "override": False}
    return resolve_live_state(binding, target)


def compare_struct(dump, schema, copy_schema, copy_name, actual_values, builder_values):
    registry_name = dump.EXTRA_BUILDER_RETURN_TYPES.get(copy_name, copy_name)
    if registry_name not in schema.registry:
        return None
    registry_fields = {field["name"] for field in schema.fields(registry_name)}
    actual = dump.jsonable_struct(copy_schema, copy_name, actual_values)
    builder = dump.jsonable_struct(copy_schema, copy_name, builder_values)
    names = sorted(name for name in actual if name in builder and name in registry_fields)
    changed = {name: {"value": actual[name], "builder": builder[name]} for name in names if dump.canonical_text(actual[name]) != dump.canonical_text(builder[name])}
    return {"struct": registry_name, "compared_fields": len(names), "changed": changed}


def collect_copy_sources(dump, actor, live_state, fire_target_state):
    sources = []
    found, live_component = read_property(actor, "live_component")
    if found and live_component is not None:
        sources.append(("TDFluidSimulationConfig", "simulation_config", live_component.get_editor_property("simulation_config"), live_state))
    for property_name, struct_name, uses_fire_target in dump.ACTOR_COPY_PROPERTIES:
        found, value = read_property(actor, property_name)
        if found and value is not None:
            sources.append((struct_name, property_name, value, fire_target_state if uses_fire_target else live_state))
    for property_name, component_name in dump.COMPONENT_COPY_PROPERTIES:
        found, component = read_property(actor, property_name)
        if found and component is not None:
            sources.append((component_name, property_name, component, live_state))
    return sources


def capture_actor(dump, binding, schema, copy_schema, expected_values, actor, control_class):
    entry = {"label": actor.get_actor_label(), "class": actor.get_class().get_name(), "excluded": None, "copies": []}
    if control_class is not None and len(actor.get_components_by_class(control_class)) > 0:
        entry["excluded"] = PARAMS["excluded_runtime_control"]
        return entry
    live_state = resolve_live_state(binding, actor)
    fire_target_state = resolve_fire_target_state(binding, actor)
    if live_state is None:
        entry["excluded"] = PARAMS["excluded_no_preset"]
        return entry
    if live_state["kind"] == PARAMS["skipped_kind"]:
        entry["excluded"] = PARAMS["excluded_custom"]
        return entry
    if live_state["override"]:
        entry["excluded"] = PARAMS["excluded_override"]
        return entry
    for copy_name, property_name, value, state in collect_copy_sources(dump, actor, live_state, fire_target_state):
        if state is None or state["kind"] == PARAMS["skipped_kind"] or state["override"]:
            continue
        actual_values = binding.neutral_struct(copy_name, value)
        comparison = compare_struct(dump, schema, copy_schema, copy_name, actual_values, expected_values[state["kind"]][copy_name])
        if comparison is None:
            continue
        entry["copies"].append(dict(comparison, property=property_name, kind=state["kind"]))
    return entry


def collect_actors(world):
    actors = {}
    for class_name in ("TDFluidLiveActor", "TDFluidFragmentEmitterActor"):
        for actor in unreal.GameplayStatics.get_all_actors_of_class(world, getattr(unreal, class_name)):
            actors[actor.get_path_name()] = actor
    return [actors[path] for path in sorted(actors)]


def main():
    missing = [name for name in PARAMS["required_names"] if not hasattr(unreal, name)]
    if missing:
        emit({"error": "unreal 이름 없음(에디터 바이너리가 헤더보다 오래됨)", "missing": missing})
        return
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor.get_game_world()
    if world is None:
        emit({"error": "PIE 가 실행 중이 아니다", "pie_running": False})
        return
    project_directory = unreal.SystemLibrary.get_project_directory()
    dump = load_dump_module(project_directory)
    checker = dump.load_checker(project_directory)
    schema = checker.load_schema(project_directory)
    copy_schema = dump.build_copy_schema(project_directory, schema)
    binding = dump.UnrealSchemaBinding(unreal, copy_schema, [name for name, _ in dump.TUNED_STRUCT_BUILDERS] + list(dump.COPY_ONLY_NAMES))
    values_by_kind = dump.collect_builder_values(unreal, binding)
    extra_values_by_kind, builder_sources = dump.collect_extra_builder_values(unreal, binding, dump.read_declared_builders(project_directory))
    expected_values = {kind: dict(values_by_kind[kind], **extra_values_by_kind[kind]) for kind in schema.kinds()}
    control_class = getattr(unreal, "TDFluidRuntimeControlComponent", None)
    actors = collect_actors(world)
    capture = {
        "format": PARAMS["format"],
        "world": world.get_name(),
        "runtime_control_class_available": control_class is not None,
        "builder_sources": builder_sources,
        "actors": [capture_actor(dump, binding, schema, copy_schema, expected_values, actor, control_class) for actor in actors],
    }
    capture_path = PARAMS["capture_path"]
    os.makedirs(os.path.dirname(capture_path), exist_ok=True)
    with open(capture_path, "w", encoding="utf-8", newline="\n") as capture_file:
        json.dump(capture, capture_file, ensure_ascii=False, indent=1, sort_keys=True)
    excluded_count = sum(1 for entry in capture["actors"] if entry["excluded"])
    emit({"capture_path": capture_path, "actor_count": len(actors), "excluded_count": excluded_count, "world": capture["world"]})


try:
    main()
except Exception as error:
    emit({"error": str(error), "traceback": traceback.format_exc()[-1500:]})
'''


def parse_marker(lines):
    for line in lines:
        marker_index = line.find(RESULT_MARKER)
        if marker_index >= 0:
            return json.loads(line[marker_index + len(RESULT_MARKER):])
    return None


def capture_from_editor(capture_path):
    import run_in_editor

    params = {
        "marker": RESULT_MARKER,
        "format": CAPTURE_FORMAT,
        "capture_path": capture_path.as_posix(),
        "required_names": list(REQUIRED_UNREAL_NAMES),
        "skipped_kind": SKIPPED_KIND,
        "excluded_runtime_control": EXCLUDED_RUNTIME_CONTROL,
        "excluded_no_preset": EXCLUDED_NO_PRESET,
        "excluded_custom": EXCLUDED_CUSTOM,
        "excluded_override": EXCLUDED_OVERRIDE,
    }
    outcome = run_in_editor.run_code(EDITOR_CODE.replace("__PARAMS__", repr(json.dumps(params))), timeout=EDITOR_TIMEOUT_SECONDS)
    if not outcome["connected"]:
        return None, {"error": "에디터에 연결하지 못했다", "lines": outcome["lines"][-5:]}
    result = parse_marker(outcome["lines"])
    if result is None:
        return None, {"error": "결과 표식이 없다", "lines": outcome["lines"][-8:]}
    if "error" in result:
        return None, result
    return json.loads(Path(result["capture_path"]).read_text(encoding="utf-8")), result


def read_offline_capture(capture_path):
    capture = json.loads(Path(capture_path).read_text(encoding="utf-8"))
    if capture.get("format") != CAPTURE_FORMAT:
        raise ValueError(f"{capture_path}: format 이 {CAPTURE_FORMAT} 가 아니다")
    return capture


def value_key(value):
    return json.dumps(value, sort_keys=True, ensure_ascii=False)


def index_candidates(capture):
    candidates = {}
    observed = {}
    excluded = []
    for actor in capture["actors"]:
        if actor.get("excluded"):
            excluded.append({"label": actor["label"], "reason": actor["excluded"]})
            continue
        for copy in actor["copies"]:
            observed_key = (copy["kind"], copy["struct"])
            observed[observed_key] = observed.get(observed_key, 0) + 1
            for field_name, change in copy["changed"].items():
                slots = candidates.setdefault((copy["kind"], copy["struct"], field_name), {})
                slot = slots.setdefault(value_key(change["value"]), {"value": change["value"], "builder": change["builder"], "sources": []})
                slot["sources"].append(f"{actor['label']}:{copy['property']}")
    return candidates, observed, excluded


def find_reject_reason(schema, kind, struct_name, field_name):
    if kind == SKIPPED_KIND or kind not in schema.kinds():
        return "알 수 없는 종류이거나 Custom"
    if struct_name not in schema.registry:
        return "조절 등록표에 없는 구조체"
    if schema.registry[struct_name] != check_fluid_preset_data.PRESET_KIND_SCOPE:
        return "종류 범위 구조체가 아님"
    if field_name not in {field["name"] for field in schema.fields(struct_name)}:
        return "구조체에 없는 필드"
    return None


def describe_candidate(kind, struct_name, field_name, slot):
    return {"kind": kind, "struct": struct_name, "field": field_name, "value": slot["value"], "builder": slot["builder"], "sources": slot["sources"]}


def make_assignment(mode, json_path, slot, owner):
    return {"mode": mode, "json_path": json_path, "value": slot["value"], "builder": slot["builder"], "owner": owner, "sources": slot["sources"]}


def find_partial_change_warnings(accepted, observed):
    warnings = []
    for (kind, struct_name, field_name), slot in accepted.items():
        captured_count = observed.get((kind, struct_name), 0)
        changed_count = len({source.split(":", 1)[0] for source in slot["sources"]})
        if captured_count > changed_count:
            warnings.append(f"부분 변경: {kind} {struct_name} 포착 {captured_count}개 중 {field_name} 이 다른 것은 {changed_count}개, 정본을 바꾸면 나머지도 같은 값이 된다")
    return warnings


def accept_candidates(schema, candidates):
    accepted = {}
    conflicts = []
    skipped = []
    for (kind, struct_name, field_name), slots in sorted(candidates.items()):
        reason = find_reject_reason(schema, kind, struct_name, field_name)
        if reason is not None:
            skipped.append({"kind": kind, "struct": struct_name, "field": field_name, "reason": reason})
            continue
        if len(slots) > 1:
            conflicts.append({"kind": kind, "struct": struct_name, "field": field_name, "values": list(slots.values())})
            continue
        accepted[(kind, struct_name, field_name)] = next(iter(slots.values()))
    return accepted, conflicts, skipped


def is_group_assignment_safe(document, members, struct_name, field_name, accepted):
    member_slots = [accepted.get((member, struct_name, field_name)) for member in members]
    if any(slot is None for slot in member_slots):
        return False
    if any(fluid_tuning_patch.kind_owns_field(document, member, struct_name, field_name) for member in members):
        return False
    return len({value_key(slot["value"]) for slot in member_slots}) == 1


def plan_assignments(capture, document, schema):
    candidates, observed, excluded = index_candidates(capture)
    accepted, conflicts, skipped = accept_candidates(schema, candidates)
    assignments = []
    warnings = []
    resolved = set()
    for (kind, struct_name, field_name), slot in accepted.items():
        if (kind, struct_name, field_name) in resolved:
            continue
        resolved.add((kind, struct_name, field_name))
        kind_path = f"{fluid_tuning_patch.KINDS_SECTION}.{kind}.{struct_name}.{field_name}"
        if fluid_tuning_patch.kind_owns_field(document, kind, struct_name, field_name):
            assignments.append(make_assignment(MODE_SET, kind_path, slot, f"Kinds.{kind}"))
            continue
        group_name = fluid_tuning_patch.find_owner_group(document, kind, struct_name, field_name)
        if group_name is None:
            assignments.append(make_assignment(MODE_SET, kind_path, slot, "StructDefaults"))
            continue
        members = document[fluid_tuning_patch.GROUPS_SECTION][group_name][fluid_tuning_patch.GROUP_MEMBERS_KEY]
        if is_group_assignment_safe(document, members, struct_name, field_name, accepted):
            group_path = f"{fluid_tuning_patch.GROUPS_SECTION}.{group_name}.{struct_name}.{field_name}"
            assignments.append(make_assignment(MODE_SET, group_path, slot, f"Groups.{group_name}"))
            resolved.update((member, struct_name, field_name) for member in members)
            continue
        assignments.append(make_assignment(MODE_SET_SPLIT, kind_path, slot, f"Groups.{group_name}"))
        warnings.append(f"--set-split: {struct_name}.{field_name} 는 Groups.{group_name} 소유이고 회원 {', '.join(members)} 중 {kind} 만(또는 값이 다르게) 바뀌어 {kind} 로 펼친 뒤 바꾼다")
    warnings.extend(find_partial_change_warnings(accepted, observed))
    return {
        "assignments": assignments,
        "warnings": warnings,
        "conflicts": conflicts,
        "skipped": skipped,
        "excluded_actors": excluded,
        "candidate_count": len(candidates),
    }


def build_patch_command(variant_directory, assignments):
    command = [sys.executable, str(PATCH_SCRIPT), "--root", str(ROOT), "variant", "--out-dir", str(variant_directory)]
    for assignment in assignments:
        flag = "--set-split" if assignment["mode"] == MODE_SET_SPLIT else "--set"
        command.extend([flag, f"{assignment['json_path']}={json.dumps(assignment['value'], ensure_ascii=False)}"])
    return command


def run_variant(variant_directory, assignments):
    completed = subprocess.run(build_patch_command(variant_directory, assignments), capture_output=True, text=True, encoding="utf-8", errors="replace")
    return completed.returncode, (completed.stdout + completed.stderr).strip()


def write_json(path, document):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes((json.dumps(document, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8"))


def resolve_output_directory(argument_value):
    if argument_value is None:
        return OUTPUT_ROOT / datetime.now().strftime("%Y%m%d-%H%M%S")
    path = Path(argument_value)
    return path if path.is_absolute() else ROOT / path


def parse_arguments():
    parser = argparse.ArgumentParser(description="PIE 액터 UPROPERTY 값과 프리셋 빌더 값을 비교해 조절 JSON variant 사본을 만든다(FX-16b O1)")
    parser.add_argument("--out-dir", default=None)
    parser.add_argument("--offline-capture", default=None, help="에디터 없이 쓸 고정 포착 JSON 경로")
    parser.add_argument("--skip-variant", action="store_true")
    return parser.parse_args()


def print_plan_summary(plan):
    print("할당", len(plan["assignments"]), "충돌", len(plan["conflicts"]), "건너뜀", len(plan["skipped"]), "제외 액터", len(plan["excluded_actors"]))
    for assignment in plan["assignments"]:
        print(f"  [{assignment['mode']}] {assignment['json_path']} = {json.dumps(assignment['value'], ensure_ascii=False)} (기존 빌더 {json.dumps(assignment['builder'], ensure_ascii=False)})")
    for warning in plan["warnings"]:
        print("경고:", warning)
    for conflict in plan["conflicts"]:
        print("충돌:", conflict["kind"], conflict["struct"], conflict["field"], "서로 다른 값", len(conflict["values"]), "개, 할당하지 않음")


def main():
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    arguments = parse_arguments()
    output_directory = resolve_output_directory(arguments.out_dir)
    try:
        fluid_tuning_patch.require_under_saved(output_directory, ROOT)
    except fluid_tuning_patch.PatchError as error:
        print("입력 오류:", error, file=sys.stderr)
        return EXIT_ENVIRONMENT
    if arguments.offline_capture is not None:
        try:
            capture = read_offline_capture(arguments.offline_capture)
        except (OSError, ValueError, KeyError) as error:
            print("입력 오류:", error, file=sys.stderr)
            return EXIT_ENVIRONMENT
        editor_result = {"offline_capture": str(arguments.offline_capture)}
    else:
        capture, editor_result = capture_from_editor(output_directory / "capture.json")
        if capture is None:
            print("환경 문제:", json.dumps(editor_result, ensure_ascii=False)[:600], file=sys.stderr)
            return EXIT_ENVIRONMENT
    write_json(output_directory / "capture.json", capture)
    try:
        schema = check_fluid_preset_data.load_schema(ROOT)
        document = fluid_tuning_patch.load_canonical_document(ROOT)
    except (check_fluid_preset_data.InputError, OSError, ValueError) as error:
        print("입력 오류:", error, file=sys.stderr)
        return EXIT_ENVIRONMENT
    plan = plan_assignments(capture, document, schema)
    plan["capture_source"] = editor_result
    print_plan_summary(plan)
    if arguments.skip_variant:
        write_json(output_directory / "assignments.json", dict(plan, variant=None))
        return 0
    variant_directory = output_directory / "variant"
    exit_code, patch_output = run_variant(variant_directory, plan["assignments"])
    plan["variant"] = {"directory": variant_directory.as_posix(), "exit_code": exit_code, "output_tail": patch_output[-1200:]}
    write_json(output_directory / "assignments.json", plan)
    print("variant 종료 코드", exit_code, variant_directory.as_posix())
    if exit_code != 0:
        print(patch_output[-1200:], file=sys.stderr)
        return EXIT_FAILED if exit_code == fluid_tuning_patch.EXIT_CHECK_FAILED else EXIT_ENVIRONMENT
    print("다음: 값을 정본에 반영하려면 부모가 write-canonical 을 같은 --set 으로 실행한다(이 도구는 정본을 고치지 않는다)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
