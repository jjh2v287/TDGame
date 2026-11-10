"""실행 환경: TDGame 언리얼 에디터 안에서 FluidFX 물 소품 마스터 재질 5개(액체 구 M_TD_FluidLiquidSphere, 폭포 판 M_TD_FluidWaterfallSheet, 원경 폭포 M_TD_FluidWaterfallFar, 수중 후처리 M_TD_FluidUnderwaterPostProcess, 유리 M_TD_FluidWaterPropGlass)와 재질 인스턴스 10개(구 3·판 1·원경 2·수중 2·유리 1·모래 흐름 1)를 만든다. 먼저 editor_make_fluid_water_material.py 가 T_TD_FluidFoamNoise·T_TD_FluidFlowPhaseNoise 를, editor_make_fluid_test_level.py 가 M_TD_FluidSurface 를 만들어 두어야 하고, 모양 정본에 look-fragment-w5.json 이 병합되어 있어야 한다.
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_water_props_material.py (PowerShell, L-repo-01. 기존 에셋을 다시 지을 때는 Saved/FluidFX/fluid_water_props_material_request.json 에 {"overwrite": true})
출력: /Game/FluidFX/Materials/M_TD_FluidLiquidSphere·M_TD_FluidWaterfallSheet·M_TD_FluidWaterfallFar·M_TD_FluidUnderwaterPostProcess·M_TD_FluidWaterPropGlass, MI_TD_FluidLiquidSphere_Clear·_Turbulent·_BloodOrb, MI_TD_FluidWaterfallSheet_Default, MI_TD_FluidWaterfallFar_White·_Blue, MI_TD_FluidUnderwater_Lake·_Tank, MI_TD_FluidWaterTankWall, MI_TD_Fluid_WaterySand, Saved/FluidFX/fluid_water_props_material.json(HLSL sha256, 마스터 기본값 다시 읽기 결과, 통계, 오류 수, 모양 정본 sha256, 인스턴스 값 일치 여부)
상태: 미검증(2026-10-29 W-5 M 작성: py_compile·가짜 unreal·check_fluid_water_props_hlsl.py 만, 에디터 실행 전). 마스터는 지우지 않고 그래프를 비운 뒤 다시 짓는다(L-editor-67). 후처리 위치 BL_SCENE_COLOR_BEFORE_DOF 는 getattr 로 확인해 없으면 멈춘다. 수중 마스터 기본값은 중립(안개 밀도 0·흡수 0·수면 높이 1e7)이고 판 마스터 VelocitySpeedReferenceCmPerSec 는 기본 600 이다(C++ 가 넣지 않음, refute M4·M5). 후처리 안의 월드 위치 노드가 장면 화소 위치를 돌려주는지는 에디터 실행 때 확인해야 한다.
"""
import hashlib
import importlib.util
import json
import os
import re

import unreal


FLUID_ROOT = "/Game/FluidFX"
MATERIAL_DIR = f"{FLUID_ROOT}/Materials"
TEXTURE_DIR = f"{FLUID_ROOT}/Textures"
SHADER_DIRECTORY = ("Tools", "FluidFX", "Shaders")
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_water_props_material_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_water_props_material.json")
MATERIAL_LOOK_MODULE_RELATIVE_PATH = ("Tools", "FluidFX", "fluid_material_look.py")
PARAMETER_GROUP = "TDFluidWaterProps"
DEFAULT_TEXTURE_PATHS = ("/Engine/EngineResources/Black", "/Engine/EngineResources/DefaultTexture")
BLACK_TEXTURE_KEY = "black"
TEXTURE_ASSET_PATHS = {
    "FoamNoiseTexture": f"{TEXTURE_DIR}/T_TD_FluidFoamNoise",
    "FlowNoiseTexture": f"{TEXTURE_DIR}/T_TD_FluidFlowPhaseNoise",
    "UnderwaterCausticsTexture": f"{TEXTURE_DIR}/T_TD_FluidCaustics",
}
SURFACE_MASTER_PATH = f"{MATERIAL_DIR}/M_TD_FluidSurface"
BLENDABLE_LOCATION_NAME = "BL_SCENE_COLOR_BEFORE_DOF"
SAND_INSTANCE_NAME = "MI_TD_Fluid_WaterySand"

RUNTIME_DRIVEN_NAMES = (
    "DensityPressureTexture", "VelocityTexture", "SimCenterWorld", "SimExtentsCm", "PlaneAxisU", "PlaneAxisV", "PlaneAxisN",
    "PlaneCoordinateOffsetCm", "SimGridResolution", "SimAreaWrapped", "VelocitySpeedReferenceCmPerSec", "UnderwaterSurfaceZCm",
)
BUILTIN_KINDS = {
    "PixelWorldPosition": "float3", "ObjectCenterWorld": "float3", "ObjectBoundsCm": "float3", "CardRightWorld": "float3",
    "CameraVectorWorld": "float3", "CameraWorldPosition": "float3", "EdgeUv": "float2", "SceneColor": "float3", "ElapsedSeconds": "float",
}

