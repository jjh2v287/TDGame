"""실행 환경: 시스템 Python 3(에디터 없이). 유체 구동 나이아가라 구조 명세(Tools/FluidFX/niagara_specs/NS_TD_*.json)와 가짜 호출 기록 정답(golden/*.calls.json)·모양 항목(TDFluidMaterialLook.json NiagaraSystems)을 오프라인으로 검사한다(spec-fx8n 2.5 ①~⑥, 반박 M1 반영: 런타임 이름은 기준선 굽기 8개만 모양에 허용). ⑥ 은 C++ 가 넣는 User 이름과 런타임 목록(drive.all_runtime_user_variables = 기준 12 ∪ water ∪ rain)을 양방향 대조하고 모양 검사의 런타임 이름 집합은 C++ 집합 ∪ 그 목록이다. ⑦ 은 엔진 ProcessCustomHlsl(NiagaraHlslTranslator.cpp:8687-8693·9161-9175) 병렬 배열 어긋남 모의로, 식마다 원래 별칭 기준 고유 변수 수가 실제 고유 변수 수보다 적으면(L-editor-62) 외부 상수(User·Engine·System·Emitter) 누락을 보고한다. ⑧ 은 water 명세가 파싱하는 HLSL 상수(파 배열 4×8·스칼라 3·지형 2, fluid_niagara_water.read_wave_constants)를 명세 폴더 아래 사본 우선으로 읽어 성공하는지 본다.
실행 명령: python -B Tools/FluidFX/check_fluid_niagara_specs.py [--root <명세 폴더>] [--look <모양 JSON>] [--update-golden] | --fixtures Tools/FluidFX/niagara_specs/fixtures (병합 전에는 --look <병합 사본>)
출력: 표준 출력 ERROR [코드] ①~⑧ 줄과 SUMMARY. 코드 spec·golden·look_missing·look_type·look_range·look_extra·runtime_name·token·runtime_list·alias_drop·wave_constants. --fixtures 는 하위 폴더마다 expect.json 의 코드가 모두 검출되는지 PASS·FAIL. 종료 0 = 통과(또는 fixture 전부 검출), 1 = 위반, 2 = 입력 오류
상태: 2026-10-29 ⑥ 확장형(water·rain 런타임 목록)·⑧ 추가(W-1b, fixture water_* 6종 오프라인 확인; C++ 런타임 이름이 들어오기 전에는 ⑥ 이 water·rain 이름 줄을 낸다). 2026-10-26 ⑦ 추가(실제 트리 7종 + fixture 10종 오프라인 실행, 옛 생성 덤프 6종의 관측 GPU 누락과 대조). 에디터 생성 7종의 GPU 로그 문제 줄 0으로 ⑦ 규칙 확인(2026-10-26).
"""
import argparse
import json
import pathlib
import re
import sys

TOOLS_ROOT = pathlib.Path(__file__).resolve().parents[1]
PROJECT_ROOT = TOOLS_ROOT.parent
sys.path.insert(0, str(TOOLS_ROOT))
sys.path.insert(0, str(TOOLS_ROOT / "FluidFX"))
import check_fluid_preset_data
import editor_make_fluid_driven_niagara as generator
import fluid_material_look
import fluid_niagara_drive as drive
import fluid_niagara_toolset as toolset

DEFAULT_ROOT = TOOLS_ROOT / "FluidFX" / "niagara_specs"
SPEC_GLOB = "NS_TD_*.json"
GOLDEN_FOLDER = "golden"
FIXTURE_LOOK_NAME = "look.json"
FIXTURE_EXPECT_NAME = "expect.json"
GOLDEN_SUFFIX = ".calls.json"
CUSTOM_HLSL_SEPARATORS = frozenset("!;/*+-=)(?:, []<>\"\t\r\n{}")
READ_NAMESPACES = ("User", "Engine", "Particles", "System", "Emitter")
EXTERNAL_NAMESPACES = ("User", "Engine", "System", "Emitter")
SWIZZLE_PATTERN = re.compile(r"^(?:[xyzw]{1,4}|[rgba]{1,4})$")


