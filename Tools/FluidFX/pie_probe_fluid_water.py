"""실행 환경: 시스템 Python(언리얼 에디터가 TDGame을 열고 MCP 포트 8000·원격 실행이 켜진 상태, C++ 묶음 B1 재빌드·재시작과 editor_make_fluid_water_material.py·editor_make_fluid_water_stage.py(scenes [0, 1]) 성공 뒤, 에디터 앞창 유지 L-editor-06). LV_TDFluidWaterStage 장면 1(강) PIE 에서 명세 acceptance [W-1 강, PIE 수치]를 셀 표본 직접 단언으로 잰다.
실행 명령: python Tools/FluidFX/pie_probe_fluid_water.py [--runs 2] [--stop-pie] (PowerShell, 좌표는 Saved/FluidFX/fluid_water_stage.json 의 river.probe_points)
출력: Docs/Validation/FluidFX/D-49-fxw1-probe-<날짜>.json(실행마다 판정 항목·측정값, 연속 통과 판정). 종료 코드 0 모든 실행 모든 항목 통과/1 실패 항목 있음/2 환경 문제
상태: 미검증(C1 작성분, B1 C++ 이름은 명세 가정). 화면 항목(태양 반점 포화 비율·TSR 번짐·창 경계 이음매)과 창 이동 프레임 p95·GPU 시간은 재지 않고 부모가 PIE 화면과 ProfileGPU 로 판정한다.
"""
import argparse
import ast
import json
import math
import re
import sys
import time
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools"))
sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))
import pie_check_fluid
import run_in_editor
import uemcp

LOG_PATH = ROOT / "Saved" / "Logs" / "TDGame.log"
STAGE_REPORT_PATH = ROOT / "Saved" / "FluidFX" / "fluid_water_stage.json"
MATERIAL_SCRIPT_PATH = ROOT / "Tools" / "FluidFX" / "editor_make_fluid_water_material.py"
EVIDENCE_PATH = ROOT / "Docs" / "Validation" / "FluidFX" / f"D-49-fxw1-probe-{date.today().isoformat()}.json"
WATER_LEVEL_PATH = "/Game/FluidFX/Maps/LV_TDFluidWaterStage"
RESULT_MARKER = "TD_FLUID_WATER_PROBE_RESULT"
NAVIGATION_MARKER = "TD_FLUID_WATER_NAV"
PERF_CONSOLE_VARIABLE = "TD.FluidFX.LogTickCost"
PERF_LINE_PATTERN = re.compile(r"TDFluidPerf: frames=(\d+) avg_tick_ms=(\S+) max_tick_ms=(\S+) avg_frame_ms=(\S+) awake_actors=(\d+) total_cells=(\d+)")
PROBLEM_LINE_PATTERN = re.compile(r"\bError:|Ensure condition failed|Assertion failed|Fatal error")
BENIGN_NAVIGATION_ENSURE_MARKER = "NavigationSystem.h] [Line: 293]"
OUTPUT_LINE_PREFIX = "[Info] "
SURFACE_TAG = "TDFluidWaterSurface"
REGION_TAG = "TDFluidWaterRegion"

FLOW_TARGET_TOLERANCE = 0.25
SPEED_RATIO_MINIMUM = 1.5
ROCK_WAKE_DENSITY_MINIMUM = 0.3
ROCK_LATERAL_MINIMUM_CM_PER_SEC = 3.0
CROSSING_RIPPLE_MINIMUM_CM = 1.0
CROSSING_MINIMUM_DEPTH_CM = 15.0
CROSSING_MINIMUM_SECONDS_IN_WATER = 0.3
FOAM_SHIFT_MINIMUM_CM = 150.0
FOAM_SHIFT_REFERENCE_SECONDS = 2.0
FOAM_MINIMUM_INTERVAL_SECONDS = 1.0
FOAM_MASS_MINIMUM = 1.0
DRY_BANK_SPEED_MAXIMUM_CM_PER_SEC = 0.5
DRY_BANK_KNOWN_MINIMUM = 0.9
WETNESS_REFERENCE_MINIMUM = 0.5
BANK_WET_MINIMUM = 0.3
BANK_FAR_MAXIMUM = 0.05
SHARED_MID_RANGE = (1, 4)
LAYER_ALPHA_ZERO_MAXIMUM = 0.02
LAYER_RAMP_RANGE_SECONDS = (0.5, 1.6)
WIDE_DEPTH_TOLERANCE_CM = 15.0
RIFFLE_DEPTH_TOLERANCE_CM = 10.0
AWAKE_TICK_BUDGET_MS = 3.0
ASLEEP_TICK_BUDGET_MS = 0.01

POINT_SAMPLE_INTERVAL_SECONDS = 0.1
CROSSING_SAMPLE_INTERVAL_SECONDS = 0.05
SLEEP_MARGIN_SECONDS = 4.0
PERF_MEASURE_SECONDS = 6.0
WAKE_OBSERVE_SECONDS = 6.0
FLOW_SETTLE_SECONDS = 8.0
FLOW_MEASURE_SECONDS = 6.0
ROCK_SETTLE_SECONDS = 8.0
ROCK_MEASURE_SECONDS = 4.0
SUBMERGED_SETTLE_SECONDS = 7.0
SUBMERGED_MEASURE_SECONDS = 3.0
CROSSING_SETTLE_SECONDS = 7.0
CROSSING_TIMEOUT_SECONDS = 14.0
CROSSING_POLL_SECONDS = 0.25
CROSSING_GOAL_TOLERANCE_CM = 80.0
FOAM_SNAPSHOT_GAP_SECONDS = 1.0
NAVIGATION_WAIT_SECONDS = 150.0
NAVIGATION_POLL_SECONDS = 5.0
ROCK_WAKE_DISTANCES_CM = (60.0, 120.0, 180.0, 240.0, 300.0)
ROCK_WAKE_LATERALS_CM = (("c", 0.0), ("l", 40.0), ("r", -40.0))
EXIT_FAILED = 1
EXIT_ENVIRONMENT = 2

