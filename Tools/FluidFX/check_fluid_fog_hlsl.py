"""실행 환경: 시스템 Python 3(numpy) 와 Windows Kits dxc.exe(ps_6_0), 에디터 불필요
실행 명령: python Tools/FluidFX/check_fluid_fog_hlsl.py [--root 프로젝트경로] [--dxc dxc경로] [--look-fragment 조각.json] [--output 요약.json]
출력: 항목별 PASS/FAIL 줄과 마지막 줄 JSON 요약(dxc 명령·표본 수, 결정론 시험 sha256), 하나라도 실패하면 종료 코드 1
상태: 미검증(2026-10-25 FX-10r 작성). TDFluidFogVolume.hlsl 을 재질 스크립트와 같은 조립 함수로 조립해 텍스트·식 고정점·이름 계약(MI 덮어쓰기에 C++ 구동·조건부 이름 없음, 정본에 아직 없으면 --look-fragment 로 조각을 더해 본다)·dxc ps_6_0 O3/Od 와 역할(정적·구동)×모양(0·1·2) 상수 접기 순열 컴파일을 보고, numpy 거울 시험 D1 창 분할·D2 돌출과 바닥(상자 아래면 = z0, 반 크기 ObjectBounds)·D3 보완 페이드·D4 UV 를 두 번 돌려 sha256 이 같은지 본다.
"""
import argparse
import glob
import hashlib
import importlib.util
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
from unittest import mock

import numpy as np

sys.dont_write_bytecode = True

HEADER = """
#define Texture2DSampleLevel(T, S, UV, L) T.SampleLevel(S, UV, L)
#define Texture3DSampleLevel(T, S, UV, L) T.SampleLevel(S, UV, L)
"""
SAMPLE_PATTERN = re.compile(r"call \S+ @dx\.op\.(?:sample|sampleLevel|sampleBias|sampleGrad)\.")
INSTRUCTION_PATTERN = re.compile(r"^\s+(?:%\S+ = |call |store |br |ret )", re.MULTILINE)
ABSOLUTE_PATH_PATTERN = re.compile(r"[A-Za-z]:[\\/]")
REQUIRED_ANCHORS = (
    "lerp(FogObjectPosition.z - FogObjectBoundsCm.z, SimCenterWorld.z, (IS_DRIVEN))",
    "float fogIsDriven = step(0.5, SimGridResolution);",
    "float2 fogWindowUv = (FogWorldPosition.xy - fogWindowCenter.xy) / fogWindowSize + 0.5;",
    "float fogWindowWeight = fogHasWindow * smoothstep(0.0, fogBlendWidthCm, fogEdgeDistanceCm);",
    "1.0 - smoothstep(fogTopCm - max(FogTopFadeCm, 1.0), fogTopCm, fogHeightCm)",
    "(FogUsesWindowMask > 0.5 ? 1.0 - fogWindowWeight : 1.0)",
    "(FogCameraFadeInvert > 0.5 ? 1.0 - fogCameraRamp : fogCameraRamp)",
)
FORBIDDEN_TOKENS = ("FogObjectBoundsCm.z * 0.5", "FogObjectBoundsCm.z / 2", "0.5 * FogObjectBoundsCm", "DensityScale", "SoftFalloffCm", "VolumeHeightCm")
FOLD_NAMES = ("SimGridResolution", "FogShape")
UV_GRID_RESOLUTION = 256
SPLIT_GRID_SIZE = 257


