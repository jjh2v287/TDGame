#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/Damage/TDDamageTarget.h"
#include "Combat/TDCombatComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TDGameplayMessages.h"
#include "Core/TDGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
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
#include <limits>

namespace
{
	struct FTDFluidTestWorldScope
	{
		explicit FTDFluidTestWorldScope(bool bShouldAttachGameInstance = false)
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidTestWorld"));
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

		~FTDFluidTestWorldScope()
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

		void TickWorld(float TotalSeconds, float StepSeconds = 0.05f)
		{
			for (float Remaining = TotalSeconds; Remaining > UE_SMALL_NUMBER;)
			{
				const float Delta = FMath::Min(Remaining, StepSeconds);
				++GFrameCounter;
				World->Tick(LEVELTICK_All, Delta);
				Remaining -= Delta;
			}
		}

		UWorld* World = nullptr;
		UGameInstance* GameInstance = nullptr;
	};

	AActor* SpawnLocatedTestActor(UWorld* World, const FVector& Location)
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

	FTDFluidCellSample SampleLiveActorAt(const ATDFluidLiveActor* LiveActor, const FVector& WorldLocation)
	{
		FTDFluidCellSample Sample;
		LiveActor->LiveComponent->SampleFluidAtWorldLocation(WorldLocation, Sample);
		return Sample;
	}

	int32 TickOnceAndReadProcessedStampCount(FTDFluidTestWorldScope& TestWorld, const ATDFluidLiveActor* LiveActor)
	{
		TestWorld.TickWorld(0.05f, 0.05f);
		return LiveActor->LiveComponent->GetLastProcessedPointCount();
	}

	float MeasureLiveActorMaxSpeed(const ATDFluidLiveActor* LiveActor)
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

	float MeasureLiveActorMaxTerrainSlope(const ATDFluidLiveActor* LiveActor)
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

