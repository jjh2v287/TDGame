"""실행 환경: 시스템 Python 3.12(표준 라이브러리만, 에디터·빌드 불필요), fxp_audit.py golden 으로 실행(p_plans.py·check_fluid_preset_data.py import만, PD-8).
실행 명령: python -B Tools/FluidFX/fxp/fxp_audit.py golden --out Saved/FluidFX/fxp/golden.json --md Saved/FluidFX/fxp/golden.md [--root <저장소 사본>].
출력: 골든 감사 줄(runbook-fxp-v3 4.6·PD-14, 입력 PC 6절): expect 채택마다 형식(FluidFX 헤더 UPROPERTY·Look 구획표·고정 엔진 속성표)·골든 경로(스냅숏 구조체 ∧ 골든 시나리오 종류)·overlay 현재 값, 열거·불·정수 채택 목록, 일지 최종 채택(PD-6, 계획 줄기별) ↔ expect 채택·overlay 마지막 제거 대조. 종료 1 = 형식 미확인·일지 채택이 expect·overlay 제거에 없음·expect 채택이 overlay 현재 값에 없음.
상태: 2026-11-07 FX-P X1b(B2) 구현, fixture fixtures_golden.py(형식 6종·골든 경로·일지 대조·overlay 없음·같은 바이트·입력 없음).
"""
import re

import common

GOLDEN_TEST_SOURCE = "Source/TDGame/FluidFX/Tests/TDFluidSolverGoldenHashTests.cpp"
SNAPSHOT_GENERATOR = "Tools/FluidFX/make_fluid_golden_snapshots.py"
HEADER_GLOB = "Source/TDGame/FluidFX/*.h"
TUNING_PATH = "Content/FluidFX/PresetData/TDFluidPresetTuning.json"
PRESET_SCHEMA_TOOL = "check_fluid_preset_data"
TEST_NAME_PATTERN = re.compile(r'IMPLEMENT_SIMPLE_AUTOMATION_TEST\(\s*\w+\s*,\s*"([^"]+)"')
SCENARIO_BLOCK_PATTERN = re.compile(r"GoldenScenarios\[\]\s*=\s*\{(.*?)\n\s*\};", re.DOTALL)
SCENARIO_KIND_PATTERN = re.compile(r"ETDFluidPresetKind::(\w+)")
DEFAULT_STRUCTS_PATTERN = re.compile(r"^DEFAULT_STRUCTS\s*=\s*\(([^)]*)\)", re.MULTILINE)
QUOTED_NAME_PATTERN = re.compile(r'"(\w+)"')
INTEGER_TYPE_PATTERN = re.compile(r"u?int(8|16|32|64)")
TYPE_FLOAT, TYPE_INTEGER, TYPE_BOOL, TYPE_ENUM, TYPE_VECTOR, TYPE_UNKNOWN = "실수", "정수", "불", "열거", "벡터", "미확인"
TYPE_ORDER = (TYPE_FLOAT, TYPE_INTEGER, TYPE_BOOL, TYPE_ENUM, TYPE_VECTOR, TYPE_UNKNOWN)
DISCRETE_TYPES = (TYPE_ENUM, TYPE_BOOL, TYPE_INTEGER)
LOOK_VALUE_TYPES = {"float": TYPE_FLOAT, "int32": TYPE_INTEGER, "bool": TYPE_BOOL}
LOOK_PREFIX = "Look:"
ACTOR_PROP_PREFIX = "actor_prop:"
ACTOR_PROP_FIELDS = ("level", "label", "component", "property")
ACTOR_PROPERTY_TYPES = {("light_component", "volumetric_scattering_intensity"):
                        (TYPE_FLOAT, "엔진 고정표: UE 5.8 Engine/Source/Runtime/Engine/Classes/Components/LightComponentBase.h:120 float VolumetricScatteringIntensity")}
