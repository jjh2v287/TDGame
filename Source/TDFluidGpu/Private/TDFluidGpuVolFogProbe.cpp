#include "TDFluidGpuVolFogProbe.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "GlobalShader.h"
#include "HAL/IConsoleManager.h"
#include "PooledRenderTarget.h"
#include "RHIStaticStates.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "ShaderParameterStruct.h"
#include "TextureResource.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidGpuVolFogProbe, Log, All);

class FTDFluidGpuVolFogProbeObstacleMaskCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FTDFluidGpuVolFogProbeObstacleMaskCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuVolFogProbeObstacleMaskCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, ObstacleMaskOut)
		SHADER_PARAMETER(FIntVector, GridSize)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuVolFogProbeObstacleMaskCS, "/TDFluidGpu/Private/TDFluidGpuVolFogProbe.usf", "VolFogProbeObstacleMaskCS", SF_Compute);

class FTDFluidGpuVolFogProbeAdvectCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FTDFluidGpuVolFogProbeAdvectCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuVolFogProbeAdvectCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, VelocityIn)
		SHADER_PARAMETER_SAMPLER(SamplerState, VelocitySampler)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<uint>, ObstacleMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, VelocityOut)
		SHADER_PARAMETER(FIntVector, GridSize)
		SHADER_PARAMETER(FVector3f, InverseGridSize)
		SHADER_PARAMETER(uint32, InputCount)
		SHADER_PARAMETER_ARRAY(FVector4f, InputPositionRadius, [8])
		SHADER_PARAMETER_ARRAY(FVector4f, InputVelocityStrength, [8])
		SHADER_PARAMETER(float, AdvectCellsPerStep)
		SHADER_PARAMETER(float, DisturbanceRecovery)
		SHADER_PARAMETER(float, VelocityDamping)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuVolFogProbeAdvectCS, "/TDFluidGpu/Private/TDFluidGpuVolFogProbe.usf", "VolFogProbeAdvectCS", SF_Compute);

class FTDFluidGpuVolFogProbeDivergenceCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FTDFluidGpuVolFogProbeDivergenceCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuVolFogProbeDivergenceCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, VelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<uint>, ObstacleMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, DivergenceOut)
		SHADER_PARAMETER(FIntVector, GridSize)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuVolFogProbeDivergenceCS, "/TDFluidGpu/Private/TDFluidGpuVolFogProbe.usf", "VolFogProbeDivergenceCS", SF_Compute);

class FTDFluidGpuVolFogProbePressureCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FTDFluidGpuVolFogProbePressureCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuVolFogProbePressureCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, PressureIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, DivergenceIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<uint>, ObstacleMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, PressureOut)
		SHADER_PARAMETER(FIntVector, GridSize)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuVolFogProbePressureCS, "/TDFluidGpu/Private/TDFluidGpuVolFogProbe.usf", "VolFogProbePressureCS", SF_Compute);

class FTDFluidGpuVolFogProbeGradientCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FTDFluidGpuVolFogProbeGradientCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuVolFogProbeGradientCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, VelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, PressureIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<uint>, ObstacleMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, VelocityOut)
		SHADER_PARAMETER(FIntVector, GridSize)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuVolFogProbeGradientCS, "/TDFluidGpu/Private/TDFluidGpuVolFogProbe.usf", "VolFogProbeGradientCS", SF_Compute);

class FTDFluidGpuVolFogProbeResidualColumnCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FTDFluidGpuVolFogProbeResidualColumnCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuVolFogProbeResidualColumnCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, VelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<uint>, ObstacleMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, ResidualColumnOut)
		SHADER_PARAMETER(FIntVector, GridSize)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuVolFogProbeResidualColumnCS, "/TDFluidGpu/Private/TDFluidGpuVolFogProbe.usf", "VolFogProbeResidualColumnCS", SF_Compute);

class FTDFluidGpuVolFogProbePatternCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FTDFluidGpuVolFogProbePatternCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuVolFogProbePatternCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, DensityOut)
		SHADER_PARAMETER(FIntVector, GridSize)
		SHADER_PARAMETER(FVector3f, InverseGridSize)
		SHADER_PARAMETER(uint32, PatternIndex)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuVolFogProbePatternCS, "/TDFluidGpu/Private/TDFluidGpuVolFogProbe.usf", "VolFogProbePatternCS", SF_Compute);

