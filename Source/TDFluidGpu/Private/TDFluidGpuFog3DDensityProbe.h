#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"

class FRHIGPUBufferReadback;

constexpr int32 TDFluidGpuFog3DDensityProbeInputVectorsPerSample = 2;
constexpr int32 TDFluidGpuFog3DDensityProbeOutputVectorsPerSample = 2;
constexpr int32 TDFluidGpuFog3DDensityProbeVectorsPerParameterSet = 7;
constexpr int32 TDFluidGpuFog3DGroundDensityProbeInputVectorsPerSample = 3;
constexpr int32 TDFluidGpuFog3DGroundDensityProbeVectorsPerParameterSet = 8;

bool AddTDFluidGpuFog3DDensityProbePass(FRDGBuilder& GraphBuilder, TConstArrayView<FVector4f> SampleInputs, TConstArrayView<FVector4f> ParameterSets, FRHIGPUBufferReadback& Readback);
bool AddTDFluidGpuFog3DGroundDensityProbePass(FRDGBuilder& GraphBuilder, TConstArrayView<FVector4f> SampleInputs, TConstArrayView<FVector4f> ParameterSets, FRHIGPUBufferReadback& Readback);
bool CopyTDFluidGpuFog3DDensityProbeOutputs(FRHIGPUBufferReadback& Readback, int32 SampleCount, TArray<FVector4f>& OutOutputs);
