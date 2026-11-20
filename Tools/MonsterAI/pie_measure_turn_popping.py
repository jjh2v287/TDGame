"""실행 환경: 시스템 Python 3.12(언리얼 에디터가 TDGame을 열고 원격 실행이 켜진 상태). 맵을 저장하지 않는다. 헤드리스 시험과 동시에 돌리지 않는다(L-editor-20). 에디터·PIE 전에 빌드 보류를 잡는다.
실행 명령: python Tools/MonsterAI/pie_measure_turn_popping.py --label baseline [--task M3-42] [--species Barghest Goblin Hyena StoneGolem] [--count 3] [--distance 900] [--seconds 30] [--fixed-fps 60] [--cvar 이름=값 ...] [--map /Game/Combat/Maps/LV_TDMegaMagicArena] [--keep-pie] [--capture-species Hyena --capture-start 2 --capture-seconds 6 --capture-every 2 --capture-size 960x540 --camera-arm 900 --capture-focus 0] | --label <이름> --analyze-only | --summarize <label> <label> ... (PowerShell, L-repo-01)
출력: Docs/MonsterAI_CombatSim/measurements/<task>/<label>/report.json·summary.md(종·몬스터마다 논리 요(액터)·보이는 메시 요·골반 뼈 요의 각속도 최대·p99·p95, 초당 720도 초과 프레임, 한 프레임 급회전(초과 + 10도 이상)과 그때 상태·행동, 각속도 급변(연속 프레임 차 400도/초 이상), 방향 뒤집힘(90도/초 이상 회전이 0.25초 안에 반대로), 멈춤 뒤 몰아 돌기(가운데 프레임 0.05도 미만·양옆 1도 이상 같은 방향), 메시-액터·골반-메시 어긋남 p95·최대, 프레임 간격), 원본 Saved/AgentOps/<날짜>/turn-popping/<task>/<label>/raw/<종>.json, 촬영 종은 같은 폴더 frames/<종>/<종>_<번호>.png(게임 뷰포트 HighResShot, 원본 JSON capture 에 발행·찍힌 게임 초). 종료 코드 0 측정 완료 / 1 일부 종 실패 / 2 환경 문제
상태: 현행(M3-42, M3-43 에서 원본 경로에 작업 ID 를 넣음 — 그 전에는 같은 날 같은 라벨이 덮어써 M3-42 after·clip-before·clip-after 원본이 M3-43 측정으로 바뀌었다). 판정선은 측정 전에 정한 아래 상수(OVER_RATE_DEG_S 등)이고 결과를 본 뒤 바꾸지 않는다. 플레이어는 시작점 둘레 반지름 650cm 별 모양 꼭짓점을 걷고 멈춤을 섞어(몬스터 공격 유도) 몬스터가 쫓으며 방향을 바꾸게 한다.
"""
import argparse
import datetime
import json
import math
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUN_IN_EDITOR = ROOT / "Tools" / "run_in_editor.py"
SAMPLER_PATH = ROOT / "Tools" / "MonsterAI" / "editor_turn_popping_sampler.py"
MEASUREMENT_ROOT = ROOT / "Docs" / "MonsterAI_CombatSim" / "measurements"
RESULT_MARKER = "TD_TURN_POPPING"
DEFAULT_SPECIES = ["Barghest", "Goblin", "Hyena", "StoneGolem"]
DEFAULT_MAP = "/Game/Combat/Maps/LV_TDMegaMagicArena"
EXIT_ENVIRONMENT = 2
PIE_START_WAIT_SECONDS = 120.0
SPAWN_WAIT_SECONDS = 20.0
SPAWN_SETTLE_SECONDS = 0.5
JOB_STALL_SECONDS = 30.0
CAPTURE_FILE_WAIT_FRAMES = 30
STAR_RADIUS_CM = 650.0
STAR_ORDER_DEGREES = (0.0, 144.0, 288.0, 72.0, 216.0)

OVER_RATE_DEG_S = 720.0
SNAP_MIN_DEG = 10.0
RATE_JUMP_DEG_S = 400.0
TURNING_DEG_S = 90.0
REVERSAL_WINDOW_SECONDS = 0.25
HOLD_STILL_DEG = 0.05
HOLD_MOVE_DEG = 1.0
HITCH_SECONDS = 1.0 / 30.0
TOP_EVENTS = 12

