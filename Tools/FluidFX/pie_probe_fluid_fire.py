"""실행 환경: 시스템 Python(언리얼 에디터가 TDGame을 열고 MCP 포트 8000·원격 실행이 켜진 상태, FX-3 C++ 재빌드·재시작과 editor_make_fluid_fire_stage.py 성공 뒤, 에디터 앞창 유지 L-editor-06). LV_TDFluidFireStage PIE에서 장면 3개(횃불 걷기·모닥불·폭발 블록)를 틱마다 표본으로 재고, 로그의 TDFluidPerf 줄과 ProfileGPU 가시성 A/B를 모은다.
실행 명령: python Tools/FluidFX/pie_probe_fluid_fire.py [--headless-reference Saved/FluidFX/fluid_fire_headless_reference.json] [--stop-pie] (PowerShell)
출력: Docs/Validation/FluidFX/D-49-fx3-probe-<날짜>.json(우리 측정값만, 헤드리스 기준이 있으면 ±30% 판정 포함), Saved/FluidFX/fluid_fire_probe_gpu_<라벨>.txt. 종료 코드 0 판정 항목 전부 통과/1 실패 항목 있음/2 환경 문제
상태: 검증됨(2026-10-18 FX-3 불 무대 생성·쇼케이스·탐침 성공). B0 파쇄 관문(trigger_break_at 1초 뒤 움직인 리프 70% 이상)과 헤드리스 값 대조는 이 스크립트가 기록만 하고 부모가 판정한다.
"""
import argparse
import json
import re
import sys
import time
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools"))
sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))
import pie_check_fluid  # noqa: E402
import run_in_editor  # noqa: E402
import uemcp  # noqa: E402

LOG_PATH = ROOT / "Saved" / "Logs" / "TDGame.log"
OUTPUT_DIRECTORY = ROOT / "Saved" / "FluidFX"
EVIDENCE_PATH = ROOT / "Docs" / "Validation" / "FluidFX" / f"D-49-fx3-probe-{date.today().isoformat()}.json"
DEFAULT_REFERENCE_PATH = OUTPUT_DIRECTORY / "fluid_fire_headless_reference.json"
FIRE_LEVEL_PATH = "/Game/FluidFX/Maps/LV_TDFluidFireStage"
RESULT_MARKER = "TD_FLUID_FIRE_PROBE_RESULT"
COMMAND_MARKER = "TD_FIRE_PROBE_CMD"
PERF_CONSOLE_VARIABLE = "TD.FluidFX.LogTickCost"
PERF_LINE_PATTERN = re.compile(r"TDFluidPerf: frames=(\d+) avg_tick_ms=(\S+) max_tick_ms=(\S+) avg_frame_ms=(\S+) awake_actors=(\d+) total_cells=(\d+)")
PROBLEM_LINE_PATTERN = re.compile(r"\bError:|Ensure condition failed|Assertion failed|Fatal error")
GPU_LINE_PATTERN = re.compile(r"Translucen|Total|TDFluid|Fluid", re.IGNORECASE)

HEADLESS_TOLERANCE = 0.30
TORCH_LEG_SECONDS = 2.5
TORCH_CORNERS = ((-300.0, -300.0), (300.0, -300.0), (300.0, 300.0), (-300.0, 300.0), (-300.0, -300.0))
TORCH_SETTLE_SECONDS = 3.0
TORCH_STAND_SECONDS = 3.0
CAMPFIRE_HOLD = (-250.0, 1600.0)
CAMPFIRE_SETTLE_SECONDS = 6.0
CAMPFIRE_MEASURE_SECONDS = 10.0
EXPLOSION_PAWN = (-600.0, 3200.0)
EXPLOSION_HIT_OFFSET = (-55.0, 0.0, -35.0)
EXPLOSION_TRIGGER_AFTER_SECONDS = 1.0
EXPLOSION_MEASURE_SECONDS = 7.0
GPU_PROFILE_SETTLE_SECONDS = 1.5
GPU_LOG_WAIT_SECONDS = 3.0
GPU_LOG_LINES = 260
CPU_BUDGET_MS = {"torch": 1.0, "campfire": 2.0, "explosion": 2.5}
EXIT_FAILED = 1
EXIT_ENVIRONMENT = 2

