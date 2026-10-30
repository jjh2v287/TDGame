#include "FluidFX/TDFluidSimulatePreviewSubsystem.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"

namespace TDFluidSimulatePreviewPrivate
{
	TAutoConsoleVariable<int32> CVarTDFluidSimulatePreview(
		TEXT("TD.FluidFX.SimulatePreview"),
		0,
		TEXT("1 wakes fluid live actors near the Simulate In Editor view location. 0 leaves activation to the player pawn."),
		ECVF_Default);
}

bool UTDFluidSimulatePreviewSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::PIE;
}

bool UTDFluidSimulatePreviewSubsystem::IsPreviewConsoleEnabled()
{
	return TDFluidSimulatePreviewPrivate::CVarTDFluidSimulatePreview.GetValueOnGameThread() != 0;
}

bool UTDFluidSimulatePreviewSubsystem::IsTickable() const
{
	return IsPreviewConsoleEnabled() && GEditor && GEditor->bIsSimulatingInEditor;
}

TStatId UTDFluidSimulatePreviewSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTDFluidSimulatePreviewSubsystem, STATGROUP_Tickables);
}

void UTDFluidSimulatePreviewSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!World || World->ViewLocationsRenderedLastFrame.IsEmpty())
	{
		return;
	}

	EvaluatePreviewActivation(*World, World->ViewLocationsRenderedLastFrame[0]);
}

int32 UTDFluidSimulatePreviewSubsystem::EvaluatePreviewActivation(UWorld& World, const FVector& ViewLocation)
{
	if (UGameplayStatics::GetPlayerPawn(&World, 0))
	{
		return 0;
	}

	int32 AwakeActorCount = 0;
	for (TActorIterator<ATDFluidLiveActor> It(&World); It; ++It)
	{
		ATDFluidLiveActor* LiveActor = *It;
		if (!LiveActor || LiveActor->IsPooledInactive() || !LiveActor->bShouldUseActivationVolume || !LiveActor->LiveComponent)
		{
			continue;
		}

		LiveActor->EvaluateActivationProximity(ViewLocation);
		AwakeActorCount += LiveActor->LiveComponent->IsSimulationAwake() ? 1 : 0;
	}
	return AwakeActorCount;
}