SPHERE_SCALARS = (
    ("SphereLongitudeRepeat", 2.0), ("SpherePoleFadeRatio", 0.08), ("SpherePoleDensity", 0.5), ("SphereDensityContrast", 1.2),
    ("SphereOpacityMin", 0.15), ("SphereOpacityMax", 0.55), ("SphereFresnelPower", 4.0), ("SphereFresnelStrength", 0.6),
    ("SphereRoughness", 0.05),
)
SPHERE_VECTORS = (
    ("SphereBaseColor", (0.55, 0.8, 0.9, 1.0)), ("SphereDenseColor", (0.85, 0.95, 1.0, 1.0)), ("SphereSelectiveColor", (0.6, 0.05, 0.05, 1.0)),
)
SHEET_SCALARS = (
    ("VelocitySpeedReferenceCmPerSec", 600.0), ("SheetOpacityScale", 1.4), ("SheetDensityContrast", 1.5), ("SheetStreakScaleCm", 40.0),
    ("SheetStreakStretch", 4.0), ("SheetFlowCycleSeconds", 0.6), ("SheetNoiseOpacityBase", 0.5), ("SheetNoiseOpacityGain", 0.5),
)
SHEET_VECTORS = (
    ("SimCenterWorld", (0.0, 0.0, 0.0, 1.0)), ("SimExtentsCm", (500.0, 800.0, 0.12, 0.0)), ("PlaneAxisU", (1.0, 0.0, 0.0, 0.0)),
    ("PlaneAxisV", (0.0, 0.0, 1.0, 0.0)), ("PlaneCoordinateOffsetCm", (0.0, 0.0, 0.0, 0.0)),
    ("SheetTint", (0.55, 0.72, 0.8, 1.0)), ("SheetFoamColor", (0.95, 0.97, 1.0, 1.0)),
)
FAR_SCALARS = (
    ("FarFlowSpeedCmPerSec", 300.0), ("FarNoiseScaleCm", 120.0), ("FarStreakStretch", 5.0), ("FarFlowCycleSeconds", 1.0),
    ("FarFoamCoverage", 0.6), ("FarOpacity", 0.85), ("FarEdgeFadeRatio", 0.12), ("FarMistHeightRatio", 0.15),
    ("FarFoamSharpness", 4.0), ("FarBaseOpacityBias", 0.4), ("FarFoamOpacityGain", 0.6),
)
FAR_VECTORS = (("FarTint", (0.9, 0.95, 1.0, 1.0)), ("FarFoamColor", (1.0, 1.0, 1.0, 1.0)))
UNDERWATER_SCALARS = (
    ("UnderwaterSurfaceZCm", 1.0e7), ("UnderwaterFogDensityPerCm", 0.0), ("UnderwaterMaxDistanceCm", 20000.0),
    ("UnderwaterCausticsStrength", 0.0), ("UnderwaterCausticsTilingCm", 220.0), ("UnderwaterCausticsDriftCmPerSec", 6.0),
    ("UnderwaterCausticsMeanLevel", 0.48), ("UnderwaterCausticsOnsetCm", 10.0), ("UnderwaterCausticsMaxDepthCm", 400.0),
    ("UnderwaterUpwardBrightness", 0.0),
)
UNDERWATER_VECTORS = (("UnderwaterFogColor", (0.02, 0.12, 0.18, 1.0)), ("UnderwaterAbsorptionPerCm", (0.0, 0.0, 0.0, 0.0)))
GLASS_SCALARS = (("GlassOpacity", 0.25), ("GlassFresnelPower", 3.0), ("GlassRoughness", 0.05))
GLASS_VECTORS = (("GlassTint", (0.35, 0.75, 0.75, 1.0)),)

