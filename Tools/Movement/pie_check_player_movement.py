"""시스템 Python으로 실행(언리얼 에디터 열림 필요): 내비메시가 있는 LV_TDMegaMagicArena(LV-Cambat에는 내비메시 경계가 없다)를 열고 에디터 내비메시 빌드가 끝난 뒤 PIE를 켜서 플레이어 폰(Mover·UAF)의 클릭 이동·도착 오차·이동 방향 회전·걷기 애니메이션(발 뼈 움직임)·점프·루트모션 공격(TDPlayMeleeAction) 이동량을 기록한 뒤 PIE를 끈다.
실행: python Tools/Movement/pie_check_player_movement.py
출력: Docs/Validation/Movement/player-pie-check.json(+ 스크린샷 2장), 중간 기록 Saved/Movement/player_probe.jsonl
상태: 현행 (2026-09-30)
"""
import json
import math
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools"))
import uemcp  # noqa: E402

PROBE_SCRIPT = ROOT / "Tools/Movement/editor_pie_player_probe.py"
PROBE_FILE = ROOT / "Saved/Movement/player_probe.jsonl"
REPORT = ROOT / "Docs/Validation/Movement/player-pie-check.json"
SHOTS = ["player_pie_walk.png", "player_pie_attack.png"]
LEVEL_PATH = "/Game/Combat/Maps/LV_TDMegaMagicArena"
MOVE_OFFSET = (700.0, 300.0)
ARRIVAL_TOLERANCE_CM = 50.0
FACING_TOLERANCE_DEGREES = 10.0
MIN_FOOT_SWING_CM = 10.0
MIN_JUMP_HEIGHT_CM = 20.0
ATTACK_DISPLACEMENT_CM = (32.0, 40.0)
NAVIGATION_WAIT_SECONDS = 60.0
EDITOR_NAVIGATION_WAIT_SECONDS = 300.0


def call(session, name, arguments):
    response = uemcp.call_tool(session, "call_tool", {"toolset_name": "EditorToolset.EditorAppToolset", "tool_name": name, "arguments": arguments})
    if not response or "error" in response or response.get("result", {}).get("isError"):
        raise RuntimeError(f"{name}: {response}")
    return response["result"]


def probe(expression):
    code = f"exec(open(r'{PROBE_SCRIPT}', encoding='utf-8').read()); {expression}"
    completed = subprocess.run([sys.executable, str(ROOT / "Tools/run_in_editor.py"), "-c", code], capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=ROOT)
    if completed.returncode != 0:
        raise RuntimeError(completed.stdout[-1500:] + completed.stderr[-500:])


def read_records():
    if not PROBE_FILE.exists():
        return []
    return [json.loads(line) for line in PROBE_FILE.read_text(encoding="utf-8").splitlines() if line.strip()]


