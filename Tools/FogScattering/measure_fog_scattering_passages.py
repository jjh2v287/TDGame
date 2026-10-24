"""실행 환경: 시스템 Python 3(numpy·Pillow), 언리얼 에디터가 TDGame을 열고 MCP 포트 8000·원격 실행이 켜진 상태(에디터 앞창 유지, 빌드 보류 획득 뒤).
실행 명령: python Tools/FogScattering/measure_fog_scattering_passages.py [--realms PaleHeights,SunkenStarfield] [--weights 0,0.1,0.25,0.4,0.5,0.6,0.75,0.9,1] [--level /Game/Level/LV_DarkFantasy_OpenWorld] [--plan-only] (PowerShell)
출력: Saved/FogScattering/passages/<시각>/summary.json·summary.md(통로 지점별 기대·실측 이계 가중치, 적용된 DistanceScale·WeightPower·Intensity, 폰 좌표, 끔 대비 띠별 선명도 손실·휘도비, 끔·끔 잡음)와 지점별 룩데브 폴더 경로. 종료 코드 0 = 전 지점 측정, 1 = 일부 실패, 2 = 환경 문제
상태: 2026-10-24 작성(SF-4 v2 경계 블렌딩 측정용)
"""
import argparse
import json
import math
import re
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools" / "FogScattering"))
import check_fog_scattering_images as images  # noqa: E402

REALM_LAYOUT = ROOT / "Saved" / "WorldGen" / "Realms" / "{realm}" / "layout.json"
LOOKDEV_TOOL = ROOT / "Tools" / "FluidFX" / "pie_lookdev_fluid.py"
RUN_IN_EDITOR = ROOT / "Tools" / "run_in_editor.py"
OUTPUT_ROOT = ROOT / "Saved" / "FogScattering" / "passages"
DEFAULT_LEVEL = "/Game/Level/LV_DarkFantasy_OpenWorld"
DEFAULT_WEIGHTS = "0,0.1,0.25,0.4,0.5,0.6,0.75,0.9,1"
PAWN_CLEARANCE_CM = 150.0
SETTLE_SECONDS = 10
READ_MARKER = "TDFOG_PASSAGE_STATE"
EXIT_PARTIAL = 1
EXIT_ENVIRONMENT = 2

READ_STATE_CODE = """
import json
import unreal
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
pawn = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
subsystem = unreal.TDRealmSubsystem.get_realm_subsystem(pawn) if pawn else None
state = {"pie": world is not None}
if pawn:
    location = pawn.get_actor_location()
    state["pawn"] = [round(location.x, 1), round(location.y, 1), round(location.z, 1)]
if subsystem:
    state["active_realm"] = str(subsystem.get_active_realm_id())
    state["weights"] = {name: round(subsystem.get_realm_weight(name), 4) for name in REALMS}
state["cvars"] = {name: round(unreal.SystemLibrary.get_console_variable_float_value(name), 4) for name in ["r.TDFogScattering.DistanceScale", "r.TDFogScattering.WeightPower", "r.TDFogScattering.Intensity"]}
print("MARKER " + json.dumps(state))
"""


