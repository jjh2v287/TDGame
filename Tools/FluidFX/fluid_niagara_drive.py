"""실행 환경: 시스템 Python 3 순수 모듈(unreal·MCP 없음, editor_make_fluid_driven_niagara.py·check_fluid_niagara_specs.py 가 가져온다).
실행 명령: 직접 실행하지 않는다(import fluid_niagara_drive).
출력: 없음. 구조 명세 Tools/FluidFX/niagara_specs/NS_TD_*.json(spec-fx8n 2.2) 검사와, 명세 → User 변수(런타임 12 + 조절 등록표)·STACK_PLAN(입력 단위 HLSL 식, 3절)·시스템/이미터/렌더러 데이터 변환을 준다. 표본 모듈마다 SampledColor 를 고유 입자 속성(SAMPLE_COPY_ATTRIBUTES)으로 연결 복사하고 $D·$A·$V 는 그 속성을 가리킨다(L-editor-62, 한 식이 표본 모듈 출력을 둘 이상 읽지 않음). 조절 등록표는 이름·형·범위만 두고 값은 모양 정본 TDFluidMaterialLook.json NiagaraSystems 에만 둔다(반박 S5).
상태: 2026-10-27 CPU(VM) 질량 격자 위치식은 정수 % 대신 fmod·floor(VM 이 정수 나머지를 거부, NS_TD_FluidMassNdcBench 0b·생성 통과). 2026-10-26 FX-5 수정(미검증, 오프라인 검사만): 대량 입력 profile mass(월드 공간 고정, 충돌 금지 P-25 ⑤, 렌더러 none, 쓰기 splat(TDFluidSplatDataInterface)·ndc(NDC Append)·kill(KillParticlesInVolume 상자, Kill Shape Box 열거 재읽기), 운동 swirl·cone·slide, CPU·GPU)와 등록표 TDMass* 를 더함. 기존 drive·ember 계획은 바이트 그대로. 이전: 2026-10-26 수정(격자 UV 를 Engine.ExecIndex 대신 Particles.UniqueID % 격자수² 로: GPU 식 입력에서 Engine.ExecIndex 가 늘 0 이라 2048개가 한 점에 모였음, sim cache 실측. 수정 뒤 MeshArray 0b·생성·HeightFollowsDensity 통과). 식 접근·SpawnBurst·Collision 은 0b 확정. EmitterState 열거 값은 엔진 Niagara 콘텐츠 Enums 에셋 표시 이름 표 해독으로 확정(Self = ENiagaraEmitterLifeCycleMode::NewEnumerator1, Once = ENiagara_EmitterStateOptions::NewEnumerator1)하고 생성기가 재읽기 표시 이름으로 다시 확인한다.
"""
import json
import pathlib
import re

import fluid_niagara_toolset as toolset

SPEC_KEYS = ("asset", "emitter_name", "profile", "space", "spawn", "features", "renderer", "cast_shadow", "max_particles",
             "fixed_bounds_half_extent_cm", "random_seed")
MASS_SPEC_KEY = "mass"
MASS_KEYS = ("motion", "sim_target", "target_channel_name", "data_channel", "allocation_count")
RENDERER_KEYS = ("kind", "material", "alignment", "mesh", "mesh_facing")
PROFILES = ("drive", "ember", "mass")
SPACES = ("world", "local")
SPAWN_MODES = ("rate_reject", "grid_burst", "rate")
SPAWN_MODES_BY_PROFILE = {"drive": ("rate_reject", "grid_burst"), "ember": ("rate_reject",), "mass": ("rate", "grid_burst")}
FEATURES = ("height", "color", "collision", "splat", "ndc", "kill")
FEATURES_BY_PROFILE = {"drive": ("height", "color"), "ember": ("collision",), "mass": ("splat", "ndc", "kill")}
MASS_WRITE_FEATURES = ("splat", "ndc")
MASS_MOTIONS = ("swirl", "cone", "slide")
MASS_SIM_TARGETS = ("gpu", "cpu")
DATA_CHANNEL_PATTERN = re.compile(r"^/Game/FluidFX/Niagara/DataChannels/NDC_TD_[A-Za-z0-9_]+\.NDC_TD_[A-Za-z0-9_]+$")
RENDERER_KINDS = ("sprite", "mesh", "none")
SPRITE_ALIGNMENTS = ("Unaligned", "VelocityAligned", "CustomAlignment")
MESH_FACINGS = ("Velocity", "Default")
ASSET_ROOT = "/Game/FluidFX/Niagara/"
ASSET_PATTERN = re.compile(r"^/Game/FluidFX/Niagara/NS_TD_[A-Za-z0-9_]+$")
OBJECT_PATTERN = re.compile(r"^/(Game|Engine)/[A-Za-z0-9_/]+\.[A-Za-z0-9_]+$|^/Game/[A-Za-z0-9_/]+$")
NAME_PATTERN = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")

SPRITE_RENDERER_CLASS = "/Script/Niagara.NiagaraSpriteRendererProperties"
MESH_RENDERER_CLASS = "/Script/Niagara.NiagaraMeshRendererProperties"
DEFAULT_TEMPLATE_SYSTEM = "/Niagara/DefaultAssets/DefaultSystem.DefaultSystem"
EMPTY_TEMPLATE_SYSTEM = ""
MINIMAL_EMITTER = "/Niagara/DefaultAssets/Templates/Emitters/Minimal.Minimal"
SAMPLE_TEXTURE_MODULE = "/Niagara/Modules/Update/Textures/SampleTexture.SampleTexture"
SPAWN_RATE_MODULE = "/Niagara/Modules/Emitter/SpawnRate.SpawnRate"
SPAWN_BURST_MODULE = "/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous"
SOLVE_FORCES_MODULE = "/Niagara/Modules/Solvers/SolveForcesAndVelocity.SolveForcesAndVelocity"
COLLISION_MODULE = "/Niagara/Modules/Collision/Collision.Collision"
INITIALIZE_PARTICLE_MODULE = "/Niagara/Modules/Spawn/Initialization/V2/InitializeParticle.InitializeParticle"
RANDOM_RANGE_FLOAT = "/Niagara/DynamicInputs/UniformRange/V2/RandomRangeFloat.RandomRangeFloat"
RANDOM_RANGE_VECTOR2D = "/Niagara/DynamicInputs/UniformRange/V2/RandomRangeVector2D.RandomRangeVector2D"
TEXTURE_DATA_INTERFACE_CLASS = "/Script/Niagara.NiagaraDataInterfaceTexture"
MASS_SPLAT_MODULE = "/Game/FluidFX/Niagara/Modules/NM_TD_FluidMassSplat.NM_TD_FluidMassSplat"
MASS_NDC_APPEND_MODULE = "/Game/FluidFX/Niagara/Modules/NM_TD_FluidNdcAppendPoint.NM_TD_FluidNdcAppendPoint"
KILL_VOLUME_MODULE = "/Niagara/Modules/Update/Lifetime/KillParticlesInVolume.KillParticlesInVolume"
SPLAT_DATA_INTERFACE_CLASS = "/Script/TDGame.TDFluidSplatDataInterface"
NDC_WRITE_DATA_INTERFACE_CLASS = "/Script/Niagara.NiagaraDataInterfaceDataChannelWrite"
MASS_MODULE_INPUTS = ("bExecute", "Position", "Velocity", "RadiusCm", "DensityStrength", "HeatStrength")
KILL_SHAPE_BOX = ("Kill Shape", toolset.KILL_SHAPE_ENUM, "ENiagaraKillVolumeOptions::NewEnumerator1", "Box")
LIFE_CYCLE_SELF = ("Life Cycle Mode", toolset.LIFE_CYCLE_ENUM, "ENiagaraEmitterLifeCycleMode::NewEnumerator1", "Self")
LOOP_BEHAVIOR_ONCE = ("Loop Behavior", toolset.LOOP_BEHAVIOR_ENUM, "ENiagara_EmitterStateOptions::NewEnumerator1", "Once")
SAMPLE_COPY_ATTRIBUTES = {"$D": "Particles.TDDriveSampleDensity", "$A": "Particles.TDDriveSampleAux", "$V": "Particles.TDDriveSampleVelocity"}

