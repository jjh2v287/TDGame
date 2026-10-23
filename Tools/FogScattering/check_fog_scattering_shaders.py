"""시스템 Python 3과 dxc.exe(Tools/FluidFX/check_fluid_gpu_shaders.py 의 find_compiler 규칙), 에디터 불필요: TDFogScattering 플러그인 Shaders/ 의 .usf 를 SM5·SM6 정의로 오프라인 dxc 컴파일한다(SF-1 명세 7절). 전역 셰이더 오류는 모든 세션의 에디터 시작을 깨므로 공용 트리 통합 전에 돌린다.
실행: python Tools/FogScattering/check_fog_scattering_shaders.py [--plugin-dir 플러그인루트(기본 Plugins/TDFogScattering)] [--engine 엔진Engine폴더(또는 환경 변수 UE_ENGINE_DIR)] [--dxc dxc경로] [--keep-temp] (PowerShell 또는 Git Bash)
출력: 셰이더 클래스·순열·프로필별 PASS/FAIL 줄과 마지막 줄 JSON 요약. Source/**/*.cpp·*.h 의 IMPLEMENT_GLOBAL_SHADER(..., SF_Compute)·SHADER_PERMUTATION_BOOL/INT/RANGE_INT/SPARSE_INT·SetDefine 을 읽고 /Plugin/TDFogScattering/ 은 <플러그인>/Shaders/ 로 푼다. 종료 코드 0 = 전부 통과 / 1 = 컴파일 실패·입력 없음
상태: 2026-10-23 SF-1 측정 장치 빌더 작성(SF-2 빌더 A가 SHADER_PERMUTATION_SPARSE_INT 읽기 추가), 픽스처(Tools/FogScattering/fixtures/pass·fail)로만 검증. View 균일 버퍼는 엔진이 컴파일 때 생성하는 파일이라 SceneView.h 의 VIEW_UNIFORM_BUFFER_MEMBER_TABLE 에서 만든 View·ViewState 선언 스텁(배열 길이 16 고정, 값은 채우지 않음)으로 대신하므로 엔진 셰이더 컴파일러 자체 검사(에디터 시작)를 대신하지 못한다
"""
import argparse
import importlib.util
import itertools
import json
import math
import os
import pathlib
import re
import shutil
import sys
import tempfile

sys.dont_write_bytecode = True

ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_ENGINE_DIR = r"C:\Program Files\Epic Games\UE_5.8\Engine"
FLUID_CHECKER_PATH = ROOT / "Tools" / "FluidFX" / "check_fluid_gpu_shaders.py"
DEFAULT_PLUGIN_DIR = ROOT / "Plugins" / "TDFogScattering"
PLUGIN_VIRTUAL_PREFIX = "/Plugin/TDFogScattering/"
GENERATED_UNIFORM_BUFFERS_PATH = "/Engine/Generated/GeneratedUniformBuffers.ush"
SCENE_VIEW_HEADER = pathlib.Path("Source") / "Runtime" / "Engine" / "Public" / "SceneView.h"
STUB_ARRAY_LENGTH = 16
LWC_TILE_SIZE = 2097152.0
COMMON_DEFINES = {
    "COMPILER_SUPPORTS_HLSL2021": "1",
    "WORKING_COLOR_SPACE_IS_SRGB": "1",
    "UE_LWC_RENDER_TILE_SIZE": repr(LWC_TILE_SIZE),
    "UE_LWC_RENDER_TILE_SIZE_SQRT": repr(math.sqrt(LWC_TILE_SIZE)),
    "UE_LWC_RENDER_TILE_SIZE_RCP": repr(1.0 / LWC_TILE_SIZE),
    "UE_LWC_RENDER_TILE_SIZE_RSQRT": repr(1.0 / math.sqrt(LWC_TILE_SIZE)),
    "UE_LWC_RENDER_TILE_SIZE_FMOD_PI": repr(math.fmod(LWC_TILE_SIZE, math.pi)),
    "UE_LWC_RENDER_TILE_SIZE_FMOD_2PI": repr(math.fmod(LWC_TILE_SIZE, 2.0 * math.pi)),
}

