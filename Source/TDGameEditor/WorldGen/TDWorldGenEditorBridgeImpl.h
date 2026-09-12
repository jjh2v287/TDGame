#pragma once

#include "CoreMinimal.h"
#include "TDWorldGenEditorBridge.h"

class TDGAMEEDITOR_API FTDWorldGenEditorBridgeImpl : public ITDWorldGenEditorBridge
{
public:
	virtual bool GenerateAndBakeDungeon(UWorld* World, UTDDungeonAtlasDefinition* Atlas, int32 SlotIndex, int32 SeedOverride, FString& OutReport) override;
	virtual bool ValidateDungeonSlot(UWorld* World, UTDDungeonAtlasDefinition* Atlas, int32 SlotIndex, FString& OutReport) override;
	virtual bool GenerateOutdoor(UWorld* World, UTDWorldDefinition* WorldDefinition, int32 Seed, bool bBake, FString& OutReport) override;
	virtual bool ValidateOutdoor(UWorld* World, UTDWorldDefinition* WorldDefinition, FString& OutReport) override;
};
