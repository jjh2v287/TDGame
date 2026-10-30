"""실행 환경: 시스템 Python 3(실제 실행은 언리얼 에디터가 TDGame을 열고 MCP 포트 8000·원격 실행이 켜진 상태, 에디터 앞창 유지 L-editor-06. 건식·offline 단독 장면은 에디터 불필요). 계획 JSON v2(spec-cal0-v2 C1)를 허용 키 표로 검사하고, 덧씌움 → base_tuning → 변형 순으로 fluid_tuning_patch 사본을 만든 뒤 장면 sources(look·probe·water_probe·offline)마다 pie_lookdev_fluid 룩데브(tuning_reload base·read_back owner/access·look_live·props·probe·schedule), 물 탐침, offline 명령(같은 사본 2회 결정론 확인)을 돌려 단계 유효성을 단언한다. 단계 종류 frame(v00 1)·screen(손잡이 끝 값, 대조 ≥3)·confirm(v00,best,v00,second|best,v00)·verify(v00 3), 장면 shots → schedule{times: settle+k·interval, reference now, fixed_fps 60}. --stage 없으면 옛 스윕(반복·control_every·control_middle).
실행 명령: python Tools/FluidFX/calib_sweep.py --plan Tools/FluidFX/calib/plans/<id>.json [--stage frame|screen|confirm|verify] [--lock <잠금 v2>] [--overlay Saved/FluidFX/calib/accepted/overlay.json] [--screen-result <screen.json>] [--dry-run] [--run-dir Saved/FluidFX/calib/<run>] [--assert-scene <장면>=<result.json>] (PowerShell, L-repo-01). screen·confirm·verify 실제 실행은 --lock 필수, confirm 은 --screen-result 필수(plan·overlay·정본 2·lock sha 대조). --dry-run 은 검사·사본·룩데브 변형 JSON·sweep.json 만 만든다
출력: <run-dir>/v<k>/(조절·모양 사본, variant.json), <run-dir>/<장면>/lookdev_spec.json·룩데브 출력(result.json·png)·water_probe.json·<단계>_offline*.json, <run-dir>/sweep.json(version 2·stage·lock·lock_sha256·overlay·overlay_sha256·screen_result·base_copy·canonical_tuning/look_sha256·started_at·warnings, 장면별 sequence·sources·shots_expected, 단계별 images·image_sha256·shots_saved·shot_game_times·metrics·look_applied·look_read_back·owner_checks(룩데브 apply 최상위)·valid·invalid_reasons, props 결과가 목록 아님(액터 없음 등)이면 무효, 물 탐침 단계는 Look 미적용 look_live_skipped 와 장면 overlay_props(덧씌움 actor_props) 적용, 덧씌움·base_tuning Look 패키지(손잡이 패키지 아님) 중 levels 에 이 장면 수준이 없는 것은 look_live.skip_if_no_owner 로 보내 소유자가 없으면 적용·다시 읽기 단언을 건너뛰고 단계 skipped_look 에 기록, 변형 base_look_levels, 손잡이 Look 패키지라도 이 장면 손잡이 없이 param scenes 가 다른 장면인 손잡이만 가진 것은 장면 out_of_scene_look_packages 로 기록하고 같은 skip_if_no_owner 로 보낸다). 종료 코드 0 모든 단계 유효·정본 불변 / 1 무효 단계·룩데브 실패·정본 바뀜·confirm 거부(screen 둔감·best 없음) / 2 계획·잠금·덧씌움·screen.json 입력 오류
상태: 오프라인 하네스(가짜 unreal·가짜 룩데브, calib_offline_test.py check_cal0_sweep)만 검사, 실제 PIE 단계 스윕 미실행(2026-10-30 CAL-0 B2). 룩데브 적용 키 검사 find_key_errors(apply)·tuning_reload base·look_live·probe 결과 형식은 B3, Look·Globals 사본은 B1, 잠금 v2 생성은 B4b 병합 뒤 확인.
"""
import argparse
import copy
import hashlib
import itertools
import json
import re
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools"))
sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))
import check_fluid_preset_data  # noqa: E402
import fluid_tuning_patch  # noqa: E402

LOOKDEV_SCRIPT = ROOT / "Tools" / "FluidFX" / "pie_lookdev_fluid.py"
CALIB_OUTPUT_ROOT = ROOT / "Saved" / "FluidFX" / "calib"
SWEEP_FILE_NAME = "sweep.json"
LOOKDEV_SPEC_NAME = "lookdev_spec.json"
LOOKDEV_RESULT_NAME = "result.json"
WATER_PROBE_FILE_NAME = "water_probe.json"
OFFLINE_OUTPUT_NAME = "offline.json"
PLAN_KEYS = ("id", "scenes", "params", "mode", "repeats", "control_every", "reset", "metrics")
PLAN_OPTIONAL_KEYS = ("control_middle", "base_tuning", "base_tuning_ships", "note", "spec_source")
PLAN_RETIRED_KEYS = ("draft_status", "p3_gaps")
SCENE_KEYS = ("name", "level", "camera", "settle")
SCENE_OPTIONAL_KEYS = ("time_dilation", "schedule", "probe_window_scale", "live2_refs", "regions_template", "targets", "backend", "sources", "shots", "step_apply", "probe", "offline")
SCENE_MOVED_KEYS = {"awake": "camera.awake 로 옮긴다", "source": "sources 목록으로 쓴다"}
PARAM_PATH_KEYS = ("json_path", "json_paths", "actor_prop")
PARAM_OPTIONAL_KEYS = ("split_group", "base", "scenes", "cost")
ACTOR_PROP_KEYS = ("label", "property")
ACTOR_PROP_OPTIONAL_KEYS = ("component",)
ASSIGNMENT_KEYS = ("json_path", "value")
ASSIGNMENT_OPTIONAL_KEYS = ("split_group", "base")
METRICS_KEYS = ("sources", "targets")
METRICS_OPTIONAL_KEYS = ("noise_floor", "derived")
TARGET_KEYS = ("metric", "weight")
TARGET_OPTIONAL_KEYS = ("tolerance", "scene", "low", "high", "relative_to_control", "role", "basis", "note")
SHOTS_KEYS = ("count", "interval")
SHOTS_OPTIONAL_KEYS = ("kind",)
OFFLINE_KEYS = ("command",)
OFFLINE_OPTIONAL_KEYS = ("output",)
OFFLINE_PLACEHOLDERS = ("{tuning}", "{output}")
MODES = ("one_at_a_time", "grid")
RESET_MODES = ("reinit", "keep")
METRIC_SOURCES = ("look", "water_probe", "probe", "offline")
SHOT_KINDS = ("replicate", "series")
TARGET_ROLES = ("score", "constraint")
COST_DIRECTIONS = ("higher_costs_more", "lower_costs_more")
DERIVED_OPERATIONS = ("ratio", "difference", "relative_step", "half_life", "e_fold", "slope", "extent")
STAGES = ("frame", "screen", "confirm", "verify")
LOCKED_STAGES = ("screen", "confirm", "verify")
MIN_STAGE_SAMPLES = 3
MIN_CONTROL_STEPS = 3
VERIFY_STEP_COUNT = 3
SCHEDULE_FIXED_FPS = 60
LOCK_VERSION = 2
OVERLAY_VERSION = 2
SCREEN_RESULT_VERSION = 2
LOOK_PREFIX = "Look:"
LOOK_ROOTS = {"Materials": ("materials", ("Scalars", "Vectors")), "NiagaraSystems": ("niagara", ("Floats", "Ints", "Bools", "Vectors2", "Vectors3", "Colors"))}
GOLDEN_INPUT_PREFIX = "StructDefaults.TDFluidSimulationConfig."
SERIES_METRIC_PATTERN = re.compile(r"^(\d+\.|derived\.)")
OUTPUT_TAIL_CHARACTERS = 600
READ_BACK_TABLE = {
    "TDFluidSimulationConfig": {"owner": "live_component", "access": "simulation_config"},
    "TDFluidSourceEmitterSettings": {"owner": "actor", "access": "source_emitter"},
    "TDFluidDownstreamFeedSettings": {"owner": "actor", "access": "downstream_feed"},
    "TDFluidFlameCardLayerSettings": {"owner": "actor", "access": "flame_card_layers"},
    "TDFluidRainSettings": {"owner": "live_component", "access": "rain_settings"},
    "TDFluidRuntimeOpsSettings": {"owner": "live_component", "access": "call:get_effective_runtime_ops_settings"},
    "TDFluidWaterPropSettings": {"owner": "actor", "access": "call:get_water_prop_settings"},
    "TDFluidInputBrushSettings": {"owner": "live_component", "access": "call:get_effective_input_brush_settings"},
    "TDFluidFieldInputSettings": {"owner": "live_component", "access": "call:get_effective_field_input_settings"},
    "TDFluidSurfaceRenderSettings": {"owner": "live_component", "access": "call:get_effective_surface_render_settings"},
    "TDFluidWaterNearRippleSettings": {"owner": "live_component", "access": "call:get_effective_near_ripple_settings"},
    "TDFluidVolumeDriverSettings": {"owner": "volume_driver", "access": "call:get_effective_volume_driver_settings"},
    "TDFluidTerrainHeightSettings": {"owner": "actor", "access": "call:get_effective_terrain_height_settings"},
    "TDFluidWaterParticleSettings": {"owner": "actor_class:TDFluidWaterParticleActor", "access": "call:get_effective_water_particle_settings"},
    "TDFluidUnderwaterSettings": {"owner": "subsystem:TDFluidUnderwaterSubsystem", "access": "call:get_effective_underwater_settings"},
    "TDFluidCloudSceneSettings": {"owner": "actor_class:TDFluidCloudLayersActor", "access": "call:get_effective_cloud_scene_settings"},
    "TDFluidOutputQualitySettings": {"owner": "subsystem:TDFluidPresetTuningSubsystem", "access": "call:get_effective_output_quality_settings"},
    "TDFluidHeterogeneousVolumeRenderProfile": {"owner": "subsystem:TDFluidPresetTuningSubsystem", "access": "call:get_effective_heterogeneous_volume_render_profile"},
}
READ_BACK_STRUCTS = tuple(READ_BACK_TABLE)
CONTROL_ID = "v00"
ID_PATTERN = re.compile(r"^[A-Za-z0-9_-]+$")
FLOAT_RELATIVE_TOLERANCE = 1e-6
FLOAT_ABSOLUTE_TOLERANCE = 1e-7
EXIT_INVALID = 1
EXIT_INPUT = 2
EMPTY_BASE_LAYER = {"assignments": [], "expect": [], "look_live": {}, "look_levels": {}}


