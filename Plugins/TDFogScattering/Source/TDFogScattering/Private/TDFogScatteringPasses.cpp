#include "TDFogScatteringPasses.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "PixelFormat.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "SceneView.h"
#include "ScreenPass.h"
#include "ShaderParameterStruct.h"
#include "ShaderPermutation.h"
#include "TDFogScatteringMedia.h"

DECLARE_GPU_STAT_NAMED(TDFogScattering, TEXT("TDFogScattering"));

namespace
{
	constexpr int32 ThreadGroupSize = 8;
	constexpr int32 MinimumViewSideLength = 4;
	constexpr int32 MinimumPyramidSideLength = 2;
	constexpr EPixelFormat FallbackPyramidFormat = PF_FloatRGBA;
}

class FTDFogScatteringComputeShader : public FGlobalShader
{
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	FTDFogScatteringComputeShader() {}
	FTDFogScatteringComputeShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
	}
};

class FTDFogScatteringDownsampleCS : public FTDFogScatteringComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFogScatteringDownsampleCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFogScatteringDownsampleCS, FTDFogScatteringComputeShader);

	class FTDDownsampleTapCountDimension : SHADER_PERMUTATION_SPARSE_INT("DOWNSAMPLE_TAP_COUNT", 4, 8, 13);
	class FTDLuminanceWeightingDimension : SHADER_PERMUTATION_BOOL("LUMINANCE_WEIGHTING");
	using FPermutationDomain = TShaderPermutationDomain<FTDDownsampleTapCountDimension, FTDLuminanceWeightingDimension>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER(FIntPoint, InputRectMinimum)
		SHADER_PARAMETER(FIntPoint, InputRectMaximum)
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FTDFogScatteringComputeShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 8);
	}
};
IMPLEMENT_GLOBAL_SHADER(FTDFogScatteringDownsampleCS, "/Plugin/TDFogScattering/Private/TDFogScattering.usf", "MainDownsampleCS", SF_Compute);

class FTDFogScatteringUpsampleCS : public FTDFogScatteringComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFogScatteringUpsampleCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFogScatteringUpsampleCS, FTDFogScatteringComputeShader);

	class FTDUpsampleTapCountDimension : SHADER_PERMUTATION_SPARSE_INT("UPSAMPLE_TAP_COUNT", 5, 9);
	using FPermutationDomain = TShaderPermutationDomain<FTDUpsampleTapCountDimension>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DetailTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, BlurTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, BlurSampler)
		SHADER_PARAMETER(FVector2f, BlurExtentInverse)
		SHADER_PARAMETER(float, UpsampleRadius)
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FTDFogScatteringComputeShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 8);
	}
};
IMPLEMENT_GLOBAL_SHADER(FTDFogScatteringUpsampleCS, "/Plugin/TDFogScattering/Private/TDFogScattering.usf", "MainUpsampleCS", SF_Compute);

class FTDFogScatteringCompositeCS : public FTDFogScatteringComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFogScatteringCompositeCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFogScatteringCompositeCS, FTDFogScatteringComputeShader);

	class FTDUpsampleTapCountDimension : SHADER_PERMUTATION_SPARSE_INT("UPSAMPLE_TAP_COUNT", 5, 9);
	using FPermutationDomain = TShaderPermutationDomain<FTDUpsampleTapCountDimension>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(FTDFogScatteringMediaParameters, Media)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, BlurTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, BlurSampler)
		SHADER_PARAMETER(FVector2f, BlurExtentInverse)
		SHADER_PARAMETER(FIntPoint, SceneColorRectMinimum)
		SHADER_PARAMETER(FIntPoint, SceneColorRectSize)
		SHADER_PARAMETER(FVector4f, ExponentialFogParameters)
		SHADER_PARAMETER(FVector4f, ExponentialFogParameters2)
		SHADER_PARAMETER(FVector4f, ExponentialFogParameters3)
		SHADER_PARAMETER(float, FogMaxOpacity)
		SHADER_PARAMETER(float, FogEndDistance)
		SHADER_PARAMETER(float, FogDistanceScale)
		SHADER_PARAMETER(float, FogOnlyOnRenderedOpaque)
		SHADER_PARAMETER(float, ScatteringIntensity)
		SHADER_PARAMETER(float, ScatteringWeightPower)
		SHADER_PARAMETER(int32, DebugView)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FTDFogScatteringComputeShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 8);
	}
};
IMPLEMENT_GLOBAL_SHADER(FTDFogScatteringCompositeCS, "/Plugin/TDFogScattering/Private/TDFogScattering.usf", "MainCompositeCS", SF_Compute);

