"""실행 환경: TDGame 언리얼 에디터 안에서 엔진 이종 볼륨(HVOL) 마스터 M_TD_FluidHeterogeneousVolume(MD_VOLUME·BLEND_ADDITIVE·HVOL 사용 플래그, Custom 노드 1개 = Tools/FluidFX/Shaders/TDFluidVolumeDensityCommon.hlsl + TDFluidHeterogeneousVolume.hlsl, 보조 출력 알베도·발광·차폐), 단면 텍스처 3장, 재질 인스턴스 7개를 만든다. 먼저 editor_make_fluid_volume_material.py(T_TD_FluidNoiseVolume·MPC_TD_FluidVolumeFlow)·make_fluid_cross_section_textures.py(PNG)가 끝나 있고, look-fragment-fx9.json 이 모양 정본에 병합되어 있어야 한다(반박 S5). MPC 가 이미 있으면 이 생성기만 다시 돌린다 — SVOL 생성기를 overwrite 로 다시 돌리면 MPC 매개변수 GUID 가 새로 생겨 이 재질의 CollectionParameter 가 잘못된 매개변수 컴파일 오류가 된다(refute-k10-r2-scene 1.3).
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_heterogeneous_volume_material.py (기존 에셋을 다시 지을 때는 Saved/FluidFX/fluid_heterogeneous_volume_material_request.json 에 {"overwrite": true})
출력: /Game/FluidFX/Materials/M_TD_FluidHeterogeneousVolume·MI_TD_FluidHeterogeneousVolume_*(7), /Game/FluidFX/Textures/T_TD_FluidCrossSection_{Mushroom,Pump,Ring}(sRGB 끔·16비트 반정밀·고정 주소), Saved/FluidFX/fluid_heterogeneous_volume_material.json(앞단 substrate|legacy, 최종 컴파일 오류 수, 사용 플래그, HLSL sha256, 텍스처 원본 sha256, flow_collection, master.elapsed_seconds)
상태: 미검증(2026-10-26 FX-9 묶음 E 작성: py_compile·가짜 unreal·check_fluid_hvol_hlsl.py 만, 에디터 실행 전). 사용 플래그는 노드 만들기 전에 재질 속성으로 켜고 확인은 컴파일 뒤(L-editor-61·36), 속성이 없으면 컴파일 뒤 set_material_usage. 도메인 변경 직후 Additive 경고는 무시하고 최종 컴파일 오류 수로 판정(L-editor-65). MI 텍스처 값은 반환값 대신 다시 읽어 판정(L-editor-66). 마스터는 제자리 재구성하고, 형식이 다른 기존 에셋만 지운 뒤 디스크 잔존을 확인한다(L-editor-67·10). MI 값은 모양 정본(apply_to_instance)만 쓴다. 2026-10-31: HVOL 라이브 셰이딩은 ObjectPositionWS 를 (0,0,0)·ObjectBounds 를 거대값으로 넘겨(엔진 LiveShadingPipeline.usf 269~276 TODO) 화면에 안 보였다 → VolObjectPosition 은 로컬 원점 TransformPosition(로컬→월드)·VolObjectBoundsCm 은 ObjectLocalBounds 반 크기 Transform(로컬→월드)의 절댓값으로, HLSL 중심·크기는 lerp 대신 선택식(PIE 3회 반복 확인, Saved/AgentOps/20261031/hvol/r2). 2026-11-01 시간 원점(K10 fx9 대조 표류, Saved/AgentOps/20261101/diag-k10-hvol.md): Custom 입력 ElapsedSeconds = Time − MPC_TD_FluidVolumeFlow.VolumeFlowOriginSeconds(SVOL M_TD_FluidVolume 과 같은 MPC·같은 꼴, MPC 는 editor_make_fluid_volume_material.py 가 만들고 여기서는 읽기만, 없거나 매개변수가 없으면 텍스처 단계 전에 중단, 기본 0 이라 게임 모양 불변) — 가짜 unreal 시험(calib_offline_test F12d)만, 에디터 재생성 전에는 자산이 옛 Time 그래프다.
"""
import gc
import hashlib
import importlib.util
import json
import os
import re

import unreal


