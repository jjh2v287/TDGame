"""실행 환경: TDGame 언리얼 에디터 안에서 FX-13r 식생 에셋을 만든다: 흐름 RT RT_TD_FluidFoliageFlow(256², RTF_RGBA16F, Clamp, 지우기 (0.5,0.5,0,1)), 창 MPC_TD_FluidFoliageWindow(SimCenterWorld·SimExtentsCm·FoliageFlowState, 스칼라 FoliageWindOriginSeconds), 복사 재질 M_TD_FluidFoliageFlowCopy(Unlit·Emissive, Custom = Shaders/TDFluidFoliageFlowCopy.hlsl)·MI, 휨 함수 MF_TD_FluidFoliageBend(정점 Custom = Shaders/TDFluidFoliageBend.hlsl, 출력 WorldPositionOffset·BendAmount), 풀 재질 M_TD_FluidFoliageGrass(Masked·양면·MSM_TWO_SIDED_FOLIAGE)·MI 4. 파동 3 FX-13c: 복사 재질에 AuxTexture·AuxFoliageBendActive(휨 벡터 원천 교체)를 더하고 채널 Press·Reed(RT·MPC·MF·풀 마스터·MI·복사 MI)를 Walk 와 같은 그래프로 더 만든다. 선택한 대상의 MI 가 모양 정본에 병합되어 있어야 한다.
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_foliage_material.py (PowerShell, L-repo-01. 기존 에셋을 다시 지을 때는 Saved/FluidFX/fluid_foliage_material_request.json 에 {"overwrite": true}, 만들 대상은 "targets": ["copy","walk","press","reed"] 의 부분집합, 없으면 전부)
출력: /Game/FluidFX/Textures/RT_TD_FluidFoliageFlow, /Game/FluidFX/Materials/{MPC_TD_FluidFoliageWindow, M_TD_FluidFoliageFlowCopy, MI_TD_FluidFoliageFlowCopy, MF_TD_FluidFoliageBend, M_TD_FluidFoliageGrass, MI_TD_FluidFoliage_*(4)}, Press·Reed 채널 {RT_TD_FluidFoliagePressFlow·RT_TD_FluidReedFlow, MPC_TD_FluidFoliagePressWindow·MPC_TD_FluidReedWindow, MF_TD_FluidFoliagePressBend·MF_TD_FluidReedBend, M_TD_FluidFoliageGrassPress·M_TD_FluidFoliageReed, MI_TD_FluidFoliage_GrassPress·MI_TD_FluidFoliage_Reed, MI_TD_FluidFoliageFlowCopy_Press·MI_TD_FluidFoliageFlowCopy_Reed}, Saved/FluidFX/fluid_foliage_material.json(풀 텍스처 출처, 재질 통계, 채널별 collection.scalars·bend_function.elapsed_seconds, HLSL sha256, 모양 정본 sha256)
상태: 미검증(2026-10-26 FX-13r R13 작성, 2026-10-29 파동 3 FX-13c 채널·Aux 원천 추가: py_compile·가짜 unreal 전체 실행·check_fluid_foliage_hlsl.py 만, 에디터 실행 전). 복사 재질의 VelocityTexture·AuxTexture·AuxFoliageBendActive·PlaneAxisU/V 는 라이브 외부 대상 MID 가, VelocitySpeedReferenceCmPerSec 는 흐름 액터가 넣는다(마스터 600, 모양 파일 금지 R7). MF 는 RT 에셋을 텍스처 객체 노드로 직접 참조하고 휨 매개변수는 MF 안에 둔다(풀 MI 가 바로 덮어씀). ISM·Nanite 사용 플래그는 노드 생성 전 속성으로 켜고 확인은 컴파일 뒤(L-editor-61). MI 값은 모양 정본(apply_to_instance)만 쓴다. 2026-10-31 시간원 2차(K6 k2_press_decay 대조 실패의 바람 몫): 채널마다 휨 MF 의 Custom 입력 ElapsedSeconds = Time − 그 채널 MPC 스칼라 FoliageWindOriginSeconds(기본 0 이라 게임 모양 불변, 같은 MPC 라 재질당 MPC 수 그대로, MI·모양 정본 덮어쓰기 금지 — 룩데브 구동 MPC 이름 자체 점검), pie_lookdev_fluid.py 가 기반 재질이 채널 풀 마스터인 사용자가 있는 무대 단계 apply 마다 그 채널 원점 = 그 게임 시각(foliage_wind_origin). 에디터 재생성 전(py_compile·calib_offline_test F13 가짜 unreal·check_fluid_foliage_hlsl.py 만), 재생성 전에는 MPC 에 이 스칼라가 없어 식생 무대 룩데브 단계가 키 오류다.
"""
import collections
import hashlib
import importlib.util
import json
import os
import re

import unreal


FLUID_ROOT = "/Game/FluidFX"
MATERIAL_DIR = f"{FLUID_ROOT}/Materials"
TEXTURE_DIR = f"{FLUID_ROOT}/Textures"
RENDER_TARGET_NAME = "RT_TD_FluidFoliageFlow"
RENDER_TARGET_SIZE = 256
RENDER_TARGET_CLEAR_COLOR = (0.5, 0.5, 0.0, 1.0)
COLLECTION_NAME = "MPC_TD_FluidFoliageWindow"
COPY_MASTER_NAME = "M_TD_FluidFoliageFlowCopy"
COPY_MASTER_PATH = f"{MATERIAL_DIR}/{COPY_MASTER_NAME}"
COPY_INSTANCE_NAME = "MI_TD_FluidFoliageFlowCopy"
FUNCTION_NAME = "MF_TD_FluidFoliageBend"
GRASS_MASTER_NAME = "M_TD_FluidFoliageGrass"
GRASS_INSTANCE_NAMES = (
    "MI_TD_FluidFoliage_GrassWind",
    "MI_TD_FluidFoliage_GrassSim",
    "MI_TD_FluidFoliage_GrassUnderwater",
    "MI_TD_FluidFoliage_PcgStandIn",
)
FoliageChannel = collections.namedtuple(
    "FoliageChannel",
    ("key", "render_target_name", "collection_name", "function_name", "grass_master_name", "grass_instance_names", "copy_instance_name"),
)
FOLIAGE_CHANNELS = (
    FoliageChannel("walk", RENDER_TARGET_NAME, COLLECTION_NAME, FUNCTION_NAME, GRASS_MASTER_NAME, GRASS_INSTANCE_NAMES, COPY_INSTANCE_NAME),
    FoliageChannel(
        "press", "RT_TD_FluidFoliagePressFlow", "MPC_TD_FluidFoliagePressWindow", "MF_TD_FluidFoliagePressBend",
        "M_TD_FluidFoliageGrassPress", ("MI_TD_FluidFoliage_GrassPress",), "MI_TD_FluidFoliageFlowCopy_Press",
    ),
    FoliageChannel(
        "reed", "RT_TD_FluidReedFlow", "MPC_TD_FluidReedWindow", "MF_TD_FluidReedBend",
        "M_TD_FluidFoliageReed", ("MI_TD_FluidFoliage_Reed",), "MI_TD_FluidFoliageFlowCopy_Reed",
    ),
)
COPY_TARGET_KEY = "copy"
REQUEST_TARGET_KEYS = (COPY_TARGET_KEY,) + tuple(channel.key for channel in FOLIAGE_CHANNELS)
GRASS_MESH_PATH = "/Game/DarkFantasyTopDown/StaticMeshes/Nature/SM_Grass"
FALLBACK_BASE_COLOR_TEXTURE_PATH = "/Game/DarkFantasyTopDown/Textures/Nature/Grass/T_Grass2_basecolor"
FALLBACK_OPACITY_TEXTURE_PATH = "/Game/DarkFantasyTopDown/Textures/Nature/Grass/T_Grass2_Alpha"
BASE_COLOR_NAME_HINTS = ("basecolor", "base_color", "albedo", "diffuse", "color")
OPACITY_NAME_HINTS = ("alpha", "opacity", "mask")
DEFAULT_TEXTURE_PATHS = ("/Engine/EngineResources/Black", "/Engine/EngineResources/DefaultTexture")
SHADER_DIRECTORY = ("Tools", "FluidFX", "Shaders")
COPY_HLSL_NAME = "TDFluidFoliageFlowCopy.hlsl"
BEND_HLSL_NAME = "TDFluidFoliageBend.hlsl"
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_foliage_material_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_foliage_material.json")
MATERIAL_LOOK_MODULE_RELATIVE_PATH = ("Tools", "FluidFX", "fluid_material_look.py")
PARAMETER_GROUP = "TDFluidFoliage"
MACRO_PREFIXES = ("TD_FOLIAGE_", "TDFoliage")

