#if WITH_DEV_AUTOMATION_TESTS

#include "Characters/TDMonsterCharacter.h"
#include "Combat/Damage/TDDamageDefinition.h"
#include "Combat/Damage/TDDamageEntity.h"
#include "Combat/TDCombatComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "MonsterAI/TDMonsterDefinitionLibrary.h"
#include "MonsterAI/TDMonsterSpeciesAsset.h"
#include "MonsterAI/TDMonsterThinkSubsystem.h"

namespace TDMidBossTests
{
	constexpr float StepSeconds = 1.f / 64.f;
	constexpr float FloorHalfThickness = 50.f;
	constexpr float BodyHeight = 95.f;
	constexpr float TargetMaxHealth = 5000.f;
	constexpr float SettleSeconds = 0.5f;
	constexpr float PhaseTimeoutSeconds = 5.f;
	constexpr float GriffonFlightAltitudeCm = 120.f;
	constexpr float AirborneAltitudeCm = GriffonFlightAltitudeCm * 0.8f;
	constexpr float GroundedAltitudeCm = 40.f;
	constexpr float LandingImpactWindupSeconds = 1.2f;
	const FName CentaurId(TEXT("Centaur_Hunter"));
	const FName GriffonId(TEXT("Griffon_Alpha"));
	const TCHAR* HoundSpeciesPath = TEXT("/Game/MonsterAI/Species/DA_TDMonster_Barghest.DA_TDMonster_Barghest");
	const TCHAR* CentaurSpeciesPath = TEXT("/Game/MonsterAI/Species/DA_TDMonster_CentaurHunter.DA_TDMonster_CentaurHunter");

	int32 SecondsToSteps(const float Seconds)
	{
		return FMath::Max(1, FMath::RoundToInt(Seconds / StepSeconds));
	}

	struct FMidBossWorld
	{
		FMidBossWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDMidBossTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
			SpawnBlockingBox(FVector(0.f, 0.f, -FloorHalfThickness), FVector(6000.f, 6000.f, FloorHalfThickness));
		}

		~FMidBossWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		void SpawnBlockingBox(const FVector& Center, const FVector& Extent)
		{
			AActor* Blocker = World->SpawnActor<AActor>();
			UBoxComponent* Box = NewObject<UBoxComponent>(Blocker);
			Blocker->AddInstanceComponent(Box);
			Blocker->SetRootComponent(Box);
			Box->SetBoxExtent(Extent);
			Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Box->RegisterComponent();
			Blocker->SetActorLocation(Center);
		}

