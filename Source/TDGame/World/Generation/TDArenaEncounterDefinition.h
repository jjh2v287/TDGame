#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TDArenaEncounterDefinition.generated.h"

class UTDMonsterSpeciesAsset;

UENUM(BlueprintType)
enum class ETDArenaSpawnAnchor : uint8
{
	AroundChallenger,
	ArenaCenter
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDArenaSpawnEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena")
	TSoftObjectPtr<UTDMonsterSpeciesAsset> Species;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena", meta=(ClampMin="1", ClampMax="32"))
	int32 Count = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena")
	ETDArenaSpawnAnchor Anchor = ETDArenaSpawnAnchor::AroundChallenger;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena", meta=(ClampMin="0", ToolTip="Distance from the anchor. Around the challenger the monsters stand on an arc facing the arena centre."))
	float DistanceCm = 800.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena", meta=(ClampMin="0", ClampMax="360"))
	float SpreadDegrees = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena|Boss")
	bool bShowsBossHealthBar = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena|Boss")
	FText BossDisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena|Boss", meta=(ToolTip="Health ratios drawn as notches on the boss bar when the monster does not report its own phase thresholds."))
	TArray<float> BossPhaseHealthMarks;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDArenaWave
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena", meta=(ClampMin="0"))
	float StartDelaySeconds = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena")
	TArray<FTDArenaSpawnEntry> Entries;
};

UCLASS(BlueprintType)
class TDGAME_API UTDArenaEncounterDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena")
	FName EncounterId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena")
	TArray<FTDArenaWave> Waves;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena|Result")
	FText VictoryRewardText;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena|Result", meta=(ClampMin="0", ToolTip="Seconds between the challenger's death and the arena reset. Keep it below the game mode respawn delay so the arena is clean when the player returns."))
	float FailureResetSeconds = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena|Result", meta=(ClampMin="0.5"))
	float BannerSeconds = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena|Result", meta=(ClampMin="0.5"))
	float VictoryBannerSeconds = 6.f;

	void GatherSpeciesPaths(TArray<FSoftObjectPath>& OutPaths) const;
	bool IsBossWave(int32 WaveIndex) const;
	int32 CountMonstersInWave(int32 WaveIndex) const;
};
