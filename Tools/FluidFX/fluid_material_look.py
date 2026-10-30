"""실행 환경: 시스템 Python 3 또는 언리얼 에디터 Python(unreal 을 가져오지 않는 순수 모듈, 에디터 스크립트는 importlib 로 불러 MaterialEditingLibrary·LinearColor 를 인수로 넘긴다)
실행 명령: python Tools/FluidFX/fluid_material_look.py compare <덤프.json> <정본.json> | python Tools/FluidFX/fluid_material_look.py check [<정본.json>] | python Tools/FluidFX/fluid_material_look.py merge <정본.json> <조각.json>... [--out <경로>] [--dry-run]
출력: 표준 출력 JSON(compare = 차이 수·차이 목록 앞 100개·제외한 런타임 User 변수 차이 수(NiagaraSystems 에서 fluid_niagara_drive.all_runtime_user_variables() 이름은 굽지 않는 런타임 값이라 세지 않는다, L-repo-30), check = 형식 문제 목록, merge = 충돌·형식 문제·정본 값 변경·추가 목록 앞 100개와 결과 sha256). merge 는 최상위 구획→항목 키→하위 구획(Parent·Scalars 등)→이름 단위 깊은 병합이다: 조각에 없는 최상위 키·하위 구획은 빈 것으로 보고 새 항목의 빠진 하위 구획은 {} 로 채운다, 조각 사이 같은 잎에 다른 값이면 충돌로 쓰지 않는다, 정본 값 변경은 허용하고 목록으로 낸다, 기존 키 순서를 보존하고 새 키는 사전순으로 자기보다 큰 첫 기존 키 앞(없으면 끝)에 넣는다(정렬된 정본은 정렬 유지), 조각의 실수 값은 float32 최단 표기, 들여쓰기·줄 끝·끝 줄바꿈은 정본을 따른다(같은 입력이면 같은 바이트). --out 이 없으면 정본에 덮어쓰고 --dry-run 은 쓰지 않는다. 종료 0 = 차이·문제·충돌 없음, 1 = 차이·문제·충돌 있음, 2 = 파일 읽기 실패(JSON 중복 키 포함). 정본 기본 경로 Content/FluidFX/PresetData/TDFluidMaterialLook.json(spec-wave0-data-bridge-v2 2.4)
상태: 미검증(2026-10-25 파동 0 P1 작성: py_compile·가짜 unreal 검사·기준선 덤프 compare 차이 0만, 에디터 실행 전). merge 는 2026-10-26 파동 1 F2 작성: fixture(python -B Tools/FluidFX/look_merge_fixtures/run_fixtures.py) 통과와 파동 1 조각 5개 병합만 확인. compare 런타임 User 변수 제외는 2026-10-30 CAL-0 B1 작성(하네스 check_cal0_patch T28만, 에디터 덤프 대조 미실행)
"""
import hashlib
import importlib
import json
import math
import os
import struct
import sys
import tempfile

LOOK_RELATIVE_PATH = ("Content", "FluidFX", "PresetData", "TDFluidMaterialLook.json")
TOP_LEVEL_KEYS = ("Materials", "NiagaraSystems")
MATERIAL_SECTIONS = ("Scalars", "Vectors", "Switches")
NIAGARA_SECTIONS = ("Floats", "Ints", "Bools", "Vectors2", "Vectors3", "Colors")
LOOK_TABLE_LAYOUTS = (("Materials", MATERIAL_SECTIONS, ("Parent",)), ("NiagaraSystems", NIAGARA_SECTIONS, ()))
NIAGARA_COMPONENT_COUNTS = {"Vectors2": 2, "Vectors3": 3, "Colors": 4}
COLOR_COMPONENT_COUNT = 4
PACKAGE_ROOT = "/Game/"
INDENT = " "
APPLY_TOLERANCE = 0.001
REPORT_ITEM_LIMIT = 100
RUNTIME_USER_VARIABLE_MODULE = "fluid_niagara_drive"


def to_float32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def pack_float32_or_none(value):
    try:
        return struct.pack("<f", value)
    except OverflowError:
        return None


