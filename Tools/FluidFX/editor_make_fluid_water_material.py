"""실행 환경: TDGame 언리얼 에디터 안에서 FluidFX 물 수면 마스터 재질 M_TD_FluidWater(Substrate 단일층 물)·M_TD_FluidWaterLite(DefaultLit)와 인스턴스 5종(강·호수·바다·폭풍·늪), 젖음 DBuffer 데칼 재질 M_TD_FluidWetnessDecal(TDFluidWaterWetDecal.hlsl), 텍스처 3장, 격자 메시 2개, 0단계 확인 3건을 만든다. Custom 노드 2개(표면·정점 오프셋)에 Tools/FluidFX/Shaders/TDFluidWaterCommon·Surface·Offset.hlsl 을 노드별로 조립해 넣는다.
실행 명령: python Tools/FluidFX/make_fluid_water_textures.py 다음 python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_water_material.py
출력: /Game/FluidFX/Materials/M_TD_FluidWater·M_TD_FluidWaterLite·MI_TD_FluidWater_River·Lake·Sea·SeaStorm·Swamp·M_TD_FluidWaterDecalProbe·M_TD_FluidWetnessDecal, /Game/FluidFX/Textures/T_TD_FluidWaterDetailN·T_TD_FluidFoamNoise·T_TD_FluidFlowPhaseNoise·T_TD_FluidCaustics, /Game/FluidFX/Meshes/SM_TD_FluidWaterGrid64·SM_TD_FluidWaterGridUnit, Saved/FluidFX/fluid_water_material.json
상태: 검증 중(2026-10-19). 기본 경로는 단일층 물(legacy = MSM_SINGLE_LAYER_WATER, 굴절 Pixel Normal Offset, 흡수·산란 계수는 1/cm 그대로 = 엔진이 cm 깊이에 곱함, SingleLayerWaterShading.ush 206·221행). SLW 가 안 보이던 것은 아래를 보는 격자 탓이었다(L-editor-40·41 정정). 코스틱 텍스처 min 결합(ColorScaleBehindWater)·미세 법선·해 반짝임 방출(SkyAtmosphereLightDirection·Illuminance)·레이스 거품 문턱. {"water_path": "substrate"|"translucent"} 로 다른 경로(반투명은 두께 = SceneDepth − PixelDepth). C1 추가분: RibbonTangentWS·RibbonFlowCmPerSec(CPD 0번)·MID_DRIVEN_* 계약 대조. 기존 에셋은 {"overwrite": true} 일 때만 다시 짓고(격자는 지운 뒤 새로), 물 무대가 열려 있으면 불 무대로 옮긴 뒤 짓는다. 미저장 작업이 있으면 중단한다. W-2r 추가분(새 파라미터 기본값은 기존 화면과 같음): 거품 문턱(FoamDensityThreshold·FoamFromDensity·FoamSpeedThreshold)·프루드 수 흰 물(FoamFromFroude·FoamFroudeThreshold)·카메라 거리 거품 페이드(FoamFadeStartCm·FoamFadeLengthCm)·선택 밀도 염료(DyeColor·DyeOpacity)·리본 로컬 UV 정적 스위치 3개(bRibbonLocalFlowDetail·Caustics·Tiling, RibbonSegmentLengthCm·RibbonWidthCm·RibbonStretchAlong, 인스턴스 "switches"), 강 MI 청록·거품 재조정.
"""
import hashlib
import json
import os
import re

import unreal


FLUID_ROOT = "/Game/FluidFX"
MATERIAL_DIR = f"{FLUID_ROOT}/Materials"
TEXTURE_DIR = f"{FLUID_ROOT}/Textures"
MESH_DIR = f"{FLUID_ROOT}/Meshes"
WATER_MASTER_NAME = "M_TD_FluidWater"
LITE_MASTER_NAME = "M_TD_FluidWaterLite"
DECAL_PROBE_NAME = "M_TD_FluidWaterDecalProbe"
WATER_MASTER_PATH = f"{MATERIAL_DIR}/{WATER_MASTER_NAME}"
LITE_MASTER_PATH = f"{MATERIAL_DIR}/{LITE_MASTER_NAME}"
DECAL_PROBE_PATH = f"{MATERIAL_DIR}/{DECAL_PROBE_NAME}"
WET_DECAL_NAME = "M_TD_FluidWetnessDecal"
WET_DECAL_PATH = f"{MATERIAL_DIR}/{WET_DECAL_NAME}"

SHADER_DIRECTORY = ("Tools", "FluidFX", "Shaders")
COMMON_HLSL_NAME = "TDFluidWaterCommon.hlsl"
SURFACE_HLSL_NAME = "TDFluidWaterSurface.hlsl"
OFFSET_HLSL_NAME = "TDFluidWaterOffset.hlsl"
WET_DECAL_HLSL_NAME = "TDFluidWaterWetDecal.hlsl"
TEXTURE_SOURCE_DIRECTORY = ("Saved", "FluidFX", "water_textures")
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_water_material_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_water_material.json")

DEFAULT_TEXTURE_PATHS = ("/Engine/EngineResources/Black", "/Engine/EngineResources/DefaultTexture")
PARAMETER_GROUP = "TDFluidWater"
LEGACY_WATER_PATH = "legacy"
TRANSLUCENT_WATER_PATH = "translucent"

TEXTURE_SPECS = (
    {"name": "T_TD_FluidWaterDetailN", "source": "T_TD_FluidWaterDetailN.png", "kind": "normal"},
    {"name": "T_TD_FluidFoamNoise", "source": "T_TD_FluidFoamNoise.png", "kind": "linear"},
    {"name": "T_TD_FluidFlowPhaseNoise", "source": "T_TD_FluidFlowPhaseNoise.png", "kind": "linear"},
    {"name": "T_TD_FluidCaustics", "source": "T_TD_FluidCaustics.png", "kind": "linear"},
    {"name": "T_TD_FluidSandRipplesN", "source": "T_TD_FluidSandRipplesN.png", "kind": "normal"},
    {"name": "T_TD_FluidSandDetail", "source": "T_TD_FluidSandDetail.png", "kind": "linear"},
)
OWN_TEXTURE_PARAMETERS = {
    "WaterDetailNormalTexture": "T_TD_FluidWaterDetailN",
    "FoamNoiseTexture": "T_TD_FluidFoamNoise",
    "FlowPhaseNoiseTexture": "T_TD_FluidFlowPhaseNoise",
    "CausticsTexture": "T_TD_FluidCaustics",
}
SIMULATION_TEXTURE_PARAMETERS = ("DensityPressureTexture", "VelocityTexture", "WaterSurfaceTexture")
TEXTURE_PARAMETER_NAMES = SIMULATION_TEXTURE_PARAMETERS + tuple(OWN_TEXTURE_PARAMETERS)

WATER_STAGE_MAP_PATH = "/Game/FluidFX/Maps/LV_TDFluidWaterStage"
RELEASE_MAP_PATH = "/Game/FluidFX/Maps/LV_TDFluidFireStage"
GRID_DEFAULT_MATERIAL_PATH = "/Engine/EngineMaterials/WorldGridMaterial"
MESH_SPECS = (
    {"name": "SM_TD_FluidWaterGrid64", "size_cm": 1600.0, "quads": 64},
    {"name": "SM_TD_FluidWaterGridUnit", "size_cm": 100.0, "quads": 192},
)

