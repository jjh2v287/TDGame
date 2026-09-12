#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/TDCombatComponent.h"
#include "Characters/TDCompanionCharacter.h"
#include "Characters/TDMonsterCharacter.h"
#include "Combat/GAS/TDCombatAttributeSet.h"
#include "Combat/GAS/TDCombatGameplayEffects.h"
#include "Core/TDGameplayTags.h"
#include "Combat/Damage/TDDamageDefinition.h"
#include "Combat/Damage/TDDamageEntity.h"
#include "Combat/Damage/TDDamageExamples.h"
#include "Combat/Damage/TDDamageSubsystem.h"
#include "Combat/Damage/TDStatusDefinition.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Characters/TDGameCharacter.h"
#include "Framework/TDGamePlayerController.h"
#include "UObject/UnrealType.h"

namespace
{
	struct FTDScopedCombatWorld
	{
		FTDScopedCombatWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDCombatTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDScopedCombatWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		UTDCombatComponent* SpawnCombatant(const FVector& Location, int32 TeamId = 1)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Actor->AddInstanceComponent(Sphere);
			Actor->SetRootComponent(Sphere);
			Sphere->SetSphereRadius(10.f);
			Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Sphere->SetCollisionObjectType(ECC_Pawn);
			Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
			Sphere->RegisterComponent();
			Actor->SetActorLocation(Location);
			UTDCombatComponent* Combatant = NewObject<UTDCombatComponent>(Actor);
			Actor->AddInstanceComponent(Combatant);
			FTDCombatStats Stats;
			Stats.TeamId = TeamId;
			Stats.BaseMaxHealth = 1000.f;
			Combatant->SetStats(Stats);
			Combatant->RegisterComponent();
			return Combatant;
		}

		void Tick(float Duration, float Step = 0.02f)
		{
			for (float Remaining = Duration; Remaining > UE_SMALL_NUMBER;)
			{
				const float Delta = FMath::Min(Remaining, Step);
				++GFrameCounter;
				World->Tick(LEVELTICK_All, Delta);
				Remaining -= Delta;
			}
		}

		UTDDamageSubsystem* GetDamageSubsystem() const
		{
			return World->GetSubsystem<UTDDamageSubsystem>();
		}