class TDCheckInputError(Exception):
    pass


class TDFindings:
    def __init__(self):
        self.items = []

    def add(self, code, rule, message):
        line = (code, rule, message)
        if line not in self.items:
            self.items.append(line)

    def codes(self):
        return {code for code, _, _ in self.items}


def load_spec(path, findings):
    try:
        spec = drive.read_spec(path)
    except (OSError, ValueError) as error:
        findings.add("spec", "①", f"{path.name}: 읽기 실패 {error}")
        return None
    problems = drive.find_spec_problems(spec)
    for problem in problems:
        findings.add("spec", "①", f"{path.name}: {problem}")
    return None if problems else spec


def is_in_range(type_path, value, low, high):
    if type_path == toolset.INT_TYPE:
        return isinstance(value, int) and not isinstance(value, bool) and low <= value <= high
    components = value if isinstance(value, list) else [value]
    return all(drive.is_number(component) and low <= component <= high for component in components)


def check_look_entry(spec, look, runtime_names, findings):
    asset = spec["asset"]
    entry = look.get("NiagaraSystems", {}).get(asset)
    if not isinstance(entry, dict):
        findings.add("look_missing", "③", f"{asset}: 모양 항목 없음")
        return
    sections = {section: entry.get(section) if isinstance(entry.get(section), dict) else {} for section in fluid_material_look.NIAGARA_SECTIONS}
    ranges = {name: (type_path, low, high) for name, type_path, low, high in drive.select_tuning_variables(spec)}
    required = drive.required_look_names(spec)
    for name, type_path in required:
        section_name = drive.LOOK_SECTION_BY_TYPE[type_path]
        if name not in sections[section_name]:
            other = [section for section, values in sections.items() if name in values]
            if other:
                findings.add("look_type", "③", f"{asset}: {name} 은 {section_name} 이어야 하는데 {', '.join(other)} 에 있음")
            else:
                findings.add("look_missing", "③", f"{asset}: {section_name}.{name} 없음")
            continue
        if name in ranges and not is_in_range(type_path, sections[section_name][name], ranges[name][1], ranges[name][2]):
            findings.add("look_range", "③", f"{asset}: {name} = {sections[section_name][name]!r} 가 범위 [{ranges[name][1]}, {ranges[name][2]}] 밖")
    known = {name for name, _ in required}
    baseline = {name.casefold() for name in check_fluid_preset_data.BASELINE_BAKED_RUNTIME_NAMES}
    for section_name, values in sections.items():
        for name in values:
            folded = name.casefold()
            if folded in runtime_names and folded not in baseline:
                findings.add("runtime_name", "④", f"{asset}: {section_name}.{name} 는 C++ 런타임 이름({runtime_names[folded]})이고 기준선 굽기 8개 밖(R7)")
                continue
            if name not in known:
                findings.add("look_extra", "③", f"{asset}: 등록표에 없는 남는 이름 {section_name}.{name}")


def collect_expressions(records):
    expressions = []
    for record in records:
        if not isinstance(record, dict) or record.get("tool") != "SetStackInputData":
            continue
        value = ((record.get("arguments") or {}).get("inputData") or {}).get("value")
        if isinstance(value, dict) and isinstance(value.get("hlslExpression"), str):
            expressions.append(value["hlslExpression"])
    return expressions


def split_custom_hlsl_tokens(expression):
    tokens, current = [], ""
    for character in expression:
        if character not in CUSTOM_HLSL_SEPARATORS:
            current += character
            continue
        if current:
            tokens.append(current)
        current = ""
    if current:
        tokens.append(current)
    return tokens


def parameter_of(token):
    parts = token.split(".")
    if len(parts) >= 3 and not SWIZZLE_PATTERN.match(parts[2]):
        return ".".join(parts[:3])
    return ".".join(parts[:2])