COLUMN_TIME, COLUMN_DELTA, COLUMN_X, COLUMN_Y, COLUMN_YAW, COLUMN_MESH, COLUMN_ROOT, COLUMN_PELVIS, COLUMN_VX, COLUMN_VY, COLUMN_STATE, COLUMN_ACTION, COLUMN_ALIVE = range(13)

DEFINITIONS = {
    "logic": "액터 요(Mover 가 정하는 논리 방향). 이동 중 회전 한도는 TurningRate(몬스터 540도/초)",
    "visual": "메시 컴포넌트 월드 요(화면에 보이는 몸 방향, 종 MeshYaw 상수 포함)",
    "pose": "골반(없으면 척추) 뼈 월드 요. 애니메이션이 몸을 돌리는 몫까지 포함",
    "rate": "연속 두 표본의 요 차(−180~180 감싸기) ÷ 그 프레임 월드 델타",
    "over_rate_frames": f"각속도 > {OVER_RATE_DEG_S:g}도/초 프레임(Mover 한도 540의 1.33배, 정상 회전으로는 나올 수 없음)",
    "snaps": f"초과 프레임 가운데 한 프레임 요 차 ≥ {SNAP_MIN_DEG:g}도(한 번에 돌아가는 팝핑)",
    "rate_jumps": f"연속 두 프레임 부호 있는 각속도 차 ≥ {RATE_JUMP_DEG_S:g}도/초(두 프레임 모두 델타 ≤ 1/30초). 회전이 가속 없이 바로 서거나 바로 최고 속도가 되는 끊김",
    "reversals": f"|각속도| ≥ {TURNING_DEG_S:g}도/초 회전 프레임의 부호가 직전 회전 프레임과 반대이고 간격 ≤ {REVERSAL_WINDOW_SECONDS:g}초(좌우로 흔들림)",
    "hold_jumps": f"가운데 프레임 요 차 < {HOLD_STILL_DEG:g}도, 양옆 ≥ {HOLD_MOVE_DEG:g}도 같은 방향(갱신이 건너뛴 뒤 몰아 도는 뚝뚝 끊김, 틱 예산 의심)",
    "visual_offset": "wrap(메시 요 − 액터 요 − 중앙값) 절대값(보이는 몸이 논리 방향에서 벗어난 각)",
    "pose_offset": "wrap(골반 요 − 메시 요 − 중앙값) 절대값(애니메이션이 몸을 돌리는 몫)",
    "per_minute": "개수 ÷ 살아 있는 표본 시간(분)",
}

PRELUDE = '''import unreal, json, sys
def _td_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
def _td_print(payload):
    print('{marker}', json.dumps(payload))
'''

LOAD_CODE = '''def _td_load():
    import importlib.util
    previous = sys.modules.get('td_turn_popping_sampler')
    restored = {{}}
    if previous:
        try:
            restored = previous.stop()
        except Exception as error:
            restored = {{'error': repr(error)}}
    spec = importlib.util.spec_from_file_location('td_turn_popping_sampler', r'{path}')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    sys.modules['td_turn_popping_sampler'] = module
    _td_print({{'loaded': True, 'previous': restored}})
_td_load()
del _td_load
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

RESET_CODE = '''def _td_reset():
    world = _td_world()
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDMonsterCharacter):
        actor.destroy_actor()
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDDamageEntity):
        actor.destroy_actor()
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    combat = pawn.get_component_by_class(unreal.TDCombatComponent)
    combat.set_stats(combat.get_stats(), True)
    origin = {origin!r}
    if origin is None:
        location = pawn.get_actor_location()
        rotation = pawn.get_actor_rotation()
        origin = [location.x, location.y, location.z, rotation.yaw]
    else:
        pawn.teleport(unreal.Vector(origin[0], origin[1], origin[2]), unreal.Rotator(0.0, origin[3], 0.0))
    _td_print({{'origin': origin}})
_td_reset()
del _td_reset
'''

SPAWN_CODE = '''def _td_spawn():
    world = _td_world()
    unreal.SystemLibrary.execute_console_command(world, 'TDSpawnMonster {species} {count} {distance}', unreal.GameplayStatics.get_player_controller(world, 0))
    _td_print({{'spawned': True}})
