#include "World/Generation/TDArenaEncounter.h"

#include "Characters/TDMonsterCharacter.h"
#include "Combat/Damage/TDDamageTypes.h"
#include "Combat/TDCombatComponent.h"
#include "Combat/TDCombatLibrary.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Core/TDGameplayTags.h"
#include "Engine/AssetManager.h"
#include "Engine/GameInstance.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/TDGameGameMode.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "MonsterAI/TDMonsterSpeciesAsset.h"
#include "World/Generation/TDArenaEncounterDefinition.h"
#include "World/Persistence/TDWorldStateSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDArenaEncounter, Log, All);

namespace TDArenaEncounterInternal
{
	const FVector DefaultEntryZoneExtent(250.f, 400.f, 200.f);
	constexpr float DefaultEntryZoneInsetCm = 350.f;
	constexpr float DefaultCheckpointOutsetCm = 600.f;
	constexpr float CheckpointHeightCm = 100.f;
	constexpr float SpawnTraceAboveCm = 250.f;
	constexpr float SpawnTraceBelowCm = 2000.f;
	constexpr float SpawnGroundClearanceCm = 3.f;
	constexpr float ArenaCleanupMarginCm = 300.f;
	constexpr float LeftoverLethalDamage = 10000000.f;

	FVector MakePlanarDirection(const float YawDegrees)
	{
		const float YawRadians = FMath::DegreesToRadians(YawDegrees);
		return FVector(FMath::Cos(YawRadians), FMath::Sin(YawRadians), 0.f);
	}

	float ComputePlanarYaw(const FVector& Direction, const float FallbackYaw)
	{
		const FVector PlanarDirection = Direction.GetSafeNormal2D();
		return PlanarDirection.IsNearlyZero() ? FallbackYaw : FMath::RadiansToDegrees(FMath::Atan2(PlanarDirection.Y, PlanarDirection.X));
	}

	FVector ClampInsideArena(const FVector& Location, const FVector& ArenaCenter, const float MaxRadiusCm)
	{
		const FVector Offset(Location.X - ArenaCenter.X, Location.Y - ArenaCenter.Y, 0.f);
		const float Distance = Offset.Size();
		if (Distance <= MaxRadiusCm || Distance <= UE_KINDA_SMALL_NUMBER)
		{
			return FVector(Location.X, Location.Y, ArenaCenter.Z);
		}
		const FVector Clamped = Offset * (MaxRadiusCm / Distance);
		return FVector(ArenaCenter.X + Clamped.X, ArenaCenter.Y + Clamped.Y, ArenaCenter.Z);
	}

#if !UE_BUILD_SHIPPING
	void KillRunningArenaWaves(const TArray<FString>& Arguments, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		for (TActorIterator<ATDArenaEncounter> It(World); It; ++It)
		{
			const int32 KilledCount = It->KillCurrentWave();
			UE_LOG(LogTDArenaEncounter, Log, TEXT("TD.Arena.KillWave: %s killed %d monsters."), *It->GetName(), KilledCount);
		}
	}

	void ResetArenaEncounters(const TArray<FString>& Arguments, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		for (TActorIterator<ATDArenaEncounter> It(World); It; ++It)
		{
			It->ResetEncounter();
		}
	}

	FAutoConsoleCommandWithWorldAndArgs KillArenaWaveCommand(TEXT("TD.Arena.KillWave"), TEXT("Kills every monster of the running arena wave (development fast kill)."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&KillRunningArenaWaves));
	FAutoConsoleCommandWithWorldAndArgs ResetArenaCommand(TEXT("TD.Arena.Reset"), TEXT("Resets every arena encounter to idle and clears its monsters."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ResetArenaEncounters));
#endif
}

ATDArenaEncounter::ATDArenaEncounter()
{
	using namespace TDArenaEncounterInternal;

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	ArenaRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ArenaRoot"));
	SetRootComponent(ArenaRoot);

	EntryZone = CreateDefaultSubobject<UBoxComponent>(TEXT("EntryZone"));
	EntryZone->SetupAttachment(ArenaRoot);
	EntryZone->SetBoxExtent(DefaultEntryZoneExtent);
	EntryZone->SetRelativeLocation(FVector(-ArenaRadiusCm + DefaultEntryZoneInsetCm, 0.f, DefaultEntryZoneExtent.Z));
	EntryZone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	EntryZone->SetGenerateOverlapEvents(false);
	EntryZone->SetCanEverAffectNavigation(false);
	EntryZone->SetHiddenInGame(true);

	CheckpointArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Checkpoint"));
	CheckpointArrow->SetupAttachment(ArenaRoot);
	CheckpointArrow->SetRelativeLocation(FVector(-ArenaRadiusCm - DefaultCheckpointOutsetCm, 0.f, CheckpointHeightCm));
	CheckpointArrow->SetHiddenInGame(true);
}

