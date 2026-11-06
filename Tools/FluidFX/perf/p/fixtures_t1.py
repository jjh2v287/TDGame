"""실행 환경: 시스템 Python 3.12(표준 라이브러리만), 에디터·빌드·unreal 모듈 불필요. Tools/FluidFX/pie_profile_fluid.py를 그대로 불러 가짜 unreal 모듈·가짜 원격 실행·가짜 PIE로 에디터 쪽 코드(EDITOR_CODE)와 --csv-gpu 흐름을 돌리고, --check-plan은 하위 프로세스로 부른다(V3 5절 T1 ①~⑧·A17).
실행 명령: python -B Tools/FluidFX/perf/p/fixtures_t1.py [--keep] (작업 루트 C:/Project/TDGame, 셸 무관)
출력: 표준 출력에 사례마다 ok/FAIL 한 줄과 끝 줄 'fixtures_t1: 통과 n/m', 종료 0 전부 통과/1 하나라도 실패. 계획 사본·가짜 CSV·가짜 실행 폴더·PP 출력 로그·T3 fixture 저장소 사본(p_plans.py build 결과)은 Saved/AgentOps/20261106/p/tools/t1/tmp/<시각>/ 아래에 만들고 --keep이 없으면 끝에 지운다
상태: 2026-11-06 작성(P T1 차선), 반박 r1 반영(첫 장면 오류 뒤 상한 관문 유지·P 생성 계획 = T3 fixture 사본 build 결과 전부에 진짜 --check-plan), 반박 r2 M1 반영(camera target_label 뷰 대상 CameraComponent 화면 비율 제한 끄기·aspect_constraints 기록). 에디터 실제 동작(숨김 되읽기 속성 이름 hidden, Deactivate 완료 시점, 라벨 조회, CineCamera 제한 해제 뒤 필러박스 없음)은 가짜로만 확인했고 P fire 시운전이 확인한다
"""
import argparse
import ast
import contextlib
import importlib.util
import io
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
import traceback
import types
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
PP_PATH = ROOT / "Tools" / "FluidFX" / "pie_profile_fluid.py"
TMP_ROOT = ROOT / "Saved" / "AgentOps" / "20261106" / "p" / "tools" / "t1" / "tmp"
FX4B_JUDGE_PLAN = ROOT / "Saved" / "FluidFX" / "fx7p" / "fx4b-budget-judge-20261024120142" / "plan.json"
FX7P_OLD_PLAN = ROOT / "Tools" / "FluidFX" / "fx7p_plan.json"
P_GENERATED_PLAN_PATTERNS = ("Tools/FluidFX/perf/p/*/pilot1.json", "Tools/FluidFX/perf/p/*/ladder.json", "Saved/AgentOps/20261106/p/L0*/*/judge-plan.json")
T3_FIXTURE_PATH = ROOT / "Tools" / "FluidFX" / "perf" / "p" / "check_p_fixtures.py"
T3_TOOL_PATH = ROOT / "Tools" / "FluidFX" / "perf" / "p_plans.py"
T3_PLAN_NAMES = ("pilot1.json", "ladder.json")
CAP_TRIP_GATE = {"frame_ms": 12.45, "tolerance": 0.03, "cluster_ms": 0.05, "cluster_share": 0.5}
HOLDER = "p-perf"
FIRE_LEVEL = "/Game/FluidFX/Maps/LV_TDFluidFireStage"
EMBER_LEVEL = "/Game/FluidFX/Maps/LV_TDFluidEmberStage"
HVOL_LEVEL = "/Game/FluidFX/Maps/LV_TDFluidHvolStage"
CLOUD_LEVEL = "/Game/FluidFX/Maps/LV_TDFluidCloudStage"
COAST_LEVEL = "/Game/FluidFX/Maps/LV_TDFluidCoastStage"
FLUID_LABELS = ("TDGen_CampfireFire", "TDGen_CampfireSmoke", "TDGen_EmberB_Campfire", "TDGen_HvolVortexA", "TDGen_HvolVortexB", "TDGen_CoastStageWaterSea", "TDGen_OtherFluid")
P_EXTRA_CVARS = ("t.OverrideFPS 0", "TD.FluidFX.WaterWetness 1", "TD.FluidFX.MassInput -1", "TD.FluidFX.CloudSetup -1", "TD.FluidFX.CloudViewSampleScale -1", "r.VolumetricFog 1")
P_EXTRA_RECORDED = ("TD.FluidFX.WaterWetness", "TD.FluidFX.MassInput", "TD.FluidFX.CloudSetup", "TD.FluidFX.CloudViewSampleScale", "t.OverrideFPS", "r.TDFogScattering",
                    "TD.FluidFX.WaterGpu", "TD.FluidFX.MaxStepRateHz", "TD.FluidFX.PauseWhenNotVisible", "TD.FluidFX.ParallelActorSteps", "TD.FluidFX.LogTickCost")
OTHERS_SLEEP = {"action": "sleep", "targets": "others", "awake": False}
SPIKE_VARIABLE = "TD.FakeFixture.Spikes"

RESULTS = []


def check(name, passed, detail=""):
    RESULTS.append(bool(passed))
    suffix = "" if passed else " | " + str(detail)[:700]
    print(("ok   " if passed else "FAIL ") + name + suffix, flush=True)


class FakeVector:
    def __init__(self, x=0.0, y=0.0, z=0.0):
        self.x, self.y, self.z = x, y, z


class FakeClassName:
    def __init__(self, name):
        self.name = name

    def get_name(self):
        return self.name


class FakeObject:
    def __init__(self, name):
        self.name = name
        self.properties = {}

    def get_name(self):
        return self.name

    def get_path_name(self):
        return "/Game/Fake/" + self.name

    def get_editor_property(self, key):
        return self.properties[key]

    def set_editor_property(self, key, value):
        self.properties[key] = value

    def get_class(self):
        return FakeClassName(type(self).__name__)


class FakeComponent(FakeObject):
    def __init__(self, name, visible=True):
        super().__init__(name)
        self.visible = visible

    def is_visible(self):
        return self.visible

    def set_visibility(self, visible):
        self.visible = bool(visible)


class FakeLightComponent(FakeComponent):
    pass


class FakePointLightComponent(FakeLightComponent):
    pass


class FakeDirectionalLightComponent(FakeLightComponent):
    pass


class FakeCameraComponent(FakeComponent):
    def __init__(self, name, constrained=False, aspect_ratio=1.7777778, field_of_view=90.0):
        super().__init__(name)
        self.properties.update({"constrain_aspect_ratio": constrained, "aspect_ratio": aspect_ratio, "field_of_view": field_of_view})

    def set_constraint_aspect_ratio(self, constrained):
        self.properties["constrain_aspect_ratio"] = bool(constrained)


class FakeCineCameraComponent(FakeCameraComponent):
    def __init__(self, name):
        super().__init__(name, constrained=True, aspect_ratio=1.7777778, field_of_view=61.9)


class FakeStuckCameraComponent(FakeCameraComponent):
    def set_constraint_aspect_ratio(self, constrained):
        pass


class FakeNiagaraComponent(FakeComponent):
    def __init__(self, name):
        super().__init__(name)
        self.active = True
        self.calls = []

    def is_active(self):
        return self.active

    def activate(self, reset=False):
        self.active = True
        self.calls.append("activate")

    def deactivate(self):
        self.active = False
        self.calls.append("deactivate")


class FakeImmediateNiagaraComponent(FakeNiagaraComponent):
    def deactivate_immediate(self):
        self.active = False
        self.calls.append("deactivate_immediate")


class FakeTexture(FakeObject):
    pass


class FakeConfig(FakeObject):
    def copy(self):
        clone = FakeConfig(self.name)
        clone.properties = dict(self.properties)
        return clone


class FakeLiveComponent(FakeObject):
    def __init__(self, name, owner):
        super().__init__(name)
        self.owner = owner
        self.awake = True
        config = FakeConfig(name + "_config")
        config.properties.update({"grid_resolution": 128, "solver_backend": "TDFluidSolverBackend.CPU", "enable_heat_channel": False})
        self.properties.update({"simulation_config": config, "active_preset": None})

    def is_simulation_awake(self):
        return self.awake

    def set_simulation_awake(self, awake):
        self.awake = bool(awake)
        for component in self.owner.get_components_by_class(FakeNiagaraComponent):
            if self.awake:
                component.activate(True)
            else:
                component.active = False

    def get_density_pressure_texture(self):
        return FakeTexture("density")

    def get_transmittance_texture(self):
        return None

    def apply_simulation_config(self, config):
        self.properties["simulation_config"] = config


class FakeActor(FakeObject):
    def __init__(self, label, components=()):
        super().__init__(label + "_C")
        self.label = label
        self.components = list(components)
        self.tags = []
        self.location = FakeVector(1.0, 2.0, 3.0)
        self.properties["hidden"] = False

    def get_actor_label(self):
        return self.label

    def set_actor_hidden_in_game(self, hidden):
        self.properties["hidden"] = bool(hidden)

    def get_components_by_class(self, component_class):
        return [component for component in self.components if isinstance(component, component_class)]

    def get_actor_location(self):
        return self.location

    def destroy_actor(self):
        FAKE.world.actors.remove(self)