namespace
{
	using FTDFogScatteringLevelSizes = TArray<FIntPoint, TInlineAllocator<16>>;

	FTDFogScatteringLevelSizes ComputePyramidLevelSizes(FIntPoint ViewSize, int32 RequestedLevelCount)
	{
		FTDFogScatteringLevelSizes LevelSizes;
		LevelSizes.Add(ViewSize);
		while (LevelSizes.Num() <= RequestedLevelCount)
		{
			const FIntPoint PreviousSize = LevelSizes.Last();
			const FIntPoint NextSize(FMath::DivideAndRoundUp(PreviousSize.X, 2), FMath::DivideAndRoundUp(PreviousSize.Y, 2));
			if (FMath::Min(NextSize.X, NextSize.Y) < MinimumPyramidSideLength)
			{
				break;
			}
			LevelSizes.Add(NextSize);
		}
		return LevelSizes;
	}

	int32 GetDownsampleTapCount(int32 QualityPreset)
	{
		switch (QualityPreset)
		{
		case 0:
			return 4;
		case 1:
		case 2:
			return 8;
		default:
			return 13;
		}
	}

	int32 GetUpsampleTapCount(int32 QualityPreset)
	{
		return QualityPreset <= 1 ? 5 : 9;
	}

	bool CanStoreTypedUAV(EPixelFormat Format)
	{
		return UE::PixelFormat::HasCapabilities(Format, EPixelFormatCapabilities::TypedUAVStore);
	}

	EPixelFormat ResolvePyramidFormat(EPixelFormat SceneColorFormat, int32 FormatSetting)
	{
		if (FormatSetting > 0)
		{
			return FallbackPyramidFormat;
		}

		const EPixelFormat RequestedFormat = FormatSetting < 0 ? SceneColorFormat : PF_FloatR11G11B10;
		return CanStoreTypedUAV(RequestedFormat) ? RequestedFormat : FallbackPyramidFormat;
	}

	FRDGTextureRef GetSceneDepthTexture(const FPostProcessMaterialInputs& Inputs)
	{
		const TRDGUniformBuffer<FSceneTextureUniformParameters>* SceneTextureUniformBuffer = Inputs.SceneTextures.SceneTextures.GetUniformBuffer();
		if (SceneTextureUniformBuffer == nullptr)
		{
			return nullptr;
		}
		return SceneTextureUniformBuffer->GetContents()->SceneDepthTexture;
	}

	FRDGTextureDesc CreateOutputDescription(const FRDGTextureDesc& InputDescription)
	{
		FRDGTextureDesc OutputDescription = InputDescription;
		OutputDescription.Reset();
		OutputDescription.Flags |= ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV;
		if (!CanStoreTypedUAV(OutputDescription.Format))
		{
			OutputDescription.Format = FallbackPyramidFormat;
			OutputDescription.UAVFormat = PF_Unknown;
		}
		return OutputDescription;
	}

	FRDGTextureRef AddDownsamplePass(
		FRDGBuilder& GraphBuilder,
		FGlobalShaderMap* ShaderMap,
		FRDGTextureRef InputTexture,
		FIntRect InputRect,
		int32 LevelIndex,
		FIntPoint OutputSize,
		EPixelFormat PyramidFormat,
		int32 TapCount,
		bool bShouldWeightLuminance)
	{
		const FRDGTextureDesc OutputDescription = FRDGTextureDesc::Create2D(
			OutputSize,
			PyramidFormat,
			FClearValueBinding::None,
			ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV);
		FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(OutputDescription, TEXT("TDFogScattering.Downsample"));

		FTDFogScatteringDownsampleCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTDFogScatteringDownsampleCS::FParameters>();
		PassParameters->InputTexture = InputTexture;
		PassParameters->InputRectMinimum = InputRect.Min;
		PassParameters->InputRectMaximum = InputRect.Max;
		PassParameters->OutputSize = OutputSize;
		PassParameters->OutputTexture = GraphBuilder.CreateUAV(OutputTexture);

		FTDFogScatteringDownsampleCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FTDFogScatteringDownsampleCS::FTDDownsampleTapCountDimension>(TapCount);
		PermutationVector.Set<FTDFogScatteringDownsampleCS::FTDLuminanceWeightingDimension>(bShouldWeightLuminance);
		TShaderRef<FTDFogScatteringDownsampleCS> ComputeShader = ShaderMap->GetShader<FTDFogScatteringDownsampleCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TDFogScattering.Downsample Level%d %dx%d", LevelIndex, OutputSize.X, OutputSize.Y),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(OutputSize, ThreadGroupSize));

		return OutputTexture;
	}

