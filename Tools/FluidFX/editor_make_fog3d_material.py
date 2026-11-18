"""실행 환경: TDGame 언리얼 에디터 안에서 3D 안개 그리기 재질(MD_VOLUME·BLEND_ADDITIVE, Custom 노드 1개)을 만든다. 변형 flat = M_TD_Fog3DVolume(TDFluidGpuFog3DDensity.ush), ground = M_TD_Fog3DVolumeGround(TDFluidGpuFog3DDensityGround.ush, 땅 높이 텍스처 Fog3DGroundHeight·벡터 Fog3DGround 추가, include 방식만). 먼저 editor_make_fluid_volume_material.py 가 T_TD_FluidNoiseVolume 을 만들어 두어야 한다. unreal 모듈 없이 import 하면 Custom 코드·입력 이름·파라미터 표를 돌려주는 순수 함수만 쓴다(check_fog3d_material_hlsl.py 가 사용).
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fog3d_material.py (PowerShell. 요청 Saved/FluidFX/fog3d_material_request.json: {"variant": "flat"|"ground"} 기본 flat, 에셋이 있을 때 다시 지으려면 "overwrite": true, flat 만 include 실패 시 "code_mode": "inline"(ground 에 inline 을 요청하면 중단))
출력: flat /Game/FluidFX/Materials/M_TD_Fog3DVolume + Saved/FluidFX/fog3d_material.json, ground /Game/FluidFX/Materials/M_TD_Fog3DVolumeGround + Saved/FluidFX/fog3d_material_ground.json(code_mode, 다시 읽은 include_file_paths, 앞단 경로 substrate|legacy, 재질 통계, .ush sha256, Custom 코드 sha256, 파라미터 표)
상태: flat 은 include 방식으로 에디터에서 만들어졌다(fog3d_material.json, 2026-11. inline 방식은 실제 재질 컴파일로 확인된 적 없음). ground 는 에디터 실행 전: 미검증(check_fog3d_material_hlsl.py 의 dxc 컴파일·가짜 unreal 그래프 만들기만).
"""
import hashlib
import json
import os
import re

try:
    import unreal
except ImportError:
    unreal = None


FLUID_ROOT = "/Game/FluidFX"
MATERIAL_DIR = f"{FLUID_ROOT}/Materials"
TEXTURE_DIR = f"{FLUID_ROOT}/Textures"
MASTER_NAME = "M_TD_Fog3DVolume"
MASTER_PATH = f"{MATERIAL_DIR}/{MASTER_NAME}"
STATE_VOLUME_DEFAULT_PATH = f"{TEXTURE_DIR}/T_TD_FluidNoiseVolume"
SHADER_MODULE_VIRTUAL_ROOT = "/TDFluidGpu"
SHADER_MODULE_RELATIVE_PATH = ("Source", "TDFluidGpu", "Shaders")
SHADER_RELATIVE_PATH = SHADER_MODULE_RELATIVE_PATH + ("Private", "TDFluidGpuFog3DDensity.ush")
INCLUDE_FILE_PATH = "/TDFluidGpu/Private/TDFluidGpuFog3DDensity.ush"
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fog3d_material_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fog3d_material.json")
PARAMETER_GROUP = "TDFog3D"
CODE_MODE_INCLUDE = "include"
CODE_MODE_INLINE = "inline"
CODE_MODES = (CODE_MODE_INCLUDE, CODE_MODE_INLINE)
INLINE_SCOPE_TYPE_NAME = "TDFog3DInlineScope"
INLINE_SCOPE_VARIABLE_NAME = "tdFog3DScope"

BODY_LINES = (
    "float3 tdUvw = TDFog3DStateUvw(Fog3DZonePositionCm, Fog3DWindow, Fog3DGrid);",
    "float4 tdState = Texture3DSampleLevel(Fog3DStateVolume, Fog3DStateVolumeSampler, tdUvw, 0);",
    "float tdDensity = TDFog3DDensity(Fog3DZonePositionCm, tdState, Fog3DWindow, Fog3DGrid, Fog3DActive, Fog3DBaseShape, Fog3DZone, Fog3DFineNoise, Fog3DStainNoise);",
    "return float4(Fog3DExtinctionPerM * tdDensity.xxx, tdDensity);",
)