EDITOR_CODE = r'''
import json
import math
import traceback

import unreal

PARAMS = json.loads(__PARAMS__)
RESULT_MARKER = "TD_FLUID_FIRE_PROBE_RESULT"
HANDLE_ATTRIBUTE = "td_fire_probe_handle"
SAMPLES_ATTRIBUTE = "td_fire_probe_samples"
PHASE_ATTRIBUTE = "td_fire_probe_phase"
SOCKET_NAME = "hand_l"
TORCH_STANDING_LIGHT_MIN_SAMPLES = 30
SCENE_LABELS = {
    "torch": ("TDGen_TorchFire",),
    "campfire": ("TDGen_CampfireFire", "TDGen_CampfireSmoke"),
    "explosion": ("TDGen_ExplosionFire", "TDGen_ExplosionSmoke"),
}


def game_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def player_pawn_and_controller(world):
    return unreal.GameplayStatics.get_player_pawn(world, 0), unreal.GameplayStatics.get_player_controller(world, 0)


def live_actors(world):
    return list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDFluidLiveActor))


def find_live_actor(world, label):
    for actor in live_actors(world):
        if actor.get_actor_label() == label:
            return actor
    return None


def mean(values):
    return sum(values) / len(values) if values else None


def standard_deviation(values):
    if len(values) < 2:
        return None
    average = mean(values)
    return math.sqrt(sum((value - average) ** 2 for value in values) / len(values))


def coefficient_of_variation(values):
    average = mean(values)
    deviation = standard_deviation(values)
    if not average or deviation is None:
        return None
    return deviation / average


def unpack_centroid(result):
    if not isinstance(result, tuple):
        return None
    values = list(result)
    if isinstance(values[0], bool):
        if not values[0]:
            return None
        values = values[1:]
    if len(values) < 2:
        return None
    return [values[0].x, values[0].y, values[0].z, float(values[1])]


def centroid_of(actor, use_selective):
    return unpack_centroid(actor.get_editor_property("live_component").compute_density_centroid_world(use_selective))


def teleport_pawn(world, x, y):
    pawn, controller = player_pawn_and_controller(world)
    if controller is not None:
        controller.stop_movement()
    location = pawn.get_actor_location()
    pawn.set_actor_location(unreal.Vector(x, y, location.z), False, True)
    return pawn, controller


def action_status(world):
    pawn, controller = player_pawn_and_controller(world)
    mesh = pawn.get_component_by_class(unreal.SkeletalMeshComponent) if pawn else None
    return {
        "map": world.get_path_name(),
        "socket_exists": bool(mesh.does_socket_exist(SOCKET_NAME)) if mesh else None,
        "live_actors": {actor.get_actor_label(): bool(actor.get_editor_property("live_component").is_simulation_awake()) for actor in live_actors(world)},
    }


def action_console(world):
    unreal.SystemLibrary.execute_console_command(world, PARAMS["command"])
    return {"command": PARAMS["command"]}


def action_scene_begin(world):
    awake_labels = set(SCENE_LABELS[PARAMS["scene"]])
    states = {}
    for actor in live_actors(world):
        is_awake = actor.get_actor_label() in awake_labels
        actor.get_editor_property("live_component").set_simulation_awake(is_awake)
        states[actor.get_actor_label()] = is_awake
    return {"awake": states}


def action_move(world):
    pawn, controller = player_pawn_and_controller(world)
    if PARAMS.get("start_x") is not None:
        teleport_pawn(world, PARAMS["start_x"], PARAMS["start_y"])
    if PARAMS.get("goal_x") is None:
        return {"placed": True}
    goal = unreal.Vector(PARAMS["goal_x"], PARAMS["goal_y"], pawn.get_actor_location().z)
    extent = unreal.Vector(80.0, 80.0, 250.0)
    if unreal.NavigationSystemV1.project_point_to_navigation(world, goal, None, None, extent) is None:
        pawn.set_actor_location(goal, False, True)
        return {"mode": "teleport", "goal": [goal.x, goal.y]}
    unreal.AIHelperLibrary.simple_move_to_location(controller, goal)
    return {"mode": "navigation", "goal": [goal.x, goal.y]}


def action_phase(world):
    setattr(unreal, PHASE_ATTRIBUTE, PARAMS["label"])
    return {"phase": PARAMS["label"]}


def action_trigger(world):
    emitters = list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDFluidFragmentEmitterActor))
    if not emitters:
        return {"error": "조각 방출 액터가 없다"}
    source = emitters[0].get_editor_property("source_breakable_actor")
    if source is None:
        return {"error": "조각 방출 액터에 파괴 액터가 연결되지 않았다"}
    center = source.get_actor_location()
    hit = unreal.Vector(center.x + PARAMS["hit_dx"], center.y + PARAMS["hit_dy"], center.z + PARAMS["hit_dz"])
    setattr(unreal, PHASE_ATTRIBUTE, "after_trigger")
    emitters[0].trigger_break_at(hit)
    return {"hit": [hit.x, hit.y, hit.z], "game_time": unreal.GameplayStatics.get_time_seconds(world)}


def stop_sampler():
    handle = getattr(unreal, HANDLE_ATTRIBUTE, None)
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
        setattr(unreal, HANDLE_ATTRIBUTE, None)


def light_intensity(actor):
    return float(actor.get_editor_property("fire_light_component").get_last_intensity_candelas())


def ember_state(actor):
    ember = actor.get_editor_property("ember_component")
    return int(ember.get_alive_ember_count()), float(ember.get_mean_ember_heat())


def make_sampler(world, scene):
    actors = {label: find_live_actor(world, label) for label in SCENE_LABELS[scene]}
    pawn, controller = player_pawn_and_controller(world)
    mesh = pawn.get_component_by_class(unreal.SkeletalMeshComponent)
    emitters = list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDFluidFragmentEmitterActor))
    emitter = emitters[0] if emitters else None
    previous_stamps = [0]

    def sample(delta_seconds):
        current_world = game_world()
        if current_world is None:
            return None
        record = {"t": unreal.GameplayStatics.get_time_seconds(current_world), "dt": delta_seconds, "phase": getattr(unreal, PHASE_ATTRIBUTE, "")}
        if scene == "torch":
            fire = actors["TDGen_TorchFire"]
            location = pawn.get_actor_location()
            socket = mesh.get_socket_location(SOCKET_NAME)
            record["pawn"] = [location.x, location.y]
            record["socket"] = [socket.x, socket.y, socket.z]
            record["centroid"] = centroid_of(fire, True)
            record["light"] = light_intensity(fire)
            record["embers"], record["heat"] = ember_state(fire)
        elif scene == "campfire":
            fire = actors["TDGen_CampfireFire"]
            record["light"] = light_intensity(fire)
            record["embers"], record["heat"] = ember_state(fire)
            record["fire_centroid"] = centroid_of(fire, True)
            record["smoke_centroid"] = centroid_of(actors["TDGen_CampfireSmoke"], False)
        else:
            fire = actors["TDGen_ExplosionFire"]
            record["light"] = light_intensity(fire)
            record["fire_centroid"] = centroid_of(fire, True)
            record["smoke_centroid"] = centroid_of(actors["TDGen_ExplosionSmoke"], False)
            if emitter is not None:
                total_stamps = int(emitter.get_total_stamp_count())
                record["tracking"] = bool(emitter.is_tracking_fragments())
                record["tracked"] = int(emitter.get_tracked_leaf_count())
                record["moved"] = int(emitter.get_moved_leaf_count())
                record["stamped"] = int(emitter.get_stamped_leaf_count())
                record["stamps_this_frame"] = total_stamps - previous_stamps[0]
                previous_stamps[0] = total_stamps
                record["spread"] = fragment_spread(emitter)
        return record

    return sample


def fragment_spread(emitter):
    source = emitter.get_editor_property("source_breakable_actor")
    if source is None:
        return None
    collection = source.get_component_by_class(unreal.GeometryCollectionComponent)
    if collection is None:
        return None
    points = [transform.translation for transform in collection.get_current_transforms()]
    if not points:
        return None
    mean = unreal.Vector(sum(point.x for point in points) / len(points), sum(point.y for point in points) / len(points), sum(point.z for point in points) / len(points))
    return max((point - mean).length() for point in points)


def action_sampler_start(world):
    stop_sampler()
    samples = []
    setattr(unreal, SAMPLES_ATTRIBUTE, samples)
    setattr(unreal, PHASE_ATTRIBUTE, "")
    sample = make_sampler(world, PARAMS["scene"])

    def tick(delta_seconds):
        record = sample(delta_seconds)
        if record is not None:
            samples.append(record)

    setattr(unreal, HANDLE_ATTRIBUTE, unreal.register_slate_post_tick_callback(tick))
    return {"started": True, "scene": PARAMS["scene"]}


def area_series(samples, key, step_seconds=0.5):
    series = []
    next_time = None
    for record in samples:
        entry = record.get(key)
        if entry is None:
            continue
        if next_time is None:
            next_time = record["t"]
        if record["t"] >= next_time:
            series.append([round(record["t"] - samples[0]["t"], 2), round(entry[3], 1)])
            next_time += step_seconds
    return series


def steady_state_rate(samples, key, window_seconds=2.0):
    end_time = samples[-1]["t"]
    window = [record[key][3] for record in samples if record.get(key) is not None and record["t"] >= end_time - window_seconds]
    times = [record["t"] for record in samples if record.get(key) is not None and record["t"] >= end_time - window_seconds]
    if len(window) < 2 or times[-1] <= times[0] or not mean(window):
        return None
    return abs(window[-1] - window[0]) / (times[-1] - times[0]) / mean(window)


def axis_key(velocity_x, velocity_y):
    if abs(velocity_x) >= abs(velocity_y):
        return "+X" if velocity_x > 0 else "-X"
    return "+Y" if velocity_y > 0 else "-Y"


def summarize_torch(samples):
    groups = {}
    standing_offsets = []
    standing_lights = []
    moving_records = []
    previous = None
    for record in samples:
        if previous is not None and record["t"] > previous["t"] and record.get("centroid") is not None:
            elapsed = record["t"] - previous["t"]
            velocity_x = (record["pawn"][0] - previous["pawn"][0]) / elapsed
            velocity_y = (record["pawn"][1] - previous["pawn"][1]) / elapsed
            speed = math.hypot(velocity_x, velocity_y)
            offset = [record["centroid"][0] - record["socket"][0], record["centroid"][1] - record["socket"][1], record["centroid"][2] - record["socket"][2]]
            if speed > 250.0:
                moving_records.append((record, offset, velocity_x, velocity_y, speed))
            elif speed < 30.0:
                standing_offsets.append(offset + [record["centroid"][3]])
                standing_lights.append(record["light"])
        previous = record
    standing_xy = [mean([offset[index] for offset in standing_offsets]) for index in range(2)] if standing_offsets else [0.0, 0.0]
    for record, offset, velocity_x, velocity_y, speed in moving_records:
        relative_x = offset[0] - standing_xy[0]
        relative_y = offset[1] - standing_xy[1]
        drag = -(relative_x * velocity_x + relative_y * velocity_y) / speed
        groups.setdefault(axis_key(velocity_x, velocity_y), []).append({"t": record["t"], "drag": drag, "area": record["centroid"][3]})
    summary = {}
    for key, entries in groups.items():
        first_time = entries[0]["t"]
        settled = [entry["drag"] for entry in entries if entry["t"] - first_time >= 0.3]
        reached = [entry for entry in entries if entry["drag"] > 5.0]
        summary[key] = {
            "samples": len(entries),
            "mean_drag_cm": mean(settled),
            "seconds_to_5cm_drag": (reached[0]["t"] - first_time) if reached else None,
            "mean_area_cm2": mean([entry["area"] for entry in entries]),
        }
    drags = [value["mean_drag_cm"] for value in summary.values() if value["mean_drag_cm"] is not None]
    lights = [record["light"] for record in samples if record["t"] >= samples[0]["t"] + 1.0]
    return {
        "directions": summary,
        "mean_drag_cm": mean(drags),
        "standing_mean_offset_xyz_area": [mean([offset[index] for offset in standing_offsets]) for index in range(4)] if standing_offsets else None,
        "light_cv": coefficient_of_variation(standing_lights) if len(standing_lights) >= TORCH_STANDING_LIGHT_MIN_SAMPLES else None,
        "light_cv_while_walking_included": coefficient_of_variation(lights),
        "standing_light_samples": len(standing_lights),
        "light_mean_cd": mean(lights),
        "ember_alive_mean": mean([record["embers"] for record in samples]),
        "ember_alive_max": max(record["embers"] for record in samples),
    }


def summarize_campfire(samples):
    window = [record for record in samples if record["t"] >= samples[0]["t"] + 1.0]
    lights = [record["light"] for record in window]
    embers = [record["embers"] for record in window]
    fire_areas = [record["fire_centroid"][3] for record in window if record.get("fire_centroid")]
    smoke_areas = [record["smoke_centroid"][3] for record in window if record.get("smoke_centroid")]
    return {
        "light_cv": coefficient_of_variation(lights),
        "light_mean_cd": mean(lights),
        "ember_alive_min": min(embers) if embers else None,
        "ember_alive_mean": mean(embers),
        "ember_alive_max": max(embers) if embers else None,
        "fire_area_mean_cm2": mean(fire_areas),
        "smoke_area_mean_cm2": mean(smoke_areas),
        "smoke_area_series": area_series(samples, "smoke_centroid"),
        "smoke_steady_rate_per_sec": steady_state_rate(samples, "smoke_centroid"),
    }


def summarize_explosion(samples):
    trigger_time = next((record["t"] for record in samples if record["phase"] == "after_trigger"), None)
    if trigger_time is None:
        return {"error": "트리거 표본이 없다"}
    after = [record for record in samples if record["t"] >= trigger_time]
    at_one_second = min(after, key=lambda record: abs(record["t"] - (trigger_time + 1.0)))
    stamps_by_game_time = {}
    for record in after:
        if record["t"] <= trigger_time + 0.6:
            stamps_by_game_time[record["t"]] = stamps_by_game_time.get(record["t"], 0) + record.get("stamps_this_frame", 0)
    first_window = [{"t": game_time, "stamps_this_frame": stamps} for game_time, stamps in sorted(stamps_by_game_time.items())]
    tracking_end = next((record["t"] - trigger_time for record in after if "tracking" in record and not record["tracking"] and record["t"] > trigger_time + 0.2), None)
    lights = [record["light"] for record in after]
    peak = max(lights)
    peak_index = lights.index(peak)
    decayed = next((after[index]["t"] - after[peak_index]["t"] for index in range(peak_index, len(after)) if lights[index] <= 0.3 * peak), None)
    moved_ratio = (at_one_second["moved"] / at_one_second["tracked"]) if at_one_second.get("tracked") else None
    spread_before = next((record.get("spread") for record in reversed(samples) if record["t"] < trigger_time and record.get("spread") is not None), None)
    spread_after = at_one_second.get("spread")
    spread_ratio = (spread_after / spread_before) if spread_before and spread_after is not None else None
    stamped_ratio = (at_one_second["stamped"] / at_one_second["moved"]) if at_one_second.get("moved") else None
    return {
        "trigger_game_time": trigger_time,
        "tracked_leaf_count": at_one_second.get("tracked"),
        "moved_leaf_count_at_1s": at_one_second.get("moved"),
        "moved_ratio_at_1s": moved_ratio,
        "fragment_spread_cm_before": spread_before,
        "fragment_spread_cm_at_1s": spread_after,
        "fragment_spread_ratio_at_1s": spread_ratio,
        "stamped_leaf_count_at_1s": at_one_second.get("stamped"),
        "stamped_ratio_of_moved": stamped_ratio,
        "frames_in_first_0_6s": len(first_window),
        "frames_with_zero_stamps_in_first_0_6s": sum(1 for record in first_window if record.get("stamps_this_frame", 0) <= 0),
        "tracking_end_seconds": tracking_end,
        "light_peak_cd": peak,
        "light_seconds_from_peak_to_30_percent": decayed,
        "smoke_area_series": area_series(after, "smoke_centroid"),
        "fire_area_series": area_series(after, "fire_centroid"),
    }


def action_sampler_stop(world):
    stop_sampler()
    samples = getattr(unreal, SAMPLES_ATTRIBUTE, [])
    if not samples:
        return {"error": "표본이 없다"}
    dts = [record["dt"] for record in samples]
    summarizer = {"torch": summarize_torch, "campfire": summarize_campfire, "explosion": summarize_explosion}[PARAMS["scene"]]
    result = summarizer(samples)
    result["sample_count"] = len(samples)
    result["mean_dt_seconds"] = mean(dts)
    result["duration_seconds"] = samples[-1]["t"] - samples[0]["t"]
    return result


def action_ember_profile(world):
    fire = find_live_actor(world, "TDGen_CampfireFire")
    ember = fire.get_editor_property("ember_component")
    base_z = fire.get_actor_location().z
    try:
        custom = list(ember.get_editor_property("per_instance_sm_custom_data"))
    except Exception:
        custom = None
    bins = {"0-100": [], "100-200": [], "200+": []}
    alive = 0
    for index in range(int(ember.get_instance_count())):
        outcome = ember.get_instance_transform(index, True)
        transform = outcome[1] if isinstance(outcome, tuple) else outcome
        if transform.scale3d.x < 0.0001:
            continue
        alive += 1
        if custom is None or len(custom) < 2 * (index + 1):
            continue
        height = transform.translation.z - base_z
        bins["0-100" if height < 100.0 else "100-200" if height < 200.0 else "200+"].append(custom[2 * index])
    return {"alive": alive, "custom_data_available": custom is not None, "mean_heat_by_height_cm": {key: mean(values) for key, values in bins.items()}}


def action_gpu_profile(world):
    hidden = set(PARAMS["hide"])
    for actor in live_actors(world):
        actor.get_editor_property("internal_surface_mesh").set_visibility("cards" not in hidden)
        actor.get_editor_property("volume_mesh_component").set_visibility("volumes" not in hidden)
        ember = actor.get_editor_property("ember_component") if hasattr(actor, "ember_component") else None
        if ember is not None:
            ember.set_visibility("embers" not in hidden)
    unreal.log("TD_FIRE_PROBE_CMD " + PARAMS["label"])
    unreal.SystemLibrary.execute_console_command(world, "ProfileGPU")
    return {"label": PARAMS["label"], "hidden": sorted(hidden)}


def run():
    world = game_world()
    if world is None:
        return {"error": "PIE 게임 월드가 없다"}
    handler = globals().get("action_" + PARAMS["action"])
    if handler is None:
        return {"error": "알 수 없는 action: " + PARAMS["action"]}
    return handler(world)


try:
    result = run()
except Exception:
    result = {"error": traceback.format_exc()[-600:]}
print(RESULT_MARKER, json.dumps(result, ensure_ascii=False, default=str))
'''


