// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "TDGameGameMode.generated.h"

struct FTDActorDeathMessage;
class UPrimitiveComponent;

/**
 *  Simple Game Mode for a top-down perspective game
 *  Sets the default gameplay framework classes
 *  Check the Blueprint derived class for the set values
 */
UCLASS(abstract)
class TDGAME_API ATDGameGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:

	/** Constructor */
	ATDGameGameMode();

	float GetPlayerRespawnDelaySeconds() const { return PlayerRespawnDelaySeconds; }

	void SetRespawnCheckpoint(const FTransform& CheckpointTransform);
	void ClearRespawnCheckpoint();
	bool HasRespawnCheckpoint() const { return bHasRespawnCheckpoint; }
	const FTransform& GetRespawnCheckpoint() const { return RespawnCheckpoint; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Respawn", meta=(ClampMin="0"))
	float PlayerRespawnDelaySeconds = 4.f;

private:
	void HandleActorDeath(FGameplayTag Channel, const FTDActorDeathMessage& Message);
	void RespawnPlayer(TWeakObjectPtr<AController> Controller);
	void PrestreamRespawnView(AController* PlayerController);
	FTransform FindClearPawnSpawnTransform(AController* NewPlayer, const UClass& PawnClass, const FTransform& RequestedTransform);
	bool TryFindClearPawnSpot(const UPrimitiveComponent& PawnShape, const FTransform& BaseTransform, const AActor* IgnoredActor, FTransform& OutTransform) const;

	FGameplayMessageListenerHandle DeathListenerHandle;
	FTransform RespawnCheckpoint = FTransform::Identity;
	bool bHasRespawnCheckpoint = false;
};



