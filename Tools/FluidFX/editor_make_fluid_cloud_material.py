"""실행 환경: TDGame 언리얼 에디터 안에서 FluidFX 구름 마스터 M_TD_FluidCloudLayers(MD_VOLUME, 고정 슬롯 4개 = Custom 노드 4개 TDFluidCloudCommon+TDFluidCloudSlot, 합성 Custom 1개 TDFluidCloudCombine, 앞단 Substrate 안개·구름 BSDF 1순위, VolumetricAdvancedMaterialOutput·CloudSampleAttribute Altitude), MI 18, 무대 지형 M_TD_FluidCloudStageTerrain·MI_TD_FluidCloudStageTerrain, 자체 지형 격자 SM_TD_FluidCloudTerrainGrid256, 텍스처 8장을 만든다. 먼저 make_fluid_cloud_textures.py 와 editor_make_fluid_volume_material.py(T_TD_FluidNoiseVolume)가 끝나 있어야 하고, 모양 정본에 look-fragment-fx11r.json 이 병합되어 있어야 한다.
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_cloud_material.py (PowerShell, L-repo-01. 기존 에셋을 다시 지을 때는 Saved/FluidFX/fluid_cloud_material_request.json 에 {"overwrite": true})
출력: /Game/FluidFX/Materials/M_TD_FluidCloudLayers·MI_TD_FluidCloud_*(18)·M_TD_FluidCloudStageTerrain·MI_TD_FluidCloudStageTerrain, /Game/FluidFX/Textures/T_TD_FluidCloud*(8), /Game/FluidFX/Meshes/SM_TD_FluidCloudTerrainGrid256, Saved/FluidFX/fluid_cloud_material.json(앞단 경로 substrate|legacy, 통계, HLSL sha256, 모양 sha256, 오류 수)
상태: 미검증(2026-10-26 FX-11r 작성: py_compile·가짜 unreal·check_fluid_cloud_hlsl.py 만, 에디터 실행 전). 단면은 FX-9 규약(맨 위 행 = 바닥, refute M5). 지형 격자는 자체 메시(경계 위 260000 cm, refute M6·P-03), TerrainFollow > 0 MI 는 지형 MI 와 발자국·높이 범위가 같아야 한다(M4, 무대 스크립트가 단언). Albedo·EmissiveColor 는 벡터(M3). MI 텍스처 연결은 set 반환값 대신 다시 읽어 판정한다(L-editor-66). 볼륨 도메인 PS 명령 0 은 실패로 보지 않고 check_fluid_cloud_hlsl.py dxc 명령 수가 대체 지표다(L-editor-65).
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
MESH_DIR = f"{FLUID_ROOT}/Meshes"
MASTER_NAME = "M_TD_FluidCloudLayers"
MASTER_PATH = f"{MATERIAL_DIR}/{MASTER_NAME}"
TERRAIN_MASTER_NAME = "M_TD_FluidCloudStageTerrain"
TERRAIN_MASTER_PATH = f"{MATERIAL_DIR}/{TERRAIN_MASTER_NAME}"
TERRAIN_INSTANCE_NAME = "MI_TD_FluidCloudStageTerrain"
NOISE_VOLUME_PATH = f"{TEXTURE_DIR}/T_TD_FluidNoiseVolume"
GRID_MESH_NAME = "SM_TD_FluidCloudTerrainGrid256"
GRID_MESH_PATH = f"{MESH_DIR}/{GRID_MESH_NAME}"
GRID_MESH_SIZE_CM = 100.0
GRID_MESH_QUADS = 256
GRID_BOUNDS_EXTENSION_UP_CM = 260000.0
GRID_BOUNDS_EXTENSION_DOWN_CM = 10.0
GRID_DEFAULT_MATERIAL_PATH = "/Engine/EngineMaterials/WorldGridMaterial"
SHADER_DIRECTORY = ("Tools", "FluidFX", "Shaders")
COMMON_HLSL_NAME = "TDFluidCloudCommon.hlsl"
SLOT_HLSL_NAME = "TDFluidCloudSlot.hlsl"
COMBINE_HLSL_NAME = "TDFluidCloudCombine.hlsl"
TEXTURE_SOURCE_DIRECTORY = ("Saved", "FluidFX", "cloud_textures")
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_cloud_material_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_cloud_material.json")
MATERIAL_LOOK_MODULE_RELATIVE_PATH = ("Tools", "FluidFX", "fluid_material_look.py")
PARAMETER_GROUP = "TDFluidCloud"
MACRO_PREFIXES = ("TD_CLOUD_",)
SLOT_COUNT = 4
MULTI_SCATTERING_OCTAVE_COUNT = 2

TEXTURE_SPECS = (
    {"name": "T_TD_FluidCloudSection_Mushroom", "kind": "data8"},
    {"name": "T_TD_FluidCloudSection_VolcanoPlume", "kind": "data8"},
    {"name": "T_TD_FluidCloudSection_Tornado", "kind": "data8"},
    {"name": "T_TD_FluidCloudSection_SandWall", "kind": "data8"},
    {"name": "T_TD_FluidCloudCoverage_Cyclone", "kind": "data8"},
    {"name": "T_TD_FluidCloudStageHeight", "kind": "height16"},
    {"name": "T_TD_FluidCloudBlack", "kind": "data8"},
    {"name": "T_TD_FluidCloudWhite", "kind": "data8"},
)
SLOT_SCALAR_DEFAULTS = (
    ("Source", 0.0), ("Shape", 0.0), ("Pattern", 0.0), ("BottomKm", 1.5), ("ThicknessKm", 3.0), ("TerrainFollow", 0.0),
    ("EdgeMaskWidthRatio", 0.25), ("EdgeMaskPower", 0.2), ("CoverageScale", 1.0), ("CoverageThreshold", 0.02),
    ("PatternCoverage", 0.5), ("PatternScaleKm", 5.0), ("LobeDepthRatio", 0.0), ("TopExponent", 1.0),
    ("BottomFadeRatio", 0.1), ("TopFadeRatio", 0.2), ("ExtinctionPerM", 0.02), ("EmissiveIntensity", 0.0),
    ("EmissiveExponent", 2.0), ("DetailNoiseScaleKm", 0.5), ("DetailErosion", 0.4), ("EdgeErosion", 0.5),
    ("RadialUExponent", 1.0), ("SectionDepthKm", 2.0), ("SectionYawDegrees", 0.0),
)
SLOT_VECTOR_DEFAULTS = (
    ("FootprintCenterKm", (0.0, 0.0, 0.0, 0.0)), ("FootprintSizeKm", (20.0, 20.0, 0.0, 0.0)),
    ("Albedo", (1.0, 1.0, 1.0, 0.0)), ("EmissiveColor", (1.0, 0.25, 0.05, 0.0)), ("WindKmPerSec", (0.01, 0.0, 0.0, 0.0)),
)
SLOT_TEXTURE_DEFAULTS = (("CoverageTexture", "T_TD_FluidCloudBlack"), ("SectionTexture", "T_TD_FluidCloudWhite"))
GLOBAL_SCALAR_DEFAULTS = (
    ("CloudPhaseG", 0.5), ("CloudPhaseG2", -0.2), ("CloudPhaseBlend", 0.5),
    ("CloudMultiScatteringContribution", 0.5), ("CloudMultiScatteringOcclusion", 0.5), ("CloudMultiScatteringEccentricity", 0.5),
    ("CloudCameraFadeStartKm", 0.0), ("CloudCameraFadeEndKm", 0.0),
)
GLOBAL_VECTOR_DEFAULTS = (("CloudTerrainFootprintKm", (-20.0, -20.0, 40.0, 40.0)), ("CloudTerrainHeightRangeKm", (0.0, 0.0, 0.0, 0.0)))
GLOBAL_TEXTURE_DEFAULTS = (("CloudTerrainHeightTexture", "T_TD_FluidCloudBlack"), ("CloudNoiseVolumeTexture", "noise"))
VECTOR4_GLOBAL_NAMES = ("CloudTerrainFootprintKm",)
VAMO_INPUT_SOURCES = (
    ("PhaseG", "CloudPhaseG"), ("PhaseG2", "CloudPhaseG2"), ("PhaseBlend", "CloudPhaseBlend"),
    ("MultiScatteringContribution", "CloudMultiScatteringContribution"),
    ("MultiScatteringOcclusion", "CloudMultiScatteringOcclusion"),
    ("MultiScatteringEccentricity", "CloudMultiScatteringEccentricity"),
)
SLOT_BUILTIN_INPUTS = ("CloudWorldPosition", "CloudCameraPosition", "CloudAltitudeCm", "ElapsedSeconds")
SLOT_TEXTURE_INPUTS = {"SlotCoverageTexture": "CoverageTexture", "SlotSectionTexture": "SectionTexture"}
SLOT_GLOBAL_INPUTS = ("TerrainHeightTexture", "NoiseVolumeTexture", "CloudTerrainFootprintKm", "CloudTerrainHeightRangeKm", "CloudCameraFadeStartKm", "CloudCameraFadeEndKm")
SLOT_GLOBAL_SOURCES = {"TerrainHeightTexture": "CloudTerrainHeightTexture", "NoiseVolumeTexture": "CloudNoiseVolumeTexture"}
SLOT_INPUT_NAMES = SLOT_BUILTIN_INPUTS + tuple(SLOT_TEXTURE_INPUTS) + SLOT_GLOBAL_INPUTS + tuple(f"Slot{name}" for name, _ in SLOT_SCALAR_DEFAULTS) + tuple(f"Slot{name}" for name, _ in SLOT_VECTOR_DEFAULTS)
SLOT_OUTPUTS = (("SlotScatterAlbedo", "CMOT_FLOAT3"), ("SlotEmissive", "CMOT_FLOAT3"))
COMBINE_INPUT_NAMES = tuple(f"Slot{slot}{suffix}" for slot in range(SLOT_COUNT) for suffix in ("Main", "ScatterAlbedo", "Emissive"))
COMBINE_OUTPUTS = (("CloudAlbedo", "CMOT_FLOAT3"), ("CloudEmissive", "CMOT_FLOAT3"))
SAMPLER_NAMES = ("SlotCoverageTextureSampler", "SlotSectionTextureSampler", "TerrainHeightTextureSampler", "NoiseVolumeTextureSampler")
ENGINE_SYMBOLS = ("Texture2DSampleLevel", "Texture3DSampleLevel")
TERRAIN_INPUT_NAMES = ("TerrainWorldPosition", "TerrainHeightTexture", "TerrainFootprintKm", "TerrainHeightRangeKm")
TERRAIN_BODY = (
    "float terrainHeightCm = TD_CLOUD_TERRAIN_HEIGHT_CM(TerrainWorldPosition.xy, TerrainFootprintKm, TerrainHeightRangeKm);\n"
    "float terrainSpanCm = max((TerrainHeightRangeKm.y - TerrainHeightRangeKm.x) * TD_CLOUD_KM, 1.0);\n"
    "return float4(0.0, 0.0, terrainHeightCm, saturate((terrainHeightCm - TerrainHeightRangeKm.x * TD_CLOUD_KM) / terrainSpanCm));\n"
)
TERRAIN_VECTOR_DEFAULTS = (
    ("StageTerrainFootprintKm", (0.0, 0.0, 0.0, 0.0)), ("StageTerrainHeightRangeKm", (0.0, 0.0, 0.0, 0.0)),
    ("StageTerrainLowColor", (0.2, 0.18, 0.16, 1.0)), ("StageTerrainHighColor", (0.42, 0.4, 0.38, 1.0)),
)
TERRAIN_SCALAR_DEFAULTS = (("StageTerrainRoughness", 0.9),)
TERRAIN_TEXTURE_NAME = "StageTerrainHeightTexture"
INSTANCE_NAMES = (
    "MI_TD_FluidCloud_Pillars", "MI_TD_FluidCloud_Vortex", "MI_TD_FluidCloud_Impulse", "MI_TD_FluidCloud_Aurora",
    "MI_TD_FluidCloud_Titan", "MI_TD_FluidCloud_Paint", "MI_TD_FluidCloud_Volcano", "MI_TD_FluidCloud_VolcanoSky",
    "MI_TD_FluidCloud_Mushroom", "MI_TD_FluidCloud_MushroomShield", "MI_TD_FluidCloud_SandWall", "MI_TD_FluidCloud_Nimbus",
    "MI_TD_FluidCloud_Stratus", "MI_TD_FluidCloud_Mammatus", "MI_TD_FluidCloud_Tornado", "MI_TD_FluidCloud_Cyclone",
    "MI_TD_FluidCloud_SkyOnly", "MI_TD_FluidCloud_TestSlots",
)
STAGE_TEXTURE_OVERRIDES = {"CloudTerrainHeightTexture": "T_TD_FluidCloudStageHeight"}
INSTANCE_TEXTURE_OVERRIDES = {
    "MI_TD_FluidCloud_Volcano": {"CloudSlot1SectionTexture": "T_TD_FluidCloudSection_VolcanoPlume"},
    "MI_TD_FluidCloud_VolcanoSky": {"CloudSlot1SectionTexture": "T_TD_FluidCloudSection_VolcanoPlume"},
    "MI_TD_FluidCloud_Mushroom": {"CloudSlot1SectionTexture": "T_TD_FluidCloudSection_Mushroom"},
    "MI_TD_FluidCloud_MushroomShield": {"CloudSlot1SectionTexture": "T_TD_FluidCloudSection_Mushroom"},
    "MI_TD_FluidCloud_SandWall": {"CloudSlot1SectionTexture": "T_TD_FluidCloudSection_SandWall"},
    "MI_TD_FluidCloud_Tornado": {"CloudSlot1SectionTexture": "T_TD_FluidCloudSection_Tornado"},
    "MI_TD_FluidCloud_Cyclone": {"CloudSlot1CoverageTexture": "T_TD_FluidCloudCoverage_Cyclone"},
}
CPP_RUNTIME_SUFFIXES = ("CoverageTexture", "SectionTexture")
SUBSTRATE_PIN_CANDIDATES = {"albedo": ("Albedo",), "extinction": ("Extinction",), "emissive": ("EmissiveColor", "Emissive Color", "Emissive")}
REQUIRED_UNREAL_NAMES = (
    "EditorAssetLibrary.load_asset", "EditorAssetLibrary.save_loaded_asset", "EditorAssetLibrary.does_asset_exist", "EditorAssetLibrary.delete_asset",
    "EditorLoadingAndSavingUtils.get_dirty_map_packages", "EditorLoadingAndSavingUtils.get_dirty_content_packages",
    "SystemLibrary.get_project_directory", "AssetImportTask",
    "MaterialEditingLibrary.create_material_expression", "MaterialEditingLibrary.connect_material_expressions",
    "MaterialEditingLibrary.connect_material_property", "MaterialEditingLibrary.delete_all_material_expressions",
    "MaterialEditingLibrary.delete_material_expression", "MaterialEditingLibrary.recompile_material",
    "MaterialEditingLibrary.get_vector_parameter_names", "MaterialEditingLibrary.get_scalar_parameter_names",
    "MaterialEditingLibrary.get_texture_parameter_names", "MaterialEditingLibrary.set_material_instance_texture_parameter_value",
    "MaterialEditingLibrary.get_material_instance_texture_parameter_value",
    "Material", "MaterialFactoryNew", "MaterialInstanceConstant", "MaterialInstanceConstantFactoryNew",
    "MaterialExpressionCustom", "CustomInput", "CustomOutput", "CustomMaterialOutputType.CMOT_FLOAT4", "CustomMaterialOutputType.CMOT_FLOAT3",
    "MaterialExpressionWorldPosition", "MaterialExpressionCameraPositionWS", "MaterialExpressionCloudSampleAttribute", "MaterialExpressionTime",
    "MaterialExpressionTextureObjectParameter", "MaterialExpressionVectorParameter", "MaterialExpressionScalarParameter",
    "MaterialExpressionComponentMask", "MaterialExpressionLinearInterpolate", "MaterialExpressionVolumetricAdvancedMaterialOutput",
    "MaterialExpressionSubstrateVolumetricFogCloudBSDF",
    "MaterialDomain.MD_VOLUME", "MaterialDomain.MD_SURFACE", "BlendMode.BLEND_ADDITIVE", "BlendMode.BLEND_OPAQUE",
    "MaterialProperty.MP_FRONT_MATERIAL", "MaterialProperty.MP_BASE_COLOR", "MaterialProperty.MP_SUBSURFACE_COLOR",
    "MaterialProperty.MP_EMISSIVE_COLOR", "MaterialProperty.MP_ROUGHNESS", "MaterialProperty.MP_WORLD_POSITION_OFFSET",
    "MaterialSamplerType.SAMPLERTYPE_COLOR", "MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR",
    "TextureCompressionSettings.TC_DEFAULT", "TextureCompressionSettings.TC_HALF_FLOAT", "TextureAddress.TA_CLAMP",
    "VolumeTexture", "StaticMesh", "StaticMeshEditorSubsystem", "TDFluidWaterMeshEditorLibrary.create_grid_static_mesh",
)

IDENTIFIER_PATTERN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
DEFINE_PATTERN = re.compile(r"^[ \t]*#define[ \t]+([A-Za-z_][A-Za-z0-9_]*)", re.MULTILINE)


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


def asset_path(name):
    return f"{MATERIAL_DIR}/{name}"


def slot_parameter(slot, name):
    return f"CloudSlot{slot}{name}"


def master_scalar_defaults():
    slots = [(slot_parameter(slot, name), value) for slot in range(SLOT_COUNT) for name, value in SLOT_SCALAR_DEFAULTS]
    return tuple(slots) + GLOBAL_SCALAR_DEFAULTS


def master_vector_defaults():
    slots = [(slot_parameter(slot, name), rgba) for slot in range(SLOT_COUNT) for name, rgba in SLOT_VECTOR_DEFAULTS]
    return tuple(slots) + GLOBAL_VECTOR_DEFAULTS


def master_texture_names():
    slots = [slot_parameter(slot, suffix) for slot in range(SLOT_COUNT) for suffix, _ in SLOT_TEXTURE_DEFAULTS]
    return tuple(slots) + tuple(name for name, _ in GLOBAL_TEXTURE_DEFAULTS)


def read_text_file(path):
    if not os.path.isfile(path):
        raise RuntimeError(f"HLSL 파일 없음: {path}")
    with open(path, encoding="utf-8") as text_file:
        return text_file.read().replace("\r\n", "\n").strip("\n") + "\n"


def read_shader_sources(project_root):
    shader_root = os.path.join(project_root, *SHADER_DIRECTORY)
    return {
        "common": read_text_file(os.path.join(shader_root, COMMON_HLSL_NAME)),
        "slot": read_text_file(os.path.join(shader_root, SLOT_HLSL_NAME)),
        "combine": read_text_file(os.path.join(shader_root, COMBINE_HLSL_NAME)),
    }


def assemble_node_code(common_code, node_code):
    body = (common_code + "\n" + node_code) if common_code else node_code
    macro_names = sorted(set(DEFINE_PATTERN.findall(body)))
    undefine_block = "".join(f"#undef {name}\n" for name in macro_names)
    return undefine_block + body + "\n" + undefine_block


def assemble_all_node_codes(sources):
    return {
        "slot": assemble_node_code(sources["common"], sources["slot"]),
        "combine": assemble_node_code("", sources["combine"]),
        "terrain": assemble_node_code(sources["common"], TERRAIN_BODY),
    }


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


def validate_node_code(label, code, input_names, output_names, samplers):
    problems = []
    identifiers = set(IDENTIFIER_PATTERN.findall(code))
    missing = [name for name in tuple(input_names) + tuple(output_names) + tuple(samplers) if name not in identifiers]
    if missing:
        problems.append(f"{label}: HLSL이 쓰지 않는 입력·출력·샘플러 " + ", ".join(missing))
    leaked = [name for name in DEFINE_PATTERN.findall(code) if not name.startswith(MACRO_PREFIXES)]
    if leaked:
        problems.append(f"{label}: 접두 밖 매크로 " + ", ".join(leaked))
    bracket_problem = find_unbalanced_brackets(code)
    if bracket_problem:
        problems.append(f"{label}: {bracket_problem}")
    if "return" not in identifiers:
        problems.append(f"{label}: return 없음")
    if "//" in code or "/*" in code:
        problems.append(f"{label}: 주석이 있음")
    return problems


def validate_all_codes(codes):
    problems = validate_node_code("슬롯", codes["slot"], SLOT_INPUT_NAMES, [name for name, _ in SLOT_OUTPUTS], SAMPLER_NAMES)
    problems += validate_node_code("합성", codes["combine"], COMBINE_INPUT_NAMES, [name for name, _ in COMBINE_OUTPUTS], ())
    problems += validate_node_code("지형", codes["terrain"], TERRAIN_INPUT_NAMES, (), ("TerrainHeightTextureSampler",))
    for engine_symbol in ENGINE_SYMBOLS:
        if engine_symbol not in codes["slot"]:
            problems.append(f"슬롯: 엔진 심볼 없음 {engine_symbol}")
    return problems


def find_instance_override_problems(entries):
    problems = []
    overridable = {name for name, _ in master_scalar_defaults()} | {name for name, _ in master_vector_defaults()}
    runtime_names = {slot_parameter(slot, suffix) for slot in range(SLOT_COUNT) for suffix in CPP_RUNTIME_SUFFIXES}
    for name in INSTANCE_NAMES:
        entry = entries.get(asset_path(name))
        if entry is None:
            problems.append(f"{name}: 모양 정본에 없음")
            continue
        if entry.get("Parent") != MASTER_PATH:
            problems.append(f"{name}: 부모 {entry.get('Parent')} ≠ {MASTER_PATH}")
        names = set(entry.get("Scalars", {})) | set(entry.get("Vectors", {})) | set(entry.get("Switches", {}))
        unknown = sorted(names - overridable)
        if unknown:
            problems.append(f"{name}: 마스터에 없는 이름 " + ", ".join(unknown))
        runtime = sorted(names & runtime_names)
        if runtime:
            problems.append(f"{name}: C++ 런타임 이름 덮어씀 " + ", ".join(runtime))
        scalar_vectors = sorted(set(entry.get("Scalars", {})) & {name for name, _ in master_vector_defaults()})
        if scalar_vectors:
            problems.append(f"{name}: 벡터 이름을 스칼라로 씀 " + ", ".join(scalar_vectors))
    terrain = entries.get(asset_path(TERRAIN_INSTANCE_NAME))
    if terrain is None:
        problems.append(f"{TERRAIN_INSTANCE_NAME}: 모양 정본에 없음")
    elif terrain.get("Parent") != TERRAIN_MASTER_PATH:
        problems.append(f"{TERRAIN_INSTANCE_NAME}: 부모 {terrain.get('Parent')} ≠ {TERRAIN_MASTER_PATH}")
    return problems


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
    paths = [MASTER_PATH, TERRAIN_MASTER_PATH, asset_path(TERRAIN_INSTANCE_NAME), GRID_MESH_PATH]
    paths += [asset_path(name) for name in INSTANCE_NAMES]
    paths += [f"{TEXTURE_DIR}/{spec['name']}" for spec in TEXTURE_SPECS]
    return paths


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


def has_editor_property(owner, property_name):
    try:
        owner.get_editor_property(property_name)
    except Exception:
        return False
    return True


def choose_sampler_type(texture):
    try:
        compression = texture.get_editor_property("compression_settings")
        is_srgb = bool(texture.get_editor_property("srgb"))
    except Exception:
        return None
    if compression == unreal.TextureCompressionSettings.TC_HALF_FLOAT:
        return unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
    if compression != unreal.TextureCompressionSettings.TC_DEFAULT:
        return None
    return unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if is_srgb else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR


def import_texture(spec, overwrite):
    source_path = project_path(*TEXTURE_SOURCE_DIRECTORY, spec["name"] + ".png")
    if not os.path.isfile(source_path):
        raise RuntimeError(f"텍스처 원본 없음(python Tools/FluidFX/make_fluid_cloud_textures.py 먼저): {source_path}")
    path = f"{TEXTURE_DIR}/{spec['name']}"
    if unreal.EditorAssetLibrary.does_asset_exist(path) and not overwrite:
        raise RuntimeError(f"기존 에셋 보존: {path}")
    ensure_directory(TEXTURE_DIR)
    task = unreal.AssetImportTask()
    for name, value in (("filename", source_path), ("destination_path", TEXTURE_DIR), ("destination_name", spec["name"]), ("replace_existing", True), ("automated", True), ("save", False)):
        task.set_editor_property(name, value)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.EditorAssetLibrary.load_asset(path)
    if texture is None:
        raise RuntimeError(f"텍스처 가져오기 실패: {path}")
    compression = unreal.TextureCompressionSettings.TC_HALF_FLOAT if spec["kind"] == "height16" else unreal.TextureCompressionSettings.TC_DEFAULT
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("compression_settings", compression)
    texture.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
    texture.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    save_asset(texture, path)
    sampler_type = choose_sampler_type(texture)
    if sampler_type != unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR:
        raise RuntimeError(f"텍스처 표본기 형식이 선형 색이 아님(데이터 텍스처): {path} {sampler_type}")
    with open(source_path, "rb") as source_file:
        source_sha256 = hashlib.sha256(source_file.read()).hexdigest()
    return texture, sampler_type, {"asset": path, "source_sha256": source_sha256, "compression": str(compression), "sampler_type": str(sampler_type), "address": "clamp"}


def load_noise_volume():
    texture = unreal.EditorAssetLibrary.load_asset(NOISE_VOLUME_PATH)
    if not isinstance(texture, unreal.VolumeTexture):
        raise RuntimeError(f"노이즈 볼륨 텍스처 없음(editor_make_fluid_volume_material.py 먼저): {NOISE_VOLUME_PATH}")
    return texture, choose_sampler_type(texture) or unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR


def create_grid_mesh(overwrite):
    if unreal.EditorAssetLibrary.does_asset_exist(GRID_MESH_PATH):
        if not overwrite:
            raise RuntimeError(f"기존 에셋 보존: {GRID_MESH_PATH}")
        if not unreal.EditorAssetLibrary.delete_asset(GRID_MESH_PATH):
            raise RuntimeError(f"기존 구름 지형 격자 삭제 실패(LV_TDFluidCloudStage 를 닫고 다시 실행): {GRID_MESH_PATH}")
    ensure_directory(MESH_DIR)
    mesh = unreal.TDFluidWaterMeshEditorLibrary.create_grid_static_mesh(MESH_DIR, GRID_MESH_NAME, GRID_MESH_SIZE_CM, GRID_MESH_QUADS, unreal.load_asset(GRID_DEFAULT_MATERIAL_PATH))
    if not isinstance(mesh, unreal.StaticMesh):
        raise RuntimeError(f"구름 지형 격자 생성 실패: {GRID_MESH_PATH}")
    vertex_total = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).get_number_verts(mesh, 0)
    if vertex_total != (GRID_MESH_QUADS + 1) ** 2:
        raise RuntimeError(f"구름 지형 격자 정점 수 불일치: {vertex_total}")
    extension = {}
    for property_name, vector in (("positive_bounds_extension", (0.0, 0.0, GRID_BOUNDS_EXTENSION_UP_CM)), ("negative_bounds_extension", (0.0, 0.0, GRID_BOUNDS_EXTENSION_DOWN_CM))):
        if not has_editor_property(mesh, property_name):
            raise RuntimeError(f"StaticMesh 경계 확장 속성 없음: {property_name}")
        mesh.set_editor_property(property_name, unreal.Vector(*vector))
        extension[property_name] = list(vector)
    save_asset(mesh, GRID_MESH_PATH)
    return {"asset": GRID_MESH_PATH, "size_cm": GRID_MESH_SIZE_CM, "quads": GRID_MESH_QUADS, "render_vertices": vertex_total, "bounds_extension": extension}


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


def try_link_first_pin(source, target, candidates, source_output=""):
    for candidate in candidates:
        if unreal.MaterialEditingLibrary.connect_material_expressions(source, source_output, target, candidate):
            return candidate
    return ""


def connect_material_output(expression, material_property, source_output=""):
    if not unreal.MaterialEditingLibrary.connect_material_property(expression, source_output, material_property):
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


def prepare_material(name, path, overwrite):
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        if not overwrite:
            raise RuntimeError(f"기존 에셋 보존: {path}")
        material = unreal.EditorAssetLibrary.load_asset(path)
        if not isinstance(material, unreal.Material):
            raise RuntimeError(f"기존 에셋 형식 불일치: {path}")
        unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
        return material, False
    ensure_directory(MATERIAL_DIR)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError(f"재질 생성 실패: {path}")
    return material, True


def build_parameter_nodes(material, textures):
    parameters = {}
    rows = iter(range(1000))

    def place():
        return -2600, -6000 + next(rows) * 90

    for name, (texture, sampler_type) in textures.items():
        x, y = place()
        node = add_node(material, unreal.MaterialExpressionTextureObjectParameter, x, y, parameter_name=name, texture=texture, sampler_type=sampler_type)
        assign_parameter_group(node)
        parameters[name] = (node, "")
    for name, rgba in master_vector_defaults():
        x, y = place()
        node = add_node(material, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=unreal.LinearColor(*rgba))
        assign_parameter_group(node)
        parameters[name] = (node, "RGBA" if name in VECTOR4_GLOBAL_NAMES else "RGB")
    for name, value in master_scalar_defaults():
        x, y = place()
        node = add_node(material, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)
        assign_parameter_group(node)
        parameters[name] = (node, "")
    return parameters


def build_builtin_sources(material):
    world = truncate_if_available(material, add_node(material, unreal.MaterialExpressionWorldPosition, -3000, -6600), -2800, -6600)
    camera = truncate_if_available(material, add_node(material, unreal.MaterialExpressionCameraPositionWS, -3000, -6500), -2800, -6500)
    altitude = add_node(material, unreal.MaterialExpressionCloudSampleAttribute, -2800, -6400)
    elapsed = add_node(material, unreal.MaterialExpressionTime, -2800, -6300)
    return {"CloudWorldPosition": (world, ""), "CloudCameraPosition": (camera, ""), "CloudAltitudeCm": (altitude, "Altitude"), "ElapsedSeconds": (elapsed, "")}


def resolve_slot_source(slot, input_name, parameters, builtins):
    if input_name in builtins:
        return builtins[input_name]
    if input_name in SLOT_TEXTURE_INPUTS:
        return parameters[slot_parameter(slot, SLOT_TEXTURE_INPUTS[input_name])]
    if input_name in SLOT_GLOBAL_SOURCES:
        return parameters[SLOT_GLOBAL_SOURCES[input_name]]
    if input_name in parameters:
        return parameters[input_name]
    return parameters[slot_parameter(slot, input_name[len("Slot"):])]


def make_custom_node(material, description, code, input_names, output_specs, x, y):
    custom = add_node(material, unreal.MaterialExpressionCustom, x, y)
    custom.set_editor_property("description", description)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    inputs = []
    for name in input_names:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        inputs.append(custom_input)
    custom.set_editor_property("inputs", inputs)
    outputs = []
    for name, kind in output_specs:
        custom_output = unreal.CustomOutput()
        custom_output.set_editor_property("output_name", name)
        custom_output.set_editor_property("output_type", getattr(unreal.CustomMaterialOutputType, kind))
        outputs.append(custom_output)
    custom.set_editor_property("additional_outputs", outputs)
    custom.set_editor_property("code", code)
    stored = [str(item.get_editor_property("input_name")) for item in custom.get_editor_property("inputs")]
    if stored != list(input_names):
        raise RuntimeError(f"{description} Custom 입력 이름 설정 확인 실패: " + ", ".join(stored))
    return custom


def link_altitude(source, custom, input_name):
    if unreal.MaterialEditingLibrary.connect_material_expressions(source, "Altitude", custom, input_name):
        return "Altitude"
    link(source, custom, input_name, "")
    return "first"


def build_slot_nodes(material, code, parameters, builtins):
    slots = []
    altitude_outputs = []
    for slot in range(SLOT_COUNT):
        custom = make_custom_node(material, f"TDFluidCloudSlot{slot}", code, SLOT_INPUT_NAMES, SLOT_OUTPUTS, -1200, -2400 + slot * 1400)
        for input_name in SLOT_INPUT_NAMES:
            source, source_output = resolve_slot_source(slot, input_name, parameters, builtins)
            if input_name == "CloudAltitudeCm":
                altitude_outputs.append(link_altitude(source, custom, input_name))
                continue
            link(source, custom, input_name, source_output)
        slots.append(custom)
    return slots, altitude_outputs


def build_combine_node(material, code, slots):
    combine = make_custom_node(material, "TDFluidCloudCombine", code, COMBINE_INPUT_NAMES, COMBINE_OUTPUTS, -400, 0)
    for slot, custom in enumerate(slots):
        link(custom, combine, f"Slot{slot}Main", "")
        link(custom, combine, f"Slot{slot}ScatterAlbedo", "SlotScatterAlbedo")
        link(custom, combine, f"Slot{slot}Emissive", "SlotEmissive")
    return combine


def build_volumetric_output(material, combine, parameters):
    conservative = add_node(material, unreal.MaterialExpressionComponentMask, -100, 400, r=False, g=False, b=False, a=True)
    link(combine, conservative)
    output = add_node(material, unreal.MaterialExpressionVolumetricAdvancedMaterialOutput, 300, 400)
    for name, value in (("multi_scattering_approximation_octave_count", MULTI_SCATTERING_OCTAVE_COUNT), ("ray_march_volume_shadow", True), ("per_sample_phase_evaluation", False)):
        output.set_editor_property(name, value)
    for input_name, parameter_name in VAMO_INPUT_SOURCES:
        source, source_output = parameters[parameter_name]
        link(source, output, input_name, source_output)
    link(conservative, output, "ConservativeDensity")
    return {"octave_count": MULTI_SCATTERING_OCTAVE_COUNT, "ray_march_volume_shadow": True, "per_sample_phase": False}


def build_front_outputs(material, combine):
    extinction = add_node(material, unreal.MaterialExpressionComponentMask, -100, -150, r=True, g=True, b=True, a=False)
    link(combine, extinction)
    bsdf = add_node(material, unreal.MaterialExpressionSubstrateVolumetricFogCloudBSDF, 300, -100)
    pins = {
        "albedo": try_link_first_pin(combine, bsdf, SUBSTRATE_PIN_CANDIDATES["albedo"], "CloudAlbedo"),
        "extinction": try_link_first_pin(extinction, bsdf, SUBSTRATE_PIN_CANDIDATES["extinction"]),
        "emissive": try_link_first_pin(combine, bsdf, SUBSTRATE_PIN_CANDIDATES["emissive"], "CloudEmissive"),
    }
    if all(pins.values()) and unreal.MaterialEditingLibrary.connect_material_property(bsdf, "", unreal.MaterialProperty.MP_FRONT_MATERIAL):
        return {"path": "substrate", "pins": pins}
    unreal.MaterialEditingLibrary.delete_material_expression(material, bsdf)
    connect_material_output(combine, unreal.MaterialProperty.MP_BASE_COLOR, "CloudAlbedo")
    connect_material_output(extinction, unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    connect_material_output(combine, unreal.MaterialProperty.MP_EMISSIVE_COLOR, "CloudEmissive")
    return {"path": "legacy", "pins": {"albedo": "MP_BASE_COLOR", "extinction": "MP_SUBSURFACE_COLOR", "emissive": "MP_EMISSIVE_COLOR"}, "substrate_pins_tried": pins}


def verify_parameters(material, vector_names, scalar_names, texture_names, label):
    editing = unreal.MaterialEditingLibrary
    found = {
        "vector": {str(name) for name in editing.get_vector_parameter_names(material)},
        "scalar": {str(name) for name in editing.get_scalar_parameter_names(material)},
        "texture": {str(name) for name in editing.get_texture_parameter_names(material)},
    }
    expected = {"vector": vector_names, "scalar": scalar_names, "texture": texture_names}
    missing = [f"{kind}:{name}" for kind, names in expected.items() for name in names if name not in found[kind]]
    if missing:
        raise RuntimeError(f"{label} 파라미터 없음: " + ", ".join(missing[:20]))


def read_material_statistics(material):
    try:
        statistics = unreal.MaterialEditingLibrary.get_statistics(material)
        return {name: statistics.get_editor_property(name) for name in ("num_vertex_shader_instructions", "num_pixel_shader_instructions", "num_samplers")}
    except Exception:
        return {}


def recompile_or_fail(material, label):
    errors = [str(item) for item in unreal.MaterialEditingLibrary.recompile_material(material) or []]
    if errors:
        raise RuntimeError(f"{label} 컴파일 실패(Saved/Logs/TDGame.log 의 LogMaterial 확인): " + " | ".join(errors))
    return len(errors)


def create_cloud_master(codes, textures, overwrite):
    material, was_created = prepare_material(MASTER_NAME, MASTER_PATH, overwrite)
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_VOLUME)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    parameters = build_parameter_nodes(material, textures)
    builtins = build_builtin_sources(material)
    slots, altitude_outputs = build_slot_nodes(material, codes["slot"], parameters, builtins)
    combine = build_combine_node(material, codes["combine"], slots)
    front = build_front_outputs(material, combine)
    volumetric = build_volumetric_output(material, combine, parameters)
    error_count = recompile_or_fail(material, MASTER_NAME)
    verify_parameters(material, [name for name, _ in master_vector_defaults()], [name for name, _ in master_scalar_defaults()], list(master_texture_names()), MASTER_NAME)
    save_asset(material, MASTER_PATH)
    return material, {
        "asset": MASTER_PATH, "created": was_created, "front": front, "volumetric_output": volumetric,
        "altitude_output": altitude_outputs, "slot_count": SLOT_COUNT, "slot_inputs": len(SLOT_INPUT_NAMES),
        "statistics": read_material_statistics(material), "compile_error_count": error_count,
        "statistics_note": "볼륨 도메인은 PS 명령 0 일 수 있음 → check_fluid_cloud_hlsl.py dxc 명령 수가 대체 지표",
    }


def create_terrain_master(code, height_texture, overwrite):
    material, was_created = prepare_material(TERRAIN_MASTER_NAME, TERRAIN_MASTER_PATH, overwrite)
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    texture, sampler_type = height_texture
    texture_node = add_node(material, unreal.MaterialExpressionTextureObjectParameter, -1400, -200, parameter_name=TERRAIN_TEXTURE_NAME, texture=texture, sampler_type=sampler_type)
    assign_parameter_group(texture_node)
    sources = {"TerrainHeightTexture": (texture_node, "")}
    vectors = {}
    for index, (name, rgba) in enumerate(TERRAIN_VECTOR_DEFAULTS):
        node = add_node(material, unreal.MaterialExpressionVectorParameter, -1400, index * 110, parameter_name=name, default_value=unreal.LinearColor(*rgba))
        assign_parameter_group(node)
        vectors[name] = node
    sources["TerrainFootprintKm"] = (vectors["StageTerrainFootprintKm"], "RGBA")
    sources["TerrainHeightRangeKm"] = (vectors["StageTerrainHeightRangeKm"], "RGB")
    roughness = add_node(material, unreal.MaterialExpressionScalarParameter, -1400, 600, parameter_name="StageTerrainRoughness", default_value=TERRAIN_SCALAR_DEFAULTS[0][1])
    assign_parameter_group(roughness)
    nodes = []
    for index, description in enumerate(("TDFluidCloudTerrainOffset", "TDFluidCloudTerrainShade")):
        world = truncate_if_available(material, add_node(material, unreal.MaterialExpressionWorldPosition, -1200, -600 - index * 120), -1000, -600 - index * 120)
        custom = make_custom_node(material, description, code, TERRAIN_INPUT_NAMES, (), -600, -300 + index * 500)
        link(world, custom, "TerrainWorldPosition")
        for input_name in TERRAIN_INPUT_NAMES[1:]:
            source, source_output = sources[input_name]
            link(source, custom, input_name, source_output)
        nodes.append(custom)
    offset_mask = add_node(material, unreal.MaterialExpressionComponentMask, -200, -300, r=True, g=True, b=True, a=False)
    link(nodes[0], offset_mask)
    shade_mask = add_node(material, unreal.MaterialExpressionComponentMask, -200, 200, r=False, g=False, b=False, a=True)
    link(nodes[1], shade_mask)
    color = add_node(material, unreal.MaterialExpressionLinearInterpolate, 0, 200)
    link(vectors["StageTerrainLowColor"], color, "A", "RGB")
    link(vectors["StageTerrainHighColor"], color, "B", "RGB")
    link(shade_mask, color, "Alpha")
    connect_material_output(offset_mask, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    connect_material_output(color, unreal.MaterialProperty.MP_BASE_COLOR)
    connect_material_output(roughness, unreal.MaterialProperty.MP_ROUGHNESS)
    error_count = recompile_or_fail(material, TERRAIN_MASTER_NAME)
    verify_parameters(material, [name for name, _ in TERRAIN_VECTOR_DEFAULTS], [name for name, _ in TERRAIN_SCALAR_DEFAULTS], [TERRAIN_TEXTURE_NAME], TERRAIN_MASTER_NAME)
    save_asset(material, TERRAIN_MASTER_PATH)
    return material, {"asset": TERRAIN_MASTER_PATH, "created": was_created, "compile_error_count": error_count, "statistics": read_material_statistics(material)}


def prepare_instance(name, overwrite):
    path = asset_path(name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        if not overwrite:
            raise RuntimeError(f"기존 에셋 보존: {path}")
        instance = unreal.EditorAssetLibrary.load_asset(path)
        if not isinstance(instance, unreal.MaterialInstanceConstant):
            raise RuntimeError(f"기존 에셋 형식 불일치: {path}")
        return instance
    instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, MATERIAL_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    if instance is None:
        raise RuntimeError(f"재질 인스턴스 생성 실패: {path}")
    return instance


def apply_texture_overrides(instance, name, overrides, textures_by_asset):
    editing = unreal.MaterialEditingLibrary
    applied = {}
    for parameter_name, texture_name in sorted(overrides.items()):
        texture = textures_by_asset[texture_name]
        editing.set_material_instance_texture_parameter_value(instance, parameter_name, texture)
        stored = editing.get_material_instance_texture_parameter_value(instance, parameter_name)
        if stored is None or stored.get_path_name() != texture.get_path_name():
            raise RuntimeError(f"{name}: 텍스처 연결 확인 실패 {parameter_name} = {texture_name}")
        applied[parameter_name] = f"{TEXTURE_DIR}/{texture_name}"
    return applied


def create_instances(look_table, master, terrain_master, textures_by_asset, overwrite):
    instances = []
    for name in INSTANCE_NAMES:
        instance = prepare_instance(name, overwrite)
        entry = look_table.apply_to_instance(unreal.MaterialEditingLibrary, unreal.LinearColor, instance, asset_path(name), master)
        overrides = dict(STAGE_TEXTURE_OVERRIDES)
        overrides.update(INSTANCE_TEXTURE_OVERRIDES.get(name, {}))
        applied = apply_texture_overrides(instance, name, overrides, textures_by_asset)
        unreal.MaterialEditingLibrary.update_material_instance(instance)
        save_asset(instance, asset_path(name))
        instances.append({"asset": asset_path(name), "scalars": entry["Scalars"], "vectors": entry["Vectors"], "textures": applied})
    terrain = prepare_instance(TERRAIN_INSTANCE_NAME, overwrite)
    entry = look_table.apply_to_instance(unreal.MaterialEditingLibrary, unreal.LinearColor, terrain, asset_path(TERRAIN_INSTANCE_NAME), terrain_master)
    applied = apply_texture_overrides(terrain, TERRAIN_INSTANCE_NAME, {TERRAIN_TEXTURE_NAME: "T_TD_FluidCloudStageHeight"}, textures_by_asset)
    unreal.MaterialEditingLibrary.update_material_instance(terrain)
    save_asset(terrain, asset_path(TERRAIN_INSTANCE_NAME))
    instances.append({"asset": asset_path(TERRAIN_INSTANCE_NAME), "vectors": entry["Vectors"], "textures": applied})
    return instances


def write_report(report):
    report_path = project_path(*REPORT_RELATIVE_PATH)
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, "w", encoding="utf-8") as report_file:
        json.dump(report, report_file, ensure_ascii=False, indent=2, default=str)
    return report_path


def main():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    missing_names = find_missing_unreal_names()
    if missing_names:
        raise RuntimeError("언리얼 Python 이름 없음: " + ", ".join(missing_names))
    if editor.get_game_world() is not None:
        raise RuntimeError("PIE를 종료한 뒤 실행하세요.")

    look_table = load_material_look_table()
    codes = assemble_all_node_codes(read_shader_sources(unreal.SystemLibrary.get_project_directory()))
    problems = validate_all_codes(codes) + find_instance_override_problems(look_table.look["Materials"])
    if problems:
        raise RuntimeError("자체 점검 실패: " + " | ".join(problems))

    overwrite = read_overwrite_request()
    existing_paths = [path for path in collect_target_asset_paths() if unreal.EditorAssetLibrary.does_asset_exist(path)]
    if existing_paths and not overwrite:
        print(json.dumps({"status": "기존 에셋이 있어 아무것도 바꾸지 않았습니다", "existing_assets": existing_paths, "overwrite_request_file": project_path(*REQUEST_RELATIVE_PATH), "overwrite_request_content": {"overwrite": True}}, ensure_ascii=False))
        raise RuntimeError("기존 에셋 보존: overwrite 요청 파일이 없어 중단합니다.")
    unsaved_names = find_unsaved_package_names(overwrite)
    if unsaved_names:
        raise RuntimeError("미저장 작업을 보존합니다: " + ", ".join(unsaved_names))

    print("단계: 텍스처·격자")
    imported = {}
    texture_reports = []
    for spec in TEXTURE_SPECS:
        texture, sampler_type, texture_report = import_texture(spec, overwrite)
        imported[spec["name"]] = (texture, sampler_type)
        texture_reports.append(texture_report)
    noise = load_noise_volume()
    grid_report = create_grid_mesh(overwrite)
    master_textures = {}
    for slot in range(SLOT_COUNT):
        for suffix, default_name in SLOT_TEXTURE_DEFAULTS:
            master_textures[slot_parameter(slot, suffix)] = imported[default_name]
    master_textures["CloudTerrainHeightTexture"] = imported["T_TD_FluidCloudBlack"]
    master_textures["CloudNoiseVolumeTexture"] = noise

    print("단계: 구름 마스터")
    master, master_summary = create_cloud_master(codes, master_textures, overwrite)
    print("단계: 지형 마스터")
    terrain_master, terrain_summary = create_terrain_master(codes["terrain"], imported["T_TD_FluidCloudBlack"], overwrite)
    print("단계: 재질 인스턴스")
    textures_by_asset = {name: texture for name, (texture, _) in imported.items()}
    instances = create_instances(look_table, master, terrain_master, textures_by_asset, overwrite)

    report = {
        "status": "ok",
        "overwrite": overwrite,
        "textures": texture_reports,
        "noise_texture": NOISE_VOLUME_PATH,
        "grid": grid_report,
        "master": master_summary,
        "terrain_master": terrain_summary,
        "instances": instances,
        "error_count": master_summary["compile_error_count"] + terrain_summary["compile_error_count"],
        "hlsl": {name: hashlib.sha256(code.encode("utf-8")).hexdigest() for name, code in codes.items()},
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
