#include "FluidFX/TDFluidLiveSubsystem.h"

#include "FluidFX/TDFluidGpuSolverProxy.h"
#include "RenderingThread.h"

bool UTDFluidLiveSubsystem::ReadGpuActorState(uint64 ActorKey, FTDFluidGpuStateLayout& OutLayout, TArray<FTDFluidStateChannelBytes>& OutChannels, FString& OutError)
{
	OutChannels.Reset();
	OutError.Reset();
	if (ActorKey == 0)
	{
		OutError = TEXT("actor has no GPU solver key");
		return false;
	}

	if (!GpuSolverRenderState)
	{
		OutError = TEXT("GPU solver has not run a frame");
		return false;
	}

	FlushRenderingCommands();
	bool bWasRead = false;
	ENQUEUE_RENDER_COMMAND(TDFluidGpuReadActorState)(
		[RenderState = GpuSolverRenderState, ActorKey, &OutLayout, &OutChannels, &OutError, &bWasRead](FRHICommandListImmediate& RHICmdList)
		{
			bWasRead = RenderState->ReadActorState(RHICmdList, ActorKey, OutLayout, OutChannels, OutError);
		});
	FlushRenderingCommands();
	return bWasRead;
}
