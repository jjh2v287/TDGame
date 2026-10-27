"""실행 환경: TDGame 언리얼 에디터 Python(PIE 아님, 읽기 전용·저장 없음·맵을 열지 않음). 나이아가라 User 값은 에디터 Python 으로 못 읽어 먼저 python -B Tools/FluidFX/editor_make_fluid_ember_niagara.py --dump-only 가 만든 Saved/FluidFX/niagara/<시스템 이름>.dump.json 의 UserVariables 절을 읽는다
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_dump_fluid_material_look.py (PowerShell)
출력: Saved/FluidFX/material_look_dump.json(키 정렬, 실수는 float32 최단 표기. Materials=/Game/FluidFX 재질 인스턴스가 덮어쓴 스칼라·벡터[r,g,b,a]·부모와 값이 다른 정적 스위치, NiagaraSystems=User 변수 기본값(Floats·Ints·Bools·Vectors2·Vectors3·Colors, 키는 User.이름 그대로), DumpInfo=덮어쓰기 텍스처·마스터에 없는 낡은 덮어쓰기(StaleOverrides, 값에서 뺌)·건너뛴 항목·나이아가라 원본 파일·오류), 오류(배열 값 ≠ MaterialEditingLibrary 값, 덮어쓰기 0개, 낡은 나이아가라 덤프 포함)가 있으면 파일을 쓴 뒤 종료 1
상태: 검증됨(2026-10-27 파동 2 A6 에디터 실행: 재질 인스턴스 91·나이아가라 9, 오류 0, fluid_material_look.py compare 차이는 에셋이 없는 나이아가라 4개뿐)
"""
import json
import math
import os
import struct

FLUID_ROOT = "/Game/FluidFX"
MATERIAL_INSTANCE_CLASS_NAME = "MaterialInstanceConstant"
NIAGARA_SYSTEM_CLASS_NAME = "NiagaraSystem"
NIAGARA_DUMP_DIRECTORY = ("Saved", "FluidFX", "niagara")
OUTPUT_PATH = ("Saved", "FluidFX", "material_look_dump.json")
INDENT = " "
FORMAT_VERSION = 1
NIAGARA_FLOAT_TYPE = "/Script/Niagara.NiagaraFloat"
NIAGARA_INT_TYPE = "/Script/Niagara.NiagaraInt32"
NIAGARA_BOOL_TYPE = "/Script/Niagara.NiagaraBool"
VECTOR2_TYPE = "/Script/CoreUObject.Vector2f"
VECTOR3_TYPE = "/Script/CoreUObject.Vector3f"
COLOR_TYPE = "/Script/CoreUObject.LinearColor"
VECTOR2_COMPONENTS = ("x", "y")
VECTOR3_COMPONENTS = ("x", "y", "z")
COLOR_COMPONENTS = ("r", "g", "b", "a")
NIAGARA_SECTIONS_BY_TYPE = {
    NIAGARA_FLOAT_TYPE: "Floats",
    NIAGARA_INT_TYPE: "Ints",
    NIAGARA_BOOL_TYPE: "Bools",
    VECTOR2_TYPE: "Vectors2",
    VECTOR3_TYPE: "Vectors3",
    COLOR_TYPE: "Colors",
}
NIAGARA_SECTION_NAMES = ("Floats", "Ints", "Bools", "Vectors2", "Vectors3", "Colors")


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


