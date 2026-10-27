"""실행 환경: 시스템 Python 3(numpy·Pillow), 에디터 불필요
실행 명령: python Tools/FluidFX/make_fluid_cross_section_textures.py [--root 프로젝트경로] [--seed 정수] [--output-folder 경로]
출력: Saved/FluidFX/cross_section_textures/T_TD_FluidCrossSection_{Mushroom,Pump,Ring}.png(256², 16비트 회색) + cross_section_textures.json(파일 sha256·규약·자체 검사), 표준 출력 같은 JSON. 하나라도 검사가 틀리면 종료 코드 1
상태: 미검증(2026-10-26 FX-9 묶음 E 작성, 에디터 가져오기 전). 규약(cross-plan P-04): 열 = u(방사 = 축 거리/반경, 링 = 시뮬 밀도), 행 = v = 바닥으로부터 높이/VolCrossSectionHeightCm, 이미지 위 행(v=0) = 바닥. 마지막 두 행은 0(고정 주소로 높이 초과가 비게), 링 0열은 0(밀도 0 칸이 비게). 시드가 같으면 바이트가 같다.
"""
import argparse
import hashlib
import json
import pathlib

import numpy as np
from PIL import Image

SIZE = 256
DEFAULT_SEED = 20261026
EMPTY_TOP_ROWS = 2
TEXTURE_NAMES = ("Mushroom", "Pump", "Ring")
CONVENTION = "열 u, 행 v = h / VolCrossSectionHeightCm, 위 행(v=0) = 바닥 (P-04)"


