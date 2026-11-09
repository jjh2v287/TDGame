"""실행 환경: 시스템 Python 3 순수 모듈(unreal·MCP 없음). fluid_niagara_drive 가 profile water 명세에서 지연 가져와 같은 이름 함수로 위임한다(X-05, spec-w1b 2.2).
실행 명령: 직접 실행하지 않는다(import fluid_niagara_water). HLSL 파 상수 점검은 python -B Tools/FluidFX/check_fluid_niagara_specs.py 의 ⑧.
출력: 없음. water 명세 검증(역할 debris·whitewater·splash)·STACK_PLAN(L5 수면 식: 재질 거스트너 2패스+잔물결+타일맵+지형 기준, 사건 물보라는 NDC Append 접촉)·런타임 User 목록 WATER_RUNTIME_USER_VARIABLES·조절 등록표 WATER_TUNING_REGISTRY·HLSL 파 상수 read_wave_constants(root=None, root 아래 사본 우선).
상태: 2026-10-29 신규(미검증, 오프라인 검사만: 가짜 호출 기록·fixture 6). 에디터 0a·0b·생성은 P3 에서. 접촉 bExecute 식이 0b 에서 실패하면 SPLASH_EXECUTE_BY_EXPRESSION 을 False 로 바꾼다(대체: 기본 true + 밀도 곱, C++ 가 밀도 0 점을 버림).
"""
import functools
import math
import pathlib
import re

import fluid_niagara_drive as drive
import fluid_niagara_toolset as toolset

TOOLS_ROOT = pathlib.Path(__file__).resolve().parents[1]
PROJECT_ROOT = TOOLS_ROOT.parent
WAVE_HLSL_NAME = "TDFluidWaterCommon.hlsl"
TERRAIN_USH_NAME = "TDFluidGpuWaterCommon.ush"
WAVE_HLSL_PATH = TOOLS_ROOT / "FluidFX" / "Shaders" / WAVE_HLSL_NAME
TERRAIN_USH_PATH = PROJECT_ROOT / "Source" / "TDFluidGpu" / "Shaders" / "Private" / TERRAIN_USH_NAME
WAVE_COUNT = 8
WAVE_ARRAY_NAMES = ("waveLengthCm", "waveAmplitudeShare", "waveAngleOffsetDegrees", "wavePhaseOffset")
WAVE_SCALAR_NAMES = ("waveSteepness", "waveTimeScale", "gravityCmPerSecondSquared")
TERRAIN_SCALAR_NAMES = (("TDFLUIDGPU_WATER_TERRAIN_HEIGHT_SCALE_CM", "terrainHeightScaleCm"),
                        ("TDFLUIDGPU_WATER_TERRAIN_HEIGHT_LIMIT", "terrainHeightLimit"))
NUMBER_TEXT = r"[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?"
WAVE_ARRAY_PATTERN = re.compile(r"(?<!static )const\s+float\s+(\w+)\s*\[\s*(\d+)\s*\]\s*=\s*\{([^}]*)\}\s*;")
WAVE_SCALAR_PATTERN = re.compile(r"(?<!static )const\s+float\s+(\w+)\s*=\s*(" + NUMBER_TEXT + r")f?\s*;")
TERRAIN_SCALAR_PATTERN = re.compile(r"static\s+const\s+float\s+(TDFLUIDGPU_WATER_TERRAIN_HEIGHT_\w+)\s*=\s*(" + NUMBER_TEXT + r")f?\s*;")

TWO_PI = 6.28318530718
NO_TERRAIN_MARGIN = 0.01
WATER_SPEC_KEY = "water"
WATER_KEYS = ("role", "sim_target", "data_channel", "allocation_count")
ROLES = ("debris", "whitewater", "splash")
SIM_TARGETS = ("gpu", "cpu")
ROLE_SPAWN = {"debris": "rate_reject", "whitewater": "rate_reject", "splash": "event_burst"}
ROLE_RENDERER_KIND = {"debris": "mesh", "whitewater": "sprite", "splash": "sprite"}
ROLE_ALIGNMENT = {"debris": None, "whitewater": "VelocityAligned", "splash": "VelocityAligned"}
ROLE_REQUIRED_FEATURES = {"debris": ("surface",), "whitewater": ("surface",), "splash": ("ndc",)}
ROLE_ALLOWED_FEATURES = {"debris": ("surface", "tilemap"), "whitewater": ("surface", "tilemap"), "splash": ("ndc",)}
SPLASH_EXECUTE_BY_EXPRESSION = True

F = toolset.FLOAT_TYPE
I = toolset.INT_TYPE
V2 = toolset.VECTOR2_TYPE
V3 = toolset.VECTOR3_TYPE
C = toolset.COLOR_TYPE
WATER_SURFACE_RUNTIME = (
    ("User.TDWaterBaseHeightCm", F),
    ("User.TDWaterUsesTerrainBase", F),
    ("User.TDWaterTerrainReferenceZCm", F),
    ("User.TDWaterDepthReferenceCm", F),
    ("User.TDWaterRippleHeightReferenceCm", F),
    ("User.TDWaterLayerActive", F),
    ("User.TDWaterBorderFadeRatio", F),
    ("User.TDWaterPressureNormalStrength", F),
    ("User.TDWaterPassiveWaveAmplitudeCm", F),
    ("User.TDWaterPassiveWaveDirectionDegrees", F),
    ("User.TDWaterPassiveWaveSimDamping", F),
    ("User.TDWaterPassiveWaveLengthScale", F),
    ("User.TDWaterPassiveWaveAngleSpread", F),
    ("User.TDWaterTimeSeconds", F),
    ("User.TDWaterCoverage", F),
    ("User.TDWaterCameraWorld", V3),
    ("User.TDDebrisPrefillRateScale", F),
)
WATER_TILEMAP_RUNTIME = (
    ("User.TDWaterTileMapHeightCm", F),
    ("User.TDWaterTileMapDensityHeightCm", F),
    ("User.TDWaterTileMapFadeStartCm", F),
    ("User.TDWaterTileMapFadeLengthCm", F),
    ("User.TDWaterTileMapOriginCm", V2),
    ("User.TDWaterTileMapPeriodCm", V2),
    ("User.TDWaterTileMapTexture", toolset.TEXTURE_CLASS),
)
WATER_SPLASH_RUNTIME = (
    ("User.TDSplashImpactSpeedCmPerSec", F),
    ("User.TDSplashImpactRadiusCm", F),
    ("User.TDSplashSurfaceHeightCm", F),
    ("User.TDSplashImpactVelocity", V3),
)
WATER_RUNTIME_USER_VARIABLES = WATER_SURFACE_RUNTIME + WATER_TILEMAP_RUNTIME + WATER_SPLASH_RUNTIME

