"""실행 환경: TDGame 언리얼 에디터 안에서 FX-8a 공용 3D 노이즈 볼륨 텍스처 T_TD_FluidNoiseVolume(Saved/FluidFX/fire_textures/T_TD_FluidNoiseAtlas.png 64³ 아틀라스)과 볼륨 레이마칭 마스터 재질 M_TD_FluidVolume(TDFluidVolumeRaymarch.hlsl)·가짜 볼륨 마스터 M_TD_FluidFakeVolume(TDFluidFakeVolume.hlsl) 및 인스턴스를 만든다.
실행 명령: python Tools/FluidFX/make_fluid_fire_textures.py 다음 python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_volume_material.py (불꽃 재질 스크립트보다 먼저)
출력: /Game/FluidFX/Textures/T_TD_FluidNoiseAtlas·T_TD_FluidNoiseVolume, /Game/FluidFX/Materials/M_TD_FluidVolume, MI_TD_FluidVolume_SmokeHard·SmokeSoft·SmokeSlab·SmokeBillow·GroundDust, M_TD_FluidFakeVolume, MI_TD_FluidFakeVolume_PurplePlasma·GrayFluff·BlueSmoke·RedSmoke·FloorPulse·PlasmaSphere·SmokeChamber, Saved/FluidFX/fluid_volume_material.json
상태: 검증됨(2026-10-20 FX-8b 에디터 실행, 볼륨 입력 73/73·PS 917 명령, 가짜 볼륨 38/38). FX-8c 3차(2026-10-20) 볼륨 입력 76개(HeatEmissionSurfaceScale·AmbientSideFloor·AmbientCreviceFloor, 기본값은 옛 출력과 같음)·디더 View.StateFrameIndexMod8 과 FX-8c 4차 G1(2026-10-20) 볼륨 입력 77개·가짜 볼륨 입력 39개(PlaneCoordinateOffsetCm, 기본 0 = 옛 출력과 같음)는 2026-10-20 12:09 에디터 재실행(볼륨 77/77 연결·PS 931 명령·표본기 6, 가짜 볼륨 39/39·PS 292 명령, 인스턴스 11개). FX-8c 5차 굽기(2026-10-20: SmokeBillow 에 HeatEmissionSurfaceScale 1, 새 MI_TD_FluidVolume_GroundDust 는 SmokeBillow 색과 HeatEmissionSurfaceScale 를 뺀 스칼라에 RenderMode 2)는 에디터 재실행 전. 기존 에셋은 Saved/FluidFX/fluid_volume_material_request.json 의 {"overwrite": true} 가 있을 때만 같은 에셋 안에서 다시 짓고(노이즈 텍스처도 다시 가져옴), 미저장 작업이 있으면 중단한다.
"""
import hashlib
import json
import os
import re

import unreal


FLUID_ROOT = "/Game/FluidFX"
MATERIAL_DIR = f"{FLUID_ROOT}/Materials"
TEXTURE_DIR = f"{FLUID_ROOT}/Textures"
SHADER_DIRECTORY = ("Tools", "FluidFX", "Shaders")
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_volume_material_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_volume_material.json")

NOISE_ATLAS_SOURCE_RELATIVE_PATH = ("Saved", "FluidFX", "fire_textures", "T_TD_FluidNoiseAtlas.png")
NOISE_ATLAS_NAME = "T_TD_FluidNoiseAtlas"
NOISE_VOLUME_NAME = "T_TD_FluidNoiseVolume"
NOISE_ATLAS_PATH = f"{TEXTURE_DIR}/{NOISE_ATLAS_NAME}"
NOISE_VOLUME_PATH = f"{TEXTURE_DIR}/{NOISE_VOLUME_NAME}"
NOISE_TILE_SIZE = 64
NOISE_TEXTURE_PARAMETER_NAME = "NoiseVolumeTexture"
NOISE_SOURCE_PROPERTY_CANDIDATES = ("source2d_texture", "source_2d_texture")
NOISE_TILE_X_PROPERTY_CANDIDATES = ("source2d_tile_size_x", "source_2d_tile_size_x")
NOISE_TILE_Y_PROPERTY_CANDIDATES = ("source2d_tile_size_y", "source_2d_tile_size_y")

DEFAULT_TEXTURE_PATHS = ("/Engine/EngineResources/Black", "/Engine/EngineResources/DefaultTexture")