FLUID_ROOT = "/Game/FluidFX"
MATERIAL_DIR = f"{FLUID_ROOT}/Materials"
TEXTURE_DIR = f"{FLUID_ROOT}/Textures"
MASTER_NAME = "M_TD_FluidHeterogeneousVolume"
MASTER_PATH = f"{MATERIAL_DIR}/{MASTER_NAME}"
NOISE_VOLUME_PATH = f"{TEXTURE_DIR}/T_TD_FluidNoiseVolume"
FLOW_COLLECTION_NAME = "MPC_TD_FluidVolumeFlow"
FLOW_COLLECTION_PATH = f"{MATERIAL_DIR}/{FLOW_COLLECTION_NAME}"
FLOW_ORIGIN_PARAMETER_NAME = "VolumeFlowOriginSeconds"
DEFAULT_TEXTURE_PATHS = ("/Engine/EngineResources/Black", "/Engine/EngineResources/DefaultTexture")
SHADER_DIRECTORY = ("Tools", "FluidFX", "Shaders")
COMMON_HLSL_NAME = "TDFluidVolumeDensityCommon.hlsl"
BODY_HLSL_NAME = "TDFluidHeterogeneousVolume.hlsl"
CROSS_SECTION_SOURCE_DIRECTORY = ("Saved", "FluidFX", "cross_section_textures")
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_heterogeneous_volume_material_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_heterogeneous_volume_material.json")
MATERIAL_LOOK_MODULE_RELATIVE_PATH = ("Tools", "FluidFX", "fluid_material_look.py")
PARAMETER_GROUP = "TDFluidHeterogeneousVolume"
MACRO_PREFIXES = ("TD_VOL_", "tdVol")
USAGE_PROPERTY = "used_with_heterogeneous_volumes"
USAGE_ENUM_NAME = "MATUSAGE_HETEROGENEOUS_VOLUMES"
ADDITIVE_WARNING_TEXT = "Volume materials must use an Additive blend mode"

