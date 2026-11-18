"""실행 환경: 시스템 Python(표준 라이브러리만, 언리얼 에디터 불필요). 3D 안개 2a-3 화면 확인(새 무대 LV_TDFog3DGroundStage)의 실행 하나분 촬영 변형 JSON·조절 표 사본·실행 기록을 만든다. 정본 JSON·코드·에셋은 건드리지 않고 값은 Saved 아래 사본에만 적는다(bFollowsGround 와 GroundFollowVolumeMaterial 은 사본에 직접 쓴다).
실행 명령: python -B Tools/FluidFX/fog3d_ground_variants.py --batch main [static follow_off walk walk_side walk_off walk_p2 run run_off order static_p2 all] [--only 실행이름...] [--set 필드=값 ...] [--keep-stain] [--dry-run], 목록: --list, 점검만: --self-test (PowerShell, 한글 출력은 PYTHONUTF8=1)
출력: Saved/FluidFX/fog3d_ground/<실행 이름>/{variants.json, tuning.json, run.json}, Saved/FluidFX/fog3d_ground/index.json(실행·묶음 색인). 종료 코드 0 정상 / 1 점검 실패 / 2 입력 문제.
상태: 에디터 실행 전: 미검증(--self-test 와 촬영 도구 키 검사 find_key_errors 만. 시각표는 1c·1d 걷기 시각표를 걷기만 남기고 앞당긴 것이고, 땅 따라가기 켠 상태의 기준 장(안개 계산 끔)이 땅 모양을 유지하는지는 첫 촬영에서 처음 본다).
"""
import argparse
import copy
import json
import math
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
for directory in (ROOT / "Tools", ROOT / "Tools" / "FluidFX"):
    if str(directory) not in sys.path:
        sys.path.insert(0, str(directory))
import fog3d_calib_variants as base  # noqa: E402
import fog3d_ground_stage as stage  # noqa: E402

GROUND_ROOT = ROOT / "Saved" / "FluidFX" / "fog3d_ground"
INDEX_FILE_NAME = "index.json"
LEVEL_PATH = stage.STAGE_MAP_PATH
GROUND_MATERIAL_FIELD = "GroundFollowVolumeMaterial"
GROUND_MATERIAL_SOFT_PATH = f"{stage.GROUND_MATERIAL_PATH}.{stage.GROUND_MATERIAL_PATH.rsplit('/', 1)[-1]}"
FOLLOW_FIELD = "bFollowsGround"
STAIN_FIELD = "bStainNoiseEnabled"
COMPUTE_OFF_COMMAND = base.COMPUTE_OFF_COMMAND
COMPUTE_ON_COMMAND = base.COMPUTE_ON_COMMAND_FORMAT.format(mode=base.DEFAULT_ON_MODE)
VERBOSE_ON_COMMAND = "log LogTDFog3DZone Verbose"
VERBOSE_OFF_COMMAND = "log LogTDFog3DZone Log"
FIXED_FRAMES_PER_SECOND = base.FIXED_FRAMES_PER_SECOND
FOLLOW_CAMERAS = {"back": (400, -12.0, 0.0), "side": (500, -20.0, 90.0)}
SIDE_CAMERA_PITCH_DEGREES = -20.0
BACK_CAMERA_BASE_PITCH_DEGREES = -12.0
VIEW_PITCH_LIMITS = (-80.0, 60.0)
SUN_YAW_OFFSET_DEGREES = 100.0
WALK_SPEED_CM_PER_SECOND = 600.0
WALK_TIMELINE = {"compute_off": 1.0, "prewalk": 1.2, "teleport": 2.4, "compute_on": 2.9, "walk": 3.6, "compute_off_end": 12.4, "log_restore": 14.6}
WALK_SHOT_TABLE = (
    ("G4.0", 4.0, "moving"), ("G4.4", 4.4, "moving"), ("G4.8", 4.8, "moving"),
    ("G6.1", 6.1, "stopped"), ("G7.1", 7.1, "stopped"), ("G8.1", 8.1, "stopped"), ("G9.6", 9.6, "stopped"), ("G11.6", 11.6, "stopped"),
    ("G13.4", 13.4, "reference"), ("G13.9", 13.9, "reference"), ("G15.0", 15.0, "hidden"),
)
WALK_STOPPED_IDS = ("G6.1", "G7.1", "G8.1", "G9.6", "G11.6")
WALK_REFERENCE_IDS = ("G13.4", "G13.9")
WALK_HIDDEN_ID = "G15.0"
WALK_HIDE_TIME = 14.4
WALK_QUALITY_END_TIME = 14.8
STATIC_QUALITY_END_TIME = 6.3
RUN_QUALITY_END_TIME = 9.6
QUALITY_FIXED_LEVEL = -1
QUALITY_SET_COMMAND = f"TD.Fog3D.Quality {QUALITY_FIXED_LEVEL}"
QUALITY_READ_COMMAND = "TD.Fog3D.Quality"
WALK_REPRESENTATIVE_ID = "G6.1"
WALK_END_MARGIN_SECONDS = 0.6
WALK_STOP_SETTLE_SECONDS = 0.4
WALK_TIMEOUT_SECONDS = 180
STATIC_SHOT_TABLE = (("A1", 4.0, "shown"), ("A2", 4.5, "shown"), ("H1", 6.0, "hidden"), ("H2", 6.5, "hidden"))
STATIC_HIDE_TIME = 5.0
STATIC_MAIN_CAMERA_BY_STATION = {"HO": "high"}
STATIC_TIMEOUT_SECONDS = 120
RUN_START_TIME = 3.0
RUN_FIRST_SHOT_TIME = 2.8
RUN_SHOT_STEP = 0.1
RUN_SHOT_COUNT = 71
RUN_TIMEOUT_SECONDS = 300
ESTIMATED_SECONDS = {"static": 45, "walk": 60, "run": 120}
BATCH_ORDER = ("static", "follow_off", "walk", "walk_side", "run", "run_off", "walk_off", "walk_p2", "static_p2", "order", "trial_off")
MAIN_BATCHES = ("static", "follow_off", "walk", "walk_side", "run")
SETTLE_SECONDS = 0.5


