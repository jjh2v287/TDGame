#if WITH_DEV_AUTOMATION_TESTS

#include "Core/TDGameplayMessages.h"
#include "Core/TDGameplayTags.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Foundation/Recording/TDGameplayRecorderSubsystem.h"
#include "Framework/TDGameplayRecorderBridge.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace TDGameplayRecorderBridgeTests
{
	TSharedPtr<FJsonObject> ParseJsonLine(const FString& Line)
	{
		TSharedPtr<FJsonObject> Parsed;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
		if (!FJsonSerializer::Deserialize(Reader, Parsed))
		{
			return nullptr;
		}
		return Parsed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDGameplayRecorderBridgeRecordsMessagesTest, "TDGame.Foundation.Recorder.BridgeRecordsMessages", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDGameplayRecorderBridgeRecordsMessagesTest::RunTest(const FString& Parameters)
{
	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Tests"), TEXT("RecorderBridgeTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDRecorderBridgeTestWorld"));
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->AddToRoot();
	GameInstance->InitializeStandalone(WorldName, GetTransientPackage());
	UWorld* World = GameInstance->GetWorld();
	UTDGameplayRecorderSubsystem* Recorder = GameInstance->GetSubsystem<UTDGameplayRecorderSubsystem>();
	TestNotNull(TEXT("The recorder exists on the game instance"), Recorder);
	if (!World || !Recorder)
	{
		GameInstance->Shutdown();
		GameInstance->RemoveFromRoot();
		return false;
	}

	Recorder->SetDirectoryOverride(Directory);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	World->SetBegunPlay(true);

	TestNotNull(TEXT("The bridge exists in the game world"), World->GetSubsystem<UTDGameplayRecorderBridge>());
	TestTrue(TEXT("The bridge opened a session"), Recorder->IsSessionOpen());

	AActor* Attacker = World->SpawnActor<AActor>();
	AActor* Victim = World->SpawnActor<AActor>();
	UGameplayMessageSubsystem& MessageSubsystem = UGameplayMessageSubsystem::Get(World);

	FTDDamageAppliedMessage DamageMessage;
	DamageMessage.Instigator = Attacker;
	DamageMessage.Target = Victim;
	DamageMessage.DamageAmount = 25.f;
	DamageMessage.Delivery = ETDDamageDelivery::Melee;
	MessageSubsystem.BroadcastMessage(TDGameplayTags::Event_Damage_Applied, DamageMessage);

	FTDActorDeathMessage DeathMessage;
	DeathMessage.DeadActor = Victim;
	DeathMessage.Killer = Attacker;
	MessageSubsystem.BroadcastMessage(TDGameplayTags::Event_Actor_Death, DeathMessage);

	FTDEncounterFinishedMessage FinishedMessage;
	FinishedMessage.EncounterId = TEXT("TestEncounter");
	FinishedMessage.bWasVictory = true;
	MessageSubsystem.BroadcastMessage(TDGameplayTags::Event_Encounter_Finished, FinishedMessage);

	const FString SessionFilePath = Recorder->GetSessionFilePath();
	Recorder->EndSession();

	FString FileText;
	TestTrue(TEXT("The session file exists"), FFileHelper::LoadFileToString(FileText, *SessionFilePath));
	TArray<FString> Lines;
	FileText.ParseIntoArrayLines(Lines);
	TestEqual(TEXT("Header plus three events"), Lines.Num(), 4);
	if (Lines.Num() == 4)
	{
		const TSharedPtr<FJsonObject> Header = TDGameplayRecorderBridgeTests::ParseJsonLine(Lines[0]);
		const TSharedPtr<FJsonObject> Damage = TDGameplayRecorderBridgeTests::ParseJsonLine(Lines[1]);
		const TSharedPtr<FJsonObject> Death = TDGameplayRecorderBridgeTests::ParseJsonLine(Lines[2]);
		const TSharedPtr<FJsonObject> Finished = TDGameplayRecorderBridgeTests::ParseJsonLine(Lines[3]);
		if (Header.IsValid() && Damage.IsValid() && Death.IsValid() && Finished.IsValid())
		{
			TestEqual(TEXT("The header origin is live"), Header->GetStringField(TEXT("origin")), FString(TEXT("live")));
			TestEqual(TEXT("The damage event kind"), Damage->GetStringField(TEXT("ev")), FString(TEXT("damage")));
			TestEqual(TEXT("The damage amount"), Damage->GetNumberField(TEXT("amount")), 25.0);
			TestEqual(TEXT("The damage source"), Damage->GetStringField(TEXT("src")), Attacker->GetName());
			TestEqual(TEXT("The damage target"), Damage->GetStringField(TEXT("dst")), Victim->GetName());
			TestEqual(TEXT("The damage delivery"), Damage->GetStringField(TEXT("ability")), FString(TEXT("Melee")));
			TestEqual(TEXT("The death event kind"), Death->GetStringField(TEXT("ev")), FString(TEXT("death")));
			TestEqual(TEXT("The encounter end event kind"), Finished->GetStringField(TEXT("ev")), FString(TEXT("encounter_end")));
			TestTrue(TEXT("The encounter end victory flag"), Finished->GetBoolField(TEXT("victory")));
		}
		else
		{
			AddError(TEXT("A recorded line is not valid JSON"));
		}
	}

	World->EndPlay(EEndPlayReason::Quit);
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	GameInstance->Shutdown();
	GameInstance->RemoveFromRoot();
	IFileManager::Get().DeleteDirectory(*Directory, false, true);
	return true;
}

#endif
