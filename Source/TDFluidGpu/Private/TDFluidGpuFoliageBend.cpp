#include "TDFluidGpuFoliageBend.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalRenderResources.h"
#include "GlobalShader.h"
#include "PooledRenderTarget.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderUtils.h"
#include "ShaderParameterStruct.h"
#include "TextureResource.h"

class FTDFluidGpuFoliageBendCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FTDFluidGpuFoliageBendCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuFoliageBendCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_TEXTURE(Texture2D, VelocityTexture)
		SHADER_PARAMETER_TEXTURE(Texture2D, DensityPressureTexture)
		SHADER_PARAMETER_TEXTURE(Texture2D, WaterSurfaceTexture)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float2>, PreviousBend)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AuxPrevious)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, NextBend)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, AuxOutput)
		SHADER_PARAMETER(int32, Resolution)
		SHADER_PARAMETER(FIntPoint, ShiftCells)
		SHADER_PARAMETER(uint32, ResetsState)
		SHADER_PARAMETER(uint32, UsesVelocity)
		SHADER_PARAMETER(uint32, HasWaterSurface)
		SHADER_PARAMETER(uint32, PreservesAuxRedGreen)
		SHADER_PARAMETER(uint32, WrapsArea)
		SHADER_PARAMETER(float, RiseBlend)
		SHADER_PARAMETER(float, RecoveryBlend)
		SHADER_PARAMETER(float, PainterDensityWeight)
		SHADER_PARAMETER(float, RippleSlopeWeight)
		SHADER_PARAMETER(FVector2f, RippleGradientScale)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuFoliageBendCS, "/TDFluidGpu/Private/TDFluidGpuFoliageBend.usf", "FoliageBendCS", SF_Compute);

namespace TDFluidGpuFoliageBendPrivate
{
	constexpr int32 FoliageBendGroupSize = 8;
	constexpr int64 FoliageBendStateTextureCount = 2;
	constexpr int64 FoliageBendStateBytesPerTexel = 8;

	FRHITexture* GetFoliageBendInputTexture(const FTextureResource* Resource)
	{
		return Resource ? Resource->TextureRHI.GetReference() : nullptr;
	}

	FRHITexture* GetFoliageBendBlackTexture()
	{
		return GBlackTexture->TextureRHI.GetReference();
	}

	bool HasFoliageBendResolution(const FRHITexture* Texture, int32 Resolution)
	{
		return Texture && Texture->GetSizeX() == static_cast<uint32>(Resolution) && Texture->GetSizeY() == static_cast<uint32>(Resolution);
	}

	bool RegisterFoliageBendStateTextures(FRDGBuilder& GraphBuilder, FTDFluidGpuFoliageBendState& State, int32 Resolution, FRDGTextureRef (&OutTextures)[2])
	{
		const bool bRecreates = State.Resolution != Resolution || !State.BendTargets[0].IsValid() || !State.BendTargets[1].IsValid();
		if (!bRecreates)
		{
			OutTextures[0] = GraphBuilder.RegisterExternalTexture(State.BendTargets[0]);
			OutTextures[1] = GraphBuilder.RegisterExternalTexture(State.BendTargets[1]);
			return false;
		}

		const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(FIntPoint(Resolution, Resolution), PF_G32R32F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV);
		OutTextures[0] = GraphBuilder.CreateTexture(Desc, TEXT("TDFluidGpu.FoliageBendState"));
		OutTextures[1] = GraphBuilder.CreateTexture(Desc, TEXT("TDFluidGpu.FoliageBendState"));
		State.Resolution = Resolution;
		State.CurrentIndex = 0;
		return true;
	}

	FRDGTextureRef CreateFoliageBendAuxCopy(FRDGBuilder& GraphBuilder, FRDGTextureRef AuxTexture, bool bPreservesAuxRedGreen)
	{
		if (!bPreservesAuxRedGreen)
		{
			const FRDGTextureDesc DummyDesc = FRDGTextureDesc::Create2D(FIntPoint(1, 1), PF_FloatRGBA, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV);
			FRDGTextureRef DummyTexture = GraphBuilder.CreateTexture(DummyDesc, TEXT("TDFluidGpu.FoliageBendAuxCopy"));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(DummyTexture), FVector4f(0.0f, 0.0f, 0.0f, 0.0f));
			return DummyTexture;
		}

		const FRDGTextureDesc CopyDesc = FRDGTextureDesc::Create2D(AuxTexture->Desc.Extent, AuxTexture->Desc.Format, FClearValueBinding::Black, TexCreate_ShaderResource);
		FRDGTextureRef CopyTexture = GraphBuilder.CreateTexture(CopyDesc, TEXT("TDFluidGpu.FoliageBendAuxCopy"));
		AddCopyTexturePass(GraphBuilder, AuxTexture, CopyTexture);
		return CopyTexture;
	}
}

int64 FTDFluidGpuFoliageBendState::EstimateStateBytes(int32 Resolution)
{
	using namespace TDFluidGpuFoliageBendPrivate;

	if (Resolution <= 0)
	{
		return 0;
	}

	const int64 TexelCount = static_cast<int64>(Resolution) * Resolution;
	return TexelCount * FoliageBendStateBytesPerTexel * FoliageBendStateTextureCount;
}

