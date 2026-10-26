"""실행 환경: 시스템 Python 3. 생성·프로브·덤프는 에디터가 이 프로젝트를 열고 MCP 8000(NiagaraToolsets)·원격 실행이 살아 있어야 하고, --fake-calls 는 에디터 없이 돈다. 구조 명세(Tools/FluidFX/niagara_specs/NS_TD_*.json, spec-fx8n 2.2)를 fluid_niagara_drive 로 스택 계획으로 바꿔 공식 NiagaraToolset_System 으로 GPU 유체 구동 나이아가라 시스템을 만든다(덮어쓰기·삭제 안 함).
실행 명령: python -B Tools/FluidFX/editor_make_fluid_driven_niagara.py --spec <명세.json> [--asset <경로>] [--look <모양 JSON>] [--probe] [--dump-only] [--fake-calls <출력.json>] (--probe 단독 = 0a 스키마 기록, --probe --asset /Game/Repro/FluidFX/<새 이름> = 0b 비엄격 시범, 생성은 그 명세의 0b 통과 뒤. 모양 키는 늘 명세 asset 이고 --asset 은 만들 경로만 바꾼다(반박 M3), 재현 사본은 /Game/FluidFX 밖 /Game/Repro/FluidFX/ 에만(반박 M1))
출력: Saved/FluidFX/niagara/driven-probe.json(0a·명세별 0b), 나이아가라 시스템, Saved/FluidFX/niagara/<이름>.dump.json(v1 과 같은 정규화 절 + 절별 sha256)·<이름>.report.json(단계·확인 항목), --fake-calls 는 호출 기록 JSON(User 기본값은 <look> 자리표시, 반박 S1). 종료 0 = 통과, 1 = 단계·확인 실패, 2 = 에디터·MCP 연결 실패, 3 = 선행 조건(명세 오류·에셋 있음·0b 미통과·재질 없음·모양 항목 없음·모르는 User 변수)
상태: 미검증(2026-10-25 FX-8n B1 작성: py_compile·가짜 클라이언트 호출 기록·검사기만, 에디터 실행 전). User 기본값은 런타임 비텍스처 8개(v1 관례, 반박 M1)와 조절 등록표 이름 전부를 모양 항목 NiagaraSystems[<명세 asset>] 에서 읽는다.
"""
import argparse
import hashlib
import json
import pathlib
import sys

TOOLS_ROOT = pathlib.Path(__file__).resolve().parents[1]
PROJECT_ROOT = TOOLS_ROOT.parent
sys.path.insert(0, str(TOOLS_ROOT))
sys.path.insert(0, str(TOOLS_ROOT / "FluidFX"))
import fluid_material_look
import fluid_niagara_drive as drive
import fluid_niagara_toolset as toolset

TOOL_NAME = "editor_make_fluid_driven_niagara"
OUTPUT_DIR = PROJECT_ROOT / "Saved" / "FluidFX" / "niagara"
PROBE_PATH = OUTPUT_DIR / "driven-probe.json"
REPRO_ASSET_ROOT = "/Game/Repro/FluidFX/"


def load_valid_spec(spec_path):
    try:
        spec = drive.read_spec(spec_path)
    except (OSError, ValueError) as error:
        raise toolset.TDPreconditionError(f"명세 읽기 실패 {spec_path}: {error}") from error
    problems = drive.find_spec_problems(spec)
    if problems:
        raise toolset.TDPreconditionError(f"명세 오류 {spec_path}: " + " | ".join(problems))
    return spec


def resolve_look_path(look_path):
    return pathlib.Path(look_path).resolve() if look_path else pathlib.Path(fluid_material_look.default_look_path(str(PROJECT_ROOT)))


def read_look_entry(spec, look_path):
    try:
        look_table = fluid_material_look.TDMaterialLookTable.read_project(str(PROJECT_ROOT), str(look_path))
        return look_table.niagara(spec["asset"])
    except (OSError, ValueError, RuntimeError) as error:
        raise toolset.TDPreconditionError(f"모양 항목 읽기 실패({look_path}): {error}") from error


