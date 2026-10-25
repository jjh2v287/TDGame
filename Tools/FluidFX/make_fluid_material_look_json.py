"""실행 환경: 시스템 Python 3(에디터 불필요, --drift-against 는 git 필요)
실행 명령: python Tools/FluidFX/make_fluid_material_look_json.py [--dump Saved/FluidFX/material_look_dump.json] [--output Content/FluidFX/PresetData/TDFluidMaterialLook.json] [--check] [--drift-against ba72ebd0 [--drift-output Saved/FluidFX/material_look_drift.json]]
출력: --output 정본(덤프의 Materials·NiagaraSystems 만, 키 정렬·float32 최단 표기, 같은 덤프면 같은 바이트). --check 는 쓰지 않고 정본 바이트가 다르면 종료 1. --drift-against 는 그 판의 재질·무대·나이아가라 스크립트 표를 ast 로 읽어 '스크립트 ≠ 에셋' 목록(정보용)을 --drift-output 에 쓰고 요약을 출력한다. 종료 0 = 성공, 1 = 덤프 오류·정본 불일치, 2 = 읽기·git 실패
상태: 미검증(2026-10-25 파동 0 P1 작성: 기준선 덤프로 정본 생성·compare 차이 0·드리프트 1회만, 에디터 실행 전, spec-wave0-data-bridge-v2 2.4·3.6)
"""
import argparse
import ast
import json
import os
import subprocess
import sys

TOOLS_FLUID_DIRECTORY = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(os.path.dirname(TOOLS_FLUID_DIRECTORY))
sys.path.insert(0, TOOLS_FLUID_DIRECTORY)
import fluid_material_look

DEFAULT_DUMP_PATH = os.path.join(PROJECT_ROOT, "Saved", "FluidFX", "material_look_dump.json")
DEFAULT_OUTPUT_PATH = os.path.join(PROJECT_ROOT, *fluid_material_look.LOOK_RELATIVE_PATH)
DEFAULT_DRIFT_OUTPUT_PATH = os.path.join(PROJECT_ROOT, "Saved", "FluidFX", "material_look_drift.json")
SCRIPT_DIRECTORY = "Tools/FluidFX"
SAFE_BUILTINS = {
    "dict": dict, "tuple": tuple, "list": list, "set": set, "range": range, "len": len, "sorted": sorted,
    "str": str, "float": float, "int": int, "zip": zip, "enumerate": enumerate, "min": min, "max": max,
}
NIAGARA_SECTION_BY_TYPE_NAME = {"FLOAT_TYPE": "Floats", "VECTOR2_TYPE": "Vectors2", "VECTOR3_TYPE": "Vectors3"}
WATER_MATERIAL_SCRIPT = "editor_make_fluid_water_material.py"
FLAME_MATERIAL_SCRIPT = "editor_make_fluid_flame_material.py"
VOLUME_MATERIAL_SCRIPT = "editor_make_fluid_volume_material.py"
TEST_LEVEL_SCRIPT = "editor_make_fluid_test_level.py"
WATER_STAGE_SCRIPT = "editor_make_fluid_water_stage.py"
FIRE_STAGE_SCRIPT = "editor_make_fluid_fire_stage.py"
NIAGARA_SCRIPT = "editor_make_fluid_ember_niagara.py"
FLAME_SPEC_TABLES = (
    ("FLAME_INSTANCE_SPECS", "FLAME_MASTER_PATH"),
    ("EMBER_INSTANCE_SPECS", "EMBER_MASTER_PATH"),
    ("EMBER_NIAGARA_INSTANCE_SPECS", "EMBER_NIAGARA_MASTER_PATH"),
    ("WOOD_INSTANCE_SPECS", "WOOD_MASTER_PATH"),
    ("FRAGMENT_INSTANCE_SPECS", "FRAGMENT_MASTER_PATH"),
)
COAST_SAND_CONSTANT_PARAMETERS = (("COAST_SAND_WET_BAND_CM", "WetBandHeightCm"), ("COAST_SEA_LEVEL_Z_CM", "WaterLevelZCm"))


def read_dump(path):
    dump = fluid_material_look.read_json_file(path)
    errors = dump.get("DumpInfo", {}).get("Errors", [])
    if errors:
        raise ValueError(f"덤프 오류 {len(errors)}건: " + " | ".join(str(error) for error in errors[:5]))
    return dump