class InputProblem(Exception):
    pass


def infer_field_types(canonical):
    settings = canonical["Globals"][base.SETTINGS_GROUP_NAME]
    types = {}
    for field, value in settings.items():
        if isinstance(value, bool):
            types[field] = "bool"
        elif isinstance(value, int):
            types[field] = "int32"
        elif isinstance(value, float):
            types[field] = "float"
    return types


def load_field_types(canonical, use_header):
    types = infer_field_types(canonical)
    if use_header and base.SETTINGS_HEADER_PATH.is_file():
        types.update(base.parse_settings_header(base.SETTINGS_HEADER_PATH.read_text(encoding="utf-8")))
    return types


def make_spec(kind, station, line_id, camera, follow, batches, extra_console=(), description="", suffix=""):
    marker = {"static": "s", "walk": "w", "run": "r"}[kind]
    key = station if kind == "static" else line_id
    return {"name": f"{marker}_{key}_{camera}_{'on' if follow else 'off'}{suffix}", "kind": kind, "station": station, "line": line_id, "camera": camera, "follow": follow,
            "batches": list(batches), "console_extra": list(extra_console), "description": description}


def all_specifications():
    specs = []
    for station in ("F", "S30", "CL", "ST", "CB", "HO"):
        camera = STATIC_MAIN_CAMERA_BY_STATION.get(station, "side")
        specs.append(make_spec("static", station, None, camera, True, ["static"], description=f"{stage.STATION_TITLES[station]} 정지 촬영, 고정 카메라 {camera}, 땅 따라가기 켬"))
    specs.append(make_spec("static", "HO", None, "side", True, ["static"], description=f"{stage.STATION_TITLES['HO']} 정지 촬영, 고정 카메라 side, 땅 따라가기 켬"))
    for station in ("S30", "CL", "F"):
        specs.append(make_spec("static", station, None, "high", True, ["static"], description=f"{stage.STATION_TITLES[station]} 정지 촬영, 고정 카메라 high, 땅 따라가기 켬"))
    for station in ("S30", "CL"):
        for camera in ("side", "high"):
            specs.append(make_spec("static", station, None, camera, False, ["follow_off", "trial_off"], description=f"{stage.STATION_TITLES[station]} 정지 촬영, 고정 카메라 {camera}, 땅 따라가기 끔"))
    for station, camera in (("F", "side"), ("F", "high"), ("ST", "side"), ("CB", "side")):
        specs.append(make_spec("static", station, None, camera, False, ["trial_off"], description=f"{stage.STATION_TITLES[station]} 정지 촬영, 고정 카메라 {camera}, 땅 따라가기 끔(시운전)"))
    for station in ("S20", "S40", "SC"):
        specs.append(make_spec("static", station, None, "side", True, ["static_p2"], description=f"{stage.STATION_TITLES[station]} 정지 촬영, 고정 카메라 side"))
    for line in stage.WALK_LINES:
        if line["id"].startswith("R"):
            continue
        batch = "walk" if line["priority"] == 1 else "walk_p2"
        specs.append(make_spec("walk", line["station"], line["id"], "back", True, [batch], description=line["description"]))
    for line_id in ("W02", "W03", "W08", "W12", "W15"):
        line = stage.find_line(line_id)
        specs.append(make_spec("walk", line["station"], line_id, "side", True, ["walk_side"], description=line["description"] + " (옆 카메라)"))
    for line_id in ("W01", "W02", "W03", "W04", "W08", "W09"):
        line = stage.find_line(line_id)
        specs.append(make_spec("walk", line["station"], line_id, "back", False, ["walk_off", "trial_off"], description=line["description"] + " (땅 따라가기 끔)"))
    specs.append(make_spec("run", "F", "R01", "side", True, ["run"], description=stage.find_line("R01")["description"]))
    specs.append(make_spec("run", "S30", "R02", "side", True, ["run"], description=stage.find_line("R02")["description"]))
    specs.append(make_spec("run", "S30", "R02", "back", True, ["run"], description=stage.find_line("R02")["description"] + " (뒤 카메라)"))
    specs.append(make_spec("run", "S30", "R02", "side", False, ["run_off", "trial_off"], description=stage.find_line("R02")["description"] + " (땅 따라가기 끔)"))
    specs.append(make_spec("run", "F", "R01", "back", True, ["order"], [VERBOSE_ON_COMMAND], "캡처 순서 확인: 평지 달리기 + 구역 Verbose 로그", "_log"))
    specs.append(make_spec("walk", "S30", "W02", "back", True, ["order"], [VERBOSE_ON_COMMAND], "캡처 순서 확인: 30도 오르막 + 구역 Verbose 로그", "_log"))
    return specs


