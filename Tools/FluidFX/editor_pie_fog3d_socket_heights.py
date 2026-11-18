"""실행 환경: 시스템 Python(언리얼 에디터가 TDGame 을 열고 MCP 포트 8000·원격 실행이 켜진 상태, 에디터 앞창 유지 L-editor-06). 3D 안개 무대 PIE 하나에서 폰의 칼날 밑·끝(소켓 HandGrip_R 에 데이터의 BladeBaseLocalCm·BladeTipLocalCm 적용)과 hand_r·foot_l·foot_r·pelvis 의 바닥 위 높이를 휘두르기 2.5초 동안과 600cm 걷기 동안 슬레이트 틱마다 기록한다.
실행 명령: python -B Tools/FluidFX/editor_pie_fog3d_socket_heights.py [--level /Game/FluidFX/Maps/LV_TDFog3DStage] [--fresh-pie] [--stop-pie] [--output-dir <폴더>] [--thresholds 30 60 90 130 170] [--tuning <조절 표 JSON>] (PowerShell), 점검만: --self-test
출력: <output-dir>(기본 Saved/FluidFX/fog3d_calib/socket_heights/<시각>)/socket_heights.json(점마다 최소·최대·중앙값·문턱 아래 비율, 틱 표본 전부), socket_heights.md(요약 표). 종료 코드 0 기록 완료 / 1 기록 실패(소켓 없음·시간 초과 등) / 2 환경 문제.
상태: 미검증(에디터 없이 가짜 unreal 로 상태 기계·통계·원격 코드 문법만 --self-test, 실제 PIE 에서 돌린 적 없음). 소켓 변환 transform_location 과 does_socket_exist 이름은 첫 실행에서 확인해야 한다.
"""
import argparse
import json
import math
import re
import statistics
import sys
import tempfile
import time
import types
import uuid
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CANONICAL_TUNING_PATH = ROOT / "Content" / "FluidFX" / "PresetData" / "TDFluidPresetTuning.json"
STAGE_SCRIPT_PATH = ROOT / "Tools" / "FluidFX" / "editor_make_fog3d_stage.py"
DEFAULT_LEVEL_PATH = "/Game/FluidFX/Maps/LV_TDFog3DStage"
DEFAULT_OUTPUT_ROOT = ROOT / "Saved" / "FluidFX" / "fog3d_calib" / "socket_heights"
SETTINGS_GROUP_NAME = "TDFog3DSettings"
RESULT_MARKER = "TD_FOG3D_SOCKET_HEIGHTS_RESULT"
DEFAULT_THRESHOLDS_CM = (30.0, 60.0, 90.0, 130.0, 170.0)
WARMUP_SECONDS = 1.0
SWING_SECONDS = 2.5
SETTLE_SECONDS = 0.5
WALK_DISTANCE_CM = 600.0
WALK_YAW_DEGREES = 0.0
FIXED_FRAMES_PER_SECOND = 60.0
OUTER_WAIT_SECONDS = 120.0
OUTER_POLL_SECONDS = 0.5
POINT_ORDER = ("blade_base", "blade_tip", "hand_r", "foot_l", "foot_r", "pelvis")
BONE_FIELDS = {"hand_r": "HandBoneName", "foot_l": "LeftFootBoneName", "foot_r": "RightFootBoneName", "pelvis": "PelvisBoneName"}
EXIT_FAILED = 1
EXIT_ENVIRONMENT = 2

