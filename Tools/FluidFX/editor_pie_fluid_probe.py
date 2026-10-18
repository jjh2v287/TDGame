"""실행 환경: TDGame 언리얼 에디터 안(PythonScriptPlugin 원격 실행), LV_TDFluidTestArena PIE가 켜져 있어야 하며 시나리오는 슬레이트 틱 콜백이 프레임마다 진행한다(원격 실행이 끝나도 콜백은 계속 돈다).
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_pie_fluid_probe.py (PowerShell, 요청 파일이 없으면 start만 수행. 레벨 열기·경사 준비·PIE 시작·캡처·종료·정리까지 한 번에 하려면 python Tools/FluidFX/pie_check_fluid.py)
출력: Saved/FluidFX/fluid_pie_probe.json (항목 8개의 passed·value·criterion, 상위 passed는 8개 전부 통과일 때만 true), 표준 출력 TD_FLUID_PROBE_RESULT 한 줄
상태: 현행(2026-10-18 첫 성공, 증거 Docs/Validation/FluidFX/D-48-*)
"""
import json
import math
import os
import time
import traceback

import unreal

LEVEL_NAME = "LV_TDFluidTestArena"
WATER_PRESET_PATH = "/Game/FluidFX/Presets/DA_TDFluid_Water"
RAMP_MESH_PATH = "/Engine/BasicShapes/Cube"
PROBE_ACTOR_PREFIX = "TDProbe_"
SLOPE_RAMP_LABEL = PROBE_ACTOR_PREFIX + "SlopeRamp"
SLOPE_FLUID_LABEL = PROBE_ACTOR_PREFIX + "SlopeFluid"
SLOPE_RAMP_CENTER = (6000.0, 0.0, 400.0)
SLOPE_RAMP_SCALE = (30.0, 20.0, 1.0)
SLOPE_RAMP_PITCH_DEGREES = 12.0
SLOPE_FLUID_GRAVITY = 600.0
SAMPLE_TERRAIN_PROPERTY_NAMES = ("sample_terrain_elevation",)

OUTPUT_DIRECTORY = os.path.join(unreal.SystemLibrary.get_project_saved_directory(), "FluidFX")
REPORT_PATH = os.path.join(OUTPUT_DIRECTORY, "fluid_pie_probe.json")
REQUEST_PATH = os.path.join(OUTPUT_DIRECTORY, "fluid_pie_probe_request.json")
CAPTURE_FLAG_PATH = os.path.join(OUTPUT_DIRECTORY, "fluid_pie_capture_done.flag")
RESULT_MARKER = "TD_FLUID_PROBE_RESULT"
TICK_HANDLE_ATTRIBUTE = "td_fluid_probe_tick_handle"

ARENA_FLOOR_FALLBACK = (150.0, 0.0, 1600.0, 1300.0)
ARENA_FLOOR_MARGIN_CM = 150.0

MIN_DENSITY = 0.1
MAX_RETURN_SPEED_CM_PER_SECOND = 1200.0
MIN_WALKED_DISTANCE_CM = 100.0
MIN_DOWNHILL_SHIFT_CM = 15.0
MIN_GROUND_GRADIENT = 0.05
ISOLATED_DEATH_DAMAGE_LIMIT = 40.0

READY_TIMEOUT_REAL_SECONDS = 30.0
NAVIGATION_TIMEOUT_REAL_SECONDS = 120.0
NAVIGATION_POLL_REAL_SECONDS = 0.5
CAPTURE_WAIT_REAL_SECONDS = 90.0
WALK_SECONDS = 3.0
WALK_SETTLE_SECONDS = 0.5
TELEPORT_AWAY_SECONDS = 1.0
RETURN_WINDOW_SECONDS = 0.6
SLOPE_SETTLE_SECONDS = 2.0
CAPTURE_SCENE_SETTLE_SECONDS = 1.0
CAPTURE_PAWN_XY = (250.0, 0.0)
POST_STAMP_FRAME_COUNT = 3

WALK_OFFSETS_CM = ((0.0, 1000.0), (0.0, -1000.0), (-800.0, 0.0), (1000.0, 0.0))
WALK_SAMPLE_DISTANCES_BEHIND_END_CM = (60.0, 140.0, 220.0)
TELEPORT_OFFSETS_CM = ((2200.0, 0.0), (-2200.0, 0.0), (0.0, 1100.0), (0.0, -1100.0), (1100.0, 1100.0), (1100.0, -1100.0), (-1100.0, 1100.0), (-1100.0, -1100.0))

FIRE_DAMAGE_AMOUNT = 30.0
DEATH_RING_DISTANCE_CM = 150.0
DEATH_OVERKILL_AMOUNT = 4.0
SCENE_FIRE_DAMAGE_AMOUNT = 20.0

GROUND_TRACE_HALF_SPAN_CM = 1500.0
GRADIENT_PROBE_DISTANCE_CM = 300.0
CENTROID_HALF_SPAN_CM = 700.0
CENTROID_SPACING_CM = 35.0
SLOPE_STAMP_RADIUS_CM = 150.0
SLOPE_STAMP_DENSITY = 1.5