def write_text(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as output_file:
        output_file.write(text)


def to_package_path(object_path):
    return object_path.split(".")[0]


def read_editor_property(owner, property_name):
    try:
        return True, owner.get_editor_property(property_name)
    except Exception:
        return False, None


def list_assets_of_class(unreal, class_name):
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    package_names = [str(data.package_name) for data in registry.get_assets_by_path(FLUID_ROOT, recursive=True) if str(data.asset_class_path.asset_name) == class_name]
    return sorted(package_names)


def read_override_entries(instance, property_name):
    found, entries = read_editor_property(instance, property_name)
    if not found or entries is None:
        return []
    return list(entries)


def read_required_override_entries(instance, property_name):
    found, entries = read_editor_property(instance, property_name)
    if not found:
        raise RuntimeError(f"재질 인스턴스 속성을 읽지 못함: {property_name}")
    return list(entries) if entries is not None else []


def find_library_mismatches(unreal, instance, scalars, vectors):
    library = unreal.MaterialEditingLibrary
    mismatches = []
    for name, value in scalars.items():
        if shortest_float32(library.get_material_instance_scalar_parameter_value(instance, name)) != value:
            mismatches.append(name)
    for name, value in vectors.items():
        color = library.get_material_instance_vector_parameter_value(instance, name)
        if [shortest_float32(getattr(color, component)) for component in COLOR_COMPONENTS] != value:
            mismatches.append(name)
    return sorted(mismatches)


def read_parameter_name(entry):
    return str(entry.get_editor_property("parameter_info").get_editor_property("name"))


def is_global_parameter(unreal, entry):
    association = entry.get_editor_property("parameter_info").get_editor_property("association")
    return association == unreal.MaterialParameterAssociation.GLOBAL_PARAMETER


def read_scalar_and_vector_overrides(unreal, instance):
    scalars = {}
    vectors = {}
    non_global = []
    for entry in read_required_override_entries(instance, "scalar_parameter_values"):
        if not is_global_parameter(unreal, entry):
            non_global.append(read_parameter_name(entry))
            continue
        scalars[read_parameter_name(entry)] = shortest_float32(entry.get_editor_property("parameter_value"))
    for entry in read_required_override_entries(instance, "vector_parameter_values"):
        if not is_global_parameter(unreal, entry):
            non_global.append(read_parameter_name(entry))
            continue
        color = entry.get_editor_property("parameter_value")
        vectors[read_parameter_name(entry)] = [shortest_float32(getattr(color, component)) for component in COLOR_COMPONENTS]
    return scalars, vectors, sorted(non_global)


def split_stale_overrides(values, available_names):
    available = {str(name) for name in available_names}
    live = {name: value for name, value in values.items() if name in available}
    return live, sorted(name for name in values if name not in available)


def read_texture_overrides(instance):
    textures = {}
    for entry in read_override_entries(instance, "texture_parameter_values"):
        texture = entry.get_editor_property("parameter_value")
        textures[read_parameter_name(entry)] = texture.get_path_name() if texture is not None else None
    return textures


def read_switch_value(unreal, material, name):
    library = unreal.MaterialEditingLibrary
    if isinstance(material, unreal.MaterialInstanceConstant):
        return bool(library.get_material_instance_static_switch_parameter_value(material, name))
    return bool(library.get_material_default_static_switch_parameter_value(material, name))


def read_switch_overrides(unreal, instance, parent):
    if parent is None:
        return {}
    switches = {}
    for name in unreal.MaterialEditingLibrary.get_static_switch_parameter_names(instance):
        name = str(name)
        value = read_switch_value(unreal, instance, name)
        if value != read_switch_value(unreal, parent, name):
            switches[name] = value
    return switches


def dump_material_instance(unreal, package_name, info):
    instance = unreal.EditorAssetLibrary.load_asset(package_name)
    if not isinstance(instance, unreal.MaterialInstanceConstant):
        raise RuntimeError(f"재질 인스턴스가 아님: {package_name}")
    parent = instance.get_editor_property("parent")
    scalars, vectors, non_global = read_scalar_and_vector_overrides(unreal, instance)
    library = unreal.MaterialEditingLibrary
    scalars, stale_scalars = split_stale_overrides(scalars, library.get_scalar_parameter_names(instance))
    vectors, stale_vectors = split_stale_overrides(vectors, library.get_vector_parameter_names(instance))
    if stale_scalars or stale_vectors:
        info["StaleOverrides"][package_name] = sorted(stale_scalars + stale_vectors)
    library_mismatches = find_library_mismatches(unreal, instance, scalars, vectors)
    if library_mismatches:
        info["Errors"].append(f"{package_name}: 덮어쓰기 배열 값과 MaterialEditingLibrary 값이 다름: {', '.join(library_mismatches)}")
    textures = read_texture_overrides(instance)
    if textures:
        info["TextureOverrides"][package_name] = textures
    if non_global:
        info["NonGlobalOverrides"][package_name] = non_global
    double_vectors = [read_parameter_name(entry) for entry in read_override_entries(instance, "double_vector_parameter_values")]
    if double_vectors:
        info["DoubleVectorOverrides"][package_name] = sorted(double_vectors)
    return {
        "Parent": to_package_path(parent.get_path_name()) if parent is not None else None,
        "Scalars": scalars,
        "Vectors": vectors,
        "Switches": read_switch_overrides(unreal, instance, parent),
    }


def read_asset_modified_time(project_directory, package_name):
    relative = package_name[len("/Game/"):].split("/")
    asset_path = os.path.join(project_directory, "Content", *relative) + ".uasset"
    return os.path.getmtime(asset_path) if os.path.isfile(asset_path) else None


def variable_value_list(default_value, components):
    return [shortest_float32(default_value[component]) for component in components]


def convert_niagara_variable(variable, system):
    type_path = variable["type"]["classStructOrEnum"]["refPath"]
    default_value = variable["defaultValue"]["value"]
    section = NIAGARA_SECTIONS_BY_TYPE.get(type_path)
    name = variable["name"]
    if section == "Floats":
        system["Floats"][name] = shortest_float32(default_value["value"])
    elif section == "Ints":
        system["Ints"][name] = int(default_value["value"])
    elif section == "Bools":
        system["Bools"][name] = bool(default_value["value"])
    elif section == "Vectors2":
        system["Vectors2"][name] = variable_value_list(default_value, VECTOR2_COMPONENTS)
    elif section == "Vectors3":
        system["Vectors3"][name] = variable_value_list(default_value, VECTOR3_COMPONENTS)
    elif section == "Colors":
        system["Colors"][name] = variable_value_list(default_value, COLOR_COMPONENTS)
    return section is not None


def dump_niagara_system(project_directory, package_name, info):
    system = {section: {} for section in NIAGARA_SECTION_NAMES}
    dump_path = os.path.join(project_directory, *NIAGARA_DUMP_DIRECTORY, package_name.rsplit("/", 1)[1] + ".dump.json")
    if not os.path.isfile(dump_path):
        info["Errors"].append(f"나이아가라 덤프 없음(editor_make_fluid_ember_niagara.py --dump-only 먼저): {dump_path}")
        return system
    asset_time = read_asset_modified_time(project_directory, package_name)
    is_stale = asset_time is not None and os.path.getmtime(dump_path) < asset_time
    relative_dump_path = "/".join(NIAGARA_DUMP_DIRECTORY + (os.path.basename(dump_path),))
    info["NiagaraSources"][package_name] = {"File": relative_dump_path, "IsStale": is_stale}
    if is_stale:
        info["Errors"].append(f"나이아가라 덤프가 에셋보다 오래됨(--dump-only 다시): {dump_path}")
    with open(dump_path, encoding="utf-8") as dump_file:
        variables = json.load(dump_file)["sections"]["UserVariables"]["userVariables"]
    skipped_objects = []
    unsupported = []
    for variable in variables:
        default_value = variable["defaultValue"]["value"]
        if "objectClass" in default_value:
            skipped_objects.append(variable["name"])
        elif not convert_niagara_variable(variable, system):
            unsupported.append({"Name": variable["name"], "Type": variable["type"]["classStructOrEnum"]["refPath"]})
    if skipped_objects:
        info["SkippedObjectUserVariables"][package_name] = sorted(skipped_objects)
    if unsupported:
        info["UnsupportedUserVariables"][package_name] = sorted(unsupported, key=lambda item: item["Name"])
    return system


def make_dump_info():
    return {
        "FormatVersion": FORMAT_VERSION,
        "TextureOverrides": {},
        "NonGlobalOverrides": {},
        "DoubleVectorOverrides": {},
        "StaleOverrides": {},
        "SkippedObjectUserVariables": {},
        "UnsupportedUserVariables": {},
        "NiagaraSources": {},
        "Errors": [],
    }


def main():
    import unreal

    project_directory = unreal.SystemLibrary.get_project_directory()
    info = make_dump_info()
    materials = {}
    for package_name in list_assets_of_class(unreal, MATERIAL_INSTANCE_CLASS_NAME):
        try:
            materials[package_name] = dump_material_instance(unreal, package_name, info)
        except Exception as error:
            info["Errors"].append(f"{package_name}: {error}")
    if materials and not any(look["Scalars"] or look["Vectors"] for look in materials.values()):
        info["Errors"].append("재질 인스턴스 덮어쓰기 스칼라·벡터가 하나도 없음(속성 읽기 실패 의심)")
    niagara_systems = {package_name: dump_niagara_system(project_directory, package_name, info) for package_name in list_assets_of_class(unreal, NIAGARA_SYSTEM_CLASS_NAME)}
    info["MaterialInstanceCount"] = len(materials)
    info["NiagaraSystemCount"] = len(niagara_systems)
    output_path = os.path.join(project_directory, *OUTPUT_PATH)
    write_text(output_path, format_json({"Materials": materials, "NiagaraSystems": niagara_systems, "DumpInfo": info}) + "\n")
    print(json.dumps({
        "status": "ok" if not info["Errors"] else "incomplete",
        "output": output_path,
        "material_instance_count": len(materials),
        "niagara_system_count": len(niagara_systems),
        "errors": info["Errors"],
    }, ensure_ascii=False))
    if info["Errors"]:
        raise RuntimeError("모양 덤프가 완전하지 않음: " + " | ".join(info["Errors"]))


if __name__ == "__main__":
    main()
