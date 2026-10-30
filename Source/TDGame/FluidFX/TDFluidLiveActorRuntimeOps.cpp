#include "FluidFX/TDFluidLiveActor.h"

#include "Engine/World.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidRuntimeOps.h"
#include "FluidFX/TDFluidVolumeDriverComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

float ATDFluidLiveActor::ResolveStepRateHz() const
{
	if (!LiveComponent)
	{
		return 0.0f;
	}

	const FTDFluidRuntimeOpsSettings& Settings = LiveComponent->GetRuntimeOpsSettings();
	const float MaxStepRateHz = TDFluidRuntimeOps::ResolveMaxStepRateHz(Settings);
	if (MaxStepRateHz <= 0.0f || LiveComponent->AcceptsMassInput())
	{
		return 0.0f;
	}

	const UWorld* World = GetWorld();
	const APawn* PlayerPawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	if (!PlayerPawn)
	{
		return MaxStepRateHz;
	}

	const double ObserverDistanceCm = FMath::Sqrt(FVector::DistSquared2D(GetActorLocation(), PlayerPawn->GetActorLocation()));
	return TDFluidRuntimeOps::ComputeLodStepRateHz(Settings, MaxStepRateHz, ObserverDistanceCm);
}

bool ATDFluidLiveActor::ShouldPauseForVisibility() const
{
	if (!LiveComponent)
	{
		return false;
	}

	const FTDFluidRuntimeOpsSettings& Settings = LiveComponent->GetRuntimeOpsSettings();
	const int32 PauseMode = TDFluidRuntimeOps::ResolvePauseWhenNotVisibleMode(Settings);
	if (PauseMode <= 0)
	{
		return false;
	}

	const bool bDrivesVolumeTargets = VolumeDriver && VolumeDriver->GetDrivenTargetCount() > 0;
	const bool bIsEligible = bHasCompletedStepSinceWake
		&& !bDrivesVolumeTargets
		&& !HasOffscreenOutputConsumers()
		&& LiveComponent->IsEligibleForVisibilityPause();
	const UWorld* World = GetWorld();
	const bool bCanJudgeVisibility = World && TDFluidRuntimeOps::CanJudgeVisibility(*World);
	return TDFluidRuntimeOps::ShouldPauseForVisibility(PauseMode, bIsEligible, bCanJudgeVisibility, WasRecentlyRendered(Settings.VisibilityGraceSeconds));
}

void ATDFluidLiveActor::ResetStepClock()
{
	StepClock = FTDFluidStepClock();
}
