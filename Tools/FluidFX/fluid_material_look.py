"""실행 환경: 시스템 Python 3 또는 언리얼 에디터 Python(unreal 을 가져오지 않는 순수 모듈, 에디터 스크립트는 importlib 로 불러 MaterialEditingLibrary·LinearColor 를 인수로 넘긴다)
실행 명령: python Tools/FluidFX/fluid_material_look.py compare <덤프.json> <정본.json> | python Tools/FluidFX/fluid_material_look.py check [<정본.json>]
출력: 표준 출력 JSON(compare = 차이 수·차이 목록 앞 100개, check = 형식 문제 목록). 종료 0 = 차이·문제 없음, 1 = 차이·문제 있음, 2 = 파일 읽기 실패. 정본 기본 경로 Content/FluidFX/PresetData/TDFluidMaterialLook.json(spec-wave0-data-bridge-v2 2.4)
상태: 미검증(2026-10-25 파동 0 P1 작성: py_compile·가짜 unreal 검사·기준선 덤프 compare 차이 0만, 에디터 실행 전)
"""
import hashlib
import json
import math
import os
import struct
import sys

LOOK_RELATIVE_PATH = ("Content", "FluidFX", "PresetData", "TDFluidMaterialLook.json")
TOP_LEVEL_KEYS = ("Materials", "NiagaraSystems")
MATERIAL_SECTIONS = ("Scalars", "Vectors", "Switches")
NIAGARA_SECTIONS = ("Floats", "Ints", "Bools", "Vectors2", "Vectors3", "Colors")
NIAGARA_COMPONENT_COUNTS = {"Vectors2": 2, "Vectors3": 3, "Colors": 4}
COLOR_COMPONENT_COUNT = 4
PACKAGE_ROOT = "/Game/"
INDENT = " "
APPLY_TOLERANCE = 0.001
REPORT_ITEM_LIMIT = 100


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


def format_json(value, depth=0):
    if isinstance(value, dict):
        if not value:
            return "{}"
        inner = INDENT * (depth + 1)
        lines = [f"{inner}{json.dumps(key, ensure_ascii=False)}: {format_json(value[key], depth + 1)}" for key in sorted(value)]
        return "{\n" + ",\n".join(lines) + "\n" + INDENT * depth + "}"
    if isinstance(value, list):
        if not any(isinstance(item, (dict, list)) for item in value):
            return "[" + ", ".join(json.dumps(item, ensure_ascii=False, allow_nan=False) for item in value) + "]"
        inner = INDENT * (depth + 1)
        return "[\n" + ",\n".join(inner + format_json(item, depth + 1) for item in value) + "\n" + INDENT * depth + "]"
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


def compare_entry(owner, dumped, canonical, sections, extra_keys=()):
    differences = []
    for key in extra_keys:
        if dumped.get(key) != canonical.get(key):
            differences.append({"at": f"{owner}.{key}", "dump": dumped.get(key), "canonical": canonical.get(key)})
    for section in sections:
        dumped_values = dumped.get(section, {})
        canonical_values = canonical.get(section, {})
        for name in sorted(set(dumped_values) | set(canonical_values)):
            dump_value = normalize_section_value(section, dumped_values[name]) if name in dumped_values else None
            canonical_value = normalize_section_value(section, canonical_values[name]) if name in canonical_values else None
            if dump_value != canonical_value:
                differences.append({"at": f"{owner}.{section}.{name}", "dump": dump_value, "canonical": canonical_value})
    return differences


def compare_looks(dump, canonical):
    differences = []
    for top_key, sections, extra_keys in (("Materials", MATERIAL_SECTIONS, ("Parent",)), ("NiagaraSystems", NIAGARA_SECTIONS, ())):
        dumped_entries = dump.get(top_key, {})
        canonical_entries = canonical.get(top_key, {})
        for path in sorted(set(dumped_entries) | set(canonical_entries)):
            owner = f"{top_key}.{path}"
            if path not in canonical_entries:
                differences.append({"at": owner, "dump": "있음", "canonical": None})
            elif path not in dumped_entries:
                differences.append({"at": owner, "dump": None, "canonical": "있음"})
            else:
                differences.extend(compare_entry(owner, dumped_entries[path], canonical_entries[path], sections, extra_keys))
    return differences


def run_compare(dump_path, canonical_path):
    dump = read_json_file(dump_path)
    canonical = read_look(canonical_path)
    differences = compare_looks(dump, canonical)
    print(json.dumps({"dump": dump_path, "canonical": canonical_path, "difference_count": len(differences), "differences": differences[:REPORT_ITEM_LIMIT]}, ensure_ascii=False, indent=1))
    return 0 if not differences else 1


def run_check(look_path):
    problems = find_look_problems(read_json_file(look_path))
    print(json.dumps({"look": look_path, "problem_count": len(problems), "problems": problems[:REPORT_ITEM_LIMIT]}, ensure_ascii=False, indent=1))
    return 0 if not problems else 1


def project_root_from_module():
    return os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def main(argv=None):
    arguments = list(sys.argv[1:] if argv is None else argv)
    try:
        if len(arguments) == 3 and arguments[0] == "compare":
            return run_compare(arguments[1], arguments[2])
        if len(arguments) in (1, 2) and arguments[0] == "check":
            return run_check(arguments[1] if len(arguments) == 2 else default_look_path(project_root_from_module()))
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"읽기 실패: {error}")
        return 2
    print("사용법: fluid_material_look.py compare <덤프.json> <정본.json> | check [<정본.json>]")
    return 2


if __name__ == "__main__":
    sys.exit(main())