class FakeFluidActor(FakeActor):
    def __init__(self, label, has_embers=True, light_actor=None, niagara_class=None):
        cards = FakeComponent(label + "_cards")
        volumes = FakeComponent(label + "_volumes")
        self.ember_component = FakeComponent(label + "_embers") if has_embers else None
        components = [cards, volumes] + ([self.ember_component] if self.ember_component else []) + ([niagara_class(label + "_ns")] if niagara_class else [])
        super().__init__(label, components)
        self.properties.update({"live_component": FakeLiveComponent(label + "_live", self), "internal_surface_mesh": cards, "volume_mesh_component": volumes,
                                "ember_component": self.ember_component, "volume_light_actor": light_actor, "should_use_activation_volume": True,
                                "should_follow_player_pawn": False})


class FakeController(FakeObject):
    def __init__(self):
        super().__init__("PlayerController")
        self.properties["player_camera_manager"] = FakeActor("PlayerCameraManager")

    def set_view_target_with_blend(self, actor, blend_time=0.0):
        FAKE.world.view_target = actor


class FakeWorld:
    def __init__(self):
        self.actors = []
        self.pawn = FakeActor("DefaultPawn")
        self.controller = FakeController()
        self.view_target = None
        self.actors.append(self.pawn)

    def find(self, label):
        return next(actor for actor in self.actors if actor.get_actor_label() == label)


class FakeEditorSubsystem:
    def get_game_world(self):
        return FAKE.world

    def get_editor_world(self):
        return FAKE.world


class FakeGameplayStatics:
    @staticmethod
    def get_all_actors_of_class(world, actor_class):
        return [actor for actor in world.actors if isinstance(actor, actor_class)]

    @staticmethod
    def get_all_actors_with_tag(world, tag):
        return [actor for actor in world.actors if tag in [str(item) for item in actor.tags]]

    @staticmethod
    def get_player_pawn(world, index):
        return world.pawn

    @staticmethod
    def get_player_controller(world, index):
        return world.controller

    @staticmethod
    def get_time_seconds(world):
        return 0.0


class FakeSystemLibrary:
    @staticmethod
    def execute_console_command(world, command):
        FAKE.run_console(command)

    @staticmethod
    def get_console_variable_float_value(name):
        return float(FAKE.variables.get(name, 0.0))

    @staticmethod
    def get_console_variable_int_value(name):
        return int(FAKE.variables.get(name, 0.0))


class FakeWidgetLayoutLibrary:
    @staticmethod
    def get_viewport_size(world):
        return FakeVector(1920.0, 1080.0)


class FakeEditor:
    def __init__(self):
        self.world = FakeWorld()
        self.variables = {}
        self.csv_directory = None
        self.csv_count = 0
        self.logs = []
        self.actions = []
        self.pie_starts = 0
        self.fail_action = None
        self.fail_after = 0
        self.hold_errors = {}
        self.hold_calls = 0
        self.csv_writes_left = None
        self.pie_start_failures = set()

    def run_console(self, command):
        parts = command.split()
        if not parts:
            return
        if parts[0] == "csvprofile":
            if self.csv_writes_left is not None:
                self.csv_writes_left -= 1
                if self.csv_writes_left < 0:
                    return
            self.write_csv(int(parts[1].split("=")[1]))
            return
        if len(parts) != 2:
            return
        try:
            self.variables[parts[0]] = float(parts[1])
        except ValueError:
            return

    def write_csv(self, frames):
        self.csv_count += 1
        awake = sum(1 for actor in self.world.actors if isinstance(actor, FakeFluidActor) and actor.properties["live_component"].awake)
        lines = ["FrameTime,GameThreadTime,GPUTime,TDFluid/StepCount,Exclusive/RenderThread/FakePass"]
        spiky = self.variables.get(SPIKE_VARIABLE, 0.0) == 1.0
        for index in range(frames):
            step = index % 50
            frame_ms = 100.0 if spiky and index % 10 == 0 else 10.0 + step * 0.1
            lines.append(f"{frame_ms:.3f},{4.0 + step * 0.05:.3f},{8.0 + step * 0.1:.3f},{awake},0.5")
        lines.append("[Platform],Windows")
        path = self.csv_directory / f"Profile_{time.time_ns()}_{self.csv_count:05d}.csv"
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        stamp = time.time() + self.csv_count
        os.utime(path, (stamp, stamp))

    def start_pie(self):
        self.pie_starts += 1
        if self.pie_starts in self.pie_start_failures:
            return False
        for actor in self.world.actors:
            if isinstance(actor, FakeFluidActor):
                actor.properties["live_component"].awake = True
        return True

    def take_hold_error(self):
        self.hold_calls += 1
        return self.hold_errors.get(self.hold_calls)


FAKE = FakeEditor()


def build_fake_unreal_module():
    module = types.ModuleType("unreal")
    module.Actor = FakeActor
    module.TDFluidLiveActor = FakeFluidActor
    module.LightComponent = FakeLightComponent
    module.PointLightComponent = FakePointLightComponent
    module.DirectionalLightComponent = FakeDirectionalLightComponent
    module.NiagaraComponent = FakeNiagaraComponent
    module.CameraComponent = FakeCameraComponent
    module.Vector = FakeVector
    module.UnrealEditorSubsystem = FakeEditorSubsystem
    module.GameplayStatics = FakeGameplayStatics
    module.SystemLibrary = FakeSystemLibrary
    module.WidgetLayoutLibrary = FakeWidgetLayoutLibrary
    module.get_editor_subsystem = lambda subsystem_class: FakeEditorSubsystem()
    module.log = lambda text: FAKE.logs.append(text)
    return module


def build_fake_world(include_other_fluid=True, immediate_niagara=False):
    world = FakeWorld()
    light_actor = FakeActor("TDGen_CampfireFire_ShadowLight", [FakePointLightComponent("shadow_point")])
    niagara_class = FakeImmediateNiagaraComponent if immediate_niagara else FakeNiagaraComponent
    world.actors.extend([
        FakeFluidActor("TDGen_CampfireFire"),
        FakeFluidActor("TDGen_CampfireSmoke", has_embers=False, light_actor=light_actor),
        light_actor,
        FakeFluidActor("TDGen_EmberB_Campfire", niagara_class=niagara_class),
        FakeFluidActor("TDGen_HvolVortexA", has_embers=False),
        FakeFluidActor("TDGen_HvolVortexB", has_embers=False),
        FakeFluidActor("TDGen_CoastStageWaterSea", has_embers=False),
        FakeActor("TDGen_CloudLayers"),
        FakeActor("TDGen_CloudCam_Volcano", [FakeCineCameraComponent("CameraComponent")]),
    ])
    if include_other_fluid:
        world.actors.append(FakeFluidActor("TDGen_OtherFluid", has_embers=False))
    return world


def read_editor_params(code):
    match = re.search(r"^PARAMS = json\.loads\((.+)\)$", code, re.M)
    return json.loads(ast.literal_eval(match.group(1)))


def run_fake_editor_code(code):
    params = read_editor_params(code)
    FAKE.actions.append(params["action"])
    if params["action"] == FAKE.fail_action:
        FAKE.fail_after -= 1
        if FAKE.fail_after < 0:
            return {"connected": False, "lines": ["가짜 원격 실행 끊김"]}
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        exec(compile(code, "<fake-editor>", "exec"), {"__name__": "__fake_editor__"})
    return {"connected": True, "lines": buffer.getvalue().splitlines()}


def make_clock_sampler(output_path, interval_ms):
    return {"process": None, "samples": [], "lock": threading.Lock(), "output_path": output_path, "error": "가짜 클럭 없음", "thread": None}


def install_fakes(pp, work):
    sys.modules["unreal"] = build_fake_unreal_module()
    fake_showcase = types.ModuleType("pie_showcase_fluid")
    fake_showcase.open_stage_level = lambda level: {"loaded": True, "level": level}
    sys.modules["pie_showcase_fluid"] = fake_showcase
    pp.run_in_editor.run_code = run_fake_editor_code
    pp.pie_check_fluid.start_pie = lambda session, environment: FAKE.start_pie()
    pp.pie_check_fluid.stop_pie = lambda session, environment: True
    pp.pie_check_fluid.wait_for_pie_state = lambda should_be_running, timeout_seconds: True
    pp.uemcp.connect = lambda: object()
    pp.check_fx7p_holder = lambda holder: ({"owner": holder}, None)
    pp.check_fx7p_build_gate = lambda: {"passed": True}
    pp.hash_fx7p_assets = lambda paths: {path: "0" * 40 for path in paths}
    pp.read_log_size = lambda: 0
    pp.read_log_lines_since = lambda offset: []
    pp.start_fx7p_clock_sampler = make_clock_sampler
    pp.judge_fx7p_block_clock = lambda context, block: (True, None)
    pp.refresh_fx7p_hold = lambda context: FAKE.take_hold_error()
    pp.FX7P_OUTPUT_ROOT = work / "fx7p"
    pp.FX7P_PROFILING_CSV_DIRECTORY = work / "profiling"
    pp.time.sleep = lambda seconds: None


