#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/Damage/TDDamageDefinition.h"
#include "Combat/Damage/TDDamageEntity.h"
#include "Combat/Damage/TDDamageExamples.h"
#include "Combat/Damage/TDDamageSubsystem.h"
#include "Combat/Damage/TDStatusDefinition.h"
#include "Combat/TDCombatComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "NiagaraSystem.h"

namespace
{
	const TCHAR* TDMegaMagicRootNames[] = {
		TEXT("DA_TDFireball"), TEXT("DA_TDBlizzard"), TEXT("DA_TDMine"), TEXT("DA_TDShockwave"),
		TEXT("DA_TDMeteor"), TEXT("DA_TDDelayedHoming"), TEXT("DA_TDThunderCage"),
		TEXT("DA_TDVenomBloom"), TEXT("DA_TDAstralLances"), TEXT("DA_TDPhoenixDive")
	};

	const TArray<FTDDamageRule>* GetMegaMagicRules(UObject* Definition)
	{
		if (const UTDDamageDefinition* Damage = Cast<UTDDamageDefinition>(Definition))
		{
			return &Damage->Rules;
		}
		if (const UTDStatusDefinition* Status = Cast<UTDStatusDefinition>(Definition))
		{
			return &Status->Rules;
		}
		return nullptr;
	}

	void GatherMegaMagicGraph(UObject* Definition, TSet<UObject*>& Definitions)
	{
		if (!Definition || Definitions.Contains(Definition))
		{
			return;
		}
		Definitions.Add(Definition);
		if (const TArray<FTDDamageRule>* Rules = GetMegaMagicRules(Definition))
		{
			for (const FTDDamageRule& Rule : *Rules)
			{
				for (const FTDDamageAction& Action : Rule.Actions)
				{
					GatherMegaMagicGraph(Action.Entity, Definitions);
					GatherMegaMagicGraph(Action.Status, Definitions);
				}
			}
		}
	}

	bool ValidateMegaMagicGraph(FAutomationTestBase& Test, UObject* Definition,
		TSet<UObject*>& ActivePath, TSet<UObject*>& Visited, bool bIsSaved)
	{
		if (!Test.TestNotNull(TEXT("Graph references a definition"), Definition))
		{
			return false;
		}
		if (!Test.TestFalse(*FString::Printf(TEXT("%s has no recursive spawn/status cycle"), *Definition->GetName()), ActivePath.Contains(Definition)))
		{
			return false;
		}
		if (Visited.Contains(Definition))
		{
			return true;
		}
		Visited.Add(Definition);
		ActivePath.Add(Definition);
		bool bIsValid = true;
		if (bIsSaved)
		{
			bIsValid &= Test.TestTrue(TEXT("Saved child belongs to the production MegaMagic asset folder"),
				Definition->GetPathName().StartsWith(TEXT("/Game/Combat/MegaMagic/DA_TD")));
			bIsValid &= Test.TestFalse(TEXT("Saved graph contains no transient definitions"), Definition->HasAnyFlags(RF_Transient));
		}
		FString Error;
		if (const UTDDamageDefinition* Damage = Cast<UTDDamageDefinition>(Definition))
		{
			const bool bDefinitionValid = Damage->ValidateDefinition(Error);
			bIsValid &= Test.TestTrue(*FString::Printf(TEXT("%s validates: %s"), *Definition->GetName(), *Error), bDefinitionValid);
			bIsValid &= Test.TestNotNull(TEXT("Every damage stage has Niagara presentation"), Damage->VisualEffect.Get());
			if (Damage->VisualEffect)
			{
				bIsValid &= Test.TestTrue(TEXT("Presentation uses the supplied bundle's gameplay VFX"),
					Damage->VisualEffect->GetPathName().StartsWith(TEXT("/Game/MegaMagicVFXBundle/VFX/")));
			}
			bIsValid &= Test.TestFalse(TEXT("Production spell disables debug geometry"), Damage->bDrawDebug);
			bIsValid &= Test.TestTrue(TEXT("Damage stage has a bounded lifetime"), Damage->Lifetime <= 30.f);
			bIsValid &= Test.TestTrue(TEXT("Presentation tail has a bounded lifetime"), Damage->VisualTailSeconds <= 10.f);
			bIsValid &= Test.TestTrue(TEXT("Production spell filters friendly targets"), Damage->TargetPolicy == ETDDamageTargetPolicy::Enemies);
		}
		if (const UTDStatusDefinition* Status = Cast<UTDStatusDefinition>(Definition))
		{
			const bool bDefinitionValid = Status->ValidateDefinition(Error);
			bIsValid &= Test.TestTrue(*FString::Printf(TEXT("%s validates: %s"), *Definition->GetName(), *Error), bDefinitionValid);
		}
		const TArray<FTDDamageRule>* Rules = GetMegaMagicRules(Definition);
		bIsValid &= Test.TestNotNull(TEXT("Graph contains only native damage/status definitions"), Rules);
		if (Rules)
		{
			for (const FTDDamageRule& Rule : *Rules)
			{
				for (const FTDDamageAction& Action : Rule.Actions)
				{
					if (Action.Entity)
					{
						bIsValid &= ValidateMegaMagicGraph(Test, Action.Entity, ActivePath, Visited, bIsSaved);
					}
					if (Action.Status)
					{
						bIsValid &= ValidateMegaMagicGraph(Test, Action.Status, ActivePath, Visited, bIsSaved);
					}
				}
			}
		}
		ActivePath.Remove(Definition);
		return bIsValid;
	}