WATER_TUNING_REGISTRY = (
    ("debris", "User.TDDebrisCount", I, 0, 1000),
    ("debris", "User.TDDebrisLifetimeMinSeconds", F, 1.0, 1e5),
    ("debris", "User.TDDebrisLifetimeMaxSeconds", F, 1.0, 1e5),
    ("debris", "User.TDDebrisSpawnUvMin", V2, 0.0, 1.0),
    ("debris", "User.TDDebrisSpawnUvMax", V2, 0.0, 1.0),
    ("debris", "User.TDDebrisMinDepthCm", F, 0.0, 500.0),
    ("debris", "User.TDDebrisFloatOffsetCm", F, -50.0, 50.0),
    ("debris", "User.TDDebrisFlowFollowScale", F, 0.0, 2.0),
    ("debris", "User.TDDebrisFlowDragPerSec", F, 0.0, 10.0),
    ("debris", "User.TDDebrisHeightFollowPerSec", F, 0.0, 60.0),
    ("debris", "User.TDDebrisBobbingHeightScale", F, 0.0, 2.0),
    ("debris", "User.TDDebrisMeshScaleCm", F, 5.0, 60.0),
    ("debris", "User.TDDebrisScaleFadePerSec", F, 0.1, 60.0),
    ("debris", "User.TDDebrisColor", C, 0.0, 64.0),
    ("whitewater", "User.TDWhitewaterSpawnPerM2PerSec", F, 0.0, 200.0),
    ("whitewater", "User.TDWhitewaterSpawnThreshold", F, 0.0, 1.0),
    ("whitewater", "User.TDWhitewaterSpawnUvMin", V2, 0.0, 1.0),
    ("whitewater", "User.TDWhitewaterSpawnUvMax", V2, 0.0, 1.0),
    ("whitewater", "User.TDWhitewaterLifetimeMinSeconds", F, 0.3, 4.0),
    ("whitewater", "User.TDWhitewaterLifetimeMaxSeconds", F, 0.3, 4.0),
    ("whitewater", "User.TDWhitewaterVelocityDragPerSec", F, 0.0, 50.0),
    ("whitewater", "User.TDWhitewaterHeightFollowPerSec", F, 0.0, 60.0),
    ("whitewater", "User.TDWhitewaterLiftCm", F, 0.0, 50.0),
    ("whitewater", "User.TDWhitewaterDensityToAlpha", F, 0.0, 4.0),
    ("whitewater", "User.TDWhitewaterFadePerSec", F, 0.0, 60.0),
    ("whitewater", "User.TDWhitewaterCameraFadeStartCm", F, 500.0, 1e4),
    ("whitewater", "User.TDWhitewaterCameraFadeLengthCm", F, 1.0, 5000.0),
    ("whitewater", "User.TDWhitewaterSpriteSizeCm", F, 0.5, 100.0),
    ("whitewater", "User.TDWhitewaterStretchPerCmPerSec", F, 0.0, 0.05),
    ("whitewater", "User.TDWhitewaterColor", C, 0.0, 64.0),
    ("splash", "User.TDSplashDropletsPerImpact", I, 0, 128),
    ("splash", "User.TDSplashFullCountSpeedCmPerSec", F, 50.0, 2000.0),
    ("splash", "User.TDSplashLifetimeMinSeconds", F, 0.1, 1.0),
    ("splash", "User.TDSplashLifetimeMaxSeconds", F, 0.3, 2.0),
    ("splash", "User.TDSplashCrownShare", F, 0.0, 1.0),
    ("splash", "User.TDSplashCrownRadiusRatio", F, 1.0, 1.5),
    ("splash", "User.TDSplashCrownThicknessRatio", F, 0.1, 0.2),
    ("splash", "User.TDSplashCrownUpSpeedRatio", F, 0.0, 3.0),
    ("splash", "User.TDSplashCrownRadialSpeedRatio", F, 0.0, 2.0),
    ("splash", "User.TDSplashDropletUpSpeedRatio", F, 0.0, 3.0),
    ("splash", "User.TDSplashDropletRadialSpeedRatio", F, 0.0, 2.0),
    ("splash", "User.TDSplashSpeedJitter", F, 0.0, 1.0),
    ("splash", "User.TDSplashCarryRatio", F, 0.0, 1.0),
    ("splash", "User.TDSplashGravityCmPerSec2", F, 0.0, 2000.0),
    ("splash", "User.TDSplashDragPerSec", F, 0.0, 20.0),
    ("splash", "User.TDSplashDropletSizeCm", F, 0.3, 20.0),
    ("splash", "User.TDSplashStretchPerCmPerSec", F, 0.0, 0.05),
    ("splash", "User.TDSplashFadePerSec", F, 0.0, 60.0),
    ("splash", "User.TDSplashSpawnLiftCm", F, 0.0, 20.0),
    ("splash", "User.TDSplashContactMinAgeSeconds", F, 0.0, 0.5),
    ("splash", "User.TDSplashContactWhitewater", F, 0.0, 1.0),
    ("splash", "User.TDSplashContactRadiusCm", F, 1.0, 200.0),
    ("splash", "User.TDSplashColor", C, 0.0, 64.0),
)

