"""실행 환경: 시스템 Python 3(numpy) 와 Windows Kits dxc.exe(ps_6_0·vs_6_0), 에디터 불필요
실행 명령: python Tools/FluidFX/check_fluid_track_hlsl.py [--root 프로젝트경로] [--dxc dxc경로] [--look-fragment 조각.json] [--output 요약.json]
출력: 항목별 PASS/FAIL 줄과 마지막 줄 JSON 요약(dxc 표본·명령 수, 결정론 시험 sha256), 하나라도 실패하면 종료 코드 1
상태: 미검증(2026-10-25 FX-12r 작성). 자국 셰이더 4개(TDFluidTrackCommon·Offset·Surface·Sample.hlsl)를 재질 스크립트와 같은 조립 함수로 노드별 조립해 텍스트·식 고정점·이름 계약(--look-fragment 로 병합 전 조각 포함)·dxc(표면·표본 ps_6_0, 오프셋 ps·vs_6_0, O3/Od)·두 노드를 한 파일에 이은 매크로 누수 검사와 음성 대조를 보고, numpy 거울 시험 T1 칸 일치·T2 홈 바닥·T3 둑 대칭·T4 이음매·T5 덮임·T6 잔차 POM 을 두 번 돌려 sha256 이 같은지 본다.
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
#define Texture2DSample(T, S, UV) T.Sample(S, UV)
#define Texture2DSampleLevel(T, S, UV, L) T.SampleLevel(S, UV, L)
"""
SAMPLE_PATTERN = re.compile(r"call \S+ @dx\.op\.(?:sample|sampleLevel|sampleBias|sampleGrad)\.")
INSTRUCTION_PATTERN = re.compile(r"^\s+(?:%\S+ = |call |store |br |ret )", re.MULTILINE)
ABSOLUTE_PATH_PATTERN = re.compile(r"[A-Za-z]:[\\/]")
REQUIRED_ANCHORS = (
    "float2 trackWindowUv = (TrackRestPosition.xy - trackWindowCenter) / trackWindowSize + 0.5;",
    "float trackInsideCm = trackLayerCm * (1.0 - saturate(TrackDepthScale * trackDepth)) + TrackBankHeightCm * max(trackBankBlur - trackDepth, 0.0);",
    "trackHeightCm = trackEdgeDistanceCm < TrackFloorClipInsetCm ? trackOutsideCm - TrackEdgeSinkCm : lerp(trackOutsideCm, trackInsideCm, trackEdgeBlend);",
    "trackFloorClip = (TrackUsesWindowMask > 0.5 && trackEdgeDistanceCm >= TrackFloorClipInsetCm) ? 1.0 : 0.0;",
    "float trackEdgeBlend = smoothstep(TrackFloorClipInsetCm, TrackFloorClipInsetCm + max(TrackEdgeFadeCm, 0.001), trackEdgeDistanceCm);",
    "float trackPomSteps = clamp(round(lerp(TrackPomMinSteps, TrackPomMaxSteps, 1.0 - abs(trackViewDirection.z))), 1.0, 64.0);",
    "float trackPomHalfSpanCm = TrackPomMaxDepthCm / max(abs(trackViewDirection.z), 0.2);",
    "trackHitOffsetCm = trackPreviousOffsetCm + trackPomStepCm * trackPreviousGapCm / max(trackPreviousGapCm - trackStepGapCm, 0.0001);",
    "TrackPixelDepthOffsetCm = max(trackHitOffsetCm, 0.0);",
)
FORBIDDEN_TOKENS = ("SimCenterWorld.z", "DensityScale", "SoftFalloffCm", "VolumeHeightCm")
GRID_RESOLUTION = 256
WINDOW_CENTER = (12000.0, -8000.0)
WINDOW_EXTENT_CM = 4000.0


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