def reset_fake(pp, world, token):
    FAKE.__init__()
    FAKE.world = world
    FAKE.csv_directory = pp.FX7P_PROFILING_CSV_DIRECTORY
    FAKE.csv_directory.mkdir(parents=True, exist_ok=True)
    FAKE.variables.update({"t.OverrideFPS": 0.0, "r.ScreenPercentage": 100.0, "TD.FluidFX.WaterWetness": 1.0, "TD.FluidFX.CloudSetup": -1.0})
    unreal = sys.modules["unreal"]
    if hasattr(unreal, "td_fx7p_snapshot"):
        delattr(unreal, "td_fx7p_snapshot")
    pp.RUN_TOKEN = token


def screen_step(percentage):
    return {"action": "console", "commands": [f"r.ScreenPercentage {percentage}"], "restore": True}


def make_p_scene(scene_id, level, targets, setup_steps, a_steps, b_steps, toggle, backend, grid, rows, block_frames=None):
    scene = {"id": scene_id, "level": level, "targets": list(targets), "setup": [dict(OTHERS_SLEEP)] + setup_steps + [screen_step(100)], "a": a_steps, "b": b_steps,
             "meta": {"screen_percentage": 100, "count": 1, "backend": backend, "grid": grid, "toggle": toggle, "rows": rows}, "gpu_clock": backend == "GPU",
             "required_columns": ["FrameTime", "GameThreadTime", "GPUTime"], "metric": "FrameTime"}
    if block_frames is not None:
        scene["block_frames"] = block_frames
    return scene


def fire_l_sides(awake):
    return [{"action": "sleep", "awake": awake},
            {"action": "visibility", "targets": ["TDGen_CampfireFire"], "parts": ["cards", "embers"], "visible": awake},
            {"action": "visibility", "targets": ["TDGen_CampfireSmoke"], "parts": ["volumes"], "visible": awake},
            {"action": "light", "targets": ["TDGen_CampfireSmoke"], "visible": awake}]


def make_fi1_scene(level=FIRE_LEVEL):
    return make_p_scene("FI1", level, ["TDGen_CampfireFire", "TDGen_CampfireSmoke"], [], fire_l_sides(True), fire_l_sides(False), "L", "CPU",
                        {"TDGen_CampfireFire": 128, "TDGen_CampfireSmoke": 96}, ["FI1/ft", "FI1/gt", "FI1/rt"], block_frames=1200)


def make_fi5b_scene(level=EMBER_LEVEL):
    labels = ["TDGen_EmberB_Campfire"]
    return make_p_scene("FI5b", level, labels, [], [{"action": "actor", "labels": labels, "niagara": "activate"}],
                        [{"action": "actor", "labels": labels, "niagara": "deactivate"}], "Xn", "CPU", {"TDGen_EmberB_Campfire": 128}, ["FI5b/ft"])


def make_hv2_scene(labels=("TDGen_HvolVortexA", "TDGen_HvolVortexB"), level=HVOL_LEVEL, scene_id="HV2"):
    labels = list(labels)
    targets = ["TDGen_HvolVortexA", "TDGen_HvolVortexB"]
    return make_p_scene(scene_id, level, targets, [], [{"action": "sleep", "awake": True}, {"action": "actor", "labels": labels, "hidden": False}],
                        [{"action": "sleep", "awake": False}, {"action": "actor", "labels": labels, "hidden": True}], "Ls+X", "CPU",
                        {"TDGen_HvolVortexA": 128, "TDGen_HvolVortexB": 128}, ["HV2/ft"])


def make_cl1_scene(level=CLOUD_LEVEL):
    setup = [{"action": "console", "commands": ["TD.FluidFX.CloudSetup 6", "TD.FluidFX.CloudViewSampleScale 2"], "restore": True},
             {"action": "camera", "target_label": "TDGen_CloudCam_Volcano"}]
    labels = ["TDGen_CloudLayers"]
    return make_p_scene("CL1", level, [], setup, [{"action": "actor", "labels": labels, "hidden": False}], [{"action": "actor", "labels": labels, "hidden": True}],
                        "X", None, {}, ["CL1/ft", "CL1/rt"])


def make_wa3_scene(level=COAST_LEVEL, scene_id="WA3"):
    return make_p_scene(scene_id, level, ["TDGen_CoastStageWaterSea"], [], [{"action": "console", "commands": ["TD.FluidFX.WaterWetness 1"]}],
                        [{"action": "console", "commands": ["TD.FluidFX.WaterWetness 0"]}], "C", "CPU", {"TDGen_CoastStageWaterSea": 192}, ["WA3/gt"])


def make_empty_sleep_scene(level):
    return make_p_scene("CLX", level, [], [], [{"action": "sleep", "awake": True}], [{"action": "sleep", "awake": False}], "Ls", None, {}, ["CLX/ft"])


def make_empty_visibility_scene(level):
    sides = [[{"action": "visibility", "targets": ["TDGen_CampfireSmoke"], "parts": ["embers"], "visible": visible}] for visible in (True, False)]
    return make_p_scene("FIX", level, ["TDGen_CampfireSmoke"], [], sides[0], sides[1], "Lb", "CPU", {"TDGen_CampfireSmoke": 96}, ["FIX/ft"])


def make_spike_scene(level, scene_id, spikes):
    scene = make_wa3_scene(level=level, scene_id=scene_id)
    scene["setup"].insert(1, {"action": "console", "commands": [f"{SPIKE_VARIABLE} {spikes}"], "restore": True})
    return scene


def make_setup_error_scene(level, scene_id):
    scene = make_wa3_scene(level=level, scene_id=scene_id)
    scene["setup"].insert(1, {"action": "camera", "target_label": "TDGen_NoCamera"})
    return scene


def make_b_side_error_scene(level, scene_id):
    return make_p_scene(scene_id, level, ["TDGen_HvolVortexA"], [], [{"action": "actor", "labels": ["TDGen_HvolVortexA"], "hidden": False}],
                        [{"action": "actor", "labels": ["TDGen_Missing"], "hidden": True}], "X", "CPU", {"TDGen_HvolVortexA": 128}, [f"{scene_id}/ft"])


def make_p_plan(pp, scenes, run_label="p-fire-pilot1", include_marker=True):
    cvars = list(pp.FX7P_DEFAULTS["cvars"]) + list(P_EXTRA_CVARS)
    recorded = list(pp.FX7P_DEFAULTS["recorded_variables"]) + [name for name in P_EXTRA_RECORDED if name not in pp.FX7P_DEFAULTS["recorded_variables"]]
    plan = {"run_label": run_label, "cvars": cvars, "recorded_variables": recorded,
            "hash_paths": ["Content/FluidFX/Maps/LV_TDFluidFireStage.umap", "Binaries/Win64/UnrealEditor-TDGame.dll"],
            "block_frames": 300, "discard_frames": 60, "order": "ABBA", "settle_seconds": 3, "initial_warmup_seconds": 90, "fresh_pie_after_gpu": True,
            "repeats": 1, "unstable_repeats": 1, "write_fx7p_evidence": False, "scenes": scenes}
    if include_marker:
        plan["_p"] = {"generator": "Tools/FluidFX/perf/p_plans.py", "p_areas_sha256": "0" * 64, "ledger_sha256": "0" * 64}
    return plan


def make_sample_scenes():
    return [make_fi1_scene(), make_fi5b_scene(), make_hv2_scene(), make_cl1_scene(), make_wa3_scene()]


def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return path


def run_check_plan(path):
    environment = {**os.environ, "PYTHONIOENCODING": "utf-8"}
    completed = subprocess.run([sys.executable, "-B", str(PP_PATH), "--check-plan", str(path)], cwd=ROOT, capture_output=True, text=True,
                               encoding="utf-8", errors="replace", env=environment)
    return completed.returncode, completed.stdout + completed.stderr


def load_module_from_path(path, name):
    specification = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


def build_t3_fixture_plans(base):
    if not (T3_FIXTURE_PATH.is_file() and T3_TOOL_PATH.is_file()):
        return [], f"T3 fixture 또는 p_plans.py가 없다: {T3_FIXTURE_PATH.relative_to(ROOT).as_posix()}·{T3_TOOL_PATH.relative_to(ROOT).as_posix()}"
    t3_fixture = load_module_from_path(T3_FIXTURE_PATH, "t3_fixture_for_t1")
    t3_tool = load_module_from_path(T3_TOOL_PATH, "t3_tool_for_t1")
    if not (hasattr(t3_fixture, "make_base_root") and hasattr(t3_fixture, "FixtureRun") and hasattr(t3_fixture, "T_REL")):
        return [], "T3 fixture에 make_base_root·FixtureRun·T_REL이 없다(교차 양성을 만들 수 없다)"
    shutil.rmtree(base, ignore_errors=True)
    t3_fixture.make_base_root(base, ROOT, t3_tool)
    code, out, err = t3_fixture.FixtureRun(T3_TOOL_PATH).build(base)
    if code != 0:
        return [], f"T3 build 종료 {code}: {(out + err)[-400:]}"
    plans = sorted(path for path in (base / t3_fixture.T_REL).rglob("*.json") if path.name in T3_PLAN_NAMES)
    if not plans:
        return [], "T3 build가 pilot1·ladder 계획을 하나도 만들지 않았다"
    return plans, None


