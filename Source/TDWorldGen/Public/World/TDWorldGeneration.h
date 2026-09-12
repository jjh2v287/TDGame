#pragma once

#include "CoreMinimal.h"
#include "World/TDWorldTypes.h"
#include "World/TDWorldDefinitions.h"
#include "Dungeon/TDDungeonDefinitions.h"

struct TDWORLDGEN_API FTDWorldGraphGenerator
{
	struct FSettings
	{
		int32 PoissonAttemptsPerPoint = 30;
		int32 MaxPlacementRetries = 200;
		float DefaultArenaRadiusCm = 2600.0f;
	};

	static bool Generate(const UTDRegionDefinition& Region, const TArray<FTDWorldAnchor>& Anchors, const FBox2D& BoundsCm, const FTDSeedContext& Seed, const FTDTerrainSampler* Terrain, const FSettings& Settings, FTDWorldLayout& OutLayout, FString& OutError);
	static bool Generate(const UTDRegionDefinition& Region, const TArray<FTDWorldAnchor>& Anchors, const FTDLockedLayoutElements& Locked, const FBox2D& BoundsCm, const FTDSeedContext& Seed, const FTDTerrainSampler* Terrain, const FSettings& Settings, FTDWorldLayout& OutLayout, FString& OutError);
};

struct TDWORLDGEN_API FTDRoadGenerator
{
	struct FSettings
	{
		float GridCm = 400.0f;
		float SlopeCostWeight = 6.0f;
		float WaterCost = 40.0f;
		float SecondaryRoadRatio = 0.3f;
		float RoadWidthCm = 600.0f;
		float RoadClearanceCm = 800.0f;
	};

	static bool Generate(FTDWorldLayout& InOutLayout, const FTDTerrainSampler* Terrain, const FSettings& Settings, const FTDSeedContext& Seed, FString& OutError);
};

struct TDWORLDGEN_API FTDWorldValidator
{
	struct FSettings
	{
		float MinEntranceSpacingCm = 15000.0f;
		FIntPoint PoiCountRange = FIntPoint(6, 12);
		float MinPoiSpacingCm = 4000.0f;
		float RoadReachCm = 4000.0f;
		float MaxSlopeDeg = 25.0f;
		float ContentFarCm = 12000.0f;
		float EmptyMaxCm = 18000.0f;
		float FieldSlotClearanceCm = 20000.0f;
	};

	static FTDValidationReport Validate(const FTDWorldLayout& Layout, const UTDDungeonAtlasDefinition* Atlas, const FTDTerrainSampler* Terrain, const FSettings& Settings);
};

struct TDWORLDGEN_API FTDWorldGenerator
{
	static bool GenerateAndValidate(const UTDRegionDefinition& Region, const TArray<FTDWorldAnchor>& Anchors, const FBox2D& BoundsCm, int32 Seed, const FTDTerrainSampler* Terrain, const UTDDungeonAtlasDefinition* Atlas, FTDWorldLayout& OutLayout, FString& OutError);
	static bool GenerateAndValidate(const UTDRegionDefinition& Region, const TArray<FTDWorldAnchor>& Anchors, const FTDLockedLayoutElements& Locked, const FBox2D& BoundsCm, int32 Seed, const FTDTerrainSampler* Terrain, const UTDDungeonAtlasDefinition* Atlas, FTDWorldLayout& OutLayout, FString& OutError);
	static FString ToJson(const FTDWorldLayout& Layout);
	static bool WriteJsonFile(const FTDWorldLayout& Layout, const FString& FilePath);
};