ZONE_POSITION_INPUT_NAME = "Fog3DZonePositionCm"
STATE_VOLUME_NAME = "Fog3DStateVolume"
CUSTOM_INPUTS = (
    (ZONE_POSITION_INPUT_NAME, "float3"),
    (STATE_VOLUME_NAME, "Texture3D"),
    ("Fog3DWindow", "float4"),
    ("Fog3DGrid", "float4"),
    ("Fog3DActive", "float"),
    ("Fog3DBaseShape", "float4"),
    ("Fog3DZone", "float4"),
    ("Fog3DFineNoise", "float4"),
    ("Fog3DStainNoise", "float4"),
    ("Fog3DExtinctionPerM", "float"),
)
CUSTOM_INPUT_NAMES = tuple(name for name, _ in CUSTOM_INPUTS)
TEXTURE_PARAMETER_NAMES = (STATE_VOLUME_NAME,)
SAMPLER_NAMES = (STATE_VOLUME_NAME + "Sampler",)
ENGINE_SYMBOLS = ("Texture3DSampleLevel",)
VECTOR_PARAMETER_DEFAULTS = (
    ("Fog3DWindow", (0.0, 0.0, 0.0, 256.0)),
    ("Fog3DGrid", (15.625, 30.0, 8.0, 8.0)),
    ("Fog3DBaseShape", (0.8, 90.0, 45.0, 0.0)),
    ("Fog3DZone", (1000.0, 1000.0, 500.0, 500.0)),
    ("Fog3DFineNoise", (0.2, 50.0, 1.0, 0.0)),
    ("Fog3DStainNoise", (0.0, 200.0, 0.0, 0.0)),
    ("Fog3DAlbedo", (0.8, 0.85, 0.95, 1.0)),
)
SCALAR_PARAMETER_DEFAULTS = (
    ("Fog3DActive", 0.0),
    ("Fog3DExtinctionPerM", 0.2),
)
FLOAT4_VECTOR_NAMES = tuple(name for name, _ in VECTOR_PARAMETER_DEFAULTS if name != "Fog3DAlbedo")
ALBEDO_NAME = "Fog3DAlbedo"
CPP_PARAMETER_NAMES = (
    "Fog3DStateVolume", "Fog3DWindow", "Fog3DGrid", "Fog3DActive",
    "Fog3DBaseShape", "Fog3DZone", "Fog3DFineNoise", "Fog3DStainNoise",
)
DEFAULT_TEXTURE_PATHS = ("/Engine/EngineResources/Black", "/Engine/EngineResources/DefaultTexture")
SUBSTRATE_PIN_CANDIDATES = {
    "albedo": ("Albedo",),
    "extinction": ("Extinction",),
}
REQUIRED_UNREAL_NAMES = (
    "EditorAssetLibrary.load_asset",
    "EditorAssetLibrary.save_loaded_asset",
    "EditorAssetLibrary.does_asset_exist",
    "EditorLoadingAndSavingUtils.get_dirty_map_packages",
    "EditorLoadingAndSavingUtils.get_dirty_content_packages",
    "SystemLibrary.get_project_directory",
    "MaterialEditingLibrary.create_material_expression",
    "MaterialEditingLibrary.connect_material_expressions",
    "MaterialEditingLibrary.connect_material_property",
    "MaterialEditingLibrary.delete_all_material_expressions",
    "MaterialEditingLibrary.delete_material_expression",
    "MaterialEditingLibrary.recompile_material",
    "MaterialEditingLibrary.get_vector_parameter_names",
    "MaterialEditingLibrary.get_scalar_parameter_names",
    "MaterialEditingLibrary.get_texture_parameter_names",
    "Material",
    "MaterialFactoryNew",
    "MaterialExpressionCustom",
    "CustomInput",
    "CustomMaterialOutputType.CMOT_FLOAT4",
    "MaterialExpressionWorldPosition",
    "MaterialExpressionObjectPositionWS",
    "MaterialExpressionSubtract",
    "MaterialExpressionTruncateLWC",
    "MaterialExpressionTextureObjectParameter",
    "MaterialExpressionVectorParameter",
    "MaterialExpressionScalarParameter",
    "MaterialExpressionComponentMask",
    "MaterialExpressionSubstrateVolumetricFogCloudBSDF",
    "MaterialDomain.MD_VOLUME",
    "BlendMode.BLEND_ADDITIVE",
    "MaterialProperty.MP_FRONT_MATERIAL",
    "MaterialProperty.MP_BASE_COLOR",
    "MaterialProperty.MP_SUBSURFACE_COLOR",
    "MaterialSamplerType.SAMPLERTYPE_COLOR",
    "MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR",
    "TextureCompressionSettings.TC_DEFAULT",
    "VolumeTexture",
)