_td_spawn()
del _td_spawn
'''

COUNT_CODE = '''def _td_count():
    world = _td_world()
    monsters = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDMonsterCharacter) if world else []
    _td_print({'monsters': len(monsters)})
_td_count()
del _td_count
'''

CVAR_CODE = '''def _td_cvar():
    world = _td_world()
    values = {values!r}
    before = {{name: unreal.SystemLibrary.get_console_variable_float_value(name) for name in values}}
    for name, value in values.items():
        unreal.SystemLibrary.execute_console_command(world, name + ' ' + str(value), None)
    _td_print({{'before': before, 'after': {{name: unreal.SystemLibrary.get_console_variable_float_value(name) for name in values}}}})
_td_cvar()
del _td_cvar
'''

JOB_CODE = '''_td_print(sys.modules['td_turn_popping_sampler'].start_job({config!r}))
'''

JOB_STATUS_CODE = '''_td_print(sys.modules['td_turn_popping_sampler'].status())
'''

STOP_JOB_CODE = '''_td_print(sys.modules['td_turn_popping_sampler'].stop() if 'td_turn_popping_sampler' in sys.modules else {'stopped': False})
'''

STOP_PIE_CODE = '''def _td_stop():
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


def wait_until(predicate, timeout_seconds, poll_seconds=0.5):
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(poll_seconds)
    return None


def star_legs():
    points = [(STAR_RADIUS_CM * math.cos(math.radians(angle)), STAR_RADIUS_CM * math.sin(math.radians(angle))) for angle in STAR_ORDER_DEGREES]
    legs = [["hold", 2.5]]
    for index, point in enumerate(points + points):
        legs.append(["move", round(point[0], 1), round(point[1], 1)])
        if index % 2 == 1:
            legs.append(["hold", 1.2])
    return legs


def parse_cvars(pairs):
    values = {}
    for pair in pairs or []:
        name, _, value = pair.partition("=")
        values[name.strip()] = value.strip()
    return values


def wrap(angle):
    return (angle + 180.0) % 360.0 - 180.0


def percentile(values, fraction):
    if not values:
        return None
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, int(math.ceil(fraction * len(ordered))) - 1))
    return ordered[index]


def rounded(value, digits=2):
    return None if value is None else round(value, digits)


def alive_frames(frames):
    result = []
    for frame in frames:
        if not frame[COLUMN_ALIVE]:
            break
        result.append(frame)
    return result


def signed_rates(frames, column):
    rates = []
    for index in range(1, len(frames)):
        previous, current = frames[index - 1], frames[index]
        delta_seconds = current[COLUMN_DELTA]
        if current[column] is None or previous[column] is None or delta_seconds <= 0.0:
            rates.append(None)
            continue
        change = wrap(current[column] - previous[column])
        rates.append((change, change / delta_seconds, delta_seconds, current))
    return rates


def count_reversals(rates):
    count = 0
    last_sign = 0
    last_time = None
    for entry in rates:
        if entry is None or abs(entry[1]) < TURNING_DEG_S:
            continue
        sign = 1 if entry[1] > 0 else -1
        frame_time = entry[3][COLUMN_TIME]
        if last_sign and sign != last_sign and frame_time - last_time <= REVERSAL_WINDOW_SECONDS:
            count += 1
        last_sign = sign
        last_time = frame_time
    return count


def count_hold_jumps(rates):
    count = 0
    for index in range(1, len(rates) - 1):
        before, middle, after = rates[index - 1], rates[index], rates[index + 1]
        if None in (before, middle, after):
            continue
        if abs(middle[0]) < HOLD_STILL_DEG and abs(before[0]) >= HOLD_MOVE_DEG and abs(after[0]) >= HOLD_MOVE_DEG and before[0] * after[0] > 0.0:
            count += 1
    return count


