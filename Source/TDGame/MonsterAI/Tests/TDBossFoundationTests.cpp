#if WITH_DEV_AUTOMATION_TESTS

#include "Characters/TDMonsterCharacter.h"
#include "Combat/Damage/TDDamageDefinition.h"
#include "Combat/Damage/TDDamageEntity.h"
#include "Combat/Damage/TDDamageSubsystem.h"
#include "Combat/TDCombatComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "MonsterAI/TDMonsterDefinitionLibrary.h"
#include "MonsterAI/TDMonsterSpeciesAsset.h"
#include "MonsterAI/TDMonsterThinkSubsystem.h"
#include "MoverSimulationTypes.h"
#include "Tickable.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	constexpr float BossTestStepSeconds = 1.f / 64.f;
	constexpr int32 BossTestStepsPerSecond = 64;
	constexpr int32 BossTestEnemyTeam = 1;
	constexpr int32 BossTestBossTeam = 2;
	constexpr float BossTestTargetHealth = 1000.f;
	constexpr float BossTestTargetRadius = 30.f;
	constexpr float BossTestFloorHalfExtent = 5000.f;
	constexpr float BossTestFloorHalfThickness = 50.f;
	constexpr float BossTestSpawnClearance = 2.f;
	constexpr float BossTestSpellHalfHeight = 150.f;
	constexpr float BossTestLethalDamage = 100000.f;
	constexpr float BossTestShapeSettleSeconds = 0.15f;
	constexpr float BossTestShapeExpireSeconds = 0.1f;
	constexpr float BossTestShapeLifetimeSeconds = 0.2f;
	constexpr float BossTestShapeDamage = 10.f;
	constexpr float BossTestFlightSeconds = 6.f;
	constexpr float BossTestFlightHoldSampleSeconds = 2.f;
	constexpr float BossTestFlightMinimumRise = 250.f;
	constexpr float BossTestFlightMaximumRise = 350.f;
	constexpr float BossTestLandedTolerance = 20.f;
	constexpr int32 BossTestSummonCount = 3;
	constexpr float BossTestSummonRadius = 400.f;
	constexpr float BossTestSummonRadiusTolerance = 80.f;
	constexpr float BossTestSummonWindowSeconds = 2.f;
	constexpr float BossTestAloneWindowSeconds = 1.f;
	constexpr float BossTestDangerSeconds = 8.f;
	constexpr float BossTestDangerLifetimeSeconds = 1.2f;
	constexpr float BossTestDangerGapSeconds = 0.9f;
	constexpr int32 BossTestDangerMinimumStarts = 3;
	constexpr int32 BossTestControlMinimumOverlap = 2;
	constexpr float BossTestPhaseReactionSeconds = 1.f;
	constexpr float BossTestPhaseDuplicateSeconds = 0.25f;
	constexpr float BossTestPhaseMarkTolerance = 0.001f;
	constexpr float BossTestWindupSeconds = 0.8f;
	constexpr float BossTestWindupEarlySeconds = 0.5f;
	constexpr float BossTestWindupLateSeconds = 1.2f;
	constexpr float BossTestWindupCastWaitSeconds = 1.f;
	const TCHAR* const BossTestMinionDefinitionId = TEXT("Goblin_Melee");
	const TCHAR* const BossTestThunderCagePath = TEXT("/Game/Combat/MegaMagic/DA_TDThunderCage.DA_TDThunderCage");

	struct FTDBossTestWorld
	{
		FTDBossTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDBossTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDBossTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		void SpawnFloor() const
		{
			AActor* Floor = World->SpawnActor<AActor>();
			UBoxComponent* FloorBox = NewObject<UBoxComponent>(Floor);
			Floor->AddInstanceComponent(FloorBox);
			Floor->SetRootComponent(FloorBox);
			FloorBox->SetBoxExtent(FVector(BossTestFloorHalfExtent, BossTestFloorHalfExtent, BossTestFloorHalfThickness));
			FloorBox->SetCollisionObjectType(ECC_WorldStatic);
			FloorBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			FloorBox->SetCollisionResponseToAllChannels(ECR_Block);
			FloorBox->RegisterComponent();
			Floor->SetActorLocation(FVector(0.f, 0.f, -BossTestFloorHalfThickness));
		}

		UTDCombatComponent* SpawnCombatant(const FVector& Location, const int32 TeamId) const
		{
			AActor* Actor = World->SpawnActor<AActor>();
			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Actor->AddInstanceComponent(Sphere);
			Actor->SetRootComponent(Sphere);
			Sphere->SetSphereRadius(BossTestTargetRadius);
			Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Sphere->SetCollisionObjectType(ECC_Pawn);
			Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
			Sphere->RegisterComponent();
			Actor->SetActorLocation(Location);
			UTDCombatComponent* Combatant = NewObject<UTDCombatComponent>(Actor);
			Actor->AddInstanceComponent(Combatant);
			FTDCombatStats Stats;
			Stats.TeamId = TeamId;
			Stats.BaseMaxHealth = BossTestTargetHealth;
			Combatant->SetStats(Stats);
			Combatant->RegisterComponent();
			return Combatant;
		}

		ATDMonsterCharacter* SpawnProbe(const FName MovementMode, const FVector2D& PlanarLocation) const
		{
			const float RestHeight = GetDefault<ATDMonsterCharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + BossTestSpawnClearance;
			const FTransform SpawnTransform(FRotator::ZeroRotator, FVector(PlanarLocation.X, PlanarLocation.Y, RestHeight));
			ATDMonsterCharacter* Probe = World->SpawnActorDeferred<ATDMonsterCharacter>(ATDMonsterCharacter::StaticClass(), SpawnTransform);
			if (!Probe)
			{
				return nullptr;
			}
			Probe->GetMoverComponent()->StartingMovementMode = MovementMode;
			Probe->FinishSpawning(SpawnTransform);
			return Probe;
		}

		void TickStep() const
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, BossTestStepSeconds);
		}

		void TickStepAndCompleteAssets() const
		{
			TickStep();
			FlushAsyncLoading();
			FTickableGameObject::TickObjects(nullptr, LEVELTICK_All, false, BossTestStepSeconds);
		}

		void Tick(const float Seconds) const
		{
			const int32 StepCount = FMath::RoundToInt(Seconds * BossTestStepsPerSecond);
			for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
			{
				TickStep();
			}
		}

		UWorld* World = nullptr;
	};

	struct FTDBossTestProbe
	{
		ATDMonsterCharacter* Actor = nullptr;
		UTDMonsterThinkSubsystem* ThinkSubsystem = nullptr;
		FTDMonsterSlotHandle Handle;
	};

	struct FTDBossShapeCase
	{
		const TCHAR* Label;
		float X;
		float Y;
		bool bShouldHit;
	};

	struct FTDBossPhaseEvent
	{
		TWeakObjectPtr<AActor> Actor;
		int32 PhaseIndex = INDEX_NONE;
		FName PhaseId;
	};

	struct FTDBossPhaseStage
	{
		float Health;
		int32 PhaseIndex;
		const TCHAR* PhaseName;
	};

	struct FTDBossDangerRun
	{
		FString SetupError;
		int32 MaximumAliveEntities = 0;
		TArray<int32> CastStartTicks;
	};

	FString GetFirstBossTestError(const TArray<FString>& Errors)
	{
		return Errors.IsEmpty() ? FString(TEXT("none")) : Errors[0];
	}

	TStrongObjectPtr<UTDDamageDefinition> MakeBossTestAreaSpell(const float Radius, const float ActivationDelaySeconds, const float LifetimeSeconds, const float Damage)
	{
		TStrongObjectPtr<UTDDamageDefinition> Spell(NewObject<UTDDamageDefinition>(GetTransientPackage()));
		Spell->Mode = ETDDamageEntityMode::Area;
		Spell->AreaShape = ETDDamageAreaShape::Circle;
		Spell->Radius = Radius;
		Spell->ActivationDelay = ActivationDelaySeconds;
		Spell->Lifetime = LifetimeSeconds;
		Spell->MaxHitsPerTarget = 1;
		Spell->bProjectToGround = false;
		Spell->HalfHeight = BossTestSpellHalfHeight;
		FTDDamageAction DamageAction;
		DamageAction.Type = ETDDamageActionType::Damage;
		DamageAction.Magnitude.Base = Damage;
		DamageAction.Magnitude.AttackRatio = 0.f;
		FTDDamageRule HitRule;
		HitRule.Event = ETDDamageEvent::Hit;
		HitRule.Actions.Add(DamageAction);
		Spell->Rules.Add(HitRule);
		return Spell;
	}

	FString MakeBossProbeText(const TCHAR* DefinitionId, const bool bIsBoss, const TArray<FString>& Sections)
	{
		return FString::Printf(TEXT(R"({ "schema": 1, "id": "%s", "boss": %s,
  "stats": { "max_health": 1000, "attack_power": 1, "armor": 0, "move_speed": 400, "team": 2 },
  %s })"), DefinitionId, bIsBoss ? TEXT("true") : TEXT("false"), *FString::Join(Sections, TEXT(",\n  ")));
	}

	FString MakeBossFlightProbeText()
	{
		TArray<FString> Sections;
		Sections.Add(TEXT(R"("sequences": { "Flight": [
    { "do": "TakeOff", "args": { "altitude": 300, "seconds": 1.0 } },
    { "do": "Wait", "args": { "seconds": 1.5 } },
    { "do": "Land", "args": { "seconds": 1.0 } } ] })"));
		Sections.Add(TEXT(R"("actions": [ { "id": "Fly", "do": "PlaySequence", "args": { "sequence": "Flight" }, "weight": 1, "cooldown": 99 } ])"));
		return MakeBossProbeText(TEXT("BossFlightProbe"), false, Sections);
	}

	FString MakeBossSummonProbeText()
	{
		TArray<FString> Sections;
		Sections.Add(TEXT(R"("actions": [
    { "id": "Summon", "do": "SummonMinions", "args": { "species": "Goblin", "count": 3, "radius": 400 }, "weight": 5, "cooldown": 99 },
    { "id": "Crowded", "do": "Wait", "weight": 1,
      "considerations": [ { "input": "MinionsAlive", "curve": "Binary", "c": 0.3, "range": [0, 8] } ] },
    { "id": "Alone", "do": "Wait", "weight": 0.2,
      "considerations": [ { "input": "MinionsAlive", "curve": "Linear", "m": -1, "b": 1, "range": [0, 1] } ] } ])"));
		return MakeBossProbeText(TEXT("BossSummonProbe"), true, Sections);
	}

	FString MakeBossDangerProbeText(const bool bIsDanger)
	{
		TArray<FString> Sections;
		Sections.Add(FString::Printf(TEXT(R"("abilities": { "Breath": { "spell": "DA_BossBreath", "range": 600, "cooldown": 0.5%s } })"), bIsDanger ? TEXT(", \"danger\": true") : TEXT("")));
		Sections.Add(TEXT(R"("actions": [
    { "id": "Breathe", "do": "CastAbility", "args": { "ability": "Breath" }, "weight": 5,
      "considerations": [ { "input": "AbilityReady", "args": { "ability": "Breath" }, "curve": "Binary" } ] },
    { "id": "Idle", "do": "Wait", "weight": 0.05 } ])"));
		return MakeBossProbeText(TEXT("BossDangerProbe"), true, Sections);
	}

	FString MakeBossPhaseProbeText()
	{
		TArray<FString> Sections;
		Sections.Add(TEXT(R"("actions": [ { "id": "Idle", "do": "Wait", "weight": 1 } ])"));
		Sections.Add(TEXT(R"("phases": [
    { "id": "Sky", "enter_when": { "input": "SelfHealthRatio", "op": "<=", "value": 0.7 } },
    { "id": "Enraged", "enter_when": { "input": "SelfHealthRatio", "op": "<=", "value": 0.4 } },
    { "id": "Inferno", "enter_when": { "input": "SelfHealthRatio", "op": "<=", "value": 0.25 } } ])"));
		return MakeBossProbeText(TEXT("BossPhaseProbe"), true, Sections);
	}

	FString MakeBossWindupProbeText()
	{
		TArray<FString> Sections;
		Sections.Add(TEXT(R"("abilities": { "Snap": { "spell": "DA_BossSnap", "range": 300, "cooldown": 5, "windup": 0.8 } })"));
		Sections.Add(TEXT(R"("actions": [
    { "id": "Snap", "do": "CastAbility", "args": { "ability": "Snap" }, "weight": 5,
      "considerations": [ { "input": "AbilityReady", "args": { "ability": "Snap" }, "curve": "Binary" } ] },
    { "id": "Idle", "do": "Wait", "weight": 0.05 } ])"));
		return MakeBossProbeText(TEXT("BossWindupProbe"), true, Sections);
	}

	FString MakeBossKeysProbeText(const FString& StanceText, const FString& CooldownScaleText, const FString& SpeciesText, const FString& CountText)
	{
		TArray<FString> Sections;
		Sections.Add(FString::Printf(TEXT(R"("abilities": { "Snap": { "spell": "DA_BossAny", "range": 300, "cooldown": 1, "windup": 0.7, "danger": true, "stance": %s } })"), *StanceText));
		Sections.Add(FString::Printf(TEXT(R"("sequences": { "Rise": [
    { "do": "TakeOff", "args": { "altitude": 420, "seconds": 1.5 } },
    { "do": "Land", "args": { "seconds": 0.9 } },
    { "do": "SummonMinions", "args": { "species": %s, "count": %s, "radius": 500, "seconds": 1.1 } } ] })"), *SpeciesText, *CountText));
		Sections.Add(TEXT(R"("actions": [ { "id": "Idle", "do": "Wait", "weight": 1 } ])"));
		Sections.Add(FString::Printf(TEXT(R"("phases": [
    { "id": "Fast", "enter_when": { "input": "SelfHealthRatio", "op": "<=", "value": 0.5 }, "cooldown_scale": %s } ])"), *CooldownScaleText));
		return MakeBossProbeText(TEXT("BossKeysProbe"), true, Sections);
	}

	TSharedPtr<const FTDResolvedMonsterDefinition> ResolveBossTestDefinition(const FString& DefinitionId, const FString& Text, const TMap<FName, UTDDamageDefinition*>& Spells, TArray<FString>& OutErrors)
	{
		TMap<FString, FString> Texts;
		Texts.Add(DefinitionId + TEXT(".json"), Text);
		TMap<FName, TSharedPtr<const FTDResolvedMonsterDefinition>> Definitions;
		UTDMonsterDefinitionLibrary::ResolveDefinitionTexts(Texts, [&Spells](const FName SpellName) -> UTDDamageDefinition*
		{
			UTDDamageDefinition* const* Spell = Spells.Find(SpellName);
			return Spell ? *Spell : nullptr;
		}, Definitions, OutErrors);
		const TSharedPtr<const FTDResolvedMonsterDefinition>* Definition = Definitions.Find(FName(*DefinitionId));
		return Definition ? *Definition : nullptr;
	}

	bool ResolveBossTestProbe(FAutomationTestBase& Test, const TCHAR* DefinitionId, const FString& Text, const TMap<FName, UTDDamageDefinition*>& Spells, TSharedPtr<const FTDResolvedMonsterDefinition>& OutDefinition)
	{
		TArray<FString> Errors;
		OutDefinition = ResolveBossTestDefinition(DefinitionId, Text, Spells, Errors);
		return Test.TestTrue(FString::Printf(TEXT("%s resolves without errors (errors %d, first: %s)"), DefinitionId, Errors.Num(), *GetFirstBossTestError(Errors)), OutDefinition.IsValid() && Errors.IsEmpty());
	}

	bool SpawnRegisteredBossTestProbe(FAutomationTestBase& Test, const FTDBossTestWorld& Fixture, const TSharedPtr<const FTDResolvedMonsterDefinition>& Definition, const FName MovementMode, const FVector2D& PlanarLocation, FTDBossTestProbe& OutProbe)
	{
		OutProbe.ThinkSubsystem = Fixture.World->GetSubsystem<UTDMonsterThinkSubsystem>();
		OutProbe.Actor = Fixture.SpawnProbe(MovementMode, PlanarLocation);
		if (!Test.TestNotNull(TEXT("The think subsystem exists"), OutProbe.ThinkSubsystem) || !Test.TestNotNull(TEXT("The probe spawns"), OutProbe.Actor))
		{
			return false;
		}
		OutProbe.Handle = OutProbe.ThinkSubsystem->RegisterMonster(OutProbe.Actor, Definition);
		return Test.TestTrue(TEXT("The probe registers with the think subsystem"), OutProbe.Handle.IsValid());
	}

	int32 CountBossTestEntities(UWorld* World, const UTDDamageDefinition* Definition, const bool bShouldMatchDefinition)
	{
		int32 Count = 0;
		for (TActorIterator<ATDDamageEntity> It(World); It; ++It)
		{
			const bool bIsAlive = IsValid(*It) && !It->IsActorBeingDestroyed() && !It->IsGameplayFinished();
			const bool bMatchesDefinition = It->GetDefinition() == Definition;
			Count += bIsAlive && bMatchesDefinition == bShouldMatchDefinition ? 1 : 0;
		}
		return Count;
	}

	int32 CountBossTestEntitiesWithDefinition(UWorld* World, const UTDDamageDefinition* Definition)
	{
		return CountBossTestEntities(World, Definition, true);
	}

	int32 CountBossTestEntitiesWithoutDefinition(UWorld* World, const UTDDamageDefinition* Definition)
	{
		return CountBossTestEntities(World, Definition, false);
	}

	void CollectBossTestMinions(UWorld* World, TArray<ATDMonsterCharacter*>& OutMinions)
	{
		OutMinions.Reset();
		for (TActorIterator<ATDMonsterCharacter> It(World); It; ++It)
		{
			const UTDMonsterSpeciesAsset* Species = It->GetSpecies();
			if (IsValid(*It) && Species && Species->DefinitionId == FName(BossTestMinionDefinitionId))
			{
				OutMinions.Add(*It);
			}
		}
	}

	bool IsBossTestMonsterEngaged(const UTDMonsterThinkSubsystem& ThinkSubsystem, const ATDMonsterCharacter& Monster)
	{
		const FString ActorMarker = FString::Printf(TEXT("actor=%s "), *Monster.GetName());
		for (const FString& Line : ThinkSubsystem.GetMonsterDebugLines())
		{
			if (Line.Contains(ActorMarker))
			{
				return Line.Contains(TEXT("engaged=1"));
			}
		}
		return false;
	}

	void LowerBossTestHealth(ATDMonsterCharacter& Probe, UTDCombatComponent& Attacker, const float DesiredHealth)
	{
		UTDCombatComponent* Combatant = Probe.GetCombatComponent();
		FTDDamageContext Context;
		Context.Caster = Attacker.GetOwner();
		Context.Stats = Attacker.GetStats();
		Combatant->ReceiveDamage(Combatant->GetCurrentHealth() - DesiredHealth, ETDDamageElement::Physical, false, Context);
	}

	void RunBossShapeCases(FAutomationTestBase& Test, const FTDBossTestWorld& Fixture, AActor& Caster, UTDDamageDefinition& Shape, const TCHAR* ShapeName, TConstArrayView<FTDBossShapeCase> Cases)
	{
		UTDDamageSubsystem* DamageSubsystem = Fixture.World->GetSubsystem<UTDDamageSubsystem>();
		if (!Test.TestNotNull(TEXT("The damage subsystem exists"), DamageSubsystem))
		{
			return;
		}
		TArray<UTDCombatComponent*> Targets;
		for (const FTDBossShapeCase& Case : Cases)
		{
			Targets.Add(Fixture.SpawnCombatant(FVector(Case.X, Case.Y, 0.f), BossTestEnemyTeam));
		}
		const bool bHasCast = DamageSubsystem->Cast(&Shape, &Caster, FVector::ZeroVector, FVector(1000.f, 0.f, 0.f)) != nullptr;
		Test.TestTrue(FString::Printf(TEXT("%s cast spawns an entity"), ShapeName), bHasCast);
		Fixture.Tick(BossTestShapeSettleSeconds);
		for (int32 CaseIndex = 0; CaseIndex < Cases.Num(); ++CaseIndex)
		{
			const FTDBossShapeCase& Case = Cases[CaseIndex];
			const float Health = Targets[CaseIndex]->GetCurrentHealth();
			const bool bWasHit = Health < BossTestTargetHealth;
			Test.TestTrue(FString::Printf(TEXT("%s target %s %s (health %.1f)"), ShapeName, Case.Label, Case.bShouldHit ? TEXT("is hit") : TEXT("is not hit"), Health), bWasHit == Case.bShouldHit);
		}
		for (UTDCombatComponent* Target : Targets)
		{
			Target->GetOwner()->Destroy();
		}
		Fixture.Tick(BossTestShapeExpireSeconds);
	}

	FTDBossDangerRun RunBossDangerScenario(const bool bIsDanger)
	{
		FTDBossDangerRun Run;
		TStrongObjectPtr<UTDDamageDefinition> Breath = MakeBossTestAreaSpell(150.f, 0.6f, BossTestDangerLifetimeSeconds, 0.1f);
		TMap<FName, UTDDamageDefinition*> Spells;
		Spells.Add(FName(TEXT("DA_BossBreath")), Breath.Get());
		FTDBossTestWorld Fixture;
		Fixture.SpawnCombatant(FVector::ZeroVector, BossTestEnemyTeam);
		TArray<FString> Errors;
		const TSharedPtr<const FTDResolvedMonsterDefinition> Definition = ResolveBossTestDefinition(TEXT("BossDangerProbe"), MakeBossDangerProbeText(bIsDanger), Spells, Errors);
		if (!Definition.IsValid() || !Errors.IsEmpty())
		{
			Run.SetupError = FString::Printf(TEXT("the definition did not resolve (errors %d, first: %s)"), Errors.Num(), *GetFirstBossTestError(Errors));
			return Run;
		}
		UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.World->GetSubsystem<UTDMonsterThinkSubsystem>();
		if (!ThinkSubsystem)
		{
			Run.SetupError = TEXT("the think subsystem is missing");
			return Run;
		}
		const FVector2D ProbeLocations[] = { FVector2D(300.f, 0.f), FVector2D(-300.f, 0.f) };
		TArray<FTDMonsterSlotHandle> Handles;
		for (const FVector2D& ProbeLocation : ProbeLocations)
		{
			ATDMonsterCharacter* Probe = Fixture.SpawnProbe(DefaultModeNames::Flying, ProbeLocation);
			const FTDMonsterSlotHandle Handle = Probe ? ThinkSubsystem->RegisterMonster(Probe, Definition) : FTDMonsterSlotHandle();
			if (!Handle.IsValid())
			{
				Run.SetupError = TEXT("a boss probe did not register");
				return Run;
			}
			Handles.Add(Handle);
		}
		TArray<bool> WasCasting;
		WasCasting.Init(false, Handles.Num());
		const int32 TickCount = FMath::RoundToInt(BossTestDangerSeconds * BossTestStepsPerSecond);
		for (int32 TickIndex = 0; TickIndex < TickCount; ++TickIndex)
		{
			Fixture.TickStep();
			Run.MaximumAliveEntities = FMath::Max(Run.MaximumAliveEntities, CountBossTestEntitiesWithDefinition(Fixture.World, Breath.Get()));
			for (int32 HandleIndex = 0; HandleIndex < Handles.Num(); ++HandleIndex)
			{
				const bool bIsCasting = ThinkSubsystem->GetMonsterState(Handles[HandleIndex]) == ETDMonsterFsmState::Cast;
				if (bIsCasting && !WasCasting[HandleIndex])
				{
					Run.CastStartTicks.Add(TickIndex);
				}
				WasCasting[HandleIndex] = bIsCasting;
			}
		}
		return Run;
	}

	void CheckBossKeyRejected(FAutomationTestBase& Test, const TMap<FName, UTDDamageDefinition*>& Spells, const TCHAR* Label, const TCHAR* ErrorKey, const FString& StanceText, const FString& CooldownScaleText, const FString& SpeciesText, const FString& CountText)
	{
		TArray<FString> Errors;
		ResolveBossTestDefinition(TEXT("BossKeysProbe"), MakeBossKeysProbeText(StanceText, CooldownScaleText, SpeciesText, CountText), Spells, Errors);
		const bool bNamesKey = Errors.ContainsByPredicate([ErrorKey](const FString& Error) { return Error.Contains(ErrorKey); });
		Test.TestTrue(FString::Printf(TEXT("%s is rejected with an error naming %s (errors %d, first: %s)"), Label, ErrorKey, Errors.Num(), *GetFirstBossTestError(Errors)), bNamesKey);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDBossShapesTest, "TDGame.MonsterAI.Boss.ConeLineRingShapes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDBossShapesTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UTDDamageDefinition> Cone = MakeBossTestAreaSpell(600.f, 0.f, BossTestShapeLifetimeSeconds, BossTestShapeDamage);
	Cone->AreaShape = ETDDamageAreaShape::Cone;
	Cone->ConeHalfAngleDegrees = 30.f;
	TStrongObjectPtr<UTDDamageDefinition> Line = MakeBossTestAreaSpell(800.f, 0.f, BossTestShapeLifetimeSeconds, BossTestShapeDamage);
	Line->AreaShape = ETDDamageAreaShape::Line;
	Line->LineHalfWidth = 100.f;
	TStrongObjectPtr<UTDDamageDefinition> Ring = MakeBossTestAreaSpell(600.f, 0.f, BossTestShapeLifetimeSeconds, BossTestShapeDamage);
	Ring->AreaShape = ETDDamageAreaShape::Ring;
	Ring->InnerRadius = 200.f;

	FTDBossTestWorld Fixture;
	UTDCombatComponent* Caster = Fixture.SpawnCombatant(FVector::ZeroVector, BossTestBossTeam);
	if (!TestNotNull(TEXT("The caster exists"), Caster))
	{
		return false;
	}
	const FTDBossShapeCase ConeCases[] =
	{
		{ TEXT("ahead (400,0)"), 400.f, 0.f, true },
		{ TEXT("outside the half angle (400,300)"), 400.f, 300.f, false },
		{ TEXT("beyond the radius (700,0)"), 700.f, 0.f, false },
		{ TEXT("behind (-200,0)"), -200.f, 0.f, false }
	};
	const FTDBossShapeCase LineCases[] =
	{
		{ TEXT("inside the band (500,50)"), 500.f, 50.f, true },
		{ TEXT("outside the half width (500,180)"), 500.f, 180.f, false },
		{ TEXT("behind the origin (-100,0)"), -100.f, 0.f, false },
		{ TEXT("beyond the length (900,0)"), 900.f, 0.f, false }
	};
	const FTDBossShapeCase RingCases[] =
	{
		{ TEXT("inside the inner radius (100,0)"), 100.f, 0.f, false },
		{ TEXT("inside the ring (400,0)"), 400.f, 0.f, true },
		{ TEXT("beyond the outer radius (700,0)"), 700.f, 0.f, false }
	};
	RunBossShapeCases(*this, Fixture, *Caster->GetOwner(), *Cone, TEXT("Cone"), ConeCases);
	RunBossShapeCases(*this, Fixture, *Caster->GetOwner(), *Line, TEXT("Line"), LineCases);
	RunBossShapeCases(*this, Fixture, *Caster->GetOwner(), *Ring, TEXT("Ring"), RingCases);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDBossTakeOffTest, "TDGame.MonsterAI.Boss.TakeOffHoldsAltitudeThenLands", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDBossTakeOffTest::RunTest(const FString& Parameters)
{
	FTDBossTestWorld Fixture;
	Fixture.SpawnFloor();
	Fixture.SpawnCombatant(FVector(600.f, 0.f, 0.f), BossTestEnemyTeam);
	TSharedPtr<const FTDResolvedMonsterDefinition> Definition;
	if (!ResolveBossTestProbe(*this, TEXT("BossFlightProbe"), MakeBossFlightProbeText(), TMap<FName, UTDDamageDefinition*>(), Definition))
	{
		return false;
	}
	FTDBossTestProbe Probe;
	if (!SpawnRegisteredBossTestProbe(*this, Fixture, Definition, DefaultModeNames::Walking, FVector2D::ZeroVector, Probe))
	{
		return false;
	}

	const float StartHeight = Probe.Actor->GetActorLocation().Z;
	const int32 TickCount = FMath::RoundToInt(BossTestFlightSeconds * BossTestStepsPerSecond);
	const int32 HoldSampleTick = FMath::RoundToInt(BossTestFlightHoldSampleSeconds * BossTestStepsPerSecond);
	float MaximumRise = 0.f;
	float HoldRise = 0.f;
	bool bSawFlying = false;
	for (int32 TickIndex = 1; TickIndex <= TickCount; ++TickIndex)
	{
		Fixture.TickStep();
		const float Rise = Probe.Actor->GetActorLocation().Z - StartHeight;
		MaximumRise = FMath::Max(MaximumRise, Rise);
		bSawFlying |= Probe.ThinkSubsystem->IsMonsterFlying(Probe.Handle);
		HoldRise = TickIndex == HoldSampleTick ? Rise : HoldRise;
	}
	const float FinalOffset = Probe.Actor->GetActorLocation().Z - StartHeight;
	TestTrue(TEXT("The brain reports flight during the sequence"), bSawFlying);
	TestTrue(FString::Printf(TEXT("The highest point is 250 to 350 cm above the start (rise %.1f)"), MaximumRise), MaximumRise >= BossTestFlightMinimumRise && MaximumRise <= BossTestFlightMaximumRise);
	TestTrue(FString::Printf(TEXT("The altitude is held in the middle of the sequence (rise %.1f at %.1f s)"), HoldRise, BossTestFlightHoldSampleSeconds), HoldRise >= BossTestFlightMinimumRise && HoldRise <= BossTestFlightMaximumRise);
	TestTrue(FString::Printf(TEXT("The probe is back on the ground after %.0f s (offset %.1f)"), BossTestFlightSeconds, FinalOffset), FMath::Abs(FinalOffset) <= BossTestLandedTolerance);
	TestEqual(TEXT("The mover walks again after landing"), Probe.Actor->GetMoverComponent()->GetMovementModeName(), DefaultModeNames::Walking);
	TestFalse(TEXT("The brain no longer reports flight after landing"), Probe.ThinkSubsystem->IsMonsterFlying(Probe.Handle));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDBossSummonTest, "TDGame.MonsterAI.Boss.SummonMinionsSpawnsEngagedRing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDBossSummonTest::RunTest(const FString& Parameters)
{
	FTDBossTestWorld Fixture;
	Fixture.SpawnFloor();
	UTDCombatComponent* Target = Fixture.SpawnCombatant(FVector(600.f, 0.f, 0.f), BossTestEnemyTeam);
	TSharedPtr<const FTDResolvedMonsterDefinition> Definition;
	if (!ResolveBossTestProbe(*this, TEXT("BossSummonProbe"), MakeBossSummonProbeText(), TMap<FName, UTDDamageDefinition*>(), Definition))
	{
		return false;
	}
	FTDBossTestProbe Boss;
	if (!SpawnRegisteredBossTestProbe(*this, Fixture, Definition, DefaultModeNames::Walking, FVector2D::ZeroVector, Boss))
	{
		return false;
	}

	TArray<ATDMonsterCharacter*> Minions;
	int32 SpawnTick = INDEX_NONE;
	bool bSawCrowded = false;
	bool bAreAllEngagedAfterOneSecond = true;
	const int32 WindowTicks = FMath::RoundToInt(BossTestSummonWindowSeconds * BossTestStepsPerSecond);
	for (int32 TickIndex = 0; TickIndex < WindowTicks; ++TickIndex)
	{
		Fixture.TickStepAndCompleteAssets();
		bSawCrowded |= Boss.ThinkSubsystem->GetMonsterActionId(Boss.Handle) == FName(TEXT("Crowded"));
		if (SpawnTick == INDEX_NONE)
		{
			CollectBossTestMinions(Fixture.World, Minions);
			if (Minions.IsEmpty())
			{
				continue;
			}
			SpawnTick = TickIndex;
			TestEqual(TEXT("The summon spawns exactly three Goblin monsters"), Minions.Num(), BossTestSummonCount);
			for (const ATDMonsterCharacter* Minion : Minions)
			{
				const float RingDistance = FVector::Dist2D(Boss.Actor->GetActorLocation(), Minion->GetActorLocation());
				TestTrue(FString::Printf(TEXT("%s stands on the 400 cm ring (distance %.1f)"), *Minion->GetName(), RingDistance), FMath::Abs(RingDistance - BossTestSummonRadius) <= BossTestSummonRadiusTolerance);
				TestTrue(FString::Printf(TEXT("%s is engaged at once"), *Minion->GetName()), IsBossTestMonsterEngaged(*Boss.ThinkSubsystem, *Minion));
			}
			continue;
		}
		if (TickIndex == SpawnTick + BossTestStepsPerSecond)
		{
			for (const ATDMonsterCharacter* Minion : Minions)
			{
				bAreAllEngagedAfterOneSecond &= IsBossTestMonsterEngaged(*Boss.ThinkSubsystem, *Minion);
			}
		}
	}
	if (!TestTrue(TEXT("The minions appear within two seconds"), SpawnTick != INDEX_NONE))
	{
		return false;
	}
	TestTrue(TEXT("The minions stay engaged one second after the summon"), bAreAllEngagedAfterOneSecond);
	TestTrue(TEXT("The boss picks Crowded while the minions live"), bSawCrowded);

	FTDDamageContext KillContext;
	KillContext.Caster = Target->GetOwner();
	KillContext.Stats = Target->GetStats();
	for (ATDMonsterCharacter* Minion : Minions)
	{
		if (IsValid(Minion))
		{
			Minion->GetCombatComponent()->ReceiveDamage(BossTestLethalDamage, ETDDamageElement::Physical, false, KillContext);
		}
	}
	bool bSawAlone = false;
	const int32 AloneTicks = FMath::RoundToInt(BossTestAloneWindowSeconds * BossTestStepsPerSecond);
	for (int32 TickIndex = 0; TickIndex < AloneTicks && !bSawAlone; ++TickIndex)
	{
		Fixture.TickStepAndCompleteAssets();
		bSawAlone = Boss.ThinkSubsystem->GetMonsterActionId(Boss.Handle) == FName(TEXT("Alone"));
	}
	TestTrue(TEXT("The boss picks Alone within one second of losing its minions"), bSawAlone);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDBossDangerTest, "TDGame.MonsterAI.Boss.DangerAttacksNeverOverlap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDBossDangerTest::RunTest(const FString& Parameters)
{
	const FTDBossDangerRun DangerRun = RunBossDangerScenario(true);
	if (!TestTrue(FString::Printf(TEXT("The danger run sets up (%s)"), *DangerRun.SetupError), DangerRun.SetupError.IsEmpty()))
	{
		return false;
	}
	const FTDBossDangerRun ControlRun = RunBossDangerScenario(false);
	if (!TestTrue(FString::Printf(TEXT("The control run sets up (%s)"), *ControlRun.SetupError), ControlRun.SetupError.IsEmpty()))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("danger run: max alive %d, starts %d; control run: max alive %d, starts %d"), DangerRun.MaximumAliveEntities, DangerRun.CastStartTicks.Num(), ControlRun.MaximumAliveEntities, ControlRun.CastStartTicks.Num()));
	TestTrue(FString::Printf(TEXT("Without the danger flag the same probes overlap (max alive %d), so the setup exercises the rule"), ControlRun.MaximumAliveEntities), ControlRun.MaximumAliveEntities >= BossTestControlMinimumOverlap);
	TestTrue(FString::Printf(TEXT("Danger attacks never overlap on one target (max alive %d)"), DangerRun.MaximumAliveEntities), DangerRun.MaximumAliveEntities <= 1);
	TestTrue(FString::Printf(TEXT("At least %d danger casts start in %.0f s (started %d)"), BossTestDangerMinimumStarts, BossTestDangerSeconds, DangerRun.CastStartTicks.Num()), DangerRun.CastStartTicks.Num() >= BossTestDangerMinimumStarts);
	const float MinimumGapSeconds = BossTestDangerLifetimeSeconds + BossTestDangerGapSeconds;
	for (int32 StartIndex = 1; StartIndex < DangerRun.CastStartTicks.Num(); ++StartIndex)
	{
		const float GapSeconds = (DangerRun.CastStartTicks[StartIndex] - DangerRun.CastStartTicks[StartIndex - 1]) * BossTestStepSeconds;
		TestTrue(FString::Printf(TEXT("Danger cast starts %d and %d are %.2f s apart, at least %.1f s"), StartIndex - 1, StartIndex, GapSeconds, MinimumGapSeconds), GapSeconds >= MinimumGapSeconds);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDBossPhaseTest, "TDGame.MonsterAI.Boss.PhaseEnteredNotifiesAndQueries", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDBossPhaseTest::RunTest(const FString& Parameters)
{
	FTDBossTestWorld Fixture;
	UTDCombatComponent* Target = Fixture.SpawnCombatant(FVector(600.f, 0.f, 0.f), BossTestEnemyTeam);
	TSharedPtr<const FTDResolvedMonsterDefinition> Definition;
	if (!ResolveBossTestProbe(*this, TEXT("BossPhaseProbe"), MakeBossPhaseProbeText(), TMap<FName, UTDDamageDefinition*>(), Definition))
	{
		return false;
	}
	FTDBossTestProbe Probe;
	if (!SpawnRegisteredBossTestProbe(*this, Fixture, Definition, DefaultModeNames::Flying, FVector2D::ZeroVector, Probe))
	{
		return false;
	}

	TArray<FTDBossPhaseEvent> Events;
	const FDelegateHandle PhaseHandle = Probe.ThinkSubsystem->OnMonsterPhaseEntered.AddLambda([&Events](AActor* Actor, const int32 PhaseIndex, const FName PhaseId)
	{
		FTDBossPhaseEvent& Event = Events.AddDefaulted_GetRef();
		Event.Actor = Actor;
		Event.PhaseIndex = PhaseIndex;
		Event.PhaseId = PhaseId;
	});
	ON_SCOPE_EXIT { Probe.ThinkSubsystem->OnMonsterPhaseEntered.Remove(PhaseHandle); };

	Fixture.Tick(0.5f);
	TestEqual(TEXT("No phase is entered at full health"), Events.Num(), 0);
	TestEqual(TEXT("The phase index starts at 0"), Probe.ThinkSubsystem->GetMonsterPhaseIndex(Probe.Handle), 0);
	TestTrue(TEXT("The probe starts at full health"), FMath::IsNearlyEqual(Probe.Actor->GetCombatComponent()->GetCurrentHealth(), BossTestTargetHealth, 0.5f));

	const FTDBossPhaseStage Stages[] =
	{
		{ 650.f, 1, TEXT("Sky") },
		{ 350.f, 2, TEXT("Enraged") },
		{ 200.f, 3, TEXT("Inferno") }
	};
	const float ExpectedMarks[] = { 0.7f, 0.4f, 0.25f };
	int32 ExpectedEventCount = 0;
	for (const FTDBossPhaseStage& Stage : Stages)
	{
		++ExpectedEventCount;
		LowerBossTestHealth(*Probe.Actor, *Target, Stage.Health);
		TestTrue(FString::Printf(TEXT("The probe is at %.0f health before waiting for %s"), Stage.Health, Stage.PhaseName), FMath::IsNearlyEqual(Probe.Actor->GetCombatComponent()->GetCurrentHealth(), Stage.Health, 0.5f));
		const int32 ReactionTicks = FMath::RoundToInt(BossTestPhaseReactionSeconds * BossTestStepsPerSecond);
		for (int32 TickIndex = 0; TickIndex < ReactionTicks && Events.Num() < ExpectedEventCount; ++TickIndex)
		{
			Fixture.TickStep();
		}
		Fixture.Tick(BossTestPhaseDuplicateSeconds);
		if (!TestEqual(FString::Printf(TEXT("Exactly %d phase notifications arrived after reaching %s"), ExpectedEventCount, Stage.PhaseName), Events.Num(), ExpectedEventCount))
		{
			return false;
		}
		const FTDBossPhaseEvent& LastEvent = Events.Last();
		TestTrue(FString::Printf(TEXT("The notification names the probe for %s"), Stage.PhaseName), LastEvent.Actor.Get() == Probe.Actor);
		TestEqual(FString::Printf(TEXT("The notification carries phase index %d for %s"), Stage.PhaseIndex, Stage.PhaseName), LastEvent.PhaseIndex, Stage.PhaseIndex);
		TestEqual(FString::Printf(TEXT("The notification carries the id %s"), Stage.PhaseName), LastEvent.PhaseId, FName(Stage.PhaseName));
		TestEqual(FString::Printf(TEXT("GetMonsterPhaseIndex reports %d for %s"), Stage.PhaseIndex, Stage.PhaseName), Probe.ThinkSubsystem->GetMonsterPhaseIndex(Probe.Handle), Stage.PhaseIndex);
		const TArray<float> Marks = Probe.ThinkSubsystem->GetMonsterPhaseHealthMarks(Probe.Handle);
		if (!TestEqual(FString::Printf(TEXT("Three health marks are listed at %s"), Stage.PhaseName), Marks.Num(), 3))
		{
			continue;
		}
		for (int32 MarkIndex = 0; MarkIndex < Marks.Num(); ++MarkIndex)
		{
			TestTrue(FString::Printf(TEXT("Health mark %d is %.2f at %s (got %.3f)"), MarkIndex, ExpectedMarks[MarkIndex], Stage.PhaseName, Marks[MarkIndex]), FMath::IsNearlyEqual(Marks[MarkIndex], ExpectedMarks[MarkIndex], BossTestPhaseMarkTolerance));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDBossWindupTest, "TDGame.MonsterAI.Boss.WindupDelaysInstantSpell", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDBossWindupTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UTDDamageDefinition> Snap = MakeBossTestAreaSpell(120.f, 0.f, BossTestShapeLifetimeSeconds, BossTestShapeDamage);
	TMap<FName, UTDDamageDefinition*> Spells;
	Spells.Add(FName(TEXT("DA_BossSnap")), Snap.Get());
	FTDBossTestWorld Fixture;
	UTDCombatComponent* Target = Fixture.SpawnCombatant(FVector(100.f, 0.f, 0.f), BossTestEnemyTeam);
	TSharedPtr<const FTDResolvedMonsterDefinition> Definition;
	if (!ResolveBossTestProbe(*this, TEXT("BossWindupProbe"), MakeBossWindupProbeText(), Spells, Definition))
	{
		return false;
	}
	FTDBossTestProbe Probe;
	if (!SpawnRegisteredBossTestProbe(*this, Fixture, Definition, DefaultModeNames::Flying, FVector2D::ZeroVector, Probe))
	{
		return false;
	}

	bool bHasStartedCast = false;
	const int32 CastWaitTicks = FMath::RoundToInt(BossTestWindupCastWaitSeconds * BossTestStepsPerSecond);
	for (int32 TickIndex = 0; TickIndex < CastWaitTicks && !bHasStartedCast; ++TickIndex)
	{
		Fixture.TickStep();
		bHasStartedCast = Probe.ThinkSubsystem->GetMonsterState(Probe.Handle) == ETDMonsterFsmState::Cast;
	}
	if (!TestTrue(TEXT("The probe starts casting within one second"), bHasStartedCast))
	{
		return false;
	}

	Fixture.Tick(BossTestWindupEarlySeconds);
	const float EarlyHealth = Target->GetCurrentHealth();
	TestTrue(FString::Printf(TEXT("The target takes no damage %.1f s after the cast starts, before the %.1f s windup ends (health %.1f)"), BossTestWindupEarlySeconds, BossTestWindupSeconds, EarlyHealth), EarlyHealth >= BossTestTargetHealth);
	TestTrue(TEXT("A warning wrapper entity, not the instant spell, is alive during the windup"), CountBossTestEntitiesWithoutDefinition(Fixture.World, Snap.Get()) >= 1);
	Fixture.Tick(BossTestWindupLateSeconds - BossTestWindupEarlySeconds);
	const float LateHealth = Target->GetCurrentHealth();
	TestTrue(FString::Printf(TEXT("The target takes damage %.1f s after the cast starts (health %.1f)"), BossTestWindupLateSeconds, LateHealth), LateHealth < BossTestTargetHealth);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDBossDefinitionKeysTest, "TDGame.MonsterAI.Boss.DefinitionKeysResolve", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDBossDefinitionKeysTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UTDDamageDefinition> AnySpell = MakeBossTestAreaSpell(120.f, 0.f, BossTestShapeLifetimeSeconds, BossTestShapeDamage);
	TMap<FName, UTDDamageDefinition*> Spells;
	Spells.Add(FName(TEXT("DA_BossAny")), AnySpell.Get());

	TSharedPtr<const FTDResolvedMonsterDefinition> Definition;
	if (!ResolveBossTestProbe(*this, TEXT("BossKeysProbe"), MakeBossKeysProbeText(TEXT("\"air\""), TEXT("0.75"), TEXT("\"Goblin\""), TEXT("4")), Spells, Definition))
	{
		return false;
	}
	const FTDResolvedAbility* Snap = Definition->Abilities.FindByPredicate([](const FTDResolvedAbility& Ability) { return Ability.Name == FName(TEXT("Snap")); });
	if (TestNotNull(TEXT("The Snap ability is resolved"), Snap))
	{
		TestEqual(TEXT("windup 0.7 is stored"), Snap->WindupSeconds, 0.7f, 0.001f);
		TestTrue(TEXT("danger true is stored"), Snap->bIsDanger);
		TestTrue(TEXT("stance air is stored"), Snap->Stance == ETDAbilityStance::Air);
	}
	if (TestEqual(TEXT("One phase is resolved"), Definition->Phases.Num(), 1))
	{
		TestEqual(TEXT("cooldown_scale 0.75 is stored on the phase"), Definition->Phases[0].CooldownScale.Get(-1.f), 0.75f, 0.001f);
		TestEqual(TEXT("The cooldown scale applies from the first phase"), Definition->GetCooldownScaleForPhase(1), 0.75f, 0.001f);
		TestEqual(TEXT("The cooldown scale is 1 before any phase"), Definition->GetCooldownScaleForPhase(0), 1.f, 0.001f);
	}
	const int32 SequenceIndex = Definition->SequenceNames.IndexOfByKey(FName(TEXT("Rise")));
	if (TestTrue(TEXT("The Rise sequence has three steps"), Definition->Sequences.IsValidIndex(SequenceIndex) && Definition->Sequences[SequenceIndex].Num() == 3))
	{
		const TArray<FTDResolvedStep>& Steps = Definition->Sequences[SequenceIndex];
		TestTrue(TEXT("Step 0 is TakeOff"), Steps[0].Primitive == ETDMonsterPrimitive::TakeOff);
		TestEqual(TEXT("TakeOff altitude 420 is stored"), Steps[0].Args.Altitude, 420.f, 0.001f);
		TestEqual(TEXT("TakeOff seconds 1.5 is stored"), Steps[0].Args.Seconds, 1.5f, 0.001f);
		TestTrue(TEXT("Step 1 is Land"), Steps[1].Primitive == ETDMonsterPrimitive::Land);
		TestEqual(TEXT("Land seconds 0.9 is stored"), Steps[1].Args.Seconds, 0.9f, 0.001f);
		TestTrue(TEXT("Step 2 is SummonMinions"), Steps[2].Primitive == ETDMonsterPrimitive::SummonMinions);
		TestEqual(TEXT("SummonMinions species Goblin is stored"), Steps[2].Args.SpeciesName, FName(TEXT("Goblin")));
		TestEqual(TEXT("SummonMinions count 4 is stored"), Steps[2].Args.Count, 4);
		TestEqual(TEXT("SummonMinions radius 500 is stored"), Steps[2].Args.Radius, 500.f, 0.001f);
		TestEqual(TEXT("SummonMinions seconds 1.1 is stored"), Steps[2].Args.Seconds, 1.1f, 0.001f);
	}

	CheckBossKeyRejected(*this, Spells, TEXT("stance sky"), TEXT("stance"), TEXT("\"sky\""), TEXT("0.75"), TEXT("\"Goblin\""), TEXT("4"));
	CheckBossKeyRejected(*this, Spells, TEXT("count 0"), TEXT("count"), TEXT("\"air\""), TEXT("0.75"), TEXT("\"Goblin\""), TEXT("0"));
	CheckBossKeyRejected(*this, Spells, TEXT("cooldown_scale 0"), TEXT("cooldown_scale"), TEXT("\"air\""), TEXT("0"), TEXT("\"Goblin\""), TEXT("4"));
	CheckBossKeyRejected(*this, Spells, TEXT("empty species"), TEXT("species"), TEXT("\"air\""), TEXT("0.75"), TEXT("\"\""), TEXT("4"));

	const TArray<FSoftObjectPath> SpellPaths = UTDMonsterDefinitionLibrary::GetSpellCandidatePaths(FName(TEXT("DA_TDThunderCage")));
	if (TestTrue(FString::Printf(TEXT("At least two spell candidate paths are listed (got %d)"), SpellPaths.Num()), SpellPaths.Num() >= 2))
	{
		TestEqual(TEXT("The second spell candidate is the MegaMagic asset"), SpellPaths[1].ToString(), FString(BossTestThunderCagePath));
	}
	return true;
}

#endif