	struct FTDMegaMagicTestWorld
	{
		FTDMegaMagicTestWorld()
		{
			const FName Name = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDMegaMagicTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, Name, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
			Caster = SpawnCombatant(FVector(-2000.f, 0.f, 0.f), 0);
			Context.Caster = Caster->GetOwner();
			Context.Stats = Caster->GetStats();
			Context.CastTarget = FVector(400.f, 0.f, 0.f);
			Context.Budget = MakeShared<FTDDamageChainBudget>();
		}

		~FTDMegaMagicTestWorld()
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
			Stats.BaseMaxHealth = 10000.f;
			Combatant->SetStats(Stats);
			Combatant->RegisterComponent();
			return Combatant;
		}

		UTDDamageDefinition* MakeSpell(int32 Index)
		{
			TArray<UTDDamageDefinition*> Spells;
			TDDamageExamples::CreateMegaMagicExamples(World, Spells);
			TSet<UObject*> Definitions;
			for (UTDDamageDefinition* Spell : Spells)
			{
				GatherMegaMagicGraph(Spell, Definitions);
			}
			for (UObject* Definition : Definitions)
			{
				if (UTDDamageDefinition* Damage = Cast<UTDDamageDefinition>(Definition))
				{
					Damage->VisualEffect = nullptr;
				}
			}
			return Spells.IsValidIndex(Index) ? Spells[Index] : nullptr;
		}

		ATDDamageEntity* Spawn(UTDDamageDefinition* Definition)
		{
			return World->GetSubsystem<UTDDamageSubsystem>()->SpawnEntity(Definition, Context, Context.CastTarget);
		}

		void Tick(float Duration)
		{
			for (float Remaining = Duration; Remaining > UE_SMALL_NUMBER;)
			{
				const float Delta = FMath::Min(Remaining, 0.02f);
				++GFrameCounter;
				World->Tick(LEVELTICK_All, Delta);
				Remaining -= Delta;
			}
		}

		int32 CountEntities() const
		{
			int32 Count = 0;
			for (TActorIterator<ATDDamageEntity> Entity(World); Entity; ++Entity)
			{
				Count += !Entity->IsActorBeingDestroyed();
			}
			return Count;
		}

		void CheckCleanup(FAutomationTestBase& Test, UTDCombatComponent* Ally)
		{
			Tick(30.f);
			Test.TestEqual(TEXT("Spell leaves no damage actors after its bounded lifetime"), CountEntities(), 0);
			Test.TestTrue(TEXT("Spell leaves spawn budget available"), Context.Budget->RemainingSpawns > 0);
			Test.TestTrue(TEXT("Spell leaves action budget available"), Context.Budget->RemainingActions > 0);
			Test.TestEqual(TEXT("Friendly combatant takes no damage"), Ally->GetCurrentHealth(), Ally->GetStats().GetMaxHealth());
			Test.TestEqual(TEXT("Caster takes no damage"), Caster->GetCurrentHealth(), Caster->GetStats().GetMaxHealth());
		}

