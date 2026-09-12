#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"

class UTDDungeonAtlasDefinition;
class UTDWorldDefinition;
class UWorld;

class TDWORLDGEN_API ITDWorldGenEditorBridge
{
public:
	virtual ~ITDWorldGenEditorBridge() = default;
	virtual bool GenerateAndBakeDungeon(UWorld* World, UTDDungeonAtlasDefinition* Atlas, int32 SlotIndex, int32 SeedOverride, FString& OutReport) = 0;
	virtual bool ValidateDungeonSlot(UWorld* World, UTDDungeonAtlasDefinition* Atlas, int32 SlotIndex, FString& OutReport) = 0;
	virtual bool GenerateOutdoor(UWorld* World, UTDWorldDefinition* WorldDefinition, int32 Seed, bool bBake, FString& OutReport) = 0;
	virtual bool ValidateOutdoor(UWorld* World, UTDWorldDefinition* WorldDefinition, FString& OutReport) = 0;
};

struct TDWORLDGEN_API FTDWorldGenEditorBridge
{
	static void Set(TSharedPtr<ITDWorldGenEditorBridge> InBridge);
	static ITDWorldGenEditorBridge* Get();
};
