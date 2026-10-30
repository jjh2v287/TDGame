"""실행 환경: 시스템 Python 3 와 Windows Kits dxc.exe(ps_6_0), 에디터 불필요
실행 명령: python Tools/FluidFX/check_fluid_rain_hlsl.py [--root 프로젝트경로] [--dxc dxc경로] [--look-fragment 조각.json] [--output 요약.json]
출력: 항목별 PASS/FAIL 줄과 마지막 줄 JSON 요약(dxc 표본·명령 수), 하나라도 실패하면 종료 코드 1
상태: 미검증(2026-10-29 W-3 D 작성). TDFluidRainDrop.hlsl·TDFluidRainPuddleDecal.hlsl 의 텍스트(주석·ASCII·LF)·입력 이름 계약(대문자 식별자는 모두 선언된 입력, 선언 입력은 모두 사용)·식 고정점(B6 floor 나눗셈·B8 고리 주기와 감쇠·해시 상수)·dxc(Od·O3)와, 조각이 있으면 MI 이름 계약(C++ 소유 이름 금지 R7)을 검사한다.
"""
import argparse
import glob
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True

HEADER = """
#define Texture2DSampleLevel(T, S, UV, L) T.SampleLevel(S, UV, L)
"""
SAMPLE_PATTERN = re.compile(r"call \S+ @dx\.op\.(?:sample|sampleLevel|sampleBias|sampleGrad)\.")
INSTRUCTION_PATTERN = re.compile(r"^\s+(?:%\S+ = |call |store |br |ret )", re.MULTILINE)
ABSOLUTE_PATH_PATTERN = re.compile(r"[A-Za-z]:[\\/]")
IDENTIFIER_PATTERN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
DROP_HLSL_NAME = "TDFluidRainDrop.hlsl"
PUDDLE_HLSL_NAME = "TDFluidRainPuddleDecal.hlsl"
DROP_MATERIAL_PATH = "/Game/FluidFX/Materials/MI_TD_FluidRainDrop_Default"
PUDDLE_MATERIAL_PATH = "/Game/FluidFX/Materials/MI_TD_FluidRainPuddleDecal_Default"
DROP_INPUTS = (
    ("ParticleColor", "float4"),
    ("TexCoord", "float2"),
    ("RainStreakWidthRatio", "float"),
    ("RainSplashRingWidth", "float"),
)
PUDDLE_INPUTS = (
    ("WaterSurfaceTexture", "texture"),
    ("WaterWorldPosition", "float3"),
    ("SimCenterWorld", "float3"),
    ("SimExtentsCm", "float3"),
    ("SimGridResolution", "float"),
    ("TerrainReferenceZCm", "float"),
    ("WaterDepthReferenceCm", "float"),
    ("RainPuddleLevelCm", "float"),
    ("RainPuddleCavityRadiusCm", "float"),
    ("RainPuddleCavityReferenceCm", "float"),
    ("RainIntensity", "float"),
    ("RainDropsPerSquareMeterPerSec", "float"),
    ("RainTimeSeconds", "float"),
    ("PuddleEdgeSoftnessCm", "float"),
    ("PuddleHeightToleranceCm", "float"),
    ("PuddleRippleNormalStrength", "float"),
    ("PuddleRippleCellCm", "float"),
    ("PuddleRippleSpeedCmPerSec", "float"),
    ("PuddleRippleWidthCm", "float"),
    ("PuddleRippleDecayPerSec", "float"),
)
PUDDLE_SAMPLERS = ("WaterSurfaceTextureSampler",)
ENGINE_SYMBOLS = ("Texture2DSampleLevel",)
CPLUSPLUS_OWNED_NAMES = (
    "RainPuddleLevelCm", "RainPuddleCavityRadiusCm", "RainPuddleCavityReferenceCm", "RainIntensity",
    "RainDropsPerSquareMeterPerSec", "RainTimeSeconds", "WaterSurfaceTexture", "WaterWorldPosition", "SimCenterWorld",
    "SimExtentsCm", "SimGridResolution", "TerrainReferenceZCm", "WaterDepthReferenceCm",
)
DROP_LOOK_SCALARS = ("RainEmissive", "RainStreakWidthRatio", "RainSplashRingWidth")
DROP_LOOK_VECTORS = ("RainTint",)
PUDDLE_LOOK_SCALARS = (
    "PuddleRoughness", "PuddleOpacity", "PuddleEdgeSoftnessCm", "PuddleHeightToleranceCm", "PuddleRippleNormalStrength",
    "PuddleRippleCellCm", "PuddleRippleSpeedCmPerSec", "PuddleRippleWidthCm", "PuddleRippleDecayPerSec",
)
PUDDLE_LOOK_VECTORS = ("PuddleColor",)
DROP_ANCHORS = (
    "tdRainAcross / max(RainStreakWidthRatio, 0.01)",
    "abs(tdRainRadius - tdRainRingRadius)",
    "max(RainSplashRingWidth, 0.01)",
    "lerp(tdRainStreak, tdRainRing, saturate(ParticleColor.r))",
    "return saturate(ParticleColor.a *",
)
PUDDLE_ANCHORS = (
    "float rainTerrainKnown = 1.0 - step(7.99, rainSurface.a);",
    "float rainCenterZ = TerrainReferenceZCm + rainSurface.a * 1000.0;",
    "float rainCavityCm = rainRingSum * 0.125 - rainCenterZ;",
    "clamp(rainCavityCm - (RainPuddleCavityReferenceCm - rainLevelCm), 0.0, rainLevelCm)",
    "float rainHeightGate = step(WaterWorldPosition.z - rainCenterZ, PuddleHeightToleranceCm);",
    "int2 rainBaseCell = int2(floor(WaterWorldPosition.xy / rainCellSize));",
    "RainDropsPerSquareMeterPerSec * RainIntensity * rainCellSize * rainCellSize * 0.0001",
    "float rainPeriod = 1.0 / max(rainCellDropsPerSec, 0.01);",
    "frac(RainTimeSeconds / rainPeriod + rainPhase) * rainPeriod",
    "exp(-PuddleRippleDecayPerSec * rainAge)",
    "float rainInBand = 1.0 - step(rainRingWidth, abs(rainBand));",
    "step(0.0001, RainIntensity * RainDropsPerSquareMeterPerSec)",
    "return float4(rainMask, rainNormal.x, rainNormal.y, 0.0);",
)
HASH_CONSTANTS = ("747796405u", "2891336453u", "277803737u")
HASH_MIRROR_MARKERS = ("TDFluidRainHash", "FTDFluidRainImpacts", "HashUint32")
HASH_MIRROR_FILES = (
    ("Source", "TDGame", "FluidFX", "TDFluidRainImpacts.cpp"),
    ("Source", "TDFluidGpu", "Shaders", "Private", "TDFluidGpuNearRipple.usf"),
)


