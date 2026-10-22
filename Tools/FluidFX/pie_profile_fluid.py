"""실행 환경: 시스템 Python(언리얼 에디터가 TDGame을 열고 MCP 포트 8000·원격 실행이 켜진 상태, C++ 콘솔 변수 TD.FluidFX.LogTickCost가 들어간 빌드), 에디터 쪽 조작은 이 파일의 EDITOR_CODE를 원격 실행으로 보낸다. --csv-gpu는 계획 JSON 장면마다 무대 PIE를 열고 효과 켬(A)·끔(B) csvprofile 블록을 ABBA로 찍으며 nvidia-smi 클럭을 함께 기록하고(build_hold.json owner가 --holder와 같아야 하며 장면마다 보류 시각을 갱신), --analyze는 에디터 없이 실행 폴더만 읽는다(utrace는 UnrealInsights로 내보냄). GPU 재구성 크래시(FX-4a 결함, 2026-10-22) 대비로 계획 fresh_pie_after_gpu는 GPU로 재구성한 장면 다음에 액터를 재운 뒤 새 PIE를 열고, configure 단계 stagger_seconds는 액터를 한 번에 하나씩 재구성한다.
실행 명령: python Tools/FluidFX/pie_profile_fluid.py [--phase-seconds 10] (PowerShell, 측정 동안 에디터 창을 앞창으로 두기: 백그라운드 PIE는 3fps로 제한된다 L-editor-06) | --csv-gpu PLAN.json --holder 이름 (예 Tools/FluidFX/fx7p_plan.json 전체·fx7p_plan_run2.json 2회차, PowerShell, 빌드·헤드리스 시험과 동시 금지) | --analyze Saved/FluidFX/fx7p/<run>
출력: Saved/FluidFX/fluid_pie_perf.json, Docs/Validation/FluidFX/D-48-pie-perf-<날짜>.json(측정이 이뤄진 경우, 그날 파일이 이미 있으면 덮지 않고 -<시분초>를 붙임), 표준 출력 구간별 표. 종료 코드 0 전 구간에 TDFluidPerf 줄 있음·나이아가라 활성 토글 통과/1 줄 없는 구간·구간 오염·나이아가라 실패 있음/2 환경 문제(에디터·MCP·PIE·콘솔 변수). --csv-gpu는 Saved/FluidFX/fx7p/<run>/session.json·plan.json·csv/·shots/·traces/·clock.csv·editor.log·result.json(증거 파일은 쓰지 않음), --analyze는 같은 폴더 result.json(J1~J9 verdicts·J6 표, 렌더 스레드는 Exclusive/RenderThread 합(EventWait 제외), S12는 A·B 투과율 텍스처 확인이 없으면 J8 null)과 Docs/Validation/FluidFX/FX-7p-baseline-<날짜>.json·FX-4a-gpu-<날짜>.json(J1·J2·J3·J5 발췌, 그날 파일이 있으면 -<시분초>). 두 경로의 종료 코드 0 완료/1 불안정·A 블록 필수 열 없음·클럭 폐기 과다·추적 분석 실패/2 환경(보류·빌드·셰이더·프레임 상한 관문, 에디터·PIE·CSV 시간 초과)
상태: 현행(2026-10-18 첫 성공, 증거 Docs/Validation/FluidFX/D-48-perf-2026-10-18.md). --csv-gpu·--analyze는 부분 검증(명세 Saved/AgentOps/20261022/fx7p/spec-b.md): 2026-10-22 1회차 PIE 31장면 중 15장면 측정(Saved/FluidFX/fx7p/fx7p-run1-20261022123641, 카메라·스폰·라벨·관문 10%는 그 측정 사본과 같음), GPU 재구성 뒤 에디터 크래시 2회로 중단(리드백 링 결함, 9cdea12e로 수정). 2회차 run2·run2b·run2c로 나머지 장면을 재서 32장면 합본(장면마다 오류 없는 마지막 기록)·--analyze 완료(판정 정본 Saved/AgentOps/20261022/fx7p/run2-judge/judge-final.md·judge-closure.md). 2026-10-22 고침: 큰 결과의 원격 출력 분할 읽기(L-editor-54), 스냅숏 설정 `.copy()`(L-editor-55, 이전 판 reset은 configure 설정을 되돌리지 못했다). 남은 한계: 안정 관문이 장면 FrameTime 하나라 GT 묶임 PIE에서 null이 많다(J별 규칙으로 바꾸는 것은 FX-7p-r)
"""
import argparse
import bisect
import calendar
import csv
import glob
import json
import math
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from datetime import date, datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools"))
sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))
import pie_check_fluid
import run_in_editor
import uemcp

LOG_PATH = ROOT / "Saved" / "Logs" / "TDGame.log"
OUTPUT_PATH = ROOT / "Saved" / "FluidFX" / "fluid_pie_perf.json"
EVIDENCE_PATH = ROOT / "Docs" / "Validation" / "FluidFX" / f"D-48-pie-perf-{date.today().isoformat()}.json"

RUN_TOKEN = datetime.now().strftime("%Y%m%d%H%M%S")
EDITOR_RESULT_MARKER = "TD_FLUID_PERF_RESULT"
CONSOLE_VARIABLE_NAME = "TD.FluidFX.LogTickCost"
ORIGINAL_RESOLUTION_RULE = "original"

PHASE_SECONDS = 10.0
SETUP_TIMEOUT_SECONDS = 60.0
SETUP_POLL_SECONDS = 1.0
LOG_FLUSH_WAIT_SECONDS = 30.0
LOG_FLUSH_POLL_SECONDS = 1.0
SLOW_FRAME_MS = 100.0
TRANSITION_DROP_LINES = 2
NIAGARA_SYSTEM_PATH = "/Game/ArrowTrail/FX/NS_ArrowTrail_Water"
NIAGARA_SETTLE_SECONDS = 2.0
EXIT_MISSING_LINES = 1
EXIT_ENVIRONMENT = 2

PHASE_DEFINITIONS = (
    ("awake_res128", True, None),
    ("all_asleep", False, None),
    ("awake_res64", True, 64),
    ("awake_res256", True, 256),
    ("awake_res128_repeat", True, ORIGINAL_RESOLUTION_RULE),
)
PHASE_NAMES = tuple(name for name, _, _ in PHASE_DEFINITIONS)

FOREGROUND_WARNING = "에디터 창이 앞창이 아니면 PIE가 3fps로 제한되어 비용이 왜곡된다(L-editor-06). 측정 동안 UnrealEditor 창을 앞창으로 두고 건드리지 않는다."

PHASE_MARKER_PATTERN = re.compile(r"TD_FLUID_PERF_PHASE (\w+) (begin|end) run=" + re.escape(RUN_TOKEN) + r"\b")
PERF_LINE_PATTERN = re.compile(r"TDFluidPerf: frames=(\d+) avg_tick_ms=(\S+) max_tick_ms=(\S+) avg_frame_ms=(\S+) awake_actors=(\d+) total_cells=(\d+)")

