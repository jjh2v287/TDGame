#include "FluidFX/TDFluidLiveComponent.h"

#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidSimStateCacheTypes.h"
#include "GameFramework/Actor.h"

namespace
{
	void TDFluidSimStateCacheFillGpuHeader(const FTDFluidGpuSolverProxy& Proxy, const FTDFluidSimulationConfig& Config, FTDFluidSimStateSnapshot& OutHeader)
	{
		OutHeader.LayoutVersion = TDFluidSimStateCacheLayoutVersion;
		OutHeader.Backend = ETDFluidSimStateCacheBackend::Gpu;
		OutHeader.Resolution = Proxy.GetResolution();
		OutHeader.SimExtentCm = Proxy.GetExtentCm();
		OutHeader.CenterPlaneCm = Proxy.GetCenterPlaneCm();
		OutHeader.AreaAddressMode = Config.AreaAddressMode;
		OutHeader.SimMode = Config.SimMode;
		OutHeader.bUsesHeatChannel = Proxy.GetSettings().bUsesHeatChannel;
		OutHeader.bUsesWaterLayer = Proxy.GetSettings().bUsesWaterLayer;
		OutHeader.SimulationTimeSeconds = Proxy.GetSimulationTimeSeconds();
	}
}

void UTDFluidLiveComponent::QueueSimStateSeed(TSharedPtr<const FTDFluidSimStateSnapshot, ESPMode::ThreadSafe> InSeed)
{
	PendingSimStateSeed = MoveTemp(InSeed);
}

bool UTDFluidLiveComponent::CaptureSimStateSnapshot(FTDFluidSimStateSnapshot& OutSnapshot, FString& OutError)
{
	OutError.Reset();
	if (bUsesGpuSolver)
	{
		UTDFluidLiveSubsystem* FluidSubsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(this);
		if (!FluidSubsystem || !GpuSolverProxy.IsInitialized() || !GpuSolverProxy.HasActorKey())
		{
			OutError = TEXT("GPU solver has no submitted state");
			return false;
		}

		TDFluidSimStateCacheFillGpuHeader(GpuSolverProxy, SimulationConfig, OutSnapshot);
		return FluidSubsystem->ReadGpuActorState(GpuSolverProxy.GetActorKey(), OutSnapshot.GpuLayout, OutSnapshot.Channels, OutError);
	}

	if (!Solver.IsInitialized())
	{
		OutError = TEXT("CPU solver is not initialized");
		return false;
	}

	Solver.CaptureStateSnapshot(OutSnapshot);
	return true;
}

void UTDFluidLiveComponent::ConsumePendingCpuSimStateSeed()
{
	if (!PendingSimStateSeed.IsValid())
	{
		return;
	}

	const TSharedPtr<const FTDFluidSimStateSnapshot, ESPMode::ThreadSafe> Seed = MoveTemp(PendingSimStateSeed);
	PendingSimStateSeed.Reset();
	FString RejectReason;
	if (!Solver.RestoreStateSnapshot(*Seed, RejectReason))
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: simulation state cache seed rejected: %s"), *GetNameSafe(GetOwner()), *RejectReason);
	}
}

void UTDFluidLiveComponent::AppendPendingGpuSimStateSeed(FTDFluidGpuFrameInputs& InOutFrameInputs)
{
	if (!PendingSimStateSeed.IsValid())
	{
		return;
	}

	const TSharedPtr<const FTDFluidSimStateSnapshot, ESPMode::ThreadSafe> Seed = MoveTemp(PendingSimStateSeed);
	PendingSimStateSeed.Reset();
	FTDFluidSimStateSnapshot CurrentHeader;
	TDFluidSimStateCacheFillGpuHeader(GpuSolverProxy, SimulationConfig, CurrentHeader);
	const FString RejectReason = TDFluidSimStateCache::DescribeHeaderMismatch(*Seed, CurrentHeader);
	if (!RejectReason.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: simulation state cache seed rejected: %s"), *GetNameSafe(GetOwner()), *RejectReason);
		return;
	}

	FTDFluidGpuStateSeedRequest& Request = InOutFrameInputs.StateSeeds.AddDefaulted_GetRef();
	Request.ActorKey = GpuSolverProxy.GetActorKey();
	Request.Layout = Seed->GpuLayout;
	Request.Channels = TSharedPtr<const TArray<FTDFluidStateChannelBytes>, ESPMode::ThreadSafe>(Seed, &Seed->Channels);
	GpuSolverProxy.SetSimulationTimeSeconds(Seed->SimulationTimeSeconds);
}