ITEM_SURFACE = "1_surface_material_instances"
ITEM_WALK = "2_walk_trail_density"
ITEM_FIRE = "3_fire_damage_stamp"
ITEM_DEATH = "4_death_stamp"
ITEM_TELEPORT = "5_teleport_return_velocity"
ITEM_SLOPE = "6_slope_flow_downhill"
ITEM_CAPTURE = "7_topdown_capture"
ITEM_PIE_ENDED = "8_pie_ended"
ITEM_ORDER = (ITEM_SURFACE, ITEM_WALK, ITEM_FIRE, ITEM_DEATH, ITEM_TELEPORT, ITEM_SLOPE, ITEM_CAPTURE, ITEM_PIE_ENDED)
ITEM_CRITERIA = {
    ITEM_SURFACE: "레벨의 유체 액터마다 내부 메시에 정적 메시가 있고 슬롯 0이 MaterialInstanceDynamic이며 DensityPressureTexture·VelocityTexture가 null이 아니고 컴포넌트가 만든 실행 중 텍스처와 같다",
    ITEM_WALK: "경로 이동(게임의 클릭 이동과 같은 simple_move_to)으로 약 3초 걷고(100cm 이상) 경로 끝에서 60·140·220cm 뒤 3점(플레이어 추적 창 안)에서 주 밀도가 0.1보다 크다",
    ITEM_FIRE: "전투 더미에 Fire 피해를 주면(적용 피해 > 0, 사전 선택 밀도 < 0.1) 3프레임 안에 더미 위치의 선택 밀도가 0.1보다 크다",
    ITEM_DEATH: "더미를 처치하면(사망) 더미 중심에서 150cm 떨어진 지점의 선택 밀도가 처치 전보다 0.1 이상 늘어난다",
    ITEM_TELEPORT: "플레이어 추적 유체 볼륨 밖으로 순간이동했다 복귀한 뒤 0.6초 동안 그 지점의 |VelocityCmPerSec| 최댓값이 1200 이하다",
    ITEM_SLOPE: "경사로 유체 위에 스탬프를 찍고 2초 뒤 밀도 무게중심이 내리막 방향으로 15cm 이상 이동한다",
    ITEM_CAPTURE: "PIE 게임 화면(탑다운 카메라) HighResShot PNG가 Docs/Validation/FluidFX/에 저장된다",
    ITEM_PIE_ENDED: "측정이 끝난 뒤 PIE가 종료되어 게임 월드가 없다",
}

NOT_READY = object()

report = {}
frame_hooks = []
scenario_state = {}


def get_game_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def get_game_seconds(world):
    return unreal.GameplayStatics.get_time_seconds(world)


def vector_as_list(vector):
    return [round(vector.x, 2), round(vector.y, 2), round(vector.z, 2)]


def planar_distance(first, second):
    return math.hypot(first.x - second.x, first.y - second.y)


def frames(count):
    return ("frames", count)


def seconds(duration):
    return ("seconds", duration)


def until(predicate, timeout_real_seconds):
    return ("until", predicate, timeout_real_seconds)


def reset_report(map_path, capture_mode):
    report.clear()
    report.update({
        "tool": "editor_pie_fluid_probe",
        "map": map_path,
        "state": "running",
        "stage": "starting",
        "passed": False,
        "capture_mode": capture_mode,
        "started": time.strftime("%Y-%m-%d %H:%M:%S"),
        "items": {},
        "events": [],
        "errors": [],
    })
    for key in (ITEM_CAPTURE, ITEM_PIE_ENDED):
        record_item(key, False, "pending", "드라이버(pie_check_fluid.py)가 채운다")
    scenario_state.clear()
    frame_hooks.clear()


def record_item(key, passed, value, note=""):
    report["items"][key] = {"passed": bool(passed), "criterion": ITEM_CRITERIA[key], "value": value, "note": note}


def write_report():
    report["passed"] = (report.get("state") == "complete" and not report["errors"]
                        and len(report["items"]) == len(ITEM_ORDER) and all(item["passed"] for item in report["items"].values()))
    os.makedirs(OUTPUT_DIRECTORY, exist_ok=True)
    temporary_path = REPORT_PATH + ".writing"
    with open(temporary_path, "w", encoding="utf-8") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2, default=str)
    for _ in range(5):
        try:
            os.replace(temporary_path, REPORT_PATH)
            return
        except OSError:
            time.sleep(0.05)


def mark_stage(stage):
    world = get_game_world()
    report["stage"] = stage
    report["events"].append({"stage": stage, "game_seconds": round(get_game_seconds(world), 3) if world else None, "real_seconds": round(time.monotonic() - scenario_state.get("started_real", time.monotonic()), 2)})
    write_report()


def finish(state, error=None):
    if report.get("state") != "running":
        return
    report["state"] = state
    if error:
        report["errors"].append(error)
    for key in ITEM_ORDER:
        if key not in report["items"]:
            record_item(key, False, None, "평가하지 못했다")
    handle = getattr(unreal, TICK_HANDLE_ATTRIBUTE, None)
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
        setattr(unreal, TICK_HANDLE_ATTRIBUTE, None)
    write_report()


def arm_wait(world, wait_specification):
    kind = wait_specification[0]
    wait = {"kind": kind}
    if kind == "frames":
        wait["frames_left"] = wait_specification[1]
    if kind == "seconds":
        wait["deadline_game"] = get_game_seconds(world) + wait_specification[1]
    if kind == "until":
        wait["predicate"] = wait_specification[1]
        wait["deadline_real"] = time.monotonic() + wait_specification[2]
    scenario_state["wait"] = wait


def evaluate_wait(world):
    wait = scenario_state["wait"]
    if wait["kind"] == "frames":
        wait["frames_left"] -= 1
        return None if wait["frames_left"] <= 0 else NOT_READY
    if wait["kind"] == "seconds":
        return None if get_game_seconds(world) >= wait["deadline_game"] else NOT_READY
    if wait["predicate"]():
        return True
    if time.monotonic() >= wait["deadline_real"]:
        return False
    return NOT_READY