def rate_metrics(frames, column):
    rates = signed_rates(frames, column)
    valid = [entry for entry in rates if entry is not None]
    if not valid:
        return None
    speeds = [abs(entry[1]) for entry in valid]
    over = [entry for entry in valid if abs(entry[1]) > OVER_RATE_DEG_S]
    snaps = [entry for entry in over if abs(entry[0]) >= SNAP_MIN_DEG]
    jumps = 0
    for index in range(1, len(rates)):
        previous, current = rates[index - 1], rates[index]
        if previous is None or current is None or previous[2] > HITCH_SECONDS or current[2] > HITCH_SECONDS:
            continue
        if abs(current[1] - previous[1]) >= RATE_JUMP_DEG_S:
            jumps += 1
    state_counts = {}
    for entry in over:
        state_counts[entry[3][COLUMN_STATE]] = state_counts.get(entry[3][COLUMN_STATE], 0) + 1
    events = sorted(snaps, key=lambda entry: -abs(entry[0]))[:TOP_EVENTS]
    return {
        "frames": len(valid), "max_deg_s": rounded(max(speeds)), "p99_deg_s": rounded(percentile(speeds, 0.99)), "p95_deg_s": rounded(percentile(speeds, 0.95)),
        "over_rate_frames": len(over), "snaps": len(snaps), "rate_jumps": jumps, "reversals": count_reversals(rates), "hold_jumps": count_hold_jumps(rates),
        "over_rate_states": state_counts,
        "snap_events": [{"t": rounded(entry[3][COLUMN_TIME], 3), "delta_deg": rounded(entry[0]), "rate_deg_s": rounded(entry[1], 0), "state": entry[3][COLUMN_STATE],
                         "action": entry[3][COLUMN_ACTION]} for entry in events],
        "speeds": speeds,
    }


def offset_metrics(frames, column_a, column_b):
    offsets = [wrap(frame[column_a] - frame[column_b]) for frame in frames if frame[column_a] is not None and frame[column_b] is not None]
    if not offsets:
        return None
    center = statistics.median(offsets)
    deviations = [abs(wrap(offset - center)) for offset in offsets]
    return {"center_deg": rounded(center), "p95_deg": rounded(percentile(deviations, 0.95)), "max_deg": rounded(max(deviations)), "deviations": deviations}


def analyze_monster(monster):
    frames = alive_frames(monster["frames"])
    if len(frames) < 3:
        return None
    seconds = frames[-1][COLUMN_TIME] - frames[0][COLUMN_TIME]
    deltas = [frame[COLUMN_DELTA] for frame in frames[1:]]
    pose_column = COLUMN_PELVIS if monster["info"].get("pelvis_bone") else COLUMN_ROOT
    moving = sum(1 for frame in frames if math.hypot(frame[COLUMN_VX], frame[COLUMN_VY]) > 50.0)
    states = {}
    for frame in frames:
        states[frame[COLUMN_STATE]] = states.get(frame[COLUMN_STATE], 0) + 1
    return {
        "name": monster["info"]["name"], "species": monster["info"]["species"], "info": monster["info"], "seconds": rounded(seconds, 3),
        "delta_p50_ms": rounded(1000.0 * percentile(deltas, 0.5)), "delta_max_ms": rounded(1000.0 * max(deltas)),
        "hitch_frames": sum(1 for value in deltas if value > HITCH_SECONDS), "moving_fraction": rounded(moving / len(frames), 3), "state_frames": states,
        "logic": rate_metrics(frames, COLUMN_YAW), "visual": rate_metrics(frames, COLUMN_MESH), "pose": rate_metrics(frames, pose_column),
        "visual_offset": offset_metrics(frames, COLUMN_MESH, COLUMN_YAW), "pose_offset": offset_metrics(frames, pose_column, COLUMN_MESH),
    }


def merge_layer(monsters, layer, minutes):
    entries = [monster[layer] for monster in monsters if monster.get(layer)]
    if not entries:
        return None
    speeds = [speed for entry in entries for speed in entry["speeds"]]
    merged = {"max_deg_s": rounded(max(speeds)), "p99_deg_s": rounded(percentile(speeds, 0.99)), "p95_deg_s": rounded(percentile(speeds, 0.95))}
    for key in ("over_rate_frames", "snaps", "rate_jumps", "reversals", "hold_jumps"):
        merged[key] = sum(entry[key] for entry in entries)
        merged[f"{key}_per_minute"] = rounded(merged[key] / minutes, 2) if minutes > 0 else None
    states = {}
    for entry in entries:
        for state, count in entry["over_rate_states"].items():
            states[state] = states.get(state, 0) + count
    merged["over_rate_states"] = states
    merged["snap_events"] = sorted([dict(event, monster=monster["name"]) for monster in monsters if monster.get(layer) for event in monster[layer]["snap_events"]],
                                   key=lambda event: -abs(event["delta_deg"]))[:TOP_EVENTS]
    return merged