VECTOR_PARAMETER_DEFAULTS = (
    ("SimCenterWorld", (0.0, 0.0, 0.0, 1.0)),
    ("SimExtentsCm", (3600.0, 3600.0, 0.1, 0.0)),
    ("WaterExtinctionPerCm", (0.004, 0.0009, 0.0006, 0.0)),
    ("WaterScatteringAlbedo", (0.03, 0.3, 0.6, 0.0)),
    ("FoamColor", (0.85, 0.88, 0.86, 0.0)),
    ("LiteBedColor", (0.18, 0.15, 0.1, 0.0)),
    ("DetailNormalTilingCm", (140.0, 430.0, 0.0, 0.0)),
    ("DyeColor", (0.3, 0.02, 0.015, 0.0)),
    ("WetDarkColor", (0.09, 0.07, 0.045, 0.0)),
)
SCALAR_PARAMETER_DEFAULTS = (
    ("SimGridResolution", 0.0),
    ("FluidWaterLayerActive", 0.0),
    ("WaterDepthReferenceCm", 300.0),
    ("RippleHeightReferenceCm", 8.0),
    ("VelocitySpeedReferenceCmPerSec", 600.0),
    ("CoastWavePhase", 0.0),
    ("CoastWaveDepthSpacingCm", 60.0),
    ("CoastWaveMaxDepthCm", 250.0),
    ("SwashHeightCm", 0.0),
    ("WaterPhaseG", 0.1),
    ("WaterRoughness", 0.04),
    ("WaterRoughnessMin", 0.02),
    ("SpecularScale", 0.5),
    ("FoamRoughness", 0.65),
    ("FoamTilingCm", 260.0),
    ("FoamFromSpeed", 0.6),
    ("ShoreFoamDepthCm", 40.0),
    ("ShoreFoamAmount", 0.25),
    ("FoamEdgeSoftness", 0.25),
    ("FoamCoverageMax", 0.7),
    ("CoastFoamThreshold", 0.6),
    ("CoastFoamStrength", 1.0),
    ("CoastBreakupAmount", 0.5),
    ("CoastBreakupScaleCm", 800.0),
    ("DetailNormalStrength", 0.35),
    ("MicroNormalTilingCm", 38.0),
    ("MicroNormalStrength", 0.25),
    ("GlintSharpness", 5000.0),
    ("GlintHotspotSharpness", 12.0),
    ("GlintIntensity", 12.0),
    ("RippleNormalStrength", 1.0),
    ("PressureNormalStrength", 0.0),
    ("FlowPhasePeriodSeconds", 1.4),
    ("MaxFlowStretch", 0.5),
    ("PassiveWaveDirectionDegrees", 20.0),
    ("PassiveWaveAmplitudeCm", 3.0),
    ("WhitecapJacobianThreshold", 0.4),
    ("CausticsStrength", 1.0),
    ("CausticsTilingCm", 220.0),
    ("CausticsMaxDepthCm", 320.0),
    ("CausticsOnsetDepthCm", 10.0),
    ("CausticsDriftCmPerSecond", 6.0),
    ("CausticsMeanLevel", 0.48),
    ("WaterRefractionIndex", 1.33),
    ("EdgeSoftnessCm", 12.0),
    ("LiteFallbackThicknessCm", 300.0),
    ("RibbonFlowCmPerSec", 0.0),
    ("FoamDensityThreshold", 0.0),
    ("FoamFromDensity", 1.0),
    ("FoamSpeedThreshold", 0.0),
    ("FoamFromFroude", 0.0),
    ("FoamFroudeThreshold", 1.0),
    ("FoamFadeStartCm", 1000000.0),
    ("FoamFadeLengthCm", 2000.0),
    ("DyeOpacity", 0.0),
    ("RibbonSegmentLengthCm", 400.0),
    ("RibbonWidthCm", 800.0),
    ("RibbonStretchAlong", 1.0),
    ("WetRoughness", 0.25),
    ("WetOpacity", 0.45),
    ("WetSubmergedFadeCm", 2.0),
)
PRIMITIVE_DATA_INDEX_BY_PARAMETER = {"RibbonFlowCmPerSec": 0}
STATIC_SWITCH_DEFAULTS = (
    ("bUseCaustics", True),
    ("bUseSecondDetailOctave", True),
    ("bRibbonLocalFlowDetail", False),
    ("bRibbonLocalCaustics", False),
    ("bRibbonLocalTiling", False),
)
STATIC_SWITCH_NAMES = tuple(name for name, _ in STATIC_SWITCH_DEFAULTS)
BUILTIN_VECTOR_SOURCE_NAMES = ("WaterWorldPosition", "CameraWorldPosition", "RibbonTangentWS", "SunDirectionWS", "SunIlluminance")
BUILTIN_FLOAT2_SOURCE_NAMES = ("RibbonUv",)
BUILTIN_SOURCE_NAMES = BUILTIN_VECTOR_SOURCE_NAMES + BUILTIN_FLOAT2_SOURCE_NAMES + (
    "PixelDepthCm",
    "SceneDepthWithoutWaterCm",
    "ElapsedSeconds",
    "LiteDepthMode",
)
MID_DRIVEN_TEXTURE_NAMES = ("DensityPressureTexture", "VelocityTexture", "WaterSurfaceTexture")
MID_DRIVEN_VECTOR_NAMES = ("SimCenterWorld", "SimExtentsCm")
MID_DRIVEN_SCALAR_NAMES = (
    "SimGridResolution",
    "WaterDepthReferenceCm",
    "RippleHeightReferenceCm",
    "VelocitySpeedReferenceCmPerSec",
    "FluidWaterLayerActive",
    "CoastWavePhase",
    "CoastWaveDepthSpacingCm",
    "CoastWaveMaxDepthCm",
    "SwashHeightCm",
)
DECAL_ONLY_MID_SCALAR_NAMES = ("TerrainReferenceZCm",)

COMMON_INPUT_NAMES = (
    "DensityPressureTexture",
    "WaterSurfaceTexture",
    "SimCenterWorld",
    "SimExtentsCm",
    "SimGridResolution",
    "WaterWorldPosition",
    "ElapsedSeconds",
    "FluidWaterLayerActive",
    "PassiveWaveAmplitudeCm",
    "PassiveWaveDirectionDegrees",
    "PressureNormalStrength",
    "RippleHeightReferenceCm",
)
OFFSET_INPUT_NAMES = COMMON_INPUT_NAMES + (
    "SwashHeightCm",
    "WaterDepthReferenceCm",
    "CoastWaveMaxDepthCm",
    "CoastWaveDepthSpacingCm",
    "CoastWavePhase",
)
SURFACE_INPUT_NAMES = COMMON_INPUT_NAMES + (
    "VelocityTexture",
    "FlowPhaseNoiseTexture",
    "WaterDetailNormalTexture",
    "FoamNoiseTexture",
    "SceneDepthWithoutWaterCm",
    "PixelDepthCm",
    "CameraWorldPosition",
    "VelocitySpeedReferenceCmPerSec",
    "WaterDepthReferenceCm",
    "RibbonTangentWS",
    "RibbonFlowCmPerSec",
    "RibbonUv",
    "RibbonSegmentLengthCm",
    "RibbonWidthCm",
    "RibbonStretchAlong",
    "bRibbonLocalFlowDetail",
    "bRibbonLocalCaustics",
    "bRibbonLocalTiling",
    "FlowPhasePeriodSeconds",
    "MaxFlowStretch",
    "DetailNormalTilingCm",
    "bUseSecondDetailOctave",
    "DetailNormalStrength",
    "MicroNormalTilingCm",
    "MicroNormalStrength",
    "RippleNormalStrength",
    "FoamTilingCm",
    "ShoreFoamDepthCm",
    "ShoreFoamAmount",
    "FoamEdgeSoftness",
    "FoamCoverageMax",
    "CoastWaveMaxDepthCm",
    "CoastWaveDepthSpacingCm",
    "CoastWavePhase",
    "CoastFoamThreshold",
    "CoastFoamStrength",
    "CoastBreakupScaleCm",
    "CoastBreakupAmount",
    "WhitecapJacobianThreshold",
    "FoamFromSpeed",
    "FoamSpeedThreshold",
    "FoamDensityThreshold",
    "FoamFromDensity",
    "FoamFromFroude",
    "FoamFroudeThreshold",
    "FoamFadeStartCm",
    "FoamFadeLengthCm",
    "DyeColor",
    "DyeOpacity",
    "FoamColor",
    "bUseCaustics",
    "CausticsTexture",
    "CausticsTilingCm",
    "CausticsMaxDepthCm",
    "CausticsStrength",
    "CausticsOnsetDepthCm",
    "CausticsDriftCmPerSecond",
    "CausticsMeanLevel",
    "SunDirectionWS",
    "SunIlluminance",
    "GlintSharpness",
    "GlintHotspotSharpness",
    "GlintIntensity",
    "EdgeSoftnessCm",
    "WaterRoughness",
    "WaterRoughnessMin",
    "FoamRoughness",
    "SpecularScale",
    "WaterExtinctionPerCm",
    "WaterScatteringAlbedo",
    "LiteBedColor",
    "LiteDepthMode",
    "LiteFallbackThicknessCm",
)
SURFACE_OUTPUT_SPECS = (
    ("OutNormalWS", "CMOT_FLOAT3"),
    ("OutBaseColor", "CMOT_FLOAT3"),
    ("OutRoughness", "CMOT_FLOAT1"),
    ("OutSpecular", "CMOT_FLOAT1"),
    ("OutTopOpacity", "CMOT_FLOAT1"),
    ("OutColorScaleBehindWater", "CMOT_FLOAT3"),
    ("OutLiteColor", "CMOT_FLOAT3"),
    ("OutTranslucentOpacity", "CMOT_FLOAT1"),
    ("OutTranslucentColor", "CMOT_FLOAT3"),
    ("OutEmissive", "CMOT_FLOAT3"),
)
COMMON_SAMPLER_NAMES = ("DensityPressureTextureSampler", "WaterSurfaceTextureSampler")
SURFACE_SAMPLER_NAMES = COMMON_SAMPLER_NAMES + (
    "VelocityTextureSampler",
    "FlowPhaseNoiseTextureSampler",
    "WaterDetailNormalTextureSampler",
    "FoamNoiseTextureSampler",
    "CausticsTextureSampler",
)
OFFSET_SAMPLER_NAMES = COMMON_SAMPLER_NAMES
SURFACE_ENGINE_SYMBOLS = ("GradientNoise3D_ALU", "Texture2DSample", "Texture2DSampleLevel")
WET_DECAL_INPUT_NAMES = (
    "WaterSurfaceTexture",
    "WaterWorldPosition",
    "SimCenterWorld",
    "SimExtentsCm",
    "SimGridResolution",
    "WaterDepthReferenceCm",
    "WetSubmergedFadeCm",
    "WetOpacity",
)
WET_DECAL_SAMPLER_NAMES = ("WaterSurfaceTextureSampler",)
WET_DECAL_ENGINE_SYMBOLS = ("Texture2DSampleLevel",)
OFFSET_ENGINE_SYMBOLS = ("Texture2DSampleLevel",)