SPAWN = drive.PARTICLE_SPAWN_SCRIPT
UPDATE = drive.PARTICLE_UPDATE_SCRIPT
EMITTER_UPDATE = drive.EMITTER_UPDATE_SCRIPT
RAW_UV = "Particles.TDWaterRawUv"
TILE_UV = "Particles.TDWaterTileUv"
FLOW_XY = f"{drive.FLUID_VELOCITY}.xy"


class TDWaterConstantsError(Exception):
    pass


def parse_number(text):
    return float(text.strip().rstrip("fF"))


def locate_source(name, root, default_path):
    if root is not None and (pathlib.Path(root) / name).is_file():
        return pathlib.Path(root) / name
    return default_path


def read_source_text(path):
    try:
        return path.read_text(encoding="utf-8")
    except OSError as error:
        raise TDWaterConstantsError(f"{path}: 읽기 실패 {error}") from error


def parse_wave_arrays(text, hlsl_path):
    arrays = {}
    for name, count, body in WAVE_ARRAY_PATTERN.findall(text):
        if name not in WAVE_ARRAY_NAMES:
            continue
        values = tuple(parse_number(item) for item in body.split(",") if item.strip())
        if int(count) != WAVE_COUNT or len(values) != WAVE_COUNT:
            raise TDWaterConstantsError(f"{hlsl_path.name}: {name} 은 {WAVE_COUNT}개여야 함(선언 {count}, 값 {len(values)})")
        arrays[name] = values
    missing = [name for name in WAVE_ARRAY_NAMES if name not in arrays]
    if missing:
        raise TDWaterConstantsError(f"{hlsl_path.name}: 파 배열 상수 없음 {missing}")
    return arrays


def parse_wave_scalars(text, hlsl_path):
    found = {name: parse_number(value) for name, value in WAVE_SCALAR_PATTERN.findall(text) if name in WAVE_SCALAR_NAMES}
    missing = [name for name in WAVE_SCALAR_NAMES if name not in found]
    if missing:
        raise TDWaterConstantsError(f"{hlsl_path.name}: 파 스칼라 상수 없음 {missing}")
    return found


def parse_terrain_scalars(text, ush_path):
    found = {name: parse_number(value) for name, value in TERRAIN_SCALAR_PATTERN.findall(text)}
    constants = {}
    for hlsl_name, key in TERRAIN_SCALAR_NAMES:
        if hlsl_name not in found:
            raise TDWaterConstantsError(f"{ush_path.name}: 지형 상수 없음 {hlsl_name}")
        constants[key] = found[hlsl_name]
    return constants


@functools.lru_cache(maxsize=None)
def parse_wave_constants(hlsl_path_text, ush_path_text):
    hlsl_path = pathlib.Path(hlsl_path_text)
    ush_path = pathlib.Path(ush_path_text)
    constants = {}
    constants.update(parse_wave_arrays(read_source_text(hlsl_path), hlsl_path))
    constants.update(parse_wave_scalars(read_source_text(hlsl_path), hlsl_path))
    constants.update(parse_terrain_scalars(read_source_text(ush_path), ush_path))
    if min(constants["waveLengthCm"]) <= 0.0 or constants["gravityCmPerSecondSquared"] <= 0.0:
        raise TDWaterConstantsError(f"{hlsl_path.name}: 파장·중력은 0 보다 커야 함")
    if constants["terrainHeightScaleCm"] <= 0.0 or constants["terrainHeightLimit"] <= NO_TERRAIN_MARGIN:
        raise TDWaterConstantsError(f"{ush_path.name}: 지형 높이 배율·한계가 올바르지 않음")
    return constants


def read_wave_constants(root=None):
    hlsl_path = locate_source(WAVE_HLSL_NAME, root, WAVE_HLSL_PATH)
    ush_path = locate_source(TERRAIN_USH_NAME, root, TERRAIN_USH_PATH)
    return parse_wave_constants(str(hlsl_path), str(ush_path))


def number(value):
    text = repr(float(value))
    if "e" in text:
        text = f"{float(value):.12f}".rstrip("0")
    return text + "0" if text.endswith(".") else text


def term(value):
    text = number(value)
    return f"({text})" if text.startswith("-") else text


def role_of(spec):
    return spec[WATER_SPEC_KEY]["role"]


def has_tilemap(spec):
    return "tilemap" in spec["features"]


def runtime_variables_of(spec):
    if role_of(spec) == "splash":
        return list(WATER_SPLASH_RUNTIME)
    runtime = list(drive.RUNTIME_USER_VARIABLES) + list(WATER_SURFACE_RUNTIME)
    return runtime + list(WATER_TILEMAP_RUNTIME) if has_tilemap(spec) else runtime


def select_tuning_variables(spec):
    role = role_of(spec)
    return [(name, type_path, low, high) for condition, name, type_path, low, high in WATER_TUNING_REGISTRY if condition == role]


def build_user_variable_plan(spec):
    return runtime_variables_of(spec) + [(name, type_path) for name, type_path, _, _ in select_tuning_variables(spec)]


def required_look_names(spec):
    unbaked = {name for name, _ in WATER_RUNTIME_USER_VARIABLES}
    return [(name, type_path) for name, type_path in build_user_variable_plan(spec) if type_path != toolset.TEXTURE_CLASS and name not in unbaked]


def sim_target_of(spec):
    return "CPUSim" if spec[WATER_SPEC_KEY]["sim_target"] == "cpu" else "GPUComputeSim"


def required_compile_scripts(spec):
    if spec[WATER_SPEC_KEY]["sim_target"] == "cpu":
        return []
    return [f"{spec['emitter_name']}.{drive.GPU_COMPUTE_SCRIPT}"]