def merge_offset(monsters, layer):
    deviations = [value for monster in monsters if monster.get(layer) for value in monster[layer]["deviations"]]
    if not deviations:
        return None
    return {"p95_deg": rounded(percentile(deviations, 0.95)), "max_deg": rounded(max(deviations))}


def strip_series(monster):
    for layer in ("logic", "visual", "pose"):
        if monster.get(layer):
            monster[layer].pop("speeds", None)
    for layer in ("visual_offset", "pose_offset"):
        if monster.get(layer):
            monster[layer].pop("deviations", None)
    return monster


def analyze_raw(raw):
    monsters = [result for result in (analyze_monster(monster) for monster in raw["monsters"]) if result]
    minutes = sum(monster["seconds"] for monster in monsters) / 60.0
    summary = {"monsters": len(monsters), "sample_minutes": rounded(minutes, 3), "result": raw.get("result"), "applied_fps": raw.get("applied_fps")}
    for layer in ("logic", "visual", "pose"):
        summary[layer] = merge_layer(monsters, layer, minutes)
    for layer in ("visual_offset", "pose_offset"):
        summary[layer] = merge_offset(monsters, layer)
    deltas = [monster["delta_max_ms"] for monster in monsters]
    summary["delta_max_ms"] = max(deltas) if deltas else None
    summary["hitch_frames"] = sum(monster["hitch_frames"] for monster in monsters)
    return {"summary": summary, "monsters": [strip_series(monster) for monster in monsters]}


def format_layer(layer):
    if not layer:
        return "–"
    return f"{layer['max_deg_s']:.0f}/{layer['p99_deg_s']:.0f}"


def summary_rows(label, report):
    rows = []
    for species, entry in report["species"].items():
        summary = entry.get("analysis", {}).get("summary")
        if not summary:
            rows.append(f"| {label} | {species} | 실패: {entry.get('error', '?')} |||||||||")
            continue
        logic, visual, pose = summary["logic"] or {}, summary["visual"] or {}, summary["pose"] or {}
        rows.append(
            f"| {label} | {species} | {summary['monsters']} | {summary['sample_minutes']:.2f} | {format_layer(logic)} | {format_layer(visual)} | {format_layer(pose)} | "
            f"{logic.get('over_rate_frames', '–')}/{visual.get('over_rate_frames', '–')} | {logic.get('snaps', '–')}/{visual.get('snaps', '–')} | "
            f"{logic.get('rate_jumps_per_minute', '–')}/{visual.get('rate_jumps_per_minute', '–')} | {logic.get('reversals_per_minute', '–')}/{visual.get('reversals_per_minute', '–')} | "
            f"{logic.get('hold_jumps', '–')}/{visual.get('hold_jumps', '–')} | {(summary['visual_offset'] or {}).get('p95_deg', '–')}/{(summary['visual_offset'] or {}).get('max_deg', '–')} | "
            f"{(summary['pose_offset'] or {}).get('p95_deg', '–')} | {summary['delta_max_ms']} |")
    return rows


TABLE_HEADER = ("| 라벨 | 종 | 마리 | 표본 분 | 논리 최대/p99 도/초 | 보이는 몸 최대/p99 | 골반 최대/p99 | 720 초과 프레임 논리/보임 | 급회전 논리/보임 | 각속도 급변/분 논리/보임 | "
                "흔들림/분 논리/보임 | 몰아 돌기 논리/보임 | 몸-논리 어긋남 p95/최대 | 골반-몸 p95 | 최대 델타 ms |\n|" + "---|" * 15)


