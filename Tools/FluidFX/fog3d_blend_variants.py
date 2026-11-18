"""실행 환경: 시스템 Python(표준 라이브러리만 --make·--list·--self-test, 촬영 --run 과 --restore 는 언리얼 에디터가 TDGame 을 열고 MCP 포트 8000·원격 실행이 켜진 상태, PowerShell 에서 실행: /Game 경로가 든 촬영 도구를 부른다). 3D 안개 섞기 지금 식 대 새 식(B4) 눈 판정용 18실행(장면 3 x 품질 2 x 판 3)의 변형 JSON·조절 표 사본을 만들고 촬영·유효 확인한다.
실행 명령: python -B Tools/FluidFX/fog3d_blend_variants.py --make | --list | --run [이름...] [--only-scene s1walkstop] | --restore | --self-test
출력: Saved/FluidFX/fog3d_blend/<장면>_<품질>_<판>/{variants.json, tuning.json, run.json, capture/, log_slice.txt, validity.json}, Saved/FluidFX/fog3d_blend/progress.json. 정본 JSON 과 에셋은 건드리지 않는다.
상태: 실행 전 점검만 확인(--self-test). 실제 촬영 결과는 progress.json 과 실행별 validity.json 으로만 확인한다.
"""
import argparse
import json
import math
import re
import sys
import time
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
for directory in (ROOT / "Tools", ROOT / "Tools" / "FluidFX"):
    if str(directory) not in sys.path:
        sys.path.insert(0, str(directory))
import fog3d_calib_report as rep  # noqa: E402
import fog3d_calib_run as base_run  # noqa: E402
import fog3d_calib_variants as base  # noqa: E402
import fog3d_ground_report as ground_report  # noqa: E402
import fog3d_ground_stage as stage  # noqa: E402
import fog3d_ground_variants as gv  # noqa: E402
import fog3d_visible_check as vc  # noqa: E402

BLEND_ROOT = ROOT / "Saved" / "FluidFX" / "fog3d_blend"
QUALITIES = {"qh": 2, "ql": 0}
QUALITY_SHAPE = {2: (256, 60.0), 0: (128, 30.0)}
VERSIONS = {"cur": (False, 1.0), "b4g100": (True, 1.0), "b4g125": (True, 1.25)}
FRAME_STEP = 0.1
SUN_YAW_DEGREES = 100.0
FACING_YAW_DEGREES = 0.0
TIMELINE = {"compute_off": 1.0, "prewalk": 1.2, "relocate": 2.4, "compute_on": 2.9}
SWING_LENGTH_SECONDS = 1.85
STILL_OFFSETS = (0.5, 1.5, 3.0)
STOP_TOLERANCE_SECONDS = 0.35
SETTINGS_LINE = re.compile(r"Fog3D settings: quality=(-?\d+) resolution=(\d+) stepsPerSecond=([\d.]+) clampedTimeWindBlend=([01]) bodyWindGain=([\d.]+) bladeWindGain=([\d.]+) followsGround=([01])")
CAMERA_ACTOR_LABEL = stage.camera_label("F", "side")
FIELD_OF_VIEW_DEGREES = stage.CAMERA_FOV_DEGREES
ASPECT = 16.0 / 9.0
FRAME_MARGIN = 0.85
SCENES = {
    "s1walkstop": {"title": "걷다 멈춘 뒤 서 있기", "start": (-1100.0, 0.0), "camera": ((200.0, -1300.0, 300.0), (200.0, 0.0, 90.0)), "walk_events": [(3.6, 1700.0, 0.0)],
                   "path": [(-300.0, 0.0), (700.0, 0.0)], "stop": 6.6, "first": 6.1, "last": 10.6, "stills": (7.1, 8.1, 9.6)},
    "s2swing": {"title": "칼 휘두른 뒤", "start": (0.0, 0.0), "camera": ((-350.0, -650.0, 220.0), (30.0, 0.0, 90.0)), "walk_events": [], "swing": 3.9,
                "path": [(0.0, 0.0), (200.0, 0.0)], "first": 3.9, "last": 8.4},
    "s3turn": {"title": "걷다가 직각으로 꺾기", "start": (-600.0, -700.0), "camera": ((-1238.0, -1702.0, 667.0), (-50.0, -250.0, 60.0)), "walk_events": [(3.6, 1000.0, 0.0), (5.5, 1200.0, 90.0)],
               "path": [(-600.0, -700.0), (500.0, -700.0), (500.0, 500.0)], "stop": 7.65, "first": 3.6, "last": 9.5, "stills": (8.1, 9.1)},
}