EMITTER_UPDATE_SCRIPT = "EmitterUpdateScript"
PARTICLE_SPAWN_SCRIPT = "ParticleSpawnScript"
PARTICLE_UPDATE_SCRIPT = "ParticleUpdateScript"
GPU_COMPUTE_SCRIPT = "ParticleGPUComputeScript"

LOOK_SECTION_BY_TYPE = {
    toolset.FLOAT_TYPE: "Floats",
    toolset.INT_TYPE: "Ints",
    toolset.VECTOR2_TYPE: "Vectors2",
    toolset.VECTOR3_TYPE: "Vectors3",
    toolset.COLOR_TYPE: "Colors",
}
RUNTIME_USER_VARIABLES = (
    ("User.SimCenterWorld", toolset.VECTOR3_TYPE),
    ("User.SimExtentsCm", toolset.VECTOR2_TYPE),
    ("User.DensityPressureTexture", toolset.TEXTURE_CLASS),
    ("User.VelocityTexture", toolset.TEXTURE_CLASS),
    ("User.AuxTexture", toolset.TEXTURE_CLASS),
    ("User.WaterSurfaceTexture", toolset.TEXTURE_CLASS),
    ("User.SimCenterPlane", toolset.VECTOR2_TYPE),
    ("User.PlaneAxisU", toolset.VECTOR3_TYPE),
    ("User.PlaneAxisV", toolset.VECTOR3_TYPE),
    ("User.PlaneAxisN", toolset.VECTOR3_TYPE),
    ("User.PlaneCoordinateOffsetCm", toolset.VECTOR2_TYPE),
    ("User.VelocityReferenceCmPerSec", toolset.FLOAT_TYPE),
)
BAKED_RUNTIME_USER_VARIABLES = tuple(item for item in RUNTIME_USER_VARIABLES if item[1] != toolset.TEXTURE_CLASS)