namespace TDFluidGpuVolFogProbePrivate
{
	constexpr int32 VolFogProbeGroupSizeX = 8;
	constexpr int32 VolFogProbeGroupSizeY = 8;
	constexpr int32 VolFogProbeGroupSizeZ = 4;
	constexpr int32 VolFogProbeInputCount = 8;
	constexpr float VolFogProbeAdvectCellsPerStep = 1.0f;
	constexpr float VolFogProbeDisturbanceRecovery = 0.02f;
	constexpr float VolFogProbeVelocityDamping = 0.995f;
	constexpr float VolFogProbeInputOrbitFraction = 0.12f;
	constexpr float VolFogProbeInputHeightFraction = 0.5f;
	constexpr float VolFogProbeInputFalloff = 1.0f / 50.0f;
	constexpr float VolFogProbeInputSpeed = 0.6f;
	constexpr float VolFogProbeInputLift = 0.1f;
	constexpr float VolFogProbeInputDisturbance = 0.2f;

	struct FTDFluidGpuVolFogProbeGraphTextures
	{
		FRDGTextureRef Velocity[2] = {};
		FRDGTextureRef Pressure[2] = {};
		FRDGTextureRef Divergence = nullptr;
		FRDGTextureRef ObstacleMask = nullptr;
	};

	FIntVector ComputeVolFogProbeGroupCount(const FIntVector& GridSize)
	{
		return FComputeShaderUtils::GetGroupCount(GridSize, FIntVector(VolFogProbeGroupSizeX, VolFogProbeGroupSizeY, VolFogProbeGroupSizeZ));
	}

	FVector3f ComputeVolFogProbeInverseGridSize(const FIntVector& GridSize)
	{
		return FVector3f(1.0f / GridSize.X, 1.0f / GridSize.Y, 1.0f / GridSize.Z);
	}

	bool IsVolFogProbeGridSizeValid(const FIntVector& GridSize)
	{
		return GridSize.X > 0 && GridSize.Y > 0 && GridSize.Z > 0;
	}

	FRDGTextureRef CreateVolFogProbeTexture(FRDGBuilder& GraphBuilder, const FIntVector& GridSize, EPixelFormat Format, const TCHAR* Name)
	{
		const FRDGTextureDesc Desc = FRDGTextureDesc::Create3D(GridSize, Format, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV);
		return GraphBuilder.CreateTexture(Desc, Name);
	}

	void AddVolFogProbeObstacleMaskPass(FRDGBuilder& GraphBuilder, FRDGTextureRef ObstacleMask, const FIntVector& GridSize)
	{
		FTDFluidGpuVolFogProbeObstacleMaskCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuVolFogProbeObstacleMaskCS::FParameters>();
		Parameters->ObstacleMaskOut = GraphBuilder.CreateUAV(ObstacleMask);
		Parameters->GridSize = GridSize;
		TShaderMapRef<FTDFluidGpuVolFogProbeObstacleMaskCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.VolFogProbe.ObstacleMask"), Shader, Parameters, ComputeVolFogProbeGroupCount(GridSize));
	}