def advance_scenario():
    world = get_game_world()
    if world is None:
        finish("failed", "PIE 월드가 시나리오 도중 사라졌다")
        return
    for hook in list(frame_hooks):
        hook(world)
    outcome = evaluate_wait(world)
    if outcome is NOT_READY:
        return
    arm_wait(world, scenario_state["iterator"].send(outcome))


def tick(delta_seconds):
    if report.get("state") != "running":
        return
    try:
        advance_scenario()
    except StopIteration:
        finish("complete")
    except Exception:
        finish("failed", traceback.format_exc())


def collect_fluid_actors(world):
    return list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDFluidLiveActor))


def is_probe_actor(actor):
    return actor.get_actor_label().startswith(PROBE_ACTOR_PREFIX)


def find_fluid_by_label(fluids, fragment):
    lowered_fragment = fragment.lower()
    for actor in fluids:
        if lowered_fragment in actor.get_actor_label().lower():
            return actor
    return None


def find_following_fluid(fluids):
    named_actor = find_fluid_by_label(fluids, "PlayerTrail")
    if named_actor is not None:
        return named_actor
    for actor in fluids:
        if actor.get_editor_property("should_follow_player_pawn"):
            return actor
    return None


def find_slope_fluid(all_fluids):
    for actor in all_fluids:
        label = actor.get_actor_label().lower()
        if not is_probe_actor(actor) and ("slope" in label or "ramp" in label):
            return actor
    for actor in all_fluids:
        if actor.get_actor_label() == SLOPE_FLUID_LABEL:
            return actor
    return None


def find_fluid_subsystem(world):
    accessor = getattr(unreal.TDFluidLiveSubsystem, "get_fluid_live_subsystem", None)
    if accessor is None:
        return None
    return accessor(world)


def sample_fluid(live_component, location):
    result = live_component.sample_fluid_at_world_location(location)
    if isinstance(result, tuple):
        return result[-1]
    return result


def velocity_speed_cm_per_second(sample):
    velocity = getattr(sample, "velocity_cm_per_sec", None)
    if velocity is None:
        return None
    return math.hypot(velocity.x, velocity.y)


def strongest_selective_density(fluids, location):
    strongest = 0.0
    sources = {}
    for actor in fluids:
        sample = sample_fluid(actor.get_editor_property("live_component"), location)
        if sample is None:
            continue
        sources[actor.get_actor_label()] = round(sample.selective_density, 4)
        strongest = max(strongest, sample.selective_density)
    return strongest, sources


def get_player_pawn_and_controller():
    world = get_game_world()
    if world is None:
        return None, None
    return unreal.GameplayStatics.get_player_pawn(world, 0), unreal.GameplayStatics.get_player_controller(world, 0)


def inspect_surface(actor):
    mesh_component = actor.get_editor_property("internal_surface_mesh")
    live_component = actor.get_editor_property("live_component")
    material = mesh_component.get_material(0)
    entry = {
        "label": actor.get_actor_label(),
        "has_static_mesh": mesh_component.get_editor_property("static_mesh") is not None,
        "slot0_is_dynamic_instance": isinstance(material, unreal.MaterialInstanceDynamic),
        "slot0_material": material.get_path_name() if material else None,
    }
    runtime_textures = {"DensityPressureTexture": live_component.get_density_pressure_texture(), "VelocityTexture": live_component.get_velocity_texture()}
    for parameter_name, runtime_texture in runtime_textures.items():
        bound_texture = material.get_texture_parameter_value(parameter_name) if entry["slot0_is_dynamic_instance"] else None
        entry[parameter_name] = bound_texture.get_path_name() if bound_texture else None
        entry[parameter_name + "_is_runtime_texture"] = bound_texture is not None and runtime_texture is not None and bound_texture.get_path_name() == runtime_texture.get_path_name()
    if entry["slot0_is_dynamic_instance"]:
        entry["SimCenterWorld"] = str(material.get_vector_parameter_value("SimCenterWorld"))
        entry["SimExtentsCm"] = str(material.get_vector_parameter_value("SimExtentsCm"))
    entry["passed"] = (entry["has_static_mesh"] and entry["slot0_is_dynamic_instance"]
                       and entry["DensityPressureTexture_is_runtime_texture"] and entry["VelocityTexture_is_runtime_texture"])
    return entry


def evaluate_surface_item(level_fluids):
    entries = [inspect_surface(actor) for actor in level_fluids]
    failed_labels = [entry["label"] for entry in entries if not entry["passed"]]
    note = ""
    if not entries:
        note = "레벨에 TDFluidLiveActor가 없다"
    if failed_labels:
        note = "실패 액터: " + ", ".join(failed_labels)
    record_item(ITEM_SURFACE, bool(entries) and not failed_labels, entries, note)


def level_fluid_setup_is_ready():
    world = get_game_world()
    if world is None:
        return False
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    level_fluids = [actor for actor in collect_fluid_actors(world) if not is_probe_actor(actor)]
    if pawn is None or not level_fluids:
        return False
    return all(inspect_surface(actor)["passed"] for actor in level_fluids)


