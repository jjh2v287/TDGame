"""실행 환경: 시스템 Python 3(에디터·PIE 불필요, numpy·Pillow·scipy 필요, 가짜 unreal 로 calib_sweep·pie_lookdev_fluid·pie_probe_fluid_water 를 프로세스 안에서 돌린다). 2026-10-25 P3 오프라인 시험(w0-P3-offline-test.py)을 본 트리로 옮겨 새 판정 규칙(목표 지표 대조·연속 점수·점수 잡음 유의)과 짧은 창 배율·대조 처음·끝 모드를 더했다.
실행 명령: python -B Tools/FluidFX/calib_offline_test.py (종료 코드 0 전부 통과 / 1 실패 있음, 마지막 줄 SUMMARY passed=N/M)
출력: Saved/FluidFX/calib/offline-test/ 아래 스윕·판정 산출물(실행마다 지움)과 표준 출력 PASS/FAIL 줄. 수용 시험 산출(Saved/FluidFX/calib/{campfire-run1,campfire-run2,campfire-r4-run1,river-run1})이 있으면 새 판정기로 재판정해 SKIP 아닌 결과를 낸다.
상태: 2026-10-25 작성, 실제 PIE 스윕은 돌리지 않는다(정본 JSON sha256 전후 비교 포함). 2026-10-30 CAL-0 B0: 공용 가짜 한 구역(F-WORLD 구조체·필드 표와 현재 상태 대비 ChangedFieldCount·모양 재적용, F-TICK 가짜 시계·슬레이트 틱 콜백·시간 배율, F-UNREAL 재질·나이아가라·데칼·구름·후처리·식생 게터), 모닥불·강 계획은 f02965cb 고정 사본 Tools/FluidFX/calib/fixtures/*.head.json, 묶음 시험은 check_cal0_<묶음>() 를 정본 sha 단언 직전 run_check 한 줄로 부르고 patched·run_check 가 패치를 되돌린다.
"""
import contextlib
import copy
import io
import json
import math
import re
import shutil
import subprocess
import sys
import time
import traceback
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
import pie_probe_fluid_water as probe  # noqa: E402
import prereg_lock  # noqa: E402

TEST_DIR = W / "Saved" / "FluidFX" / "calib" / "offline-test"
ACCEPTANCE_DIR = W / "Saved" / "FluidFX" / "calib"
CANONICAL = fluid_tuning_patch.canonical_tuning_path()
CANONICAL_LOOK = fluid_tuning_patch.canonical_look_path()
CAMPFIRE_PLAN = "Tools/FluidFX/calib/fixtures/campfire_vorticity.head.json"
RIVER_PLAN = "Tools/FluidFX/calib/fixtures/river_vorticity.head.json"
DEFAULT_WORLD_FIELDS = (("TDFluidSimulationConfig", "VorticityStrength"),)
FIELD_HOLDERS = {"TDFluidSimulationConfig": ("live_component", "simulation_config"), "TDFluidSourceEmitterSettings": (None, "source_emitter")}
TICK_SECONDS = 1.0 / 60.0
MISSING = object()
RESULTS = []
PROBE_WINDOW_SCALES = []


def check(name, condition, detail=""):
    RESULTS.append((name, bool(condition)))
    print(("PASS " if condition else "FAIL ") + name + ("" if condition else f"  {detail}"))


@contextlib.contextmanager
def patched(target, **replacements):
    saved = {name: vars(target).get(name, MISSING) for name in replacements}
    for name, value in replacements.items():
        setattr(target, name, value)
    try:
        yield target
    finally:
        for name, value in saved.items():
            if value is not MISSING:
                setattr(target, name, value)
            elif name in vars(target):
                delattr(target, name)


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


class FakeClock:
    def __init__(self):
        self.real_seconds = 1.0
        self.game_seconds = 1.0
        self.time_dilation = 1.0
        self.callbacks = {}
        self.last_handle = 0
        self.is_ticking = False

    def register(self, callback):
        self.last_handle += 1
        self.callbacks[self.last_handle] = callback
        return self.last_handle

    def unregister(self, handle):
        self.callbacks.pop(handle, None)

    def tick(self):
        self.real_seconds += TICK_SECONDS
        self.game_seconds += TICK_SECONDS * self.time_dilation
        if self.is_ticking:
            return
        self.is_ticking = True
        try:
            for handle in list(self.callbacks):
                if handle in self.callbacks:
                    self.callbacks[handle](TICK_SECONDS)
        finally:
            self.is_ticking = False

    def sleep(self, seconds):
        for _ in range(max(0, math.ceil(seconds / TICK_SECONDS - 1e-9))):
            self.tick()

    def monotonic(self):
        return self.real_seconds


CLOCK = FakeClock()
unreal = FakeModule("unreal")
unreal.EnumBase = FakeEnum
unreal.Object = type("Object", (Props,), {})


class FakeLinearColor:
    def __init__(self, r=0.0, g=0.0, b=0.0, a=1.0):
        self.r, self.g, self.b, self.a = r, g, b, a

    def __eq__(self, other):
        return isinstance(other, FakeLinearColor) and (self.r, self.g, self.b, self.a) == (other.r, other.g, other.b, other.a)


unreal.LinearColor = FakeLinearColor
unreal.Vector2D = type("Vector2D", (), {})
unreal.Vector = type("Vector", (), {})


class FakeAsset(unreal.Object):
    def __init__(self, path, **values):
        super().__init__(**values)
        self.path = path

    def get_name(self):
        return self.path.rsplit("/", 1)[-1]

    def get_path_name(self):
        return f"{self.path}.{self.get_name()}"


class FakeMaterialInterface(FakeAsset):
    def __init__(self, path, scalars=None, vectors=None, parent=None):
        super().__init__(path, parent=parent)
        self.scalars = dict(scalars or {})
        self.vectors = dict(vectors or {})

    def get_base_material(self):
        material = self
        while material.parent is not None:
            material = material.parent
        return material

    def parameter_value(self, table_name, name):
        material = self
        while material is not None:
            table = getattr(material, table_name)
            if str(name) in table:
                return table[str(name)]
            material = material.parent
        return None


class FakeMaterial(FakeMaterialInterface):
    pass


class FakeMaterialInstance(FakeMaterialInterface):
    pass


class FakeMaterialInstanceConstant(FakeMaterialInstance):
    pass


class FakeMaterialInstanceDynamic(FakeMaterialInstance):
    created_count = 0

    def __init__(self, parent):
        FakeMaterialInstanceDynamic.created_count += 1
        super().__init__(f"/Engine/Transient/MaterialInstanceDynamic_{FakeMaterialInstanceDynamic.created_count}", parent=parent)

    def set_scalar_parameter_value(self, name, value):
        self.scalars[str(name)] = value

    def get_scalar_parameter_value(self, name):
        value = self.parameter_value("scalars", name)
        return 0.0 if value is None else value

    def set_vector_parameter_value(self, name, value):
        self.vectors[str(name)] = value

    def get_vector_parameter_value(self, name):
        value = self.parameter_value("vectors", name)
        return FakeLinearColor() if value is None else value