def shortest_float32(value):
    value = to_float32(float(value))
    if not math.isfinite(value):
        raise ValueError(f"유한하지 않은 실수: {value!r}")
    target = struct.pack("<f", value)
    for precision in range(1, 10):
        text = "%.*g" % (precision, value)
        if pack_float32_or_none(float(text)) == target:
            return float(text)
    raise ValueError(f"float32 왕복 표기 실패: {value!r}")


def format_json(value, depth=0, indent=INDENT, should_sort_keys=True):
    if isinstance(value, dict):
        if not value:
            return "{}"
        inner = indent * (depth + 1)
        keys = sorted(value) if should_sort_keys else list(value)
        lines = [f"{inner}{json.dumps(key, ensure_ascii=False)}: {format_json(value[key], depth + 1, indent, should_sort_keys)}" for key in keys]
        return "{\n" + ",\n".join(lines) + "\n" + indent * depth + "}"
    if isinstance(value, list):
        if not any(isinstance(item, (dict, list)) for item in value):
            return "[" + ", ".join(json.dumps(item, ensure_ascii=False, allow_nan=False) for item in value) + "]"
        inner = indent * (depth + 1)
        return "[\n" + ",\n".join(inner + format_json(item, depth + 1, indent, should_sort_keys) for item in value) + "\n" + indent * depth + "]"
    return json.dumps(value, ensure_ascii=False, allow_nan=False)


def format_look_text(look):
    return format_json(look) + "\n"


def is_finite_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def is_number_list(value, count):
    return isinstance(value, list) and len(value) == count and all(is_finite_number(item) for item in value)


def is_package_path(value):
    return isinstance(value, str) and value.startswith(PACKAGE_ROOT) and "." not in value and not value.endswith("/")


def find_section_problems(owner, section, values):
    if not isinstance(values, dict):
        return [f"{owner}.{section}: 객체가 아님"]
    problems = []
    for name, value in values.items():
        label = f"{owner}.{section}.{name}"
        if not isinstance(name, str) or not name:
            problems.append(f"{label}: 이름이 비었음")
        elif section in ("Scalars", "Floats") and not is_finite_number(value):
            problems.append(f"{label}: 유한 실수가 아님")
        elif section == "Vectors" and not is_number_list(value, COLOR_COMPONENT_COUNT):
            problems.append(f"{label}: 실수 4개 배열이 아님")
        elif section in ("Switches", "Bools") and not isinstance(value, bool):
            problems.append(f"{label}: 불이 아님")
        elif section == "Ints" and (not isinstance(value, int) or isinstance(value, bool)):
            problems.append(f"{label}: 정수가 아님")
        elif section in NIAGARA_COMPONENT_COUNTS and not is_number_list(value, NIAGARA_COMPONENT_COUNTS[section]):
            problems.append(f"{label}: 실수 {NIAGARA_COMPONENT_COUNTS[section]}개 배열이 아님")
    return problems


def find_entry_problems(owner, entry, sections, extra_keys=()):
    if not isinstance(entry, dict):
        return [f"{owner}: 객체가 아님"]
    expected = set(sections) | set(extra_keys)
    problems = [f"{owner}: 모르는 키 {key}" for key in sorted(set(entry) - expected)]
    problems.extend(f"{owner}: 키 없음 {key}" for key in sorted(expected - set(entry)))
    for section in sections:
        if section in entry:
            problems.extend(find_section_problems(owner, section, entry[section]))
    return problems


