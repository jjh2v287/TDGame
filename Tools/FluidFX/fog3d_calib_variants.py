"""실행 환경: 시스템 Python(표준 라이브러리만, 언리얼 에디터 불필요). 3D 안개 1c 일괄 보정(prereg-1c.md)의 실행 하나분 촬영 변형 JSON·조절 표 사본·실행 기록을 만든다. 값은 Saved 아래 사본으로만 바꾸고 정본 JSON·코드·에셋은 건드리지 않는다.
실행 명령: python -B Tools/FluidFX/fog3d_calib_variants.py --name <실행 이름> [--set 필드=값 ...] [--camera back|side|high|top] [--light orig|side|front|side_flip] [--camera-yaw 도] [--walk-yaw 도] [--hide-zone] [--console "명령" ...] [--console-restore "명령" ...] [--on-mode 2|1] [--walk-shift 초] 또는 --matrix explore|round2 [--light ...] [--grid-size-z 현재값] [--only 이름...] [--dry-run], 점검만: --self-test (PowerShell, 한글 출력은 PYTHONUTF8=1)
출력: Saved/FluidFX/fog3d_calib/<실행 이름>/variants.json(촬영 도구 입력), tuning.json(정본 사본에서 지정 필드만 바꾼 조절 표), run.json(바꾼 값·장 이름과 시각 대응표·유효 조건·실행 명령), --matrix explore 일 때 Saved/FluidFX/fog3d_calib/index.json 과 run_all.ps1, --matrix round2 일 때 index_round2.json(2회차 27실행, --only 없을 때만). 종료 코드 0 정상 / 1 점검 실패 / 2 입력 문제.
상태: 2회차 추가분(카메라 top·빛 side_flip·카메라/걷기 방향·구역 숨김·round2 27실행)은 에디터 없이 --self-test 만 확인(촬영 도구의 키 검사 find_key_errors 통과, 시각표를 등록본 표와 대조). 실제 PIE 촬영은 미검증, 해 회전을 props 의 root_component.relative_rotation 으로 바꾸는 방식은 에디터에서 처음 확인해야 한다.
"""
import argparse
import copy
import difflib
import hashlib
import json
import math
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CALIBRATION_ROOT = ROOT / "Saved" / "FluidFX" / "fog3d_calib"
CANONICAL_TUNING_PATH = ROOT / "Content" / "FluidFX" / "PresetData" / "TDFluidPresetTuning.json"
SETTINGS_HEADER_PATH = ROOT / "Source" / "TDGame" / "FluidFX" / "TDFog3DTypes.h"
STAGE_SCRIPT_PATH = ROOT / "Tools" / "FluidFX" / "editor_make_fog3d_stage.py"
PREREGISTRATION_PATH = ROOT / "Docs" / "VolumeFog3D" / "stage1" / "prereg-1c.md"
STAGE_LEVEL_PATH = "/Game/FluidFX/Maps/LV_TDFog3DStage"
SETTINGS_GROUP_NAME = "TDFog3DSettings"
SETTINGS_STRUCT_MARKER = "struct TDGAME_API FTDFog3DSettings"
SUPPORTED_FIELD_TYPES = ("bool", "int32", "float")
SWING_COMMAND = "TDPlayMeleeAction"
COMPUTE_OFF_COMMAND = "TD.Fog3D.Mode 0"
COMPUTE_ON_COMMAND_FORMAT = "TD.Fog3D.Mode {mode}"
ALLOWED_ON_MODES = (1, 2)
DEFAULT_ON_MODE = 2
SWING_START_TIME = 0.0
SWING_COMPUTE_OFF_TIME = 7.3
COMPUTE_ON_TIME = 8.9
WALK_START_TIME = 9.6
WALK_COMPUTE_OFF_TIME = 18.4
WALK_DISTANCE_CM = 600
WALK_YAW_DEGREES = 0
RESTORE_DELAY_AFTER_LAST_SHOT_SECONDS = 0.6
SWING_SHOT_TABLE = (
    ("S0.6", 0.6, "moving"), ("S1.2", 1.2, "moving"), ("S1.9", 1.9, "moving"),
    ("S2.5", 2.5, "stopped"), ("S3.5", 3.5, "stopped"), ("S5.0", 5.0, "stopped"), ("S7.0", 7.0, "stopped"),
    ("S8.3", 8.3, "reference"), ("S8.8", 8.8, "reference"),
)
WALK_SHOT_TABLE = (
    ("W10.0", 10.0, "moving"), ("W10.4", 10.4, "moving"), ("W10.8", 10.8, "moving"),
    ("W12.1", 12.1, "stopped"), ("W13.1", 13.1, "stopped"), ("W14.1", 14.1, "stopped"), ("W15.6", 15.6, "stopped"), ("W17.6", 17.6, "stopped"),
    ("W19.4", 19.4, "reference"), ("W19.9", 19.9, "reference"),
)
SWING_GAP_RANGE_SECONDS = (2.15, 3.0)
WALK_END_LATEST_SECONDS = 11.6
SHOT_PREFIX = "c"
VARIANT_NAME = "calib"
FIXED_FRAMES_PER_SECOND = 60
SCHEDULE_TIMEOUT_SECONDS = 240
DENSE_SWING_LAST_SECONDS = 6.0
DENSE_COMPUTE_OFF_TIME = 6.3
DENSE_COMPUTE_ON_TIME = 6.6
DENSE_WALK_START_TIME = 7.3
DENSE_WALK_LAST_SECONDS = 14.3
DENSE_SCHEDULE_TIMEOUT_SECONDS = 420
DENSE_LATE_LIMIT_SECONDS = 0.05
DENSE_ALLOWED_STEPS = (0.1, 0.2)
CAPTURE_SUBDIRECTORY = "capture"
CAMERAS = {"back": (400, [-12, 0]), "side": (500, [-20, 90]), "high": (700, [-35, 30]), "top": (800, [-60, 0])}
SUN_LABEL = "TDGen_Fog3D_Sun"
LIGHT_ROTATIONS = {"orig": [-18.0, 160.0, 0.0], "side": [-35.0, 100.0, 0.0], "front": [-40.0, 20.0, 0.0], "side_flip": [-35.0, 280.0, 0.0]}
ZONE_LABEL = "TDGen_Fog3D_Zone"
WALK_START_POINT_CM = (-1000.0, 0.0)
ZONE_HALF_EXTENT_XY_CM = 3000.0
OBSTACLE_BOXES_CM = ((500.0, -400.0, 75.0, 75.0), (500.0, 400.0, 75.0, 75.0), (1100.0, 0.0, 5.0, 150.0))
WALK_PATH_CLEARANCE_CM = 150.0
WALK_PATH_SAMPLE_COUNT = 61
MID_VALUES = {"FootRadiusCm": 45.0, "HandRadiusCm": 45.0, "PelvisRadiusCm": 60.0, "BodyWindStrength": 2.0, "BladeRadiusCm": 30.0}
MID_STAIN = {"bStainNoiseEnabled": True, "StainNoiseAmplitude": 0.6, "StainNoiseSpacingCm": 400.0}
THICK_FOG = {"BaseThicknessCm": 150.0, "BaseTopBlurCm": 120.0}
GRID_SIZE_Z_COMMAND = "r.VolumetricFog.GridSizeZ"
INDEX_FILE_NAMES = {"explore": "index.json", "round2": "index_round2.json"}
ENGINE_HISTORY_WEIGHT_ORIGINAL = 0.9
ENGINE_GRID_PIXEL_SIZE_ORIGINAL = 8
ENGINE_HISTORY_COMMAND = "r.VolumetricFog.HistoryWeight"
ENGINE_GRID_COMMAND = "r.VolumetricFog.GridPixelSize"
EXIT_FAILED = 1
EXIT_INPUT_PROBLEM = 2


class InputProblem(Exception):
    pass


def write_text(path, text):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)


def write_json(path, value):
    write_text(path, json.dumps(value, ensure_ascii=False, indent=2) + "\n")


