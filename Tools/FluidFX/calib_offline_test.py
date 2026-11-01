"""실행 환경: 시스템 Python 3(에디터·PIE 불필요, numpy·Pillow·scipy 필요, 가짜 unreal 로 calib_sweep·pie_lookdev_fluid·pie_probe_fluid_water 를 프로세스 안에서 돌린다). 2026-10-25 P3 오프라인 시험(w0-P3-offline-test.py)을 본 트리로 옮겨 새 판정 규칙(목표 지표 대조·연속 점수·점수 잡음 유의)과 짧은 창 배율·대조 처음·끝 모드를 더했다.
실행 명령: python -B Tools/FluidFX/calib_offline_test.py (종료 코드 0 전부 통과 / 1 실패 있음, 마지막 줄 SUMMARY passed=N/M)
출력: Saved/FluidFX/calib/offline-test/ 아래 스윕·판정 산출물(실행마다 지움)과 표준 출력 PASS/FAIL 줄. 수용 시험 산출(Saved/FluidFX/calib/{campfire-run1,campfire-run2,campfire-r4-run1,river-run1})이 있으면 새 판정기로 재판정해 SKIP 아닌 결과를 낸다.
상태: 2026-10-25 작성, 실제 PIE 스윕은 돌리지 않는다(정본 JSON sha256 전후 비교 포함). 2026-10-30 CAL-0 B0: 공용 가짜 한 구역(F-WORLD 구조체·필드 표와 현재 상태 대비 ChangedFieldCount·모양 재적용, F-TICK 가짜 시계·슬레이트 틱 콜백·시간 배율, F-UNREAL 재질·나이아가라·데칼·구름·후처리·식생 게터), 모닥불·강 계획은 f02965cb 고정 사본 Tools/FluidFX/calib/fixtures/*.head.json(모닥불 영역 틀도 712dfa89 전 고정 사본 fixtures/campfire_fire_regions.head.json, 재판정은 계획 사본의 영역 틀로), 묶음 시험은 check_cal0_<묶음>() 를 정본 sha 단언 직전 run_check 한 줄로 부르고 patched·run_check 가 패치를 되돌린다. CAL-0 마무리 check_cal0_real_queue 는 실제 batch_queue.json·calib/plans·calib/regions 를 가짜 B8(스윕 자리에서 calib_sweep.load_plan 단계 검사)로 돌려 failed·초안 상자 잠금·needs_frame 경로 미선언을 거부한다(영역 상자를 그려 넣으면 그 계획은 잠금·screen 쪽으로 간다). 2026-10-31 check_lookdev_frame_fixes(F1~F11): 사전 move 입력 걷기·시한 초과·목록 move·이벤트 move·move 형식 키 오류·base 오류 키 오류·calib_sweep base/단계 오류 무효·AEM_MANUAL 노출 바이어스·reinit 전체 상태 초기화 수와 예약 시간 원점·구름 흐름 시계 재시작(reinit·keep 단계마다 1회, 예약 원점과 같은 게임 시각, 예외는 키 오류, 가짜 구름 액터에 restart_cloud_flow_clock)·안개 흐름 원점(F11: 안개 마스터 사용자가 있는 무대만 단계마다 MPC FogFlowOriginSeconds = 그 apply 게임 시각, MPC 매개변수 없음·MPC 없음은 키 오류, 비안개 무대는 MPC 가 메모리에 있어도 키 없음, 가짜 MaterialParameterCollection·MaterialLibrary, MPC 경로·이름은 editor_make_fluid_fog_material 상수로 대조, F11b 모양 정본 MPC 이름 덮어쓰기 거부), 가짜 월드에 기본 플레이어 폰·컨트롤러(F-UNREAL Vector·Rotator 인자 생성)·set_simulation_awake·set_actor_hidden_in_game 을 더해 카메라 apply 가 오프라인에서도 실제로 돈다. 2026-10-31 check_capture_tool_fixes(C1~C7): 가짜 UE 로그 스트림(TextIOWrapper + write 마다 로그 항목)으로 큰 결과의 표식·본문 분리 재현과 새 파서, 큰 탐침 결과 파일 전달, 빈 표식·원격 실행 예외·결과 파일 없음 흡수(단계 무효), foliage 소프트 참조 None 사유, calib_sweep 지난 result.json 이동·run_id·variants_sha256 대조. 2026-10-31 check_time_origin_fixes(시간원 2차 F12~F14): F12 볼륨 흐름 원점(기반 재질 M_TD_FluidVolume 사용자 무대만 단계마다 MPC_TD_FluidVolumeFlow VolumeFlowOriginSeconds = 그 apply 게임 시각, 가짜 볼륨 마스터만이면 키 없음, MPC 매개변수 없음·MPC 없음은 키 오류)·F12b 볼륨 모양 정본 MPC 이름 덮어쓰기 거부·F12c 볼륨 마스터 그래프(기록 가짜 MaterialEditingLibrary: Subtract(Time, CollectionParameter), 가짜 볼륨은 Time)와 흐름 MPC overwrite 규칙·F13 잎 바람 원점(채널 3개 MPC FoliageWindOriginSeconds, 같은 무대 안개·볼륨 원점 같은 시각, 한 채널 매개변수 없음은 그 채널만 키 오류 'foliage_wind_origin: <MPC 이름>: ')·F13b 식생 MPC 이름 덮어쓰기 거부·F13c 휨 MF 그래프와 채널 MPC 스칼라·F14 reinit 의 TDFluidLiveActor reset_interaction_history(가짜 라이브 액터에 호출 수, keep 은 없음, 실패는 state_reset 키 오류). B4b 는 Saved/AgentOps/20261020/fx8c/scratch/live2-targets/targets.json 이 있어야 해서 Saved 없는 작업 트리에서는 그 파일을 메인 트리에서 복사해 돌린다(복사 뒤 281/281, 수용 산출 Saved/FluidFX/calib/campfire-run1·campfire-run2·campfire-r4-run1·river-run1 도 복사하면 289/289). 2026-11-01 K9: 어느 손잡이 param scenes 에도 없는 장면의 screen 건너뜀(skipped_scenes)·confirm 재판정 일치·sweep params[].scenes 손편집 거부 check 2개. 2026-11-01 K9 G: 덧씌움 Look cloud_setups(같은 레벨 구름 셋업)로 셋업 밖 장면 skip_if_no_owner·셋업 안·CloudSetup 없는 장면 엄격·정수 아닌 목록 종료 2, accept 의 cloud_setups 기록 check 2개. 2026-11-01 check_water_time_origin_fixes(물 시간 원점 F15~F15c): F15 물 흐름 원점(기반 재질 M_TD_FluidWater·Lite 사용자 무대만 reinit·keep 단계마다 MPC_TD_FluidWaterFlow WaterFlowOriginSeconds = 그 apply 게임 시각, 같은 값으로 모든 가짜 TDFluidWaterParticleActor set_water_time_origin_seconds, MPC 매개변수 없음·MPC 없음·설정 함수 없음은 키 오류 'water_flow_origin: ', 물 사용자 없으면 키 없음)·F15b 물 모양 정본 MPC 이름 덮어쓰기 거부·F15c 물 마스터 그래프 Subtract(Time, CollectionParameter)·원천 캐시 공유·흐름 MPC overwrite 규칙·물 HLSL 직접 시간 기호 없음. 2026-11-01 K10 2차: B1~B5 자기 채택 출처를 from.plan 프로젝트 상대 경로+plan_sha256 으로, B4 는 두 레벨 장면 계획, C1 calib_sweep verify P-20 자손 거부·C2 calib_batch 사슬 재검증 P-20 자손 제외(일지 reason·batch.log), D1 다른 폴더 같은 stem·sha 다름 거부, E1 자기 actor_props 대상 불일치 거부, E2 actor_props 대상 겹침 거부(screen 자기·verify·frame 남의, 레벨 밖 통과), F12d HVOL 마스터 ElapsedSeconds = Time − MPC_TD_FluidVolumeFlow·MPC 읽기 규칙.
"""
import contextlib
import copy
import hashlib
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


class FakeVector:
    def __init__(self, x=0.0, y=0.0, z=0.0):
        self.x, self.y, self.z = float(x), float(y), float(z)


class FakeRotator(Props):
    def __init__(self, roll=0.0, pitch=0.0, yaw=0.0):
        super().__init__(roll=roll, pitch=pitch, yaw=yaw)


class FakePlayerController(Props):
    def __init__(self):
        super().__init__(control_rotation=FakeRotator())

    def get_control_rotation(self):
        return self.control_rotation

    def set_control_rotation(self, rotation):
        self.control_rotation = rotation

    def stop_movement(self):
        pass


class FakePlayerPawn(Props):
    def __init__(self, speed=600.0):
        super().__init__(spring_arm=Props(target_arm_length=300.0, use_pawn_control_rotation=False, do_collision_test=True))
        self.location = FakeVector(0.0, 0.0, 90.0)
        self.speed = speed
        self.pending_intent = None
        self.teleports = []

    def get_actor_location(self):
        return FakeVector(self.location.x, self.location.y, self.location.z)

    def set_actor_location(self, location, sweep=False, teleport=False):
        self.location = FakeVector(location.x, location.y, location.z)
        self.teleports.append((location.x, location.y))

    def get_component_by_class(self, cls):
        return self.spring_arm

    def add_movement_input(self, direction, scale=1.0, force=False):
        self.pending_intent = (direction.x * scale, direction.y * scale)

    def get_max_move_speed(self):
        return self.speed

    def set_max_move_speed(self, speed):
        self.speed = speed


unreal.LinearColor = FakeLinearColor
unreal.Vector2D = type("Vector2D", (), {})
unreal.Vector = FakeVector
unreal.Rotator = FakeRotator


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


class FakeMaterialParameterCollection(FakeAsset):
    def __init__(self, path, scalar_names=()):
        super().__init__(path, scalar_parameters=[Props(parameter_name=name, default_value=0.0) for name in scalar_names])


def set_collection_scalar(world, collection, parameter_name, parameter_value):
    world.collection_scalars[(collection.path, str(parameter_name))] = float(parameter_value)


def get_collection_scalar(world, collection, parameter_name):
    return world.collection_scalars.get((collection.path, str(parameter_name)), 0.0)


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
unreal.MaterialParameterCollection = FakeMaterialParameterCollection
unreal.MaterialLibrary = Props(set_scalar_parameter_value=set_collection_scalar, get_scalar_parameter_value=get_collection_scalar)
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

    def set_actor_hidden_in_game(self, is_hidden):
        self.is_hidden_in_game = is_hidden

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
        self.flow_restart_game_times = []

    def restart_cloud_flow_clock(self):
        self.flow_restart_game_times.append(CLOCK.game_seconds)


class TDFluidWaterParticleActor(FakeActor):
    def __init__(self, label):
        super().__init__(label)
        self.water_time_origins = []

    def set_water_time_origin_seconds(self, origin_seconds):
        self.water_time_origins.append(origin_seconds)


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
unreal.TDFluidWaterParticleActor = TDFluidWaterParticleActor
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
        self.reset_count = 0
        self.is_awake = True

    def reset_simulation_state(self):
        self.reset_count += 1

    def set_simulation_awake(self, is_awake):
        self.is_awake = is_awake

    def init_system_state_from_preset(self, preset):
        self.init_count += 1
        self.simulation_config = copy.copy(preset.simulation_config)
        if not uses_copies(preset):
            self.world.apply_effective(self.owner_actor)


class TDFluidLiveActor(FakeActor):
    def __init__(self, world, label, preset):
        super().__init__(label, live_component=FakeLive(world, preset), source_emitter=Props(radius_cm=26.0), ember_component=None, internal_surface_mesh=None, volume_mesh_component=None)
        self.live_component.owner_actor = self
        self.history_reset_count = 0

    def reset_interaction_history(self):
        self.history_reset_count += 1


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
        self.collection_scalars = {}
        self.shot_count = 0
        self.player_pawn = FakePlayerPawn()
        self.player_controller = FakePlayerController()

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
                               set_global_time_dilation=lambda world, value: setattr(CLOCK, "time_dilation", value), get_player_controller=lambda world, index: world.player_controller,
                               get_player_pawn=lambda world, index: world.player_pawn)
unreal.SystemLibrary = Props(execute_console_command=console, get_console_variable_float_value=lambda name: 0.0)
sys.modules["unreal"] = unreal
import editor_make_fluid_fog_material as fog_material  # noqa: E402
import editor_make_fluid_foliage_material as foliage_material  # noqa: E402
import editor_make_fluid_heterogeneous_volume_material as hvol_material  # noqa: E402
import editor_make_fluid_volume_material as volume_material  # noqa: E402
import editor_make_fluid_water_material as water_material  # noqa: E402
import fluid_material_look  # noqa: E402


EDITOR_SHARED_NAMESPACE = None


def fake_editor_action(action, **fields):
    code = pie_showcase_fluid.EDITOR_CODE.replace("\ndef run():", lookdev.LOOKDEV_ACTIONS, 1)
    code = code.replace("__PARAMS__", repr(json.dumps({"action": action, **fields})))
    namespace = EDITOR_SHARED_NAMESPACE if EDITOR_SHARED_NAMESPACE is not None else {"__name__": "editor"}
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        exec(compile(code, "<editor>", "exec"), namespace)
    for line in buffer.getvalue().splitlines():
        if pie_showcase_fluid.RESULT_MARKER in line:
            return json.loads(line.split(pie_showcase_fluid.RESULT_MARKER, 1)[1])
    return {"error": "no marker: " + buffer.getvalue()[-300:]}


REAL_LOOKDEV_EDITOR_ACTION = lookdev.editor_action
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


def run_lookdev_in_process(spec_path, level, output_dir, run_id=None):
    saved_argv = sys.argv
    sys.argv = ["pie_lookdev_fluid.py", "--variants", str(spec_path), "--level", level, "--output-dir", str(output_dir), "--stop-pie"] + (["--run-id", run_id] if run_id else [])
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
    mutate(plan)
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
check("find_key_errors 가 R2 키를 셈", len(lookdev.find_outcome_errors(applied)) == 1, lookdev.find_outcome_errors(applied))
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
    plan_regions = {scene["name"]: scene.get("regions_template") for scene in load_json(plan_path)["scenes"]}
    for scene in old_sweep.get("scenes", []):
        if plan_regions.get(scene.get("name")):
            scene["regions_template"] = plan_regions[scene["name"]]
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
    check("CAL-0 재반박 N1 accept: Look 채택 항목에 levels(Look 을 적용한 장면의 수준), 조절 채택 항목에는 없음",
          entries.get(river_scalar, {}).get("levels") == ["/Game/CAL0/L_B1"] and "levels" not in entries.get(cloud_global, {}), (entries.get(river_scalar), entries.get(cloud_global)))

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


def check_cal0_lookdev():
    output_dir = TEST_DIR / "cal0-lookdev"
    output_dir.mkdir(parents=True, exist_ok=True)
    lookdev.OUTPUT_DIRECTORY = output_dir

    typo = lookdev.find_key_errors({"veiw_target": "TDGen_CloudCam_Stratus", "arm": 300.0})
    cameras = [scene["camera"] for path in sorted((W / "Tools/FluidFX/calib/plans").glob("*.json")) for scene in load_json(path).get("scenes", []) if isinstance(scene, dict) and isinstance(scene.get("camera"), dict)]
    camera_errors = [error for camera in cameras for error in lookdev.find_key_errors(camera)]
    nested = lookdev.find_key_errors({"schedule": {"times": [1.0], "reference": "call", "evnts": []}, "look_live": {"materials": {"/Game/CAL0/MI_Look": {"Switches": {"A": True}}}},
                                      "probe": {"rt": [{"label": "P", "property": "rt", "box": [0, 0, 1], "step": 0, "channel": "xy", "prefix": "p"}]}})
    check("T07 find_key_errors(apply): 모르는 키 veiw_target 1건(view_target 제안)·현재 계획 camera 전부 0건·중첩 키(schedule·reference call·Switches·rt box·step·channel) 6건",
          len(typo) == 1 and "veiw_target" in typo[0] and "view_target" in typo[0] and cameras and not camera_errors and len(nested) == 6
          and all(any(word in line for line in nested) for word in ("evnts", "reference call", "Switches", ".box", ".step", ".channel")), (typo, camera_errors[:3], nested))

    reset_world([(TDFluidFireActor, "TDGen_CampfireFire", FakePreset("CAMPFIRE"))])
    tuning = load_json(CANONICAL)
    tuning["Kinds"]["Campfire"]["TDFluidSimulationConfig"]["VorticityStrength"] = 2.25
    control_path = output_dir / "control-tuning.json"
    control_path.write_text(json.dumps(tuning), encoding="utf-8")
    old = lookdev.run_tuning_reload({"tuning": str(control_path), "look": "", "reset": "keep"})
    new = lookdev.run_tuning_reload({"tuning": str(control_path), "look": "", "reset": "keep", "base": {"tuning": str(control_path), "look": ""}})
    check("T68 G-BASE: base 없는 옛 요청은 덧씌움 든 대조 사본에서 ChangedFieldCount 1(단계 무효 조건)·base 요청은 대조 사본을 먼저 적재해 0",
          old["report"]["changed_field_count"] == 1 and new["report"]["changed_field_count"] == 0 and Path(new["base_copy"]["tuning"]).resolve() == control_path.resolve()
          and Path(new["base"]["tuning_source_path"]).resolve() == control_path.resolve(), (old.get("report"), new.get("report"), new.get("error")))

    package = "/Game/CAL0/MI_Look"
    base_material = FakeMaterial("/Game/CAL0/M_Look", scalars={"Glow": 1.0}, vectors={"Tint": FakeLinearColor(1.0, 1.0, 1.0, 1.0)})
    target = FakeMaterialInstanceConstant(package, scalars={"Glow": 2.0}, parent=base_material)
    child = FakeMaterialInstanceConstant("/Game/CAL0/MI_LookChild", parent=target)
    variant_values = {"Scalars": {"Glow": 4.0}, "Vectors": {"Tint": [0.5, 0.25, 0.0, 1.0]}}
    control_values = {"Scalars": {"Glow": 4.0}, "Vectors": {"Tint": [1.0, 1.0, 1.0, 1.0]}}
    look_paths = {}
    for name, values in (("variant", variant_values), ("control", control_values)):
        look = load_json(CANONICAL_LOOK)
        look["Materials"][package] = dict(values, Parent="/Game/CAL0/M_Look", Switches={})
        look_paths[name] = output_dir / f"look-{name}.json"
        look_paths[name].write_text(json.dumps(look), encoding="utf-8")

    def run_look(build, look_live=None, extra=None):
        reset_world([(TDFluidFireActor, "TDGen_CampfireFire", FakePreset("CAMPFIRE"))])
        WORLD.add_asset(target)
        actors = build()
        WORLD.actors += actors
        created_before = FakeMaterialInstanceDynamic.created_count
        state = {"uses_tuning_reload": False, "key_errors": []}
        apply = dict({"tuning_reload": {"tuning": "", "look": str(look_paths["variant"]), "reset": "keep", "relative_to_base": False},
                      "look_live": look_live or {"materials": {package: variant_values}}}, **(extra or {}))
        outcome = lookdev.apply_variant_with_tuning(apply, "s00", state)
        return actors, outcome, state["key_errors"], FakeMaterialInstanceDynamic.created_count - created_before

    def owners(outcome):
        return [(item["owner"], item["by"], item.get("matches")) for item in outcome.get("owner_checks", [])]

    def reads_match(outcome):
        entries = outcome.get("look_read_back", {}).get(package, {})
        return entries and all(entry["matches"] and entry["read"] for section in entries.values() for entry in section.values())

    actors, outcome, errors, created = run_look(lambda: [FakeActor("TDGen_Prop", components=[FakeStaticMeshComponent("Mesh", [target])])],
                                               extra={"look_live": {"materials": {package: variant_values}, "base": {package: {"Scalars": {"Glow": 2.0}}}}})
    slot = actors[0].components[0].get_material(0)
    check("T25a primitive_slot: MI 슬롯을 MID 로 교체·값 설정·MI 자산 불변·1초 뒤 다시 읽기 일치·적용 수",
          isinstance(slot, FakeMaterialInstanceDynamic) and slot.parent is target and slot.get_scalar_parameter_value("Glow") == 4.0 and target.scalars["Glow"] == 2.0 and created == 1
          and outcome["look_applied"] == {package: {"Glow": 1, "Tint": 1}} and owners(outcome) == [("primitive_slot", "python", True)] and reads_match(outcome) and not errors, (outcome, errors))
    _, mismatch, mismatch_errors, _ = run_look(lambda: [FakeActor("TDGen_Prop", components=[FakeStaticMeshComponent("Mesh", [target])])],
                                               extra={"look_live": {"materials": {package: variant_values}, "base": {package: {"Scalars": {"Glow": 9.0}}}}})
    check("T86 첫 대조 단계 원 MI 값 ≠ base → base 불일치(일치하면 0건)",
          outcome["look_base"] == {"checked": 1, "mismatches": []} and len(mismatch["look_base"]["mismatches"]) == 1 and any("base 불일치" in line for line in mismatch_errors), (mismatch.get("look_base"), mismatch_errors))

    def cpp_card_actor():
        actor = TDFluidLiveActor(WORLD, "TDGen_CardFire", FakePreset("CAMPFIRE"))
        actor.components.append(FakeStaticMeshComponent("Card", [WORLD.add_cpp_material(target)]))
        return [actor]

    actors, outcome, errors, created = run_look(cpp_card_actor)
    card = actors[0].components[0].get_material(0)

    def cpp_card_without_reapply():
        WORLD.cpp_reapplies_look = False
        return cpp_card_actor()

    _, stale, stale_errors, _ = run_look(cpp_card_without_reapply)
    check("T25b cpp_mid: 교체·직접 설정 0·모양 사본 적재로 C++ 가 다시 적용·다시 읽기 일치 / C++ 가 안 하면 단계 무효 문구",
          created == 0 and card.get_scalar_parameter_value("Glow") == 4.0 and owners(outcome) == [("cpp_mid", "cpp", True)] and reads_match(outcome) and not errors
          and owners(stale) == [("cpp_mid", "cpp", False)] and any("C++ 소유 MID가 모양 사본을 다시 적용하지 않음" in line for line in stale_errors), (outcome, stale_errors))

    actors, outcome, errors, created = run_look(lambda: [TDFluidCloudLayersActor("TDGen_Clouds", WORLD.add_cpp_material(target))])
    check("T25c cloud_component: 구름 컴포넌트 C++ MID 를 교체 없이 다시 읽기 일치",
          created == 0 and owners(outcome) == [("cloud_component", "cpp", True)] and reads_match(outcome) and not errors, (outcome, errors))

    actors, outcome, errors, created = run_look(lambda: [TDFluidFoliageFlowActor("TDGen_Foliage", WORLD.add_cpp_material(target))])
    with patched(TDFluidFoliageFlowActor, get_flow_copy_material_instance=None):
        _, no_getter, no_getter_errors, _ = run_look(lambda: [TDFluidFoliageFlowActor("TDGen_Foliage", WORLD.add_cpp_material(target))])
    check("T25d foliage_flow: B5 게터로 FlowCopy MID 다시 읽기 일치 / 게터 없으면 '게터 없음: B5 필요'",
          created == 0 and owners(outcome) == [("foliage_flow", "cpp", True)] and reads_match(outcome) and not errors
          and any("게터 없음: B5 필요" in line for line in no_getter_errors) and no_getter["look_applied"][package]["Glow"] == 0, (outcome, no_getter_errors))

    actors, outcome, errors, created = run_look(lambda: [TDFluidSurfaceOutputActor("TDGen_Blood", target), TDFluidSurfaceOutputActor("TDGen_BloodCpp", WORLD.add_cpp_material(target))])
    decal = actors[0].output_decal.get_decal_material()
    check("T25e decal: MI 데칼은 create_dynamic_material_instance 뒤 설정·C++ MID 데칼은 다시 읽기만·둘 다 일치",
          created == 1 and isinstance(decal, FakeMaterialInstanceDynamic) and decal.parent is target and decal.get_scalar_parameter_value("Glow") == 4.0
          and owners(outcome) == [("decal", "python", True), ("decal", "cpp", True)] and reads_match(outcome) and not errors, (outcome, errors))

    actors, outcome, errors, created = run_look(lambda: [FakePostProcessVolume("TDGen_Underwater", [WORLD.add_cpp_material(target)])])
    check("T25f post_process: 후처리 blendable C++ MID 를 교체 없이 다시 읽기 일치",
          created == 0 and owners(outcome) == [("post_process", "cpp", True)] and reads_match(outcome) and not errors, (outcome, errors))

    rain_system = FakeNiagaraSystem("/Game/CAL0/NS_Look")

    def rain_actor():
        WORLD.add_asset(rain_system)
        return [FakeActor("TDGen_Rain", components=[FakeNiagaraComponent("Rain", rain_system)])]

    actors, outcome, errors, _ = run_look(rain_actor, look_live={"niagara": {"/Game/CAL0/NS_Look": {"Floats": {"User.Rate": 5.0}}}, "niagara_reset": True})
    rain = actors[0].components[0]
    check("T26 look_live NS + niagara_reset: set_variable 기록·reinitialize 1·적용 수 1",
          rain.variables == {"User.Rate": 5.0} and rain.reinitialize_count == 1 and outcome["look_applied"] == {"/Game/CAL0/NS_Look": {"User.Rate": 1}} and not errors, (outcome, errors))

    _, outcome, errors, _ = run_look(lambda: [], look_live={"materials": {"/Game/CAL0/MI_Missing": {"Scalars": {"Glow": 4.0}}}})
    check("T27 look_live 대상 패키지가 레벨에 없음 → 적용 수 0 → key_errors(단계 무효)",
          outcome["look_applied"] == {"/Game/CAL0/MI_Missing": {"Glow": 0}} and errors == ["s00: look_live: look 적용 대상 없음: /Game/CAL0/MI_Missing"], (outcome, errors))

    actors, outcome, errors, created = run_look(lambda: [FakeActor("TDGen_Prop", components=[FakeStaticMeshComponent("Mesh", [child])]), TDFluidCloudLayersActor("TDGen_Clouds", WORLD.add_cpp_material(child))])
    check("T85 조부모가 대상인 MI·C++ MID(직접 부모 아님) → 적용 0·슬롯 그대로",
          created == 0 and actors[0].components[0].get_material(0) is child and outcome["look_applied"] == {package: {"Glow": 0, "Tint": 0}}
          and any("look 적용 대상 없음" in line for line in errors), (outcome, errors))

    reset_world([(TDFluidFireActor, "TDGen_CampfireFire", FakePreset("CAMPFIRE"))])
    WORLD.add_asset(target)
    prop_mesh = FakeStaticMeshComponent("Mesh", [target])
    clouds = TDFluidCloudLayersActor("TDGen_Clouds", WORLD.add_cpp_material(target))
    WORLD.actors += [FakeActor("TDGen_Prop", components=[prop_mesh]), clouds]
    steps = [("s00_v00", "control", control_values), ("s01_v01", "variant", variant_values), ("s02_v00", "control", control_values)]
    spec = {"base": {}, "variants": [{"name": name, "settle": 0.1, "apply": {"tuning_reload": {"tuning": "", "look": str(look_paths[copy]), "reset": "keep", "base": {"tuning": "", "look": str(look_paths["control"])}},
                                                                          "look_live": {"materials": {package: values}}}} for name, copy, values in steps]}
    spec_path = output_dir / "t87-spec.json"
    spec_path.write_text(json.dumps(spec), encoding="utf-8")
    exit_code = run_lookdev_in_process(spec_path, "/Game/CAL0/Stage", output_dir / "t87")
    result = load_json(output_dir / "t87" / "result.json")
    outcomes = [entry["apply"] for entry in result["variants"]]
    check("T87 덧씌움 Look → 대조 포함 모든 단계 look_live 적용·단계마다 다시 읽기 일치(파이썬·C++ 소유자)·대조로 돌아오면 C++ MID 도 대조 값",
          exit_code == 0 and not result["key_errors"] and len(outcomes) == 3 and all(reads_match(item) and sorted(owners(item)) == [("cloud_component", "cpp", True), ("primitive_slot", "python", True)] for item in outcomes)
          and clouds.components[0].material.get_vector_parameter_value("Tint") == FakeLinearColor(1.0, 1.0, 1.0, 1.0) and prop_mesh.get_material(0).get_vector_parameter_value("Tint") == FakeLinearColor(1.0, 1.0, 1.0, 1.0),
          (exit_code, result.get("key_errors"), [owners(item) for item in outcomes]))

    reset_world([(TDFluidFireActor, "TDGen_CampfireFire", FakePreset("CAMPFIRE"))])
    camera = FakeActor("TDGen_CloudCam_Stratus")
    WORLD.actors.append(camera)
    view_calls = []
    controller = Props(set_view_target_with_blend=lambda actor, blend_time=0.0: view_calls.append((actor, blend_time)))
    state = {"uses_tuning_reload": False, "key_errors": []}
    with patched(unreal.GameplayStatics, get_player_controller=lambda world, index: controller):
        found = lookdev.apply_variant_with_tuning({"view_target": "TDGen_CloudCam_Stratus"}, "s00", state)
        missing = lookdev.apply_variant_with_tuning({"view_target": "TDGen_CloudCam_Missing"}, "s01", state)
    check("T41 view_target: 라벨 있으면 set_view_target_with_blend 1회 / 없으면 key_errors",
          view_calls == [(camera, 0.0)] and found["view_target"]["ok"] and not missing["view_target"]["ok"] and state["key_errors"] == ["s01: view_target TDGen_CloudCam_Missing: 액터 없음"], (found, missing, state))

    live = WORLD.actors[0].live_component
    live.rain_settings = Props(rain_rate=3.0)
    live.get_effective_runtime_ops_settings = lambda: Props(max_steps_per_frame=4)

    class TDFluidWaterParticleActor(FakeActor):
        def get_water_particle_settings(self):
            return Props(spawn_rate_per_second=120.0)

    WORLD.actors.append(TDFluidWaterParticleActor("TDGen_Spray"))
    requests = [{"struct": "TDFluidRainSettings", "field": "RainRate", "kinds": ["Campfire"], "owner": "live_component", "access": "rain_settings"},
                {"struct": "TDFluidRuntimeOpsSettings", "field": "MaxStepsPerFrame", "kinds": ["Campfire"], "owner": "live_component", "access": "call:get_effective_runtime_ops_settings"},
                {"struct": "TDFluidWaterParticleSettings", "field": "SpawnRatePerSecond", "owner": "actor_class:TDFluidWaterParticleActor", "access": "call:get_water_particle_settings"},
                {"struct": "TDFluidInputBrushSettings", "field": "RadiusCm", "kinds": ["Campfire"], "owner": "live_component", "access": "call:get_effective_input_brush_settings"}]
    with patched(unreal, TDFluidWaterParticleActor=TDFluidWaterParticleActor):
        read = fake_editor_action("lookdev_tuning_read_back", requests=requests).get("read_back", [])
    check("T42 다시 읽기: Rain(속성)·RuntimeOps(call:) 값 일치·요청 owner·access 기록",
          len(read) == 4 and read[0]["actors"]["TDGen_CampfireFire"]["value"] == 3.0 and read[1]["actors"]["TDGen_CampfireFire"]["value"] == 4 and read[1]["access"] == "call:get_effective_runtime_ops_settings", read)
    check("T43 actor_class: 소유자(LiveActor 밖)는 그 클래스 액터만 읽음", len(read) == 4 and read[2]["actors"] == {"TDGen_Spray": {"value": 120.0}}, read[2:3])
    check("T44 게터 없음 → 다시 읽기 오류 '게터 없음: B5 필요'(단계 무효 근거)", len(read) == 4 and "게터 없음: B5 필요" in read[3]["actors"]["TDGen_CampfireFire"].get("error", ""), read[3:])
    carried = lookdev.find_key_errors({"tuning_reload": {"tuning": "", "look": "", "read_back": requests + [{"struct": "TDFluidUnderwaterSettings", "field": "FogDensity", "owner": "subsystem:TDFluidUnderwaterSubsystem", "access": "call:get_underwater_settings"}]}})
    wrong_owner = lookdev.find_key_errors({"tuning_reload": {"read_back": [{"struct": "TDFluidRainSettings", "field": "RainRate", "owner": "component", "acess": "rain_settings"}]}})
    check("T45(B3 측) 요청 read_back[] 의 owner·access 를 받아 결과에 되적음·모르는 owner·키는 키 오류",
          not carried and len(wrong_owner) == 2 and any("acess" in line for line in wrong_owner) and any("owner component" in line for line in wrong_owner)
          and read[2]["owner"] == "actor_class:TDFluidWaterParticleActor" and read[0]["access"] == "rain_settings", (carried, wrong_owner))

    WORLD.actors += [FakeActor("TDGen_Prop", get_water_prop_stats=lambda: Props(last_coupling_mean_down_speed_cm_per_sec=120.0, contact_count=3)),
                     FakeActor("TDGen_Painter", paint=Props(flow_target="RT_Flow"))]
    probe_request = {"stats": [{"label": "TDGen_Prop", "function": "get_water_prop_stats", "prefix": "prop"}],
                     "rt": [{"label": "TDGen_Painter", "component": "paint", "property": "flow_target", "box": [0.1, 0.1, 0.9, 0.9], "step": 0.2, "channel": "rg_len", "radial_center": [0.5, 0.5], "threshold": 0.3, "prefix": "flow"}],
                     "times": [0.5, 1.0, 1.5]}
    state = {"uses_tuning_reload": False, "key_errors": []}
    with patched(unreal, RenderingLibrary=Props(read_render_target_raw_uv=lambda world, render_target, u, v: FakeLinearColor(u - 0.5, v - 0.5, 0.0, 1.0))):
        probed = lookdev.apply_variant_with_tuning({"probe": probe_request}, "s00", state)
        missing = lookdev.apply_variant_with_tuning({"probe": {"stats": [{"label": "TDGen_Prop", "function": "get_missing_stats", "prefix": "prop"}]}}, "s01", state)
    metrics = probed["probe"]["metrics"]
    samples = probed["probe"]["samples"]
    check("T46 probe_stats: 구조체 반환을 probe.prop.<필드> 로 펼침·times 3장 표본·게임 시각 뒤",
          metrics.get("probe.prop.LastCouplingMeanDownSpeedCmPerSec") == 120.0 and metrics.get("probe.prop.ContactCount") == 3 and len(samples) == 3
          and all(sample["elapsed"] >= sample["time"] for sample in samples), probed.get("probe"))
    grid = load_json(samples[0]["grid_paths"]["flow"]) if samples and "grid_paths" in samples[0] else {}
    check("T47 probe_rt box·radial_center(방사 벡터장) → radial_alignment_mean ≈1.0·격자 파일 5×5",
          abs((metrics.get("probe.flow.radial_alignment_mean") or 0.0) - 1.0) < 1e-9 and len(grid.get("values", [])) == 5 and len(grid["values"][0]) == 5 and "probe.flow.fraction_above" in metrics, metrics)
    check("T48 probe 함수 이름 없음 → key_errors(단계 무효)", state["key_errors"] == ["s01: probe: probe.stats TDGen_Prop.get_missing_stats: 함수 없음"] * 1 and not missing["probe"]["metrics"], state["key_errors"])
    WORLD.actors.append(FakeActor("TDGen_FluidVelocity", get_velocity_texture=lambda: "RT_Velocity"))
    getter_rt = {"label": "TDGen_FluidVelocity", "function": "get_velocity_texture", "box": [0.0, 0.0, 1.0, 1.0], "step": 0.5, "channel": "rg_len", "prefix": "velocity"}
    with patched(unreal, RenderingLibrary=Props(read_render_target_raw_uv=lambda world, render_target, u, v: FakeLinearColor(1.0 if render_target == "RT_Velocity" else 0.0, 0.0, 0.0, 1.0))):
        by_getter = lookdev.apply_variant_with_tuning({"probe": {"rt": [getter_rt], "times": [0.5]}}, "s02", state)
    both = lookdev.find_key_errors({"probe": {"rt": [dict(getter_rt, property="flow_target")]}})
    neither = lookdev.find_key_errors({"probe": {"rt": [{key: value for key, value in getter_rt.items() if key != "function"}]}})
    check("T47b probe_rt function(B5 텍스처 게터)로 렌더 타깃 읽기·property 와 function 은 하나만",
          by_getter["probe"]["metrics"].get("probe.velocity.mean") == 1.0 and not lookdev.find_key_errors({"probe": {"rt": [getter_rt]}})
          and len(both) == 1 and len(neither) == 1 and "하나만" in both[0] + neither[0], (by_getter.get("probe"), both, neither))

    breaks = []
    WORLD.actors.append(FakeActor("TDGen_Breaker", trigger_break=lambda: breaks.append(CLOCK.game_seconds)))
    state = {"uses_tuning_reload": False, "key_errors": []}
    called = lookdev.apply_variant_with_tuning({"call": {"TDGen_Breaker": {"function": "trigger_break", "args": []}}, "schedule": {"times": [0.5, 1.0, 1.5], "reference": "call", "prefix": "call"}}, "s00", state)
    status = lookdev.wait_for_schedule(Path(called["schedule"]["status_path"]), 30.0)
    check("T56 call + schedule.reference call: 첫 호출 게임 시각 기준 촬영 3장",
          len(breaks) == 1 and called["call_game_time"] == breaks[0] and status.get("reference_game_time") == breaks[0] and status.get("done") and len(status.get("shots", [])) == 3
          and all(shot["saved"] and shot["captured_game_time"] >= shot["time"] for shot in status["shots"]) and not state["key_errors"], (called, status))

    events = [{"time": 2.0, "call": {"TDGen_Breaker": {"function": "trigger_break"}}}, {"time": 1.0, "console": ["r.CAL0.Value 1"]}, {"time": 1.5, "call": {"TDGen_Breaker": {"function": "missing_function"}}}]
    scheduled = lookdev.apply_variant_with_tuning({"schedule": {"times": [2.5], "reference": "now", "prefix": "events", "events": events}}, "s01", state)
    status = lookdev.wait_for_schedule(Path(scheduled["schedule"]["status_path"]), 30.0)
    done = status.get("events_done", [])
    event_errors = lookdev.find_schedule_event_errors(status)
    check("T57 schedule.events: 틱 콜백 안에서 시각 순서(1.0 콘솔·1.5·2.0 call)·게임 시각 기록·없는 함수는 key_errors 문구",
          [event["time"] for event in done] == [1.0, 1.5, 2.0] and all(event["game_time"] >= event["time"] for event in done) and done[0]["console"][0]["command"] == "r.CAL0.Value 1"
          and done[2]["call"]["TDGen_Breaker"]["function"] == "trigger_break" and len(breaks) == 2 and status.get("done") and status["shots"][0]["saved"]
          and event_errors == ["schedule.events t=1.5 call.TDGen_Breaker: 함수 없음: missing_function"], (done, event_errors))

    water = FakeActor("TDGen_Water", is_waiting_for_dense_water_cache=True)
    WORLD.actors.append(water)
    state = {"uses_tuning_reload": False, "key_errors": []}
    condition = {"label": "TDGen_Water", "property": "bIsWaitingForDenseWaterCache", "equals": False, "timeout": 0.5}
    timed_out = lookdev.apply_variant_with_tuning({"wait_until": condition}, "s00", state)
    water.is_waiting_for_dense_water_cache = False
    ready = lookdev.apply_variant_with_tuning({"wait_until": condition}, "s01", state)
    check("T58 wait_until 시간 초과 → key_errors / 조건 맞으면 통과",
          "시간 초과" in timed_out["wait"].get("error", "") and timed_out["wait"]["checks"] > 1 and ready["wait"].get("matched") is True
          and len(state["key_errors"]) == 1 and state["key_errors"][0].startswith("s00: wait_until: 시간 초과"), (timed_out.get("wait"), ready.get("wait"), state))