TUNING_SECTIONS_WITH_KIND = {"Kinds": 4, "Groups": 4}
TUNING_SECTIONS_ALL_KINDS = {"Globals": 3, "StructDefaults": 3}
WRITE_OPERATIONS = ("add", "replace")
REMOVE_OPERATION = "remove"
JOURNAL_ADOPTED = "adopted"
GOLDEN_LINE_TEMPLATE = "채택된 열거·불·정수 분기는 골든이 덮지 않는다(RP-S1): 채택 {n}(W11 overlay 채택, 영향 {influences} 제외) 중 열거·불·정수 {k}, 골든 경로 {m}; CPU 솔버 {s}시나리오 회귀만 보증"
TYPE_RULE = ("형식 = 대상 필드의 선언 형식(값 리터럴 아님): 조절 경로는 Source/TDGame/FluidFX/*.h 의 USTRUCT·UPROPERTY(check_fluid_preset_data 정규식, 상위 구조체 포함), "
             "Look 은 check_fluid_preset_data LOOK_MATERIAL_SECTIONS·LOOK_NIAGARA_SECTIONS 구획표, actor_prop 은 고정 엔진 속성표; 모르면 미확인(종료 1)")
GOLDEN_PATH_RULE = "골든 경로 = 조절 경로 ∧ 구조체 ∈ 스냅숏 생성기 DEFAULT_STRUCTS ∧ 영향 종류(Kinds 1·Groups 묶음·Globals·StructDefaults 전체) ∩ 골든 시나리오 종류 ≠ ∅"
JOURNAL_RULE = "일지 대조(PD-14): 일지 최종(PD-6, 계획 줄기별) adopted 마다 overlay history add·replace(from.plan 줄기 같음) 키가 expect 채택 entry_id 이거나 overlay 마지막 연산이 remove 여야 한다"


def preset_schema_tool():
    return common.load_tool_module("Tools", PRESET_SCHEMA_TOOL)


def read_golden_scenarios(reads):
    text = reads.read_text(GOLDEN_TEST_SOURCE).replace("\r\n", "\n")
    test_name = TEST_NAME_PATTERN.search(text)
    block = SCENARIO_BLOCK_PATTERN.search(text)
    kinds = SCENARIO_KIND_PATTERN.findall(block.group(1)) if block else []
    if test_name is None or not kinds:
        raise common.InputError(f"{GOLDEN_TEST_SOURCE}: IMPLEMENT_SIMPLE_AUTOMATION_TEST 이름·GoldenScenarios[] 종류를 못 읽음")
    return {"test": test_name.group(1), "source": GOLDEN_TEST_SOURCE, "scenario_kinds": kinds, "scenarios": len(kinds)}


def read_snapshot_structs(reads):
    match = DEFAULT_STRUCTS_PATTERN.search(reads.read_text(SNAPSHOT_GENERATOR).replace("\r\n", "\n"))
    names = QUOTED_NAME_PATTERN.findall(match.group(1)) if match else []
    if not names:
        raise common.InputError(f"{SNAPSHOT_GENERATOR}: DEFAULT_STRUCTS 를 못 읽음")
    return names


def line_number(text, offset):
    return text.count("\n", 0, offset) + 1


def read_header_schema(reads, tool):
    enums, structs = {}, {}
    paths = reads.glob(HEADER_GLOB)
    if not paths:
        raise common.InputError(f"헤더 없음: {HEADER_GLOB}")
    for path in paths:
        text = reads.read_text(path).replace("\r\n", "\n")
        for match in tool.ENUM_PATTERN.finditer(text):
            enums[match.group(1)] = tool.parse_enum_names(match.group(2))
        for match in tool.STRUCT_PATTERN.finditer(text):
            body_start = match.start(3)
            fields = [{"name": prop.group(3), "type": prop.group(2), "file": path, "line": line_number(text, body_start + prop.start(2))}
                      for prop in tool.PROPERTY_PATTERN.finditer(match.group(3))]
            structs[match.group(1)] = (match.group(2) or None, fields)
    return enums, structs


def find_field(structs, struct_name, field_name, chain=()):
    if struct_name not in structs or struct_name in chain:
        return None
    base, fields = structs[struct_name]
    found = next((field for field in fields if field["name"] == field_name), None)
    if found is not None or not base:
        return found
    return find_field(structs, base, field_name, chain + (struct_name,))


