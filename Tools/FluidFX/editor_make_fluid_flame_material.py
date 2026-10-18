"""실행 환경: TDGame 언리얼 에디터 안에서 FluidFX 불꽃 카드 마스터 재질 M_TD_FluidFlame(Custom 노드에 Tools/FluidFX/Shaders/TDFluidFlameCard.hlsl)과 불티 재질 M_TD_FluidEmber(TDFluidEmber.hlsl) 및 인스턴스를 만든다.
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_flame_material.py
출력: /Game/FluidFX/Materials/M_TD_FluidFlame, MI_TD_FluidFlame_Torch·Campfire·Explosion, M_TD_FluidEmber, MI_TD_FluidEmber_Default, Saved/FluidFX/fluid_flame_material.json
상태: 검증됨(2026-10-18 FX-3 불 무대 생성·쇼케이스·탐침 성공). 기존 에셋은 Saved/FluidFX/fluid_flame_material_request.json 의 {"overwrite": true} 가 있을 때만 같은 에셋 안에서 다시 짓고(flame_blend_mode 가 "translucent" 이면 BLEND_TRANSLUCENT, 아니면 BLEND_ADDITIVE), 미저장 작업이 있으면 중단한다.
"""
import hashlib
import json
import os
import re

import unreal


FLUID_ROOT = "/Game/FluidFX"
MATERIAL_DIR = f"{FLUID_ROOT}/Materials"
FLAME_MASTER_NAME = "M_TD_FluidFlame"
EMBER_MASTER_NAME = "M_TD_FluidEmber"
FLAME_MASTER_PATH = f"{MATERIAL_DIR}/{FLAME_MASTER_NAME}"
EMBER_MASTER_PATH = f"{MATERIAL_DIR}/{EMBER_MASTER_NAME}"

FLAME_HLSL_RELATIVE_PATH = ("Tools", "FluidFX", "Shaders", "TDFluidFlameCard.hlsl")
EMBER_HLSL_RELATIVE_PATH = ("Tools", "FluidFX", "Shaders", "TDFluidEmber.hlsl")
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_flame_material_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_flame_material.json")

DEFAULT_TEXTURE_PATHS = ("/Engine/EngineResources/Black", "/Engine/EngineResources/DefaultTexture")
FLAME_PARAMETER_GROUP = "TDFluidFlame"
EMBER_PARAMETER_GROUP = "TDFluidEmber"
TRANSLUCENT_BLEND_REQUEST = "translucent"

FLAME_INSTANCE_SPECS = (
    {
        "name": "MI_TD_FluidFlame_Torch",
        "scalar_overrides": {
            "FlameIntensity": 10.0,
            "FlameTemperatureMinKelvin": 1000.0,
            "FlameTemperatureMaxKelvin": 2400.0,
            "FlameErosion": 0.2,
            "FlameDepthFadeCm": 15.0,
            "FlameNoiseScale": 0.05,
            "FlameNoiseStrength": 2.0,
            "FlameOpacityScale": 8.0,
        },
    },
    {
        "name": "MI_TD_FluidFlame_Campfire",
        "scalar_overrides": {
            "FlameIntensity": 12.0,
            "FlameTemperatureMinKelvin": 1000.0,
            "FlameTemperatureMaxKelvin": 2500.0,
            "FlameErosion": 0.15,
            "FlameDepthFadeCm": 10.0,
            "FlameNoiseScale": 0.03,
            "FlameNoiseStrength": 2.0,
            "FlameOpacityScale": 8.0,
        },
    },
    {
        "name": "MI_TD_FluidFlame_Explosion",
        "scalar_overrides": {
            "FlameIntensity": 10.0,
            "FlameTemperatureMinKelvin": 1000.0,
            "FlameTemperatureMaxKelvin": 2700.0,
            "FlameDepthFadeCm": 30.0,
            "FlameNoiseScale": 0.015,
            "FlameNoiseStrength": 12.0,
            "FlameOpacityScale": 6.0,
        },
    },
)
EMBER_INSTANCE_SPECS = ({"name": "MI_TD_FluidEmber_Default", "scalar_overrides": {}},)

