"""실행 환경: 시스템 Python 3.12(언리얼 에디터가 TDGame을 열고 원격 실행이 켜진 상태, 에디터 앞창 유지 L-editor-06). 맵을 저장하지 않는다. 헤드리스 시험과 동시에 돌리지 않는다(L-editor-20).
실행 명령: python Tools/MonsterAI/pie_measure_ragdoll_jitter.py --label baseline [--task M3-39] [--species Goblin ...] [--force-species Hyena ...] [--no-player] [--repeats 2] [--count 3 --pile-spacing-scale 2 | --pile-gaps 2.4 3.4] [--flight-hold 2.6] [--freeze] [--cvar TD.Feedback.SlowMo=0] [--analyze-only] | --summarize <label> ... (마크다운 비교표만) (PowerShell, L-repo-01)
출력: Docs/MonsterAI_CombatSim/measurements/<task>/<label>/report.json(대상마다 정착 창 뼈 위치 RMS·프레임 이동 RMS·고주파 RMS·최대 각속도·잠들기 초·잠들 때마다 바디 속도·다시 깨어난 횟수·슬로모 겹침·다른 시체 정착)과 <대상>-<n>_shot<k>.png 연사, 원본 표본 Saved/AgentOps/<날짜>/ragdoll-<task>/<label>/raw/(M3-38은 20261018/ragdoll-jitter). 종료 코드 0 측정 완료 / 2 환경 문제
상태: 현행(M3-39). 정착 창 = 착지(모든 바디 선속도 60cm/s 미만, 래그돌 1.5초 뒤 상한) 0.5초 뒤부터 2.5초(가라앉기 시작 전까지). 목표 렌더 위치 RMS 0.5cm 이하·래그돌 3초 안 잠들기·재우는 순간 바디 속도 30cm/s 이하(멈춤이 안 보이게, 잠들 때마다)
"""
import argparse
import json
import math
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUN_IN_EDITOR = ROOT / "Tools" / "run_in_editor.py"
SAMPLER_PATH = ROOT / "Tools" / "MonsterAI" / "editor_ragdoll_jitter_sampler.py"
SHOT_DIRECTORY = ROOT / "Saved" / "Screenshots" / "WindowsEditor"
MEASUREMENT_ROOT = ROOT / "Docs" / "MonsterAI_CombatSim" / "measurements"
M338_RAW_ROOT = ROOT / "Saved" / "AgentOps" / "20261018" / "ragdoll-jitter"
RESULT_MARKER = "TD_RAGDOLL_JITTER"
DEFAULT_SPECIES = ["Goblin", "SkeletonGuard", "StoneGolem", "BookHeadBrute", "Greystone", "Wukong", "Swordsman"]
PIE_START_WAIT_SECONDS = 120.0
SPAWN_WAIT_SECONDS = 15.0
SPAWN_SETTLE_SECONDS = 1.0
JOB_EXTRA_WAIT_SECONDS = 25.0
SHOT_WAIT_SECONDS = 15.0
LAND_SPEED_CM_S = 60.0
LAND_MIN_SECONDS = 0.2
LAND_MAX_SECONDS = 1.5
WINDOW_DELAY_SECONDS = 0.5
WINDOW_LENGTH_SECONDS = 2.5
MIN_WINDOW_SECONDS = 1.0
TARGET_POSITION_RMS_CM = 0.5
TARGET_SLEEP_SECONDS = 3.0
TARGET_FREEZE_BODY_SPEED_CM_S = 30.0
SLEEP_CHECK_TOLERANCE_SECONDS = 0.05
FREEZE_LOOKBACK_SECONDS = 0.35
EXIT_ENVIRONMENT = 2
PLAYER_KILLER_SPECIES = "Goblin"
PLAYER_KILLER_DISTANCE = 1200.0

PRELUDE = '''import unreal, json, sys
def _td_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
def _td_print(payload):
    print('{marker}', json.dumps(payload))
'''

START_CODE = '''def _td_start():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if editor.get_game_world() is None:
        if not editor.get_editor_world().get_path_name().startswith('{level}'):
            levels.load_level('{level}')
        levels.editor_request_begin_play()
    _td_print({{'requested': True}})
_td_start()
del _td_start
'''

STATUS_CODE = '''def _td_status():
    world = _td_world()
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
    _td_print({'pie': world is not None, 'pawn': pawn is not None})
_td_status()
del _td_status
'''

