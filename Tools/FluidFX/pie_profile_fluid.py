"""실행 환경: 시스템 Python(언리얼 에디터가 TDGame을 열고 MCP 포트 8000·원격 실행이 켜진 상태, C++ 콘솔 변수 TD.FluidFX.LogTickCost가 들어간 빌드), 에디터 쪽 조작은 이 파일의 EDITOR_CODE를 원격 실행으로 보낸다.
실행 명령: python Tools/FluidFX/pie_profile_fluid.py [--phase-seconds 10] (PowerShell, 측정 동안 에디터 창을 앞창으로 두기: 백그라운드 PIE는 3fps로 제한된다 L-editor-06)
출력: Saved/FluidFX/fluid_pie_perf.json, Docs/Validation/FluidFX/D-48-pie-perf-<날짜>.json(측정이 이뤄진 경우, 그날 파일이 이미 있으면 덮지 않고 -<시분초>를 붙임), 표준 출력 구간별 표. 종료 코드 0 전 구간에 TDFluidPerf 줄 있음·나이아가라 활성 토글 통과/1 줄 없는 구간·구간 오염·나이아가라 실패 있음/2 환경 문제(에디터·MCP·PIE·콘솔 변수)
상태: 현행(2026-10-18 첫 성공, 증거 Docs/Validation/FluidFX/D-48-perf-2026-10-18.md)
"""
import argparse
import json
import re
import sys
import time
from datetime import date, datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools"))
sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))
import pie_check_fluid
import run_in_editor
import uemcp

LOG_PATH = ROOT / "Saved" / "Logs" / "TDGame.log"
OUTPUT_PATH = ROOT / "Saved" / "FluidFX" / "fluid_pie_perf.json"
EVIDENCE_PATH = ROOT / "Docs" / "Validation" / "FluidFX" / f"D-48-pie-perf-{date.today().isoformat()}.json"

RUN_TOKEN = datetime.now().strftime("%Y%m%d%H%M%S")
EDITOR_RESULT_MARKER = "TD_FLUID_PERF_RESULT"
CONSOLE_VARIABLE_NAME = "TD.FluidFX.LogTickCost"
ORIGINAL_RESOLUTION_RULE = "original"

PHASE_SECONDS = 10.0
SETUP_TIMEOUT_SECONDS = 60.0
SETUP_POLL_SECONDS = 1.0
LOG_FLUSH_WAIT_SECONDS = 30.0
LOG_FLUSH_POLL_SECONDS = 1.0
SLOW_FRAME_MS = 100.0
TRANSITION_DROP_LINES = 2
NIAGARA_SYSTEM_PATH = "/Game/ArrowTrail/FX/NS_ArrowTrail_Water"
NIAGARA_SETTLE_SECONDS = 2.0
EXIT_MISSING_LINES = 1
EXIT_ENVIRONMENT = 2

PHASE_DEFINITIONS = (
    ("awake_res128", True, None),
    ("all_asleep", False, None),
    ("awake_res64", True, 64),
    ("awake_res256", True, 256),
    ("awake_res128_repeat", True, ORIGINAL_RESOLUTION_RULE),
)
PHASE_NAMES = tuple(name for name, _, _ in PHASE_DEFINITIONS)

FOREGROUND_WARNING = "에디터 창이 앞창이 아니면 PIE가 3fps로 제한되어 비용이 왜곡된다(L-editor-06). 측정 동안 UnrealEditor 창을 앞창으로 두고 건드리지 않는다."

PHASE_MARKER_PATTERN = re.compile(r"TD_FLUID_PERF_PHASE (\w+) (begin|end) run=" + re.escape(RUN_TOKEN) + r"\b")
PERF_LINE_PATTERN = re.compile(r"TDFluidPerf: frames=(\d+) avg_tick_ms=(\S+) max_tick_ms=(\S+) avg_frame_ms=(\S+) awake_actors=(\d+) total_cells=(\d+)")