def select_specifications(specs, batch_names, only_names):
    wanted = set()
    for name in batch_names:
        if name == "all":
            wanted.update(BATCH_ORDER)
        elif name == "main":
            wanted.update(MAIN_BATCHES)
        elif name in BATCH_ORDER:
            wanted.add(name)
        else:
            raise InputProblem(f"모르는 묶음: {name} (가능: main all {' '.join(BATCH_ORDER)})")
    selected = [spec for spec in specs if wanted.intersection(spec["batches"])]
    if only_names:
        known = {spec["name"] for spec in specs}
        unknown = [name for name in only_names if name not in known]
        if unknown:
            raise InputProblem("모르는 실행 이름: " + ", ".join(unknown))
        selected = [spec for spec in specs if spec["name"] in only_names]
    return selected


def build_tuning(canonical, field_types, spec, extra_assignments, keep_stain):
    assignments = [f"{FOLLOW_FIELD}={'true' if spec['follow'] else 'false'}"]
    if spec["kind"] != "static" and not keep_stain:
        assignments.append(f"{STAIN_FIELD}=false")
    changes = base.parse_assignments(assignments, field_types)
    changes.update(base.parse_assignments(extra_assignments, field_types))
    tuning = base.build_tuning_copy(canonical, changes, field_types)
    settings = tuning["Globals"][base.SETTINGS_GROUP_NAME]
    if spec["follow"]:
        if GROUND_MATERIAL_FIELD not in settings:
            raise InputProblem(f"정본 JSON 의 {base.SETTINGS_GROUP_NAME} 에 {GROUND_MATERIAL_FIELD} 가 없다")
        settings[GROUND_MATERIAL_FIELD] = GROUND_MATERIAL_SOFT_PATH
        changes[GROUND_MATERIAL_FIELD] = GROUND_MATERIAL_SOFT_PATH
    predicted = base.predict_value_problems(settings)
    if predicted:
        raise InputProblem("값 검사에 걸릴 것으로 예측: " + "; ".join(predicted))
    return tuning, changes


def view_for_follow_camera(camera, yaw, alpha_degrees):
    arm, pitch, yaw_offset = FOLLOW_CAMERAS[camera]
    if camera == "back":
        pitch = min(max(BACK_CAMERA_BASE_PITCH_DEGREES + alpha_degrees, VIEW_PITCH_LIMITS[0]), VIEW_PITCH_LIMITS[1])
    return arm, [round(pitch, 3), round(yaw + yaw_offset, 3)]


def walk_end_estimate_seconds(distance_cm):
    return distance_cm / WALK_SPEED_CM_PER_SECOND + WALK_END_MARGIN_SECONDS