LOAD_SAMPLER_CODE = '''def _td_load():
    import importlib.util
    previous = sys.modules.get('td_ragdoll_sampler')
    if previous and previous.STATE.get('handle') is not None:
        unreal.unregister_slate_post_tick_callback(previous.STATE['handle'])
        previous.STATE['handle'] = None
    if previous:
        previous.restore_species()
        previous.restore_overrides()
    spec = importlib.util.spec_from_file_location('td_ragdoll_sampler', r'{path}')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    sys.modules['td_ragdoll_sampler'] = module
    _td_print({{'loaded': True}})
_td_load()
del _td_load
'''

RESET_CODE = '''def _td_reset():
    world = _td_world()
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDMonsterCharacter):
        actor.destroy_actor()
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDDamageEntity):
        actor.destroy_actor()
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    combat = pawn.get_component_by_class(unreal.TDCombatComponent) if pawn else None
    if combat:
        combat.set_stats(combat.get_stats(), True)
    if '{species}':
        unreal.SystemLibrary.execute_console_command(world, 'TDSpawnMonster {species} {count} {distance}', unreal.GameplayStatics.get_player_controller(world, 0))
    _td_print({{'reset': True}})
_td_reset()
del _td_reset
'''

COUNT_CODE = '''def _td_count():
    world = _td_world()
    monsters = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDMonsterCharacter) if world else []
    _td_print({'monsters': [actor.get_species().get_name() if actor.get_species() else None for actor in monsters]})
_td_count()
del _td_count
'''

JOB_CODE = '''_td_print(sys.modules['td_ragdoll_sampler'].start_job({label!r}, {kind!r}, r{out!r}, {kill_fraction}, {kill_delay}, {observe}, {shots}, {force}, {assist!r}, {kill_others}, {scenario!r}))
'''

CVAR_CODE = '''def _td_cvar():
    world = _td_world()
    names = {names!r}
    before = {{name: unreal.SystemLibrary.get_console_variable_float_value(name) for name in names}}
    for name, value in {values!r}.items():
        unreal.SystemLibrary.execute_console_command(world, name + ' ' + str(value), None)
    _td_print({{'before': before, 'after': {{name: unreal.SystemLibrary.get_console_variable_float_value(name) for name in names}}}})
_td_cvar()
del _td_cvar
'''

OVERRIDE_CODE = '''_td_print(sys.modules['td_ragdoll_sampler'].apply_overrides({meshes!r}, {player}, {body!r}, {self_collision_off}, {species!r}))
'''

JOB_STATUS_CODE = '''_td_print(sys.modules['td_ragdoll_sampler'].status())
'''

RESTORE_CODE = '''_td_print({'species': sys.modules['td_ragdoll_sampler'].restore_species() is None, 'overrides': sys.modules['td_ragdoll_sampler'].restore_overrides()})
'''

STOP_CODE = '''def _td_stop():
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    _td_print({'stopped': True})
_td_stop()
del _td_stop
'''


def run_editor_code(body):
    code = PRELUDE.format(marker=RESULT_MARKER) + body
    completed = subprocess.run([sys.executable, str(RUN_IN_EDITOR), "-c", code], capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=ROOT)
    for line in completed.stdout.splitlines():
        index = line.find(RESULT_MARKER)
        if index >= 0:
            return json.loads(line[index + len(RESULT_MARKER):])
    return {"error": completed.stdout[-600:] + completed.stderr[-300:]}


def wait_until(predicate, timeout_seconds, poll_seconds=0.4):
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(poll_seconds)
    return None


def start_pie(map_path, overrides):
    if "loaded" not in run_editor_code(LOAD_SAMPLER_CODE.format(path=SAMPLER_PATH.as_posix())):
        return False
    if overrides:
        applied = run_editor_code(OVERRIDE_CODE.format(**overrides))
        print(json.dumps(applied, ensure_ascii=False))
        if "overridden" not in applied:
            return False
    if "error" in run_editor_code(START_CODE.format(level=map_path)):
        return False
    if not wait_until(lambda: run_editor_code(STATUS_CODE).get("pawn"), PIE_START_WAIT_SECONDS, 2.0):
        return False
    time.sleep(3.0)
    return True


def set_cvars(values):
    if not values:
        return {}
    values = {name: int(value) if float(value).is_integer() else value for name, value in values.items()}
    result = run_editor_code(CVAR_CODE.format(names=list(values), values=values))
    print(json.dumps({"cvar": result}, ensure_ascii=False))
    return result.get("before", {})


def stop_pie():
    run_editor_code(RESTORE_CODE)
    run_editor_code(STOP_CODE)
    wait_until(lambda: not run_editor_code(STATUS_CODE).get("pie", True), 30.0, 1.0)


