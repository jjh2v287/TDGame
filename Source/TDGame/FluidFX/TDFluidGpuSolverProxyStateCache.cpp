#include "FluidFX/TDFluidGpuSolverProxy.h"

#include "TDFluidGpuStateCache.h"

bool FTDFluidGpuSolverRenderState::ReadActorState(FRHICommandListImmediate& RHICmdList, uint64 ActorKey, FTDFluidGpuStateLayout& OutLayout, TArray<FTDFluidStateChannelBytes>& OutChannels, FString& OutError)
{
	return ReadTDFluidGpuActorState(RHICmdList, Batches, ActorKey, OutLayout, OutChannels, OutError);
}
