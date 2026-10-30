"""실행 환경: TDGame 언리얼 에디터 안(Python 원격 실행). W-3 비 재질 2종과 인스턴스 2개를 만든다: M_TD_FluidRainDrop(Unlit·Translucent, 나이아가라 스프라이트 사용 플래그를 노드 생성 전에 켬, Custom = Shaders/TDFluidRainDrop.hlsl, 방출 = RainTint x RainEmissive, 불투명도 = Custom 알파)와 M_TD_FluidRainPuddleDecal(DeferredDecal·Translucent, Custom = Shaders/TDFluidRainPuddleDecal.hlsl FLOAT4 x 마스크 yz 법선, 색 PuddleColor·거칠기 PuddleRoughness·법선 normalize(float3(yz,1))·불투명도 x PuddleOpacity), MI_TD_FluidRainDrop_Default·MI_TD_FluidRainPuddleDecal_Default(값은 모양 정본 TDFluidMaterialLook.json Materials, 만든 뒤 다시 읽어 판정).
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_rain_assets.py (PowerShell, L-repo-01. 모양 정본에 조각 look-fragment-w3.json 이 병합된 뒤. 기존 에셋을 다시 지을 때는 Saved/FluidFX/fluid_rain_assets_request.json 에 {"overwrite": true})
출력: /Game/FluidFX/Materials/{M_TD_FluidRainDrop, M_TD_FluidRainPuddleDecal, MI_TD_FluidRainDrop_Default, MI_TD_FluidRainPuddleDecal_Default}, Saved/FluidFX/fluid_rain_assets.json(status·재질 통계·HLSL sha256·material_look_sha256, run_in_editor 가 SUCCESS 여도 출력 본문 status 로 판정 L-editor-73). 기존 에셋은 overwrite 요청이 없으면 하나도 바꾸지 않고 중단하며 미저장 작업이 있으면 중단한다.
상태: 미검증(2026-10-29 파동 3 W-3 D 작성: py_compile·가짜 unreal 순수 함수 검사만, 에디터 실행 전). 웅덩이 데칼의 RainPuddleLevelCm 등 6개와 공유 물 입력 7개는 C++ MID 가 넣는 값이라 모양 정본에 두지 않는다(R7). HLSL 파일 두 개는 S3 가 작성한 입력 이름과 이 파일의 입력 표가 어긋나면 재질을 만들기 전에 중단한다.
"""
import hashlib
import importlib.util
import json
import os
import re

import unreal

MATERIAL_DIR = "/Game/FluidFX/Materials"
DROP_MASTER_NAME = "M_TD_FluidRainDrop"
DROP_INSTANCE_NAME = "MI_TD_FluidRainDrop_Default"
PUDDLE_MASTER_NAME = "M_TD_FluidRainPuddleDecal"
PUDDLE_INSTANCE_NAME = "MI_TD_FluidRainPuddleDecal_Default"
PARAMETER_GROUP = "TDFluidRain"
SHADER_DIRECTORY = ("Tools", "FluidFX", "Shaders")
DROP_HLSL_NAME = "TDFluidRainDrop.hlsl"
PUDDLE_HLSL_NAME = "TDFluidRainPuddleDecal.hlsl"
MATERIAL_LOOK_MODULE_RELATIVE_PATH = ("Tools", "FluidFX", "fluid_material_look.py")
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_rain_assets_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_rain_assets.json")
DEFAULT_TEXTURE_PATHS = ("/Engine/EngineResources/Black", "/Engine/EngineResources/DefaultTexture")

DROP_INPUT_NAMES = ("ParticleColor", "TexCoord", "RainStreakWidthRatio", "RainSplashRingWidth")
DROP_LOOK_SCALAR_NAMES = ("RainEmissive", "RainStreakWidthRatio", "RainSplashRingWidth")
DROP_LOOK_VECTOR_NAMES = ("RainTint",)