def find_dxc(explicit_path):
    candidates = [explicit_path, os.environ.get("DXC_PATH")]
    program_files = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    candidates.extend(reversed(sorted(glob.glob(os.path.join(program_files, "Windows Kits", "10", "bin", "*", "x64", "dxc.exe")))))
    return next((candidate for candidate in candidates if candidate and os.path.isfile(candidate)), "")


def normalize(text):
    return re.sub(r"\s+", " ", text)


def run_shader_text_checks(name, text):
    failures = []
    if "//" in text or "/*" in text:
        failures.append(f"{name}: 주석이 있음")
    if ABSOLUTE_PATH_PATTERN.search(text):
        failures.append(f"{name}: 절대 경로가 있음")
    if not text.isascii():
        failures.append(f"{name}: ASCII 가 아닌 문자가 있음")
    if "\r" in text:
        failures.append(f"{name}: CRLF 줄바꿈")
    if not text.endswith("\n"):
        failures.append(f"{name}: 끝 줄바꿈 없음")
    return failures


def find_unbalanced_brackets(code):
    pairs = {")": "(", "]": "[", "}": "{"}
    stack = []
    for index, character in enumerate(code):
        if character in "([{":
            stack.append(character)
            continue
        if character in pairs and (not stack or stack.pop() != pairs[character]):
            return f"괄호 불일치 위치 {index}"
    return f"닫히지 않은 괄호 {len(stack)}개" if stack else ""


