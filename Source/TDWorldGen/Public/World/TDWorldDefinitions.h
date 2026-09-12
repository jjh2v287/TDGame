#pragma once

#include "Engine/DataAsset.h"
#include "World/TDWorldTypes.h"
#include "TDWorldDefinitions.generated.h"

class UStaticMesh;
class UTDDungeonAtlasDefinition;

UCLASS(BlueprintType)
class TDWORLDGEN_API UTDPoiArchetype : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "POI")
	FName PoiId = TEXT("Camp");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "POI")
	ETDPoiKind Kind = ETDPoiKind::Camp;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "POI", meta = (ClampMin = "0"))
	float ExclusionRadiusCm = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "POI", meta = (ClampMin = "0"))
	float RequiredClearRadiusCm = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "POI", meta = (ClampMin = "0"))
	float MaxSlopeDeg = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "POI")
	bool bRequiresCombatSpace = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "POI")
	TArray<TSoftObjectPtr<UWorld>> LevelInstanceCandidates;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

UCLASS(BlueprintType)
class TDWORLDGEN_API UTDBiomeDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome")
	FName BiomeId = TEXT("Forest");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assets")
	TArray<TSoftObjectPtr<UStaticMesh>> TreeSet;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assets")
	TArray<TSoftObjectPtr<UStaticMesh>> RockSet;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assets")
	TArray<TSoftObjectPtr<UStaticMesh>> BushSet;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assets")
	TArray<TSoftObjectPtr<UStaticMesh>> GroundClutterSet;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Density", meta = (ClampMin = "0"))
	float TreeDensityPer100SqM = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Density", meta = (ClampMin = "0"))
	float MinTreeDistanceCm = 370.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Density", meta = (ClampMin = "0", ClampMax = "90"))
	float MaxSlopeDeg = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clearance", meta = (ClampMin = "0"))
	float RoadClearanceCm = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clearance", meta = (ClampMin = "0"))
	float PoiClearanceCm = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PCG")
	TSoftObjectPtr<UObject> PcgGraph;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

USTRUCT(BlueprintType)
struct TDWORLDGEN_API FTDPoiQuota
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<UTDPoiArchetype> Archetype;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FIntPoint CountRange = FIntPoint(1, 2);
};

UCLASS(BlueprintType)
class TDWORLDGEN_API UTDRegionDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region")
	FName RegionId = TEXT("Forest");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region")
	TSoftObjectPtr<UTDBiomeDefinition> Biome;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Counts", meta = (ClampMin = "0"))
	int32 TownCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Counts", meta = (ClampMin = "0"))
	int32 MainDungeonCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Counts", meta = (ClampMin = "0"))
	int32 SideDungeonCount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Counts")
	TArray<FTDPoiQuota> PoiQuotas;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Counts")
	FIntPoint EventAreaRange = FIntPoint(2, 4);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Counts")
	FIntPoint ArenaRange = FIntPoint(2, 3);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spacing", meta = (ClampMin = "0"))
	float MinEntranceSpacingCm = 15000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spacing", meta = (ClampMin = "0"))
	float MinPoiSpacingCm = 4000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spacing", meta = (ClampMin = "0"))
	float BorderMarginCm = 6000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roads", meta = (ClampMin = "0"))
	float RoadWidthCm = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roads", meta = (ClampMin = "0", ClampMax = "1"))
	float SecondaryRoadRatio = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain", meta = (ClampMin = "0", ClampMax = "90"))
	float MaxPlacementSlopeDeg = 25.0f;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

UCLASS(BlueprintType)
class TDWORLDGEN_API UTDWorldDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World")
	FName WorldId = TEXT("Main");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World")
	TArray<TSoftObjectPtr<UTDRegionDefinition>> Regions;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World")
	TSoftObjectPtr<UTDDungeonAtlasDefinition> DungeonAtlas;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World")
	FBox2D FieldBoundsCm = FBox2D(FVector2D(-50400.0, -50400.0), FVector2D(50400.0, 50400.0));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World")
	TArray<FTDWorldAnchor> HandAuthoredAnchors;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
