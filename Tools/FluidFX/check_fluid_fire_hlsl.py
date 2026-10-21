"""실행 환경: 시스템 Python 3 와 Windows Kits dxc.exe(ps_6_0), 에디터 불필요
실행 명령: python Tools/FluidFX/check_fluid_fire_hlsl.py [--root 프로젝트경로] [--dxc dxc경로]
출력: 항목별 PASS/FAIL 줄과 마지막 줄 JSON 요약(셰이더별 정적 표본 명령 수 포함), 하나라도 실패하면 종료 코드 1
상태: 검증됨(2026-10-19 19/19 통과, 음성 대조 2건 검출. 2026-10-20 FX-8b 21/21, 투과율 상수 3곳 대조 추가. 2026-10-20 FX-8c 3차 22/22: View.StateFrameIndexMod8 스텁·볼륨 디더 회귀 검사·볼륨 표본 36. 2026-10-20 FX-8c 4차 G1 24/24: 불꽃·볼륨·가짜 볼륨의 위치 투영(화소·카메라·광원)은 PlaneCoordinateOffsetCm 을 더하고 방향 투영(광원 방향·월드 위)은 더하지 않는지 보는 규칙과 음성 대조 3건. 2026-10-20 FX-8c 5차 26/26: 불꽃 색 램프 끔 경로 정적 검사(흑체 색도·밝기·덮임·방출 줄 보호, 보호 변수 대입은 FlameColorRampMode 분기 안 chromaColor 만, 마스터 기본 0)와 음성 대조 10건. 2026-10-20 FX-8c 7차 30/30: 볼륨 노이즈 부드러운 절댓값 계약(NoiseSoftAbsEpsilon·NoiseSoftAbsMeanShift 기본 0, 인스턴스는 둘을 함께, TD_SHIFT_OF 두 항 모두 TD_NOISE_ABS), dxc -O3 자체 점검(엡실론 호출 인수 0 으로 접은 본문 = TD_NOISE_ABS 를 abs 로 바꾼 기준 본문, 0.04 는 다름)과 음성 대조 6건, --root 변형 사본 4개 중 4개 검출). 2026-10-20 FX-8c 10차 조각 발광 셰이더 TDFluidFragmentGlow.hlsl 추가(입력 원천·평면 오프셋·정적 표본 1·C++ 값 이름 계약: 방출 액터가 넣는 Fragment* 스칼라 4개와 색 벡터 FragmentColdGlowColor·FragmentHotGlowColor 는 마스터 기본값에 있고 FragmentHeatRatio 기본 0, 라이브 컴포넌트가 등록 MID 에 넣는 평면·Aux 이름이 셰이더 입력을 덮음, 음성 대조 5건, 조각 마스터 생성 함수가 지오메트리 컬렉션 사용 플래그 함수를 부르는지 보는 문자열 규칙과 음성 대조 1건), 정적 검사만 통과(사본 37/37)하고 에디터 미실행. 불·연기 셰이더 6개(TDFluidFlameCard·Ember·CharredWood·FragmentGlow·VolumeRaymarch·FakeVolume.hlsl)를 재질 스크립트의 입력 목록으로 언리얼 Custom 노드 함수 형태로 감싸 컴파일·정적 검사한다. 에디터 화면 확인은 별도(FX-8a 룩데브). 2026-10-21 FX-8c 11차: 조각 스칼라 7·벡터 3 계약, C++ 조각 기본 = 마스터 기본 계약, 항등 입력 기본 0·0 접기 dxc 본문 = 기준과 음성 대조. 2026-10-21 FX-8c 11차 단계 B: 항등 입력에 불꽃 FlameColdRidgeContrast(기준 = heatGate 의 coldRidgeTexture 곱 제거) 추가, 항등 기본 음성 대조는 변형이 원래 없던 문제를 새로 만들어야 잡음으로 셈. 2026-10-21 FX-8c 11차 굽기: 항등 입력의 MI 덮어쓰기 금지 조항을 뺌(굽기 B2 가 MI_TD_FluidEmber_Default 에 EmberTintBlend 1.0 을 넣음), 마스터 기본 0 계약과 그 음성 대조는 유지.
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
struct FMaterialPixelParameters { float4 SvPosition; };
struct FViewUniformShaderParameters { uint StateFrameIndexMod8; };
cbuffer ViewUniformBuffer { FViewUniformShaderParameters View; };
float4 Texture2DSampleLevel(Texture2D Tex, SamplerState Sampler, float2 UV, float Mip) { return Tex.SampleLevel(Sampler, UV, Mip); }
float4 Texture3DSampleLevel(Texture3D Tex, SamplerState Sampler, float3 UV, float Mip) { return Tex.SampleLevel(Sampler, UV, Mip); }
"""
SAMPLE_PATTERN = re.compile(r"call \S+ @dx\.op\.(?:sample|sampleLevel|sampleBias|sampleGrad)\.")
ABSOLUTE_PATH_PATTERN = re.compile(r"[A-Za-z]:[\\/]")
DEFINE_PATTERN = re.compile(r"^#define\s+([A-Za-z_][A-Za-z0-9_]*)", re.MULTILINE)
UNDEF_PATTERN = re.compile(r"^#undef\s+([A-Za-z_][A-Za-z0-9_]*)", re.MULTILINE)
FLAME_CARD_SAMPLE_BUDGET = 12
FRAGMENT_SAMPLE_BUDGET = 1
VOLUME_SAMPLE_BUDGET = 36
VOLUME_TEXTURE_NAMES = {"NoiseVolumeTexture"}
TEXTURE_2D_NAMES = {"DensityPressureTexture", "VelocityTexture", "AuxTexture", "TransmittanceTexture"}
TRANSMITTANCE_CONSTANT_SOURCES = (
    ("Tools/FluidFX/Shaders/TDFluidVolumeRaymarch.hlsl", (r"edgeFadeFraction = (\d+\.\d+)", r"TD_EDGE\(P\) \* (\d+\.\d+)\)", r"baseHeightCm = VolumeHeightCm \* (\d+\.\d+)", r"\((\d+\.\d+) \* VolumeHeightCm\)\)", r"levelHeightScale = (\d+)\.0 /")),
    ("Source/TDFluidGpu/Shaders/Private/TDFluidGpuTransmittance.usf", (r"EDGE_FADE_FRACTION = (\d+\.\d+)f", r"COVERAGE_SCALE = (\d+\.\d+)f", r"HEIGHT_SCALE_RATIO = (\d+\.\d+)f", r"TOP_FADE_RATIO = (\d+\.\d+)f", r"LEVEL_COUNT = (\d+);")),
    ("Source/TDGame/FluidFX/TDFluidVolumeTransmittance.h", (r"EdgeFadeFraction = (\d+\.\d+)f", r"CoverageScale = (\d+\.\d+)f", r"HeightScaleRatio = (\d+\.\d+)f", r"TopFadeRatio = (\d+\.\d+)f", r"LevelCount = (\d+);")),
)
NO_ALU_NOISE_SHADERS = ("TDFluidFlameCard.hlsl", "TDFluidVolumeRaymarch.hlsl", "TDFluidFakeVolume.hlsl", "TDFluidCharredWood.hlsl", "TDFluidFragmentGlow.hlsl")
RAMP_GUARD_LINE = "if (FlameColorRampMode > 0.5)"
RAMP_PROTECTED_LINES = (
    "float3 wavelengthMicrometers = float3(0.610, 0.550, 0.465);",
    "float3 radiationExponent = 14388.0 / (wavelengthMicrometers * max(temperatureKelvin, 300.0));",
    "float3 radiationLevel = 1.0 / (pow(wavelengthMicrometers, 5.0) * (exp(radiationExponent) - 1.0));",
    "float3 chromaColor = radiationLevel / max(dot(radiationLevel, float3(0.2126, 0.7152, 0.0722)), 1e-30);",
    "float temperatureBrightness = pow(saturate(temperatureKelvin / max(FlameTemperatureMaxKelvin, 1.0)), max(FlameBrightnessExponent, 0.0));",
    "float emissionCoverage = usesHeat ? pow(max(erodedDensity - 1e-4, 0.0), heatDensityExponent) : erodedDensity;",
    "float3 flameEmission = chromaColor * (temperatureBrightness * FlameIntensity * emissionCoverage * heatGate);",
)
RAMP_PROTECTED_ASSIGNMENT_PATTERN = re.compile(r"^\s*(chromaColor|temperatureBrightness|emissionCoverage|flameEmission|FlameColorRampMode)(?:\.\w+)?\s*[-+*/]?=(?!=)")
RAMP_MUTATIONS = (
    ("분기 밖 chromaColor 대입", "float temperatureBrightness", "chromaColor = chromaColor * 1.0;\nfloat temperatureBrightness"),
    ("분기 밖 chromaColor 복합 대입", "float temperatureBrightness", "chromaColor *= 1.0001;\nfloat temperatureBrightness"),
    ("분기 밖 chromaColor 성분 대입", "float temperatureBrightness", "chromaColor.g = chromaColor.g * 0.5;\nfloat temperatureBrightness"),
    ("방출 줄 변경", "float3 flameEmission = chromaColor * (", "float3 flameEmission = chromaColor * 0.5 * ("),
    ("밝기 줄 변경", "pow(saturate(temperatureKelvin /", "pow(saturate(0.999 * temperatureKelvin /"),
    ("덮임 재대입", "float heatGateRamp", "emissionCoverage = emissionCoverage * 0.9;\nfloat heatGateRamp"),
    ("흑체 파장 변경", "float3(0.610, 0.550, 0.465)", "float3(0.600, 0.550, 0.465)"),
    ("분기 조건 변경", "if (FlameColorRampMode > 0.5)", "if (FlameColorRampMode > -0.5)"),
    ("모드 강제 켬", "if (FlameColorRampMode > 0.5)", "FlameColorRampMode = 1.0;\nif (FlameColorRampMode > 0.5)"),
    ("분기 안 chromaColor 대입 제거", "    chromaColor = rampChroma /", "    float3 unusedChroma = rampChroma /"),
)
SOLVER_PLANE_SHADER_LABELS = ("flame", "volume", "fake", "fragment")
POSITION_INPUT_NAMES = ("PixelWorldPosition", "CameraWorldPosition", "VolumeLightPosition")
DIRECTION_INPUT_PATTERNS = ("VolumeLightDirection", "float3(0.0, 0.0, 1.0)")
PLANE_PROJECTION_MARKERS = ("PlaneAxisU", "TD_TO_PLANE(", "TD_TO_SOLVER_PLANE(")
SOLVER_PLANE_MARKERS = ("PlaneCoordinateOffsetCm", "TD_TO_SOLVER_PLANE(")
SOLVER_PLANE_MUTATIONS = (
    ("volume", "TD_TO_SOLVER_PLANE(VolumeLightPosition)", "TD_TO_PLANE(VolumeLightPosition)"),
    ("volume", "TD_TO_PLANE(VolumeLightDirection)", "TD_TO_SOLVER_PLANE(VolumeLightDirection)"),
    ("flame", ") + PlaneCoordinateOffsetCm.xy;", ");"),
    ("fragment", ") + PlaneCoordinateOffsetCm.xy;", ");"),
)
SOFT_ABS_DEFAULTS = (("NoiseSoftAbsEpsilon", 0.0), ("NoiseSoftAbsMeanShift", 0.0))
SOFT_ABS_DEFINE_PREFIX = "#define TD_NOISE_ABS(X) "
SOFT_ABS_SHIFT_PREFIX = "#define TD_SHIFT_OF(S) "
SOFT_ABS_REFERENCE_DEFINE = "#define TD_NOISE_ABS(X) abs(X)"
SOFT_ABS_CALL_PATTERN = re.compile(r"^(return CustomExpression0\(.*)\bInput_NoiseSoftAbsEpsilon\b", re.MULTILINE)
FRAGMENT_EMITTER_SOURCE = ("Source", "TDGame", "FluidFX", "TDFluidFragmentEmitterActor.cpp")
LIVE_COMPONENT_SOURCE = ("Source", "TDGame", "FluidFX", "TDFluidLiveComponent.cpp")
FRAGMENT_EMITTER_PUSH_PATTERN = re.compile(r'FragmentGlowMaterial->SetScalarParameterValue\(TEXT\("(\w+)"\)')
LIVE_MID_PUSH_PATTERN = re.compile(r'TargetMID->Set(?:Scalar|Vector|Texture)ParameterValue\(TEXT\("(\w+)"\)')
FRAGMENT_REGISTRATION_LINE = "RegisterExternalMaterialTarget(FragmentGlowMaterial)"
FRAGMENT_EMITTER_SCALARS = ("FragmentHeatRatio", "FragmentGlowIntensity", "FragmentFireHeatWeight", "FragmentFireHeatSlabCm", "FragmentFacingContrast", "FragmentEdgeCharWidth", "FragmentEdgeCharStrength")
FRAGMENT_EMITTER_VECTOR_PUSH_PATTERN = re.compile(r'FragmentGlowMaterial->SetVectorParameterValue\(TEXT\("(\w+)"\)')
FRAGMENT_EMITTER_VECTORS = ("FragmentColdGlowColor", "FragmentHotGlowColor", "FragmentKeyCenterWorld")
FRAGMENT_MATERIAL_SCRIPT = ("Tools", "FluidFX", "editor_make_fluid_flame_material.py")
FRAGMENT_MASTER_FUNCTION = "create_fragment_master"
FRAGMENT_USAGE_FUNCTION = "enable_geometry_collection_usage"
FRAGMENT_USAGE_CALL = "usage = enable_geometry_collection_usage(material)"
FRAGMENT_USAGE_FLAG_CALL = "set_material_usage(material, unreal.MaterialUsage.MATUSAGE_GEOMETRY_COLLECTIONS)"
FRAGMENT_REGISTRATION_INPUTS = ("SimCenterWorld", "SimExtentsCm", "PlaneAxisU", "PlaneAxisV", "PlaneAxisN", "PlaneCoordinateOffsetCm", "ExtrudeSymmetric", "AuxTexture", "AuxHeatActive")
FIRE_TYPES_SOURCE = ("Source", "TDGame", "FluidFX", "TDFluidFireTypes.h")
FRAGMENT_SETTINGS_STRUCT_LINE = "struct TDGAME_API FTDFluidFragmentGlowSettings"
CPP_FLOAT_DEFAULT_PATTERN = re.compile(r"float (\w+) = (-?[\d.]+)f;")
FRAGMENT_CPP_DEFAULT_FIELDS = ("GlowIntensity", "FireHeatWeight", "FireHeatSlabCm", "FacingContrast", "EdgeCharWidth", "EdgeCharStrength")
FRAGMENT_CPP_DEFAULT_MUTATION = ("0.12f", "0.2f")
IDENTITY_DEFAULT_INPUTS = (("ember", "EmberTintBlend"), ("fragment", "FragmentFacingContrast"), ("flame", "FlameColdRidgeContrast"))
IDENTITY_REFERENCE_EDITS = {
    "ember": ("normalizedColor = lerp(normalizedColor, emberTintChroma, saturate(EmberTintBlend));\n", ""),
    "fragment": ("rimFactor * faceFactor)", "rimFactor)"),
    "flame": (") * coldRidgeTexture;", ");"),
}
SOFT_ABS_DXC_MUTATIONS = (
    ("켬 조건 >=", "NoiseSoftAbsEpsilon > 0.0", "NoiseSoftAbsEpsilon >= 0.0"),
    ("끔 경로 abs → sqrt(X²)", ": abs(X))", ": sqrt((X) * (X)))"),
)