def find_walkable_goal(holder, pawn):
    if time.monotonic() - holder.get("last_check", 0.0) < NAVIGATION_POLL_REAL_SECONDS:
        return False
    holder["last_check"] = time.monotonic()
    world = get_game_world()
    if world is None:
        return False
    start = pawn.get_actor_location()
    for offset_x, offset_y in WALK_OFFSETS_CM:
        goal = unreal.Vector(start.x + offset_x, start.y + offset_y, start.z)
        path = unreal.NavigationSystemV1.find_path_to_location_synchronously(world, start, goal, pawn)
        if path is not None and path.is_valid() and not path.is_partial():
            holder["goal"] = goal
            return True
    return False


def locations_behind_path_end(samples, distances_behind_end):
    cumulative = [0.0]
    for index in range(1, len(samples)):
        cumulative.append(cumulative[-1] + planar_distance(samples[index - 1], samples[index]))
    locations = []
    for distance_behind_end in distances_behind_end:
        target = cumulative[-1] - distance_behind_end
        if target < 0.0:
            continue
        for index, travelled in enumerate(cumulative):
            if travelled >= target:
                locations.append(samples[index])
                break
    return cumulative[-1], locations


def walk_item(level_fluids):
    pawn, controller = get_player_pawn_and_controller()
    trail_actor = find_following_fluid(level_fluids)
    if pawn is None or controller is None or trail_actor is None:
        record_item(ITEM_WALK, False, None, "플레이어 폰·컨트롤러 또는 플레이어 추적 유체 액터가 없다")
        return
    goal_holder = {}
    navigation_started = time.monotonic()
    navigation_ready = yield until(lambda: find_walkable_goal(goal_holder, pawn), NAVIGATION_TIMEOUT_REAL_SECONDS)
    navigation_wait = round(time.monotonic() - navigation_started, 1)
    if not navigation_ready:
        record_item(ITEM_WALK, False, {"navigation_wait_real_seconds": navigation_wait}, "내비 경로를 얻지 못해 걷지 못했다")
        return
    path_locations = []
    hook = lambda active_world: path_locations.append(pawn.get_actor_location())
    frame_hooks.append(hook)
    unreal.AIHelperLibrary.simple_move_to_location(controller, goal_holder["goal"])
    yield seconds(WALK_SECONDS)
    frame_hooks.remove(hook)
    controller.stop_movement()
    walked_distance, sample_locations = locations_behind_path_end(path_locations, WALK_SAMPLE_DISTANCES_BEHIND_END_CM)
    live_component = trail_actor.get_editor_property("live_component")
    points = []
    for location in sample_locations:
        sample = sample_fluid(live_component, location)
        points.append({"location": vector_as_list(location), "primary_density": None if sample is None else round(sample.primary_density, 4)})
    densities_ok = len(points) == len(WALK_SAMPLE_DISTANCES_BEHIND_END_CM) and all(point["primary_density"] is not None and point["primary_density"] > MIN_DENSITY for point in points)
    note = ""
    if walked_distance < MIN_WALKED_DISTANCE_CM:
        note = "폰이 거의 걷지 않았다"
    elif not densities_ok:
        note = "경로 위 점의 주 밀도가 0.1 이하이거나 볼륨 밖이다"
    value = {"trail_actor": trail_actor.get_actor_label(), "goal": vector_as_list(goal_holder["goal"]), "walked_cm": round(walked_distance, 1),
             "navigation_wait_real_seconds": navigation_wait, "points": points}
    record_item(ITEM_WALK, walked_distance >= MIN_WALKED_DISTANCE_CM and densities_ok, value, note)
    yield seconds(WALK_SETTLE_SECONDS)


def sorted_damage_targets(world):
    targets = list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDDamageTarget))
    return sorted(targets, key=lambda actor: (actor.get_actor_location().x, actor.get_actor_location().y))


def make_damage_spec(amount, element):
    spec = unreal.TDDamageSpec()
    spec.set_editor_property("amount", amount)
    spec.set_editor_property("element", element)
    return spec


def fire_damage_item(level_fluids):
    targets = sorted_damage_targets(get_game_world())
    if not targets:
        record_item(ITEM_FIRE, False, None, "TDDamageTarget 더미가 없다")
        return
    dummy = targets[0]
    location = dummy.get_actor_location()
    baseline, baseline_sources = strongest_selective_density(level_fluids, location)
    applied_damage = unreal.TDCombatLibrary.try_apply_damage(dummy, make_damage_spec(FIRE_DAMAGE_AMOUNT, unreal.TDDamageElement.FIRE))
    readings = []
    for _ in range(POST_STAMP_FRAME_COUNT):
        yield frames(1)
        strongest, sources = strongest_selective_density(level_fluids, location)
        readings.append({"selective_density": round(strongest, 4), "by_actor": sources})
    peak = max(reading["selective_density"] for reading in readings)
    first_visible_frame = next((index + 1 for index, reading in enumerate(readings) if reading["selective_density"] > MIN_DENSITY), None)
    note = ""
    if applied_damage <= 0.0:
        note = "Fire 피해가 적용되지 않았다"
    elif baseline >= MIN_DENSITY:
        note = "피해 전에 이미 선택 밀도가 0.1 이상이라 귀속할 수 없다"
    elif peak <= MIN_DENSITY:
        note = "피해 뒤 3프레임 동안 선택 밀도가 0.1 이하다"
    value = {"dummy": dummy.get_actor_label(), "location": vector_as_list(location), "applied_damage": round(applied_damage, 2),
             "baseline": round(baseline, 4), "baseline_by_actor": baseline_sources, "frames_after_damage": readings, "first_visible_frame": first_visible_frame}
    record_item(ITEM_FIRE, applied_damage > 0.0 and baseline < MIN_DENSITY and peak > MIN_DENSITY, value, note)