COLLECTION_VECTOR_DEFAULTS = (
    ("SimCenterWorld", (0.0, 0.0, 0.0, 0.0)),
    ("SimExtentsCm", (0.0, 0.0, 0.0, 0.0)),
    ("FoliageFlowState", (0.0, 0.0, 0.0, 0.0)),
)
WIND_ORIGIN_PARAMETER_NAME = "FoliageWindOriginSeconds"
COLLECTION_SCALAR_DEFAULTS = ((WIND_ORIGIN_PARAMETER_NAME, 0.0),)
COLLECTION_RUNTIME_NAMES = frozenset(name for name, _ in COLLECTION_SCALAR_DEFAULTS)
BEND_COLLECTION_SOURCE_BY_INPUT = {
    "FoliageWindowCenter": "SimCenterWorld",
    "FoliageWindowExtents": "SimExtentsCm",
    "FoliageFlowState": "FoliageFlowState",
}

COPY_DRIVEN_VECTOR_DEFAULTS = (
    ("PlaneAxisU", (1.0, 0.0, 0.0, 0.0)),
    ("PlaneAxisV", (0.0, 1.0, 0.0, 0.0)),
)
COPY_DRIVEN_SCALAR_DEFAULTS = (("VelocitySpeedReferenceCmPerSec", 600.0), ("AuxFoliageBendActive", 0.0))
COPY_LOOK_SCALAR_DEFAULTS = (
    ("FoliageBendMaxDegrees", 0.0),
    ("FoliageBendSpeedReferenceCmPerSec", 300.0),
    ("FoliageBendResponse", 1.0),
)
COPY_LOOK_VECTOR_DEFAULTS = ()
COPY_TEXTURE_PARAMETER_NAMES = ("VelocityTexture", "AuxTexture")
COPY_INPUT_NAMES = (
    "FlowUv", "VelocityTexture", "AuxTexture", "PlaneAxisU", "PlaneAxisV", "VelocitySpeedReferenceCmPerSec", "AuxFoliageBendActive",
    "FoliageBendMaxDegrees", "FoliageBendSpeedReferenceCmPerSec", "FoliageBendResponse",
)
COPY_VECTOR_INPUT_NAMES = ("PlaneAxisU", "PlaneAxisV")
COPY_FLOAT2_INPUT_NAMES = ("FlowUv",)
COPY_SAMPLER_NAMES = ("VelocityTextureSampler", "AuxTextureSampler")

BEND_LOOK_SCALAR_DEFAULTS = (
    ("FoliageBendScale", 1.0),
    ("FoliageBendHeightExponent", 2.0),
    ("FoliageBladeHeightCm", 0.0),
    ("FoliageWindowEdgeFadeRatio", 0.1),
    ("FoliageWindAmplitude", 0.0),
    ("FoliageWindMaxDegrees", 25.0),
    ("FoliageWindSpeed", 1.0),
    ("FoliageWindGustScaleCm", 800.0),
    ("FoliageWindGustStrength", 0.5),
    ("FoliageSimReplacesWind", 0.0),
)
BEND_LOOK_VECTOR_DEFAULTS = (("FoliageWindDirection", (1.0, 0.0, 0.0, 0.0)),)
BEND_TEXTURE_OBJECT_NAMES = ("FoliageFlowTexture",)
BEND_BUILTIN_SOURCE_NAMES = ("BendWorldPosition", "BendRootWorld", "BendLocalPosition", "BendLocalBoundsMin", "BendLocalBoundsMax", "ElapsedSeconds")
BEND_INPUT_NAMES = (
    "BendWorldPosition", "BendRootWorld", "BendLocalPosition", "BendLocalBoundsMin", "BendLocalBoundsMax", "ElapsedSeconds",
    "FoliageFlowTexture", "FoliageWindowCenter", "FoliageWindowExtents", "FoliageFlowState",
    "FoliageBendScale", "FoliageBendHeightExponent", "FoliageBladeHeightCm", "FoliageWindowEdgeFadeRatio",
    "FoliageWindAmplitude", "FoliageWindMaxDegrees", "FoliageWindSpeed", "FoliageWindDirection",
    "FoliageWindGustScaleCm", "FoliageWindGustStrength", "FoliageSimReplacesWind",
)
BEND_VECTOR_INPUT_NAMES = ("BendWorldPosition", "BendRootWorld", "BendLocalPosition", "BendLocalBoundsMin", "BendLocalBoundsMax", "FoliageWindDirection")
BEND_FLOAT4_INPUT_NAMES = ("FoliageWindowCenter", "FoliageWindowExtents", "FoliageFlowState")
BEND_SAMPLER_NAMES = ("FoliageFlowTextureSampler",)
BEND_ADDITIONAL_OUTPUT_NAMES = ("BendAmount",)
FUNCTION_OUTPUT_NAMES = ("WorldPositionOffset", "BendAmount")