def load_script(root, name):
    spec = importlib.util.spec_from_file_location(name, root / "Tools" / "FluidFX" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def find_dxc(explicit_path):
    candidates = [explicit_path, os.environ.get("DXC_PATH")]
    program_files = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    candidates.extend(reversed(sorted(glob.glob(os.path.join(program_files, "Windows Kits", "10", "bin", "*", "x64", "dxc.exe")))))
    return next((candidate for candidate in candidates if candidate and os.path.isfile(candidate)), "")


def normalize(text):
    return re.sub(r"\s+", " ", text)


def declare_inputs(material, folded):
    vector_names = set(material.VECTOR_INPUT_NAMES)
    lines = ["Texture2D DensityPressureTexture;", "SamplerState DensityPressureTextureSampler;", "Texture3D NoiseVolumeTexture;", "SamplerState NoiseVolumeTextureSampler;"]
    constants = "".join(f"static const float {name} = {value};\n" for name, value in folded.items())
    scalars = [f"{'float3' if name in vector_names else 'float'} {name};" for name in material.CUSTOM_INPUT_NAMES if name not in material.TEXTURE_PARAMETER_NAMES and name not in folded]
    return "\n".join(lines) + "\n" + constants + "cbuffer Inputs {\n" + "\n".join(scalars) + "\n}\n"


def build_source(material, code, folded):
    return HEADER + declare_inputs(material, folded) + f"float4 TDFogDensity(float4 SvPositionValue)\n{{\n{code}\n}}\n" + "float4 main(float4 sv : SV_Position) : SV_Target\n{\nreturn TDFogDensity(sv);\n}\n"


def compile_source(dxc, source, label, optimize, work_folder):
    path = work_folder / f"{label}.hlsl"
    disassembly = work_folder / f"{label}.dxil.txt"
    path.write_text(source, encoding="utf-8")
    command = [dxc, "-T", "ps_6_0", "-E", "main", str(path), "-Fc", str(disassembly), "-Fo", str(work_folder / f"{label}.bin"), "-O3" if optimize else "-Od"]
    completed = subprocess.run(command, capture_output=True, text=True)
    text = disassembly.read_text(encoding="utf-8", errors="replace") if disassembly.exists() else ""
    first_error = next((line for line in (completed.stderr + completed.stdout).splitlines() if "error" in line), "")
    return {"exit": completed.returncode, "samples": len(SAMPLE_PATTERN.findall(text)), "instructions": len(INSTRUCTION_PATTERN.findall(text)), "error": first_error}


def smoothstep(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def window_weight(points_xy, center_xy, extent_xy, blend_ratio):
    size = np.maximum(np.asarray(extent_xy, dtype=np.float64), 1.0)
    uv = (points_xy - np.asarray(center_xy)) / size + 0.5
    edge = (0.5 - np.abs(uv - 0.5)) * size
    edge_distance = np.minimum(edge[..., 0], edge[..., 1])
    has_window = float(extent_xy[0] >= 0.5 and extent_xy[1] >= 0.5)
    blend_width = max(blend_ratio * float(size.min()), 1.0)
    return has_window * smoothstep(0.0, blend_width, edge_distance), edge_distance


def role_weight(is_driven, weight, uses_window_mask):
    if is_driven:
        return weight
    return 1.0 - weight if uses_window_mask else np.ones_like(weight)


def layer_profile(source, height_cm, extrude_cm, exponent, top_fade_cm):
    top = extrude_cm * np.power(np.clip(source, 0.0, 1.0), max(exponent, 0.01))
    profile = 1.0 - smoothstep(top - max(top_fade_cm, 1.0), top, height_cm)
    return np.where(top > 0.0, profile, 0.0)


def camera_fade(distance_cm, start_cm, length_cm, invert):
    if length_cm <= 0.0:
        return np.ones_like(distance_cm)
    ramp = smoothstep(start_cm, start_cm + max(length_cm, 0.001), distance_cm)
    return 1.0 - ramp if invert else ramp


def floor_height(is_driven, object_position_z, object_bounds_half_z, sim_center_z):
    return sim_center_z if is_driven else object_position_z - object_bounds_half_z


def test_window_split():
    center, extent, ratio, constant = (21000.0, 25000.0), (700.0, 700.0), 0.15, 0.6
    axis_x = np.linspace(center[0] - 0.75 * extent[0], center[0] + 0.75 * extent[0], SPLIT_GRID_SIZE)
    axis_y = np.linspace(center[1] - 0.75 * extent[1], center[1] + 0.75 * extent[1], SPLIT_GRID_SIZE)
    points = np.stack(np.meshgrid(axis_x, axis_y, indexing="ij"), axis=-1)
    weight, edge = window_weight(points, center, extent, ratio)
    driven = constant * role_weight(True, weight, True) * (edge >= 0.0)
    passive = constant * role_weight(False, weight, True)
    total = driven + passive
    error = float(np.abs(total - constant).max())
    no_writer, _ = window_weight(points, center, (0.0, 0.0), ratio)
    passive_alone = constant * role_weight(False, no_writer, True)
    error_alone = float(np.abs(passive_alone - constant).max())
    inside_full = float(weight[SPLIT_GRID_SIZE // 2, SPLIT_GRID_SIZE // 2])
    passed = error <= 1e-6 and error_alone <= 1e-6 and abs(inside_full - 1.0) <= 1e-9 and float(weight[0, 0]) == 0.0
    return passed, f"합 오차 {error:.2e}, MPC 없음 오차 {error_alone:.2e}, 중심 가중 {inside_full}", total


def test_extrude_and_floor():
    heights = np.linspace(-10.0, 200.0, 21001)
    profile = layer_profile(np.ones_like(heights), heights, 90.0, 1.0, 40.0)
    monotone = bool(np.all(np.diff(profile) <= 1e-12))
    upper = heights[np.argmin(np.abs(profile - 0.9))]
    lower = heights[np.argmin(np.abs(profile - 0.1))]
    width_ratio = float((lower - upper) / 40.0)
    zero_source = float(layer_profile(np.zeros(5), np.linspace(0.0, 50.0, 5), 90.0, 1.0, 40.0).max())
    floor_cm, box_height_cm = 123.0, 1300.0
    box_center_z = floor_cm + 0.5 * box_height_cm
    object_bounds_half_z = 0.5 * box_height_cm
    static_floor = floor_height(False, box_center_z, object_bounds_half_z, 0.0)
    driven_floor = floor_height(True, box_center_z, object_bounds_half_z, floor_cm)
    box_bottom = box_center_z - object_bounds_half_z
    old_formula_gap = abs((box_center_z - 0.5 * object_bounds_half_z) - box_bottom)
    passed = monotone and abs(width_ratio - 0.608) <= 0.005 and zero_source == 0.0 and static_floor == box_bottom and driven_floor == box_bottom and old_formula_gap > 1.0
    detail = f"단조 {monotone}, 10-90 폭/페이드 {width_ratio:.4f}, d=0 최대 {zero_source}, 상자 아래면 {box_bottom} 정적 {static_floor} 구동 {driven_floor}, 옛 식 차 {old_formula_gap}"
    return passed, detail, profile


def test_complementary_fade():
    distance = np.linspace(0.0, 10000.0, 10001)
    near = camera_fade(distance, 2500.0, 1000.0, True)
    far = camera_fade(distance, 2500.0, 1000.0, False)
    error = float(np.abs(near + far - 1.0).max())
    off = camera_fade(distance, 2500.0, 0.0, False)
    passed = error <= 1e-12 and float(off.min()) == 1.0 and float(near[0]) == 1.0 and float(far[-1]) == 1.0
    return passed, f"근+원 오차 {error:.2e}, 끔 최소 {float(off.min())}", np.stack([near, far])


def test_window_uv():
    center = np.array([21000.0, 25000.0], dtype=np.float32)
    extent = np.array([700.0, 700.0], dtype=np.float32)
    corners = np.array([center - 0.5 * extent, center + 0.5 * extent], dtype=np.float32)
    corner_uv = (corners - center) / extent + np.float32(0.5)
    corner_error = float(np.abs(corner_uv - np.array([[0.0, 0.0], [1.0, 1.0]], dtype=np.float32)).max())
    cells = np.arange(UV_GRID_RESOLUTION, dtype=np.float32)
    cell_points = center[0] + ((cells + np.float32(0.5)) / np.float32(UV_GRID_RESOLUTION) - np.float32(0.5)) * extent[0]
    cell_uv = (cell_points - center[0]) / extent[0] + np.float32(0.5)
    round_trip = cell_uv * np.float32(UV_GRID_RESOLUTION) - np.float32(0.5)
    round_error = float(np.abs(round_trip - cells).max())
    cpp_uv = (cell_points - (center[0] - np.float32(0.5) * extent[0])) / extent[0]
    formula_error = float(np.abs(cpp_uv - cell_uv).max() * UV_GRID_RESOLUTION)
    passed = corner_error <= 1e-6 and round_error <= 1e-4 and formula_error <= 1.0
    return passed, f"모서리 {corner_error:.2e}, 칸 왕복 {round_error:.2e}칸, C++ 식 대비 {formula_error:.2e}칸", round_trip


def run_mirror_tests():
    outcomes = {}
    digest = hashlib.sha256()
    for label, test in (("D1 창 분할", test_window_split), ("D2 돌출·바닥", test_extrude_and_floor), ("D3 보완 페이드", test_complementary_fade), ("D4 UV", test_window_uv)):
        passed, detail, values = test()
        values = np.ascontiguousarray(values)
        digest.update(label.encode("utf-8"))
        digest.update(values.tobytes())
        outcomes[label] = (passed, detail, hashlib.sha256(values.tobytes()).hexdigest())
    return outcomes, digest.hexdigest()


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
    return failures


def collect_look_entries(root, fragment_path):
    entries = {}
    canonical = root / "Content" / "FluidFX" / "PresetData" / "TDFluidMaterialLook.json"
    if canonical.is_file():
        entries.update(json.loads(canonical.read_text(encoding="utf-8")).get("Materials", {}))
    if fragment_path:
        fragment = json.loads(pathlib.Path(fragment_path).read_text(encoding="utf-8"))
        overlap = sorted(set(fragment.get("Materials", {})) & set(entries))
        entries.update(fragment.get("Materials", {}))
        return entries, overlap
    return entries, []


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
    material = load_script(root, "editor_make_fluid_fog_material")
    record("재질 스크립트 unreal 스텁 임포트", True)
    stage_path = root / "Tools" / "FluidFX" / "editor_make_fluid_fog_stage.py"
    if stage_path.is_file():
        stage = load_script(root, "editor_make_fluid_fog_stage")
        layout_problems = stage.validate_stage_layout()
        record("무대 배치(구역 간격·200 m 상자 분리·상자 높이 ≥ 돌출+페이드·MPC 쓰기 액터 1개)", not layout_problems, "; ".join(layout_problems))
    else:
        record("무대 스크립트 unreal 스텁 임포트", False, "editor_make_fluid_fog_stage.py 없음")

    source = material.read_fog_source(str(root))
    text_failures = run_shader_text_checks((root / "Tools" / "FluidFX" / "Shaders" / material.FOG_HLSL_NAME).read_text(encoding="utf-8"))
    record("셰이더 텍스트(주석·절대 경로·ASCII·LF)", not text_failures, "; ".join(text_failures))
    code = material.assemble_node_code(source)
    problems = material.validate_hlsl_code(code) + [f"입력 원천 없음: {name}" for name in material.find_unresolved_inputs()]
    record("조립 코드 자체 점검", not problems, "; ".join(problems))
    normalized = normalize(code)
    missing_anchors = [anchor for anchor in REQUIRED_ANCHORS if normalize(anchor) not in normalized]
    forbidden = [token for token in FORBIDDEN_TOKENS if token in code]
    record("식 고정점(바닥 = 반 크기 아래면, 창 UV, 역할 가중, 페이드)·금지 토큰", not missing_anchors and not forbidden, "; ".join(missing_anchors + forbidden))

    entries, overlap = collect_look_entries(root, arguments.look_fragment)
    override_problems = material.find_instance_override_problems(entries)
    record("MI 이름 계약(부모·마스터 이름·C++ 구동·조건부 이름 없음)", not override_problems, "; ".join(override_problems))
    record("조각과 정본 키 서로소", not overlap or not arguments.look_fragment, ", ".join(overlap))

    outcomes_first, digest_first = run_mirror_tests()
    outcomes_second, digest_second = run_mirror_tests()
    for label, (passed, detail, value_sha) in outcomes_first.items():
        record(f"거울 {label}", passed, f"{detail} sha256 {value_sha[:16]}")
    record("거울 시험 2회 sha256 같음", digest_first == digest_second, digest_first)
    summary["mirror_sha256"] = digest_first

    dxc = find_dxc(arguments.dxc)
    if not dxc:
        record("dxc 찾기", False, "Windows Kits dxc.exe 없음(--dxc 로 지정)")
        return finish(results, summary, arguments.output)
    record("dxc 찾기", True, dxc)
    compiles = {}
    with tempfile.TemporaryDirectory() as folder:
        work_folder = pathlib.Path(folder)
        for optimize in (False, True):
            suffix = "O3" if optimize else "Od"
            outcome = compile_source(dxc, build_source(material, code, {}), f"fog_{suffix}", optimize, work_folder)
            record(f"dxc 안개 ps_6_0 {suffix}", outcome["exit"] == 0 and outcome["samples"] >= 3, outcome["error"] or f"표본 {outcome['samples']} 명령 {outcome['instructions']}")
            compiles[suffix] = outcome
        for role_value, role_label in ((0.0, "static"), (256.0, "driven")):
            for shape_value in (0.0, 1.0, 2.0):
                label = f"{role_label}_shape{int(shape_value)}"
                outcome = compile_source(dxc, build_source(material, code, {"SimGridResolution": role_value, "FogShape": shape_value}), label, True, work_folder)
                record(f"dxc 순열 {label} O3", outcome["exit"] == 0, outcome["error"] or f"표본 {outcome['samples']} 명령 {outcome['instructions']}")
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
