"""실행 환경: 시스템 Python(언리얼 에디터가 TDGame을 열고 MCP 포트 8000·원격 실행이 켜진 상태), 에디터 쪽 측정은 editor_pie_fluid_probe.py가 PIE 안에서 프레임마다 진행한다.
실행 명령: python Tools/FluidFX/pie_check_fluid.py (PowerShell, 에디터 앞창 유지 권장: 백그라운드 PIE는 3fps로 제한된다 L-editor-06)
출력: Saved/FluidFX/fluid_pie_probe.json(항목 8개 판정), Docs/Validation/FluidFX/D-48-pie-gameview-<날짜>.png(PIE 게임 화면 HighResShot), 종료 코드 0 전부 통과/1 항목 실패/2 환경 문제
상태: 현행(2026-10-18 첫 성공, 증거 Docs/Validation/FluidFX/D-48-*)
"""
import json
import subprocess
import sys
import time
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools"))
import uemcp

RUN_IN_EDITOR = ROOT / "Tools" / "run_in_editor.py"
PROBE_SCRIPT = ROOT / "Tools" / "FluidFX" / "editor_pie_fluid_probe.py"
OUTPUT_DIRECTORY = ROOT / "Saved" / "FluidFX"
REPORT_PATH = OUTPUT_DIRECTORY / "fluid_pie_probe.json"
REQUEST_PATH = OUTPUT_DIRECTORY / "fluid_pie_probe_request.json"
CAPTURE_FLAG_PATH = OUTPUT_DIRECTORY / "fluid_pie_capture_done.flag"
CAPTURE_IMAGE_PATH = ROOT / "Docs" / "Validation" / "FluidFX" / f"D-48-pie-gameview-{date.today().isoformat()}.png"
CAPTURE_SIZE = "1920x1080"
CAPTURE_FILE_WAIT_SECONDS = 20.0

LEVEL_PATH = "/Game/FluidFX/Maps/LV_TDFluidTestArena"
RESULT_MARKER = "TD_FLUID_PROBE_RESULT"
NAVIGATION_STATE_MARKER = "TD_NAV_STATE"
ITEM_CAPTURE = "7_topdown_capture"
ITEM_PIE_ENDED = "8_pie_ended"
ITEM_COUNT = 8


EDITOR_NAVIGATION_WAIT_SECONDS = 120.0
EDITOR_NAVIGATION_POLL_SECONDS = 5.0
EDITOR_NAVIGATION_READY_CHECKS = 2
PIE_START_WAIT_SECONDS = 120.0
PIE_STOP_WAIT_SECONDS = 60.0
PROBE_TIMEOUT_SECONDS = 420.0
PROBE_POLL_SECONDS = 1.0
EXIT_ITEM_FAILED = 1
EXIT_ENVIRONMENT = 2


def run_editor_code(code):
    return subprocess.run([sys.executable, str(RUN_IN_EDITOR), "-c", code], capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=ROOT)


def parse_marker(completed, marker):
    for line in completed.stdout.splitlines():
        marker_index = line.find(marker)
        if marker_index >= 0:
            return json.loads(line[marker_index + len(marker):])
    return {"error": "결과 표식이 없다", "returncode": completed.returncode, "output_tail": (completed.stdout[-600:] + completed.stderr[-300:])}


def perform_probe_action(action, **fields):
    OUTPUT_DIRECTORY.mkdir(parents=True, exist_ok=True)
    REQUEST_PATH.write_text(json.dumps({"action": action, **fields}), encoding="utf-8")
    try:
        completed = subprocess.run([sys.executable, str(RUN_IN_EDITOR), str(PROBE_SCRIPT)], capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=ROOT)
    finally:
        REQUEST_PATH.unlink(missing_ok=True)
    return parse_marker(completed, RESULT_MARKER)


def call_editor_tool(session, name, arguments):
    response = uemcp.call_tool(session, "call_tool", {"toolset_name": "EditorToolset.EditorAppToolset", "tool_name": name, "arguments": arguments})
    if not response or "error" in response or response.get("result", {}).get("isError"):
        raise RuntimeError(f"{name}: {str(response)[:400]}")
    return response["result"]


def open_arena_level():
    code = ("import unreal, json; "
            "editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem); "
            "levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem); "
            f"exists = unreal.EditorAssetLibrary.does_asset_exist('{LEVEL_PATH}'); "
            "pie_running = editor.get_game_world() is not None; "
            f"loaded = exists and not pie_running and (editor.get_editor_world().get_path_name().startswith('{LEVEL_PATH}') or levels.load_level('{LEVEL_PATH}')); "
            f"print('{RESULT_MARKER}', json.dumps({{'exists': exists, 'pie_running': pie_running, 'loaded': bool(loaded), 'world': editor.get_editor_world().get_path_name()}}))")
    return parse_marker(run_editor_code(code), RESULT_MARKER)