def find_water_object_problems(water):
    if not isinstance(water, dict):
        return ["water: profile water 는 water 객체가 필수"]
    problems = [f"water: 모르는 키 {key!r}" for key in water if key not in WATER_KEYS]
    problems.extend(f"water: 키 없음 {key!r}" for key in ("role", "sim_target") if key not in water)
    if problems:
        return problems
    if water["role"] not in ROLES:
        problems.append(f"water.role: {'|'.join(ROLES)} 중 하나여야 함, 값 {water['role']!r}")
    if water["sim_target"] not in SIM_TARGETS:
        problems.append(f"water.sim_target: {'|'.join(SIM_TARGETS)} 중 하나여야 함, 값 {water['sim_target']!r}")
    return problems


def find_role_pairing_problems(spec, role):
    problems = []
    if spec["space"] != "world":
        problems.append("space: profile water 는 world 에서만 허용")
    if spec["spawn"] != ROLE_SPAWN[role]:
        problems.append(f"spawn: role {role} 는 {ROLE_SPAWN[role]} 만 허용")
    renderer = spec["renderer"]
    if renderer["kind"] != ROLE_RENDERER_KIND[role]:
        problems.append(f"renderer.kind: role {role} 는 {ROLE_RENDERER_KIND[role]} 만 허용")
    if renderer["kind"] == ROLE_RENDERER_KIND[role] and renderer["alignment"] != ROLE_ALIGNMENT[role]:
        problems.append(f"renderer.alignment: role {role} 는 {ROLE_ALIGNMENT[role]!r} 여야 함")
    features = spec["features"]
    problems.extend(f"features: role {role} 에는 {item!r} 필수" for item in ROLE_REQUIRED_FEATURES[role] if item not in features)
    problems.extend(f"features: role {role} 에는 {item!r} 불가" for item in features if item not in ROLE_ALLOWED_FEATURES[role])
    if "tilemap" in features and "surface" not in features:
        problems.append("features: tilemap 은 surface 가 필요")
    return problems


def find_channel_problems(spec, role):
    water = spec[WATER_SPEC_KEY]
    if role != "splash":
        return [f"water: role {role} 에는 {key} 불가" for key in ("data_channel", "allocation_count") if key in water]
    problems = []
    if not isinstance(water.get("data_channel"), str) or not drive.DATA_CHANNEL_PATTERN.match(water["data_channel"]):
        problems.append("water.data_channel: /Game/FluidFX/Niagara/DataChannels/NDC_TD_*.NDC_TD_* 객체 경로여야 함")
    count = water.get("allocation_count")
    max_particles = spec["max_particles"] if isinstance(spec["max_particles"], int) else 65536
    if not isinstance(count, int) or isinstance(count, bool) or not max_particles <= count <= 65536:
        problems.append("water.allocation_count: max_particles 이상 65536 이하 정수")
    return problems


def collect_expression_texts(plan):
    texts = []
    for step in plan:
        if isinstance(step.get("expression"), str):
            texts.append(step["expression"])
        texts.extend(entry[3] for entry in step.get("entries", ()) if entry[2] == "expression")
        texts.extend(expression for _, expression in step.get("expressions", ()))
    return texts


def find_cpu_problems(spec):
    try:
        plan = build_stack_plan(spec)
    except TDWaterConstantsError:
        return []
    if any("%" in text for text in collect_expression_texts(plan)):
        return ["water.sim_target: cpu 는 식에 정수 % 를 쓸 수 없음(L-editor-73)"]
    return []


def find_spec_problems(spec):
    problems = drive.find_base_spec_problems(spec)
    if problems:
        return problems
    problems = find_water_object_problems(spec.get(WATER_SPEC_KEY))
    if problems:
        return problems
    role = role_of(spec)
    problems = find_role_pairing_problems(spec, role) + find_channel_problems(spec, role)
    if not problems and spec[WATER_SPEC_KEY]["sim_target"] == "cpu":
        problems.extend(find_cpu_problems(spec))
    return problems


def set_step(script, label, *entries):
    return drive.set_step(script, label, *entries)


def expression_entry(name, type_path, expression):
    return (name, type_path, "expression", expression)


def tile_sample_steps(label):
    steps = drive.sample_steps(UPDATE, label, "User.TDWaterTileMapTexture", "$T")
    steps[0]["uv_attribute"] = TILE_UV
    return steps


def raw_uv_expression(spec):
    plane_uv = drive.plane_uv_expression(spec)
    prefix = "saturate("
    if not (plane_uv.startswith(prefix) and plane_uv.endswith(")")):
        raise ValueError("drive.plane_uv_expression 이 saturate(…) 형태가 아님")
    return plane_uv[len(prefix):-1]


def tile_uv_expression():
    return "frac((Particles.Position.xy - User.TDWaterTileMapOriginCm) / max(User.TDWaterTileMapPeriodCm, float2(1.0, 1.0)))"


def base_height_expression(constants):
    limit = number(constants["terrainHeightLimit"] - NO_TERRAIN_MARGIN)
    return (f"(User.TDWaterUsesTerrainBase > 0.5 && $S.a < {limit} && $S.r > 0.001 && $S.r < 0.999)"
            f" ? User.TDWaterTerrainReferenceZCm + $S.a * {number(constants['terrainHeightScaleCm'])} + $S.r * User.TDWaterDepthReferenceCm"
            " : User.TDWaterBaseHeightCm")


def window_fade_expression():
    uv = toolset.PLANE_UV_ATTRIBUTE
    return (f"saturate(min(min({uv}.x, 1.0 - {uv}.x), min({uv}.y, 1.0 - {uv}.y)) / max(User.TDWaterBorderFadeRatio, 0.0001))")


