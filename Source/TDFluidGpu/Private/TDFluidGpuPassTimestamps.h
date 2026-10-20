#pragma once

#include "CoreMinimal.h"
#include "RHIResources.h"
#include "RenderGraphFwd.h"

class FTDFluidGpuBatch;
struct FTDFluidGpuFrameInputs;
struct FTDFluidGpuFrameOutputs;

struct FTDFluidGpuPassTimestamps
{
	TArray<FString> Labels;
	TArray<FRenderQueryRHIRef> Queries;
};

FTDFluidGpuFrameOutputs AddTDFluidGpuTimestampedFramePasses(FRDGBuilder& GraphBuilder, const FTDFluidGpuFrameInputs& FrameInputs, TArray<FTDFluidGpuBatch>& InOutBatches, FTDFluidGpuPassTimestamps& OutTimestamps);
