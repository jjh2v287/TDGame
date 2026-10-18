#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/TDCombatComponent.h"
#include "Combat/TDCombatLibrary.h"
#include "Combat/Damage/TDDamageDefinition.h"
#include "Combat/Damage/TDDamageSubsystem.h"
#include "Combat/Damage/TDStatusDefinition.h"
#include "Components/SphereComponent.h"
#include "Core/TDGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "Misc/AutomationTest.h"

namespace TDDamageEventTests
{
	struct FTDDamageEventTestWorld
	{
		FTDDamageEventTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDDamageEventTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDDamageEventTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		FTDDamageEventTestWorld(const FTDDamageEventTestWorld&) = delete;
		FTDDamageEventTestWorld& operator=(const FTDDamageEventTestWorld&) = delete;

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

		void Tick(float Duration, float Step = 0.01f)
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

	struct FTDDamageEventRecorder
	{
		explicit FTDDamageEventRecorder(UTDCombatComponent* InTarget)
			: Target(InTarget)
		{
			DamagedHandle = Target->OnDamaged.AddLambda([this](const FTDDamageResult& Result, const FTDDamageContext&)
			{
				Results.Add(Result);
			});
			AvoidedHandle = Target->OnDamageAvoided.AddLambda([this](const FTDDamageContext&, const FVector& Location)
			{
				AvoidedLocations.Add(Location);
			});
			DeathHandle = Target->OnDeath.AddLambda([this](const FTDDamageContext&)
			{
				++DeathCount;
			});
		}

		~FTDDamageEventRecorder()
		{
			if (!IsValid(Target))
			{
				return;
			}
			Target->OnDamaged.Remove(DamagedHandle);
			Target->OnDamageAvoided.Remove(AvoidedHandle);
			Target->OnDeath.Remove(DeathHandle);
		}

		FTDDamageEventRecorder(const FTDDamageEventRecorder&) = delete;
		FTDDamageEventRecorder& operator=(const FTDDamageEventRecorder&) = delete;

		UTDCombatComponent* Target = nullptr;
		TArray<FTDDamageResult> Results;
		TArray<FVector> AvoidedLocations;
		int32 DeathCount = 0;
		FDelegateHandle DamagedHandle;
		FDelegateHandle AvoidedHandle;
		FDelegateHandle DeathHandle;
	};

	FTDDamageContext MakeDamageEventContext(UTDCombatComponent* Caster, ETDDamageDelivery Delivery = ETDDamageDelivery::Direct)
	{
		FTDDamageContext Context;
		Context.Caster = Caster->GetOwner();
		Context.Stats = Caster->GetStats();
		Context.Delivery = Delivery;
		Context.Budget = MakeShared<FTDDamageChainBudget>();
		return Context;
	}

	FTDDamageAction MakeDamageEventAction(float Amount, ETDDamageElement Element = ETDDamageElement::Physical)
	{
		FTDDamageAction Action;
		Action.Magnitude.Base = Amount;
		Action.Element = Element;
		return Action;
	}

	FTDDamageRule MakeDamageEventRule(ETDDamageEvent Event, const FTDDamageAction& Action)
	{
		FTDDamageRule Rule;
		Rule.Event = Event;
		Rule.Actions.Add(Action);
		return Rule;
	}

	UTDStatusDefinition* MakeDamageEventFreeze(UObject* Outer)
	{
		UTDStatusDefinition* Status = NewObject<UTDStatusDefinition>(Outer);
		Status->Duration = 0.12f;
		Status->bFreezesTarget = true;
		return Status;
	}