SUBSTRATE_PIN_CANDIDATES = {
    "BaseColor": ("BaseColor", "Base Color"),
    "Specular": ("Specular",),
    "Roughness": ("Roughness",),
    "Normal": ("Normal",),
    "TopMaterialOpacity": ("TopMaterialOpacity", "Top Material Opacity"),
    "WaterAlbedo": ("WaterAlbedo", "Water Albedo"),
    "WaterExtinction": ("WaterExtinction", "Water Extinction"),
    "WaterPhaseG": ("WaterPhaseG", "Water Phase G"),
    "ColorScaleBehindWater": ("ColorScaleBehindWater", "Color Scale Behind Water"),
    "EmissiveColor": ("EmissiveColor", "Emissive Color", "Emissive"),
}
LEGACY_WATER_OUTPUT_PIN_CANDIDATES = {
    "ScatteringCoefficients": ("ScatteringCoefficients", "Scattering Coefficients"),
    "AbsorptionCoefficients": ("AbsorptionCoefficients", "Absorption Coefficients"),
    "PhaseG": ("PhaseG", "Phase G"),
    "ColorScaleBehindWater": ("ColorScaleBehindWater", "Color Scale Behind Water"),
}

INSTANCE_SPECS = (
    {
        "name": "MI_TD_FluidWater_River",
        "vectors": {
            "WaterExtinctionPerCm": (0.028, 0.0068, 0.0042),
            "WaterScatteringAlbedo": (0.04, 0.3, 0.45),
            "DetailNormalTilingCm": (200.0, 640.0, 0.0),
        },
        "scalars": {
            "PassiveWaveAmplitudeCm": 1.5,
            "CoastFoamStrength": 0.0,
            "ShoreFoamDepthCm": 6.0,
            "ShoreFoamAmount": 0.7,
            "EdgeSoftnessCm": 3.0,
            "FoamFromSpeed": 0.0,
            "FoamDensityThreshold": 0.08,
            "FoamFromDensity": 1.6,
            "FoamFromFroude": 0.32,
            "FoamFroudeThreshold": 1.2,
            "FoamCoverageMax": 0.7,
            "FoamTilingCm": 180.0,
            "FoamFadeStartCm": 2200.0,
            "FoamFadeLengthCm": 2000.0,
            "DetailNormalStrength": 0.22,
            "MicroNormalStrength": 0.12,
            "MicroNormalTilingCm": 30.0,
            "WaterRoughness": 0.03,
            "GlintSharpness": 1500.0,
            "GlintHotspotSharpness": 6.0,
            "GlintIntensity": 10.0,
            "CausticsStrength": 0.45,
            "CausticsTilingCm": 180.0,
            "CausticsMaxDepthCm": 160.0,
            "FlowPhasePeriodSeconds": 1.0,
            "RibbonSegmentLengthCm": 395.0,
            "RibbonWidthCm": 800.0,
            "RibbonStretchAlong": 3.0,
            "DyeOpacity": 0.8,
        },
        "switches": {"bRibbonLocalFlowDetail": True, "bRibbonLocalCaustics": True, "bRibbonLocalTiling": True},
    },
    {
        "name": "MI_TD_FluidWater_Lake",
        "vectors": {
            "WaterExtinctionPerCm": (0.006, 0.0016, 0.0011),
            "WaterScatteringAlbedo": (0.05, 0.35, 0.55),
            "DetailNormalTilingCm": (320.0, 900.0, 0.0),
        },
        "scalars": {
            "PassiveWaveAmplitudeCm": 3.0,
            "CoastFoamStrength": 0.0,
            "ShoreFoamDepthCm": 8.0,
            "ShoreFoamAmount": 0.15,
            "DetailNormalStrength": 0.17,
            "MicroNormalStrength": 0.07,
            "CausticsStrength": 1.6,
            "CausticsTilingCm": 340.0,
            "FoamCoverageMax": 0.85,
            "FoamFromSpeed": 1.1,
        },
    },
    {
        "name": "MI_TD_FluidWater_Sea",
        "vectors": {"WaterExtinctionPerCm": (0.004, 0.0009, 0.0006), "WaterScatteringAlbedo": (0.03, 0.3, 0.6)},
        "scalars": {"PassiveWaveAmplitudeCm": 12.0, "ShoreFoamDepthCm": 25.0},
    },
    {
        "name": "MI_TD_FluidWater_SeaStorm",
        "vectors": {"WaterExtinctionPerCm": (0.009, 0.0035, 0.003), "WaterScatteringAlbedo": (0.08, 0.35, 0.45)},
        "scalars": {"PassiveWaveAmplitudeCm": 40.0, "ShoreFoamDepthCm": 25.0},
    },
    {
        "name": "MI_TD_FluidWater_Swamp",
        "vectors": {
            "WaterExtinctionPerCm": (0.02, 0.018, 0.026),
            "WaterScatteringAlbedo": (0.32, 0.3, 0.16),
            "FoamColor": (0.55, 0.52, 0.4),
            "DetailNormalTilingCm": (320.0, 900.0, 0.0),
        },
        "scalars": {
            "PassiveWaveAmplitudeCm": 0.5,
            "CoastFoamStrength": 0.0,
            "ShoreFoamDepthCm": 4.0,
            "ShoreFoamAmount": 0.3,
            "EdgeSoftnessCm": 3.0,
            "FoamCoverageMax": 0.4,
            "DetailNormalStrength": 0.08,
            "MicroNormalStrength": 0.03,
            "WaterRoughness": 0.08,
            "CausticsStrength": 0.0,
            "GlintIntensity": 4.0,
        },
    },
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
    "MaterialEditingLibrary.set_material_instance_vector_parameter_value",
    "MaterialEditingLibrary.set_material_instance_static_switch_parameter_value",
    "MaterialEditingLibrary.get_material_instance_static_switch_parameter_value",
    "MaterialEditingLibrary.get_material_instance_scalar_parameter_value",
    "MaterialEditingLibrary.update_material_instance",
    "EditorLoadingAndSavingUtils.get_dirty_map_packages",
    "EditorLoadingAndSavingUtils.get_dirty_content_packages",
    "SystemLibrary.get_project_directory",
    "AssetImportTask",
    "StaticMesh",
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
    "MaterialExpressionPixelDepth",
    "MaterialExpressionVertexTangentWS",
    "MaterialExpressionTextureCoordinate",
    "MaterialExpressionSkyAtmosphereLightDirection",
    "MaterialExpressionSkyAtmosphereLightIlluminance",
    "MaterialProperty.MP_EMISSIVE_COLOR",
    "MaterialExpressionSceneDepthWithoutWater",
    "MaterialExpressionTime",
    "MaterialExpressionConstant",
    "MaterialExpressionConstant3Vector",
    "MaterialExpressionTextureObjectParameter",
    "MaterialExpressionVectorParameter",
    "MaterialExpressionScalarParameter",
    "MaterialProperty.MP_WORLD_POSITION_OFFSET",
    "MaterialProperty.MP_BASE_COLOR",
    "MaterialProperty.MP_SPECULAR",
    "MaterialProperty.MP_ROUGHNESS",
    "MaterialProperty.MP_NORMAL",
    "MaterialProperty.MP_OPACITY",
    "MaterialDomain.MD_DEFERRED_DECAL",
    "BlendMode.BLEND_TRANSLUCENT",
    "MaterialSamplerType.SAMPLERTYPE_COLOR",
    "MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR",
    "MaterialSamplerType.SAMPLERTYPE_NORMAL",
    "TextureCompressionSettings.TC_DEFAULT",
    "TextureCompressionSettings.TC_NORMALMAP",
)
SUBSTRATE_REQUIRED_NAMES = ("MaterialExpressionSubstrateSingleLayerWaterBSDF", "MaterialProperty.MP_FRONT_MATERIAL")
TRANSLUCENT_REQUIRED_NAMES = (
    "MaterialExpressionSceneDepth",
    "TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING",
    "BlendMode.BLEND_TRANSLUCENT",
)
LEGACY_REQUIRED_NAMES = (
    "MaterialExpressionSingleLayerWaterMaterialOutput",
    "MaterialExpressionMultiply",
    "MaterialExpressionOneMinus",
    "MaterialShadingModel.MSM_SINGLE_LAYER_WATER",
)
PHASE_ZERO_RESULTS = {}

