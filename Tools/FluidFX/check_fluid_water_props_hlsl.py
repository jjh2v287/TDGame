"""실행 환경: 시스템 Python 3 와 Windows Kits dxc.exe(ps_6_0), 에디터 불필요
실행 명령: python Tools/FluidFX/check_fluid_water_props_hlsl.py [--root 프로젝트경로] [--dxc dxc경로] [--look-fragment 조각.json] [--output 요약.json]
출력: 항목별 PASS/FAIL 줄과 마지막 줄 JSON 요약(HLSL sha256, dxc 명령·표본 수), 하나라도 실패하면 종료 코드 1
상태: 미검증(2026-10-29 W-5 M 작성). editor_make_fluid_water_props_material.py 를 가짜 unreal 로 불러 마스터 5·MI 10 이름, HLSL 4개 텍스트(주석·ASCII·LF·절대 경로·모양 상수 직접 쓰기 금지), Custom 입력 목록 ⊂ HLSL 식별자와 spec-w5 2b 이름 계약, 수중 마스터 중립 기본값(M4)·판 속도 기준 기본 600(M5), MI 이름 계약(부모·마스터 이름·C++ 구동 이름 없음)을 보고 dxc ps_6_0 O3/Od 로 마스터 4개를 컴파일한다. 정본에 조각이 아직 병합되지 않았으면 --look-fragment 로 조각을 더해 본다.
"""
import argparse
import hashlib
import importlib.util
import json
import pathlib
import re
import sys
import tempfile
from unittest import mock

sys.dont_write_bytecode = True

HEADER = """
#define Texture2DSample(T, S, UV) T.Sample(S, UV)
#define Texture2DSampleLevel(T, S, UV, L) T.SampleLevel(S, UV, L)
"""
ABSOLUTE_PATH_PATTERN = re.compile(r"[A-Za-z]:[\\/]")
IDENTIFIER_PATTERN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
SIMULATION_MID_NAMES = (
    "DensityPressureTexture", "VelocityTexture", "SimCenterWorld", "SimExtentsCm", "PlaneAxisU", "PlaneAxisV",
    "PlaneCoordinateOffsetCm", "VelocitySpeedReferenceCmPerSec",
)
ENGINE_NODE_NAMES = (
    "PixelWorldPosition", "ObjectCenterWorld", "ObjectBoundsCm", "CardRightWorld", "CameraVectorWorld", "CameraWorldPosition",
    "EdgeUv", "SceneColor", "ElapsedSeconds",
)
NOISE_TEXTURE_NAMES = ("FoamNoiseTexture", "FlowNoiseTexture")
NOISE_TEXTURE_PATHS = {
    "FoamNoiseTexture": "/Game/FluidFX/Textures/T_TD_FluidFoamNoise",
    "FlowNoiseTexture": "/Game/FluidFX/Textures/T_TD_FluidFlowPhaseNoise",
}
SHAPE_PREFIXES = ("Sphere", "Sheet", "Far", "Underwater", "Glass")
EXPECTED_MASTER_NAMES = (
    "M_TD_FluidLiquidSphere", "M_TD_FluidWaterfallSheet", "M_TD_FluidWaterfallFar", "M_TD_FluidUnderwaterPostProcess", "M_TD_FluidWaterPropGlass",
)
EXPECTED_INSTANCE_NAMES = (
    "MI_TD_FluidLiquidSphere_Clear", "MI_TD_FluidLiquidSphere_Turbulent", "MI_TD_FluidLiquidSphere_BloodOrb", "MI_TD_FluidWaterfallSheet_Default",
    "MI_TD_FluidWaterfallFar_White", "MI_TD_FluidWaterfallFar_Blue", "MI_TD_FluidUnderwater_Lake", "MI_TD_FluidUnderwater_Tank",
    "MI_TD_FluidWaterTankWall",
)
FORBIDDEN_SHAPE_LITERALS = ("0.4 +", "0.6 *", "0.5 + 0.5", "* 4.0")
MINIMUM_SAMPLES = {"sphere": 1, "sheet": 4, "far": 3, "underwater": 0}
NEUTRAL_UNDERWATER_SCALARS = {"UnderwaterFogDensityPerCm": 0.0, "UnderwaterSurfaceZCm": 1.0e7}
NEUTRAL_UNDERWATER_VECTORS = {"UnderwaterAbsorptionPerCm": (0.0, 0.0, 0.0, 0.0)}
SHEET_SPEED_REFERENCE_DEFAULT = 600.0


def load_module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run_shader_text_checks(text):
    failures = []
    if "//" in text or "/*" in text:
        failures.append("주석이 있음")
    if ABSOLUTE_PATH_PATTERN.search(text):
        failures.append("절대 경로가 있음")
    if not text.isascii():
        failures.append("ASCII 가 아닌 문자가 있음")
    if "\r" in text:
        failures.append("CRLF 줄바꿈")
    failures.extend(f"모양 상수를 직접 씀 {literal!r}" for literal in FORBIDDEN_SHAPE_LITERALS if literal in text)
    return failures