	bool AreAllDeliveries(const TArray<FTDDamageResult>& Results, int32 FirstIndex, ETDDamageDelivery Delivery)
	{
		for (int32 Index = FirstIndex; Index < Results.Num(); ++Index)
		{
			if (Results[Index].Delivery != Delivery)
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDamageEventResultCarriesHitInfoTest, "TDGame.Combat.DamageEvent.ResultCarriesHitInfo", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDamageEventResultCarriesHitInfoTest::RunTest(const FString& Parameters)
{
	using namespace TDDamageEventTests;
	FTDDamageEventTestWorld Fixture;
	UTDCombatComponent* Caster = Fixture.SpawnCombatant(FVector(-300.f, 0.f, 0.f), 0);
	UTDCombatComponent* Target = Fixture.SpawnCombatant(FVector::ZeroVector);
	FTDDamageEventRecorder Recorder(Target);
	FTDDamageHitInfo HitInfo;
	HitInfo.Location = FVector(-8.f, 4.f, 50.f);
	HitInfo.Direction = FVector(0.f, 3.f, 2.f);
	HitInfo.bHasLocation = true;
	Target->ReceiveDamage(100.f, ETDDamageElement::Fire, false, MakeDamageEventContext(Caster, ETDDamageDelivery::Melee), HitInfo);
	if (!TestEqual(TEXT("One damage event is broadcast"), Recorder.Results.Num(), 1))
	{
		return false;
	}
	const FTDDamageResult& Broadcast = Recorder.Results[0];
	TestEqual(TEXT("Unmitigated fire damage is applied in full"), Broadcast.AppliedDamage, 100.f);
	TestTrue(TEXT("Result carries the damage element"), Broadcast.Element == ETDDamageElement::Fire);
	TestTrue(TEXT("Result carries the context delivery"), Broadcast.Delivery == ETDDamageDelivery::Melee);
	TestTrue(TEXT("Result reports an authored hit location"), Broadcast.bHasHitLocation);
	TestTrue(TEXT("Result carries the authored hit location"), Broadcast.HitLocation.Equals(HitInfo.Location, 0.01f));
	TestTrue(TEXT("Authored hit direction is flattened and normalized"), Broadcast.HitDirection.Equals(FVector(0.f, 1.f, 0.f), 0.001f));
	TestEqual(TEXT("Damage ratio is applied damage over maximum health"), Broadcast.DamageRatio, 0.1f, 0.0001f);
	TestTrue(TEXT("Last damage result is stored before the broadcast"), Target->GetLastDamageResult().HitLocation.Equals(HitInfo.Location, 0.01f)
		&& Target->GetLastDamageResult().Delivery == ETDDamageDelivery::Melee);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDamageEventResultReturnsDamageRatioTest, "TDGame.Combat.DamageEvent.ResultReturnsDamageRatio", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDamageEventResultReturnsDamageRatioTest::RunTest(const FString& Parameters)
{
	using namespace TDDamageEventTests;
	FTDDamageEventTestWorld Fixture;
	UTDCombatComponent* Caster = Fixture.SpawnCombatant(FVector(-300.f, 0.f, 0.f), 0);
	UTDCombatComponent* Target = Fixture.SpawnCombatant(FVector::ZeroVector);
	FTDCombatStats TargetStats = Target->GetStats();
	TargetStats.Armor = 100.f;
	Target->SetStats(TargetStats);
	TestEqual(TEXT("Maximum health accessor reads the attribute set"), Target->GetMaxHealth(), 1000.f);
	const FTDDamageResult Returned = Target->ReceiveDamage(100.f, ETDDamageElement::Physical, false, MakeDamageEventContext(Caster));
	TestEqual(TEXT("Armor halves the returned damage"), Returned.AppliedDamage, 50.f);
	TestEqual(TEXT("Returned result carries the damage ratio"), Returned.DamageRatio, 0.05f, 0.0001f);
	TestTrue(TEXT("Returned result carries the delivery"), Returned.Delivery == ETDDamageDelivery::Direct);
	TestTrue(TEXT("Returned result carries the fallback direction"), Returned.HitDirection.Equals(FVector(1.f, 0.f, 0.f), 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDamageEventDirectionFallbackTest, "TDGame.Combat.DamageEvent.DirectionFallsBackToCasterVector", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDamageEventDirectionFallbackTest::RunTest(const FString& Parameters)
{
	using namespace TDDamageEventTests;
	FTDDamageEventTestWorld Fixture;
	UTDCombatComponent* Caster = Fixture.SpawnCombatant(FVector(0.f, -400.f, 30.f), 0);
	UTDCombatComponent* Target = Fixture.SpawnCombatant(FVector::ZeroVector);
	FTDDamageEventRecorder Recorder(Target);
	const FTDDamageContext Context = MakeDamageEventContext(Caster);
	Target->ReceiveDamage(10.f, ETDDamageElement::Physical, false, Context);
	FTDDamageHitInfo VerticalHit;
	VerticalHit.Direction = FVector::DownVector;
	Target->ReceiveDamage(10.f, ETDDamageElement::Physical, false, Context, VerticalHit);
	FTDDamageContext CasterlessContext = Context;
	CasterlessContext.Caster.Reset();
	Target->ReceiveDamage(10.f, ETDDamageElement::Physical, false, CasterlessContext);
	if (!TestEqual(TEXT("Three damage events are broadcast"), Recorder.Results.Num(), 3))
	{
		return false;
	}
	TestTrue(TEXT("Missing direction falls back to caster toward target"), Recorder.Results[0].HitDirection.Equals(FVector(0.f, 1.f, 0.f), 0.001f));
	TestFalse(TEXT("Missing location is not reported as authored"), Recorder.Results[0].bHasHitLocation);
	TestTrue(TEXT("Missing location falls back to the target actor location"), Recorder.Results[0].HitLocation.Equals(FVector::ZeroVector, 0.01f));
	TestTrue(TEXT("Vertical-only direction falls back to caster toward target"), Recorder.Results[1].HitDirection.Equals(FVector(0.f, 1.f, 0.f), 0.001f));
	TestTrue(TEXT("Missing caster falls back to the target backward vector"), Recorder.Results[2].HitDirection.Equals(FVector(-1.f, 0.f, 0.f), 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDamageEventInvulnerableTest, "TDGame.Combat.DamageEvent.InvulnerableBlocksDamage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDamageEventInvulnerableTest::RunTest(const FString& Parameters)
{
	using namespace TDDamageEventTests;
	FTDDamageEventTestWorld Fixture;
	UTDCombatComponent* Caster = Fixture.SpawnCombatant(FVector(-300.f, 0.f, 0.f), 0);
	UTDCombatComponent* Target = Fixture.SpawnCombatant(FVector::ZeroVector);
	FTDDamageEventRecorder Recorder(Target);
	const FTDDamageContext Context = MakeDamageEventContext(Caster);
	FTDDamageHitInfo HitInfo;
	HitInfo.Location = FVector(-10.f, 0.f, 40.f);
	HitInfo.bHasLocation = true;
	FTDDamageAction FreezeAction;
	FreezeAction.Type = ETDDamageActionType::ApplyStatus;
	FreezeAction.Status = MakeDamageEventFreeze(Fixture.World);
	const TArray<FTDDamageRule> FreezeRules = { MakeDamageEventRule(ETDDamageEvent::Hit, FreezeAction) };

	Target->AddLooseGameplayTag(TDGameplayTags::State_Invulnerable);
	const FTDDamageResult Blocked = Target->ReceiveDamage(100.f, ETDDamageElement::Physical, false, Context, HitInfo);
	Fixture.GetDamageSubsystem()->ExecuteRules(FreezeRules, ETDDamageEvent::Hit, Context, Target->GetOwner(), FVector::ZeroVector);
	TestEqual(TEXT("Invulnerable target takes no damage"), Blocked.AppliedDamage, 0.f);
	TestEqual(TEXT("Invulnerable target keeps full health"), Target->GetCurrentHealth(), 1000.f);
	TestEqual(TEXT("Invulnerable target broadcasts no damage event"), Recorder.Results.Num(), 0);
	TestFalse(TEXT("Invulnerable target ignores status actions"), Target->IsFrozen());
	if (TestEqual(TEXT("Avoided damage is broadcast once"), Recorder.AvoidedLocations.Num(), 1))
	{
		TestTrue(TEXT("Avoided broadcast carries the hit location"), Recorder.AvoidedLocations[0].Equals(HitInfo.Location, 0.01f));
	}

	Target->RemoveLooseGameplayTag(TDGameplayTags::State_Invulnerable);
	const FTDDamageResult Applied = Target->ReceiveDamage(100.f, ETDDamageElement::Physical, false, Context, HitInfo);
	Fixture.GetDamageSubsystem()->ExecuteRules(FreezeRules, ETDDamageEvent::Hit, Context, Target->GetOwner(), FVector::ZeroVector);
	TestEqual(TEXT("Damage applies again after invulnerability ends"), Applied.AppliedDamage, 100.f);
	TestTrue(TEXT("Status actions apply again after invulnerability ends"), Target->IsFrozen());
	TestEqual(TEXT("No further avoided broadcast"), Recorder.AvoidedLocations.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDamageEventAvoidedWithoutGameInstanceTest, "TDGame.Combat.DamageEvent.AvoidedWithoutGameInstanceIsSafe", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDamageEventAvoidedWithoutGameInstanceTest::RunTest(const FString& Parameters)
{
	using namespace TDDamageEventTests;
	FTDDamageEventTestWorld Fixture;
	TestFalse(TEXT("Fixture world has no message subsystem"), UGameplayMessageSubsystem::HasInstance(Fixture.World));
	UTDCombatComponent* Caster = Fixture.SpawnCombatant(FVector(-300.f, 0.f, 0.f), 0);
	UTDCombatComponent* Target = Fixture.SpawnCombatant(FVector::ZeroVector);
	FTDDamageEventRecorder Recorder(Target);
	FTDDamageAction FrostHit = MakeDamageEventAction(50.f, ETDDamageElement::Frost);
	FrostHit.Status = MakeDamageEventFreeze(Fixture.World);
	const TArray<FTDDamageRule> Rules = { MakeDamageEventRule(ETDDamageEvent::Hit, FrostHit) };
	Target->AddLooseGameplayTag(TDGameplayTags::State_Invulnerable);
	Fixture.GetDamageSubsystem()->ExecuteRules(Rules, ETDDamageEvent::Hit, MakeDamageEventContext(Caster, ETDDamageDelivery::Melee), Target->GetOwner(), FVector(-10.f, 0.f, 40.f));
	TestEqual(TEXT("Avoided rule damage is broadcast once without a game instance"), Recorder.AvoidedLocations.Num(), 1);
	TestEqual(TEXT("Avoided rule damage leaves health unchanged"), Target->GetCurrentHealth(), 1000.f);
	TestFalse(TEXT("Avoided rule damage does not apply its status"), Target->IsFrozen());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDamageEventStatusDeliveryTest, "TDGame.Combat.DamageEvent.StatusDeliveryMarked", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDamageEventStatusDeliveryTest::RunTest(const FString& Parameters)
{
	using namespace TDDamageEventTests;
	FTDDamageEventTestWorld Fixture;
	UTDCombatComponent* Caster = Fixture.SpawnCombatant(FVector(-300.f, 0.f, 0.f), 0);
	UTDCombatComponent* Target = Fixture.SpawnCombatant(FVector::ZeroVector);
	FTDDamageEventRecorder Recorder(Target);
	UTDStatusDefinition* Burn = NewObject<UTDStatusDefinition>(Fixture.World);
	Burn->Duration = 0.35f;
	Burn->PulseInterval = 0.1f;
	Burn->Rules.Add(MakeDamageEventRule(ETDDamageEvent::Spawn, MakeDamageEventAction(5.f, ETDDamageElement::Fire)));
	Burn->Rules.Add(MakeDamageEventRule(ETDDamageEvent::Pulse, MakeDamageEventAction(5.f, ETDDamageElement::Fire)));
	Target->ApplyStatus(Burn, MakeDamageEventContext(Caster, ETDDamageDelivery::Melee));
	if (!TestEqual(TEXT("Status spawn damage is applied immediately"), Recorder.Results.Num(), 1))
	{
		return false;
	}
	Fixture.Tick(0.3f);
	TestTrue(TEXT("Status pulses apply further damage"), Recorder.Results.Num() >= 3);
	TestTrue(TEXT("Every status damage event is marked as status delivery"), AreAllDeliveries(Recorder.Results, 0, ETDDamageDelivery::Status));
	for (const FTDDamageResult& Result : Recorder.Results)
	{
		TestFalse(TEXT("Status damage has no authored hit location"), Result.bHasHitLocation);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDamageEventRuleDeliveryTest, "TDGame.Combat.DamageEvent.RuleDeliveryFollowsSource", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDamageEventRuleDeliveryTest::RunTest(const FString& Parameters)
{
	using namespace TDDamageEventTests;
	FTDDamageEventTestWorld Fixture;
	UTDCombatComponent* Caster = Fixture.SpawnCombatant(FVector(-500.f, 0.f, 0.f), 0);
	UTDCombatComponent* Target = Fixture.SpawnCombatant(FVector::ZeroVector);
	FTDDamageEventRecorder Recorder(Target);
	const TArray<FTDDamageRule> HitRules = { MakeDamageEventRule(ETDDamageEvent::Hit, MakeDamageEventAction(10.f)) };
	const FVector ImpactLocation(-9.f, 2.f, 60.f);
	Fixture.GetDamageSubsystem()->ExecuteRules(HitRules, ETDDamageEvent::Hit, MakeDamageEventContext(Caster, ETDDamageDelivery::Melee), Target->GetOwner(), ImpactLocation);
	Fixture.GetDamageSubsystem()->ExecuteRules(HitRules, ETDDamageEvent::Hit, MakeDamageEventContext(Caster), Target->GetOwner(), ImpactLocation);
	if (!TestEqual(TEXT("Two rule damage events are broadcast"), Recorder.Results.Num(), 2))
	{
		return false;
	}
	TestTrue(TEXT("Melee rule damage keeps melee delivery"), Recorder.Results[0].Delivery == ETDDamageDelivery::Melee);
	TestTrue(TEXT("Melee rule damage reports the impact location"), Recorder.Results[0].bHasHitLocation && Recorder.Results[0].HitLocation.Equals(ImpactLocation, 0.01f));
	TestTrue(TEXT("Direct rule damage keeps direct delivery"), Recorder.Results[1].Delivery == ETDDamageDelivery::Direct);
	TestFalse(TEXT("Direct rule damage has no authored hit location"), Recorder.Results[1].bHasHitLocation);

	UTDDamageDefinition* Area = NewObject<UTDDamageDefinition>(Fixture.World);
	Area->Lifetime = 0.5f;
	Area->PulseInterval = 0.05f;
	Area->HitInterval = 0.05f;
	Area->MaxHitsPerTarget = 0;
	Area->bDestroyOnHit = false;
	Area->Rules.Add(MakeDamageEventRule(ETDDamageEvent::Hit, MakeDamageEventAction(1.f, ETDDamageElement::Arcane)));
	FTDDamageAction SpawnArea;
	SpawnArea.Type = ETDDamageActionType::SpawnEntity;
	SpawnArea.Entity = Area;
	SpawnArea.SpawnAnchor = ETDDamageSpawnAnchor::Target;
	Fixture.GetDamageSubsystem()->ExecuteRules({ MakeDamageEventRule(ETDDamageEvent::Hit, SpawnArea) }, ETDDamageEvent::Hit, MakeDamageEventContext(Caster, ETDDamageDelivery::Melee), Target->GetOwner(), ImpactLocation);
	Fixture.Tick(0.2f);
	if (!TestTrue(TEXT("Area spawned by a melee rule deals damage"), Recorder.Results.Num() > 2))
	{
		return false;
	}
	TestTrue(TEXT("Entity damage is spell delivery even from a melee context"), AreAllDeliveries(Recorder.Results, 2, ETDDamageDelivery::Spell));
	TestFalse(TEXT("Area damage has no authored hit location"), Recorder.Results[2].bHasHitLocation);
	TestTrue(TEXT("Area damage centered on the target falls back to caster toward target"), Recorder.Results[2].HitDirection.Equals(FVector(1.f, 0.f, 0.f), 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDamageEventSpellProjectileTest, "TDGame.Combat.DamageEvent.SpellProjectileCarriesImpact", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDamageEventSpellProjectileTest::RunTest(const FString& Parameters)
{
	using namespace TDDamageEventTests;
	FTDDamageEventTestWorld Fixture;
	UTDCombatComponent* Caster = Fixture.SpawnCombatant(FVector(-200.f, 0.f, 0.f), 0);
	UTDCombatComponent* Target = Fixture.SpawnCombatant(FVector::ZeroVector);
	FTDDamageEventRecorder Recorder(Target);
	UTDDamageDefinition* Projectile = NewObject<UTDDamageDefinition>(Fixture.World);
	Projectile->Mode = ETDDamageEntityMode::Projectile;
	Projectile->ProjectileSpeed = 2000.f;
	Projectile->Rules.Add(MakeDamageEventRule(ETDDamageEvent::Hit, MakeDamageEventAction(5.f, ETDDamageElement::Fire)));
	const bool bWasCast = Fixture.GetDamageSubsystem()->Cast(Projectile, Caster->GetOwner(), Caster->GetOwner()->GetActorLocation(), FVector::ZeroVector) != nullptr;
	if (!TestTrue(TEXT("Projectile is spawned by the public cast path"), bWasCast))
	{
		return false;
	}
	Fixture.Tick(0.3f);
	if (!TestEqual(TEXT("Projectile hits the target once"), Recorder.Results.Num(), 1))
	{
		return false;
	}
	const FTDDamageResult& Impact = Recorder.Results[0];
	TestTrue(TEXT("Projectile damage is spell delivery"), Impact.Delivery == ETDDamageDelivery::Spell);
	TestTrue(TEXT("Projectile damage reports its impact location"), Impact.bHasHitLocation);
	TestTrue(TEXT("Projectile damage follows the travel direction"), Impact.HitDirection.Equals(FVector(1.f, 0.f, 0.f), 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDamageEventNoMessageSubsystemTest, "TDGame.Combat.DamageEvent.NoMessageSubsystemIsSafe", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDamageEventNoMessageSubsystemTest::RunTest(const FString& Parameters)
{
	using namespace TDDamageEventTests;
	FTDDamageEventTestWorld Fixture;
	TestFalse(TEXT("Fixture world has no message subsystem"), UGameplayMessageSubsystem::HasInstance(Fixture.World));
	UTDCombatComponent* Caster = Fixture.SpawnCombatant(FVector(-300.f, 0.f, 0.f), 0);
	UTDCombatComponent* Target = Fixture.SpawnCombatant(FVector::ZeroVector);
	FTDDamageEventRecorder Recorder(Target);
	FTDDamageSpec Spec;
	Spec.Amount = 200.f;
	Spec.Instigator = Caster->GetOwner();
	TestEqual(TEXT("Library damage applies without a game instance"), UTDCombatLibrary::TryApplyDamage(Target->GetOwner(), Spec), 200.f);
	const FTDDamageResult Killing = Target->ReceiveDamage(5000.f, ETDDamageElement::Physical, false, MakeDamageEventContext(Caster));
	TestTrue(TEXT("Lethal damage reports the kill"), Killing.bWasKilled);
	TestEqual(TEXT("Lethal damage ratio reflects remaining health"), Killing.DamageRatio, 0.8f, 0.0001f);
	TestEqual(TEXT("Death is broadcast once"), Recorder.DeathCount, 1);
	TestEqual(TEXT("Both hits broadcast damage events"), Recorder.Results.Num(), 2);
	TestTrue(TEXT("Last damage result records the kill"), Target->GetLastDamageResult().bWasKilled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDamageEventUnclampedDamageOnKillTest, "TDGame.Combat.DamageEvent.UnclampedDamageOnKill", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDamageEventUnclampedDamageOnKillTest::RunTest(const FString& Parameters)
{
	using namespace TDDamageEventTests;
	FTDDamageEventTestWorld Fixture;
	UTDCombatComponent* Caster = Fixture.SpawnCombatant(FVector(-300.f, 0.f, 0.f), 0);
	UTDCombatComponent* Target = Fixture.SpawnCombatant(FVector::ZeroVector);
	FTDCombatStats TargetStats = Target->GetStats();
	TargetStats.Armor = 100.f;
	Target->SetStats(TargetStats);
	FTDDamageEventRecorder Recorder(Target);
	const FTDDamageContext Context = MakeDamageEventContext(Caster, ETDDamageDelivery::Melee);
	const FTDDamageResult Wounding = Target->ReceiveDamage(1990.f, ETDDamageElement::Physical, false, Context);
	TestEqual(TEXT("Armor halves the wounding hit"), Wounding.AppliedDamage, 995.f);
	TestEqual(TEXT("A hit within remaining health reports the same unclamped damage"), Wounding.UnclampedDamage, 995.f);
	if (!TestEqual(TEXT("Five health remains before the killing hit"), Target->GetCurrentHealth(), 5.f))
	{
		return false;
	}
	const FTDDamageResult Killing = Target->ReceiveDamage(44.f, ETDDamageElement::Physical, false, Context);
	TestTrue(TEXT("The overflowing hit kills"), Killing.bWasKilled);
	TestEqual(TEXT("Applied damage is clamped to the remaining health"), Killing.AppliedDamage, 5.f);
	TestEqual(TEXT("Unclamped damage keeps the mitigated amount before the health clamp"), Killing.UnclampedDamage, 22.f);
	TestEqual(TEXT("Damage ratio still uses the applied damage"), Killing.DamageRatio, 0.005f, 0.00001f);
	if (TestEqual(TEXT("Both hits broadcast damage events"), Recorder.Results.Num(), 2))
	{
		TestEqual(TEXT("Broadcast result keeps the clamped applied damage"), Recorder.Results[1].AppliedDamage, 5.f);
		TestEqual(TEXT("Broadcast result carries the unclamped damage"), Recorder.Results[1].UnclampedDamage, 22.f);
	}
	TestEqual(TEXT("Last damage result carries the unclamped damage"), Target->GetLastDamageResult().UnclampedDamage, 22.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDamageEventAvoidedSkipsStatusDeliveryTest, "TDGame.Combat.DamageEvent.AvoidedSkipsStatusDelivery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDamageEventAvoidedSkipsStatusDeliveryTest::RunTest(const FString& Parameters)
{
	using namespace TDDamageEventTests;
	FTDDamageEventTestWorld Fixture;
	UTDCombatComponent* Caster = Fixture.SpawnCombatant(FVector(-300.f, 0.f, 0.f), 0);
	UTDCombatComponent* Target = Fixture.SpawnCombatant(FVector::ZeroVector);
	FTDDamageEventRecorder Recorder(Target);
	UTDStatusDefinition* Burn = NewObject<UTDStatusDefinition>(Fixture.World);
	Burn->Duration = 1.f;
	Burn->PulseInterval = 0.1f;
	Burn->Rules.Add(MakeDamageEventRule(ETDDamageEvent::Spawn, MakeDamageEventAction(5.f, ETDDamageElement::Fire)));
	Burn->Rules.Add(MakeDamageEventRule(ETDDamageEvent::Pulse, MakeDamageEventAction(5.f, ETDDamageElement::Fire)));
	Target->ApplyStatus(Burn, MakeDamageEventContext(Caster));
	if (!TestEqual(TEXT("Status spawn damage applies before invulnerability"), Recorder.Results.Num(), 1))
	{
		return false;
	}

	Target->AddLooseGameplayTag(TDGameplayTags::State_Invulnerable);
	Fixture.Tick(0.25f);
	const FTDDamageResult BlockedStatus = Target->ReceiveDamage(10.f, ETDDamageElement::Fire, false, MakeDamageEventContext(Caster, ETDDamageDelivery::Status));
	TestEqual(TEXT("Invulnerable target takes no status damage"), BlockedStatus.AppliedDamage, 0.f);
	TestEqual(TEXT("Blocked status damage reports no unclamped damage"), BlockedStatus.UnclampedDamage, 0.f);
	TestEqual(TEXT("Status pulses during invulnerability broadcast no damage"), Recorder.Results.Num(), 1);
	TestEqual(TEXT("Status delivery broadcasts no avoided event"), Recorder.AvoidedLocations.Num(), 0);
	TestEqual(TEXT("Invulnerable target keeps its health"), Target->GetCurrentHealth(), 995.f);

	Target->ReceiveDamage(10.f, ETDDamageElement::Physical, false, MakeDamageEventContext(Caster, ETDDamageDelivery::Melee));
	TestEqual(TEXT("Melee delivery still broadcasts the avoided event"), Recorder.AvoidedLocations.Num(), 1);

	Target->RemoveLooseGameplayTag(TDGameplayTags::State_Invulnerable);
	Fixture.Tick(0.25f);
	TestTrue(TEXT("The status keeps pulsing after invulnerability ends"), Recorder.Results.Num() > 1);
	TestTrue(TEXT("Resumed pulses are status delivery"), AreAllDeliveries(Recorder.Results, 0, ETDDamageDelivery::Status));
	TestEqual(TEXT("Resumed pulses broadcast no avoided event"), Recorder.AvoidedLocations.Num(), 1);
	return true;
}

#endif