CROSS_SECTION_NAMES = ("Mushroom", "Pump", "Ring")
CROSS_SECTION_TEXTURE_PARAMETER = "CrossSectionTexture"
DRIVEN_VECTOR_DEFAULTS = (
    ("SimCenterWorld", (0.0, 0.0, 0.0, 0.0)),
    ("SimExtentsCm", (0.0, 0.0, 0.0, 0.0)),
    ("PlaneAxisU", (1.0, 0.0, 0.0, 0.0)),
    ("PlaneAxisV", (0.0, 1.0, 0.0, 0.0)),
    ("PlaneAxisN", (0.0, 0.0, 1.0, 0.0)),
    ("PlaneCoordinateOffsetCm", (0.0, 0.0, 0.0, 0.0)),
    ("VolumeOffsetCm", (0.0, 0.0, 0.0, 0.0)),
    ("TerrainHeightWindow", (0.0, 0.0, 0.0, 0.0)),
    ("TerrainHeightDecode", (0.0, 0.0, 0.0, 0.0)),
)
DRIVEN_SCALAR_DEFAULTS = (("SimGridResolution", 0.0), ("ExtrudeSymmetric", 0.0), ("AuxHeatActive", 0.0))
LOOK_VECTOR_DEFAULTS = (
    ("VolAlbedo", (0.8, 0.8, 0.82, 1.0)),
    ("VolNoiseWindCmPerSec", (0.0, 0.0, 20.0, 0.0)),
    ("VolCrossSectionAxisOffsetCm", (0.0, 0.0, 0.0, 0.0)),
)
LOOK_SCALAR_DEFAULTS = (
    ("VolExtinctionPerM", 0.4),
    ("VolAmbientFloor", 0.35),
    ("VolEdgeFadeCm", 40.0),
    ("VolExtrudeHeightCm", 120.0),
    ("VolExtrudeExponent", 1.0),
    ("VolTopFadeCm", 60.0),
    ("VolTerrainAlignBlend", 0.0),
    ("VolTerrainMaxDropCm", 2000.0),
    ("VolCrossSectionMode", 0.0),
    ("VolCrossSectionHeightCm", 300.0),
    ("VolCrossSectionRadialExponent", 1.0),
    ("VolNoiseInWorldSpace", 1.0),
    ("VolNoiseScaleCm", 300.0),
    ("VolNoiseSecondScaleCm", 900.0),
    ("VolNoiseMix", 0.0),
    ("VolNoiseSubtract", 0.3),
    ("VolPolarNoise", 0.0),
    ("VolPolarAngularScaleCm", 100.0),
    ("VolFlowStrength", 0.0),
    ("VolFlowVelocityReferenceCmPerSec", 600.0),
    ("VolFlowCycleSeconds", 1.2),
    ("VolHeightFadeStartCm", 0.0),
    ("VolHeightFadeLengthCm", 0.0),
    ("VolCameraFadeStartCm", 0.0),
    ("VolCameraFadeLengthCm", 0.0),
    ("VolCameraFadeInvert", 0.0),
    ("VolColorInputBlend", 0.0),
    ("VolColorInputHeightFromAlpha", 0.0),
    ("VolBlackbodyTemperatureK", 1500.0),
    ("VolBlackbodyIntensity", 0.0),
    ("VolBlackbodyExponent", 2.0),
    ("VolPassiveCoverage", 0.5),
    ("VolPassiveNoiseScaleCm", 1500.0),
)
VECTOR_PARAMETER_DEFAULTS = DRIVEN_VECTOR_DEFAULTS + LOOK_VECTOR_DEFAULTS
SCALAR_PARAMETER_DEFAULTS = DRIVEN_SCALAR_DEFAULTS + LOOK_SCALAR_DEFAULTS
FLOAT4_VECTOR_NAMES = ("TerrainHeightWindow", "TerrainHeightDecode")
TEXTURE_PARAMETER_NAMES = (
    "DensityPressureTexture", "VelocityTexture", "AuxTexture", "NoiseVolumeTexture",
    CROSS_SECTION_TEXTURE_PARAMETER, "VolColorInputTexture", "TerrainHeightTexture",
)
VOLUME_TEXTURE_NAMES = ("NoiseVolumeTexture",)
BUILTIN_SOURCE_NAMES = ("VolWorldPosition", "VolCameraPosition", "VolObjectPosition", "VolObjectBoundsCm", "ElapsedSeconds")
BUILTIN_VECTOR_NAMES = ("VolWorldPosition", "VolCameraPosition", "VolObjectPosition", "VolObjectBoundsCm")
CUSTOM_INPUT_NAMES = BUILTIN_SOURCE_NAMES + TEXTURE_PARAMETER_NAMES + tuple(name for name, _ in VECTOR_PARAMETER_DEFAULTS) + tuple(name for name, _ in SCALAR_PARAMETER_DEFAULTS)
VECTOR_INPUT_NAMES = BUILTIN_VECTOR_NAMES + tuple(name for name, _ in VECTOR_PARAMETER_DEFAULTS if name not in FLOAT4_VECTOR_NAMES)
OUTPUT_SPECS = (("VolAlbedoOut", "CMOT_FLOAT3"), ("VolEmissiveOut", "CMOT_FLOAT3"), ("VolAmbientOcclusionOut", "CMOT_FLOAT1"))
SAMPLER_NAMES = tuple(name + "Sampler" for name in TEXTURE_PARAMETER_NAMES)
ENGINE_SYMBOLS = ("Texture2DSampleLevel", "Texture3DSampleLevel")
MID_DRIVEN_NAMES = tuple(name for name, _ in DRIVEN_VECTOR_DEFAULTS) + tuple(name for name, _ in DRIVEN_SCALAR_DEFAULTS) + (
    "DensityPressureTexture", "VelocityTexture", "AuxTexture", "TerrainHeightTexture", "VolumeHeightCm", "VolumeBoxScale",
)
CONDITIONAL_CPP_NAMES = ("DensityScale", "SoftFalloffCm")
INSTANCE_NAMES = (
    "MI_TD_FluidHeterogeneousVolume_Smoke",
    "MI_TD_FluidHeterogeneousVolume_Vortex",
    "MI_TD_FluidHeterogeneousVolume_CameraFacing",
    "MI_TD_FluidHeterogeneousVolume_FloorSmoke",
    "MI_TD_FluidHeterogeneousVolume_CrossSectionRadial",
    "MI_TD_FluidHeterogeneousVolume_CrossSectionRing",
    "MI_TD_FluidHeterogeneousVolume_Passive",
)
INSTANCE_CROSS_SECTIONS = {
    "MI_TD_FluidHeterogeneousVolume_CrossSectionRadial": "Mushroom",
    "MI_TD_FluidHeterogeneousVolume_CrossSectionRing": "Ring",
}
SUBSTRATE_PIN_CANDIDATES = {
    "albedo": ("Albedo",),
    "extinction": ("Extinction",),
    "emissive": ("EmissiveColor", "Emissive Color", "Emissive"),
    "occlusion": ("AmbientOcclusion", "Ambient Occlusion"),
}
REQUIRED_UNREAL_NAMES = (
    "EditorAssetLibrary.load_asset",
    "EditorAssetLibrary.save_loaded_asset",
    "EditorAssetLibrary.does_asset_exist",
    "EditorAssetLibrary.delete_asset",
    "EditorLoadingAndSavingUtils.get_dirty_map_packages",
    "EditorLoadingAndSavingUtils.get_dirty_content_packages",
    "EditorLoadingAndSavingUtils.unload_packages",
    "SystemLibrary.get_project_directory",
    "SystemLibrary.collect_garbage",
    "AssetRegistryHelpers.get_asset_registry",
    "MaterialEditingLibrary.create_material_expression",
    "MaterialEditingLibrary.connect_material_expressions",
    "MaterialEditingLibrary.connect_material_property",
    "MaterialEditingLibrary.delete_all_material_expressions",
    "MaterialEditingLibrary.delete_material_expression",
    "MaterialEditingLibrary.recompile_material",
    "MaterialEditingLibrary.has_material_usage",
    "MaterialEditingLibrary.get_vector_parameter_names",
    "MaterialEditingLibrary.get_scalar_parameter_names",
    "MaterialEditingLibrary.get_texture_parameter_names",
    "MaterialEditingLibrary.set_material_instance_texture_parameter_value",
    "MaterialEditingLibrary.get_material_instance_texture_parameter_value",
    "MaterialEditingLibrary.update_material_instance",
    "Material",
    "MaterialFactoryNew",
    "MaterialInstanceConstant",
    "MaterialInstanceConstantFactoryNew",
    "MaterialExpressionCustom",
    "CustomInput",
    "CustomOutput",
    "CustomMaterialOutputType.CMOT_FLOAT1",
    "CustomMaterialOutputType.CMOT_FLOAT3",
    "CustomMaterialOutputType.CMOT_FLOAT4",
    "MaterialExpressionWorldPosition",
    "MaterialExpressionCameraPositionWS",
    "MaterialExpressionConstant3Vector",
    "MaterialExpressionTransformPosition",
    "MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL",
    "MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD",
    "MaterialExpressionObjectLocalBounds",
    "MaterialExpressionTransform",
    "MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL",
    "MaterialVectorCoordTransform.TRANSFORM_WORLD",
    "MaterialExpressionAbs",
    "MaterialExpressionTime",
    "MaterialExpressionCollectionParameter",
    "MaterialExpressionSubtract",
    "MaterialParameterCollection",
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
    "MaterialProperty.MP_EMISSIVE_COLOR",
    "MaterialProperty.MP_AMBIENT_OCCLUSION",
    "MaterialSamplerType.SAMPLERTYPE_COLOR",
    "MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR",
    "TextureCompressionSettings.TC_DEFAULT",
    "TextureCompressionSettings.TC_HALF_FLOAT",
    "TextureAddress.TA_CLAMP",
    "TextureMipGenSettings.TMGS_NO_MIPMAPS",
    "AssetImportTask",
    "VolumeTexture",
    "Texture2D",
)