def death_stamp_item(level_fluids):
    targets = sorted_damage_targets(get_game_world())
    if len(targets) < 2:
        record_item(ITEM_DEATH, False, None, "처치용으로 쓸 두 번째 더미가 없다")
        return
    dummy = targets[-1]
    combat = dummy.get_component_by_class(unreal.TDCombatComponent)
    if combat is None:
        record_item(ITEM_DEATH, False, None, "더미에 TDCombatComponent가 없다")
        return
    stats = combat.get_stats()
    stats.set_editor_property("base_max_health", 1.0)
    combat.set_stats(stats, True)
    health_before = combat.get_current_health()
    location = dummy.get_actor_location()
    ring_location = unreal.Vector(location.x + DEATH_RING_DISTANCE_CM, location.y, location.z)
    baseline, baseline_sources = strongest_selective_density(level_fluids, ring_location)
    kill_amount = health_before + DEATH_OVERKILL_AMOUNT
    applied_damage = unreal.TDCombatLibrary.try_apply_damage(dummy, make_damage_spec(kill_amount, unreal.TDDamageElement.PHYSICAL))
    readings = []
    for _ in range(POST_STAMP_FRAME_COUNT):
        yield frames(1)
        strongest, sources = strongest_selective_density(level_fluids, ring_location)
        readings.append({"selective_density": round(strongest, 4), "by_actor": sources})
    peak = max(reading["selective_density"] for reading in readings)
    increase = peak - baseline
    is_dead = not combat.is_alive()
    note = ""
    if not is_dead:
        note = "더미가 처치되지 않았다"
    elif increase < MIN_DENSITY:
        note = "처치 뒤 링 지점의 선택 밀도 증가가 0.1 미만이다"
    value = {"dummy": dummy.get_actor_label(), "ring_location": vector_as_list(ring_location), "health_before_kill": round(health_before, 2),
             "kill_amount": round(kill_amount, 2), "applied_damage": round(applied_damage, 2), "is_dead": is_dead,
             "baseline": round(baseline, 4), "baseline_by_actor": baseline_sources, "frames_after_kill": readings, "increase": round(increase, 4),
             "isolated_from_damage_stamp": kill_amount <= ISOLATED_DEATH_DAMAGE_LIMIT}
    record_item(ITEM_DEATH, is_dead and increase >= MIN_DENSITY, value, note)


def arena_floor_bounds(world):
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.StaticMeshActor):
        if "FluidArenaFloor" in actor.get_actor_label():
            origin, extent = actor.get_actor_bounds(False)
            return origin.x, origin.y, extent.x, extent.y
    return ARENA_FLOOR_FALLBACK


def choose_teleport_destination(world, origin):
    center_x, center_y, half_x, half_y = arena_floor_bounds(world)
    for offset_x, offset_y in TELEPORT_OFFSETS_CM:
        destination_x = origin.x + offset_x
        destination_y = origin.y + offset_y
        if abs(destination_x - center_x) <= half_x - ARENA_FLOOR_MARGIN_CM and abs(destination_y - center_y) <= half_y - ARENA_FLOOR_MARGIN_CM:
            return unreal.Vector(destination_x, destination_y, origin.z)
    return unreal.Vector(origin.x + TELEPORT_OFFSETS_CM[0][0], origin.y, origin.z)


def teleport_item(level_fluids):
    pawn, controller = get_player_pawn_and_controller()
    trail_actor = find_following_fluid(level_fluids)
    if pawn is None or trail_actor is None:
        record_item(ITEM_TELEPORT, False, None, "플레이어 폰 또는 플레이어 추적 유체 액터가 없다")
        return
    live_component = trail_actor.get_editor_property("live_component")
    origin = pawn.get_actor_location()
    destination = choose_teleport_destination(get_game_world(), origin)
    speed_before = None
    sample_before = sample_fluid(live_component, origin)
    if sample_before is not None:
        speed_before = velocity_speed_cm_per_second(sample_before)
    pawn.set_actor_location(destination, False, True)
    yield seconds(TELEPORT_AWAY_SECONDS)
    origin_sample_while_away = sample_fluid(live_component, origin)
    destination_sample_while_away = sample_fluid(live_component, destination)
    left_volume = origin_sample_while_away is None or destination_sample_while_away is None
    return_readings = []
    hook = lambda active_world: return_readings.append(sample_fluid(live_component, origin))
    pawn.set_actor_location(origin, False, True)
    frame_hooks.append(hook)
    yield seconds(RETURN_WINDOW_SECONDS)
    frame_hooks.remove(hook)
    speeds = [velocity_speed_cm_per_second(sample) for sample in return_readings if sample is not None]
    valid_speeds = [speed for speed in speeds if speed is not None]
    peak_speed = max(valid_speeds) if valid_speeds else None
    note = ""
    if not left_volume:
        note = "순간이동 중에도 두 지점이 모두 볼륨 안이라 볼륨 밖 조건이 성립하지 않았다"
    elif not valid_speeds:
        note = "복귀 뒤 샘플이 없거나 VelocityCmPerSec 필드가 없다"
    elif peak_speed > MAX_RETURN_SPEED_CM_PER_SECOND:
        note = "복귀 직후 속도가 1200cm/s를 넘었다"
    value = {"trail_actor": trail_actor.get_actor_label(), "origin": vector_as_list(origin), "destination": vector_as_list(destination),
             "speed_before_cm_per_second": None if speed_before is None else round(speed_before, 1), "left_volume": left_volume,
             "return_frames": len(return_readings), "valid_samples": len(valid_speeds),
             "peak_speed_cm_per_second": None if peak_speed is None else round(peak_speed, 1),
             "last_speed_cm_per_second": None if not valid_speeds else round(valid_speeds[-1], 1)}
    record_item(ITEM_TELEPORT, left_volume and bool(valid_speeds) and peak_speed <= MAX_RETURN_SPEED_CM_PER_SECOND, value, note)