EDITOR_CODE = r'''
import json
import math
import traceback

import unreal

PARAMS = json.loads(__PARAMS__)
RESULT_MARKER = "TD_FLUID_WATER_PROBE_RESULT"
HANDLE_ATTRIBUTE = "td_water_probe_handle"
SAMPLES_ATTRIBUTE = "td_water_probe_samples"
META_ATTRIBUTE = "td_water_probe_meta"
FOAM_BASELINE_ATTRIBUTE = "td_water_probe_foam_baseline"
FOAM_NOISE_ATTRIBUTE = "td_water_probe_foam_noise"
FOAM_NOISE_MARGIN = 0.01
FOAM_NOISE_PERCENTILE = 0.95
SAMPLE_PROPERTIES = (
    "primary_density",
    "obstacle_sdf",
    "velocity_cm_per_sec",
    "water_depth_cm",
    "wetness",
    "flow_guide_weight",
    "flow_target_velocity",
    "ripple_height_cm",
    "is_terrain_known",
)
RING_RADII_CM = (50.0, 100.0)
RING_DIRECTIONS = 8


def game_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def pawn_and_controller(world):
    return unreal.GameplayStatics.get_player_pawn(world, 0), unreal.GameplayStatics.get_player_controller(world, 0)


def water_actors(world):
    return list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDFluidWaterActor))


def water_actor(world):
    found = water_actors(world)
    return found[0] if found else None


def water_component(world):
    actor = water_actor(world)
    return actor.get_editor_property("live_component") if actor is not None else None


def has_property(owner, name):
    try:
        owner.get_editor_property(name)
    except Exception:
        return False
    return True


def read_property(owner, name, default=None):
    try:
        return owner.get_editor_property(name)
    except Exception:
        return default


def mean(values):
    return sum(values) / len(values) if values else None


def read_cell(component, x_cm, y_cm, z_cm):
    outcome = component.sample_fluid_at_world_location(unreal.Vector(x_cm, y_cm, z_cm))
    if outcome is None:
        return None
    if isinstance(outcome, tuple):
        if not outcome[0]:
            return None
        outcome = outcome[1]
    sample = outcome
    velocity = sample.get_editor_property("velocity_cm_per_sec")
    target = sample.get_editor_property("flow_target_velocity")
    return {
        "vx": float(velocity.x),
        "vy": float(velocity.y),
        "tx": float(target.x),
        "ty": float(target.y),
        "density": float(sample.get_editor_property("primary_density")),
        "ripple": float(sample.get_editor_property("ripple_height_cm")),
        "wetness": float(sample.get_editor_property("wetness")),
        "depth": float(sample.get_editor_property("water_depth_cm")),
        "guide": float(sample.get_editor_property("flow_guide_weight")),
        "sdf": float(sample.get_editor_property("obstacle_sdf")),
        "known": bool(sample.get_editor_property("is_terrain_known")),
    }


def aggregate(records):
    if not records:
        return {"count": 0}
    mean_vx = mean([record["vx"] for record in records])
    mean_vy = mean([record["vy"] for record in records])
    mean_tx = mean([record["tx"] for record in records])
    mean_ty = mean([record["ty"] for record in records])
    target_norm = math.hypot(mean_tx, mean_ty)
    error = math.hypot(mean_vx - mean_tx, mean_vy - mean_ty)
    return {
        "count": len(records),
        "velocity": [mean_vx, mean_vy],
        "target": [mean_tx, mean_ty],
        "speed_mean": mean([math.hypot(record["vx"], record["vy"]) for record in records]),
        "target_speed": target_norm,
        "relative_error": error / target_norm if target_norm > 1e-3 else None,
        "density_mean": mean([record["density"] for record in records]),
        "density_max": max(record["density"] for record in records),
        "ripple_abs_max": max(abs(record["ripple"]) for record in records),
        "wetness_mean": mean([record["wetness"] for record in records]),
        "depth_mean": mean([record["depth"] for record in records]),
        "guide_mean": mean([record["guide"] for record in records]),
        "sdf_min": min(record["sdf"] for record in records),
        "sdf_mean": mean([record["sdf"] for record in records]),
        "known_fraction": mean([1.0 if record["known"] else 0.0 for record in records]),
    }


def game_time(world):
    return unreal.GameplayStatics.get_time_seconds(world)


def action_status(world):
    actor = water_actor(world)
    sample_struct = unreal.TDFluidCellSample()
    live = {}
    for live_actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDFluidLiveActor):
        live[live_actor.get_actor_label()] = bool(live_actor.get_editor_property("live_component").is_simulation_awake())
    return {
        "map": world.get_path_name(),
        "water_actor_count": len(water_actors(world)),
        "water_actor": actor.get_actor_label() if actor is not None else None,
        "sleep_delay_seconds": float(read_property(actor, "sleep_delay_seconds", 5.0)) if actor is not None else None,
        "fade_in_seconds": float(read_property(actor, "water_layer_fade_in_seconds", 1.0)) if actor is not None else None,
        "missing_sample_properties": [name for name in SAMPLE_PROPERTIES if not has_property(sample_struct, name)],
        "live_actors": live,
    }


def action_console(world):
    unreal.SystemLibrary.execute_console_command(world, PARAMS["command"])
    return {"command": PARAMS["command"]}


def action_isolate(world):
    states = {}
    for live_actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDFluidLiveActor):
        if isinstance(live_actor, unreal.TDFluidWaterActor):
            continue
        live_actor.get_editor_property("live_component").set_simulation_awake(False)
        states[live_actor.get_actor_label()] = False
    return {"slept": states}


def action_teleport(world):
    pawn, controller = pawn_and_controller(world)
    if controller is not None:
        controller.stop_movement()
    pawn.set_actor_location(unreal.Vector(PARAMS["x"], PARAMS["y"], PARAMS["z"]), False, True)
    location = pawn.get_actor_location()
    return {"pawn": [location.x, location.y, location.z]}


def action_move(world):
    pawn, controller = pawn_and_controller(world)
    goal = unreal.Vector(PARAMS["goal_x"], PARAMS["goal_y"], pawn.get_actor_location().z)
    extent = unreal.Vector(80.0, 80.0, 400.0)
    if unreal.NavigationSystemV1.project_point_to_navigation(world, goal, None, None, extent) is None:
        return {"mode": "unreachable", "goal": [goal.x, goal.y]}
    unreal.AIHelperLibrary.simple_move_to_location(controller, goal)
    return {"mode": "navigation", "goal": [goal.x, goal.y]}


def action_pawn_state(world):
    pawn, controller = pawn_and_controller(world)
    location = pawn.get_actor_location()
    velocity = pawn.get_velocity()
    return {"x": location.x, "y": location.y, "z": location.z, "speed": math.hypot(velocity.x, velocity.y)}


def action_water_state(world):
    actor = water_actor(world)
    component = actor.get_editor_property("live_component")
    return {"awake": bool(component.is_simulation_awake()), "layer_alpha": float(actor.get_water_layer_display_alpha()), "game_time": game_time(world)}


def ring_offsets():
    offsets = [(0.0, 0.0)]
    for radius in RING_RADII_CM:
        for index in range(RING_DIRECTIONS):
            angle = 2.0 * math.pi * index / RING_DIRECTIONS
            offsets.append((radius * math.cos(angle), radius * math.sin(angle)))
    return offsets


def stop_sampler():
    handle = getattr(unreal, HANDLE_ATTRIBUTE, None)
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
        setattr(unreal, HANDLE_ATTRIBUTE, None)


def make_points_sampler(world, meta):
    component = water_component(world)
    points = meta["points"]
    z_cm = meta["z"]

    def sample(delta_seconds):
        current_world = game_world()
        if current_world is None:
            return None
        record = {"t": game_time(current_world), "dt": delta_seconds}
        for name, point in points.items():
            record[name] = read_cell(component, point[0], point[1], z_cm)
        return record

    return sample


def make_wake_sampler(world, meta):
    actor = water_actor(world)
    component = actor.get_editor_property("live_component")

    def sample(delta_seconds):
        current_world = game_world()
        if current_world is None:
            return None
        return {
            "t": game_time(current_world),
            "dt": delta_seconds,
            "alpha": float(actor.get_water_layer_display_alpha()),
            "awake": bool(component.is_simulation_awake()),
        }

    return sample


def make_crossing_sampler(world, meta):
    component = water_component(world)
    pawn, controller = pawn_and_controller(world)
    offsets = ring_offsets()
    z_cm = meta["z"]

    def sample(delta_seconds):
        current_world = game_world()
        if current_world is None:
            return None
        location = pawn.get_actor_location()
        cells = [read_cell(component, location.x + dx, location.y + dy, z_cm) for dx, dy in offsets]
        valid = [cell for cell in cells if cell is not None]
        return {
            "t": game_time(current_world),
            "dt": delta_seconds,
            "pawn": [location.x, location.y],
            "center_depth": cells[0]["depth"] if cells[0] is not None else None,
            "ripple_abs_max": max(abs(cell["ripple"]) for cell in valid) if valid else None,
            "density_max": max(cell["density"] for cell in valid) if valid else None,
        }

    return sample


SAMPLER_FACTORIES = {"points": make_points_sampler, "wake": make_wake_sampler, "crossing": make_crossing_sampler}


def action_sampler_start(world):
    stop_sampler()
    meta = {key: value for key, value in PARAMS.items() if key != "action"}
    samples = []
    setattr(unreal, SAMPLES_ATTRIBUTE, samples)
    setattr(unreal, META_ATTRIBUTE, meta)
    sample = SAMPLER_FACTORIES[meta["kind"]](world, meta)
    interval = float(meta.get("interval", 0.0))
    last_time = [None]

    def tick(delta_seconds):
        current_world = game_world()
        if current_world is None:
            return
        now = game_time(current_world)
        elapsed = delta_seconds if last_time[0] is None else now - last_time[0]
        if last_time[0] is not None and elapsed < interval:
            return
        last_time[0] = now
        record = sample(elapsed)
        if record is not None:
            samples.append(record)

    setattr(unreal, HANDLE_ATTRIBUTE, unreal.register_slate_post_tick_callback(tick))
    return {"started": True, "kind": meta["kind"]}


def summarize_points(samples, meta):
    result = {}
    for name in meta["points"]:
        records = [record[name] for record in samples if record.get(name) is not None]
        entry = aggregate(records)
        entry["missing"] = len(samples) - len(records)
        result[name] = entry
    return result


def summarize_wake(samples, meta):
    first_awake = next((record for record in samples if record["awake"]), None)
    if first_awake is None:
        return {"error": "물 액터가 깨어나지 않았다", "sample_count": len(samples), "was_asleep_at_start": not samples[0]["awake"]}
    awake_samples = [record for record in samples if record["t"] >= first_awake["t"]]
    first_positive = next((record for record in awake_samples if record["alpha"] > 0.001), None)
    first_full = next((record for record in awake_samples if record["alpha"] >= 0.999), None)
    ramp = None
    monotone = None
    if first_positive is not None and first_full is not None:
        ramp = first_full["t"] - first_positive["t"]
        window = [record["alpha"] for record in awake_samples if first_positive["t"] <= record["t"] <= first_full["t"]]
        monotone = all(window[index + 1] >= window[index] - 1e-4 for index in range(len(window) - 1))
    return {
        "sample_count": len(samples),
        "was_asleep_at_start": not samples[0]["awake"],
        "awake_after_seconds": first_awake["t"] - samples[0]["t"],
        "alpha_at_first_awake": first_awake["alpha"],
        "terrain_wait_seconds": (first_positive["t"] - first_awake["t"]) if first_positive is not None else None,
        "ramp_seconds": ramp,
        "monotone": monotone,
        "final_alpha": samples[-1]["alpha"],
    }


def summarize_crossing(samples, meta):
    in_water = [record for record in samples if record["center_depth"] is not None and record["center_depth"] >= meta["min_depth"]]
    ripples = [record["ripple_abs_max"] for record in samples if record["ripple_abs_max"] is not None]
    depths = [record["center_depth"] for record in samples if record["center_depth"] is not None]
    return {
        "ticks": len(samples),
        "ticks_in_water": len(in_water),
        "seconds_in_water": sum(record["dt"] for record in in_water),
        "max_abs_ripple_cm": max(ripples) if ripples else None,
        "max_center_depth_cm": max(depths) if depths else None,
        "pawn_start": samples[0]["pawn"],
        "pawn_end": samples[-1]["pawn"],
        "duration_seconds": samples[-1]["t"] - samples[0]["t"],
    }


SUMMARIZERS = {"points": summarize_points, "wake": summarize_wake, "crossing": summarize_crossing}


def action_sampler_stop(world):
    stop_sampler()
    samples = getattr(unreal, SAMPLES_ATTRIBUTE, [])
    meta = getattr(unreal, META_ATTRIBUTE, None)
    if not samples or meta is None:
        return {"error": "표본이 없다"}
    result = SUMMARIZERS[meta["kind"]](samples, meta)
    result["sample_count"] = len(samples)
    result["mean_dt_seconds"] = mean([record["dt"] for record in samples])
    result["duration_seconds"] = samples[-1]["t"] - samples[0]["t"]
    return result


def action_foam_grid(world):
    component = water_component(world)
    origin = PARAMS["origin"]
    tangent = PARAMS["tangent"]
    normal = (-tangent[1], tangent[0])
    step = PARAMS["step"]
    grid = []
    distance = PARAMS["s0"]
    while distance <= PARAMS["s1"] + 1e-6:
        lateral = -PARAMS["half"]
        while lateral <= PARAMS["half"] + 1e-6:
            grid.append((distance, origin[0] + tangent[0] * distance + normal[0] * lateral, origin[1] + tangent[1] * distance + normal[1] * lateral))
            lateral += step
        distance += step
    densities = []
    missing = 0
    for _, x_cm, y_cm in grid:
        cell = read_cell(component, x_cm, y_cm, PARAMS["z"])
        if cell is None:
            missing += 1
        densities.append(cell["density"] if cell is not None else 0.0)
    if PARAMS["mode"] == "baseline":
        setattr(unreal, FOAM_BASELINE_ATTRIBUTE, densities)
        setattr(unreal, FOAM_NOISE_ATTRIBUTE, 0.0)
        return {"points": len(grid), "missing": missing, "baseline_mass": sum(densities), "baseline_max": max(densities), "game_time": game_time(world)}
    baseline = getattr(unreal, FOAM_BASELINE_ATTRIBUTE, None)
    if baseline is None or len(baseline) != len(densities):
        return {"error": "거품 기준 격자가 없다"}
    if PARAMS["mode"] == "noise":
        fluctuations = sorted(max(0.0, density - base) for density, base in zip(densities, baseline))
        noise = (fluctuations[int(FOAM_NOISE_PERCENTILE * (len(fluctuations) - 1))] if fluctuations else 0.0) + FOAM_NOISE_MARGIN
        setattr(unreal, FOAM_NOISE_ATTRIBUTE, noise)
        return {"points": len(grid), "missing": missing, "noise_threshold": noise, "game_time": game_time(world)}
    noise_threshold = getattr(unreal, FOAM_NOISE_ATTRIBUTE, 0.0)
    excess = [value if value > noise_threshold else 0.0 for value in (max(0.0, density - base) for density, base in zip(densities, baseline))]
    mass = sum(excess)
    profile = {}
    for value, point in zip(excess, grid):
        key = round(point[0], 3)
        profile[key] = profile.get(key, 0.0) + value
    profile_s = sorted(profile)
    return {
        "points": len(grid),
        "missing": missing,
        "excess_mass": mass,
        "excess_max": max(excess),
        "centroid_s_cm": (sum(value * point[0] for value, point in zip(excess, grid)) / mass) if mass > 1e-6 else None,
        "profile_s_cm": profile_s,
        "profile": [profile[key] for key in profile_s],
        "game_time": game_time(world),
    }


def base_material_of(material):
    try:
        return material.get_base_material()
    except Exception:
        pass
    current = material
    for _ in range(8):
        if isinstance(current, unreal.Material):
            return current
        current = read_property(current, "parent")
        if current is None:
            return None
    return None


def describe_material_instance(mid):
    base = base_material_of(mid)
    editing = unreal.MaterialEditingLibrary
    declared = {"scalar": set(), "vector": set(), "texture": set()}
    if base is not None:
        declared["scalar"] = {str(name) for name in editing.get_scalar_parameter_names(base)}
        declared["vector"] = {str(name) for name in editing.get_vector_parameter_names(base)}
        declared["texture"] = {str(name) for name in editing.get_texture_parameter_names(base)}
    scalars = {name: float(mid.get_scalar_parameter_value(name)) for name in PARAMS["scalar_names"]}
    vectors = {}
    for name in PARAMS["vector_names"]:
        color = mid.get_vector_parameter_value(name)
        vectors[name] = [color.r, color.g, color.b, color.a]
    textures = {name: mid.get_texture_parameter_value(name) is not None for name in PARAMS["texture_names"]}
    undeclared = [name for name in PARAMS["scalar_names"] if name not in declared["scalar"]]
    undeclared += [name for name in PARAMS["vector_names"] if name not in declared["vector"]]
    undeclared += [name for name in PARAMS["texture_names"] if name not in declared["texture"]]
    return {
        "name": mid.get_name(),
        "parent": mid.get_path_name(),
        "base_material": base.get_path_name() if base is not None else None,
        "scalars": scalars,
        "vectors": vectors,
        "textures": textures,
        "undeclared_in_material": undeclared,
    }


def action_mid_state(world):
    actor = water_actor(world)
    component = actor.get_editor_property("live_component")
    surface_actors = []
    mids = {}
    for surface_actor in unreal.GameplayStatics.get_all_actors_with_tag(world, PARAMS["surface_tag"]):
        tags = [str(tag) for tag in surface_actor.get_editor_property("tags")]
        entry = {"actor": surface_actor.get_actor_label(), "has_region_tag": PARAMS["region_tag"] in tags, "components": 0, "non_mid_slots": 0}
        for mesh in surface_actor.get_components_by_class(unreal.StaticMeshComponent):
            entry["components"] += 1
            material = mesh.get_material(0)
            if not isinstance(material, unreal.MaterialInstanceDynamic):
                entry["non_mid_slots"] += 1
                continue
            mids[material.get_name()] = material
        surface_actors.append(entry)
    return {
        "awake": bool(component.is_simulation_awake()),
        "layer_alpha": float(actor.get_water_layer_display_alpha()),
        "driven_surface_count": int(actor.get_driven_surface_count()),
        "shared_material_count": int(actor.get_shared_surface_material_count()),
        "surface_texture_bound": component.get_water_surface_texture() is not None,
        "surface_actors": surface_actors,
        "unique_mid_count": len(mids),
        "mids": [describe_material_instance(mid) for mid in mids.values()],
    }


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

NAVIGATION_CHECK_CODE = r'''
import json
import unreal

