"""시스템 Python 3(numpy·Pillow)에서 실행, 에디터 불필요: 안개 산란 켬/끔 PNG 짝의 평균 휘도비·변화 화소 비율·수평 띠 3개(위·중간·아래)별 라플라시안 분산비(선명도 손실)·최대 차이를 잰다(SF-1 명세 7절).
실행: python Tools/FogScattering/check_fog_scattering_images.py --off 끔.png --on 켬.png | --run Saved/FluidFX/lookdev/<시각> [--pair 끔이름:켬이름 ...] [--luminance-tolerance 0.03] [--out 경로.json] | --self-test (PowerShell 또는 Git Bash)
출력: 표준 출력 JSON 요약과 Saved/FogScattering/images/result-<시각>.json(또는 --out). 종료 코드 0 = 전 짝 휘도비 허용 범위 안 / 1 = 범위 밖 또는 자체 시험 실패 / 2 = 입력 없음·크기 불일치
상태: 2026-10-23 SF-1 측정 장치 빌더 작성, 자체 시험만 통과(실제 촬영 영상 미검증, 휘도는 8비트 sRGB 값에 Rec.709 계수를 곱한 값)
"""
import argparse
import json
import re
import sys
import tempfile
import time
from pathlib import Path

import numpy as np
from PIL import Image

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT_DIRECTORY = ROOT / "Saved" / "FogScattering" / "images"
DEFAULT_PAIRS = ("top_off:top_on", "top_off:top_ds8", "third_off:third_on")
LUMA_WEIGHTS = np.array([0.2126, 0.7152, 0.0722])
CHANGED_PIXEL_THRESHOLD = 2.0
BAND_NAMES = ("top", "middle", "bottom")
DEFAULT_LUMINANCE_TOLERANCE = 0.03
SELF_TEST_TOLERANCE = 0.01
SELF_TEST_SIZE = (640, 360)
SELF_TEST_BLUR_SIGMA = 3.0
EXIT_OUT_OF_RANGE = 1
EXIT_INPUT = 2


def load_rgb(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)


def compute_luma(rgb):
    return rgb @ LUMA_WEIGHTS


def compute_laplacian(luma):
    padded = np.pad(luma, 1, mode="edge")
    return 4.0 * luma - padded[:-2, 1:-1] - padded[2:, 1:-1] - padded[1:-1, :-2] - padded[1:-1, 2:]


def split_bands(values):
    edges = np.linspace(0, values.shape[0], len(BAND_NAMES) + 1).astype(int)
    return {name: values[edges[index]:edges[index + 1]] for index, name in enumerate(BAND_NAMES)}


def round_or_none(value, digits=5):
    return None if value is None else round(float(value), digits)


def measure_pair(off_rgb, on_rgb, luminance_tolerance):
    off_luma = compute_luma(off_rgb)
    on_luma = compute_luma(on_rgb)
    mean_off = float(off_luma.mean())
    mean_on = float(on_luma.mean())
    ratio = mean_on / mean_off if mean_off > 0.0 else None
    difference = np.abs(on_rgb - off_rgb)
    off_bands = split_bands(compute_laplacian(off_luma))
    on_bands = split_bands(compute_laplacian(on_luma))
    bands = {}
    for name in BAND_NAMES:
        variance_off = float(off_bands[name].var())
        variance_on = float(on_bands[name].var())
        variance_ratio = variance_on / variance_off if variance_off > 0.0 else None
        bands[name] = {
            "laplacian_variance_off": round_or_none(variance_off, 3),
            "laplacian_variance_on": round_or_none(variance_on, 3),
            "variance_ratio": round_or_none(variance_ratio),
            "sharpness_loss": round_or_none(None if variance_ratio is None else 1.0 - variance_ratio),
        }
    return {
        "mean_luma_off": round_or_none(mean_off, 3),
        "mean_luma_on": round_or_none(mean_on, 3),
        "luminance_ratio": round_or_none(ratio),
        "luminance_ok": ratio is not None and abs(ratio - 1.0) <= luminance_tolerance,
        "changed_pixel_ratio": round_or_none((difference.max(axis=2) > CHANGED_PIXEL_THRESHOLD).mean()),
        "max_difference": round_or_none(difference.max(), 1),
        "bands": bands,
    }


def measure_files(off_path, on_path, luminance_tolerance):
    off_rgb = load_rgb(off_path)
    on_rgb = load_rgb(on_path)
    if off_rgb.shape != on_rgb.shape:
        raise ValueError(f"크기 불일치: {off_path} {off_rgb.shape[1::-1]} / {on_path} {on_rgb.shape[1::-1]}")
    result = measure_pair(off_rgb, on_rgb, luminance_tolerance)
    result.update({"off": str(off_path), "on": str(on_path), "size": [off_rgb.shape[1], off_rgb.shape[0]]})
    return result


def find_run_image(run_directory, name):
    pattern = re.compile(r"^\d+_" + re.escape(name) + r"\.png$")
    matches = sorted(path for path in Path(run_directory).glob("*.png") if pattern.match(path.name))
    return matches[-1] if matches else None


