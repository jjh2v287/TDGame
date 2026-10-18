#include "Characters/TDMonsterCharacter.h"
#include "Combat/TDCombatComponent.h"
#include "Combat/TDCombatLibrary.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"
#include "MonsterAI/TDMonsterSpeciesAsset.h"
#include "World/Generation/TDArenaEncounter.h"
#include "World/Generation/TDArenaEncounterDefinition.h"

namespace TDArenaEncounterTests
{
	constexpr float ArenaStepSeconds = 1.f / 30.f;
	constexpr float ArenaWaveDelaySeconds = 0.5f;
	constexpr float ArenaFailureResetSeconds = 1.f;
	constexpr float ArenaChallengerMaxHealth = 1000000.f;
	constexpr float ArenaLethalDamage = 100000000.f;
	constexpr float ArenaBossProbeDamage = 300.f;
	const FVector ArenaChallengerLocation(-1000.f, 0.f, 100.f);
	const TCHAR* const ArenaMinionDefinitionId = TEXT("Goblin_Melee");
	const TCHAR* const ArenaBossDefinitionId = TEXT("Ogre_Boss");

	struct FTDArenaTestWorld
	{
		FTDArenaTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDArenaTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
			SpawnFloor();
			MinionSpecies = MakeSpecies(ArenaMinionDefinitionId);
			BossSpecies = MakeSpecies(ArenaBossDefinitionId);
		}

		~FTDArenaTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		UTDMonsterSpeciesAsset* MakeSpecies(const FName DefinitionId) const
		{
			UTDMonsterSpeciesAsset* Species = NewObject<UTDMonsterSpeciesAsset>(World);
			Species->DefinitionId = DefinitionId;
			return Species;
		}

		void SpawnFloor() const
		{
			AActor* Floor = World->SpawnActor<AActor>();
			UBoxComponent* FloorBox = NewObject<UBoxComponent>(Floor);
			Floor->AddInstanceComponent(FloorBox);
			Floor->SetRootComponent(FloorBox);
			FloorBox->SetBoxExtent(FVector(5000.f, 5000.f, 50.f));
			FloorBox->SetCollisionObjectType(ECC_WorldStatic);
			FloorBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			FloorBox->SetCollisionResponseToAllChannels(ECR_Block);
			FloorBox->RegisterComponent();
			Floor->SetActorLocation(FVector(0.f, 0.f, -50.f));
		}

		AActor* SpawnGate(const FVector& Location) const
		{
			AActor* Gate = World->SpawnActor<AActor>();
			UBoxComponent* GateBox = NewObject<UBoxComponent>(Gate);
			Gate->AddInstanceComponent(GateBox);
			Gate->SetRootComponent(GateBox);
			GateBox->SetBoxExtent(FVector(50.f, 300.f, 200.f));
			GateBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			GateBox->RegisterComponent();
			Gate->SetActorLocation(Location);
			return Gate;
		}

		APawn* SpawnChallenger(const FVector& Location) const
		{
			APawn* Challenger = World->SpawnActor<APawn>();
			USphereComponent* Sphere = NewObject<USphereComponent>(Challenger);
			Challenger->AddInstanceComponent(Sphere);
			Challenger->SetRootComponent(Sphere);
			Sphere->SetSphereRadius(30.f);
			Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Sphere->SetCollisionObjectType(ECC_Pawn);
			Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
			Sphere->RegisterComponent();
			Challenger->SetActorLocation(Location);
			UTDCombatComponent* Combatant = NewObject<UTDCombatComponent>(Challenger);
			Challenger->AddInstanceComponent(Combatant);
			FTDCombatStats Stats;
			Stats.TeamId = 1;
			Stats.BaseMaxHealth = ArenaChallengerMaxHealth;
			Combatant->SetStats(Stats);
			Combatant->RegisterComponent();
			return Challenger;
		}

		ATDArenaEncounter* SpawnEncounter(UTDArenaEncounterDefinition* Definition) const
		{
			ATDArenaEncounter* Encounter = World->SpawnActorDeferred<ATDArenaEncounter>(ATDArenaEncounter::StaticClass(), FTransform::Identity);
			Encounter->SetEncounterDefinition(Definition);
			Encounter->FinishSpawning(FTransform::Identity);
			return Encounter;
		}

