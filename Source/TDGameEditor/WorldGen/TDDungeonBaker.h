#pragma once

#include "CoreMinimal.h"
#include "Dungeon/TDDungeonTypes.h"

class UWorld;

struct TDGAMEEDITOR_API FTDDungeonBaker
{
	static bool Bake(UWorld* World, const FTDDungeonLayout& Layout, const FVector& SlotOriginCm, FName DungeonId, int32 SlotIndex, bool bClearExisting, FString& OutError);
	static int32 ClearSlot(UWorld* World, int32 SlotIndex);
	static FString MakeLabelPrefix(int32 SlotIndex);
	static FString MakeFolderPath(int32 SlotIndex);
	static FBox ComputeWorldBounds(const FTDDungeonLayout& Layout, const FVector& SlotOriginCm);
};