def declare_inputs(material):
    vector_names = set(material.VECTOR_INPUT_NAMES)
    names = list(dict.fromkeys(material.SURFACE_INPUT_NAMES + material.OFFSET_INPUT_NAMES + material.SAMPLE_INPUT_NAMES))
    textures = [f"Texture2D {name};\nSamplerState {name}Sampler;" for name in names if name in material.TEXTURE_PARAMETER_NAMES]
    scalars = [f"{'float3' if name in vector_names else 'float'} {name};" for name in names if name not in material.TEXTURE_PARAMETER_NAMES]
    return "\n".join(textures) + "\ncbuffer Inputs {\n" + "\n".join(scalars) + "\n}\n"


FUNCTIONS = {
    "offset": ("float3 TDOffset(float4 SvPositionValue)", "float3 a = TDOffset(sv);", "float4(a, 1.0)"),
    "surface": ("float4 TDSurface(float4 SvPositionValue, out float3 TrackNormalWS, out float TrackPixelDepthOffsetCm)", "float3 n; float p; float4 f = TDSurface(sv, n, p);", "f + float4(n, p)"),
    "sample": ("float4 TDSample(float4 SvPositionValue, out float3 TrackSampleNormalWS)", "float3 q; float4 g = TDSample(sv, q);", "g + float4(q, 0.0)"),
}


def build_source(declarations, entries, order):
    functions = "".join(f"{FUNCTIONS[kind][0]}\n{{\n{entries[kind]}\n}}\n" for kind in order)
    calls = "\n".join(FUNCTIONS[kind][1] for kind in order)
    total = " + ".join(FUNCTIONS[kind][2] for kind in order)
    return HEADER + declarations + functions + f"float4 main(float4 sv : SV_Position) : SV_Target\n{{\n{calls}\nreturn {total};\n}}\n"


def build_vertex_source(declarations, entries):
    return HEADER + declarations + f"{FUNCTIONS['offset'][0]}\n{{\n{entries['offset']}\n}}\n" + "float4 main(float4 position : POSITION) : SV_Position\n{\nfloat3 a = TDOffset(position);\nreturn float4(a, 1.0);\n}\n"


def compile_source(dxc, source, label, profile, optimize, work_folder):
    path = work_folder / f"{label}.hlsl"
    disassembly = work_folder / f"{label}.dxil.txt"
    path.write_text(source, encoding="utf-8")
    command = [dxc, "-T", profile, "-E", "main", str(path), "-Fc", str(disassembly), "-Fo", str(work_folder / f"{label}.bin"), "-O3" if optimize else "-Od"]
    completed = subprocess.run(command, capture_output=True, text=True)
    text = disassembly.read_text(encoding="utf-8", errors="replace") if disassembly.exists() else ""
    first_error = next((line for line in (completed.stderr + completed.stdout).splitlines() if "error" in line), "")
    return {"exit": completed.returncode, "samples": len(SAMPLE_PATTERN.findall(text)), "instructions": len(INSTRUCTION_PATTERN.findall(text)), "error": first_error}


