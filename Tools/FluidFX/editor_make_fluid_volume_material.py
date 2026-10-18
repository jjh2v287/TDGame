"""실행 환경: TDGame 언리얼 에디터 안에서 FluidFX 볼륨 레이마칭 마스터 재질 M_TD_FluidVolume(Custom 노드에 Tools/FluidFX/Shaders/TDFluidVolumeRaymarch.hlsl을 넣음)과 HARD·SOFT·SmokeSlab(RenderMode 2 대칭 돌출) 재질 인스턴스를 만든다.
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_volume_material.py
출력: /Game/FluidFX/Materials/M_TD_FluidVolume, MI_TD_FluidVolume_SmokeHard, MI_TD_FluidVolume_SmokeSoft, MI_TD_FluidVolume_SmokeSlab, Saved/FluidFX/fluid_volume_material.json
상태: 검증됨(2026-10-18 FX-1 HARD·SOFT 룩데브 5회 실행 성공, D-49). 기존 에셋은 Saved/FluidFX/fluid_volume_material_request.json 의 {"overwrite": true} 가 있을 때만 같은 에셋 안에서 다시 짓고, 미저장 작업이 있으면 중단한다.
"""
import hashlib
import json
import os
import re

import unreal


FLUID_ROOT = "/Game/FluidFX"
MATERIAL_DIR = f"{FLUID_ROOT}/Materials"
MASTER_NAME = "M_TD_FluidVolume"
MASTER_PATH = f"{MATERIAL_DIR}/{MASTER_NAME}"

HLSL_RELATIVE_PATH = ("Tools", "FluidFX", "Shaders", "TDFluidVolumeRaymarch.hlsl")
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_volume_material_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_volume_material.json")

DEFAULT_TEXTURE_PATHS = ("/Engine/EngineResources/Black", "/Engine/EngineResources/DefaultTexture")
PARAMETER_GROUP = "TDFluidVolume"

SMOKE_COLOR = (0.88, 0.12, 0.045)
SHADOW_COLOR = (0.018, 0.002, 0.001)
AMBIENT_COLOR = (0.16, 0.07, 0.05)
SLAB_SMOKE_COLOR = (0.35, 0.33, 0.31)
SLAB_AMBIENT_COLOR = (0.05, 0.05, 0.06)
SLAB_SHADOW_COLOR = (0.01, 0.009, 0.008)

INSTANCE_SPECS = (
    {"name": "MI_TD_FluidVolume_SmokeHard", "render_mode": 0.0},
    {"name": "MI_TD_FluidVolume_SmokeSoft", "render_mode": 1.0},
    {
        "name": "MI_TD_FluidVolume_SmokeSlab",
        "render_mode": 2.0,
        "extrude_symmetric": 1.0,
        "vector_overrides": {"SmokeColor": SLAB_SMOKE_COLOR, "ShadowColor": SLAB_SHADOW_COLOR, "AmbientColor": SLAB_AMBIENT_COLOR},
        "scalar_overrides": {"DensityScale": 0.04, "NoiseStrength": 0.8, "NoiseScale": 0.012},
    },
)

VECTOR_PARAMETER_DEFAULTS = (
    ("SimCenterWorld", (0.0, 0.0, 0.0, 1.0)),
    ("SimExtentsCm", (700.0, 700.0, 0.1, 0.0)),
    ("VolumeLightPosition", (0.0, 0.0, 300.0, 0.0)),
    ("VolumeLightColor", (0.0, 0.0, 0.0, 0.0)),
    ("VolumeLightDirection", (0.0, 0.0, -1.0, 0.0)),
    ("SmokeColor", SMOKE_COLOR + (1.0,)),
    ("ShadowColor", SHADOW_COLOR + (1.0,)),
    ("AmbientColor", AMBIENT_COLOR + (1.0,)),
    ("PlaneAxisU", (1.0, 0.0, 0.0, 0.0)),
    ("PlaneAxisV", (0.0, 1.0, 0.0, 0.0)),
    ("PlaneAxisN", (0.0, 0.0, 1.0, 0.0)),
)
SCALAR_PARAMETER_DEFAULTS = (
    ("VolumeHeightCm", 150.0),
    ("VolumeLightRadius", 600.0),
    ("VolumeLightMode", 0.0),
    ("DensityScale", 0.035),
    ("StepCount", 48.0),
    ("ShadowSteps", 6.0),
    ("NoiseScale", 0.0075),
    ("NoiseStrength", 0.55),
    ("NoiseFlowSpeed", 0.6),
    ("SoftFalloffCm", 28.0),
    ("RenderMode", 0.0),
    ("SurfaceSoftnessCm", 14.0),
    ("ExtrudeSymmetric", 0.0),
)
TEXTURE_PARAMETER_NAMES = ("DensityPressureTexture", "VelocityTexture")