IDENTIFIER_PATTERN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
DEFINE_PATTERN = re.compile(r"^[ \t]*#define[ \t]+(TD_WATER_[A-Za-z0-9_]+)", re.MULTILINE)


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


def find_missing_unreal_names(names):
    missing = []
    for dotted_name in names:
        target = unreal
        for part in dotted_name.split("."):
            target = getattr(target, part, None)
            if target is None:
                missing.append(dotted_name)
                break
    return missing


def read_text_file(path):
    if not os.path.isfile(path):
        raise RuntimeError(f"HLSL 파일 없음: {path}")
    with open(path, encoding="utf-8") as text_file:
        return text_file.read().replace("\r\n", "\n").strip("\n") + "\n"


def read_shader_sources(project_root):
    shader_root = os.path.join(project_root, *SHADER_DIRECTORY)
    return {
        "common": read_text_file(os.path.join(shader_root, COMMON_HLSL_NAME)),
        "surface": read_text_file(os.path.join(shader_root, SURFACE_HLSL_NAME)),
        "offset": read_text_file(os.path.join(shader_root, OFFSET_HLSL_NAME)),
    }


def assemble_node_code(common_code, node_code, is_vertex):
    body = common_code + "\n" + node_code
    macro_names = sorted(set(DEFINE_PATTERN.findall(body)) | {"TD_WATER_VERTEX"})
    undefine_block = "".join(f"#undef {name}\n" for name in macro_names)
    vertex_flag = 1 if is_vertex else 0
    return undefine_block + f"#define TD_WATER_VERTEX {vertex_flag}\n" + body + "\n" + undefine_block


def assemble_all_node_codes(sources):
    return {
        "surface": assemble_node_code(sources["common"], sources["surface"], False),
        "offset": assemble_node_code(sources["common"], sources["offset"], True),
    }


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


def validate_hlsl_code(code, input_names, sampler_names, engine_symbols, output_names):
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
    unassigned_outputs = [name for name in output_names if name not in identifiers]
    if unassigned_outputs:
        problems.append("HLSL이 채우지 않는 추가 출력: " + ", ".join(unassigned_outputs))
    bracket_problem = find_unbalanced_brackets(code)
    if bracket_problem:
        problems.append(bracket_problem)
    if "return" not in identifiers:
        problems.append("HLSL에 return 이 없음")
    if "//" in code or "/*" in code:
        problems.append("HLSL에 주석이 있음")
    return problems


def validate_all_node_codes(codes):
    surface_problems = validate_hlsl_code(
        codes["surface"],
        SURFACE_INPUT_NAMES,
        SURFACE_SAMPLER_NAMES,
        SURFACE_ENGINE_SYMBOLS,
        [name for name, _ in SURFACE_OUTPUT_SPECS],
    )
    offset_problems = validate_hlsl_code(codes["offset"], OFFSET_INPUT_NAMES, OFFSET_SAMPLER_NAMES, OFFSET_ENGINE_SYMBOLS, ())
    return ["표면: " + item for item in surface_problems] + ["오프셋: " + item for item in offset_problems]


def read_request():
    request_path = project_path(*REQUEST_RELATIVE_PATH)
    if not os.path.isfile(request_path):
        return {}
    with open(request_path, encoding="utf-8") as request_file:
        request = json.load(request_file)
    return request if isinstance(request, dict) else {}


def asset_path(name):
    return f"{MATERIAL_DIR}/{name}"


def collect_target_asset_paths():
    paths = [WATER_MASTER_PATH, LITE_MASTER_PATH, DECAL_PROBE_PATH, WET_DECAL_PATH]
    paths.extend(asset_path(spec["name"]) for spec in INSTANCE_SPECS)
    paths.extend(f"{TEXTURE_DIR}/{spec['name']}" for spec in TEXTURE_SPECS)
    paths.extend(f"{MESH_DIR}/{spec['name']}" for spec in MESH_SPECS)
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
    if compression == unreal.TextureCompressionSettings.TC_NORMALMAP:
        return unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
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


def import_texture(spec, overwrite):
    source_path = project_path(*TEXTURE_SOURCE_DIRECTORY, spec["source"])
    if not os.path.isfile(source_path):
        raise RuntimeError(f"텍스처 원본 없음(python Tools/FluidFX/make_fluid_water_textures.py 먼저): {source_path}")
    path = f"{TEXTURE_DIR}/{spec['name']}"
    if unreal.EditorAssetLibrary.does_asset_exist(path) and not overwrite:
        raise RuntimeError(f"기존 에셋 보존: {path}")
    ensure_directory(TEXTURE_DIR)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", source_path)
    task.set_editor_property("destination_path", TEXTURE_DIR)
    task.set_editor_property("destination_name", spec["name"])
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", False)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.EditorAssetLibrary.load_asset(path)
    if texture is None:
        raise RuntimeError(f"텍스처 가져오기 실패: {path}")
    texture.set_editor_property("srgb", False)
    if spec["kind"] == "normal":
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    else:
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    save_asset(texture, path)
    sampler_type = choose_sampler_type(texture)
    if sampler_type is None:
        raise RuntimeError(f"텍스처 샘플러 형식을 정할 수 없음: {path}")
    return texture, sampler_type


def create_grid_mesh(spec, overwrite):
    path = f"{MESH_DIR}/{spec['name']}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        if not overwrite:
            raise RuntimeError(f"기존 에셋 보존: {path}")
        if not unreal.EditorAssetLibrary.delete_asset(path):
            raise RuntimeError(f"기존 격자 삭제 실패(참조 중인 맵을 닫고 다시 실행): {path}")
    ensure_directory(MESH_DIR)
    mesh = unreal.TDFluidWaterMeshEditorLibrary.create_grid_static_mesh(
        MESH_DIR, spec["name"], spec["size_cm"], spec["quads"], unreal.load_asset(GRID_DEFAULT_MATERIAL_PATH)
    )
    if not isinstance(mesh, unreal.StaticMesh):
        raise RuntimeError(f"격자 메시 생성 실패(UTDFluidWaterMeshEditorLibrary): {path}")
    vertex_total = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).get_number_verts(mesh, 0)
    expected_vertices = (spec["quads"] + 1) ** 2
    if vertex_total != expected_vertices:
        raise RuntimeError(f"격자 메시 정점 수 불일치: {path} {vertex_total} != {expected_vertices}")
    save_asset(mesh, path)
    bounds = mesh.get_bounds()
    return {
        "asset": path,
        "size_cm": spec["size_cm"],
        "quads": spec["quads"],
        "render_vertices": vertex_total,
        "bounds_extent": [bounds.box_extent.x, bounds.box_extent.y, bounds.box_extent.z],
    }


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


def link_first_matching_pin(source, source_output, target, candidates):
    for candidate in candidates:
        if unreal.MaterialEditingLibrary.connect_material_expressions(source, source_output, target, candidate):
            return candidate
    raise RuntimeError(f"재질 핀 연결 실패: {source.get_name()}[{source_output}] -> {target.get_name()} 후보 {list(candidates)}")


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


def make_custom_input(input_name):
    custom_input = unreal.CustomInput()
    custom_input.set_editor_property("input_name", input_name)
    return custom_input


def make_custom_output(output_name, output_type_name):
    custom_output = unreal.CustomOutput()
    custom_output.set_editor_property("output_name", output_name)
    custom_output.set_editor_property("output_type", getattr(unreal.CustomMaterialOutputType, output_type_name))
    return custom_output