def check_cal0_sweep():
    sweep_dir = TEST_DIR / "cal0-sweep"
    sweep_dir.mkdir(parents=True, exist_ok=True)
    sha = fluid_tuning_patch.file_sha256
    flicker = load_json(CANONICAL)["StructDefaults"]["TDFluidSourceEmitterSettings"]["FlickerAmplitude"]
    vorticity_path = "Kinds.Campfire.TDFluidSimulationConfig.VorticityStrength"
    flicker_path = "Kinds.Campfire.TDFluidSourceEmitterSettings.FlickerAmplitude"
    torch_path = "Kinds.Torch.TDFluidSimulationConfig.VorticityStrength"
    fire_world = [(TDFluidFireActor, "TDGen_CampfireFire", FakePreset("CAMPFIRE")), (TDFluidFireActor, "TDGen_TorchFire", FakePreset("TORCH"))]

    def sweep_text(arguments):
        saved_argv = sys.argv
        sys.argv = ["calib_sweep.py"] + [str(argument) for argument in arguments]
        errors = io.StringIO()
        try:
            with contextlib.redirect_stderr(errors), contextlib.redirect_stdout(io.StringIO()):
                code = calib_sweep.main()
        finally:
            sys.argv = saved_argv
        return code, errors.getvalue()

    def stage_plan(name, mutate=None):
        plan = load_json(W / CAMPFIRE_PLAN)
        plan["scenes"][0].update(schedule=None, shots={"count": 3, "interval": 0.25})
        if mutate:
            plan = mutate(plan) or plan
        path = sweep_dir / f"{name}.json"
        path.write_text(json.dumps(plan, ensure_ascii=False), encoding="utf-8")
        return path

    def dry(name, path, *extra):
        code, text = sweep_text(["--plan", path, "--run-dir", sweep_dir / name, "--dry-run", *extra])
        record = sweep_dir / name / "sweep.json"
        return code, text, load_json(record) if code != 2 and record.is_file() else None

    def write_json(name, value):
        path = sweep_dir / name
        path.write_text(json.dumps(value, ensure_ascii=False), encoding="utf-8")
        return path

    def write_lock(name, plan_path, extra=(), version=2):
        plan = load_json(plan_path)
        files = [Path(plan_path)] + [W / scene[key] for scene in plan["scenes"] for key in ("regions_template", "targets") if scene.get(key) and (W / scene[key]).is_file()] + list(extra)
        lock = {"created_at": "2026-10-30T00:00:00", "files": {calib_sweep.relative_text(path): sha(path) for path in files}}
        if version:
            lock["version"] = version
        return write_json(f"{name}.lock.json", lock)

    def screen_file(name, plan_path, **fields):
        screen = {"version": 2, "plan_sha256": sha(plan_path), "overlay_sha256": None, "canonical_tuning_sha256": sha(CANONICAL), "canonical_look_sha256": sha(CANONICAL_LOOK),
                  "best": {"variant": "v02", "gain": 3.0}, "second": None, "insensitive": False}
        screen.update(fields)
        return write_json(f"{name}.screen.json", screen)

    def lookdev_spec(name, result, index=0):
        return load_json(sweep_dir / name / result["scenes"][index]["name"] / "lookdev_spec.json")

    def copy_value(name, variant_id, kind, struct_name, field_name):
        return load_json(sweep_dir / name / variant_id / "TDFluidPresetTuning.json")["Kinds"][kind][struct_name][field_name]

    phrases = {"no_split": "split_group: true", "unsupported_struct": "다시 읽기가 없어", "control_only": "다른 값이 없다", "no_targets": "필수 키 없음: targets", "empty_targets": "목표 지표 목록",
               "water_without_range": "low 와 high 는 함께", "range_reversed": "low < high", "weight_zero": "weight: 양수", "tolerance_negative": "tolerance: 0 이상",
               "duplicate_metric": "같은 지표가 두 번", "unknown_scene": "계획 장면 이름", "window_zero": "probe_window_scale: 양수", "control_middle_text": "control_middle: 불"}
    rebuilt = {"no_targets": lambda plan: plan["metrics"].pop("targets"), "water_without_range": lambda plan: plan["metrics"]["targets"][0].pop("low")}
    for name, mutate in rebuilt.items():
        plan = load_json(W / RIVER_PLAN)
        mutate(plan)
        write_json(f"phrase_{name}.json", plan)
    for name, phrase in phrases.items():
        plan_path = sweep_dir / f"phrase_{name}.json" if name in rebuilt else TEST_DIR / f"bad_{name}.json"
        code, text = sweep_text(["--plan", plan_path, "--run-dir", sweep_dir / f"phrase_{name}"])
        check(f"B2 기존 거부 {name}: 종료 2·사유 '{phrase}'", code == 2 and phrase in text, text[-300:])

    code, text, _ = dry("t01", stage_plan("t01", lambda plan: plan.update(base_tunning=[])))
    check("B2 T01 최상위 모르는 키 base_tunning: 종료 2·키 이름", code == 2 and "모르는 키 base_tunning" in text, text)
    code, text, _ = dry("t02", stage_plan("t02", lambda plan: plan["scenes"][0].update(awake={"TDGen_CampfireFire": True})))
    check("B2 T02 장면 awake: 종료 2·camera.awake 문구", code == 2 and "camera.awake" in text, text)
    code_param, text_param, _ = dry("t03a", stage_plan("t03a", lambda plan: plan["params"][0].update(step=0.1)))
    code_metrics, text_metrics, _ = dry("t03b", stage_plan("t03b", lambda plan: plan["metrics"].update(extra=1)))
    code_source, text_source, _ = dry("t03c", stage_plan("t03c", lambda plan: plan["scenes"][0].update(source="look")))
    code_shots, text_shots, _ = dry("t03d", stage_plan("t03d", lambda plan: plan["scenes"][0]["shots"].update(every=1)))
    check("B2 T03 param·metrics·장면 source·shots 모르는 키: 각각 종료 2·위치와 키",
          (code_param, code_metrics, code_source, code_shots) == (2, 2, 2, 2) and "params[0]: 모르는 키 step" in text_param and "metrics: 모르는 키 extra" in text_metrics
          and "sources 목록으로" in text_source and "shots: 모르는 키 every" in text_shots, (text_param, text_metrics, text_source, text_shots))
    code, text, _ = dry("t05", stage_plan("t05", lambda plan: plan.update(base_tuning=[{"json_path": vorticity_path, "value": 1.3}], base_tuning_ships=True)))
    check("B2 T05 base_tuning 경로 = params 경로: 종료 2", code == 2 and "base_tuning 경로가 params 경로와 같다" in text, text)
    draft_path = stage_plan("t06", lambda plan: plan.update(draft_status="draft"))
    code_real, text_real = sweep_text(["--plan", draft_path, "--run-dir", sweep_dir / "t06-real"])
    code_dry, _, record = dry("t06-dry", draft_path)
    check("B2 T06 draft_status: 실제 실행 종료 2 / 건식 0·경고", code_real == 2 and "draft_status" in text_real and code_dry == 0 and any("draft_status" in line for line in record["warnings"]), (text_real, record and record["warnings"]))

    two_knobs = lambda plan: plan["params"].append({"json_path": flicker_path, "values": [flicker * 0.5, flicker, flicker * 2.0]})
    two_path = stage_plan("t08", two_knobs)
    code, text, record = dry("t08", two_path, "--stage", "screen")
    check("B2 T08 screen P=2: v00,a_lo,a_hi,v00,b_lo,b_hi,v00·끝 값만·반복 1", code == 0 and record["sequence"] == ["v00", "v01", "v02", "v00", "v03", "v04", "v00"] and record["scenes"][0]["sequence"] == record["sequence"]
          and [variant["values"] for variant in record["variants"][1:]] == [[1.2, flicker], [1.8, flicker], [1.5, flicker * 0.5], [1.5, flicker * 2.0]], (text, record and record["sequence"]))
    code, text, record = dry("t08b", stage_plan("t08b"), "--stage", "screen")
    check("B2 T08b screen P=1: v00,lo,v00,hi,v00(대조 최소 3)", code == 0 and record["sequence"] == ["v00", "v01", "v00", "v02", "v00"], (text, record and record["sequence"]))
    code, text, record = dry("t09", two_path, "--stage", "confirm", "--screen-result", screen_file("t09", two_path))
    code_second, _, record_second = dry("t09b", two_path, "--stage", "confirm", "--screen-result", screen_file("t09b", two_path, second={"variant": "v03", "gain": 2.5}))
    check("B2 T09 confirm: v00,best,v00,best,v00 / second 있으면 v00,best,v00,second,v00·screen sha 기록",
          code == 0 and record["sequence"] == ["v00", "v02", "v00", "v02", "v00"] and record["screen_result_sha256"] == sha(sweep_dir / "t09.screen.json")
          and code_second == 0 and record_second["sequence"] == ["v00", "v02", "v00", "v03", "v00"] and [variant["id"] for variant in record_second["variants"]] == ["v00", "v02", "v03"], (text, record and record["sequence"]))
    code_plan, text_plan, _ = dry("t10", two_path, "--stage", "confirm", "--screen-result", screen_file("t10", two_path, plan_sha256="0" * 64))
    code_look, text_look, _ = dry("t10b", two_path, "--stage", "confirm", "--screen-result", screen_file("t10b", two_path, canonical_look_sha256="0" * 64))
    code_dull, text_dull, _ = dry("t10c", two_path, "--stage", "confirm", "--screen-result", screen_file("t10c", two_path, insensitive=True))
    code_missing, text_missing, _ = dry("t10d", two_path, "--stage", "confirm")
    check("B2 T10 screen.json plan·정본 sha 불일치 종료 2 / 둔감 confirm 거부 종료 1 / screen-result 없음 종료 2",
          (code_plan, code_look, code_dull, code_missing) == (2, 2, 1, 2) and "plan_sha256 불일치" in text_plan and "canonical_look_sha256 불일치" in text_look and "confirm 거부" in text_dull and "--screen-result" in text_missing,
          (text_plan, text_dull, text_missing))
    code, text, record = dry("t11", stage_plan("t11", lambda plan: plan["scenes"][0].update(shots={"count": 5, "interval": 0.25})), "--stage", "screen")
    spec = lookdev_spec("t11", record) if record else {"variants": [{}]}
    schedule = spec["variants"][0].get("apply", {}).get("schedule", {})
    check("B2 T11 shots{5, 0.25} settle 6 → schedule times [6..7]·reference now·fixed_fps 60·모든 단계", code == 0 and schedule.get("times") == [6.0, 6.25, 6.5, 6.75, 7.0] and schedule.get("reference") == "now"
          and schedule.get("fixed_fps") == 60 and all(step["apply"]["schedule"] == schedule for step in spec["variants"]) and record["scenes"][0]["shots_expected"] == 5, (text, schedule))
    code, text, record = dry("t12", stage_plan("t12"), "--stage", "frame")
    no_regions = stage_plan("t12b", lambda plan: plan["scenes"][0].update(regions_template="Tools/FluidFX/calib/regions/cal0_missing.json"))
    code_frame, _, _ = dry("t12b", no_regions, "--stage", "frame")
    code_screen, text_screen, _ = dry("t12c", no_regions, "--stage", "screen")
    check("B2 T12 frame: v00 1단계·N장 schedule / 영역 틀 없어도 frame 0·screen 은 종료 2 '영역 틀 없음'",
          code == 0 and record["sequence"] == ["v00"] and len(record["variants"]) == 1 and len(lookdev_spec("t12", record)["variants"][0]["apply"]["schedule"]["times"]) == 3
          and code_frame == 0 and code_screen == 2 and "영역 틀 없음" in text_screen, (text, text_screen))

    overlay_path = write_json("overlay.json", {"version": 2, "assignments": [{"json_path": torch_path, "value": 2.5, "split_group": False, "kind": "knob"}], "actor_props": [], "history": []})
    code, text, _ = dry("t18", stage_plan("t18"), "--stage", "screen", "--overlay", write_json("overlay-same.json", {"version": 2, "assignments": [{"json_path": vorticity_path, "value": 1.6}], "actor_props": []}))
    check("B2 T18 덧씌움 경로 = params 경로: 종료 2", code == 2 and "덧씌움 경로가 계획 params·base_tuning 경로와 같다" in text, text)
    base_path = stage_plan("t04", lambda plan: plan.update(base_tuning=[{"json_path": torch_path, "value": 2.5}], base_tuning_ships=True))
    code, text, record = dry("t04-dry", base_path, "--stage", "screen")
    check("B2 T04 사본: base_tuning 이 대조·변형 사본 모두에·변형은 base 위에 손잡이", code == 0 and copy_value("t04-dry", "v00", "Torch", "TDFluidSimulationConfig", "VorticityStrength") == 2.5
          and copy_value("t04-dry", "v01", "Torch", "TDFluidSimulationConfig", "VorticityStrength") == 2.5 and copy_value("t04-dry", "v01", "Campfire", "TDFluidSimulationConfig", "VorticityStrength") == 1.2
          and copy_value("t04-dry", "v00", "Campfire", "TDFluidSimulationConfig", "VorticityStrength") == 1.5 and all(variant["expect"][-1]["role"] == "base_tuning" for variant in record["variants"]), text)
    reset_world(fire_world)
    code, text = sweep_text(["--plan", base_path, "--run-dir", sweep_dir / "t04", "--stage", "screen", "--lock", write_lock("t04", base_path)])
    record = load_json(sweep_dir / "t04" / "sweep.json") if (sweep_dir / "t04" / "sweep.json").is_file() else {"scenes": [{"steps": []}]}
    steps = record["scenes"][0]["steps"]
    spec = lookdev_spec("t04", record) if code != 2 else {"variants": []}
    check("B2 T04 가짜 PIE(B3 base 필요): 대조 ChangedFieldCount 0·변형 ≥1·모든 단계 다시 읽기에 base 경로·base 사본 요청", code == 0 and [step["report"]["changed_field_count"] for step in steps] == [0, 1, 0, 1, 0]
          and all(step["valid"] for step in steps) and all(step["apply"]["tuning_reload"]["read_back"][-1]["kinds"] == ["Torch"] and step["apply"]["tuning_reload"]["base"]["tuning"] == record["base_copy"]["tuning"] for step in spec["variants"]),
          record.get("problems"))
    reset_world(fire_world)
    verify_path = stage_plan("t17")
    verify_lock = write_lock("t17", verify_path)
    code, text = sweep_text(["--plan", verify_path, "--run-dir", sweep_dir / "t17", "--stage", "verify", "--lock", verify_lock, "--overlay", overlay_path])
    record = load_json(sweep_dir / "t17" / "sweep.json") if (sweep_dir / "t17" / "sweep.json").is_file() else {"scenes": [{"steps": []}]}
    steps = record["scenes"][0]["steps"]
    check("B2 T17 덧씌움 가짜 PIE verify(B3 base 필요): 대조 3단계 ChangedFieldCount 0·overlay sha·lock sha 기록·덧씌움 경로 다시 읽기 단언", code == 0 and record["sequence"] == ["v00", "v00", "v00"]
          and [step["report"]["changed_field_count"] for step in steps] == [0, 0, 0] and record["overlay_sha256"] == sha(overlay_path) and record["lock_sha256"] == sha(verify_lock)
          and all(variant["expect"][-1]["role"] == "덧씌움" for variant in record["variants"]), record.get("problems"))

    variant = {"is_control": True, "counts_change": False, "tuning": str(CANONICAL), "look_live": {}, "props": {},
               "expect": [{"struct": "TDFluidSimulationConfig", "field": "VorticityStrength", "kinds": ["Torch"], "value": 2.5, "role": "덧씌움", "owner": "live_component", "access": "simulation_config"}]}
    outcome = lambda actors: {"tuning_reload": {"report": {"changed_field_count": 0, "tuning_source_path": str(CANONICAL)}, "read_back": [{"actors": actors}]}}
    missed = calib_sweep.judge_step(variant, outcome({"TDGen_TorchFire": {"value": 3.0}}))
    check("B2 T69 덧씌움 경로 다시 읽기 값이 다르면 대조 단계 무효 / 같으면 유효 / 그 종류가 장면에 없으면 단언 생략",
          len(missed) == 1 and "덧씌움 경로" in missed[0] and calib_sweep.judge_step(variant, outcome({"TDGen_TorchFire": {"value": 2.5}})) == [] and calib_sweep.judge_step(variant, outcome({})) == [], missed)

    relative = lambda role: lambda plan: plan["metrics"]["targets"][0].update(relative_to_control={"low": 0.9, "high": 1.1}, **({"role": role} if role else {}))
    code_score, text_score, _ = dry("t39a", stage_plan("t39a", relative(None)), "--stage", "screen")
    code_constraint, _, _ = dry("t39b", stage_plan("t39b", relative("constraint")), "--stage", "screen")
    check("B2 T39 relative_to_control [0.9,1.1] 은 1.0 포함 → score 면 종료 2 / constraint 면 0", code_score == 2 and "대조 중심 띠" in text_score and code_constraint == 0, text_score)

    def probe_plan(plan):
        scene = plan["scenes"][0]
        scene.pop("shots")
        scene.update(sources=["probe"], probe={"stats": [{"label": "TDGen_CampfireFire", "component": "live_component", "function": "get_cal0_probe_stats", "prefix": "live"}], "times": [1.0, 1.5, 2.0]})
        plan["metrics"].update(sources=["probe"], targets=[{"metric": "probe.live.mean_speed", "weight": 1.0, "low": 0.0, "high": 10.0, "tolerance": 0.01}])
    reset_world(fire_world)
    probe_path = stage_plan("t49", probe_plan)
    with patched(FakeLive, get_cal0_probe_stats=lambda self: Props(mean_speed=self.simulation_config.vorticity_strength)):
        code, text = sweep_text(["--plan", probe_path, "--run-dir", sweep_dir / "t49", "--stage", "screen", "--lock", write_lock("t49", probe_path)])
    record = load_json(sweep_dir / "t49" / "sweep.json") if (sweep_dir / "t49" / "sweep.json").is_file() else {"scenes": [{"steps": []}]}
    steps = record["scenes"][0]["steps"]
    check("B2 T49 sources [probe] 가짜 PIE(B3 probe 필요): 단계마다 probe.* 지표·metrics sha", code == 0 and steps and all(step["metrics"] and all(key.startswith("probe.") for key in step["metrics"]) and step["metrics_sha256"] for step in steps),
          (text[-300:], record.get("problems")))
    shot_values = [calib_judge.probe_sample_values(step) for step in steps]
    check("통합 (b) probe 단계 기록 probe_samples = 룩데브 probe.samples(times 3장) → 판정기 장별 표본 probe.* 3개",
          code == 0 and steps and all(len(step.get("probe_samples", [])) == 3 and all("time" in sample for sample in step["probe_samples"]) for step in steps)
          and all(len(shots) == 3 and all(key.startswith("probe.") for shot in shots for key in shot) for shots in shot_values), shot_values[:1])

    code, text, record = dry("t59", stage_plan("t59", lambda plan: plan["scenes"][0].update(step_apply={"trigger": True})), "--stage", "screen")
    check("B2 T59 step_apply(trigger) 가 모든 단계 apply 에", code == 0 and all(step["apply"].get("trigger") is True and "tuning_reload" in step["apply"] for step in lookdev_spec("t59", record)["variants"]), text)
    linked = "Kinds.Torch.TDFluidSourceEmitterSettings.FlickerAmplitude"
    code, text, record = dry("t60", stage_plan("t60", lambda plan: plan.update(params=[{"json_paths": [flicker_path, linked], "values": [flicker, flicker * 2.0]}])), "--stage", "screen")
    code_diff, text_diff, _ = dry("t60b", stage_plan("t60b", lambda plan: plan.update(params=[{"json_paths": [vorticity_path, torch_path], "values": [1.5, 2.0]}])), "--stage", "screen")
    check("B2 T60 json_paths 연동: 두 경로 같은 값·다시 읽기 2개 / 기준 다르면 종료 2", code == 0 and copy_value("t60", "v01", "Campfire", "TDFluidSourceEmitterSettings", "FlickerAmplitude") == flicker * 2.0
          and copy_value("t60", "v01", "Torch", "TDFluidSourceEmitterSettings", "FlickerAmplitude") == flicker * 2.0 and len(record["variants"][1]["expect"]) == 2
          and code_diff == 2 and "기준 값이 다르다" in text_diff, (text, text_diff))

    def two_scenes(param_scenes):
        def mutate(plan):
            second = copy.deepcopy(plan["scenes"][0])
            second["name"] = "campfire_b"
            plan["scenes"].append(second)
            for target in plan["metrics"]["targets"]:
                target["scene"] = "campfire"
            plan["params"][0]["scenes"] = param_scenes
            plan["params"].append({"json_path": flicker_path, "values": [flicker * 0.5, flicker * 2.0]})
        return mutate
    code, text, record = dry("t61", stage_plan("t61", two_scenes(["campfire"])), "--stage", "screen")
    code_unknown, text_unknown, _ = dry("t61b", stage_plan("t61b", two_scenes(["nowhere"])), "--stage", "screen")
    check("B2 T61 param scenes(장면 A만): 장면 B 순서에 그 변형 없음 / 모르는 장면 종료 2", code == 0 and record["scenes"][0]["sequence"] == ["v00", "v01", "v02", "v00", "v03", "v04", "v00"]
          and record["scenes"][1]["sequence"] == ["v00", "v03", "v00", "v04", "v00"] and code_unknown == 2 and "장면에 없는 이름" in text_unknown, (text, record and [scene["sequence"] for scene in record["scenes"]]))
    def no_scene(plan):
        two_scenes(None)(plan)
        plan["params"][0].pop("scenes")
        plan["metrics"]["targets"][1].pop("scene")
    code, text, _ = dry("t76", stage_plan("t76", no_scene), "--stage", "screen")
    check("B2 T76 장면 2개 계획 target 에 scene 없음: 건식 종료 2", code == 2 and "모든 target 에 scene" in text, text)
    prop = {"label": "TDGen_CampfireFire", "property": "cal0_glow"}
    code, text, record = dry("t62", stage_plan("t62", lambda plan: plan.update(params=[{"actor_prop": prop, "base": 1.0, "values": [1.0, 2.0]}])), "--stage", "screen")
    code_base, text_base, _ = dry("t62b", stage_plan("t62b", lambda plan: plan.update(params=[{"actor_prop": prop, "values": [1.0, 2.0]}])), "--stage", "screen")
    spec_steps = lookdev_spec("t62", record)["variants"] if record else []
    check("B2 T62 actor_prop: 조절 사본 = 대조 = 정본 바이트·단계 apply props(대조는 base)·base 없으면 종료 2",
          code == 0 and all((sweep_dir / "t62" / variant_id / "TDFluidPresetTuning.json").read_bytes() == CANONICAL.read_bytes() for variant_id in ("v00", "v01"))
          and [step["apply"]["props"]["TDGen_CampfireFire"]["cal0_glow"] for step in spec_steps] == [1.0, 2.0, 1.0, 1.0] and code_base == 2 and "actor_prop 은 base" in text_base, (text, text_base))

    script = sweep_dir / "fake_offline.py"
    script.write_text("import json\nimport sys\narguments = dict(zip(sys.argv[1::2], sys.argv[2::2]))\ntuning = json.load(open(arguments['--tuning'], encoding='utf-8'))\n"
                      "value = tuning['Kinds']['Campfire']['TDFluidSimulationConfig']['VorticityStrength']\nopen(arguments['--json'], 'w', encoding='utf-8').write(json.dumps({'x': value * 2}))\n"
                      "sys.exit(int(arguments['--exit']))\n", encoding="utf-8")

    def offline_plan(exit_code):
        def mutate(plan):
            scene = plan["scenes"][0]
            scene.pop("shots")
            scene.update(sources=["offline"], offline={"command": [str(script), "--tuning", "{tuning}", "--json", "{output}", "--exit", str(exit_code)]})
            plan["metrics"].update(sources=["offline"], targets=[{"metric": "offline.x", "weight": 1.0, "low": 2.0, "high": 4.0, "tolerance": 0.0}])
        return mutate
    offline_path = stage_plan("t63", offline_plan(0))
    offline_lock = write_lock("t63", offline_path, [script])
    code, text = sweep_text(["--plan", offline_path, "--run-dir", sweep_dir / "t63", "--stage", "screen", "--lock", offline_lock])
    record = load_json(sweep_dir / "t63" / "sweep.json") if code != 2 else {"scenes": [{"steps": []}]}
    steps = record["scenes"][0]["steps"]
    check("B2 T63 offline 명령(가짜 스크립트 사본 값 ×2): offline.x = 2×값·2회 sha 같음·lock sha 기록", code == 0 and [round(step["metrics"].get("offline.x", 0), 9) for step in steps] == [3.0, 2.4, 3.0, 3.6, 3.0]
          and all(step["offline"]["output_sha256"] == step["offline"]["repeat_sha256"] for step in steps) and record["lock_sha256"] == sha(offline_lock), (text, record.get("problems")))
    failing_path = stage_plan("t64", offline_plan(1))
    code, text = sweep_text(["--plan", failing_path, "--run-dir", sweep_dir / "t64", "--stage", "screen", "--lock", write_lock("t64", failing_path, [script])])
    record = load_json(sweep_dir / "t64" / "sweep.json") if code != 2 else {"scenes": [{"steps": [{"valid": True, "errors": []}]}]}
    check("B2 T64 offline 명령 종료 1: 단계 무효·스윕 종료 1", code == 1 and all(not step["valid"] and "offline 명령 종료 1" in step["errors"][0] for step in record["scenes"][0]["steps"]), (code, text))

    one_shot = stage_plan("t72", lambda plan: plan["scenes"][0].update(shots={"count": 1, "interval": 0.25}))
    code, text, _ = dry("t72", one_shot, "--stage", "screen")
    code_frame, _, _ = dry("t72b", one_shot, "--stage", "frame")
    check("B2 T72 look shots.count 1 단계 실행: 건식 종료 2 '표본 ≥3' / frame 은 0", code == 2 and "표본 ≥3" in text and code_frame == 0, text)
    code, text, _ = dry("t84", stage_plan("t84", lambda plan: plan.update(base_tuning=[{"json_path": torch_path, "value": 2.5}])), "--stage", "screen")
    code_ships, _, record = dry("t84b", stage_plan("t84b", lambda plan: plan.update(base_tuning=[{"json_path": torch_path, "value": 2.5}], base_tuning_ships=False)), "--stage", "screen")
    check("B2 T84 base_tuning 있음 + ships 없음 종료 2 / ships 불이면 0·기록", code == 2 and "base_tuning_ships" in text and code_ships == 0 and record["base_tuning_ships"] is False, text)

    lock_plan = stage_plan("t90")
    code_none, text_none = sweep_text(["--plan", lock_plan, "--run-dir", sweep_dir / "t90a", "--stage", "screen"])
    code_v1, text_v1 = sweep_text(["--plan", lock_plan, "--run-dir", sweep_dir / "t90b", "--stage", "screen", "--lock", write_lock("t90b", lock_plan, version=None)])
    stale_lock = write_lock("t90c", lock_plan)
    lock_plan.write_text(lock_plan.read_text(encoding="utf-8") + " ", encoding="utf-8")
    code_stale, text_stale = sweep_text(["--plan", lock_plan, "--run-dir", sweep_dir / "t90c", "--stage", "screen", "--lock", stale_lock])
    short_lock = write_json("t90d.lock.json", {"version": 2, "created_at": "2026-10-30T00:00:00", "files": {calib_sweep.relative_text(lock_plan): sha(lock_plan)}})
    code_short, text_short = sweep_text(["--plan", lock_plan, "--run-dir", sweep_dir / "t90d", "--stage", "screen", "--lock", short_lock])
    check("B2 T90 단계 실제 실행 --lock 없음·v1 잠금·sha 불일치·필수 파일 누락: 각각 종료 2", (code_none, code_v1, code_stale, code_short) == (2, 2, 2, 2) and "--lock" in text_none
          and "version 2" in text_v1 and "sha256 불일치" in text_stale and "필수 파일 누락" in text_short, (text_none, text_v1, text_stale, text_short))

    def series(prefixed):
        def mutate(plan):
            plan["scenes"][0]["shots"]["kind"] = "series"
            if not prefixed:
                plan["metrics"]["targets"][0]["metric"] = "campfire.interior_billow_contrast_s4_s24"
        return mutate
    code, text, _ = dry("t93", stage_plan("t93", series(False)), "--stage", "screen")
    code_prefixed, _, _ = dry("t93b", stage_plan("t93b", series(True)), "--stage", "screen")
    check("B2 T93 series 장면 + 접두 없는 target: 건식 종료 2 / 접두 있으면 0", code == 2 and "접두" in text and code_prefixed == 0, text)
    code, text, _ = dry("t94", stage_plan("t94", lambda plan: plan["scenes"][0]["camera"].update(veiw_target="TDGen_CampfireFire")), "--stage", "screen")
    check("B2 T94 camera 오타 veiw_target(B3 find_key_errors 필요): 건식 종료 2·키 이름", code == 2 and "veiw_target" in text, text)
    overlay_base = write_json("overlay-base.json", {"version": 2, "assignments": [{"json_path": "StructDefaults.TDFluidSourceEmitterSettings.FlickerAmplitude", "value": 0.4, "split_group": False, "kind": "knob"}], "actor_props": []})
    base_knob = stage_plan("t95", lambda plan: plan.update(params=[{"json_path": flicker_path, "values": [flicker, 0.4, 0.6]}]))
    code, text, record = dry("t95", base_knob, "--stage", "screen", "--overlay", overlay_base)
    code_plain, _, record_plain = dry("t95b", base_knob, "--stage", "screen")
    check("B2 T95 describe_params base_value = 정본+덧씌움 값·screen 끝 값은 그 기준을 뺀 최소·최대", code == 0 and record["params"][0]["base_value"] == 0.4 and [variant["values"] for variant in record["variants"][1:]] == [[flicker], [0.6]]
          and code_plain == 0 and record_plain["params"][0]["base_value"] == flicker and [variant["values"] for variant in record_plain["variants"][1:]] == [[0.4], [0.6]], (text, record and record["params"][0]["base_value"]))
    golden = "StructDefaults.TDFluidSimulationConfig.VorticityStrength"
    code_param, text_param, _ = dry("t96", stage_plan("t96", lambda plan: plan.update(params=[{"json_path": golden, "values": [1.0, 1.2]}])), "--stage", "screen")
    code_base, _, _ = dry("t96b", stage_plan("t96b", lambda plan: plan.update(base_tuning=[{"json_path": golden, "value": 1.2}], base_tuning_ships=True)), "--stage", "screen")
    code_overlay, _, _ = dry("t96c", stage_plan("t96c"), "--stage", "screen", "--overlay", write_json("overlay-golden.json", {"version": 2, "assignments": [{"json_path": golden, "value": 1.2}], "actor_props": []}))
    check("B2 T96 StructDefaults.TDFluidSimulationConfig.* params·base_tuning·덧씌움: 각각 종료 2 '골든 입력'", (code_param, code_base, code_overlay) == (2, 2, 2) and "골든 입력" in text_param, text_param)
    look_missing = "Look:Materials./Game/CAL0/MI_None.Scalars.Glow"
    code_missing, text_missing, _ = dry("look-a", stage_plan("look-a", lambda plan: plan.update(params=[{"json_path": look_missing, "values": [1.0, 2.0]}])), "--stage", "screen")
    code_switch, text_switch, _ = dry("look-b", stage_plan("look-b", lambda plan: plan.update(params=[{"json_path": "Look:Materials./Game/CAL0/MI_None.Switches.Glow", "values": [True]}])), "--stage", "screen")
    check("B2 G-LOOK 검사: 정본 모양에 없는 키인데 base 없음·Switches 각각 종료 2", code_missing == 2 and "base(직접 부모 기본값)" in text_missing and code_switch == 2 and "Switches" in text_switch, (text_missing, text_switch))

    reed = load_json(CANONICAL)
    reed["Kinds"]["ReedShallows"]["TDFluidSimulationConfig"]["BendVectorRiseSeconds"] = 0.2
    reed_path = write_json("reed_tuning.json", reed)
    bend = "Tools/FluidFX/check_fluid_foliage_bend.py"
    runs = [python(bend, "--tuning", str(path), "--kind", "ReedShallows", "--json", str(sweep_dir / name)) for path, name in ((CANONICAL, "reed0.json"), (CANONICAL, "reed0b.json"), (reed_path, "reed1.json"))]
    outputs = [load_json(sweep_dir / name) if (sweep_dir / name).is_file() else {} for name in ("reed0.json", "reed0b.json", "reed1.json")]
    check("B2 T97 갈대 offline(B1 effective_value 필요): 사본 값 변경 → ripple_lag 변화·같은 사본 2회 sha 같음·실효 입력·unmapped",
          all(code == 0 for code, _ in runs) and sha(sweep_dir / "reed0.json") == sha(sweep_dir / "reed0b.json")
          and outputs[0]["ripple_lag_seconds"]["equal_blend"] != outputs[2]["ripple_lag_seconds"]["equal_blend"] and outputs[2]["inputs"]["LAG_RISE_SECONDS"]["value"] == 0.2
          and outputs[0]["inputs"]["LAG_RISE_SECONDS"]["value"] == 0.08 and "LAG_FREQUENCY_HZ" in outputs[0]["unmapped"], [output[-300:] for _, output in runs])

    def staged_run(name, plan_path, *extra):
        code, text = sweep_text(["--plan", plan_path, "--run-dir", sweep_dir / name, "--stage", "screen", "--lock", write_lock(name, plan_path), *extra])
        record_path = sweep_dir / name / "sweep.json"
        record = load_json(record_path) if record_path.is_file() else {}
        return code, text, [step for scene in record.get("scenes", []) for step in scene.get("steps", [])]

    def step_errors(steps):
        return sorted({error for step in steps for error in step["errors"]})

    glow = {"label": "TDGen_CampfireFire", "property": "cal0_glow"}
    reset_world(fire_world)
    WORLD.actors[0].cal0_glow = 1.0
    code, text, steps = staged_run("fix-s2", stage_plan("fix-s2", lambda plan: plan.update(params=[{"actor_prop": glow, "base": 1.0, "values": [1.0, 2.0]}])))
    check("CAL-0 반박 S2 actor_prop 만 있는 계획(조절 다시 읽기 요청 []): 룩데브가 read_back [] 을 남겨 단계 전부 유효·종료 0",
          code == 0 and steps and all(step["valid"] for step in steps), (code, text[-300:], step_errors(steps)[:4]))

    missing = {"label": "TDGen_Missing", "property": "cal0_glow"}
    reset_world(fire_world)
    WORLD.actors[0].cal0_glow = 1.0
    code_param, text_param, param_steps = staged_run("fix-s3a", stage_plan("fix-s3a", lambda plan: plan["params"].append({"actor_prop": missing, "base": 1.0, "values": [1.0, 2.0]})))
    campfire_level = load_json(W / CAMPFIRE_PLAN)["scenes"][0]["level"]
    missing_overlay = write_json("fix-s3-overlay.json", {"version": 2, "assignments": [], "actor_props": [dict(missing, level=campfire_level, value=2.0)], "history": []})
    reset_world(fire_world)
    WORLD.actors[0].cal0_glow = 1.0
    code_overlay, text_overlay, overlay_steps = staged_run("fix-s3b", stage_plan("fix-s3b"), "--overlay", missing_overlay)
    check("CAL-0 반박 S3 없는 라벨 actor_prop(계획 손잡이·덧씌움): '액터 없음' 으로 단계 무효·종료 1",
          code_param == 1 and any("액터 없음" in error for error in step_errors(param_steps)) and code_overlay == 1 and overlay_steps and not any(step["valid"] for step in overlay_steps),
          (code_param, text_param[-200:], step_errors(param_steps)[:4], code_overlay, text_overlay[-200:], step_errors(overlay_steps)[:4]))

    look_package, look_name = "/Game/FluidFX/Materials/Debug/MI_TD_FluidBufferView_Aux", "DebugBias"
    look_path = f"Look:Materials.{look_package}.Scalars.{look_name}"
    reset_world(fire_world)
    look_material = FakeMaterialInstanceConstant(look_package, scalars={look_name: 0.0}, parent=FakeMaterial("/Game/CAL0/M_Look", scalars={look_name: 0.0}, vectors={"Tint": FakeLinearColor(1.0, 1.0, 1.0, 1.0)}))
    WORLD.add_asset(look_material)
    WORLD.actors.append(FakeActor("TDGen_LookProp", components=[FakeStaticMeshComponent("Mesh", [look_material])]))
    code, text, look_steps = staged_run("fix-s1", stage_plan("fix-s1", lambda plan: plan["params"].append({"json_path": look_path, "base": 0.0, "values": [0.0, 1.0]})))
    check("CAL-0 반박 S1 Look 손잡이: 룩데브 최상위 look_applied·look_read_back({expected, read, matches})로 판정 → 단계 전부 유효·종료 0·단계 기록 look_read_back",
          code == 0 and look_steps and all(step["valid"] and step.get("look_read_back") for step in look_steps), (code, text[-300:], step_errors(look_steps)[:4]))

    river_level = load_json(W / RIVER_PLAN)["scenes"][0]["level"]
    river_overlay = write_json("fix-s1-river-overlay.json", {"version": 2, "assignments": [{"json_path": look_path, "value": 0.5, "split_group": False}],
                                                            "actor_props": [{"level": river_level, "label": "TDGen_River", "property": "cal0_glow", "value": 2.0}], "history": []})
    reset_world([(TDFluidLiveActor, "TDGen_River", FakePreset("RIVER", vorticity=0.3)), (TDFluidLiveActor, "TDGen_Sea", FakePreset("SEA", vorticity=0.3))])
    WORLD.actors[0].cal0_glow = 1.0
    code, text = sweep_text(["--plan", W / RIVER_PLAN, "--run-dir", sweep_dir / "fix-s1-river", "--overlay", river_overlay])
    river_record = sweep_dir / "fix-s1-river" / "sweep.json"
    river_steps = load_json(river_record)["scenes"][0]["steps"] if river_record.is_file() else []
    check("CAL-0 반박 S1 물 탐침 장면 + 덧씌움 Look·actor_props: Look 은 물리 탐침이라 건너뜀 기록, actor_props 는 적용 → 단계 전부 유효·종료 0",
          code == 0 and river_steps and all(step["valid"] and step.get("look_live_skipped") for step in river_steps) and WORLD.actors[0].cal0_glow == 2.0,
          (code, text[-300:], step_errors(river_steps)[:4], WORLD.actors[0].cal0_glow))

    def look_overlay(name, **entry_extra):
        return write_json(name, {"version": 2, "assignments": [dict({"json_path": look_path, "value": 0.5, "split_group": False}, **entry_extra)], "actor_props": [], "history": []})

    ownerless_runs = {}
    for name, plan_edit, extra in (("fix-n1a", None, ("--overlay", look_overlay("fix-n1a-overlay.json"))),
                                   ("fix-n1b", lambda plan: plan.update(base_tuning=[{"json_path": look_path, "value": 0.5}], base_tuning_ships=True), ()),
                                   ("fix-n1c", None, ("--overlay", look_overlay("fix-n1c-overlay.json", levels=["/Game/CAL0/LV_Other"]))),
                                   ("fix-n1d", None, ("--overlay", look_overlay("fix-n1d-overlay.json", levels=[campfire_level]))),
                                   ("fix-n1e", lambda plan: plan["params"].append({"json_path": look_path, "base": 0.0, "values": [0.0, 1.0]}), ())):
        reset_world(fire_world)
        ownerless_runs[name] = staged_run(name, stage_plan(name, plan_edit), *extra)
    skipped_runs = [ownerless_runs[name] for name in ("fix-n1a", "fix-n1b", "fix-n1c")]
    check("CAL-0 재반박 N1 덧씌움·base_tuning Look 이 이 수준에 소유자 없음(levels 없음·다른 수준) → 적용·다시 읽기 건너뜀 skipped_look·단계 유효·종료 0 / 이 수준 levels·손잡이 Look 은 엄격(종료 1)",
          all(code == 0 and steps and all(step["valid"] and step.get("skipped_look") == [look_package] for step in steps) for code, _, steps in skipped_runs)
          and ownerless_runs["fix-n1d"][0] == 1 and ownerless_runs["fix-n1e"][0] == 1 and any("look 적용 대상 없음" in error for error in step_errors(ownerless_runs["fix-n1e"][2])),
          {name: (code, text[-160:], step_errors(steps)[:3]) for name, (code, text, steps) in ownerless_runs.items()})

    def cloud_setup_plan(setup):
        def mutate(plan):
            if setup is not None:
                plan["scenes"][0]["camera"]["console"] = [f"TD.FluidFX.CloudSetup {setup}"]
        return mutate

    campfire_scene_name = load_json(W / CAMPFIRE_PLAN)["scenes"][0]["name"]
    setup_overlay = look_overlay("fix-n1f-overlay.json", levels=[campfire_level], cloud_setups=[9])
    setup_runs = {}
    for name, setup, overlay_path in (("fix-n1f", 12, setup_overlay), ("fix-n1g", 9, setup_overlay), ("fix-n1h", None, setup_overlay),
                                      ("fix-n1i", 12, look_overlay("fix-n1i-overlay.json", levels=[campfire_level], cloud_setups=["9"]))):
        reset_world(fire_world)
        setup_runs[name] = staged_run(name, stage_plan(name, cloud_setup_plan(setup)), "--overlay", overlay_path)
    setup_spec_path = sweep_dir / "fix-n1f" / campfire_scene_name / "lookdev_spec.json"
    setup_spec_steps = load_json(setup_spec_path)["variants"] if setup_spec_path.is_file() else []
    setup_record_path = sweep_dir / "fix-n1f" / "sweep.json"
    setup_scene_entries = load_json(setup_record_path)["scenes"] if setup_record_path.is_file() else []
    check("K9 G 같은 레벨 구름 셋업: 덧씌움 Look levels=[이 레벨]·cloud_setups=[9] 는 CloudSetup 12 장면에서 skip_if_no_owner·skipped_look·단계 유효·종료 0(장면 cloud_setup 12) / CloudSetup 9 장면·CloudSetup 줄 없는 장면은 엄격(종료 1 'look 적용 대상 없음') / 정수 아닌 cloud_setups 는 덧씌움 입력 오류(종료 2)",
          setup_runs["fix-n1f"][0] == 0 and setup_runs["fix-n1f"][2] and all(step["valid"] and step.get("skipped_look") == [look_package] for step in setup_runs["fix-n1f"][2])
          and setup_spec_steps and all(look_package in step["apply"]["look_live"].get("skip_if_no_owner", []) for step in setup_spec_steps)
          and [entry.get("cloud_setup") for entry in setup_scene_entries] == [12]
          and all(setup_runs[name][0] == 1 and any("look 적용 대상 없음" in error for error in step_errors(setup_runs[name][2])) for name in ("fix-n1g", "fix-n1h"))
          and setup_runs["fix-n1i"][0] == 2,
          {name: (code, text[-160:], step_errors(steps)[:3]) for name, (code, text, steps) in setup_runs.items()})

    cloud_level = "/Game/FluidFX/Maps/LV_TDFluidCloudStage"
    setup_plan = {"id": "k9g_setup", "scenes": [{"name": "shield", "level": cloud_level, "camera": {"console": ["TD.FluidFX.CloudSetup 8", "TD.FluidFX.CloudSetup 9"]}},
                                                {"name": "stratus", "level": cloud_level, "camera": {"console": ["TD.FluidFX.CloudSetup 12"]}},
                                                {"name": "plain", "level": campfire_level, "camera": {}}],
                  "params": [{"json_path": look_path, "scenes": ["shield"]}]}
    setup_judge_path = write_json("fix-n1j-judge.json", {"plan": "k9g_setup.json", "adopt": {"variant": "v01"}})

    def adopted_look_entry(plan, json_path):
        knobs, _, _ = fluid_tuning_patch.adopted_entries(setup_judge_path, load_json(setup_judge_path), {"variant": "v01", "assignments": [{"json_path": json_path, "value": 1.0}]}, plan)
        return knobs[0]

    shield_entry = adopted_look_entry(setup_plan, look_path)
    every_scene_entry = adopted_look_entry(dict(setup_plan, params=[{"json_path": look_path, "scenes": None}]), look_path)
    plain_entry = adopted_look_entry(dict(setup_plan, params=[{"json_path": look_path, "scenes": ["plain"]}]), look_path)
    check("K9 G accept: Look 채택 항목에 levels 와 함께 채택 장면 camera.console 의 CloudSetup 번호(마지막 줄) cloud_setups 기록, param scenes 없으면 모든 장면 번호, CloudSetup 줄 없는 장면만이면 키 생략",
          shield_entry.get("levels") == [cloud_level] and shield_entry.get("cloud_setups") == [9]
          and every_scene_entry.get("levels") == [cloud_level, campfire_level] and every_scene_entry.get("cloud_setups") == [9, 12]
          and plain_entry.get("levels") == [campfire_level] and "cloud_setups" not in plain_entry,
          (shield_entry, every_scene_entry, plain_entry))

    cloud_look = load_json(CANONICAL_LOOK)["Materials"]
    cap_package, shield_package = "/Game/FluidFX/Materials/MI_TD_FluidCloud_Mushroom", "/Game/FluidFX/Materials/MI_TD_FluidCloud_MushroomShield"
    cap_name, cap_exponent_name, shield_name = "CloudSlot1EmissiveIntensity", "CloudSlot1EmissiveExponent", "CloudSlot2PatternCoverage"

    def cloud_knob(package, name, scenes=None):
        base = cloud_look[package]["Scalars"][name]
        return dict({"json_path": f"Look:Materials.{package}.Scalars.{name}", "values": [base, base + 1.0]}, **({"scenes": scenes} if scenes else {}))

    def two_scene_plan(name, knobs):
        def add_second_scene(plan):
            plan["scenes"].append(dict(copy.deepcopy(plan["scenes"][0]), name="campfire_b"))
            for target in plan["metrics"]["targets"]:
                target["scene"] = "campfire"
            plan["params"].extend(knobs)
        return stage_plan(name, add_second_scene)

    def scene_skips(name, record):
        return [(scene["out_of_scene_look_packages"], lookdev_spec(name, record, index)["variants"][0]["apply"]["look_live"].get("skip_if_no_owner")) for index, scene in enumerate(record["scenes"])]

    reset_world(fire_world)
    cap_base, shield_base = cloud_look[cap_package]["Scalars"][cap_name], cloud_look[shield_package]["Scalars"][shield_name]
    cap_material = FakeMaterialInstanceConstant(cap_package, scalars={cap_name: cap_base}, parent=FakeMaterial("/Game/CAL0/M_Cloud", scalars={cap_name: cap_base}))
    WORLD.add_asset(cap_material)
    WORLD.add_asset(FakeMaterialInstanceConstant(shield_package, scalars={shield_name: shield_base}, parent=FakeMaterial("/Game/CAL0/M_CloudShield", scalars={shield_name: shield_base})))
    WORLD.actors.append(FakeActor("TDGen_CloudLayers", components=[FakeStaticMeshComponent("Mesh", [cap_material])]))
    scoped_path = two_scene_plan("fix-p1", [cloud_knob(cap_package, cap_name, ["campfire"]), cloud_knob(shield_package, shield_name, ["campfire_b"])])
    code, text = sweep_text(["--plan", scoped_path, "--run-dir", sweep_dir / "fix-p1", "--stage", "frame"])
    scoped_record = load_json(sweep_dir / "fix-p1" / "sweep.json") if (sweep_dir / "fix-p1" / "sweep.json").is_file() else {"scenes": []}
    cap_steps, shield_steps = ([scene["steps"] for scene in scoped_record["scenes"]] + [[], []])[:2]
    check("CAL-0 frame P1 같은 레벨 2장면·패키지별 param scenes: 장면 밖 손잡이 패키지만 skip_if_no_owner·out_of_scene_look_packages, 소유자 없으면 skipped_look 로 유효 / 장면 밖이라도 소유자 있으면 적용·다시 읽기 / 장면 안 패키지 소유자 없음은 엄격(종료 1)",
          code == 1 and scene_skips("fix-p1", scoped_record) == [([shield_package], [shield_package]), ([cap_package], [cap_package])]
          and cap_steps and all(step["valid"] and step.get("skipped_look") == [shield_package] and cap_package in (step.get("look_read_back") or {}) for step in cap_steps)
          and shield_steps and not any(step["valid"] for step in shield_steps) and all(not step.get("skipped_look") and cap_package in (step.get("look_read_back") or {}) for step in shield_steps)
          and any("look 적용 대상 없음" in error and shield_package in error for error in step_errors(shield_steps)),
          (code, text[-300:], step_errors(cap_steps)[:3], step_errors(shield_steps)[:3]))

    aux_knob = {"json_path": look_path, "base": 0.0, "values": [0.0, 1.0], "scenes": ["campfire_b"]}
    mixed_knobs = [cloud_knob(cap_package, cap_name, ["campfire"]), cloud_knob(cap_package, cap_exponent_name, ["campfire_b"]), cloud_knob(shield_package, shield_name), aux_knob]
    code, text, mixed_record = dry("fix-p2", two_scene_plan("fix-p2", mixed_knobs), "--stage", "frame")
    check("CAL-0 frame P2 같은 패키지에 이 장면 손잡이가 하나라도 있으면 필수·param scenes 없는(null) 손잡이 패키지는 모든 장면 필수: 장면 밖 패키지만 skip_if_no_owner",
          code == 0 and scene_skips("fix-p2", mixed_record) == [([look_package], [look_package]), ([], None)], (code, text[-300:], mixed_record and scene_skips("fix-p2", mixed_record)))

    ember_path = "Kinds.Campfire.TDFluidEmberSettings.SpawnPerSecond"
    ember_base = load_json(CANONICAL)["Kinds"]["Campfire"].get("TDFluidEmberSettings", {}).get("SpawnPerSecond", 1.0)
    code, text, _ = dry("fix-s4", stage_plan("fix-s4", lambda plan: plan["params"].append({"json_path": ember_path, "values": [ember_base, ember_base * 2 + 1]})), "--stage", "screen")
    owner_errors = lookdev.read_back_owner_errors([{"owner": entry["owner"]} for entry in calib_sweep.READ_BACK_TABLE.values()])
    check("CAL-0 반박 S4 다시 읽기 표 owner 는 모두 룩데브 허용 owner, 게터 없는 TDFluidEmberSettings 손잡이는 건식 종료 2",
          owner_errors == [] and code == 2 and "다시 읽기가 없어" in text, (owner_errors, code, text[-300:]))

    code, text, _ = dry("fix-s5", stage_plan("fix-s5", lambda plan: plan["scenes"][0].update(schedule={"prefx": "a", "events": [{"time": 0.1, "cal": {}}]})), "--stage", "screen")
    check("CAL-0 반박 S5 장면 schedule 오타: 건식도 PIE 단계 apply 와 같은 schedule 키 검사로 종료 2·'prefx'", code == 2 and "prefx" in text, (code, text[-300:]))

    fog_path = "Look:Materials./Game/FluidFX/Materials/MI_TD_FluidFog_Sim.Scalars.FogExtrudeHeightCm"
    fog_look = calib_sweep.parse_look_path(fog_path)
    other_source = {"plan": "Tools/FluidFX/calib/plans/fog_f2_passive.json"}
    overlap_text = "덧씌움 경로가 계획 params·base_tuning 경로와 같다"
    prop_overlap_text = "덧씌움 actor_props 가 계획 actor_prop 손잡이와 같은 대상이다"
    other_level = "/Game/CAL0/LV_Other"

    def chain_plan(name, extra_params=(), mutate=None):
        def edit(plan):
            plan["params"] = [{"json_path": vorticity_path, "values": [1.2, 1.5, 1.8]}, {"actor_prop": glow, "base": 1.0, "values": [1.0, 2.0]}] + list(extra_params)
            if mutate:
                mutate(plan)
        return stage_plan(name, edit)

    def own_overlay(name, assignments, actor_props=()):
        return write_json(f"{name}-overlay.json", {"version": 2, "assignments": list(assignments), "actor_props": list(actor_props), "history": []})

    def own_from(plan_path):
        return {"plan": calib_sweep.relative_text(plan_path), "plan_sha256": sha(plan_path)}

    def own_glow(plan_path, **extra):
        return dict(dict(glow, level=campfire_level, value=2.0, **{"from": own_from(plan_path)}), **extra)

    def own_adopted(plan_path):
        source = own_from(plan_path)
        return [{"json_path": vorticity_path, "value": 1.8, "split_group": False, "kind": "knob", "from": source},
                {"json_path": fog_path, "value": 60.0, "split_group": False, "kind": "knob", "base": 90.0, "from": source}]

    def add_other_level_scene(plan):
        plan["scenes"].append(dict(copy.deepcopy(plan["scenes"][0]), name="campfire_other", level=other_level))
        for target in plan["metrics"]["targets"]:
            target["scene"] = plan["scenes"][0]["name"]

    fog_knob = {"json_path": fog_path, "base": 90.0, "values": [60.0, 90.0, 120.0]}
    chain_path = chain_plan("k10-b1", [fog_knob])
    chain_overlay = own_overlay("k10-b1", own_adopted(chain_path), [own_glow(chain_path)])
    code, text, record = dry("k10-b1", chain_path, "--stage", "verify", "--overlay", chain_overlay)
    spec_steps = lookdev_spec("k10-b1", record)["variants"] if record else []
    fog_copy = load_json(sweep_dir / "k10-b1" / "v00" / "TDFluidMaterialLook.json")["Materials"][fog_look["package"]]["Scalars"].get("FogExtrudeHeightCm") if record else None
    check("K10 B1 verify: 덧씌움에 이 계획(from.plan 프로젝트 상대 경로·plan_sha256 같음) 채택 경로(조절·정본에 없는 Look 키+base)가 있어도 겹침 거부 없음·종료 0, v00 base_value = 덧씌움 값(조절 1.8·Look 60·actor_props 2.0)·사본·look_live·props 도 그 값",
          code == 0 and record["sequence"] == ["v00", "v00", "v00"] and [param["base_value"] for param in record["params"]] == [1.8, 2.0, 60.0]
          and [variant["id"] for variant in record["variants"]] == ["v00"] and record["variants"][0]["values"] == [1.8, 2.0, 60.0]
          and copy_value("k10-b1", "v00", "Campfire", "TDFluidSimulationConfig", "VorticityStrength") == 1.8 and fog_copy == 60.0 and len(spec_steps) == 3
          and all(step["apply"]["props"]["TDGen_CampfireFire"]["cal0_glow"] == 2.0 and step["apply"]["look_live"][fog_look["live_root"]][fog_look["package"]][fog_look["section"]]["FogExtrudeHeightCm"] == 60.0
                  for step in spec_steps),
          (code, text[-300:], record and [param["base_value"] for param in record["params"]]))

    foreign = {}
    for name, source in (("k10-b2", lambda path: other_source), ("k10-b2b", lambda path: None)):
        foreign_path = chain_plan(name)
        foreign[name] = dry(name, foreign_path, "--stage", "verify", "--overlay", own_overlay(name, [dict(own_adopted(foreign_path)[0], **{"from": source(foreign_path)})]))[:2]
    check("K10 B2 verify 라도 남의 계획(from.plan 다른 경로)·from 없는 덧씌움 항목이 params 경로와 같으면 기존대로 종료 2 '덧씌움 경로가 계획 params·base_tuning 경로와 같다'",
          all(code == 2 and overlap_text in text and vorticity_path in text for code, text in foreign.values()), foreign)

    stage_codes = {}
    for stage in ("screen", "confirm"):
        name = f"k10-b3-{stage}"
        stage_path = chain_plan(name, [fog_knob])
        stage_overlay = own_overlay(name, own_adopted(stage_path))
        extra = ("--screen-result", screen_file(name, stage_path, overlay_sha256=sha(stage_overlay))) if stage == "confirm" else ()
        stage_codes[stage] = dry(name, stage_path, "--stage", stage, "--overlay", stage_overlay, *extra)[:2]
    shipped_path = chain_plan("k10-b3-base", mutate=lambda plan: plan.update(base_tuning=[{"json_path": torch_path, "value": 2.5}], base_tuning_ships=True))
    shipped = dry("k10-b3-base", shipped_path, "--stage", "verify", "--overlay", own_overlay("k10-b3-base", [{"json_path": torch_path, "value": 2.5, "split_group": False, "kind": "base_tuning", "from": own_from(shipped_path)}]))[:2]
    check("K10 B3 자기 채택 경로 겹침 허용은 verify 만: 그 계획 자신의 덧씌움으로 screen·confirm 은 종료 2 겹침 거부 / verify 라도 base_tuning 경로와 겹치면(자기 항목이라도) 종료 2",
          all(code == 2 and overlap_text in text for code, text in stage_codes.values()) and shipped[0] == 2 and overlap_text in shipped[1] and torch_path in shipped[1],
          (stage_codes, shipped))

    split_path = chain_plan("k10-b4", mutate=add_other_level_scene)
    code_split, text_split, _ = dry("k10-b4", split_path, "--stage", "verify", "--overlay", own_overlay("k10-b4", [], [own_glow(split_path), own_glow(split_path, level=other_level, value=3.0)]))
    check("K10 B4 actor_props: verify 에서 이 계획의 같은 대상(계획 장면 두 레벨) 채택 값이 레벨마다 다르면 종료 2 '레벨마다 다르다'",
          code_split == 2 and "레벨마다 다르다" in text_split, (code_split, text_split[-200:]))

    reset_world(fire_world)
    WORLD.actors[0].cal0_glow = 1.0
    live_path = chain_plan("k10-b5")
    live_overlay = own_overlay("k10-b5", [own_adopted(live_path)[0]], [own_glow(live_path)])
    code, text = sweep_text(["--plan", live_path, "--run-dir", sweep_dir / "k10-b5", "--stage", "verify", "--lock", write_lock("k10-b5", live_path), "--overlay", live_overlay])
    live_record = load_json(sweep_dir / "k10-b5" / "sweep.json") if (sweep_dir / "k10-b5" / "sweep.json").is_file() else {"scenes": [{"steps": []}]}
    live_steps = live_record["scenes"][0]["steps"]
    check("K10 B5 가짜 PIE verify(자기 채택 조절 1.8·actor_props 2.0): 대조 3단계 유효·ChangedFieldCount 0·액터 값 2.0(계획 base 1.0 이 덮지 않음)·종료 0",
          code == 0 and len(live_steps) == 3 and all(step["valid"] for step in live_steps) and [step["report"]["changed_field_count"] for step in live_steps] == [0, 0, 0]
          and WORLD.actors[0].cal0_glow == 2.0, (code, text[-300:], step_errors(live_steps)[:4], WORLD.actors[0].cal0_glow))

    round_one = chain_plan("k10-c1")
    round_two = chain_plan("k10-c1-p20")
    descendant_text = calib_sweep.relative_text(round_two)
    p20_refusal = f"P-20 2라운드 채택이 있는 1라운드 계획은 재검증하지 않는다: {descendant_text}"
    p20_overlay = own_overlay("k10-c1", [own_adopted(round_two)[0]])
    code_one, text_one, _ = dry("k10-c1", round_one, "--stage", "verify", "--overlay", p20_overlay)
    code_prop, text_prop, _ = dry("k10-c1b", round_one, "--stage", "verify", "--overlay", own_overlay("k10-c1b", [], [own_glow(round_two)]))
    code_two, text_two, record_two = dry("k10-c1-p20", round_two, "--stage", "verify", "--overlay", p20_overlay)
    code_round_screen, text_round_screen, _ = dry("k10-c1s", round_one, "--stage", "screen", "--overlay", p20_overlay)
    check("K10 C1 P-20 자손: 덧씌움에 같은 폴더 <계획 stem>-p20.json 채택(조절·actor_props 어느 쪽이든)이 있으면 1라운드 계획 verify 는 종료 2 'P-20 2라운드 채택이 있는 1라운드 계획은 재검증하지 않는다: <경로>' / 2라운드 계획 자신의 verify 는 일반 규칙(자기 채택 허용·종료 0·base 1.8) / 1라운드 screen 은 기존 겹침 거부",
          code_one == 2 and p20_refusal in text_one and code_prop == 2 and p20_refusal in text_prop and code_two == 0 and record_two["params"][0]["base_value"] == 1.8
          and code_round_screen == 2 and overlap_text in text_round_screen and "P-20" not in text_round_screen,
          (code_one, text_one[-200:], code_prop, text_prop[-200:], code_two, text_two[-200:], code_round_screen, text_round_screen[-200:]))

    elsewhere_path = chain_plan("k10-d1")
    elsewhere_source = {"plan": f"Tools/FluidFX/calib/plans/{elsewhere_path.stem}.json", "plan_sha256": sha(elsewhere_path)}
    code_elsewhere, text_elsewhere, _ = dry("k10-d1", elsewhere_path, "--stage", "verify", "--overlay", own_overlay("k10-d1", [dict(own_adopted(elsewhere_path)[0], **{"from": elsewhere_source})]))
    changed_path = chain_plan("k10-d1b")
    changed_source = dict(own_from(changed_path), plan_sha256="0" * 64)
    code_changed, text_changed, _ = dry("k10-d1b", changed_path, "--stage", "verify", "--overlay", own_overlay("k10-d1b", [dict(own_adopted(changed_path)[0], **{"from": changed_source})]))
    code_changed_prop, text_changed_prop, _ = dry("k10-d1c", changed_path, "--stage", "verify", "--overlay", own_overlay("k10-d1c", [], [own_glow(changed_path, **{"from": changed_source})]))
    check("K10 D1 자기 채택 판별은 from.plan 프로젝트 상대 경로 + plan_sha256: 다른 폴더의 같은 stem·같은 sha 출처는 남의 계획(verify 겹침 종료 2) / 경로는 같고 sha 가 다르면 조절·actor_props 모두 종료 2 '계획이 채택 뒤 바뀜'",
          code_elsewhere == 2 and overlap_text in text_elsewhere and code_changed == 2 and "계획이 채택 뒤 바뀜" in text_changed and code_changed_prop == 2 and "계획이 채택 뒤 바뀜" in text_changed_prop,
          (code_elsewhere, text_elsewhere[-200:], code_changed, text_changed[-200:], code_changed_prop, text_changed_prop[-200:]))

    orphan_path = chain_plan("k10-e1")
    orphans = {name: dry(name, orphan_path, "--stage", "verify", "--overlay", own_overlay(name, [], [own_glow(orphan_path, **extra)]))[:2]
               for name, extra in (("k10-e1", {"component": "light_component"}), ("k10-e1b", {"level": other_level}), ("k10-e1c", {"property": "cal0_other"}))}
    check("K10 E1 actor_props(a): 이 계획 자신의 덧씌움 actor_props 항목이 대상 키(component·level(계획 장면 레벨 밖)·property)로 계획 actor_prop 손잡이와 하나도 맞지 않으면 verify 종료 2 '하나도 맞지 않는다'(계획 base 로 조용히 돌아가지 않음)",
          all(code == 2 and "하나도 맞지 않는다" in text for code, text in orphans.values()), orphans)

    prop_runs = {}
    for name, stage, item in (("k10-e2", "screen", "own"), ("k10-e2b", "verify", "other"), ("k10-e2c", "frame", "other"), ("k10-e2d", "verify", "other_level")):
        prop_path = chain_plan(name)
        prop_item = {"own": own_glow(prop_path), "other": own_glow(prop_path, **{"from": other_source}), "other_level": own_glow(prop_path, level=other_level, **{"from": other_source})}[item]
        prop_runs[name] = dry(name, prop_path, "--stage", stage, "--overlay", own_overlay(name, [], [prop_item]))
    blocked = [prop_runs[name] for name in ("k10-e2", "k10-e2b", "k10-e2c")]
    unrelated_code, _, unrelated_record = prop_runs["k10-e2d"]
    check("K10 E2 actor_props(b): 덧씌움 actor_props 가 계획 actor_prop 손잡이와 같은 대상(level·label·property·component)이면 json_path 겹침과 같이 종료 2 — 자기 채택이라도 screen, 남의 채택은 verify·frame 모두 / 계획 장면 레벨 밖 같은 라벨은 다른 대상이라 통과(base 1.0)",
          all(code == 2 and prop_overlap_text in text and "cal0_glow" in text for code, text, _ in blocked) and unrelated_code == 0 and unrelated_record["params"][1]["base_value"] == 1.0,
          ([(code, text[-200:]) for code, text, _ in blocked], unrelated_code))