def planar_distance(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def by_prefix(records, prefix):
    return [record for record in records if record["tag"].startswith(prefix) and "player" in record]


def summarize_move(records, goal):
    samples = by_prefix(records, "move_")
    order = by_prefix(records, "move")[0]
    moving = [record for record in samples if math.hypot(*record["player"]["velocity"][:2]) > 100.0]
    swing = 0.0
    if moving:
        foot_x = [record["player"]["feet"]["foot_l"][0] for record in moving]
        swing = max(foot_x) - min(foot_x)
    final = samples[-1]["player"]
    start = by_prefix(records, "start")[0]["player"]
    travel = (final["location"][0] - start["location"][0], final["location"][1] - start["location"][1])
    travel_yaw = math.degrees(math.atan2(travel[1], travel[0]))
    facing_error = abs((final["yaw"] - travel_yaw + 180.0) % 360.0 - 180.0)
    return {
        "arrival_error_cm": round(planar_distance(final["location"], goal), 2),
        "moving_samples": len(moving),
        "foot_swing_cm": round(swing, 2),
        "facing_error_deg": round(facing_error, 2),
        "modes": sorted({record["player"]["mode"] for record in samples}),
        "path_states": sorted({str(record["player"].get("path")) for record in samples}),
        "level": order.get("level"),
        "samples": len(samples),
    }


def summarize_jump(records):
    samples = by_prefix(records, "jump_")
    before = by_prefix(records, "pre_jump")[0]["player"]
    heights = [record["player"]["location"][2] - before["location"][2] for record in samples]
    order = [record for record in records if record["tag"] == "jump"][0]
    return {
        "jump_accepted": order.get("result"),
        "max_height_cm": round(max(heights), 2) if heights else 0.0,
        "airborne_seen": any(record["player"]["airborne"] for record in samples),
        "landed": samples[-1]["player"]["on_ground"] if samples else False,
        "modes": sorted({record["player"]["mode"] for record in samples}),
    }


def summarize_attack(records):
    before = by_prefix(records, "pre_attack")[0]["player"]
    after = by_prefix(records, "attack_")[-1]["player"]
    return {"displacement_cm": round(planar_distance(before["location"], after["location"]), 2)}


def wait_for_editor_navigation():
    check = ("import unreal; world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world(); "
             "print('TD_NAV_BUILDING', unreal.NavigationSystemV1.is_navigation_being_built(world))")
    started = time.monotonic()
    while time.monotonic() - started < EDITOR_NAVIGATION_WAIT_SECONDS:
        completed = subprocess.run([sys.executable, str(ROOT / "Tools/run_in_editor.py"), "-c", check], capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=ROOT)
        if "TD_NAV_BUILDING False" in completed.stdout:
            return
        time.sleep(5.0)


def main():
    if PROBE_FILE.exists():
        PROBE_FILE.unlink()
    open_level = ("import unreal; editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem); "
                  "levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem); "
                  "world = editor.get_editor_world(); "
                  f"ok = world.get_path_name().startswith('{LEVEL_PATH}') or levels.load_level('{LEVEL_PATH}'); "
                  "print('level', editor.get_editor_world().get_path_name(), ok)")
    subprocess.run([sys.executable, str(ROOT / "Tools/run_in_editor.py"), "-c", open_level], check=True, cwd=ROOT)
    wait_for_editor_navigation()
    session = uemcp.connect()
    call(session, "StartPIE", {"options": {"warmupSeconds": 2.0}})
    try:
        probe("probe('snapshot', 'start')")
        start = read_records()[-1]["player"]["location"]
        goal = (start[0] + MOVE_OFFSET[0], start[1] + MOVE_OFFSET[1])
        navigation_wait_start = time.monotonic()
        while time.monotonic() - navigation_wait_start < NAVIGATION_WAIT_SECONDS:
            probe(f"probe('path_ready', 'path_ready', x={goal[0]}, y={goal[1]})")
            if read_records()[-1].get("result"):
                break
            time.sleep(1.0)
        navigation_wait_seconds = round(time.monotonic() - navigation_wait_start, 1)
        probe(f"probe('move_to', 'move', x={goal[0]}, y={goal[1]}, record_seconds=3.0)")
        time.sleep(0.6)
        probe("probe('snapshot', 'walk_shot', shot='player_pie_walk.png')")
        time.sleep(3.0)
        probe("probe('snapshot', 'pre_jump')")
        probe("probe('jump', 'jump', record_seconds=1.6)")
        time.sleep(2.2)
        probe("probe('snapshot', 'pre_attack')")
        probe("probe('console', 'attack', command='TDPlayMeleeAction', record_seconds=2.2)")
        time.sleep(0.45)
        probe("probe('snapshot', 'attack_shot', shot='player_pie_attack.png')")
        time.sleep(2.3)
        probe("probe('snapshot', 'attack_end')")
    finally:
        call(session, "StopPIE", {})

    records = read_records()
    report = {
        "navigation_wait_seconds": navigation_wait_seconds,
        "path_ready": any(record.get("result") for record in records if record["tag"] == "path_ready"),
        "pawn_class": records[0]["player"].get("class"),
        "mesh_animation_enabled": records[0]["player"].get("mesh_animation_enabled"),
        "move": summarize_move(records, goal),
        "jump": summarize_jump(records),
        "attack": summarize_attack(records),
    }
    report["passed"] = (report["path_ready"] and report["move"]["arrival_error_cm"] <= ARRIVAL_TOLERANCE_CM
                        and report["move"]["facing_error_deg"] <= FACING_TOLERANCE_DEGREES
                        and report["move"]["foot_swing_cm"] >= MIN_FOOT_SWING_CM
                        and report["jump"]["max_height_cm"] >= MIN_JUMP_HEIGHT_CM and report["jump"]["landed"]
                        and ATTACK_DISPLACEMENT_CM[0] <= report["attack"]["displacement_cm"] <= ATTACK_DISPLACEMENT_CM[1])
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    shots_dir = ROOT / "Saved/Screenshots/WindowsEditor"
    for name in SHOTS:
        source = shots_dir / name
        if source.exists():
            shutil.copy(source, REPORT.parent / name)
    report["records"] = records
    REPORT.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items() if key != "records"}, indent=2, ensure_ascii=False))
    if not report["passed"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