FLAME_VECTOR_PARAMETER_DEFAULTS = (
    ("SimCenterWorld", (0.0, 0.0, 0.0, 1.0)),
    ("SimExtentsCm", (120.0, 160.0, 0.08, 0.0)),
    ("PlaneAxisU", (1.0, 0.0, 0.0, 0.0)),
    ("PlaneAxisV", (0.0, 1.0, 0.0, 0.0)),
)
FLAME_SCALAR_PARAMETER_DEFAULTS = (
    ("SimGridResolution", 96.0),
    ("FlameIntensity", 4.0),
    ("FlameTemperatureMinKelvin", 1400.0),
    ("FlameTemperatureMaxKelvin", 2300.0),
    ("FlameNoiseScale", 0.1),
    ("FlameNoiseStrength", 5.0),
    ("FlameFlowSpeed", 0.8),
    ("FlameErosion", 0.2),
    ("FlameDensitySource", 0.0),
    ("FlameDepthFadeCm", 15.0),
    ("FlameOpacityScale", 4.0),
)
FLAME_TEXTURE_PARAMETER_NAMES = ("DensityPressureTexture", "VelocityTexture")
FLAME_BUILTIN_SOURCE_NAMES = ("PixelWorldPosition", "SceneDepthCm", "PixelDepthCm", "ElapsedSeconds")

FLAME_CUSTOM_INPUT_NAMES = (
    "DensityPressureTexture",
    "VelocityTexture",
    "SimCenterWorld",
    "SimExtentsCm",
    "SimGridResolution",
    "PlaneAxisU",
    "PlaneAxisV",
    "PixelWorldPosition",
    "SceneDepthCm",
    "PixelDepthCm",
    "ElapsedSeconds",
    "FlameIntensity",
    "FlameTemperatureMinKelvin",
    "FlameTemperatureMaxKelvin",
    "FlameNoiseScale",
    "FlameNoiseStrength",
    "FlameFlowSpeed",
    "FlameErosion",
    "FlameDensitySource",
    "FlameDepthFadeCm",
    "FlameOpacityScale",
)
FLAME_HLSL_SAMPLER_NAMES = ("DensityPressureTextureSampler", "VelocityTextureSampler")
FLAME_HLSL_ENGINE_SYMBOLS = ("GradientNoise3D_ALU", "Texture2DSampleLevel")

EMBER_SCALAR_PARAMETER_DEFAULTS = (
    ("EmberIntensity", 6.0),
    ("EmberTemperatureMinKelvin", 900.0),
    ("EmberTemperatureMaxKelvin", 2200.0),
)
EMBER_INSTANCE_DATA_NAMES = (("EmberHeat", 0), ("EmberFade", 1))
EMBER_ENGINE_VECTOR_NAMES = ("SurfaceNormal", "ViewVector")
EMBER_CUSTOM_INPUT_NAMES = (
    "EmberHeat",
    "EmberFade",
    "EmberIntensity",
    "EmberTemperatureMinKelvin",
    "EmberTemperatureMaxKelvin",
    "SurfaceNormal",
    "ViewVector",
)
EMBER_HLSL_ENGINE_SYMBOLS = ()

OPTIONAL_MATERIAL_SETTINGS = (
    ("use_translucency_vertex_fog", False),
    ("apply_cloud_fogging", False),
)