SMOKE_COLOR = (0.88, 0.12, 0.045)
SHADOW_COLOR = (0.018, 0.002, 0.001)
AMBIENT_COLOR = (0.16, 0.07, 0.05)
SLAB_SMOKE_COLOR = (0.35, 0.33, 0.31)
SLAB_AMBIENT_COLOR = (0.05, 0.05, 0.06)
SLAB_SHADOW_COLOR = (0.01, 0.009, 0.008)
BILLOW_AMBIENT_COLOR = (0.2, 0.2, 0.22)
RED_SMOKE_COLORS = {"SmokeColor": SMOKE_COLOR, "ShadowColor": SHADOW_COLOR, "AmbientColor": AMBIENT_COLOR}

BUILTIN_SOURCE_NAMES = ("PixelWorldPosition", "CameraWorldPosition", "SceneDepthCm", "PixelDepthCm", "ElapsedSeconds")
FLUID_TEXTURE_PARAMETER_NAMES = ("DensityPressureTexture", "VelocityTexture", "TransmittanceTexture", "AuxTexture")
SHADOW_CAPSULE_VECTOR_DEFAULTS = tuple(
    (f"ShadowCapsule{edge}{index}", (0.0, 0.0, 0.0, 0.0)) for edge in ("Start", "End") for index in range(8)
) + tuple((f"ShadowCapsuleRadii{group}", (0.0, 0.0, 0.0, 0.0)) for group in ("A", "B", "C"))
FX8B_VOLUME_VECTOR_DEFAULTS = (
    ("VolumeOffsetCm", (0.0, 0.0, 0.0, 0.0)),
    ("HeatEmissionColor", (1.0, 0.25, 0.05, 1.0)),
) + SHADOW_CAPSULE_VECTOR_DEFAULTS
FX8B_VOLUME_SCALAR_DEFAULTS = (
    ("TransmittanceActive", 0.0),
    ("AuxHeatActive", 0.0),
    ("VolumeFadeEndCm", 4200.0),
    ("VolumeFadeLengthCm", 1600.0),
    ("VolumeBoxScale", 1.0),
    ("HeatEmissionIntensity", 0.0),
    ("HeatEmissionExponent", 2.0),
    ("ShadowCapsuleCount", 0.0),
    ("ShadowCapsuleOpticalDepth", 1.897),
)
FX8C_VOLUME_SCALAR_DEFAULTS = (
    ("HeatEmissionSurfaceScale", 0.0),
    ("AmbientSideFloor", 0.35),
    ("AmbientCreviceFloor", 0.0),
)

VOLUME_MASTER = {
    "name": "M_TD_FluidVolume",
    "description": "TDFluidVolumeRaymarch",
    "hlsl": "TDFluidVolumeRaymarch.hlsl",
    "group": "TDFluidVolume",
    "blend": "BLEND_TRANSLUCENT",
    "disable_depth_test": True,
    "vectors": (
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
        ("PlaneCoordinateOffsetCm", (0.0, 0.0, 0.0, 0.0)),
        ("ExtinctionTint", (1.0, 1.3, 1.8, 1.0)),
    ) + FX8B_VOLUME_VECTOR_DEFAULTS,
    "scalars": (
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
        ("NoiseErosion", 0.8),
        ("NoiseGrain", 0.3),
        ("NoiseGrainScale", 4.0),
        ("PhaseForwardG", 0.6),
        ("PhaseBackwardG", -0.25),
        ("PhaseForwardWeight", 0.7),
        ("PhaseScatterScale", 0.5),
        ("MultiScatterStrength", 0.5),
    ) + FX8B_VOLUME_SCALAR_DEFAULTS + FX8C_VOLUME_SCALAR_DEFAULTS,
    "inputs": (
        "PixelWorldPosition",
        "CameraWorldPosition",
        "SceneDepthCm",
        "PixelDepthCm",
        "ElapsedSeconds",
        "DensityPressureTexture",
        "VelocityTexture",
        "NoiseVolumeTexture",
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
        "PlaneCoordinateOffsetCm",
        "ExtrudeSymmetric",
        "NoiseErosion",
        "NoiseGrain",
        "NoiseGrainScale",
        "PhaseForwardG",
        "PhaseBackwardG",
        "PhaseForwardWeight",
        "PhaseScatterScale",
        "MultiScatterStrength",
        "ExtinctionTint",
        "TransmittanceTexture",
        "AuxTexture",
    ) + tuple(name for name, _ in FX8B_VOLUME_SCALAR_DEFAULTS) + tuple(name for name, _ in FX8B_VOLUME_VECTOR_DEFAULTS) + tuple(name for name, _ in FX8C_VOLUME_SCALAR_DEFAULTS),
    "samplers": ("DensityPressureTextureSampler", "VelocityTextureSampler", "NoiseVolumeTextureSampler", "TransmittanceTextureSampler", "AuxTextureSampler"),
    "engine_symbols": ("Parameters", "View", "Texture2DSampleLevel", "Texture3DSampleLevel"),
}