REMOTE_CODE = r'''
import json
import math
import traceback

import unreal

PARAMS = json.loads(__PARAMS__)
RESULT_MARKER = "TD_FOG3D_SOCKET_HEIGHTS_RESULT"
STATE_ATTRIBUTE = "td_fog3d_socket_heights"
FIXED_STEP_VARIABLE = "t.OverrideFPS"


def game_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def find_mesh(pawn):
    try:
        mesh = pawn.get_editor_property("mesh")
    except Exception:
        mesh = None
    if mesh is None:
        mesh = pawn.get_component_by_class(unreal.SkeletalMeshComponent)
    return mesh


def transform_local_point(transform, point):
    vector = unreal.Vector(point[0], point[1], point[2])
    if hasattr(transform, "transform_location"):
        return transform.transform_location(vector)
    return unreal.MathLibrary.transform_location(transform, vector)


def read_point_heights(mesh, state):
    floor_top = PARAMS["floor_top_z_cm"]
    heights = {}
    socket_name = PARAMS["blade_socket"]
    if socket_name and mesh.does_socket_exist(socket_name):
        transform = mesh.get_socket_transform(socket_name, unreal.RelativeTransformSpace.RTS_WORLD)
        heights["blade_base"] = transform_local_point(transform, PARAMS["blade_base_local_cm"]).z - floor_top
        heights["blade_tip"] = transform_local_point(transform, PARAMS["blade_tip_local_cm"]).z - floor_top
    else:
        state["problems"].add("소켓 없음: " + str(socket_name))
    for key, bone_name in PARAMS["bones"].items():
        if mesh.does_socket_exist(bone_name):
            heights[key] = mesh.get_socket_location(bone_name).z - floor_top
        else:
            state["problems"].add("뼈 없음: " + str(bone_name))
    return heights


def write_status(state, error, done):
    payload = {"done": done, "error": error, "phase": state["phase"], "problems": sorted(state["problems"]), "swing_samples": state["swing_samples"],
               "walk_samples": state["walk_samples"], "walk": state["walk"], "fixed_step": state["fixed_step"]}
    with open(PARAMS["raw_path"], "w", encoding="utf-8") as handle:
        json.dump(payload, handle, ensure_ascii=False)


def restore_fixed_step(state):
    before = state["fixed_step"].get("before")
    if before is None:
        return
    state["fixed_step"]["before"] = None
    unreal.SystemLibrary.execute_console_command(game_world(), FIXED_STEP_VARIABLE + " " + str(before), None)
    state["fixed_step"]["restored"] = unreal.SystemLibrary.get_console_variable_float_value(FIXED_STEP_VARIABLE)


def finish(state, error):
    if getattr(unreal, STATE_ATTRIBUTE, None) is state:
        setattr(unreal, STATE_ATTRIBUTE, None)
    if state["handle"] is not None:
        unreal.unregister_slate_post_tick_callback(state["handle"])
        state["handle"] = None
    try:
        restore_fixed_step(state)
    except Exception as cleanup_error:
        state["fixed_step"]["cleanup_error"] = str(cleanup_error)[-200:]
    state["phase"] = "done"
    write_status(state, error, True)


def enter_phase(state, phase, now):
    state["phase"] = phase
    state["phase_start"] = now


def begin_walk(state, world, pawn, now):
    max_speed = float(pawn.get_max_move_speed())
    if max_speed <= 0.0:
        finish(state, "최대 속도 " + str(max_speed))
        return
    location = pawn.get_actor_location()
    yaw = PARAMS["walk_yaw_degrees"]
    state["walk"] = {"start": [location.x, location.y], "distance_cm": PARAMS["walk_distance_cm"], "yaw": yaw, "max_speed": max_speed,
                     "timeout_seconds": 2.0 * PARAMS["walk_distance_cm"] / max_speed + 3.0, "moved_cm": 0.0, "game_seconds": 0.0, "completed": False}
    state["direction"] = unreal.Vector(math.cos(math.radians(yaw)), math.sin(math.radians(yaw)), 0.0)
    enter_phase(state, "walk", now)


def advance(state):
    world = game_world()
    if world is None:
        finish(state, "PIE 게임 월드가 사라졌다")
        return
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    mesh = find_mesh(pawn) if pawn is not None else None
    if mesh is None:
        finish(state, "플레이어 폰 또는 스켈레탈 메시 없음")
        return
    now = unreal.GameplayStatics.get_time_seconds(world)
    elapsed = now - state["phase_start"]
    phase = state["phase"]
    if phase == "warmup":
        if elapsed >= PARAMS["warmup_seconds"]:
            enter_phase(state, "swing", now)
            unreal.SystemLibrary.execute_console_command(world, PARAMS["swing_command"], unreal.GameplayStatics.get_player_controller(world, 0))
        return
    if phase == "swing":
        state["swing_samples"].append({"t": elapsed, "heights": read_point_heights(mesh, state)})
        if elapsed >= PARAMS["swing_seconds"]:
            enter_phase(state, "settle", now)
        return
    if phase == "settle":
        if elapsed >= PARAMS["settle_seconds"]:
            begin_walk(state, world, pawn, now)
        return
    walk = state["walk"]
    location = pawn.get_actor_location()
    walk["moved_cm"] = math.hypot(location.x - walk["start"][0], location.y - walk["start"][1])
    walk["game_seconds"] = elapsed
    state["walk_samples"].append({"t": elapsed, "heights": read_point_heights(mesh, state)})
    if walk["moved_cm"] >= walk["distance_cm"]:
        walk["completed"] = True
        finish(state, None)
        return
    if elapsed > walk["timeout_seconds"]:
        finish(state, "걷기 시한 초과 " + str(walk["moved_cm"]) + "/" + str(walk["distance_cm"]))
        return
    pawn.add_movement_input(state["direction"], 1.0, True)


def tick_callback(state):
    def tick(delta_seconds):
        try:
            advance(state)
        except Exception:
            finish(state, traceback.format_exc()[-600:])
    return tick


def start():
    previous = getattr(unreal, STATE_ATTRIBUTE, None)
    if previous is not None:
        finish(previous, "새 기록이 이전 기록을 대체했다")
    world = game_world()
    if world is None:
        print(RESULT_MARKER + " " + json.dumps({"error": "PIE 게임 월드 없음"}))
        return
    now = unreal.GameplayStatics.get_time_seconds(world)
    state = {"phase": "warmup", "phase_start": now, "handle": None, "swing_samples": [], "walk_samples": [], "problems": set(), "walk": None, "direction": None, "fixed_step": {}}
    state["fixed_step"] = {"before": unreal.SystemLibrary.get_console_variable_float_value(FIXED_STEP_VARIABLE)}
    unreal.SystemLibrary.execute_console_command(world, FIXED_STEP_VARIABLE + " " + str(PARAMS["fixed_fps"]), None)
    state["fixed_step"]["applied"] = unreal.SystemLibrary.get_console_variable_float_value(FIXED_STEP_VARIABLE)
    write_status(state, None, False)
    state["handle"] = unreal.register_slate_post_tick_callback(tick_callback(state))
    setattr(unreal, STATE_ATTRIBUTE, state)
    print(RESULT_MARKER + " " + json.dumps({"started": True, "game_time": now, "fixed_step": state["fixed_step"]}))


start()
'''