void ATDArenaEncounter::OnConstruction(const FTransform& Transform)
{
	using namespace TDArenaEncounterInternal;

	Super::OnConstruction(Transform);
	if (!bShouldPlaceEntryFromRadius)
	{
		return;
	}
	if (EntryZone)
	{
		EntryZone->SetRelativeLocation(FVector(-ArenaRadiusCm + DefaultEntryZoneInsetCm, 0.f, EntryZone->GetUnscaledBoxExtent().Z));
	}
	if (CheckpointArrow)
	{
		CheckpointArrow->SetRelativeLocation(FVector(-ArenaRadiusCm - DefaultCheckpointOutsetCm, 0.f, CheckpointHeightCm));
	}
}

void ATDArenaEncounter::ComputeSpawnLocations(const FTDArenaSpawnEntry& Entry, const FVector& ArenaCenter, const float ArenaRadiusCm, const FVector& ChallengerLocation, TArray<FVector>& OutLocations)
{
	using namespace TDArenaEncounterInternal;

	OutLocations.Reset();
	const int32 SpawnCount = Entry.Count;
	if (SpawnCount <= 0)
	{
		return;
	}

	const bool bIsAroundChallenger = Entry.Anchor == ETDArenaSpawnAnchor::AroundChallenger;
	const FVector AnchorLocation = bIsAroundChallenger ? ChallengerLocation : ArenaCenter;
	const FVector FacingDirection = bIsAroundChallenger ? ArenaCenter - ChallengerLocation : ChallengerLocation - ArenaCenter;
	const float BaseYaw = ComputePlanarYaw(FacingDirection, 0.f);
	const float SpreadDegrees = FMath::Clamp(Entry.SpreadDegrees, 0.f, 360.f);
	const bool bIsFullCircle = SpreadDegrees >= 360.f - UE_KINDA_SMALL_NUMBER;
	const float StepDegrees = SpawnCount <= 1 ? 0.f : (bIsFullCircle ? 360.f / SpawnCount : SpreadDegrees / (SpawnCount - 1));
	const float FirstYaw = SpawnCount <= 1 || bIsFullCircle ? BaseYaw : BaseYaw - SpreadDegrees * 0.5f;
	const float MaxRadiusCm = FMath::Max(ArenaRadiusCm, 0.f);

	OutLocations.Reserve(SpawnCount);
	for (int32 SpawnIndex = 0; SpawnIndex < SpawnCount; ++SpawnIndex)
	{
		const FVector Direction = MakePlanarDirection(FirstYaw + StepDegrees * SpawnIndex);
		const FVector Candidate = AnchorLocation + Direction * FMath::Max(Entry.DistanceCm, 0.f);
		OutLocations.Add(ClampInsideArena(Candidate, ArenaCenter, MaxRadiusCm));
	}
}

bool ATDArenaEncounter::TryStartEncounter(APawn* Challenger)
{
	if (EncounterState != ETDArenaEncounterState::Idle || !Challenger || !Definition || Definition->Waves.IsEmpty())
	{
		return false;
	}
	if (!UTDCombatLibrary::IsActorAlive(Challenger))
	{
		return false;
	}

	++AttemptCount;
	KilledMonsterCount = 0;
	bWasBossDefeated = false;
	CurrentWaveIndex = INDEX_NONE;
	BindChallenger(Challenger);
	BeginCameraPullBack(Challenger);
	SetSealed(true);
	ApplyRespawnCheckpoint();
	UE_LOG(LogTDArenaEncounter, Log, TEXT("%s: attempt %d started by %s."), *GetName(), AttemptCount, *Challenger->GetName());

	if (!bAreSpeciesLoaded)
	{
		EncounterState = ETDArenaEncounterState::Loading;
		if (!SpeciesHandle.IsValid())
		{
			RequestSpeciesLoad();
		}
		if (!bAreSpeciesLoaded)
		{
			return true;
		}
	}
	BeginEncounter();
	return true;
}

void ATDArenaEncounter::ResetEncounter()
{
	DestroyHostilesInArena();
	for (const TWeakObjectPtr<ATDMonsterCharacter>& Monster : AliveMonsters)
	{
		if (ATDMonsterCharacter* LivingMonster = Monster.Get())
		{
			LivingMonster->Destroy();
		}
	}
	AliveMonsters.Reset();
	HideBossHealthBar();
	UnbindChallenger();
	EndCameraPullBack();
	CurrentWaveIndex = INDEX_NONE;
	StateSecondsRemaining = 0.f;
	SetSealed(false);
	SetExitOpen(false);
	EncounterState = ETDArenaEncounterState::Idle;
	UE_LOG(LogTDArenaEncounter, Log, TEXT("%s: reset to idle after attempt %d."), *GetName(), AttemptCount);
}

