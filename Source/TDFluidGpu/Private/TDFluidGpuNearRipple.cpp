#include "TDFluidGpuNearRipple.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "PooledRenderTarget.h"
#include "RHIStaticStates.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderUtils.h"
#include "ShaderParameterStruct.h"
#include "TextureResource.h"

BEGIN_SHADER_PARAMETER_STRUCT(FTDFluidGpuNearRippleCommonParameters, )
	SHADER_PARAMETER_TEXTURE(Texture2D, WaterSurfaceTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, WaterSurfaceSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D, AuxTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, AuxSampler)
	SHADER_PARAMETER(FIntPoint, WindowMinCell)
	SHADER_PARAMETER(int32, Resolution)
	SHADER_PARAMETER(float, CellSizeCm)
	SHADER_PARAMETER(float, EdgeFadeCells)
	SHADER_PARAMETER(float, MinWaterDepthCm)
	SHADER_PARAMETER(FVector2f, MainWindowCenterFromOriginCm)
	SHADER_PARAMETER(FVector2f, MainWindowExtentCm)
	SHADER_PARAMETER(float, MainWaterDepthReferenceCm)
	SHADER_PARAMETER(float, ObstacleDistanceOutputRangeCm)
	SHADER_PARAMETER(uint32, HasObstacleDistanceAux)
END_SHADER_PARAMETER_STRUCT()

class FTDFluidGpuNearRippleShader : public FGlobalShader
{
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	FTDFluidGpuNearRippleShader() {}
	FTDFluidGpuNearRippleShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
	}
};

class FTDFluidGpuNearRippleStepCS : public FTDFluidGpuNearRippleShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuNearRippleStepCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuNearRippleStepCS, FTDFluidGpuNearRippleShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTDFluidGpuNearRippleCommonParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float2>, StateIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, StateOut)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuNearRippleSource>, Sources)
		SHADER_PARAMETER(FIntPoint, PreviousWindowMinCell)
		SHADER_PARAMETER(uint32, SourceCount)
		SHADER_PARAMETER(uint32, ClearsCellsOutsidePreviousWindow)
		SHADER_PARAMETER(float, SubstepDeltaSeconds)
		SHADER_PARAMETER(float, WaveSpeedCmPerSec)
		SHADER_PARAMETER(float, DampingPerSec)
		SHADER_PARAMETER(float, EdgeAbsorptionStrength)
		SHADER_PARAMETER(float, IdleContactAmplitudeCm)
		SHADER_PARAMETER(float, IdleContactPhase)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuNearRippleStepCS, "/TDFluidGpu/Private/TDFluidGpuNearRipple.usf", "NearRippleStepCS", SF_Compute);

class FTDFluidGpuNearRippleFinalizeCS : public FTDFluidGpuNearRippleShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuNearRippleFinalizeCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuNearRippleFinalizeCS, FTDFluidGpuNearRippleShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTDFluidGpuNearRippleCommonParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float2>, StateIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTarget)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuNearRippleFinalizeCS, "/TDFluidGpu/Private/TDFluidGpuNearRipple.usf", "NearRippleFinalizeCS", SF_Compute);

void FTDFluidGpuNearRippleState::Release()
{
	HeightVelocityTargets[0].SafeRelease();
	HeightVelocityTargets[1].SafeRelease();
	Resolution = 0;
	CurrentIndex = 0;
}

namespace
{
	constexpr int32 TDFluidNearRippleGroupSize = 8;
	constexpr int32 TDFluidNearRippleMaximumSubsteps = 8;
	constexpr int64 TDFluidNearRippleStateTextureCount = 2;
	constexpr int64 TDFluidNearRippleStateBytesPerTexel = 8;

	FRHITexture* ResolveTDFluidNearRippleMaskTexture(const FTextureResource* Resource)
	{
		FRHITexture* Texture = Resource ? Resource->TextureRHI.GetReference() : nullptr;
		return Texture ? Texture : GBlackTexture->TextureRHI.GetReference();
	}