class InputProblem(Exception):
    pass


def run_names():
    return [f"{scene}_{quality}_{version}" for scene in SCENES for quality in QUALITIES for version in VERSIONS]


def split_name(name):
    scene, quality, version = name.rsplit("_", 2)
    if scene not in SCENES or quality not in QUALITIES or version not in VERSIONS:
        raise InputProblem(f"모르는 실행 이름: {name}")
    return scene, quality, version


def frame_times(scene_id):
    scene = SCENES[scene_id]
    count = int(round((scene["last"] - scene["first"]) / FRAME_STEP))
    return [round(scene["first"] + index * FRAME_STEP, 3) for index in range(count + 1)]


def still_times(scene_id):
    scene = SCENES[scene_id]
    if scene_id == "s2swing":
        end = scene["swing"] + SWING_LENGTH_SECONDS
        return [round(end + offset, 3) for offset in STILL_OFFSETS]
    return list(scene["stills"])


def shot_times(scene_id):
    return sorted(set(frame_times(scene_id)) | set(still_times(scene_id)))


def camera_rotation(scene_id):
    position, target = SCENES[scene_id]["camera"]
    pitch, yaw = stage.camera_rotation_degrees(position, target)
    return round(pitch, 4), round(yaw, 4)


def frame_coverage(scene_id, margin_cm=120.0):
    position, target = SCENES[scene_id]["camera"]
    pitch, yaw = (math.radians(value) for value in camera_rotation(scene_id))
    forward = (math.cos(pitch) * math.cos(yaw), math.cos(pitch) * math.sin(yaw), math.sin(pitch))
    right = (-math.sin(yaw), math.cos(yaw), 0.0)
    up = (-math.sin(pitch) * math.cos(yaw), -math.sin(pitch) * math.sin(yaw), math.cos(pitch))
    horizontal = math.tan(math.radians(FIELD_OF_VIEW_DEGREES / 2.0))
    vertical = horizontal / ASPECT
    worst = 0.0
    for x, y in SCENES[scene_id]["path"]:
        for height in (0.0, 190.0):
            for dx in (-margin_cm, margin_cm):
                for dy in (-margin_cm, margin_cm):
                    offset = (x + dx - position[0], y + dy - position[1], height - position[2])
                    depth = sum(a * b for a, b in zip(offset, forward))
                    if depth <= 1.0:
                        return 9.9
                    worst = max(worst, abs(sum(a * b for a, b in zip(offset, right)) / depth / horizontal), abs(sum(a * b for a, b in zip(offset, up)) / depth / vertical))
    return worst


def expected_log_values(quality, version):
    resolution, steps = QUALITY_SHAPE[QUALITIES[quality]]
    clamped, gain = VERSIONS[version]
    return {"quality": QUALITIES[quality], "resolution": resolution, "steps": steps, "clamped": int(clamped), "body_gain": gain, "blade_gain": gain, "follows": 0}


def parse_settings_lines(text):
    lines = []
    for line in text.splitlines():
        match = SETTINGS_LINE.search(line)
        if match:
            lines.append({"raw": line.strip(), "quality": int(match.group(1)), "resolution": int(match.group(2)), "steps": float(match.group(3)), "clamped": int(match.group(4)),
                          "body_gain": float(match.group(5)), "blade_gain": float(match.group(6)), "follows": int(match.group(7))})
    return lines


def settings_line_matches(parsed, expected):
    return all(abs(float(parsed[key]) - float(value)) < 1e-6 for key, value in expected.items())


def build_events(scene_id, quality_command_read):
    scene = SCENES[scene_id]
    start = [round(scene["start"][0], 3), round(scene["start"][1], 3)]
    events = [{"time": TIMELINE["compute_off"], "console": [gv.COMPUTE_OFF_COMMAND]},
              {"time": TIMELINE["prewalk"], "move": {"distance_cm": gv.stage.PREWALK_CM, "yaw": FACING_YAW_DEGREES}},
              {"time": TIMELINE["relocate"], "move": start},
              {"time": TIMELINE["compute_on"], "console": [gv.COMPUTE_ON_COMMAND]}]
    for time_value, distance, yaw in scene["walk_events"]:
        events.append({"time": time_value, "move": {"distance_cm": distance, "yaw": yaw}})
    if "swing" in scene:
        events.append({"time": scene["swing"], "console": [base.SWING_COMMAND]})
    events.append({"time": round(max(shot_times(scene_id)) + 0.4, 3), "console": [quality_command_read]})
    return events