class FakeMaterialEditingLibrary:
    @staticmethod
    def get_scalar_parameter_names(material):
        return sorted(material.get_base_material().scalars)

    @staticmethod
    def get_vector_parameter_names(material):
        return sorted(material.get_base_material().vectors)

    @staticmethod
    def get_material_instance_scalar_parameter_value(instance, parameter_name, association=None):
        value = instance.parameter_value("scalars", parameter_name)
        return 0.0 if value is None else value

    @staticmethod
    def get_material_instance_vector_parameter_value(instance, parameter_name, association=None):
        value = instance.parameter_value("vectors", parameter_name)
        return FakeLinearColor() if value is None else value


class FakeNiagaraSystem(FakeAsset):
    pass


class FakeComponent(unreal.Object):
    def __init__(self, name, **values):
        super().__init__(**values)
        self.name = name

    def get_name(self):
        return self.name


class FakePrimitiveComponent(FakeComponent):
    def __init__(self, name, materials=()):
        super().__init__(name)
        self.materials = list(materials)

    def get_num_materials(self):
        return len(self.materials)

    def get_material(self, element_index):
        return self.materials[element_index] if 0 <= element_index < len(self.materials) else None

    def set_material(self, element_index, material):
        if 0 <= element_index < len(self.materials):
            self.materials[element_index] = material

    def create_dynamic_material_instance(self, element_index, source_material=None, optional_name="None"):
        if source_material is not None:
            self.set_material(element_index, source_material)
        current = self.get_material(element_index)
        if isinstance(current, FakeMaterialInstanceDynamic):
            return current
        instance = FakeMaterialInstanceDynamic(current)
        self.set_material(element_index, instance)
        return instance


class FakeStaticMeshComponent(FakePrimitiveComponent):
    pass


class FakeNiagaraComponent(FakePrimitiveComponent):
    def __init__(self, name, asset):
        super().__init__(name)
        self.asset = asset
        self.variables = {}
        self.reinitialize_count = 0

    def get_asset(self):
        return self.asset

    def set_variable_float(self, name, value):
        self.variables[str(name)] = value

    set_variable_int = set_variable_bool = set_variable_vec2 = set_variable_vec3 = set_variable_vec4 = set_variable_linear_color = set_variable_float

    def reinitialize_system(self):
        self.reinitialize_count += 1


class FakeDecalComponent(FakeComponent):
    def __init__(self, name, decal_material=None):
        super().__init__(name, decal_material=decal_material)

    def get_decal_material(self):
        return self.decal_material

    def set_decal_material(self, material):
        self.decal_material = material

    def create_dynamic_material_instance(self):
        instance = FakeMaterialInstanceDynamic(self.decal_material)
        self.decal_material = instance
        return instance


class FakeVolumetricCloudComponent(FakeComponent):
    def __init__(self, name, material=None):
        super().__init__(name, material=material)

    def set_material(self, material):
        self.material = material


unreal.MaterialInterface = FakeMaterialInterface
unreal.Material = FakeMaterial
unreal.MaterialInstance = FakeMaterialInstance
unreal.MaterialInstanceConstant = FakeMaterialInstanceConstant
unreal.MaterialInstanceDynamic = FakeMaterialInstanceDynamic
unreal.MaterialEditingLibrary = FakeMaterialEditingLibrary
unreal.NiagaraSystem = FakeNiagaraSystem
unreal.ActorComponent = unreal.SceneComponent = FakeComponent
unreal.PrimitiveComponent = FakePrimitiveComponent
unreal.StaticMeshComponent = FakeStaticMeshComponent
unreal.NiagaraComponent = FakeNiagaraComponent
unreal.DecalComponent = FakeDecalComponent
unreal.VolumetricCloudComponent = FakeVolumetricCloudComponent


class FakeActor(unreal.Actor, Props):
    def __init__(self, label, components=(), **values):
        super().__init__(**values)
        self.label = label
        self.components = list(components)

    def get_actor_label(self):
        return self.label

    def get_components_by_class(self, cls):
        return [component for component in self.components if isinstance(component, cls)]

    def get_component_by_class(self, cls):
        return next(iter(self.get_components_by_class(cls)), None)


class FakePostProcessVolume(FakeActor):
    def __init__(self, label, blendables=()):
        super().__init__(label, settings=Props(weighted_blendables=Props(array=[Props(weight=1.0, object=material) for material in blendables])))


class TDFluidCloudLayersActor(FakeActor):
    def __init__(self, label, cloud_material):
        super().__init__(label, components=[FakeVolumetricCloudComponent("VolumetricCloud", cloud_material)])


class TDFluidFoliageFlowActor(FakeActor):
    def __init__(self, label, flow_copy_material):
        super().__init__(label, flow_copy_material_instance=flow_copy_material)

    def get_flow_copy_material_instance(self):
        return self.flow_copy_material_instance


class TDFluidSurfaceOutputActor(FakeActor):
    def __init__(self, label, decal_material):
        decal = FakeDecalComponent("OutputDecal", decal_material)
        super().__init__(label, components=[decal], output_decal=decal)


unreal.PostProcessVolume = FakePostProcessVolume
unreal.TDFluidCloudLayersActor = TDFluidCloudLayersActor
unreal.TDFluidFoliageFlowActor = TDFluidFoliageFlowActor
unreal.TDFluidSurfaceOutputActor = TDFluidSurfaceOutputActor


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


def kind_json_name(kind_name):
    return "".join(part.capitalize() for part in kind_name.split("_"))


def snake_name(field_name):
    stripped = field_name[1:] if len(field_name) > 1 and field_name[0] == "b" and field_name[1].isupper() else field_name
    return re.sub(r"(?<!^)(?=[A-Z])", "_", stripped).lower()


def field_holder(actor, struct_name):
    component_name, property_name = FIELD_HOLDERS[struct_name]
    holder = getattr(actor, component_name) if component_name else actor
    return getattr(holder, property_name) if property_name else holder


class FakeLive(unreal.Object):
    def __init__(self, world, preset):
        super().__init__(active_preset=preset, simulation_config=copy.copy(preset.simulation_config))
        self.world = world
        self.owner_actor = None
        self.init_count = 0

    def init_system_state_from_preset(self, preset):
        self.init_count += 1
        self.simulation_config = copy.copy(preset.simulation_config)
        if not uses_copies(preset):
            self.world.apply_effective(self.owner_actor)