def find_look_problems(look):
    if not isinstance(look, dict):
        return ["최상위: 객체가 아님"]
    problems = [f"최상위: 모르는 키 {key}" for key in sorted(set(look) - set(TOP_LEVEL_KEYS))]
    problems.extend(f"최상위: 키 없음 {key}" for key in TOP_LEVEL_KEYS if key not in look)
    if problems:
        return problems
    for top_key in TOP_LEVEL_KEYS:
        if not isinstance(look[top_key], dict):
            problems.append(f"{top_key}: 객체가 아님")
    if problems:
        return problems
    for package_path, entry in look["Materials"].items():
        if not is_package_path(package_path):
            problems.append(f"Materials.{package_path}: /Game 패키지 경로가 아님")
        problems.extend(find_entry_problems(f"Materials.{package_path}", entry, MATERIAL_SECTIONS, ("Parent",)))
        if isinstance(entry, dict) and "Parent" in entry and not is_package_path(entry["Parent"]):
            problems.append(f"Materials.{package_path}.Parent: /Game 패키지 경로가 아님")
    for package_path, entry in look["NiagaraSystems"].items():
        if not is_package_path(package_path):
            problems.append(f"NiagaraSystems.{package_path}: /Game 패키지 경로가 아님")
        problems.extend(find_entry_problems(f"NiagaraSystems.{package_path}", entry, NIAGARA_SECTIONS))
    return problems


def read_json_file(path):
    with open(path, "r", encoding="utf-8") as json_file:
        return json.load(json_file)


def read_look(path):
    look = read_json_file(path)
    problems = find_look_problems(look)
    if problems:
        raise ValueError(f"모양 정본 형식 오류({path}): " + " | ".join(problems[:20]))
    return look


def default_look_path(project_directory):
    return os.path.join(project_directory, *LOOK_RELATIVE_PATH)


def file_sha256(path):
    with open(path, "rb") as source_file:
        return hashlib.sha256(source_file.read()).hexdigest()


def package_path_of(asset):
    return str(asset.get_path_name()).split(".", 1)[0]


class TDMaterialLookTable:
    def __init__(self, look, source_path):
        self.look = look
        self.source_path = source_path

    @classmethod
    def read_project(cls, project_directory, look_path=None):
        path = look_path or default_look_path(project_directory)
        return cls(read_look(path), path)

    def source_sha256(self):
        return file_sha256(self.source_path)

    def material(self, package_path, parent_path=None):
        entry = self.look["Materials"].get(package_path)
        if entry is None:
            raise RuntimeError(f"모양 정본에 재질 인스턴스 없음: {package_path} ({self.source_path})")
        if parent_path is not None and entry["Parent"] != parent_path:
            raise RuntimeError(f"모양 정본 부모 불일치: {package_path} 정본 {entry['Parent']} ≠ 스크립트 {parent_path}")
        return entry

    def override_names(self, package_path):
        entry = self.material(package_path)
        return set(entry["Scalars"]) | set(entry["Vectors"])

    def niagara(self, package_path):
        entry = self.look["NiagaraSystems"].get(package_path)
        if entry is None:
            raise RuntimeError(f"모양 정본에 나이아가라 시스템 없음: {package_path} ({self.source_path})")
        return entry

    def apply_to_instance(self, editing, linear_color, instance, package_path, parent):
        entry = self.material(package_path, package_path_of(parent))
        editing.set_material_instance_parent(instance, parent)
        apply_material_entry(editing, linear_color, instance, entry)
        editing.update_material_instance(instance)
        require_applied_entry(editing, instance, entry, package_path)
        return entry


def apply_material_entry(editing, linear_color, instance, entry):
    for name in sorted(entry["Vectors"]):
        rgba = entry["Vectors"][name]
        editing.set_material_instance_vector_parameter_value(instance, name, linear_color(rgba[0], rgba[1], rgba[2], rgba[3]))
    for name in sorted(entry["Scalars"]):
        editing.set_material_instance_scalar_parameter_value(instance, name, entry["Scalars"][name])
    for name in sorted(entry["Switches"]):
        editing.set_material_instance_static_switch_parameter_value(instance, name, entry["Switches"][name])


def find_applied_mismatches(editing, instance, entry, tolerance=APPLY_TOLERANCE):
    mismatches = []
    for name in sorted(entry["Vectors"]):
        stored = editing.get_material_instance_vector_parameter_value(instance, name)
        stored_rgba = (stored.r, stored.g, stored.b, stored.a)
        if max(abs(actual - wanted) for actual, wanted in zip(stored_rgba, entry["Vectors"][name])) > tolerance:
            mismatches.append(f"Vectors.{name}")
    for name in sorted(entry["Scalars"]):
        if abs(editing.get_material_instance_scalar_parameter_value(instance, name) - entry["Scalars"][name]) > tolerance:
            mismatches.append(f"Scalars.{name}")
    for name in sorted(entry["Switches"]):
        if bool(editing.get_material_instance_static_switch_parameter_value(instance, name)) != entry["Switches"][name]:
            mismatches.append(f"Switches.{name}")
    return mismatches


