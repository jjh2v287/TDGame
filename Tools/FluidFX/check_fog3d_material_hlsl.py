"""실행 환경: 시스템 Python 3 와 Windows Kits dxc.exe(ps_6_0), 에디터·numpy 불필요
실행 명령: python Tools/FluidFX/check_fog3d_material_hlsl.py [--root 프로젝트경로] [--dxc dxc경로] [--output 요약.json] [--baseline 이전 요약.json(flat 결과 대조)]
출력: 항목별 PASS/FAIL 줄, flat 대 ground 표본·명령 수 INFO 줄, 마지막 줄 JSON 요약(.ush sha256 둘, 방식별 dxc 표본·명령 수, flat_vs_ground), 하나라도 실패하면 종료 코드 1, TDFluidGpuFog3DDensity.ush 가 아직 없으면 미실행으로 알리고 종료 코드 2
상태: editor_make_fog3d_material.py 를 unreal 없이 import 해 flat(몸통 4줄, include·inline)과 ground(몸통 6줄, include 만) 변형의 Custom 코드·입력 이름·파라미터 표를 계약 사본과 맞대고, 입력 선언을 붙여 dxc ps_6_0 O3/Od 로 컴파일하며, 가짜 unreal 모듈로 재질 만들기 흐름(그래프·보고·중단 조건)을 돌린다. 상한 숫자는 아직 없다(출력만). 실제 재질 컴파일은 에디터에서만 확인된다.
"""
import argparse
import glob
import hashlib
import importlib.util
import json
import os
import pathlib
import posixpath
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True

HEADER = """
#define Texture3DSampleLevel(T, S, UV, L) T.SampleLevel(S, UV, L)
"""
GROUND_HEADER = """
#define Texture2DSampleLevel(T, S, UV, L) T.SampleLevel(S, UV, L)
"""
SAMPLE_PATTERN = re.compile(r"call \S+ @dx\.op\.(?:sample|sampleLevel|sampleBias|sampleGrad)\.")
INSTRUCTION_PATTERN = re.compile(r"^\s+(?:%\S+ = |call |store |br |ret )", re.MULTILINE)
INCLUDE_LINE_PATTERN = re.compile(r'^([ \t]*#[ \t]*include[ \t]+")([^"]+)(")', re.MULTILINE)
EXIT_NOT_RUN = 2

CONTRACT_BODY_LINES = (
    "float3 tdUvw = TDFog3DStateUvw(Fog3DZonePositionCm, Fog3DWindow, Fog3DGrid);",
    "float4 tdState = Texture3DSampleLevel(Fog3DStateVolume, Fog3DStateVolumeSampler, tdUvw, 0);",
    "float tdDensity = TDFog3DDensity(Fog3DZonePositionCm, tdState, Fog3DWindow, Fog3DGrid, Fog3DActive, Fog3DBaseShape, Fog3DZone, Fog3DFineNoise, Fog3DStainNoise);",
    "return float4(Fog3DExtinctionPerM * tdDensity.xxx, tdDensity);",
)
CONTRACT_INCLUDE_PATH = "/TDFluidGpu/Private/TDFluidGpuFog3DDensity.ush"
CONTRACT_SHADER_RELATIVE_PATH = ("Source", "TDFluidGpu", "Shaders", "Private", "TDFluidGpuFog3DDensity.ush")
CONTRACT_SHADER_MODULE_ROOT = ("Source", "TDFluidGpu", "Shaders")
CONTRACT_CPP_NAMES = frozenset((
    "Fog3DStateVolume", "Fog3DWindow", "Fog3DGrid", "Fog3DActive",
    "Fog3DBaseShape", "Fog3DZone", "Fog3DFineNoise", "Fog3DStainNoise",
))
CONTRACT_GRAPH_ONLY_NAMES = frozenset(("Fog3DExtinctionPerM", "Fog3DAlbedo"))
CONTRACT_DERIVED_INPUT_NAME = "Fog3DZonePositionCm"
CONTRACT_VECTOR_DEFAULTS = {
    "Fog3DGrid": (15.625, 30.0, 8.0, 8.0),
    "Fog3DBaseShape": (0.8, 90.0, 45.0, 0.0),
    "Fog3DAlbedo": (0.8, 0.85, 0.95, None),
}
CONTRACT_SCALAR_DEFAULTS = {"Fog3DExtinctionPerM": 0.2}
CONTRACT_VECTOR_NAMES = frozenset(("Fog3DWindow", "Fog3DGrid", "Fog3DBaseShape", "Fog3DZone", "Fog3DFineNoise", "Fog3DStainNoise", "Fog3DAlbedo"))
CONTRACT_SCALAR_NAMES = frozenset(("Fog3DActive", "Fog3DExtinctionPerM"))
MATERIAL_LOOK_RELATIVE_PATH = ("Content", "FluidFX", "PresetData", "TDFluidMaterialLook.json")
CPP_SOURCE_GLOB = ("Source", "TDGame", "FluidFX", "TDFog3D*.cpp")
CODE_MODE_LABELS = (("include", "include 방식"), ("inline", "inline 방식"))
GROUND_CONTRACT_BODY_LINES = (
    "float2 tdGroundUv = TDFog3DGroundUv(Fog3DZonePositionCm.xy, Fog3DGround, Fog3DGrid);",
    "float tdGroundHeight = Texture2DSampleLevel(Fog3DGroundHeight, Fog3DGroundHeightSampler, tdGroundUv, 0).r;",
    "float3 tdUvw = TDFog3DGroundStateUvw(Fog3DZonePositionCm, Fog3DWindow, Fog3DGrid, tdGroundHeight);",
    "float4 tdState = Texture3DSampleLevel(Fog3DStateVolume, Fog3DStateVolumeSampler, tdUvw, 0);",
    "float tdDensity = TDFog3DGroundDensity(Fog3DZonePositionCm, tdState, Fog3DWindow, Fog3DGrid, Fog3DActive, Fog3DBaseShape, Fog3DZone, Fog3DFineNoise, Fog3DStainNoise, tdGroundHeight);",
    "return float4(Fog3DExtinctionPerM * tdDensity.xxx, tdDensity);",
)
GROUND_CONTRACT_INCLUDE_PATH = "/TDFluidGpu/Private/TDFluidGpuFog3DDensityGround.ush"
GROUND_CONTRACT_SHADER_RELATIVE_PATH = ("Source", "TDFluidGpu", "Shaders", "Private", "TDFluidGpuFog3DDensityGround.ush")
GROUND_CONTRACT_ASSET_PATH = "/Game/FluidFX/Materials/M_TD_Fog3DVolumeGround"
GROUND_CONTRACT_EXTRA_NAMES = frozenset(("Fog3DGroundHeight", "Fog3DGround"))
GROUND_CONTRACT_HEIGHT_DEFAULT_PATH = "/Engine/EngineResources/Black"
GROUND_CONTRACT_INCLUDED_FILE = "/TDFluidGpu/Private/TDFluidGpuFog3DDensity.ush"
FLAT_ASSET_PATH = "/Game/FluidFX/Materials/M_TD_Fog3DVolume"
FAKE_ENUM_NAMES = ("CustomMaterialOutputType", "MaterialDomain", "BlendMode", "MaterialProperty", "MaterialSamplerType", "TextureCompressionSettings")
FAKE_PARAMETER_KINDS = {"MaterialExpressionVectorParameter": "vector", "MaterialExpressionScalarParameter": "scalar", "MaterialExpressionTextureObjectParameter": "texture"}