EDITOR_CODE = '''
import inspect
import json
import time
import traceback

import unreal

PARAMS = json.loads(__PARAMS__)
RESULT_MARKER = "TD_FLUID_PERF_RESULT"
CONSOLE_VARIABLE = "TD.FluidFX.LogTickCost"


def get_game_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def collect_fluid_actors(world):
    return list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDFluidLiveActor))


def log_phase_marker(phase_name, edge):
    unreal.log("TD_FLUID_PERF_PHASE " + phase_name + " " + edge + " run=" + PARAMS["run_token"])


def read_console_variable():
    reader = getattr(unreal.SystemLibrary, "get_console_variable_int_value", None)
    if reader is None:
        return None
    return reader(CONSOLE_VARIABLE)


def describe_actors(actors):
    entries = []
    for actor in actors:
        component = actor.get_editor_property("live_component")
        configuration = component.get_editor_property("simulation_config")
        entries.append({
            "path": actor.get_path_name(),
            "label": actor.get_actor_label(),
            "resolution": configuration.get_editor_property("grid_resolution"),
            "awake": component.is_simulation_awake(),
            "has_runtime_texture": component.get_density_pressure_texture() is not None,
        })
    return entries


def run_setup(world):
    actors = collect_fluid_actors(world)
    if not actors:
        return {"error": "게임 월드에 TDFluidLiveActor가 없다"}
    entries = describe_actors(actors)
    if not all(entry["has_runtime_texture"] for entry in entries):
        return {"ready": False, "actors": entries}
    unreal.SystemLibrary.execute_console_command(world, CONSOLE_VARIABLE + " 1")
    for actor in actors:
        actor.set_editor_property("should_use_activation_volume", False)
    return {"ready": True, "console_variable_value": read_console_variable(), "actors": describe_actors(actors)}


def run_phase(world):
    previous_phase = PARAMS.get("previous_phase")
    if previous_phase:
        log_phase_marker(previous_phase, "end")
    log_phase_marker(PARAMS["phase"], "begin")
    actors = collect_fluid_actors(world)
    resolutions = PARAMS["resolutions"]
    for actor in actors:
        actor.set_editor_property("should_use_activation_volume", False)
        component = actor.get_editor_property("live_component")
        target_resolution = resolutions.get(actor.get_path_name())
        if target_resolution is not None:
            configuration = component.get_editor_property("simulation_config")
            configuration.set_editor_property("grid_resolution", int(target_resolution))
            component.apply_simulation_config(configuration)
        component.set_simulation_awake(PARAMS["awake"])
    return {"phase": PARAMS["phase"], "actors": describe_actors(actors)}


def run_end_phase(world):
    log_phase_marker(PARAMS["phase"], "end")
    return {"ended": PARAMS["phase"]}


def run_finish(world):
    previous_phase = PARAMS.get("previous_phase")
    if world is not None and previous_phase:
        log_phase_marker(previous_phase, "end")
    target_world = world
    if target_world is None:
        target_world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    unreal.SystemLibrary.execute_console_command(target_world, CONSOLE_VARIABLE + " 0")
    return {"console_variable_value": read_console_variable(), "used_game_world": world is not None}


def find_niagara_target(world):
    actors = [actor for actor in collect_fluid_actors(world) if not actor.get_editor_property("should_follow_player_pawn")]
    return actors[0] if actors else None


def collect_owned_niagara_states(actor):
    states = []
    for component in actor.get_components_by_class(unreal.NiagaraComponent):
        asset = component.get_asset()
        states.append({"component": component.get_name(), "asset": asset.get_path_name() if asset else None, "active": component.is_active()})
    return states


def run_niagara_attach(world):
    actor = find_niagara_target(world)
    if actor is None:
        return {"error": "나이아가라 검사 대상 유체 액터가 없다"}
    system = unreal.load_asset(PARAMS["niagara_system"])
    if system is None:
        return {"error": "나이아가라 시스템을 불러오지 못했다: " + PARAMS["niagara_system"]}
    component = actor.get_editor_property("live_component")
    current = component.get_editor_property("active_preset")
    preset = unreal.new_object(unreal.TDFluidPresetAsset)
    if current is not None:
        for name in ("preset_kind", "simulation_config", "default_channel", "output_material", "surface_mesh", "override_preset_defaults"):
            preset.set_editor_property(name, current.get_editor_property(name))
    preset.set_editor_property("external_niagara_system", system)
    component.init_system_state_from_preset(preset)
    component.set_simulation_awake(True)
    return {"actor": actor.get_actor_label(), "states": collect_owned_niagara_states(actor)}


def run_niagara_state(world):
    actor = find_niagara_target(world)
    if actor is None:
        return {"error": "나이아가라 검사 대상 유체 액터가 없다"}
    component = actor.get_editor_property("live_component")
    if PARAMS.get("awake") is not None:
        component.set_simulation_awake(bool(PARAMS["awake"]))
    return {"actor": actor.get_actor_label(), "awake": component.is_simulation_awake(), "states": collect_owned_niagara_states(actor)}


FX7P_SPAWN_TAG = "TD_FX7P_SPAWN"
FX7P_CAMERA_TAG = "TD_FX7P_CAMERA"
FX7P_SNAPSHOT_ATTRIBUTE = "td_fx7p_snapshot"
FX7P_PRESET_FIELD_NAMES = ("preset_kind", "override_preset_defaults", "simulation_config", "default_channel", "output_material", "surface_mesh", "external_niagara_system", "parameter_collection", "external_renderer_actor_tag", "volume_material", "volume_mesh", "volume_height_cm", "plane_settings", "translucency_sort_priority", "volume_render")
FX7P_PART_NAMES = ("cards", "volumes", "embers")
FX7P_GRID_HALF_WIDTH = 3


def fx7p_mark(text):
    unreal.log("TD_FX7P_MARK " + text + " epoch=" + repr(time.time()))


def fx7p_is_spawned(actor):
    return any(str(tag) == FX7P_SPAWN_TAG for tag in actor.tags)


def fx7p_matches(actor, names):
    return actor.get_actor_label() in names or ("spawned" in names and fx7p_is_spawned(actor))


def fx7p_targets(world):
    actors = collect_fluid_actors(world)
    targets = PARAMS.get("targets", "all")
    if targets == "all":
        return actors
    if isinstance(targets, dict):
        excluded = set(targets.get("exclude", []))
        return [actor for actor in actors if not fx7p_matches(actor, excluded)]
    names = set([targets] if isinstance(targets, str) else targets)
    return [actor for actor in actors if fx7p_matches(actor, names)]


def fx7p_find_label(world, label):
    return next((actor for actor in collect_fluid_actors(world) if actor.get_actor_label() == label), None)


def fx7p_part_components(actor):
    ember = actor.get_editor_property("ember_component") if hasattr(actor, "ember_component") else None
    return {"cards": actor.get_editor_property("internal_surface_mesh"), "volumes": actor.get_editor_property("volume_mesh_component"), "embers": ember}


def fx7p_light_components(actor):
    light_actor = actor.get_editor_property("volume_light_actor")
    if light_actor is None:
        return []
    return list(light_actor.get_components_by_class(unreal.LightComponent))


def fx7p_light_mode(actor):
    lights = fx7p_light_components(actor)
    if not lights or not lights[0].is_visible():
        return 0
    if isinstance(lights[0], unreal.DirectionalLightComponent):
        return 2
    return 1 if isinstance(lights[0], unreal.PointLightComponent) else 0


def fx7p_describe_transmittance(component, actor):
    try:
        preset = component.get_editor_property("active_preset")
        enabled = preset.get_editor_property("volume_render").get_editor_property("enable_transmittance") if preset is not None else None
        return {"preset_enabled": enabled, "light_mode": fx7p_light_mode(actor), "texture": component.get_transmittance_texture() is not None}
    except Exception as error:
        return {"error": repr(error)[-200:]}


def fx7p_describe(actor):
    component = actor.get_editor_property("live_component")
    configuration = component.get_editor_property("simulation_config")
    texture = component.get_density_pressure_texture()
    parts = fx7p_part_components(actor)
    return {"label": actor.get_actor_label(), "spawned": fx7p_is_spawned(actor), "awake": component.is_simulation_awake(),
            "grid_resolution": configuration.get_editor_property("grid_resolution"), "backend": str(configuration.get_editor_property("solver_backend")),
            "heat": configuration.get_editor_property("enable_heat_channel"),
            "density_texture_class": texture.get_class().get_name() if texture is not None else None,
            "visible": {name: part.is_visible() for name, part in parts.items() if part is not None},
            "lights_visible": [light.is_visible() for light in fx7p_light_components(actor)],
            "transmittance": fx7p_describe_transmittance(component, actor)}


def fx7p_destroy_tagged(world, tag):
    actors = list(unreal.GameplayStatics.get_all_actors_with_tag(world, tag))
    for actor in actors:
        actor.destroy_actor()
    return len(actors)


def fx7p_view_pawn(world):
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    controller = unreal.GameplayStatics.get_player_controller(world, 0)
    if controller is not None and pawn is not None:
        controller.set_view_target_with_blend(pawn, 0.0)
    return fx7p_destroy_tagged(world, FX7P_CAMERA_TAG)


def fx7p_snapshot(world):
    snapshot = getattr(unreal, FX7P_SNAPSHOT_ATTRIBUTE, None)
    if snapshot is not None and snapshot["pie_session"] == PARAMS["pie_session"] and snapshot.get("run_token") == PARAMS["run_token"]:
        return snapshot
    entries = {}
    for actor in collect_fluid_actors(world):
        if fx7p_is_spawned(actor):
            continue
        component = actor.get_editor_property("live_component")
        parts = fx7p_part_components(actor)
        entries[actor.get_path_name()] = {"config": component.get_editor_property("simulation_config").copy(), "preset": component.get_editor_property("active_preset"),
                                          "awake": component.is_simulation_awake(),
                                          "visible": {name: part.is_visible() for name, part in parts.items() if part is not None},
                                          "lights": [light.is_visible() for light in fx7p_light_components(actor)]}
    snapshot = {"pie_session": PARAMS["pie_session"], "run_token": PARAMS["run_token"], "actors": entries, "configured": set()}
    setattr(unreal, FX7P_SNAPSHOT_ATTRIBUTE, snapshot)
    return snapshot


def fx7p_read_variable(name):
    try:
        return unreal.SystemLibrary.get_console_variable_float_value(name)
    except Exception:
        return None


def fx7p_backend_value(name):
    enum = unreal.TDFluidSolverBackend
    value = getattr(enum, name.upper(), None)
    if value is None:
        value = getattr(enum, name)
    return value


def fx7p_clone_preset(source):
    clone = unreal.new_object(type(source))
    discovered = {name for name, member in inspect.getmembers(type(source)) if inspect.isgetsetdescriptor(member) and not name.startswith("_")}
    failures = []
    for name in sorted(set(FX7P_PRESET_FIELD_NAMES) | discovered):
        try:
            clone.set_editor_property(name, source.get_editor_property(name))
        except Exception:
            failures.append(name)
    return clone, failures


def run_fx7p_snapshot(world):
    snapshot = fx7p_snapshot(world)
    return {"pie_session": snapshot["pie_session"], "actors": [actor.get_actor_label() for actor in collect_fluid_actors(world)]}


def run_fx7p_reset(world):
    snapshot = fx7p_snapshot(world)
    destroyed = fx7p_destroy_tagged(world, FX7P_SPAWN_TAG)
    unreal.SystemLibrary.execute_console_command(world, "TD.FluidFX.SolverBackend 0")
    restored = []
    reapplied = []
    for actor in collect_fluid_actors(world):
        entry = snapshot["actors"].get(actor.get_path_name())
        if entry is None:
            continue
        component = actor.get_editor_property("live_component")
        actor.set_editor_property("should_use_activation_volume", False)
        if entry["preset"] is not None:
            component.set_editor_property("active_preset", entry["preset"])
        if actor.get_path_name() in snapshot["configured"]:
            component.apply_simulation_config(entry["config"])
            reapplied.append(actor.get_actor_label())
        parts = fx7p_part_components(actor)
        for name, visible in entry["visible"].items():
            if parts.get(name) is not None:
                parts[name].set_visibility(visible)
        for light, visible in zip(fx7p_light_components(actor), entry["lights"]):
            light.set_visibility(visible)
        component.set_simulation_awake(entry["awake"])
        restored.append(actor.get_actor_label())
    snapshot["configured"].clear()
    return {"destroyed_spawned": destroyed, "destroyed_cameras": fx7p_view_pawn(world), "restored": restored, "reapplied_config": reapplied}


def run_fx7p_configure(world):
    unreal.SystemLibrary.execute_console_command(world, "TD.FluidFX.SolverBackend 0")
    snapshot = fx7p_snapshot(world)
    targets = sorted(fx7p_targets(world), key=lambda actor: actor.get_path_name())
    start = int(PARAMS.get("batch_start", 0))
    size = PARAMS.get("batch_size")
    batch = targets[start:] if size is None else targets[start:start + int(size)]
    described = []
    for actor in batch:
        snapshot["configured"].add(actor.get_path_name())
        component = actor.get_editor_property("live_component")
        configuration = component.get_editor_property("simulation_config")
        if PARAMS.get("backend") is not None:
            configuration.set_editor_property("solver_backend", fx7p_backend_value(PARAMS["backend"]))
        if PARAMS.get("grid_resolution") is not None:
            configuration.set_editor_property("grid_resolution", int(PARAMS["grid_resolution"]))
        if PARAMS.get("enable_heat_channel") is not None:
            configuration.set_editor_property("enable_heat_channel", bool(PARAMS["enable_heat_channel"]))
        component.apply_simulation_config(configuration)
        described.append(fx7p_describe(actor))
    return {"actors": described, "total": len(targets)}


def run_fx7p_spawn(world):
    source = fx7p_find_label(world, PARAMS["source"])
    if source is None:
        return {"error": "스폰 원본 액터가 없다: " + PARAMS["source"]}
    cells = sorted([(x, y) for x in range(-FX7P_GRID_HALF_WIDTH, FX7P_GRID_HALF_WIDTH + 1) for y in range(-FX7P_GRID_HALF_WIDTH, FX7P_GRID_HALF_WIDTH + 1) if (x, y) != (0, 0)],
                   key=lambda cell: (cell[0] * cell[0] + cell[1] * cell[1], cell))
    count = int(PARAMS["count"])
    if count > len(cells):
        return {"error": "7x7 격자에는 원본 밖 칸이 " + str(len(cells)) + "개뿐이다"}
    spacing = float(PARAMS.get("spacing_cm", 1500.0))
    center = source.get_actor_location()
    preset = source.get_editor_property("live_component").get_editor_property("active_preset")
    light_actor = source.get_editor_property("volume_light_actor")
    spawned = []
    subsystem = unreal.TDFluidLiveSubsystem.get_fluid_live_subsystem(world)
    source_volume = source.get_editor_property("volume_mesh_component")
    for x, y in cells[:count]:
        actor = subsystem.spawn_pooled_fluid_actor(preset, unreal.Vector(center.x + x * spacing, center.y + y * spacing, center.z), 0.0)
        if actor is None:
            return {"error": "SpawnPooledFluidActor가 None을 돌려줬다", "spawned": spawned}
        actor.set_actor_rotation(source.get_actor_rotation(), False)
        actor.set_editor_property("volume_light_actor", light_actor)
        actor.set_editor_property("should_use_activation_volume", False)
        actor.tags = [FX7P_SPAWN_TAG]
        target_volume = actor.get_editor_property("volume_mesh_component")
        if source_volume is not None and target_volume is not None:
            for index in range(source_volume.get_num_materials()):
                target_volume.set_material(index, source_volume.get_material(index))
        spawned.append(actor.get_name())
    return {"spawned": spawned}


def run_fx7p_despawn(world):
    return {"destroyed": fx7p_destroy_tagged(world, FX7P_SPAWN_TAG)}


def run_fx7p_visibility(world):
    parts = PARAMS.get("parts") or list(FX7P_PART_NAMES)
    visible = bool(PARAMS["visible"])
    labels = []
    for actor in fx7p_targets(world):
        components = fx7p_part_components(actor)
        for name in parts:
            if components.get(name) is not None:
                components[name].set_visibility(visible)
        labels.append(actor.get_actor_label())
    return {"visible": visible, "parts": parts, "actors": labels}


def run_fx7p_sleep(world):
    awake = bool(PARAMS["awake"])
    labels = []
    for actor in fx7p_targets(world):
        actor.get_editor_property("live_component").set_simulation_awake(awake)
        labels.append(actor.get_actor_label())
    return {"awake": awake, "actors": labels}


def run_fx7p_light(world):
    visible = bool(PARAMS["visible"])
    count = 0
    for actor in fx7p_targets(world):
        for light in fx7p_light_components(actor):
            light.set_visibility(visible)
            count += 1
    return {"visible": visible, "lights": count}


def run_fx7p_camera(world):
    if PARAMS.get("mode") == "pawn":
        return {"destroyed_cameras": fx7p_view_pawn(world)}
    target = fx7p_find_label(world, PARAMS["target"])
    if target is None:
        return {"error": "카메라 대상 액터가 없다: " + PARAMS["target"]}
    origin = target.get_actor_location()
    rotation = unreal.Rotator(roll=0.0, pitch=float(PARAMS.get("pitch", -20.0)), yaw=float(PARAMS.get("yaw", 0.0)))
    forward = rotation.get_forward_vector()
    distance = float(PARAMS["distance_cm"])
    height = float(PARAMS.get("height_cm", 0.0))
    location = unreal.Vector(origin.x - forward.x * distance, origin.y - forward.y * distance, origin.z + height - forward.z * distance)
    controller = unreal.GameplayStatics.get_player_controller(world, 0)
    camera = controller.get_editor_property("player_camera_manager")
    camera.set_actor_location_and_rotation(location, rotation, False, True)
    controller.set_view_target_with_blend(camera, 0.0)
    return {"location": [location.x, location.y, location.z], "distance_cm": distance, "camera": "player_camera_manager_self_view"}


def run_fx7p_pawn(world):
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    controller = unreal.GameplayStatics.get_player_controller(world, 0)
    if PARAMS.get("x") is not None:
        if controller is not None:
            controller.stop_movement()
        location = pawn.get_actor_location()
        pawn.set_actor_location(unreal.Vector(float(PARAMS["x"]), float(PARAMS["y"]), location.z), False, True)
    if PARAMS.get("goal_x") is not None:
        goal = unreal.Vector(float(PARAMS["goal_x"]), float(PARAMS["goal_y"]), pawn.get_actor_location().z)
        unreal.AIHelperLibrary.simple_move_to_location(controller, goal)
    location = pawn.get_actor_location()
    return {"pawn": [location.x, location.y, location.z]}


def run_fx7p_console(world):
    readback = {}
    for command in PARAMS["commands"]:
        unreal.SystemLibrary.execute_console_command(world, command)
        parts = command.split()
        if len(parts) != 2:
            continue
        try:
            float(parts[1])
        except ValueError:
            continue
        readback[parts[0]] = fx7p_read_variable(parts[0])
    return {"readback": readback}


def run_fx7p_conditions(world):
    size = unreal.WidgetLayoutLibrary.get_viewport_size(world)
    return {"variables": {name: fx7p_read_variable(name) for name in PARAMS["variables"]}, "viewport": [size.x, size.y]}


def run_fx7p_csv(world):
    described = [fx7p_describe(actor) for actor in fx7p_targets(world)]
    fx7p_mark("block_begin:" + PARAMS["block_id"])
    unreal.SystemLibrary.execute_console_command(world, "csvprofile frames=" + str(int(PARAMS["frames"])))
    return {"actors": described}


def run_fx7p_shot(world):
    unreal.SystemLibrary.execute_console_command(world, "HighResShot 1 filename=" + PARAMS["path"])
    return {"requested": PARAMS["path"]}


def run_fx7p_trace(world):
    if PARAMS["mode"] == "start":
        unreal.SystemLibrary.execute_console_command(world, "Trace.Stop")
        unreal.SystemLibrary.execute_console_command(world, "Trace.File " + PARAMS["path"] + " " + PARAMS.get("channels", "cpu"))
        fx7p_mark("trace_begin:" + PARAMS["label"])
        return {"started": PARAMS["path"]}
    fx7p_mark("trace_end:" + PARAMS["label"])
    unreal.SystemLibrary.execute_console_command(world, "Trace.Stop")
    return {"stopped": PARAMS["label"]}


def run_fx7p_trigger(world):
    emitters = list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDFluidFragmentEmitterActor))
    if not emitters:
        return {"error": "조각 방출 액터가 없다"}
    source = emitters[0].get_editor_property("source_breakable_actor")
    if source is None:
        return {"error": "조각 방출 액터에 파괴 액터가 연결되지 않았다"}
    center = source.get_actor_location()
    hit = unreal.Vector(center.x + PARAMS["hit_dx"], center.y + PARAMS["hit_dy"], center.z + PARAMS["hit_dz"])
    emitters[0].trigger_break_at(hit)
    fx7p_mark("trigger")
    return {"hit": [hit.x, hit.y, hit.z], "game_time": unreal.GameplayStatics.get_time_seconds(world)}


def run_fx7p_transmittance(world):
    snapshot = fx7p_snapshot(world)
    results = []
    for actor in fx7p_targets(world):
        component = actor.get_editor_property("live_component")
        entry = snapshot["actors"].get(actor.get_path_name())
        source = entry["preset"] if entry is not None and entry["preset"] is not None else component.get_editor_property("active_preset")
        if source is None:
            return {"error": "활성 프리셋이 없다: " + actor.get_actor_label()}
        clone, failures = fx7p_clone_preset(source)
        render = clone.get_editor_property("volume_render")
        render.set_editor_property("enable_transmittance", bool(PARAMS["enabled"]))
        clone.set_editor_property("volume_render", render)
        component.set_editor_property("active_preset", clone)
        results.append({"label": actor.get_actor_label(), "enabled": clone.get_editor_property("volume_render").get_editor_property("enable_transmittance"), "copy_failures": failures})
    return {"actors": results}


def run_fx7p_marker(world):
    fx7p_mark(PARAMS["text"])
    return {"marked": PARAMS["text"]}


FX7P_HANDLERS = {"snapshot": run_fx7p_snapshot, "reset": run_fx7p_reset, "configure": run_fx7p_configure, "spawn": run_fx7p_spawn, "despawn": run_fx7p_despawn,
                 "visibility": run_fx7p_visibility, "sleep": run_fx7p_sleep, "light": run_fx7p_light, "camera": run_fx7p_camera, "pawn": run_fx7p_pawn,
                 "console": run_fx7p_console, "conditions": run_fx7p_conditions, "csv": run_fx7p_csv, "shot": run_fx7p_shot, "trace": run_fx7p_trace,
                 "trigger": run_fx7p_trigger, "transmittance": run_fx7p_transmittance, "marker": run_fx7p_marker}


def run_action():
    world = get_game_world()
    action = PARAMS["action"]
    if action == "finish":
        return run_finish(world)
    if world is None:
        return {"error": "PIE 게임 월드가 없다"}
    handlers = {"setup": run_setup, "phase": run_phase, "end_phase": run_end_phase, "niagara_attach": run_niagara_attach, "niagara_state": run_niagara_state, **FX7P_HANDLERS}
    handler = handlers.get(action)
    if handler is None:
        return {"error": "알 수 없는 action: " + action}
    return handler(world)


try:
    result = run_action()
except Exception:
    result = {"error": traceback.format_exc()[-600:]}
print(RESULT_MARKER, json.dumps(result, ensure_ascii=False, default=str))
'''


def run_editor_action(action, **fields):
    params = {"action": action, "run_token": RUN_TOKEN, **fields}
    code = EDITOR_CODE.replace("__PARAMS__", repr(json.dumps(params)))
    outcome = run_in_editor.run_code(code)
    if not outcome["connected"]:
        return {"error": "에디터 원격 실행에 연결하지 못했다: " + " | ".join(outcome["lines"])[-300:]}
    for line_index, line in enumerate(outcome["lines"]):
        marker_index = line.find(EDITOR_RESULT_MARKER)
        if marker_index < 0:
            continue
        following_lines = [re.sub(r"^\[[A-Za-z]+\] ", "", next_line) for next_line in outcome["lines"][line_index + 1:]]
        following_text = "\n".join([line[marker_index + len(EDITOR_RESULT_MARKER):]] + following_lines)
        try:
            return json.JSONDecoder().raw_decode(following_text.lstrip())[0]
        except ValueError:
            return {"error": "결과 표식 뒤 JSON을 읽지 못했다: " + line[marker_index:][:300]}
    return {"error": "결과 표식이 없다: " + " | ".join(outcome["lines"])[-400:]}