def list_shots():
    return {path.name: path for path in SHOT_DIRECTORY.glob("ScreenShot*.png")} if SHOT_DIRECTORY.exists() else {}


def collect_shots(before, expected, evidence_directory, stem):
    deadline = time.monotonic() + SHOT_WAIT_SECONDS
    fresh = []
    while time.monotonic() < deadline:
        fresh = sorted((path for name, path in list_shots().items() if name not in before), key=lambda path: path.stat().st_mtime)
        if len(fresh) >= expected and all(path.stat().st_size > 0 for path in fresh):
            break
        time.sleep(0.5)
    time.sleep(0.5)
    moved = []
    for index, path in enumerate(fresh, start=1):
        target = evidence_directory / f"{stem}_shot{index}.png"
        target.unlink(missing_ok=True)
        shutil.move(str(path), str(target))
        moved.append(target.relative_to(ROOT).as_posix())
    return moved


def run_target(arguments, kind, species_name, repeat_index, evidence_directory, raw_directory):
    stem = f"{species_name or 'Player'}-{repeat_index}"
    raw_path = raw_directory / f"{stem}.json"
    raw_path.unlink(missing_ok=True)
    if kind == "monster":
        run_editor_code(RESET_CODE.format(species=species_name, count=arguments.count, distance=arguments.distance))
        if not wait_until(lambda: species_name and f"DA_TDMonster_{species_name}" in (run_editor_code(COUNT_CODE).get("monsters") or []), SPAWN_WAIT_SECONDS):
            return {"target": stem, "error": "spawn not observed"}
        time.sleep(SPAWN_SETTLE_SECONDS)
    else:
        run_editor_code(RESET_CODE.format(species=PLAYER_KILLER_SPECIES, count=1, distance=PLAYER_KILLER_DISTANCE))
        if not wait_until(lambda: f"DA_TDMonster_{PLAYER_KILLER_SPECIES}" in (run_editor_code(COUNT_CODE).get("monsters") or []), SPAWN_WAIT_SECONDS):
            return {"target": stem, "error": "player killer spawn not observed"}
    before = list_shots()
    shot_offsets = arguments.shots if repeat_index == 1 else []
    force = kind == "monster" and species_name in (arguments.force_species or [])
    scenario = build_scenario(arguments) if kind == "monster" else {}
    started = run_editor_code(JOB_CODE.format(label=stem, kind="player" if kind == "player" else "monster", out=str(raw_path), kill_fraction=arguments.kill_fraction,
                                              kill_delay=arguments.kill_delay, observe=arguments.observe, shots=shot_offsets, force=force, assist=parse_assist(arguments.assist),
                                              kill_others=arguments.count > 1 and not arguments.pile_gaps, scenario=scenario))
    if "started" not in started:
        return {"target": stem, "error": f"job not started: {started}"}
    finished = wait_until(lambda: run_editor_code(JOB_STATUS_CODE).get("done"), arguments.observe + arguments.kill_delay + scenario.get("kill_timeout", 0.0) + JOB_EXTRA_WAIT_SECONDS, 0.5)
    if not finished or not raw_path.exists():
        return {"target": stem, "error": "job did not finish"}
    shots = collect_shots(before, len(shot_offsets), evidence_directory, stem) if shot_offsets else []
    result = analyze(json.loads(raw_path.read_text(encoding="utf-8")))
    result.update({"target": stem, "kind": kind, "forced_ragdoll": force, "shots": shots})
    return result


def build_scenario(arguments):
    scenario = {}
    if arguments.flight_hold is not None:
        scenario.update({"flight_hold": arguments.flight_hold, "kill_timeout": arguments.kill_timeout})
    if arguments.freeze:
        scenario["freeze"] = True
    if arguments.pile_spacing_scale:
        scenario["pile_spacing_scale"] = arguments.pile_spacing_scale
    if arguments.pile_gaps:
        scenario["pile_gaps"] = sorted(arguments.pile_gaps)
    if arguments.emulate_launch_clamp:
        seconds, relative_cm_s, angular_deg_s = (float(value) for value in arguments.emulate_launch_clamp.split(","))
        scenario["emulate_launch_clamp"] = {"seconds": seconds, "relative_cm_s": relative_cm_s, "angular_deg_s": angular_deg_s}
    if arguments.emulate_firm_damping:
        start, extra_damping = (float(value) for value in arguments.emulate_firm_damping.split(","))
        scenario["emulate_firm_damping"] = {"start": start, "extra_damping": extra_damping}
    return scenario