def trace_ground_height(world, x, y, reference_z, ignored_actors):
    start = unreal.Vector(x, y, reference_z + GROUND_TRACE_HALF_SPAN_CM)
    end = unreal.Vector(x, y, reference_z - GROUND_TRACE_HALF_SPAN_CM)
    hit = unreal.SystemLibrary.line_trace_single(world, start, end, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, ignored_actors, unreal.DrawDebugTrace.NONE, True)
    if hit is None:
        return None
    hit_values = hit.to_tuple()
    if not hit_values[0]:
        return None
    return hit_values[5].z


def measure_downhill_direction(world, center, ignored_actors):
    spacing = GRADIENT_PROBE_DISTANCE_CM
    offsets = {"plus_x": (spacing, 0.0), "minus_x": (-spacing, 0.0), "plus_y": (0.0, spacing), "minus_y": (0.0, -spacing)}
    heights = {name: trace_ground_height(world, center.x + offset_x, center.y + offset_y, center.z, ignored_actors) for name, (offset_x, offset_y) in offsets.items()}
    if any(height is None for height in heights.values()):
        return None, heights
    gradient_x = (heights["plus_x"] - heights["minus_x"]) / (2.0 * spacing)
    gradient_y = (heights["plus_y"] - heights["minus_y"]) / (2.0 * spacing)
    magnitude = math.hypot(gradient_x, gradient_y)
    if magnitude < MIN_GROUND_GRADIENT:
        return None, heights
    return (-gradient_x / magnitude, -gradient_y / magnitude), heights


def measure_density_centroid(live_component, center):
    step_count = int(CENTROID_HALF_SPAN_CM / CENTROID_SPACING_CM)
    mass = 0.0
    weighted_x = 0.0
    weighted_y = 0.0
    for step_x in range(-step_count, step_count + 1):
        for step_y in range(-step_count, step_count + 1):
            location = unreal.Vector(center.x + step_x * CENTROID_SPACING_CM, center.y + step_y * CENTROID_SPACING_CM, center.z)
            sample = sample_fluid(live_component, location)
            if sample is None:
                continue
            mass += sample.primary_density
            weighted_x += sample.primary_density * location.x
            weighted_y += sample.primary_density * location.y
    if mass <= 0.0:
        return 0.0, None
    return mass, (weighted_x / mass, weighted_y / mass)


def make_brush_stamp(location, radius_cm, density_strength, channel):
    stamp = unreal.TDFluidBrushStamp()
    stamp.set_editor_property("world_location", location)
    stamp.set_editor_property("world_velocity", unreal.Vector(0.0, 0.0, 0.0))
    stamp.set_editor_property("radius_cm", radius_cm)
    stamp.set_editor_property("density_strength", density_strength)
    stamp.set_editor_property("velocity_strength", 0.0)
    stamp.set_editor_property("channel", channel)
    return stamp


def slope_item(all_fluids):
    slope_actor = find_slope_fluid(all_fluids)
    if slope_actor is None:
        record_item(ITEM_SLOPE, False, None, "경사로 유체 액터가 없다(레벨의 Slope·Ramp 라벨 액터도, prepare가 만든 TDProbe_SlopeFluid도 없다)")
        return
    world = get_game_world()
    live_component = slope_actor.get_editor_property("live_component")
    center = slope_actor.get_actor_location()
    downhill, heights = measure_downhill_direction(world, center, [slope_actor])
    if downhill is None:
        record_item(ITEM_SLOPE, False, {"slope_actor": slope_actor.get_actor_label(), "ground_heights": heights}, "경사로 유체 중심 주변 지면 기울기를 트레이스로 잡지 못했다")
        return
    ground_z = heights["plus_x"]
    stamp_location = unreal.Vector(center.x, center.y, ground_z)
    live_component.queue_brush_stamp(make_brush_stamp(stamp_location, SLOPE_STAMP_RADIUS_CM, SLOPE_STAMP_DENSITY, unreal.TDFluidBrushChannel.CHANNEL_A_WATER))
    yield frames(2)
    mass_start, centroid_start = measure_density_centroid(live_component, stamp_location)
    slope_sample = sample_fluid(live_component, stamp_location)
    yield seconds(SLOPE_SETTLE_SECONDS)
    mass_end, centroid_end = measure_density_centroid(live_component, stamp_location)
    value = {"slope_actor": slope_actor.get_actor_label(), "ground_heights": heights, "downhill_direction": [round(downhill[0], 3), round(downhill[1], 3)],
             "terrain_slope_at_center": None if slope_sample is None else [round(slope_sample.terrain_slope.x, 4), round(slope_sample.terrain_slope.y, 4)],
             "mass_start": round(mass_start, 3), "mass_end": round(mass_end, 3), "centroid_start": centroid_start, "centroid_end": centroid_end}
    if centroid_start is None or centroid_end is None:
        record_item(ITEM_SLOPE, False, value, "스탬프 뒤 밀도가 샘플되지 않았다(볼륨 밖이거나 밀도 0)")
        return
    shift_x = centroid_end[0] - centroid_start[0]
    shift_y = centroid_end[1] - centroid_start[1]
    shift_along_downhill = shift_x * downhill[0] + shift_y * downhill[1]
    value["shift_cm"] = round(math.hypot(shift_x, shift_y), 2)
    value["shift_along_downhill_cm"] = round(shift_along_downhill, 2)
    note = ""
    if shift_along_downhill < MIN_DOWNHILL_SHIFT_CM:
        note = "밀도 무게중심이 내리막으로 15cm 이상 움직이지 않았다(지형 고도 표본 플래그·TerrainSlopeGravity 확인)"
    record_item(ITEM_SLOPE, shift_along_downhill >= MIN_DOWNHILL_SHIFT_CM, value, note)


