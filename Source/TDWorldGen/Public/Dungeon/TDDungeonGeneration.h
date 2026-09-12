#pragma once

#include "CoreMinimal.h"
#include "Dungeon/TDDungeonTypes.h"
#include "Dungeon/TDDungeonDefinitions.h"

struct TDWORLDGEN_API FTDDungeonFlowGenerator
{
	static bool Generate(const UTDDungeonFlowTemplate& Template, ETDDungeonSize Size, const FTDSeedContext& Seed, FTDDungeonFlowGraph& OutGraph, FString& OutError);
	static bool IsKeyBeforeLockOrderValid(const FTDDungeonFlowGraph& Graph);
};

struct TDWORLDGEN_API FTDDungeonLayoutSolver
{
	struct FSettings
	{
		int32 MaxCandidateTries = 16;
		int32 MaxBacktrackDepth = 4;
		int32 MaxRestarts = 200;
		int32 MaxCorridorCells = 3;
	};

	static bool Solve(const FTDDungeonFlowGraph& Graph, const UTDDungeonTheme& Theme, const FTDSeedContext& Seed, const FSettings& Settings, FTDDungeonLayout& OutLayout, FString& OutError);
};

struct TDWORLDGEN_API FTDDungeonValidator
{
	struct FSettings
	{
		FIntPoint RoomCountRange = FIntPoint(10, 15);
		float MaxDeadEndRatio = 0.4f;
	};

	static FTDValidationReport Validate(const FTDDungeonLayout& Layout, const FSettings& Settings);
};

struct TDWORLDGEN_API FTDCandidateSelector
{
	struct FCandidate
	{
		int32 Seed = 0;
		float Score = 0.0f;
		bool bPassed = false;
		FString Summary;
	};

	static float ScoreDungeon(const FTDDungeonLayout& Layout);
	static void Rank(TArray<FCandidate>& InOutCandidates);
};

struct TDWORLDGEN_API FTDDungeonGenerator
{
	static bool GenerateAndValidate(const UTDDungeonTheme& Theme, const UTDDungeonFlowTemplate& Template, ETDDungeonSize Size, int32 Seed, FTDDungeonLayout& OutLayout, FString& OutError);
	static FString ToJson(const FTDDungeonLayout& Layout, const FVector& WorldOffsetCm);
	static bool WriteJsonFile(const FTDDungeonLayout& Layout, const FVector& WorldOffsetCm, const FString& FilePath);
};
