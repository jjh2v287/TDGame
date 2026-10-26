"""실행 환경: 시스템 Python 3(numpy·Pillow), 에디터 불필요
실행 명령: python Tools/FluidFX/make_fluid_track_textures.py [--root 프로젝트경로] [--seed 정수]
출력: Saved/FluidFX/track_textures/T_TD_FluidDuneHeight.png(512², 16비트 회색, 이음매 없는 비등방 잔물결, 주 파장 = 타일의 1/8), T_TD_FluidSnowDetailN.png(512², 8비트 RGB 법선), track_textures.json(파일 sha256·주 파장·이음매 검사), 표준 출력 같은 JSON
상태: 미검증(2026-10-25 FX-12r 작성, 에디터 가져오기 전). 두 장 모두 FFT 주기 합성이라 타일 경계가 이어지고, 시드가 같으면 바이트가 같다.
"""
import argparse
import hashlib
import json
import pathlib

import numpy as np
from PIL import Image

SIZE = 512
DUNE_CYCLES_PER_TILE = 8
DUNE_ALONG_SPREAD = 1.2
DUNE_ACROSS_SPREAD = 4.0
SNOW_SPECTRAL_EXPONENT = 2.2
SNOW_NORMAL_STRENGTH = 6.0
DEFAULT_SEED = 20261025


def spectral_field(rng, weight):
    phases = np.exp(2j * np.pi * rng.random((SIZE, SIZE)))
    field = np.real(np.fft.ifft2(phases * weight))
    field -= field.min()
    return field / max(field.max(), 1e-12)


def frequency_grid():
    frequencies = np.fft.fftfreq(SIZE) * SIZE
    return np.meshgrid(frequencies, frequencies, indexing="ij")


def make_dune_height(rng):
    along, across = frequency_grid()
    ridge = np.exp(-0.5 * (((np.abs(along) - DUNE_CYCLES_PER_TILE) / DUNE_ALONG_SPREAD) ** 2 + (across / DUNE_ACROSS_SPREAD) ** 2))
    ridge[0, 0] = 0.0
    ripples = spectral_field(rng, ridge)
    sharpened = np.power(ripples, 1.6)
    return (sharpened - sharpened.min()) / max(np.ptp(sharpened), 1e-12)


def make_snow_normal(rng):
    along, across = frequency_grid()
    radius = np.sqrt(along * along + across * across)
    weight = np.where(radius > 0.0, np.power(np.maximum(radius, 1.0), -SNOW_SPECTRAL_EXPONENT * 0.5), 0.0)
    weight *= np.exp(-radius / 96.0)
    height = spectral_field(rng, weight)
    gradient_x = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * 0.5 * SNOW_NORMAL_STRENGTH
    gradient_y = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * 0.5 * SNOW_NORMAL_STRENGTH
    normal = np.stack([-gradient_x, -gradient_y, np.ones_like(height)], axis=-1)
    return normal / np.linalg.norm(normal, axis=-1, keepdims=True)


def dominant_cycles(height):
    spectrum = np.abs(np.fft.fft2(height - height.mean()))
    spectrum[0, 0] = 0.0
    index = np.unravel_index(np.argmax(spectrum), spectrum.shape)
    frequencies = np.fft.fftfreq(SIZE) * SIZE
    return [abs(float(frequencies[index[0]])), abs(float(frequencies[index[1]]))]


def seam_ratio(field):
    interior = np.abs(np.diff(field, axis=0)).mean()
    seam = np.abs(field[0] - field[-1]).mean()
    return float(seam / max(interior, 1e-12))


def file_sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parents[2]))
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    arguments = parser.parse_args()
    output_folder = pathlib.Path(arguments.root) / "Saved" / "FluidFX" / "track_textures"
    output_folder.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(arguments.seed)

    dune = make_dune_height(rng)
    dune_path = output_folder / "T_TD_FluidDuneHeight.png"
    Image.fromarray(np.round(dune.T * 65535.0).astype(np.uint16)).save(dune_path)
    normal = make_snow_normal(rng)
    normal_path = output_folder / "T_TD_FluidSnowDetailN.png"
    Image.fromarray(np.round((normal.transpose(1, 0, 2) * 0.5 + 0.5) * 255.0).astype(np.uint8), mode="RGB").save(normal_path)

    cycles = dominant_cycles(dune)
    dune_seam = max(seam_ratio(dune), seam_ratio(dune.T))
    normal_seam = max(seam_ratio(normal[..., 0]), seam_ratio(normal[..., 1].T))
    report = {
        "seed": arguments.seed,
        "size": SIZE,
        "dune_height": {"path": dune_path.relative_to(arguments.root).as_posix() if pathlib.Path(arguments.root) in dune_path.parents else str(dune_path), "sha256": file_sha256(dune_path), "bits": 16, "dominant_cycles_per_tile": cycles, "expected_cycles_per_tile": DUNE_CYCLES_PER_TILE, "seam_over_interior": dune_seam},
        "snow_detail_normal": {"path": normal_path.name, "sha256": file_sha256(normal_path), "seam_over_interior": normal_seam},
        "ok": abs(max(cycles) - DUNE_CYCLES_PER_TILE) <= 1.0 and dune_seam < 3.0 and normal_seam < 3.0,
    }
    (output_folder / "track_textures.json").write_text(json.dumps(report, ensure_ascii=False, indent=1) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps(report, ensure_ascii=False))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