def smoothstep(edge0, edge1, value):
    if edge1 <= edge0:
        return 1.0 if value >= edge1 else 0.0
    t = min(max((value - edge0) / (edge1 - edge0), 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def connector_weight(along_cm, approach_cm, departure_cm):
    if along_cm <= 0.0:
        return 0.5 * smoothstep(-approach_cm, 0.0, along_cm)
    return 0.5 + 0.5 * smoothstep(0.0, departure_cm, along_cm)


def find_distance_for_weight(target, approach_cm, departure_cm):
    low, high = 0.0, approach_cm + departure_cm
    for _ in range(60):
        middle = (low + high) / 2.0
        if connector_weight(middle - approach_cm, approach_cm, departure_cm) < target:
            low = middle
        else:
            high = middle
    return high if target > 0.0 else 0.0


def point_on_polyline(points, distance_cm):
    remaining = distance_cm
    for start, end in zip(points, points[1:]):
        length = math.dist(start[:2], end[:2])
        if remaining <= length or end is points[-1]:
            alpha = 0.0 if length <= 0.0 else min(remaining / length, 1.0)
            return [start[index] + (end[index] - start[index]) * alpha for index in range(3)]
        remaining -= length
    return list(points[-1])


def build_samples(realms, weights):
    samples = []
    for realm in realms:
        layout = json.loads(Path(str(REALM_LAYOUT).format(realm=realm)).read_text(encoding="utf-8"))
        for gate in layout["realm"]["gates"]:
            if gate["gate_kind"] != "Passage":
                continue
            approach_cm = float(gate["approach_length_cm"])
            departure_cm = float(gate["departure_length_cm"])
            for weight in weights:
                distance_cm = find_distance_for_weight(weight, approach_cm, departure_cm)
                floor = point_on_polyline(gate["centerline_cm"], distance_cm)
                samples.append({
                    "name": f"{realm[:4].lower()}_w{int(round(weight * 100)):03d}",
                    "realm": realm,
                    "gate": gate["gate_id"],
                    "expected_weight": round(connector_weight(distance_cm - approach_cm, approach_cm, departure_cm), 4),
                    "distance_m": round(distance_cm / 100.0, 2),
                    "pawn_target": [round(floor[0], 1), round(floor[1], 1), round(floor[2] + PAWN_CLEARANCE_CM, 1)],
                })
    return samples


def write_plan(sample, directory):
    plan = {
        "base": {"console": ["r.ProfileGPU.ShowUI 0", "r.AntiAliasingMethod 0"]},
        "variants": [
            {"name": f"{sample['name']}_setup", "apply": {"pawn": sample["pawn_target"], "time_dilation": 1.0, "console": ["r.TDFogScattering 1", "r.TDFogScattering.DebugView 0"]}, "settle": SETTLE_SECONDS},
            {"name": f"{sample['name']}_freeze", "apply": {"time_dilation": 0.0001}, "settle": 3},
            {"name": f"{sample['name']}_off", "apply": {"console": ["r.TDFogScattering 0"]}},
            {"name": f"{sample['name']}_off2", "apply": {"console": ["r.TDFogScattering 1", "r.TDFogScattering 0"]}},
            {"name": f"{sample['name']}_on", "apply": {"console": ["r.TDFogScattering 1"]}},
            {"name": f"{sample['name']}_resume", "apply": {"time_dilation": 1.0}, "settle": 1},
        ],
    }
    path = directory / f"plan_{sample['name']}.json"
    path.write_text(json.dumps(plan, ensure_ascii=False, indent=1), encoding="utf-8")
    return path


def run_lookdev(plan_path, level):
    completed = subprocess.run([sys.executable, str(LOOKDEV_TOOL), "--variants", str(plan_path), "--level", level, "--settle-seconds", "3"], capture_output=True, text=True, encoding="utf-8", errors="replace")
    match = re.search(r"격자 (\S+)grid\.png", completed.stdout)
    return completed.returncode, (ROOT / match.group(1)) if match else None


def read_editor_state(realms, directory):
    script = directory / "read_state.py"
    script.write_text(READ_STATE_CODE.replace("REALMS", json.dumps(realms)).replace("MARKER", READ_MARKER), encoding="utf-8")
    completed = subprocess.run([sys.executable, str(RUN_IN_EDITOR), str(script)], capture_output=True, text=True, encoding="utf-8", errors="replace")
    for line in completed.stdout.splitlines():
        index = line.find(READ_MARKER)
        if index >= 0:
            return json.loads(line[index + len(READ_MARKER):].strip())
    return {"error": completed.stdout[-300:]}


def stop_pie():
    subprocess.run([sys.executable, str(RUN_IN_EDITOR), "-c", "import unreal; unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()"], capture_output=True)


def measure_sample(sample, run_directory):
    record = {}
    for label, other in (("on", "on"), ("noise", "off2")):
        off_path = images.find_run_image(run_directory, f"{sample['name']}_off")
        on_path = images.find_run_image(run_directory, f"{sample['name']}_{other}")
        if off_path is None or on_path is None:
            record[label] = None
            continue
        result = images.measure_files(off_path, on_path, images.DEFAULT_LUMINANCE_TOLERANCE)
        record[label] = {"luminance_ratio": result["luminance_ratio"], **{band: result["bands"][band]["sharpness_loss"] for band in images.BAND_NAMES}}
    return record


def write_summary(directory, samples):
    (directory / "summary.json").write_text(json.dumps(samples, ensure_ascii=False, indent=1), encoding="utf-8")
    lines = ["# 안개 산란 통로 블렌딩 측정", "", "| 지점 | 통로 거리 m | 기대 가중치 | 실측 가중치 | k·p·i | 폰 z | 위 | 중간 | 아래 | 휘도비 | 끔·끔 잡음 최대 |", "|---|---|---|---|---|---|---|---|---|---|---|"]
    for sample in samples:
        state = sample.get("state", {})
        cvars = state.get("cvars", {})
        on = sample.get("metrics", {}).get("on") or {}
        noise = sample.get("metrics", {}).get("noise") or {}
        noise_max = max((abs(noise.get(band, 0.0)) for band in images.BAND_NAMES), default=None) if noise else None
        weight = state.get("weights", {}).get(sample["realm"])
        lines.append("| {name} | {distance} | {expected} | {weight} | {k}·{p}·{i} | {z} | {top} | {middle} | {bottom} | {lum} | {noise} |".format(
            name=sample["name"], distance=sample["distance_m"], expected=sample["expected_weight"], weight=weight,
            k=cvars.get("r.TDFogScattering.DistanceScale"), p=cvars.get("r.TDFogScattering.WeightPower"), i=cvars.get("r.TDFogScattering.Intensity"),
            z=(state.get("pawn") or [None, None, None])[2],
            top=on.get("top"), middle=on.get("middle"), bottom=on.get("bottom"), lum=on.get("luminance_ratio"), noise=None if noise_max is None else round(noise_max, 4)))
    (directory / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--realms", default="PaleHeights,SunkenStarfield")
    parser.add_argument("--weights", default=DEFAULT_WEIGHTS)
    parser.add_argument("--level", default=DEFAULT_LEVEL)
    parser.add_argument("--plan-only", action="store_true")
    arguments = parser.parse_args()
    realms = [name.strip() for name in arguments.realms.split(",") if name.strip()]
    weights = [float(value) for value in arguments.weights.split(",")]
    directory = OUTPUT_ROOT / datetime.now().strftime("%Y%m%d-%H%M%S")
    directory.mkdir(parents=True, exist_ok=True)
    samples = build_samples(realms, weights)
    if arguments.plan_only:
        write_summary(directory, samples)
        print(json.dumps(samples, ensure_ascii=False, indent=1))
        return 0

    failures = 0
    try:
        for sample in samples:
            code, run_directory = run_lookdev(write_plan(sample, directory), arguments.level)
            if run_directory is None:
                sample["error"] = f"lookdev exit {code}"
                failures += 1
                if code == EXIT_ENVIRONMENT:
                    break
                continue
            sample["lookdev"] = str(run_directory.relative_to(ROOT))
            sample["state"] = read_editor_state(realms, directory)
            sample["metrics"] = measure_sample(sample, run_directory)
            on = sample["metrics"].get("on") or {}
            print(f"{sample['name']}: weight {sample['state'].get('weights')} cvars {sample['state'].get('cvars')} top {on.get('top')} bottom {on.get('bottom')}", flush=True)
            time.sleep(1.0)
    finally:
        stop_pie()
    write_summary(directory, samples)
    print("summary:", directory / "summary.md")
    return EXIT_PARTIAL if failures else 0


if __name__ == "__main__":
    sys.exit(main())
