#if WITH_DEV_AUTOMATION_TESTS

#include "Characters/TDMonsterCharacter.h"
#include "Combat/Damage/TDStatusDefinition.h"
#include "Combat/TDCombatComponent.h"
#include "Components/SphereComponent.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
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

	const TSharedPtr<const FTDResolvedMonsterDefinition> Ogre = Library->FindDefinition(TEXT("Ogre_Boss"));
	if (!TestTrue(TEXT("Ogre_Boss resolves"), Ogre.IsValid()))
	{
		return false;
	}
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

#endif