def validate_input_contract(name, code, inputs, samplers):
    declared = {input_name for input_name, _ in inputs} | set(samplers) | set(ENGINE_SYMBOLS)
    identifiers = set(IDENTIFIER_PATTERN.findall(code))
    problems = []
    unused = [input_name for input_name, _ in inputs if input_name not in identifiers]
    if unused:
        problems.append(f"{name}: HLSL 이 쓰지 않는 Custom 입력 {', '.join(unused)}")
    unused_samplers = [sampler for sampler in samplers if sampler not in identifiers]
    if unused_samplers:
        problems.append(f"{name}: HLSL 이 쓰지 않는 샘플러 {', '.join(unused_samplers)}")
    unknown = sorted(item for item in identifiers if item[0].isupper() and item not in declared)
    if unknown:
        problems.append(f"{name}: 선언되지 않은 대문자 식별자(입력 이름 오타 의심) {', '.join(unknown)}")
    bracket_problem = find_unbalanced_brackets(code)
    if bracket_problem:
        problems.append(f"{name}: {bracket_problem}")
    if "return" not in identifiers:
        problems.append(f"{name}: return 이 없음")
    return problems


def declare_inputs(inputs):
    lines = []
    members = []
    for input_name, kind in inputs:
        if kind == "texture":
            lines.append(f"Texture2D {input_name};")
            lines.append(f"SamplerState {input_name}Sampler;")
            continue
        members.append(f"{kind} {input_name};")
    return "\n".join(lines) + "\ncbuffer Inputs {\n" + "\n".join(members) + "\n}\n"


def build_drop_source(code):
    declarations = declare_inputs(DROP_INPUTS)
    return HEADER + declarations + f"float TDRainDrop()\n{{\n{code}\n}}\n" + "float4 main(float4 sv : SV_Position) : SV_Target\n{\nreturn TDRainDrop().xxxx;\n}\n"


def build_puddle_source(code):
    declarations = declare_inputs(PUDDLE_INPUTS)
    return HEADER + declarations + f"float4 TDRainPuddleDecal()\n{{\n{code}\n}}\n" + "float4 main(float4 sv : SV_Position) : SV_Target\n{\nreturn TDRainPuddleDecal();\n}\n"


def compile_source(dxc, source, label, optimize, work_folder):
    path = work_folder / f"{label}.hlsl"
    disassembly = work_folder / f"{label}.dxil.txt"
    path.write_text(source, encoding="utf-8")
    command = [dxc, "-T", "ps_6_0", "-E", "main", str(path), "-Fc", str(disassembly), "-Fo", str(work_folder / f"{label}.bin"), "-O3" if optimize else "-Od"]
    completed = subprocess.run(command, capture_output=True, text=True)
    text = disassembly.read_text(encoding="utf-8", errors="replace") if disassembly.exists() else ""
    first_error = next((line for line in (completed.stderr + completed.stdout).splitlines() if "error" in line), "")
    return {"exit": completed.returncode, "samples": len(SAMPLE_PATTERN.findall(text)), "instructions": len(INSTRUCTION_PATTERN.findall(text)),
            "warnings": completed.stderr.count("warning:"), "error": first_error}


def find_missing_anchors(code, anchors):
    flat = normalize(code)
    return [anchor for anchor in anchors if normalize(anchor) not in flat]


def read_fragment_materials(fragment_path):
    fragment = json.loads(pathlib.Path(fragment_path).read_text(encoding="utf-8"))
    return fragment.get("Materials", {})


def find_instance_problems(materials, material_path, scalar_names, vector_names):
    entry = materials.get(material_path)
    if not isinstance(entry, dict):
        return [f"{material_path}: 조각에 항목 없음"]
    scalars = entry.get("Scalars", {})
    vectors = entry.get("Vectors", {})
    problems = [f"{material_path}: 모양 스칼라 없음 {name}" for name in scalar_names if name not in scalars]
    problems += [f"{material_path}: 모양 벡터 없음 {name}" for name in vector_names if name not in vectors]
    problems += [f"{material_path}: 모양 스칼라 남는 이름 {name}" for name in scalars if name not in scalar_names]
    problems += [f"{material_path}: 모양 벡터 남는 이름 {name}" for name in vectors if name not in vector_names]
    problems += [f"{material_path}: C++ 소유 이름이 모양에 있음(R7) {name}" for name in list(scalars) + list(vectors) if name in CPLUSPLUS_OWNED_NAMES]
    return problems