def editor_action(action, **fields):
    params = {"action": action, **fields}
    outcome = run_in_editor.run_code(EDITOR_CODE.replace("__PARAMS__", repr(json.dumps(params))), timeout=30)
    if not outcome["connected"]:
        return {"error": "에디터 원격 실행에 연결하지 못했다"}
    for line in outcome["lines"]:
        index = line.find(RESULT_MARKER)
        if index >= 0:
            try:
                return json.loads(line[index + len(RESULT_MARKER):].strip().splitlines()[0])
            except ValueError:
                return {"error": "결과 해석 실패: " + line[-200:]}
    return {"error": "결과 표식 없음: " + " | ".join(outcome["lines"])[-300:]}


def open_stage_level():
    code = (
        "import unreal, json\n"
        "def open_stage():\n"
        "    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)\n"
        "    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)\n"
        f"    exists = unreal.EditorAssetLibrary.does_asset_exist('{FIRE_LEVEL_PATH}')\n"
        "    pie_running = editor.get_game_world() is not None\n"
        f"    loaded = exists and not pie_running and (editor.get_editor_world().get_path_name().startswith('{FIRE_LEVEL_PATH}') or levels.load_level('{FIRE_LEVEL_PATH}'))\n"
        f"    print('{pie_check_fluid.RESULT_MARKER}', json.dumps({{'exists': exists, 'pie_running': pie_running, 'loaded': bool(loaded), 'world': editor.get_editor_world().get_path_name()}}))\n"
        "open_stage()\n"
    )
    return pie_check_fluid.parse_marker(pie_check_fluid.run_editor_code(code), pie_check_fluid.RESULT_MARKER)


