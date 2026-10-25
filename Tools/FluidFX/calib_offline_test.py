"""실행 환경: 시스템 Python 3(에디터·PIE 불필요, numpy·Pillow·scipy 필요, 가짜 unreal 로 calib_sweep·pie_lookdev_fluid·pie_probe_fluid_water 를 프로세스 안에서 돌린다). 2026-10-25 P3 오프라인 시험(w0-P3-offline-test.py)을 본 트리로 옮겨 새 판정 규칙(목표 지표 대조·연속 점수·점수 잡음 유의)과 짧은 창 배율·대조 처음·끝 모드를 더했다.
실행 명령: python Tools/FluidFX/calib_offline_test.py (종료 코드 0 전부 통과 / 1 실패 있음, 마지막 줄 SUMMARY passed=N/M)
출력: Saved/FluidFX/calib/offline-test/ 아래 스윕·판정 산출물(실행마다 지움)과 표준 출력 PASS/FAIL 줄. 수용 시험 산출(Saved/FluidFX/calib/{campfire-run1,campfire-run2,campfire-r4-run1,river-run1})이 있으면 새 판정기로 재판정해 SKIP 아닌 결과를 낸다.
상태: 2026-10-25 작성, 실제 PIE 스윕은 돌리지 않는다(정본 JSON sha256 전후 비교 포함).
"""
import contextlib
import copy
import io
import json
import shutil
import subprocess
import sys
import time
import types
from pathlib import Path

from PIL import Image, ImageDraw

W = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(W / "Tools"))
sys.path.insert(0, str(W / "Tools" / "FluidFX"))
import pie_lookdev_fluid as lookdev  # noqa: E402
import pie_showcase_fluid  # noqa: E402
import calib_judge  # noqa: E402
import calib_sweep  # noqa: E402
import fluid_tuning_patch  # noqa: E402
import prereg_lock  # noqa: E402

TEST_DIR = W / "Saved" / "FluidFX" / "calib" / "offline-test"
ACCEPTANCE_DIR = W / "Saved" / "FluidFX" / "calib"
KIND_JSON = {"CAMPFIRE": "Campfire", "RIVER": "River", "SEA": "Sea", "TORCH": "Torch", "CUSTOM": "Custom"}
CANONICAL = fluid_tuning_patch.canonical_tuning_path()
CAMPFIRE_PLAN = "Tools/FluidFX/calib/plans/campfire_vorticity.json"
RIVER_PLAN = "Tools/FluidFX/calib/plans/river_vorticity.json"
RESULTS = []
PROBE_WINDOW_SCALES = []


def check(name, condition, detail=""):
    RESULTS.append((name, bool(condition)))
    print(("PASS " if condition else "FAIL ") + name + ("" if condition else f"  {detail}"))


class FakeEnum:
    def __init__(self, name):
        self.name = name


class Props:
    def __init__(self, **values):
        for key, value in values.items():
            setattr(self, key, value)

    def get_editor_property(self, name):
        value = getattr(self, name)
        if isinstance(value, Props) and not isinstance(value, unreal.Object) and not hasattr(value, "label") and type(value) is Props:
            return copy.copy(value)
        return value

    def set_editor_property(self, name, value):
        setattr(self, name, value)


class FakeModule(types.ModuleType):
    def __getattr__(self, name):
        if name.startswith("_") or not name[0].isupper():
            raise AttributeError(name)
        dummy = type(name, (), {})
        setattr(self, name, dummy)
        return dummy


unreal = FakeModule("unreal")
unreal.EnumBase = FakeEnum
unreal.Object = type("Object", (Props,), {})
unreal.Vector2D = type("Vector2D", (), {})
unreal.Vector = type("Vector", (), {})
unreal.LinearColor = type("LinearColor", (), {})


class FakePreset(unreal.Object):
    def __init__(self, kind="CAMPFIRE", override=False, vorticity=1.5):
        super().__init__(preset_kind=FakeEnum(kind), override_preset_defaults=override,
                         simulation_config=Props(vorticity_strength=vorticity, enable_heat_channel=True), plane_settings=Props(camera_depth_bias_cm=10.0),
                         volume_render=Props(density_scale=1.0), volume_height_cm=100.0)

    def get_path_name(self):
        return "/Game/FluidFX/Presets/DA_Fake"

    def get_name(self):
        return f"Preset_{id(self)}"


unreal.new_object = lambda cls: cls()


def uses_copies(preset):
    return preset.preset_kind.name == "CUSTOM" or preset.override_preset_defaults


class FakeLive(unreal.Object):
    def __init__(self, world, preset):
        super().__init__(active_preset=preset, simulation_config=Props(vorticity_strength=preset.simulation_config.vorticity_strength, enable_heat_channel=True))
        self.world = world
        self.init_count = 0

    def init_system_state_from_preset(self, preset):
        self.init_count += 1
        value = preset.simulation_config.vorticity_strength if uses_copies(preset) else self.world.effective(preset.preset_kind.name)
        self.simulation_config = Props(vorticity_strength=value, enable_heat_channel=True)


class TDFluidLiveActor(unreal.Actor, Props):
    def __init__(self, world, label, preset):
        super().__init__(live_component=FakeLive(world, preset), source_emitter=Props(radius_cm=26.0), ember_component=None, internal_surface_mesh=None, volume_mesh_component=None)
        self.label = label

    def get_actor_label(self):
        return self.label


class TDFluidFireActor(TDFluidLiveActor):
    pass


unreal.TDFluidLiveActor = TDFluidLiveActor
unreal.TDFluidFireActor = TDFluidFireActor