def read_user_defaults(spec, look_path):
    entry = read_look_entry(spec, look_path)
    required = drive.required_look_names(spec)
    known_names = {name for name, _ in required}
    unknown_names = sorted(name for section in fluid_material_look.NIAGARA_SECTIONS for name in entry[section] if name not in known_names)
    if unknown_names:
        raise toolset.TDPreconditionError("모양 항목에 명세가 모르는 User 변수: " + ", ".join(unknown_names))
    defaults = {}
    for name, type_path in required:
        section_name = drive.LOOK_SECTION_BY_TYPE[type_path]
        if name not in entry[section_name]:
            raise toolset.TDPreconditionError(f"모양 항목에 User 변수 없음: {spec['asset']} {section_name}.{name}")
        defaults[name] = entry[section_name][name]
    return defaults


def build_user_variables(spec, user_defaults):
    variables = []
    for name, type_path in drive.build_user_variable_plan(spec):
        default = None if type_path == toolset.TEXTURE_CLASS else (user_defaults[name] if user_defaults is not None else toolset.LOOK_PLACEHOLDER)
        variables.append({"name": name, "type": toolset.type_definition(type_path), "description": "", "defaultValue": toolset.instanced_value(type_path, default)})
    return variables


def build_system(client, system_path, spec, user_defaults, recorder, template, should_create=True):
    emitter = spec["emitter_name"]
    system_ref = toolset.reference(toolset.object_path(system_path))
    binding_report = []
    state_report = []
    placeholders = {}
    if should_create:
        recorder.run("1 시스템 생성", lambda: toolset.create_system(client, system_path, template))
    removed = recorder.run("1 템플릿 이미터 제거", lambda: toolset.remove_template_emitters(client, system_path, emitter))
    recorder.run("1 시스템 데이터", lambda: client.call("SetSystemData", system=system_ref, systemData={"propertyValues": json.dumps(drive.build_system_data(spec))}))
    recorder.run("2 이미터 추가", lambda: client.call("AddEmitter", system=system_ref, templateEmitter=toolset.reference(drive.MINIMAL_EMITTER), emitterName=emitter))
    recorder.run("2 이미터 데이터", lambda: client.call(
        "SetEmitterData", emitter=toolset.stack_ref(system_path, emitter), emitterData={"propertyValues": json.dumps(drive.build_emitter_data(spec))}))
    topology = recorder.run("2 이미터 토폴로지", lambda: client.call("GetEmitterTopology", emitterRef=toolset.stack_ref(system_path, emitter)))
    user_variables = build_user_variables(spec, user_defaults)
    recorder.run(f"3 User 변수 {len(user_variables)}", lambda: client.call("AddUserVariables", system=system_ref, variablesToAdd=user_variables))
    builders = {
        "set": lambda step: toolset.add_set_parameters(client, system_path, emitter, step, placeholders.setdefault(step["script"], {})),
        "module": lambda step: toolset.add_module(client, system_path, emitter, step)[0],
        "expression_module": lambda step: toolset.add_expression_module(client, system_path, emitter, step),
        "linked_inputs_module": lambda step: toolset.add_linked_inputs_module(client, system_path, emitter, step),
        "state_enums": lambda step: toolset.set_state_enums(client, system_path, emitter, step, state_report),
        "sample": lambda step: toolset.add_sample_texture(client, system_path, emitter, step, drive.SAMPLE_TEXTURE_MODULE, binding_report),
    }
    module_names = {}
    for step in drive.build_stack_plan(spec):
        module_names[step["label"]] = recorder.run(f"4 {step['script']} {step['label']}", lambda step=step: builders[step["kind"]](step))
        if step["kind"] == "sample" and module_names[step["label"]]:
            placeholders.setdefault(step["script"], {})[step["placeholder"]] = toolset.sampled_color_attribute(module_names[step["label"]])
    recorder.run("5 렌더러", lambda: toolset.replace_renderer(client, system_path, emitter, drive.renderer_class_of(spec), drive.build_renderer_data(spec)))
    return {"template_topology": topology, "removed_template_emitters": removed, "module_names": module_names,
            "placeholders": placeholders, "binding": binding_report, "state": state_report}


def record_fake_calls(spec):
    client = toolset.TDFakeNiagaraToolsetClient()
    recorder = toolset.TDStepRecorder(strict=True)
    build_system(client, spec["asset"], spec, None, recorder, drive.DEFAULT_TEMPLATE_SYSTEM)
    return client.records