def read_editor_navigation_state():
    code = ("import unreal, json; world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world(); "
            "starts = [start.get_actor_location() for start in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PlayerStart)]; "
            "extent = unreal.Vector(50.0, 50.0, 250.0); "
            f"print('{NAVIGATION_STATE_MARKER}', json.dumps({{"
            "'navmesh_actors': len(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RecastNavMesh)), "
            "'building_or_locked': unreal.NavigationSystemV1.is_navigation_being_built_or_locked(world), "
            "'player_start_found': len(starts) > 0, "
            "'projected': [unreal.NavigationSystemV1.project_point_to_navigation(world, point, None, None, extent) is not None for point in starts]}))")
    return parse_marker(run_editor_code(code), NAVIGATION_STATE_MARKER)


def is_navigation_ready(state):
    return (state.get("navmesh_actors", 0) > 0 and state.get("building_or_locked") is False
            and state.get("player_start_found") is True and all(state.get("projected", [False])))


def request_editor_navigation_rebuild():
    code = ("import unreal; world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world(); "
            "unreal.SystemLibrary.execute_console_command(world, 'RebuildNavigation')")
    run_editor_code(code)


def wait_for_editor_navigation():
    started = time.monotonic()
    ready_checks = 0
    has_requested_rebuild = False
    while time.monotonic() - started < EDITOR_NAVIGATION_WAIT_SECONDS:
        state = read_editor_navigation_state()
        ready_checks = ready_checks + 1 if is_navigation_ready(state) else 0
        if not ready_checks and not has_requested_rebuild and not state.get("building_or_locked"):
            request_editor_navigation_rebuild()
            has_requested_rebuild = True
            print(NAVIGATION_STATE_MARKER, "RebuildNavigation 요청(새 맵은 에디터 내비가 비어 있을 수 있음)", flush=True)
        print(NAVIGATION_STATE_MARKER, round(time.monotonic() - started, 1), json.dumps(state, ensure_ascii=False), flush=True)
        if ready_checks >= EDITOR_NAVIGATION_READY_CHECKS:
            return True
        time.sleep(EDITOR_NAVIGATION_POLL_SECONDS)
    return False


def wait_for_pie_state(should_be_running, timeout_seconds):
    started = time.monotonic()
    while time.monotonic() - started < timeout_seconds:
        status = perform_probe_action("status")
        if status.get("pie_running") is should_be_running:
            return True
        time.sleep(2.0)
    return False


