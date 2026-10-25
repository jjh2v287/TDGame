"""실행 환경: 시스템 Python(에디터 밖), 작업 트리 루트 기준(--root, 기본은 이 파일의 두 단계 위). 헤더(Source/TDGame/FluidFX/TDFluidTypes.h·TDFluidFireTypes.h)에서 필드 형식과 Clamp 범위를 읽는다
실행 명령: python Tools/FluidFX/make_fluid_golden_snapshots.py --dump <기준선 preset_builder_dump.json> [--check] [--structs TDFluidSimulationConfig] [--output Source/TDGame/FluidFX/Tests/TDFluidGoldenPresetSnapshots.h]
출력: 골든 시험 입력 헤더(종류별 빌더 값 중 Custom 빌더 값=헤더 기본값과 다른 필드만, float.hex 를 16진 부동소수 리터럴로). --check 는 쓰지 않고 기존 파일과 비교해 다르면 종료 1, 덤프·헤더 불일치는 종료 2
상태: 2026-10-25 파동 0 묶음 D 작성, 기준선 덤프(w0-baseline/dumps)로 생성·py_compile 만 확인, 골든 해시 통과로 스냅숏 정확성 확인은 빌드 뒤
"""
import argparse
import json
import os
import re
import struct
import sys

ROOT_DEFAULT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEADER_PATHS = (
    os.path.join("Source", "TDGame", "FluidFX", "TDFluidPlaneTypes.h"),
    os.path.join("Source", "TDGame", "FluidFX", "TDFluidTypes.h"),
    os.path.join("Source", "TDGame", "FluidFX", "TDFluidFireTypes.h"),
)
OUTPUT_DEFAULT = os.path.join("Source", "TDGame", "FluidFX", "Tests", "TDFluidGoldenPresetSnapshots.h")
DEFAULT_STRUCTS = ("TDFluidSimulationConfig",)
REFERENCE_KIND = "Custom"
PRESET_KIND_ENUM = "ETDFluidPresetKind"
STRUCT_INCLUDES = {
    "TDFluidTypes.h": "FluidFX/TDFluidTypes.h",
    "TDFluidFireTypes.h": "FluidFX/TDFluidFireTypes.h",
    "TDFluidPlaneTypes.h": "FluidFX/TDFluidPlaneTypes.h",
}
COMPONENT_TYPES = {
    "FVector2D": (("X", "Y"), "double"),
    "FVector": (("X", "Y", "Z"), "double"),
    "FLinearColor": (("R", "G", "B", "A"), "float"),
}
INTEGER_TYPES = ("int32", "uint8", "int64", "uint32")
FIELD_LINE = re.compile(r"^\s*(?P<type>[A-Za-z_][\w:<>, ]*?)\s+(?P<name>[A-Za-z_]\w*)\s*(?:=[^;]*)?;\s*$")
CLAMP_META = re.compile(r"\b(?P<key>ClampMin|ClampMax)\s*=\s*\"(?P<value>[^\"]+)\"")


class SnapshotError(Exception):
    pass


def read_text(path):
    with open(path, encoding="utf-8") as source_file:
        return source_file.read()


def find_struct_body(text, struct_name):
    match = re.search(r"struct\s+(?:TDGAME_API\s+)?F" + re.escape(struct_name) + r"\b[^;{]*\{", text)
    if not match:
        return None
    depth = 1
    index = match.end()
    while index < len(text) and depth > 0:
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
        index += 1
    return text[match.end():index - 1]


def parse_struct_fields(body):
    fields = {}
    pending_meta = ""
    for raw_line in body.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("//") or line.startswith("GENERATED_BODY"):
            continue
        if line.startswith("UPROPERTY"):
            pending_meta = line
            continue
        match = FIELD_LINE.match(line)
        if not match or "(" in match.group("type"):
            pending_meta = ""
            continue
        clamps = {item.group("key"): float(item.group("value")) for item in CLAMP_META.finditer(pending_meta)}
        fields[match.group("name")] = {"type": match.group("type").strip(), "clamp_min": clamps.get("ClampMin"), "clamp_max": clamps.get("ClampMax")}
        pending_meta = ""
    return fields