		UTDCombatComponent* SpawnTarget(const FVector& Location)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Actor->AddInstanceComponent(Sphere);
			Actor->SetRootComponent(Sphere);
			Sphere->SetSphereRadius(30.f);
			Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Sphere->SetCollisionObjectType(ECC_Pawn);
			Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
			Sphere->RegisterComponent();
			Actor->SetActorLocation(Location + FVector(0.f, 0.f, BodyHeight));
			UTDCombatComponent* Combatant = NewObject<UTDCombatComponent>(Actor);
			Actor->AddInstanceComponent(Combatant);
			FTDCombatStats Stats;
			Stats.TeamId = 1;
			Stats.BaseMaxHealth = TargetMaxHealth;
			Combatant->SetStats(Stats);
			Combatant->RegisterComponent();
			return Combatant;
		}

		ATDMonsterCharacter* SpawnMonster(const FName DefinitionId, const FVector& Location)
		{
			UTDMonsterSpeciesAsset* Species = NewObject<UTDMonsterSpeciesAsset>(World);
			Species->DefinitionId = DefinitionId;
			return SpawnSpecies(Species, Location);
		}

		ATDMonsterCharacter* SpawnSpecies(UTDMonsterSpeciesAsset* Species, const FVector& Location)
		{
			const FTransform SpawnTransform(FRotator::ZeroRotator, Location + FVector(0.f, 0.f, BodyHeight));
			ATDMonsterCharacter* Monster = World->SpawnActorDeferred<ATDMonsterCharacter>(ATDMonsterCharacter::StaticClass(), SpawnTransform);
			Monster->SetSpecies(Species);
			Monster->FinishSpawning(SpawnTransform);
			return Monster;
		}

		void Step()
		{
			FlushAsyncLoading();
			++GFrameCounter;
			World->Tick(LEVELTICK_All, StepSeconds);
		}

		void Tick(const float Duration)
		{
			for (int32 StepIndex = SecondsToSteps(Duration); StepIndex > 0; --StepIndex)
			{
				Step();
			}
		}

		UTDMonsterThinkSubsystem* GetThinkSubsystem() const
		{
			return World->GetSubsystem<UTDMonsterThinkSubsystem>();
		}

		UWorld* World = nullptr;
	};

	FString ReadField(const FString& Line, const TCHAR* Key)
	{
		const FString Padded = FString(TEXT(" ")) + Line;
		const FString Token = FString(TEXT(" ")) + Key + TEXT("=");
		const int32 TokenStart = Padded.Find(Token, ESearchCase::CaseSensitive);
		if (TokenStart == INDEX_NONE)
		{
			return FString();
		}
		const int32 ValueStart = TokenStart + Token.Len();
		const int32 SpaceIndex = Padded.Find(TEXT(" "), ESearchCase::CaseSensitive, ESearchDir::FromStart, ValueStart);
		const int32 ValueEnd = SpaceIndex == INDEX_NONE ? Padded.Len() : SpaceIndex;
		return Padded.Mid(ValueStart, ValueEnd - ValueStart);
	}

	FString FindLine(const UTDMonsterThinkSubsystem& ThinkSubsystem, const AActor* Monster)
	{
		const FString ActorName = Monster ? Monster->GetName() : FString();
		for (const FString& Line : ThinkSubsystem.GetMonsterDebugLines())
		{
			if (ReadField(Line, TEXT("actor")) == ActorName)
			{
				return Line;
			}
		}
		return FString();
	}

	int32 ReadPhase(const FMidBossWorld& Fixture, const AActor* Monster)
	{
		return FCString::Atoi(*ReadField(FindLine(*Fixture.GetThinkSubsystem(), Monster), TEXT("phase")));
	}

	void HitMonster(ATDMonsterCharacter* Monster, UTDCombatComponent* Source, const float Damage)
	{
		if (!Monster || !Source || !Monster->GetCombatComponent()->IsAlive())
		{
			return;
		}
		FTDDamageContext Context;
		Context.Caster = Source->GetOwner();
		Context.Stats = Source->GetStats();
		Monster->GetCombatComponent()->ReceiveDamage(Damage, ETDDamageElement::Physical, false, Context);
	}

	void DamageToHealthRatio(ATDMonsterCharacter* Monster, UTDCombatComponent* Source, const float Ratio)
	{
		UTDCombatComponent* Combatant = Monster ? Monster->GetCombatComponent() : nullptr;
		if (!Combatant)
		{
			return;
		}
		const float ChunkDamage = Combatant->GetMaxHealth() * 0.08f;
		for (int32 HitIndex = 0; HitIndex < 64 && Combatant->GetCurrentHealth() > Combatant->GetMaxHealth() * Ratio; ++HitIndex)
		{
			HitMonster(Monster, Source, ChunkDamage);
		}
	}

	void IgnoreEngineInjectionEnsures(FAutomationTestBase& Test)
	{
		Test.AddExpectedError(TEXT("TimelineTrait.GetState(Context, ChildState)"), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT("Event.SerialNumber != 0"), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT("=== Handled ensure: ==="), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT("Stack:"), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT("[Callstack]"), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT(""), EAutomationExpectedErrorFlags::Exact, -1, false);
	}

	bool IsStunnedState(const FString& State)
	{
		return State == TEXT("Stagger");
	}

	TArray<ATDMonsterCharacter*> CollectOtherLiveMonsters(UWorld& World, const AActor* Boss)
	{
		TArray<ATDMonsterCharacter*> Monsters;
		for (TActorIterator<ATDMonsterCharacter> It(&World); It; ++It)
		{
			if (*It != Boss && It->GetCombatComponent() && It->GetCombatComponent()->IsAlive())
			{
				Monsters.Add(*It);
			}
		}
		return Monsters;
	}

	void KillOtherMonsters(UWorld& World, const AActor* Boss, UTDCombatComponent* Source)
	{
		for (ATDMonsterCharacter* Monster : CollectOtherLiveMonsters(World, Boss))
		{
			HitMonster(Monster, Source, 100000.f);
		}
	}

	struct FEntityTracker
	{
		TArray<FName> CollectNewEntities(UWorld& World, const AActor* Owner)
		{
			TArray<FName> NewDefinitions;
			for (TActorIterator<ATDDamageEntity> It(&World); It; ++It)
			{
				if (It->GetOwner() != Owner || SeenEntities.Contains(*It))
				{
					continue;
				}
				SeenEntities.Add(*It);
				const UTDDamageDefinition* Definition = It->GetDefinition();
				NewDefinitions.Add(Definition ? Definition->GetFName() : NAME_None);
			}
			return NewDefinitions;
		}

		TSet<TWeakObjectPtr<AActor>> SeenEntities;
	};

	struct FBossSample
	{
		float Seconds = 0.f;
		FString State;
		FString Action;
		int32 Phase = 0;
		FVector Location = FVector::ZeroVector;
		bool bIsFlying = false;
		float TargetHealth = 0.f;
		TArray<FName> NewEntities;
	};

	FBossSample SampleBoss(const FMidBossWorld& Fixture, const ATDMonsterCharacter* Boss, const UTDCombatComponent* Target, FEntityTracker& Tracker, const float Seconds)
	{
		FBossSample Sample;
		Sample.Seconds = Seconds;
		const FString Line = FindLine(*Fixture.GetThinkSubsystem(), Boss);
		Sample.State = ReadField(Line, TEXT("state"));
		Sample.Action = ReadField(Line, TEXT("action"));
		Sample.Phase = FCString::Atoi(*ReadField(Line, TEXT("phase")));
		Sample.Location = Boss->GetActorLocation();
		Sample.bIsFlying = Boss->GetMoverComponent() && Boss->GetMoverComponent()->IsFlying();
		Sample.TargetHealth = Target ? Target->GetCurrentHealth() : 0.f;
		Sample.NewEntities = Tracker.CollectNewEntities(*Fixture.World, Boss);
		return Sample;
	}

	float FindLongestRunSeconds(const TArray<FBossSample>& Samples, TFunctionRef<bool(const FBossSample&)> Predicate)
	{
		int32 LongestSteps = 0;
		int32 CurrentSteps = 0;
		for (const FBossSample& Sample : Samples)
		{
			CurrentSteps = Predicate(Sample) ? CurrentSteps + 1 : 0;
			LongestSteps = FMath::Max(LongestSteps, CurrentSteps);
		}
		return LongestSteps * StepSeconds;
	}

	int32 FindFirstSampleIndex(const TArray<FBossSample>& Samples, const int32 StartIndex, TFunctionRef<bool(const FBossSample&)> Predicate)
	{
		for (int32 Index = FMath::Max(0, StartIndex); Index < Samples.Num(); ++Index)
		{
			if (Predicate(Samples[Index]))
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	FString DescribeTimeline(const TArray<FBossSample>& Samples, const float GroundZ)
	{
		FString Text;
		for (int32 Index = 0; Index < Samples.Num(); Index += 8)
		{
			const FBossSample& Sample = Samples[Index];
			Text += FString::Printf(TEXT("%.2f:%s/%s/%s/%.0f%s "), Sample.Seconds, *Sample.State, *Sample.Action, Sample.bIsFlying ? TEXT("fly") : TEXT("gnd"), Sample.Location.Z - GroundZ, Sample.NewEntities.IsEmpty() ? TEXT("") : TEXT("*"));
		}
		return Text;
	}

	bool HasNewEntity(const FBossSample& Sample)
	{
		return !Sample.NewEntities.IsEmpty();
	}

	bool StepUntilPhase(FMidBossWorld& Fixture, const ATDMonsterCharacter* Boss, const int32 Phase, const float TimeoutSeconds)
	{
		for (int32 StepIndex = SecondsToSteps(TimeoutSeconds); StepIndex > 0; --StepIndex)
		{
			Fixture.Step();
			if (ReadPhase(Fixture, Boss) >= Phase)
			{
				return true;
			}
		}
		return false;
	}

	bool StepUntilAirborne(FMidBossWorld& Fixture, const ATDMonsterCharacter* Boss, const float GroundZ, const float TimeoutSeconds, float& OutSeconds)
	{
		for (int32 StepIndex = 0; StepIndex < SecondsToSteps(TimeoutSeconds); ++StepIndex)
		{
			Fixture.Step();
			if (Boss->GetMoverComponent()->IsFlying() && Boss->GetActorLocation().Z - GroundZ >= AirborneAltitudeCm)
			{
				OutSeconds = (StepIndex + 1) * StepSeconds;
				return true;
			}
		}
		return false;
	}

	uint64 HashText(const uint64 Seed, const FString& Text)
	{
		uint64 Hash = Seed;
		for (const TCHAR Character : Text)
		{
			Hash = (Hash ^ static_cast<uint64>(Character)) * 1099511628211ull;
		}
		return Hash;
	}

	uint64 RunScriptedFight(const FName BossId, const float FightSeconds, const float PhaseDamageSeconds, const float PhaseRatio, int32& OutSampleCount, int32& OutHighestPhase)
	{
		FMidBossWorld Fixture;
		UTDCombatComponent* Target = Fixture.SpawnTarget(FVector(500.f, 0.f, 0.f));
		ATDMonsterCharacter* Boss = Fixture.SpawnMonster(BossId, FVector::ZeroVector);
		uint64 Hash = 1469598103934665603ull;
		OutSampleCount = 0;
		OutHighestPhase = 0;
		if (!Target || !Boss)
		{
			return Hash;
		}
		const int32 PhaseDamageStep = SecondsToSteps(PhaseDamageSeconds);
		const int32 StepCount = SecondsToSteps(FightSeconds);
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			if (StepIndex == PhaseDamageStep)
			{
				DamageToHealthRatio(Boss, Target, PhaseRatio);
			}
			Fixture.Step();
			TArray<FString> Lines = Fixture.GetThinkSubsystem()->GetMonsterDebugLines();
			for (FString& Line : Lines)
			{
				const FString ActorField = FString(TEXT(" actor=")) + ReadField(Line, TEXT("actor"));
				Line.ReplaceInline(*ActorField, TEXT(""), ESearchCase::CaseSensitive);
			}
			Lines.Sort();
			for (const FString& Line : Lines)
			{
				Hash = HashText(Hash, Line);
			}
			OutHighestPhase = FMath::Max(OutHighestPhase, ReadPhase(Fixture, Boss));
			const FVector Location = Boss->GetActorLocation();
			Hash = HashText(Hash, FString::Printf(TEXT("%d|%d|%d|%.0f"), FMath::RoundToInt(Location.X), FMath::RoundToInt(Location.Y), FMath::RoundToInt(Location.Z), Target->GetCurrentHealth()));
			++OutSampleCount;
		}
		return Hash;
	}
}