VARIANT_FLAT = "flat"
VARIANT_GROUND = "ground"
VARIANT_NAMES = (VARIANT_FLAT, VARIANT_GROUND)
GROUND_MASTER_NAME = "M_TD_Fog3DVolumeGround"
GROUND_SHADER_RELATIVE_PATH = SHADER_MODULE_RELATIVE_PATH + ("Private", "TDFluidGpuFog3DDensityGround.ush")
GROUND_INCLUDE_FILE_PATH = "/TDFluidGpu/Private/TDFluidGpuFog3DDensityGround.ush"
GROUND_REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fog3d_material_ground.json")
GROUND_HEIGHT_NAME = "Fog3DGroundHeight"
GROUND_VECTOR_NAME = "Fog3DGround"
GROUND_HEIGHT_DEFAULT_PATH = "/Engine/EngineResources/Black"
GROUND_BODY_LINES = (
    "float2 tdGroundUv = TDFog3DGroundUv(Fog3DZonePositionCm.xy, Fog3DGround, Fog3DGrid);",
    "float tdGroundHeight = Texture2DSampleLevel(Fog3DGroundHeight, Fog3DGroundHeightSampler, tdGroundUv, 0).r;",
    "float3 tdUvw = TDFog3DGroundStateUvw(Fog3DZonePositionCm, Fog3DWindow, Fog3DGrid, tdGroundHeight);",
    "float4 tdState = Texture3DSampleLevel(Fog3DStateVolume, Fog3DStateVolumeSampler, tdUvw, 0);",
    "float tdDensity = TDFog3DGroundDensity(Fog3DZonePositionCm, tdState, Fog3DWindow, Fog3DGrid, Fog3DActive, Fog3DBaseShape, Fog3DZone, Fog3DFineNoise, Fog3DStainNoise, tdGroundHeight);",
    "return float4(Fog3DExtinctionPerM * tdDensity.xxx, tdDensity);",
)
GROUND_CUSTOM_INPUTS = CUSTOM_INPUTS + ((GROUND_HEIGHT_NAME, "Texture2D"), (GROUND_VECTOR_NAME, "float4"))
GROUND_VECTOR_PARAMETER_DEFAULTS = VECTOR_PARAMETER_DEFAULTS + ((GROUND_VECTOR_NAME, (0.0, 0.0, 1.0, 1.0)),)
GROUND_REQUIRED_UNREAL_NAMES = ("Texture2D",)


class TDFog3DMaterialVariant:
    def __init__(self, name, master_name, shader_relative_path, include_file_path, report_relative_path, body_lines, custom_inputs,
                 texture_parameter_names, engine_symbols, vector_parameter_defaults, cpp_parameter_names, code_modes, description):
        self.name = name
        self.master_name = master_name
        self.master_path = f"{MATERIAL_DIR}/{master_name}"
        self.shader_relative_path = shader_relative_path
        self.include_file_path = include_file_path
        self.report_relative_path = report_relative_path
        self.body_lines = body_lines
        self.custom_inputs = custom_inputs
        self.custom_input_names = tuple(input_name for input_name, _ in custom_inputs)
        self.texture_parameter_names = texture_parameter_names
        self.sampler_names = tuple(texture_name + "Sampler" for texture_name in texture_parameter_names)
        self.engine_symbols = engine_symbols
        self.vector_parameter_defaults = vector_parameter_defaults
        self.scalar_parameter_defaults = SCALAR_PARAMETER_DEFAULTS
        self.float4_vector_names = tuple(vector_name for vector_name, _ in vector_parameter_defaults if vector_name != ALBEDO_NAME)
        self.cpp_parameter_names = cpp_parameter_names
        self.code_modes = code_modes
        self.description = description


FLAT_VARIANT = TDFog3DMaterialVariant(
    VARIANT_FLAT, MASTER_NAME, SHADER_RELATIVE_PATH, INCLUDE_FILE_PATH, REPORT_RELATIVE_PATH, BODY_LINES, CUSTOM_INPUTS,
    TEXTURE_PARAMETER_NAMES, ENGINE_SYMBOLS, VECTOR_PARAMETER_DEFAULTS, CPP_PARAMETER_NAMES, CODE_MODES, "TDFog3DVolume")
GROUND_VARIANT = TDFog3DMaterialVariant(
    VARIANT_GROUND, GROUND_MASTER_NAME, GROUND_SHADER_RELATIVE_PATH, GROUND_INCLUDE_FILE_PATH, GROUND_REPORT_RELATIVE_PATH, GROUND_BODY_LINES,
    GROUND_CUSTOM_INPUTS, TEXTURE_PARAMETER_NAMES + (GROUND_HEIGHT_NAME,), ENGINE_SYMBOLS + ("Texture2DSampleLevel",), GROUND_VECTOR_PARAMETER_DEFAULTS,
    CPP_PARAMETER_NAMES + (GROUND_HEIGHT_NAME, GROUND_VECTOR_NAME), (CODE_MODE_INCLUDE,), "TDFog3DVolumeGround")
MATERIAL_VARIANTS = {FLAT_VARIANT.name: FLAT_VARIANT, GROUND_VARIANT.name: GROUND_VARIANT}