def read_struct_fields(root, struct_name):
    for header_path in HEADER_PATHS:
        text = read_text(os.path.join(root, header_path))
        body = find_struct_body(text, struct_name)
        if body is not None:
            return parse_struct_fields(body), os.path.basename(header_path)
    raise SnapshotError(f"{struct_name}: 헤더에서 구조체를 찾지 못함({', '.join(HEADER_PATHS)})")


def read_enum_values(root, enum_name):
    for header_path in HEADER_PATHS:
        text = read_text(os.path.join(root, header_path))
        match = re.search(r"enum\s+class\s+" + re.escape(enum_name) + r"\b[^{]*\{(?P<body>[^}]*)\}", text)
        if match:
            return [item.split("=")[0].strip() for item in match.group("body").split(",") if item.split("=")[0].strip()]
    raise SnapshotError(f"{enum_name}: 헤더에서 열거형을 찾지 못함")


def parse_hex(value, label):
    if not isinstance(value, str):
        raise SnapshotError(f"{label}: float.hex 문자열이 아님({value!r})")
    try:
        number = float.fromhex(value)
    except ValueError as error:
        raise SnapshotError(f"{label}: float.hex 해석 실패({value!r})") from error
    if number != number or number in (float("inf"), float("-inf")):
        raise SnapshotError(f"{label}: 유한값이 아님({value!r})")
    return number


def format_hex_literal(number, is_float, label):
    if is_float and struct.unpack("<f", struct.pack("<f", number))[0] != number:
        raise SnapshotError(f"{label}: float 로 정확히 표현되지 않음({number.hex()})")
    text = number.hex()
    sign = "-" if text.startswith("-") else ""
    mantissa, exponent = text.lstrip("-").split("p")
    if "." in mantissa:
        mantissa = mantissa.rstrip("0")
        if mantissa.endswith("."):
            mantissa += "0"
    return f"{sign}{mantissa}p{exponent}" + ("f" if is_float else "")


def format_field_value(field_type, value, label, enum_names):
    if field_type == "bool":
        if not isinstance(value, bool):
            raise SnapshotError(f"{label}: bool 이 아님({value!r})")
        return "true" if value else "false"
    if field_type in INTEGER_TYPES:
        if isinstance(value, bool) or not isinstance(value, int):
            raise SnapshotError(f"{label}: 정수가 아님({value!r})")
        return str(value)
    if field_type in ("float", "double"):
        return format_hex_literal(parse_hex(value, label), field_type == "float", label)
    if field_type in COMPONENT_TYPES:
        components, component_type = COMPONENT_TYPES[field_type]
        if not isinstance(value, dict) or sorted(value) != sorted(components):
            raise SnapshotError(f"{label}: {field_type} 성분이 맞지 않음({value!r})")
        literals = [format_hex_literal(parse_hex(value[name], f"{label}.{name}"), component_type == "float", f"{label}.{name}") for name in components]
        return f"{field_type}({', '.join(literals)})"
    if field_type in enum_names:
        if not isinstance(value, str) or value not in enum_names[field_type]:
            raise SnapshotError(f"{label}: {field_type} 값이 아님({value!r})")
        return f"{field_type}::{value}"
    raise SnapshotError(f"{label}: 지원하지 않는 형식 {field_type}")


def collect_enum_names(root, field_maps):
    enum_names = {}
    for fields in field_maps:
        for field in fields.values():
            field_type = field["type"]
            if field_type.startswith("E") and field_type not in enum_names:
                enum_names[field_type] = read_enum_values(root, field_type)
    return enum_names


def struct_function_name(struct_name):
    return "Make" + struct_name[len("TDFluid"):] if struct_name.startswith("TDFluid") else "Make" + struct_name