FAKE_MASTER = {
    "name": "M_TD_FluidFakeVolume",
    "description": "TDFluidFakeVolume",
    "hlsl": "TDFluidFakeVolume.hlsl",
    "group": "TDFluidFakeVolume",
    "blend": "BLEND_ALPHA_COMPOSITE",
    "disable_depth_test": False,
    "vectors": (
        ("SimCenterWorld", (0.0, 0.0, 0.0, 1.0)),
        ("SimExtentsCm", (700.0, 700.0, 0.1, 0.0)),
        ("PlaneAxisU", (1.0, 0.0, 0.0, 0.0)),
        ("PlaneAxisV", (0.0, 1.0, 0.0, 0.0)),
        ("PlaneAxisN", (0.0, 0.0, 1.0, 0.0)),
        ("PlaneCoordinateOffsetCm", (0.0, 0.0, 0.0, 0.0)),
        ("VolumeLightPosition", (0.0, 0.0, 300.0, 0.0)),
        ("VolumeLightDirection", (-0.45, 0.3, -0.84, 0.0)),
        ("VolumeLightColor", (3.0, 2.8, 2.6, 1.0)),
        ("SmokeColor", (0.5, 0.5, 0.52, 1.0)),
        ("AmbientColor", (0.08, 0.09, 0.11, 1.0)),
        ("ShadowColor", (0.004, 0.004, 0.005, 1.0)),
        ("EmissionColor", (0.0, 0.0, 0.0, 1.0)),
        ("ExtinctionTint", (1.0, 1.15, 1.3, 1.0)),
    ),
    "scalars": (
        ("VolumeLightRadius", 800.0),
        ("VolumeLightMode", 2.0),
        ("FakeDensitySource", 1.0),
        ("DensityScale", 0.03),
        ("FakeThicknessCm", 100.0),
        ("ShadowSteps", 8.0),
        ("FakeShadowLengthCm", 300.0),
        ("NoiseScale", 0.01),
        ("NoiseStrength", 0.5),
        ("NoiseFlowSpeed", 0.6),
        ("EmissionPower", 2.0),
        ("PulseRate", 0.0),
        ("PulseDepth", 0.0),
        ("PhaseForwardG", 0.6),
        ("PhaseBackwardG", -0.25),
        ("PhaseForwardWeight", 0.7),
        ("DepthFadeCm", 20.0),
    ),
    "inputs": (
        "DensityPressureTexture",
        "VelocityTexture",
        "NoiseVolumeTexture",
        "SimCenterWorld",
        "SimExtentsCm",
        "PlaneAxisU",
        "PlaneAxisV",
        "PlaneAxisN",
        "PlaneCoordinateOffsetCm",
        "PixelWorldPosition",
        "CameraWorldPosition",
        "SceneDepthCm",
        "PixelDepthCm",
        "ElapsedSeconds",
        "VolumeLightPosition",
        "VolumeLightDirection",
        "VolumeLightColor",
        "VolumeLightRadius",
        "VolumeLightMode",
        "SmokeColor",
        "AmbientColor",
        "ShadowColor",
        "EmissionColor",
        "ExtinctionTint",
        "FakeDensitySource",
        "DensityScale",
        "FakeThicknessCm",
        "ShadowSteps",
        "FakeShadowLengthCm",
        "NoiseScale",
        "NoiseStrength",
        "NoiseFlowSpeed",
        "EmissionPower",
        "PulseRate",
        "PulseDepth",
        "PhaseForwardG",
        "PhaseBackwardG",
        "PhaseForwardWeight",
        "DepthFadeCm",
    ),
    "samplers": ("DensityPressureTextureSampler", "VelocityTextureSampler", "NoiseVolumeTextureSampler"),
    "engine_symbols": ("Parameters", "Texture2DSampleLevel", "Texture3DSampleLevel"),
}
MASTER_SPECS = (VOLUME_MASTER, FAKE_MASTER)