int32 ATDArenaEncounter::KillCurrentWave()
{
	if (EncounterState != ETDArenaEncounterState::WaveActive && EncounterState != ETDArenaEncounterState::BossActive)
	{
		return 0;
	}
	TArray<ATDMonsterCharacter*> Monsters;
	GetAliveMonsters(Monsters);
	FTDDamageSpec LethalDamage;
	LethalDamage.Amount = TDArenaEncounterInternal::LeftoverLethalDamage;
	LethalDamage.Instigator = ChallengerPawn.Get();
	LethalDamage.Causer = this;
	int32 KilledCount = 0;
	for (ATDMonsterCharacter* Monster : Monsters)
	{
		if (UTDCombatLibrary::IsActorAlive(Monster))
		{
			UTDCombatLibrary::TryApplyDamage(Monster, LethalDamage);
			++KilledCount;
		}
	}
	return KilledCount;
}

bool ATDArenaEncounter::SetEncounterDefinition(UTDArenaEncounterDefinition* NewDefinition)
{
	if (EncounterState != ETDArenaEncounterState::Idle)
	{
		return false;
	}
	if (SpeciesHandle.IsValid())
	{
		SpeciesHandle->CancelHandle();
		SpeciesHandle.Reset();
	}
	Definition = NewDefinition;
	LoadedSpecies.Reset();
	bAreSpeciesLoaded = false;
	if (HasActorBegunPlay())
	{
		RequestSpeciesLoad();
	}
	return true;
}

void ATDArenaEncounter::SetGateActors(const TArray<AActor*>& NewSealGateActors, const TArray<AActor*>& NewExitGateActors)
{
	SealGateActors.Reset();
	ExitGateActors.Reset();
	for (AActor* Gate : NewSealGateActors)
	{
		SealGateActors.Add(Gate);
	}
	for (AActor* Gate : NewExitGateActors)
	{
		ExitGateActors.Add(Gate);
	}
	SetSealed(bIsSealed);
	SetExitOpen(bIsExitOpen);
}

int32 ATDArenaEncounter::GetAliveMonsterCount() const
{
	int32 AliveCount = 0;
	for (const TWeakObjectPtr<ATDMonsterCharacter>& Monster : AliveMonsters)
	{
		AliveCount += Monster.IsValid() ? 1 : 0;
	}
	return AliveCount;
}

FTransform ATDArenaEncounter::GetCheckpointTransform() const
{
	if (!CheckpointArrow)
	{
		return GetActorTransform();
	}
	const FRotator CheckpointRotation(0.f, CheckpointArrow->GetComponentRotation().Yaw, 0.f);
	return FTransform(CheckpointRotation, CheckpointArrow->GetComponentLocation());
}

bool ATDArenaEncounter::IsLocationInEntryZone(const FVector& WorldLocation) const
{
	if (!EntryZone)
	{
		return false;
	}
	const FVector LocalLocation = EntryZone->GetComponentTransform().InverseTransformPosition(WorldLocation);
	const FVector Extent = EntryZone->GetUnscaledBoxExtent();
	return FMath::Abs(LocalLocation.X) <= Extent.X && FMath::Abs(LocalLocation.Y) <= Extent.Y && FMath::Abs(LocalLocation.Z) <= Extent.Z;
}

FVector ATDArenaEncounter::GetEntryZoneCenter() const
{
	return EntryZone ? EntryZone->GetComponentLocation() : GetActorLocation();
}

void ATDArenaEncounter::GetAliveMonsters(TArray<ATDMonsterCharacter*>& OutMonsters) const
{
	OutMonsters.Reset();
	for (const TWeakObjectPtr<ATDMonsterCharacter>& Monster : AliveMonsters)
	{
		if (ATDMonsterCharacter* LivingMonster = Monster.Get())
		{
			OutMonsters.Add(LivingMonster);
		}
	}
}

void ATDArenaEncounter::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateCameraDistance(DeltaSeconds);

	if (IsFightRunning() && !UTDCombatLibrary::IsActorAlive(ChallengerPawn.Get()))
	{
		FailEncounter();
		return;
	}

	switch (EncounterState)
	{
	case ETDArenaEncounterState::Idle:
		if (APawn* Challenger = FindChallengerInEntryZone())
		{
			TryStartEncounter(Challenger);
		}
		break;
	case ETDArenaEncounterState::Loading:
		if (bAreSpeciesLoaded)
		{
			BeginEncounter();
		}
		break;
	case ETDArenaEncounterState::WaveDelay:
		StateSecondsRemaining -= DeltaSeconds;
		if (StateSecondsRemaining <= 0.f)
		{
			SpawnCurrentWave();
		}
		break;
	case ETDArenaEncounterState::WaveActive:
	case ETDArenaEncounterState::BossActive:
		PruneVanishedMonsters();
		if (AliveMonsters.IsEmpty())
		{
			AdvanceAfterWaveCleared();
		}
		break;
	case ETDArenaEncounterState::Failed:
		StateSecondsRemaining -= DeltaSeconds;
		if (StateSecondsRemaining <= 0.f)
		{
			ResetEncounter();
		}
		break;
	default:
		break;
	}
}