DERIVED_VIEW_STATE_MEMBERS = (
    "FDFInverseMatrix WorldToClip;", "FDFMatrix ClipToWorld;", "FDFMatrix ScreenToWorld;", "FDFMatrix PrevClipToWorld;",
    "FDFVector3 WorldCameraOrigin;", "FDFVector3 WorldViewOrigin;", "FDFVector3 PrevWorldCameraOrigin;",
    "FDFVector3 PrevWorldViewOrigin;", "FDFVector3 PreViewTranslation;", "FDFVector3 PrevPreViewTranslation;",
)
DRAW_RECTANGLE_STUB = "\n".join((
    "cbuffer DrawRectangleStubBuffer", "{",
    "\tfloat4 DrawRectangleParameters_PosScaleBias;", "\tfloat4 DrawRectangleParameters_UVScaleBias;", "\tfloat4 DrawRectangleParameters_InvTargetSizeAndTextureSize;",
    "}", "static const struct", "{", "\tfloat4 PosScaleBias;", "\tfloat4 UVScaleBias;", "\tfloat4 InvTargetSizeAndTextureSize;",
    "} DrawRectangleParameters = { DrawRectangleParameters_PosScaleBias, DrawRectangleParameters_UVScaleBias, DrawRectangleParameters_InvTargetSizeAndTextureSize };", ""))
COMMENT_PATTERN = re.compile(r"//[^\n]*|/\*.*?\*/", re.DOTALL)
CLASS_PATTERN = re.compile(r"class\s+(?:\w+_API\s+)?(\w+)\s*:\s*public\s+\w+")
RANGE_INT_PATTERN = re.compile(r'SHADER_PERMUTATION_RANGE_INT\(\s*"(\w+)"\s*,\s*(-?\d+)\s*,\s*(\d+)\s*\)')
SPARSE_INT_PATTERN = re.compile(r'SHADER_PERMUTATION_SPARSE_INT\(\s*"(\w+)"\s*,\s*(-?\d+(?:\s*,\s*-?\d+)*)\s*\)')
SET_DEFINE_PATTERN = re.compile(r'SetDefine\(\s*TEXT\("(\w+)"\)\s*,\s*([^)]*?)\s*\)')
LITERAL_DEFINE_PATTERN = re.compile(r"^(?:-?\d+(?:\.\d+)?f?|true|false)$")

VIEW_TABLE_START = "#define VIEW_UNIFORM_BUFFER_MEMBER_TABLE"
VIEW_STRUCT_START = "BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT_WITH_CONSTRUCTOR(FViewUniformShaderParameters"
VIEW_STRUCT_END = "END_GLOBAL_SHADER_PARAMETER_STRUCT"
VIEW_MEMBER_PATTERN = re.compile(r"VIEW_UNIFORM_BUFFER_MEMBER(?P<array>_ARRAY)?(?:_PER_VIEW)?(?:_EX)?\(\s*(?P<type>\w+)\s*,\s*(?P<name>\w+)\s*(?:,\s*(?P<extra>[^)]*))?\)")
VIEW_SCALAR_PATTERN = re.compile(r"(?m)^\s*SHADER_PARAMETER\(\s*(\w+)\s*,\s*(\w+)\s*\)")
VIEW_RESOURCE_PATTERN = re.compile(r"(?m)^\s*SHADER_PARAMETER_(TEXTURE|SAMPLER|SRV)\(\s*([^,()]+?)\s*,\s*(\w+)\s*\)")
HLSL_TYPES = {
    "float": "float", "uint32": "uint", "int32": "int", "int": "int", "FMatrix44f": "float4x4", "FVector4f": "float4",
    "FVector3f": "float3", "FVector2f": "float2", "FLinearColor": "float4", "FIntPoint": "int2", "FIntVector": "int3",
    "FIntVector4": "int4", "FUintVector4": "uint4",
}