def classify_cpp_type(type_name, enums, tool):
    if type_name == "bool":
        return TYPE_BOOL
    if type_name in ("float", "double"):
        return TYPE_FLOAT
    if INTEGER_TYPE_PATTERN.fullmatch(type_name):
        return TYPE_INTEGER
    enum_as_byte = tool.ENUM_AS_BYTE_TYPE_PATTERN.fullmatch(type_name)
    if type_name in enums or (enum_as_byte and enum_as_byte.group(1) in enums):
        return TYPE_ENUM
    if type_name in tool.COMPONENT_TYPES:
        return TYPE_VECTOR
    return TYPE_UNKNOWN


def unknown_form(form, detail):
    return {"form": form, "type_class": TYPE_UNKNOWN, "type_source": detail, "kinds": [], "golden_path": False}


class TypeResolver:
    def __init__(self, reads, golden, snapshot_structs):
        self.reads = reads
        self.tool = preset_schema_tool()
        self.enums, self.structs = read_header_schema(reads, self.tool)
        self.all_kinds = list(self.enums.get(self.tool.PRESET_KIND_ENUM, []))
        self.golden_kinds = set(golden["scenario_kinds"])
        self.snapshot_structs = set(snapshot_structs)
        self.groups = None

    def group_kinds(self, group):
        if self.groups is None:
            tuning = self.reads.read_json(TUNING_PATH)
            groups = tuning.get("Groups") if isinstance(tuning, dict) else None
            self.groups = groups if isinstance(groups, dict) else {}
        entry = self.groups.get(group)
        kinds = entry.get("Kinds") if isinstance(entry, dict) else None
        return [kind for kind in kinds if common.is_text(kind)] if isinstance(kinds, list) else None

    def describe(self, entry_id):
        if entry_id.startswith(LOOK_PREFIX):
            return self.describe_look(entry_id)
        if entry_id.startswith(ACTOR_PROP_PREFIX):
            return self.describe_actor_prop(entry_id)
        return self.describe_tuning(entry_id)

    def describe_look(self, entry_id):
        parts = entry_id[len(LOOK_PREFIX):].split(".", 3)
        if len(parts) != 4 or not all(parts) or not parts[1].startswith("/Game/"):
            return unknown_form("Look", "Look:<뿌리>.<패키지>.<구획>.<이름> 형식 아님")
        root, package, section, name = parts
        tables = {self.tool.LOOK_MATERIALS_KEY: ("LOOK_MATERIAL_SECTIONS", self.tool.LOOK_MATERIAL_SECTIONS),
                  self.tool.LOOK_NIAGARA_KEY: ("LOOK_NIAGARA_SECTIONS", self.tool.LOOK_NIAGARA_SECTIONS)}
        if root not in tables or section not in tables[root][1]:
            return unknown_form("Look", f"구획표에 없음: {root}.{section}")
        table_name, sections = tables[root]
        value_kind = sections[section]
        type_class = TYPE_VECTOR if isinstance(value_kind, int) else LOOK_VALUE_TYPES.get(value_kind, TYPE_UNKNOWN)
        return {"form": "Look", "root": root, "package": package, "section": section, "name": name, "type_class": type_class,
                "type_source": f"{PRESET_SCHEMA_TOOL}.{table_name}[{section}] = {value_kind}", "kinds": [], "golden_path": False}

    def describe_actor_prop(self, entry_id):
        body = entry_id[len(ACTOR_PROP_PREFIX):]
        level, separator, rest = body.rpartition("/")
        parts = rest.split(".")
        if not separator or not level or len(parts) != 3 or not all(parts):
            return unknown_form("actor_prop", "actor_prop:<레벨>/<라벨>.<컴포넌트>.<속성> 형식 아님")
        label, component, property_name = parts
        known = ACTOR_PROPERTY_TYPES.get((component, property_name))
        if known is None:
            return unknown_form("actor_prop", f"고정 엔진 속성표에 없음: {component}.{property_name}")
        return {"form": "actor_prop", "level": level, "label": label, "component": component, "property": property_name, "type_class": known[0],
                "type_source": known[1], "kinds": [], "golden_path": False}

    def describe_tuning(self, entry_id):
        parts = entry_id.split(".")
        section = parts[0]
        if TUNING_SECTIONS_WITH_KIND.get(section) == len(parts):
            owner, struct_name, field_name = parts[1:]
            kinds = [owner] if section == "Kinds" and owner in self.all_kinds else (self.group_kinds(owner) if section == "Groups" else None)
            if kinds is None:
                return unknown_form(section, f"{'종류' if section == 'Kinds' else '묶음'} 미확인: {owner}")
        elif TUNING_SECTIONS_ALL_KINDS.get(section) == len(parts):
            owner, struct_name, field_name = None, parts[1], parts[2]
            kinds = list(self.all_kinds)
        else:
            return unknown_form("조절", "Kinds|Groups.<이름>.<구조체>.<필드> 또는 Globals|StructDefaults.<구조체>.<필드> 형식 아님")
        field = find_field(self.structs, struct_name, field_name)
        if field is None:
            return unknown_form(section, f"F{struct_name}.{field_name} UPROPERTY 를 {HEADER_GLOB} 에서 못 찾음")
        type_class = classify_cpp_type(field["type"], self.enums, self.tool)
        golden_path = struct_name in self.snapshot_structs and bool(self.golden_kinds & set(kinds))
        return {"form": section, "owner": owner, "struct": struct_name, "field": field_name, "cpp_type": field["type"], "type_class": type_class,
                "type_source": f"{field['file']}:{field['line']} {field['type']} {field_name}", "kinds": sorted(kinds), "golden_path": golden_path}


