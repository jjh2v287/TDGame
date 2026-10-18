#if WITH_DEV_AUTOMATION_TESTS

#include "Characters/TDMonsterCharacter.h"
#include "Combat/Damage/TDDamageDefinition.h"
#include "Combat/Damage/TDStatusDefinition.h"
#include "Combat/TDCombatComponent.h"
#include "Components/SphereComponent.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MonsterAI/TDMonsterDefinitionLibrary.h"
#include "MonsterAI/TDMonsterSpeciesAsset.h"
#include "MonsterAI/TDMonsterThinkSubsystem.h"
#include "MonsterAI/TDResponseCurve.h"
#include "MonsterAI/TDUtilityScorer.h"
#include "MoverSimulationTypes.h"

namespace
{
	struct FTDMonsterAITestWorld
	{
		FTDMonsterAITestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDMonsterAITestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDMonsterAITestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
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
			Actor->SetActorLocation(Location);
			UTDCombatComponent* Combatant = NewObject<UTDCombatComponent>(Actor);
			Actor->AddInstanceComponent(Combatant);
			FTDCombatStats Stats;
			Stats.TeamId = 1;
			Stats.BaseMaxHealth = 1000.f;
			Combatant->SetStats(Stats);
			Combatant->RegisterComponent();
			return Combatant;
		}

		ATDMonsterCharacter* SpawnMonster(const FName DefinitionId, const FVector& Location)
		{
			UTDMonsterSpeciesAsset* Species = NewObject<UTDMonsterSpeciesAsset>(World);
			Species->DefinitionId = DefinitionId;
			const FTransform SpawnTransform(FRotator::ZeroRotator, Location);
			ATDMonsterCharacter* Monster = World->SpawnActorDeferred<ATDMonsterCharacter>(ATDMonsterCharacter::StaticClass(), SpawnTransform);
			Monster->SetSpecies(Species);
			Monster->GetMoverComponent()->StartingMovementMode = DefaultModeNames::Flying;
			Monster->FinishSpawning(SpawnTransform);
			return Monster;
		}

		void Tick(const float Duration)
		{
			const float Step = 1.f / 64.f;
			for (float Remaining = Duration; Remaining > UE_SMALL_NUMBER; Remaining -= Step)
			{
				++GFrameCounter;
				World->Tick(LEVELTICK_All, Step);
			}
		}