class InputProblem(Exception):
    pass


def load_settings(tuning_path):
    data = json.loads(Path(tuning_path).read_text(encoding="utf-8"))
    try:
        return data["Globals"][SETTINGS_GROUP_NAME]
    except KeyError as error:
        raise InputProblem(f"{tuning_path} 에 Globals.{SETTINGS_GROUP_NAME} 가 없다: {error}")


def read_floor_top_cm():
    match = re.search(r"^FLOOR_TOP_Z_CM\s*=\s*(-?[\d.]+)", STAGE_SCRIPT_PATH.read_text(encoding="utf-8"), re.MULTILINE)
    if match is None:
        raise InputProblem(f"{STAGE_SCRIPT_PATH} 에서 FLOOR_TOP_Z_CM 을 찾지 못했다")
    return float(match.group(1))


def vector_list(value):
    return [float(value["X"]), float(value["Y"]), float(value["Z"])]


def build_remote_parameters(settings, floor_top_cm, raw_path):
    return {"blade_socket": settings["BladeSocketName"], "blade_base_local_cm": vector_list(settings["BladeBaseLocalCm"]), "blade_tip_local_cm": vector_list(settings["BladeTipLocalCm"]),
            "bones": {key: settings[field] for key, field in BONE_FIELDS.items()}, "floor_top_z_cm": floor_top_cm, "warmup_seconds": WARMUP_SECONDS, "swing_seconds": SWING_SECONDS,
            "settle_seconds": SETTLE_SECONDS, "walk_distance_cm": WALK_DISTANCE_CM, "walk_yaw_degrees": WALK_YAW_DEGREES, "fixed_fps": FIXED_FRAMES_PER_SECOND,
            "swing_command": "TDPlayMeleeAction", "raw_path": Path(raw_path).as_posix()}