class TDFluidLiveActor(FakeActor):
    def __init__(self, world, label, preset):
        super().__init__(label, live_component=FakeLive(world, preset), source_emitter=Props(radius_cm=26.0), ember_component=None, internal_surface_mesh=None, volume_mesh_component=None)
        self.live_component.owner_actor = self


class TDFluidFireActor(TDFluidLiveActor):
    pass


unreal.TDFluidLiveActor = TDFluidLiveActor
unreal.TDFluidFireActor = TDFluidFireActor


class FakeWorld:
    def __init__(self, fields=DEFAULT_WORLD_FIELDS):
        self.actors = []
        self.fields = list(fields)
        self.table = json.loads(CANONICAL.read_text(encoding="utf-8"))
        self.look_table = json.loads(CANONICAL_LOOK.read_text(encoding="utf-8"))
        self.generation = 1
        self.pending = None
        self.source = CANONICAL.as_posix()
        self.look_source = CANONICAL_LOOK.as_posix()
        self.report = {"table_generation": 1, "tuning_source_path": self.source, "look_source_path": self.look_source, "reapplied_actor_count": 0,
                       "state_reset_actor_count": 0, "changed_field_count": 0, "live_material_count": 0, "bake_only_changed_count": 0}
        self.report_calls = 0
        self.assets = {}
        self.cpp_materials = []
        self.cpp_reapplies_look = True
        self.shot_count = 0

    def live_actors(self):
        return [actor for actor in self.actors if isinstance(actor, TDFluidLiveActor)]

    def effective_value(self, kind_name, struct_name, field_name):
        kind = kind_json_name(kind_name)
        entries = self.table["Kinds"].get(kind, {}).get(struct_name, {})
        if field_name in entries:
            return entries[field_name]
        for group in self.table["Groups"].values():
            if kind in group["Kinds"] and field_name in group.get(struct_name, {}):
                return group[struct_name][field_name]
        return self.table["StructDefaults"].get(struct_name, {}).get(field_name)

    def apply_effective(self, actor):
        changed = 0
        kind_name = actor.live_component.active_preset.preset_kind.name
        for struct_name, field_name in self.fields:
            value = self.effective_value(kind_name, struct_name, field_name)
            holder = field_holder(actor, struct_name)
            attribute = snake_name(field_name)
            if value is None or getattr(holder, attribute, None) == value:
                continue
            setattr(holder, attribute, value)
            changed += 1
        return changed

    def apply_look_to_cpp_materials(self):
        if not self.cpp_reapplies_look:
            return 0
        applied = 0
        for material in self.cpp_materials:
            entry = self.look_table.get("Materials", {}).get(material.parent.path if material.parent is not None else "")
            if entry is None:
                continue
            for name, value in entry.get("Scalars", {}).items():
                material.set_scalar_parameter_value(name, float(value))
            for name, value in entry.get("Vectors", {}).items():
                material.set_vector_parameter_value(name, FakeLinearColor(*value))
            applied += 1
        return applied

    def add_asset(self, asset):
        self.assets[asset.path] = asset
        return asset

    def add_cpp_material(self, parent):
        material = FakeMaterialInstanceDynamic(parent)
        self.cpp_materials.append(material)
        return material

    def tick_reapply(self):
        live_actors = self.live_actors()
        changed = sum(self.apply_effective(actor) for actor in live_actors if not uses_copies(actor.live_component.active_preset))
        self.report = dict(self.report, table_generation=self.pending, tuning_source_path=self.source, look_source_path=self.look_source, changed_field_count=changed,
                           reapplied_actor_count=len(live_actors), live_material_count=self.apply_look_to_cpp_materials())
        self.pending = None


WORLD = FakeWorld()


def is_outside_saved(path_text):
    return bool(path_text) and (W / "Saved").resolve() not in Path(path_text).resolve().parents


class FakeTuningSubsystem:
    def reload_preset_tuning_from_paths(self, tuning, look):
        if is_outside_saved(tuning) or is_outside_saved(look):
            self.errors = ["refused"]
            return 1
        self.errors = []
        tuning_path = Path(tuning) if tuning else CANONICAL
        look_path = Path(look) if look else CANONICAL_LOOK
        WORLD.table = json.loads(tuning_path.read_text(encoding="utf-8"))
        WORLD.look_table = json.loads(look_path.read_text(encoding="utf-8"))
        WORLD.source = tuning_path.as_posix()
        WORLD.look_source = look_path.as_posix()
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
        return WORLD.look_source

    def is_using_override_paths(self):
        return WORLD.source != CANONICAL.as_posix() or WORLD.look_source != CANONICAL_LOOK.as_posix()


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
unreal.load_asset = lambda path: WORLD.assets.get(str(path).split(".")[0])
unreal.EditorAssetLibrary = Props(load_asset=unreal.load_asset)
unreal.register_slate_post_tick_callback = CLOCK.register
unreal.unregister_slate_post_tick_callback = CLOCK.unregister


def all_actors(world, cls):
    return [actor for actor in world.actors if isinstance(cls, type) and isinstance(actor, cls)]


