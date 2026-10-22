"""실행 환경: 시스템 Python 3(표준 라이브러리만), 에디터·빌드 불필요
실행 명령: python Tools/check_fluid_preset_data.py [--root 경로] [--stage-report 보고.json ...]
출력: ERROR·WARN·STALE 줄과 마지막 SUMMARY 줄, 종료 코드 0 통과(WARN 허용) / 1 ERROR 또는 STALE / 2 입력 오류(헤더·파일 없음)
상태: 검증됨(2026-10-22 FX-8c 프리셋 데이터: 사본 fixture 25/25, 실제 트리 정본 오류 0·WARN 1(FootprintPainter AdvectionSpeedScale 0 < ClampMin 0.01, 런타임 그대로)). 굽기 절차는 Docs/TDFluidLiveSystemDesign.md 9.1절(D-58). sha 없는 옛 무대 보고는 그 무대를 재생성하기 전까지 STALE
"""
import argparse
import hashlib
import json
import math
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TUNING_RELATIVE_PATH = ("Content", "FluidFX", "PresetData", "TDFluidPresetTuning.json")
GAME_INI_RELATIVE_PATH = ("Config", "DefaultGame.ini")
NON_UFS_LINE = '+DirectoriesToAlwaysStageAsNonUFS=(Path="FluidFX/PresetData")'
HEADER_RELATIVE_PATHS = (
    ("Source", "TDGame", "FluidFX", "TDFluidTypes.h"),
    ("Source", "TDGame", "FluidFX", "TDFluidFireTypes.h"),
)
TUNED_STRUCT_NAMES = (
    "TDFluidSimulationConfig",
    "TDFluidSourceEmitterSettings",
    "TDFluidFragmentTrailSettings",
    "TDFluidVolumeRenderSettings",
    "TDFluidFlameCardLayerSettings",
)
PRESET_KIND_ENUM = "ETDFluidPresetKind"
TOP_LEVEL_KEYS = ("StructDefaults", "Groups", "Kinds")
GROUP_KINDS_KEY = "Kinds"
COMPONENT_TYPES = {
    "FVector2D": (("X", "Y"), "double"),
    "FVector": (("X", "Y", "Z"), "double"),
    "FLinearColor": (("R", "G", "B", "A"), "float"),
}
SCALAR_TYPES = ("bool", "int32", "float", "double")
INT32_RANGE = (-(2 ** 31), 2 ** 31 - 1)
FLOAT32_MAX = 3.4028234663852886e38

ENUM_PATTERN = re.compile(r"UENUM\([^)]*\)\s*enum\s+class\s+(\w+)\s*:\s*\w+\s*\{(.*?)\};", re.DOTALL)
STRUCT_PATTERN = re.compile(r"USTRUCT\([^)]*\)\s*struct\s+(?:\w+_API\s+)?F(\w+)\s*\{(.*?)\n\};", re.DOTALL)
PROPERTY_PATTERN = re.compile(r"UPROPERTY\((.*?)\)\s*\n\s*([\w:<>]+)\s+(\w+)\s*(?:=\s*([^;]+))?;")
META_NUMBER_PATTERN = re.compile(r'(ClampMin|ClampMax)\s*=\s*"([^"]+)"')


class FluidPresetSchema:
    def __init__(self, enums, structs):
        self.enums = enums
        self.structs = structs

    def kinds(self):
        return self.enums[PRESET_KIND_ENUM]

    def fields(self, struct_name):
        return self.structs[struct_name]


class InputError(Exception):
    pass


class JsonDocumentError(Exception):
    pass


def parse_enum_names(body):
    names = []
    for entry in body.split(","):
        entry = re.sub(r"UMETA\(.*?\)", "", entry).strip()
        if entry:
            names.append(re.match(r"\w+", entry).group(0))
    return names


def parse_property(meta_text, type_name, field_name, default_text):
    clamp = {key: float(value) for key, value in META_NUMBER_PATTERN.findall(meta_text)}
    return {
        "name": field_name,
        "type": type_name,
        "default": default_text.strip() if default_text else None,
        "clamp_min": clamp.get("ClampMin"),
        "clamp_max": clamp.get("ClampMax"),
    }