void ATDArenaEncounter::BeginPlay()
{
	Super::BeginPlay();
	SetSealed(false);
	if (WasVictoryRecorded())
	{
		EncounterState = ETDArenaEncounterState::Victory;
		SetExitOpen(true);
		return;
	}
	SetExitOpen(false);
	RequestSpeciesLoad();
}

void ATDArenaEncounter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (SpeciesHandle.IsValid())
	{
		SpeciesHandle->CancelHandle();
		SpeciesHandle.Reset();
	}
	UnbindChallenger();
	Super::EndPlay(EndPlayReason);
}

void ATDArenaEncounter::RequestSpeciesLoad()
{
	if (!Definition)
	{
		return;
	}

	TArray<FSoftObjectPath> SpeciesPaths;
	Definition->GatherSpeciesPaths(SpeciesPaths);
	TArray<FSoftObjectPath> UnresolvedPaths = SpeciesPaths.FilterByPredicate([](const FSoftObjectPath& Path) { return Path.ResolveObject() == nullptr; });
	CollectLoadedSpecies(false);
	if (UnresolvedPaths.IsEmpty())
	{
		HandleSpeciesLoaded();
		return;
	}

	SpeciesHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(MoveTemp(UnresolvedPaths), FStreamableDelegate::CreateUObject(this, &ThisClass::HandleSpeciesLoaded), FStreamableManager::AsyncLoadHighPriority);
	if (!SpeciesHandle.IsValid())
	{
		UE_LOG(LogTDArenaEncounter, Warning, TEXT("%s: species load request failed; spawning only resident species."), *GetName());
		HandleSpeciesLoaded();
	}
}

void ATDArenaEncounter::HandleSpeciesLoaded()
{
	CollectLoadedSpecies(true);
	bAreSpeciesLoaded = true;
	if (EncounterState == ETDArenaEncounterState::Loading)
	{
		BeginEncounter();
	}
}

void ATDArenaEncounter::CollectLoadedSpecies(const bool bShouldWarnMissing)
{
	LoadedSpecies.Reset();
	if (!Definition)
	{
		return;
	}
	TArray<FSoftObjectPath> SpeciesPaths;
	Definition->GatherSpeciesPaths(SpeciesPaths);
	for (const FSoftObjectPath& SpeciesPath : SpeciesPaths)
	{
		UTDMonsterSpeciesAsset* Species = Cast<UTDMonsterSpeciesAsset>(SpeciesPath.ResolveObject());
		if (!Species)
		{
			UE_CLOG(bShouldWarnMissing, LogTDArenaEncounter, Warning, TEXT("%s: species '%s' did not load."), *GetName(), *SpeciesPath.ToString());
			continue;
		}
		LoadedSpecies.AddUnique(Species);
	}
}

APawn* ATDArenaEncounter::FindChallengerInEntryZone() const
{
	if (!Definition)
	{
		return nullptr;
	}
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!PlayerPawn || !UTDCombatLibrary::IsActorAlive(PlayerPawn))
	{
		return nullptr;
	}
	return IsLocationInEntryZone(PlayerPawn->GetActorLocation()) ? PlayerPawn : nullptr;
}

void ATDArenaEncounter::BeginEncounter()
{
	BeginWaveDelay(0);
}

void ATDArenaEncounter::BeginWaveDelay(const int32 WaveIndex)
{
	if (!Definition || !Definition->Waves.IsValidIndex(WaveIndex))
	{
		return;
	}

	const FTDArenaWave& Wave = Definition->Waves[WaveIndex];
	CurrentWaveIndex = WaveIndex;
	EncounterState = ETDArenaEncounterState::WaveDelay;
	StateSecondsRemaining = Wave.StartDelaySeconds;

	if (Definition->IsBossWave(WaveIndex))
	{
		const FTDArenaSpawnEntry* BossEntry = Wave.Entries.FindByPredicate([](const FTDArenaSpawnEntry& Entry) { return Entry.bShowsBossHealthBar; });
		const FText BossTitle = BossEntry && !BossEntry->BossDisplayName.IsEmpty() ? BossEntry->BossDisplayName : MakeWaveTitle(WaveIndex);
		BroadcastBanner(BossTitle, Wave.Title, ETDEncounterBannerStyle::Boss, Definition->BannerSeconds);
	}
	else
	{
		BroadcastBanner(MakeWaveTitle(WaveIndex), WaveIndex == 0 ? Definition->DisplayName : FText::GetEmpty(), ETDEncounterBannerStyle::Wave, Definition->BannerSeconds);
	}

	if (StateSecondsRemaining <= 0.f)
	{
		SpawnCurrentWave();
	}
}