def read_log_size():
    if not LOG_PATH.exists():
        return 0
    return LOG_PATH.stat().st_size


def read_log_lines_since(offset):
    if not LOG_PATH.exists():
        return []
    start = offset if LOG_PATH.stat().st_size >= offset else 0
    with LOG_PATH.open("rb") as handle:
        handle.seek(start)
        return handle.read().decode("utf-8", errors="replace").splitlines()


def wait_for_end_marker(log_offset, last_phase_name):
    needle = f"TD_FLUID_PERF_PHASE {last_phase_name} end run={RUN_TOKEN}"
    deadline = time.monotonic() + LOG_FLUSH_WAIT_SECONDS
    while True:
        lines = read_log_lines_since(log_offset)
        if any(needle in line for line in lines) or time.monotonic() >= deadline:
            return lines
        time.sleep(LOG_FLUSH_POLL_SECONDS)


def parse_perf_line(text):
    match = PERF_LINE_PATTERN.search(text)
    if match is None:
        return None
    try:
        return {"frames": int(match.group(1)), "avg_tick_ms": float(match.group(2)), "max_tick_ms": float(match.group(3)),
                "avg_frame_ms": float(match.group(4)), "awake_actors": int(match.group(5)), "total_cells": int(match.group(6))}
    except ValueError:
        return None


def parse_phase_lines(log_lines):
    phases = {}
    current_phase = None
    for text in log_lines:
        marker = PHASE_MARKER_PATTERN.search(text)
        if marker is not None:
            current_phase = marker.group(1) if marker.group(2) == "begin" else None
            if current_phase is not None:
                phases.setdefault(current_phase, [])
            continue
        if current_phase is None:
            continue
        perf_line = parse_perf_line(text)
        if perf_line is not None:
            phases[current_phase].append(perf_line)
    return phases


def expected_editor_state(actor_entries):
    awake_entries = [entry for entry in actor_entries if entry["awake"]]
    return {"awake_actors": len(awake_entries), "total_cells": sum(entry["resolution"] ** 2 for entry in awake_entries)}


def summarize_phase(phase_record, perf_lines):
    summary = {"started": phase_record is not None, "lines": 0, "lines_total": len(perf_lines), "dropped_transition_lines": 0,
               "frames_total": None, "avg_tick_ms": None, "max_tick_ms": None, "avg_frame_ms": None,
               "awake_actors": None, "total_cells": None, "state_consistent": None, "matches_editor_state": None,
               "expected_editor_state": None, "driver_wait_seconds": None, "raw_lines": perf_lines}
    if phase_record is not None:
        summary["expected_editor_state"] = expected_editor_state(phase_record["actors"])
        summary["driver_wait_seconds"] = phase_record.get("driver_wait_seconds")
    drop_count = TRANSITION_DROP_LINES if len(perf_lines) > TRANSITION_DROP_LINES + 1 else min(1, len(perf_lines) - 1)
    kept_lines = perf_lines[max(drop_count, 0):]
    if not kept_lines:
        return summary
    summary["lines"] = len(kept_lines)
    summary["dropped_transition_lines"] = len(perf_lines) - len(kept_lines)
    summary["frames_total"] = sum(line["frames"] for line in kept_lines)
    frames_total = max(summary["frames_total"], 1)
    summary["avg_tick_ms"] = round(sum(line["avg_tick_ms"] * line["frames"] for line in kept_lines) / frames_total, 4)
    summary["max_tick_ms"] = round(max(line["max_tick_ms"] for line in kept_lines), 4)
    summary["avg_frame_ms"] = round(sum(line["avg_frame_ms"] * line["frames"] for line in kept_lines) / frames_total, 3)
    summary["awake_actors"] = kept_lines[-1]["awake_actors"]
    summary["total_cells"] = kept_lines[-1]["total_cells"]
    summary["state_consistent"] = len({(line["awake_actors"], line["total_cells"]) for line in kept_lines}) == 1
    expected = summary["expected_editor_state"]
    if expected is not None:
        summary["matches_editor_state"] = expected["awake_actors"] == summary["awake_actors"] and expected["total_cells"] == summary["total_cells"]
    return summary


def build_resolution_targets(resolution_rule, original_entries):
    if resolution_rule is None:
        return {}
    if resolution_rule == ORIGINAL_RESOLUTION_RULE:
        return {entry["path"]: entry["resolution"] for entry in original_entries}
    return {entry["path"]: resolution_rule for entry in original_entries}


def wait_for_fluid_setup():
    started = time.monotonic()
    result = {"error": "유체 액터 준비를 확인하지 못했다"}
    while time.monotonic() - started < SETUP_TIMEOUT_SECONDS:
        result = run_editor_action("setup")
        if "error" in result or result.get("ready"):
            return result
        time.sleep(SETUP_POLL_SECONDS)
    return {"error": f"{SETUP_TIMEOUT_SECONDS:.0f}초 안에 모든 유체 액터의 실행 중 텍스처가 준비되지 않았다", "last": result}


def run_phases(phase_seconds, report):
    previous_phase = None
    window_started = None
    for name, should_wake, resolution_rule in PHASE_DEFINITIONS:
        if previous_phase is not None:
            report["phases"][previous_phase]["driver_wait_seconds"] = round(time.monotonic() - window_started, 2)
        result = run_editor_action("phase", phase=name, previous_phase=previous_phase, awake=should_wake,
                                   resolutions=build_resolution_targets(resolution_rule, report["setup_actors"]))
        if "error" in result:
            return f"구간 {name} 전환 실패: {result['error']}"
        report["phases"][name] = {"awake": should_wake, "resolution_rule": resolution_rule, "actors": result["actors"]}
        report["last_started_phase"] = name
        print(f"phase {name} begin: awake={should_wake} resolution_rule={resolution_rule} {phase_seconds:.0f}초 대기", flush=True)
        previous_phase = name
        window_started = time.monotonic()
        time.sleep(phase_seconds)
    report["phases"][previous_phase]["driver_wait_seconds"] = round(time.monotonic() - window_started, 2)
    return None


def owned_niagara_entries(state):
    expected_path = NIAGARA_SYSTEM_PATH + "." + NIAGARA_SYSTEM_PATH.rsplit("/", 1)[-1]
    return [entry for entry in state.get("states", []) if entry.get("asset") == expected_path]


def read_niagara_state(awake=None):
    if awake is not None:
        run_editor_action("niagara_state", awake=awake)
        time.sleep(NIAGARA_SETTLE_SECONDS)
    return run_editor_action("niagara_state", awake=None)


def check_niagara_activation(report):
    attach = run_editor_action("niagara_attach", niagara_system=NIAGARA_SYSTEM_PATH)
    if "error" in attach:
        report["niagara_check"] = {"passed": False, "error": attach["error"]}
        return
    time.sleep(NIAGARA_SETTLE_SECONDS)
    spawned = read_niagara_state()
    asleep = read_niagara_state(awake=False)
    awake = read_niagara_state(awake=True)
    stages = {"spawned": owned_niagara_entries(spawned), "asleep": owned_niagara_entries(asleep), "awake_again": owned_niagara_entries(awake)}
    passed = (len(stages["spawned"]) == 1 and stages["spawned"][0]["active"]
              and len(stages["asleep"]) == 1 and not stages["asleep"][0]["active"]
              and len(stages["awake_again"]) == 1 and stages["awake_again"][0]["active"])
    report["niagara_check"] = {"passed": passed, "actor": attach.get("actor"), "system": NIAGARA_SYSTEM_PATH, "stages": stages,
                               "criterion": "프리셋 ExternalNiagaraSystem 컴포넌트가 정확히 1개 생기고 활성, 잠들면 비활성, 깨면 다시 활성"}
    print("niagara", json.dumps(report["niagara_check"], ensure_ascii=False), flush=True)


def finish_measurement(report):
    previous_phase = None if report.get("last_phase_end_logged") else report["last_started_phase"]
    result = run_editor_action("finish", previous_phase=previous_phase)
    if "error" in result:
        return f"측정 종료 처리 실패: {result['error']}"
    report["finish_logged"] = True
    if result.get("console_variable_value") in (0, None):
        report["console_variable_state"] = "off"
    return None


def measure_in_pie(arguments, report):
    if not pie_check_fluid.wait_for_pie_state(True, pie_check_fluid.PIE_START_WAIT_SECONDS):
        return "PIE 게임 월드가 나타나지 않았다"
    report["console_variable_state"] = "maybe_on"
    setup_result = wait_for_fluid_setup()
    if "error" in setup_result:
        return f"측정 준비 실패: {setup_result['error']}"
    report["console_variable_state"] = "on"
    report["setup_actors"] = setup_result["actors"]
    report["console_variable_readback"] = setup_result.get("console_variable_value")
    print("setup", json.dumps({"actors": len(setup_result["actors"]), "resolutions": [entry["resolution"] for entry in setup_result["actors"]],
                               "console_variable_readback": report["console_variable_readback"]}, ensure_ascii=False), flush=True)
    if report["console_variable_readback"] not in (1, None):
        return f"콘솔 변수 {CONSOLE_VARIABLE_NAME}이 1이 되지 않았다(읽은 값 {report['console_variable_readback']}): C++ 재빌드 뒤 에디터를 재시작했는지 확인"
    phase_error = run_phases(arguments.phase_seconds, report)
    if phase_error is not None:
        return phase_error
    end_result = run_editor_action("end_phase", phase=report["last_started_phase"])
    if "error" in end_result:
        return f"마지막 구간 종료 표식 실패: {end_result['error']}"
    report["last_phase_end_logged"] = True
    check_niagara_activation(report)
    return finish_measurement(report)


def shut_down_pie(session, report, pie_started):
    environment = report["environment"]
    if pie_started and not report["finish_logged"] and report["console_variable_state"] in ("on", "maybe_on"):
        finish_measurement(report)
    pie_ended = pie_check_fluid.stop_pie(session, environment) if pie_started else pie_check_fluid.wait_for_pie_state(False, pie_check_fluid.PIE_STOP_WAIT_SECONDS)
    environment["pie_ended"] = pie_ended
    if report["console_variable_state"] not in ("on", "maybe_on"):
        return
    restore_result = run_editor_action("finish", previous_phase=None)
    if "error" not in restore_result and restore_result.get("console_variable_value") in (0, None):
        report["console_variable_state"] = "off" if restore_result.get("console_variable_value") == 0 else "off_unverified"
        return
    report["warnings"].append(f"{CONSOLE_VARIABLE_NAME}을 0으로 되돌리지 못했다. 에디터 콘솔에서 직접 0으로 돌린다")


def run_session(arguments, report):
    environment = report["environment"]
    level_status = pie_check_fluid.open_arena_level()
    environment["level"] = level_status
    if not level_status.get("loaded"):
        return "레벨을 열지 못했다(PIE가 이미 켜져 있거나 에셋·에디터 연결이 없다): " + json.dumps(level_status, ensure_ascii=False)[:300]
    try:
        session = uemcp.connect()
    except (OSError, ValueError, KeyError) as error:
        return f"MCP 서버(127.0.0.1:8000)에 연결하지 못했다: {error}"
    pie_started = pie_check_fluid.start_pie(session, environment)
    try:
        if not pie_started:
            return "PIE를 시작하지 못했다: " + environment.get("start_pie_error", "")[:300]
        return measure_in_pie(arguments, report)
    finally:
        shut_down_pie(session, report, pie_started)


def collect_phase_summaries(report, log_offset):
    log_lines = wait_for_end_marker(log_offset, report["last_started_phase"]) if report["finish_logged"] else read_log_lines_since(log_offset)
    parsed = parse_phase_lines(log_lines)
    return {name: summarize_phase(report["phases"].get(name), parsed.get(name, [])) for name in PHASE_NAMES}


def format_cell(value, pattern):
    return "-" if value is None else pattern.format(value)


def print_table(summaries):
    print(f"{'phase':<22}{'lines':>6}{'avg_tick_ms':>13}{'max_tick_ms':>13}{'avg_frame_ms':>14}{'awake':>7}{'total_cells':>13}  state")
    for name in PHASE_NAMES:
        summary = summaries[name]
        state = {True: "ok", False: "DIFF", None: "-"}[summary["matches_editor_state"]]
        print(f"{name:<22}{summary['lines']:>6}{format_cell(summary['avg_tick_ms'], '{:.4f}'):>13}{format_cell(summary['max_tick_ms'], '{:.4f}'):>13}"
              f"{format_cell(summary['avg_frame_ms'], '{:.3f}'):>14}{format_cell(summary['awake_actors'], '{}'):>7}{format_cell(summary['total_cells'], '{}'):>13}  {state}")


def add_slow_frame_warning(report, summaries):
    slow_phases = [name for name, summary in summaries.items()
                   if summary["avg_frame_ms"] is not None and summary["avg_tick_ms"] is not None
                   and summary["avg_frame_ms"] - summary["avg_tick_ms"] >= SLOW_FRAME_MS]
    if slow_phases:
        report["warnings"].append(f"유체 틱을 뺀 나머지 프레임 시간이 {SLOW_FRAME_MS:.0f}ms 이상인 구간 {', '.join(slow_phases)}: 백그라운드 3fps 제한이 의심된다(L-editor-06)")


def write_report(report, exit_code):
    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(report, indent=2, ensure_ascii=False)
    OUTPUT_PATH.write_text(text, encoding="utf-8")
    if exit_code == EXIT_ENVIRONMENT:
        return
    evidence_path = EVIDENCE_PATH
    if evidence_path.exists():
        evidence_path = EVIDENCE_PATH.with_name(f"{EVIDENCE_PATH.stem}-{datetime.now().strftime('%H%M%S')}.json")
    evidence_path.parent.mkdir(parents=True, exist_ok=True)
    evidence_path.write_text(text, encoding="utf-8")
    print("evidence", evidence_path.relative_to(ROOT))