POINTS = json.loads(__POINTS__)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
extent = unreal.Vector(80.0, 80.0, 400.0)
projected = [unreal.NavigationSystemV1.project_point_to_navigation(world, unreal.Vector(point[0], point[1], point[2]), None, None, extent) is not None for point in POINTS]
print("TD_FLUID_WATER_NAV", json.dumps({"projected": projected, "building": bool(unreal.NavigationSystemV1.is_navigation_being_built_or_locked(world))}))
'''


def editor_action(action, **fields):
    params = {"action": action, **fields}
    outcome = run_in_editor.run_code(EDITOR_CODE.replace("__PARAMS__", repr(json.dumps(params))), timeout=30)
    if not outcome["connected"]:
        return {"error": "에디터 원격 실행에 연결하지 못했다"}
    joined = "".join(line[len(OUTPUT_LINE_PREFIX):] if line.startswith(OUTPUT_LINE_PREFIX) else line for line in outcome["lines"])
    index = joined.find(RESULT_MARKER)
    if index < 0:
        return {"error": "결과 표식 없음: " + joined[-300:]}
    remainder = joined[index + len(RESULT_MARKER):].lstrip()
    try:
        return json.JSONDecoder().raw_decode(remainder)[0]
    except ValueError:
        return {"error": "결과 해석 실패: " + remainder[:200]}


def open_stage_level():
    code = (
        "import unreal, json\n"
        "def open_stage():\n"
        "    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)\n"
        "    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)\n"
        f"    exists = unreal.EditorAssetLibrary.does_asset_exist('{WATER_LEVEL_PATH}')\n"
        "    pie_running = editor.get_game_world() is not None\n"
        f"    loaded = exists and not pie_running and (editor.get_editor_world().get_path_name().startswith('{WATER_LEVEL_PATH}') or levels.load_level('{WATER_LEVEL_PATH}'))\n"
        f"    print('{pie_check_fluid.RESULT_MARKER}', json.dumps({{'exists': exists, 'pie_running': pie_running, 'loaded': bool(loaded), 'world': editor.get_editor_world().get_path_name()}}))\n"
        "open_stage()\n"
    )
    return pie_check_fluid.parse_marker(pie_check_fluid.run_editor_code(code), pie_check_fluid.RESULT_MARKER)


def wait_for_river_navigation(points):
    probe_points = [points["crossing"]["start"], points["crossing"]["goal"], points["flow_pawn"]]
    code = NAVIGATION_CHECK_CODE.replace("__POINTS__", repr(json.dumps(probe_points)))
    started = time.monotonic()
    has_requested_rebuild = False
    state = {}
    while time.monotonic() - started < NAVIGATION_WAIT_SECONDS:
        state = pie_check_fluid.parse_marker(pie_check_fluid.run_editor_code(code), NAVIGATION_MARKER)
        print(NAVIGATION_MARKER, round(time.monotonic() - started, 1), json.dumps(state, ensure_ascii=False), flush=True)
        if state.get("projected") and all(state["projected"]) and not state.get("building"):
            return True, state
        if not has_requested_rebuild and not state.get("building"):
            pie_check_fluid.request_editor_navigation_rebuild()
            has_requested_rebuild = True
        time.sleep(NAVIGATION_POLL_SECONDS)
    return False, state


def log_size():
    return LOG_PATH.stat().st_size if LOG_PATH.exists() else 0


def read_log_since(offset):
    if not LOG_PATH.exists():
        return ""
    with open(LOG_PATH, "rb") as log_file:
        log_file.seek(offset)
        return log_file.read().decode("utf-8", errors="replace")


def summarize_perf_lines(text):
    entries = [match for match in (PERF_LINE_PATTERN.search(line) for line in text.splitlines()) if match]
    if not entries:
        return {"lines": 0}
    frames = [int(match.group(1)) for match in entries]
    total_frames = max(sum(frames), 1)
    return {
        "lines": len(entries),
        "avg_tick_ms": sum(float(match.group(2)) * count for match, count in zip(entries, frames)) / total_frames,
        "max_tick_ms": max(float(match.group(3)) for match in entries),
        "avg_frame_ms": sum(float(match.group(4)) * count for match, count in zip(entries, frames)) / total_frames,
        "awake_actors_max": max(int(match.group(5)) for match in entries),
        "awake_actors_min": min(int(match.group(5)) for match in entries),
        "total_cells": max(int(match.group(6)) for match in entries),
    }


def is_benign_python_navigation_ensure(block_lines):
    joined = "\n".join(block_lines)
    return BENIGN_NAVIGATION_ENSURE_MARKER in joined and "PythonScriptPlugin" in joined


def find_problem_lines(text):
    matches = []
    ignored_blocks = 0
    block = []
    for line in text.splitlines() + [""]:
        if block and ("LogOutputDevice: Error:" in line or "EnsureFailed:" in line):
            block.append(line)
            continue
        if block:
            if is_benign_python_navigation_ensure(block):
                ignored_blocks += 1
            else:
                matches.extend(entry.strip()[:240] for entry in block if PROBLEM_LINE_PATTERN.search(entry))
            block = []
        if "=== Handled ensure: ===" in line:
            block = [line]
            continue
        if PROBLEM_LINE_PATTERN.search(line) and not (BENIGN_NAVIGATION_ENSURE_MARKER in line and "EnsureFailed:" in line):
            matches.append(line.strip()[:240])
    return {"count": len(matches), "first": matches[:3], "ignored_python_navigation_ensures": ignored_blocks}


def read_material_contract():
    names = {"MID_DRIVEN_TEXTURE_NAMES": "texture_names", "MID_DRIVEN_VECTOR_NAMES": "vector_names", "MID_DRIVEN_SCALAR_NAMES": "scalar_names"}
    found = {}
    tree = ast.parse(MATERIAL_SCRIPT_PATH.read_text(encoding="utf-8"))
    for node in tree.body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name) and node.targets[0].id in names:
            found[names[node.targets[0].id]] = list(ast.literal_eval(node.value))
    return found


def read_stage_report():
    if not STAGE_REPORT_PATH.exists():
        return None
    try:
        report = json.loads(STAGE_REPORT_PATH.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    if not isinstance(report, dict) or report.get("status") != "ok" or not report.get("river"):
        return None
    return report


def teleport(position):
    return editor_action("teleport", x=position[0], y=position[1], z=position[2])


def point_beside(center, tangent, along_cm, lateral_cm):
    normal = (-tangent[1], tangent[0])
    return [center[0] + tangent[0] * along_cm + normal[0] * lateral_cm, center[1] + tangent[1] * along_cm + normal[1] * lateral_cm]


def build_flow_points(points):
    named = {}
    for index, point in enumerate(points["wide_center"]):
        named[f"wide_{index}"] = point
    for index, point in enumerate(points["riffle_center"]):
        named[f"riffle_{index}"] = point
    for name, point in points["bank"].items():
        named["bank_" + name] = point
    return named


def build_rock_points(rock):
    center, tangent, radius = rock["center"], rock["tangent"], rock["waterline_radius_cm"]
    named = {
        "center": center,
        "flank_left_up": point_beside(center, tangent, -0.75 * radius, 1.25 * radius),
        "flank_right_up": point_beside(center, tangent, -0.75 * radius, -1.25 * radius),
        "flank_left_down": point_beside(center, tangent, 0.75 * radius, 1.25 * radius),
        "flank_right_down": point_beside(center, tangent, 0.75 * radius, -1.25 * radius),
    }
    for index, distance in enumerate(ROCK_WAKE_DISTANCES_CM):
        for lateral_name, lateral in ROCK_WAKE_LATERALS_CM:
            named[f"wake_{index}_{lateral_name}"] = point_beside(center, tangent, radius + distance, lateral)
    return named


def build_submerged_points(rock):
    center, tangent, radius = rock["center"], rock["tangent"], rock["bounding_radius_cm"]
    return {
        "center": center,
        "beside_left": point_beside(center, tangent, 0.0, radius + 60.0),
        "beside_right": point_beside(center, tangent, 0.0, -(radius + 60.0)),
    }


def run_asleep_leg(points, sleep_delay_seconds):
    teleport(points["asleep_pawn"])
    time.sleep(sleep_delay_seconds + SLEEP_MARGIN_SECONDS)
    state = editor_action("water_state")
    offset = log_size()
    time.sleep(PERF_MEASURE_SECONDS)
    return {"state": state, "cpu": summarize_perf_lines(read_log_since(offset))}


def run_wake_leg(points):
    editor_action("sampler_start", kind="wake")
    teleport(points["flow_pawn"])
    time.sleep(WAKE_OBSERVE_SECONDS)
    return editor_action("sampler_stop")


def run_flow_leg(points, contract):
    time.sleep(FLOW_SETTLE_SECONDS)
    offset = log_size()
    editor_action("sampler_start", kind="points", points=build_flow_points(points), z=points["flow_pawn"][2], interval=POINT_SAMPLE_INTERVAL_SECONDS)
    time.sleep(FLOW_MEASURE_SECONDS)
    summary = editor_action("sampler_stop")
    summary["cpu"] = summarize_perf_lines(read_log_since(offset))
    summary["mid"] = editor_action("mid_state", surface_tag=SURFACE_TAG, region_tag=REGION_TAG, **contract)
    return summary


def run_rock_leg(rock):
    teleport(rock["pawn"])
    time.sleep(ROCK_SETTLE_SECONDS)
    editor_action("sampler_start", kind="points", points=build_rock_points(rock), z=rock["pawn"][2], interval=POINT_SAMPLE_INTERVAL_SECONDS)
    time.sleep(ROCK_MEASURE_SECONDS)
    return editor_action("sampler_stop")


def run_submerged_leg(rock):
    teleport(rock["pawn"])
    time.sleep(SUBMERGED_SETTLE_SECONDS)
    editor_action("sampler_start", kind="points", points=build_submerged_points(rock), z=rock["pawn"][2], interval=POINT_SAMPLE_INTERVAL_SECONDS)
    time.sleep(SUBMERGED_MEASURE_SECONDS)
    return editor_action("sampler_stop")


def run_crossing_leg(crossing):
    start, goal, box = crossing["start"], crossing["goal"], crossing["foam_box"]
    teleport(start)
    time.sleep(CROSSING_SETTLE_SECONDS)
    grid = {"origin": box["origin"], "tangent": crossing["tangent"], "s0": box["s_range_cm"][0], "s1": box["s_range_cm"][1], "half": box["half_width_cm"], "step": box["step_cm"], "z": start[2]}
    baseline = editor_action("foam_grid", mode="baseline", **grid)
    time.sleep(FOAM_SNAPSHOT_GAP_SECONDS)
    baseline["noise"] = editor_action("foam_grid", mode="noise", **grid)
    editor_action("sampler_start", kind="crossing", z=start[2], min_depth=CROSSING_MINIMUM_DEPTH_CM, interval=CROSSING_SAMPLE_INTERVAL_SECONDS)
    move = editor_action("move", goal_x=goal[0], goal_y=goal[1])
    result = {"baseline": baseline, "move": move}
    if move.get("mode") != "navigation":
        result["crossing"] = editor_action("sampler_stop")
        return result
    deadline = time.monotonic() + CROSSING_TIMEOUT_SECONDS
    water_exit_y = box["origin"][1] + box["half_width_cm"] * abs(crossing["tangent"][0])
    reached = False
    while time.monotonic() < deadline:
        state = editor_action("pawn_state")
        if state.get("y") is not None and state["y"] >= water_exit_y:
            break
        time.sleep(CROSSING_POLL_SECONDS)
    result["foam_first"] = editor_action("foam_grid", mode="compare", **grid)
    time.sleep(FOAM_SNAPSHOT_GAP_SECONDS)
    result["foam_second"] = editor_action("foam_grid", mode="compare", **grid)
    while time.monotonic() < deadline:
        state = editor_action("pawn_state")
        if state.get("y") is not None and state["y"] >= goal[1] - CROSSING_GOAL_TOLERANCE_CM:
            reached = True
            break
        time.sleep(CROSSING_POLL_SECONDS)
    result["reached_goal"] = reached
    result["crossing"] = editor_action("sampler_stop")
    return result


def lateral_component(aggregate_entry):
    if not aggregate_entry or not aggregate_entry.get("count"):
        return None
    target = aggregate_entry["target"]
    norm = math.hypot(target[0], target[1])
    direction = (target[0] / norm, target[1] / norm) if norm > 1e-3 else (1.0, 0.0)
    normal = (-direction[1], direction[0])
    velocity = aggregate_entry["velocity"]
    return velocity[0] * normal[0] + velocity[1] * normal[1]


def mean_of(values):
    present = [value for value in values if value is not None]
    return sum(present) / len(present) if present else None


def rule(passed, value, detail="", required=True):
    return {"passed": passed, "value": value, "detail": detail, "required": required}


def judge_flow_target(flow):
    errors = [flow.get(f"wide_{index}", {}).get("relative_error") for index in range(3)]
    passed = all(error is not None and error <= FLOW_TARGET_TOLERANCE for error in errors)
    return rule(passed, errors, f"수로 중심 3점 상대 오차 ≤ {FLOW_TARGET_TOLERANCE}")


def judge_speed_ratio(flow):
    wide = mean_of([flow.get(f"wide_{index}", {}).get("speed_mean") for index in range(3)])
    riffle = mean_of([flow.get(f"riffle_{index}", {}).get("speed_mean") for index in range(3)])
    ratio = (riffle / wide) if wide and riffle is not None else None
    return rule(ratio is not None and ratio >= SPEED_RATIO_MINIMUM, {"ratio": ratio, "wide_cm_per_sec": wide, "riffle_cm_per_sec": riffle}, f"여울 중심 속력 / 넓은 구간 ≥ {SPEED_RATIO_MINIMUM}")


def judge_channel_depth(flow, layout):
    wide = mean_of([flow.get(f"wide_{index}", {}).get("depth_mean") for index in range(3)])
    riffle = mean_of([flow.get(f"riffle_{index}", {}).get("depth_mean") for index in range(3)])
    wide_ok = wide is not None and abs(wide - layout["depth_cm"]) <= WIDE_DEPTH_TOLERANCE_CM
    riffle_ok = riffle is not None and abs(riffle - layout["riffle"]["depth_cm"]) <= RIFFLE_DEPTH_TOLERANCE_CM
    return rule(wide_ok and riffle_ok, {"wide_cm": wide, "riffle_cm": riffle}, "무대 지형 수심(120·40cm)이 트레이스로 솔버에 전달됨")


def judge_dry_bank(flow):
    entries = [flow.get("bank_dry_mid_south", {}), flow.get("bank_dry_far_south", {})]
    speeds = [entry.get("speed_mean") for entry in entries]
    known = [entry.get("known_fraction") for entry in entries]
    passed = all(speed is not None and speed <= DRY_BANK_SPEED_MAXIMUM_CM_PER_SEC for speed in speeds) and all(item is not None and item >= DRY_BANK_KNOWN_MINIMUM for item in known)
    return rule(passed, {"speed_cm_per_sec": speeds, "known_fraction": known}, f"마른 기슭 셀 속도 ≤ {DRY_BANK_SPEED_MAXIMUM_CM_PER_SEC}")


def judge_bank_wetness(flow):
    reference = flow.get("wide_1", {}).get("wetness_mean")
    if reference is None or reference < WETNESS_REFERENCE_MINIMUM:
        return rule(None, {"water_cell_wetness": reference}, "물 셀 젖음이 0이라 젖음 갱신(W-2 A2)이 아직 없음: 판정 보류", required=False)
    wet = [flow.get("bank_wet_band_south", {}).get("wetness_mean"), flow.get("bank_wet_band_north", {}).get("wetness_mean")]
    far = flow.get("bank_dry_far_south", {}).get("wetness_mean")
    passed = all(item is not None and item >= BANK_WET_MINIMUM for item in wet) and far is not None and far <= BANK_FAR_MAXIMUM
    return rule(passed, {"wet_band": wet, "far": far}, f"기슭 띠 젖음 ≥ {BANK_WET_MINIMUM}, 먼 기슭 ≤ {BANK_FAR_MAXIMUM}")


def judge_rock_wake(rock):
    densities = [entry.get("density_mean") for name, entry in rock.items() if name.startswith("wake_") and isinstance(entry, dict)]
    peak = max((value for value in densities if value is not None), default=None)
    return rule(peak is not None and peak >= ROCK_WAKE_DENSITY_MINIMUM, peak, f"바위 하류 3m 안 시간 평균 흰 물보라 최대 ≥ {ROCK_WAKE_DENSITY_MINIMUM}")


def judge_rock_flank(rock):
    left = lateral_component(rock.get("flank_left_up"))
    right = lateral_component(rock.get("flank_right_up"))
    passed = left is not None and right is not None and left * right < 0.0 and min(abs(left), abs(right)) >= ROCK_LATERAL_MINIMUM_CM_PER_SEC
    return rule(passed, {"left_cm_per_sec": left, "right_cm_per_sec": right}, "바위 양옆(상류 어깨) 횡속도 부호 반대")


def judge_rock_interior(rock):
    center = rock.get("center", {})
    speed = center.get("speed_mean")
    return rule(speed is not None and speed <= DRY_BANK_SPEED_MAXIMUM_CM_PER_SEC, speed, "수면 위 바위 안 속도 0", required=False)


def judge_submerged(submerged):
    center = submerged.get("center", {})
    sdf = center.get("sdf_min")
    depth = center.get("depth_mean")
    return rule(sdf is not None and sdf <= 0.0, {"sdf_min": sdf, "depth_mean": depth}, "잠긴 바위 중심 셀이 장애물 태그 경로로 고체")


def judge_crossing_ripple(crossing_leg):
    summary = crossing_leg.get("crossing", {})
    if crossing_leg.get("move", {}).get("mode") != "navigation":
        return rule(None, crossing_leg.get("move"), "내비게이션 이동 불가라 걷기 판정 불가(강 내비 메시 필요)")
    ripple = summary.get("max_abs_ripple_cm")
    seconds = summary.get("seconds_in_water")
    passed = ripple is not None and ripple >= CROSSING_RIPPLE_MINIMUM_CM and seconds is not None and seconds >= CROSSING_MINIMUM_SECONDS_IN_WATER
    return rule(passed, {"max_abs_ripple_cm": ripple, "seconds_in_water": seconds, "reached_goal": crossing_leg.get("reached_goal")}, f"폰 1m 안 |RippleHeightCm| ≥ {CROSSING_RIPPLE_MINIMUM_CM}cm")


def best_profile_shift_cm(first, second):
    s_values, before, after = first.get("profile_s_cm"), first.get("profile"), second.get("profile")
    if not s_values or not before or not after or len(before) != len(after) or len(s_values) < 2:
        return None
    step = s_values[1] - s_values[0]
    count = len(before)
    best_shift, best_score = None, 0.0
    for offset in range(-(count // 2), count // 2 + 1):
        overlap = range(max(0, -offset), min(count, count - offset))
        if len(overlap) < count // 4:
            continue
        score = sum(before[index] * after[index + offset] for index in overlap) / len(overlap)
        if score > best_score:
            best_shift, best_score = offset * step, score
    return best_shift


def judge_foam_shift(crossing_leg):
    first, second = crossing_leg.get("foam_first"), crossing_leg.get("foam_second")
    if not first or not second or "error" in first or "error" in second:
        return rule(None, {"first": first, "second": second}, "거품 격자 표본 없음")
    elapsed = second["game_time"] - first["game_time"]
    shift = best_profile_shift_cm(first, second)
    centroid_shift = None
    if first.get("centroid_s_cm") is not None and second.get("centroid_s_cm") is not None:
        centroid_shift = second["centroid_s_cm"] - first["centroid_s_cm"]
    required = FOAM_SHIFT_MINIMUM_CM * min(elapsed, FOAM_SHIFT_REFERENCE_SECONDS) / FOAM_SHIFT_REFERENCE_SECONDS
    passed = shift is not None and elapsed >= FOAM_MINIMUM_INTERVAL_SECONDS and first["excess_mass"] >= FOAM_MASS_MINIMUM and shift >= required
    return rule(passed, {"shift_cm": shift, "centroid_shift_cm": centroid_shift, "elapsed_seconds": elapsed, "required_cm": required, "first_mass": first["excess_mass"], "second_mass": second["excess_mass"]}, "진단: 물을 벗어난 직후 잡음 문턱 위 거품(R) 하류 분포의 상호상관 이동(여울 급류 흰 물보라가 같은 채널을 채워 PIE 에서는 분리 불가, 수용은 헤드리스 TDGame.FluidFX.Water.FlowGuide.FoamBlobDriftsDownstream)", required=False)


def judge_shared_mid(mid):
    count = mid.get("unique_mid_count")
    shared = mid.get("shared_material_count")
    driven = mid.get("driven_surface_count")
    low, high = SHARED_MID_RANGE
    passed = count is not None and low <= count <= high and shared is not None and low <= shared <= high and driven is not None and driven >= 1
    return rule(passed, {"unique_mid_count": count, "shared_material_count": shared, "driven_surface_count": driven}, "공유 수면 MID 수 1~4, 구동 표면 1개 이상")


def judge_surface_texture(mid):
    mids = mid.get("mids", [])
    bound = [item.get("textures", {}).get("WaterSurfaceTexture") for item in mids]
    passed = bool(mid.get("surface_texture_bound")) and bool(mids) and all(bound)
    return rule(passed, {"component_texture": mid.get("surface_texture_bound"), "mid_textures": bound}, "WaterSurfaceTexture 가 컴포넌트와 모든 MID 에 연결")


def judge_mid_declared(mid):
    undeclared = [item.get("undeclared_in_material") for item in mid.get("mids", [])]
    passed = bool(undeclared) and all(not item for item in undeclared)
    return rule(passed, undeclared, "C++ 구동 파라미터가 MID 의 부모 재질에 모두 선언됨")


def judge_mid_values(mid):
    problems = []
    for item in mid.get("mids", []):
        scalars = item.get("scalars", {})
        if scalars.get("FluidWaterLayerActive", 0.0) < 0.99:
            problems.append("FluidWaterLayerActive")
        phase = scalars.get("CoastWavePhase")
        if phase is None or not 0.0 <= phase < 1.0:
            problems.append("CoastWavePhase")
        for name in ("WaterDepthReferenceCm", "RippleHeightReferenceCm", "VelocitySpeedReferenceCmPerSec", "SimGridResolution"):
            if scalars.get(name, 0.0) <= 0.0:
                problems.append(name)
    return rule(bool(mid.get("mids")) and not problems, sorted(set(problems)), "안정 뒤 FluidWaterLayerActive 1, 해안 위상 0~1, 기준 길이·속도·해상도 양수")


def judge_no_region_tag(mid):
    actors = mid.get("surface_actors", [])
    tagged = [item["actor"] for item in actors if item.get("has_region_tag")]
    return rule(bool(actors) and not tagged, {"surface_actors": len(actors), "region_tagged": tagged}, "강 리본 액터에 수역 태그 없음")


def judge_wake(wake):
    if "error" in wake:
        return rule(False, wake, "깨어남 추적 실패")
    ramp = wake.get("ramp_seconds")
    low, high = LAYER_RAMP_RANGE_SECONDS
    passed = (
        wake.get("was_asleep_at_start") is True
        and wake.get("alpha_at_first_awake") is not None
        and wake["alpha_at_first_awake"] <= LAYER_ALPHA_ZERO_MAXIMUM
        and ramp is not None
        and low <= ramp <= high
        and wake.get("monotone") is True
        and wake.get("final_alpha", 0.0) >= 0.999
    )
    return rule(passed, wake, "잠들었다 깨어난 직후 FluidWaterLayerActive 0, 지형 완료 뒤 1초에 걸쳐 1")


def judge_awake_cost(flow):
    cpu = flow.get("cpu", {})
    average = cpu.get("avg_tick_ms")
    passed = average is not None and average <= AWAKE_TICK_BUDGET_MS and cpu.get("awake_actors_max") == 1
    return rule(passed, cpu, f"깨어 있을 때 TDFluidPerf 틱 평균 ≤ {AWAKE_TICK_BUDGET_MS}ms, 깨어 있는 액터 1개")


def judge_asleep_cost(asleep):
    cpu = asleep.get("cpu", {})
    average = cpu.get("avg_tick_ms")
    passed = average is not None and average <= ASLEEP_TICK_BUDGET_MS and cpu.get("awake_actors_max") == 0 and asleep.get("state", {}).get("awake") is False
    return rule(passed, {"cpu": cpu, "state": asleep.get("state")}, f"잠들었을 때 틱 평균 ≤ {ASLEEP_TICK_BUDGET_MS}ms, 깨어 있는 액터 0개")


def judge_log(problems):
    return rule(problems["count"] == 0, problems, "실행 중 로그에 오류·단언 실패 없음")


def judge_run(run_data, layout):
    flow = run_data["flow"]
    mid = flow.get("mid", {})
    return {
        "flow_matches_target_center_3pts": judge_flow_target(flow),
        "riffle_speed_ratio": judge_speed_ratio(flow),
        "channel_depth_matches_stage": judge_channel_depth(flow, layout),
        "dry_bank_velocity_zero": judge_dry_bank(flow),
        "bank_band_wetness": judge_bank_wetness(flow),
        "rock_wake_density": judge_rock_wake(run_data["rock"]),
        "rock_flank_lateral_opposite": judge_rock_flank(run_data["rock"]),
        "rock_interior_velocity_zero": judge_rock_interior(run_data["rock"]),
        "submerged_rock_is_solid": judge_submerged(run_data["submerged"]),
        "crossing_ripple_height": judge_crossing_ripple(run_data["crossing"]),
        "crossing_foam_centroid_shift": judge_foam_shift(run_data["crossing"]),
        "shared_mid_count": judge_shared_mid(mid),
        "water_surface_texture_bound": judge_surface_texture(mid),
        "mid_parameters_declared": judge_mid_declared(mid),
        "mid_parameter_values": judge_mid_values(mid),
        "ribbon_actor_has_no_region_tag": judge_no_region_tag(mid),
        "wake_layer_alpha_ramp": judge_wake(run_data["wake"]),
        "awake_tick_cost": judge_awake_cost(flow),
        "asleep_tick_cost": judge_asleep_cost(run_data["asleep"]),
        "no_log_errors": judge_log(run_data["log_problems"]),
    }


def count_failures(rules):
    return [name for name, item in rules.items() if item["required"] and item["passed"] is not True]


def run_once(points, layout, contract, sleep_delay_seconds):
    log_offset = log_size()
    run_data = {}
    run_data["asleep"] = run_asleep_leg(points, sleep_delay_seconds)
    run_data["wake"] = run_wake_leg(points)
    run_data["flow"] = run_flow_leg(points, contract)
    run_data["rock"] = run_rock_leg(points["rock_primary"])
    run_data["submerged"] = run_submerged_leg(points["rock_submerged"])
    run_data["crossing"] = run_crossing_leg(points["crossing"])
    run_data["log_problems"] = find_problem_lines(read_log_since(log_offset))
    rules = judge_run(run_data, layout)
    return {"data": run_data, "rules": rules, "failed": count_failures(rules)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--runs", type=int, default=2, help="연속 실행 횟수(명세는 연속 2회 통과)")
    parser.add_argument("--stop-pie", action="store_true", help="끝난 뒤 PIE를 끈다(기본은 켜 둔다)")
    arguments = parser.parse_args()
    print("경고: 측정 동안 UnrealEditor 창을 앞창으로 두고 건드리지 않는다(L-editor-06).", flush=True)

    stage_report = read_stage_report()
    if stage_report is None:
        print("무대 보고서에 강 장면이 없다: editor_make_fluid_water_stage.py 를 scenes [0, 1] 로 먼저 실행", STAGE_REPORT_PATH)
        return EXIT_ENVIRONMENT
    river = stage_report["river"]
    points = river["probe_points"]
    layout = river["layout"]
    contract = read_material_contract()
    if sorted(contract) != ["scalar_names", "texture_names", "vector_names"]:
        print("재질 스크립트에서 MID_DRIVEN_* 계약을 읽지 못했다", MATERIAL_SCRIPT_PATH)
        return EXIT_ENVIRONMENT

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
    environment["river_navigation_ready"], environment["river_navigation_state"] = wait_for_river_navigation(points)
    if not pie_check_fluid.start_pie(session, environment) or not pie_check_fluid.wait_for_pie_state(True, pie_check_fluid.PIE_START_WAIT_SECONDS):
        print("PIE를 시작하지 못했다:", json.dumps(environment, ensure_ascii=False)[:300])
        return EXIT_ENVIRONMENT
    time.sleep(3.0)

    status = editor_action("status")
    if "error" in status or status.get("water_actor_count") != 1 or status.get("missing_sample_properties"):
        print("환경 점검 실패(물 액터는 월드에 정확히 1개, 셀 표본 속성 필요):", json.dumps(status, ensure_ascii=False)[:600])
        if arguments.stop_pie:
            pie_check_fluid.stop_pie(session, environment)
        return EXIT_ENVIRONMENT

    runs = []
    editor_action("isolate")
    editor_action("console", command=f"{PERF_CONSOLE_VARIABLE} 1")
    try:
        for _ in range(max(arguments.runs, 1)):
            runs.append(run_once(points, layout, contract, status["sleep_delay_seconds"]))
    finally:
        editor_action("console", command=f"{PERF_CONSOLE_VARIABLE} 0")
        editor_action("sampler_stop")
        if arguments.stop_pie:
            pie_check_fluid.stop_pie(session, environment)

    consecutive = all(not item["failed"] for item in runs)
    evidence = {
        "date": date.today().isoformat(),
        "level": WATER_LEVEL_PATH,
        "stage_report": STAGE_REPORT_PATH.relative_to(ROOT).as_posix(),
        "status": status,
        "environment": environment,
        "layout": layout,
        "runs": [{"failed": item["failed"], "rules": item["rules"], "data": item["data"]} for item in runs],
        "all_runs_passed": consecutive,
        "notes": [
            "화면 항목(태양 반점 포화 비율, TSR 번짐, 창 경계 이음매, 바닥 비침·커스틱)은 이 탐침이 재지 않는다",
            "창 이동 프레임 틱 p95와 SingleLayerWater GPU 시간은 C++ 계측과 ProfileGPU 로 따로 잰다",
            "bank_band_wetness 는 물 셀 젖음이 0이면(W-2 A2 미구현) 판정을 보류한다",
        ],
    }
    EVIDENCE_PATH.parent.mkdir(parents=True, exist_ok=True)
    EVIDENCE_PATH.write_text(json.dumps(evidence, ensure_ascii=False, indent=2, default=str), encoding="utf-8")
    print("증거", EVIDENCE_PATH.relative_to(ROOT).as_posix())
    for index, item in enumerate(runs):
        for name in item["failed"]:
            print(f"실패(실행 {index + 1}):", name, json.dumps(item["rules"][name], ensure_ascii=False, default=str)[:260])
    print("연속 통과" if consecutive else "통과하지 못함", f"({len(runs)}회 실행)")
    return 0 if consecutive else EXIT_FAILED


if __name__ == "__main__":
    sys.exit(main())