PUDDLE_SHARED_INPUT_NAMES = (
    "WaterSurfaceTexture",
    "WaterWorldPosition",
    "SimCenterWorld",
    "SimExtentsCm",
    "SimGridResolution",
    "TerrainReferenceZCm",
    "WaterDepthReferenceCm",
)
PUDDLE_RUNTIME_INPUT_NAMES = (
    "RainPuddleLevelCm",
    "RainPuddleCavityRadiusCm",
    "RainPuddleCavityReferenceCm",
    "RainIntensity",
    "RainDropsPerSquareMeterPerSec",
    "RainTimeSeconds",
)
PUDDLE_LOOK_INPUT_NAMES = (
    "PuddleEdgeSoftnessCm",
    "PuddleHeightToleranceCm",
    "PuddleRippleNormalStrength",
    "PuddleRippleCellCm",
    "PuddleRippleSpeedCmPerSec",
    "PuddleRippleWidthCm",
    "PuddleRippleDecayPerSec",
)
RAIN_PUDDLE_INPUT_NAMES = PUDDLE_SHARED_INPUT_NAMES + PUDDLE_RUNTIME_INPUT_NAMES + PUDDLE_LOOK_INPUT_NAMES
PUDDLE_GRAPH_SCALAR_NAMES = ("PuddleRoughness", "PuddleOpacity")
PUDDLE_GRAPH_VECTOR_NAMES = ("PuddleColor",)
PUDDLE_LOOK_SCALAR_NAMES = PUDDLE_LOOK_INPUT_NAMES + PUDDLE_GRAPH_SCALAR_NAMES
PUDDLE_MASTER_SCALAR_NAMES = PUDDLE_LOOK_SCALAR_NAMES + PUDDLE_RUNTIME_INPUT_NAMES + ("SimGridResolution", "TerrainReferenceZCm", "WaterDepthReferenceCm")
PUDDLE_MASTER_VECTOR_NAMES = PUDDLE_GRAPH_VECTOR_NAMES + ("SimCenterWorld", "SimExtentsCm")
PUDDLE_SAMPLER_NAMES = ("WaterSurfaceTextureSampler",)
DROP_SAMPLER_NAMES = ()

SCALAR_DEFAULTS = {
    "RainEmissive": 1.0,
    "RainStreakWidthRatio": 0.12,
    "RainSplashRingWidth": 0.15,
    "SimGridResolution": 0.0,
    "TerrainReferenceZCm": 0.0,
    "WaterDepthReferenceCm": 300.0,
    "RainPuddleLevelCm": 0.0,
    "RainPuddleCavityRadiusCm": 150.0,
    "RainPuddleCavityReferenceCm": 4.0,
    "RainIntensity": 0.0,
    "RainDropsPerSquareMeterPerSec": 0.0,
    "RainTimeSeconds": 0.0,
    "PuddleEdgeSoftnessCm": 0.4,
    "PuddleHeightToleranceCm": 30.0,
    "PuddleRippleNormalStrength": 0.6,
    "PuddleRippleCellCm": 12.0,
    "PuddleRippleSpeedCmPerSec": 25.0,
    "PuddleRippleWidthCm": 3.0,
    "PuddleRippleDecayPerSec": 4.0,
    "PuddleRoughness": 0.04,
    "PuddleOpacity": 0.85,
}
VECTOR_DEFAULTS = {
    "RainTint": (0.75, 0.8, 0.85, 1.0),
    "SimCenterWorld": (0.0, 0.0, 0.0, 1.0),
    "SimExtentsCm": (3600.0, 3600.0, 0.1, 0.0),
    "PuddleColor": (0.05, 0.06, 0.07, 1.0),
}