def build_variants(scene_id, quality, tuning_path, solids):
    scene = SCENES[scene_id]
    position, _ = scene["camera"]
    pitch, yaw = camera_rotation(scene_id)
    height = stage.ground_height(solids, scene["start"][0], scene["start"][1]) + stage.PAWN_SPAWN_HEIGHT_CM
    pawn = [round(scene["start"][0], 3), round(scene["start"][1], 3), round(height, 3)]
    quality_set = f"TD.Fog3D.Quality {QUALITIES[quality]}"
    props = base.build_light_props("side", SUN_YAW_DEGREES)
    props[CAMERA_ACTOR_LABEL] = {"root_component": {"relative_rotation": [pitch, yaw, 0.0]}}
    base_apply = {"tuning_reload": {"tuning": tuning_path, "reset": "keep"}, "pawn": pawn, "view_target": CAMERA_ACTOR_LABEL, "location": {CAMERA_ACTOR_LABEL: list(position)},
                  "props": props, "console": [quality_set, gv.QUALITY_READ_COMMAND, gv.COMPUTE_ON_COMMAND]}
    events = build_events(scene_id, gv.QUALITY_READ_COMMAND)
    schedule = {"reference": "now", "prefix": "g", "fixed_fps": gv.FIXED_FRAMES_PER_SECOND, "timeout": 300, "times": shot_times(scene_id), "events": events}
    record = {"pawn_start": pawn, "camera_position": list(position), "camera_target": list(scene["camera"][1]), "camera_pitch_yaw": [pitch, yaw], "sun_yaw": SUN_YAW_DEGREES,
              "quality_command": quality_set, "frame_times": frame_times(scene_id), "still_times": still_times(scene_id), "planned_stop": scene.get("stop"), "events": events}
    return {"base": base_apply, "variants": [{"name": "blend", "apply": {"schedule": schedule}, "settle": gv.SETTLE_SECONDS}]}, record


def build_tuning(canonical, field_types, version):
    clamped, gain = VERSIONS[version]
    spec = {"kind": "walk", "follow": False}
    extra = [f"bUsesClampedTimeWindBlend={'true' if clamped else 'false'}", f"BodyWindGain={gain}", f"BladeWindGain={gain}"]
    tuning, changes = gv.build_tuning(canonical, field_types, spec, extra, False)
    return tuning, changes


def materialize(name, output_root, canonical, field_types, canonical_sha256, solids, write_files):
    scene_id, quality, version = split_name(name)
    run_directory = base.describe_run_directory(output_root, name)
    tuning, changes = build_tuning(canonical, field_types, version)
    variants, record = build_variants(scene_id, quality, f"{run_directory}/tuning.json", solids)
    problems = base.find_variant_key_problems(variants)
    if problems:
        raise InputProblem("변형 키 오류: " + "; ".join(problems))
    run_record = {"name": name, "scene": scene_id, "quality": quality, "version": version, "level": stage.STAGE_MAP_PATH, "expected_log": expected_log_values(quality, version),
                  "changed_fields": base.describe_changes(canonical, {key: value for key, value in changes.items() if key in canonical["Globals"][base.SETTINGS_GROUP_NAME]}),
                  "canonical_tuning_sha256": canonical_sha256, "canonical_tuning_path": base.CANONICAL_TUNING_PATH.relative_to(ROOT).as_posix(), **record}
    if write_files:
        directory = Path(output_root) / name
        base.write_json(directory / "tuning.json", tuning)
        base.write_json(directory / "variants.json", variants)
        base.write_json(directory / "run.json", run_record)
    return {"variants": variants, "tuning": tuning, "record": run_record}


def make_all(output_root, names):
    canonical = base.load_canonical_tuning()
    canonical_sha256 = base.sha256_of_file(base.CANONICAL_TUNING_PATH)
    field_types = gv.load_field_types(canonical, True)
    solids = stage.build_solids()
    for name in names:
        materialize(name, output_root, canonical, field_types, canonical_sha256, solids, True)
    return canonical_sha256


def find_walk_statuses(capture_directory):
    return [json.loads(path.read_text(encoding="utf-8")) for path in sorted(Path(capture_directory).glob("*_walk.json"))]