def load_material_script(root):
    path = root / "Tools" / "FluidFX" / "editor_make_fog3d_material.py"
    spec = importlib.util.spec_from_file_location("editor_make_fog3d_material", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def find_dxc(explicit_path):
    candidates = [explicit_path, os.environ.get("DXC_PATH")]
    program_files = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    candidates.extend(reversed(sorted(glob.glob(os.path.join(program_files, "Windows Kits", "10", "bin", "*", "x64", "dxc.exe")))))
    return next((candidate for candidate in candidates if candidate and os.path.isfile(candidate)), "")


def normalize(text):
    return re.sub(r"\s+", " ", text).strip()


def declare_inputs(custom_inputs):
    texture_lines = []
    constant_lines = []
    for name, hlsl_type in custom_inputs:
        if hlsl_type in ("Texture3D", "Texture2D"):
            texture_lines.append(f"{hlsl_type} {name};")
            texture_lines.append(f"SamplerState {name}Sampler;")
            continue
        constant_lines.append(f"{hlsl_type} {name};")
    return "\n".join(texture_lines) + "\ncbuffer Inputs {\n" + "\n".join(constant_lines) + "\n}\n"


def build_source(custom_inputs, code, include_paths, header=HEADER):
    include_block = "".join(f'#include "{path.lstrip("/")}"\n' for path in include_paths)
    return (
        header + include_block + declare_inputs(custom_inputs)
        + f"float4 TDFog3DMaterialExpression(float4 SvPositionValue)\n{{\n{code}\n}}\n"
        + "float4 main(float4 sv : SV_Position) : SV_Target\n{\nreturn TDFog3DMaterialExpression(sv);\n}\n"
    )


def shader_module_file(root, virtual_path):
    prefix = CONTRACT_INCLUDE_PATH.split("/")[1]
    parts = virtual_path.strip("/").split("/")
    if parts[0] != prefix:
        return None
    return root.joinpath(*CONTRACT_SHADER_MODULE_ROOT, *parts[1:])


def stage_shader_file(root, virtual_path, work_folder, staged):
    if virtual_path in staged:
        return
    staged.add(virtual_path)
    target = work_folder.joinpath(*virtual_path.strip("/").split("/"))
    target.parent.mkdir(parents=True, exist_ok=True)
    if virtual_path.startswith("/Engine/"):
        target.write_text("", encoding="utf-8")
        return
    source = shader_module_file(root, virtual_path)
    if source is None or not source.is_file():
        raise FileNotFoundError(f"include 대상 없음: {virtual_path}")
    parent = posixpath.dirname(virtual_path)

    def rewrite(match):
        included = match.group(2)
        resolved = included if included.startswith("/") else posixpath.normpath(posixpath.join(parent, included))
        stage_shader_file(root, resolved, work_folder, staged)
        return match.group(1) + resolved.lstrip("/") + match.group(3)

    target.write_text(INCLUDE_LINE_PATTERN.sub(rewrite, source.read_text(encoding="utf-8")), encoding="utf-8", newline="\n")


def compile_source(dxc, source, label, optimize, work_folder):
    path = work_folder / f"{label}.hlsl"
    disassembly = work_folder / f"{label}.dxil.txt"
    path.write_text(source, encoding="utf-8")
    command = [dxc, "-T", "ps_6_0", "-E", "main", "-I", str(work_folder), str(path), "-Fc", str(disassembly), "-Fo", str(work_folder / f"{label}.bin"), "-O3" if optimize else "-Od"]
    completed = subprocess.run(command, capture_output=True, text=True)
    text = disassembly.read_text(encoding="utf-8", errors="replace") if disassembly.exists() else ""
    first_error = next((line for line in (completed.stderr + completed.stdout).splitlines() if "error" in line), "")
    return {"exit": completed.returncode, "samples": len(SAMPLE_PATTERN.findall(text)), "instructions": len(INSTRUCTION_PATTERN.findall(text)), "error": first_error}


def find_name_contract_problems(material):
    problems = []
    expected_inputs = set(CONTRACT_CPP_NAMES) | {"Fog3DExtinctionPerM", CONTRACT_DERIVED_INPUT_NAME}
    actual_inputs = list(material.CUSTOM_INPUT_NAMES)
    if len(actual_inputs) != len(set(actual_inputs)):
        problems.append("Custom 입력 이름 중복")
    if set(actual_inputs) != expected_inputs:
        problems.append(f"Custom 입력 이름 집합 차이: 더 있음 {sorted(set(actual_inputs) - expected_inputs)}, 없음 {sorted(expected_inputs - set(actual_inputs))}")
    vector_names = {name for name, _ in material.VECTOR_PARAMETER_DEFAULTS}
    scalar_names = {name for name, _ in material.SCALAR_PARAMETER_DEFAULTS}
    texture_names = set(material.TEXTURE_PARAMETER_NAMES)
    if vector_names != CONTRACT_VECTOR_NAMES:
        problems.append(f"벡터 파라미터 집합 차이: {sorted(vector_names ^ CONTRACT_VECTOR_NAMES)}")
    if scalar_names != CONTRACT_SCALAR_NAMES:
        problems.append(f"스칼라 파라미터 집합 차이: {sorted(scalar_names ^ CONTRACT_SCALAR_NAMES)}")
    if texture_names != {"Fog3DStateVolume"}:
        problems.append(f"텍스처 파라미터 집합 차이: {sorted(texture_names)}")
    parameter_names = vector_names | scalar_names | texture_names
    if parameter_names != CONTRACT_CPP_NAMES | CONTRACT_GRAPH_ONLY_NAMES:
        problems.append(f"파라미터 전체 집합 ≠ 계약(C++ 8 + 그래프 전용 2): {sorted(parameter_names ^ (CONTRACT_CPP_NAMES | CONTRACT_GRAPH_ONLY_NAMES))}")
    if set(material.CPP_PARAMETER_NAMES) != CONTRACT_CPP_NAMES:
        problems.append(f"CPP_PARAMETER_NAMES 집합 차이: {sorted(set(material.CPP_PARAMETER_NAMES) ^ CONTRACT_CPP_NAMES)}")
    vector_defaults = dict(material.VECTOR_PARAMETER_DEFAULTS)
    for name, expected in CONTRACT_VECTOR_DEFAULTS.items():
        actual = vector_defaults.get(name)
        if actual is None:
            continue
        for index, value in enumerate(expected):
            if value is not None and abs(actual[index] - value) > 1e-9:
                problems.append(f"{name} 기본값 {actual} ≠ 계약 {expected}")
                break
    scalar_defaults = dict(material.SCALAR_PARAMETER_DEFAULTS)
    for name, expected in CONTRACT_SCALAR_DEFAULTS.items():
        if abs(scalar_defaults.get(name, float("nan")) - expected) > 1e-9:
            problems.append(f"{name} 기본값 {scalar_defaults.get(name)} ≠ 계약 {expected}")
    unresolved = material.find_unresolved_inputs()
    if unresolved:
        problems.append(f"입력 원천 없음: {unresolved}")
    return problems


def find_look_table_collisions(root, material):
    path = root.joinpath(*MATERIAL_LOOK_RELATIVE_PATH)
    if not path.is_file():
        return None
    text = path.read_text(encoding="utf-8")
    return sorted(name for name in set(material.CUSTOM_INPUT_NAMES) | set(material.CPP_PARAMETER_NAMES) | {"Fog3DAlbedo"} if name in text)


def find_body_problems(material, shader_text):
    problems = []
    include_code, include_paths = material.build_custom_code("include", shader_text)
    if normalize(include_code) != normalize("\n".join(CONTRACT_BODY_LINES)):
        problems.append("include 방식 코드가 계약 몸통 4줄과 다름")
    if include_paths != [CONTRACT_INCLUDE_PATH]:
        problems.append(f"IncludeFilePaths {include_paths} ≠ [{CONTRACT_INCLUDE_PATH}]")
    inline_code, inline_paths = material.build_custom_code("inline", shader_text)
    if normalize("\n".join(CONTRACT_BODY_LINES)) not in normalize(inline_code):
        problems.append("inline 방식 코드에 계약 몸통 4줄이 그대로 들어 있지 않음")
    if inline_paths:
        problems.append(f"inline 방식인데 IncludeFilePaths {inline_paths}")
    if normalize(material.prepare_inline_scope(shader_text)[0]) not in normalize(inline_code):
        problems.append("inline 방식 코드에 .ush 본문(Engine include·pragma once 제외, 한 줄 static const 는 매크로로)이 그대로 들어 있지 않음")
    return problems


def find_cpp_name_problems(root, material):
    pattern = "/".join(CPP_SOURCE_GLOB[:-1])
    files = sorted(root.joinpath(*CPP_SOURCE_GLOB[:-1]).glob(CPP_SOURCE_GLOB[-1])) if root.joinpath(*CPP_SOURCE_GLOB[:-1]).is_dir() else []
    if not files:
        return None, pattern
    text = "\n".join(path.read_text(encoding="utf-8", errors="replace") for path in files)
    missing = [name for name in names_to_find(material) if not re.search(r"TEXT\(\s*\"" + re.escape(name) + r"\"\s*\)", text)]
    return missing, pattern


def names_to_find(material_or_variant):
    return getattr(material_or_variant, "CPP_PARAMETER_NAMES", None) or material_or_variant.cpp_parameter_names


def find_ground_name_contract_problems(material):
    problems = []
    flat = material.FLAT_VARIANT
    ground = material.GROUND_VARIANT
    expected_inputs = set(CONTRACT_CPP_NAMES) | {"Fog3DExtinctionPerM", CONTRACT_DERIVED_INPUT_NAME} | GROUND_CONTRACT_EXTRA_NAMES
    actual_inputs = list(ground.custom_input_names)
    if len(actual_inputs) != len(set(actual_inputs)):
        problems.append("ground Custom 입력 이름 중복")
    if set(actual_inputs) != expected_inputs:
        problems.append(f"ground Custom 입력 이름 집합 차이: 더 있음 {sorted(set(actual_inputs) - expected_inputs)}, 없음 {sorted(expected_inputs - set(actual_inputs))}")
    if actual_inputs[:len(flat.custom_input_names)] != list(flat.custom_input_names):
        problems.append("ground Custom 입력 앞부분이 flat 입력 순서와 다름")
    if dict(ground.custom_inputs).get("Fog3DGroundHeight") != "Texture2D" or dict(ground.custom_inputs).get("Fog3DGround") != "float4":
        problems.append("ground 입력 형식: Fog3DGroundHeight = Texture2D, Fog3DGround = float4 이어야 함")
    vector_names = {name for name, _ in ground.vector_parameter_defaults}
    if vector_names != CONTRACT_VECTOR_NAMES | {"Fog3DGround"}:
        problems.append(f"ground 벡터 파라미터 집합 차이: {sorted(vector_names ^ (CONTRACT_VECTOR_NAMES | {'Fog3DGround'}))}")
    if "Fog3DGround" not in ground.float4_vector_names:
        problems.append("Fog3DGround 가 RGBA(float4) 로 연결되지 않음")
    if {name for name, _ in ground.scalar_parameter_defaults} != CONTRACT_SCALAR_NAMES:
        problems.append("ground 스칼라 파라미터 집합 차이")
    if set(ground.texture_parameter_names) != {"Fog3DStateVolume", "Fog3DGroundHeight"}:
        problems.append(f"ground 텍스처 파라미터 집합 차이: {sorted(ground.texture_parameter_names)}")
    if set(ground.cpp_parameter_names) != CONTRACT_CPP_NAMES | GROUND_CONTRACT_EXTRA_NAMES:
        problems.append(f"ground C++ 이름 집합 차이: {sorted(set(ground.cpp_parameter_names) ^ (CONTRACT_CPP_NAMES | GROUND_CONTRACT_EXTRA_NAMES))}")
    if ground.master_path != GROUND_CONTRACT_ASSET_PATH or flat.master_path != FLAT_ASSET_PATH:
        problems.append(f"에셋 경로 차이: flat {flat.master_path}, ground {ground.master_path}")
    if tuple(ground.code_modes) != ("include",):
        problems.append(f"ground 변형이 include 외 방식을 허용: {ground.code_modes}")
    unresolved = material.find_unresolved_inputs(ground)
    if unresolved:
        problems.append(f"ground 입력 원천 없음: {unresolved}")
    return problems


def find_ground_body_problems(material, ground_text):
    problems = []
    code, include_paths = material.build_custom_code("include", ground_text, material.GROUND_VARIANT)
    if normalize(code) != normalize("\n".join(GROUND_CONTRACT_BODY_LINES)):
        problems.append("ground include 방식 코드가 계약 몸통 6줄과 다름")
    if include_paths != [GROUND_CONTRACT_INCLUDE_PATH]:
        problems.append(f"ground IncludeFilePaths {include_paths} ≠ [{GROUND_CONTRACT_INCLUDE_PATH}]")
    try:
        material.build_custom_code("inline", ground_text, material.GROUND_VARIANT)
        problems.append("ground 변형이 inline 요청을 거부하지 않음")
    except ValueError:
        pass
    includes = [match.group(2) for match in INCLUDE_LINE_PATTERN.finditer(ground_text)]
    if includes != [GROUND_CONTRACT_INCLUDED_FILE]:
        problems.append(f"ground .ush 의 include 가 기존 그리는 식 파일 하나가 아님: {includes}")
    for function_name in ("TDFog3DGroundUv", "TDFog3DGroundStateUvw", "TDFog3DGroundStateWeight", "TDFog3DGroundDensity"):
        if not re.search(r"\b" + function_name + r"\s*\(", ground_text):
            problems.append(f"ground .ush 에 함수 없음: {function_name}")
    return problems


class FakeUnrealObject:
    def __init__(self, *args, **kwargs):
        self.arguments = args
        self.properties = {}
        self.expressions = []

    def set_editor_property(self, name, value):
        self.properties[name] = value

    def get_editor_property(self, name):
        return self.properties[name]

    def get_name(self):
        return type(self).__name__


class FakeUnrealEnum:
    def __init__(self, enum_name):
        self.enum_name = enum_name

    def __getattr__(self, item):
        if item.startswith("__"):
            raise AttributeError(item)
        return f"{self.enum_name}.{item}"


def make_fake_unreal(project_dir, existing_assets, record):
    import types
    module = types.ModuleType("unreal")
    classes = {}

    def make_class(name):
        if name not in classes:
            classes[name] = type(name, (FakeUnrealObject,), {})
        return classes[name]

    def module_getattr(name):
        if name.startswith("__"):
            raise AttributeError(name)
        return make_class(name)

    def load_asset(path):
        record["loaded"].append(path)
        if path == "/Game/FluidFX/Textures/T_TD_FluidNoiseVolume":
            return make_class("VolumeTexture")()
        if path == GROUND_CONTRACT_HEIGHT_DEFAULT_PATH:
            texture = make_class("Texture2D")()
            texture.set_editor_property("compression_settings", module.TextureCompressionSettings.TC_DEFAULT)
            texture.set_editor_property("srgb", True)
            return texture
        if path in existing_assets:
            material = make_class("Material")()
            record["materials"].append(material)
            return material
        return None

    def create_asset(name, directory, asset_class, factory):
        record["created"].append(f"{directory}/{name}")
        material = asset_class()
        record["materials"].append(material)
        return material

    def create_material_expression(material, expression_class, x, y):
        expression = expression_class()
        material.expressions.append(expression)
        return expression

    def parameter_names(kind):
        return lambda material: [expression.properties["parameter_name"] for expression in material.expressions if FAKE_PARAMETER_KINDS.get(type(expression).__name__) == kind]

    def save_loaded_asset(asset, only_if_is_dirty=True):
        record["saved"].append(asset)
        return True

    def get_statistics(material):
        raise RuntimeError("가짜 unreal 에는 재질 통계가 없음")

    module.__getattr__ = module_getattr
    for enum_name in FAKE_ENUM_NAMES:
        setattr(module, enum_name, FakeUnrealEnum(enum_name))
    module.SystemLibrary = types.SimpleNamespace(get_project_directory=lambda: str(project_dir))
    module.EditorAssetLibrary = types.SimpleNamespace(load_asset=load_asset, save_loaded_asset=save_loaded_asset, does_asset_exist=lambda path: path in existing_assets,
                                                      does_directory_exist=lambda path: True, make_directory=lambda path: True)
    module.EditorLoadingAndSavingUtils = types.SimpleNamespace(get_dirty_map_packages=lambda: [], get_dirty_content_packages=lambda: [])
    module.MaterialEditingLibrary = types.SimpleNamespace(
        create_material_expression=create_material_expression, connect_material_expressions=lambda *args: True, connect_material_property=lambda *args: True,
        delete_all_material_expressions=lambda material: material.expressions.clear(), delete_material_expression=lambda material, expression: material.expressions.remove(expression),
        recompile_material=lambda material: [], get_vector_parameter_names=parameter_names("vector"), get_scalar_parameter_names=parameter_names("scalar"),
        get_texture_parameter_names=parameter_names("texture"), get_statistics=get_statistics)
    module.AssetToolsHelpers = types.SimpleNamespace(get_asset_tools=lambda: types.SimpleNamespace(create_asset=create_asset))
    module.get_editor_subsystem = lambda subsystem_class: types.SimpleNamespace(get_game_world=lambda: None)
    return module


def run_fake_editor(root, request, existing_assets):
    import contextlib
    import io
    import shutil
    record = {"loaded": [], "created": [], "saved": [], "materials": [], "error": "", "report": None}
    with tempfile.TemporaryDirectory() as folder:
        project_dir = pathlib.Path(folder)
        shader_folder = project_dir.joinpath(*CONTRACT_SHADER_MODULE_ROOT, "Private")
        shader_folder.mkdir(parents=True)
        for relative_path in (CONTRACT_SHADER_RELATIVE_PATH, GROUND_CONTRACT_SHADER_RELATIVE_PATH):
            shutil.copyfile(root.joinpath(*relative_path), project_dir.joinpath(*relative_path))
        saved_folder = project_dir / "Saved" / "FluidFX"
        saved_folder.mkdir(parents=True)
        if request is not None:
            (saved_folder / "fog3d_material_request.json").write_text(json.dumps(request), encoding="utf-8")
        previous = sys.modules.get("unreal")
        sys.modules["unreal"] = make_fake_unreal(project_dir, set(existing_assets), record)
        try:
            script = load_material_script(root)
            with contextlib.redirect_stdout(io.StringIO()):
                script.run_with_failure_report()
        except Exception as error:
            record["error"] = str(error)
        finally:
            if previous is None:
                sys.modules.pop("unreal", None)
            else:
                sys.modules["unreal"] = previous
        reports = {path.name: json.loads(path.read_text(encoding="utf-8")) for path in saved_folder.glob("fog3d_material*.json") if path.name != "fog3d_material_request.json"}
        record["report"] = reports
    return record


def describe_fake_material(material):
    customs = [expression for expression in material.expressions if type(expression).__name__ == "MaterialExpressionCustom"]
    if len(customs) != 1:
        return None
    custom = customs[0]
    parameters = {}
    for expression in material.expressions:
        kind = FAKE_PARAMETER_KINDS.get(type(expression).__name__)
        if kind:
            parameters[expression.properties["parameter_name"]] = (kind, expression)
    return {
        "inputs": [str(item.get_editor_property("input_name")) for item in custom.get_editor_property("inputs")],
        "includes": list(custom.get_editor_property("include_file_paths")),
        "code": custom.get_editor_property("code"),
        "parameters": parameters,
    }


def find_fake_editor_problems(root, material):
    problems = []
    cases = (
        ("flat 기본(요청 없음)", None, (), material.FLAT_VARIANT, CONTRACT_BODY_LINES, CONTRACT_INCLUDE_PATH, "fog3d_material.json"),
        ("ground", {"variant": "ground"}, (FLAT_ASSET_PATH,), material.GROUND_VARIANT, GROUND_CONTRACT_BODY_LINES, GROUND_CONTRACT_INCLUDE_PATH, "fog3d_material_ground.json"),
        ("ground 덮어쓰기", {"variant": "ground", "overwrite": True}, (FLAT_ASSET_PATH, GROUND_CONTRACT_ASSET_PATH), material.GROUND_VARIANT, GROUND_CONTRACT_BODY_LINES, GROUND_CONTRACT_INCLUDE_PATH, "fog3d_material_ground.json"),
    )
    for label, request, existing, variant, body_lines, include_path, report_name in cases:
        record = run_fake_editor(root, request, existing)
        if record["error"]:
            problems.append(f"{label}: 실패 {record['error']}")
            continue
        description = describe_fake_material(record["materials"][-1]) if record["materials"] else None
        if description is None:
            problems.append(f"{label}: Custom 노드가 1개가 아님")
            continue
        if description["inputs"] != list(variant.custom_input_names):
            problems.append(f"{label}: Custom 입력 {description['inputs']}")
        if description["includes"] != [include_path]:
            problems.append(f"{label}: include {description['includes']}")
        if normalize(description["code"]) != normalize("\n".join(body_lines)):
            problems.append(f"{label}: Custom 몸통이 계약과 다름")
        expected_parameters = {name for name, _ in variant.vector_parameter_defaults} | {name for name, _ in variant.scalar_parameter_defaults} | set(variant.texture_parameter_names)
        if set(description["parameters"]) != expected_parameters:
            problems.append(f"{label}: 파라미터 노드 {sorted(description['parameters'])}")
        if variant is material.GROUND_VARIANT:
            kind, node = description["parameters"].get("Fog3DGroundHeight", (None, None))
            if kind != "texture" or type(node.properties.get("texture")).__name__ != "Texture2D":
                problems.append(f"{label}: Fog3DGroundHeight 기본 텍스처가 Texture2D({GROUND_CONTRACT_HEIGHT_DEFAULT_PATH})가 아님")
            if GROUND_CONTRACT_HEIGHT_DEFAULT_PATH not in record["loaded"]:
                problems.append(f"{label}: 땅 높이 기본 텍스처를 불러오지 않음")
        expected_created = [] if variant.master_path in existing else [variant.master_path]
        if record["created"] != expected_created:
            problems.append(f"{label}: 만든 에셋 {record['created']} ≠ {expected_created}")
        if FLAT_ASSET_PATH in existing and variant is not material.FLAT_VARIANT and FLAT_ASSET_PATH in record["loaded"]:
            problems.append(f"{label}: flat 에셋을 불러옴")
        report = record["report"].get(report_name, {})
        if report.get("status") != "ok" or report.get("master", {}).get("asset") != variant.master_path:
            problems.append(f"{label}: 보고 {report_name} 상태 {report.get('status')}")
        if set(record["report"]) != {report_name}:
            problems.append(f"{label}: 다른 보고 파일도 씀 {sorted(record['report'])}")
    refusals = (
        ("ground + inline", {"variant": "ground", "code_mode": "inline", "overwrite": True}, ()),
        ("ground 기존 에셋·overwrite 없음", {"variant": "ground"}, (GROUND_CONTRACT_ASSET_PATH,)),
        ("모르는 variant", {"variant": "slope"}, ()),
    )
    for label, request, existing in refusals:
        record = run_fake_editor(root, request, existing)
        if not record["error"]:
            problems.append(f"{label}: 중단하지 않음")
        if record["created"] or record["saved"]:
            problems.append(f"{label}: 에셋을 만들거나 저장함 {record['created']}")
    return problems


def run_checks(root, dxc_path, output_path, baseline_path=""):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    results = []
    summary = {}

    def record(label, passed, detail=""):
        results.append({"check": label, "passed": bool(passed), "detail": detail})
        print(("PASS " if passed else "FAIL ") + label + (": " + detail if detail else ""))

    shader_path = root.joinpath(*CONTRACT_SHADER_RELATIVE_PATH)
    if not shader_path.is_file():
        message = f"미실행: 그리는 식 셰이더 파일이 아직 없음 {shader_path}"
        print(message)
        print(json.dumps({"status": "not_run", "reason": message}, ensure_ascii=False))
        return EXIT_NOT_RUN

    try:
        material = load_material_script(root)
    except Exception as error:
        record("재질 스크립트 unreal 없이 import", False, repr(error))
        return finish(results, summary, output_path)
    record("재질 스크립트 unreal 없이 import", material.unreal is None, "unreal 모듈 없음 상태로 import")

    name_problems = find_name_contract_problems(material)
    record("입력 이름·파라미터 집합·기본값 = 계약 5절(Custom 입력 10 = 구역 기준 위치 + C++ 8 + 그래프 소광)", not name_problems, "; ".join(name_problems))
    collisions = find_look_table_collisions(root, material)
    if collisions is None:
        print("SKIP 모양 정본 JSON 에 런타임 이름 없음: TDFluidMaterialLook.json 없음")
    else:
        record("모양 정본 JSON 에 3D 안개 런타임 이름 없음(R7)", not collisions, ", ".join(collisions))

    shader_text = material.read_density_source(str(root))
    summary["ush_sha256"] = hashlib.sha256(shader_text.encode("utf-8")).hexdigest()
    raw_text = shader_path.read_text(encoding="utf-8")
    text_problems = material.find_shader_text_problems(raw_text)
    record(".ush 텍스트(주석 없음·절대 경로·ASCII·LF)", not text_problems, "; ".join(text_problems))
    body_problems = find_body_problems(material, shader_text)
    record("Custom 몸통 4줄 고정점(include 방식 = 4줄만, inline 방식 = .ush + 4줄)", not body_problems, "; ".join(body_problems))
    for code_mode, label in CODE_MODE_LABELS:
        code, include_paths = material.build_custom_code(code_mode, shader_text)
        problems = material.validate_custom_code(code_mode, code, include_paths, shader_text)
        record(f"{label} 코드 자체 점검", not problems, "; ".join(problems))

    cpp_missing, cpp_pattern = find_cpp_name_problems(root, material)
    if cpp_missing is None:
        print(f"SKIP C++ 이름 사용 확인: {cpp_pattern} 없음")
    else:
        record(f"C++ 파라미터 이름 8개가 {cpp_pattern} 에 모두 있음", not cpp_missing, ", ".join(cpp_missing))

    ground_path = root.joinpath(*GROUND_CONTRACT_SHADER_RELATIVE_PATH)
    ground_text = ""
    if not ground_path.is_file():
        record("ground .ush 있음", False, str(ground_path))
    else:
        ground_text = material.read_density_source(str(root), material.GROUND_VARIANT)
        summary["ground_ush_sha256"] = hashlib.sha256(ground_text.encode("utf-8")).hexdigest()
        ground_text_problems = material.find_shader_text_problems(ground_path.read_text(encoding="utf-8"))
        record("ground .ush 텍스트(주석 없음·절대 경로·ASCII·LF)", not ground_text_problems, "; ".join(ground_text_problems))
        ground_name_problems = find_ground_name_contract_problems(material)
        record("ground 입력 이름·파라미터 집합 = flat + 텍스처 Fog3DGroundHeight + 벡터 Fog3DGround, 에셋 M_TD_Fog3DVolumeGround, include 방식만", not ground_name_problems, "; ".join(ground_name_problems))
        ground_body_problems = find_ground_body_problems(material, ground_text)
        record("ground Custom 몸통 6줄·include 경로·inline 거부·.ush 가 기존 파일 하나만 include", not ground_body_problems, "; ".join(ground_body_problems))
        ground_code, ground_includes = material.build_custom_code("include", ground_text, material.GROUND_VARIANT)
        ground_code_problems = material.validate_custom_code("include", ground_code, ground_includes, ground_text, material.GROUND_VARIANT)
        record("ground include 방식 코드 자체 점검", not ground_code_problems, "; ".join(ground_code_problems))
        ground_cpp_missing, _ = find_cpp_name_problems(root, material.GROUND_VARIANT)
        if ground_cpp_missing is not None:
            record(f"ground C++ 파라미터 이름 10개가 {cpp_pattern} 에 모두 있음", not ground_cpp_missing, ", ".join(ground_cpp_missing))
        fake_problems = find_fake_editor_problems(root, material)
        record("가짜 unreal 로 재질 만들기 흐름(flat 기본·ground·ground 덮어쓰기 = 그래프·보고, ground+inline·기존 에셋·모르는 variant = 중단)", not fake_problems, "; ".join(fake_problems))

    dxc = find_dxc(dxc_path)
    if not dxc:
        record("dxc 찾기", False, "Windows Kits dxc.exe 없음(--dxc 로 지정)")
        return finish(results, summary, output_path)
    record("dxc 찾기", True, dxc)
    summary["dxc"] = {}
    with tempfile.TemporaryDirectory() as folder:
        work_folder = pathlib.Path(folder)
        for code_mode, label in CODE_MODE_LABELS:
            code, include_paths = material.build_custom_code(code_mode, shader_text)
            try:
                staged = set()
                for include_path in include_paths:
                    stage_shader_file(root, include_path, work_folder, staged)
            except FileNotFoundError as error:
                record(f"{label} include 준비", False, str(error))
                continue
            source = build_source(material.CUSTOM_INPUTS, code, include_paths)
            for optimize in (True, False):
                suffix = "O3" if optimize else "Od"
                outcome = compile_source(dxc, source, f"fog3d_{code_mode}_{suffix}", optimize, work_folder)
                record(f"dxc {label} ps_6_0 {suffix}", outcome["exit"] == 0 and outcome["samples"] >= 1, outcome["error"] or f"표본 {outcome['samples']} 명령 {outcome['instructions']}")
                summary["dxc"][f"{code_mode}_{suffix}"] = {"samples": outcome["samples"], "instructions": outcome["instructions"]}
        if ground_text:
            compile_ground(root, material, ground_text, dxc, work_folder, record, summary)
    compare_flat_baseline(baseline_path, summary, record)
    print_flat_ground_table(summary)
    return finish(results, summary, output_path)


def compile_ground(root, material, ground_text, dxc, work_folder, record, summary):
    code, include_paths = material.build_custom_code("include", ground_text, material.GROUND_VARIANT)
    ground_folder = work_folder / "ground"
    try:
        staged = set()
        for include_path in include_paths:
            stage_shader_file(root, include_path, ground_folder, staged)
    except FileNotFoundError as error:
        record("ground include 방식 include 준비", False, str(error))
        return
    source = build_source(material.GROUND_VARIANT.custom_inputs, code, include_paths, HEADER + GROUND_HEADER)
    for optimize in (True, False):
        suffix = "O3" if optimize else "Od"
        outcome = compile_source(dxc, source, f"fog3d_ground_include_{suffix}", optimize, ground_folder)
        record(f"dxc ground include 방식 ps_6_0 {suffix}", outcome["exit"] == 0 and outcome["samples"] >= 2, outcome["error"] or f"표본 {outcome['samples']} 명령 {outcome['instructions']}")
        summary["dxc"][f"ground_include_{suffix}"] = {"samples": outcome["samples"], "instructions": outcome["instructions"]}


def compare_flat_baseline(baseline_path, summary, record):
    if not baseline_path:
        return
    baseline = json.loads(pathlib.Path(baseline_path).read_text(encoding="utf-8"))
    keys = [f"{code_mode}_{suffix}" for code_mode, _ in CODE_MODE_LABELS for suffix in ("O3", "Od")]
    differences = [f"{key} {baseline.get('dxc', {}).get(key)} → {summary.get('dxc', {}).get(key)}" for key in keys if baseline.get("dxc", {}).get(key) != summary.get("dxc", {}).get(key)]
    if baseline.get("ush_sha256") != summary.get("ush_sha256"):
        differences.append("flat .ush sha256 다름")
    record(f"flat 결과(.ush sha256·방식별 표본·명령 수) = 기준 {baseline_path}", not differences, "; ".join(differences))


def print_flat_ground_table(summary):
    dxc = summary.get("dxc", {})
    for suffix in ("O3", "Od"):
        flat = dxc.get(f"include_{suffix}")
        ground = dxc.get(f"ground_include_{suffix}")
        if not flat or not ground:
            continue
        line = f"flat 대 ground (include {suffix}): 표본 {flat['samples']} → {ground['samples']} ({ground['samples'] - flat['samples']:+d}), 명령 {flat['instructions']} → {ground['instructions']} ({ground['instructions'] - flat['instructions']:+d})"
        summary.setdefault("flat_vs_ground", {})[suffix] = {"samples_added": ground["samples"] - flat["samples"], "instructions_added": ground["instructions"] - flat["instructions"]}
        print("INFO " + line)


def finish(results, summary, output_path):
    failed = [item["check"] for item in results if not item["passed"]]
    report = {"passed": len(results) - len(failed), "failed": failed, **summary}
    if output_path:
        pathlib.Path(output_path).write_text(json.dumps(report, ensure_ascii=False, indent=1) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps(report, ensure_ascii=False))
    return 1 if failed else 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parents[2]))
    parser.add_argument("--dxc", default="")
    parser.add_argument("--output", default="")
    parser.add_argument("--baseline", default="")
    arguments = parser.parse_args()
    return run_checks(pathlib.Path(arguments.root), arguments.dxc, arguments.output, arguments.baseline)


if __name__ == "__main__":
    sys.exit(main())
