#include "TDFluidGpuFog3DPasses.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"

class FTDFluidGpuFog3DPressureColumnCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FTDFluidGpuFog3DPressureColumnCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuFog3DPressureColumnCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTDFluidGpuFog3DCommonParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, DivergenceIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<uint>, FaceFlagsIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, PressureInOut)
		SHADER_PARAMETER(uint32, PressureColor)
		SHADER_PARAMETER(float, OverRelaxation)
		SHADER_PARAMETER(float, HorizontalCoupling)
		SHADER_PARAMETER(float, VerticalCoupling)
		SHADER_PARAMETER(float, CellVolume)
		SHADER_PARAMETER(float, PivotGuardRatio)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuFog3DPressureColumnCS, "/TDFluidGpu/Private/TDFluidGpuFog3DPressure.usf", "Fog3DPressureColumnCS", SF_Compute);

namespace TDFluidGpuFog3DPressurePrivate
{
	constexpr int32 Fog3DPressureGroupSizeX = 8;
	constexpr int32 Fog3DPressureGroupSizeY = 8;
	constexpr int32 Fog3DPressureColorCount = 2;
	constexpr double Fog3DPressurePivotGuardRatio = 1.0e-12;

	FIntVector GetFog3DPressureGroupCount(int32 Resolution)
	{
		return FComputeShaderUtils::GetGroupCount(FIntVector(Resolution / 2, Resolution, 1), FIntVector(Fog3DPressureGroupSizeX, Fog3DPressureGroupSizeY, 1));
	}

	void FillFog3DPressureCouplings(const FTDFluidGpuFog3DCommonParameters& CommonParameters, FTDFluidGpuFog3DPressureColumnCS::FParameters& Parameters)
	{
		const double CellSizeCm = CommonParameters.CellSizeCm;
		const double LayerHeightCm = CommonParameters.LayerHeightCm;
		Parameters.HorizontalCoupling = static_cast<float>(CellSizeCm * LayerHeightCm / CellSizeCm);
		Parameters.VerticalCoupling = static_cast<float>(CellSizeCm * CellSizeCm / LayerHeightCm);
		Parameters.CellVolume = static_cast<float>(CellSizeCm * CellSizeCm * LayerHeightCm);
		Parameters.PivotGuardRatio = static_cast<float>(Fog3DPressurePivotGuardRatio);
	}
}

void AddTDFluidGpuFog3DPressurePasses(FRDGBuilder& GraphBuilder, const FTDFluidGpuFog3DCommonParameters& CommonParameters, FRDGTextureRef Divergence, FRDGTextureRef FaceFlags, FRDGTextureRef Pressure, const FTDFluidGpuFog3DPressureSettings& Settings)
{
	using namespace TDFluidGpuFog3DPressurePrivate;

	if (!Divergence || !FaceFlags || !Pressure || Settings.SweepCount <= 0)
	{
		return;
	}

	TShaderMapRef<FTDFluidGpuFog3DPressureColumnCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	const FIntVector GroupCount = GetFog3DPressureGroupCount(CommonParameters.Resolution);
	FRDGTextureSRVRef DivergenceSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(Divergence));
	FRDGTextureSRVRef FaceFlagsSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(FaceFlags));
	for (int32 SweepIndex = 0; SweepIndex < Settings.SweepCount; ++SweepIndex)
	{
		for (int32 Color = 0; Color < Fog3DPressureColorCount; ++Color)
		{
			FTDFluidGpuFog3DPressureColumnCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuFog3DPressureColumnCS::FParameters>();
			Parameters->Common = CommonParameters;
			Parameters->DivergenceIn = DivergenceSRV;
			Parameters->FaceFlagsIn = FaceFlagsSRV;
			Parameters->PressureInOut = GraphBuilder.CreateUAV(Pressure);
			Parameters->PressureColor = static_cast<uint32>(Color);
			Parameters->OverRelaxation = Settings.OverRelaxation;
			FillFog3DPressureCouplings(CommonParameters, *Parameters);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.Fog3D.PressureColumn"), Shader, Parameters, GroupCount);
		}
	}
}