IDENTIFIER_PATTERN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
DEFINE_PATTERN = re.compile(r"^[ \t]*#define[ \t]+([A-Za-z_][A-Za-z0-9_]*)", re.MULTILINE)


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


def asset_path(name):
    return f"{MATERIAL_DIR}/{name}"


def texture_asset_name(section_name):
    return f"T_TD_FluidCrossSection_{section_name}"


def texture_asset_path(section_name):
    return f"{TEXTURE_DIR}/{texture_asset_name(section_name)}"


def asset_disk_path(path):
    return project_path("Content", *path[len("/Game/"):].split("/")) + ".uasset"


def read_text_file(path):
    if not os.path.isfile(path):
        raise RuntimeError(f"HLSL 파일 없음: {path}")
    with open(path, encoding="utf-8") as text_file:
        return text_file.read().replace("\r\n", "\n").strip("\n") + "\n"


def read_shader_sources(project_root):
    shader_root = os.path.join(project_root, *SHADER_DIRECTORY)
    return read_text_file(os.path.join(shader_root, COMMON_HLSL_NAME)), read_text_file(os.path.join(shader_root, BODY_HLSL_NAME))


def assemble_node_code(common_code, body_code):
    body = common_code + "\n" + body_code
    macro_names = sorted(set(DEFINE_PATTERN.findall(body)))
    undefine_block = "".join(f"#undef {name}\n" for name in macro_names)
    return undefine_block + body + "\n" + undefine_block


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
    output_names = tuple(name for name, _ in OUTPUT_SPECS)
    for label, names in (("Custom 입력", CUSTOM_INPUT_NAMES), ("샘플러", SAMPLER_NAMES), ("추가 출력", output_names), ("엔진 심볼", ENGINE_SYMBOLS)):
        missing = [name for name in names if name not in identifiers]
        if missing:
            problems.append(f"HLSL이 쓰지 않는 {label}: " + ", ".join(missing))
    leaked = [name for name in DEFINE_PATTERN.findall(code) if not name.startswith(MACRO_PREFIXES)]
    if leaked:
        problems.append("접두 밖 매크로: " + ", ".join(leaked))
    duplicated = sorted({name for name in CUSTOM_INPUT_NAMES if CUSTOM_INPUT_NAMES.count(name) > 1})
    if duplicated:
        problems.append("Custom 입력 이름 중복: " + ", ".join(duplicated))
    bracket_problem = find_unbalanced_brackets(code)
    if bracket_problem:
        problems.append(bracket_problem)
    if "return" not in identifiers:
        problems.append("HLSL에 return 이 없음")
    if "//" in code or "/*" in code:
        problems.append("HLSL에 주석이 있음")
    return problems


def find_unresolved_inputs():
    resolvable = set(BUILTIN_SOURCE_NAMES) | set(TEXTURE_PARAMETER_NAMES)
    resolvable |= {name for name, _ in VECTOR_PARAMETER_DEFAULTS} | {name for name, _ in SCALAR_PARAMETER_DEFAULTS}
    return sorted(set(CUSTOM_INPUT_NAMES) - resolvable)


def find_instance_override_problems(entries):
    problems = []
    overridable = {name for name, _ in LOOK_VECTOR_DEFAULTS} | {name for name, _ in LOOK_SCALAR_DEFAULTS}
    for name in INSTANCE_NAMES:
        entry = entries.get(asset_path(name))
        if entry is None:
            problems.append(f"{name}: 모양 정본에 없음(look-fragment-fx9.json 병합 먼저)")
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
    return [MASTER_PATH] + [asset_path(name) for name in INSTANCE_NAMES] + [texture_asset_path(name) for name in CROSS_SECTION_NAMES]


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