INSTANCE_SPECS = (
    {"name": "MI_TD_FluidVolume_SmokeHard", "master": VOLUME_MASTER["name"], "vectors": RED_SMOKE_COLORS, "scalars": {"RenderMode": 0.0}},
    {"name": "MI_TD_FluidVolume_SmokeSoft", "master": VOLUME_MASTER["name"], "vectors": RED_SMOKE_COLORS, "scalars": {"RenderMode": 1.0}},
    {
        "name": "MI_TD_FluidVolume_SmokeSlab",
        "master": VOLUME_MASTER["name"],
        "vectors": {"SmokeColor": SLAB_SMOKE_COLOR, "ShadowColor": SLAB_SHADOW_COLOR, "AmbientColor": SLAB_AMBIENT_COLOR, "ExtinctionTint": (1.0, 1.15, 1.35)},
        "scalars": {"RenderMode": 2.0, "ExtrudeSymmetric": 1.0, "DensityScale": 0.04, "NoiseStrength": 0.8, "NoiseScale": 0.012, "NoiseErosion": 0.5},
    },
    {
        "name": "MI_TD_FluidVolume_SmokeBillow",
        "master": VOLUME_MASTER["name"],
        "vectors": {"SmokeColor": SLAB_SMOKE_COLOR, "ShadowColor": SLAB_SHADOW_COLOR, "AmbientColor": BILLOW_AMBIENT_COLOR, "ExtinctionTint": (1.0, 1.15, 1.35)},
        "scalars": {"RenderMode": 1.0, "ExtrudeSymmetric": 1.0, "DensityScale": 0.04, "NoiseStrength": 0.8, "NoiseScale": 0.012, "NoiseErosion": 0.3, "SurfaceSoftnessCm": 24.0, "HeatEmissionSurfaceScale": 1.0},
    },
    {
        "name": "MI_TD_FluidVolume_GroundDust",
        "master": VOLUME_MASTER["name"],
        "vectors": {"SmokeColor": SLAB_SMOKE_COLOR, "ShadowColor": SLAB_SHADOW_COLOR, "AmbientColor": BILLOW_AMBIENT_COLOR, "ExtinctionTint": (1.0, 1.15, 1.35)},
        "scalars": {"RenderMode": 2.0, "ExtrudeSymmetric": 1.0, "DensityScale": 0.04, "NoiseStrength": 0.8, "NoiseScale": 0.012, "NoiseErosion": 0.3, "SurfaceSoftnessCm": 24.0},
    },
    {
        "name": "MI_TD_FluidFakeVolume_PurplePlasma",
        "master": FAKE_MASTER["name"],
        "vectors": {"SmokeColor": (0.22, 0.08, 0.4), "AmbientColor": (0.05, 0.03, 0.08), "EmissionColor": (1.2, 0.25, 2.4), "ExtinctionTint": (1.2, 1.6, 1.0)},
        "scalars": {"DensityScale": 0.012, "FakeThicknessCm": 80.0, "EmissionPower": 2.5, "PulseRate": 1.5, "PulseDepth": 0.15},
    },
    {
        "name": "MI_TD_FluidFakeVolume_GrayFluff",
        "master": FAKE_MASTER["name"],
        "vectors": {"SmokeColor": (0.62, 0.62, 0.64), "AmbientColor": (0.1, 0.11, 0.13)},
        "scalars": {"DensityScale": 0.03, "FakeThicknessCm": 140.0, "NoiseStrength": 0.7, "PhaseForwardG": 0.45, "ShadowSteps": 10.0},
    },
    {
        "name": "MI_TD_FluidFakeVolume_BlueSmoke",
        "master": FAKE_MASTER["name"],
        "vectors": {"SmokeColor": (0.16, 0.3, 0.62), "AmbientColor": (0.04, 0.06, 0.12), "ExtinctionTint": (1.6, 1.2, 1.0)},
        "scalars": {"DensityScale": 0.025, "FakeThicknessCm": 100.0},
    },
    {
        "name": "MI_TD_FluidFakeVolume_RedSmoke",
        "master": FAKE_MASTER["name"],
        "vectors": {"SmokeColor": (0.78, 0.12, 0.045), "AmbientColor": (0.12, 0.04, 0.03), "ExtinctionTint": (1.0, 1.5, 2.2)},
        "scalars": {"DensityScale": 0.03, "FakeThicknessCm": 100.0},
    },
    {
        "name": "MI_TD_FluidFakeVolume_FloorPulse",
        "master": FAKE_MASTER["name"],
        "vectors": {"SmokeColor": (0.2, 0.24, 0.3), "EmissionColor": (0.15, 0.9, 1.3)},
        "scalars": {"DensityScale": 0.02, "FakeThicknessCm": 40.0, "EmissionPower": 1.5, "PulseRate": 3.5, "PulseDepth": 0.8},
    },
    {
        "name": "MI_TD_FluidFakeVolume_PlasmaSphere",
        "master": FAKE_MASTER["name"],
        "vectors": {"SmokeColor": (0.3, 0.12, 0.5), "EmissionColor": (2.0, 0.7, 3.0)},
        "scalars": {"DensityScale": 0.008, "FakeThicknessCm": 160.0, "EmissionPower": 1.2, "PulseRate": 2.2, "PulseDepth": 0.35},
    },
    {
        "name": "MI_TD_FluidFakeVolume_SmokeChamber",
        "master": FAKE_MASTER["name"],
        "vectors": {"SmokeColor": (0.09, 0.085, 0.08), "AmbientColor": (0.04, 0.04, 0.05), "ExtinctionTint": (1.0, 1.1, 1.25)},
        "scalars": {"DensityScale": 0.06, "FakeThicknessCm": 220.0, "ShadowSteps": 12.0, "FakeShadowLengthCm": 500.0},
    },
)

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
    "AssetImportTask",
    "Material",
    "MaterialFactoryNew",
    "MaterialInstanceConstant",
    "MaterialInstanceConstantFactoryNew",
    "VolumeTexture",
    "VolumeTextureFactory",
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
    "BlendMode.BLEND_ALPHA_COMPOSITE",
    "MaterialShadingModel.MSM_UNLIT",
    "MaterialProperty.MP_EMISSIVE_COLOR",
    "MaterialProperty.MP_OPACITY",
    "MaterialSamplerType.SAMPLERTYPE_COLOR",
    "MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR",
    "TextureCompressionSettings.TC_DEFAULT",
    "TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP",
    "TextureMipGenSettings.TMGS_NO_MIPMAPS",
    "TextureAddress.TA_WRAP",
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