def tile_height_expression():
    return ("(User.TDWaterTileMapPeriodCm.x > 0.0 && User.TDWaterTileMapPeriodCm.y > 0.0)"
            " ? (($T.g * 2.0 - 1.0) * User.TDWaterTileMapHeightCm + $T.r * User.TDWaterTileMapDensityHeightCm)"
            " * (1.0 - saturate((length(Particles.Position - User.TDWaterCameraWorld) - User.TDWaterTileMapFadeStartCm)"
            " / max(User.TDWaterTileMapFadeLengthCm, 1.0)))"
            " : 0.0")


def wave_angle(constants, index):
    offset = math.radians(constants["waveAngleOffsetDegrees"][index])
    return f"(Particles.TDWaterWaveBaseAngle + ({term(offset)} * User.TDWaterPassiveWaveAngleSpread))"


def wave_phase(constants, index, point):
    wave_number = TWO_PI / constants["waveLengthCm"][index]
    angular = math.sqrt(constants["gravityCmPerSecondSquared"] * wave_number) * constants["waveTimeScale"]
    angle = wave_angle(constants, index)
    length_scale = "max(User.TDWaterPassiveWaveLengthScale, 0.1)"
    return (f"(({number(wave_number)} / {length_scale}) * dot(float2(cos({angle}), sin({angle})), {point})"
            f" - ({number(angular)} / sqrt({length_scale})) * User.TDWaterTimeSeconds + {term(constants['wavePhaseOffset'][index])})")


def wave_rest_expression(constants):
    terms = []
    for index in range(WAVE_COUNT):
        angle = wave_angle(constants, index)
        phase = wave_phase(constants, index, "Particles.Position.xy")
        terms.append(f"float2(cos({angle}), sin({angle})) * ({term(constants['waveAmplitudeShare'][index])} * cos({phase}))")
    return (f"Particles.Position.xy - {number(constants['waveSteepness'])} * Particles.TDWaterWaveAmplitudeCm"
            f" * ({' + '.join(terms)})")


def wave_height_expression(constants):
    terms = [f"{term(constants['waveAmplitudeShare'][index])} * sin({wave_phase(constants, index, 'Particles.TDWaterRestXY')})"
             for index in range(WAVE_COUNT)]
    return f"Particles.TDWaterWaveAmplitudeCm * ({' + '.join(terms)})"


def ripple_height_expression():
    density_ripple = "clamp($D.g * 2.0 - 1.0, -1.0, 1.0)"
    layer_ripple = f"clamp($S.b * 2.0 - 1.0, -1.0, 1.0) + User.TDWaterPressureNormalStrength * {density_ripple}"
    return (f"lerp({density_ripple}, {layer_ripple}, saturate(User.TDWaterLayerActive))"
            " * User.TDWaterRippleHeightReferenceCm * min(Particles.TDWaterWindowFade, saturate($D.a))")


def raw_uv_outside_expression():
    return " || ".join(f"{RAW_UV}.{axis} {comparison}" for axis in ("x", "y") for comparison in ("< 0.0", "> 1.0"))


def surface_height_expression(spec):
    tile = "Particles.TDWaterTileHeightCm" if has_tilemap(spec) else "0.0"
    return f"Particles.TDWaterBaseZCm + Particles.TDWaterWaveHeightCm + Particles.TDWaterRippleHeightCm + {tile}"


def surface_spawn_steps(spec, constants):
    steps = [set_step(SPAWN, "spawn_water_uv", expression_entry(RAW_UV, V2, raw_uv_expression(spec))),
             set_step(SPAWN, "spawn_plane_uv", expression_entry(toolset.PLANE_UV_ATTRIBUTE, V2, f"saturate({RAW_UV})"))]
    steps.extend(drive.sample_steps(SPAWN, "spawn_density_sample", "User.DensityPressureTexture", "$D"))
    steps.extend(drive.sample_steps(SPAWN, "spawn_surface_sample", "User.WaterSurfaceTexture", "$S"))
    steps.append(set_step(SPAWN, "spawn_base_height", expression_entry("Particles.TDWaterBaseZCm", F, base_height_expression(constants))))
    return steps


def surface_update_steps(spec, constants):
    tilemap = has_tilemap(spec)
    uv_entries = [expression_entry(RAW_UV, V2, raw_uv_expression(spec))]
    if tilemap:
        uv_entries.append(expression_entry(TILE_UV, V2, tile_uv_expression()))
    steps = [set_step(UPDATE, "update_water_uv", *uv_entries),
             set_step(UPDATE, "update_plane_uv", expression_entry(toolset.PLANE_UV_ATTRIBUTE, V2, f"saturate({RAW_UV})"))]
    steps.extend(drive.sample_steps(UPDATE, "update_velocity_sample", "User.VelocityTexture", "$V"))
    steps.extend(drive.sample_steps(UPDATE, "update_density_sample", "User.DensityPressureTexture", "$D"))
    steps.extend(drive.sample_steps(UPDATE, "update_surface_sample", "User.WaterSurfaceTexture", "$S"))
    if tilemap:
        steps.extend(tile_sample_steps("update_tile_sample"))
    input_entries = [expression_entry("Particles.TDWaterWindowFade", F, window_fade_expression()),
                     expression_entry("Particles.TDWaterWaveBaseAngle", F, "radians(User.TDWaterPassiveWaveDirectionDegrees)"),
                     expression_entry("Particles.TDWaterBaseZCm", F, base_height_expression(constants))]
    if tilemap:
        input_entries.append(expression_entry("Particles.TDWaterTileHeightCm", F, tile_height_expression()))
    steps.append(set_step(UPDATE, "update_surface_inputs", *input_entries))
    steps.append(set_step(UPDATE, "update_wave_amplitude",
                          expression_entry("Particles.TDWaterWaveAmplitudeCm", F, "User.TDWaterPassiveWaveAmplitudeCm * (1.0 - User.TDWaterPassiveWaveSimDamping * Particles.TDWaterWindowFade)")))
    steps.append(set_step(UPDATE, "update_wave_rest", expression_entry("Particles.TDWaterRestXY", V2, wave_rest_expression(constants))))
    steps.append(set_step(UPDATE, "update_wave_height",
                          expression_entry("Particles.TDWaterWaveHeightCm", F, wave_height_expression(constants)),
                          expression_entry("Particles.TDWaterRippleHeightCm", F, ripple_height_expression())))
    steps.append(set_step(UPDATE, "update_surface_height", expression_entry("Particles.TDWaterSurfaceZCm", F, surface_height_expression(spec))))
    return steps