def delete_asset_fully(path):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    unreal.EditorAssetLibrary.delete_asset(path)
    disk_path = asset_disk_path(path)
    if not os.path.isfile(disk_path):
        return "deleted"
    if asset is not None:
        unreal.EditorLoadingAndSavingUtils.unload_packages([asset.get_outermost()])
    asset = None
    gc.collect()
    unreal.SystemLibrary.collect_garbage()
    try:
        os.remove(disk_path)
    except OSError as error:
        raise RuntimeError(f"디스크 잔존 에셋 삭제 실패(에디터를 멈추고 이 파일만 지운 뒤 재실행, L-editor-67): {disk_path} {error}")
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([path.rsplit("/", 1)[0]], True)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        raise RuntimeError(f"디스크에서 지웠지만 메모리에 남음(에디터 재시작, L-editor-10): {path}")
    return "deleted_from_disk"


def load_or_replace(path, expected_class, overwrite):
    assets = unreal.EditorAssetLibrary
    if not assets.does_asset_exist(path):
        return None, ""
    if not overwrite:
        raise RuntimeError(f"기존 에셋 보존: {path}")
    existing = assets.load_asset(path)
    if isinstance(existing, expected_class):
        return existing, "reused"
    return None, delete_asset_fully(path)


def choose_sampler_type(texture):
    try:
        compression = texture.get_editor_property("compression_settings")
        is_srgb = bool(texture.get_editor_property("srgb"))
    except Exception:
        return None
    if compression == unreal.TextureCompressionSettings.TC_HALF_FLOAT:
        return unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
    if compression != unreal.TextureCompressionSettings.TC_DEFAULT:
        return unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
    return unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if is_srgb else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR


def pick_default_texture():
    for texture_path in DEFAULT_TEXTURE_PATHS:
        texture = unreal.EditorAssetLibrary.load_asset(texture_path)
        sampler_type = choose_sampler_type(texture) if texture is not None else None
        if sampler_type is not None:
            return texture_path, texture, sampler_type
    raise RuntimeError("기본 텍스처로 쓸 엔진 텍스처가 없음: " + ", ".join(DEFAULT_TEXTURE_PATHS))


def load_noise_volume():
    texture = unreal.EditorAssetLibrary.load_asset(NOISE_VOLUME_PATH)
    if not isinstance(texture, unreal.VolumeTexture):
        raise RuntimeError(f"노이즈 볼륨 텍스처 없음(editor_make_fluid_volume_material.py 먼저): {NOISE_VOLUME_PATH}")
    return texture, choose_sampler_type(texture) or unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR


def load_flow_collection():
    collection = unreal.EditorAssetLibrary.load_asset(FLOW_COLLECTION_PATH)
    if not isinstance(collection, unreal.MaterialParameterCollection):
        raise RuntimeError(f"흐름 원점 MPC 없음(editor_make_fluid_volume_material.py 먼저): {FLOW_COLLECTION_PATH}")
    names = [str(item.get_editor_property("parameter_name")) for item in collection.get_editor_property("scalar_parameters")]
    if FLOW_ORIGIN_PARAMETER_NAME not in names:
        raise RuntimeError(f"흐름 원점 MPC 매개변수 없음: {FLOW_ORIGIN_PARAMETER_NAME}({FLOW_COLLECTION_PATH}, editor_make_fluid_volume_material.py 재생성)")
    return collection


