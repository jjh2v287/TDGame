"""시스템 Python으로 실행(언리얼 에디터에 LV-Cambat 열림 필요): PIE를 켜고 플레이어를 하이에나 무리 옆으로 옮겨 교전·예고·타격을 기록한 뒤, 플레이어 주문으로 몬스터를 공격해 경직·사망을 기록하고 PIE를 끈다.
실행: python Tools/MonsterAI/pie_check_monsters.py
출력: Docs/MonsterAI_CombatSim/measurements/monster-pie-check.json(+ .png 스크린샷 2장), 중간 기록 Saved/MonsterAI/pie_probe.jsonl
상태: 현행
"""
import json
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools"))
import uemcp  # noqa: E402

PROBE_SCRIPT = ROOT / "Tools/MonsterAI/editor_pie_monster_probe.py"
PROBE_FILE = ROOT / "Saved/MonsterAI/pie_probe.jsonl"
REPORT = ROOT / "Docs/MonsterAI_CombatSim/measurements/monster-pie-check.json"
SHOTS = ["monster_pie_engage.png", "monster_pie_fight.png"]


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


def count_state(record, state):
    return sum(1 for line in record["monsters"]["lines"] if f"state={state}" in line)


def count_engaged(record):
    return sum(1 for line in record["monsters"]["lines"] if "engaged=1" in line)


def main():
    if PROBE_FILE.exists():
        PROBE_FILE.unlink()
    open_level = ("import unreal; editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem); "
                  "levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem); "
                  "world = editor.get_editor_world(); "
                  "ok = world.get_path_name().startswith('/Game/Level/LV-Cambat') or levels.load_level('/Game/Level/LV-Cambat'); "
                  "print('level', editor.get_editor_world().get_path_name(), ok)")
    subprocess.run([sys.executable, str(ROOT / "Tools/run_in_editor.py"), "-c", open_level], check=True, cwd=ROOT)
    session = uemcp.connect()
    call(session, "StartPIE", {"options": {"warmupSeconds": 2.0}})
    states_seen = set()
    try:
        probe("probe('snapshot', 'start')")
        probe("probe('console', 'TD.MonsterAI.DebugDraw 1')")
        probe("probe('console', 'TDSetCasterLevel 10')")
        probe("probe('teleport', 'near_hyenas', x=1250.0, y=0.0)")
        for index in range(6):
            time.sleep(0.45)
            probe(f"probe('snapshot', 'engage_{index}'" + (", shot='monster_pie_engage.png'" if index == 3 else "") + ")")
        for index in range(24):
            time.sleep(0.45)
            if index % 2 == 0:
                probe(f"probe('cast', 'cast_{index}', slot={(index // 2) % 5})")
                continue
            probe(f"probe('snapshot', 'fight_{index}'" + (", shot='monster_pie_fight.png'" if index == 5 else "") + ")")
        time.sleep(1.0)
        probe("probe('snapshot', 'end')")
    finally:
        call(session, "StopPIE", {})

    records = read_records()
    for record in records:
        for line in record.get("monsters", {}).get("lines", []):
            match = re.search(r"state=(\w+)", line)
            if match:
                states_seen.add(match.group(1))
    start = records[0]
    end = records[-1]
    engage_records = [r for r in records if r["tag"].startswith("engage_")]
    report = {
        "monsters_registered_at_start": start["monsters"]["active"],
        "monster_actors": start["monsters"]["actors"],
        "max_engaged": max(count_engaged(r) for r in records),
        "max_casting": max(count_state(r, "Cast") for r in records),
        "states_seen": sorted(states_seen),
        "player_health_start": start["player"].get("health"),
        "player_health_min": min(r["player"].get("health") or 0 for r in records),
        "dead_monsters_max": max(r["monsters"]["dead"] for r in records),
        "monsters_removed": start["monsters"]["active"] - min(r["monsters"]["active"] for r in records),
        "active_monsters_end": end["monsters"]["active"],
        "engage_samples": len(engage_records),
    }
    report["passed"] = (report["monsters_registered_at_start"] >= 10 and report["max_engaged"] >= 3 and "Cast" in states_seen
                        and "Move" in states_seen and report["player_health_min"] < report["player_health_start"] and (report["dead_monsters_max"] >= 1 or report["monsters_removed"] >= 1))
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
