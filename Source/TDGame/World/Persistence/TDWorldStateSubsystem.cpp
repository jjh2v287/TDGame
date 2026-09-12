#include "World/Persistence/TDWorldStateSubsystem.h"

#include "Kismet/GameplayStatics.h"
#include "TDGame.h"
#include "TDWorldGenTypes.h"
#include "World/Persistence/TDPersistentStateComponent.h"

UTDWorldStateSubsystem::UTDWorldStateSubsystem()
	: GeneratorVersion(TD_WORLDGEN_VERSION)
{
}

bool UTDWorldStateSubsystem::FindActorRecord(const FGuid& StableId, FTDActorStateRecord& OutRecord) const
{
	const FTDActorStateRecord* Found = ActorRecords.Find(StableId);
	if (!Found)
	{
		return false;
	}

	OutRecord = *Found;
	return true;
}

void UTDWorldStateSubsystem::StoreActorRecord(const FTDActorStateRecord& Record)
{
	if (!Record.StableId.IsValid())
	{
		UE_LOG(LogTDGame, Warning, TEXT("StoreActorRecord: record with invalid StableId ignored."));
		return;
	}

	ActorRecords.Add(Record.StableId, Record);
}

bool UTDWorldStateSubsystem::FindDungeonState(FName DungeonId, FTDDungeonState& OutState) const
{
	const FTDDungeonState* Found = DungeonStates.Find(DungeonId);
	if (!Found)
	{
		return false;
	}

	OutState = *Found;
	return true;
}

void UTDWorldStateSubsystem::StoreDungeonState(FName DungeonId, const FTDDungeonState& State)
{
	if (DungeonId.IsNone())
	{
		return;
	}

	DungeonStates.Add(DungeonId, State);
}

FTDDungeonState& UTDWorldStateSubsystem::FindOrAddDungeonState(FName DungeonId)
{
	return DungeonStates.FindOrAdd(DungeonId);
}

void UTDWorldStateSubsystem::WriteToSaveGame(UTDSaveGame& SaveGame) const
{
	SaveGame.FormatVersion = UTDSaveGame::CurrentFormatVersion;
	SaveGame.MasterSeed = MasterSeed;
	SaveGame.GeneratorVersion = GeneratorVersion;

	SaveGame.ActorRecords.Reset(ActorRecords.Num());
	for (const TPair<FGuid, FTDActorStateRecord>& Pair : ActorRecords)
	{
		SaveGame.ActorRecords.Add(Pair.Value);
	}

	SaveGame.DungeonStates.Reset(DungeonStates.Num());
	for (const TPair<FName, FTDDungeonState>& Pair : DungeonStates)
	{
		FTDDungeonStateEntry& Entry = SaveGame.DungeonStates.AddDefaulted_GetRef();
		Entry.DungeonId = Pair.Key;
		Entry.State = Pair.Value;
	}
}

bool UTDWorldStateSubsystem::MigrateSaveGame(UTDSaveGame& SaveGame)
{
	using FMigrationStep = bool (*)(UTDSaveGame&);
	struct FMigrationEntry
	{
		int32 FromVersion;
		FMigrationStep Step;
	};
	static const FMigrationEntry MigrationTable[] =
	{
		{ 1, [](UTDSaveGame&) { return true; } }
	};

	if (SaveGame.FormatVersion < 1 || SaveGame.FormatVersion > UTDSaveGame::CurrentFormatVersion)
	{
		UE_LOG(LogTDGame, Error, TEXT("MigrateSaveGame: unsupported format version %d (current %d)."), SaveGame.FormatVersion, UTDSaveGame::CurrentFormatVersion);
		return false;
	}

	for (const FMigrationEntry& Entry : MigrationTable)
	{
		if (Entry.FromVersion < SaveGame.FormatVersion)
		{
			continue;
		}
		if (!Entry.Step(SaveGame))
		{
			return false;
		}
	}

	SaveGame.FormatVersion = UTDSaveGame::CurrentFormatVersion;
	return true;
}

