#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/Damage/TDDamageTypes.h"
#include "Combat/TDCombatComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TDGameplayMessages.h"
#include "Core/TDGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "Misc/AutomationTest.h"

namespace TDFluidScenario
{
	struct FScenarioWorld
	{
		explicit FScenarioWorld(bool bShouldAttachGameInstance = false)
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidScenarioWorld"));
			if (bShouldAttachGameInstance)
			{
				GameInstance = NewObject<UGameInstance>(GEngine);
				GameInstance->AddToRoot();
				GameInstance->InitializeStandalone(WorldName, GetTransientPackage());
				World = GameInstance->GetWorld();
			}
			else
			{
				World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			}
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FScenarioWorld()
		{
			if (World)
			{
				World->EndPlay(EEndPlayReason::Quit);
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
			if (GameInstance)
			{
				GameInstance->Shutdown();
				GameInstance->RemoveFromRoot();
			}
		}

		void TickSteps(int32 StepCount, float StepSeconds = 0.05f)
		{
			for (int32 Step = 0; Step < StepCount; ++Step)
			{
				++GFrameCounter;
				World->Tick(LEVELTICK_All, StepSeconds);
			}
		}

		int32 TickOnceAndReadStampCount(const ATDFluidLiveActor* LiveActor)
		{
			TickSteps(1);
			return LiveActor->LiveComponent->GetLastProcessedPointCount();
		}

		UWorld* World = nullptr;
		UGameInstance* GameInstance = nullptr;
	};

	AActor* SpawnPlainActor(UWorld* World, const FVector& Location)
	{
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), Location, FRotator::ZeroRotator);
		if (!Actor)
		{
			return nullptr;
		}