def require_applied_entry(editing, instance, entry, package_path):
    mismatches = find_applied_mismatches(editing, instance, entry)
    if mismatches:
        raise RuntimeError(f"재질 인스턴스 모양 설정 확인 실패: {package_path} " + ", ".join(mismatches))


def normalize_section_value(section, value):
    if section in ("Scalars", "Floats"):
        return shortest_float32(value)
    if section == "Vectors" or section in NIAGARA_COMPONENT_COUNTS:
        return [shortest_float32(item) for item in value]
    return value


def normalize_entry(entry, sections, extra_keys=()):
    normalized = {key: entry[key] for key in extra_keys}
    for section in sections:
        values = entry.get(section, {})
        normalized[section] = {name: normalize_section_value(section, values[name]) for name in sorted(values)}
    return normalized


def make_canonical_look(dump):
    return {
        "Materials": {path: normalize_entry(dump["Materials"][path], MATERIAL_SECTIONS, ("Parent",)) for path in sorted(dump["Materials"])},
        "NiagaraSystems": {path: normalize_entry(dump["NiagaraSystems"][path], NIAGARA_SECTIONS) for path in sorted(dump["NiagaraSystems"])},
    }


def compare_entry(owner, dumped, canonical, sections, extra_keys=(), excluded_names=frozenset()):
    differences = []
    for key in extra_keys:
        if dumped.get(key) != canonical.get(key):
            differences.append({"at": f"{owner}.{key}", "dump": dumped.get(key), "canonical": canonical.get(key)})
    for section in sections:
        dumped_values = dumped.get(section, {})
        canonical_values = canonical.get(section, {})
        for name in sorted((set(dumped_values) | set(canonical_values)) - set(excluded_names)):
            dump_value = normalize_section_value(section, dumped_values[name]) if name in dumped_values else None
            canonical_value = normalize_section_value(section, canonical_values[name]) if name in canonical_values else None
            if dump_value != canonical_value:
                differences.append({"at": f"{owner}.{section}.{name}", "dump": dump_value, "canonical": canonical_value})
    return differences


def compare_looks(dump, canonical, runtime_user_names=frozenset()):
    differences = []
    for top_key, sections, extra_keys in (("Materials", MATERIAL_SECTIONS, ("Parent",)), ("NiagaraSystems", NIAGARA_SECTIONS, ())):
        dumped_entries = dump.get(top_key, {})
        canonical_entries = canonical.get(top_key, {})
        excluded_names = runtime_user_names if top_key == "NiagaraSystems" else frozenset()
        for path in sorted(set(dumped_entries) | set(canonical_entries)):
            owner = f"{top_key}.{path}"
            if path not in canonical_entries:
                differences.append({"at": owner, "dump": "있음", "canonical": None})
            elif path not in dumped_entries:
                differences.append({"at": owner, "dump": None, "canonical": "있음"})
            else:
                differences.extend(compare_entry(owner, dumped_entries[path], canonical_entries[path], sections, extra_keys, excluded_names))
    return differences


def load_runtime_user_names():
    module_directory = os.path.dirname(os.path.abspath(__file__))
    if module_directory not in sys.path:
        sys.path.insert(0, module_directory)
    drive = importlib.import_module(RUNTIME_USER_VARIABLE_MODULE)
    return frozenset(name for name, _ in drive.all_runtime_user_variables())


def run_compare(dump_path, canonical_path):
    dump = read_json_file(dump_path)
    canonical = read_look(canonical_path)
    runtime_user_names = load_runtime_user_names()
    differences = compare_looks(dump, canonical, runtime_user_names)
    excluded_count = len(compare_looks(dump, canonical)) - len(differences)
    print(json.dumps({"dump": dump_path, "canonical": canonical_path, "difference_count": len(differences), "excluded_runtime_user_differences": excluded_count,
                      "differences": differences[:REPORT_ITEM_LIMIT]}, ensure_ascii=False, indent=1))
    return 0 if not differences else 1