def read_report():
    if not REPORT_PATH.exists():
        return None
    try:
        return json.loads(REPORT_PATH.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


def capture_topdown(session):
    CAPTURE_IMAGE_PATH.parent.mkdir(parents=True, exist_ok=True)
    CAPTURE_IMAGE_PATH.unlink(missing_ok=True)
    target = CAPTURE_IMAGE_PATH.as_posix()
    code = ("import unreal; world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(); "
            f"unreal.SystemLibrary.execute_console_command(world, 'HighResShot {CAPTURE_SIZE} filename={target}') if world else None; "
            f"print('{RESULT_MARKER}', world is not None)")
    run_editor_code(code)
    deadline = time.monotonic() + CAPTURE_FILE_WAIT_SECONDS
    while time.monotonic() < deadline:
        if CAPTURE_IMAGE_PATH.exists() and CAPTURE_IMAGE_PATH.stat().st_size > 0:
            time.sleep(0.5)
            return {"saved": True, "path": CAPTURE_IMAGE_PATH.relative_to(ROOT).as_posix(), "bytes": CAPTURE_IMAGE_PATH.stat().st_size, "source": "PIE HighResShot"}
        time.sleep(0.5)
    return {"saved": False, "note": f"HighResShot PNG가 {CAPTURE_FILE_WAIT_SECONDS:.0f}초 안에 생기지 않았다"}


def wait_for_probe(session):
    started = time.monotonic()
    capture_outcome = {"saved": False, "note": "캡처 단계에 도달하지 못했다"}
    capture_attempted = False
    while time.monotonic() - started < PROBE_TIMEOUT_SECONDS:
        report = read_report()
        if report is not None:
            if report.get("stage") == "awaiting_capture" and not capture_attempted:
                capture_attempted = True
                capture_outcome = capture_topdown(session)
                CAPTURE_FLAG_PATH.write_text("done", encoding="utf-8")
            if report.get("state") in ("complete", "failed"):
                return report, capture_outcome
        time.sleep(PROBE_POLL_SECONDS)
    return read_report(), capture_outcome


def finalize_report(report, capture_outcome, pie_ended, environment):
    report = report or {"tool": "pie_check_fluid", "state": "failed", "items": {}, "errors": ["에디터 쪽 보고서를 읽지 못했다"]}
    items = report.setdefault("items", {})
    items[ITEM_CAPTURE] = {"passed": bool(capture_outcome.get("saved")), "criterion": items.get(ITEM_CAPTURE, {}).get("criterion", "위에서 내려다본 뷰포트 캡처 PNG가 Docs/Validation/FluidFX/에 저장된다"),
                           "value": capture_outcome, "note": capture_outcome.get("note", "")}
    items[ITEM_PIE_ENDED] = {"passed": pie_ended, "criterion": items.get(ITEM_PIE_ENDED, {}).get("criterion", "측정이 끝난 뒤 PIE가 종료되어 게임 월드가 없다"),
                             "value": {"pie_running_after_stop": not pie_ended}, "note": "" if pie_ended else f"{PIE_STOP_WAIT_SECONDS:.0f}초 안에 PIE가 종료되지 않았다"}
    report["environment"] = environment
    report["passed"] = (report.get("state") == "complete" and not report.get("errors")
                        and len(items) >= ITEM_COUNT and all(item.get("passed") for item in items.values()))
    OUTPUT_DIRECTORY.mkdir(parents=True, exist_ok=True)
    REPORT_PATH.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    return report


def print_summary(report):
    for key, item in sorted(report.get("items", {}).items()):
        print(("PASS" if item.get("passed") else "FAIL"), key, item.get("note", ""))
    print("state", report.get("state"), "passed", report.get("passed"), "report", REPORT_PATH.relative_to(ROOT).as_posix())
    for error in report.get("errors", []):
        print("error", error[:300])


def start_pie(session, environment):
    try:
        call_editor_tool(session, "StartPIE", {"options": {"bSimulate": False, "warmupSeconds": 2.0}})
    except RuntimeError as error:
        environment["start_pie_error"] = str(error)
        return False
    return True


def stop_pie(session, environment):
    try:
        call_editor_tool(session, "StopPIE", {})
    except RuntimeError as error:
        environment["stop_pie_error"] = str(error)
    return wait_for_pie_state(False, PIE_STOP_WAIT_SECONDS)


def run_measurement(session, environment):
    if not wait_for_pie_state(True, PIE_START_WAIT_SECONDS):
        environment["pie_world_error"] = "PIE 게임 월드가 나타나지 않았다"
        return None, {"saved": False, "note": "PIE 게임 월드가 나타나지 않았다"}
    REPORT_PATH.unlink(missing_ok=True)
    start_result = perform_probe_action("start", capture_mode="driver")
    environment["start"] = start_result
    print("start", json.dumps(start_result, ensure_ascii=False), flush=True)
    if start_result.get("started") is not True:
        return read_report(), {"saved": False, "note": "에디터 쪽 측정이 시작되지 않았다"}
    return wait_for_probe(session)


def main():
    environment = {}
    level_status = open_arena_level()
    environment["level"] = level_status
    if not level_status.get("loaded"):
        print("레벨을 열지 못했다:", json.dumps(level_status, ensure_ascii=False))
        return EXIT_ENVIRONMENT
    try:
        session = uemcp.connect()
    except OSError as error:
        print("MCP 서버(127.0.0.1:8000)에 연결하지 못했다:", error)
        return EXIT_ENVIRONMENT
    environment["editor_navigation_ready"] = wait_for_editor_navigation()
    CAPTURE_FLAG_PATH.unlink(missing_ok=True)
    environment["prepare"] = perform_probe_action("prepare")
    print("prepare", json.dumps(environment["prepare"], ensure_ascii=False), flush=True)
    report = None
    capture_outcome = {"saved": False, "note": "PIE를 시작하지 못했다"}
    pie_started = False
    try:
        pie_started = start_pie(session, environment)
        if pie_started:
            report, capture_outcome = run_measurement(session, environment)
    finally:
        pie_ended = stop_pie(session, environment) if pie_started else wait_for_pie_state(False, PIE_STOP_WAIT_SECONDS)
        environment["cleanup"] = perform_probe_action("cleanup") if pie_ended else {"error": "PIE가 아직 켜져 있어 정리하지 않았다"}
        CAPTURE_FLAG_PATH.unlink(missing_ok=True)
    report = finalize_report(report, capture_outcome, pie_ended, environment)
    print_summary(report)
    return 0 if report["passed"] else EXIT_ITEM_FAILED


if __name__ == "__main__":
    sys.exit(main())