using namespace TDMidBossTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMidBossFixtureWalksTest, "TDGame.MonsterAI.MidBoss.FixtureWalksOnFloor", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMidBossFixtureWalksTest::RunTest(const FString& Parameters)
{
	FMidBossWorld Fixture;
	UTDCombatComponent* Target = Fixture.SpawnTarget(FVector(700.f, 0.f, 0.f));
	ATDMonsterCharacter* Goblin = Fixture.SpawnMonster(TEXT("Goblin_Melee"), FVector::ZeroVector);
	if (!TestNotNull(TEXT("Target exists"), Target) || !TestNotNull(TEXT("Goblin exists"), Goblin))
	{
		return false;
	}
	Fixture.Tick(SettleSeconds);
	const FVector Settled = Goblin->GetActorLocation();
	Fixture.Tick(2.5f);
	const FVector Moved = Goblin->GetActorLocation();
	TestTrue(TEXT("A walking monster closes in on the target across the floor"), Moved.X - Settled.X > 200.f);
	TestTrue(TEXT("A walking monster stays on the floor"), FMath::Abs(Moved.Z - Settled.Z) < 10.f);
	TestFalse(TEXT("A walking monster is not flying"), Goblin->GetMoverComponent()->IsFlying());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMidBossDefinitionsTest, "TDGame.MonsterAI.MidBoss.DefinitionsResolve", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMidBossDefinitionsTest::RunTest(const FString& Parameters)
{
	UTDMonsterDefinitionLibrary* Library = UTDMonsterDefinitionLibrary::Get();
	if (!TestNotNull(TEXT("Definition library exists"), Library))
	{
		return false;
	}
	struct FExpectedBoss
	{
		FName Id;
		float MaxHealth;
		float PhaseHealthRatio;
	};
	const FExpectedBoss ExpectedBosses[] = { { CentaurId, 3200.f, 0.5f }, { GriffonId, 3600.f, 0.6f } };
	for (const FExpectedBoss& Expected : ExpectedBosses)
	{
		const TSharedPtr<const FTDResolvedMonsterDefinition> Definition = Library->FindDefinition(Expected.Id);
		const FString Name = Expected.Id.ToString();
		if (!TestTrue(FString::Printf(TEXT("%s resolves"), *Name), Definition.IsValid()))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s is a boss"), *Name), Definition->bIsBoss);
		TestEqual(FString::Printf(TEXT("%s max health"), *Name), Definition->Stats.MaxHealth, Expected.MaxHealth);
		TestEqual(FString::Printf(TEXT("%s has one phase"), *Name), Definition->Phases.Num(), 1);
		TestEqual(FString::Printf(TEXT("%s has two action lists"), *Name), Definition->ActionsByPhase.Num(), 2);
		if (Definition->Phases.Num() == 1)
		{
			TestEqual(FString::Printf(TEXT("%s phase threshold"), *Name), Definition->Phases[0].Value, Expected.PhaseHealthRatio);
			TestTrue(FString::Printf(TEXT("%s phase plays an on_enter sequence"), *Name), Definition->Phases[0].OnEnterSequenceIndex != INDEX_NONE);
		}
		TestTrue(FString::Printf(TEXT("%s binds every ability to a spell"), *Name),
			!Definition->Abilities.IsEmpty() && !Definition->Abilities.ContainsByPredicate([](const FTDResolvedAbility& Ability) { return Ability.Spell == nullptr; }));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMidBossCentaurBandTest, "TDGame.MonsterAI.MidBoss.CentaurHoldsBandAndShoots", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMidBossCentaurBandTest::RunTest(const FString& Parameters)
{
	FMidBossWorld Fixture;
	UTDCombatComponent* Target = Fixture.SpawnTarget(FVector(300.f, 0.f, 0.f));
	ATDMonsterCharacter* Centaur = Fixture.SpawnMonster(CentaurId, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Target exists"), Target) || !TestNotNull(TEXT("Centaur exists"), Centaur) || !TestTrue(TEXT("Centaur has a brain"), Centaur->HasMonsterBrain()))
	{
		return false;
	}
	Fixture.Tick(SettleSeconds);
	const float StartHealth = Target->GetCurrentHealth();
	TArray<float> LateDistances;
	const int32 StepCount = SecondsToSteps(10.f);
	for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
	{
		Fixture.Step();
		if (StepIndex >= SecondsToSteps(4.f))
		{
			LateDistances.Add(FVector::Dist2D(Centaur->GetActorLocation(), Target->GetOwner()->GetActorLocation()));
		}
	}
	LateDistances.Sort();
	const float MedianDistance = LateDistances[LateDistances.Num() / 2];
	TestTrue(TEXT("Arrows land on the target in the bow phase"), Target->GetCurrentHealth() < StartHealth);
	TestTrue(FString::Printf(TEXT("The centaur backs out to the 600 to 900 band (median %.0f)"), MedianDistance), MedianDistance >= 550.f && MedianDistance <= 950.f);
	TestTrue(FString::Printf(TEXT("The centaur stays out of melee once it has its band (closest %.0f)"), LateDistances[0]), LateDistances[0] >= 400.f);
	TestEqual(TEXT("The bow phase holds above half health"), ReadPhase(Fixture, Centaur), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMidBossCentaurSummonTest, "TDGame.MonsterAI.MidBoss.CentaurPhaseSummonsTwoHounds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMidBossCentaurSummonTest::RunTest(const FString& Parameters)
{
	IgnoreEngineInjectionEnsures(*this);
	FMidBossWorld Fixture;
	UTDCombatComponent* Target = Fixture.SpawnTarget(FVector(800.f, 0.f, 0.f));
	ATDMonsterCharacter* Centaur = Fixture.SpawnMonster(CentaurId, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Target exists"), Target) || !TestNotNull(TEXT("Centaur exists"), Centaur))
	{
		return false;
	}
	Fixture.Tick(SettleSeconds);
	TestEqual(TEXT("No hounds before the sword phase"), CollectOtherLiveMonsters(*Fixture.World, Centaur).Num(), 0);
	DamageToHealthRatio(Centaur, Target, 0.49f);
	if (!TestTrue(TEXT("The sword phase starts below half health"), StepUntilPhase(Fixture, Centaur, 1, PhaseTimeoutSeconds)))
	{
		return false;
	}
	Fixture.Tick(3.f);
	const TArray<ATDMonsterCharacter*> Hounds = CollectOtherLiveMonsters(*Fixture.World, Centaur);
	TestEqual(TEXT("The phase change summons two hounds"), Hounds.Num(), 2);
	for (const ATDMonsterCharacter* Hound : Hounds)
	{
		const FString Definition = ReadField(FindLine(*Fixture.GetThinkSubsystem(), Hound), TEXT("def"));
		TestTrue(FString::Printf(TEXT("A summoned hound is a Barghest (%s)"), *Definition), Definition.StartsWith(TEXT("Barghest")));
		TestTrue(TEXT("A summoned hound joins the fight"), ReadField(FindLine(*Fixture.GetThinkSubsystem(), Hound), TEXT("engaged")) == TEXT("1"));
	}
	Fixture.Tick(6.f);
	TestEqual(TEXT("The summon happens once per fight"), CollectOtherLiveMonsters(*Fixture.World, Centaur).Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMidBossCentaurBreakTest, "TDGame.MonsterAI.MidBoss.CentaurDrawSwordBreaksIntoStun", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMidBossCentaurBreakTest::RunTest(const FString& Parameters)
{
	IgnoreEngineInjectionEnsures(*this);
	float LongestStunSeconds[2] = { 0.f, 0.f };
	float TargetLossDuringStun = 0.f;
	bool bControlAttacked = false;
	for (int32 RunIndex = 0; RunIndex < 2; ++RunIndex)
	{
		const bool bBreaksChannel = RunIndex == 0;
		FMidBossWorld Fixture;
		UTDCombatComponent* Target = Fixture.SpawnTarget(FVector(800.f, 0.f, 0.f));
		ATDMonsterCharacter* Centaur = Fixture.SpawnMonster(CentaurId, FVector::ZeroVector);
		if (!TestNotNull(TEXT("Target exists"), Target) || !TestNotNull(TEXT("Centaur exists"), Centaur))
		{
			return false;
		}
		Fixture.Tick(SettleSeconds);
		DamageToHealthRatio(Centaur, Target, 0.49f);
		if (!TestTrue(TEXT("The sword phase starts"), StepUntilPhase(Fixture, Centaur, 1, PhaseTimeoutSeconds)))
		{
			return false;
		}
		FEntityTracker Tracker;
		TArray<FBossSample> Samples;
		bool bHasStunned = false;
		for (int32 StepIndex = 0; StepIndex < SecondsToSteps(6.f); ++StepIndex)
		{
			if (bBreaksChannel && !bHasStunned && StepIndex < SecondsToSteps(3.f) && StepIndex % 12 == 0)
			{
				HitMonster(Centaur, Target, 40.f);
			}
			Fixture.Step();
			KillOtherMonsters(*Fixture.World, Centaur, Target);
			Samples.Add(SampleBoss(Fixture, Centaur, Target, Tracker, StepIndex * StepSeconds));
			bHasStunned |= IsStunnedState(Samples.Last().State);
		}
		LongestStunSeconds[RunIndex] = FindLongestRunSeconds(Samples, [](const FBossSample& Sample) { return IsStunnedState(Sample.State); });
		if (bBreaksChannel)
		{
			const int32 StunStart = FindFirstSampleIndex(Samples, 0, [](const FBossSample& Sample) { return IsStunnedState(Sample.State); });
			const int32 StunEnd = StunStart == INDEX_NONE ? INDEX_NONE : FindFirstSampleIndex(Samples, StunStart, [](const FBossSample& Sample) { return !IsStunnedState(Sample.State); });
			if (StunStart != INDEX_NONE && StunEnd != INDEX_NONE)
			{
				TargetLossDuringStun = Samples[StunStart].TargetHealth - Samples[StunEnd].TargetHealth;
			}
			continue;
		}
		bControlAttacked = Samples.ContainsByPredicate([](const FBossSample& Sample) { return HasNewEntity(Sample) && Sample.Seconds <= 4.f; });
	}
	TestTrue(FString::Printf(TEXT("Hitting the blue draw-sword channel stuns the centaur for 3 seconds (%.2f)"), LongestStunSeconds[0]), LongestStunSeconds[0] >= 2.9f);
	TestEqual(TEXT("The stunned centaur deals no damage"), TargetLossDuringStun, 0.f);
	TestTrue(FString::Printf(TEXT("Without the hits the centaur is not stunned (%.2f)"), LongestStunSeconds[1]), LongestStunSeconds[1] < 0.5f);
	TestTrue(TEXT("Without the hits the centaur attacks within 4 seconds of the phase change"), bControlAttacked);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMidBossCentaurChargeTest, "TDGame.MonsterAI.MidBoss.CentaurChargeIntoWallStuns", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMidBossCentaurChargeTest::RunTest(const FString& Parameters)
{
	IgnoreEngineInjectionEnsures(*this);
	constexpr float WallX = 600.f;
	float LongestStunSeconds[2] = { 0.f, 0.f };
	float FarthestX[2] = { 0.f, 0.f };
	float FastestSpeed[2] = { 0.f, 0.f };
	float TargetLoss[2] = { 0.f, 0.f };
	for (int32 RunIndex = 0; RunIndex < 2; ++RunIndex)
	{
		const bool bHasWall = RunIndex == 0;
		FMidBossWorld Fixture;
		if (bHasWall)
		{
			Fixture.SpawnBlockingBox(FVector(WallX, 0.f, 200.f), FVector(40.f, 500.f, 200.f));
		}
		UTDCombatComponent* Target = Fixture.SpawnTarget(FVector(1100.f, 0.f, 0.f));
		ATDMonsterCharacter* Centaur = Fixture.SpawnMonster(CentaurId, FVector::ZeroVector);
		if (!TestNotNull(TEXT("Target exists"), Target) || !TestNotNull(TEXT("Centaur exists"), Centaur))
		{
			return false;
		}
		Fixture.Tick(SettleSeconds);
		DamageToHealthRatio(Centaur, Target, 0.49f);
		if (!TestTrue(TEXT("The sword phase starts"), StepUntilPhase(Fixture, Centaur, 1, PhaseTimeoutSeconds)))
		{
			return false;
		}
		const float StartHealth = Target->GetCurrentHealth();
		FEntityTracker Tracker;
		TArray<FBossSample> Samples;
		FVector PreviousLocation = Centaur->GetActorLocation();
		for (int32 StepIndex = 0; StepIndex < SecondsToSteps(12.f); ++StepIndex)
		{
			Fixture.Step();
			KillOtherMonsters(*Fixture.World, Centaur, Target);
			Samples.Add(SampleBoss(Fixture, Centaur, Target, Tracker, StepIndex * StepSeconds));
			const FVector Location = Centaur->GetActorLocation();
			FarthestX[RunIndex] = FMath::Max(FarthestX[RunIndex], Location.X);
			FastestSpeed[RunIndex] = FMath::Max(FastestSpeed[RunIndex], FVector::Dist2D(Location, PreviousLocation) / StepSeconds);
			PreviousLocation = Location;
		}
		LongestStunSeconds[RunIndex] = FindLongestRunSeconds(Samples, [](const FBossSample& Sample) { return IsStunnedState(Sample.State); });
		TargetLoss[RunIndex] = StartHealth - Target->GetCurrentHealth();
	}
	TestTrue(FString::Printf(TEXT("A charge into the wall stuns the centaur for 2.5 seconds (%.2f)"), LongestStunSeconds[0]), LongestStunSeconds[0] >= 2.4f);
	TestTrue(FString::Printf(TEXT("The wall stops the charge (farthest %.0f)"), FarthestX[0]), FarthestX[0] < WallX - 40.f);
	TestTrue(FString::Printf(TEXT("The charge dashes at least 1200 cm/s toward the wall (%.0f)"), FastestSpeed[0]), FastestSpeed[0] >= 1200.f);
	TestTrue(FString::Printf(TEXT("An open charge does not stun (%.2f)"), LongestStunSeconds[1]), LongestStunSeconds[1] < 0.5f);
	TestTrue(FString::Printf(TEXT("An open charge dashes at least 1200 cm/s (%.0f)"), FastestSpeed[1]), FastestSpeed[1] >= 1200.f);
	TestTrue(FString::Printf(TEXT("An open charge hits the target (lost %.1f)"), TargetLoss[1]), TargetLoss[1] > 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMidBossGriffonGroundTest, "TDGame.MonsterAI.MidBoss.GriffonGroundMelee", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMidBossGriffonGroundTest::RunTest(const FString& Parameters)
{
	FMidBossWorld Fixture;
	UTDCombatComponent* Target = Fixture.SpawnTarget(FVector(220.f, 0.f, 0.f));
	ATDMonsterCharacter* Griffon = Fixture.SpawnMonster(GriffonId, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Target exists"), Target) || !TestNotNull(TEXT("Griffon exists"), Griffon) || !TestTrue(TEXT("Griffon has a brain"), Griffon->HasMonsterBrain()))
	{
		return false;
	}
	Fixture.Tick(SettleSeconds);
	const float GroundZ = Griffon->GetActorLocation().Z;
	const float StartHealth = Target->GetCurrentHealth();
	FEntityTracker Tracker;
	TSet<FName> AttackDefinitions;
	float HighestAltitude = 0.f;
	bool bEverFlew = false;
	for (int32 StepIndex = 0; StepIndex < SecondsToSteps(8.f); ++StepIndex)
	{
		Fixture.Step();
		const FBossSample Sample = SampleBoss(Fixture, Griffon, Target, Tracker, StepIndex * StepSeconds);
		AttackDefinitions.Append(Sample.NewEntities);
		HighestAltitude = FMath::Max(HighestAltitude, Sample.Location.Z - GroundZ);
		bEverFlew |= Sample.bIsFlying;
	}
	TestTrue(TEXT("The griffon's melee lands on the target"), Target->GetCurrentHealth() < StartHealth);
	TestTrue(FString::Printf(TEXT("The griffon mixes at least two different melee attacks in eight seconds (%d)"), AttackDefinitions.Num()), AttackDefinitions.Num() >= 2);
	TestFalse(TEXT("The griffon stays grounded above 60 percent health"), bEverFlew);
	TestTrue(FString::Printf(TEXT("The griffon stays on the floor (%.0f)"), HighestAltitude), HighestAltitude < 50.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMidBossGriffonAirRaidTest, "TDGame.MonsterAI.MidBoss.GriffonTakesOffAndLandsWithPunishWindow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMidBossGriffonAirRaidTest::RunTest(const FString& Parameters)
{
	FMidBossWorld Fixture;
	UTDCombatComponent* Target = Fixture.SpawnTarget(FVector(400.f, 0.f, 0.f));
	ATDMonsterCharacter* Griffon = Fixture.SpawnMonster(GriffonId, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Target exists"), Target) || !TestNotNull(TEXT("Griffon exists"), Griffon))
	{
		return false;
	}
	Fixture.Tick(SettleSeconds);
	const float GroundZ = Griffon->GetActorLocation().Z;
	DamageToHealthRatio(Griffon, Target, 0.59f);
	if (!TestTrue(TEXT("The sky phase starts below 60 percent health"), StepUntilPhase(Fixture, Griffon, 1, PhaseTimeoutSeconds)))
	{
		return false;
	}
	float TakeOffSeconds = 0.f;
	if (!TestTrue(TEXT("The griffon takes off within 3 seconds of the sky phase"), StepUntilAirborne(Fixture, Griffon, GroundZ, 3.f, TakeOffSeconds)))
	{
		return false;
	}
	FEntityTracker Tracker;
	TArray<FBossSample> Samples;
	for (int32 StepIndex = 0; StepIndex < SecondsToSteps(26.f); ++StepIndex)
	{
		Fixture.Step();
		Samples.Add(SampleBoss(Fixture, Griffon, Target, Tracker, StepIndex * StepSeconds));
	}
	const int32 TouchdownIndex = FindFirstSampleIndex(Samples, 0, [GroundZ](const FBossSample& Sample) { return !Sample.bIsFlying && Sample.Location.Z - GroundZ <= GroundedAltitudeCm; });
	if (!TestTrue(TEXT("The griffon lands again"), TouchdownIndex != INDEX_NONE))
	{
		AddInfo(DescribeTimeline(Samples, GroundZ));
		return false;
	}
	const float AirSeconds = Samples[TouchdownIndex].Seconds;
	if (AirSeconds < 8.f || AirSeconds > 18.f)
	{
		AddInfo(DescribeTimeline(Samples, GroundZ));
	}
	TestTrue(FString::Printf(TEXT("The air raid lasts 8 to 18 seconds (%.2f)"), AirSeconds), AirSeconds >= 8.f && AirSeconds <= 18.f);
	const bool bAttackedFromTheAir = Samples.ContainsByPredicate([AirSeconds](const FBossSample& Sample) { return HasNewEntity(Sample) && Sample.Seconds < AirSeconds - 1.f; });
	TestTrue(TEXT("The griffon attacks from the air"), bAttackedFromTheAir);
	const int32 ImpactIndex = FindFirstSampleIndex(Samples, TouchdownIndex, HasNewEntity);
	if (!TestTrue(TEXT("The landing casts an impact within 1.5 seconds of touchdown"), ImpactIndex != INDEX_NONE && Samples[ImpactIndex].Seconds <= AirSeconds + 1.5f))
	{
		return false;
	}
	const int32 NextAttackIndex = FindFirstSampleIndex(Samples, ImpactIndex + 1, HasNewEntity);
	const float NextAttackSeconds = NextAttackIndex == INDEX_NONE ? Samples.Last().Seconds : Samples[NextAttackIndex].Seconds;
	const float PunishSeconds = NextAttackSeconds - Samples[ImpactIndex].Seconds - LandingImpactWindupSeconds;
	TestTrue(FString::Printf(TEXT("The griffon gives a 2.5 second punish window after the landing impact (%.2f)"), PunishSeconds), PunishSeconds >= 2.4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMidBossGriffonShotDownTest, "TDGame.MonsterAI.MidBoss.GriffonShotDownFalls", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMidBossGriffonShotDownTest::RunTest(const FString& Parameters)
{
	FMidBossWorld Fixture;
	UTDCombatComponent* Target = Fixture.SpawnTarget(FVector(400.f, 0.f, 0.f));
	ATDMonsterCharacter* Griffon = Fixture.SpawnMonster(GriffonId, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Target exists"), Target) || !TestNotNull(TEXT("Griffon exists"), Griffon))
	{
		return false;
	}
	Fixture.Tick(SettleSeconds);
	const float GroundZ = Griffon->GetActorLocation().Z;
	DamageToHealthRatio(Griffon, Target, 0.59f);
	float TakeOffSeconds = 0.f;
	if (!TestTrue(TEXT("The sky phase starts"), StepUntilPhase(Fixture, Griffon, 1, PhaseTimeoutSeconds))
		|| !TestTrue(TEXT("The griffon is airborne before the shoot-down volley"), StepUntilAirborne(Fixture, Griffon, GroundZ, 3.f, TakeOffSeconds)))
	{
		return false;
	}
	FEntityTracker Tracker;
	TArray<FBossSample> Samples;
	for (int32 StepIndex = 0; StepIndex < SecondsToSteps(9.f); ++StepIndex)
	{
		if (StepIndex < 5 * 6 && StepIndex % 6 == 0)
		{
			HitMonster(Griffon, Target, 50.f);
		}
		Fixture.Step();
		Samples.Add(SampleBoss(Fixture, Griffon, Target, Tracker, StepIndex * StepSeconds));
	}
	const int32 TouchdownIndex = FindFirstSampleIndex(Samples, 0, [GroundZ](const FBossSample& Sample) { return !Sample.bIsFlying && Sample.Location.Z - GroundZ <= GroundedAltitudeCm; });
	if (!TestTrue(TEXT("The volley shoots the griffon down"), TouchdownIndex != INDEX_NONE))
	{
		return false;
	}
	const float TouchdownSeconds = Samples[TouchdownIndex].Seconds;
	TestTrue(FString::Printf(TEXT("The griffon hits the ground within 2 seconds (%.2f)"), TouchdownSeconds), TouchdownSeconds <= 2.f);
	const int32 NextAttackIndex = FindFirstSampleIndex(Samples, TouchdownIndex, HasNewEntity);
	const float DownedSeconds = (NextAttackIndex == INDEX_NONE ? Samples.Last().Seconds : Samples[NextAttackIndex].Seconds) - TouchdownSeconds;
	if (DownedSeconds < 4.5f)
	{
		AddInfo(DescribeTimeline(Samples, GroundZ));
	}
	TestTrue(FString::Printf(TEXT("The downed griffon does not attack for 4.5 seconds (%.2f)"), DownedSeconds), DownedSeconds >= 4.5f);
	const bool bFliesAgainWhileDowned = Samples.ContainsByPredicate([TouchdownSeconds](const FBossSample& Sample) { return Sample.bIsFlying && Sample.Seconds > TouchdownSeconds && Sample.Seconds < TouchdownSeconds + 4.5f; });
	TestFalse(TEXT("The downed griffon stays on the ground"), bFliesAgainWhileDowned);
	TestTrue(FString::Printf(TEXT("The downed griffon is stunned for most of the window (%.2f)"), FindLongestRunSeconds(Samples, [](const FBossSample& Sample) { return IsStunnedState(Sample.State); })),
		FindLongestRunSeconds(Samples, [](const FBossSample& Sample) { return IsStunnedState(Sample.State); }) >= 4.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMidBossDeterminismTest, "TDGame.MonsterAI.MidBoss.FightIsDeterministic", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMidBossDeterminismTest::RunTest(const FString& Parameters)
{
	IgnoreEngineInjectionEnsures(*this);
	TestNotNull(TEXT("The hound species is resident before the scripted fights"), FSoftObjectPath(HoundSpeciesPath).TryLoad());
	struct FScriptedFight
	{
		FName BossId;
		float PhaseRatio;
	};
	const FScriptedFight Fights[] = { { CentaurId, 0.49f }, { GriffonId, 0.59f } };
	for (const FScriptedFight& Fight : Fights)
	{
		int32 FirstSamples = 0;
		int32 SecondSamples = 0;
		int32 FirstPhase = 0;
		int32 SecondPhase = 0;
		const uint64 FirstHash = RunScriptedFight(Fight.BossId, 20.f, 3.f, Fight.PhaseRatio, FirstSamples, FirstPhase);
		const uint64 SecondHash = RunScriptedFight(Fight.BossId, 20.f, 3.f, Fight.PhaseRatio, SecondSamples, SecondPhase);
		const FString Name = Fight.BossId.ToString();
		TestTrue(FString::Printf(TEXT("%s fight was sampled"), *Name), FirstSamples > 0 && FirstSamples == SecondSamples);
		TestTrue(FString::Printf(TEXT("%s fight reaches its second phase in both runs"), *Name), FirstPhase >= 1 && SecondPhase >= 1);
		TestEqual(FString::Printf(TEXT("%s replays step for step"), *Name), FirstHash, SecondHash);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMidBossSpeciesPhaseTest, "TDGame.MonsterAI.MidBoss.CentaurSpeciesSwapsToSwordWhileAnimating", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMidBossSpeciesPhaseTest::RunTest(const FString& Parameters)
{
	IgnoreEngineInjectionEnsures(*this);
	UTDMonsterSpeciesAsset* Species = Cast<UTDMonsterSpeciesAsset>(FSoftObjectPath(CentaurSpeciesPath).TryLoad());
	if (!TestNotNull(TEXT("The centaur hunter species asset loads"), Species))
	{
		return false;
	}
	TestTrue(TEXT("The species swaps its look in the sword phase"), Species->PhasePresentations.Contains(TEXT("Sword")));
	TestTrue(TEXT("The species plays a channel clip for drawing the sword"), Species->ChannelClip.IsValidClip());
	FMidBossWorld Fixture;
	UTDCombatComponent* Target = Fixture.SpawnTarget(FVector(800.f, 0.f, 0.f));
	ATDMonsterCharacter* Centaur = Fixture.SpawnSpecies(Species, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Target exists"), Target) || !TestNotNull(TEXT("Centaur exists"), Centaur))
	{
		return false;
	}
	Fixture.Tick(1.f);
	DamageToHealthRatio(Centaur, Target, 0.49f);
	TestTrue(TEXT("The sword phase starts with the real species"), StepUntilPhase(Fixture, Centaur, 1, PhaseTimeoutSeconds));
	for (int32 StepIndex = SecondsToSteps(4.f); StepIndex > 0; --StepIndex)
	{
		Fixture.Step();
		KillOtherMonsters(*Fixture.World, Centaur, Target);
	}
	TestTrue(TEXT("The centaur keeps fighting after its look changes"), Centaur->GetCombatComponent()->IsAlive() && Centaur->HasMonsterBrain());
	return true;
}

#endif
