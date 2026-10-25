"""실행 환경: 시스템 Python 3(표준 라이브러리만), 에디터·빌드 불필요
실행 명령: python Tools/check_fluid_preset_data.py [--root 경로] [--tuning 조절.json] [--look 모양.json] [--stage-report 보고.json ...]
출력: ERROR·WARN·STALE 줄과 마지막 SUMMARY 줄(조절·모양 두 파일 합계), 종료 코드 0 통과(WARN 허용) / 1 ERROR 또는 STALE / 2 입력 오류(헤더·등록표·지정 파일 없음)
상태: 검증됨(2026-10-22 FX-8c 프리셋 데이터: 사본 fixture 25/25, 실제 트리 정본 오류 0·WARN 1(FootprintPainter AdvectionSpeedScale 0 < ClampMin 0.01, 런타임 그대로)). 파동 0(2026-10-25, 명세 spec-wave0-data-bridge-v2 2.5·R6): 등록표·범위는 C++ GetRegisteredStructs 에서, 형식은 Source/TDGame/FluidFX/*.h 전부에서 읽고 최상위 Globals(범위 Global 만·전 필드 필수)·TArray·FName·소프트 경로(/Game 형식+패키지 존재)·하위 구조체를 받는다(--root fixture 확인). 파동 0 P2(2026-10-25): --tuning·--look 로 사본을 검사하고(기본은 루트 정본), 모양 파일 TDFluidMaterialLook.json 을 C++ ParseMaterialLookText 와 같은 규칙(최상위 두 키, 객체 이름 없는 /Game 패키지 키, Parent 경로, 필수 구획, 값 형식·유한값·float·int32 범위)으로 검사한다. R7 런타임 이름은 Source/TDGame/FluidFX 런타임 소스(Tests 제외)의 Set*ParameterValue(TEXT(…))·TEXT("User.…") 에서 뽑아 Scalars·Vectors·나이아가라 구획에 있으면 ERROR(대소문자 무시), 조건부 DensityScale·SoftFalloffCm 과 기준선 에셋 굽기 이름(BASELINE_BAKED_RUNTIME_NAMES)은 WARN, Content 에 없는 패키지는 WARN 이다. --stage-report 는 material_look_sha256 도 비교한다(다르면 STALE, 없으면 WARN). 검증: --root fixture 76/76(C++ RejectsBadInput 와 같은 잘못된 입력 52건 거부 포함), 실제 트리 정본 오류 0·WARN 4. 굽기 절차는 Docs/TDFluidLiveSystemDesign.md 9.1절(D-58). sha 없는 옛 무대 보고는 그 무대를 재생성하기 전까지 STALE
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
HEADER_FOLDER_RELATIVE_PATH = ("Source", "TDGame", "FluidFX")
REGISTRY_RELATIVE_PATH = ("Source", "TDGame", "FluidFX", "TDFluidPresetTuningSubsystem.cpp")
PRESET_KIND_ENUM = "ETDFluidPresetKind"
PRESET_KIND_SCOPE = "PresetKind"
GLOBAL_SCOPE = "Global"
SECTION_BY_SCOPE = {PRESET_KIND_SCOPE: "StructDefaults", GLOBAL_SCOPE: "Globals"}
TOP_LEVEL_KEYS = ("StructDefaults", "Groups", "Kinds", "Globals")
GROUP_KINDS_KEY = "Kinds"
COMPONENT_TYPES = {
    "FVector2D": (("X", "Y"), "double"),
    "FVector": (("X", "Y", "Z"), "double"),
    "FLinearColor": (("R", "G", "B", "A"), "float"),
}
SCALAR_TYPES = ("bool", "int32", "uint8", "uint32", "float", "double")
INTEGER_RANGES = {
    "int32": (-(2 ** 31), 2 ** 31 - 1),
    "uint8": (0, 2 ** 8 - 1),
    "uint32": (0, 2 ** 32 - 1),
}
FLOAT32_MAX = 3.4028234663852886e38
NAME_TYPE = "FName"
SOFT_PATH_STRUCT_TYPE = "FSoftObjectPath"
PACKAGE_SUFFIXES = (".uasset", ".umap")
EMPTY_ARRAY_DEFAULTS = (None, "{}")
LOOK_RELATIVE_PATH = ("Content", "FluidFX", "PresetData", "TDFluidMaterialLook.json")
LOOK_MATERIALS_KEY = "Materials"
LOOK_NIAGARA_KEY = "NiagaraSystems"
LOOK_TOP_LEVEL_KEYS = (LOOK_MATERIALS_KEY, LOOK_NIAGARA_KEY)
LOOK_PARENT_KEY = "Parent"
LOOK_MATERIAL_SECTIONS = {"Scalars": "float", "Vectors": 4, "Switches": "bool"}
LOOK_NIAGARA_SECTIONS = {"Floats": "float", "Ints": "int32", "Bools": "bool", "Vectors2": 2, "Vectors3": 3, "Colors": 4}
LOOK_MID_OVERRIDE_SECTIONS = ("Scalars", "Vectors")
RUNTIME_SOURCE_RELATIVE_PATH = ("Source", "TDGame", "FluidFX")
RUNTIME_SOURCE_SUFFIXES = (".h", ".cpp")
RUNTIME_SOURCE_EXCLUDED_FOLDER = "Tests"
CONDITIONAL_RUNTIME_NAMES = {
    "DensityScale": "투과(bEnableTransmittance)가 켜진 볼륨에서만 C++ 가 설정하고 그때는 C++ 값이 나중에 이김",
    "SoftFalloffCm": "투과(bEnableTransmittance)가 켜진 볼륨에서만 C++ 가 설정하고 그때는 C++ 값이 나중에 이김",
}
BASELINE_BAKED_RUNTIME_NAMES = {
    "ExtrudeSymmetric": "기준선 에셋 덤프의 MI 굽기 값(M6), 런타임 MID 는 C++ 값이 이김",
    "User.SimCenterWorld": "기준선 에셋 덤프의 나이아가라 User 기본값(M6), 런타임 컴포넌트는 C++ 값이 이김",
    "User.SimExtentsCm": "기준선 에셋 덤프의 나이아가라 User 기본값(M6), 런타임 컴포넌트는 C++ 값이 이김",
    "User.SimCenterPlane": "기준선 에셋 덤프의 나이아가라 User 기본값(M6), 런타임 컴포넌트는 C++ 값이 이김",
    "User.PlaneAxisU": "기준선 에셋 덤프의 나이아가라 User 기본값(M6), 런타임 컴포넌트는 C++ 값이 이김",
    "User.PlaneAxisV": "기준선 에셋 덤프의 나이아가라 User 기본값(M6), 런타임 컴포넌트는 C++ 값이 이김",
    "User.PlaneAxisN": "기준선 에셋 덤프의 나이아가라 User 기본값(M6), 런타임 컴포넌트는 C++ 값이 이김",
    "User.PlaneCoordinateOffsetCm": "기준선 에셋 덤프의 나이아가라 User 기본값(M6), 런타임 컴포넌트는 C++ 값이 이김",
    "User.VelocityReferenceCmPerSec": "기준선 에셋 덤프의 나이아가라 User 기본값(M6), 런타임 컴포넌트는 C++ 값이 이김",
}

ENUM_PATTERN = re.compile(r"UENUM\([^)]*\)\s*enum\s+class\s+(\w+)\s*:\s*\w+\s*\{(.*?)\};", re.DOTALL)
STRUCT_PATTERN = re.compile(r"USTRUCT\([^)]*\)\s*struct\s+(?:\w+_API\s+)?F(\w+)(?:\s*:\s*public\s+F(\w+))?\s*\{(.*?)\n\};", re.DOTALL)
PROPERTY_PATTERN = re.compile(r"UPROPERTY\((.*?)\)\s*\n\s*([\w:<>]+)\s+(\w+)\s*(?:=\s*([^;]+))?;")
META_NUMBER_PATTERN = re.compile(r'(ClampMin|ClampMax)\s*=\s*"([^"]+)"')
REGISTRY_BODY_PATTERN = re.compile(r"UTDFluidPresetTuningSubsystem::GetRegisteredStructs\(\)\s*\{(.*?)\n\}", re.DOTALL)
REGISTRY_ENTRY_PATTERN = re.compile(r"\{\s*F(\w+)::StaticStruct\(\)\s*,\s*ETDFluidPresetTuningScope::(\w+)\s*\}")
ARRAY_TYPE_PATTERN = re.compile(r"TArray<(.+)>")
ENUM_AS_BYTE_TYPE_PATTERN = re.compile(r"TEnumAsByte<(\w+)>")
SOFT_POINTER_TYPE_PATTERN = re.compile(r"TSoft(?:Object|Class)Ptr<\w+>")
GAME_PATH_PATTERN = re.compile(r"/Game/[A-Za-z0-9_]+(?:/[A-Za-z0-9_]+)*(?:\.[A-Za-z0-9_]+)?")
GAME_PACKAGE_PATTERN = re.compile(r"/Game/[A-Za-z0-9_]+(?:/[A-Za-z0-9_]+)*")
MATERIAL_PARAMETER_SET_PATTERN = re.compile(r'Set\w*ParameterValue\w*\(\s*TEXT\("([^"]+)"\)')
NIAGARA_USER_NAME_PATTERN = re.compile(r'TEXT\("(User\.[^"]+)"\)')


class FluidPresetSchema:
    def __init__(self, enums, structs, registry, root):
        self.enums = enums
        self.structs = structs
        self.registry = registry
        self.root = root

    def kinds(self):
        return self.enums[PRESET_KIND_ENUM]

    def fields(self, struct_name):
        return self.structs[struct_name]

    def registered_names(self, scope):
        return [name for name, struct_scope in self.registry.items() if struct_scope == scope]

    def type_kind(self, type_name):
        if type_name in SCALAR_TYPES:
            return "scalar", type_name
        if type_name in self.enums:
            return "enum", type_name
        enum_as_byte = ENUM_AS_BYTE_TYPE_PATTERN.fullmatch(type_name)
        if enum_as_byte and enum_as_byte.group(1) in self.enums:
            return "enum", enum_as_byte.group(1)
        if type_name in COMPONENT_TYPES:
            return "component", type_name
        if type_name == NAME_TYPE:
            return "name", type_name
        if type_name == SOFT_PATH_STRUCT_TYPE or SOFT_POINTER_TYPE_PATTERN.fullmatch(type_name):
            return "soft_path", type_name
        array = ARRAY_TYPE_PATTERN.fullmatch(type_name)
        if array:
            return "array", array.group(1)
        if type_name.startswith("F") and type_name[1:] in self.structs:
            return "struct", type_name[1:]
        return "unsupported", type_name


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


def read_registry(root):
    registry_path = root.joinpath(*REGISTRY_RELATIVE_PATH)
    if not registry_path.is_file():
        raise InputError(f"등록표 소스 없음: {registry_path}")
    body = REGISTRY_BODY_PATTERN.search(registry_path.read_text(encoding="utf-8").replace("\r\n", "\n"))
    entries = REGISTRY_ENTRY_PATTERN.findall(body.group(1)) if body else []
    if not entries:
        raise InputError(f"등록표를 찾지 못함: {registry_path} GetRegisteredStructs")
    registry = {}
    for struct_name, scope in entries:
        if scope not in SECTION_BY_SCOPE:
            raise InputError(f"등록표 {struct_name}: 알 수 없는 범위 {scope}")
        if struct_name in registry:
            raise InputError(f"등록표 {struct_name}: 두 번 등록됨")
        registry[struct_name] = scope
    return registry


def read_headers(root):
    header_folder = root.joinpath(*HEADER_FOLDER_RELATIVE_PATH)
    header_paths = sorted(header_folder.glob("*.h")) if header_folder.is_dir() else []
    if not header_paths:
        raise InputError(f"헤더 없음: {header_folder}")
    enums = {}
    declarations = {}
    for header_path in header_paths:
        text = header_path.read_text(encoding="utf-8").replace("\r\n", "\n")
        for enum_name, body in ENUM_PATTERN.findall(text):
            enums[enum_name] = parse_enum_names(body)
        for struct_name, base_name, body in STRUCT_PATTERN.findall(text):
            declarations[struct_name] = (base_name, [parse_property(*match) for match in PROPERTY_PATTERN.findall(body)])
    return enums, declarations


def merge_base_fields(declarations):
    structs = {}

    def resolve(struct_name, chain):
        if struct_name in structs:
            return structs[struct_name]
        if struct_name in chain:
            raise InputError(f"상위 구조체 순환: {' -> '.join(chain + (struct_name,))}")
        base_name, own_fields = declarations[struct_name]
        if base_name and base_name not in declarations:
            structs[struct_name] = None
            return None
        base_fields = resolve(base_name, chain + (struct_name,)) if base_name else []
        structs[struct_name] = None if base_fields is None else base_fields + own_fields
        return structs[struct_name]

    for struct_name in declarations:
        resolve(struct_name, ())
    return {name: fields for name, fields in structs.items() if fields is not None}, [name for name, fields in structs.items() if fields is None]


def check_field_types(schema, struct_name, unresolved_structs, checked_structs):
    if struct_name in checked_structs:
        return
    checked_structs.add(struct_name)
    if struct_name in unresolved_structs:
        raise InputError(f"상위 구조체를 FluidFX 헤더에서 찾지 못함: F{struct_name}")
    for field in schema.fields(struct_name):
        check_type_supported(schema, field["type"], f"{struct_name}.{field['name']}", unresolved_structs, checked_structs)
        if schema.type_kind(field["type"])[0] == "array" and field["default"] not in EMPTY_ARRAY_DEFAULTS:
            raise InputError(f"배열 헤더 기본값은 빈 배열만 허용: {struct_name}.{field['name']} = {field['default']}")


def check_type_supported(schema, type_name, path, unresolved_structs, checked_structs):
    kind, argument = schema.type_kind(type_name)
    if kind == "unsupported" and type_name.startswith("F") and type_name[1:] in unresolved_structs:
        raise InputError(f"상위 구조체를 FluidFX 헤더에서 찾지 못함: {type_name}")
    if kind == "unsupported":
        raise InputError(f"지원하지 않는 필드 형식: {path} {type_name}")
    if kind == "array":
        if schema.type_kind(argument)[0] == "array":
            raise InputError(f"지원하지 않는 필드 형식(배열의 배열): {path} {type_name}")
        check_type_supported(schema, argument, f"{path}[]", unresolved_structs, checked_structs)
    if kind == "struct":
        check_field_types(schema, argument, unresolved_structs, checked_structs)


def load_schema(root=None):
    root = Path(root) if root else ROOT
    registry = read_registry(root)
    enums, declarations = read_headers(root)
    structs, unresolved_structs = merge_base_fields(declarations)
    missing = [name for name in registry if name not in structs and name not in unresolved_structs] + ([PRESET_KIND_ENUM] if PRESET_KIND_ENUM not in enums else [])
    if missing:
        raise InputError("헤더에서 찾지 못함: " + ", ".join(missing))
    schema = FluidPresetSchema(enums, structs, registry, root)
    checked_structs = set()
    for struct_name in registry:
        check_field_types(schema, struct_name, unresolved_structs, checked_structs)
    return schema


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


def is_finite_number(value):
    if not is_number(value):
        return False
    try:
        return math.isfinite(value)
    except OverflowError:
        return False


def check_scalar(type_name, value, path):
    if type_name == "bool":
        return [] if isinstance(value, bool) else [f"{path}: true 또는 false 여야 함, 값 {value!r}"]
    if not is_number(value):
        return [f"{path}: 숫자여야 함, 값 {value!r}"]
    if not is_finite_number(value):
        return [f"{path}: 유한한 숫자여야 함"]
    if type_name in INTEGER_RANGES:
        if isinstance(value, float) and not value.is_integer():
            return [f"{path}: 정수여야 함, 값 {value!r}"]
        if not INTEGER_RANGES[type_name][0] <= value <= INTEGER_RANGES[type_name][1]:
            return [f"{path}: {type_name} 범위 밖, 값 {value!r}"]
    if type_name == "float" and abs(value) > FLOAT32_MAX:
        return [f"{path}: float 범위 밖, 값 {value!r}"]
    return []


def find_package_file(root, package_path):
    content_path = root.joinpath("Content", *package_path[len("/Game/"):].split("/"))
    return next((content_path.with_name(content_path.name + suffix) for suffix in PACKAGE_SUFFIXES if content_path.with_name(content_path.name + suffix).is_file()), None)


def check_soft_path(root, value, path):
    if not isinstance(value, str):
        return [f"{path}: /Game/ 경로 문자열이어야 함, 값 {value!r}"]
    if not value:
        return []
    if not GAME_PATH_PATTERN.fullmatch(value):
        return [f"{path}: {value!r} 는 빈 문자열이나 /Game/폴더/에셋[.객체] 경로가 아님"]
    package_path = value.split(".", 1)[0]
    if find_package_file(root, package_path) is None:
        return [f"{path}: 패키지 {package_path} 가 Content 에 없음({', '.join(PACKAGE_SUFFIXES)})"]
    return []


def check_typed_value(schema, type_name, value, path):
    if value is None:
        return [f"{path}: null 은 값이 아님"]
    kind, argument = schema.type_kind(type_name)
    if kind == "enum":
        if not isinstance(value, str):
            return [f"{path}: {argument} 이름 문자열이어야 함, 값 {value!r}"]
        return [] if value in schema.enums[argument] else [f"{path}: {argument} 에 없는 이름 {value!r}(대소문자 구분)"]
    if kind == "component":
        component_names, component_type = COMPONENT_TYPES[argument]
        if not isinstance(value, dict):
            return [f"{path}: {argument} 성분 객체여야 함, 값 {value!r}"]
        errors = [f"{path}.{key}: {argument} 에 없는 성분" for key in value if key not in component_names]
        errors.extend(f"{path}: 성분 {name} 없음" for name in component_names if name not in value)
        for name in component_names:
            if name in value:
                errors.extend(check_scalar(component_type, value[name], f"{path}.{name}"))
        return errors
    if kind == "array":
        if not isinstance(value, list):
            return [f"{path}: 배열이어야 함, 값 {value!r}"]
        errors = []
        for index, element in enumerate(value):
            errors.extend(check_typed_value(schema, argument, element, f"{path}[{index}]"))
        return errors
    if kind == "name":
        return [] if isinstance(value, str) else [f"{path}: 이름 문자열이어야 함, 값 {value!r}"]
    if kind == "soft_path":
        return check_soft_path(schema.root, value, path)
    if kind == "struct":
        if not isinstance(value, dict):
            return [f"{path}: F{argument} 성분 객체여야 함, 값 {value!r}"]
        errors, _ = check_fields(schema, argument, value, path, True)
        return errors
    return check_scalar(argument, value, path)


def check_value(schema, field, value, path):
    return check_typed_value(schema, field["type"], value, path)


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
        errors.extend(f"{path}: 필드 {name} 없음(모든 필드 필수)" for name in declared if name not in fields)
    return errors, warnings


def check_struct_scope(schema, struct_name, scope, path):
    if struct_name not in schema.registry:
        return [f"{path}: 알 수 없는 구조체 이름(대소문자 구분, 허용 {', '.join(schema.registered_names(scope))})"]
    if schema.registry[struct_name] != scope:
        return [f"{path}: {struct_name} 는 {SECTION_BY_SCOPE[schema.registry[struct_name]]} 에서만 정함"]
    return []


def check_struct_entries(schema, entries, path, ignored_keys=()):
    errors = []
    warnings = []
    assigned = []
    for struct_name, fields in entries.items():
        if struct_name in ignored_keys:
            continue
        scope_errors = check_struct_scope(schema, struct_name, PRESET_KIND_SCOPE, f"{path}.{struct_name}")
        if scope_errors:
            errors.extend(scope_errors)
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


def check_scoped_section(schema, section, scope):
    section_key = SECTION_BY_SCOPE[scope]
    errors = []
    warnings = []
    for struct_name in section:
        errors.extend(check_struct_scope(schema, struct_name, scope, f"{section_key}.{struct_name}"))
    for struct_name in schema.registered_names(scope):
        if struct_name not in section:
            errors.append(f"{section_key}: 구조체 {struct_name} 없음")
            continue
        field_errors, field_warnings = check_fields(schema, struct_name, section[struct_name], f"{section_key}.{struct_name}", True)
        errors.extend(field_errors)
        warnings.extend(field_warnings)
    return errors, warnings


def check_document(schema, document):
    errors = []
    warnings = []
    if not isinstance(document, dict):
        return ["최상위는 객체여야 함"], warnings
    errors.extend(f"최상위 키 {key!r} 는 허용되지 않음(허용 {', '.join(TOP_LEVEL_KEYS)})" for key in document if key not in TOP_LEVEL_KEYS)
    errors.extend(f"최상위 키 {key} 없음" for key in TOP_LEVEL_KEYS if key not in document)
    sections = {key: document.get(key, {}) for key in TOP_LEVEL_KEYS}
    errors.extend(f"{key}: 객체여야 함" for key, value in sections.items() if not isinstance(value, dict))
    if errors:
        return errors, warnings

    for scope in SECTION_BY_SCOPE:
        section_errors, section_warnings = check_scoped_section(schema, sections[SECTION_BY_SCOPE[scope]], scope)
        errors.extend(section_errors)
        warnings.extend(section_warnings)

    owner_by_target = {}
    known_kinds = schema.kinds()
    for group_name, group in sections["Groups"].items():
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

    for kind_name, entries in sections["Kinds"].items():
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


def collect_soft_path_values(schema, document):
    values = []

    def walk_value(type_name, value, path):
        kind, argument = schema.type_kind(type_name)
        if kind == "soft_path" and isinstance(value, str) and value:
            values.append((path, value))
        elif kind == "array" and isinstance(value, list):
            for index, element in enumerate(value):
                walk_value(argument, element, f"{path}[{index}]")
        elif kind == "struct" and isinstance(value, dict):
            walk_fields(argument, value, path)

    def walk_fields(struct_name, fields, path):
        declared = {field["name"]: field for field in schema.fields(struct_name)}
        for key, value in fields.items():
            if key in declared:
                walk_value(declared[key]["type"], value, f"{path}.{key}")

    for section_key in TOP_LEVEL_KEYS:
        section = document.get(section_key)
        if not isinstance(section, dict):
            continue
        for owner_name, entries in section.items():
            if section_key in SECTION_BY_SCOPE.values():
                owned_entries = [(owner_name, entries)]
                owner_path = section_key
            else:
                owned_entries = list(entries.items()) if isinstance(entries, dict) else []
                owner_path = f"{section_key}.{owner_name}"
            for struct_name, fields in owned_entries:
                if struct_name in schema.registry and isinstance(fields, dict):
                    walk_fields(struct_name, fields, f"{owner_path}.{struct_name}")
    return values


def collect_tuning_soft_paths(root):
    root = Path(root)
    tuning_path = root.joinpath(*TUNING_RELATIVE_PATH)
    if not tuning_path.is_file() or not root.joinpath(*REGISTRY_RELATIVE_PATH).is_file():
        return []
    schema = load_schema(root)
    document, _ = read_document(tuning_path)
    return collect_soft_path_values(schema, document) if isinstance(document, dict) else []


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


def collect_runtime_parameter_names(root):
    source_folder = root.joinpath(*RUNTIME_SOURCE_RELATIVE_PATH)
    material_names = {}
    niagara_names = {}
    source_paths = sorted(source_folder.rglob("*")) if source_folder.is_dir() else []
    for source_path in source_paths:
        if not source_path.is_file() or source_path.suffix not in RUNTIME_SOURCE_SUFFIXES:
            continue
        if RUNTIME_SOURCE_EXCLUDED_FOLDER in source_path.relative_to(source_folder).parts:
            continue
        text = source_path.read_text(encoding="utf-8")
        location = source_path.relative_to(root).as_posix()
        for name in MATERIAL_PARAMETER_SET_PATTERN.findall(text):
            material_names.setdefault(name.casefold(), location)
        for name in NIAGARA_USER_NAME_PATTERN.findall(text):
            niagara_names.setdefault(name.casefold(), location)
    return material_names, niagara_names


def check_look_value(value_kind, value, path):
    if value_kind == "bool":
        return [] if isinstance(value, bool) else [f"{path}: true 또는 false 여야 함, 값 {value!r}"]
    if isinstance(value_kind, int):
        is_number_list = isinstance(value, list) and len(value) == value_kind and all(is_finite_number(element) for element in value)
        return [] if is_number_list else [f"{path}: 유한한 숫자 {value_kind}개 배열이어야 함, 값 {value!r}"]
    return check_scalar(value_kind, value, path)


def check_look_sections(entry, sections, path):
    errors = []
    for section, value_kind in sections.items():
        values = entry.get(section)
        if not isinstance(values, dict):
            errors.append(f"{path}.{section}: 객체여야 함")
            continue
        for name, value in values.items():
            if not name:
                errors.append(f"{path}.{section}: 빈 이름")
                continue
            errors.extend(check_look_value(value_kind, value, f"{path}.{section}.{name}"))
    return errors


def check_look_entry_shape(key, entry, path, allowed_keys):
    if not GAME_PACKAGE_PATTERN.fullmatch(key):
        return [f"{path}: 키는 /Game/폴더/에셋 패키지 경로여야 함(객체 이름 없이)"]
    if not isinstance(entry, dict):
        return [f"{path}: 객체여야 함"]
    return [f"{path}: 키 {name!r} 는 허용되지 않음(허용 {', '.join(allowed_keys)})" for name in entry if name not in allowed_keys]


def check_look_packages(root, path, package_paths):
    warnings = []
    for package_path in package_paths:
        if find_package_file(root, package_path) is None:
            warnings.append(f"{path}: 패키지 {package_path} 가 Content 에 없음(굽기 전 새 항목이면 정상, 아니면 이름 오타라 런타임에 적용되지 않음)")
    return warnings


def collect_runtime_name_hits(entries, sections, runtime_names, path_prefix):
    hits = {}
    for key, entry in entries:
        for section in sections:
            values = entry.get(section)
            if not isinstance(values, dict):
                continue
            for name in values:
                source_location = runtime_names.get(name.casefold())
                if source_location is not None:
                    hits.setdefault(name.casefold(), (name, source_location, []))[2].append(f"{path_prefix}.{key}.{section}.{name}")
    return hits


def classify_runtime_name_hits(hits):
    errors = []
    accepted_reasons = {name.casefold(): reason for name, reason in {**CONDITIONAL_RUNTIME_NAMES, **BASELINE_BAKED_RUNTIME_NAMES}.items()}
    accepted_hits_by_reason = {}
    for folded_name, (name, source_location, paths) in sorted(hits.items()):
        reason = accepted_reasons.get(folded_name)
        if reason is None:
            errors.extend(f"{path}: {name} 는 C++ 가 런타임에 설정하는 이름({source_location})이라 모양 파일에 둘 수 없음(R7)" for path in paths)
            continue
        accepted_hits_by_reason.setdefault(reason, []).append((name, len(paths)))
    warnings = [
        f"모양: C++ 가 런타임에 설정하는 이름 {', '.join(f'{name}×{count}' for name, count in named_counts)} — {reason}"
        for reason, named_counts in accepted_hits_by_reason.items()
    ]
    return errors, warnings


def check_look_document(root, document):
    if not isinstance(document, dict):
        return ["모양: 최상위는 객체여야 함"], []
    errors = [f"모양: 최상위 키 {key!r} 는 허용되지 않음(허용 {', '.join(LOOK_TOP_LEVEL_KEYS)})" for key in document if key not in LOOK_TOP_LEVEL_KEYS]
    errors.extend(f"모양.{key}: 객체여야 함" for key in LOOK_TOP_LEVEL_KEYS if not isinstance(document.get(key), dict))
    if any(not isinstance(document.get(key), dict) for key in LOOK_TOP_LEVEL_KEYS):
        return errors, []

    warnings = []
    valid_materials = []
    material_allowed_keys = (LOOK_PARENT_KEY,) + tuple(LOOK_MATERIAL_SECTIONS)
    for key, entry in document[LOOK_MATERIALS_KEY].items():
        path = f"모양.{LOOK_MATERIALS_KEY}.{key}"
        shape_errors = check_look_entry_shape(key, entry, path, material_allowed_keys)
        errors.extend(shape_errors)
        if not isinstance(entry, dict) or not GAME_PACKAGE_PATTERN.fullmatch(key):
            continue
        parent = entry.get(LOOK_PARENT_KEY)
        package_paths = [key]
        if isinstance(parent, str) and GAME_PATH_PATTERN.fullmatch(parent):
            package_paths.append(parent.split(".", 1)[0])
        else:
            errors.append(f"{path}.{LOOK_PARENT_KEY}: /Game/ 경로 문자열이어야 함, 값 {parent!r}")
        errors.extend(check_look_sections(entry, LOOK_MATERIAL_SECTIONS, path))
        warnings.extend(check_look_packages(root, path, package_paths))
        valid_materials.append((key, entry))

    valid_systems = []
    for key, entry in document[LOOK_NIAGARA_KEY].items():
        path = f"모양.{LOOK_NIAGARA_KEY}.{key}"
        errors.extend(check_look_entry_shape(key, entry, path, tuple(LOOK_NIAGARA_SECTIONS)))
        if not isinstance(entry, dict) or not GAME_PACKAGE_PATTERN.fullmatch(key):
            continue
        errors.extend(check_look_sections(entry, LOOK_NIAGARA_SECTIONS, path))
        warnings.extend(check_look_packages(root, path, [key]))
        valid_systems.append((key, entry))

    material_names, niagara_names = collect_runtime_parameter_names(root)
    hits = collect_runtime_name_hits(valid_materials, LOOK_MID_OVERRIDE_SECTIONS, material_names, f"모양.{LOOK_MATERIALS_KEY}")
    hits.update(collect_runtime_name_hits(valid_systems, tuple(LOOK_NIAGARA_SECTIONS), niagara_names, f"모양.{LOOK_NIAGARA_KEY}"))
    name_errors, name_warnings = classify_runtime_name_hits(hits)
    return errors + name_errors, warnings + name_warnings


def inspect_look_file(path, root=None):
    root = Path(root) if root else ROOT
    if not Path(path).is_file():
        return [f"모양 정본 JSON 없음: {path}"], []
    document, read_errors = read_document(path)
    if document is None:
        return [f"모양: {error}" for error in read_errors], []
    errors, warnings = check_look_document(root, document)
    return [f"모양: {error}" for error in read_errors] + errors, warnings


def file_sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None


def check_stage_reports(report_paths, tuning_path, look_path):
    current_tuning = file_sha256(tuning_path)
    current_look = file_sha256(look_path)
    stale = []
    warnings = []
    for report_path in report_paths:
        report_path = Path(report_path)
        if not report_path.is_file():
            raise InputError(f"무대 보고 없음: {report_path}")
        report = json.loads(report_path.read_text(encoding="utf-8"))
        report = report if isinstance(report, dict) else {}
        recorded_tuning = report.get("preset_tuning_sha256")
        if recorded_tuning is None:
            stale.append(f"{report_path}: preset_tuning_sha256 없음(정본 도입 전 무대 또는 실패 보고)")
        elif recorded_tuning != current_tuning:
            stale.append(f"{report_path}: 무대가 만든 정본 {recorded_tuning[:12]} 와 현재 {str(current_tuning)[:12]} 가 다름(그 무대 재생성 필요)")
        recorded_look = report.get("material_look_sha256")
        if recorded_look is None:
            warnings.append(f"{report_path}: material_look_sha256 없음(무대 스크립트가 모양 정본 판을 기록하지 않아 모양 STALE 판정 불가)")
        elif recorded_look != current_look:
            stale.append(f"{report_path}: 무대가 만든 모양 정본 {recorded_look[:12]} 와 현재 {str(current_look)[:12]} 가 다름(그 무대 재생성 필요)")
    return stale, warnings


def resolve_checked_path(requested_path, default_path):
    if requested_path is None:
        return default_path
    checked_path = Path(requested_path).resolve()
    if not checked_path.is_file():
        raise InputError(f"지정한 파일 없음: {checked_path}")
    return checked_path


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description="FluidFX 프리셋 정본 JSON 검사(spec-pd-b 4절, spec-wave0-data-bridge-v2 2.5)")
    parser.add_argument("--root", default=str(ROOT))
    parser.add_argument("--stage-report", action="append", default=[])
    parser.add_argument("--tuning", help="검사할 조절 JSON(기본: 루트의 정본 TDFluidPresetTuning.json)")
    parser.add_argument("--look", help="검사할 모양 JSON(기본: 루트의 정본 TDFluidMaterialLook.json)")
    arguments = parser.parse_args()
    root = Path(arguments.root).resolve()
    try:
        tuning_path = resolve_checked_path(arguments.tuning, root.joinpath(*TUNING_RELATIVE_PATH))
        look_path = resolve_checked_path(arguments.look, root.joinpath(*LOOK_RELATIVE_PATH))
        errors, warnings = inspect_tuning_file(tuning_path, root)
        look_errors, look_warnings = inspect_look_file(look_path, root)
        errors.extend(look_errors)
        warnings.extend(look_warnings)
        errors.extend(check_game_ini(root))
        stale, report_warnings = check_stage_reports(arguments.stage_report, tuning_path, look_path)
        warnings.extend(report_warnings)
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
