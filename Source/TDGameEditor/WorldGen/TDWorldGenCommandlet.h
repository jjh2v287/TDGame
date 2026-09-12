#pragma once

#include "Commandlets/Commandlet.h"
#include "Dungeon/TDDungeonTypes.h"
#include "TDWorldGenCommandlet.generated.h"

class UTDDungeonFlowTemplate;
class UTDDungeonTheme;

struct FTDWorldGenSeedResult
{
	int32 Seed = 0;
	bool bGenerated = false;
	bool bPassed = false;
	float Score = 0.0f;
	int32 RoomCount = 0;
	int32 DoorCount = 0;
	int32 LockedDoorCount = 0;
	int32 Restarts = 0;
	int32 ErrorCount = 0;
	int32 WarningCount = 0;
	FString Error;
};

UCLASS()
class UTDWorldGenCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UTDWorldGenCommandlet();

	virtual int32 Main(const FString& Params) override;

private:
	static UTDDungeonTheme* ResolveTheme(const FString& ThemeArgument);
	static UTDDungeonFlowTemplate* ResolveFlowTemplate(const FString& FlowArgument, FString& OutFlowName);
	static bool ParseSize(const FString& SizeArgument, ETDDungeonSize& OutSize);
	static bool ParseSeedRange(const FString& SeedsArgument, TArray<int32>& OutSeeds);
	static FString BuildReportMarkdown(const FString& ThemeName, const FString& FlowName, const FString& SizeName, const TArray<FTDWorldGenSeedResult>& Results);
};
