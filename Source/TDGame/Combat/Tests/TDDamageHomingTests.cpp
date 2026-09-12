#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/TDCombatComponent.h"
#include "Combat/Damage/TDDamageDefinition.h"
#include "Combat/Damage/TDDamageEntity.h"
#include "Combat/Damage/TDDamageSubsystem.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"

namespace
{
	struct FTDHomingTestWorld
	{
		FTDHomingTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDHomingTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
			Caster = SpawnCombatant(FVector(-1000.f, -1000.f, 0.f), 0);
		}

		~FTDHomingTestWorld()
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
			Sphere->SetSphereRadius(5.f);
			Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
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

		UTDDamageDefinition* MakeProjectile()
		{
			UTDDamageDefinition* Definition = NewObject<UTDDamageDefinition>(World);
			Definition->Mode = ETDDamageEntityMode::Projectile;
			Definition->Lifetime = 5.f;
			Definition->ProjectileSpeed = 20.f;
			Definition->ProjectileRadius = 1.f;
			return Definition;
		}

		ATDDamageEntity* SpawnEntity(UTDDamageDefinition* Definition, const FVector& Location = FVector::ZeroVector)
		{
			FTDDamageContext Context;
			Context.Caster = Caster->GetOwner();
			Context.Stats = Caster->GetStats();
			Context.Direction = FVector::ForwardVector;
			Context.Budget = MakeShared<FTDDamageChainBudget>();
			return World->GetSubsystem<UTDDamageSubsystem>()->SpawnEntity(Definition, Context, Location);
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

		int32 CountEntities(const UTDDamageDefinition* Definition) const
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

		UWorld* World = nullptr;
		UTDCombatComponent* Caster = nullptr;
	};

	FTDDamageAction MakeHomingAction(float Delay, ETDDamageActionType Type = ETDDamageActionType::ApplyHoming)
	{
		FTDDamageAction Action;
		Action.Type = Type;
		Action.DelaySeconds = Delay;
		Action.Homing.SearchRadius = 2000.f;
		Action.Homing.TurnRateDegreesPerSecond = 180.f;
		Action.Homing.RetargetInterval = 0.05f;
		return Action;
	}

	FTDDamageRule MakeHomingRule(ETDDamageEvent Event, const TArray<FTDDamageAction>& Actions)
	{
		FTDDamageRule Rule;
		Rule.Event = Event;
		Rule.Actions = Actions;
		return Rule;
	}

