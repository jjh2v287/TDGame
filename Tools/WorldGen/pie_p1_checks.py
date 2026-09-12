"""에디터 밖에서 실행: PIE를 켜고 P1-05(걸어 들어가기 왕복 ×3)와 P1-06/07(영속 상자 열림 상태가 스트리밍·세이브/로드를 견디는지)을 자동으로 돌린다.

절차: MCP StartPIE → 내비메시 생성 대기 → [approach → enter → where → exit → where] ×3 → 상자 status/open/save 1 → 던전 왕복 → status → load 1 → status → StopPIE.
결과는 Saved/WorldGen/pie_p1_checks.md 에 기록한다.
실행: python Tools/WorldGen/pie_p1_checks.py [--rounds 3] [--settle 20]
"""
import argparse
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


def run(script, step):
    os.makedirs(os.path.dirname(STEP), exist_ok=True)
    with open(STEP, "w", encoding="utf-8") as f:
        f.write(step)
    res = subprocess.run([sys.executable, os.path.join(TOOLS, "run_in_editor.py"), os.path.join(TOOLS, "WorldGen", script)], capture_output=True, text=True, encoding="utf-8", errors="replace")
    text = "\n".join(l.replace("[Info] ", "").strip() for l in res.stdout.splitlines() if l.strip() and l.strip() != "SUCCESS")
    log(f"- `{step}` → {text.replace(chr(10), ' | ')}")
    return text


def pawn_x(text):
    m = re.search(r"pawn: .*?x: (-?[0-9.]+)", text)
    return float(m.group(1)) if m else float("nan")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rounds", type=int, default=3)
    ap.add_argument("--settle", type=float, default=20.0)
    ap.add_argument("--travel-wait", type=float, default=14.0)
    a = ap.parse_args()
    log(f"# PIE P1 검증 ({time.strftime('%Y-%m-%d %H:%M')})")
    log(str(pie("StartPIE"))[:160])
    if not wait_for_pie(90):
        log("PIE did not start; abort")
        return
    time.sleep(a.settle)
    log("## P1-05 걸어 들어가기 왕복")
    for r in range(1, a.rounds + 1):
        log(f"### round {r}")
        run("pie_walkin_check.py", "approach MainCrypt")
        time.sleep(3)
        run("pie_walkin_check.py", "enter MainCrypt")
        time.sleep(a.travel_wait)
        entered = run("pie_travel_check.py", "where")
        run("pie_walkin_check.py", "exit MainCrypt")
        time.sleep(a.travel_wait)
        returned = run("pie_travel_check.py", "where")
        log(f"- 판정: 입장 {'OK' if pawn_x(entered) > 250000 else 'FAIL'} / 귀환 {'OK' if pawn_x(returned) < 100000 else 'FAIL'}")
    log("## P1-06/07 영속 상자")
    run("pie_chest_check.py", "chest status")
    run("pie_chest_check.py", "chest open")
    run("pie_chest_check.py", "chest save 1")
    time.sleep(2)
    run("pie_travel_check.py", "to MainCrypt")
    time.sleep(a.travel_wait)
    run("pie_travel_check.py", "field")
    time.sleep(a.travel_wait)
    run("pie_chest_check.py", "chest status")
    run("pie_chest_check.py", "chest load 1")
    time.sleep(2)
    run("pie_chest_check.py", "chest status")
    log(str(pie("StopPIE"))[:160])
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(LINES) + "\n")
    print("written:", OUT)


if __name__ == "__main__":
    main()