def declare_inputs(material, spec):
    texture_lines = []
    value_lines = []
    for name in spec["inputs"]:
        kind = material.input_kind(spec, name)
        if kind == "texture":
            texture_lines += [f"Texture2D {name};", f"SamplerState {name}Sampler;"]
        else:
            value_lines.append(f"{kind} {name};")
    return "\n".join(texture_lines) + "\ncbuffer Inputs {\n" + "\n".join(value_lines) + "\n}\n"


def build_pixel_source(material, spec, code):
    result_call = "return TDWaterProp();" if spec["output"] == "float4" else "return float4(TDWaterProp(), 1.0);"
    return (HEADER + declare_inputs(material, spec) + f"{spec['output']} TDWaterProp()\n{{\n{code}\n}}\n"
            + "float4 main(float4 sv : SV_Position) : SV_Target\n{\n" + result_call + "\n}\n")


def find_input_contract_problems(material, spec, code):
    problems = []
    identifiers = set(IDENTIFIER_PATTERN.findall(code))
    unused = [name for name in spec["inputs"] if name not in identifiers]
    if unused:
        problems.append("HLSL이 쓰지 않는 입력 " + ", ".join(unused))
    unused_samplers = [name for name in material.sampler_names(spec) if name not in identifiers]
    if unused_samplers:
        problems.append("HLSL이 쓰지 않는 샘플러 " + ", ".join(unused_samplers))
    scalar_names = {name for name, _ in spec["scalars"]}
    vector_names = {name for name, _ in spec["vectors"]}
    sourced = set(spec["textures"]) | (set(ENGINE_NODE_NAMES) & set(spec["inputs"])) | scalar_names | vector_names
    unsourced = [name for name in spec["inputs"] if name not in sourced]
    if unsourced:
        problems.append("값을 주는 노드가 없는 입력 " + ", ".join(unsourced))
    allowed = set(SIMULATION_MID_NAMES) | set(ENGINE_NODE_NAMES) | set(NOISE_TEXTURE_NAMES)
    outside = [name for name in spec["inputs"] if name not in allowed and not name.startswith(SHAPE_PREFIXES)]
    if outside:
        problems.append("2b 이름 계약 밖 입력 " + ", ".join(outside))
    direct_outside = [name for name, _ in spec["direct_scalar_outputs"] if name not in scalar_names]
    if direct_outside:
        problems.append("직접 연결 스칼라가 마스터에 없음 " + ", ".join(direct_outside))
    return problems


def find_master_default_problems(material):
    problems = []
    underwater = material.master_spec("underwater")
    scalars = dict(underwater["scalars"])
    vectors = dict(underwater["vectors"])
    for name, expected in NEUTRAL_UNDERWATER_SCALARS.items():
        if scalars.get(name) != expected:
            problems.append(f"수중 마스터 {name} 기본 {scalars.get(name)} ≠ 중립 {expected}(M4)")
    for name, expected in NEUTRAL_UNDERWATER_VECTORS.items():
        if tuple(vectors.get(name, ())) != expected:
            problems.append(f"수중 마스터 {name} 기본 {vectors.get(name)} ≠ 중립 {expected}(M4)")
    sheet_scalars = dict(material.master_spec("sheet")["scalars"])
    if sheet_scalars.get("VelocitySpeedReferenceCmPerSec") != SHEET_SPEED_REFERENCE_DEFAULT:
        problems.append(f"판 마스터 VelocitySpeedReferenceCmPerSec 기본 {sheet_scalars.get('VelocitySpeedReferenceCmPerSec')} ≠ {SHEET_SPEED_REFERENCE_DEFAULT}(M5)")
    return problems


def find_fragment_runtime_name_problems(material, fragment):
    problems = []
    for package_path, entry in fragment.get("Materials", {}).items():
        names = set(entry.get("Scalars", {})) | set(entry.get("Vectors", {})) | set(entry.get("Switches", {}))
        runtime = sorted(names & set(material.RUNTIME_DRIVEN_NAMES))
        if runtime:
            problems.append(f"{package_path}: " + ", ".join(runtime))
    return problems


def read_fragment(fragment_path):
    if not fragment_path:
        return {}
    return json.loads(pathlib.Path(fragment_path).read_text(encoding="utf-8"))