REQUIRED_UNREAL_NAMES = (
    "EditorAssetLibrary.load_asset",
    "EditorAssetLibrary.save_loaded_asset",
    "MaterialEditingLibrary.recompile_material",
    "MaterialEditingLibrary.delete_all_material_expressions",
    "MaterialEditingLibrary.get_vector_parameter_names",
    "MaterialEditingLibrary.get_scalar_parameter_names",
    "MaterialEditingLibrary.get_texture_parameter_names",
    "MaterialEditingLibrary.set_material_instance_parent",
    "MaterialEditingLibrary.set_material_instance_scalar_parameter_value",
    "MaterialEditingLibrary.get_material_instance_scalar_parameter_value",
    "MaterialEditingLibrary.update_material_instance",
    "MaterialEditingLibrary.set_material_usage",
    "MaterialEditingLibrary.has_material_usage",
    "EditorLoadingAndSavingUtils.get_dirty_map_packages",
    "EditorLoadingAndSavingUtils.get_dirty_content_packages",
    "SystemLibrary.get_project_directory",
    "Material",
    "MaterialFactoryNew",
    "MaterialInstanceConstant",
    "MaterialInstanceConstantFactoryNew",
    "MaterialExpressionCustom",
    "CustomInput",
    "CustomMaterialOutputType.CMOT_FLOAT3",
    "CustomMaterialOutputType.CMOT_FLOAT4",
    "MaterialExpressionWorldPosition",
    "MaterialExpressionSceneDepth",
    "MaterialExpressionPixelDepth",
    "MaterialExpressionTime",
    "MaterialExpressionTextureObjectParameter",
    "MaterialExpressionVectorParameter",
    "MaterialExpressionScalarParameter",
    "MaterialExpressionComponentMask",
    "MaterialExpressionPerInstanceCustomData",
    "MaterialExpressionPixelNormalWS",
    "MaterialExpressionCameraVectorWS",
    "BlendMode.BLEND_ADDITIVE",
    "BlendMode.BLEND_TRANSLUCENT",
    "MaterialShadingModel.MSM_UNLIT",
    "MaterialProperty.MP_EMISSIVE_COLOR",
    "MaterialProperty.MP_OPACITY",
    "MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES",
    "MaterialSamplerType.SAMPLERTYPE_COLOR",
    "MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR",
    "TextureCompressionSettings.TC_DEFAULT",
)

IDENTIFIER_PATTERN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


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
        return {}
    with open(request_path, encoding="utf-8") as request_file:
        request = json.load(request_file)
    return request if isinstance(request, dict) else {}


def resolve_flame_blend_name(request):
    if request.get("flame_blend_mode") == TRANSLUCENT_BLEND_REQUEST:
        return "BLEND_TRANSLUCENT"
    return "BLEND_ADDITIVE"


def asset_path(name):
    return f"{MATERIAL_DIR}/{name}"


def collect_target_asset_paths():
    names = [FLAME_MASTER_NAME, EMBER_MASTER_NAME]
    names.extend(spec["name"] for spec in FLAME_INSTANCE_SPECS)
    names.extend(spec["name"] for spec in EMBER_INSTANCE_SPECS)
    return [asset_path(name) for name in names]


def find_unsaved_package_names(overwrite):
    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    dirty.extend(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    names = sorted({package.get_name() for package in dirty})
    if not overwrite:
        return names
    return [name for name in names if not name.startswith(FLUID_ROOT + "/")]


def ensure_directory(directory_path):
    assets = unreal.EditorAssetLibrary
    if assets.does_directory_exist(directory_path):
        return
    if not assets.make_directory(directory_path):
        raise RuntimeError(f"폴더 생성 실패: {directory_path}")


def save_asset(asset, path):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"에셋 저장 실패: {path}")


def load_hlsl_code(relative_path):
    hlsl_path = project_path(*relative_path)
    if not os.path.isfile(hlsl_path):
        raise RuntimeError(f"HLSL 파일 없음: {hlsl_path}")
    with open(hlsl_path, encoding="utf-8") as hlsl_file:
        code = hlsl_file.read().replace("\r\n", "\n").strip("\n") + "\n"
    return hlsl_path, code


def find_unbalanced_brackets(code):
    pairs = {")": "(", "]": "[", "}": "{"}
    stack = []
    for index, character in enumerate(code):
        if character in "([{":
            stack.append(character)
            continue
        if character in pairs:
            if not stack or stack.pop() != pairs[character]:
                return f"괄호 불일치 위치 {index}"
    return f"닫히지 않은 괄호 {len(stack)}개" if stack else ""