EDITOR_CODE = '''
import json
import traceback

import unreal

PARAMS = json.loads(__PARAMS__)
RESULT_MARKER = "TD_FLUID_PERF_RESULT"
CONSOLE_VARIABLE = "TD.FluidFX.LogTickCost"


def get_game_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def collect_fluid_actors(world):
    return list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDFluidLiveActor))


def log_phase_marker(phase_name, edge):
    unreal.log("TD_FLUID_PERF_PHASE " + phase_name + " " + edge + " run=" + PARAMS["run_token"])


def read_console_variable():
    reader = getattr(unreal.SystemLibrary, "get_console_variable_int_value", None)
    if reader is None:
        return None
    return reader(CONSOLE_VARIABLE)


def describe_actors(actors):
    entries = []
    for actor in actors:
        component = actor.get_editor_property("live_component")
        configuration = component.get_editor_property("simulation_config")
        entries.append({
            "path": actor.get_path_name(),
            "label": actor.get_actor_label(),
            "resolution": configuration.get_editor_property("grid_resolution"),
            "awake": component.is_simulation_awake(),
            "has_runtime_texture": component.get_density_pressure_texture() is not None,
        })
    return entries


def run_setup(world):
    actors = collect_fluid_actors(world)
    if not actors:
        return {"error": "게임 월드에 TDFluidLiveActor가 없다"}
    entries = describe_actors(actors)
    if not all(entry["has_runtime_texture"] for entry in entries):
        return {"ready": False, "actors": entries}
    unreal.SystemLibrary.execute_console_command(world, CONSOLE_VARIABLE + " 1")
    for actor in actors:
        actor.set_editor_property("should_use_activation_volume", False)
    return {"ready": True, "console_variable_value": read_console_variable(), "actors": describe_actors(actors)}


def run_phase(world):
    previous_phase = PARAMS.get("previous_phase")
    if previous_phase:
        log_phase_marker(previous_phase, "end")
    log_phase_marker(PARAMS["phase"], "begin")
    actors = collect_fluid_actors(world)
    resolutions = PARAMS["resolutions"]
    for actor in actors:
        actor.set_editor_property("should_use_activation_volume", False)
        component = actor.get_editor_property("live_component")
        target_resolution = resolutions.get(actor.get_path_name())
        if target_resolution is not None:
            configuration = component.get_editor_property("simulation_config")
            configuration.set_editor_property("grid_resolution", int(target_resolution))
            component.apply_simulation_config(configuration)
        component.set_simulation_awake(PARAMS["awake"])
    return {"phase": PARAMS["phase"], "actors": describe_actors(actors)}


def run_end_phase(world):
    log_phase_marker(PARAMS["phase"], "end")
    return {"ended": PARAMS["phase"]}


def run_finish(world):
    previous_phase = PARAMS.get("previous_phase")
    if world is not None and previous_phase:
        log_phase_marker(previous_phase, "end")
    target_world = world
    if target_world is None:
        target_world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    unreal.SystemLibrary.execute_console_command(target_world, CONSOLE_VARIABLE + " 0")
    return {"console_variable_value": read_console_variable(), "used_game_world": world is not None}


def find_niagara_target(world):
    actors = [actor for actor in collect_fluid_actors(world) if not actor.get_editor_property("should_follow_player_pawn")]
    return actors[0] if actors else None


def collect_owned_niagara_states(actor):
    states = []
    for component in actor.get_components_by_class(unreal.NiagaraComponent):
        asset = component.get_asset()
        states.append({"component": component.get_name(), "asset": asset.get_path_name() if asset else None, "active": component.is_active()})
    return states


def run_niagara_attach(world):
    actor = find_niagara_target(world)
    if actor is None:
        return {"error": "나이아가라 검사 대상 유체 액터가 없다"}
    system = unreal.load_asset(PARAMS["niagara_system"])
    if system is None:
        return {"error": "나이아가라 시스템을 불러오지 못했다: " + PARAMS["niagara_system"]}
    component = actor.get_editor_property("live_component")
    current = component.get_editor_property("active_preset")
    preset = unreal.new_object(unreal.TDFluidPresetAsset)
    if current is not None:
        for name in ("preset_kind", "simulation_config", "default_channel", "output_material", "surface_mesh", "override_preset_defaults"):
            preset.set_editor_property(name, current.get_editor_property(name))
    preset.set_editor_property("external_niagara_system", system)
    component.init_system_state_from_preset(preset)
    component.set_simulation_awake(True)
    return {"actor": actor.get_actor_label(), "states": collect_owned_niagara_states(actor)}


def run_niagara_state(world):
    actor = find_niagara_target(world)
    if actor is None:
        return {"error": "나이아가라 검사 대상 유체 액터가 없다"}
    component = actor.get_editor_property("live_component")
    if PARAMS.get("awake") is not None:
        component.set_simulation_awake(bool(PARAMS["awake"]))
    return {"actor": actor.get_actor_label(), "awake": component.is_simulation_awake(), "states": collect_owned_niagara_states(actor)}


def run_action():
    world = get_game_world()
    action = PARAMS["action"]
    if action == "finish":
        return run_finish(world)
    if world is None:
        return {"error": "PIE 게임 월드가 없다"}
    handlers = {"setup": run_setup, "phase": run_phase, "end_phase": run_end_phase, "niagara_attach": run_niagara_attach, "niagara_state": run_niagara_state}
    handler = handlers.get(action)
    if handler is None:
        return {"error": "알 수 없는 action: " + action}
    return handler(world)


try:
    result = run_action()
except Exception:
    result = {"error": traceback.format_exc()[-600:]}
print(RESULT_MARKER, json.dumps(result, ensure_ascii=False, default=str))
'''