	FRDGTextureRef AddUpsamplePass(
		FRDGBuilder& GraphBuilder,
		FGlobalShaderMap* ShaderMap,
		FRDGTextureRef DetailTexture,
		FRDGTextureRef BlurTexture,
		FIntPoint BlurSize,
		int32 LevelIndex,
		FIntPoint OutputSize,
		EPixelFormat PyramidFormat,
		int32 TapCount,
		float Radius)
	{
		const FRDGTextureDesc OutputDescription = FRDGTextureDesc::Create2D(
			OutputSize,
			PyramidFormat,
			FClearValueBinding::None,
			ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV);
		FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(OutputDescription, TEXT("TDFogScattering.Upsample"));

		FTDFogScatteringUpsampleCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTDFogScatteringUpsampleCS::FParameters>();
		PassParameters->DetailTexture = DetailTexture;
		PassParameters->BlurTexture = BlurTexture;
		PassParameters->BlurSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->BlurExtentInverse = FVector2f(1.0f / BlurSize.X, 1.0f / BlurSize.Y);
		PassParameters->UpsampleRadius = Radius;
		PassParameters->OutputSize = OutputSize;
		PassParameters->OutputTexture = GraphBuilder.CreateUAV(OutputTexture);

		FTDFogScatteringUpsampleCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FTDFogScatteringUpsampleCS::FTDUpsampleTapCountDimension>(TapCount);
		TShaderRef<FTDFogScatteringUpsampleCS> ComputeShader = ShaderMap->GetShader<FTDFogScatteringUpsampleCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TDFogScattering.Upsample Level%d %dx%d", LevelIndex, OutputSize.X, OutputSize.Y),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(OutputSize, ThreadGroupSize));

		return OutputTexture;
	}

	void AddCompositePass(
		FRDGBuilder& GraphBuilder,
		FGlobalShaderMap* ShaderMap,
		const FSceneView& View,
		const FScreenPassTexture& SceneColor,
		FRDGTextureRef SceneDepthTexture,
		FRDGTextureRef BlurTexture,
		FIntPoint BlurSize,
		FRDGTextureRef OutputTexture,
		const FTDFogScatteringSettings& Settings,
		const FTDFogScatteringFogParameters& FogParameters)
	{
		const FIntPoint SceneColorRectSize = SceneColor.ViewRect.Size();

		FTDFogScatteringCompositeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTDFogScatteringCompositeCS::FParameters>();
		PassParameters->View = View.ViewUniformBuffer;
		SetupTDFogScatteringMediaParameters(GraphBuilder, View, Settings, PassParameters->Media);
		PassParameters->SceneColorTexture = SceneColor.Texture;
		PassParameters->SceneDepthTexture = SceneDepthTexture;
		PassParameters->BlurTexture = BlurTexture;
		PassParameters->BlurSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->BlurExtentInverse = FVector2f(1.0f / BlurSize.X, 1.0f / BlurSize.Y);
		PassParameters->SceneColorRectMinimum = SceneColor.ViewRect.Min;
		PassParameters->SceneColorRectSize = SceneColorRectSize;
		PassParameters->ExponentialFogParameters = FogParameters.ExponentialFogParameters;
		PassParameters->ExponentialFogParameters2 = FogParameters.ExponentialFogParameters2;
		PassParameters->ExponentialFogParameters3 = FogParameters.ExponentialFogParameters3;
		PassParameters->FogMaxOpacity = FogParameters.FogMaxOpacity;
		PassParameters->FogEndDistance = FogParameters.FogEndDistance;
		PassParameters->FogOnlyOnRenderedOpaque = FogParameters.FogOnlyOnRenderedOpaque;
		PassParameters->FogDistanceScale = Settings.DistanceScale;
		PassParameters->ScatteringIntensity = Settings.Intensity;
		PassParameters->ScatteringWeightPower = Settings.WeightPower;
		PassParameters->DebugView = Settings.DebugView;
		PassParameters->OutputTexture = GraphBuilder.CreateUAV(OutputTexture);

		FTDFogScatteringCompositeCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FTDFogScatteringCompositeCS::FTDUpsampleTapCountDimension>(GetUpsampleTapCount(Settings.QualityPreset));
		TShaderRef<FTDFogScatteringCompositeCS> ComputeShader = ShaderMap->GetShader<FTDFogScatteringCompositeCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TDFogScattering.Composite %dx%d", SceneColorRectSize.X, SceneColorRectSize.Y),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(SceneColorRectSize, ThreadGroupSize));
	}
}