IDENTIFIER_PATTERN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
ENGINE_INCLUDE_PATTERN = re.compile(r'^[ \t]*#[ \t]*include[ \t]+"(/Engine/[^"]*)"[ \t]*$', re.MULTILINE)
ANY_INCLUDE_PATTERN = re.compile(r'^[ \t]*#[ \t]*include\b.*$', re.MULTILINE)
PRAGMA_ONCE_PATTERN = re.compile(r'^[ \t]*#[ \t]*pragma[ \t]+once[ \t]*$', re.MULTILINE)
STATIC_CONSTANT_PATTERN = re.compile(r'^[ \t]*static[ \t]+const[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*=[ \t]*([^;{}\n]+);[ \t]*$', re.MULTILINE)
ANY_STATIC_CONSTANT_PATTERN = re.compile(r'^[ \t]*static[ \t]+const\b.*$', re.MULTILINE)
ABSOLUTE_PATH_PATTERN =re.compile(r"[A-Za-z]:[\\/]")
BODY_FUNCTION_PATTERN = re.compile(r"\bTDFog3D[A-Za-z0-9_]*")


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


def normalize_text(text):
    return text.replace("\r\n", "\n").strip("\n") + "\n"


def find_variant(variant_name):
    variant = MATERIAL_VARIANTS.get(variant_name)
    if variant is None:
        raise ValueError(f"알 수 없는 variant: {variant_name} (가능: {', '.join(VARIANT_NAMES)})")
    return variant


def read_density_source(project_root, variant=FLAT_VARIANT):
    path = os.path.join(project_root, *variant.shader_relative_path)
    if not os.path.isfile(path):
        raise FileNotFoundError(f"그리는 식 셰이더 파일 없음: {path}")
    with open(path, encoding="utf-8") as shader_file:
        return normalize_text(shader_file.read())


def find_shader_text_problems(text):
    problems = []
    if "//" in text or "/*" in text:
        problems.append("주석이 있음")
    if ABSOLUTE_PATH_PATTERN.search(text):
        problems.append("절대 경로가 있음")
    if not text.isascii():
        problems.append("ASCII 가 아닌 문자가 있음")
    if "\r" in text:
        problems.append("CRLF 줄바꿈")
    return problems


def body_function_names():
    names = []
    for line in BODY_LINES:
        for name in BODY_FUNCTION_PATTERN.findall(line):
            if name not in names:
                names.append(name)
    return names


def split_inline_includes(shader_text):
    stripped_lines = [match.group(0).strip() for match in ENGINE_INCLUDE_PATTERN.finditer(shader_text)]
    remaining = ENGINE_INCLUDE_PATTERN.sub("", shader_text)
    remaining = PRAGMA_ONCE_PATTERN.sub("", remaining)
    return remaining, stripped_lines


def convert_static_constants_to_macros(scope_text):
    matches = list(STATIC_CONSTANT_PATTERN.finditer(scope_text))
    macro_lines = "".join(f"#define {match.group(1)} ({match.group(2).strip()})\n" for match in matches)
    return STATIC_CONSTANT_PATTERN.sub("", scope_text), macro_lines, [match.group(1) for match in matches]


def prepare_inline_scope(shader_text):
    remaining, _ = split_inline_includes(shader_text)
    scope_text, constant_macro_lines, constant_names = convert_static_constants_to_macros(remaining)
    return scope_text.strip("\n"), constant_macro_lines, constant_names


def build_inline_code(shader_text):
    scope_body, constant_macro_lines, constant_names = prepare_inline_scope(shader_text)
    function_names = body_function_names()
    macro_lines = "".join(f"#define {name} {INLINE_SCOPE_VARIABLE_NAME}.{name}\n" for name in function_names)
    undefine_lines = "".join(f"#undef {name}\n" for name in reversed(function_names + constant_names))
    return (
        constant_macro_lines
        + f"struct {INLINE_SCOPE_TYPE_NAME}\n{{\n{scope_body}\n}};\n"
        + f"{INLINE_SCOPE_TYPE_NAME} {INLINE_SCOPE_VARIABLE_NAME};\n"
        + macro_lines
        + "\n".join(BODY_LINES)
        + "\n"
        + undefine_lines
    )


def build_custom_code(code_mode, shader_text, variant=FLAT_VARIANT):
    if code_mode in CODE_MODES and code_mode not in variant.code_modes:
        raise ValueError(f"{variant.name} 변형은 {', '.join(variant.code_modes)} 방식만 쓴다: {code_mode}")
    if code_mode == CODE_MODE_INCLUDE:
        return "\n".join(variant.body_lines) + "\n", [variant.include_file_path]
    if code_mode == CODE_MODE_INLINE:
        return build_inline_code(shader_text), []
    raise ValueError(f"알 수 없는 code_mode: {code_mode} (가능: {', '.join(CODE_MODES)})")