def run_check(look_path):
    problems = find_look_problems(read_json_file(look_path))
    print(json.dumps({"look": look_path, "problem_count": len(problems), "problems": problems[:REPORT_ITEM_LIMIT]}, ensure_ascii=False, indent=1))
    return 0 if not problems else 1


def reject_duplicate_keys(pairs):
    values = {}
    for key, value in pairs:
        if key in values:
            raise ValueError(f"JSON 중복 키 {key}")
        values[key] = value
    return values


def read_json_text_strict(path):
    with open(path, "rb") as json_file:
        raw = json_file.read()
    try:
        text = raw.decode("utf-8")
        return text, json.loads(text, object_pairs_hook=reject_duplicate_keys)
    except ValueError as error:
        raise ValueError(f"{path}: {error}") from error


def detect_json_style(text):
    indent = INDENT
    for line in text.splitlines()[1:]:
        content = line.lstrip(" \t")
        if content and len(content) < len(line):
            indent = line[: len(line) - len(content)]
            break
    return {"indent": indent, "newline": "\r\n" if "\r\n" in text else "\n", "has_final_newline": text.endswith("\n")}


def format_styled_json(value, style):
    text = format_json(value, indent=style["indent"], should_sort_keys=False).replace("\n", style["newline"])
    return text + style["newline"] if style["has_final_newline"] else text


def find_fragment_problems(label, fragment):
    if not isinstance(fragment, dict):
        return [f"{label}: 최상위가 객체가 아님"]
    problems = [f"{label}: 모르는 최상위 키 {key}" for key in sorted(set(fragment) - set(TOP_LEVEL_KEYS))]
    for top_key, sections, extra_keys in LOOK_TABLE_LAYOUTS:
        entries = fragment.get(top_key, {})
        if not isinstance(entries, dict):
            problems.append(f"{label}: {top_key} 가 객체가 아님")
            continue
        for package_path, entry in entries.items():
            owner = f"{label}: {top_key}.{package_path}"
            if not is_package_path(package_path):
                problems.append(f"{owner}: /Game 패키지 경로가 아님")
            if not isinstance(entry, dict):
                problems.append(f"{owner}: 객체가 아님")
                continue
            problems.extend(f"{owner}: 모르는 키 {key}" for key in sorted(set(entry) - set(sections) - set(extra_keys)))
            problems.extend(f"{owner}.{key}: /Game 패키지 경로가 아님" for key in extra_keys if key in entry and not is_package_path(entry[key]))
            for section in sections:
                if section in entry:
                    problems.extend(find_section_problems(owner, section, entry[section]))
    return problems


def iterate_fragment_leaves(fragment):
    for top_key, sections, extra_keys in LOOK_TABLE_LAYOUTS:
        for package_path, entry in fragment.get(top_key, {}).items():
            for key in extra_keys:
                if key in entry:
                    yield (top_key, package_path, key), entry[key]
            for section in sections:
                for name, value in entry.get(section, {}).items():
                    yield (top_key, package_path, section, name), normalize_section_value(section, value)


def collect_fragment_leaves(fragments):
    leaf_sources = {}
    for label, fragment in fragments:
        for leaf_path, value in iterate_fragment_leaves(fragment):
            leaf_sources.setdefault(leaf_path, []).append((label, value))
    return leaf_sources


def find_leaf_conflicts(leaf_sources):
    conflicts = []
    for leaf_path in sorted(leaf_sources):
        sources = leaf_sources[leaf_path]
        if any(value != sources[0][1] for _, value in sources[1:]):
            conflicts.append({"at": ".".join(leaf_path), "values": [{"fragment": label, "value": value} for label, value in sources]})
    return conflicts


