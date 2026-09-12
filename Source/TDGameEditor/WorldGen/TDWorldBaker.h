#pragma once

#include "CoreMinimal.h"
#include "World/TDWorldTypes.h"

class AActor;
class UWorld;

struct TDGAMEEDITOR_API FTDWorldBakeOptions
{
	bool bClearExisting = true;
	FName RegionFilter;
	FBox2D RegionBoundsCm = FBox2D(ForceInit);
};

struct TDGAMEEDITOR_API FTDWorldBakeStats
{
	int32 RemovedCount = 0;
	int32 UpdatedCount = 0;
	int32 CreatedCount = 0;
	int32 LockedKeptCount = 0;
	int32 SkippedForLockedCount = 0;
	int32 ActorsOutsideRegionCount = 0;
	int32 LayoutElementsOutsideRegionCount = 0;

	FString ToString() const;
};

struct TDGAMEEDITOR_API FTDWorldBaker
{
	static bool Bake(UWorld* World, const FTDWorldLayout& Layout, bool bClearExisting, FString& OutError);
	static bool BakeWithOptions(UWorld* World, const FTDWorldLayout& Layout, const FTDWorldBakeOptions& Options, FTDWorldBakeStats& OutStats, FString& OutError);
	static int32 ClearGenerated(UWorld* World);
	static bool ResolveRegionBounds(UWorld* World, FName RegionId, FBox2D& OutBoundsCm);
	static void CollectLockedElements(UWorld* World, FTDLockedLayoutElements& OutLocked);
	static bool IsLockedActor(const AActor& Actor);
	static bool ReadStableId(const AActor& Actor, FGuid& OutStableId);
	static const TCHAR* GetLabelPrefix();
	static FName GetLockedTagName();
	static FName GetGeneratedTagName();
};