void FTDFluidGpuFoliageBendState::Release()
{
	BendTargets[0].SafeRelease();
	BendTargets[1].SafeRelease();
	Resolution = 0;
	CurrentIndex = 0;
}

void AddTDFluidGpuFoliageBendPasses(FRDGBuilder& GraphBuilder, FTDFluidGpuFoliageBendState& State, const FTDFluidGpuFoliageBendFrameInput& FrameInput)
{
	using namespace TDFluidGpuFoliageBendPrivate;

	if (!FrameInput.AuxTarget || FrameInput.Resolution <= 0)
	{
		return;
	}

	FRHITexture* AuxRHI = FrameInput.AuxTarget->GetRenderTargetTexture().GetReference();
	FRHITexture* DensityPressureRHI = GetFoliageBendInputTexture(FrameInput.DensityPressureTexture);
	if (!HasFoliageBendResolution(AuxRHI, FrameInput.Resolution) || !HasFoliageBendResolution(DensityPressureRHI, FrameInput.Resolution))
	{
		return;
	}

	FRHITexture* VelocityRHI = GetFoliageBendInputTexture(FrameInput.VelocityTexture);
	FRHITexture* WaterSurfaceRHI = GetFoliageBendInputTexture(FrameInput.WaterSurfaceTexture);
	const bool bUsesVelocity = FrameInput.bUsesVelocity && HasFoliageBendResolution(VelocityRHI, FrameInput.Resolution);
	const bool bHasWaterSurface = FrameInput.bHasWaterSurface && HasFoliageBendResolution(WaterSurfaceRHI, FrameInput.Resolution);

	RDG_EVENT_SCOPE(GraphBuilder, "TDFluidGpu.FoliageBend %d", FrameInput.Resolution);
	FRDGTextureRef AuxTexture = RegisterExternalTexture(GraphBuilder, AuxRHI, TEXT("TDFluidGpu.FoliageBend"), ERDGTextureFlags::None);
	GraphBuilder.SetTextureAccessFinal(AuxTexture, ERHIAccess::SRVMask);

	FRDGTextureRef StateTextures[2];
	const bool bRecreatesState = RegisterFoliageBendStateTextures(GraphBuilder, State, FrameInput.Resolution, StateTextures);
	if (bRecreatesState)
	{
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(StateTextures[State.CurrentIndex]), FVector4f(0.0f, 0.0f, 0.0f, 0.0f));
	}

	FRDGTextureRef AuxCopyTexture = CreateFoliageBendAuxCopy(GraphBuilder, AuxTexture, FrameInput.bPreservesAuxRedGreen);

	FTDFluidGpuFoliageBendCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuFoliageBendCS::FParameters>();
	Parameters->VelocityTexture = bUsesVelocity ? VelocityRHI : GetFoliageBendBlackTexture();
	Parameters->DensityPressureTexture = DensityPressureRHI;
	Parameters->WaterSurfaceTexture = bHasWaterSurface ? WaterSurfaceRHI : GetFoliageBendBlackTexture();
	Parameters->PreviousBend = GraphBuilder.CreateSRV(StateTextures[State.CurrentIndex]);
	Parameters->AuxPrevious = AuxCopyTexture;
	Parameters->NextBend = GraphBuilder.CreateUAV(StateTextures[1 - State.CurrentIndex]);
	Parameters->AuxOutput = GraphBuilder.CreateUAV(AuxTexture);
	Parameters->Resolution = FrameInput.Resolution;
	Parameters->ShiftCells = FrameInput.ShiftCells;
	Parameters->ResetsState = (bRecreatesState || FrameInput.bResetsState) ? 1u : 0u;
	Parameters->UsesVelocity = bUsesVelocity ? 1u : 0u;
	Parameters->HasWaterSurface = bHasWaterSurface ? 1u : 0u;
	Parameters->PreservesAuxRedGreen = FrameInput.bPreservesAuxRedGreen ? 1u : 0u;
	Parameters->WrapsArea = FrameInput.bWrapsArea ? 1u : 0u;
	Parameters->RiseBlend = FrameInput.RiseBlend;
	Parameters->RecoveryBlend = FrameInput.RecoveryBlend;
	Parameters->PainterDensityWeight = FrameInput.PainterDensityWeight;
	Parameters->RippleSlopeWeight = FrameInput.RippleSlopeWeight;
	Parameters->RippleGradientScale = FrameInput.RippleGradientScale;

	TShaderMapRef<FTDFluidGpuFoliageBendCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(FIntPoint(FrameInput.Resolution, FrameInput.Resolution), FoliageBendGroupSize);
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.FoliageBend"), Shader, Parameters, GroupCount);
	State.CurrentIndex = 1 - State.CurrentIndex;

	GraphBuilder.QueueTextureExtraction(StateTextures[0], &State.BendTargets[0]);
	GraphBuilder.QueueTextureExtraction(StateTextures[1], &State.BendTargets[1]);
}