def stage_capture_scene(world, level_fluids):
    staged = {"fire_hits": 0, "mud_stamps": 0, "water_stamps": 0}
    subsystem = find_fluid_subsystem(world)
    for dummy in sorted_damage_targets(world):
        combat = dummy.get_component_by_class(unreal.TDCombatComponent)
        if combat is None or not combat.is_alive():
            continue
        if unreal.TDCombatLibrary.try_apply_damage(dummy, make_damage_spec(SCENE_FIRE_DAMAGE_AMOUNT, unreal.TDDamageElement.FIRE)) > 0.0:
            staged["fire_hits"] += 1
    if subsystem is None:
        return staged
    mud_actor = find_fluid_by_label(level_fluids, "MudZone")
    if mud_actor is not None:
        mud_center = mud_actor.get_actor_location()
        for offset_x, offset_y in ((-150.0, 0.0), (0.0, 0.0), (150.0, 100.0)):
            location = unreal.Vector(mud_center.x + offset_x, mud_center.y + offset_y, mud_center.z)
            staged["mud_stamps"] += 1 if subsystem.inject_combat_brush_stamp(make_brush_stamp(location, 110.0, 1.2, unreal.TDFluidBrushChannel.CHANNEL_R_MUD)) > 0 else 0
    pawn, controller = get_player_pawn_and_controller()
    if pawn is not None:
        current_location = pawn.get_actor_location()
        pawn.set_actor_location(unreal.Vector(CAPTURE_PAWN_XY[0], CAPTURE_PAWN_XY[1], current_location.z), False, True)
        pawn_location = pawn.get_actor_location()
        staged["pawn_location"] = vector_as_list(pawn_location)
        location = unreal.Vector(pawn_location.x + 120.0, pawn_location.y, pawn_location.z)
        staged["water_stamps"] += 1 if subsystem.inject_combat_brush_stamp(make_brush_stamp(location, 130.0, 1.0, unreal.TDFluidBrushChannel.CHANNEL_A_WATER)) > 0 else 0
    return staged


def capture_item(level_fluids):
    staged = stage_capture_scene(get_game_world(), level_fluids)
    report["capture_scene"] = staged
    yield seconds(CAPTURE_SCENE_SETTLE_SECONDS)
    if report["capture_mode"] != "driver":
        report["items"][ITEM_CAPTURE]["note"] = "드라이버 없이 단독 실행이라 캡처하지 않았다"
        return
    mark_stage("awaiting_capture")
    flag_seen = yield until(lambda: os.path.exists(CAPTURE_FLAG_PATH), CAPTURE_WAIT_REAL_SECONDS)
    if not flag_seen:
        report["items"][ITEM_CAPTURE]["note"] = "드라이버가 캡처 완료 신호를 주지 않았다"


def scenario():
    scenario_state["started_real"] = time.monotonic()
    mark_stage("waiting_for_fluid_setup")
    ready = yield until(level_fluid_setup_is_ready, READY_TIMEOUT_REAL_SECONDS)
    world = get_game_world()
    all_fluids = collect_fluid_actors(world)
    level_fluids = [actor for actor in all_fluids if not is_probe_actor(actor)]
    report["fluid_actors"] = [actor.get_actor_label() for actor in all_fluids]
    report["setup_ready_within_timeout"] = ready
    report["subsystem_found"] = find_fluid_subsystem(world) is not None
    if not report["subsystem_found"]:
        report["errors"].append("unreal.TDFluidLiveSubsystem.get_fluid_live_subsystem 접근자가 없다(공통 API 계약 미반영 빌드)")
    mark_stage("surface_material_instances")
    evaluate_surface_item(level_fluids)
    mark_stage("walk")
    yield from walk_item(level_fluids)
    mark_stage("fire_damage")
    yield from fire_damage_item(level_fluids)
    mark_stage("death_stamp")
    yield from death_stamp_item(level_fluids)
    mark_stage("teleport_return")
    yield from teleport_item(level_fluids)
    mark_stage("slope_flow")
    yield from slope_item(all_fluids)
    mark_stage("capture_scene")
    yield from capture_item(level_fluids)


def stop_previous_probe():
    handle = getattr(unreal, TICK_HANDLE_ATTRIBUTE, None)
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
        setattr(unreal, TICK_HANDLE_ATTRIBUTE, None)


def start_probe(capture_mode):
    stop_previous_probe()
    world = get_game_world()
    map_path = world.get_path_name() if world else ""
    reset_report(map_path, capture_mode)
    if world is None or LEVEL_NAME not in map_path:
        finish("failed", f"{LEVEL_NAME} PIE가 켜져 있어야 한다(현재 PIE 월드: {map_path or '없음'})")
        return {"started": False, "map": map_path}
    if os.path.exists(CAPTURE_FLAG_PATH):
        os.remove(CAPTURE_FLAG_PATH)
    scenario_state["iterator"] = scenario()
    scenario_state["wait"] = {"kind": "frames", "frames_left": 1}
    write_report()
    setattr(unreal, TICK_HANDLE_ATTRIBUTE, unreal.register_slate_post_tick_callback(tick))
    return {"started": True, "map": map_path, "capture_mode": capture_mode}