def write_summary(path, label, report):
    lines = [f"# 회전 팝핑 측정 {label}", "", f"- 맵 {report['map']}, 종마다 {report['count']}마리·{report['seconds']}초, 고정 FPS {report['fixed_fps']}, cvar {report['cvars']}", "",
             TABLE_HEADER, *summary_rows(label, report), "", "## 급회전 상위(보이는 몸)", ""]
    for species, entry in report["species"].items():
        visual = ((entry.get("analysis") or {}).get("summary") or {}).get("visual") or {}
        for event in visual.get("snap_events", [])[:5]:
            lines.append(f"- {species} {event['monster']} t={event['t']} Δ{event['delta_deg']}° {event['rate_deg_s']}도/초 상태 {event['state']} 행동 {event['action']}")
    lines += ["", "## 정의", ""] + [f"- {key}: {value}" for key, value in DEFINITIONS.items()]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def summarize(task, labels):
    lines = [TABLE_HEADER]
    for label in labels:
        report = json.loads((MEASUREMENT_ROOT / task / label / "report.json").read_text(encoding="utf-8"))
        lines += summary_rows(label, report)
    print("\n".join(lines))


def raw_directory(task, label):
    return ROOT / "Saved" / "AgentOps" / datetime.date.today().strftime("%Y%m%d") / "turn-popping" / task / label / "raw"


def wait_for_job(seconds):
    started = time.monotonic()
    progressed = started
    last_game = None
    while True:
        job_status = run_editor_code(JOB_STATUS_CODE)
        if job_status.get("done"):
            return job_status
        now = time.monotonic()
        game_seconds = job_status.get("game_seconds")
        if game_seconds is not None and (last_game is None or game_seconds > last_game):
            last_game = game_seconds
            progressed = now
        if now - started > seconds and now - progressed > JOB_STALL_SECONDS:
            return dict(job_status, stalled=True)
        time.sleep(1.0)


def measure_species(arguments, species, origin, raw_dir):
    reset = run_editor_code(RESET_CODE.format(origin=origin))
    if "origin" not in reset:
        return origin, {"error": f"reset failed {reset}"}
    origin = reset["origin"]
    time.sleep(0.5)
    run_editor_code(SPAWN_CODE.format(species=species, count=arguments.count, distance=arguments.distance))
    if not wait_until(lambda: run_editor_code(COUNT_CODE).get("monsters", 0) >= arguments.count, SPAWN_WAIT_SECONDS):
        return origin, {"error": "monsters did not spawn"}
    time.sleep(SPAWN_SETTLE_SECONDS)
    out_path = raw_dir / f"{species}.json"
    config = {"label": f"{arguments.label}:{species}", "out_path": str(out_path), "seconds": arguments.seconds, "fixed_fps": arguments.fixed_fps, "legs": star_legs()}
    if species in (arguments.capture_species or []):
        frames_dir = raw_dir.parent / "frames" / species
        frames_dir.mkdir(parents=True, exist_ok=True)
        config["capture"] = {"start": arguments.capture_start, "seconds": arguments.capture_seconds, "every_frames": arguments.capture_every, "size": arguments.capture_size,
                             "out_dir": frames_dir.as_posix(), "prefix": species, "camera_arm_cm": arguments.camera_arm, "focus_index": arguments.capture_focus,
                             "file_wait_frames": CAPTURE_FILE_WAIT_FRAMES}
    started = run_editor_code(JOB_CODE.format(config=config))
    if "started" not in started:
        return origin, {"error": f"job start failed {started}"}
    finished = wait_for_job(arguments.seconds + 20.0)
    if not finished.get("done"):
        run_editor_code(STOP_JOB_CODE)
        return origin, {"error": f"job did not finish {finished}"}
    raw = json.loads(out_path.read_text(encoding="utf-8"))
    return origin, {"result": finished.get("result"), "raw": str(out_path.relative_to(ROOT)), "monsters": started.get("monsters"), "analysis": analyze_raw(raw)}


def write_report(arguments, results):
    out_dir = MEASUREMENT_ROOT / arguments.task / arguments.label
    out_dir.mkdir(parents=True, exist_ok=True)
    report = {"label": arguments.label, "task": arguments.task, "map": arguments.map, "count": arguments.count, "distance": arguments.distance, "seconds": arguments.seconds,
              "fixed_fps": arguments.fixed_fps, "cvars": parse_cvars(arguments.cvar), "legs": star_legs(), "measured_at": datetime.datetime.now().isoformat(timespec="seconds"),
              "definitions": DEFINITIONS, "species": results}
    (out_dir / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding="utf-8")
    write_summary(out_dir / "summary.md", arguments.label, report)
    print("\n".join([TABLE_HEADER, *summary_rows(arguments.label, report)]))
    return report


