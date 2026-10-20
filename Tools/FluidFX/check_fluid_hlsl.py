"""실행 환경: 시스템 Python 3 와 Windows Kits dxc.exe(ps_6_0·vs_6_0), 에디터 불필요
실행 명령: python Tools/FluidFX/check_fluid_hlsl.py [--root 프로젝트경로] [--dxc dxc경로]
출력: 항목별 PASS/FAIL 줄과 마지막 줄 JSON 요약, 하나라도 실패하면 종료 코드 1
상태: 미검증(첫 성공 전). 물 셰이더 3개(TDFluidWaterCommon·Surface·Offset.hlsl)를 재질 스크립트와 같은 조립 함수로 노드별 조립해 엔진 순서(정점 오프셋이 먼저)대로 한 파일에 이어 붙여 컴파일하고, 정점용 매크로가 표면 함수로 새면(표본 명령 수가 단독 합과 다르면) 실패한다. 매크로를 지우지 않는 조립을 음성 대조로 넣어 검사의 검출력을 함께 확인한다. C1 추가분: 리본 입력(RibbonTangentWS·RibbonFlowCmPerSec)·C++ 구동 파라미터 계약(MID_DRIVEN_*)·무대 강 배치 자체 점검(validate_river_layout)도 함께 본다. W-2r 추가분: 젖음 데칼 HLSL(TDFluidWaterWetDecal.hlsl) 텍스트·자체 점검·dxc 컴파일, 리본 로컬 UV 정적 스위치 0·1 순열 컴파일(표본 수 불변), 인스턴스 정적 스위치 이름 대조.
"""
import argparse
import glob
import importlib.util
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
from unittest import mock

sys.dont_write_bytecode = True

HEADER = """
float GradientNoise3D_ALU(float3 v, bool bTiling, float RepeatSize) { return frac(sin(dot(v, float3(12.9898, 78.233, 37.719))) * 43758.5453) * 2.0 - 1.0; }
#define Texture2DSample(T, S, UV) T.Sample(S, UV)
#define Texture2DSampleLevel(T, S, UV, L) T.SampleLevel(S, UV, L)
"""
SURFACE_SIGNATURE = (
    "float4 TDSurface(float4 SvPositionValue, out float3 OutNormalWS, out float3 OutBaseColor, out float OutRoughness, "
    "out float OutSpecular, out float OutTopOpacity, out float3 OutColorScaleBehindWater, out float3 OutLiteColor, out float OutTranslucentOpacity, out float3 OutTranslucentColor, out float3 OutEmissive)"
)
SURFACE_CALL = (
    "float3 n; float3 b; float3 c; float3 d; float3 e; float3 m; float r; float s; float t; float o; "
    "float4 f = TDSurface(sv, n, b, r, s, t, c, d, o, e, m);"
)
SAMPLE_PATTERN = re.compile(r"call \S+ @dx\.op\.(?:sample|sampleLevel|sampleBias|sampleGrad)\.")
ABSOLUTE_PATH_PATTERN = re.compile(r"[A-Za-z]:[\\/]")