def build_remote_code(parameters):
    return REMOTE_CODE.replace("__PARAMS__", repr(json.dumps(parameters)))


def summarize_samples(samples, thresholds):
    summary = {}
    for point in POINT_ORDER:
        values = [sample["heights"][point] for sample in samples if point in sample["heights"]]
        if not values:
            summary[point] = {"count": 0}
            continue
        summary[point] = {"count": len(values), "min_cm": min(values), "max_cm": max(values), "median_cm": statistics.median(values),
                          "fraction_below_cm": {f"{threshold:g}": sum(1 for value in values if value < threshold) / len(values) for threshold in thresholds}}
    return summary


def build_markdown(result, thresholds):
    lines = ["# 칼날·몸 높이(바닥 위, cm)", "", f"바닥 윗면 z = {result['meta']['floor_top_z_cm']:g}cm, 틱 표본(고정 {FIXED_FRAMES_PER_SECOND:g}fps). 문턱 아래 비율은 표본 중 높이가 문턱보다 낮은 비율.", ""]
    for phase_key, title in (("swing", f"휘두르기 {SWING_SECONDS:g}초"), ("walk", f"{WALK_DISTANCE_CM:g}cm 걷기")):
        phase = result[phase_key]
        lines += [f"## {title} (표본 {phase['sample_count']}개)", "", "| 점 | 최소 | 중앙값 | 최대 | " + " | ".join(f"<{threshold:g}cm" for threshold in thresholds) + " |",
                  "|" + "|".join(["---"] * (4 + len(thresholds))) + "|"]
        for point in POINT_ORDER:
            stats = phase["points"][point]
            if not stats["count"]:
                lines.append(f"| {point} | - | - | - |" + " - |" * len(thresholds))
                continue
            fractions = " | ".join(f"{stats['fraction_below_cm'][f'{threshold:g}']:.2f}" for threshold in thresholds)
            lines.append(f"| {point} | {stats['min_cm']:.1f} | {stats['median_cm']:.1f} | {stats['max_cm']:.1f} | {fractions} |")
        lines.append("")
    if result["problems"]:
        lines += ["## 문제", ""] + [f"- {problem}" for problem in result["problems"]]
    return "\n".join(lines) + "\n"


def build_result(raw, floor_top_cm, thresholds, settings):
    swing, walk = raw.get("swing_samples", []), raw.get("walk_samples", [])
    problems = list(raw.get("problems", []))
    if raw.get("error"):
        problems.append("기록 오류: " + str(raw["error"]))
    if not swing:
        problems.append("휘두르기 표본 0개")
    if not (raw.get("walk") or {}).get("completed"):
        problems.append("걷기가 끝까지 기록되지 않았다")
    return {"meta": {"floor_top_z_cm": floor_top_cm, "thresholds_cm": list(thresholds), "settings_used": {key: settings[key] for key in ("BladeSocketName", "BladeBaseLocalCm", "BladeTipLocalCm", *BONE_FIELDS.values())},
                     "fixed_step": raw.get("fixed_step"), "walk": raw.get("walk")},
            "swing": {"sample_count": len(swing), "duration_seconds": swing[-1]["t"] if swing else None, "points": summarize_samples(swing, thresholds)},
            "walk": {"sample_count": len(walk), "duration_seconds": walk[-1]["t"] if walk else None, "points": summarize_samples(walk, thresholds)},
            "problems": problems, "swing_samples": swing, "walk_samples": walk}


def parse_remote_lines(lines):
    for position, line in enumerate(lines):
        index = line.find(RESULT_MARKER)
        if index < 0:
            continue
        text = line[index + len(RESULT_MARKER):].strip()
        try:
            return json.JSONDecoder().raw_decode(text)[0]
        except ValueError as error:
            return {"error": f"결과 해석 실패: {error}: {text[:200]}"}
    return {"error": "결과 표식 없음: " + " | ".join(lines)[-300:]}