def verify_system(spec, dump, raw_emitter_data, raw_system_data, built):
    sections = dump["sections"]
    emitter = spec["emitter_name"]
    plan = drive.build_stack_plan(spec)
    statuses = toolset.collect_compile_statuses(sections.get("CompileState"))
    user_names = [str(toolset.field(item, "name")) for item in toolset.as_list(sections.get("UserVariables"), "userVariables") if isinstance(item, dict)]
    renderer_texts = [json.dumps(value) for key, value in sections.items() if ".Renderer." in key]
    emitter_names = [str(toolset.field(item, "emitterName")) for item in toolset.as_list(sections.get("SystemSummary"), "emitters") if isinstance(item, dict)]
    sample_count = drive.count_sample_steps(plan)
    checks = {
        f"이미터 = {emitter} 하나": emitter_names == [emitter],
        "이미터 데이터 재읽기 = 요청(GPU·공간·고정 경계·결정론·시드·영속 ID·FixedCount)": toolset.values_match(toolset.read_property_values(raw_emitter_data), drive.build_emitter_data(spec)),
        "시스템 데이터 재읽기 = 요청(결정론·시드·그림자)": toolset.values_match(toolset.read_property_values(raw_system_data), drive.build_system_data(spec)),
        "GPU 계산 스크립트 UpToDate": statuses.get(f"{emitter}.{drive.GPU_COMPUTE_SCRIPT}") in toolset.UP_TO_DATE_STATUSES,
        "컴파일 오류 없음": toolset.find_first(sections.get("CompileState"), "hasErrors") is False,
        "스택 오류 0": toolset.count_error_issues(sections.get("StackIssues")) == 0,
        f"User 변수 수 = {len(drive.build_user_variable_plan(spec))}": len(user_names) == len(drive.build_user_variable_plan(spec)),
        f"SampleTexture 바인딩 왕복 {sample_count}개": toolset.is_binding_roundtrip_ok(built["binding"], sample_count),
        "렌더러·메시·재질 재읽기": any(all(path in text for path in drive.expected_renderer_paths(spec)) for text in renderer_texts),
    }
    state_count = drive.count_state_enums(plan)
    if state_count:
        checks[f"EmitterState 열거 재읽기 {state_count}개(Self·Once)"] = toolset.is_state_roundtrip_ok(built["state"], state_count)
    return [{"check": name, "passed": bool(passed)} for name, passed in checks.items()], statuses


def read_probe():
    if not PROBE_PATH.is_file():
        return {}
    return json.loads(PROBE_PATH.read_text(encoding="utf-8"))


def require_stage0b(spec):
    stage = (read_probe().get("stage0b") or {}).get(toolset.asset_name(spec["asset"])) or {}
    if "template_system" not in stage:
        raise toolset.TDPreconditionError(f"단계 0b 결과 없음({spec['asset']}): --probe --asset {REPRO_ASSET_ROOT}<새 이름> 를 먼저 실행")
    if stage.get("failed_steps") or not stage.get("binding_roundtrip_ok") or not stage.get("state_roundtrip_ok", True):
        raise toolset.TDPreconditionError(f"단계 0b 미통과(failed_steps={stage.get('failed_steps')}): 계획을 고치고 새 경로로 0b 재실행")
    return stage["template_system"]


def validate_asset_path(spec, path):
    if path == spec["asset"]:
        return
    if not path.startswith(REPRO_ASSET_ROOT) or path.endswith("/") or "." in toolset.asset_name(path):
        raise toolset.TDPreconditionError(f"--asset 은 명세 asset 이거나 {REPRO_ASSET_ROOT} 아래 새 패키지 경로여야 함: {path}")