def find_unbalanced_brackets(code):
    pairs = {")": "(", "]": "[", "}": "{"}
    stack = []
    for index, character in enumerate(code):
        if character in "([{":
            stack.append(character)
            continue
        if character in pairs and (not stack or stack.pop() != pairs[character]):
            return f"괄호 불일치 위치 {index}"
    return f"닫히지 않은 괄호 {len(stack)}개" if stack else ""


def validate_custom_code(code_mode, code, include_paths, shader_text, variant=FLAT_VARIANT):
    problems = []
    identifiers = set(IDENTIFIER_PATTERN.findall(code))
    for label, names in (("Custom 입력", variant.custom_input_names), ("샘플러", variant.sampler_names), ("엔진 심볼", variant.engine_symbols)):
        missing = [name for name in names if name not in identifiers]
        if missing:
            problems.append(f"코드가 쓰지 않는 {label}: " + ", ".join(missing))
    bracket_problem = find_unbalanced_brackets(code)
    if bracket_problem:
        problems.append(bracket_problem)
    if "return" not in identifiers:
        problems.append("코드에 return 이 없음")
    if "//" in code or "/*" in code:
        problems.append("코드에 주석이 있음")
    problems.extend("그리는 식 셰이더 " + problem for problem in find_shader_text_problems(shader_text))
    if code_mode == CODE_MODE_INCLUDE and include_paths != [variant.include_file_path]:
        problems.append(f"include 방식인데 IncludeFilePaths 가 {include_paths}")
    if code_mode not in variant.code_modes:
        problems.append(f"{variant.name} 변형이 쓰지 않는 방식: {code_mode}")
    if code_mode == CODE_MODE_INLINE:
        if include_paths:
            problems.append(f"inline 방식인데 IncludeFilePaths 가 {include_paths}")
        scope_body, _, _ = prepare_inline_scope(shader_text)
        leftover = [line.strip() for line in ANY_INCLUDE_PATTERN.findall(scope_body)]
        if leftover:
            problems.append("inline 으로 못 넣는 include: " + ", ".join(leftover))
        unconverted = [line.strip() for line in ANY_STATIC_CONSTANT_PATTERN.findall(scope_body)]
        if unconverted:
            problems.append("inline 으로 못 넣는 static const(한 줄 스칼라 초기화만 매크로로 바꿈): " + ", ".join(unconverted))
    return problems


def find_unresolved_inputs(variant=FLAT_VARIANT):
    resolvable = {name for name, _ in variant.vector_parameter_defaults} | {name for name, _ in variant.scalar_parameter_defaults}
    resolvable |= set(variant.texture_parameter_names) | {ZONE_POSITION_INPUT_NAME}
    return sorted(set(variant.custom_input_names) - resolvable)


def code_sha256(code):
    return hashlib.sha256(code.encode("utf-8")).hexdigest()


def find_missing_unreal_names():
    missing = []
    for dotted_name in REQUIRED_UNREAL_NAMES:
        target = unreal
        for part in dotted_name.split("."):
            target = getattr(target, part, None)
            if target is None:
                missing.append(dotted_name)
                break
    return missing


def read_request():
    request_path = project_path(*REQUEST_RELATIVE_PATH)
    if not os.path.isfile(request_path):
        return {"overwrite": False, "code_mode": CODE_MODE_INCLUDE, "variant": VARIANT_FLAT}
    with open(request_path, encoding="utf-8") as request_file:
        request = json.load(request_file)
    return validate_request(request, request_path)


def validate_request(request, request_path):
    if not isinstance(request, dict):
        raise RuntimeError(f"요청 파일은 JSON 객체여야 합니다: {request_path}")
    code_mode = request.get("code_mode", CODE_MODE_INCLUDE)
    if code_mode not in CODE_MODES:
        raise RuntimeError(f"요청 파일 code_mode 는 {', '.join(CODE_MODES)} 중 하나: {code_mode}")
    variant_name = request.get("variant", VARIANT_FLAT)
    if variant_name not in VARIANT_NAMES:
        raise RuntimeError(f"요청 파일 variant 는 {', '.join(VARIANT_NAMES)} 중 하나: {variant_name}")
    if code_mode not in MATERIAL_VARIANTS[variant_name].code_modes:
        raise RuntimeError(f"{variant_name} 변형은 {', '.join(MATERIAL_VARIANTS[variant_name].code_modes)} 방식만 쓴다(요청 {code_mode}). inline 이 필요하면 멈추고 감독에게 알린다.")
    return {"overwrite": request.get("overwrite") is True, "code_mode": code_mode, "variant": variant_name}


def find_unsaved_package_names(variant=FLAT_VARIANT):
    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    dirty.extend(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    names = sorted({package.get_name() for package in dirty})
    return [name for name in names if name != variant.master_path]


def ensure_directory(directory_path):
    assets = unreal.EditorAssetLibrary
    if assets.does_directory_exist(directory_path):
        return
    if not assets.make_directory(directory_path):
        raise RuntimeError(f"폴더 생성 실패: {directory_path}")


def save_asset(asset, path):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"에셋 저장 실패: {path}")