GRASS_LOOK_SCALAR_DEFAULTS = (
    ("FoliagePressedBrightness", 0.0),
    ("FoliageRoughness", 0.8),
)
GRASS_LOOK_VECTOR_DEFAULTS = (
    ("FoliageBaseColorTint", (1.0, 1.0, 1.0, 0.0)),
    ("FoliageSubsurfaceColor", (0.35, 0.5, 0.15, 0.0)),
)
GRASS_TEXTURE_PARAMETER_NAMES = ("FoliageBaseColorTexture", "FoliageOpacityTexture")

ENGINE_SYMBOLS = ("Texture2DSampleLevel",)
MID_DRIVEN_NAMES = (
    "SimCenterWorld", "SimExtentsCm", "SimGridResolution", "DensityPressureTexture", "VelocityTexture",
    "PlaneAxisU", "PlaneAxisV", "PlaneAxisN", "PlaneCoordinateOffsetCm", "ExtrudeSymmetric", "AuxTexture", "AuxHeatActive", "AuxFoliageBendActive",
    "VelocitySpeedReferenceCmPerSec", "FoliageFlowState",
)
COPY_OVERRIDABLE_NAMES = {name for name, _ in COPY_LOOK_SCALAR_DEFAULTS} | {name for name, _ in COPY_LOOK_VECTOR_DEFAULTS}
GRASS_OVERRIDABLE_NAMES = {name for name, _ in BEND_LOOK_SCALAR_DEFAULTS + GRASS_LOOK_SCALAR_DEFAULTS} | {name for name, _ in BEND_LOOK_VECTOR_DEFAULTS + GRASS_LOOK_VECTOR_DEFAULTS}
REQUIRED_UNREAL_NAMES = (
    "EditorAssetLibrary.load_asset",
    "EditorAssetLibrary.save_loaded_asset",
    "EditorAssetLibrary.does_asset_exist",
    "EditorLoadingAndSavingUtils.get_dirty_map_packages",
    "EditorLoadingAndSavingUtils.get_dirty_content_packages",
    "SystemLibrary.get_project_directory",
    "MaterialEditingLibrary.create_material_expression",
    "MaterialEditingLibrary.create_material_expression_in_function",
    "MaterialEditingLibrary.connect_material_expressions",
    "MaterialEditingLibrary.connect_material_property",
    "MaterialEditingLibrary.delete_all_material_expressions",
    "MaterialEditingLibrary.delete_all_material_expressions_in_function",
    "MaterialEditingLibrary.recompile_material",
    "MaterialEditingLibrary.update_material_function",
    "MaterialEditingLibrary.has_material_usage",
    "MaterialEditingLibrary.get_vector_parameter_names",
    "MaterialEditingLibrary.get_scalar_parameter_names",
    "MaterialEditingLibrary.get_texture_parameter_names",
    "MaterialEditingLibrary.get_material_default_scalar_parameter_value",
    "Material",
    "MaterialFunction",
    "MaterialFactoryNew",
    "MaterialFunctionFactoryNew",
    "MaterialInstanceConstant",
    "MaterialInstanceConstantFactoryNew",
    "MaterialParameterCollection",
    "MaterialParameterCollectionFactoryNew",
    "CollectionVectorParameter",
    "CollectionScalarParameter",
    "TextureRenderTarget2D",
    "TextureRenderTargetFactoryNew",
    "TextureRenderTargetFormat.RTF_RGBA16F",
    "TextureAddress.TA_CLAMP",
    "MaterialExpressionCollectionParameter",
    "MaterialExpressionCustom",
    "CustomInput",
    "CustomOutput",
    "CustomMaterialOutputType.CMOT_FLOAT1",
    "CustomMaterialOutputType.CMOT_FLOAT3",
    "CustomMaterialOutputType.CMOT_FLOAT4",
    "MaterialExpressionWorldPosition",
    "MaterialExpressionPreSkinnedPosition",
    "MaterialExpressionPreSkinnedLocalBounds",
    "MaterialExpressionTransformPosition",
    "MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL",
    "MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD",
    "MaterialExpressionAppendVector",
    "MaterialExpressionVertexInterpolator",
    "MaterialExpressionFunctionOutput",
    "MaterialExpressionMaterialFunctionCall",
    "MaterialExpressionTime",
    "MaterialExpressionTextureCoordinate",
    "MaterialExpressionTextureObject",
    "MaterialExpressionTextureObjectParameter",
    "MaterialExpressionTextureSampleParameter2D",
    "MaterialExpressionVectorParameter",
    "MaterialExpressionScalarParameter",
    "MaterialExpressionComponentMask",
    "MaterialExpressionMultiply",
    "MaterialExpressionAdd",
    "MaterialExpressionSubtract",
    "MaterialShadingModel.MSM_UNLIT",
    "MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE",
    "BlendMode.BLEND_OPAQUE",
    "BlendMode.BLEND_MASKED",
    "MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES",
    "MaterialProperty.MP_EMISSIVE_COLOR",
    "MaterialProperty.MP_BASE_COLOR",
    "MaterialProperty.MP_OPACITY_MASK",
    "MaterialProperty.MP_SUBSURFACE_COLOR",
    "MaterialProperty.MP_ROUGHNESS",
    "MaterialProperty.MP_WORLD_POSITION_OFFSET",
    "MaterialSamplerType.SAMPLERTYPE_COLOR",
    "MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR",
    "TextureCompressionSettings.TC_DEFAULT",
)

IDENTIFIER_PATTERN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
DEFINE_PATTERN = re.compile(r"^[ \t]*#define[ \t]+([A-Za-z_][A-Za-z0-9_]*)", re.MULTILINE)
FORBIDDEN_SHADER_TOKENS = ("normalize(", "Texture2DSample(")


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


def asset_path(name):
    return f"{MATERIAL_DIR}/{name}"


def texture_asset_path(name):
    return f"{TEXTURE_DIR}/{name}"


def select_channels(targets):
    return [channel for channel in FOLIAGE_CHANNELS if channel.key in targets]


def make_instance_parents(channels):
    parents = {}
    for channel in channels:
        parents[channel.copy_instance_name] = COPY_MASTER_PATH
        for name in channel.grass_instance_names:
            parents[name] = asset_path(channel.grass_master_name)
    return parents


def read_text_file(path):
    if not os.path.isfile(path):
        raise RuntimeError(f"HLSL 파일 없음: {path}")
    with open(path, encoding="utf-8") as text_file:
        return text_file.read().replace("\r\n", "\n").strip("\n") + "\n"


def read_shader_source(project_root, file_name):
    return read_text_file(os.path.join(project_root, *SHADER_DIRECTORY, file_name))


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


