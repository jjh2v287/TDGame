#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/TDGameplayMessages.h"
#include "TDArenaEncounter.generated.h"

class APawn;
class ATDMonsterCharacter;
class UArrowComponent;
class UBoxComponent;
class USpringArmComponent;
class UTDArenaEncounterDefinition;
class UTDCombatComponent;
class UTDMonsterSpeciesAsset;
struct FStreamableHandle;
struct FTDArenaSpawnEntry;
struct FTDDamageContext;

UENUM(BlueprintType)
enum class ETDArenaEncounterState : uint8
{
	Idle,
	Loading,
	WaveDelay,
	WaveActive,
	BossActive,
	Victory,
	Failed
};

UCLASS()
class TDGAME_API ATDArenaEncounter : public AActor
{
	GENERATED_BODY()

public:
	ATDArenaEncounter();

	static void ComputeSpawnLocations(const FTDArenaSpawnEntry& Entry, const FVector& ArenaCenter, float ArenaRadiusCm, const FVector& ChallengerLocation, TArray<FVector>& OutLocations);

	UFUNCTION(BlueprintCallable, Category="Arena")
	bool TryStartEncounter(APawn* Challenger);

	UFUNCTION(BlueprintCallable, Category="Arena")
	void ResetEncounter();

	UFUNCTION(BlueprintCallable, Category="Arena|Debug")
	int32 KillCurrentWave();

	UFUNCTION(BlueprintCallable, Category="Arena")
	bool SetEncounterDefinition(UTDArenaEncounterDefinition* NewDefinition);

	UFUNCTION(BlueprintCallable, Category="Arena")
	void SetGateActors(const TArray<AActor*>& NewSealGateActors, const TArray<AActor*>& NewExitGateActors);

	UFUNCTION(BlueprintPure, Category="Arena")
	ETDArenaEncounterState GetEncounterState() const { return EncounterState; }

	UFUNCTION(BlueprintPure, Category="Arena")
	int32 GetCurrentWaveIndex() const { return CurrentWaveIndex; }

	UFUNCTION(BlueprintPure, Category="Arena")
	int32 GetAliveMonsterCount() const;

	UFUNCTION(BlueprintPure, Category="Arena")
	int32 GetKilledMonsterCount() const { return KilledMonsterCount; }

	UFUNCTION(BlueprintPure, Category="Arena")
	int32 GetAttemptCount() const { return AttemptCount; }

	UFUNCTION(BlueprintPure, Category="Arena")
	bool AreSpeciesLoaded() const { return bAreSpeciesLoaded; }

	UFUNCTION(BlueprintPure, Category="Arena")
	bool IsSealed() const { return bIsSealed; }

	UFUNCTION(BlueprintPure, Category="Arena")
	bool IsExitOpen() const { return bIsExitOpen; }

	UFUNCTION(BlueprintPure, Category="Arena")
	float GetArenaRadiusCm() const { return ArenaRadiusCm; }

	UFUNCTION(BlueprintPure, Category="Arena")
	ATDMonsterCharacter* GetBossHealthBarTarget() const { return BossHealthBarTarget.Get(); }

	UFUNCTION(BlueprintPure, Category="Arena")
	FTransform GetCheckpointTransform() const;

	UFUNCTION(BlueprintPure, Category="Arena")
	bool IsLocationInEntryZone(const FVector& WorldLocation) const;

	UFUNCTION(BlueprintPure, Category="Arena")
	FVector GetEntryZoneCenter() const;

