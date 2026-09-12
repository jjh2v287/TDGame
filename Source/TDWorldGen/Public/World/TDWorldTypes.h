#pragma once

#include "CoreMinimal.h"
#include "TDWorldGenTypes.h"
#include "TDWorldTypes.generated.h"

UENUM(BlueprintType)
enum class ETDPoiKind : uint8
{
	Camp,
	Shrine,
	Ruin,
	Graveyard,
	EventArea,
	Arena,
	Custom
};

UENUM(BlueprintType)
enum class ETDWorldAnchorKind : uint8
{
	Town,
	MainDungeon,
	Landmark,
	RegionBoundary,
	PlayerStart
};

USTRUCT(BlueprintType)
struct TDWORLDGEN_API FTDWorldAnchor
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName AnchorId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ETDWorldAnchorKind Kind = ETDWorldAnchorKind::Landmark;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector LocationCm = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float ExclusionRadiusCm = 5000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float YawDeg = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bLocked = true;
};

USTRUCT(BlueprintType)
struct TDWORLDGEN_API FTDPoiPlacement
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FName PoiId;

	UPROPERTY(BlueprintReadOnly)
	FName ArchetypeId;

	UPROPERTY(BlueprintReadOnly)
	ETDPoiKind Kind = ETDPoiKind::Custom;

	UPROPERTY(BlueprintReadOnly)
	FVector LocationCm = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	float Yaw = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float ExclusionRadiusCm = 2000.0f;

	UPROPERTY(BlueprintReadOnly)
	bool bLocked = false;

	UPROPERTY(BlueprintReadOnly)
	FGuid StableId;
};

USTRUCT(BlueprintType)
struct TDWORLDGEN_API FTDDungeonEntrancePlacement
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FName DungeonId;

	UPROPERTY(BlueprintReadOnly)
	FVector LocationCm = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	float Yaw = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	bool bIsMain = false;

	UPROPERTY(BlueprintReadOnly)
	bool bLocked = false;

	UPROPERTY(BlueprintReadOnly)
	FGuid StableId;
};

USTRUCT(BlueprintType)
struct TDWORLDGEN_API FTDRoadPolyline
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FName RoadId;

	UPROPERTY(BlueprintReadOnly)
	TArray<FVector> PointsCm;

	UPROPERTY(BlueprintReadOnly)
	float WidthCm = 600.0f;

	UPROPERTY(BlueprintReadOnly)
	float ClearanceCm = 800.0f;

	UPROPERTY(BlueprintReadOnly)
	bool bIsPrimary = true;

	UPROPERTY(BlueprintReadOnly)
	bool bLocked = false;

	UPROPERTY(BlueprintReadOnly)
	FGuid StableId;

	float LengthCm() const;
};

USTRUCT(BlueprintType)
struct TDWORLDGEN_API FTDLockedLayoutElements
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	TArray<FTDPoiPlacement> Pois;

	UPROPERTY(BlueprintReadOnly)
	TArray<FTDDungeonEntrancePlacement> Entrances;

	UPROPERTY(BlueprintReadOnly)
	TArray<FTDRoadPolyline> Roads;

	bool IsEmpty() const { return Pois.Num() == 0 && Entrances.Num() == 0 && Roads.Num() == 0; }
};

USTRUCT(BlueprintType)
struct TDWORLDGEN_API FTDExclusionArea
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FVector CenterCm = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	float RadiusCm = 1000.0f;

	UPROPERTY(BlueprintReadOnly)
	FName Reason;
};

USTRUCT(BlueprintType)
struct TDWORLDGEN_API FTDWorldLayout
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 Seed = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 GeneratorVersion = TD_WORLDGEN_VERSION;

	UPROPERTY(BlueprintReadOnly)
	FName RegionId;

	UPROPERTY(BlueprintReadOnly)
	FBox2D BoundsCm = FBox2D(ForceInit);

	UPROPERTY(BlueprintReadOnly)
	TArray<FTDWorldAnchor> Anchors;

	UPROPERTY(BlueprintReadOnly)
	TArray<FTDPoiPlacement> Pois;

	UPROPERTY(BlueprintReadOnly)
	TArray<FTDDungeonEntrancePlacement> Entrances;

	UPROPERTY(BlueprintReadOnly)
	TArray<FTDRoadPolyline> Roads;

	UPROPERTY(BlueprintReadOnly)
	TArray<FTDExclusionArea> Exclusions;

	UPROPERTY(BlueprintReadOnly)
	FTDValidationReport Validation;

	const FTDWorldAnchor* FindAnchor(ETDWorldAnchorKind Kind) const;
	uint32 ComputeHash() const;
};

struct TDWORLDGEN_API FTDWorldLayoutRegionFilter
{
	static bool ContainsLocation(const FBox2D& RegionBoundsCm, const FVector& LocationCm);
	static bool TouchesRoad(const FBox2D& RegionBoundsCm, const FTDRoadPolyline& Road);
	static void Split(const FTDWorldLayout& Layout, const FBox2D& RegionBoundsCm, FTDWorldLayout& OutInside, FTDWorldLayout& OutOutside);
};

struct TDWORLDGEN_API FTDTerrainSampler
{
	TFunction<float(const FVector2D& LocationCm)> HeightCm;
	TFunction<float(const FVector2D& LocationCm)> SlopeDeg;
	float WaterLevelCm = -175.0f;

	bool IsValid() const { return static_cast<bool>(HeightCm) && static_cast<bool>(SlopeDeg); }
	float SampleHeight(const FVector2D& LocationCm) const { return HeightCm ? HeightCm(LocationCm) : 0.0f; }
	float SampleSlope(const FVector2D& LocationCm) const { return SlopeDeg ? SlopeDeg(LocationCm) : 0.0f; }
	bool IsUnderWater(const FVector2D& LocationCm) const { return SampleHeight(LocationCm) < WaterLevelCm; }
};