def validate_hlsl_code(code, input_names, sampler_names, output_names=()):
    problems = []
    identifiers = set(IDENTIFIER_PATTERN.findall(code))
    for label, names in (("Custom 입력", input_names), ("샘플러", sampler_names), ("엔진 심볼", ENGINE_SYMBOLS), ("추가 출력", output_names)):
        missing = [name for name in names if name not in identifiers]
        if missing:
            problems.append(f"HLSL이 쓰지 않는 {label}: " + ", ".join(missing))
    leaked = [name for name in DEFINE_PATTERN.findall(code) if not name.startswith(MACRO_PREFIXES)]
    if leaked:
        problems.append("접두 밖 매크로: " + ", ".join(leaked))
    forbidden = [token for token in FORBIDDEN_SHADER_TOKENS if token in code]
    if forbidden:
        problems.append("금지 토큰(안전 정규화·정점 표본 규칙): " + ", ".join(forbidden))
    bracket_problem = find_unbalanced_brackets(code)
    if bracket_problem:
        problems.append(bracket_problem)
    if "return" not in identifiers:
        problems.append("HLSL에 return 이 없음")
    if "//" in code or "/*" in code:
        problems.append("HLSL에 주석이 있음")
    return problems


def find_unresolved_inputs():
    copy_resolvable = {"FlowUv"} | set(COPY_TEXTURE_PARAMETER_NAMES)
    copy_resolvable |= {name for name, _ in COPY_DRIVEN_VECTOR_DEFAULTS + COPY_LOOK_VECTOR_DEFAULTS}
    copy_resolvable |= {name for name, _ in COPY_DRIVEN_SCALAR_DEFAULTS + COPY_LOOK_SCALAR_DEFAULTS}
    bend_resolvable = set(BEND_BUILTIN_SOURCE_NAMES) | set(BEND_TEXTURE_OBJECT_NAMES) | set(BEND_COLLECTION_SOURCE_BY_INPUT)
    bend_resolvable |= {name for name, _ in BEND_LOOK_SCALAR_DEFAULTS} | {name for name, _ in BEND_LOOK_VECTOR_DEFAULTS}
    unresolved = [f"복사:{name}" for name in COPY_INPUT_NAMES if name not in copy_resolvable]
    unresolved += [f"휨:{name}" for name in BEND_INPUT_NAMES if name not in bend_resolvable]
    return unresolved


def find_instance_override_problems(entries, channels=FOLIAGE_CHANNELS):
    problems = []
    for name, parent_path in make_instance_parents(channels).items():
        entry = entries.get(asset_path(name))
        if entry is None:
            problems.append(f"{name}: 모양 정본에 없음")
            continue
        if entry.get("Parent") != parent_path:
            problems.append(f"{name}: 부모 {entry.get('Parent')} ≠ {parent_path}")
        names = set(entry.get("Scalars", {})) | set(entry.get("Vectors", {})) | set(entry.get("Switches", {}))
        driven = sorted(names & set(MID_DRIVEN_NAMES))
        if driven:
            problems.append(f"{name}: C++ 구동 이름 덮어씀(R7) " + ", ".join(driven))
        collection_driven = sorted(names & COLLECTION_RUNTIME_NAMES)
        if collection_driven:
            problems.append(f"{name}: 룩데브 구동 MPC 이름 덮어씀 " + ", ".join(collection_driven))
        unknown = sorted(names - (COPY_OVERRIDABLE_NAMES if parent_path == COPY_MASTER_PATH else GRASS_OVERRIDABLE_NAMES) - COLLECTION_RUNTIME_NAMES)
        if unknown:
            problems.append(f"{name}: 마스터에 없는 이름 " + ", ".join(unknown))
        vector_names = {item for item, _ in COPY_LOOK_VECTOR_DEFAULTS + BEND_LOOK_VECTOR_DEFAULTS + GRASS_LOOK_VECTOR_DEFAULTS}
        misplaced = sorted((set(entry.get("Scalars", {})) & vector_names) | (set(entry.get("Vectors", {})) - vector_names))
        if misplaced:
            problems.append(f"{name}: 스칼라·벡터 구획 어긋남(M3) " + ", ".join(misplaced))
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


def resolve_request_targets(requested):
    if requested is None:
        return REQUEST_TARGET_KEYS
    if not isinstance(requested, list) or not requested:
        raise RuntimeError("요청 targets 는 비어 있지 않은 목록이어야 함: " + repr(requested))
    unknown = [str(item) for item in requested if item not in REQUEST_TARGET_KEYS]
    if unknown:
        raise RuntimeError("알 수 없는 targets: " + ", ".join(unknown) + " (허용: " + ", ".join(REQUEST_TARGET_KEYS) + ")")
    return tuple(key for key in REQUEST_TARGET_KEYS if key in requested)


def read_request():
    request_path = project_path(*REQUEST_RELATIVE_PATH)
    if not os.path.isfile(request_path):
        return False, REQUEST_TARGET_KEYS
    with open(request_path, encoding="utf-8") as request_file:
        request = json.load(request_file)
    if not isinstance(request, dict):
        return False, REQUEST_TARGET_KEYS
    return request.get("overwrite") is True, resolve_request_targets(request.get("targets"))


def collect_target_asset_paths(targets=REQUEST_TARGET_KEYS):
    paths = [COPY_MASTER_PATH] if COPY_TARGET_KEY in targets else []
    for channel in select_channels(targets):
        paths.append(texture_asset_path(channel.render_target_name))
        names = [channel.collection_name, channel.function_name, channel.grass_master_name, channel.copy_instance_name] + list(channel.grass_instance_names)
        paths += [asset_path(name) for name in names]
    return paths


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


def load_or_create(path, asset_class, factory, overwrite):
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(path):
        if not overwrite:
            raise RuntimeError(f"기존 에셋 보존: {path}")
        asset = assets.load_asset(path)
        if not isinstance(asset, asset_class):
            raise RuntimeError(f"기존 에셋 형식 불일치: {path}")
        return asset, False
    directory, name = path.rsplit("/", 1)
    ensure_directory(directory)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, directory, asset_class, factory)
    if asset is None:
        raise RuntimeError(f"에셋 생성 실패: {path}")
    return asset, True


def choose_sampler_type(texture):
    try:
        compression = texture.get_editor_property("compression_settings")
        is_srgb = bool(texture.get_editor_property("srgb"))
    except Exception:
        return None
    if compression != unreal.TextureCompressionSettings.TC_DEFAULT:
        return unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
    return unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if is_srgb else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR


def load_texture_with_sampler(path):
    texture = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if texture is None:
        return None
    sampler_type = choose_sampler_type(texture)
    return (texture, sampler_type) if sampler_type is not None else None


def pick_default_texture():
    for texture_path in DEFAULT_TEXTURE_PATHS:
        loaded = load_texture_with_sampler(texture_path)
        if loaded is not None:
            return texture_path, loaded
    raise RuntimeError("기본 텍스처로 쓸 엔진 텍스처가 없음: " + ", ".join(DEFAULT_TEXTURE_PATHS))