	FTDDamageAction MakeDelayedChild(UTDDamageDefinition* Child, float Delay)
	{
		FTDDamageAction Action;
		Action.Type = ETDDamageActionType::SpawnEntity;
		Action.Entity = Child;
		Action.DelaySeconds = Delay;
		return Action;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDHomingTimelineTest, "TDGame.Combat.Homing.ApplyStopAndReapplyTimeline", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDHomingTimelineTest::RunTest(const FString& Parameters)
{
	FTDHomingTestWorld Fixture;
	UTDCombatComponent* Target = Fixture.SpawnCombatant(FVector(0.f, 500.f, 0.f));
	UTDDamageDefinition* Definition = Fixture.MakeProjectile();
	FTDDamageAction Reapply = MakeHomingAction(3.f);
	Reapply.Homing.TurnRateDegreesPerSecond = 30.f;
	Reapply.Homing.SearchRadius = 1000.f;
	Definition->Rules.Add(MakeHomingRule(ETDDamageEvent::Spawn,
		{ MakeHomingAction(1.f), MakeHomingAction(2.f, ETDDamageActionType::StopHoming), Reapply }));
	ATDDamageEntity* Entity = Fixture.SpawnEntity(Definition);
	if (!TestNotNull(TEXT("Timeline projectile is created"), Entity))
	{
		return false;
	}
	Fixture.Tick(0.95f, 0.05f);
	TestFalse(TEXT("Homing stays disabled before its one-second deadline"), Entity->IsHoming());
	TestTrue(TEXT("Movement is straight before activation"), Entity->GetTravelDirection().Equals(FVector::ForwardVector, 0.001));
	Fixture.Tick(0.1f, 0.01f);
	TestTrue(TEXT("One-second ApplyHoming enables tracking"), Entity->IsHoming());
	TestEqual(TEXT("Homing selects the enemy"), Entity->GetHomingTarget(), Target->GetOwner());
	TestTrue(TEXT("Projectile turns toward the side target"), Entity->GetTravelDirection().Y > 0.05);
	Fixture.Tick(1.f, 0.05f);
	TestFalse(TEXT("Two-second StopHoming disables tracking"), Entity->IsHoming());
	const FVector StoppedDirection = Entity->GetTravelDirection();
	Target->GetOwner()->SetActorLocation(FVector(500.f, -500.f, 0.f));
	Fixture.Tick(0.8f, 0.05f);
	TestTrue(TEXT("Stopping homing preserves its last travel heading"), Entity->GetTravelDirection().Equals(StoppedDirection, 0.001));
	Fixture.Tick(0.2f, 0.01f);
	TestTrue(TEXT("Three-second reapplication enables homing again"), Entity->IsHoming());
	TestFalse(TEXT("Reapplication starts turning toward the moved target"), Entity->GetTravelDirection().Equals(StoppedDirection, 0.01));
	TestEqual(TEXT("First action settings in the data asset remain unchanged"), Definition->Rules[0].Actions[0].Homing.TurnRateDegreesPerSecond, 180.f);
	TestEqual(TEXT("Reapplication keeps its independent data settings"), Definition->Rules[0].Actions[2].Homing.TurnRateDegreesPerSecond, 30.f);
	TestEqual(TEXT("Delayed action is not rewritten into an immediate asset action"), Definition->Rules[0].Actions[0].DelaySeconds, 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDHomingLongFrameTest, "TDGame.Combat.Homing.LongFrameSplitsMotionAtActionDeadlines", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDHomingLongFrameTest::RunTest(const FString& Parameters)
{
	FTDHomingTestWorld Fixture;
	Fixture.SpawnCombatant(FVector(0.f, 1000.f, 0.f));
	UTDDamageDefinition* Definition = Fixture.MakeProjectile();
	Definition->ProjectileSpeed = 100.f;
	Definition->Rules.Add(MakeHomingRule(ETDDamageEvent::Spawn,
		{ MakeHomingAction(0.1f), MakeHomingAction(0.2f, ETDDamageActionType::StopHoming) }));
	ATDDamageEntity* Entity = Fixture.SpawnEntity(Definition);
	if (!TestNotNull(TEXT("Long-frame projectile is created"), Entity))
	{
		return false;
	}
	Fixture.Tick(0.3f, 0.3f);
	TestFalse(TEXT("Both deadlines execute in one frame and leave homing stopped"), Entity->IsHoming());
	const float HeadingDegrees = Entity->GetTravelDirection().Rotation().Yaw;
	TestTrue(FString::Printf(TEXT("Only the 0.1-second homing interval rotates the projectile, yaw=%g"), HeadingDegrees), FMath::IsNearlyEqual(HeadingDegrees, 18.f, 0.2f));
	TestTrue(TEXT("No turning is applied to the initial straight segment"), Entity->GetActorLocation().Y < 6.3);
	TestTrue(TEXT("The projectile travels through the entire frame"), Entity->GetActorLocation().X > 28.5);
	const FVector Direction = Entity->GetTravelDirection();
	Fixture.Tick(0.2f, 0.1f);
	TestTrue(TEXT("Post-stop movement does not resume tracking"), Entity->GetTravelDirection().Equals(Direction, 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDHomingTargetSelectionTest, "TDGame.Combat.Homing.ReapplicationAndEventTargetSelection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDHomingTargetSelectionTest::RunTest(const FString& Parameters)
{
	FTDHomingTestWorld Fixture;
	UTDCombatComponent* Ally = Fixture.SpawnCombatant(FVector(1.f, 0.f, 0.f), 0);
	UTDCombatComponent* First = Fixture.SpawnCombatant(FVector(0.f, 200.f, 0.f));
	UTDCombatComponent* Second = Fixture.SpawnCombatant(FVector(300.f, 0.f, 0.f));
	ATDDamageEntity* Entity = Fixture.SpawnEntity(Fixture.MakeProjectile());
	if (!TestNotNull(TEXT("Selection projectile is created"), Entity))
	{
		return false;
	}
	FTDHomingSettings Settings;
	Settings.SearchRadius = 1000.f;
	Entity->ApplyHoming(Settings);
	TestEqual(TEXT("Nearest enemy selection ignores a closer ally"), Entity->GetHomingTarget(), First->GetOwner());
	Second->GetOwner()->SetActorLocation(FVector(25.f, 0.f, 0.f));
	Settings.bRetargetOnApply = false;
	Settings.TurnRateDegreesPerSecond = 45.f;
	Entity->ApplyHoming(Settings);
	TestEqual(TEXT("Reapplying without retargeting preserves the current valid target"), Entity->GetHomingTarget(), First->GetOwner());
	Settings.bRetargetOnApply = true;
	Entity->ApplyHoming(Settings);
	TestEqual(TEXT("Explicit reselection chooses the now-closer enemy"), Entity->GetHomingTarget(), Second->GetOwner());
	Settings.TargetSelection = ETDHomingTargetSelection::EventTarget;
	Entity->ApplyHoming(Settings, First->GetOwner());
	TestEqual(TEXT("EventTarget selects the supplied enemy rather than the nearest one"), Entity->GetHomingTarget(), First->GetOwner());
	Entity->ApplyHoming(Settings, Ally->GetOwner());
	TestNotEqual(TEXT("EventTarget cannot bypass team filtering"), Entity->GetHomingTarget(), Ally->GetOwner());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDHomingTargetLossTest, "TDGame.Combat.Homing.TargetLossReacquiresOrContinuesStraight", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDHomingTargetLossTest::RunTest(const FString& Parameters)
{
	FTDHomingTestWorld Fixture;
	UTDCombatComponent* First = Fixture.SpawnCombatant(FVector(0.f, 200.f, 0.f));
	UTDCombatComponent* Replacement = Fixture.SpawnCombatant(FVector(300.f, 0.f, 0.f));
	ATDDamageEntity* Entity = Fixture.SpawnEntity(Fixture.MakeProjectile());
	if (!TestNotNull(TEXT("Reacquiring projectile is created"), Entity))
	{
		return false;
	}
	FTDHomingSettings Settings;
	Settings.SearchRadius = 1000.f;
	Settings.RetargetInterval = 0.05f;
	Settings.TargetLossPolicy = ETDHomingTargetLossPolicy::Reacquire;
	Entity->ApplyHoming(Settings);
	TestEqual(TEXT("Initial target is selected"), Entity->GetHomingTarget(), First->GetOwner());
	First->GetOwner()->Destroy();
	Fixture.Tick(0.1f, 0.01f);
	TestEqual(TEXT("Reacquire finds a replacement after target destruction"), Entity->GetHomingTarget(), Replacement->GetOwner());
	Replacement->GetOwner()->Destroy();
	Fixture.Tick(0.1f, 0.01f);
	TestNull(TEXT("No candidates leaves the target empty"), Entity->GetHomingTarget());
	TestFalse(TEXT("An empty target set never creates a non-finite direction"), Entity->GetTravelDirection().ContainsNaN());
	UTDCombatComponent* NewTarget = Fixture.SpawnCombatant(FVector(0.f, 250.f, 0.f));
	Fixture.Tick(0.1f, 0.01f);
	TestEqual(TEXT("Reacquisition resumes when a new enemy enters the world"), Entity->GetHomingTarget(), NewTarget->GetOwner());
	Settings.TargetLossPolicy = ETDHomingTargetLossPolicy::ContinueStraight;
	Entity->ApplyHoming(Settings);
	Fixture.Tick(0.05f, 0.01f);
	const FVector HeadingBeforeLoss = Entity->GetTravelDirection();
	NewTarget->GetOwner()->Destroy();
	Fixture.SpawnCombatant(FVector(0.f, -200.f, 0.f));
	Fixture.Tick(0.15f, 0.01f);
	TestNull(TEXT("ContinueStraight does not select a replacement"), Entity->GetHomingTarget());
	TestTrue(TEXT("ContinueStraight retains the heading at target loss"), Entity->GetTravelDirection().Equals(HeadingBeforeLoss, 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDelayedActionCancellationTest, "TDGame.Combat.Homing.FinishAndDestroyCancelOwnedDelayedActions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDelayedActionCancellationTest::RunTest(const FString& Parameters)
{
	FTDHomingTestWorld Fixture;
	UTDDamageDefinition* Child = NewObject<UTDDamageDefinition>(Fixture.World);
	UTDDamageDefinition* Parent = Fixture.MakeProjectile();
	Parent->Rules.Add(MakeHomingRule(ETDDamageEvent::Spawn, { MakeDelayedChild(Child, 0.2f) }));
	ATDDamageEntity* Finished = Fixture.SpawnEntity(Parent);
	ATDDamageEntity* Destroyed = Fixture.SpawnEntity(Parent, FVector(0.f, 100.f, 0.f));
	if (!TestNotNull(TEXT("Finish test parent exists"), Finished) || !TestNotNull(TEXT("Destroy test parent exists"), Destroyed))
	{
		return false;
	}
	Fixture.Tick(0.1f);
	Finished->Finish();
	Destroyed->Destroy();
	Fixture.Tick(0.3f);
	TestEqual(TEXT("Normal finish and external destroy both cancel delayed child spawning"), Fixture.CountEntities(Child), 0);
	TestEqual(TEXT("Both source entities have ended"), Fixture.CountEntities(Parent), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDelayedActionExpirationTest, "TDGame.Combat.Homing.ExpirationBoundaryExcludesDelayedActions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDelayedActionExpirationTest::RunTest(const FString& Parameters)
{
	FTDHomingTestWorld Fixture;
	UTDDamageDefinition* Child = NewObject<UTDDamageDefinition>(Fixture.World);
	UTDDamageDefinition* Parent = Fixture.MakeProjectile();
	Parent->Lifetime = 0.2f;
	Parent->Rules.Add(MakeHomingRule(ETDDamageEvent::Spawn, { MakeDelayedChild(Child, Parent->Lifetime) }));
	ATDDamageEntity* Entity = Fixture.SpawnEntity(Parent);
	if (!TestNotNull(TEXT("Boundary source exists"), Entity))
	{
		return false;
	}
	Fixture.Tick(0.3f, 0.3f);
	TestEqual(TEXT("An action due exactly at source expiry does not execute"), Fixture.CountEntities(Child), 0);
	TestEqual(TEXT("Source still expires in the crossing frame"), Fixture.CountEntities(Parent), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDSameTimeActionOrderTest, "TDGame.Combat.Homing.EqualDeadlineActionsPreserveAuthoredOrder", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDSameTimeActionOrderTest::RunTest(const FString& Parameters)
{
	FTDHomingTestWorld Fixture;
	Fixture.SpawnCombatant(FVector(0.f, 300.f, 0.f));
	UTDDamageDefinition* ApplyThenStop = Fixture.MakeProjectile();
	ApplyThenStop->Rules.Add(MakeHomingRule(ETDDamageEvent::Spawn,
		{ MakeHomingAction(0.1f), MakeHomingAction(0.1f, ETDDamageActionType::StopHoming) }));
	UTDDamageDefinition* StopThenApply = Fixture.MakeProjectile();
	StopThenApply->Rules.Add(MakeHomingRule(ETDDamageEvent::Spawn,
		{ MakeHomingAction(0.1f, ETDDamageActionType::StopHoming), MakeHomingAction(0.1f) }));
	ATDDamageEntity* Stopped = Fixture.SpawnEntity(ApplyThenStop);
	ATDDamageEntity* Homing = Fixture.SpawnEntity(StopThenApply, FVector(0.f, -100.f, 0.f));
	if (!TestNotNull(TEXT("Apply-then-stop projectile exists"), Stopped) || !TestNotNull(TEXT("Stop-then-apply projectile exists"), Homing))
	{
		return false;
	}
	Fixture.Tick(0.15f, 0.15f);
	TestFalse(TEXT("Later StopHoming wins at an equal deadline"), Stopped->IsHoming());
	TestTrue(TEXT("Later ApplyHoming wins at an equal deadline"), Homing->IsHoming());
	TestTrue(TEXT("Zero-duration homing between equal-deadline actions cannot rotate"), Stopped->GetTravelDirection().Equals(FVector::ForwardVector, 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDSpawnActivateEventTest, "TDGame.Combat.Homing.SpawnAndActivateHaveIndependentTiming", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDSpawnActivateEventTest::RunTest(const FString& Parameters)
{
	FTDHomingTestWorld Fixture;
	UTDDamageDefinition* SpawnChild = NewObject<UTDDamageDefinition>(Fixture.World);
	UTDDamageDefinition* ActivateChild = NewObject<UTDDamageDefinition>(Fixture.World);
	UTDDamageDefinition* Parent = Fixture.MakeProjectile();
	Parent->ActivationDelay = 0.2f;
	Parent->Rules.Add(MakeHomingRule(ETDDamageEvent::Spawn, { MakeDelayedChild(SpawnChild, 0.f) }));
	Parent->Rules.Add(MakeHomingRule(ETDDamageEvent::Activate, { MakeDelayedChild(ActivateChild, 0.f) }));
	ATDDamageEntity* Entity = Fixture.SpawnEntity(Parent);
	if (!TestNotNull(TEXT("Delayed-activation entity is created"), Entity))
	{
		return false;
	}
	TestEqual(TEXT("Spawn fires at physical creation despite activation delay"), Fixture.CountEntities(SpawnChild), 1);
	TestEqual(TEXT("Activate does not fire at physical creation"), Fixture.CountEntities(ActivateChild), 0);
	Fixture.Tick(0.15f, 0.05f);
	TestTrue(TEXT("Inactive projectile has not moved"), Entity->GetActorLocation().IsNearlyZero(0.001));
	TestEqual(TEXT("Activate is still pending before its deadline"), Fixture.CountEntities(ActivateChild), 0);
	Fixture.Tick(0.1f, 0.1f);
	TestEqual(TEXT("Activate fires once when its deadline is crossed"), Fixture.CountEntities(ActivateChild), 1);
	TestEqual(TEXT("Activation does not emit Spawn again"), Fixture.CountEntities(SpawnChild), 1);
	TestTrue(TEXT("Movement starts only after activation within the crossing frame"), FMath::IsNearlyEqual(Entity->GetActorLocation().X, 1.0, 0.05));
	return true;
}

#endif
