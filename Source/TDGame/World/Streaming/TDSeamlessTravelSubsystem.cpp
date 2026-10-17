#include "World/Streaming/TDSeamlessTravelSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Characters/TDCombatCharacter.h"
#include "Dungeon/TDDungeonDefinitions.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "NavigationSystem.h"
#include "TDGame.h"
#include "TDWorldGenSettings.h"
#include "World/Streaming/TDRealmSubsystem.h"
#include "World/TDRealmDefinitions.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

namespace
{
	const FName TDSeamlessTravelSourceName(TEXT("TDSeamlessTravel"));
	const FName TDSeamlessTravelDepartureHoldSourceName(TEXT("TDSeamlessTravelDepartureHold"));
	const FVector TDNavigationProjectionExtent(300.0, 300.0, 600.0);
	constexpr float TDSeamlessTravelReadinessRadiusCm = 20000.0f;
	constexpr float TDSeamlessTravelRealmSourceRadiusCm = 25000.0f;

	TAutoConsoleVariable<float> CVarTDRealmSimulatedPreloadDelaySeconds(
		TEXT("td.Realm.SimulatedPreloadDelaySeconds"),
		0.0f,
		TEXT("Test only. Seconds of world time after a seamless travel preload starts during which the destination is reported as not ready. 0 = Off."));

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

	float GetTravelFadeOutSeconds()
	{
		const UTDWorldGenSettings* Settings = UTDWorldGenSettings::Get();
		return Settings ? FMath::Max(Settings->TravelFadeOutSeconds, 0.0f) : 0.0f;
	}

	float GetTravelFadeInSeconds()
	{
		const UTDWorldGenSettings* Settings = UTDWorldGenSettings::Get();
		return Settings ? FMath::Max(Settings->TravelFadeInSeconds, 0.0f) : 0.0f;
	}
}

bool UTDSeamlessTravelSubsystem::TeleportPawnToDestination(APawn& Pawn, const FVector& TargetLocation, const FRotator& TargetRotation)
{
	if (ATDCombatCharacter* CombatCharacter = Cast<ATDCombatCharacter>(&Pawn))
	{
		CombatCharacter->TeleportPawn(TargetLocation, TargetRotation);
		return true;
	}

	if (Pawn.TeleportTo(TargetLocation, TargetRotation))
	{
		return true;
	}

	return Pawn.SetActorLocationAndRotation(TargetLocation, TargetRotation, false, nullptr, ETeleportType::TeleportPhysics);
}

UTDSeamlessTravelSubsystem* UTDSeamlessTravelSubsystem::GetSeamlessTravelSubsystem(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UTDSeamlessTravelSubsystem>() : nullptr;
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
	RequestDungeonAtlas();
}

void UTDSeamlessTravelSubsystem::Deinitialize()
{
	UnregisterStreamingProvider();
	DungeonAtlasHandle.Reset();
	Super::Deinitialize();
}

void UTDSeamlessTravelSubsystem::RequestDungeonAtlas()
{
	if (DungeonAtlasHandle.IsValid())
	{
		return;
	}

	const UTDWorldGenSettings* Settings = UTDWorldGenSettings::Get();
	if (!Settings || Settings->DefaultDungeonAtlas.IsNull())
	{
		return;
	}

	DungeonAtlasHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		Settings->DefaultDungeonAtlas.ToSoftObjectPath(), FStreamableDelegate(), FStreamableManager::AsyncLoadHighPriority);
}