F = toolset.FLOAT_TYPE
I = toolset.INT_TYPE
V2 = toolset.VECTOR2_TYPE
V3 = toolset.VECTOR3_TYPE
C = toolset.COLOR_TYPE
TUNING_REGISTRY = (
    ("drive", "rate", "User.DriveSpawnRatePerSec", F, 0.0, 2e4),
    ("drive", "rate", "User.DriveSpawnPerM2PerSec", F, 0.0, 1e5),
    ("drive", "rate", "User.DriveSpawnUvMin", V2, 0.0, 1.0),
    ("drive", "rate", "User.DriveSpawnUvMax", V2, 0.0, 1.0),
    ("drive", "rate", "User.DriveSpawnThreshold", F, 0.0, 1.0),
    ("drive", "rate", "User.DriveSpawnWeightPrimary", F, 0.0, 4.0),
    ("drive", "rate", "User.DriveSpawnWeightSelective", F, 0.0, 4.0),
    ("drive", "rate", "User.DriveSpawnWeightHeat", F, 0.0, 4.0),
    ("drive", "rate", "User.DriveSpawnWeightUniform", F, 0.0, 4.0),
    ("drive", "rate", "User.DriveLifetimeMinSeconds", F, 0.05, 1e5),
    ("drive", "grid", "User.DriveGridCount", I, 1, 64),
    ("drive", "always", "User.DriveLifetimeMaxSeconds", F, 0.05, 1e5),
    ("drive", "always", "User.DriveDepthJitterCm", F, 0.0, 200.0),
    ("drive", "always", "User.DriveVelocityFollowScale", F, 0.0, 20.0),
    ("drive", "always", "User.DriveVelocityDragPerSec", F, 0.0, 50.0),
    ("drive", "always", "User.DriveVelocityAccelerationPerSec", F, 0.0, 20.0),
    ("drive", "always", "User.DriveDensityBuoyancyCmPerSec2", F, 0.0, 5000.0),
    ("drive", "always", "User.DriveGravityCmPerSec2", F, 0.0, 2000.0),
    ("drive", "always", "User.DriveSlowKillSpeedCmPerSec", F, 0.0, 500.0),
    ("drive", "always", "User.DriveSlowKillSeconds", F, 0.0, 5.0),
    ("drive", "always", "User.DriveRestSeconds", F, 0.0, 30.0),
    ("drive", "always", "User.DriveStretchPerCmPerSec", F, 0.0, 0.05),
    ("drive", "height", "User.DrivePressureToHeightCm", F, -500.0, 500.0),
    ("drive", "height", "User.DriveDensityToHeightCm", F, -500.0, 500.0),
    ("drive", "height", "User.DriveHeightOffsetCm", F, -1000.0, 1000.0),
    ("drive", "height", "User.DriveHeightFollowPerSec", F, 0.0, 60.0),
    ("drive", "color", "User.DriveDensityToColor", F, 0.0, 4.0),
    ("drive", "color", "User.DriveDensityToAlpha", F, 0.0, 4.0),
    ("drive", "color", "User.DriveFadePerSec", F, 0.0, 60.0),
    ("drive", "color", "User.DriveColorLow", C, 0.0, 64.0),
    ("drive", "color", "User.DriveColorHigh", C, 0.0, 64.0),
    ("drive", "sprite", "User.DriveSpriteSizeCm", F, 0.1, 500.0),
    ("drive", "mesh", "User.DriveMeshScale", F, 0.001, 10.0),
    ("ember", "always", "User.EmberSpawnPerHeatM2PerSec", F, 0.0, 1e5),
    ("ember", "always", "User.EmberSpawnUvMin", V2, 0.0, 1.0),
    ("ember", "always", "User.EmberSpawnUvMax", V2, 0.0, 1.0),
    ("ember", "always", "User.EmberSpawnThreshold", F, 0.0, 1.0),
    ("ember", "always", "User.EmberDensitySpawnWeight", F, 0.0, 4.0),
    ("ember", "always", "User.EmberSpawnHeatFloor", F, 0.0, 1.0),
    ("ember", "always", "User.EmberLifetimeMinSeconds", F, 0.05, 30.0),
    ("ember", "always", "User.EmberLifetimeMaxSeconds", F, 0.05, 30.0),
    ("ember", "always", "User.EmberDragPerSec", F, 0.0, 50.0),
    ("ember", "always", "User.EmberBuoyancyCmPerSec2", F, 0.0, 5000.0),
    ("ember", "always", "User.EmberGravityCmPerSec2", F, 0.0, 2000.0),
    ("ember", "always", "User.EmberAmbientHeatBuoyancyCmPerSec2", F, 0.0, 5000.0),
    ("ember", "always", "User.EmberCoolingPerSec", F, 0.0, 20.0),
    ("ember", "always", "User.EmberSlowKillSpeedCmPerSec", F, 0.0, 500.0),
    ("ember", "always", "User.EmberSlowKillSeconds", F, 0.0, 5.0),
    ("ember", "always", "User.EmberGroundRestSeconds", F, 0.0, 30.0),
    ("ember", "always", "User.EmberFadePerSec", F, 0.0, 60.0),
    ("ember", "always", "User.EmberBaseScale", F, 0.001, 1.0),
    ("ember", "always", "User.EmberStretchPerCmPerSec", F, 0.0, 0.05),
    ("ember", "always", "User.EmberDepthJitterCm", F, 0.0, 200.0),
    ("ember", "collision", "User.EmberCollisionRestitution", F, 0.0, 1.0),
    ("ember", "collision", "User.EmberCollisionFriction", F, 0.0, 1.0),
    ("mass", "always", "User.TDMassRadiusCm", F, 1.0, 1000.0),
    ("mass", "always", "User.TDMassDensity", F, 0.0, 4.0),
    ("mass", "always", "User.TDMassFadeWithAge", F, 0.0, 1.0),
    ("mass", "always", "User.TDMassHeat", F, 0.0, 4.0),
    ("mass", "always", "User.TDMassLifetimeSeconds", F, 0.05, 1e5),
    ("mass", "grid", "User.TDMassBurstGridCount", I, 1, 128),
    ("mass", "grid", "User.TDMassGridSpacingCm", F, 1.0, 1000.0),
    ("mass", "rate", "User.TDMassSpawnRatePerSec", F, 0.0, 1e5),
    ("mass", "swirl", "User.TDMassSwirlCmPerSec", F, 0.0, 5000.0),
    ("mass", "cone", "User.TDMassConeAngleDegrees", F, 0.0, 89.0),
    ("mass", "cone", "User.TDMassSpeedCmPerSec", F, 0.0, 1e4),
    ("mass", "cone", "User.TDMassDragPerSec", F, 0.0, 50.0),
    ("mass", "cone", "User.TDMassBuoyancyCmPerSec2", F, -5000.0, 5000.0),
    ("mass", "slide", "User.TDMassBoxExtentCm", V3, 0.0, 1e5),
    ("mass", "slide", "User.TDMassSlopeDirection", V3, -1.0, 1.0),
    ("mass", "slide", "User.TDMassSlideAccelerationCmPerSec2", F, 0.0, 1e4),
    ("mass", "kill", "User.TDMassKillBoxCenter", V3, -1e7, 1e7),
    ("mass", "kill", "User.TDMassKillBoxSizeCm", V3, 0.0, 1e5),
)
ALLOWED_ENGINE_NAMES = ("Engine.DeltaTime", "Engine.ExecIndex", "Engine.Owner.SystemLocalToWorld", "Engine.Owner.SystemWorldToLocal")
ALLOWED_PARTICLE_NAMES = ("Position", "Velocity", "Lifetime", "Age", "Color", "SpriteSize", "Scale", "DynamicMaterialParameter", "UniqueID")
TOKEN_PATTERN = re.compile(r"(?<![A-Za-z0-9_])(User|Particles|Engine)\.([A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)")
PLACEHOLDERS = ("$D", "$V", "$A")


def read_spec(path):
    return json.loads(pathlib.Path(path).read_text(encoding="utf-8"))


def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def find_spec_problems(spec):
    if not isinstance(spec, dict):
        return ["명세 최상위는 객체여야 함"]
    problems = [f"모르는 키 {key!r}" for key in spec if key not in SPEC_KEYS and key != MASS_SPEC_KEY]
    problems.extend(f"키 없음 {key!r}" for key in SPEC_KEYS if key not in spec)
    if problems:
        return problems
    if not isinstance(spec["asset"], str) or not ASSET_PATTERN.match(spec["asset"]):
        problems.append(f"asset: {ASSET_ROOT}NS_TD_* 패키지 경로여야 함, 값 {spec['asset']!r}")
    if not isinstance(spec["emitter_name"], str) or not NAME_PATTERN.match(spec["emitter_name"]):
        problems.append("emitter_name: 영문 식별자여야 함")
    for key, allowed in (("profile", PROFILES), ("space", SPACES), ("spawn", SPAWN_MODES)):
        if spec[key] not in allowed:
            problems.append(f"{key}: {'|'.join(allowed)} 중 하나여야 함, 값 {spec[key]!r}")
    features = spec["features"]
    if not isinstance(features, list) or len(set(map(str, features))) != len(features):
        problems.append("features: 중복 없는 목록이어야 함")
        features = []
    problems.extend(f"features: 모르는 기능 {item!r}" for item in features if item not in FEATURES)
    allowed_features = FEATURES_BY_PROFILE.get(spec["profile"], ())
    problems.extend(f"features: {item!r} 는 profile {spec['profile']} 에서 쓸 수 없음" for item in features if item in FEATURES and item not in allowed_features)
    if spec["profile"] in SPAWN_MODES_BY_PROFILE and spec["spawn"] in SPAWN_MODES and spec["spawn"] not in SPAWN_MODES_BY_PROFILE[spec["profile"]]:
        problems.append(f"spawn: profile {spec['profile']} 는 {'|'.join(SPAWN_MODES_BY_PROFILE[spec['profile']])} 만 허용")
    if spec["profile"] in ("ember", "mass") and spec["space"] != "world":
        problems.append(f"space: profile {spec['profile']} 는 world 에서만 허용")
    problems.extend(find_mass_problems(spec, features))
    if not isinstance(spec["cast_shadow"], bool):
        problems.append("cast_shadow: true|false")
    if not isinstance(spec["max_particles"], int) or isinstance(spec["max_particles"], bool) or not 1 <= spec["max_particles"] <= 65536:
        problems.append("max_particles: 1~65536 정수")
    if not is_number(spec["fixed_bounds_half_extent_cm"]) or not 1.0 <= spec["fixed_bounds_half_extent_cm"] <= 100000.0:
        problems.append("fixed_bounds_half_extent_cm: 1~100000 수")
    if not isinstance(spec["random_seed"], int) or isinstance(spec["random_seed"], bool) or spec["random_seed"] < 0:
        problems.append("random_seed: 0 이상 정수")
    problems.extend(find_renderer_problems(spec["renderer"]))
    if isinstance(spec["renderer"], dict) and spec["renderer"].get("kind") == "none" and spec["profile"] != "mass":
        problems.append("renderer.kind: none 은 profile mass 에서만 허용")
    return problems