def load_schema(root=None):
    root = Path(root) if root else ROOT
    enums = {}
    structs = {}
    for relative in HEADER_RELATIVE_PATHS:
        header_path = root.joinpath(*relative)
        if not header_path.is_file():
            raise InputError(f"헤더 없음: {header_path}")
        text = header_path.read_text(encoding="utf-8").replace("\r\n", "\n")
        for enum_name, body in ENUM_PATTERN.findall(text):
            enums[enum_name] = parse_enum_names(body)
        for struct_name, body in STRUCT_PATTERN.findall(text):
            if struct_name in TUNED_STRUCT_NAMES:
                structs[struct_name] = [parse_property(*match) for match in PROPERTY_PATTERN.findall(body)]
    missing = [name for name in TUNED_STRUCT_NAMES if name not in structs] + ([PRESET_KIND_ENUM] if PRESET_KIND_ENUM not in enums else [])
    if missing:
        raise InputError("헤더에서 찾지 못함: " + ", ".join(missing))
    for struct_name in TUNED_STRUCT_NAMES:
        for field in structs[struct_name]:
            if field["type"] not in SCALAR_TYPES and field["type"] not in COMPONENT_TYPES and field["type"] not in enums:
                raise InputError(f"지원하지 않는 필드 형식: {struct_name}.{field['name']} {field['type']}")
    return FluidPresetSchema(enums, structs)


def reject_constant(name):
    raise JsonDocumentError(f"유한하지 않은 숫자 {name} 는 쓸 수 없음")


def make_pairs_hook(errors):
    def collect_pairs(pairs):
        result = {}
        seen = {}
        for key, value in pairs:
            folded = key.casefold()
            if folded in seen:
                errors.append(f"중복 키(대소문자 무시 비교): {seen[folded]!r} 와 {key!r}")
                continue
            seen[folded] = key
            result[key] = value
        return result
    return collect_pairs


def read_document(path):
    errors = []
    raw = Path(path).read_bytes()
    if raw.startswith(b"\xef\xbb\xbf"):
        errors.append("파일이 BOM 으로 시작함(UTF-8, BOM 없음이어야 함)")
        raw = raw[3:]
    try:
        document = json.loads(raw.decode("utf-8"), object_pairs_hook=make_pairs_hook(errors), parse_constant=reject_constant)
    except (JsonDocumentError, ValueError) as error:
        return None, errors + [f"JSON 해석 실패: {error}"]
    return document, errors


def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def check_scalar(type_name, value, path):
    if type_name == "bool":
        return [] if isinstance(value, bool) else [f"{path}: true 또는 false 여야 함, 값 {value!r}"]
    if not is_number(value):
        return [f"{path}: 숫자여야 함, 값 {value!r}"]
    if not math.isfinite(value):
        return [f"{path}: 유한한 숫자여야 함"]
    if type_name == "int32":
        if isinstance(value, float) and not value.is_integer():
            return [f"{path}: 정수여야 함, 값 {value!r}"]
        if not INT32_RANGE[0] <= value <= INT32_RANGE[1]:
            return [f"{path}: int32 범위 밖, 값 {value!r}"]
    if type_name == "float" and abs(value) > FLOAT32_MAX:
        return [f"{path}: float 범위 밖, 값 {value!r}"]
    return []


def check_value(schema, field, value, path):
    type_name = field["type"]
    if value is None:
        return [f"{path}: null 은 값이 아님"]
    if type_name in schema.enums:
        if not isinstance(value, str):
            return [f"{path}: {type_name} 이름 문자열이어야 함, 값 {value!r}"]
        return [] if value in schema.enums[type_name] else [f"{path}: {type_name} 에 없는 이름 {value!r}(대소문자 구분)"]
    if type_name in COMPONENT_TYPES:
        component_names, component_type = COMPONENT_TYPES[type_name]
        if not isinstance(value, dict):
            return [f"{path}: {type_name} 성분 객체여야 함, 값 {value!r}"]
        errors = [f"{path}.{key}: {type_name} 에 없는 성분" for key in value if key not in component_names]
        errors.extend(f"{path}: 성분 {name} 없음" for name in component_names if name not in value)
        for name in component_names:
            if name in value:
                errors.extend(check_scalar(component_type, value[name], f"{path}.{name}"))
        return errors
    return check_scalar(type_name, value, path)