CUSTOM_INPUT_NAMES = (
    "PixelWorldPosition",
    "CameraWorldPosition",
    "SceneDepthCm",
    "PixelDepthCm",
    "ElapsedSeconds",
    "DensityPressureTexture",
    "VelocityTexture",
    "SimCenterWorld",
    "SimExtentsCm",
    "VolumeHeightCm",
    "VolumeLightPosition",
    "VolumeLightColor",
    "VolumeLightRadius",
    "VolumeLightMode",
    "VolumeLightDirection",
    "SmokeColor",
    "ShadowColor",
    "AmbientColor",
    "DensityScale",
    "StepCount",
    "ShadowSteps",
    "NoiseScale",
    "NoiseStrength",
    "NoiseFlowSpeed",
    "SoftFalloffCm",
    "RenderMode",
    "SurfaceSoftnessCm",
    "PlaneAxisU",
    "PlaneAxisV",
    "PlaneAxisN",
    "ExtrudeSymmetric",
)
HLSL_SAMPLER_NAMES = ("DensityPressureTextureSampler", "VelocityTextureSampler")
HLSL_ENGINE_SYMBOLS = ("Parameters", "GradientNoise3D_ALU", "Texture2DSampleLevel")

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
    "MaterialEditingLibrary.set_material_instance_vector_parameter_value",
    "MaterialEditingLibrary.set_material_instance_scalar_parameter_value",
    "MaterialEditingLibrary.get_material_instance_vector_parameter_value",
    "MaterialEditingLibrary.get_material_instance_scalar_parameter_value",
    "MaterialEditingLibrary.update_material_instance",
    "EditorLoadingAndSavingUtils.get_dirty_map_packages",
    "EditorLoadingAndSavingUtils.get_dirty_content_packages",
    "SystemLibrary.get_project_directory",
    "Material",
    "MaterialFactoryNew",
    "MaterialInstanceConstant",
    "MaterialInstanceConstantFactoryNew",
    "MaterialExpressionCustom",
    "CustomInput",
    "CustomMaterialOutputType.CMOT_FLOAT4",
    "MaterialExpressionWorldPosition",
    "MaterialExpressionCameraPositionWS",
    "MaterialExpressionSceneDepth",
    "MaterialExpressionPixelDepth",
    "MaterialExpressionTime",
    "MaterialExpressionTextureObjectParameter",
    "MaterialExpressionVectorParameter",
    "MaterialExpressionScalarParameter",
    "MaterialExpressionComponentMask",
    "BlendMode.BLEND_TRANSLUCENT",
    "MaterialShadingModel.MSM_UNLIT",
    "MaterialProperty.MP_EMISSIVE_COLOR",
    "MaterialProperty.MP_OPACITY",
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


def read_overwrite_request():
    request_path = project_path(*REQUEST_RELATIVE_PATH)
    if not os.path.isfile(request_path):
        return False
    with open(request_path, encoding="utf-8") as request_file:
        request = json.load(request_file)
    return isinstance(request, dict) and request.get("overwrite") is True


def instance_asset_path(spec):
    return f"{MATERIAL_DIR}/{spec['name']}"


def collect_target_asset_paths():
    return [MASTER_PATH] + [instance_asset_path(spec) for spec in INSTANCE_SPECS]


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


def save_asset(asset, asset_path):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"에셋 저장 실패: {asset_path}")


def load_hlsl_code():
    hlsl_path = project_path(*HLSL_RELATIVE_PATH)
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


def validate_hlsl_code(code):
    problems = []
    identifiers = set(IDENTIFIER_PATTERN.findall(code))
    unused_inputs = [name for name in CUSTOM_INPUT_NAMES if name not in identifiers]
    if unused_inputs:
        problems.append("HLSL이 쓰지 않는 Custom 입력: " + ", ".join(unused_inputs))
    missing_samplers = [name for name in HLSL_SAMPLER_NAMES if name not in identifiers]
    if missing_samplers:
        problems.append("HLSL이 쓰지 않는 샘플러: " + ", ".join(missing_samplers))
    missing_symbols = [name for name in HLSL_ENGINE_SYMBOLS if name not in identifiers]
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