def log_size():
    return LOG_PATH.stat().st_size if LOG_PATH.exists() else 0


def read_log_since(offset):
    if not LOG_PATH.exists():
        return ""
    with open(LOG_PATH, "rb") as log_file:
        log_file.seek(offset)
        return log_file.read().decode("utf-8", errors="replace")


def summarize_perf_lines(text):
    lines = [PERF_LINE_PATTERN.search(line) for line in text.splitlines()]
    entries = [match for match in lines if match]
    if not entries:
        return {"lines": 0}
    frames = [int(match.group(1)) for match in entries]
    weighted_average = sum(float(match.group(2)) * count for match, count in zip(entries, frames)) / max(sum(frames), 1)
    return {
        "lines": len(entries),
        "avg_tick_ms": weighted_average,
        "max_tick_ms": max(float(match.group(3)) for match in entries),
        "avg_frame_ms": sum(float(match.group(4)) * count for match, count in zip(entries, frames)) / max(sum(frames), 1),
        "awake_actors": max(int(match.group(5)) for match in entries),
        "total_cells": max(int(match.group(6)) for match in entries),
    }


def find_problem_lines(text):
    matches = [line.strip()[:240] for line in text.splitlines() if PROBLEM_LINE_PATTERN.search(line)]
    return {"count": len(matches), "first": matches[:3]}