		UWorld* World = nullptr;
		UTDCombatComponent* Caster = nullptr;
		FTDDamageContext Context;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMegaMagicFactoryTest, "TDGame.Combat.MegaMagic.FactoryGraphsAreProductionReady", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMegaMagicFactoryTest::RunTest(const FString& Parameters)
{
	UObject* Outer = NewObject<UTDDamageDefinition>();
	TArray<UTDDamageDefinition*> Spells;
	TDDamageExamples::CreateMegaMagicExamples(Outer, Spells);
	if (!TestEqual(TEXT("All ten spells are available"), Spells.Num(), static_cast<int32>(UE_ARRAY_COUNT(TDMegaMagicRootNames))))
	{
		return false;
	}
	TSet<UObject*> ActivePath;
	TSet<UObject*> Visited;
	for (int32 Index = 0; Index < Spells.Num(); ++Index)
	{
		if (TestNotNull(TEXT("Factory returns a root spell"), Spells[Index]))
		{
			TestEqual(TEXT("Spell selection preserves the authored root ordering"), Spells[Index]->GetName(), FString(TDMegaMagicRootNames[Index]));
			ValidateMegaMagicGraph(*this, Spells[Index], ActivePath, Visited, false);
		}
	}
	TestTrue(TEXT("Validation includes child damage and status stages"), Visited.Num() > Spells.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMegaMagicSavedAssetsTest, "TDGame.Combat.MegaMagic.SavedAssetGraphsLoadAndValidate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMegaMagicSavedAssetsTest::RunTest(const FString& Parameters)
{
	TSet<UObject*> ActivePath;
	TSet<UObject*> Visited;
	for (const TCHAR* Name : TDMegaMagicRootNames)
	{
		const FString Path = FString::Printf(TEXT("/Game/Combat/MegaMagic/%s.%s"), Name, Name);
		UTDDamageDefinition* Definition = LoadObject<UTDDamageDefinition>(nullptr, *Path);
		if (TestNotNull(*FString::Printf(TEXT("Saved spell loads: %s"), *Path), Definition))
		{
			ValidateMegaMagicGraph(*this, Definition, ActivePath, Visited, true);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMegaMagicThunderTest, "TDGame.Combat.MegaMagic.ThunderCageTelegraphsPulsesAndDetonates", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMegaMagicThunderTest::RunTest(const FString& Parameters)
{
	FTDMegaMagicTestWorld Fixture;
	UTDDamageDefinition* Spell = Fixture.MakeSpell(6);
	UTDCombatComponent* Enemy = Fixture.SpawnCombatant(Fixture.Context.CastTarget);
	UTDCombatComponent* Ally = Fixture.SpawnCombatant(Fixture.Context.CastTarget + FVector(0.f, 30.f, 0.f), 0);
	if (!TestNotNull(TEXT("Thunder cage spawns"), Fixture.Spawn(Spell)))
	{
		return false;
	}
	const float InitialHealth = Enemy->GetCurrentHealth();
	Fixture.Tick(Spell->ActivationDelay * 0.5f);
	TestEqual(TEXT("Telegraph causes no early damage"), Enemy->GetCurrentHealth(), InitialHealth);
	Fixture.Tick(Spell->ActivationDelay + Spell->PulseInterval);
	const float FirstPulseHealth = Enemy->GetCurrentHealth();
	TestTrue(TEXT("Activated cage damages enemies"), FirstPulseHealth < InitialHealth);
	Fixture.Tick(Spell->PulseInterval * 1.5f);
	TestTrue(TEXT("Cage deals repeated damage"), Enemy->GetCurrentHealth() < FirstPulseHealth);
	const int32 SpawnsBeforeExpiry = Fixture.Context.Budget->RemainingSpawns;
	const float HealthBeforeExpiry = Enemy->GetCurrentHealth();
	Fixture.Tick(Spell->Lifetime);
	TestTrue(TEXT("Expiry creates the detonation stage"), Fixture.Context.Budget->RemainingSpawns < SpawnsBeforeExpiry);
	TestTrue(TEXT("Detonation damages the target"), Enemy->GetCurrentHealth() < HealthBeforeExpiry);
	Fixture.CheckCleanup(*this, Ally);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMegaMagicVenomTest, "TDGame.Combat.MegaMagic.VenomBloomIgnoresAlliesAndLeavesPersistentDamage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMegaMagicVenomTest::RunTest(const FString& Parameters)
{
	FTDMegaMagicTestWorld Fixture;
	UTDDamageDefinition* Spell = Fixture.MakeSpell(7);
	UTDCombatComponent* Enemy = Fixture.SpawnCombatant(Fixture.Context.CastTarget + FVector(1000.f, 0.f, 0.f));
	UTDCombatComponent* Ally = Fixture.SpawnCombatant(Fixture.Context.CastTarget, 0);
	if (!TestNotNull(TEXT("Venom bloom spawns"), Fixture.Spawn(Spell)))
	{
		return false;
	}
	const int32 ArmedSpawnBudget = Fixture.Context.Budget->RemainingSpawns;
	Fixture.Tick(Spell->ActivationDelay + Spell->PulseInterval * 2.f);
	TestEqual(TEXT("An ally cannot trigger the armed trap"), Fixture.Context.Budget->RemainingSpawns, ArmedSpawnBudget);
	Enemy->GetOwner()->SetActorLocation(Fixture.Context.CastTarget);
	Fixture.Tick(Spell->PulseInterval * 2.f);
	TestTrue(TEXT("Enemy entry creates the venom pool"), Fixture.Context.Budget->RemainingSpawns < ArmedSpawnBudget);
	const float FirstHitHealth = Enemy->GetCurrentHealth();
	TestTrue(TEXT("Triggered pool damages enemies"), FirstHitHealth < Enemy->GetStats().GetMaxHealth());
	Fixture.Tick(1.2f);
	TestTrue(TEXT("Venom pool continues damaging after its trap finishes"), Enemy->GetCurrentHealth() < FirstHitHealth);
	Fixture.CheckCleanup(*this, Ally);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMegaMagicAstralTest, "TDGame.Combat.MegaMagic.AstralLancesLaunchSeekAndImpact", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMegaMagicAstralTest::RunTest(const FString& Parameters)
{
	FTDMegaMagicTestWorld Fixture;
	UTDDamageDefinition* Spell = Fixture.MakeSpell(8);
	UTDCombatComponent* Enemy = Fixture.SpawnCombatant(Fixture.Context.CastTarget);
	UTDCombatComponent* Ally = Fixture.SpawnCombatant(Fixture.Context.CastTarget + FVector(0.f, 30.f, 0.f), 0);
	if (!TestNotNull(TEXT("Astral lance launcher spawns"), Fixture.Spawn(Spell)))
	{
		return false;
	}
	const int32 InitialSpawnBudget = Fixture.Context.Budget->RemainingSpawns;
	Fixture.Tick(0.1f);
	TestEqual(TEXT("Lances respect their launch delay"), Fixture.Context.Budget->RemainingSpawns, InitialSpawnBudget);
	Fixture.Tick(0.15f);
	const int32 FirstLaunchBudget = Fixture.Context.Budget->RemainingSpawns;
	TestTrue(TEXT("First lance launches after its deadline"), FirstLaunchBudget < InitialSpawnBudget);
	Fixture.Tick(4.f);
	TestTrue(TEXT("Later launches and impacts spawn additional stages"), Fixture.Context.Budget->RemainingSpawns < FirstLaunchBudget - 2);
	TestTrue(TEXT("Descending homing lances reach the enemy"), Enemy->GetCurrentHealth() < Enemy->GetStats().GetMaxHealth());
	Fixture.CheckCleanup(*this, Ally);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMegaMagicPhoenixTest, "TDGame.Combat.MegaMagic.PhoenixDiveCreatesBlastWaveAndBurningGround", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMegaMagicPhoenixTest::RunTest(const FString& Parameters)
{
	FTDMegaMagicTestWorld Fixture;
	UTDDamageDefinition* Spell = Fixture.MakeSpell(9);
	UTDCombatComponent* Enemy = Fixture.SpawnCombatant(Fixture.Context.CastTarget);
	UTDCombatComponent* OuterEnemy = Fixture.SpawnCombatant(Fixture.Context.CastTarget + FVector(420.f, 0.f, 0.f));
	UTDCombatComponent* Ally = Fixture.SpawnCombatant(Fixture.Context.CastTarget + FVector(0.f, 30.f, 0.f), 0);
	if (!TestNotNull(TEXT("Phoenix dive telegraph spawns"), Fixture.Spawn(Spell)))
	{
		return false;
	}
	const int32 InitialSpawnBudget = Fixture.Context.Budget->RemainingSpawns;
	Fixture.Tick(0.25f);
	TestEqual(TEXT("Telegraph delays the falling projectile"), Fixture.Context.Budget->RemainingSpawns, InitialSpawnBudget);
	TestEqual(TEXT("Telegraph deals no damage"), Enemy->GetCurrentHealth(), Enemy->GetStats().GetMaxHealth());
	Fixture.Tick(2.5f);
	TestTrue(TEXT("Dive impact spawns its aftermath stages"), Fixture.Context.Budget->RemainingSpawns < InitialSpawnBudget - 2);
	TestTrue(TEXT("Dive damages the enemy at its center"), Enemy->GetCurrentHealth() < Enemy->GetStats().GetMaxHealth());
	TestTrue(TEXT("Expanding wake reaches an enemy beyond the explosion radius"), OuterEnemy->GetCurrentHealth() < OuterEnemy->GetStats().GetMaxHealth());
	const float ImpactHealth = Enemy->GetCurrentHealth();
	Fixture.Tick(1.f);
	TestTrue(TEXT("Burning ground persists after impact"), Enemy->GetCurrentHealth() < ImpactHealth);
	Fixture.CheckCleanup(*this, Ally);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMegaMagicVisualTailTest, "TDGame.Combat.MegaMagic.VisualTailStopsGameplayAndExpires", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMegaMagicVisualTailTest::RunTest(const FString& Parameters)
{
	FTDMegaMagicTestWorld Fixture;
	UTDCombatComponent* Enemy = Fixture.SpawnCombatant(Fixture.Context.CastTarget);
	UTDDamageDefinition* Definition = NewObject<UTDDamageDefinition>(Fixture.World);
	Definition->Lifetime = 2.f;
	Definition->PulseInterval = 0.05f;
	Definition->HitInterval = 0.05f;
	Definition->MaxHitsPerTarget = 0;
	Definition->VisualEffect = NewObject<UNiagaraSystem>(Fixture.World);
	Definition->VisualTailSeconds = 0.4f;
	FTDDamageRule& DamageRule = Definition->Rules.AddDefaulted_GetRef();
	DamageRule.Event = ETDDamageEvent::Hit;
	DamageRule.Actions.AddDefaulted_GetRef().Magnitude.Base = 5.f;
	FTDDamageRule& SpawnRule = Definition->Rules.AddDefaulted_GetRef();
	SpawnRule.Event = ETDDamageEvent::Spawn;
	FTDDamageAction& DelayedSpawn = SpawnRule.Actions.AddDefaulted_GetRef();
	DelayedSpawn.Type = ETDDamageActionType::SpawnEntity;
	DelayedSpawn.DelaySeconds = 0.2f;
	DelayedSpawn.Entity = NewObject<UTDDamageDefinition>(Fixture.World);
	ATDDamageEntity* Entity = Fixture.Spawn(Definition);
	if (!TestNotNull(TEXT("Tail test entity spawns"), Entity))
	{
		return false;
	}
	Fixture.Tick(0.06f);
	TestTrue(TEXT("Active entity deals damage"), Enemy->GetCurrentHealth() < Enemy->GetStats().GetMaxHealth());
	Entity->Finish();
	TestFalse(TEXT("Visual tail retains the actor after gameplay completes"), Entity->IsActorBeingDestroyed());
	TestFalse(TEXT("Tail disables gameplay ticking"), Entity->IsActorTickEnabled());
	const float FinishedHealth = Enemy->GetCurrentHealth();
	const int32 FinishedSpawnBudget = Fixture.Context.Budget->RemainingSpawns;
	Fixture.Tick(0.25f);
	TestEqual(TEXT("Tail cannot repeat damage"), Enemy->GetCurrentHealth(), FinishedHealth);
	TestEqual(TEXT("Tail cancels source-owned delayed spawns"), Fixture.Context.Budget->RemainingSpawns, FinishedSpawnBudget);
	Fixture.Tick(0.25f);
	TestEqual(TEXT("Tail destroys the retained actor on time"), Fixture.CountEntities(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMegaMagicFinishDuringDamageTest, "TDGame.Combat.MegaMagic.FinishDuringDamageCancelsRemainingActions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMegaMagicFinishDuringDamageTest::RunTest(const FString& Parameters)
{
	for (const bool bIsLethal : { false, true })
	{
		FTDMegaMagicTestWorld Fixture;
		UTDCombatComponent* Enemy = Fixture.SpawnCombatant(Fixture.Context.CastTarget);
		UTDStatusDefinition* Freeze = NewObject<UTDStatusDefinition>(Fixture.World);
		Freeze->bFreezesTarget = true;
		UTDDamageDefinition* ForbiddenChild = NewObject<UTDDamageDefinition>(Fixture.World);
		ForbiddenChild->Lifetime = 0.15f;
		UTDDamageDefinition* EndChild = NewObject<UTDDamageDefinition>(Fixture.World);
		EndChild->Lifetime = 0.15f;
		UTDDamageDefinition* Definition = NewObject<UTDDamageDefinition>(Fixture.World);
		Definition->ActivationDelay = 0.05f;
		Definition->Lifetime = 2.f;
		Definition->VisualEffect = NewObject<UNiagaraSystem>(Fixture.World);
		Definition->VisualTailSeconds = 0.4f;
		FTDDamageAction SpawnForbidden;
		SpawnForbidden.Type = ETDDamageActionType::SpawnEntity;
		SpawnForbidden.Entity = ForbiddenChild;
		FTDDamageRule& HitRule = Definition->Rules.AddDefaulted_GetRef();
		HitRule.Event = ETDDamageEvent::Hit;
		FTDDamageAction& Damage = HitRule.Actions.AddDefaulted_GetRef();
		Damage.Magnitude.Base = bIsLethal ? Enemy->GetStats().GetMaxHealth() * 2.f : 5.f;
		Damage.Status = Freeze;
		HitRule.Actions.Add(SpawnForbidden);
		FTDDamageAction& ApplyFreeze = HitRule.Actions.AddDefaulted_GetRef();
		ApplyFreeze.Type = ETDDamageActionType::ApplyStatus;
		ApplyFreeze.Status = Freeze;
		FTDDamageRule& KillRule = Definition->Rules.AddDefaulted_GetRef();
		KillRule.Event = ETDDamageEvent::Kill;
		KillRule.Actions.Add(SpawnForbidden);
		FTDDamageRule& EndRule = Definition->Rules.AddDefaulted_GetRef();
		EndRule.Event = ETDDamageEvent::End;
		FTDDamageAction& SpawnEnd = EndRule.Actions.AddDefaulted_GetRef();
		SpawnEnd.Type = ETDDamageActionType::SpawnEntity;
		SpawnEnd.Entity = EndChild;
		FTDDamageRule& SpawnRule = Definition->Rules.AddDefaulted_GetRef();
		SpawnRule.Event = ETDDamageEvent::Spawn;
		SpawnForbidden.DelaySeconds = 0.2f;
		SpawnRule.Actions.Add(SpawnForbidden);
		const int32 InitialSpawnBudget = Fixture.Context.Budget->RemainingSpawns;
		ATDDamageEntity* Source = Fixture.Spawn(Definition);
		if (!TestNotNull(TEXT("Reentrant completion source spawns"), Source))
		{
			return false;
		}
		const FDelegateHandle DamageHandle = Enemy->OnDamaged.AddLambda(
			[Source](const FTDDamageResult&, const FTDDamageContext&)
			{
				Source->Finish();
				Source->Finish();
			});
		Fixture.Tick(0.08f);
		TestTrue(TEXT("Damage callback finishes gameplay"), Source->IsGameplayFinished());
		TestFalse(TEXT("Finished source remains alive for its visual tail"), Source->IsActorBeingDestroyed());
		TestEqual(TEXT("Only the root and one terminal End child consume spawn budget"),
			Fixture.Context.Budget->RemainingSpawns, InitialSpawnBudget - 2);
		TestEqual(TEXT("End chain executes once while unfinished Hit and Kill chains are canceled"), Fixture.CountEntities(), 2);
		TestFalse(TEXT("Damage callback completion prevents attached and subsequent status application"), Enemy->IsFrozen());
		TestEqual(TEXT("Damage delivery still respects the lethal case"), Enemy->IsAlive(), !bIsLethal);
		const float FinishedHealth = Enemy->GetCurrentHealth();
		Fixture.Tick(0.25f);
		TestEqual(TEXT("Finished source cannot execute its pre-existing delayed spawn"),
			Fixture.Context.Budget->RemainingSpawns, InitialSpawnBudget - 2);
		TestEqual(TEXT("No damage continues during the visual tail"), Enemy->GetCurrentHealth(), FinishedHealth);
		Fixture.Tick(0.25f);
		TestEqual(TEXT("Reentrant completion and its terminal child clean up"), Fixture.CountEntities(), 0);
		Enemy->OnDamaged.Remove(DamageHandle);
	}
	return true;
}

#endif