def build_walk_events(line, start_world):
    yaw = line["yaw"]
    timeline = WALK_TIMELINE
    return [
        {"kind": "compute_off", "time": timeline["compute_off"], "console": [COMPUTE_OFF_COMMAND]},
        {"kind": "prewalk", "time": timeline["prewalk"], "move": {"distance_cm": line["prewalk_cm"], "yaw": yaw}},
        {"kind": "relocate", "time": timeline["teleport"], "move": [round(start_world[0], 3), round(start_world[1], 3)]},
        {"kind": "compute_on", "time": timeline["compute_on"], "console": [COMPUTE_ON_COMMAND]},
        {"kind": "walk", "time": timeline["walk"], "move": {"distance_cm": line["distance_cm"], "yaw": yaw}},
        {"kind": "compute_off_end", "time": timeline["compute_off_end"], "console": [COMPUTE_OFF_COMMAND]},
        {"kind": "hide_zone", "time": WALK_HIDE_TIME, "call": {stage.actor_label(stage.ZONE_LABEL): {"function": "set_actor_hidden_in_game", "args": [True]}}},
        {"kind": "quality_end", "time": WALK_QUALITY_END_TIME, "console": [QUALITY_READ_COMMAND]},
    ]


def build_static_events():
    return [{"kind": "hide_zone", "time": STATIC_HIDE_TIME, "call": {stage.actor_label(stage.ZONE_LABEL): {"function": "set_actor_hidden_in_game", "args": [True]}}},
            {"kind": "quality_end", "time": STATIC_QUALITY_END_TIME, "console": [QUALITY_READ_COMMAND]}]


def build_run_events(line):
    return [{"kind": "run", "time": RUN_START_TIME, "move": {"distance_cm": line["distance_cm"], "yaw": line["yaw"]}},
            {"kind": "quality_end", "time": RUN_QUALITY_END_TIME, "console": [QUALITY_READ_COMMAND]}]


def build_shot_table(kind):
    if kind == "static":
        return [{"id": shot_id, "group": "A" if role == "shown" else "H", "time": time_value, "role": role} for shot_id, time_value, role in STATIC_SHOT_TABLE]
    if kind == "run":
        times = [round(RUN_FIRST_SHOT_TIME + index * RUN_SHOT_STEP, 3) for index in range(RUN_SHOT_COUNT)]
        return [{"id": f"D{time_value:.1f}", "group": "D", "time": time_value, "role": "dense"} for time_value in times]
    return [{"id": shot_id, "group": "G", "time": time_value, "role": role} for shot_id, time_value, role in WALK_SHOT_TABLE]


def strip_kind(events):
    return [{key: value for key, value in event.items() if key != "kind"} for event in events]


def build_schedule(kind, events, extra_restore):
    shots = build_shot_table(kind)
    timeout = {"static": STATIC_TIMEOUT_SECONDS, "walk": WALK_TIMEOUT_SECONDS, "run": RUN_TIMEOUT_SECONDS}[kind]
    records = list(events)
    if extra_restore:
        records.append({"kind": "log_restore", "time": round(max(entry["time"] for entry in shots) + 0.6, 3), "console": list(extra_restore)})
    return {"reference": "now", "prefix": "g", "fixed_fps": FIXED_FRAMES_PER_SECOND, "timeout": timeout, "times": [entry["time"] for entry in shots], "events": strip_kind(records)}, records, shots


def resolve_spec_geometry(spec, solids):
    if spec["kind"] == "static":
        pawn = stage.static_pawn_world(spec["station"])
        height = stage.ground_height(solids, pawn[0], pawn[1])
        return {"pawn_xy": pawn, "pawn_z": height + stage.PAWN_SPAWN_HEIGHT_CM}
    line = stage.find_line(spec["line"])
    start = stage.line_start_world(line)
    return {"pawn_xy": start, "pawn_z": stage.line_start_height(solids, line) + stage.PAWN_SPAWN_HEIGHT_CM, "alpha": stage.line_end_slope_degrees(solids, line), "line": line}


def build_variants(spec, tuning_path, solids):
    geometry = resolve_spec_geometry(spec, solids)
    pawn = [round(geometry["pawn_xy"][0], 3), round(geometry["pawn_xy"][1], 3), round(geometry["pawn_z"], 3)]
    base_apply = {"tuning_reload": {"tuning": tuning_path, "reset": "keep"}, "pawn": pawn, "console": [QUALITY_SET_COMMAND, QUALITY_READ_COMMAND, COMPUTE_ON_COMMAND] + list(spec["console_extra"])}
    extra_restore = [VERBOSE_OFF_COMMAND] if VERBOSE_ON_COMMAND in spec["console_extra"] else []
    record = {"pawn_start": pawn}
    if spec["kind"] == "static":
        base_apply["view_target"] = stage.camera_label(spec["station"], spec["camera"])
        events = build_static_events()
        record["view_target"] = base_apply["view_target"]
    else:
        line = geometry["line"]
        arm, view = view_for_follow_camera(spec["camera"], line["yaw"], geometry["alpha"])
        sun_yaw = line["yaw"] + SUN_YAW_OFFSET_DEGREES
        base_apply["arm"], base_apply["view"] = arm, view
        light_props = base.build_light_props("side", sun_yaw)
        base_apply["props"] = light_props
        events = build_run_events(line) if spec["kind"] == "run" else build_walk_events(line, stage.line_start_world(line))
        record.update({"arm": arm, "view": view, "sun_yaw": sun_yaw, "view_alpha_degrees": round(geometry["alpha"], 3), "walk_yaw": line["yaw"],
                       "walk_start_world": list(stage.line_start_world(line)), "walk_end_world": list(stage.line_end_world(line)), "walk_distance_cm": line["distance_cm"]})
    schedule, event_records, shots = build_schedule(spec["kind"], events, extra_restore)
    variants = {"base": base_apply, "variants": [{"name": "ground", "apply": {"schedule": schedule}, "settle": SETTLE_SECONDS}]}
    record.update({"events": event_records, "shots": shots})
    return variants, record


