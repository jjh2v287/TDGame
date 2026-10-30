"""실행 환경: 시스템 Python 3 순수 모듈(unreal·MCP 없음, fluid_niagara_drive.profile_module 이 지연 import 한다).
실행 명령: 직접 실행하지 않는다(import fluid_niagara_rain).
출력: 없음. profile rain(spec-w3 3.6 빗방울: 월드 공간 고정·SpawnRate 방출·충돌과 쓰기 모듈 없음) 명세 검사와 STACK_PLAN(입력 단위 HLSL 식)·User 변수 계획(런타임 TDRainIntensity + 조절 등록표 User.TDRain*)·모양 필수 이름(런타임 제외, R3-M2)·시뮬 대상·컴파일 스크립트를 fluid_niagara_drive 의 같은 이름 함수와 같은 반환형으로 준다.
상태: 2026-10-29 작성(미검증, 오프라인 모듈 호출만). 위치 원점은 drive.MASS_ORIGIN 재사용(Engine.Owner.Position 새 허용 안 함, B11), 스프라이트 정렬은 명세대로 Unaligned 유지(B10), 런타임 표는 drive.RAIN_RUNTIME_USER_VARIABLES(S1 소유)를 호출 시점에 읽는다.
"""
import fluid_niagara_drive as drive
import fluid_niagara_toolset as toolset

RAIN_PROFILE = "rain"
RAIN_SPAWN_MODE = "rate"
RAIN_SPACE = "world"
F = toolset.FLOAT_TYPE
V2 = toolset.VECTOR2_TYPE
RAIN_TUNING_REGISTRY = (
    ("User.TDRainSpawnRatePerSec", F, 0.0, 1e5),
    ("User.TDRainSpawnHeightCm", F, 1.0, 1e5),
    ("User.TDRainFallSpeedCmPerSec", F, 1.0, 1e5),
    ("User.TDRainSplashSeconds", F, 0.01, 10.0),
    ("User.TDRainOpacity", F, 0.0, 1.0),
    ("User.TDRainBoxHalfExtentCm", V2, 1.0, 1e5),
    ("User.TDRainStreakSizeCm", V2, 0.01, 1000.0),
    ("User.TDRainSplashSizeCm", V2, 0.01, 1000.0),
    ("User.TDRainWindCmPerSec", V2, -5000.0, 5000.0),
)
RAIN_SPAWN_HEIGHT = "User.TDRainSpawnHeightCm * (0.5 + 0.5 * Particles.TDRainRollZ)"
RAIN_FALL_VELOCITY = "float3(User.TDRainWindCmPerSec.x, User.TDRainWindCmPerSec.y, -User.TDRainFallSpeedCmPerSec)"
RAIN_LANDED = "step(Particles.TDRainFallSeconds, Particles.Age)"
RAIN_OWNER_HEIGHT = f"({drive.MASS_ORIGIN}).z"


def runtime_user_variables():
    return drive.RAIN_RUNTIME_USER_VARIABLES


def find_identity_problems(spec):
    problems = []
    if not isinstance(spec["asset"], str) or not drive.ASSET_PATTERN.match(spec["asset"]):
        problems.append(f"asset: {drive.ASSET_ROOT}NS_TD_* 패키지 경로여야 함, 값 {spec['asset']!r}")
    if not isinstance(spec["emitter_name"], str) or not drive.NAME_PATTERN.match(spec["emitter_name"]):
        problems.append("emitter_name: 영문 식별자여야 함")
    return problems


def find_profile_problems(spec):
    problems = []
    if spec["profile"] != RAIN_PROFILE:
        problems.append(f"profile: rain 모듈은 {RAIN_PROFILE} 만 검사함, 값 {spec['profile']!r}")
    if spec["space"] != RAIN_SPACE:
        problems.append(f"space: profile rain 은 {RAIN_SPACE} 에서만 허용, 값 {spec['space']!r}")
    if spec["spawn"] != RAIN_SPAWN_MODE:
        problems.append(f"spawn: profile rain 은 {RAIN_SPAWN_MODE} 만 허용, 값 {spec['spawn']!r}")
    features = spec["features"]
    if not isinstance(features, list):
        problems.append("features: 목록이어야 함")
        return problems
    problems.extend(f"features: profile rain 은 기능이 없음(충돌·쓰기 모듈 금지), 값 {item!r}" for item in features)
    return problems


def find_bound_problems(spec):
    problems = []
    if not isinstance(spec["cast_shadow"], bool):
        problems.append("cast_shadow: true|false")
    if not isinstance(spec["max_particles"], int) or isinstance(spec["max_particles"], bool) or not 1 <= spec["max_particles"] <= 65536:
        problems.append("max_particles: 1~65536 정수")
    if not drive.is_number(spec["fixed_bounds_half_extent_cm"]) or not 1.0 <= spec["fixed_bounds_half_extent_cm"] <= 100000.0:
        problems.append("fixed_bounds_half_extent_cm: 1~100000 수")
    if not isinstance(spec["random_seed"], int) or isinstance(spec["random_seed"], bool) or spec["random_seed"] < 0:
        problems.append("random_seed: 0 이상 정수")
    return problems


def find_renderer_problems(renderer):
    problems = drive.find_renderer_problems(renderer)
    if problems:
        return problems
    if renderer["kind"] != "sprite":
        return [f"renderer.kind: profile rain 은 sprite 만 허용, 값 {renderer['kind']!r}"]
    return []