def read_grass_mesh_textures():
    mesh = unreal.EditorAssetLibrary.load_asset(GRASS_MESH_PATH) if unreal.EditorAssetLibrary.does_asset_exist(GRASS_MESH_PATH) else None
    if mesh is None:
        return {}, None
    material = mesh.get_material(0)
    found = {}
    if material is not None:
        for name in unreal.MaterialEditingLibrary.get_texture_parameter_names(material):
            value = None
            try:
                value = material.get_texture_parameter_value(name) if isinstance(material, unreal.MaterialInstance) else unreal.MaterialEditingLibrary.get_material_default_texture_parameter_value(material, name)
            except Exception:
                value = None
            if value is not None:
                found[str(name)] = value
    nanite_enabled = None
    try:
        nanite_enabled = bool(mesh.get_editor_property("nanite_settings").get_editor_property("enabled"))
    except Exception:
        nanite_enabled = None
    return found, nanite_enabled


def pick_mesh_texture(found, hints, excluded):
    for hint in hints:
        for name in sorted(found):
            if hint in name.lower() and name not in excluded:
                return name
    return ""


def resolve_grass_textures():
    found, nanite_enabled = read_grass_mesh_textures()
    base_name = pick_mesh_texture(found, BASE_COLOR_NAME_HINTS, ())
    opacity_name = pick_mesh_texture(found, OPACITY_NAME_HINTS, (base_name,))
    sources = {}
    textures = {}
    for parameter_name, mesh_name, fallback_path in (
        ("FoliageBaseColorTexture", base_name, FALLBACK_BASE_COLOR_TEXTURE_PATH),
        ("FoliageOpacityTexture", opacity_name, FALLBACK_OPACITY_TEXTURE_PATH),
    ):
        texture = found.get(mesh_name) if mesh_name else None
        sampler_type = choose_sampler_type(texture) if texture is not None else None
        if texture is not None and sampler_type is not None:
            textures[parameter_name] = (texture, sampler_type)
            sources[parameter_name] = {"from": "SM_Grass material 0", "parameter": mesh_name, "texture": texture.get_path_name()}
            continue
        loaded = load_texture_with_sampler(fallback_path)
        if loaded is None:
            raise RuntimeError(f"풀 텍스처 없음: {fallback_path}")
        textures[parameter_name] = loaded
        sources[parameter_name] = {"from": "fallback", "texture": fallback_path}
    return textures, sources, {"mesh": GRASS_MESH_PATH, "mesh_texture_parameters": sorted(found), "nanite_enabled": nanite_enabled}


def load_or_create_render_target(channel, overwrite):
    render_target_path = texture_asset_path(channel.render_target_name)
    render_target, was_created = load_or_create(render_target_path, unreal.TextureRenderTarget2D, unreal.TextureRenderTargetFactoryNew(), overwrite)
    for name, value in (
        ("render_target_format", unreal.TextureRenderTargetFormat.RTF_RGBA16F),
        ("size_x", RENDER_TARGET_SIZE),
        ("size_y", RENDER_TARGET_SIZE),
        ("clear_color", unreal.LinearColor(*RENDER_TARGET_CLEAR_COLOR)),
        ("address_x", unreal.TextureAddress.TA_CLAMP),
        ("address_y", unreal.TextureAddress.TA_CLAMP),
        ("auto_generate_mips", False),
    ):
        render_target.set_editor_property(name, value)
    if int(render_target.get_editor_property("size_x")) != RENDER_TARGET_SIZE or render_target.get_editor_property("render_target_format") != unreal.TextureRenderTargetFormat.RTF_RGBA16F:
        raise RuntimeError(f"흐름 RT 설정 확인 실패: {render_target_path}")
    save_asset(render_target, render_target_path)
    return render_target, was_created


def load_or_create_collection(channel, overwrite):
    collection_path = asset_path(channel.collection_name)
    collection, was_created = load_or_create(collection_path, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew(), overwrite)
    parameters = []
    for name, rgba in COLLECTION_VECTOR_DEFAULTS:
        parameter = unreal.CollectionVectorParameter()
        parameter.set_editor_property("parameter_name", name)
        parameter.set_editor_property("default_value", unreal.LinearColor(*rgba))
        parameters.append(parameter)
    scalar_parameters = []
    for name, value in COLLECTION_SCALAR_DEFAULTS:
        parameter = unreal.CollectionScalarParameter()
        parameter.set_editor_property("parameter_name", name)
        parameter.set_editor_property("default_value", value)
        scalar_parameters.append(parameter)
    collection.set_editor_property("vector_parameters", parameters)
    collection.set_editor_property("scalar_parameters", scalar_parameters)
    stored = [str(item.get_editor_property("parameter_name")) for item in collection.get_editor_property("vector_parameters")]
    if stored != [name for name, _ in COLLECTION_VECTOR_DEFAULTS]:
        raise RuntimeError("MPC 벡터 이름 설정 확인 실패: " + ", ".join(stored))
    stored_scalars = [(str(item.get_editor_property("parameter_name")), float(item.get_editor_property("default_value"))) for item in collection.get_editor_property("scalar_parameters")]
    if stored_scalars != list(COLLECTION_SCALAR_DEFAULTS):
        raise RuntimeError("MPC 스칼라 이름·기본값 설정 확인 실패: " + ", ".join(f"{name}={value}" for name, value in stored_scalars))
    save_asset(collection, collection_path)
    return collection, was_created


class NodeFactory:
    def __init__(self, create_expression):
        self.create_expression = create_expression
        self.row = 0

    def node(self, expression_class, x, y, **properties):
        expression = self.create_expression(expression_class, x, y)
        if expression is None:
            raise RuntimeError(f"재질 노드 생성 실패: {expression_class.__name__}")
        for name, value in properties.items():
            expression.set_editor_property(name, value)
        return expression

    def place(self):
        self.row += 1
        return -1600, -1800 + self.row * 110

    def parameter(self, expression_class, **properties):
        x, y = self.place()
        expression = self.node(expression_class, x, y, **properties)
        try:
            expression.set_editor_property("group", PARAMETER_GROUP)
        except Exception:
            pass
        return expression


def link(source, target, target_input="", source_output=""):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(source, source_output, target, target_input):
        raise RuntimeError(f"재질 노드 연결 실패: {source.get_name()}[{source_output}] -> {target.get_name()}.{target_input}")


def connect_material_output(expression, material_property, source_output=""):
    if not unreal.MaterialEditingLibrary.connect_material_property(expression, source_output, material_property):
        raise RuntimeError(f"재질 출력 연결 실패: {material_property}")


def truncate_if_available(factory, source, x, y, source_output=""):
    truncate_class = getattr(unreal, "MaterialExpressionTruncateLWC", None)
    if truncate_class is None:
        return source, source_output
    truncated = factory.node(truncate_class, x, y)
    link(source, truncated, "", source_output)
    return truncated, ""