def is_mass(spec):
    return spec["profile"] == "mass"


def find_mass_problems(spec, features):
    if not is_mass(spec):
        return ["mass: profile mass 에서만 쓰는 키"] if MASS_SPEC_KEY in spec else []
    mass = spec.get(MASS_SPEC_KEY)
    if not isinstance(mass, dict):
        return ["mass: profile mass 는 mass 객체가 필수"]
    problems = [f"mass: 모르는 키 {key!r}" for key in mass if key not in MASS_KEYS]
    problems.extend(f"mass: 키 없음 {key!r}" for key in MASS_KEYS if key not in mass)
    if problems:
        return problems
    if "collision" in features:
        problems.append("features: profile mass 는 충돌 모듈 금지(P-25 ⑤)")
    if not any(item in features for item in MASS_WRITE_FEATURES):
        problems.append("features: profile mass 는 쓰기 모듈(splat|ndc)이 하나 이상 필요")
    if mass["motion"] not in MASS_MOTIONS:
        problems.append(f"mass.motion: {'|'.join(MASS_MOTIONS)} 중 하나여야 함")
    if mass["sim_target"] not in MASS_SIM_TARGETS:
        problems.append(f"mass.sim_target: {'|'.join(MASS_SIM_TARGETS)} 중 하나여야 함")
    if "splat" in features and mass["sim_target"] != "gpu":
        problems.append("mass.sim_target: splat 은 GPU 전용 데이터 인터페이스라 gpu 여야 함")
    if not isinstance(mass["target_channel_name"], str) or not NAME_PATTERN.match(mass["target_channel_name"]):
        problems.append("mass.target_channel_name: 영문 식별자여야 함")
    if "ndc" not in features:
        if mass["data_channel"] is not None or mass["allocation_count"] is not None:
            problems.append("mass: ndc 가 없으면 data_channel·allocation_count 는 null")
        return problems
    if not isinstance(mass["data_channel"], str) or not DATA_CHANNEL_PATTERN.match(mass["data_channel"]):
        problems.append("mass.data_channel: /Game/FluidFX/Niagara/DataChannels/NDC_TD_*.NDC_TD_* 객체 경로여야 함(R5-M4)")
    count = mass["allocation_count"]
    max_particles = spec["max_particles"] if isinstance(spec["max_particles"], int) else 65536
    if not isinstance(count, int) or isinstance(count, bool) or not max_particles <= count <= 65536:
        problems.append("mass.allocation_count: max_particles 이상 65536 이하 정수(S5)")
    return problems


def find_renderer_problems(renderer):
    if not isinstance(renderer, dict):
        return ["renderer: 객체여야 함"]
    problems = [f"renderer: 모르는 키 {key!r}" for key in renderer if key not in RENDERER_KEYS]
    problems.extend(f"renderer: 키 없음 {key!r}" for key in RENDERER_KEYS if key not in renderer)
    if problems:
        return problems
    if renderer["kind"] not in RENDERER_KINDS:
        return [f"renderer.kind: {'|'.join(RENDERER_KINDS)} 중 하나여야 함"]
    if renderer["kind"] == "none":
        if any(renderer[key] is not None for key in RENDERER_KEYS if key != "kind"):
            return ["renderer: none 은 material·alignment·mesh·mesh_facing 이 null 이어야 함"]
        return []
    if not isinstance(renderer["material"], str) or not OBJECT_PATTERN.match(renderer["material"]):
        problems.append("renderer.material: /Game 패키지 경로여야 함")
    if renderer["kind"] == "sprite":
        if renderer["alignment"] not in SPRITE_ALIGNMENTS:
            problems.append(f"renderer.alignment: {'|'.join(SPRITE_ALIGNMENTS)}")
        if renderer["mesh"] is not None or renderer["mesh_facing"] is not None:
            problems.append("renderer: sprite 는 mesh·mesh_facing 이 null 이어야 함")
        return problems
    if not isinstance(renderer["mesh"], str) or not OBJECT_PATTERN.match(renderer["mesh"]):
        problems.append("renderer.mesh: mesh 렌더러는 메시 경로가 필수")
    if renderer["mesh_facing"] not in MESH_FACINGS:
        problems.append(f"renderer.mesh_facing: {'|'.join(MESH_FACINGS)}")
    if renderer["alignment"] is not None:
        problems.append("renderer.alignment: mesh 는 null 이어야 함")
    return problems


def spec_conditions(spec):
    conditions = {"always", spec["renderer"]["kind"]}
    conditions.add("rate" if spec["spawn"] in ("rate_reject", "rate") else "grid")
    conditions.update(spec["features"])
    if is_mass(spec):
        conditions.add(spec[MASS_SPEC_KEY]["motion"])
    return conditions


def select_tuning_variables(spec):
    conditions = spec_conditions(spec)
    return [(name, type_path, low, high) for profile, condition, name, type_path, low, high in TUNING_REGISTRY
            if profile == spec["profile"] and condition in conditions]


def build_user_variable_plan(spec):
    runtime = [] if is_mass(spec) else list(RUNTIME_USER_VARIABLES)
    return runtime + [(name, type_path) for name, type_path, _, _ in select_tuning_variables(spec)]


def required_look_names(spec):
    return [(name, type_path) for name, type_path in build_user_variable_plan(spec) if type_path != toolset.TEXTURE_CLASS]


def is_local(spec):
    return spec["space"] == "local"


def world_position(spec):
    return "mul(float4(Particles.Position, 1.0), Engine.Owner.SystemLocalToWorld).xyz" if is_local(spec) else "Particles.Position"


def world_velocity(spec):
    return "mul(float4(Particles.Velocity, 0.0), Engine.Owner.SystemLocalToWorld).xyz" if is_local(spec) else "Particles.Velocity"


def to_simulation_position(spec, expression):
    return f"mul(float4({expression}, 1.0), Engine.Owner.SystemWorldToLocal).xyz" if is_local(spec) else expression


def to_simulation_velocity(spec, expression):
    return f"mul(float4({expression}, 0.0), Engine.Owner.SystemWorldToLocal).xyz" if is_local(spec) else expression


def plane_uv_expression(spec):
    position = world_position(spec)
    return (f"saturate((float2(dot({position}, User.PlaneAxisU), dot({position}, User.PlaneAxisV))"
            " + User.PlaneCoordinateOffsetCm - User.SimCenterPlane) / max(User.SimExtentsCm, float2(1.0, 1.0)) + 0.5)")