	FTDFluidGpuVolFogProbeGraphTextures CreateVolFogProbeState(FRDGBuilder& GraphBuilder, FTDFluidGpuVolFogProbeState& State, const FIntVector& GridSize)
	{
		FTDFluidGpuVolFogProbeGraphTextures Textures;
		Textures.Velocity[0] = CreateVolFogProbeTexture(GraphBuilder, GridSize, PF_FloatRGBA, TEXT("TDFluidGpu.VolFogProbeVelocity"));
		Textures.Velocity[1] = CreateVolFogProbeTexture(GraphBuilder, GridSize, PF_FloatRGBA, TEXT("TDFluidGpu.VolFogProbeVelocity"));
		Textures.Pressure[0] = CreateVolFogProbeTexture(GraphBuilder, GridSize, PF_R32_FLOAT, TEXT("TDFluidGpu.VolFogProbePressure"));
		Textures.Pressure[1] = CreateVolFogProbeTexture(GraphBuilder, GridSize, PF_R32_FLOAT, TEXT("TDFluidGpu.VolFogProbePressure"));
		Textures.Divergence = CreateVolFogProbeTexture(GraphBuilder, GridSize, PF_R32_FLOAT, TEXT("TDFluidGpu.VolFogProbeDivergence"));
		Textures.ObstacleMask = CreateVolFogProbeTexture(GraphBuilder, GridSize, PF_R8_UINT, TEXT("TDFluidGpu.VolFogProbeObstacleMask"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Textures.Velocity[0]), FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Textures.Pressure[0]), 0.0f);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Textures.Pressure[1]), 0.0f);
		AddVolFogProbeObstacleMaskPass(GraphBuilder, Textures.ObstacleMask, GridSize);

		State.GridSize = GridSize;
		State.Velocity[0] = GraphBuilder.ConvertToExternalTexture(Textures.Velocity[0]);
		State.Velocity[1] = GraphBuilder.ConvertToExternalTexture(Textures.Velocity[1]);
		State.Pressure[0] = GraphBuilder.ConvertToExternalTexture(Textures.Pressure[0]);
		State.Pressure[1] = GraphBuilder.ConvertToExternalTexture(Textures.Pressure[1]);
		State.Divergence = GraphBuilder.ConvertToExternalTexture(Textures.Divergence);
		State.ObstacleMask = GraphBuilder.ConvertToExternalTexture(Textures.ObstacleMask);
		return Textures;
	}

	bool HasVolFogProbeState(const FTDFluidGpuVolFogProbeState& State, const FIntVector& GridSize)
	{
		return State.GridSize == GridSize
			&& State.Velocity[0].IsValid() && State.Velocity[1].IsValid()
			&& State.Pressure[0].IsValid() && State.Pressure[1].IsValid()
			&& State.Divergence.IsValid() && State.ObstacleMask.IsValid();
	}

	FTDFluidGpuVolFogProbeGraphTextures RegisterVolFogProbeState(FRDGBuilder& GraphBuilder, FTDFluidGpuVolFogProbeState& State, const FIntVector& GridSize)
	{
		if (!HasVolFogProbeState(State, GridSize))
		{
			return CreateVolFogProbeState(GraphBuilder, State, GridSize);
		}

		FTDFluidGpuVolFogProbeGraphTextures Textures;
		Textures.Velocity[0] = GraphBuilder.RegisterExternalTexture(State.Velocity[0]);
		Textures.Velocity[1] = GraphBuilder.RegisterExternalTexture(State.Velocity[1]);
		Textures.Pressure[0] = GraphBuilder.RegisterExternalTexture(State.Pressure[0]);
		Textures.Pressure[1] = GraphBuilder.RegisterExternalTexture(State.Pressure[1]);
		Textures.Divergence = GraphBuilder.RegisterExternalTexture(State.Divergence);
		Textures.ObstacleMask = GraphBuilder.RegisterExternalTexture(State.ObstacleMask);
		return Textures;
	}

	void FillVolFogProbeInputs(const FIntVector& GridSize, FTDFluidGpuVolFogProbeAdvectCS::FParameters& Parameters)
	{
		Parameters.InputCount = VolFogProbeInputCount;
		for (int32 InputIndex = 0; InputIndex < VolFogProbeInputCount; ++InputIndex)
		{
			const float Angle = InputIndex * UE_PI / 4.0f;
			const float Cosine = FMath::Cos(Angle);
			const float Sine = FMath::Sin(Angle);
			Parameters.InputPositionRadius[InputIndex] = FVector4f(
				GridSize.X * 0.5f + Cosine * GridSize.X * VolFogProbeInputOrbitFraction,
				GridSize.Y * 0.5f + Sine * GridSize.Y * VolFogProbeInputOrbitFraction,
				GridSize.Z * VolFogProbeInputHeightFraction,
				VolFogProbeInputFalloff);
			Parameters.InputVelocityStrength[InputIndex] = FVector4f(-Sine * VolFogProbeInputSpeed, Cosine * VolFogProbeInputSpeed, VolFogProbeInputLift, VolFogProbeInputDisturbance);
		}
	}

	void AddVolFogProbeAdvectPass(FRDGBuilder& GraphBuilder, FRDGTextureRef VelocityIn, FRDGTextureRef ObstacleMask, FRDGTextureRef VelocityOut, const FIntVector& GridSize)
	{
		FTDFluidGpuVolFogProbeAdvectCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuVolFogProbeAdvectCS::FParameters>();
		Parameters->VelocityIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(VelocityIn));
		Parameters->VelocitySampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Parameters->ObstacleMask = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(ObstacleMask));
		Parameters->VelocityOut = GraphBuilder.CreateUAV(VelocityOut);
		Parameters->GridSize = GridSize;
		Parameters->InverseGridSize = ComputeVolFogProbeInverseGridSize(GridSize);
		Parameters->AdvectCellsPerStep = VolFogProbeAdvectCellsPerStep;
		Parameters->DisturbanceRecovery = VolFogProbeDisturbanceRecovery;
		Parameters->VelocityDamping = VolFogProbeVelocityDamping;
		FillVolFogProbeInputs(GridSize, *Parameters);
		TShaderMapRef<FTDFluidGpuVolFogProbeAdvectCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.VolFogProbe.Advect"), Shader, Parameters, ComputeVolFogProbeGroupCount(GridSize));
	}

	void AddVolFogProbeDivergencePass(FRDGBuilder& GraphBuilder, FRDGTextureRef VelocityIn, FRDGTextureRef ObstacleMask, FRDGTextureRef DivergenceOut, const FIntVector& GridSize)
	{
		FTDFluidGpuVolFogProbeDivergenceCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuVolFogProbeDivergenceCS::FParameters>();
		Parameters->VelocityIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(VelocityIn));
		Parameters->ObstacleMask = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(ObstacleMask));
		Parameters->DivergenceOut = GraphBuilder.CreateUAV(DivergenceOut);
		Parameters->GridSize = GridSize;
		TShaderMapRef<FTDFluidGpuVolFogProbeDivergenceCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.VolFogProbe.Divergence"), Shader, Parameters, ComputeVolFogProbeGroupCount(GridSize));
	}

	void AddVolFogProbePressurePass(FRDGBuilder& GraphBuilder, FRDGTextureRef PressureIn, FRDGTextureRef DivergenceIn, FRDGTextureRef ObstacleMask, FRDGTextureRef PressureOut, const FIntVector& GridSize)
	{
		FTDFluidGpuVolFogProbePressureCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuVolFogProbePressureCS::FParameters>();
		Parameters->PressureIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(PressureIn));
		Parameters->DivergenceIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(DivergenceIn));
		Parameters->ObstacleMask = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(ObstacleMask));
		Parameters->PressureOut = GraphBuilder.CreateUAV(PressureOut);
		Parameters->GridSize = GridSize;
		TShaderMapRef<FTDFluidGpuVolFogProbePressureCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.VolFogProbe.Pressure"), Shader, Parameters, ComputeVolFogProbeGroupCount(GridSize));
	}

	void AddVolFogProbeGradientPass(FRDGBuilder& GraphBuilder, FRDGTextureRef VelocityIn, FRDGTextureRef PressureIn, FRDGTextureRef ObstacleMask, FRDGTextureRef VelocityOut, const FIntVector& GridSize)
	{
		FTDFluidGpuVolFogProbeGradientCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuVolFogProbeGradientCS::FParameters>();
		Parameters->VelocityIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(VelocityIn));
		Parameters->PressureIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(PressureIn));
		Parameters->ObstacleMask = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(ObstacleMask));
		Parameters->VelocityOut = GraphBuilder.CreateUAV(VelocityOut);
		Parameters->GridSize = GridSize;
		TShaderMapRef<FTDFluidGpuVolFogProbeGradientCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.VolFogProbe.Gradient"), Shader, Parameters, ComputeVolFogProbeGroupCount(GridSize));
	}

	void AddVolFogProbeResidualColumnPass(FRDGBuilder& GraphBuilder, FRDGTextureRef VelocityIn, FRDGTextureRef ObstacleMask, const FIntVector& GridSize, TUniquePtr<FRHIGPUTextureReadback>& OutReadback, const TCHAR* Name)
	{
		const FRDGTextureDesc ColumnDesc = FRDGTextureDesc::Create2D(FIntPoint(GridSize.X, GridSize.Y), PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV);
		FRDGTextureRef ColumnTexture = GraphBuilder.CreateTexture(ColumnDesc, Name);
		FTDFluidGpuVolFogProbeResidualColumnCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuVolFogProbeResidualColumnCS::FParameters>();
		Parameters->VelocityIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(VelocityIn));
		Parameters->ObstacleMask = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(ObstacleMask));
		Parameters->ResidualColumnOut = GraphBuilder.CreateUAV(ColumnTexture);
		Parameters->GridSize = GridSize;
		TShaderMapRef<FTDFluidGpuVolFogProbeResidualColumnCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.VolFogProbe.ResidualColumn"), Shader, Parameters, FComputeShaderUtils::GetGroupCount(FIntVector(GridSize.X, GridSize.Y, 1), FIntVector(VolFogProbeGroupSizeX, VolFogProbeGroupSizeY, 1)));
		OutReadback = MakeUnique<FRHIGPUTextureReadback>(Name);
		AddEnqueueCopyPass(GraphBuilder, OutReadback.Get(), ColumnTexture);
	}

	FTextureRenderTargetResource* FindVolFogProbeWritableResource(UTextureRenderTargetVolume* Target)
	{
		if (!Target)
		{
			return nullptr;
		}

		if (!Target->bCanCreateUAV)
		{
			UE_LOG(LogTDFluidGpuVolFogProbe, Error, TEXT("VolFogProbe pattern write needs a volume render target created with UAV support: %s"), *Target->GetPathName());
			return nullptr;
		}

		if (Target->GetFormat() != PF_FloatRGBA)
		{
			UE_LOG(LogTDFluidGpuVolFogProbe, Error, TEXT("VolFogProbe pattern write needs PF_FloatRGBA (RGBA16F): %s has %s"), *Target->GetPathName(), GetPixelFormatString(Target->GetFormat()));
			return nullptr;
		}

		return Target->GameThread_GetRenderTargetResource();
	}

	bool IsVolFogProbeWritableTexture(const FRHITexture* Texture)
	{
		return Texture
			&& Texture->GetDesc().Dimension == ETextureDimension::Texture3D
			&& Texture->GetFormat() == PF_FloatRGBA
			&& EnumHasAnyFlags(Texture->GetFlags(), TexCreate_UAV);
	}
}