def material_asset_path(name):
    return f"{MATERIAL_DIR}/{name}"


def collect_target_asset_paths():
    paths = [NOISE_ATLAS_PATH, NOISE_VOLUME_PATH]
    paths.extend(material_asset_path(spec["name"]) for spec in MASTER_SPECS)
    paths.extend(material_asset_path(spec["name"]) for spec in INSTANCE_SPECS)
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


def save_asset(asset, asset_path):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"에셋 저장 실패: {asset_path}")


def has_editor_property(owner, property_name):
    try:
        owner.get_editor_property(property_name)
    except Exception:
        return False
    return True


def set_first_existing_property(owner, candidates, value):
    for property_name in candidates:
        if has_editor_property(owner, property_name):
            owner.set_editor_property(property_name, value)
            return property_name
    raise RuntimeError("속성 이름 없음(엔진 버전 확인 필요): " + ", ".join(candidates))


def load_hlsl_code(file_name):
    hlsl_path = project_path(*SHADER_DIRECTORY, file_name)
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


def validate_hlsl_code(code, spec):
    problems = []
    identifiers = set(IDENTIFIER_PATTERN.findall(code))
    unused_inputs = [name for name in spec["inputs"] if name not in identifiers]
    if unused_inputs:
        problems.append(f"{spec['name']} HLSL이 쓰지 않는 Custom 입력: " + ", ".join(unused_inputs))
    missing_samplers = [name for name in spec["samplers"] if name not in identifiers]
    if missing_samplers:
        problems.append(f"{spec['name']} HLSL이 쓰지 않는 샘플러: " + ", ".join(missing_samplers))
    missing_symbols = [name for name in spec["engine_symbols"] if name not in identifiers]
    if missing_symbols:
        problems.append(f"{spec['name']} HLSL에 없는 엔진 심볼: " + ", ".join(missing_symbols))
    bracket_problem = find_unbalanced_brackets(code)
    if bracket_problem:
        problems.append(f"{spec['name']} {bracket_problem}")
    if "return" not in identifiers:
        problems.append(f"{spec['name']} HLSL에 return 이 없음")
    return problems


def find_unresolved_inputs(spec):
    resolvable = set(BUILTIN_SOURCE_NAMES) | set(FLUID_TEXTURE_PARAMETER_NAMES) | {NOISE_TEXTURE_PARAMETER_NAME}
    resolvable |= {name for name, _ in spec["vectors"]}
    resolvable |= {name for name, _ in spec["scalars"]}
    return [name for name in spec["inputs"] if name not in resolvable]