REQUIRED_UNREAL_NAMES = (
    "AssetToolsHelpers.get_asset_tools",
    "Material",
    "MaterialFactoryNew",
    "MaterialInstanceConstant",
    "MaterialInstanceConstantFactoryNew",
    "EditorAssetLibrary.load_asset",
    "EditorAssetLibrary.save_loaded_asset",
    "EditorAssetLibrary.delete_asset",
    "EditorAssetLibrary.does_asset_exist",
    "MaterialEditingLibrary.create_material_expression",
    "MaterialEditingLibrary.connect_material_expressions",
    "MaterialEditingLibrary.connect_material_property",
    "MaterialEditingLibrary.recompile_material",
    "MaterialEditingLibrary.has_material_usage",
    "MaterialEditingLibrary.set_material_instance_parent",
    "MaterialEditingLibrary.update_material_instance",
    "MaterialEditingLibrary.get_scalar_parameter_names",
    "MaterialEditingLibrary.get_vector_parameter_names",
    "MaterialEditingLibrary.get_texture_parameter_names",
    "MaterialExpressionParticleColor",
    "MaterialExpressionTextureCoordinate",
    "MaterialExpressionScalarParameter",
    "MaterialExpressionVectorParameter",
    "MaterialExpressionTextureObjectParameter",
    "MaterialExpressionMultiply",
    "MaterialExpressionComponentMask",
    "MaterialExpressionAppendVector",
    "MaterialExpressionNormalize",
    "MaterialExpressionConstant",
    "MaterialExpressionCustom",
    "MaterialExpressionWorldPosition",
    "CustomInput",
    "CustomMaterialOutputType.CMOT_FLOAT1",
    "CustomMaterialOutputType.CMOT_FLOAT4",
    "MaterialUsage.MATUSAGE_NIAGARA_SPRITES",
    "MaterialProperty.MP_EMISSIVE_COLOR",
    "MaterialProperty.MP_OPACITY",
    "MaterialProperty.MP_BASE_COLOR",
    "MaterialProperty.MP_ROUGHNESS",
    "MaterialProperty.MP_NORMAL",
    "MaterialDomain.MD_DEFERRED_DECAL",
    "BlendMode.BLEND_TRANSLUCENT",
    "MaterialShadingModel.MSM_UNLIT",
    "TextureCompressionSettings.TC_DEFAULT",
    "TextureCompressionSettings.TC_NORMALMAP",
    "MaterialSamplerType.SAMPLERTYPE_COLOR",
    "MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR",
    "MaterialSamplerType.SAMPLERTYPE_NORMAL",
    "EditorLoadingAndSavingUtils.get_dirty_map_packages",
    "EditorLoadingAndSavingUtils.get_dirty_content_packages",
    "SystemLibrary.get_project_directory",
)


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


def asset_path(name):
    return f"{MATERIAL_DIR}/{name}"


def asset_disk_path(path):
    return project_path("Content", path[len("/Game/"):] + ".uasset")


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


def load_material_look_module():
    module_path = project_path(*MATERIAL_LOOK_MODULE_RELATIVE_PATH)
    module_spec = importlib.util.spec_from_file_location("fluid_material_look", module_path)
    if module_spec is None or module_spec.loader is None:
        raise RuntimeError(f"모양 정본 모듈 없음: {module_path}")
    material_look = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(material_look)
    return material_look


def read_request():
    request_path = project_path(*REQUEST_RELATIVE_PATH)
    if not os.path.isfile(request_path):
        return {}
    with open(request_path, encoding="utf-8") as request_file:
        request = json.load(request_file)
    return request if isinstance(request, dict) else {}