def choose_sampler_type(texture):
    try:
        compression = texture.get_editor_property("compression_settings")
        is_srgb = bool(texture.get_editor_property("srgb"))
    except Exception:
        return None
    if compression != unreal.TextureCompressionSettings.TC_DEFAULT:
        return unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
    return unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if is_srgb else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR


def load_state_volume_default():
    texture = unreal.EditorAssetLibrary.load_asset(STATE_VOLUME_DEFAULT_PATH)
    if not isinstance(texture, unreal.VolumeTexture):
        raise RuntimeError(f"기본 볼륨 텍스처 없음(editor_make_fluid_volume_material.py 먼저): {STATE_VOLUME_DEFAULT_PATH}")
    return texture, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR


def load_ground_height_default():
    texture = unreal.EditorAssetLibrary.load_asset(GROUND_HEIGHT_DEFAULT_PATH)
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError(f"땅 높이 기본 텍스처(높이 0 = 검정) 없음: {GROUND_HEIGHT_DEFAULT_PATH}")
    sampler_type = choose_sampler_type(texture)
    if sampler_type is None:
        raise RuntimeError(f"땅 높이 기본 텍스처의 압축·sRGB 설정을 못 읽음: {GROUND_HEIGHT_DEFAULT_PATH}")
    return texture, sampler_type


def add_node(material, expression_class, x, y, **properties):
    expression = unreal.MaterialEditingLibrary.create_material_expression(material, expression_class, x, y)
    if expression is None:
        raise RuntimeError(f"재질 노드 생성 실패: {expression_class.__name__}")
    for name, value in properties.items():
        expression.set_editor_property(name, value)
    return expression


def link(source, target, target_input="", source_output=""):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(source, source_output, target, target_input):
        raise RuntimeError(f"재질 노드 연결 실패: {source.get_name()}[{source_output}] -> {target.get_name()}.{target_input}")


def try_link_first_pin(source, target, candidates):
    for candidate in candidates:
        if unreal.MaterialEditingLibrary.connect_material_expressions(source, "", target, candidate):
            return candidate
    return ""


def assign_parameter_group(expression):
    try:
        expression.set_editor_property("group", PARAMETER_GROUP)
    except Exception:
        return