def run_stage0a(client):
    description = client.describe_toolset(client.system_toolset)
    tools = description.get("tools", []) if isinstance(description, dict) else []
    results = {}
    for label, asset in (("module_sample_texture", drive.SAMPLE_TEXTURE_MODULE), ("module_spawn_rate", drive.SPAWN_RATE_MODULE),
                         ("module_spawn_burst", drive.SPAWN_BURST_MODULE), ("module_solve_forces", drive.SOLVE_FORCES_MODULE),
                         ("module_collision", drive.COLLISION_MODULE), ("module_initialize_particle", drive.INITIALIZE_PARTICLE_MODULE)):
        toolset.probe_call(client, label, "GetModuleSchemaFromAsset", {"moduleAsset": toolset.reference(asset)}, results)
    for label, asset in (("dynamic_random_vector2d", drive.RANDOM_RANGE_VECTOR2D), ("dynamic_random_float", drive.RANDOM_RANGE_FLOAT)):
        toolset.probe_call(client, label, "GetDynamicInputSchemaFromAsset", {"dynamicInputAsset": toolset.reference(asset)}, results)
    toolset.probe_call(client, "data_interface_texture", "GetDataInterfaceSchema", {"dataInterfaceClass": toolset.reference(drive.TEXTURE_DATA_INTERFACE_CLASS)}, results)
    toolset.probe_call(client, "emitter_schema", "GetEmitterSchema", {}, results)
    toolset.probe_call(client, "system_schema", "GetSystemSchema", {}, results)
    for label, renderer_class in (("sprite_renderer_schema", drive.SPRITE_RENDERER_CLASS), ("mesh_renderer_schema", drive.MESH_RENDERER_CLASS)):
        toolset.probe_call(client, label, "GetRendererSchema", {"rendererClass": toolset.reference(renderer_class)}, results)
    return {
        "toolsets": client.toolset_names,
        "system_toolset": client.system_toolset,
        "system_tools": sorted(str(tool.get("name", "")).rsplit(".", 1)[-1] for tool in tools),
        "save_assets_tool": toolset.find_save_tool(client),
        "editor_remote_exec": toolset.read_marker(toolset.run_editor_code("print('TD_PING', 1)\n"), "TD_PING") == "1",
        "calls": results,
        "failed_calls": sorted(label for label, item in results.items() if not item["ok"]),
    }


def run_stage0b(client, spec, probe_asset, user_defaults):
    if toolset.editor_asset_exists(probe_asset):
        raise toolset.TDPreconditionError(f"0b 경로에 에셋이 이미 있음(덮어쓰기 안 함): {probe_asset}")
    template = drive.EMPTY_TEMPLATE_SYSTEM
    empty_template_error = ""
    try:
        toolset.create_system(client, probe_asset, drive.EMPTY_TEMPLATE_SYSTEM)
    except toolset.TDNiagaraToolError as error:
        empty_template_error = str(error)
        template = drive.DEFAULT_TEMPLATE_SYSTEM
        if toolset.editor_asset_exists(probe_asset):
            raise toolset.TDPreconditionError(f"빈 템플릿 실패 뒤 에셋이 남음, 새 경로로 다시: {probe_asset}")
    recorder = toolset.TDStepRecorder(strict=False)
    built = build_system(client, probe_asset, spec, user_defaults, recorder, template, should_create=bool(empty_template_error))
    recorder.run("6 저장", lambda: toolset.editor_save_asset(probe_asset))
    emitter_data = toolset.call_for_section(client, "GetEmitterData", {"emitterRef": toolset.stack_ref(probe_asset, spec["emitter_name"])})
    system_data = toolset.call_for_section(client, "GetSystemData", {"system": toolset.reference(toolset.object_path(probe_asset))})
    dump = toolset.dump_system(client, probe_asset, spec["emitter_name"], OUTPUT_DIR, TOOL_NAME)
    checks, statuses = verify_system(spec, dump, emitter_data, system_data, built)
    plan = drive.build_stack_plan(spec)
    return {
        "spec_asset": spec["asset"],
        "asset": probe_asset,
        "template_system": template,
        "empty_template_error": empty_template_error,
        "steps": recorder.steps,
        "failed_steps": recorder.failed_steps(),
        "binding_roundtrip_ok": toolset.is_binding_roundtrip_ok(built["binding"], drive.count_sample_steps(plan)),
        "state_roundtrip_ok": toolset.is_state_roundtrip_ok(built["state"], drive.count_state_enums(plan)),
        "checks": checks,
        "compile_statuses": statuses,
        "built": built,
        "sha256": dump["sha256"],
    }


def run_probe(client, spec, probe_asset, user_defaults):
    probe = read_probe()
    probe["stage0a"] = run_stage0a(client)
    toolset.write_json(PROBE_PATH, probe)
    stage0b = None
    if probe_asset:
        stage0b = run_stage0b(client, spec, probe_asset, user_defaults)
        probe.setdefault("stage0b", {})[toolset.asset_name(spec["asset"])] = stage0b
        toolset.write_json(PROBE_PATH, probe)
    print("프로브: " + str(PROBE_PATH))
    passed = not probe["stage0a"]["failed_calls"] and (
        stage0b is None or (not stage0b["failed_steps"] and stage0b["binding_roundtrip_ok"] and stage0b["state_roundtrip_ok"]))
    print(json.dumps({"stage0a_failed_calls": probe["stage0a"]["failed_calls"],
                      "stage0b_failed_steps": stage0b["failed_steps"] if stage0b else None,
                      "binding_roundtrip_ok": stage0b["binding_roundtrip_ok"] if stage0b else None,
                      "state_roundtrip_ok": stage0b["state_roundtrip_ok"] if stage0b else None}, ensure_ascii=False))
    return 0 if passed else 1