def build_valid_conditions(spec, record):
    if spec["kind"] == "static":
        return {"shown_shots": ["A1", "A2"], "hidden_shots": ["H1", "H2"], "hide_event_seconds": STATIC_HIDE_TIME}
    if spec["kind"] == "run":
        return {"dense_step_seconds": RUN_SHOT_STEP, "run_start_seconds": RUN_START_TIME, "run_end_latest_seconds": round(RUN_START_TIME + record["walk_distance_cm"] / WALK_SPEED_CM_PER_SECOND + 2.0, 3),
                "late_limit_seconds": base.DENSE_LATE_LIMIT_SECONDS}
    first_stopped = min(entry["time"] for entry in record["shots"] if entry["role"] == "stopped")
    return {"walk_start_seconds": WALK_TIMELINE["walk"], "walk_end_before_seconds": round(first_stopped - WALK_STOP_SETTLE_SECONDS, 3),
            "walk_end_estimate_seconds": round(WALK_TIMELINE["walk"] + walk_end_estimate_seconds(record["walk_distance_cm"]), 3),
            "stopped_ids": list(WALK_STOPPED_IDS), "reference_ids": list(WALK_REFERENCE_IDS), "hidden_id": WALK_HIDDEN_ID, "representative_id": WALK_REPRESENTATIVE_ID}


def materialize(spec, output_root, canonical, field_types, canonical_sha256, solids, extra_assignments, keep_stain, write_files):
    if not re.fullmatch(r"[A-Za-z0-9_]{1,40}", spec["name"]):
        raise InputProblem(f"실행 이름은 영문·숫자·밑줄 40자 이하: {spec['name']}")
    run_directory = base.describe_run_directory(output_root, spec["name"])
    tuning, changes = build_tuning(canonical, field_types, spec, extra_assignments, keep_stain)
    variants, record = build_variants(spec, f"{run_directory}/tuning.json", solids)
    key_problems = base.find_variant_key_problems(variants)
    if key_problems:
        raise InputProblem("변형 키 오류: " + "; ".join(key_problems))
    run_record = {
        "name": spec["name"], "kind": spec["kind"], "station": spec["station"], "line": spec["line"], "camera": spec["camera"], "follow": spec["follow"], "batches": spec["batches"],
        "description": spec["description"], "level": LEVEL_PATH, "changed_fields": base.describe_changes(canonical, {key: value for key, value in changes.items() if key in canonical["Globals"][base.SETTINGS_GROUP_NAME]}),
        "ground_material_soft_path": GROUND_MATERIAL_SOFT_PATH if spec["follow"] else "", "console_extra": spec["console_extra"],
        "quality_cvar": {"requested": QUALITY_FIXED_LEVEL, "commands": [QUALITY_SET_COMMAND, QUALITY_READ_COMMAND], "log_values": None},
        "canonical_tuning_sha256": canonical_sha256, "canonical_tuning_path": base.CANONICAL_TUNING_PATH.relative_to(ROOT).as_posix(),
        "valid_conditions": build_valid_conditions(spec, record), "commands": {"run": f"python -B Tools/FluidFX/fog3d_ground_run.py {spec['name']}", "report": f"python -B Tools/FluidFX/fog3d_ground_report.py --runs {spec['name']}"},
        **record,
    }
    if write_files:
        directory = Path(output_root) / spec["name"]
        base.write_json(directory / "tuning.json", tuning)
        base.write_json(directory / "variants.json", variants)
        base.write_json(directory / "run.json", run_record)
    return {"variants": variants, "tuning": tuning, "record": run_record}


