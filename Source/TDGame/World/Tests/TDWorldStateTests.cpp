#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "World/Persistence/TDPersistentActor.h"
#include "World/Persistence/TDWorldStateSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDWorldStateActorRecordRoundTripTest, "TDGame.WorldState.ActorRecordRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDWorldStateActorRecordRoundTripTest::RunTest(const FString& Parameters)
{
	UGameInstance* OwnerGameInstance = NewObject<UGameInstance>(GetTransientPackage());
	UTDWorldStateSubsystem* SourceState = NewObject<UTDWorldStateSubsystem>(OwnerGameInstance);
	SourceState->MasterSeed = 12345;

	FTDActorStateRecord ChestRecord;
	ChestRecord.StableId = FGuid::NewGuid();
	ChestRecord.BoolValues.Add(TEXT("Opened"), true);
	ChestRecord.IntValues.Add(TEXT("LootRolls"), 3);
	ChestRecord.FloatValues.Add(TEXT("Durability"), 0.75f);
	SourceState->StoreActorRecord(ChestRecord);

	FTDActorStateRecord DoorRecord;
	DoorRecord.StableId = FGuid::NewGuid();
	DoorRecord.BoolValues.Add(TEXT("Unlocked"), false);
	SourceState->StoreActorRecord(DoorRecord);

	FTDDungeonState CryptState;
	CryptState.bCompleted = true;
	CryptState.bBossKilled = false;
	CryptState.OpenedChests.Add(ChestRecord.StableId);
	CryptState.ActivatedEvents.Add(FGuid::NewGuid());
	SourceState->StoreDungeonState(TEXT("Crypt01"), CryptState);

	UTDSaveGame* SourceSave = NewObject<UTDSaveGame>(GetTransientPackage());
	SourceState->WriteToSaveGame(*SourceSave);
	TestEqual(TEXT("save format version"), SourceSave->FormatVersion, UTDSaveGame::CurrentFormatVersion);
	TestEqual(TEXT("save generator version"), SourceSave->GeneratorVersion, SourceState->GeneratorVersion);
	TestEqual(TEXT("save actor record count"), SourceSave->ActorRecords.Num(), 2);

	TArray<uint8> Bytes;
	{
		FMemoryWriter MemoryWriter(Bytes, true);
		FObjectAndNameAsStringProxyArchive Writer(MemoryWriter, false);
		Writer.ArIsSaveGame = true;
		SourceSave->Serialize(Writer);
	}
	TestTrue(TEXT("serialized bytes not empty"), Bytes.Num() > 0);

	UTDSaveGame* LoadedSave = NewObject<UTDSaveGame>(GetTransientPackage());
	{
		FMemoryReader MemoryReader(Bytes, true);
		FObjectAndNameAsStringProxyArchive Reader(MemoryReader, true);
		Reader.ArIsSaveGame = true;
		LoadedSave->Serialize(Reader);
	}

	UTDWorldStateSubsystem* LoadedState = NewObject<UTDWorldStateSubsystem>(OwnerGameInstance);
	TestTrue(TEXT("read from save game"), LoadedState->ReadFromSaveGame(*LoadedSave));
	TestEqual(TEXT("master seed"), LoadedState->MasterSeed, 12345);
	TestEqual(TEXT("generator version"), LoadedState->GeneratorVersion, SourceState->GeneratorVersion);
	TestEqual(TEXT("actor record count"), LoadedState->GetActorRecordCount(), 2);
	TestEqual(TEXT("dungeon state count"), LoadedState->GetDungeonStateCount(), 1);

	FTDActorStateRecord LoadedChest;
	TestTrue(TEXT("chest record found"), LoadedState->FindActorRecord(ChestRecord.StableId, LoadedChest));
	TestTrue(TEXT("chest stable id"), LoadedChest.StableId == ChestRecord.StableId);
	TestTrue(TEXT("chest opened"), LoadedChest.GetBool(TEXT("Opened")));
	TestEqual(TEXT("chest loot rolls"), LoadedChest.GetInt(TEXT("LootRolls")), 3);
	TestEqual(TEXT("chest durability"), LoadedChest.GetFloat(TEXT("Durability")), 0.75f);

	FTDActorStateRecord LoadedDoor;
	TestTrue(TEXT("door record found"), LoadedState->FindActorRecord(DoorRecord.StableId, LoadedDoor));
	TestFalse(TEXT("door unlocked"), LoadedDoor.GetBool(TEXT("Unlocked"), true));

	FTDDungeonState LoadedCrypt;
	TestTrue(TEXT("dungeon state found"), LoadedState->FindDungeonState(TEXT("Crypt01"), LoadedCrypt));
	TestTrue(TEXT("dungeon completed"), LoadedCrypt.bCompleted);
	TestFalse(TEXT("dungeon boss killed"), LoadedCrypt.bBossKilled);
	TestTrue(TEXT("opened chest set contains chest"), LoadedCrypt.OpenedChests.Contains(ChestRecord.StableId));
	TestEqual(TEXT("activated event count"), LoadedCrypt.ActivatedEvents.Num(), 1);

	FTDActorStateRecord MissingRecord;
	TestFalse(TEXT("unknown record not found"), LoadedState->FindActorRecord(FGuid::NewGuid(), MissingRecord));

	return true;
}

#endif