FLUID_VELOCITY = "((User.PlaneAxisU * ($V.r * 2.0 - 1.0) + User.PlaneAxisV * ($V.g * 2.0 - 1.0)) * User.VelocityReferenceCmPerSec)"


def prefix_of(spec):
    return "Drive" if spec["profile"] == "drive" else "Ember"


def spawn_rate_expression(spec):
    prefix = prefix_of(spec)
    area = (f"User.SimExtentsCm.x * User.SimExtentsCm.y * (User.{prefix}SpawnUvMax.x - User.{prefix}SpawnUvMin.x)"
            f" * (User.{prefix}SpawnUvMax.y - User.{prefix}SpawnUvMin.y) * 0.0001")
    if spec["profile"] == "ember":
        return f"User.EmberSpawnPerHeatM2PerSec * {area}"
    return f"User.DriveSpawnRatePerSec + User.DriveSpawnPerM2PerSec * {area}"


def emitter_spawn_steps(spec):
    if spec["spawn"] == "grid_burst":
        return [
            {"script": EMITTER_UPDATE_SCRIPT, "kind": "state_enums", "label": "loop_once", "module_fragment": "EmitterState",
             "enums": (LIFE_CYCLE_SELF, LOOP_BEHAVIOR_ONCE)},
            {"script": EMITTER_UPDATE_SCRIPT, "kind": "expression_module", "label": "spawn_burst", "module": SPAWN_BURST_MODULE,
             "input": "Spawn Count", "expression": "User.DriveGridCount * User.DriveGridCount"},
        ]
    return [{"script": EMITTER_UPDATE_SCRIPT, "kind": "expression_module", "label": "spawn_rate", "module": SPAWN_RATE_MODULE,
             "input": "SpawnRate", "expression": spawn_rate_expression(spec)}]


def spawn_uv_expression(spec):
    if spec["spawn"] == "grid_burst":
        count = "max(User.DriveGridCount, 1)"
        index = f"(Particles.UniqueID % ({count} * {count}))"
        return f"(float2({index} % {count}, {index} / {count}) + 0.5) / {count}"
    prefix = prefix_of(spec)
    return f"lerp(User.{prefix}SpawnUvMin, User.{prefix}SpawnUvMax, Particles.TDDriveSpawnRoll)"


def spawn_position_expression(spec):
    prefix = prefix_of(spec)
    world = ("User.SimCenterWorld + User.PlaneAxisU * ((Particles.TDDriveSpawnUv.x - 0.5) * User.SimExtentsCm.x)"
             " + User.PlaneAxisV * ((Particles.TDDriveSpawnUv.y - 0.5) * User.SimExtentsCm.y)"
             f" + User.PlaneAxisN * (User.{prefix}DepthJitterCm * Particles.TDDriveDepthRoll)")
    return to_simulation_position(spec, world)


def spawn_lifetime_expression(spec):
    if spec["spawn"] == "grid_burst":
        return "User.DriveLifetimeMaxSeconds"
    prefix = prefix_of(spec)
    return f"lerp(User.{prefix}LifetimeMinSeconds, User.{prefix}LifetimeMaxSeconds, Particles.TDDriveLifeRoll)"


def shape_entry(spec, is_spawn):
    stretch_factor = "1.0" if is_spawn else f"(1.0 + User.{prefix_of(spec)}StretchPerCmPerSec * length(Particles.Velocity))"
    if spec["profile"] == "ember":
        return ("Particles.Scale", toolset.VECTOR3_TYPE, "expression",
                f"float3(User.EmberBaseScale * {stretch_factor}, User.EmberBaseScale, User.EmberBaseScale)")
    if spec["renderer"]["kind"] == "sprite":
        return ("Particles.SpriteSize", toolset.VECTOR2_TYPE, "expression", f"float2(User.DriveSpriteSizeCm, User.DriveSpriteSizeCm * {stretch_factor})")
    return ("Particles.Scale", toolset.VECTOR3_TYPE, "expression", f"float3(User.DriveMeshScale * {stretch_factor}, User.DriveMeshScale, User.DriveMeshScale)")


def sample_steps(script, label, texture, placeholder):
    return [
        {"script": script, "kind": "sample", "label": label, "texture": texture, "placeholder": placeholder},
        set_step(script, label + "_copy", (SAMPLE_COPY_ATTRIBUTES[placeholder], toolset.COLOR_TYPE, "sample_link", placeholder)),
    ]


def set_step(script, label, *entries):
    return {"script": script, "kind": "set", "label": label, "entries": tuple(entries)}


def spawn_steps(spec):
    is_ember = spec["profile"] == "ember"
    is_rate = spec["spawn"] == "rate_reject"
    place_entries = [
        ("Particles.Position", toolset.POSITION_TYPE, "expression", spawn_position_expression(spec)),
        ("Particles.Lifetime", toolset.FLOAT_TYPE, "expression", spawn_lifetime_expression(spec)),
        ("Particles.TDDriveSlowSeconds", toolset.FLOAT_TYPE, "value", 0.0),
        shape_entry(spec, True),
    ]
    if is_ember:
        place_entries.append(("Particles.TDEmberHeat", toolset.FLOAT_TYPE, "value", 1.0))
        place_entries.append(("Particles.DynamicMaterialParameter", toolset.VECTOR4_TYPE, "value", (1.0, 1.0, 0.0, 0.0)))
    else:
        place_entries.append(("Particles.Color", toolset.COLOR_TYPE, "value", (1.0, 1.0, 1.0, 1.0)))
    steps = [
        set_step(PARTICLE_SPAWN_SCRIPT, "spawn_random",
                 ("Particles.TDDriveSpawnRoll", toolset.VECTOR2_TYPE, "random", (RANDOM_RANGE_VECTOR2D, (0.0, 0.0), (1.0, 1.0))),
                 ("Particles.TDDriveLifeRoll", toolset.FLOAT_TYPE, "random", (RANDOM_RANGE_FLOAT, 0.0, 1.0)),
                 ("Particles.TDDriveRejectRoll", toolset.FLOAT_TYPE, "random", (RANDOM_RANGE_FLOAT, 0.0, 1.0)),
                 ("Particles.TDDriveDepthRoll", toolset.FLOAT_TYPE, "random", (RANDOM_RANGE_FLOAT, -1.0, 1.0))),
        set_step(PARTICLE_SPAWN_SCRIPT, "spawn_uv", ("Particles.TDDriveSpawnUv", toolset.VECTOR2_TYPE, "expression", spawn_uv_expression(spec))),
        set_step(PARTICLE_SPAWN_SCRIPT, "spawn_place", *place_entries),
    ]
    if not is_rate:
        return steps
    steps.append(set_step(PARTICLE_SPAWN_SCRIPT, "spawn_plane_uv", (toolset.PLANE_UV_ATTRIBUTE, toolset.VECTOR2_TYPE, "expression", plane_uv_expression(spec))))
    steps.extend(sample_steps(PARTICLE_SPAWN_SCRIPT, "spawn_density_sample", "User.DensityPressureTexture", "$D"))
    steps.extend(sample_steps(PARTICLE_SPAWN_SCRIPT, "spawn_aux_sample", "User.AuxTexture", "$A"))
    if is_ember:
        signal = "max($A.r, max($D.r, $D.b) * User.EmberDensitySpawnWeight)"
    else:
        signal = ("max(max($D.r * User.DriveSpawnWeightPrimary, $D.b * User.DriveSpawnWeightSelective),"
                  " max($A.r * User.DriveSpawnWeightHeat, User.DriveSpawnWeightUniform))")
    threshold = f"User.{prefix_of(spec)}SpawnThreshold"
    weight_entries = [("Particles.TDDriveSpawnWeight", toolset.FLOAT_TYPE, "expression",
                       f"saturate((Particles.TDDriveSpawnSignal - {threshold}) / max(1.0 - {threshold}, 0.001)) * $D.a")]
    if is_ember:
        weight_entries.append(("Particles.TDEmberHeat", toolset.FLOAT_TYPE, "expression",
                               "lerp(User.EmberSpawnHeatFloor, 1.0, saturate(Particles.TDDriveSpawnSignal))"))
    steps.append(set_step(PARTICLE_SPAWN_SCRIPT, "spawn_signal", ("Particles.TDDriveSpawnSignal", toolset.FLOAT_TYPE, "expression", signal)))
    steps.append(set_step(PARTICLE_SPAWN_SCRIPT, "spawn_weight", *weight_entries))
    steps.append(set_step(PARTICLE_SPAWN_SCRIPT, "spawn_reject", ("Particles.Lifetime", toolset.FLOAT_TYPE, "expression",
                                                                  "Particles.TDDriveRejectRoll < Particles.TDDriveSpawnWeight ? Particles.Lifetime : 0.0")))
    return steps