def assign_parameter_group(expression):
    try:
        expression.set_editor_property("group", PARAMETER_GROUP)
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


def build_custom_node(material, code, x, y):
    custom = add_node(material, unreal.MaterialExpressionCustom, x, y)
    custom.set_editor_property("description", "TDFluidVolumeRaymarch")
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property("inputs", [make_custom_input(name) for name in CUSTOM_INPUT_NAMES])
    custom.set_editor_property("code", code)
    stored_names = [str(item.get_editor_property("input_name")) for item in custom.get_editor_property("inputs")]
    if stored_names != list(CUSTOM_INPUT_NAMES):
        raise RuntimeError("Custom 노드 입력 이름 설정 확인 실패: " + ", ".join(stored_names))
    return custom


def apply_material_settings(material):
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("disable_depth_test", True)
    applied = {
        "blend_mode": "BLEND_TRANSLUCENT",
        "shading_model": "MSM_UNLIT",
        "two_sided": True,
        "disable_depth_test": True,
    }
    skipped = []
    for property_name, value in OPTIONAL_MATERIAL_SETTINGS:
        try:
            material.set_editor_property(property_name, value)
        except Exception:
            skipped.append(property_name)
            continue
        applied[property_name] = value
    return applied, skipped


def prepare_master_material(overwrite):
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(MASTER_PATH):
        if not overwrite:
            raise RuntimeError(f"기존 에셋 보존: {MASTER_PATH}")
        material = assets.load_asset(MASTER_PATH)
        if not isinstance(material, unreal.Material):
            raise RuntimeError(f"기존 에셋 형식 불일치: {MASTER_PATH}")
        unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
        return material, False

    ensure_directory(MATERIAL_DIR)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        MASTER_NAME, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew()
    )
    if material is None:
        raise RuntimeError(f"마스터 재질 생성 실패: {MASTER_PATH}")
    return material, True