def check_clamp(field, value, path):
    if not is_number(value) or isinstance(value, bool):
        return []
    if field["clamp_min"] is not None and value < field["clamp_min"]:
        return [f"{path}: 값 {value!r} 가 ClampMin {field['clamp_min']!r} 보다 작음(편집기 UI 제약, 런타임은 그대로 씀)"]
    if field["clamp_max"] is not None and value > field["clamp_max"]:
        return [f"{path}: 값 {value!r} 가 ClampMax {field['clamp_max']!r} 보다 큼(편집기 UI 제약, 런타임은 그대로 씀)"]
    return []


def check_fields(schema, struct_name, fields, path, requires_all_fields):
    errors = []
    warnings = []
    if not isinstance(fields, dict):
        return [f"{path}: 필드 객체여야 함"], warnings
    declared = {field["name"]: field for field in schema.fields(struct_name)}
    for key, value in fields.items():
        if key not in declared:
            errors.append(f"{path}.{key}: {struct_name} 에 없는 필드(대소문자 구분)")
            continue
        errors.extend(check_value(schema, declared[key], value, f"{path}.{key}"))
        warnings.extend(check_clamp(declared[key], value, f"{path}.{key}"))
    if requires_all_fields:
        errors.extend(f"{path}: 필드 {name} 없음(StructDefaults 는 모든 필드 필수)" for name in declared if name not in fields)
    return errors, warnings


def check_struct_entries(schema, entries, path, ignored_keys=()):
    errors = []
    warnings = []
    assigned = []
    for struct_name, fields in entries.items():
        if struct_name in ignored_keys:
            continue
        if struct_name not in TUNED_STRUCT_NAMES:
            errors.append(f"{path}.{struct_name}: 알 수 없는 구조체 이름(대소문자 구분, 허용 {', '.join(TUNED_STRUCT_NAMES)})")
            continue
        field_errors, field_warnings = check_fields(schema, struct_name, fields, f"{path}.{struct_name}", False)
        errors.extend(field_errors)
        warnings.extend(field_warnings)
        if isinstance(fields, dict):
            assigned.extend((struct_name, field_name) for field_name in fields)
    return errors, warnings, assigned


def claim_fields(owner_by_target, kinds, assigned, owner):
    errors = []
    for kind in kinds:
        for struct_name, field_name in assigned:
            target = (kind, struct_name, field_name)
            if target in owner_by_target:
                errors.append(f"{kind}.{struct_name}.{field_name}: {owner_by_target[target]} 와 {owner} 두 곳에서 정함(한 곳 규칙)")
                continue
            owner_by_target[target] = owner
    return errors


def check_document(schema, document):
    errors = []
    warnings = []
    if not isinstance(document, dict):
        return ["최상위는 객체여야 함"], warnings
    errors.extend(f"최상위 키 {key!r} 는 허용되지 않음(허용 {', '.join(TOP_LEVEL_KEYS)})" for key in document if key not in TOP_LEVEL_KEYS)
    errors.extend(f"최상위 키 {key} 없음" for key in TOP_LEVEL_KEYS if key not in document)
    struct_defaults = document.get("StructDefaults", {})
    groups = document.get("Groups", {})
    kinds = document.get("Kinds", {})
    for key, value in (("StructDefaults", struct_defaults), ("Groups", groups), ("Kinds", kinds)):
        if not isinstance(value, dict):
            errors.append(f"{key}: 객체여야 함")
    if errors:
        return errors, warnings

    for struct_name, fields in struct_defaults.items():
        if struct_name not in TUNED_STRUCT_NAMES:
            errors.append(f"StructDefaults.{struct_name}: 알 수 없는 구조체 이름(대소문자 구분)")
            continue
        field_errors, field_warnings = check_fields(schema, struct_name, fields, f"StructDefaults.{struct_name}", True)
        errors.extend(field_errors)
        warnings.extend(field_warnings)
    errors.extend(f"StructDefaults: 구조체 {name} 없음" for name in TUNED_STRUCT_NAMES if name not in struct_defaults)

    owner_by_target = {}
    known_kinds = schema.kinds()
    for group_name, group in groups.items():
        path = f"Groups.{group_name}"
        if not isinstance(group, dict):
            errors.append(f"{path}: 객체여야 함")
            continue
        members = group.get(GROUP_KINDS_KEY)
        if not isinstance(members, list) or not members:
            errors.append(f"{path}.{GROUP_KINDS_KEY}: 종류 이름 배열(1개 이상)이어야 함")
            members = []
        valid_members = []
        for member in members:
            if not isinstance(member, str) or member not in known_kinds:
                errors.append(f"{path}.{GROUP_KINDS_KEY}: 알 수 없는 종류 {member!r}(대소문자 구분)")
                continue
            if member in valid_members:
                errors.append(f"{path}.{GROUP_KINDS_KEY}: 종류 {member} 중복")
                continue
            valid_members.append(member)
        entry_errors, entry_warnings, assigned = check_struct_entries(schema, group, path, ignored_keys=(GROUP_KINDS_KEY,))
        errors.extend(entry_errors)
        warnings.extend(entry_warnings)
        errors.extend(claim_fields(owner_by_target, valid_members, assigned, path))

    for kind_name, entries in kinds.items():
        path = f"Kinds.{kind_name}"
        if kind_name not in known_kinds:
            errors.append(f"{path}: 알 수 없는 종류(대소문자 구분)")
            continue
        if not isinstance(entries, dict):
            errors.append(f"{path}: 객체여야 함")
            continue
        entry_errors, entry_warnings, assigned = check_struct_entries(schema, entries, path)
        errors.extend(entry_errors)
        warnings.extend(entry_warnings)
        errors.extend(claim_fields(owner_by_target, [kind_name], assigned, path))
    return errors, warnings