class FakeWorld:
    def __init__(self):
        self.actors = []
        self.table = json.loads(CANONICAL.read_text(encoding="utf-8"))
        self.generation = 1
        self.pending = None
        self.report = {"table_generation": 1, "tuning_source_path": CANONICAL.as_posix(), "look_source_path": "", "reapplied_actor_count": 0,
                       "state_reset_actor_count": 0, "changed_field_count": 0, "live_material_count": 0, "bake_only_changed_count": 0}
        self.report_calls = 0
        self.source = CANONICAL.as_posix()
        self.shot_count = 0

    def effective(self, kind_name):
        kind = KIND_JSON[kind_name]
        entries = self.table["Kinds"].get(kind, {}).get("TDFluidSimulationConfig", {})
        if "VorticityStrength" in entries:
            return entries["VorticityStrength"]
        for group in self.table["Groups"].values():
            if kind in group["Kinds"] and "VorticityStrength" in group.get("TDFluidSimulationConfig", {}):
                return group["TDFluidSimulationConfig"]["VorticityStrength"]
        return self.table["StructDefaults"]["TDFluidSimulationConfig"]["VorticityStrength"]

    def tick_reapply(self):
        changed = 0
        for actor in self.actors:
            live = actor.live_component
            if uses_copies(live.active_preset):
                continue
            value = self.effective(live.active_preset.preset_kind.name)
            if live.simulation_config.vorticity_strength != value:
                changed += 1
                live.simulation_config.vorticity_strength = value
        self.report = dict(self.report, table_generation=self.pending, tuning_source_path=self.source, changed_field_count=changed, reapplied_actor_count=len(self.actors))
        self.pending = None


WORLD = FakeWorld()


class FakeTuningSubsystem:
    def reload_preset_tuning_from_paths(self, tuning, look):
        path = Path(tuning) if tuning else CANONICAL
        if tuning and (W / "Saved").resolve() not in path.resolve().parents:
            self.errors = ["refused"]
            return 1
        self.errors = []
        WORLD.table = json.loads(path.read_text(encoding="utf-8"))
        WORLD.source = path.as_posix()
        WORLD.generation += 1
        WORLD.pending = WORLD.generation
        return 0

    def get_load_errors(self):
        return self.errors

    def get_table_generation(self):
        return WORLD.generation

    def get_tuning_source_path(self):
        return WORLD.source

    def get_look_source_path(self):
        return ""

    def is_using_override_paths(self):
        return WORLD.source != CANONICAL.as_posix()


TUNING = FakeTuningSubsystem()


class FakeLiveSubsystem:
    @staticmethod
    def get_fluid_live_subsystem(world):
        return FakeLiveSubsystem()

    def get_last_reapply_report(self):
        WORLD.report_calls += 1
        if WORLD.pending is not None and WORLD.report_calls % 2 == 0:
            WORLD.tick_reapply()
        return Props(**WORLD.report)


unreal.TDFluidLiveSubsystem = FakeLiveSubsystem
unreal.get_engine_subsystem = lambda cls: TUNING
unreal.get_editor_subsystem = lambda cls: Props(get_game_world=lambda: WORLD)


def all_actors(world, cls):
    return [actor for actor in world.actors if isinstance(cls, type) and isinstance(actor, cls)]