def literal_kind(value):
    if isinstance(value, bool):
        return "불 리터럴"
    if isinstance(value, int):
        return "정수 리터럴"
    if isinstance(value, float):
        return "실수 리터럴"
    return "그 밖 리터럴"


def actor_prop_key(prop):
    values = [prop.get(name) if isinstance(prop, dict) else None for name in ACTOR_PROP_FIELDS]
    if not all(common.is_text(value) for value in values):
        return None
    level, label, component, property_name = values
    return f"{ACTOR_PROP_PREFIX}{level}/{label}.{component}.{property_name}"


def history_key(entry):
    if common.is_text(entry.get("json_path")):
        return entry["json_path"]
    if "actor_prop" in entry:
        return actor_prop_key(entry["actor_prop"])
    return None


def read_overlay(reads, expect):
    if not common.is_text(expect.get("overlay")):
        raise common.InputError("expect.json overlay 경로 필요")
    path = reads.relative(expect["overlay"])
    overlay = reads.read_json(path)
    if not isinstance(overlay, dict) or not isinstance(overlay.get("history"), list):
        raise common.InputError(f"{path}: history 목록 필요")
    current = set()
    for entry in overlay.get("assignments") or []:
        if isinstance(entry, dict) and common.is_text(entry.get("json_path")):
            current.add(entry["json_path"])
    for entry in overlay.get("actor_props") or []:
        key = actor_prop_key(entry)
        if key:
            current.add(key)
    last_operation, writes_by_stem = {}, {}
    for index, entry in enumerate(overlay["history"]):
        key = history_key(entry) if isinstance(entry, dict) else None
        if key is None:
            raise common.InputError(f"{path}: history[{index}] json_path·actor_prop 키 없음")
        operation = entry.get("op")
        last_operation[key] = {"op": operation, "at": entry.get("at"), "index": index}
        source = entry.get("from") if isinstance(entry.get("from"), dict) else {}
        if operation in WRITE_OPERATIONS and common.is_text(source.get("plan")):
            keys = writes_by_stem.setdefault(common.plan_stem(source["plan"]), [])
            if key not in keys:
                keys.append(key)
    return {"path": path, "current": current, "last_operation": last_operation, "writes_by_stem": writes_by_stem}


