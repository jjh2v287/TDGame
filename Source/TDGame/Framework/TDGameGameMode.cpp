// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/TDGameGameMode.h"
#include "Components/PrimitiveComponent.h"
#include "Core/TDGameplayMessages.h"
#include "Core/TDGameplayTags.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "TimerManager.h"
#include "World/Streaming/TDTexturePrestreamSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDGameMode, Log, All);

namespace TDGameGameModeInternal
{
	constexpr float PawnSpawnLiftStepsCm[] = { 0.f, 25.f, 75.f, 150.f };
	constexpr float RespawnPrestreamTailSeconds = 2.f;
}

ATDGameGameMode::ATDGameGameMode()
{
	// stub
}

void ATDGameGameMode::SetRespawnCheckpoint(const FTransform& CheckpointTransform)
{
	RespawnCheckpoint = CheckpointTransform;
	bHasRespawnCheckpoint = true;
}

void ATDGameGameMode::ClearRespawnCheckpoint()
{
	RespawnCheckpoint = FTransform::Identity;
	bHasRespawnCheckpoint = false;
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
	PrestreamRespawnView(PlayerController);
}

void ATDGameGameMode::PrestreamRespawnView(AController* PlayerController)
{
	const UWorld* World = GetWorld();
	const UTDTexturePrestreamSubsystem* Prestream = World ? World->GetSubsystem<UTDTexturePrestreamSubsystem>() : nullptr;
	if (!Prestream)
	{
		return;
	}
	if (bHasRespawnCheckpoint)
	{
		Prestream->PrestreamAtPawnLocation(RespawnCheckpoint.GetLocation(), FMath::Max(PlayerRespawnDelaySeconds, 0.01f) + TDGameGameModeInternal::RespawnPrestreamTailSeconds);
		return;
	}
	const AActor* PlayerStart = FindPlayerStart(PlayerController);
	if (!PlayerStart)
	{
		return;
	}
	Prestream->PrestreamAtPawnLocation(PlayerStart->GetActorLocation(), FMath::Max(PlayerRespawnDelaySeconds, 0.01f) + TDGameGameModeInternal::RespawnPrestreamTailSeconds);
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
	if (bHasRespawnCheckpoint)
	{
		RestartPlayerAtTransform(PlayerController, RespawnCheckpoint);
		return;
	}
	RestartPlayer(PlayerController);
}

APawn* ATDGameGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform)
{
	UWorld* World = GetWorld();
	UClass* PawnClass = GetDefaultPawnClassForController(NewPlayer);
	if (!World || !PawnClass)
	{
		UE_LOG(LogTDGameMode, Warning, TEXT("%s: no world or pawn class to spawn a pawn for %s."), *GetName(), *GetNameSafe(NewPlayer));
		return nullptr;
	}
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Instigator = GetInstigator();
	SpawnParameters.ObjectFlags |= RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	APawn* SpawnedPawn = World->SpawnActor<APawn>(PawnClass, FindClearPawnSpawnTransform(NewPlayer, *PawnClass, SpawnTransform), SpawnParameters);
	UE_CLOG(!SpawnedPawn, LogTDGameMode, Warning, TEXT("%s: could not spawn %s for %s."), *GetName(), *PawnClass->GetName(), *GetNameSafe(NewPlayer));
	return SpawnedPawn;
}

FTransform ATDGameGameMode::FindClearPawnSpawnTransform(AController* NewPlayer, const UClass& PawnClass, const FTransform& RequestedTransform)
{
	const APawn* PawnDefaults = Cast<APawn>(PawnClass.GetDefaultObject());
	const UPrimitiveComponent* PawnShape = PawnDefaults ? Cast<UPrimitiveComponent>(PawnDefaults->GetRootComponent()) : nullptr;
	if (!PawnShape || !PawnShape->IsQueryCollisionEnabled())
	{
		return RequestedTransform;
	}
	FTransform ClearTransform;
	if (TryFindClearPawnSpot(*PawnShape, RequestedTransform, nullptr, ClearTransform))
	{
		return ClearTransform;
	}
	AActor* StartSpot = FindPlayerStart(NewPlayer);
	const bool bHasPlayerStart = StartSpot && !StartSpot->IsA<AWorldSettings>();
	const FTransform StartTransform = bHasPlayerStart ? FTransform(FRotator(0.f, StartSpot->GetActorRotation().Yaw, 0.f), StartSpot->GetActorLocation()) : RequestedTransform;
	if (bHasPlayerStart && TryFindClearPawnSpot(*PawnShape, StartTransform, StartSpot, ClearTransform))
	{
		UE_LOG(LogTDGameMode, Warning, TEXT("%s: pawn spawn spot %s is blocked; using player start %s."), *GetName(), *RequestedTransform.GetLocation().ToCompactString(), *StartSpot->GetName());
		return ClearTransform;
	}
	UE_LOG(LogTDGameMode, Warning, TEXT("%s: no clear pawn spawn spot near %s or at a player start; spawning there anyway."), *GetName(), *RequestedTransform.GetLocation().ToCompactString());
	return RequestedTransform;
}

bool ATDGameGameMode::TryFindClearPawnSpot(const UPrimitiveComponent& PawnShape, const FTransform& BaseTransform, const AActor* IgnoredActor, FTransform& OutTransform) const
{
	using namespace TDGameGameModeInternal;

	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TDPawnSpawnClearance), false, IgnoredActor);
	FCollisionResponseParams ResponseParams;
	PawnShape.InitSweepCollisionParams(QueryParams, ResponseParams);
	const FCollisionShape CollisionShape = PawnShape.GetCollisionShape();
	for (const float LiftCm : PawnSpawnLiftStepsCm)
	{
		const FVector Location = BaseTransform.GetLocation() + FVector(0.f, 0.f, LiftCm);
		if (World->OverlapBlockingTestByChannel(Location, BaseTransform.GetRotation(), PawnShape.GetCollisionObjectType(), CollisionShape, QueryParams, ResponseParams))
		{
			continue;
		}
		UE_CLOG(LiftCm > 0.f, LogTDGameMode, Log, TEXT("%s: pawn spawn spot %s is blocked; lifted %.0f cm."), *GetName(), *BaseTransform.GetLocation().ToCompactString(), LiftCm);
		OutTransform = FTransform(BaseTransform.GetRotation(), Location, BaseTransform.GetScale3D());
		return true;
	}
	return false;
}