def smoothstep(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def texel_grid(size=SIZE):
    centers = (np.arange(size, dtype=np.float64) + 0.5) / size
    v, u = np.meshgrid(centers, centers, indexing="ij")
    return u, v


def smooth_noise(rng, size=SIZE, cutoff=6.0):
    frequencies = np.fft.fftfreq(size) * size
    fx, fy = np.meshgrid(frequencies, frequencies, indexing="ij")
    weight = np.exp(-(fx * fx + fy * fy) / (2.0 * cutoff * cutoff))
    weight[0, 0] = 0.0
    field = np.real(np.fft.ifft2(np.exp(2j * np.pi * rng.random((size, size))) * weight))
    field -= field.min()
    return field / max(float(field.max()), 1e-12)


def finish_section(values):
    values = np.clip(values, 0.0, 1.0)
    values[-EMPTY_TOP_ROWS:, :] = 0.0
    return values


def make_mushroom(rng, size=SIZE):
    u, v = texel_grid(size)
    stem_radius = 0.09 + 0.03 * v + 0.12 * (1.0 - smoothstep(0.0, 0.1, v))
    stem = (1.0 - smoothstep(stem_radius - 0.04, stem_radius, u)) * (1.0 - smoothstep(0.6, 0.68, v))
    cap_distance = np.sqrt((u / 0.6) ** 2 + ((v - 0.72) / 0.16) ** 2)
    cap = 1.0 - smoothstep(0.8, 1.0, cap_distance)
    roll_distance = np.sqrt(((u - 0.5) / 0.13) ** 2 + ((v - 0.69) / 0.12) ** 2)
    roll = 1.0 - smoothstep(0.6, 1.0, roll_distance)
    shape = np.maximum(np.maximum(stem, 0.8 * cap), roll)
    shape *= 0.85 + 0.15 * smooth_noise(rng, size)
    shape *= (1.0 - smoothstep(0.9, 0.97, u)) * (1.0 - smoothstep(0.9, 0.96, v))
    return finish_section(shape)


def make_pump(rng, size=SIZE):
    u, v = texel_grid(size)
    bulge = np.power(0.5 + 0.5 * np.cos(2.0 * np.pi * 3.0 * v), 2.0)
    radius = 0.1 + 0.22 * bulge * (1.0 - 0.4 * v)
    shape = 1.0 - smoothstep(radius - 0.05, radius, u)
    shape *= 0.9 + 0.1 * smooth_noise(rng, size)
    shape *= 1.0 - smoothstep(0.86, 0.94, v)
    return finish_section(shape)


def make_ring(rng, size=SIZE):
    u, v = texel_grid(size)
    blob = np.exp(-0.5 * (((u - 0.65) / 0.22) ** 2 + ((v - 0.3) / 0.12) ** 2))
    shape = blob * smoothstep(0.0, 0.15, u) * (0.9 + 0.1 * smooth_noise(rng, size))
    shape *= 1.0 - smoothstep(0.8, 0.92, v)
    shape = finish_section(shape)
    shape[:, 0] = 0.0
    return shape


def make_sections(seed):
    rng = np.random.default_rng(seed)
    return {"Mushroom": make_mushroom(rng), "Pump": make_pump(rng), "Ring": make_ring(rng)}


def quantize(values):
    return np.round(np.clip(values, 0.0, 1.0) * 65535.0).astype(np.uint16)


def check_sections(sections):
    problems = []
    for name, values in sections.items():
        if values.shape != (SIZE, SIZE):
            problems.append(f"{name}: 크기 {values.shape}")
        if float(np.abs(values[-EMPTY_TOP_ROWS:, :]).max()) != 0.0:
            problems.append(f"{name}: 마지막 {EMPTY_TOP_ROWS}행(높이 초과)이 0이 아님")
        if not np.isfinite(values).all():
            problems.append(f"{name}: 유한하지 않은 값")
    mushroom = sections["Mushroom"]
    if float(mushroom[0, 0]) < 0.5:
        problems.append(f"Mushroom: 바닥 행 축(0,0) 밀도 {float(mushroom[0, 0]):.3f} < 0.5(위 행 = 바닥 규약 위반 의심)")
    if float(mushroom[int(0.72 * SIZE), int(0.45 * SIZE)]) < 0.5:
        problems.append("Mushroom: 갓 높이(v=0.72) 바깥 반경 밀도가 낮음")
    if float(mushroom[int(0.4 * SIZE), int(0.45 * SIZE)]) > 0.1:
        problems.append("Mushroom: 줄기 높이(v=0.4) 바깥 반경이 비지 않음")
    if float(np.abs(sections["Ring"][:, 0]).max()) != 0.0:
        problems.append("Ring: 0열(시뮬 밀도 0)이 0이 아님")
    pump_axis = sections["Pump"][: int(0.8 * SIZE), int(0.2 * SIZE)]
    if int(np.sum(np.diff((pump_axis > 0.5).astype(np.int8)) == 1)) < 2:
        problems.append("Pump: 반경 0.2 에서 부풂 구간이 2개 미만")
    return problems


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parents[2]))
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument("--output-folder", default="")
    arguments = parser.parse_args()
    output_folder = pathlib.Path(arguments.output_folder) if arguments.output_folder else pathlib.Path(arguments.root) / "Saved" / "FluidFX" / "cross_section_textures"
    output_folder.mkdir(parents=True, exist_ok=True)
    sections = make_sections(arguments.seed)
    problems = check_sections(sections)
    files = {}
    for name in TEXTURE_NAMES:
        path = output_folder / f"T_TD_FluidCrossSection_{name}.png"
        Image.fromarray(quantize(sections[name])).save(path)
        reread = np.asarray(Image.open(path))
        if reread.dtype != np.uint16 or not np.array_equal(reread, quantize(sections[name])):
            problems.append(f"{name}: 16비트 PNG 다시 읽기 불일치({reread.dtype})")
        files[name] = {"path": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "bits": 16, "size": SIZE}
    report = {"seed": arguments.seed, "convention": CONVENTION, "empty_top_rows": EMPTY_TOP_ROWS, "files": files, "problems": problems, "ok": not problems}
    (output_folder / "cross_section_textures.json").write_text(json.dumps(report, ensure_ascii=False, indent=1) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps(report, ensure_ascii=False))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
