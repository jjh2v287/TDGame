#include "Framework/TDGameplayRecorderBridge.h"

#include "Combat/TDCombatLibrary.h"
#include "Core/TDGameplayMessages.h"
#include "Core/TDGameplayTags.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Foundation/Recording/TDGameplayRecorderSubsystem.h"
#include "GameFramework/Actor.h"

namespace TDGameplayRecorderBridge
{
	const TCHAR* const LiveOrigin = TEXT("live");

	FString DescribeActor(const AActor* Actor)
	{
		return Actor ? Actor->GetName() : FString();
	}

	void SetActorFields(const TSharedRef<FJsonObject>& Fields, const TCHAR* NameKey, const TCHAR* TeamKey, const AActor* Actor)
	{
		Fields->SetStringField(NameKey, DescribeActor(Actor));
		Fields->SetNumberField(TeamKey, Actor ? UTDCombatLibrary::GetActorTeamId(Actor) : INDEX_NONE);
	}
}

bool UTDGameplayRecorderBridge::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* OuterWorld = Cast<UWorld>(Outer);
	if (!OuterWorld)
	{
		return false;
	}
	return OuterWorld->WorldType == EWorldType::Game || OuterWorld->WorldType == EWorldType::PIE;
}

void UTDGameplayRecorderBridge::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	UTDGameplayRecorderSubsystem* Recorder = GetRecorder();
	if (!Recorder)
	{
		return;
	}
	if (!UGameplayMessageSubsystem::HasInstance(&InWorld))
	{
		return;
	}
	if (!Recorder->BeginSession(TDGameplayRecorderBridge::LiveOrigin, UWorld::RemovePIEPrefix(InWorld.GetMapName())))
	{
		return;
	}

	bHasOpenedSession = true;
	RegisterListeners(InWorld);
}

void UTDGameplayRecorderBridge::Deinitialize()
{
	UnregisterListeners();
	if (bHasOpenedSession)
	{
		if (UTDGameplayRecorderSubsystem* Recorder = GetRecorder())
		{
			Recorder->EndSession();
		}
		bHasOpenedSession = false;
	}
	Super::Deinitialize();
}

UTDGameplayRecorderSubsystem* UTDGameplayRecorderBridge::GetRecorder() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UTDGameplayRecorderSubsystem>() : nullptr;
}

void UTDGameplayRecorderBridge::RegisterListeners(UWorld& InWorld)
{
	UGameplayMessageSubsystem& MessageSubsystem = UGameplayMessageSubsystem::Get(&InWorld);
	DamageAppliedListener = MessageSubsystem.RegisterListener<FTDDamageAppliedMessage>(TDGameplayTags::Event_Damage_Applied, this, &ThisClass::HandleDamageApplied);
	DamageAvoidedListener = MessageSubsystem.RegisterListener<FTDDamageAvoidedMessage>(TDGameplayTags::Event_Damage_Avoided, this, &ThisClass::HandleDamageAvoided);
	ActorDeathListener = MessageSubsystem.RegisterListener<FTDActorDeathMessage>(TDGameplayTags::Event_Actor_Death, this, &ThisClass::HandleActorDeath);
	EncounterBannerListener = MessageSubsystem.RegisterListener<FTDEncounterBannerMessage>(TDGameplayTags::Event_Encounter_Banner, this, &ThisClass::HandleEncounterBanner);
	EncounterFinishedListener = MessageSubsystem.RegisterListener<FTDEncounterFinishedMessage>(TDGameplayTags::Event_Encounter_Finished, this, &ThisClass::HandleEncounterFinished);
	MonsterPhaseListener = MessageSubsystem.RegisterListener<FTDMonsterPhaseMessage>(TDGameplayTags::Event_Monster_PhaseEntered, this, &ThisClass::HandleMonsterPhaseEntered);
}

void UTDGameplayRecorderBridge::UnregisterListeners()
{
	DamageAppliedListener.Unregister();
	DamageAvoidedListener.Unregister();
	ActorDeathListener.Unregister();
	EncounterBannerListener.Unregister();
	EncounterFinishedListener.Unregister();
	MonsterPhaseListener.Unregister();
}

