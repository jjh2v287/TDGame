#include "TDFluidGpuKernelPressurePasses.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "TDFluidGpuSolverOptionSettings.h"

namespace TDFluidGpuKernelPressurePrivate
{
	constexpr int32 KernelCellGroupSize = 8;
	constexpr EPixelFormat KernelDivergenceFormat = PF_R32_FLOAT;
	constexpr EPixelFormat KernelPartialSumsFormat = PF_A32B32G32R32F;

	FRDGTextureDesc MakeKernelStateArrayDesc(int32 Resolution, int32 SliceCount, EPixelFormat Format)
	{
		return FRDGTextureDesc::Create2DArray(FIntPoint(Resolution, Resolution), Format, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV, static_cast<uint16>(SliceCount));
	}

	FIntVector MakeKernelCellGroupCount(int32 Resolution, int32 EntryCount)
	{
		return FIntVector(FMath::DivideAndRoundUp(Resolution, KernelCellGroupSize), FMath::DivideAndRoundUp(Resolution, KernelCellGroupSize), EntryCount);
	}

	FRDGTextureSRVRef MakeKernelTextureSRV(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture)
	{
		return GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(Texture));
	}

	FRDGTextureUAVRef MakeKernelTextureUAV(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture)
	{
		return GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Texture));
	}

	bool HasKernelPressureInputs(FGlobalShaderMap* ShaderMap, const FTDFluidGpuKernelPressurePassInputs& Inputs)
	{
		return ShaderMap != nullptr
			&& Inputs.EntryCount > 0
			&& Inputs.Resolution > 0
			&& Inputs.ActorParameters != nullptr
			&& Inputs.SolverOptionParameters != nullptr
			&& Inputs.ObstacleSdf != nullptr
			&& Inputs.ProjectedVelocity != nullptr
			&& Inputs.PressureTextures[0] != nullptr
			&& Inputs.PressureTextures[1] != nullptr;
	}
}

class FTDFluidGpuKernelPressureShader : public FGlobalShader
{
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	FTDFluidGpuKernelPressureShader() {}
	FTDFluidGpuKernelPressureShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
	}
};

class FTDFluidGpuKernelDivergenceCS : public FTDFluidGpuKernelPressureShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuKernelDivergenceCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuKernelDivergenceCS, FTDFluidGpuKernelPressureShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, ObstacleSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, VelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, KernelDivergenceOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuKernelDivergenceCS, "/TDFluidGpu/Private/TDFluidGpuKernelPressure.usf", "KernelDivergenceCS", SF_Compute);

class FTDFluidGpuKernelHorizontalCS : public FTDFluidGpuKernelPressureShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuKernelHorizontalCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuKernelHorizontalCS, FTDFluidGpuKernelPressureShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuSolverOptionParameters>, SolverOptionParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, ObstacleSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, PressureIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, KernelDivergenceIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float4>, KernelPartialSumsOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuKernelHorizontalCS, "/TDFluidGpu/Private/TDFluidGpuKernelPressure.usf", "KernelHorizontalCS", SF_Compute);

class FTDFluidGpuKernelVerticalCS : public FTDFluidGpuKernelPressureShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuKernelVerticalCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuKernelVerticalCS, FTDFluidGpuKernelPressureShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuSolverOptionParameters>, SolverOptionParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, ObstacleSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float4>, KernelPartialSumsIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, PressureOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuKernelVerticalCS, "/TDFluidGpu/Private/TDFluidGpuKernelPressure.usf", "KernelVerticalCS", SF_Compute);

FRDGBufferSRVRef CreateTDFluidGpuSolverOptionParametersSRV(FRDGBuilder& GraphBuilder, const FTDFluidGpuBatchFrame& Frame)
{
	TArray<FTDFluidGpuSolverOptionParameters> UploadParameters = Frame.SolverOptionParameters;
	if (UploadParameters.IsEmpty())
	{
		UploadParameters.AddDefaulted();
	}
	return GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.SolverOptionParameters"), UploadParameters));
}