def run_torch_scene():
    editor_action("scene_begin", scene="torch")
    editor_action("move", start_x=TORCH_CORNERS[0][0], start_y=TORCH_CORNERS[0][1], goal_x=None, goal_y=None)
    time.sleep(TORCH_SETTLE_SECONDS)
    log_offset = log_size()
    editor_action("sampler_start", scene="torch")
    editor_action("phase", label="stand")
    time.sleep(TORCH_STAND_SECONDS)
    for leg in range(4):
        editor_action("phase", label=f"leg{leg}")
        goal = TORCH_CORNERS[leg + 1]
        editor_action("move", start_x=None, start_y=None, goal_x=goal[0], goal_y=goal[1])
        time.sleep(TORCH_LEG_SECONDS)
    summary = editor_action("sampler_stop", scene="torch")
    summary["cpu"] = summarize_perf_lines(read_log_since(log_offset))
    return summary


def run_gpu_ab(label_prefix):
    results = {}
    for name, hidden in (("all", []), ("without_cards", ["cards"]), ("without_volumes", ["volumes"]), ("without_embers", ["embers"])):
        label = f"{label_prefix}_{name}"
        offset = log_size()
        editor_action("gpu_profile", label=label, hide=hidden)
        time.sleep(GPU_LOG_WAIT_SECONDS)
        text = read_log_since(offset)
        marker_index = text.find(COMMAND_MARKER + " " + label)
        excerpt = text[marker_index:].splitlines()[:GPU_LOG_LINES] if marker_index >= 0 else []
        OUTPUT_DIRECTORY.mkdir(parents=True, exist_ok=True)
        dump_path = OUTPUT_DIRECTORY / f"fluid_fire_probe_gpu_{label}.txt"
        dump_path.write_text("\n".join(excerpt), encoding="utf-8")
        results[name] = {
            "dump": dump_path.relative_to(ROOT).as_posix(),
            "matching_lines": [line.strip()[:200] for line in excerpt if GPU_LINE_PATTERN.search(line)][:24],
        }
        time.sleep(GPU_PROFILE_SETTLE_SECONDS)
    editor_action("gpu_profile", label=label_prefix + "_restore", hide=[])
    return results