void ATDArenaEncounter::SpawnCurrentWave()
{
	if (!Definition || !Definition->Waves.IsValidIndex(CurrentWaveIndex))
	{
		return;
	}

	const APawn* Challenger = ChallengerPawn.Get();
	const FVector ArenaCenter = GetActorLocation();
	const FVector ChallengerLocation = Challenger ? Challenger->GetActorLocation() : ArenaCenter;
	const float SpawnRadiusCm = FMath::Max(ArenaRadiusCm - SpawnEdgeMarginCm, 0.f);
	EncounterState = Definition->IsBossWave(CurrentWaveIndex) ? ETDArenaEncounterState::BossActive : ETDArenaEncounterState::WaveActive;

	const FTDArenaSpawnEntry* UnloadedEntry = Definition->Waves[CurrentWaveIndex].Entries.FindByPredicate([](const FTDArenaSpawnEntry& Entry) { return !Entry.Species.IsNull() && Entry.Count > 0 && !Entry.Species.Get(); });
	if (UnloadedEntry)
	{
		UE_LOG(LogTDArenaEncounter, Error, TEXT("%s: wave %d species '%s' is not loaded; failing the attempt instead of granting a free clear."), *GetName(), CurrentWaveIndex, *UnloadedEntry->Species.ToString());
		FailEncounter();
		return;
	}

	TArray<FVector> SpawnLocations;
	LastWaveSpawnLocations.Reset();
	for (const FTDArenaSpawnEntry& Entry : Definition->Waves[CurrentWaveIndex].Entries)
	{
		UTDMonsterSpeciesAsset* Species = Entry.Species.Get();
		if (!Species)
		{
			UE_LOG(LogTDArenaEncounter, Warning, TEXT("%s: wave %d skips unloaded species '%s'."), *GetName(), CurrentWaveIndex, *Entry.Species.ToString());
			continue;
		}

		ComputeSpawnLocations(Entry, ArenaCenter, SpawnRadiusCm, ChallengerLocation, SpawnLocations);
		bool bHasShownBossBar = false;
		for (const FVector& SpawnLocation : SpawnLocations)
		{
			const FRotator FacingChallenger(0.f, TDArenaEncounterInternal::ComputePlanarYaw(ChallengerLocation - SpawnLocation, GetActorRotation().Yaw), 0.f);
			ATDMonsterCharacter* Monster = SpawnMonster(*Species, SpawnLocation, FacingChallenger);
			if (!Monster)
			{
				UE_LOG(LogTDArenaEncounter, Warning, TEXT("%s: failed to spawn '%s' at %s."), *GetName(), *Species->GetName(), *SpawnLocation.ToCompactString());
				continue;
			}
			AliveMonsters.Add(Monster);
			LastWaveSpawnLocations.Add(SpawnLocation);
			Monster->EngageMonsterBrain(ChallengerPawn.Get());
			if (UTDCombatComponent* MonsterCombat = Monster->GetCombatComponent())
			{
				MonsterCombat->OnDeath.AddUObject(this, &ThisClass::HandleMonsterDeath, TWeakObjectPtr<ATDMonsterCharacter>(Monster));
			}
			if (Entry.bShowsBossHealthBar && !bHasShownBossBar)
			{
				ShowBossHealthBar(*Monster, Entry);
				bHasShownBossBar = true;
			}
		}
	}
	UE_LOG(LogTDArenaEncounter, Log, TEXT("%s: wave %d spawned %d monsters."), *GetName(), CurrentWaveIndex, AliveMonsters.Num());
	if (AliveMonsters.IsEmpty() && Definition->CountMonstersInWave(CurrentWaveIndex) > 0)
	{
		UE_LOG(LogTDArenaEncounter, Error, TEXT("%s: wave %d spawned nothing; failing the attempt instead of granting a free clear."), *GetName(), CurrentWaveIndex);
		FailEncounter();
	}
}

ATDMonsterCharacter* ATDArenaEncounter::SpawnMonster(UTDMonsterSpeciesAsset& Species, const FVector& Location, const FRotator& Rotation)
{
	using namespace TDArenaEncounterInternal;

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	ATDMonsterCharacter* Monster = World->SpawnActorDeferred<ATDMonsterCharacter>(ATDMonsterCharacter::StaticClass(), FTransform(Rotation, Location), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!Monster)
	{
		return nullptr;
	}

	Monster->SetSpecies(&Species);
	FVector GroundedLocation = Location;
	FCollisionObjectQueryParams GroundObjects;
	GroundObjects.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TDArenaEncounterSpawn), false, this);
	QueryParams.AddIgnoredActor(Monster);
	QueryParams.AddIgnoredActor(ChallengerPawn.Get());
	FHitResult GroundHit;
	if (World->LineTraceSingleByObjectType(GroundHit, Location + FVector(0.f, 0.f, SpawnTraceAboveCm), Location - FVector(0.f, 0.f, SpawnTraceBelowCm), GroundObjects, QueryParams))
	{
		const UCapsuleComponent* Capsule = Monster->GetCapsuleComponent();
		const float CapsuleHalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0.f;
		GroundedLocation.Z = GroundHit.ImpactPoint.Z + CapsuleHalfHeight + SpawnGroundClearanceCm;
	}

	Monster->FinishSpawning(FTransform(Rotation, GroundedLocation));
	return Monster;
}