		UWorld* World = nullptr;
	};

	TSharedPtr<const FTDResolvedMonsterDefinition> ResolveSingle(const FString& FileName, const FString& Text, TArray<FString>& OutErrors)
	{
		TMap<FString, FString> Texts;
		Texts.Add(FileName, Text);
		TMap<FName, TSharedPtr<const FTDResolvedMonsterDefinition>> Definitions;
		UTDMonsterDefinitionLibrary::ResolveDefinitionTexts(Texts, [](FName) -> UTDDamageDefinition* { return nullptr; }, Definitions, OutErrors);
		const TSharedPtr<const FTDResolvedMonsterDefinition>* Definition = Definitions.Find(FName(*FPaths::GetBaseFilename(FileName)));
		return Definition ? *Definition : nullptr;
	}

	TSharedPtr<const FTDResolvedMonsterDefinition> ResolveSingleWithSpells(const FString& FileName, const FString& Text, TArray<FString>& OutErrors)
	{
		TMap<FString, FString> Texts;
		Texts.Add(FileName, Text);
		TMap<FName, TSharedPtr<const FTDResolvedMonsterDefinition>> Definitions;
		UTDMonsterDefinitionLibrary::ResolveDefinitionTexts(Texts, [](FName) -> UTDDamageDefinition* { return GetMutableDefault<UTDDamageDefinition>(); }, Definitions, OutErrors);
		const TSharedPtr<const FTDResolvedMonsterDefinition>* Definition = Definitions.Find(FName(*FPaths::GetBaseFilename(FileName)));
		return Definition ? *Definition : nullptr;
	}

	FString MakeKeyProbeText(const FString& TopLevelFields, const FString& NipExtraFields)
	{
		return FString::Printf(TEXT(R"({ "schema": 1, "id": "KeyProbe", %s
  "stats": { "max_health": 100, "attack_power": 1, "armor": 0, "move_speed": 100, "team": 2 },
  "abilities": {
    "Nip": { "spell": "DA_Any", "range": 150, "cooldown": 1%s },
    "Maul": { "spell": "DA_Any", "range": 200, "cooldown": 1, "token_cost": 0.75 },
    "Plain": { "spell": "DA_Any", "range": 300, "cooldown": 1 } },
  "actions": [ { "id": "Idle", "do": "Wait", "weight": 1 } ] })"), *TopLevelFields, *NipExtraFields);
	}

	int32 FindAbilityTokenCostQuarters(const FTDResolvedMonsterDefinition& Definition, const FName AbilityName)
	{
		const FTDResolvedAbility* Ability = Definition.Abilities.FindByPredicate([AbilityName](const FTDResolvedAbility& Candidate) { return Candidate.Name == AbilityName; });
		return Ability ? Ability->TokenCostQuarters : INDEX_NONE;
	}

	FString MakeConstantBrainText(const float FirstWeight, const float SecondWeight)
	{
		return FString::Printf(TEXT(R"({ "schema": 1, "id": "ScorerProbe",
  "stats": { "max_health": 100, "attack_power": 1, "armor": 0, "move_speed": 100, "team": 2 },
  "inertia": { "switch_ratio": 1.15, "min_hold_seconds": 0.5 },
  "actions": [
    { "id": "First", "do": "Wait", "weight": %.3f, "considerations": [ { "input": "SelfHealthRatio", "curve": "Constant", "b": 0.5 } ] },
    { "id": "Second", "do": "Wait", "weight": %.3f, "considerations": [ { "input": "SelfHealthRatio", "curve": "Constant", "b": 0.5 } ] },
    { "id": "Never", "do": "Wait", "weight": 9.0, "considerations": [ { "input": "SelfHealthRatio", "curve": "Constant", "b": 0.0 }, { "input": "SelfHealthRatio", "curve": "Constant", "b": 1.0 } ] } ] })"), FirstWeight, SecondWeight);
	}

	constexpr float EngagementTestStepSeconds = 1.f / 64.f;
	constexpr float EngagementTestHomeBand = 240.f;
	constexpr float EngagementTestNearPlayerX = 1300.f;
	constexpr float EngagementTestUnreachablePlayerX = 4000.f;
	constexpr float EngagementTestInsideLeashPlayerX = 1500.f;
	constexpr float EngagementTestNearbyPlayerOffset = 600.f;
	constexpr float EngagementTestOutsideReturnHitPlayerX = 2700.f;
	constexpr float EngagementTestHitDamage = 2.f;
	constexpr float EngagementTestGiveUpWaitSeconds = 4.f;
	constexpr int32 EngagementTestEngageWaitSteps = 2;
	constexpr float KitingBotSpeed = 600.f;
	constexpr float KitingBotKeepDistance = 1300.f;
	constexpr float KitingTestSeconds = 40.f;
	constexpr float KitingChaseCeiling = 4800.f;
	constexpr float KitingMinimumChase = 1000.f;
	constexpr float KitingScriptTolerance = 0.1f;
	constexpr int32 KitingArrivalSettleSteps = 64;
	constexpr float HitKitingTestSeconds = 20.f;
	constexpr float HitKitingChaseCeiling = 4700.f;
	constexpr int32 HitKitingIntervalSteps = 192;
	constexpr float StallTestLeashEdgePlayerX = 2800.f;
	constexpr float StallTestChaseSeconds = 6.f;
	constexpr float StallTestFarPlayerX = 5500.f;
	constexpr float StallTestMinBlockedSpawnDistance = 2500.f;
	constexpr int32 StallTestEngageWaitSteps = 128;
	constexpr float StallTestDriftTolerance = 1.f;
	constexpr float AllyAlertTestAllyOffsetY = 500.f;

	struct FTDKitingRunReport
	{
		float MaxHomeDistance = 0.f;
		int32 FarEngagements = 0;
		int32 Disengagements = 0;
		int32 Arrivals = 0;
		int32 ExternalBotMoves = 0;
		int32 LineCountMismatches = 0;
		int32 MinArrivalToEngageSteps = INDEX_NONE;
		int32 Hits = 0;
		bool bDidFirstHitLand = false;
		FString FirstFarEngagement;
		FString FirstCeilingBreach;
		FString FirstUnreturnedDisengagement;
		FString FirstExternalBotMove;
	};

	APawn* SpawnKitingPlayerPawn(UWorld& World, const FVector& Location)
	{
		APawn* PlayerPawn = World.SpawnActor<APawn>();
		USphereComponent* Sphere = NewObject<USphereComponent>(PlayerPawn);
		PlayerPawn->AddInstanceComponent(Sphere);
		PlayerPawn->SetRootComponent(Sphere);
		Sphere->SetSphereRadius(30.f);
		Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Sphere->SetCollisionObjectType(ECC_Pawn);
		Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
		Sphere->RegisterComponent();
		PlayerPawn->SetActorLocation(Location);
		UTDCombatComponent* Combatant = NewObject<UTDCombatComponent>(PlayerPawn);
		PlayerPawn->AddInstanceComponent(Combatant);
		FTDCombatStats Stats;
		Stats.TeamId = 1;
		Stats.BaseMaxHealth = 100000.f;
		Combatant->SetStats(Stats);
		Combatant->RegisterComponent();
		APlayerController* PlayerController = World.SpawnActor<APlayerController>();
		PlayerController->SetActorTickEnabled(false);
		PlayerController->SetPawn(PlayerPawn);
		return PlayerPawn;
	}

	bool IsSoleMonsterEngaged(const UTDMonsterThinkSubsystem& ThinkSubsystem)
	{
		const TArray<FString> Lines = ThinkSubsystem.GetMonsterDebugLines();
		return Lines.Num() == 1 && Lines[0].Contains(TEXT("engaged=1"));
	}

	bool IsMonsterLineEngaged(const UTDMonsterThinkSubsystem& ThinkSubsystem, const int32 LineIndex)
	{
		const TArray<FString> Lines = ThinkSubsystem.GetMonsterDebugLines();
		return Lines.IsValidIndex(LineIndex) && Lines[LineIndex].Contains(TEXT("engaged=1"));
	}

	bool CheckEngagementTestSetup(FAutomationTestBase& Test, UWorld* World, APawn* PlayerPawn, ATDMonsterCharacter* Monster, UTDMonsterThinkSubsystem* ThinkSubsystem)
	{
		if (!Test.TestNotNull(TEXT("Player pawn exists"), PlayerPawn) || !Test.TestNotNull(TEXT("Monster exists"), Monster) || !Test.TestNotNull(TEXT("Think subsystem exists"), ThinkSubsystem))
		{
			return false;
		}
		if (!Test.TestTrue(TEXT("The scripted pawn is player 0"), UGameplayStatics::GetPlayerPawn(World, 0) == PlayerPawn))
		{
			return false;
		}
		return Test.TestEqual(TEXT("Exactly one monster brain is registered"), ThinkSubsystem->GetMonsterDebugLines().Num(), 1);
	}

	bool TickUntilSoleMonsterEngages(FTDMonsterAITestWorld& Fixture, const UTDMonsterThinkSubsystem& ThinkSubsystem, const int32 MaxSteps)
	{
		for (int32 Step = 0; Step < MaxSteps; ++Step)
		{
			Fixture.Tick(EngagementTestStepSeconds);
			if (IsSoleMonsterEngaged(ThinkSubsystem))
			{
				return true;
			}
		}
		return false;
	}

	bool TickUntilSoleMonsterDisengages(FTDMonsterAITestWorld& Fixture, const UTDMonsterThinkSubsystem& ThinkSubsystem, const float MaxSeconds)
	{
		const int32 MaxSteps = FMath::RoundToInt(MaxSeconds / EngagementTestStepSeconds);
		for (int32 Step = 0; Step < MaxSteps; ++Step)
		{
			Fixture.Tick(EngagementTestStepSeconds);
			if (!IsSoleMonsterEngaged(ThinkSubsystem))
			{
				return true;
			}
		}
		return false;
	}

	bool DriveSoleMonsterToGiveUp(FAutomationTestBase& Test, FTDMonsterAITestWorld& Fixture, const UTDMonsterThinkSubsystem& ThinkSubsystem, APawn& PlayerPawn)
	{
		if (!Test.TestTrue(TEXT("The monster engages the nearby player"), TickUntilSoleMonsterEngages(Fixture, ThinkSubsystem, EngagementTestEngageWaitSteps)))
		{
			return false;
		}
		Fixture.Tick(0.5f);
		PlayerPawn.SetActorLocation(FVector(EngagementTestUnreachablePlayerX, 0.f, 0.f));
		return Test.TestTrue(TEXT("The monster gives up on a player beyond its leash within four seconds"), TickUntilSoleMonsterDisengages(Fixture, ThinkSubsystem, EngagementTestGiveUpWaitSeconds));
	}

	void HitMonsterFromScriptedPlayer(ATDMonsterCharacter& Monster, APawn& PlayerPawn)
	{
		FTDDamageContext HitContext;
		HitContext.Caster = &PlayerPawn;
		const UTDCombatComponent* PlayerCombat = PlayerPawn.FindComponentByClass<UTDCombatComponent>();
		if (PlayerCombat)
		{
			HitContext.Stats = PlayerCombat->GetStats();
		}
		Monster.GetCombatComponent()->ReceiveDamage(EngagementTestHitDamage, ETDDamageElement::Physical, false, HitContext);
	}

	FVector StepKitingBot(const FVector& BotLocation, const FVector& MonsterLocation, const bool bIsMonsterEngaged)
	{
		const float StepDistance = KitingBotSpeed * EngagementTestStepSeconds;
		if (bIsMonsterEngaged)
		{
			return BotLocation + FVector(StepDistance, 0.f, 0.f);
		}
		const float ToMonsterX = static_cast<float>(MonsterLocation.X - BotLocation.X);
		const float Gap = FMath::Abs(ToMonsterX) - KitingBotKeepDistance;
		return BotLocation + FVector(FMath::Sign(ToMonsterX) * FMath::Clamp(Gap, -StepDistance, StepDistance), 0.f, 0.f);
	}

	void RecordFirstKitingViolation(FString& FirstViolation, const int32 Step, const float Value)
	{
		if (!FirstViolation.IsEmpty())
		{
			return;
		}
		FirstViolation = FString::Printf(TEXT("first at step %d (%.2f s), value %.1f"), Step, (Step + 1) * EngagementTestStepSeconds, Value);
	}

	FTDKitingRunReport RunKitingBotScenario(FTDMonsterAITestWorld& Fixture, APawn& PlayerPawn, ATDMonsterCharacter& Monster, const UTDMonsterThinkSubsystem& ThinkSubsystem, const float Seconds, const float ChaseCeiling, const int32 HitIntervalSteps)
	{
		FTDKitingRunReport Report;
		const int32 StepCount = FMath::RoundToInt(Seconds / EngagementTestStepSeconds);
		FVector BotLocation = PlayerPawn.GetActorLocation();
		bool bWasEngaged = false;
		bool bIsArrivalPending = false;
		int32 LastArrivalStep = INDEX_NONE;
		for (int32 Step = 0; Step < StepCount; ++Step)
		{
			BotLocation = StepKitingBot(BotLocation, Monster.GetActorLocation(), bWasEngaged);
			PlayerPawn.SetActorLocation(BotLocation);
			if (HitIntervalSteps > 0 && bWasEngaged && Step % HitIntervalSteps == 0)
			{
				const float HealthBeforeHit = Monster.GetCombatComponent()->GetCurrentHealth();
				HitMonsterFromScriptedPlayer(Monster, PlayerPawn);
				if (Report.Hits == 0)
				{
					Report.bDidFirstHitLand = Monster.GetCombatComponent()->GetCurrentHealth() < HealthBeforeHit;
				}
				++Report.Hits;
			}
			Fixture.Tick(EngagementTestStepSeconds);
			if (ThinkSubsystem.GetMonsterDebugLines().Num() != 1)
			{
				++Report.LineCountMismatches;
			}
			const bool bIsEngaged = IsSoleMonsterEngaged(ThinkSubsystem);
			const float HomeDistance = FVector::Dist2D(Monster.GetActorLocation(), FVector::ZeroVector);
			Report.MaxHomeDistance = FMath::Max(Report.MaxHomeDistance, HomeDistance);
			const float BotDrift = FVector::Dist(PlayerPawn.GetActorLocation(), BotLocation);
			if (BotDrift > KitingScriptTolerance)
			{
				++Report.ExternalBotMoves;
				RecordFirstKitingViolation(Report.FirstExternalBotMove, Step, BotDrift);
			}
			if (HomeDistance > ChaseCeiling)
			{
				RecordFirstKitingViolation(Report.FirstCeilingBreach, Step, HomeDistance);
			}
			if (bIsEngaged && !bWasEngaged && HomeDistance > EngagementTestHomeBand)
			{
				++Report.FarEngagements;
				RecordFirstKitingViolation(Report.FirstFarEngagement, Step, HomeDistance);
			}
			if (bIsEngaged && !bWasEngaged && LastArrivalStep != INDEX_NONE)
			{
				const int32 ArrivalToEngageSteps = Step - LastArrivalStep;
				Report.MinArrivalToEngageSteps = Report.MinArrivalToEngageSteps == INDEX_NONE ? ArrivalToEngageSteps : FMath::Min(Report.MinArrivalToEngageSteps, ArrivalToEngageSteps);
				LastArrivalStep = INDEX_NONE;
			}
			if (bWasEngaged && !bIsEngaged)
			{
				++Report.Disengagements;
				bIsArrivalPending = true;
			}
			if (!bIsEngaged && bIsArrivalPending && HomeDistance <= EngagementTestHomeBand)
			{
				++Report.Arrivals;
				bIsArrivalPending = false;
				LastArrivalStep = Step;
			}
			if (Report.Disengagements > Report.Arrivals + 1)
			{
				RecordFirstKitingViolation(Report.FirstUnreturnedDisengagement, Step, HomeDistance);
			}
			bWasEngaged = bIsEngaged;
		}
		return Report;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMonsterAIResponseCurveTest, "TDGame.MonsterAI.ResponseCurves", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMonsterAIResponseCurveTest::RunTest(const FString& Parameters)
{
	FTDResponseCurve Curve;
	Curve.Type = ETDResponseCurveType::Linear;
	TestEqual(TEXT("Linear passes the normalized input through"), Curve.Evaluate(0.3f), 0.3f, 0.0001f);
	Curve.bInvert = true;
	TestEqual(TEXT("Invert mirrors the output"), Curve.Evaluate(0.3f), 0.7f, 0.0001f);
	Curve = FTDResponseCurve();
	Curve.Type = ETDResponseCurveType::Binary;
	Curve.C = 0.5f;
	TestEqual(TEXT("Binary is zero at the threshold"), Curve.Evaluate(0.5f), 0.f);
	TestEqual(TEXT("Binary is one above the threshold"), Curve.Evaluate(0.51f), 1.f);
	Curve = FTDResponseCurve();
	Curve.Type = ETDResponseCurveType::Logistic;
	Curve.M = 12.f;
	Curve.C = 0.15f;
	TestEqual(TEXT("Logistic is one half at its midpoint"), Curve.Evaluate(0.15f), 0.5f, 0.0001f);
	Curve = FTDResponseCurve();
	Curve.Type = ETDResponseCurveType::Quadratic;
	Curve.M = -1.f;
	Curve.K = 2.f;
	Curve.B = 1.f;
	TestEqual(TEXT("Quadratic 1 - x^2 at 0.5"), Curve.Evaluate(0.5f), 0.75f, 0.0001f);
	Curve = FTDResponseCurve();
	Curve.Type = ETDResponseCurveType::Gaussian;
	Curve.M = 0.1f;
	Curve.C = 0.4f;
	TestEqual(TEXT("Gaussian peaks at its center"), Curve.Evaluate(0.4f), 1.f, 0.0001f);
	Curve.M = 0.f;
	TestEqual(TEXT("Degenerate Gaussian falls back to b"), Curve.Evaluate(0.4f), 0.f, 0.0001f);
	Curve = FTDResponseCurve();
	Curve.Type = ETDResponseCurveType::Linear;
	Curve.M = 5.f;
	TestEqual(TEXT("Outputs are clamped to one"), Curve.Evaluate(1.f), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMonsterAIDefinitionFilesTest, "TDGame.MonsterAI.Validate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMonsterAIDefinitionFilesTest::RunTest(const FString& Parameters)
{
	UTDMonsterDefinitionLibrary* Library = UTDMonsterDefinitionLibrary::Get();
	if (!TestNotNull(TEXT("Definition library exists"), Library))
	{
		return false;
	}
	const int32 ErrorCount = Library->ReloadDefinitions();
	for (const FString& Error : Library->GetLoadErrors())
	{
		AddError(Error);
	}
	TestEqual(TEXT("Every shipped definition loads without errors"), ErrorCount, 0);
	const TSharedPtr<const FTDResolvedMonsterDefinition> Archer = Library->FindDefinition(TEXT("Archer"));
	if (!TestTrue(TEXT("Archer resolves"), Archer.IsValid()))
	{
		return false;
	}
	TArray<FName> ArcherActionIds;
	for (const FTDResolvedAction& Action : Archer->GetActions(0))
	{
		ArcherActionIds.Add(Action.Id);
	}
	const TArray<FName> ExpectedArcherActions = { TEXT("Flee"), TEXT("Idle"), TEXT("KeepDistance"), TEXT("Shoot"), TEXT("Backpedal") };
	TestTrue(TEXT("extends removes, keeps and appends actions in order"), ArcherActionIds == ExpectedArcherActions);
	TestEqual(TEXT("Archer overrides only the stats it lists"), Archer->Stats.Armor, 5.f);
	TestEqual(TEXT("Archer inherits the team"), Archer->Stats.Team, 2);
	TestEqual(TEXT("think_hz 8 becomes an 8-step period"), Archer->ThinkPeriodSteps, 8);
	TestTrue(TEXT("Archer's own role overrides the Melee role it inherits"), Archer->CombatRole == ETDMonsterCombatRole::Ranged);

	const TSharedPtr<const FTDResolvedMonsterDefinition> Goblin = Library->FindDefinition(TEXT("Goblin_Melee"));
	if (!TestTrue(TEXT("Goblin_Melee resolves"), Goblin.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("Goblin_Melee declares the Melee role"), Goblin->CombatRole == ETDMonsterCombatRole::Melee);

	const TSharedPtr<const FTDResolvedMonsterDefinition> Ogre = Library->FindDefinition(TEXT("Ogre_Boss"));
	if (!TestTrue(TEXT("Ogre_Boss resolves"), Ogre.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("Ogre_Boss is marked as a boss"), Ogre->bIsBoss);
	TestTrue(TEXT("Every Ogre_Boss ability keeps the default one-token cost"),
		!Ogre->Abilities.IsEmpty() && !Ogre->Abilities.ContainsByPredicate([](const FTDResolvedAbility& Ability) { return Ability.TokenCostQuarters != TDMonsterAI::QuartersPerToken; }));
	TestEqual(TEXT("One phase gives two action lists"), Ogre->ActionsByPhase.Num(), 2);
	const bool bEnragedHasCombo = Ogre->GetActions(1).ContainsByPredicate([](const FTDResolvedAction& Action) { return Action.Id == TEXT("SmashCombo"); });
	const bool bEnragedKeepsSmash = Ogre->GetActions(1).ContainsByPredicate([](const FTDResolvedAction& Action) { return Action.Id == TEXT("Smash"); });
	TestTrue(TEXT("Enraged phase adds SmashCombo"), bEnragedHasCombo);
	TestFalse(TEXT("Enraged phase removes Smash"), bEnragedKeepsSmash);
	TestEqual(TEXT("Enraged phase raises attack power"), Ogre->GetStatsForPhase(1).AttackPower, 56.f);
	for (const FName Id : { FName(TEXT("Hyena")), FName(TEXT("BookHead_Brute")), FName(TEXT("BookHead_Caster")), FName(TEXT("StoneGolem")) })
	{
		const TSharedPtr<const FTDResolvedMonsterDefinition> Definition = Library->FindDefinition(Id);
		TestTrue(FString::Printf(TEXT("%s resolves with every ability bound to a spell"), *Id.ToString()),
			Definition.IsValid() && !Definition->Abilities.ContainsByPredicate([](const FTDResolvedAbility& Ability) { return Ability.Spell == nullptr; }));
	}
	const TSharedPtr<const FTDResolvedMonsterDefinition> Hyena = Library->FindDefinition(TEXT("Hyena"));
	TestTrue(TEXT("Hyena is not a boss"), Hyena.IsValid() && !Hyena->bIsBoss);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMonsterAIDefinitionErrorsTest, "TDGame.MonsterAI.DefinitionErrorsNameTheFix", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMonsterAIDefinitionErrorsTest::RunTest(const FString& Parameters)
{
	TArray<FString> Errors;
	const FString BrokenText = TEXT(R"({ "schema": 1, "id": "Broken", "colour": "red",
  "stats": { "max_health": 10, "attack_power": 1, "armor": 0, "move_speed": 100, "team": 2 },
  "actions": [ { "id": "Chase", "do": "MoveTowards", "weight": 1.0,
    "considerations": [ { "input": "DistanceTarget", "curve": "Logistik" } ] } ] })");
	const TSharedPtr<const FTDResolvedMonsterDefinition> Broken = ResolveSingle(TEXT("Broken.json"), BrokenText, Errors);
	TestFalse(TEXT("A definition with errors is rejected"), Broken.IsValid());
	const FString Joined = FString::Join(Errors, TEXT("\n"));
	TestTrue(TEXT("Unknown top-level key is reported"), Joined.Contains(TEXT("Broken.json: colour: unknown key")));
	TestTrue(TEXT("Unknown primitive suggests MoveToward"), Joined.Contains(TEXT("did you mean 'MoveToward'?")));
	TestTrue(TEXT("Unknown input suggests DistanceToTarget"), Joined.Contains(TEXT("did you mean 'DistanceToTarget'?")));
	TestTrue(TEXT("Unknown curve suggests Logistic"), Joined.Contains(TEXT("did you mean 'Logistic'?")));

	Errors.Reset();
	const FString MismatchText = TEXT(R"({ "schema": 1, "id": "Other", "stats": { "max_health": 10, "attack_power": 1, "armor": 0, "move_speed": 100, "team": 2 }, "actions": [ { "id": "Idle", "do": "Wait", "weight": 1 } ] })");
	TestFalse(TEXT("id must match the file name"), ResolveSingle(TEXT("Mismatch.json"), MismatchText, Errors).IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMonsterAIDefinitionTacticsKeysTest, "TDGame.MonsterAI.DefinitionTacticsKeys", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMonsterAIDefinitionTacticsKeysTest::RunTest(const FString& Parameters)
{
	TArray<FString> Errors;
	const TSharedPtr<const FTDResolvedMonsterDefinition> Probe = ResolveSingleWithSpells(TEXT("KeyProbe.json"), MakeKeyProbeText(TEXT("\"role\": \"Ranged\", \"boss\": true,"), TEXT(", \"token_cost\": 0.25")), Errors);
	if (!TestTrue(TEXT("A definition with role, boss and token_cost resolves"), Probe.IsValid()))
	{
		AddError(FString::Join(Errors, TEXT("\n")));
		return false;
	}
	TestEqual(TEXT("The tactics keys resolve without errors"), Errors.Num(), 0);
	TestTrue(TEXT("role Ranged resolves to the Ranged combat role"), Probe->CombatRole == ETDMonsterCombatRole::Ranged);
	TestTrue(TEXT("boss true marks the definition as a boss"), Probe->bIsBoss);
	TestEqual(TEXT("token_cost 0.25 is one quarter token"), FindAbilityTokenCostQuarters(*Probe, TEXT("Nip")), 1);
	TestEqual(TEXT("token_cost 0.75 is three quarter tokens"), FindAbilityTokenCostQuarters(*Probe, TEXT("Maul")), 3);
	TestEqual(TEXT("An ability without token_cost costs one whole token"), FindAbilityTokenCostQuarters(*Probe, TEXT("Plain")), TDMonsterAI::QuartersPerToken);

	Errors.Reset();
	const TSharedPtr<const FTDResolvedMonsterDefinition> Bare = ResolveSingleWithSpells(TEXT("KeyProbe.json"), MakeKeyProbeText(FString(), FString()), Errors);
	if (!TestTrue(TEXT("A definition without role and boss resolves"), Bare.IsValid()))
	{
		AddError(FString::Join(Errors, TEXT("\n")));
		return false;
	}
	TestEqual(TEXT("Omitted tactics keys resolve without errors"), Errors.Num(), 0);
	TestTrue(TEXT("An omitted role stays Unspecified"), Bare->CombatRole == ETDMonsterCombatRole::Unspecified);
	TestFalse(TEXT("An omitted boss flag stays false"), Bare->bIsBoss);

	auto CheckRejected = [this, &Errors](const TCHAR* CaseName, const FString& TopLevelFields, const FString& NipExtraFields, const TCHAR* ExpectedMessage)
	{
		Errors.Reset();
		const TSharedPtr<const FTDResolvedMonsterDefinition> Rejected = ResolveSingleWithSpells(TEXT("KeyProbe.json"), MakeKeyProbeText(TopLevelFields, NipExtraFields), Errors);
		TestFalse(FString::Printf(TEXT("%s rejects the definition"), CaseName), Rejected.IsValid());
		TestTrue(FString::Printf(TEXT("%s reports '%s'"), CaseName, ExpectedMessage), FString::Join(Errors, TEXT("\n")).Contains(ExpectedMessage));
	};
	CheckRejected(TEXT("An unknown role"), TEXT("\"role\": \"Boss\","), FString(), TEXT("\"boss\": true"));
	CheckRejected(TEXT("A misspelled role"), TEXT("\"role\": \"Range\","), FString(), TEXT("did you mean 'Ranged'?"));
	CheckRejected(TEXT("A numeric role"), TEXT("\"role\": 1,"), FString(), TEXT("role: expected Melee, Ranged, Charger or Support"));
	CheckRejected(TEXT("A text boss flag"), TEXT("\"boss\": \"yes\","), FString(), TEXT("boss: expected true or false"));
	CheckRejected(TEXT("token_cost 0.3"), FString(), TEXT(", \"token_cost\": 0.3"), TEXT("quarter tokens"));
	CheckRejected(TEXT("token_cost 0"), FString(), TEXT(", \"token_cost\": 0"), TEXT("quarter tokens"));
	CheckRejected(TEXT("token_cost 1.5"), FString(), TEXT(", \"token_cost\": 1.5"), TEXT("quarter tokens"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMonsterAIScorerTest, "TDGame.MonsterAI.ScorerTieBreakAndInertia", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMonsterAIScorerTest::RunTest(const FString& Parameters)
{
	TArray<FString> Errors;
	const TSharedPtr<const FTDResolvedMonsterDefinition> Tied = ResolveSingle(TEXT("ScorerProbe.json"), MakeConstantBrainText(1.f, 1.f), Errors);
	if (!TestTrue(TEXT("Probe definition resolves"), Tied.IsValid()))
	{
		for (const FString& Error : Errors)
		{
			AddError(Error);
		}
		return false;
	}
	FTDBrainInputs Inputs;
	FTDBrainMemory Memory;
	TArray<int32> Cooldowns;
	Cooldowns.Init(0, 3);
	Memory.ActionCooldownSteps = Cooldowns;
	FTDBrainDecision Decision = FTDUtilityScorer::Think(*Tied, 0, Inputs, Memory);
	TestEqual(TEXT("A tie goes to the earlier action"), Decision.ActionIndex, 0);
	TestEqual(TEXT("A zero consideration stops the product"), FTDUtilityScorer::ScoreAction(Tied->GetActions(0)[2], Inputs), 0.f);

	const TSharedPtr<const FTDResolvedMonsterDefinition> SlightlyBetter = ResolveSingle(TEXT("ScorerProbe.json"), MakeConstantBrainText(1.1f, 1.f), Errors);
	Memory.CurrentActionIndex = 1;
	Memory.HoldRemainingSteps = 0;
	Decision = FTDUtilityScorer::Think(*SlightlyBetter, 0, Inputs, Memory);
	TestEqual(TEXT("A challenger within the switch ratio does not replace the current action"), Decision.ActionIndex, 1);
	TestTrue(TEXT("The kept decision is flagged"), Decision.bKeptCurrent);

	const TSharedPtr<const FTDResolvedMonsterDefinition> ClearlyBetter = ResolveSingle(TEXT("ScorerProbe.json"), MakeConstantBrainText(1.5f, 1.f), Errors);
	Decision = FTDUtilityScorer::Think(*ClearlyBetter, 0, Inputs, Memory);
	TestEqual(TEXT("A challenger above the switch ratio replaces the current action"), Decision.ActionIndex, 0);
	Memory.HoldRemainingSteps = 5;
	Decision = FTDUtilityScorer::Think(*ClearlyBetter, 0, Inputs, Memory);
	TestEqual(TEXT("Minimum hold keeps the current action"), Decision.ActionIndex, 1);
	TestEqual(TEXT("min_hold_seconds 0.5 converts to 32 steps"), ClearlyBetter->MinHoldSteps, 32);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMonsterAIAttackTest, "TDGame.MonsterAI.MonsterTelegraphsThenHits", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMonsterAIAttackTest::RunTest(const FString& Parameters)
{
	FTDMonsterAITestWorld Fixture;
	UTDCombatComponent* Target = Fixture.SpawnTarget(FVector(120.f, 0.f, 0.f));
	ATDMonsterCharacter* Monster = Fixture.SpawnMonster(TEXT("Goblin_Melee"), FVector::ZeroVector);
	UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.World->GetSubsystem<UTDMonsterThinkSubsystem>();
	if (!TestNotNull(TEXT("Target exists"), Target) || !TestNotNull(TEXT("Monster exists"), Monster) || !TestNotNull(TEXT("Think subsystem exists"), ThinkSubsystem))
	{
		return false;
	}
	TestTrue(TEXT("Monster registered a brain"), Monster->HasMonsterBrain());
	TestEqual(TEXT("Monster stats come from the JSON"), Monster->GetCombatComponent()->GetCurrentHealth(), 120.f);
	const float StartHealth = Target->GetCurrentHealth();
	Fixture.Tick(0.3f);
	TestEqual(TEXT("No damage lands during the telegraph"), Target->GetCurrentHealth(), StartHealth);
	TestTrue(TEXT("The monster is casting"), ThinkSubsystem->GetMonsterDebugLines().ContainsByPredicate([](const FString& Line) { return Line.Contains(TEXT("state=Cast")); }));
	Fixture.Tick(1.2f);
	TestTrue(TEXT("The slash lands after the telegraph"), Target->GetCurrentHealth() < StartHealth);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMonsterAIInterruptTest, "TDGame.MonsterAI.HeavyHitInterruptsTelegraph", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMonsterAIInterruptTest::RunTest(const FString& Parameters)
{
	FTDMonsterAITestWorld Fixture;
	UTDCombatComponent* Target = Fixture.SpawnTarget(FVector(120.f, 0.f, 0.f));
	ATDMonsterCharacter* Monster = Fixture.SpawnMonster(TEXT("Goblin_Melee"), FVector::ZeroVector);
	if (!TestNotNull(TEXT("Target exists"), Target) || !TestNotNull(TEXT("Monster exists"), Monster))
	{
		return false;
	}
	const float StartHealth = Target->GetCurrentHealth();
	Fixture.Tick(0.2f);
	FTDDamageContext HeavyHitContext;
	HeavyHitContext.Caster = Target->GetOwner();
	HeavyHitContext.Stats = Target->GetStats();
	Monster->GetCombatComponent()->ReceiveDamage(40.f, ETDDamageElement::Physical, false, HeavyHitContext);
	Fixture.Tick(0.4f);
	TestEqual(TEXT("A heavy hit cancels the pending slash"), Target->GetCurrentHealth(), StartHealth);
	TestTrue(TEXT("The monster survives the hit"), Monster->GetCombatComponent()->IsAlive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMonsterAIFreezeTest, "TDGame.MonsterAI.Fsm.FreezeHaltsThenResumes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMonsterAIFreezeTest::RunTest(const FString& Parameters)
{
	FTDMonsterAITestWorld Fixture;
	UTDCombatComponent* Target = Fixture.SpawnTarget(FVector(120.f, 0.f, 0.f));
	ATDMonsterCharacter* Monster = Fixture.SpawnMonster(TEXT("Goblin_Melee"), FVector::ZeroVector);
	UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.World->GetSubsystem<UTDMonsterThinkSubsystem>();
	if (!TestNotNull(TEXT("Target exists"), Target) || !TestNotNull(TEXT("Monster exists"), Monster) || !TestNotNull(TEXT("Think subsystem exists"), ThinkSubsystem))
	{
		return false;
	}
	const float StartHealth = Target->GetCurrentHealth();
	Fixture.Tick(0.2f);
	UTDStatusDefinition* Freeze = NewObject<UTDStatusDefinition>(Fixture.World);
	Freeze->Duration = 0.6f;
	Freeze->bFreezesTarget = true;
	FTDDamageContext FreezeContext;
	FreezeContext.Caster = Target->GetOwner();
	FreezeContext.Stats = Target->GetStats();
	Monster->GetCombatComponent()->ApplyStatus(Freeze, FreezeContext);
	TestTrue(TEXT("The status freezes the monster"), Monster->GetCombatComponent()->IsFrozen());
	TestTrue(TEXT("Freezing cancels the cast"), ThinkSubsystem->GetMonsterDebugLines().ContainsByPredicate([](const FString& Line) { return Line.Contains(TEXT("state=Idle")); }));
	Fixture.Tick(0.5f);
	TestEqual(TEXT("No damage lands while frozen"), Target->GetCurrentHealth(), StartHealth);
	TestTrue(TEXT("The monster stays idle while frozen"), ThinkSubsystem->GetMonsterDebugLines().ContainsByPredicate([](const FString& Line) { return Line.Contains(TEXT("state=Idle")); }));
	Fixture.Tick(2.f);
	TestFalse(TEXT("The freeze expires"), Monster->GetCombatComponent()->IsFrozen());
	TestTrue(TEXT("The monster thinks again and lands a slash after thawing"), Target->GetCurrentHealth() < StartHealth);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMonsterAIPhaseTest, "TDGame.MonsterAI.Fsm.PhasePlaysOnEnterSequence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMonsterAIPhaseTest::RunTest(const FString& Parameters)
{
	FTDMonsterAITestWorld Fixture;
	UTDCombatComponent* Target = Fixture.SpawnTarget(FVector(600.f, 0.f, 0.f));
	ATDMonsterCharacter* Monster = Fixture.SpawnMonster(TEXT("BookHead_Brute"), FVector::ZeroVector);
	UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.World->GetSubsystem<UTDMonsterThinkSubsystem>();
	if (!TestNotNull(TEXT("Target exists"), Target) || !TestNotNull(TEXT("Monster exists"), Monster) || !TestNotNull(TEXT("Think subsystem exists"), ThinkSubsystem))
	{
		return false;
	}
	Fixture.Tick(0.1f);
	FTDDamageContext HitContext;
	HitContext.Caster = Target->GetOwner();
	HitContext.Stats = Target->GetStats();
	Monster->GetCombatComponent()->ReceiveDamage(185.f, ETDDamageElement::Physical, false, HitContext);
	TestTrue(TEXT("The monster is below the 40 percent phase threshold"), Monster->GetCombatComponent()->GetCurrentHealth() <= 104.f);
	bool bSawSequence = false;
	bool bSawPhase = false;
	for (int32 Sample = 0; Sample < 24 && !bSawSequence; ++Sample)
	{
		Fixture.Tick(1.f / 32.f);
		for (const FString& Line : ThinkSubsystem->GetMonsterDebugLines())
		{
			bSawSequence |= Line.Contains(TEXT("state=Sequence"));
			bSawPhase |= Line.Contains(TEXT("phase=1"));
		}
	}
	TestTrue(TEXT("The Enraged phase is entered"), bSawPhase);
	TestTrue(TEXT("The on_enter sequence runs as the Sequence state"), bSawSequence);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMonsterAINoGlobalRandomTest, "TDGame.MonsterAI.NoGlobalRandom", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMonsterAINoGlobalRandomTest::RunTest(const FString& Parameters)
{
	const FString SourceDirectory = FPaths::Combine(FPaths::GameSourceDir(), TEXT("TDGame"), TEXT("MonsterAI"));
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *SourceDirectory, TEXT("*.cpp"), true, false);
	IFileManager::Get().FindFilesRecursive(Files, *SourceDirectory, TEXT("*.h"), true, false, false);
	TestTrue(TEXT("MonsterAI sources were found"), Files.Num() > 5);
	const TArray<FString> ForbiddenCalls = { TEXT("FMath::FRand"), TEXT("FMath::Rand"), TEXT("FMath::RandRange"), TEXT("FMath::RandBool"), TEXT("FMath::VRand"), TEXT("FMath::SRand"), TEXT("FMath::RandInit") };
	for (const FString& File : Files)
	{
		if (File.EndsWith(TEXT("TDMonsterAITests.cpp")))
		{
			continue;
		}
		FString Text;
		FFileHelper::LoadFileToString(Text, *File);
		for (const FString& Call : ForbiddenCalls)
		{
			TestFalse(FString::Printf(TEXT("%s must not use %s"), *FPaths::GetCleanFilename(File), *Call), Text.Contains(Call));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMonsterAIKitingTest, "TDGame.MonsterAI.Engagement.KitingDoesNotYoYo", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMonsterAIKitingTest::RunTest(const FString& Parameters)
{
	FTDMonsterAITestWorld Fixture;
	APawn* PlayerPawn = SpawnKitingPlayerPawn(*Fixture.World, FVector(KitingBotKeepDistance, 0.f, 0.f));
	ATDMonsterCharacter* Monster = Fixture.SpawnMonster(TEXT("Goblin_Melee"), FVector::ZeroVector);
	UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.World->GetSubsystem<UTDMonsterThinkSubsystem>();
	if (!CheckEngagementTestSetup(*this, Fixture.World, PlayerPawn, Monster, ThinkSubsystem))
	{
		return false;
	}
	const FTDKitingRunReport Report = RunKitingBotScenario(Fixture, *PlayerPawn, *Monster, *ThinkSubsystem, KitingTestSeconds, KitingChaseCeiling, 0);
	AddInfo(FString::Printf(TEXT("max home distance %.0f cm, disengagements %d, arrivals %d, far engagements %d, external bot moves %d, shortest arrival to engage %d steps"), Report.MaxHomeDistance, Report.Disengagements, Report.Arrivals, Report.FarEngagements, Report.ExternalBotMoves, Report.MinArrivalToEngageSteps));
	TestEqual(FString::Printf(TEXT("Nothing else moves the scripted player (%s)"), *Report.FirstExternalBotMove), Report.ExternalBotMoves, 0);
	TestEqual(TEXT("The think subsystem reports exactly one monster on every step"), Report.LineCountMismatches, 0);
	TestEqual(FString::Printf(TEXT("A disengaged monster away from home never re-engages by proximity (%s)"), *Report.FirstFarEngagement), Report.FarEngagements, 0);
	TestTrue(FString::Printf(TEXT("The kiting player never drags the monster past %.0f cm from home (%s)"), KitingChaseCeiling, *Report.FirstCeilingBreach), Report.MaxHomeDistance <= KitingChaseCeiling);
	TestTrue(FString::Printf(TEXT("Every disengagement ends at home: disengagements %d, arrivals %d (%s)"), Report.Disengagements, Report.Arrivals, *Report.FirstUnreturnedDisengagement), Report.Disengagements <= Report.Arrivals + 1);
	TestTrue(FString::Printf(TEXT("The monster gives up at least twice (%d)"), Report.Disengagements), Report.Disengagements >= 2);
	TestTrue(FString::Printf(TEXT("The monster chases at least %.0f cm from home (%.0f)"), KitingMinimumChase, Report.MaxHomeDistance), Report.MaxHomeDistance >= KitingMinimumChase);
	if (Report.MinArrivalToEngageSteps == INDEX_NONE)
	{
		AddInfo(TEXT("The monster never engaged again after arriving home, so the arrival settle check is skipped"));
		return true;
	}
	TestTrue(FString::Printf(TEXT("An arrived monster waits at least %d steps before engaging again (shortest %d)"), KitingArrivalSettleSteps, Report.MinArrivalToEngageSteps), Report.MinArrivalToEngageSteps >= KitingArrivalSettleSteps);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMonsterAIReturningHitTest, "TDGame.MonsterAI.Engagement.ReturningIgnoresOutsideHits", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMonsterAIReturningHitTest::RunTest(const FString& Parameters)
{
	FTDMonsterAITestWorld Fixture;
	APawn* PlayerPawn = SpawnKitingPlayerPawn(*Fixture.World, FVector(EngagementTestNearPlayerX, 0.f, 0.f));
	ATDMonsterCharacter* Monster = Fixture.SpawnMonster(TEXT("Goblin_Melee"), FVector::ZeroVector);
	UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.World->GetSubsystem<UTDMonsterThinkSubsystem>();
	if (!CheckEngagementTestSetup(*this, Fixture.World, PlayerPawn, Monster, ThinkSubsystem))
	{
		return false;
	}
	if (!DriveSoleMonsterToGiveUp(*this, Fixture, *ThinkSubsystem, *PlayerPawn))
	{
		return false;
	}
	PlayerPawn->SetActorLocation(FVector(EngagementTestOutsideReturnHitPlayerX, 0.f, 0.f));
	const float HealthBeforeOutsideHit = Monster->GetCombatComponent()->GetCurrentHealth();
	HitMonsterFromScriptedPlayer(*Monster, *PlayerPawn);
	TestTrue(TEXT("The hit from beyond the returning re-engage range lands"), Monster->GetCombatComponent()->GetCurrentHealth() < HealthBeforeOutsideHit);
	Fixture.Tick(0.5f);
	TestFalse(TEXT("A hit from beyond the returning re-engage range does not pull a returning monster back"), IsSoleMonsterEngaged(*ThinkSubsystem));
	PlayerPawn->SetActorLocation(FVector(EngagementTestInsideLeashPlayerX, 0.f, 0.f));
	Fixture.Tick(0.25f);
	TestFalse(TEXT("A returning monster ignores a player who walks up to it"), IsSoleMonsterEngaged(*ThinkSubsystem));
	const float HomeDistance = FVector::Dist2D(Monster->GetActorLocation(), FVector::ZeroVector);
	TestTrue(FString::Printf(TEXT("The monster is still on its way home (%.0f cm from home)"), HomeDistance), HomeDistance > EngagementTestHomeBand);
	HitMonsterFromScriptedPlayer(*Monster, *PlayerPawn);
	TestTrue(TEXT("A hit from inside the leash re-engages within two steps"), TickUntilSoleMonsterEngages(Fixture, *ThinkSubsystem, EngagementTestEngageWaitSteps));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMonsterAIReturnStallTest, "TDGame.MonsterAI.Engagement.BlockedReturnSettlesInPlace", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMonsterAIReturnStallTest::RunTest(const FString& Parameters)
{
	FTDMonsterAITestWorld Fixture;
	APawn* PlayerPawn = SpawnKitingPlayerPawn(*Fixture.World, FVector(EngagementTestNearPlayerX, 0.f, 0.f));
	ATDMonsterCharacter* Monster = Fixture.SpawnMonster(TEXT("Goblin_Melee"), FVector::ZeroVector);
	UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.World->GetSubsystem<UTDMonsterThinkSubsystem>();
	if (!CheckEngagementTestSetup(*this, Fixture.World, PlayerPawn, Monster, ThinkSubsystem))
	{
		return false;
	}
	if (!TestTrue(TEXT("The monster engages the nearby player"), TickUntilSoleMonsterEngages(Fixture, *ThinkSubsystem, EngagementTestEngageWaitSteps)))
	{
		return false;
	}
	PlayerPawn->SetActorLocation(FVector(StallTestLeashEdgePlayerX, 0.f, 0.f));
	Fixture.Tick(StallTestChaseSeconds);
	PlayerPawn->SetActorLocation(FVector(StallTestFarPlayerX, 0.f, 0.f));
	if (!TestTrue(TEXT("The monster gives up on a player beyond its leash within four seconds"), TickUntilSoleMonsterDisengages(Fixture, *ThinkSubsystem, EngagementTestGiveUpWaitSeconds)))
	{
		return false;
	}
	Monster->DisableMovement();
	const FVector BlockedLocation = Monster->GetActorLocation();
	const float BlockedSpawnDistance = FVector::Dist2D(BlockedLocation, FVector::ZeroVector);
	if (!TestTrue(FString::Printf(TEXT("The monster is blocked more than %.0f cm from its spawn (%.0f)"), StallTestMinBlockedSpawnDistance, BlockedSpawnDistance), BlockedSpawnDistance > StallTestMinBlockedSpawnDistance))
	{
		return false;
	}
	PlayerPawn->SetActorLocation(BlockedLocation + FVector(EngagementTestNearbyPlayerOffset, 0.f, 0.f));
	Fixture.Tick(0.5f);
	TestFalse(TEXT("A returning monster ignores a nearby player before its blocked return settles"), IsSoleMonsterEngaged(*ThinkSubsystem));
	Fixture.Tick(1.f);
	TestFalse(TEXT("A settled monster waits out its arrival settle before engaging"), IsSoleMonsterEngaged(*ThinkSubsystem));
	TestTrue(TEXT("A blocked return settles in place and engages the nearby player within 3.5 seconds of giving up"), TickUntilSoleMonsterEngages(Fixture, *ThinkSubsystem, StallTestEngageWaitSteps));
	const float BlockedDrift = FVector::Dist2D(Monster->GetActorLocation(), BlockedLocation);
	AddInfo(FString::Printf(TEXT("blocked %.0f cm from spawn, drift %.1f cm"), BlockedSpawnDistance, BlockedDrift));
	TestTrue(FString::Printf(TEXT("The blocked monster stays put (%.1f cm)"), BlockedDrift), BlockedDrift < StallTestDriftTolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMonsterAIHitKitingTest, "TDGame.MonsterAI.Engagement.HitKitingIsCappedByMaxChase", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMonsterAIHitKitingTest::RunTest(const FString& Parameters)
{
	FTDMonsterAITestWorld Fixture;
	APawn* PlayerPawn = SpawnKitingPlayerPawn(*Fixture.World, FVector(KitingBotKeepDistance, 0.f, 0.f));
	ATDMonsterCharacter* Monster = Fixture.SpawnMonster(TEXT("Goblin_Melee"), FVector::ZeroVector);
	UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.World->GetSubsystem<UTDMonsterThinkSubsystem>();
	if (!CheckEngagementTestSetup(*this, Fixture.World, PlayerPawn, Monster, ThinkSubsystem))
	{
		return false;
	}
	const FTDKitingRunReport Report = RunKitingBotScenario(Fixture, *PlayerPawn, *Monster, *ThinkSubsystem, HitKitingTestSeconds, HitKitingChaseCeiling, HitKitingIntervalSteps);
	AddInfo(FString::Printf(TEXT("max home distance %.0f cm, hits %d, disengagements %d, far engagements %d"), Report.MaxHomeDistance, Report.Hits, Report.Disengagements, Report.FarEngagements));
	TestTrue(FString::Printf(TEXT("The first kiting hit lands (%d hits)"), Report.Hits), Report.bDidFirstHitLand);
	TestTrue(FString::Printf(TEXT("Repeated hits never drag the monster past %.0f cm from home (%s)"), HitKitingChaseCeiling, *Report.FirstCeilingBreach), Report.MaxHomeDistance <= HitKitingChaseCeiling);
	TestTrue(FString::Printf(TEXT("The hit monster still gives up at its maximum chase distance (%d)"), Report.Disengagements), Report.Disengagements >= 1);
	TestEqual(FString::Printf(TEXT("A disengaged monster away from home never re-engages by proximity (%s)"), *Report.FirstFarEngagement), Report.FarEngagements, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMonsterAIAllyAlertTest, "TDGame.MonsterAI.Engagement.ReturningIgnoresAllyAlert", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMonsterAIAllyAlertTest::RunTest(const FString& Parameters)
{
	FTDMonsterAITestWorld Fixture;
	APawn* PlayerPawn = SpawnKitingPlayerPawn(*Fixture.World, FVector(EngagementTestNearPlayerX, 0.f, 0.f));
	ATDMonsterCharacter* ReturningMonster = Fixture.SpawnMonster(TEXT("Goblin_Melee"), FVector::ZeroVector);
	UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.World->GetSubsystem<UTDMonsterThinkSubsystem>();
	if (!CheckEngagementTestSetup(*this, Fixture.World, PlayerPawn, ReturningMonster, ThinkSubsystem))
	{
		return false;
	}
	if (!DriveSoleMonsterToGiveUp(*this, Fixture, *ThinkSubsystem, *PlayerPawn))
	{
		return false;
	}
	ATDMonsterCharacter* AlertingMonster = Fixture.SpawnMonster(TEXT("Goblin_Melee"), ReturningMonster->GetActorLocation() + FVector(0.f, AllyAlertTestAllyOffsetY, 0.f));
	Fixture.Tick(EngagementTestStepSeconds);
	if (!TestNotNull(TEXT("The alerting monster exists"), AlertingMonster) || !TestEqual(TEXT("Both monster brains are registered"), ThinkSubsystem->GetMonsterDebugLines().Num(), 2))
	{
		return false;
	}
	PlayerPawn->SetActorLocation(FVector(EngagementTestOutsideReturnHitPlayerX, 0.f, 0.f));
	HitMonsterFromScriptedPlayer(*AlertingMonster, *PlayerPawn);
	Fixture.Tick(EngagementTestStepSeconds * EngagementTestEngageWaitSteps);
	TestTrue(TEXT("The hit ally engages"), IsMonsterLineEngaged(*ThinkSubsystem, 1));
	TestFalse(TEXT("A returning monster ignores an ally's alert"), IsMonsterLineEngaged(*ThinkSubsystem, 0));
	return true;
}

#endif