def run_campfire_scene():
    editor_action("scene_begin", scene="campfire")
    editor_action("move", start_x=CAMPFIRE_HOLD[0], start_y=CAMPFIRE_HOLD[1], goal_x=None, goal_y=None)
    time.sleep(CAMPFIRE_SETTLE_SECONDS)
    log_offset = log_size()
    editor_action("sampler_start", scene="campfire")
    time.sleep(CAMPFIRE_MEASURE_SECONDS)
    summary = editor_action("sampler_stop", scene="campfire")
    summary["cpu"] = summarize_perf_lines(read_log_since(log_offset))
    summary["ember_profile"] = editor_action("ember_profile")
    summary["gpu_visibility_ab"] = run_gpu_ab("campfire")
    return summary


def run_explosion_scene():
    editor_action("scene_begin", scene="explosion")
    editor_action("move", start_x=EXPLOSION_PAWN[0], start_y=EXPLOSION_PAWN[1], goal_x=None, goal_y=None)
    time.sleep(TORCH_SETTLE_SECONDS)
    log_offset = log_size()
    editor_action("sampler_start", scene="explosion")
    time.sleep(EXPLOSION_TRIGGER_AFTER_SECONDS)
    trigger_offset = log_size()
    trigger = editor_action("trigger", hit_dx=EXPLOSION_HIT_OFFSET[0], hit_dy=EXPLOSION_HIT_OFFSET[1], hit_dz=EXPLOSION_HIT_OFFSET[2])
    time.sleep(EXPLOSION_MEASURE_SECONDS)
    summary = editor_action("sampler_stop", scene="explosion")
    summary["trigger"] = trigger
    summary["cpu"] = summarize_perf_lines(read_log_since(log_offset))
    summary["log_problems_after_trigger"] = find_problem_lines(read_log_since(trigger_offset))
    return summary