def smoothstep(edge0, edge1, x):
    t = np.clip((np.asarray(x, dtype=np.float64) - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def saturate(value):
    return np.clip(value, 0.0, 1.0)


class TrackMirror:
    def __init__(self, parameters, field, center=WINDOW_CENTER, extent=WINDOW_EXTENT_CM):
        self.p = parameters
        self.field = np.asarray(field, dtype=np.float64)
        self.center = np.asarray(center, dtype=np.float64)
        self.size = float(extent)
        self.resolution = self.field.shape[0]

    def uv(self, xy):
        return (np.asarray(xy, dtype=np.float64) - self.center) / self.size + 0.5

    def edge_distance(self, xy):
        uv = self.uv(xy)
        edge = (0.5 - np.abs(uv - 0.5)) * self.size
        return np.minimum(edge[..., 0], edge[..., 1])

    def edge_blend(self, edge):
        return smoothstep(self.p["TrackFloorClipInsetCm"], self.p["TrackFloorClipInsetCm"] + max(self.p["TrackEdgeFadeCm"], 0.001), edge)

    def depth(self, uv):
        coordinates = saturate(np.asarray(uv, dtype=np.float64)) * self.resolution - 0.5
        coordinates = np.clip(coordinates, 0.0, self.resolution - 1.0)
        x0 = np.floor(coordinates[..., 0]).astype(int)
        y0 = np.floor(coordinates[..., 1]).astype(int)
        x1 = np.minimum(x0 + 1, self.resolution - 1)
        y1 = np.minimum(y0 + 1, self.resolution - 1)
        fx = coordinates[..., 0] - x0
        fy = coordinates[..., 1] - y0
        f = self.field
        return (f[x0, y0] * (1 - fx) * (1 - fy) + f[x1, y0] * fx * (1 - fy) + f[x0, y1] * (1 - fx) * fy + f[x1, y1] * fx * fy)

    def bank_blur(self, uv, taps):
        radius_uv = max(self.p["TrackBankRadiusCm"], 1.0) / self.size
        total = 0.0
        for index in range(taps):
            angle = 2.0 * np.pi * index / taps
            total = total + self.depth(np.asarray(uv) + np.array([np.cos(angle), np.sin(angle)]) * radius_uv)
        return total / taps

    def layer(self):
        layer = max(self.p["TrackLayerThicknessCm"], 0.0)
        return layer, layer * float(saturate(self.p["TrackOutsideLayerRatio"]))

    def inside(self, layer, depth, blur):
        return layer * (1.0 - saturate(self.p["TrackDepthScale"] * depth)) + self.p["TrackBankHeightCm"] * np.maximum(blur - depth, 0.0)

    def plane_height(self, xy, taps):
        uv = self.uv(xy)
        layer, outside = self.layer()
        depth = self.depth(uv)
        blur = self.bank_blur(uv, taps)
        edge = self.edge_distance(xy)
        inside = self.inside(layer, depth, blur)
        return np.where(edge < self.p["TrackFloorClipInsetCm"], outside - self.p["TrackEdgeSinkCm"], outside + (inside - outside) * self.edge_blend(edge))

    def floor_visible(self, edge, uses_window_mask):
        return ~((uses_window_mask > 0.5) & (edge >= self.p["TrackFloorClipInsetCm"]))

    def pom_height(self, xy, blur_center, blend_center):
        layer, outside = self.layer()
        depth = self.depth(self.uv(xy))
        return outside + (self.inside(layer, depth, blur_center) - outside) * blend_center

    def pom(self, pixel_xy, raster_height, view, blur_center, blend_center):
        steps = float(np.clip(np.round(self.p["TrackPomMinSteps"] + (self.p["TrackPomMaxSteps"] - self.p["TrackPomMinSteps"]) * (1.0 - abs(view[2]))), 1.0, 64.0))
        half_span = self.p["TrackPomMaxDepthCm"] / max(abs(view[2]), 0.2)
        step = 2.0 * half_span / steps

        def gap(offset):
            return raster_height + view[2] * offset - float(self.pom_height(pixel_xy + view[:2] * offset, blur_center, blend_center))

        previous_offset = -half_span
        previous_gap = gap(previous_offset)
        hit = previous_offset if previous_gap <= 0.0 else 0.0
        for index in range(1, 65):
            if previous_gap <= 0.0 or index > steps:
                break
            offset = -half_span + step * index
            current_gap = gap(offset)
            if current_gap <= 0.0:
                hit = previous_offset + step * previous_gap / max(previous_gap - current_gap, 0.0001)
            previous_offset, previous_gap = offset, current_gap
        return hit, step, half_span, gap


def snow_parameters(material, entries):
    parameters = dict(material.LOOK_SCALAR_DEFAULTS)
    entry = entries.get(material.asset_path("MI_TD_FluidTrack_Snow"), {})
    parameters.update(entry.get("Scalars", {}))
    return parameters


def window_points(offsets_cm):
    return np.stack([WINDOW_CENTER[0] + np.asarray(offsets_cm, dtype=np.float64), np.full(len(offsets_cm), WINDOW_CENTER[1])], axis=-1)


def test_cell_match():
    rng = np.random.default_rng(20261025)
    center = np.array(WINDOW_CENTER, dtype=np.float32)
    extent = np.float32(WINDOW_EXTENT_CM)
    points = (center + (rng.random((20000, 2)) - 0.5).astype(np.float32) * extent * np.float32(0.999)).astype(np.float32)
    shader_uv = (points - center) / extent + np.float32(0.5)
    cpp_uv = (points - (center - np.float32(0.5) * extent)) / extent
    shader_cell = np.floor(shader_uv * GRID_RESOLUTION)
    cpp_cell = np.floor(cpp_uv * GRID_RESOLUTION)
    worst = float(np.abs(shader_cell - cpp_cell).max())
    return worst <= 1.0, f"셰이더·C++ PlanePointToGridUV 칸 차 최대 {worst}", np.concatenate([shader_uv, cpp_uv], axis=1)


def test_groove_floor(parameters):
    mirror = TrackMirror(parameters, np.ones((GRID_RESOLUTION, GRID_RESOLUTION)))
    points = window_points([0.0, 300.0, -450.0])
    results = np.array([mirror.plane_height(points, taps) for taps in (4, 8)])
    worst = float(np.abs(results).max())
    return worst <= 0.01, f"넓은 자국 r=1 홈 바닥 |H| 최대 {worst:.2e} cm(층 {parameters['TrackLayerThicknessCm']})", results


def groove_field(half_width_cm, soft_cm):
    cell = WINDOW_EXTENT_CM / GRID_RESOLUTION
    texel_x = (np.arange(GRID_RESOLUTION) + 0.5) * cell - 0.5 * WINDOW_EXTENT_CM
    profile = 1.0 - smoothstep(half_width_cm, half_width_cm + soft_cm, np.abs(texel_x))
    return np.repeat(profile[:, None], GRID_RESOLUTION, axis=1)


def test_bank_symmetry(parameters):
    mirror = TrackMirror(parameters, groove_field(18.0, 12.0))
    offsets = np.linspace(0.0, 200.0, 401)
    asymmetry = 0.0
    peak = -1.0
    profiles = []
    for taps in (4, 8):
        right = mirror.plane_height(window_points(offsets), taps)
        left = mirror.plane_height(window_points(-offsets), taps)
        asymmetry = max(asymmetry, float(np.abs(right - left).max()))
        peak = max(peak, float(right.max()))
        profiles.append(right)
    layer = parameters["TrackLayerThicknessCm"]
    passed = asymmetry <= 1e-5 and peak > layer
    return passed, f"대칭 단면 비대칭 최대 {asymmetry:.2e} cm, 둑 정점 {peak:.3f} > 층 {layer}", np.array(profiles)


def test_seam(parameters):
    outcomes = []
    for ratio in (1.0, 0.5):
        local = dict(parameters, TrackOutsideLayerRatio=ratio)
        mirror = TrackMirror(local, np.zeros((GRID_RESOLUTION, GRID_RESOLUTION)))
        inset = local["TrackFloorClipInsetCm"]
        _, outside = mirror.layer()
        just_inside = window_points([0.5 * WINDOW_EXTENT_CM - (inset + 0.01)])
        just_outside = window_points([0.5 * WINDOW_EXTENT_CM - (inset - 0.01), 0.5 * WINDOW_EXTENT_CM - 0.5 * inset])
        seam_gap = float(np.abs(mirror.plane_height(just_inside, 8) - outside).max())
        under = bool(np.all(mirror.plane_height(just_outside, 8) < outside))
        outcomes.append((seam_gap, under))
    passed = all(gap <= 0.1 and under for gap, under in outcomes)
    return passed, "; ".join(f"비 {ratio}: e=Inset+ε 평면−바닥 {gap:.2e} cm, e<Inset 평면<바닥 {under}" for ratio, (gap, under) in zip((1.0, 0.5), outcomes)), np.array(outcomes, dtype=np.float64)


def test_coverage(parameters):
    mirror = TrackMirror(parameters, groove_field(18.0, 12.0))
    edges = np.linspace(-300.0, 1000.0, 2601)
    points = window_points(0.5 * WINDOW_EXTENT_CM - edges)
    _, outside = mirror.layer()
    floor_height = np.full(len(edges), outside)
    floor_visible = mirror.floor_visible(edges, 1.0)
    plane_exists = edges >= 0.0
    plane_height = mirror.plane_height(points, 8)
    surfaces = np.stack([np.where(floor_visible, floor_height, -np.inf), np.where(plane_exists, plane_height, -np.inf)])
    top = surfaces.max(axis=0)
    at_top = (np.abs(surfaces - top) <= 1e-3).sum(axis=0)
    passed = bool(np.all(np.isfinite(top)) and np.all(at_top == 1))
    return passed, f"보이는 면 수 최소 {int(at_top.min())} 최대 {int(at_top.max())}, 빈 곳 {int((~np.isfinite(top)).sum())}", top


def step_field():
    cell = WINDOW_EXTENT_CM / GRID_RESOLUTION
    texel_x = (np.arange(GRID_RESOLUTION) + 0.5) * cell - 0.5 * WINDOW_EXTENT_CM
    profile = ((texel_x >= 0.0) & (texel_x < 60.0)).astype(np.float64)
    return np.repeat(profile[:, None], GRID_RESOLUTION, axis=1)


def find_crossing(function, start, end, increment):
    offsets = np.arange(start, end + increment, increment)
    values = np.array([function(offset) for offset in offsets])
    below = np.nonzero(values <= 0.0)[0]
    if len(below) == 0:
        return None
    index = below[0]
    if index == 0:
        return float(offsets[0])
    return float(offsets[index - 1] + increment * values[index - 1] / (values[index - 1] - values[index]))


def test_residual_pom(parameters):
    mirror = TrackMirror(parameters, step_field())
    vertex_spacing = WINDOW_EXTENT_CM / GRID_RESOLUTION
    vertex_offsets = np.arange(-40, 41) * vertex_spacing
    vertex_heights = mirror.plane_height(window_points(vertex_offsets), 4)
    errors, depth_offsets, records = [], [], []
    for pitch_degrees in (25.0, 40.0, 60.0, 80.0):
        for direction in (1.0, -1.0):
            for aim_cm in (-20.0, 10.0, 30.0, 55.0, 80.0):
                pitch = np.radians(pitch_degrees)
                view = np.array([direction * np.cos(pitch), 0.0, -np.sin(pitch)])

                def raster_gap(distance):
                    point = np.array([aim_cm - 2000.0 * view[0], 0.0, -2000.0 * view[2]]) + view * distance
                    return point[2] - np.interp(point[0], vertex_offsets, vertex_heights)

                distance = find_crossing(raster_gap, 1500.0, 2500.0, 0.01)
                if distance is None:
                    continue
                raster_point = np.array([aim_cm - 2000.0 * view[0], 0.0, -2000.0 * view[2]]) + view * distance
                pixel_xy = np.array([WINDOW_CENTER[0] + raster_point[0], WINDOW_CENTER[1]])
                uv = mirror.uv(pixel_xy)
                blur_center = float(mirror.bank_blur(uv, 8))
                blend_center = float(mirror.edge_blend(mirror.edge_distance(pixel_xy)))
                hit, step, half_span, gap = mirror.pom(pixel_xy, raster_point[2], view, blur_center, blend_center)
                truth = find_crossing(gap, -half_span, half_span, 0.005)
                if truth is None or truth <= -half_span + 1e-9:
                    continue
                errors.append(abs(hit - truth) / step)
                depth_offsets.append(max(hit, 0.0))
                records.append((pitch_degrees, direction, aim_cm, hit, truth, step))
    worst = float(max(errors)) if errors else float("inf")
    passed = bool(errors) and worst <= 1.0 and min(depth_offsets) >= 0.0
    return passed, f"광선 {len(errors)}개, 맞은 점 오차 최대 {worst:.3f} 단계, PDO 최소 {min(depth_offsets) if depth_offsets else 'n/a'}", np.array(records, dtype=np.float64)


def run_mirror_tests(parameters):
    outcomes = {}
    digest = hashlib.sha256()
    for label, test in (
        ("T1 칸 일치", test_cell_match),
        ("T2 홈 바닥", lambda: test_groove_floor(parameters)),
        ("T3 둑 대칭", lambda: test_bank_symmetry(parameters)),
        ("T4 이음매", lambda: test_seam(parameters)),
        ("T5 덮임", lambda: test_coverage(parameters)),
        ("T6 잔차 POM", lambda: test_residual_pom(parameters)),
    ):
        passed, detail, values = test()
        values = np.ascontiguousarray(values)
        digest.update(label.encode("utf-8"))
        digest.update(values.tobytes())
        outcomes[label] = (passed, detail, hashlib.sha256(values.tobytes()).hexdigest())
    return outcomes, digest.hexdigest()


def run_shader_text_checks(root, names):
    failures = []
    for name in names:
        text = (root / "Tools" / "FluidFX" / "Shaders" / name).read_text(encoding="utf-8")
        if "//" in text or "/*" in text:
            failures.append(f"{name}: 주석이 있음")
        if ABSOLUTE_PATH_PATTERN.search(text):
            failures.append(f"{name}: 절대 경로가 있음")
        if not text.isascii():
            failures.append(f"{name}: ASCII 가 아닌 문자가 있음")
        if "\r" in text:
            failures.append(f"{name}: CRLF 줄바꿈")
    return failures


def collect_look_entries(root, fragment_path):
    entries = {}
    canonical = root / "Content" / "FluidFX" / "PresetData" / "TDFluidMaterialLook.json"
    if canonical.is_file():
        entries.update(json.loads(canonical.read_text(encoding="utf-8")).get("Materials", {}))
    overlap = []
    if fragment_path:
        fragment = json.loads(pathlib.Path(fragment_path).read_text(encoding="utf-8")).get("Materials", {})
        overlap = sorted(set(fragment) & set(entries))
        entries.update(fragment)
    return entries, overlap


def naive_entries(sources):
    return {
        "offset": "#define TD_TRACK_VERTEX 1\n" + sources["common"] + "\n" + sources["offset"],
        "surface": sources["common"] + "\n" + sources["surface"],
        "sample": sources["common"] + "\n" + sources["sample"],
    }


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
    material = load_script(root, "editor_make_fluid_track_material")
    record("재질 스크립트 unreal 스텁 임포트", True)
    stage_path = root / "Tools" / "FluidFX" / "editor_make_fluid_track_stage.py"
    if stage_path.is_file():
        stage = load_script(root, "editor_make_fluid_track_stage")
        layout_problems = stage.validate_stage_layout()
        record("무대 배치(페인터 Z = 충돌면, 비탈은 창 밖·오르막·클립 끔 MI, 충돌 바닥, MPC 쓰기 1개, 굴림 궤도 창 안)", not layout_problems, "; ".join(layout_problems))
    else:
        record("무대 스크립트 unreal 스텁 임포트", False, "editor_make_fluid_track_stage.py 없음")

    shader_names = (material.COMMON_HLSL_NAME, material.OFFSET_HLSL_NAME, material.SURFACE_HLSL_NAME, material.SAMPLE_HLSL_NAME)
    text_failures = run_shader_text_checks(root, shader_names)
    record("셰이더 텍스트(주석·절대 경로·ASCII·LF)", not text_failures, "; ".join(text_failures))
    sources = material.read_shader_sources(str(root))
    codes = material.assemble_all_node_codes(sources)
    problems = material.validate_all_node_codes(codes) + [f"입력 원천 없음: {name}" for name in material.find_unresolved_inputs()]
    record("조립 코드 자체 점검", not problems, "; ".join(problems))
    joined = normalize("\n".join(sources.values()))
    missing_anchors = [anchor for anchor in REQUIRED_ANCHORS if normalize(anchor) not in joined]
    forbidden = [token for token in FORBIDDEN_TOKENS if token in "\n".join(sources.values())]
    record("식 고정점(창 UV·층·이음매·클립·POM·PDO)·금지 토큰(표면 MID z=0 이라 SimCenterWorld.z 금지)", not missing_anchors and not forbidden, "; ".join(missing_anchors + forbidden))

    entries, overlap = collect_look_entries(root, arguments.look_fragment)
    override_problems = material.find_instance_override_problems(entries)
    record("MI 이름 계약(부모·마스터 이름·C++ 구동·조건부 이름 없음·먼지 = 땅먼지 값 + 색)", not override_problems, "; ".join(override_problems))
    record("조각과 정본 키 서로소", not overlap, ", ".join(overlap))

    parameters = snow_parameters(material, entries)
    outcomes_first, digest_first = run_mirror_tests(parameters)
    _, digest_second = run_mirror_tests(parameters)
    for label, (passed, detail, value_sha) in outcomes_first.items():
        record(f"거울 {label}", passed, f"{detail} sha256 {value_sha[:16]}")
    record("거울 시험 2회 sha256 같음", digest_first == digest_second, digest_first)
    summary["mirror_sha256"] = digest_first

    dxc = find_dxc(arguments.dxc)
    if not dxc:
        record("dxc 찾기", False, "Windows Kits dxc.exe 없음(--dxc 로 지정)")
        return finish(results, summary, arguments.output)
    record("dxc 찾기", True, dxc)
    declarations = declare_inputs(material)
    counts = {}
    with tempfile.TemporaryDirectory() as folder:
        work_folder = pathlib.Path(folder)
        for optimize in (False, True):
            suffix = "O3" if optimize else "Od"
            outcomes = {
                "surface": compile_source(dxc, build_source(declarations, codes, ("surface",)), f"surface_{suffix}", "ps_6_0", optimize, work_folder),
                "sample": compile_source(dxc, build_source(declarations, codes, ("sample",)), f"sample_{suffix}", "ps_6_0", optimize, work_folder),
                "offset": compile_source(dxc, build_source(declarations, codes, ("offset",)), f"offset_ps_{suffix}", "ps_6_0", optimize, work_folder),
                "vertex": compile_source(dxc, build_vertex_source(declarations, codes), f"offset_vs_{suffix}", "vs_6_0", optimize, work_folder),
            }
            for label, outcome in outcomes.items():
                record(f"dxc {label} {suffix}", outcome["exit"] == 0, outcome["error"] or f"표본 {outcome['samples']} 명령 {outcome['instructions']}")
            counts[suffix] = {label: {"samples": item["samples"], "instructions": item["instructions"]} for label, item in outcomes.items()}
        od = counts["Od"]
        record("오프셋 ps 와 vs 표본 수 같음", od["offset"]["samples"] == od["vertex"]["samples"] > 0, f"{od['offset']['samples']} / {od['vertex']['samples']}")
        record("표면 표본 수 > 오프셋(둑 고리 8 대 4, POM·법선)", od["surface"]["samples"] > od["offset"]["samples"], f"표면 {od['surface']['samples']} 오프셋 {od['offset']['samples']}")
        expected_total = od["surface"]["samples"] + od["offset"]["samples"]
        for order in (("offset", "surface"), ("surface", "offset")):
            combined = compile_source(dxc, build_source(declarations, codes, order), "combined_" + "_".join(order), "ps_6_0", False, work_folder)
            record(f"매크로 새지 않음({'→'.join(order)})", combined["exit"] == 0 and combined["samples"] == expected_total, combined["error"] or f"합 {combined['samples']} 기대 {expected_total}")
        control = compile_source(dxc, build_source(declarations, naive_entries(sources), ("offset", "surface")), "control_naive", "ps_6_0", False, work_folder)
        record("음성 대조: 매크로를 지우지 않는 조립은 누수를 검출", control["samples"] != expected_total, f"합 {control['samples']} 기대 {expected_total} 종료 {control['exit']}")
    summary["dxc"] = counts
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