def spawn_roll_step():
    return set_step(SPAWN, "spawn_random",
                    ("Particles.TDWaterSpawnRoll", V2, "random", (drive.RANDOM_RANGE_VECTOR2D, (0.0, 0.0), (1.0, 1.0))),
                    ("Particles.TDWaterLifeRoll", F, "random", (drive.RANDOM_RANGE_FLOAT, 0.0, 1.0)),
                    ("Particles.TDWaterRejectRoll", F, "random", (drive.RANDOM_RANGE_FLOAT, 0.0, 1.0)))


def plane_position_expression():
    return ("User.SimCenterWorld + User.PlaneAxisU * ((Particles.TDWaterSpawnUv.x - 0.5) * User.SimExtentsCm.x)"
            " + User.PlaneAxisV * ((Particles.TDWaterSpawnUv.y - 0.5) * User.SimExtentsCm.y)")


def rate_steps(role):
    if role == "debris":
        rate = ("User.TDDebrisCount * (2.0 / max(User.TDDebrisLifetimeMinSeconds + User.TDDebrisLifetimeMaxSeconds, 0.1)"
                " + User.TDDebrisPrefillRateScale) / max(User.TDWaterCoverage, 0.05)")
    else:
        rate = ("User.TDWhitewaterSpawnPerM2PerSec * User.SimExtentsCm.x * User.SimExtentsCm.y"
                " * (User.TDWhitewaterSpawnUvMax.x - User.TDWhitewaterSpawnUvMin.x)"
                " * (User.TDWhitewaterSpawnUvMax.y - User.TDWhitewaterSpawnUvMin.y) * 0.0001")
    return [{"script": EMITTER_UPDATE, "kind": "expression_module", "label": "spawn_rate", "module": drive.SPAWN_RATE_MODULE,
             "input": "SpawnRate", "expression": f"User.TDWaterCoverage > 0.0 ? {rate} : 0.0"}]


def reject_steps(role):
    if role == "debris":
        weight = ("saturate(($S.r * User.TDWaterDepthReferenceCm - User.TDDebrisMinDepthCm) / max(User.TDDebrisMinDepthCm, 1.0))"
                  " * step(0.5, $D.a)")
        height = "Particles.TDWaterBaseZCm + User.TDDebrisFloatOffsetCm"
    else:
        weight = ("saturate(($D.r - User.TDWhitewaterSpawnThreshold) / max(1.0 - User.TDWhitewaterSpawnThreshold, 0.001)) * $D.a")
        height = "Particles.TDWaterBaseZCm + User.TDWhitewaterLiftCm"
    return [set_step(SPAWN, "spawn_weight", expression_entry("Particles.TDWaterSpawnWeight", F, weight)),
            set_step(SPAWN, "spawn_reject",
                     expression_entry("Particles.Lifetime", F, "Particles.TDWaterRejectRoll < Particles.TDWaterSpawnWeight ? Particles.Lifetime : 0.0"),
                     expression_entry("Particles.Position", toolset.POSITION_TYPE, f"float3(Particles.Position.x, Particles.Position.y, {height})"))]


def surface_role_spawn_steps(spec, constants):
    role = role_of(spec)
    prefix = "TDDebris" if role == "debris" else "TDWhitewater"
    place_entries = [
        expression_entry("Particles.Position", toolset.POSITION_TYPE, plane_position_expression()),
        expression_entry("Particles.Lifetime", F, f"lerp(User.{prefix}LifetimeMinSeconds, User.{prefix}LifetimeMaxSeconds, Particles.TDWaterLifeRoll)"),
    ]
    if role == "debris":
        place_entries.append(("Particles.TDWaterExitSeconds", F, "value", 0.0))
        place_entries.append(("Particles.Scale", V3, "value", (0.0, 0.0, 0.0)))
        place_entries.append(expression_entry("Particles.Color", C, "User.TDDebrisColor"))
    else:
        place_entries.append(expression_entry("Particles.SpriteSize", V2, "float2(User.TDWhitewaterSpriteSizeCm, User.TDWhitewaterSpriteSizeCm)"))
        place_entries.append(expression_entry("Particles.Color", C, "float4(User.TDWhitewaterColor.rgb, 0.0)"))
    steps = [spawn_roll_step(),
             set_step(SPAWN, "spawn_uv", expression_entry("Particles.TDWaterSpawnUv", V2,
                                                          f"lerp(User.{prefix}SpawnUvMin, User.{prefix}SpawnUvMax, Particles.TDWaterSpawnRoll)")),
             set_step(SPAWN, "spawn_place", *place_entries)]
    steps.extend(surface_spawn_steps(spec, constants))
    steps.extend(reject_steps(role))
    return steps