def drive_motion_expression(spec):
    velocity = world_velocity(spec)
    world = (f"lerp({velocity}, {FLUID_VELOCITY} * User.DriveVelocityFollowScale, 1.0 - exp(-User.DriveVelocityDragPerSec * Engine.DeltaTime))"
             f" + {FLUID_VELOCITY} * (User.DriveVelocityAccelerationPerSec * Engine.DeltaTime)"
             " + float3(0.0, 0.0, (User.DriveDensityBuoyancyCmPerSec2 * saturate($D.r + $D.b) - User.DriveGravityCmPerSec2) * Engine.DeltaTime)")
    return to_simulation_velocity(spec, world)


def drive_height_expression(spec):
    velocity = world_velocity(spec)
    position = world_position(spec)
    target = ("(dot(User.SimCenterWorld, User.PlaneAxisN) + User.DriveHeightOffsetCm + ($D.g * 2.0 - 1.0) * User.DrivePressureToHeightCm"
              " + $D.r * User.DriveDensityToHeightCm)")
    world = (f"{velocity} + User.PlaneAxisN * (User.DriveHeightFollowPerSec > 0.0 ? ({target} - dot({position}, User.PlaneAxisN))"
             f" * User.DriveHeightFollowPerSec - dot({velocity}, User.PlaneAxisN) : 0.0)")
    return to_simulation_velocity(spec, world)


def ember_motion_expression():
    return (f"lerp(Particles.Velocity, {FLUID_VELOCITY}, 1.0 - exp(-User.EmberDragPerSec * Engine.DeltaTime))"
            " + float3(0.0, 0.0, (User.EmberBuoyancyCmPerSec2 * Particles.TDEmberHeat - User.EmberGravityCmPerSec2 * (1.0 - Particles.TDEmberHeat)"
            " + User.EmberAmbientHeatBuoyancyCmPerSec2 * $A.r) * Engine.DeltaTime)")


def update_steps(spec):
    is_ember = spec["profile"] == "ember"
    prefix = prefix_of(spec)
    steps = [set_step(PARTICLE_UPDATE_SCRIPT, "update_plane_uv", (toolset.PLANE_UV_ATTRIBUTE, toolset.VECTOR2_TYPE, "expression", plane_uv_expression(spec)))]
    steps.extend(sample_steps(PARTICLE_UPDATE_SCRIPT, "update_velocity_sample", "User.VelocityTexture", "$V"))
    steps.extend(sample_steps(PARTICLE_UPDATE_SCRIPT, "update_density_sample", "User.DensityPressureTexture", "$D"))
    if is_ember:
        steps.extend(sample_steps(PARTICLE_UPDATE_SCRIPT, "update_aux_sample", "User.AuxTexture", "$A"))
        steps.append(set_step(PARTICLE_UPDATE_SCRIPT, "update_motion",
                              ("Particles.Velocity", toolset.VECTOR3_TYPE, "expression", ember_motion_expression()),
                              ("Particles.TDEmberHeat", toolset.FLOAT_TYPE, "expression", "Particles.TDEmberHeat - User.EmberCoolingPerSec * Engine.DeltaTime")))
    else:
        steps.append(set_step(PARTICLE_UPDATE_SCRIPT, "update_motion", ("Particles.Velocity", toolset.VECTOR3_TYPE, "expression", drive_motion_expression(spec))))
    if "height" in spec["features"]:
        steps.append(set_step(PARTICLE_UPDATE_SCRIPT, "update_height", ("Particles.Velocity", toolset.VECTOR3_TYPE, "expression", drive_height_expression(spec))))
    steps.append(set_step(PARTICLE_UPDATE_SCRIPT, "update_slow", ("Particles.TDDriveSlowSeconds", toolset.FLOAT_TYPE, "expression",
                                                                  f"length(Particles.Velocity) < User.{prefix}SlowKillSpeedCmPerSec ? Particles.TDDriveSlowSeconds + Engine.DeltaTime : 0.0")))
    if is_ember:
        kill = ("(Particles.TDEmberHeat <= 0.0 || Particles.TDDriveSlowSeconds >= User.EmberSlowKillSeconds + User.EmberGroundRestSeconds)"
                " ? min(Particles.Age, Particles.Lifetime) : Particles.Lifetime")
    else:
        kill = ("(User.DriveSlowKillSeconds > 0.0 && Particles.TDDriveSlowSeconds >= User.DriveSlowKillSeconds + User.DriveRestSeconds)"
                " ? min(Particles.Age, Particles.Lifetime) : Particles.Lifetime")
    steps.append(set_step(PARTICLE_UPDATE_SCRIPT, "update_kill", ("Particles.Lifetime", toolset.FLOAT_TYPE, "expression", kill)))
    shape_entries = [shape_entry(spec, False)]
    if is_ember:
        shape_entries.append(("Particles.DynamicMaterialParameter", toolset.VECTOR4_TYPE, "expression",
                              "float4(saturate(Particles.TDEmberHeat), saturate(min(Particles.TDEmberHeat, Particles.Lifetime - Particles.Age) * User.EmberFadePerSec), 0.0, 0.0)"))
    if "color" in spec["features"]:
        density = "max($D.r, $D.b)"
        shape_entries.append(("Particles.Color", toolset.COLOR_TYPE, "expression",
                              f"float4(lerp(User.DriveColorLow.rgb, User.DriveColorHigh.rgb, saturate({density} * User.DriveDensityToColor)),"
                              f" saturate({density} * User.DriveDensityToAlpha) * saturate(min(Particles.Age, Particles.Lifetime - Particles.Age) * User.DriveFadePerSec))"))
    steps.append(set_step(PARTICLE_UPDATE_SCRIPT, "update_shape", *shape_entries))
    steps.append({"script": PARTICLE_UPDATE_SCRIPT, "kind": "module", "label": "solve_forces", "module": SOLVE_FORCES_MODULE})
    if "collision" in spec["features"]:
        steps.append({"script": PARTICLE_UPDATE_SCRIPT, "kind": "linked_inputs_module", "label": "collision", "module": COLLISION_MODULE,
                      "links": (("Restitution", ("User.EmberCollisionRestitution", toolset.FLOAT_TYPE)),
                                ("Friction", ("User.EmberCollisionFriction", toolset.FLOAT_TYPE)))})
    return steps