def group_fragment_leaves(fragments, leaf_sources):
    grouped = {top_key: {} for top_key in TOP_LEVEL_KEYS}
    for _, fragment in fragments:
        for top_key in TOP_LEVEL_KEYS:
            for package_path in fragment.get(top_key, {}):
                grouped[top_key].setdefault(package_path, {})
    for leaf_path, sources in leaf_sources.items():
        entry_leaves = grouped[leaf_path[0]][leaf_path[1]]
        if len(leaf_path) == 3:
            entry_leaves[leaf_path[2]] = sources[0][1]
        else:
            entry_leaves.setdefault(leaf_path[2], {})[leaf_path[3]] = sources[0][1]
    return grouped


def insert_new_keys(existing, additions):
    pending = sorted(additions)
    merged = {}
    index = 0
    for key, value in existing.items():
        while index < len(pending) and pending[index] < key:
            merged[pending[index]] = additions[pending[index]]
            index += 1
        merged[key] = value
    for key in pending[index:]:
        merged[key] = additions[key]
    return merged


def normalized_leaf(leaf_path, value):
    return normalize_section_value(leaf_path[2], value) if len(leaf_path) == 4 else value


def merge_section(section, values, section_leaves):
    updated = {}
    for name, value in values.items():
        is_same = name not in section_leaves or normalize_section_value(section, value) == section_leaves[name]
        updated[name] = value if is_same else section_leaves[name]
    return insert_new_keys(updated, {name: value for name, value in section_leaves.items() if name not in values})


def merge_entry(entry, entry_leaves, sections):
    source = entry if entry is not None else {section: {} for section in sorted(sections)}
    updated = {}
    for key, value in source.items():
        updated[key] = merge_section(key, value, entry_leaves.get(key, {})) if key in sections else entry_leaves.get(key, value)
    return insert_new_keys(updated, {key: value for key, value in entry_leaves.items() if key not in source})


def merge_table(table, table_leaves, sections):
    updated = {path: merge_entry(entry, table_leaves[path], sections) if path in table_leaves else entry for path, entry in table.items()}
    return insert_new_keys(updated, {path: merge_entry(None, entry_leaves, sections) for path, entry_leaves in table_leaves.items() if path not in table})


def read_canonical_leaf(look, leaf_path):
    entry = look[leaf_path[0]].get(leaf_path[1])
    if entry is None:
        return False, None
    values = entry if len(leaf_path) == 3 else entry[leaf_path[2]]
    name = leaf_path[-1]
    return (name in values), values.get(name)


def describe_leaf_changes(canonical, leaf_sources):
    changes = []
    added_leaves = []
    unchanged_count = 0
    for leaf_path in sorted(leaf_sources):
        sources = leaf_sources[leaf_path]
        has_canonical, canonical_value = read_canonical_leaf(canonical, leaf_path)
        if not has_canonical:
            added_leaves.append(".".join(leaf_path))
        elif normalized_leaf(leaf_path, canonical_value) == sources[0][1]:
            unchanged_count += 1
        else:
            changes.append({"at": ".".join(leaf_path), "canonical": canonical_value, "merged": sources[0][1], "fragments": [label for label, _ in sources]})
    return changes, added_leaves, unchanged_count


def merge_look_fragments(canonical, fragments):
    outcome = {"problems": [problem for label, fragment in fragments for problem in find_fragment_problems(label, fragment)], "conflicts": []}
    if outcome["problems"]:
        return None, outcome
    leaf_sources = collect_fragment_leaves(fragments)
    outcome["conflicts"] = find_leaf_conflicts(leaf_sources)
    if outcome["conflicts"]:
        return None, outcome
    grouped = group_fragment_leaves(fragments, leaf_sources)
    sections_by_table = {top_key: sections for top_key, sections, _ in LOOK_TABLE_LAYOUTS}
    merged = {top_key: merge_table(canonical[top_key], grouped[top_key], sections_by_table[top_key]) for top_key in canonical}
    outcome["problems"] = find_look_problems(merged)
    outcome["changes"], outcome["added_leaves"], outcome["unchanged_leaf_count"] = describe_leaf_changes(canonical, leaf_sources)
    outcome["added_entries"] = sorted(f"{top_key}.{path}" for top_key in TOP_LEVEL_KEYS for path in grouped[top_key] if path not in canonical[top_key])
    return (None if outcome["problems"] else merged), outcome