def debris_update_steps(spec, constants):
    tile = "Particles.TDWaterTileHeightCm" if has_tilemap(spec) else "0.0"
    outside = f"{raw_uv_outside_expression()} || $S.r * User.TDWaterDepthReferenceCm < User.TDDebrisMinDepthCm || $D.a < 0.5"
    steps = surface_update_steps(spec, constants)
    steps.append(set_step(UPDATE, "update_debris_target",
                          expression_entry("Particles.TDWaterTargetZCm", F,
                                           "Particles.TDWaterBaseZCm + User.TDDebrisBobbingHeightScale"
                                           f" * (Particles.TDWaterWaveHeightCm + Particles.TDWaterRippleHeightCm + {tile}) + User.TDDebrisFloatOffsetCm"),
                          expression_entry("Particles.TDWaterOutside", F, f"({outside}) ? 1.0 : 0.0")))
    steps.append(set_step(UPDATE, "update_motion", expression_entry(
        "Particles.Velocity", V3,
        f"float3(lerp(Particles.Velocity.xy, {FLOW_XY} * User.TDDebrisFlowFollowScale, 1.0 - exp(-User.TDDebrisFlowDragPerSec * Engine.DeltaTime)),"
        " (Particles.TDWaterTargetZCm - Particles.Position.z) * (1.0 - exp(-User.TDDebrisHeightFollowPerSec * Engine.DeltaTime))"
        " / max(Engine.DeltaTime, 0.0001))")))
    steps.append(set_step(UPDATE, "update_exit", expression_entry(
        "Particles.TDWaterExitSeconds", F,
        "(Particles.TDWaterExitSeconds > 0.0 || Particles.TDWaterOutside > 0.5) ? Particles.TDWaterExitSeconds + Engine.DeltaTime : 0.0")))
    steps.append(set_step(UPDATE, "update_shape",
                          expression_entry("Particles.Scale", V3,
                                           "float3(1.0, 1.0, 1.0) * ((User.TDDebrisMeshScaleCm / 100.0)"
                                           " * saturate(min(Particles.Age, Particles.Lifetime - Particles.Age) * User.TDDebrisScaleFadePerSec)"
                                           " * saturate(1.0 - Particles.TDWaterExitSeconds * User.TDDebrisScaleFadePerSec))"),
                          expression_entry("Particles.Color", C, "User.TDDebrisColor")))
    steps.append(set_step(UPDATE, "update_kill", expression_entry(
        "Particles.Lifetime", F,
        "Particles.TDWaterExitSeconds * User.TDDebrisScaleFadePerSec >= 1.0 ? min(Particles.Age, Particles.Lifetime) : Particles.Lifetime")))
    steps.append({"script": UPDATE, "kind": "module", "label": "solve_forces", "module": drive.SOLVE_FORCES_MODULE})
    return steps


def whitewater_update_steps(spec, constants):
    camera_fade = ("(1.0 - saturate((length(Particles.Position - User.TDWaterCameraWorld) - User.TDWhitewaterCameraFadeStartCm)"
                   " / max(User.TDWhitewaterCameraFadeLengthCm, 1.0)))")
    steps = surface_update_steps(spec, constants)
    steps.append(set_step(UPDATE, "update_whitewater_target",
                          expression_entry("Particles.TDWaterTargetZCm", F, "Particles.TDWaterSurfaceZCm + User.TDWhitewaterLiftCm")))
    steps.append(set_step(UPDATE, "update_motion", expression_entry(
        "Particles.Velocity", V3,
        f"float3(lerp(Particles.Velocity.xy, {FLOW_XY}, 1.0 - exp(-User.TDWhitewaterVelocityDragPerSec * Engine.DeltaTime)),"
        " (Particles.TDWaterTargetZCm - Particles.Position.z) * (1.0 - exp(-User.TDWhitewaterHeightFollowPerSec * Engine.DeltaTime))"
        " / max(Engine.DeltaTime, 0.0001))")))
    steps.append(set_step(UPDATE, "update_shape",
                          expression_entry("Particles.SpriteSize", V2,
                                           "float2(User.TDWhitewaterSpriteSizeCm, User.TDWhitewaterSpriteSizeCm"
                                           " * (1.0 + User.TDWhitewaterStretchPerCmPerSec * length(Particles.Velocity)))"),
                          expression_entry("Particles.Color", C,
                                           "float4(User.TDWhitewaterColor.rgb, User.TDWhitewaterColor.a * saturate($D.r * User.TDWhitewaterDensityToAlpha)"
                                           " * saturate(min(Particles.Age, Particles.Lifetime - Particles.Age) * User.TDWhitewaterFadePerSec)"
                                           f" * {camera_fade})")))
    steps.append(set_step(UPDATE, "update_kill", expression_entry(
        "Particles.Lifetime", F, f"({raw_uv_outside_expression()}) ? min(Particles.Age, Particles.Lifetime) : Particles.Lifetime")))
    steps.append({"script": UPDATE, "kind": "module", "label": "solve_forces", "module": drive.SOLVE_FORCES_MODULE})
    return steps


def splash_burst_expression():
    return ("(int)(User.TDSplashDropletsPerImpact * saturate(User.TDSplashImpactSpeedCmPerSec"
            " / max(User.TDSplashFullCountSpeedCmPerSec, 1.0)) + 0.5)")


def splash_emitter_steps():
    return [
        {"script": EMITTER_UPDATE, "kind": "state_enums", "label": "loop_once", "module_fragment": "EmitterState",
         "enums": (drive.LIFE_CYCLE_SELF, drive.LOOP_BEHAVIOR_ONCE)},
        {"script": EMITTER_UPDATE, "kind": "expression_module", "label": "spawn_burst", "module": drive.SPAWN_BURST_MODULE,
         "input": "Spawn Count", "expression": splash_burst_expression()},
    ]