def sha256_of_file(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def parse_settings_header(text):
    start = text.find(SETTINGS_STRUCT_MARKER)
    if start < 0:
        raise InputProblem(f"헤더에서 {SETTINGS_STRUCT_MARKER} 를 찾지 못했다")
    end = text.find("\n};", start)
    body = text[start:end]
    pattern = re.compile(r"UPROPERTY\([^)]*\)\s*(bool|int32|float|FName|FVector)\s+(\w+)\s*=\s*([^;]+);")
    return {match.group(2): match.group(1) for match in pattern.finditer(body)}


def load_canonical_tuning():
    return json.loads(CANONICAL_TUNING_PATH.read_text(encoding="utf-8"))


def parse_boolean(text):
    lowered = text.strip().lower()
    if lowered in ("true", "1", "on", "yes"):
        return True
    if lowered in ("false", "0", "off", "no"):
        return False
    raise ValueError(f"불리언은 true/false 여야 한다: {text}")


def parse_field_value(field_type, text):
    if field_type == "bool":
        return parse_boolean(text)
    if field_type == "int32":
        return int(text.strip())
    value = float(text.strip())
    if not math.isfinite(value):
        raise ValueError(f"유한한 숫자여야 한다: {text}")
    return value


def parse_assignments(assignments, field_types):
    changes = {}
    for assignment in assignments:
        field, separator, text = assignment.partition("=")
        field = field.strip()
        if not separator or not field:
            raise InputProblem(f"--set 은 필드=값 꼴이어야 한다: {assignment}")
        if field not in field_types:
            close = difflib.get_close_matches(field, list(field_types), n=1)
            raise InputProblem(f"헤더에 없는 필드 {field}" + (f" ({close[0]}?)" if close else ""))
        if field_types[field] not in SUPPORTED_FIELD_TYPES:
            raise InputProblem(f"{field} 은 {field_types[field]} 형식이라 이 도구가 바꾸지 않는다")
        if field in changes:
            raise InputProblem(f"같은 필드를 두 번 지정했다: {field}")
        try:
            changes[field] = parse_field_value(field_types[field], text)
        except ValueError as error:
            raise InputProblem(f"{field}: {error}")
    return changes


def build_tuning_copy(canonical, changes, field_types):
    tuning = copy.deepcopy(canonical)
    settings = tuning["Globals"][SETTINGS_GROUP_NAME]
    for field, value in changes.items():
        if field not in settings:
            raise InputProblem(f"정본 JSON 의 {SETTINGS_GROUP_NAME} 에 {field} 가 없다")
        settings[field] = float(value) if field_types[field] == "float" else value
    return tuning


def describe_changes(canonical, changes):
    settings = canonical["Globals"][SETTINGS_GROUP_NAME]
    return {field: {"from": settings[field], "to": value} for field, value in changes.items()}


def predict_value_problems(settings):
    problems = []

    def at_least(name, minimum):
        if not settings[name] >= minimum:
            problems.append(f"{name} 은 {minimum} 이상이어야 한다({settings[name]})")

    def greater_than(name, lower):
        if not settings[name] > lower:
            problems.append(f"{name} 은 {lower} 보다 커야 한다({settings[name]})")

    def within(name, minimum, maximum):
        if not minimum <= settings[name] <= maximum:
            problems.append(f"{name} 은 [{minimum}, {maximum}] 안이어야 한다({settings[name]})")

    def power_of_two_resolution(name):
        value = settings[name]
        if not (value > 0 and value & (value - 1) == 0 and 64 <= value <= 512):
            problems.append(f"{name} 은 64~512 의 2 의 거듭제곱이어야 한다({value})")

    within("Mode", 0, 2)
    within("DefaultQualityLevel", 0, 2)
    for name in ("QualityLowResolution", "QualityMediumResolution", "QualityHighResolution"):
        power_of_two_resolution(name)
    for name in ("QualityLowStepsPerSecond", "QualityMediumStepsPerSecond", "QualityHighStepsPerSecond"):
        within(name, 1e-8, 240.0)
    if abs(settings["CellSizeCm"] - 15.625) > 1e-6:
        problems.append(f"CellSizeCm 은 1단계에서 15.625 여야 한다({settings['CellSizeCm']})")
    if abs(settings["LayerThicknessCm"] - 30.0) > 1e-6:
        problems.append(f"LayerThicknessCm 은 1단계에서 30 이어야 한다({settings['LayerThicknessCm']})")
    within("MaximumStepSeconds", 1e-8, 0.05)
    within("WindowFollowMarginCells", 8, 56)
    smallest = min(settings["QualityLowResolution"], settings["QualityMediumResolution"], settings["QualityHighResolution"])
    follow_limit = smallest // 2 - settings["EdgeBandCells"] - 8
    if settings["WindowFollowMarginCells"] > follow_limit:
        problems.append(f"WindowFollowMarginCells 는 {follow_limit} 이하여야 한다({settings['WindowFollowMarginCells']})")
    at_least("BaseDensity", 0.0)
    at_least("BaseTopBlurCm", 0.0)
    greater_than("BaseThicknessCm", settings["BaseTopBlurCm"])
    for name in ("WindRefillPerSec", "DisplacementRefillHorizontalPerSec", "DisplacementRefillVerticalPerSec", "FillRefillPerSec", "EdgeBandAbsorptionPerSec"):
        at_least(name, 0.0)
    within("EdgeBandCells", 0, 16)
    for name in ("BladeRadiusCm", "HandRadiusCm", "FootRadiusCm", "PelvisRadiusCm"):
        at_least(name, 16.0)
    for name in ("BladeWindStrength", "BladeErosionPerSec", "BodyWindStrength", "BodyErosionPerSec"):
        at_least(name, 0.0)
    greater_than("MaximumInputSpeedCmPerSec", 0.0)
    within("MaximumInputSphereCount", 1, 16)
    within("MaximumObstacleCount", 0, 32)
    greater_than("ObstacleBaseHalfThicknessCm", 0.0)
    for name in ("ObstacleUpwardExtraCm", "ZoneEdgeFadeCm", "ZoneEntryVerticalMarginCm"):
        at_least(name, 0.0)
    for prefix in ("FineNoise", "StainNoise"):
        within(prefix + "Amplitude", 0.0, 1.0 - 1e-8)
        greater_than(prefix + "SpacingCm", 0.0)
        within(prefix + "Seed", 0, 8388607)
    return problems


def shift_time(value, walk_shift):
    return round(value + walk_shift, 3)


def build_shot_table(walk_shift=0.0):
    table = [{"id": shot_id, "group": "S", "time": time_value, "role": role} for shot_id, time_value, role in SWING_SHOT_TABLE]
    table += [{"id": shot_id, "group": "W", "time": shift_time(time_value, walk_shift), "role": role} for shot_id, time_value, role in WALK_SHOT_TABLE]
    return table


def build_dense_shot_table(step):
    swing_count = int(round(DENSE_SWING_LAST_SECONDS / step))
    walk_count = int(round((DENSE_WALK_LAST_SECONDS - DENSE_WALK_START_TIME) / step))
    table = [{"id": f"S{round(index * step, 3):.1f}", "group": "S", "time": round(index * step, 3), "role": "dense"} for index in range(swing_count + 1)]
    table += [{"id": f"W{round(DENSE_WALK_START_TIME + index * step, 3):.1f}", "group": "W", "time": round(DENSE_WALK_START_TIME + index * step, 3), "role": "dense"} for index in range(walk_count + 1)]
    return table


def build_dense_event_records(on_mode, restore_commands, walk_yaw=WALK_YAW_DEGREES):
    records = [
        {"kind": "swing", "time": SWING_START_TIME, "console": [SWING_COMMAND]},
        {"kind": "swing_compute_off", "time": DENSE_COMPUTE_OFF_TIME, "console": [COMPUTE_OFF_COMMAND]},
        {"kind": "compute_on", "time": DENSE_COMPUTE_ON_TIME, "console": [COMPUTE_ON_COMMAND_FORMAT.format(mode=on_mode)]},
        {"kind": "walk", "time": DENSE_WALK_START_TIME, "move": {"distance_cm": WALK_DISTANCE_CM, "yaw": walk_yaw}},
    ]
    if restore_commands:
        records.append({"kind": "restore", "time": round(DENSE_WALK_LAST_SECONDS + RESTORE_DELAY_AFTER_LAST_SHOT_SECONDS, 3), "console": list(restore_commands)})
    return records


def build_event_records(on_mode, walk_shift, restore_commands, walk_yaw=WALK_YAW_DEGREES):
    records = [
        {"kind": "swing", "time": SWING_START_TIME, "console": [SWING_COMMAND]},
        {"kind": "swing_compute_off", "time": SWING_COMPUTE_OFF_TIME, "console": [COMPUTE_OFF_COMMAND]},
        {"kind": "compute_on", "time": COMPUTE_ON_TIME, "console": [COMPUTE_ON_COMMAND_FORMAT.format(mode=on_mode)]},
        {"kind": "walk", "time": shift_time(WALK_START_TIME, walk_shift), "move": {"distance_cm": WALK_DISTANCE_CM, "yaw": walk_yaw}},
        {"kind": "walk_compute_off", "time": shift_time(WALK_COMPUTE_OFF_TIME, walk_shift), "console": [COMPUTE_OFF_COMMAND]},
    ]
    if restore_commands:
        last_shot_time = shift_time(WALK_SHOT_TABLE[-1][1], walk_shift)
        records.append({"kind": "restore", "time": round(last_shot_time + RESTORE_DELAY_AFTER_LAST_SHOT_SECONDS, 3), "console": list(restore_commands)})
    return records


def build_schedule(on_mode, walk_shift, restore_commands, dense_step=None, walk_yaw=WALK_YAW_DEGREES):
    if dense_step:
        dense_events = [{key: value for key, value in record.items() if key != "kind"} for record in build_dense_event_records(on_mode, restore_commands, walk_yaw)]
        return {"reference": "now", "prefix": SHOT_PREFIX, "fixed_fps": FIXED_FRAMES_PER_SECOND, "timeout": DENSE_SCHEDULE_TIMEOUT_SECONDS,
                "times": [entry["time"] for entry in build_dense_shot_table(dense_step)], "events": dense_events}
    events = [{key: value for key, value in record.items() if key != "kind"} for record in build_event_records(on_mode, walk_shift, restore_commands, walk_yaw)]
    return {"reference": "now", "prefix": SHOT_PREFIX, "fixed_fps": FIXED_FRAMES_PER_SECOND, "timeout": SCHEDULE_TIMEOUT_SECONDS,
            "times": [entry["time"] for entry in build_shot_table(walk_shift)], "events": events}


def effective_sun_rotation(light, sun_yaw=None):
    if sun_yaw is not None:
        return [-35.0, float(sun_yaw), 0.0]
    return LIGHT_ROTATIONS[light]


def build_light_props(light, sun_yaw=None):
    rotation = effective_sun_rotation(light, sun_yaw)
    if rotation is None:
        return None
    return {SUN_LABEL: {"root_component": {"relative_rotation": list(rotation)}}}


def describe_run_directory(output_root, name):
    directory = (Path(output_root) / name).resolve()
    try:
        return directory.relative_to(ROOT).as_posix()
    except ValueError:
        return directory.as_posix()


def build_variants(name, camera, light, on_mode, extra_console, restore_commands, walk_shift, run_directory, dense_step=None, camera_yaw=None, walk_yaw=WALK_YAW_DEGREES, hide_zone=False, sun_yaw=None):
    arm_length, view = CAMERAS[camera]
    if camera_yaw is not None:
        view = [view[0], camera_yaw]
    tuning_path = f"{run_directory}/tuning.json"
    base = {"arm": arm_length, "view": list(view), "console": [COMPUTE_ON_COMMAND_FORMAT.format(mode=on_mode)] + list(extra_console),
            "tuning_reload": {"tuning": tuning_path, "reset": "keep"}}
    light_props = build_light_props(light, sun_yaw)
    if light_props is not None:
        base["props"] = light_props
    if hide_zone:
        base["hide"] = {ZONE_LABEL: True}
    return {"base": base, "variants": [{"name": VARIANT_NAME, "apply": {"schedule": build_schedule(on_mode, walk_shift, restore_commands, dense_step, walk_yaw)}, "settle": 0.5}]}


def find_variant_key_problems(variants):
    for directory in (ROOT / "Tools", ROOT / "Tools" / "FluidFX"):
        if str(directory) not in sys.path:
            sys.path.insert(0, str(directory))
    import pie_lookdev_fluid
    problems = [f"base: {line}" for line in pie_lookdev_fluid.find_key_errors(variants.get("base", {}))]
    for entry in variants.get("variants", []):
        problems += [f"{entry.get('name')}: {line}" for line in pie_lookdev_fluid.find_key_errors(entry.get("apply", {}))]
    names = [entry.get("name") for entry in variants.get("variants", [])]
    if len(set(names)) != len(names):
        problems.append("변형 이름이 겹친다")
    return problems


def build_capture_commands(name, run_directory):
    return {
        "capture": f"python Tools/FluidFX/pie_lookdev_fluid.py --variants {run_directory}/variants.json --level {STAGE_LEVEL_PATH} --fresh-pie --stop-pie --output-dir {run_directory}/{CAPTURE_SUBDIRECTORY} --run-id fog3d_calib_{name}",
        "snapshot_log": f"python -B Tools/FluidFX/fog3d_calib_report.py --snapshot-log {name}",
        "report": f"python -B Tools/FluidFX/fog3d_calib_report.py --runs {name}",
    }


def build_run_record(specification, canonical, changes, canonical_sha256, run_directory):
    walk_shift = specification["walk_shift"]
    dense_step = specification.get("dense_step")
    if dense_step:
        records = build_dense_event_records(specification["on_mode"], specification["console_restore"], specification.get("walk_yaw", WALK_YAW_DEGREES))
        shots = build_dense_shot_table(dense_step)
        valid_conditions = {"dense": True, "dense_step_seconds": dense_step, "late_limit_seconds": DENSE_LATE_LIMIT_SECONDS, "walk_start_seconds": DENSE_WALK_START_TIME}
    else:
        records = build_event_records(specification["on_mode"], walk_shift, specification["console_restore"], specification.get("walk_yaw", WALK_YAW_DEGREES))
        shots = build_shot_table(walk_shift)
        valid_conditions = {"swing_to_S2.5_seconds": list(SWING_GAP_RANGE_SECONDS), "walk_end_before_seconds": round(WALK_END_LATEST_SECONDS + walk_shift, 3)}
    return {
        "name": specification["name"], "group": specification.get("group", ""), "description": specification.get("description", ""),
        "camera": specification["camera"], "light": specification["light"], "on_mode": specification["on_mode"], "walk_shift_seconds": walk_shift,
        "camera_yaw_degrees": specification.get("camera_yaw"), "walk_yaw_degrees": specification.get("walk_yaw", WALK_YAW_DEGREES), "hide_zone": bool(specification.get("hide_zone")),
        "walk_end_cm": [round(value, 3) for value in walk_end_point(specification.get("walk_yaw", WALK_YAW_DEGREES))],
        "changed_fields": describe_changes(canonical, changes), "console_extra": list(specification["console_extra"]), "console_restore": list(specification["console_restore"]),
        "level": STAGE_LEVEL_PATH, "shots": shots, "events": records, "dense_step_seconds": dense_step,
        "valid_conditions": valid_conditions,
        "sun_label": SUN_LABEL, "sun_rotation_degrees": effective_sun_rotation(specification["light"], specification.get("sun_yaw")),
        "canonical_tuning_sha256": canonical_sha256, "preregistration": PREREGISTRATION_PATH.relative_to(ROOT).as_posix(), "preregistration_sha256": sha256_of_file(PREREGISTRATION_PATH),
        "commands": build_capture_commands(specification["name"], run_directory),
        "notes": ["엔진 안개 cvar 는 프로세스 전역이다. 실행이 중간에 끊기면 console_restore 를 에디터 콘솔에서 직접 입력해 되돌린다." if specification["console_restore"] else "엔진 설정을 바꾸지 않는다."],
    }


def make_specification(name, camera, light, on_mode, walk_shift, changes=None, console_extra=(), console_restore=(), group="", description="", dense_step=None, camera_yaw=None, walk_yaw=WALK_YAW_DEGREES, hide_zone=False, sun_yaw=None):
    return {"sun_yaw": sun_yaw, "dense_step": dense_step, "camera_yaw": camera_yaw, "walk_yaw": walk_yaw, "hide_zone": bool(hide_zone), "name": name, "camera": camera, "light": light, "on_mode": on_mode, "walk_shift": walk_shift, "changes": dict(changes or {}),
            "console_extra": list(console_extra), "console_restore": list(console_restore), "group": group, "description": description}


def walk_end_point(walk_yaw, distance_cm=WALK_DISTANCE_CM):
    radians = math.radians(walk_yaw)
    return (WALK_START_POINT_CM[0] + distance_cm * math.cos(radians), WALK_START_POINT_CM[1] + distance_cm * math.sin(radians))


def distance_to_box(point, box):
    center_x, center_y, half_x, half_y = box
    outside_x = max(abs(point[0] - center_x) - half_x, 0.0)
    outside_y = max(abs(point[1] - center_y) - half_y, 0.0)
    return math.hypot(outside_x, outside_y)


def walk_path_problems(walk_yaw, distance_cm=WALK_DISTANCE_CM):
    end_x, end_y = walk_end_point(walk_yaw, distance_cm)
    problems = []
    limit = ZONE_HALF_EXTENT_XY_CM - WALK_PATH_CLEARANCE_CM
    if abs(end_x) > limit or abs(end_y) > limit:
        problems.append(f"걷기 끝 ({end_x:.0f}, {end_y:.0f}) 이 구역 안쪽 {limit:.0f} 밖")
    for index in range(WALK_PATH_SAMPLE_COUNT):
        fraction = index / (WALK_PATH_SAMPLE_COUNT - 1)
        point = (WALK_START_POINT_CM[0] + (end_x - WALK_START_POINT_CM[0]) * fraction, WALK_START_POINT_CM[1] + (end_y - WALK_START_POINT_CM[1]) * fraction)
        for box in OBSTACLE_BOXES_CM:
            if distance_to_box(point, box) < WALK_PATH_CLEARANCE_CM:
                problems.append(f"걷는 길 ({point[0]:.0f}, {point[1]:.0f}) 이 물체 {box[:2]} 에서 {WALK_PATH_CLEARANCE_CM:.0f}cm 안")
                return problems
    return problems


def validate_specification(specification):
    if not re.fullmatch(r"[A-Za-z0-9_]{1,32}", specification["name"]):
        raise InputProblem(f"실행 이름은 영문·숫자·밑줄 32자 이하: {specification['name']}")
    if specification["camera"] not in CAMERAS:
        raise InputProblem(f"카메라는 {'|'.join(CAMERAS)} 중 하나: {specification['camera']}")
    if specification["light"] not in LIGHT_ROTATIONS:
        raise InputProblem(f"빛은 {'|'.join(LIGHT_ROTATIONS)} 중 하나: {specification['light']}")
    if specification["on_mode"] not in ALLOWED_ON_MODES:
        raise InputProblem(f"--on-mode 는 {ALLOWED_ON_MODES} 중 하나: {specification['on_mode']}")
    if not math.isfinite(specification["walk_shift"]) or specification["walk_shift"] < 0:
        raise InputProblem(f"--walk-shift 는 0 이상 숫자(초): {specification['walk_shift']}")
    for key in ("camera_yaw", "walk_yaw", "sun_yaw"):
        value = specification.get(key)
        if value is not None and not (isinstance(value, (int, float)) and math.isfinite(value)):
            raise InputProblem(f"{key} 는 유한한 숫자(도): {value}")
    path_problems = walk_path_problems(specification.get("walk_yaw", WALK_YAW_DEGREES))
    if path_problems:
        raise InputProblem("걷기 길 점검 실패: " + "; ".join(path_problems))
    dense_step = specification.get("dense_step")
    if dense_step is not None and (dense_step not in DENSE_ALLOWED_STEPS or specification["walk_shift"] != 0):
        raise InputProblem(f"--dense-step 은 {DENSE_ALLOWED_STEPS} 중 하나이고 --walk-shift 와 함께 쓰지 않는다: {dense_step}")


def materialize_run(specification, output_root, canonical, field_types, canonical_sha256, write_files=True):
    validate_specification(specification)
    changes = specification["changes"]
    tuning = build_tuning_copy(canonical, changes, field_types)
    predicted_problems = predict_value_problems(tuning["Globals"][SETTINGS_GROUP_NAME])
    if predicted_problems:
        return {"status": "skipped", "reason": "값 검사에 걸릴 것으로 예측: " + "; ".join(predicted_problems)}
    run_directory = describe_run_directory(output_root, specification["name"])
    variants = build_variants(specification["name"], specification["camera"], specification["light"], specification["on_mode"], specification["console_extra"],
                              specification["console_restore"], specification["walk_shift"], run_directory, specification.get("dense_step"),
                              specification.get("camera_yaw"), specification.get("walk_yaw", WALK_YAW_DEGREES), specification.get("hide_zone", False), specification.get("sun_yaw"))
    key_problems = find_variant_key_problems(variants)
    if key_problems:
        raise InputProblem("변형 키 오류: " + "; ".join(key_problems))
    record = build_run_record(specification, canonical, changes, canonical_sha256, run_directory)
    if write_files:
        run_directory = Path(output_root) / specification["name"]
        write_json(run_directory / "tuning.json", tuning)
        write_json(run_directory / "variants.json", variants)
        write_json(run_directory / "run.json", record)
    return {"status": "written", "record": record, "variants": variants, "tuning": tuning}


def explore_specifications(camera, light, walk_shift):
    body_size = {"BodyErosionPerSec": 12.0, "FootRadiusCm": 35.0, "HandRadiusCm": 35.0, "PelvisRadiusCm": 45.0}
    engine_restore = [f"{ENGINE_HISTORY_COMMAND} {ENGINE_HISTORY_WEIGHT_ORIGINAL:g}", f"{ENGINE_GRID_COMMAND} {ENGINE_GRID_PIXEL_SIZE_ORIGINAL:g}"]
    table = [
        ("기본값", "base_a", {}, DEFAULT_ON_MODE, [], [], "기본값 처음"),
        ("몸 깎기", "body4", {"BodyErosionPerSec": 4.0}, DEFAULT_ON_MODE, [], [], "BodyErosionPerSec 4"),
        ("몸 깎기", "body12", {"BodyErosionPerSec": 12.0}, DEFAULT_ON_MODE, [], [], "BodyErosionPerSec 12"),
        ("몸 깎기", "body30", {"BodyErosionPerSec": 30.0}, DEFAULT_ON_MODE, [], [], "BodyErosionPerSec 30"),
        ("몸 크기", "bodysize", body_size, DEFAULT_ON_MODE, [], [], "발·손 반지름 35, 골반 45, 몸 깎기 12"),
        ("얼룩", "stain1", {"bStainNoiseEnabled": True, "StainNoiseAmplitude": 0.3, "StainNoiseSpacingCm": 200.0}, DEFAULT_ON_MODE, [], [], "얼룩 켬 0.3 / 200"),
        ("얼룩", "stain2", {"bStainNoiseEnabled": True, "StainNoiseAmplitude": 0.6, "StainNoiseSpacingCm": 300.0}, DEFAULT_ON_MODE, [], [], "얼룩 켬 0.6 / 300"),
        ("얼룩", "stain3", {"bStainNoiseEnabled": True, "StainNoiseAmplitude": 0.6, "StainNoiseSpacingCm": 500.0}, DEFAULT_ON_MODE, [], [], "얼룩 켬 0.6 / 500"),
        ("얼룩", "stain4", {"bStainNoiseEnabled": True, "StainNoiseAmplitude": 0.9, "StainNoiseSpacingCm": 400.0}, DEFAULT_ON_MODE, [], [], "얼룩 켬 0.9 / 400"),
        ("잔무늬", "fine_off", {"bFineNoiseEnabled": False}, DEFAULT_ON_MODE, [], [], "잔무늬 끔"),
        ("잔무늬", "fine1", {"FineNoiseAmplitude": 0.5, "FineNoiseSpacingCm": 100.0}, DEFAULT_ON_MODE, [], [], "잔무늬 0.5 / 100"),
        ("잔무늬", "fine2", {"FineNoiseAmplitude": 0.5, "FineNoiseSpacingCm": 150.0}, DEFAULT_ON_MODE, [], [], "잔무늬 0.5 / 150"),
        ("잔무늬", "fine3", {"FineNoiseAmplitude": 0.8, "FineNoiseSpacingCm": 150.0}, DEFAULT_ON_MODE, [], [], "잔무늬 0.8 / 150"),
        ("잔무늬", "fine4", {"FineNoiseAmplitude": 0.8, "FineNoiseSpacingCm": 300.0}, DEFAULT_ON_MODE, [], [], "잔무늬 0.8 / 300 (덧붙임 2절)"),
        ("잔무늬", "fine5", {"FineNoiseAmplitude": 0.8, "FineNoiseSpacingCm": 500.0}, DEFAULT_ON_MODE, [], [], "잔무늬 0.8 / 500 (덧붙임 2절)"),
        ("잔무늬", "fine_stain", {"FineNoiseAmplitude": 0.8, "FineNoiseSpacingCm": 300.0, "bStainNoiseEnabled": True, "StainNoiseAmplitude": 0.6, "StainNoiseSpacingCm": 500.0}, DEFAULT_ON_MODE, [], [], "잔무늬 0.8 / 300 + 얼룩 켬 0.6 / 500 (덧붙임 2절)"),
        ("안개 높이", "h60", {"BaseThicknessCm": 60.0, "BaseTopBlurCm": 30.0}, DEFAULT_ON_MODE, [], [], "두께 60, 윗면 흐림 30"),
        ("안개 높이", "h130", {"BaseThicknessCm": 130.0, "BaseTopBlurCm": 65.0}, DEFAULT_ON_MODE, [], [], "두께 130, 윗면 흐림 65"),
        ("안개 높이", "h170", {"BaseThicknessCm": 170.0, "BaseTopBlurCm": 85.0}, DEFAULT_ON_MODE, [], [], "두께 170, 윗면 흐림 85"),
        ("안개 짙기", "dens04", {"BaseDensity": 0.4}, DEFAULT_ON_MODE, [], [], "BaseDensity 0.4"),
        ("안개 짙기", "dens16", {"BaseDensity": 1.6}, DEFAULT_ON_MODE, [], [], "BaseDensity 1.6"),
        ("다시 차오름", "refill015", {"FillRefillPerSec": 0.15}, DEFAULT_ON_MODE, [], [], "FillRefillPerSec 0.15"),
        ("다시 차오름", "refill06", {"FillRefillPerSec": 0.6}, DEFAULT_ON_MODE, [], [], "FillRefillPerSec 0.6"),
        ("밀린 것 되돌림", "disp_a", {"DisplacementRefillHorizontalPerSec": 0.03, "DisplacementRefillVerticalPerSec": 0.1}, DEFAULT_ON_MODE, [], [], "가로 0.03 / 세로 0.1"),
        ("밀린 것 되돌림", "disp_b", {"DisplacementRefillHorizontalPerSec": 0.3, "DisplacementRefillVerticalPerSec": 0.9}, DEFAULT_ON_MODE, [], [], "가로 0.3 / 세로 0.9"),
        ("바람", "wind_a", {"BodyWindStrength": 2.0, "BladeWindStrength": 2.0}, DEFAULT_ON_MODE, [], [], "몸·칼 바람 세기 2"),
        ("바람", "wind_b", {"WindRefillPerSec": 0.3}, DEFAULT_ON_MODE, [], [], "WindRefillPerSec 0.3"),
        ("칼", "blade_a", {"BladeErosionPerSec": 60.0}, DEFAULT_ON_MODE, [], [], "칼 깎기 60"),
        ("칼", "blade_b", {"BladeRadiusCm": 30.0}, DEFAULT_ON_MODE, [], [], "칼 반지름 30"),
        ("바람 없는 판", "mode1", {}, 1, [], [], "콘솔 TD.Fog3D.Mode 1"),
        ("엔진 안개", "eng_hist", {}, DEFAULT_ON_MODE, [f"{ENGINE_HISTORY_COMMAND} 0.7"], engine_restore[:1], "r.VolumetricFog.HistoryWeight 0.7"),
        ("엔진 안개", "eng_grid4", {}, DEFAULT_ON_MODE, [f"{ENGINE_GRID_COMMAND} 4"], engine_restore[1:], "r.VolumetricFog.GridPixelSize 4 (덧붙임 2절, 끝나면 8)"),
        ("기본값", "base_b", {}, DEFAULT_ON_MODE, [], [], "기본값 끝(같은 값이 다시 나오는지)"),
    ]
    return [make_specification(name, camera, light, on_mode, walk_shift, changes, console_extra, console_restore, group, description)
            for group, name, changes, on_mode, console_extra, console_restore, description in table]


def round2_specifications(light, grid_size_z):
    mid = {**MID_VALUES, **MID_STAIN}
    mid_without_stain = {**MID_VALUES, "bStainNoiseEnabled": False}
    thick = dict(THICK_FOG)
    table = [
        ("개념 증명", "top_hidden", "top", {}, {"hide_zone": True}, "구역 액터를 숨긴 장(Fv 의 기준)"),
        ("개념 증명", "T0", "top", {}, {}, "기본값"),
        ("개념 증명", "T1", "top", {"BaseDensity": 4.0}, {}, "BaseDensity 4"),
        ("개념 증명", "T2", "top", {"BaseDensity": 8.0}, {}, "BaseDensity 8"),
        ("개념 증명", "T3", "top", {"BaseDensity": 16.0}, {}, "BaseDensity 16"),
        ("개념 증명", "T4", "top", {"BaseDensity": 8.0, **thick}, {}, "BaseDensity 8 + 두께 150 / 윗면 흐림 120"),
        ("개념 증명", "T5", "top", {"BaseDensity": 16.0, **thick}, {}, "BaseDensity 16 + 두께 150 / 윗면 흐림 120"),
        ("개념 증명", "T6", "top", {"BaseDensity": 8.0, **thick, **mid_without_stain}, {}, "T4 + 중 값에서 얼룩을 뺀 것"),
        ("개념 증명", "T7", "top", {"BaseDensity": 16.0, **thick, **mid_without_stain}, {}, "T5 + 중 값에서 얼룩을 뺀 것"),
        ("개념 증명", "T8", "top", {"BaseDensity": 16.0, **thick, **mid}, {}, "T7 + 얼룩 0.6 / 400"),
        ("개념 증명 뒤 카메라", "T4_back", "back", {"BaseDensity": 8.0, **thick}, {}, "T4, 카메라 back"),
        ("개념 증명 뒤 카메라", "T7_back", "back", {"BaseDensity": 16.0, **thick, **mid_without_stain}, {}, "T7, 카메라 back"),
    ]
    if grid_size_z is not None:
        doubled = f"{GRID_SIZE_Z_COMMAND} {grid_size_z * 2:g}"
        restore = f"{GRID_SIZE_Z_COMMAND} {grid_size_z:g}"
        table.append(("개념 증명 엔진 칸", "T7_gridz", "top", {"BaseDensity": 16.0, **thick, **mid_without_stain}, {"console_extra": [doubled], "console_restore": [restore]}, f"T7, {GRID_SIZE_Z_COMMAND} {grid_size_z:g} → {grid_size_z * 2:g}(끝나면 되돌림)"))
    table += [
        ("치우침 진단", "A0", "back", dict(mid), {}, "중 값 그대로 +X 로 걷기"),
        ("치우침 진단", "A1", "back", {**mid, "BladeWindStrength": 0.0, "BladeErosionPerSec": 0.0}, {}, "중 값 + 칼 바람 0·칼 깎기 0"),
        ("치우침 진단", "A2", "back", dict(mid), {"walk_yaw": 180.0, "camera_yaw": 180.0}, "중 값, −X 로 걷기(걷는 방향 180, 카메라 방향 180)"),
        ("치우침 진단", "A3", "back", dict(mid), {"walk_yaw": 90.0, "camera_yaw": 90.0}, "중 값, +Y 로 걷기(걷는 방향 90, 카메라 방향 90)"),
        ("치우침 진단", "A4", "back", dict(mid), {"light": "side_flip"}, "중 값, 빛 반대쪽(해 높이 35도, 방향 280)"),
        ("구름·이어짐", "C1", "back", {**mid, "StainNoiseAmplitude": 0.75, "StainNoiseSpacingCm": 400.0}, {}, "중 값 + 얼룩 0.75 / 400"),
        ("구름·이어짐", "C2", "back", {**mid, "StainNoiseAmplitude": 0.75, "StainNoiseSpacingCm": 600.0}, {}, "중 값 + 얼룩 0.75 / 600"),
        ("구름·이어짐", "C3", "back", {**mid, "StainNoiseAmplitude": 0.85, "StainNoiseSpacingCm": 500.0}, {}, "중 값 + 얼룩 0.85 / 500"),
        ("구름·이어짐", "C4", "back", {**mid, "StainNoiseAmplitude": 0.75, "StainNoiseSpacingCm": 400.0, "BaseThicknessCm": 110.0, "BaseTopBlurCm": 70.0}, {}, "C1 + 두께 110 / 윗면 흐림 70"),
        ("구름·이어짐", "R1", "back", {**mid, "DisplacementRefillHorizontalPerSec": 0.05, "DisplacementRefillVerticalPerSec": 0.15}, {}, "중 값 + 밀린 것 되돌림 0.05 / 0.15"),
        ("구름·이어짐", "R2", "back", {**mid, "DisplacementRefillHorizontalPerSec": 0.05, "DisplacementRefillVerticalPerSec": 0.15, "WindRefillPerSec": 0.5}, {}, "R1 + WindRefillPerSec 0.5"),
        ("구름·이어짐 눈", "C1_top", "top", {**mid, "StainNoiseAmplitude": 0.75, "StainNoiseSpacingCm": 400.0}, {}, "C1, 카메라 top"),
        ("구름·이어짐 눈", "C3_top", "top", {**mid, "StainNoiseAmplitude": 0.85, "StainNoiseSpacingCm": 500.0}, {}, "C3, 카메라 top"),
        ("구름·이어짐 눈", "R1_top", "top", {**mid, "DisplacementRefillHorizontalPerSec": 0.05, "DisplacementRefillVerticalPerSec": 0.15}, {}, "R1, 카메라 top"),
    ]
    specifications = []
    for group, name, camera, changes, extra, description in table:
        specifications.append(make_specification(name, camera, extra.get("light", light), DEFAULT_ON_MODE, 0.0, changes, extra.get("console_extra", []), extra.get("console_restore", []), group, description,
                                                 camera_yaw=extra.get("camera_yaw"), walk_yaw=extra.get("walk_yaw", WALK_YAW_DEGREES), hide_zone=extra.get("hide_zone", False)))
    return specifications


def build_run_all_script(names, output_root):
    lines = [
        "$ErrorActionPreference = 'Continue'",
        f"Set-Location {ROOT.as_posix()}",
        "$env:PYTHONUTF8 = '1'",
    ]
    for name in names:
        commands = build_capture_commands(name, describe_run_directory(output_root, name))
        lines += [f"Write-Host '=== {name} ==='", commands["capture"], commands["snapshot_log"]]
    lines.append("python -B Tools/FluidFX/fog3d_calib_report.py --all --animate")
    return "\n".join(lines) + "\n"


def run_matrix(arguments, canonical, field_types, canonical_sha256):
    matrix_name = getattr(arguments, "matrix", None) or "explore"
    if matrix_name == "round2":
        specifications = round2_specifications(arguments.light, getattr(arguments, "grid_size_z", None))
        if getattr(arguments, "grid_size_z", None) is None:
            print("T7_gridz 는 --grid-size-z <현재값> 이 있어야 만든다(에디터에서 읽은 값)")
    else:
        specifications = explore_specifications(arguments.camera, arguments.light, arguments.walk_shift)
    only_names = list(getattr(arguments, "only", None) or [])
    if only_names:
        specifications = [specification for specification in specifications if specification["name"] in only_names]
    output_root = Path(arguments.out_root)
    index = {"matrix": matrix_name, "camera": arguments.camera, "light": arguments.light, "walk_shift_seconds": arguments.walk_shift, "runs": []}
    written_names = []
    for specification in specifications:
        outcome = materialize_run(specification, output_root, canonical, field_types, canonical_sha256, write_files=not arguments.dry_run)
        entry = {"name": specification["name"], "group": specification["group"], "description": specification["description"], "status": outcome["status"]}
        if outcome["status"] == "skipped":
            entry["reason"] = outcome["reason"]
        else:
            written_names.append(specification["name"])
        index["runs"].append(entry)
        print(f"{specification['name']:<10} {outcome['status']:<8} [{specification['group']}] {specification['description']}" + (f"  ({outcome['reason']})" if outcome["status"] == "skipped" else ""))
    print(f"실행 {len(written_names)}개 만듦, 건너뜀 {len(specifications) - len(written_names)}개" + (" (건식, 파일 안 씀)" if arguments.dry_run else ""))
    if not arguments.dry_run and matrix_name == "explore":
        write_json(output_root / "index.json", index)
        write_text(output_root / "run_all.ps1", build_run_all_script(written_names, output_root))
        print("색인", (output_root / "index.json").as_posix(), "/ 실행 순서", (output_root / "run_all.ps1").as_posix())
    if not arguments.dry_run and matrix_name == "round2" and not only_names:
        write_json(output_root / INDEX_FILE_NAMES["round2"], index)
        print("색인", (output_root / INDEX_FILE_NAMES["round2"]).as_posix())
    return 0


def run_single(arguments, canonical, field_types, canonical_sha256):
    changes = parse_assignments(arguments.set or [], field_types)
    walk_yaw = getattr(arguments, "walk_yaw", None)
    specification = make_specification(arguments.name, arguments.camera, arguments.light, arguments.on_mode, arguments.walk_shift, changes,
                                       arguments.console or [], arguments.console_restore or [], "", arguments.description or "", getattr(arguments, "dense_step", None),
                                       getattr(arguments, "camera_yaw", None), WALK_YAW_DEGREES if walk_yaw is None else walk_yaw, getattr(arguments, "hide_zone", False), getattr(arguments, "sun_yaw", None))
    outcome = materialize_run(specification, Path(arguments.out_root), canonical, field_types, canonical_sha256, write_files=not arguments.dry_run)
    if outcome["status"] == "skipped":
        print(outcome["reason"])
        return EXIT_INPUT_PROBLEM
    run_directory = Path(arguments.out_root) / specification["name"]
    print("만듦:", run_directory.as_posix(), "(variants.json, tuning.json, run.json)" if not arguments.dry_run else "(건식, 파일 안 씀)")
    print("바꾼 값:", json.dumps(outcome["record"]["changed_fields"], ensure_ascii=False))
    print("촬영 명령(PowerShell):", outcome["record"]["commands"]["capture"])
    return 0


def parse_preregistered_schedule(text):
    section = text.split("## 2.")[1].split("## 3.")[0]
    shots, events = [], []
    for line in section.splitlines():
        if not line.startswith("| ") or line.startswith("| 시각"):
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        times = [float(value) for value in re.split(r",\s*", cells[0])]
        action, shot = cells[1], cells[2]
        for time_value in times:
            if "휘두르기" in action:
                events.append(("swing", time_value))
            elif "계산 끔" in action:
                events.append(("compute_off", time_value))
            elif "계산 켬" in action:
                events.append(("compute_on", time_value))
            elif "걷기 시작" in action:
                events.append(("walk", time_value))
            if shot:
                role = "moving" if ("움직이는" in shot or "걷는 중" in shot) else "stopped" if "멈춘" in shot else "reference" if "기준" in shot else "?"
                shots.append((shot[0], time_value, role))
    return shots, events


def parse_preregistered_level_counts(text):
    section = text.split("## 4.")[1].split("## 5.")[0]
    counts = {}
    for line in section.splitlines():
        if not line.startswith("| ") or line.startswith("| 묶음"):
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        match = re.search(r"(\d+)벌", cells[2])
        counts[cells[0]] = int(match.group(1)) if match else len([part for part in re.split(r",\s+", cells[2]) if part])
    return counts


def check(condition, label, failures):
    if not condition:
        failures.append(label)
    print(("통과 " if condition else "실패 ") + label)


def run_self_test():
    failures = []
    header_types = parse_settings_header(SETTINGS_HEADER_PATH.read_text(encoding="utf-8"))
    canonical = load_canonical_tuning()
    canonical_settings = canonical["Globals"][SETTINGS_GROUP_NAME]
    canonical_sha256 = sha256_of_file(CANONICAL_TUNING_PATH)
    check(set(header_types) == set(canonical_settings), f"헤더 필드 {len(header_types)}개 = 정본 JSON 필드 {len(canonical_settings)}개", failures)
    check(predict_value_problems(canonical_settings) == [], "정본 값은 예측 값 검사를 통과", failures)
    bad_settings = dict(canonical_settings, BaseThicknessCm=20.0)
    check(len(predict_value_problems(bad_settings)) == 1, "BaseThicknessCm 20 < BaseTopBlurCm 45 는 값 검사 걸림으로 예측", failures)
    prereg_text = PREREGISTRATION_PATH.read_text(encoding="utf-8")
    expected_shots, expected_events = parse_preregistered_schedule(prereg_text)
    generated_shots = [(entry["group"], entry["time"], entry["role"]) for entry in build_shot_table(0.0)]
    check(sorted(expected_shots) == sorted(generated_shots) and len(expected_shots) == 19, f"장 {len(generated_shots)}개의 시각·역할이 등록본 2절 표와 같다", failures)
    kind_map = {"swing": "swing", "swing_compute_off": "compute_off", "walk_compute_off": "compute_off", "compute_on": "compute_on", "walk": "walk"}
    generated_events = [(kind_map[record["kind"]], record["time"]) for record in build_event_records(2, 0.0, [])]
    check(sorted(expected_events) == sorted(generated_events), "이벤트 시각이 등록본 2절 표와 같다", failures)
    shifted_shots = build_shot_table(0.4)
    check([entry["time"] for entry in shifted_shots if entry["group"] == "S"] == [entry[1] for entry in SWING_SHOT_TABLE], "걷기 미룸이 S 장 시각을 바꾸지 않는다", failures)
    check([entry["time"] for entry in shifted_shots if entry["group"] == "W"][0] == 10.4 and shifted_shots[-1]["time"] == 20.3, "걷기 미룸 0.4 가 W 장 시각을 함께 민다", failures)
    shifted_events = {record["kind"]: record["time"] for record in build_event_records(2, 0.4, ["x 1"])}
    check(shifted_events["walk"] == 10.0 and shifted_events["walk_compute_off"] == 18.8 and shifted_events["compute_on"] == 8.9 and shifted_events["restore"] == 20.9, "걷기 시작·끔 이벤트가 밀리고 S 쪽 이벤트는 그대로, 복구는 마지막 장 0.6초 뒤", failures)
    check(build_event_records(1, 0.0, [])[2]["console"] == ["TD.Fog3D.Mode 1"], "--on-mode 1 이 계산 켬 명령을 바꾼다", failures)
    stage_text = STAGE_SCRIPT_PATH.read_text(encoding="utf-8")
    check('LABEL_PREFIX = "TDGen_"' in stage_text and '"Fog3D_Sun"' in stage_text and SUN_LABEL == "TDGen_Fog3D_Sun", "해 액터 라벨이 무대 스크립트와 같다", failures)
    check("SUN_ROTATION_DEGREES" in stage_text and '"pitch": -35.0' in stage_text and '"yaw": 100.0' in stage_text, "무대의 해가 지금 옆빛(높이 35도·방향 100)임을 무대 스크립트에서 확인", failures)
    check(LIGHT_ROTATIONS["orig"] == [-18.0, 160.0, 0.0] and build_light_props("orig")[SUN_LABEL]["root_component"]["relative_rotation"] == [-18.0, 160.0, 0.0], "--light orig 는 옛 역광(높이 18도·방향 160)을 직접 넣는다", failures)

    expected_counts = parse_preregistered_level_counts(prereg_text)
    with tempfile.TemporaryDirectory() as directory:
        arguments = argparse.Namespace(camera="back", light="orig", walk_shift=0.0, out_root=directory, dry_run=False)
        specifications = explore_specifications("back", "orig", 0.0)
        names = [specification["name"] for specification in specifications]
        check(len(set(names)) == len(names), f"실행 이름 {len(names)}개가 겹치지 않는다", failures)
        group_counts = {}
        for specification in specifications:
            group_counts[specification["group"]] = group_counts.get(specification["group"], 0) + 1
        explore_counts = {group: count for group, count in group_counts.items() if group != "기본값"}
        addendum_counts = dict(expected_counts)
        addendum_counts["잔무늬"] = addendum_counts["잔무늬"] + 3
        check(explore_counts == addendum_counts, f"탐색 묶음별 실행 수가 등록본 4절 표 + 덧붙임 2절(잔무늬 +3, 엔진 안개 -1+1)과 같다 {explore_counts}", failures)
        check(group_counts["기본값"] == 2 and sum(explore_counts.values()) == 31 and len(specifications) == 33, "기본값 2 + 탐색 31 = 33", failures)
        check(run_matrix(arguments, canonical, header_types, canonical_sha256) == 0, "--matrix explore 가 파일을 만든다", failures)
        index = json.loads((Path(directory) / "index.json").read_text(encoding="utf-8"))
        check(all(entry["status"] == "written" for entry in index["runs"]), "건너뛴 실행 0개(값 검사 예측)", failures)
        key_problem_total = 0
        tuning_problem_total = 0
        for name in names:
            run_directory = Path(directory) / name
            variants = json.loads((run_directory / "variants.json").read_text(encoding="utf-8"))
            record = json.loads((run_directory / "run.json").read_text(encoding="utf-8"))
            tuning = json.loads((run_directory / "tuning.json").read_text(encoding="utf-8"))
            key_problem_total += len(find_variant_key_problems(variants))
            expected_tuning = copy.deepcopy(canonical)
            for field, change in record["changed_fields"].items():
                expected_tuning["Globals"][SETTINGS_GROUP_NAME][field] = change["to"]
            differing = [field for field in canonical_settings if tuning["Globals"][SETTINGS_GROUP_NAME][field] != canonical_settings[field]]
            if tuning != expected_tuning or sorted(differing) != sorted(field for field, change in record["changed_fields"].items() if change["from"] != change["to"]):
                tuning_problem_total += 1
        check(key_problem_total == 0, "만든 변형 전부 find_key_errors 키 오류 0", failures)
        check(tuning_problem_total == 0, "조절 사본이 정본과 지정 필드만 다르다(33개 전부)", failures)
        base_run = json.loads((Path(directory) / "base_a" / "tuning.json").read_text(encoding="utf-8"))
        check(base_run == canonical, "기본값 실행의 조절 사본은 정본과 같다", failures)
        engine_variants = json.loads((Path(directory) / "eng_grid4" / "variants.json").read_text(encoding="utf-8"))
        engine_events = engine_variants["variants"][0]["apply"]["schedule"]["events"]
        check("r.VolumetricFog.GridPixelSize 4" in engine_variants["base"]["console"] and engine_events[-1]["console"] == ["r.VolumetricFog.GridPixelSize 8"], "엔진 안개 실행은 끝에 원래 값으로 되돌리는 이벤트를 가진다", failures)
        light_arguments = argparse.Namespace(name="t_light", set=["BodyErosionPerSec=12"], camera="high", light="side", on_mode=2, walk_shift=0.2, console=None, console_restore=None, out_root=directory, dry_run=False, description="")
        check(run_single(light_arguments, canonical, header_types, canonical_sha256) == 0, "단일 실행 만들기(높은 카메라·옆빛·값 하나)", failures)
        light_variants = json.loads((Path(directory) / "t_light" / "variants.json").read_text(encoding="utf-8"))
        check(light_variants["base"]["arm"] == 700 and light_variants["base"]["view"] == [-35, 30] and SUN_LABEL in light_variants["base"]["props"], "카메라 high 팔 700·[-35, 30], 옆빛 props", failures)
        check(find_variant_key_problems(light_variants) == [], "단일 실행 변형 키 오류 0", failures)
    dense_fine = build_dense_shot_table(0.1)
    dense_coarse = build_dense_shot_table(0.2)
    check(len([entry for entry in dense_fine if entry["group"] == "S"]) == 61 and len([entry for entry in dense_fine if entry["group"] == "W"]) == 71 and len(dense_fine) == 132, "촘촘한 시각표 0.1초: S 61장 + W 71장", failures)
    check(len([entry for entry in dense_coarse if entry["group"] == "S"]) == 31 and len([entry for entry in dense_coarse if entry["group"] == "W"]) == 36, "촘촘한 시각표 0.2초: S 31장 + W 36장", failures)
    check(dense_fine[0]["time"] == 0.0 and dense_fine[60]["time"] == 6.0 and dense_fine[61]["time"] == 7.3 and dense_fine[-1]["time"] == 14.3 and len({entry["id"] for entry in dense_fine}) == 132, "촘촘한 시각표 시작·끝 시각과 장 이름 겹침 없음", failures)
    check([(record["kind"], record["time"]) for record in build_dense_event_records(2, [])] == [("swing", 0.0), ("swing_compute_off", 6.3), ("compute_on", 6.6), ("walk", 7.3)], "촘촘한 이벤트: 0.0 휘두르기, 6.3 끔, 6.6 켬, 7.3 걷기", failures)
    with tempfile.TemporaryDirectory() as dense_directory:
        dense_arguments = argparse.Namespace(name="t_dense", set=["FootRadiusCm=45"], camera="high", light="side", on_mode=2, walk_shift=0.0, console=None, console_restore=None, out_root=dense_directory, dry_run=False, description="", dense_step=0.1)
        check(run_single(dense_arguments, canonical, header_types, canonical_sha256) == 0, "촘촘한 실행 만들기", failures)
        dense_variants = json.loads((Path(dense_directory) / "t_dense" / "variants.json").read_text(encoding="utf-8"))
        dense_record = json.loads((Path(dense_directory) / "t_dense" / "run.json").read_text(encoding="utf-8"))
        dense_schedule = dense_variants["variants"][0]["apply"]["schedule"]
        check(len(dense_schedule["times"]) == 132 and len(dense_record["shots"]) == 132 and dense_record["valid_conditions"]["dense"] is True, "촘촘한 실행의 장 132개·유효 조건 표식", failures)
        check(find_variant_key_problems(dense_variants) == [], "촘촘한 실행 변형 키 오류 0", failures)
        try:
            validate_specification(make_specification("t_bad", "back", "side", 2, 0.4, dense_step=0.1))
            check(False, "촘촘한 시각표와 걷기 미룸 함께 쓰기 거부", failures)
        except InputProblem:
            check(True, "촘촘한 시각표와 걷기 미룸 함께 쓰기 거부", failures)
    check(CAMERAS["top"] == (800, [-60, 0]) and LIGHT_ROTATIONS["side_flip"] == [-35.0, 280.0, 0.0], "카메라 top 은 팔 800·[-60, 0], 빛 side_flip 은 높이 35도·방향 280", failures)
    stage_script_text = STAGE_SCRIPT_PATH.read_text(encoding="utf-8")
    check("PLAYER_START_CM = (-1000.0, 0.0, 100.0)" in stage_script_text and "ZONE_HALF_EXTENT_CM = (3000.0, 3000.0, 150.0)" in stage_script_text, "걷기 시작점·구역 반크기 상수가 무대 스크립트와 같다", failures)
    check('"center": (500.0, -400.0), "size": (150.0, 150.0, 30.0)' in stage_script_text and '"center": (500.0, 400.0), "size": (150.0, 150.0, 180.0)' in stage_script_text and '"center": (1100.0, 0.0), "size": (10.0, 300.0, 250.0)' in stage_script_text, "물체 상자 둘·벽 위치와 크기가 무대 스크립트와 같다", failures)
    check(all(walk_path_problems(yaw) == [] for yaw in (0.0, 90.0, 180.0)), "걷기 방향 0·90·180 은 끝이 구역 안·물체 밖", failures)
    check(len(walk_path_problems(0.0, 2000.0)) >= 1 and len(walk_path_problems(180.0, 2500.0)) >= 1, "너무 멀리 걷거나 구역 끝에 가면 걷기 길 점검에 걸린다", failures)
    round2 = round2_specifications("side", 64.0)
    round2_names = [specification["name"] for specification in round2]
    check(len(round2) == 27 and len(set(round2_names)) == 27 and len(round2_specifications("side", None)) == 26, "round2 27실행(--grid-size-z 없으면 26)·이름 겹침 없음", failures)
    with tempfile.TemporaryDirectory() as round2_directory:
        round2_arguments = argparse.Namespace(matrix="round2", camera="back", light="side", walk_shift=0.0, out_root=round2_directory, dry_run=False, grid_size_z=64.0, only=None)
        check(run_matrix(round2_arguments, canonical, header_types, canonical_sha256) == 0 and (Path(round2_directory) / INDEX_FILE_NAMES["round2"]).is_file() and not (Path(round2_directory) / "index.json").exists(), "--matrix round2 가 index_round2.json 만 쓴다(index.json 은 건드리지 않는다)", failures)
        loaded = {}
        for name in round2_names:
            loaded[name] = {"variants": json.loads((Path(round2_directory) / name / "variants.json").read_text(encoding="utf-8")), "record": json.loads((Path(round2_directory) / name / "run.json").read_text(encoding="utf-8")),
                            "tuning": json.loads((Path(round2_directory) / name / "tuning.json").read_text(encoding="utf-8"))}
        check(sum(find_variant_key_problems(entry["variants"]) != [] for entry in loaded.values()) == 0, "round2 변형 전부 find_key_errors 키 오류 0", failures)
        settings_of = lambda name: loaded[name]["tuning"]["Globals"][SETTINGS_GROUP_NAME]
        check(loaded["top_hidden"]["variants"]["base"]["hide"] == {ZONE_LABEL: True} and all("hide" not in entry["variants"]["base"] for name, entry in loaded.items() if name != "top_hidden"), "구역 숨김은 top_hidden 하나만", failures)
        top_names = ("top_hidden", "T0", "T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8", "T7_gridz", "C1_top", "C3_top", "R1_top")
        check(all(loaded[name]["variants"]["base"]["arm"] == 800 and loaded[name]["variants"]["base"]["view"] == [-60, 0] for name in top_names), "top 카메라 실행 14개는 팔 800·[-60, 0]", failures)
        check(all(loaded[name]["variants"]["base"]["arm"] == 400 and loaded[name]["variants"]["base"]["view"][0] == -12 for name in ("T4_back", "T7_back", "A0", "A1", "C1", "C2", "C3", "C4", "R1", "R2")), "back 카메라 실행은 팔 400·내려다봄 12도", failures)
        check(settings_of("T4")["BaseDensity"] == 8.0 and settings_of("T4")["BaseThicknessCm"] == 150.0 and settings_of("T4")["BaseTopBlurCm"] == 120.0 and settings_of("T4")["FootRadiusCm"] == canonical_settings["FootRadiusCm"], "T4 = 짙기 8 + 두께 150/흐림 120(몸 값은 기본)", failures)
        check(settings_of("T7")["BaseDensity"] == 16.0 and settings_of("T7")["FootRadiusCm"] == 45.0 and settings_of("T7")["PelvisRadiusCm"] == 60.0 and settings_of("T7")["BodyWindStrength"] == 2.0 and settings_of("T7")["BladeRadiusCm"] == 30.0 and settings_of("T7")["bStainNoiseEnabled"] is False, "T7 = T5 + 중 값에서 얼룩을 뺀 것", failures)
        check(settings_of("T8")["bStainNoiseEnabled"] is True and settings_of("T8")["StainNoiseAmplitude"] == 0.6 and settings_of("T8")["StainNoiseSpacingCm"] == 400.0, "T8 = T7 + 얼룩 0.6 / 400", failures)
        s3_file = CALIBRATION_ROOT / "s3" / "run.json"
        if s3_file.is_file():
            s3_changes = {field: change["to"] for field, change in json.loads(s3_file.read_text(encoding="utf-8"))["changed_fields"].items()}
            check(s3_changes == {field: change["to"] for field, change in loaded["A0"]["record"]["changed_fields"].items()}, "A0 의 바뀐 값이 조합 탐색의 s3 와 완전히 같다", failures)
        check(settings_of("A1")["BladeWindStrength"] == 0.0 and settings_of("A1")["BladeErosionPerSec"] == 0.0, "A1 = 칼 바람 0·칼 깎기 0", failures)
        a2_events = loaded["A2"]["variants"]["variants"][0]["apply"]["schedule"]["events"]
        a3_events = loaded["A3"]["variants"]["variants"][0]["apply"]["schedule"]["events"]
        check(loaded["A2"]["variants"]["base"]["view"] == [-12, 180.0] and [event["move"]["yaw"] for event in a2_events if "move" in event] == [180.0] and loaded["A3"]["variants"]["base"]["view"] == [-12, 90.0] and [event["move"]["yaw"] for event in a3_events if "move" in event] == [90.0], "A2·A3 은 걷는 방향과 카메라 방향이 같다(180·90)", failures)
        check(loaded["A4"]["record"]["light"] == "side_flip" and loaded["A4"]["variants"]["base"]["props"][SUN_LABEL]["root_component"]["relative_rotation"] == [-35.0, 280.0, 0.0] and loaded["A0"]["record"]["light"] == "side", "A4 만 빛 side_flip, 나머지는 옆빛", failures)
        check(settings_of("C4")["BaseThicknessCm"] == 110.0 and settings_of("C4")["BaseTopBlurCm"] == 70.0 and settings_of("C4")["StainNoiseAmplitude"] == 0.75 and settings_of("C2")["StainNoiseSpacingCm"] == 600.0 and settings_of("C3")["StainNoiseAmplitude"] == 0.85, "C1~C4 값이 등록본 3절 표와 같다", failures)
        check(settings_of("R1")["DisplacementRefillHorizontalPerSec"] == 0.05 and settings_of("R1")["DisplacementRefillVerticalPerSec"] == 0.15 and settings_of("R2")["WindRefillPerSec"] == 0.5 and settings_of("R2")["DisplacementRefillVerticalPerSec"] == 0.15, "R1·R2 값이 등록본 3절 표와 같다", failures)
        gridz_events = loaded["T7_gridz"]["variants"]["variants"][0]["apply"]["schedule"]["events"]
        check(f"{GRID_SIZE_Z_COMMAND} 128" in loaded["T7_gridz"]["variants"]["base"]["console"] and gridz_events[-1]["console"] == [f"{GRID_SIZE_Z_COMMAND} 64"], "T7_gridz 는 칸 수를 2배로 올리고 끝에 되돌린다", failures)
        check(loaded["T7_gridz"]["tuning"] == loaded["T7"]["tuning"], "T7_gridz 의 조절 값은 T7 과 같다", failures)
        check(loaded["T0"]["tuning"] == canonical and loaded["top_hidden"]["tuning"] == canonical, "T0·top_hidden 은 정본 기본값", failures)
    for bad_assignment in ("NoSuchField=1", "BaseDensity", "BladeSocketName=x", "bStainNoiseEnabled=maybe", "MaximumObstacleCount=2.5"):
        try:
            parse_assignments([bad_assignment], header_types)
            check(False, f"잘못된 --set 거부: {bad_assignment}", failures)
        except InputProblem:
            check(True, f"잘못된 --set 거부: {bad_assignment}", failures)
    print("자체 점검", "통과" if not failures else "실패 " + str(len(failures)) + "건")
    return 0 if not failures else EXIT_FAILED


def parse_arguments(argument_list):
    parser = argparse.ArgumentParser(description="3D 안개 1c 실행 변형 만들기")
    parser.add_argument("--name", help="실행 이름(영문·숫자·밑줄)")
    parser.add_argument("--set", nargs="+", action="extend", help="필드=값 여러 개(헤더에 있는 bool·int32·float 필드만)")
    parser.add_argument("--camera", choices=list(CAMERAS), default="back")
    parser.add_argument("--light", choices=list(LIGHT_ROTATIONS), default="orig")
    parser.add_argument("--on-mode", type=int, default=DEFAULT_ON_MODE, help="계산 켬에 쓰는 TD.Fog3D.Mode 값(기본 2, 바람 없는 판은 1)")
    parser.add_argument("--walk-shift", type=float, default=0.0, help="걷기 시작 이후 시각을 함께 미루는 초")
    parser.add_argument("--dense-step", type=float, default=None, help="촘촘한 시각표(휘두르기 0~6초, 걷기 7.3~14.3초)의 간격 0.1 또는 0.2초, 기준 장 없음")
    parser.add_argument("--console", action="append", help="촬영 시작 전 base 에서 실행할 콘솔 명령(여러 번 가능)")
    parser.add_argument("--console-restore", action="append", help="마지막 장 뒤에 실행할 되돌리기 콘솔 명령(여러 번 가능)")
    parser.add_argument("--description", help="run.json 에 적을 설명")
    parser.add_argument("--camera-yaw", type=float, default=None, help="카메라 방향(도), 없으면 카메라 이름의 기본 방향")
    parser.add_argument("--walk-yaw", type=float, default=None, help="걷는 방향(도), 기본 0(+X). 시작점 (-1000, 0)에서 600cm 걷고 끝이 구역 안·물체 밖이어야 한다")
    parser.add_argument("--sun-yaw", type=float, default=None, help="해 방향(도)을 숫자로 준다(높이 -35도 고정). 없으면 --light 이름의 값")
    parser.add_argument("--hide-zone", action="store_true", help="구역 액터를 게임에서 숨긴다(구역 숨김 장)")
    parser.add_argument("--grid-size-z", type=float, default=None, help="--matrix round2 의 T7_gridz 용: 에디터에서 읽은 r.VolumetricFog.GridSizeZ 현재 값(2배로 올리고 끝나면 되돌린다)")
    parser.add_argument("--only", nargs="+", default=None, help="--matrix 에서 이 이름들만 만든다(색인은 안 쓴다)")
    parser.add_argument("--matrix", choices=["explore", "round2"], help="explore: 등록본 4절 탐색 실행 전부(기본값 2 포함) / round2: 2회차 등록본 27실행(옆빛은 --light side)")
    parser.add_argument("--out-root", default=str(CALIBRATION_ROOT))
    parser.add_argument("--dry-run", action="store_true", help="파일을 쓰지 않고 목록과 점검만")
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args(argument_list)


def main(argument_list=None):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    arguments = parse_arguments(argument_list)
    if arguments.self_test:
        return run_self_test()
    try:
        field_types = parse_settings_header(SETTINGS_HEADER_PATH.read_text(encoding="utf-8"))
        canonical = load_canonical_tuning()
        canonical_sha256 = sha256_of_file(CANONICAL_TUNING_PATH)
        if arguments.matrix:
            if arguments.name or arguments.set or arguments.console or arguments.console_restore:
                raise InputProblem("--matrix 는 --name·--set·--console 과 함께 쓰지 않는다")
            if arguments.matrix == "explore" and (arguments.only or arguments.grid_size_z is not None):
                raise InputProblem("--only·--grid-size-z 는 --matrix round2 에서만 쓴다")
            return run_matrix(arguments, canonical, field_types, canonical_sha256)
        if not arguments.name:
            raise InputProblem("--name 또는 --matrix 또는 --self-test 가 필요하다")
        return run_single(arguments, canonical, field_types, canonical_sha256)
    except (InputProblem, OSError, ValueError) as error:
        print("입력 문제:", error)
        return EXIT_INPUT_PROBLEM


if __name__ == "__main__":
    sys.exit(main())
