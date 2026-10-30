#include "FluidFX/TDFluidLiveSubsystem.h"

#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "TDGame.h"

namespace
{
	bool TDFluidOpsDoesClaimantKeepSharedOutput(const UTDFluidLiveComponent* Claimant, const UTDFluidLiveComponent* Requester)
	{
		if (!Claimant || Claimant == Requester || !Claimant->IsSimulationAwake())
		{
			return false;
		}

		const ATDFluidLiveActor* ClaimantActor = Cast<ATDFluidLiveActor>(Claimant->GetOwner());
		return !ClaimantActor || !ClaimantActor->IsPooledInactive();
	}
}

bool UTDFluidLiveSubsystem::ClaimSharedOutput(const UObject* SharedOutput, UTDFluidLiveComponent* Requester)
{
	if (!SharedOutput || !Requester)
	{
		return false;
	}

	const FObjectKey OutputKey(SharedOutput);
	TWeakObjectPtr<UTDFluidLiveComponent>& Claimant = SharedOutputClaims.FindOrAdd(OutputKey);
	const UTDFluidLiveComponent* CurrentClaimant = Claimant.Get();
	if (!TDFluidOpsDoesClaimantKeepSharedOutput(CurrentClaimant, Requester))
	{
		Claimant = Requester;
		return true;
	}

	bool bWasAlreadyWarned = false;
	WarnedSharedOutputs.Add(OutputKey, &bWasAlreadyWarned);
	if (!bWasAlreadyWarned)
	{
		UE_LOG(LogTDGame, Warning, TEXT("FluidFX shared output %s is claimed by %s; %s does not write it"),
			*GetNameSafe(SharedOutput), *GetNameSafe(CurrentClaimant->GetOwner()), *GetNameSafe(Requester->GetOwner()));
	}
	return false;
}

bool UTDFluidLiveSubsystem::IsSharedOutputClaimedBy(const UObject* SharedOutput, const UTDFluidLiveComponent* Component) const
{
	const TWeakObjectPtr<UTDFluidLiveComponent>* Claimant = SharedOutput ? SharedOutputClaims.Find(FObjectKey(SharedOutput)) : nullptr;
	return Claimant && Component && Claimant->Get() == Component;
}

bool UTDFluidLiveSubsystem::FindFluidSampleAtWorldLocation(const FVector& WorldLocation, FTDFluidCellSample& OutSample) const
{
	bool bHasSample = false;
	double BestDistanceSquared = 0.0;
	const FVector2D WorldLocationXY(WorldLocation.X, WorldLocation.Y);
	for (const TWeakObjectPtr<ATDFluidLiveActor>& RegisteredActor : RegisteredActors)
	{
		const ATDFluidLiveActor* LiveActor = RegisteredActor.Get();
		const UTDFluidLiveComponent* LiveComponent = LiveActor ? LiveActor->LiveComponent.Get() : nullptr;
		if (!LiveComponent || LiveActor->IsPooledInactive() || !LiveComponent->IsSimulationAwake() || !LiveComponent->GetPlaneBasis().IsWorldFacing())
		{
			continue;
		}

		FTDFluidCellSample CandidateSample;
		if (!LiveComponent->SampleFluidAtWorldLocation(WorldLocation, CandidateSample))
		{
			continue;
		}

		const double DistanceSquared = FVector2D::DistSquared(LiveComponent->GetSimulationCenterWorld(), WorldLocationXY);
		if (bHasSample && DistanceSquared >= BestDistanceSquared)
		{
			continue;
		}

		BestDistanceSquared = DistanceSquared;
		OutSample = CandidateSample;
		bHasSample = true;
	}
	return bHasSample;
}