def parse_cvars(pairs):
    values = {}
    for pair in pairs or []:
        name, value = pair.split("=", 1)
        values[name] = float(value) if "." in value else int(value)
    return values


def parse_assist(text):
    if not text:
        return None
    min_seconds, speed_cm_s, hold_seconds = (float(value) for value in text.split(","))
    return {"min_seconds": min_seconds, "speed_cm_s": speed_cm_s, "hold_seconds": hold_seconds}


def parse_body_values(pairs):
    values = {}
    for pair in pairs or []:
        name, value = pair.split("=", 1)
        if value.lower() in ("true", "false"):
            values[name] = value.lower() == "true"
            continue
        try:
            values[name] = int(value) if value.isdigit() else float(value)
        except ValueError:
            values[name] = value
    return values


def build_overrides(arguments, species_names):
    body = parse_body_values(arguments.body)
    if not body and not arguments.self_collision_off:
        return None
    table = json.loads((ROOT / "Tools" / "MonsterAI" / "species_table.json").read_text(encoding="utf-8"))
    meshes = sorted({entry["mesh"] for entry in table["species"] if entry["name"] in species_names})
    return {"meshes": meshes, "player": not arguments.no_player, "body": body, "self_collision_off": arguments.self_collision_off, "species": sorted(species_names)}


def length(vector):
    return math.sqrt(sum(component * component for component in vector))


def difference(first, second):
    return [a - b for a, b in zip(first, second)]


def position_metrics(window, slot):
    bone_count = len(window[0]["bones"])
    per_bone_rms = []
    step_squares = []
    high_frequency_squares = []
    for bone_index in range(bone_count):
        positions = [frame["bones"][bone_index][slot] for frame in window]
        mean = [sum(position[axis] for position in positions) / len(positions) for axis in range(3)]
        per_bone_rms.append(math.sqrt(sum(length(difference(position, mean)) ** 2 for position in positions) / len(positions)))
        for index in range(1, len(positions)):
            step_squares.append(length(difference(positions[index], positions[index - 1])) ** 2)
        for index in range(1, len(positions) - 1):
            midpoint = [(positions[index - 1][axis] + positions[index + 1][axis]) * 0.5 for axis in range(3)]
            high_frequency_squares.append(length(difference(positions[index], midpoint)) ** 2)
    return {
        "position_rms_cm": round(math.sqrt(sum(value * value for value in per_bone_rms) / bone_count), 4),
        "step_rms_cm": round(math.sqrt(sum(step_squares) / max(len(step_squares), 1)), 4),
        "high_frequency_rms_cm": round(math.sqrt(sum(high_frequency_squares) / max(len(high_frequency_squares), 1)), 4),
        "per_bone_rms_cm": per_bone_rms,
    }


def find_sleep_time(frames, ragdoll_time, sink_time):
    candidates = [frame for frame in frames if frame["t"] >= ragdoll_time and (sink_time is None or frame["t"] < sink_time)]
    sleep_time = None
    for frame in candidates:
        is_asleep = frame["awake"] == 0
        if is_asleep and sleep_time is None:
            sleep_time = frame["t"]
        elif not is_asleep:
            sleep_time = None
    return sleep_time


def measure_freeze(frames, ragdoll_time):
    after = [frame for frame in frames if frame["t"] >= ragdoll_time]
    index = next((position for position in range(1, len(after)) if after[position]["awake"] == 0 and after[position - 1]["awake"] == 1), None)
    if index is None:
        return {"first_sleep_seconds": None}
    last_awake = after[index - 1]
    sleep_time = after[index]["t"]
    earlier = next((frame for frame in after if frame["t"] >= sleep_time - FREEZE_LOOKBACK_SECONDS), last_awake)
    moves = [length(difference(last_awake["bones"][bone][0], earlier["bones"][bone][0])) for bone in range(len(last_awake["bones"]))]
    return {
        "first_sleep_seconds": round(sleep_time - ragdoll_time, 4),
        "freeze_body_speed_cm_s": round(max(length(row[2]) for row in last_awake["bones"]), 1),
        "pre_sleep_move_cm": round(max(moves), 2),
    }


def sleep_transitions(frames, start_time, end_time, awake_of, speed_of):
    events = []
    wakes = []
    previous = None
    for frame in frames:
        if frame["t"] < start_time or (end_time is not None and frame["t"] >= end_time):
            continue
        awake = awake_of(frame)
        if previous is not None and previous[0] == 1 and awake == 0:
            events.append([round(frame["t"] - start_time, 3), round(previous[1], 1)])
        if previous is not None and previous[0] == 0 and awake == 1:
            wakes.append(round(frame["t"] - start_time, 3))
        previous = (awake, speed_of(frame))
    return events, wakes