def run_editor_action(action, **fields):
    params = {"action": action, "run_token": RUN_TOKEN, **fields}
    code = EDITOR_CODE.replace("__PARAMS__", repr(json.dumps(params)))
    outcome = run_in_editor.run_code(code)
    if not outcome["connected"]:
        return {"error": "에디터 원격 실행에 연결하지 못했다: " + " | ".join(outcome["lines"])[-300:]}
    for line in outcome["lines"]:
        marker_index = line.find(EDITOR_RESULT_MARKER)
        if marker_index < 0:
            continue
        result_lines = line[marker_index + len(EDITOR_RESULT_MARKER):].strip().splitlines()
        try:
            return json.loads(result_lines[0])
        except (IndexError, ValueError):
            return {"error": "결과 표식 뒤 JSON을 읽지 못했다: " + line[marker_index:][:300]}
    return {"error": "결과 표식이 없다: " + " | ".join(outcome["lines"])[-400:]}


def read_log_size():
    if not LOG_PATH.exists():
        return 0
    return LOG_PATH.stat().st_size


def read_log_lines_since(offset):
    if not LOG_PATH.exists():
        return []
    start = offset if LOG_PATH.stat().st_size >= offset else 0
    with LOG_PATH.open("rb") as handle:
        handle.seek(start)
        return handle.read().decode("utf-8", errors="replace").splitlines()


def wait_for_end_marker(log_offset, last_phase_name):
    needle = f"TD_FLUID_PERF_PHASE {last_phase_name} end run={RUN_TOKEN}"
    deadline = time.monotonic() + LOG_FLUSH_WAIT_SECONDS
    while True:
        lines = read_log_lines_since(log_offset)
        if any(needle in line for line in lines) or time.monotonic() >= deadline:
            return lines
        time.sleep(LOG_FLUSH_POLL_SECONDS)


