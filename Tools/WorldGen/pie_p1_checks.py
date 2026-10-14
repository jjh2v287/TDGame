"""에디터 밖에서 실행: PIE를 켜고 목적지 ID마다 걸어 들어가기 왕복 ×rounds(P1-05, P3-13 이계 관문 포함)와 P1-06/07(영속 상자 열림 상태가 스트리밍·세이브/로드를 견디는지)을 자동으로 돌린다.
실행: python Tools/WorldGen/pie_p1_checks.py [--ids MainCrypt,SunkenStarfieldSouthwest] [--rounds 3] [--settle 20] [--travel-wait 90] (PowerShell 또는 Git Bash, 에디터 실행 중)
출력: Saved/WorldGen/pie_p1_checks.md (종료 코드 0 = 모든 판정 OK, 1 = FAIL 있음)
상태: 현행

절차: MCP StartPIE → PIE 대기 → 목적지마다 [approach → enter(입구가 없으면 선로딩 취소 뒤 콘솔 이동) → 도착 대기·판정 → exit(출구가 없으면 선로딩 취소 뒤 콘솔 귀환) → 귀환 대기·판정] ×rounds → 상자 status/open/save 1 → 첫 목적지 콘솔 왕복 → status → load 1 → status → StopPIE.
판정: 이계 관문 ID(Saved/WorldGen/Realms/*/layout.json realm.gates)면 폰 XY가 그 이계 상자 안, 아니면(아틀라스 던전) X > 250000이고, 마지막 이동 ID가 읽히면 목적지 ID와 같아야 한다. 귀환은 필드 상자 |X|,|Y| ≤ 60000. 도착 대기는 이동 상태가 TRAVELING·SETTLING이 아니고 판정이 맞을 때까지 --travel-wait 초 안에서 3초마다 확인한다. 관문 근처에 도착하면 입구가 선로딩을 다시 걸어 상태가 PRELOADING·READY_TO_TRAVEL에 머물므로 IDLE을 요구하지 않는다.
"""
import argparse
import glob
import json
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOLS = os.path.join(ROOT, "Tools")
sys.path.insert(0, TOOLS)
import uemcp  # noqa: E402

STEP = os.path.join(ROOT, "Saved", "WorldGen", "pie_travel_step.txt")
OUT = os.path.join(ROOT, "Saved", "WorldGen", "pie_p1_checks.md")
REALM_LAYOUTS = os.path.join(ROOT, "Saved", "WorldGen", "Realms", "*", "layout.json")
FIELD_HALF_EXTENT_CM = 60000.0
DUNGEON_MIN_X_CM = 250000.0
POLL_SECONDS = 3.0
MOVING_STATES = ("TRAVELING", "SETTLING")
PAWN_PATTERN = re.compile(r"pawn_cm: x=(-?[0-9.]+) y=(-?[0-9.]+) z=(-?[0-9.]+) state=(\S+) last=(\S+)")
LINES = []


def log(msg):
    print(msg, flush=True)
    LINES.append(msg)


def pie(tool):
    sid = uemcp.connect()
    args = {"options": {"bSimulate": False}} if tool == "StartPIE" else {}
    return uemcp.call_tool(sid, "call_tool", {"toolset_name": "EditorToolset.EditorAppToolset", "tool_name": tool, "arguments": args})


def wait_for_pie(timeout):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if "PIE world not running" not in run("pie_travel_check.py", "where"):
            return True
        time.sleep(5)
    return False


def run(script, step, quiet=False):
    os.makedirs(os.path.dirname(STEP), exist_ok=True)
    with open(STEP, "w", encoding="utf-8") as f:
        f.write(step)
    res = subprocess.run([sys.executable, os.path.join(TOOLS, "run_in_editor.py"), os.path.join(TOOLS, "WorldGen", script)], capture_output=True, text=True, encoding="utf-8", errors="replace")
    text = "\n".join(l.replace("[Info] ", "").strip() for l in res.stdout.splitlines() if l.strip() and l.strip() != "SUCCESS")
    if not quiet:
        log(f"- `{step}` → {text.replace(chr(10), ' | ')}")
    return text


def load_realm_gate_boxes():
    boxes = {}
    for path in sorted(glob.glob(REALM_LAYOUTS)):
        with open(path, encoding="utf-8") as f:
            layout = json.load(f)
        realm = layout.get("realm") or {}
        center = realm.get("center_cm")
        half = realm.get("half_extent_cm")
        if center is None or half is None:
            continue
        half_x, half_y = (half, half) if isinstance(half, (int, float)) else (half[0], half[1])
        for gate in realm.get("gates") or layout.get("gates") or []:
            boxes[gate["gate_id"]] = (realm.get("realm_id"), center[0], center[1], half_x, half_y)
    return boxes


