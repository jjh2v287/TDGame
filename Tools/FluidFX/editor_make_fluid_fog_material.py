"""실행 환경: TDGame 언리얼 에디터 안에서 FluidFX 볼류메트릭 포그 마스터 M_TD_FluidFogVolume(MD_VOLUME·BLEND_ADDITIVE, Custom 노드 1개 = Tools/FluidFX/Shaders/TDFluidFogVolume.hlsl), 창 MPC_TD_FluidFogWindow, 재질 인스턴스 15개를 만든다. 먼저 editor_make_fluid_volume_material.py 가 T_TD_FluidNoiseVolume 을 만들어 두어야 하고, 모양 정본 Content/FluidFX/PresetData/TDFluidMaterialLook.json 에 MI 15개가 병합되어 있어야 한다(MI_TD_FluidFog_TileSync 는 FX-10c 조각 병합 뒤).
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_fog_material.py (기존 에셋을 다시 지을 때는 Saved/FluidFX/fluid_fog_material_request.json 에 {"overwrite": true})
출력: /Game/FluidFX/Materials/M_TD_FluidFogVolume·MPC_TD_FluidFogWindow·MI_TD_FluidFog_*(15), Saved/FluidFX/fluid_fog_material.json(앞단 경로 substrate|legacy, 재질 통계, HLSL sha256)
상태: 미검증(2026-10-25 FX-10r 작성: py_compile·가짜 unreal·check_fluid_fog_hlsl.py 만, 에디터 실행 전). 앞단은 Substrate 안개·구름 BSDF 1순위, 연결 실패 시 기존 핀(BaseColor=알베도·SubsurfaceColor=소광·Emissive) 자동 변환. 구동(MID, SimGridResolution > 0.5)은 MID 창·바닥 = SimCenterWorld.z, 정적(MI)은 MPC 창·바닥 = ObjectPositionWS.z − ObjectBounds.z(반 크기). MI 값은 모양 정본(apply_to_instance)만 쓴다. 볼륨 도메인은 재질 통계 PS 명령이 0일 수 있어 실패로 보지 않고, dxc 명령 수(check_fluid_fog_hlsl.py)를 대체 지표로 둔다. MI 수는 명세 13개 + 대형 정적 상자용 MI_TD_FluidFog_PassiveField 1개(반박 S7, 상자 가장자리 부드러움) + 감싸기 타일용 MI_TD_FluidFog_TileSync 1개(FX-10c F6) = 15개. 2026-10-26 FX-10c: C++ 구동 스칼라 SimAreaWrapped(기본 0, MI 덮어쓰기 금지)를 Custom 입력에 더함 — 1이면 창 UV 를 자르지 않고(RT TA_Wrap) 창 가중 1, 기존 마스터는 지우지 않고 식만 제자리 재구성(L-editor-67). 에디터 실행 전(py_compile·가짜 unreal·check_fluid_fog_hlsl.py 만).
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
MASTER_NAME = "M_TD_FluidFogVolume"
MASTER_PATH = f"{MATERIAL_DIR}/{MASTER_NAME}"
COLLECTION_NAME = "MPC_TD_FluidFogWindow"
COLLECTION_PATH = f"{MATERIAL_DIR}/{COLLECTION_NAME}"
NOISE_VOLUME_PATH = f"{TEXTURE_DIR}/T_TD_FluidNoiseVolume"
DEFAULT_TEXTURE_PATHS = ("/Engine/EngineResources/Black", "/Engine/EngineResources/DefaultTexture")
SHADER_DIRECTORY = ("Tools", "FluidFX", "Shaders")
FOG_HLSL_NAME = "TDFluidFogVolume.hlsl"
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_fog_material_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_fog_material.json")
MATERIAL_LOOK_MODULE_RELATIVE_PATH = ("Tools", "FluidFX", "fluid_material_look.py")
PARAMETER_GROUP = "TDFluidFog"
MACRO_PREFIXES = ("TD_FOG_", "TDFog")

COLLECTION_VECTOR_DEFAULTS = (
    ("SimCenterWorld", (0.0, 0.0, 0.0, 0.0)),
    ("SimExtentsCm", (0.0, 0.0, 0.0, 0.0)),
)
COLLECTION_SOURCE_BY_INPUT = {"FogWindowCenterWorld": "SimCenterWorld", "FogWindowExtentsCm": "SimExtentsCm"}

DRIVEN_VECTOR_DEFAULTS = (
    ("SimCenterWorld", (0.0, 0.0, 0.0, 0.0)),
    ("SimExtentsCm", (0.0, 0.0, 0.0, 0.0)),
)
DRIVEN_SCALAR_DEFAULTS = (("SimGridResolution", 0.0), ("SimAreaWrapped", 0.0))
LOOK_VECTOR_DEFAULTS = (
    ("FogAlbedo", (0.8, 0.85, 0.95, 1.0)),
    ("FogExtinctionTint", (1.0, 1.0, 1.0, 1.0)),
    ("FogEmissive", (0.0, 0.0, 0.0, 1.0)),
    ("FogNoiseWindCmPerSec", (30.0, 0.0, 5.0, 0.0)),
)
LOOK_SCALAR_DEFAULTS = (
    ("FogExtinctionPerM", 0.2),
    ("FogExtrudeHeightCm", 90.0),
    ("FogExtrudeExponent", 1.0),
    ("FogTopFadeCm", 40.0),
    ("FogNoiseSubtract", 0.3),
    ("FogNoiseScaleCm", 400.0),
    ("FogPassiveCoverage", 0.5),
    ("FogPassiveNoiseScaleCm", 3000.0),
    ("FogWindowBlendRatio", 0.15),
    ("FogUsesWindowMask", 1.0),
    ("FogShape", 0.0),
    ("FogShapeSoftCm", 60.0),
    ("FogShellThicknessCm", 120.0),
    ("FogCameraFadeStartCm", 0.0),
    ("FogCameraFadeLengthCm", 0.0),
    ("FogCameraFadeInvert", 0.0),
)
VECTOR_PARAMETER_DEFAULTS = DRIVEN_VECTOR_DEFAULTS + LOOK_VECTOR_DEFAULTS
SCALAR_PARAMETER_DEFAULTS = DRIVEN_SCALAR_DEFAULTS + LOOK_SCALAR_DEFAULTS
TEXTURE_PARAMETER_NAMES = ("DensityPressureTexture", "NoiseVolumeTexture")
BUILTIN_SOURCE_NAMES = ("FogWorldPosition", "FogCameraPosition", "FogObjectPosition", "FogObjectBoundsCm", "ElapsedSeconds")
VECTOR_INPUT_NAMES = (
    "FogWorldPosition", "FogCameraPosition", "FogObjectPosition", "FogObjectBoundsCm",
    "SimCenterWorld", "SimExtentsCm", "FogWindowCenterWorld", "FogWindowExtentsCm",
    "FogExtinctionTint", "FogNoiseWindCmPerSec",
)
CUSTOM_INPUT_NAMES = (
    "FogWorldPosition", "FogCameraPosition", "FogObjectPosition", "FogObjectBoundsCm", "ElapsedSeconds",
    "DensityPressureTexture", "NoiseVolumeTexture",
    "SimCenterWorld", "SimExtentsCm", "SimGridResolution", "SimAreaWrapped", "FogWindowCenterWorld", "FogWindowExtentsCm",
    "FogExtinctionPerM", "FogExtinctionTint", "FogExtrudeHeightCm", "FogExtrudeExponent", "FogTopFadeCm",
    "FogNoiseSubtract", "FogNoiseScaleCm", "FogNoiseWindCmPerSec", "FogPassiveCoverage", "FogPassiveNoiseScaleCm",
    "FogWindowBlendRatio", "FogUsesWindowMask", "FogShape", "FogShapeSoftCm", "FogShellThicknessCm",
    "FogCameraFadeStartCm", "FogCameraFadeLengthCm", "FogCameraFadeInvert",
)
SAMPLER_NAMES = ("DensityPressureTextureSampler", "NoiseVolumeTextureSampler")
ENGINE_SYMBOLS = ("Texture2DSampleLevel", "Texture3DSampleLevel")
MID_DRIVEN_NAMES = (
    "SimCenterWorld", "SimExtentsCm", "SimGridResolution", "SimAreaWrapped", "DensityPressureTexture", "VelocityTexture",
    "PlaneAxisU", "PlaneAxisV", "PlaneAxisN", "PlaneCoordinateOffsetCm", "VolumeHeightCm", "VolumeBoxScale",
    "VolumeOffsetCm", "ExtrudeSymmetric", "AuxTexture", "AuxHeatActive",
)
CONDITIONAL_CPP_NAMES = ("DensityScale", "SoftFalloffCm")
INSTANCE_NAMES = (
    "MI_TD_FluidFog_Sim",
    "MI_TD_FluidFog_SimCharacter",
    "MI_TD_FluidFog_SimTrail",
    "MI_TD_FluidFog_Passive0",
    "MI_TD_FluidFog_Passive1",
    "MI_TD_FluidFog_Passive2",
    "MI_TD_FluidFog_Passive3",
    "MI_TD_FluidFog_Passive4",
    "MI_TD_FluidFog_Passive5",
    "MI_TD_FluidFog_Passive6",
    "MI_TD_FluidFog_PassiveField",
    "MI_TD_FluidFog_CylA",
    "MI_TD_FluidFog_CylB",
    "MI_TD_FluidFog_Hollow",
    "MI_TD_FluidFog_TileSync",
)
SUBSTRATE_PIN_CANDIDATES = {
    "albedo": ("Albedo",),
    "extinction": ("Extinction",),
    "emissive": ("EmissiveColor", "Emissive Color", "Emissive"),
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
    "MaterialEditingLibrary.set_material_instance_parent",
    "MaterialEditingLibrary.update_material_instance",
    "Material",
    "MaterialFactoryNew",
    "MaterialInstanceConstant",
    "MaterialInstanceConstantFactoryNew",
    "MaterialParameterCollection",
    "MaterialParameterCollectionFactoryNew",
    "CollectionVectorParameter",
    "MaterialExpressionCollectionParameter",
    "MaterialExpressionCustom",
    "CustomInput",
    "CustomMaterialOutputType.CMOT_FLOAT4",
    "MaterialExpressionWorldPosition",
    "MaterialExpressionCameraPositionWS",
    "MaterialExpressionObjectPositionWS",
    "MaterialExpressionObjectBounds",
    "MaterialExpressionTime",
    "MaterialExpressionTextureObjectParameter",
    "MaterialExpressionVectorParameter",
    "MaterialExpressionScalarParameter",
    "MaterialExpressionComponentMask",
    "MaterialExpressionMultiply",
    "MaterialExpressionSubstrateVolumetricFogCloudBSDF",
    "MaterialDomain.MD_VOLUME",
    "BlendMode.BLEND_ADDITIVE",
    "MaterialProperty.MP_FRONT_MATERIAL",
    "MaterialProperty.MP_BASE_COLOR",
    "MaterialProperty.MP_SUBSURFACE_COLOR",
    "MaterialProperty.MP_EMISSIVE_COLOR",
    "MaterialSamplerType.SAMPLERTYPE_COLOR",
    "MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR",
    "TextureCompressionSettings.TC_DEFAULT",
    "VolumeTexture",
)

IDENTIFIER_PATTERN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
DEFINE_PATTERN = re.compile(r"^[ \t]*#define[ \t]+([A-Za-z_][A-Za-z0-9_]*)", re.MULTILINE)


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


def asset_path(name):
    return f"{MATERIAL_DIR}/{name}"


def read_text_file(path):
    if not os.path.isfile(path):
        raise RuntimeError(f"HLSL 파일 없음: {path}")
    with open(path, encoding="utf-8") as text_file:
        return text_file.read().replace("\r\n", "\n").strip("\n") + "\n"


def read_fog_source(project_root):
    return read_text_file(os.path.join(project_root, *SHADER_DIRECTORY, FOG_HLSL_NAME))


def assemble_node_code(code):
    macro_names = sorted(set(DEFINE_PATTERN.findall(code)))
    undefine_block = "".join(f"#undef {name}\n" for name in macro_names)
    return undefine_block + code + "\n" + undefine_block


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


def validate_hlsl_code(code):
    problems = []
    identifiers = set(IDENTIFIER_PATTERN.findall(code))
    for label, names in (("Custom 입력", CUSTOM_INPUT_NAMES), ("샘플러", SAMPLER_NAMES), ("엔진 심볼", ENGINE_SYMBOLS)):
        missing = [name for name in names if name not in identifiers]
        if missing:
            problems.append(f"HLSL이 쓰지 않는 {label}: " + ", ".join(missing))
    leaked = [name for name in DEFINE_PATTERN.findall(code) if not name.startswith(MACRO_PREFIXES)]
    if leaked:
        problems.append("접두 밖 매크로: " + ", ".join(leaked))
    bracket_problem = find_unbalanced_brackets(code)
    if bracket_problem:
        problems.append(bracket_problem)
    if "return" not in identifiers:
        problems.append("HLSL에 return 이 없음")
    if "//" in code or "/*" in code:
        problems.append("HLSL에 주석이 있음")
    return problems


def find_unresolved_inputs():
    resolvable = set(BUILTIN_SOURCE_NAMES) | set(TEXTURE_PARAMETER_NAMES) | set(COLLECTION_SOURCE_BY_INPUT)
    resolvable |= {name for name, _ in VECTOR_PARAMETER_DEFAULTS} | {name for name, _ in SCALAR_PARAMETER_DEFAULTS}
    return sorted(set(CUSTOM_INPUT_NAMES) - resolvable)


def find_instance_override_problems(entries):
    problems = []
    overridable = {name for name, _ in LOOK_VECTOR_DEFAULTS} | {name for name, _ in LOOK_SCALAR_DEFAULTS}
    for name in INSTANCE_NAMES:
        entry = entries.get(asset_path(name))
        if entry is None:
            problems.append(f"{name}: 모양 정본에 없음")
            continue
        if entry.get("Parent") != MASTER_PATH:
            problems.append(f"{name}: 부모 {entry.get('Parent')} ≠ {MASTER_PATH}")
        names = set(entry.get("Scalars", {})) | set(entry.get("Vectors", {})) | set(entry.get("Switches", {}))
        driven = sorted(names & (set(MID_DRIVEN_NAMES) | set(CONDITIONAL_CPP_NAMES)))
        if driven:
            problems.append(f"{name}: C++ 구동·조건부 이름 덮어씀 " + ", ".join(driven))
        unknown = sorted(names - overridable)
        if unknown:
            problems.append(f"{name}: 마스터에 없는 이름 " + ", ".join(unknown))
    return problems


def load_material_look_table():
    module_path = project_path(*MATERIAL_LOOK_MODULE_RELATIVE_PATH)
    module_spec = importlib.util.spec_from_file_location("fluid_material_look", module_path)
    if module_spec is None or module_spec.loader is None:
        raise RuntimeError(f"모양 정본 모듈 없음: {module_path}")
    material_look = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(material_look)
    return material_look.TDMaterialLookTable.read_project(unreal.SystemLibrary.get_project_directory())


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


def collect_target_asset_paths():
    return [MASTER_PATH, COLLECTION_PATH] + [asset_path(name) for name in INSTANCE_NAMES]


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


def choose_sampler_type(texture):
    try:
        compression = texture.get_editor_property("compression_settings")
        is_srgb = bool(texture.get_editor_property("srgb"))
    except Exception:
        return None
    if compression != unreal.TextureCompressionSettings.TC_DEFAULT:
        return unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
    return unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if is_srgb else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR


def pick_default_texture():
    for texture_path in DEFAULT_TEXTURE_PATHS:
        texture = unreal.EditorAssetLibrary.load_asset(texture_path)
        if texture is None:
            continue
        sampler_type = choose_sampler_type(texture)
        if sampler_type is not None:
            return texture_path, texture, sampler_type
    raise RuntimeError("기본 텍스처로 쓸 엔진 텍스처가 없음: " + ", ".join(DEFAULT_TEXTURE_PATHS))


def load_noise_volume():
    texture = unreal.EditorAssetLibrary.load_asset(NOISE_VOLUME_PATH)
    if not isinstance(texture, unreal.VolumeTexture):
        raise RuntimeError(f"노이즈 볼륨 텍스처 없음(editor_make_fluid_volume_material.py 먼저): {NOISE_VOLUME_PATH}")
    sampler_type = choose_sampler_type(texture) or unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
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


def mask_rgb(material, source, x, y):
    mask = add_node(material, unreal.MaterialExpressionComponentMask, x, y, r=True, g=True, b=True, a=False)
    link(source, mask)
    return mask


def load_or_create_collection(overwrite):
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(COLLECTION_PATH):
        if not overwrite:
            raise RuntimeError(f"기존 에셋 보존: {COLLECTION_PATH}")
        collection = assets.load_asset(COLLECTION_PATH)
        if not isinstance(collection, unreal.MaterialParameterCollection):
            raise RuntimeError(f"기존 에셋 형식 불일치: {COLLECTION_PATH}")
    else:
        ensure_directory(MATERIAL_DIR)
        collection = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            COLLECTION_NAME, MATERIAL_DIR, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew()
        )
        if collection is None:
            raise RuntimeError(f"MPC 생성 실패: {COLLECTION_PATH}")
    parameters = []
    for name, rgba in COLLECTION_VECTOR_DEFAULTS:
        parameter = unreal.CollectionVectorParameter()
        parameter.set_editor_property("parameter_name", name)
        parameter.set_editor_property("default_value", unreal.LinearColor(*rgba))
        parameters.append(parameter)
    collection.set_editor_property("vector_parameters", parameters)
    collection.set_editor_property("scalar_parameters", [])
    stored = [str(item.get_editor_property("parameter_name")) for item in collection.get_editor_property("vector_parameters")]
    if stored != [name for name, _ in COLLECTION_VECTOR_DEFAULTS]:
        raise RuntimeError("MPC 벡터 이름 설정 확인 실패: " + ", ".join(stored))
    save_asset(collection, COLLECTION_PATH)
    return collection


def prepare_master(overwrite):
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
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(MASTER_NAME, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError(f"마스터 재질 생성 실패: {MASTER_PATH}")
    return material, True


def build_sources(material, textures, collection):
    sources = {}
    rows = iter(range(200))

    def place():
        return -1500, -1600 + next(rows) * 110

    x, y = place()
    sources["FogWorldPosition"] = (truncate_if_available(material, add_node(material, unreal.MaterialExpressionWorldPosition, x - 220, y), x, y), "")
    x, y = place()
    sources["FogCameraPosition"] = (truncate_if_available(material, add_node(material, unreal.MaterialExpressionCameraPositionWS, x - 220, y), x, y), "")
    x, y = place()
    sources["FogObjectPosition"] = (truncate_if_available(material, add_node(material, unreal.MaterialExpressionObjectPositionWS, x - 220, y), x, y), "")
    x, y = place()
    sources["FogObjectBoundsCm"] = (add_node(material, unreal.MaterialExpressionObjectBounds, x, y), "")
    x, y = place()
    sources["ElapsedSeconds"] = (add_node(material, unreal.MaterialExpressionTime, x, y), "")
    for name, (texture, sampler_type) in textures.items():
        x, y = place()
        node = add_node(material, unreal.MaterialExpressionTextureObjectParameter, x, y, parameter_name=name, texture=texture, sampler_type=sampler_type)
        assign_parameter_group(node)
        sources[name] = (node, "")
    for name, rgba in VECTOR_PARAMETER_DEFAULTS:
        x, y = place()
        node = add_node(material, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=unreal.LinearColor(*rgba))
        assign_parameter_group(node)
        sources[name] = (node, "RGB")
    for name, value in SCALAR_PARAMETER_DEFAULTS:
        x, y = place()
        node = add_node(material, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)
        assign_parameter_group(node)
        sources[name] = (node, "")
    for input_name, collection_name in COLLECTION_SOURCE_BY_INPUT.items():
        x, y = place()
        node = add_node(material, unreal.MaterialExpressionCollectionParameter, x - 220, y, collection=collection, parameter_name=collection_name)
        sources[input_name] = (mask_rgb(material, node, x, y), "")
    return sources


def build_custom_node(material, code, sources):
    custom = add_node(material, unreal.MaterialExpressionCustom, -500, 0)
    custom.set_editor_property("description", "TDFluidFogVolume")
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    inputs = []
    for name in CUSTOM_INPUT_NAMES:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        inputs.append(custom_input)
    custom.set_editor_property("inputs", inputs)
    custom.set_editor_property("code", code)
    stored_names = [str(item.get_editor_property("input_name")) for item in custom.get_editor_property("inputs")]
    if stored_names != list(CUSTOM_INPUT_NAMES):
        raise RuntimeError("Custom 노드 입력 이름 설정 확인 실패: " + ", ".join(stored_names))
    for name in CUSTOM_INPUT_NAMES:
        source, source_output = sources[name]
        link(source, custom, name, source_output)
    return custom


def build_front_outputs(material, custom, sources):
    extinction = add_node(material, unreal.MaterialExpressionComponentMask, -150, -120, r=True, g=True, b=True, a=False)
    density = add_node(material, unreal.MaterialExpressionComponentMask, -150, 60, r=False, g=False, b=False, a=True)
    link(custom, extinction)
    link(custom, density)
    emissive = add_node(material, unreal.MaterialExpressionMultiply, 20, 120)
    link(density, emissive, "A")
    emissive_source, emissive_output = sources["FogEmissive"]
    link(emissive_source, emissive, "B", emissive_output)
    albedo_source, albedo_output = sources["FogAlbedo"]

    bsdf = add_node(material, unreal.MaterialExpressionSubstrateVolumetricFogCloudBSDF, 250, 0)
    pins = {
        "albedo": unreal.MaterialEditingLibrary.connect_material_expressions(albedo_source, albedo_output, bsdf, SUBSTRATE_PIN_CANDIDATES["albedo"][0]) and SUBSTRATE_PIN_CANDIDATES["albedo"][0],
        "extinction": try_link_first_pin(extinction, bsdf, SUBSTRATE_PIN_CANDIDATES["extinction"]),
        "emissive": try_link_first_pin(emissive, bsdf, SUBSTRATE_PIN_CANDIDATES["emissive"]),
    }
    if all(pins.values()) and unreal.MaterialEditingLibrary.connect_material_property(bsdf, "", unreal.MaterialProperty.MP_FRONT_MATERIAL):
        return {"path": "substrate", "pins": pins}

    unreal.MaterialEditingLibrary.delete_material_expression(material, bsdf)
    if not unreal.MaterialEditingLibrary.connect_material_property(albedo_source, albedo_output, unreal.MaterialProperty.MP_BASE_COLOR):
        raise RuntimeError("기존 핀 BaseColor(알베도) 연결 실패")
    connect_material_output(extinction, unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    connect_material_output(emissive, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return {"path": "legacy", "pins": {"albedo": "MP_BASE_COLOR", "extinction": "MP_SUBSURFACE_COLOR", "emissive": "MP_EMISSIVE_COLOR"}, "substrate_pins_tried": pins}


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
        raise RuntimeError("마스터 파라미터 없음: " + ", ".join(missing))
    for name, reason in (("SimGridResolution", "정적 역할 판정 깨짐"), ("SimAreaWrapped", "감싸기 끔 항등 깨짐")):
        default_value = editing.get_material_default_scalar_parameter_value(material, name)
        if abs(default_value) > 1e-6:
            raise RuntimeError(f"마스터 {name} 기본값이 0이 아님({reason}): {default_value}")


def read_material_statistics(material):
    try:
        statistics = unreal.MaterialEditingLibrary.get_statistics(material)
        return {name: statistics.get_editor_property(name) for name in ("num_vertex_shader_instructions", "num_pixel_shader_instructions", "num_samplers")}
    except Exception:
        return {}


def create_master(code, textures, collection, overwrite):
    material, was_created = prepare_master(overwrite)
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_VOLUME)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    sources = build_sources(material, textures, collection)
    custom = build_custom_node(material, code, sources)
    front = build_front_outputs(material, custom, sources)
    errors = [str(item) for item in unreal.MaterialEditingLibrary.recompile_material(material) or []]
    if errors:
        raise RuntimeError("M_TD_FluidFogVolume 컴파일 실패(Saved/Logs/TDGame.log 의 LogMaterial 확인): " + " | ".join(errors))
    verify_master_parameters(material)
    save_asset(material, MASTER_PATH)
    statistics = read_material_statistics(material)
    return material, {
        "asset": MASTER_PATH,
        "created": was_created,
        "front": front,
        "statistics": statistics,
        "statistics_note": "볼륨 도메인은 베이스 패스가 없어 PS 명령 0일 수 있음 → check_fluid_fog_hlsl.py dxc 명령 수가 대체 지표",
    }


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


def write_report(report):
    report_path = project_path(*REPORT_RELATIVE_PATH)
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, "w", encoding="utf-8") as report_file:
        json.dump(report, report_file, ensure_ascii=False, indent=2, default=str)
    return report_path


def main():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    assets = unreal.EditorAssetLibrary
    missing_names = find_missing_unreal_names()
    if missing_names:
        raise RuntimeError("언리얼 Python 이름 없음: " + ", ".join(missing_names))
    if editor.get_game_world() is not None:
        raise RuntimeError("PIE를 종료한 뒤 실행하세요.")

    look_table = load_material_look_table()
    look_problems = find_instance_override_problems(look_table.look["Materials"])
    code = assemble_node_code(read_fog_source(unreal.SystemLibrary.get_project_directory()))
    problems = validate_hlsl_code(code) + [f"입력 원천 없음: {name}" for name in find_unresolved_inputs()] + look_problems
    if problems:
        raise RuntimeError("자체 점검 실패: " + " | ".join(problems))

    overwrite = read_overwrite_request()
    existing_paths = [path for path in collect_target_asset_paths() if assets.does_asset_exist(path)]
    if existing_paths and not overwrite:
        print(json.dumps({"status": "기존 에셋이 있어 아무것도 바꾸지 않았습니다", "existing_assets": existing_paths, "overwrite_request_file": project_path(*REQUEST_RELATIVE_PATH), "overwrite_request_content": {"overwrite": True}}, ensure_ascii=False))
        raise RuntimeError("기존 에셋 보존: overwrite 요청 파일이 없어 중단합니다.")
    unsaved_names = find_unsaved_package_names(overwrite)
    if unsaved_names:
        raise RuntimeError("미저장 작업을 보존합니다: " + ", ".join(unsaved_names))

    print("단계: 텍스처·MPC")
    texture_path, default_texture, default_sampler = pick_default_texture()
    noise_texture, noise_sampler = load_noise_volume()
    textures = {"DensityPressureTexture": (default_texture, default_sampler), "NoiseVolumeTexture": (noise_texture, noise_sampler)}
    collection = load_or_create_collection(overwrite)

    print("단계: 마스터 재질")
    master, master_summary = create_master(code, textures, collection, overwrite)

    print("단계: 재질 인스턴스")
    instances = []
    for name in INSTANCE_NAMES:
        instance = prepare_instance(name, overwrite)
        entry = look_table.apply_to_instance(unreal.MaterialEditingLibrary, unreal.LinearColor, instance, asset_path(name), master)
        save_asset(instance, asset_path(name))
        instances.append({"asset": asset_path(name), "scalars": entry["Scalars"], "vectors": entry["Vectors"]})

    report = {
        "status": "ok",
        "overwrite": overwrite,
        "default_texture": texture_path,
        "noise_texture": NOISE_VOLUME_PATH,
        "collection": COLLECTION_PATH,
        "master": master_summary,
        "instances": instances,
        "hlsl": {"path": "/".join(SHADER_DIRECTORY + (FOG_HLSL_NAME,)), "sha256": hashlib.sha256(code.encode("utf-8")).hexdigest()},
        "material_look_sha256": look_table.source_sha256(),
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