def mask_channels(factory, source, x, y, channels, source_output=""):
    mask = factory.node(unreal.MaterialExpressionComponentMask, x, y, r="R" in channels, g="G" in channels, b="B" in channels, a="A" in channels)
    link(source, mask, "", source_output)
    return mask


def add_parameters(factory, sources, scalar_defaults, vector_defaults):
    for name, rgba in vector_defaults:
        sources[name] = (factory.parameter(unreal.MaterialExpressionVectorParameter, parameter_name=name, default_value=unreal.LinearColor(*rgba)), "RGB")
    for name, value in scalar_defaults:
        sources[name] = (factory.parameter(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=value), "")


def build_custom_node(factory, description, output_type, input_names, code, sources, x, y, additional_outputs=()):
    custom = factory.node(unreal.MaterialExpressionCustom, x, y)
    custom.set_editor_property("description", description)
    custom.set_editor_property("output_type", output_type)
    inputs = []
    for name in input_names:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        inputs.append(custom_input)
    custom.set_editor_property("inputs", inputs)
    outputs = []
    for name in additional_outputs:
        custom_output = unreal.CustomOutput()
        custom_output.set_editor_property("output_name", name)
        custom_output.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        outputs.append(custom_output)
    custom.set_editor_property("additional_outputs", outputs)
    custom.set_editor_property("code", code)
    stored_names = [str(item.get_editor_property("input_name")) for item in custom.get_editor_property("inputs")]
    if stored_names != list(input_names):
        raise RuntimeError(f"{description} Custom 입력 이름 설정 확인 실패: " + ", ".join(stored_names))
    for name in input_names:
        source, source_output = sources[name]
        link(source, custom, name, source_output)
    return custom


def compile_material(material, label):
    errors = [str(item) for item in unreal.MaterialEditingLibrary.recompile_material(material) or []]
    if errors:
        raise RuntimeError(f"{label} 컴파일 실패(Saved/Logs/TDGame.log 의 LogMaterial 확인): " + " | ".join(errors))
    try:
        statistics = unreal.MaterialEditingLibrary.get_statistics(material)
        return {name: statistics.get_editor_property(name) for name in ("num_vertex_shader_instructions", "num_pixel_shader_instructions", "num_samplers")}
    except Exception:
        return {}


def verify_parameters(material, label, scalar_names, vector_names, texture_names):
    editing = unreal.MaterialEditingLibrary
    found = {
        "vector": {str(name) for name in editing.get_vector_parameter_names(material)},
        "scalar": {str(name) for name in editing.get_scalar_parameter_names(material)},
        "texture": {str(name) for name in editing.get_texture_parameter_names(material)},
    }
    expected = {"vector": vector_names, "scalar": scalar_names, "texture": texture_names}
    missing = [f"{kind}:{name}" for kind, names in expected.items() for name in names if name not in found[kind]]
    if missing:
        raise RuntimeError(f"{label} 파라미터 없음: " + ", ".join(missing))


def prepare_material(path, overwrite):
    material, was_created = load_or_create(path, unreal.Material, unreal.MaterialFactoryNew(), overwrite)
    if not was_created:
        unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
    return material, was_created