def build_custom_node(material, description, output_type, input_names, code, x, y, output_specs=()):
    custom = add_node(material, unreal.MaterialExpressionCustom, x, y)
    custom.set_editor_property("description", description)
    custom.set_editor_property("output_type", output_type)
    custom.set_editor_property("inputs", [make_custom_input(name) for name in input_names])
    if output_specs:
        custom.set_editor_property("additional_outputs", [make_custom_output(name, kind) for name, kind in output_specs])
    custom.set_editor_property("code", code)
    stored_names = [str(item.get_editor_property("input_name")) for item in custom.get_editor_property("inputs")]
    if stored_names != list(input_names):
        raise RuntimeError("Custom 노드 입력 이름 설정 확인 실패: " + ", ".join(stored_names))
    stored_outputs = [str(item.get_editor_property("output_name")) for item in custom.get_editor_property("additional_outputs")]
    if stored_outputs != [name for name, _ in output_specs]:
        raise RuntimeError("Custom 노드 추가 출력 이름 설정 확인 실패: " + ", ".join(stored_outputs))
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


def release_water_stage_world():
    editor_world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if not editor_world.get_path_name().startswith(WATER_STAGE_MAP_PATH):
        return
    if not unreal.EditorAssetLibrary.does_asset_exist(RELEASE_MAP_PATH):
        raise RuntimeError(f"물 무대를 닫을 대체 맵이 없음: {RELEASE_MAP_PATH}")
    if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(RELEASE_MAP_PATH):
        raise RuntimeError(f"대체 맵 열기 실패: {RELEASE_MAP_PATH}")
    unreal.SystemLibrary.collect_garbage()


def delete_existing_instances():
    for spec in INSTANCE_SPECS:
        path = asset_path(spec["name"])
        if unreal.EditorAssetLibrary.does_asset_exist(path) and not unreal.EditorAssetLibrary.delete_asset(path):
            raise RuntimeError(f"기존 재질 인스턴스 삭제 실패: {path}")


def prepare_master_material(master_name, master_path, overwrite):
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(master_path):
        if not overwrite:
            raise RuntimeError(f"기존 에셋 보존: {master_path}")
        material = assets.load_asset(master_path)
        if not isinstance(material, unreal.Material):
            raise RuntimeError(f"기존 에셋 형식 불일치: {master_path}")
        if assets.delete_asset(master_path):
            return create_master_asset(master_name, master_path)
        material = assets.load_asset(master_path)
        if not isinstance(material, unreal.Material):
            raise RuntimeError(f"마스터 재질 삭제가 반쯤 실패해 다시 불러오지 못함(에디터 재시작 필요): {master_path}")
        clear_all_expressions(material)
        return material, False

    return create_master_asset(master_name, master_path)


def clear_all_expressions(material):
    editing = unreal.MaterialEditingLibrary
    editing.delete_all_material_expressions(material)
    for expression in list(editing.get_material_expressions(material)):
        editing.delete_material_expression(material, expression)
    remaining = editing.get_num_material_expressions(material)
    if remaining:
        raise RuntimeError(f"기존 재질 표현식 {remaining}개를 지우지 못함: {material.get_path_name()}")


def create_master_asset(master_name, master_path):
    ensure_directory(MATERIAL_DIR)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        master_name, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew()
    )
    if material is None:
        raise RuntimeError(f"마스터 재질 생성 실패: {master_path}")
    return material, True


class SourceFactory:
    def __init__(self, material, texture_defaults, is_lite, water_path=LEGACY_WATER_PATH):
        self.material = material
        self.texture_defaults = texture_defaults
        self.is_lite = is_lite
        self.water_path = water_path
        self.sources = {}
        self.next_row = 0
        self.vector_defaults = dict(VECTOR_PARAMETER_DEFAULTS)
        self.scalar_defaults = dict(SCALAR_PARAMETER_DEFAULTS)
        self.switch_defaults = dict(STATIC_SWITCH_DEFAULTS)
        self.used_static_switches = []

    def place(self):
        y = -1500 + self.next_row * 110
        self.next_row += 1
        return -1300, y

    def get(self, name):
        if name not in self.sources:
            self.sources[name] = self.create(name)
        return self.sources[name]

    def create(self, name):
        material = self.material
        x, y = self.place()
        if name == "WaterWorldPosition":
            return truncate_if_available(material, add_node(material, unreal.MaterialExpressionWorldPosition, x - 200, y), x, y), ""
        if name == "CameraWorldPosition":
            return truncate_if_available(material, add_node(material, unreal.MaterialExpressionCameraPositionWS, x - 200, y), x, y), ""
        if name == "PixelDepthCm":
            return add_node(material, unreal.MaterialExpressionPixelDepth, x, y), ""
        if name == "RibbonTangentWS":
            return add_node(material, unreal.MaterialExpressionVertexTangentWS, x, y), ""
        if name == "RibbonUv":
            return add_node(material, unreal.MaterialExpressionTextureCoordinate, x, y, coordinate_index=0), ""
        if name == "SunDirectionWS":
            return add_node(material, unreal.MaterialExpressionSkyAtmosphereLightDirection, x, y), ""
        if name == "SunIlluminance":
            return add_node(material, unreal.MaterialExpressionSkyAtmosphereLightIlluminance, x, y), ""
        if name == "ElapsedSeconds":
            return add_node(material, unreal.MaterialExpressionTime, x, y), ""
        if name == "SceneDepthWithoutWaterCm":
            if self.is_lite:
                return add_node(material, unreal.MaterialExpressionConstant, x, y, r=0.0), ""
            if self.water_path == TRANSLUCENT_WATER_PATH:
                return add_node(material, unreal.MaterialExpressionSceneDepth, x, y), ""
            return add_node(material, unreal.MaterialExpressionSceneDepthWithoutWater, x, y), ""
        if name == "LiteDepthMode":
            return add_node(material, unreal.MaterialExpressionConstant, x, y, r=1.0 if self.is_lite else 0.0), ""
        if name in self.texture_defaults:
            texture, sampler_type = self.texture_defaults[name]
            node = add_node(
                material,
                unreal.MaterialExpressionTextureObjectParameter,
                x,
                y,
                parameter_name=name,
                texture=texture,
                sampler_type=sampler_type,
            )
            assign_parameter_group(node)
            return node, ""
        if name in self.vector_defaults:
            node = add_node(
                material,
                unreal.MaterialExpressionVectorParameter,
                x,
                y,
                parameter_name=name,
                default_value=unreal.LinearColor(*self.vector_defaults[name]),
            )
            assign_parameter_group(node)
            return node, "RGB"
        if name in self.scalar_defaults:
            properties = {"parameter_name": name, "default_value": self.scalar_defaults[name]}
            if name in PRIMITIVE_DATA_INDEX_BY_PARAMETER:
                properties["use_custom_primitive_data"] = True
                properties["primitive_data_index"] = PRIMITIVE_DATA_INDEX_BY_PARAMETER[name]
            node = add_node(material, unreal.MaterialExpressionScalarParameter, x, y, **properties)
            assign_parameter_group(node)
            return node, ""
        if name in self.switch_defaults:
            return self.create_static_switch(name, x, y), ""
        raise RuntimeError(f"입력 원천을 정할 수 없음: {name}")

    def create_static_switch(self, name, x, y):
        material = self.material
        default_value = False if self.is_lite else self.switch_defaults[name]
        switch_class = getattr(unreal, "MaterialExpressionStaticSwitchParameter", None)
        if switch_class is None:
            node = add_node(
                material, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=1.0 if default_value else 0.0
            )
            assign_parameter_group(node)
            return node
        node = add_node(material, switch_class, x, y, parameter_name=name, default_value=default_value)
        assign_parameter_group(node)
        on_value = add_node(material, unreal.MaterialExpressionConstant, x - 200, y - 20, r=1.0)
        off_value = add_node(material, unreal.MaterialExpressionConstant, x - 200, y + 20, r=0.0)
        link(on_value, node, "True")
        link(off_value, node, "False")
        self.used_static_switches.append(name)
        return node


def link_custom_inputs(custom, input_names, factory):
    for input_name in input_names:
        source, source_output = factory.get(input_name)
        link(source, custom, input_name, source_output)