		UWorld* World = nullptr;
	};

	FTDDamageContext MakeContext(UTDCombatComponent* Caster)
	{
		FTDDamageContext Context;
		Context.Caster = Caster->GetOwner();
		Context.Stats = Caster->GetStats();
		Context.Budget = MakeShared<FTDDamageChainBudget>();
		return Context;
	}

	FTDDamageAction MakeDamage(float Amount, ETDDamageElement Element = ETDDamageElement::Physical)
	{
		FTDDamageAction Action;
		Action.Magnitude.Base = Amount;
		Action.Element = Element;
		return Action;
	}

	FTDDamageRule MakeRule(ETDDamageEvent Event, const FTDDamageAction& Action)
	{
		FTDDamageRule Rule;
		Rule.Event = Event;
		Rule.Actions.Add(Action);
		return Rule;
	}

	UTDStatusDefinition* MakeFreeze(UObject* Outer, float Threshold = 0.f)
	{
		UTDStatusDefinition* Status = NewObject<UTDStatusDefinition>(Outer);
		Status->Duration = 0.12f;
		Status->bFreezesTarget = true;
		Status->DamageThreshold = Threshold;
		Status->Rules.Add(MakeRule(ETDDamageEvent::Expire, MakeDamage(15.f)));
		return Status;
	}

	int32 CountEntities(UWorld* World, const UTDDamageDefinition* Definition)
	{
		int32 Count = 0;
		for (TActorIterator<ATDDamageEntity> Entity(World); Entity; ++Entity)
		{
			if (!Entity->IsActorBeingDestroyed() && Entity->GetDefinition() == Definition)
			{
				++Count;
			}
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDamageScalingTest, "TDGame.Combat.ScalingMitigationAndCriticalBoundaries", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDamageScalingTest::RunTest(const FString& Parameters)
{
	FTDCombatStats Stats;
	FTDScaledValue Value;
	Value.Base = 5.f;
	Value.PerLevel = 2.f;
	Value.AttackRatio = 0.5f;
	Value.SpellRatio = 1.f;
	Stats.Level = -20;
	TestEqual(TEXT("Levels below one use level one stats"), Value.Evaluate(Stats), 30.f);
	Stats.Level = 11;
	TestEqual(TEXT("Level growth includes base damage and caster powers"), Value.Evaluate(Stats), 90.f);
	Stats.Level = MAX_int32;
	TestEqual(TEXT("Large levels clamp to level one thousand"), Value.Evaluate(Stats), 6024.f);
	Stats.BaseMaxHealth = -5.f;
	Stats.HealthPerLevel = -2.f;
	TestEqual(TEXT("Invalid negative health inputs retain one health"), Stats.GetMaxHealth(), 1.f);

	FTDScopedCombatWorld Fixture;
	UTDCombatComponent* Caster = Fixture.SpawnCombatant(FVector(-500.f, 0.f, 0.f), 0);
	UTDCombatComponent* Target = Fixture.SpawnCombatant(FVector::ZeroVector);
	FTDCombatStats TargetStats = Target->GetStats();
	TargetStats.Armor = 100.f;
	TargetStats.MagicResistance = 100.f;
	Target->SetStats(TargetStats);
	FTDDamageContext Context = MakeContext(Caster);
	Context.Stats.CriticalChance = 0.f;
	Context.Stats.CriticalMultiplier = 2.f;
	FTDDamageResult Result = Target->ReceiveDamage(100.f, ETDDamageElement::Physical, true, Context);
	TestEqual(TEXT("One hundred armor halves physical damage"), Result.AppliedDamage, 50.f);
	TestFalse(TEXT("Zero critical chance never crits"), Result.bWasCritical);
	Context.Stats.CriticalChance = 1.f;
	Result = Target->ReceiveDamage(100.f, ETDDamageElement::Frost, true, Context);
	TestEqual(TEXT("Critical multiplier is applied before magic mitigation"), Result.AppliedDamage, 100.f);
	TestTrue(TEXT("Full critical chance always crits"), Result.bWasCritical);
	Result = Target->ReceiveDamage(100.f, ETDDamageElement::Fire, false, Context);
	TestEqual(TEXT("Actions can disable critical hits"), Result.AppliedDamage, 50.f);
	TestFalse(TEXT("Critical flag stays false when disabled"), Result.bWasCritical);
	TestEqual(TEXT("Negative incoming damage cannot heal"), Target->ReceiveDamage(-100.f, ETDDamageElement::Physical, false, Context).AppliedDamage, 0.f);
	Result = Target->ReceiveDamage(10000.f, ETDDamageElement::Physical, false, Context);
	TestEqual(TEXT("Overkill reports actual remaining health"), Result.AppliedDamage, 800.f);
	TestTrue(TEXT("Lethal damage reports death"), Result.bWasKilled);
	TestEqual(TEXT("Dead combatants receive no more damage"), Target->ReceiveDamage(1.f, ETDDamageElement::Physical, false, Context).AppliedDamage, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDProjectileChainTest, "TDGame.Combat.ProjectileCreatesPeriodicAreaWithCasterSnapshot", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDProjectileChainTest::RunTest(const FString& Parameters)
{
	FTDScopedCombatWorld Fixture;
	UTDCombatComponent* Caster = Fixture.SpawnCombatant(FVector(-200.f, 0.f, 0.f), 0);
	UTDCombatComponent* Target = Fixture.SpawnCombatant(FVector::ZeroVector);
	UTDDamageDefinition* Flame = NewObject<UTDDamageDefinition>(Fixture.World);
	Flame->Lifetime = 0.5f;
	Flame->PulseInterval = 0.05f;
	Flame->HitInterval = 0.05f;
	Flame->MaxHitsPerTarget = 0;
	Flame->bDestroyOnHit = false;
	FTDDamageAction FlameDamage = MakeDamage(1.f, ETDDamageElement::Fire);
	FlameDamage.Magnitude.SpellRatio = 0.1f;
	Flame->Rules.Add(MakeRule(ETDDamageEvent::Hit, FlameDamage));
	UTDDamageDefinition* Projectile = NewObject<UTDDamageDefinition>(Fixture.World);
	Projectile->Mode = ETDDamageEntityMode::Projectile;
	Projectile->ProjectileSpeed = 2000.f;
	FTDDamageAction Impact = MakeDamage(5.f, ETDDamageElement::Fire);
	Impact.Magnitude.SpellRatio = 1.f;
	FTDDamageRule ImpactRule = MakeRule(ETDDamageEvent::Hit, Impact);
	FTDDamageAction SpawnFlame;
	SpawnFlame.Type = ETDDamageActionType::SpawnEntity;
	SpawnFlame.Entity = Flame;
	SpawnFlame.SpawnAnchor = ETDDamageSpawnAnchor::Target;
	ImpactRule.Actions.Add(SpawnFlame);
	Projectile->Rules.Add(ImpactRule);
	TArray<float> AppliedDamage;
	const FDelegateHandle DamageHandle = Target->OnDamaged.AddLambda([&AppliedDamage](const FTDDamageResult& Result, const FTDDamageContext& Context)
	{
		AppliedDamage.Add(Result.AppliedDamage);
	});
	ATDDamageEntity* CastEntity = Fixture.GetDamageSubsystem()->Cast(Projectile, Caster->GetOwner(), Caster->GetOwner()->GetActorLocation(), FVector::ZeroVector);
	if (!TestNotNull(TEXT("Projectile is spawned by the public cast path"), CastEntity))
	{
		Target->OnDamaged.Remove(DamageHandle);
		return false;
	}
	FTDCombatStats ChangedStats = Caster->GetStats();
	ChangedStats.Level = 100;
	ChangedStats.SpellPower = 1000.f;
	Caster->SetStats(ChangedStats);
	Caster->GetOwner()->Destroy();
	Fixture.Tick(0.3f, 0.01f);
	TestEqual(TEXT("Projectile ends after impact"), CountEntities(Fixture.World, Projectile), 0);
	TestEqual(TEXT("Exactly one flame area is created after caster destruction"), CountEntities(Fixture.World, Flame), 1);
	TestTrue(TEXT("Impact is followed by multiple area ticks"), AppliedDamage.Num() >= 3);
	if (!AppliedDamage.IsEmpty())
	{
		TestEqual(TEXT("Impact uses the original caster spell power"), AppliedDamage[0], 25.f);
		for (int32 Index = 1; Index < AppliedDamage.Num(); ++Index)
		{
			TestEqual(FString::Printf(TEXT("Child tick %d inherits the original caster snapshot"), Index), AppliedDamage[Index], 3.f);
		}
	}
	Target->OnDamaged.Remove(DamageHandle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDFrostBuildupTest, "TDGame.Combat.MitigatedBuildupFreezesAndExpiresOnce", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFrostBuildupTest::RunTest(const FString& Parameters)
{
	FTDScopedCombatWorld Fixture;
	UTDCombatComponent* Caster = Fixture.SpawnCombatant(FVector(-500.f, 0.f, 0.f), 0);
	UTDCombatComponent* Target = Fixture.SpawnCombatant(FVector::ZeroVector);
	FTDCombatStats TargetStats = Target->GetStats();
	TargetStats.MagicResistance = 100.f;
	Target->SetStats(TargetStats);
	Target->GetOwner()->CustomTimeDilation = 0.75f;
	FTDDamageAction FrostDamage = MakeDamage(40.f, ETDDamageElement::Frost);
	FrostDamage.Status = MakeFreeze(Fixture.World, 40.f);
	const TArray<FTDDamageRule> Rules = { MakeRule(ETDDamageEvent::Hit, FrostDamage) };
	const FTDDamageContext Context = MakeContext(Caster);
	Fixture.GetDamageSubsystem()->ExecuteRules(Rules, ETDDamageEvent::Hit, Context, Target->GetOwner(), FVector::ZeroVector);
	TestFalse(TEXT("Forty raw damage is only twenty buildup after resistance"), Target->IsFrozen());
	Fixture.GetDamageSubsystem()->ExecuteRules(Rules, ETDDamageEvent::Hit, Context, Target->GetOwner(), FVector::ZeroVector);
	TestTrue(TEXT("Accumulated actual damage reaches the freeze threshold"), Target->IsFrozen());
	TestEqual(TEXT("Frozen target actor time is stopped"), Target->GetOwner()->CustomTimeDilation, 0.f);
	TestEqual(TEXT("Two frost hits reduce health by forty"), Target->GetCurrentHealth(), 960.f);
	Fixture.Tick(0.25f);
	TestFalse(TEXT("World timers unfreeze an actor with zero custom time dilation"), Target->IsFrozen());
	TestEqual(TEXT("Expiry restores the pre-freeze time dilation"), Target->GetOwner()->CustomTimeDilation, 0.75f);
	TestEqual(TEXT("Expiry applies the configured burst damage"), Target->GetCurrentHealth(), 945.f);
	Fixture.Tick(0.4f);
	TestEqual(TEXT("Expiry burst is dispatched only once"), Target->GetCurrentHealth(), 945.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDStatusCancellationTest, "TDGame.Combat.ResetAndDeathCancelStatusExpiry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDStatusCancellationTest::RunTest(const FString& Parameters)
{
	FTDScopedCombatWorld Fixture;
	UTDCombatComponent* Caster = Fixture.SpawnCombatant(FVector(-500.f, 0.f, 0.f), 0);
	UTDCombatComponent* Target = Fixture.SpawnCombatant(FVector::ZeroVector);
	const FTDDamageContext Context = MakeContext(Caster);
	UTDStatusDefinition* Freeze = MakeFreeze(Fixture.World);
	Target->ApplyStatus(Freeze, Context);
	TestTrue(TEXT("Immediate freeze activates"), Target->IsFrozen());
	Target->SetStats(Target->GetStats());
	TestFalse(TEXT("Reset clears freeze immediately"), Target->IsFrozen());
	const int32 ActionsAfterReset = Context.Budget->RemainingActions;
	Fixture.Tick(0.3f);
	TestEqual(TEXT("Reset prevents expiry damage"), Target->GetCurrentHealth(), 1000.f);
	TestEqual(TEXT("Reset prevents expiry rule execution"), Context.Budget->RemainingActions, ActionsAfterReset);
	Target->ApplyStatus(Freeze, Context);
	Target->ReceiveDamage(1000.f, ETDDamageElement::Physical, false, Context);
	TestFalse(TEXT("Death clears freeze immediately"), Target->IsFrozen());
	TestFalse(TEXT("Target is dead"), Target->IsAlive());
	const int32 ActionsAfterDeath = Context.Budget->RemainingActions;
	Fixture.Tick(0.3f);
	TestEqual(TEXT("Death prevents expiry rule execution"), Context.Budget->RemainingActions, ActionsAfterDeath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDShockwaveSweepTest, "TDGame.Combat.FastShockwaveSweepsRingAndPreservesHole", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDShockwaveSweepTest::RunTest(const FString& Parameters)
{
	FTDScopedCombatWorld Fixture;
	UTDCombatComponent* Caster = Fixture.SpawnCombatant(FVector(-500.f, 0.f, 0.f), 0);
	UTDCombatComponent* Center = Fixture.SpawnCombatant(FVector::ZeroVector);
	UTDCombatComponent* InitialBand = Fixture.SpawnCombatant(FVector(90.f, 0.f, 0.f));
	UTDCombatComponent* SweptBand = Fixture.SpawnCombatant(FVector(250.f, 0.f, 0.f));
	UTDCombatComponent* AboveBand = Fixture.SpawnCombatant(FVector(250.f, 0.f, 100.f));
	UTDDamageDefinition* Ring = NewObject<UTDDamageDefinition>(Fixture.World);
	Ring->Mode = ETDDamageEntityMode::Shockwave;
	Ring->Radius = 100.f;
	Ring->InnerRadius = 80.f;
	Ring->HalfHeight = 50.f;
	Ring->ExpansionSpeed = 2000.f;
	Ring->bDestroyOnHit = false;
	Ring->Rules.Add(MakeRule(ETDDamageEvent::Hit, MakeDamage(10.f)));
	Fixture.GetDamageSubsystem()->SpawnEntity(Ring, MakeContext(Caster), FVector::ZeroVector);
	Fixture.Tick(0.1f, 0.1f);
	TestEqual(TEXT("Initial inner hole is never damaged"), Center->GetCurrentHealth(), 1000.f);
	TestEqual(TEXT("Initial annulus band is hit"), InitialBand->GetCurrentHealth(), 990.f);
	TestEqual(TEXT("A narrow ring cannot skip a target during a long frame"), SweptBand->GetCurrentHealth(), 990.f);
	TestEqual(TEXT("Cylinder height excludes targets above the wave"), AboveBand->GetCurrentHealth(), 1000.f);
	Fixture.Tick(0.1f, 0.1f);
	TestEqual(TEXT("Default hit limit prevents duplicate wave hits"), SweptBand->GetCurrentHealth(), 990.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDamageTargetPolicyTest, "TDGame.Combat.TeamPolicyAndSpatialFiltering", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDamageTargetPolicyTest::RunTest(const FString& Parameters)
{
	FTDScopedCombatWorld Fixture;
	UTDCombatComponent* Caster = Fixture.SpawnCombatant(FVector::ZeroVector, 7);
	UTDCombatComponent* Ally = Fixture.SpawnCombatant(FVector(10.f, 0.f, 0.f), 7);
	UTDCombatComponent* Enemy = Fixture.SpawnCombatant(FVector(20.f, 0.f, 0.f), 8);
	Fixture.SpawnCombatant(FVector(500.f, 0.f, 0.f), 8);
	Fixture.SpawnCombatant(FVector(20.f, 0.f, 200.f), 8);
	UTDDamageSubsystem* Subsystem = Fixture.GetDamageSubsystem();
	const FTDDamageContext Context = MakeContext(Caster);
	TestFalse(TEXT("Caster is excluded even by Everyone policy"), Subsystem->CanTarget(Caster, Context, ETDDamageTargetPolicy::Everyone));
	TestFalse(TEXT("Enemy policy prevents friendly fire"), Subsystem->CanTarget(Ally, Context, ETDDamageTargetPolicy::Enemies));
	TestTrue(TEXT("Ally policy includes matching team"), Subsystem->CanTarget(Ally, Context, ETDDamageTargetPolicy::Allies));
	TestTrue(TEXT("Enemy policy includes different team"), Subsystem->CanTarget(Enemy, Context, ETDDamageTargetPolicy::Enemies));
	TArray<UTDCombatComponent*> Targets;
	Subsystem->GatherTargets(FVector::ZeroVector, 100.f, 50.f, Context, ETDDamageTargetPolicy::Enemies, Targets);
	TestEqual(TEXT("Gathering combines team, radius, and height filtering"), Targets.Num(), 1);
	TestTrue(TEXT("The nearby enemy is registered by component BeginPlay"), Targets.Contains(Enemy));
	Caster->GetOwner()->Destroy();
	TestFalse(TEXT("Team snapshot still excludes allies after caster destruction"), Subsystem->CanTarget(Ally, Context, ETDDamageTargetPolicy::Enemies));
	Enemy->ReceiveDamage(1000.f, ETDDamageElement::Physical, false, Context);
	TestFalse(TEXT("Dead targets cannot be selected"), Subsystem->CanTarget(Enemy, Context, ETDDamageTargetPolicy::Everyone));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDamageChainBudgetTest, "TDGame.Combat.CyclicSpawnsAndActionsRespectSharedBudget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDamageChainBudgetTest::RunTest(const FString& Parameters)
{
	FTDScopedCombatWorld Fixture;
	UTDCombatComponent* Caster = Fixture.SpawnCombatant(FVector(-500.f, 0.f, 0.f), 0);
	UTDDamageDefinition* Cycle = NewObject<UTDDamageDefinition>(Fixture.World);
	FTDDamageAction Spawn;
	Spawn.Type = ETDDamageActionType::SpawnEntity;
	Spawn.Entity = Cycle;
	Spawn.SpawnCount = 4;
	Cycle->Rules.Add(MakeRule(ETDDamageEvent::Spawn, Spawn));
	FTDDamageContext Context = MakeContext(Caster);
	Context.Budget->RemainingSpawns = 8;
	Context.Budget->RemainingActions = 16;
	Fixture.GetDamageSubsystem()->SpawnEntity(Cycle, Context, FVector::ZeroVector);
	TestEqual(TEXT("Cyclic fan-out stops at the shared spawn budget"), CountEntities(Fixture.World, Cycle), 8);
	TestEqual(TEXT("Spawn budget cannot become negative"), Context.Budget->RemainingSpawns, 0);
	UTDCombatComponent* Target = Fixture.SpawnCombatant(FVector(50.f, 0.f, 0.f));
	FTDDamageRule Rule = MakeRule(ETDDamageEvent::Hit, MakeDamage(10.f));
	Rule.Actions.Add(MakeDamage(10.f));
	Context.Budget->RemainingActions = 1;
	Fixture.GetDamageSubsystem()->ExecuteRules({ Rule }, ETDDamageEvent::Hit, Context, Target->GetOwner(), FVector::ZeroVector);
	TestEqual(TEXT("Action budget stops later actions in the same event"), Target->GetCurrentHealth(), 990.f);
	TestEqual(TEXT("Action budget cannot become negative"), Context.Budget->RemainingActions, 0);
	Context.Budget->RemainingSpawns = 8;
	Context.Depth = 33;
	TestNull(TEXT("Over-depth entity chains are refused"), Fixture.GetDamageSubsystem()->SpawnEntity(Cycle, Context, FVector::ZeroVector));
	TestEqual(TEXT("Rejected depth does not allocate a spawn"), Context.Budget->RemainingSpawns, 8);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDInvalidDamageDefinitionTest, "TDGame.Combat.InvalidDefinitionsAreRejected", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDInvalidDamageDefinitionTest::RunTest(const FString& Parameters)
{
	FTDScopedCombatWorld Fixture;
	UTDCombatComponent* Caster = Fixture.SpawnCombatant(FVector::ZeroVector, 0);
	UTDDamageDefinition* Definition = NewObject<UTDDamageDefinition>(Fixture.World);
	FString Error;
	TestTrue(TEXT("Default damage definition is valid"), Definition->ValidateDefinition(Error));
	Definition->Mode = ETDDamageEntityMode::Shockwave;
	Definition->InnerRadius = Definition->Radius;
	TestFalse(TEXT("An empty annulus is invalid"), Definition->ValidateDefinition(Error));
	TestTrue(TEXT("Validation identifies the faulty shape"), Error.Contains(TEXT("InnerRadius")));
	const FTDDamageContext Context = MakeContext(Caster);
	const int32 InitialSpawns = Context.Budget->RemainingSpawns;
	AddExpectedMessage(TEXT("Cannot spawn damage definition"), ELogVerbosity::Warning);
	TestNull(TEXT("Runtime refuses an invalid definition"), Fixture.GetDamageSubsystem()->SpawnEntity(Definition, Context, FVector::ZeroVector));
	TestEqual(TEXT("Invalid configuration consumes no spawn budget"), Context.Budget->RemainingSpawns, InitialSpawns);
	Definition->Mode = ETDDamageEntityMode::Area;
	Definition->ActivationDelay = Definition->Lifetime;
	TestFalse(TEXT("Activation cannot begin at or after expiry"), Definition->ValidateDefinition(Error));
	Definition->ActivationDelay = 0.f;
	FTDDamageAction MissingChild;
	MissingChild.Type = ETDDamageActionType::SpawnEntity;
	Definition->Rules = { MakeRule(ETDDamageEvent::Hit, MissingChild) };
	TestFalse(TEXT("Spawn actions require a referenced definition"), Definition->ValidateDefinition(Error));
	UTDStatusDefinition* Status = MakeFreeze(Fixture.World);
	Status->Duration = 0.f;
	TestFalse(TEXT("Zero-duration status is refused"), Status->ValidateDefinition(Error));
	TArray<UTDDamageDefinition*> Presets;
	TDDamageExamples::CreateExamples(Fixture.World, Presets);
	TestTrue(TEXT("Built-in spell presets are available"), Presets.Num() >= 5);
	TArray<UObject*> Pending;
	TSet<UObject*> Visited;
	for (UTDDamageDefinition* Preset : Presets)
	{
		Pending.Add(Preset);
	}
	while (!Pending.IsEmpty())
	{
		UObject* Preset = Pending.Pop();
		if (!Preset || Visited.Contains(Preset))
		{
			continue;
		}
		Visited.Add(Preset);
		const TArray<FTDDamageRule>* Rules = nullptr;
		if (const UTDDamageDefinition* DamagePreset = Cast<UTDDamageDefinition>(Preset))
		{
			TestTrue(FString::Printf(TEXT("Damage preset %s validates: %s"), *Preset->GetName(), *Error), DamagePreset->ValidateDefinition(Error));
			Rules = &DamagePreset->Rules;
		}
		if (const UTDStatusDefinition* StatusPreset = Cast<UTDStatusDefinition>(Preset))
		{
			TestTrue(FString::Printf(TEXT("Status preset %s validates: %s"), *Preset->GetName(), *Error), StatusPreset->ValidateDefinition(Error));
			Rules = &StatusPreset->Rules;
		}
		if (!Rules)
		{
			continue;
		}
		for (const FTDDamageRule& Rule : *Rules)
		{
			for (const FTDDamageAction& Action : Rule.Actions)
			{
				Pending.Add(Action.Entity);
				Pending.Add(Action.Status);
			}
		}
	}
	TestTrue(TEXT("Preset validation traverses child entities and status definitions"), Visited.Num() > Presets.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMineArmingTest, "TDGame.Combat.MineWaitsForArmingAndTriggersOnce", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMineArmingTest::RunTest(const FString& Parameters)
{
	FTDScopedCombatWorld Fixture;
	UTDCombatComponent* Caster = Fixture.SpawnCombatant(FVector(-500.f, 0.f, 0.f), 0);
	UTDCombatComponent* Enemy = Fixture.SpawnCombatant(FVector(20.f, 0.f, 0.f));
	UTDCombatComponent* Ally = Fixture.SpawnCombatant(FVector(10.f, 0.f, 0.f), 0);
	UTDDamageDefinition* Mine = NewObject<UTDDamageDefinition>(Fixture.World);
	Mine->Mode = ETDDamageEntityMode::Mine;
	Mine->ActivationDelay = 0.2f;
	Mine->PulseInterval = 0.05f;
	Mine->Rules.Add(MakeRule(ETDDamageEvent::Hit, MakeDamage(10.f)));
	Fixture.GetDamageSubsystem()->SpawnEntity(Mine, MakeContext(Caster), FVector::ZeroVector);
	Fixture.Tick(0.15f);
	TestEqual(TEXT("Mine cannot damage before arming"), Enemy->GetCurrentHealth(), 1000.f);
	Enemy->GetOwner()->SetActorLocation(FVector(500.f, 0.f, 0.f));
	Fixture.Tick(0.15f);
	TestEqual(TEXT("Nearby ally does not detonate armed mine"), CountEntities(Fixture.World, Mine), 1);
	Enemy->GetOwner()->SetActorLocation(FVector(20.f, 0.f, 0.f));
	Fixture.Tick(0.1f);
	TestEqual(TEXT("Enemy entering an armed mine receives one hit"), Enemy->GetCurrentHealth(), 990.f);
	TestEqual(TEXT("Detonation preserves friendly fire filtering"), Ally->GetCurrentHealth(), 1000.f);
	TestEqual(TEXT("Triggered mine ends"), CountEntities(Fixture.World, Mine), 0);
	Fixture.Tick(0.2f);
	TestEqual(TEXT("Mine cannot trigger a second time"), Enemy->GetCurrentHealth(), 990.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDProjectileCollisionTest, "TDGame.Combat.ProjectileSweepsWallsAndPiercesEnemies", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDProjectileCollisionTest::RunTest(const FString& Parameters)
{
	FTDScopedCombatWorld Fixture;
	UTDCombatComponent* Caster = Fixture.SpawnCombatant(FVector(-500.f, 0.f, 0.f), 0);
	UTDCombatComponent* Ally = Fixture.SpawnCombatant(FVector(40.f, 0.f, 0.f), 0);
	UTDCombatComponent* First = Fixture.SpawnCombatant(FVector(100.f, 0.f, 0.f));
	UTDCombatComponent* Second = Fixture.SpawnCombatant(FVector(200.f, 0.f, 0.f));
	UTDCombatComponent* BehindWall = Fixture.SpawnCombatant(FVector(300.f, 0.f, 0.f));
	Ally->GetOwner()->FindComponentByClass<USphereComponent>()->SetCollisionResponseToAllChannels(ECR_Block);
	AActor* Wall = Fixture.World->SpawnActor<AActor>();
	UBoxComponent* WallCollision = NewObject<UBoxComponent>(Wall);
	Wall->AddInstanceComponent(WallCollision);
	Wall->SetRootComponent(WallCollision);
	WallCollision->SetBoxExtent(FVector(10.f, 100.f, 100.f));
	WallCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	WallCollision->SetCollisionObjectType(ECC_WorldStatic);
	WallCollision->SetCollisionResponseToAllChannels(ECR_Block);
	WallCollision->RegisterComponent();
	Wall->SetActorLocation(FVector(250.f, 0.f, 0.f));
	UTDDamageDefinition* Projectile = NewObject<UTDDamageDefinition>(Fixture.World);
	Projectile->Mode = ETDDamageEntityMode::Projectile;
	Projectile->ProjectileSpeed = 5000.f;
	Projectile->ProjectileRadius = 5.f;
	Projectile->bDestroyOnHit = false;
	Projectile->Rules.Add(MakeRule(ETDDamageEvent::Hit, MakeDamage(10.f)));
	Fixture.GetDamageSubsystem()->SpawnEntity(Projectile, MakeContext(Caster), FVector::ZeroVector);
	Fixture.Tick(0.1f, 0.1f);
	TestEqual(TEXT("Blocking friendly body is ignored"), Ally->GetCurrentHealth(), 1000.f);
	TestEqual(TEXT("Fast projectile cannot skip first enemy"), First->GetCurrentHealth(), 990.f);
	TestEqual(TEXT("Piercing projectile reaches second enemy in the same frame"), Second->GetCurrentHealth(), 990.f);
	TestEqual(TEXT("Wall blocks enemies behind it"), BehindWall->GetCurrentHealth(), 1000.f);
	TestEqual(TEXT("Piercing still ends on world geometry"), CountEntities(Fixture.World, Projectile), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDamageReentrantDeathTest, "TDGame.Combat.DamageCallbacksCannotReportStaleDeath", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDamageReentrantDeathTest::RunTest(const FString& Parameters)
{
	FTDScopedCombatWorld Fixture;
	UTDCombatComponent* Caster = Fixture.SpawnCombatant(FVector(-500.f, 0.f, 0.f), 0);
	UTDCombatComponent* Target = Fixture.SpawnCombatant(FVector::ZeroVector);
	int32 Deaths = 0;
	Target->OnDeath.AddLambda([&Deaths](const FTDDamageContext&) { ++Deaths; });
	FDelegateHandle Handle = Target->OnDamaged.AddLambda([Target](const FTDDamageResult&, const FTDDamageContext&)
	{
		Target->SetStats(Target->GetStats());
	});
	FTDDamageResult Result = Target->ReceiveDamage(1000.f, ETDDamageElement::Physical, false, MakeContext(Caster));
	TestFalse(TEXT("Reset during damage callback cancels pending kill"), Result.bWasKilled);
	TestEqual(TEXT("Reset restores health"), Target->GetCurrentHealth(), 1000.f);
	TestEqual(TEXT("Reset prevents stale death notification"), Deaths, 0);
	Target->OnDamaged.Remove(Handle);
	Target->OnDamaged.AddLambda([Target](const FTDDamageResult&, const FTDDamageContext&) { Target->GetOwner()->Destroy(); });
	Result = Target->ReceiveDamage(1000.f, ETDDamageElement::Physical, false, MakeContext(Caster));
	TestFalse(TEXT("Destruction during damage callback cancels pending kill"), Result.bWasKilled);
	TestEqual(TEXT("Destroyed target does not dispatch a stale death"), Deaths, 0);
	Target->OnDamaged.Clear();
	Target->OnDeath.Clear();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDFreezeOwnershipTest, "TDGame.Combat.OverlappingFreezesPreserveExternalMovementChanges", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFreezeOwnershipTest::RunTest(const FString& Parameters)
{
	FTDScopedCombatWorld Fixture;
	UTDCombatComponent* Caster = Fixture.SpawnCombatant(FVector(-500.f, 0.f, 0.f), 0);
	ACharacter* FrozenCharacter = Fixture.World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("Native character for freeze restoration exists"), FrozenCharacter))
	{
		return false;
	}
	UTDCombatComponent* Target = NewObject<UTDCombatComponent>(FrozenCharacter);
	FrozenCharacter->AddInstanceComponent(Target);
	Target->RegisterComponent();
	FrozenCharacter->CustomTimeDilation = 0.75f;
	FrozenCharacter->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	UTDStatusDefinition* ShortFreeze = MakeFreeze(Fixture.World);
	UTDStatusDefinition* LongFreeze = MakeFreeze(Fixture.World);
	LongFreeze->Duration = 0.3f;
	Target->ApplyStatus(ShortFreeze, MakeContext(Caster));
	Target->ApplyStatus(LongFreeze, MakeContext(Caster));
	Fixture.Tick(0.2f);
	TestTrue(TEXT("First expiry does not thaw a second active freeze"), Target->IsFrozen());
	TestEqual(TEXT("Movement stays disabled until last freeze ends"), FrozenCharacter->GetCharacterMovement()->MovementMode.GetValue(), MOVE_None);
	FrozenCharacter->CustomTimeDilation = 0.4f;
	FrozenCharacter->GetCharacterMovement()->SetMovementMode(MOVE_Swimming);
	Fixture.Tick(0.2f);
	TestFalse(TEXT("Last freeze naturally expires"), Target->IsFrozen());
	TestEqual(TEXT("External time dilation is preserved"), FrozenCharacter->CustomTimeDilation, 0.4f);
	TestEqual(TEXT("External movement mode is preserved"), FrozenCharacter->GetCharacterMovement()->MovementMode.GetValue(), MOVE_Swimming);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDSourceDestructionTest, "TDGame.Combat.SourceDestructionCancelsSpawnFanout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDSourceDestructionTest::RunTest(const FString& Parameters)
{
	FTDScopedCombatWorld Fixture;
	UTDCombatComponent* Caster = Fixture.SpawnCombatant(FVector(-500.f, 0.f, 0.f), 0);
	UTDCombatComponent* Target = Fixture.SpawnCombatant(FVector::ZeroVector);
	UTDDamageDefinition* Child = NewObject<UTDDamageDefinition>(Fixture.World);
	Child->Rules.Add(MakeRule(ETDDamageEvent::Hit, MakeDamage(1.f)));
	UTDDamageDefinition* Parent = NewObject<UTDDamageDefinition>(Fixture.World);
	FTDDamageAction Spawn;
	Spawn.Type = ETDDamageActionType::SpawnEntity;
	Spawn.Entity = Child;
	Spawn.SpawnCount = 3;
	Spawn.DelaySeconds = 0.1f;
	Parent->Rules.Add(MakeRule(ETDDamageEvent::Spawn, Spawn));
	ATDDamageEntity* Source = Fixture.GetDamageSubsystem()->SpawnEntity(Parent, MakeContext(Caster), FVector::ZeroVector);
	if (!TestNotNull(TEXT("Fan-out source exists"), Source))
	{
		return false;
	}
	const FDelegateHandle Handle = Target->OnDamaged.AddLambda([Source](const FTDDamageResult&, const FTDDamageContext&) { Source->Destroy(); });
	Fixture.Tick(0.2f);
	TestEqual(TEXT("Only first child spawns when its callback destroys source"), CountEntities(Fixture.World, Child), 1);
	TestEqual(TEXT("Remaining children cannot damage after source cancellation"), Target->GetCurrentHealth(), 999.f);
	Target->OnDamaged.Remove(Handle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDefaultCombatIntegrationTest, "TDGame.Combat.DefaultGameModeUsesNativeCombatClasses", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDefaultCombatIntegrationTest::RunTest(const FString& Parameters)
{
	FString GameModePath;
	GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GlobalDefaultGameMode"), GameModePath, GEngineIni);
	UClass* GameModeClass = LoadClass<AGameModeBase>(nullptr, *GameModePath);
	if (!TestNotNull(TEXT("Configured game mode asset can be loaded"), GameModeClass))
	{
		return false;
	}
	const AGameModeBase* GameModeDefaults = GameModeClass->GetDefaultObject<AGameModeBase>();
	const bool bHasCombatPawn = GameModeDefaults->DefaultPawnClass && GameModeDefaults->DefaultPawnClass->IsChildOf(ATDGameCharacter::StaticClass());
	const bool bHasCombatController = GameModeDefaults->PlayerControllerClass && GameModeDefaults->PlayerControllerClass->IsChildOf(ATDGamePlayerController::StaticClass());
	TestTrue(TEXT("Default pawn inherits the native combat character"), bHasCombatPawn);
	TestTrue(TEXT("Default controller inherits the native spell input controller"), bHasCombatController);
	if (!bHasCombatPawn || !bHasCombatController)
	{
		return false;
	}
	const ATDGameCharacter* CharacterDefaults = GameModeDefaults->DefaultPawnClass->GetDefaultObject<ATDGameCharacter>();
	TestNotNull(TEXT("Saved pawn class includes its combat component"), CharacterDefaults->GetCombatComponent());
	TestNotNull(TEXT("Saved pawn class has a visible skeletal mesh"), CharacterDefaults->GetMesh()->GetSkeletalMeshAsset());
	const UObject* ControllerDefaults = GameModeDefaults->PlayerControllerClass->GetDefaultObject();
	const FName RequiredInputs[] = { TEXT("DefaultMappingContext"), TEXT("SetDestinationClickAction"), TEXT("SetDestinationTouchAction") };
	for (const FName PropertyName : RequiredInputs)
	{
		const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(ControllerDefaults->GetClass(), PropertyName);
		TestTrue(FString::Printf(TEXT("Saved controller binds %s"), *PropertyName.ToString()), Property && Property->GetObjectPropertyValue_InContainer(ControllerDefaults));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDGASCharacterFoundationTest, "TDGame.Combat.GAS.SharedCharactersAndLevelGrowth", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDGASCharacterFoundationTest::RunTest(const FString& Parameters)
{
	FTDScopedCombatWorld Fixture;
	ATDCompanionCharacter* Companion = Fixture.World->SpawnActor<ATDCompanionCharacter>();
	ATDMonsterCharacter* Monster = Fixture.World->SpawnActor<ATDMonsterCharacter>();
	if (!TestNotNull(TEXT("Companion exists"), Companion) || !TestNotNull(TEXT("Monster exists"), Monster))
	{
		return false;
	}
	UTDCombatComponent* Combat = Companion->GetCombatComponent();
	TestTrue(TEXT("Combat component is the character's single ASC"), Companion->GetAbilitySystemComponent() == Combat);
	TestEqual(TEXT("Companion uses player team"), Combat->GetStats().TeamId, 1);
	TestEqual(TEXT("Monster uses enemy team"), Monster->GetCombatComponent()->GetStats().TeamId, 2);
	TestNotNull(TEXT("ASC owns the registered attribute set"), Combat->GetSet<UTDCombatAttributeSet>());
	Combat->SetCombatLevel(10);
	TestEqual(TEXT("Level progression updates effective spell power once"), Combat->GetStats().GetSpellPower(), 47.f);
	Combat->SetCombatLevel(11);
	TestEqual(TEXT("Later progression retains authored growth coefficients"), Combat->GetStats().GetSpellPower(), 50.f);
	TestEqual(TEXT("GAS Health is authoritative"), Combat->GetCurrentHealth(), Combat->GetNumericAttribute(UTDCombatAttributeSet::GetHealthAttribute()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDGASDamageAbilityTest, "TDGame.Combat.GAS.DamageAbilityUsesIndependentCooldowns", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDGASDamageAbilityTest::RunTest(const FString& Parameters)
{
	FTDScopedCombatWorld Fixture;
	ATDCompanionCharacter* Caster = Fixture.World->SpawnActor<ATDCompanionCharacter>();
	if (!TestNotNull(TEXT("Native GAS caster exists"), Caster))
	{
		return false;
	}
	UTDCombatComponent* Target = Fixture.SpawnCombatant(FVector(200.f, 0.f, 0.f), 2);
	UTDDamageDefinition* FirstSpell = NewObject<UTDDamageDefinition>(Fixture.World);
	FirstSpell->Cooldown = 0.2f;
	FirstSpell->Lifetime = 0.05f;
	FirstSpell->Rules.Add(MakeRule(ETDDamageEvent::Hit, MakeDamage(10.f)));
	UTDDamageDefinition* SecondSpell = DuplicateObject<UTDDamageDefinition>(FirstSpell, Fixture.World);
	UTDCombatComponent* Combat = Caster->GetCombatComponent();
	TestTrue(TEXT("First cast activates the GAS ability"), Combat->TryCastDamageDefinition(FirstSpell, Target->GetOwner()->GetActorLocation()));
	TestEqual(TEXT("Ability-created entity applies a Health gameplay effect"), Target->GetCurrentHealth(), 990.f);
	TestTrue(TEXT("GAS cooldown is active"), Combat->HasMatchingGameplayTag(TDGameplayTags::Effect_Cooldown_Damage));
	TestFalse(TEXT("Same definition cannot bypass cooldown"), Combat->TryCastDamageDefinition(FirstSpell, Target->GetOwner()->GetActorLocation()));
	TestTrue(TEXT("Another definition has an independent cooldown"), Combat->TryCastDamageDefinition(SecondSpell, Target->GetOwner()->GetActorLocation()));
	Fixture.Tick(0.3f);
	TestTrue(TEXT("Cooldown expiration allows casting again"), Combat->TryCastDamageDefinition(FirstSpell, Target->GetOwner()->GetActorLocation()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDGASStatusCleanseTest, "TDGame.Combat.GAS.CleansingFreezeCancelsExpiryBurst", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDGASStatusCleanseTest::RunTest(const FString& Parameters)
{
	FTDScopedCombatWorld Fixture;
	UTDCombatComponent* Caster = Fixture.SpawnCombatant(FVector(-500.f, 0.f, 0.f), 0);
	UTDCombatComponent* Target = Fixture.SpawnCombatant(FVector::ZeroVector);
	Target->ApplyStatus(MakeFreeze(Fixture.World), MakeContext(Caster));
	TestTrue(TEXT("Freeze grants the GAS frozen tag"), Target->HasMatchingGameplayTag(TDGameplayTags::State_Frozen));
	const FGameplayEffectQuery Query = FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(TDGameplayTags::State_Frozen));
	const TArray<FActiveGameplayEffectHandle> Handles = Target->GetActiveEffects(Query);
	TestEqual(TEXT("Freeze is represented by an active gameplay effect"), Handles.Num(), 1);
	for (const FActiveGameplayEffectHandle Handle : Handles)
	{
		Target->RemoveActiveGameplayEffect(Handle);
	}
	TestFalse(TEXT("Removing the effect immediately unfreezes"), Target->IsFrozen());
	Fixture.Tick(0.3f);
	TestEqual(TEXT("Premature removal does not produce natural expiry damage"), Target->GetCurrentHealth(), 1000.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDGASDeathReentryTest, "TDGame.Combat.GAS.RevivalDuringDeadTagApplicationIsConsistent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDGASDeathReentryTest::RunTest(const FString& Parameters)
{
	FTDScopedCombatWorld Fixture;
	UTDCombatComponent* Caster = Fixture.SpawnCombatant(FVector(-500.f, 0.f, 0.f), 0);
	UTDCombatComponent* Target = Fixture.SpawnCombatant(FVector::ZeroVector);
	const FDelegateHandle Handle = Target->RegisterGameplayTagEvent(TDGameplayTags::State_Dead, EGameplayTagEventType::NewOrRemoved).AddLambda([Target](const FGameplayTag, int32 Count)
	{
		if (Count > 0)
		{
			Target->SetStats(Target->GetStats());
		}
	});
	const FTDDamageResult Result = Target->ReceiveDamage(1000.f, ETDDamageElement::Physical, false, MakeContext(Caster));
	TestTrue(TEXT("Callback revival restores an alive state"), Target->IsAlive());
	TestFalse(TEXT("A revived actor cannot retain the just-applied dead effect"), Target->HasMatchingGameplayTag(TDGameplayTags::State_Dead));
	TestFalse(TEXT("Revival before death notification cancels the pending kill"), Result.bWasKilled);
	Target->RegisterGameplayTagEvent(TDGameplayTags::State_Dead, EGameplayTagEventType::NewOrRemoved).Remove(Handle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDGASHealthModifierPolicyTest, "TDGame.Combat.GAS.HealthUsesInstantOrPeriodicResourceEffects", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDGASHealthModifierPolicyTest::RunTest(const FString& Parameters)
{
	FTDScopedCombatWorld Fixture;
	UTDCombatComponent* Target = Fixture.SpawnCombatant(FVector::ZeroVector);
	UGameplayEffect* HealthBuff = NewObject<UGameplayEffect>(Fixture.World);
	HealthBuff->DurationPolicy = EGameplayEffectDurationType::HasDuration;
	HealthBuff->DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(1.f));
	FGameplayModifierInfo& Modifier = HealthBuff->Modifiers.AddDefaulted_GetRef();
	Modifier.Attribute = UTDCombatAttributeSet::GetHealthAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(50.f));
	AddExpectedMessage(TEXT("Rejected persistent Health modifier"), ELogVerbosity::Warning);
	TestFalse(TEXT("Persistent Health modifiers cannot create an unkillable health floor"), Target->ApplyGameplayEffectToSelf(HealthBuff, 1.f, Target->MakeEffectContext()).WasSuccessfullyApplied());
	return true;
}

#endif