def build_struct_function(struct_name, fields, dump, kind_names, enum_names):
    reference = dump.get(REFERENCE_KIND, {}).get(struct_name)
    if reference is None:
        raise SnapshotError(f"{REFERENCE_KIND}.{struct_name}: 기준 종류가 덤프에 없음")
    lines = [
        f"\tinline F{struct_name} {struct_function_name(struct_name)}(ETDFluidPresetKind Kind)",
        "\t{",
        f"\t\tF{struct_name} Settings;",
        "\t\tswitch (Kind)",
        "\t\t{",
    ]
    for kind in kind_names:
        if kind == REFERENCE_KIND:
            continue
        values = dump[kind].get(struct_name)
        if values is None:
            raise SnapshotError(f"{kind}.{struct_name}: 덤프에 없음")
        unknown = sorted(set(values) - set(fields))
        if unknown:
            raise SnapshotError(f"{kind}.{struct_name}: 헤더에 없는 필드 {', '.join(unknown)}")
        assignments = []
        for field_name in sorted(values):
            if values[field_name] == reference.get(field_name):
                continue
            label = f"{kind}.{struct_name}.{field_name}"
            assignments.append(f"\t\t\tSettings.{field_name} = {format_field_value(fields[field_name]['type'], values[field_name], label, enum_names)};")
        if not assignments:
            continue
        lines.append(f"\t\tcase ETDFluidPresetKind::{kind}:")
        lines.extend(assignments)
        lines.append("\t\t\tbreak;")
    lines.extend(["\t\tdefault:", "\t\t\tbreak;", "\t\t}", "\t\treturn Settings;", "\t}"])
    return lines


def build_header(root, dump, struct_names):
    kind_order = read_enum_values(root, PRESET_KIND_ENUM)
    unknown_kinds = sorted(set(dump) - set(kind_order))
    if unknown_kinds:
        raise SnapshotError(f"덤프의 종류가 {PRESET_KIND_ENUM} 에 없음: {', '.join(unknown_kinds)}")
    kind_names = [kind for kind in kind_order if kind in dump]
    field_maps = {}
    includes = set()
    for struct_name in struct_names:
        fields, header_name = read_struct_fields(root, struct_name)
        field_maps[struct_name] = fields
        includes.add(STRUCT_INCLUDES[header_name])
    includes.add(STRUCT_INCLUDES["TDFluidTypes.h"])
    enum_names = collect_enum_names(root, field_maps.values())
    lines = ["#pragma once", ""]
    lines.extend(f"#include \"{include}\"" for include in sorted(includes))
    lines.extend(["", "namespace TDFluidGoldenPresetSnapshots", "{"])
    for index, struct_name in enumerate(struct_names):
        if index:
            lines.append("")
        lines.extend(build_struct_function(struct_name, field_maps[struct_name], dump, kind_names, enum_names))
    lines.extend(["}", ""])
    return "\r\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="기준선 빌더 덤프에서 골든 시험용 프리셋 스냅숏 헤더를 만든다")
    parser.add_argument("--dump", required=True, help="기준선 preset_builder_dump.json(editor_dump_fluid_preset_builders.py 출력)")
    parser.add_argument("--root", default=ROOT_DEFAULT)
    parser.add_argument("--output", default=OUTPUT_DEFAULT, help="--root 기준 상대 경로 또는 절대 경로")
    parser.add_argument("--structs", default=",".join(DEFAULT_STRUCTS), help="쉼표로 구분한 구조체 이름(F 접두어 없이)")
    parser.add_argument("--check", action="store_true", help="쓰지 않고 기존 파일과 비교만 한다")
    args = parser.parse_args()
    sys.stdout.reconfigure(errors="replace")

    struct_names = [name.strip() for name in args.structs.split(",") if name.strip()]
    try:
        with open(args.dump, encoding="utf-8") as dump_file:
            dump = json.load(dump_file)
        header_text = build_header(args.root, dump, struct_names)
    except (OSError, ValueError, SnapshotError) as error:
        print(f"오류: {error}")
        return 2

    output_path = args.output if os.path.isabs(args.output) else os.path.join(args.root, args.output)
    if args.check:
        existing = open(output_path, encoding="utf-8", newline="").read() if os.path.exists(output_path) else None
        if existing != header_text:
            print(f"다름: {output_path} 는 {args.dump} 에서 생성한 내용과 다르다")
            return 1
        print(f"같음: {output_path}")
        return 0

    with open(output_path, "w", encoding="utf-8", newline="") as output_file:
        output_file.write(header_text)
    print(f"생성: {output_path} ({len(header_text.splitlines())}줄, 구조체 {', '.join(struct_names)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
