#include "TDFluidGpuFog3DDensityProbe.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "RHIGPUReadback.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"

class FTDFluidGpuFog3DDensityProbeCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FTDFluidGpuFog3DDensityProbeCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuFog3DDensityProbeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, SampleInputs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ParameterSets)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, SampleOutputs)
		SHADER_PARAMETER(uint32, SampleCount)
		SHADER_PARAMETER(uint32, ParameterSetCount)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuFog3DDensityProbeCS, "/TDFluidGpu/Private/TDFluidGpuFog3DDensityProbe.usf", "Fog3DDensityProbeCS", SF_Compute);

class FTDFluidGpuFog3DGroundDensityProbeCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FTDFluidGpuFog3DGroundDensityProbeCS);
	using FParameters = FTDFluidGpuFog3DDensityProbeCS::FParameters;
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuFog3DGroundDensityProbeCS, FGlobalShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuFog3DGroundDensityProbeCS, "/TDFluidGpu/Private/TDFluidGpuFog3DDensityProbe.usf", "Fog3DGroundDensityProbeCS", SF_Compute);

namespace TDFluidGpuFog3DDensityProbePrivate
{
	constexpr int32 DensityProbeThreadsPerGroup = 64;

	int64 GetDensityProbeOutputByteCount(int32 SampleCount)
	{
		return static_cast<int64>(SampleCount) * TDFluidGpuFog3DDensityProbeOutputVectorsPerSample * sizeof(FVector4f);
	}

	template <typename ProbeShaderType>
	bool AddDensityProbePass(FRDGBuilder& GraphBuilder, TConstArrayView<FVector4f> SampleInputs, TConstArrayView<FVector4f> ParameterSets, FRHIGPUBufferReadback& Readback,
		int32 InputVectorsPerSample, int32 VectorsPerParameterSet, FRDGEventName&& PassName)
	{
		if (SampleInputs.Num() == 0 || SampleInputs.Num() % InputVectorsPerSample != 0)
		{
			return false;
		}

		if (ParameterSets.Num() == 0 || ParameterSets.Num() % VectorsPerParameterSet != 0)
		{
			return false;
		}

		const int32 SampleCount = SampleInputs.Num() / InputVectorsPerSample;
		const int64 OutputByteCount = GetDensityProbeOutputByteCount(SampleCount);
		if (OutputByteCount > MAX_uint32)
		{
			return false;
		}

		FRDGBufferRef InputBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.Fog3DDensityProbeInputs"), sizeof(FVector4f), SampleInputs.Num(), SampleInputs.GetData(), SampleInputs.NumBytes());
		FRDGBufferRef ParameterBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.Fog3DDensityProbeParameters"), sizeof(FVector4f), ParameterSets.Num(), ParameterSets.GetData(), ParameterSets.NumBytes());
		FRDGBufferRef OutputBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), SampleCount * TDFluidGpuFog3DDensityProbeOutputVectorsPerSample), TEXT("TDFluidGpu.Fog3DDensityProbeOutputs"));

		typename ProbeShaderType::FParameters* Parameters = GraphBuilder.AllocParameters<typename ProbeShaderType::FParameters>();
		Parameters->SampleInputs = GraphBuilder.CreateSRV(InputBuffer);
		Parameters->ParameterSets = GraphBuilder.CreateSRV(ParameterBuffer);
		Parameters->SampleOutputs = GraphBuilder.CreateUAV(OutputBuffer);
		Parameters->SampleCount = static_cast<uint32>(SampleCount);
		Parameters->ParameterSetCount = static_cast<uint32>(ParameterSets.Num() / VectorsPerParameterSet);
		TShaderMapRef<ProbeShaderType> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		const FIntVector GroupCount(FMath::DivideAndRoundUp(SampleCount, DensityProbeThreadsPerGroup), 1, 1);
		FComputeShaderUtils::AddPass(GraphBuilder, MoveTemp(PassName), Shader, Parameters, GroupCount);
		AddEnqueueCopyPass(GraphBuilder, &Readback, OutputBuffer, static_cast<uint32>(OutputByteCount));
		return true;
	}
}

bool AddTDFluidGpuFog3DDensityProbePass(FRDGBuilder& GraphBuilder, TConstArrayView<FVector4f> SampleInputs, TConstArrayView<FVector4f> ParameterSets, FRHIGPUBufferReadback& Readback)
{
	using namespace TDFluidGpuFog3DDensityProbePrivate;
	return AddDensityProbePass<FTDFluidGpuFog3DDensityProbeCS>(GraphBuilder, SampleInputs, ParameterSets, Readback,
		TDFluidGpuFog3DDensityProbeInputVectorsPerSample, TDFluidGpuFog3DDensityProbeVectorsPerParameterSet, RDG_EVENT_NAME("TDFluidGpu.Fog3D.DensityProbe"));
}

bool AddTDFluidGpuFog3DGroundDensityProbePass(FRDGBuilder& GraphBuilder, TConstArrayView<FVector4f> SampleInputs, TConstArrayView<FVector4f> ParameterSets, FRHIGPUBufferReadback& Readback)
{
	using namespace TDFluidGpuFog3DDensityProbePrivate;
	return AddDensityProbePass<FTDFluidGpuFog3DGroundDensityProbeCS>(GraphBuilder, SampleInputs, ParameterSets, Readback,
		TDFluidGpuFog3DGroundDensityProbeInputVectorsPerSample, TDFluidGpuFog3DGroundDensityProbeVectorsPerParameterSet, RDG_EVENT_NAME("TDFluidGpu.Fog3D.GroundDensityProbe"));
}


bool CopyTDFluidGpuFog3DDensityProbeOutputs(FRHIGPUBufferReadback& Readback, int32 SampleCount, TArray<FVector4f>& OutOutputs)
{
	using namespace TDFluidGpuFog3DDensityProbePrivate;

	OutOutputs.Reset();
	const int64 OutputByteCount = GetDensityProbeOutputByteCount(SampleCount);
	if (SampleCount <= 0 || OutputByteCount > MAX_uint32 || !Readback.IsReady())
	{
		return false;
	}

	const FVector4f* Values = static_cast<const FVector4f*>(Readback.Lock(static_cast<uint32>(OutputByteCount)));
	if (!Values)
	{
		return false;
	}

	OutOutputs.SetNumUninitialized(SampleCount * TDFluidGpuFog3DDensityProbeOutputVectorsPerSample);
	FMemory::Memcpy(OutOutputs.GetData(), Values, OutputByteCount);
	Readback.Unlock();
	return true;
}