def update_index(output_root, written_specs):
    index_path = Path(output_root) / INDEX_FILE_NAME
    index = json.loads(index_path.read_text(encoding="utf-8")) if index_path.is_file() else {"level": LEVEL_PATH, "runs": []}
    by_name = {entry["name"]: entry for entry in index["runs"]}
    for spec in written_specs:
        by_name[spec["name"]] = {"name": spec["name"], "kind": spec["kind"], "station": spec["station"], "line": spec["line"], "camera": spec["camera"], "follow": spec["follow"], "batches": spec["batches"], "status": "written"}
    index["runs"] = list(by_name.values())
    index["batches"] = {batch: [entry["name"] for entry in index["runs"] if batch in entry["batches"]] for batch in BATCH_ORDER}
    base.write_json(index_path, index)
    return index_path


def estimate_minutes(specs):
    return sum(ESTIMATED_SECONDS[spec["kind"]] for spec in specs) / 60.0


def describe_batches(specs):
    lines = []
    for batch in BATCH_ORDER:
        members = [spec for spec in specs if batch in spec["batches"]]
        lines.append(f"{batch:<10} {len(members):>2}개  어림 {estimate_minutes(members):4.1f}분  " + " ".join(spec["name"] for spec in members))
    main_members = [spec for spec in specs if set(MAIN_BATCHES).intersection(spec["batches"])]
    lines.append(f"{'main':<10} {len(main_members):>2}개  어림 {estimate_minutes(main_members):4.1f}분")
    return lines


def run_batch(arguments):
    canonical = base.load_canonical_tuning()
    canonical_sha256 = base.sha256_of_file(base.CANONICAL_TUNING_PATH)
    field_types = load_field_types(canonical, True)
    specs = all_specifications()
    selected = select_specifications(specs, arguments.batch or [], arguments.only or [])
    if not selected:
        raise InputProblem("--batch 또는 --only 가 필요하다")
    solids = stage.build_solids()
    layout_problems = stage.find_layout_problems(solids)
    if layout_problems:
        raise InputProblem("무대 배치 점검 실패: " + "; ".join(layout_problems[:4]))
    material_file = ROOT.joinpath(*stage.GROUND_MATERIAL_FILE_RELATIVE_PATH)
    if not material_file.is_file():
        print("경고: 땅 기준 재질 에셋 파일이 아직 없다:", material_file.as_posix(), "(켬 실행은 재질 없이는 거부된다)")
    output_root = Path(arguments.out_root)
    written = []
    for spec in selected:
        materialize(spec, output_root, canonical, field_types, canonical_sha256, solids, arguments.set or [], arguments.keep_stain, not arguments.dry_run)
        written.append(spec)
        print(f"{spec['name']:<22} {spec['kind']:<6} {spec['description']}")
    print(f"실행 {len(written)}개 만듦, 어림 {estimate_minutes(written):.1f}분" + (" (건식, 파일 안 씀)" if arguments.dry_run else ""))
    if not arguments.dry_run:
        print("색인", update_index(output_root, written).as_posix())
    return 0


def deep_diff(first, second, prefix=""):
    differences = []
    if isinstance(first, dict) and isinstance(second, dict):
        for key in sorted(set(first) | set(second)):
            if key not in first or key not in second:
                differences.append(prefix + key)
            else:
                differences += deep_diff(first[key], second[key], prefix + key + ".")
    elif first != second:
        differences.append(prefix.rstrip("."))
    return differences