void ATDArenaEncounter::ShowBossHealthBar(ATDMonsterCharacter& Monster, const FTDArenaSpawnEntry& Entry)
{
	BossHealthBarTarget = &Monster;
	bIsBossHealthBarShown = true;
	if (!UGameplayMessageSubsystem::HasInstance(this))
	{
		return;
	}
	FTDBossHealthBarMessage Message;
	Message.Encounter = this;
	Message.Boss = &Monster;
	Message.DisplayName = Entry.BossDisplayName.IsEmpty() ? FText::FromString(Entry.Species.GetAssetName()) : Entry.BossDisplayName;
	const TArray<float> ReportedMarks = Monster.GetPhaseHealthMarks();
	Message.PhaseHealthMarks = ReportedMarks.IsEmpty() ? Entry.BossPhaseHealthMarks : ReportedMarks;
	Message.bIsShown = true;
	UGameplayMessageSubsystem::Get(this).BroadcastMessage(TDGameplayTags::Event_Encounter_BossHealthBar, Message);
}

void ATDArenaEncounter::HideBossHealthBar()
{
	if (!bIsBossHealthBarShown)
	{
		return;
	}
	ATDMonsterCharacter* Boss = BossHealthBarTarget.Get();
	BossHealthBarTarget.Reset();
	bIsBossHealthBarShown = false;
	if (!UGameplayMessageSubsystem::HasInstance(this))
	{
		return;
	}
	FTDBossHealthBarMessage Message;
	Message.Encounter = this;
	Message.Boss = Boss;
	Message.bIsShown = false;
	UGameplayMessageSubsystem::Get(this).BroadcastMessage(TDGameplayTags::Event_Encounter_BossHealthBar, Message);
}

void ATDArenaEncounter::HandleMonsterDeath(const FTDDamageContext& Context, const TWeakObjectPtr<ATDMonsterCharacter> Monster)
{
	const int32 RemovedCount = AliveMonsters.RemoveAll([&Monster](const TWeakObjectPtr<ATDMonsterCharacter>& AliveMonster) { return AliveMonster == Monster; });
	if (RemovedCount == 0)
	{
		return;
	}
	KilledMonsterCount += RemovedCount;
	if (Monster.IsValid() && BossHealthBarTarget == Monster)
	{
		bWasBossDefeated = true;
		HideBossHealthBar();
	}
}

void ATDArenaEncounter::HandleChallengerDeath(const FTDDamageContext& Context)
{
	if (!IsFightRunning())
	{
		return;
	}
	FailEncounter();
}

void ATDArenaEncounter::PruneVanishedMonsters()
{
	AliveMonsters.RemoveAll([](const TWeakObjectPtr<ATDMonsterCharacter>& Monster) { return !Monster.IsValid(); });
}

void ATDArenaEncounter::AdvanceAfterWaveCleared()
{
	if (!Definition)
	{
		return;
	}
	if (Definition->IsBossWave(CurrentWaveIndex))
	{
		KillHostilesInArena();
	}
	const int32 NextWaveIndex = CurrentWaveIndex + 1;
	if (Definition->Waves.IsValidIndex(NextWaveIndex))
	{
		BeginWaveDelay(NextWaveIndex);
		return;
	}
	FinishWithVictory();
}

void ATDArenaEncounter::FinishWithVictory()
{
	EncounterState = ETDArenaEncounterState::Victory;
	StateSecondsRemaining = 0.f;
	HideBossHealthBar();
	KillHostilesInArena();
	SetSealed(false);
	SetExitOpen(true);
	UnbindChallenger();
	EndCameraPullBack();
	ReleaseRespawnCheckpoint();
	RecordVictory();
	const FText VictoryTitle = FText::FromString(TEXT("승리"));
	BroadcastBanner(VictoryTitle, Definition ? Definition->VictoryRewardText : FText::GetEmpty(), ETDEncounterBannerStyle::Victory, Definition ? Definition->VictoryBannerSeconds : 6.f);
	BroadcastFinished(true);
	UE_LOG(LogTDArenaEncounter, Log, TEXT("%s: victory after %d kills on attempt %d."), *GetName(), KilledMonsterCount, AttemptCount);
}

void ATDArenaEncounter::FailEncounter()
{
	EncounterState = ETDArenaEncounterState::Failed;
	StateSecondsRemaining = Definition ? Definition->FailureResetSeconds : 0.f;
	HideBossHealthBar();
	UnbindChallenger();
	EndCameraPullBack();
	const FText DefeatTitle = FText::FromString(TEXT("패배"));
	const FText DefeatSubtitle = FText::FromString(TEXT("입구에서 다시 도전하세요"));
	BroadcastBanner(DefeatTitle, DefeatSubtitle, ETDEncounterBannerStyle::Defeat, Definition ? Definition->BannerSeconds : 3.f);
	BroadcastFinished(false);
	UE_LOG(LogTDArenaEncounter, Log, TEXT("%s: challenger fell in wave %d on attempt %d."), *GetName(), CurrentWaveIndex, AttemptCount);
}

