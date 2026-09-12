#include "World/Streaming/TDSeamlessTravelSubsystem.h"

#include "Dungeon/TDDungeonDefinitions.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "NavigationSystem.h"
#include "TDGame.h"
#include "TDWorldGenSettings.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

namespace
{
	const FName TDSeamlessTravelSourceName(TEXT("TDSeamlessTravel"));
	const FVector TDNavigationProjectionExtent(300.0, 300.0, 600.0);

	const TCHAR* TravelStateToString(ETDSeamlessTravelState State)
	{
		switch (State)
		{
		case ETDSeamlessTravelState::Idle: return TEXT("Idle");
		case ETDSeamlessTravelState::Preloading: return TEXT("Preloading");
		case ETDSeamlessTravelState::ReadyToTravel: return TEXT("ReadyToTravel");
		case ETDSeamlessTravelState::Traveling: return TEXT("Traveling");
		case ETDSeamlessTravelState::Settling: return TEXT("Settling");
		}
		return TEXT("Unknown");
	}
}

bool UTDSeamlessTravelSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UTDSeamlessTravelSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UWorldPartitionSubsystem>();
	RegisterStreamingProvider();
}

void UTDSeamlessTravelSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	RegisterStreamingProvider();
}

void UTDSeamlessTravelSubsystem::Deinitialize()
{
	UnregisterStreamingProvider();
	Super::Deinitialize();
}

void UTDSeamlessTravelSubsystem::RegisterStreamingProvider()
{
	UWorld* World = GetWorld();
	UWorldPartitionSubsystem* WorldPartition = World ? World->GetSubsystem<UWorldPartitionSubsystem>() : nullptr;
	if (!WorldPartition)
	{
		return;
	}

	if (!WorldPartition->IsStreamingSourceProviderRegistered(this))
	{
		WorldPartition->RegisterStreamingSourceProvider(this);
	}
	if (!StreamingStateUpdatedHandle.IsValid())
	{
		StreamingStateUpdatedHandle = WorldPartition->OnStreamingStateUpdated().AddUObject(this, &UTDSeamlessTravelSubsystem::HandleStreamingStateUpdated);
	}
	bIsProviderRegistered = true;
}

void UTDSeamlessTravelSubsystem::UnregisterStreamingProvider()
{
	if (!bIsProviderRegistered)
	{
		return;
	}

	bIsProviderRegistered = false;
	UWorld* World = GetWorld();
	UWorldPartitionSubsystem* WorldPartition = World ? World->GetSubsystem<UWorldPartitionSubsystem>() : nullptr;
	if (!WorldPartition)
	{
		return;
	}

	WorldPartition->UnregisterStreamingSourceProvider(this);
	if (StreamingStateUpdatedHandle.IsValid())
	{
		WorldPartition->OnStreamingStateUpdated().Remove(StreamingStateUpdatedHandle);
		StreamingStateUpdatedHandle.Reset();
	}
}

bool UTDSeamlessTravelSubsystem::GetStreamingSources(TArray<FWorldPartitionStreamingSource>& StreamingSources) const
{
	if (TravelState == ETDSeamlessTravelState::Idle)
	{
		return false;
	}

	FWorldPartitionStreamingSource& Source = StreamingSources.AddDefaulted_GetRef();
	Source.Name = TDSeamlessTravelSourceName;
	Source.Location = Destination.GetLocation();
	Source.Rotation = Destination.Rotator();
	Source.TargetState = EStreamingSourceTargetState::Activated;
	Source.Priority = EStreamingSourcePriority::Highest;
	FStreamingSourceShape& Shape = Source.Shapes.AddDefaulted_GetRef();
	Shape.bUseGridLoadingRange = true;
	return true;
}

bool UTDSeamlessTravelSubsystem::ResolveDestination(FName DungeonId, bool bToEntry, FTransform& OutDestination) const
{
	const UTDWorldGenSettings* Settings = UTDWorldGenSettings::Get();
	if (!Settings)
	{
		return false;
	}

	const UTDDungeonAtlasDefinition* Atlas = Settings->DefaultDungeonAtlas.LoadSynchronous();
	if (!Atlas)
	{
		UE_LOG(LogTDGame, Warning, TEXT("SeamlessTravel: DefaultDungeonAtlas is not set in TD World Generation settings."));
		return false;
	}

	const FTDDungeonSlot* Slot = Atlas->FindSlotPtr(DungeonId);
	if (!Slot)
	{
		UE_LOG(LogTDGame, Warning, TEXT("SeamlessTravel: dungeon '%s' not found in atlas '%s'."), *DungeonId.ToString(), *Atlas->GetName());
		return false;
	}

	OutDestination = bToEntry ? Slot->EntryTransform : Slot->FieldReturnTransform;
	return true;
}