MASTER_SPECS = (
    {
        "key": "sphere", "name": "M_TD_FluidLiquidSphere", "hlsl": "TDFluidLiquidSphere.hlsl", "output": "float4",
        "textures": ("DensityPressureTexture",),
        "inputs": (
            "DensityPressureTexture", "PixelWorldPosition", "ObjectCenterWorld", "CameraVectorWorld", "SphereLongitudeRepeat",
            "SpherePoleFadeRatio", "SpherePoleDensity", "SphereDensityContrast", "SphereOpacityMin", "SphereOpacityMax",
            "SphereFresnelPower", "SphereFresnelStrength", "SphereBaseColor", "SphereDenseColor", "SphereSelectiveColor",
        ),
        "scalars": SPHERE_SCALARS, "vectors": SPHERE_VECTORS, "direct_scalar_outputs": (("SphereRoughness", "MP_ROUGHNESS"),),
        "engine_symbols": ("Texture2DSampleLevel",), "two_sided": False,
    },
    {
        "key": "sheet", "name": "M_TD_FluidWaterfallSheet", "hlsl": "TDFluidWaterfallSheet.hlsl", "output": "float4",
        "textures": ("DensityPressureTexture", "VelocityTexture", "FoamNoiseTexture"),
        "inputs": (
            "DensityPressureTexture", "VelocityTexture", "FoamNoiseTexture", "SimCenterWorld", "SimExtentsCm", "PlaneAxisU", "PlaneAxisV",
            "PlaneCoordinateOffsetCm", "VelocitySpeedReferenceCmPerSec", "PixelWorldPosition", "ElapsedSeconds", "SheetOpacityScale",
            "SheetDensityContrast", "SheetStreakScaleCm", "SheetStreakStretch", "SheetFlowCycleSeconds", "SheetNoiseOpacityBase",
            "SheetNoiseOpacityGain", "SheetTint", "SheetFoamColor",
        ),
        "scalars": SHEET_SCALARS, "vectors": SHEET_VECTORS, "direct_scalar_outputs": (),
        "engine_symbols": ("Texture2DSampleLevel", "Texture2DSample"), "two_sided": True,
    },
    {
        "key": "far", "name": "M_TD_FluidWaterfallFar", "hlsl": "TDFluidWaterfallFar.hlsl", "output": "float4",
        "textures": ("FlowNoiseTexture", "FoamNoiseTexture"),
        "inputs": (
            "FlowNoiseTexture", "FoamNoiseTexture", "PixelWorldPosition", "ObjectCenterWorld", "ObjectBoundsCm", "CardRightWorld", "EdgeUv",
            "ElapsedSeconds", "FarFlowSpeedCmPerSec", "FarNoiseScaleCm", "FarStreakStretch", "FarFlowCycleSeconds", "FarFoamCoverage",
            "FarOpacity", "FarEdgeFadeRatio", "FarMistHeightRatio", "FarFoamSharpness", "FarBaseOpacityBias", "FarFoamOpacityGain",
            "FarTint", "FarFoamColor",
        ),
        "scalars": FAR_SCALARS, "vectors": FAR_VECTORS, "direct_scalar_outputs": (),
        "engine_symbols": ("Texture2DSample",), "two_sided": True,
    },
    {
        "key": "underwater", "name": "M_TD_FluidUnderwaterPostProcess", "hlsl": "TDFluidUnderwaterPostProcess.hlsl", "output": "float3",
        "textures": ("UnderwaterCausticsTexture",),
        "inputs": (
            "UnderwaterCausticsTexture", "SceneColor", "PixelWorldPosition", "CameraWorldPosition", "ElapsedSeconds", "UnderwaterSurfaceZCm",
            "UnderwaterFogDensityPerCm", "UnderwaterMaxDistanceCm", "UnderwaterFogColor", "UnderwaterAbsorptionPerCm",
            "UnderwaterCausticsStrength", "UnderwaterCausticsTilingCm", "UnderwaterCausticsDriftCmPerSec", "UnderwaterCausticsMeanLevel",
            "UnderwaterCausticsOnsetCm", "UnderwaterCausticsMaxDepthCm", "UnderwaterUpwardBrightness",
        ),
        "scalars": UNDERWATER_SCALARS, "vectors": UNDERWATER_VECTORS, "direct_scalar_outputs": (),
        "engine_symbols": ("Texture2DSample",), "two_sided": False,
    },
    {
        "key": "glass", "name": "M_TD_FluidWaterPropGlass", "hlsl": "", "output": "",
        "textures": (), "inputs": (),
        "scalars": GLASS_SCALARS, "vectors": GLASS_VECTORS, "direct_scalar_outputs": (),
        "engine_symbols": (), "two_sided": False,
    },
)
INSTANCE_SPECS = (
    ("MI_TD_FluidLiquidSphere_Clear", "sphere"), ("MI_TD_FluidLiquidSphere_Turbulent", "sphere"), ("MI_TD_FluidLiquidSphere_BloodOrb", "sphere"),
    ("MI_TD_FluidWaterfallSheet_Default", "sheet"), ("MI_TD_FluidWaterfallFar_White", "far"), ("MI_TD_FluidWaterfallFar_Blue", "far"),
    ("MI_TD_FluidUnderwater_Lake", "underwater"), ("MI_TD_FluidUnderwater_Tank", "underwater"), ("MI_TD_FluidWaterTankWall", "glass"),
)
REQUIRED_UNREAL_NAMES = (
    "EditorAssetLibrary.load_asset", "EditorAssetLibrary.save_loaded_asset", "EditorAssetLibrary.does_asset_exist",
    "EditorAssetLibrary.does_directory_exist", "EditorAssetLibrary.make_directory",
    "EditorLoadingAndSavingUtils.get_dirty_map_packages", "EditorLoadingAndSavingUtils.get_dirty_content_packages",
    "SystemLibrary.get_project_directory", "AssetToolsHelpers.get_asset_tools", "UnrealEditorSubsystem",
    "MaterialEditingLibrary.create_material_expression", "MaterialEditingLibrary.connect_material_expressions",
    "MaterialEditingLibrary.connect_material_property", "MaterialEditingLibrary.delete_all_material_expressions",
    "MaterialEditingLibrary.recompile_material",
    "MaterialEditingLibrary.get_vector_parameter_names", "MaterialEditingLibrary.get_scalar_parameter_names",
    "MaterialEditingLibrary.get_texture_parameter_names", "MaterialEditingLibrary.get_material_default_scalar_parameter_value",
    "MaterialEditingLibrary.get_material_default_vector_parameter_value", "MaterialEditingLibrary.get_material_default_texture_parameter_value",
    "MaterialEditingLibrary.set_material_instance_parent", "MaterialEditingLibrary.set_material_instance_vector_parameter_value",
    "MaterialEditingLibrary.set_material_instance_scalar_parameter_value", "MaterialEditingLibrary.get_material_instance_vector_parameter_value",
    "MaterialEditingLibrary.get_material_instance_scalar_parameter_value", "MaterialEditingLibrary.update_material_instance",
    "Material", "MaterialFactoryNew", "MaterialInstanceConstant", "MaterialInstanceConstantFactoryNew", "LinearColor",
    "MaterialExpressionCustom", "CustomInput", "CustomMaterialOutputType.CMOT_FLOAT3", "CustomMaterialOutputType.CMOT_FLOAT4",
    "MaterialExpressionWorldPosition", "MaterialExpressionObjectPositionWS", "MaterialExpressionObjectBounds", "MaterialExpressionCameraVectorWS",
    "MaterialExpressionCameraPositionWS", "MaterialExpressionTextureCoordinate", "MaterialExpressionTime", "MaterialExpressionSceneTexture",
    "SceneTextureId.PPI_POST_PROCESS_INPUT0", "MaterialExpressionTransform", "MaterialExpressionConstant3Vector", "MaterialExpressionConstant",
    "MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL", "MaterialVectorCoordTransform.TRANSFORM_WORLD",
    "MaterialExpressionTextureObjectParameter", "MaterialExpressionVectorParameter", "MaterialExpressionScalarParameter",
    "MaterialExpressionComponentMask", "MaterialExpressionLinearInterpolate", "MaterialExpressionFresnel",
    "MaterialDomain.MD_SURFACE", "MaterialDomain.MD_POST_PROCESS", "BlendMode.BLEND_TRANSLUCENT", "BlendMode.BLEND_OPAQUE",
    "MaterialShadingModel.MSM_DEFAULT_LIT", "MaterialProperty.MP_BASE_COLOR", "MaterialProperty.MP_OPACITY", "MaterialProperty.MP_ROUGHNESS",
    "MaterialProperty.MP_EMISSIVE_COLOR", "MaterialSamplerType.SAMPLERTYPE_COLOR", "MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR",
    "TextureCompressionSettings.TC_DEFAULT", "TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP",
)