def validate_hlsl_code(code, input_names, sampler_names, engine_symbols):
    problems = []
    identifiers = set(IDENTIFIER_PATTERN.findall(code))
    unused_inputs = [name for name in input_names if name not in identifiers]
    if unused_inputs:
        problems.append("HLSL이 쓰지 않는 Custom 입력: " + ", ".join(unused_inputs))
    missing_samplers = [name for name in sampler_names if name not in identifiers]
    if missing_samplers:
        problems.append("HLSL이 쓰지 않는 샘플러: " + ", ".join(missing_samplers))
    missing_symbols = [name for name in engine_symbols if name not in identifiers]
    if missing_symbols:
        problems.append("HLSL에 없는 엔진 심볼: " + ", ".join(missing_symbols))
    bracket_problem = find_unbalanced_brackets(code)
    if bracket_problem:
        problems.append(bracket_problem)
    if "return" not in identifiers:
        problems.append("HLSL에 return 이 없음")
    return problems


def choose_sampler_type(texture):
    if texture is None:
        return None
    try:
        compression = texture.get_editor_property("compression_settings")
        is_srgb = bool(texture.get_editor_property("srgb"))
    except Exception:
        return None
    if compression != unreal.TextureCompressionSettings.TC_DEFAULT:
        return None
    if is_srgb:
        return unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
    return unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR


def pick_default_texture():
    attempts = []
    for texture_path in DEFAULT_TEXTURE_PATHS:
        texture = unreal.EditorAssetLibrary.load_asset(texture_path)
        sampler_type = choose_sampler_type(texture)
        if sampler_type is not None:
            return texture_path, texture, sampler_type
        attempts.append(texture_path)
    raise RuntimeError("기본 텍스처로 쓸 수 있는 엔진 텍스처가 없음: " + ", ".join(attempts))


def has_editor_property(owner, property_name):
    try:
        owner.get_editor_property(property_name)
    except Exception:
        return False
    return True


def add_node(material, expression_class, x, y, **properties):
    expression = unreal.MaterialEditingLibrary.create_material_expression(material, expression_class, x, y)
    if expression is None:
        raise RuntimeError(f"재질 노드 생성 실패: {expression_class.__name__}")
    for name, value in properties.items():
        expression.set_editor_property(name, value)
    return expression


def link(source, target, target_input="", source_output=""):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(source, source_output, target, target_input):
        raise RuntimeError(
            f"재질 노드 연결 실패: {source.get_name()}[{source_output}] -> {target.get_name()}.{target_input}"
        )


def connect_material_output(expression, material_property):
    if not unreal.MaterialEditingLibrary.connect_material_property(expression, "", material_property):
        raise RuntimeError(f"재질 출력 연결 실패: {material_property}")


def assign_parameter_group(expression, group_name):
    try:
        expression.set_editor_property("group", group_name)
    except Exception:
        return


def truncate_if_available(material, source, x, y):
    truncate_class = getattr(unreal, "MaterialExpressionTruncateLWC", None)
    if truncate_class is None:
        return source
    truncated = add_node(material, truncate_class, x, y)
    link(source, truncated)
    return truncated


def make_custom_input(input_name):
    custom_input = unreal.CustomInput()
    custom_input.set_editor_property("input_name", input_name)
    return custom_input


def build_custom_node(material, description, output_type, input_names, code, x, y):
    custom = add_node(material, unreal.MaterialExpressionCustom, x, y)
    custom.set_editor_property("description", description)
    custom.set_editor_property("output_type", output_type)
    custom.set_editor_property("inputs", [make_custom_input(name) for name in input_names])
    custom.set_editor_property("code", code)
    stored_names = [str(item.get_editor_property("input_name")) for item in custom.get_editor_property("inputs")]
    if stored_names != list(input_names):
        raise RuntimeError("Custom 노드 입력 이름 설정 확인 실패: " + ", ".join(stored_names))
    return custom


def apply_required_properties(material, assignments):
    missing = [name for name, _ in assignments if not has_editor_property(material, name)]
    if missing:
        raise RuntimeError("재질 속성 이름 없음(엔진 버전 확인 필요): " + ", ".join(missing))
    applied = {}
    for name, value in assignments:
        material.set_editor_property(name, value)
        applied[name] = str(value)
    return applied


def apply_optional_properties(material):
    applied = {}
    skipped = []
    for property_name, value in OPTIONAL_MATERIAL_SETTINGS:
        try:
            material.set_editor_property(property_name, value)
        except Exception:
            skipped.append(property_name)
            continue
        applied[property_name] = value
    return applied, skipped