def parse_perf_line(text):
    match = PERF_LINE_PATTERN.search(text)
    if match is None:
        return None
    try:
        return {"frames": int(match.group(1)), "avg_tick_ms": float(match.group(2)), "max_tick_ms": float(match.group(3)),
                "avg_frame_ms": float(match.group(4)), "awake_actors": int(match.group(5)), "total_cells": int(match.group(6))}
    except ValueError:
        return None


def parse_phase_lines(log_lines):
    phases = {}
    current_phase = None
    for text in log_lines:
        marker = PHASE_MARKER_PATTERN.search(text)
        if marker is not None:
            current_phase = marker.group(1) if marker.group(2) == "begin" else None
            if current_phase is not None:
                phases.setdefault(current_phase, [])
            continue
        if current_phase is None:
            continue
        perf_line = parse_perf_line(text)
        if perf_line is not None:
            phases[current_phase].append(perf_line)
    return phases


def expected_editor_state(actor_entries):
    awake_entries = [entry for entry in actor_entries if entry["awake"]]
    return {"awake_actors": len(awake_entries), "total_cells": sum(entry["resolution"] ** 2 for entry in awake_entries)}


def summarize_phase(phase_record, perf_lines):
    summary = {"started": phase_record is not None, "lines": 0, "lines_total": len(perf_lines), "dropped_transition_lines": 0,
               "frames_total": None, "avg_tick_ms": None, "max_tick_ms": None, "avg_frame_ms": None,
               "awake_actors": None, "total_cells": None, "state_consistent": None, "matches_editor_state": None,
               "expected_editor_state": None, "driver_wait_seconds": None, "raw_lines": perf_lines}
    if phase_record is not None:
        summary["expected_editor_state"] = expected_editor_state(phase_record["actors"])
        summary["driver_wait_seconds"] = phase_record.get("driver_wait_seconds")
    drop_count = TRANSITION_DROP_LINES if len(perf_lines) > TRANSITION_DROP_LINES + 1 else min(1, len(perf_lines) - 1)
    kept_lines = perf_lines[max(drop_count, 0):]
    if not kept_lines:
        return summary
    summary["lines"] = len(kept_lines)
    summary["dropped_transition_lines"] = len(perf_lines) - len(kept_lines)
    summary["frames_total"] = sum(line["frames"] for line in kept_lines)
    frames_total = max(summary["frames_total"], 1)
    summary["avg_tick_ms"] = round(sum(line["avg_tick_ms"] * line["frames"] for line in kept_lines) / frames_total, 4)
    summary["max_tick_ms"] = round(max(line["max_tick_ms"] for line in kept_lines), 4)
    summary["avg_frame_ms"] = round(sum(line["avg_frame_ms"] * line["frames"] for line in kept_lines) / frames_total, 3)
    summary["awake_actors"] = kept_lines[-1]["awake_actors"]
    summary["total_cells"] = kept_lines[-1]["total_cells"]
    summary["state_consistent"] = len({(line["awake_actors"], line["total_cells"]) for line in kept_lines}) == 1
    expected = summary["expected_editor_state"]
    if expected is not None:
        summary["matches_editor_state"] = expected["awake_actors"] == summary["awake_actors"] and expected["total_cells"] == summary["total_cells"]
    return summary


def build_resolution_targets(resolution_rule, original_entries):
    if resolution_rule is None:
        return {}
    if resolution_rule == ORIGINAL_RESOLUTION_RULE:
        return {entry["path"]: entry["resolution"] for entry in original_entries}
    return {entry["path"]: resolution_rule for entry in original_entries}


def wait_for_fluid_setup():
    started = time.monotonic()
    result = {"error": "유체 액터 준비를 확인하지 못했다"}
    while time.monotonic() - started < SETUP_TIMEOUT_SECONDS:
        result = run_editor_action("setup")
        if "error" in result or result.get("ready"):
            return result
        time.sleep(SETUP_POLL_SECONDS)
    return {"error": f"{SETUP_TIMEOUT_SECONDS:.0f}초 안에 모든 유체 액터의 실행 중 텍스처가 준비되지 않았다", "last": result}