IDENTIFIER_PATTERN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


def asset_path(name):
    return f"{MATERIAL_DIR}/{name}"


def master_spec(key):
    return next(spec for spec in MASTER_SPECS if spec["key"] == key)


def master_path(spec):
    return asset_path(spec["name"])


def input_kind(spec, name):
    if name in spec["textures"]:
        return "texture"
    if name in BUILTIN_KINDS:
        return BUILTIN_KINDS[name]
    if any(name == scalar_name for scalar_name, _ in spec["scalars"]):
        return "float"
    return "float3"


def custom_input_names(spec):
    return tuple(spec["inputs"])


def sampler_names(spec):
    return tuple(f"{name}Sampler" for name in spec["textures"])


def has_hlsl(spec):
    return bool(spec["hlsl"])


def find_missing_unreal_names(required_names=REQUIRED_UNREAL_NAMES):
    missing = []
    for dotted_name in required_names:
        target = unreal
        for part in dotted_name.split("."):
            target = getattr(target, part, None)
            if target is None:
                missing.append(dotted_name)
                break
    return missing


def resolve_blendable_location():
    location_enum = getattr(unreal, "BlendableLocation", None)
    location = getattr(location_enum, BLENDABLE_LOCATION_NAME, None) if location_enum is not None else None
    if location is None:
        raise RuntimeError(f"unreal.BlendableLocation.{BLENDABLE_LOCATION_NAME} 없음(엔진 버전 확인, 다른 위치로 대체하지 않음)")
    return location


def load_material_look_table():
    module_path = project_path(*MATERIAL_LOOK_MODULE_RELATIVE_PATH)
    module_spec = importlib.util.spec_from_file_location("fluid_material_look", module_path)
    if module_spec is None or module_spec.loader is None:
        raise RuntimeError(f"모양 정본 모듈 없음: {module_path}")
    material_look = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(material_look)
    return material_look.TDMaterialLookTable.read_project(unreal.SystemLibrary.get_project_directory())


def read_overwrite_request():
    request_path = project_path(*REQUEST_RELATIVE_PATH)
    if not os.path.isfile(request_path):
        return False
    with open(request_path, encoding="utf-8") as request_file:
        request = json.load(request_file)
    return isinstance(request, dict) and request.get("overwrite") is True


def collect_target_asset_paths():
    return [master_path(spec) for spec in MASTER_SPECS] + [asset_path(name) for name, _ in INSTANCE_SPECS] + [asset_path(SAND_INSTANCE_NAME)]


def find_unsaved_package_names(overwrite):
    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    dirty.extend(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    names = sorted({package.get_name() for package in dirty})
    return [name for name in names if not name.startswith(FLUID_ROOT + "/")] if overwrite else names


def ensure_directory(directory_path):
    if unreal.EditorAssetLibrary.does_directory_exist(directory_path):
        return
    if not unreal.EditorAssetLibrary.make_directory(directory_path):
        raise RuntimeError(f"폴더 생성 실패: {directory_path}")


def save_asset(asset, path):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"에셋 저장 실패: {path}")


def load_hlsl_code(spec):
    hlsl_path = project_path(*SHADER_DIRECTORY, spec["hlsl"])
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
        if character in pairs and (not stack or stack.pop() != pairs[character]):
            return f"괄호 불일치 위치 {index}"
    return f"닫히지 않은 괄호 {len(stack)}개" if stack else ""