def summarize_sleep(events, wakes, final_sleep_seconds):
    rewake_settle = []
    for wake in wakes:
        later = [event[0] for event in events if event[0] > wake]
        rewake_settle.append(round(later[0] - wake, 3) if later else None)
    max_freeze = max((event[1] for event in events), default=None)
    first_sleep = events[0][0] if events else None
    return {
        "sleep_events": events, "rewake_seconds": wakes, "rewake_settle_seconds": rewake_settle,
        "max_freeze_body_speed_cm_s": max_freeze,
        "meets_sleep_target": bool(final_sleep_seconds is not None and first_sleep is not None and first_sleep <= TARGET_SLEEP_SECONDS + SLEEP_CHECK_TOLERANCE_SECONDS
                                   and max_freeze <= TARGET_FREEZE_BODY_SPEED_CM_S
                                   and all(value is not None and value <= TARGET_SLEEP_SECONDS + SLEEP_CHECK_TOLERANCE_SECONDS for value in rewake_settle)),
    }


def analyze_others(frames, t_kill, observe_end):
    if not frames or not frames[0].get("others"):
        return []
    results = []
    for index in range(len(frames[0]["others"])):
        rows = [(frame["t"], frame["others"][index]) for frame in frames if len(frame.get("others", [])) > index]
        ragdoll = next((time for time, row in rows if row[0]), None)
        if ragdoll is None:
            results.append({"index": index, "ragdoll": False})
            continue
        sink = next((time for time, row in rows if len(row) > 3 and row[3] and time > ragdoll), None)
        own = [{"t": time, "awake": row[1], "speed": row[2]} for time, row in rows if time >= ragdoll]
        events, wakes = sleep_transitions(own, ragdoll, sink, lambda frame: frame["awake"], lambda frame: frame["speed"])
        final_sleep = find_sleep_time(own, ragdoll, sink)
        final_sleep_seconds = round(final_sleep - ragdoll, 3) if final_sleep is not None else None
        result = {"index": index, "ragdoll_after_target_kill_seconds": round(ragdoll - t_kill, 3), "sleep_seconds": final_sleep_seconds,
                  "observed_seconds": round((sink or observe_end) - ragdoll, 3)}
        result.update(summarize_sleep(events, wakes, final_sleep_seconds))
        results.append(result)
    return results