def run_create(client, spec, system_path, user_defaults, look_path):
    template = require_stage0b(spec)
    if toolset.editor_asset_exists(system_path):
        raise toolset.TDPreconditionError(f"에셋이 이미 있음(덮어쓰기 안 함, 새 경로 사용): {system_path}")
    material = spec["renderer"]["material"]
    if not toolset.editor_asset_exists(material):
        raise toolset.TDPreconditionError(f"재질 없음: {material}(editor_make_fluid_particle_material.py 먼저)")
    recorder = toolset.TDStepRecorder(strict=True)
    report = {"asset": system_path, "spec_asset": spec["asset"], "template_system": template, "look_path": str(look_path),
              "look_sha256": hashlib.sha256(pathlib.Path(look_path).read_bytes()).hexdigest(), "user_defaults": user_defaults, "checks": []}
    try:
        built = build_system(client, system_path, spec, user_defaults, recorder, template)
        recorder.run("6 저장", lambda: toolset.editor_save_asset(system_path))
        emitter_data = recorder.run("7 이미터 데이터 재읽기", lambda: client.call("GetEmitterData", emitterRef=toolset.stack_ref(system_path, spec["emitter_name"])))
        system_data = recorder.run("7 시스템 데이터 재읽기", lambda: client.call("GetSystemData", system=toolset.reference(toolset.object_path(system_path))))
        dump = toolset.dump_system(client, system_path, spec["emitter_name"], OUTPUT_DIR, TOOL_NAME)
        checks, statuses = verify_system(spec, dump, emitter_data, system_data, built)
        report.update({"built": built, "checks": checks, "compile_statuses": statuses, "sha256": dump["sha256"], "sha256_all": dump["sha256_all"]})
    finally:
        report["steps"] = recorder.steps
        toolset.write_json(OUTPUT_DIR / f"{toolset.asset_name(system_path)}.report.json", report)
    for check in report["checks"]:
        print(("PASS " if check["passed"] else "FAIL ") + check["check"])
    return 0 if report["checks"] and all(check["passed"] for check in report["checks"]) else 1


def write_fake_calls(spec, output_path):
    path = pathlib.Path(output_path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(toolset.format_call_records(record_fake_calls(spec)), encoding="utf-8")
    print("가짜 호출 기록: " + str(path))
    return 0


def parse_arguments(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--spec", required=True)
    parser.add_argument("--asset", default="")
    parser.add_argument("--look", default="")
    parser.add_argument("--probe", action="store_true")
    parser.add_argument("--dump-only", action="store_true")
    parser.add_argument("--fake-calls", default="")
    arguments = parser.parse_args(argv)
    if sum(1 for flag in (arguments.probe, arguments.dump_only, bool(arguments.fake_calls)) if flag) > 1:
        parser.error("--probe·--dump-only·--fake-calls 는 함께 쓸 수 없음")
    return arguments


def main(argv=None):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    arguments = parse_arguments(argv)
    try:
        spec = load_valid_spec(arguments.spec)
        if arguments.fake_calls:
            return write_fake_calls(spec, arguments.fake_calls)
        system_path = arguments.asset or ("" if arguments.probe else spec["asset"])
        if system_path:
            validate_asset_path(spec, system_path)
        look_path = resolve_look_path(arguments.look)
        user_defaults = None if arguments.dump_only else read_user_defaults(spec, look_path)
        client = toolset.TDNiagaraToolsetClient()
        if arguments.probe:
            return run_probe(client, spec, system_path, user_defaults)
        if arguments.dump_only:
            toolset.dump_system(client, system_path, spec["emitter_name"], OUTPUT_DIR, TOOL_NAME)
            return 0
        return run_create(client, spec, system_path, user_defaults, look_path)
    except toolset.TDEditorConnectionError as error:
        print("연결 실패: " + str(error))
        return 2
    except toolset.TDPreconditionError as error:
        print("선행 조건: " + str(error))
        return 3
    except toolset.TDNiagaraToolError as error:
        print("실패: " + str(error))
        return 1


if __name__ == "__main__":
    sys.exit(main())