class PlanError(Exception):
    pass


class ConfirmRefused(Exception):
    pass


def file_sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def text_sha256(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def project_path(text):
    path = Path(text)
    return path if path.is_absolute() else ROOT / path


def relative_text(path):
    resolved = Path(path).resolve()
    try:
        return resolved.relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return resolved.as_posix()


def require(condition, message):
    if not condition:
        raise PlanError(message)


def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def check_keys(value, path, required, optional=(), moved=None):
    require(isinstance(value, dict), f"{path}: 객체여야 한다")
    for key, hint in (moved or {}).items():
        require(key not in value, f"{path}.{key}: 받지 않는 키({hint})")
    missing = [key for key in required if key not in value]
    require(not missing, f"{path}: 필수 키 없음: {', '.join(missing)}")
    unknown = sorted(key for key in value if key not in required and key not in optional)
    require(not unknown, f"{path}: 모르는 키 {', '.join(unknown)}")


def parse_look_path(json_path):
    parts = json_path[len(LOOK_PREFIX):].split(".", 3)
    require(len(parts) == 4 and all(parts), f"{json_path}: Look:<Materials|NiagaraSystems>.<패키지>.<구역>.<이름> 이어야 한다")
    root, package, section, name = parts
    require(root in LOOK_ROOTS, f"{json_path}: Look 뿌리는 {'|'.join(LOOK_ROOTS)} 만")
    require(package.startswith("/Game/"), f"{json_path}: 패키지는 /Game/ 로 시작하고 . 이 없어야 한다")
    require(section != "Switches", f"{json_path}: Switches 는 실행 중에 바꿀 수 없어 거부한다")
    live_root, sections = LOOK_ROOTS[root]
    require(section in sections, f"{json_path}: {root} 구역은 {'|'.join(sections)} 만")
    return {"root": root, "live_root": live_root, "package": package, "section": section, "name": name}


def require_tunable_path(json_path, path):
    require(isinstance(json_path, str) and json_path, f"{path}: 경로 문자열이어야 한다")
    require(not json_path.startswith(GOLDEN_INPUT_PREFIX), f"{path}: {json_path} 는 골든 입력(StructDefaults.TDFluidSimulationConfig, 골든 스냅숏이 헤더 기본값과의 차이만 담음)이라 스윕·되쓰기를 거부한다(C2, RP-M9)")
    if json_path.startswith(LOOK_PREFIX):
        parse_look_path(json_path)


def scene_sources(scene, plan):
    if "sources" in scene:
        return list(scene["sources"])
    return ["water_probe"] if "water_probe" in plan["metrics"]["sources"] else ["look"]


def scene_sample_count(scene, sources):
    if "look" in sources:
        if "shots" in scene:
            return scene["shots"]["count"]
        schedule = scene.get("schedule")
        return len(schedule.get("times", [])) if isinstance(schedule, dict) else 1
    if "probe" in sources:
        return len(scene["probe"].get("times", []))
    if sources == ["offline"]:
        return None
    return 1


def validate_shots(shots, path):
    check_keys(shots, path, SHOTS_KEYS, SHOTS_OPTIONAL_KEYS)
    require(isinstance(shots["count"], int) and not isinstance(shots["count"], bool) and shots["count"] >= 1, f"{path}.count: 1 이상 정수")
    require(is_number(shots["interval"]) and shots["interval"] > 0, f"{path}.interval: 0 보다 큰 초")
    require(shots.get("kind", "replicate") in SHOT_KINDS, f"{path}.kind: {'|'.join(SHOT_KINDS)}")


def validate_offline(offline, path):
    check_keys(offline, path, OFFLINE_KEYS, OFFLINE_OPTIONAL_KEYS)
    command = offline["command"]
    require(isinstance(command, list) and command and all(isinstance(part, str) and part for part in command), f"{path}.command: 문자열 목록이어야 한다")
    missing = [placeholder for placeholder in OFFLINE_PLACEHOLDERS if placeholder not in command]
    require(not missing, f"{path}.command: 자리표시 {', '.join(missing)} 가 필요하다")
    require(not command[0].endswith(".py") or project_path(command[0]).is_file(), f"{path}.command: 스크립트가 없다: {command[0]}")
    require(isinstance(offline.get("output", OFFLINE_OUTPUT_NAME), str) and ID_PATTERN.match(Path(offline.get("output", OFFLINE_OUTPUT_NAME)).stem), f"{path}.output: 영문 파일 이름")


def validate_scene(scene, index, plan, stage):
    path = f"scenes[{index}]"
    check_keys(scene, path, SCENE_KEYS, SCENE_OPTIONAL_KEYS, SCENE_MOVED_KEYS)
    require(isinstance(scene.get("name"), str) and ID_PATTERN.match(scene["name"]), f"{path}.name: 영문·숫자·_- 이름이어야 한다")
    require(isinstance(scene.get("level"), str) and scene["level"].startswith("/Game/"), f"{path}.level: /Game/ 레벨 경로여야 한다")
    require(isinstance(scene.get("camera"), dict), f"{path}.camera: 룩데브 적용 객체(pawn·arm·view·awake·hide 등)여야 한다")
    require("tuning_reload" not in scene["camera"], f"{path}.camera: tuning_reload 는 스윕이 넣는다")
    require(is_number(scene.get("settle")) and scene["settle"] > 0, f"{path}.settle: 양수 초여야 한다")
    require(is_number(scene.get("time_dilation", 1.0)) and scene.get("time_dilation", 1.0) > 0, f"{path}.time_dilation: 양수여야 한다")
    require(is_number(scene.get("probe_window_scale", 1.0)) and scene.get("probe_window_scale", 1.0) > 0, f"{path}.probe_window_scale: 양수여야 한다")
    require(scene.get("schedule") is None or isinstance(scene["schedule"], dict), f"{path}.schedule: null 또는 룩데브 schedule 객체여야 한다")
    require(isinstance(scene.get("live2_refs", []), list), f"{path}.live2_refs: 목록이어야 한다")
    if "sources" in scene:
        sources = scene["sources"]
        require(isinstance(sources, list) and sources and all(source in METRIC_SOURCES for source in sources) and len(set(sources)) == len(sources), f"{path}.sources: {'|'.join(METRIC_SOURCES)} 목록(중복 없이)")
        require("water_probe" not in sources or sources == ["water_probe"], f"{path}.sources: water_probe 는 단독으로만 쓴다")
    sources = scene_sources(scene, plan)
    if "shots" in scene:
        validate_shots(scene["shots"], f"{path}.shots")
        require(not (isinstance(scene.get("schedule"), dict) and "times" in scene["schedule"]), f"{path}: shots 와 schedule.times 를 함께 쓰지 않는다(shots 가 schedule 을 만든다)")
    if "step_apply" in scene:
        require(isinstance(scene["step_apply"], dict) and "tuning_reload" not in scene["step_apply"] and "schedule" not in scene["step_apply"], f"{path}.step_apply: 객체여야 하고 tuning_reload·schedule 은 스윕이 넣는다")
    require(("probe" in sources) == ("probe" in scene), f"{path}.probe: sources 에 probe 가 있을 때만(그리고 반드시) probe 설정을 둔다")
    if "probe" in scene:
        require(isinstance(scene["probe"], dict), f"{path}.probe: 객체여야 한다")
        times = scene["probe"].get("times", [])
        require(isinstance(times, list) and all(is_number(value) and value >= 0 for value in times), f"{path}.probe.times: 0 이상 초 목록")
    require(("offline" in sources) == ("offline" in scene), f"{path}.offline: sources 에 offline 이 있을 때만(그리고 반드시) offline 설정을 둔다")
    if "offline" in scene:
        validate_offline(scene["offline"], f"{path}.offline")
    if "look" in sources and stage != "frame":
        require(isinstance(scene.get("regions_template"), str) and project_path(scene["regions_template"]).is_file(), f"{path}.regions_template: 영역 틀 없음: {scene.get('regions_template')}")
        require(isinstance(scene.get("targets"), str) and project_path(scene["targets"]).is_file(), f"{path}.targets: 목표 파일 없음: {scene.get('targets')}")
    if stage in LOCKED_STAGES:
        count = scene_sample_count(scene, sources)
        require(count is None or count >= MIN_STAGE_SAMPLES, f"{path}: 단계 실행은 장면 표본 ≥{MIN_STAGE_SAMPLES} 이 필요하다(look 은 shots.count, probe 만 쓰면 probe.times, 지금 {count})")


def target_scene(target, plan):
    if "scene" in target:
        return next(scene for scene in plan["scenes"] if scene["name"] == target["scene"])
    return plan["scenes"][0]


def validate_target_band(target, path):
    has_low, has_high = "low" in target, "high" in target
    require(has_low == has_high, f"{path}: low 와 high 는 함께 쓴다")
    if has_low:
        require(is_number(target["low"]) and is_number(target["high"]) and target["low"] < target["high"], f"{path}: low < high 인 수여야 한다")
    require(target.get("role", "score") in TARGET_ROLES, f"{path}.role: {'|'.join(TARGET_ROLES)}")
    if "relative_to_control" in target:
        band = target["relative_to_control"]
        require(isinstance(band, dict) and set(band) == {"low", "high"} and is_number(band["low"]) and is_number(band["high"]) and band["low"] < band["high"], f"{path}.relative_to_control: {{low, high}} 배율 수 2개(low < high)")
        require(not (band["low"] <= 1.0 <= band["high"]) or target.get("role") == "constraint", f"{path}: relative_to_control 띠가 1.0 을 포함하면 role constraint 여야 한다(대조 중심 띠는 점수로 쓰면 후보가 늘 v00)")


def validate_targets(plan):
    metrics = plan["metrics"]
    targets = metrics.get("targets")
    require(isinstance(targets, list) and targets, "metrics.targets: 목표 지표 목록(1개 이상)이어야 한다")
    scene_names = {scene["name"] for scene in plan["scenes"]}
    needs_range = "look" not in metrics["sources"]
    seen = set()
    for index, target in enumerate(targets):
        path = f"metrics.targets[{index}]"
        check_keys(target, path, TARGET_KEYS, TARGET_OPTIONAL_KEYS)
        require(isinstance(target.get("metric"), str) and target["metric"], f"{path}.metric: 지표 이름 문자열이어야 한다")
        require(is_number(target.get("weight", 1.0)) and target.get("weight", 1.0) > 0, f"{path}.weight: 양수여야 한다")
        require(is_number(target.get("tolerance", 0.0)) and target.get("tolerance", 0.0) >= 0, f"{path}.tolerance: 0 이상 수(대조 표류 절대 허용)여야 한다")
        require("scene" not in target or target["scene"] in scene_names, f"{path}.scene: 계획 장면 이름이어야 한다")
        require(len(plan["scenes"]) < 2 or "scene" in target, f"{path}.scene: 장면이 2개 이상인 계획은 모든 target 에 scene 이 필요하다")
        validate_target_band(target, path)
        require("low" in target or "relative_to_control" in target or not needs_range, f"{path}: look 지표가 없는 계획은 low·high(목표 범위)가 필요하다")
        if target_scene(target, plan).get("shots", {}).get("kind") == "series":
            require(SERIES_METRIC_PATTERN.match(target["metric"]), f"{path}.metric: series 장면 목표는 <n>. 접두 지표나 derived.* 만(시계열 중앙값은 뜻이 없다)")
            require(target.get("tolerance", 0.0) > 0, f"{path}.tolerance: series 장면 목표는 tolerance > 0 이 필요하다")
        key = (target.get("scene"), target["metric"])
        require(key not in seen, f"{path}: 같은 지표가 두 번 있다")
        seen.add(key)


def validate_metrics(metrics):
    check_keys(metrics, "metrics", METRICS_KEYS, METRICS_OPTIONAL_KEYS)
    sources = metrics["sources"]
    require(isinstance(sources, list) and sources, "metrics.sources: 지표 출처 목록(look|water_probe|probe|offline)이어야 한다")
    old_names = [source for source in sources if source not in METRIC_SOURCES]
    require(not old_names, f"metrics.sources: {', '.join(map(str, old_names))} 는 모르는 출처다({'|'.join(METRIC_SOURCES)} 만, 옛 이름은 B6 가 바꾼다)")
    require(isinstance(metrics.get("noise_floor", {}), dict) and all(is_number(value) and value >= 0 for value in metrics.get("noise_floor", {}).values()), "metrics.noise_floor: {지표: 0 이상 수}")
    derived = metrics.get("derived", [])
    require(isinstance(derived, list), "metrics.derived: 목록이어야 한다")
    for index, item in enumerate(derived):
        path = f"metrics.derived[{index}]"
        require(isinstance(item, dict) and isinstance(item.get("name"), str) and ID_PATTERN.match(item["name"]), f"{path}.name: 영문 이름")
        require(item.get("op") in DERIVED_OPERATIONS, f"{path}.op: {'|'.join(DERIVED_OPERATIONS)} 만(recovery_63 은 삭제됨)")


def param_paths(param):
    if "json_paths" in param:
        return list(param["json_paths"])
    return [param["json_path"]] if "json_path" in param else []


def validate_param(param, index, scene_names):
    path = f"params[{index}]"
    require(isinstance(param, dict), f"{path}: 객체여야 한다")
    given = [key for key in PARAM_PATH_KEYS if key in param]
    require(len(given) == 1, f"{path}: json_path·json_paths·actor_prop 중 하나만 쓴다(지금 {', '.join(given) or '없음'})")
    check_keys(param, path, ("values",), PARAM_PATH_KEYS + PARAM_OPTIONAL_KEYS)
    require(isinstance(param.get("values"), list) and param["values"], f"{path}.values: 1개 이상")
    require(isinstance(param.get("split_group", False), bool), f"{path}.split_group: 불")
    require(param.get("cost", COST_DIRECTIONS[0]) in COST_DIRECTIONS, f"{path}.cost: {'|'.join(COST_DIRECTIONS)}")
    if "scenes" in param:
        require(isinstance(param["scenes"], list) and param["scenes"] and all(name in scene_names for name in param["scenes"]), f"{path}.scenes: 계획 장면 이름 목록이어야 한다(장면에 없는 이름)")
    if "json_paths" in param:
        paths = param["json_paths"]
        require(isinstance(paths, list) and len(paths) >= 2 and len(set(map(str, paths))) == len(paths), f"{path}.json_paths: 서로 다른 경로 2개 이상")
        require(all(isinstance(item, str) and not item.startswith(LOOK_PREFIX) for item in paths), f"{path}.json_paths: 조절 경로만(Look 은 json_path 하나로)")
    if "actor_prop" in param:
        check_keys(param["actor_prop"], f"{path}.actor_prop", ACTOR_PROP_KEYS, ACTOR_PROP_OPTIONAL_KEYS)
        require(all(isinstance(value, str) and value for value in param["actor_prop"].values()), f"{path}.actor_prop: label·property·component 는 문자열")
        require("base" in param, f"{path}.base: actor_prop 은 base(레벨의 현재 값)가 필요하다(대조 단계가 그 값으로 되돌린다)")
    for item in param_paths(param):
        require_tunable_path(item, f"{path}.json_path")


def validate_assignment_list(items, path):
    require(isinstance(items, list), f"{path}: 할당 목록이어야 한다")
    for index, item in enumerate(items):
        item_path = f"{path}[{index}]"
        check_keys(item, item_path, ASSIGNMENT_KEYS, ASSIGNMENT_OPTIONAL_KEYS)
        require_tunable_path(item["json_path"], f"{item_path}.json_path")
        require(isinstance(item.get("split_group", False), bool), f"{item_path}.split_group: 불")


def validate_plan(plan, stage=None):
    require(isinstance(plan, dict), "계획 최상위는 객체여야 한다")
    missing = [key for key in PLAN_KEYS if key not in plan]
    require(not missing, f"계획 키 없음: {', '.join(missing)}")
    check_keys(plan, "계획", PLAN_KEYS, PLAN_OPTIONAL_KEYS + PLAN_RETIRED_KEYS)
    require(isinstance(plan["id"], str) and ID_PATTERN.match(plan["id"]), "id: 영문·숫자·_- 이름이어야 한다")
    require(plan["mode"] in MODES, f"mode: {'|'.join(MODES)}")
    require(isinstance(plan["repeats"], int) and not isinstance(plan["repeats"], bool) and plan["repeats"] >= 1, "repeats: 1 이상 정수")
    require(isinstance(plan["control_every"], int) and not isinstance(plan["control_every"], bool) and plan["control_every"] >= 0, "control_every: 0 이상 정수(0이면 처음·중간·끝만)")
    require(plan["reset"] in RESET_MODES, f"reset: {'|'.join(RESET_MODES)}")
    require(isinstance(plan.get("control_middle", True), bool), "control_middle: 불(거짓이면 대조를 처음·끝 두 번만)")
    validate_metrics(plan["metrics"])
    require(isinstance(plan["scenes"], list) and plan["scenes"], "scenes: 1개 이상")
    for index, scene in enumerate(plan["scenes"]):
        validate_scene(scene, index, plan, stage)
    names = [scene["name"] for scene in plan["scenes"]]
    require(len(set(names)) == len(names), "scenes: name 중복")
    validate_targets(plan)
    require(isinstance(plan["params"], list) and plan["params"], "params: 1개 이상")
    for index, param in enumerate(plan["params"]):
        validate_param(param, index, set(names))
    paths = [item for param in plan["params"] for item in param_paths(param)]
    require(len(set(paths)) == len(paths), "params: json_path 중복")
    props = [json.dumps(param["actor_prop"], sort_keys=True) for param in plan["params"] if "actor_prop" in param]
    require(len(set(props)) == len(props), "params: actor_prop 중복")
    base_tuning = plan.get("base_tuning", [])
    validate_assignment_list(base_tuning, "base_tuning")
    require(not base_tuning or isinstance(plan.get("base_tuning_ships"), bool), "base_tuning_ships: base_tuning 이 있으면 출하 여부 불(참이면 채택 때 base 항목도 덧씌움에 쓴다)이 필요하다")
    require("base_tuning_ships" not in plan or isinstance(plan["base_tuning_ships"], bool), "base_tuning_ships: 불")
    overlap = sorted(set(paths) & {item["json_path"] for item in base_tuning})
    require(not overlap, f"base_tuning 경로가 params 경로와 같다: {', '.join(overlap)}")
    return plan


def lookdev_key_errors(plan):
    try:
        from pie_lookdev_fluid import find_key_errors
    except ImportError as error:
        raise PlanError(f"pie_lookdev_fluid.find_key_errors(apply) 를 불러오지 못했다(CAL-0 B3 계약 함수 필요): {error}") from error
    errors = []
    for index, scene in enumerate(plan["scenes"]):
        step_extra = copy.deepcopy(scene.get("step_apply", {}))
        if "probe" in scene:
            step_extra["probe"] = copy.deepcopy(scene["probe"])
        schedule = scene_schedule(scene)
        if schedule:
            step_extra["schedule"] = copy.deepcopy(schedule)
        for label, apply in ((f"scenes[{index}].camera", scene["camera"]), (f"scenes[{index}].step_apply", step_extra)):
            errors.extend(f"{label}: {line}" for line in find_key_errors(copy.deepcopy(apply)))
    return errors


def load_plan(path, stage=None):
    try:
        plan = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise PlanError(f"계획을 읽지 못했다: {error}") from error
    validate_plan(plan, stage)
    key_errors = lookdev_key_errors(plan)
    require(not key_errors, "룩데브 적용 키 오류: " + "; ".join(key_errors))
    return plan


def plan_warnings(plan, stage):
    warnings = [f"{key}: B6 가 지울 초안 키(실제 실행은 거부)" for key in PLAN_RETIRED_KEYS if key in plan]
    if stage and plan["metrics"].get("noise_floor"):
        warnings.append("metrics.noise_floor: 단계 실행에서는 무시(잡음은 판정기가 단계 표본으로 계산)")
    return warnings


def look_entry(document, look):
    return document.get(look["root"], {}).get(look["package"], {}).get(look["section"], {})


def set_look_value(document, look, value):
    document.setdefault(look["root"], {}).setdefault(look["package"], {}).setdefault(look["section"], {})[look["name"]] = copy.deepcopy(value)


def set_live_value(look_live, look, value):
    look_live.setdefault(look["live_root"], {}).setdefault(look["package"], {}).setdefault(look["section"], {})[look["name"]] = copy.deepcopy(value)


def set_prop_value(props, actor_prop, value):
    target = props.setdefault(actor_prop["label"], {})
    if actor_prop.get("component"):
        target = target.setdefault(actor_prop["component"], {})
    target[actor_prop["property"]] = copy.deepcopy(value)


def merge_into(target, source):
    for key, value in source.items():
        if isinstance(value, dict) and isinstance(target.get(key), dict):
            merge_into(target[key], value)
        else:
            target[key] = copy.deepcopy(value)
    return target


def read_back_expect(struct_name, field_name, kinds, value, role):
    source = READ_BACK_TABLE[struct_name]
    return {"struct": struct_name, "field": field_name, "kinds": kinds, "value": value, "role": role, "owner": source["owner"], "access": source["access"]}


def counts_in_reapply_report(struct_name):
    return ":" not in READ_BACK_TABLE[struct_name]["owner"]


def build_base_layer(plan, overlay, document, look_document, schema):
    items = [dict(item, origin="덧씌움") for item in (overlay["assignments"] if overlay else [])]
    items.extend(dict(item, origin="base_tuning") for item in plan.get("base_tuning", []))
    current_document = copy.deepcopy(document)
    current_look = copy.deepcopy(look_document)
    layer = {"assignments": [], "expect": [], "look_live": {}, "look_levels": {}}
    for item in items:
        assignment = {key: copy.deepcopy(item[key]) for key in ("json_path", "value", "split_group", "base") if key in item}
        assignment.setdefault("split_group", False)
        layer["assignments"].append(assignment)
        if item["json_path"].startswith(LOOK_PREFIX):
            look = parse_look_path(item["json_path"])
            set_look_value(current_look, look, item["value"])
            set_live_value(layer["look_live"], look, item["value"])
            levels = layer["look_levels"].setdefault(look["package"], [])
            levels.extend(level for level in item.get("levels") or [] if level not in levels)
            continue
        try:
            info = fluid_tuning_patch.apply_assignment(current_document, schema, assignment)
        except fluid_tuning_patch.PatchError as error:
            raise PlanError(f"{item['origin']} {item['json_path']}: {error}") from error
        require(info["struct"] in READ_BACK_TABLE, f"{item['origin']} {item['json_path']}: {info['struct']} 은 다시 읽기가 없어 모든 단계에서 단언할 수 없다(C6)")
        layer["expect"].append(read_back_expect(info["struct"], info["field"], info["affected_kinds"], item["value"], item["origin"]))
    return layer, current_document, current_look


def describe_tuning_path(document, schema, json_path, value, split_group, index):
    try:
        info = fluid_tuning_patch.describe_assignment(document, schema, {"json_path": json_path, "value": value, "split_group": split_group})
    except fluid_tuning_patch.PatchError as error:
        raise PlanError(f"params[{index}]: {error}") from error
    require(info["struct"] in READ_BACK_TABLE, f"params[{index}]: {info['struct']} 은 PIE 실효 값 다시 읽기가 없어 변형 유효성을 단언할 수 없다(허용 {', '.join(READ_BACK_TABLE)})")
    require(info["affected_kinds"] or not counts_in_reapply_report(info["struct"]), f"params[{index}]: 이 경로를 따르는 종류가 없다")
    return info


def look_base_value(look_document, look, param, index):
    entry = look_entry(look_document, look)
    if look["name"] in entry:
        current = entry[look["name"]]
        require("base" not in param or values_match(param["base"], current), f"params[{index}].base: {param.get('base')!r} ≠ 정본 모양 값 {current!r}")
        return current
    require("base" in param, f"params[{index}]: 정본 모양에 {look['name']} 이 없어 base(직접 부모 기본값)가 필요하다")
    return param["base"]


def describe_params(plan, document, schema, look_document=None):
    described = []
    for index, param in enumerate(plan["params"]):
        item = {"index": index, "values": param["values"], "split_group": param.get("split_group", False), "scenes": param.get("scenes"), "cost": param.get("cost")}
        if "actor_prop" in param:
            item.update(kind="actor_prop", json_path=None, actor_prop=param["actor_prop"], base_value=param["base"])
        elif str(param.get("json_path", "")).startswith(LOOK_PREFIX):
            look = parse_look_path(param["json_path"])
            item.update(kind="look", json_path=param["json_path"], look=look, base_value=look_base_value(look_document or {}, look, param, index))
        else:
            paths = param_paths(param)
            infos = [describe_tuning_path(document, schema, path, param["values"][0], item["split_group"], index) for path in paths]
            bases = [info["base_value"] for info in infos]
            require(all(values_match(base, bases[0]) for base in bases), f"params[{index}].json_paths: 기준 값이 다르다({bases!r}) — 연동 손잡이는 같은 기준에서 출발해야 한다")
            first = infos[0]
            item.update(kind="tuning", json_path=paths[0], struct=first["struct"], field=first["field"], kinds=first["affected_kinds"], owner=first["owner"], base_value=first["base_value"],
                        fields=[{"json_path": path, "struct": info["struct"], "field": info["field"], "kinds": info["affected_kinds"]} for path, info in zip(paths, infos)])
            if "json_paths" in param:
                item["json_paths"] = paths
        changed_values = [value for value in param["values"] if value != item["base_value"]]
        require(changed_values, f"params[{index}]: 기준 값 {item['base_value']!r} 과 다른 값이 없다(대조만 있는 스윕)")
        item["changed_values"] = changed_values
        described.append(item)
    return described


def variant_scenes(changed_params):
    restricted = [set(param["scenes"]) for param in changed_params if param.get("scenes")]
    if not restricted:
        return None
    return sorted(set.intersection(*restricted))


def make_variant(variant_id, params, chosen_values, base_layer=None):
    base_layer = base_layer or EMPTY_BASE_LAYER
    assignments = copy.deepcopy(base_layer["assignments"])
    knob_expect = []
    look_live = copy.deepcopy(base_layer["look_live"])
    props = {}
    changed_params = []
    knob_packages = {param["look"]["package"] for param in params if param["kind"] == "look"}
    for param, value in zip(params, chosen_values):
        is_changed = value != param["base_value"]
        if is_changed:
            changed_params.append(param)
        if param["kind"] == "tuning":
            for field in param["fields"]:
                knob_expect.append(read_back_expect(field["struct"], field["field"], field["kinds"], value, "knob"))
                if is_changed:
                    assignments.append({"json_path": field["json_path"], "value": value, "split_group": param["split_group"]})
        elif param["kind"] == "look":
            set_live_value(look_live, param["look"], value)
            assignments.append({"json_path": param["json_path"], "value": value, "split_group": False, "base": param["base_value"]})
        else:
            set_prop_value(props, param["actor_prop"], value)
    return {"id": variant_id, "is_control": not changed_params, "values": list(chosen_values), "assignments": assignments, "expect": knob_expect + copy.deepcopy(base_layer["expect"]),
            "look_live": look_live, "props": props, "knobs": [param["index"] for param in changed_params], "scenes": variant_scenes(changed_params),
            "base_look_levels": {package: list(levels) for package, levels in base_layer.get("look_levels", {}).items() if package not in knob_packages},
            "counts_change": any(param["kind"] == "tuning" and counts_in_reapply_report(param["struct"]) for param in changed_params)}


def build_variants(plan, params, base_layer=None):
    base_values = [param["base_value"] for param in params]
    if plan["mode"] == "grid":
        combinations = [list(values) for values in itertools.product(*[param["values"] for param in params]) if list(values) != base_values]
    else:
        combinations = []
        for index, param in enumerate(params):
            for value in param["changed_values"]:
                combinations.append(base_values[:index] + [value] + base_values[index + 1:])
    unique = []
    for combination in combinations:
        if combination not in unique:
            unique.append(combination)
    variants = [make_variant(CONTROL_ID, params, base_values, base_layer)]
    variants.extend(make_variant(f"v{index:02d}", params, combination, base_layer) for index, combination in enumerate(unique, start=1))
    return variants


def knob_ends(param):
    changed = param["changed_values"]
    if all(is_number(value) for value in changed):
        low, high = min(changed), max(changed)
        return [low] if low == high else [low, high]
    unique = []
    for value in changed:
        if value not in unique:
            unique.append(value)
    return unique


def build_screen_variants(params, base_layer):
    base_values = [param["base_value"] for param in params]
    variants = [make_variant(CONTROL_ID, params, base_values, base_layer)]
    for index, param in enumerate(params):
        for value in knob_ends(param):
            variants.append(make_variant(f"v{len(variants):02d}", params, base_values[:index] + [value] + base_values[index + 1:], base_layer))
    return variants


def build_sequence(variant_ids, repeats, control_every, control_middle=True):
    items = [variant_id for _ in range(repeats) for variant_id in variant_ids]
    middle = len(items) // 2
    sequence = [CONTROL_ID]
    for index, item in enumerate(items):
        wants_control = index > 0 and ((control_middle and index == middle) or (control_every and index % control_every == 0))
        if wants_control and sequence[-1] != CONTROL_ID:
            sequence.append(CONTROL_ID)
        sequence.append(item)
    sequence.append(CONTROL_ID)
    return sequence


def build_screen_sequence(variant_ids):
    sequence = [CONTROL_ID]
    for start in range(0, len(variant_ids), 2):
        sequence.extend(variant_ids[start:start + 2])
        sequence.append(CONTROL_ID)
    if sequence.count(CONTROL_ID) < MIN_CONTROL_STEPS:
        sequence = [CONTROL_ID]
        for variant_id in variant_ids:
            sequence.extend([variant_id, CONTROL_ID])
    while sequence.count(CONTROL_ID) < MIN_CONTROL_STEPS:
        sequence.append(CONTROL_ID)
    return sequence


def confirm_candidates(screen, screen_variant_ids):
    best = (screen.get("best") or {}).get("variant")
    second = (screen.get("second") or {}).get("variant")
    for name, variant_id in (("best", best), ("second", second)):
        require(variant_id is None or (variant_id != CONTROL_ID and variant_id in screen_variant_ids), f"screen.json {name} 변형 {variant_id!r} 이 이 계획의 screen 변형({', '.join(screen_variant_ids)})에 없다")
    return best, second


def stage_scene_sequence(stage, scene_name, variants, candidates):
    applies = [variant["id"] for variant in variants if not variant["is_control"] and (variant["scenes"] is None or scene_name in variant["scenes"])]
    if stage == "frame":
        return [CONTROL_ID]
    if stage == "verify":
        return [CONTROL_ID] * VERIFY_STEP_COUNT
    if stage == "screen":
        return build_screen_sequence(applies) if applies else []
    best, second = candidates
    if best not in applies:
        return []
    return [CONTROL_ID, best, CONTROL_ID, second if second in applies else best, CONTROL_ID]


def legacy_scene_sequence(plan, scene_name, variants):
    applies = [variant["id"] for variant in variants if not variant["is_control"] and (variant["scenes"] is None or scene_name in variant["scenes"])]
    return build_sequence(applies, plan["repeats"], plan["control_every"], plan.get("control_middle", True)) if applies else []


def write_variant_copies(variants, run_dir, schema):
    for variant in variants:
        try:
            manifest = fluid_tuning_patch.write_variant(variant["assignments"], run_dir / variant["id"], schema=schema)
        except fluid_tuning_patch.PatchError as error:
            raise PlanError(f"{variant['id']}: {error}") from error
        variant.update({"tuning": manifest["tuning"], "look": manifest["look"], "tuning_sha256": manifest["tuning_sha256"], "look_sha256": manifest["look_sha256"], "owners": [item.get("owner") for item in manifest["assignments"]]})


def read_back_requests(variant):
    return [{"struct": item["struct"], "field": item["field"], "kinds": item["kinds"], "owner": item["owner"], "access": item["access"]} for item in variant["expect"]]


def step_name(step, variant_id):
    return f"s{step:02d}_{variant_id}"


def tuning_reload_request(variant, reset, base_variant=None):
    request = {"tuning": variant["tuning"], "look": variant["look"], "reset": reset, "read_back": read_back_requests(variant)}
    if base_variant is not None:
        request["base"] = {"tuning": base_variant["tuning"], "look": base_variant["look"]}
    return request


def scene_schedule(scene):
    shots = scene.get("shots")
    if not shots:
        return copy.deepcopy(scene.get("schedule"))
    schedule = copy.deepcopy(scene.get("schedule") or {})
    schedule.update(times=[round(scene["settle"] + step * shots["interval"], 6) for step in range(shots["count"])], reference="now", fixed_fps=SCHEDULE_FIXED_FPS)
    return schedule


def overlay_scene_props(overlay, scene):
    props = {}
    for item in (overlay or {}).get("actor_props", []):
        if item.get("level") == scene["level"]:
            set_prop_value(props, item, item["value"])
    return props


def ownerless_look_packages(variant, level):
    return sorted(package for package, levels in variant.get("base_look_levels", {}).items() if level not in levels)


def out_of_scene_look_packages(params, scene_name):
    look_params = [param for param in params if param["kind"] == "look"]
    in_scene = {param["look"]["package"] for param in look_params if not param.get("scenes") or scene_name in param["scenes"]}
    return sorted({param["look"]["package"] for param in look_params} - in_scene)


def skippable_look_packages(variant, level, out_of_scene_packages=()):
    return sorted(set(ownerless_look_packages(variant, level)) | set(out_of_scene_packages))


def make_lookdev_spec(scene, plan, sequence, variants_by_id, base_variant=None, overlay=None, out_of_scene_packages=()):
    base = dict(copy.deepcopy(scene["camera"]), time_dilation=scene.get("time_dilation", 1.0))
    schedule = scene_schedule(scene)
    scene_props = overlay_scene_props(overlay, scene)
    steps = []
    for step, variant_id in enumerate(sequence):
        variant = variants_by_id[variant_id]
        apply = {"tuning_reload": tuning_reload_request(variant, plan["reset"], base_variant)}
        merge_into(apply, scene.get("step_apply", {}))
        if variant.get("look_live"):
            apply["look_live"] = copy.deepcopy(variant["look_live"])
            optional_packages = skippable_look_packages(variant, scene["level"], out_of_scene_packages)
            if optional_packages:
                apply["look_live"]["skip_if_no_owner"] = optional_packages
        props = merge_into(merge_into(copy.deepcopy(apply.get("props", {})), scene_props), variant.get("props", {}))
        if props:
            apply["props"] = props
        if "probe" in scene:
            apply["probe"] = copy.deepcopy(scene["probe"])
        if schedule:
            apply["schedule"] = copy.deepcopy(schedule)
        steps.append({"name": step_name(step, variant_id), "apply": apply, "settle": scene["settle"]})
    return {"base": base, "variants": steps}


def normalized_name(value):
    return str(value).split(".")[-1].replace("_", "").upper()


def values_match(actual, expected):
    if isinstance(expected, bool) or isinstance(actual, bool):
        return actual is expected
    if is_number(expected):
        if not is_number(actual):
            return False
        if isinstance(expected, int) and isinstance(actual, int):
            return actual == expected
        return abs(actual - expected) <= max(FLOAT_RELATIVE_TOLERANCE * abs(expected), FLOAT_ABSOLUTE_TOLERANCE)
    if isinstance(expected, str):
        return isinstance(actual, str) and normalized_name(actual) == normalized_name(expected)
    if isinstance(expected, dict):
        return isinstance(actual, dict) and sorted(actual) == sorted(expected) and all(values_match(actual[key], expected[key]) for key in expected)
    if isinstance(expected, list):
        return isinstance(actual, list) and len(actual) == len(expected) and all(values_match(left, right) for left, right in zip(actual, expected))
    return actual == expected


def same_path(left, right):
    return bool(left) and bool(right) and Path(left).resolve() == Path(right).resolve()


def judge_read_back(expect, read):
    label = f"{expect['struct']}.{expect['field']}"
    if not isinstance(read, dict):
        return [f"{label}: 다시 읽기 결과 없음"]
    if "error" in read:
        return [f"{label}: {read['error']}"]
    actors = read.get("actors") or {}
    if not actors:
        if expect.get("role", "knob") != "knob":
            return []
        return [f"{label}: 종류 {', '.join(expect['kinds'])} 액터가 장면에 없다(변형이 아무것도 바꾸지 않음)"]
    errors = []
    for actor_label, actor in sorted(actors.items()):
        if "error" in actor:
            errors.append(f"{actor_label}: {actor['error']}")
        elif actor.get("uses_own_copies"):
            errors.append(f"{actor_label}: Custom 또는 override 프리셋이라 표 값이 쓰이지 않는다")
        elif not values_match(actor.get("value"), expect["value"]):
            role = "" if expect.get("role", "knob") == "knob" else f"({expect['role']} 경로) "
            errors.append(f"{actor_label}: {role}{label} 실효 값 {actor.get('value')!r} ≠ 기대 값 {expect['value']!r}")
    return errors


def judge_look_live(variant, apply_outcome, optional_packages=()):
    if not variant.get("look_live"):
        return []
    if "look_applied" not in apply_outcome:
        return ["look_live 결과 없음"]
    skipped = set(apply_outcome.get("skipped_look") or [])
    errors = [f"look_live: {package}: 손잡이 Look 인데 소유자 없음으로 건너뜀" for package in sorted(skipped - set(optional_packages))]
    errors += [f"look_live: {line}" for line in apply_outcome.get("look_errors", [])]
    judged = {group: {package: sections for package, sections in packages.items() if package not in skipped} for group, packages in variant["look_live"].items()}
    if any(judged.values()) and not apply_outcome["look_applied"]:
        errors.append("look 적용 대상 없음(look_applied 0)")
    read_back = apply_outcome.get("look_read_back") or {}
    for package, sections in judged.get("materials", {}).items():
        for section, names in sections.items():
            for name, value in names.items():
                entry = (read_back.get(package) or {}).get(section, {}).get(name)
                if not isinstance(entry, dict) or not entry.get("read"):
                    errors.append(f"{package}.{section}.{name}: 다시 읽기 없음")
                elif entry.get("matches") is not True:
                    errors.append(f"{package}.{section}.{name}: 다시 읽은 값 {entry['read']!r} ≠ 기대 값 {value!r}")
    return errors


def judge_props(expected_props, apply_outcome):
    outcome = apply_outcome.get("props")
    if expected_props and not isinstance(outcome, dict):
        return ["props 결과 없음"]
    errors = []
    for label, entry in sorted((outcome if isinstance(outcome, dict) else {}).items()):
        if isinstance(entry, dict) and "error" in entry:
            errors.append(f"props.{label}: {entry['error']}")
        elif not isinstance(entry, list):
            errors.append(f"props.{label}: {entry}")
    return errors


def judge_step(variant, apply_outcome, expected_props=None, judges_look=True, optional_packages=()):
    reload_outcome = apply_outcome.get("tuning_reload") if isinstance(apply_outcome, dict) else None
    if not isinstance(reload_outcome, dict):
        return ["tuning_reload 결과 없음"]
    if "error" in reload_outcome:
        return ["tuning_reload: " + str(reload_outcome["error"])]
    report = reload_outcome.get("report") or {}
    errors = []
    changed = report.get("changed_field_count")
    if not isinstance(changed, int):
        errors.append("보고서에 changed_field_count 없음")
    elif variant["is_control"] and changed != 0:
        errors.append(f"대조 변형인데 ChangedFieldCount {changed} ≠ 0")
    elif not variant["is_control"] and variant.get("counts_change", True) and changed < 1:
        errors.append(f"ChangedFieldCount {changed} < 1(변형이 아무 필드도 바꾸지 않음)")
    if not same_path(report.get("tuning_source_path"), variant["tuning"]):
        errors.append(f"보고서 조절 출처 {report.get('tuning_source_path')!r} ≠ 사본 {variant['tuning']}")
    read_back = reload_outcome.get("read_back")
    if not isinstance(read_back, list) or len(read_back) != len(variant["expect"]):
        return errors + ["실효 값 다시 읽기 결과 수가 기대와 다르다"]
    for expect, read in zip(variant["expect"], read_back):
        errors.extend(judge_read_back(expect, read))
    look_errors = judge_look_live(variant, apply_outcome, optional_packages) if judges_look else []
    return errors + look_errors + judge_props(variant.get("props") if expected_props is None else expected_props, apply_outcome)


def flatten_metrics(value, prefix):
    if is_number(value):
        return {prefix: value}
    if not isinstance(value, dict):
        return {}
    flat = {}
    for key, item in value.items():
        flat.update(flatten_metrics(item, f"{prefix}.{key}"))
    return flat


def probe_metrics(apply_outcome):
    probe = apply_outcome.get("probe") if isinstance(apply_outcome, dict) else None
    if not isinstance(probe, dict):
        return {}, ["probe 결과 없음"]
    if "error" in probe:
        return {}, ["probe: " + str(probe["error"])]
    flat = {key.replace("probe.probe.", "probe.", 1): value for key, value in flatten_metrics(probe.get("metrics", {}), "probe").items()}
    return flat, ([] if flat else ["probe 지표 없음"])


def lookdev_images(output_dir, index, entry):
    schedule = entry.get("schedule")
    if isinstance(schedule, dict):
        return [relative_text(shot["path"]) for shot in schedule.get("shots", []) if "path" in shot]
    path = Path(output_dir) / f"{index:02d}_{entry.get('name')}.png"
    return [relative_text(path)] if entry.get("saved") else []


def shot_game_times(entry, apply):
    schedule = entry.get("schedule")
    if isinstance(schedule, dict):
        return [shot.get("captured_game_time", shot.get("game_time")) for shot in schedule.get("shots", []) if "path" in shot]
    return [apply.get("shot_game_time")] if entry.get("saved") else []


def image_hashes(images):
    return [file_sha256(project_path(image)) if project_path(image).is_file() else None for image in images]


def probe_samples(apply_outcome):
    probe = apply_outcome.get("probe")
    return copy.deepcopy(probe.get("samples", [])) if isinstance(probe, dict) else []


def metrics_record(metrics):
    return {"metrics": metrics, "metrics_sha256": text_sha256(json.dumps(metrics, sort_keys=True))}


def assert_lookdev_result(result, sequence, variants_by_id, output_dir, scene_entry=None):
    scene_entry = scene_entry or {}
    sources = scene_entry.get("sources", ["look"])
    is_stage = bool(scene_entry.get("stage"))
    entries = result.get("variants", []) if isinstance(result, dict) else []
    steps = []
    for index, variant_id in enumerate(sequence):
        entry = entries[index] if index < len(entries) else None
        expected_name = step_name(index, variant_id)
        if entry is None or entry.get("name") != expected_name:
            errors = ["룩데브 결과에 이 단계가 없다"]
            steps.append({"step": index, "name": expected_name, "variant": variant_id, "valid": False, "errors": errors, "invalid_reasons": errors, "images": []})
            continue
        variant = variants_by_id[variant_id]
        errors = judge_step(variant, entry.get("apply"), optional_packages=skippable_look_packages(variant, scene_entry.get("level"), scene_entry.get("out_of_scene_look_packages", [])))
        apply = entry.get("apply") if isinstance(entry.get("apply"), dict) else {}
        images = lookdev_images(output_dir, index, entry)
        metrics, samples = {}, None
        if "probe" in sources:
            metrics, probe_errors = probe_metrics(apply)
            errors.extend(probe_errors)
            samples = probe_samples(apply)
        shots_expected = scene_entry.get("shots_expected")
        if is_stage and "look" in sources and len(images) != shots_expected:
            errors.append(f"촬영 장수 {len(images)}/{shots_expected}")
        steps.append({"step": index, "name": expected_name, "variant": variant_id, "valid": not errors, "errors": errors, "invalid_reasons": errors, "sources": sources,
                      "images": images, "image_sha256": image_hashes(images), "shots_expected": shots_expected, "shots_saved": len(images), "shot_game_times": shot_game_times(entry, apply),
                      "shot_game_time": apply.get("shot_game_time"), "report": (apply.get("tuning_reload") or {}).get("report"),
                      "look_applied": apply.get("look_applied"), "look_read_back": apply.get("look_read_back"), "owner_checks": apply.get("owner_checks"), **metrics_record(metrics),
                      **({"skipped_look": apply["skipped_look"]} if apply.get("skipped_look") else {}),
                      **({} if samples is None else {"probe_samples": samples})})
    return steps


def run_lookdev(spec_path, level, output_dir):
    command = [sys.executable, str(LOOKDEV_SCRIPT), "--variants", str(spec_path), "--level", level, "--output-dir", str(output_dir), "--stop-pie"]
    return subprocess.run(command).returncode


def offline_command(scene, variant, output_path):
    replacements = {"{tuning}": variant["tuning"], "{look}": variant["look"], "{output}": str(output_path)}
    parts = [replacements.get(part, part) for part in scene["offline"]["command"]]
    if parts[0].endswith(".py"):
        return [sys.executable, str(project_path(parts[0]))] + parts[1:]
    return parts


def run_offline_command(scene, variant, output_path):
    output_path.unlink(missing_ok=True)
    completed = subprocess.run(offline_command(scene, variant, output_path), capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=ROOT)
    if completed.returncode != 0:
        return [f"offline 명령 종료 {completed.returncode}: {(completed.stdout + completed.stderr)[-OUTPUT_TAIL_CHARACTERS:]}"]
    if not output_path.is_file():
        return [f"offline 출력 없음: {relative_text(output_path)}"]
    return []


def run_offline_step(scene, variant, scene_dir, name):
    output_name = scene["offline"].get("output", OFFLINE_OUTPUT_NAME)
    first = scene_dir / f"{name}_{output_name}"
    repeat = scene_dir / f"{name}_repeat_{output_name}"
    errors = run_offline_command(scene, variant, first) or run_offline_command(scene, variant, repeat)
    if errors:
        return {}, errors, None
    record = {"output": relative_text(first), "output_sha256": file_sha256(first), "repeat_sha256": file_sha256(repeat)}
    if record["output_sha256"] != record["repeat_sha256"]:
        return {}, ["offline 결정론 불일치(같은 사본 2회 출력 sha256 이 다르다)"], record
    try:
        metrics = flatten_metrics(json.loads(first.read_text(encoding="utf-8")), "offline")
    except ValueError as error:
        return {}, [f"offline 출력 JSON 오류: {error}"], record
    return metrics, ([] if metrics else ["offline 지표 없음"]), record


def run_offline_scene(scene, entry, variants_by_id, scene_dir):
    steps = []
    for index, variant_id in enumerate(entry["sequence"]):
        name = step_name(index, variant_id)
        metrics, errors, record = run_offline_step(scene, variants_by_id[variant_id], scene_dir, name)
        steps.append({"step": index, "name": name, "variant": variant_id, "valid": not errors, "errors": errors, "invalid_reasons": errors, "sources": ["offline"], "images": [],
                      "offline": record, **metrics_record(metrics)})
    return steps


def merge_offline_metrics(scene, entry, variants_by_id, scene_dir):
    for step in entry["steps"]:
        metrics, errors, record = run_offline_step(scene, variants_by_id[step["variant"]], scene_dir, step["name"])
        step["errors"].extend(errors)
        step["invalid_reasons"] = step["errors"]
        step["valid"] = not step["errors"]
        step["offline"] = record
        step.update(metrics_record(dict(step.get("metrics") or {}, **metrics)))


def run_water_probe_scene(scene, plan, sequence, variants_by_id, scene_dir, base_variant=None, scene_props=None):
    import pie_lookdev_fluid
    import pie_probe_fluid_water as probe

    stage_report = probe.read_stage_report()
    contract = probe.read_material_contract()
    if stage_report is None or sorted(contract) != ["scalar_names", "texture_names", "vector_names"]:
        return {"error": "물 무대 보고서 또는 재질 계약이 없다(pie_probe_fluid_water.py 전제 조건)"}, []
    points, layout = stage_report["river"]["probe_points"], stage_report["river"]["layout"]
    if not probe.open_stage_level().get("loaded"):
        return {"error": "물 무대 레벨을 열지 못했다"}, []
    session = probe.uemcp.connect()
    environment = {"navigation_ready": probe.pie_check_fluid.wait_for_editor_navigation()}
    if not probe.pie_check_fluid.start_pie(session, environment) or not probe.pie_check_fluid.wait_for_pie_state(True, probe.pie_check_fluid.PIE_START_WAIT_SECONDS):
        return {"error": "PIE를 시작하지 못했다", "environment": environment}, []
    time.sleep(3.0)
    setup = {"requested_backend": scene.get("backend", "cpu"), "requested_grid": None}
    steps = []
    try:
        status = probe.editor_action("status")
        if "error" in status or not probe.prepare_backend(setup, setup["requested_backend"], None):
            return {"error": "물 탐침 환경·백엔드 확인 실패", "status": status}, []
        probe.editor_action("isolate")
        probe.editor_action("console", command=f"{probe.PERF_CONSOLE_VARIABLE} 1")
        for index, variant_id in enumerate(sequence):
            variant = variants_by_id[variant_id]
            started = time.monotonic()
            reload_outcome = pie_lookdev_fluid.run_tuning_reload(tuning_reload_request(variant, plan["reset"], base_variant))
            props = merge_into(copy.deepcopy(scene_props or {}), variant.get("props", {}))
            outcome = {"tuning_reload": reload_outcome}
            if props:
                outcome["props"] = pie_lookdev_fluid.apply_variant({"props": props}, step_name(index, variant_id)).get("props")
            errors = judge_step(variant, outcome, props, judges_look=False)
            run = None
            if not errors:
                time.sleep(scene["settle"])
                run = probe.run_once(points, layout, contract, status["sleep_delay_seconds"], setup["requested_backend"], scene.get("probe_window_scale", 1.0))
            steps.append({"step": index, "name": step_name(index, variant_id), "variant": variant_id, "valid": not errors, "errors": errors, "invalid_reasons": errors, "images": [],
                          "report": reload_outcome.get("report"), "elapsed_seconds": round(time.monotonic() - started, 1), "water_probe": None if run is None else {"rules": run["rules"], "failed": run["failed"]},
                          **({"look_live_skipped": "물 탐침은 물리 지표라 Look 을 적용하지 않는다"} if variant.get("look_live") else {})})
    finally:
        canonical = pie_lookdev_fluid.editor_action("lookdev_tuning_load", tuning="", look="")
        probe.editor_action("console", command=f"{probe.PERF_CONSOLE_VARIABLE} 0")
        probe.editor_action("sampler_stop")
        setup["restored"] = probe.restore_backend(setup)
        probe.pie_check_fluid.stop_pie(session, environment)
    return {"canonical_reload": canonical, "backend": setup}, steps


def canonical_hashes():
    return {"tuning": file_sha256(fluid_tuning_patch.canonical_tuning_path()), "look": file_sha256(fluid_tuning_patch.canonical_look_path())}


def lock_required_files(plan_path, plan):
    required = [plan_path]
    for scene in plan["scenes"]:
        sources = scene_sources(scene, plan)
        if "look" in sources:
            required.extend(project_path(scene[key]) for key in ("regions_template", "targets") if isinstance(scene.get(key), str))
        if "offline" in scene and scene["offline"]["command"][0].endswith(".py"):
            required.append(project_path(scene["offline"]["command"][0]))
    return [relative_text(path) for path in required]


def read_lock(lock_path, plan_path, plan):
    try:
        lock = json.loads(Path(lock_path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise PlanError(f"잠금을 읽지 못했다: {error}") from error
    require(isinstance(lock, dict) and lock.get("version") == LOCK_VERSION, f"잠금 {relative_text(lock_path)}: version {LOCK_VERSION} 잠금만 받는다(v1 은 옛 스윕 재판정 전용, C8)")
    files = lock.get("files")
    require(isinstance(files, dict) and files, "잠금 files: {경로: sha256} 가 비어 있다")
    for text, expected in sorted(files.items()):
        path = project_path(text)
        require(path.is_file() and file_sha256(path) == expected, f"잠금 파일 sha256 불일치 또는 없음: {text}")
    locked = {relative_text(project_path(text)) for text in files}
    missing = [text for text in lock_required_files(plan_path, plan) if text not in locked]
    require(not missing, f"잠금에 필수 파일 누락: {', '.join(missing)}")
    return {"lock": relative_text(lock_path), "lock_sha256": file_sha256(lock_path), "lock_created_at": lock.get("created_at")}


def load_overlay(overlay_path):
    try:
        overlay = json.loads(Path(overlay_path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise PlanError(f"덧씌움을 읽지 못했다: {error}") from error
    require(isinstance(overlay, dict) and overlay.get("version") == OVERLAY_VERSION, f"덧씌움 {relative_text(overlay_path)}: version {OVERLAY_VERSION} 만 받는다(C7)")
    assignments = overlay.get("assignments", [])
    require(isinstance(assignments, list) and all(isinstance(item, dict) and isinstance(item.get("json_path"), str) and "value" in item and isinstance(item.get("levels", []), list) for item in assignments),
            "덧씌움 assignments: {json_path, value, levels?(목록), …} 목록")
    for index, item in enumerate(assignments):
        require_tunable_path(item["json_path"], f"덧씌움 assignments[{index}]")
    actor_props = overlay.get("actor_props", [])
    require(isinstance(actor_props, list) and all(isinstance(item, dict) and all(isinstance(item.get(key), str) for key in ("level", "label", "property")) and "value" in item for item in actor_props),
            "덧씌움 actor_props: {level, label, property, component?, value} 목록")
    return {"path": relative_text(overlay_path), "sha256": file_sha256(overlay_path), "assignments": assignments, "actor_props": actor_props}


def check_overlay_overlap(plan, overlay):
    if overlay is None:
        return
    overlay_paths = {item["json_path"] for item in overlay["assignments"]}
    plan_paths = {path for param in plan["params"] for path in param_paths(param)} | {item["json_path"] for item in plan.get("base_tuning", [])}
    overlap = sorted(overlay_paths & plan_paths)
    require(not overlap, f"덧씌움 경로가 계획 params·base_tuning 경로와 같다: {', '.join(overlap)}")


def load_screen_result(screen_path, plan_path, overlay, lock_record, before):
    try:
        screen = json.loads(Path(screen_path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise PlanError(f"screen.json 을 읽지 못했다: {error}") from error
    require(isinstance(screen, dict) and screen.get("version") == SCREEN_RESULT_VERSION, f"screen.json: version {SCREEN_RESULT_VERSION} 만 받는다(C7)")
    expected = {"plan_sha256": file_sha256(plan_path), "overlay_sha256": overlay["sha256"] if overlay else None, "canonical_tuning_sha256": before["tuning"], "canonical_look_sha256": before["look"]}
    if lock_record is not None:
        expected["lock_sha256"] = lock_record["lock_sha256"]
    for key, value in expected.items():
        require(screen.get(key) == value, f"screen.json {key} 불일치: {screen.get(key)!r} ≠ 현재 {value!r}")
    if screen.get("insensitive") or not (screen.get("best") or {}).get("variant"):
        raise ConfirmRefused("screen.json 이 둔감(insensitive)이거나 best 가 없어 confirm 을 돌리지 않는다")
    return dict(screen, path=relative_text(screen_path), sha256=file_sha256(screen_path))


def build_stage_variants(stage, params, base_layer, screen):
    if stage in ("frame", "verify"):
        return [make_variant(CONTROL_ID, params, [param["base_value"] for param in params], base_layer)], (None, None)
    variants = build_screen_variants(params, base_layer)
    if stage == "screen":
        return variants, (None, None)
    best, second = confirm_candidates(screen, [variant["id"] for variant in variants])
    return [variant for variant in variants if variant["id"] in (CONTROL_ID, best, second)], (best, second)


def prepare_sweep(plan_path, run_dir, plan=None, stage=None, overlay=None, screen=None):
    plan = plan or load_plan(plan_path, stage)
    check_overlay_overlap(plan, overlay)
    schema = check_fluid_preset_data.load_schema(ROOT)
    document = fluid_tuning_patch.load_canonical_document(ROOT)
    look_document = json.loads(fluid_tuning_patch.canonical_look_path(ROOT).read_text(encoding="utf-8"))
    base_layer, base_document, base_look = build_base_layer(plan, overlay, document, look_document, schema)
    params = describe_params(plan, base_document, schema, base_look)
    if stage:
        variants, candidates = build_stage_variants(stage, params, base_layer, screen)
        sequence = stage_scene_sequence(stage, None, [dict(variant, scenes=None) for variant in variants], candidates)
    else:
        variants, candidates = build_variants(plan, params, base_layer), (None, None)
        sequence = build_sequence([variant["id"] for variant in variants if not variant["is_control"]], plan["repeats"], plan["control_every"], plan.get("control_middle", True))
    write_variant_copies(variants, run_dir, schema)
    variants_by_id = {variant["id"]: variant for variant in variants}
    control = variants_by_id[CONTROL_ID]
    base_variant = control if stage or base_layer["assignments"] else None
    sweep = {"tool": "calib_sweep", "version": 2, "stage": stage, "plan": relative_text(plan_path), "plan_sha256": file_sha256(plan_path), "plan_id": plan["id"], "run_dir": relative_text(run_dir),
             "base_copy": {"tuning": control["tuning"], "look": control["look"]}, "base_tuning": plan.get("base_tuning", []), "base_tuning_ships": plan.get("base_tuning_ships"),
             "params": params, "variants": variants, "sequence": sequence, "candidates": {"best": candidates[0], "second": candidates[1]}, "metrics": plan["metrics"], "scenes": []}
    for scene in plan["scenes"]:
        scene_dir = run_dir / scene["name"]
        scene_dir.mkdir(parents=True, exist_ok=True)
        scene_sequence = stage_scene_sequence(stage, scene["name"], variants, candidates) if stage else legacy_scene_sequence(plan, scene["name"], variants)
        sources = scene_sources(scene, plan)
        entry = {"name": scene["name"], "level": scene["level"], "output_dir": relative_text(scene_dir), "regions_template": scene.get("regions_template"), "targets": scene.get("targets"),
                 "live2_refs": scene.get("live2_refs", []), "uses_water_probe": sources == ["water_probe"], "sources": sources, "stage": stage, "sequence": scene_sequence,
                 "shots": scene.get("shots"), "shots_expected": scene_sample_count(scene, sources) if "look" in sources else None,
                 "settle": scene["settle"], "probe_window_scale": scene.get("probe_window_scale", 1.0), "out_of_scene_look_packages": out_of_scene_look_packages(params, scene["name"]), "steps": []}
        if sources == ["water_probe"] and overlay_scene_props(overlay, scene):
            entry["overlay_props"] = overlay_scene_props(overlay, scene)
        if not scene_sequence:
            entry["skipped"] = "이 단계의 변형이 이 장면에서 돌지 않는다(param scenes)"
        elif any(source in sources for source in ("look", "probe")):
            spec_path = scene_dir / LOOKDEV_SPEC_NAME
            spec_path.write_text(json.dumps(make_lookdev_spec(scene, plan, scene_sequence, variants_by_id, base_variant, overlay, entry["out_of_scene_look_packages"]), ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
            entry["spec"] = relative_text(spec_path)
        sweep["scenes"].append(entry)
    return plan, sweep


def run_scenes(plan, sweep, assert_results):
    variants_by_id = {variant["id"]: variant for variant in sweep["variants"]}
    base_variant = variants_by_id[CONTROL_ID] if sweep.get("stage") or variants_by_id[CONTROL_ID]["assignments"] else None
    for scene, entry in zip(plan["scenes"], sweep["scenes"]):
        if entry.get("skipped"):
            continue
        scene_dir = project_path(entry["output_dir"])
        started = time.monotonic()
        if entry["uses_water_probe"]:
            entry["water_probe"], entry["steps"] = run_water_probe_scene(scene, plan, entry["sequence"], variants_by_id, scene_dir, base_variant, entry.get("overlay_props"))
            entry["wall_seconds"] = round(time.monotonic() - started, 1)
            (scene_dir / WATER_PROBE_FILE_NAME).write_text(json.dumps(entry, ensure_ascii=False, indent=2, default=str) + "\n", encoding="utf-8", newline="\n")
            continue
        if entry["sources"] == ["offline"]:
            entry["steps"] = run_offline_scene(scene, entry, variants_by_id, scene_dir)
            entry["wall_seconds"] = round(time.monotonic() - started, 1)
            continue
        if entry["name"] in assert_results:
            result_path = project_path(assert_results[entry["name"]])
            entry["lookdev_exit"] = None
        else:
            entry["lookdev_exit"] = run_lookdev(project_path(entry["spec"]), scene["level"], scene_dir)
            result_path = scene_dir / LOOKDEV_RESULT_NAME
        entry["wall_seconds"] = round(time.monotonic() - started, 1)
        try:
            result = json.loads(result_path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            entry["error"] = f"룩데브 결과를 읽지 못했다: {error}"
            continue
        entry["lookdev_result"] = relative_text(result_path)
        entry["key_errors"] = result.get("key_errors", [])
        entry["steps"] = assert_lookdev_result(result, entry["sequence"], variants_by_id, result_path.parent, entry)
        if "offline" in entry["sources"]:
            merge_offline_metrics(scene, entry, variants_by_id, scene_dir)


def count_problems(sweep):
    problems = []
    for entry in sweep["scenes"]:
        if entry.get("error"):
            problems.append(f"{entry['name']}: {entry['error']}")
        if isinstance(entry.get("water_probe"), dict) and "error" in entry["water_probe"]:
            problems.append(f"{entry['name']}: {entry['water_probe']['error']}")
        if entry.get("lookdev_exit") not in (None, 0):
            problems.append(f"{entry['name']}: 룩데브 종료 {entry['lookdev_exit']}")
        problems.extend(f"{entry['name']}: {line}" for line in entry.get("key_errors", []))
        problems.extend(f"{entry['name']}/{step['name']}: {error}" for step in entry["steps"] for error in step["errors"])
        if not entry["steps"] and not entry.get("dry_run") and not entry.get("skipped"):
            problems.append(f"{entry['name']}: 단계 결과 없음")
    if sweep["canonical_sha256_after"] != sweep["canonical_sha256_before"]:
        problems.append("실행 전후 정본 sha256 이 다르다")
    return problems


def parse_assert_results(texts):
    results = {}
    for text in texts:
        name, separator, path = text.partition("=")
        if not separator:
            raise PlanError(f"--assert-scene {text!r}: <장면>=<result.json>")
        results[name] = path
    return results


def check_stage_arguments(arguments, plan):
    retired = [key for key in PLAN_RETIRED_KEYS if key in plan]
    require(arguments.dry_run or not retired, f"{', '.join(retired)}: B6 가 지울 초안 키가 남아 있어 실제 실행을 거부한다(건식은 경고)")
    require(arguments.stage != "confirm" or arguments.screen_result, "--stage confirm 은 --screen-result <screen.json> 이 필요하다")
    require(arguments.screen_result is None or arguments.stage == "confirm", "--screen-result 는 --stage confirm 에서만 쓴다")
    require(arguments.dry_run or arguments.stage not in LOCKED_STAGES or arguments.lock, "단계 실행(screen·confirm·verify)은 --lock <잠금 v2> 이 필요하다(C8)")


def main():
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description="FluidFX 장면×파라미터 보정 스윕(spec-cal0-v2 C1·G-PROTO)")
    parser.add_argument("--plan", required=True, type=Path)
    parser.add_argument("--run-dir", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--assert-scene", action="append", default=[])
    parser.add_argument("--stage", choices=STAGES)
    parser.add_argument("--lock", type=Path)
    parser.add_argument("--overlay", type=Path)
    parser.add_argument("--screen-result", type=Path)
    arguments = parser.parse_args()
    started_at = datetime.now().isoformat(timespec="seconds")
    try:
        plan_path = project_path(arguments.plan)
        assert_results = parse_assert_results(arguments.assert_scene)
        before = canonical_hashes()
        plan = load_plan(plan_path, arguments.stage)
        check_stage_arguments(arguments, plan)
        lock_record = read_lock(project_path(arguments.lock), plan_path, plan) if arguments.lock else None
        overlay = load_overlay(project_path(arguments.overlay)) if arguments.overlay else None
        screen = load_screen_result(project_path(arguments.screen_result), plan_path, overlay, lock_record, before) if arguments.screen_result else None
        run_dir = project_path(arguments.run_dir) if arguments.run_dir else CALIB_OUTPUT_ROOT / f"{plan['id']}-{arguments.stage or 'sweep'}-{datetime.now().strftime('%Y%m%d-%H%M%S')}"
        fluid_tuning_patch.require_under_saved(run_dir, ROOT)
        plan, sweep = prepare_sweep(plan_path, run_dir, plan, arguments.stage, overlay, screen)
    except ConfirmRefused as error:
        print(f"confirm 거부: {error}", file=sys.stderr)
        return EXIT_INVALID
    except (PlanError, fluid_tuning_patch.PatchError, check_fluid_preset_data.InputError, OSError) as error:
        print(f"계획 오류: {error}", file=sys.stderr)
        return EXIT_INPUT
    sweep.update({"started_at": started_at, "warnings": plan_warnings(plan, arguments.stage), "key_errors": [], "lock": None, "lock_sha256": None, "lock_created_at": None,
                  "overlay": overlay["path"] if overlay else None, "overlay_sha256": overlay["sha256"] if overlay else None,
                  "screen_result": screen["path"] if screen else None, "screen_result_sha256": screen["sha256"] if screen else None,
                  "canonical_tuning_sha256": before["tuning"], "canonical_look_sha256": before["look"], "canonical_sha256_before": before})
    sweep.update(lock_record or {})
    try:
        if arguments.dry_run:
            for entry in sweep["scenes"]:
                entry["dry_run"] = True
        else:
            run_scenes(plan, sweep, assert_results)
    finally:
        sweep["canonical_sha256_after"] = canonical_hashes()
        problems = count_problems(sweep)
        sweep["problems"] = problems
        sweep["invalid_count"] = len(problems)
        (run_dir / SWEEP_FILE_NAME).write_text(json.dumps(sweep, ensure_ascii=False, indent=2, default=str) + "\n", encoding="utf-8", newline="\n")
    print("스윕", relative_text(run_dir / SWEEP_FILE_NAME), f"단계 {sweep['stage'] or '옛 스윕'}, 변형 {len(sweep['variants'])}개, 단계 {len(sweep['sequence'])}개, 장면 {len(sweep['scenes'])}개")
    for line in sweep["warnings"]:
        print("경고:", line)
    for line in problems:
        print("무효:", line)
    return EXIT_INVALID if problems else 0


if __name__ == "__main__":
    sys.exit(main())