const UTDDungeonAtlasDefinition* UTDSeamlessTravelSubsystem::WaitForDungeonAtlas()
{
	const UTDWorldGenSettings* Settings = UTDWorldGenSettings::Get();
	if (!Settings || Settings->DefaultDungeonAtlas.IsNull())
	{
		return nullptr;
	}

	RequestDungeonAtlas();
	if (DungeonAtlasHandle.IsValid() && !DungeonAtlasHandle->HasLoadCompleted())
	{
		UE_LOG(LogTDGame, Warning, TEXT("SeamlessTravel: dungeon atlas '%s' was requested before its preload finished; waiting for it."), *Settings->DefaultDungeonAtlas.ToSoftObjectPath().ToString());
		DungeonAtlasHandle->WaitUntilComplete();
	}
	return Settings->DefaultDungeonAtlas.Get();
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
	const bool bHasTravelSource = TravelState != ETDSeamlessTravelState::Idle;
	if (bHasTravelSource)
	{
		FWorldPartitionStreamingSource& Source = StreamingSources.AddDefaulted_GetRef();
		Source.Name = TDSeamlessTravelSourceName;
		Source.Location = bIsRealmGateTarget ? ReadinessLocation : Destination.GetLocation();
		Source.Rotation = Destination.Rotator();
		Source.TargetState = EStreamingSourceTargetState::Activated;
		Source.Priority = EStreamingSourcePriority::Highest;
		FStreamingSourceShape& Shape = Source.Shapes.AddDefaulted_GetRef();
		Shape.bUseGridLoadingRange = !bIsRealmGateTarget;
		Shape.Radius = TDSeamlessTravelRealmSourceRadiusCm;
	}

	const bool bHasDepartureHoldSource = IsDepartureHoldActive();
	if (bHasDepartureHoldSource)
	{
		FWorldPartitionStreamingSource& HoldSource = StreamingSources.AddDefaulted_GetRef();
		HoldSource.Name = TDSeamlessTravelDepartureHoldSourceName;
		HoldSource.Location = DepartureHoldLocation;
		HoldSource.TargetState = EStreamingSourceTargetState::Activated;
		HoldSource.Priority = EStreamingSourcePriority::High;
		FStreamingSourceShape& HoldShape = HoldSource.Shapes.AddDefaulted_GetRef();
		HoldShape.bUseGridLoadingRange = true;
	}

	return bHasTravelSource || bHasDepartureHoldSource;
}

bool UTDSeamlessTravelSubsystem::IsDepartureHoldActive() const
{
	return GetWorldTimeSeconds() < DepartureHoldEndSeconds;
}

void UTDSeamlessTravelSubsystem::BeginDepartureHold(const FVector& DepartureLocation)
{
	const UTDWorldGenSettings* Settings = UTDWorldGenSettings::Get();
	const float HoldSeconds = Settings ? FMath::Max(Settings->RealmDepartureHoldSeconds, 0.0f) : 0.0f;
	if (HoldSeconds <= 0.0f)
	{
		return;
	}

	DepartureHoldLocation = DepartureLocation;
	DepartureHoldEndSeconds = GetWorldTimeSeconds() + HoldSeconds;
	UE_LOG(LogTDGame, Log, TEXT("SeamlessTravel: holding departure area %s loaded for %.1fs."), *DepartureLocation.ToCompactString(), HoldSeconds);
}

bool UTDSeamlessTravelSubsystem::ResolveDestination(FName DungeonId, bool bToEntry, FTransform& OutDestination, FLinearColor& OutFadeColor)
{
	const UTDRealmDefinition* Realm = UTDRealmDefinition::FindRealmByGateId(DungeonId);
	const FTDRealmGate* Gate = Realm ? Realm->FindGate(DungeonId) : nullptr;
	if (Gate)
	{
		OutDestination = Gate->GetTravelDestination(bToEntry);
		OutFadeColor = Realm->TransitionFadeColor;
		return true;
	}

	const UTDDungeonAtlasDefinition* Atlas = WaitForDungeonAtlas();
	const FTDDungeonSlot* Slot = Atlas ? Atlas->FindSlotPtr(DungeonId) : nullptr;
	if (!Slot)
	{
		UE_LOG(LogTDGame, Warning, TEXT("SeamlessTravel: destination '%s' not found in realm definitions or dungeon atlas '%s'."), *DungeonId.ToString(), Atlas ? *Atlas->GetName() : TEXT("<DefaultDungeonAtlas not set>"));
		return false;
	}

	OutDestination = bToEntry ? Slot->EntryTransform : Slot->FieldReturnTransform;
	OutFadeColor = FLinearColor::Black;
	return true;
}

bool UTDSeamlessTravelSubsystem::IsSameTarget(FName DungeonId, bool bToEntry) const
{
	return ActiveDungeonId == DungeonId && bIsTargetEntry == bToEntry;
}