def read_headless_reference(path):
    if not path.exists():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}
    return data if isinstance(data, dict) else {}


def judge_against_reference(measured, reference):
    verdict = {}
    for key, value in measured.items():
        expected = reference.get(key)
        if value is None or not expected:
            verdict[key] = {"value": value, "reference": expected, "passed": None}
            continue
        ratio = value / expected
        verdict[key] = {"value": value, "reference": expected, "ratio": ratio, "passed": abs(ratio - 1.0) <= HEADLESS_TOLERANCE}
    return verdict


def collect_measured_values(scenes):
    torch = scenes.get("torch", {})
    campfire = scenes.get("campfire", {})
    return {
        "torch_drag_cm": torch.get("mean_drag_cm"),
        "torch_light_cv": torch.get("light_cv"),
        "campfire_light_cv": campfire.get("light_cv"),
        "campfire_ember_alive_mean": campfire.get("ember_alive_mean"),
        "campfire_fire_area_cm2": campfire.get("fire_area_mean_cm2"),
        "campfire_smoke_area_cm2": campfire.get("smoke_area_mean_cm2"),
    }


def collect_budget_verdict(scenes):
    verdict = {}
    for name, budget in CPU_BUDGET_MS.items():
        average = scenes.get(name, {}).get("cpu", {}).get("avg_tick_ms")
        verdict[name] = {"avg_tick_ms": average, "budget_ms": budget, "passed": (average <= budget) if average is not None else None}
    return verdict


