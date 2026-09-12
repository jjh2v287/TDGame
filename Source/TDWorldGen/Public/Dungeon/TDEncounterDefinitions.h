#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TDWorldGenTypes.h"
#include "TDEncounterDefinitions.generated.h"

class AActor;

USTRUCT(BlueprintType)
struct TDWORLDGEN_API FTDEncounterEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Encounter")
	TSoftClassPtr<AActor> EnemyClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Encounter", meta = (ClampMin = "0"))
	int32 MinDepth = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Encounter", meta = (ClampMin = "0"))
	int32 MaxDepth = 99;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Encounter", meta = (ClampMin = "0"))
	float Weight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Encounter", meta = (ClampMin = "0"))
	float DifficultyCost = 1.0f;

	bool MatchesDepth(int32 RoomDepth) const { return RoomDepth >= MinDepth && RoomDepth <= MaxDepth; }
};

USTRUCT(BlueprintType)
struct TDWORLDGEN_API FTDEncounterPick
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 MarkerIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly)
	int32 EntryIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly)
	TSoftClassPtr<AActor> EnemyClass;

	UPROPERTY(BlueprintReadOnly)
	float DifficultyCost = 0.0f;
};

UCLASS(BlueprintType)
class TDWORLDGEN_API UTDEncounterSet : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter")
	FName EncounterSetId = TEXT("Default");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter")
	TArray<FTDEncounterEntry> Entries;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter", meta = (ClampMin = "0"))
	float DifficultyBudgetPerRoom = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter", meta = (ClampMin = "0"))
	int32 MaxPerRoom = 4;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

struct TDWORLDGEN_API FTDEncounterResolver
{
	static TArray<FTDEncounterPick> Resolve(const UTDEncounterSet* EncounterSet, int32 RoomDepth, int32 MarkerCount, const FTDSeedContext& Seed);
	static float SumDifficulty(const TArray<FTDEncounterPick>& Picks);
};