def window_before_restore(text, name):
    lines = text.splitlines()
    loads = [index for index, line in enumerate(lines) if f"{name}/tuning.json (loaded)" in line]
    if not loads:
        return ""
    start = loads[-1]
    end = next((index for index in range(start + 1, len(lines)) if "LogTDFluidPresetTuning: Preset tuning" in lines[index]), len(lines))
    return "\n".join(lines[start:end])


def judge_run(run_directory, run_record):
    run_directory = Path(run_directory)
    capture_directory = run_directory / "capture"
    reasons, details = [], {}
    try:
        schedule_path = rep.find_schedule_file(capture_directory)
        schedule_status = json.loads(Path(schedule_path).read_text(encoding="utf-8"))
    except (OSError, ValueError, rep.InputProblem) as error:
        return {"valid": False, "reasons": [f"예약 상태 파일 오류: {error}"], "details": details}
    shots = schedule_status.get("shots", [])
    missing = [shot["time"] for shot in shots if not shot.get("saved") or not Path(shot["path"]).is_file()]
    if len(shots) != len(shot_times(run_record["scene"])) or missing:
        reasons.append(f"장 {len(shots)}/{len(shot_times(run_record['scene']))}, 없는 장 {missing[:6]}")
    if not schedule_status.get("done") or schedule_status.get("error"):
        reasons.append(f"예약 미완료 또는 오류: {schedule_status.get('error')}")
    events_done = schedule_status.get("events_done", [])
    if len(events_done) != len(run_record["events"]):
        reasons.append(f"이벤트 {len(events_done)}/{len(run_record['events'])}개만 실행됨")
    event_errors = [event for event in events_done if isinstance(event.get("move"), dict) and event["move"].get("error")]
    if event_errors:
        reasons.append("이동 이벤트 오류: " + str(event_errors[0]["move"]["error"])[:120])
    capture_gaps = [float(shot["captured_game_time"]) - float(shot["time"]) for shot in shots if shot.get("captured_game_time") is not None]
    details["shot_capture_delay_max_seconds"] = max(capture_gaps) if capture_gaps else None
    details["late_shots_over_0.05"] = sum(1 for gap in capture_gaps if gap > 0.05)
    if capture_gaps and details["late_shots_over_0.05"] > 0.1 * len(capture_gaps):
        reasons.append(f"찍힌 게임 시각이 0.05초 넘게 늦은 장 {details['late_shots_over_0.05']}/{len(capture_gaps)}")
    details["event_late_max_seconds"] = max((float(event["game_time"]) - float(event["time"]) for event in events_done), default=None)
    walk_events = SCENES[run_record["scene"]]["walk_events"]
    walks = find_walk_statuses(capture_directory)
    details["walks"] = [{"start": walk.get("start"), "yaw": walk.get("yaw"), "distance_cm": walk.get("distance_cm"), "moved_cm": walk.get("moved_cm"), "elapsed_game_seconds": walk.get("elapsed_game_seconds"),
                         "done": walk.get("done"), "timed_out": walk.get("timed_out"), "error": walk.get("error")} for walk in walks]
    if walk_events:
        last_walk = [walk for walk in walks if walk.get("distance_cm") == walk_events[-1][1] and walk.get("yaw") == walk_events[-1][2]]
        if not last_walk:
            reasons.append("마지막 걷기 기록 없음")
        else:
            walk = last_walk[-1]
            stop_time = float(walk["start_game_time"]) + float(walk["elapsed_game_seconds"]) - float(schedule_status["reference_game_time"])
            details["actual_stop_seconds"] = round(stop_time, 3)
            if not walk.get("done") or walk.get("timed_out") or walk.get("error"):
                reasons.append(f"걷기 미완료: done={walk.get('done')} timed_out={walk.get('timed_out')} error={walk.get('error')}")
            elif float(walk.get("moved_cm") or 0.0) < 0.9 * walk_events[-1][1]:
                reasons.append(f"걸은 거리 {walk.get('moved_cm'):.0f}cm 가 요청 {walk_events[-1][1]:.0f}cm 의 90% 미만")
            if abs(stop_time - run_record["planned_stop"]) > STOP_TOLERANCE_SECONDS:
                reasons.append(f"멈춘 시각 {stop_time:.2f}초가 계획 {run_record['planned_stop']}초에서 {STOP_TOLERANCE_SECONDS}초 넘게 벗어남")
    summary = ground_report.read_log_summary(run_directory)
    details["log_usable"] = summary.get("usable")
    details["log_submitted"] = summary.get("submitted")
    if summary.get("usable") is not True:
        reasons.append(f"로그 점검 usable={summary.get('usable')} ensure={summary.get('ensure')} rdg={summary.get('rdg_error_lines')} rejected={summary.get('rejected')}")
    snapshot = run_directory / rep.LOG_SNAPSHOT_FILE_NAME
    settings_lines = parse_settings_lines(window_before_restore(snapshot.read_text(encoding="utf-8", errors="replace"), run_record["name"])) if snapshot.is_file() else []
    details["settings_lines"] = [entry["raw"] for entry in settings_lines]
    if not settings_lines:
        reasons.append("Fog3D settings 로그 줄이 없다")
        details["settings_line_missing"] = True
    elif not settings_line_matches(settings_lines[-1], run_record["expected_log"]):
        reasons.append(f"마지막 Fog3D settings 줄이 뜻과 다름: {settings_lines[-1]['raw']} (기대 {run_record['expected_log']})")
    quality_values = ground_report.read_quality_values(run_directory)
    details["quality_cvar_log_values"] = quality_values
    if quality_values and set(quality_values) - {run_record["expected_log"]["quality"]}:
        reasons.append(f"품질 변수 값 줄에 다른 값: {sorted(set(quality_values))}")
    return {"valid": not reasons, "reasons": reasons, "details": details}