bool UTDSeamlessTravelSubsystem::IsUnrequestedPreload() const
{
	const bool bIsPreloading = TravelState == ETDSeamlessTravelState::Preloading || TravelState == ETDSeamlessTravelState::ReadyToTravel;
	return bIsPreloading && !bIsTravelRequested;
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

	return StartPreload(DungeonId, bToEntry);
}

bool UTDSeamlessTravelSubsystem::StartPreload(FName DungeonId, bool bToEntry)
{
	if (DungeonId.IsNone())
	{
		return false;
	}

	FTransform ResolvedDestination;
	FLinearColor ResolvedFadeColor = FLinearColor::Black;
	if (!ResolveDestination(DungeonId, bToEntry, ResolvedDestination, ResolvedFadeColor))
	{
		return false;
	}

	if (TravelState != ETDSeamlessTravelState::Idle)
	{
		UE_LOG(LogTDGame, Log, TEXT("SeamlessTravel: unrequested preload '%s' replaced by '%s'."), *ActiveDungeonId.ToString(), *DungeonId.ToString());
	}

	const UTDRealmDefinition* Realm = UTDRealmDefinition::FindRealmByGateId(DungeonId);
	const FTDRealmGate* Gate = Realm ? Realm->FindGate(DungeonId) : nullptr;
	ActiveDungeonId = DungeonId;
	bIsTargetEntry = bToEntry;
	bIsTravelRequested = false;
	Destination = ResolvedDestination;
	bIsRealmGateTarget = Gate != nullptr;
	ReadinessLocation = Gate ? Gate->GetPreloadLocation(bToEntry) : ResolvedDestination.GetLocation();
	TravelFadeColor = ResolvedFadeColor;
	RequestStartSeconds = FPlatformTime::Seconds();
	PreloadStartWorldSeconds = GetWorldTimeSeconds();
	NextTimeoutWarningSeconds = RequestStartSeconds + GetTimeoutSeconds();
	RegisterStreamingProvider();
	UE_LOG(LogTDGame, Log, TEXT("SeamlessTravel: preload '%s' (%s) at %s, readiness query at %s."), *DungeonId.ToString(), bToEntry ? TEXT("entry") : TEXT("field return"), *Destination.GetLocation().ToCompactString(), *ReadinessLocation.ToCompactString());
	const float SimulatedDelaySeconds = CVarTDRealmSimulatedPreloadDelaySeconds.GetValueOnGameThread();
	if (SimulatedDelaySeconds > 0.0f)
	{
		UE_LOG(LogTDGame, Log, TEXT("SeamlessTravel: simulated preload delay %.1fs for '%s' (td.Realm.SimulatedPreloadDelaySeconds)."), SimulatedDelaySeconds, *DungeonId.ToString());
	}
	SetTravelState(ETDSeamlessTravelState::Preloading);
	return true;
}

bool UTDSeamlessTravelSubsystem::RequestTravel(FName DungeonId, bool bToEntry)
{
	const bool bCanReplacePreload = IsUnrequestedPreload() && !IsSameTarget(DungeonId, bToEntry);
	const bool bShouldStartPreload = TravelState == ETDSeamlessTravelState::Idle || bCanReplacePreload;
	if (bShouldStartPreload && !StartPreload(DungeonId, bToEntry))
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

	if (!bIsTravelRequested)
	{
		bIsTravelRequested = true;
		BeginTravelFadeOut();
	}
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
	BeginTravelFadeIn();
	SetTravelState(ETDSeamlessTravelState::Idle);
}

bool UTDSeamlessTravelSubsystem::CancelPreload(FName DungeonId, bool bToEntry)
{
	if (!IsUnrequestedPreload() || !IsSameTarget(DungeonId, bToEntry))
	{
		return false;
	}

	CancelTravel();
	return true;
}

