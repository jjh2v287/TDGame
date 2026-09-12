#include "World/Persistence/TDPersistentTestChest.h"

#include "Components/StaticMeshComponent.h"
#include "TDGame.h"
#include "World/Persistence/TDPersistentStateComponent.h"

const FName ATDPersistentTestChest::OpenedStateKey(TEXT("Opened"));

ATDPersistentTestChest::ATDPersistentTestChest()
{
	PrimaryActorTick.bCanEverTick = false;

	ChestMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChestMesh"));
	SetRootComponent(ChestMesh);

	PersistentState = CreateDefaultSubobject<UTDPersistentStateComponent>(TEXT("PersistentState"));
}

void ATDPersistentTestChest::Open()
{
	if (bIsOpened)
	{
		return;
	}

	bIsOpened = true;
	UE_LOG(LogTDGame, Log, TEXT("TDPersistentTestChest '%s' opened (StableId %s)."), *GetName(), *PersistentState->StableId.ToString());
}

void ATDPersistentTestChest::WriteState(FTDActorStateRecord& OutRecord) const
{
	OutRecord.BoolValues.Add(OpenedStateKey, bIsOpened);
}

void ATDPersistentTestChest::ReadState(const FTDActorStateRecord& Record)
{
	bIsOpened = Record.GetBool(OpenedStateKey, bIsOpened);
}