def write_progress(record):
    BLEND_ROOT.mkdir(parents=True, exist_ok=True)
    (BLEND_ROOT / "progress.json").write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")


def run_all(names, no_retry=False, maximum_attempts=3):
    base_run.CALIBRATION_ROOT = BLEND_ROOT
    base_run.LEVEL_PATH = stage.STAGE_MAP_PATH
    pie_check_module, lookdev_module, original_wait = base_run.load_capture_modules(base_run.DEFAULT_NAVIGATION_WAIT_SECONDS)
    progress_path = BLEND_ROOT / "progress.json"
    record = json.loads(progress_path.read_text(encoding="utf-8")) if progress_path.is_file() else {"runs": {}}
    record.setdefault("runs", {})
    record["started_at"] = datetime.now().isoformat(timespec="seconds")
    record["finished_at"] = None
    for name in names:
        run_record = json.loads((BLEND_ROOT / name / "run.json").read_text(encoding="utf-8"))
        entry = {"name": name, "attempts": [], "final_valid": False}
        record["runs"][name] = entry
        for attempt in range(1, maximum_attempts + 1):
            started = time.monotonic()
            attempt_record = {"attempt": attempt, "started_at": datetime.now().isoformat(timespec="seconds")}
            entry["attempts"].append(attempt_record)
            write_progress(record)
            exit_code, navigation_lines, stdout_path = base_run.run_capture(lookdev_module, name, attempt)
            attempt_record.update({"capture_exit_code": exit_code, "navigation_state_lines": navigation_lines})
            snapshot_count, has_summary = rep.snapshot_run_log(BLEND_ROOT / name, vc.DEFAULT_LOG_PATH)
            attempt_record.update({"log_lines": snapshot_count, "log_has_summary": has_summary})
            judged = judge_run(BLEND_ROOT / name, run_record)
            attempt_record["validity"] = judged
            attempt_record["wall_seconds"] = round(time.monotonic() - started, 1)
            (BLEND_ROOT / name / "validity.json").write_text(json.dumps(attempt_record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
            write_progress(record)
            print(f"== {name} 시도 {attempt}: {'유효' if judged['valid'] and exit_code == 0 else '무효'} {attempt_record['wall_seconds']}초 " + "; ".join(judged["reasons"])[:200], flush=True)
            if exit_code == 0 and judged["valid"]:
                entry["final_valid"] = True
                break
            if no_retry:
                break
    record["finished_at"] = datetime.now().isoformat(timespec="seconds")
    write_progress(record)
    (BLEND_ROOT / "done.flag").write_text("done " + record["finished_at"] + "\n", encoding="utf-8", newline="\n")
    return 0 if all(record["runs"][name]["final_valid"] for name in names) else 1


def restore_console_variables():
    import pie_check_fluid
    marker = "TD_BLEND_RESTORE"
    code = ("import unreal, json\nfor command in ('TD.Fog3D.Quality -1', 'TD.Fog3D.Mode -1'):\n    unreal.SystemLibrary.execute_console_command(None, command)\n"
            "print('" + marker + " ' + json.dumps({'quality': unreal.SystemLibrary.get_console_variable_int_value('TD.Fog3D.Quality'), 'mode': unreal.SystemLibrary.get_console_variable_int_value('TD.Fog3D.Mode')}))")
    result = pie_check_fluid.parse_marker(pie_check_fluid.run_editor_code(code), marker)
    print("되돌림 결과", result)
    return 0 if result.get("quality") == -1 and result.get("mode") == -1 else 1


def run_self_test():
    failures = []

    def check(condition, label, detail=""):
        if not condition:
            failures.append(label)
        print(("통과 " if condition else "실패 ") + label + (": " + detail if detail else ""))

    names = run_names()
    check(len(names) == 18 and len(set(names)) == 18, "실행 18개", str(len(names)))
    check(all(split_name(name) for name in names), "이름 분해")
    for scene_id in SCENES:
        coverage = frame_coverage(scene_id)
        check(coverage <= FRAME_MARGIN, f"{scene_id} 카메라가 걷는 길을 안쪽 {FRAME_MARGIN:.0%} 안에 담음", f"{coverage:.2f}")
        check(all(abs(value - round(value, 3)) < 1e-9 for value in shot_times(scene_id)) and len(shot_times(scene_id)) == len(set(shot_times(scene_id))), f"{scene_id} 장 시각")
    check(len(frame_times("s1walkstop")) == 46 and len(frame_times("s2swing")) == 46 and len(frame_times("s3turn")) == 60, "움직이는 그림 장 수 46·46·60")
    check(still_times("s2swing") == [6.25, 7.25, 8.75] and still_times("s1walkstop") == [7.1, 8.1, 9.6], "정지 그림 시각")
    canonical = base.load_canonical_tuning()
    field_types = gv.load_field_types(canonical, True)
    sha = base.sha256_of_file(base.CANONICAL_TUNING_PATH)
    solids = stage.build_solids()
    for name in names:
        outcome = materialize(name, BLEND_ROOT, canonical, field_types, sha, solids, False)
        scene_id, quality, version = split_name(name)
        settings = outcome["tuning"]["Globals"][base.SETTINGS_GROUP_NAME]
        clamped, gain = VERSIONS[version]
        okay = settings["bUsesClampedTimeWindBlend"] is clamped and settings["BodyWindGain"] == gain and settings["BladeWindGain"] == gain and settings["bFollowsGround"] is False
        okay = okay and settings["BodyWindStrength"] == canonical["Globals"][base.SETTINGS_GROUP_NAME]["BodyWindStrength"] and settings["BladeWindStrength"] == canonical["Globals"][base.SETTINGS_GROUP_NAME]["BladeWindStrength"]
        if not okay:
            failures.append(name)
    check(not failures, "조절 표 사본의 세 키·세기·땅 따라가기")
    check(base.sha256_of_file(base.CANONICAL_TUNING_PATH) == sha, "정본 JSON 해시 불변")
    line = "LogTDFog3DZone: Fog3D settings: quality=2 resolution=256 stepsPerSecond=60 clampedTimeWindBlend=1 bodyWindGain=1.25 bladeWindGain=1.25 followsGround=0"
    parsed = parse_settings_lines(line)
    check(len(parsed) == 1 and settings_line_matches(parsed[0], expected_log_values("qh", "b4g125")) and not settings_line_matches(parsed[0], expected_log_values("ql", "b4g125")) and not settings_line_matches(parsed[0], expected_log_values("qh", "cur")), "로그 줄 해석과 대조")
    print("자체 점검", "통과" if not failures else "실패 " + str(len(failures)) + "건")
    return 0 if not failures else 1


def main(argument_list=None):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description="3D 안개 섞기 비교 촬영 변형·실행")
    parser.add_argument("names", nargs="*")
    parser.add_argument("--make", action="store_true")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--restore", action="store_true")
    parser.add_argument("--only-scene")
    parser.add_argument("--no-retry", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args(argument_list)
    try:
        if arguments.self_test:
            return run_self_test()
        names = arguments.names or run_names()
        if arguments.only_scene:
            names = [name for name in names if name.startswith(arguments.only_scene + "_")]
        if arguments.list:
            print("\n".join(names))
            return 0
        if arguments.make:
            print("정본 sha256", make_all(BLEND_ROOT, names), f"실행 {len(names)}개")
            return 0
        if arguments.run:
            return run_all(names, arguments.no_retry)
        if arguments.restore:
            return restore_console_variables()
        parser.print_help()
        return 2
    except (InputProblem, base.InputProblem, OSError, ValueError, KeyError) as error:
        print("입력 문제:", error)
        return 2


if __name__ == "__main__":
    sys.exit(main())