def ensure_pie(arguments):
    sys.path.insert(0, str(ROOT / "Tools"))
    sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))
    import pie_check_fluid
    import pie_showcase_fluid
    import uemcp
    environment = {}
    session = None
    if arguments.fresh_pie and pie_check_fluid.wait_for_pie_state(True, 0.5):
        session = uemcp.connect()
        if not pie_check_fluid.stop_pie(session, environment):
            raise InputProblem("남은 PIE 를 멈추지 못했다: " + json.dumps(environment, ensure_ascii=False)[:300])
    if not pie_check_fluid.wait_for_pie_state(True, 0.5):
        level_status = pie_showcase_fluid.open_stage_level(arguments.level)
        if not level_status.get("loaded"):
            raise InputProblem("레벨을 열지 못했다: " + json.dumps(level_status, ensure_ascii=False))
        session = uemcp.connect()
        pie_check_fluid.wait_for_editor_navigation()
        if not pie_check_fluid.start_pie(session, environment) or not pie_check_fluid.wait_for_pie_state(True, pie_check_fluid.PIE_START_WAIT_SECONDS):
            raise InputProblem("PIE 를 시작하지 못했다: " + json.dumps(environment, ensure_ascii=False)[:300])
        time.sleep(3.0)
    return session, environment, pie_check_fluid, uemcp


def wait_for_raw(raw_path):
    deadline = time.monotonic() + OUTER_WAIT_SECONDS
    raw = {}
    while time.monotonic() < deadline:
        try:
            raw = json.loads(Path(raw_path).read_text(encoding="utf-8"))
        except (OSError, ValueError):
            raw = raw or {}
        if raw.get("done"):
            return raw
        time.sleep(OUTER_POLL_SECONDS)
    return dict(raw, error="바깥 Python 대기 시간 초과", done=False)