void ATDArenaEncounter::KillHostilesInArena()
{
	TArray<ATDMonsterCharacter*> Hostiles;
	CollectHostilesInArena(Hostiles);
	FTDDamageSpec LethalDamage;
	LethalDamage.Amount = TDArenaEncounterInternal::LeftoverLethalDamage;
	LethalDamage.Instigator = ChallengerPawn.Get();
	LethalDamage.Causer = this;
	for (ATDMonsterCharacter* Hostile : Hostiles)
	{
		if (UTDCombatLibrary::IsActorAlive(Hostile))
		{
			UTDCombatLibrary::TryApplyDamage(Hostile, LethalDamage);
		}
	}
}

void ATDArenaEncounter::DestroyHostilesInArena()
{
	TArray<ATDMonsterCharacter*> Hostiles;
	CollectHostilesInArena(Hostiles);
	for (ATDMonsterCharacter* Hostile : Hostiles)
	{
		Hostile->Destroy();
	}
}

void ATDArenaEncounter::CollectHostilesInArena(TArray<ATDMonsterCharacter*>& OutMonsters) const
{
	OutMonsters.Reset();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const AActor* ReferencePawn = ChallengerPawn.IsValid() ? ChallengerPawn.Get() : UGameplayStatics::GetPlayerPawn(this, 0);
	const FVector ArenaCenter = GetActorLocation();
	const float CleanupRadiusSquared = FMath::Square(ArenaRadiusCm + TDArenaEncounterInternal::ArenaCleanupMarginCm);
	for (TActorIterator<ATDMonsterCharacter> It(World); It; ++It)
	{
		ATDMonsterCharacter* Monster = *It;
		if (!IsValid(Monster) || FVector::DistSquared2D(Monster->GetActorLocation(), ArenaCenter) > CleanupRadiusSquared)
		{
			continue;
		}
		const bool bIsKnownAlly = ReferencePawn && UTDCombatLibrary::GetActorTeamId(ReferencePawn) == UTDCombatLibrary::GetActorTeamId(Monster);
		if (!bIsKnownAlly)
		{
			OutMonsters.Add(Monster);
		}
	}
}

void ATDArenaEncounter::SetGatesClosed(const TArray<TObjectPtr<AActor>>& Gates, const bool bShouldClose)
{
	for (AActor* Gate : Gates)
	{
		if (!IsValid(Gate))
		{
			continue;
		}
		Gate->SetActorHiddenInGame(!bShouldClose);
		Gate->SetActorEnableCollision(bShouldClose);
	}
}

void ATDArenaEncounter::SetSealed(const bool bShouldSeal)
{
	bIsSealed = bShouldSeal;
	SetGatesClosed(SealGateActors, bShouldSeal);
}

void ATDArenaEncounter::SetExitOpen(const bool bShouldOpen)
{
	bIsExitOpen = bShouldOpen;
	SetGatesClosed(ExitGateActors, !bShouldOpen);
}

void ATDArenaEncounter::BindChallenger(APawn* Challenger)
{
	UnbindChallenger();
	ChallengerPawn = Challenger;
	UTDCombatComponent* Combat = UTDCombatLibrary::GetCombatComponent(Challenger);
	if (!Combat)
	{
		return;
	}
	ChallengerCombat = Combat;
	ChallengerDeathHandle = Combat->OnDeath.AddUObject(this, &ThisClass::HandleChallengerDeath);
}

void ATDArenaEncounter::UnbindChallenger()
{
	if (UTDCombatComponent* Combat = ChallengerCombat.Get())
	{
		Combat->OnDeath.Remove(ChallengerDeathHandle);
	}
	ChallengerCombat.Reset();
	ChallengerDeathHandle.Reset();
}

void ATDArenaEncounter::BeginCameraPullBack(APawn* Challenger)
{
	USpringArmComponent* CameraBoom = Challenger ? Challenger->FindComponentByClass<USpringArmComponent>() : nullptr;
	if (!CameraBoom)
	{
		return;
	}
	if (ChallengerCameraBoom.Get() != CameraBoom)
	{
		ChallengerCameraBoom = CameraBoom;
		BaseCameraArmLength = CameraBoom->TargetArmLength;
	}
	DesiredCameraArmLength = BaseCameraArmLength * CameraDistanceScale;
}

void ATDArenaEncounter::EndCameraPullBack()
{
	DesiredCameraArmLength = BaseCameraArmLength;
}