def connect_substrate_water_outputs(material, surface, factory):
    bsdf = add_node(material, unreal.MaterialExpressionSubstrateSingleLayerWaterBSDF, 400, 0)
    pins = {}
    pins["BaseColor"] = link_first_matching_pin(surface, "OutBaseColor", bsdf, SUBSTRATE_PIN_CANDIDATES["BaseColor"])
    pins["Specular"] = link_first_matching_pin(surface, "OutSpecular", bsdf, SUBSTRATE_PIN_CANDIDATES["Specular"])
    pins["Roughness"] = link_first_matching_pin(surface, "OutRoughness", bsdf, SUBSTRATE_PIN_CANDIDATES["Roughness"])
    pins["Normal"] = link_first_matching_pin(surface, "OutNormalWS", bsdf, SUBSTRATE_PIN_CANDIDATES["Normal"])
    pins["TopMaterialOpacity"] = link_first_matching_pin(surface, "OutTopOpacity", bsdf, SUBSTRATE_PIN_CANDIDATES["TopMaterialOpacity"])
    pins["ColorScaleBehindWater"] = link_first_matching_pin(
        surface, "OutColorScaleBehindWater", bsdf, SUBSTRATE_PIN_CANDIDATES["ColorScaleBehindWater"]
    )
    pins["EmissiveColor"] = link_first_matching_pin(surface, "OutEmissive", bsdf, SUBSTRATE_PIN_CANDIDATES["EmissiveColor"])
    albedo, albedo_output = factory.get("WaterScatteringAlbedo")
    extinction, extinction_output = factory.get("WaterExtinctionPerCm")
    phase, phase_output = factory.get("WaterPhaseG")
    pins["WaterAlbedo"] = link_first_matching_pin(albedo, albedo_output, bsdf, SUBSTRATE_PIN_CANDIDATES["WaterAlbedo"])
    pins["WaterExtinction"] = link_first_matching_pin(extinction, extinction_output, bsdf, SUBSTRATE_PIN_CANDIDATES["WaterExtinction"])
    pins["WaterPhaseG"] = link_first_matching_pin(phase, phase_output, bsdf, SUBSTRATE_PIN_CANDIDATES["WaterPhaseG"])
    connect_material_output(bsdf, unreal.MaterialProperty.MP_FRONT_MATERIAL)
    pins["RayTracingWaterOutput"] = add_water_coefficient_output(material, factory, -100)
    return pins


def connect_pixel_normal_refraction(material, factory):
    refraction_modes = getattr(unreal, "RefractionMode", None)
    pixel_normal_offset = getattr(refraction_modes, "RM_PIXEL_NORMAL_OFFSET", None) if refraction_modes else None
    refraction_property = getattr(unreal.MaterialProperty, "MP_REFRACTION", None)
    if pixel_normal_offset is None or refraction_property is None or not has_editor_property(material, "refraction_method"):
        return "굴절 설정 이름 없음(RefractionMode.RM_PIXEL_NORMAL_OFFSET·MP_REFRACTION·refraction_method)"
    material.set_editor_property("refraction_method", pixel_normal_offset)
    index, index_output = factory.get("WaterRefractionIndex")
    connect_material_output(index, refraction_property, index_output)
    return "Pixel Normal Offset"


def add_water_coefficient_output(material, factory, y):
    water_output = add_node(material, unreal.MaterialExpressionSingleLayerWaterMaterialOutput, 400, y + 400)
    albedo, albedo_output = factory.get("WaterScatteringAlbedo")
    extinction, extinction_output = factory.get("WaterExtinctionPerCm")
    phase, phase_output = factory.get("WaterPhaseG")
    scattering = add_node(material, unreal.MaterialExpressionMultiply, 100, y + 600)
    link(albedo, scattering, "A", albedo_output)
    link(extinction, scattering, "B", extinction_output)
    inverse_albedo = add_node(material, unreal.MaterialExpressionOneMinus, 0, y + 720)
    link(albedo, inverse_albedo, "", albedo_output)
    absorption = add_node(material, unreal.MaterialExpressionMultiply, 100, y + 740)
    link(inverse_albedo, absorption, "A")
    link(extinction, absorption, "B", extinction_output)
    link(scattering, water_output, "ScatteringCoefficients")
    link(absorption, water_output, "AbsorptionCoefficients")
    link(phase, water_output, "PhaseG", phase_output)
    return "ScatteringCoefficients·AbsorptionCoefficients·PhaseG"


def connect_legacy_water_outputs(material, surface, factory):
    connect_material_output(surface, unreal.MaterialProperty.MP_BASE_COLOR, "OutBaseColor")
    connect_material_output(surface, unreal.MaterialProperty.MP_SPECULAR, "OutSpecular")
    connect_material_output(surface, unreal.MaterialProperty.MP_ROUGHNESS, "OutRoughness")
    connect_material_output(surface, unreal.MaterialProperty.MP_NORMAL, "OutNormalWS")
    connect_material_output(surface, unreal.MaterialProperty.MP_OPACITY, "OutTopOpacity")
    connect_material_output(surface, unreal.MaterialProperty.MP_EMISSIVE_COLOR, "OutEmissive")
    water_output = add_node(material, unreal.MaterialExpressionSingleLayerWaterMaterialOutput, 400, 0)
    albedo, albedo_output = factory.get("WaterScatteringAlbedo")
    extinction, extinction_output = factory.get("WaterExtinctionPerCm")
    phase, phase_output = factory.get("WaterPhaseG")
    multiply_class = unreal.MaterialExpressionMultiply
    scattering = add_node(material, multiply_class, 100, 200)
    link(albedo, scattering, "A", albedo_output)
    link(extinction, scattering, "B", extinction_output)
    one_minus_class = unreal.MaterialExpressionOneMinus
    inverse_albedo = add_node(material, one_minus_class, 0, 320)
    link(albedo, inverse_albedo, "", albedo_output)
    absorption = add_node(material, multiply_class, 100, 340)
    link(inverse_albedo, absorption, "A")
    link(extinction, absorption, "B", extinction_output)
    pins = {"Refraction": connect_pixel_normal_refraction(material, factory)}
    pins["ScatteringCoefficients"] = link_first_matching_pin(
        scattering, "", water_output, LEGACY_WATER_OUTPUT_PIN_CANDIDATES["ScatteringCoefficients"]
    )
    pins["AbsorptionCoefficients"] = link_first_matching_pin(
        absorption, "", water_output, LEGACY_WATER_OUTPUT_PIN_CANDIDATES["AbsorptionCoefficients"]
    )
    pins["PhaseG"] = link_first_matching_pin(phase, phase_output, water_output, LEGACY_WATER_OUTPUT_PIN_CANDIDATES["PhaseG"])
    pins["ColorScaleBehindWater"] = link_first_matching_pin(
        surface, "OutColorScaleBehindWater", water_output, LEGACY_WATER_OUTPUT_PIN_CANDIDATES["ColorScaleBehindWater"]
    )
    return pins


def connect_translucent_outputs(surface):
    connect_material_output(surface, unreal.MaterialProperty.MP_BASE_COLOR, "OutTranslucentColor")
    connect_material_output(surface, unreal.MaterialProperty.MP_OPACITY, "OutTranslucentOpacity")
    connect_material_output(surface, unreal.MaterialProperty.MP_SPECULAR, "OutSpecular")
    connect_material_output(surface, unreal.MaterialProperty.MP_ROUGHNESS, "OutRoughness")
    connect_material_output(surface, unreal.MaterialProperty.MP_NORMAL, "OutNormalWS")
    connect_material_output(surface, unreal.MaterialProperty.MP_EMISSIVE_COLOR, "OutEmissive")


def connect_lite_outputs(surface):
    connect_material_output(surface, unreal.MaterialProperty.MP_BASE_COLOR, "OutLiteColor")
    connect_material_output(surface, unreal.MaterialProperty.MP_SPECULAR, "OutSpecular")
    connect_material_output(surface, unreal.MaterialProperty.MP_ROUGHNESS, "OutRoughness")
    connect_material_output(surface, unreal.MaterialProperty.MP_NORMAL, "OutNormalWS")


def read_material_statistics(material):
    try:
        statistics = unreal.MaterialEditingLibrary.get_statistics(material)
        return {
            name: statistics.get_editor_property(name)
            for name in ("num_vertex_shader_instructions", "num_pixel_shader_instructions", "num_samplers")
        }
    except Exception:
        return {}


def compile_material(material, label):
    errors = [str(item) for item in unreal.MaterialEditingLibrary.recompile_material(material) or []]
    if errors:
        raise RuntimeError(f"{label} 컴파일 실패(Saved/Logs/TDGame.log 의 LogMaterial 다음 들여쓴 줄 확인): " + " | ".join(errors))
    statistics = read_material_statistics(material)
    if statistics and not statistics.get("num_pixel_shader_instructions"):
        raise RuntimeError(
            f"{label} 셰이더 컴파일 결과 없음(픽셀 셰이더 명령 0). Saved/Logs/TDGame.log 의 LogMaterial·LogShaderCompilers 다음 들여쓴 줄 확인"
        )
    return statistics