def inspect_tuning_file(path, root=None):
    schema = load_schema(root)
    if not Path(path).is_file():
        return [f"프리셋 정본 JSON 없음: {path}"], []
    document, read_errors = read_document(path)
    if document is None:
        return read_errors, []
    errors, warnings = check_document(schema, document)
    return read_errors + errors, warnings


def check_tuning_file(path, root=None):
    errors, _ = inspect_tuning_file(path, root)
    return errors


def check_game_ini(root):
    ini_path = root.joinpath(*GAME_INI_RELATIVE_PATH)
    if not ini_path.is_file():
        raise InputError(f"설정 파일 없음: {ini_path}")
    lines = [line.strip() for line in ini_path.read_text(encoding="utf-8").splitlines()]
    return [] if NON_UFS_LINE in lines else [f"{ini_path.name}: {NON_UFS_LINE} 줄 없음(쿠킹 빌드에 JSON 이 실리지 않음)"]


def check_stage_reports(report_paths, tuning_path):
    current = hashlib.sha256(tuning_path.read_bytes()).hexdigest() if tuning_path.is_file() else None
    stale = []
    for report_path in report_paths:
        report_path = Path(report_path)
        if not report_path.is_file():
            raise InputError(f"무대 보고 없음: {report_path}")
        report = json.loads(report_path.read_text(encoding="utf-8"))
        recorded = report.get("preset_tuning_sha256") if isinstance(report, dict) else None
        if recorded is None:
            stale.append(f"{report_path}: preset_tuning_sha256 없음(정본 도입 전 무대 또는 실패 보고)")
        elif recorded != current:
            stale.append(f"{report_path}: 무대가 만든 정본 {recorded[:12]} 와 현재 {str(current)[:12]} 가 다름(그 무대 재생성 필요)")
    return stale


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description="FluidFX 프리셋 정본 JSON 검사(spec-pd-b 4절)")
    parser.add_argument("--root", default=str(ROOT))
    parser.add_argument("--stage-report", action="append", default=[])
    arguments = parser.parse_args()
    root = Path(arguments.root).resolve()
    tuning_path = root.joinpath(*TUNING_RELATIVE_PATH)
    try:
        errors, warnings = inspect_tuning_file(tuning_path, root)
        errors.extend(check_game_ini(root))
        stale = check_stage_reports(arguments.stage_report, tuning_path)
    except (InputError, OSError, ValueError) as error:
        print(f"INPUT {error}")
        print("SUMMARY errors=0 warnings=0 stale=0 input_error=1")
        return 2
    for line in errors:
        print("ERROR " + line)
    for line in warnings:
        print("WARN " + line)
    for line in stale:
        print("STALE " + line)
    print(f"SUMMARY errors={len(errors)} warnings={len(warnings)} stale={len(stale)} input_error=0")
    return 1 if errors or stale else 0


if __name__ == "__main__":
    sys.exit(main())