def run_phases(phase_seconds, report):
    previous_phase = None
    window_started = None
    for name, should_wake, resolution_rule in PHASE_DEFINITIONS:
        if previous_phase is not None:
            report["phases"][previous_phase]["driver_wait_seconds"] = round(time.monotonic() - window_started, 2)
        result = run_editor_action("phase", phase=name, previous_phase=previous_phase, awake=should_wake,
                                   resolutions=build_resolution_targets(resolution_rule, report["setup_actors"]))
        if "error" in result:
            return f"구간 {name} 전환 실패: {result['error']}"
        report["phases"][name] = {"awake": should_wake, "resolution_rule": resolution_rule, "actors": result["actors"]}
        report["last_started_phase"] = name
        print(f"phase {name} begin: awake={should_wake} resolution_rule={resolution_rule} {phase_seconds:.0f}초 대기", flush=True)
        previous_phase = name
        window_started = time.monotonic()
        time.sleep(phase_seconds)
    report["phases"][previous_phase]["driver_wait_seconds"] = round(time.monotonic() - window_started, 2)
    return None


def owned_niagara_entries(state):
    expected_path = NIAGARA_SYSTEM_PATH + "." + NIAGARA_SYSTEM_PATH.rsplit("/", 1)[-1]
    return [entry for entry in state.get("states", []) if entry.get("asset") == expected_path]


def read_niagara_state(awake=None):
    if awake is not None:
        run_editor_action("niagara_state", awake=awake)
        time.sleep(NIAGARA_SETTLE_SECONDS)
    return run_editor_action("niagara_state", awake=None)


def check_niagara_activation(report):
    attach = run_editor_action("niagara_attach", niagara_system=NIAGARA_SYSTEM_PATH)
    if "error" in attach:
        report["niagara_check"] = {"passed": False, "error": attach["error"]}
        return
    time.sleep(NIAGARA_SETTLE_SECONDS)
    spawned = read_niagara_state()
    asleep = read_niagara_state(awake=False)
    awake = read_niagara_state(awake=True)
    stages = {"spawned": owned_niagara_entries(spawned), "asleep": owned_niagara_entries(asleep), "awake_again": owned_niagara_entries(awake)}
    passed = (len(stages["spawned"]) == 1 and stages["spawned"][0]["active"]
              and len(stages["asleep"]) == 1 and not stages["asleep"][0]["active"]
              and len(stages["awake_again"]) == 1 and stages["awake_again"][0]["active"])
    report["niagara_check"] = {"passed": passed, "actor": attach.get("actor"), "system": NIAGARA_SYSTEM_PATH, "stages": stages,
                               "criterion": "프리셋 ExternalNiagaraSystem 컴포넌트가 정확히 1개 생기고 활성, 잠들면 비활성, 깨면 다시 활성"}
    print("niagara", json.dumps(report["niagara_check"], ensure_ascii=False), flush=True)


def finish_measurement(report):
    previous_phase = None if report.get("last_phase_end_logged") else report["last_started_phase"]
    result = run_editor_action("finish", previous_phase=previous_phase)
    if "error" in result:
        return f"측정 종료 처리 실패: {result['error']}"
    report["finish_logged"] = True
    if result.get("console_variable_value") in (0, None):
        report["console_variable_state"] = "off"
    return None


