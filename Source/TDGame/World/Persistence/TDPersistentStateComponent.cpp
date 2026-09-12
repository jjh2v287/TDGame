#include "World/Persistence/TDPersistentStateComponent.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "TDGame.h"
#include "World/Persistence/TDPersistentActor.h"
#include "World/Persistence/TDWorldStateSubsystem.h"
#include "WorldPartition/ActorInstanceGuids.h"

UTDPersistentStateComponent::UTDPersistentStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UTDPersistentStateComponent::OnRegister()
{
	Super::OnRegister();
	ResolveStableId();
}

void UTDPersistentStateComponent::ResolveStableId()
{
	AActor* Owner = GetOwner();
	if (!Owner || Owner->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		return;
	}

	UWorld* World = Owner->GetWorld();
	const bool bIsGameWorld = World && World->IsGameWorld();
	if (bIsGameWorld)
	{
		const FGuid InstanceGuid = FActorInstanceGuid::GetActorInstanceGuid(*Owner);
		if (InstanceGuid.IsValid())
		{
			StableId = InstanceGuid;
			return;
		}

		if (!StableId.IsValid())
		{
			UE_LOG(LogTDGame, Warning, TEXT("TDPersistentStateComponent: '%s' has no valid actor instance guid and no saved StableId; state will not persist."), *Owner->GetName());
		}
		return;
	}

#if WITH_EDITOR
	if (!StableId.IsValid())
	{
		StableId = Owner->GetActorGuid();
	}
#endif
}

UTDWorldStateSubsystem* UTDPersistentStateComponent::FindWorldStateSubsystem() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	const UGameInstance* GameInstance = World->GetGameInstance();
	if (!GameInstance)
	{
		return nullptr;
	}

	return GameInstance->GetSubsystem<UTDWorldStateSubsystem>();
}

void UTDPersistentStateComponent::BeginPlay()
{
	Super::BeginPlay();

	UTDWorldStateSubsystem* WorldState = FindWorldStateSubsystem();
	if (!WorldState)
	{
		return;
	}

	WorldState->RegisterLiveComponent(this);
	ApplyStoredStateToOwner();
}

void UTDPersistentStateComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UTDWorldStateSubsystem* WorldState = FindWorldStateSubsystem();
	if (WorldState)
	{
		const bool bShouldCapture = EndPlayReason == EEndPlayReason::RemovedFromWorld || EndPlayReason == EEndPlayReason::Destroyed;
		if (bShouldCapture)
		{
			CaptureOwnerState();
		}
		WorldState->UnregisterLiveComponent(this);
	}

	Super::EndPlay(EndPlayReason);
}

bool UTDPersistentStateComponent::CaptureOwnerState()
{
	if (!StableId.IsValid())
	{
		return false;
	}

	const ITDPersistentActor* PersistentOwner = Cast<ITDPersistentActor>(GetOwner());
	UTDWorldStateSubsystem* WorldState = FindWorldStateSubsystem();
	if (!PersistentOwner || !WorldState)
	{
		return false;
	}

	FTDActorStateRecord Record;
	Record.StableId = StableId;
	PersistentOwner->WriteState(Record);
	WorldState->StoreActorRecord(Record);
	return true;
}

bool UTDPersistentStateComponent::ApplyStoredStateToOwner()
{
	if (!StableId.IsValid())
	{
		return false;
	}

	ITDPersistentActor* PersistentOwner = Cast<ITDPersistentActor>(GetOwner());
	const UTDWorldStateSubsystem* WorldState = FindWorldStateSubsystem();
	if (!PersistentOwner || !WorldState)
	{
		return false;
	}

	FTDActorStateRecord Record;
	if (!WorldState->FindActorRecord(StableId, Record))
	{
		return false;
	}

	PersistentOwner->ReadState(Record);
	return true;
}