def alias_intact_of(parameter):
    parts = parameter.split(".")
    if len(parts) == 3 and parts[0] != "Engine":
        return f"{parts[0]}.Module.{parts[2]}"
    return parameter


def find_alias_drops(expression):
    actual, intact = [], []
    for token in split_custom_hlsl_tokens(expression):
        if "." not in token or token.split(".")[0] not in READ_NAMESPACES:
            continue
        parameter = parameter_of(token)
        if alias_intact_of(parameter) not in intact:
            intact.append(alias_intact_of(parameter))
        if parameter not in actual:
            actual.append(parameter)
    if len(actual) == len(intact):
        return None
    return [name for name in actual[len(intact):] if name.split(".")[0] in EXTERNAL_NAMESPACES]


def check_alias_drops(spec, expressions, label, findings):
    for expression in expressions:
        dropped = find_alias_drops(expression)
        if dropped is None:
            continue
        findings.add("alias_drop", "⑦", f"{spec['asset']} {label}: 원래 별칭이 겹쳐 등록 안 되는 외부 상수 {dropped or '없음(짝 어긋남만)'} ← {expression[:160]}")


def check_tokens(spec, records, label, findings):
    plan = drive.build_stack_plan(spec)
    user_names = {name for name, _ in drive.build_user_variable_plan(spec)}
    particle_names = drive.collect_declared_particle_names(plan)
    expressions = collect_expressions(records)
    for expression in expressions:
        for problem in drive.find_token_problems(expression, user_names, particle_names):
            findings.add("token", "⑤", f"{spec['asset']} {label}: {problem}")
    check_alias_drops(spec, expressions, label, findings)


def read_golden(path, findings, spec):
    try:
        text = path.read_text(encoding="utf-8")
        return text, json.loads(text)
    except (OSError, ValueError) as error:
        findings.add("golden", "②", f"{spec['asset']}: 정답 기록 읽기 실패 {path.name}: {error}")
        return None, None


def check_golden(spec, spec_path, golden_folder, update_golden, findings):
    records = generator.record_fake_calls(spec)
    text = toolset.format_call_records(records)
    check_tokens(spec, records, "생성 기록", findings)
    if golden_folder is None:
        return
    golden_path = golden_folder / (spec_path.stem + GOLDEN_SUFFIX)
    if update_golden:
        golden_folder.mkdir(parents=True, exist_ok=True)
        golden_path.write_text(text, encoding="utf-8")
        return
    if not golden_path.is_file():
        findings.add("golden", "②", f"{spec['asset']}: 정답 기록 없음 {golden_path.name}(--update-golden)")
        return
    golden_text, golden_records = read_golden(golden_path, findings, spec)
    if golden_text is None:
        return
    check_tokens(spec, golden_records, "정답 기록", findings)
    if golden_text != text:
        findings.add("golden", "②", f"{spec['asset']}: 가짜 호출 기록이 정답 {golden_path.name} 와 다름(구조 변경이면 검토 뒤 --update-golden)")


def declared_runtime_names():
    return {name.casefold(): name for name, _ in drive.all_runtime_user_variables()}


def check_runtime_list(runtime_names, findings):
    declared = declared_runtime_names()
    for folded, location in sorted(runtime_names.items()):
        if folded not in declared:
            findings.add("runtime_list", "⑥", f"C++ 가 넣는 User 이름 {folded}({location}) 이 런타임 목록(기준 12 ∪ water ∪ rain)에 없음")
    for folded, name in sorted(declared.items()):
        if folded not in runtime_names:
            findings.add("runtime_list", "⑥", f"런타임 목록의 {name} 을 C++ 가 넣지 않음")


def merge_registry_runtime_names(runtime_names):
    merged = dict(runtime_names)
    for folded in declared_runtime_names():
        merged.setdefault(folded, "런타임 등록표")
    return merged