def check_cal0_integration():
    look_path = "Look:Materials./Game/FluidFX/Materials/MI_TD_FluidCloud_SandWall.Scalars.CloudSlot1DetailErosion"
    look_param = {"index": 0, "kind": "look", "json_path": look_path, "look": calib_sweep.parse_look_path(look_path), "base_value": 0.4, "split_group": False, "scenes": None}
    control = calib_sweep.make_variant("v00", [look_param], [0.4])
    variant = calib_sweep.make_variant("v01", [look_param], [0.8])
    accepted = fluid_tuning_patch.describe_assignment(None, None, control["assignments"][0], look=fluid_tuning_patch.load_canonical_look())
    check("통합 B5#10 모양 손잡이: 대조 사본에도 {json_path, value: base}(정본에 없는 키도 재적재로 base 복귀)·대조 판정 불변",
          control["is_control"] and control["assignments"] == [{"json_path": look_path, "value": 0.4, "split_group": False, "base": 0.4}] and accepted["added_key"]
          and not variant["is_control"] and variant["assignments"][0]["value"] == 0.8 and variant["knobs"] == [0], (control["assignments"], variant["assignments"]))
    other_path = look_path.replace("CloudSlot1DetailErosion", "CloudSlot1SectionDepthKm")
    other_param = dict(look_param, index=1, json_path=other_path, look=calib_sweep.parse_look_path(other_path), base_value=2.0)
    first_only = calib_sweep.make_variant("v01", [look_param, other_param], [0.8, 2.0])
    staged_sweep = {"params": [{"json_path": look_path}, {"json_path": other_path}], "variants": [first_only]}
    adopted = calib_judge.plan_param_assignments(staged_sweep, "v01")
    check("통합 B5#10 채택: 변형이 돌린 손잡이만 adopt.assignments(base 그대로 실린 다른 모양 손잡이는 덧씌움에 들어가지 않음)",
          len(first_only["assignments"]) == 2 and [item["json_path"] for item in adopted] == [look_path] and adopted[0]["value"] == 0.8, adopted)

    def judge_rejection(sweep, lock_path):
        try:
            calib_judge.check_lock(sweep, lock_path)
        except calib_judge.JudgeError as error:
            return str(error)
        return None
    lock_dir = TEST_DIR / "cal0-integration"
    lock_dir.mkdir(parents=True, exist_ok=True)
    plan_text = calib_sweep.relative_text(W / CAMPFIRE_PLAN)
    plan_only_lock = lock_dir / "plan-only.lock.json"
    prereg_lock.create_lock([plan_text], plan_only_lock)
    full_lock = lock_dir / "full.lock.json"
    prereg_lock.create_lock(prereg_lock.required_files(plan_text), full_lock)
    campfire_plan = load_json(W / CAMPFIRE_PLAN)
    rule_copies = {"metrics": campfire_plan["metrics"], "params": campfire_plan["params"],
                   "scenes": [{"name": scene["name"], "sources": calib_sweep.scene_sources(scene, campfire_plan), "regions_template": scene.get("regions_template"), "targets": scene.get("targets")}
                              for scene in campfire_plan["scenes"]]}
    staged = dict(rule_copies, plan=plan_text, plan_sha256=fluid_tuning_patch.file_sha256(W / CAMPFIRE_PLAN), stage="screen", started_at="2999-01-01T00:00:00+00:00")
    missing_file = judge_rejection(staged, plan_only_lock)
    missing_lock_sha = judge_rejection(staged, full_lock)
    accepted_lock = calib_judge.check_lock(dict(staged, lock_sha256=fluid_tuning_patch.file_sha256(full_lock)), full_lock)
    check("통합 (a) 판정기 check_lock = prereg_lock.check_sweep_lock(닫힌 세계 C8): 필수 파일 누락·lock_sha256 없음 거부, 다 갖추면 통과",
          missing_file is not None and "필수 파일 누락" in missing_file and missing_lock_sha is not None and "lock_sha256" in missing_lock_sha and accepted_lock.get("version") == 2,
          (missing_file, missing_lock_sha))


