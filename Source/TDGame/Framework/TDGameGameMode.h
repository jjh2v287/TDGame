// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "TDGameGameMode.generated.h"

struct FTDActorDeathMessage;

/**
 *  Simple Game Mode for a top-down perspective game
 *  Sets the default gameplay framework classes
 *  Check the Blueprint derived class for the set values
 */
UCLASS(abstract)
class ATDGameGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:

	/** Constructor */
	ATDGameGameMode();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Respawn", meta=(ClampMin="0"))
	float PlayerRespawnDelaySeconds = 3.f;

private:
	void HandleActorDeath(FGameplayTag Channel, const FTDActorDeathMessage& Message);
	void RespawnPlayer(TWeakObjectPtr<AController> Controller);

	FGameplayMessageListenerHandle DeathListenerHandle;
};