	FTDFluidGpuNearRippleCommonParameters MakeTDFluidNearRippleCommonParameters(const FTDFluidGpuNearRippleFrameInput& FrameInput)
	{
		FTDFluidGpuNearRippleCommonParameters Parameters;
		Parameters.WaterSurfaceTexture = ResolveTDFluidNearRippleMaskTexture(FrameInput.WaterSurfaceTexture);
		Parameters.WaterSurfaceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Parameters.AuxTexture = ResolveTDFluidNearRippleMaskTexture(FrameInput.bHasObstacleDistanceAux ? FrameInput.AuxTexture : nullptr);
		Parameters.AuxSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Parameters.WindowMinCell = FrameInput.WindowMinCell;
		Parameters.Resolution = FrameInput.Resolution;
		Parameters.CellSizeCm = FrameInput.CellSizeCm;
		Parameters.EdgeFadeCells = FMath::Max(FrameInput.EdgeFadeRatio * static_cast<float>(FrameInput.Resolution), 1.0f);
		Parameters.MinWaterDepthCm = FrameInput.MinWaterDepthCm;
		Parameters.MainWindowCenterFromOriginCm = FrameInput.MainWindowCenterFromOriginCm;
		Parameters.MainWindowExtentCm = FrameInput.MainWindowExtentCm;
		Parameters.MainWaterDepthReferenceCm = FrameInput.MainWaterDepthReferenceCm;
		Parameters.ObstacleDistanceOutputRangeCm = FrameInput.ObstacleDistanceOutputRangeCm;
		Parameters.HasObstacleDistanceAux = FrameInput.bHasObstacleDistanceAux && FrameInput.AuxTexture ? 1u : 0u;
		return Parameters;
	}

	bool RegisterTDFluidNearRippleStateTextures(FRDGBuilder& GraphBuilder, FTDFluidGpuNearRippleState& State, int32 Resolution, FRDGTextureRef (&OutTextures)[2])
	{
		const bool bRecreates = State.Resolution != Resolution || !State.HeightVelocityTargets[0].IsValid() || !State.HeightVelocityTargets[1].IsValid();
		if (bRecreates)
		{
			const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(FIntPoint(Resolution, Resolution), PF_G32R32F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV);
			OutTextures[0] = GraphBuilder.CreateTexture(Desc, TEXT("TDFluidGpu.NearRippleStateA"));
			OutTextures[1] = GraphBuilder.CreateTexture(Desc, TEXT("TDFluidGpu.NearRippleStateB"));
			State.Resolution = Resolution;
			State.CurrentIndex = 0;
		}
		else
		{
			OutTextures[0] = GraphBuilder.RegisterExternalTexture(State.HeightVelocityTargets[0]);
			OutTextures[1] = GraphBuilder.RegisterExternalTexture(State.HeightVelocityTargets[1]);
		}
		return bRecreates;
	}

	void QueueTDFluidNearRippleStateExtractions(FRDGBuilder& GraphBuilder, FTDFluidGpuNearRippleState& State, FRDGTextureRef (&StateTextures)[2])
	{
		GraphBuilder.QueueTextureExtraction(StateTextures[0], &State.HeightVelocityTargets[0]);
		GraphBuilder.QueueTextureExtraction(StateTextures[1], &State.HeightVelocityTargets[1]);
	}

	bool HasTDFluidNearRippleMaskSource(const FTDFluidGpuNearRippleFrameInput& FrameInput)
	{
		return FrameInput.WaterSurfaceTexture && FrameInput.MainWindowExtentCm.X > 0.0f && FrameInput.MainWindowExtentCm.Y > 0.0f;
	}
}

int64 FTDFluidGpuNearRippleState::EstimateStateBytes(int32 Resolution)
{
	if (Resolution <= 0)
	{
		return 0;
	}

	const int64 TexelCount = static_cast<int64>(Resolution) * Resolution;
	return TexelCount * TDFluidNearRippleStateBytesPerTexel * TDFluidNearRippleStateTextureCount;
}

