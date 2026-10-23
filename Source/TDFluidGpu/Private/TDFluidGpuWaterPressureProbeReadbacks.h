#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "Templates/UniquePtr.h"

class FRHIGPUBufferReadback;
class FRHIGPUTextureReadback;
class FTDFluidGpuBatch;

struct FTDFluidGpuWaterPressureProbeReadbacks
{
	uint32 SliceIndex = 0;
	TUniquePtr<FRHIGPUTextureReadback> Velocity;
	TUniquePtr<FRHIGPUTextureReadback> CompositeSdf;
	TUniquePtr<FRHIGPUTextureReadback> SurfaceHeightField;
	TArray<int32> RecordedCycleCounts;
	TArray<TUniquePtr<FRHIGPUTextureReadback>> PressureFullByCycle;
	TUniquePtr<FRHIGPUBufferReadback> OpenBorderFlags;
};

void AddTDFluidGpuWaterPressureProbePasses(FRDGBuilder& GraphBuilder, FTDFluidGpuBatch& Batch, int32 EntryIndex, const TArray<int32>& RecordedCycleCounts, FTDFluidGpuWaterPressureProbeReadbacks& OutReadbacks);
