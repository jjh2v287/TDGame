#pragma once

#include "CoreMinimal.h"
#include "TDFluidGpuBatch.h"
#include "TDFluidGpuStateCacheTypes.h"

class FRHICommandListImmediate;

struct FTDFluidGpuStateCacheChannel
{
	const TCHAR* Name;
	TRefCountPtr<IPooledRenderTarget>& (*ResolveTexture)(FTDFluidGpuBatchStateTextures&);
};

TDFLUIDGPU_API TConstArrayView<FTDFluidGpuStateCacheChannel> GetTDFluidGpuStateCacheChannels();
TDFLUIDGPU_API FTDFluidGpuStateLayout MakeTDFluidGpuStateLayout(const FTDFluidGpuBatchKey& Key);
TDFLUIDGPU_API bool ReadTDFluidGpuActorState(FRHICommandListImmediate& RHICmdList, TArray<FTDFluidGpuBatch>& Batches, uint64 ActorKey, FTDFluidGpuStateLayout& OutLayout, TArray<FTDFluidStateChannelBytes>& OutChannels, FString& OutError);
TDFLUIDGPU_API int32 ApplyTDFluidGpuStateSeeds(FRHICommandListImmediate& RHICmdList, TConstArrayView<FTDFluidGpuStateSeedRequest> Seeds, TArray<FTDFluidGpuBatch>& Batches, TArray<FString>& OutErrors);