	UTDArenaEncounterDefinition* GetEncounterDefinition() const { return Definition; }
	const TArray<FVector>& GetLastWaveSpawnLocations() const { return LastWaveSpawnLocations; }
	void GetAliveMonsters(TArray<ATDMonsterCharacter*>& OutMonsters) const;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USceneComponent> ArenaRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UBoxComponent> EntryZone;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UArrowComponent> CheckpointArrow;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena")
	TObjectPtr<UTDArenaEncounterDefinition> Definition;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena", meta=(ClampMin="200", ToolTip="Planar radius of the fighting floor around this actor. Spawns are clamped inside it and leftovers inside it are cleared on reset."))
	float ArenaRadiusCm = 1400.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena", meta=(ClampMin="0"))
	float SpawnEdgeMarginCm = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena", meta=(ToolTip="Closed while the fight runs, opened again on victory or after a failed attempt."))
	TArray<TObjectPtr<AActor>> SealGateActors;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena", meta=(ToolTip="Closed until the encounter is won."))
	TArray<TObjectPtr<AActor>> ExitGateActors;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena", meta=(ToolTip="Keeps the entry zone just inside the entrance and the checkpoint outside it whenever the arena radius changes. Turn off to place them by hand."))
	bool bShouldPlaceEntryFromRadius = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena")
	bool bShouldSetRespawnCheckpoint = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena")
	bool bShouldRecordVictoryInWorldState = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena|Camera", meta=(ClampMin="0.5", ClampMax="3", ToolTip="Camera boom length multiplier while the fight runs so large bosses stay on screen."))
	float CameraDistanceScale = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena|Camera", meta=(ClampMin="0.1"))
	float CameraBlendSeconds = 1.5f;

private:
	void RequestSpeciesLoad();
	void HandleSpeciesLoaded();
	void CollectLoadedSpecies(bool bShouldWarnMissing);
	APawn* FindChallengerInEntryZone() const;
	void BeginEncounter();
	void BeginWaveDelay(int32 WaveIndex);
	void SpawnCurrentWave();
	ATDMonsterCharacter* SpawnMonster(UTDMonsterSpeciesAsset& Species, const FVector& Location, const FRotator& Rotation);
	void ShowBossHealthBar(ATDMonsterCharacter& Monster, const FTDArenaSpawnEntry& Entry);
	void HideBossHealthBar();
	void HandleMonsterDeath(const FTDDamageContext& Context, TWeakObjectPtr<ATDMonsterCharacter> Monster);
	void HandleChallengerDeath(const FTDDamageContext& Context);
	void PruneVanishedMonsters();
	void AdvanceAfterWaveCleared();
	void FinishWithVictory();
	void FailEncounter();
	void KillHostilesInArena();
	void DestroyHostilesInArena();
	void CollectHostilesInArena(TArray<ATDMonsterCharacter*>& OutMonsters) const;
	void SetGatesClosed(const TArray<TObjectPtr<AActor>>& Gates, bool bShouldClose);
	void SetSealed(bool bShouldSeal);
	void SetExitOpen(bool bShouldOpen);
	void BindChallenger(APawn* Challenger);
	void UnbindChallenger();
	void BeginCameraPullBack(APawn* Challenger);
	void EndCameraPullBack();
	void UpdateCameraDistance(float DeltaSeconds);
	void ApplyRespawnCheckpoint() const;
	void ReleaseRespawnCheckpoint() const;
	bool WasVictoryRecorded() const;
	void RecordVictory() const;
	void BroadcastBanner(const FText& Title, const FText& Subtitle, ETDEncounterBannerStyle Style, float DisplaySeconds);
	void BroadcastFinished(bool bWasVictory);
	FText MakeWaveTitle(int32 WaveIndex) const;
	FName GetEncounterId() const;
	bool IsFightRunning() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTDMonsterSpeciesAsset>> LoadedSpecies;

	TArray<TWeakObjectPtr<ATDMonsterCharacter>> AliveMonsters;
	TArray<FVector> LastWaveSpawnLocations;
	TWeakObjectPtr<ATDMonsterCharacter> BossHealthBarTarget;
	TWeakObjectPtr<APawn> ChallengerPawn;
	TWeakObjectPtr<UTDCombatComponent> ChallengerCombat;
	FDelegateHandle ChallengerDeathHandle;
	TWeakObjectPtr<USpringArmComponent> ChallengerCameraBoom;
	float BaseCameraArmLength = 0.f;
	float DesiredCameraArmLength = 0.f;
	TSharedPtr<FStreamableHandle> SpeciesHandle;
	ETDArenaEncounterState EncounterState = ETDArenaEncounterState::Idle;
	float StateSecondsRemaining = 0.f;
	int32 CurrentWaveIndex = INDEX_NONE;
	int32 KilledMonsterCount = 0;
	int32 AttemptCount = 0;
	bool bAreSpeciesLoaded = false;
	bool bIsSealed = false;
	bool bIsExitOpen = false;
	bool bIsBossHealthBarShown = false;
	bool bWasBossDefeated = false;
};