def enable_instanced_static_mesh_usage(material):
    editing = unreal.MaterialEditingLibrary
    editing.set_material_usage(material, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    is_enabled = editing.has_material_usage(material, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    if not is_enabled:
        raise RuntimeError("불티 재질에 인스턴스드 스태틱 메시 사용 플래그를 켜지 못함")
    return "MATUSAGE_INSTANCED_STATIC_MESHES"


def prepare_master_material(master_name, master_path, overwrite):
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(master_path):
        if not overwrite:
            raise RuntimeError(f"기존 에셋 보존: {master_path}")
        material = assets.load_asset(master_path)
        if not isinstance(material, unreal.Material):
            raise RuntimeError(f"기존 에셋 형식 불일치: {master_path}")
        unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
        return material, False

    ensure_directory(MATERIAL_DIR)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        master_name, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew()
    )
    if material is None:
        raise RuntimeError(f"마스터 재질 생성 실패: {master_path}")
    return material, True


def verify_custom_inputs_connected(material, custom, expected_count):
    getter = getattr(unreal.MaterialEditingLibrary, "get_inputs_for_material_expression", None)
    if getter is None:
        return "입력 연결 개수 확인 도구 없음"
    connected = [item for item in getter(material, custom) if item is not None]
    if len(connected) != expected_count:
        raise RuntimeError(f"Custom 입력 연결 개수 불일치: {len(connected)}/{expected_count}")
    return f"{len(connected)}/{expected_count}"


def read_material_statistics(material):
    try:
        statistics = unreal.MaterialEditingLibrary.get_statistics(material)
        return {
            name: statistics.get_editor_property(name)
            for name in ("num_vertex_shader_instructions", "num_pixel_shader_instructions", "num_samplers")
        }
    except Exception:
        return {}


def compile_master_material(material, label):
    errors = [str(item) for item in unreal.MaterialEditingLibrary.recompile_material(material) or []]
    if errors:
        raise RuntimeError(f"{label} 컴파일 실패(Saved/Logs/TDGame.log 의 LogMaterial 다음 들여쓴 줄 확인): " + " | ".join(errors))
    statistics = read_material_statistics(material)
    if statistics and not statistics.get("num_pixel_shader_instructions"):
        raise RuntimeError(
            f"{label} 셰이더 컴파일 결과 없음(픽셀 셰이더 명령 0). Saved/Logs/TDGame.log 의 LogMaterial·LogShaderCompilers 다음 들여쓴 줄 확인"
        )
    return statistics


def verify_master_parameters(material, expected):
    editing = unreal.MaterialEditingLibrary
    found = {
        "vector": {str(name) for name in editing.get_vector_parameter_names(material)},
        "scalar": {str(name) for name in editing.get_scalar_parameter_names(material)},
        "texture": {str(name) for name in editing.get_texture_parameter_names(material)},
    }
    missing = [f"{kind}:{name}" for kind, names in expected.items() for name in names if name not in found[kind]]
    if missing:
        raise RuntimeError("재질 파라미터 없음: " + ", ".join(missing))


def build_flame_master_graph(material, code, default_texture, sampler_type, blend_name):
    sources = {}
    world_position = add_node(material, unreal.MaterialExpressionWorldPosition, -1500, -1100)
    sources["PixelWorldPosition"] = (truncate_if_available(material, world_position, -1300, -1100), "")
    sources["SceneDepthCm"] = (add_node(material, unreal.MaterialExpressionSceneDepth, -1300, -980), "")
    sources["PixelDepthCm"] = (add_node(material, unreal.MaterialExpressionPixelDepth, -1300, -880), "")
    sources["ElapsedSeconds"] = (add_node(material, unreal.MaterialExpressionTime, -1300, -780), "")

    for row, texture_name in enumerate(FLAME_TEXTURE_PARAMETER_NAMES):
        texture_node = add_node(
            material,
            unreal.MaterialExpressionTextureObjectParameter,
            -1300,
            -640 + row * 140,
            parameter_name=texture_name,
            texture=default_texture,
            sampler_type=sampler_type,
        )
        assign_parameter_group(texture_node, FLAME_PARAMETER_GROUP)
        sources[texture_name] = (texture_node, "")

    for row, (parameter_name, default_value) in enumerate(FLAME_VECTOR_PARAMETER_DEFAULTS):
        vector_node = add_node(
            material,
            unreal.MaterialExpressionVectorParameter,
            -1300,
            -340 + row * 130,
            parameter_name=parameter_name,
            default_value=unreal.LinearColor(*default_value),
        )
        assign_parameter_group(vector_node, FLAME_PARAMETER_GROUP)
        sources[parameter_name] = (vector_node, "RGB")

    for row, (parameter_name, default_value) in enumerate(FLAME_SCALAR_PARAMETER_DEFAULTS):
        scalar_node = add_node(
            material,
            unreal.MaterialExpressionScalarParameter,
            -1300,
            240 + row * 110,
            parameter_name=parameter_name,
            default_value=default_value,
        )
        assign_parameter_group(scalar_node, FLAME_PARAMETER_GROUP)
        sources[parameter_name] = (scalar_node, "")

    flame_code = code
    custom = build_custom_node(
        material,
        "TDFluidFlameCard",
        unreal.CustomMaterialOutputType.CMOT_FLOAT4,
        FLAME_CUSTOM_INPUT_NAMES,
        flame_code,
        -300,
        0,
    )
    for input_name in FLAME_CUSTOM_INPUT_NAMES:
        source, source_output = sources[input_name]
        link(source, custom, input_name, source_output)

    color_mask = add_node(material, unreal.MaterialExpressionComponentMask, 200, -80, r=True, g=True, b=True, a=False)
    link(custom, color_mask)
    connect_material_output(color_mask, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    alpha_mask = add_node(material, unreal.MaterialExpressionComponentMask, 200, 80, r=False, g=False, b=False, a=True)
    link(custom, alpha_mask)
    connect_material_output(alpha_mask, unreal.MaterialProperty.MP_OPACITY)
    return custom


def build_ember_master_graph(material, code):
    sources = {}
    for row, (input_name, data_index) in enumerate(EMBER_INSTANCE_DATA_NAMES):
        data_node = add_node(material, unreal.MaterialExpressionPerInstanceCustomData, -1000, -300 + row * 120)
        if not has_editor_property(data_node, "data_index"):
            raise RuntimeError("PerInstanceCustomData 노드에 data_index 속성이 없음")
        data_node.set_editor_property("data_index", data_index)
        sources[input_name] = (data_node, "")

    for row, (parameter_name, default_value) in enumerate(EMBER_SCALAR_PARAMETER_DEFAULTS):
        scalar_node = add_node(
            material,
            unreal.MaterialExpressionScalarParameter,
            -1000,
            -40 + row * 110,
            parameter_name=parameter_name,
            default_value=default_value,
        )
        assign_parameter_group(scalar_node, EMBER_PARAMETER_GROUP)
        sources[parameter_name] = (scalar_node, "")

    sources["SurfaceNormal"] = (add_node(material, unreal.MaterialExpressionPixelNormalWS, -1000, 340), "")
    sources["ViewVector"] = (add_node(material, unreal.MaterialExpressionCameraVectorWS, -1000, 460), "")

    custom = build_custom_node(
        material,
        "TDFluidEmber",
        unreal.CustomMaterialOutputType.CMOT_FLOAT3,
        EMBER_CUSTOM_INPUT_NAMES,
        code,
        -300,
        0,
    )
    for input_name in EMBER_CUSTOM_INPUT_NAMES:
        source, source_output = sources[input_name]
        link(source, custom, input_name, source_output)
    connect_material_output(custom, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return custom


def create_flame_master(code, default_texture, sampler_type, overwrite, blend_name):
    material, was_created = prepare_master_material(FLAME_MASTER_NAME, FLAME_MASTER_PATH, overwrite)
    settings = apply_required_properties(
        material,
        (
            ("blend_mode", getattr(unreal.BlendMode, blend_name)),
            ("shading_model", unreal.MaterialShadingModel.MSM_UNLIT),
            ("two_sided", True),
            ("disable_depth_test", False),
            ("enable_responsive_aa", True),
            ("output_translucent_velocity", False),
        ),
    )
    optional_applied, skipped = apply_optional_properties(material)
    settings.update(optional_applied)
    custom = build_flame_master_graph(material, code, default_texture, sampler_type, blend_name)
    connected = verify_custom_inputs_connected(material, custom, len(FLAME_CUSTOM_INPUT_NAMES))
    statistics = compile_master_material(material, "불꽃 카드 재질")
    verify_master_parameters(
        material,
        {
            "vector": [name for name, _ in FLAME_VECTOR_PARAMETER_DEFAULTS],
            "scalar": [name for name, _ in FLAME_SCALAR_PARAMETER_DEFAULTS],
            "texture": list(FLAME_TEXTURE_PARAMETER_NAMES),
        },
    )
    save_asset(material, FLAME_MASTER_PATH)
    return material, {
        "created": was_created,
        "blend_mode": blend_name,
        "settings": settings,
        "skipped_settings": skipped,
        "custom_inputs_connected": connected,
        "statistics": statistics,
    }


def create_ember_master(code, overwrite):
    material, was_created = prepare_master_material(EMBER_MASTER_NAME, EMBER_MASTER_PATH, overwrite)
    settings = apply_required_properties(
        material,
        (
            ("blend_mode", unreal.BlendMode.BLEND_ADDITIVE),
            ("shading_model", unreal.MaterialShadingModel.MSM_UNLIT),
            ("enable_responsive_aa", True),
            ("output_translucent_velocity", True),
        ),
    )
    optional_applied, skipped = apply_optional_properties(material)
    settings.update(optional_applied)
    usage = enable_instanced_static_mesh_usage(material)
    custom = build_ember_master_graph(material, code)
    connected = verify_custom_inputs_connected(material, custom, len(EMBER_CUSTOM_INPUT_NAMES))
    statistics = compile_master_material(material, "불티 재질")
    verify_master_parameters(
        material,
        {"vector": [], "scalar": [name for name, _ in EMBER_SCALAR_PARAMETER_DEFAULTS], "texture": []},
    )
    if not unreal.MaterialEditingLibrary.has_material_usage(material, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES):
        raise RuntimeError("재질 컴파일 뒤 인스턴스드 스태틱 메시 사용 플래그가 꺼져 있음")
    save_asset(material, EMBER_MASTER_PATH)
    return material, {
        "created": was_created,
        "blend_mode": "BLEND_ADDITIVE",
        "settings": settings,
        "skipped_settings": skipped,
        "usage": usage,
        "custom_inputs_connected": connected,
        "statistics": statistics,
    }


def prepare_instance(spec, overwrite):
    assets = unreal.EditorAssetLibrary
    path = asset_path(spec["name"])
    if assets.does_asset_exist(path):
        if not overwrite:
            raise RuntimeError(f"기존 에셋 보존: {path}")
        instance = assets.load_asset(path)
        if not isinstance(instance, unreal.MaterialInstanceConstant):
            raise RuntimeError(f"기존 에셋 형식 불일치: {path}")
        return instance
    instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        spec["name"], MATERIAL_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew()
    )
    if instance is None:
        raise RuntimeError(f"재질 인스턴스 생성 실패: {path}")
    return instance


def create_material_instance(spec, master, overwrite):
    editing = unreal.MaterialEditingLibrary
    path = asset_path(spec["name"])
    instance = prepare_instance(spec, overwrite)
    editing.set_material_instance_parent(instance, master)
    for parameter_name, value in spec["scalar_overrides"].items():
        editing.set_material_instance_scalar_parameter_value(instance, parameter_name, value)
    editing.update_material_instance(instance)
    for parameter_name, value in spec["scalar_overrides"].items():
        stored = editing.get_material_instance_scalar_parameter_value(instance, parameter_name)
        if abs(stored - value) > 0.001:
            raise RuntimeError(f"재질 인스턴스 스칼라 설정 확인 실패: {path}.{parameter_name}")
    save_asset(instance, path)
    return {"asset": path, "scalar_overrides": spec["scalar_overrides"]}


def write_report(report):
    report_path = project_path(*REPORT_RELATIVE_PATH)
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, "w", encoding="utf-8") as report_file:
        json.dump(report, report_file, ensure_ascii=False, indent=2, default=str)
    return report_path


def find_missing_saved_files():
    expected = [
        project_path("Content", *path[len("/Game/"):].split("/")) + ".uasset" for path in collect_target_asset_paths()
    ]
    return [path for path in expected if not os.path.isfile(path)]


def describe_hlsl(hlsl_path, code):
    return {"path": hlsl_path, "characters": len(code), "sha256": hashlib.sha256(code.encode("utf-8")).hexdigest()}


def main():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    assets = unreal.EditorAssetLibrary

    missing_names = find_missing_unreal_names()
    if missing_names:
        raise RuntimeError("언리얼 Python 이름 없음: " + ", ".join(missing_names))
    if editor.get_game_world() is not None:
        raise RuntimeError("PIE를 종료한 뒤 실행하세요.")

    flame_path, flame_code = load_hlsl_code(FLAME_HLSL_RELATIVE_PATH)
    ember_path, ember_code = load_hlsl_code(EMBER_HLSL_RELATIVE_PATH)
    problems = validate_hlsl_code(
        flame_code, FLAME_CUSTOM_INPUT_NAMES, FLAME_HLSL_SAMPLER_NAMES, FLAME_HLSL_ENGINE_SYMBOLS
    )
    problems += validate_hlsl_code(ember_code, EMBER_CUSTOM_INPUT_NAMES, (), EMBER_HLSL_ENGINE_SYMBOLS)
    if problems:
        raise RuntimeError("HLSL 자체 점검 실패: " + " | ".join(problems))

    request = read_request()
    overwrite = request.get("overwrite") is True
    blend_name = resolve_flame_blend_name(request)
    existing_paths = [path for path in collect_target_asset_paths() if assets.does_asset_exist(path)]
    if existing_paths and not overwrite:
        print(json.dumps({
            "status": "기존 에셋이 있어 아무것도 바꾸지 않았습니다",
            "existing_assets": existing_paths,
            "overwrite_request_file": project_path(*REQUEST_RELATIVE_PATH),
            "overwrite_request_content": {"overwrite": True, "flame_blend_mode": "additive"},
        }, ensure_ascii=False))
        raise RuntimeError("기존 에셋 보존: overwrite 요청 파일이 없어 중단합니다.")

    unsaved_names = find_unsaved_package_names(overwrite)
    if unsaved_names:
        raise RuntimeError("미저장 작업을 보존합니다: " + ", ".join(unsaved_names))

    print("단계: 기본 텍스처 선택")
    texture_path, default_texture, sampler_type = pick_default_texture()

    print("단계: 불꽃 카드 마스터 재질")
    flame_master, flame_summary = create_flame_master(flame_code, default_texture, sampler_type, overwrite, blend_name)

    print("단계: 불티 마스터 재질")
    ember_master, ember_summary = create_ember_master(ember_code, overwrite)

    print("단계: 재질 인스턴스")
    flame_instances = [create_material_instance(spec, flame_master, overwrite) for spec in FLAME_INSTANCE_SPECS]
    ember_instances = [create_material_instance(spec, ember_master, overwrite) for spec in EMBER_INSTANCE_SPECS]

    missing_files = find_missing_saved_files()
    if missing_files:
        raise RuntimeError("저장 파일 확인 실패: " + ", ".join(missing_files))

    report = {
        "status": "ok",
        "overwrite": overwrite,
        "flame_hlsl": describe_hlsl(flame_path, flame_code),
        "ember_hlsl": describe_hlsl(ember_path, ember_code),
        "default_texture": texture_path,
        "sampler_type": str(sampler_type),
        "flame_master": flame_summary,
        "ember_master": ember_summary,
        "flame_instances": flame_instances,
        "ember_instances": ember_instances,
        "saved": True,
    }
    report_path = write_report(report)
    print(json.dumps(report, ensure_ascii=False, default=str))
    print("보고서: " + report_path)


def run_with_failure_report():
    try:
        main()
    except Exception as error:
        write_report({"status": "failed", "error": str(error)})
        raise


if __name__ == "__main__":
    run_with_failure_report()