def prepare_master(overwrite, variant=FLAT_VARIANT):
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(variant.master_path):
        if not overwrite:
            raise RuntimeError(f"기존 에셋 보존: {variant.master_path}")
        material = assets.load_asset(variant.master_path)
        if not isinstance(material, unreal.Material):
            raise RuntimeError(f"기존 에셋 형식 불일치: {variant.master_path}")
        unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
        return material, False
    ensure_directory(MATERIAL_DIR)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(variant.master_name, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError(f"마스터 재질 생성 실패: {variant.master_path}")
    return material, True


def build_zone_position(material, x, y):
    world_position = add_node(material, unreal.MaterialExpressionWorldPosition, x - 440, y)
    object_position = add_node(material, unreal.MaterialExpressionObjectPositionWS, x - 440, y + 55)
    difference = add_node(material, unreal.MaterialExpressionSubtract, x - 220, y)
    link(world_position, difference, "A")
    link(object_position, difference, "B")
    truncated = add_node(material, unreal.MaterialExpressionTruncateLWC, x, y)
    link(difference, truncated)
    return truncated


def build_sources(material, state_volume, variant=FLAT_VARIANT, ground_height=None):
    sources = {}
    rows = iter(range(200))

    def place():
        return -1500, -800 + next(rows) * 110

    x, y = place()
    sources[ZONE_POSITION_INPUT_NAME] = (build_zone_position(material, x, y), "")
    textures = {STATE_VOLUME_NAME: state_volume, GROUND_HEIGHT_NAME: ground_height}
    for texture_name in variant.texture_parameter_names:
        x, y = place()
        texture, sampler_type = textures[texture_name]
        node = add_node(material, unreal.MaterialExpressionTextureObjectParameter, x, y, parameter_name=texture_name, texture=texture, sampler_type=sampler_type)
        assign_parameter_group(node)
        sources[texture_name] = (node, "")
    for name, rgba in variant.vector_parameter_defaults:
        x, y = place()
        node = add_node(material, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=unreal.LinearColor(*rgba))
        assign_parameter_group(node)
        sources[name] = (node, "RGBA" if name in variant.float4_vector_names else "RGB")
    for name, value in variant.scalar_parameter_defaults:
        x, y = place()
        node = add_node(material, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)
        assign_parameter_group(node)
        sources[name] = (node, "")
    return sources


def build_custom_node(material, code, include_paths, sources, variant=FLAT_VARIANT):
    custom = add_node(material, unreal.MaterialExpressionCustom, -500, 0)
    custom.set_editor_property("description", variant.description)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    inputs = []
    for name in variant.custom_input_names:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        inputs.append(custom_input)
    custom.set_editor_property("inputs", inputs)
    custom.set_editor_property("code", code)
    custom.set_editor_property("include_file_paths", list(include_paths))
    stored_names = [str(item.get_editor_property("input_name")) for item in custom.get_editor_property("inputs")]
    if stored_names != list(variant.custom_input_names):
        raise RuntimeError("Custom 노드 입력 이름 설정 확인 실패: " + ", ".join(stored_names))
    stored_includes = [str(path) for path in custom.get_editor_property("include_file_paths")]
    if stored_includes != list(include_paths):
        raise RuntimeError("Custom 노드 include_file_paths 설정 확인 실패: " + ", ".join(stored_includes))
    for name in variant.custom_input_names:
        source, source_output = sources[name]
        link(source, custom, name, source_output)
    return custom, stored_includes


def build_front_outputs(material, custom, sources):
    extinction = add_node(material, unreal.MaterialExpressionComponentMask, -150, -120, r=True, g=True, b=True, a=False)
    link(custom, extinction)
    albedo_source, albedo_output = sources[ALBEDO_NAME]

    bsdf = add_node(material, unreal.MaterialExpressionSubstrateVolumetricFogCloudBSDF, 250, 0)
    pins = {
        "albedo": unreal.MaterialEditingLibrary.connect_material_expressions(albedo_source, albedo_output, bsdf, SUBSTRATE_PIN_CANDIDATES["albedo"][0]) and SUBSTRATE_PIN_CANDIDATES["albedo"][0],
        "extinction": try_link_first_pin(extinction, bsdf, SUBSTRATE_PIN_CANDIDATES["extinction"]),
    }
    if all(pins.values()) and unreal.MaterialEditingLibrary.connect_material_property(bsdf, "", unreal.MaterialProperty.MP_FRONT_MATERIAL):
        return {"path": "substrate", "pins": pins}

    unreal.MaterialEditingLibrary.delete_material_expression(material, bsdf)
    if not unreal.MaterialEditingLibrary.connect_material_property(albedo_source, albedo_output, unreal.MaterialProperty.MP_BASE_COLOR):
        raise RuntimeError("기존 핀 BaseColor(알베도) 연결 실패")
    if not unreal.MaterialEditingLibrary.connect_material_property(extinction, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR):
        raise RuntimeError("기존 핀 SubsurfaceColor(소광) 연결 실패")
    return {"path": "legacy", "pins": {"albedo": "MP_BASE_COLOR", "extinction": "MP_SUBSURFACE_COLOR"}, "substrate_pins_tried": pins}


def verify_master_parameters(material, variant=FLAT_VARIANT):
    editing = unreal.MaterialEditingLibrary
    found = {
        "vector": {str(name) for name in editing.get_vector_parameter_names(material)},
        "scalar": {str(name) for name in editing.get_scalar_parameter_names(material)},
        "texture": {str(name) for name in editing.get_texture_parameter_names(material)},
    }
    expected = {
        "vector": [name for name, _ in variant.vector_parameter_defaults],
        "scalar": [name for name, _ in variant.scalar_parameter_defaults],
        "texture": list(variant.texture_parameter_names),
    }
    missing = [f"{kind}:{name}" for kind, names in expected.items() for name in names if name not in found[kind]]
    if missing:
        raise RuntimeError("마스터 파라미터 없음: " + ", ".join(missing))


def read_material_statistics(material):
    try:
        statistics = unreal.MaterialEditingLibrary.get_statistics(material)
        return {name: statistics.get_editor_property(name) for name in ("num_vertex_shader_instructions", "num_pixel_shader_instructions", "num_samplers")}
    except Exception:
        return {}


def create_master(code, include_paths, state_volume, overwrite, variant=FLAT_VARIANT, ground_height=None):
    material, was_created = prepare_master(overwrite, variant)
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_VOLUME)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    sources = build_sources(material, state_volume, variant, ground_height)
    custom, stored_includes = build_custom_node(material, code, include_paths, sources, variant)
    front = build_front_outputs(material, custom, sources)
    errors = [str(item) for item in unreal.MaterialEditingLibrary.recompile_material(material) or []]
    if errors:
        fallback = "include 때문이면 요청 파일 code_mode inline" if CODE_MODE_INLINE in variant.code_modes else "include 방식만 허용되므로 멈추고 감독에게 알린다"
        raise RuntimeError(f"{variant.master_name} 컴파일 실패(Saved/Logs/TDGame.log 의 LogMaterial 확인, {fallback}): " + " | ".join(errors))
    verify_master_parameters(material, variant)
    save_asset(material, variant.master_path)
    return material, {
        "asset": variant.master_path,
        "created": was_created,
        "front": front,
        "include_file_paths": stored_includes,
        "statistics": read_material_statistics(material),
        "statistics_note": "볼륨 도메인은 베이스 패스가 없어 PS 명령 0일 수 있음(L-editor-65 경고도 무해) → check_fog3d_material_hlsl.py dxc 명령 수가 대체 지표",
    }