def find_spec_problems(spec):
    if not isinstance(spec, dict):
        return ["명세 최상위는 객체여야 함"]
    problems = [f"모르는 키 {key!r}" for key in spec if key not in drive.SPEC_KEYS]
    problems.extend(f"키 없음 {key!r}" for key in drive.SPEC_KEYS if key not in spec)
    if problems:
        return problems
    problems.extend(find_identity_problems(spec))
    problems.extend(find_profile_problems(spec))
    problems.extend(find_bound_problems(spec))
    problems.extend(find_renderer_problems(spec["renderer"]))
    return problems


def select_tuning_variables(spec):
    return list(RAIN_TUNING_REGISTRY)


def build_user_variable_plan(spec):
    return list(runtime_user_variables()) + [(name, type_path) for name, type_path, _, _ in select_tuning_variables(spec)]


def required_look_names(spec):
    runtime_names = {name for name, _ in runtime_user_variables()}
    return [(name, type_path) for name, type_path in build_user_variable_plan(spec)
            if type_path != toolset.TEXTURE_CLASS and name not in runtime_names]


def emitter_steps():
    return [{"script": drive.EMITTER_UPDATE_SCRIPT, "kind": "expression_module", "label": "spawn_rate", "module": drive.SPAWN_RATE_MODULE,
             "input": "SpawnRate", "expression": "User.TDRainSpawnRatePerSec * saturate(User.TDRainIntensity)"}]


def spawn_steps():
    script = drive.PARTICLE_SPAWN_SCRIPT
    return [
        drive.set_step(script, "rain_spawn_random",
                       ("Particles.TDRainRoll", toolset.VECTOR2_TYPE, "random", (drive.RANDOM_RANGE_VECTOR2D, (-1.0, -1.0), (1.0, 1.0))),
                       ("Particles.TDRainRollZ", toolset.FLOAT_TYPE, "random", (drive.RANDOM_RANGE_FLOAT, 0.0, 1.0))),
        drive.set_step(script, "rain_spawn_fall",
                       ("Particles.TDRainFallSeconds", toolset.FLOAT_TYPE, "expression",
                        f"{RAIN_SPAWN_HEIGHT} / max(User.TDRainFallSpeedCmPerSec, 1.0)")),
        drive.set_step(script, "rain_spawn_place",
                       ("Particles.Position", toolset.POSITION_TYPE, "expression",
                        f"{drive.MASS_ORIGIN} + float3(Particles.TDRainRoll.x * User.TDRainBoxHalfExtentCm.x,"
                        f" Particles.TDRainRoll.y * User.TDRainBoxHalfExtentCm.y, {RAIN_SPAWN_HEIGHT})"),
                       ("Particles.Lifetime", toolset.FLOAT_TYPE, "expression", "Particles.TDRainFallSeconds + max(User.TDRainSplashSeconds, 0.01)"),
                       ("Particles.Velocity", toolset.VECTOR3_TYPE, "expression", RAIN_FALL_VELOCITY),
                       ("Particles.SpriteSize", toolset.VECTOR2_TYPE, "expression", "User.TDRainStreakSizeCm"),
                       ("Particles.Color", toolset.COLOR_TYPE, "expression", "float4(0.0, 0.0, 0.0, User.TDRainOpacity)"),
                       ("Particles.TDRainSplashPhase", toolset.FLOAT_TYPE, "value", 0.0)),
    ]


def update_steps():
    script = drive.PARTICLE_UPDATE_SCRIPT
    return [
        drive.set_step(script, "rain_update_phase",
                       ("Particles.TDRainSplashPhase", toolset.FLOAT_TYPE, "expression",
                        "saturate((Particles.Age - Particles.TDRainFallSeconds) / max(User.TDRainSplashSeconds, 0.01))")),
        drive.set_step(script, "rain_update_motion",
                       ("Particles.Velocity", toolset.VECTOR3_TYPE, "expression", f"{RAIN_FALL_VELOCITY} * (1.0 - {RAIN_LANDED})")),
        {"script": script, "kind": "module", "label": "solve_forces", "module": drive.SOLVE_FORCES_MODULE},
        drive.set_step(script, "rain_update_floor",
                       ("Particles.Position", toolset.POSITION_TYPE, "expression",
                        f"float3(Particles.Position.x, Particles.Position.y, max(Particles.Position.z, {RAIN_OWNER_HEIGHT}))")),
        drive.set_step(script, "rain_update_shape",
                       ("Particles.SpriteSize", toolset.VECTOR2_TYPE, "expression",
                        f"lerp(User.TDRainStreakSizeCm, User.TDRainSplashSizeCm * (0.3 + 0.7 * Particles.TDRainSplashPhase), {RAIN_LANDED})"),
                       ("Particles.Color", toolset.COLOR_TYPE, "expression",
                        f"float4({RAIN_LANDED}, Particles.TDRainSplashPhase, 0.0, User.TDRainOpacity * (1.0 - Particles.TDRainSplashPhase))")),
    ]


def build_stack_plan(spec):
    return emitter_steps() + spawn_steps() + update_steps()


def required_compile_scripts(spec):
    return [f"{spec['emitter_name']}.{drive.GPU_COMPUTE_SCRIPT}"]


def sim_target_of(spec):
    return "GPUComputeSim"
