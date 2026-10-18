"""실행 환경: 시스템 Python(에디터 불필요). build_and_test.py로 TDGame.FluidFX.Fire 시험을 돌린 뒤의 시험 로그에서 LogTDFluidFireCalibration 줄을 읽는다.
실행 명령: python Tools/FluidFX/make_fire_headless_reference.py [--log Saved/AgentOps/<날짜>/<라벨>-tests.log]
출력: Saved/FluidFX/fluid_fire_headless_reference.json(pie_probe_fluid_fire.py의 ±30% 기준). 종료 코드 0 키 하나 이상 기록/1 보정 줄 없음
상태: 검증됨(2026-10-18 fx3-b5 로그로 torch_light_cv·campfire_light_cv·campfire_fire_area_cm2·campfire_smoke_area_cm2 추출). torch_drag_cm는 FX-3 후속 빌드 뒤 로그부터, campfire_ember_alive_mean은 헤드리스 시험이 없어 null.
"""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
AGENT_OPS_DIRECTORY = ROOT / "Saved" / "AgentOps"
OUTPUT_PATH = ROOT / "Saved" / "FluidFX" / "fluid_fire_headless_reference.json"
CALIBRATION_MARKER = "LogTDFluidFireCalibration"
STEADY_STATE_PATTERN = re.compile(r"SteadyStateMeanArea (\w+) = ([\d.]+) cm2, light CoV = ([\d.]+)")
SMOKE_PATTERN = re.compile(r"CampfireSmoke area [\d.]+ -> ([\d.]+) cm2")
TORCH_DRAG_PATTERN = re.compile(r"TorchDrag600 lag (-?[\d.]+) cm")
REFERENCE_KEYS = (
    "torch_drag_cm",
    "torch_light_cv",
    "campfire_light_cv",
    "campfire_ember_alive_mean",
    "campfire_fire_area_cm2",
    "campfire_smoke_area_cm2",
)


def find_latest_calibration_log():
    candidates = sorted(AGENT_OPS_DIRECTORY.glob("*/*-tests.log"), key=lambda path: path.stat().st_mtime, reverse=True)
    for path in candidates:
        if CALIBRATION_MARKER in path.read_text(encoding="utf-8", errors="replace"):
            return path
    return None


def extract_reference(log_text):
    reference = dict.fromkeys(REFERENCE_KEYS)
    for line in log_text.splitlines():
        if CALIBRATION_MARKER not in line:
            continue
        steady_state = STEADY_STATE_PATTERN.search(line)
        if steady_state and steady_state.group(1) == "Torch":
            reference["torch_light_cv"] = float(steady_state.group(3))
        if steady_state and steady_state.group(1) == "Campfire":
            reference["campfire_fire_area_cm2"] = float(steady_state.group(2))
            reference["campfire_light_cv"] = float(steady_state.group(3))
        smoke = SMOKE_PATTERN.search(line)
        if smoke:
            reference["campfire_smoke_area_cm2"] = float(smoke.group(1))
        torch_drag = TORCH_DRAG_PATTERN.search(line)
        if torch_drag:
            reference["torch_drag_cm"] = float(torch_drag.group(1))
    return reference


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--log", type=Path)
    arguments = parser.parse_args()

    log_path = arguments.log or find_latest_calibration_log()
    if log_path is None or not log_path.exists():
        print("보정 줄이 있는 시험 로그가 없다. build_and_test.py --filter TDGame.FluidFX.Fire 를 먼저 돌린다.")
        return 1

    reference = extract_reference(log_path.read_text(encoding="utf-8", errors="replace"))
    if all(value is None for value in reference.values()):
        print(f"{log_path} 에 {CALIBRATION_MARKER} 줄이 없다.")
        return 1

    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_PATH.write_text(json.dumps({"source_log": str(log_path), **reference}, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"output": str(OUTPUT_PATH), **reference}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