def gaussian_blur(image, sigma):
    radius = int(np.ceil(3.0 * sigma))
    offsets = np.arange(-radius, radius + 1)
    kernel = np.exp(-0.5 * (offsets / sigma) ** 2)
    kernel /= kernel.sum()
    horizontal = np.pad(image, ((0, 0), (radius, radius), (0, 0)), mode="reflect")
    blurred = sum(weight * horizontal[:, index:index + image.shape[1]] for index, weight in enumerate(kernel))
    vertical = np.pad(blurred, ((radius, radius), (0, 0), (0, 0)), mode="reflect")
    return sum(weight * vertical[index:index + image.shape[0]] for index, weight in enumerate(kernel))


def make_self_test_pair(directory):
    width, height = SELF_TEST_SIZE
    generator = np.random.default_rng(20261023)
    noise = generator.random((height, width, 3))
    stripes = 0.5 + 0.5 * np.sin(np.arange(width) * 0.9)[None, :, None]
    off = np.clip(40.0 + 170.0 * (0.6 * noise + 0.4 * stripes), 0, 255)
    weight = np.linspace(1.0, 0.0, height)[:, None, None]
    on = (1.0 - weight) * off + weight * gaussian_blur(off, SELF_TEST_BLUR_SIGMA)
    paths = {}
    for name, image in (("off", off), ("on", on), ("dark", off * 0.9)):
        paths[name] = Path(directory) / f"{name}.png"
        Image.fromarray(np.round(image).astype(np.uint8)).save(paths[name])
    return paths


def run_self_test():
    failures = []

    def check(label, condition, detail):
        print(("PASS " if condition else "FAIL ") + label + " " + detail)
        if not condition:
            failures.append(label)

    with tempfile.TemporaryDirectory(prefix="td_fog_images_") as directory:
        paths = make_self_test_pair(directory)
        blended = measure_files(paths["off"], paths["on"], SELF_TEST_TOLERANCE)
        darkened = measure_files(paths["off"], paths["dark"], SELF_TEST_TOLERANCE)
        identical = measure_files(paths["off"], paths["off"], SELF_TEST_TOLERANCE)
    top_loss = blended["bands"]["top"]["sharpness_loss"]
    middle_loss = blended["bands"]["middle"]["sharpness_loss"]
    bottom_loss = blended["bands"]["bottom"]["sharpness_loss"]
    check("띠별 손실 위>중간>아래", top_loss > middle_loss > bottom_loss, f"top={top_loss} middle={middle_loss} bottom={bottom_loss}")
    check("블렌드 휘도비 ±1%", blended["luminance_ok"], f"ratio={blended['luminance_ratio']}")
    check("블렌드 변화 화소 비율 > 0", blended["changed_pixel_ratio"] > 0.0, f"ratio={blended['changed_pixel_ratio']}")
    check("어둡게 한 영상은 휘도비 ±1% 밖", not darkened["luminance_ok"], f"ratio={darkened['luminance_ratio']}")
    check("같은 영상은 변화 0·휘도비 1", identical["changed_pixel_ratio"] == 0.0 and identical["luminance_ratio"] == 1.0 and identical["max_difference"] == 0.0, f"changed={identical['changed_pixel_ratio']} ratio={identical['luminance_ratio']}")
    print("자체 시험", "통과" if not failures else "실패: " + ", ".join(failures))
    return 0 if not failures else EXIT_OUT_OF_RANGE


def collect_pairs(arguments):
    if arguments.off or arguments.on:
        if not (arguments.off and arguments.on):
            raise ValueError("--off 와 --on 을 함께 지정해야 한다")
        return [(Path(arguments.off), Path(arguments.on))]
    if not arguments.run:
        raise ValueError("--off/--on, --run, --self-test 중 하나가 필요하다")
    run_directory = Path(arguments.run)
    if not run_directory.is_dir():
        raise ValueError(f"실행 폴더가 없다: {run_directory}")
    pairs = []
    for pair_text in arguments.pair or DEFAULT_PAIRS:
        off_name, _, on_name = pair_text.partition(":")
        off_path = find_run_image(run_directory, off_name)
        on_path = find_run_image(run_directory, on_name)
        if off_path is None or on_path is None:
            raise ValueError(f"짝 이미지를 찾지 못함: {pair_text} (끔={off_path}, 켬={on_path})")
        pairs.append((off_path, on_path))
    return pairs


def main():
    parser = argparse.ArgumentParser(description="안개 산란 켬/끔 영상 짝 측정")
    parser.add_argument("--off")
    parser.add_argument("--on")
    parser.add_argument("--run")
    parser.add_argument("--pair", action="append")
    parser.add_argument("--luminance-tolerance", type=float, default=DEFAULT_LUMINANCE_TOLERANCE)
    parser.add_argument("--out")
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    if arguments.self_test:
        return run_self_test()
    try:
        pairs = collect_pairs(arguments)
        results = [measure_files(off_path, on_path, arguments.luminance_tolerance) for off_path, on_path in pairs]
    except (OSError, ValueError) as error:
        print("입력 오류:", error)
        return EXIT_INPUT
    summary = {"luminance_tolerance": arguments.luminance_tolerance, "pairs": results}
    output_path = Path(arguments.out) if arguments.out else DEFAULT_OUTPUT_DIRECTORY / f"result-{time.strftime('%Y%m%d-%H%M%S')}.json"
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False, indent=1))
    print("결과 파일:", output_path)
    return 0 if all(result["luminance_ok"] for result in results) else EXIT_OUT_OF_RANGE


if __name__ == "__main__":
    sys.exit(main())