FX7P_OUTPUT_ROOT = ROOT / "Saved" / "FluidFX" / "fx7p"
FX7P_EVIDENCE_DIRECTORY = ROOT / "Docs" / "Validation" / "FluidFX"
FX7P_PROFILING_CSV_DIRECTORY = ROOT / "Saved" / "Profiling" / "CSV"
FX7P_EDITOR_DLL = ROOT / "Binaries" / "Win64" / "UnrealEditor-TDGame.dll"
FX7P_SOURCE_DIRECTORY = ROOT / "Source"
FX7P_SESSION_NAME = "session.json"
FX7P_PLAN_COPY_NAME = "plan.json"
FX7P_RESULT_NAME = "result.json"
FX7P_LOG_COPY_NAME = "editor.log"
FX7P_CLOCK_NAME = "clock.csv"
HEADER_AT_END_MARKER = "[HasHeaderRowAtEnd]"
GPU_SUM_COLUMN = "GPU/* 합(패스별 + Unaccounted)"
GPU_PASS_PREFIX = "GPU/"
RENDER_THREAD_SUM_COLUMN = "Exclusive/RenderThread/* 합(EventWait 제외)"
RENDER_THREAD_PREFIX = "Exclusive/RenderThread/"
RENDER_THREAD_WAIT_PREFIX = "Exclusive/RenderThread/EventWait"
FRAME_COLUMN = "FrameTime"
STEP_COUNT_COLUMN = "TDFluid/StepCount"
SOLVER_COLUMN = "GPU/TDFluidGpuSolver"
TRANSLUCENCY_COLUMN = "GPU/Translucency"
TICK_METRIC = "tick_ms"
FX7P_METRIC_COLUMNS = (FRAME_COLUMN, "GameThreadTime", RENDER_THREAD_SUM_COLUMN, "GPUTime", SOLVER_COLUMN, TRANSLUCENCY_COLUMN, STEP_COUNT_COLUMN, "GPUMem/LocalUsedMB")
FX7P_CLOCK_COMMAND = ["nvidia-smi", "--query-gpu=timestamp,clocks.gr,clocks.mem,temperature.gpu,power.draw,pstate", "--format=csv,noheader,nounits"]
FX7P_STEP_TIMER = "TDFluid_StepFluidWorld"
FX7P_SOLVER_TIMER = "TDFluid_RunSolverSteps"
FX7P_RECONFIGURE_ACTIONS = ("configure", "spawn", "despawn", "transmittance")
FX7P_LOG_TIME_PATTERN = re.compile(r"^\[(\d{4})\.(\d{2})\.(\d{2})-(\d{2})\.(\d{2})\.(\d{2}):(\d{3})\]")
FX7P_MARK_PATTERN = re.compile(r"TD_FX7P_MARK (\S+)")
FX7P_BUDGET_PATTERN = re.compile(r"TDFluidGpuPressureBudget: multigrid1_x1_median_ms=([\d.]+).*?multigrid1_x1_p95_ms=([\d.]+).*?multigrid1_x4_median_ms=([\d.]+)")
FX7P_TEST_RESULT_PATTERN = re.compile(r"Test Completed\. Result=\{(\w+)\} Name=\{([^}]*)\}")
FX7P_SVOL_BUDGET_MS = {("HARD", "none"): 1.0, ("HARD", "point"): 2.0, ("SOFT", "none"): 4.0, ("SOFT", "point"): 4.0}
FX7P_SVOL_REFERENCE_MPX = 0.52
FX7P_THRESHOLD_RESOLUTIONS = (96, 128, 192, 256)
FX7P_THRESHOLD_COUNTS = (1, 16)
FX7P_DEFAULTS = {
    "run_label": "fx7p",
    "cvars": ["t.MaxFPS 1000", "r.VSync 0", "r.VSyncEditor 0", "r.GPUCsvStatsEnabled 1", "r.DynamicRes.OperationMode 0", "r.ScreenPercentage 100",
              "TD.FluidFX.LogTickCost 1", "TD.FluidFX.SolverBackend 0"],
    "recorded_variables": ["t.MaxFPS", "r.VSync", "r.VSyncEditor", "r.GPUCsvStatsEnabled", "r.DynamicRes.OperationMode", "r.ScreenPercentage",
                           "r.Editor.Viewport.OverridePIEScreenPercentage", "r.AntiAliasingMethod", "r.VolumetricFog", "sg.ResolutionQuality",
                           "sg.ViewDistanceQuality", "sg.AntiAliasingQuality", "sg.ShadowQuality", "sg.GlobalIlluminationQuality", "sg.ReflectionQuality",
                           "sg.PostProcessQuality", "sg.TextureQuality", "sg.EffectsQuality", "sg.FoliageQuality", "sg.ShadingQuality"],
    "hash_paths": ["Content/FluidFX/Maps/LV_TDFluidFireStage.umap", "Content/FluidFX/Materials/MI_TD_FluidFireCharredWood.uasset",
                   "Content/FluidFX/Materials/MI_TD_FluidFireFloor.uasset", "Content/FluidFX/Destruction/GC_TD_FluidExplosionCube.uasset"],
    "block_frames": 300, "discard_frames": 60, "order": "ABBA", "repeats": 2, "unstable_repeats": 3,
    "fresh_pie_after_gpu": False, "gpu_quiesce_seconds": 1.0,
    "settle_seconds": 3.0, "reconfigure_settle_seconds": 5.0, "initial_warmup_seconds": 90.0, "csv_timeout_seconds": 120.0, "file_timeout_seconds": 60.0,
    "warmup": {"frames": 100, "spike_ratio": 3.0, "p50_tolerance": 0.10, "consecutive": 2, "max_attempts": 5,
               "p50_tolerance_reason": "1회차(2026-10-22) 예열 블록 연속 p50 변화 40개: 최대 9.7%(D1)·90번째 5.7%, 셰이더 돌출 0~1개. 3%는 에디터 Slate·TSR 잡음만으로 S2를 막았다(fx7p-run1-20261022123052, 종료 2). 셰이더 판단은 돌출 0과 10% 안 두 번 연속으로 한다"},
    "cap_gate": {"frame_ms": 8.33, "tolerance": 0.03, "cluster_ms": 0.05, "cluster_share": 0.5},
    "clock": {"interval_ms": 200, "tolerance": 0.03, "retries": 2, "minimum_blocks": 3, "wait_seconds": 3.0},
    "stability": {"relative": 0.10, "small_diff_ms": 0.2, "absolute_ms": 0.05},
    "occupancy": {"threshold": 2, "max_iterations": 3, "min_distance_cm": 500.0, "max_step_ratio": 2.0},
    "offscreen_logs": [], "headless_logs": [], "bench_outputs": [],
    "scenes": [],
}


def fx7p_percentile(values, fraction):
    if not values:
        return None
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = math.floor(position)
    upper = math.ceil(position)
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def fx7p_median(values):
    return fx7p_percentile(values, 0.5)


def fx7p_round(value, digits=4):
    return None if value is None else round(value, digits)


def fx7p_now():
    return time.strftime("%Y-%m-%d %H:%M:%S")


def write_fx7p_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False, default=str), encoding="utf-8")


def merge_fx7p_plan(plan):
    config = json.loads(json.dumps(FX7P_DEFAULTS))
    for key, value in plan.items():
        if isinstance(value, dict) and isinstance(config.get(key), dict):
            config[key] = {**config[key], **value}
            continue
        config[key] = value
    return config


def validate_fx7p_plan(config):
    if not config["scenes"]:
        return "계획에 scenes가 없다"
    seen = set()
    for scene in config["scenes"]:
        scene_id = scene.get("id")
        if not scene_id or scene_id in seen:
            return f"장면 id가 없거나 중복이다: {scene_id}"
        seen.add(scene_id)
        if not scene.get("level"):
            return f"{scene_id}: level이 없다"
        if scene.get("kind", "csv") == "csv" and not ("a" in scene and "b" in scene and "targets" in scene):
            return f"{scene_id}: csv 장면에는 targets·a·b가 있어야 한다"
        if scene.get("kind") == "trace" and "trace_steps" not in scene:
            return f"{scene_id}: trace 장면에는 trace_steps가 있어야 한다"
    if set(config["order"]) != {"A", "B"} or config["order"].count("A") != config["order"].count("B"):
        return "order는 A와 B를 같은 수로 가져야 한다(예 ABBA)"
    return None


def read_csv_columns(path, discard_rows=0):
    with open(path, encoding="utf-8", errors="replace", newline="") as handle:
        rows = list(csv.reader(handle))
    if not rows:
        return {}
    metadata_index = next((index for index, row in enumerate(rows) if row and row[0].startswith("[")), len(rows))
    has_header_at_end = metadata_index < len(rows) and HEADER_AT_END_MARKER in rows[metadata_index]
    header = rows[metadata_index - 1] if has_header_at_end else rows[0]
    data_rows = rows[1:metadata_index - 1] if has_header_at_end else rows[1:metadata_index]
    data_rows = data_rows[discard_rows:]
    header = [name.strip() for name in header]
    columns = {name: [] for name in header}
    columns[GPU_SUM_COLUMN] = []
    gpu_indices = [index for index, name in enumerate(header) if name.startswith(GPU_PASS_PREFIX)]
    render_indices = [index for index, name in enumerate(header) if name.startswith(RENDER_THREAD_PREFIX) and not name.startswith(RENDER_THREAD_WAIT_PREFIX)]
    if render_indices:
        columns[RENDER_THREAD_SUM_COLUMN] = []
    for row in data_rows:
        if len(row) > len(header):
            continue
        for name, value in zip(header, row):
            try:
                columns[name].append(float(value))
            except ValueError:
                continue
        columns[GPU_SUM_COLUMN].append(sum(read_float(row[index]) for index in gpu_indices if index < len(row)))
        if render_indices:
            columns[RENDER_THREAD_SUM_COLUMN].append(sum(read_float(row[index]) for index in render_indices if index < len(row)))
    return columns


def read_float(text):
    try:
        return float(text)
    except ValueError:
        return 0.0


def summarize_fx7p_values(values):
    if not values:
        return None
    return {"n": len(values), "p50": fx7p_round(fx7p_percentile(values, 0.5)), "p95": fx7p_round(fx7p_percentile(values, 0.95)),
            "mean": fx7p_round(sum(values) / len(values)), "max": fx7p_round(max(values))}


def count_fx7p_spikes(frames, ratio):
    median = fx7p_median(frames)
    if not median:
        return None
    return sum(1 for value in frames if value > ratio * median)


def describe_fx7p_csv(path, discard_rows, spike_ratio):
    columns = read_csv_columns(path, discard_rows)
    frames = columns.get(FRAME_COLUMN) or []
    stats = {name: summarize_fx7p_values(columns[name]) for name in FX7P_METRIC_COLUMNS if columns.get(name)}
    return {"rows": len(frames), "spikes": count_fx7p_spikes(frames, spike_ratio), "stats": stats, "frames": frames}


def check_fx7p_frame_cap(frames, rule):
    if not frames:
        return {"tripped": None, "reason": "FrameTime 없음"}
    ordered = sorted(frames)
    width = rule["cluster_ms"]
    largest = max(bisect.bisect_right(ordered, value + width) - bisect.bisect_left(ordered, value - width) for value in ordered)
    share = largest / len(ordered)
    p50 = fx7p_median(ordered)
    near_cap = abs(p50 - rule["frame_ms"]) <= rule["frame_ms"] * rule["tolerance"]
    return {"tripped": near_cap or share >= rule["cluster_share"], "p50": fx7p_round(p50), "cluster_share": round(share, 3), "near_cap": near_cap}


def parse_fx7p_clock_line(line):
    parts = [part.strip() for part in line.split(",")]
    if len(parts) < 6:
        return None
    try:
        stamp = datetime.strptime(parts[0], "%Y/%m/%d %H:%M:%S.%f").timestamp()
        graphics = float(parts[1])
    except ValueError:
        return None
    return {"t": stamp, "graphics_mhz": graphics, "memory_mhz": read_float(parts[2]), "temperature_c": read_float(parts[3]), "power_w": read_float(parts[4]), "pstate": parts[5]}


def read_fx7p_clock_output(sampler):
    with sampler["output_path"].open("w", encoding="utf-8") as handle:
        for line in sampler["process"].stdout:
            handle.write(line)
            handle.flush()
            sample = parse_fx7p_clock_line(line)
            if sample is None:
                continue
            with sampler["lock"]:
                sampler["samples"].append(sample)


def start_fx7p_clock_sampler(output_path, interval_ms):
    sampler = {"process": None, "samples": [], "lock": threading.Lock(), "output_path": output_path, "error": None, "thread": None}
    try:
        sampler["process"] = subprocess.Popen(FX7P_CLOCK_COMMAND + ["-lms", str(int(interval_ms))], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                              text=True, encoding="utf-8", errors="replace")
    except OSError as error:
        sampler["error"] = f"nvidia-smi 실행 실패: {error}"
        return sampler
    sampler["thread"] = threading.Thread(target=read_fx7p_clock_output, args=(sampler,), daemon=True)
    sampler["thread"].start()
    return sampler


def stop_fx7p_clock_sampler(sampler):
    if sampler["process"] is None:
        return
    sampler["process"].terminate()
    try:
        sampler["process"].wait(timeout=5)
    except subprocess.TimeoutExpired:
        sampler["process"].kill()
    sampler["thread"].join(timeout=5)


def copy_fx7p_clock_samples(sampler):
    with sampler["lock"]:
        return list(sampler["samples"])


def read_fx7p_clock_samples(path):
    if not path.exists():
        return []
    samples = [parse_fx7p_clock_line(line) for line in path.read_text(encoding="utf-8", errors="replace").splitlines()]
    return [sample for sample in samples if sample is not None]


def average_fx7p_clock(samples, start, end):
    values = [sample["graphics_mhz"] for sample in samples if start <= sample["t"] <= end]
    return sum(values) / len(values) if values else None