bool UTDSeamlessTravelSubsystem::IsSameTarget(FName DungeonId, bool bToEntry) const
{
	return ActiveDungeonId == DungeonId && bIsTargetEntry == bToEntry;
}

bool UTDSeamlessTravelSubsystem::BeginPreload(FName DungeonId, bool bToEntry)
{
	if (DungeonId.IsNone())
	{
		return false;
	}

	if (TravelState != ETDSeamlessTravelState::Idle)
	{
		return IsSameTarget(DungeonId, bToEntry);
	}

	FTransform ResolvedDestination;
	if (!ResolveDestination(DungeonId, bToEntry, ResolvedDestination))
	{
		return false;
	}

	ActiveDungeonId = DungeonId;
	bIsTargetEntry = bToEntry;
	bIsTravelRequested = false;
	Destination = ResolvedDestination;
	RequestStartSeconds = FPlatformTime::Seconds();
	NextTimeoutWarningSeconds = RequestStartSeconds + GetTimeoutSeconds();
	RegisterStreamingProvider();
	UE_LOG(LogTDGame, Log, TEXT("SeamlessTravel: preload '%s' (%s) at %s."), *DungeonId.ToString(), bToEntry ? TEXT("entry") : TEXT("field return"), *Destination.GetLocation().ToCompactString());
	SetTravelState(ETDSeamlessTravelState::Preloading);
	return true;
}

bool UTDSeamlessTravelSubsystem::RequestTravel(FName DungeonId, bool bToEntry)
{
	if (TravelState == ETDSeamlessTravelState::Idle && !BeginPreload(DungeonId, bToEntry))
	{
		return false;
	}

	if (!IsSameTarget(DungeonId, bToEntry))
	{
		UE_LOG(LogTDGame, Warning, TEXT("SeamlessTravel: travel to '%s' refused, '%s' is in state %s."), *DungeonId.ToString(), *ActiveDungeonId.ToString(), TravelStateToString(TravelState));
		return false;
	}

	if (TravelState == ETDSeamlessTravelState::Traveling || TravelState == ETDSeamlessTravelState::Settling)
	{
		return true;
	}

	bIsTravelRequested = true;
	if (TravelState == ETDSeamlessTravelState::ReadyToTravel)
	{
		TickReadyToTravel();
	}
	return true;
}

void UTDSeamlessTravelSubsystem::CancelTravel()
{
	if (TravelState != ETDSeamlessTravelState::Preloading && TravelState != ETDSeamlessTravelState::ReadyToTravel)
	{
		return;
	}

	UE_LOG(LogTDGame, Log, TEXT("SeamlessTravel: cancelled '%s' after %.2fs."), *ActiveDungeonId.ToString(), GetElapsedSeconds());
	bIsTravelRequested = false;
	ActiveDungeonId = NAME_None;
	SetTravelState(ETDSeamlessTravelState::Idle);
}

bool UTDSeamlessTravelSubsystem::IsTickable() const
{
	return TravelState == ETDSeamlessTravelState::Preloading || TravelState == ETDSeamlessTravelState::ReadyToTravel;
}

TStatId UTDSeamlessTravelSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTDSeamlessTravelSubsystem, STATGROUP_Tickables);
}

void UTDSeamlessTravelSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	switch (TravelState)
	{
	case ETDSeamlessTravelState::Preloading:
		TickPreloading();
		break;
	case ETDSeamlessTravelState::ReadyToTravel:
		TickReadyToTravel();
		break;
	default:
		break;
	}
}

bool UTDSeamlessTravelSubsystem::IsDestinationOnNavigation() const
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavigationSystem)
	{
		return false;
	}

	FNavLocation ProjectedLocation;
	return NavigationSystem->ProjectPointToNavigation(Destination.GetLocation(), ProjectedLocation, TDNavigationProjectionExtent);
}

bool UTDSeamlessTravelSubsystem::IsDestinationReady() const
{
	UWorld* World = GetWorld();
	const UWorldPartitionSubsystem* WorldPartition = World ? World->GetSubsystem<UWorldPartitionSubsystem>() : nullptr;
	if (WorldPartition && !WorldPartition->IsStreamingCompleted(this))
	{
		return false;
	}

	return IsDestinationOnNavigation();
}