int32 AddTDFluidGpuKernelPressurePasses(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, const FTDFluidGpuKernelPressurePassInputs& Inputs)
{
	using namespace TDFluidGpuKernelPressurePrivate;

	int32 CurrentPressureIndex = Inputs.PressureIndex;
	if (!HasKernelPressureInputs(ShaderMap, Inputs))
	{
		return CurrentPressureIndex;
	}

	const int32 SliceCount = FMath::Max(Inputs.SliceCapacity, Inputs.EntryCount);
	const FIntVector GroupCount = MakeKernelCellGroupCount(Inputs.Resolution, Inputs.EntryCount);
	FRDGTextureRef KernelDivergence = GraphBuilder.CreateTexture(MakeKernelStateArrayDesc(Inputs.Resolution, SliceCount, KernelDivergenceFormat), TEXT("TDFluidGpu.KernelDivergence"));
	FRDGTextureRef KernelPartialSums = GraphBuilder.CreateTexture(MakeKernelStateArrayDesc(Inputs.Resolution, SliceCount, KernelPartialSumsFormat), TEXT("TDFluidGpu.KernelPartialSums"));
	FRDGTextureSRVRef ObstacleSdf = MakeKernelTextureSRV(GraphBuilder, Inputs.ObstacleSdf);

	FTDFluidGpuKernelDivergenceCS::FParameters* DivergenceParameters = GraphBuilder.AllocParameters<FTDFluidGpuKernelDivergenceCS::FParameters>();
	DivergenceParameters->ActorParameters = Inputs.ActorParameters;
	DivergenceParameters->ObstacleSdf = ObstacleSdf;
	DivergenceParameters->VelocityIn = MakeKernelTextureSRV(GraphBuilder, Inputs.ProjectedVelocity);
	DivergenceParameters->KernelDivergenceOut = MakeKernelTextureUAV(GraphBuilder, KernelDivergence);
	TShaderMapRef<FTDFluidGpuKernelDivergenceCS> DivergenceShader(ShaderMap);
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.KernelDivergence"), DivergenceShader, DivergenceParameters, GroupCount);

	TShaderMapRef<FTDFluidGpuKernelHorizontalCS> HorizontalShader(ShaderMap);
	TShaderMapRef<FTDFluidGpuKernelVerticalCS> VerticalShader(ShaderMap);
	const int32 PassCount = FMath::Clamp(Inputs.PassesPerProjection, 1, TDFluidGpuSolverOptionConstants::MaximumKernelPassesPerProjection);
	for (int32 PassIndex = 0; PassIndex < PassCount; ++PassIndex)
	{
		const int32 TargetPressureIndex = 1 - CurrentPressureIndex;

		FTDFluidGpuKernelHorizontalCS::FParameters* HorizontalParameters = GraphBuilder.AllocParameters<FTDFluidGpuKernelHorizontalCS::FParameters>();
		HorizontalParameters->ActorParameters = Inputs.ActorParameters;
		HorizontalParameters->SolverOptionParameters = Inputs.SolverOptionParameters;
		HorizontalParameters->ObstacleSdf = ObstacleSdf;
		HorizontalParameters->PressureIn = MakeKernelTextureSRV(GraphBuilder, Inputs.PressureTextures[CurrentPressureIndex]);
		HorizontalParameters->KernelDivergenceIn = MakeKernelTextureSRV(GraphBuilder, KernelDivergence);
		HorizontalParameters->KernelPartialSumsOut = MakeKernelTextureUAV(GraphBuilder, KernelPartialSums);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.KernelHorizontal"), HorizontalShader, HorizontalParameters, GroupCount);

		FTDFluidGpuKernelVerticalCS::FParameters* VerticalParameters = GraphBuilder.AllocParameters<FTDFluidGpuKernelVerticalCS::FParameters>();
		VerticalParameters->ActorParameters = Inputs.ActorParameters;
		VerticalParameters->SolverOptionParameters = Inputs.SolverOptionParameters;
		VerticalParameters->ObstacleSdf = ObstacleSdf;
		VerticalParameters->KernelPartialSumsIn = MakeKernelTextureSRV(GraphBuilder, KernelPartialSums);
		VerticalParameters->PressureOut = MakeKernelTextureUAV(GraphBuilder, Inputs.PressureTextures[TargetPressureIndex]);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.KernelVertical"), VerticalShader, VerticalParameters, GroupCount);

		CurrentPressureIndex = TargetPressureIndex;
	}
	return CurrentPressureIndex;
}