def analyze(raw):
    frames = raw["frames"]
    info = raw["info"]
    ragdoll_time = raw["t_ragdoll"]
    summary = {"result": raw["result"], "species": info["species"], "physics_asset": info["physics_asset"], "body_count": len(info["bones"]),
               "bone_mass_kg": dict(zip(info["bones"], info["bone_mass_kg"])), "frames": len(frames)}
    if ragdoll_time is None or not frames or not info["bones"]:
        summary["ragdoll"] = False
        return summary
    summary["ragdoll"] = True
    summary["ragdoll_delay_seconds"] = round(ragdoll_time - raw["t_kill"], 4)
    for key in ("at_kill", "freeze_applied", "pile_spacing_cm", "pile_placed", "pile_dropped_seconds", "others_killed"):
        if key in info:
            summary[key] = info[key]
    after = [frame for frame in frames if frame["t"] >= ragdoll_time]
    summary["launch_speed_cm_s"] = round(max(length(row[2]) for row in after[min(1, len(after) - 1)]["bones"]), 1)
    land_time = next((frame["t"] for frame in after if frame["t"] >= ragdoll_time + LAND_MIN_SECONDS
                      and max(length(row[2]) for row in frame["bones"]) < LAND_SPEED_CM_S), None)
    land_time = min(land_time, ragdoll_time + LAND_MAX_SECONDS) if land_time is not None else ragdoll_time + LAND_MAX_SECONDS
    sink_time = next((frame["t"] for frame in frames if frame["sink"]), None)
    window_start = land_time + WINDOW_DELAY_SECONDS
    window_end = window_start + WINDOW_LENGTH_SECONDS
    if sink_time is not None:
        window_end = min(window_end, sink_time)
    window = [frame for frame in frames if window_start <= frame["t"] <= window_end]
    observed_seconds = frames[-1]["t"] - ragdoll_time
    sleep_time = find_sleep_time(frames, ragdoll_time, sink_time)
    summary.update(measure_freeze(frames, ragdoll_time))
    summary.update({
        "land_seconds": round(land_time - ragdoll_time, 4),
        "sink_seconds": round(sink_time - ragdoll_time, 4) if sink_time is not None else None,
        "window_seconds": [round(window_start - ragdoll_time, 4), round(window_end - ragdoll_time, 4)],
        "observed_seconds": round(observed_seconds, 4),
        "sleep_seconds": round(sleep_time - ragdoll_time, 4) if sleep_time is not None else None,
        "fps": round((len(frames) - 1) / max(frames[-1]["rt"] - frames[0]["rt"], 1e-6), 1),
        "min_time_dilation": min(frame["dilation"] for frame in frames),
        "assist_sleep_seconds": round(raw["t_assist_sleep"] - ragdoll_time, 4) if raw.get("t_assist_sleep") is not None else None,
    })
    ragdoll_frame = after[0]
    sleep_frame = next((frame for frame in after if sleep_time is not None and frame["t"] >= sleep_time), None)
    slowed = [frame for frame in after if frame["dilation"] < 0.999]
    summary.update({
        "sleep_real_seconds": round(sleep_frame["rt"] - ragdoll_frame["rt"], 4) if sleep_frame else None,
        "ragdoll_in_slowmo": any(frame["t"] <= ragdoll_time + 1.0 for frame in slowed),
        "slowmo_after_ragdoll_real_seconds": round(slowed[-1]["rt"] - slowed[0]["rt"], 3) if slowed else 0.0,
        "min_actor_dilation_after_ragdoll": min((frame.get("actor_dilation", 1.0) for frame in after), default=None),
    })
    events, wakes = sleep_transitions(frames, ragdoll_time, sink_time, lambda frame: frame["awake"], lambda frame: max(length(row[2]) for row in frame["bones"]))
    summary.update(summarize_sleep(events, wakes, summary["sleep_seconds"]))
    summary["others"] = analyze_others(frames, raw["t_kill"], frames[-1]["t"])
    if len(window) < 3 or window[-1]["t"] - window[0]["t"] < MIN_WINDOW_SECONDS:
        summary["window_short"] = True
    if len(window) < 3:
        return summary
    render = position_metrics(window, 0)
    physics = position_metrics(window, 1)
    worst = sorted(zip(info["bones"], render.pop("per_bone_rms_cm")), key=lambda item: -item[1])[:4]
    physics.pop("per_bone_rms_cm")
    component_steps = [length(difference(window[index]["component"], window[index - 1]["component"])) for index in range(1, len(window))]
    summary.update({
        "render": render,
        "physics": physics,
        "worst_bones_render_rms_cm": [[bone, round(value, 4)] for bone, value in worst],
        "component_step_rms_cm": round(math.sqrt(sum(step * step for step in component_steps) / len(component_steps)), 4),
        "max_angular_deg_s": round(max(length(row[3]) for frame in window for row in frame["bones"]), 2),
        "max_linear_cm_s": round(max(length(row[2]) for frame in window for row in frame["bones"]), 2),
        "awake_fraction": round(sum(1 for frame in window if frame["awake"]) / len(window), 3),
    })
    summary["meets_target"] = bool(render["position_rms_cm"] <= TARGET_POSITION_RMS_CM and summary["sleep_seconds"] is not None
                                   and summary["sleep_seconds"] <= TARGET_SLEEP_SECONDS
                                   and summary.get("freeze_body_speed_cm_s", 0.0) <= TARGET_FREEZE_BODY_SPEED_CM_S)
    return summary


def print_table(results):
    print("target | ragdoll | launch | land | render rms | hf rms | max ang | sleep | sleep real | sleeps | max freeze | rewake settle | slowmo | others sleep/max freeze | sleep target | target")
    for result in results:
        if "render" not in result:
            print(f"{result.get('target')} | {result.get('error') or result.get('result')} | ragdoll={result.get('ragdoll')} window_short={result.get('window_short')}")
            continue
        others = " ".join(f"{other.get('sleep_seconds')}/{other.get('max_freeze_body_speed_cm_s')}" for other in result.get("others", []))
        print(f"{result['target']} | {result['ragdoll_delay_seconds']} | {result['launch_speed_cm_s']} | {result['land_seconds']} | "
              f"{result['render']['position_rms_cm']} | {result['render']['high_frequency_rms_cm']} | {result['max_angular_deg_s']} | "
              f"{result['sleep_seconds']} | {result.get('sleep_real_seconds')} | {len(result.get('sleep_events', []))} | {result.get('max_freeze_body_speed_cm_s')} | "
              f"{result.get('rewake_settle_seconds')} | {result.get('ragdoll_in_slowmo')} | {others or '-'} | {result.get('meets_sleep_target')} | {result['meets_target']}")