def build_master_graph(material, code, default_texture, sampler_type):
    vector_class = unreal.MaterialExpressionVectorParameter
    scalar_class = unreal.MaterialExpressionScalarParameter
    sources = {}

    world_position = add_node(material, unreal.MaterialExpressionWorldPosition, -1500, -1100)
    camera_position = add_node(material, unreal.MaterialExpressionCameraPositionWS, -1500, -980)
    sources["PixelWorldPosition"] = (truncate_if_available(material, world_position, -1300, -1100), "")
    sources["CameraWorldPosition"] = (truncate_if_available(material, camera_position, -1300, -980), "")
    sources["SceneDepthCm"] = (add_node(material, unreal.MaterialExpressionSceneDepth, -1300, -860), "")
    sources["PixelDepthCm"] = (add_node(material, unreal.MaterialExpressionPixelDepth, -1300, -760), "")
    sources["ElapsedSeconds"] = (add_node(material, unreal.MaterialExpressionTime, -1300, -660), "")

    for row, texture_name in enumerate(TEXTURE_PARAMETER_NAMES):
        texture_node = add_node(
            material,
            unreal.MaterialExpressionTextureObjectParameter,
            -1300,
            -520 + row * 140,
            parameter_name=texture_name,
            texture=default_texture,
            sampler_type=sampler_type,
        )
        assign_parameter_group(texture_node)
        sources[texture_name] = (texture_node, "")

    for row, (parameter_name, default_value) in enumerate(VECTOR_PARAMETER_DEFAULTS):
        vector_node = add_node(
            material,
            vector_class,
            -1300,
            -200 + row * 130,
            parameter_name=parameter_name,
            default_value=unreal.LinearColor(*default_value),
        )
        assign_parameter_group(vector_node)
        sources[parameter_name] = (vector_node, "RGB")

    for row, (parameter_name, default_value) in enumerate(SCALAR_PARAMETER_DEFAULTS):
        scalar_node = add_node(
            material,
            scalar_class,
            -1300,
            860 + row * 110,
            parameter_name=parameter_name,
            default_value=default_value,
        )
        assign_parameter_group(scalar_node)
        sources[parameter_name] = (scalar_node, "")

    custom = build_custom_node(material, code, -300, 0)
    for input_name in CUSTOM_INPUT_NAMES:
        source, source_output = sources[input_name]
        link(source, custom, input_name, source_output)

    color_mask = add_node(material, unreal.MaterialExpressionComponentMask, 200, -80, r=True, g=True, b=True, a=False)
    alpha_mask = add_node(material, unreal.MaterialExpressionComponentMask, 200, 80, r=False, g=False, b=False, a=True)
    link(custom, color_mask)
    link(custom, alpha_mask)
    connect_material_output(color_mask, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    connect_material_output(alpha_mask, unreal.MaterialProperty.MP_OPACITY)
    return custom


def verify_custom_inputs_connected(material, custom):
    getter = getattr(unreal.MaterialEditingLibrary, "get_inputs_for_material_expression", None)
    if getter is None:
        return "입력 연결 개수 확인 도구 없음"
    connected = [item for item in getter(material, custom) if item is not None]
    if len(connected) != len(CUSTOM_INPUT_NAMES):
        raise RuntimeError(f"Custom 입력 연결 개수 불일치: {len(connected)}/{len(CUSTOM_INPUT_NAMES)}")
    return f"{len(connected)}/{len(CUSTOM_INPUT_NAMES)}"


def verify_master_parameters(material):
    editing = unreal.MaterialEditingLibrary
    found = {
        "vector": {str(name) for name in editing.get_vector_parameter_names(material)},
        "scalar": {str(name) for name in editing.get_scalar_parameter_names(material)},
        "texture": {str(name) for name in editing.get_texture_parameter_names(material)},
    }
    expected = {
        "vector": [name for name, _ in VECTOR_PARAMETER_DEFAULTS],
        "scalar": [name for name, _ in SCALAR_PARAMETER_DEFAULTS],
        "texture": list(TEXTURE_PARAMETER_NAMES),
    }
    missing = [f"{kind}:{name}" for kind, names in expected.items() for name in names if name not in found[kind]]
    if missing:
        raise RuntimeError("볼륨 재질 파라미터 없음: " + ", ".join(missing))


def read_material_statistics(material):
    try:
        statistics = unreal.MaterialEditingLibrary.get_statistics(material)
        return {
            name: statistics.get_editor_property(name)
            for name in ("num_vertex_shader_instructions", "num_pixel_shader_instructions", "num_samplers")
        }
    except Exception:
        return {}


def compile_master_material(material):
    errors = [str(item) for item in unreal.MaterialEditingLibrary.recompile_material(material) or []]
    if errors:
        raise RuntimeError("볼륨 재질 컴파일 실패(Saved/Logs/TDGame.log 의 LogMaterial 다음 들여쓴 줄 확인): " + " | ".join(errors))
    statistics = read_material_statistics(material)
    if statistics and not statistics.get("num_pixel_shader_instructions"):
        raise RuntimeError(
            "볼륨 재질 셰이더 컴파일 결과 없음(픽셀 셰이더 명령 0). Saved/Logs/TDGame.log 의 LogMaterial·LogShaderCompilers 다음 들여쓴 줄 확인"
        )
    return statistics


def create_master_material(code, default_texture, sampler_type, overwrite):
    material, was_created = prepare_master_material(overwrite)
    settings, skipped_settings = apply_material_settings(material)
    custom = build_master_graph(material, code, default_texture, sampler_type)
    connected = verify_custom_inputs_connected(material, custom)
    statistics = compile_master_material(material)
    verify_master_parameters(material)
    save_asset(material, MASTER_PATH)
    return material, {
        "created": was_created,
        "settings": settings,
        "skipped_settings": skipped_settings,
        "custom_inputs_connected": connected,
        "statistics_available": bool(statistics),
        "statistics": statistics,
    }


def prepare_instance(spec, overwrite):
    assets = unreal.EditorAssetLibrary
    asset_path = instance_asset_path(spec)
    if assets.does_asset_exist(asset_path):
        if not overwrite:
            raise RuntimeError(f"기존 에셋 보존: {asset_path}")
        instance = assets.load_asset(asset_path)
        if not isinstance(instance, unreal.MaterialInstanceConstant):
            raise RuntimeError(f"기존 에셋 형식 불일치: {asset_path}")
        return instance
    instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        spec["name"], MATERIAL_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew()
    )
    if instance is None:
        raise RuntimeError(f"재질 인스턴스 생성 실패: {asset_path}")
    return instance


