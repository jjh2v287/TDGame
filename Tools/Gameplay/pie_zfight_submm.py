"""실행 환경: 시스템 Python(numpy·Pillow), 언리얼 에디터가 이 프로젝트(LV_DarkFantasy_OpenWorld)를 열고 있어야 한다. 에디터·PIE 를 쓰므로 빌드 창 보류(build_and_test.py --acquire-hold) 안에서 돌린다.
실행 명령: python -B Tools/Gameplay/pie_zfight_submm.py --label <이름> [--spots="-18500,-4800;-12700,-8100"] [--step-mm 0.1] [--steps 4] [--start-pie] [--stop-pie]
출력: Saved/Gameplay/ZFight/<label>/s<지점>_<번호>.png(0·1 = 제자리 두 장, 2~ = 걸음마다)와 result.json(지점별 제자리 잡음·걸음별 바뀐 화소 수), 표준 출력 지점별 요약
상태: GF-07(2026-11-21) 낙엽 판 Z-파이팅 판정에 씀. 시간 거의 정지(slomo 0.0001)·AA 끔·카메라 지연 끔에서 카메라를 0.1mm 씩 옮기면 보통 물체는 거의 안 바뀌고 같은 깊이로 겹친 면만 뒤집힌다(두께 0 낙엽 판 3.6만~6.6만 화소, 띄운 판 5.5천~9천, 제자리 잡음 2천~4천). 끝나면 slomo 1·AA 원래 값으로 되돌린다.
"""
import argparse
import json
import os
import sys
import time

import numpy as np
from PIL import Image

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(PROJECT_ROOT, "Tools"))
sys.path.insert(0, os.path.join(PROJECT_ROOT, "Tools", "Gameplay"))
import pie_floor_popping as probe
import run_in_editor

OUTPUT_ROOT = os.path.join(PROJECT_ROOT, "Saved", "Gameplay", "ZFight")
DEFAULT_SPOTS = "-18500,-4800;-12700,-8100;-20700,-700"
CHANGE_THRESHOLD = 24
SETTLE_SECONDS = 4.0
STEP_SETTLE_SECONDS = 0.8
SHOT_TIMEOUT_SECONDS = 15.0
PAWN_HALF_HEIGHT_CM = 96.0
PRELUDE = ("import unreal\n"
           "world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\n"
           "pawn = unreal.GameplayStatics.get_player_pawn(world, 0)\n")


def run_remote(code):
    result = run_in_editor.run_code(PRELUDE + code)
    return [line.split("OUT ", 1)[1].strip() for line in result.get("lines", []) if "OUT " in line]


def find_ground_height(x, y):
    lines = run_remote(
        f"hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector({x}, {y}, 20000), unreal.Vector({x}, {y}, -20000), unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [pawn], unreal.DrawDebugTrace.NONE, True)\n"
        "print('OUT', hit.to_tuple()[4].z if hit else 'none')\n")
    if not lines or lines[0] == "none":
        raise RuntimeError(f"no ground under {x},{y}")
    return float(lines[0])


def place_pawn(x, y, z):
    run_remote(f"pawn.set_actor_location(unreal.Vector({x}, {y}, {z}), False, True)\nprint('OUT placed')\n")


def freeze_view():
    lines = run_remote(
        "anti_aliasing = unreal.SystemLibrary.get_console_variable_int_value('r.AntiAliasingMethod')\n"
        "boom = pawn.get_component_by_class(unreal.SpringArmComponent)\n"
        "boom.set_editor_property('enable_camera_lag', False)\n"
        "boom.set_editor_property('enable_camera_rotation_lag', False)\n"
        "unreal.SystemLibrary.execute_console_command(world, 'slomo 0.0001')\n"
        "unreal.SystemLibrary.execute_console_command(world, 'r.AntiAliasingMethod 0')\n"
        "print('OUT', anti_aliasing)\n")
    return int(lines[0]) if lines else 4