void UTDGameplayRecorderBridge::HandleDamageApplied(FGameplayTag Channel, const FTDDamageAppliedMessage& Message)
{
	UTDGameplayRecorderSubsystem* Recorder = GetRecorder();
	if (!Recorder)
	{
		return;
	}

	const TSharedRef<FJsonObject> Fields = MakeShared<FJsonObject>();
	TDGameplayRecorderBridge::SetActorFields(Fields, TEXT("src"), TEXT("src_team"), Message.Instigator);
	TDGameplayRecorderBridge::SetActorFields(Fields, TEXT("dst"), TEXT("dst_team"), Message.Target);
	Fields->SetNumberField(TEXT("amount"), Message.DamageAmount);
	Fields->SetStringField(TEXT("ability"), StaticEnum<ETDDamageDelivery>()->GetNameStringByValue(static_cast<int64>(Message.Delivery)));
	Fields->SetStringField(TEXT("element"), StaticEnum<ETDDamageElement>()->GetNameStringByValue(static_cast<int64>(Message.Element)));
	Fields->SetBoolField(TEXT("crit"), Message.bWasCritical);
	Fields->SetBoolField(TEXT("kill"), Message.bWasKilled);
	Recorder->RecordEvent(TEXT("damage"), GetWorld()->GetTimeSeconds(), Fields);
}

void UTDGameplayRecorderBridge::HandleDamageAvoided(FGameplayTag Channel, const FTDDamageAvoidedMessage& Message)
{
	UTDGameplayRecorderSubsystem* Recorder = GetRecorder();
	if (!Recorder)
	{
		return;
	}

	const TSharedRef<FJsonObject> Fields = MakeShared<FJsonObject>();
	TDGameplayRecorderBridge::SetActorFields(Fields, TEXT("src"), TEXT("src_team"), Message.Instigator);
	TDGameplayRecorderBridge::SetActorFields(Fields, TEXT("dst"), TEXT("dst_team"), Message.Target);
	Recorder->RecordEvent(TEXT("avoid"), GetWorld()->GetTimeSeconds(), Fields);
}

void UTDGameplayRecorderBridge::HandleActorDeath(FGameplayTag Channel, const FTDActorDeathMessage& Message)
{
	UTDGameplayRecorderSubsystem* Recorder = GetRecorder();
	if (!Recorder)
	{
		return;
	}

	const TSharedRef<FJsonObject> Fields = MakeShared<FJsonObject>();
	TDGameplayRecorderBridge::SetActorFields(Fields, TEXT("src"), TEXT("src_team"), Message.Killer);
	TDGameplayRecorderBridge::SetActorFields(Fields, TEXT("dst"), TEXT("dst_team"), Message.DeadActor);
	Recorder->RecordEvent(TEXT("death"), GetWorld()->GetTimeSeconds(), Fields);
}

void UTDGameplayRecorderBridge::HandleEncounterBanner(FGameplayTag Channel, const FTDEncounterBannerMessage& Message)
{
	UTDGameplayRecorderSubsystem* Recorder = GetRecorder();
	if (!Recorder)
	{
		return;
	}

	const TSharedRef<FJsonObject> Fields = MakeShared<FJsonObject>();
	Fields->SetStringField(TEXT("enc"), TDGameplayRecorderBridge::DescribeActor(Message.Encounter));
	Fields->SetStringField(TEXT("title"), Message.Title.ToString());
	Fields->SetStringField(TEXT("style"), StaticEnum<ETDEncounterBannerStyle>()->GetNameStringByValue(static_cast<int64>(Message.Style)));
	Recorder->RecordEvent(TEXT("encounter_banner"), GetWorld()->GetTimeSeconds(), Fields);
}

void UTDGameplayRecorderBridge::HandleEncounterFinished(FGameplayTag Channel, const FTDEncounterFinishedMessage& Message)
{
	UTDGameplayRecorderSubsystem* Recorder = GetRecorder();
	if (!Recorder)
	{
		return;
	}

	const TSharedRef<FJsonObject> Fields = MakeShared<FJsonObject>();
	Fields->SetStringField(TEXT("enc"), TDGameplayRecorderBridge::DescribeActor(Message.Encounter));
	Fields->SetStringField(TEXT("id"), Message.EncounterId.ToString());
	Fields->SetBoolField(TEXT("victory"), Message.bWasVictory);
	Recorder->RecordEvent(TEXT("encounter_end"), GetWorld()->GetTimeSeconds(), Fields);
}

void UTDGameplayRecorderBridge::HandleMonsterPhaseEntered(FGameplayTag Channel, const FTDMonsterPhaseMessage& Message)
{
	UTDGameplayRecorderSubsystem* Recorder = GetRecorder();
	if (!Recorder)
	{
		return;
	}

	const TSharedRef<FJsonObject> Fields = MakeShared<FJsonObject>();
	TDGameplayRecorderBridge::SetActorFields(Fields, TEXT("src"), TEXT("src_team"), Message.Monster);
	Fields->SetNumberField(TEXT("phase"), Message.PhaseIndex);
	Fields->SetStringField(TEXT("phase_id"), Message.PhaseId.ToString());
	Recorder->RecordEvent(TEXT("phase"), GetWorld()->GetTimeSeconds(), Fields);
}