MASS_ORIGIN = "mul(float4(0.0, 0.0, 0.0, 1.0), Engine.Owner.SystemLocalToWorld).xyz"


def mass_axis(component):
    return f"mul(float4({component}, 0.0), Engine.Owner.SystemLocalToWorld).xyz"


def mass_emitter_steps(spec):
    if spec["spawn"] == "grid_burst":
        return [
            {"script": EMITTER_UPDATE_SCRIPT, "kind": "state_enums", "label": "loop_once", "module_fragment": "EmitterState",
             "enums": (LIFE_CYCLE_SELF, LOOP_BEHAVIOR_ONCE)},
            {"script": EMITTER_UPDATE_SCRIPT, "kind": "expression_module", "label": "spawn_burst", "module": SPAWN_BURST_MODULE,
             "input": "Spawn Count", "expression": "User.TDMassBurstGridCount * User.TDMassBurstGridCount"},
        ]
    return [{"script": EMITTER_UPDATE_SCRIPT, "kind": "expression_module", "label": "spawn_rate", "module": SPAWN_RATE_MODULE,
             "input": "SpawnRate", "expression": "User.TDMassSpawnRatePerSec"}]


def mass_spawn_position(spec):
    if spec["spawn"] == "grid_burst" and spec[MASS_SPEC_KEY]["sim_target"] == "cpu":
        count = "(float)max(User.TDMassBurstGridCount, 1)"
        index = f"fmod((float)Particles.UniqueID, {count} * {count})"
        offset = f"((float2(fmod({index}, {count}), floor({index} / {count})) - ({count} - 1.0) * 0.5) * User.TDMassGridSpacingCm)"
        return f"{MASS_ORIGIN} + {mass_axis('float3(1.0, 0.0, 0.0)')} * {offset}.x + {mass_axis('float3(0.0, 1.0, 0.0)')} * {offset}.y"
    if spec["spawn"] == "grid_burst":
        count = "max(User.TDMassBurstGridCount, 1)"
        index = f"(Particles.UniqueID % ({count} * {count}))"
        offset = f"((float2({index} % {count}, {index} / {count}) - ({count} - 1) * 0.5) * User.TDMassGridSpacingCm)"
        return f"{MASS_ORIGIN} + {mass_axis('float3(1.0, 0.0, 0.0)')} * {offset}.x + {mass_axis('float3(0.0, 1.0, 0.0)')} * {offset}.y"
    if spec[MASS_SPEC_KEY]["motion"] == "slide":
        return f"{MASS_ORIGIN} + float3(Particles.TDMassBoxRoll.x, Particles.TDMassBoxRoll.y, Particles.TDMassBoxRollZ) * User.TDMassBoxExtentCm"
    return MASS_ORIGIN


def mass_spawn_velocity(spec):
    if spec[MASS_SPEC_KEY]["motion"] != "cone":
        return ("Particles.Velocity", toolset.VECTOR3_TYPE, "value", (0.0, 0.0, 0.0))
    spread = (f"({mass_axis('float3(0.0, 1.0, 0.0)')} * Particles.TDMassConeRoll.x + {mass_axis('float3(0.0, 0.0, 1.0)')} * Particles.TDMassConeRoll.y)"
              " * tan(radians(User.TDMassConeAngleDegrees))")
    return ("Particles.Velocity", toolset.VECTOR3_TYPE, "expression",
            f"normalize({mass_axis('float3(1.0, 0.0, 0.0)')} + {spread}) * User.TDMassSpeedCmPerSec")


def mass_spawn_steps(spec):
    motion = spec[MASS_SPEC_KEY]["motion"]
    rolls = []
    if motion == "cone":
        rolls.append(("Particles.TDMassConeRoll", toolset.VECTOR2_TYPE, "random", (RANDOM_RANGE_VECTOR2D, (-1.0, -1.0), (1.0, 1.0))))
    if motion == "slide" and spec["spawn"] != "grid_burst":
        rolls.append(("Particles.TDMassBoxRoll", toolset.VECTOR2_TYPE, "random", (RANDOM_RANGE_VECTOR2D, (-1.0, -1.0), (1.0, 1.0))))
        rolls.append(("Particles.TDMassBoxRollZ", toolset.FLOAT_TYPE, "random", (RANDOM_RANGE_FLOAT, -1.0, 1.0)))
    steps = [set_step(PARTICLE_SPAWN_SCRIPT, "mass_spawn_random", *rolls)] if rolls else []
    steps.append(set_step(PARTICLE_SPAWN_SCRIPT, "mass_spawn_place",
                          ("Particles.Position", toolset.POSITION_TYPE, "expression", mass_spawn_position(spec)),
                          ("Particles.Lifetime", toolset.FLOAT_TYPE, "expression", "User.TDMassLifetimeSeconds"),
                          mass_spawn_velocity(spec)))
    return steps


def mass_motion_expression(spec):
    motion = spec[MASS_SPEC_KEY]["motion"]
    if motion == "swirl":
        relative = f"(Particles.Position - {MASS_ORIGIN})"
        return f"cross(float3(0.0, 0.0, 1.0), {relative}) * (User.TDMassSwirlCmPerSec / max(length({relative}.xy), 1.0))"
    if motion == "cone":
        return ("Particles.Velocity * exp(-User.TDMassDragPerSec * Engine.DeltaTime)"
                " + float3(0.0, 0.0, User.TDMassBuoyancyCmPerSec2 * Engine.DeltaTime)")
    return "Particles.Velocity + User.TDMassSlopeDirection * (User.TDMassSlideAccelerationCmPerSec2 * Engine.DeltaTime)"