bool UTDWorldStateSubsystem::ReadFromSaveGame(UTDSaveGame& SaveGame)
{
	if (!MigrateSaveGame(SaveGame))
	{
		return false;
	}

	MasterSeed = SaveGame.MasterSeed;
	GeneratorVersion = SaveGame.GeneratorVersion;

	ActorRecords.Reset();
	for (const FTDActorStateRecord& Record : SaveGame.ActorRecords)
	{
		StoreActorRecord(Record);
	}

	DungeonStates.Reset();
	for (const FTDDungeonStateEntry& Entry : SaveGame.DungeonStates)
	{
		StoreDungeonState(Entry.DungeonId, Entry.State);
	}

	return true;
}

bool UTDWorldStateSubsystem::SaveToSlot(const FString& SlotName)
{
	if (SlotName.IsEmpty())
	{
		UE_LOG(LogTDGame, Warning, TEXT("SaveToSlot: empty slot name."));
		return false;
	}

	CaptureLiveActorStates();

	UTDSaveGame* SaveGame = NewObject<UTDSaveGame>(GetTransientPackage());
	WriteToSaveGame(*SaveGame);

	const bool bSaved = UGameplayStatics::SaveGameToSlot(SaveGame, SlotName, 0);
	UE_LOG(LogTDGame, Log, TEXT("SaveToSlot '%s': %s (%d actor records, %d dungeon states)."), *SlotName, bSaved ? TEXT("ok") : TEXT("FAILED"), ActorRecords.Num(), DungeonStates.Num());
	return bSaved;
}

bool UTDWorldStateSubsystem::LoadFromSlot(const FString& SlotName)
{
	if (SlotName.IsEmpty())
	{
		UE_LOG(LogTDGame, Warning, TEXT("LoadFromSlot: empty slot name."));
		return false;
	}

	UTDSaveGame* SaveGame = Cast<UTDSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
	if (!SaveGame)
	{
		UE_LOG(LogTDGame, Warning, TEXT("LoadFromSlot '%s': no save game or wrong class."), *SlotName);
		return false;
	}

	if (!ReadFromSaveGame(*SaveGame))
	{
		return false;
	}

	ApplyStateToLiveActors();
	UE_LOG(LogTDGame, Log, TEXT("LoadFromSlot '%s': ok (%d actor records, %d dungeon states, seed %d, generator %d)."), *SlotName, ActorRecords.Num(), DungeonStates.Num(), MasterSeed, GeneratorVersion);
	return true;
}

void UTDWorldStateSubsystem::ClearAllState()
{
	ActorRecords.Reset();
	DungeonStates.Reset();
}

void UTDWorldStateSubsystem::RegisterLiveComponent(UTDPersistentStateComponent* Component)
{
	if (!Component)
	{
		return;
	}

	LiveComponents.AddUnique(Component);
}

void UTDWorldStateSubsystem::UnregisterLiveComponent(UTDPersistentStateComponent* Component)
{
	LiveComponents.RemoveAll([Component](const TWeakObjectPtr<UTDPersistentStateComponent>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Component;
	});
}

void UTDWorldStateSubsystem::CaptureLiveActorStates()
{
	for (const TWeakObjectPtr<UTDPersistentStateComponent>& Entry : LiveComponents)
	{
		UTDPersistentStateComponent* Component = Entry.Get();
		if (!Component)
		{
			continue;
		}

		Component->CaptureOwnerState();
	}
}

void UTDWorldStateSubsystem::ApplyStateToLiveActors()
{
	for (const TWeakObjectPtr<UTDPersistentStateComponent>& Entry : LiveComponents)
	{
		UTDPersistentStateComponent* Component = Entry.Get();
		if (!Component)
		{
			continue;
		}

		Component->ApplyStoredStateToOwner();
	}
}