def read_expect(reads):
    expect = reads.read_json(common.p_plans().EXPECT_PATH)
    adoptions = expect.get("adoptions") if isinstance(expect, dict) else None
    influences = expect.get("influences") if isinstance(expect, dict) and expect.get("influences") is not None else []
    if not isinstance(adoptions, dict) or not isinstance(influences, list):
        raise common.InputError("expect.json adoptions 객체·influences 목록 필요")
    for key, entry in adoptions.items():
        if not isinstance(entry, dict) or not common.is_text(entry.get("entry_id")):
            raise common.InputError(f"expect.json adoptions[{key}].entry_id 필요")
    return expect, adoptions, [str(value) for value in influences]


def check_journals(journal, overlay, expect_keys, problems):
    results = []
    for stem in sorted(journal.final_by_stem):
        record = journal.final_by_stem[stem]
        if record["status"] != JOURNAL_ADOPTED:
            continue
        keys = overlay["writes_by_stem"].get(stem, [])
        matches = []
        for key in keys:
            if key in expect_keys:
                matched = f"expect 채택 {expect_keys[key]}"
            elif (overlay["last_operation"].get(key) or {}).get("op") == REMOVE_OPERATION:
                matched = "overlay 마지막 제거"
            else:
                matched = None
                problems.append(f"일지 채택이 expect·overlay 제거에 없음: {stem}({record['journal']}) → {key}")
            matches.append({"key": key, "matched": matched})
        if not keys:
            problems.append(f"일지 채택이 expect·overlay 제거에 없음: {stem}({record['journal']}) overlay 쓰기 없음")
        results.append({"stem": stem, "area": common.plan_area(stem), "journal": record["journal"], "batch": record["batch"], "keys": matches})
    return results


def adoption_records(adoptions, resolver, overlay, journal_results, problems):
    p = common.p_plans()
    sources_by_key = {}
    for result in journal_results:
        for match in result["keys"]:
            sources_by_key.setdefault(match["key"], []).append(f"{result['batch']}:{result['stem']}")
    records = []
    for key in sorted(adoptions, key=p.adoption_order):
        entry = adoptions[key]
        entry_id = entry["entry_id"]
        described = resolver.describe(entry_id)
        if described["type_class"] == TYPE_UNKNOWN:
            problems.append(f"형식 미확인: 채택 {key} {entry_id} ({described['type_source']})")
        in_overlay = entry_id in overlay["current"]
        if not in_overlay:
            problems.append(f"expect 채택 {key}가 overlay 현재 값에 없음(W11 overlay 채택 아님): {entry_id}")
        record = {"id": key, "entry_id": entry_id, "adopted_value": entry.get("adopted_value"), "value_literal": literal_kind(entry.get("adopted_value")),
                  "in_overlay": in_overlay, "journal_sources": sources_by_key.get(entry_id, []), "discrete": described["type_class"] in DISCRETE_TYPES}
        record.update(described)
        records.append(record)
    return records


def type_counts(records):
    return {name: sum(1 for record in records if record["type_class"] == name) for name in TYPE_ORDER}


def golden_line(records, influences, golden):
    return GOLDEN_LINE_TEMPLATE.format(n=len(records), influences="·".join(influences) if influences else "없음", k=sum(1 for record in records if record["discrete"]),
                                       m=sum(1 for record in records if record["golden_path"]), s=golden["scenarios"])