def analyze_only(arguments):
    raw_dir = raw_directory(arguments.task, arguments.label)
    previous_path = MEASUREMENT_ROOT / arguments.task / arguments.label / "report.json"
    previous = json.loads(previous_path.read_text(encoding="utf-8")) if previous_path.exists() else {}
    results = {}
    for species in arguments.species:
        recorded = (previous.get("species", {}).get(species) or {}).get("raw")
        path = ROOT / recorded if recorded else raw_dir / f"{species}.json"
        if not path.is_file():
            results[species] = {"error": f"no raw {path}"}
            continue
        results[species] = {"result": "reanalyzed", "raw": str(path.relative_to(ROOT)), "analysis": analyze_raw(json.loads(path.read_text(encoding="utf-8")))}
    write_report(arguments, results)
    return 0


def run(arguments):
    raw_dir = raw_directory(arguments.task, arguments.label)
    raw_dir.mkdir(parents=True, exist_ok=True)
    loaded = run_editor_code(LOAD_CODE.format(path=SAMPLER_PATH.as_posix()))
    if "loaded" not in loaded:
        print(json.dumps({"error": "sampler load failed", "detail": loaded}, ensure_ascii=False))
        return EXIT_ENVIRONMENT
    if "error" in run_editor_code(START_CODE.format(level=arguments.map)):
        return EXIT_ENVIRONMENT
    if not wait_until(lambda: run_editor_code(STATUS_CODE).get("pawn"), PIE_START_WAIT_SECONDS, 2.0):
        print(json.dumps({"error": "PIE did not start"}))
        return EXIT_ENVIRONMENT
    time.sleep(3.0)
    cvars = parse_cvars(arguments.cvar)
    cvar_before = run_editor_code(CVAR_CODE.format(values=cvars)).get("before", {}) if cvars else {}
    results = {}
    origin = None
    try:
        for species in arguments.species:
            origin, results[species] = measure_species(arguments, species, origin, raw_dir)
            print(json.dumps({"species": species, "result": results[species].get("result"), "error": results[species].get("error")}, ensure_ascii=False))
    finally:
        run_editor_code(STOP_JOB_CODE)
        if cvar_before:
            run_editor_code(CVAR_CODE.format(values={name: (int(value) if float(value).is_integer() else value) for name, value in cvar_before.items()}))
        if not arguments.keep_pie:
            run_editor_code(STOP_PIE_CODE)
            wait_until(lambda: not run_editor_code(STATUS_CODE).get("pie", True), 30.0, 1.0)
    write_report(arguments, results)
    return 0 if all("analysis" in entry for entry in results.values()) else 1


def main():
    parser = argparse.ArgumentParser(description="몬스터 회전 팝핑 PIE 측정(M3-42)")
    parser.add_argument("--label")
    parser.add_argument("--task", default="M3-42")
    parser.add_argument("--species", nargs="+", default=DEFAULT_SPECIES)
    parser.add_argument("--count", type=int, default=3)
    parser.add_argument("--distance", type=float, default=900.0)
    parser.add_argument("--seconds", type=float, default=30.0)
    parser.add_argument("--fixed-fps", type=float, default=60.0)
    parser.add_argument("--cvar", nargs="*", default=[])
    parser.add_argument("--map", default=DEFAULT_MAP)
    parser.add_argument("--keep-pie", action="store_true")
    parser.add_argument("--capture-species", nargs="*", default=[])
    parser.add_argument("--capture-start", type=float, default=2.0)
    parser.add_argument("--capture-seconds", type=float, default=6.0)
    parser.add_argument("--capture-every", type=int, default=2)
    parser.add_argument("--capture-size", default="960x540")
    parser.add_argument("--camera-arm", type=float, default=900.0)
    parser.add_argument("--capture-focus", type=int, default=0)
    parser.add_argument("--analyze-only", action="store_true")
    parser.add_argument("--summarize", nargs="+")
    arguments = parser.parse_args()
    if arguments.summarize:
        summarize(arguments.task, arguments.summarize)
        return 0
    if not arguments.label:
        parser.error("--label is required")
    if arguments.analyze_only:
        return analyze_only(arguments)
    return run(arguments)


if __name__ == "__main__":
    sys.exit(main())