def load_script(root, name):
    spec = importlib.util.spec_from_file_location(name, root / "Tools" / "FluidFX" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def find_dxc(explicit_path):
    candidates = [explicit_path, os.environ.get("DXC_PATH")]
    program_files = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    found = sorted(glob.glob(os.path.join(program_files, "Windows Kits", "10", "bin", "*", "x64", "dxc.exe")))
    candidates.extend(reversed(found))
    for candidate in candidates:
        if candidate and os.path.isfile(candidate):
            return candidate
    return ""


def declare_inputs(material):
    texture_names = set(material.TEXTURE_PARAMETER_NAMES)
    vector_names = {name for name, _ in material.VECTOR_PARAMETER_DEFAULTS} | set(material.BUILTIN_VECTOR_SOURCE_NAMES)
    float2_names = set(material.BUILTIN_FLOAT2_SOURCE_NAMES)
    input_names = list(dict.fromkeys(material.SURFACE_INPUT_NAMES + material.OFFSET_INPUT_NAMES))
    textures, scalars = [], []
    for name in input_names:
        if name in texture_names:
            textures.append(f"Texture2D {name};\nSamplerState {name}Sampler;")
            continue
        kind = "float2" if name in float2_names else "float3" if name in vector_names else "float"
        scalars.append(f"{kind} {name};")
    return "\n".join(textures) + "\ncbuffer Inputs {\n" + "\n".join(scalars) + "\n}\n"


def function_source(kind, code):
    if kind == "surface":
        return f"{SURFACE_SIGNATURE}\n{{\n{code}\n}}\n"
    return f"float3 TDOffset(float4 SvPositionValue)\n{{\n{code}\n}}\n"


def build_source(declarations, entries, order):
    functions = "".join(function_source(kind, entries[kind]) for kind in order)
    calls = ""
    if "offset" in order:
        calls += "float3 a = TDOffset(sv);\n"
    if "surface" in order:
        calls += SURFACE_CALL + "\n"
    if order == ("offset",):
        body = f"{calls}return float4(a, 1.0);"
    elif order == ("surface",):
        body = f"{calls}return f + float4(n + b + c + d + m, r + s + t);"
    else:
        body = f"{calls}return f + float4(a + n + b + c + d + m, r + s + t);"
    return HEADER + declarations + functions + f"float4 main(float4 sv : SV_Position) : SV_Target\n{{\n{body}\n}}\n"


def declare_names(material, names):
    texture_names = set(material.TEXTURE_PARAMETER_NAMES)
    vector_names = {name for name, _ in material.VECTOR_PARAMETER_DEFAULTS} | set(material.BUILTIN_VECTOR_SOURCE_NAMES)
    textures = [f"Texture2D {name};\nSamplerState {name}Sampler;" for name in names if name in texture_names]
    scalars = [f"{'float3' if name in vector_names else 'float'} {name};" for name in names if name not in texture_names]
    return "\n".join(textures) + "\ncbuffer Inputs {\n" + "\n".join(scalars) + "\n}\n"


def build_wet_decal_source(material, code):
    return (
        HEADER + declare_names(material, material.WET_DECAL_INPUT_NAMES) + f"float TDWetDecal(float4 SvPositionValue)\n{{\n{code}\n}}\n"
        + "float4 main(float4 sv : SV_Position) : SV_Target\n{\nreturn TDWetDecal(sv).xxxx;\n}\n"
    )


def fold_static_switches(declarations, names, value):
    for name in names:
        declarations = declarations.replace(f"float {name};\n", "")
    return "".join(f"static const float {name} = {value};\n" for name in names) + declarations


def build_vertex_source(declarations, entries):
    return (
        HEADER + declarations + function_source("offset", entries["offset"])
        + "float4 main(float4 position : POSITION) : SV_Position\n{\nfloat3 a = TDOffset(position);\nreturn float4(a, 1.0);\n}\n"
    )


def compile_source(dxc, source, label, profile, optimize, work_folder):
    path = work_folder / f"{label}.hlsl"
    disassembly = work_folder / f"{label}.dxil.txt"
    path.write_text(source, encoding="utf-8")
    command = [dxc, "-T", profile, "-E", "main", str(path), "-Fc", str(disassembly), "-Fo", str(work_folder / f"{label}.bin"), "-O3" if optimize else "-Od"]
    completed = subprocess.run(command, capture_output=True, text=True)
    text = disassembly.read_text(encoding="utf-8", errors="replace") if disassembly.exists() else ""
    sample_count = len(SAMPLE_PATTERN.findall(text))
    warnings = completed.stderr.count("warning:")
    first_error = next((line for line in (completed.stderr + completed.stdout).splitlines() if "error" in line), "")
    return {"exit": completed.returncode, "samples": sample_count, "warnings": warnings, "error": first_error}


def naive_entries(common_code, node_codes):
    return {
        "offset": "#define TD_WATER_VERTEX 1\n" + common_code + "\n" + node_codes["offset"],
        "surface": common_code + "\n" + node_codes["surface"],
    }


def run_shader_text_checks(root, shader_names):
    failures = []
    shader_root = root / "Tools" / "FluidFX" / "Shaders"
    for name in shader_names:
        text = (shader_root / name).read_text(encoding="utf-8")
        if "//" in text or "/*" in text:
            failures.append(f"{name}: 주석이 있음")
        if ABSOLUTE_PATH_PATTERN.search(text):
            failures.append(f"{name}: 절대 경로가 있음")
        if not text.isascii():
            failures.append(f"{name}: ASCII 가 아닌 문자가 있음")
        if "\r" in text:
            failures.append(f"{name}: CRLF 줄바꿈")
    return failures


def run_contract_checks(material):
    failures = []
    resolvable = set(material.BUILTIN_SOURCE_NAMES) | set(material.TEXTURE_PARAMETER_NAMES)
    resolvable |= {name for name, _ in material.VECTOR_PARAMETER_DEFAULTS}
    resolvable |= {name for name, _ in material.SCALAR_PARAMETER_DEFAULTS}
    resolvable |= set(material.STATIC_SWITCH_NAMES)
    for label, names in (("표면", material.SURFACE_INPUT_NAMES), ("오프셋", material.OFFSET_INPUT_NAMES), ("젖음 데칼", material.WET_DECAL_INPUT_NAMES)):
        if len(set(names)) != len(names):
            failures.append(f"{label} 입력 이름 중복")
        unresolved = sorted(set(names) - resolvable)
        if unresolved:
            failures.append(f"{label} 입력의 원천 없음: " + ", ".join(unresolved))
    typed_driven = (
        ("텍스처", material.MID_DRIVEN_TEXTURE_NAMES, set(material.TEXTURE_PARAMETER_NAMES)),
        ("벡터", material.MID_DRIVEN_VECTOR_NAMES, {name for name, _ in material.VECTOR_PARAMETER_DEFAULTS}),
        ("스칼라", material.MID_DRIVEN_SCALAR_NAMES, {name for name, _ in material.SCALAR_PARAMETER_DEFAULTS}),
    )
    surface_and_offset = set(material.SURFACE_INPUT_NAMES) | set(material.OFFSET_INPUT_NAMES)
    for label, driven_names, declared_names in typed_driven:
        undeclared = [name for name in driven_names if name not in declared_names]
        if undeclared:
            failures.append(f"C++ 구동 {label} 파라미터가 마스터 선언에 없음: " + ", ".join(undeclared))
        unused = [name for name in driven_names if name not in surface_and_offset]
        if unused:
            failures.append(f"C++ 구동 {label} 파라미터가 셰이더 입력에 없음: " + ", ".join(unused))
    shadowed = material.find_shadowed_mid_parameters()
    if shadowed:
        failures.append("인스턴스가 C++ 구동 파라미터를 덮어씀: " + ", ".join(shadowed))
    primitive_indices = material.PRIMITIVE_DATA_INDEX_BY_PARAMETER
    scalar_names = {name for name, _ in material.SCALAR_PARAMETER_DEFAULTS}
    if len(set(primitive_indices.values())) != len(primitive_indices):
        failures.append("Custom Primitive Data 인덱스 중복")
    primitive_not_scalar = sorted(set(primitive_indices) - scalar_names)
    if primitive_not_scalar:
        failures.append("Custom Primitive Data 이름이 스칼라 선언에 없음: " + ", ".join(primitive_not_scalar))
    for spec in material.INSTANCE_SPECS:
        overridable = {name for name, _ in material.VECTOR_PARAMETER_DEFAULTS} | {name for name, _ in material.SCALAR_PARAMETER_DEFAULTS}
        unknown = sorted((set(spec["vectors"]) | set(spec["scalars"])) - overridable)
        if unknown:
            failures.append(f"{spec['name']} 덮어쓰기 이름이 마스터에 없음: " + ", ".join(unknown))
        unknown_switches = sorted(set(spec.get("switches", {})) - set(material.STATIC_SWITCH_NAMES))
        if unknown_switches:
            failures.append(f"{spec['name']} 정적 스위치 이름이 마스터에 없음: " + ", ".join(unknown_switches))
    return failures


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parents[2]))
    parser.add_argument("--dxc", default="")
    arguments = parser.parse_args()
    root = pathlib.Path(arguments.root)
    results = []

    def record(label, passed, detail=""):
        results.append({"check": label, "passed": bool(passed), "detail": detail})
        print(("PASS " if passed else "FAIL ") + label + (": " + detail if detail else ""))

    sys.modules["unreal"] = mock.MagicMock()
    material = load_script(root, "editor_make_fluid_water_material")
    record("재질 스크립트 unreal 스텁 임포트", True)
    stage_path = root / "Tools" / "FluidFX" / "editor_make_fluid_water_stage.py"
    if stage_path.is_file():
        stage = load_script(root, "editor_make_fluid_water_stage")
        record("무대 스크립트 unreal 스텁 임포트", True)
        layout_problems = stage.validate_river_layout()
        record("무대 강 배치(폭·깊이·기울기·바위·탐침 좌표·창 안쪽·호수 불변)", not layout_problems, "; ".join(layout_problems))
    else:
        record("무대 스크립트 unreal 스텁 임포트", False, "editor_make_fluid_water_stage.py 없음")

    shader_names = (material.COMMON_HLSL_NAME, material.SURFACE_HLSL_NAME, material.OFFSET_HLSL_NAME, material.WET_DECAL_HLSL_NAME)
    text_failures = run_shader_text_checks(root, shader_names)
    record("셰이더 텍스트(주석·절대 경로·ASCII·LF)", not text_failures, "; ".join(text_failures))

    contract_failures = run_contract_checks(material)
    record("입력·파라미터 계약", not contract_failures, "; ".join(contract_failures))

    sources = material.read_shader_sources(str(root))
    codes = material.assemble_all_node_codes(sources)
    problems = material.validate_all_node_codes(codes)
    record("조립 코드 자체 점검", not problems, "; ".join(problems))

    try:
        wet_decal_code = material.read_wet_decal_code(str(root))
        record("젖음 데칼 코드 자체 점검", True)
    except RuntimeError as error:
        wet_decal_code = ""
        record("젖음 데칼 코드 자체 점검", False, str(error))

    offset_text = codes["offset"]
    pixel_only_in_offset = [token for token in ("SceneDepthWithoutWaterCm", "PixelDepthCm", "Texture2DSample(") if token in offset_text]
    record("정점 오프셋에 픽셀 전용 호출 없음", not pixel_only_in_offset, ", ".join(pixel_only_in_offset))

    dxc = find_dxc(arguments.dxc)
    if not dxc:
        record("dxc 찾기", False, "Windows Kits dxc.exe 없음(--dxc 로 지정)")
        return finish(results)
    record("dxc 찾기", True, dxc)

    declarations = declare_inputs(material)
    entries = {"surface": codes["surface"], "offset": codes["offset"]}
    with tempfile.TemporaryDirectory() as folder:
        work_folder = pathlib.Path(folder)
        counts = {}
        for optimize in (False, True):
            suffix = "O3" if optimize else "Od"
            surface = compile_source(dxc, build_source(declarations, entries, ("surface",)), f"surface_{suffix}", "ps_6_0", optimize, work_folder)
            offset_pixel = compile_source(dxc, build_source(declarations, entries, ("offset",)), f"offset_ps_{suffix}", "ps_6_0", optimize, work_folder)
            offset_vertex = compile_source(dxc, build_vertex_source(declarations, entries), f"offset_vs_{suffix}", "vs_6_0", optimize, work_folder)
            for label, outcome in (("표면 ps_6_0", surface), ("오프셋 ps_6_0", offset_pixel), ("오프셋 vs_6_0", offset_vertex)):
                record(f"dxc {label} {suffix}", outcome["exit"] == 0, outcome["error"] or f"표본 {outcome['samples']} 경고 {outcome['warnings']}")
            counts[suffix] = {"surface": surface["samples"], "offset": offset_pixel["samples"], "vertex": offset_vertex["samples"]}

        record(
            "표면 표본 수가 정점 오프셋보다 3배 이상(탭 5 대 1)",
            counts["Od"]["surface"] >= 3 * counts["Od"]["offset"] > 0,
            f"표면 {counts['Od']['surface']} 오프셋 {counts['Od']['offset']}",
        )
        record("오프셋 ps 와 vs 표본 수 동일", counts["Od"]["offset"] == counts["Od"]["vertex"], f"{counts['Od']['offset']} / {counts['Od']['vertex']}")

        ribbon_switches = [name for name in material.STATIC_SWITCH_NAMES if name.startswith("bRibbonLocal")]
        for value in (0, 1):
            folded = fold_static_switches(declarations, ribbon_switches, f"{value}.0")
            permutation = compile_source(dxc, build_source(folded, entries, ("surface",)), f"surface_ribbon_{value}", "ps_6_0", True, work_folder)
            record(
                f"리본 로컬 UV 정적 스위치 {value} 순열 컴파일·표본 수 동일",
                permutation["exit"] == 0 and permutation["samples"] == counts["O3"]["surface"],
                permutation["error"] or f"표본 {permutation['samples']} 기대 {counts['O3']['surface']}",
            )

        if wet_decal_code:
            decal = compile_source(dxc, build_wet_decal_source(material, wet_decal_code), "wet_decal", "ps_6_0", True, work_folder)
            record("dxc 젖음 데칼 ps_6_0 O3", decal["exit"] == 0 and decal["samples"] == 1, decal["error"] or f"표본 {decal['samples']} 경고 {decal['warnings']}")

        expected_total = counts["Od"]["surface"] + counts["Od"]["offset"]
        for order in (("offset", "surface"), ("surface", "offset")):
            label = "_".join(order)
            combined = compile_source(dxc, build_source(declarations, entries, order), f"combined_{label}", "ps_6_0", False, work_folder)
            passed = combined["exit"] == 0 and combined["samples"] == expected_total
            record(f"매크로 새지 않음({'→'.join(order)})", passed, combined["error"] or f"합 {combined['samples']} 기대 {expected_total}")

        control_entries = naive_entries(sources["common"], {"offset": sources["offset"], "surface": sources["surface"]})
        control = compile_source(dxc, build_source(declarations, control_entries, ("offset", "surface")), "control_naive", "ps_6_0", False, work_folder)
        record(
            "음성 대조: 매크로를 지우지 않는 조립은 누수를 검출",
            control["exit"] == 0 and control["samples"] != expected_total,
            f"합 {control['samples']} 기대 {expected_total}",
        )
    return finish(results)


def finish(results):
    failed = [item["check"] for item in results if not item["passed"]]
    print(json.dumps({"passed": len(results) - len(failed), "failed": failed}, ensure_ascii=False))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