def measure_in_pie(arguments, report):
    if not pie_check_fluid.wait_for_pie_state(True, pie_check_fluid.PIE_START_WAIT_SECONDS):
        return "PIE 게임 월드가 나타나지 않았다"
    report["console_variable_state"] = "maybe_on"
    setup_result = wait_for_fluid_setup()
    if "error" in setup_result:
        return f"측정 준비 실패: {setup_result['error']}"
    report["console_variable_state"] = "on"
    report["setup_actors"] = setup_result["actors"]
    report["console_variable_readback"] = setup_result.get("console_variable_value")
    print("setup", json.dumps({"actors": len(setup_result["actors"]), "resolutions": [entry["resolution"] for entry in setup_result["actors"]],
                               "console_variable_readback": report["console_variable_readback"]}, ensure_ascii=False), flush=True)
    if report["console_variable_readback"] not in (1, None):
        return f"콘솔 변수 {CONSOLE_VARIABLE_NAME}이 1이 되지 않았다(읽은 값 {report['console_variable_readback']}): C++ 재빌드 뒤 에디터를 재시작했는지 확인"
    phase_error = run_phases(arguments.phase_seconds, report)
    if phase_error is not None:
        return phase_error
    end_result = run_editor_action("end_phase", phase=report["last_started_phase"])
    if "error" in end_result:
        return f"마지막 구간 종료 표식 실패: {end_result['error']}"
    report["last_phase_end_logged"] = True
    check_niagara_activation(report)
    return finish_measurement(report)


def shut_down_pie(session, report, pie_started):
    environment = report["environment"]
    if pie_started and not report["finish_logged"] and report["console_variable_state"] in ("on", "maybe_on"):
        finish_measurement(report)
    pie_ended = pie_check_fluid.stop_pie(session, environment) if pie_started else pie_check_fluid.wait_for_pie_state(False, pie_check_fluid.PIE_STOP_WAIT_SECONDS)
    environment["pie_ended"] = pie_ended
    if report["console_variable_state"] not in ("on", "maybe_on"):
        return
    restore_result = run_editor_action("finish", previous_phase=None)
    if "error" not in restore_result and restore_result.get("console_variable_value") in (0, None):
        report["console_variable_state"] = "off" if restore_result.get("console_variable_value") == 0 else "off_unverified"
        return
    report["warnings"].append(f"{CONSOLE_VARIABLE_NAME}을 0으로 되돌리지 못했다. 에디터 콘솔에서 직접 0으로 돌린다")


def run_session(arguments, report):
    environment = report["environment"]
    level_status = pie_check_fluid.open_arena_level()
    environment["level"] = level_status
    if not level_status.get("loaded"):
        return "레벨을 열지 못했다(PIE가 이미 켜져 있거나 에셋·에디터 연결이 없다): " + json.dumps(level_status, ensure_ascii=False)[:300]
    try:
        session = uemcp.connect()
    except (OSError, ValueError, KeyError) as error:
        return f"MCP 서버(127.0.0.1:8000)에 연결하지 못했다: {error}"
    pie_started = pie_check_fluid.start_pie(session, environment)
    try:
        if not pie_started:
            return "PIE를 시작하지 못했다: " + environment.get("start_pie_error", "")[:300]
        return measure_in_pie(arguments, report)
    finally:
        shut_down_pie(session, report, pie_started)


def collect_phase_summaries(report, log_offset):
    log_lines = wait_for_end_marker(log_offset, report["last_started_phase"]) if report["finish_logged"] else read_log_lines_since(log_offset)
    parsed = parse_phase_lines(log_lines)
    return {name: summarize_phase(report["phases"].get(name), parsed.get(name, [])) for name in PHASE_NAMES}


def format_cell(value, pattern):
    return "-" if value is None else pattern.format(value)


def print_table(summaries):
    print(f"{'phase':<22}{'lines':>6}{'avg_tick_ms':>13}{'max_tick_ms':>13}{'avg_frame_ms':>14}{'awake':>7}{'total_cells':>13}  state")
    for name in PHASE_NAMES:
        summary = summaries[name]
        state = {True: "ok", False: "DIFF", None: "-"}[summary["matches_editor_state"]]
        print(f"{name:<22}{summary['lines']:>6}{format_cell(summary['avg_tick_ms'], '{:.4f}'):>13}{format_cell(summary['max_tick_ms'], '{:.4f}'):>13}"
              f"{format_cell(summary['avg_frame_ms'], '{:.3f}'):>14}{format_cell(summary['awake_actors'], '{}'):>7}{format_cell(summary['total_cells'], '{}'):>13}  {state}")