def count_problem_lines(output):
    return sum(1 for line in output.splitlines() if line.startswith("문제:"))


def check_plan_negative(name, path, phrase):
    code, output = run_check_plan(path)
    check(f"④ check-plan 음성 {name}: 종료 2·사유 '{phrase}'", code == 2 and phrase in output and count_problem_lines(output) >= 1, (code, output))


def mutate_plan(pp, change):
    plan = make_p_plan(pp, make_sample_scenes())
    change(plan)
    return plan


def check_check_plan(pp, work):
    plans = work / "plans"
    fx4b_copy = plans / "fx4b-judge-plan-copy.json"
    fx4b_copy.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(FX4B_JUDGE_PLAN, fx4b_copy)
    code, output = run_check_plan(fx4b_copy)
    check("④ check-plan 양성 FX-4b 판정 계획 사본(3.3 선례): 종료 0·문제 0", code == 0 and count_problem_lines(output) == 0 and "통과" in output, (code, output))
    sample = write_json(plans / "p-sample.json", make_p_plan(pp, make_sample_scenes()))
    code, output = run_check_plan(sample)
    check("④ check-plan 양성 LG v2 형식 P 견본(actor·camera target_label·sleep others·console restore·SP 단계·Ls+X·Xn·C·L): 종료 0",
          code == 0 and count_problem_lines(output) == 0, (code, output))
    check("④ check-plan은 에디터 경로를 타지 않는다(앞창 경고·실행 폴더 출력 없음)", "경고:" not in output and "run " not in output, output)
    t3_base = work / "t3-fixture-root"
    built, build_problem = build_t3_fixture_plans(t3_base)
    check("④ check-plan 양성 P 생성 계획: T3 fixture 저장소 사본에서 p_plans.py build가 계획을 만든다(빈 목록이면 실패)", build_problem is None and built, build_problem)
    generated = sorted({path for pattern in P_GENERATED_PLAN_PATTERNS for path in ROOT.glob(pattern)})
    named_plans = [(path.relative_to(ROOT).as_posix(), path) for path in generated] + [("T3 fixture build " + path.relative_to(t3_base).as_posix(), path) for path in built]
    for name, path in named_plans:
        code, output = run_check_plan(path)
        check(f"④ check-plan 양성 P 생성 계획 {name}: 종료 0", code == 0 and count_problem_lines(output) == 0,
              (code, [line for line in output.splitlines() if line.startswith("문제:")][:6] or output[-400:]))
    code, output = run_check_plan(FX7P_OLD_PLAN)
    check("④ check-plan 음성 Tools/FluidFX/fx7p_plan.json(evidence 키 없음): 종료 2·사유 하나", code == 2 and count_problem_lines(output) == 1
          and "write_fx7p_evidence가 없다" in output, (code, output))
    check("④ 옛 계획 Tools/FluidFX/fx7p_plan.json은 고치지 않는다(git 변경 없음)", git_is_clean("Tools/FluidFX/fx7p_plan.json"), "git status")

    def add_scene_key(plan):
        plan["scenes"][0]["camera_view"] = "TDGen_CloudCam_Volcano"

    def add_draft(plan):
        plan["draft_status"] = "draft"

    def drop_labels(plan):
        del plan["scenes"][1]["b"][0]["labels"]

    def typo_niagara_key(plan):
        plan["scenes"][1]["b"][0]["niagra"] = plan["scenes"][1]["b"][0].pop("niagara")

    def typo_niagara_value(plan):
        plan["scenes"][1]["b"][0]["niagara"] = "deactive"

    def add_tick(plan):
        plan["scenes"][2]["b"][1]["tick"] = False

    def evidence_true(plan):
        plan["write_fx7p_evidence"] = True

    def evidence_missing(plan):
        del plan["write_fx7p_evidence"]

    def add_top_key(plan):
        plan["block_frame"] = 300

    def unknown_action(plan):
        plan["scenes"][0]["a"].append({"action": "camera_view_target", "target": "TDGen_CloudCam_Volcano"})

    def camera_label_conflict(plan):
        plan["scenes"][3]["setup"][2]["distance_cm"] = 500.0

    def hidden_text(plan):
        plan["scenes"][2]["b"][1]["hidden"] = "true"

    def empty_actor(plan):
        plan["scenes"][2]["b"][1] = {"action": "actor", "labels": ["TDGen_HvolVortexA"]}

    def duplicate_id(plan):
        plan["scenes"][1]["id"] = "FI1"

    def unknown_step_key(plan):
        plan["scenes"][0]["b"][1]["part"] = ["cards"]

    def missing_required(plan):
        del plan["scenes"][0]["b"][0]["awake"]

    negatives = (("모르는 장면 키", add_scene_key, "모르는 장면 키: camera_view"), ("draft_status", add_draft, "draft_status가 있다"),
                 ("actor labels 없음", drop_labels, "actor: labels가 없다"), ("niagara 키 오타", typo_niagara_key, "모르는 동작 키: niagra"),
                 ("niagara 값 오타", typo_niagara_value, "niagara 값은 deactivate 또는 activate"), ("actor tick 키", add_tick, "tick 키는 없다(E2"),
                 ("write_fx7p_evidence 참", evidence_true, "write_fx7p_evidence가 false가 아니다"),
                 ("write_fx7p_evidence 없음", evidence_missing, "write_fx7p_evidence가 없다"), ("모르는 최상위 키", add_top_key, "모르는 최상위 키: block_frame"),
                 ("모르는 동작", unknown_action, "모르는 동작: 'camera_view_target'"),
                 ("camera target_label과 배치 키", camera_label_conflict, "target_label과 함께 쓰지 않는 키: distance_cm"),
                 ("actor hidden 문자열", hidden_text, "hidden은 true 또는 false"), ("actor 빈 토글", empty_actor, "hidden·niagara 중 하나는 있어야 한다"),
                 ("장면 id 중복", duplicate_id, "중복"), ("모르는 동작 키", unknown_step_key, "visibility: 모르는 동작 키: part"),
                 ("필수 동작 키 없음", missing_required, "sleep: awake가 없다"))
    for name, change, phrase in negatives:
        check_plan_negative(name, write_json(plans / f"negative-{len(RESULTS):03d}.json", mutate_plan(pp, change)), phrase)
    broken = plans / "broken.json"
    broken.write_text('{"run_label": ', encoding="utf-8")
    check_plan_negative("잘린 JSON", broken, "계획 JSON을 읽지 못했다")
    comment = mutate_plan(pp, lambda plan: plan.update({"_note": "주석 키"}))
    code, output = run_check_plan(write_json(plans / "comment.json", comment))
    check("④ check-plan _ 시작 최상위 키는 주석(종료 0)", code == 0, (code, output))
    check("④ 허용 키는 코드 상수(최상위 = FX7P_DEFAULTS 키, actor = labels·hidden·niagara)",
          set(pp.FX7P_PLAN_KEYS) == set(pp.FX7P_DEFAULTS) and pp.FX7P_STEP_KEYS["actor"] == ("labels", "hidden", "niagara") and "tick" not in pp.FX7P_STEP_KEYS["actor"],
          (pp.FX7P_PLAN_KEYS, pp.FX7P_STEP_KEYS["actor"]))
    probe = subprocess.run([sys.executable, "-B", "-c", "import sys; sys.path.insert(0, 'Tools/FluidFX'); import pie_profile_fluid; print('unreal' in sys.modules)"],
                           cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")
    check("④ 모듈 첫머리는 unreal을 불러오지 않는다(--check-plan 에디터 없이)", probe.returncode == 0 and probe.stdout.strip() == "False", (probe.returncode, probe.stdout, probe.stderr))


def git_is_clean(relative):
    completed = subprocess.run(["git", "status", "--porcelain", "--", relative], cwd=ROOT, capture_output=True, text=True)
    return completed.returncode == 0 and completed.stdout.strip() == ""


def read_head_defaults():
    completed = subprocess.run(["git", "show", "HEAD:Tools/FluidFX/pie_profile_fluid.py"], cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if completed.returncode != 0:
        return None
    for node in ast.parse(completed.stdout).body:
        if isinstance(node, ast.Assign) and any(getattr(target, "id", None) == "FX7P_DEFAULTS" for target in node.targets):
            return ast.literal_eval(node.value)
    return None


def check_pure_functions(pp):
    empty = pp.find_fx7p_empty_target
    check("③ 대상 0: sleep targets others 0개는 예외(B5)", empty("sleep", {"action": "sleep", "targets": "others"}, {"awake": False, "actors": []}) is None)
    check("③ 대상 0: sleep(scene) 0개 → 장면 오류 사유", "sleep 대상이 0개다" in (empty("sleep", {"action": "sleep"}, {"awake": False, "actors": []}) or ""))
    check("③ 대상 0: visibility 부품 0개 → 사유", "visibility 대상이 0개다" in (empty("visibility", {}, {"actors": ["A"], "components": 0}) or ""))
    check("③ 대상 0: visibility 부품 2개 통과", empty("visibility", {}, {"actors": ["A"], "components": 2}) is None)
    check("③ 대상 0: light 광원 0개 → 사유", "light 대상이 0개다" in (empty("light", {}, {"visible": False, "lights": 0}) or ""))
    check("③ 대상 0: configure total 0 → 사유", "configure 대상이 0개다" in (empty("configure", {}, {"actors": [], "total": 0}) or ""))
    check("③ 대상 0: transmittance 0개 → 사유", "transmittance 대상이 0개다" in (empty("transmittance", {}, {"actors": []}) or ""))
    check("③ 대상 0: 수 기록 없음도 0으로 본다(엄격)", empty("sleep", {"action": "sleep"}, {"awake": True}) is not None)
    check("③ 대상 0: 이미 오류인 결과와 대상 없는 동작(console·csv)은 건드리지 않는다",
          empty("sleep", {}, {"error": "x", "actors": []}) is None and empty("console", {}, {"readback": {}}) is None and empty("csv", {}, {"actors": []}) is None)
    scene = {"targets": ["TDGen_HvolVortexA"]}
    check("기존 키 불변: sleep targets others → {exclude: 장면 targets}(PP:resolve_fx7p_step)",
          pp.resolve_fx7p_step({"action": "sleep", "targets": "others", "awake": False}, scene) == {"targets": {"exclude": ["TDGen_HvolVortexA"]}, "awake": False})
    check("기존 키 불변: targets 없음 → 장면 targets", pp.resolve_fx7p_step({"action": "sleep", "awake": True}, scene)["targets"] == ["TDGen_HvolVortexA"])
    link = pp.is_fx7p_link_error
    check("⑥ 원격 실행 끊김·결과 표식 없음·JSON 못 읽음은 환경 오류, 핸들러 오류는 아님",
          link({"error": pp.EDITOR_UNREACHABLE_ERROR + ": x"}) and link({"error": pp.EDITOR_MISSING_RESULT_ERROR + ": x"}) and link({"error": pp.EDITOR_UNREADABLE_RESULT_ERROR + ": x"})
          and not link({"error": "actor 라벨의 PIE 게임 월드 액터가 없다: X"}) and not link({"ok": True}))
    check("⑥ 장면 오류 분류: 환경 실패 있음 → environment, 없음 → scene, 오류 없음 → None",
          pp.classify_fx7p_scene_error("e", "env") == "environment" and pp.classify_fx7p_scene_error("e", None) == "scene" and pp.classify_fx7p_scene_error(None, None) is None)
    check("⑥ 종료 코드: 장면 오류(scene) 1, 종류 없는 옛 기록 오류 2",
          pp.compute_fx7p_exit_code({"X": {"error": "e", "error_kind": "scene", "kind": "csv"}}) == 1 and pp.compute_fx7p_exit_code({"X": {"error": "e", "kind": "csv"}}) == 2)
    restores = pp.collect_fx7p_actor_restores(make_hv2_scene())
    check("① 장면 끝 복원 단계: X → hidden false 한 번(a·b 중복 제거)", restores == [{"labels": ["TDGen_HvolVortexA", "TDGen_HvolVortexB"], "hidden": False}], restores)
    restores = pp.collect_fx7p_actor_restores(make_fi5b_scene())
    check("① 장면 끝 복원 단계: Xn → niagara activate", restores == [{"labels": ["TDGen_EmberB_Campfire"], "niagara": "activate"}], restores)
    check("① 장면 끝 복원 단계: actor 없는 장면 → 없음", pp.collect_fx7p_actor_restores(make_wa3_scene()) == [])
    params = pp.build_fx7p_actor_readback_params({"action": "actor", "labels": ["A"], "hidden": True})
    check("① 되읽기 인수: 선언 키만 + readback_only", params == {"labels": ["A"], "hidden": True, "readback_only": True}, params)
    head_defaults = read_head_defaults()
    check("⑧ FX7P_DEFAULTS는 HEAD와 같다(바꾸지 않음)", head_defaults is not None and head_defaults == pp.FX7P_DEFAULTS, "HEAD 대조")
    check("⑧ FX7P_DEFAULTS recorded_variables에 t.OverrideFPS 없음(계획이 더한다)", "t.OverrideFPS" not in pp.FX7P_DEFAULTS["recorded_variables"])
    merged = pp.merge_fx7p_plan(make_p_plan(pp, make_sample_scenes()))
    check("⑧ P 계획 merge 뒤 recorded_variables에 t.OverrideFPS(목록 대체)", "t.OverrideFPS" in merged["recorded_variables"])


def call_editor(pp, action, **fields):
    return pp.run_editor_action(action, pie_session=1, **fields)


def check_editor_handlers(pp):
    reset_fake(pp, build_fake_world(), "20261106000001")
    world = FAKE.world
    result = call_editor(pp, "actor", labels=["TDGen_CloudLayers"], hidden=True)
    check("① actor hidden true: 클래스 무관 숨김·되읽기 {labels, matched 1, hidden 1}, ns_inactive 키 없음",
          result == {"labels": ["TDGen_CloudLayers"], "matched": 1, "hidden": 1} and world.find("TDGen_CloudLayers").properties["hidden"] is True, result)
    result = call_editor(pp, "actor", labels=["TDGen_CloudLayers"], hidden=False, readback_only=True)
    check("① actor readback_only는 상태를 바꾸지 않는다", result["hidden"] == 1 and world.find("TDGen_CloudLayers").properties["hidden"] is True, result)
    result = call_editor(pp, "actor", labels=["TDGen_CloudLayers"], hidden=False)
    check("① actor hidden false → hidden 0", result.get("hidden") == 0 and world.find("TDGen_CloudLayers").properties["hidden"] is False, result)
    result = call_editor(pp, "actor", labels=["TDGen_CloudLayers", "TDGen_Missing"], hidden=True)
    check("①③ actor 라벨 하나라도 없음 → 오류, 다른 액터도 건드리지 않음",
          "라벨의 PIE 게임 월드 액터가 없다: TDGen_Missing" in str(result.get("error")) and world.find("TDGen_CloudLayers").properties["hidden"] is False, result)
    result = call_editor(pp, "actor", labels=["TDGen_EmberB_Campfire"], niagara="deactivate")
    component = world.find("TDGen_EmberB_Campfire").get_components_by_class(FakeNiagaraComponent)[0]
    check("① actor niagara deactivate: 파이썬에 deactivate_immediate 없으면 Deactivate, 되읽기 ns_inactive 1·hidden 키 없음",
          result == {"labels": ["TDGen_EmberB_Campfire"], "matched": 1, "ns_inactive": 1, "niagara_method": ["deactivate"]} and component.active is False, result)
    result = call_editor(pp, "actor", labels=["TDGen_EmberB_Campfire"], niagara="activate")
    check("① actor niagara activate → Activate(true)·ns_inactive 0", result.get("ns_inactive") == 0 and component.calls[-1] == "activate", result)
    result = call_editor(pp, "actor", labels=["TDGen_CloudLayers"], niagara="deactivate")
    check("①③ actor niagara 대상에 NS 없음 → 오류", "나이아가라 컴포넌트가 없다" in str(result.get("error")), result)
    reset_fake(pp, build_fake_world(immediate_niagara=True), "20261106000002")
    result = call_editor(pp, "actor", labels=["TDGen_EmberB_Campfire"], niagara="deactivate")
    check("① actor niagara deactivate: deactivate_immediate가 있으면 그것", result.get("niagara_method") == ["deactivate_immediate"], result)
    world = FAKE.world
    result = call_editor(pp, "camera", target_label="TDGen_CloudCam_Volcano")
    check("② camera target_label: 임의 라벨 액터를 뷰 대상으로", result.get("matched") == 1 and world.view_target is world.find("TDGen_CloudCam_Volcano"), result)
    cine_camera = world.find("TDGen_CloudCam_Volcano").get_components_by_class(FakeCameraComponent)[0]
    check("② camera target_label 뷰 대상 CineCamera의 화면 비율 제한(엔진 기본 참)을 PIE 인스턴스에서 끄고 aspect_constraints에 before 참·after 거짓·비율·시야 기록(렌더 화소 = conditions.viewport)",
          result.get("aspect_constraints") == [{"component": "CameraComponent", "constrain_before": True, "constrain_after": False, "aspect_ratio": 1.7777778, "field_of_view": 61.9}]
          and cine_camera.properties["constrain_aspect_ratio"] is False, result)
    result = call_editor(pp, "camera", target_label="TDGen_CloudCam_Volcano")
    check("② camera target_label 같은 카메라 다시: 이미 꺼진 제한은 before 거짓·after 거짓", [(entry["constrain_before"], entry["constrain_after"]) for entry in result.get("aspect_constraints") or []] == [(False, False)], result)
    layers = world.find("TDGen_CloudLayers")
    result = call_editor(pp, "camera", target_label="TDGen_CloudLayers")
    check("② camera target_label 카메라 컴포넌트 없는 액터 → aspect_constraints 키 없음·뷰 대상 바꿈", result.get("matched") == 1 and "aspect_constraints" not in result and "error" not in result
          and world.view_target is layers, result)
    layers.components.extend([FakeCineCameraComponent("CineFirst"), FakeCameraComponent("PlainSecond")])
    result = call_editor(pp, "camera", target_label="TDGen_CloudLayers")
    check("② camera target_label 카메라 컴포넌트 여럿 → 모두 끄고 각각 기록", [(entry["component"], entry["constrain_before"], entry["constrain_after"]) for entry in result.get("aspect_constraints") or []]
          == [("CineFirst", True, False), ("PlainSecond", False, False)], result)
    stuck_camera = FakeStuckCameraComponent("StuckCamera", constrained=True)
    layers.components = [component for component in layers.components if not isinstance(component, FakeCameraComponent)] + [stuck_camera]
    world.view_target = None
    result = call_editor(pp, "camera", target_label="TDGen_CloudLayers")
    check("②③ camera target_label 화면 비율 제한을 끄지 못함 → 오류(장면 오류)·뷰 대상 바꾸지 않음", "화면 비율 제한을 끄지 못했다: StuckCamera" in str(result.get("error"))
          and world.view_target is None, result)
    layers.components.remove(stuck_camera)
    result = call_editor(pp, "camera", target_label="TDGen_NoCamera")
    check("②③ camera target_label 0개 → 오류", "0개다" in str(result.get("error")), result)
    world.actors.append(FakeActor("TDGen_CloudCam_Volcano"))
    result = call_editor(pp, "camera", target_label="TDGen_CloudCam_Volcano")
    check("② camera target_label 2개 → 오류(모호)", "2개다" in str(result.get("error")), result)
    result = call_editor(pp, "visibility", targets=["TDGen_CampfireSmoke"], parts=["embers"], visible=False)
    check("③ visibility 결과 components = 실제로 바꾼 부품 수(불티 없는 연기 0)", result.get("components") == 0 and result.get("actors") == ["TDGen_CampfireSmoke"], result)
    result = call_editor(pp, "visibility", targets=["TDGen_CampfireFire"], parts=["cards", "embers"], visible=False)
    check("③ visibility 결과 components 2", result.get("components") == 2, result)
    result = call_editor(pp, "sleep", targets={"exclude": list(FLUID_LABELS)}, awake=False)
    check("기존 동작 불변: sleep exclude 전부 → 0개, 오류 아님(에디터 쪽)", result == {"awake": False, "actors": []}, result)


def run_plan_scenario(pp, work, name, plan, world, token):
    reset_fake(pp, world, token)
    return run_plan_scenario_keep(pp, work, name, plan, token)


def scene_record(session, scene_id):
    return next(record for record in session["scenes"] if record["id"] == scene_id)


def side_entries(block, action):
    return [entry for entry in block.get("side_steps") or [] if entry["action"] == action]


def blocks_by_side(record, side):
    return [block for block in record["blocks"] if block["side"] == side]


def check_full_p_run(pp, work):
    run = run_plan_scenario(pp, work, "s1-p-full", make_p_plan(pp, make_sample_scenes(), run_label="t1-s1"), build_fake_world(), "20261106000011")
    session = run["session"]
    check("S1 P 견본 5장면: --csv-gpu 종료 0·환경 오류 없음·result.json 5장면", run["exit"] == 0 and session and session["environment_error"] is None
          and run["result"] and len(run["result"]["scenes"]) == 5, (run["exit"], session and session["environment_error"], run["log"][-800:]))
    if not session:
        return
    records = session["scenes"]
    gate_checks = [record.get("cap_gate") or [] for record in records]
    check("⑥ 상한 관문은 B 블록을 검사한 첫 csv 장면(FI1)에서 끝나고 뒤 장면은 비활성", records[0]["cap_gate_active"] is True
          and len(gate_checks[0]) == len(blocks_by_side(records[0], "B")) >= 1 and all(record["cap_gate_active"] is False and not record.get("cap_gate") for record in records[1:]),
          [(record["id"], record["cap_gate_active"], len(gate)) for record, gate in zip(records, gate_checks)])
    blocks = [block for record in records for block in record["blocks"]]
    check("⑤ 모든 블록에 side_steps [{action, result}]", blocks and all(isinstance(block.get("side_steps"), list) and block["side_steps"]
          and all({"action", "result"} <= set(entry) for entry in block["side_steps"]) for block in blocks), len(blocks))
    check("⑥ P 계획 장면 기록에 error_kind(오류 없음 = None)", all("error_kind" in record and record["error_kind"] is None for record in records),
          [record.get("error_kind") for record in records])
    fi1 = scene_record(session, "FI1")
    b_block = blocks_by_side(fi1, "B")[0]
    check("⑤ L(FI1) B 블록 side_steps = sleep·visibility·visibility·light 결과", [entry["action"] for entry in b_block["side_steps"]] == ["sleep", "visibility", "visibility", "light"]
          and [entry["result"].get("components") for entry in side_entries(b_block, "visibility")] == [2, 1] and side_entries(b_block, "light")[0]["result"]["lights"] == 1,
          b_block["side_steps"])
    others = fi1["steps"][0]
    check("기존 동작: B5 sleep others = 대상 밖 라이브 유체만 재움", others["action"] == "sleep" and "TDGen_CampfireFire" not in others["result"]["actors"]
          and "TDGen_OtherFluid" in others["result"]["actors"], others)
    fi5b = scene_record(session, "FI5b")
    a_results = [side_entries(block, "actor")[0]["result"] for block in blocks_by_side(fi5b, "A")]
    b_results = [side_entries(block, "actor")[0]["result"] for block in blocks_by_side(fi5b, "B")]
    check("①⑤ Xn(FI5b) B 블록 actor 되읽기 {labels, matched 1, ns_inactive 1}, A ns_inactive 0, hidden 키 없음(선언 키만)",
          all(result.get("matched") == 1 and result.get("ns_inactive") == 1 and "hidden" not in result for result in b_results)
          and all(result.get("ns_inactive") == 0 for result in a_results) and len(a_results) == 2 and len(b_results) == 2, (a_results, b_results))
    applied = side_entries(blocks_by_side(fi5b, "B")[0], "actor")[0].get("applied") or {}
    check("① actor side_steps result = 측 안정 뒤 되읽기, applied = 단계 직후 결과", applied.get("niagara_method") == ["deactivate"] and "niagara_method" not in b_results[0], applied)
    hv2 = scene_record(session, "HV2")
    b_actor = [side_entries(block, "actor")[0]["result"] for block in blocks_by_side(hv2, "B")]
    a_actor = [side_entries(block, "actor")[0]["result"] for block in blocks_by_side(hv2, "A")]
    check("①⑤ Ls+X(HV2) B: sleep 결과 + actor {matched 2, hidden 2}, A hidden 0, ns_inactive 키 없음",
          all(result == {"labels": ["TDGen_HvolVortexA", "TDGen_HvolVortexB"], "matched": 2, "hidden": 2} for result in b_actor)
          and all(result.get("hidden") == 0 for result in a_actor) and all(side_entries(block, "sleep") for block in hv2["blocks"]), (a_actor, b_actor))
    cl1 = scene_record(session, "CL1")
    camera = [step for step in cl1["steps"] if step["action"] == "camera"]
    check("② CL1 setup camera target_label 결과 기록(matched 1)", camera and camera[0]["result"].get("target_label") == "TDGen_CloudCam_Volcano"
          and camera[0]["result"].get("matched") == 1, camera)
    cl1_constraints = (camera[0]["result"].get("aspect_constraints") or []) if camera else []
    check("② CL1 setup camera 결과 aspect_constraints: 구름 CineCamera 화면 비율 제한 before 참·after 거짓(블록 전에 꺼짐)",
          [(entry["constrain_before"], entry["constrain_after"]) for entry in cl1_constraints] == [(True, False)] and cl1["blocks"], cl1_constraints)
    check("① CL1 X: B hidden 1·A hidden 0", [side_entries(block, "actor")[0]["result"]["hidden"] for block in cl1["blocks"]] == [0, 1, 1, 0],
          [side_entries(block, "actor")[0]["result"] for block in cl1["blocks"]])
    check("기존 동작: setup console restore 참 → 장면 끝 복원(CloudSetup·SP)", (cl1.get("restored_console") or {}).get("readback", {}).get("TD.FluidFX.CloudSetup") == -1.0,
          cl1.get("restored_console"))
    wa3 = scene_record(session, "WA3")
    readbacks = [side_entries(block, "console")[0]["result"]["readback"] for block in wa3["blocks"]]
    check("⑦ C(WA3) A/B 토글 console 단계 되읽기를 블록마다 기록(A 1·B 0·B 0·A 1)",
          readbacks == [{"TD.FluidFX.WaterWetness": 1.0}, {"TD.FluidFX.WaterWetness": 0.0}, {"TD.FluidFX.WaterWetness": 0.0}, {"TD.FluidFX.WaterWetness": 1.0}], readbacks)
    conditions_ok = all("t.OverrideFPS" in ((record.get("conditions") or {}).get("variables") or {}) for record in records)
    pie_conditions_ok = all("t.OverrideFPS" in ((entry.get("conditions") or {}).get("variables") or {}) for entry in session["pie_sessions"])
    check("⑧ 계획 recorded_variables의 t.OverrideFPS가 장면·PIE conditions에 남는다", conditions_ok and pie_conditions_ok and session["pie_sessions"],
          [record.get("conditions") for record in records][:1])
    restored = {record["id"]: record.get("restored_actors") for record in records}
    restored_results = {scene_id: [entry["result"] for entry in restored[scene_id] or []] for scene_id in restored}
    check("① 장면 끝 복원 = 건드린 속성을 보임·Activate(true)(FI5b ns_inactive 0, HV2·CL1 hidden 0, actor 없는 장면은 키 없음)",
          restored_results["FI5b"] == [{"labels": ["TDGen_EmberB_Campfire"], "matched": 1, "ns_inactive": 0, "niagara_method": ["activate"]}]
          and restored_results["HV2"] == [{"labels": ["TDGen_HvolVortexA", "TDGen_HvolVortexB"], "matched": 2, "hidden": 0}]
          and restored_results["CL1"] == [{"labels": ["TDGen_CloudLayers"], "matched": 1, "hidden": 0}] and restored["FI1"] is None and restored["WA3"] is None,
          restored_results)
    hidden_left = [actor.label for actor in FAKE.world.actors if actor.properties.get("hidden")]
    check("① 실행 끝 가짜 월드에 숨긴 액터가 남지 않는다(PIE 재시작은 숨김을 되돌리지 않는 가짜)", not hidden_left, hidden_left)


def check_scene_error_continues(pp, work):
    scenes = [make_hv2_scene(labels=("TDGen_HvolVortexA", "TDGen_Missing"), level=HVOL_LEVEL, scene_id="HVX"), make_wa3_scene(level=HVOL_LEVEL, scene_id="WA3a"),
              make_empty_sleep_scene(HVOL_LEVEL), make_wa3_scene(level=HVOL_LEVEL, scene_id="WA3b"), make_empty_visibility_scene(HVOL_LEVEL),
              make_wa3_scene(level=HVOL_LEVEL, scene_id="WA3c"), make_spike_scene(HVOL_LEVEL, "SPK", 1), make_spike_scene(HVOL_LEVEL, "WA3d", 0)]
    error_ids = ("HVX", "CLX", "FIX", "SPK")
    after_ids = ("WA3a", "WA3b", "WA3c", "WA3d")
    run = run_plan_scenario(pp, work, "s2-p-scene-error", make_p_plan(pp, scenes, run_label="t1-s2"), build_fake_world(), "20261106000012")
    session = run["session"]
    check("S2 ⑥ 장면 오류 뒤에도 실행 계속: 8장면 기록·환경 오류 없음·result.json 8장면·종료 1", run["exit"] == 1 and session and session["environment_error"] is None
          and len(session["scenes"]) == 8 and run["result"] and len(run["result"]["scenes"]) == 8, (run["exit"], session and session["environment_error"], run["log"][-800:]))
    if not session or len(session["scenes"]) != 8:
        return
    records = {record["id"]: record for record in session["scenes"]}
    check("⑥①③ 라벨 없는 actor 장면 = 장면 오류(error_kind scene), 사유에 라벨", records["HVX"]["error_kind"] == "scene"
          and "라벨의 PIE 게임 월드 액터가 없다: TDGen_Missing" in str(records["HVX"]["error"]), records["HVX"]["error"])
    check("③ P 대상 0: sleep(scene) 대상 0개 = 장면 오류", records["CLX"]["error_kind"] == "scene" and "sleep 대상이 0개다" in str(records["CLX"]["error"]), records["CLX"]["error"])
    check("③ P 대상 0: visibility 부품 0개 = 장면 오류", records["FIX"]["error_kind"] == "scene" and "visibility 대상이 0개다" in str(records["FIX"]["error"]), records["FIX"]["error"])
    check("⑥ 셰이더 관문 실패(돌출 프레임) = 장면 오류(V3 환경 목록 밖)", records["SPK"]["error_kind"] == "scene" and "셰이더 관문 실패" in str(records["SPK"]["error"]),
          records["SPK"]["error"])
    fresh = [records[scene_id]["fresh_pie"] for scene_id in after_ids]
    check("⑥ 장면 오류 다음 장면은 새 PIE(fresh_pie after_scene_error, 같은 레벨인데 PIE 다시 시작)", fresh == ["after_scene_error"] * 4
          and len(session["pie_sessions"]) == 5 and FAKE.pie_starts == 5, (fresh, len(session["pie_sessions"]), FAKE.pie_starts))
    check("⑥ 오류 뒤 장면은 정상 측정(블록 4·오류 없음)", all(records[scene_id]["error"] is None and len(records[scene_id]["blocks"]) == 4 for scene_id in after_ids),
          [(scene_id, records[scene_id]["error"]) for scene_id in after_ids])
    check("⑥ 오류 장면도 restore 수행(setup console 복원 기록)", all(records[scene_id].get("restored_console") for scene_id in error_ids),
          [records[scene_id].get("restored_console") for scene_id in error_ids])
    check("⑥ result.json 장면에 error_kind 전달", all(run["result"]["scenes"][scene_id].get("error_kind") == "scene" for scene_id in error_ids))
    check("⑥ 표준 출력에 장면 오류 계속 문구", run["log"].count("장면 오류(기록하고 다음 장면은 새 PIE)") == 4, run["log"][-600:])


def check_environment_error_stops(pp, work):
    scenes = [make_wa3_scene(scene_id="WA3a"), make_hv2_scene(), make_wa3_scene(scene_id="WA3b")]
    plan = make_p_plan(pp, scenes, run_label="t1-s3")
    reset_fake(pp, build_fake_world(), "20261106000013")
    run = run_with_failure(pp, work, "s3-p-environment", plan, "csv", 7, "20261106000013")
    session = run["session"]
    check("S3 ⑥ 환경 오류(원격 실행 끊김) → 실행 멈춤·종료 2·result.json 없음", run["exit"] == 2 and session and pp.EDITOR_UNREACHABLE_ERROR in str(session["environment_error"])
          and run["result"] is None and len(session["scenes"]) == 2, (run["exit"], session and session["environment_error"], session and len(session["scenes"])))
    if session:
        record = session["scenes"][1]
        check("⑥ 환경 오류 장면 error_kind environment·actor 복원은 건너뜀", record.get("error_kind") == "environment" and "restored_actors" not in record, record.get("error_kind"))
    reset_fake(pp, build_fake_world(), "20261106000014")
    FAKE.hold_errors = {2: "보류 owner other가 --holder p-perf와 다르다"}
    run = run_plan_scenario_keep(pp, work, "s5-p-hold", make_p_plan(pp, [make_wa3_scene(scene_id="WA3a"), make_wa3_scene(scene_id="WA3b"), make_wa3_scene(scene_id="WA3c")],
                                                                    run_label="t1-s5"), "20261106000014")
    session = run["session"]
    check("S5 ⑥ 보류 실패 → 환경 오류로 멈춤(2장면 기록, 종료 2)", run["exit"] == 2 and session and "보류 owner other" in str(session["environment_error"])
          and len(session["scenes"]) == 2 and session["scenes"][1].get("error_kind") == "environment", (run["exit"], session and session["environment_error"]))
    reset_fake(pp, build_fake_world(), "20261106000015")
    plan = make_p_plan(pp, [make_wa3_scene(scene_id="WA3a"), make_wa3_scene(scene_id="WA3b")], run_label="t1-s6")
    plan["cap_gate"] = dict(CAP_TRIP_GATE)
    run = run_plan_scenario_keep(pp, work, "s6-p-cap", plan, "20261106000015")
    session = run["session"]
    check("S6 ⑥ 프레임 상한 관문 = 환경 오류(실행 멈춤)", run["exit"] == 2 and session and "상한 관문" in str(session["environment_error"])
          and session["scenes"][0].get("error_kind") == "environment" and len(session["scenes"]) == 1, (run["exit"], session and session["environment_error"]))


def check_cap_gate_after_first_scene_error(pp, work):
    first_scenes = (("setup 단계", make_setup_error_scene(HVOL_LEVEL, "SUX")),
                    ("A측 단계", make_hv2_scene(labels=("TDGen_HvolVortexA", "TDGen_Missing"), level=HVOL_LEVEL, scene_id="HVX")),
                    ("셰이더 관문", make_spike_scene(HVOL_LEVEL, "SPK", 1)),
                    ("첫 B 블록 전 B측 단계", make_b_side_error_scene(HVOL_LEVEL, "HVB")))
    for index, (where, first_scene) in enumerate(first_scenes):
        plan = make_p_plan(pp, [first_scene, make_spike_scene(HVOL_LEVEL, "WA3a", 0)], run_label=f"t1-s12-{index}")
        plan["cap_gate"] = dict(CAP_TRIP_GATE)
        run = run_plan_scenario(pp, work, f"s12-p-cap-after-{index}", plan, build_fake_world(), f"2026110600003{index}")
        session = run["session"] or {}
        records = {record["id"]: record for record in session.get("scenes", [])}
        first_record = records.get(first_scene["id"], {})
        second_record = records.get("WA3a", {})
        check(f"S12 ⑥ 첫 장면 {where} 오류(상한 관문이 B 블록을 못 봄) 뒤 둘째 장면에 상한 관문이 다시 걸려 환경 오류로 멈춤(종료 2)",
              run["exit"] == 2 and "WA3a_r1_2B 상한 관문" in str(session.get("environment_error")) and first_record.get("error_kind") == "scene"
              and not first_record.get("cap_gate") and second_record.get("cap_gate_active") is True and second_record.get("error_kind") == "environment",
              {"exit": run["exit"], "environment_error": session.get("environment_error"), "first": (first_record.get("error_kind"), str(first_record.get("error"))[:160]),
               "WA3a": (second_record.get("cap_gate_active"), second_record.get("cap_gate"), second_record.get("error_kind"))})


def check_timeouts_and_pie_failures(pp, work):
    reset_fake(pp, build_fake_world(), "20261106000019")
    FAKE.csv_writes_left = 7
    plan = make_p_plan(pp, [make_wa3_scene(scene_id="WA3a"), make_wa3_scene(scene_id="WA3b"), make_wa3_scene(scene_id="WA3c")], run_label="t1-s9")
    plan["csv_timeout_seconds"] = 0.5
    run = run_plan_scenario_keep(pp, work, "s9-p-csv-timeout", plan, "20261106000019")
    session = run["session"]
    check("S9 ⑥ CSV 시간 초과 = 환경 오류(실행 멈춤·종료 2·2장면 기록)", run["exit"] == 2 and session and "CSV가 완성되지 않았다" in str(session["environment_error"])
          and len(session["scenes"]) == 2 and session["scenes"][1].get("error_kind") == "environment", (run["exit"], session and session["environment_error"]))
    reset_fake(pp, build_fake_world(), "20261106000020")
    FAKE.pie_start_failures = {2}
    plan = make_p_plan(pp, [make_hv2_scene(labels=("TDGen_HvolVortexA", "TDGen_Missing"), level=HVOL_LEVEL, scene_id="HVX"), make_wa3_scene(level=HVOL_LEVEL, scene_id="WA3a"),
                            make_wa3_scene(level=HVOL_LEVEL, scene_id="WA3b")], run_label="t1-s10")
    run = run_plan_scenario_keep(pp, work, "s10-p-pie-start", plan, "20261106000020")
    session = run["session"]
    check("S10 ⑥ 장면 오류 뒤 새 PIE 시작 실패 = 환경 오류(실행 멈춤)", run["exit"] == 2 and session and "PIE를 시작하지 못했다" in str(session["environment_error"])
          and [record.get("error_kind") for record in session["scenes"]] == ["scene", "environment"], (run["exit"], session and session["environment_error"]))


def run_with_failure(pp, work, name, plan, action, allowed_calls, token):
    FAKE.fail_action = action
    FAKE.fail_after = allowed_calls
    return run_plan_scenario_keep(pp, work, name, plan, token)


def run_plan_scenario_keep(pp, work, name, plan, token):
    plan_path = write_json(work / "plans" / f"{name}.json", {"csv_timeout_seconds": 5.0, "file_timeout_seconds": 5.0, **plan})
    log_path = work / "logs" / f"{name}.log"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    pp.RUN_TOKEN = token
    with open(log_path, "w", encoding="utf-8") as handle, contextlib.redirect_stdout(handle):
        exit_code = pp.run_fx7p_plan(plan_path, HOLDER)
    run_dir = pp.FX7P_OUTPUT_ROOT / f"{plan['run_label']}-{token}"
    session_path = run_dir / pp.FX7P_SESSION_NAME
    result_path = run_dir / pp.FX7P_RESULT_NAME
    return {"exit": exit_code, "session": json.loads(session_path.read_text(encoding="utf-8")) if session_path.exists() else None,
            "result": json.loads(result_path.read_text(encoding="utf-8")) if result_path.exists() else None, "log": log_path.read_text(encoding="utf-8")}


def check_others_without_other_fluids(pp, work):
    world = build_fake_world(include_other_fluid=False)
    world.actors = [actor for actor in world.actors if not isinstance(actor, FakeFluidActor) or actor.label == "TDGen_CoastStageWaterSea"]
    run = run_plan_scenario(pp, work, "s4-p-others-empty", make_p_plan(pp, [make_wa3_scene()], run_label="t1-s4"), world, "20261106000016")
    session = run["session"]
    others = session["scenes"][0]["steps"][0] if session else None
    check("S4 ③ 예외: 대상 밖 유체가 없는 맵의 sleep targets others(0개)는 오류 아님", run["exit"] == 0 and others and others["result"] == {"awake": False, "actors": []}
          and session["scenes"][0]["error"] is None, (run["exit"], others, run["log"][-500:]))


def check_partial_scene_error(pp, work):
    scene = make_b_side_error_scene(HVOL_LEVEL, "HVB")
    run = run_plan_scenario(pp, work, "s11-p-partial", make_p_plan(pp, [scene, make_wa3_scene(level=HVOL_LEVEL)], run_label="t1-s11"), build_fake_world(), "20261106000021")
    session = run["session"]
    records = {record["id"]: record for record in (session or {}).get("scenes", [])}
    hvb = records.get("HVB", {})
    analyzed = ((run["result"] or {}).get("scenes") or {}).get("HVB") or {}
    check("S11 ⑥ 블록 일부를 잰 뒤 B측 단계 오류: 장면 오류(블록 1 남김)·분석 예외 없음·다음 장면 새 PIE·종료 1", run["exit"] == 1 and hvb.get("error_kind") == "scene"
          and len(hvb.get("blocks", [])) == 1 and analyzed.get("error_kind") == "scene" and records.get("WA3", {}).get("fresh_pie") == "after_scene_error"
          and records.get("WA3", {}).get("error") is None
          and [entry["result"].get("hidden") for entry in hvb.get("restored_actors") or []] == [0, None]
          and "TDGen_Missing" in str((hvb.get("restored_actors") or [{}, {}])[-1].get("result", {}).get("error")),
          (run["exit"], hvb.get("error"), len(hvb.get("blocks", [])), run["log"][-500:]))


def check_old_plan_behavior(pp, work):
    scenes = [make_hv2_scene(labels=("TDGen_HvolVortexA", "TDGen_Missing"), scene_id="HVX"), make_wa3_scene()]
    run = run_plan_scenario(pp, work, "s7-old-scene-error", make_p_plan(pp, scenes, run_label="t1-s7", include_marker=False), build_fake_world(), "20261106000017")
    session = run["session"]
    check("S7 기존 동작 불변(_p 없는 계획): 장면 오류에서 실행 멈춤·종료 2·error_kind 키 없음", run["exit"] == 2 and session and "actor 실패" in str(session["environment_error"])
          and len(session["scenes"]) == 1 and "error_kind" not in session["scenes"][0], (run["exit"], session and session["environment_error"]))
    run = run_plan_scenario(pp, work, "s8-old-empty-sleep", make_p_plan(pp, [make_empty_sleep_scene(CLOUD_LEVEL)], run_label="t1-s8", include_marker=False),
                            build_fake_world(), "20261106000018")
    session = run["session"]
    check("S8 기존 동작 불변(_p 없는 계획): 대상 0 단계는 오류 아님·실행 완료", run["exit"] == 0 and session and session["scenes"][0]["error"] is None
          and run["result"] is not None and "error_kind" not in run["result"]["scenes"]["CLX"], (run["exit"], session and session["scenes"][0]["error"]))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--keep", action="store_true", help="임시 폴더를 지우지 않는다")
    arguments = parser.parse_args()
    work = TMP_ROOT / datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    work.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(ROOT / "Tools"))
    sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))
    import pie_profile_fluid as pp
    original_sleep = time.sleep
    original_modules = {name: sys.modules.get(name) for name in ("unreal", "pie_showcase_fluid")}
    groups = ((check_check_plan, (pp, work)), (check_pure_functions, (pp,)), (install_fakes, (pp, work)), (check_editor_handlers, (pp,)),
              (check_full_p_run, (pp, work)), (check_scene_error_continues, (pp, work)), (check_partial_scene_error, (pp, work)),
              (check_environment_error_stops, (pp, work)), (check_cap_gate_after_first_scene_error, (pp, work)), (check_timeouts_and_pie_failures, (pp, work)), (check_others_without_other_fluids, (pp, work)),
              (check_old_plan_behavior, (pp, work)))
    try:
        for group, group_arguments in groups:
            try:
                group(*group_arguments)
            except Exception:
                check(f"{group.__name__} 예외 없이 끝남", False, traceback.format_exc()[-700:])
    finally:
        time.sleep = original_sleep
        for name, module in original_modules.items():
            if module is None:
                sys.modules.pop(name, None)
            else:
                sys.modules[name] = module
        if not arguments.keep:
            shutil.rmtree(work, ignore_errors=True)
    passed = sum(RESULTS)
    print(f"fixtures_t1: 통과 {passed}/{len(RESULTS)}" + ("" if not arguments.keep else f" (임시 {work.relative_to(ROOT).as_posix()})"))
    return 0 if RESULTS and passed == len(RESULTS) else 1


if __name__ == "__main__":
    sys.exit(main())