def find_unsaved_package_names(overwrite):
    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    dirty.extend(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    names = sorted({package.get_name() for package in dirty})
    if not overwrite:
        return names
    return [name for name in names if not name.startswith(MATERIAL_DIR + "/")]


def read_text_file(path):
    if not os.path.isfile(path):
        raise RuntimeError(f"HLSL 파일 없음(S3 산출물): {path}")
    with open(path, encoding="utf-8") as text_file:
        return text_file.read()


def find_unbalanced_brackets(code):
    pairs = {")": "(", "]": "[", "}": "{"}
    stack = []
    for character in code:
        if character in "([{":
            stack.append(character)
        elif character in pairs and (not stack or stack.pop() != pairs[character]):
            return True
    return bool(stack)


def validate_hlsl_code(code, input_names, sampler_names):
    problems = []
    if not code.strip():
        problems.append("코드가 비었음")
        return problems
    if "#include" in code:
        problems.append("#include 사용 불가(Custom 노드 본문만)")
    if not re.search(r"\breturn\b", code):
        problems.append("return 없음")
    if find_unbalanced_brackets(code):
        problems.append("괄호 짝이 안 맞음")
    identifiers = set(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", code))
    unused = [name for name in input_names if name not in identifiers]
    if unused:
        problems.append("입력이 코드에서 쓰이지 않음: " + ", ".join(unused))
    unknown_samplers = sorted(name for name in identifiers if name.endswith("Sampler") and name not in sampler_names)
    if unknown_samplers:
        problems.append("모르는 샘플러: " + ", ".join(unknown_samplers))
    return problems


def read_hlsl(file_name, input_names, sampler_names):
    code = read_text_file(project_path(*SHADER_DIRECTORY, file_name))
    problems = validate_hlsl_code(code, input_names, sampler_names)
    if problems:
        raise RuntimeError(f"HLSL 자체 점검 실패 {file_name}: " + " | ".join(problems))
    return code


def find_look_problems(look_table):
    problems = []
    expectations = (
        (asset_path(DROP_INSTANCE_NAME), DROP_LOOK_SCALAR_NAMES, DROP_LOOK_VECTOR_NAMES),
        (asset_path(PUDDLE_INSTANCE_NAME), PUDDLE_LOOK_SCALAR_NAMES, PUDDLE_GRAPH_VECTOR_NAMES),
    )
    runtime_names = set(PUDDLE_SHARED_INPUT_NAMES) | set(PUDDLE_RUNTIME_INPUT_NAMES)
    for path, scalar_names, vector_names in expectations:
        entry = look_table.material(path)
        missing = [name for name in scalar_names if name not in entry["Scalars"]]
        missing.extend(name for name in vector_names if name not in entry["Vectors"])
        if missing:
            problems.append(f"{path}: 모양 정본에 없음 " + ", ".join(missing))
        owned = sorted(runtime_names & (set(entry["Scalars"]) | set(entry["Vectors"])))
        if owned:
            problems.append(f"{path}: C++ MID 소유 이름이 모양 정본에 있음(R7) " + ", ".join(owned))
    return problems


def choose_sampler_type(texture):
    if texture is None:
        return None
    try:
        compression = texture.get_editor_property("compression_settings")
        is_srgb = bool(texture.get_editor_property("srgb"))
    except Exception:
        return None
    if compression == unreal.TextureCompressionSettings.TC_NORMALMAP:
        return unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
    if compression != unreal.TextureCompressionSettings.TC_DEFAULT:
        return None
    if is_srgb:
        return unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
    return unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR


def pick_default_texture():
    for texture_path in DEFAULT_TEXTURE_PATHS:
        texture = unreal.EditorAssetLibrary.load_asset(texture_path)
        sampler_type = choose_sampler_type(texture)
        if sampler_type is not None:
            return texture_path, texture, sampler_type
    raise RuntimeError("기본 텍스처로 쓸 수 있는 엔진 텍스처가 없음: " + ", ".join(DEFAULT_TEXTURE_PATHS))


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


def connect_output(expression, material_property, source_output=""):
    if not unreal.MaterialEditingLibrary.connect_material_property(expression, source_output, material_property):
        raise RuntimeError(f"재질 출력 연결 실패: {material_property} <- {source_output}")


def add_scalar_parameter(material, name, x, y):
    return add_node(material, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=SCALAR_DEFAULTS[name], group=PARAMETER_GROUP)


def add_vector_parameter(material, name, x, y):
    default_value = unreal.LinearColor(*VECTOR_DEFAULTS[name])
    return add_node(material, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=default_value, group=PARAMETER_GROUP)


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


def verify_custom_inputs_connected(material, custom, expected_count):
    getter = getattr(unreal.MaterialEditingLibrary, "get_inputs_for_material_expression", None)
    if getter is None:
        return "입력 연결 개수 확인 도구 없음"
    connected_count = len([item for item in getter(material, custom) if item is not None])
    if connected_count != expected_count:
        raise RuntimeError(f"Custom 노드 연결 수 불일치: 기대 {expected_count} 실제 {connected_count}")
    return f"{connected_count}/{expected_count}"


def compile_material(material, label):
    errors = [str(item) for item in unreal.MaterialEditingLibrary.recompile_material(material) or []]
    if errors:
        raise RuntimeError(f"재질 컴파일 실패: {label}, {errors}")


def save_asset(asset, path):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"에셋 저장 실패: {path}")


def remove_asset_completely(path):
    assets = unreal.EditorAssetLibrary
    if not assets.does_asset_exist(path):
        return
    package = assets.load_asset(path).get_outermost()
    if not assets.delete_asset(path):
        raise RuntimeError(f"에셋 삭제 실패: {path}")
    disk_path = asset_disk_path(path)
    if not os.path.isfile(disk_path):
        return
    unreal.EditorLoadingAndSavingUtils.unload_packages([package])
    unreal.SystemLibrary.collect_garbage()
    os.remove(disk_path)
    unreal.AssetRegistryHelpers.get_asset_registry().scan_files_synchronous([disk_path], True)
    if os.path.isfile(disk_path):
        raise RuntimeError(f"삭제 뒤에도 디스크에 남음(L-editor-67, 에디터를 멈추고 미커밋 파일을 지우세요): {disk_path}")


def create_master(name):
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError(f"재질 생성 실패: {asset_path(name)}")
    return material


def build_drop_master(code):
    path = asset_path(DROP_MASTER_NAME)
    material = create_master(DROP_MASTER_NAME)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("used_with_niagara_sprites", True)
    particle_color = add_node(material, unreal.MaterialExpressionParticleColor, -900, -200)
    texture_coordinate = add_node(material, unreal.MaterialExpressionTextureCoordinate, -900, -80, coordinate_index=0)
    streak_width = add_scalar_parameter(material, "RainStreakWidthRatio", -900, 40)
    ring_width = add_scalar_parameter(material, "RainSplashRingWidth", -900, 160)
    custom = build_custom_node(material, "TDFluidRainDrop", unreal.CustomMaterialOutputType.CMOT_FLOAT1, DROP_INPUT_NAMES, code, -500, -100)
    link(particle_color, custom, "ParticleColor", "RGBA")
    link(texture_coordinate, custom, "TexCoord")
    link(streak_width, custom, "RainStreakWidthRatio")
    link(ring_width, custom, "RainSplashRingWidth")
    tint = add_vector_parameter(material, "RainTint", -500, 160)
    emissive = add_scalar_parameter(material, "RainEmissive", -500, 280)
    multiply = add_node(material, unreal.MaterialExpressionMultiply, -200, 200)
    link(tint, multiply, "A", "RGB")
    link(emissive, multiply, "B")
    connect_output(multiply, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    connect_output(custom, unreal.MaterialProperty.MP_OPACITY)
    connected_count = verify_custom_inputs_connected(material, custom, len(DROP_INPUT_NAMES))
    compile_material(material, path)
    if not unreal.MaterialEditingLibrary.has_material_usage(material, unreal.MaterialUsage.MATUSAGE_NIAGARA_SPRITES):
        raise RuntimeError(f"나이아가라 스프라이트 사용 플래그 확인 실패: {path}")
    save_asset(material, path)
    return material, {
        "asset": path,
        "blend_mode": "BLEND_TRANSLUCENT",
        "shading_model": "MSM_UNLIT",
        "usages": ["MATUSAGE_NIAGARA_SPRITES"],
        "custom_inputs": list(DROP_INPUT_NAMES),
        "custom_inputs_connected": connected_count,
    }


def add_puddle_source(material, name, texture_default, row):
    x, y = -1300, -1500 + row * 110
    if name == "WaterWorldPosition":
        return add_node(material, unreal.MaterialExpressionWorldPosition, x, y), ""
    if name == "WaterSurfaceTexture":
        _, texture, sampler_type = texture_default
        node = add_node(material, unreal.MaterialExpressionTextureObjectParameter, x, y, parameter_name=name, texture=texture, sampler_type=sampler_type, group=PARAMETER_GROUP)
        return node, ""
    if name in VECTOR_DEFAULTS:
        return add_vector_parameter(material, name, x, y), "RGB"
    return add_scalar_parameter(material, name, x, y), ""


def mask_channels(material, source, x, y, channels):
    mask = add_node(material, unreal.MaterialExpressionComponentMask, x, y, r="r" in channels, g="g" in channels, b="b" in channels, a="a" in channels)
    link(source, mask)
    return mask


def build_puddle_master(code, texture_default):
    path = asset_path(PUDDLE_MASTER_NAME)
    material = create_master(PUDDLE_MASTER_NAME)
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    custom = build_custom_node(material, "TDFluidRainPuddleDecal", unreal.CustomMaterialOutputType.CMOT_FLOAT4, RAIN_PUDDLE_INPUT_NAMES, code, -300, 0)
    for row, name in enumerate(RAIN_PUDDLE_INPUT_NAMES):
        source, source_output = add_puddle_source(material, name, texture_default, row)
        link(source, custom, name, source_output)
    color = add_vector_parameter(material, "PuddleColor", 0, -240)
    roughness = add_scalar_parameter(material, "PuddleRoughness", 0, -120)
    opacity_scale = add_scalar_parameter(material, "PuddleOpacity", 0, 360)
    mask_value = mask_channels(material, custom, 0, 240, "r")
    opacity = add_node(material, unreal.MaterialExpressionMultiply, 300, 300)
    link(mask_value, opacity, "A")
    link(opacity_scale, opacity, "B")
    slope = mask_channels(material, custom, 0, 0, "gb")
    one = add_node(material, unreal.MaterialExpressionConstant, 0, 120, r=1.0)
    append = add_node(material, unreal.MaterialExpressionAppendVector, 300, 60)
    link(slope, append, "A")
    link(one, append, "B")
    normal = add_node(material, unreal.MaterialExpressionNormalize, 600, 60)
    link(append, normal)
    connect_output(color, unreal.MaterialProperty.MP_BASE_COLOR, "RGB")
    connect_output(roughness, unreal.MaterialProperty.MP_ROUGHNESS)
    connect_output(normal, unreal.MaterialProperty.MP_NORMAL)
    connect_output(opacity, unreal.MaterialProperty.MP_OPACITY)
    connected_count = verify_custom_inputs_connected(material, custom, len(RAIN_PUDDLE_INPUT_NAMES))
    compile_material(material, path)
    save_asset(material, path)
    return material, {
        "asset": path,
        "material_domain": "MD_DEFERRED_DECAL",
        "blend_mode": "BLEND_TRANSLUCENT",
        "custom_inputs": list(RAIN_PUDDLE_INPUT_NAMES),
        "custom_inputs_connected": connected_count,
        "default_texture": texture_default[0],
    }


def collect_parameter_names(material):
    editing = unreal.MaterialEditingLibrary
    return {
        "scalars": sorted(str(name) for name in editing.get_scalar_parameter_names(material)),
        "vectors": sorted(str(name) for name in editing.get_vector_parameter_names(material)),
        "textures": sorted(str(name) for name in editing.get_texture_parameter_names(material)),
    }


def verify_master_parameters(material, scalar_names, vector_names):
    names = collect_parameter_names(material)
    missing = [name for name in scalar_names if name not in names["scalars"]]
    missing.extend(name for name in vector_names if name not in names["vectors"])
    if missing:
        raise RuntimeError(f"{material.get_name()} 마스터에 파라미터 없음: " + ", ".join(missing))
    return names


def build_instance(name, master, look_table):
    path = asset_path(name)
    instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, MATERIAL_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    if instance is None:
        raise RuntimeError(f"재질 인스턴스 생성 실패: {path}")
    entry = look_table.apply_to_instance(unreal.MaterialEditingLibrary, unreal.LinearColor, instance, path, master)
    save_asset(instance, path)
    return {"asset": path, "scalar_overrides": entry["Scalars"], "vector_overrides": entry["Vectors"]}


def file_sha256(path):
    with open(path, "rb") as source_file:
        return hashlib.sha256(source_file.read()).hexdigest()


def write_report(report):
    report_path = project_path(*REPORT_RELATIVE_PATH)
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, "w", encoding="utf-8") as report_file:
        json.dump(report, report_file, ensure_ascii=False, indent=2, default=str)
    return report_path


def main():
    missing_names = find_missing_unreal_names()
    if missing_names:
        raise RuntimeError("언리얼 Python 이름 없음: " + ", ".join(missing_names))
    request = read_request()
    overwrite = request.get("overwrite") is True
    target_names = (DROP_MASTER_NAME, PUDDLE_MASTER_NAME, DROP_INSTANCE_NAME, PUDDLE_INSTANCE_NAME)
    existing_paths = [asset_path(name) for name in target_names if unreal.EditorAssetLibrary.does_asset_exist(asset_path(name))]
    if existing_paths and not overwrite:
        raise RuntimeError("기존 에셋 보존(덮어쓰려면 " + project_path(*REQUEST_RELATIVE_PATH) + ' 에 {"overwrite": true}): ' + ", ".join(existing_paths))
    unsaved_names = find_unsaved_package_names(overwrite)
    if unsaved_names:
        raise RuntimeError("미저장 작업을 보존합니다: " + ", ".join(unsaved_names))

    print("단계: 모양 정본·HLSL 점검")
    material_look = load_material_look_module()
    look_table = material_look.TDMaterialLookTable.read_project(unreal.SystemLibrary.get_project_directory())
    look_problems = find_look_problems(look_table)
    if look_problems:
        raise RuntimeError("모양 정본 점검 실패(조각 look-fragment-w3.json 먼저 병합, L-repo-27): " + " | ".join(look_problems))
    drop_code = read_hlsl(DROP_HLSL_NAME, DROP_INPUT_NAMES, DROP_SAMPLER_NAMES)
    puddle_code = read_hlsl(PUDDLE_HLSL_NAME, RAIN_PUDDLE_INPUT_NAMES, PUDDLE_SAMPLER_NAMES)
    texture_default = pick_default_texture()

    for name in target_names:
        remove_asset_completely(asset_path(name))

    print("단계: 빗방울 재질")
    drop_master, drop_summary = build_drop_master(drop_code)
    drop_summary["parameters"] = verify_master_parameters(drop_master, DROP_LOOK_SCALAR_NAMES, DROP_LOOK_VECTOR_NAMES)
    print("단계: 웅덩이 데칼 재질")
    puddle_master, puddle_summary = build_puddle_master(puddle_code, texture_default)
    puddle_summary["parameters"] = verify_master_parameters(puddle_master, PUDDLE_MASTER_SCALAR_NAMES, PUDDLE_MASTER_VECTOR_NAMES)
    print("단계: 재질 인스턴스")
    instances = [build_instance(DROP_INSTANCE_NAME, drop_master, look_table), build_instance(PUDDLE_INSTANCE_NAME, puddle_master, look_table)]

    report = {
        "status": "ok",
        "overwrite": overwrite,
        "material_look_sha256": look_table.source_sha256(),
        "hlsl_sha256": {
            DROP_HLSL_NAME: file_sha256(project_path(*SHADER_DIRECTORY, DROP_HLSL_NAME)),
            PUDDLE_HLSL_NAME: file_sha256(project_path(*SHADER_DIRECTORY, PUDDLE_HLSL_NAME)),
        },
        "masters": [drop_summary, puddle_summary],
        "instances": instances,
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
        print("실패: " + str(error))
        raise


if __name__ == "__main__":
    run_with_failure_report()