void AddTDFluidGpuVolFogProbeStepPasses(FRDGBuilder& GraphBuilder, FTDFluidGpuVolFogProbeState& State, const FIntVector& GridSize, int32 PressureIterationCount, FTDFluidGpuVolFogProbeResidualReadbacks* ResidualReadbacks)
{
	using namespace TDFluidGpuVolFogProbePrivate;

	if (!IsVolFogProbeGridSizeValid(GridSize) || PressureIterationCount < 0)
	{
		return;
	}

	const FTDFluidGpuVolFogProbeGraphTextures Textures = RegisterVolFogProbeState(GraphBuilder, State, GridSize);
	AddVolFogProbeAdvectPass(GraphBuilder, Textures.Velocity[0], Textures.ObstacleMask, Textures.Velocity[1], GridSize);
	if (ResidualReadbacks)
	{
		AddVolFogProbeResidualColumnPass(GraphBuilder, Textures.Velocity[1], Textures.ObstacleMask, GridSize, ResidualReadbacks->BeforePressure, TEXT("TDFluidGpu.VolFogProbeResidualBefore"));
	}

	AddVolFogProbeDivergencePass(GraphBuilder, Textures.Velocity[1], Textures.ObstacleMask, Textures.Divergence, GridSize);
	for (int32 IterationIndex = 0; IterationIndex < PressureIterationCount; ++IterationIndex)
	{
		const int32 ReadIndex = IterationIndex % 2;
		AddVolFogProbePressurePass(GraphBuilder, Textures.Pressure[ReadIndex], Textures.Divergence, Textures.ObstacleMask, Textures.Pressure[1 - ReadIndex], GridSize);
	}

	const int32 FinalPressureIndex = PressureIterationCount % 2;
	AddVolFogProbeGradientPass(GraphBuilder, Textures.Velocity[1], Textures.Pressure[FinalPressureIndex], Textures.ObstacleMask, Textures.Velocity[0], GridSize);
	if (ResidualReadbacks)
	{
		AddVolFogProbeResidualColumnPass(GraphBuilder, Textures.Velocity[0], Textures.ObstacleMask, GridSize, ResidualReadbacks->AfterPressure, TEXT("TDFluidGpu.VolFogProbeResidualAfter"));
	}

	if (FinalPressureIndex != 0)
	{
		Swap(State.Pressure[0], State.Pressure[1]);
	}
}