def add_slow_frame_warning(report, summaries):
    slow_phases = [name for name, summary in summaries.items()
                   if summary["avg_frame_ms"] is not None and summary["avg_tick_ms"] is not None
                   and summary["avg_frame_ms"] - summary["avg_tick_ms"] >= SLOW_FRAME_MS]
    if slow_phases:
        report["warnings"].append(f"유체 틱을 뺀 나머지 프레임 시간이 {SLOW_FRAME_MS:.0f}ms 이상인 구간 {', '.join(slow_phases)}: 백그라운드 3fps 제한이 의심된다(L-editor-06)")


def write_report(report, exit_code):
    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(report, indent=2, ensure_ascii=False)
    OUTPUT_PATH.write_text(text, encoding="utf-8")
    if exit_code == EXIT_ENVIRONMENT:
        return
    evidence_path = EVIDENCE_PATH
    if evidence_path.exists():
        evidence_path = EVIDENCE_PATH.with_name(f"{EVIDENCE_PATH.stem}-{datetime.now().strftime('%H%M%S')}.json")
    evidence_path.parent.mkdir(parents=True, exist_ok=True)
    evidence_path.write_text(text, encoding="utf-8")
    print("evidence", evidence_path.relative_to(ROOT))


def parse_arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument("--phase-seconds", type=float, default=PHASE_SECONDS)
    arguments = parser.parse_args()
    if arguments.phase_seconds <= 0:
        parser.error("--phase-seconds는 0보다 커야 한다")
    return arguments


def main():
    arguments = parse_arguments()
    report = {"tool": "pie_profile_fluid", "run_token": RUN_TOKEN, "date": date.today().isoformat(), "started": time.strftime("%Y-%m-%d %H:%M:%S"),
              "level": pie_check_fluid.LEVEL_PATH, "phase_seconds": arguments.phase_seconds, "warnings": [FOREGROUND_WARNING], "environment": {},
              "setup_actors": [], "phases": {}, "last_started_phase": None, "finish_logged": False, "console_variable_state": "unchanged",
              "console_variable_readback": None, "environment_error": None, "niagara_check": None, "passed": False}
    print("경고:", FOREGROUND_WARNING, flush=True)
    log_offset = read_log_size()
    try:
        report["environment_error"] = run_session(arguments, report)
    except Exception as error:
        report["environment_error"] = f"예외: {type(error).__name__}: {error}"[:500]
    summaries = collect_phase_summaries(report, log_offset) if report["phases"] else {}
    report["phases"] = {name: {**summaries[name], "editor_state": report["phases"].get(name, {}).get("actors")} for name in summaries}
    if summaries:
        add_slow_frame_warning(report, summaries)
        print_table(summaries)
    exit_code = 0
    if report["environment_error"] is not None:
        exit_code = EXIT_ENVIRONMENT
    elif not summaries or any(summary["lines"] < 1 for summary in summaries.values()):
        exit_code = EXIT_MISSING_LINES
    elif any(summary["state_consistent"] is False or summary["matches_editor_state"] is False for summary in summaries.values()):
        exit_code = EXIT_MISSING_LINES
        report["warnings"].append("구간 안에서 깨어 있는 액터 수·셀 수가 바뀌었거나 에디터 상태와 다르다(구간 오염)")
    elif not (report.get("niagara_check") or {}).get("passed"):
        exit_code = EXIT_MISSING_LINES
    report["passed"] = exit_code == 0
    report["exit_code"] = exit_code
    write_report(report, exit_code)
    for warning in report["warnings"]:
        print("경고:", warning)
    if report["environment_error"] is not None:
        print("환경 문제:", report["environment_error"])
    print("passed", report["passed"], "exit_code", exit_code, "report", OUTPUT_PATH.relative_to(ROOT).as_posix())
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