def find_unknown_instance_overrides():
    declared = {
        spec["name"]: {name for name, _ in spec["vectors"]} | {name for name, _ in spec["scalars"]} for spec in MASTER_SPECS
    }
    unknown = []
    for instance in INSTANCE_SPECS:
        names = set(instance["vectors"]) | set(instance["scalars"])
        unknown.extend(f"{instance['name']}.{name}" for name in sorted(names - declared[instance["master"]]))
    return unknown


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


def import_noise_atlas():
    source_path = project_path(*NOISE_ATLAS_SOURCE_RELATIVE_PATH)
    if not os.path.isfile(source_path):
        raise RuntimeError(f"노이즈 아틀라스 원본 없음(python Tools/FluidFX/make_fluid_fire_textures.py 먼저): {source_path}")
    ensure_directory(TEXTURE_DIR)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", source_path)
    task.set_editor_property("destination_path", TEXTURE_DIR)
    task.set_editor_property("destination_name", NOISE_ATLAS_NAME)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", False)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    atlas = unreal.EditorAssetLibrary.load_asset(NOISE_ATLAS_PATH)
    if not isinstance(atlas, unreal.Texture2D):
        raise RuntimeError(f"노이즈 아틀라스 가져오기 실패: {NOISE_ATLAS_PATH}")
    atlas.set_editor_property("srgb", False)
    atlas.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
    atlas.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    save_asset(atlas, NOISE_ATLAS_PATH)
    return atlas, source_path


def prepare_noise_volume_asset():
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(NOISE_VOLUME_PATH):
        volume = assets.load_asset(NOISE_VOLUME_PATH)
        if not isinstance(volume, unreal.VolumeTexture):
            raise RuntimeError(f"기존 에셋 형식 불일치: {NOISE_VOLUME_PATH}")
        return volume
    volume = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        NOISE_VOLUME_NAME, TEXTURE_DIR, unreal.VolumeTexture, unreal.VolumeTextureFactory()
    )
    if volume is None:
        raise RuntimeError(f"볼륨 텍스처 생성 실패: {NOISE_VOLUME_PATH}")
    return volume


def build_noise_volume_texture():
    atlas, source_path = import_noise_atlas()
    volume = prepare_noise_volume_asset()
    source_property = set_first_existing_property(volume, NOISE_SOURCE_PROPERTY_CANDIDATES, atlas)
    tile_x_property = set_first_existing_property(volume, NOISE_TILE_X_PROPERTY_CANDIDATES, NOISE_TILE_SIZE)
    tile_y_property = set_first_existing_property(volume, NOISE_TILE_Y_PROPERTY_CANDIDATES, NOISE_TILE_SIZE)
    volume.set_editor_property("srgb", False)
    volume.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
    volume.set_editor_property("address_mode", unreal.TextureAddress.TA_WRAP)
    stored_tiles = (volume.get_editor_property(tile_x_property), volume.get_editor_property(tile_y_property))
    if stored_tiles != (NOISE_TILE_SIZE, NOISE_TILE_SIZE):
        raise RuntimeError(f"볼륨 텍스처 타일 크기 확인 실패: {stored_tiles}")
    if volume.get_editor_property(source_property) != atlas:
        raise RuntimeError("볼륨 텍스처 원본 아틀라스 연결 확인 실패")
    save_asset(volume, NOISE_VOLUME_PATH)
    sampler_type = choose_sampler_type(volume)
    if sampler_type is None:
        raise RuntimeError(f"볼륨 텍스처 샘플러 형식을 정할 수 없음: {NOISE_VOLUME_PATH}")
    with open(source_path, "rb") as source_file:
        source_sha256 = hashlib.sha256(source_file.read()).hexdigest()
    summary = {
        "atlas": NOISE_ATLAS_PATH,
        "volume": NOISE_VOLUME_PATH,
        "source": source_path,
        "source_sha256": source_sha256,
        "tile_size": NOISE_TILE_SIZE,
        "properties": [source_property, tile_x_property, tile_y_property],
        "sampler_type": str(sampler_type),
    }
    return volume, sampler_type, summary


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


def build_custom_node(material, spec, code, x, y):
    custom = add_node(material, unreal.MaterialExpressionCustom, x, y)
    custom.set_editor_property("description", spec["description"])
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property("inputs", [make_custom_input(name) for name in spec["inputs"]])
    custom.set_editor_property("code", code)
    stored_names = [str(item.get_editor_property("input_name")) for item in custom.get_editor_property("inputs")]
    if stored_names != list(spec["inputs"]):
        raise RuntimeError("Custom 노드 입력 이름 설정 확인 실패: " + ", ".join(stored_names))
    return custom