def verify_custom_inputs_connected(material, custom, expected_count):
    getter = getattr(unreal.MaterialEditingLibrary, "get_inputs_for_material_expression", None)
    if getter is None:
        return "입력 연결 개수 확인 도구 없음"
    connected = [item for item in getter(material, custom) if item is not None]
    if len(connected) != expected_count:
        raise RuntimeError(f"Custom 입력 연결 개수 불일치: {len(connected)}/{expected_count}")
    return f"{len(connected)}/{expected_count}"


def verify_master_parameters(material, uses_water_phase):
    editing = unreal.MaterialEditingLibrary
    found = {str(name) for name in editing.get_vector_parameter_names(material)}
    found |= {str(name) for name in editing.get_scalar_parameter_names(material)}
    found |= {str(name) for name in editing.get_texture_parameter_names(material)}
    switch_getter = getattr(editing, "get_static_switch_parameter_names", None)
    if switch_getter is None:
        found |= set(STATIC_SWITCH_NAMES)
    else:
        found |= {str(name) for name in switch_getter(material)}
    expected = set(SURFACE_INPUT_NAMES) | set(OFFSET_INPUT_NAMES)
    expected -= set(BUILTIN_SOURCE_NAMES)
    if uses_water_phase:
        expected.add("WaterPhaseG")
    missing = sorted(expected - found)
    if missing:
        raise RuntimeError("재질 파라미터 없음: " + ", ".join(missing))
    return sorted(found)


def collect_instance_override_names():
    names = set()
    for spec in INSTANCE_SPECS:
        names |= set(spec["vectors"]) | set(spec["scalars"])
    return names


def find_shadowed_mid_parameters():
    driven = set(MID_DRIVEN_TEXTURE_NAMES) | set(MID_DRIVEN_VECTOR_NAMES) | set(MID_DRIVEN_SCALAR_NAMES)
    return sorted(driven & collect_instance_override_names())


def verify_primitive_data_parameters(factory):
    stored = {}
    for name, index in PRIMITIVE_DATA_INDEX_BY_PARAMETER.items():
        node = factory.get(name)[0]
        if not node.get_editor_property("use_custom_primitive_data") or int(node.get_editor_property("primitive_data_index")) != index:
            raise RuntimeError(f"Custom Primitive Data 설정 확인 실패: {name} 인덱스 {index}")
        stored[name] = index
    return stored


def verify_mid_contract(material):
    editing = unreal.MaterialEditingLibrary
    groups = (
        ("texture", MID_DRIVEN_TEXTURE_NAMES, editing.get_texture_parameter_names),
        ("vector", MID_DRIVEN_VECTOR_NAMES, editing.get_vector_parameter_names),
        ("scalar", MID_DRIVEN_SCALAR_NAMES, editing.get_scalar_parameter_names),
    )
    missing = []
    for kind, names, getter in groups:
        found = {str(name) for name in getter(material)}
        missing.extend(f"{kind}:{name}" for name in names if name not in found)
    if missing:
        raise RuntimeError("C++ 구동 파라미터가 재질에 없음(MID 가 값을 넣어도 반영되지 않음): " + ", ".join(missing))
    return {
        "textures": list(MID_DRIVEN_TEXTURE_NAMES),
        "vectors": list(MID_DRIVEN_VECTOR_NAMES),
        "scalars": list(MID_DRIVEN_SCALAR_NAMES),
        "decal_only_scalars": list(DECAL_ONLY_MID_SCALAR_NAMES),
    }