def restore_view(anti_aliasing):
    run_remote(f"unreal.SystemLibrary.execute_console_command(world, 'slomo 1')\nunreal.SystemLibrary.execute_console_command(world, 'r.AntiAliasingMethod {anti_aliasing}')\nprint('OUT restored')\n")


def take_shot(path_without_extension):
    path = path_without_extension + ".png"
    if os.path.exists(path):
        os.remove(path)
    run_remote(f"unreal.SystemLibrary.execute_console_command(world, 'Shot filename={path_without_extension} -nosuffix')\nprint('OUT shot')\n")
    deadline = time.time() + SHOT_TIMEOUT_SECONDS
    last_size = -1
    while time.time() < deadline:
        if os.path.exists(path):
            size = os.path.getsize(path)
            if size > 0 and size == last_size:
                return path
            last_size = size
        time.sleep(0.3)
    raise RuntimeError("screenshot timed out: " + path)


def count_changed_pixels(first_path, second_path):
    first = np.asarray(Image.open(first_path).convert("RGB"), dtype=np.int16)
    second = np.asarray(Image.open(second_path).convert("RGB"), dtype=np.int16)
    return int((np.abs(first - second).max(axis=2) > CHANGE_THRESHOLD).sum())


def measure_spot(run_dir, index, x, y, step_cm, steps):
    z = find_ground_height(x, y) + PAWN_HALF_HEIGHT_CM
    place_pawn(x, y, z)
    time.sleep(SETTLE_SECONDS)
    paths = []
    for shot_index in range(steps + 2):
        offset_cm = max(shot_index - 1, 0) * step_cm
        place_pawn(x + offset_cm, y, z)
        time.sleep(STEP_SETTLE_SECONDS)
        paths.append(take_shot(os.path.join(run_dir, f"s{index}_{shot_index}").replace("\\", "/")))
    noise = count_changed_pixels(paths[0], paths[1])
    steps_changed = [count_changed_pixels(first, second) for first, second in zip(paths[1:], paths[2:])]
    return {"spot": [x, y], "pawn_z": round(z, 2), "repeat_noise": noise, "step_changed": steps_changed, "mean_step_over_noise": round(float(np.mean(steps_changed)) / max(noise, 1), 2)}


def parse_spots(text):
    return [tuple(float(value) for value in pair.split(",")) for pair in text.split(";") if pair.strip()]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--label", required=True)
    parser.add_argument("--spots", default=DEFAULT_SPOTS)
    parser.add_argument("--step-mm", type=float, default=0.1)
    parser.add_argument("--steps", type=int, default=4)
    parser.add_argument("--start-pie", action="store_true")
    parser.add_argument("--stop-pie", action="store_true")
    args = parser.parse_args()
    run_dir = os.path.join(OUTPUT_ROOT, args.label)
    os.makedirs(run_dir, exist_ok=True)
    if not probe.ensure_pie(args.start_pie):
        return 2
    anti_aliasing = freeze_view()
    spots = []
    try:
        for index, (x, y) in enumerate(parse_spots(args.spots)):
            spots.append(measure_spot(run_dir, index, x, y, args.step_mm / 10.0, args.steps))
            print(f"지점 {index} ({x:.0f},{y:.0f}): 제자리 {spots[-1]['repeat_noise']} / 걸음 {spots[-1]['step_changed']} (잡음 대비 {spots[-1]['mean_step_over_noise']}배)", flush=True)
    finally:
        restore_view(anti_aliasing)
        if args.stop_pie:
            probe.pie_tool("StopPIE")
    result = {"label": args.label, "step_mm": args.step_mm, "threshold": CHANGE_THRESHOLD, "spots": spots}
    with open(os.path.join(run_dir, "result.json"), "w", encoding="utf-8") as handle:
        json.dump(result, handle, ensure_ascii=False, indent=1)
    print("결과:", os.path.join(run_dir, "result.json"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
