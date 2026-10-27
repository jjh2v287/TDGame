"""실행 환경: 시스템 Python 3(numpy) 와 Windows Kits dxc.exe(ps_6_0·vs_6_0), 에디터 불필요
실행 명령: python Tools/FluidFX/check_fluid_cloud_hlsl.py [--root 프로젝트경로] [--dxc dxc경로] [--look-fragment 조각.json] [--output 요약.json]
출력: 항목별 PASS/FAIL 줄과 마지막 줄 JSON 요약(dxc 명령·표본 수, 거울 시험 sha256), 하나라도 실패하면 종료 코드 1
상태: 미검증(2026-10-26 FX-11r 작성). TDFluidCloud{Common,Slot,Combine}.hlsl 을 재질 스크립트와 같은 조립 함수로 조립해 텍스트·식 고정점(단면 CS(u, v) FX-9 규약 M5, 카메라 페이드 km→cm S9①)·이름 계약(MI 덮어쓰기·벡터 색 M3·지형 추종 MI 와 지형 MI 일치 M4, 정본에 아직 없으면 --look-fragment)·무대 배치·dxc O3/Od 와 Source×Shape 접기 순열·합성·지형(ps·vs) 컴파일을 보고, numpy 거울 C1 꺼짐 0·C2 보수 밀도 ≥ 밀도(MI 18 × 10만)·C3 합성 가산·C4 방사 회전 불변·C5 발자국 마스크 단조·C6 지형 추종 불변·C7 판 = C++ 시험 사례를 두 번 돌려 sha256 이 같은지 본다.
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
    "TD_CLOUD_SECTION(float2(cloudRadialU, cloudV))",
    "TD_CLOUD_SECTION(float2(cloudLinearU, cloudV))",
    "TD_CLOUD_SECTION(float2(cloudCoverageRaw, cloudV))",
    "float cloudFadeStartCm = CloudCameraFadeStartKm * TD_CLOUD_KM;",
    "float cloudFadeEndCm = CloudCameraFadeEndKm * TD_CLOUD_KM;",
    "if (SlotSource < 0.5)",
    "max(cloudCoverageTap.r, cloudCoverageTap.b)",
    "CloudAlbedo = combineSigma > 1e-8 ? combineScatter / combineSigma : float3(1.0, 1.0, 1.0);",
)
FORBIDDEN_TOKENS = ("1.0 - cloudV", "1 - cloudV", "normalize(", "Texture2DSample(", "Texture3DSample(")
TEXTURE2D_INPUTS = ("SlotCoverageTexture", "SlotSectionTexture", "TerrainHeightTexture")
FLOAT4_INPUTS = ("CloudTerrainFootprintKm", "TerrainFootprintKm")
FLOAT3_INPUTS = ("CloudWorldPosition", "CloudCameraPosition", "CloudTerrainHeightRangeKm", "TerrainWorldPosition", "TerrainHeightRangeKm")
KM = 100000.0
MIRROR_SAMPLE_COUNT = 100000
NOISE_SIZE = 64
SECTION_SIZE = 64
SLAB_CASES = (
    ([(True, 1.5, 3.0, 0.0, 0.0)], (0.0, 0.0), (1.5, 3.0)),
    ([(True, 0.2, 0.3, 0.0, 0.0), (True, 2.0, 4.0, 0.0, 0.0)], (0.0, 0.0), (0.2, 5.8)),
    ([(False, 0.1, 0.1, 0.0, 0.0), (True, 2.0, 1.0, 0.0, 0.0)], (0.0, 0.0), (2.0, 1.0)),
    ([(True, 0.02, 0.3, 1.0, 0.0)], (0.1, 2.5), (0.12, 2.70)),
    ([(True, 1.0, 2.0, 0.0, 0.3)], (0.0, 0.0), (0.4, 2.6)),
    ([], (0.0, 0.0), (0.0, 0.0)),
    ([(True, 1.0, 0.01, 0.0, 0.0)], (0.0, 0.0), (1.0, 0.1)),
)


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


def declare_inputs(input_names, folded):
    lines = []
    scalars = []
    for name in input_names:
        if name in folded:
            continue
        if name in TEXTURE2D_INPUTS:
            lines += [f"Texture2D {name};", f"SamplerState {name}Sampler;"]
        elif name == "NoiseVolumeTexture":
            lines += ["Texture3D NoiseVolumeTexture;", "SamplerState NoiseVolumeTextureSampler;"]
        elif name in FLOAT4_INPUTS or name.endswith("Main"):
            scalars.append(f"float4 {name};")
        elif name in FLOAT3_INPUTS or name.endswith(("ScatterAlbedo", "Emissive", "Km", "Color", "Albedo", "PerSec")) and not name.endswith(("BottomKm", "ThicknessKm", "PatternScaleKm", "DetailNoiseScaleKm", "SectionDepthKm", "FadeStartKm", "FadeEndKm")):
            scalars.append(f"float3 {name};")
        else:
            scalars.append(f"float {name};")
    constants = "".join(f"static const float {name} = {value};\n" for name, value in folded.items())
    return "\n".join(lines) + "\n" + constants + "cbuffer Inputs {\n" + "\n".join(scalars) + "\n}\n"


def build_pixel_source(input_names, outputs, code, folded, function_name):
    parameters = ", ".join(f"inout float3 {name}" for name in outputs)
    arguments = ", ".join(f"out_{name}" for name in outputs)
    locals_block = "".join(f"float3 out_{name} = float3(0.0, 0.0, 0.0);\n" for name in outputs)
    sums = "".join(f" + float4(out_{name}, 0.0)" for name in outputs)
    return (HEADER + declare_inputs(input_names, folded) + f"float4 {function_name}({parameters})\n{{\n{code}\n}}\n"
            + "float4 main(float4 sv : SV_Position) : SV_Target\n{\n" + locals_block + f"float4 result = {function_name}({arguments});\nreturn result{sums};\n}}\n")


def build_vertex_source(input_names, code):
    return HEADER + declare_inputs(input_names, {}) + f"float4 TDCloudTerrain()\n{{\n{code}\n}}\n" + "float4 main(float4 position : POSITION) : SV_Position\n{\nreturn position + TDCloudTerrain();\n}\n"


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
    t = np.clip((x - edge0) / np.where(edge1 - edge0 == 0.0, 1e-12, edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def safe_pow(x, e):
    return np.power(np.maximum(x, 0.0), max(e, 0.01))


class TDCloudMirror:
    def __init__(self, seed):
        rng = np.random.default_rng(seed)
        self.noise = rng.random((NOISE_SIZE, NOISE_SIZE, NOISE_SIZE))
        u, v = np.meshgrid((np.arange(SECTION_SIZE) + 0.5) / SECTION_SIZE, (np.arange(SECTION_SIZE) + 0.5) / SECTION_SIZE, indexing="xy")
        self.section = np.clip(1.0 - u - 0.3 * v + 0.2 * rng.random(u.shape), 0.0, 1.0)
        self.coverage = rng.random((SECTION_SIZE, SECTION_SIZE, 3))
        self.terrain = smoothstep(0.0, 1.0, rng.random((SECTION_SIZE, SECTION_SIZE)))
        self.terrain_scale = 1.0

    @staticmethod
    def fetch2d(texture, uv, wrap):
        size = texture.shape[0]
        coordinates = np.floor(np.mod(uv, 1.0) * size if wrap else np.clip(uv, 0.0, 1.0) * size).astype(int)
        coordinates = np.clip(coordinates, 0, size - 1)
        return texture[coordinates[..., 1], coordinates[..., 0]]

    def noise3(self, uvw):
        index = np.floor(np.mod(uvw, 1.0) * NOISE_SIZE).astype(int) % NOISE_SIZE
        return self.noise[index[..., 2], index[..., 1], index[..., 0]]

    def slot(self, p, altitude_cm, camera, elapsed, params, globals_):
        n = p.shape[0]
        zero = np.zeros(n)
        if params["Source"] < 0.5:
            return zero, zero, zero, np.zeros((n, 3)), np.zeros((n, 3))
        shape = round(params["Shape"])
        pattern = round(params["Pattern"])
        center = np.array(params["FootprintCenterKm"][:2]) * KM
        size = np.maximum(np.array(params["FootprintSizeKm"][:2]), 0.0) * KM
        pattern_scale = max(params["PatternScaleKm"] * KM, 1.0)
        has_footprint = params["FootprintSizeKm"][0] >= 1e-6
        offset = p[:, :2] - center
        uv = offset / np.maximum(size, 1.0) + 0.5 if has_footprint else np.mod(p[:, :2] / pattern_scale, 1.0)
        yaw = np.radians(params["SectionYawDegrees"])
        direction = np.array([np.cos(yaw), np.sin(yaw)])
        linear_u = offset @ direction / max(size[0], 1.0) + 0.5
        edge = np.minimum(0.5 - np.abs(uv[:, 0] - 0.5), 0.5 - np.abs(uv[:, 1] - 0.5))
        if shape == 2:
            edge = np.minimum(linear_u, 1.0 - linear_u)
        ramp = safe_pow(np.clip(edge / max(0.5 * params["EdgeMaskWidthRatio"], 1e-4), 0.0, 1.0), params["EdgeMaskPower"])
        edge_mask = np.where(edge <= 0.0, 0.0, ramp) if has_footprint else np.ones(n)
        flow = np.array([params["WindKmPerSec"][0], params["WindKmPerSec"][1], 0.0]) * KM * elapsed
        lobe_cell = np.mod(p[:, :2] / pattern_scale, 1.0) - 0.5
        lobe = np.sqrt(np.clip(1.0 - 4.0 * np.sum(lobe_cell * lobe_cell, axis=1), 0.0, 1.0))
        if params["Source"] < 1.5:
            tap = self.fetch2d(self.coverage, np.clip(uv, 0.0, 1.0) if has_footprint else uv, not has_footprint)
            raw = np.maximum(tap[:, 0], tap[:, 2])
        elif params["Source"] < 2.5:
            q = (p[:, :2] - flow[:2]) / pattern_scale
            n2 = (self.noise3(np.column_stack([q, np.full(n, 0.37)])) + 0.5 * self.noise3(np.column_stack([q * 2.03, np.full(n, 0.37)]))) / 1.5
            knob = np.clip(params["PatternCoverage"], 0.0, 1.0)
            stratus = smoothstep(1.0 - knob - 0.15, 1.0 - knob + 0.15, n2)
            nimbus = np.clip(knob + 0.35 * (n2 - 0.5), 0.0, 1.0)
            q4 = q * 4.0
            fine = (self.noise3(np.column_stack([q4, np.full(n, 0.37)])) + 0.5 * self.noise3(np.column_stack([q4 * 2.03, np.full(n, 0.37)]))) / 1.5
            cumulus = smoothstep(1.0 - knob - 0.08, 1.0 - knob + 0.08, n2) * (0.6 + 0.4 * fine)
            raw = stratus if pattern in (0, 2) else (nimbus if pattern == 1 else cumulus)
        else:
            raw = np.ones(n)
        threshold = params["CoverageThreshold"]
        coverage = np.clip((raw * params["CoverageScale"] - threshold) / max(1.0 - threshold, 1e-3), 0.0, 1.0)
        terrain_cm = np.zeros(n)
        footprint = globals_["CloudTerrainFootprintKm"]
        height_range = globals_["CloudTerrainHeightRangeKm"]
        if params["TerrainFollow"] > 0.0 and footprint[2] >= 1e-6:
            terrain_uv = (p[:, :2] - np.array(footprint[:2]) * KM) / np.maximum(np.array(footprint[2:4]) * KM, 1.0)
            inside = np.all((terrain_uv >= 0.0) & (terrain_uv <= 1.0), axis=1)
            height = self.fetch2d(self.terrain, np.clip(terrain_uv, 0.0, 1.0), False) * self.terrain_scale * inside
            terrain_cm = KM * (height_range[0] + (height_range[1] - height_range[0]) * height)
        thickness = max(params["ThicknessKm"], 0.0) * KM
        mammatus = 1.0 if pattern == 2 else 0.0
        bottom = params["BottomKm"] * KM + params["TerrainFollow"] * terrain_cm - mammatus * params["LobeDepthRatio"] * thickness * lobe
        height_cm = altitude_cm - bottom
        v = height_cm / max(thickness, 1.0)
        in_band = ((height_cm >= 0.0) & (height_cm <= thickness)).astype(float)
        if shape == 0:
            top = thickness * safe_pow(coverage, params["TopExponent"])
            rise = smoothstep(0.0, max(params["BottomFadeRatio"] * thickness, 1.0), height_cm)
            cap = 1.0 - smoothstep(top - max(params["TopFadeRatio"] * thickness, 1.0), top, height_cm)
            density = np.where(top > 0.0, coverage * rise * cap * (height_cm >= 0.0), 0.0)
        elif shape == 1:
            radial_u = safe_pow(np.clip(np.linalg.norm(offset, axis=1) / max(0.5 * size[0], 1.0), 0.0, 1.0), params["RadialUExponent"])
            density = coverage * self.fetch2d(self.section, np.clip(np.column_stack([radial_u, v]), 0.0, 1.0), False) * in_band
        elif shape == 2:
            across = np.abs(offset @ np.array([-direction[1], direction[0]]))
            depth_fade = 1.0 - smoothstep(0.0, max(0.5 * params["SectionDepthKm"] * KM, 1.0), across)
            in_u = ((linear_u >= 0.0) & (linear_u <= 1.0)).astype(float)
            density = coverage * self.fetch2d(self.section, np.clip(np.column_stack([linear_u, v]), 0.0, 1.0), False) * depth_fade * in_band * in_u
        else:
            density = self.fetch2d(self.section, np.clip(np.column_stack([raw, v]), 0.0, 1.0), False) * in_band * (raw > threshold)
        detail = (p - flow) / max(params["DetailNoiseScaleKm"] * KM, 1.0)
        first = np.clip(density - params["DetailErosion"] * (1.0 - (0.65 * self.noise3(detail) + 0.35 * self.noise3(detail * 2.03))) * (1.0 - density), 0.0, 1.0)
        eroded = np.where(density > 0.0, np.clip(first - params["EdgeErosion"] * (1.0 - self.noise3(detail * 4.1)) * (1.0 - smoothstep(0.0, 0.35, first)), 0.0, 1.0), 0.0)
        start, end = globals_["CloudCameraFadeStartKm"] * KM, globals_["CloudCameraFadeEndKm"] * KM
        camera_fade = smoothstep(start, end, np.linalg.norm(p - camera, axis=1)) if end > start else np.ones(n)
        weight = edge_mask * camera_fade
        sigma = max(params["ExtinctionPerM"], 0.0) * eroded * weight
        scatter = sigma[:, None] * np.clip(np.array(params["Albedo"][:3]), 0.0, 1.0)
        emissive = np.maximum(np.array(params["EmissiveColor"][:3]), 0.0) * max(params["EmissiveIntensity"], 0.0) * (safe_pow(eroded, params["EmissiveExponent"]) * weight)[:, None]
        conservative = (np.where(raw > threshold, 1.0, 0.0) if shape == 3 else coverage) * edge_mask * in_band
        return sigma, conservative, eroded * weight, scatter, emissive


def master_parameters(material, entry, slot):
    params = {name: value for name, value in material.SLOT_SCALAR_DEFAULTS}
    params.update({name: list(rgba) for name, rgba in material.SLOT_VECTOR_DEFAULTS})
    for name in list(params):
        key = f"CloudSlot{slot}{name}"
        if key in entry.get("Scalars", {}):
            params[name] = entry["Scalars"][key]
        if key in entry.get("Vectors", {}):
            params[name] = entry["Vectors"][key]
    return params


def master_globals(material, entry):
    values = {name: value for name, value in material.GLOBAL_SCALAR_DEFAULTS}
    values.update({name: list(rgba) for name, rgba in material.GLOBAL_VECTOR_DEFAULTS})
    values.update({name: value for name, value in entry.get("Scalars", {}).items() if name in values})
    values.update({name: value for name, value in entry.get("Vectors", {}).items() if name in values})
    return values


def random_points(rng, count, half_extent_km, top_km):
    xy = (rng.random((count, 2)) * 2.0 - 1.0) * half_extent_km * KM
    altitude = rng.random(count) * top_km * KM
    return np.column_stack([xy, altitude]), altitude


def test_source_off(material, mirror):
    rng = np.random.default_rng(1)
    params = master_parameters(material, {}, 0)
    points, altitude = random_points(rng, 4096, 30.0, 12.0)
    sigma, conservative, _, scatter, emissive = mirror.slot(points, altitude, np.zeros(3), 3.0, params, master_globals(material, {}))
    total = float(np.abs(sigma).max() + np.abs(conservative).max() + np.abs(scatter).max() + np.abs(emissive).max())
    return total == 0.0, f"Source 0 출력 합 {total}", np.stack([sigma, conservative])


def test_conservative_bound(material, mirror, entries):
    rng = np.random.default_rng(2)
    worst = 0.0
    checked = 0
    values = []
    for name in material.INSTANCE_NAMES:
        entry = entries.get(material.asset_path(name), {})
        globals_ = master_globals(material, entry)
        for slot in range(material.SLOT_COUNT):
            params = master_parameters(material, entry, slot)
            if params["Source"] < 0.5:
                continue
            top = max(params["BottomKm"] + params["ThicknessKm"] + 3.0, 1.0)
            points, altitude = random_points(rng, MIRROR_SAMPLE_COUNT // 10, 45.0, top)
            camera = np.array([0.0, 0.0, 100.0])
            _, conservative, weighted, _, _ = mirror.slot(points, altitude, camera, 7.0, params, globals_)
            worst = max(worst, float((weighted - conservative).max()))
            checked += points.shape[0]
            values.append(np.round(conservative - weighted, 9))
    joined = np.concatenate(values) if values else np.zeros(1)
    return worst <= 1e-9 and checked >= MIRROR_SAMPLE_COUNT, f"표본 {checked}, 최대 (밀도 − 보수) {worst:.3e}", joined


def combine(sigmas, albedos, emissives):
    sigma = np.sum(sigmas, axis=0)
    scatter = np.sum(sigmas[..., None] * albedos, axis=0)
    albedo = np.where(sigma[:, None] > 1e-8, scatter / np.maximum(sigma, 1e-30)[:, None], 1.0)
    return sigma, albedo, np.sum(emissives, axis=0)


def test_combine(_material, _mirror):
    rng = np.random.default_rng(3)
    sigmas = rng.random((4, 4096)) * 0.05
    sigmas[:, :16] = 0.0
    albedos = rng.random((4, 4096, 3))
    emissives = rng.random((4, 4096, 3))
    sigma, albedo, emissive = combine(sigmas, albedos, emissives)
    additive = float(np.abs(sigma - sigmas.sum(axis=0)).max())
    weighted = float(np.abs(albedo * sigma[:, None] - np.sum(sigmas[..., None] * albedos, axis=0)).max())
    empty = float(np.abs(albedo[:16] - 1.0).max())
    emission = float(np.abs(emissive - emissives.sum(axis=0)).max())
    return max(additive, weighted, empty, emission) <= 1e-6, f"가산 {additive:.1e}, 알베도 가중 {weighted:.1e}, 빈 곳 {empty}, 발광 {emission:.1e}", np.column_stack([sigma, albedo])


def radial_parameters(material):
    params = master_parameters(material, {}, 1)
    params.update({"Source": 3.0, "Shape": 1.0, "FootprintSizeKm": [8.0, 8.0, 0.0, 0.0], "FootprintCenterKm": [3.0, -2.0, 0.0, 0.0], "BottomKm": 0.5, "ThicknessKm": 6.0, "DetailErosion": 0.0, "EdgeErosion": 0.0, "RadialUExponent": 1.5})
    return params


def test_radial_rotation(material, mirror):
    rng = np.random.default_rng(4)
    params = radial_parameters(material)
    center = np.array(params["FootprintCenterKm"][:2]) * KM
    radius = rng.random(8192) * 0.37 * params["FootprintSizeKm"][0] * KM
    angle = rng.random(8192) * 2.0 * np.pi
    altitude = (0.5 + rng.random(8192) * 6.0) * KM
    turn = rng.random(8192) * 2.0 * np.pi
    first = np.column_stack([center + np.column_stack([np.cos(angle), np.sin(angle)]) * radius[:, None], altitude])
    second = np.column_stack([center + np.column_stack([np.cos(angle + turn), np.sin(angle + turn)]) * radius[:, None], altitude])
    globals_ = master_globals(material, {})
    a = mirror.slot(first, altitude, np.zeros(3), 0.0, params, globals_)[2]
    b = mirror.slot(second, altitude, np.zeros(3), 0.0, params, globals_)[2]
    error = float(np.abs(a - b).max())
    return error <= 1e-6 and float(a.max()) > 0.0, f"회전 차 {error:.2e}, 최대 밀도 {float(a.max()):.3f}", a


def test_footprint_mask(material, mirror):
    params = master_parameters(material, {}, 1)
    params.update({"Source": 3.0, "FootprintSizeKm": [10.0, 10.0, 0.0, 0.0], "DetailErosion": 0.0, "EdgeErosion": 0.0, "ThicknessKm": 4.0, "BottomKm": 1.0})
    distance = np.linspace(0.0, 9.0, 4001) * KM
    points = np.column_stack([distance, np.zeros_like(distance), np.full_like(distance, 2.0 * KM)])
    mask = mirror.slot(points, points[:, 2], np.zeros(3), 0.0, params, master_globals(material, {}))[2]
    monotone = bool(np.all(np.diff(mask) <= 1e-12))
    outside = float(mask[distance > 5.0 * KM].max())
    return monotone and outside == 0.0 and float(mask[0]) > 0.0, f"단조 {monotone}, 밖 최대 {outside}, 중심 {float(mask[0]):.3f}", mask


def test_terrain_follow(material, mirror):
    params = master_parameters(material, {}, 1)
    params.update({"Source": 3.0, "TerrainFollow": 1.0, "BottomKm": 0.3, "ThicknessKm": 1.0, "DetailErosion": 0.0, "EdgeErosion": 0.0, "FootprintSizeKm": [0.0, 0.0, 0.0, 0.0]})
    globals_ = master_globals(material, {})
    globals_["CloudTerrainHeightRangeKm"] = [0.0, 2.5, 0.0, 0.0]
    rng = np.random.default_rng(6)
    xy = (rng.random((8192, 2)) * 2.0 - 1.0) * 19.0 * KM
    above = rng.random(8192) * 1.4 * KM
    results = []
    for scale in (1.0, 0.4):
        mirror.terrain_scale = scale
        terrain_uv = (xy - np.array(globals_["CloudTerrainFootprintKm"][:2]) * KM) / (np.array(globals_["CloudTerrainFootprintKm"][2:4]) * KM)
        ground = 2.5 * KM * mirror.fetch2d(mirror.terrain, np.clip(terrain_uv, 0.0, 1.0), False) * scale
        altitude = ground + 0.3 * KM + above
        points = np.column_stack([xy, altitude])
        results.append(mirror.slot(points, altitude, np.zeros(3), 0.0, params, globals_)[2])
    mirror.terrain_scale = 1.0
    error = float(np.abs(results[0] - results[1]).max())
    return error <= 1e-6 and float(results[0].max()) > 0.0, f"지형 배율 1·0.4 밀도 차 {error:.2e}", results[0]


def compute_slab(bands, terrain_range):
    active = [band for band in bands if band[0]]
    if not active:
        return 0.0, 0.0
    bottom = min(band[1] + band[3] * terrain_range[0] - band[4] * max(band[2], 0.0) for band in active)
    top = max(band[1] + max(band[2], 0.0) + band[3] * terrain_range[1] for band in active)
    slab_bottom = max(bottom, 0.0)
    return slab_bottom, max(top - slab_bottom, 0.1)


def test_slab_cases(_material, _mirror):
    worst = 0.0
    values = []
    for bands, terrain_range, expected in SLAB_CASES:
        slab = compute_slab(bands, terrain_range)
        worst = max(worst, abs(slab[0] - expected[0]), abs(slab[1] - expected[1]))
        values.extend(slab)
    return worst <= 1e-6, f"C++ 시험 사례 {len(SLAB_CASES)}개 최대 오차 {worst:.1e}", np.array(values)


def run_mirror_tests(material, entries):
    mirror = TDCloudMirror(20261026)
    tests = (
        ("C1 꺼짐 0", lambda: test_source_off(material, mirror)),
        ("C2 보수 밀도 ≥ 밀도", lambda: test_conservative_bound(material, mirror, entries)),
        ("C3 합성 가산·알베도 가중", lambda: test_combine(material, mirror)),
        ("C4 방사 회전 불변", lambda: test_radial_rotation(material, mirror)),
        ("C5 발자국 마스크 단조·밖 0", lambda: test_footprint_mask(material, mirror)),
        ("C6 지형 추종 불변", lambda: test_terrain_follow(material, mirror)),
        ("C7 판 = C++ 사례", lambda: test_slab_cases(material, mirror)),
    )
    outcomes = {}
    digest = hashlib.sha256()
    for label, test in tests:
        passed, detail, values = test()
        values = np.ascontiguousarray(np.asarray(values, dtype=np.float64))
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
    if not fragment_path:
        return entries, []
    fragment = json.loads(pathlib.Path(fragment_path).read_text(encoding="utf-8"))
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
    material = load_script(root, "editor_make_fluid_cloud_material")
    record("재질 스크립트 unreal 스텁 임포트", True)
    stage = load_script(root, "editor_make_fluid_cloud_stage")
    layout_problems = stage.validate_stage_layout()
    record("무대 배치(셋업 17·동적 순서·원천·40 km 축척)", not layout_problems, "; ".join(layout_problems))

    shader_root = root / "Tools" / "FluidFX" / "Shaders"
    text_failures = []
    for name in (material.COMMON_HLSL_NAME, material.SLOT_HLSL_NAME, material.COMBINE_HLSL_NAME):
        text_failures += [f"{name}: {failure}" for failure in run_shader_text_checks((shader_root / name).read_bytes().decode("utf-8"))]
    record("셰이더 텍스트(주석·절대 경로·ASCII·LF)", not text_failures, "; ".join(text_failures))
    codes = material.assemble_all_node_codes(material.read_shader_sources(str(root)))
    record("조립 코드 자체 점검(입력·출력·샘플러·매크로 접두)", not material.validate_all_codes(codes), "; ".join(material.validate_all_codes(codes)))
    normalized = normalize(codes["slot"] + codes["combine"])
    missing_anchors = [anchor for anchor in REQUIRED_ANCHORS if normalize(anchor) not in normalized]
    forbidden = [token for token in FORBIDDEN_TOKENS if token in codes["slot"] + codes["combine"] + codes["terrain"]]
    record("식 고정점(단면 CS(u, v)·카메라 페이드 km→cm·합성)·금지 토큰", not missing_anchors and not forbidden, "; ".join(missing_anchors + forbidden))
    vector_names = {name for name, _ in material.SLOT_VECTOR_DEFAULTS}
    record("색 매개변수 벡터(Albedo·EmissiveColor, M3)", {"Albedo", "EmissiveColor"} <= vector_names and len(material.SLOT_SCALAR_DEFAULTS) + len(material.SLOT_VECTOR_DEFAULTS) == 30, f"슬롯 이름 {len(material.SLOT_SCALAR_DEFAULTS) + len(material.SLOT_VECTOR_DEFAULTS)}")

    entries, overlap = collect_look_entries(root, arguments.look_fragment)
    override_problems = material.find_instance_override_problems(entries)
    record("MI 이름 계약(부모·마스터 이름·런타임 텍스처 이름 없음·벡터 이름)", not override_problems, "; ".join(override_problems))
    record("조각과 정본 키 서로소", not overlap or not arguments.look_fragment, ", ".join(overlap))
    terrain_problems = stage.find_terrain_contract_problems(entries)
    record("지형 추종 MI = 지형 MI 발자국·높이 범위(M4)", not terrain_problems, "; ".join(terrain_problems))

    outcomes_first, digest_first = run_mirror_tests(material, entries)
    outcomes_second, digest_second = run_mirror_tests(material, entries)
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
    slot_outputs = [name for name, _ in material.SLOT_OUTPUTS]
    combine_outputs = [name for name, _ in material.COMBINE_OUTPUTS]
    with tempfile.TemporaryDirectory() as folder:
        work_folder = pathlib.Path(folder)
        for optimize in (False, True):
            suffix = "O3" if optimize else "Od"
            outcome = compile_source(dxc, build_pixel_source(material.SLOT_INPUT_NAMES, slot_outputs, codes["slot"], {}, "TDCloudSlot"), f"slot_{suffix}", "ps_6_0", optimize, work_folder)
            record(f"dxc 슬롯 ps_6_0 {suffix}", outcome["exit"] == 0 and outcome["samples"] >= 4, outcome["error"] or f"표본 {outcome['samples']} 명령 {outcome['instructions']}")
            compiles[f"slot_{suffix}"] = outcome
        for source_value in (1.0, 2.0, 3.0):
            for shape_value in (0.0, 1.0, 2.0, 3.0):
                label = f"source{int(source_value)}_shape{int(shape_value)}"
                outcome = compile_source(dxc, build_pixel_source(material.SLOT_INPUT_NAMES, slot_outputs, codes["slot"], {"SlotSource": source_value, "SlotShape": shape_value}, "TDCloudSlot"), label, "ps_6_0", True, work_folder)
                record(f"dxc 순열 {label} O3", outcome["exit"] == 0, outcome["error"] or f"표본 {outcome['samples']} 명령 {outcome['instructions']}")
                compiles[label] = outcome
        outcome = compile_source(dxc, build_pixel_source(material.COMBINE_INPUT_NAMES, combine_outputs, codes["combine"], {}, "TDCloudCombine"), "combine_O3", "ps_6_0", True, work_folder)
        record("dxc 합성 ps_6_0 O3", outcome["exit"] == 0, outcome["error"] or f"명령 {outcome['instructions']}")
        compiles["combine_O3"] = outcome
        for profile in ("ps_6_0", "vs_6_0"):
            source = build_pixel_source(material.TERRAIN_INPUT_NAMES, [], codes["terrain"], {}, "TDCloudTerrain") if profile == "ps_6_0" else build_vertex_source(material.TERRAIN_INPUT_NAMES, codes["terrain"])
            outcome = compile_source(dxc, source, f"terrain_{profile}", profile, True, work_folder)
            record(f"dxc 지형 {profile} O3", outcome["exit"] == 0 and outcome["samples"] >= 1, outcome["error"] or f"표본 {outcome['samples']} 명령 {outcome['instructions']}")
            compiles[f"terrain_{profile}"] = outcome
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