def load_fluid_checker():
    specification = importlib.util.spec_from_file_location("check_fluid_gpu_shaders", FLUID_CHECKER_PATH)
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


def parse_arguments():
    parser = argparse.ArgumentParser(description="TDFogScattering 전역 셰이더 오프라인 dxc 컴파일 검사")
    parser.add_argument("--plugin-dir", default=str(DEFAULT_PLUGIN_DIR))
    parser.add_argument("--engine", default=os.environ.get("UE_ENGINE_DIR", DEFAULT_ENGINE_DIR))
    parser.add_argument("--dxc", default="")
    parser.add_argument("--keep-temp", action="store_true")
    return parser.parse_args()


def read_plugin_sources(plugin_dir):
    source_dir = pathlib.Path(plugin_dir) / "Source"
    paths = sorted(source_dir.rglob("*.h")) + sorted(source_dir.rglob("*.cpp"))
    return "\n".join(COMMENT_PATTERN.sub("", path.read_text(encoding="utf-8", errors="replace")) for path in paths)


def extract_class_body(text, start):
    open_index = text.find("{", start)
    if open_index < 0:
        return ""
    depth = 0
    for index in range(open_index, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[open_index:index + 1]
    return text[open_index:]


def parse_define_value(text):
    if not LITERAL_DEFINE_PATTERN.match(text):
        return None
    return {"true": "1", "false": "0"}.get(text, text.rstrip("f"))


def parse_shader_classes(fluid, text):
    bodies = {match.group(1): extract_class_body(text, match.end()) for match in CLASS_PATTERN.finditer(text)}
    classes = []
    for implement in fluid.IMPLEMENT_PATTERN.finditer(text):
        class_name, virtual_path, entry_point = implement.groups()
        body = bodies.get(class_name, "")
        dimensions = [(name, ["0", "1"]) for name in fluid.BOOL_PERMUTATION_PATTERN.findall(body)]
        dimensions += [(name, [str(value) for value in range(int(count))]) for name, count in fluid.INT_PERMUTATION_PATTERN.findall(body)]
        dimensions += [(name, [str(int(first) + value) for value in range(int(count))]) for name, first, count in RANGE_INT_PATTERN.findall(body)]
        dimensions += [(name, [value.strip() for value in listed.split(",")]) for name, listed in SPARSE_INT_PATTERN.findall(body)]
        defines = {}
        unresolved_defines = []
        for name, raw_value in SET_DEFINE_PATTERN.findall(body):
            value = parse_define_value(raw_value)
            if value is None:
                unresolved_defines.append(name)
                continue
            defines[name] = value
        classes.append({"class": class_name, "path": virtual_path, "entry": entry_point, "dimensions": dimensions,
                        "defines": defines, "unresolved_defines": unresolved_defines, "declared": class_name in bodies})
    return classes


def build_generated_files(engine_dir):
    header_path = pathlib.Path(engine_dir) / SCENE_VIEW_HEADER
    if not header_path.is_file():
        return None
    text = header_path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()
    table_start = next((index for index, line in enumerate(lines) if line.startswith(VIEW_TABLE_START)), None)
    struct_start = text.find(VIEW_STRUCT_START)
    struct_end = text.find(VIEW_STRUCT_END, struct_start)
    if table_start is None or struct_start < 0 or struct_end < 0:
        return None
    table_end = table_start
    while lines[table_end].rstrip().endswith("\\"):
        table_end += 1
    numeric_members = []
    for match in VIEW_MEMBER_PATTERN.finditer("\n".join(lines[table_start + 1:table_end + 1])):
        hlsl_type = HLSL_TYPES.get(match.group("type"))
        if hlsl_type is None:
            continue
        extra = (match.group("extra") or "").strip()
        length = (int(extra) if extra.isdigit() else STUB_ARRAY_LENGTH) if match.group("array") else None
        numeric_members.append((hlsl_type, match.group("name"), length))
    struct_text = text[struct_start:struct_end]
    for type_name, name in VIEW_SCALAR_PATTERN.findall(struct_text):
        if type_name in HLSL_TYPES:
            numeric_members.append((HLSL_TYPES[type_name], name, None))
    resource_members = [(resource_type, name) for _, resource_type, name in VIEW_RESOURCE_PATTERN.findall(struct_text)]

    def format_member(hlsl_type, name, length, prefix=""):
        return f"{hlsl_type} {prefix}{name}{'' if length is None else f'[{length}]'};"

    view_lines = ["#pragma once", "cbuffer ViewStubBuffer", "{"]
    view_lines += ["\t" + format_member(hlsl_type, name, length, "View_") for hlsl_type, name, length in numeric_members]
    view_lines += ["}"] + [f"{resource_type} View_{name};" for resource_type, name in resource_members]
    view_lines += ["static const struct", "{"]
    view_lines += ["\t" + format_member(hlsl_type, name, length) for hlsl_type, name, length in numeric_members]
    view_lines += [f"\t{resource_type} {name};" for resource_type, name in resource_members]
    initializers = [f"View_{name}" for _, name, _ in numeric_members] + [f"View_{name}" for _, name in resource_members]
    view_lines += ["} View = {", "\t" + ",\n\t".join(initializers), "};", ""]

    state_lines = ["struct ViewState", "{"]
    state_lines += ["\t" + format_member(hlsl_type, name, length) for hlsl_type, name, length in numeric_members]
    state_lines += ["\t" + line for line in DERIVED_VIEW_STATE_MEMBERS]
    state_lines += ["};", "void FinalizeViewState(inout ViewState InOutView);", "ViewState GetPrimaryView()", "{", "\tViewState Result = (ViewState)0;"]
    state_lines += [f"\tResult.{name} = View.{name};" for _, name, _ in numeric_members]
    state_lines += ["\tFinalizeViewState(Result);", "\treturn Result;", "}", "ViewState GetInstancedView(uint ViewIndex)", "{", "\treturn GetPrimaryView();", "}", ""]
    return {
        GENERATED_UNIFORM_BUFFERS_PATH: '#include "/Engine/Generated/UniformBuffers/View.ush"\n' + DRAW_RECTANGLE_STUB,
        "/Engine/Generated/UniformBuffers/View.ush": "\n".join(view_lines),
        "/Engine/Generated/UniformBuffers/InstancedView.ush": "#pragma once\n",
        "/Engine/Generated/GeneratedInstancedStereo.ush": "\n".join(state_lines),
    }


def install_virtual_path_resolver(fluid, generated_paths):
    original_resolve = fluid.resolve_virtual_path

    def resolve_plugin_virtual_path(virtual_path, engine_shader_dir, module_shader_dir):
        if virtual_path.startswith(PLUGIN_VIRTUAL_PREFIX):
            return pathlib.Path(module_shader_dir) / virtual_path[len(PLUGIN_VIRTUAL_PREFIX):]
        if virtual_path in generated_paths:
            return generated_paths[virtual_path]
        return original_resolve(virtual_path, engine_shader_dir, module_shader_dir)

    fluid.resolve_virtual_path = resolve_plugin_virtual_path


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    arguments = parse_arguments()
    fluid = load_fluid_checker()
    plugin_dir = pathlib.Path(arguments.plugin_dir)
    engine_dir = pathlib.Path(arguments.engine)
    engine_shader_dir = engine_dir / "Shaders"
    module_shader_dir = plugin_dir / "Shaders"
    summary = {"plugin_dir": str(plugin_dir), "compiler": "", "compiler_version": "", "hlsl_versions": [], "view_stub": False,
               "passed": 0, "failed": 0, "failures": [], "unresolved_includes": [], "warnings": []}

    if not (plugin_dir / "Source").is_dir() or not module_shader_dir.is_dir() or not engine_shader_dir.is_dir():
        print(f"FAIL 입력 경로 없음: {plugin_dir / 'Source'} / {module_shader_dir} / {engine_shader_dir}")
        print(json.dumps(summary, ensure_ascii=False))
        return 1

    work_dir = tempfile.mkdtemp(prefix="td_fog_shaders_")
    try:
        dxc_path, compiler_label, compiler_version = fluid.find_compiler(arguments.dxc, engine_dir, work_dir)
        summary["compiler"] = compiler_label
        summary["compiler_version"] = compiler_version
        if not dxc_path:
            print("FAIL dxc.exe 를 찾지 못함")
            print(json.dumps(summary, ensure_ascii=False))
            return 1

        hlsl_versions = [""]
        if fluid.supports_hlsl_2021(dxc_path, work_dir):
            hlsl_versions.append("2021")
        summary["hlsl_versions"] = [version or "기본" for version in hlsl_versions]

        shader_classes = parse_shader_classes(fluid, read_plugin_sources(plugin_dir))
        if not shader_classes:
            print("FAIL IMPLEMENT_GLOBAL_SHADER(..., SF_Compute) 를 찾지 못함")
            print(json.dumps(summary, ensure_ascii=False))
            return 1

        generated_files = build_generated_files(engine_dir)
        generated_paths = {}
        if generated_files is None:
            summary["warnings"].append("SceneView.h 에서 View 스텁을 만들지 못함")
        else:
            for virtual_path, file_text in generated_files.items():
                generated_path = pathlib.Path(work_dir) / "generated" / virtual_path.lstrip("/")
                generated_path.parent.mkdir(parents=True, exist_ok=True)
                generated_path.write_text(file_text, encoding="utf-8")
                generated_paths[virtual_path] = generated_path
            summary["view_stub"] = True
        install_virtual_path_resolver(fluid, generated_paths)

        mirror = fluid.ShaderMirror(pathlib.Path(work_dir) / "mirror", engine_shader_dir, module_shader_dir)
        for shader_class in shader_classes:
            if not shader_class["declared"]:
                summary["warnings"].append(f"{shader_class['class']}: 클래스 선언을 찾지 못해 순열 없이 컴파일")
            for name in shader_class["unresolved_defines"]:
                summary["warnings"].append(f"{shader_class['class']}: SetDefine {name} 값이 리터럴이 아니라 건너뜀")
            source_path = mirror.materialize(shader_class["path"])
            if source_path is None:
                summary["failed"] += 1
                summary["failures"].append(f"{shader_class['class']}: 소스 없음 {shader_class['path']}")
                print(f"FAIL {shader_class['class']} 소스 없음 {shader_class['path']}")
                continue
            names = [name for name, _ in shader_class["dimensions"]]
            value_lists = [values for _, values in shader_class["dimensions"]]
            for values in itertools.product(*value_lists):
                for profile_name, profile_defines in fluid.PROFILE_DEFINES.items():
                    for hlsl_version in hlsl_versions:
                        defines = dict(profile_defines)
                        defines.update(COMMON_DEFINES)
                        defines.update(shader_class["defines"])
                        defines.update(zip(names, values))
                        permutation_label = ",".join(f"{name}={value}" for name, value in zip(names, values)) or "-"
                        label = f"{shader_class['class']} [{permutation_label}] {profile_name} HV{hlsl_version or '기본'}"
                        succeeded, errors = fluid.compile_permutation(dxc_path, mirror.mirror_dir, source_path, shader_class["entry"], defines, hlsl_version)
                        if succeeded:
                            summary["passed"] += 1
                            print(f"PASS {label}")
                            continue
                        summary["failed"] += 1
                        summary["failures"].append(label + " :: " + " | ".join(errors[:4]))
                        print(f"FAIL {label}")
                        for error in errors[:8]:
                            print(f"    {error}")
        summary["unresolved_includes"] = sorted(mirror.unresolved)
    finally:
        if arguments.keep_temp:
            print(f"임시 폴더 유지: {work_dir}")
        else:
            shutil.rmtree(work_dir, ignore_errors=True)

    print(json.dumps(summary, ensure_ascii=False))
    return 0 if summary["failed"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