void AddTDFluidGpuNearRipplePasses(FRDGBuilder& GraphBuilder, FTDFluidGpuNearRippleState& State, const FTDFluidGpuNearRippleFrameInput& FrameInput)
{
	if (!FrameInput.OutputTarget || FrameInput.Resolution <= 0 || !(FrameInput.CellSizeCm > 0.0f))
	{
		return;
	}

	FRHITexture* OutputRHI = FrameInput.OutputTarget->GetRenderTargetTexture().GetReference();
	if (!OutputRHI || OutputRHI->GetSizeX() != static_cast<uint32>(FrameInput.Resolution) || OutputRHI->GetSizeY() != static_cast<uint32>(FrameInput.Resolution))
	{
		return;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "TDFluidGpuNearRipple %d", FrameInput.Resolution);
	FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(OutputRHI, TEXT("TDFluidGpu.NearRippleOutput")));
	GraphBuilder.SetTextureAccessFinal(OutputTexture, ERHIAccess::SRVMask);

	FRDGTextureRef StateTextures[2];
	const bool bRecreatesState = RegisterTDFluidNearRippleStateTextures(GraphBuilder, State, FrameInput.Resolution, StateTextures);
	const bool bResetsState = bRecreatesState || FrameInput.bResetsState;
	if (bResetsState)
	{
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(StateTextures[State.CurrentIndex]), FVector4f(0.0f, 0.0f, 0.0f, 0.0f));
	}

	if (!HasTDFluidNearRippleMaskSource(FrameInput))
	{
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(StateTextures[1 - State.CurrentIndex]), FVector4f(0.0f, 0.0f, 0.0f, 0.0f));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputTexture), FVector4f(0.0f, 0.0f, 0.0f, 0.0f));
		QueueTDFluidNearRippleStateExtractions(GraphBuilder, State, StateTextures);
		return;
	}

	const FTDFluidGpuNearRippleCommonParameters CommonParameters = MakeTDFluidNearRippleCommonParameters(FrameInput);
	TArray<FTDFluidGpuNearRippleSource> UploadedSources = FrameInput.Sources;
	if (UploadedSources.IsEmpty())
	{
		UploadedSources.AddDefaulted();
	}
	FRDGBufferSRVRef SourceBuffer = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.NearRippleSources"), UploadedSources));
	const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(FIntPoint(FrameInput.Resolution, FrameInput.Resolution), TDFluidNearRippleGroupSize);
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

	TShaderMapRef<FTDFluidGpuNearRippleStepCS> StepShader(ShaderMap);
	const int32 SubstepCount = FMath::Clamp(FrameInput.SubstepCount, 1, TDFluidNearRippleMaximumSubsteps);
	for (int32 SubstepIndex = 0; SubstepIndex < SubstepCount; ++SubstepIndex)
	{
		FTDFluidGpuNearRippleStepCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuNearRippleStepCS::FParameters>();
		Parameters->Common = CommonParameters;
		Parameters->StateIn = GraphBuilder.CreateSRV(StateTextures[State.CurrentIndex]);
		Parameters->StateOut = GraphBuilder.CreateUAV(StateTextures[1 - State.CurrentIndex]);
		Parameters->Sources = SourceBuffer;
		Parameters->PreviousWindowMinCell = FrameInput.PreviousWindowMinCell;
		Parameters->SourceCount = static_cast<uint32>(FrameInput.Sources.Num());
		Parameters->ClearsCellsOutsidePreviousWindow = SubstepIndex == 0 && !bResetsState ? 1u : 0u;
		Parameters->SubstepDeltaSeconds = FrameInput.SubstepDeltaSeconds;
		Parameters->WaveSpeedCmPerSec = FrameInput.WaveSpeedCmPerSec;
		Parameters->DampingPerSec = FrameInput.DampingPerSec;
		Parameters->EdgeAbsorptionStrength = FrameInput.EdgeAbsorptionStrength;
		Parameters->IdleContactAmplitudeCm = FrameInput.IdleContactAmplitudeCm;
		Parameters->IdleContactPhase = static_cast<float>(FMath::Fmod(FrameInput.IdleContactStartPhase + static_cast<double>(SubstepIndex + 1) * FrameInput.IdleContactPhasePerSubstep, UE_DOUBLE_TWO_PI));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.NearRippleStep %d", SubstepIndex), StepShader, Parameters, GroupCount);
		State.CurrentIndex = 1 - State.CurrentIndex;
	}

	FTDFluidGpuNearRippleFinalizeCS::FParameters* FinalizeParameters = GraphBuilder.AllocParameters<FTDFluidGpuNearRippleFinalizeCS::FParameters>();
	FinalizeParameters->Common = CommonParameters;
	FinalizeParameters->StateIn = GraphBuilder.CreateSRV(StateTextures[State.CurrentIndex]);
	FinalizeParameters->OutputTarget = GraphBuilder.CreateUAV(OutputTexture);
	TShaderMapRef<FTDFluidGpuNearRippleFinalizeCS> FinalizeShader(ShaderMap);
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.NearRippleFinalize"), FinalizeShader, FinalizeParameters, GroupCount);
	QueueTDFluidNearRippleStateExtractions(GraphBuilder, State, StateTextures);
}