def import_cross_section_texture(section_name, overwrite):
    source_path = project_path(*CROSS_SECTION_SOURCE_DIRECTORY, texture_asset_name(section_name) + ".png")
    if not os.path.isfile(source_path):
        raise RuntimeError(f"단면 원본 없음(python Tools/FluidFX/make_fluid_cross_section_textures.py 먼저): {source_path}")
    path = texture_asset_path(section_name)
    _, replaced = load_or_replace(path, unreal.Texture2D, overwrite)
    ensure_directory(TEXTURE_DIR)
    task = unreal.AssetImportTask()
    for name, value in (("filename", source_path), ("destination_path", TEXTURE_DIR), ("destination_name", texture_asset_name(section_name)), ("replace_existing", True), ("automated", True), ("save", False)):
        task.set_editor_property(name, value)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.EditorAssetLibrary.load_asset(path)
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError(f"단면 텍스처 가져오기 실패: {path}")
    for name, value in (
        ("srgb", False),
        ("compression_settings", unreal.TextureCompressionSettings.TC_HALF_FLOAT),
        ("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS),
        ("address_x", unreal.TextureAddress.TA_CLAMP),
        ("address_y", unreal.TextureAddress.TA_CLAMP),
    ):
        texture.set_editor_property(name, value)
    save_asset(texture, path)
    if bool(texture.get_editor_property("srgb")):
        raise RuntimeError(f"단면 텍스처 sRGB 끄기 확인 실패(반박 S5): {path}")
    sampler_type = choose_sampler_type(texture)
    with open(source_path, "rb") as source_file:
        source_sha256 = hashlib.sha256(source_file.read()).hexdigest()
    return texture, sampler_type, {"asset": path, "source_sha256": source_sha256, "replaced": replaced, "srgb": False, "compression": "TC_HALF_FLOAT", "address": "TA_CLAMP", "sampler_type": str(sampler_type)}


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


def try_link_first_pin(source, source_output, target, candidates):
    for candidate in candidates:
        if unreal.MaterialEditingLibrary.connect_material_expressions(source, source_output, target, candidate):
            return candidate
    return ""


def connect_material_output(expression, material_property, source_output=""):
    if not unreal.MaterialEditingLibrary.connect_material_property(expression, source_output, material_property):
        raise RuntimeError(f"재질 출력 연결 실패: {material_property} <- {source_output}")


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


def build_object_position(material, x, y):
    origin = add_node(material, unreal.MaterialExpressionConstant3Vector, x - 440, y, constant=unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
    to_world = add_node(material, unreal.MaterialExpressionTransformPosition, x - 220, y,
                        transform_source_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL,
                        transform_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    link(origin, to_world)
    return truncate_if_available(material, to_world, x, y)


def build_object_half_extents(material, x, y):
    bounds = add_node(material, unreal.MaterialExpressionObjectLocalBounds, x - 440, y)
    to_world = add_node(material, unreal.MaterialExpressionTransform, x - 220, y,
                        transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,
                        transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    link(bounds, to_world, source_output="Half Extents")
    absolute = add_node(material, unreal.MaterialExpressionAbs, x, y)
    link(to_world, absolute)
    return absolute


def build_elapsed_seconds(material, flow_collection, x, y):
    game_time = add_node(material, unreal.MaterialExpressionTime, x - 440, y)
    flow_origin = add_node(material, unreal.MaterialExpressionCollectionParameter, x - 440, y + 55, collection=flow_collection, parameter_name=FLOW_ORIGIN_PARAMETER_NAME)
    elapsed = add_node(material, unreal.MaterialExpressionSubtract, x - 220, y)
    link(game_time, elapsed, "A")
    link(flow_origin, elapsed, "B")
    return elapsed


def describe_elapsed_seconds():
    return f"Time - {FLOW_COLLECTION_NAME}.{FLOW_ORIGIN_PARAMETER_NAME}"


def prepare_master(overwrite):
    material, replaced = load_or_replace(MASTER_PATH, unreal.Material, overwrite)
    if material is not None:
        unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
        return material, False, replaced
    ensure_directory(MATERIAL_DIR)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(MASTER_NAME, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError(f"마스터 재질 생성 실패: {MASTER_PATH}")
    return material, True, replaced


def enable_usage_before_nodes(material):
    try:
        material.set_editor_property(USAGE_PROPERTY, True)
    except Exception as error:
        return f"property_missing: {error}"
    return "property"


def build_sources(material, textures, flow_collection):
    sources = {}
    rows = iter(range(200))

    def place():
        return -1700, -2400 + next(rows) * 110

    builtin_classes = {
        "VolWorldPosition": unreal.MaterialExpressionWorldPosition,
        "VolCameraPosition": unreal.MaterialExpressionCameraPositionWS,
    }
    for name, expression_class in builtin_classes.items():
        x, y = place()
        sources[name] = (truncate_if_available(material, add_node(material, expression_class, x - 220, y), x, y), "")
    x, y = place()
    sources["VolObjectPosition"] = (build_object_position(material, x, y), "")
    x, y = place()
    sources["VolObjectBoundsCm"] = (build_object_half_extents(material, x, y), "")
    x, y = place()
    sources["ElapsedSeconds"] = (build_elapsed_seconds(material, flow_collection, x, y), "")
    for name in TEXTURE_PARAMETER_NAMES:
        texture, sampler_type = textures[name]
        x, y = place()
        node = add_node(material, unreal.MaterialExpressionTextureObjectParameter, x, y, parameter_name=name, texture=texture, sampler_type=sampler_type)
        assign_parameter_group(node)
        sources[name] = (node, "")
    for name, rgba in VECTOR_PARAMETER_DEFAULTS:
        x, y = place()
        node = add_node(material, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=unreal.LinearColor(*rgba))
        assign_parameter_group(node)
        sources[name] = (node, "RGBA" if name in FLOAT4_VECTOR_NAMES else "RGB")
    for name, value in SCALAR_PARAMETER_DEFAULTS:
        x, y = place()
        node = add_node(material, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)
        assign_parameter_group(node)
        sources[name] = (node, "")
    return sources


def build_custom_node(material, code, sources):
    custom = add_node(material, unreal.MaterialExpressionCustom, -600, 0)
    custom.set_editor_property("description", "TDFluidHeterogeneousVolume")
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    inputs = []
    for name in CUSTOM_INPUT_NAMES:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        inputs.append(custom_input)
    custom.set_editor_property("inputs", inputs)
    outputs = []
    for name, kind in OUTPUT_SPECS:
        custom_output = unreal.CustomOutput()
        custom_output.set_editor_property("output_name", name)
        custom_output.set_editor_property("output_type", getattr(unreal.CustomMaterialOutputType, kind))
        outputs.append(custom_output)
    custom.set_editor_property("additional_outputs", outputs)
    custom.set_editor_property("code", code)
    stored_names = [str(item.get_editor_property("input_name")) for item in custom.get_editor_property("inputs")]
    if stored_names != list(CUSTOM_INPUT_NAMES):
        raise RuntimeError("Custom 노드 입력 이름 설정 확인 실패: " + ", ".join(stored_names))
    for name in CUSTOM_INPUT_NAMES:
        source, source_output = sources[name]
        link(source, custom, name, source_output)
    return custom


def build_front_outputs(material, custom):
    extinction = add_node(material, unreal.MaterialExpressionComponentMask, -250, -120, r=True, g=True, b=True, a=False)
    link(custom, extinction)
    bsdf = add_node(material, unreal.MaterialExpressionSubstrateVolumetricFogCloudBSDF, 200, 0)
    pins = {
        "albedo": try_link_first_pin(custom, "VolAlbedoOut", bsdf, SUBSTRATE_PIN_CANDIDATES["albedo"]),
        "extinction": try_link_first_pin(extinction, "", bsdf, SUBSTRATE_PIN_CANDIDATES["extinction"]),
        "emissive": try_link_first_pin(custom, "VolEmissiveOut", bsdf, SUBSTRATE_PIN_CANDIDATES["emissive"]),
        "occlusion": try_link_first_pin(custom, "VolAmbientOcclusionOut", bsdf, SUBSTRATE_PIN_CANDIDATES["occlusion"]),
    }
    if all(pins.values()) and unreal.MaterialEditingLibrary.connect_material_property(bsdf, "", unreal.MaterialProperty.MP_FRONT_MATERIAL):
        return {"path": "substrate", "pins": pins}
    unreal.MaterialEditingLibrary.delete_material_expression(material, bsdf)
    connect_material_output(custom, unreal.MaterialProperty.MP_BASE_COLOR, "VolAlbedoOut")
    connect_material_output(extinction, unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    connect_material_output(custom, unreal.MaterialProperty.MP_EMISSIVE_COLOR, "VolEmissiveOut")
    occlusion_linked = unreal.MaterialEditingLibrary.connect_material_property(custom, "VolAmbientOcclusionOut", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    return {"path": "legacy", "pins": {"albedo": "MP_BASE_COLOR", "extinction": "MP_SUBSURFACE_COLOR", "emissive": "MP_EMISSIVE_COLOR", "occlusion": "MP_AMBIENT_OCCLUSION" if occlusion_linked else ""}, "substrate_pins_tried": pins}


def compile_errors(material):
    errors = [str(item) for item in unreal.MaterialEditingLibrary.recompile_material(material) or []]
    return [error for error in errors if ADDITIVE_WARNING_TEXT not in error]


def ensure_usage_after_compile(material, usage_route):
    editing = unreal.MaterialEditingLibrary
    usage = getattr(unreal.MaterialUsage, USAGE_ENUM_NAME, None)
    if usage is None:
        if usage_route != "property":
            raise RuntimeError(f"HVOL 사용 플래그를 켤 수단이 없음(속성 {USAGE_PROPERTY}·열거 {USAGE_ENUM_NAME} 둘 다 없음)")
        return {"route": usage_route, "verified": False, "note": f"MaterialUsage.{USAGE_ENUM_NAME} 없음 → 속성 다시 읽기로만 확인"}
    if editing.has_material_usage(material, usage):
        return {"route": usage_route, "verified": True}
    editing.set_material_usage(material, usage)
    errors = compile_errors(material)
    if errors:
        raise RuntimeError("사용 플래그 뒤 재컴파일 실패: " + " | ".join(errors))
    if not editing.has_material_usage(material, usage):
        raise RuntimeError("HVOL 사용 플래그 확인 실패(쿠킹 게임에서 기본 재질로 바뀜, L-editor-61)")
    return {"route": usage_route + "+set_material_usage_after_compile", "verified": True}


def verify_master_parameters(material):
    editing = unreal.MaterialEditingLibrary
    found = {
        "vector": {str(name) for name in editing.get_vector_parameter_names(material)},
        "scalar": {str(name) for name in editing.get_scalar_parameter_names(material)},
        "texture": {str(name) for name in editing.get_texture_parameter_names(material)},
    }
    expected = {"vector": [name for name, _ in VECTOR_PARAMETER_DEFAULTS], "scalar": [name for name, _ in SCALAR_PARAMETER_DEFAULTS], "texture": list(TEXTURE_PARAMETER_NAMES)}
    missing = [f"{kind}:{name}" for kind, names in expected.items() for name in names if name not in found[kind]]
    if missing:
        raise RuntimeError("마스터 파라미터 없음: " + ", ".join(missing))
    if abs(editing.get_material_default_scalar_parameter_value(material, "SimGridResolution")) > 1e-6:
        raise RuntimeError("마스터 SimGridResolution 기본값이 0이 아님(수동 역할 판정 깨짐)")
    if bool(material.get_editor_property(USAGE_PROPERTY)) is not True:
        raise RuntimeError(f"마스터 {USAGE_PROPERTY} 다시 읽기 거짓")


def read_material_statistics(material):
    try:
        statistics = unreal.MaterialEditingLibrary.get_statistics(material)
        return {name: statistics.get_editor_property(name) for name in ("num_vertex_shader_instructions", "num_pixel_shader_instructions", "num_samplers")}
    except Exception:
        return {}


def create_master(code, textures, flow_collection, overwrite):
    material, was_created, replaced = prepare_master(overwrite)
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_VOLUME)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    usage_route = enable_usage_before_nodes(material)
    sources = build_sources(material, textures, flow_collection)
    custom = build_custom_node(material, code, sources)
    front = build_front_outputs(material, custom)
    errors = compile_errors(material)
    if errors:
        raise RuntimeError(f"{MASTER_NAME} 컴파일 실패(Saved/Logs/TDGame.log 의 LogMaterial 확인): " + " | ".join(errors))
    usage = ensure_usage_after_compile(material, usage_route)
    verify_master_parameters(material)
    save_asset(material, MASTER_PATH)
    return material, {
        "asset": MASTER_PATH,
        "created": was_created,
        "replaced_wrong_type": replaced,
        "front": front,
        "elapsed_seconds": describe_elapsed_seconds(),
        "compile_error_count": 0,
        "ignored_warning": ADDITIVE_WARNING_TEXT,
        "usage": usage,
        "statistics": read_material_statistics(material),
        "statistics_note": "볼륨 도메인은 PS 명령 0일 수 있음 → check_fluid_hvol_hlsl.py dxc 명령 수가 대체 지표",
    }


def prepare_instance(name, overwrite):
    path = asset_path(name)
    instance, replaced = load_or_replace(path, unreal.MaterialInstanceConstant, overwrite)
    if instance is not None:
        return instance, replaced
    instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, MATERIAL_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    if instance is None:
        raise RuntimeError(f"재질 인스턴스 생성 실패: {path}")
    return instance, replaced


def apply_cross_section_texture(instance, name, section_texture):
    editing = unreal.MaterialEditingLibrary
    editing.set_material_instance_texture_parameter_value(instance, CROSS_SECTION_TEXTURE_PARAMETER, section_texture)
    editing.update_material_instance(instance)
    stored = editing.get_material_instance_texture_parameter_value(instance, CROSS_SECTION_TEXTURE_PARAMETER)
    if stored is None or stored.get_path_name() != section_texture.get_path_name():
        raise RuntimeError(f"{name}: 단면 텍스처 다시 읽기 불일치(L-editor-66): {stored}")
    return section_texture.get_path_name()


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
    code = assemble_node_code(*read_shader_sources(unreal.SystemLibrary.get_project_directory()))
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

    print("단계: 흐름 원점 MPC")
    flow_collection = load_flow_collection()

    print("단계: 텍스처")
    texture_path, default_texture, default_sampler = pick_default_texture()
    noise_texture, noise_sampler = load_noise_volume()
    section_textures = {}
    texture_records = []
    for section_name in CROSS_SECTION_NAMES:
        texture, sampler_type, record = import_cross_section_texture(section_name, overwrite)
        section_textures[section_name] = (texture, sampler_type)
        texture_records.append(record)
    textures = {name: (default_texture, default_sampler) for name in TEXTURE_PARAMETER_NAMES}
    textures["NoiseVolumeTexture"] = (noise_texture, noise_sampler)
    textures[CROSS_SECTION_TEXTURE_PARAMETER] = section_textures["Mushroom"]

    print("단계: 마스터 재질")
    master, master_summary = create_master(code, textures, flow_collection, overwrite)

    print("단계: 재질 인스턴스")
    instances = []
    for name in INSTANCE_NAMES:
        instance, replaced = prepare_instance(name, overwrite)
        entry = look_table.apply_to_instance(unreal.MaterialEditingLibrary, unreal.LinearColor, instance, asset_path(name), master)
        section_path = ""
        if name in INSTANCE_CROSS_SECTIONS:
            section_path = apply_cross_section_texture(instance, name, section_textures[INSTANCE_CROSS_SECTIONS[name]][0])
        save_asset(instance, asset_path(name))
        instances.append({"asset": asset_path(name), "replaced_wrong_type": replaced, "scalars": entry["Scalars"], "vectors": entry["Vectors"], "cross_section_texture": section_path})

    report = {
        "status": "ok",
        "overwrite": overwrite,
        "default_texture": texture_path,
        "noise_texture": NOISE_VOLUME_PATH,
        "flow_collection": FLOW_COLLECTION_PATH,
        "cross_section_textures": texture_records,
        "cross_section_v_convention": "v = saturate(h / VolCrossSectionHeightCm), 이미지 위 행(v=0) = 바닥 (cross-plan P-04)",
        "master": master_summary,
        "instances": instances,
        "hlsl": {"paths": ["/".join(SHADER_DIRECTORY + (COMMON_HLSL_NAME,)), "/".join(SHADER_DIRECTORY + (BODY_HLSL_NAME,))], "sha256": hashlib.sha256(code.encode("utf-8")).hexdigest()},
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