void ATDArenaEncounter::UpdateCameraDistance(const float DeltaSeconds)
{
	USpringArmComponent* CameraBoom = ChallengerCameraBoom.Get();
	if (!CameraBoom || DesiredCameraArmLength <= 0.f || FMath::IsNearlyEqual(CameraBoom->TargetArmLength, DesiredCameraArmLength))
	{
		return;
	}
	const float BlendSpeed = FMath::Abs(BaseCameraArmLength * (CameraDistanceScale - 1.f)) / FMath::Max(CameraBlendSeconds, 0.1f);
	CameraBoom->TargetArmLength = FMath::FInterpConstantTo(CameraBoom->TargetArmLength, DesiredCameraArmLength, DeltaSeconds, FMath::Max(BlendSpeed, 1.f));
}

void ATDArenaEncounter::ApplyRespawnCheckpoint() const
{
	if (!bShouldSetRespawnCheckpoint)
	{
		return;
	}
	const UWorld* World = GetWorld();
	if (ATDGameGameMode* GameMode = World ? World->GetAuthGameMode<ATDGameGameMode>() : nullptr)
	{
		GameMode->SetRespawnCheckpoint(GetCheckpointTransform());
	}
}

void ATDArenaEncounter::ReleaseRespawnCheckpoint() const
{
	if (!bShouldSetRespawnCheckpoint)
	{
		return;
	}
	const UWorld* World = GetWorld();
	ATDGameGameMode* GameMode = World ? World->GetAuthGameMode<ATDGameGameMode>() : nullptr;
	if (GameMode && GameMode->HasRespawnCheckpoint() && GameMode->GetRespawnCheckpoint().Equals(GetCheckpointTransform()))
	{
		GameMode->ClearRespawnCheckpoint();
	}
}

bool ATDArenaEncounter::WasVictoryRecorded() const
{
	if (!bShouldRecordVictoryInWorldState)
	{
		return false;
	}
	const UGameInstance* GameInstance = GetGameInstance();
	const UTDWorldStateSubsystem* WorldState = GameInstance ? GameInstance->GetSubsystem<UTDWorldStateSubsystem>() : nullptr;
	FTDDungeonState RecordedState;
	return WorldState && WorldState->FindDungeonState(GetEncounterId(), RecordedState) && RecordedState.bCompleted;
}

void ATDArenaEncounter::RecordVictory() const
{
	if (!bShouldRecordVictoryInWorldState)
	{
		return;
	}
	const UGameInstance* GameInstance = GetGameInstance();
	UTDWorldStateSubsystem* WorldState = GameInstance ? GameInstance->GetSubsystem<UTDWorldStateSubsystem>() : nullptr;
	if (!WorldState)
	{
		return;
	}
	FTDDungeonState& DungeonState = WorldState->FindOrAddDungeonState(GetEncounterId());
	DungeonState.bCompleted = true;
	DungeonState.bBossKilled = DungeonState.bBossKilled || bWasBossDefeated;
}

void ATDArenaEncounter::BroadcastBanner(const FText& Title, const FText& Subtitle, const ETDEncounterBannerStyle Style, const float DisplaySeconds)
{
	if (!UGameplayMessageSubsystem::HasInstance(this))
	{
		return;
	}
	FTDEncounterBannerMessage Message;
	Message.Encounter = this;
	Message.Title = Title;
	Message.Subtitle = Subtitle;
	Message.Style = Style;
	Message.DisplaySeconds = DisplaySeconds;
	UGameplayMessageSubsystem::Get(this).BroadcastMessage(TDGameplayTags::Event_Encounter_Banner, Message);
}

void ATDArenaEncounter::BroadcastFinished(const bool bWasVictory)
{
	if (!UGameplayMessageSubsystem::HasInstance(this))
	{
		return;
	}
	FTDEncounterFinishedMessage Message;
	Message.Encounter = this;
	Message.EncounterId = GetEncounterId();
	Message.bWasVictory = bWasVictory;
	UGameplayMessageSubsystem::Get(this).BroadcastMessage(TDGameplayTags::Event_Encounter_Finished, Message);
}

FText ATDArenaEncounter::MakeWaveTitle(const int32 WaveIndex) const
{
	if (Definition && Definition->Waves.IsValidIndex(WaveIndex) && !Definition->Waves[WaveIndex].Title.IsEmpty())
	{
		return Definition->Waves[WaveIndex].Title;
	}
	const int32 WaveCount = Definition ? Definition->Waves.Num() : 0;
	return FText::Format(FText::FromString(TEXT("웨이브 {0}/{1}")), FText::AsNumber(WaveIndex + 1), FText::AsNumber(WaveCount));
}

FName ATDArenaEncounter::GetEncounterId() const
{
	if (Definition && !Definition->EncounterId.IsNone())
	{
		return Definition->EncounterId;
	}
	return GetFName();
}

bool ATDArenaEncounter::IsFightRunning() const
{
	switch (EncounterState)
	{
	case ETDArenaEncounterState::Loading:
	case ETDArenaEncounterState::WaveDelay:
	case ETDArenaEncounterState::WaveActive:
	case ETDArenaEncounterState::BossActive:
		return true;
	default:
		return false;
	}
}
