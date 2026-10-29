#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "UObject/SoftObjectPtr.h"

class AActor;

struct FTDFluidInputActorFilterRules
{
	TConstArrayView<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	TConstArrayView<FName> Tags;
	TConstArrayView<TSoftObjectPtr<AActor>> ExcludedActors;
	float LargeObjectExcludeRatio = 0.0f;
	FVector2D WindowExtentCm = FVector2D::ZeroVector;
};

struct TDGAME_API FTDFluidInputActorFilter
{
	static bool HasActiveRules(const FTDFluidInputActorFilterRules& Rules);
	static bool ShouldHarvest(const AActor& Actor, bool bIsExplicit, const FVector& BoundsExtentCm, bool bHasBounds, const FTDFluidInputActorFilterRules& Rules);
};
