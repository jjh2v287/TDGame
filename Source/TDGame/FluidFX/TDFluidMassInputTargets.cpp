#include "FluidFX/TDFluidMassInputTargets.h"

#include "HAL/IConsoleManager.h"
#include "Misc/ScopeLock.h"

namespace
{
	TAutoConsoleVariable<int32> CVarTDFluidMassInput(
		TEXT("TD.FluidFX.MassInput"),
		-1,
		TEXT("Mass input override: -1 follows the preset, 0 forces mass input off."),
		ECVF_Default);
}

bool TDFluidMassInput::IsAllowedByConsole()
{
	return CVarTDFluidMassInput.GetValueOnAnyThread() != 0;
}

void FTDFluidMassInputHub::PublishTargets(TArray<FTDFluidMassInputTarget>&& InTargets)
{
	FScopeLock TargetsLock(&TargetsMutex);
	Targets = MoveTemp(InTargets);
}

TArray<FTDFluidMassInputTarget> FTDFluidMassInputHub::GetTargets() const
{
	FScopeLock TargetsLock(&TargetsMutex);
	return Targets;
}

bool FTDFluidMassInputHub::FindNearestTarget(FName ChannelName, const FVector& WorldLocation, FTDFluidMassInputTarget& OutTarget) const
{
	FScopeLock TargetsLock(&TargetsMutex);
	const FTDFluidMassInputTarget* NearestTarget = nullptr;
	double NearestDistanceSquared = TNumericLimits<double>::Max();
	for (const FTDFluidMassInputTarget& Target : Targets)
	{
		if (Target.ChannelName != ChannelName)
		{
			continue;
		}

		const double DistanceSquared = FVector::DistSquared(Target.WindowCenterWorld, WorldLocation);
		const bool bIsCloser = DistanceSquared < NearestDistanceSquared;
		const bool bWinsTie = DistanceSquared == NearestDistanceSquared && NearestTarget && Target.ActorKey < NearestTarget->ActorKey;
		if (bIsCloser || bWinsTie)
		{
			NearestTarget = &Target;
			NearestDistanceSquared = DistanceSquared;
		}
	}

	if (!NearestTarget)
	{
		return false;
	}

	OutTarget = *NearestTarget;
	return true;
}