def console(world, command, controller=None):
    if command.startswith("HighResShot"):
        path = Path(command.split("filename=", 1)[1])
        path.parent.mkdir(parents=True, exist_ok=True)
        live_actors = WORLD.live_actors()
        value = live_actors[0].live_component.simulation_config.vorticity_strength if live_actors else 1.0
        WORLD.shot_count += 1
        image = Image.new("RGB", (1280, 720), (20, 24, 40))
        draw = ImageDraw.Draw(image)
        half = int(60 + 40 * value)
        draw.rectangle((850 - half, 300 - half, 850 + half, 300 + half // 2), fill=(255, 150 + (WORLD.shot_count % 2), 40))
        draw.rectangle((850 - half // 3, 260 - half // 3, 850 + half // 3, 260), fill=(255, 230, 120))
        image.save(path)


unreal.GameplayStatics = Props(get_all_actors_of_class=all_actors, get_time_seconds=lambda world: CLOCK.game_seconds, get_real_time_seconds=lambda world: CLOCK.real_seconds,
                               get_world_delta_seconds=lambda world: TICK_SECONDS * CLOCK.time_dilation, get_global_time_dilation=lambda world: CLOCK.time_dilation,
                               set_global_time_dilation=lambda world, value: setattr(CLOCK, "time_dilation", value), get_player_controller=lambda world, index: None)
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
time.sleep = CLOCK.sleep
time.monotonic = CLOCK.monotonic
time.perf_counter = CLOCK.monotonic


def fake_probe_run(points, layout, contract, delay, backend, window_scale=1.0):
    PROBE_WINDOW_SCALES.append(window_scale)
    vorticity = WORLD.live_actors()[0].live_component.simulation_config.vorticity_strength
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


def reset_world(actors, fields=DEFAULT_WORLD_FIELDS):
    global WORLD
    WORLD = FakeWorld(fields)
    WORLD.actors = [cls(WORLD, label, preset) for cls, label, preset in actors]
    CLOCK.time_dilation = 1.0


def run_lookdev_in_process(spec_path, level, output_dir):
    saved_argv = sys.argv
    sys.argv = ["pie_lookdev_fluid.py", "--variants", str(spec_path), "--level", level, "--output-dir", str(output_dir), "--stop-pie"]
    try:
        return lookdev.main()
    finally:
        sys.argv = saved_argv


calib_sweep.run_lookdev = run_lookdev_in_process


def run_check(check_function):
    modules = {id(module): module for module in (sys.modules[__name__], unreal, time, lookdev, lookdev.pie_check_fluid, lookdev.uemcp, pie_showcase_fluid,
                                                 calib_judge, calib_sweep, fluid_tuning_patch, prereg_lock, probe, probe.pie_check_fluid, probe.uemcp)}
    snapshots = [(module, dict(vars(module))) for module in modules.values()]
    callbacks = dict(CLOCK.callbacks)
    saved_argv = list(sys.argv)
    try:
        check_function()
    except Exception:
        check(f"{check_function.__name__} 예외 없이 끝남", False, traceback.format_exc()[-800:])
    finally:
        for module, snapshot in snapshots:
            namespace = vars(module)
            for name in set(namespace) - set(snapshot):
                del namespace[name]
            namespace.update(snapshot)
        CLOCK.callbacks = callbacks
        CLOCK.time_dilation = 1.0
        sys.argv = saved_argv


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
recorded = []
leg_sleeps = {}
with patched(probe, log_size=lambda: 0, read_log_since=lambda offset: "", build_flow_points=lambda points: {}, build_rock_points=lambda rock: {}, build_submerged_points=lambda rock: {}), \
        patched(time, sleep=lambda seconds: (recorded.append(seconds), CLOCK.sleep(seconds))):
    for leg_name, run_leg in legs.items():
        for scale in (1.0, 0.5):
            recorded.clear()
            try:
                run_leg(scale)
            except Exception as error:
                leg_sleeps[(leg_name, scale)] = repr(error)
                continue
            leg_sleeps[(leg_name, scale)] = list(recorded)
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


def check_cal0_b0():
    output_dir = TEST_DIR / "cal0-b0"
    output_dir.mkdir(parents=True, exist_ok=True)
    reset_world([(TDFluidFireActor, "TDGen_CampfireFire", FakePreset("CAMPFIRE"))], fields=DEFAULT_WORLD_FIELDS + (("TDFluidSourceEmitterSettings", "RadiusCm"),))
    applied = fake_editor_action("lookdev_apply", apply={"schedule": {"times": [0.5, 1.0, 1.5], "reference": "now", "prefix": "b0"}}, tag="b0", output=output_dir.as_posix(), size="1280x720")
    status = lookdev.wait_for_schedule(Path(applied["schedule"]["status_path"]), 30.0)
    shots = status.get("shots", [])
    check("B0 F-TICK: 예약 촬영 3장이 가짜 시계로 끝남·게임 시간 순서·콜백 해제", status.get("done") and status.get("error") is None and not status.get("outer_timeout") and len(shots) == 3
          and all(shot["saved"] and shot["captured_game_time"] >= shot["time"] for shot in shots) and not CLOCK.callbacks, status)

    tuning = load_json(CANONICAL)
    tuning["Kinds"]["Campfire"]["TDFluidSimulationConfig"]["VorticityStrength"] = 2.0
    tuning["Kinds"]["Campfire"].setdefault("TDFluidSourceEmitterSettings", {})["RadiusCm"] = 33.0
    tuning_path = output_dir / "tuning.json"
    tuning_path.write_text(json.dumps(tuning), encoding="utf-8")
    request = {"tuning": str(tuning_path), "look": "", "reset": "keep", "relative_to_base": False, "read_back": [{"struct": "TDFluidSourceEmitterSettings", "field": "RadiusCm", "kinds": ["Campfire"]}]}
    first, second = lookdev.run_tuning_reload(request), lookdev.run_tuning_reload(request)
    check("B0 F-WORLD: ChangedFieldCount 는 현재 상태 대비(첫 적재 2·같은 사본 다시 0)·다시 읽기가 적용 값",
          first["report"]["changed_field_count"] == 2 and second["report"]["changed_field_count"] == 0 and second["read_back"][0]["actors"]["TDGen_CampfireFire"]["value"] == 33.0, (first.get("report"), second.get("read_back")))

    base = FakeMaterial("/Game/CAL0/M_Glow", scalars={"Glow": 1.0}, vectors={"Tint": FakeLinearColor(1.0, 1.0, 1.0)})
    instance = WORLD.add_asset(FakeMaterialInstanceConstant("/Game/CAL0/MI_Glow", scalars={"Glow": 2.0}, parent=base))
    child = FakeMaterialInstanceConstant("/Game/CAL0/MI_GlowChild", parent=instance)
    mesh = FakeStaticMeshComponent("Mesh", [instance])
    slot_material = mesh.create_dynamic_material_instance(0)
    slot_material.set_scalar_parameter_value("Glow", 5.0)
    cloud = TDFluidCloudLayersActor("Clouds", WORLD.add_cpp_material(instance))
    foliage = TDFluidFoliageFlowActor("Foliage", WORLD.add_cpp_material(instance))
    grandchild_owned = WORLD.add_cpp_material(child)
    blood = TDFluidSurfaceOutputActor("Blood", instance)
    decal_material = blood.output_decal.create_dynamic_material_instance()
    underwater = FakePostProcessVolume("Underwater", [WORLD.add_cpp_material(instance)])
    rain = FakeNiagaraComponent("Rain", WORLD.add_asset(FakeNiagaraSystem("/Game/CAL0/NS_Rain")))
    rain.set_variable_float("User.Rate", 3.0)
    rain.reinitialize_system()
    WORLD.actors += [cloud, foliage, blood, underwater]
    check("B0 F-UNREAL: 슬롯 MID 교체·MI 자산 값 불변·load_asset·데칼 MID·구름·식생 게터·후처리 blendable·나이아가라 변수",
          mesh.get_material(0) is slot_material and slot_material.get_editor_property("parent") is instance and slot_material.get_scalar_parameter_value("Glow") == 5.0
          and unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(instance, "Glow") == 2.0 and unreal.load_asset("/Game/CAL0/MI_Glow.MI_Glow") is instance
          and blood.output_decal.get_decal_material() is decal_material and decal_material.parent is instance
          and cloud.get_component_by_class(unreal.VolumetricCloudComponent).get_editor_property("material").parent is instance
          and foliage.get_flow_copy_material_instance().parent is instance and all_actors(WORLD, unreal.PostProcessVolume) == [underwater]
          and underwater.get_editor_property("settings").get_editor_property("weighted_blendables").get_editor_property("array")[0].get_editor_property("object").parent is instance
          and rain.get_asset().get_path_name() == "/Game/CAL0/NS_Rain.NS_Rain" and rain.variables == {"User.Rate": 3.0} and rain.reinitialize_count == 1)

    look = load_json(CANONICAL_LOOK)
    look["Materials"]["/Game/CAL0/MI_Glow"] = {"Parent": "/Game/CAL0/M_Glow", "Scalars": {"Glow": 7.0}, "Switches": {}, "Vectors": {"Tint": [0.5, 0.25, 0.0, 1.0]}}
    look_path = output_dir / "look.json"
    look_path.write_text(json.dumps(look), encoding="utf-8")
    reloaded = lookdev.run_tuning_reload({"tuning": "", "look": str(look_path), "reset": "keep", "relative_to_base": False})
    owned = [cloud.components[0].material, foliage.get_flow_copy_material_instance(), underwater.settings.weighted_blendables.array[0].object]
    check("B0 F-WORLD: 모양 사본 적재 때 C++ 소유 MID 는 직접 부모 항목만 다시 적용(조부모 대상은 그대로)",
          reloaded["report"]["live_material_count"] == 3 and Path(reloaded["report"]["look_source_path"]).resolve() == look_path.resolve()
          and all(material.get_scalar_parameter_value("Glow") == 7.0 and material.get_vector_parameter_value("Tint") == FakeLinearColor(0.5, 0.25, 0.0, 1.0) for material in owned)
          and grandchild_owned.get_scalar_parameter_value("Glow") == 2.0 and slot_material.get_scalar_parameter_value("Glow") == 5.0, reloaded.get("report"))

    with patched(lookdev, FRAME_FILE_WAIT_SECONDS=9.0, CAL0_ADDED_NAME=1):
        patched_values = (lookdev.FRAME_FILE_WAIT_SECONDS, lookdev.CAL0_ADDED_NAME)
    check("B0 patched: 바꾼 속성은 원래 값으로, 없던 속성은 지움", patched_values == (9.0, 1) and lookdev.FRAME_FILE_WAIT_SECONDS == 0.5 and not hasattr(lookdev, "CAL0_ADDED_NAME"))


def check_cal0_patch():
    output_dir = TEST_DIR / "cal0-b1"
    output_dir.mkdir(parents=True, exist_ok=True)
    patch = fluid_tuning_patch
    look_module = patch.fluid_material_look
    schema = patch.check_fluid_preset_data.load_schema(W)
    canonical_bytes = CANONICAL.read_bytes()
    canonical_look = load_json(CANONICAL_LOOK)
    river = "/Game/FluidFX/Materials/MI_TD_FluidWater_River"
    river_scalar = f"Look:Materials.{river}.Scalars.CausticsStrength"
    missing_scalar = f"Look:Materials.{river}.Scalars.CAL0MissingScalar"
    debris_package = "/Game/FluidFX/Niagara/NS_TD_FluidWaterDebris"
    debris_count = f"Look:NiagaraSystems.{debris_package}.Ints.User.TDDebrisCount"
    cloud_global = "Globals.TDFluidCloudSceneSettings.ViewSampleCountScaleFar"
    vorticity = "Kinds.Campfire.TDFluidSimulationConfig.VorticityStrength"

    def cli(*arguments):
        return python("Tools/FluidFX/fluid_tuning_patch.py", *arguments)

    def rejection(call):
        try:
            call()
        except patch.InputRejected as error:
            return str(error)
        return None

    def sha(path):
        return patch.file_sha256(path)

    t21 = patch.write_variant([{"json_path": river_scalar, "value": 0.6}], output_dir / "t21", root=W, schema=schema)
    t21_look = load_json(output_dir / "t21" / "TDFluidMaterialLook.json")
    check("T21 Look 재질 Scalars 변형: 조절 사본 = 정본 바이트, 모양 사본만 값 변경, 검사 0",
          (output_dir / "t21" / "TDFluidPresetTuning.json").read_bytes() == canonical_bytes and t21_look["Materials"][river]["Scalars"]["CausticsStrength"] == 0.6
          and t21_look["Materials"][river]["Scalars"]["CausticsMaxDepthCm"] == canonical_look["Materials"][river]["Scalars"]["CausticsMaxDepthCm"]
          and t21["assignments"][0]["scope"] == "look" and t21["assignments"][0]["base_value"] == 0.45 and "errors=0" in t21["checker"], t21)

    patch.write_variant([{"json_path": debris_count, "value": 120}], output_dir / "t22", root=W, schema=schema)
    check("T22 Look NS Ints.User.TDDebrisCount(이름에 점): 4부분 파싱·모양 사본 반영",
          patch.parse_json_path(debris_count) == ("Look:NiagaraSystems", debris_package, "Ints", "User.TDDebrisCount")
          and load_json(output_dir / "t22" / "TDFluidMaterialLook.json")["NiagaraSystems"][debris_package]["Ints"]["User.TDDebrisCount"] == 120)

    bad_paths = {"switch": f"Look:Materials.{river}.Switches.bRibbonLocalCaustics", "dot": "Look:Materials./Game/FluidFX/MI_TD.Fluid.Scalars.X", "game": "Look:Materials./Engine/MI_X.Scalars.X"}
    rejected = {name: cli("variant", "--out-dir", str(output_dir / "t23"), "--set", f"{path}=1.0") for name, path in bad_paths.items()}
    check("T23 Look 거부: Switches·패키지에 '.'·/Game/ 아님 → 각각 종료 2와 사유 문구",
          rejected["switch"][0] == 2 and "Switches" in rejected["switch"][1] and rejected["dot"][0] == 2 and "'.'" in rejected["dot"][1]
          and rejected["game"][0] == 2 and "/Game/" in rejected["game"][1], {name: (code, text[-200:]) for name, (code, text) in rejected.items()})

    patch.write_variant([], output_dir / "t24-v00", root=W, schema=schema)
    t24 = patch.write_variant([{"json_path": missing_scalar, "value": 2.0, "base": 1.0}], output_dir / "t24-v01", root=W, schema=schema)
    no_base = cli("variant", "--out-dir", str(output_dir / "t24-v02"), "--set", f"{missing_scalar}=2.0")
    wrong_base = rejection(lambda: patch.describe_assignment(load_json(CANONICAL), schema, {"json_path": river_scalar, "value": 0.6, "base": 0.5}))
    check("T24 정본에 없는 Look 키: base 있으면 대조 사본에 키 없음·변형 사본에 키 / base 없음 → 종료 2 / base ≠ 정본 → 거부",
          "CAL0MissingScalar" not in load_json(output_dir / "t24-v00" / "TDFluidMaterialLook.json")["Materials"][river]["Scalars"]
          and load_json(output_dir / "t24-v01" / "TDFluidMaterialLook.json")["Materials"][river]["Scalars"]["CAL0MissingScalar"] == 2.0
          and t24["assignments"][0]["added_key"] and t24["assignments"][0]["base_value"] == 1.0
          and no_base[0] == 2 and "base" in no_base[1] and wrong_base and "정본 값" in wrong_base, (no_base[0], no_base[1][-200:], wrong_base))

    t53 = patch.write_variant([{"json_path": cloud_global, "value": 3.0}], output_dir / "t53", root=W, schema=schema)
    check("T53 Globals.TDFluidCloudSceneSettings 변형: 사본 Globals 값·영향 전역·모양 사본 = 정본 바이트",
          load_json(output_dir / "t53" / "TDFluidPresetTuning.json")["Globals"]["TDFluidCloudSceneSettings"]["ViewSampleCountScaleFar"] == 3.0
          and t53["assignments"][0]["scope"] == "global" and t53["assignments"][0]["owner"] == "Globals.TDFluidCloudSceneSettings" and t53["assignments"][0]["base_value"] == 2.0
          and (output_dir / "t53" / "TDFluidMaterialLook.json").read_bytes() == CANONICAL_LOOK.read_bytes(), t53["assignments"])

    unknown_global = cli("variant", "--out-dir", str(output_dir / "t54"), "--set", "Globals.TDFluidNoSuchSettings.Value=1.0")
    kind_scoped_global = cli("variant", "--out-dir", str(output_dir / "t54"), "--set", "Globals.TDFluidSimulationConfig.VorticityStrength=1.0")
    check("T54 정본에 없는 Globals 구조체·종류 범위 구조체 → 종료 2",
          unknown_global[0] == 2 and "TDFluidNoSuchSettings" in unknown_global[1] and kind_scoped_global[0] == 2 and "Globals 등록 구조체" in kind_scoped_global[1],
          (unknown_global, kind_scoped_global))

    temp_root = output_dir / "root"
    for relative in (patch.check_fluid_preset_data.TUNING_RELATIVE_PATH, patch.check_fluid_preset_data.LOOK_RELATIVE_PATH, patch.check_fluid_preset_data.GAME_INI_RELATIVE_PATH, patch.CHECKER_RELATIVE_PATH):
        temp_root.joinpath(*relative).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(W.joinpath(*relative), temp_root.joinpath(*relative))
    shutil.copytree(W / "Source" / "TDGame" / "FluidFX", temp_root / "Source" / "TDGame" / "FluidFX", ignore=shutil.ignore_patterns("Tests"))
    for _, soft_path in patch.check_fluid_preset_data.collect_tuning_soft_paths(W):
        package_file = patch.check_fluid_preset_data.find_package_file(W, soft_path.split(".", 1)[0])
        placeholder = temp_root / package_file.relative_to(W)
        placeholder.parent.mkdir(parents=True, exist_ok=True)
        placeholder.write_bytes(b"")
    temp_tuning = patch.canonical_tuning_path(temp_root)
    temp_look = patch.canonical_look_path(temp_root)
    temp_shas = (sha(temp_tuning), sha(temp_look))
    t55 = cli("--root", str(temp_root), "write-canonical", "--backup-dir", str(temp_root / "Saved" / "t55"), "--set", f"{cloud_global}=3.0")
    check("T55 write-canonical Globals 경로 --acknowledge-header 없음 → 종료 2·임시 정본 불변",
          t55[0] == 2 and "--acknowledge-header" in t55[1] and (sha(temp_tuning), sha(temp_look)) == temp_shas, t55)

    t96 = cli("--root", str(temp_root), "write-canonical", "--backup-dir", str(temp_root / "Saved" / "t96"), "--acknowledge-header", "--set", "StructDefaults.TDFluidSimulationConfig.VorticityStrength=1.25")
    t96_variant = rejection(lambda: patch.check_canonical_writable([{"json_path": "StructDefaults.TDFluidSimulationConfig.GridResolution", "value": 128}], True))
    check("T96(B1) StructDefaults.TDFluidSimulationConfig write-canonical → --acknowledge-header 있어도 종료 2·임시 정본 불변",
          t96[0] == 2 and "골든 스냅숏" in t96[1] and t96_variant and (sha(temp_tuning), sha(temp_look)) == temp_shas, t96)

    t20_overlay = temp_root / "Saved" / "overlay.json"
    patch.write_overlay(t20_overlay, dict(patch.empty_overlay(), assignments=[{"json_path": vorticity, "value": 1.75, "split_group": False, "kind": "knob", "from": {}},
                                                                               {"json_path": missing_scalar, "value": 2.0, "base": 1.0, "split_group": False, "kind": "knob", "from": {}}]))
    t20 = cli("--root", str(temp_root), "write-canonical", "--backup-dir", str(temp_root / "Saved" / "t20"), "--from-overlay", str(t20_overlay))
    written_tuning, written_look = load_json(temp_tuning), load_json(temp_look)
    look_check = python("Tools/FluidFX/fluid_material_look.py", "check", str(temp_look))[0]
    backup_shas = (sha(temp_root / "Saved" / "t20" / "TDFluidPresetTuning.json"), sha(temp_root / "Saved" / "t20" / "TDFluidMaterialLook.json"))
    restored = cli("--root", str(temp_root), "restore", "--backup-dir", str(temp_root / "Saved" / "t20"))
    check("T20 write-canonical --from-overlay(임시 루트, Kinds 1·Look 1): 두 정본 백업·쓰기·검사 0, restore 로 원래 sha",
          t20[0] == 0 and written_tuning["Kinds"]["Campfire"]["TDFluidSimulationConfig"]["VorticityStrength"] == 1.75
          and written_look["Materials"][river]["Scalars"]["CAL0MissingScalar"] == 2.0 and look_check == 0 and backup_shas == temp_shas
          and restored[0] == 0 and (sha(temp_tuning), sha(temp_look)) == temp_shas, (t20[0], t20[1][-400:], restored))

    runtime_names = look_module.load_runtime_user_names()
    dump = copy.deepcopy(canonical_look)
    dump["NiagaraSystems"][debris_package]["Vectors2"]["User.SimExtentsCm"] = [1.0, 2.0]
    dump["NiagaraSystems"][debris_package]["Floats"]["User.VelocityReferenceCmPerSec"] = 3.0
    dump_path = output_dir / "t28-dump.json"
    dump_path.write_text(json.dumps(dump), encoding="utf-8")
    compared = python("Tools/FluidFX/fluid_material_look.py", "compare", str(dump_path), str(CANONICAL_LOOK))
    real_change = copy.deepcopy(dump)
    real_change["Materials"][river]["Scalars"]["CausticsStrength"] = 0.9
    check("T28 compare: 런타임 User 변수 차이는 0(제외 목록)·실제 모양 차이는 셈",
          "User.SimExtentsCm" in runtime_names and look_module.compare_looks(dump, canonical_look, runtime_names) == [] and len(look_module.compare_looks(dump, canonical_look)) == 2
          and len(look_module.compare_looks(real_change, canonical_look, runtime_names)) == 1 and compared[0] == 0 and '"excluded_runtime_user_differences": 2' in compared[1], compared)

    def make_judge(name, assignments, plan_extra=None, decision=True, overlay_sha=None, version=2, plan_path=None):
        if plan_path is None:
            plan_path = output_dir / f"{name}-plan.json"
            plan_path.write_text(json.dumps(dict({"id": f"cal0-b1-{name}", "scenes": [{"name": "s", "level": "/Game/CAL0/L_B1"}], "params": []}, **(plan_extra or {}))), encoding="utf-8")
        tool_path = output_dir / f"{name}-tool.py"
        tool_path.write_text("TOOL = 1\n", encoding="utf-8")
        lock_path = output_dir / f"{name}-lock.json"
        prereg_lock.create_lock([str(plan_path), str(tool_path)], lock_path)
        judge = {"tool": "calib_judge", "version": version, "stage": "confirm", "plan": prereg_lock.relative_text(plan_path), "plan_sha256": sha(plan_path),
                 "lock": prereg_lock.relative_text(lock_path), "lock_sha256": sha(lock_path), "overlay_sha256": overlay_sha, "screen_result": f"{name}/screen.json",
                 "adopt": {"decision": decision, "variant": "v01", "assignments": assignments, "reasons": [] if decision else ["후보가 v00"], "evidence": {}}}
        if version != 2:
            judge = {"tool": "calib_judge", "sweep": "x", "lock": judge["lock"], "scenes": [], "accepted": True}
        judge_path = output_dir / f"{name}-judge.json"
        judge_path.write_text(json.dumps(judge), encoding="utf-8")
        return judge_path

    overlay_path = output_dir / "overlay.json"
    accept = lambda judge_path: cli("accept", "--judge", str(judge_path), "--overlay", str(overlay_path))
    t19_judge = make_judge("t19", [{"json_path": vorticity, "value": 1.75}])
    t19_ok = accept(t19_judge)
    t19_overlay = load_json(overlay_path) if overlay_path.exists() else {"assignments": [{}], "history": [{}]}
    t19_entry = t19_overlay["assignments"][0]
    t19_false = accept(make_judge("t19-false", [{"json_path": cloud_global, "value": 3.0}], decision=False, overlay_sha=sha(overlay_path) if overlay_path.exists() else None))
    t19_old = accept(make_judge("t19-old", [{"json_path": cloud_global, "value": 3.0}], version=1))
    check("T19 accept: v2 adopt 참 → 항목·from·history / adopt 거짓 → 종료 1 / 옛 형식 → 종료 2",
          t19_ok[0] == 0 and t19_entry.get("json_path") == vorticity and t19_entry.get("value") == 1.75 and t19_entry.get("kind") == "knob"
          and t19_entry["from"]["variant"] == "v01" and t19_entry["from"]["judge_sha256"] == sha(t19_judge) and t19_entry["from"]["screen_judge"] == "t19/screen.json"
          and t19_overlay["history"][0]["op"] == "add" and t19_overlay["history"][0]["json_path"] == vorticity
          and t19_false[0] == 1 and "adopt.decision" in t19_false[1] and t19_old[0] == 2 and "v2" in t19_old[1], (t19_ok[1][-300:], t19_false, t19_old))

    after_t19 = sha(overlay_path)
    t78_judge = make_judge("t78", [{"json_path": cloud_global, "value": 3.0}], overlay_sha=after_t19)
    tool_path = output_dir / "t78-tool.py"
    tool_path.write_bytes(tool_path.read_bytes() + b"#")
    t78 = accept(t78_judge)
    t78_stale = accept(make_judge("t78-stale", [{"json_path": cloud_global, "value": 3.0}], overlay_sha="0" * 64))
    check("T78 accept 때 잠금 도구 1바이트 변경 → 종료 2 / 덧씌움 sha 가 판정과 다름 → 종료 2 / 덧씌움 그대로",
          t78[0] == 2 and "잠금 재검증 실패" in t78[1] and t78_stale[0] == 2 and "끼워 넣기" in t78_stale[1] and sha(overlay_path) == after_t19, (t78, t78_stale))

    base_extent = {"json_path": "Kinds.FoliageWalk.TDFluidSimulationConfig.SimExtentCm", "value": {"X": 1500.0, "Y": 1500.0}}
    reed_recovery = "Kinds.ReedShallows.TDFluidSimulationConfig.BendVectorRecoverySeconds"
    t84_none = accept(make_judge("t84-none", [{"json_path": cloud_global, "value": 3.0}], {"base_tuning": [base_extent]}, overlay_sha=after_t19))
    t84_ships = accept(make_judge("t84-ships", [{"json_path": cloud_global, "value": 3.0}], {"base_tuning": [base_extent], "base_tuning_ships": True}, overlay_sha=after_t19))
    measured_extra = {"base_tuning": [{"json_path": reed_recovery, "value": 0.2}], "base_tuning_ships": False}
    measured_judge = make_judge("t84-measured", [{"json_path": river_scalar, "value": 0.6}], measured_extra, overlay_sha=sha(overlay_path))
    measured_overlay_sha = load_json(measured_judge)["overlay_sha256"]
    t84_measured = accept(measured_judge)
    entries = {entry["json_path"]: entry for entry in load_json(overlay_path)["assignments"]}
    check("T84 base_tuning: ships 없음 → 종료 2 / ships 참 → base 항목 kind base_tuning / 거짓 → measured_under_base",
          t84_none[0] == 2 and "base_tuning_ships" in t84_none[1] and t84_ships[0] == 0 and entries.get(base_extent["json_path"], {}).get("kind") == "base_tuning"
          and entries.get(cloud_global, {}).get("kind") == "knob" and "measured_under_base" not in entries.get(cloud_global, {})
          and t84_measured[0] == 0 and entries.get(river_scalar, {}).get("measured_under_base") == [reed_recovery] and reed_recovery not in entries,
          (t84_none, t84_ships[1][-300:], t84_measured[1][-300:], list(entries)))

    first = cli("materialize", "--overlay", str(overlay_path), "--out", str(output_dir / "t83-a"))
    second = cli("materialize", "--overlay", str(overlay_path), "--out", str(output_dir / "t83-b"))
    copy_tuning = load_json(output_dir / "t83-a" / "TDFluidPresetTuning.json") if first[0] == 0 else {}
    copy_look = load_json(output_dir / "t83-a" / "TDFluidMaterialLook.json") if first[0] == 0 else {}
    materialized = first[0] == 0 and second[0] == 0 and sha(output_dir / "t83-a" / "TDFluidPresetTuning.json") == sha(output_dir / "t83-b" / "TDFluidPresetTuning.json") \
        and sha(output_dir / "t83-a" / "TDFluidMaterialLook.json") == sha(output_dir / "t83-b" / "TDFluidMaterialLook.json") \
        and copy_tuning["Kinds"]["Campfire"]["TDFluidSimulationConfig"]["VorticityStrength"] == 1.75 and copy_tuning["Globals"]["TDFluidCloudSceneSettings"]["ViewSampleCountScaleFar"] == 3.0 \
        and copy_tuning["Kinds"]["FoliageWalk"]["TDFluidSimulationConfig"]["SimExtentCm"] == {"X": 1500.0, "Y": 1500.0} and copy_look["Materials"][river]["Scalars"]["CausticsStrength"] == 0.6 \
        and load_json(output_dir / "t83-a" / "variant.json")["overlay_sha256"] == sha(overlay_path)
    removed = cli("overlay-remove", "--overlay", str(overlay_path), "--json-path", cloud_global, "--reason", "사슬 재검증 실패")
    after_remove = load_json(overlay_path)
    replace_judge = make_judge("t83-replace", [{"json_path": river_scalar, "value": 0.7}], plan_path=output_dir / "t84-measured-plan.json", overlay_sha=measured_overlay_sha)
    replaced = cli("overlay-replace", "--overlay", str(overlay_path), "--json-path", river_scalar, "--judge", str(replace_judge))
    after_replace = load_json(overlay_path)
    replaced_entry = next((entry for entry in after_replace["assignments"] if entry["json_path"] == river_scalar), {})
    stale_replace = cli("overlay-replace", "--overlay", str(overlay_path), "--json-path", river_scalar, "--judge", str(make_judge("t83-stale", [{"json_path": river_scalar, "value": 0.8}], plan_path=output_dir / "t84-measured-plan.json", overlay_sha=sha(overlay_path))))
    check("T83 materialize(정본+덧씌움 값·두 번 같은 sha) / overlay-remove(항목 빠짐·history remove·사유) / overlay-replace(재판정 값·history replace·다른 덧씌움 판정은 종료 2)",
          materialized and removed[0] == 0 and all(entry["json_path"] != cloud_global for entry in after_remove["assignments"])
          and after_remove["history"][-1]["op"] == "remove" and after_remove["history"][-1]["reason"] == "사슬 재검증 실패" and after_remove["history"][-1]["value"] == 3.0
          and replaced[0] == 0 and replaced_entry.get("value") == 0.7 and replaced_entry.get("measured_under_base") == [reed_recovery] and replaced_entry["from"]["judge_sha256"] == sha(replace_judge)
          and after_replace["history"][-1]["op"] == "replace" and after_replace["history"][-1]["previous"]["value"] == 0.6 and stale_replace[0] == 2,
          (first[1][-300:], removed[1][-300:], replaced[1][-300:], stale_replace[1][-200:]))

    light = {"actor_prop": {"label": "TDGen_LabGreenLight", "component": "light_component", "property": "volumetric_scattering_intensity"}, "value": 4.0}
    prop_first = accept(make_judge("t19-prop", [light], overlay_sha=sha(overlay_path)))
    prop_entries = load_json(overlay_path)["actor_props"]
    prop_again = accept(make_judge("t19-prop-again", [light], overlay_sha=sha(overlay_path)))
    check("T19 보강 accept actor_prop: 레벨은 계획 장면에서·history 기술자·같은 속성 두 번 → 종료 2",
          prop_first[0] == 0 and len(prop_entries) == 1 and prop_entries[0]["level"] == "/Game/CAL0/L_B1" and prop_entries[0]["value"] == 4.0
          and load_json(overlay_path)["history"][-1]["json_path"] == "actor_prop:/Game/CAL0/L_B1/TDGen_LabGreenLight.light_component.volumetric_scattering_intensity"
          and prop_again[0] == 2 and "같은 속성" in prop_again[1], (prop_first[1][-300:], prop_again[1][-300:]))

    document = load_json(CANONICAL)
    layered = copy.deepcopy(document)
    layered["Groups"]["FireFamily"]["TDFluidSimulationConfig"]["TerrainSlopeGravity"] = 0.25
    layered["Kinds"]["Campfire"]["TDFluidSimulationConfig"].pop("VorticityStrength")
    for group in layered["Groups"].values():
        group.get("TDFluidSimulationConfig", {}).pop("VorticityStrength", None)
    layered["StructDefaults"]["TDFluidSimulationConfig"]["VorticityStrength"] = 1.23
    reed_copy = patch.write_variant([{"json_path": reed_recovery, "value": 0.2}], output_dir / "t97", root=W, schema=schema)
    reed_document = load_json(reed_copy["tuning"])
    check("T97(B1) effective_value: Kinds → Groups → StructDefaults·Globals 한 층, 갈대 사본 값 변경이 실효 값에 보임, 없는 필드는 거부",
          patch.effective_value(document, "Campfire", "TDFluidSimulationConfig", "VorticityStrength") == 1.5
          and patch.effective_value(layered, "Campfire", "TDFluidSimulationConfig", "TerrainSlopeGravity") == 0.25
          and patch.effective_value(layered, "Campfire", "TDFluidSimulationConfig", "VorticityStrength") == 1.23
          and patch.effective_value(document, "Campfire", "TDFluidCloudSceneSettings", "ViewSampleCountScaleFar") == 2.0
          and patch.effective_value(document, "ReedShallows", "TDFluidSimulationConfig", "BendVectorRecoverySeconds") == 0.08
          and patch.effective_value(reed_document, "ReedShallows", "TDFluidSimulationConfig", "BendVectorRecoverySeconds") == 0.2
          and rejection(lambda: patch.effective_value(document, "Campfire", "TDFluidSimulationConfig", "NoSuchField")) is not None)


run_check(check_cal0_b0)
run_check(check_cal0_patch)
check("실행 전후 정본 sha256 동일", fluid_tuning_patch.file_sha256(CANONICAL) == canonical_before and fluid_tuning_patch.file_sha256(fluid_tuning_patch.canonical_look_path()) == look_canonical_before)

failed = [name for name, passed in RESULTS if not passed]
print(f"SUMMARY passed={len(RESULTS) - len(failed)}/{len(RESULTS)}")
sys.exit(1 if failed else 0)
