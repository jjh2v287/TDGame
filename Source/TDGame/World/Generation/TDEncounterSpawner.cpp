#include "World/Generation/TDEncounterSpawner.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDEncounterSpawner, Log, All);

namespace
{
	const FName SpawnMarkerTag(TEXT("TDSpawnMarker"));

	struct FTDSpawnMarkerEntry
	{
		FString SortKey;
		FTransform Transform;
	};
}

ATDEncounterSpawner::ATDEncounterSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Tags.Add(TEXT("TDEncounter"));
}

FName ATDEncounterSpawner::GetSpawnMarkerTagName()
{
	return SpawnMarkerTag;
}

void ATDEncounterSpawner::BeginPlay()
{
	Super::BeginPlay();
	if (bShouldSpawnOnBeginPlay)
	{
		SpawnEncounter();
	}
}

void ATDEncounterSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DespawnEncounter();
	Super::EndPlay(EndPlayReason);
}

FTDSeedContext ATDEncounterSpawner::MakeSeedContext() const
{
	const FString Domain = RoomId.IsNone() ? FString(TEXT("Room")) : RoomId.ToString();
	return FTDSeedContext(FTDSeedContext(Seed).DeriveSeed(Domain));
}

void ATDEncounterSpawner::CollectSpawnMarkerTransforms(TArray<FTransform>& OutTransforms) const
{
	OutTransforms.Reset();
	TArray<FTDSpawnMarkerEntry> Markers;

	TArray<USceneComponent*> SceneComponents;
	GetComponents<USceneComponent>(SceneComponents);
	for (const USceneComponent* Component : SceneComponents)
	{
		if (!Component->ComponentHasTag(SpawnMarkerTag))
		{
			continue;
		}
		Markers.Add({ FString::Printf(TEXT("C_%s"), *Component->GetName()), Component->GetComponentTransform() });
	}

	TArray<AActor*> AttachedActors;
	GetAttachedActors(AttachedActors);
	for (const AActor* Attached : AttachedActors)
	{
		if (!Attached->ActorHasTag(SpawnMarkerTag))
		{
			continue;
		}
		Markers.Add({ FString::Printf(TEXT("A_%s"), *Attached->GetName()), Attached->GetActorTransform() });
	}

	Markers.Sort([](const FTDSpawnMarkerEntry& A, const FTDSpawnMarkerEntry& B) { return A.SortKey < B.SortKey; });
	for (const FTDSpawnMarkerEntry& Marker : Markers)
	{
		OutTransforms.Add(Marker.Transform);
	}
}

int32 ATDEncounterSpawner::SpawnEncounter()
{
	UWorld* World = GetWorld();
	if (!World || !EncounterSet)
	{
		return 0;
	}
	DespawnEncounter();

	TArray<FTransform> MarkerTransforms;
	CollectSpawnMarkerTransforms(MarkerTransforms);
	if (MarkerTransforms.Num() == 0)
	{
		UE_LOG(LogTDEncounterSpawner, Warning, TEXT("%s: no components or attached actors tagged %s"), *GetName(), *SpawnMarkerTag.ToString());
		return 0;
	}

	const TArray<FTDEncounterPick> Picks = FTDEncounterResolver::Resolve(EncounterSet, RoomDepth, MarkerTransforms.Num(), MakeSeedContext());
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	for (const FTDEncounterPick& Pick : Picks)
	{
		UClass* EnemyClass = Pick.EnemyClass.LoadSynchronous();
		if (!EnemyClass)
		{
			UE_LOG(LogTDEncounterSpawner, Warning, TEXT("%s: enemy class could not be loaded: %s"), *GetName(), *Pick.EnemyClass.ToString());
			continue;
		}
		AActor* Enemy = World->SpawnActor<AActor>(EnemyClass, MarkerTransforms[Pick.MarkerIndex], Params);
		if (!Enemy)
		{
			continue;
		}
		SpawnedEnemies.Add(Enemy);
	}
	UE_LOG(LogTDEncounterSpawner, Log, TEXT("%s: room %s depth %d seed %d markers %d picks %d spawned %d difficulty %.1f"),
		*GetName(), *RoomId.ToString(), RoomDepth, Seed, MarkerTransforms.Num(), Picks.Num(), SpawnedEnemies.Num(), FTDEncounterResolver::SumDifficulty(Picks));
	return SpawnedEnemies.Num();
}

void ATDEncounterSpawner::DespawnEncounter()
{
	for (AActor* Enemy : SpawnedEnemies)
	{
		if (IsValid(Enemy))
		{
			Enemy->Destroy();
		}
	}
	SpawnedEnemies.Reset();
}