def set_first_available_property(structure, property_names, value):
    for property_name in property_names:
        try:
            structure.set_editor_property(property_name, value)
            return property_name
        except Exception:
            continue
    return None


def resolve_preset_configuration(preset):
    resolver = getattr(preset, "resolve_effective_config", None)
    if resolver is not None:
        return resolver()
    return preset.get_editor_property("simulation_config")


def remove_probe_actors(actor_subsystem):
    removed = []
    for actor in list(actor_subsystem.get_all_level_actors()):
        label = actor.get_actor_label()
        if label.startswith(PROBE_ACTOR_PREFIX) and actor_subsystem.destroy_actor(actor):
            removed.append(label)
    return removed


def prepare_slope_scenario():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world = editor.get_editor_world()
    if editor.get_game_world() is not None:
        return {"error": "PIE가 켜져 있다. prepare는 PIE 시작 전에 실행해야 한다"}
    if LEVEL_NAME not in world.get_path_name():
        return {"error": f"에디터 월드가 {LEVEL_NAME}이 아니다: {world.get_path_name()}"}
    removed = remove_probe_actors(actor_subsystem)
    level_slope = find_slope_fluid(collect_fluid_actors(world))
    if level_slope is not None:
        return {"mode": "level", "label": level_slope.get_actor_label(), "removed_stale": removed}
    cube = unreal.EditorAssetLibrary.load_asset(RAMP_MESH_PATH)
    preset = unreal.EditorAssetLibrary.load_asset(WATER_PRESET_PATH)
    if cube is None or preset is None:
        return {"error": f"에셋을 불러오지 못했다: 큐브={cube is not None} 물 프리셋={preset is not None}"}
    center_x, center_y, center_z = SLOPE_RAMP_CENTER
    ramp = actor_subsystem.spawn_actor_from_object(cube, unreal.Vector(center_x, center_y, center_z), unreal.Rotator(pitch=SLOPE_RAMP_PITCH_DEGREES))
    ramp.set_actor_label(SLOPE_RAMP_LABEL)
    ramp.set_actor_scale3d(unreal.Vector(*SLOPE_RAMP_SCALE))
    ramp.set_folder_path("TDGen/FluidFX/ProbeTemporary")
    surface_z = center_z + 0.5 * 100.0 * SLOPE_RAMP_SCALE[2] / math.cos(math.radians(SLOPE_RAMP_PITCH_DEGREES)) + 5.0
    fluid_actor = actor_subsystem.spawn_actor_from_class(unreal.TDFluidLiveActor, unreal.Vector(center_x, center_y, surface_z), unreal.Rotator())
    fluid_actor.set_actor_label(SLOPE_FLUID_LABEL)
    fluid_actor.set_folder_path("TDGen/FluidFX/ProbeTemporary")
    fluid_actor.set_editor_property("should_follow_player_pawn", False)
    fluid_actor.set_editor_property("should_use_activation_volume", False)
    terrain_property = set_first_available_property(fluid_actor, SAMPLE_TERRAIN_PROPERTY_NAMES, True)
    if terrain_property is None or not fluid_actor.get_editor_property(terrain_property):
        return {"error": f"지형 고도 표본 속성을 켜지 못했다: 후보 {SAMPLE_TERRAIN_PROPERTY_NAMES}"}
    configuration = resolve_preset_configuration(preset)
    configuration.set_editor_property("terrain_slope_gravity", max(configuration.get_editor_property("terrain_slope_gravity"), SLOPE_FLUID_GRAVITY))
    live_component = fluid_actor.get_editor_property("live_component")
    live_component.set_editor_property("active_preset", preset)
    live_component.set_editor_property("should_init_system_state_from_preset", False)
    live_component.set_editor_property("simulation_config", configuration)
    return {"mode": "temporary", "ramp": SLOPE_RAMP_LABEL, "fluid": SLOPE_FLUID_LABEL, "terrain_flag_property": terrain_property, "removed_stale": removed}


def cleanup_slope_scenario():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if editor.get_game_world() is not None:
        return {"error": "PIE가 켜져 있어 정리하지 않았다"}
    removed = remove_probe_actors(unreal.get_editor_subsystem(unreal.EditorActorSubsystem))
    return {"removed": removed}


def report_editor_status():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    game_world = editor.get_game_world()
    if game_world is not None:
        return {"pie_running": True, "pie_world": game_world.get_path_name(), "editor_world": None}
    editor_world = editor.get_editor_world()
    return {"pie_running": False, "pie_world": None, "editor_world": editor_world.get_path_name() if editor_world else None}


def read_request():
    if not os.path.exists(REQUEST_PATH):
        return {"action": "start", "capture_mode": "standalone"}
    with open(REQUEST_PATH, encoding="utf-8") as handle:
        request = json.load(handle)
    os.remove(REQUEST_PATH)
    return request


def main():
    request = read_request()
    action = request.get("action", "start")
    handlers = {
        "start": lambda: start_probe(request.get("capture_mode", "standalone")),
        "prepare": prepare_slope_scenario,
        "cleanup": cleanup_slope_scenario,
        "status": report_editor_status,
    }
    handler = handlers.get(action)
    result = handler() if handler else {"error": f"알 수 없는 action: {action}"}
    print(RESULT_MARKER, json.dumps(result, ensure_ascii=False, default=str))


main()