def load_script(root, name):
    spec = importlib.util.spec_from_file_location(name, root / "Tools" / "FluidFX" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.project_path = lambda *parts: os.path.join(str(root), *parts)
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


def describe_nodes(flame, volume):
    flame_vectors = {name for name, _ in flame.FLAME_VECTOR_PARAMETER_DEFAULTS} | {"PixelWorldPosition"}
    wood_vectors = {name for name, _ in flame.WOOD_VECTOR_PARAMETER_DEFAULTS} | {"PixelWorldPosition"}
    builtin_vectors = {"PixelWorldPosition", "CameraWorldPosition"}
    fragment_vectors = {name for name, _ in flame.FRAGMENT_VECTOR_PARAMETER_DEFAULTS} | set(flame.FRAGMENT_BUILTIN_SOURCE_NAMES)
    return (
        {
            "label": "flame",
            "hlsl": "TDFluidFlameCard.hlsl",
            "inputs": flame.FLAME_CUSTOM_INPUT_NAMES,
            "vectors": flame_vectors,
            "output": "float4",
            "sources": set(flame.FLAME_TEXTURE_PARAMETER_NAMES) | {flame.NOISE_TEXTURE_PARAMETER_NAME} | flame_vectors
            | {name for name, _ in flame.FLAME_SCALAR_PARAMETER_DEFAULTS} | set(flame.FLAME_BUILTIN_SOURCE_NAMES),
        },
        {
            "label": "ember",
            "hlsl": "TDFluidEmber.hlsl",
            "inputs": flame.EMBER_CUSTOM_INPUT_NAMES,
            "vectors": set(flame.EMBER_ENGINE_VECTOR_NAMES),
            "output": "float3",
            "sources": {name for name, _ in flame.EMBER_INSTANCE_DATA_NAMES} | {name for name, _ in flame.EMBER_SCALAR_PARAMETER_DEFAULTS}
            | set(flame.EMBER_ENGINE_VECTOR_NAMES),
        },
        {
            "label": "wood",
            "hlsl": "TDFluidCharredWood.hlsl",
            "inputs": flame.WOOD_CUSTOM_INPUT_NAMES,
            "vectors": wood_vectors,
            "output": "float4",
            "sources": wood_vectors | {name for name, _ in flame.WOOD_SCALAR_PARAMETER_DEFAULTS} | {"ElapsedSeconds", flame.NOISE_TEXTURE_PARAMETER_NAME},
        },
        {
            "label": "fragment",
            "hlsl": "TDFluidFragmentGlow.hlsl",
            "inputs": flame.FRAGMENT_CUSTOM_INPUT_NAMES,
            "vectors": fragment_vectors,
            "output": "float3",
            "sources": fragment_vectors | {name for name, _ in flame.FRAGMENT_SCALAR_PARAMETER_DEFAULTS} | set(flame.FRAGMENT_TEXTURE_PARAMETER_NAMES),
        },
        {
            "label": "volume",
            "hlsl": volume.VOLUME_MASTER["hlsl"],
            "inputs": volume.VOLUME_MASTER["inputs"],
            "vectors": {name for name, _ in volume.VOLUME_MASTER["vectors"]} | builtin_vectors,
            "output": "float4",
            "sources": None,
        },
        {
            "label": "fake",
            "hlsl": volume.FAKE_MASTER["hlsl"],
            "inputs": volume.FAKE_MASTER["inputs"],
            "vectors": {name for name, _ in volume.FAKE_MASTER["vectors"]} | builtin_vectors,
            "output": "float4",
            "sources": None,
        },
    )


def declare_argument(name, vectors, volume_names):
    if name in volume_names:
        return f"Texture3D {name}, SamplerState {name}Sampler"
    if name in TEXTURE_2D_NAMES or name in VOLUME_TEXTURE_NAMES:
        return f"Texture2D {name}, SamplerState {name}Sampler"
    return f"float3 {name}" if name in vectors else f"float {name}"


def declare_global(name, vectors, volume_names):
    if name in volume_names:
        return f"Texture3D Input_{name};\nSamplerState Input_{name}Sampler;\n", ""
    if name in TEXTURE_2D_NAMES or name in VOLUME_TEXTURE_NAMES:
        return f"Texture2D Input_{name};\nSamplerState Input_{name}Sampler;\n", ""
    return "", (f"float3 Input_{name};\n" if name in vectors else f"float Input_{name};\n")


def build_source(node, code, volume_names):
    resources, constants, arguments, call_arguments = "", "", [], []
    for name in node["inputs"]:
        resource, constant = declare_global(name, node["vectors"], volume_names)
        resources += resource
        constants += constant
        arguments.append(declare_argument(name, node["vectors"], volume_names))
        is_texture = name in TEXTURE_2D_NAMES or name in VOLUME_TEXTURE_NAMES
        call_arguments.append(f"Input_{name}, Input_{name}Sampler" if is_texture else f"Input_{name}")
    function = f"{node['output']} CustomExpression0(FMaterialPixelParameters Parameters, " + ", ".join(arguments) + f")\n{{\n{code}\n}}\n"
    result = "CustomExpression0(Parameters, " + ", ".join(call_arguments) + ")"
    returned = result if node["output"] == "float4" else f"float4({result}, 1.0)"
    entry = (
        "float4 main(float4 sv : SV_Position) : SV_Target\n{\nFMaterialPixelParameters Parameters;\nParameters.SvPosition = sv;\n"
        f"return {returned};\n}}\n"
    )
    return HEADER + resources + "cbuffer Inputs {\n" + constants + "}\n" + function + entry


def compile_source(dxc, source, label, optimize, work_folder):
    path = work_folder / f"{label}.hlsl"
    disassembly = work_folder / f"{label}.dxil.txt"
    path.write_text(source, encoding="utf-8")
    command = [dxc, "-T", "ps_6_0", "-E", "main", str(path), "-Fc", str(disassembly), "-Fo", str(work_folder / f"{label}.bin"), "-O3" if optimize else "-Od"]
    completed = subprocess.run(command, capture_output=True, text=True)
    text = disassembly.read_text(encoding="utf-8", errors="replace") if disassembly.exists() else ""
    first_error = next((line for line in (completed.stderr + completed.stdout).splitlines() if "error" in line), "")
    body_start = text.find("define void @main()")
    body_end = text.find("\n}\n", body_start)
    return {
        "exit": completed.returncode,
        "samples": len(SAMPLE_PATTERN.findall(text)),
        "warnings": completed.stderr.count("warning:"),
        "error": first_error,
        "body": text[body_start:body_end] if 0 <= body_start < body_end else "",
    }


def find_text_problems(name, text):
    problems = []
    if "//" in text or "/*" in text:
        problems.append(f"{name}: 주석이 있음")
    if ABSOLUTE_PATH_PATTERN.search(text):
        problems.append(f"{name}: 절대 경로가 있음")
    if not text.isascii():
        problems.append(f"{name}: ASCII 가 아닌 문자가 있음")
    if "\r" in text:
        problems.append(f"{name}: CRLF 줄바꿈")
    defined = set(DEFINE_PATTERN.findall(text))
    undefined = set(UNDEF_PATTERN.findall(text))
    if defined != undefined:
        problems.append(f"{name}: #undef 짝 불일치(누락 {sorted(defined - undefined)}, 여분 {sorted(undefined - defined)})")
    return problems


def find_solver_plane_offset_problems(label, code):
    problems = []
    for number, line in enumerate(code.splitlines(), start=1):
        if line.startswith("#define TD_TO_PLANE("):
            continue
        if line.startswith("#define TD_TO_SOLVER_PLANE("):
            if "PlaneCoordinateOffsetCm" not in line:
                problems.append(f"{label}:{number} TD_TO_SOLVER_PLANE 정의에 PlaneCoordinateOffsetCm 없음")
            continue
        if not any(marker in line for marker in PLANE_PROJECTION_MARKERS):
            continue
        has_offset = any(marker in line for marker in SOLVER_PLANE_MARKERS)
        if any(name in line for name in POSITION_INPUT_NAMES) and not has_offset:
            problems.append(f"{label}:{number} 위치 투영에 솔버 평면 오프셋 없음")
        if any(pattern in line for pattern in DIRECTION_INPUT_PATTERNS) and has_offset:
            problems.append(f"{label}:{number} 방향 투영에 솔버 평면 오프셋이 더해짐")
    if "PlaneCoordinateOffsetCm" not in code:
        problems.append(f"{label}: PlaneCoordinateOffsetCm 을 쓰지 않음")
    return problems


def find_undetected_solver_plane_mutations(codes):
    undetected = []
    for label, original, mutated in SOLVER_PLANE_MUTATIONS:
        code = codes[label]
        if original not in code or not find_solver_plane_offset_problems(label, code.replace(original, mutated, 1)):
            undetected.append(f"{label}: {original} → {mutated}")
    return undetected


def find_flame_contract_problems(flame, nodes):
    problems = []
    for node in nodes:
        if node["sources"] is None:
            continue
        if len(set(node["inputs"])) != len(node["inputs"]):
            problems.append(f"{node['label']} 입력 이름 중복")
        unresolved = sorted(set(node["inputs"]) - node["sources"])
        if unresolved:
            problems.append(f"{node['label']} 입력의 원천 없음: " + ", ".join(unresolved))
    flame_scalars = {name for name, _ in flame.FLAME_SCALAR_PARAMETER_DEFAULTS}
    for spec in flame.FLAME_INSTANCE_SPECS:
        unknown = sorted(set(spec["scalar_overrides"]) - flame_scalars)
        if unknown:
            problems.append(f"{spec['name']} 덮어쓰기 이름이 마스터에 없음: " + ", ".join(unknown))
    fragment_scalars = {name for name, _ in flame.FRAGMENT_SCALAR_PARAMETER_DEFAULTS}
    fragment_vector_names = {name for name, _ in flame.FRAGMENT_VECTOR_PARAMETER_DEFAULTS}
    for spec in flame.FRAGMENT_INSTANCE_SPECS:
        unknown = sorted((set(spec["scalar_overrides"]) - fragment_scalars) | (set(spec.get("vector_overrides", {})) - fragment_vector_names))
        if unknown:
            problems.append(f"{spec['name']} 덮어쓰기 이름이 마스터에 없음: " + ", ".join(unknown))
    missing_surface = sorted(set(flame.FRAGMENT_SURFACE_PARAMETER_NAMES) - fragment_scalars - fragment_vector_names)
    if missing_surface:
        problems.append("조각 표면 매개변수가 마스터에 없음: " + ", ".join(missing_surface))
    wood_scalars = {name for name, _ in flame.WOOD_SCALAR_PARAMETER_DEFAULTS}
    for spec in flame.WOOD_INSTANCE_SPECS:
        unknown = sorted(set(spec["scalar_overrides"]) - wood_scalars)
        if unknown:
            problems.append(f"{spec['name']} 덮어쓰기 이름이 마스터에 없음: " + ", ".join(unknown))
    for request_value, expected in (({}, "BLEND_ALPHA_COMPOSITE"), ({"flame_blend_mode": "additive"}, "BLEND_ADDITIVE"), ({"flame_blend_mode": "translucent"}, "BLEND_TRANSLUCENT")):
        resolved = flame.resolve_flame_blend_name(request_value)
        if resolved != expected:
            problems.append(f"혼합 요청 {request_value} → {resolved}, 기대 {expected}")
    return problems


def find_ramp_guard_problems(code):
    lines = code.splitlines()
    stripped = [line.strip() for line in lines]
    problems = [f"램프 끔 경로 보호 줄이 바뀜: {line}" for line in RAMP_PROTECTED_LINES if line not in stripped]
    if stripped.count(RAMP_GUARD_LINE) != 1:
        return problems + [f"램프 분기 줄 수 {stripped.count(RAMP_GUARD_LINE)}(기대 1)"]
    guard = stripped.index(RAMP_GUARD_LINE)
    closing = next((index for index in range(guard + 2, len(lines)) if lines[index] == "}"), None)
    if guard + 1 >= len(lines) or lines[guard + 1] != "{" or closing is None:
        return problems + ["램프 분기 블록 { } 를 찾지 못함"]
    assignments = [(index, match.group(1)) for index, match in enumerate(map(RAMP_PROTECTED_ASSIGNMENT_PATTERN.match, lines)) if match]
    inside = [index for index, name in assignments if name == "chromaColor" and guard < index < closing]
    leaked = [index + 1 for index, _ in assignments if index not in inside]
    if not inside:
        problems.append("램프 분기 안에 chromaColor 대입이 없음")
    if leaked:
        problems.append(f"램프 분기 밖 chromaColor 대입 또는 보호 변수 재대입 줄 {leaked}")
    return problems


def find_undetected_ramp_mutations(code):
    return [name for name, original, mutated in RAMP_MUTATIONS if original not in code or not find_ramp_guard_problems(code.replace(original, mutated, 1))]


def find_soft_abs_contract_problems(scalars, inputs, instances, code):
    problems = []
    shift_lines = [line for line in code.split("\n") if line.startswith(SOFT_ABS_SHIFT_PREFIX)]
    if len(shift_lines) != 1 or shift_lines[0].count("TD_NOISE_ABS(") != 2 or re.search(r"\babs\(", shift_lines[0]):
        problems.append("TD_SHIFT_OF 의 r·b 두 항이 모두 TD_NOISE_ABS 를 거치지 않음")
    for name, expected in SOFT_ABS_DEFAULTS:
        if name not in inputs:
            problems.append(f"볼륨 입력에 {name} 없음")
        if scalars.get(name) != expected:
            problems.append(f"마스터 {name} 기본값 {scalars.get(name)}(기대 {expected})")
    for instance in instances:
        overrides = instance["scalars"]
        if (overrides.get("NoiseSoftAbsEpsilon", 0.0) > 0.0) != (overrides.get("NoiseSoftAbsMeanShift", 0.0) > 0.0):
            problems.append(f"{instance['name']}: NoiseSoftAbsEpsilon 과 NoiseSoftAbsMeanShift 는 함께 양수로 둔다")
    return problems


def find_undetected_soft_abs_contract_mutations(scalars, inputs, instances, code):
    raw_abs_code = code.replace("2.0 * TD_NOISE_ABS(2.0 * (S).b - 1.0)", "2.0 * abs(2.0 * (S).b - 1.0)", 1)
    mutations = (
        ("마스터 엡실론 기본 0.04", dict(scalars, NoiseSoftAbsEpsilon=0.04), inputs, instances, code),
        ("평균 변위 입력 누락", scalars, tuple(name for name in inputs if name != "NoiseSoftAbsMeanShift"), instances, code),
        ("인스턴스가 엡실론만 덮어씀", scalars, inputs, list(instances) + [{"name": "synthetic", "scalars": {"NoiseSoftAbsEpsilon": 0.04}}], code),
        ("TD_SHIFT_OF b 항이 abs 직접 사용", scalars, inputs, instances, raw_abs_code),
    )
    return [label for label, *arguments in mutations if not find_soft_abs_contract_problems(*arguments)]


def soft_abs_reference_code(code):
    lines = code.split("\n")
    matches = [index for index, line in enumerate(lines) if line.startswith(SOFT_ABS_DEFINE_PREFIX)]
    if len(matches) != 1:
        return ""
    lines[matches[0]] = SOFT_ABS_REFERENCE_DEFINE
    return "\n".join(lines)


def compile_folded_body(dxc, node, code, epsilon, label, work_folder):
    if not code:
        return ""
    folded, count = SOFT_ABS_CALL_PATTERN.subn(rf"\g<1>{epsilon}", build_source(node, code, VOLUME_TEXTURE_NAMES))
    if count != 1:
        return ""
    outcome = compile_source(dxc, folded, label, True, work_folder)
    return outcome["body"] if outcome["exit"] == 0 else ""


def read_transmittance_constants(root):
    constants = {}
    for relative, patterns in TRANSMITTANCE_CONSTANT_SOURCES:
        path = root / relative
        if not path.is_file():
            constants[relative] = None
            continue
        text = path.read_text(encoding="utf-8")
        values = []
        for pattern in patterns:
            match = re.search(pattern, text, re.MULTILINE)
            values.append(float(match.group(1)) if match else None)
        constants[relative] = tuple(values)
    return constants


def find_fragment_push_problems(flame, emitter_text, live_text):
    problems = []
    scalars = dict(flame.FRAGMENT_SCALAR_PARAMETER_DEFAULTS)
    pushed = set(FRAGMENT_EMITTER_PUSH_PATTERN.findall(emitter_text))
    missing_pushes = sorted(set(FRAGMENT_EMITTER_SCALARS) - pushed)
    if missing_pushes:
        problems.append("방출 액터가 넣지 않는 조각 스칼라: " + ", ".join(missing_pushes))
    unknown_pushes = sorted(pushed - set(scalars))
    if unknown_pushes:
        problems.append("방출 액터가 넣는 이름이 조각 마스터에 없음: " + ", ".join(unknown_pushes))
    vector_names = {name for name, _ in flame.FRAGMENT_VECTOR_PARAMETER_DEFAULTS}
    pushed_vectors = set(FRAGMENT_EMITTER_VECTOR_PUSH_PATTERN.findall(emitter_text))
    missing_vector_pushes = sorted(set(FRAGMENT_EMITTER_VECTORS) - pushed_vectors)
    if missing_vector_pushes:
        problems.append("방출 액터가 넣지 않는 조각 색 벡터: " + ", ".join(missing_vector_pushes))
    unknown_vector_pushes = sorted(pushed_vectors - vector_names)
    if unknown_vector_pushes:
        problems.append("방출 액터가 넣는 벡터 이름이 조각 마스터 벡터 기본값에 없음: " + ", ".join(unknown_vector_pushes))
    if FRAGMENT_REGISTRATION_LINE not in emitter_text:
        problems.append("방출 액터가 조각 MID 를 불 라이브 컴포넌트에 등록하지 않음")
    registered = set(LIVE_MID_PUSH_PATTERN.findall(live_text))
    missing_registered = sorted(set(FRAGMENT_REGISTRATION_INPUTS) - registered)
    if missing_registered:
        problems.append("라이브 컴포넌트가 등록 MID 에 넣지 않는 셰이더 입력: " + ", ".join(missing_registered))
    not_inputs = sorted(set(FRAGMENT_REGISTRATION_INPUTS) - set(flame.FRAGMENT_CUSTOM_INPUT_NAMES))
    if not_inputs:
        problems.append("등록 이름이 조각 셰이더 입력에 없음: " + ", ".join(not_inputs))
    if scalars.get("FragmentHeatRatio") != 0.0:
        problems.append(f"마스터 FragmentHeatRatio 기본값 {scalars.get('FragmentHeatRatio')}(기대 0.0)")
    return problems


def find_undetected_fragment_push_mutations(flame, emitter_text, live_text):
    mutations = (
        ("열 비율 이름 오타", emitter_text.replace('TEXT("FragmentHeatRatio")', 'TEXT("FragmentHeatRatios")', 1), live_text, flame),
        ("불 등록 누락", emitter_text.replace(FRAGMENT_REGISTRATION_LINE, "RegisterExternalMaterialTarget(nullptr)", 1), live_text, flame),
        ("평면 법선 이름 누락", emitter_text, live_text.replace('TEXT("PlaneAxisN")', 'TEXT("PlaneAxisNormal")'), flame),
        ("마스터 열 비율 기본 1", emitter_text, live_text, FragmentDefaultsOverride(flame, {"FragmentHeatRatio": 1.0})),
        ("마스터 뜨거운 색 기본값 누락", emitter_text, live_text, FragmentDefaultsOverride(flame, dropped_vector="FragmentHotGlowColor")),
    )
    return [label for label, emitter, live, source in mutations if not find_fragment_push_problems(source, emitter, live)]


class FragmentDefaultsOverride:
    def __init__(self, flame, scalar_overrides=None, dropped_vector=""):
        overrides = scalar_overrides or {}
        self.FRAGMENT_SCALAR_PARAMETER_DEFAULTS = tuple((key, overrides.get(key, default)) for key, default in flame.FRAGMENT_SCALAR_PARAMETER_DEFAULTS)
        self.FRAGMENT_VECTOR_PARAMETER_DEFAULTS = tuple((key, default) for key, default in flame.FRAGMENT_VECTOR_PARAMETER_DEFAULTS if key != dropped_vector)
        self.FRAGMENT_CUSTOM_INPUT_NAMES = flame.FRAGMENT_CUSTOM_INPUT_NAMES


def read_fragment_cpp_defaults(header_text):
    start = header_text.find(FRAGMENT_SETTINGS_STRUCT_LINE)
    if start < 0:
        return None
    end = header_text.find("\n};", start)
    block = header_text[start:] if end < 0 else header_text[start:end]
    return {name: float(value) for name, value in CPP_FLOAT_DEFAULT_PATTERN.findall(block)}


def find_fragment_cpp_default_problems(flame, header_text):
    cpp_defaults = read_fragment_cpp_defaults(header_text)
    if cpp_defaults is None:
        return [f"{FRAGMENT_SETTINGS_STRUCT_LINE} 블록을 찾지 못함"]
    master = dict(flame.FRAGMENT_SCALAR_PARAMETER_DEFAULTS)
    problems = []
    for field in FRAGMENT_CPP_DEFAULT_FIELDS:
        parameter = "Fragment" + field
        if field not in cpp_defaults:
            problems.append(f"C++ 조각 설정에 {field} 기본값 없음")
        elif parameter not in master:
            problems.append(f"조각 마스터에 {parameter} 기본값 없음")
        elif abs(cpp_defaults[field] - master[parameter]) > 1e-6:
            problems.append(f"{field} C++ {cpp_defaults[field]} 와 마스터 {parameter} {master[parameter]} 다름")
    return problems


def replace_in_fragment_settings(header_text, original, mutated):
    start = header_text.find(FRAGMENT_SETTINGS_STRUCT_LINE)
    if start < 0:
        return header_text
    return header_text[:start] + header_text[start:].replace(original, mutated, 1)


def collect_identity_sources(flame, nodes):
    defaults = {
        "flame": dict(flame.FLAME_SCALAR_PARAMETER_DEFAULTS),
        "ember": dict(flame.EMBER_SCALAR_PARAMETER_DEFAULTS),
        "fragment": dict(flame.FRAGMENT_SCALAR_PARAMETER_DEFAULTS),
    }
    instances = [spec for key, specs in vars(flame).items() if key.endswith("_INSTANCE_SPECS") for spec in specs]
    inputs = {node["label"]: tuple(node["inputs"]) for node in nodes}
    return defaults, instances, inputs


def find_identity_default_problems(defaults, instances, inputs):
    problems = []
    for label, name in IDENTITY_DEFAULT_INPUTS:
        if name not in inputs.get(label, ()):
            problems.append(f"{label} 입력에 {name} 없음")
        master_value = defaults.get(label, {}).get(name)
        if master_value != 0.0:
            problems.append(f"마스터 {name} 기본값 {master_value}(기대 0.0)")
    return problems


def find_undetected_identity_default_mutations(defaults, instances, inputs):
    undetected = []
    baseline_problems = set(find_identity_default_problems(defaults, instances, inputs))
    for label, name in IDENTITY_DEFAULT_INPUTS:
        mutated = {key: dict(values) for key, values in defaults.items()}
        mutated.setdefault(label, {})[name] = 0.5
        if set(find_identity_default_problems(mutated, instances, inputs)) <= baseline_problems:
            undetected.append(name)
    return undetected


def identity_reference_code(label, code):
    original, replacement = IDENTITY_REFERENCE_EDITS[label]
    return code.replace(original, replacement, 1) if code.count(original) == 1 else ""


def compile_identity_body(dxc, node, code, name, value, label, work_folder):
    if not code:
        return ""
    source = build_source(node, code, VOLUME_TEXTURE_NAMES)
    if value is not None:
        pattern = re.compile(r"^(return (?:float4\()?CustomExpression0\(.*)\bInput_" + name + r"\b", re.MULTILINE)
        source, count = pattern.subn(rf"\g<1>{value}", source)
        if count != 1:
            return ""
    outcome = compile_source(dxc, source, label, True, work_folder)
    return outcome["body"] if outcome["exit"] == 0 else ""


def find_python_function_body(text, name):
    start = text.find(f"\ndef {name}(")
    if start < 0:
        return ""
    end = text.find("\ndef ", start + 1)
    return text[start:] if end < 0 else text[start:end]


def find_fragment_usage_problems(flame_text):
    problems = []
    if FRAGMENT_USAGE_CALL not in find_python_function_body(flame_text, FRAGMENT_MASTER_FUNCTION):
        problems.append(f"{FRAGMENT_MASTER_FUNCTION} 가 {FRAGMENT_USAGE_FUNCTION} 를 부르지 않음")
    if FRAGMENT_USAGE_FLAG_CALL not in find_python_function_body(flame_text, FRAGMENT_USAGE_FUNCTION):
        problems.append(f"{FRAGMENT_USAGE_FUNCTION} 가 MATUSAGE_GEOMETRY_COLLECTIONS 를 켜지 않음")
    return problems


def read_project_text(root, parts):
    path = root.joinpath(*parts)
    return path.read_text(encoding="utf-8") if path.is_file() else ""


def find_script_self_check_problems(flame, volume):
    problems = []
    for node_code, inputs, samplers, symbols in (
        (flame.FLAME_HLSL_RELATIVE_PATH, flame.FLAME_CUSTOM_INPUT_NAMES, flame.FLAME_HLSL_SAMPLER_NAMES, flame.FLAME_HLSL_ENGINE_SYMBOLS),
        (flame.EMBER_HLSL_RELATIVE_PATH, flame.EMBER_CUSTOM_INPUT_NAMES, (), flame.EMBER_HLSL_ENGINE_SYMBOLS),
        (flame.WOOD_HLSL_RELATIVE_PATH, flame.WOOD_CUSTOM_INPUT_NAMES, flame.WOOD_HLSL_SAMPLER_NAMES, flame.WOOD_HLSL_ENGINE_SYMBOLS),
        (flame.FRAGMENT_HLSL_RELATIVE_PATH, flame.FRAGMENT_CUSTOM_INPUT_NAMES, flame.FRAGMENT_HLSL_SAMPLER_NAMES, flame.FRAGMENT_HLSL_ENGINE_SYMBOLS),
    ):
        _, code = flame.load_hlsl_code(node_code)
        problems += flame.validate_hlsl_code(code, inputs, samplers, symbols)
    _, volume_problems = volume.load_and_check_all_hlsl()
    return problems + volume_problems


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parents[2]))
    parser.add_argument("--dxc", default="")
    arguments = parser.parse_args()
    root = pathlib.Path(arguments.root)
    results = []
    sample_counts = {}

    def record(label, passed, detail=""):
        results.append({"check": label, "passed": bool(passed), "detail": detail})
        print(("PASS " if passed else "FAIL ") + label + (": " + detail if detail else ""))

    sys.modules["unreal"] = mock.MagicMock()
    flame = load_script(root, "editor_make_fluid_flame_material")
    volume = load_script(root, "editor_make_fluid_volume_material")
    record("재질 스크립트 2개 unreal 스텁 임포트", True)

    nodes = describe_nodes(flame, volume)
    shader_root = root / "Tools" / "FluidFX" / "Shaders"
    codes = {node["label"]: (shader_root / node["hlsl"]).read_text(encoding="utf-8") for node in nodes}

    text_problems = []
    for node in nodes:
        text_problems += find_text_problems(node["hlsl"], codes[node["label"]])
    record("셰이더 텍스트(주석·절대 경로·ASCII·LF·#undef 짝)", not text_problems, "; ".join(text_problems))
    record("음성 대조: 텍스트 검사가 주석·#undef 누락을 잡음", len(find_text_problems("synthetic", "#define TD_X 1\nfloat a = 1.0; // note\n")) == 2)
    ramp_problems = find_ramp_guard_problems(codes["flame"])
    if dict(flame.FLAME_SCALAR_PARAMETER_DEFAULTS).get("FlameColorRampMode") != 0.0:
        ramp_problems.append("마스터 FlameColorRampMode 기본값이 0.0 이 아님")
    record("불꽃 램프 끔 경로 보존(마스터 FlameColorRampMode 기본 0, 흑체 색도·밝기·덮임·방출 줄 그대로, 보호 변수 대입은 분기 안 chromaColor 만)", not ramp_problems, "; ".join(ramp_problems))
    undetected_ramp_mutations = find_undetected_ramp_mutations(codes["flame"])
    record(f"음성 대조: 램프 끔 경로 변형 {len(RAMP_MUTATIONS)}건(분기 밖 대입·방출·밝기·덮임·흑체·분기 조건·모드 강제·분기 안 대입 제거)을 잡음", not undetected_ramp_mutations, ", ".join(undetected_ramp_mutations))

    volume_code = codes["volume"]
    record(
        "볼륨 디더 프레임 = View.StateFrameIndexMod8(게임 시간 디더 없음)",
        "View.StateFrameIndexMod8" in volume_code and "ElapsedSeconds * 60.0" not in volume_code,
    )
    volume_scalars = dict(volume.VOLUME_MASTER["scalars"])
    volume_instances = [spec for spec in volume.INSTANCE_SPECS if spec["master"] == volume.VOLUME_MASTER["name"]]
    soft_abs_problems = find_soft_abs_contract_problems(volume_scalars, volume.VOLUME_MASTER["inputs"], volume_instances, volume_code)
    record("볼륨 노이즈 부드러운 절댓값 계약(NoiseSoftAbsEpsilon·NoiseSoftAbsMeanShift 등록, 마스터 기본 0, 인스턴스는 둘을 함께 양수로, TD_SHIFT_OF 두 항 모두 TD_NOISE_ABS)", not soft_abs_problems, "; ".join(soft_abs_problems))
    undetected_soft_abs = find_undetected_soft_abs_contract_mutations(volume_scalars, volume.VOLUME_MASTER["inputs"], volume_instances, volume_code)
    record("음성 대조: 부드러운 절댓값 계약 변형 4건(엡실론 기본 0.04·평균 변위 입력 누락·엡실론만 덮어씀·b 항 abs 직접 사용)을 잡음", not undetected_soft_abs, ", ".join(undetected_soft_abs))

    solver_plane_problems = []
    for label in SOLVER_PLANE_SHADER_LABELS:
        solver_plane_problems += find_solver_plane_offset_problems(label, codes[label])
    record("위치 투영 = 축 내적 + PlaneCoordinateOffsetCm, 방향 투영 = 축 내적(불꽃·볼륨·가짜 볼륨)", not solver_plane_problems, "; ".join(solver_plane_problems))
    undetected_mutations = find_undetected_solver_plane_mutations(codes)
    record("음성 대조: 광원 위치 오프셋 누락·광원 방향 오프셋·불꽃 UV 오프셋 누락을 잡음", not undetected_mutations, "; ".join(undetected_mutations))

    alu_users = [name for name in NO_ALU_NOISE_SHADERS if "GradientNoise3D_ALU" in (shader_root / name).read_text(encoding="utf-8")]
    record("ALU 그라디언트 노이즈 → 3D 노이즈 텍스처 대체", not alu_users, ", ".join(alu_users))

    self_problems = find_script_self_check_problems(flame, volume)
    record("재질 스크립트 자체 점검(입력 사용·샘플러·엔진 심볼·괄호·인스턴스 이름)", not self_problems, "; ".join(self_problems))
    contract_problems = find_flame_contract_problems(flame, nodes)
    record("불꽃·불티·장작·조각 입력 원천과 혼합 요청 사상", not contract_problems, "; ".join(contract_problems))
    emitter_text = read_project_text(root, FRAGMENT_EMITTER_SOURCE)
    live_text = read_project_text(root, LIVE_COMPONENT_SOURCE)
    fragment_push_problems = find_fragment_push_problems(flame, emitter_text, live_text)
    record("조각 발광 C++ 값 이름 계약(방출 액터 Fragment* 스칼라 7개·벡터 3개(색 2·키 중심 1)가 마스터 기본값에 있음, MID 를 불 라이브 컴포넌트에 등록, 등록 MID 의 평면·Aux 이름 9개가 셰이더 입력, 마스터 FragmentHeatRatio 기본 0)", not fragment_push_problems, "; ".join(fragment_push_problems))
    undetected_fragment_push = find_undetected_fragment_push_mutations(flame, emitter_text, live_text)
    record("음성 대조: 조각 값 이름 계약 변형 5건(열 비율 이름 오타·불 등록 누락·평면 법선 이름 누락·마스터 열 비율 기본 1·마스터 뜨거운 색 기본값 누락)을 잡음", not undetected_fragment_push, ", ".join(undetected_fragment_push))
    header_text = read_project_text(root, FIRE_TYPES_SOURCE)
    cpp_default_problems = find_fragment_cpp_default_problems(flame, header_text)
    record(f"C++ 조각 기본 = 마스터 기본({', '.join(FRAGMENT_CPP_DEFAULT_FIELDS)} 와 Fragment+이름, 1e-6)", not cpp_default_problems, "; ".join(cpp_default_problems))
    mutated_header_text = replace_in_fragment_settings(header_text, *FRAGMENT_CPP_DEFAULT_MUTATION)
    record(
        f"음성 대조: C++ 조각 설정 {FRAGMENT_CPP_DEFAULT_MUTATION[0]} → {FRAGMENT_CPP_DEFAULT_MUTATION[1]} 를 잡음",
        mutated_header_text != header_text and bool(find_fragment_cpp_default_problems(flame, mutated_header_text)),
    )
    identity_defaults, identity_instances, identity_inputs = collect_identity_sources(flame, nodes)
    identity_problems = find_identity_default_problems(identity_defaults, identity_instances, identity_inputs)
    record(f"항등 입력 기본({', '.join(name for _, name in IDENTITY_DEFAULT_INPUTS)}: 셰이더 입력, 마스터 기본 0.0)", not identity_problems, "; ".join(identity_problems))
    undetected_identity = find_undetected_identity_default_mutations(identity_defaults, identity_instances, identity_inputs)
    record("음성 대조: 항등 입력 마스터 기본 0.5 를 잡음", not undetected_identity, ", ".join(undetected_identity))
    flame_text = read_project_text(root, FRAGMENT_MATERIAL_SCRIPT)
    usage_problems = find_fragment_usage_problems(flame_text)
    record("조각 마스터 생성 함수가 지오메트리 컬렉션 사용 플래그를 켬(create_fragment_master → enable_geometry_collection_usage → MATUSAGE_GEOMETRY_COLLECTIONS)", not usage_problems, "; ".join(usage_problems))
    removed_usage_text = flame_text.replace(FRAGMENT_USAGE_CALL, 'usage = "MATUSAGE_GEOMETRY_COLLECTIONS"', 1)
    record("음성 대조: 조각 마스터 생성 함수의 사용 플래그 호출 삭제를 잡음", removed_usage_text != flame_text and bool(find_fragment_usage_problems(removed_usage_text)))
    transmittance_constants = read_transmittance_constants(root)
    constant_values = set(transmittance_constants.values())
    record(
        "투과율 상수 3곳 일치(가장자리 0.24·덮임 2.5·높이 0.68·상단 0.15·높이 단 4)",
        len(constant_values) == 1 and None not in next(iter(constant_values)),
        json.dumps({key: value for key, value in transmittance_constants.items()}, ensure_ascii=False),
    )

    dxc = find_dxc(arguments.dxc)
    if not dxc:
        record("dxc 찾기", False, "Windows Kits dxc.exe 없음(--dxc 로 지정)")
        return finish(results, sample_counts)
    record("dxc 찾기", True, dxc)

    with tempfile.TemporaryDirectory() as folder:
        work_folder = pathlib.Path(folder)
        for node in nodes:
            source = build_source(node, codes[node["label"]], VOLUME_TEXTURE_NAMES)
            for optimize in (False, True):
                suffix = "O3" if optimize else "Od"
                outcome = compile_source(dxc, source, f"{node['label']}_{suffix}", optimize, work_folder)
                record(f"dxc {node['label']} ps_6_0 {suffix}", outcome["exit"] == 0, outcome["error"] or f"표본 {outcome['samples']} 경고 {outcome['warnings']}")
                if optimize:
                    sample_counts[node["label"]] = outcome["samples"]

        record(
            f"불꽃 카드 정적 표본 수 = {FLAME_CARD_SAMPLE_BUDGET}(흐름 1·미리보기 밀도 1·노이즈 2위상×2·Catmull-Rom 5·Aux 열 1)",
            sample_counts.get("flame") == FLAME_CARD_SAMPLE_BUDGET,
            f"{sample_counts.get('flame')}",
        )
        record(
            f"조각 발광 정적 표본 수 = {FRAGMENT_SAMPLE_BUDGET}(Aux 열 1)",
            sample_counts.get("fragment") == FRAGMENT_SAMPLE_BUDGET,
            f"{sample_counts.get('fragment')}",
        )
        record(
            f"볼륨 정적 표본 수 = {VOLUME_SAMPLE_BUDGET}(FX-8a 33 + 투과율 1 + Aux 껍질 1 + Aux 표면 1)",
            sample_counts.get("volume") == VOLUME_SAMPLE_BUDGET,
            f"{sample_counts.get('volume')}",
        )
        volume_node = next(node for node in nodes if node["label"] == "volume")
        reference_body = compile_folded_body(dxc, volume_node, soft_abs_reference_code(volume_code), "0.0", "soft_abs_reference", work_folder)
        default_body = compile_folded_body(dxc, volume_node, volume_code, "0.0", "soft_abs_default", work_folder)
        soft_body = compile_folded_body(dxc, volume_node, volume_code, "0.04", "soft_abs_on", work_folder)
        record(
            "볼륨 NoiseSoftAbsEpsilon 호출 인수를 0 으로 접은 dxc -O3 본문 = TD_NOISE_ABS 를 abs 로 바꾼 기준 본문(바이트 동일), 0.04 로 접으면 다름",
            bool(reference_body) and default_body == reference_body and bool(soft_body) and soft_body != reference_body,
            f"기준 {len(reference_body)}자, 0 {len(default_body)}자, 0.04 {len(soft_body)}자",
        )
        undetected_soft_abs_mutations = [
            label
            for index, (label, original, mutated) in enumerate(SOFT_ABS_DXC_MUTATIONS)
            if original not in volume_code
            or compile_folded_body(dxc, volume_node, volume_code.replace(original, mutated, 1), "0.0", f"soft_abs_mutant_{index}", work_folder) in ("", reference_body)
        ]
        record(f"음성 대조: 부드러운 절댓값 끔 경로 변형 {len(SOFT_ABS_DXC_MUTATIONS)}건(켬 조건 >=·끔 경로 sqrt(X²))은 0 본문이 기준과 달라 잡힘", not undetected_soft_abs_mutations, ", ".join(undetected_soft_abs_mutations))
        nodes_by_label = {node["label"]: node for node in nodes}
        for index, (label, name) in enumerate(IDENTITY_DEFAULT_INPUTS):
            node = nodes_by_label[label]
            code = codes[label]
            reference_body = compile_identity_body(dxc, node, identity_reference_code(label, code), name, None, f"identity_reference_{index}", work_folder)
            zero_body = compile_identity_body(dxc, node, code, name, "0.0", f"identity_zero_{index}", work_folder)
            half_body = compile_identity_body(dxc, node, code, name, "0.5", f"identity_half_{index}", work_folder)
            record(
                f"항등 입력 {name} 호출 인수를 0 으로 접은 dxc -O3 본문 = 기준 본문({label} 기준 편집), 0.5 로 접으면 다름",
                bool(reference_body) and zero_body == reference_body and bool(half_body) and half_body != reference_body,
                f"기준 {len(reference_body)}자, 0 {len(zero_body)}자, 0.5 {len(half_body)}자",
            )
            mutant_code = code.replace(f"saturate({name})", f"saturate({name} + 0.05)", 1)
            mutant_body = compile_identity_body(dxc, node, mutant_code, name, "0.0", f"identity_mutant_{index}", work_folder) if mutant_code != code else ""
            record(
                f"음성 대조: saturate({name}) → saturate({name} + 0.05) 는 0 접기 본문이 기준과 달라 잡힘",
                bool(reference_body) and mutant_body not in ("", reference_body),
            )
        flame_node = next(node for node in nodes if node["label"] == "flame")
        control = compile_source(dxc, build_source(flame_node, codes["flame"], set()), "control_texture2d", False, work_folder)
        record("음성 대조: 노이즈를 Texture2D 로 선언하면 컴파일 실패", control["exit"] != 0, control["error"][:160])
    return finish(results, sample_counts)


def finish(results, sample_counts):
    failed = [item["check"] for item in results if not item["passed"]]
    print(json.dumps({"passed": len(results) - len(failed), "failed": failed, "static_samples": sample_counts}, ensure_ascii=False))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
