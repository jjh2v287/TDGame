"""실행 환경: TDGame 언리얼 에디터 안에서 FluidFX 자국 층 마스터 M_TD_FluidTrackLayer(불투명 Masked·월드 공간 법선, Custom 노드 2개 = 정점 WPO TDFluidTrackOffset·화소 TDFluidTrackSurface, 공용 TDFluidTrackCommon), 지형용 MF_TD_FluidTrackSample, 창 MPC_TD_FluidTrackWindow, 격자 SM_TD_FluidTrackGridUnit256, 텍스처 2장, MI 5개(자국 3·먼지 2)를 만든다. 먼저 python Tools/FluidFX/make_fluid_track_textures.py 를 돌리고, 모양 정본에 MI 5개가 병합되어 있어야 한다.
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_track_material.py (기존 에셋을 다시 지을 때는 Saved/FluidFX/fluid_track_material_request.json 에 {"overwrite": true})
출력: /Game/FluidFX/Materials/M_TD_FluidTrackLayer·MF_TD_FluidTrackSample·MPC_TD_FluidTrackWindow·MI_TD_FluidTrack_Snow·SnowSlope·Sand·MI_TD_FluidVolume_SnowDust·SandDust, /Game/FluidFX/Textures/T_TD_FluidDuneHeight·T_TD_FluidSnowDetailN, /Game/FluidFX/Meshes/SM_TD_FluidTrackGridUnit256, Saved/FluidFX/fluid_track_material.json
상태: 미검증(2026-10-25 FX-12r 작성: py_compile·가짜 unreal·check_fluid_track_hlsl.py 만, 에디터 실행 전). 구동(MID, SimGridResolution > 0.5) = 내부 표면 평면(창 안 층 높이·잔차 POM·자기 그림자·PDO), 정적(MI) = 바닥(MPC 창 안 클립, TrackUsesWindowMask 0 이면 비탈용으로 클립 끔). 층 기준면은 셰이더 오프셋을 뺀 월드 위치(평면·바닥 메시 높이)다. MI 값은 모양 정본(apply_to_instance)만, 모래 MI 의 세부 법선 텍스처(T_TD_FluidSandRipplesN)는 에셋 연결로 넣는다(반박 S4). 사구 높이 PNG 16비트는 TC_HALF_FLOAT·선형 표본기(반박 S3). 정점 WPO 에 카메라 거리 페이드가 있고 바닥 클립이 MPC 창을 따라가므로 VSM Rigid 를 쓰지 않는다(반박 S2).
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
MASTER_NAME = "M_TD_FluidTrackLayer"
MASTER_PATH = f"{MATERIAL_DIR}/{MASTER_NAME}"
FUNCTION_NAME = "MF_TD_FluidTrackSample"
FUNCTION_PATH = f"{MATERIAL_DIR}/{FUNCTION_NAME}"
COLLECTION_NAME = "MPC_TD_FluidTrackWindow"
COLLECTION_PATH = f"{MATERIAL_DIR}/{COLLECTION_NAME}"
VOLUME_MASTER_PATH = f"{MATERIAL_DIR}/M_TD_FluidVolume"
GRID_MESH_NAME = "SM_TD_FluidTrackGridUnit256"
GRID_MESH_PATH = f"{MESH_DIR}/{GRID_MESH_NAME}"
GRID_MESH_SIZE_CM = 100.0
GRID_MESH_QUADS = 256
GRID_BOUNDS_EXTENSION_UP_CM = 80.0
GRID_BOUNDS_EXTENSION_DOWN_CM = 10.0
GRID_DEFAULT_MATERIAL_PATH = "/Engine/EngineMaterials/WorldGridMaterial"
SAND_RIPPLES_NORMAL_PATH = f"{TEXTURE_DIR}/T_TD_FluidSandRipplesN"
DEFAULT_TEXTURE_PATHS = ("/Engine/EngineResources/Black", "/Engine/EngineResources/DefaultTexture")
SHADER_DIRECTORY = ("Tools", "FluidFX", "Shaders")
COMMON_HLSL_NAME = "TDFluidTrackCommon.hlsl"
OFFSET_HLSL_NAME = "TDFluidTrackOffset.hlsl"
SURFACE_HLSL_NAME = "TDFluidTrackSurface.hlsl"
SAMPLE_HLSL_NAME = "TDFluidTrackSample.hlsl"
TEXTURE_SOURCE_DIRECTORY = ("Saved", "FluidFX", "track_textures")
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_track_material_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_track_material.json")
MATERIAL_LOOK_MODULE_RELATIVE_PATH = ("Tools", "FluidFX", "fluid_material_look.py")
PARAMETER_GROUP = "TDFluidTrack"
MACRO_PREFIX = "TD_TRACK_"

TEXTURE_SPECS = (
    {"name": "T_TD_FluidDuneHeight", "source": "T_TD_FluidDuneHeight.png", "kind": "height16"},
    {"name": "T_TD_FluidSnowDetailN", "source": "T_TD_FluidSnowDetailN.png", "kind": "normal"},
)
TEXTURE_PARAMETER_SOURCES = {
    "DensityPressureTexture": "default",
    "TrackDuneHeightTexture": "T_TD_FluidDuneHeight",
    "TrackDetailNormalTexture": "T_TD_FluidSnowDetailN",
}
TEXTURE_PARAMETER_NAMES = tuple(TEXTURE_PARAMETER_SOURCES)
INSTANCE_TEXTURE_OVERRIDES = {"MI_TD_FluidTrack_Sand": {"TrackDetailNormalTexture": SAND_RIPPLES_NORMAL_PATH}}
COLLECTION_VECTOR_DEFAULTS = (("SimCenterWorld", (0.0, 0.0, 0.0, 0.0)), ("SimExtentsCm", (0.0, 0.0, 0.0, 0.0)))
COLLECTION_SOURCE_BY_INPUT = {"TrackWindowCenterWorld": "SimCenterWorld", "TrackWindowExtentsCm": "SimExtentsCm"}

DRIVEN_VECTOR_DEFAULTS = (("SimCenterWorld", (0.0, 0.0, 0.0, 0.0)), ("SimExtentsCm", (0.0, 0.0, 0.0, 0.0)))
DRIVEN_SCALAR_DEFAULTS = (("SimGridResolution", 0.0),)
LOOK_VECTOR_DEFAULTS = (
    ("TrackShadowLightDirection", (-0.5, -0.3, -0.81, 0.0)),
    ("TrackSurfaceColor", (0.85, 0.88, 0.92, 1.0)),
    ("TrackGrooveColor", (0.70, 0.75, 0.82, 1.0)),
    ("TileMapPeriodCm", (800.0, 800.0, 0.0, 0.0)),
)
LOOK_SCALAR_DEFAULTS = (
    ("TrackLayerThicknessCm", 20.0),
    ("TrackDepthScale", 1.0),
    ("TrackBankHeightCm", 6.0),
    ("TrackBankRadiusCm", 15.0),
    ("TrackOutsideLayerRatio", 1.0),
    ("TrackEdgeFadeCm", 250.0),
    ("TrackFloorClipInsetCm", 20.0),
    ("TrackEdgeSinkCm", 1.0),
    ("TrackUsesWindowMask", 1.0),
    ("TrackNormalStrength", 1.0),
    ("TrackPomMinSteps", 8.0),
    ("TrackPomMaxSteps", 24.0),
    ("TrackPomMaxDepthCm", 25.0),
    ("TrackPomFadeEndCm", 3000.0),
    ("TrackPomShadowStrength", 0.5),
    ("TrackPomShadowSteps", 8.0),
    ("TrackRoughness", 0.75),
    ("TrackDetailTileCm", 50.0),
    ("TrackDetailNormalStrength", 0.5),
    ("TrackSparkleIntensity", 0.6),
    ("TileMapHeightCm", 0.0),
    ("TileMapNormalStrength", 0.0),
    ("TileMapWindDegrees", 0.0),
    ("TileMapCameraFadeCm", 3000.0),
    ("TileMapCameraFadeLengthCm", 5000.0),
    ("TileMapWpoFadeEndCm", 6000.0),
)
VECTOR_PARAMETER_DEFAULTS = DRIVEN_VECTOR_DEFAULTS + LOOK_VECTOR_DEFAULTS
SCALAR_PARAMETER_DEFAULTS = DRIVEN_SCALAR_DEFAULTS + LOOK_SCALAR_DEFAULTS
BUILTIN_SOURCE_NAMES = ("TrackRestPosition", "TrackPixelPosition", "CameraWorldPosition", "TrackSunDirectionWS")
VECTOR_INPUT_NAMES = (
    "TrackRestPosition", "TrackPixelPosition", "CameraWorldPosition", "TrackSunDirectionWS",
    "SimCenterWorld", "SimExtentsCm", "TrackWindowCenterWorld", "TrackWindowExtentsCm",
    "TrackShadowLightDirection", "TrackSurfaceColor", "TrackGrooveColor", "TileMapPeriodCm",
)
COMMON_INPUT_NAMES = (
    "TrackRestPosition", "CameraWorldPosition", "DensityPressureTexture", "TrackDuneHeightTexture",
    "SimCenterWorld", "SimExtentsCm", "SimGridResolution", "TrackWindowCenterWorld", "TrackWindowExtentsCm",
    "TrackLayerThicknessCm", "TrackDepthScale", "TrackBankHeightCm", "TrackBankRadiusCm", "TrackOutsideLayerRatio",
    "TrackEdgeFadeCm", "TrackFloorClipInsetCm", "TrackEdgeSinkCm", "TrackUsesWindowMask",
    "TileMapPeriodCm", "TileMapHeightCm", "TileMapWindDegrees", "TileMapWpoFadeEndCm",
)
OFFSET_INPUT_NAMES = COMMON_INPUT_NAMES
SURFACE_INPUT_NAMES = COMMON_INPUT_NAMES + (
    "TrackPixelPosition", "TrackSunDirectionWS", "TrackDetailNormalTexture", "TrackNormalStrength",
    "TrackPomMinSteps", "TrackPomMaxSteps", "TrackPomMaxDepthCm", "TrackPomFadeEndCm", "TrackPomShadowStrength",
    "TrackPomShadowSteps", "TrackShadowLightDirection", "TrackSurfaceColor", "TrackGrooveColor",
    "TrackDetailTileCm", "TrackDetailNormalStrength", "TrackSparkleIntensity", "TileMapNormalStrength",
    "TileMapCameraFadeCm", "TileMapCameraFadeLengthCm",
)
SAMPLE_INPUT_NAMES = COMMON_INPUT_NAMES + ("TrackNormalStrength",)
SURFACE_OUTPUT_SPECS = (("TrackNormalWS", "CMOT_FLOAT3"), ("TrackPixelDepthOffsetCm", "CMOT_FLOAT1"))
SAMPLE_OUTPUT_SPECS = (("TrackSampleNormalWS", "CMOT_FLOAT3"),)
SAMPLE_FUNCTION_OUTPUTS = (("WindowMask", "R"), ("TrackDepth", "G"), ("HeightCm", "B"))
COMMON_SAMPLER_NAMES = ("DensityPressureTextureSampler", "TrackDuneHeightTextureSampler")
SURFACE_SAMPLER_NAMES = COMMON_SAMPLER_NAMES + ("TrackDetailNormalTextureSampler",)
MID_DRIVEN_NAMES = (
    "SimCenterWorld", "SimExtentsCm", "SimGridResolution", "DensityPressureTexture", "VelocityTexture",
    "PlaneAxisU", "PlaneAxisV", "PlaneAxisN", "PlaneCoordinateOffsetCm", "VolumeHeightCm", "VolumeBoxScale",
    "VolumeOffsetCm", "ExtrudeSymmetric", "AuxTexture", "AuxHeatActive",
)
CONDITIONAL_CPP_NAMES = ("DensityScale", "SoftFalloffCm")
TRACK_INSTANCE_NAMES = ("MI_TD_FluidTrack_Snow", "MI_TD_FluidTrack_SnowSlope", "MI_TD_FluidTrack_Sand")
DUST_INSTANCE_NAMES = ("MI_TD_FluidVolume_SnowDust", "MI_TD_FluidVolume_SandDust")
DUST_SOURCE_INSTANCE = "MI_TD_FluidVolume_GroundDust"
POM_TAP_FORMULA = "단계 수 × 1 + 중심 1 + 둑 고리 8 + 맞은 점 1 + 법선 4 + 그림자 단계 × 1 + 사구 2(+법선 3) + 세부 법선 1"
REQUIRED_UNREAL_NAMES = (
    "EditorAssetLibrary.load_asset",
    "EditorAssetLibrary.save_loaded_asset",
    "EditorAssetLibrary.does_asset_exist",
    "EditorAssetLibrary.delete_asset",
    "EditorLoadingAndSavingUtils.get_dirty_map_packages",
    "EditorLoadingAndSavingUtils.get_dirty_content_packages",
    "SystemLibrary.get_project_directory",
    "AssetImportTask",
    "MaterialEditingLibrary.create_material_expression",
    "MaterialEditingLibrary.create_material_expression_in_function",
    "MaterialEditingLibrary.connect_material_expressions",
    "MaterialEditingLibrary.connect_material_property",
    "MaterialEditingLibrary.delete_all_material_expressions",
    "MaterialEditingLibrary.delete_all_material_expressions_in_function",
    "MaterialEditingLibrary.update_material_function",
    "MaterialEditingLibrary.recompile_material",
    "MaterialEditingLibrary.get_vector_parameter_names",
    "MaterialEditingLibrary.get_scalar_parameter_names",
    "MaterialEditingLibrary.get_texture_parameter_names",
    "MaterialEditingLibrary.set_material_instance_texture_parameter_value",
    "MaterialEditingLibrary.get_material_default_scalar_parameter_value",
    "Material",
    "MaterialFactoryNew",
    "MaterialFunction",
    "MaterialFunctionFactoryNew",
    "MaterialInstanceConstant",
    "MaterialInstanceConstantFactoryNew",
    "MaterialParameterCollection",
    "MaterialParameterCollectionFactoryNew",
    "CollectionVectorParameter",
    "MaterialExpressionCollectionParameter",
    "MaterialExpressionCustom",
    "MaterialExpressionFunctionOutput",
    "CustomInput",
    "CustomOutput",
    "CustomMaterialOutputType.CMOT_FLOAT1",
    "CustomMaterialOutputType.CMOT_FLOAT3",
    "CustomMaterialOutputType.CMOT_FLOAT4",
    "MaterialExpressionWorldPosition",
    "WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS",
    "MaterialExpressionCameraPositionWS",
    "MaterialExpressionSkyAtmosphereLightDirection",
    "MaterialExpressionTextureObjectParameter",
    "MaterialExpressionVectorParameter",
    "MaterialExpressionScalarParameter",
    "MaterialExpressionComponentMask",
    "BlendMode.BLEND_MASKED",
    "MaterialShadingModel.MSM_DEFAULT_LIT",
    "MaterialProperty.MP_BASE_COLOR",
    "MaterialProperty.MP_NORMAL",
    "MaterialProperty.MP_ROUGHNESS",
    "MaterialProperty.MP_OPACITY_MASK",
    "MaterialProperty.MP_WORLD_POSITION_OFFSET",
    "MaterialProperty.MP_PIXEL_DEPTH_OFFSET",
    "MaterialSamplerType.SAMPLERTYPE_COLOR",
    "MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR",
    "MaterialSamplerType.SAMPLERTYPE_NORMAL",
    "TextureCompressionSettings.TC_DEFAULT",
    "TextureCompressionSettings.TC_NORMALMAP",
    "TextureCompressionSettings.TC_HALF_FLOAT",
    "TDFluidWaterMeshEditorLibrary.create_grid_static_mesh",
    "StaticMesh",
    "StaticMeshEditorSubsystem",
)

IDENTIFIER_PATTERN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
DEFINE_PATTERN = re.compile(r"^[ \t]*#define[ \t]+(TD_TRACK_[A-Za-z0-9_]+)", re.MULTILINE)
ANY_DEFINE_PATTERN = re.compile(r"^[ \t]*#define[ \t]+([A-Za-z_][A-Za-z0-9_]*)", re.MULTILINE)


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


def asset_path(name):
    return f"{MATERIAL_DIR}/{name}"


def read_text_file(path):
    if not os.path.isfile(path):
        raise RuntimeError(f"HLSL 파일 없음: {path}")
    with open(path, encoding="utf-8") as text_file:
        return text_file.read().replace("\r\n", "\n").strip("\n") + "\n"


def read_shader_sources(project_root):
    shader_root = os.path.join(project_root, *SHADER_DIRECTORY)
    return {
        "common": read_text_file(os.path.join(shader_root, COMMON_HLSL_NAME)),
        "offset": read_text_file(os.path.join(shader_root, OFFSET_HLSL_NAME)),
        "surface": read_text_file(os.path.join(shader_root, SURFACE_HLSL_NAME)),
        "sample": read_text_file(os.path.join(shader_root, SAMPLE_HLSL_NAME)),
    }


def assemble_node_code(common_code, node_code, is_vertex):
    body = common_code + "\n" + node_code
    macro_names = sorted(set(DEFINE_PATTERN.findall(body)) | {"TD_TRACK_VERTEX"})
    undefine_block = "".join(f"#undef {name}\n" for name in macro_names)
    return undefine_block + f"#define TD_TRACK_VERTEX {1 if is_vertex else 0}\n" + body + "\n" + undefine_block


def assemble_all_node_codes(sources):
    return {
        "offset": assemble_node_code(sources["common"], sources["offset"], True),
        "surface": assemble_node_code(sources["common"], sources["surface"], False),
        "sample": assemble_node_code(sources["common"], sources["sample"], False),
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


def validate_hlsl_code(code, input_names, sampler_names, output_names):
    problems = []
    identifiers = set(IDENTIFIER_PATTERN.findall(code))
    for label, names in (("Custom 입력", input_names), ("샘플러", sampler_names), ("추가 출력", output_names), ("엔진 심볼", ("Texture2DSampleLevel",))):
        missing = [name for name in names if name not in identifiers]
        if missing:
            problems.append(f"HLSL이 쓰지 않는 {label}: " + ", ".join(missing))
    stray = [name for name in ANY_DEFINE_PATTERN.findall(code) if not name.startswith(MACRO_PREFIX)]
    if stray:
        problems.append("접두 밖 매크로: " + ", ".join(stray))
    bracket_problem = find_unbalanced_brackets(code)
    if bracket_problem:
        problems.append(bracket_problem)
    if "return" not in identifiers:
        problems.append("HLSL에 return 이 없음")
    if "//" in code or "/*" in code:
        problems.append("HLSL에 주석이 있음")
    return problems


def validate_all_node_codes(codes):
    problems = []
    for label, code, inputs, samplers, outputs in (
        ("오프셋", codes["offset"], OFFSET_INPUT_NAMES, COMMON_SAMPLER_NAMES, ()),
        ("표면", codes["surface"], SURFACE_INPUT_NAMES, SURFACE_SAMPLER_NAMES, [name for name, _ in SURFACE_OUTPUT_SPECS]),
        ("표본", codes["sample"], SAMPLE_INPUT_NAMES, COMMON_SAMPLER_NAMES, [name for name, _ in SAMPLE_OUTPUT_SPECS]),
    ):
        problems.extend(f"{label}: {item}" for item in validate_hlsl_code(code, inputs, samplers, outputs))
    pixel_only = [token for token in ("TrackPixelPosition", "Texture2DSample(", "TD_TRACK_HEIGHT_AT") if token in codes["offset"]]
    if pixel_only:
        problems.append("오프셋에 화소 전용 이름: " + ", ".join(pixel_only))
    return problems


def find_unresolved_inputs():
    resolvable = set(BUILTIN_SOURCE_NAMES) | set(TEXTURE_PARAMETER_NAMES) | set(COLLECTION_SOURCE_BY_INPUT)
    resolvable |= {name for name, _ in VECTOR_PARAMETER_DEFAULTS} | {name for name, _ in SCALAR_PARAMETER_DEFAULTS}
    return sorted(set(SURFACE_INPUT_NAMES + OFFSET_INPUT_NAMES + SAMPLE_INPUT_NAMES) - resolvable)


def find_instance_override_problems(entries):
    problems = []
    overridable = {name for name, _ in LOOK_VECTOR_DEFAULTS} | {name for name, _ in LOOK_SCALAR_DEFAULTS}
    forbidden = set(MID_DRIVEN_NAMES) | set(CONDITIONAL_CPP_NAMES)
    for name in TRACK_INSTANCE_NAMES:
        entry = entries.get(asset_path(name))
        if entry is None:
            problems.append(f"{name}: 모양 정본에 없음")
            continue
        if entry.get("Parent") != MASTER_PATH:
            problems.append(f"{name}: 부모 {entry.get('Parent')} ≠ {MASTER_PATH}")
        names = set(entry.get("Scalars", {})) | set(entry.get("Vectors", {})) | set(entry.get("Switches", {}))
        if names & forbidden:
            problems.append(f"{name}: C++ 구동·조건부 이름 덮어씀 " + ", ".join(sorted(names & forbidden)))
        if names - overridable:
            problems.append(f"{name}: 마스터에 없는 이름 " + ", ".join(sorted(names - overridable)))
    source = entries.get(asset_path(DUST_SOURCE_INSTANCE))
    for name in DUST_INSTANCE_NAMES:
        entry = entries.get(asset_path(name))
        if entry is None:
            problems.append(f"{name}: 모양 정본에 없음")
            continue
        if entry.get("Parent") != VOLUME_MASTER_PATH:
            problems.append(f"{name}: 부모 {entry.get('Parent')} ≠ {VOLUME_MASTER_PATH}")
        if source is not None:
            source_scalars = {key: value for key, value in source.get("Scalars", {}).items() if key not in MID_DRIVEN_NAMES}
            if entry.get("Scalars") != source_scalars:
                problems.append(f"{name}: 스칼라가 {DUST_SOURCE_INSTANCE}(C++ 구동 이름 제외) 와 다름(색만 바꾼다, 반박 S1)")
            if set(entry.get("Vectors", {})) != set(source.get("Vectors", {})):
                problems.append(f"{name}: 벡터 이름 집합이 {DUST_SOURCE_INSTANCE} 와 다름")
        driven = sorted((set(entry.get("Scalars", {})) | set(entry.get("Vectors", {}))) & set(MID_DRIVEN_NAMES))
        if driven:
            problems.append(f"{name}: C++ 구동 이름 덮어씀 " + ", ".join(driven))
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
    paths = [MASTER_PATH, FUNCTION_PATH, COLLECTION_PATH, GRID_MESH_PATH]
    paths.extend(asset_path(name) for name in TRACK_INSTANCE_NAMES + DUST_INSTANCE_NAMES)
    paths.extend(f"{TEXTURE_DIR}/{spec['name']}" for spec in TEXTURE_SPECS)
    return paths


def find_unsaved_package_names(overwrite):
    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    dirty.extend(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    names = sorted({package.get_name() for package in dirty})
    return names if not overwrite else [name for name in names if not name.startswith(FLUID_ROOT + "/")]


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
    if compression == unreal.TextureCompressionSettings.TC_NORMALMAP:
        return unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
    if compression == unreal.TextureCompressionSettings.TC_HALF_FLOAT:
        return unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
    if compression != unreal.TextureCompressionSettings.TC_DEFAULT:
        return None
    return unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if is_srgb else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR


def pick_default_texture():
    for texture_path in DEFAULT_TEXTURE_PATHS:
        texture = unreal.EditorAssetLibrary.load_asset(texture_path)
        sampler_type = choose_sampler_type(texture) if texture is not None else None
        if sampler_type is not None:
            return texture_path, texture, sampler_type
    raise RuntimeError("기본 텍스처로 쓸 엔진 텍스처가 없음: " + ", ".join(DEFAULT_TEXTURE_PATHS))


def import_texture(spec, overwrite):
    source_path = project_path(*TEXTURE_SOURCE_DIRECTORY, spec["source"])
    if not os.path.isfile(source_path):
        raise RuntimeError(f"텍스처 원본 없음(python Tools/FluidFX/make_fluid_track_textures.py 먼저): {source_path}")
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
    texture.set_editor_property("srgb", False)
    compression = unreal.TextureCompressionSettings.TC_NORMALMAP if spec["kind"] == "normal" else unreal.TextureCompressionSettings.TC_HALF_FLOAT
    texture.set_editor_property("compression_settings", compression)
    save_asset(texture, path)
    sampler_type = choose_sampler_type(texture)
    if sampler_type is None:
        raise RuntimeError(f"텍스처 표본기 형식을 정할 수 없음: {path}")
    with open(source_path, "rb") as source_file:
        source_sha256 = hashlib.sha256(source_file.read()).hexdigest()
    return texture, sampler_type, {"asset": path, "source_sha256": source_sha256, "compression": str(compression), "sampler_type": str(sampler_type)}


def create_grid_mesh(overwrite):
    if unreal.EditorAssetLibrary.does_asset_exist(GRID_MESH_PATH):
        if not overwrite:
            raise RuntimeError(f"기존 에셋 보존: {GRID_MESH_PATH}")
        if not unreal.EditorAssetLibrary.delete_asset(GRID_MESH_PATH):
            raise RuntimeError(f"기존 격자 삭제 실패(참조 중인 맵을 닫고 다시 실행): {GRID_MESH_PATH}")
    ensure_directory(MESH_DIR)
    mesh = unreal.TDFluidWaterMeshEditorLibrary.create_grid_static_mesh(MESH_DIR, GRID_MESH_NAME, GRID_MESH_SIZE_CM, GRID_MESH_QUADS, unreal.load_asset(GRID_DEFAULT_MATERIAL_PATH))
    if not isinstance(mesh, unreal.StaticMesh):
        raise RuntimeError(f"격자 메시 생성 실패: {GRID_MESH_PATH}")
    vertex_total = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).get_number_verts(mesh, 0)
    if vertex_total != (GRID_MESH_QUADS + 1) ** 2:
        raise RuntimeError(f"격자 메시 정점 수 불일치: {vertex_total}")
    extension = {}
    for property_name, vector in (("positive_bounds_extension", (0.0, 0.0, GRID_BOUNDS_EXTENSION_UP_CM)), ("negative_bounds_extension", (0.0, 0.0, GRID_BOUNDS_EXTENSION_DOWN_CM))):
        if has_editor_property(mesh, property_name):
            mesh.set_editor_property(property_name, unreal.Vector(*vector))
            extension[property_name] = list(vector)
    save_asset(mesh, GRID_MESH_PATH)
    return {"asset": GRID_MESH_PATH, "size_cm": GRID_MESH_SIZE_CM, "quads": GRID_MESH_QUADS, "render_vertices": vertex_total, "bounds_extension": extension}


def load_or_create_collection(overwrite):
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(COLLECTION_PATH):
        if not overwrite:
            raise RuntimeError(f"기존 에셋 보존: {COLLECTION_PATH}")
        collection = assets.load_asset(COLLECTION_PATH)
    else:
        ensure_directory(MATERIAL_DIR)
        collection = unreal.AssetToolsHelpers.get_asset_tools().create_asset(COLLECTION_NAME, MATERIAL_DIR, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    if not isinstance(collection, unreal.MaterialParameterCollection):
        raise RuntimeError(f"MPC 준비 실패: {COLLECTION_PATH}")
    parameters = []
    for name, rgba in COLLECTION_VECTOR_DEFAULTS:
        parameter = unreal.CollectionVectorParameter()
        parameter.set_editor_property("parameter_name", name)
        parameter.set_editor_property("default_value", unreal.LinearColor(*rgba))
        parameters.append(parameter)
    collection.set_editor_property("vector_parameters", parameters)
    collection.set_editor_property("scalar_parameters", [])
    save_asset(collection, COLLECTION_PATH)
    return collection


class SourceFactory:
    def __init__(self, create_expression, textures, collection):
        self.create_expression = create_expression
        self.textures = textures
        self.collection = collection
        self.sources = {}
        self.row = 0

    def node(self, expression_class, x, y, **properties):
        expression = self.create_expression(expression_class, x, y)
        if expression is None:
            raise RuntimeError(f"재질 노드 생성 실패: {expression_class.__name__}")
        for name, value in properties.items():
            expression.set_editor_property(name, value)
        return expression

    def truncated(self, source, x, y):
        truncate_class = getattr(unreal, "MaterialExpressionTruncateLWC", None)
        if truncate_class is None:
            return source
        truncated = self.node(truncate_class, x, y)
        link(source, truncated)
        return truncated

    def get(self, name):
        if name not in self.sources:
            x, y = -1600, -2400 + self.row * 110
            self.row += 1
            self.sources[name] = self.create(name, x, y)
        return self.sources[name]

    def create(self, name, x, y):
        if name == "TrackRestPosition":
            position = self.node(unreal.MaterialExpressionWorldPosition, x - 220, y, world_position_shader_offset=unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
            return self.truncated(position, x, y), ""
        if name == "TrackPixelPosition":
            return self.truncated(self.node(unreal.MaterialExpressionWorldPosition, x - 220, y), x, y), ""
        if name == "CameraWorldPosition":
            return self.truncated(self.node(unreal.MaterialExpressionCameraPositionWS, x - 220, y), x, y), ""
        if name == "TrackSunDirectionWS":
            return self.node(unreal.MaterialExpressionSkyAtmosphereLightDirection, x, y), ""
        if name in COLLECTION_SOURCE_BY_INPUT:
            parameter = self.node(unreal.MaterialExpressionCollectionParameter, x - 220, y, collection=self.collection, parameter_name=COLLECTION_SOURCE_BY_INPUT[name])
            mask = self.node(unreal.MaterialExpressionComponentMask, x, y, r=True, g=True, b=True, a=False)
            link(parameter, mask)
            return mask, ""
        if name in self.textures:
            texture, sampler_type = self.textures[name]
            node = self.node(unreal.MaterialExpressionTextureObjectParameter, x, y, parameter_name=name, texture=texture, sampler_type=sampler_type)
            assign_parameter_group(node)
            return node, ""
        vectors = dict(VECTOR_PARAMETER_DEFAULTS)
        if name in vectors:
            node = self.node(unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=unreal.LinearColor(*vectors[name]))
            assign_parameter_group(node)
            return node, "RGB"
        scalars = dict(SCALAR_PARAMETER_DEFAULTS)
        if name in scalars:
            node = self.node(unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=scalars[name])
            assign_parameter_group(node)
            return node, ""
        raise RuntimeError(f"입력 원천을 정할 수 없음: {name}")


def link(source, target, target_input="", source_output=""):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(source, source_output, target, target_input):
        raise RuntimeError(f"재질 노드 연결 실패: {source.get_name()}[{source_output}] -> {target.get_name()}.{target_input}")


def connect_material_output(expression, material_property, source_output=""):
    if not unreal.MaterialEditingLibrary.connect_material_property(expression, source_output, material_property):
        raise RuntimeError(f"재질 출력 연결 실패: {material_property} <- {source_output}")


def assign_parameter_group(expression):
    try:
        expression.set_editor_property("group", PARAMETER_GROUP)
    except Exception:
        return


def build_custom_node(factory, description, output_type, input_names, code, x, y, output_specs):
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
    for name, kind in output_specs:
        custom_output = unreal.CustomOutput()
        custom_output.set_editor_property("output_name", name)
        custom_output.set_editor_property("output_type", getattr(unreal.CustomMaterialOutputType, kind))
        outputs.append(custom_output)
    custom.set_editor_property("additional_outputs", outputs)
    custom.set_editor_property("code", code)
    stored = [str(item.get_editor_property("input_name")) for item in custom.get_editor_property("inputs")]
    if stored != list(input_names):
        raise RuntimeError("Custom 노드 입력 이름 설정 확인 실패: " + ", ".join(stored))
    for name in input_names:
        source, source_output = factory.get(name)
        link(source, custom, name, source_output)
    return custom


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


def read_material_statistics(material):
    try:
        statistics = unreal.MaterialEditingLibrary.get_statistics(material)
        return {name: statistics.get_editor_property(name) for name in ("num_vertex_shader_instructions", "num_pixel_shader_instructions", "num_samplers")}
    except Exception:
        return {}


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
        raise RuntimeError("마스터 SimGridResolution 기본값이 0이 아님(정적 역할 판정 깨짐)")


def create_master(codes, textures, collection, overwrite):
    material, was_created = prepare_master(overwrite)
    for name, value in (("blend_mode", unreal.BlendMode.BLEND_MASKED), ("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT), ("tangent_space_normal", False), ("two_sided", False)):
        material.set_editor_property(name, value)
    factory = SourceFactory(lambda expression_class, x, y: unreal.MaterialEditingLibrary.create_material_expression(material, expression_class, x, y), textures, collection)
    offset = build_custom_node(factory, "TDFluidTrackOffset", unreal.CustomMaterialOutputType.CMOT_FLOAT3, OFFSET_INPUT_NAMES, codes["offset"], -500, 700, ())
    surface = build_custom_node(factory, "TDFluidTrackSurface", unreal.CustomMaterialOutputType.CMOT_FLOAT4, SURFACE_INPUT_NAMES, codes["surface"], -500, 0, SURFACE_OUTPUT_SPECS)
    color = factory.node(unreal.MaterialExpressionComponentMask, -150, -120, r=True, g=True, b=True, a=False)
    opacity = factory.node(unreal.MaterialExpressionComponentMask, -150, 40, r=False, g=False, b=False, a=True)
    link(surface, color)
    link(surface, opacity)
    roughness_source, _ = factory.get("TrackRoughness")
    connect_material_output(color, unreal.MaterialProperty.MP_BASE_COLOR)
    connect_material_output(opacity, unreal.MaterialProperty.MP_OPACITY_MASK)
    connect_material_output(surface, unreal.MaterialProperty.MP_NORMAL, "TrackNormalWS")
    connect_material_output(surface, unreal.MaterialProperty.MP_PIXEL_DEPTH_OFFSET, "TrackPixelDepthOffsetCm")
    connect_material_output(roughness_source, unreal.MaterialProperty.MP_ROUGHNESS)
    connect_material_output(offset, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    errors = [str(item) for item in unreal.MaterialEditingLibrary.recompile_material(material) or []]
    if errors:
        raise RuntimeError("M_TD_FluidTrackLayer 컴파일 실패(Saved/Logs/TDGame.log 의 LogMaterial 확인): " + " | ".join(errors))
    verify_master_parameters(material)
    save_asset(material, MASTER_PATH)
    return material, {"asset": MASTER_PATH, "created": was_created, "statistics": read_material_statistics(material), "pom_taps_per_pixel": POM_TAP_FORMULA}


def create_sample_function(codes, textures, collection, overwrite):
    assets = unreal.EditorAssetLibrary
    editing = unreal.MaterialEditingLibrary
    if assets.does_asset_exist(FUNCTION_PATH):
        if not overwrite:
            raise RuntimeError(f"기존 에셋 보존: {FUNCTION_PATH}")
        function = assets.load_asset(FUNCTION_PATH)
        editing.delete_all_material_expressions_in_function(function)
    else:
        function = unreal.AssetToolsHelpers.get_asset_tools().create_asset(FUNCTION_NAME, MATERIAL_DIR, unreal.MaterialFunction, unreal.MaterialFunctionFactoryNew())
    if not isinstance(function, unreal.MaterialFunction):
        raise RuntimeError(f"재질 함수 준비 실패: {FUNCTION_PATH}")
    factory = SourceFactory(lambda expression_class, x, y: editing.create_material_expression_in_function(function, expression_class, x, y), textures, collection)
    sample = build_custom_node(factory, "TDFluidTrackSample", unreal.CustomMaterialOutputType.CMOT_FLOAT4, SAMPLE_INPUT_NAMES, codes["sample"], -500, 0, SAMPLE_OUTPUT_SPECS)
    for index, (output_name, channel) in enumerate(SAMPLE_FUNCTION_OUTPUTS):
        mask = factory.node(unreal.MaterialExpressionComponentMask, -150, index * 160, r=channel == "R", g=channel == "G", b=channel == "B", a=False)
        link(sample, mask)
        output = factory.node(unreal.MaterialExpressionFunctionOutput, 150, index * 160, output_name=output_name, sort_priority=index)
        link(mask, output)
    normal_output = factory.node(unreal.MaterialExpressionFunctionOutput, 150, 480, output_name="NormalWS", sort_priority=len(SAMPLE_FUNCTION_OUTPUTS))
    link(sample, normal_output, "", "TrackSampleNormalWS")
    editing.update_material_function(function)
    save_asset(function, FUNCTION_PATH)
    return {"asset": FUNCTION_PATH, "outputs": [name for name, _ in SAMPLE_FUNCTION_OUTPUTS] + ["NormalWS"]}


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


def create_instances(look_table, track_master, volume_master, overwrite):
    records = []
    for name, master in [(name, track_master) for name in TRACK_INSTANCE_NAMES] + [(name, volume_master) for name in DUST_INSTANCE_NAMES]:
        instance = prepare_instance(name, overwrite)
        entry = look_table.apply_to_instance(unreal.MaterialEditingLibrary, unreal.LinearColor, instance, asset_path(name), master)
        texture_overrides = {}
        for parameter_name, texture_path in INSTANCE_TEXTURE_OVERRIDES.get(name, {}).items():
            texture = unreal.EditorAssetLibrary.load_asset(texture_path)
            if texture is None:
                raise RuntimeError(f"MI 텍스처 연결 대상 없음: {texture_path}")
            if not unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(instance, parameter_name, texture):
                raise RuntimeError(f"MI 텍스처 연결 실패: {name}.{parameter_name}")
            texture_overrides[parameter_name] = texture_path
        if texture_overrides:
            unreal.MaterialEditingLibrary.update_material_instance(instance)
        save_asset(instance, asset_path(name))
        records.append({"asset": asset_path(name), "scalars": entry["Scalars"], "vectors": entry["Vectors"], "texture_overrides": texture_overrides})
    return records


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
    codes = assemble_all_node_codes(read_shader_sources(unreal.SystemLibrary.get_project_directory()))
    problems = validate_all_node_codes(codes) + [f"입력 원천 없음: {name}" for name in find_unresolved_inputs()]
    problems += find_instance_override_problems(look_table.look["Materials"])
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
    volume_master = assets.load_asset(VOLUME_MASTER_PATH)
    sand_ripples = assets.load_asset(SAND_RIPPLES_NORMAL_PATH)
    if volume_master is None or sand_ripples is None:
        raise RuntimeError("기존 에셋 없음(editor_make_fluid_volume_material.py·editor_make_fluid_water_material.py 먼저): M_TD_FluidVolume 또는 T_TD_FluidSandRipplesN")

    print("단계: 텍스처·격자·MPC")
    default_path, default_texture, default_sampler = pick_default_texture()
    imported = {}
    texture_records = []
    for spec in TEXTURE_SPECS:
        texture, sampler_type, record = import_texture(spec, overwrite)
        imported[spec["name"]] = (texture, sampler_type)
        texture_records.append(record)
    textures = {name: (default_texture, default_sampler) if source == "default" else imported[source] for name, source in TEXTURE_PARAMETER_SOURCES.items()}
    grid_record = create_grid_mesh(overwrite)
    collection = load_or_create_collection(overwrite)

    print("단계: 마스터·재질 함수·인스턴스")
    master, master_record = create_master(codes, textures, collection, overwrite)
    function_record = create_sample_function(codes, textures, collection, overwrite)
    instance_records = create_instances(look_table, master, volume_master, overwrite)

    report = {
        "status": "ok",
        "overwrite": overwrite,
        "default_texture": default_path,
        "textures": texture_records,
        "grid_mesh": grid_record,
        "collection": COLLECTION_PATH,
        "master": master_record,
        "function": function_record,
        "instances": instance_records,
        "hlsl_sha256": {label: hashlib.sha256(code.encode("utf-8")).hexdigest() for label, code in codes.items()},
        "material_look_sha256": look_table.source_sha256(),
        "shadow_cache": "Rigid 쓰지 않음(정점 WPO 카메라 거리 페이드·MPC 창 클립)",
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
