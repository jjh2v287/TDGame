#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "World/Persistence/TDPersistentActor.h"
#include "TDWorldStateSubsystem.generated.h"

class UTDPersistentStateComponent;

USTRUCT(BlueprintType)
struct FTDDungeonState
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite)
	bool bCompleted = false;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite)
	bool bBossKilled = false;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite)
	TSet<FGuid> OpenedChests;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite)
	TSet<FGuid> ActivatedEvents;
};

USTRUCT()
struct FTDDungeonStateEntry
{
	GENERATED_BODY()

	UPROPERTY(SaveGame)
	FName DungeonId;

	UPROPERTY(SaveGame)
	FTDDungeonState State;
};

UCLASS()
class UTDSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	static constexpr int32 CurrentFormatVersion = 1;

	UPROPERTY(SaveGame)
	int32 FormatVersion = CurrentFormatVersion;

	UPROPERTY(SaveGame)
	int32 MasterSeed = 0;

	UPROPERTY(SaveGame)
	int32 GeneratorVersion = 0;

	UPROPERTY(SaveGame)
	TArray<FTDActorStateRecord> ActorRecords;

	UPROPERTY(SaveGame)
	TArray<FTDDungeonStateEntry> DungeonStates;
};

UCLASS()
class TDGAME_API UTDWorldStateSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = "TD|WorldState")
	int32 MasterSeed = 0;

	UPROPERTY(BlueprintReadOnly, Category = "TD|WorldState")
	int32 GeneratorVersion = 0;

	UTDWorldStateSubsystem();

	UFUNCTION(BlueprintCallable, Category = "TD|WorldState")
	bool FindActorRecord(const FGuid& StableId, FTDActorStateRecord& OutRecord) const;

	UFUNCTION(BlueprintCallable, Category = "TD|WorldState")
	void StoreActorRecord(const FTDActorStateRecord& Record);

	UFUNCTION(BlueprintCallable, Category = "TD|WorldState")
	bool FindDungeonState(FName DungeonId, FTDDungeonState& OutState) const;

	UFUNCTION(BlueprintCallable, Category = "TD|WorldState")
	void StoreDungeonState(FName DungeonId, const FTDDungeonState& State);

	FTDDungeonState& FindOrAddDungeonState(FName DungeonId);

	UFUNCTION(BlueprintCallable, Category = "TD|WorldState")
	bool SaveToSlot(const FString& SlotName);

	UFUNCTION(BlueprintCallable, Category = "TD|WorldState")
	bool LoadFromSlot(const FString& SlotName);

	UFUNCTION(BlueprintCallable, Category = "TD|WorldState")
	void ClearAllState();

	void WriteToSaveGame(UTDSaveGame& SaveGame) const;
	bool ReadFromSaveGame(UTDSaveGame& SaveGame);

	void RegisterLiveComponent(UTDPersistentStateComponent* Component);
	void UnregisterLiveComponent(UTDPersistentStateComponent* Component);
	void CaptureLiveActorStates();
	void ApplyStateToLiveActors();

	int32 GetActorRecordCount() const { return ActorRecords.Num(); }
	int32 GetDungeonStateCount() const { return DungeonStates.Num(); }

private:
	static bool MigrateSaveGame(UTDSaveGame& SaveGame);

	UPROPERTY()
	TMap<FGuid, FTDActorStateRecord> ActorRecords;

	UPROPERTY()
	TMap<FName, FTDDungeonState> DungeonStates;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UTDPersistentStateComponent>> LiveComponents;
};