def run_self_test():
    failures = []

    def check(condition, label, detail=""):
        if not condition:
            failures.append(label)
        print(("통과 " if condition else "실패 ") + label + (": " + detail if detail else ""))

    canonical = base.load_canonical_tuning()
    canonical_sha_before = base.sha256_of_file(base.CANONICAL_TUNING_PATH)
    field_types = infer_field_types(canonical)
    specs = all_specifications()
    names = [spec["name"] for spec in specs]
    check(len(set(names)) == len(names) and all(re.fullmatch(r"[A-Za-z0-9_]{1,40}", name) for name in names), "실행 이름이 겹치지 않고 규칙에 맞음", f"{len(names)}개")
    counts = {batch: sum(1 for spec in specs if batch in spec["batches"]) for batch in BATCH_ORDER}
    check(counts == {"static": 10, "follow_off": 4, "walk": 12, "walk_side": 5, "run": 3, "run_off": 1, "walk_off": 6, "walk_p2": 5, "static_p2": 3, "order": 2, "trial_off": 15}, "묶음별 실행 수", str(counts))
    solids = stage.build_solids()
    check(not stage.find_layout_problems(solids), "무대 배치 점검 통과")
    check(LEVEL_PATH == "/Game/FluidFX/Maps/LV_TDFog3DGroundStage" and GROUND_MATERIAL_SOFT_PATH == "/Game/FluidFX/Materials/M_TD_Fog3DVolumeGround.M_TD_Fog3DVolumeGround", "맵·재질 경로", GROUND_MATERIAL_SOFT_PATH)
    check(GROUND_MATERIAL_FIELD in canonical["Globals"][base.SETTINGS_GROUP_NAME] and FOLLOW_FIELD in canonical["Globals"][base.SETTINGS_GROUP_NAME], "정본 JSON 에 땅 따라가기 필드가 있음")

    with tempfile.TemporaryDirectory() as directory:
        output_root = Path(directory)
        records = {}
        for spec in specs:
            outcome = materialize(spec, output_root, canonical, field_types, canonical_sha_before, solids, [], False, True)
            records[spec["name"]] = outcome
        check(len(records) == len(specs), "모든 실행의 변형 JSON·조절 표 사본·기록을 만들고 변형 키 검사(find_key_errors)를 통과", f"{len(records)}개")
        check(base.sha256_of_file(base.CANONICAL_TUNING_PATH) == canonical_sha_before, "정본 JSON 해시가 만든 뒤에도 같음")

        wrong_diffs = []
        for spec in specs:
            tuning = records[spec["name"]]["tuning"]
            allowed = {f"Globals.{base.SETTINGS_GROUP_NAME}.{FOLLOW_FIELD}"}
            if spec["follow"]:
                allowed.add(f"Globals.{base.SETTINGS_GROUP_NAME}.{GROUND_MATERIAL_FIELD}")
            if spec["kind"] != "static":
                allowed.add(f"Globals.{base.SETTINGS_GROUP_NAME}.{STAIN_FIELD}")
            differences = set(deep_diff(canonical, tuning))
            settings = tuning["Globals"][base.SETTINGS_GROUP_NAME]
            if not differences <= allowed or settings[FOLLOW_FIELD] is not spec["follow"] or (settings[GROUND_MATERIAL_FIELD] != "") != spec["follow"]:
                wrong_diffs.append(spec["name"])
        check(not wrong_diffs, "조절 표 사본은 정본과 허용 필드(땅 따라가기·재질·얼룩)만 다르고 켬일 때만 재질이 있음", ",".join(wrong_diffs[:4]))

        timeline_problems = []
        for spec in specs:
            if spec["kind"] != "walk":
                continue
            record = records[spec["name"]]["record"]
            events = {event["kind"]: event for event in record["events"]}
            line = stage.find_line(spec["line"])
            start = stage.line_start_world(line)
            order = [events[key]["time"] for key in ("compute_off", "prewalk", "relocate", "compute_on", "walk", "compute_off_end")]
            if order != sorted(order) or len(set(order)) != len(order):
                timeline_problems.append(f"{spec['name']} 순서")
            hidden_times = [entry["time"] for entry in record["shots"] if entry["role"] == "hidden"]
            reference_times = [entry["time"] for entry in record["shots"] if entry["role"] == "reference"]
            if len(hidden_times) != 1 or not (max(reference_times) < events["hide_zone"]["time"] < hidden_times[0]) or events["quality_end"]["time"] >= hidden_times[0]:
                timeline_problems.append(f"{spec['name']} 구역 숨김 장")
            if events["relocate"]["time"] - events["prewalk"]["time"] < 1.0 or events["compute_on"]["time"] - events["relocate"]["time"] < 0.5 or events["walk"]["time"] - events["compute_on"]["time"] < 0.6:
                timeline_problems.append(f"{spec['name']} 간격")
            if events["relocate"]["move"] != [round(start[0], 3), round(start[1], 3)] or not (events["prewalk"]["move"]["yaw"] == events["walk"]["move"]["yaw"] == line["yaw"]):
                timeline_problems.append(f"{spec['name']} 시작점·방향")
            end_estimate = events["walk"]["time"] + walk_end_estimate_seconds(line["distance_cm"])
            stopped = [entry["time"] for entry in record["shots"] if entry["role"] == "stopped"]
            references = [entry["time"] for entry in record["shots"] if entry["role"] == "reference"]
            if end_estimate + WALK_STOP_SETTLE_SECONDS > min(stopped) or max(stopped) >= events["compute_off_end"]["time"] or min(references) <= events["compute_off_end"]["time"]:
                timeline_problems.append(f"{spec['name']} 촬영 시각")
            expected_z = stage.line_start_height(solids, line) + stage.PAWN_SPAWN_HEIGHT_CM
            if abs(record["pawn_start"][2] - expected_z) > 1e-3 or record["sun_yaw"] != line["yaw"] + SUN_YAW_OFFSET_DEGREES:
                timeline_problems.append(f"{spec['name']} 폰 높이·해 방향")
        check(not timeline_problems, "걷기 시각표: 계산 끔 < 미리 걷기 < 순간이동 < 계산 켬 < 걷기 < 계산 끔, 멈춘 장은 걷기 끝 뒤, 기준 장은 계산 끔 뒤", ",".join(timeline_problems[:4]))

        static_problems = []
        for spec in specs:
            if spec["kind"] != "static":
                continue
            record = records[spec["name"]]["record"]
            shown = [entry["time"] for entry in record["shots"] if entry["role"] == "shown"]
            hidden = [entry["time"] for entry in record["shots"] if entry["role"] == "hidden"]
            hide_time = record["events"][0]["time"]
            if not (max(shown) < hide_time < min(hidden)) or record["view_target"] != stage.camera_label(spec["station"], spec["camera"]):
                static_problems.append(spec["name"])
        check(not static_problems, "정지 촬영: 켬 두 장 → 구역 숨김 → 끔 두 장, 고정 카메라 라벨이 무대 카메라와 같음", ",".join(static_problems))

        run_problems = []
        for spec in specs:
            if spec["kind"] != "run":
                continue
            record = records[spec["name"]]["record"]
            last_shot = max(entry["time"] for entry in record["shots"])
            end_estimate = RUN_START_TIME + record["walk_distance_cm"] / WALK_SPEED_CM_PER_SECOND
            if len(record["shots"]) != RUN_SHOT_COUNT or last_shot < end_estimate + 1.0 or record["events"][0]["move"]["distance_cm"] != 3000.0:
                run_problems.append(spec["name"])
        check(not run_problems, "달리기: 0.1초 간격 71장, 마지막 장이 달리기 끝 추정 뒤 1초 이상, 거리 3000")

        order_names = [spec["name"] for spec in specs if "order" in spec["batches"]]
        order_ok = all(VERBOSE_ON_COMMAND in records[name]["variants"]["base"]["console"] and records[name]["variants"]["variants"][0]["apply"]["schedule"]["events"][-1]["console"] == [VERBOSE_OFF_COMMAND] for name in order_names)
        check(order_ok, "순서 확인 실행: 구역 Verbose 로그를 켜고 끝에서 되돌림")

        back_run = records["w_W02_back_on"]["record"]
        check(abs(back_run["view"][0] - (-12.0 + 30.0)) < 0.01 and back_run["view"][1] == 0.0 and back_run["arm"] == 400, "뒤 카메라 높낮이 보정: 30도 오르막 pitch −12+30", str(back_run["view"]))
        down_run = records["w_W03_back_on"]["record"]
        check(abs(down_run["view"][0] - (-12.0 - 30.0)) < 0.01, "뒤 카메라 높낮이 보정: 30도 내리막 pitch −42", str(down_run["view"]))
        side_run = records["w_W08_side_on"]["record"]
        check(side_run["view"] == [-20.0, 180.0] and side_run["arm"] == 500, "옆 카메라: 걷는 방향 90도 + 90 = 180", str(side_run["view"]))
        json.dumps([records[name]["record"] for name in records], allow_nan=False)
        check(True, "모든 실행 기록이 JSON 으로 직렬화됨(NaN 없음)")

        index_path = update_index(output_root, specs)
        index = json.loads(index_path.read_text(encoding="utf-8"))
        check(sum(len(names_) for names_ in index["batches"].values()) == sum(len(spec["batches"]) for spec in specs), "색인 묶음 수가 맞음")
    print("자체 점검", "통과" if not failures else "실패 " + str(len(failures)) + "건")
    return 0 if not failures else base.EXIT_FAILED


def parse_arguments(argument_list):
    parser = argparse.ArgumentParser(description="3D 안개 2a-3 땅 따라가기 촬영 변형·조절 표 사본 만들기")
    parser.add_argument("--batch", nargs="+", help="묶음 이름들(main all " + " ".join(BATCH_ORDER) + ")")
    parser.add_argument("--only", nargs="+", help="실행 이름으로 직접 고름")
    parser.add_argument("--set", nargs="+", help="모든 실행의 조절 표 사본에 더할 필드=값(bool·int32·float)")
    parser.add_argument("--keep-stain", action="store_true", help="걷기·달리기에서도 얼룩을 끄지 않는다(기본은 L-editor-104 대로 끔)")
    parser.add_argument("--out-root", default=str(GROUND_ROOT))
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args(argument_list)


def main(argument_list=None):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    arguments = parse_arguments(argument_list)
    if arguments.self_test:
        return run_self_test()
    if arguments.list:
        print("\n".join(describe_batches(all_specifications())))
        return 0
    try:
        return run_batch(arguments)
    except (InputProblem, base.InputProblem, OSError, ValueError, KeyError) as error:
        print("입력 문제:", error)
        return base.EXIT_INPUT_PROBLEM


if __name__ == "__main__":
    sys.exit(main())
