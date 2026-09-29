// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/TDGameGameMode.h"
#include "Core/TDGameplayMessages.h"
#include "Core/TDGameplayTags.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

ATDGameGameMode::ATDGameGameMode()
{
	// stub
}

void ATDGameGameMode::BeginPlay()
{
	Super::BeginPlay();
	DeathListenerHandle = UGameplayMessageSubsystem::Get(this).RegisterListener<FTDActorDeathMessage>(TDGameplayTags::Event_Actor_Death, this, &ThisClass::HandleActorDeath);
}

void ATDGameGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DeathListenerHandle.Unregister();
	Super::EndPlay(EndPlayReason);
}

void ATDGameGameMode::HandleActorDeath(FGameplayTag Channel, const FTDActorDeathMessage& Message)
{
	const APawn* DeadPawn = Cast<APawn>(Message.DeadActor);
	APlayerController* PlayerController = DeadPawn ? Cast<APlayerController>(DeadPawn->GetController()) : nullptr;
	if (!PlayerController)
	{
		return;
	}
	FTimerHandle RespawnTimer;
	GetWorldTimerManager().SetTimer(RespawnTimer, FTimerDelegate::CreateUObject(this, &ThisClass::RespawnPlayer, TWeakObjectPtr<AController>(PlayerController)), FMath::Max(PlayerRespawnDelaySeconds, 0.01f), false);
}

void ATDGameGameMode::RespawnPlayer(const TWeakObjectPtr<AController> Controller)
{
	AController* PlayerController = Controller.Get();
	if (!PlayerController)
	{
		return;
	}
	if (APawn* OldPawn = PlayerController->GetPawn())
	{
		PlayerController->UnPossess();
		OldPawn->Destroy();
	}
	RestartPlayer(PlayerController);
}