void AddTDFluidGpuVolFogProbePatternPass(FRDGBuilder& GraphBuilder, FRDGTextureRef DensityTexture, int32 PatternIndex)
{
	using namespace TDFluidGpuVolFogProbePrivate;

	if (!DensityTexture || PatternIndex < 0 || PatternIndex >= TDFluidGpuVolFogProbePatternCount)
	{
		return;
	}

	const FIntVector GridSize = DensityTexture->Desc.GetSize();
	if (!IsVolFogProbeGridSizeValid(GridSize))
	{
		return;
	}

	FTDFluidGpuVolFogProbePatternCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuVolFogProbePatternCS::FParameters>();
	Parameters->DensityOut = GraphBuilder.CreateUAV(DensityTexture);
	Parameters->GridSize = GridSize;
	Parameters->InverseGridSize = ComputeVolFogProbeInverseGridSize(GridSize);
	Parameters->PatternIndex = static_cast<uint32>(PatternIndex);
	TShaderMapRef<FTDFluidGpuVolFogProbePatternCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.VolFogProbe.Pattern"), Shader, Parameters, ComputeVolFogProbeGroupCount(GridSize));
}

bool WriteTDFluidGpuVolFogProbePattern(UTextureRenderTargetVolume* Target, int32 PatternIndex)
{
	using namespace TDFluidGpuVolFogProbePrivate;

	if (PatternIndex < 0 || PatternIndex >= TDFluidGpuVolFogProbePatternCount)
	{
		return false;
	}

	FTextureRenderTargetResource* Resource = FindVolFogProbeWritableResource(Target);
	if (!Resource)
	{
		return false;
	}

	ENQUEUE_RENDER_COMMAND(TDFluidGpuVolFogProbeWritePattern)(
		[Resource, PatternIndex](FRHICommandListImmediate& RHICmdList)
		{
			FRHITexture* Texture = Resource->GetRenderTargetTexture().GetReference();
			if (!IsVolFogProbeWritableTexture(Texture))
			{
				UE_LOG(LogTDFluidGpuVolFogProbe, Error, TEXT("VolFogProbe pattern write skipped: render thread texture is not a writable RGBA16F volume"));
				return;
			}

			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuVolFogProbePattern"));
			FRDGTextureRef DensityTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Texture, TEXT("TDFluidGpu.VolFogProbeDensity")));
			AddTDFluidGpuVolFogProbePatternPass(GraphBuilder, DensityTexture, PatternIndex);
			GraphBuilder.SetTextureAccessFinal(DensityTexture, ERHIAccess::SRVMask);
			GraphBuilder.Execute();
		});
	return true;
}