def record(arguments):
    settings = load_settings(arguments.tuning)
    floor_top_cm = read_floor_top_cm()
    output_directory = Path(arguments.output_dir) if arguments.output_dir else DEFAULT_OUTPUT_ROOT / datetime.now().strftime("%Y%m%d-%H%M%S")
    output_directory = output_directory if output_directory.is_absolute() else ROOT / output_directory
    output_directory.mkdir(parents=True, exist_ok=True)
    raw_path = output_directory / f"raw_{uuid.uuid4().hex}.json"
    session, environment, pie_check_fluid, uemcp = ensure_pie(arguments)
    try:
        sys.path.insert(0, str(ROOT / "Tools"))
        import run_in_editor
        outcome = run_in_editor.run_code(build_remote_code(build_remote_parameters(settings, floor_top_cm, raw_path)))
        if not outcome["connected"]:
            raise InputProblem("에디터 원격 실행에 연결하지 못했다")
        started = parse_remote_lines(outcome["lines"])
        if "error" in started:
            raise InputProblem("기록 시작 실패: " + started["error"])
        raw = wait_for_raw(raw_path)
    finally:
        if arguments.stop_pie:
            session = session or uemcp.connect()
            pie_check_fluid.stop_pie(session, environment)
    result = build_result(raw, floor_top_cm, arguments.thresholds, settings)
    raw_path.unlink(missing_ok=True)
    (output_directory / "socket_heights.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    (output_directory / "socket_heights.md").write_text(build_markdown(result, arguments.thresholds), encoding="utf-8", newline="\n")
    print("기록:", (output_directory / "socket_heights.json").as_posix())
    print("요약:", (output_directory / "socket_heights.md").as_posix())
    for problem in result["problems"]:
        print("문제:", problem)
    return EXIT_FAILED if result["problems"] else 0


def check(condition, label, failures):
    if not condition:
        failures.append(label)
    print(("통과 " if condition else "실패 ") + label)


class FakeVector:
    def __init__(self, x, y, z):
        self.x, self.y, self.z = x, y, z


class FakeTransform:
    def __init__(self, z):
        self.z = z

    def transform_location(self, vector):
        return FakeVector(vector.x, vector.y, self.z + vector.z)


class FakeMesh:
    def __init__(self, world):
        self.world = world

    def does_socket_exist(self, name):
        return name != "missing_bone"

    def height(self, offset):
        return offset + 10.0 * math.sin(self.world.time * 4.0)

    def get_socket_transform(self, name, space):
        return FakeTransform(self.height(50.0))

    def get_socket_location(self, name):
        return FakeVector(0.0, 0.0, self.height({"hand_r": 90.0, "foot_l": 8.0, "foot_r": 12.0, "pelvis": 95.0}[name]))


class FakePawn:
    def __init__(self, world):
        self.world = world
        self.x = 0.0
        self.mesh = FakeMesh(world)
        self.pending_input = False

    def get_editor_property(self, name):
        return self.mesh

    def get_max_move_speed(self):
        return 600.0

    def get_actor_location(self):
        return FakeVector(self.x, 0.0, 0.0)

    def add_movement_input(self, direction, scale, force):
        self.pending_input = True


def build_fake_unreal(world, pawn, console_log):
    module = types.ModuleType("unreal")
    module.Vector = FakeVector
    module.UnrealEditorSubsystem = object
    module.SkeletalMeshComponent = object
    module.RelativeTransformSpace = types.SimpleNamespace(RTS_WORLD="world")
    module.get_editor_subsystem = lambda subsystem_class: types.SimpleNamespace(get_game_world=lambda: world)
    module.GameplayStatics = types.SimpleNamespace(get_time_seconds=lambda current_world: current_world.time, get_player_pawn=lambda current_world, index: pawn,
                                                   get_player_controller=lambda current_world, index: "controller")
    cvars = {"t.OverrideFPS": 0.0}

    def execute_console_command(current_world, command, controller):
        console_log.append(command)
        parts = command.split()
        if len(parts) == 2 and parts[0] in cvars:
            cvars[parts[0]] = float(parts[1])

    module.SystemLibrary = types.SimpleNamespace(execute_console_command=execute_console_command, get_console_variable_float_value=lambda name: cvars[name])
    module.callbacks = []
    module.register_slate_post_tick_callback = lambda function: module.callbacks.append(function) or len(module.callbacks)
    module.unregister_slate_post_tick_callback = lambda handle: module.callbacks.__setitem__(handle - 1, None)
    return module


def run_fake_recording(raw_path, floor_top_cm):
    world = types.SimpleNamespace(time=100.0)
    pawn = FakePawn(world)
    console_log = []
    fake = build_fake_unreal(world, pawn, console_log)
    settings = load_settings(CANONICAL_TUNING_PATH)
    parameters = build_remote_parameters(settings, floor_top_cm, raw_path)
    sys.modules["unreal"] = fake
    try:
        exec(compile(build_remote_code(parameters), "remote_code", "exec"), {"__name__": "remote_code"})
        for _ in range(2000):
            callback = fake.callbacks[0] if fake.callbacks else None
            if callback is None:
                break
            world.time += 1.0 / 60.0
            if pawn.pending_input:
                pawn.x += 600.0 / 60.0
                pawn.pending_input = False
            callback(1.0 / 60.0)
    finally:
        del sys.modules["unreal"]
    return json.loads(Path(raw_path).read_text(encoding="utf-8")), console_log


def run_self_test():
    failures = []
    settings = load_settings(CANONICAL_TUNING_PATH)
    check(settings["BladeSocketName"] == "HandGrip_R" and all(settings[field] for field in BONE_FIELDS.values()), "정본 데이터의 소켓·뼈 이름 읽기", failures)
    floor_top_cm = read_floor_top_cm()
    check(floor_top_cm == 0.0, "무대 스크립트의 바닥 윗면 높이 읽기(0cm)", failures)
    parameters = build_remote_parameters(settings, floor_top_cm, "C:/x/raw.json")
    code = build_remote_code(parameters)
    check(".py" not in code, "원격 코드에 .py 낱말이 없다(L-editor 원격 실행 함정)", failures)
    check(f"{parameters['blade_base_local_cm'][2]:g}" == "6.06", "칼날 밑 로컬 좌표를 데이터에서 읽음", failures)
    samples = [{"t": index, "heights": {"blade_base": float(index), "foot_l": 10.0}} for index in range(1, 6)]
    summary = summarize_samples(samples, (3.0, 10.0))
    check(summary["blade_base"]["min_cm"] == 1.0 and summary["blade_base"]["max_cm"] == 5.0 and summary["blade_base"]["median_cm"] == 3.0, "점 통계: 최소·최대·중앙값", failures)
    check(summary["blade_base"]["fraction_below_cm"] == {"3": 0.4, "10": 1.0} and summary["foot_l"]["fraction_below_cm"]["10"] == 0.0, "점 통계: 문턱 아래 비율", failures)
    check(summary["pelvis"] == {"count": 0}, "표본 없는 점은 개수 0", failures)
    with tempfile.TemporaryDirectory() as directory:
        raw_path = Path(directory) / "raw.json"
        raw, console_log = run_fake_recording(raw_path, floor_top_cm)
        check(raw["done"] and raw["error"] is None, "가짜 에디터: 기록이 오류 없이 끝남", failures)
        check("TDPlayMeleeAction" in console_log and "t.OverrideFPS 60.0" in console_log and raw["fixed_step"]["restored"] == 0.0, "휘두르기 명령 실행, 고정 틱 설정·복구", failures)
        check(len(raw["swing_samples"]) >= 150 and abs(raw["swing_samples"][-1]["t"] - SWING_SECONDS) < 0.02, f"휘두르기 표본 {len(raw['swing_samples'])}개, 마지막 시각 2.5초", failures)
        check(raw["walk"]["completed"] and raw["walk"]["moved_cm"] >= WALK_DISTANCE_CM and len(raw["walk_samples"]) > 50, f"걷기 600cm 완료, 표본 {len(raw['walk_samples'])}개", failures)
        check(set(raw["swing_samples"][0]["heights"]) == set(POINT_ORDER), "표본마다 여섯 점 높이가 있다", failures)
        result = build_result(raw, floor_top_cm, DEFAULT_THRESHOLDS_CM, settings)
        check(not result["problems"], "결과 조립: 문제 없음", failures)
        base = result["swing"]["points"]["blade_base"]
        check(abs(base["median_cm"] - 56.06) < 10.0 and base["max_cm"] <= 66.1 and base["min_cm"] >= 46.0, "칼날 밑 높이 = 소켓 높이 + 로컬 z 6.06 (가짜 값 범위)", failures)
        markdown = build_markdown(result, DEFAULT_THRESHOLDS_CM)
        check("| blade_tip |" in markdown and "휘두르기 2.5초" in markdown, "마크다운 요약 표 생성", failures)
    print("자체 점검", "통과" if not failures else "실패 " + str(len(failures)) + "건")
    return 0 if not failures else EXIT_FAILED


def parse_arguments(argument_list):
    parser = argparse.ArgumentParser(description="PIE 소켓·뼈 바닥 위 높이 기록")
    parser.add_argument("--level", default=DEFAULT_LEVEL_PATH)
    parser.add_argument("--fresh-pie", action="store_true", help="남은 PIE 를 멈추고 --level 을 새로 연다")
    parser.add_argument("--stop-pie", action="store_true", help="기록 뒤 PIE 를 멈춘다")
    parser.add_argument("--output-dir", help="출력 폴더(상대 경로는 프로젝트 루트 기준)")
    parser.add_argument("--thresholds", nargs="+", type=float, default=list(DEFAULT_THRESHOLDS_CM), help="높이 문턱(cm) 목록")
    parser.add_argument("--tuning", default=str(CANONICAL_TUNING_PATH), help="소켓·뼈 이름과 칼날 좌표를 읽을 조절 표 JSON")
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args(argument_list)


def main(argument_list=None):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    arguments = parse_arguments(argument_list)
    if arguments.self_test:
        return run_self_test()
    try:
        return record(arguments)
    except (InputProblem, OSError, ValueError, KeyError) as error:
        print("환경 문제:", error)
        return EXIT_ENVIRONMENT


if __name__ == "__main__":
    sys.exit(main())
