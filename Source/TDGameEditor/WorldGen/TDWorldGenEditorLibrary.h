#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Dungeon/TDDungeonTypes.h"
#include "World/TDWorldTypes.h"
#include "TDWorldGenEditorLibrary.generated.h"

class ALandscapeProxy;
class UTDDungeonAtlasDefinition;
class UTDDungeonFlowTemplate;
class UTDDungeonTheme;
class UTDRegionDefinition;
struct FTDTerrainSampler;

UCLASS()
class TDGAMEEDITOR_API UTDWorldGenEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "TD|WorldGen")
	static bool GenerateDungeonLayout(UTDDungeonTheme* Theme, UTDDungeonFlowTemplate* FlowTemplate, ETDDungeonSize Size, int32 Seed, FTDDungeonLayout& OutLayout, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "TD|WorldGen", meta = (WorldContext = "WorldContextObject"))
	static bool BakeDungeonToSlot(UObject* WorldContextObject, const FTDDungeonLayout& Layout, UTDDungeonAtlasDefinition* Atlas, int32 SlotIndex, FName DungeonId, bool bClearExisting, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "TD|WorldGen", meta = (WorldContext = "WorldContextObject"))
	static bool GenerateAndBakeDungeon(UObject* WorldContextObject, UTDDungeonTheme* Theme, UTDDungeonFlowTemplate* FlowTemplate, ETDDungeonSize Size, int32 Seed, UTDDungeonAtlasDefinition* Atlas, int32 SlotIndex, FName DungeonId, FString& OutReportMarkdown);

	UFUNCTION(BlueprintCallable, Category = "TD|WorldGen")
	static FString ExportDungeonLayoutJson(const FTDDungeonLayout& Layout, FVector WorldOffsetCm);

	UFUNCTION(BlueprintCallable, Category = "TD|WorldGen", meta = (WorldContext = "WorldContextObject"))
	static bool GenerateWorldLayout(UObject* WorldContextObject, UTDRegionDefinition* Region, const TArray<FTDWorldAnchor>& Anchors, FBox2D BoundsCm, int32 Seed, ALandscapeProxy* LandscapeForSampling, UTDDungeonAtlasDefinition* Atlas, FTDWorldLayout& OutLayout, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "TD|WorldGen")
	static FTDValidationReport ValidateWorldLayout(const FTDWorldLayout& Layout, UTDDungeonAtlasDefinition* Atlas);

	UFUNCTION(BlueprintCallable, Category = "TD|WorldGen", meta = (WorldContext = "WorldContextObject"))
	static bool BakeWorldLayout(UObject* WorldContextObject, const FTDWorldLayout& Layout, bool bClearExisting, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "TD|WorldGen", meta = (WorldContext = "WorldContextObject"))
	static bool GenerateWorldLayoutWithLocked(UObject* WorldContextObject, UTDRegionDefinition* Region, const TArray<FTDWorldAnchor>& Anchors, const FTDLockedLayoutElements& Locked, FBox2D BoundsCm, int32 Seed, ALandscapeProxy* LandscapeForSampling, UTDDungeonAtlasDefinition* Atlas, FTDWorldLayout& OutLayout, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "TD|WorldGen", meta = (WorldContext = "WorldContextObject"))
	static bool BakeWorldLayoutInRegion(UObject* WorldContextObject, const FTDWorldLayout& Layout, FName RegionFilter, bool bClearExisting, FString& OutStats, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "TD|WorldGen", meta = (WorldContext = "WorldContextObject"))
	static FTDLockedLayoutElements CollectLockedLayoutElements(UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "TD|WorldGen", meta = (WorldContext = "WorldContextObject"))
	static TArray<FTDWorldAnchor> CollectWorldAnchorActors(UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "TD|WorldGen", meta = (WorldContext = "WorldContextObject"))
	static bool ResolveRegionVolumeBounds(UObject* WorldContextObject, FName RegionId, FBox2D& OutBoundsCm);

	UFUNCTION(BlueprintCallable, Category = "TD|WorldGen", meta = (WorldContext = "WorldContextObject"))
	static ALandscapeProxy* FindLandscapeForSampling(UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "TD|WorldGen")
	static void LogReportToMessageLog(const FTDValidationReport& Report, const FString& Title);

	UFUNCTION(BlueprintCallable, Category = "TD|WorldGen", meta = (WorldContext = "WorldContextObject"))
	static int32 RemoveActorsWithLabelPrefix(UObject* WorldContextObject, const FString& Prefix);

	UFUNCTION(BlueprintCallable, Category = "TD|WorldGen")
	static bool WriteTextFile(const FString& Path, const FString& Text);

	UFUNCTION(BlueprintCallable, Category = "TD|WorldGen")
	static FTDValidationReport ValidateRoomModuleLevels(UTDDungeonTheme* Theme);

	UFUNCTION(BlueprintCallable, Category = "TD|WorldGen", meta = (WorldContext = "WorldContextObject"))
	static FTDValidationReport ValidateDungeonNavigation(UObject* WorldContextObject, UTDDungeonAtlasDefinition* Atlas, int32 SlotIndex);

	UFUNCTION(BlueprintCallable, Category = "TD|WorldGen", meta = (WorldContext = "WorldContextObject"))
	static int32 RegenerateAllPcg(UObject* WorldContextObject);

	static void CollectLockedPlacements(UWorld* World, TArray<FTDWorldAnchor>& OutAnchors);

	static FVector ResolveSlotOriginCm(const UTDDungeonAtlasDefinition* Atlas, int32 SlotIndex);
	static void BuildLandscapeSampler(ALandscapeProxy* Landscape, FTDTerrainSampler& OutSampler);
	static FString ResolveProjectRelativePath(const FString& Path);
};