def create_copy_master(code, velocity_texture, overwrite):
    material, was_created = prepare_material(COPY_MASTER_PATH, overwrite)
    for name, value in (("shading_model", unreal.MaterialShadingModel.MSM_UNLIT), ("blend_mode", unreal.BlendMode.BLEND_OPAQUE), ("two_sided", False)):
        material.set_editor_property(name, value)
    factory = NodeFactory(lambda expression_class, x, y: unreal.MaterialEditingLibrary.create_material_expression(material, expression_class, x, y))
    sources = {"FlowUv": (factory.node(unreal.MaterialExpressionTextureCoordinate, -1600, -1900, coordinate_index=0), "")}
    texture, sampler_type = velocity_texture
    for texture_name in COPY_TEXTURE_PARAMETER_NAMES:
        sources[texture_name] = (factory.parameter(unreal.MaterialExpressionTextureObjectParameter, parameter_name=texture_name, texture=texture, sampler_type=sampler_type), "")
    add_parameters(factory, sources, COPY_DRIVEN_SCALAR_DEFAULTS + COPY_LOOK_SCALAR_DEFAULTS, COPY_DRIVEN_VECTOR_DEFAULTS + COPY_LOOK_VECTOR_DEFAULTS)
    custom = build_custom_node(factory, "TDFluidFoliageFlowCopy", unreal.CustomMaterialOutputType.CMOT_FLOAT4, COPY_INPUT_NAMES, code, sources, -500, 0)
    connect_material_output(mask_channels(factory, custom, -150, 0, "RGB"), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    statistics = compile_material(material, COPY_MASTER_NAME)
    verify_parameters(material, COPY_MASTER_NAME, [name for name, _ in COPY_DRIVEN_SCALAR_DEFAULTS + COPY_LOOK_SCALAR_DEFAULTS], [name for name, _ in COPY_DRIVEN_VECTOR_DEFAULTS], list(COPY_TEXTURE_PARAMETER_NAMES))
    speed_reference = unreal.MaterialEditingLibrary.get_material_default_scalar_parameter_value(material, "VelocitySpeedReferenceCmPerSec")
    if abs(speed_reference - 600.0) > 1e-3:
        raise RuntimeError(f"복사 마스터 VelocitySpeedReferenceCmPerSec 기본값이 600이 아님(M1): {speed_reference}")
    save_asset(material, COPY_MASTER_PATH)
    return material, {"asset": COPY_MASTER_PATH, "created": was_created, "statistics": statistics}


def build_bend_root(factory):
    local_position = factory.node(unreal.MaterialExpressionPreSkinnedPosition, -2400, 0)
    local_bounds = factory.node(unreal.MaterialExpressionPreSkinnedLocalBounds, -2400, 200)
    planar = mask_channels(factory, local_position, -2200, 0, "RG")
    bottom = mask_channels(factory, local_bounds, -2200, 200, "B", "Min")
    root_local = factory.node(unreal.MaterialExpressionAppendVector, -2000, 100)
    link(planar, root_local, "A")
    link(bottom, root_local, "B")
    transform = factory.node(
        unreal.MaterialExpressionTransformPosition, -1800, 100,
        transform_source_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL,
        transform_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD,
    )
    link(root_local, transform)
    root_world = truncate_if_available(factory, transform, -1650, 100)
    world_position = truncate_if_available(factory, factory.node(unreal.MaterialExpressionWorldPosition, -1800, -100), -1650, -100)
    return {
        "BendWorldPosition": world_position,
        "BendRootWorld": root_world,
        "BendLocalPosition": (local_position, ""),
        "BendLocalBoundsMin": (local_bounds, "Min"),
        "BendLocalBoundsMax": (local_bounds, "Max"),
    }


def build_wind_elapsed_seconds(factory, collection):
    game_time = factory.node(unreal.MaterialExpressionTime, -1800, -1900)
    wind_origin = factory.node(unreal.MaterialExpressionCollectionParameter, -1800, -1845, collection=collection, parameter_name=WIND_ORIGIN_PARAMETER_NAME)
    elapsed = factory.node(unreal.MaterialExpressionSubtract, -1600, -1900)
    link(game_time, elapsed, "A")
    link(wind_origin, elapsed, "B")
    return elapsed


def create_bend_function(channel, code, render_target, collection, overwrite):
    editing = unreal.MaterialEditingLibrary
    function_path = asset_path(channel.function_name)
    function, was_created = load_or_create(function_path, unreal.MaterialFunction, unreal.MaterialFunctionFactoryNew(), overwrite)
    if not was_created:
        editing.delete_all_material_expressions_in_function(function)
    factory = NodeFactory(lambda expression_class, x, y: editing.create_material_expression_in_function(function, expression_class, x, y))
    sources = build_bend_root(factory)
    sources["ElapsedSeconds"] = (build_wind_elapsed_seconds(factory, collection), "")
    sources["FoliageFlowTexture"] = (factory.node(unreal.MaterialExpressionTextureObject, -1600, -2000, texture=render_target, sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR), "")
    for input_name, collection_name in BEND_COLLECTION_SOURCE_BY_INPUT.items():
        x, y = factory.place()
        sources[input_name] = (factory.node(unreal.MaterialExpressionCollectionParameter, x, y, collection=collection, parameter_name=collection_name), "")
    add_parameters(factory, sources, BEND_LOOK_SCALAR_DEFAULTS, BEND_LOOK_VECTOR_DEFAULTS)
    custom = build_custom_node(factory, "TDFluidFoliageBend", unreal.CustomMaterialOutputType.CMOT_FLOAT3, BEND_INPUT_NAMES, code, sources, -500, 0, BEND_ADDITIONAL_OUTPUT_NAMES)
    offset_output = factory.node(unreal.MaterialExpressionFunctionOutput, 200, 0, output_name=FUNCTION_OUTPUT_NAMES[0], sort_priority=0)
    link(custom, offset_output)
    interpolator = factory.node(unreal.MaterialExpressionVertexInterpolator, -100, 200)
    link(custom, interpolator, "", BEND_ADDITIONAL_OUTPUT_NAMES[0])
    amount_output = factory.node(unreal.MaterialExpressionFunctionOutput, 200, 200, output_name=FUNCTION_OUTPUT_NAMES[1], sort_priority=1)
    link(interpolator, amount_output)
    editing.update_material_function(function)
    save_asset(function, function_path)
    return function, {"asset": function_path, "created": was_created, "outputs": list(FUNCTION_OUTPUT_NAMES), "render_target": texture_asset_path(channel.render_target_name), "collection": asset_path(channel.collection_name),
                      "elapsed_seconds": f"Time - {channel.collection_name}.{WIND_ORIGIN_PARAMETER_NAME}"}


def create_grass_master(channel, function, textures, nanite_enabled, overwrite):
    editing = unreal.MaterialEditingLibrary
    grass_master_path = asset_path(channel.grass_master_name)
    material, was_created = prepare_material(grass_master_path, overwrite)
    assignments = [
        ("blend_mode", unreal.BlendMode.BLEND_MASKED),
        ("shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE),
        ("two_sided", True),
        ("used_with_instanced_static_meshes", True),
    ]
    if nanite_enabled:
        assignments.append(("used_with_nanite", True))
    for name, value in assignments:
        material.set_editor_property(name, value)
    factory = NodeFactory(lambda expression_class, x, y: editing.create_material_expression(material, expression_class, x, y))
    call = factory.node(unreal.MaterialExpressionMaterialFunctionCall, -900, 400)
    call.set_editor_property("material_function", function)
    connect_material_output(call, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET, FUNCTION_OUTPUT_NAMES[0])
    sources = {}
    add_parameters(factory, sources, GRASS_LOOK_SCALAR_DEFAULTS, GRASS_LOOK_VECTOR_DEFAULTS)
    base_texture, base_sampler = textures["FoliageBaseColorTexture"]
    opacity_texture, opacity_sampler = textures["FoliageOpacityTexture"]
    base_sample = factory.parameter(unreal.MaterialExpressionTextureSampleParameter2D, parameter_name="FoliageBaseColorTexture", texture=base_texture, sampler_type=base_sampler)
    opacity_sample = factory.parameter(unreal.MaterialExpressionTextureSampleParameter2D, parameter_name="FoliageOpacityTexture", texture=opacity_texture, sampler_type=opacity_sampler)
    tinted = factory.node(unreal.MaterialExpressionMultiply, -600, -400)
    link(base_sample, tinted, "A", "RGB")
    tint_source, tint_output = sources["FoliageBaseColorTint"]
    link(tint_source, tinted, "B", tint_output)
    pressed = factory.node(unreal.MaterialExpressionMultiply, -600, -250)
    brightness_source, _ = sources["FoliagePressedBrightness"]
    link(brightness_source, pressed, "A")
    link(call, pressed, "B", FUNCTION_OUTPUT_NAMES[1])
    lift = factory.node(unreal.MaterialExpressionAdd, -450, -250, const_a=1.0)
    link(pressed, lift, "B")
    base_color = factory.node(unreal.MaterialExpressionMultiply, -300, -400)
    link(tinted, base_color, "A")
    link(lift, base_color, "B")
    connect_material_output(base_color, unreal.MaterialProperty.MP_BASE_COLOR)
    connect_material_output(opacity_sample, unreal.MaterialProperty.MP_OPACITY_MASK, "R")
    subsurface_source, subsurface_output = sources["FoliageSubsurfaceColor"]
    connect_material_output(subsurface_source, unreal.MaterialProperty.MP_SUBSURFACE_COLOR, subsurface_output)
    roughness_source, _ = sources["FoliageRoughness"]
    connect_material_output(roughness_source, unreal.MaterialProperty.MP_ROUGHNESS)
    statistics = compile_material(material, channel.grass_master_name)
    if not editing.has_material_usage(material, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES):
        raise RuntimeError(f"{channel.grass_master_name} 인스턴스드 스태틱 메시 사용 플래그 확인 실패(L-editor-61)")
    verify_parameters(
        material, channel.grass_master_name,
        [name for name, _ in BEND_LOOK_SCALAR_DEFAULTS + GRASS_LOOK_SCALAR_DEFAULTS],
        [name for name, _ in BEND_LOOK_VECTOR_DEFAULTS + GRASS_LOOK_VECTOR_DEFAULTS],
        list(GRASS_TEXTURE_PARAMETER_NAMES),
    )
    save_asset(material, grass_master_path)
    return material, {"asset": grass_master_path, "created": was_created, "statistics": statistics, "used_with_nanite": bool(nanite_enabled)}


def prepare_instance(name, overwrite):
    instance, _ = load_or_create(asset_path(name), unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew(), overwrite)
    return instance


def write_report(report):
    report_path = project_path(*REPORT_RELATIVE_PATH)
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, "w", encoding="utf-8") as report_file:
        json.dump(report, report_file, ensure_ascii=False, indent=2, default=str)
    return report_path


def load_existing_copy_master():
    if not unreal.EditorAssetLibrary.does_asset_exist(COPY_MASTER_PATH):
        raise RuntimeError(f"복사 마스터 없음(targets 에 copy 를 넣으세요): {COPY_MASTER_PATH}")
    material = unreal.EditorAssetLibrary.load_asset(COPY_MASTER_PATH)
    texture_names = {str(name) for name in unreal.MaterialEditingLibrary.get_texture_parameter_names(material)}
    missing = [name for name in COPY_TEXTURE_PARAMETER_NAMES if name not in texture_names]
    if missing:
        raise RuntimeError("복사 마스터에 텍스처 매개변수 없음(targets 에 copy 를 넣어 다시 만드세요): " + ", ".join(missing))
    return material


def build_channel(channel, bend_code, grass_textures, nanite_enabled, overwrite):
    print(f"단계: 채널 {channel.key} 흐름 RT·MPC")
    render_target, render_target_created = load_or_create_render_target(channel, overwrite)
    collection, collection_created = load_or_create_collection(channel, overwrite)
    print(f"단계: 채널 {channel.key} 휨 재질 함수·풀 재질")
    function, function_summary = create_bend_function(channel, bend_code, render_target, collection, overwrite)
    grass_master, grass_summary = create_grass_master(channel, function, grass_textures, nanite_enabled, overwrite)
    summary = {
        "render_target": {"asset": texture_asset_path(channel.render_target_name), "created": render_target_created, "size": RENDER_TARGET_SIZE, "format": "RTF_RGBA16F", "clear_color": list(RENDER_TARGET_CLEAR_COLOR)},
        "collection": {"asset": asset_path(channel.collection_name), "created": collection_created, "vectors": [name for name, _ in COLLECTION_VECTOR_DEFAULTS], "scalars": dict(COLLECTION_SCALAR_DEFAULTS)},
        "bend_function": function_summary,
        "grass_master": grass_summary,
    }
    return grass_master, summary


def main():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    assets = unreal.EditorAssetLibrary
    missing_names = find_missing_unreal_names()
    if missing_names:
        raise RuntimeError("언리얼 Python 이름 없음: " + ", ".join(missing_names))
    if editor.get_game_world() is not None:
        raise RuntimeError("PIE를 종료한 뒤 실행하세요.")

    overwrite, targets = read_request()
    channels = select_channels(targets)
    look_table = load_material_look_table()
    project_root = unreal.SystemLibrary.get_project_directory()
    copy_code = assemble_node_code(read_shader_source(project_root, COPY_HLSL_NAME))
    bend_code = assemble_node_code(read_shader_source(project_root, BEND_HLSL_NAME))
    problems = validate_hlsl_code(copy_code, COPY_INPUT_NAMES, COPY_SAMPLER_NAMES)
    problems += validate_hlsl_code(bend_code, BEND_INPUT_NAMES, BEND_SAMPLER_NAMES, BEND_ADDITIONAL_OUTPUT_NAMES)
    problems += [f"입력 원천 없음: {name}" for name in find_unresolved_inputs()]
    problems += find_instance_override_problems(look_table.look["Materials"], channels)
    if problems:
        raise RuntimeError("자체 점검 실패: " + " | ".join(problems))

    existing_paths = [path for path in collect_target_asset_paths(targets) if assets.does_asset_exist(path)]
    if existing_paths and not overwrite:
        print(json.dumps({"status": "기존 에셋이 있어 아무것도 바꾸지 않았습니다", "existing_assets": existing_paths, "overwrite_request_file": project_path(*REQUEST_RELATIVE_PATH), "overwrite_request_content": {"overwrite": True}}, ensure_ascii=False))
        raise RuntimeError("기존 에셋 보존: overwrite 요청 파일이 없어 중단합니다.")
    unsaved_names = find_unsaved_package_names(overwrite)
    if unsaved_names:
        raise RuntimeError("미저장 작업을 보존합니다: " + ", ".join(unsaved_names))

    builds_copy_master = COPY_TARGET_KEY in targets
    copy_master = load_existing_copy_master() if channels and not builds_copy_master else None
    default_texture_path, default_texture = pick_default_texture() if builds_copy_master else ("", None)
    grass_textures, grass_texture_sources, grass_mesh = resolve_grass_textures() if channels else ({}, {}, {})

    copy_summary = None
    if builds_copy_master:
        print("단계: 복사 재질")
        copy_master, copy_summary = create_copy_master(copy_code, default_texture, overwrite)

    grass_masters = {}
    channel_reports = {}
    for channel in channels:
        grass_master, channel_reports[channel.key] = build_channel(channel, bend_code, grass_textures, grass_mesh["nanite_enabled"], overwrite)
        grass_masters[asset_path(channel.grass_master_name)] = grass_master

    print("단계: 재질 인스턴스")
    instances = []
    for name, parent_path in make_instance_parents(channels).items():
        parent = copy_master if parent_path == COPY_MASTER_PATH else grass_masters[parent_path]
        instance = prepare_instance(name, overwrite)
        entry = look_table.apply_to_instance(unreal.MaterialEditingLibrary, unreal.LinearColor, instance, asset_path(name), parent)
        save_asset(instance, asset_path(name))
        instances.append({"asset": asset_path(name), "parent": parent_path, "scalars": entry["Scalars"], "vectors": entry["Vectors"]})

    report = {
        "status": "ok",
        "overwrite": overwrite,
        "targets": list(targets),
        "copy_master": copy_summary,
        "channels": channel_reports,
        "grass_textures": grass_texture_sources,
        "grass_mesh": grass_mesh,
        "default_velocity_texture": default_texture_path,
        "instances": instances,
        "hlsl": {
            "copy": {"path": "/".join(SHADER_DIRECTORY + (COPY_HLSL_NAME,)), "sha256": hashlib.sha256(copy_code.encode("utf-8")).hexdigest()},
            "bend": {"path": "/".join(SHADER_DIRECTORY + (BEND_HLSL_NAME,)), "sha256": hashlib.sha256(bend_code.encode("utf-8")).hexdigest()},
        },
        "material_look_sha256": look_table.source_sha256(),
        "niagara": "미사용(L-editor-62·63·68 해당 없음)",
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