def collect_look_entries(root, fragment):
    entries = {}
    canonical = root / "Content" / "FluidFX" / "PresetData" / "TDFluidMaterialLook.json"
    if canonical.is_file():
        entries.update(json.loads(canonical.read_text(encoding="utf-8")).get("Materials", {}))
    overlap = sorted(set(fragment.get("Materials", {})) & set(entries))
    entries.update(fragment.get("Materials", {}))
    return entries, overlap


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parents[2]))
    parser.add_argument("--dxc", default="")
    parser.add_argument("--look-fragment", default="")
    parser.add_argument("--output", default="")
    arguments = parser.parse_args()
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    root = pathlib.Path(arguments.root)
    results = []
    summary = {}

    def record(label, passed, detail=""):
        results.append({"check": label, "passed": bool(passed), "detail": detail})
        print(("PASS " if passed else "FAIL ") + label + (": " + detail if detail else ""))

    sys.modules["unreal"] = mock.MagicMock()
    material = load_module(root / "Tools" / "FluidFX" / "editor_make_fluid_water_props_material.py", "editor_make_fluid_water_props_material")
    record("재질 스크립트 unreal 스텁 임포트", True)

    master_names = tuple(spec["name"] for spec in material.MASTER_SPECS)
    instance_names = tuple(name for name, _ in material.INSTANCE_SPECS)
    record("마스터 5개 이름", master_names == EXPECTED_MASTER_NAMES, ", ".join(master_names))
    record("스크립트 MI 9개 + 모래 흐름 1개 = 10", instance_names == EXPECTED_INSTANCE_NAMES and material.SAND_INSTANCE_NAME == "MI_TD_Fluid_WaterySand", f"{len(instance_names)} + {material.SAND_INSTANCE_NAME}")
    record("내장 입력 이름 = 2b 엔진 노드 행", set(material.BUILTIN_KINDS) == set(ENGINE_NODE_NAMES), ", ".join(sorted(set(material.BUILTIN_KINDS) ^ set(ENGINE_NODE_NAMES))))
    record("노이즈 텍스처 경로 = 2.6", dict(material.TEXTURE_ASSET_PATHS) == NOISE_TEXTURE_PATHS, str(material.TEXTURE_ASSET_PATHS))
    default_problems = find_master_default_problems(material)
    record("마스터 기본값(수중 중립 M4·판 속도 기준 600 M5)", not default_problems, "; ".join(default_problems))

    codes = {}
    shader_root = root / "Tools" / "FluidFX" / "Shaders"
    for spec in material.MASTER_SPECS:
        if not material.has_hlsl(spec):
            continue
        text = (shader_root / spec["hlsl"]).read_bytes().decode("utf-8")
        codes[spec["key"]] = text.replace("\r\n", "\n").strip("\n") + "\n"
        text_failures = run_shader_text_checks(text)
        record(f"셰이더 텍스트 {spec['hlsl']}(주석·절대 경로·ASCII·LF·모양 상수)", not text_failures, "; ".join(text_failures))
        contract_problems = find_input_contract_problems(material, spec, codes[spec["key"]])
        record(f"입력 목록 ⊂ HLSL 식별자·2b 대조 {spec['name']}", not contract_problems, "; ".join(contract_problems))
        brackets = material.find_unbalanced_brackets(codes[spec["key"]])
        record(f"괄호 균형 {spec['hlsl']}", not brackets, brackets)

    fragment = read_fragment(arguments.look_fragment)
    entries, overlap = collect_look_entries(root, fragment)
    override_problems = material.find_instance_override_problems(entries)
    record("MI 이름 계약(부모·마스터 이름·구동 이름 없음·시작값 전부)", not override_problems, "; ".join(override_problems))
    record("조각과 정본 키 서로소", not overlap or not arguments.look_fragment, ", ".join(overlap))
    runtime_problems = find_fragment_runtime_name_problems(material, fragment)
    record("MID 구동 이름이 조각에 없음", not runtime_problems, "; ".join(runtime_problems))

    summary["hlsl_sha256"] = {key: hashlib.sha256(code.encode("utf-8")).hexdigest() for key, code in codes.items()}

    sys.path.insert(0, str(root / "Tools" / "FluidFX"))
    hlsl_checker = load_module(root / "Tools" / "FluidFX" / "check_fluid_hlsl.py", "check_fluid_hlsl")
    dxc = hlsl_checker.find_dxc(arguments.dxc)
    if not dxc:
        record("dxc 찾기", False, "Windows Kits dxc.exe 없음(--dxc 로 지정)")
        return finish(results, summary, arguments.output)
    record("dxc 찾기", True, dxc)
    compiles = {}
    with tempfile.TemporaryDirectory() as folder:
        work_folder = pathlib.Path(folder)
        for spec in material.MASTER_SPECS:
            if spec["key"] not in codes:
                continue
            source = build_pixel_source(material, spec, codes[spec["key"]])
            for optimize in (False, True):
                suffix = "O3" if optimize else "Od"
                outcome = hlsl_checker.compile_source(dxc, source, f"{spec['key']}_{suffix}", "ps_6_0", optimize, work_folder)
                passed = outcome["exit"] == 0 and outcome["samples"] >= MINIMUM_SAMPLES[spec["key"]]
                record(f"dxc {spec['name']} ps_6_0 {suffix}", passed, outcome["error"] or f"표본 {outcome['samples']} 경고 {outcome['warnings']}")
                compiles[f"{spec['key']}_{suffix}"] = {"samples": outcome["samples"], "warnings": outcome["warnings"]}
    summary["dxc"] = compiles
    return finish(results, summary, arguments.output)


def finish(results, summary, output_path):
    failed = [item["check"] for item in results if not item["passed"]]
    report = {"passed": len(results) - len(failed), "failed": failed, **summary}
    if output_path:
        pathlib.Path(output_path).write_text(json.dumps(report, ensure_ascii=False, indent=1) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps(report, ensure_ascii=False))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