def apply_material_settings(material, spec):
    material.set_editor_property("blend_mode", getattr(unreal.BlendMode, spec["blend"]))
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("disable_depth_test", spec["disable_depth_test"])
    applied = {
        "blend_mode": spec["blend"],
        "shading_model": "MSM_UNLIT",
        "two_sided": True,
        "disable_depth_test": spec["disable_depth_test"],
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


def prepare_master_material(spec, overwrite):
    assets = unreal.EditorAssetLibrary
    master_path = material_asset_path(spec["name"])
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
        spec["name"], MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew()
    )
    if material is None:
        raise RuntimeError(f"마스터 재질 생성 실패: {master_path}")
    return material, True


def add_builtin_sources(material, sources):
    world_position = add_node(material, unreal.MaterialExpressionWorldPosition, -1500, -1100)
    camera_position = add_node(material, unreal.MaterialExpressionCameraPositionWS, -1500, -980)
    sources["PixelWorldPosition"] = (truncate_if_available(material, world_position, -1300, -1100), "")
    sources["CameraWorldPosition"] = (truncate_if_available(material, camera_position, -1300, -980), "")
    sources["SceneDepthCm"] = (add_node(material, unreal.MaterialExpressionSceneDepth, -1300, -860), "")
    sources["PixelDepthCm"] = (add_node(material, unreal.MaterialExpressionPixelDepth, -1300, -760), "")
    sources["ElapsedSeconds"] = (add_node(material, unreal.MaterialExpressionTime, -1300, -660), "")


def add_texture_sources(material, spec, sources, textures):
    for row, (texture_name, (texture, sampler_type)) in enumerate(textures.items()):
        texture_node = add_node(
            material,
            unreal.MaterialExpressionTextureObjectParameter,
            -1300,
            -520 + row * 140,
            parameter_name=texture_name,
            texture=texture,
            sampler_type=sampler_type,
        )
        assign_parameter_group(texture_node, spec["group"])
        sources[texture_name] = (texture_node, "")


def add_parameter_sources(material, spec, sources):
    for row, (parameter_name, default_value) in enumerate(spec["vectors"]):
        vector_node = add_node(
            material,
            unreal.MaterialExpressionVectorParameter,
            -1300,
            -100 + row * 130,
            parameter_name=parameter_name,
            default_value=unreal.LinearColor(*default_value),
        )
        assign_parameter_group(vector_node, spec["group"])
        sources[parameter_name] = (vector_node, "RGB")
    scalar_top = -100 + len(spec["vectors"]) * 130 + 60
    for row, (parameter_name, default_value) in enumerate(spec["scalars"]):
        scalar_node = add_node(
            material,
            unreal.MaterialExpressionScalarParameter,
            -1300,
            scalar_top + row * 110,
            parameter_name=parameter_name,
            default_value=default_value,
        )
        assign_parameter_group(scalar_node, spec["group"])
        sources[parameter_name] = (scalar_node, "")


def build_master_graph(material, spec, code, textures):
    sources = {}
    add_builtin_sources(material, sources)
    add_texture_sources(material, spec, sources, textures)
    add_parameter_sources(material, spec, sources)

    custom = build_custom_node(material, spec, code, -300, 0)
    for input_name in spec["inputs"]:
        source, source_output = sources[input_name]
        link(source, custom, input_name, source_output)

    color_mask = add_node(material, unreal.MaterialExpressionComponentMask, 200, -80, r=True, g=True, b=True, a=False)
    alpha_mask = add_node(material, unreal.MaterialExpressionComponentMask, 200, 80, r=False, g=False, b=False, a=True)
    link(custom, color_mask)
    link(custom, alpha_mask)
    connect_material_output(color_mask, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    connect_material_output(alpha_mask, unreal.MaterialProperty.MP_OPACITY)
    return custom


def verify_custom_inputs_connected(material, custom, expected_count):
    getter = getattr(unreal.MaterialEditingLibrary, "get_inputs_for_material_expression", None)
    if getter is None:
        return "입력 연결 개수 확인 도구 없음"
    connected = [item for item in getter(material, custom) if item is not None]
    if len(connected) != expected_count:
        raise RuntimeError(f"Custom 입력 연결 개수 불일치: {len(connected)}/{expected_count}")
    return f"{len(connected)}/{expected_count}"


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
        "texture": list(FLUID_TEXTURE_PARAMETER_NAMES) + [NOISE_TEXTURE_PARAMETER_NAME],
    }
    missing = [f"{kind}:{name}" for kind, names in expected.items() for name in names if name not in found[kind]]
    if missing:
        raise RuntimeError(f"{spec['name']} 재질 파라미터 없음: " + ", ".join(missing))


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


