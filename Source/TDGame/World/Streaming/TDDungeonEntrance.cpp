#include "World/Streaming/TDDungeonEntrance.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TDGame.h"

ATDDungeonEntrance::ATDDungeonEntrance()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	PreloadTrigger = CreateDefaultSubobject<USphereComponent>(TEXT("PreloadTrigger"));
	PreloadTrigger->SetupAttachment(GetRootComponent());
	PreloadTrigger->SetSphereRadius(PreloadDistanceCm);
	PreloadTrigger->SetCollisionProfileName(TEXT("OverlapOnlyPawn"));
	PreloadTrigger->SetGenerateOverlapEvents(true);

	TravelTrigger = CreateDefaultSubobject<UBoxComponent>(TEXT("TravelTrigger"));
	TravelTrigger->SetupAttachment(GetRootComponent());
	TravelTrigger->SetBoxExtent(FVector(200.0f, 200.0f, 200.0f));
	TravelTrigger->SetCollisionProfileName(TEXT("OverlapOnlyPawn"));
	TravelTrigger->SetGenerateOverlapEvents(true);

	Tags.Add(TEXT("TDDungeonEntrance"));
}

void ATDDungeonEntrance::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	PreloadTrigger->SetSphereRadius(PreloadDistanceCm);
}

void ATDDungeonEntrance::BeginPlay()
{
	Super::BeginPlay();

	PreloadTrigger->OnComponentBeginOverlap.AddUniqueDynamic(this, &ATDDungeonEntrance::HandlePreloadTriggerBeginOverlap);
	TravelTrigger->OnComponentBeginOverlap.AddUniqueDynamic(this, &ATDDungeonEntrance::HandleTravelTriggerBeginOverlap);

	if (UTDSeamlessTravelSubsystem* Travel = FindTravelSubsystem())
	{
		Travel->OnTravelStateChanged.AddUniqueDynamic(this, &ATDDungeonEntrance::HandleTravelStateChanged);
	}
}

void ATDDungeonEntrance::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	PreloadTrigger->OnComponentBeginOverlap.RemoveDynamic(this, &ATDDungeonEntrance::HandlePreloadTriggerBeginOverlap);
	TravelTrigger->OnComponentBeginOverlap.RemoveDynamic(this, &ATDDungeonEntrance::HandleTravelTriggerBeginOverlap);
	if (UTDSeamlessTravelSubsystem* Travel = FindTravelSubsystem())
	{
		Travel->OnTravelStateChanged.RemoveDynamic(this, &ATDDungeonEntrance::HandleTravelStateChanged);
	}
	EndTransition();
	Super::EndPlay(EndPlayReason);
}

UTDSeamlessTravelSubsystem* ATDDungeonEntrance::FindTravelSubsystem() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UTDSeamlessTravelSubsystem>() : nullptr;
}

APlayerController* ATDDungeonEntrance::FindPlayerControllerOfPawn(const AActor* Actor)
{
	const APawn* Pawn = Cast<APawn>(Actor);
	if (!Pawn)
	{
		return nullptr;
	}

	return Cast<APlayerController>(Pawn->GetController());
}

void ATDDungeonEntrance::HandlePreloadTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!FindPlayerControllerOfPawn(OtherActor))
	{
		return;
	}

	UTDSeamlessTravelSubsystem* Travel = FindTravelSubsystem();
	if (!Travel || Travel->GetTravelState() != ETDSeamlessTravelState::Idle)
	{
		return;
	}

	Travel->BeginPreload(DungeonId, !bIsExit);
}

void ATDDungeonEntrance::HandleTravelTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	APlayerController* PlayerController = FindPlayerControllerOfPawn(OtherActor);
	if (!PlayerController)
	{
		return;
	}

	UTDSeamlessTravelSubsystem* Travel = FindTravelSubsystem();
	if (!Travel)
	{
		return;
	}

	const ETDSeamlessTravelState CurrentState = Travel->GetTravelState();
	const bool bCanRequest = CurrentState == ETDSeamlessTravelState::Idle || CurrentState == ETDSeamlessTravelState::Preloading || CurrentState == ETDSeamlessTravelState::ReadyToTravel;
	if (!bCanRequest || bIsTransitionActive)
	{
		return;
	}

	BeginTransition(PlayerController);
	if (!Travel->RequestTravel(DungeonId, !bIsExit))
	{
		EndTransition();
	}
}

void ATDDungeonEntrance::BeginTransition(APlayerController* PlayerController)
{
	bIsTransitionActive = true;
	RestrictedController = PlayerController;
	PlayerController->SetIgnoreMoveInput(true);
	UE_LOG(LogTDGame, Log, TEXT("DungeonEntrance '%s': transition begin (%s '%s')."), *GetName(), bIsExit ? TEXT("exit") : TEXT("entry"), *DungeonId.ToString());
	OnTransitionBegin();
}

void ATDDungeonEntrance::EndTransition()
{
	if (!bIsTransitionActive)
	{
		return;
	}

	bIsTransitionActive = false;
	if (APlayerController* PlayerController = RestrictedController.Get())
	{
		PlayerController->SetIgnoreMoveInput(false);
	}
	RestrictedController.Reset();
	UE_LOG(LogTDGame, Log, TEXT("DungeonEntrance '%s': transition end."), *GetName());
	OnTransitionEnd();
}

void ATDDungeonEntrance::HandleTravelStateChanged(ETDSeamlessTravelState OldState, ETDSeamlessTravelState NewState)
{
	if (!bIsTransitionActive || NewState != ETDSeamlessTravelState::Idle)
	{
		return;
	}

	EndTransition();
}