		USceneComponent* ActorRoot = NewObject<USceneComponent>(Actor);
		Actor->AddInstanceComponent(ActorRoot);
		Actor->SetRootComponent(ActorRoot);
		ActorRoot->RegisterComponent();
		Actor->SetActorLocation(Location);
		return Actor;
	}

	AActor* SpawnOverlapProbe(UWorld* World, const FVector& Location)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		if (!Actor)
		{
			return nullptr;
		}

		USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
		Actor->AddInstanceComponent(Sphere);
		Actor->SetRootComponent(Sphere);
		Sphere->SetSphereRadius(30.0f);
		Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Sphere->SetCollisionObjectType(ECC_Pawn);
		Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
		Sphere->RegisterComponent();
		Actor->SetActorLocation(Location);
		return Actor;
	}

	UTDCombatComponent* AddCombatant(AActor* Actor)
	{
		UTDCombatComponent* Combatant = NewObject<UTDCombatComponent>(Actor);
		Actor->AddInstanceComponent(Combatant);
		FTDCombatStats Stats;
		Stats.BaseMaxHealth = 1000.0f;
		Combatant->SetStats(Stats);
		Combatant->RegisterComponent();
		return Combatant;
	}

	AActor* SpawnStaticBlock(UWorld* World, UStaticMesh* Mesh, const FVector& Center, const FRotator& Rotation, const FVector& Scale)
	{
		AActor* Block = World->SpawnActor<AActor>();
		if (!Block)
		{
			return nullptr;
		}

		UStaticMeshComponent* BlockMesh = NewObject<UStaticMeshComponent>(Block);
		Block->AddInstanceComponent(BlockMesh);
		Block->SetRootComponent(BlockMesh);
		BlockMesh->SetMobility(EComponentMobility::Static);
		BlockMesh->SetStaticMesh(Mesh);
		BlockMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		BlockMesh->SetCollisionObjectType(ECC_WorldStatic);
		BlockMesh->SetCollisionResponseToAllChannels(ECR_Block);
		BlockMesh->SetWorldTransform(FTransform(Rotation, Center, Scale));
		BlockMesh->RegisterComponent();
		return Block;
	}

	AActor* SpawnRamp(UWorld* World, UStaticMesh* CubeMesh, float PitchDegrees)
	{
		return SpawnStaticBlock(World, CubeMesh, FVector(0.0f, 0.0f, -100.0f), FRotator(PitchDegrees, 0.0f, 0.0f), FVector(80.0f, 80.0f, 1.0f));
	}

	ATDFluidLiveActor* SpawnPresetLiveActor(FScenarioWorld& TestWorld, ETDFluidPresetKind Kind, const FVector& Location)
	{
		UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
		if (!Subsystem)
		{
			return nullptr;
		}

		UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, Kind);
		return Subsystem->SpawnPooledFluidActor(Preset, Location, 120.0f);
	}

	FTDFluidCellSample SampleAt(const ATDFluidLiveActor* LiveActor, const FVector& WorldLocation)
	{
		FTDFluidCellSample Sample;
		LiveActor->LiveComponent->SampleFluidAtWorldLocation(WorldLocation, Sample);
		return Sample;
	}

	float MeasureMaxSpeed(const ATDFluidLiveActor* LiveActor)
	{
		const FTDFluidGrid2DSolver& Solver = LiveActor->LiveComponent->GetSolver();
		const int32 Resolution = Solver.GetResolution();
		float MaxSpeed = 0.0f;
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				MaxSpeed = FMath::Max(MaxSpeed, static_cast<float>(Solver.SampleAtCell(CellX, CellY).VelocityCmPerSec.Size()));
			}
		}
		return MaxSpeed;
	}

	float MeasureMaxTerrainSlope(const ATDFluidLiveActor* LiveActor)
	{
		const FTDFluidGrid2DSolver& Solver = LiveActor->LiveComponent->GetSolver();
		const int32 Resolution = Solver.GetResolution();
		float MaxSlope = 0.0f;
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				MaxSlope = FMath::Max(MaxSlope, static_cast<float>(Solver.SampleAtCell(CellX, CellY).TerrainSlope.Size()));
			}
		}
		return MaxSlope;
	}

	float SumPrimaryDensity(const FTDFluidGrid2DSolver& Solver)
	{
		const int32 Resolution = Solver.GetResolution();
		float Sum = 0.0f;
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				Sum += Solver.SampleAtCell(CellX, CellY).PrimaryDensity;
			}
		}
		return Sum;
	}

	void ExpectStampAt(FAutomationTestBase& Test, const ATDFluidLiveActor* LiveActor, const TCHAR* Label, const FVector& StampLocation, const FVector& AbsentLocation)
	{
		const float AtStamp = SampleAt(LiveActor, StampLocation).PrimaryDensity;
		const float AtAbsent = SampleAt(LiveActor, AbsentLocation).PrimaryDensity;
		Test.TestTrue(*FString::Printf(TEXT("%s: density lands at the expected location (%.3f)"), Label, AtStamp), AtStamp > 0.4f);
		Test.TestTrue(*FString::Printf(TEXT("%s: no density at the other candidate location (%.3f)"), Label, AtAbsent), AtAbsent < 0.05f);
	}

	constexpr float RampPitchDegrees = 10.0f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidHitLocationStampTest,
	"TDGame.FluidFX.CombatQueue.HitLocationDecidesStampPositionAndMergeKeepsLocation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidHitLocationStampTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidScenario;

	FScenarioWorld TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	ATDFluidLiveActor* LiveActor = SpawnPresetLiveActor(TestWorld, ETDFluidPresetKind::Blood, FVector::ZeroVector);
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem) || !TestNotNull(TEXT("Spawned Blood LiveActor"), LiveActor))
	{
		return false;
	}

	const FVector SingleTarget(-400.0f, 0.0f, 0.0f);
	const FVector SingleHit(-100.0f, 0.0f, 0.0f);
	const FVector StrongUnlocatedTarget(-400.0f, 600.0f, 0.0f);
	const FVector StrongUnlocatedHit(-100.0f, 600.0f, 0.0f);
	const FVector WeakUnlocatedTarget(-400.0f, -600.0f, 0.0f);
	const FVector WeakUnlocatedHit(-100.0f, -600.0f, 0.0f);
	const FVector WeakFirstTarget(-400.0f, 1000.0f, 0.0f);
	const FVector WeakFirstWeakHit(-800.0f, 1000.0f, 0.0f);
	const FVector WeakFirstStrongHit(300.0f, 1000.0f, 0.0f);
	const FVector StrongFirstTarget(-400.0f, -1000.0f, 0.0f);
	const FVector StrongFirstStrongHit(300.0f, -1000.0f, 0.0f);
	const FVector StrongFirstWeakHit(-800.0f, -1000.0f, 0.0f);

	AActor* SingleActor = SpawnPlainActor(TestWorld.World, SingleTarget);
	AActor* StrongUnlocatedActor = SpawnPlainActor(TestWorld.World, StrongUnlocatedTarget);
	AActor* WeakUnlocatedActor = SpawnPlainActor(TestWorld.World, WeakUnlocatedTarget);
	AActor* WeakFirstActor = SpawnPlainActor(TestWorld.World, WeakFirstTarget);
	AActor* StrongFirstActor = SpawnPlainActor(TestWorld.World, StrongFirstTarget);
	if (!SingleActor || !StrongUnlocatedActor || !WeakUnlocatedActor || !WeakFirstActor || !StrongFirstActor)
	{
		AddError(TEXT("Failed to spawn the hit location target actors"));
		return false;
	}

	Subsystem->QueueCombatDamage(nullptr, SingleActor, 40.0f, ETDDamageElement::Fire, true, false, ETDDamageDelivery::Direct, true, SingleHit);

	Subsystem->QueueCombatDamage(nullptr, StrongUnlocatedActor, 50.0f, ETDDamageElement::Physical, false, false);
	Subsystem->QueueCombatDamage(nullptr, StrongUnlocatedActor, 20.0f, ETDDamageElement::Frost, true, false, ETDDamageDelivery::Direct, true, StrongUnlocatedHit);

	Subsystem->QueueCombatDamage(nullptr, WeakUnlocatedActor, 20.0f, ETDDamageElement::Frost, true, false, ETDDamageDelivery::Direct, true, WeakUnlocatedHit);
	Subsystem->QueueCombatDamage(nullptr, WeakUnlocatedActor, 50.0f, ETDDamageElement::Physical, false, false);

	Subsystem->QueueCombatDamage(nullptr, WeakFirstActor, 20.0f, ETDDamageElement::Physical, false, false, ETDDamageDelivery::Direct, true, WeakFirstWeakHit);
	Subsystem->QueueCombatDamage(nullptr, WeakFirstActor, 60.0f, ETDDamageElement::Physical, false, false, ETDDamageDelivery::Direct, true, WeakFirstStrongHit);

	Subsystem->QueueCombatDamage(nullptr, StrongFirstActor, 60.0f, ETDDamageElement::Physical, false, false, ETDDamageDelivery::Direct, true, StrongFirstStrongHit);
	Subsystem->QueueCombatDamage(nullptr, StrongFirstActor, 20.0f, ETDDamageElement::Physical, false, false, ETDDamageDelivery::Direct, true, StrongFirstWeakHit);

	TestEqual(TEXT("Nine queued hits on five targets become five stamps"), TestWorld.TickOnceAndReadStampCount(LiveActor), 5);

	ExpectStampAt(*this, LiveActor, TEXT("A single hit with a hit location stamps there instead of at the target"), SingleHit, SingleTarget);
	ExpectStampAt(*this, LiveActor, TEXT("A located weak hit merged after an unlocated strong hit still sets the location"), StrongUnlocatedHit, StrongUnlocatedTarget);
	ExpectStampAt(*this, LiveActor, TEXT("A located weak hit merged before an unlocated strong hit keeps its location"), WeakUnlocatedHit, WeakUnlocatedTarget);
	ExpectStampAt(*this, LiveActor, TEXT("When both hits are located the stronger hit later in the queue wins the location"), WeakFirstStrongHit, WeakFirstWeakHit);
	ExpectStampAt(*this, LiveActor, TEXT("When both hits are located the stronger hit earlier in the queue keeps the location"), StrongFirstStrongHit, StrongFirstWeakHit);

	const FTDFluidCellSample SingleHitSample = SampleAt(LiveActor, SingleHit);
	TestTrue(*FString::Printf(TEXT("The known fire element reaches the stamp at the hit location (selective %.3f)"), SingleHitSample.SelectiveDensity), SingleHitSample.SelectiveDensity > 0.3f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidStatusDeliveryFilterTest,
	"TDGame.FluidFX.CombatQueue.StatusDeliveryNeverStampsAndOtherDeliveriesDo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidStatusDeliveryFilterTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidScenario;

	FScenarioWorld TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	ATDFluidLiveActor* LiveActor = SpawnPresetLiveActor(TestWorld, ETDFluidPresetKind::Blood, FVector::ZeroVector);
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem) || !TestNotNull(TEXT("Spawned Blood LiveActor"), LiveActor))
	{
		return false;
	}

	AActor* StatusOnlyTarget = SpawnPlainActor(TestWorld.World, FVector(-400.0f, 0.0f, 0.0f));
	AActor* StatusFirstTarget = SpawnPlainActor(TestWorld.World, FVector(-400.0f, 500.0f, 0.0f));
	AActor* MeleeFirstTarget = SpawnPlainActor(TestWorld.World, FVector(-400.0f, -500.0f, 0.0f));
	AActor* SpellTarget = SpawnPlainActor(TestWorld.World, FVector(400.0f, 0.0f, 0.0f));
	AActor* DirectTarget = SpawnPlainActor(TestWorld.World, FVector(400.0f, 600.0f, 0.0f));
	if (!StatusOnlyTarget || !StatusFirstTarget || !MeleeFirstTarget || !SpellTarget || !DirectTarget)
	{
		AddError(TEXT("Failed to spawn the delivery target actors"));
		return false;
	}

	Subsystem->QueueCombatDamage(nullptr, StatusOnlyTarget, 80.0f, ETDDamageElement::Fire, true, false, ETDDamageDelivery::Status);
	TestEqual(TEXT("A status-only hit queues nothing"), TestWorld.TickOnceAndReadStampCount(LiveActor), 0);
	TestNearlyEqual(TEXT("A status-only hit leaves the whole grid empty"), SumPrimaryDensity(LiveActor->LiveComponent->GetSolver()), 0.0f, 1e-4f);

	Subsystem->QueueCombatDamage(nullptr, StatusFirstTarget, 80.0f, ETDDamageElement::Frost, true, false, ETDDamageDelivery::Status);
	Subsystem->QueueCombatDamage(nullptr, StatusFirstTarget, 30.0f, ETDDamageElement::Physical, false, false, ETDDamageDelivery::Melee);
	Subsystem->QueueCombatDamage(nullptr, MeleeFirstTarget, 30.0f, ETDDamageElement::Physical, false, false, ETDDamageDelivery::Melee);
	Subsystem->QueueCombatDamage(nullptr, MeleeFirstTarget, 80.0f, ETDDamageElement::Frost, true, false, ETDDamageDelivery::Status);
	Subsystem->QueueCombatDamage(nullptr, SpellTarget, 30.0f, ETDDamageElement::Fire, true, false, ETDDamageDelivery::Spell);
	Subsystem->QueueCombatDamage(nullptr, DirectTarget, 30.0f, ETDDamageElement::Fire, true, false, ETDDamageDelivery::Direct);

	TestEqual(TEXT("Status plus melee on one target is one stamp, so four targets stamp four times"), TestWorld.TickOnceAndReadStampCount(LiveActor), 4);

	const float StatusFirstSelective = SampleAt(LiveActor, FVector(-400.0f, 500.0f, 0.0f)).SelectiveDensity;
	const float MeleeFirstSelective = SampleAt(LiveActor, FVector(-400.0f, -500.0f, 0.0f)).SelectiveDensity;
	TestTrue(*FString::Printf(TEXT("A status hit queued before melee does not turn the melee stamp into the frost water channel (selective %.3f)"), StatusFirstSelective), StatusFirstSelective > 0.3f);
	TestTrue(*FString::Printf(TEXT("A status hit queued after melee does not turn the melee stamp into the frost water channel (selective %.3f)"), MeleeFirstSelective), MeleeFirstSelective > 0.3f);
	TestTrue(TEXT("Spell delivery still stamps"), SampleAt(LiveActor, FVector(400.0f, 0.0f, 0.0f)).SelectiveDensity > 0.3f);
	TestTrue(TEXT("Direct delivery still stamps"), SampleAt(LiveActor, FVector(400.0f, 600.0f, 0.0f)).SelectiveDensity > 0.3f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidHitDirectionStampTest,
	"TDGame.FluidFX.CombatQueue.HitDirectionSteersStampVelocity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidHitDirectionStampTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidScenario;

	FScenarioWorld TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	ATDFluidLiveActor* LiveActor = SpawnPresetLiveActor(TestWorld, ETDFluidPresetKind::Blood, FVector::ZeroVector);
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem) || !TestNotNull(TEXT("Spawned Blood LiveActor"), LiveActor))
	{
		return false;
	}

	const FVector PositiveYLocation(-600.0f, 0.0f, 0.0f);
	const FVector NegativeYLocation(600.0f, 0.0f, 0.0f);
	const FVector TiltedLocation(0.0f, 800.0f, 0.0f);
	AActor* PositiveYTarget = SpawnPlainActor(TestWorld.World, PositiveYLocation);
	AActor* NegativeYTarget = SpawnPlainActor(TestWorld.World, NegativeYLocation);
	AActor* TiltedTarget = SpawnPlainActor(TestWorld.World, TiltedLocation);
	if (!PositiveYTarget || !NegativeYTarget || !TiltedTarget)
	{
		AddError(TEXT("Failed to spawn the direction target actors"));
		return false;
	}

	Subsystem->QueueCombatDamage(nullptr, PositiveYTarget, 60.0f, ETDDamageElement::Physical, false, false, ETDDamageDelivery::Direct, true, PositiveYLocation, FVector(0.0f, 1.0f, 0.0f));
	Subsystem->QueueCombatDamage(nullptr, NegativeYTarget, 60.0f, ETDDamageElement::Physical, false, false, ETDDamageDelivery::Direct, true, NegativeYLocation, FVector(0.0f, -1.0f, 0.0f));
	Subsystem->QueueCombatDamage(nullptr, TiltedTarget, 60.0f, ETDDamageElement::Physical, false, false, ETDDamageDelivery::Direct, true, TiltedLocation, FVector(0.0f, 4.0f, 9.0f));

	TestEqual(TEXT("Three targets stamp three times"), TestWorld.TickOnceAndReadStampCount(LiveActor), 3);

	const FVector2D PositiveYVelocity = SampleAt(LiveActor, PositiveYLocation).Velocity;
	const FVector2D NegativeYVelocity = SampleAt(LiveActor, NegativeYLocation).Velocity;
	const FVector2D TiltedVelocity = SampleAt(LiveActor, TiltedLocation).Velocity;
	AddInfo(FString::Printf(TEXT("Velocity +Y (%.1f, %.1f) -Y (%.1f, %.1f) tilted (%.1f, %.1f)"), PositiveYVelocity.X, PositiveYVelocity.Y, NegativeYVelocity.X, NegativeYVelocity.Y, TiltedVelocity.X, TiltedVelocity.Y));

	TestTrue(*FString::Printf(TEXT("A +Y hit direction pushes the fluid toward +Y (%.1f)"), PositiveYVelocity.Y), PositiveYVelocity.Y > 50.0f);
	TestTrue(*FString::Printf(TEXT("A +Y hit direction leaves almost no X velocity (%.1f)"), PositiveYVelocity.X), FMath::Abs(PositiveYVelocity.X) < 0.3f * PositiveYVelocity.Y);
	TestTrue(*FString::Printf(TEXT("A -Y hit direction pushes the fluid toward -Y (%.1f)"), NegativeYVelocity.Y), NegativeYVelocity.Y < -50.0f);
	TestTrue(*FString::Printf(TEXT("A -Y hit direction leaves almost no X velocity (%.1f)"), NegativeYVelocity.X), FMath::Abs(NegativeYVelocity.X) < 0.3f * FMath::Abs(NegativeYVelocity.Y));
	TestTrue(*FString::Printf(TEXT("A tilted unnormalized direction is flattened to unit +Y, matching the plain +Y push (%.1f vs %.1f)"), TiltedVelocity.Y, PositiveYVelocity.Y), FMath::Abs(TiltedVelocity.Y - PositiveYVelocity.Y) < 0.25f * PositiveYVelocity.Y);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidCombatDelegateHitInfoTest,
	"TDGame.FluidFX.CombatBinding.DelegateCarriesElementLocationDirectionAndDelivery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidCombatDelegateHitInfoTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidScenario;

	FScenarioWorld TestWorld;
	ATDFluidLiveActor* LiveActor = SpawnPresetLiveActor(TestWorld, ETDFluidPresetKind::Blood, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Spawned Blood LiveActor"), LiveActor))
	{
		return false;
	}

	const FVector TargetLocation(-400.0f, 0.0f, 0.0f);
	const FVector HitLocation(-100.0f, 0.0f, 0.0f);
	AActor* TargetActor = SpawnOverlapProbe(TestWorld.World, TargetLocation);
	AActor* Attacker = SpawnPlainActor(TestWorld.World, FVector(-1000.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Spawned target"), TargetActor) || !TestNotNull(TEXT("Spawned attacker"), Attacker))
	{
		return false;
	}

	UTDCombatComponent* Combatant = AddCombatant(TargetActor);
	LiveActor->RegisterInteractingActor(TargetActor);
	if (!TestEqual(TEXT("The combatant has one binding entry"), LiveActor->GetBoundCombatComponentCount(), 1))
	{
		return false;
	}

	FTDDamageContext DirectContext;
	DirectContext.Caster = Attacker;
	DirectContext.CastTarget = TargetLocation;
	DirectContext.Direction = FVector(1.0f, 0.0f, 0.0f);
	DirectContext.Budget = MakeShared<FTDDamageChainBudget>();

	FTDDamageHitInfo HitInfo;
	HitInfo.Location = HitLocation;
	HitInfo.Direction = FVector(0.0f, 1.0f, 0.0f);
	HitInfo.bHasLocation = true;

	const FTDDamageResult LocatedResult = Combatant->ReceiveDamage(60.0f, ETDDamageElement::Frost, false, DirectContext, HitInfo);
	TestTrue(TEXT("The combatant took the located frost hit"), LocatedResult.AppliedDamage > 0.0f);
	TestTrue(TEXT("The damage result carries the frost element"), LocatedResult.Element == ETDDamageElement::Frost);
	TestTrue(TEXT("The damage result carries the hit location"), LocatedResult.bHasHitLocation && LocatedResult.HitLocation.Equals(HitLocation, 0.01));

	TestEqual(TEXT("One located hit becomes exactly one stamp"), TestWorld.TickOnceAndReadStampCount(LiveActor), 1);
	const FTDFluidCellSample LocatedHitSample = SampleAt(LiveActor, HitLocation);
	TestTrue(*FString::Printf(TEXT("The stamp lands at the hit location (%.3f)"), LocatedHitSample.PrimaryDensity), LocatedHitSample.PrimaryDensity > 0.4f);
	TestTrue(*FString::Printf(TEXT("The target pivot location receives no stamp (%.3f)"), SampleAt(LiveActor, TargetLocation).PrimaryDensity), SampleAt(LiveActor, TargetLocation).PrimaryDensity < 0.05f);
	TestNearlyEqual(TEXT("The frost element selects the water channel, so selective density stays zero"), LocatedHitSample.SelectiveDensity, 0.0f, 1e-3f);
	TestTrue(*FString::Printf(TEXT("The authored +Y hit direction beats the attacker-to-target direction (%.1f)"), LocatedHitSample.Velocity.Y), LocatedHitSample.Velocity.Y > 50.0f);

	FTDDamageContext StatusContext = DirectContext;
	StatusContext.Delivery = ETDDamageDelivery::Status;
	const FTDDamageResult StatusResult = Combatant->ReceiveDamage(60.0f, ETDDamageElement::Fire, false, StatusContext, HitInfo);
	TestTrue(TEXT("The combatant took the status hit"), StatusResult.AppliedDamage > 0.0f);
	TestTrue(TEXT("The damage result carries the status delivery"), StatusResult.Delivery == ETDDamageDelivery::Status);
	TestEqual(TEXT("A status delivery hit through the delegate stamps nothing"), TestWorld.TickOnceAndReadStampCount(LiveActor), 0);

	const FTDDamageResult PlainResult = Combatant->ReceiveDamage(60.0f, ETDDamageElement::Fire, false, DirectContext);
	TestTrue(TEXT("The combatant took the plain fire hit"), PlainResult.AppliedDamage > 0.0f);
	TestFalse(TEXT("A hit without hit info reports no hit location"), PlainResult.bHasHitLocation);
	TestEqual(TEXT("A hit without hit info becomes one stamp"), TestWorld.TickOnceAndReadStampCount(LiveActor), 1);
	const FTDFluidCellSample PlainSample = SampleAt(LiveActor, TargetLocation);
	TestTrue(*FString::Printf(TEXT("A hit without hit info stamps fire selective density at the target (%.3f)"), PlainSample.SelectiveDensity), PlainSample.SelectiveDensity > 0.3f);
	TestTrue(*FString::Printf(TEXT("A hit without hit info pushes along attacker to target (%.1f)"), PlainSample.Velocity.X), PlainSample.Velocity.X > 50.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidCombatMessageHitInfoTest,
	"TDGame.FluidFX.CombatQueue.MessageCarriesLocationDirectionElementAndDelivery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidCombatMessageHitInfoTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidScenario;

	FScenarioWorld TestWorld(true);
	if (!TestTrue(TEXT("Test world owns a GameInstance with a message subsystem"), UGameplayMessageSubsystem::HasInstance(TestWorld.World)))
	{
		return false;
	}

	ATDFluidLiveActor* LiveActor = SpawnPresetLiveActor(TestWorld, ETDFluidPresetKind::Blood, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Spawned Blood LiveActor"), LiveActor))
	{
		return false;
	}

	const FVector LocatedTargetLocation(-400.0f, 0.0f, 0.0f);
	const FVector LocatedHitLocation(-100.0f, 0.0f, 0.0f);
	const FVector StatusTargetLocation(400.0f, 0.0f, 0.0f);
	const FVector StatusHitLocation(700.0f, 0.0f, 0.0f);
	AActor* Attacker = SpawnPlainActor(TestWorld.World, FVector(-1000.0f, 0.0f, 0.0f));
	AActor* LocatedTarget = SpawnPlainActor(TestWorld.World, LocatedTargetLocation);
	AActor* StatusTarget = SpawnPlainActor(TestWorld.World, StatusTargetLocation);
	if (!Attacker || !LocatedTarget || !StatusTarget)
	{
		AddError(TEXT("Failed to spawn the message test actors"));
		return false;
	}

	UGameplayMessageSubsystem& MessageRouter = UGameplayMessageSubsystem::Get(TestWorld.World);

	FTDDamageAppliedMessage LocatedMessage;
	LocatedMessage.Instigator = Attacker;
	LocatedMessage.Target = LocatedTarget;
	LocatedMessage.DamageAmount = 60.0f;
	LocatedMessage.Element = ETDDamageElement::Frost;
	LocatedMessage.bHasHitLocation = true;
	LocatedMessage.HitLocation = LocatedHitLocation;
	LocatedMessage.HitDirection = FVector(0.0f, -1.0f, 0.0f);
	MessageRouter.BroadcastMessage(TDGameplayTags::Event_Damage_Applied, LocatedMessage);

	FTDDamageAppliedMessage StatusMessage;
	StatusMessage.Instigator = Attacker;
	StatusMessage.Target = StatusTarget;
	StatusMessage.DamageAmount = 60.0f;
	StatusMessage.Element = ETDDamageElement::Fire;
	StatusMessage.Delivery = ETDDamageDelivery::Status;
	StatusMessage.bHasHitLocation = true;
	StatusMessage.HitLocation = StatusHitLocation;
	MessageRouter.BroadcastMessage(TDGameplayTags::Event_Damage_Applied, StatusMessage);

	TestEqual(TEXT("A located message and a status message become exactly one stamp"), TestWorld.TickOnceAndReadStampCount(LiveActor), 1);

	const FTDFluidCellSample LocatedHitSample = SampleAt(LiveActor, LocatedHitLocation);
	TestTrue(*FString::Printf(TEXT("The message stamp lands at the message hit location (%.3f)"), LocatedHitSample.PrimaryDensity), LocatedHitSample.PrimaryDensity > 0.4f);
	TestTrue(*FString::Printf(TEXT("The message target pivot location receives no stamp (%.3f)"), SampleAt(LiveActor, LocatedTargetLocation).PrimaryDensity), SampleAt(LiveActor, LocatedTargetLocation).PrimaryDensity < 0.05f);
	TestNearlyEqual(TEXT("The message frost element selects the water channel, so selective density stays zero"), LocatedHitSample.SelectiveDensity, 0.0f, 1e-3f);
	TestTrue(*FString::Printf(TEXT("The message hit direction -Y pushes the fluid toward -Y (%.1f)"), LocatedHitSample.Velocity.Y), LocatedHitSample.Velocity.Y < -50.0f);
	TestNearlyEqual(TEXT("The status message leaves its hit location empty"), SampleAt(LiveActor, StatusHitLocation).PrimaryDensity, 0.0f, 1e-4f);
	TestNearlyEqual(TEXT("The status message leaves its target location empty"), SampleAt(LiveActor, StatusTargetLocation).PrimaryDensity, 0.0f, 1e-4f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSolverGenerationTest,
	"TDGame.FluidFX.Solver.ReinitializationGenerationAdvancesOnResetAndResolutionChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSolverGenerationTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidScenario;

	FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Mud);
	Config.GridResolution = 32;
	Config.SimExtentCm = FVector2D(1600.0f, 1600.0f);

	FTDFluidBrushStamp Mark;
	Mark.WorldLocation = FVector::ZeroVector;
	Mark.RadiusCm = 150.0f;
	Mark.DensityStrength = 1.0f;

	FTDFluidGrid2DSolver Solver;
	const int32 FreshGeneration = Solver.GetReinitializationGeneration();
	Solver.ResetSimulation();
	TestEqual(TEXT("Resetting a solver that was never initialized changes nothing"), Solver.GetReinitializationGeneration(), FreshGeneration);

	Solver.Initialize(Config, FVector2D::ZeroVector);
	const int32 AfterInitialize = Solver.GetReinitializationGeneration();
	TestTrue(TEXT("Initialize advances the generation"), AfterInitialize > FreshGeneration);

	Solver.ApplyBrushStamps({ Mark }, 0.05f);
	TestTrue(TEXT("The mark writes density before the reset"), SumPrimaryDensity(Solver) > 1.0f);
	Solver.ResetSimulation();
	const int32 AfterReset = Solver.GetReinitializationGeneration();
	TestTrue(TEXT("ResetSimulation advances the generation"), AfterReset > AfterInitialize);
	TestNearlyEqual(TEXT("ResetSimulation clears the density"), SumPrimaryDensity(Solver), 0.0f, 1e-4f);

	Solver.Initialize(Config, FVector2D::ZeroVector);
	const int32 AfterSecondInitialize = Solver.GetReinitializationGeneration();
	TestTrue(TEXT("A second Initialize advances the generation again"), AfterSecondInitialize > AfterReset);

	Solver.ApplyBrushStamps({ Mark }, 0.05f);
	Solver.UpdateConfig(Config);
	TestEqual(TEXT("UpdateConfig with the same configuration keeps the generation"), Solver.GetReinitializationGeneration(), AfterSecondInitialize);
	TestTrue(TEXT("UpdateConfig with the same configuration keeps the density"), SumPrimaryDensity(Solver) > 1.0f);

	FTDFluidSimulationConfig DampedConfig = Config;
	DampedConfig.VelocityDamping = Config.VelocityDamping + 0.5f;
	Solver.UpdateConfig(DampedConfig);
	TestEqual(TEXT("UpdateConfig without a resolution change keeps the generation"), Solver.GetReinitializationGeneration(), AfterSecondInitialize);
	TestNearlyEqual(TEXT("UpdateConfig without a resolution change still applies the new damping"), Solver.GetConfig().VelocityDamping, DampedConfig.VelocityDamping, 1e-4f);

	FTDFluidSimulationConfig ResizedConfig = Config;
	ResizedConfig.GridResolution = 48;
	Solver.UpdateConfig(ResizedConfig);
	TestEqual(TEXT("UpdateConfig with a new resolution resizes the grid"), Solver.GetResolution(), 48);
	TestTrue(TEXT("UpdateConfig with a new resolution advances the generation"), Solver.GetReinitializationGeneration() > AfterSecondInitialize);
	TestNearlyEqual(TEXT("UpdateConfig with a new resolution clears the density"), SumPrimaryDensity(Solver), 0.0f, 1e-4f);

	FTDFluidGrid2DSolver LateSolver;
	LateSolver.UpdateConfig(Config);
	TestTrue(TEXT("UpdateConfig on a solver that was never initialized initializes it"), LateSolver.IsInitialized());
	TestTrue(TEXT("UpdateConfig on a solver that was never initialized advances the generation"), LateSolver.GetReinitializationGeneration() > FreshGeneration);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidTerrainResampleAfterResetTest,
	"TDGame.FluidFX.LiveActor.TerrainResamplesAfterSimulationReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidTerrainResampleAfterResetTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidScenario;

	FScenarioWorld TestWorld;
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Engine cube mesh loads"), CubeMesh))
	{
		return false;
	}

	AActor* Ramp = SpawnRamp(TestWorld.World, CubeMesh, RampPitchDegrees);
	if (!TestNotNull(TEXT("Spawned ramp"), Ramp))
	{
		return false;
	}
	TestWorld.TickSteps(2);

	ATDFluidLiveActor* TerrainFluid = SpawnPresetLiveActor(TestWorld, ETDFluidPresetKind::Water, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Spawned terrain fluid actor"), TerrainFluid))
	{
		return false;
	}
	TerrainFluid->bSampleTerrainElevation = true;
	TestWorld.TickSteps(12);

	const float ExpectedSlope = FMath::Tan(FMath::DegreesToRadians(RampPitchDegrees));
	const float InitialSlope = SampleAt(TerrainFluid, FVector::ZeroVector).TerrainSlope.X;
	if (!TestTrue(*FString::Printf(TEXT("The first pass samples the ramp slope (%.3f vs %.3f)"), InitialSlope, ExpectedSlope), FMath::Abs(InitialSlope - ExpectedSlope) < 0.02f))
	{
		return false;
	}

	TerrainFluid->LiveComponent->ResetSimulationState();
	TestNearlyEqual(TEXT("ResetSimulationState wipes the slope"), MeasureMaxTerrainSlope(TerrainFluid), 0.0f, 1e-4f);
	TestWorld.TickSteps(12);
	const float SlopeAfterReset = SampleAt(TerrainFluid, FVector::ZeroVector).TerrainSlope.X;
	const float CornerSlopeAfterReset = SampleAt(TerrainFluid, FVector(-1400.0f, 1200.0f, 0.0f)).TerrainSlope.X;
	TestTrue(*FString::Printf(TEXT("The slope at the center is sampled again after ResetSimulationState (%.3f vs %.3f)"), SlopeAfterReset, ExpectedSlope), FMath::Abs(SlopeAfterReset - ExpectedSlope) < 0.02f);
	TestTrue(*FString::Printf(TEXT("The slope away from the center is sampled again after ResetSimulationState (%.3f)"), CornerSlopeAfterReset), FMath::Abs(CornerSlopeAfterReset - ExpectedSlope) < 0.02f);

	UTDFluidPresetAsset* WaterPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Water);
	TerrainFluid->LiveComponent->InitSystemStateFromPreset(WaterPreset);
	TestNearlyEqual(TEXT("InitSystemStateFromPreset wipes the slope"), MeasureMaxTerrainSlope(TerrainFluid), 0.0f, 1e-4f);
	TestWorld.TickSteps(12);
	const float SlopeAfterPreset = SampleAt(TerrainFluid, FVector::ZeroVector).TerrainSlope.X;
	TestTrue(*FString::Printf(TEXT("The slope is sampled again after InitSystemStateFromPreset (%.3f vs %.3f)"), SlopeAfterPreset, ExpectedSlope), FMath::Abs(SlopeAfterPreset - ExpectedSlope) < 0.02f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidTerrainResampleOnWakeTest,
	"TDGame.FluidFX.LiveActor.TerrainResamplesWhenWakingAfterGroundChanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidTerrainResampleOnWakeTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidScenario;

	FScenarioWorld TestWorld;
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Engine cube mesh loads"), CubeMesh))
	{
		return false;
	}

	AActor* UphillRamp = SpawnRamp(TestWorld.World, CubeMesh, RampPitchDegrees);
	if (!TestNotNull(TEXT("Spawned uphill ramp"), UphillRamp))
	{
		return false;
	}
	TestWorld.TickSteps(2);

	ATDFluidLiveActor* TerrainFluid = SpawnPresetLiveActor(TestWorld, ETDFluidPresetKind::Water, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Spawned terrain fluid actor"), TerrainFluid))
	{
		return false;
	}
	TerrainFluid->bSampleTerrainElevation = true;
	TestWorld.TickSteps(12);

	const float ExpectedSlope = FMath::Tan(FMath::DegreesToRadians(RampPitchDegrees));
	const float InitialSlope = SampleAt(TerrainFluid, FVector::ZeroVector).TerrainSlope.X;
	if (!TestTrue(*FString::Printf(TEXT("The first pass samples the +X ramp slope (%.3f vs %.3f)"), InitialSlope, ExpectedSlope), FMath::Abs(InitialSlope - ExpectedSlope) < 0.02f))
	{
		return false;
	}

	TerrainFluid->LiveComponent->SetSimulationAwake(false);
	TestWorld.TickSteps(2);
	UphillRamp->Destroy();
	AActor* DownhillRamp = SpawnRamp(TestWorld.World, CubeMesh, -RampPitchDegrees);
	if (!TestNotNull(TEXT("Spawned downhill ramp"), DownhillRamp))
	{
		return false;
	}
	TestWorld.TickSteps(2);

	const float SleepingSlope = SampleAt(TerrainFluid, FVector::ZeroVector).TerrainSlope.X;
	TestTrue(*FString::Printf(TEXT("A sleeping actor does not sample the new ground (%.3f)"), SleepingSlope), SleepingSlope > 0.1f);

	TerrainFluid->LiveComponent->SetSimulationAwake(true);
	TestWorld.TickSteps(12);
	const float WokenSlope = SampleAt(TerrainFluid, FVector::ZeroVector).TerrainSlope.X;
	const float WokenCornerSlope = SampleAt(TerrainFluid, FVector(-1400.0f, 1200.0f, 0.0f)).TerrainSlope.X;
	TestTrue(*FString::Printf(TEXT("Waking resamples the center and the slope flips to -X (%.3f vs %.3f)"), WokenSlope, -ExpectedSlope), FMath::Abs(WokenSlope + ExpectedSlope) < 0.03f);
	TestTrue(*FString::Printf(TEXT("Waking resamples away from the center too (%.3f)"), WokenCornerSlope), FMath::Abs(WokenCornerSlope + ExpectedSlope) < 0.03f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidTerrainResampleOnLayoutChangeTest,
	"TDGame.FluidFX.LiveActor.TerrainResamplesAfterGridLayoutChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidTerrainResampleOnLayoutChangeTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidScenario;

	FScenarioWorld TestWorld;
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Engine cube mesh loads"), CubeMesh))
	{
		return false;
	}

	AActor* Platform = SpawnStaticBlock(TestWorld.World, CubeMesh, FVector(1000.0f, 0.0f, -100.0f), FRotator::ZeroRotator, FVector(10.0f, 40.0f, 4.0f));
	if (!TestNotNull(TEXT("Spawned platform covering X from 500 to 1500 with its top at height 100"), Platform))
	{
		return false;
	}
	TestWorld.TickSteps(2);

	ATDFluidLiveActor* TerrainFluid = SpawnPresetLiveActor(TestWorld, ETDFluidPresetKind::Water, FVector(0.0f, 0.0f, 300.0f));
	if (!TestNotNull(TEXT("Spawned terrain fluid actor at height 300"), TerrainFluid))
	{
		return false;
	}
	TerrainFluid->bSampleTerrainElevation = true;
	TestWorld.TickSteps(12);

	const FTDFluidGrid2DSolver& Solver = TerrainFluid->LiveComponent->GetSolver();
	constexpr float DropToPlatformCm = 200.0f;
	const float InitialCellCm = Solver.GetExtentCm().X / Solver.GetResolution();
	const float InitialEdgeSlope = SampleAt(TerrainFluid, FVector(500.0f - InitialCellCm * 0.5f, 0.0f, 0.0f)).TerrainSlope.X;
	if (!TestTrue(*FString::Printf(TEXT("The first pass sees the platform edge (%.3f)"), InitialEdgeSlope), FMath::Abs(InitialEdgeSlope + DropToPlatformCm / (2.0f * InitialCellCm)) < 0.05f))
	{
		return false;
	}

	FTDFluidSimulationConfig CoarseConfig = TerrainFluid->LiveComponent->SimulationConfig;
	CoarseConfig.GridResolution = 32;
	CoarseConfig.SimExtentCm = FVector2D(3200.0f, 3200.0f);
	TerrainFluid->LiveComponent->ApplySimulationConfig(CoarseConfig);
	TestEqual(TEXT("The grid is now 32 cells wide"), Solver.GetResolution(), 32);
	TestNearlyEqual(TEXT("The layout change wipes the slope"), MeasureMaxTerrainSlope(TerrainFluid), 0.0f, 1e-4f);
	TestWorld.TickSteps(6);

	constexpr float CoarseCellCm = 100.0f;
	const float RisingEdgeSlope = SampleAt(TerrainFluid, FVector(500.0f - CoarseCellCm * 0.5f, 0.0f, 0.0f)).TerrainSlope.X;
	const float FallingEdgeSlope = SampleAt(TerrainFluid, FVector(1500.0f - CoarseCellCm * 0.5f, 0.0f, 0.0f)).TerrainSlope.X;
	const float ExpectedCoarseSlope = DropToPlatformCm / (2.0f * CoarseCellCm);
	TestTrue(*FString::Printf(TEXT("After a resolution and extent change the rising platform edge is resampled (%.3f vs %.3f)"), RisingEdgeSlope, -ExpectedCoarseSlope), FMath::Abs(RisingEdgeSlope + ExpectedCoarseSlope) < 0.05f);
	TestTrue(*FString::Printf(TEXT("After a resolution and extent change the falling platform edge is resampled (%.3f vs %.3f)"), FallingEdgeSlope, ExpectedCoarseSlope), FMath::Abs(FallingEdgeSlope - ExpectedCoarseSlope) < 0.05f);

	FTDFluidSimulationConfig NarrowConfig = CoarseConfig;
	NarrowConfig.SimExtentCm = FVector2D(1600.0f, 1600.0f);
	TerrainFluid->LiveComponent->ApplySimulationConfig(NarrowConfig);
	TestEqual(TEXT("An extent-only change keeps the grid 32 cells wide"), Solver.GetResolution(), 32);
	TestWorld.TickSteps(6);

	constexpr float NarrowCellCm = 50.0f;
	const float NarrowEdgeSlope = SampleAt(TerrainFluid, FVector(500.0f - NarrowCellCm * 0.5f, 0.0f, 0.0f)).TerrainSlope.X;
	const float ExpectedNarrowSlope = DropToPlatformCm / (2.0f * NarrowCellCm);
	TestTrue(*FString::Printf(TEXT("After an extent-only change the platform edge is resampled at the new cell size (%.3f vs %.3f)"), NarrowEdgeSlope, -ExpectedNarrowSlope), FMath::Abs(NarrowEdgeSlope + ExpectedNarrowSlope) < 0.1f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidBoneStampPathTest,
	"TDGame.FluidFX.LiveActor.TrackedBoneStampsFollowSkeletalMeshAndRejectTeleport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidBoneStampPathTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidScenario;

	FScenarioWorld TestWorld;
	USkeletalMesh* MannequinMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	if (!TestNotNull(TEXT("The Manny skeletal mesh loads"), MannequinMesh))
	{
		return false;
	}

	ATDFluidLiveActor* LiveActor = SpawnPresetLiveActor(TestWorld, ETDFluidPresetKind::Blood, FVector::ZeroVector);
	AActor* Walker = SpawnOverlapProbe(TestWorld.World, FVector(-600.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Spawned fluid actor"), LiveActor) || !TestNotNull(TEXT("Spawned walker"), Walker))
	{
		return false;
	}

	USkeletalMeshComponent* WalkerMesh = NewObject<USkeletalMeshComponent>(Walker);
	Walker->AddInstanceComponent(WalkerMesh);
	WalkerMesh->SetupAttachment(Walker->GetRootComponent());
	WalkerMesh->SetSkeletalMeshAsset(MannequinMesh);
	WalkerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WalkerMesh->RegisterComponent();
	WalkerMesh->RefreshBoneTransforms();

	const FName FootBone(TEXT("foot_l"));
	if (!TestTrue(TEXT("The Manny mesh has a foot_l bone"), WalkerMesh->GetBoneIndex(FootBone) != INDEX_NONE))
	{
		return false;
	}

	auto MoveWalkerBy = [Walker, WalkerMesh](float DeltaX)
	{
		Walker->AddActorWorldOffset(FVector(DeltaX, 0.0f, 0.0f));
		WalkerMesh->RefreshBoneTransforms();
	};
	auto PlaceWalkerAt = [Walker, WalkerMesh](float LocationX)
	{
		Walker->SetActorLocation(FVector(LocationX, 0.0f, 0.0f));
		WalkerMesh->RefreshBoneTransforms();
	};

	const float BoneBeforeMove = WalkerMesh->GetBoneLocation(FootBone).X;
	MoveWalkerBy(100.0f);
	const float BoneMovement = WalkerMesh->GetBoneLocation(FootBone).X - BoneBeforeMove;
	if (!TestTrue(*FString::Printf(TEXT("The foot_l bone follows a 100 cm actor move without animation (%.2f)"), BoneMovement), FMath::Abs(BoneMovement - 100.0f) < 1.0f))
	{
		return false;
	}
	MoveWalkerBy(-100.0f);

	LiveActor->TrackedBoneNames.Reset();
	TestWorld.TickSteps(1);
	MoveWalkerBy(20.0f);
	TestEqual(TEXT("Without tracked bones a walking actor stamps its pivot only"), TestWorld.TickOnceAndReadStampCount(LiveActor), 1);
	MoveWalkerBy(20.0f);
	TestEqual(TEXT("Without tracked bones the second walking step still stamps the pivot only"), TestWorld.TickOnceAndReadStampCount(LiveActor), 1);

	LiveActor->TrackedBoneNames = { FootBone };
	MoveWalkerBy(20.0f);
	TestEqual(TEXT("The first step after tracking starts has no bone history, so only the pivot stamps"), TestWorld.TickOnceAndReadStampCount(LiveActor), 1);
	MoveWalkerBy(20.0f);
	TestEqual(TEXT("A tracked foot_l bone adds one stamp next to the pivot stamp"), TestWorld.TickOnceAndReadStampCount(LiveActor), 2);
	MoveWalkerBy(20.0f);
	TestEqual(TEXT("The tracked bone keeps stamping every walking step"), TestWorld.TickOnceAndReadStampCount(LiveActor), 2);

	MoveWalkerBy(600.0f);
	TestEqual(TEXT("A 12000 cm/s jump stamps neither the pivot nor the bone"), TestWorld.TickOnceAndReadStampCount(LiveActor), 0);
	MoveWalkerBy(20.0f);
	TestEqual(TEXT("Walking right after the jump stamps the pivot and the bone again"), TestWorld.TickOnceAndReadStampCount(LiveActor), 2);

	LiveActor->LiveComponent->ResetSimulationState();
	PlaceWalkerAt(1150.0f);
	TestEqual(TEXT("Placing the walker near the volume edge stamps nothing"), TestWorld.TickOnceAndReadStampCount(LiveActor), 0);
	PlaceWalkerAt(1300.0f);
	TestWorld.TickSteps(3);
	TestEqual(TEXT("A walker outside the interaction volume stamps nothing"), LiveActor->LiveComponent->GetLastProcessedPointCount(), 0);

	PlaceWalkerAt(1100.0f);
	TestEqual(TEXT("Re-entering the volume stamps nothing from the stale bone and pivot positions"), TestWorld.TickOnceAndReadStampCount(LiveActor), 0);
	TestNearlyEqual(TEXT("Re-entering leaves the fluid at rest"), MeasureMaxSpeed(LiveActor), 0.0f, 1e-3f);

	constexpr float WalkSpeedCmPerSec = 400.0f;
	for (int32 Step = 0; Step < 4; ++Step)
	{
		MoveWalkerBy(WalkSpeedCmPerSec * 0.05f);
		TestEqual(TEXT("Walking after re-entry stamps the pivot and the bone"), TestWorld.TickOnceAndReadStampCount(LiveActor), 2);
	}

	const float WalkMaxSpeed = MeasureMaxSpeed(LiveActor);
	TestTrue(*FString::Printf(TEXT("Stamping resumes after re-entry (max speed %.1f)"), WalkMaxSpeed), WalkMaxSpeed > 10.0f);
	TestTrue(*FString::Printf(TEXT("Fluid speed stays within 1.5x of the real walking speed (max speed %.1f)"), WalkMaxSpeed), WalkMaxSpeed <= WalkSpeedCmPerSec * 1.5f);
	TestTrue(TEXT("Fluid speed never exceeds the cell speed cap"), WalkMaxSpeed <= 1200.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidCombatMessageKillStampTest,
	"TDGame.FluidFX.CombatQueue.KilledDamageMessageStampsDeathWithoutDeathEvent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidCombatMessageKillStampTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidScenario;

	FScenarioWorld TestWorld(true);
	if (!TestTrue(TEXT("Test world owns a GameInstance with a message subsystem"), UGameplayMessageSubsystem::HasInstance(TestWorld.World)))
	{
		return false;
	}

	ATDFluidLiveActor* LiveActor = SpawnPresetLiveActor(TestWorld, ETDFluidPresetKind::Blood, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Spawned Blood LiveActor"), LiveActor))
	{
		return false;
	}

	const FVector KilledLocation(-900.0f, -700.0f, 0.0f);
	const FVector SurvivorLocation(-900.0f, 0.0f, 0.0f);
	const FVector LocatedKilledLocation(300.0f, -700.0f, 0.0f);
	const FVector LocatedKillHitLocation(600.0f, -700.0f, 0.0f);
	const FVector StatusKilledLocation(400.0f, 400.0f, 0.0f);
	const FVector DuplicateKilledLocation(-400.0f, 700.0f, 0.0f);
	const FVector DeathOnlyOffset(0.0f, 150.0f, 0.0f);
	const FVector AttackerOffset(-400.0f, 0.0f, 0.0f);

	AActor* KilledTarget = SpawnPlainActor(TestWorld.World, KilledLocation);
	AActor* SurvivorTarget = SpawnPlainActor(TestWorld.World, SurvivorLocation);
	AActor* LocatedKilledTarget = SpawnPlainActor(TestWorld.World, LocatedKilledLocation);
	AActor* StatusKilledTarget = SpawnPlainActor(TestWorld.World, StatusKilledLocation);
	AActor* DuplicateKilledTarget = SpawnPlainActor(TestWorld.World, DuplicateKilledLocation);
	AActor* KilledAttacker = SpawnPlainActor(TestWorld.World, KilledLocation + AttackerOffset);
	AActor* SurvivorAttacker = SpawnPlainActor(TestWorld.World, SurvivorLocation + AttackerOffset);
	AActor* LocatedKilledAttacker = SpawnPlainActor(TestWorld.World, LocatedKilledLocation + AttackerOffset);
	AActor* StatusKilledAttacker = SpawnPlainActor(TestWorld.World, StatusKilledLocation + AttackerOffset);
	AActor* DuplicateKilledAttacker = SpawnPlainActor(TestWorld.World, DuplicateKilledLocation + AttackerOffset);
	if (!KilledTarget || !SurvivorTarget || !LocatedKilledTarget || !StatusKilledTarget || !DuplicateKilledTarget || !KilledAttacker || !SurvivorAttacker || !LocatedKilledAttacker || !StatusKilledAttacker || !DuplicateKilledAttacker)
	{
		AddError(TEXT("Failed to spawn the kill message test actors"));
		return false;
	}

	UGameplayMessageSubsystem& MessageRouter = UGameplayMessageSubsystem::Get(TestWorld.World);
	auto BroadcastDamage = [&MessageRouter](AActor* Attacker, AActor* Target, bool bWasKilled, ETDDamageDelivery Delivery, bool bHasHitLocation, const FVector& HitLocation)
	{
		FTDDamageAppliedMessage Message;
		Message.Instigator = Attacker;
		Message.Target = Target;
		Message.DamageAmount = 10.0f;
		Message.Element = ETDDamageElement::Physical;
		Message.bWasKilled = bWasKilled;
		Message.Delivery = Delivery;
		Message.bHasHitLocation = bHasHitLocation;
		Message.HitLocation = HitLocation;
		MessageRouter.BroadcastMessage(TDGameplayTags::Event_Damage_Applied, Message);
	};

	BroadcastDamage(KilledAttacker, KilledTarget, true, ETDDamageDelivery::Direct, false, FVector::ZeroVector);
	BroadcastDamage(SurvivorAttacker, SurvivorTarget, false, ETDDamageDelivery::Direct, false, FVector::ZeroVector);
	BroadcastDamage(LocatedKilledAttacker, LocatedKilledTarget, true, ETDDamageDelivery::Direct, true, LocatedKillHitLocation);
	BroadcastDamage(StatusKilledAttacker, StatusKilledTarget, true, ETDDamageDelivery::Status, false, FVector::ZeroVector);
	BroadcastDamage(DuplicateKilledAttacker, DuplicateKilledTarget, true, ETDDamageDelivery::Direct, false, FVector::ZeroVector);

	FTDActorDeathMessage DuplicateDeathMessage;
	DuplicateDeathMessage.DeadActor = DuplicateKilledTarget;
	DuplicateDeathMessage.Killer = DuplicateKilledAttacker;
	DuplicateDeathMessage.DeathLocation = DuplicateKilledLocation;
	MessageRouter.BroadcastMessage(TDGameplayTags::Event_Actor_Death, DuplicateDeathMessage);

	TestEqual(TEXT("Killed, survivor, located killed, status killed and duplicate killed hits make eight stamps, and the duplicate death event adds none"), TestWorld.TickOnceAndReadStampCount(LiveActor), 8);

	const float KilledDeathOnly = SampleAt(LiveActor, KilledLocation + DeathOnlyOffset).SelectiveDensity;
	TestTrue(*FString::Printf(TEXT("A killed damage message stamps the death splash beyond the damage stamp radius (selective %.3f)"), KilledDeathOnly), KilledDeathOnly > 0.1f);

	const float SurvivorAtTarget = SampleAt(LiveActor, SurvivorLocation).SelectiveDensity;
	const float SurvivorDeathOnly = SampleAt(LiveActor, SurvivorLocation + DeathOnlyOffset).SelectiveDensity;
	TestTrue(*FString::Printf(TEXT("A surviving hit still stamps its damage at the target (selective %.3f)"), SurvivorAtTarget), SurvivorAtTarget > 0.3f);
	TestTrue(*FString::Printf(TEXT("A surviving hit stamps no death splash (selective %.3f)"), SurvivorDeathOnly), SurvivorDeathOnly < 0.02f);

	const float LocatedDeathOnly = SampleAt(LiveActor, LocatedKillHitLocation + DeathOnlyOffset).SelectiveDensity;
	const float LocatedAtPivot = SampleAt(LiveActor, LocatedKilledLocation).SelectiveDensity;
	TestTrue(*FString::Printf(TEXT("A killed hit with a hit location stamps the death splash at the hit location (selective %.3f)"), LocatedDeathOnly), LocatedDeathOnly > 0.1f);
	TestTrue(*FString::Printf(TEXT("A killed hit with a hit location leaves the target pivot without a death splash (selective %.3f)"), LocatedAtPivot), LocatedAtPivot < 0.02f);

	const float StatusDeathOnly = SampleAt(LiveActor, StatusKilledLocation + DeathOnlyOffset).SelectiveDensity;
	const float StatusAtTarget = SampleAt(LiveActor, StatusKilledLocation).SelectiveDensity;
	TestTrue(*FString::Printf(TEXT("A status delivery kill still stamps the death splash at the target (selective %.3f)"), StatusAtTarget), StatusAtTarget > 0.3f);
	TestTrue(*FString::Printf(TEXT("A status delivery kill stamps the death splash beyond the damage stamp radius (selective %.3f)"), StatusDeathOnly), StatusDeathOnly > 0.1f);

	const float DuplicateDeathOnly = SampleAt(LiveActor, DuplicateKilledLocation + DeathOnlyOffset).SelectiveDensity;
	TestTrue(*FString::Printf(TEXT("A killed hit that also sends a death event still stamps one death splash (selective %.3f)"), DuplicateDeathOnly), DuplicateDeathOnly > 0.1f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidMissingHitDirectionFallbackTest,
	"TDGame.FluidFX.CombatQueue.MissingHitDirectionFallsBackToAttackerToTargetDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidMissingHitDirectionFallbackTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidScenario;

	FScenarioWorld TestWorld(true);
	if (!TestTrue(TEXT("Test world owns a GameInstance with a message subsystem"), UGameplayMessageSubsystem::HasInstance(TestWorld.World)))
	{
		return false;
	}

	ATDFluidLiveActor* LiveActor = SpawnPresetLiveActor(TestWorld, ETDFluidPresetKind::Blood, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Spawned Blood LiveActor"), LiveActor))
	{
		return false;
	}

	const FVector PushedPositiveXLocation(-400.0f, 0.0f, 0.0f);
	const FVector PushedNegativeXLocation(400.0f, 0.0f, 0.0f);
	AActor* PushedPositiveXTarget = SpawnPlainActor(TestWorld.World, PushedPositiveXLocation);
	AActor* PushedNegativeXTarget = SpawnPlainActor(TestWorld.World, PushedNegativeXLocation);
	AActor* AttackerOnNegativeXSide = SpawnPlainActor(TestWorld.World, FVector(-1000.0f, 0.0f, 0.0f));
	AActor* AttackerOnPositiveXSide = SpawnPlainActor(TestWorld.World, FVector(1000.0f, 0.0f, 0.0f));
	if (!PushedPositiveXTarget || !PushedNegativeXTarget || !AttackerOnNegativeXSide || !AttackerOnPositiveXSide)
	{
		AddError(TEXT("Failed to spawn the fallback direction test actors"));
		return false;
	}

	UGameplayMessageSubsystem& MessageRouter = UGameplayMessageSubsystem::Get(TestWorld.World);
	auto BroadcastUndirectedDamage = [&MessageRouter](AActor* Attacker, AActor* Target)
	{
		FTDDamageAppliedMessage Message;
		Message.Instigator = Attacker;
		Message.Target = Target;
		Message.DamageAmount = 60.0f;
		Message.Element = ETDDamageElement::Physical;
		MessageRouter.BroadcastMessage(TDGameplayTags::Event_Damage_Applied, Message);
	};

	BroadcastUndirectedDamage(AttackerOnNegativeXSide, PushedPositiveXTarget);
	BroadcastUndirectedDamage(AttackerOnPositiveXSide, PushedNegativeXTarget);

	TestEqual(TEXT("Two undirected hits make two stamps"), TestWorld.TickOnceAndReadStampCount(LiveActor), 2);

	const FVector2D PositiveXVelocity = SampleAt(LiveActor, PushedPositiveXLocation).Velocity;
	const FVector2D NegativeXVelocity = SampleAt(LiveActor, PushedNegativeXLocation).Velocity;
	AddInfo(FString::Printf(TEXT("Velocity from -X attacker (%.1f, %.1f) from +X attacker (%.1f, %.1f)"), PositiveXVelocity.X, PositiveXVelocity.Y, NegativeXVelocity.X, NegativeXVelocity.Y));

	TestTrue(*FString::Printf(TEXT("An attacker on the -X side pushes the fluid toward +X without a hit direction (%.1f)"), PositiveXVelocity.X), PositiveXVelocity.X > 50.0f);
	TestTrue(*FString::Printf(TEXT("An attacker on the +X side pushes the fluid toward -X without a hit direction (%.1f)"), NegativeXVelocity.X), NegativeXVelocity.X < -50.0f);
	TestTrue(*FString::Printf(TEXT("The fallback push leaves almost no Y velocity (%.1f)"), PositiveXVelocity.Y), FMath::Abs(PositiveXVelocity.Y) < 0.3f * FMath::Abs(PositiveXVelocity.X));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidTeleportSpeedThresholdTest,
	"TDGame.FluidFX.LiveActor.RegisteredActorStampsJustBelowTeleportSpeedAndSkipsJustAboveIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidTeleportSpeedThresholdTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidScenario;

	FScenarioWorld TestWorld;
	ATDFluidLiveActor* LiveActor = SpawnPresetLiveActor(TestWorld, ETDFluidPresetKind::Blood, FVector::ZeroVector);
	AActor* Mover = SpawnPlainActor(TestWorld.World, FVector(-600.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Spawned fluid actor"), LiveActor) || !TestNotNull(TEXT("Spawned mover"), Mover))
	{
		return false;
	}

	constexpr float StepSeconds = 0.05f;
	LiveActor->RegisterInteractingActor(Mover);
	TestWorld.TickSteps(2);
	TestEqual(TEXT("A registered actor standing still stamps nothing"), LiveActor->LiveComponent->GetLastProcessedPointCount(), 0);

	Mover->AddActorWorldOffset(FVector(2900.0f * StepSeconds, 0.0f, 0.0f));
	TestEqual(TEXT("A 2900 cm/s step is below the teleport speed and stamps the pivot"), TestWorld.TickOnceAndReadStampCount(LiveActor), 1);

	Mover->AddActorWorldOffset(FVector(3100.0f * StepSeconds, 0.0f, 0.0f));
	TestEqual(TEXT("A 3100 cm/s step is above the teleport speed and stamps nothing"), TestWorld.TickOnceAndReadStampCount(LiveActor), 0);

	Mover->AddActorWorldOffset(FVector(400.0f * StepSeconds, 0.0f, 0.0f));
	TestEqual(TEXT("A 400 cm/s step right after the teleport stamps the pivot again"), TestWorld.TickOnceAndReadStampCount(LiveActor), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidKnownElementOverridesUnknownElementTest,
	"TDGame.FluidFX.CombatQueue.KnownElementOverridesUnknownElementOnTheSameTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidKnownElementOverridesUnknownElementTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidScenario;

	FScenarioWorld TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	ATDFluidLiveActor* LiveActor = SpawnPresetLiveActor(TestWorld, ETDFluidPresetKind::Blood, FVector::ZeroVector);
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem) || !TestNotNull(TEXT("Spawned Blood LiveActor"), LiveActor))
	{
		return false;
	}

	const FVector UnknownFirstLocation(-800.0f, 0.0f, 0.0f);
	const FVector KnownFirstLocation(800.0f, 0.0f, 0.0f);
	const FVector UnknownOnlyLocation(0.0f, 800.0f, 0.0f);
	AActor* UnknownFirstActor = SpawnPlainActor(TestWorld.World, UnknownFirstLocation);
	AActor* KnownFirstActor = SpawnPlainActor(TestWorld.World, KnownFirstLocation);
	AActor* UnknownOnlyActor = SpawnPlainActor(TestWorld.World, UnknownOnlyLocation);
	if (!UnknownFirstActor || !KnownFirstActor || !UnknownOnlyActor)
	{
		AddError(TEXT("Failed to spawn the element override target actors"));
		return false;
	}

	Subsystem->QueueCombatDamage(nullptr, UnknownFirstActor, 50.0f, ETDDamageElement::Physical, false, false);
	Subsystem->QueueCombatDamage(nullptr, UnknownFirstActor, 20.0f, ETDDamageElement::Frost, true, false);

	Subsystem->QueueCombatDamage(nullptr, KnownFirstActor, 20.0f, ETDDamageElement::Frost, true, false);
	Subsystem->QueueCombatDamage(nullptr, KnownFirstActor, 50.0f, ETDDamageElement::Physical, false, false);

	Subsystem->QueueCombatDamage(nullptr, UnknownOnlyActor, 50.0f, ETDDamageElement::Physical, false, false);

	TestEqual(TEXT("Five queued hits on three targets become three stamps"), TestWorld.TickOnceAndReadStampCount(LiveActor), 3);

	const FTDFluidCellSample UnknownFirstSample = SampleAt(LiveActor, UnknownFirstLocation);
	const FTDFluidCellSample KnownFirstSample = SampleAt(LiveActor, KnownFirstLocation);
	const FTDFluidCellSample UnknownOnlySample = SampleAt(LiveActor, UnknownOnlyLocation);
	TestTrue(*FString::Printf(TEXT("A known frost hit after an unknown physical hit stamps the water channel (primary %.3f)"), UnknownFirstSample.PrimaryDensity), UnknownFirstSample.PrimaryDensity > 0.4f);
	TestNearlyEqual(TEXT("A known frost hit after an unknown physical hit leaves the selective density empty"), UnknownFirstSample.SelectiveDensity, 0.0f, 1e-3f);
	TestTrue(*FString::Printf(TEXT("An unknown physical hit after a known frost hit keeps the water channel (primary %.3f)"), KnownFirstSample.PrimaryDensity), KnownFirstSample.PrimaryDensity > 0.4f);
	TestNearlyEqual(TEXT("An unknown physical hit after a known frost hit leaves the selective density empty"), KnownFirstSample.SelectiveDensity, 0.0f, 1e-3f);
	TestTrue(*FString::Printf(TEXT("An unknown physical hit alone falls back to the blood channel (selective %.3f)"), UnknownOnlySample.SelectiveDensity), UnknownOnlySample.SelectiveDensity > 0.3f);

	return true;
}

#endif