def write_text(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as output_file:
        output_file.write(text)


def read_text_or_none(path):
    if not os.path.isfile(path):
        return None
    with open(path, "r", encoding="utf-8", newline="") as source_file:
        return source_file.read()


def read_script_at_revision(revision, script_name):
    completed = subprocess.run(
        ["git", "-C", PROJECT_ROOT, "show", f"{revision}:{SCRIPT_DIRECTORY}/{script_name}"],
        capture_output=True, check=False,
    )
    if completed.returncode != 0:
        raise OSError(f"git show 실패: {revision}:{script_name} {completed.stderr.decode('utf-8', 'replace').strip()}")
    return completed.stdout.decode("utf-8")


def evaluate_expression(node, namespace):
    code = compile(ast.Expression(node), "<table>", "eval")
    return eval(code, {"__builtins__": SAFE_BUILTINS, **namespace})


def evaluate_module_constants(module_tree):
    namespace = {}
    for node in module_tree.body:
        if not isinstance(node, ast.Assign) or len(node.targets) != 1 or not isinstance(node.targets[0], ast.Name):
            continue
        try:
            namespace[node.targets[0].id] = evaluate_expression(node.value, namespace)
        except Exception:
            continue
    return namespace


def make_material_values(parent, scalars, vectors, switches):
    return {"Parent": parent, "Scalars": dict(scalars), "Vectors": dict(vectors), "Switches": dict(switches)}


def with_alpha(rgb, alpha):
    return [float(rgb[0]), float(rgb[1]), float(rgb[2]), alpha]


def read_water_material_tables(namespace):
    parent = namespace["WATER_MASTER_PATH"]
    tables = {}
    for spec in namespace["INSTANCE_SPECS"]:
        vectors = {name: with_alpha(rgb, 0.0) for name, rgb in spec["vectors"].items()}
        tables[f"{namespace['MATERIAL_DIR']}/{spec['name']}"] = make_material_values(parent, spec["scalars"], vectors, spec.get("switches", {}))
    return tables


def read_flame_material_tables(namespace):
    tables = {}
    for table_name, master_name in FLAME_SPEC_TABLES:
        for spec in namespace[table_name]:
            vectors = {name: [float(component) for component in rgba] for name, rgba in spec.get("vector_overrides", {}).items()}
            tables[f"{namespace['MATERIAL_DIR']}/{spec['name']}"] = make_material_values(namespace[master_name], spec["scalar_overrides"], vectors, {})
    return tables


def read_volume_material_tables(namespace):
    tables = {}
    for spec in namespace["INSTANCE_SPECS"]:
        vectors = {name: with_alpha(rgb, 1.0) for name, rgb in spec["vectors"].items()}
        parent = f"{namespace['MATERIAL_DIR']}/{spec['master']}"
        tables[f"{namespace['MATERIAL_DIR']}/{spec['name']}"] = make_material_values(parent, spec["scalars"], vectors, {})
    return tables


def read_test_level_tables(namespace):
    tables = {}
    for spec in namespace["PRESET_SPECS"]:
        vectors = {name: with_alpha(rgb, 1.0) for name, rgb in spec["tints"].items()}
        tables[f"{namespace['MATERIAL_DIR']}/MI_TD_Fluid_{spec['name']}"] = make_material_values(namespace["FLUID_SURFACE_MAT_PATH"], {}, vectors, {})
    return tables


def read_matte_instance_tables(module_tree, namespace):
    tables = {}
    for node in ast.walk(module_tree):
        if not isinstance(node, ast.Call) or not isinstance(node.func, ast.Name) or node.func.id != "write_matte_instance":
            continue
        if len(node.args) != 4:
            continue
        path = evaluate_expression(node.args[0], namespace)
        color = evaluate_expression(node.args[2], namespace)
        roughness = evaluate_expression(node.args[3], namespace)
        tables[path] = make_material_values(None, {"Roughness": roughness}, {"BaseColor": with_alpha(color, 1.0)}, {})
    coast_path = namespace.get("COAST_SAND_INSTANCE_PATH")
    if coast_path in tables:
        for constant_name, parameter_name in COAST_SAND_CONSTANT_PARAMETERS:
            tables[coast_path]["Scalars"][parameter_name] = namespace[constant_name]
    return tables


def read_niagara_tables(namespace):
    sections = {section: {} for section in fluid_material_look.NIAGARA_SECTIONS}
    type_sections = {namespace[type_name]: section for type_name, section in NIAGARA_SECTION_BY_TYPE_NAME.items()}
    for name, type_path, value in namespace["USER_VARIABLES"]:
        if value is None or type_path not in type_sections:
            continue
        section = type_sections[type_path]
        sections[section][name] = float(value) if section == "Floats" else [float(component) for component in value]
    return {namespace["DEFAULT_ASSET_PATH"]: sections}


def read_script_tables(revision):
    materials = {}
    niagara_systems = {}
    sources = {}
    readers = (
        (WATER_MATERIAL_SCRIPT, lambda tree, namespace: read_water_material_tables(namespace)),
        (FLAME_MATERIAL_SCRIPT, lambda tree, namespace: read_flame_material_tables(namespace)),
        (VOLUME_MATERIAL_SCRIPT, lambda tree, namespace: read_volume_material_tables(namespace)),
        (TEST_LEVEL_SCRIPT, lambda tree, namespace: read_test_level_tables(namespace)),
        (WATER_STAGE_SCRIPT, read_matte_instance_tables),
        (FIRE_STAGE_SCRIPT, read_matte_instance_tables),
    )
    for script_name, reader in readers:
        tree = ast.parse(read_script_at_revision(revision, script_name))
        for path, values in reader(tree, evaluate_module_constants(tree)).items():
            materials[path] = values
            sources[path] = script_name
    niagara_tree = ast.parse(read_script_at_revision(revision, NIAGARA_SCRIPT))
    for path, values in read_niagara_tables(evaluate_module_constants(niagara_tree)).items():
        niagara_systems[path] = values
        sources[path] = NIAGARA_SCRIPT
    return materials, niagara_systems, sources


def normalized_or_none(section, values, name):
    if name not in values:
        return None
    return fluid_material_look.normalize_section_value(section, values[name])


def compare_script_entry(path, script_name, script_values, asset_values, sections):
    items = []
    if script_values.get("Parent") is not None and script_values["Parent"] != asset_values.get("Parent"):
        items.append({"asset": path, "script": script_name, "at": "Parent", "script_value": script_values["Parent"], "asset_value": asset_values.get("Parent"), "kind": "value"})
    for section in sections:
        script_section = script_values.get(section, {})
        asset_section = asset_values.get(section, {})
        for name in sorted(set(script_section) | set(asset_section)):
            script_value = normalized_or_none(section, script_section, name)
            asset_value = normalized_or_none(section, asset_section, name)
            if script_value == asset_value:
                continue
            kind = "script_only" if asset_value is None else ("asset_only" if script_value is None else "value")
            items.append({"asset": path, "script": script_name, "at": f"{section}.{name}", "script_value": script_value, "asset_value": asset_value, "kind": kind})
    return items


def find_drift(dump, revision):
    materials, niagara_systems, sources = read_script_tables(revision)
    items = []
    for top_key, script_tables, sections in (
        ("Materials", materials, fluid_material_look.MATERIAL_SECTIONS),
        ("NiagaraSystems", niagara_systems, fluid_material_look.NIAGARA_SECTIONS),
    ):
        asset_tables = dump[top_key]
        for path in sorted(set(script_tables) | set(asset_tables)):
            if path not in asset_tables:
                items.append({"asset": path, "script": sources[path], "at": "", "kind": "missing_asset"})
            elif path not in script_tables:
                items.append({"asset": path, "script": None, "at": "", "kind": "no_script_table"})
            else:
                items.extend(compare_script_entry(path, sources[path], script_tables[path], asset_tables[path], sections))
    counts = {}
    for item in items:
        counts[item["kind"]] = counts.get(item["kind"], 0) + 1
    return {"revision": revision, "script_asset_count": len(materials) + len(niagara_systems), "counts": counts, "items": items}


def parse_arguments(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--dump", default=DEFAULT_DUMP_PATH)
    parser.add_argument("--output", default=DEFAULT_OUTPUT_PATH)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--drift-against", default="")
    parser.add_argument("--drift-output", default=DEFAULT_DRIFT_OUTPUT_PATH)
    return parser.parse_args(argv)


def main(argv=None):
    arguments = parse_arguments(argv)
    try:
        dump = read_dump(arguments.dump)
        canonical_text = fluid_material_look.format_look_text(fluid_material_look.make_canonical_look(dump))
        problems = fluid_material_look.find_look_problems(json.loads(canonical_text))
        if problems:
            print("정본 형식 오류: " + " | ".join(problems[:20]))
            return 1
        drift = find_drift(dump, arguments.drift_against) if arguments.drift_against else None
    except (OSError, json.JSONDecodeError, ValueError, KeyError, SyntaxError) as error:
        print(f"읽기 실패: {error}")
        return 2
    summary = {"dump": arguments.dump, "output": arguments.output, "material_count": len(dump["Materials"]), "niagara_system_count": len(dump["NiagaraSystems"])}
    if drift is not None:
        write_text(arguments.drift_output, fluid_material_look.format_json(drift) + "\n")
        summary.update({"drift_output": arguments.drift_output, "drift_counts": drift["counts"]})
    if arguments.check:
        is_same = read_text_or_none(arguments.output) == canonical_text
        summary["is_canonical_current"] = is_same
        print(json.dumps(summary, ensure_ascii=False, indent=1))
        return 0 if is_same else 1
    write_text(arguments.output, canonical_text)
    summary["output_sha256"] = fluid_material_look.file_sha256(arguments.output)
    print(json.dumps(summary, ensure_ascii=False, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