bool UTDSeamlessTravelSubsystem::IsReadyFor(FName DungeonId, bool bToEntry) const
{
	if (TravelState != ETDSeamlessTravelState::ReadyToTravel || !IsSameTarget(DungeonId, bToEntry))
	{
		return false;
	}

	return IsDestinationReady();
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

bool UTDSeamlessTravelSubsystem::IsSimulatedPreloadDelayActive() const
{
	const float DelaySeconds = CVarTDRealmSimulatedPreloadDelaySeconds.GetValueOnGameThread();
	if (DelaySeconds <= 0.0f)
	{
		return false;
	}

	return GetWorldTimeSeconds() < PreloadStartWorldSeconds + DelaySeconds;
}

bool UTDSeamlessTravelSubsystem::IsDestinationReady() const
{
	if (IsSimulatedPreloadDelayActive())
	{
		return false;
	}

	UWorld* World = GetWorld();
	const UWorldPartitionSubsystem* WorldPartition = World ? World->GetSubsystem<UWorldPartitionSubsystem>() : nullptr;
	if (WorldPartition && !WorldPartition->IsStreamingCompleted(EWorldPartitionRuntimeCellState::Activated, { MakeReadinessQuerySource() }, false))
	{
		return false;
	}

	return IsDestinationOnNavigation();
}

FWorldPartitionStreamingQuerySource UTDSeamlessTravelSubsystem::MakeReadinessQuerySource() const
{
	FWorldPartitionStreamingQuerySource QuerySource(ReadinessLocation);
	QuerySource.Radius = TDSeamlessTravelReadinessRadiusCm;
	QuerySource.bUseGridLoadingRange = false;
	if (const UDataLayerManager* DataLayerManager = UDataLayerManager::GetDataLayerManager(GetWorld()))
	{
		QuerySource.DataLayers = DataLayerManager->GetEffectiveActiveDataLayerNames().Array();
	}
	return QuerySource;
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

	if (GetWorldTimeSeconds() < TravelFadeOutEndSeconds)
	{
		return;
	}

	if (!IsDestinationReady())
	{
		UE_LOG(LogTDGame, Log, TEXT("SeamlessTravel: '%s' no longer ready, back to preloading."), *ActiveDungeonId.ToString());
		SetTravelState(ETDSeamlessTravelState::Preloading);
		return;
	}

	FTransform DepartureSeamTransform;
	FLinearColor DepartureSeamFadeColor;
	const bool bIsRealmGate = UTDRealmDefinition::FindRealmByGateId(ActiveDungeonId) != nullptr
		&& ResolveDestination(ActiveDungeonId, !bIsTargetEntry, DepartureSeamTransform, DepartureSeamFadeColor);

	SetTravelState(ETDSeamlessTravelState::Traveling);
	if (!PerformTravel())
	{
		bIsTravelRequested = false;
		BeginTravelFadeIn();
		SetTravelState(ETDSeamlessTravelState::ReadyToTravel);
		return;
	}

	if (UTDRealmSubsystem* RealmSubsystem = UTDRealmSubsystem::GetRealmSubsystem(this))
	{
		RealmSubsystem->ApplyAtmosphereForLocation(Destination.GetLocation());
	}

	if (bIsRealmGate)
	{
		BeginDepartureHold(DepartureSeamTransform.GetLocation());
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

APlayerCameraManager* UTDSeamlessTravelSubsystem::FindLocalCameraManager() const
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	return PlayerController ? PlayerController->PlayerCameraManager.Get() : nullptr;
}

double UTDSeamlessTravelSubsystem::GetWorldTimeSeconds() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

void UTDSeamlessTravelSubsystem::BeginTravelFadeOut()
{
	TravelFadeOutEndSeconds = GetWorldTimeSeconds();
	const float FadeOutSeconds = GetTravelFadeOutSeconds();
	APlayerCameraManager* CameraManager = FindLocalCameraManager();
	if (FadeOutSeconds <= 0.0f || !CameraManager)
	{
		return;
	}

	CameraManager->StartCameraFade(0.0f, 1.0f, FadeOutSeconds, TravelFadeColor, false, true);
	TravelFadeOutEndSeconds += FadeOutSeconds;
	bIsTravelFadeActive = true;
}

void UTDSeamlessTravelSubsystem::BeginTravelFadeIn()
{
	if (!bIsTravelFadeActive)
	{
		return;
	}

	bIsTravelFadeActive = false;
	APlayerCameraManager* CameraManager = FindLocalCameraManager();
	if (!CameraManager)
	{
		return;
	}

	CameraManager->StartCameraFade(CameraManager->FadeAmount, 0.0f, GetTravelFadeInSeconds(), TravelFadeColor, false, false);
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

	if (!TeleportPawnToDestination(*Pawn, TargetLocation, TargetRotation))
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
	BeginTravelFadeIn();
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
