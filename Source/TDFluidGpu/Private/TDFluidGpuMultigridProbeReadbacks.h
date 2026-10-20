#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "Templates/UniquePtr.h"

class FRHIGPUTextureReadback;
class FTDFluidGpuBatch;

struct FTDFluidGpuMultigridProbeReadbacks
{
	uint32 SliceIndex = 0;
	TUniquePtr<FRHIGPUTextureReadback> Velocity;
	TUniquePtr<FRHIGPUTextureReadback> ObstacleSdf;
	TArray<int32> RecordedCycleCounts;
	TArray<TUniquePtr<FRHIGPUTextureReadback>> Pressures;
};

void AddTDFluidGpuMultigridProbePasses(FRDGBuilder& GraphBuilder, FTDFluidGpuBatch& Batch, int32 EntryIndex, const TArray<int32>& RecordedCycleCounts, FTDFluidGpuMultigridProbeReadbacks& OutReadbacks);