def check_cal0_judge():
    from datetime import datetime, timezone
    root = TEST_DIR / "cal0-b4a"
    root.mkdir(parents=True, exist_ok=True)
    calib_judge.PERF_CANDIDATES_PATH = root / "perf-candidates.json"
    knob_a, knob_b = "Kinds.T.TDFluidSimulationConfig.A", "Kinds.T.TDFluidSimulationConfig.B"
    variant_knobs = {"v00": [], "v01": [(knob_a, 0.5)], "v02": [(knob_a, 1.5)], "v03": [(knob_b, 0.5)], "v04": [(knob_b, 1.5)]}
    target_m = {"metric": "probe.m", "low": 6.0, "high": 10.0, "weight": 1.0}
    floor = 0.2 / math.sqrt(3)
    score_noise = floor / 2.0

    def close(value, expected, tolerance=1e-9):
        return isinstance(value, (int, float)) and not isinstance(value, bool) and abs(value - expected) <= tolerance

    def dig(value, *keys):
        for key in keys:
            if isinstance(value, dict):
                value = value.get(key)
            elif isinstance(value, list) and isinstance(key, int) and -len(value) <= key < len(value):
                value = value[key]
            else:
                return None
        return value

    def write_document(path, document):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(document, ensure_ascii=False, indent=1), encoding="utf-8")
        return path

    def rows(*entries):
        return [(variant, {metric: [value - 0.1, value, value + 0.1] for metric, value in values.items()}) for variant, values in entries]

    def probe_scene(name):
        return {"name": name, "level": "/Game/CAL0/Level", "camera": {}, "settle": 1.0, "sources": ["probe"], "probe": {"times": [1.0, 1.5, 2.0]}}

    def make_plan(name, scenes, targets, costs=None, scopes=None):
        params = [dict({"json_path": path, "values": [0.5, 1.0, 1.5]}, **({"cost": costs[path]} if costs and path in costs else {}), **({"scenes": scopes[path]} if scopes and path in scopes else {}))
                  for path in (knob_a, knob_b)]
        metrics = {"sources": sorted({source for scene in scenes for source in scene.get("sources", ["look"])}), "targets": targets}
        plan_path = write_document(root / name / "plan.json", {"id": name, "scenes": scenes, "params": params, "mode": "one_at_a_time", "repeats": 1, "control_every": 0, "reset": "reinit", "metrics": metrics})
        lock_path = root / name / "lock.json"
        python("Tools/FluidFX/prereg_lock.py", "lock", "--out", str(lock_path), "--plan", str(plan_path))
        return plan_path, lock_path

    def make_sweep(plan_path, lock_path, stage, scene_steps, folder, extra=None):
        run_dir = plan_path.parent / folder
        plan = load_json(plan_path)
        scenes = []
        for scene_name, steps in scene_steps.items():
            plan_scene = next(scene for scene in plan["scenes"] if scene["name"] == scene_name)
            if steps and isinstance(steps[0], tuple):
                steps = [{"step": index, "name": f"s{index:02d}_{variant}", "variant": variant, "valid": True, "errors": [], "images": [], "metrics": metrics, "shot_game_times": [1.0, 1.5, 2.0]}
                         for index, (variant, metrics) in enumerate(steps)]
                for step in steps if plan_scene.get("sources") == ["offline"] else []:
                    digest = hashlib.sha256(json.dumps(step["metrics"], sort_keys=True).encode("utf-8")).hexdigest()
                    step["offline"] = {"output_sha256": digest, "repeat_sha256": digest}
            scenes.append({"name": scene_name, "level": "/Game/CAL0/Level", "sources": plan_scene.get("sources", ["look"]), "regions_template": plan_scene.get("regions_template"), "targets": plan_scene.get("targets"), "steps": steps})
        sweep = {"tool": "calib_sweep", "stage": stage, "plan": prereg_lock.relative_text(plan_path), "plan_sha256": prereg_lock.file_sha256(plan_path), "plan_id": plan["id"],
                 "run_dir": prereg_lock.relative_text(run_dir), "lock": prereg_lock.relative_text(lock_path), "lock_sha256": prereg_lock.file_sha256(lock_path),
                 "overlay": None, "overlay_sha256": None, "canonical_tuning_sha256": "1" * 64, "canonical_look_sha256": "2" * 64, "started_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
                 "params": [{"json_path": param["json_path"], "base_value": 1.0, "values": param["values"], "cost": param.get("cost"), "scenes": param.get("scenes")} for param in plan["params"]],
                 "variants": [{"id": variant, "assignments": [{"json_path": path, "value": value, "split_group": False} for path, value in knobs]} for variant, knobs in variant_knobs.items()],
                 "metrics": plan["metrics"], "scenes": scenes}
        sweep.update(extra or {})
        return write_document(run_dir / "sweep.json", sweep)

    def judge(*arguments):
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output), patched(sys, argv=["calib_judge.py", *[str(argument) for argument in arguments]]):
            code = calib_judge.main()
        return code, output.getvalue()

    def screen(plan_path, lock_path, scene_steps, folder="screen"):
        sweep_path = make_sweep(plan_path, lock_path, "screen", scene_steps, folder)
        code, output = judge("--sweep", sweep_path, "--lock", lock_path)
        result_path = sweep_path.parent / "screen.json"
        return sweep_path, code, output, load_json(result_path) if result_path.is_file() else {}

    def confirm(plan_path, lock_path, scene_steps, screen_sweep_path, folder="confirm", screen_result_path=None, extra=None):
        screen_result_path = screen_result_path or screen_sweep_path.parent / "screen.json"
        fields = {"screen_result": prereg_lock.relative_text(screen_result_path), "screen_result_sha256": prereg_lock.file_sha256(screen_result_path)}
        fields.update(extra or {})
        sweep_path = make_sweep(plan_path, lock_path, "confirm", scene_steps, folder, fields)
        code, output = judge("--sweep", sweep_path, "--lock", lock_path, "--screen-sweep", screen_sweep_path)
        result_path = sweep_path.parent / "judge.json"
        return code, output, load_json(result_path) if result_path.is_file() else {}

    screen_steps = {"s1": rows(("v00", {"probe.m": 5.0}), ("v01", {"probe.m": 4.0}), ("v02", {"probe.m": 7.5}), ("v00", {"probe.m": 5.05}), ("v03", {"probe.m": 5.03}), ("v04", {"probe.m": 4.98}), ("v00", {"probe.m": 4.95}))}
    p1_plan, p1_lock = make_plan("p1", [probe_scene("s1")], [target_m])
    p1_screen, code, output, screen_document = screen(p1_plan, p1_lock, screen_steps)
    noise = dig(screen_document, "noise", "s1", "probe.m") or {}
    params = {param["json_path"]: param for param in screen_document.get("params", [])}
    required = {"version", "plan_sha256", "overlay_sha256", "canonical_tuning_sha256", "canonical_look_sha256", "lock_sha256", "screen_sweep", "screen_sweep_sha256", "noise", "score_noise", "params", "keep", "best", "second", "insensitive", "unreachable_targets"}
    check("CAL-0 B4a T13 screen 합성: 잡음 between 0.1·shot 0.2·median_floor 0.2/√3·noise=max, 효과 a 2.5 민감·b 0.03 둔감, keep·best v02·reach, screen.json 필드 전부",
          code == 0 and required <= set(screen_document) and screen_document["version"] == 2 and close(noise.get("between"), 0.1) and close(noise.get("shot"), 0.2) and close(noise.get("median_floor"), floor) and close(noise.get("noise"), floor)
          and close(dig(params, knob_a, "effect", "s1", "probe.m"), 2.5) and dig(params, knob_a, "sensitive") is True and close(dig(params, knob_b, "effect", "s1", "probe.m"), 0.03) and dig(params, knob_b, "sensitive") is False
          and screen_document["keep"] == [knob_a] and dig(screen_document, "best", "variant") == "v02" and close(dig(screen_document, "best", "gain"), 1.25 / score_noise, 1e-6) and screen_document["second"] is None
          and screen_document["insensitive"] is False and close(dig(screen_document, "score_noise", "s1"), score_noise) and dig(params, knob_a, "reach") == {"probe.m": "inside"} and dig(params, knob_b, "reach") == {"probe.m": "outside"}
          and screen_document["unreachable_targets"] == [], output[-800:])
    check("CAL-0 B4a T16 강한 효과(gain > 20)라도 screen 만으로는 adopt 거짓(confirm 필요)", dig(screen_document, "adopt", "decision") is False and "confirm" in " ".join(dig(screen_document, "adopt", "reasons") or []), screen_document.get("adopt"))

    def drift_steps(second_control):
        return {"s1": rows(("v00", {"probe.m": 5.0}), ("v01", {"probe.m": 4.0}), ("v02", {"probe.m": 7.5}), ("v00", {"probe.m": second_control}), ("v03", {"probe.m": 5.03}), ("v04", {"probe.m": 4.98}), ("v00", {"probe.m": 5.0}))}

    _, pass_code, _, drift_pass = screen(p1_plan, p1_lock, drift_steps(5.15), "screen-drift-pass")
    _, fail_code, fail_output, drift_fail = screen(p1_plan, p1_lock, drift_steps(5.5), "screen-drift-fail")
    check("CAL-0 B4a T70 표류 0.15 < 2×median_floor 0.231 통과 / 표류 0.5 > 허용이면 대조 실패(between 0.5 라 동어반복 규칙이면 통과했을 것)·종료 1·best 없음",
          pass_code == 0 and dig(drift_pass, "scenes", 0, "control_check", "probe.m") == "pass" and close(dig(drift_pass, "scenes", 0, "control_allowed", "probe.m"), 2 * floor)
          and fail_code == 1 and dig(drift_fail, "scenes", 0, "control_check", "probe.m") == "fail" and close(dig(drift_fail, "noise", "s1", "probe.m", "between"), 0.5) and drift_fail.get("best") is None, (drift_pass.get("scenes"), fail_output[-300:]))

    confirm_steps = {"s1": rows(("v00", {"probe.m": 5.01}), ("v02", {"probe.m": 7.6}), ("v00", {"probe.m": 5.02}), ("v02", {"probe.m": 7.4}), ("v00", {"probe.m": 4.98 - 1e-6}))}
    code, output, confirmed = confirm(p1_plan, p1_lock, confirm_steps, p1_screen)
    adopt = confirmed.get("adopt") or {}
    check("CAL-0 B4a T15 양성: 다른 실행·같은 후보 v02·둘 다 유의 → adopt 참·할당·증거(교집합 0, sha 표 일치)",
          code == 0 and adopt.get("decision") is True and adopt.get("variant") == "v02" and adopt.get("assignments") == [{"json_path": knob_a, "value": 1.5, "split_group": False}]
          and dig(adopt, "evidence", "image_overlap") == 0 and dig(adopt, "evidence", "screen_run") != dig(adopt, "evidence", "confirm_run") and all(row["match"] for row in (dig(adopt, "evidence", "sha") or {"x": {"match": False}}).values())
          and confirmed.get("version") == 2 and confirmed.get("stage") == "confirm" and confirmed.get("lock_sha256") == prereg_lock.file_sha256(p1_lock) and dig(confirmed, "scenes", 0, "candidate_medians", "probe.m") is not None, (adopt.get("reasons"), output[-500:]))
    verify_close = make_sweep(p1_plan, p1_lock, "verify", {"s1": rows(("v00", {"probe.m": 7.5}), ("v00", {"probe.m": 7.52}), ("v00", {"probe.m": 7.48}))}, "verify-close")
    verify_close_code, verify_close_output = judge("--sweep", verify_close, "--lock", p1_lock, "--verify-against", p1_plan.parent / "confirm" / "judge.json")
    verify_far = make_sweep(p1_plan, p1_lock, "verify", {"s1": rows(("v00", {"probe.m": 5.0}), ("v00", {"probe.m": 5.02}), ("v00", {"probe.m": 4.98}))}, "verify-far")
    verify_far_code, _ = judge("--sweep", verify_far, "--lock", p1_lock, "--verify-against", p1_plan.parent / "confirm" / "judge.json")
    verify_report = load_json(verify_close.parent / "judge.json") if (verify_close.parent / "judge.json").is_file() else {}
    check("CAL-0 B4a verify --verify-against: 대조 기준이 앞 채택 후보 중앙값 7.5 ± max(2×screen median_floor, tolerance) 안이면 통과(종료 0), 5.0 이면 실패(종료 1)",
          verify_close_code == 0 and dig(verify_report, "verify", "passed") is True and close(dig(verify_report, "verify", "checks", 0, "allowed"), 2 * floor) and verify_far_code == 1, verify_close_output[-400:])
    other_steps = {"s1": rows(("v00", {"probe.m": 5.0}), ("v04", {"probe.m": 7.5}), ("v00", {"probe.m": 5.02}), ("v04", {"probe.m": 7.5}), ("v00", {"probe.m": 4.98}))}
    code, output, other = confirm(p1_plan, p1_lock, other_steps, p1_screen, "confirm-other")
    check("CAL-0 B4a T15 음성: confirm 후보가 screen best 와 다름 → adopt 거짓(후보 다름)", dig(other, "adopt", "decision") is False and any("후보 다름" in reason for reason in dig(other, "adopt", "reasons") or []), (dig(other, "adopt"), output[-300:]))
    wide_steps = {"s1": rows(("v00", {"probe.m": 5.0}), ("v02", {"probe.m": 5.24}), ("v00", {"probe.m": 5.15}), ("v02", {"probe.m": 5.24}), ("v00", {"probe.m": 4.85}))}
    code, output, wide = confirm(p1_plan, p1_lock, wide_steps, p1_screen, "confirm-wide")
    check("CAL-0 B4a T71 confirm 잡음 = max(screen noise, confirm between): 작으면 screen 0.115 유지 / 0.3 이면 점수 잡음 0.15 로 개선 0.12 는 유의 아님(screen 잡음이면 유의)",
          close(dig(confirmed, "scenes", 0, "noise", "probe.m", "noise"), floor) and close(dig(confirmed, "scenes", 0, "noise", "probe.m", "between"), 0.04, 1e-5)
          and close(dig(wide, "scenes", 0, "noise", "probe.m", "noise"), 0.3) and close(dig(wide, "scenes", 0, "score_noise"), 0.15) and close(dig(wide, "scenes", 0, "variants", "v02", "improvement"), 0.12)
          and dig(wide, "scenes", 0, "control_ok") is True and dig(wide, "adopt", "decision") is False and any("유의 아님" in reason for reason in dig(wide, "adopt", "reasons") or []), (dig(wide, "scenes", 0, "noise"), dig(wide, "adopt"), output[-300:]))

    _, _, same = confirm(p1_plan, p1_lock, screen_steps, p1_screen, "confirm-same", extra={"run_dir": load_json(p1_screen)["run_dir"]})
    overlay_code, overlay_output, _ = confirm(p1_plan, p1_lock, confirm_steps, p1_screen, "confirm-overlay", extra={"overlay_sha256": "f" * 64})
    flat_steps = {"s1": rows(("v00", {"probe.m": 5.0}), ("v01", {"probe.m": 5.0}), ("v02", {"probe.m": 5.02}), ("v00", {"probe.m": 5.05}), ("v03", {"probe.m": 5.03}), ("v04", {"probe.m": 4.98}), ("v00", {"probe.m": 4.95}))}
    flat_screen, _, _, flat_document = screen(p1_plan, p1_lock, flat_steps, "screen-flat")
    _, _, flat = confirm(p1_plan, p1_lock, confirm_steps, flat_screen, "confirm-flat")
    check("CAL-0 B4a T77 같은 스윕 두 번 → adopt 거짓(같은 실행·교집합) / overlay sha 다름 → 종료 2 / 후보 v00(best 없음) → 거짓 / 유의 아님 → 거짓",
          dig(same, "adopt", "decision") is False and any("같은 실행" in reason for reason in dig(same, "adopt", "reasons") or []) and (dig(same, "adopt", "evidence", "image_overlap") or 0) > 0
          and overlay_code == 2 and "sha 불일치" in overlay_output and "overlay_sha256" in overlay_output
          and flat_document.get("best") is None and dig(flat, "adopt", "decision") is False and any("best 가 없다" in reason for reason in dig(flat, "adopt", "reasons") or [])
          and dig(wide, "adopt", "decision") is False, (dig(same, "adopt", "reasons"), overlay_output[-300:], dig(flat, "adopt", "reasons")))

    edited = load_json(p1_screen.parent / "screen.json")
    edited["keep"] = [knob_a, knob_b]
    edited_path = write_document(p1_plan.parent / "screen-edited.json", edited)
    edited_code, edited_output, _ = confirm(p1_plan, p1_lock, confirm_steps, p1_screen, "confirm-edited", screen_result_path=edited_path)
    check("CAL-0 B4a T79 screen.json keep 손편집 뒤 confirm 판정 → 종료 2 'screen 재판정 불일치'", edited_code == 2 and "screen 재판정 불일치" in edited_output and "keep" in edited_output, edited_output[-300:])

    target_n = {"metric": "probe.n", "low": 0.0, "high": 4.0, "weight": 1.0, "scene": "s2"}
    p2_plan, p2_lock = make_plan("p2", [probe_scene("s1"), probe_scene("s2")], [dict(target_m, scene="s1"), target_n])
    multi_steps = {"s1": rows(("v00", {"probe.m": 5.0}), ("v01", {"probe.m": 4.0}), ("v02", {"probe.m": 7.5}), ("v00", {"probe.m": 5.05}), ("v03", {"probe.m": 4.0}), ("v04", {"probe.m": 6.8}), ("v00", {"probe.m": 4.95})),
                   "s2": rows(("v00", {"probe.n": 3.0}), ("v01", {"probe.n": 3.0}), ("v02", {"probe.n": 3.0}), ("v00", {"probe.n": 3.05}), ("v03", {"probe.n": 2.0}), ("v04", {"probe.n": 2.2}), ("v00", {"probe.n": 2.95}))}
    _, code, output, multi = screen(p2_plan, p2_lock, multi_steps)
    check("CAL-0 B4a T80 다장면: 장면별 최선(s1 v02·s2 v03)이 달라도 gain 합 최대 v04(1.3/q), s1 악화 v03 제외, second v02(차 < 1)",
          code == 0 and dig(multi, "best", "variant") == "v04" and close(dig(multi, "best", "gain"), 1.3 / score_noise, 1e-6) and dig(multi, "second", "variant") == "v02" and close(dig(multi, "second", "gain"), 1.25 / score_noise, 1e-6)
          and dig(multi, "scenes", 0, "variants", "v03", "eligible") is False and close(dig(multi, "scenes", 1, "variants", "v03", "score"), 0.0) and close(dig(multi, "scenes", 0, "variants", "v02", "score"), 0.25), (multi.get("best"), multi.get("second"), output[-300:]))

    lane_sweep = {"params": [{"kind": "tuning", "json_path": knob_a, "json_paths": [knob_a, knob_b], "base_value": 1.0}, {"kind": "actor_prop", "json_path": None, "actor_prop": {"label": "Rock", "property": "scale"}, "base_value": 1.0}],
                  "variants": [{"id": "v00", "knobs": [], "values": [1.0, 1.0], "assignments": [{"json_path": "Kinds.T.TDFluidSimulationConfig.Overlay", "value": 3.0}]},
                               {"id": "v01", "knobs": [0], "values": [1.5, 1.0], "assignments": [{"json_path": "Kinds.T.TDFluidSimulationConfig.Overlay", "value": 3.0}, {"json_path": knob_a, "value": 1.5}, {"json_path": knob_b, "value": 1.5}]},
                               {"id": "v02", "knobs": [1], "values": [1.0, 2.0], "assignments": [{"json_path": "Kinds.T.TDFluidSimulationConfig.Overlay", "value": 3.0}]}]}
    check("CAL-0 B4a 변형↔손잡이 대응(스윕 knobs·values): 연동 json_paths 는 한 손잡이, actor_prop 채택 할당 생성, 덧씌움 할당은 adopt 에서 뺌",
          calib_judge.variant_param_values(lane_sweep) == {"v00": {}, "v01": {0: 1.5}, "v02": {1: 2.0}}
          and calib_judge.plan_param_assignments(lane_sweep, "v01") == [{"json_path": knob_a, "value": 1.5}, {"json_path": knob_b, "value": 1.5}]
          and calib_judge.plan_param_assignments(lane_sweep, "v02") == [{"actor_prop": {"label": "Rock", "property": "scale"}, "value": 2.0}])

    p3_plan, p3_lock = make_plan("p3", [probe_scene("s1")], [target_m, {"metric": "probe.c", "low": 0.0, "high": 1.0, "role": "constraint"}])
    constraint_steps = {"s1": rows(("v00", {"probe.m": 5.0, "probe.c": 0.5}), ("v01", {"probe.m": 4.0, "probe.c": 0.5}), ("v02", {"probe.m": 7.5, "probe.c": 1.5}), ("v00", {"probe.m": 5.05, "probe.c": 0.5}),
                                ("v03", {"probe.m": 4.0, "probe.c": 0.5}), ("v04", {"probe.m": 6.8, "probe.c": 0.5}), ("v00", {"probe.m": 4.95, "probe.c": 0.5}))}
    _, code, output, constrained = screen(p3_plan, p3_lock, constraint_steps)
    check("CAL-0 B4a T81 제약 실패 변형 v02 가 점수 최선이어도 자격 없음 → 다음 v04",
          code == 0 and dig(constrained, "best", "variant") == "v04" and dig(constrained, "scenes", 0, "variants", "v02", "constraint_failures") == ["probe.c"] and dig(constrained, "scenes", 0, "variants", "v02", "eligible") is False
          and close(dig(constrained, "scenes", 0, "variants", "v02", "score"), 0.25), (constrained.get("best"), output[-300:]))

    p4_plan, p4_lock = make_plan("p4", [probe_scene("s1")], [target_m], costs={knob_a: "higher_costs_more"})
    p4_screen, code, output, costly = screen(p4_plan, p4_lock, screen_steps)
    _, _, costly_confirm = confirm(p4_plan, p4_lock, confirm_steps, p4_screen)
    perf_items = load_json(calib_judge.PERF_CANDIDATES_PATH).get("items", []) if calib_judge.PERF_CANDIDATES_PATH.is_file() else []
    check("CAL-0 B4a T82 최선이 비용 증가 끝(higher_costs_more, 1.0→1.5) → best 없음·cost_blocked_best v02·perf-candidates 기록·confirm adopt 거짓",
          dig(costly, "best") is None and dig(costly, "cost_blocked_best", "variant") == "v02" and dig(costly, "params", 0, "cost_blocked") == ["v02"]
          and any(item.get("variant") == "v02" and item.get("plan_sha256") == prereg_lock.file_sha256(p4_plan) for item in perf_items) and dig(costly_confirm, "adopt", "decision") is False, (costly.get("params"), perf_items, output[-300:]))

    relative_target = {"metric": "probe.c", "role": "constraint", "relative_to_control": {"low": 0.9, "high": 1.1}}
    p5_plan, p5_lock = make_plan("p5", [probe_scene("s1")], [target_m, relative_target])
    relative_steps = {"s1": rows(("v00", {"probe.m": 5.0, "probe.c": 2.0}), ("v01", {"probe.m": 4.0, "probe.c": 2.0}), ("v02", {"probe.m": 7.5, "probe.c": 2.5}), ("v00", {"probe.m": 5.05, "probe.c": 2.0}),
                              ("v03", {"probe.m": 4.0, "probe.c": 2.0}), ("v04", {"probe.m": 6.8, "probe.c": 2.0}), ("v00", {"probe.m": 4.95, "probe.c": 2.0}))}
    _, code, output, relative_document = screen(p5_plan, p5_lock, relative_steps)
    band = next((row for row in dig(relative_document, "scenes", 0, "targets") or [] if row["metric"] == "probe.c"), {})
    p6_plan, p6_lock = make_plan("p6", [probe_scene("s1")], [target_m, dict(relative_target, role="score")])
    _, score_code, score_output, _ = screen(p6_plan, p6_lock, relative_steps)
    check("CAL-0 B4a T39 relative_to_control {0.9,1.1} 제약 띠 = 대조 2.0 × 0.9·1.1 = [1.8, 2.2](v02 c 2.5 자격 없음) / score 면 종료 2 '1.0 을 포함'",
          code == 0 and close(band.get("low"), 1.8) and close(band.get("high"), 2.2) and band.get("band_source") == "relative" and dig(relative_document, "best", "variant") == "v04"
          and score_code == 2 and "1.0 을 포함" in score_output, (band, score_output[-300:]))

    p7_plan, p7_lock = make_plan("p7", [probe_scene("s1"), probe_scene("s2")], [target_m, target_n])
    _, unscoped_code, unscoped_output, _ = screen(p7_plan, p7_lock, multi_steps)
    check("CAL-0 B4a T76(판정기 쪽 방어) 장면 2개 계획 target 에 scene 없음 → 종료 2", unscoped_code == 2 and "scene 이 있어야" in unscoped_output, unscoped_output[-300:])

    derived_items = [{"step": index, "variant": "v00", "metrics": {"derived.decay": None}, "samples": {}, "rule_score": 0, "bands": {}, "shot_count": 0} for index in range(3)]
    derived_targets = calib_judge.resolve_targets({"metrics": {"targets": [{"metric": "derived.decay", "low": 1.0, "high": 3.0, "tolerance": 0.1}]}}, "s", derived_items)
    undetermined = calib_judge.judge_staged_scene("s", derived_items, derived_targets, True, False)
    check("CAL-0 B4a T38 맞춤 점 2개 → 파생 None → 목표면 장면 판정 불가(대조 통과 아님)",
          calib_judge.series_value("half_life", [(0.0, 1.0), (1.0, 0.5)]) is None and undetermined["determinable"] is False and not undetermined["control_ok"]
          and any("derived.decay" in reason for reason in undetermined["undetermined_reasons"]), undetermined["undetermined_reasons"])

    image_dir = root / "png"
    image_dir.mkdir(parents=True, exist_ok=True)

    def write_png(name, left, right):
        image = Image.new("RGB", (256, 144))
        draw = ImageDraw.Draw(image)
        for x0, (top, bottom) in ((0, left), (128, right)):
            draw.rectangle([x0, 0, x0 + 127, 63], fill=(top, top, top))
            draw.rectangle([x0, 64, x0 + 127, 143], fill=(bottom, bottom, bottom))
        path = image_dir / f"{name}.png"
        image.save(path)
        return prereg_lock.relative_text(path)

    regions_path = write_document(root / "g1" / "regions.json", {"regions": [{"name": "haze", "kind": "fog", "box": [0, 0, 128, 144]}, {"name": "body", "kind": "volume", "box": [128, 0, 256, 144]}]})
    targets_path = write_document(root / "g1" / "targets.json", {"note": "B4a fixture", "region_metrics": {"haze": {"atmosphere.p95_over_p5": [1.0, 5.0]}}, "kind_metrics": {"volume": {"atmosphere.p95_over_p5": [1.0, 4.0]}}})
    look_scene = {"name": "fog", "level": "/Game/CAL0/Level", "camera": {}, "settle": 6.0, "sources": ["look"], "shots": {"count": 5, "interval": 0.5},
                  "regions_template": prereg_lock.relative_text(regions_path), "targets": prereg_lock.relative_text(targets_path)}
    replicate_images = [write_png(f"rep{index}", (64, level), (32, level // 2)) for index, level in enumerate((128, 160, 192, 224, 240))]

    def look_step(index, images, saved=None, metrics=None, times=None):
        step = {"step": index, "name": f"s{index:02d}_v00", "variant": "v00", "valid": True, "errors": [], "images": list(images), "image_sha256": [prereg_lock.file_sha256(W / image) for image in images],
                "shots_expected": 5, "shots_saved": saved or [True] * 5, "shot_game_times": times or [6.0, 6.5, 7.0, 7.5, 8.0]}
        if metrics is not None:
            step["metrics"] = metrics
        return step

    look_steps = [look_step(index, replicate_images, metrics={"probe.flow": [1.0, 2.0, 3.0]}) for index in range(3)]
    items, band_lookup, _, _ = calib_judge.measure_staged_scene(look_scene, look_scene, look_steps, ["look", "probe"], root / "g1" / "measure")
    metrics = items[0]["metrics"]
    resolved = {target["metric"]: target for target in calib_judge.resolve_targets({"metrics": {"targets": [{"metric": "haze.atmosphere.p95_over_p5"}, {"metric": "body.atmosphere.p95_over_p5"}]}}, "fog", items, band_lookup)}
    check("CAL-0 B4a T29 일반 경로: 영역 PNG 5장(p95/p5 = 2·2.5·3·3.5·3.75) 중앙값 3.0 = 손계산, 띠 출처 region [1,5]",
          close(metrics.get("haze.atmosphere.p95_over_p5"), 3.0, 1e-6) and resolved["haze.atmosphere.p95_over_p5"]["band_source"] == "region" and resolved["haze.atmosphere.p95_over_p5"]["high"] == 5.0, {key: value for key, value in metrics.items() if "p95_over_p5" in key})
    check("CAL-0 B4a T30 kind 띠만 있는 영역(body·volume) → 띠 출처 kind [1,4]", resolved["body.atmosphere.p95_over_p5"]["band_source"] == "kind" and resolved["body.atmosphere.p95_over_p5"]["high"] == 4.0, resolved["body.atmosphere.p95_over_p5"])
    check("CAL-0 B4a T31 <n>. 접두 장별 지표와 중앙값 지표가 둘 다 측정에 있음", close(metrics.get("0.haze.atmosphere.p95_over_p5"), 2.0, 1e-6) and close(metrics.get("4.haze.atmosphere.p95_over_p5"), 3.75, 1e-6) and "haze.atmosphere.p95_over_p5" in metrics)
    try:
        calib_judge.resolve_targets({"metrics": {"targets": [{"metric": "haze.atmosphere.p95_over_p6"}]}}, "fog", items, band_lookup)
        typo_message = ""
    except calib_judge.JudgeError as error:
        typo_message = str(error)
    check("CAL-0 B4a T32 목표 지표 이름 오타 → 판정 불가(입력 오류) + 가까운 이름 3개 중 정답", "측정값에 없다" in typo_message and "가까운 이름" in typo_message and "haze.atmosphere.p95_over_p5" in typo_message, typo_message)
    check("CAL-0 B4a T33 frame_difference 짝짓기: 같은 단계 k≥1 장에만 키", "1.haze.frame_difference.mean" in metrics and "4.haze.frame_difference.mean" in metrics and "0.haze.frame_difference.mean" not in metrics)
    check("CAL-0 B4a T34 kind volume 영역 → atmosphere 키(fog 로 측정)", "body.atmosphere.p95_over_p5" in metrics and "body.atmosphere.fine_detail" in metrics)
    check("CAL-0 B4a T75 sources [look, probe] 한 단계 → 이미지 지표 + probe 지표 합침(probe 목록은 장 표본·중앙값)", "haze.luminance_mean" in metrics and metrics.get("probe.flow") == 2.0 and metrics.get("2.probe.flow") == 3.0 and items[0]["samples"].get("probe.flow") == [1.0, 2.0, 3.0])
    calib_judge.add_derived_metrics(items, look_steps, [{"name": "glow_ratio", "op": "ratio", "a": "haze.luminance_mean", "b": "body.luminance_mean"}, {"name": "decay", "op": "half_life", "a": "haze.luminance_mean"}], "fog", False)
    check("CAL-0 B4a T36 ratio(두 영역 밝기 비 2.0 PNG) → derived 2.0±0.02, 시계열 op 는 replicate 장면에서 건너뜀", close(items[0]["metrics"].get("derived.glow_ratio"), 2.0, 0.02) and "derived.decay" not in items[0]["metrics"], items[0]["metrics"].get("derived.glow_ratio"))

    g1_plan, g1_lock = make_plan("g1", [look_scene], [{"metric": "haze.atmosphere.p95_over_p5"}, {"metric": "body.atmosphere.p95_over_p5"}])
    good = make_sweep(g1_plan, g1_lock, "verify", {"fog": [look_step(index, replicate_images) for index in range(3)]}, "verify")
    good_code, good_output = judge("--sweep", good, "--lock", g1_lock)
    short = make_sweep(g1_plan, g1_lock, "verify", {"fog": [look_step(0, replicate_images), look_step(1, replicate_images[:4]), look_step(2, replicate_images)]}, "verify-short")
    short_code, short_output = judge("--sweep", short, "--lock", g1_lock)
    unsaved = make_sweep(g1_plan, g1_lock, "verify", {"fog": [look_step(0, replicate_images), look_step(1, replicate_images, saved=[True, True, False, True, True]), look_step(2, replicate_images)]}, "verify-unsaved")
    unsaved_code, unsaved_output = judge("--sweep", unsaved, "--lock", g1_lock)
    good_judge = load_json(good.parent / "judge.json") if (good.parent / "judge.json").is_file() else {}
    check("CAL-0 B4a T74 단계 이미지 4장(잠긴 count 5) / saved 거짓 → 판정 종료 2, 정상 5장 verify 는 끝까지 종료 0",
          short_code == 2 and "shots.count 5" in short_output and unsaved_code == 2 and "saved" in unsaved_output and good_code == 0 and dig(good_judge, "scenes", 0, "control_ok") is True
          and close(dig(good_judge, "scenes", 0, "noise", "haze.atmosphere.p95_over_p5", "median_floor"), 1.75 / math.sqrt(5), 1e-5), (short_output[-200:], unsaved_output[-200:], good_output[-300:]))

    single_scene = dict(look_scene, shots={"count": 1, "interval": 0.5})
    p8_plan, p8_lock = make_plan("p8", [single_scene], [{"metric": "haze.atmosphere.p95_over_p5"}])
    single_steps = [dict(look_step(index, replicate_images[:1]), shots_expected=1, shots_saved=[True]) for index in range(3)]
    single_code, single_output = judge("--sweep", make_sweep(p8_plan, p8_lock, "verify", {"fog": single_steps}, "verify"), "--lock", p8_lock)
    check("CAL-0 B4a T72(판정기 쪽 방어) 단계 실행인데 look shots.count 1 → 종료 2 '표본 ≥3'", single_code == 2 and "표본 ≥3" in single_output, single_output[-300:])

    series_images = [write_png(f"series{index}", (round(204 * 2 ** (-index / 2)),) * 2, (50 + 25 * index,) * 2) for index in range(5)]
    series_scene = dict(look_scene, shots={"count": 5, "interval": 1.0, "kind": "series"})
    series_steps = [look_step(index, series_images, times=[10.0, 11.0, 12.0, 13.0, 14.0]) for index in range(3)]
    series_items, _, _, _ = calib_judge.measure_staged_scene(series_scene, series_scene, series_steps, ["look"], root / "g1" / "series")
    calib_judge.add_derived_metrics(series_items, series_steps, [{"name": "decay", "op": "half_life", "a": "haze.luminance_mean"}, {"name": "rise", "op": "slope", "a": "body.luminance_mean"}], "fog", True)
    series_metrics = series_items[0]["metrics"]
    check("CAL-0 B4a T37 half_life(반감기 2초 5장) → 2.0±0.1, r2 > 0.99", close(series_metrics.get("derived.decay"), 2.0, 0.1) and (series_metrics.get("derived.decay.r2") or 0) > 0.99, (series_metrics.get("derived.decay"), series_metrics.get("derived.decay.r2")))
    check("CAL-0 B4a T92 slope(선형 증가 5장, 25/255 per 게임초) → ±2%", close(series_metrics.get("derived.rise"), 25 / 255, 0.02 * 25 / 255), series_metrics.get("derived.rise"))

    size, apex_row = 101, 50
    slope_limit = math.tan(math.radians(20.0))
    grid_path = write_document(root / "grid" / "wake.json", {"values": [[1.0 if column > 0 and abs(row - apex_row) <= column * slope_limit else 0.0 for column in range(size)] for row in range(size)], "radial_center": [0.0, 0.5]})
    extent_item = {"step": 0, "variant": "v00", "metrics": {}, "samples": {}, "rule_score": 0, "bands": {}, "shot_count": 0}
    calib_judge.add_derived_metrics([extent_item], [{"step": 0, "grids": {"wake": prereg_lock.relative_text(grid_path)}}], [{"name": "wake", "op": "extent", "grid": "wake", "threshold": 0.5}], "s", False)
    check("CAL-0 B4a T40 extent(RT 격자 쐐기 반각 20°) → half_angle_deg 20±1·length 100", close(extent_item["metrics"].get("derived.wake.half_angle_deg"), 20.0, 1.0) and close(extent_item["metrics"].get("derived.wake.length"), 100.0, 1e-6)
          and 0 < (extent_item["metrics"].get("derived.wake.area_fraction") or 0) < 1, extent_item["metrics"])

    sample_steps = [{"step": index, "name": f"s{index:02d}_v00", "variant": "v00", "valid": True, "images": [], "metrics": {"probe.wake.mean": 2.0},
                     "probe_samples": [{"time": at, "metrics": {"probe.wake.mean": value}, "grid_paths": {"wake": prereg_lock.relative_text(grid_path)}} for at, value in ((1.0, 1.9), (1.5, 2.0), (2.0, 2.2))]} for index in range(3)]
    sample_items, _, _, _ = calib_judge.measure_staged_scene({"name": "s1"}, {"name": "s1"}, sample_steps, ["probe"], root / "samples")
    calib_judge.add_derived_metrics(sample_items, sample_steps, [{"name": "wake", "op": "extent", "grid": "wake", "threshold": 0.5}], "s1", False)
    sample_metrics = sample_items[0]["metrics"]
    check("CAL-0 B4a 출처 probe_samples(룩데브 probe.samples 그대로) → 장 표본 [1.9, 2.0, 2.2]·중앙값·표본별 격자 extent 3장",
          sample_items[0]["samples"].get("probe.wake.mean") == [1.9, 2.0, 2.2] and sample_metrics.get("probe.wake.mean") == 2.0 and sample_metrics.get("2.probe.wake.mean") == 2.2
          and close(sample_metrics.get("derived.wake.half_angle_deg"), 20.0, 1.0) and len(sample_items[0]["samples"].get("derived.wake.half_angle_deg", [])) == 3, sample_metrics)

    def judge_edited(plan_path, lock_path, stage, scene_steps, folder, edit, *arguments, extra=None):
        sweep_path = make_sweep(plan_path, lock_path, stage, scene_steps, folder, extra)
        sweep = load_json(sweep_path)
        edit(sweep)
        write_document(sweep_path, sweep)
        code, output = judge("--sweep", sweep_path, "--lock", lock_path, *arguments)
        result_path = sweep_path.parent / ("screen.json" if stage == "screen" else "judge.json")
        return code, output, load_json(result_path) if result_path.is_file() else {}

    def skip_scene(name):
        def edit(sweep):
            next(scene for scene in sweep["scenes"] if scene["name"] == name).update(steps=[], sequence=[], skipped="이 단계의 변형이 이 장면에서 돌지 않는다(param scenes)")
        return edit

    def screen_fields(screen_sweep_path):
        return {"screen_result": prereg_lock.relative_text(screen_sweep_path.parent / "screen.json"), "screen_result_sha256": prereg_lock.file_sha256(screen_sweep_path.parent / "screen.json")}

    two_targets = [dict(target_m, scene="s1"), dict(target_m, scene="s2")]
    l1_plan, l1_lock = make_plan("fix-l1", [probe_scene("s1"), probe_scene("s2")], two_targets)
    two_steps = {"s1": screen_steps["s1"], "s2": screen_steps["s1"]}
    tamperings = {"목표 띠": lambda sweep: sweep["metrics"]["targets"][0].update(low=3.0, high=4.5), "비용": lambda sweep: sweep["params"][0].update(cost="lower_costs_more"),
                  "장면 집합": lambda sweep: sweep["scenes"].pop(1), "장면 sources": lambda sweep: sweep["scenes"][0].update(sources=["look"]),
                  "values": lambda sweep: sweep["params"][1].update(values=[0.5, 1.0]), "param scenes": lambda sweep: sweep["params"][0].update(scenes=["s1"])}
    honest_code, honest_output, _ = judge_edited(l1_plan, l1_lock, "screen", two_steps, "fix-l1-honest", lambda sweep: None)
    tampered = {label: judge_edited(l1_plan, l1_lock, "screen", two_steps, f"fix-l1-{index}", edit)[:2] for index, (label, edit) in enumerate(tamperings.items())}
    check("CAL-0 반박 lock1 sweep.json 규칙 사본(목표 띠·비용·장면 집합·sources·values·param scenes)이 잠긴 계획과 다르면 종료 2 '사전 등록 잠금', 정직한 사본은 통과",
          honest_code == 0 and all(code == 2 and "사전 등록 잠금" in output for code, output in tampered.values()),
          (honest_output[-200:], {label: (code, output[-160:]) for label, (code, output) in tampered.items()}))

    prior_path = p1_plan.parent / "confirm" / "judge.json"
    q_plan, q_lock = make_plan("fix-q", [probe_scene("s1")], [target_m])
    q_verify = make_sweep(q_plan, q_lock, "verify", {"s1": rows(("v00", {"probe.m": 7.5}), ("v00", {"probe.m": 7.52}), ("v00", {"probe.m": 7.48}))}, "verify")
    q_code, q_output = judge("--sweep", q_verify, "--lock", q_lock, "--verify-against", prior_path)
    close_report = load_json(verify_close.parent / "judge.json") if (verify_close.parent / "judge.json").is_file() else {}
    check("CAL-0 반박 lock2 --verify-against 앞 판정의 plan·plan_sha256·lock_sha256 이 이번 verify 와 다르면 종료 2, 통과 보고에 앞 판정 sha256",
          q_code == 2 and "verify-against" in q_output and dig(close_report, "verify", "against_sha256") == prereg_lock.file_sha256(prior_path), (q_code, q_output[-300:], dig(close_report, "verify", "against_sha256")))

    scoped_plan, scoped_lock = make_plan("fix-a1", [probe_scene("s1"), probe_scene("s2")], two_targets, scopes={knob_a: ["s1"], knob_b: ["s2"]})
    scoped_screen, scoped_screen_code, _, _ = screen(scoped_plan, scoped_lock, {
        "s1": rows(("v00", {"probe.m": 5.0}), ("v01", {"probe.m": 4.0}), ("v02", {"probe.m": 7.5}), ("v00", {"probe.m": 5.05}), ("v00", {"probe.m": 4.95})),
        "s2": rows(("v00", {"probe.m": 5.0}), ("v03", {"probe.m": 5.03}), ("v04", {"probe.m": 4.98}), ("v00", {"probe.m": 5.05}), ("v00", {"probe.m": 4.95}))})
    scoped_code, scoped_output, scoped = judge_edited(scoped_plan, scoped_lock, "confirm", {"s1": confirm_steps["s1"], "s2": []}, "confirm", skip_scene("s2"),
                                                      "--screen-sweep", scoped_screen, extra=screen_fields(scoped_screen))
    inside_code, inside_output, _ = judge_edited(scoped_plan, scoped_lock, "confirm", {"s1": [], "s2": confirm_steps["s1"]}, "confirm-inside", skip_scene("s1"),
                                                 "--screen-sweep", scoped_screen, extra=screen_fields(scoped_screen))
    check("CAL-0 반박 adopt1 param scenes 계획 confirm: best 손잡이 범위 밖 skipped 장면은 건너뛰어 adopt 참 / 범위 안 skipped 는 종료 2",
          scoped_screen_code == 0 and scoped_code == 0 and dig(scoped, "adopt", "decision") is True and dig(scoped, "adopt", "variant") == "v02" and inside_code == 2,
          (scoped_output[-300:], dig(scoped, "adopt", "reasons"), inside_code, inside_output[-200:]))

    scoped_prior = scoped_plan.parent / "confirm" / "judge.json"
    unjudged_s2 = rows(("v00", {"probe.m": 5.0}), ("v00", {"probe.m": 5.3}), ("v00", {"probe.m": 4.7}))
    verify_runs = {}
    for folder, s1_rows in (("verify-n2-match", rows(("v00", {"probe.m": 7.5}), ("v00", {"probe.m": 7.52}), ("v00", {"probe.m": 7.48}))),
                            ("verify-n2-drift", rows(("v00", {"probe.m": 5.0}), ("v00", {"probe.m": 5.02}), ("v00", {"probe.m": 4.98})))):
        code, output = judge("--sweep", make_sweep(scoped_plan, scoped_lock, "verify", {"s1": s1_rows, "s2": unjudged_s2}, folder), "--lock", scoped_lock, "--verify-against", scoped_prior)
        report_path = scoped_plan.parent / folder / "judge.json"
        verify_runs[folder] = (code, output, load_json(report_path) if report_path.is_file() else {})
    match_code, match_output, match_report = verify_runs["verify-n2-match"]
    drift_code, drift_output, drift_report = verify_runs["verify-n2-drift"]
    check("CAL-0 재반박 N2 범위 손잡이 verify: 앞 confirm 판정에 없는 장면 s2 는 건너뜀 기록·판정 제외 → 값 일치 종료 0 / 값 표류 종료 1·대조 통과(control_ok 참)·verify.passed 거짓(되돌림 대상)",
          match_code == 0 and [scene["name"] for scene in match_report.get("scenes", [])] == ["s1"] and dig(match_report, "skipped_scenes", 0, "name") == "s2"
          and dig(match_report, "verify", "passed") is True and drift_code == 1 and drift_report.get("scenes") and all(scene["control_ok"] for scene in drift_report["scenes"])
          and dig(drift_report, "verify", "passed") is False,
          (match_code, match_output[-300:], drift_code, drift_output[-300:], dig(drift_report, "verify")))

    untouched_plan, untouched_lock = make_plan("fix-k9", [probe_scene("s1"), probe_scene("s2")], two_targets, scopes={knob_a: ["s1"], knob_b: ["s1"]})
    untouched_code, untouched_output, untouched = judge_edited(untouched_plan, untouched_lock, "screen", {"s1": screen_steps["s1"], "s2": []}, "screen", skip_scene("s2"))
    untouched_sweep_path = untouched_plan.parent / "screen" / "sweep.json"
    untouched_confirm_code, untouched_confirm_output, untouched_confirm = judge_edited(untouched_plan, untouched_lock, "confirm", {"s1": confirm_steps["s1"], "s2": []}, "confirm", skip_scene("s2"),
                                                                                       "--screen-sweep", untouched_sweep_path, extra=screen_fields(untouched_sweep_path))
    covered_code, covered_output, _ = judge_edited(scoped_plan, scoped_lock, "screen", {"s1": screen_steps["s1"], "s2": []}, "screen-covered", skip_scene("s2"))
    check("K9 screen: 어느 손잡이 param scenes 에도 없는 probe 장면 s2(단계 0개·skipped)는 건너뛰어 종료 2 아님·scenes [s1]·skipped_scenes [s2], 그 screen 의 confirm 재판정 일치 / 손잡이 범위 안 s2 가 비면 종료 2",
          untouched_code in (0, 1) and [scene["name"] for scene in untouched.get("scenes", [])] == ["s1"] and dig(untouched, "skipped_scenes", 0, "name") == "s2" and len(untouched["skipped_scenes"]) == 1
          and untouched_confirm_code in (0, 1) and "재판정 불일치" not in untouched_confirm_output and dig(untouched_confirm, "skipped_scenes", 0, "name") == "s2" and covered_code == 2 and "무효 단계" in covered_output,
          (untouched_code, untouched_output[-300:], untouched.get("skipped_scenes"), untouched_confirm_code, untouched_confirm_output[-300:], covered_code, covered_output[-200:]))

    narrowed_code, narrowed_output, _ = judge_edited(scoped_plan, scoped_lock, "screen", {"s1": screen_steps["s1"], "s2": []}, "screen-narrowed",
                                                     lambda sweep: (skip_scene("s2")(sweep), sweep["params"][1].update(scenes=["s1"])))
    narrowed_sweep = load_json(scoped_plan.parent / "screen-narrowed" / "sweep.json")
    try:
        calib_judge.find_screen_skippable_scenes(narrowed_sweep, load_json(scoped_plan))
        narrowed_error = ""
    except calib_judge.JudgeError as error:
        narrowed_error = str(error)
    honest_skippable = calib_judge.find_screen_skippable_scenes(load_json(untouched_sweep_path), load_json(untouched_plan))
    check("K9 반박 screen 건너뜀은 잠긴 계획 params[].scenes 기준: sweep params[1].scenes 를 [s1] 로 손편집해 범위 안 s2 를 빼면 종료 2, 판정 함수도 JudgeError(sweep 손편집) / 정직한 sweep 은 {s2}",
          narrowed_code == 2 and "sweep 손편집" in narrowed_error and honest_skippable == {"s2"}, (narrowed_code, narrowed_output[-200:], narrowed_error, honest_skippable))

    offline_scene = {"name": "s_off", "level": "/Game/CAL0/Level", "camera": {}, "settle": 1.0, "sources": ["offline"]}
    lag_target = {"metric": "offline.lag", "low": 1.1, "high": 1.3, "weight": 1.0, "scene": "s_off"}
    mixed_plan, mixed_lock = make_plan("fix-a2", [offline_scene, probe_scene("s_p")], [lag_target, dict(target_m, scene="s_p")])
    lag = {"v00": 1.0, "v01": 0.9, "v02": 1.2, "v03": 1.0, "v04": 1.0}

    def lag_rows(*variants):
        return [(variant, {"offline.lag": lag[variant]}) for variant in variants]

    mixed_screen, mixed_screen_code, _, _ = screen(mixed_plan, mixed_lock, {"s_off": lag_rows("v00", "v01", "v02", "v00", "v03", "v04", "v00"), "s_p": screen_steps["s1"]})
    mixed_confirm = {"s_off": lag_rows("v00", "v02", "v00", "v02", "v00"), "s_p": confirm_steps["s1"]}
    mixed_code, mixed_output, mixed = confirm(mixed_plan, mixed_lock, mixed_confirm, mixed_screen)

    def break_determinism(sweep):
        for step in sweep["scenes"][0]["steps"]:
            step["offline"]["repeat_sha256"] = "0" * 64

    _, _, unrepeated = judge_edited(mixed_plan, mixed_lock, "confirm", mixed_confirm, "confirm-unrepeated", break_determinism, "--screen-sweep", mixed_screen, extra=screen_fields(mixed_screen))
    check("CAL-0 반박 adopt2 offline+probe 혼합: offline 장면은 지문에서 빼고 결정론 증거로 → adopt 참·교집합 0 / 결정론 증거 없으면 거짓",
          mixed_screen_code == 0 and mixed_code == 0 and dig(mixed, "adopt", "decision") is True and dig(mixed, "adopt", "evidence", "image_overlap") == 0
          and dig(unrepeated, "adopt", "decision") is False and any("결정론" in reason for reason in dig(unrepeated, "adopt", "reasons") or []),
          (dig(mixed, "adopt", "reasons"), dig(mixed, "adopt", "evidence", "image_overlap"), dig(unrepeated, "adopt", "reasons"), mixed_output[-200:]))

    fx9 = load_json(W / "Tools/FluidFX/calib/plans/fx9_hvol_vs_svol.json")
    step_count = next(param for param in fx9["params"] if str(param.get("json_path", "")).endswith(".MaxStepCount"))
    canonical_steps = load_json(CANONICAL)["Globals"]["TDFluidHeterogeneousVolumeRenderProfile"]["MaxStepCount"]
    blocked = [value for value in step_count["values"] if calib_judge.cost_direction(step_count.get("cost"), value, canonical_steps) > 0]
    check("CAL-0 반박 adopt3 fx9 MaxStepCount: 정본 -1(엔진 기본 512)이라 끝 값 256 은 비용 감소 → cost_blocked 아님",
          canonical_steps == -1 and blocked == [], (canonical_steps, step_count, blocked))

    campfire_sweep = load_json(TEST_DIR / "campfire" / "sweep.json")
    campfire_scene = campfire_sweep["scenes"][0]
    fire_items, _, _ = calib_judge.measure_fire_scene(campfire_scene, campfire_scene["steps"], root / "fire")
    fire_pairs = [(item["metrics"].get(name), item["metrics"].get("0." + name)) for item in fire_items for name in item["bands"]]
    check("CAL-0 B4a T73 불 경로 중앙값 이름 <영역>.<지표> 가 한 장 스윕에서 0. 이름과 같은 값", fire_pairs and all(median == first for median, first in fire_pairs), fire_pairs[:4])

    expected_rejudge = {"campfire-run1": (True, "v01", {"v00": 3.524208828, "v01": 3.004812262, "v02": 3.566825772}),
                        "campfire-run2": (True, "v02", {"v00": 3.384534853, "v01": 3.702105652, "v02": 3.292333965}),
                        "campfire-r4-run1": (False, "v02", {"v00": 3.789017843, "v01": 3.484807815, "v02": 3.452179595})}
    present = [name for name in expected_rejudge if name in rejudged and rejudged[name][1]]
    if not present:
        print("SKIP CAL-0 B4a T35 수용 시험 산출 없음")
        return
    check("CAL-0 B4a T35 모닥불 불 경로 재판정(고정 사본)이 B4a 전과 같음(control_ok·후보·점수 9자리, Q3)",
          all((rejudged[name][1]["control_ok"], rejudged[name][1]["candidate"]["variant"], {key: round(value, 9) for key, value in rejudged[name][1]["candidate"]["scores"].items()}) == expected_rejudge[name] for name in present),
          {name: (rejudged[name][1]["control_ok"], rejudged[name][1]["candidate"]["variant"]) for name in present})


def check_cal0_measure():
    from datetime import datetime, timedelta
    output_dir = TEST_DIR / "cal0-b4b"
    output_dir.mkdir(parents=True, exist_ok=True)
    measure_script = W / "Tools" / "FluidFX" / "measure_fx_look.py"
    baseline_script = output_dir / "baseline" / "measure_fx_look.py"
    baseline_script.parent.mkdir(parents=True, exist_ok=True)
    baseline_script.write_bytes(subprocess.run(["git", "show", "5674bb4c:Tools/FluidFX/measure_fx_look.py"], capture_output=True, cwd=W).stdout)

    def write_gray(name, value_at):
        image = Image.new("L", (256, 144))
        image.putdata([value_at(x, y) for y in range(144) for x in range(256)])
        image.convert("RGB").save(output_dir / name)
        return (output_dir / name).as_posix()

    def measure(script, name, regions_path, *extra):
        output_path = output_dir / f"{name}.json"
        exit_code, text = python(str(script), "--regions", str(regions_path), "--output", str(output_path), *extra)
        return exit_code, output_path.read_bytes() if output_path.is_file() else b"", text

    def write_regions(name, images):
        regions_path = output_dir / f"{name}_regions.json"
        regions_path.write_text(json.dumps({"images": images}), encoding="utf-8")
        return regions_path

    def region_metrics(document_bytes, image_id, region_name):
        image = next(item for item in json.loads(document_bytes)["images"] if item["id"] == image_id)
        return next(region for region in image["regions"] if region["name"] == region_name)["metrics"]

    stripes = write_gray("stripes.png", lambda x, y: int(round(128 + 80 * math.sin(2 * math.pi * x / 32))))
    groove = write_gray("groove.png", lambda x, y: 60 if 122 <= x < 134 else 200)
    gradient = write_gray("gradient.png", lambda x, y: 30 + x // 2 + (y // 9) * 3)
    analysed = write_regions("analysed", [
        {"path": stripes, "id": "stripes", "regions": [{"name": "tile", "kind": "fog", "box": [0, 0, 256, 144], "analyses": ["autocorrelation", "spectrum"]}]},
        {"path": groove, "id": "groove", "regions": [{"name": "groove_line", "kind": "snow", "box": [32, 40, 224, 104], "analyses": ["line_profile"], "line": [[64, 72], [191, 72]], "cm_per_px": 2}]},
        {"path": gradient, "id": "volume", "regions": [{"name": "volume", "kind": "volume", "box": [8, 8, 248, 136]}, {"name": "fog", "kind": "fog", "box": [8, 8, 248, 136]}]}])
    exit_code, measured, text = measure(measure_script, "analysed", analysed)
    tile = region_metrics(measured, "stripes", "tile") if exit_code == 0 else {}
    check("T50 B4b 주기 32px 줄무늬: autocorrelation period_px 32±1·secondary_peak > 0.5, spectrum dominant_wavelength_px 32±1",
          abs(tile.get("autocorrelation", {}).get("period_px", 0) - 32) <= 1 and tile["autocorrelation"]["secondary_peak"] > 0.5 and abs(tile.get("spectrum", {}).get("dominant_wavelength_px", 0) - 32) <= 1, (exit_code, tile, text[-300:]))
    line = region_metrics(measured, "groove", "groove_line").get("line_profile", {}) if exit_code == 0 else {}
    check("T51 B4b 폭 12px 홈 line_profile(cm_per_px 2): groove_width_cm 24±2·gap_count 0", abs((line.get("groove_width_cm") or 0) - 24) <= 2 and line.get("gap_count") == 0, line)
    volume = region_metrics(measured, "volume", "volume") if exit_code == 0 else {}
    check("B4b kind volume 은 atmosphere 별칭(같은 상자 fog 와 값 동일)·분석 키 없음", "atmosphere" in volume and volume == region_metrics(measured, "volume", "fog") and "autocorrelation" not in volume, volume)

    plain_regions = [{"name": "fire", "kind": "fire", "box": [96, 0, 160, 144]}, {"name": "embers", "kind": "embers", "box": [0, 0, 128, 72]}, {"name": "cloud", "kind": "cloud", "box": [0, 0, 256, 144]},
                     {"name": "fog", "kind": "fog", "box": [16, 16, 240, 128], "object_height_px": 40}, {"name": "dust", "kind": "dust", "box": [0, 72, 128, 144]}, {"name": "snow", "kind": "snow", "box": [128, 0, 256, 72]},
                     {"name": "sky", "kind": "sky", "box": [128, 72, 256, 144]}, {"name": "water", "kind": "water", "box": [32, 32, 224, 112]}]
    plain = write_regions("plain", [{"path": stripes, "id": "a", "view": "v", "condition": "c0", "regions": plain_regions},
                                    {"path": groove, "id": "b", "view": "v", "condition": "c1", "previous": stripes, "regions": plain_regions},
                                    {"path": gradient, "id": "c", "view": "v", "condition": "c2", "previous": groove, "regions": plain_regions}])
    base_run, new_run = measure(baseline_script, "plain_base", plain), measure(measure_script, "plain_new", plain)
    check("T52 B4b analyses 없는 영역: 기본 모드 결과 JSON·표준 출력이 기준 커밋 5674bb4c 와 바이트 동일", base_run[0] == new_run[0] == 0 and base_run[1] == new_run[1] and base_run[2] == new_run[2] and b"autocorrelation" not in new_run[1],
          (base_run[0], new_run[0], len(base_run[1]), len(new_run[1]), new_run[2][-300:]))

    judged_regions = TEST_DIR / "campfire" / "judge" / "campfire_regions.json"
    live2_targets = W / "Saved" / "AgentOps" / "20261020" / "fx8c" / "scratch" / "live2-targets" / "targets.json"
    fire_document = {"images": load_json(judged_regions)["images"][:2]} if judged_regions.is_file() else {"images": []}
    for image in fire_document["images"]:
        image["path"] = (W / image["path"]).as_posix()
    fire = write_regions("fire_targets", fire_document["images"])
    base_run = measure(baseline_script, "fire_base", fire, "--targets", str(live2_targets))
    new_run = measure(measure_script, "fire_new", fire, "--targets", str(live2_targets))
    check("B4b 불 경로(--targets) 측정: 결과 JSON·격차표가 기준 커밋과 바이트 동일(모닥불 가짜 스윕 2장)", fire_document["images"] and base_run[0] == new_run[0] == 0 and base_run[1] == new_run[1] and base_run[2] == new_run[2],
          (len(fire_document["images"]), base_run[0], new_run[0], new_run[2][-300:]))

    small = write_regions("small", [{"path": stripes, "id": "a", "regions": [{"name": "tile", "kind": "fog", "box": [0, 0, 31, 144], "analyses": ["autocorrelation"]}]}])
    unknown = write_regions("unknown", [{"path": stripes, "id": "a", "regions": [{"name": "tile", "kind": "fog", "box": [0, 0, 64, 64], "analyses": ["histogram"]}]}])
    small_run, unknown_run = measure(measure_script, "small", small), measure(measure_script, "unknown", unknown)
    check("B4b 분석 거부: 31px 영역 → 종료 2 '분석에 너무 작음' / 모르는 분석 이름 → 종료 2", small_run[0] == 2 and "분석에 너무 작음" in small_run[2] and unknown_run[0] == 2 and "analyses" in unknown_run[2],
          (small_run[2][-200:], unknown_run[2][-200:]))

    root = output_dir / "lock-root"
    shutil.rmtree(root, ignore_errors=True)
    for tool_text in prereg_lock.JUDGE_TOOL_FILES:
        (root / tool_text).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(W / tool_text, root / tool_text)

    def put(file_text, content):
        (root / file_text).parent.mkdir(parents=True, exist_ok=True)
        (root / file_text).write_text(content if isinstance(content, str) else json.dumps(content), encoding="utf-8")
        return file_text

    regions_text = put("Tools/FluidFX/calib/regions/b4b_fire.json", {"regions": [{"name": "fire", "kind": "fire", "box": [0, 0, 64, 64]}]})
    targets_text = put("Tools/FluidFX/calib/targets/b4b_fire.json", {"live2_targets": "Saved/live2/targets.json", "kinds": ["fire"]})
    live2_text = put("Saved/live2/targets.json", {"targets": []})
    script_text = put("Tools/FluidFX/check_fluid_foliage_bend.py", "print('reed')\n")
    offline_scene = {"name": "reed", "offline": {"command": ["{python}", script_text, "--tuning", "{tuning}", "--json", "{output}"]}}
    plan_text = put("Tools/FluidFX/calib/plans/b4b.json", {"id": "b4b", "scenes": [{"name": "fire", "regions_template": regions_text, "targets": targets_text}, offline_scene]})
    lock_tool = str(root / "Tools" / "FluidFX" / "prereg_lock.py")
    required = [plan_text, regions_text, targets_text, live2_text, script_text, *prereg_lock.JUDGE_TOOL_FILES]

    def lock_files(name, *arguments):
        lock_path = output_dir / f"{name}-lock.json"
        lock_path.unlink(missing_ok=True)
        return python(lock_tool, "lock", "--out", str(lock_path), *arguments)[0], lock_path

    def write_sweep(name, lock_path, **fields):
        created_at = datetime.fromisoformat(load_json(lock_path).get("created_at") or "2026-10-30T00:00:00+09:00")
        sweep = {"plan": plan_text, "plan_sha256": prereg_lock.file_sha256(root / plan_text), "stage": "screen", "lock": str(lock_path), "lock_sha256": prereg_lock.file_sha256(lock_path),
                 "started_at": (created_at + timedelta(minutes=1)).isoformat(),
                 "scenes": [{"name": "fire", "sources": ["look"], "regions_template": regions_text, "targets": targets_text}, {"name": "reed", "sources": ["look"], "regions_template": None, "targets": None}]}
        sweep.update(fields)
        sweep = {key: value for key, value in sweep.items() if value is not MISSING}
        sweep_path = output_dir / f"{name}-sweep.json"
        sweep_path.write_text(json.dumps(sweep), encoding="utf-8")
        return sweep_path

    def verify(lock_path, sweep_path=None):
        return python(lock_tool, "verify", "--lock", str(lock_path), *(["--sweep", str(sweep_path)] if sweep_path else []))

    lock_code, plan_lock = lock_files("plan", "--plan", plan_text, "--note", "b4b")
    lock_document = load_json(plan_lock) if plan_lock.is_file() else {}
    good = verify(plan_lock, write_sweep("good", plan_lock)) if plan_lock.is_file() else (None, "")
    check("T65 B4b C8 잠금 v2: --plan 필수 집합(계획·영역 틀·목표·live2·offline 스크립트·도구 7개)·version 2·created_at, 닫힌 세계 스윕 판정 종료 0",
          lock_code == 0 and lock_document.get("version") == 2 and lock_document.get("created_at") and sorted(lock_document.get("files", {})) == sorted(required) and good[0] == 0, (lock_code, sorted(lock_document.get("files", {})), good[1][-300:]))

    lock_code, file_lock = lock_files("file", *[part for text in required if text != targets_text for part in ("--file", text)])
    exit_code, text = verify(file_lock, write_sweep("file", file_lock))
    check("T88 B4b --file 잠금에 targets 누락 → 판정 종료 2 '필수 파일 누락'", lock_code == 0 and exit_code == 2 and "필수 파일 누락" in text and targets_text in text, text[-300:])

    v1_lock = output_dir / "v1-lock.json"
    v1_lock.write_text(json.dumps({"tool": "prereg_lock", "created": "2026-10-25T21:35:48", "note": "v1", "files": {text: prereg_lock.file_sha256(root / text) for text in (plan_text, regions_text, targets_text, live2_text)}}), encoding="utf-8")
    staged_v1 = verify(v1_lock, write_sweep("v1-staged", v1_lock))
    check("T89 B4b v1 잠금 + stage 있는 스윕 → 종료 2", staged_v1[0] == 2 and "v1 잠금은 stage 없는" in staged_v1[1], staged_v1[1][-300:])
    old_v1 = verify(v1_lock, write_sweep("v1-old", v1_lock, stage=MISSING, lock_sha256=MISSING, started_at=MISSING))
    check("T66 B4b v1 옛 잠금: verify 일치 0·stage 없는 옛 스윕 판정 통과 0(하위 호환)", verify(v1_lock)[0] == 0 and old_v1[0] == 0, old_v1[1][-300:])

    wrong_sha = verify(plan_lock, write_sweep("wrong-sha", plan_lock, lock_sha256="0" * 64))
    no_lock = verify(plan_lock, write_sweep("no-lock", plan_lock, lock_sha256=MISSING))
    early = verify(plan_lock, write_sweep("early", plan_lock, started_at=(datetime.fromisoformat(lock_document.get("created_at") or "2026-10-30T00:00:00+09:00") - timedelta(hours=1)).isoformat()))
    check("T90 B4b 스윕 lock_sha256 ≠ 잠금 / 단계 스윕에 lock_sha256 없음(--lock 없이 실행) / 잠금 created_at > 스윕 started_at → 각각 종료 2",
          (wrong_sha[0], no_lock[0], early[0]) == (2, 2, 2) and "lock_sha256" in wrong_sha[1] and "--lock 없이" in no_lock[1] and "started_at" in early[1], (wrong_sha[1][-160:], no_lock[1][-160:], early[1][-160:]))

    moved = verify(plan_lock, write_sweep("moved", plan_lock, scenes=[{"name": "fire", "regions_template": regions_text, "targets": "Tools/FluidFX/calib/targets/other.json"}]))
    check("B4b 스윕 장면 targets 경로 ≠ 계획 → 종료 2", moved[0] == 2 and "≠ 계획" in moved[1], moved[1][-300:])

    live2_bytes = (root / live2_text).read_bytes()
    (root / live2_text).write_bytes(live2_bytes + b" ")
    changed_live2 = verify(plan_lock, write_sweep("live2", plan_lock))
    (root / live2_text).write_bytes(live2_bytes)
    check("T91 B4b 목표가 가리키는 live2_targets 1바이트 변경 → 판정 종료 2", changed_live2[0] == 2 and f"sha256 불일치: {live2_text}" in changed_live2[1], changed_live2[1][-300:])

    sweep_tool = root / "Tools" / "FluidFX" / "calib_sweep.py"
    sweep_bytes = sweep_tool.read_bytes()
    sweep_tool.write_bytes(sweep_bytes + b"\n")
    changed_tool = verify(plan_lock)
    sweep_tool.write_bytes(sweep_bytes)
    check("T65 B4b 잠금 v2 도구 calib_sweep.py 1바이트 변경 → verify 종료 2", changed_tool[0] == 2 and "sha256 불일치: Tools/FluidFX/calib_sweep.py" in changed_tool[1], changed_tool[1][-300:])

    missing_plan = put("Tools/FluidFX/calib/plans/b4b_missing.json", {"id": "b4b_missing", "scenes": [{"name": "reed", "offline": {"command": ["{python}", "Tools/FluidFX/absent_tool.py", "{output}"]}}]})
    lock_code, missing_lock = lock_files("missing", "--plan", missing_plan)
    check("B4b offline 명령 스크립트가 없는 계획 잠금 → 종료 1·잠금 안 씀", lock_code == 1 and not missing_lock.exists(), lock_code)

    rejudged_runs = sorted(path.parent for path in (TEST_DIR / "rejudge").glob("*/lock.json"))
    compatibility = {}
    for run_dir in rejudged_runs:
        try:
            compatibility[run_dir.name] = prereg_lock.check_sweep_lock(load_json(run_dir / "sweep.json"), run_dir / "lock.json").get("version")
        except prereg_lock.LockError as error:
            compatibility[run_dir.name] = str(error)
    if not rejudged_runs:
        print("SKIP 재판정 산출 없음(닫힌 세계 하위 호환 시험)")
    check("B4b 재판정(Q3) 스윕은 --plan 잠금 v2 로 닫힌 세계 판정 통과", all(value == 2 for value in compatibility.values()), compatibility)


def check_cal0_derived_chain():
    def close(value, expected, tolerance):
        return isinstance(value, (int, float)) and abs(value - expected) <= tolerance

    def series_items(times, columns):
        steps = [{"step": index, "name": f"s{index:02d}_v00", "variant": "v00", "valid": True, "shot_game_times": list(times)} for index in range(3)]
        items = []
        for step in steps:
            metrics = {f"{shot}.{name}": values[shot] for name, values in columns.items() for shot in range(len(times))}
            metrics.update({name: sorted(values)[len(values) // 2] for name, values in columns.items()})
            items.append({"step": step["step"], "variant": "v00", "metrics": metrics, "samples": {name: list(values) for name, values in columns.items()}, "rule_score": 0, "bands": {}, "shot_count": len(times)})
        return items, steps

    def plan_error(specs):
        items, steps = series_items([0.0, 1.0, 2.0], {"x.m": [3.0, 2.0, 1.0], "y.m": [1.0, 1.0, 1.0]})
        try:
            calib_judge.add_derived_metrics(items, steps, specs, "s", True)
        except calib_judge.JudgeError as error:
            return str(error)
        return ""

    press_times = [20.0 + shot for shot in range(9)]
    press_items, press_steps = series_items(press_times, {"pressed_side.luminance_mean": [0.70 + 0.06 * 2 ** (-(time - 20.0) / 4.0) for time in press_times],
                                                          "outer_ring.luminance_mean": [0.70] * 9})
    press_specs = [{"name": "pressed_luma_ratio", "op": "ratio", "a": "pressed_side.luminance_mean", "b": "outer_ring.luminance_mean"},
                   {"name": "press_contrast", "op": "difference", "a": "pressed_side.luminance_mean", "b": "outer_ring.luminance_mean"},
                   {"name": "press_half_life_seconds", "op": "half_life", "a": "derived.press_contrast"},
                   {"name": "press_absolute_half_life", "op": "half_life", "a": "pressed_side.luminance_mean"},
                   {"name": "press_contrast_over_ratio", "op": "ratio", "a": "derived.press_contrast", "b": "derived.pressed_luma_ratio"}]
    calib_judge.add_derived_metrics(press_items, press_steps, press_specs, "k2_press_decay", True)
    press = press_items[0]["metrics"]
    check("CAL-0 파생 연쇄: difference(눌림 − 바깥) 장별 계열 → half_life(derived.press_contrast) 합성 반감기 4초 ±1%·r2>0.999, 절대 휘도 half_life 는 띠 [3,8] 밖(합성 계열 약 92 s)",
          close(press.get("derived.press_half_life_seconds"), 4.0, 0.04) and (press.get("derived.press_half_life_seconds.r2") or 0) > 0.999 and (press.get("derived.press_absolute_half_life") or 0) > 30.0
          and close(press.get("8.derived.press_contrast"), 0.06 * 2 ** -2, 1e-12) and len(press_items[2]["samples"].get("derived.press_contrast", [])) == 9,
          {key: press.get(key) for key in ("derived.press_half_life_seconds", "derived.press_half_life_seconds.r2", "derived.press_absolute_half_life", "8.derived.press_contrast")})
    check("CAL-0 파생 연쇄: 쌍 연산이 앞 파생 둘을 피연산으로(대비 ÷ 비, 장별·중앙값)", close(press.get("derived.press_contrast_over_ratio"), press.get("derived.press_contrast", 0) / press.get("derived.pressed_luma_ratio", 1), 1e-12)
          and close(press.get("0.derived.press_contrast_over_ratio"), 0.06 / (0.76 / 0.70), 1e-12), {key: value for key, value in press.items() if "over_ratio" in key})

    replicate_items, replicate_steps = series_items(press_times[:5], {"pressed_side.luminance_mean": [0.72] * 5, "outer_ring.luminance_mean": [0.70] * 5})
    calib_judge.add_derived_metrics(replicate_items, replicate_steps, press_specs, "k2_press", False)
    replicate = replicate_items[0]["metrics"]
    check("CAL-0 파생 연쇄: replicate 장면은 대비(쌍 연산)만 내고 시계열 연산은 건너뜀(연쇄된 쌍 연산은 계속)", close(replicate.get("derived.press_contrast"), 0.02, 1e-12)
          and "derived.press_half_life_seconds" not in replicate and "derived.press_contrast_over_ratio" in replicate, sorted(key for key in replicate if key.startswith("derived.")))

    groove_times = [15.0 * shot for shot in range(5)]
    groove_items, groove_steps = series_items(groove_times, {"snow_surface.luminance_mean": [0.84] * 5, "groove_decay.luminance_mean": [0.84 - 0.2 * 2 ** (-time / 90.0) for time in groove_times]})
    groove_specs = [{"name": "groove_contrast", "op": "difference", "a": "snow_surface.luminance_mean", "b": "groove_decay.luminance_mean"},
                    {"name": "groove_half_life_seconds", "op": "half_life", "a": "derived.groove_contrast"},
                    {"name": "groove_absolute_half_life", "op": "half_life", "a": "groove_decay.luminance_mean"},
                    {"name": "groove_reversed", "op": "difference", "a": "groove_decay.luminance_mean", "b": "snow_surface.luminance_mean"},
                    {"name": "groove_reversed_half_life", "op": "half_life", "a": "derived.groove_reversed"},
                    {"name": "groove_magnitude_half_life", "op": "half_life", "a": "derived.groove_reversed", "absolute": True},
                    {"name": "groove_magnitude_e_fold", "op": "e_fold", "a": "derived.groove_reversed", "absolute": True}]
    calib_judge.add_derived_metrics(groove_items, groove_steps, groove_specs, "groove_decay", True)
    groove = groove_items[1]["metrics"]
    check("CAL-0 대비 반감기: 홈이 밝아지며 복원(표면 − 홈 대비 반감기 90 s) → 90 s ±1%, 절대 홈 휘도 half_life 는 None(증가 계열)",
          close(groove.get("derived.groove_half_life_seconds"), 90.0, 0.9) and groove.get("derived.groove_absolute_half_life") is None,
          {key: groove.get(key) for key in ("derived.groove_half_life_seconds", "derived.groove_absolute_half_life")})
    check("CAL-0 대비 반감기: 거꾸로 뺀 대비(홈 − 표면, 음수) → None, absolute 참이면 방향 무관 90 s ±1%·e_fold 90/ln2",
          groove.get("derived.groove_reversed_half_life") is None and close(groove.get("derived.groove_magnitude_half_life"), 90.0, 0.9) and close(groove.get("derived.groove_magnitude_e_fold"), 90.0 / math.log(2.0), 1.3),
          {key: groove.get(key) for key in ("derived.groove_reversed_half_life", "derived.groove_magnitude_half_life", "derived.groove_magnitude_e_fold")})

    self_message = plan_error([{"name": "loop", "op": "difference", "a": "derived.loop", "b": "y.m"}])
    forward_message = plan_error([{"name": "first", "op": "half_life", "a": "derived.second"}, {"name": "second", "op": "difference", "a": "x.m", "b": "y.m"}])
    cycle_message = plan_error([{"name": "first", "op": "ratio", "a": "derived.second", "b": "y.m"}, {"name": "second", "op": "ratio", "a": "derived.first", "b": "y.m"}])
    undefined_message = plan_error([{"name": "contrast", "op": "difference", "a": "x.m", "b": "y.m"}, {"name": "decay", "op": "half_life", "a": "derived.contrats"}])
    duplicate_message = plan_error([{"name": "contrast", "op": "difference", "a": "x.m", "b": "y.m"}, {"name": "contrast", "op": "ratio", "a": "x.m", "b": "y.m"}])
    absolute_message = plan_error([{"name": "contrast", "op": "difference", "a": "x.m", "b": "y.m", "absolute": True}])
    check("CAL-0 파생 연쇄 거부(계획 오류): 자기 참조·뒤 파생 참조·두 식 순환 → '순환 또는 순서 오류', 미정의 이름 → '정의되지 않은'+가까운 이름, 이름 중복·absolute 를 쌍 연산에",
          all("순환 또는 순서 오류" in message for message in (self_message, forward_message, cycle_message)) and "정의되지 않은" in undefined_message and "derived.contrats" in undefined_message
          and "contrast" in undefined_message.split("가까운 이름")[-1] and "겹친다" in duplicate_message and "absolute" in absolute_message,
          (self_message, forward_message, cycle_message, undefined_message, duplicate_message, absolute_message))

    scoped_items, scoped_steps = series_items([0.0, 1.0, 2.0], {"x.m": [3.0, 2.0, 1.0], "y.m": [1.0, 1.0, 1.0]})
    calib_judge.add_derived_metrics(scoped_items, scoped_steps, [{"name": "other_contrast", "op": "difference", "a": "x.m", "b": "y.m", "scene": "other"},
                                                                 {"name": "other_ratio", "op": "ratio", "a": "derived.other_contrast", "b": "y.m"},
                                                                 {"name": "missing_ratio", "op": "ratio", "a": "z.m", "b": "y.m"}], "s", True)
    check("CAL-0 파생 연쇄: 앞 파생이 다른 장면 전용이면 이 장면에서 뒤 파생도 건너뜀(오류 아님), 측정에 없는 피연산 건너뜀은 그대로",
          not any(key.startswith("derived.") or ".derived." in key for key in scoped_items[0]["metrics"]), sorted(scoped_items[0]["metrics"]))


def check_cal0_viscous_measure():
    output_dir = TEST_DIR / "cal0-viscous"
    output_dir.mkdir(parents=True, exist_ok=True)
    measure_script = W / "Tools" / "FluidFX" / "measure_fx_look.py"

    def write_gray(name, value_at):
        image = Image.new("L", (256, 144))
        image.putdata([max(0, min(255, value_at(x, y))) for y in range(144) for x in range(256)])
        image.convert("RGB").save(output_dir / name)
        return (output_dir / name).as_posix()

    def measure(name, images):
        regions_path = output_dir / f"{name}_regions.json"
        regions_path.write_text(json.dumps({"images": images}), encoding="utf-8")
        output_path = output_dir / f"{name}.json"
        output_path.unlink(missing_ok=True)
        exit_code, text = python(str(measure_script), "--regions", str(regions_path), "--output", str(output_path))
        document = json.loads(output_path.read_text(encoding="utf-8")) if exit_code == 0 and output_path.is_file() else {"images": []}
        return exit_code, {(image["id"], region["name"]): region["metrics"] for image in document["images"] for region in image["regions"]}, text

    swirl = write_gray("swirl.png", lambda x, y: int(round(128 + 70 * math.sin(2 * math.pi * x / 20.0 + 0.3 * math.sin(2 * math.pi * y / 48.0)))))
    ramp = write_gray("edge.png", lambda x, y: 40 + 8 * min(max(x - 120, 0), 20))
    flat = write_gray("flat.png", lambda x, y: 128)
    exit_code, measured, text = measure("viscous", [
        {"path": swirl, "id": "swirl", "regions": [{"name": "mud_surface", "kind": "water", "box": [40, 20, 200, 120], "analyses": ["swirl"], "cm_per_px": 0.75},
                                                   {"name": "mud_band", "kind": "water", "box": [40, 20, 200, 120], "analyses": ["swirl"], "band_px": [8, 64]},
                                                   {"name": "plain", "kind": "water", "box": [40, 20, 200, 120]}]},
        {"path": ramp, "id": "edge", "regions": [{"name": "decal_edge", "kind": "water", "box": [80, 40, 180, 104], "analyses": ["edge_profile"], "line": [[90, 72], [170, 72]], "cm_per_px": 0.5, "cell_cm": 5},
                                                 {"name": "decal_back", "kind": "water", "box": [80, 40, 180, 104], "analyses": ["edge_profile"], "line": [[170, 60], [90, 60]], "cm_per_px": 0.5}]},
        {"path": flat, "id": "flat", "regions": [{"name": "decal_edge", "kind": "water", "box": [80, 40, 180, 104], "analyses": ["edge_profile", "swirl"], "line": [[90, 72], [170, 72]], "cm_per_px": 0.5, "cell_cm": 5}]}])
    mud, band = measured.get(("swirl", "mud_surface"), {}).get("swirl", {}), measured.get(("swirl", "mud_band"), {}).get("swirl", {})
    check("CAL-0 점성 swirl: 가로 주기 20px 소용돌이 무늬(세로로 흔들림) → spacing_px 20±0.5·spacing_cm(cm_per_px 0.75) 15±0.4, cm_per_px 없으면 cm null·band_px 지정도 같은 값",
          exit_code == 0 and abs((mud.get("spacing_px") or 0) - 20) <= 0.5 and abs((mud.get("spacing_cm") or 0) - 15) <= 0.4 and band.get("spacing_cm") is None and abs((band.get("spacing_px") or 0) - 20) <= 0.5,
          (exit_code, mud, band, text[-300:]))
    edge, back = measured.get(("edge", "decal_edge"), {}).get("edge_profile", {}), measured.get(("edge", "decal_back"), {}).get("edge_profile", {})
    check("CAL-0 점성 edge_profile: 20px 선형 경사 가장자리 → 10~90% 폭 16±1 px·8±0.5 cm·1.6±0.1 칸(cell_cm 5), 반대 방향 선도 같은 폭·cell_cm 없으면 칸 null",
          abs((edge.get("width_px") or 0) - 16) <= 1 and abs((edge.get("width_cm") or 0) - 8) <= 0.5 and abs((edge.get("width_cells") or 0) - 1.6) <= 0.1
          and abs((back.get("width_px") or 0) - 16) <= 1 and back.get("width_cells") is None, (edge, back))
    flat_metrics = measured.get(("flat", "decal_edge"), {})
    check("CAL-0 점성 평평한 영역: edge_profile 세 값 null·swirl 두 값 null, 분석 없는 같은 상자 영역에는 swirl·edge_profile 키 없음",
          flat_metrics.get("edge_profile") == {"width_px": None, "width_cm": None, "width_cells": None} and flat_metrics.get("swirl") == {"spacing_px": None, "spacing_cm": None}
          and "swirl" not in measured.get(("swirl", "plain"), {"swirl": 1}) and "edge_profile" not in measured.get(("swirl", "plain"), {"edge_profile": 1}), (flat_metrics, sorted(measured.get(("swirl", "plain"), {}))))
    no_line = measure("no-line", [{"path": ramp, "id": "a", "regions": [{"name": "decal_edge", "kind": "water", "box": [80, 40, 180, 104], "analyses": ["edge_profile"]}]}])
    bad_cell = measure("bad-cell", [{"path": ramp, "id": "a", "regions": [{"name": "decal_edge", "kind": "water", "box": [80, 40, 180, 104], "analyses": ["edge_profile"], "line": [[90, 72], [170, 72]], "cell_cm": 0}]}])
    check("CAL-0 점성 입력 거부: edge_profile 에 line 없음 → 종료 2 'edge_profile 은 line', cell_cm 0 → 종료 2 'cell_cm 는 양수'",
          no_line[0] == 2 and "edge_profile 은 line" in no_line[2] and bad_cell[0] == 2 and "cell_cm 는 양수" in bad_cell[2], (no_line[2][-200:], bad_cell[2][-200:]))


BATCH_TESTS = ("TDGame.FluidFX.Design.A", "TDGame.FluidFX.Numeric.B", "TDGame.FluidFX.Other.C")


def command_option(command, name):
    return command[command.index(name) + 1] if name in command else ""


class FakeBatchTools:
    def __init__(self, overlay_path):
        self.overlay_path = overlay_path
        self.calls = []
        self.sweep_outcomes = {}
        self.verify_codes = {}
        self.headless_failures = []
        self.headless_events = []
        self.headless_commands = []
        self.accept_paths = {}
        self.interrupt_at = None

    def describe(self, command):
        if command[0].endswith("UnrealEditor-Cmd.exe"):
            return "headless"
        script = Path(command[1]).stem
        if script == "calib_sweep":
            return f"sweep {Path(command_option(command, '--plan')).stem} {command_option(command, '--stage')}"
        if script == "calib_judge":
            output = Path(command_option(command, "--output"))
            return f"judge {output.parent.parent.name} {output.stem}"
        if script == "prereg_lock":
            return f"lock {Path(command_option(command, '--plan')).stem}"
        if script == "fluid_tuning_patch":
            return {"accept": f"accept {Path(command_option(command, '--judge')).parent.parent.name}",
                    "overlay-remove": f"remove {command_option(command, '--json-path')}"}.get(command[2], command[2])
        if script == "ue_editor":
            return command[2]
        if script == "run_in_editor":
            return "fps" if "OverrideFPS" in command[-1] else "reload"
        if script == "build_and_test":
            return "release " + command_option(command, "--release-hold") if "--release-hold" in command else "acquire " + command_option(command, "--acquire-hold")
        return script

    def __call__(self, command, log_path, timeout):
        label = self.describe(command)
        self.calls.append(label)
        if label == self.interrupt_at:
            self.interrupt_at = None
            raise KeyboardInterrupt
        return self.respond(label, command, timeout)

    def respond(self, label, command, timeout):
        words = label.split()
        if words[0] == "sweep":
            code, seconds = (self.sweep_outcomes.get((words[1], words[2])) or [(0, 10.0)]).pop(0)
            CLOCK.real_seconds += min(seconds, timeout)
            if seconds > timeout:
                return None, "멈춤"
            run_dir = Path(command_option(command, "--run-dir"))
            (run_dir / "s1").mkdir(parents=True, exist_ok=True)
            (run_dir / "s1" / "b8_schedule.json").write_text(json.dumps({"real_dt_mean": 0.03}), encoding="utf-8")
            if words[2] == "confirm" and load_json(command_option(command, "--screen-result")).get("overlay_sha256") != fluid_tuning_patch.file_sha256(self.overlay_path):
                return 2, "screen.json overlay_sha256 불일치"
            return code, f"{label} 종료 {code}"
        if words[0] == "judge":
            output = Path(command_option(command, "--output"))
            documents = {"screen": {"version": 2, "insensitive": False, "best": {"variant": "v01", "gain": 3.0}, "overlay_sha256": fluid_tuning_patch.file_sha256(self.overlay_path)},
                         "judge": {"version": 2, "adopt": {"decision": True, "variant": "v01", "reasons": []}}}
            if words[2] in documents:
                output.write_text(json.dumps(documents[words[2]]), encoding="utf-8")
                return 0, ""
            outcome = (self.verify_codes.get(words[1]) or [0]).pop(0)
            output.write_text(json.dumps({"scenes": [{"control_ok": outcome != "drift"}], "verify": {"passed": outcome != 1}}), encoding="utf-8")
            return (1 if outcome == "drift" else outcome), "재검증"
        if words[0] in ("accept", "remove"):
            overlay = load_json(self.overlay_path)
            json_path = self.accept_paths.get(words[1], f"Groups.B8.TDFluidTest.{words[1]}") if words[0] == "accept" else words[1]
            if words[0] == "accept":
                overlay["assignments"].append({"json_path": json_path, "value": 2.0, "kind": "knob", "from": {"plan": words[1]}})
            else:
                overlay["assignments"] = [entry for entry in overlay["assignments"] if entry["json_path"] != json_path]
            overlay["history"].append({"op": "add" if words[0] == "accept" else "remove", "json_path": json_path, "from": {"plan": json_path.rsplit(".", 1)[-1]}})
            self.overlay_path.write_text(json.dumps(overlay), encoding="utf-8")
            return 0, ""
        if label == "materialize":
            output = Path(command_option(command, "--out"))
            output.mkdir(parents=True, exist_ok=True)
            for name in ("TDFluidPresetTuning.json", "TDFluidMaterialLook.json"):
                (output / name).write_text("{}", encoding="utf-8")
            return 0, ""
        if label == "headless":
            self.headless_commands.append(command)
            failures = self.headless_failures.pop(0) if self.headless_failures else []
            events = self.headless_events.pop(0) if self.headless_events else {}
            tests = list(BATCH_TESTS) + [test for test in failures if test not in BATCH_TESTS]
            log_path = Path(next(argument for argument in command if argument.startswith("-abslog="))[len("-abslog="):])
            tuning = next(argument for argument in command if argument.startswith("-TDFluidTuningFile="))
            lines = [f"LogTemp: Display: Preset tuning file {tuning}", f"LogAutomationController: Found {len(tests)} automation tests"]
            for test in tests:
                lines.append(f"Test Completed. Result={{{'Fail' if test in failures else 'Success'}}} Name={{{test}}} Path={{{test}}}")
                if test in events:
                    stamp = "[2026.11.01-02.15.17:872][497]LogAutomationController: "
                    lines += [f"{stamp}BeginEvents: {test}"] + [f"{stamp}{event}" for event in events[test]] + [f"{stamp}EndEvents: {test}"]
            log_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
            return 0, ""
        return 0, ""


def check_cal0_batch():
    import calib_batch
    root = TEST_DIR / "cal0-b8"
    shutil.rmtree(root, ignore_errors=True)
    (root / "plans").mkdir(parents=True)
    classes_path = root / "test_classes.json"
    classes_path.write_text(json.dumps({"numeric_pinned": ["TDGame.FluidFX.Numeric.B"], "timing_record_only": ["TDGame.FluidFX.Other.C"]}), encoding="utf-8")

    def batch_plan(name, regions=None):
        scene = {"name": "s1", "level": "/Game/CAL0/LV_B8Stage", "camera": {}, "settle": 1.0, "shots": {"count": 3, "interval": 0.25}}
        if regions:
            scene["regions_template"] = regions.relative_to(W).as_posix()
        path = root / "plans" / f"{name}.json"
        path.write_text(json.dumps({"id": name, "scenes": [scene], "params": [{"json_path": f"Groups.B8.TDFluidTest.{name}", "values": [1.0, 2.0]}]}), encoding="utf-8")
        return path.relative_to(W).as_posix()

    def batch_arguments(name, blocks):
        queue_path = root / f"{name}-queue.json"
        test_classes = {"path": classes_path.relative_to(W).as_posix(), "sha256": calib_batch.file_sha256(classes_path)}
        queue_path.write_text(json.dumps({"test_classes": test_classes, "blocks": blocks}), encoding="utf-8")
        return ["--queue", str(queue_path), "--holder", "b8-test", "--journal", str(root / name / "journal.json"),
                "--overlay", str(root / name / "overlay.json"), "--locks-dir", str(root / name / "locks")]

    def run_batch(arguments):
        sys.argv = ["calib_batch.py"] + arguments
        with contextlib.redirect_stdout(io.StringIO()):
            return calib_batch.main()

    def entry(name, chain=None, regions=None, **extra):
        return dict({"plan": batch_plan(name, regions), "chain": chain, "stages": ["screen", "confirm"]}, **extra)

    def statuses(journal):
        return {Path(item["plan"]).stem: item for item in journal["items"]}

    regions_path = root / "t98" / "regions_alpha.json"
    arguments = batch_arguments("t98", [{"id": "K1", "plans": [entry("b8_alpha", regions=regions_path), entry("b8_beta"), entry("b8_gamma"),
                                                                entry("b8_delta", estimate_seconds={"screen": 4000, "confirm": 10})]}])
    journal_path = root / "t98" / "journal.json"
    fake = FakeBatchTools(root / "t98" / "overlay.json")
    fake.interrupt_at = "sweep b8_beta screen"
    with patched(calib_batch, run_tool=fake, HOLD_FILE=root / "hold.json"):
        try:
            run_batch(arguments)
            interrupted = False
        except KeyboardInterrupt:
            interrupted = True
        first, first_calls = statuses(load_json(journal_path)), list(fake.calls)
        start_lines = [line.split(" | ")[0].split(" ", 1)[1] for line in (journal_path.parent / "batch.log").read_text(encoding="utf-8").splitlines()[:4]]
        killed = load_json(journal_path)
        released_on_interrupt = bool(killed["holds"][-1].get("released_at"))
        for key in ("released_at", "released_by"):
            killed["holds"][-1].pop(key, None)
        journal_path.write_text(json.dumps(killed), encoding="utf-8")
        regions_path.write_text("{}", encoding="utf-8")
        fake.calls.clear()
        fake.sweep_outcomes = {("b8_beta", "screen"): [(1, 5.0), (0, 10.0)], ("b8_gamma", "screen"): [(0, 1.0e6)], ("b8_delta", "screen"): [(0, 7990.0)]}
        resumed_code = run_batch(arguments + ["--resume"])
        refused_code = run_batch(arguments)
    second, calls = statuses(load_json(journal_path)), fake.calls
    check("T98 시작 처리 4개 기록: 보류 회수 → ensure → t.OverrideFPS 0 → 정본 재적재(→ 보류 획득) 순서·batch.log",
          first_calls[:4] == ["ensure", "fps", "reload", "acquire b8-test"] and start_lines == ["보류 회수", "ensure", "t.OverrideFPS 0", "정본 재적재"],
          (first_calls[:4], start_lines))
    check("T98 k단계 뒤 중단 → 일지 상태열(영역 틀 없음 needs_regions·locked) → --resume 이 남은 보류 회수부터 이어 함(잠금·frame 중복 없음)",
          interrupted and released_on_interrupt and first["b8_alpha"]["status"] == "needs_regions" and "영역 틀 없음" in first["b8_alpha"]["reason"]
          and first["b8_beta"]["status"] == "locked" and calls[:2] == ["release b8-test", "ensure"] and "lock b8_beta" not in calls and "sweep b8_alpha frame" not in calls
          and calls.count("lock b8_alpha") == 1 and second["b8_alpha"]["status"] == second["b8_beta"]["status"] == "adopted" and resumed_code == 1 and refused_code == 2,
          (first_calls, calls, {name: item["status"] for name, item in second.items()}, resumed_code, refused_code))
    beta_screen = calls.index("sweep b8_beta screen")
    check("T98 장면 실패 → ensure → 1회 재시도",
          calls[beta_screen:beta_screen + 3] == ["sweep b8_beta screen", "ensure", "sweep b8_beta screen"] and calls.count("sweep b8_beta screen") == 2, calls[beta_screen:beta_screen + 4])
    gamma_screen = calls.index("sweep b8_gamma screen")
    check("T98 단계 2배 초과 → 장면 중단(재시도 없음)·failed·꼬리 600자 이하",
          second["b8_gamma"]["status"] == "failed" and "2배" in second["b8_gamma"]["reason"] and calls.count("sweep b8_gamma screen") == 1
          and calls[gamma_screen + 1] != "ensure" and len(second["b8_gamma"]["reason"]) < 700, second["b8_gamma"])
    check("T98 블록 누적 > 1.25 × 추정 → 남은 confirm deferred·real_dt 블록 첫 단계로 갱신·블록 헤드리스 기록",
          second["b8_delta"]["status"] == "deferred" and "sweep b8_delta confirm" not in calls and load_json(journal_path)["real_dt"] == {"LV_B8Stage": 0.03}
          and "headless" in second["b8_delta"]["runs"] and load_json(journal_path)["holds"][-1].get("released_at"), (second["b8_delta"], load_json(journal_path)["real_dt"]))

    chain = "Groups.TrackPainter"
    arguments = batch_arguments("t99", [{"id": "K7", "plans": [entry("b8_c1", chain), entry("b8_c2", chain), entry("b8_c3", chain)]}, {"id": "K8", "plans": [entry("b8_d1")]}])
    fake = FakeBatchTools(root / "t99" / "overlay.json")
    fake.verify_codes = {"b8_c1": [1, 0], "b8_c2": [0]}
    fake.headless_failures = [["TDGame.FluidFX.Design.A"], [], ["TDGame.FluidFX.Numeric.B", "TDGame.FluidFX.Other.C"]]
    with patched(calib_batch, run_tool=fake, HOLD_FILE=root / "hold.json"):
        chain_code = run_batch(arguments)
    journal = load_json(root / "t99" / "journal.json")
    items, calls = statuses(journal), fake.calls
    after_c3 = calls[calls.index("accept b8_c3") + 1:]
    chain_calls = after_c3[:after_c3.index("materialize")]
    verify_c1, verify_c2 = ["sweep b8_c1 verify", "judge b8_c1 verify"], ["sweep b8_c2 verify", "judge b8_c2 verify"]
    check("T99 사슬 끝 verify 실패 → 가장 최근 채택부터 overlay-remove → 남은 앞 채택 재검증 통과 시 멈춤(conflict 기록)",
          chain_calls == verify_c1 + verify_c2 + ["remove Groups.B8.TDFluidTest.b8_c3"] + verify_c1
          and items["b8_c3"]["status"] == "verify_failed" and "conflict" in items["b8_c3"]["reason"] and items["b8_c1"]["status"] == "adopted"
          and "verify2" in items["b8_c1"]["runs"] and load_json(root / "t99" / "chains" / "Groups_TrackPainter.json")["rounds"] == [{"b8_c1": False, "b8_c2": True}, {"b8_c1": True}],
          (chain_calls, items["b8_c3"]))
    block_calls = after_c3[len(chain_calls):after_c3.index("lock b8_d1")]
    tuning_arguments = [argument for argument in fake.headless_commands[0] if argument.startswith("-TDFluidTuningFile=")]
    check("T99 블록 헤드리스(-TDFluidTuningFile=materialize 사본): 설계 단언 실패 → 블록의 가장 최근 채택 되돌림 → 다시 시험 통과",
          block_calls == ["materialize", "headless", "remove Groups.B8.TDFluidTest.b8_c2", "materialize", "headless"]
          and items["b8_c2"]["status"] == "verify_failed" and "설계 단언" in items["b8_c2"]["reason"] and "headless" in items["b8_c3"]["runs"]
          and len(tuning_arguments) == 1 and Path(tuning_arguments[0].split("=", 1)[1]).parent.name == "copy" and Path(tuning_arguments[0].split("=", 1)[1]).is_file(),
          (block_calls, tuning_arguments))
    headless_record = load_json(W / items["b8_d1"]["runs"].get("headless", "missing") / "headless.json") if items["b8_d1"]["runs"].get("headless") else {}
    check("T99 블록 헤드리스 실패가 숫자 고정·시간 측정(timing_record_only) 목록뿐 → 기록만(되돌림 없음)·덧씌움 최종 c1·d1",
          calls[calls.index("accept b8_d1") + 1:] == ["materialize", "headless", "release b8-test"] and items["b8_d1"]["status"] == "adopted"
          and sorted(headless_record.get("allowlisted_failures") or []) == ["TDGame.FluidFX.Numeric.B", "TDGame.FluidFX.Other.C"] and headless_record.get("verdict_code") == 0
          and [assignment["json_path"] for assignment in load_json(root / "t99" / "overlay.json")["assignments"]] == ["Groups.B8.TDFluidTest.b8_c1", "Groups.B8.TDFluidTest.b8_d1"]
          and chain_code == 0, (calls[calls.index("accept b8_d1") + 1:], headless_record, chain_code))

    arguments = batch_arguments("fix-a4", [{"id": "K9", "plans": [entry("b8_e1", estimate_seconds={"screen": 1000, "confirm": 10})]},
                                           {"id": "K9b", "plans": [entry("b8_e2", estimate_seconds={"screen": 10, "confirm": 10})]}])
    fake = FakeBatchTools(root / "fix-a4" / "overlay.json")
    fake.sweep_outcomes = {("b8_e1", "screen"): [(0, 1990.0)]}
    with patched(calib_batch, run_tool=fake, HOLD_FILE=root / "hold.json"):
        run_batch(arguments)
        deferred = statuses(load_json(root / "fix-a4" / "journal.json"))
        fake.calls.clear()
        revisit_code = run_batch(arguments + ["--resume", "--revisit-deferred", "--block", "K9"])
    revisited, calls = statuses(load_json(root / "fix-a4" / "journal.json")), fake.calls
    check("CAL-0 반박 adopt4 deferred 재방문 때 덧씌움이 바뀌었으면 locked 로 되돌려 재screen → confirm(상한 없음) → 채택(failed 아님)",
          deferred["b8_e1"]["status"] == "deferred" and deferred["b8_e2"]["status"] == "adopted" and revisited["b8_e1"]["status"] == "adopted" and calls.count("sweep b8_e1 screen") == 1
          and "sweep b8_e1 confirm" in calls and calls.index("sweep b8_e1 screen") < calls.index("sweep b8_e1 confirm") and revisit_code == 0, (deferred["b8_e1"], revisited["b8_e1"], calls))

    arguments = batch_arguments("fix-a5", [{"id": "K11", "plans": [entry("b8_f1", "Groups.Drift"), entry("b8_f2", "Groups.Drift")]}])
    fake = FakeBatchTools(root / "fix-a5" / "overlay.json")
    fake.verify_codes = {"b8_f1": ["drift"]}
    with patched(calib_batch, run_tool=fake, HOLD_FILE=root / "hold.json"):
        run_batch(arguments)
    drifted, calls = statuses(load_json(root / "fix-a5" / "journal.json")), fake.calls
    check("CAL-0 반박 adopt5 사슬 재검증 판정 종료 1 이 대조 표류(control_ok 거짓)면 실패로 읽지 않음 → 되돌림 없음·사슬 중단 사유 기록",
          drifted["b8_f2"]["status"] == "adopted" and not any(call.startswith("remove") for call in calls) and "대조" in drifted["b8_f1"]["reason"], (calls, drifted["b8_f1"], drifted["b8_f2"]))

    with patched(calib_batch, run_tool=fake, HOLD_FILE=root / "hold.json"):
        fake.calls.clear()
        run_batch(arguments + ["--resume"])
    marker = statuses(load_json(root / "fix-a5" / "journal.json"))["b8_f2"]["runs"].get("chain_verify")
    chain_summary = load_json(W / marker) if marker and (W / marker).is_file() else {}
    check("CAL-0 재반박 N2 사슬 재검증이 판정 불가로 중단되면 chain_verify 표시(interrupted 계획·사유)를 남겨 --resume 때 verify 스윕을 반복하지 않음",
          bool(marker) and (chain_summary.get("interrupted") or {}).get("plan") == "b8_f1" and "sweep b8_f1 verify" not in fake.calls, (marker, chain_summary, fake.calls))

    header_test = "TDGame.FluidFX.PresetData.StructDefaultsMatchHeader"
    wind_path, emitter_path = "Globals.TDFluidCloudSceneSettings.WindCurtainSpeedCmPerSec", "StructDefaults.TDFluidSourceEmitterSettings.FlickerAmplitude"

    def header_error(json_path, default="150.0"):
        return (f"Error: Expected '{json_path} equals the header default {default} (a header change does not move presets; update the tuning file on purpose)' to be true. "
                "[C:\\Project\\TDGame\\Source\\TDGame\\FluidFX\\Tests\\TDFluidPresetTuningTests.cpp(669)]")

    def header_run(name, accept_paths, failures, events):
        arguments = batch_arguments(name, [{"id": "K12", "plans": [entry(plan) for plan in accept_paths]}])
        fake = FakeBatchTools(root / name / "overlay.json")
        fake.accept_paths, fake.headless_failures, fake.headless_events = accept_paths, [failures], [events]
        with patched(calib_batch, run_tool=fake, HOLD_FILE=root / "hold.json"):
            code = run_batch(arguments)
        last_plan = list(accept_paths)[-1]
        record_path = root / name / "blocks" / "K12-1" / "headless.json"
        return {"code": code, "items": statuses(load_json(root / name / "journal.json")), "after": fake.calls[fake.calls.index(f"accept {last_plan}") + 1:],
                "record": load_json(record_path) if record_path.is_file() else {}, "log": (root / name / "batch.log").read_text(encoding="utf-8"),
                "overlay": [assignment["json_path"] for assignment in load_json(root / name / "overlay.json")["assignments"]]}

    synced = header_run("k10-a1", {"b8_g1": emitter_path, "b8_g2": wind_path}, [header_test, "TDGame.FluidFX.Numeric.B"],
                        {header_test: [header_error(wind_path), header_error(emitter_path, "0.2")]})
    check("K10 A1 블록 헤드리스 설계 단언 실패가 StructDefaultsMatchHeader 하나·오류 줄 전부 'Globals|StructDefaults 경로 equals the header default' 꼴·경로 전부 덧씌움 json_path → 통과(되돌림 없음)·headless.json header_sync_pending·batch.log '헤더 기본값 동기 대기(W 단계)'",
          synced["after"] == ["materialize", "headless", "release b8-test"] and synced["items"]["b8_g2"]["status"] == synced["items"]["b8_g1"]["status"] == "adopted"
          and "headless" in synced["items"]["b8_g2"]["runs"] and synced["record"].get("header_sync_pending") == [wind_path, emitter_path]
          and synced["record"].get("outside_allowlist") == [header_test] and synced["record"].get("allowlisted_failures") == ["TDGame.FluidFX.Numeric.B"]
          and f"헤더 기본값 동기 대기(W 단계): {wind_path}, {emitter_path}" in synced["log"] and synced["overlay"] == [emitter_path, wind_path] and synced["code"] == 0,
          (synced["after"], synced["record"], synced["log"][-400:]))

    def reverted(run):
        return (run["after"][:3] == ["materialize", "headless", f"remove {wind_path}"] and run["items"]["b8_" + run["name"]]["status"] == "verify_failed"
                and "설계 단언" in run["items"]["b8_" + run["name"]]["reason"] and run["record"].get("header_sync_pending") == [] and run["overlay"] == [] and "헤더 기본값 동기 대기" not in run["log"])

    mixed = dict(header_run("k10-a2", {"b8_h1": wind_path}, [header_test, "TDGame.FluidFX.Design.A"], {header_test: [header_error(wind_path)]}), name="h1")
    check("K10 A2 StructDefaultsMatchHeader(헤더 기본값 꼴)와 다른 설계 단언 실패 Design.A 가 섞이면 기존대로 블록의 가장 최근 채택 되돌림·header_sync_pending []",
          reverted(mixed), (mixed["after"], mixed["record"], mixed["items"]["b8_h1"]["status"]))
    outside = dict(header_run("k10-a3", {"b8_i1": wind_path}, [header_test], {header_test: [header_error(wind_path), header_error("Globals.TDFluidCloudSceneSettings.WindCurtainGustScale", "1.0")]}), name="i1")
    check("K10 A3 헤더 기본값 꼴 줄의 경로 하나라도 덧씌움 assignments 밖(채택 안 한 Globals 필드) → 되돌림·header_sync_pending []",
          reverted(outside), (outside["after"], outside["record"], outside["items"]["b8_i1"]["status"]))
    array_error = "Error: Expected 'TDFluidCloudSceneSettings array fields default to empty arrays (WindCurtainSamples)' to be true. [C:\\Project\\TDGame\\Source\\TDGame\\FluidFX\\Tests\\TDFluidPresetTuningTests.cpp(657)]"
    other_form = dict(header_run("k10-a4", {"b8_j1": wind_path}, [header_test], {header_test: [header_error(wind_path), array_error]}), name="j1")
    no_lines = dict(header_run("k10-a4b", {"b8_j2": wind_path}, [header_test], {header_test: ["Warning: only a warning line"]}), name="j2")
    check("K10 A4 StructDefaultsMatchHeader 오류 줄에 다른 꼴(빈 배열 기본값 단언)이 섞임 / Error 줄이 없음(Warning 만) → 각각 되돌림·header_sync_pending []",
          reverted(other_form) and reverted(no_lines), (other_form["after"], other_form["record"], no_lines["after"], no_lines["record"]))

    p20_chain = "Groups.P20"
    arguments = batch_arguments("k10-c2", [{"id": "K13", "plans": [entry("b8_p1", p20_chain), entry("b8_p2", p20_chain), entry("b8_p3", p20_chain)]}])
    p20_overlay_path = root / "k10-c2" / "overlay.json"
    p20_overlay_path.parent.mkdir(parents=True, exist_ok=True)
    descendant_text = f"{(root / 'plans').relative_to(W).as_posix()}/b8_p1-p20.json"
    p20_overlay_path.write_text(json.dumps({"version": 2, "assignments": [{"json_path": "Groups.B8.TDFluidTest.b8_p1_round2", "value": 2.0, "kind": "knob", "from": {"plan": descendant_text}}],
                                            "actor_props": [], "history": []}), encoding="utf-8")
    fake = FakeBatchTools(p20_overlay_path)
    with patched(calib_batch, run_tool=fake, HOLD_FILE=root / "hold.json"):
        p20_code = run_batch(arguments)
    items, calls = statuses(load_json(root / "k10-c2" / "journal.json")), fake.calls
    after_p3 = calls[calls.index("accept b8_p3") + 1:] if "accept b8_p3" in calls else []
    p20_chain_calls = after_p3[:after_p3.index("materialize")] if "materialize" in after_p3 else after_p3
    p20_log = (root / "k10-c2" / "batch.log").read_text(encoding="utf-8")
    p20_summary_path = root / "k10-c2" / "chains" / "Groups_P20.json"
    p20_summary = load_json(p20_summary_path) if p20_summary_path.is_file() else {}
    p20_note = f"P-20 2라운드 채택이 있는 1라운드 계획은 재검증하지 않는다: {descendant_text}"
    check("K10 C2 사슬 재검증: 덧씌움에 같은 폴더 <stem>-p20.json 채택이 있는 앞 구성원(b8_p1)은 verify 스윕 없이 빼고 일지 reason·batch.log '사슬 재검증 | 제외' 한 줄에 사유, 나머지 앞 구성원(b8_p2)만 재검증·사슬 요약 rounds [{b8_p2: 통과}]",
          p20_chain_calls == ["sweep b8_p2 verify", "judge b8_p2 verify"] and "sweep b8_p1 verify" not in calls and items["b8_p1"]["status"] == "adopted"
          and items["b8_p1"]["reason"].count(p20_note) == 1 and sum(1 for line in p20_log.splitlines() if "b8_p1 사슬 재검증 | 제외 |" in line and p20_note in line) == 1
          and p20_summary.get("rounds") == [{"b8_p2": True}] and p20_code == 0,
          (p20_chain_calls, items["b8_p1"]["reason"][-300:], p20_summary, p20_log[-400:], p20_code))

    arguments = batch_arguments("r5-limit", [{"id": "K14", "plans": [entry("b8_q1", estimate_seconds={"screen": 10, "confirm": 10}),
                                                                      entry("b8_q2", estimate_seconds={"screen": 10, "confirm": 10})]}])
    fake = FakeBatchTools(root / "r5-limit" / "overlay.json")
    fake.sweep_outcomes = {("b8_q1", "screen"): [(0, 250.0)], ("b8_q2", "screen"): [(0, 270.0)]}
    with patched(calib_batch, run_tool=fake, HOLD_FILE=root / "hold.json"):
        run_batch(arguments)
    items = statuses(load_json(root / "r5-limit" / "journal.json"))
    check("R5 단계 한도 = 2 × max(추정, 장면 고정 비용 130초): 250초면 screen 이 끝까지 돌고(블록 누적 상한으로 confirm 만 deferred), 270초면 중단·failed·사유에 한도 260초",
          items["b8_q1"]["status"] == "deferred" and "블록 누적" in items["b8_q1"]["reason"] and items["b8_q2"]["status"] == "failed" and "한도 260초" in items["b8_q2"]["reason"] and "2배" in items["b8_q2"]["reason"],
          (items["b8_q1"]["status"], items["b8_q2"]["status"], items["b8_q2"]["reason"][:200]))
    measured_limits = [("w5_underwater_l18a", "screen", 225.0), ("fog_f2_passive", "verify", 211.0)]
    limit_rows = [(name, stage, measured, calib_batch.STEP_LIMIT_FACTOR * calib_batch.estimate_stage_seconds(load_json(W / f"Tools/FluidFX/calib/plans/{name}.json"), stage, {}))
                  for name, stage, measured in measured_limits]
    check("R5 실측 대조(L-build-19): 수중 screen 225초·안개 f2 verify 211초의 한도가 실측 1.5배 이상이고 3fps 스로틀 하한 모형(약 592·595초)보다 짧다",
          all(1.5 * measured <= limit < 590.0 for _, _, measured, limit in limit_rows), limit_rows)


class ValidatingBatchTools(FakeBatchTools):
    def respond(self, label, command, timeout):
        words = label.split()
        if words[0] == "sweep":
            try:
                calib_sweep.load_plan(W / command_option(command, "--plan"), words[2])
            except calib_sweep.PlanError as error:
                return 2, str(error)
        return super().respond(label, command, timeout)


def declared_regions(plan_text):
    return [scene["regions_template"] for scene in load_json(W / plan_text).get("scenes", []) if scene.get("regions_template")]


def is_draft_regions(regions_text):
    path = W / regions_text
    return path.is_file() and "초안" in str(load_json(path).get("note", ""))


def check_cal0_real_queue():
    import calib_batch
    root = TEST_DIR / "cal0-real-queue"
    shutil.rmtree(root, ignore_errors=True)
    calib_dir = W / "Tools" / "FluidFX" / "calib"
    queue_path = calib_dir / "batch_queue.json"
    queue = load_json(queue_path)
    entries = {Path(entry["plan"]).stem: entry for _, entry in calib_batch.queue_entries(queue)}
    blocked = {Path(entry["plan"]).stem: entry for entry in queue.get("blocked", [])}
    plan_names = sorted(path.stem for path in (calib_dir / "plans").glob("*.json"))
    placed = sorted(list(entries) + list(blocked) + [entry["plan"] for entry in queue.get("not_run", []) if entry["plan"] in plan_names])
    check("CAL-0 마무리 실제 큐: calib/plans 계획마다 실행 블록·blocked·not_run 중 한 곳에만, blocked 항목은 막힘 사유·돌아갈 블록이 있다",
          placed == plan_names and all(entry.get("blocked") and entry.get("block") for entry in blocked.values()), (sorted(set(plan_names) ^ set(placed)), list(blocked)))
    undeclared = [name for name, entry in {**entries, **blocked}.items() if entry.get("needs_frame") and not declared_regions(entry["plan"])]
    drafts = sorted(path.name for path in (calib_dir / "regions").glob("*.json") if is_draft_regions(path.relative_to(W).as_posix()))
    check("CAL-0 마무리 실제 큐: needs_frame 계획은 영역 틀 경로를 선언하고 calib/regions 에 초안 상자(note '초안')가 없다",
          not undeclared and not drafts, (undeclared, drafts))
    sys.argv = ["calib_batch.py", "--queue", str(queue_path), "--holder", "real-queue-test", "--journal", str(root / "journal.json"),
                "--overlay", str(root / "overlay.json"), "--locks-dir", str(root / "locks")]
    fake = ValidatingBatchTools(root / "overlay.json")
    with patched(calib_batch, run_tool=fake, HOLD_FILE=root / "hold.json"), contextlib.redirect_stdout(io.StringIO()):
        batch_code = calib_batch.main()
    items = {Path(item["plan"]).stem: item for item in load_json(root / "journal.json")["items"]}
    failed = {name: item["reason"][:160] for name, item in items.items() if item["status"] == "failed"}
    draft_locked = [name for name, item in items.items() if "lock" in item["runs"] and any(is_draft_regions(text) for text in declared_regions(item["plan"]))]
    unmarked_waiting = [name for name, item in items.items() if item["status"] == "needs_regions" and not entries[name].get("needs_frame")]
    check("CAL-0 마무리 실제 큐 가짜 B8(스윕은 calib_sweep.load_plan 단계 검사): failed 0·배치 종료 0·초안 상자 잠금 0·blocked 미실행·영역 틀 대기는 모두 needs_frame",
          batch_code == 0 and not failed and not draft_locked and not unmarked_waiting and not set(blocked) & set(items),
          (batch_code, failed, draft_locked, unmarked_waiting, {name: item["status"] for name, item in items.items()}))


class FakeWalkingPawn(FakePlayerPawn):
    def __init__(self, speed=600.0, is_blocked=False):
        super().__init__(speed)
        self.is_blocked = is_blocked
        CLOCK.register(self.consume_move_intent)

    def consume_move_intent(self, delta_seconds):
        intent, self.pending_intent = self.pending_intent, None
        if intent is None or self.is_blocked:
            return
        self.location = FakeVector(self.location.x + intent[0] * self.speed * delta_seconds, self.location.y + intent[1] * self.speed * delta_seconds, self.location.z)


def make_controller(yaw):
    return Props(get_control_rotation=lambda: Props(pitch=0.0, yaw=yaw, roll=0.0), stop_movement=lambda: None)


def make_exposure_volume(label, method, bias, applies_physical_camera=False):
    volume = FakePostProcessVolume(label)
    volume.settings = Props(override_auto_exposure_method=True, auto_exposure_method=FakeEnum(method), auto_exposure_bias=bias, auto_exposure_apply_physical_camera_exposure=applies_physical_camera,
                            depth_of_field_fstop=4.0, camera_shutter_speed=60.0, camera_iso=100.0, auto_exposure_min_brightness=-10.0, auto_exposure_max_brightness=20.0)
    volume.get_path_name = lambda: f"/Game/CAL0/Stage.Stage:PersistentLevel.{label}"
    return volume


def check_lookdev_frame_fixes():
    output_dir = TEST_DIR / "frame-fixes"
    output_dir.mkdir(parents=True, exist_ok=True)
    lookdev.OUTPUT_DIRECTORY = output_dir

    def players(pawn, controller):
        return patched(unreal.GameplayStatics, get_player_pawn=lambda world, index: pawn, get_player_controller=lambda world, index: controller)

    def run_spec(name, spec):
        spec_path = output_dir / f"{name}-spec.json"
        spec_path.write_text(json.dumps(spec), encoding="utf-8")
        exit_code = run_lookdev_in_process(spec_path, "/Game/CAL0/Stage", output_dir / name)
        return exit_code, load_json(output_dir / name / "result.json")

    def near(values, expected):
        return len(values) == len(expected) and all(abs(value - target) < 1e-6 for value, target in zip(values, expected))

    no_navigation = Props(project_point_to_navigation=lambda world, goal, data, query, extent: None)
    reset_world([])
    pawn = FakeWalkingPawn()
    spec = {"base": {"move": {"distance_cm": 300.0}}, "variants": [{"name": "s00_walk", "settle": 0.1, "apply": {"pawn": [0.0, 0.0], "move": {"distance_cm": 300.0, "yaw": 0.0}}}]}
    with patched(unreal, NavigationSystemV1=no_navigation), players(pawn, make_controller(90.0)):
        exit_code, result = run_spec("walk", spec)
    base_move, step_move = result["base"].get("move", {}), result["variants"][0]["apply"].get("move", {})
    check("F1 사전 move {distance_cm, yaw?}: 내비 없이 입력 걷기·base 는 조종 yaw 90·단계는 yaw 0·순간이동은 pawn 키 1회뿐·결과 mode·yaw·goal·moved_cm·timed_out·키 오류 0",
          exit_code == 0 and not result["key_errors"] and base_move.get("mode") == "input" and base_move.get("yaw") == 90.0 and near(base_move.get("goal", []), [0.0, 300.0])
          and base_move.get("moved_cm", 0.0) >= 300.0 and base_move.get("timed_out") is False and base_move.get("done") and "error" not in base_move
          and step_move.get("yaw") == 0.0 and near(step_move.get("goal", []), [300.0, 0.0]) and step_move.get("moved_cm", 0.0) >= 300.0 and step_move.get("done")
          and pawn.teleports == [(0.0, 0.0)] and pawn.location.x >= 300.0, (exit_code, result["key_errors"], base_move, step_move, pawn.teleports))

    reset_world([])
    blocked = FakeWalkingPawn(is_blocked=True)
    with players(blocked, make_controller(0.0)):
        exit_code, result = run_spec("walk-blocked", {"base": {}, "variants": [{"name": "s00_blocked", "settle": 0.1, "apply": {"move": {"distance_cm": 300.0}}}]})
    blocked_move = result["variants"][0]["apply"].get("move", {})
    check("F2 걷기 시한(2d/최대 속도 + 3 = 4 게임 초) 초과 → move.timed_out·error, 키 오류 '00: move: 시한 초과'·종료 1",
          exit_code == 1 and blocked_move.get("timed_out") is True and abs(blocked_move.get("timeout_seconds", 0.0) - 4.0) < 1e-9 and blocked_move.get("moved_cm") == 0.0
          and any(line.startswith("00: move: 시한 초과") for line in result["key_errors"]) and not blocked.teleports, (exit_code, blocked_move, result["key_errors"]))

    reset_world([])
    pawn = FakeWalkingPawn()
    state = {"uses_tuning_reload": False, "key_errors": []}
    with patched(unreal, NavigationSystemV1=no_navigation), players(pawn, make_controller(0.0)):
        listed = lookdev.apply_variant_with_tuning({"move": [500.0, 200.0]}, "s00", state)
        walk_errors = lookdev.find_walk_errors(listed, None)
    check("F3 목록 move [goal_x, goal_y]: 옛 절대 목표 동작(내비 없으면 순간이동) 그대로·걷기 상태 파일 없음·오류 0",
          listed.get("move") == {"goal": [500.0, 200.0], "mode": "teleport"} and pawn.teleports == [(500.0, 200.0)] and not walk_errors and not state["key_errors"]
          and lookdev.find_key_errors({"move": [500.0, 200.0]}) == [], (listed.get("move"), pawn.teleports, walk_errors, state))

    reset_world([])
    pawn = FakeWalkingPawn()
    state = {"uses_tuning_reload": False, "key_errors": []}
    events = [{"time": 0.2, "move": {"distance_cm": 120.0}}]
    with players(pawn, make_controller(0.0)):
        scheduled = lookdev.apply_variant_with_tuning({"schedule": {"times": [1.0], "reference": "now", "prefix": "walk", "events": events}}, "s01", state)
        status = lookdev.wait_for_schedule(Path(scheduled["schedule"]["status_path"]), 30.0)
        event_errors = lookdev.find_walk_errors(scheduled, status)
        pawn.is_blocked = True
        stuck = lookdev.apply_variant_with_tuning({"schedule": {"times": [1.0], "reference": "now", "prefix": "stuck", "events": events}}, "s02", state)
        stuck_status = lookdev.wait_for_schedule(Path(stuck["schedule"]["status_path"]), 30.0)
        stuck_errors = lookdev.find_walk_errors(stuck, stuck_status)
    event_move = (status.get("events_done") or [{}])[0].get("move", {})
    check("F4 schedule.events move 사전 형식: 예약과 다른 틱 콜백에서 걷고 끝 상태를 이벤트에 적음 / 막히면 'schedule.events t=0.2 move: 시한 초과'",
          event_move.get("mode") == "input" and event_move.get("done") and event_move.get("moved_cm", 0.0) >= 120.0 and not event_errors
          and any(line.startswith("schedule.events t=0.2 move: 시한 초과") for line in stuck_errors), (event_move, event_errors, stuck_errors))

    global EDITOR_SHARED_NAMESPACE
    reset_world([])
    pawn = FakeWalkingPawn()
    EDITOR_SHARED_NAMESPACE = {"__name__": "editor"}
    with players(pawn, make_controller(0.0)):
        shared = lookdev.apply_variant_with_tuning({"schedule": {"times": [1.0], "reference": "now", "prefix": "shared", "events": events}}, "s03", state)
        fake_editor_action("status")
        shared_status = lookdev.wait_for_schedule(Path(shared["schedule"]["status_path"]), 30.0)
        shared_errors = lookdev.find_walk_errors(shared, shared_status)
    EDITOR_SHARED_NAMESPACE = None
    shared_move = (shared_status.get("events_done") or [{}])[0].get("move", {})
    check("F4c 원격 호출이 전역 이름 공간을 공유(실제 에디터): 예약 시작 뒤 다른 호출이 PARAMS 를 덮어써도 schedule.events move 가 예약 상태의 output·tag 로 걷기 상태 파일을 씀(fog_f2 d14 KeyError 'output' 회귀)",
          shared_move.get("mode") == "input" and shared_move.get("done") and shared_move.get("moved_cm", 0.0) >= 120.0 and not shared_errors
          and str(shared_move.get("status_path", "")).endswith("_event00200ms_walk.json") and Path(shared_move.get("status_path", "")).exists(),
          (shared_move, shared_errors))

    move_errors = [lookdev.find_key_errors(apply) for apply in ({"move": {"distance": 5.0}}, {"move": [1.0, 2.0, 3.0]}, {"move": {"distance_cm": 0}}, {"move": {"distance_cm": 100.0, "yaw": "east"}},
                                                               {"schedule": {"events": [{"time": 1.0, "move": "forward"}]}}, {"move": {"distance_cm": 1500.0}}, {"move": {"distance_cm": 800.0, "yaw": 45.0}})]
    check("F5 move 형식 키 오류: 모르는 키·목록 3개·distance_cm 0·yaw 문자열·이벤트 문자열은 오류, 사전 {distance_cm, yaw?} 는 통과",
          all(errors for errors in move_errors[:5]) and move_errors[5:] == [[], []] and any("distance_cm?" in line for line in move_errors[0]), move_errors)

    reset_world([])
    with players(None, make_controller(0.0)):
        exit_code, result = run_spec("base-error", {"base": {"arm": 300.0}, "variants": [{"name": "s00_still", "settle": 0.1, "apply": {}}]})
    check("F6 base apply 원격 호출 오류(예외)는 result base.error 에만 남지 않고 키 오류 'base: apply: …'·종료 1",
          exit_code == 1 and "error" in result["base"] and any(line.startswith("base: apply: ") for line in result["key_errors"]), (exit_code, result.get("base"), result["key_errors"]))

    source_dir, copy_dir = TEST_DIR / "campfire", TEST_DIR / "frame-base-error"
    shutil.rmtree(copy_dir, ignore_errors=True)
    shutil.copytree(source_dir, copy_dir)
    result_text = (copy_dir / "campfire" / "result.json").read_text(encoding="utf-8").replace(source_dir.as_posix() + "/", copy_dir.as_posix() + "/")
    doctored = json.loads(result_text)
    doctored["key_errors"] = []
    doctored["base"]["error"] = "ValueError: too many values to unpack (expected 2)"
    doctored["variants"][1]["apply"]["move"] = {"mode": "input", "timed_out": True, "error": "시한 초과: 4.00 게임 초 안에 0/300 cm"}
    doctored_path = copy_dir / "campfire" / "result-frame-error.json"
    doctored_path.write_text(json.dumps(doctored), encoding="utf-8")
    code = run_sweep(["--plan", CAMPFIRE_PLAN, "--run-dir", str(copy_dir), "--assert-scene", f"campfire={doctored_path}"])
    swept = load_json(copy_dir / "sweep.json")
    scene = swept["scenes"][0]
    step_errors = [step["errors"] for step in scene.get("steps", [])]
    check("F7 calib_sweep: result.base.error → 장면 error(sweep 무효·종료 1), 단계 move 오류 → 그 단계만 무효",
          code == 1 and str(scene.get("error", "")).startswith("룩데브 base: apply: ValueError") and any("룩데브 base" in line for line in swept["problems"])
          and len(step_errors) == 7 and step_errors[1] and any(line.startswith("move: 시한 초과") for line in step_errors[1]) and not any(step_errors[:1] + step_errors[2:]),
          (code, scene.get("error"), step_errors))

    reset_world([])
    cloud_volume = make_exposure_volume("CloudVolume", "AEM_Manual", 0.0)
    WORLD.actors.append(cloud_volume)
    state = {"uses_tuning_reload": False, "key_errors": []}
    cloud = lookdev.apply_variant_with_tuning({"exposure_ev100": 12.0}, "s00", state).get("exposure_applied", [])
    check("F8a 구름 무대꼴(AEM_MANUAL·바이어스 0·물리 카메라 끔)·exposure_ev100 12·exposure_manual 없음: 바이어스 0 그대로(−12 스톱 회귀 없음), 결과 mode manual_ignored·현재 바이어스·엔진 무시 필드",
          cloud_volume.settings.auto_exposure_bias == 0.0 and getattr(cloud_volume.settings, "override_auto_exposure_bias", None) is None and len(cloud) == 1
          and cloud[0]["mode"] == "manual_ignored" and cloud[0]["auto_exposure_bias"] == 0.0 and "auto_exposure_min_brightness" in cloud[0]["ignored_by_engine"] and not state["key_errors"],
          (cloud_volume.settings.auto_exposure_bias, cloud))

    reset_world([])
    volumes = [make_exposure_volume("ManualVolume", "AEM_Manual", 0.5), make_exposure_volume("CameraVolume", "AEM_Manual", 0.0, True), make_exposure_volume("AutoVolume", "AEM_Histogram", 0.25)]
    WORLD.actors += volumes
    state = {"uses_tuning_reload": False, "key_errors": []}
    ignored = lookdev.apply_variant_with_tuning({"exposure_ev100": 1.0}, "s00", state).get("exposure_applied", [])
    ignored_biases = [volume.settings.auto_exposure_bias for volume in volumes]
    first = lookdev.apply_variant_with_tuning({"exposure_ev100": 1.0, "exposure_manual": "bias"}, "s01", state)
    second = lookdev.apply_variant_with_tuning({"exposure_ev100": 1.0, "exposure_manual": "bias"}, "s02", state)
    camera_ev100 = math.log2(4.0 * 4.0 * 60.0 * 100.0 / 100.0)
    biases = [volume.settings.auto_exposure_bias for volume in volumes]
    applied = second.get("exposure_applied", [])
    check("F8 exposure_manual 기본(ignore): 수동 볼륨 바이어스 불변·자동 볼륨 min/max = EV100, \"bias\": AEM_MANUAL 볼륨 auto_exposure_bias = 원래 + 물리 카메라 EV100 − EV100(엔진 수동 식)·두 번 적용해도 같음, 결과에 mode·필드·값",
          near(ignored_biases, [0.5, 0.0, 0.25]) and [entry["mode"] for entry in ignored] == ["manual_ignored", "manual_ignored", "auto"]
          and near(biases, [-0.5, camera_ev100 - 1.0, 0.25]) and volumes[2].settings.auto_exposure_min_brightness == 1.0 and volumes[2].settings.auto_exposure_max_brightness == 1.0
          and [entry["mode"] for entry in applied] == ["manual", "manual", "auto"] and near([applied[0]["fields"]["auto_exposure_bias"], applied[0]["original_bias"]], [-0.5, 0.5])
          and first["exposure"] == ["ManualVolume", "CameraVolume", "AutoVolume"] and volumes[0].settings.override_auto_exposure_bias is True and not state["key_errors"], (ignored_biases, biases, applied))
    manual_key_errors = [lookdev.find_key_errors(apply) for apply in ({"exposure_manual": "bias"}, {"exposure_ev100": 1.0, "exposure_manual": "on"},
                                                                       {"exposure_ev100": 1.0, "exposure_manual": "bias"}, {"exposure_ev100": 1.0, "exposure_manual": "ignore"})]
    check("F8b exposure_manual 키 오류: exposure_ev100 없이 1건·모르는 값 1건, bias·ignore 는 0건",
          [len(errors) for errors in manual_key_errors] == [1, 1, 0, 0] and "exposure_ev100" in manual_key_errors[0][0] and "bias" in manual_key_errors[1][0], manual_key_errors)

    reset_world([(TDFluidLiveActor, "TDGen_RainPond", FakePreset("CAMPFIRE")), (TDFluidLiveActor, "TDGen_RainBench", FakePreset("CAMPFIRE"))])
    for actor in WORLD.live_actors():
        actor.components.append(actor.live_component)
    track = FakeActor("TDGen_SnowTrack", components=[FakeLive(WORLD, FakePreset("CAMPFIRE"))])
    WORLD.actors += [track, FakeActor("TDGen_Prop")]
    lives = [actor.live_component for actor in WORLD.live_actors()] + track.components
    state = {"uses_tuning_reload": False, "key_errors": []}
    with patched(unreal, TDFluidLiveComponent=FakeLive):
        reinit = lookdev.apply_variant_with_tuning({"tuning_reload": {"tuning": "", "look": "", "reset": "reinit", "relative_to_base": False},
                                                    "schedule": {"times": [0.5], "reference": "now", "prefix": "reset"}}, "s00", state)
        status = lookdev.wait_for_schedule(Path(reinit["schedule"]["status_path"]), 30.0)
        kept = lookdev.apply_variant_with_tuning({"tuning_reload": {"tuning": "", "look": "", "reset": "keep", "relative_to_base": False}}, "s01", state)
    reload_outcome = reinit.get("tuning_reload", {})
    check("F9 reset reinit: 같은 apply 원격 호출 첫머리에서 모든 TDFluidLiveComponent reset_simulation_state(라이브 액터 2 + 다른 액터 1)·state_reset_actor_count 3·예약 시간 원점 = 초기화 게임 시각 / keep 은 초기화 없음",
          reload_outcome.get("state_reset_actor_count") == 3 and [live.reset_count for live in lives] == [1, 1, 1] and [live.init_count for live in lives[:2]] == [1, 1]
          and sorted(reload_outcome["state_reset"]["actors"]) == ["TDGen_RainBench", "TDGen_RainPond", "TDGen_SnowTrack"]
          and reload_outcome["state_reset"]["game_time"] == reinit["schedule"]["reference_game_time"] == status.get("reference_game_time") and status.get("done")
          and "state_reset" not in kept.get("tuning_reload", {}) and "state_reset" not in kept and not state["key_errors"], (reload_outcome, [live.reset_count for live in lives], state))

    reset_world([(TDFluidLiveActor, "TDGen_StormLive", FakePreset("CAMPFIRE"))])
    clouds = TDFluidCloudLayersActor("TDGen_Clouds", None)
    WORLD.actors.append(clouds)
    state = {"uses_tuning_reload": False, "key_errors": []}
    flow_steps = []
    for tag, reset_mode in (("s00", "reinit"), ("s01", "keep")):
        step = lookdev.apply_variant_with_tuning({"tuning_reload": {"tuning": "", "look": "", "reset": reset_mode, "relative_to_base": False},
                                                  "schedule": {"times": [0.5], "reference": "now", "prefix": f"flow_{reset_mode}"}}, tag, state)
        lookdev.wait_for_schedule(Path(step["schedule"]["status_path"]), 30.0)
        flow_steps.append(step)
    restarts = [step.get("cloud_flow_restart", {}) for step in flow_steps]
    reset_world([])
    no_cloud = lookdev.apply_variant_with_tuning({}, "s02", state)
    reset_world([])
    WORLD.actors.append(TDFluidCloudLayersActor("TDGen_BrokenClouds", None))

    def refuse_restart(actor):
        raise RuntimeError("restart_cloud_flow_clock 없음")

    broken_state = {"uses_tuning_reload": False, "key_errors": []}
    with patched(TDFluidCloudLayersActor, restart_cloud_flow_clock=refuse_restart):
        broken = lookdev.apply_variant_with_tuning({}, "s03", broken_state)
    broken_restart = broken.get("cloud_flow_restart", {})
    check("F10 구름 흐름 시계: reinit·keep 단계마다 같은 apply 원격 호출 첫머리에서 TDFluidCloudLayersActor restart_cloud_flow_clock 1회·cloud_flow_restart.game_time = 예약 reference_game_time / 구름 액터 없으면 키 없음 / 재시작 예외는 키 오류 'cloud_flow_restart: <라벨>: '",
          len(clouds.flow_restart_game_times) == 2 and all(item.get("actors") == ["TDGen_Clouds"] and item.get("errors") == [] for item in restarts)
          and [item.get("game_time") for item in restarts] == clouds.flow_restart_game_times == [step["schedule"]["reference_game_time"] for step in flow_steps]
          and clouds.flow_restart_game_times[0] < clouds.flow_restart_game_times[1] and "cloud_flow_restart" not in no_cloud and not state["key_errors"]
          and broken_restart.get("actors") == [] and len(broken_restart.get("errors", [])) == 1
          and any(line.startswith("s03: cloud_flow_restart: TDGen_BrokenClouds: ") for line in broken_state["key_errors"]),
          (restarts, clouds.flow_restart_game_times, [step.get("schedule") for step in flow_steps], no_cloud.get("cloud_flow_restart"), broken_restart, state["key_errors"], broken_state["key_errors"]))

    origin_name = fog_material.FLOW_ORIGIN_PARAMETER_NAME
    origin_key = (fog_material.COLLECTION_PATH, origin_name)

    def fog_user(label, material):
        return FakeActor(label, components=[FakeStaticMeshComponent("FogBox", [material])])

    def fog_instance(name):
        return FakeMaterialInstanceConstant(fog_material.asset_path(name), parent=FakeMaterial(fog_material.MASTER_PATH))

    reset_world([(TDFluidLiveActor, "TDGen_FogAmbient", FakePreset("CAMPFIRE"))])
    WORLD.add_asset(FakeMaterialParameterCollection(fog_material.COLLECTION_PATH, [name for name, _ in fog_material.COLLECTION_SCALAR_DEFAULTS]))
    WORLD.actors.append(fog_user("TDGen_FogPassiveField", fog_instance("MI_TD_FluidFog_PassiveField")))
    state = {"uses_tuning_reload": False, "key_errors": []}
    origin_steps = []
    for tag, reset_mode in (("s00", "reinit"), ("s01", "keep")):
        step = lookdev.apply_variant_with_tuning({"tuning_reload": {"tuning": "", "look": "", "reset": reset_mode, "relative_to_base": False},
                                                  "schedule": {"times": [0.5], "reference": "now", "prefix": f"fog_{reset_mode}"}}, tag, state)
        lookdev.wait_for_schedule(Path(step["schedule"]["status_path"]), 30.0)
        origin_steps.append(step)
    origins = [step.get("fog_flow_origin", {}) for step in origin_steps]
    final_origin = WORLD.collection_scalars.get(origin_key)

    reset_world([])
    WORLD.add_asset(FakeMaterialParameterCollection(fog_material.COLLECTION_PATH, []))
    WORLD.actors.append(fog_user("TDGen_FogRoom", FakeMaterialInstanceDynamic(fog_instance("MI_TD_FluidFog_Sim"))))
    stale_state = {"uses_tuning_reload": False, "key_errors": []}
    stale = lookdev.apply_variant_with_tuning({}, "s02", stale_state).get("fog_flow_origin", {})
    stale_scalars = dict(WORLD.collection_scalars)

    reset_world([])
    WORLD.add_asset(FakeMaterialParameterCollection(fog_material.COLLECTION_PATH, []))
    WORLD.actors.append(fog_user("TDGen_CloudProp", FakeMaterialInstanceConstant("/Game/FluidFX/Materials/MI_TD_FluidCloud_Fake", parent=FakeMaterial("/Game/FluidFX/Materials/M_TD_FluidCloudLayers"))))
    non_fog_state = {"uses_tuning_reload": False, "key_errors": []}
    non_fog = lookdev.apply_variant_with_tuning({}, "s03", non_fog_state)

    reset_world([])
    WORLD.actors.append(fog_user("TDGen_FogCut", fog_instance("MI_TD_FluidFog_Sim")))
    missing_state = {"uses_tuning_reload": False, "key_errors": []}
    missing = lookdev.apply_variant_with_tuning({}, "s04", missing_state).get("fog_flow_origin", {})
    check("F11 안개 흐름 원점: 안개 마스터 사용자(MI·MID 기반 재질)가 있으면 reinit·keep 단계마다 같은 apply 원격 호출 첫머리에서 MPC FogFlowOriginSeconds = 그 apply 게임 시각 = 예약 reference_game_time(다시 읽은 value 같음, MPC 경로·이름은 재질 스크립트 상수, 기본 0) / MPC 매개변수 없음(재질 재생성 전)·MPC 없음은 키 오류 'fog_flow_origin: ' 로 단계 무효 / 비안개 무대는 MPC 가 메모리에 있어도 키 없음",
          dict(fog_material.COLLECTION_SCALAR_DEFAULTS) == {origin_name: 0.0}
          and all(item.get("mpc") == fog_material.COLLECTION_PATH and item.get("user") == "TDGen_FogPassiveField" and item.get("errors") == [] for item in origins)
          and [item.get("game_time") for item in origins] == [item.get("value") for item in origins] == [step["schedule"]["reference_game_time"] for step in origin_steps]
          and origins[0]["game_time"] < origins[1]["game_time"] and final_origin == origins[1]["game_time"] and not state["key_errors"]
          and stale.get("user") == "TDGen_FogRoom" and stale.get("value") is None and len(stale.get("errors", [])) == 1 and stale_scalars == {}
          and any(line.startswith(f"s02: fog_flow_origin: MPC 매개변수 없음: {origin_name}") for line in stale_state["key_errors"])
          and "fog_flow_origin" not in non_fog and not non_fog_state["key_errors"]
          and missing.get("value") is None and any(line.startswith(f"s04: fog_flow_origin: MPC 없음: {fog_material.COLLECTION_PATH}") for line in missing_state["key_errors"]),
          (origins, final_origin, [step.get("schedule", {}).get("reference_game_time") for step in origin_steps], state["key_errors"], stale, stale_scalars, stale_state["key_errors"],
           non_fog.get("fog_flow_origin"), non_fog_state["key_errors"], missing, missing_state["key_errors"]))
    look_entries = {fog_material.asset_path(name): {"Parent": fog_material.MASTER_PATH, "Scalars": {}, "Vectors": {}} for name in fog_material.INSTANCE_NAMES}
    look_entries[fog_material.asset_path("MI_TD_FluidFog_PassiveField")]["Scalars"] = {origin_name: 5.0, "FogExtinctionPerM": 0.1}
    override_problems = fog_material.find_instance_override_problems(look_entries)
    check("F11b 안개 재질 스크립트: 모양 정본 MI 가 룩데브 구동 MPC 이름 FogFlowOriginSeconds 를 덮으면 그 MI 하나만 '룩데브 구동 MPC 이름 덮어씀'(마스터에 없는 이름 중복 없음), 모양 이름은 통과",
          override_problems == [f"MI_TD_FluidFog_PassiveField: 룩데브 구동 MPC 이름 덮어씀 {origin_name}"], override_problems)


class RecordedMaterialNode(Props):
    def __init__(self, expression_class):
        super().__init__()
        self.expression_class = expression_class

    def get_name(self):
        return self.expression_class.__name__


def record_material_graph():
    links = []

    def create_expression(material, expression_class, x, y):
        return RecordedMaterialNode(expression_class)

    def connect(source, source_output, target, target_input):
        links.append((source, source_output, target, target_input))
        return True

    return links, create_expression, connect


def find_linked_source(links, target, target_input):
    return next((source for source, _, linked_target, linked_input in links if linked_target is target and linked_input == target_input), None)


def is_origin_subtract(node, links, collection, parameter_name):
    game_time = find_linked_source(links, node, "A")
    origin = find_linked_source(links, node, "B")
    return (node.get_name() == "MaterialExpressionSubtract" and game_time is not None and game_time.get_name() == "MaterialExpressionTime"
            and origin is not None and origin.get_name() == "MaterialExpressionCollectionParameter" and origin.collection is collection and origin.parameter_name == parameter_name)


def make_collection_props():
    return Props(vector_parameters=[], scalar_parameters=[])


def collection_parameter_types():
    return {"CollectionScalarParameter": type("CollectionScalarParameter", (Props,), {}), "CollectionVectorParameter": type("CollectionVectorParameter", (Props,), {})}


def stored_collection_scalars(collection):
    return [(item.parameter_name, item.default_value) for item in collection.scalar_parameters]


def check_time_origin_fixes():
    output_dir = TEST_DIR / "time-origin"
    output_dir.mkdir(parents=True, exist_ok=True)
    lookdev.OUTPUT_DIRECTORY = output_dir

    def material_user(label, material):
        return FakeActor(label, components=[FakeStaticMeshComponent("Mesh", [material])])

    def instance_of(master_path, name):
        return FakeMaterialInstanceConstant(f"{volume_material.MATERIAL_DIR}/{name}", parent=FakeMaterial(master_path))

    def add_collection(path, scalar_names):
        WORLD.add_asset(FakeMaterialParameterCollection(path, scalar_names))

    def fresh_state():
        return {"uses_tuning_reload": False, "key_errors": []}

    def apply_reinit_and_keep(prefix, state):
        steps = []
        for tag, reset_mode in (("s00", "reinit"), ("s01", "keep")):
            step = lookdev.apply_variant_with_tuning({"tuning_reload": {"tuning": "", "look": "", "reset": reset_mode, "relative_to_base": False},
                                                      "schedule": {"times": [0.5], "reference": "now", "prefix": f"{prefix}_{reset_mode}"}}, tag, state)
            lookdev.wait_for_schedule(Path(step["schedule"]["status_path"]), 30.0)
            steps.append(step)
        return steps

    def reference_times(steps):
        return [step["schedule"]["reference_game_time"] for step in steps]

    volume_master_path = volume_material.material_asset_path(volume_material.VOLUME_MASTER["name"])
    fake_master_path = volume_material.material_asset_path(volume_material.FAKE_MASTER["name"])
    volume_origin_name = volume_material.FLOW_ORIGIN_PARAMETER_NAME
    volume_scalar_names = [name for name, _ in volume_material.COLLECTION_SCALAR_DEFAULTS]

    reset_world([(TDFluidLiveActor, "TDGen_SmokeLive", FakePreset("CAMPFIRE"))])
    add_collection(volume_material.FLOW_COLLECTION_PATH, volume_scalar_names)
    WORLD.actors.append(material_user("TDGen_SmokeVolume", FakeMaterialInstanceDynamic(instance_of(volume_master_path, "MI_TD_FluidVolume_SmokeBillow"))))
    state = fresh_state()
    volume_steps = apply_reinit_and_keep("volume", state)
    volume_origins = [step.get("volume_flow_origin", {}) for step in volume_steps]
    final_volume_origin = WORLD.collection_scalars.get((volume_material.FLOW_COLLECTION_PATH, volume_origin_name))
    stray_keys = [key for step in volume_steps for key in ("fog_flow_origin", "foliage_wind_origin") if key in step]

    reset_world([])
    add_collection(volume_material.FLOW_COLLECTION_PATH, volume_scalar_names)
    WORLD.actors.append(material_user("TDGen_FakeVolume", instance_of(fake_master_path, "MI_TD_FluidFakeVolume_GrayFluff")))
    fake_state = fresh_state()
    fake_only = lookdev.apply_variant_with_tuning({}, "s02", fake_state)

    reset_world([])
    add_collection(volume_material.FLOW_COLLECTION_PATH, [])
    WORLD.actors.append(material_user("TDGen_SmokeStale", instance_of(volume_master_path, "MI_TD_FluidVolume_SmokeHard")))
    stale_state = fresh_state()
    stale = lookdev.apply_variant_with_tuning({}, "s03", stale_state).get("volume_flow_origin", {})
    stale_scalars = dict(WORLD.collection_scalars)

    reset_world([])
    WORLD.actors.append(material_user("TDGen_SmokeCut", instance_of(volume_master_path, "MI_TD_FluidVolume_SmokeHard")))
    missing_state = fresh_state()
    missing = lookdev.apply_variant_with_tuning({}, "s04", missing_state).get("volume_flow_origin", {})
    check("F12 볼륨 흐름 원점: 기반 재질 M_TD_FluidVolume 사용자(MID)가 있으면 reinit·keep 단계마다 같은 apply 원격 호출 첫머리에서 MPC_TD_FluidVolumeFlow VolumeFlowOriginSeconds = 그 apply 게임 시각 = 예약 reference_game_time(다시 읽은 value 같음, 경로·이름은 볼륨 재질 스크립트 상수, 기본 0) / 안개·식생 키 없음 / 가짜 볼륨 마스터 사용자만이면 키 없음 / MPC 매개변수 없음·MPC 없음은 키 오류 'volume_flow_origin: ' 로 단계 무효",
          dict(volume_material.COLLECTION_SCALAR_DEFAULTS) == {volume_origin_name: 0.0} and volume_material.FLOW_COLLECTION_PATH == "/Game/FluidFX/Materials/MPC_TD_FluidVolumeFlow"
          and all(item.get("mpc") == volume_material.FLOW_COLLECTION_PATH and item.get("user") == "TDGen_SmokeVolume" and item.get("errors") == [] for item in volume_origins)
          and [item.get("game_time") for item in volume_origins] == [item.get("value") for item in volume_origins] == reference_times(volume_steps)
          and volume_origins[0]["game_time"] < volume_origins[1]["game_time"] and final_volume_origin == volume_origins[1]["game_time"] and not stray_keys and not state["key_errors"]
          and "volume_flow_origin" not in fake_only and not fake_state["key_errors"]
          and stale.get("user") == "TDGen_SmokeStale" and stale.get("value") is None and len(stale.get("errors", [])) == 1 and stale_scalars == {}
          and any(line.startswith(f"s03: volume_flow_origin: MPC 매개변수 없음: {volume_origin_name}") for line in stale_state["key_errors"])
          and missing.get("value") is None and any(line.startswith(f"s04: volume_flow_origin: MPC 없음: {volume_material.FLOW_COLLECTION_PATH}") for line in missing_state["key_errors"]),
          (volume_origins, final_volume_origin, reference_times(volume_steps), stray_keys, state["key_errors"], fake_only.get("volume_flow_origin"), fake_state["key_errors"],
           stale, stale_scalars, stale_state["key_errors"], missing, missing_state["key_errors"]))

    look = copy.deepcopy(WORLD.look_table)
    clean_table = fluid_material_look.TDMaterialLookTable(copy.deepcopy(look), "fixture")
    base_unknown = volume_material.find_unknown_instance_overrides(clean_table)
    base_driven = volume_material.find_collection_driven_overrides(clean_table)
    look["Materials"][volume_material.material_asset_path("MI_TD_FluidVolume_SmokeHard")]["Scalars"][volume_origin_name] = 5.0
    injected_table = fluid_material_look.TDMaterialLookTable(look, "fixture")
    injected_unknown = volume_material.find_unknown_instance_overrides(injected_table)
    injected_driven = volume_material.find_collection_driven_overrides(injected_table)
    check("F12b 볼륨 재질 스크립트: 모양 정본 MI 가 룩데브 구동 MPC 이름 VolumeFlowOriginSeconds 를 덮으면 그 MI 하나만 '룩데브 구동 MPC 이름' 목록(마스터에 없는 이름 목록에 중복 없음), 정본 그대로는 0건",
          base_driven == [] and injected_driven == [f"MI_TD_FluidVolume_SmokeHard.{volume_origin_name}"] and injected_unknown == base_unknown,
          (base_driven, injected_driven, base_unknown, injected_unknown))

    links, create_expression, connect = record_material_graph()
    flow_collection = FakeMaterialParameterCollection(volume_material.FLOW_COLLECTION_PATH, volume_scalar_names)
    volume_sources, fake_sources = {}, {}
    with patched(FakeMaterialEditingLibrary, create_material_expression=create_expression, connect_material_expressions=connect):
        volume_material.add_builtin_sources(None, volume_material.VOLUME_MASTER, volume_sources, flow_collection)
        fake_link_count = len(links)
        volume_material.add_builtin_sources(None, volume_material.FAKE_MASTER, fake_sources, flow_collection)
    volume_elapsed, fake_elapsed = volume_sources["ElapsedSeconds"][0], fake_sources["ElapsedSeconds"][0]
    flow_saves = []
    existing_collection = FakeMaterialParameterCollection(volume_material.FLOW_COLLECTION_PATH, [])
    with patched(unreal, EditorAssetLibrary=Props(does_asset_exist=lambda path: path == volume_material.FLOW_COLLECTION_PATH, load_asset=lambda path: existing_collection), **collection_parameter_types()), \
            patched(volume_material, save_asset=lambda asset, path: flow_saves.append(path)):
        built_collection = volume_material.load_or_create_flow_collection(True)
        try:
            volume_material.load_or_create_flow_collection(False)
            preserved = False
        except RuntimeError as error:
            preserved = str(error).startswith("기존 에셋 보존")
    check("F12c 볼륨 마스터 그래프: M_TD_FluidVolume ElapsedSeconds 원천 = Subtract(A Time, B CollectionParameter MPC_TD_FluidVolumeFlow.VolumeFlowOriginSeconds), M_TD_FluidFakeVolume 은 Time 그대로(뺄셈 없음) / 흐름 MPC 재사용은 overwrite 때만 스칼라 {VolumeFlowOriginSeconds: 0} 로 쓰고 저장, overwrite 없으면 '기존 에셋 보존' / 대상 에셋 목록에 MPC",
          is_origin_subtract(volume_elapsed, links, flow_collection, volume_origin_name) and fake_elapsed.get_name() == "MaterialExpressionTime"
          and not any(target is fake_elapsed for _, _, target, _ in links[fake_link_count:]) and volume_material.describe_elapsed_seconds(volume_material.FAKE_MASTER) == "Time"
          and built_collection is existing_collection and stored_collection_scalars(existing_collection) == list(volume_material.COLLECTION_SCALAR_DEFAULTS)
          and flow_saves == [volume_material.FLOW_COLLECTION_PATH] and preserved and volume_material.FLOW_COLLECTION_PATH in volume_material.collect_target_asset_paths(),
          ([(source.get_name(), output, target.get_name(), target_input) for source, output, target, target_input in links], fake_elapsed.get_name(), stored_collection_scalars(existing_collection), flow_saves, preserved))

    hvol_links, hvol_create, hvol_connect = record_material_graph()
    hvol_collection = FakeMaterialParameterCollection(hvol_material.FLOW_COLLECTION_PATH, volume_scalar_names)
    hvol_textures = {name: (None, None) for name in hvol_material.TEXTURE_PARAMETER_NAMES}
    with patched(FakeMaterialEditingLibrary, create_material_expression=hvol_create, connect_material_expressions=hvol_connect), \
            patched(hvol_material, build_object_position=lambda material, x, y: RecordedMaterialNode(unreal.MaterialExpressionAbs),
                    build_object_half_extents=lambda material, x, y: RecordedMaterialNode(unreal.MaterialExpressionAbs)):
        hvol_sources = hvol_material.build_sources(None, hvol_textures, hvol_collection)
    hvol_elapsed = hvol_sources["ElapsedSeconds"][0]
    hvol_time_targets = [target for source, _, target, _ in hvol_links if source.get_name() == "MaterialExpressionTime"]

    def hvol_flow_error(collection):
        with patched(unreal, EditorAssetLibrary=Props(load_asset=lambda path: collection)):
            try:
                hvol_material.load_flow_collection()
            except RuntimeError as error:
                return str(error)
        return ""

    with patched(unreal, EditorAssetLibrary=Props(load_asset=lambda path: hvol_collection if path == hvol_material.FLOW_COLLECTION_PATH else None)):
        hvol_loaded = hvol_material.load_flow_collection()
    hvol_stale, hvol_missing = hvol_flow_error(FakeMaterialParameterCollection(hvol_material.FLOW_COLLECTION_PATH, [])), hvol_flow_error(None)
    check("F12d HVOL 마스터 그래프: M_TD_FluidHeterogeneousVolume ElapsedSeconds 원천 = Subtract(A Time, B CollectionParameter MPC_TD_FluidVolumeFlow.VolumeFlowOriginSeconds)(SVOL·룩데브와 같은 MPC 경로·이름, 보고 식 같음, Time 은 뺄셈에만) / MPC 는 읽기만: 있으면 그 MPC, 매개변수 없음·MPC 없음은 '매개변수 없음'·'MPC 없음' 중단 / 필요 언리얼 이름에 CollectionParameter·Subtract·MaterialParameterCollection",
          is_origin_subtract(hvol_elapsed, hvol_links, hvol_collection, volume_origin_name) and hvol_time_targets == [hvol_elapsed]
          and hvol_material.FLOW_COLLECTION_PATH == volume_material.FLOW_COLLECTION_PATH and hvol_material.FLOW_ORIGIN_PARAMETER_NAME == volume_origin_name
          and f'VOLUME_FLOW_COLLECTION_PATH = "{hvol_material.FLOW_COLLECTION_PATH}"' in lookdev.LOOKDEV_ACTIONS and f'VOLUME_FLOW_ORIGIN_PARAMETER_NAME = "{volume_origin_name}"' in lookdev.LOOKDEV_ACTIONS
          and hvol_material.describe_elapsed_seconds() == volume_material.describe_elapsed_seconds(volume_material.VOLUME_MASTER)
          and hvol_loaded is hvol_collection and "매개변수 없음" in hvol_stale and "MPC 없음" in hvol_missing
          and all(name in hvol_material.REQUIRED_UNREAL_NAMES for name in ("MaterialExpressionCollectionParameter", "MaterialExpressionSubtract", "MaterialParameterCollection")),
          ([(source.get_name(), output, target.get_name(), target_input) for source, output, target, target_input in hvol_links if target is hvol_elapsed], hvol_time_targets, hvol_stale, hvol_missing))

    wind_origin_name = foliage_material.WIND_ORIGIN_PARAMETER_NAME
    wind_scalar_names = [name for name, _ in foliage_material.COLLECTION_SCALAR_DEFAULTS]
    wind_collections = [(foliage_material.asset_path(channel.grass_master_name), foliage_material.asset_path(channel.collection_name)) for channel in foliage_material.FOLIAGE_CHANNELS]
    fog_master_path = fog_material.MASTER_PATH

    reset_world([(TDFluidLiveActor, "TDGen_FoliageLive", FakePreset("CAMPFIRE"))])
    for index, (master_path, collection_path) in enumerate(wind_collections):
        add_collection(collection_path, wind_scalar_names)
        WORLD.actors.append(material_user(f"TDGen_Grass{index}", instance_of(master_path, f"MI_TD_FluidFoliage_Test{index}")))
    add_collection(fog_material.COLLECTION_PATH, [name for name, _ in fog_material.COLLECTION_SCALAR_DEFAULTS])
    add_collection(volume_material.FLOW_COLLECTION_PATH, volume_scalar_names)
    WORLD.actors.append(material_user("TDGen_FogField", instance_of(fog_master_path, "MI_TD_FluidFog_PassiveField")))
    WORLD.actors.append(material_user("TDGen_DustVolume", instance_of(volume_master_path, "MI_TD_FluidVolume_GroundDust")))
    state = fresh_state()
    wind_steps = apply_reinit_and_keep("wind", state)
    winds = [step.get("foliage_wind_origin", []) for step in wind_steps]
    expected_pairs = [(collection_path, f"TDGen_Grass{index}") for index, (_, collection_path) in enumerate(wind_collections)]
    final_winds = [WORLD.collection_scalars.get((collection_path, wind_origin_name)) for _, collection_path in wind_collections]
    shared_origins_match = all(step.get(key, {}).get("value") == step["schedule"]["reference_game_time"] for step in wind_steps for key in ("fog_flow_origin", "volume_flow_origin"))

    reset_world([])
    for index, (master_path, collection_path) in enumerate(wind_collections[:2]):
        add_collection(collection_path, [] if index == 0 else wind_scalar_names)
        WORLD.actors.append(material_user(f"TDGen_Grass{index}", instance_of(master_path, f"MI_TD_FluidFoliage_Test{index}")))
    stale_state = fresh_state()
    stale_winds = lookdev.apply_variant_with_tuning({}, "s02", stale_state).get("foliage_wind_origin", [])
    stale_name = wind_collections[0][1].rsplit("/", 1)[-1]

    reset_world([])
    WORLD.actors.append(material_user("TDGen_Rock", instance_of("/Game/FluidFX/Materials/M_TD_FluidTrackLayer", "MI_TD_FluidTrack_Snow")))
    for _, collection_path in wind_collections:
        add_collection(collection_path, wind_scalar_names)
    plain_state = fresh_state()
    plain = lookdev.apply_variant_with_tuning({}, "s03", plain_state)
    check("F13 잎 바람 원점: 채널 풀 마스터(Grass·GrassPress·Reed) 사용자가 있는 채널마다 reinit·keep 단계 apply 첫머리에서 그 채널 MPC FoliageWindOriginSeconds = 그 apply 게임 시각(채널 순서·MPC 경로는 식생 재질 스크립트 FOLIAGE_CHANNELS, 다시 읽은 value 같음) / 같은 무대의 안개·볼륨 원점도 같은 시각(사용자 찾기 1회) / 한 채널 MPC 매개변수 없음은 그 채널만 키 오류 'foliage_wind_origin: <MPC 이름>: ' / 풀 마스터 사용자 없으면 키 없음",
          dict(foliage_material.COLLECTION_SCALAR_DEFAULTS) == {wind_origin_name: 0.0}
          and all([(item.get("mpc"), item.get("user")) for item in wind] == expected_pairs for wind in winds)
          and all(item.get("value") == item.get("game_time") == step["schedule"]["reference_game_time"] and item.get("errors") == [] for step, wind in zip(wind_steps, winds) for item in wind)
          and final_winds == [winds[1][0]["game_time"]] * len(wind_collections) and winds[0][0]["game_time"] < winds[1][0]["game_time"] and shared_origins_match and not state["key_errors"]
          and [item.get("value") is None for item in stale_winds] == [True, False] and len(stale_state["key_errors"]) == 1
          and stale_state["key_errors"][0].startswith(f"s02: foliage_wind_origin: {stale_name}: MPC 매개변수 없음: {wind_origin_name}")
          and not any(key in plain for key in ("fog_flow_origin", "volume_flow_origin", "foliage_wind_origin")) and not plain_state["key_errors"],
          (winds, expected_pairs, final_winds, [step.get("schedule", {}).get("reference_game_time") for step in wind_steps], shared_origins_match, state["key_errors"],
           stale_winds, stale_state["key_errors"], plain_state["key_errors"]))

    entries = copy.deepcopy(WORLD.look_table["Materials"])
    base_problems = foliage_material.find_instance_override_problems(entries)
    entries[foliage_material.asset_path("MI_TD_FluidFoliage_GrassPress")]["Scalars"][wind_origin_name] = 5.0
    new_problems = [problem for problem in foliage_material.find_instance_override_problems(entries) if problem not in base_problems]
    check("F13b 식생 재질 스크립트: 모양 정본 풀 MI 가 룩데브 구동 MPC 이름 FoliageWindOriginSeconds 를 덮으면 그 MI 하나만 '룩데브 구동 MPC 이름 덮어씀'(마스터에 없는 이름·구획 어긋남 중복 없음)",
          new_problems == [f"MI_TD_FluidFoliage_GrassPress: 룩데브 구동 MPC 이름 덮어씀 {wind_origin_name}"], (new_problems, base_problems))

    links, create_expression, connect = record_material_graph()
    wind_collection = make_collection_props()
    factory = foliage_material.NodeFactory(lambda expression_class, x, y: create_expression(None, expression_class, x, y))
    channel = foliage_material.FOLIAGE_CHANNELS[1]
    collection_loads = []
    with patched(FakeMaterialEditingLibrary, connect_material_expressions=connect):
        wind_elapsed = foliage_material.build_wind_elapsed_seconds(factory, wind_collection)
    with patched(unreal, **collection_parameter_types()), \
            patched(foliage_material, load_or_create=lambda path, asset_class, asset_factory, overwrite: (collection_loads.append(path) or wind_collection, False), save_asset=lambda asset, path: None):
        _, was_created = foliage_material.load_or_create_collection(channel, True)
    check("F13c 식생 휨 MF 그래프: ElapsedSeconds 원천 = Subtract(A Time, B CollectionParameter 채널 MPC.FoliageWindOriginSeconds) / 채널 MPC 는 벡터 3개 그대로에 스칼라 {FoliageWindOriginSeconds: 0} 를 더해 씀",
          is_origin_subtract(wind_elapsed, links, wind_collection, wind_origin_name) and collection_loads == [foliage_material.asset_path(channel.collection_name)] and was_created is False
          and [item.parameter_name for item in wind_collection.vector_parameters] == [name for name, _ in foliage_material.COLLECTION_VECTOR_DEFAULTS]
          and stored_collection_scalars(wind_collection) == list(foliage_material.COLLECTION_SCALAR_DEFAULTS),
          ([(source.get_name(), output, target.get_name(), target_input) for source, output, target, target_input in links], collection_loads, stored_collection_scalars(wind_collection)))

    reset_world([(TDFluidLiveActor, "TDGen_StirLive", FakePreset("CAMPFIRE")), (TDFluidFireActor, "TDGen_FireLive", FakePreset("CAMPFIRE"))])
    for actor in WORLD.live_actors():
        actor.components.append(actor.live_component)
    WORLD.actors.append(FakeActor("TDGen_SnowTrack", components=[FakeLive(WORLD, FakePreset("CAMPFIRE"))]))
    live_actors = WORLD.live_actors()
    state = fresh_state()
    with patched(unreal, TDFluidLiveComponent=FakeLive):
        reset_steps = apply_reinit_and_keep("history", state)
    history_counts = [actor.history_reset_count for actor in live_actors]
    reinit_reset = reset_steps[0].get("tuning_reload", {}).get("state_reset", {})

    def refuse_history(actor):
        raise RuntimeError("reset_interaction_history 없음")

    broken_state = fresh_state()
    with patched(unreal, TDFluidLiveComponent=FakeLive), patched(TDFluidLiveActor, reset_interaction_history=refuse_history):
        broken = lookdev.apply_variant_with_tuning({"tuning_reload": {"tuning": "", "look": "", "reset": "reinit", "relative_to_base": False}}, "s02", broken_state)
    broken_reset = broken.get("tuning_reload", {}).get("state_reset", {})
    check("F14 reinit 상태 초기화가 TDFluidLiveActor 마다 reset_interaction_history() 1회(이전 위치·잔류 도장 기록, 단계 첫머리 순간이동 도장 fx15a s00), 라이브 액터가 아닌 라이브 컴포넌트 소유자는 컴포넌트만 / keep 은 호출 없음 / 결과 state_reset.interaction_history_actors / 호출 실패(C++ 빌드 전)는 그 액터 키 오류 'state_reset: <라벨>: ' 로 단계 무효",
          history_counts == [1, 1] and sorted(reinit_reset.get("interaction_history_actors", [])) == ["TDGen_FireLive", "TDGen_StirLive"]
          and sorted(reinit_reset.get("actors", [])) == ["TDGen_FireLive", "TDGen_SnowTrack", "TDGen_StirLive"] and "state_reset" not in reset_steps[1].get("tuning_reload", {})
          and not state["key_errors"] and broken_reset.get("actors") == ["TDGen_SnowTrack"] and broken_reset.get("interaction_history_actors") == []
          and sorted(line.split(": ", 3)[2] for line in broken_state["key_errors"] if line.startswith("s02: state_reset: ")) == ["TDGen_FireLive", "TDGen_StirLive"],
          (history_counts, reinit_reset, reset_steps[1].get("tuning_reload", {}).get("state_reset"), state["key_errors"], broken_reset, broken_state["key_errors"]))


def check_water_time_origin_fixes():
    output_dir = TEST_DIR / "water-time-origin"
    output_dir.mkdir(parents=True, exist_ok=True)
    lookdev.OUTPUT_DIRECTORY = output_dir
    origin_name = water_material.FLOW_ORIGIN_PARAMETER_NAME
    collection_path = water_material.FLOW_COLLECTION_PATH
    scalar_names = [name for name, _ in water_material.COLLECTION_SCALAR_DEFAULTS]

    def water_user(label, master_path, instance_name):
        instance = FakeMaterialInstanceConstant(water_material.asset_path(instance_name), parent=FakeMaterial(master_path))
        return FakeActor(label, components=[FakeStaticMeshComponent("Mesh", [FakeMaterialInstanceDynamic(instance)])])

    def fresh_state():
        return {"uses_tuning_reload": False, "key_errors": []}

    def build_world(master_path, scalar_names_or_none, particle_labels):
        reset_world([])
        if scalar_names_or_none is not None:
            WORLD.add_asset(FakeMaterialParameterCollection(collection_path, scalar_names_or_none))
        WORLD.actors.append(water_user("TDGen_WaterPool", master_path, "MI_TD_FluidWater_Lake"))
        particles = [TDFluidWaterParticleActor(label) for label in particle_labels]
        WORLD.actors.extend(particles)
        return particles

    particles = build_world(water_material.WATER_MASTER_PATH, scalar_names, ["TDGen_WaterParticles", "TDGen_WaterParticlesFar"])
    state = fresh_state()
    steps = []
    for tag, reset_mode in (("s00", "reinit"), ("s01", "keep")):
        step = lookdev.apply_variant_with_tuning({"tuning_reload": {"tuning": "", "look": "", "reset": reset_mode, "relative_to_base": False},
                                                  "schedule": {"times": [0.5], "reference": "now", "prefix": f"water_{reset_mode}"}}, tag, state)
        lookdev.wait_for_schedule(Path(step["schedule"]["status_path"]), 30.0)
        steps.append(step)
    origins = [step.get("water_flow_origin", {}) for step in steps]
    reference_times = [step["schedule"]["reference_game_time"] for step in steps]
    final_value = WORLD.collection_scalars.get((collection_path, origin_name))
    stray_keys = [key for step in steps for key in ("fog_flow_origin", "volume_flow_origin", "foliage_wind_origin") if key in step]

    lite_particles = build_world(water_material.LITE_MASTER_PATH, scalar_names, ["TDGen_LiteParticles"])
    lite_state = fresh_state()
    lite = lookdev.apply_variant_with_tuning({}, "s02", lite_state).get("water_flow_origin", {})

    stale_particles = build_world(water_material.WATER_MASTER_PATH, [], ["TDGen_StaleParticles"])
    stale_state = fresh_state()
    stale = lookdev.apply_variant_with_tuning({}, "s03", stale_state).get("water_flow_origin", {})

    build_world(water_material.WATER_MASTER_PATH, None, [])
    missing_state = fresh_state()
    missing = lookdev.apply_variant_with_tuning({}, "s04", missing_state).get("water_flow_origin", {})

    unbuilt_particles = build_world(water_material.WATER_MASTER_PATH, scalar_names, ["TDGen_UnbuiltParticles"])
    unbuilt_state = fresh_state()
    with patched(TDFluidWaterParticleActor, set_water_time_origin_seconds=None):
        unbuilt = lookdev.apply_variant_with_tuning({}, "s05", unbuilt_state).get("water_flow_origin", {})

    reset_world([])
    WORLD.add_asset(FakeMaterialParameterCollection(collection_path, scalar_names))
    dry_particles = [TDFluidWaterParticleActor("TDGen_DryParticles")]
    WORLD.actors.extend(dry_particles)
    dry_state = fresh_state()
    dry = lookdev.apply_variant_with_tuning({}, "s06", dry_state)
    check("F15 물 흐름 원점: 기반 재질 M_TD_FluidWater·M_TD_FluidWaterLite 사용자(MID)가 있으면 reinit·keep 단계마다 같은 apply 원격 호출 첫머리에서 MPC_TD_FluidWaterFlow WaterFlowOriginSeconds = 그 apply 게임 시각 = 예약 reference_game_time(다시 읽은 value 같음, 경로·이름은 물 재질 스크립트 상수, 기본 0)이고 레벨의 모든 TDFluidWaterParticleActor 에 같은 값으로 set_water_time_origin_seconds 1회(actors 라벨) / 안개·볼륨·식생 키 없음 / MPC 매개변수 없음·MPC 없음은 키 오류 'water_flow_origin: '(입자 원점은 그대로 설정) / 입자 액터 설정 함수 없음(C++ 빌드 전)은 그 액터 키 오류 / 물 마스터 사용자 없으면 입자 액터가 있어도 키 없음·호출 없음",
          dict(water_material.COLLECTION_SCALAR_DEFAULTS) == {origin_name: 0.0} and collection_path == "/Game/FluidFX/Materials/MPC_TD_FluidWaterFlow"
          and all(item.get("mpc") == collection_path and item.get("user") == "TDGen_WaterPool" and item.get("errors") == [] for item in origins)
          and all(item.get("actors") == ["TDGen_WaterParticles", "TDGen_WaterParticlesFar"] for item in origins)
          and [item.get("game_time") for item in origins] == [item.get("value") for item in origins] == reference_times
          and origins[0]["game_time"] < origins[1]["game_time"] and final_value == origins[1]["game_time"]
          and all(particle.water_time_origins == reference_times for particle in particles) and not stray_keys and not state["key_errors"]
          and lite.get("value") == lite.get("game_time") and lite.get("errors") == [] and lite_particles[0].water_time_origins == [lite["game_time"]] and not lite_state["key_errors"]
          and stale.get("value") is None and len(stale.get("errors", [])) == 1 and stale_particles[0].water_time_origins == [stale.get("game_time")]
          and any(line.startswith(f"s03: water_flow_origin: MPC 매개변수 없음: {origin_name}") for line in stale_state["key_errors"])
          and missing.get("value") is None and missing.get("actors") == [] and any(line.startswith(f"s04: water_flow_origin: MPC 없음: {collection_path}") for line in missing_state["key_errors"])
          and unbuilt.get("value") == unbuilt.get("game_time") and unbuilt.get("actors") == [] and unbuilt_particles[0].water_time_origins == []
          and unbuilt_state["key_errors"] == ["s05: water_flow_origin: TDGen_UnbuiltParticles: set_water_time_origin_seconds 없음(C++ 빌드 전)"]
          and "water_flow_origin" not in dry and dry_particles[0].water_time_origins == [] and not dry_state["key_errors"],
          (origins, reference_times, final_value, [particle.water_time_origins for particle in particles], stray_keys, state["key_errors"], lite, lite_state["key_errors"],
           stale, stale_state["key_errors"], missing, missing_state["key_errors"], unbuilt, unbuilt_state["key_errors"], dry.get("water_flow_origin"), dry_state["key_errors"]))

    look = copy.deepcopy(WORLD.look_table)
    clean_table = fluid_material_look.TDMaterialLookTable(copy.deepcopy(look), "fixture")
    base_driven = water_material.find_collection_driven_overrides(clean_table)
    base_shadowed = water_material.find_shadowed_mid_parameters(clean_table)
    look["Materials"][water_material.asset_path("MI_TD_FluidWater_Lake")]["Scalars"][origin_name] = 5.0
    injected_table = fluid_material_look.TDMaterialLookTable(look, "fixture")
    injected_driven = water_material.find_collection_driven_overrides(injected_table)
    injected_shadowed = water_material.find_shadowed_mid_parameters(injected_table)
    check("F15b 물 재질 스크립트: 모양 정본 MI 가 룩데브 구동 MPC 이름 WaterFlowOriginSeconds 를 덮으면 '룩데브 구동 MPC 이름' 목록에 그 이름(C++ 구동 MID 목록은 그대로), 정본 그대로는 0건",
          base_driven == [] and injected_driven == [origin_name] and injected_shadowed == base_shadowed,
          (base_driven, injected_driven, base_shadowed, injected_shadowed))

    links, create_expression, connect = record_material_graph()
    flow_collection = FakeMaterialParameterCollection(collection_path, scalar_names)
    with patched(FakeMaterialEditingLibrary, create_material_expression=create_expression, connect_material_expressions=connect):
        water_factory = water_material.SourceFactory(None, {}, False, water_material.LEGACY_WATER_PATH, flow_collection)
        water_elapsed = water_factory.get("ElapsedSeconds")[0]
        water_elapsed_again = water_factory.get("ElapsedSeconds")[0]
        lite_elapsed = water_material.SourceFactory(None, {}, True, water_material.LEGACY_WATER_PATH, flow_collection).get("ElapsedSeconds")[0]
        try:
            water_material.SourceFactory(None, {}, False).get("ElapsedSeconds")
            refused_without_collection = False
        except RuntimeError as error:
            refused_without_collection = str(error).startswith("ElapsedSeconds 원점 MPC 없음")
    flow_saves = []
    existing_collection = FakeMaterialParameterCollection(collection_path, [])
    with patched(unreal, EditorAssetLibrary=Props(does_asset_exist=lambda path: path == collection_path, load_asset=lambda path: existing_collection), **collection_parameter_types()), \
            patched(water_material, save_asset=lambda asset, path: flow_saves.append(path)):
        built_collection = water_material.load_or_create_flow_collection(True)
        try:
            water_material.load_or_create_flow_collection(False)
            preserved = False
        except RuntimeError as error:
            preserved = str(error).startswith("기존 에셋 보존")
    check("F15c 물 마스터 그래프: M_TD_FluidWater·Lite ElapsedSeconds 원천 = Subtract(A Time, B CollectionParameter MPC_TD_FluidWaterFlow.WaterFlowOriginSeconds), 표면·정점 오프셋 노드가 한 원천을 공유(캐시), 원점 MPC 없이 ElapsedSeconds 를 만들면 거부 / 흐름 MPC 재사용은 overwrite 때만 스칼라 {WaterFlowOriginSeconds: 0} 로 쓰고 저장, overwrite 없으면 '기존 에셋 보존' / 대상 에셋 목록에 MPC / 물 HLSL 에 직접 시간 기호(View.GameTime·RealTime) 없음",
          is_origin_subtract(water_elapsed, links, flow_collection, origin_name) and water_elapsed_again is water_elapsed and is_origin_subtract(lite_elapsed, links, flow_collection, origin_name)
          and refused_without_collection and water_material.describe_elapsed_seconds() == f"Time - MPC_TD_FluidWaterFlow.{origin_name}"
          and built_collection is existing_collection and stored_collection_scalars(existing_collection) == list(water_material.COLLECTION_SCALAR_DEFAULTS)
          and flow_saves == [collection_path] and preserved and collection_path in water_material.collect_target_asset_paths()
          and not any(symbol in (W / "Tools" / "FluidFX" / "Shaders" / name).read_text(encoding="utf-8") for symbol in ("GameTime", "RealTime", "View.Time")
                      for name in (water_material.COMMON_HLSL_NAME, water_material.SURFACE_HLSL_NAME, water_material.OFFSET_HLSL_NAME, water_material.LOD_MORPH_HLSL_NAME, water_material.TILE_MAP_HLSL_NAME)),
          ([(source.get_name(), output, target.get_name(), target_input) for source, output, target, target_input in links], refused_without_collection, flow_saves, preserved,
           stored_collection_scalars(existing_collection)))


class FakeUnrealLog(io.BufferedIOBase):
    def __init__(self):
        self.entries = []

    def writable(self):
        return True

    def write(self, data):
        text = bytes(data).decode("utf-8")
        if text.strip():
            self.entries.append(text)
        return len(data)


def run_code_through_unreal_log(code, timeout=12, try_enable=True):
    log = FakeUnrealLog()
    stream = io.TextIOWrapper(log, encoding="utf-8", line_buffering=True)
    with contextlib.redirect_stdout(stream):
        exec(compile(code, "<editor>", "exec"), {"__name__": "editor"})
        stream.flush()
    return {"success": True, "lines": [f"[Info] {entry}" for entry in log.entries], "connected": True}


def check_capture_tool_fixes():
    output_dir = TEST_DIR / "capture-fixes"
    output_dir.mkdir(parents=True, exist_ok=True)
    lookdev.OUTPUT_DIRECTORY = output_dir
    lookdev.REMOTE_RESULT_DIRECTORY = output_dir / "remote_results"
    marker = pie_showcase_fluid.RESULT_MARKER
    big_grid = {"grid": [[0.123456789012] * 41] * 41}
    old_style = run_code_through_unreal_log(f"import json\nprint({marker!r}, json.dumps({big_grid!r}))\n")["lines"]
    try:
        old_style[0][old_style[0].find(marker) + len(marker):].strip().splitlines()[0]
        old_parser_error = None
    except IndexError as error:
        old_parser_error = error
    check("C1 원인: UE stdout(TextIOWrapper 8192자 덩어리, unreal_core._Logger 가 write 마다 로그 1항목)에서 큰 결과는 표식과 본문이 다른 출력 항목으로 갈라져 옛 파서 IndexError / 새 파서는 뒤 항목까지 모아 해석",
          len(old_style) == 2 and old_style[0].strip() == "[Info] " + marker and isinstance(old_parser_error, IndexError) and lookdev.parse_editor_result(old_style) == big_grid,
          ([line[:60] for line in old_style], old_parser_error))
    broken = [lookdev.parse_editor_result(lines) for lines in (["[Info] " + marker + " "], ["[Info] " + marker + ' {"a": 1'], ["[Info] " + marker + " [1, 2]"], ["[Info] other"])]
    check("C2 표식 뒤가 비었거나 잘림·객체 아님·표식 없음 → 예외 없이 error", all(set(item) == {"error"} for item in broken), broken)

    reset_world([])
    WORLD.actors.append(FakeActor("TDGen_CloudPainter", get_density_pressure_texture=lambda: "RT_Density"))
    rt_entry = {"label": "TDGen_CloudPainter", "function": "get_density_pressure_texture", "box": [0.0, 0.0, 1.0, 1.0], "step": 0.025, "channel": "r", "prefix": "density"}
    reader = Props(read_render_target_raw_uv=lambda world, render_target, u, v: FakeLinearColor(u * 0.3711 + v * 0.1234567, v, 0.0, 1.0))
    captured = []

    def capturing_run_code(code, **kwargs):
        outcome = run_code_through_unreal_log(code)
        captured.append(outcome["lines"])
        return outcome

    with patched(unreal, RenderingLibrary=reader), patched(lookdev.run_in_editor, run_code=capturing_run_code), patched(lookdev, editor_action=REAL_LOOKDEV_EDITOR_ACTION):
        large = lookdev.editor_action("lookdev_probe", probe={"rt": [rt_entry]}, reference=0.0, at=0.0)
        small = lookdev.editor_action("lookdev_probe", probe={"rt": [dict(rt_entry, step=0.5)]}, reference=0.0, at=0.0)
    grid = large.get("grids", {}).get("density", {})
    leftovers = sorted(lookdev.REMOTE_RESULT_DIRECTORY.glob("*.json")) if lookdev.REMOTE_RESULT_DIRECTORY.exists() else []
    check("C3 큰 탐침 결과(41×41 격자, g4 painter_stroke 꼴) → 에디터가 Saved 아래 파일에 쓰고 표식 뒤엔 경로만, 바깥이 읽고 지움 / 작은 결과는 한 줄 그대로 / 결과 print 치환 대상이 EDITOR_CODE 에 있음",
          not large.get("errors") and len(grid.get("values", [])) == 41 and len(grid["values"][0]) == 41 and "probe.density.mean" in large.get("metrics", {})
          and any('"result_file"' in line for line in captured[0]) and max(len(line) for line in captured[0]) < lookdev.REMOTE_RESULT_INLINE_LIMIT_BYTES
          and len(small.get("grids", {}).get("density", {}).get("values", [])) == 3 and not any('"result_file"' in line for line in captured[1]) and not leftovers
          and lookdev.REMOTE_RESULT_PRINT in pie_showcase_fluid.EDITOR_CODE and lookdev.REMOTE_RESULT_EMIT in lookdev.build_editor_code("lookdev_probe", {}, output_dir / "r.json"),
          (large.get("errors"), [len(line) for line in captured[0]] if captured else None, leftovers))

    def broken_probe_run_code(code, **kwargs):
        if '"action": "lookdev_probe"' in code:
            return {"success": True, "lines": ["[Info] " + marker + " "], "connected": True}
        return run_code_through_unreal_log(code)

    def failing_run_code(code, **kwargs):
        raise RuntimeError("Remote party failed to send a valid response!")

    state = {"uses_tuning_reload": False, "key_errors": []}
    with patched(unreal, RenderingLibrary=reader), patched(lookdev.run_in_editor, run_code=broken_probe_run_code), patched(lookdev, editor_action=REAL_LOOKDEV_EDITOR_ACTION):
        probed = lookdev.apply_variant_with_tuning({"probe": {"rt": [rt_entry], "times": [0.0]}}, "s00", state)
    with patched(lookdev.run_in_editor, run_code=failing_run_code), patched(lookdev, editor_action=REAL_LOOKDEV_EDITOR_ACTION):
        raised = lookdev.editor_action("lookdev_probe", probe={"rt": [rt_entry]}, reference=0.0, at=0.0)
    spilled_without_file = {"success": True, "lines": ["[Info] " + marker + ' {"result_file": "elsewhere.json", "result_bytes": 9}'], "connected": True}
    with patched(lookdev.run_in_editor, run_code=lambda code, **kwargs: spilled_without_file), patched(lookdev, editor_action=REAL_LOOKDEV_EDITOR_ACTION):
        missing_file = lookdev.editor_action("lookdev_probe", probe={"rt": [rt_entry]}, reference=0.0, at=0.0)
    probe_errors = probed.get("probe", {}).get("errors", [])
    check("C4 그래도 실패하면 죽지 않음: 표식 뒤 빈 응답 → 그 표본 errors '결과 해석 실패'·키 오류 's00: probe: …'(단계 무효) / 원격 실행 예외·결과 파일 없음 → error",
          len(probe_errors) == 1 and "결과 해석 실패" in probe_errors[0] and any(line.startswith("s00: probe: 결과 해석 실패") for line in state["key_errors"])
          and raised.get("error", "").startswith("에디터 원격 실행 예외 RuntimeError") and missing_file.get("error", "").startswith("결과 파일 읽기 실패"),
          (probe_errors, state["key_errors"], raised, missing_file))

    reset_world([])
    WORLD.actors.append(FakeActor("TDGen_FoliageFlow_Walk", flow_render_target=None))
    soft_entry = {"label": "TDGen_FoliageFlow_Walk", "property": "flow_render_target", "box": [0.0, 0.0, 1.0, 1.0], "step": 0.5, "channel": "rg_len", "prefix": "flow"}
    unloaded = fake_editor_action("lookdev_probe", probe={"rt": [soft_entry, dict(soft_entry, label="TDGen_Missing", prefix="missing")]}, reference=0.0, at=0.0)
    check("C5 foliage flow_render_target(TSoftObjectPtr) None → '렌더 타깃 없음(속성 None: TSoftObjectPtr …)' / 액터 없음 → '(소유자 없음)', 둘 다 표본 오류",
          len(unloaded.get("errors", [])) == 2 and "TSoftObjectPtr" in unloaded["errors"][0] and unloaded["errors"][1].endswith("렌더 타깃 없음(소유자 없음)"), unloaded)

    source_dir, copy_dir = TEST_DIR / "campfire", TEST_DIR / "capture-stale"
    shutil.rmtree(copy_dir, ignore_errors=True)
    shutil.copytree(source_dir, copy_dir)
    scene_dir = copy_dir / "campfire"
    stale_result = load_json(scene_dir / "result.json")
    calls = []

    def failing_lookdev(spec_path, level, output_dir, run_id):
        calls.append(run_id)
        return 1

    with patched(calib_sweep, run_lookdev=failing_lookdev):
        code = run_sweep(["--plan", CAMPFIRE_PLAN, "--run-dir", str(copy_dir)])
    scene = load_json(copy_dir / "sweep.json")["scenes"][0]
    archived = sorted((copy_dir / calib_sweep.PREVIOUS_OUTPUTS_NAME / "campfire").glob("*/result.json"))
    check("C6 지난 result.json 이 남은 장면에서 룩데브가 새 결과 없이 종료 1 → 실행 전에 지난 산출물을 <run-dir>/_previous/<장면>/<시각>/ 로 옮겨(장면 폴더엔 lookdev_spec.json 만) 옛 결과로 판정하지 않음·장면 error '룩데브 종료 1, 이번 실행 결과 없음'·단계 0·종료 1",
          code == 1 and len(calls) == 1 and str(scene.get("error", "")).startswith("룩데브 종료 1, 이번 실행 결과 없음") and not scene["steps"] and [path.name for path in scene_dir.iterdir()] == [calib_sweep.LOOKDEV_SPEC_NAME]
          and len(archived) == 1 and load_json(archived[0]) == stale_result and (scene_dir / calib_sweep.LOOKDEV_SPEC_NAME).exists() and scene.get("previous_outputs") and scene.get("lookdev_run_id") == calls[0],
          (code, calls, scene.get("error"), archived, scene.get("previous_outputs")))

    def replaying_lookdev(result_run_id, sha256):
        def run(spec_path, level, output_dir, run_id):
            replay = dict(stale_result, run_id=result_run_id or run_id, variants_sha256=sha256 or calib_sweep.file_sha256(spec_path))
            (Path(output_dir) / "result.json").write_text(json.dumps(replay), encoding="utf-8")
            return 0
        return run

    errors = []
    for result_run_id, sha256 in (("old-run", None), (None, "0" * 64)):
        with patched(calib_sweep, run_lookdev=replaying_lookdev(result_run_id, sha256)):
            code = run_sweep(["--plan", CAMPFIRE_PLAN, "--run-dir", str(copy_dir)])
        errors.append((code, load_json(copy_dir / "sweep.json")["scenes"][0].get("error", "")))
    fresh = load_json(source_dir / "sweep.json")["scenes"][0]
    fresh_result = load_json(source_dir / "campfire" / "result.json")
    check("C7 새 result.json 의 run_id 가 이번 실행과 다름 → '이번 실행 것이 아니다'·variants_sha256 가 이번 lookdev_spec 과 다름 → 무효(종료 1) / 정상 실행은 run_id·variants_sha256·started_at 일치",
          [code for code, _ in errors] == [1, 1] and "이번 실행 것이 아니다" in errors[0][1] and "variants_sha256" in errors[1][1]
          and fresh.get("lookdev_run_id") and fresh_result.get("run_id") == fresh["lookdev_run_id"] and fresh_result.get("started_at")
          and fresh_result.get("variants_sha256") == calib_sweep.file_sha256(source_dir / "campfire" / calib_sweep.LOOKDEV_SPEC_NAME),
          (errors, fresh.get("lookdev_run_id"), fresh_result.get("run_id")))


def check_pie_session_fixes():
    output_dir = TEST_DIR / "pie-session"
    output_dir.mkdir(parents=True, exist_ok=True)
    spec_path = output_dir / "spec.json"
    spec_path.write_text(json.dumps({"base": {}, "variants": []}), encoding="utf-8")
    reset_world([])
    calls = []
    pie = {"running": True}

    def fake_wait(running, timeout):
        return pie["running"] is running

    def fake_stop(session, environment):
        calls.append("stop")
        pie["running"] = False
        return True

    def fake_start(session, environment):
        calls.append("start")
        pie["running"] = True
        return True

    def fake_open(level):
        calls.append("open")
        return {"loaded": True}

    def run(extra, capture=None):
        calls.clear()
        pie["running"] = True
        saved_argv = sys.argv
        sys.argv = ["pie_lookdev_fluid.py", "--variants", str(spec_path), "--level", "/Game/Test/LV", "--output-dir", str(output_dir), "--stop-pie"] + extra
        replacements = {"capture_variants": capture} if capture else {}
        try:
            with patched(lookdev.pie_check_fluid, wait_for_pie_state=fake_wait, stop_pie=fake_stop, start_pie=fake_start, wait_for_editor_navigation=lambda: True), \
                    patched(lookdev.pie_showcase_fluid, open_stage_level=fake_open), patched(lookdev, **replacements):
                return lookdev.main(), list(calls)
        except RuntimeError as error:
            return str(error), list(calls)
        finally:
            sys.argv = saved_argv

    fresh_code, fresh_calls = run(["--fresh-pie"])
    fresh_result = load_json(output_dir / "result.json")
    check("P1 --fresh-pie: 앞 실행이 남긴 PIE 를 먼저 멈추고 --level 을 새로 열어 PIE 시작, result.stopped_leftover_pie 참, 끝에 --stop-pie 로 멈춤",
          fresh_code == 0 and fresh_calls == ["stop", "open", "start", "stop"] and fresh_result.get("stopped_leftover_pie") is True, (fresh_code, fresh_calls, fresh_result.get("stopped_leftover_pie")))
    reuse_code, reuse_calls = run([])
    check("P2 --fresh-pie 없음: 이미 도는 PIE 를 그대로 쓰는 옛 동작(대화형 룩데브), 끝에 --stop-pie 로 멈춤", reuse_code == 0 and reuse_calls == ["stop"], (reuse_code, reuse_calls))

    def crashing_capture(*args):
        raise RuntimeError("캡처 중 예외")

    crash_code, crash_calls = run(["--fresh-pie"], crashing_capture)
    check("P3 촬영 중 예외가 나도 finally 에서 PIE 를 멈춘다(남은 PIE 가 다음 계획 촬영을 오염시키지 않게)", crash_code == "캡처 중 예외" and crash_calls[-1] == "stop" and not pie["running"], (crash_code, crash_calls))


run_check(check_cal0_b0)
run_check(check_cal0_patch)
run_check(check_cal0_lookdev)
run_check(check_cal0_sweep)
run_check(check_cal0_judge)
run_check(check_cal0_measure)
run_check(check_cal0_derived_chain)
run_check(check_cal0_viscous_measure)
run_check(check_cal0_batch)
run_check(check_cal0_integration)
run_check(check_cal0_real_queue)
run_check(check_lookdev_frame_fixes)
run_check(check_time_origin_fixes)
run_check(check_water_time_origin_fixes)
run_check(check_capture_tool_fixes)
run_check(check_pie_session_fixes)
check("실행 전후 정본 sha256 동일", fluid_tuning_patch.file_sha256(CANONICAL) == canonical_before and fluid_tuning_patch.file_sha256(fluid_tuning_patch.canonical_look_path()) == look_canonical_before)

failed = [name for name, passed in RESULTS if not passed]
print(f"SUMMARY passed={len(RESULTS) - len(failed)}/{len(RESULTS)}")
sys.exit(1 if failed else 0)