def parse_pawn(text):
    m = PAWN_PATTERN.search(text)
    if m is None:
        return None
    return {"x": float(m.group(1)), "y": float(m.group(2)), "z": float(m.group(3)), "state": m.group(4), "last": m.group(5)}


def is_inside_destination(destination_id, pawn, boxes):
    if pawn["last"] not in (destination_id, "None"):
        return False
    box = boxes.get(destination_id)
    if box is None:
        return pawn["x"] > DUNGEON_MIN_X_CM
    _, center_x, center_y, half_x, half_y = box
    return abs(pawn["x"] - center_x) <= half_x and abs(pawn["y"] - center_y) <= half_y


def is_inside_field(pawn):
    return abs(pawn["x"]) <= FIELD_HALF_EXTENT_CM and abs(pawn["y"]) <= FIELD_HALF_EXTENT_CM


def wait_for_arrival(is_arrived, timeout):
    t0 = time.time()
    while True:
        text = run("pie_walkin_check.py", "where", quiet=True)
        pawn = parse_pawn(text)
        arrived = pawn is not None and pawn["state"] not in MOVING_STATES and is_arrived(pawn)
        if arrived or time.time() - t0 >= timeout:
            log(f"- `where` ({time.time() - t0:.0f}s) → {text.replace(chr(10), ' | ')}")
            return arrived
        time.sleep(POLL_SECONDS)


def enter_destination(destination_id):
    approach = run("pie_walkin_check.py", f"approach {destination_id}")
    if "entrance not found" in approach:
        log("- 필드 입구 없음(D-41) → 선로딩 취소 뒤 콘솔 이동으로 대체")
        run("pie_walkin_check.py", f"to {destination_id}")
        return
    time.sleep(3)
    run("pie_walkin_check.py", f"enter {destination_id}")


def leave_destination(destination_id):
    leave = run("pie_walkin_check.py", f"exit {destination_id}")
    if "entrance not found" in leave:
        log("- 출구 없음 → 선로딩 취소 뒤 콘솔 귀환으로 대체")
        run("pie_walkin_check.py", "field")


def check_round_trip(destination_id, boxes, travel_wait):
    enter_destination(destination_id)
    entered = wait_for_arrival(lambda p: is_inside_destination(destination_id, p, boxes), travel_wait)
    leave_destination(destination_id)
    returned = wait_for_arrival(is_inside_field, travel_wait)
    log(f"- 판정 {destination_id}: 입장 {'OK' if entered else 'FAIL'} / 귀환 {'OK' if returned else 'FAIL'}")
    return entered and returned


def main():
    ap = argparse.ArgumentParser(description="PIE 걸어 들어가기 왕복(던전·이계 관문)과 영속 상자 검사")
    ap.add_argument("--ids", default="MainCrypt", help="쉼표로 구분한 목적지 ID(아틀라스 던전 ID 또는 이계 관문 ID)")
    ap.add_argument("--rounds", type=int, default=3)
    ap.add_argument("--settle", type=float, default=20.0)
    ap.add_argument("--travel-wait", type=float, default=90.0, help="도착·귀환을 기다리는 최대 초")
    a = ap.parse_args()
    ids = [i.strip() for i in a.ids.split(",") if i.strip()]
    if not ids:
        ap.error("--ids 가 비었다")
    boxes = load_realm_gate_boxes()
    log(f"# PIE P1 검증 ({time.strftime('%Y-%m-%d %H:%M')})")
    log(f"- 목적지: {ids} / 이계 관문 판정 상자: {sorted(k for k in boxes if k in ids)}")
    log(str(pie("StartPIE"))[:160])
    if not wait_for_pie(90):
        log("PIE did not start; abort")
        return 1
    time.sleep(a.settle)
    failures = []
    log("## P1-05 걸어 들어가기 왕복")
    for destination_id in ids:
        for r in range(1, a.rounds + 1):
            log(f"### {destination_id} round {r}")
            if not check_round_trip(destination_id, boxes, a.travel_wait):
                failures.append(f"{destination_id} round {r}")
    log("## P1-06/07 영속 상자")
    run("pie_chest_check.py", "chest status")
    run("pie_chest_check.py", "chest open")
    run("pie_chest_check.py", "chest save 1")
    time.sleep(2)
    run("pie_walkin_check.py", f"to {ids[0]}")
    wait_for_arrival(lambda p: is_inside_destination(ids[0], p, boxes), a.travel_wait)
    run("pie_walkin_check.py", "field")
    wait_for_arrival(is_inside_field, a.travel_wait)
    run("pie_chest_check.py", "chest status")
    run("pie_chest_check.py", "chest load 1")
    time.sleep(2)
    run("pie_chest_check.py", "chest status")
    log(str(pie("StopPIE"))[:160])
    log(f"## 요약: 왕복 FAIL {len(failures)}건 {failures}")
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(LINES) + "\n")
    print("written:", OUT)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