def create_master_material(spec, code, textures, overwrite):
    master_path = material_asset_path(spec["name"])
    material, was_created = prepare_master_material(spec, overwrite)
    settings, skipped_settings = apply_material_settings(material, spec)
    custom = build_master_graph(material, spec, code, textures)
    connected = verify_custom_inputs_connected(material, custom, len(spec["inputs"]))
    statistics = compile_master_material(material, spec["name"])
    verify_master_parameters(material, spec)
    save_asset(material, master_path)
    return material, {
        "asset": master_path,
        "created": was_created,
        "settings": settings,
        "skipped_settings": skipped_settings,
        "custom_inputs_connected": connected,
        "statistics_available": bool(statistics),
        "statistics": statistics,
    }


def prepare_instance(spec, overwrite):
    assets = unreal.EditorAssetLibrary
    asset_path = material_asset_path(spec["name"])
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
    asset_path = material_asset_path(spec["name"])
    instance = prepare_instance(spec, overwrite)
    editing.set_material_instance_parent(instance, master)
    for parameter_name, rgb in spec["vectors"].items():
        editing.set_material_instance_vector_parameter_value(
            instance, parameter_name, unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0)
        )
    for parameter_name, value in spec["scalars"].items():
        editing.set_material_instance_scalar_parameter_value(instance, parameter_name, value)
    editing.update_material_instance(instance)

    for parameter_name, rgb in spec["vectors"].items():
        stored = editing.get_material_instance_vector_parameter_value(instance, parameter_name)
        error = max(abs(stored.r - rgb[0]), abs(stored.g - rgb[1]), abs(stored.b - rgb[2]))
        if error > 0.001:
            raise RuntimeError(f"재질 인스턴스 색 설정 확인 실패: {asset_path}.{parameter_name}")
    for parameter_name, value in spec["scalars"].items():
        stored = editing.get_material_instance_scalar_parameter_value(instance, parameter_name)
        if abs(stored - value) > 0.001:
            raise RuntimeError(f"재질 인스턴스 스칼라 설정 확인 실패: {asset_path}.{parameter_name}")
    save_asset(instance, asset_path)
    return {"asset": asset_path, "master": spec["master"], "vectors": spec["vectors"], "scalars": spec["scalars"]}


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


def load_and_check_all_hlsl():
    codes = {}
    problems = []
    for spec in MASTER_SPECS:
        hlsl_path, code = load_hlsl_code(spec["hlsl"])
        codes[spec["name"]] = (hlsl_path, code)
        problems += validate_hlsl_code(code, spec)
        unresolved = find_unresolved_inputs(spec)
        if unresolved:
            problems.append(f"{spec['name']} 입력의 원천 없음: " + ", ".join(unresolved))
    unknown = find_unknown_instance_overrides()
    if unknown:
        problems.append("인스턴스 덮어쓰기 이름이 마스터에 없음: " + ", ".join(unknown))
    return codes, problems


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

    codes, problems = load_and_check_all_hlsl()
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

    print("단계: 3D 노이즈 볼륨 텍스처")
    noise_texture, noise_sampler_type, noise_summary = build_noise_volume_texture()
    textures = {
        "DensityPressureTexture": (default_texture, sampler_type),
        "VelocityTexture": (default_texture, sampler_type),
        "TransmittanceTexture": (default_texture, sampler_type),
        "AuxTexture": (default_texture, sampler_type),
        NOISE_TEXTURE_PARAMETER_NAME: (noise_texture, noise_sampler_type),
    }

    print("단계: 마스터 재질 생성")
    masters = {}
    master_summaries = []
    for spec in MASTER_SPECS:
        master, summary = create_master_material(spec, codes[spec["name"]][1], textures, overwrite)
        masters[spec["name"]] = master
        summary["hlsl"] = describe_hlsl(*codes[spec["name"]])
        master_summaries.append(summary)

    print("단계: 재질 인스턴스 생성")
    instances = [create_material_instance(spec, masters[spec["master"]], overwrite) for spec in INSTANCE_SPECS]

    missing_files = find_missing_saved_files()
    if missing_files:
        raise RuntimeError("저장 파일 확인 실패: " + ", ".join(missing_files))

    report = {
        "status": "ok",
        "overwrite": overwrite,
        "default_texture": texture_path,
        "sampler_type": str(sampler_type),
        "noise_texture": noise_summary,
        "masters": master_summaries,
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
        raise


if __name__ == "__main__":
    run_with_failure_report()