def join_values(values):
    return "/".join("–" if value is None else (f"{value:.2f}".rstrip("0").rstrip(".") if isinstance(value, float) else str(value)) for value in values)


def summarize_reports(task, labels):
    print("| 측정 | 대상 | 래그돌 시작 초 | 시작 최대 속도 cm/s | 처치 높이 cm | 첫 잠들기 초 | 잠들기 실시간 초 | 잠든 횟수 | 재우는 순간 최대 바디 속도 cm/s | 다시 깨어 정착 초 | 위치 RMS cm | 슬로모 중 시작 | 다른 시체 잠들기 초/재우는 순간 cm/s | 목표(3초·30cm/s) |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    for label in labels:
        report = json.loads((MEASUREMENT_ROOT / task / label / "report.json").read_text(encoding="utf-8"))
        groups = {}
        for result in report["results"]:
            groups.setdefault(result["target"].rsplit("-", 1)[0], []).append(result)
        for name, results in groups.items():
            measured = [result for result in results if "render" in result]
            failed = len(results) - len(measured)
            others = [f"{other.get('sleep_seconds')}/{other.get('max_freeze_body_speed_cm_s')}" for result in measured for other in result.get("others", []) if other.get("ragdoll", True)]
            print(f"| {label} | {name}{f' (실패 {failed})' if failed else ''} | {join_values([result['ragdoll_delay_seconds'] for result in measured])} | "
                  f"{join_values([result['launch_speed_cm_s'] for result in measured])} | {join_values([(result.get('at_kill') or {}).get('height_cm') for result in measured])} | "
                  f"{join_values([result['sleep_events'][0][0] if result.get('sleep_events') else None for result in measured])} | "
                  f"{join_values([result.get('sleep_real_seconds') for result in measured])} | {join_values([len(result.get('sleep_events', [])) for result in measured])} | "
                  f"{join_values([result.get('max_freeze_body_speed_cm_s') for result in measured])} | "
                  f"{join_values([max((value for value in result.get('rewake_settle_seconds') or [] if value is not None), default=None) for result in measured])} | "
                  f"{join_values([result['render']['position_rms_cm'] for result in measured])} | {join_values(['예' if result.get('ragdoll_in_slowmo') else '아니오' for result in measured])} | "
                  f"{' '.join(others) or '–'} | {sum(1 for result in measured if result.get('meets_sleep_target'))}/{len(results)} |")


def analyze_existing(raw_directory):
    results = []
    for raw_path in sorted(raw_directory.glob("*.json")):
        result = analyze(json.loads(raw_path.read_text(encoding="utf-8")))
        result["target"] = raw_path.stem
        results.append(result)
    return results


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--label", default="")
    parser.add_argument("--summarize", nargs="*", default=[], help="measurements/<task>/<label>/report.json들을 마크다운 비교표로 출력하고 끝냄")
    parser.add_argument("--species", nargs="*", default=DEFAULT_SPECIES)
    parser.add_argument("--force-species", nargs="*", default=[])
    parser.add_argument("--no-player", action="store_true")
    parser.add_argument("--repeats", type=int, default=1)
    parser.add_argument("--map", default="/Game/Combat/Maps/LV_TDMegaMagicArena")
    parser.add_argument("--distance", type=float, default=350.0)
    parser.add_argument("--kill-fraction", type=float, default=0.25)
    parser.add_argument("--kill-delay", type=float, default=0.6)
    parser.add_argument("--observe", type=float, default=7.0)
    parser.add_argument("--shots", type=float, nargs="*", default=[1.6, 1.75, 1.9, 2.05])
    parser.add_argument("--analyze-only", action="store_true")
    parser.add_argument("--count", type=int, default=1, help="종마다 같은 자리에 부를 마릿수(2 이상이면 같은 순간 모두 처치해 시체 더미, 첫 마리만 측정)")
    parser.add_argument("--body", nargs="*", default=[], help="물리 에셋 바디 기본값 메모리 재정의(저장 안 함), 예: angular_damping=1.0 sleep_family=SENSITIVE")
    parser.add_argument("--self-collision-off", action="store_true", help="물리 에셋 바디 자기 충돌 끄기(메모리만)")
    parser.add_argument("--assist", default="", help="정착 재우기 흉내: 최소초,평균속도cm/s,유지초 예 1.0,15,0.3")
    parser.add_argument("--task", default="M3-38", help="증거 폴더 measurements/<task>/<label>")
    parser.add_argument("--flight-hold", type=float, default=None, help="비행 중으로 바뀐 뒤 이 초만큼 날면 처치(kill-fraction을 비행 페이즈 문턱 아래로)")
    parser.add_argument("--kill-timeout", type=float, default=20.0, help="--flight-hold 처치 조건을 기다리는 최대 초")
    parser.add_argument("--freeze", action="store_true", help="처치 kill-delay초 전에 빙결(TDFreezeStatusEffect 1초)")
    parser.add_argument("--pile-spacing-scale", type=float, default=0.0, help="--count 2 이상일 때 처치 순간 다른 몬스터를 대상 뒤·옆 캡슐 반지름×이 값 간격에 붙임")
    parser.add_argument("--pile-gaps", type=float, nargs="*", default=[], help="대상 처치 뒤 이 초마다 다른 몬스터를 대상 시체 위로 옮겨 처치(순차 더미)")
    parser.add_argument("--cvar", nargs="*", default=[], help="PIE 시작 뒤 설정하고 끝나면 되돌릴 콘솔 변수, 예 TD.Feedback.SlowMo=0")
    parser.add_argument("--emulate-launch-clamp", default="", help="C++ 수정 흉내: 초,질량중심 대비 선속도 상한cm/s,각속도 상한도/초 예 0.4,600,1440")
    parser.add_argument("--emulate-firm-damping", default="", help="C++ 수정 흉내: 시작초,추가 감쇠 예 2.5,8")
    arguments = parser.parse_args()
    if arguments.summarize:
        summarize_reports(arguments.task, arguments.summarize)
        return 0
    if not arguments.label:
        parser.error("--label이 필요하다")
    evidence_directory = MEASUREMENT_ROOT / arguments.task / arguments.label
    raw_root = M338_RAW_ROOT if arguments.task == "M3-38" else ROOT / "Saved" / "AgentOps" / time.strftime("%Y%m%d") / f"ragdoll-{arguments.task.lower()}"
    raw_directory = raw_root / arguments.label / "raw"
    cvars = parse_cvars(arguments.cvar)
    evidence_directory.mkdir(parents=True, exist_ok=True)
    raw_directory.mkdir(parents=True, exist_ok=True)

    if arguments.analyze_only:
        results = analyze_existing(raw_directory)
    else:
        monster_targets = list(arguments.species) + [name for name in arguments.force_species if name not in arguments.species]
        overrides = build_overrides(arguments, monster_targets)
        if not start_pie(arguments.map, overrides):
            print("editor remote execution or PIE unavailable")
            stop_pie()
            return EXIT_ENVIRONMENT
        cvars_before = set_cvars(cvars)
        results = []
        for repeat_index in range(1, arguments.repeats + 1):
            for species_name in monster_targets:
                result = run_target(arguments, "monster", species_name, repeat_index, evidence_directory, raw_directory)
                results.append(result)
                print(json.dumps({key: value for key, value in result.items() if key not in ("shots", "bone_mass_kg")}, ensure_ascii=False))
        for repeat_index in range(1, 0 if arguments.no_player else arguments.repeats + 1):
            stop_pie()
            if not start_pie(arguments.map, overrides):
                break
            set_cvars(cvars)
            result = run_target(arguments, "player", None, repeat_index, evidence_directory, raw_directory)
            results.append(result)
            print(json.dumps({key: value for key, value in result.items() if key not in ("shots", "bone_mass_kg")}, ensure_ascii=False))
        set_cvars(cvars_before)
        stop_pie()

    report = {"label": arguments.label, "map": arguments.map, "distance": arguments.distance, "kill_fraction": arguments.kill_fraction, "count": arguments.count,
              "experiment": {"body": parse_body_values(arguments.body), "self_collision_off": arguments.self_collision_off, "assist": parse_assist(arguments.assist)},
              "scenario": build_scenario(arguments), "cvars": cvars, "kill_delay": arguments.kill_delay, "observe": arguments.observe,
              "window": {"land_speed_cm_s": LAND_SPEED_CM_S, "delay_seconds": WINDOW_DELAY_SECONDS, "length_seconds": WINDOW_LENGTH_SECONDS},
              "target": {"position_rms_cm": TARGET_POSITION_RMS_CM, "sleep_seconds": TARGET_SLEEP_SECONDS, "freeze_body_speed_cm_s": TARGET_FREEZE_BODY_SPEED_CM_S},
              "results": results}
    (evidence_directory / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding="utf-8")
    print_table(results)
    print(f"-> {(evidence_directory / 'report.json').relative_to(ROOT).as_posix()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