def console(world, command, controller=None):
    if command.startswith("HighResShot"):
        path = Path(command.split("filename=", 1)[1])
        path.parent.mkdir(parents=True, exist_ok=True)
        value = WORLD.actors[0].live_component.simulation_config.vorticity_strength if WORLD.actors else 1.0
        WORLD.shot_count += 1
        image = Image.new("RGB", (1280, 720), (20, 24, 40))
        draw = ImageDraw.Draw(image)
        half = int(60 + 40 * value)
        draw.rectangle((850 - half, 300 - half, 850 + half, 300 + half // 2), fill=(255, 150 + (WORLD.shot_count % 2), 40))
        draw.rectangle((850 - half // 3, 260 - half // 3, 850 + half // 3, 260), fill=(255, 230, 120))
        image.save(path)


unreal.GameplayStatics = Props(get_all_actors_of_class=all_actors, get_time_seconds=lambda world: 1.0, get_player_controller=lambda world, index: None)
unreal.SystemLibrary = Props(execute_console_command=console, get_console_variable_float_value=lambda name: 0.0)
sys.modules["unreal"] = unreal


def fake_editor_action(action, **fields):
    code = pie_showcase_fluid.EDITOR_CODE.replace("\ndef run():", lookdev.LOOKDEV_ACTIONS, 1)
    code = code.replace("__PARAMS__", repr(json.dumps({"action": action, **fields})))
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        exec(compile(code, "<editor>", "exec"), {"__name__": "editor"})
    for line in buffer.getvalue().splitlines():
        if pie_showcase_fluid.RESULT_MARKER in line:
            return json.loads(line.split(pie_showcase_fluid.RESULT_MARKER, 1)[1])
    return {"error": "no marker: " + buffer.getvalue()[-300:]}


lookdev.editor_action = fake_editor_action
lookdev.pie_check_fluid.wait_for_pie_state = lambda running, timeout: True
lookdev.pie_check_fluid.stop_pie = lambda session, environment: True
lookdev.uemcp.connect = lambda: None
lookdev.FRAME_FILE_WAIT_SECONDS = 0.5
time.sleep = lambda seconds: None


def reset_world(actors):
    global WORLD
    WORLD = FakeWorld()
    WORLD.actors = [cls(WORLD, label, preset) for cls, label, preset in actors]


def run_lookdev_in_process(spec_path, level, output_dir):
    saved_argv = sys.argv
    sys.argv = ["pie_lookdev_fluid.py", "--variants", str(spec_path), "--level", level, "--output-dir", str(output_dir), "--stop-pie"]
    try:
        return lookdev.main()
    finally:
        sys.argv = saved_argv


calib_sweep.run_lookdev = run_lookdev_in_process


def run_sweep(arguments):
    saved_argv = sys.argv
    sys.argv = ["calib_sweep.py"] + arguments
    try:
        with contextlib.redirect_stderr(io.StringIO()):
            return calib_sweep.main()
    finally:
        sys.argv = saved_argv


def python(*arguments):
    completed = subprocess.run([sys.executable, *arguments], capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=W)
    return completed.returncode, completed.stdout + completed.stderr


def write_plan(name, mutate):
    plan = json.loads((W / RIVER_PLAN).read_text(encoding="utf-8"))
    plan = mutate(plan) or plan
    path = TEST_DIR / f"{name}.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(plan), encoding="utf-8")
    return path


def load_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


shutil.rmtree(TEST_DIR, ignore_errors=True)
canonical_before = fluid_tuning_patch.file_sha256(CANONICAL)
look_canonical_before = fluid_tuning_patch.file_sha256(fluid_tuning_patch.canonical_look_path())

reset_world([(TDFluidFireActor, "TDGen_CampfireFire", FakePreset("CAMPFIRE"))])
outcome = lookdev.run_tuning_reload({"tuning": "", "look": "", "reset": "bogus"})
check("tuning_reload 잘못된 reset 거부", "error" in outcome, outcome)
outcome = lookdev.run_tuning_reload({"tuning": "C:/Users/jjh/elsewhere.json", "look": ""})
check("Saved 밖 경로 거부(표 유지)", "error" in outcome and WORLD.source == CANONICAL.as_posix(), outcome)

reset_world([(TDFluidFireActor, "TDGen_CampfireFire", FakePreset("CAMPFIRE")), (TDFluidFireActor, "TDGen_Custom", FakePreset("CUSTOM", vorticity=9.0))])
applied = fake_editor_action("lookdev_apply", apply={"preset": {"TDGen_CampfireFire": {"plane_settings": {"camera_depth_bias_cm": 30.0}}}}, tag="t", output=str(TEST_DIR), size="1280x720")
entry = applied.get("preset", {}).get("TDGen_CampfireFire", {})
check("preset R2 무시 키 오류", entry.get("ignored_copy_paths") == ["plane_settings.camera_depth_bias_cm"] and WORLD.actors[0].live_component.active_preset.plane_settings.camera_depth_bias_cm == 10.0, applied)
check("find_key_errors 가 R2 키를 셈", len(lookdev.find_key_errors(applied)) == 1, lookdev.find_key_errors(applied))
applied = fake_editor_action("lookdev_apply", apply={"preset": {"TDGen_CampfireFire": {"force_override": True, "plane_settings": {"camera_depth_bias_cm": 30.0}}}}, tag="t", output=str(TEST_DIR), size="1280x720")
active = WORLD.actors[0].live_component.active_preset
check("preset force_override 복제 override 켬", applied["preset"]["TDGen_CampfireFire"].get("forced_override") is True and active.override_preset_defaults and active.plane_settings.camera_depth_bias_cm == 30.0, applied)
reset_world([(TDFluidFireActor, "TDGen_CampfireFire", FakePreset("CAMPFIRE")), (TDFluidFireActor, "TDGen_Custom", FakePreset("CUSTOM", vorticity=9.0))])
applied = fake_editor_action("lookdev_apply", apply={"emitter": {"TDGen_CampfireFire": {"radius_cm": 40.0}, "TDGen_Custom": {"radius_cm": 41.0}}}, tag="t", output=str(TEST_DIR), size="1280x720")
check("emitter R2 무시 오류·Custom 은 적용", applied["emitter"]["TDGen_CampfireFire"].get("ignored_copy_paths") == ["source_emitter.radius_cm"] and WORLD.actors[0].source_emitter.radius_cm == 26.0 and WORLD.actors[1].source_emitter.radius_cm == 41.0, applied)
applied = fake_editor_action("lookdev_apply", apply={"emitter": {"TDGen_CampfireFire": {"force_override": True, "radius_cm": 40.0}}}, tag="t", output=str(TEST_DIR), size="1280x720")
check("emitter force_override", applied["emitter"]["TDGen_CampfireFire"].get("forced_override") is True and WORLD.actors[0].source_emitter.radius_cm == 40.0 and WORLD.actors[0].live_component.active_preset.override_preset_defaults, applied)
read = fake_editor_action("lookdev_tuning_read_back", requests=[{"struct": "TDFluidSimulationConfig", "field": "bEnableHeatChannel", "kinds": ["Campfire", "Custom"]}, {"struct": "TDFluidPlaneSettings", "field": "CameraDepthBiasCm", "kinds": ["Campfire"]}])
check("read_back 불 이름 변환·미지원 구조체 오류", read["read_back"][0]["actors"]["TDGen_CampfireFire"]["value"] is True and read["read_back"][0]["actors"]["TDGen_Custom"]["uses_own_copies"] is True and "error" in read["read_back"][1], read)

code = run_sweep(["--plan", RIVER_PLAN, "--dry-run", "--run-dir", str(TEST_DIR / "river-dry")])
sweep = load_json(TEST_DIR / "river-dry" / "sweep.json")
check("강 계획 dry-run 종료 0·대조 처음·끝 2회만(control_middle false)", code == 0 and sweep["sequence"] == ["v00", "v01", "v02", "v01", "v02", "v00"], (code, sweep.get("sequence")))
check("강 스윕 기록에 settle·probe_window_scale·목표 지표", sweep["scenes"][0]["settle"] == 1.0 and sweep["scenes"][0]["probe_window_scale"] == 0.5 and len(sweep["metrics"]["targets"]) == 6, sweep["scenes"][0])
campfire_dry = run_sweep(["--plan", CAMPFIRE_PLAN, "--dry-run", "--run-dir", str(TEST_DIR / "campfire-dry")])
campfire_dry_sweep = load_json(TEST_DIR / "campfire-dry" / "sweep.json")
check("모닥불 계획 dry-run: 대조 처음·중간·끝(기본)", campfire_dry == 0 and campfire_dry_sweep["sequence"] == ["v00", "v01", "v02", "v00", "v01", "v02", "v00"], campfire_dry_sweep.get("sequence"))
check("build_sequence control_middle 거짓·control_every 3", calib_sweep.build_sequence(["v01", "v02"], 2, 0, False) == ["v00", "v01", "v02", "v01", "v02", "v00"] and calib_sweep.build_sequence(["v01", "v02"], 3, 3, False) == ["v00", "v01", "v02", "v01", "v00", "v02", "v01", "v02", "v00"], calib_sweep.build_sequence(["v01", "v02"], 3, 3, False))
river_copy = load_json(TEST_DIR / "river-dry" / "v01" / "TDFluidPresetTuning.json")
check("강 사본 split_group: River 만 0.2, Sea 0.3, 묶음에서 빠짐", river_copy["Kinds"]["River"]["TDFluidSimulationConfig"]["VorticityStrength"] == 0.2 and river_copy["Kinds"]["Sea"]["TDFluidSimulationConfig"]["VorticityStrength"] == 0.3 and "VorticityStrength" not in river_copy["Groups"]["WaterLayer"]["TDFluidSimulationConfig"], river_copy["Kinds"]["River"])
check("대조 사본은 정본과 바이트 동일", (TEST_DIR / "river-dry" / "v00" / "TDFluidPresetTuning.json").read_bytes() == CANONICAL.read_bytes())

bad_plans = {
    "no_split": {"json_path": "Kinds.River.TDFluidSimulationConfig.VorticityStrength", "values": [0.2, 0.3], "split_group": False},
    "unsupported_struct": {"json_path": "Kinds.Campfire.TDFluidPlaneSettings.CameraDepthBiasCm", "values": [20.0], "split_group": False},
    "control_only": {"json_path": "Kinds.Campfire.TDFluidSimulationConfig.VorticityStrength", "values": [1.5], "split_group": False},
}
base_plan = load_json(W / CAMPFIRE_PLAN)
for name, param in bad_plans.items():
    plan = dict(base_plan, id=f"bad_{name}", params=[param])
    plan_path = TEST_DIR / f"bad_{name}.json"
    plan_path.parent.mkdir(parents=True, exist_ok=True)
    plan_path.write_text(json.dumps(plan), encoding="utf-8")
    check(f"계획 거부 {name} 종료 2", run_sweep(["--plan", str(plan_path), "--run-dir", str(TEST_DIR / f"bad_{name}")]) == 2)

bad_metric_plans = {
    "no_targets": lambda plan: plan["metrics"].pop("targets"),
    "empty_targets": lambda plan: plan["metrics"].update(targets=[]),
    "water_without_range": lambda plan: plan["metrics"]["targets"][0].pop("low"),
    "range_reversed": lambda plan: plan["metrics"]["targets"][0].update(low=0.5, high=-0.5),
    "weight_zero": lambda plan: plan["metrics"]["targets"][0].update(weight=0),
    "tolerance_negative": lambda plan: plan["metrics"]["targets"][0].update(tolerance=-1),
    "duplicate_metric": lambda plan: plan["metrics"]["targets"].append(dict(plan["metrics"]["targets"][0])),
    "unknown_scene": lambda plan: plan["metrics"]["targets"][0].update(scene="nowhere"),
    "window_zero": lambda plan: plan["scenes"][0].update(probe_window_scale=0),
    "control_middle_text": lambda plan: plan.update(control_middle="no"),
}
for name, mutate in bad_metric_plans.items():
    plan_path = write_plan(f"bad_{name}", mutate)
    check(f"계획 거부 {name} 종료 2", run_sweep(["--plan", str(plan_path), "--run-dir", str(TEST_DIR / f"bad_{name}")]) == 2)

reset_world([(TDFluidFireActor, "TDGen_CampfireFire", FakePreset("CAMPFIRE")), (TDFluidFireActor, "TDGen_TorchFire", FakePreset("TORCH"))])
code = run_sweep(["--plan", CAMPFIRE_PLAN, "--run-dir", str(TEST_DIR / "campfire")])
sweep = load_json(TEST_DIR / "campfire" / "sweep.json")
steps = sweep["scenes"][0]["steps"]
check("모닥불 스윕 종료 0·7단계 유효·이미지", code == 0 and len(steps) == 7 and all(step["valid"] and step["images"] for step in steps), sweep.get("problems"))
check("대조 단계 ChangedFieldCount 0·변형 단계 1", [step["report"]["changed_field_count"] for step in steps] == [0, 1, 1, 0, 1, 1, 0], [step["report"]["changed_field_count"] for step in steps])
check("실행 뒤 정본 다시 읽음(표 출처 정본)", WORLD.source == CANONICAL.as_posix())
check("재초기화 호출(reinit 기본)", WORLD.actors[0].live_component.init_count == 7, WORLD.actors[0].live_component.init_count)

reset_world([(TDFluidFireActor, "TDGen_CampfireFire", FakePreset("CAMPFIRE", override=True))])
code = run_sweep(["--plan", CAMPFIRE_PLAN, "--run-dir", str(TEST_DIR / "campfire-override")])
sweep_override = load_json(TEST_DIR / "campfire-override" / "sweep.json")
check("override DA 장면: 무효 변형 거부 종료 1", code == 1 and any("Custom 또는 override" in line for line in sweep_override["problems"]) and any("ChangedFieldCount 0 < 1" in line for line in sweep_override["problems"]), sweep_override["problems"][:3])
reset_world([(TDFluidFireActor, "TDGen_TorchFire", FakePreset("TORCH"))])
code = run_sweep(["--plan", CAMPFIRE_PLAN, "--run-dir", str(TEST_DIR / "campfire-missing")])
sweep_missing = load_json(TEST_DIR / "campfire-missing" / "sweep.json")
check("대상 종류 액터 없음: 무효 종료 1", code == 1 and any("액터가 장면에 없다" in line for line in sweep_missing["problems"]), sweep_missing["problems"][:2])

lock_path = TEST_DIR / "campfire-prereg-lock.json"
exit_code, output = python("Tools/FluidFX/calib_judge.py", "--sweep", str(TEST_DIR / "campfire" / "sweep.json"), "--lock", str(lock_path))
check("잠금 없으면 판정 종료 2", exit_code == 2, output[-300:])
exit_code, output = python("Tools/FluidFX/prereg_lock.py", "lock", "--out", str(lock_path), "--plan", CAMPFIRE_PLAN, "--note", "offline")
check("잠금 생성 종료 0", exit_code == 0, output[-300:])
check("잠금 덮어쓰기 거부", python("Tools/FluidFX/prereg_lock.py", "lock", "--out", str(lock_path), "--plan", CAMPFIRE_PLAN)[0] == 1)
exit_code, output = python("Tools/FluidFX/calib_judge.py", "--sweep", str(TEST_DIR / "campfire" / "sweep.json"), "--lock", str(lock_path))
judge_path = TEST_DIR / "campfire" / "judge.json"
scene = (load_json(judge_path).get("scenes") or [{}])[0] if judge_path.exists() else {}
check("모닥불 판정 실행(0 또는 1)·목표 표 6개·곡선 3점·LIVE-2 시트·후보", exit_code in (0, 1) and len(scene.get("targets", [])) == 6 and len(scene.get("curves", [{}])[0].get("points", [])) == 3 and scene.get("live2_sheet") and scene.get("candidate", {}).get("variant"), output[-600:])
check("Docs 출력 거부 종료 2", python("Tools/FluidFX/calib_judge.py", "--sweep", str(TEST_DIR / "campfire" / "sweep.json"), "--lock", str(lock_path), "--output", "Docs/Validation/x.json")[0] == 2)
tampered = load_json(TEST_DIR / "campfire" / "sweep.json")
tampered["plan_sha256"] = "0" * 64
(TEST_DIR / "tampered_sweep.json").write_text(json.dumps(tampered), encoding="utf-8")
check("잠금과 계획 sha 다르면 종료 2", python("Tools/FluidFX/calib_judge.py", "--sweep", str(TEST_DIR / "tampered_sweep.json"), "--lock", str(lock_path))[0] == 2)
check("무효 스윕 판정 거부 종료 1", python("Tools/FluidFX/calib_judge.py", "--sweep", str(TEST_DIR / "campfire-override" / "sweep.json"), "--lock", str(lock_path))[0] == 1)
scratch_file = TEST_DIR / "locked_note.txt"
scratch_file.write_text("a", encoding="utf-8")
python("Tools/FluidFX/prereg_lock.py", "lock", "--out", str(TEST_DIR / "note-lock.json"), "--file", str(scratch_file))
first = python("Tools/FluidFX/prereg_lock.py", "verify", "--lock", str(TEST_DIR / "note-lock.json"))[0]
scratch_file.write_text("b", encoding="utf-8")
check("잠금 검증 일치 0 → 바뀌면 2", first == 0 and python("Tools/FluidFX/prereg_lock.py", "verify", "--lock", str(TEST_DIR / "note-lock.json"))[0] == 2)

import pie_probe_fluid_water as probe  # noqa: E402


def fake_probe_run(points, layout, contract, delay, backend, window_scale=1.0):
    PROBE_WINDOW_SCALES.append(window_scale)
    vorticity = WORLD.actors[0].live_component.simulation_config.vorticity_strength
    rules = {"flow_matches_target_center_3pts": {"passed": True, "value": [0.10 + vorticity * 0.05, 0.12, 0.11], "required": True},
             "riffle_speed_ratio": {"passed": True, "value": {"ratio": 2.0 + vorticity, "wide_cm_per_sec": 200.0, "riffle_cm_per_sec": 400.0}, "required": True},
             "rock_wake_density": {"passed": vorticity >= 0.3, "value": 0.4 + 0.5 * vorticity, "required": True},
             "crossing_ripple_height": {"passed": True, "value": {"max_abs_ripple_cm": 50.0, "seconds_in_water": 0.5, "reached_goal": True}, "required": True}}
    return {"rules": rules, "failed": [name for name, rule in rules.items() if not rule["passed"]]}


probe.read_stage_report = lambda: {"river": {"probe_points": {}, "layout": {}}}
probe.read_material_contract = lambda: {"scalar_names": [], "texture_names": [], "vector_names": []}
probe.open_stage_level = lambda: {"loaded": True}
probe.uemcp.connect = lambda: None
probe.pie_check_fluid.wait_for_editor_navigation = lambda: True
probe.pie_check_fluid.start_pie = lambda session, environment: True
probe.editor_action = lambda action, **fields: {"sleep_delay_seconds": 1.0} if action == "status" else {}
probe.prepare_backend = lambda setup, backend, grid: True
probe.restore_backend = lambda setup: True
probe.run_once = fake_probe_run
reset_world([(TDFluidLiveActor, "TDGen_River", FakePreset("RIVER", vorticity=0.3)), (TDFluidLiveActor, "TDGen_Sea", FakePreset("SEA", vorticity=0.3))])
code = run_sweep(["--plan", RIVER_PLAN, "--run-dir", str(TEST_DIR / "river")])
river_sweep = load_json(TEST_DIR / "river" / "sweep.json")
river_steps = river_sweep["scenes"][0]["steps"]
check("강 물 탐침 스윕 종료 0·6단계 유효·Sea 불변", code == 0 and len(river_steps) == 6 and all(step["valid"] and step["water_probe"] for step in river_steps) and WORLD.actors[1].live_component.simulation_config.vorticity_strength == 0.3, river_sweep.get("problems"))
check("짧은 창 배율이 단계마다 탐침에 전달되고 단계 시간이 기록됨", PROBE_WINDOW_SCALES == [0.5] * 6 and all(isinstance(step.get("elapsed_seconds"), float) for step in river_steps), (PROBE_WINDOW_SCALES, [step.get("elapsed_seconds") for step in river_steps]))
river_lock = TEST_DIR / "river-prereg-lock.json"
python("Tools/FluidFX/prereg_lock.py", "lock", "--out", str(river_lock), "--plan", RIVER_PLAN)
exit_code, output = python("Tools/FluidFX/calib_judge.py", "--sweep", str(TEST_DIR / "river" / "sweep.json"), "--lock", str(river_lock))
river_judge = load_json(TEST_DIR / "river" / "judge.json")
river_scene = river_judge["scenes"][0]
check("강 판정: 대조 통과·가짜 탐침에서 v02 가 연속 점수 최소·유의", exit_code == 0 and river_judge["accepted"] and river_scene["candidate"]["variant"] == "v02" and river_scene["candidate"]["is_significant"] and "live2_sheet" not in river_scene, output[-500:])
check("강 판정: 목표 표 6개·점수 3변형 서로 다름·보조 규칙 점수 남음", len(river_scene["targets"]) == 6 and len(set(river_scene["candidate"]["scores"].values())) == 3 and "rule_score" in river_scene["candidate"], river_scene["candidate"])

legs = {"asleep": lambda scale: probe.run_asleep_leg({"asleep_pawn": [0, 0, 0]}, 5.0, scale), "wake": lambda scale: probe.run_wake_leg({"flow_pawn": [0, 0, 0]}, scale),
        "flow": lambda scale: probe.run_flow_leg({"flow_pawn": [0, 0, 0], "wide_center": [[0, 0, 0]] * 3, "riffle_center": [[0, 0, 0]] * 3}, {}, scale),
        "rock": lambda scale: probe.run_rock_leg({"pawn": [0, 0, 0], "center": [0, 0], "tangent": [1, 0], "bounding_radius_cm": 50.0}, scale),
        "submerged": lambda scale: probe.run_submerged_leg({"pawn": [0, 0, 0], "center": [0, 0], "tangent": [1, 0], "bounding_radius_cm": 50.0}, scale),
        "crossing": lambda scale: probe.run_crossing_leg({"start": [0, 0, 0], "goal": [0, 100, 0], "tangent": [1, 0], "foam_box": {"origin": [0, 0], "half_width_cm": 100.0, "s_range_cm": [0, 100], "step_cm": 10}}, "cpu", scale)}
probe.log_size = lambda: 0
probe.read_log_since = lambda offset: ""
probe.build_flow_points = lambda points: {}
probe.build_rock_points = lambda rock: {}
probe.build_submerged_points = lambda rock: {}
recorded = []
time.sleep = lambda seconds: recorded.append(seconds)
leg_sleeps = {}
for leg_name, run_leg in legs.items():
    for scale in (1.0, 0.5):
        recorded.clear()
        try:
            run_leg(scale)
        except Exception as error:
            leg_sleeps[(leg_name, scale)] = repr(error)
            continue
        leg_sleeps[(leg_name, scale)] = list(recorded)
time.sleep = lambda seconds: None
expected_full = {"asleep": [5.0 + probe.SLEEP_MARGIN_SECONDS, probe.PERF_MEASURE_SECONDS], "wake": [probe.WAKE_OBSERVE_SECONDS], "flow": [probe.FLOW_SETTLE_SECONDS, probe.FLOW_MEASURE_SECONDS],
                 "rock": [probe.ROCK_SETTLE_SECONDS, probe.ROCK_MEASURE_SECONDS], "submerged": [probe.SUBMERGED_SETTLE_SECONDS, probe.SUBMERGED_MEASURE_SECONDS],
                 "crossing": [probe.CROSSING_SETTLE_SECONDS, probe.FOAM_SNAPSHOT_GAP_SECONDS]}
check("탐침 대기 배율 1.0 은 기존 대기 시간 그대로", all(leg_sleeps[(name, 1.0)] == waits for name, waits in expected_full.items()), {name: leg_sleeps[(name, 1.0)] for name in expected_full})
scaled_names = {"wake": [0], "flow": [0, 1], "rock": [0, 1], "submerged": [0, 1], "crossing": [0], "asleep": [1]}
check("탐침 대기 배율 0.5 는 정착·측정 대기만 절반(잠들기 지연·소품 간격은 그대로)",
      all(isinstance(leg_sleeps[(name, 0.5)], list) and all(abs(leg_sleeps[(name, 0.5)][index] - expected_full[name][index] * 0.5) < 1e-9 for index in indexes) for name, indexes in scaled_names.items())
      and leg_sleeps[("asleep", 0.5)][0] == expected_full["asleep"][0] and leg_sleeps[("crossing", 0.5)][1] == probe.FOAM_SNAPSHOT_GAP_SECONDS, leg_sleeps)


def synthetic_sweep(targets, noise_floor=None):
    variants = [{"id": "v00", "expect": [{"value": 1.0}], "assignments": []}, {"id": "v01", "expect": [{"value": 2.0}], "assignments": [{"json_path": "x", "value": 2.0, "split_group": False}]},
                {"id": "v02", "expect": [{"value": 3.0}], "assignments": [{"json_path": "x", "value": 3.0, "split_group": False}]}]
    return {"metrics": {"sources": ["water_probe"], "noise_floor": noise_floor or {}, "targets": targets}, "variants": variants, "params": [{"json_path": "x", "base_value": 1.0}]}


def synthetic_items(rows):
    return [{"step": index, "variant": variant, "metrics": dict(metrics), calib_judge.RULE_METRIC: 0, "bands": {}} for index, (variant, metrics) in enumerate(rows)]


def judge_synthetic(rows, targets, noise_floor=None):
    sweep = synthetic_sweep(targets, noise_floor)
    items = synthetic_items(rows)
    resolved = calib_judge.resolve_targets(sweep, "s", items)
    aggregated = calib_judge.aggregate(items, sweep, resolved)
    return resolved, aggregated, calib_judge.choose_candidate(sweep, aggregated)


target_a = {"metric": "a", "low": 0.0, "high": 10.0, "weight": 1.0}
rows = [("v00", {"a": 5.0, "b": 1.0}), ("v01", {"a": 4.0, "b": 1.0}), ("v02", {"a": 6.0, "b": 1.0}), ("v01", {"a": 4.4, "b": 1.0}), ("v02", {"a": 6.2, "b": 1.0}), ("v00", {"a": 5.6, "b": 9.0})]
_, aggregated, _ = judge_synthetic(rows, [target_a])
check("대조 허용 2×잡음: 표류 0.6 > 잡음 0.4 여도 ≤0.8 이면 통과", aggregated["control_check"] == {"a": "pass"} and aggregated["control_ok"] and abs(aggregated["control_allowed"]["a"] - 0.8) < 1e-9, aggregated["control_allowed"])
check("목표 밖 지표 b 표류 8.0 은 진단에만 남고 control_ok 에 안 걸림", "b" in aggregated["diagnostic_drift_over_allowed"] and "b" not in aggregated["control_check"] and aggregated["control_ok"], aggregated["diagnostic_drift_over_allowed"])
rows_fail = [row if index != 5 else ("v00", {"a": 5.9, "b": 1.0}) for index, row in enumerate(rows)]
_, aggregated, _ = judge_synthetic(rows_fail, [target_a])
check("대조 표류 0.9 > 2×잡음 0.8 이면 실패·control_ok 거짓", aggregated["control_check"] == {"a": "fail"} and not aggregated["control_ok"], aggregated["control_check"])
rows_flat = [("v00", {"a": 5.0}), ("v01", {"a": 4.0}), ("v02", {"a": 6.0}), ("v01", {"a": 4.0}), ("v02", {"a": 6.0}), ("v00", {"a": 5.05})]
_, aggregated, _ = judge_synthetic(rows_flat, [dict(target_a, tolerance=0.1)])
_, aggregated_zero, _ = judge_synthetic(rows_flat, [target_a])
check("잡음 0 이어도 계획 절대 허용 0.1 안의 표류 0.05 는 통과, 허용 0 이면 실패", aggregated["control_check"] == {"a": "pass"} and aggregated_zero["control_check"] == {"a": "fail"}, (aggregated["control_check"], aggregated_zero["control_check"]))
rows_single = [("v00", {"a": 5.0}), ("v01", {"a": 4.0}), ("v02", {"a": 6.0}), ("v00", {"a": 5.2})]
_, aggregated, candidate = judge_synthetic(rows_single, [target_a])
_, aggregated_tolerance, _ = judge_synthetic(rows_single, [dict(target_a, tolerance=0.5)])
check("반복 1(잡음 바닥 없음)·허용 0 이면 판정 불가, 허용이 있으면 판정", aggregated["control_check"] == {"a": "undetermined"} and not aggregated["control_ok"] and aggregated_tolerance["control_check"] == {"a": "pass"} and candidate["is_significant"] is False, (aggregated["control_check"], aggregated_tolerance["control_check"]))

target_c = {"metric": "c", "low": 100.0, "high": 200.0, "weight": 2.0}
rows = [("v00", {"a": 5.0, "c": 100.0}), ("v01", {"a": 7.5, "c": 150.0}), ("v02", {"a": 5.0, "c": 125.0}), ("v01", {"a": 7.5, "c": 150.0}), ("v02", {"a": 5.0, "c": 125.0}), ("v00", {"a": 5.0, "c": 100.0})]
resolved, aggregated, candidate = judge_synthetic(rows, [target_a, target_c])
expected = {"v00": 2.0, "v01": 0.5, "v02": 1.0}
check("연속 점수: Σ 가중치×|값−중앙|/반폭 손계산과 일치·최소 v01", all(abs(candidate["scores"][name] - value) < 1e-9 for name, value in expected.items()) and candidate["variant"] == "v01", candidate["scores"])
check("잡음 0 이면 개선 1.5 > 점수 잡음 0 → 유의", candidate["is_significant"] and abs(candidate["improvement"] - 1.5) < 1e-9 and candidate["score_noise_floor"] == 0.0, candidate)
noisy_rows = [("v00", {"a": 5.0, "c": 100.0}), ("v01", {"a": 5.0, "c": 99.0}), ("v02", {"a": 5.0, "c": 125.0}), ("v01", {"a": 5.0, "c": 101.0}), ("v02", {"a": 5.0, "c": 125.0}), ("v00", {"a": 5.0, "c": 100.0})]
_, aggregated, candidate = judge_synthetic(noisy_rows, [target_a, target_c])
check("점수 잡음 환산: 가중치 2 × 잡음 2 / 반폭 50 = 0.08, 개선 1.0 이 더 커서 v02 유의", candidate["variant"] == "v02" and candidate["is_significant"] and abs(candidate["score_noise_floor"] - 2.0 * 2.0 / 50.0) < 1e-9, candidate)
small_rows = [("v00", {"c": 100.0}), ("v01", {"c": 90.0}), ("v02", {"c": 105.0}), ("v01", {"c": 118.0}), ("v02", {"c": 107.0}), ("v00", {"c": 101.0})]
_, aggregated, candidate = judge_synthetic(small_rows, [target_c])
check("잡음 안 차이는 유의 아님: 개선 < 점수 잡음이면 is_significant 거짓", candidate["variant"] is not None and not candidate["is_significant"] and candidate["improvement"] < candidate["score_noise_floor"], candidate)
tie_rows = [("v00", {"a": 5.0}), ("v01", {"a": 5.0}), ("v02", {"a": 5.0}), ("v01", {"a": 5.0}), ("v02", {"a": 5.0}), ("v00", {"a": 5.0})]
_, aggregated, candidate = judge_synthetic(tie_rows, [target_a])
check("동점이면 대조 변형을 후보로(유의 아님)", candidate["variant"] == "v00" and not candidate["is_significant"], candidate)
missing_rows = [("v00", {"a": 5.0}), ("v01", {}), ("v02", {"a": 6.0}), ("v01", {"a": 4.0}), ("v02", {"a": 6.0}), ("v00", {"a": 5.0})]
resolved, aggregated, candidate = judge_synthetic(missing_rows, [target_a])
check("목표 값이 빠진 단계는 점수에서 빠지고 나머지로 판정", aggregated["repeat_counts"]["v01"] == 2 and candidate["variant"] is not None and "v01" in candidate["scores"], candidate)
try:
    calib_judge.resolve_targets(synthetic_sweep([{"metric": "nope", "low": 0.0, "high": 1.0}]), "s", synthetic_items(rows))
    unknown_rejected = False
except calib_judge.JudgeError as error:
    unknown_rejected = "측정값에 없다" in str(error)
try:
    calib_judge.resolve_targets(synthetic_sweep([]), "s", synthetic_items(rows))
    empty_rejected = False
except calib_judge.JudgeError as error:
    empty_rejected = "metrics.targets" in str(error)
try:
    calib_judge.resolve_targets(synthetic_sweep([{"metric": "a"}]), "s", synthetic_items(rows))
    range_rejected = False
except calib_judge.JudgeError as error:
    range_rejected = "목표 범위" in str(error)
check("목표 판정 입력 오류: 없는 지표·목표 목록 없음·범위 없음 거부", unknown_rejected and empty_rejected and range_rejected, (unknown_rejected, empty_rejected, range_rejected))
band_items = synthetic_items([("v00", {"0.campfire.m": 5.0})])
band_items[0]["bands"] = {"campfire.m": ["fire", 0.0, 10.0]}
band_resolved = calib_judge.resolve_targets(synthetic_sweep([{"metric": "0.campfire.m", "weight": 2.0}]), "s", band_items)
check("look 목표는 low·high 가 없으면 LIVE-2 합격 범위를 씀", band_resolved[0]["center"] == 5.0 and band_resolved[0]["half_width"] == 5.0 and band_resolved[0]["weight"] == 2.0, band_resolved)
flat = calib_judge.water_rule_metrics({"r": {"value": {"ratio": 2.0, "nested": {"x": 1.5, "flag": True}, "list": [1.0, None, 3.0], "name": "text"}}, "s": {"value": [0.1, 0.2]}, "t": {"value": 4.0}, "u": {"value": None}})
check("물 규칙 값 펼치기: 사전 항목은 이름.키, 목록은 이름[번호], 불·문자열 제외", flat == {"r.ratio": 2.0, "r.nested.x": 1.5, "r.list[0]": 1.0, "r.list[2]": 3.0, "s[0]": 0.1, "s[1]": 0.2, "t": 4.0}, flat)

rejudge_dir = TEST_DIR / "rejudge"
rejudge_jobs = [("campfire-run1", CAMPFIRE_PLAN), ("campfire-run2", CAMPFIRE_PLAN), ("campfire-r4-run1", CAMPFIRE_PLAN), ("river-run1", RIVER_PLAN)]
rejudged = {}
for run_name, plan_text in rejudge_jobs:
    old_sweep_path = ACCEPTANCE_DIR / run_name / "sweep.json"
    if not old_sweep_path.is_file():
        print(f"SKIP 수용 시험 산출 {run_name} 없음")
        continue
    run_dir = rejudge_dir / run_name
    run_dir.mkdir(parents=True)
    old_sweep = load_json(old_sweep_path)
    plan_path = W / plan_text
    old_sweep.update(plan=plan_text, plan_sha256=prereg_lock.file_sha256(plan_path), metrics=load_json(plan_path)["metrics"])
    (run_dir / "sweep.json").write_text(json.dumps(old_sweep, ensure_ascii=False, indent=2), encoding="utf-8")
    prereg_lock.create_lock(prereg_lock.plan_lock_files(plan_path), run_dir / "lock.json", "rejudge")
    exit_code, output = python("Tools/FluidFX/calib_judge.py", "--sweep", str(run_dir / "sweep.json"), "--lock", str(run_dir / "lock.json"), "--output", str(run_dir / "judge.json"))
    rejudged[run_name] = (exit_code, load_json(run_dir / "judge.json")["scenes"][0] if (run_dir / "judge.json").is_file() else None, output)
for run_name in ("campfire-run1", "campfire-run2", "campfire-r4-run1"):
    if run_name not in rejudged:
        continue
    exit_code, scene, output = rejudged[run_name]
    old_judge = ACCEPTANCE_DIR / run_name / "judge.json"
    old_ok = load_json(old_judge)["scenes"][0]["control_ok"] if old_judge.is_file() else None
    print(f"  재판정 {run_name}: 이전 control_ok={old_ok} → 새 control_ok={scene and scene['control_ok']} 후보 {scene and scene['candidate']['variant']} 점수 {scene and round(scene['candidate']['score'], 4)} 대조 {scene and round(scene['candidate']['control_score'], 4)} 점수 잡음 {scene and round(scene['candidate']['score_noise_floor'], 4)} 유의 {scene and scene['candidate']['is_significant']} 실패 {[t['metric'] for t in (scene or {}).get('targets', []) if t['control_check'] != 'pass']}")
    check(f"재판정 {run_name}: 모닥불 control_ok 계산됨(판정 불가 0)·종료 0 또는 1", scene is not None and exit_code in (0, 1) and isinstance(scene["control_ok"], bool) and all(t["control_check"] in ("pass", "fail") for t in scene["targets"]) and len(scene["targets"]) == 6, output[-400:])
if "river-run1" in rejudged:
    exit_code, scene, output = rejudged["river-run1"]
    print(f"  재판정 river-run1: 새 control_ok={scene and scene['control_ok']} 후보 {scene and scene['candidate']['variant']} 점수 {scene and scene['candidate']['scores']} 점수 잡음 {scene and round(scene['candidate']['score_noise_floor'], 4)} 유의 {scene and scene['candidate']['is_significant']}")
    check("재판정 river-run1: 연속 점수 3변형이 서로 달라 최소 점수 변형을 후보로 고름", scene is not None and len(set(scene["candidate"]["scores"].values())) == 3 and scene["candidate"]["variant"] == min(scene["candidate"]["scores"], key=lambda name: (scene["candidate"]["scores"][name], name != "v00", name)), output[-400:])
    wake = next(item for item in scene["targets"] if item["metric"] == "rock_wake_density")
    check("재판정 river-run1: 와도에 따라 변하는 rock_wake_density 거리가 변형마다 다르게 점수에 들어감", len(set(round(value, 6) for value in wake["distance_by_variant"].values())) == 3, wake["distance_by_variant"])

check("실행 전후 정본 sha256 동일", fluid_tuning_patch.file_sha256(CANONICAL) == canonical_before and fluid_tuning_patch.file_sha256(fluid_tuning_patch.canonical_look_path()) == look_canonical_before)

failed = [name for name, passed in RESULTS if not passed]
print(f"SUMMARY passed={len(RESULTS) - len(failed)}/{len(RESULTS)}")
sys.exit(1 if failed else 0)