#if !UE_BUILD_SHIPPING
namespace TDFluidGpuVolFogProbeCommandPrivate
{
	void ExecuteVolFogProbeWriteCommand(const TArray<FString>& Arguments)
	{
		if (Arguments.Num() < 2 || !Arguments[1].IsNumeric())
		{
			UE_LOG(LogTDFluidGpuVolFogProbe, Error, TEXT("Usage: TD.Fog3D.ProbeWrite <volume render target object path> <pattern 0-3>"));
			return;
		}

		const int32 PatternIndex = FCString::Atoi(*Arguments[1]);
		UTextureRenderTargetVolume* Target = FindObject<UTextureRenderTargetVolume>(nullptr, *Arguments[0]);
		if (!Target)
		{
			UE_LOG(LogTDFluidGpuVolFogProbe, Error, TEXT("TD.Fog3D.ProbeWrite could not find volume render target %s"), *Arguments[0]);
			return;
		}

		if (!WriteTDFluidGpuVolFogProbePattern(Target, PatternIndex))
		{
			UE_LOG(LogTDFluidGpuVolFogProbe, Error, TEXT("TD.Fog3D.ProbeWrite failed: %s pattern %d"), *Arguments[0], PatternIndex);
			return;
		}

		UE_LOG(LogTDFluidGpuVolFogProbe, Display, TEXT("TD.Fog3D.ProbeWrite wrote pattern %d to %s (%dx%dx%d)"), PatternIndex, *Target->GetPathName(), Target->SizeX, Target->SizeY, Target->SizeZ);
	}

	FAutoConsoleCommand VolFogProbeWriteCommand(
		TEXT("TD.Fog3D.ProbeWrite"),
		TEXT("Writes a fog probe pattern into a volume render target: <object path> <pattern 0=N 1=C 2=A 3=B>"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecuteVolFogProbeWriteCommand));
}
#endif