def render_markdown(line, golden, snapshot_structs, records, influences, journal_results, journal_order, counts):
    cell = common.markdown_cell
    lines = ["# FX-P 골든 감사 줄 (PD-14·RP-S1, runbook-fxp-v3 4.6)", "", f"- 골든 줄: {line}",
             f"- 골든 시험: `{golden['test']}` ({golden['source']}), 시나리오 {golden['scenarios']}개 종류 {'·'.join(golden['scenario_kinds'])}, 스냅숏 구조체 {'·'.join(snapshot_structs)}({SNAPSHOT_GENERATOR} DEFAULT_STRUCTS)",
             f"- {TYPE_RULE}", f"- {GOLDEN_PATH_RULE}", f"- {JOURNAL_RULE}",
             f"- 영향(채택 아님, 줄 n에서 뺌): {'·'.join(influences) if influences else '없음'}",
             "- 형식 분포: " + " · ".join(f"{name} {counts[name]}" for name in TYPE_ORDER), "",
             "| 번호 | 채택 경로 | 형식(근거) | 값 표기 | 골든 경로 | overlay 현재 값 | 일지 원천 |", "|---|---|---|---|---|---|---|"]
    for record in records:
        lines.append(f"| {record['id']} | {cell(record['entry_id'])} | {record['type_class']}({cell(record['type_source'])}) | {cell(record['adopted_value'])} {record['value_literal']} | "
                     f"{'예' if record['golden_path'] else '아니오'} | {'있음' if record['in_overlay'] else '없음'} | {cell(' · '.join(record['journal_sources']) or None)} |")
    discrete = [record for record in records if record["discrete"]]
    lines += ["", "## 골든이 덮지 않는 열거·불·정수 분기 채택", ""]
    lines += [f"- {record['id']} {record['entry_id']} ({record['type_class']}, 골든 경로 {'예' if record['golden_path'] else '아니오'})" for record in discrete] or ["- 없음"]
    lines += ["", "## 일지 최종 채택 대조(PD-6, 계획 줄기별)", "", "| 계획 줄기 | 영역 | 일지 | overlay 쓰기 → 대조 |", "|---|---|---|---|"]
    for result in journal_results:
        writes = " · ".join(f"{match['key']} → {match['matched'] or '없음(종료 1)'}" for match in result["keys"]) or "없음(종료 1)"
        lines.append(f"| {result['stem']} | {cell(result['area'])} | {result['batch']} ({result['journal']}) | {cell(writes)} |")
    lines += ["", "## 일지 합침 순서(PD-6)", ""] + [f"- {entry['batch']} · {cell(entry['first_hold'])} · {entry['journal']}" for entry in journal_order]
    return lines


def build(context):
    reads, problems = context.reads, []
    golden = read_golden_scenarios(reads)
    snapshot_structs = read_snapshot_structs(reads)
    resolver = TypeResolver(reads, golden, snapshot_structs)
    expect, adoptions, influences = read_expect(reads)
    overlay = read_overlay(reads, expect)
    journal = common.merge_journals(reads)
    expect_keys = {adoptions[key]["entry_id"]: key for key in sorted(adoptions, key=common.p_plans().adoption_order)}
    journal_results = check_journals(journal, overlay, expect_keys, problems)
    records = adoption_records(adoptions, resolver, overlay, journal_results, problems)
    counts = type_counts(records)
    line = golden_line(records, influences, golden)
    discrete = [record["id"] for record in records if record["discrete"]]
    golden_path = [record["id"] for record in records if record["golden_path"]]
    unmatched = sum(1 for result in journal_results for match in result["keys"] if match["matched"] is None) + sum(1 for result in journal_results if not result["keys"])
    document = {"version": 1, "golden_line": line, "golden": golden, "snapshot_structs": snapshot_structs, "adoptions": records, "influences": influences,
                "discrete_adoptions": discrete, "golden_path_adoptions": golden_path, "type_counts": counts, "journal_check": journal_results,
                "journal_order": journal.order(), "overlay": overlay["path"],
                "rule": {"type": TYPE_RULE, "golden_path": GOLDEN_PATH_RULE, "journal": JOURNAL_RULE, "line": GOLDEN_LINE_TEMPLATE}}
    markdown = render_markdown(line, golden, snapshot_structs, records, influences, journal_results, journal.order(), counts)
    summary = (f"adoptions={len(records)} influences={len(influences)} discrete={len(discrete)} golden_path={len(golden_path)} scenarios={golden['scenarios']} "
               f"unknown_types={counts[TYPE_UNKNOWN]} not_in_overlay={sum(1 for record in records if not record['in_overlay'])} "
               f"journals={len(journal.entries)} journal_adopted={len(journal_results)} journal_unmatched={unmatched}")
    return common.Outcome(document, markdown, summary, problems)