def check_fx7p_holder(holder):
    import build_and_test
    hold_path = Path(build_and_test.HOLD_FILE)
    if not hold_path.is_file():
        return None, f"보류 파일이 없다({hold_path.relative_to(ROOT).as_posix()}): build_and_test.py --acquire-hold {holder} 먼저"
    try:
        hold = json.loads(hold_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        return None, f"보류 파일을 읽지 못했다: {error}"
    if hold.get("owner") != holder:
        return hold, f"보류 owner {hold.get('owner')}가 --holder {holder}와 다르다"
    age_seconds = time.time() - hold_path.stat().st_mtime
    if age_seconds > build_and_test.HOLD_EXPIRY_SECONDS:
        return hold, f"보류가 {age_seconds / 3600:.1f}시간 지나 다른 세션이 무시한다: 해제 뒤 다시 획득한다"
    return {**hold, "age_seconds": round(age_seconds)}, None


def refresh_fx7p_hold(context):
    import build_and_test
    hold_path = Path(build_and_test.HOLD_FILE)
    hold, error = check_fx7p_holder(context["holder"])
    if error is not None:
        return error
    os.utime(hold_path, None)
    return None


def check_fx7p_build_gate():
    if not FX7P_EDITOR_DLL.exists():
        return {"passed": False, "reason": "UnrealEditor-TDGame.dll이 없다"}
    dll_time = FX7P_EDITOR_DLL.stat().st_mtime
    newer = [path.relative_to(ROOT).as_posix() for path in FX7P_SOURCE_DIRECTORY.rglob("*") if path.is_file() and path.stat().st_mtime > dll_time]
    return {"passed": not newer, "dll": FX7P_EDITOR_DLL.relative_to(ROOT).as_posix(), "dll_modified": datetime.fromtimestamp(dll_time).isoformat(timespec="seconds"),
            "newer_sources": newer[:20], "newer_source_count": len(newer)}


def hash_fx7p_assets(paths):
    hashes = {}
    for relative in paths:
        completed = subprocess.run(["git", "hash-object", relative], cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")
        hashes[relative] = completed.stdout.strip() if completed.returncode == 0 else None
    return hashes


def list_fx7p_profiling_csvs():
    return set(glob.glob(str(FX7P_PROFILING_CSV_DIRECTORY / "**" / "*.csv"), recursive=True))


def is_fx7p_csv_complete(path):
    try:
        with open(path, "rb") as handle:
            handle.seek(max(os.path.getsize(path) - 8192, 0))
            tail = handle.read().decode("utf-8", errors="replace")
    except OSError:
        return False
    lines = [line for line in tail.splitlines() if line.strip()]
    return bool(lines) and lines[-1].startswith("[")


def wait_for_fx7p_csv(before, timeout_seconds):
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        fresh = sorted(list_fx7p_profiling_csvs() - before, key=os.path.getmtime)
        if fresh and is_fx7p_csv_complete(fresh[-1]):
            return fresh[-1], time.time()
        time.sleep(0.25)
    return None, time.time()


def wait_for_fx7p_file(path, timeout_seconds):
    deadline = time.monotonic() + timeout_seconds
    previous_size = -1
    while time.monotonic() < deadline:
        size = path.stat().st_size if path.exists() else 0
        if size > 0 and size == previous_size:
            return True
        previous_size = size
        time.sleep(1.0)
    return False


def run_fx7p_action(context, action, **fields):
    return run_editor_action(action, pie_session=context["pie_session"], **fields)


def resolve_fx7p_step(step, scene):
    params = {key: value for key, value in step.items() if key != "action"}
    targets = params.get("targets", "scene")
    if targets == "scene":
        params["targets"] = scene.get("targets", "all")
    elif targets == "others":
        params["targets"] = {"exclude": scene.get("targets", [])}
    return params


def run_fx7p_staggered_configure(context, params):
    seconds = float(params.pop("stagger_seconds"))
    params["batch_size"] = int(params.pop("stagger_batch", 1))
    params["batch_start"] = 0
    described = []
    while True:
        result = run_fx7p_action(context, "configure", **params)
        if "error" in result:
            return result
        described.extend(result["actors"])
        params["batch_start"] += params["batch_size"]
        if params["batch_start"] >= result["total"]:
            return {"actors": described, "total": result["total"], "stagger_seconds": seconds, "batch_size": params["batch_size"]}
        time.sleep(seconds)


def execute_fx7p_steps(context, scene, steps, records):
    for step in steps:
        action = step["action"]
        if action == "wait":
            time.sleep(float(step["seconds"]))
            continue
        params = resolve_fx7p_step(step, scene)
        if action == "configure" and params.get("stagger_seconds") is not None:
            result = run_fx7p_staggered_configure(context, params)
        else:
            result = run_fx7p_action(context, action, **params)
        if records is not None:
            records.append({"action": action, "result": result})
        if "error" in result:
            return f"{scene['id']} {action} 실패: {str(result['error'])[-300:]}"
    return None


def stop_fx7p_pie(context):
    if not context["pie_running"]:
        return
    if context["gpu_dirty"]:
        context["session"].setdefault("gpu_quiesce", []).append(run_fx7p_action(context, "sleep", targets="all", awake=False))
        time.sleep(context["config"]["gpu_quiesce_seconds"])
        context["gpu_dirty"] = False
    context["environment"]["pie_ended"] = pie_check_fluid.stop_pie(context["mcp"], context["environment"])
    context["pie_running"] = False


def format_fx7p_variable(name, value):
    text = str(int(value)) if float(value).is_integer() else repr(float(value))
    return f"{name} {text}"


def ensure_fx7p_pie(context, level, fresh):
    if context["pie_running"] and context["level"] == level and not fresh:
        return None
    stop_fx7p_pie(context)
    import pie_showcase_fluid
    level_status = pie_showcase_fluid.open_stage_level(level)
    if not level_status.get("loaded"):
        return "레벨을 열지 못했다: " + json.dumps(level_status, ensure_ascii=False)[:300]
    context["level"] = level
    context["pie_running"] = pie_check_fluid.start_pie(context["mcp"], context["environment"])
    if not context["pie_running"]:
        return "PIE를 시작하지 못했다: " + context["environment"].get("start_pie_error", "")[:300]
    if not pie_check_fluid.wait_for_pie_state(True, pie_check_fluid.PIE_START_WAIT_SECONDS):
        return "PIE 게임 월드가 나타나지 않았다"
    context["pie_session"] += 1
    setup = wait_for_fluid_setup()
    if "error" in setup:
        return f"유체 액터 준비 실패: {setup['error']}"
    config = context["config"]
    if context["previous_variables"] is None:
        names = [command.split()[0] for command in config["cvars"] if len(command.split()) == 2]
        before = run_fx7p_action(context, "conditions", variables=names)
        context["previous_variables"] = before.get("variables", {})
    applied = run_fx7p_action(context, "console", commands=config["cvars"])
    if "error" in applied:
        return f"콘솔 변수 적용 실패: {applied['error']}"
    snapshot = run_fx7p_action(context, "snapshot")
    if "error" in snapshot:
        return f"초기 상태 기록 실패: {snapshot['error']}"
    conditions = run_fx7p_action(context, "conditions", variables=config["recorded_variables"])
    context["session"]["pie_sessions"].append({"index": context["pie_session"], "level": level, "started": fx7p_now(), "cvar_readback": applied.get("readback"),
                                               "conditions": conditions, "actors": snapshot.get("actors")})
    if context["pie_session"] == 1 and config["initial_warmup_seconds"] > 0:
        print(f"첫 PIE 예열 {config['initial_warmup_seconds']:.0f}초", flush=True)
        time.sleep(config["initial_warmup_seconds"])
    return None


def prepare_fx7p_scene(context, scene, record, fresh):
    error = ensure_fx7p_pie(context, scene["level"], fresh)
    if error is not None:
        return error
    hold_error = refresh_fx7p_hold(context)
    if hold_error is not None:
        return hold_error
    reset = run_fx7p_action(context, "reset")
    record.setdefault("resets", []).append(reset)
    if "error" in reset:
        return f"{scene['id']} 초기화 실패: {reset['error']}"
    record["conditions"] = run_fx7p_action(context, "conditions", variables=context["config"]["recorded_variables"])
    return execute_fx7p_steps(context, scene, scene.get("setup", []), record["steps"])


def apply_fx7p_side(context, scene, side):
    steps = scene[side.lower()]
    error = execute_fx7p_steps(context, scene, steps, None)
    if error is not None:
        return error
    reconfigured = any(step["action"] in FX7P_RECONFIGURE_ACTIONS for step in steps)
    time.sleep(context["config"]["reconfigure_settle_seconds"] if reconfigured else context["config"]["settle_seconds"])
    return None


def capture_fx7p_csv(context, scene, block_id, frames, discard_rows):
    before = list_fx7p_profiling_csvs()
    started = time.time()
    result = run_fx7p_action(context, "csv", block_id=block_id, frames=frames, targets=scene["targets"])
    if "error" in result:
        return None, f"{block_id} csvprofile 요청 실패: {result['error']}"
    source, finished = wait_for_fx7p_csv(before, context["config"]["csv_timeout_seconds"])
    if source is None:
        return None, f"{block_id}: {context['config']['csv_timeout_seconds']:.0f}초 안에 CSV가 완성되지 않았다(Saved/Profiling/CSV)"
    target = context["run_dir"] / "csv" / f"{block_id}.csv"
    shutil.copy2(source, target)
    description = describe_fx7p_csv(target, discard_rows, context["config"]["warmup"]["spike_ratio"])
    block = {"block_id": block_id, "scene": scene["id"], "csv": target.relative_to(context["run_dir"]).as_posix(), "source_csv": source,
             "t_start": round(started, 3), "t_end": round(finished, 3), "rows": description["rows"], "spikes": description["spikes"],
             "stats": description["stats"], "actors": result.get("actors")}
    return block, description["frames"]


def judge_fx7p_block_clock(context, block):
    rule = context["config"]["clock"]
    deadline = time.monotonic() + rule["wait_seconds"]
    while time.monotonic() < deadline:
        samples = copy_fx7p_clock_samples(context["clock"])
        if samples and samples[-1]["t"] >= block["t_end"]:
            break
        time.sleep(0.2)
    mhz = average_fx7p_clock(copy_fx7p_clock_samples(context["clock"]), block["t_start"], block["t_end"])
    if mhz is None:
        return True, None
    accepted = context["gpu_clock_values"]
    if len(accepted) >= rule["minimum_blocks"]:
        median = fx7p_median(accepted)
        if abs(mhz - median) > median * rule["tolerance"]:
            return False, round(mhz, 1)
    accepted.append(mhz)
    return True, round(mhz, 1)


def capture_fx7p_slot(context, scene, record, side, slot):
    config = context["config"]
    for attempt in range(config["clock"]["retries"] + 1):
        block_id = slot if attempt == 0 else f"{slot}_retry{attempt}"
        block, frames = capture_fx7p_csv(context, scene, block_id, scene.get("block_frames", config["block_frames"]), config["discard_frames"])
        if block is None:
            return frames
        block.update({"kind": "block", "side": side, "slot": slot, "attempt": attempt, "status": "ok", "clock_mhz": None})
        record["blocks"].append(block)
        if side == "B" and record.get("cap_gate_active"):
            cap = check_fx7p_frame_cap(frames, config["cap_gate"])
            record.setdefault("cap_gate", []).append({"block_id": block_id, **cap})
            if cap["tripped"]:
                return f"{block_id} 상한 관문: FrameTime p50 {cap['p50']}ms·한 값 몰림 {cap['cluster_share']}(프레임 상한이 풀리지 않았다)"
        if not scene.get("gpu_clock"):
            return None
        accepted, mhz = judge_fx7p_block_clock(context, block)
        block["clock_mhz"] = mhz
        if accepted:
            return None
        block["status"] = "discarded_clock"
        time.sleep(1.0)
    return None


def run_fx7p_shader_gate(context, scene):
    rule = context["config"]["warmup"]
    error = apply_fx7p_side(context, scene, "A")
    if error is not None:
        return {"passed": False, "error": error, "attempts": []}
    attempts = []
    previous = None
    consecutive = 0
    for attempt in range(rule["max_attempts"]):
        block, frames = capture_fx7p_csv(context, scene, f"{scene['id']}_warm{attempt + 1}", rule["frames"], 0)
        if block is None:
            return {"passed": False, "error": frames, "attempts": attempts}
        p50 = fx7p_median(frames)
        spikes = count_fx7p_spikes(frames, rule["spike_ratio"])
        steady = previous is not None and p50 is not None and previous > 0 and abs(p50 - previous) / previous <= rule["p50_tolerance"]
        passed = spikes == 0 and steady
        consecutive = consecutive + 1 if passed else 0
        attempts.append({"block_id": block["block_id"], "p50": fx7p_round(p50), "spikes": spikes, "passed": passed})
        previous = p50
        if consecutive >= rule["consecutive"]:
            return {"passed": True, "attempts": attempts}
    return {"passed": False, "attempts": attempts}


def run_fx7p_rounds(context, scene, record):
    config = context["config"]
    round_index = 0
    while True:
        for position, side in enumerate(config["order"]):
            slot = f"{scene['id']}_r{round_index + 1}_{position + 1}{side}"
            error = apply_fx7p_side(context, scene, side)
            if error is None:
                error = capture_fx7p_slot(context, scene, record, side, slot)
            if error is not None:
                return error
        round_index += 1
        record["rounds"] = round_index
        if round_index < config["repeats"]:
            continue
        if round_index >= config["unstable_repeats"]:
            return None
        quick = analyze_fx7p_csv_scene(scene, record, {"config": config, "run_dir": context["run_dir"]})
        if quick["stable"]:
            return None
        print(f"scene {scene['id']} 불안정(변동 {quick['variation']}): 한 바퀴 더", flush=True)


def measure_fx7p_occupancy(path_a, path_b, threshold):
    import numpy
    from PIL import Image
    with Image.open(path_a) as image_a, Image.open(path_b) as image_b:
        pixels_a = numpy.asarray(image_a.convert("RGB"), dtype=numpy.int16)
        pixels_b = numpy.asarray(image_b.convert("RGB"), dtype=numpy.int16)
    if pixels_a.shape != pixels_b.shape:
        return None
    mask = numpy.abs(pixels_a - pixels_b).max(axis=2) > threshold
    opened = dilate_fx7p_mask(erode_fx7p_mask(mask))
    height, width = mask.shape
    return {"fraction": round(float(opened.sum()) / mask.size, 5), "pixels": int(opened.sum()), "raw_pixels": int(mask.sum()), "image": [width, height]}


def shift_fx7p_windows(mask, fill):
    import numpy
    padded = numpy.pad(mask, 1, constant_values=fill)
    height, width = mask.shape
    return [padded[row:row + height, column:column + width] for row in range(3) for column in range(3)]


def erode_fx7p_mask(mask):
    import numpy
    return numpy.logical_and.reduce(shift_fx7p_windows(mask, False))


def dilate_fx7p_mask(mask):
    import numpy
    return numpy.logical_or.reduce(shift_fx7p_windows(mask, False))


def take_fx7p_shot(context, path):
    result = run_fx7p_action(context, "shot", path=path.as_posix())
    if "error" in result:
        return result["error"]
    if not wait_for_fx7p_file(path, context["config"]["file_timeout_seconds"]):
        return f"스크린숏이 생기지 않았다: {path.name}"
    return None


def calibrate_fx7p_occupancy(context, scene, record):
    config = context["config"]
    occupancy = scene["occupancy"]
    camera = dict(occupancy["camera"])
    target_fraction = occupancy.get("target_fraction")
    max_iterations = occupancy.get("max_iterations", config["occupancy"]["max_iterations"])
    min_distance = occupancy.get("min_distance_cm", config["occupancy"]["min_distance_cm"])
    step_ratio = config["occupancy"]["max_step_ratio"]
    mask_light = bool(occupancy.get("mask_light"))
    before = run_fx7p_action(context, "conditions", variables=["r.AntiAliasingMethod"])
    previous_aa = (before.get("variables") or {}).get("r.AntiAliasingMethod")
    run_fx7p_action(context, "console", commands=["r.AntiAliasingMethod 0"])
    iterations = []
    error = None
    try:
        if mask_light:
            lit = run_fx7p_action(context, "light", targets=scene["targets"], visible=True)
            if "error" in lit:
                return f"{scene['id']} 점유 마스크용 광원 켜기 실패: {lit['error']}"
        for iteration in range(max_iterations + 1):
            placed = run_fx7p_action(context, "camera", **camera)
            if "error" in placed:
                error = f"{scene['id']} 카메라 실패: {placed['error']}"
                break
            paths = {}
            for side in ("A", "B"):
                error = apply_fx7p_side(context, scene, side)
                if error is not None:
                    break
                paths[side] = context["run_dir"] / "shots" / f"{scene['id']}_{iteration}{side}.png"
                error = take_fx7p_shot(context, paths[side])
                if error is not None:
                    break
            if error is not None:
                break
            measured = measure_fx7p_occupancy(paths["A"], paths["B"], config["occupancy"]["threshold"])
            iterations.append({"iteration": iteration, "camera": dict(camera), "measured": measured,
                               "shots": [path.relative_to(context["run_dir"]).as_posix() for path in paths.values()]})
            if measured is None or not target_fraction or target_fraction[0] <= measured["fraction"] <= target_fraction[1] or iteration == max_iterations:
                break
            goal = (target_fraction[0] + target_fraction[1]) / 2.0
            ratio = math.sqrt(measured["fraction"] / goal) if measured["fraction"] > 0 else 1.0 / step_ratio
            distance = camera["distance_cm"] * min(max(ratio, 1.0 / step_ratio), step_ratio)
            if distance < min_distance and camera["distance_cm"] <= min_distance:
                break
            camera["distance_cm"] = max(distance, min_distance)
    finally:
        if previous_aa is not None:
            run_fx7p_action(context, "console", commands=[format_fx7p_variable("r.AntiAliasingMethod", previous_aa)])
        if mask_light:
            run_fx7p_action(context, "light", targets=scene["targets"], visible=False)
    record["occupancy"] = {"iterations": iterations, "final": iterations[-1]["measured"] if iterations else None, "camera": camera,
                           "target_fraction": target_fraction, "antialiasing_restored": previous_aa, "mask": "light_proxy" if mask_light else "difference",
                           "min_distance_cm": min_distance}
    return error


def run_fx7p_trace_scene(context, scene, record):
    record["traces"] = []
    for repeat in range(scene.get("repeats", 3)):
        if repeat > 0 and scene.get("pie_per_repeat"):
            error = prepare_fx7p_scene(context, scene, record, True)
            if error is not None:
                return error
        label = f"{scene['id']}_{repeat + 1}"
        path = context["run_dir"] / "traces" / f"{label}.utrace"
        started = run_fx7p_action(context, "trace", mode="start", path=path.as_posix(), label=label, channels=scene.get("channels", "cpu"))
        if "error" in started:
            return f"{label} 추적 시작 실패: {started['error']}"
        step_error = execute_fx7p_steps(context, scene, scene["trace_steps"], record["steps"])
        stopped = run_fx7p_action(context, "trace", mode="stop", label=label)
        exists = wait_for_fx7p_file(path, context["config"]["file_timeout_seconds"])
        record["traces"].append({"label": label, "path": path.relative_to(context["run_dir"]).as_posix(), "exists": exists, "stop": stopped})
        if step_error is not None:
            return step_error
        if not exists:
            return f"{label}: utrace 파일이 생기지 않았다"
    return None


def restore_fx7p_scene_console(context, scene, record):
    names = [command.split()[0] for step in scene.get("setup", []) if step["action"] == "console" and step.get("restore")
             for command in step["commands"] if len(command.split()) == 2]
    previous = (record.get("conditions") or {}).get("variables") or {}
    commands = [format_fx7p_variable(name, previous[name]) for name in names if previous.get(name) is not None]
    if not commands:
        return None
    return run_fx7p_action(context, "console", commands=commands)


def fx7p_reconfigures_gpu(scene):
    steps = [step for key in ("setup", "a", "b", "trace_steps") for step in scene.get(key, [])]
    return any(step["action"] == "configure" and str(step.get("backend", "")).lower() == "gpu" for step in steps)


def run_fx7p_scene(context, scene):
    kind = scene.get("kind", "csv")
    record = {"id": scene["id"], "kind": kind, "level": scene["level"], "meta": scene.get("meta", {}), "started": fx7p_now(), "steps": [], "blocks": [], "error": None}
    context["session"]["scenes"].append(record)
    record["cap_gate_active"] = bool(scene.get("cap_gate", kind == "csv" and not context["cap_gate_done"]))
    after_gpu = bool(context["config"]["fresh_pie_after_gpu"] and context["gpu_dirty"])
    record["fresh_pie"] = "plan" if scene.get("fresh_pie") else "after_gpu" if after_gpu else None
    error = prepare_fx7p_scene(context, scene, record, bool(scene.get("fresh_pie")) or after_gpu)
    context["gpu_dirty"] = context["gpu_dirty"] or fx7p_reconfigures_gpu(scene)
    if error is None and kind == "trace":
        error = run_fx7p_trace_scene(context, scene, record)
    elif error is None:
        if scene.get("occupancy"):
            error = calibrate_fx7p_occupancy(context, scene, record)
        if error is None:
            record["shader_gate"] = run_fx7p_shader_gate(context, scene)
            record["shader_gate"]["p50_tolerance"] = context["config"]["warmup"]["p50_tolerance"]
            if not record["shader_gate"]["passed"]:
                error = f"{scene['id']} 셰이더 관문 실패: {record['shader_gate'].get('error') or record['shader_gate']['attempts']}"
        if error is None:
            error = run_fx7p_rounds(context, scene, record)
        context["cap_gate_done"] = context["cap_gate_done"] or record["cap_gate_active"]
    if context["pie_running"]:
        record["restored_console"] = restore_fx7p_scene_console(context, scene, record)
    record["finished"] = fx7p_now()
    record["error"] = error
    write_fx7p_json(context["run_dir"] / FX7P_SESSION_NAME, context["session"])
    return error


def run_fx7p_scenes(context):
    try:
        context["mcp"] = uemcp.connect()
    except (OSError, ValueError, KeyError) as error:
        return f"MCP 서버(127.0.0.1:8000)에 연결하지 못했다: {error}"
    for scene in context["config"]["scenes"]:
        print(f"scene {scene['id']} 시작 {fx7p_now()}", flush=True)
        error = run_fx7p_scene(context, scene)
        if error is not None:
            return error
    return None


def finish_fx7p_session(context, log_offset):
    session = context["session"]
    if context["pie_running"] and context["previous_variables"]:
        commands = [format_fx7p_variable(name, value) for name, value in context["previous_variables"].items() if value is not None]
        session["restored_variables"] = run_fx7p_action(context, "console", commands=commands)
    if context["pie_running"]:
        session["finish"] = run_editor_action("finish", previous_phase=None)
    stop_fx7p_pie(context)
    stop_fx7p_clock_sampler(context["clock"])
    log_lines = read_log_lines_since(log_offset)
    (context["run_dir"] / FX7P_LOG_COPY_NAME).write_text("\n".join(log_lines), encoding="utf-8")
    session["finished"] = fx7p_now()
    write_fx7p_json(context["run_dir"] / FX7P_SESSION_NAME, session)


def run_fx7p_plan(plan_path, holder):
    try:
        plan = json.loads(plan_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        print("환경 문제: 계획 JSON을 읽지 못했다:", error)
        return EXIT_ENVIRONMENT
    config = merge_fx7p_plan(plan)
    problem = validate_fx7p_plan(config)
    if problem is not None:
        print("환경 문제: 계획 오류:", problem)
        return EXIT_ENVIRONMENT
    hold, hold_error = check_fx7p_holder(holder)
    if hold_error is not None:
        print("환경 문제: 보류 거부:", hold_error)
        return EXIT_ENVIRONMENT
    build_gate = check_fx7p_build_gate()
    if not build_gate["passed"]:
        print("환경 문제: 빌드 관문:", json.dumps(build_gate, ensure_ascii=False)[:600])
        return EXIT_ENVIRONMENT
    run_dir = FX7P_OUTPUT_ROOT / f"{config['run_label']}-{RUN_TOKEN}"
    for name in ("csv", "shots", "traces"):
        (run_dir / name).mkdir(parents=True, exist_ok=True)
    write_fx7p_json(run_dir / FX7P_PLAN_COPY_NAME, config)
    session = {"tool": "pie_profile_fluid --csv-gpu", "run_token": RUN_TOKEN, "plan": str(plan_path), "started": fx7p_now(), "holder": holder, "hold": hold,
               "build_gate": build_gate, "asset_hashes": hash_fx7p_assets(config["hash_paths"]), "warnings": [FOREGROUND_WARNING], "pie_sessions": [],
               "scenes": [], "environment": {}, "environment_error": None, "restored_variables": None}
    context = {"config": config, "run_dir": run_dir, "session": session, "holder": holder, "environment": session["environment"], "mcp": None,
               "level": None, "pie_running": False, "pie_session": 0, "previous_variables": None, "gpu_clock_values": [], "cap_gate_done": False, "gpu_dirty": False}
    print("경고:", FOREGROUND_WARNING, flush=True)
    log_offset = read_log_size()
    context["clock"] = start_fx7p_clock_sampler(run_dir / FX7P_CLOCK_NAME, config["clock"]["interval_ms"])
    if context["clock"]["error"] is not None:
        session["warnings"].append(context["clock"]["error"] + ": 클럭 판정은 하지 않는다")
    try:
        session["environment_error"] = run_fx7p_scenes(context)
    except Exception as error:
        session["environment_error"] = f"예외: {type(error).__name__}: {error}"[:500]
    finally:
        finish_fx7p_session(context, log_offset)
    if session["environment_error"] is not None:
        print("환경 문제:", session["environment_error"])
        print("run", run_dir.relative_to(ROOT).as_posix(), "exit_code", EXIT_ENVIRONMENT)
        return EXIT_ENVIRONMENT
    return analyze_fx7p_run(run_dir, False)


def read_fx7p_log(path):
    entries = []
    if not path.exists():
        return entries
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = FX7P_LOG_TIME_PATTERN.match(line)
        stamp = None
        if match is not None:
            year, month, day, hour, minute, second, millisecond = (int(group) for group in match.groups())
            stamp = calendar.timegm((year, month, day, hour, minute, second)) + millisecond / 1000.0
        entries.append((stamp, line))
    return entries


def collect_fx7p_perf_lines(entries):
    lines = []
    for stamp, text in entries:
        perf_line = parse_perf_line(text)
        if stamp is not None and perf_line is not None:
            lines.append((stamp, perf_line))
    return lines


def find_fx7p_block_windows(entries):
    windows = {}
    pending = None
    started = None
    for stamp, text in entries:
        mark = FX7P_MARK_PATTERN.search(text)
        if mark is not None and mark.group(1).startswith("block_begin:"):
            pending = mark.group(1)[len("block_begin:"):]
            started = None
            continue
        if pending is None or stamp is None:
            continue
        if started is None and "Capture started" in text:
            started = stamp
        elif started is not None and ("Capture Ending" in text or "Capture Ended" in text):
            windows[pending] = (started, stamp)
            pending = None
    return windows


def find_fx7p_trace_windows(entries):
    begins = {}
    windows = {}
    for stamp, text in entries:
        mark = FX7P_MARK_PATTERN.search(text)
        if mark is None or stamp is None:
            continue
        name = mark.group(1)
        if name.startswith("trace_begin:"):
            begins[name[len("trace_begin:"):]] = stamp
        elif name.startswith("trace_end:") and name[len("trace_end:"):] in begins:
            label = name[len("trace_end:"):]
            windows[label] = (begins[label], stamp)
    return windows


def fx7p_tick_in_window(perf_lines, window):
    if window is None:
        return None
    start, end = window
    inside = [line for stamp, line in perf_lines if stamp <= end and stamp - line["frames"] * line["avg_frame_ms"] / 1000.0 >= start]
    if not inside:
        return None
    frames = sum(line["frames"] for line in inside)
    return {"lines": len(inside), "frames": frames, "avg_tick_ms": round(sum(line["avg_tick_ms"] * line["frames"] for line in inside) / max(frames, 1), 4),
            "max_tick_ms": max(line["max_tick_ms"] for line in inside)}


def fx7p_zero_stats():
    return {"n": 0, "p50": 0.0, "p95": 0.0, "mean": 0.0, "max": 0.0, "assumed_zero": True}


def summarize_fx7p_block(block, context):
    config = context["config"]
    path = context["run_dir"] / block["csv"]
    summary = {"block_id": block["block_id"], "side": block["side"], "slot": block["slot"], "attempt": block.get("attempt", 0), "status": block.get("status", "ok"),
               "rows": 0, "spikes": None, "clock_mhz": block.get("clock_mhz"), "stats": {}, "assumed_zero": [], "actors": block.get("actors") or [], "missing_csv": not path.exists()}
    if summary["missing_csv"]:
        return summary
    columns = read_csv_columns(path, config["discard_frames"])
    frames = columns.get(FRAME_COLUMN) or []
    stats = {name: summarize_fx7p_values(columns[name]) for name in FX7P_METRIC_COLUMNS if columns.get(name)}
    if block["side"] == "B":
        if STEP_COUNT_COLUMN not in stats:
            stats[STEP_COUNT_COLUMN] = fx7p_zero_stats()
            summary["assumed_zero"].append(STEP_COUNT_COLUMN)
        if SOLVER_COLUMN not in stats and stats[STEP_COUNT_COLUMN]["max"] == 0:
            stats[SOLVER_COLUMN] = fx7p_zero_stats()
            summary["assumed_zero"].append(SOLVER_COLUMN)
        if TRANSLUCENCY_COLUMN not in stats:
            stats[TRANSLUCENCY_COLUMN] = fx7p_zero_stats()
            summary["assumed_zero"].append(TRANSLUCENCY_COLUMN)
    tick = fx7p_tick_in_window(context.get("perf_lines", []), context.get("block_windows", {}).get(block["block_id"]))
    if tick is not None:
        stats[TICK_METRIC] = {"n": tick["lines"], "p50": tick["avg_tick_ms"], "p95": None, "mean": tick["avg_tick_ms"], "max": tick["max_tick_ms"]}
    clock = average_fx7p_clock(context.get("clock_samples", []), block["t_start"], block["t_end"])
    if clock is not None:
        summary["clock_mhz"] = round(clock, 1)
    summary.update({"rows": len(frames), "spikes": count_fx7p_spikes(frames, config["warmup"]["spike_ratio"]), "stats": stats})
    return summary


def choose_fx7p_attempt(attempts, gpu_judged, context):
    median = context.get("clock_median")
    if not (gpu_judged and median):
        return next((attempt for attempt in attempts if attempt["status"] == "ok" and not attempt["missing_csv"]), None)
    tolerance = context["config"]["clock"]["tolerance"]
    for attempt in attempts:
        if attempt["missing_csv"]:
            continue
        if attempt["clock_mhz"] is None or abs(attempt["clock_mhz"] - median) <= median * tolerance:
            return attempt
    return None


def collect_fx7p_side_values(blocks, metric):
    p50s = [block["stats"][metric]["p50"] for block in blocks if block["stats"].get(metric) and block["stats"][metric]["p50"] is not None]
    p95s = [block["stats"][metric]["p95"] for block in blocks if block["stats"].get(metric) and block["stats"][metric]["p95"] is not None]
    maxima = [block["stats"][metric]["max"] for block in blocks if block["stats"].get(metric) and block["stats"][metric]["max"] is not None]
    if not p50s:
        return None
    return {"p50": fx7p_round(fx7p_median(p50s)), "p95": fx7p_round(fx7p_median(p95s)), "max": fx7p_round(max(maxima)) if maxima else None, "blocks": len(p50s)}


def compute_fx7p_diff(valid_pairs, metric):
    entry = {}
    for quantile in ("p50", "p95"):
        values = []
        for a, b in valid_pairs:
            a_stats = a["stats"].get(metric)
            b_stats = b["stats"].get(metric)
            if a_stats and b_stats and a_stats[quantile] is not None and b_stats[quantile] is not None:
                values.append(a_stats[quantile] - b_stats[quantile])
        entry[quantile] = fx7p_round(fx7p_median(values))
        entry["pairs_" + quantile] = [round(value, 4) for value in values]
        entry["range_" + quantile] = fx7p_round(max(values) - min(values)) if values else None
    return entry if entry["pairs_p50"] else None


def judge_fx7p_stability(entry, rule):
    if entry is None or len(entry["pairs_p50"]) < 2:
        return False, {"kind": "pairs", "value": 0 if entry is None else len(entry["pairs_p50"])}
    if abs(entry["p50"]) < rule["small_diff_ms"]:
        return entry["range_p50"] <= rule["absolute_ms"], {"kind": "absolute_ms", "value": entry["range_p50"]}
    relative = entry["range_p50"] / abs(entry["p50"])
    return relative <= rule["relative"], {"kind": "relative", "value": round(relative, 4)}


def check_fx7p_backend(plan_scene, a_blocks, b_blocks, a_values):
    expected = (plan_scene.get("meta") or {}).get("backend")
    actors = a_blocks[0]["actors"] if a_blocks else []
    awake = [actor for actor in actors if actor.get("awake")]
    uses_render_target = ["RenderTarget" in str(actor.get("density_texture_class")) for actor in awake]
    solver = (a_values.get(SOLVER_COLUMN) or {}).get("p50")
    step_a = (a_values.get(STEP_COUNT_COLUMN) or {}).get("max")
    step_b = (collect_fx7p_side_values(b_blocks, STEP_COUNT_COLUMN) or {}).get("max")
    backend_ok = None
    if expected == "Gpu":
        backend_ok = bool(awake) and all(uses_render_target) and (solver or 0) > 0
    elif expected == "Cpu":
        backend_ok = bool(awake) and not any(uses_render_target)
    return {"expected": expected, "awake_targets": len(awake), "density_texture_classes": sorted({str(actor.get("density_texture_class")) for actor in awake}),
            "solver_p50": solver, "step_count_max_a": step_a, "step_count_max_b": step_b, "backend_ok": backend_ok,
            "step_count_ok": None if step_a is None else step_a == len(awake)}


def check_fx7p_transmittance(plan_scene, a_blocks, b_blocks):
    if not (plan_scene.get("meta") or {}).get("transmittance_ab"):
        return None
    sides = {}
    for side, blocks, expected in (("a", a_blocks, True), ("b", b_blocks, False)):
        states = [actor.get("transmittance") for block in blocks for actor in block["actors"] if actor.get("awake")]
        sides[side] = {"expected_texture": expected, "actors": len(states), "texture": sorted({str((state or {}).get("texture")) for state in states}),
                       "preset_enabled": sorted({str((state or {}).get("preset_enabled")) for state in states}),
                       "light_mode": sorted({(state or {}).get("light_mode") for state in states}, key=str)}
        if not states or any(state is None or "texture" not in state for state in states):
            sides[side]["ok"] = None
            continue
        sides[side]["ok"] = all(state["texture"] is expected for state in states)
    flags = [sides[side]["ok"] for side in sides]
    active = None if None in flags else all(flags)
    reason = None
    if active is None:
        reason = "블록 기술에 투과율 기록 없음(구판 실행)"
    elif not active:
        reason = "A에서 투과율 출력 텍스처가 없거나 B에서 남아 있다(IsTransmittanceActive: 프리셋 플래그·광원 Mode>0·초기화)"
    return {"active": active, "reason": reason, "sides": sides}


def compute_fx7p_occupancy(plan_scene, record, diff):
    final = (record.get("occupancy") or {}).get("final")
    conditions = record.get("conditions") or {}
    viewport = conditions.get("viewport")
    percentage = (conditions.get("variables") or {}).get("r.ScreenPercentage")
    if not final or not viewport:
        return None, None, "점유 측정 없음"
    if not percentage or percentage <= 0:
        return None, None, "r.ScreenPercentage가 자동(≤0)이라 내부 화소를 모른다"
    internal_pixels = viewport[0] * viewport[1] * (percentage / 100.0) ** 2
    occupancy_px = final["fraction"] * internal_pixels
    metric_diff = (diff.get(plan_scene.get("occupancy_metric", TRANSLUCENCY_COLUMN)) or {}).get("p50")
    if metric_diff is None or occupancy_px <= 0:
        return round(occupancy_px), None, "차분 또는 점유 화소 없음"
    return round(occupancy_px), round(metric_diff / (occupancy_px / 1e6), 4), None


def analyze_fx7p_csv_scene(plan_scene, record, context):
    config = context["config"]
    gpu_judged = bool(plan_scene.get("gpu_clock"))
    slots = {}
    for block in record.get("blocks", []):
        if block.get("kind") == "block":
            slots.setdefault(block["slot"], []).append(summarize_fx7p_block(block, context))
    chosen = {slot: choose_fx7p_attempt(attempts, gpu_judged, context) for slot, attempts in slots.items()}
    a_slots = [slot for slot, attempts in slots.items() if attempts[0]["side"] == "A"]
    b_slots = [slot for slot, attempts in slots.items() if attempts[0]["side"] == "B"]
    pairs = []
    valid_pairs = []
    for index, (a_slot, b_slot) in enumerate(zip(a_slots, b_slots)):
        a = chosen[a_slot]
        b = chosen[b_slot]
        discarded = a is None or b is None
        pairs.append({"order": index + 1, "a": a["block_id"] if a else a_slot, "b": b["block_id"] if b else b_slot,
                      "clock_mhz": [a["clock_mhz"] if a else None, b["clock_mhz"] if b else None], "discarded": discarded,
                      "spikes": [a["spikes"] if a else None, b["spikes"] if b else None]})
        if not discarded:
            valid_pairs.append((a, b))
    a_blocks = [a for a, _ in valid_pairs]
    b_blocks = [b for _, b in valid_pairs]
    metrics = FX7P_METRIC_COLUMNS + (TICK_METRIC,)
    diff = {metric: compute_fx7p_diff(valid_pairs, metric) for metric in metrics}
    diff = {metric: entry for metric, entry in diff.items() if entry is not None}
    a_values = {metric: collect_fx7p_side_values(a_blocks, metric) for metric in metrics}
    a_values = {metric: entry for metric, entry in a_values.items() if entry is not None}
    b_values = {metric: collect_fx7p_side_values(b_blocks, metric) for metric in metrics}
    b_values = {metric: entry for metric, entry in b_values.items() if entry is not None}
    stable, variation = judge_fx7p_stability(diff.get(plan_scene.get("metric", FRAME_COLUMN)), config["stability"])
    required = plan_scene.get("required_columns", [FRAME_COLUMN])
    missing_columns = [column for column in required if not a_blocks or any(column not in block["stats"] for block in a_blocks)]
    discarded_pairs = sum(1 for pair in pairs if pair["discarded"])
    occupancy_px, ms_per_mpx, occupancy_note = compute_fx7p_occupancy(plan_scene, record, diff)
    transmittance_check = check_fx7p_transmittance(plan_scene, a_blocks, b_blocks)
    return {"id": record["id"], "kind": "csv", "meta": plan_scene.get("meta", {}), "metric": plan_scene.get("metric", FRAME_COLUMN), "rounds": record.get("rounds"),
            "error": record.get("error"), "pairs": pairs, "discarded_pairs": discarded_pairs,
            "clock_null": gpu_judged and bool(pairs) and discarded_pairs * 3 > len(pairs), "diff": diff, "a": a_values, "b": b_values,
            "variation": variation, "stable": stable, "missing_columns": missing_columns, "occupancy_px": occupancy_px, "ms_per_mpx": ms_per_mpx,
            "occupancy_note": occupancy_note, "occupancy": record.get("occupancy"), "backend_check": check_fx7p_backend(plan_scene, a_blocks, b_blocks, a_values),
            "assumed_zero": sorted({column for block in b_blocks for column in block["assumed_zero"]}), "shader_gate": record.get("shader_gate"),
            "cap_gate": record.get("cap_gate"), "transmittance_check": transmittance_check, "fresh_pie": record.get("fresh_pie")}


def export_fx7p_trace_events(utrace_path):
    events_path = utrace_path.with_name(utrace_path.stem + "-events.csv")
    if events_path.exists():
        return events_path, None
    if not utrace_path.exists():
        return None, "utrace 없음"
    import profile_fluid_bench
    returncode = profile_fluid_bench.export_timing_events(str(utrace_path), str(events_path), str(utrace_path.with_name(utrace_path.stem + "-insights.log")))
    if not events_path.exists():
        return None, f"UnrealInsights 내보내기 실패(종료 코드 {returncode})"
    return events_path, None


def measure_fx7p_trace_frames(events):
    steps = sorted((start, end) for name, start, end in events if name == FX7P_STEP_TIMER)
    solvers = sorted((start, end) for name, start, end in events if name == FX7P_SOLVER_TIMER)
    solver_starts = [start for start, _ in solvers]
    frames = []
    for start, end in steps:
        first = bisect.bisect_left(solver_starts, start)
        inner = 0.0
        for solver_start, solver_end in solvers[first:]:
            if solver_start > end:
                break
            if solver_end <= end:
                inner += solver_end - solver_start
        frames.append({"step_ms": (end - start) * 1000.0, "outside_ms": (end - start - inner) * 1000.0})
    return frames


def analyze_fx7p_trace(context, trace):
    utrace_path = context["run_dir"] / trace["path"]
    events_path, error = export_fx7p_trace_events(utrace_path)
    if error is not None:
        return {"label": trace["label"], "error": error}
    import profile_fluid_bench
    frames = measure_fx7p_trace_frames(profile_fluid_bench.read_timing_events(str(events_path)))
    if not frames:
        return {"label": trace["label"], "error": f"{FX7P_STEP_TIMER} 범위가 없다"}
    step_values = [frame["step_ms"] for frame in frames]
    outside_values = [frame["outside_ms"] for frame in frames]
    step_mean = sum(step_values) / len(step_values)
    tick = fx7p_tick_in_window(context.get("perf_lines", []), context.get("trace_windows", {}).get(trace["label"]))
    distortion = None
    if tick is not None and tick["avg_tick_ms"] > 0:
        distortion = abs(step_mean - tick["avg_tick_ms"]) / tick["avg_tick_ms"] > 0.2
    return {"label": trace["label"], "frames": len(frames), "step_ms_mean": round(step_mean, 4), "step_ms_p50": fx7p_round(fx7p_median(step_values)),
            "outside_ms_p50": fx7p_round(fx7p_median(outside_values)), "outside_ms_p95": fx7p_round(fx7p_percentile(outside_values, 0.95)),
            "tick": tick, "trace_distortion": distortion, "events": events_path.relative_to(context["run_dir"]).as_posix()}


def analyze_fx7p_trace_scene(plan_scene, record, context):
    traces = [analyze_fx7p_trace(context, trace) for trace in record.get("traces", [])]
    measured = [trace for trace in traces if "error" not in trace]
    return {"id": record["id"], "kind": "trace", "meta": plan_scene.get("meta", {}), "error": record.get("error"), "traces": traces,
            "outside_ms_p50": fx7p_round(fx7p_median([trace["outside_ms_p50"] for trace in measured])),
            "step_ms_mean": fx7p_round(fx7p_median([trace["step_ms_mean"] for trace in measured])),
            "trace_distortion": any(trace["trace_distortion"] for trace in measured) if measured else None}


def expand_fx7p_globs(patterns):
    paths = []
    for pattern in patterns:
        paths.extend(sorted(glob.glob(str(ROOT / pattern), recursive=True)))
    return paths


def analyze_fx7p_headless(config):
    budgets = []
    for path in expand_fx7p_globs(config["offscreen_logs"]):
        match = FX7P_BUDGET_PATTERN.search(Path(path).read_text(encoding="utf-8", errors="replace"))
        if match is not None:
            budgets.append({"log": Path(path).relative_to(ROOT).as_posix(), "x1_median_ms": float(match.group(1)), "x1_p95_ms": float(match.group(2)),
                            "x4_median_ms": float(match.group(3))})
    memory_results = []
    for path in expand_fx7p_globs(config["headless_logs"]):
        for result, name in FX7P_TEST_RESULT_PATTERN.findall(Path(path).read_text(encoding="utf-8", errors="replace")):
            if "MemoryEstimateWithinBudget" in name:
                memory_results.append({"log": Path(path).relative_to(ROOT).as_posix(), "result": result, "name": name})
    return {"pressure_budgets": budgets, "memory_estimate": memory_results, "bench_outputs": expand_fx7p_globs(config["bench_outputs"])}


def make_fx7p_verdict(verdict_id, value, limit, passed, reason, blocks):
    return {"id": verdict_id, "value": value, "limit": limit, "pass": passed, "reason": reason, "blocks": blocks}


def fx7p_scene_problem(scene, metric=None):
    if scene is None:
        return "장면 없음"
    if scene.get("error"):
        return "장면 오류: " + str(scene["error"])[:120]
    if scene["kind"] != "csv":
        return None
    if scene["missing_columns"]:
        return "A 블록 필수 열 없음: " + ", ".join(scene["missing_columns"])
    if not scene["stable"]:
        return f"불안정({scene['variation']})"
    if scene["clock_null"]:
        return "클럭 폐기 쌍이 1/3 초과"
    if metric is not None and metric not in scene["diff"]:
        return f"{metric} 차분 없음"
    return None


def fx7p_scene_blocks(*scenes):
    return [scene["id"] for scene in scenes if scene is not None]


def fx7p_noise_width(first, second, metric, quantile):
    ranges = [scene["diff"][metric]["range_" + quantile] for scene in (first, second)]
    return None if None in ranges else (ranges[0] + ranges[1]) / 2.0


def compare_fx7p_not_worse(gpu, cpu, metric, p95_mode):
    problem = fx7p_scene_problem(gpu, metric) or fx7p_scene_problem(cpu, metric)
    if problem is not None:
        return None, None, None, problem
    noise = fx7p_noise_width(gpu, cpu, metric, "p50")
    gpu_diff = gpu["diff"][metric]
    cpu_diff = cpu["diff"][metric]
    if p95_mode == "noise":
        p95_noise = fx7p_noise_width(gpu, cpu, metric, "p95")
        p95_limit = None if p95_noise is None or cpu_diff["p95"] is None else cpu_diff["p95"] + p95_noise
    else:
        p95_limit = None if cpu_diff["p95"] is None else cpu_diff["p95"] * 1.1
    value = {"p50": gpu_diff["p50"], "p95": gpu_diff["p95"]}
    limit = {"p50": fx7p_round(cpu_diff["p50"] + noise), "p95": fx7p_round(p95_limit), "noise_width": fx7p_round(noise)}
    if gpu_diff["p95"] is None or p95_limit is None:
        return value, limit, None, "p95 차분 없음"
    return value, limit, gpu_diff["p50"] <= cpu_diff["p50"] + noise and gpu_diff["p95"] <= p95_limit, None


def judge_fx7p_limit(verdict_id, scene, metric, quantile, limit, side="diff"):
    problem = fx7p_scene_problem(scene, metric if side == "diff" else None)
    if problem is None and side != "diff" and (scene[side].get(metric) or {}).get(quantile) is None:
        problem = f"{metric} {side} 값 없음"
    if problem is not None:
        return make_fx7p_verdict(verdict_id, None, limit, None, problem, fx7p_scene_blocks(scene))
    value = scene[side][metric][quantile]
    return make_fx7p_verdict(verdict_id, value, limit, value <= limit, None, fx7p_scene_blocks(scene))


def fit_fx7p_slope(points):
    if len({count for count, _ in points}) < 2:
        return None
    mean_count = sum(count for count, _ in points) / len(points)
    mean_value = sum(value for _, value in points) / len(points)
    denominator = sum((count - mean_count) ** 2 for count, _ in points)
    return sum((count - mean_count) * (value - mean_value) for count, value in points) / denominator


def find_fx7p_scene(scenes, **meta):
    for scene in scenes.values():
        scene_meta = scene.get("meta") or {}
        if all(scene_meta.get(key) == value for key, value in meta.items()):
            return scene
    return None


def judge_fx7p_slopes(scenes):
    slope_scenes = [scene for scene in scenes.values() if (scene.get("meta") or {}).get("slope")]
    verdicts = []
    for verdict_id, metric, limit in (("J3.slope_game", "GameThreadTime", 0.04), ("J3.slope_render", RENDER_THREAD_SUM_COLUMN, 0.006)):
        problems = [f"{scene['id']}: {fx7p_scene_problem(scene, metric)}" for scene in slope_scenes if fx7p_scene_problem(scene, metric)]
        if problems or not slope_scenes:
            verdicts.append(make_fx7p_verdict(verdict_id, None, limit, None, "; ".join(problems) or "기울기 장면 없음", fx7p_scene_blocks(*slope_scenes)))
            continue
        slope = fit_fx7p_slope([(scene["meta"]["count"], scene["diff"][metric]["p50"]) for scene in slope_scenes])
        passed = None if slope is None else slope <= limit
        verdicts.append(make_fx7p_verdict(verdict_id, fx7p_round(slope, 5), limit, passed, None if slope is not None else "액터 수가 한 가지뿐", fx7p_scene_blocks(*slope_scenes)))
    return verdicts


def judge_fx7p_threshold(scenes):
    table = {}
    blocks = []
    for resolution in FX7P_THRESHOLD_RESOLUTIONS:
        tier = 1 << (resolution - 1).bit_length()
        for count in FX7P_THRESHOLD_COUNTS:
            cpu = find_fx7p_scene(scenes, backend="Cpu", resolution=resolution, count=count)
            gpu = find_fx7p_scene(scenes, backend="Gpu", resolution=tier, count=count)
            blocks.extend(fx7p_scene_blocks(cpu, gpu))
            value, limit, not_worse, problem = compare_fx7p_not_worse(gpu, cpu, FRAME_COLUMN, "ratio")
            table[f"{resolution}x{count}"] = {"cpu": cpu["id"] if cpu else None, "gpu": gpu["id"] if gpu else None, "gpu_tier": tier, "gpu_diff": value,
                                              "limit": limit, "gpu_loses": None if not_worse is None else not not_worse, "reason": problem}
    loses = {resolution: [table[f"{resolution}x{count}"]["gpu_loses"] for count in FX7P_THRESHOLD_COUNTS] for resolution in FX7P_THRESHOLD_RESOLUTIONS}

    def holds(resolution):
        if any(flag is True for flag in loses[resolution]):
            return False
        return None if any(flag is None for flag in loses[resolution]) else True

    recommendation = ">256 유지"
    reason = None
    if holds(256) is None:
        recommendation, reason = None, "256 비교 판정 불가"
    elif all(holds(resolution) is True for resolution in FX7P_THRESHOLD_RESOLUTIONS):
        recommendation = ">64"
    elif holds(192) is True and holds(256) is True:
        recommendation = ">128"
        if any(holds(resolution) is None for resolution in (96, 128)):
            reason = "96·128 비교 판정 불가: >64는 평가하지 않음"
    elif any(holds(resolution) is None for resolution in (192, 256)):
        recommendation, reason = None, "192·256 비교 판정 불가"
    verdict = make_fx7p_verdict("J6", recommendation, "GPU p50 ≤ CPU+노이즈 폭, p95 ≤ CPU×1.1", None if recommendation is None else True, reason, sorted(set(blocks)))
    return verdict, table


def judge_fx7p_svol(scenes):
    verdicts = []
    for scene in scenes.values():
        meta = scene.get("meta") or {}
        key = (meta.get("material"), meta.get("light"))
        if scene["kind"] != "csv" or key not in FX7P_SVOL_BUDGET_MS or meta.get("record_only"):
            continue
        limit = FX7P_SVOL_BUDGET_MS[key]
        problem = fx7p_scene_problem(scene, TRANSLUCENCY_COLUMN) or (scene["occupancy_note"] if scene["ms_per_mpx"] is None else None)
        value = None if problem else round(scene["ms_per_mpx"] * FX7P_SVOL_REFERENCE_MPX, 4)
        verdicts.append(make_fx7p_verdict(f"J7.{scene['id']}", value, limit, None if problem else value <= limit, problem, [scene["id"]]))
    return verdicts


def judge_fx7p_memory(headless, scenes):
    results = headless["memory_estimate"]
    s9 = scenes.get("S9")
    gpu_memory = (s9["a"].get("GPUMem/LocalUsedMB") if s9 and s9["kind"] == "csv" else None)
    if not results:
        return make_fx7p_verdict("J4", {"s9_gpu_mem_mb": gpu_memory}, "MemoryEstimateWithinBudget 통과", None, "headless_logs에 시험 결과 없음", [])
    passed = all(entry["result"] == "Success" for entry in results)
    return make_fx7p_verdict("J4", {"results": [entry["result"] for entry in results], "s9_gpu_mem_mb": gpu_memory}, "MemoryEstimateWithinBudget 통과", passed, None, ["S9"])


def judge_fx7p_pressure_budget(headless):
    budgets = headless["pressure_budgets"]
    limit = {"x1_median_ms": 0.45, "x1_p95_ms": 0.55, "x4_median_ms": 1.6}
    if not budgets:
        return make_fx7p_verdict("J5", None, limit, None, "offscreen_logs에 TDFluidGpuPressureBudget 줄 없음", [])
    value = {key: fx7p_round(fx7p_median([entry[key] for entry in budgets]), 4) for key in limit}
    value["runs"] = len(budgets)
    passed = all(value[key] <= limit[key] for key in limit)
    reason = None if len(budgets) >= 3 else f"오프스크린 실행 {len(budgets)}회(3회 필요)"
    if limit["x1_median_ms"] < value["x1_median_ms"] <= 0.50:
        value["proposed_x1_budget_ms"] = math.ceil(value["x1_median_ms"] * 1.05 / 0.05 - 1e-9) * 0.05
        reason = (reason + "; " if reason else "") + f"0.45~0.50: 예산 개정 제안 {value['proposed_x1_budget_ms']:.2f}ms(FX-16)"
    elif value["x1_median_ms"] > 0.50:
        reason = (reason + "; " if reason else "") + "0.50 초과: 회귀"
    return make_fx7p_verdict("J5", value, limit, passed, reason, [entry["log"] for entry in budgets])


def judge_fx7p_torch(scenes):
    torch = scenes.get("F5")
    if torch is None or torch["kind"] != "trace" or torch["outside_ms_p50"] is None:
        return make_fx7p_verdict("J9", None, {"outside_ms_p50": 0.6, "step_ms": 1.0}, None, "F5 추적 없음 또는 분석 실패", fx7p_scene_blocks(torch))
    value = {"outside_ms_p50": torch["outside_ms_p50"], "step_ms_mean": torch["step_ms_mean"], "trace_distortion": torch["trace_distortion"]}
    limit = {"outside_ms_p50": 0.6, "step_ms": 1.0}
    if torch["outside_ms_p50"] > 0.6:
        return make_fx7p_verdict("J9", value, limit, None, "솔버 밖 > 0.6ms: 게임 스레드 1.0ms 기준 미적용, FX-8b 항목", ["F5"])
    reason = "추적 왜곡(StepFluidWorld 평균과 TDFluidPerf가 20% 넘게 다름)" if torch["trace_distortion"] else None
    return make_fx7p_verdict("J9", value, limit, torch["step_ms_mean"] <= 1.0, reason, ["F5"])


def judge_fx7p_transmittance(scene):
    verdict = judge_fx7p_limit("J8.transmittance", scene, SOLVER_COLUMN, "p50", 0.10)
    check = (scene or {}).get("transmittance_check")
    if verdict["pass"] is None or (check and check["active"]):
        return verdict
    verdict["pass"] = None
    verdict["reason"] = "투과율 활성 확인 실패: " + (check["reason"] if check else "장면에 transmittance_ab 표시 없음")
    return verdict


def build_fx7p_verdicts(scenes, headless):
    verdicts = []
    f1, f2 = scenes.get("F1"), scenes.get("F2")
    value, limit, passed, problem = compare_fx7p_not_worse(f2, f1, FRAME_COLUMN, "noise")
    verdicts.append(make_fx7p_verdict("J1", value, limit, passed, problem, fx7p_scene_blocks(f1, f2)))
    verdicts.append(judge_fx7p_limit("J2", scenes.get("S6"), FRAME_COLUMN, "p50", 1.7))
    s9 = scenes.get("S9")
    verdicts.append(judge_fx7p_limit("J3.solver", s9, SOLVER_COLUMN, "p50", 4.0, side="a"))
    verdicts.append(judge_fx7p_limit("J3.game", s9, "GameThreadTime", "p50", 2.0))
    verdicts.append(judge_fx7p_limit("J3.render", s9, RENDER_THREAD_SUM_COLUMN, "p50", 0.3))
    verdicts.extend(judge_fx7p_slopes(scenes))
    verdicts.append(judge_fx7p_memory(headless, scenes))
    verdicts.append(judge_fx7p_pressure_budget(headless))
    threshold_verdict, threshold_table = judge_fx7p_threshold(scenes)
    verdicts.append(threshold_verdict)
    verdicts.extend(judge_fx7p_svol(scenes))
    verdicts.append(judge_fx7p_limit("J7.F3", scenes.get("F3"), TRANSLUCENCY_COLUMN, "p50", 0.3))
    verdicts.append(make_fx7p_verdict("J7.translucency_plus10", None, "FX-8a 전 기준선 대비 +10%", None, "FX-8a 전 기준선 없음(Q1)", []))
    verdicts.append(judge_fx7p_transmittance(scenes.get("S12")))
    verdicts.append(judge_fx7p_limit("J8.campfire_tick", f1, TICK_METRIC, "p50", 2.0, side="a"))
    f6 = scenes.get("F6")
    if f6 is None or f6["kind"] != "trace" or f6["step_ms_mean"] is None:
        verdicts.append(make_fx7p_verdict("J8.explosion", None, 2.5, None, "F6 추적 없음 또는 분석 실패", fx7p_scene_blocks(f6)))
    else:
        verdicts.append(make_fx7p_verdict("J8.explosion", f6["step_ms_mean"], 2.5, f6["step_ms_mean"] <= 2.5, None, ["F6"]))
    verdicts.append(judge_fx7p_torch(scenes))
    return verdicts, threshold_table


def compute_fx7p_clock_median(session, plan_scenes, clock_samples):
    values = []
    for record in session["scenes"]:
        if not plan_scenes.get(record["id"], {}).get("gpu_clock"):
            continue
        for block in record.get("blocks", []):
            if block.get("kind") == "block":
                mhz = average_fx7p_clock(clock_samples, block["t_start"], block["t_end"])
                if mhz is not None:
                    values.append(mhz)
    return fx7p_median(values)


def compute_fx7p_exit_code(scenes):
    for scene in scenes.values():
        if scene.get("error"):
            return EXIT_ENVIRONMENT
        if scene["kind"] == "csv" and (scene["missing_columns"] or not scene["stable"] or scene["clock_null"]):
            return EXIT_MISSING_LINES
        if scene["kind"] == "trace" and any("error" in trace for trace in scene["traces"]):
            return EXIT_MISSING_LINES
    return 0


def write_fx7p_evidence(result):
    stamp = date.today().isoformat()
    written = []
    fx4a_ids = ("J1", "J2", "J3.solver", "J3.game", "J3.render", "J3.slope_game", "J3.slope_render", "J5")
    fx4a = {"tool": result["tool"], "run": result["run"], "date": result["date"], "verdicts": [verdict for verdict in result["verdicts"] if verdict["id"] in fx4a_ids],
            "scenes": {scene_id: result["scenes"][scene_id] for scene_id in ("F1", "F2", "S6", "S9") if scene_id in result["scenes"]}}
    for name, data in ((f"FX-7p-baseline-{stamp}.json", result), (f"FX-4a-gpu-{stamp}.json", fx4a)):
        path = FX7P_EVIDENCE_DIRECTORY / name
        if path.exists():
            path = path.with_name(f"{path.stem}-{datetime.now().strftime('%H%M%S')}.json")
        write_fx7p_json(path, data)
        written.append(path.relative_to(ROOT).as_posix())
    return written


def print_fx7p_summary(result):
    print(f"{'verdict':<26}{'pass':>6}  value / limit / reason")
    for verdict in result["verdicts"]:
        state = {True: "ok", False: "FAIL", None: "null"}[verdict["pass"]]
        print(f"{verdict['id']:<26}{state:>6}  {json.dumps(verdict['value'], ensure_ascii=False)} / {json.dumps(verdict['limit'], ensure_ascii=False)} / {verdict['reason'] or ''}")
    print("J6 해상도x액터수 GPU 짐:", ", ".join(f"{key}={entry['gpu_loses']}" for key, entry in result["threshold_table"].items()))


def analyze_fx7p_run(run_dir, write_evidence):
    session_path = run_dir / FX7P_SESSION_NAME
    plan_path = run_dir / FX7P_PLAN_COPY_NAME
    if not session_path.exists() or not plan_path.exists():
        print("환경 문제: 실행 폴더에 session.json·plan.json이 없다:", run_dir)
        return EXIT_ENVIRONMENT
    session = json.loads(session_path.read_text(encoding="utf-8"))
    config = merge_fx7p_plan(json.loads(plan_path.read_text(encoding="utf-8")))
    plan_scenes = {scene["id"]: scene for scene in config["scenes"]}
    entries = read_fx7p_log(run_dir / FX7P_LOG_COPY_NAME)
    clock_samples = read_fx7p_clock_samples(run_dir / FX7P_CLOCK_NAME)
    context = {"config": config, "run_dir": run_dir, "perf_lines": collect_fx7p_perf_lines(entries), "block_windows": find_fx7p_block_windows(entries),
               "trace_windows": find_fx7p_trace_windows(entries), "clock_samples": clock_samples,
               "clock_median": compute_fx7p_clock_median(session, plan_scenes, clock_samples)}
    scenes = {}
    for record in session["scenes"]:
        plan_scene = plan_scenes.get(record["id"], {})
        if record["kind"] == "trace":
            scenes[record["id"]] = analyze_fx7p_trace_scene(plan_scene, record, context)
        else:
            scenes[record["id"]] = analyze_fx7p_csv_scene(plan_scene, record, context)
    headless = analyze_fx7p_headless(config)
    verdicts, threshold_table = build_fx7p_verdicts(scenes, headless)
    exit_code = EXIT_ENVIRONMENT if session.get("environment_error") else compute_fx7p_exit_code(scenes) if scenes else EXIT_ENVIRONMENT
    result = {"tool": "pie_profile_fluid --analyze", "run": run_dir.name, "date": date.today().isoformat(), "analyzed": fx7p_now(),
              "conditions": {"hold": session.get("hold"), "build_gate": session.get("build_gate"), "asset_hashes": session.get("asset_hashes"),
                             "pie_sessions": session.get("pie_sessions"), "restored_variables": session.get("restored_variables"),
                             "clock_median_mhz": fx7p_round(context["clock_median"], 1), "clock_samples": len(clock_samples)},
              "environment_error": session.get("environment_error"), "warnings": session.get("warnings", []), "headless": headless,
              "verdicts": verdicts, "threshold_table": threshold_table, "scenes": scenes, "exit_code": exit_code, "evidence": []}
    if write_evidence:
        result["evidence"] = write_fx7p_evidence(result)
    write_fx7p_json(run_dir / FX7P_RESULT_NAME, result)
    print_fx7p_summary(result)
    print("result", (run_dir / FX7P_RESULT_NAME).as_posix(), "evidence", result["evidence"], "exit_code", exit_code)
    return exit_code


def parse_arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument("--phase-seconds", type=float, default=PHASE_SECONDS)
    parser.add_argument("--csv-gpu", metavar="PLAN.json", help="FX-7p 계획 JSON의 장면을 csvprofile ABBA 블록으로 잰다(--holder 필수)")
    parser.add_argument("--analyze", metavar="RUN_DIR", help="--csv-gpu 실행 폴더를 판정하고 증거 JSON을 쓴다(에디터 불필요)")
    parser.add_argument("--holder", help="Saved/AgentOps/build_hold.json owner와 같아야 하는 보류 이름")
    arguments = parser.parse_args()
    if arguments.phase_seconds <= 0:
        parser.error("--phase-seconds는 0보다 커야 한다")
    if arguments.csv_gpu and arguments.analyze:
        parser.error("--csv-gpu와 --analyze는 함께 쓰지 않는다")
    if arguments.csv_gpu and not arguments.holder:
        parser.error("--csv-gpu에는 --holder가 필요하다")
    return arguments


def main():
    arguments = parse_arguments()
    if arguments.analyze:
        return analyze_fx7p_run(Path(arguments.analyze).resolve(), True)
    if arguments.csv_gpu:
        return run_fx7p_plan(Path(arguments.csv_gpu).resolve(), arguments.holder)
    report = {"tool": "pie_profile_fluid", "run_token": RUN_TOKEN, "date": date.today().isoformat(), "started": time.strftime("%Y-%m-%d %H:%M:%S"),
              "level": pie_check_fluid.LEVEL_PATH, "phase_seconds": arguments.phase_seconds, "warnings": [FOREGROUND_WARNING], "environment": {},
              "setup_actors": [], "phases": {}, "last_started_phase": None, "finish_logged": False, "console_variable_state": "unchanged",
              "console_variable_readback": None, "environment_error": None, "niagara_check": None, "passed": False}
    print("경고:", FOREGROUND_WARNING, flush=True)
    log_offset = read_log_size()
    try:
        report["environment_error"] = run_session(arguments, report)
    except Exception as error:
        report["environment_error"] = f"예외: {type(error).__name__}: {error}"[:500]
    summaries = collect_phase_summaries(report, log_offset) if report["phases"] else {}
    report["phases"] = {name: {**summaries[name], "editor_state": report["phases"].get(name, {}).get("actors")} for name in summaries}
    if summaries:
        add_slow_frame_warning(report, summaries)
        print_table(summaries)
    exit_code = 0
    if report["environment_error"] is not None:
        exit_code = EXIT_ENVIRONMENT
    elif not summaries or any(summary["lines"] < 1 for summary in summaries.values()):
        exit_code = EXIT_MISSING_LINES
    elif any(summary["state_consistent"] is False or summary["matches_editor_state"] is False for summary in summaries.values()):
        exit_code = EXIT_MISSING_LINES
        report["warnings"].append("구간 안에서 깨어 있는 액터 수·셀 수가 바뀌었거나 에디터 상태와 다르다(구간 오염)")
    elif not (report.get("niagara_check") or {}).get("passed"):
        exit_code = EXIT_MISSING_LINES
    report["passed"] = exit_code == 0
    report["exit_code"] = exit_code
    write_report(report, exit_code)
    for warning in report["warnings"]:
        print("경고:", warning)
    if report["environment_error"] is not None:
        print("환경 문제:", report["environment_error"])
    print("passed", report["passed"], "exit_code", exit_code, "report", OUTPUT_PATH.relative_to(ROOT).as_posix())
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
