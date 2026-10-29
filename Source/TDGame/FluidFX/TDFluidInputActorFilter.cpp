#include "FluidFX/TDFluidInputActorFilter.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"

namespace
{
	bool HarvestFilterIsExcluded(const AActor& Actor, TConstArrayView<TSoftObjectPtr<AActor>> ExcludedActors)
	{
		for (const TSoftObjectPtr<AActor>& ExcludedActor : ExcludedActors)
		{
			if (ExcludedActor.Get() == &Actor)
			{
				return true;
			}
		}
		return false;
	}

	bool HarvestFilterMatchesObjectType(const AActor& Actor, TConstArrayView<TEnumAsByte<EObjectTypeQuery>> ObjectTypes)
	{
		const UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(Actor.GetRootComponent());
		if (!RootPrimitive)
		{
			return false;
		}

		const TEnumAsByte<EObjectTypeQuery> ActorObjectType = UEngineTypes::ConvertToObjectType(RootPrimitive->GetCollisionObjectType());
		return ObjectTypes.Contains(ActorObjectType);
	}

	bool HarvestFilterMatchesAnyTag(const AActor& Actor, TConstArrayView<FName> Tags)
	{
		for (const FName& Tag : Tags)
		{
			if (Actor.ActorHasTag(Tag))
			{
				return true;
			}
		}
		return false;
	}
}

bool FTDFluidInputActorFilter::HasActiveRules(const FTDFluidInputActorFilterRules& Rules)
{
	return !Rules.ObjectTypes.IsEmpty() || !Rules.Tags.IsEmpty() || !Rules.ExcludedActors.IsEmpty() || Rules.LargeObjectExcludeRatio > 0.0f;
}

bool FTDFluidInputActorFilter::ShouldHarvest(const AActor& Actor, bool bIsExplicit, const FVector& BoundsExtentCm, bool bHasBounds, const FTDFluidInputActorFilterRules& Rules)
{
	if (HarvestFilterIsExcluded(Actor, Rules.ExcludedActors))
	{
		return false;
	}

	if (bIsExplicit)
	{
		return true;
	}

	if (!Rules.ObjectTypes.IsEmpty() && !HarvestFilterMatchesObjectType(Actor, Rules.ObjectTypes))
	{
		return false;
	}

	if (!Rules.Tags.IsEmpty() && !HarvestFilterMatchesAnyTag(Actor, Rules.Tags))
	{
		return false;
	}

	if (Rules.LargeObjectExcludeRatio <= 0.0f || !bHasBounds)
	{
		return true;
	}

	const double LargestFootprintCm = 2.0 * FMath::Max(BoundsExtentCm.X, BoundsExtentCm.Y);
	const double WindowLimitCm = static_cast<double>(Rules.LargeObjectExcludeRatio) * FMath::Min(Rules.WindowExtentCm.X, Rules.WindowExtentCm.Y);
	return LargestFootprintCm <= WindowLimitCm;
}