FScreenPassTexture AddTDFogScatteringPasses(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FPostProcessMaterialInputs& Inputs,
	const FTDFogScatteringSettings& Settings,
	const FTDFogScatteringFogParameters& FogParameters)
{
	FRDGTextureRef SceneDepthTexture = GetSceneDepthTexture(Inputs);
	if (SceneDepthTexture == nullptr || View.GetFeatureLevel() < ERHIFeatureLevel::SM5)
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));
	if (!SceneColor.IsValid())
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	const FIntPoint ViewSize = SceneColor.ViewRect.Size();
	if (FMath::Min(ViewSize.X, ViewSize.Y) < MinimumViewSideLength)
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	RDG_EVENT_SCOPE_STAT(GraphBuilder, TDFogScattering, "TDFogScattering");

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	const FTDFogScatteringLevelSizes LevelSizes = ComputePyramidLevelSizes(ViewSize, Settings.PassAmount);
	const int32 LevelCount = LevelSizes.Num() - 1;
	const EPixelFormat PyramidFormat = ResolvePyramidFormat(SceneColor.Texture->Desc.Format, Settings.Format);
	const int32 DownsampleTapCount = GetDownsampleTapCount(Settings.QualityPreset);
	const int32 UpsampleTapCount = GetUpsampleTapCount(Settings.QualityPreset);

	TArray<FRDGTextureRef, TInlineAllocator<16>> DownsampleTextures;
	DownsampleTextures.Add(SceneColor.Texture);
	FIntRect InputRect = SceneColor.ViewRect;
	for (int32 LevelIndex = 1; LevelIndex <= LevelCount; ++LevelIndex)
	{
		const bool bShouldWeightLuminance = LevelIndex == 1 && Settings.bShouldWeightLuminance;
		DownsampleTextures.Add(AddDownsamplePass(GraphBuilder, ShaderMap, DownsampleTextures.Last(), InputRect, LevelIndex, LevelSizes[LevelIndex], PyramidFormat, DownsampleTapCount, bShouldWeightLuminance));
		InputRect = FIntRect(FIntPoint::ZeroValue, LevelSizes[LevelIndex]);
	}

	FRDGTextureRef BlurTexture = DownsampleTextures[LevelCount];
	for (int32 LevelIndex = LevelCount - 1; LevelIndex >= 1; --LevelIndex)
	{
		BlurTexture = AddUpsamplePass(GraphBuilder, ShaderMap, DownsampleTextures[LevelIndex], BlurTexture, LevelSizes[LevelIndex + 1], LevelIndex, LevelSizes[LevelIndex], PyramidFormat, UpsampleTapCount, Settings.Radius);
	}

	FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(CreateOutputDescription(SceneColor.Texture->Desc), TEXT("TDFogScattering.Output"));
	AddCompositePass(GraphBuilder, ShaderMap, View, SceneColor, SceneDepthTexture, BlurTexture, LevelSizes[1], OutputTexture, Settings, FogParameters);

	const FScreenPassTexture Output(OutputTexture, SceneColor.ViewRect);
	if (!Inputs.OverrideOutput.IsValid())
	{
		return Output;
	}

	AddDrawTexturePass(GraphBuilder, FScreenPassViewInfo(View), Output, Inputs.OverrideOutput);
	return FScreenPassTexture(Inputs.OverrideOutput.Texture, Inputs.OverrideOutput.ViewRect);
}