def check_wave_constants(spec, root, findings):
    if spec["profile"] != "water":
        return True
    module = drive.profile_module(spec)
    try:
        module.read_wave_constants(root)
    except module.TDWaterConstantsError as error:
        findings.add("wave_constants", "⑧", f"{spec['asset']}: {error}")
        return False
    return True


def read_look(path):
    try:
        return json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise TDCheckInputError(f"모양 JSON 읽기 실패 {path}: {error}") from error


def resolve_root_look(root, look_argument):
    fixture_look = root / FIXTURE_LOOK_NAME
    if fixture_look.is_file():
        return fixture_look
    if look_argument:
        return pathlib.Path(look_argument).resolve()
    return pathlib.Path(fluid_material_look.default_look_path(str(PROJECT_ROOT)))


def check_root(root, look_argument, update_golden, requires_golden):
    if not root.is_dir():
        raise TDCheckInputError(f"명세 폴더 없음: {root}")
    spec_paths = sorted(root.glob(SPEC_GLOB))
    if not spec_paths:
        raise TDCheckInputError(f"명세 없음: {root}/{SPEC_GLOB}")
    look = read_look(resolve_root_look(root, look_argument))
    _, runtime_names = check_fluid_preset_data.collect_runtime_parameter_names(PROJECT_ROOT)
    golden_folder = root / GOLDEN_FOLDER
    if not golden_folder.is_dir() and not requires_golden and not update_golden:
        golden_folder = None
    findings = TDFindings()
    check_runtime_list(runtime_names, findings)
    look_runtime_names = merge_registry_runtime_names(runtime_names)
    for spec_path in spec_paths:
        spec = load_spec(spec_path, findings)
        if spec is None:
            continue
        if check_wave_constants(spec, root, findings):
            check_golden(spec, spec_path, golden_folder, update_golden, findings)
        check_look_entry(spec, look, look_runtime_names, findings)
    return findings, len(spec_paths)


def run_fixtures(fixtures_root):
    if not fixtures_root.is_dir():
        raise TDCheckInputError(f"fixture 폴더 없음: {fixtures_root}")
    cases = sorted(path for path in fixtures_root.iterdir() if path.is_dir())
    if not cases:
        raise TDCheckInputError(f"fixture 없음: {fixtures_root}")
    failed = 0
    for case in cases:
        expected = set(json.loads((case / FIXTURE_EXPECT_NAME).read_text(encoding="utf-8"))["codes"])
        findings, _ = check_root(case, None, False, False)
        detected = findings.codes()
        passed = bool(expected) and expected <= detected
        failed += 0 if passed else 1
        print(f"{'PASS' if passed else 'FAIL'} {case.name}: 기대 {sorted(expected)} 검출 {sorted(detected)}")
    print(f"SUMMARY fixtures={len(cases)} failed={failed}")
    return 1 if failed else 0


def main(argv=None):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=str(DEFAULT_ROOT))
    parser.add_argument("--look", default="")
    parser.add_argument("--update-golden", action="store_true")
    parser.add_argument("--fixtures", default="")
    arguments = parser.parse_args(argv)
    try:
        if arguments.fixtures:
            return run_fixtures(pathlib.Path(arguments.fixtures).resolve())
        findings, spec_count = check_root(pathlib.Path(arguments.root).resolve(), arguments.look, arguments.update_golden, True)
    except (TDCheckInputError, toolset.TDNiagaraToolError) as error:
        print(f"INPUT {error}")
        print("SUMMARY specs=0 errors=0 input_error=1")
        return 2
    for code, rule, message in findings.items:
        print(f"ERROR [{code}] {rule} {message}")
    print(f"SUMMARY specs={spec_count} errors={len(findings.items)} input_error=0{' golden_updated=1' if arguments.update_golden else ''}")
    return 1 if findings.items else 0


if __name__ == "__main__":
    sys.exit(main())
