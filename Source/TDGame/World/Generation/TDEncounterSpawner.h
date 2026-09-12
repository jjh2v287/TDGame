#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Dungeon/TDEncounterDefinitions.h"
#include "TDEncounterSpawner.generated.h"

class USceneComponent;

UCLASS()
class TDGAME_API ATDEncounterSpawner : public AActor
{
	GENERATED_BODY()

public:
	ATDEncounterSpawner();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Encounter")
	TObjectPtr<UTDEncounterSet> EncounterSet;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Encounter")
	FName RoomId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Encounter", meta = (ClampMin = "0"))
	int32 RoomDepth = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Encounter")
	int32 Seed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Encounter")
	bool bShouldSpawnOnBeginPlay = true;

	UPROPERTY(BlueprintReadOnly, Category = "TD|Encounter")
	TArray<TObjectPtr<AActor>> SpawnedEnemies;

	UFUNCTION(BlueprintCallable, Category = "TD|Encounter")
	int32 SpawnEncounter();

	UFUNCTION(BlueprintCallable, Category = "TD|Encounter")
	void DespawnEncounter();

	UFUNCTION(BlueprintCallable, Category = "TD|Encounter")
	void CollectSpawnMarkerTransforms(TArray<FTransform>& OutTransforms) const;

	static FName GetSpawnMarkerTagName();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	FTDSeedContext MakeSeedContext() const;
};