def create_water_master(is_lite, codes, texture_defaults, overwrite, water_path):
    master_name = LITE_MASTER_NAME if is_lite else WATER_MASTER_NAME
    master_path = LITE_MASTER_PATH if is_lite else WATER_MASTER_PATH
    material, was_created = prepare_master_material(master_name, master_path, overwrite)
    assignments = [("has_pixel_animation", True), ("tangent_space_normal", False), ("two_sided", True)]
    if not is_lite and water_path == LEGACY_WATER_PATH:
        assignments.append(("shading_model", unreal.MaterialShadingModel.MSM_SINGLE_LAYER_WATER))
    if not is_lite and water_path == TRANSLUCENT_WATER_PATH:
        assignments = [
            ("has_pixel_animation", True),
            ("tangent_space_normal", False),
            ("two_sided", False),
            ("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT),
            ("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT),
            ("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING),
        ]
    assignments.append(("used_with_spline_meshes", True))
    settings = apply_required_properties(material, assignments)

    factory = SourceFactory(material, texture_defaults, is_lite, water_path)
    surface = build_custom_node(
        material,
        "TDFluidWaterSurface",
        unreal.CustomMaterialOutputType.CMOT_FLOAT4,
        SURFACE_INPUT_NAMES,
        codes["surface"],
        -300,
        0,
        SURFACE_OUTPUT_SPECS,
    )
    offset = build_custom_node(
        material, "TDFluidWaterOffset", unreal.CustomMaterialOutputType.CMOT_FLOAT3, OFFSET_INPUT_NAMES, codes["offset"], -300, 1500
    )
    link_custom_inputs(surface, SURFACE_INPUT_NAMES, factory)
    link_custom_inputs(offset, OFFSET_INPUT_NAMES, factory)
    connect_material_output(offset, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

    pins = {}
    if is_lite:
        connect_lite_outputs(surface)
    elif water_path == TRANSLUCENT_WATER_PATH:
        connect_translucent_outputs(surface)
    elif water_path == LEGACY_WATER_PATH:
        pins = connect_legacy_water_outputs(material, surface, factory)
    else:
        pins = connect_substrate_water_outputs(material, surface, factory)

    surface_connected = verify_custom_inputs_connected(material, surface, len(SURFACE_INPUT_NAMES))
    offset_connected = verify_custom_inputs_connected(material, offset, len(OFFSET_INPUT_NAMES))
    statistics = compile_material(material, "물 수면 재질" + (" Lite" if is_lite else ""))
    parameter_names = verify_master_parameters(material, not is_lite and water_path != TRANSLUCENT_WATER_PATH)
    mid_contract = verify_mid_contract(material)
    primitive_data = verify_primitive_data_parameters(factory)
    save_asset(material, master_path)
    return material, {
        "created": was_created,
        "lite": is_lite,
        "water_path": "lite" if is_lite else water_path,
        "settings": settings,
        "pins": pins,
        "static_switches": factory.used_static_switches,
        "surface_inputs_connected": surface_connected,
        "offset_inputs_connected": offset_connected,
        "statistics": statistics,
        "parameter_count": len(parameter_names),
        "mid_contract": mid_contract,
        "primitive_data": primitive_data,
    }


def probe_dbuffer_decal(overwrite):
    try:
        material, _ = prepare_master_material(DECAL_PROBE_NAME, DECAL_PROBE_PATH, overwrite)
        apply_required_properties(
            material,
            (
                ("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL),
                ("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT),
            ),
        )
        color = add_node(material, unreal.MaterialExpressionConstant3Vector, -300, -100, constant=unreal.LinearColor(0.02, 0.02, 0.02, 1.0))
        roughness = add_node(material, unreal.MaterialExpressionConstant, -300, 20, r=0.2)
        opacity = add_node(material, unreal.MaterialExpressionConstant, -300, 120, r=0.5)
        connect_material_output(color, unreal.MaterialProperty.MP_BASE_COLOR)
        connect_material_output(roughness, unreal.MaterialProperty.MP_ROUGHNESS)
        connect_material_output(opacity, unreal.MaterialProperty.MP_OPACITY)
        compile_material(material, "DBuffer 데칼 탐침")
        save_asset(material, DECAL_PROBE_PATH)
    except Exception as error:
        if unreal.EditorAssetLibrary.does_asset_exist(DECAL_PROBE_PATH):
            unreal.EditorAssetLibrary.delete_asset(DECAL_PROBE_PATH)
        return {"ok": False, "detail": str(error)}
    return {"ok": True, "detail": "Substrate 켜짐 상태에서 디퍼드 데칼(반투명 블렌드, UE 5.8 은 DecalBlendMode 대신 블렌드 모드·연결 출력으로 DBuffer 결정) 재질 컴파일 성공"}


def read_wet_decal_code(project_root):
    code = read_text_file(os.path.join(project_root, *SHADER_DIRECTORY, WET_DECAL_HLSL_NAME))
    problems = validate_hlsl_code(code, WET_DECAL_INPUT_NAMES, WET_DECAL_SAMPLER_NAMES, WET_DECAL_ENGINE_SYMBOLS, ())
    if problems:
        raise RuntimeError("젖음 데칼 HLSL 자체 점검 실패: " + " | ".join(problems))
    return code


def create_wetness_decal(texture_defaults, overwrite):
    try:
        code = read_wet_decal_code(unreal.SystemLibrary.get_project_directory())
        material, _ = prepare_master_material(WET_DECAL_NAME, WET_DECAL_PATH, overwrite)
        apply_required_properties(
            material,
            (
                ("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL),
                ("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT),
            ),
        )
        factory = SourceFactory(material, texture_defaults, False)
        wetness = build_custom_node(
            material, "TDFluidWaterWetDecal", unreal.CustomMaterialOutputType.CMOT_FLOAT1, WET_DECAL_INPUT_NAMES, code, -300, 0
        )
        link_custom_inputs(wetness, WET_DECAL_INPUT_NAMES, factory)
        color, color_output = factory.get("WetDarkColor")
        roughness, roughness_output = factory.get("WetRoughness")
        connect_material_output(color, unreal.MaterialProperty.MP_BASE_COLOR, color_output)
        connect_material_output(roughness, unreal.MaterialProperty.MP_ROUGHNESS, roughness_output)
        connect_material_output(wetness, unreal.MaterialProperty.MP_OPACITY)
        statistics = compile_material(material, "젖음 DBuffer 데칼")
        save_asset(material, WET_DECAL_PATH)
    except Exception as error:
        if unreal.EditorAssetLibrary.does_asset_exist(WET_DECAL_PATH):
            unreal.EditorAssetLibrary.delete_asset(WET_DECAL_PATH)
        return {"ok": False, "detail": str(error)}
    return {"ok": True, "asset": WET_DECAL_PATH, "statistics": statistics, "inputs": list(WET_DECAL_INPUT_NAMES)}


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
    instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, MATERIAL_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew()
    )
    if instance is None:
        raise RuntimeError(f"재질 인스턴스 생성 실패: {path}")
    return instance


def create_material_instance(spec, master, overwrite):
    editing = unreal.MaterialEditingLibrary
    path = asset_path(spec["name"])
    instance = prepare_instance(spec["name"], overwrite)
    editing.set_material_instance_parent(instance, master)
    for parameter_name, value in spec["vectors"].items():
        editing.set_material_instance_vector_parameter_value(instance, parameter_name, unreal.LinearColor(value[0], value[1], value[2], 0.0))
    for parameter_name, value in spec["scalars"].items():
        editing.set_material_instance_scalar_parameter_value(instance, parameter_name, value)
    switches = spec.get("switches", {})
    for parameter_name, value in switches.items():
        editing.set_material_instance_static_switch_parameter_value(instance, parameter_name, value)
    editing.update_material_instance(instance)
    for parameter_name, value in spec["scalars"].items():
        stored = editing.get_material_instance_scalar_parameter_value(instance, parameter_name)
        if abs(stored - value) > 0.001:
            raise RuntimeError(f"재질 인스턴스 스칼라 설정 확인 실패: {path}.{parameter_name}")
    for parameter_name, value in switches.items():
        if editing.get_material_instance_static_switch_parameter_value(instance, parameter_name) != value:
            raise RuntimeError(f"재질 인스턴스 정적 스위치 설정 확인 실패: {path}.{parameter_name}")
    save_asset(instance, path)
    return {"asset": path, "scalars": spec["scalars"], "vectors": spec["vectors"], "switches": switches}


def write_report(report):
    report_path = project_path(*REPORT_RELATIVE_PATH)
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, "w", encoding="utf-8") as report_file:
        json.dump(report, report_file, ensure_ascii=False, indent=2, default=str)
    return report_path


def find_missing_saved_files(has_decal_probe, has_wet_decal):
    skipped = set()
    if not has_decal_probe:
        skipped.add(DECAL_PROBE_PATH)
    if not has_wet_decal:
        skipped.add(WET_DECAL_PATH)
    expected = [
        project_path("Content", *path[len("/Game/"):].split("/")) + ".uasset"
        for path in collect_target_asset_paths()
        if path not in skipped
    ]
    return [path for path in expected if not os.path.isfile(path)]


def describe_codes(codes):
    return {name: {"characters": len(code), "sha256": hashlib.sha256(code.encode("utf-8")).hexdigest()} for name, code in codes.items()}


def main():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    assets = unreal.EditorAssetLibrary

    request = read_request()
    water_path = request.get("water_path", LEGACY_WATER_PATH)
    if water_path not in ("substrate", LEGACY_WATER_PATH, TRANSLUCENT_WATER_PATH):
        raise RuntimeError(f"알 수 없는 water_path: {water_path}")
    path_names = {
        LEGACY_WATER_PATH: LEGACY_REQUIRED_NAMES,
        "substrate": SUBSTRATE_REQUIRED_NAMES,
        TRANSLUCENT_WATER_PATH: TRANSLUCENT_REQUIRED_NAMES,
    }[water_path]
    phase_zero = PHASE_ZERO_RESULTS
    phase_zero["substrate_slw_class_present"] = not find_missing_unreal_names(("MaterialExpressionSubstrateSingleLayerWaterBSDF",))
    missing_names = find_missing_unreal_names(REQUIRED_UNREAL_NAMES + path_names)
    if missing_names:
        raise RuntimeError("언리얼 Python 이름 없음(0단계 확인 실패면 요청 파일에 water_path legacy): " + ", ".join(missing_names))
    if editor.get_game_world() is not None:
        raise RuntimeError("PIE를 종료한 뒤 실행하세요.")
    shadowed_parameters = find_shadowed_mid_parameters()
    if shadowed_parameters:
        raise RuntimeError("인스턴스가 C++ 구동 파라미터를 덮어쓰고 있음: " + ", ".join(shadowed_parameters))

    sources = read_shader_sources(unreal.SystemLibrary.get_project_directory())
    codes = assemble_all_node_codes(sources)
    problems = validate_all_node_codes(codes)
    if problems:
        raise RuntimeError("HLSL 자체 점검 실패: " + " | ".join(problems))

    overwrite = request.get("overwrite") is True
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
    texture_path, default_texture, default_sampler = pick_default_texture()
    texture_defaults = {name: (default_texture, default_sampler) for name in SIMULATION_TEXTURE_PARAMETERS}

    print("단계: 텍스처 가져오기")
    imported = {spec["name"]: import_texture(spec, overwrite) for spec in TEXTURE_SPECS}
    for parameter_name, texture_name in OWN_TEXTURE_PARAMETERS.items():
        texture_defaults[parameter_name] = imported[texture_name]

    print("단계: 격자 메시")
    meshes = [create_grid_mesh(spec, overwrite) for spec in MESH_SPECS]

    print("단계: 0단계 확인 3 DBuffer 데칼")
    phase_zero["dbuffer_decal_with_substrate"] = probe_dbuffer_decal(overwrite)

    print("단계: 젖음 DBuffer 데칼 재질(J6)")
    wetness_decal = create_wetness_decal(texture_defaults, overwrite)

    if overwrite:
        release_water_stage_world()
        delete_existing_instances()
    print("단계: 물 마스터 재질(0단계 확인 2 SceneDepthWithoutWater 컴파일)")
    try:
        water_master, water_summary = create_water_master(False, codes, texture_defaults, overwrite, water_path)
    except Exception as error:
        phase_zero["scene_depth_without_water_compiles"] = {"ok": False, "detail": str(error)}
        raise
    phase_zero["scene_depth_without_water_compiles"] = {"ok": True, "detail": water_path}

    print("단계: Lite 마스터 재질")
    lite_master, lite_summary = create_water_master(True, codes, texture_defaults, overwrite, water_path)

    print("단계: 재질 인스턴스")
    instances = [create_material_instance(spec, water_master, overwrite) for spec in INSTANCE_SPECS]

    missing_files = find_missing_saved_files(phase_zero["dbuffer_decal_with_substrate"]["ok"], wetness_decal["ok"])
    if missing_files:
        raise RuntimeError("저장 파일 확인 실패: " + ", ".join(missing_files))

    report = {
        "status": "ok",
        "overwrite": overwrite,
        "water_path": water_path,
        "phase_zero": phase_zero,
        "hlsl": describe_codes(codes),
        "default_texture": texture_path,
        "textures": {name: str(sampler) for name, (_, sampler) in imported.items()},
        "meshes": meshes,
        "water_master": water_summary,
        "lite_master": lite_summary,
        "instances": instances,
        "wetness_decal": wetness_decal,
        "saved": True,
    }
    report_path = write_report(report)
    print(json.dumps(report, ensure_ascii=False, default=str))
    print("보고서: " + report_path)


def run_with_failure_report():
    try:
        main()
    except Exception as error:
        write_report({"status": "failed", "error": str(error), "phase_zero": PHASE_ZERO_RESULTS})
        raise


if __name__ == "__main__":
    run_with_failure_report()