def create_material_instance(spec, master, overwrite):
    editing = unreal.MaterialEditingLibrary
    asset_path = instance_asset_path(spec)
    instance = prepare_instance(spec, overwrite)
    editing.set_material_instance_parent(instance, master)

    vector_values = {
        "SmokeColor": SMOKE_COLOR,
        "ShadowColor": SHADOW_COLOR,
        "AmbientColor": AMBIENT_COLOR,
    }
    vector_values.update(spec.get("vector_overrides", {}))
    scalar_values = {"RenderMode": spec["render_mode"]}
    if "extrude_symmetric" in spec:
        scalar_values["ExtrudeSymmetric"] = spec["extrude_symmetric"]
    scalar_values.update(spec.get("scalar_overrides", {}))
    for parameter_name, rgb in vector_values.items():
        editing.set_material_instance_vector_parameter_value(
            instance, parameter_name, unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0)
        )
    for parameter_name, value in scalar_values.items():
        editing.set_material_instance_scalar_parameter_value(instance, parameter_name, value)
    editing.update_material_instance(instance)

    for parameter_name, rgb in vector_values.items():
        stored = editing.get_material_instance_vector_parameter_value(instance, parameter_name)
        error = max(abs(stored.r - rgb[0]), abs(stored.g - rgb[1]), abs(stored.b - rgb[2]))
        if error > 0.001:
            raise RuntimeError(f"재질 인스턴스 색 설정 확인 실패: {asset_path}.{parameter_name}")
    for parameter_name, value in scalar_values.items():
        stored = editing.get_material_instance_scalar_parameter_value(instance, parameter_name)
        if abs(stored - value) > 0.001:
            raise RuntimeError(f"재질 인스턴스 스칼라 설정 확인 실패: {asset_path}.{parameter_name}")
    save_asset(instance, asset_path)
    return {"asset": asset_path, "render_mode": spec["render_mode"], "scalar_values": scalar_values}


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


def main():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    assets = unreal.EditorAssetLibrary

    missing_names = find_missing_unreal_names()
    if missing_names:
        raise RuntimeError("언리얼 Python 이름 없음: " + ", ".join(missing_names))
    if editor.get_game_world() is not None:
        raise RuntimeError("PIE를 종료한 뒤 실행하세요.")

    hlsl_path, code = load_hlsl_code()
    problems = validate_hlsl_code(code)
    if problems:
        raise RuntimeError("HLSL 자체 점검 실패: " + " | ".join(problems))

    overwrite = read_overwrite_request()
    existing_paths = [path for path in collect_target_asset_paths() if assets.does_asset_exist(path)]
    if existing_paths and not overwrite:
        print(json.dumps({
            "status": "기존 에셋이 있어 아무것도 바꾸지 않았습니다",
            "existing_assets": existing_paths,
            "overwrite_request_file": project_path(*REQUEST_RELATIVE_PATH),
            "overwrite_request_content": {"overwrite": True},
        }, ensure_ascii=False))
        raise RuntimeError("기존 에셋 보존: overwrite 요청 파일이 없어 중단합니다.")

    unsaved_names = find_unsaved_package_names(overwrite)
    if unsaved_names:
        raise RuntimeError("미저장 작업을 보존합니다: " + ", ".join(unsaved_names))

    print("단계: 기본 텍스처 선택")
    texture_path, default_texture, sampler_type = pick_default_texture()

    print("단계: 마스터 재질 생성")
    master, master_summary = create_master_material(code, default_texture, sampler_type, overwrite)

    print("단계: 재질 인스턴스 생성")
    instances = [create_material_instance(spec, master, overwrite) for spec in INSTANCE_SPECS]

    missing_files = find_missing_saved_files()
    if missing_files:
        raise RuntimeError("저장 파일 확인 실패: " + ", ".join(missing_files))

    report = {
        "master": MASTER_PATH,
        "overwrite": overwrite,
        "hlsl": {
            "path": hlsl_path,
            "characters": len(code),
            "sha256": hashlib.sha256(code.encode("utf-8")).hexdigest(),
        },
        "default_texture": texture_path,
        "sampler_type": str(sampler_type),
        "master_summary": master_summary,
        "instances": instances,
        "custom_inputs": list(CUSTOM_INPUT_NAMES),
        "saved": True,
    }
    report_path = write_report(report)
    print(json.dumps(report, ensure_ascii=False, default=str))
    print("보고서: " + report_path)


if __name__ == "__main__":
    main()