def validate_hlsl_code(spec, code):
    problems = []
    identifiers = set(IDENTIFIER_PATTERN.findall(code))
    unused_inputs = [name for name in custom_input_names(spec) if name not in identifiers]
    if unused_inputs:
        problems.append(f"{spec['name']}: HLSL이 쓰지 않는 Custom 입력 " + ", ".join(unused_inputs))
    missing_samplers = [name for name in sampler_names(spec) if name not in identifiers]
    if missing_samplers:
        problems.append(f"{spec['name']}: HLSL이 쓰지 않는 샘플러 " + ", ".join(missing_samplers))
    missing_symbols = [name for name in spec["engine_symbols"] if name not in identifiers]
    if missing_symbols:
        problems.append(f"{spec['name']}: HLSL에 없는 엔진 심볼 " + ", ".join(missing_symbols))
    bracket_problem = find_unbalanced_brackets(code)
    if bracket_problem:
        problems.append(f"{spec['name']}: {bracket_problem}")
    if "return" not in identifiers:
        problems.append(f"{spec['name']}: return 없음")
    if "//" in code or "/*" in code:
        problems.append(f"{spec['name']}: 주석이 있음")
    return problems


def find_instance_override_problems(entries):
    problems = []
    for name, key in INSTANCE_SPECS:
        spec = master_spec(key)
        entry = entries.get(asset_path(name))
        if entry is None:
            problems.append(f"{name}: 모양 정본에 없음")
            continue
        if entry.get("Parent") != master_path(spec):
            problems.append(f"{name}: 부모 {entry.get('Parent')} ≠ {master_path(spec)}")
        scalar_names = {item for item, _ in spec["scalars"]}
        vector_names = {item for item, _ in spec["vectors"]}
        used_scalars = set(entry.get("Scalars", {}))
        used_vectors = set(entry.get("Vectors", {}))
        unknown = sorted((used_scalars - scalar_names) | (used_vectors - vector_names))
        if unknown:
            problems.append(f"{name}: 마스터에 없거나 종류가 다른 이름 " + ", ".join(unknown))
        runtime = sorted((used_scalars | used_vectors | set(entry.get("Switches", {}))) & set(RUNTIME_DRIVEN_NAMES))
        if runtime:
            problems.append(f"{name}: C++ 구동 이름을 덮어씀 " + ", ".join(runtime))
        missing = sorted((scalar_names | vector_names) - set(RUNTIME_DRIVEN_NAMES) - used_scalars - used_vectors)
        if missing:
            problems.append(f"{name}: 시작값이 없는 모양 이름 " + ", ".join(missing))
    sand = entries.get(asset_path(SAND_INSTANCE_NAME))
    if sand is None:
        problems.append(f"{SAND_INSTANCE_NAME}: 모양 정본에 없음")
    elif sand.get("Parent") != SURFACE_MASTER_PATH:
        problems.append(f"{SAND_INSTANCE_NAME}: 부모 {sand.get('Parent')} ≠ {SURFACE_MASTER_PATH}")
    return problems


def has_editor_property(owner, property_name):
    try:
        owner.get_editor_property(property_name)
    except Exception:
        return False
    return True


def choose_sampler_type(texture):
    if texture is None:
        return None
    try:
        compression = texture.get_editor_property("compression_settings")
        is_srgb = bool(texture.get_editor_property("srgb"))
    except Exception:
        return None
    if compression not in (unreal.TextureCompressionSettings.TC_DEFAULT, unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP):
        return None
    return unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if is_srgb else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR


def load_texture_with_sampler(texture_path):
    texture = unreal.EditorAssetLibrary.load_asset(texture_path)
    sampler_type = choose_sampler_type(texture)
    if sampler_type is None:
        return None, None
    return texture, sampler_type


def pick_default_texture():
    for texture_path in DEFAULT_TEXTURE_PATHS:
        texture, sampler_type = load_texture_with_sampler(texture_path)
        if sampler_type is not None:
            return texture_path, texture, sampler_type
    raise RuntimeError("기본 텍스처로 쓸 수 있는 엔진 텍스처가 없음: " + ", ".join(DEFAULT_TEXTURE_PATHS))


def load_texture_table():
    table = {}
    _, black_texture, black_sampler = pick_default_texture()
    table[BLACK_TEXTURE_KEY] = (black_texture, black_sampler)
    for parameter_name, texture_path in TEXTURE_ASSET_PATHS.items():
        texture, sampler_type = load_texture_with_sampler(texture_path)
        if sampler_type is None:
            raise RuntimeError(f"텍스처 없음 또는 샘플러 형식 불명: {texture_path} (editor_make_fluid_water_material.py 먼저)")
        table[parameter_name] = (texture, sampler_type)
    return table


def texture_for_parameter(table, parameter_name):
    return table.get(parameter_name, table[BLACK_TEXTURE_KEY])


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


def connect_material_output(expression, material_property_name, source_output=""):
    material_property = getattr(unreal.MaterialProperty, material_property_name)
    if not unreal.MaterialEditingLibrary.connect_material_property(expression, source_output, material_property):
        raise RuntimeError(f"재질 출력 연결 실패: {material_property_name}")


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


def apply_required_properties(material, assignments):
    missing = [name for name, _ in assignments if not has_editor_property(material, name)]
    if missing:
        raise RuntimeError("재질 속성 이름 없음(엔진 버전 확인 필요): " + ", ".join(missing))
    applied = {}
    for name, value in assignments:
        material.set_editor_property(name, value)
        applied[name] = str(value)
    return applied