def write_report(report, variant=FLAT_VARIANT):
    report_path = project_path(*variant.report_relative_path)
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, "w", encoding="utf-8") as report_file:
        json.dump(report, report_file, ensure_ascii=False, indent=2, default=str)
    return report_path


def main():
    if unreal is None:
        raise RuntimeError("언리얼 에디터 안에서만 실행할 수 있습니다(python Tools/run_in_editor.py).")
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    assets = unreal.EditorAssetLibrary
    missing_names = find_missing_unreal_names()
    if missing_names:
        raise RuntimeError("언리얼 Python 이름 없음: " + ", ".join(missing_names))
    if editor.get_game_world() is not None:
        raise RuntimeError("PIE를 종료한 뒤 실행하세요.")

    request = read_request()
    overwrite = request["overwrite"]
    code_mode = request["code_mode"]
    variant = find_variant(request["variant"])
    if variant is GROUND_VARIANT:
        missing_names = [dotted_name for dotted_name in GROUND_REQUIRED_UNREAL_NAMES if getattr(unreal, dotted_name, None) is None]
        if missing_names:
            raise RuntimeError("언리얼 Python 이름 없음: " + ", ".join(missing_names))
    project_root = unreal.SystemLibrary.get_project_directory()
    shader_text = read_density_source(project_root, variant)
    code, include_paths = build_custom_code(code_mode, shader_text, variant)
    problems = validate_custom_code(code_mode, code, include_paths, shader_text, variant) + [f"입력 원천 없음: {name}" for name in find_unresolved_inputs(variant)]
    if problems:
        raise RuntimeError("자체 점검 실패: " + " | ".join(problems))

    if assets.does_asset_exist(variant.master_path) and not overwrite:
        print(json.dumps({"status": "기존 에셋이 있어 아무것도 바꾸지 않았습니다", "existing_assets": [variant.master_path], "overwrite_request_file": project_path(*REQUEST_RELATIVE_PATH), "overwrite_request_content": {"overwrite": True, "code_mode": code_mode, "variant": variant.name}}, ensure_ascii=False))
        raise RuntimeError("기존 에셋 보존: overwrite 요청 파일이 없어 중단합니다.")
    unsaved_names = find_unsaved_package_names(variant)
    if unsaved_names:
        raise RuntimeError("미저장 작업을 보존합니다: " + ", ".join(unsaved_names))

    print("단계: 기본 볼륨 텍스처")
    state_volume = load_state_volume_default()
    ground_height = None
    texture_defaults = {STATE_VOLUME_NAME: STATE_VOLUME_DEFAULT_PATH}
    if variant is GROUND_VARIANT:
        print("단계: 땅 높이 기본 텍스처")
        ground_height = load_ground_height_default()
        texture_defaults[GROUND_HEIGHT_NAME] = GROUND_HEIGHT_DEFAULT_PATH

    print(f"단계: 마스터 재질({variant.name}, {code_mode})")
    _, master_summary = create_master(code, include_paths, state_volume, overwrite, variant, ground_height)

    report = {
        "status": "ok",
        "overwrite": overwrite,
        "code_mode": code_mode,
        "state_volume_default": STATE_VOLUME_DEFAULT_PATH,
        "master": master_summary,
        "hlsl": {
            "ush": "/".join(variant.shader_relative_path),
            "ush_sha256": hashlib.sha256(shader_text.encode("utf-8")).hexdigest(),
            "custom_code_sha256": code_sha256(code),
            "inline_stripped_includes": split_inline_includes(shader_text)[1] if code_mode == CODE_MODE_INLINE else [],
        },
        "parameters": {
            "vector": {name: list(rgba) for name, rgba in variant.vector_parameter_defaults},
            "scalar": dict(variant.scalar_parameter_defaults),
            "texture": texture_defaults,
        },
        "saved": True,
    }
    if variant is not FLAT_VARIANT:
        report["variant"] = variant.name
    report_path = write_report(report, variant)
    print(json.dumps(report, ensure_ascii=False, default=str))
    print("보고서: " + report_path)


def find_requested_variant():
    try:
        return find_variant(read_request()["variant"])
    except Exception:
        return FLAT_VARIANT


def run_with_failure_report():
    try:
        main()
    except Exception as error:
        if unreal is not None:
            write_report({"status": "failed", "error": str(error)}, find_requested_variant())
        raise


if __name__ == "__main__":
    run_with_failure_report()