def mass_strength_expressions():
    return (("RadiusCm", "User.TDMassRadiusCm"),
            ("DensityStrength", "User.TDMassDensity * saturate(1.0 - Particles.Age / max(Particles.Lifetime, 0.0001) * User.TDMassFadeWithAge)"),
            ("HeatStrength", "User.TDMassHeat"))


def mass_write_step(spec, feature):
    mass = spec[MASS_SPEC_KEY]
    links = (("Position", ("Particles.Position", toolset.POSITION_TYPE)), ("Velocity", ("Particles.Velocity", toolset.VECTOR3_TYPE)))
    if feature == "splat":
        return {"script": PARTICLE_UPDATE_SCRIPT, "kind": "data_interface_module", "label": "mass_splat", "module": MASS_SPLAT_MODULE,
                "data_interface_input": "SplatInterface", "data_interface_values": {"TargetChannelName": mass["target_channel_name"]},
                "links": links, "expressions": mass_strength_expressions()}
    values = {"Channel": mass["data_channel"], "AllocationMode": "Static", "AllocationCount": mass["allocation_count"],
              "bPublishToGame": True, "bPublishToCPU": False, "bPublishToGPU": False}
    return {"script": PARTICLE_UPDATE_SCRIPT, "kind": "data_interface_module", "label": "mass_ndc_append", "module": MASS_NDC_APPEND_MODULE,
            "data_interface_input": "DataChannelWriter", "data_interface_values": values, "links": links, "expressions": mass_strength_expressions()}


def mass_update_steps(spec):
    steps = [set_step(PARTICLE_UPDATE_SCRIPT, "mass_update_motion", ("Particles.Velocity", toolset.VECTOR3_TYPE, "expression", mass_motion_expression(spec))),
             {"script": PARTICLE_UPDATE_SCRIPT, "kind": "module", "label": "solve_forces", "module": SOLVE_FORCES_MODULE}]
    steps.extend(mass_write_step(spec, feature) for feature in MASS_WRITE_FEATURES if feature in spec["features"])
    if "kill" in spec["features"]:
        steps.append({"script": PARTICLE_UPDATE_SCRIPT, "kind": "kill_volume", "label": "mass_kill", "module": KILL_VOLUME_MODULE,
                      "enums": (KILL_SHAPE_BOX,),
                      "expressions": (("Volume Origin", "User.TDMassKillBoxCenter"), ("Box Size", "User.TDMassKillBoxSizeCm"))})
    return steps


def build_stack_plan(spec):
    if is_mass(spec):
        return mass_emitter_steps(spec) + mass_spawn_steps(spec) + mass_update_steps(spec)
    return emitter_spawn_steps(spec) + spawn_steps(spec) + update_steps(spec)


def count_sample_steps(plan):
    return sum(1 for step in plan if step["kind"] == "sample")


def count_state_enums(plan):
    return sum(len(step["enums"]) for step in plan if step["kind"] in ("state_enums", "kill_volume"))


def count_data_interface_steps(plan):
    return sum(1 for step in plan if step["kind"] == "data_interface_module")


def required_compile_scripts(spec):
    if is_mass(spec) and spec[MASS_SPEC_KEY]["sim_target"] == "cpu":
        return []
    return [f"{spec['emitter_name']}.{GPU_COMPUTE_SCRIPT}"]


def sim_target_of(spec):
    if is_mass(spec) and spec[MASS_SPEC_KEY]["sim_target"] == "cpu":
        return "CPUSim"
    return "GPUComputeSim"


def build_system_data(spec):
    data = {"bDeterminism": True, "RandomSeed": spec["random_seed"], "bFixedTickDelta": False}
    if spec["cast_shadow"]:
        data.update({"bOverrideCastShadow": True, "bCastShadow": True})
    return data


def build_emitter_data(spec):
    half = float(spec["fixed_bounds_half_extent_cm"])
    return {
        "SimTarget": sim_target_of(spec),
        "bLocalSpace": is_local(spec),
        "CalculateBoundsMode": "Fixed",
        "FixedBounds": {"Min": {"X": -half, "Y": -half, "Z": -half}, "Max": {"X": half, "Y": half, "Z": half}, "IsValid": 1},
        "bDeterminism": True,
        "RandomSeed": spec["random_seed"],
        "bRequiresPersistentIDs": True,
        "AttributesToPreserve": ["Particles.UniqueID"],
        "AllocationMode": "FixedCount",
        "PreAllocationCount": spec["max_particles"],
    }


def renderer_class_of(spec):
    if spec["renderer"]["kind"] == "none":
        return None
    return SPRITE_RENDERER_CLASS if spec["renderer"]["kind"] == "sprite" else MESH_RENDERER_CLASS


def build_renderer_data(spec):
    renderer = spec["renderer"]
    material = toolset.object_path(renderer["material"]) if "." not in renderer["material"] else renderer["material"]
    if renderer["kind"] == "sprite":
        return {"Material": material, "Alignment": renderer["alignment"]}
    return {"Meshes": [{"Mesh": renderer["mesh"]}], "FacingMode": renderer["mesh_facing"], "bOverrideMaterials": True,
            "OverrideMaterials": [{"ExplicitMat": material}]}


def expected_renderer_paths(spec):
    renderer = spec["renderer"]
    if renderer["kind"] == "none":
        return []
    paths = [renderer["material"]]

    if renderer["mesh"]:
        paths.append(renderer["mesh"])
    return paths


def collect_declared_particle_names(plan):
    names = set()
    for step in plan:
        for entry in step.get("entries", ()):
            names.add(entry[0].split(".", 1)[1])
    names.add(toolset.PLANE_UV_ATTRIBUTE.split(".", 1)[1])
    return names


def find_token_problems(expression, user_names, particle_names):
    problems = [f"치환되지 않은 자리표시 {placeholder}" for placeholder in PLACEHOLDERS if placeholder in expression]
    for namespace, dotted in TOKEN_PATTERN.findall(expression):
        segments = dotted.split(".")
        if namespace == "User":
            if f"User.{segments[0]}" not in user_names:
                problems.append(f"선언 안 된 User 변수 User.{segments[0]}")
            continue
        if namespace == "Particles":
            if len(segments) >= 2 and segments[1] == "SampledColor":
                continue
            name = segments[0]
            if name.startswith("TD") and name not in particle_names:
                problems.append(f"선언 안 된 입자 속성 Particles.{name}")
            elif not name.startswith("TD") and name not in ALLOWED_PARTICLE_NAMES:
                problems.append(f"허용 목록 밖 입자 속성 Particles.{name}")
            continue
        full = f"Engine.{dotted}"
        if not any(full == allowed or full.startswith(allowed + ".") for allowed in ALLOWED_ENGINE_NAMES):
            problems.append(f"허용 목록 밖 엔진 이름 {full}")
    return problems