def splash_spawn_steps():
    is_crown = "Particles.TDSplashIsCrown > 0.5"
    origin = f"({drive.MASS_ORIGIN}).xy"
    radius = f"User.TDSplashImpactRadiusCm * User.TDSplashCrownRadiusRatio * ({is_crown} ? 1.0 : Particles.TDSplashRadialRoll)"
    return [
        set_step(SPAWN, "spawn_random",
                 ("Particles.TDSplashAngleRoll", F, "random", (drive.RANDOM_RANGE_FLOAT, 0.0, 1.0)),
                 ("Particles.TDSplashRoleRoll", F, "random", (drive.RANDOM_RANGE_FLOAT, 0.0, 1.0)),
                 ("Particles.TDSplashLifeRoll", F, "random", (drive.RANDOM_RANGE_FLOAT, 0.0, 1.0)),
                 ("Particles.TDSplashRadialRoll", F, "random", (drive.RANDOM_RANGE_FLOAT, 0.0, 1.0)),
                 ("Particles.TDSplashSpeedRoll", F, "random", (drive.RANDOM_RANGE_FLOAT, -1.0, 1.0))),
        set_step(SPAWN, "spawn_role",
                 expression_entry("Particles.TDSplashIsCrown", F, "Particles.TDSplashRoleRoll < User.TDSplashCrownShare ? 1.0 : 0.0"),
                 expression_entry("Particles.TDSplashDirection", V3,
                                  f"float3(cos({number(TWO_PI)} * Particles.TDSplashAngleRoll), sin({number(TWO_PI)} * Particles.TDSplashAngleRoll), 0.0)"),
                 expression_entry("Particles.TDSplashSpeedCmPerSec", F, "User.TDSplashImpactSpeedCmPerSec * (1.0 + User.TDSplashSpeedJitter * Particles.TDSplashSpeedRoll)")),
        set_step(SPAWN, "spawn_place",
                 expression_entry("Particles.Position", toolset.POSITION_TYPE,
                                  f"float3({origin}, User.TDSplashSurfaceHeightCm + User.TDSplashSpawnLiftCm) + Particles.TDSplashDirection * ({radius})"),
                 expression_entry("Particles.Velocity", V3,
                                  f"Particles.TDSplashDirection * (Particles.TDSplashSpeedCmPerSec * ({is_crown} ? User.TDSplashCrownRadialSpeedRatio : User.TDSplashDropletRadialSpeedRatio))"
                                  " + float3(User.TDSplashImpactVelocity.xy * User.TDSplashCarryRatio,"
                                  f" Particles.TDSplashSpeedCmPerSec * ({is_crown} ? User.TDSplashCrownUpSpeedRatio : User.TDSplashDropletUpSpeedRatio))"),
                 expression_entry("Particles.Lifetime", F, "lerp(User.TDSplashLifetimeMinSeconds, User.TDSplashLifetimeMaxSeconds, Particles.TDSplashLifeRoll)"),
                 expression_entry("Particles.TDSplashWidthCm", F,
                                  f"{is_crown} ? 2.0 * User.TDSplashImpactRadiusCm * User.TDSplashCrownThicknessRatio : User.TDSplashDropletSizeCm"),
                 ("Particles.TDSplashContact", F, "value", 0.0)),
        set_step(SPAWN, "spawn_shape",
                 expression_entry("Particles.SpriteSize", V2, "float2(Particles.TDSplashWidthCm, Particles.TDSplashWidthCm)"),
                 expression_entry("Particles.Color", C, "float4(User.TDSplashColor.rgb, User.TDSplashColor.a)")),
    ]


def splash_contact_step(spec):
    water = spec[WATER_SPEC_KEY]
    values = {"Channel": water["data_channel"], "AllocationMode": "Static", "AllocationCount": water["allocation_count"],
              "bPublishToGame": True, "bPublishToCPU": False, "bPublishToGPU": False}
    links = (("Position", ("Particles.Position", toolset.POSITION_TYPE)), ("Velocity", ("Particles.Velocity", V3)))
    density = "User.TDSplashContactWhitewater"
    expressions = [("RadiusCm", "User.TDSplashContactRadiusCm")]
    if SPLASH_EXECUTE_BY_EXPRESSION:
        expressions.insert(0, ("bExecute", "Particles.TDSplashContact > 0.5"))
    else:
        density = f"{density} * saturate(Particles.TDSplashContact)"
    expressions.extend((("DensityStrength", density), ("HeatStrength", "0.0")))
    return {"script": UPDATE, "kind": "data_interface_module", "label": "splash_ndc_append", "module": drive.MASS_NDC_APPEND_MODULE,
            "data_interface_input": "DataChannelWriter", "data_interface_values": values, "links": links, "expressions": tuple(expressions)}


def splash_update_steps(spec):
    return [
        set_step(UPDATE, "update_motion", expression_entry(
            "Particles.Velocity", V3,
            "Particles.Velocity * exp(-User.TDSplashDragPerSec * Engine.DeltaTime) - float3(0.0, 0.0, User.TDSplashGravityCmPerSec2 * Engine.DeltaTime)")),
        {"script": UPDATE, "kind": "module", "label": "solve_forces", "module": drive.SOLVE_FORCES_MODULE},
        set_step(UPDATE, "update_contact", expression_entry(
            "Particles.TDSplashContact", F,
            "(Particles.Position.z <= User.TDSplashSurfaceHeightCm && Particles.Velocity.z < 0.0 && Particles.Age >= User.TDSplashContactMinAgeSeconds)"
            " ? 1.0 : 0.0")),
        splash_contact_step(spec),
        set_step(UPDATE, "update_kill", expression_entry(
            "Particles.Lifetime", F, "Particles.TDSplashContact > 0.5 ? min(Particles.Age, Particles.Lifetime) : Particles.Lifetime")),
        set_step(UPDATE, "update_shape",
                 expression_entry("Particles.SpriteSize", V2,
                                  "float2(Particles.TDSplashWidthCm, Particles.TDSplashWidthCm"
                                  " * (1.0 + User.TDSplashStretchPerCmPerSec * length(Particles.Velocity)))"),
                 expression_entry("Particles.Color", C,
                                  "float4(User.TDSplashColor.rgb, User.TDSplashColor.a"
                                  " * saturate((Particles.Lifetime - Particles.Age) * User.TDSplashFadePerSec))")),
    ]


def build_stack_plan(spec):
    role = role_of(spec)
    if role == "splash":
        return splash_emitter_steps() + splash_spawn_steps() + splash_update_steps(spec)
    constants = read_wave_constants()
    update_steps = debris_update_steps if role == "debris" else whitewater_update_steps
    return rate_steps(role) + surface_role_spawn_steps(spec, constants) + update_steps(spec, constants)