void UTDSeamlessTravelSubsystem::TickPreloading()
{
	if (IsDestinationReady())
	{
		UE_LOG(LogTDGame, Log, TEXT("SeamlessTravel: '%s' ready after %.2fs."), *ActiveDungeonId.ToString(), GetElapsedSeconds());
		SetTravelState(ETDSeamlessTravelState::ReadyToTravel);
		return;
	}

	const double NowSeconds = FPlatformTime::Seconds();
	if (NowSeconds < NextTimeoutWarningSeconds)
	{
		return;
	}

	NextTimeoutWarningSeconds = NowSeconds + GetTimeoutSeconds();
	UE_LOG(LogTDGame, Warning, TEXT("SeamlessTravel: '%s' still not ready after %.1fs (timeout %.1fs); continuing to wait."), *ActiveDungeonId.ToString(), GetElapsedSeconds(), GetTimeoutSeconds());
}

void UTDSeamlessTravelSubsystem::TickReadyToTravel()
{
	if (!bIsTravelRequested)
	{
		return;
	}

	if (!IsDestinationReady())
	{
		UE_LOG(LogTDGame, Log, TEXT("SeamlessTravel: '%s' no longer ready, back to preloading."), *ActiveDungeonId.ToString());
		SetTravelState(ETDSeamlessTravelState::Preloading);
		return;
	}

	SetTravelState(ETDSeamlessTravelState::Traveling);
	if (!PerformTravel())
	{
		bIsTravelRequested = false;
		SetTravelState(ETDSeamlessTravelState::ReadyToTravel);
		return;
	}

	LastTraveledDungeonId = ActiveDungeonId;
	bIsTravelRequested = false;
	UE_LOG(LogTDGame, Log, TEXT("SeamlessTravel: '%s' traveled after %.2fs."), *ActiveDungeonId.ToString(), GetElapsedSeconds());
	SetTravelState(ETDSeamlessTravelState::Settling);
}

APawn* UTDSeamlessTravelSubsystem::FindLocalPlayerPawn() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	APlayerController* PlayerController = World->GetFirstPlayerController();
	return PlayerController ? PlayerController->GetPawn() : nullptr;
}

bool UTDSeamlessTravelSubsystem::PerformTravel()
{
	APawn* Pawn = FindLocalPlayerPawn();
	if (!Pawn)
	{
		UE_LOG(LogTDGame, Warning, TEXT("SeamlessTravel: no local player pawn to move."));
		return false;
	}

	const FVector TargetLocation = Destination.GetLocation();
	const FRotator TargetRotation = Destination.Rotator();

	AController* Controller = Pawn->GetController();
	if (Controller)
	{
		Controller->StopMovement();
	}

	const bool bTeleported = Pawn->TeleportTo(TargetLocation, TargetRotation);
	if (!bTeleported && !Pawn->SetActorLocationAndRotation(TargetLocation, TargetRotation, false, nullptr, ETeleportType::TeleportPhysics))
	{
		UE_LOG(LogTDGame, Warning, TEXT("SeamlessTravel: failed to move '%s' to %s."), *Pawn->GetName(), *TargetLocation.ToCompactString());
		return false;
	}

	if (Controller)
	{
		Controller->SetControlRotation(TargetRotation);
	}
	return true;
}

void UTDSeamlessTravelSubsystem::HandleStreamingStateUpdated()
{
	if (TravelState != ETDSeamlessTravelState::Settling)
	{
		return;
	}

	UE_LOG(LogTDGame, Log, TEXT("SeamlessTravel: '%s' settled, temporary source released after %.2fs."), *ActiveDungeonId.ToString(), GetElapsedSeconds());
	ActiveDungeonId = NAME_None;
	SetTravelState(ETDSeamlessTravelState::Idle);
}

void UTDSeamlessTravelSubsystem::SetTravelState(ETDSeamlessTravelState NewState)
{
	if (TravelState == NewState)
	{
		return;
	}

	const ETDSeamlessTravelState OldState = TravelState;
	TravelState = NewState;
	UE_LOG(LogTDGame, Log, TEXT("SeamlessTravel: %s -> %s (%.2fs)."), TravelStateToString(OldState), TravelStateToString(NewState), GetElapsedSeconds());
	OnTravelStateChanged.Broadcast(OldState, NewState);
}

double UTDSeamlessTravelSubsystem::GetElapsedSeconds() const
{
	return FPlatformTime::Seconds() - RequestStartSeconds;
}

float UTDSeamlessTravelSubsystem::GetTimeoutSeconds() const
{
	const UTDWorldGenSettings* Settings = UTDWorldGenSettings::Get();
	return Settings ? Settings->SeamlessTravelTimeoutSeconds : 20.0f;
}
