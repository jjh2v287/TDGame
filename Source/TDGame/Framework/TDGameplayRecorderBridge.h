#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "TDGameplayRecorderBridge.generated.h"

class UTDGameplayRecorderSubsystem;
struct FTDActorDeathMessage;
struct FTDDamageAppliedMessage;
struct FTDDamageAvoidedMessage;
struct FTDEncounterBannerMessage;
struct FTDEncounterFinishedMessage;
struct FTDMonsterPhaseMessage;

UCLASS()
class TDGAME_API UTDGameplayRecorderBridge : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

private:
	UTDGameplayRecorderSubsystem* GetRecorder() const;
	void RegisterListeners(UWorld& InWorld);
	void UnregisterListeners();

	void HandleDamageApplied(FGameplayTag Channel, const FTDDamageAppliedMessage& Message);
	void HandleDamageAvoided(FGameplayTag Channel, const FTDDamageAvoidedMessage& Message);
	void HandleActorDeath(FGameplayTag Channel, const FTDActorDeathMessage& Message);
	void HandleEncounterBanner(FGameplayTag Channel, const FTDEncounterBannerMessage& Message);
	void HandleEncounterFinished(FGameplayTag Channel, const FTDEncounterFinishedMessage& Message);
	void HandleMonsterPhaseEntered(FGameplayTag Channel, const FTDMonsterPhaseMessage& Message);

	FGameplayMessageListenerHandle DamageAppliedListener;
	FGameplayMessageListenerHandle DamageAvoidedListener;
	FGameplayMessageListenerHandle ActorDeathListener;
	FGameplayMessageListenerHandle EncounterBannerListener;
	FGameplayMessageListenerHandle EncounterFinishedListener;
	FGameplayMessageListenerHandle MonsterPhaseListener;
	bool bHasOpenedSession = false;
};