def collect_rule_verdict(scenes):
    explosion = scenes.get("explosion", {})
    campfire = scenes.get("campfire", {})
    torch = scenes.get("torch", {})
    ember_alive = campfire.get("ember_alive_mean")
    return {
        "light_cv_in_0.05_0.30": {
            "torch": torch.get("light_cv"),
            "campfire": campfire.get("light_cv"),
            "passed": all(value is not None and 0.05 <= value <= 0.30 for value in (torch.get("light_cv"), campfire.get("light_cv"))),
        },
        "ember_alive_20_256": {"value": ember_alive, "passed": ember_alive is not None and 20 <= ember_alive <= 256},
        "smoke_steady_under_5_percent_per_sec": {
            "value": campfire.get("smoke_steady_rate_per_sec"),
            "passed": campfire.get("smoke_steady_rate_per_sec") is not None and campfire["smoke_steady_rate_per_sec"] < 0.05,
        },
        "b0_moved_leaf_ratio_at_least_0.7": {"value": explosion.get("moved_ratio_at_1s"), "passed": explosion.get("moved_ratio_at_1s") is not None and explosion["moved_ratio_at_1s"] >= 0.7},
        "b0_fragments_scatter_spread_ratio_at_least_2": {"value": explosion.get("fragment_spread_ratio_at_1s"), "passed": explosion.get("fragment_spread_ratio_at_1s") is not None and explosion["fragment_spread_ratio_at_1s"] >= 2.0},
        "stamped_leaf_ratio_at_least_0.7": {"value": explosion.get("stamped_ratio_of_moved"), "passed": explosion.get("stamped_ratio_of_moved") is not None and explosion["stamped_ratio_of_moved"] >= 0.7},
        "stamps_every_frame_first_0_6s": {"value": explosion.get("frames_with_zero_stamps_in_first_0_6s"), "passed": explosion.get("frames_with_zero_stamps_in_first_0_6s") == 0},
        "tracking_ends_within_2_6s": {"value": explosion.get("tracking_end_seconds"), "passed": explosion.get("tracking_end_seconds") is not None and explosion["tracking_end_seconds"] <= 2.6},
        "light_decays_to_30_percent_within_2_5s": {"value": explosion.get("light_seconds_from_peak_to_30_percent"), "passed": explosion.get("light_seconds_from_peak_to_30_percent") is not None and explosion["light_seconds_from_peak_to_30_percent"] <= 2.5},
        "no_errors_after_trigger": {"value": explosion.get("log_problems_after_trigger"), "passed": explosion.get("log_problems_after_trigger", {}).get("count") == 0},
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--headless-reference", default=str(DEFAULT_REFERENCE_PATH))
    parser.add_argument("--stop-pie", action="store_true", help="끝난 뒤 PIE를 끈다(기본은 켜 둔다)")
    arguments = parser.parse_args()
    print("경고: 측정 동안 UnrealEditor 창을 앞창으로 두고 건드리지 않는다(L-editor-06).", flush=True)

    level_status = open_stage_level()
    if not level_status.get("loaded"):
        print("레벨을 열지 못했다:", json.dumps(level_status, ensure_ascii=False))
        return EXIT_ENVIRONMENT
    try:
        session = uemcp.connect()
    except (OSError, ValueError, KeyError) as error:
        print("MCP 서버에 연결하지 못했다:", error)
        return EXIT_ENVIRONMENT
    environment = {"navigation_ready": pie_check_fluid.wait_for_editor_navigation()}
    if not pie_check_fluid.start_pie(session, environment) or not pie_check_fluid.wait_for_pie_state(True, pie_check_fluid.PIE_START_WAIT_SECONDS):
        print("PIE를 시작하지 못했다:", json.dumps(environment, ensure_ascii=False)[:300])
        return EXIT_ENVIRONMENT
    time.sleep(3.0)

    status = editor_action("status")
    editor_action("console", command=f"{PERF_CONSOLE_VARIABLE} 1")
    scenes = {}
    try:
        scenes["torch"] = run_torch_scene()
        scenes["campfire"] = run_campfire_scene()
        scenes["explosion"] = run_explosion_scene()
    finally:
        editor_action("console", command=f"{PERF_CONSOLE_VARIABLE} 0")
        if arguments.stop_pie:
            pie_check_fluid.stop_pie(session, environment)

    reference_path = Path(arguments.headless_reference)
    reference = read_headless_reference(reference_path)
    measured = collect_measured_values(scenes)
    evidence = {
        "date": date.today().isoformat(),
        "level": FIRE_LEVEL_PATH,
        "status": status,
        "environment": environment,
        "scenes": scenes,
        "measured": measured,
        "headless_reference_path": reference_path.as_posix() if reference else None,
        "headless_judgement": judge_against_reference(measured, reference),
        "cpu_budget": collect_budget_verdict(scenes),
        "rules": collect_rule_verdict(scenes),
        "notes": [
            "불티 스텝·조각 추적 개별 비용은 C++ 계측 없이 잴 수 없어 서브시스템 틱 평균만 기록한다",
            "ProfileGPU 출력 형식은 엔진 버전에 따라 달라 Saved/FluidFX/fluid_fire_probe_gpu_*.txt 원문을 함께 본다",
        ],
    }
    EVIDENCE_PATH.parent.mkdir(parents=True, exist_ok=True)
    EVIDENCE_PATH.write_text(json.dumps(evidence, ensure_ascii=False, indent=2, default=str), encoding="utf-8")
    print("증거", EVIDENCE_PATH.relative_to(ROOT).as_posix())
    failed = [name for name, item in evidence["rules"].items() if item.get("passed") is False]
    for name in failed:
        print("실패:", name, json.dumps(evidence["rules"][name], ensure_ascii=False)[:200])
    return EXIT_FAILED if failed else 0


if __name__ == "__main__":
    sys.exit(main())