		void Tick(const float Duration) const
		{
			for (float Remaining = Duration; Remaining > UE_SMALL_NUMBER; Remaining -= ArenaStepSeconds)
			{
				++GFrameCounter;
				World->Tick(LEVELTICK_All, ArenaStepSeconds);
			}
		}

		int32 CountLiveMonsterActors() const
		{
			int32 MonsterCount = 0;
			for (TActorIterator<ATDMonsterCharacter> It(World); It; ++It)
			{
				MonsterCount += IsValid(*It) ? 1 : 0;
			}
			return MonsterCount;
		}

		UWorld* World = nullptr;
		UTDMonsterSpeciesAsset* MinionSpecies = nullptr;
		UTDMonsterSpeciesAsset* BossSpecies = nullptr;
	};

	FTDArenaSpawnEntry MakeEntry(UTDMonsterSpeciesAsset* Species, const int32 Count, const bool bIsBoss)
	{
		FTDArenaSpawnEntry Entry;
		Entry.Species = Species;
		Entry.Count = Count;
		Entry.DistanceCm = 700.f;
		Entry.SpreadDegrees = 90.f;
		Entry.Anchor = bIsBoss ? ETDArenaSpawnAnchor::ArenaCenter : ETDArenaSpawnAnchor::AroundChallenger;
		Entry.DistanceCm = bIsBoss ? 0.f : Entry.DistanceCm;
		Entry.bShowsBossHealthBar = bIsBoss;
		Entry.BossDisplayName = bIsBoss ? FText::FromString(TEXT("Test Boss")) : FText::GetEmpty();
		Entry.BossPhaseHealthMarks = bIsBoss ? TArray<float>{ 0.7f, 0.4f, 0.25f } : TArray<float>();
		return Entry;
	}

	FTDArenaWave MakeWave(const TArray<FTDArenaSpawnEntry>& Entries)
	{
		FTDArenaWave Wave;
		Wave.StartDelaySeconds = ArenaWaveDelaySeconds;
		Wave.Entries = Entries;
		return Wave;
	}

	UTDArenaEncounterDefinition* MakeThreeWaveDefinition(const FTDArenaTestWorld& Fixture)
	{
		UTDArenaEncounterDefinition* Definition = NewObject<UTDArenaEncounterDefinition>(Fixture.World);
		Definition->EncounterId = TEXT("TestArena");
		Definition->FailureResetSeconds = ArenaFailureResetSeconds;
		Definition->Waves.Add(MakeWave({ MakeEntry(Fixture.MinionSpecies, 2, false) }));
		Definition->Waves.Add(MakeWave({ MakeEntry(Fixture.MinionSpecies, 1, false) }));
		Definition->Waves.Add(MakeWave({ MakeEntry(Fixture.BossSpecies, 1, true) }));
		return Definition;
	}

	void KillActor(AActor* Target, AActor* Instigator)
	{
		FTDDamageSpec Lethal;
		Lethal.Amount = ArenaLethalDamage;
		Lethal.Instigator = Instigator;
		UTDCombatLibrary::TryApplyDamage(Target, Lethal);
	}

	void KillAliveMonsters(const ATDArenaEncounter& Encounter, AActor* Instigator)
	{
		TArray<ATDMonsterCharacter*> Monsters;
		Encounter.GetAliveMonsters(Monsters);
		for (ATDMonsterCharacter* Monster : Monsters)
		{
			KillActor(Monster, Instigator);
		}
	}

	FString DescribeEncounter(const ATDArenaEncounter& Encounter)
	{
		TArray<ATDMonsterCharacter*> Monsters;
		Encounter.GetAliveMonsters(Monsters);
		FString Description = FString::Printf(TEXT("state=%d wave=%d alive=%d killed=%d sealed=%d exit=%d"), static_cast<int32>(Encounter.GetEncounterState()), Encounter.GetCurrentWaveIndex(), Encounter.GetAliveMonsterCount(), Encounter.GetKilledMonsterCount(), Encounter.IsSealed() ? 1 : 0, Encounter.IsExitOpen() ? 1 : 0);
		for (const ATDMonsterCharacter* Monster : Monsters)
		{
			const UTDMonsterSpeciesAsset* Species = Monster->GetSpecies();
			Description += FString::Printf(TEXT(" %s"), Species ? *Species->DefinitionId.ToString() : TEXT("none"));
		}
		for (const FVector& SpawnLocation : Encounter.GetLastWaveSpawnLocations())
		{
			Description += FString::Printf(TEXT(" @(%.1f,%.1f)"), SpawnLocation.X, SpawnLocation.Y);
		}
		return Description;
	}

	bool IsGateClosed(const AActor* Gate)
	{
		return Gate && !Gate->IsHidden() && Gate->GetActorEnableCollision();
	}

	TArray<FString> RunFullClear(FAutomationTestBase& Test, FTDArenaTestWorld& Fixture)
	{
		TArray<FString> Trace;
		ATDArenaEncounter* Encounter = Fixture.SpawnEncounter(MakeThreeWaveDefinition(Fixture));
		AActor* SealGate = Fixture.SpawnGate(FVector(-1450.f, 0.f, 200.f));
		AActor* ExitGate = Fixture.SpawnGate(FVector(1450.f, 0.f, 200.f));
		Encounter->SetGateActors({ SealGate }, { ExitGate });
		APawn* Challenger = Fixture.SpawnChallenger(ArenaChallengerLocation);

		Test.TestTrue(TEXT("Transient species count as loaded at begin play"), Encounter->AreSpeciesLoaded());
		Test.TestFalse(TEXT("Seal gate starts open"), IsGateClosed(SealGate));
		Test.TestTrue(TEXT("Exit gate starts closed"), IsGateClosed(ExitGate));
		Test.TestTrue(TEXT("Challenger starts the encounter"), Encounter->TryStartEncounter(Challenger));
		Test.TestEqual(TEXT("Start waits for the first wave"), static_cast<int32>(Encounter->GetEncounterState()), static_cast<int32>(ETDArenaEncounterState::WaveDelay));
		Test.TestTrue(TEXT("Start closes the seal gate"), IsGateClosed(SealGate));
		Test.TestFalse(TEXT("A running encounter refuses a second start"), Encounter->TryStartEncounter(Challenger));
		Trace.Add(DescribeEncounter(*Encounter));

		Fixture.Tick(ArenaWaveDelaySeconds + 0.1f);
		Test.TestEqual(TEXT("First wave becomes active"), static_cast<int32>(Encounter->GetEncounterState()), static_cast<int32>(ETDArenaEncounterState::WaveActive));
		Test.TestEqual(TEXT("First wave spawns two minions"), Encounter->GetAliveMonsterCount(), 2);
		for (const FVector& SpawnLocation : Encounter->GetLastWaveSpawnLocations())
		{
			Test.TestTrue(TEXT("Minion spawns inside the arena"), FVector::Dist2D(SpawnLocation, Encounter->GetActorLocation()) <= Encounter->GetArenaRadiusCm());
			Test.TestTrue(TEXT("Minion spawns within engage reach"), FVector::Dist2D(SpawnLocation, ArenaChallengerLocation) <= 1500.0);
		}
		Trace.Add(DescribeEncounter(*Encounter));

		TArray<ATDMonsterCharacter*> FirstWave;
		Encounter->GetAliveMonsters(FirstWave);
		if (FirstWave.Num() == 2)
		{
			KillActor(FirstWave[0], Challenger);
			Fixture.Tick(ArenaStepSeconds);
			Test.TestEqual(TEXT("One kill leaves one minion"), Encounter->GetAliveMonsterCount(), 1);
			Test.TestEqual(TEXT("One kill is counted"), Encounter->GetKilledMonsterCount(), 1);
			Test.TestEqual(TEXT("Wave stays active with a survivor"), static_cast<int32>(Encounter->GetEncounterState()), static_cast<int32>(ETDArenaEncounterState::WaveActive));
			Trace.Add(DescribeEncounter(*Encounter));
		}

		KillAliveMonsters(*Encounter, Challenger);
		Fixture.Tick(ArenaStepSeconds);
		Test.TestEqual(TEXT("Clearing a wave waits for the next"), static_cast<int32>(Encounter->GetEncounterState()), static_cast<int32>(ETDArenaEncounterState::WaveDelay));
		Test.TestEqual(TEXT("Second wave is next"), Encounter->GetCurrentWaveIndex(), 1);
		Trace.Add(DescribeEncounter(*Encounter));

		Fixture.Tick(ArenaWaveDelaySeconds + 0.1f);
		Test.TestEqual(TEXT("Second wave spawns one minion"), Encounter->GetAliveMonsterCount(), 1);
		KillAliveMonsters(*Encounter, Challenger);
		Fixture.Tick(ArenaStepSeconds);
		Test.TestEqual(TEXT("Boss wave is next"), Encounter->GetCurrentWaveIndex(), 2);
		Fixture.Tick(ArenaWaveDelaySeconds + 0.1f);
		Test.TestEqual(TEXT("Boss wave is a boss fight"), static_cast<int32>(Encounter->GetEncounterState()), static_cast<int32>(ETDArenaEncounterState::BossActive));
		Test.TestEqual(TEXT("Boss wave spawns the boss"), Encounter->GetAliveMonsterCount(), 1);
		Test.TestNotNull(TEXT("Boss gets the health bar"), Encounter->GetBossHealthBarTarget());
		Test.TestTrue(TEXT("Boss spawns at the arena centre"), Encounter->GetLastWaveSpawnLocations().Num() == 1 && FVector::Dist2D(Encounter->GetLastWaveSpawnLocations()[0], Encounter->GetActorLocation()) < 1.0);
		Trace.Add(DescribeEncounter(*Encounter));

		KillAliveMonsters(*Encounter, Challenger);
		Fixture.Tick(ArenaStepSeconds);
		Test.TestEqual(TEXT("Killing the boss wins"), static_cast<int32>(Encounter->GetEncounterState()), static_cast<int32>(ETDArenaEncounterState::Victory));
		Test.TestEqual(TEXT("Every kill is counted"), Encounter->GetKilledMonsterCount(), 4);
		Test.TestNull(TEXT("Victory clears the boss bar target"), Encounter->GetBossHealthBarTarget());
		Test.TestFalse(TEXT("Victory opens the seal gate"), IsGateClosed(SealGate));
		Test.TestFalse(TEXT("Victory opens the exit gate"), IsGateClosed(ExitGate));
		Test.TestTrue(TEXT("Victory reports the exit open"), Encounter->IsExitOpen());
		Test.TestFalse(TEXT("A won encounter does not restart"), Encounter->TryStartEncounter(Challenger));
		Trace.Add(DescribeEncounter(*Encounter));
		return Trace;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDArenaWavesAdvanceOnKillsTest, "TDGame.Encounter.Arena.WavesAdvanceOnKills", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDArenaWavesAdvanceOnKillsTest::RunTest(const FString& Parameters)
{
	using namespace TDArenaEncounterTests;
	FTDArenaTestWorld Fixture;
	RunFullClear(*this, Fixture);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDArenaRunIsDeterministicTest, "TDGame.Encounter.Arena.RunIsDeterministic", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDArenaRunIsDeterministicTest::RunTest(const FString& Parameters)
{
	using namespace TDArenaEncounterTests;
	TArray<FString> FirstTrace;
	TArray<FString> SecondTrace;
	{
		FTDArenaTestWorld Fixture;
		FirstTrace = RunFullClear(*this, Fixture);
	}
	{
		FTDArenaTestWorld Fixture;
		SecondTrace = RunFullClear(*this, Fixture);
	}
	TestEqual(TEXT("Both runs record the same number of checkpoints"), SecondTrace.Num(), FirstTrace.Num());
	for (int32 Index = 0; Index < FMath::Min(FirstTrace.Num(), SecondTrace.Num()); ++Index)
	{
		TestEqual(FString::Printf(TEXT("Checkpoint %d matches"), Index), SecondTrace[Index], FirstTrace[Index]);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDArenaChallengerDeathResetsTest, "TDGame.Encounter.Arena.ChallengerDeathResetsEncounter", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDArenaChallengerDeathResetsTest::RunTest(const FString& Parameters)
{
	using namespace TDArenaEncounterTests;
	FTDArenaTestWorld Fixture;
	UTDArenaEncounterDefinition* Definition = NewObject<UTDArenaEncounterDefinition>(Fixture.World);
	Definition->FailureResetSeconds = ArenaFailureResetSeconds;
	Definition->Waves.Add(MakeWave({ MakeEntry(Fixture.MinionSpecies, 2, false) }));
	Definition->Waves.Add(MakeWave({ MakeEntry(Fixture.BossSpecies, 1, true) }));
	ATDArenaEncounter* Encounter = Fixture.SpawnEncounter(Definition);
	AActor* SealGate = Fixture.SpawnGate(FVector(-1450.f, 0.f, 200.f));
	AActor* ExitGate = Fixture.SpawnGate(FVector(1450.f, 0.f, 200.f));
	Encounter->SetGateActors({ SealGate }, { ExitGate });
	APawn* Challenger = Fixture.SpawnChallenger(ArenaChallengerLocation);

	TestTrue(TEXT("First attempt starts"), Encounter->TryStartEncounter(Challenger));
	Fixture.Tick(ArenaWaveDelaySeconds + 0.1f);
	KillAliveMonsters(*Encounter, Challenger);
	Fixture.Tick(ArenaWaveDelaySeconds + 0.2f);
	TestEqual(TEXT("First attempt reaches the boss"), static_cast<int32>(Encounter->GetEncounterState()), static_cast<int32>(ETDArenaEncounterState::BossActive));

	ATDMonsterCharacter* FirstBoss = Encounter->GetBossHealthBarTarget();
	UTDCombatComponent* FirstBossCombat = FirstBoss ? FirstBoss->GetCombatComponent() : nullptr;
	if (!TestNotNull(TEXT("First boss exists"), FirstBossCombat))
	{
		return false;
	}
	FTDDamageSpec Probe;
	Probe.Amount = ArenaBossProbeDamage;
	Probe.Instigator = Challenger;
	UTDCombatLibrary::TryApplyDamage(FirstBoss, Probe);
	TestTrue(TEXT("Probe damage lowers the first boss"), FirstBossCombat->GetCurrentHealth() < FirstBossCombat->GetMaxHealth());

	KillActor(Challenger, FirstBoss);
	TestEqual(TEXT("Challenger death fails the encounter at once"), static_cast<int32>(Encounter->GetEncounterState()), static_cast<int32>(ETDArenaEncounterState::Failed));
	TestNull(TEXT("Failure clears the boss bar target"), Encounter->GetBossHealthBarTarget());
	TestTrue(TEXT("Monsters stay until the reset delay ends"), Fixture.CountLiveMonsterActors() > 0);

	Fixture.Tick(ArenaFailureResetSeconds + 0.1f);
	TestEqual(TEXT("Reset returns to idle"), static_cast<int32>(Encounter->GetEncounterState()), static_cast<int32>(ETDArenaEncounterState::Idle));
	TestEqual(TEXT("Reset forgets the wave"), Encounter->GetCurrentWaveIndex(), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("Reset leaves no tracked monster"), Encounter->GetAliveMonsterCount(), 0);
	TestEqual(TEXT("Reset clears every monster actor in the arena"), Fixture.CountLiveMonsterActors(), 0);
	TestFalse(TEXT("Reset opens the seal gate"), IsGateClosed(SealGate));
	TestTrue(TEXT("Reset keeps the exit closed"), IsGateClosed(ExitGate));
	TestFalse(TEXT("Reset does not open the exit"), Encounter->IsExitOpen());

	APawn* ReturningChallenger = Fixture.SpawnChallenger(ArenaChallengerLocation);
	TestTrue(TEXT("Second attempt starts"), Encounter->TryStartEncounter(ReturningChallenger));
	TestEqual(TEXT("Attempts are counted"), Encounter->GetAttemptCount(), 2);
	TestEqual(TEXT("Second attempt starts with no kills"), Encounter->GetKilledMonsterCount(), 0);
	TestEqual(TEXT("Second attempt starts from the first wave"), Encounter->GetCurrentWaveIndex(), 0);
	Fixture.Tick(ArenaWaveDelaySeconds + 0.1f);
	TestEqual(TEXT("Second attempt respawns the first wave"), Encounter->GetAliveMonsterCount(), 2);
	KillAliveMonsters(*Encounter, ReturningChallenger);
	Fixture.Tick(ArenaWaveDelaySeconds + 0.2f);

	ATDMonsterCharacter* SecondBoss = Encounter->GetBossHealthBarTarget();
	UTDCombatComponent* SecondBossCombat = SecondBoss ? SecondBoss->GetCombatComponent() : nullptr;
	if (!TestNotNull(TEXT("Second boss exists"), SecondBossCombat))
	{
		return false;
	}
	TestTrue(TEXT("Second boss is a fresh actor"), SecondBoss != FirstBoss);
	TestEqual(TEXT("Second boss starts at full health"), SecondBossCombat->GetCurrentHealth(), SecondBossCombat->GetMaxHealth(), 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDArenaSpawnLocationsTest, "TDGame.Encounter.Arena.SpawnLocationsStayInsideArena", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDArenaSpawnLocationsTest::RunTest(const FString& Parameters)
{
	constexpr float ArenaRadiusCm = 1250.f;
	const FVector ArenaCenter(0.f, 0.f, 0.f);
	const FVector ChallengerLocation(-1100.f, 0.f, 0.f);

	FTDArenaSpawnEntry ArcEntry;
	ArcEntry.Count = 5;
	ArcEntry.DistanceCm = 800.f;
	ArcEntry.SpreadDegrees = 120.f;
	TArray<FVector> FirstLocations;
	TArray<FVector> SecondLocations;
	ATDArenaEncounter::ComputeSpawnLocations(ArcEntry, ArenaCenter, ArenaRadiusCm, ChallengerLocation, FirstLocations);
	ATDArenaEncounter::ComputeSpawnLocations(ArcEntry, ArenaCenter, ArenaRadiusCm, ChallengerLocation, SecondLocations);
	TestEqual(TEXT("Arc spawns every monster"), FirstLocations.Num(), 5);
	TestTrue(TEXT("Same inputs give the same arc"), FirstLocations == SecondLocations);
	for (const FVector& Location : FirstLocations)
	{
		TestTrue(TEXT("Arc spawn stays inside the arena"), FVector::Dist2D(Location, ArenaCenter) <= ArenaRadiusCm + 0.1f);
		TestTrue(TEXT("Arc spawn stays within engage reach of the challenger"), FVector::Dist2D(Location, ChallengerLocation) <= 1500.f);
	}
	if (FirstLocations.Num() == 5)
	{
		TestEqual(TEXT("Arc is mirrored across the challenger line"), FirstLocations[0].Y, -FirstLocations[4].Y, 0.1);
		TestTrue(TEXT("Middle spawn sits toward the arena centre"), FirstLocations[2].X > ChallengerLocation.X);
	}

	FTDArenaSpawnEntry CenterEntry;
	CenterEntry.Count = 1;
	CenterEntry.Anchor = ETDArenaSpawnAnchor::ArenaCenter;
	CenterEntry.DistanceCm = 0.f;
	TArray<FVector> CenterLocations;
	ATDArenaEncounter::ComputeSpawnLocations(CenterEntry, ArenaCenter, ArenaRadiusCm, ChallengerLocation, CenterLocations);
	TestTrue(TEXT("Centre anchor with no distance spawns at the centre"), CenterLocations.Num() == 1 && CenterLocations[0].Equals(ArenaCenter, 0.1f));

	FTDArenaSpawnEntry RingEntry;
	RingEntry.Count = 4;
	RingEntry.Anchor = ETDArenaSpawnAnchor::ArenaCenter;
	RingEntry.DistanceCm = 600.f;
	RingEntry.SpreadDegrees = 360.f;
	TArray<FVector> RingLocations;
	ATDArenaEncounter::ComputeSpawnLocations(RingEntry, ArenaCenter, ArenaRadiusCm, ChallengerLocation, RingLocations);
	TestEqual(TEXT("Full ring spawns every monster"), RingLocations.Num(), 4);
	if (RingLocations.Num() == 4)
	{
		TestEqual(TEXT("Full ring spaces neighbours evenly"), FVector::Dist2D(RingLocations[0], RingLocations[1]), FVector::Dist2D(RingLocations[1], RingLocations[2]), 0.5);
		TestFalse(TEXT("Full ring does not stack the first and last"), RingLocations[0].Equals(RingLocations[3], 1.f));
	}

	FTDArenaSpawnEntry FarEntry;
	FarEntry.Count = 1;
	FarEntry.DistanceCm = 5000.f;
	TArray<FVector> FarLocations;
	ATDArenaEncounter::ComputeSpawnLocations(FarEntry, ArenaCenter, ArenaRadiusCm, ChallengerLocation, FarLocations);
	TestTrue(TEXT("Far spawn is clamped onto the arena edge"), FarLocations.Num() == 1 && FMath::IsNearlyEqual(FVector::Dist2D(FarLocations[0], ArenaCenter), ArenaRadiusCm, 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDArenaEntryZoneTest, "TDGame.Encounter.Arena.EntryZoneAndCheckpoint", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDArenaEntryZoneTest::RunTest(const FString& Parameters)
{
	using namespace TDArenaEncounterTests;
	FTDArenaTestWorld Fixture;
	ATDArenaEncounter* Encounter = Fixture.SpawnEncounter(MakeThreeWaveDefinition(Fixture));
	const float ArenaRadiusCm = Encounter->GetArenaRadiusCm();
	TestTrue(TEXT("Entry zone covers the inside of the entrance"), Encounter->IsLocationInEntryZone(FVector(-ArenaRadiusCm + 350.f, 0.f, 100.f)));
	TestFalse(TEXT("Arena centre is outside the entry zone"), Encounter->IsLocationInEntryZone(FVector(0.f, 0.f, 100.f)));
	const FVector Checkpoint = Encounter->GetCheckpointTransform().GetLocation();
	TestTrue(TEXT("Checkpoint lies outside the arena"), FVector::Dist2D(Checkpoint, Encounter->GetActorLocation()) > ArenaRadiusCm);
	TestFalse(TEXT("Checkpoint lies outside the entry zone"), Encounter->IsLocationInEntryZone(Checkpoint));
	TestFalse(TEXT("Missing challenger cannot start"), Encounter->TryStartEncounter(nullptr));
	TestEqual(TEXT("Encounter idles without a challenger"), static_cast<int32>(Encounter->GetEncounterState()), static_cast<int32>(ETDArenaEncounterState::Idle));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDArenaVanishedChallengerFailsTest, "TDGame.Encounter.Arena.VanishedChallengerFailsEncounter", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDArenaVanishedChallengerFailsTest::RunTest(const FString& Parameters)
{
	using namespace TDArenaEncounterTests;
	FTDArenaTestWorld Fixture;
	ATDArenaEncounter* Encounter = Fixture.SpawnEncounter(MakeThreeWaveDefinition(Fixture));
	APawn* Challenger = Fixture.SpawnChallenger(ArenaChallengerLocation);
	TestTrue(TEXT("Challenger starts the encounter"), Encounter->TryStartEncounter(Challenger));
	Fixture.Tick(ArenaWaveDelaySeconds + 0.1f);
	TestEqual(TEXT("First wave is running"), static_cast<int32>(Encounter->GetEncounterState()), static_cast<int32>(ETDArenaEncounterState::WaveActive));
	Challenger->Destroy();
	Fixture.Tick(ArenaStepSeconds);
	TestEqual(TEXT("A challenger removed without dying still fails the encounter"), static_cast<int32>(Encounter->GetEncounterState()), static_cast<int32>(ETDArenaEncounterState::Failed));
	Fixture.Tick(ArenaFailureResetSeconds + 0.1f);
	TestEqual(TEXT("The failed encounter resets"), static_cast<int32>(Encounter->GetEncounterState()), static_cast<int32>(ETDArenaEncounterState::Idle));
	TestEqual(TEXT("The reset clears the wave"), Fixture.CountLiveMonsterActors(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDArenaEmptyWaveFailsTest, "TDGame.Encounter.Arena.EmptyWaveFailsInsteadOfWinning", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDArenaEmptyWaveFailsTest::RunTest(const FString& Parameters)
{
	using namespace TDArenaEncounterTests;
	FTDArenaTestWorld Fixture;
	UTDArenaEncounterDefinition* Definition = NewObject<UTDArenaEncounterDefinition>(Fixture.World);
	Definition->EncounterId = TEXT("EmptyWaveArena");
	Definition->Waves.Add(MakeWave({ MakeEntry(Fixture.MinionSpecies, 2, false) }));
	ATDArenaEncounter* Encounter = Fixture.SpawnEncounter(Definition);
	APawn* Challenger = Fixture.SpawnChallenger(ArenaChallengerLocation);
	TestTrue(TEXT("Challenger starts the encounter"), Encounter->TryStartEncounter(Challenger));
	AddExpectedError(TEXT("failing the attempt instead of granting a free clear"), EAutomationExpectedErrorFlags::Contains, 1);
	Fixture.MinionSpecies->MarkAsGarbage();
	Fixture.Tick(ArenaWaveDelaySeconds + 0.1f);
	TestEqual(TEXT("A wave whose species vanished spawns nothing"), Encounter->GetAliveMonsterCount(), 0);
	TestEqual(TEXT("A wave that spawned nothing fails instead of winning"), static_cast<int32>(Encounter->GetEncounterState()), static_cast<int32>(ETDArenaEncounterState::Failed));
	TestFalse(TEXT("A failed empty wave never opens the exit"), Encounter->IsExitOpen());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDArenaBossDeathClearsSummonsTest, "TDGame.Encounter.Arena.BossDeathClearsLeftoverSummons", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDArenaBossDeathClearsSummonsTest::RunTest(const FString& Parameters)
{
	using namespace TDArenaEncounterTests;
	FTDArenaTestWorld Fixture;
	UTDArenaEncounterDefinition* Definition = NewObject<UTDArenaEncounterDefinition>(Fixture.World);
	Definition->Waves.Add(MakeWave({ MakeEntry(Fixture.BossSpecies, 1, true) }));
	Definition->Waves.Add(MakeWave({ MakeEntry(Fixture.MinionSpecies, 1, false) }));
	ATDArenaEncounter* Encounter = Fixture.SpawnEncounter(Definition);
	APawn* Challenger = Fixture.SpawnChallenger(ArenaChallengerLocation);
	TestTrue(TEXT("Challenger starts the encounter"), Encounter->TryStartEncounter(Challenger));
	Fixture.Tick(ArenaWaveDelaySeconds + 0.1f);
	TestEqual(TEXT("The boss wave is running"), static_cast<int32>(Encounter->GetEncounterState()), static_cast<int32>(ETDArenaEncounterState::BossActive));

	const FTransform SummonTransform(FRotator::ZeroRotator, FVector(300.f, 300.f, 100.f));
	ATDMonsterCharacter* Summon = Fixture.World->SpawnActorDeferred<ATDMonsterCharacter>(ATDMonsterCharacter::StaticClass(), SummonTransform);
	Summon->SetSpecies(Fixture.MinionSpecies);
	Summon->FinishSpawning(SummonTransform);
	TestTrue(TEXT("The untracked summon starts alive"), UTDCombatLibrary::IsActorAlive(Summon));

	KillAliveMonsters(*Encounter, Challenger);
	Fixture.Tick(ArenaStepSeconds);
	TestFalse(TEXT("Clearing the boss wave kills a summon the encounter did not spawn"), UTDCombatLibrary::IsActorAlive(Summon));
	TestEqual(TEXT("The summon does not count as an encounter kill"), Encounter->GetKilledMonsterCount(), 1);
	TestEqual(TEXT("The next wave follows the boss wave"), Encounter->GetCurrentWaveIndex(), 1);
	return true;
}