def read_bytes_or_none(path):
    try:
        with open(path, "rb") as source_file:
            return source_file.read()
    except FileNotFoundError:
        return None


def write_bytes_replacing(path, data):
    handle, temporary_path = tempfile.mkstemp(dir=os.path.dirname(os.path.abspath(path)), prefix=".look-merge-", suffix=".tmp")
    try:
        with os.fdopen(handle, "wb") as output_file:
            output_file.write(data)
        os.replace(temporary_path, path)
    finally:
        if os.path.exists(temporary_path):
            os.remove(temporary_path)


def run_merge(canonical_path, fragment_paths, out_path, is_dry_run):
    canonical_text, canonical = read_json_text_strict(canonical_path)
    canonical_problems = find_look_problems(canonical)
    if canonical_problems:
        raise ValueError(f"모양 정본 형식 오류({canonical_path}): " + " | ".join(canonical_problems[:20]))
    fragments = [(path, read_json_text_strict(path)[1]) for path in fragment_paths]
    merged, outcome = merge_look_fragments(canonical, fragments)
    report = {"canonical": canonical_path, "fragments": fragment_paths, "out": out_path, "dry_run": is_dry_run,
              "conflict_count": len(outcome["conflicts"]), "conflicts": outcome["conflicts"][:REPORT_ITEM_LIMIT],
              "problem_count": len(outcome["problems"]), "problems": outcome["problems"][:REPORT_ITEM_LIMIT], "written": False}
    if merged is None:
        print(json.dumps(report, ensure_ascii=False, indent=1))
        return 1
    output_bytes = format_styled_json(merged, detect_json_style(canonical_text)).encode("utf-8")
    is_out_different = read_bytes_or_none(out_path) != output_bytes
    if is_out_different and not is_dry_run:
        write_bytes_replacing(out_path, output_bytes)
    report.update({"written": is_out_different and not is_dry_run, "out_differs": is_out_different, "output_sha256": hashlib.sha256(output_bytes).hexdigest(),
                   "added_entry_count": len(outcome["added_entries"]), "added_entries": outcome["added_entries"][:REPORT_ITEM_LIMIT],
                   "added_leaf_count": len(outcome["added_leaves"]), "added_leaves": outcome["added_leaves"][:REPORT_ITEM_LIMIT],
                   "changed_count": len(outcome["changes"]), "changes": outcome["changes"][:REPORT_ITEM_LIMIT], "unchanged_leaf_count": outcome["unchanged_leaf_count"]})
    print(json.dumps(report, ensure_ascii=False, indent=1))
    return 0


def parse_merge_arguments(arguments):
    positional = []
    out_path = None
    is_dry_run = False
    index = 0
    while index < len(arguments):
        argument = arguments[index]
        if argument == "--dry-run":
            is_dry_run = True
        elif argument == "--out" and index + 1 < len(arguments):
            out_path = arguments[index + 1]
            index += 1
        elif argument.startswith("--"):
            return None
        else:
            positional.append(argument)
        index += 1
    if len(positional) < 2:
        return None
    return positional[0], positional[1:], out_path or positional[0], is_dry_run


def project_root_from_module():
    return os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def main(argv=None):
    arguments = list(sys.argv[1:] if argv is None else argv)
    try:
        if len(arguments) == 3 and arguments[0] == "compare":
            return run_compare(arguments[1], arguments[2])
        if len(arguments) in (1, 2) and arguments[0] == "check":
            return run_check(arguments[1] if len(arguments) == 2 else default_look_path(project_root_from_module()))
        merge_arguments = parse_merge_arguments(arguments[1:]) if arguments and arguments[0] == "merge" else None
        if merge_arguments is not None:
            return run_merge(*merge_arguments)
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"읽기 실패: {error}")
        return 2
    print("사용법: fluid_material_look.py compare <덤프.json> <정본.json> | check [<정본.json>] | merge <정본.json> <조각.json>... [--out <경로>] [--dry-run]")
    return 2


if __name__ == "__main__":
    sys.exit(main())