def prepare_master_material(spec, overwrite):
    assets = unreal.EditorAssetLibrary
    path = master_path(spec)
    if assets.does_asset_exist(path):
        if not overwrite:
            raise RuntimeError(f"기존 에셋 보존: {path}")
        material = assets.load_asset(path)
        if not isinstance(material, unreal.Material):
            raise RuntimeError(f"기존 에셋 형식 불일치: {path}")
        unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
        return material, False
    ensure_directory(MATERIAL_DIR)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(spec["name"], MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError(f"마스터 재질 생성 실패: {path}")
    return material, True


def build_custom_node(material, description, output_type, spec, code, x, y):
    custom = add_node(material, unreal.MaterialExpressionCustom, x, y)
    custom.set_editor_property("description", description)
    custom.set_editor_property("output_type", output_type)
    inputs = []
    for name in custom_input_names(spec):
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        inputs.append(custom_input)
    custom.set_editor_property("inputs", inputs)
    custom.set_editor_property("code", code)
    stored_names = [str(item.get_editor_property("input_name")) for item in custom.get_editor_property("inputs")]
    if stored_names != list(custom_input_names(spec)):
        raise RuntimeError("Custom 노드 입력 이름 설정 확인 실패: " + ", ".join(stored_names))
    return custom


def build_builtin_source(material, name, x, y):
    if name == "PixelWorldPosition":
        return truncate_if_available(material, add_node(material, unreal.MaterialExpressionWorldPosition, x - 220, y), x, y), ""
    if name == "ObjectCenterWorld":
        return truncate_if_available(material, add_node(material, unreal.MaterialExpressionObjectPositionWS, x - 220, y), x, y), ""
    if name == "CameraWorldPosition":
        return truncate_if_available(material, add_node(material, unreal.MaterialExpressionCameraPositionWS, x - 220, y), x, y), ""
    if name == "ObjectBoundsCm":
        return add_node(material, unreal.MaterialExpressionObjectBounds, x, y), ""
    if name == "CameraVectorWorld":
        return add_node(material, unreal.MaterialExpressionCameraVectorWS, x, y), ""
    if name == "ElapsedSeconds":
        return add_node(material, unreal.MaterialExpressionTime, x, y), ""
    if name == "EdgeUv":
        return add_node(material, unreal.MaterialExpressionTextureCoordinate, x, y, coordinate_index=0), ""
    if name == "SceneColor":
        scene_texture = add_node(material, unreal.MaterialExpressionSceneTexture, x, y, scene_texture_id=unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
        return scene_texture, "Color"
    if name == "CardRightWorld":
        local_right = add_node(material, unreal.MaterialExpressionConstant3Vector, x - 220, y, constant=unreal.LinearColor(1.0, 0.0, 0.0, 0.0))
        transform = add_node(
            material, unreal.MaterialExpressionTransform, x, y,
            transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,
            transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD,
        )
        link(local_right, transform)
        return transform, ""
    raise RuntimeError(f"모르는 내장 입력: {name}")


def build_parameter_sources(material, spec, textures):
    sources = {}
    row = 0

    def place():
        nonlocal row
        row += 1
        return -1500, -1600 + row * 120

    for name in spec["textures"]:
        x, y = place()
        texture, sampler_type = texture_for_parameter(textures, name)
        node = add_node(material, unreal.MaterialExpressionTextureObjectParameter, x, y, parameter_name=name, texture=texture, sampler_type=sampler_type)
        assign_parameter_group(node)
        sources[name] = (node, "")
    for name in spec["inputs"]:
        if name in BUILTIN_KINDS:
            x, y = place()
            sources[name] = build_builtin_source(material, name, x, y)
    for name, rgba in spec["vectors"]:
        x, y = place()
        node = add_node(material, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=unreal.LinearColor(*rgba))
        assign_parameter_group(node)
        sources[name] = (node, "RGB")
    for name, value in spec["scalars"]:
        x, y = place()
        node = add_node(material, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)
        assign_parameter_group(node)
        sources[name] = (node, "")
    return sources


def verify_custom_inputs_connected(material, custom, expected_count):
    getter = getattr(unreal.MaterialEditingLibrary, "get_inputs_for_material_expression", None)
    if getter is None:
        return "입력 연결 개수 확인 도구 없음"
    connected = [item for item in getter(material, custom) if item is not None]
    if len(connected) != expected_count:
        raise RuntimeError(f"Custom 입력 연결 개수 불일치: {len(connected)}/{expected_count}")
    return f"{len(connected)}/{expected_count}"


def build_custom_graph(material, spec, code, textures):
    sources = build_parameter_sources(material, spec, textures)
    is_vector4 = spec["output"] == "float4"
    output_type = unreal.CustomMaterialOutputType.CMOT_FLOAT4 if is_vector4 else unreal.CustomMaterialOutputType.CMOT_FLOAT3
    custom = build_custom_node(material, spec["name"], output_type, spec, code, -300, 0)
    for name in custom_input_names(spec):
        source, source_output = sources[name]
        link(source, custom, name, source_output)
    if is_vector4:
        color_mask = add_node(material, unreal.MaterialExpressionComponentMask, 200, -80, r=True, g=True, b=True, a=False)
        link(custom, color_mask)
        connect_material_output(color_mask, "MP_BASE_COLOR")
        alpha_mask = add_node(material, unreal.MaterialExpressionComponentMask, 200, 80, r=False, g=False, b=False, a=True)
        link(custom, alpha_mask)
        connect_material_output(alpha_mask, "MP_OPACITY")
    else:
        connect_material_output(custom, "MP_EMISSIVE_COLOR")
    for scalar_name, material_property_name in spec["direct_scalar_outputs"]:
        scalar_source, scalar_output = sources[scalar_name]
        connect_material_output(scalar_source, material_property_name, scalar_output)
    return custom


def build_glass_graph(material, spec):
    sources = build_parameter_sources(material, spec, {})
    fresnel = add_node(material, unreal.MaterialExpressionFresnel, -300, 0)
    exponent_source, exponent_output = sources["GlassFresnelPower"]
    link(exponent_source, fresnel, "ExponentIn", exponent_output)
    full_opacity = add_node(material, unreal.MaterialExpressionConstant, -300, 160, r=1.0)
    opacity = add_node(material, unreal.MaterialExpressionLinearInterpolate, 0, 0)
    opacity_source, opacity_output = sources["GlassOpacity"]
    link(opacity_source, opacity, "A", opacity_output)
    link(full_opacity, opacity, "B")
    link(fresnel, opacity, "Alpha")
    connect_material_output(opacity, "MP_OPACITY")
    tint_source, tint_output = sources["GlassTint"]
    connect_material_output(tint_source, "MP_BASE_COLOR", tint_output)
    roughness_source, roughness_output = sources["GlassRoughness"]
    connect_material_output(roughness_source, "MP_ROUGHNESS", roughness_output)
    return fresnel


def read_material_statistics(material):
    try:
        statistics = unreal.MaterialEditingLibrary.get_statistics(material)
        return {name: statistics.get_editor_property(name) for name in ("num_vertex_shader_instructions", "num_pixel_shader_instructions", "num_samplers")}
    except Exception:
        return {}


def compile_master_material(material, label):
    errors = [str(item) for item in unreal.MaterialEditingLibrary.recompile_material(material) or []]
    if errors:
        raise RuntimeError(f"{label} 컴파일 실패(Saved/Logs/TDGame.log 의 LogMaterial 다음 들여쓴 줄 확인): " + " | ".join(errors))
    statistics = read_material_statistics(material)
    if statistics and not statistics.get("num_pixel_shader_instructions"):
        raise RuntimeError(f"{label} 셰이더 컴파일 결과 없음(픽셀 셰이더 명령 0). Saved/Logs/TDGame.log 의 LogMaterial·LogShaderCompilers 다음 들여쓴 줄 확인")
    return statistics


def verify_master_parameters(material, spec):
    editing = unreal.MaterialEditingLibrary
    found = {
        "vector": {str(name) for name in editing.get_vector_parameter_names(material)},
        "scalar": {str(name) for name in editing.get_scalar_parameter_names(material)},
        "texture": {str(name) for name in editing.get_texture_parameter_names(material)},
    }
    expected = {
        "vector": [name for name, _ in spec["vectors"]],
        "scalar": [name for name, _ in spec["scalars"]],
        "texture": list(spec["textures"]),
    }
    missing = [f"{kind}:{name}" for kind, names in expected.items() for name in names if name not in found[kind]]
    if missing:
        raise RuntimeError(f"{spec['name']} 재질 파라미터 없음: " + ", ".join(missing))


def verify_master_defaults(material, spec, textures):
    editing = unreal.MaterialEditingLibrary
    mismatches = []
    for name, value in spec["scalars"]:
        stored = editing.get_material_default_scalar_parameter_value(material, name)
        if abs(stored - value) > 1e-6 * max(1.0, abs(value)):
            mismatches.append(f"scalar:{name} {stored} ≠ {value}")
    for name, rgba in spec["vectors"]:
        stored = editing.get_material_default_vector_parameter_value(material, name)
        if max(abs(a - b) for a, b in zip((stored.r, stored.g, stored.b, stored.a), rgba)) > 1e-6:
            mismatches.append(f"vector:{name} ({stored.r}, {stored.g}, {stored.b}, {stored.a}) ≠ {rgba}")
    for name in spec["textures"]:
        stored = editing.get_material_default_texture_parameter_value(material, name)
        expected_texture, _ = texture_for_parameter(textures, name)
        if stored is None or stored.get_name() != expected_texture.get_name():
            mismatches.append(f"texture:{name} {stored.get_name() if stored else None} ≠ {expected_texture.get_name()}")
    if mismatches:
        raise RuntimeError(f"{spec['name']} 마스터 기본값 다시 읽기 불일치: " + " | ".join(mismatches))
    return {"scalars": len(spec["scalars"]), "vectors": len(spec["vectors"]), "textures": len(spec["textures"])}


def create_master(spec, code, textures, blendable_location, overwrite):
    material, was_created = prepare_master_material(spec, overwrite)
    if spec["key"] == "underwater":
        assignments = (
            ("material_domain", unreal.MaterialDomain.MD_POST_PROCESS),
            ("blendable_location", blendable_location),
            ("blend_mode", unreal.BlendMode.BLEND_OPAQUE),
        )
    else:
        assignments = (
            ("material_domain", unreal.MaterialDomain.MD_SURFACE),
            ("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT),
            ("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT),
            ("two_sided", spec["two_sided"]),
        )
    settings = apply_required_properties(material, assignments)
    if spec["key"] == "glass":
        build_glass_graph(material, spec)
        connected = "노드만"
    else:
        custom = build_custom_graph(material, spec, code, textures)
        connected = verify_custom_inputs_connected(material, custom, len(custom_input_names(spec)))
    statistics = compile_master_material(material, spec["name"])
    verify_master_parameters(material, spec)
    defaults = verify_master_defaults(material, spec, textures)
    save_asset(material, master_path(spec))
    return material, {"created": was_created, "settings": settings, "custom_inputs_connected": connected, "statistics": statistics, "defaults_reread": defaults}


def prepare_instance(name, overwrite):
    assets = unreal.EditorAssetLibrary
    path = asset_path(name)
    if assets.does_asset_exist(path):
        if not overwrite:
            raise RuntimeError(f"기존 에셋 보존: {path}")
        instance = assets.load_asset(path)
        if not isinstance(instance, unreal.MaterialInstanceConstant):
            raise RuntimeError(f"기존 에셋 형식 불일치: {path}")
        return instance
    instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, MATERIAL_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    if instance is None:
        raise RuntimeError(f"재질 인스턴스 생성 실패: {path}")
    return instance


def create_material_instance(name, parent, overwrite, look_table):
    path = asset_path(name)
    instance = prepare_instance(name, overwrite)
    entry = look_table.apply_to_instance(unreal.MaterialEditingLibrary, unreal.LinearColor, instance, path, parent)
    save_asset(instance, path)
    return {"asset": path, "scalar_overrides": entry["Scalars"], "vector_overrides": entry["Vectors"], "matches_look_table": True}


def load_surface_master():
    surface = unreal.EditorAssetLibrary.load_asset(SURFACE_MASTER_PATH)
    if not isinstance(surface, unreal.Material):
        raise RuntimeError(f"모래 흐름 부모 재질 없음: {SURFACE_MASTER_PATH} (editor_make_fluid_test_level.py 먼저)")
    return surface


def write_report(report):
    report_path = project_path(*REPORT_RELATIVE_PATH)
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, "w", encoding="utf-8") as report_file:
        json.dump(report, report_file, ensure_ascii=False, indent=2, default=str)
    return report_path


def find_missing_saved_files():
    expected = [project_path("Content", *path[len("/Game/"):].split("/")) + ".uasset" for path in collect_target_asset_paths()]
    return [path for path in expected if not os.path.isfile(path)]


def describe_hlsl(hlsl_path, code):
    return {"path": hlsl_path, "characters": len(code), "sha256": hashlib.sha256(code.encode("utf-8")).hexdigest()}


def main():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    assets = unreal.EditorAssetLibrary

    missing_names = find_missing_unreal_names()
    if missing_names:
        raise RuntimeError("언리얼 Python 이름 없음: " + ", ".join(missing_names))
    blendable_location = resolve_blendable_location()
    if editor.get_game_world() is not None:
        raise RuntimeError("PIE를 종료한 뒤 실행하세요.")

    codes = {}
    hlsl_reports = {}
    problems = []
    for spec in MASTER_SPECS:
        if not has_hlsl(spec):
            continue
        hlsl_path, code = load_hlsl_code(spec)
        codes[spec["key"]] = code
        hlsl_reports[spec["key"]] = describe_hlsl(hlsl_path, code)
        problems += validate_hlsl_code(spec, code)
    if problems:
        raise RuntimeError("HLSL 자체 점검 실패: " + " | ".join(problems))

    overwrite = read_overwrite_request()
    look_table = load_material_look_table()
    override_problems = find_instance_override_problems(look_table.look["Materials"])
    if override_problems:
        raise RuntimeError("모양 정본 이름 계약 위반: " + " | ".join(override_problems))
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

    print("단계: 텍스처 확인")
    textures = load_texture_table()
    surface_master = load_surface_master()

    masters = {}
    master_reports = {}
    for spec in MASTER_SPECS:
        print(f"단계: 마스터 재질 {spec['name']}")
        masters[spec["key"]], master_reports[spec["key"]] = create_master(spec, codes.get(spec["key"], ""), textures, blendable_location, overwrite)

    print("단계: 재질 인스턴스")
    instance_reports = [create_material_instance(name, masters[key], overwrite, look_table) for name, key in INSTANCE_SPECS]
    instance_reports.append(create_material_instance(SAND_INSTANCE_NAME, surface_master, overwrite, look_table))

    missing_files = find_missing_saved_files()
    if missing_files:
        raise RuntimeError("저장 파일 확인 실패: " + ", ".join(missing_files))

    report = {
        "status": "ok",
        "error_count": 0,
        "overwrite": overwrite,
        "hlsl": hlsl_reports,
        "look_sha256": look_table.source_sha256(),
        "masters": master_reports,
        "instances": instance_reports,
        "saved": True,
    }
    report_path = write_report(report)
    print(json.dumps(report, ensure_ascii=False, default=str))
    print("보고서: " + report_path)


def run_with_failure_report():
    try:
        main()
    except Exception as error:
        write_report({"status": "failed", "error_count": 1, "error": str(error)})
        raise


if __name__ == "__main__":
    run_with_failure_report()