	FVector2D MeasureLiveActorDensityCentroid(const ATDFluidLiveActor* LiveActor)
	{
		const FTDFluidGrid2DSolver& Solver = LiveActor->LiveComponent->GetSolver();
		const int32 Resolution = Solver.GetResolution();
		double WeightSum = 0.0;
		FVector2D WeightedPosition = FVector2D::ZeroVector;
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const double Weight = Solver.SampleAtCell(CellX, CellY).PrimaryDensity;
				WeightSum += Weight;
				WeightedPosition += Solver.CellToWorldCenter(CellX, CellY) * Weight;
			}
		}
		return WeightSum > 0.0 ? WeightedPosition / WeightSum : FVector2D::ZeroVector;
	}

	AActor* SpawnOverlapProbeActor(UWorld* World, const FVector& Location)
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

	UTDCombatComponent* AddLiveActorCombatant(AActor* Actor)
	{
		UTDCombatComponent* Combatant = NewObject<UTDCombatComponent>(Actor);
		Actor->AddInstanceComponent(Combatant);
		FTDCombatStats Stats;
		Stats.BaseMaxHealth = 1000.0f;
		Combatant->SetStats(Stats);
		Combatant->RegisterComponent();
		return Combatant;
	}

	AActor* SpawnStaticTerrainBlock(UWorld* World, UStaticMesh* Mesh, const FVector& Center, const FRotator& Rotation, const FVector& Scale)
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

	bool AreAllPackedChannelsZero(const TArray<FFloat16Color>& Pixels)
	{
		if (Pixels.IsEmpty())
		{
			return false;
		}
		for (const FFloat16Color& Pixel : Pixels)
		{
			if (Pixel.R.GetFloat() != 0.0f || Pixel.B.GetFloat() != 0.0f)
			{
				return false;
			}
		}
		return true;
	}

	bool HasAnyPackedDensity(const TArray<FFloat16Color>& Pixels)
	{
		for (const FFloat16Color& Pixel : Pixels)
		{
			if (Pixel.R.GetFloat() > 0.0f && Pixel.B.GetFloat() > 0.0f)
			{
				return true;
			}
		}
		return false;
	}

	void TickLiveActorSteps(FTDFluidTestWorldScope& TestWorld, int32 StepCount)
	{
		for (int32 Step = 0; Step < StepCount; ++Step)
		{
			TestWorld.TickWorld(0.05f, 0.05f);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSimplePainterTest,
	"TDGame.FluidFX.SimplePainter.FootprintTrailAndFade",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSimplePainterTest::RunTest(const FString& Parameters)
{
	FTDFluidSimulationConfig PainterConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::FootprintPainter);
	PainterConfig.DensityDissipation = 0.5f;

	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(PainterConfig, FVector2D::ZeroVector);

	FTDFluidBrushStamp FootprintStamp;
	FootprintStamp.WorldLocation = FVector(0.0f, 0.0f, 0.0f);
	FootprintStamp.WorldVelocity = FVector(800.0f, 0.0f, 0.0f);
	FootprintStamp.RadiusCm = 120.0f;
	FootprintStamp.DensityStrength = 1.0f;
	FootprintStamp.Channel = ETDFluidBrushChannel::ChannelR_Mud;

	Solver.ApplyBrushStamps({ FootprintStamp }, 0.05f);

	FTDFluidCellSample InitialCenterSample;
	TestTrue(TEXT("Center sample is valid"), Solver.SampleAtWorldPosition(FVector2D(0.0f, 0.0f), InitialCenterSample));
	TestTrue(TEXT("Footprint stamp writes positive primary density"), InitialCenterSample.PrimaryDensity > 0.5f);

	Solver.StepSimulation(0.1f);

	FTDFluidCellSample AfterStepCenterSample;
	FTDFluidCellSample FarDownstreamSample;
	Solver.SampleAtWorldPosition(FVector2D(0.0f, 0.0f), AfterStepCenterSample);
	Solver.SampleAtWorldPosition(FVector2D(600.0f, 0.0f), FarDownstreamSample);

	TestTrue(TEXT("SimplePainter decays density over time"), AfterStepCenterSample.PrimaryDensity < InitialCenterSample.PrimaryDensity);
	TestTrue(TEXT("SimplePainter retains trail density"), AfterStepCenterSample.PrimaryDensity > 0.1f);
	TestNearlyEqual(TEXT("SimplePainter does not advect density far downstream"), FarDownstreamSample.PrimaryDensity, 0.0f, 1e-4f);

	TArray<FFloat16Color> DensityPixels;
	TArray<FFloat16Color> VelocityPixels;
	Solver.PackOutputPixels(DensityPixels, VelocityPixels);
	TestEqual(TEXT("Packed pixel buffer matches grid cell count"), DensityPixels.Num(), PainterConfig.GridResolution * PainterConfig.GridResolution);
	TestEqual(TEXT("Packed velocity buffer matches grid cell count"), VelocityPixels.Num(), PainterConfig.GridResolution * PainterConfig.GridResolution);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSimAdvectionAndViscosityTest,
	"TDGame.FluidFX.FluidSim.AdvectionPressureAndViscosity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSimAdvectionAndViscosityTest::RunTest(const FString& Parameters)
{
	FTDFluidSimulationConfig WaterConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Water);
	FTDFluidSimulationConfig MudConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Mud);
	WaterConfig.SimExtentCm = FVector2D(3200.0f, 3200.0f);
	MudConfig.SimExtentCm = FVector2D(3200.0f, 3200.0f);

	FTDFluidGrid2DSolver WaterSolver;
	FTDFluidGrid2DSolver MudSolver;
	WaterSolver.Initialize(WaterConfig, FVector2D::ZeroVector);
	MudSolver.Initialize(MudConfig, FVector2D::ZeroVector);

	FTDFluidBrushStamp ImpulseStamp;
	ImpulseStamp.WorldLocation = FVector(0.0f, 0.0f, 0.0f);
	ImpulseStamp.WorldVelocity = FVector(1200.0f, 0.0f, 0.0f);
	ImpulseStamp.RadiusCm = 180.0f;
	ImpulseStamp.DensityStrength = 1.0f;
	ImpulseStamp.VelocityStrength = 1.0f;

	WaterSolver.ApplyBrushStamps({ ImpulseStamp }, 0.05f);
	MudSolver.ApplyBrushStamps({ ImpulseStamp }, 0.05f);

	for (int32 Step = 0; Step < 8; ++Step)
	{
		WaterSolver.StepSimulation(0.05f);
		MudSolver.StepSimulation(0.05f);
	}

	FTDFluidCellSample WaterDownstream;
	FTDFluidCellSample MudDownstream;
	WaterSolver.SampleAtWorldPosition(FVector2D(250.0f, 0.0f), WaterDownstream);
	MudSolver.SampleAtWorldPosition(FVector2D(250.0f, 0.0f), MudDownstream);

	TestTrue(TEXT("Water advects density downstream"), WaterDownstream.PrimaryDensity > 0.01f);
	TestTrue(TEXT("Low-viscosity water retains higher downstream speed than high-viscosity mud"), WaterDownstream.Velocity.X > MudDownstream.Velocity.X);

	for (int32 Step = 0; Step < 20; ++Step)
	{
		WaterSolver.StepSimulation(0.05f);
		MudSolver.StepSimulation(0.05f);
	}

	FTDFluidCellSample WaterOriginLate;
	FTDFluidCellSample MudOriginLate;
	WaterSolver.SampleAtWorldPosition(FVector2D(50.0f, 0.0f), WaterOriginLate);
	MudSolver.SampleAtWorldPosition(FVector2D(50.0f, 0.0f), MudOriginLate);
	TestTrue(TEXT("High-viscosity mud persists longer near origin than water"), MudOriginLate.PrimaryDensity > WaterOriginLate.PrimaryDensity);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFieldBufferObstacleAndSlopeTest,
	"TDGame.FluidFX.FieldBuffer.SdfObstacleAndTerrainSlope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFieldBufferObstacleAndSlopeTest::RunTest(const FString& Parameters)
{
	FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Blood);
	Config.GridResolution = 64;
	Config.SimExtentCm = FVector2D(3200.0f, 3200.0f);

	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Config, FVector2D::ZeroVector);

	FTDFluidSdfObstacle RockObstacle;
	RockObstacle.WorldCenter = FVector(250.0f, 0.0f, 0.0f);
	RockObstacle.RadiusCm = 150.0f;
	RockObstacle.bIsBox = false;
	Solver.RasterizeSdfObstacles({ RockObstacle });

	FTDFluidBrushStamp StreamStamp;
	StreamStamp.WorldLocation = FVector(0.0f, 0.0f, 0.0f);
	StreamStamp.WorldVelocity = FVector(900.0f, 0.0f, 0.0f);
	StreamStamp.RadiusCm = 160.0f;
	StreamStamp.DensityStrength = 1.0f;
	StreamStamp.Channel = ETDFluidBrushChannel::ChannelG_Blood;

	for (int32 Step = 0; Step < 6; ++Step)
	{
		Solver.ApplyBrushStamps({ StreamStamp }, 0.05f);
		Solver.StepSimulation(0.05f);
	}

	FTDFluidCellSample InsideRockSample;
	Solver.SampleAtWorldPosition(FVector2D(250.0f, 0.0f), InsideRockSample);
	TestTrue(TEXT("Obstacle interior has non-positive SDF"), InsideRockSample.ObstacleSdf <= 0.0f);
	TestNearlyEqual(TEXT("Obstacle interior blocks fluid density"), InsideRockSample.PrimaryDensity, 0.0f, 1e-4f);
	TestNearlyEqual(TEXT("Obstacle interior zeroes velocity"), static_cast<float>(InsideRockSample.Velocity.Size()), 0.0f, 1e-4f);

	FTDFluidGrid2DSolver SlopeSolver;
	SlopeSolver.Initialize(Config, FVector2D::ZeroVector);
	SlopeSolver.SetUniformTerrainSlope(FVector2D(-0.5f, 0.0f));

	FTDFluidBrushStamp StaticBloodDrop;
	StaticBloodDrop.WorldLocation = FVector(0.0f, 0.0f, 0.0f);
	StaticBloodDrop.WorldVelocity = FVector::ZeroVector;
	StaticBloodDrop.RadiusCm = 150.0f;
	StaticBloodDrop.DensityStrength = 1.0f;
	StaticBloodDrop.Channel = ETDFluidBrushChannel::ChannelG_Blood;

	SlopeSolver.ApplyBrushStamps({ StaticBloodDrop }, 0.05f);
	SlopeSolver.StepSimulation(0.05f);

	FTDFluidCellSample SlopedSample;
	SlopeSolver.SampleAtWorldPosition(FVector2D(0.0f, 0.0f), SlopedSample);
	TestTrue(TEXT("Terrain slope accelerates stationary fluid downhill along +X"), SlopedSample.Velocity.X > 1.0f);
	TestTrue(TEXT("Blood channel populates selective density buffer"), SlopedSample.SelectiveDensity > 0.2f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSlidingWindowShiftTest,
	"TDGame.FluidFX.SlidingWindow.WorldCoordinatePreservation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSlidingWindowShiftTest::RunTest(const FString& Parameters)
{
	FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::FootprintPainter);
	Config.GridResolution = 64;
	Config.SimExtentCm = FVector2D(3200.0f, 3200.0f);

	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Config, FVector2D::ZeroVector);

	const FVector2D MarkedWorldPos(425.0f, -275.0f);
	FTDFluidBrushStamp WorldMark;
	WorldMark.WorldLocation = FVector(MarkedWorldPos.X, MarkedWorldPos.Y, 0.0f);
	WorldMark.RadiusCm = 120.0f;
	WorldMark.DensityStrength = 1.0f;
	Solver.ApplyBrushStamps({ WorldMark }, 0.05f);

	FTDFluidCellSample BeforeShiftSample;
	Solver.SampleAtWorldPosition(MarkedWorldPos, BeforeShiftSample);
	TestTrue(TEXT("World mark exists before sliding window shift"), BeforeShiftSample.PrimaryDensity > 0.5f);

	TestTrue(TEXT("Sliding window starts centered on the origin"), Solver.GetCenterWorld().Equals(FVector2D::ZeroVector, 0.01f));

	const bool bShifted = Solver.ShiftOriginForSlidingWindow(FVector2D(300.0f, -200.0f));
	TestTrue(TEXT("Sliding window shifts when displacement exceeds cell size"), bShifted);
	TestTrue(TEXT("Sliding window center actually moved to the requested cell-aligned position"), Solver.GetCenterWorld().Equals(FVector2D(300.0f, -200.0f), 0.01f));

	FTDFluidCellSample AfterShiftSample;
	Solver.SampleAtWorldPosition(MarkedWorldPos, AfterShiftSample);
	TestNearlyEqual(TEXT("World mark stays anchored at same world coordinates after window shift"), AfterShiftSample.PrimaryDensity, BeforeShiftSample.PrimaryDensity, 0.05f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidLiveActorSleepWakeAndTrackingTest,
	"TDGame.FluidFX.LiveActor.ActivationSleepWakeAndActorTracking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidLiveActorSleepWakeAndTrackingTest::RunTest(const FString& Parameters)
{
	FTDFluidTestWorldScope TestWorld;

	ATDFluidLiveActor* LiveActor = TestWorld.World->SpawnActor<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Spawned ATDFluidLiveActor"), LiveActor))
	{
		return false;
	}

	LiveActor->bShouldUseActivationVolume = true;
	LiveActor->ActivationDistanceCm = 1000.0f;

	LiveActor->EvaluateActivationProximity(FVector(2500.0f, 0.0f, 0.0f));
	TestFalse(TEXT("LiveComponent sleeps when observer is outside ActivationDistanceCm"), LiveActor->LiveComponent->IsSimulationAwake());

	FTDFluidBrushStamp IgnoredStamp;
	IgnoredStamp.WorldLocation = FVector::ZeroVector;
	IgnoredStamp.DensityStrength = 1.0f;
	LiveActor->LiveComponent->QueueBrushStamp(IgnoredStamp);
	LiveActor->StepLiveActor(0.05f);

	FTDFluidCellSample AsleepSample;
	LiveActor->LiveComponent->SampleFluidAtWorldLocation(FVector::ZeroVector, AsleepSample);
	TestNearlyEqual(TEXT("Sleeping LiveComponent ignores brush stamps"), AsleepSample.PrimaryDensity, 0.0f, 1e-4f);

	LiveActor->EvaluateActivationProximity(FVector(300.0f, 0.0f, 0.0f));
	TestTrue(TEXT("LiveComponent wakes when observer enters ActivationDistanceCm"), LiveActor->LiveComponent->IsSimulationAwake());

	AActor* MovingDummy = TestWorld.World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	USceneComponent* DummyRoot = NewObject<USceneComponent>(MovingDummy);
	MovingDummy->AddInstanceComponent(DummyRoot);
	MovingDummy->SetRootComponent(DummyRoot);
	DummyRoot->RegisterComponent();

	LiveActor->RegisterInteractingActor(MovingDummy);
	MovingDummy->SetActorLocation(FVector(0.0f, 0.0f, 0.0f));
	LiveActor->StepLiveActor(0.05f);

	MovingDummy->SetActorLocation(FVector(100.0f, 0.0f, 0.0f));
	LiveActor->StepLiveActor(0.05f);

	FTDFluidCellSample WakeTrailSample;
	LiveActor->LiveComponent->SampleFluidAtWorldLocation(FVector(100.0f, 0.0f, 0.0f), WakeTrailSample);
	TestTrue(TEXT("Moving registered actor paints velocity-driven density trail"), WakeTrailSample.PrimaryDensity > 0.05f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSubsystemPoolAndBudgetTest,
	"TDGame.FluidFX.Subsystem.PresetPoolAndPointBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSubsystemPoolAndBudgetTest::RunTest(const FString& Parameters)
{
	FTDFluidTestWorldScope TestWorld;
	UTDFluidLiveSubsystem* Subsystem = TestWorld.World->GetSubsystem<UTDFluidLiveSubsystem>();
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists in Game world"), Subsystem))
	{
		return false;
	}

	UTDFluidPresetAsset* BloodPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Blood);
	ATDFluidLiveActor* FirstSpawn = Subsystem->SpawnPooledFluidActor(BloodPreset, FVector::ZeroVector, 0.15f);
	if (!TestNotNull(TEXT("SpawnPooledFluidActor returns valid LiveActor"), FirstSpawn))
	{
		return false;
	}

	TestEqual(TEXT("Subsystem tracks 1 active actor"), Subsystem->GetActiveActorCount(), 1);
	TestEqual(TEXT("Subsystem tracks 0 inactive pooled actors"), Subsystem->GetPooledInactiveActorCount(), 0);

	TArray<FVector> SplashPositions;
	TArray<FVector> SplashVelocities;
	for (int32 Idx = 0; Idx < 100; ++Idx)
	{
		SplashPositions.Add(FVector(static_cast<float>(Idx), 0.0f, 0.0f));
		SplashVelocities.Add(FVector(0.0f, 100.0f, -200.0f));
	}

	const int32 Receivers = Subsystem->InjectCombatParticleSplash(
		SplashPositions,
		SplashVelocities,
		40.0f,
		ETDFluidBrushChannel::ChannelG_Blood);
	TestEqual(TEXT("Particle splash routed to active LiveActor"), Receivers, 1);

	TestWorld.TickWorld(0.05f, 0.05f);
	TestEqual(
		TEXT("LiveComponent clamps processed points to preset MaxPointsPerTick"),
		FirstSpawn->LiveComponent->GetLastProcessedPointCount(),
		FirstSpawn->LiveComponent->SimulationConfig.MaxPointsPerTick);

	TestWorld.TickWorld(0.20f, 0.05f);
	TestEqual(TEXT("Expired LiveActor automatically returns to pool"), Subsystem->GetActiveActorCount(), 0);
	TestEqual(TEXT("Inactive pool contains 1 actor"), Subsystem->GetPooledInactiveActorCount(), 1);

	UTDFluidPresetAsset* FirePreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Fire);
	ATDFluidLiveActor* SecondSpawn = Subsystem->SpawnPooledFluidActor(FirePreset, FVector(500.0f, 0.0f, 0.0f), 1.0f);
	TestTrue(TEXT("Second pooled spawn reuses pooled actor instance"), SecondSpawn == FirstSpawn);
	TestEqual(TEXT("Reused actor updates default channel to Fire"), SecondSpawn->InteractionBrushChannel, ETDFluidBrushChannel::ChannelB_Fire);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidCombatIntegrationTest,
	"TDGame.FluidFX.CombatIntegration.DamageAndDeathInjectFluidStamps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidCombatIntegrationTest::RunTest(const FString& Parameters)
{
	FTDFluidTestWorldScope TestWorld;
	UTDFluidLiveSubsystem* Subsystem = TestWorld.World->GetSubsystem<UTDFluidLiveSubsystem>();
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem))
	{
		return false;
	}

	UTDFluidPresetAsset* BloodPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Blood);
	ATDFluidLiveActor* LiveActor = Subsystem->SpawnPooledFluidActor(BloodPreset, FVector::ZeroVector, 10.0f);
	if (!TestNotNull(TEXT("Spawned Blood LiveActor"), LiveActor))
	{
		return false;
	}

	const FVector ExpectedMeshScale(
		LiveActor->LiveComponent->SimulationConfig.SimExtentCm.X / 100.0f,
		LiveActor->LiveComponent->SimulationConfig.SimExtentCm.Y / 100.0f,
		1.0f);
	TestTrue(
		TEXT("InternalSurfaceMesh automatically scales to match SimExtentCm"),
		LiveActor->InternalSurfaceMesh->GetRelativeScale3D().Equals(ExpectedMeshScale, 0.01f));

	AActor* Attacker = TestWorld.World->SpawnActor<AActor>(AActor::StaticClass(), FVector(-200.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	USceneComponent* AttackerRoot = NewObject<USceneComponent>(Attacker);
	Attacker->AddInstanceComponent(AttackerRoot);
	Attacker->SetRootComponent(AttackerRoot);
	AttackerRoot->RegisterComponent();
	Attacker->SetActorLocation(FVector(-200.0f, 0.0f, 0.0f));

	ATDDamageTarget* CombatTarget = TestWorld.World->SpawnActor<ATDDamageTarget>(
		ATDDamageTarget::StaticClass(),
		FVector(150.0f, 0.0f, 0.0f),
		FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Spawned ATDDamageTarget"), CombatTarget))
	{
		return false;
	}

	LiveActor->RegisterInteractingActor(CombatTarget);

	FTDDamageContext HitContext;
	HitContext.Caster = Attacker;
	HitContext.CastTarget = CombatTarget->GetActorLocation();
	HitContext.Direction = FVector(1.0f, 0.0f, 0.0f);
	HitContext.Budget = MakeShared<FTDDamageChainBudget>();

	const FTDDamageResult HitResult = CombatTarget->GetCombatComponent()->ReceiveDamage(
		80.0f,
		ETDDamageElement::Physical,
		false,
		HitContext);
	TestTrue(TEXT("Combat target received positive damage"), HitResult.AppliedDamage > 0.0f);

	TestWorld.TickWorld(0.05f, 0.05f);

	FTDFluidCellSample HitSample;
	TestTrue(
		TEXT("Sampled fluid at combat target location"),
		LiveActor->LiveComponent->SampleFluidAtWorldLocation(FVector(150.0f, 0.0f, 0.0f), HitSample));
	TestTrue(TEXT("Combat hit automatically injects primary density"), HitSample.PrimaryDensity > 0.1f);
	TestTrue(TEXT("Combat hit automatically injects blood selective density"), HitSample.SelectiveDensity > 0.1f);
	TestTrue(TEXT("Combat hit injects directional velocity along attacker->target"), HitSample.Velocity.X > 10.0f);

	const int32 FireReceivers = Subsystem->NotifyCombatDamageApplied(
		Attacker,
		CombatTarget,
		120.0f,
		ETDDamageElement::Fire,
		true);
	TestEqual(TEXT("Elemental fire hit notifies active LiveActor"), FireReceivers, 1);

	const FTDDamageResult KillResult = CombatTarget->GetCombatComponent()->ReceiveDamage(
		500.0f,
		ETDDamageElement::Physical,
		false,
		HitContext);
	TestTrue(TEXT("Lethal damage kills target"), KillResult.bWasKilled);

	TestWorld.TickWorld(0.05f, 0.05f);

	FTDFluidCellSample PostDeathSample;
	LiveActor->LiveComponent->SampleFluidAtWorldLocation(FVector(150.0f, 0.0f, 0.0f), PostDeathSample);
	TestTrue(
		TEXT("Fire and death splashes accumulate higher selective density"),
		PostDeathSample.SelectiveDensity > HitSample.SelectiveDensity);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidCombatQueueMergeTest,
	"TDGame.FluidFX.CombatQueue.MergesPerTargetAndKeepsKnownElementChannel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidCombatQueueMergeTest::RunTest(const FString& Parameters)
{
	FTDFluidTestWorldScope TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("GetFluidLiveSubsystem resolves the world subsystem"), Subsystem))
	{
		return false;
	}

	UTDFluidPresetAsset* BloodPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Blood);
	ATDFluidLiveActor* LiveActor = Subsystem->SpawnPooledFluidActor(BloodPreset, FVector::ZeroVector, 10.0f);
	if (!TestNotNull(TEXT("Spawned Blood LiveActor"), LiveActor))
	{
		return false;
	}

	const FVector FrostLocation(500.0f, 0.0f, 0.0f);
	const FVector FireLocation(-500.0f, 0.0f, 0.0f);
	AActor* FrostTarget = SpawnLocatedTestActor(TestWorld.World, FrostLocation);
	AActor* FireTarget = SpawnLocatedTestActor(TestWorld.World, FireLocation);
	if (!TestNotNull(TEXT("Spawned frost target"), FrostTarget) || !TestNotNull(TEXT("Spawned fire target"), FireTarget))
	{
		return false;
	}

	Subsystem->QueueCombatDamage(nullptr, FrostTarget, 30.0f, ETDDamageElement::Physical, false, false);
	Subsystem->QueueCombatDamage(nullptr, FrostTarget, 20.0f, ETDDamageElement::Frost, true, false);
	Subsystem->QueueCombatDamage(nullptr, FireTarget, 20.0f, ETDDamageElement::Fire, true, false);
	Subsystem->QueueCombatDamage(nullptr, FireTarget, 30.0f, ETDDamageElement::Physical, false, false);

	const FTDFluidCellSample BeforeTick = SampleLiveActorAt(LiveActor, FrostLocation);
	TestNearlyEqual(TEXT("Queued damage does not stamp before the subsystem tick"), BeforeTick.PrimaryDensity, 0.0f, 1e-4f);

	TestEqual(TEXT("Four queued hits on two targets in one frame become two stamps"), TickOnceAndReadProcessedStampCount(TestWorld, LiveActor), 2);

	const FTDFluidCellSample FrostSample = SampleLiveActorAt(LiveActor, FrostLocation);
	TestTrue(TEXT("Frost stamp writes primary density"), FrostSample.PrimaryDensity > 0.1f);
	TestNearlyEqual(TEXT("Known frost element wins over the unknown physical element so the water channel leaves selective density at zero"), FrostSample.SelectiveDensity, 0.0f, 1e-4f);

	const FTDFluidCellSample FireSample = SampleLiveActorAt(LiveActor, FireLocation);
	TestTrue(TEXT("Known fire element writes selective density"), FireSample.SelectiveDensity > 0.1f);

	Subsystem->QueueCombatDamage(nullptr, FrostTarget, 30.0f, ETDDamageElement::Frost, true, false);
	const int32 FirstFrameStamps = TickOnceAndReadProcessedStampCount(TestWorld, LiveActor);
	Subsystem->QueueCombatDamage(nullptr, FrostTarget, 30.0f, ETDDamageElement::Frost, true, false);
	const int32 SecondFrameStamps = TickOnceAndReadProcessedStampCount(TestWorld, LiveActor);
	TestEqual(TEXT("The same target hit in two different frames stamps once per frame"), FirstFrameStamps + SecondFrameStamps, 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidCombatQueueMaximumDamageTest,
	"TDGame.FluidFX.CombatQueue.MergedHitsUseMaximumDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidCombatQueueMaximumDamageTest::RunTest(const FString& Parameters)
{
	FTDFluidTestWorldScope TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem))
	{
		return false;
	}

	UTDFluidPresetAsset* BloodPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Blood);
	ATDFluidLiveActor* LiveActor = Subsystem->SpawnPooledFluidActor(BloodPreset, FVector::ZeroVector, 10.0f);
	if (!TestNotNull(TEXT("Spawned Blood LiveActor"), LiveActor))
	{
		return false;
	}

	const FVector MergedLocation(0.0f, 600.0f, 0.0f);
	const FVector HeavyLocation(0.0f, -600.0f, 0.0f);
	const FVector LightLocation(900.0f, 0.0f, 0.0f);
	AActor* MergedTarget = SpawnLocatedTestActor(TestWorld.World, MergedLocation);
	AActor* HeavyTarget = SpawnLocatedTestActor(TestWorld.World, HeavyLocation);
	AActor* LightTarget = SpawnLocatedTestActor(TestWorld.World, LightLocation);
	if (!TestNotNull(TEXT("Spawned merged target"), MergedTarget)
		|| !TestNotNull(TEXT("Spawned heavy target"), HeavyTarget)
		|| !TestNotNull(TEXT("Spawned light target"), LightTarget))
	{
		return false;
	}

	Subsystem->QueueCombatDamage(nullptr, MergedTarget, 5.0f, ETDDamageElement::Physical, false, false);
	Subsystem->QueueCombatDamage(nullptr, MergedTarget, 200.0f, ETDDamageElement::Physical, false, false);
	Subsystem->QueueCombatDamage(nullptr, MergedTarget, 5.0f, ETDDamageElement::Physical, false, false);
	Subsystem->QueueCombatDamage(nullptr, HeavyTarget, 200.0f, ETDDamageElement::Physical, false, false);
	Subsystem->QueueCombatDamage(nullptr, LightTarget, 5.0f, ETDDamageElement::Physical, false, false);

	TestEqual(TEXT("Three targets produce three stamps"), TickOnceAndReadProcessedStampCount(TestWorld, LiveActor), 3);

	const float MergedSpeed = SampleLiveActorAt(LiveActor, MergedLocation).Velocity.X;
	const float HeavySpeed = SampleLiveActorAt(LiveActor, HeavyLocation).Velocity.X;
	const float LightSpeed = SampleLiveActorAt(LiveActor, LightLocation).Velocity.X;
	TestTrue(
		*FString::Printf(TEXT("Merged hits push as hard as a single maximum hit (merged=%.1f heavy=%.1f light=%.1f)"), MergedSpeed, HeavySpeed, LightSpeed),
		FMath::Abs(MergedSpeed - HeavySpeed) < 0.15f * FMath::Abs(HeavySpeed));
	TestTrue(
		*FString::Printf(TEXT("Merged hits push much harder than the weak hit alone (merged=%.1f light=%.1f)"), MergedSpeed, LightSpeed),
		MergedSpeed > 2.0f * LightSpeed);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidCombatQueueDeathTest,
	"TDGame.FluidFX.CombatQueue.DeathStampsOncePerFrameAndStaysSeparateFromDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidCombatQueueDeathTest::RunTest(const FString& Parameters)
{
	FTDFluidTestWorldScope TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem))
	{
		return false;
	}

	UTDFluidPresetAsset* BloodPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Blood);
	ATDFluidLiveActor* LiveActor = Subsystem->SpawnPooledFluidActor(BloodPreset, FVector::ZeroVector, 10.0f);
	if (!TestNotNull(TEXT("Spawned Blood LiveActor"), LiveActor))
	{
		return false;
	}

	const FVector DeathLocation(300.0f, 0.0f, 0.0f);
	AActor* DeadActor = SpawnLocatedTestActor(TestWorld.World, DeathLocation);
	AActor* Killer = SpawnLocatedTestActor(TestWorld.World, FVector(-300.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Spawned dead actor"), DeadActor) || !TestNotNull(TEXT("Spawned killer"), Killer))
	{
		return false;
	}

	Subsystem->QueueCombatDeath(DeadActor, Killer, DeathLocation);
	Subsystem->QueueCombatDeath(DeadActor, Killer, DeathLocation);
	TestEqual(TEXT("Two death notifications for one actor in one frame become one stamp"), TickOnceAndReadProcessedStampCount(TestWorld, LiveActor), 1);

	const FTDFluidCellSample DeathSample = SampleLiveActorAt(LiveActor, DeathLocation);
	TestTrue(TEXT("Death stamp writes primary density"), DeathSample.PrimaryDensity > 0.1f);
	TestTrue(TEXT("Death stamp writes blood selective density"), DeathSample.SelectiveDensity > 0.1f);

	Subsystem->QueueCombatDamage(Killer, DeadActor, 40.0f, ETDDamageElement::Physical, false, false);
	Subsystem->QueueCombatDeath(DeadActor, Killer, DeathLocation);
	Subsystem->QueueCombatDeath(DeadActor, Killer, DeathLocation);
	TestEqual(TEXT("A killing hit queues one damage stamp and one death stamp"), TickOnceAndReadProcessedStampCount(TestWorld, LiveActor), 2);

	Subsystem->QueueCombatDeath(DeadActor, Killer, DeathLocation);
	TestEqual(TEXT("A later frame stamps the death again"), TickOnceAndReadProcessedStampCount(TestWorld, LiveActor), 1);

	TestEqual(TEXT("Empty queue stamps nothing"), TickOnceAndReadProcessedStampCount(TestWorld, LiveActor), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidCombatMessagePathTest,
	"TDGame.FluidFX.CombatQueue.GameplayMessagesReachTheQueue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidCombatMessagePathTest::RunTest(const FString& Parameters)
{
	FTDFluidTestWorldScope TestWorld(true);
	if (!TestTrue(TEXT("Test world owns a GameInstance with a message subsystem"), UGameplayMessageSubsystem::HasInstance(TestWorld.World)))
	{
		return false;
	}

	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem))
	{
		return false;
	}

	UTDFluidPresetAsset* BloodPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Blood);
	ATDFluidLiveActor* LiveActor = Subsystem->SpawnPooledFluidActor(BloodPreset, FVector::ZeroVector, 10.0f);
	if (!TestNotNull(TEXT("Spawned Blood LiveActor"), LiveActor))
	{
		return false;
	}

	const FVector FrostLocation(500.0f, 0.0f, 0.0f);
	const FVector FireLocation(-500.0f, 0.0f, 0.0f);
	const FVector DeathLocation(0.0f, 600.0f, 0.0f);
	AActor* Attacker = SpawnLocatedTestActor(TestWorld.World, FVector(0.0f, -600.0f, 0.0f));
	AActor* FrostTarget = SpawnLocatedTestActor(TestWorld.World, FrostLocation);
	AActor* FireTarget = SpawnLocatedTestActor(TestWorld.World, FireLocation);
	AActor* DeadActor = SpawnLocatedTestActor(TestWorld.World, DeathLocation);
	if (!Attacker || !FrostTarget || !FireTarget || !DeadActor)
	{
		AddError(TEXT("Failed to spawn message test actors"));
		return false;
	}

	UGameplayMessageSubsystem& MessageRouter = UGameplayMessageSubsystem::Get(TestWorld.World);

	FTDDamageAppliedMessage FrostHit;
	FrostHit.Instigator = Attacker;
	FrostHit.Target = FrostTarget;
	FrostHit.DamageAmount = 40.0f;
	FrostHit.Element = ETDDamageElement::Frost;
	MessageRouter.BroadcastMessage(TDGameplayTags::Event_Damage_Applied, FrostHit);
	FrostHit.DamageAmount = 15.0f;
	MessageRouter.BroadcastMessage(TDGameplayTags::Event_Damage_Applied, FrostHit);

	FTDDamageAppliedMessage FireHit;
	FireHit.Instigator = Attacker;
	FireHit.Target = FireTarget;
	FireHit.DamageAmount = 25.0f;
	FireHit.Element = ETDDamageElement::Fire;
	MessageRouter.BroadcastMessage(TDGameplayTags::Event_Damage_Applied, FireHit);

	FTDActorDeathMessage DeathMessage;
	DeathMessage.DeadActor = DeadActor;
	DeathMessage.Killer = Attacker;
	DeathMessage.DeathLocation = DeathLocation;
	MessageRouter.BroadcastMessage(TDGameplayTags::Event_Actor_Death, DeathMessage);
	MessageRouter.BroadcastMessage(TDGameplayTags::Event_Actor_Death, DeathMessage);

	TestEqual(TEXT("Five broadcast messages become two damage stamps and one death stamp"), TickOnceAndReadProcessedStampCount(TestWorld, LiveActor), 3);

	const FTDFluidCellSample FrostSample = SampleLiveActorAt(LiveActor, FrostLocation);
	TestTrue(TEXT("Frost message writes primary density"), FrostSample.PrimaryDensity > 0.1f);
	TestNearlyEqual(TEXT("Frost message maps to the water channel without selective density"), FrostSample.SelectiveDensity, 0.0f, 1e-4f);
	TestTrue(TEXT("Fire message writes selective density"), SampleLiveActorAt(LiveActor, FireLocation).SelectiveDensity > 0.1f);
	TestTrue(TEXT("Death message writes blood selective density"), SampleLiveActorAt(LiveActor, DeathLocation).SelectiveDensity > 0.1f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidObstacleReinitializationTest,
	"TDGame.FluidFX.LiveComponent.ObstaclesSurviveReinitialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidObstacleReinitializationTest::RunTest(const FString& Parameters)
{
	FTDFluidTestWorldScope TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem))
	{
		return false;
	}

	UTDFluidPresetAsset* BloodPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Blood);
	ATDFluidLiveActor* LiveActor = Subsystem->SpawnPooledFluidActor(BloodPreset, FVector::ZeroVector, 10.0f);
	if (!TestNotNull(TEXT("Spawned Blood LiveActor"), LiveActor))
	{
		return false;
	}

	const FVector RockLocation(250.0f, 0.0f, 0.0f);
	FTDFluidSdfObstacle Rock;
	Rock.WorldCenter = RockLocation;
	Rock.RadiusCm = 120.0f;
	TArray<FTDFluidSdfObstacle> Obstacles;
	Obstacles.Add(Rock);
	LiveActor->LiveComponent->SetSdfObstacles(Obstacles);
	TestTrue(TEXT("Obstacle is solid after SetSdfObstacles"), SampleLiveActorAt(LiveActor, RockLocation).ObstacleSdf <= 0.0f);
	TestTrue(TEXT("Open cell next to the obstacle stays open"), SampleLiveActorAt(LiveActor, FVector(-500.0f, 0.0f, 0.0f)).ObstacleSdf > 0.0f);

	UTDFluidPresetAsset* FirePreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Fire);
	LiveActor->LiveComponent->InitSystemStateFromPreset(FirePreset);
	TestTrue(TEXT("Obstacle is still solid after InitSystemStateFromPreset"), SampleLiveActorAt(LiveActor, RockLocation).ObstacleSdf <= 0.0f);

	FTDFluidSimulationConfig ResizedConfig = LiveActor->LiveComponent->SimulationConfig;
	ResizedConfig.GridResolution = 96;
	LiveActor->LiveComponent->ApplySimulationConfig(ResizedConfig);
	TestEqual(TEXT("Grid resolution changed"), LiveActor->LiveComponent->GetSolver().GetResolution(), 96);
	TestTrue(TEXT("Obstacle is still solid after a resolution change"), SampleLiveActorAt(LiveActor, RockLocation).ObstacleSdf <= 0.0f);

	LiveActor->LiveComponent->ResetSimulationState();
	TestTrue(TEXT("Obstacle is still solid after ResetSimulationState"), SampleLiveActorAt(LiveActor, RockLocation).ObstacleSdf <= 0.0f);

	TestWorld.TickWorld(0.1f, 0.05f);
	TestTrue(TEXT("Obstacle is still solid after stepping"), SampleLiveActorAt(LiveActor, RockLocation).ObstacleSdf <= 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPooledReuseWithoutPresetTest,
	"TDGame.FluidFX.Subsystem.PooledReuseWithoutPresetMovesSolverCenter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPooledReuseWithoutPresetTest::RunTest(const FString& Parameters)
{
	FTDFluidTestWorldScope TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem))
	{
		return false;
	}

	UTDFluidPresetAsset* BloodPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Blood);
	ATDFluidLiveActor* FirstSpawn = Subsystem->SpawnPooledFluidActor(BloodPreset, FVector::ZeroVector, 0.1f);
	if (!TestNotNull(TEXT("Spawned Blood LiveActor"), FirstSpawn))
	{
		return false;
	}

	TestWorld.TickWorld(0.3f, 0.05f);
	TestEqual(TEXT("Expired actor returned to the pool"), Subsystem->GetPooledInactiveActorCount(), 1);

	const FVector ReuseLocation(3000.0f, 0.0f, 0.0f);
	ATDFluidLiveActor* SecondSpawn = Subsystem->SpawnPooledFluidActor(nullptr, ReuseLocation, 5.0f);
	TestTrue(TEXT("Pooled actor is reused"), SecondSpawn == FirstSpawn);
	if (!SecondSpawn)
	{
		return false;
	}

	TestTrue(
		TEXT("Solver center follows the reused actor even without a preset"),
		SecondSpawn->LiveComponent->GetSolver().GetCenterWorld().Equals(FVector2D(ReuseLocation.X, ReuseLocation.Y), 1.0f));

	FTDFluidBrushStamp ReuseStamp;
	ReuseStamp.WorldLocation = ReuseLocation;
	ReuseStamp.RadiusCm = 150.0f;
	ReuseStamp.DensityStrength = 1.0f;
	SecondSpawn->LiveComponent->QueueBrushStamp(ReuseStamp);
	TestWorld.TickWorld(0.05f, 0.05f);
	TestTrue(TEXT("A stamp at the new location lands in the grid"), SampleLiveActorAt(SecondSpawn, ReuseLocation).PrimaryDensity > 0.1f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidParticleSplashRoutingTest,
	"TDGame.FluidFX.Subsystem.ParticleSplashCountsOnlyActorsThatReceiveParticles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidParticleSplashRoutingTest::RunTest(const FString& Parameters)
{
	FTDFluidTestWorldScope TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem))
	{
		return false;
	}

	UTDFluidPresetAsset* BloodPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Blood);
	ATDFluidLiveActor* NearActor = Subsystem->SpawnPooledFluidActor(BloodPreset, FVector::ZeroVector, 10.0f);
	ATDFluidLiveActor* FarActor = Subsystem->SpawnPooledFluidActor(BloodPreset, FVector(20000.0f, 0.0f, 0.0f), 10.0f);
	if (!TestNotNull(TEXT("Spawned near actor"), NearActor) || !TestNotNull(TEXT("Spawned far actor"), FarActor))
	{
		return false;
	}

	TArray<FVector> OutsidePositions;
	OutsidePositions.Add(FVector(5000.0f, 0.0f, 0.0f));
	OutsidePositions.Add(FVector(0.0f, -5000.0f, 0.0f));
	TArray<FVector> OutsideVelocities;
	OutsideVelocities.Add(FVector(0.0f, 100.0f, 0.0f));
	OutsideVelocities.Add(FVector(0.0f, 100.0f, 0.0f));
	TestEqual(
		TEXT("Particles outside every grid reach no actor"),
		Subsystem->InjectCombatParticleSplash(OutsidePositions, OutsideVelocities, 40.0f, ETDFluidBrushChannel::ChannelG_Blood),
		0);
	TestWorld.TickWorld(0.05f, 0.05f);
	TestEqual(TEXT("Near actor received no stamps from outside particles"), NearActor->LiveComponent->GetLastProcessedPointCount(), 0);
	TestEqual(TEXT("Far actor received no stamps from outside particles"), FarActor->LiveComponent->GetLastProcessedPointCount(), 0);

	const double QuietNaN = std::numeric_limits<double>::quiet_NaN();
	TArray<FVector> MixedPositions;
	MixedPositions.Add(FVector(0.0f, 0.0f, 0.0f));
	MixedPositions.Add(FVector(100.0f, 0.0f, 0.0f));
	MixedPositions.Add(FVector(0.0f, 100.0f, 0.0f));
	MixedPositions.Add(FVector(5000.0f, 0.0f, 0.0f));
	MixedPositions.Add(FVector(QuietNaN, 0.0, 0.0));
	TArray<FVector> MixedVelocities;
	for (int32 Index = 0; Index < MixedPositions.Num(); ++Index)
	{
		MixedVelocities.Add(FVector(0.0f, 100.0f, 0.0f));
	}

	TestEqual(
		TEXT("Only the actor whose grid contains particles counts as a receiver"),
		Subsystem->InjectCombatParticleSplash(MixedPositions, MixedVelocities, 40.0f, ETDFluidBrushChannel::ChannelG_Blood),
		1);
	TestWorld.TickWorld(0.05f, 0.05f);
	TestEqual(TEXT("Near actor received only the finite in-range particles"), NearActor->LiveComponent->GetLastProcessedPointCount(), 3);
	TestEqual(TEXT("Far actor received nothing"), FarActor->LiveComponent->GetLastProcessedPointCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPackedPixelOrientationTest,
	"TDGame.FluidFX.LiveComponent.PackedPixelsFollowWorldAxes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPackedPixelOrientationTest::RunTest(const FString& Parameters)
{
	UTDFluidLiveComponent* LiveComponent = NewObject<UTDFluidLiveComponent>(GetTransientPackage());
	LiveComponent->SimulationConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Mud);
	LiveComponent->SimulationConfig.SimExtentCm = FVector2D(3200.0f, 3200.0f);
	LiveComponent->SimulationConfig.GridResolution = 64;

	const int32 Resolution = LiveComponent->SimulationConfig.GridResolution;
	const float CellSizeCm = LiveComponent->SimulationConfig.SimExtentCm.X / static_cast<float>(Resolution);

	struct FStampTrial
	{
		FVector WorldLocation;
		bool bExpectPositiveQuadrantOnly;
	};
	const FStampTrial Trials[] = {
		{ FVector(600.0f, 300.0f, 0.0f), true },
		{ FVector(-600.0f, -300.0f, 0.0f), false },
		{ FVector(-600.0f, 300.0f, 0.0f), false }
	};

	for (const FStampTrial& Trial : Trials)
	{
		LiveComponent->ResetSimulationState();
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = Trial.WorldLocation;
		Stamp.RadiusCm = 150.0f;
		Stamp.DensityStrength = 1.0f;
		Stamp.VelocityStrength = 0.0f;
		Stamp.Channel = ETDFluidBrushChannel::ChannelR_Mud;
		LiveComponent->QueueBrushStamp(Stamp);
		LiveComponent->AdvanceSimulationStep(0.05f, FVector2D::ZeroVector, false);

		const TArray<FFloat16Color>& Pixels = LiveComponent->GetPackedDensityPressurePixels();
		if (!TestEqual(TEXT("Packed pixel count matches the grid"), Pixels.Num(), Resolution * Resolution))
		{
			return false;
		}

		double WeightSum = 0.0;
		double WeightedColumn = 0.0;
		double WeightedRow = 0.0;
		int32 OutsidePositiveQuadrantCount = 0;
		for (int32 Row = 0; Row < Resolution; ++Row)
		{
			for (int32 Column = 0; Column < Resolution; ++Column)
			{
				const float Density = Pixels[Row * Resolution + Column].R.GetFloat();
				WeightSum += Density;
				WeightedColumn += static_cast<double>(Density) * (Column + 0.5);
				WeightedRow += static_cast<double>(Density) * (Row + 0.5);
				if (Density > 0.0f && (Column < Resolution / 2 || Row < Resolution / 2))
				{
					++OutsidePositiveQuadrantCount;
				}
			}
		}

		if (!TestTrue(TEXT("Stamp wrote visible density into the packed pixels"), WeightSum > 1.0))
		{
			return false;
		}

		const double ExpectedColumn = (Trial.WorldLocation.X + LiveComponent->SimulationConfig.SimExtentCm.X * 0.5) / CellSizeCm;
		const double ExpectedRow = (Trial.WorldLocation.Y + LiveComponent->SimulationConfig.SimExtentCm.Y * 0.5) / CellSizeCm;
		TestTrue(
			*FString::Printf(TEXT("Density column grows with world X for stamp at %s"), *Trial.WorldLocation.ToString()),
			FMath::Abs(WeightedColumn / WeightSum - ExpectedColumn) < 1.5);
		TestTrue(
			*FString::Printf(TEXT("Density row grows with world Y for stamp at %s"), *Trial.WorldLocation.ToString()),
			FMath::Abs(WeightedRow / WeightSum - ExpectedRow) < 1.5);
		if (Trial.bExpectPositiveQuadrantOnly)
		{
			TestEqual(TEXT("A +X +Y stamp only lights pixels in the upper column and upper row half"), OutsidePositiveQuadrantCount, 0);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSurfaceMeshAssignmentTest,
	"TDGame.FluidFX.LiveComponent.PresetSurfaceMeshLoadsAsyncAndRebinds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSurfaceMeshAssignmentTest::RunTest(const FString& Parameters)
{
	FTDFluidTestWorldScope TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem))
	{
		return false;
	}

	UTDFluidPresetAsset* BloodPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Blood);
	BloodPreset->SurfaceMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Plane.Plane")));
	ATDFluidLiveActor* LiveActor = Subsystem->SpawnPooledFluidActor(BloodPreset, FVector::ZeroVector, 30.0f);
	if (!TestNotNull(TEXT("Spawned Blood LiveActor"), LiveActor))
	{
		return false;
	}

	for (int32 Attempt = 0; Attempt < 200 && !LiveActor->InternalSurfaceMesh->GetStaticMesh(); ++Attempt)
	{
		FlushAsyncLoading();
		TestWorld.TickWorld(0.05f, 0.05f);
	}
	TestTrue(TEXT("Preset SurfaceMesh is assigned to the internal mesh component after the async load"), LiveActor->InternalSurfaceMesh->GetStaticMesh() != nullptr);

	LiveActor->InternalSurfaceMesh->SetStaticMesh(nullptr);
	LiveActor->LiveComponent->BindInternalMeshComponent(LiveActor->InternalSurfaceMesh);
	TestTrue(TEXT("Binding after the load assigns the already loaded mesh immediately"), LiveActor->InternalSurfaceMesh->GetStaticMesh() != nullptr);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidLiveActorIgnoresFluidActorsTest,
	"TDGame.FluidFX.LiveActor.OverlappingFluidActorsDoNotStampEachOther",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidLiveActorIgnoresFluidActorsTest::RunTest(const FString& Parameters)
{
	FTDFluidTestWorldScope TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem))
	{
		return false;
	}

	UTDFluidPresetAsset* BloodPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Blood);
	ATDFluidLiveActor* MovingFluid = Subsystem->SpawnPooledFluidActor(BloodPreset, FVector::ZeroVector, 60.0f);
	ATDFluidLiveActor* WatchingFluid = Subsystem->SpawnPooledFluidActor(BloodPreset, FVector(100.0f, 0.0f, 0.0f), 60.0f);
	if (!TestNotNull(TEXT("Spawned moving fluid actor"), MovingFluid) || !TestNotNull(TEXT("Spawned watching fluid actor"), WatchingFluid))
	{
		return false;
	}

	if (!TestTrue(TEXT("The interaction volumes of the two fluid actors overlap"), WatchingFluid->InteractionVolume->IsOverlappingActor(MovingFluid)))
	{
		return false;
	}

	TickLiveActorSteps(TestWorld, 2);
	for (int32 Step = 0; Step < 6; ++Step)
	{
		MovingFluid->AddActorWorldOffset(FVector(30.0f, 0.0f, 0.0f));
		TickLiveActorSteps(TestWorld, 1);
	}

	TestNearlyEqual(TEXT("A fluid actor moving through another volume leaves its solver at rest"), MeasureLiveActorMaxSpeed(WatchingFluid), 0.0f, 1e-4f);
	TestNearlyEqual(TEXT("The moving fluid actor does not stamp itself"), MeasureLiveActorMaxSpeed(MovingFluid), 0.0f, 1e-4f);

	AActor* WalkingProbe = SpawnOverlapProbeActor(TestWorld.World, FVector(-200.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Spawned walking probe"), WalkingProbe))
	{
		return false;
	}

	TickLiveActorSteps(TestWorld, 1);
	for (int32 Step = 0; Step < 4; ++Step)
	{
		WalkingProbe->AddActorWorldOffset(FVector(30.0f, 0.0f, 0.0f));
		TickLiveActorSteps(TestWorld, 1);
	}
	TestTrue(TEXT("An ordinary actor walking through the same volume still stamps"), MeasureLiveActorMaxSpeed(WatchingFluid) > 10.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidLiveActorReentryTest,
	"TDGame.FluidFX.LiveActor.ReenteringActorDoesNotStampStaleDisplacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidLiveActorReentryTest::RunTest(const FString& Parameters)
{
	FTDFluidTestWorldScope TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem))
	{
		return false;
	}

	UTDFluidPresetAsset* BloodPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Blood);
	ATDFluidLiveActor* LiveActor = Subsystem->SpawnPooledFluidActor(BloodPreset, FVector::ZeroVector, 60.0f);
	AActor* Probe = SpawnOverlapProbeActor(TestWorld.World, FVector(-900.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Spawned fluid actor"), LiveActor) || !TestNotNull(TEXT("Spawned probe"), Probe))
	{
		return false;
	}

	if (!TestTrue(TEXT("Probe starts inside the interaction volume"), LiveActor->InteractionVolume->IsOverlappingActor(Probe)))
	{
		return false;
	}
	TickLiveActorSteps(TestWorld, 2);

	Probe->SetActorLocation(FVector(3000.0f, 0.0f, 0.0f));
	if (!TestFalse(TEXT("Probe leaves the interaction volume"), LiveActor->InteractionVolume->IsOverlappingActor(Probe)))
	{
		return false;
	}
	TickLiveActorSteps(TestWorld, 3);

	Probe->SetActorLocation(FVector(900.0f, 0.0f, 0.0f));
	if (!TestTrue(TEXT("Probe re-enters the interaction volume"), LiveActor->InteractionVolume->IsOverlappingActor(Probe)))
	{
		return false;
	}
	TickLiveActorSteps(TestWorld, 1);
	TestNearlyEqual(TEXT("The re-entry step stamps nothing from the stale position"), MeasureLiveActorMaxSpeed(LiveActor), 0.0f, 1e-3f);

	constexpr float WalkSpeedCmPerSec = 400.0f;
	for (int32 Step = 0; Step < 4; ++Step)
	{
		Probe->AddActorWorldOffset(FVector(WalkSpeedCmPerSec * 0.05f, 0.0f, 0.0f));
		TickLiveActorSteps(TestWorld, 1);
	}

	const float WalkMaxSpeed = MeasureLiveActorMaxSpeed(LiveActor);
	TestTrue(*FString::Printf(TEXT("Stamping resumes after re-entry (max speed %.1f)"), WalkMaxSpeed), WalkMaxSpeed > 10.0f);
	TestTrue(*FString::Printf(TEXT("Fluid speed stays within 1.5x of the real walking speed (max speed %.1f)"), WalkMaxSpeed), WalkMaxSpeed <= WalkSpeedCmPerSec * 1.5f);
	TestTrue(TEXT("Fluid speed never exceeds the cell speed cap"), WalkMaxSpeed <= 1200.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidLiveActorTeleportTest,
	"TDGame.FluidFX.LiveActor.TeleportedRegisteredActorKeepsPositionWithoutStamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidLiveActorTeleportTest::RunTest(const FString& Parameters)
{
	FTDFluidTestWorldScope TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem))
	{
		return false;
	}

	UTDFluidPresetAsset* BloodPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Blood);
	ATDFluidLiveActor* LiveActor = Subsystem->SpawnPooledFluidActor(BloodPreset, FVector::ZeroVector, 60.0f);
	AActor* Probe = SpawnOverlapProbeActor(TestWorld.World, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Spawned fluid actor"), LiveActor) || !TestNotNull(TEXT("Spawned probe"), Probe))
	{
		return false;
	}

	LiveActor->RegisterInteractingActor(Probe);
	TickLiveActorSteps(TestWorld, 2);

	Probe->SetActorLocation(FVector(600.0f, 0.0f, 0.0f));
	TickLiveActorSteps(TestWorld, 1);
	TestNearlyEqual(TEXT("A 12000 cm/s jump counts as a teleport and stamps nothing"), MeasureLiveActorMaxSpeed(LiveActor), 0.0f, 1e-3f);

	for (int32 Step = 0; Step < 3; ++Step)
	{
		Probe->AddActorWorldOffset(FVector(20.0f, 0.0f, 0.0f));
		TickLiveActorSteps(TestWorld, 1);
	}

	const float WalkMaxSpeed = MeasureLiveActorMaxSpeed(LiveActor);
	TestTrue(*FString::Printf(TEXT("Walking after the teleport stamps from the updated position (max speed %.1f)"), WalkMaxSpeed), WalkMaxSpeed > 10.0f);
	TestTrue(*FString::Printf(TEXT("Walking stamp speed stays within 1.5x of the real speed (max speed %.1f)"), WalkMaxSpeed), WalkMaxSpeed <= 600.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidLiveActorCombatBindingTest,
	"TDGame.FluidFX.LiveActor.CombatBindingStampsOncePerHitAndReleasesOnDestroy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidLiveActorCombatBindingTest::RunTest(const FString& Parameters)
{
	FTDFluidTestWorldScope TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem))
	{
		return false;
	}

	UTDFluidPresetAsset* BloodPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Blood);
	ATDFluidLiveActor* LiveActor = Subsystem->SpawnPooledFluidActor(BloodPreset, FVector::ZeroVector, 60.0f);
	const FVector TargetLocation(150.0f, 0.0f, 0.0f);
	AActor* TargetActor = SpawnOverlapProbeActor(TestWorld.World, TargetLocation);
	AActor* Attacker = SpawnLocatedTestActor(TestWorld.World, FVector(-200.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Spawned fluid actor"), LiveActor) || !TestNotNull(TEXT("Spawned target"), TargetActor) || !TestNotNull(TEXT("Spawned attacker"), Attacker))
	{
		return false;
	}

	UTDCombatComponent* Combatant = AddLiveActorCombatant(TargetActor);
	LiveActor->RegisterInteractingActor(TargetActor);
	TickLiveActorSteps(TestWorld, 4);

	TestEqual(TEXT("Four collection steps leave exactly one binding entry for the combat component"), LiveActor->GetBoundCombatComponentCount(), 1);
	TestTrue(TEXT("The damage delegate is bound"), Combatant->OnDamaged.IsBound());
	TestTrue(TEXT("The death delegate is bound"), Combatant->OnDeath.IsBound());
	TestNearlyEqual(TEXT("No selective density before the hit"), SampleLiveActorAt(LiveActor, TargetLocation).SelectiveDensity, 0.0f, 1e-4f);

	FTDDamageContext HitContext;
	HitContext.Caster = Attacker;
	HitContext.CastTarget = TargetLocation;
	HitContext.Direction = FVector(1.0f, 0.0f, 0.0f);
	HitContext.Budget = MakeShared<FTDDamageChainBudget>();
	const FTDDamageResult HitResult = Combatant->ReceiveDamage(80.0f, ETDDamageElement::Physical, false, HitContext);
	TestTrue(TEXT("The combatant took damage"), HitResult.AppliedDamage > 0.0f);

	TestEqual(TEXT("One hit becomes exactly one stamp"), TickOnceAndReadProcessedStampCount(TestWorld, LiveActor), 1);
	const float SelectiveAfterHit = SampleLiveActorAt(LiveActor, TargetLocation).SelectiveDensity;
	TestTrue(*FString::Printf(TEXT("The hit writes selective density at the target (%.3f)"), SelectiveAfterHit), SelectiveAfterHit > 0.1f);

	TestEqual(TEXT("A tick without a new hit stamps nothing"), TickOnceAndReadProcessedStampCount(TestWorld, LiveActor), 0);
	TestTrue(TEXT("Selective density does not grow without a new hit"), SampleLiveActorAt(LiveActor, TargetLocation).SelectiveDensity <= SelectiveAfterHit + 1e-3f);

	LiveActor->Destroy();
	TestFalse(TEXT("Destroying the fluid actor leaves no damage delegate behind (detects a leftover duplicate binding)"), Combatant->OnDamaged.IsBound());
	TestFalse(TEXT("Destroying the fluid actor leaves no death delegate behind (detects a leftover duplicate binding)"), Combatant->OnDeath.IsBound());

	Combatant->ReceiveDamage(80.0f, ETDDamageElement::Physical, false, HitContext);
	TickLiveActorSteps(TestWorld, 2);
	TestEqual(TEXT("No fluid actor remains to stamp after the destroy"), Subsystem->GetActiveActorCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidLiveActorPlaneScaleTest,
	"TDGame.FluidFX.LiveActor.PlaneScaleFollowsClampedSolverExtent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidLiveActorPlaneScaleTest::RunTest(const FString& Parameters)
{
	FTDFluidTestWorldScope TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem))
	{
		return false;
	}

	UTDFluidPresetAsset* BloodPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Blood);
	ATDFluidLiveActor* LiveActor = Subsystem->SpawnPooledFluidActor(BloodPreset, FVector::ZeroVector, 60.0f);
	if (!TestNotNull(TEXT("Spawned fluid actor"), LiveActor))
	{
		return false;
	}

	FTDFluidSimulationConfig TinyConfig = LiveActor->LiveComponent->SimulationConfig;
	TinyConfig.SimExtentCm = FVector2D(50.0f, 50.0f);
	LiveActor->LiveComponent->ApplySimulationConfig(TinyConfig);
	TickLiveActorSteps(TestWorld, 1);
	TestTrue(TEXT("The solver clamps a 50 cm request to 100 cm"), LiveActor->LiveComponent->GetSolver().GetExtentCm().Equals(FVector2D(100.0f, 100.0f), 0.01f));
	TestTrue(
		*FString::Printf(TEXT("A clamped 100 cm extent gives plane scale 1 (%s)"), *LiveActor->InternalSurfaceMesh->GetRelativeScale3D().ToString()),
		LiveActor->InternalSurfaceMesh->GetRelativeScale3D().Equals(FVector(1.0f, 1.0f, 1.0f), 0.001f));
	TestTrue(
		TEXT("The interaction volume follows the clamped extent"),
		LiveActor->InteractionVolume->GetUnscaledBoxExtent().Equals(FVector(50.0f, 50.0f, 300.0f), 0.01f));

	FTDFluidSimulationConfig WideConfig = LiveActor->LiveComponent->SimulationConfig;
	WideConfig.SimExtentCm = FVector2D(5000.0f, 3000.0f);
	LiveActor->LiveComponent->ApplySimulationConfig(WideConfig);
	TickLiveActorSteps(TestWorld, 1);
	TestTrue(TEXT("A larger extent scales the plane per axis"), LiveActor->InternalSurfaceMesh->GetRelativeScale3D().Equals(FVector(50.0f, 30.0f, 1.0f), 0.001f));

	UTDFluidPresetAsset* TinyPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Custom);
	TinyPreset->bOverridePresetDefaults = true;
	TinyPreset->SimulationConfig.SimExtentCm = FVector2D(50.0f, 50.0f);
	LiveActor->InitializeFromPreset(TinyPreset, 60.0f);
	TestTrue(
		*FString::Printf(TEXT("Preset initialization rescales the plane to the clamped extent (%s)"), *LiveActor->InternalSurfaceMesh->GetRelativeScale3D().ToString()),
		LiveActor->InternalSurfaceMesh->GetRelativeScale3D().Equals(FVector(1.0f, 1.0f, 1.0f), 0.001f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidLiveActorSleepWakeHysteresisTest,
	"TDGame.FluidFX.LiveActor.SleepWakeHysteresisAndWakeClearsHistory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidLiveActorSleepWakeHysteresisTest::RunTest(const FString& Parameters)
{
	FTDFluidTestWorldScope TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem))
	{
		return false;
	}

	UTDFluidPresetAsset* BloodPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Blood);
	ATDFluidLiveActor* LiveActor = Subsystem->SpawnPooledFluidActor(BloodPreset, FVector::ZeroVector, 60.0f);
	if (!TestNotNull(TEXT("Spawned fluid actor"), LiveActor))
	{
		return false;
	}

	LiveActor->bShouldUseActivationVolume = true;
	LiveActor->ActivationDistanceCm = 1000.0f;

	LiveActor->EvaluateActivationProximity(FVector(1050.0f, 0.0f, 0.0f));
	TestTrue(TEXT("An awake actor stays awake at 1.05x the activation distance"), LiveActor->LiveComponent->IsSimulationAwake());
	LiveActor->EvaluateActivationProximity(FVector(1150.0f, 0.0f, 0.0f));
	TestFalse(TEXT("An awake actor sleeps beyond 1.1x the activation distance"), LiveActor->LiveComponent->IsSimulationAwake());
	LiveActor->EvaluateActivationProximity(FVector(1050.0f, 0.0f, 0.0f));
	TestFalse(TEXT("A sleeping actor stays asleep at 1.05x the activation distance"), LiveActor->LiveComponent->IsSimulationAwake());
	LiveActor->EvaluateActivationProximity(FVector(1000.0f, 0.0f, 0.0f));
	TestTrue(TEXT("A sleeping actor wakes at the activation distance"), LiveActor->LiveComponent->IsSimulationAwake());
	LiveActor->EvaluateActivationProximity(FVector(1099.0f, 0.0f, 0.0f));
	TestTrue(TEXT("A freshly woken actor stays awake just inside 1.1x"), LiveActor->LiveComponent->IsSimulationAwake());

	AActor* Probe = SpawnOverlapProbeActor(TestWorld.World, FVector(0.0f, 0.0f, 2000.0f));
	if (!TestNotNull(TEXT("Spawned probe above the interaction volume"), Probe))
	{
		return false;
	}

	LiveActor->RegisterInteractingActor(Probe);
	TickLiveActorSteps(TestWorld, 2);

	LiveActor->EvaluateActivationProximity(FVector(3000.0f, 0.0f, 0.0f));
	TestFalse(TEXT("Actor sleeps when the observer is far away"), LiveActor->LiveComponent->IsSimulationAwake());
	Probe->AddActorWorldOffset(FVector(100.0f, 0.0f, 0.0f));
	TickLiveActorSteps(TestWorld, 1);

	LiveActor->EvaluateActivationProximity(FVector::ZeroVector);
	TestTrue(TEXT("Actor wakes when the observer returns"), LiveActor->LiveComponent->IsSimulationAwake());
	TickLiveActorSteps(TestWorld, 1);
	TestNearlyEqual(TEXT("Waking clears the movement history so the sleep displacement is not stamped"), MeasureLiveActorMaxSpeed(LiveActor), 0.0f, 1e-3f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidLiveActorPoolLifecycleTest,
	"TDGame.FluidFX.LiveActor.PoolReturnClearsOutputAndReuseClearsState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidLiveActorPoolLifecycleTest::RunTest(const FString& Parameters)
{
	FTDFluidTestWorldScope TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem))
	{
		return false;
	}

	UTDFluidPresetAsset* BloodPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Blood);
	ATDFluidLiveActor* LiveActor = Subsystem->SpawnPooledFluidActor(BloodPreset, FVector::ZeroVector, 60.0f);
	if (!TestNotNull(TEXT("Spawned fluid actor"), LiveActor))
	{
		return false;
	}

	const FVector RockLocation(250.0f, 0.0f, 0.0f);
	FTDFluidSdfObstacle Rock;
	Rock.WorldCenter = RockLocation;
	Rock.RadiusCm = 120.0f;
	LiveActor->LiveComponent->SetSdfObstacles({ Rock });
	TestTrue(TEXT("Obstacle is solid before the pool return"), SampleLiveActorAt(LiveActor, RockLocation).ObstacleSdf <= 0.0f);

	const FVector RegisteredLocation(100.0f, 0.0f, 2000.0f);
	AActor* RegisteredActor = SpawnOverlapProbeActor(TestWorld.World, RegisteredLocation);
	if (!TestNotNull(TEXT("Spawned registered actor above the interaction volume"), RegisteredActor))
	{
		return false;
	}
	UTDCombatComponent* Combatant = AddLiveActorCombatant(RegisteredActor);
	LiveActor->RegisterInteractingActor(RegisteredActor);

	FTDFluidBrushStamp BloodStamp;
	BloodStamp.WorldLocation = FVector::ZeroVector;
	BloodStamp.RadiusCm = 200.0f;
	BloodStamp.DensityStrength = 1.0f;
	BloodStamp.Channel = ETDFluidBrushChannel::ChannelG_Blood;
	LiveActor->LiveComponent->QueueBrushStamp(BloodStamp);
	TickLiveActorSteps(TestWorld, 2);
	TestTrue(TEXT("Packed pixels hold primary and selective density before the return"), HasAnyPackedDensity(LiveActor->LiveComponent->GetPackedDensityPressurePixels()));
	TestTrue(TEXT("The registered combatant is bound"), Combatant->OnDamaged.IsBound());

	Subsystem->ReleaseFluidActorToPool(LiveActor);
	TestTrue(TEXT("Packed pixels are cleared at the pool return so no afterimage is published"), AreAllPackedChannelsZero(LiveActor->LiveComponent->GetPackedDensityPressurePixels()));
	TestFalse(TEXT("The pool return removes the combat damage delegate"), Combatant->OnDamaged.IsBound());
	TestFalse(TEXT("The pool return removes the combat death delegate"), Combatant->OnDeath.IsBound());
	TestEqual(TEXT("The pool return empties the binding table"), LiveActor->GetBoundCombatComponentCount(), 0);

	ATDFluidLiveActor* ReusedActor = Subsystem->SpawnPooledFluidActor(nullptr, FVector(800.0f, 0.0f, 0.0f), 60.0f);
	TestTrue(TEXT("The pooled actor is reused"), ReusedActor == LiveActor);
	if (!ReusedActor)
	{
		return false;
	}

	FTDFluidCellSample RockSample;
	TestTrue(TEXT("The old rock position lies inside the moved grid"), ReusedActor->LiveComponent->SampleFluidAtWorldLocation(RockLocation, RockSample));
	TestTrue(TEXT("Reuse removes the previous obstacles"), RockSample.ObstacleSdf > 0.0f);

	TickLiveActorSteps(TestWorld, 1);
	RegisteredActor->AddActorWorldOffset(FVector(100.0f, 0.0f, 0.0f));
	TickLiveActorSteps(TestWorld, 1);
	TestNearlyEqual(TEXT("Reuse forgets the previously registered actor"), MeasureLiveActorMaxSpeed(ReusedActor), 0.0f, 1e-3f);
	TestFalse(TEXT("Reuse does not rebind the forgotten combatant"), Combatant->OnDamaged.IsBound());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidLiveActorTerrainSlopeTest,
	"TDGame.FluidFX.LiveActor.TerrainElevationDrivesDownhillFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidLiveActorTerrainSlopeTest::RunTest(const FString& Parameters)
{
	FTDFluidTestWorldScope TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem) || !TestNotNull(TEXT("Engine cube mesh loads"), CubeMesh))
	{
		return false;
	}

	constexpr float RampPitchDegrees = 10.0f;
	AActor* Ramp = SpawnStaticTerrainBlock(TestWorld.World, CubeMesh, FVector(0.0f, 0.0f, -100.0f), FRotator(RampPitchDegrees, 0.0f, 0.0f), FVector(80.0f, 80.0f, 1.0f));
	if (!TestNotNull(TEXT("Spawned ramp"), Ramp))
	{
		return false;
	}
	TickLiveActorSteps(TestWorld, 2);

	FHitResult ProbeHit;
	const bool bProbeHit = TestWorld.World->LineTraceSingleByObjectType(
		ProbeHit,
		FVector(0.0f, 0.0f, 2000.0f),
		FVector(0.0f, 0.0f, -4000.0f),
		FCollisionObjectQueryParams(ECC_WorldStatic));
	if (!TestTrue(TEXT("The test world answers an object trace against the ramp"), bProbeHit))
	{
		return false;
	}

	UTDFluidPresetAsset* WaterPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Water);
	ATDFluidLiveActor* TerrainFluid = Subsystem->SpawnPooledFluidActor(WaterPreset, FVector::ZeroVector, 120.0f);
	ATDFluidLiveActor* FlatFluid = Subsystem->SpawnPooledFluidActor(WaterPreset, FVector::ZeroVector, 120.0f);
	if (!TestNotNull(TEXT("Spawned terrain fluid actor"), TerrainFluid) || !TestNotNull(TEXT("Spawned flat control fluid actor"), FlatFluid))
	{
		return false;
	}
	TerrainFluid->bSampleTerrainElevation = true;

	TickLiveActorSteps(TestWorld, 12);

	const float ExpectedSlope = FMath::Tan(FMath::DegreesToRadians(RampPitchDegrees));
	const FTDFluidCellSample CenterSample = SampleLiveActorAt(TerrainFluid, FVector::ZeroVector);
	const FTDFluidCellSample CornerSample = SampleLiveActorAt(TerrainFluid, FVector(-1400.0f, 1200.0f, 0.0f));
	TestTrue(*FString::Printf(TEXT("Slope at the center follows the ramp pitch (%.3f vs %.3f)"), CenterSample.TerrainSlope.X, ExpectedSlope), FMath::Abs(CenterSample.TerrainSlope.X - ExpectedSlope) < 0.02f);
	TestTrue(*FString::Printf(TEXT("Slope across the ramp is flat (%.3f)"), CenterSample.TerrainSlope.Y), FMath::Abs(CenterSample.TerrainSlope.Y) < 0.02f);
	TestTrue(*FString::Printf(TEXT("Slope away from the center follows the ramp too (%.3f)"), CornerSample.TerrainSlope.X), FMath::Abs(CornerSample.TerrainSlope.X - ExpectedSlope) < 0.02f);
	TestNearlyEqual(TEXT("The control actor without terrain sampling stays flat"), MeasureLiveActorMaxTerrainSlope(FlatFluid), 0.0f, 1e-4f);

	FTDFluidBrushStamp Puddle;
	Puddle.WorldLocation = FVector::ZeroVector;
	Puddle.RadiusCm = 250.0f;
	Puddle.DensityStrength = 1.0f;
	Puddle.VelocityStrength = 0.0f;
	Puddle.Channel = ETDFluidBrushChannel::ChannelA_Water;
	TerrainFluid->LiveComponent->QueueBrushStamp(Puddle);
	FlatFluid->LiveComponent->QueueBrushStamp(Puddle);
	TickLiveActorSteps(TestWorld, 1);

	const FVector2D TerrainStart = MeasureLiveActorDensityCentroid(TerrainFluid);
	const FVector2D FlatStart = MeasureLiveActorDensityCentroid(FlatFluid);
	TickLiveActorSteps(TestWorld, 30);
	const FVector2D TerrainEnd = MeasureLiveActorDensityCentroid(TerrainFluid);
	const FVector2D FlatEnd = MeasureLiveActorDensityCentroid(FlatFluid);

	AddInfo(FString::Printf(TEXT("Centroid terrain (%.2f, %.2f) -> (%.2f, %.2f), flat (%.2f, %.2f) -> (%.2f, %.2f)"), TerrainStart.X, TerrainStart.Y, TerrainEnd.X, TerrainEnd.Y, FlatStart.X, FlatStart.Y, FlatEnd.X, FlatEnd.Y));
	TestTrue(*FString::Printf(TEXT("The puddle drifts toward the downhill side (-X) by more than 20 cm (%.2f)"), TerrainEnd.X - TerrainStart.X), TerrainEnd.X - TerrainStart.X < -20.0f);
	TestTrue(*FString::Printf(TEXT("The puddle does not drift sideways (%.2f)"), TerrainEnd.Y - TerrainStart.Y), FMath::Abs(TerrainEnd.Y - TerrainStart.Y) < 0.5f);
	TestTrue(*FString::Printf(TEXT("The flat control puddle stays put (%.2f)"), FlatEnd.X - FlatStart.X), FMath::Abs(FlatEnd.X - FlatStart.X) < 0.5f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidLiveActorTerrainBudgetTest,
	"TDGame.FluidFX.LiveActor.TerrainSamplingContinuesAcrossTicksWithinBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidLiveActorTerrainBudgetTest::RunTest(const FString& Parameters)
{
	FTDFluidTestWorldScope TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem) || !TestNotNull(TEXT("Engine cube mesh loads"), CubeMesh))
	{
		return false;
	}

	AActor* Ramp = SpawnStaticTerrainBlock(TestWorld.World, CubeMesh, FVector(0.0f, 0.0f, -100.0f), FRotator(10.0f, 0.0f, 0.0f), FVector(80.0f, 80.0f, 1.0f));
	if (!TestNotNull(TEXT("Spawned ramp"), Ramp))
	{
		return false;
	}
	TickLiveActorSteps(TestWorld, 2);

	UTDFluidPresetAsset* WaterPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Water);
	ATDFluidLiveActor* TerrainFluid = Subsystem->SpawnPooledFluidActor(WaterPreset, FVector::ZeroVector, 120.0f);
	if (!TestNotNull(TEXT("Spawned terrain fluid actor"), TerrainFluid))
	{
		return false;
	}

	const int32 Resolution = TerrainFluid->LiveComponent->GetSolver().GetResolution();
	constexpr int32 TraceBudget = 256;
	TerrainFluid->MaxTerrainTracesPerTick = TraceBudget;
	TerrainFluid->bSampleTerrainElevation = true;

	const int32 TicksNeeded = FMath::DivideAndRoundUp(Resolution * Resolution, TraceBudget);
	TickLiveActorSteps(TestWorld, TicksNeeded - 2);
	TestNearlyEqual(TEXT("The slope stays untouched while cells are still waiting for a trace"), MeasureLiveActorMaxTerrainSlope(TerrainFluid), 0.0f, 1e-4f);

	TickLiveActorSteps(TestWorld, 3);
	TestTrue(
		*FString::Printf(TEXT("The slope appears once the last queued cell is traced (%d ticks)"), TicksNeeded + 1),
		MeasureLiveActorMaxTerrainSlope(TerrainFluid) > 0.1f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidLiveActorTerrainMissTest,
	"TDGame.FluidFX.LiveActor.TerrainTraceMissFallsBackToActorHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidLiveActorTerrainMissTest::RunTest(const FString& Parameters)
{
	FTDFluidTestWorldScope TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem) || !TestNotNull(TEXT("Engine cube mesh loads"), CubeMesh))
	{
		return false;
	}

	AActor* Platform = SpawnStaticTerrainBlock(TestWorld.World, CubeMesh, FVector(1000.0f, 0.0f, -100.0f), FRotator::ZeroRotator, FVector(10.0f, 40.0f, 4.0f));
	if (!TestNotNull(TEXT("Spawned platform covering X from 500 to 1500 with its top at height 100"), Platform))
	{
		return false;
	}
	TickLiveActorSteps(TestWorld, 2);

	UTDFluidPresetAsset* WaterPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Water);
	ATDFluidLiveActor* TerrainFluid = Subsystem->SpawnPooledFluidActor(WaterPreset, FVector(0.0f, 0.0f, 300.0f), 120.0f);
	if (!TestNotNull(TEXT("Spawned terrain fluid actor at height 300"), TerrainFluid))
	{
		return false;
	}
	TerrainFluid->bSampleTerrainElevation = true;
	TickLiveActorSteps(TestWorld, 12);

	const float CellSizeCm = TerrainFluid->LiveComponent->GetSolver().GetExtentCm().X / TerrainFluid->LiveComponent->GetSolver().GetResolution();
	const FTDFluidCellSample FlatSample = SampleLiveActorAt(TerrainFluid, FVector(-1000.0f, 0.0f, 0.0f));
	const FTDFluidCellSample EdgeSample = SampleLiveActorAt(TerrainFluid, FVector(500.0f - CellSizeCm * 0.5f, 0.0f, 0.0f));
	const float ExpectedEdgeSlope = (100.0f - 300.0f) / (2.0f * CellSizeCm);
	TestNearlyEqual(TEXT("Cells whose trace missed form flat ground at the actor height"), static_cast<float>(FlatSample.TerrainSlope.Size()), 0.0f, 1e-4f);
	TestTrue(
		*FString::Printf(TEXT("The platform edge drops from the actor height 300 to the platform top 100 (%.3f vs %.3f)"), EdgeSample.TerrainSlope.X, ExpectedEdgeSlope),
		FMath::Abs(EdgeSample.TerrainSlope.X - ExpectedEdgeSlope) < 0.05f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidLiveActorTerrainSlidingWindowTest,
	"TDGame.FluidFX.LiveActor.TerrainSlidingWindowTracesOnlyNewStrips",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidLiveActorTerrainSlidingWindowTest::RunTest(const FString& Parameters)
{
	FTDFluidTestWorldScope TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem) || !TestNotNull(TEXT("Engine cube mesh loads"), CubeMesh))
	{
		return false;
	}

	constexpr float RampPitchDegrees = 10.0f;
	AActor* Ramp = SpawnStaticTerrainBlock(TestWorld.World, CubeMesh, FVector(0.0f, 0.0f, -100.0f), FRotator(RampPitchDegrees, 0.0f, 0.0f), FVector(80.0f, 80.0f, 1.0f));
	AActor* FollowTarget = SpawnOverlapProbeActor(TestWorld.World, FVector(0.0f, 0.0f, 2000.0f));
	if (!TestNotNull(TEXT("Spawned ramp"), Ramp) || !TestNotNull(TEXT("Spawned follow target"), FollowTarget))
	{
		return false;
	}
	TickLiveActorSteps(TestWorld, 2);

	UTDFluidPresetAsset* WaterPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Water);
	ATDFluidLiveActor* TerrainFluid = Subsystem->SpawnPooledFluidActor(WaterPreset, FVector::ZeroVector, 120.0f);
	if (!TestNotNull(TEXT("Spawned terrain fluid actor"), TerrainFluid))
	{
		return false;
	}

	constexpr int32 TraceBudget = 256;
	TerrainFluid->MaxTerrainTracesPerTick = TraceBudget;
	TerrainFluid->bSampleTerrainElevation = true;
	TerrainFluid->bShouldFollowPlayerPawn = true;
	TerrainFluid->FollowTargetActor = FollowTarget;

	const int32 Resolution = TerrainFluid->LiveComponent->GetSolver().GetResolution();
	const int32 FullPassTicks = FMath::DivideAndRoundUp(Resolution * Resolution, TraceBudget);
	TickLiveActorSteps(TestWorld, FullPassTicks + 2);

	const float ExpectedSlope = FMath::Tan(FMath::DegreesToRadians(RampPitchDegrees));
	const FVector NewStripLocation(2500.0f, 0.0f, 0.0f);
	const FVector RetainedLocation(-500.0f, 0.0f, 0.0f);
	FTDFluidCellSample OutsideSample;
	TestFalse(TEXT("The new strip lies outside the grid before the window moves"), TerrainFluid->LiveComponent->SampleFluidAtWorldLocation(NewStripLocation, OutsideSample));
	TestTrue(*FString::Printf(TEXT("The first full pass fills the slope (%.3f)"), SampleLiveActorAt(TerrainFluid, RetainedLocation).TerrainSlope.X), FMath::Abs(SampleLiveActorAt(TerrainFluid, RetainedLocation).TerrainSlope.X - ExpectedSlope) < 0.02f);

	FollowTarget->SetActorLocation(FVector(1000.0f, 0.0f, 2000.0f));
	const int32 StripTicks = 5;
	TickLiveActorSteps(TestWorld, StripTicks);

	TestTrue(
		*FString::Printf(TEXT("The window followed the target to X=1000 (%s)"), *TerrainFluid->LiveComponent->GetSolver().GetCenterWorld().ToString()),
		TerrainFluid->LiveComponent->GetSolver().GetCenterWorld().Equals(FVector2D(1000.0f, 0.0f), 1.0f));

	const FTDFluidCellSample NewStripSample = SampleLiveActorAt(TerrainFluid, NewStripLocation);
	const FTDFluidCellSample RetainedSample = SampleLiveActorAt(TerrainFluid, RetainedLocation);
	TestTrue(
		*FString::Printf(TEXT("The new strip is traced within %d ticks, far sooner than a %d tick full pass (%.3f)"), StripTicks, FullPassTicks, NewStripSample.TerrainSlope.X),
		FMath::Abs(NewStripSample.TerrainSlope.X - ExpectedSlope) < 0.02f);
	TestTrue(*FString::Printf(TEXT("The retained cells keep their slope (%.3f)"), RetainedSample.TerrainSlope.X), FMath::Abs(RetainedSample.TerrainSlope.X - ExpectedSlope) < 0.02f);

	return true;
}

#endif