def find_hash_mirror_problems(root):
    problems = []
    seen = []
    for parts in HASH_MIRROR_FILES:
        path = root.joinpath(*parts)
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        if not any(marker in text for marker in HASH_MIRROR_MARKERS):
            continue
        seen.append(path.name)
        problems += [f"{path.name}: 해시 상수 {constant} 없음" for constant in HASH_CONSTANTS if constant.rstrip("u") not in text]
    return problems, seen


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

    shader_folder = root / "Tools" / "FluidFX" / "Shaders"
    texts = {}
    for name in (DROP_HLSL_NAME, PUDDLE_HLSL_NAME):
        path = shader_folder / name
        if not path.is_file():
            record(f"셰이더 파일 {name}", False, "없음")
            return finish(results, summary, arguments.output)
        texts[name] = path.read_text(encoding="utf-8")
    text_failures = [item for name, text in texts.items() for item in run_shader_text_checks(name, text)]
    record("셰이더 텍스트(주석·절대 경로·ASCII·LF)", not text_failures, "; ".join(text_failures))
    contract_problems = validate_input_contract(DROP_HLSL_NAME, texts[DROP_HLSL_NAME], DROP_INPUTS, ())
    contract_problems += validate_input_contract(PUDDLE_HLSL_NAME, texts[PUDDLE_HLSL_NAME], PUDDLE_INPUTS, PUDDLE_SAMPLERS)
    record("입력 이름 계약(선언 입력 전부 사용·대문자 식별자 전부 선언·괄호·return)", not contract_problems, "; ".join(contract_problems))
    missing = [f"{DROP_HLSL_NAME}: {item}" for item in find_missing_anchors(texts[DROP_HLSL_NAME], DROP_ANCHORS)]
    missing += [f"{PUDDLE_HLSL_NAME}: {item}" for item in find_missing_anchors(texts[PUDDLE_HLSL_NAME], PUDDLE_ANCHORS)]
    record("식 고정점(B6 floor 나눗셈·B8 주기와 감쇠 이름·오목부 채움·높이 관문)", not missing, "; ".join(missing))
    missing_constants = [constant for constant in HASH_CONSTANTS if constant not in texts[PUDDLE_HLSL_NAME]]
    record("해시 상수 3.2 (747796405u·2891336453u·277803737u)", not missing_constants, ", ".join(missing_constants))
    mirror_problems, mirror_seen = find_hash_mirror_problems(root)
    record("해시 상수가 C++·근접 층 셰이더와 같음", not mirror_problems, "; ".join(mirror_problems) if mirror_problems else f"대조한 파일 {mirror_seen or '없음(비 해시 구현이 아직 없음, C4·C5 뒤 다시 실행)'}")

    if arguments.look_fragment:
        materials = read_fragment_materials(arguments.look_fragment)
        instance_problems = find_instance_problems(materials, DROP_MATERIAL_PATH, DROP_LOOK_SCALARS, DROP_LOOK_VECTORS)
        instance_problems += find_instance_problems(materials, PUDDLE_MATERIAL_PATH, PUDDLE_LOOK_SCALARS, PUDDLE_LOOK_VECTORS)
        record("조각 MI 이름 계약(모양 이름만·C++ 소유 이름 없음)", not instance_problems, "; ".join(instance_problems))

    dxc = find_dxc(arguments.dxc)
    if not dxc:
        record("dxc 찾기", False, "Windows Kits dxc.exe 없음(--dxc 로 지정)")
        return finish(results, summary, arguments.output)
    record("dxc 찾기", True, dxc)
    jobs = []
    for optimize in (False, True):
        suffix = "O3" if optimize else "Od"
        jobs.append((f"rain_drop_{suffix}", build_drop_source(texts[DROP_HLSL_NAME]), optimize, 0))
        jobs.append((f"rain_puddle_{suffix}", build_puddle_source(texts[PUDDLE_HLSL_NAME]), optimize, 9))
    compiles = {}
    with tempfile.TemporaryDirectory() as folder:
        work_folder = pathlib.Path(folder)
        for label, source, optimize, expected_samples in jobs:
            outcome = compile_source(dxc, source, label, optimize, work_folder)
            passed = outcome["exit"] == 0 and outcome["samples"] == expected_samples
            record(f"dxc {label} ps_6_0", passed, outcome["error"] or f"표본 {outcome['samples']}(기대 {expected_samples}) 명령 {outcome['instructions']} 경고 {outcome['warnings']}")
            compiles[label] = outcome
    summary["dxc"] = {label: {"samples": item["samples"], "instructions": item["instructions"]} for label, item in compiles.items()}
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
