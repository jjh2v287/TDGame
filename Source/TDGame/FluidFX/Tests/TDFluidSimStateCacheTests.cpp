#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidSimStateCacheAsset.h"
#include "FluidFX/TDFluidSimStateCacheTypes.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TDFluidSimStateCacheTestPrivate
{
	constexpr float SimStateCacheStepSeconds = 1.0f / 60.0f;
	constexpr int32 SimStateCacheWarmupSteps = 30;
	constexpr int32 SimStateCacheResolution = 64;
	constexpr double SimStateCacheExtentCm = 1280.0;

	FTDFluidSimulationConfig MakeSimStateCacheConfig(bool bUsesWater)
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = SimStateCacheResolution;
		Config.SimExtentCm = FVector2D(SimStateCacheExtentCm, SimStateCacheExtentCm);
		Config.bEnableHeatChannel = !bUsesWater;
		Config.bEnableWaterLayer = bUsesWater;
		Config.bEnableWaterWetness = bUsesWater;
		Config.WetnessFromWhitewater = 0.5f;
		Config.WetnessDryingSeconds = 4.0f;
		Config.RippleWaveSpeedCmPerSec = bUsesWater ? 300.0f : 0.0f;
		return Config;
	}

	void ApplyStaticInputs(FTDFluidGrid2DSolver& Solver, bool bUsesWater)
	{
		if (!bUsesWater)
		{
			return;
		}

		for (int32 CellY = 0; CellY < Solver.GetResolution(); ++CellY)
		{
			for (int32 CellX = 0; CellX < Solver.GetResolution(); ++CellX)
			{
				Solver.SetTerrainElevationAtCell(CellX, CellY, -50.0f);
			}
		}
		Solver.ComputeTerrainSlopeFromElevation();

		FTDFluidWaterRegion Region;
		Region.MinXY = FVector2D(-2000.0, -2000.0);
		Region.MaxXY = FVector2D(0.0, 2000.0);
		Region.SurfaceHeightCm = 0.0f;
		TArray<FTDFluidWaterRegion> Regions;
		Regions.Add(Region);
		Solver.SetWaterRegions(Regions);
	}

	void StepWithInputs(FTDFluidGrid2DSolver& Solver, int32 StepIndex, bool bUsesWater)
	{
		TArray<FTDFluidBrushStamp> Stamps;
		FTDFluidBrushStamp& DensityStamp = Stamps.AddDefaulted_GetRef();
		DensityStamp.WorldLocation = bUsesWater ? FVector(200.0, 0.0, 0.0) : FVector::ZeroVector;
		DensityStamp.WorldVelocity = FVector(0.0, 300.0, 0.0);
		DensityStamp.RadiusCm = 100.0f;
		DensityStamp.DensityStrength = 1.0f;
		DensityStamp.HeatStrength = bUsesWater ? 0.0f : 1.0f;
		if (bUsesWater && StepIndex % 5 == 0)
		{
			FTDFluidBrushStamp& RippleStamp = Stamps.AddDefaulted_GetRef();
			RippleStamp.WorldLocation = FVector(-300.0, 0.0, 0.0);
			RippleStamp.RadiusCm = 80.0f;
			RippleStamp.DensityStrength = 0.0f;
			RippleStamp.RippleImpulseCm = 2.5f;
		}

		Solver.ApplyBrushStamps(Stamps, SimStateCacheStepSeconds);
		Solver.StepSimulation(SimStateCacheStepSeconds);
	}

	void InitializeSolver(FTDFluidGrid2DSolver& Solver, bool bUsesWater)
	{
		Solver.Initialize(MakeSimStateCacheConfig(bUsesWater), FVector2D::ZeroVector);
		ApplyStaticInputs(Solver, bUsesWater);
	}

	void RunWarmup(FTDFluidGrid2DSolver& Solver, bool bUsesWater)
	{
		for (int32 StepIndex = 0; StepIndex < SimStateCacheWarmupSteps; ++StepIndex)
		{
			StepWithInputs(Solver, StepIndex, bUsesWater);
		}
	}

	bool HasNonZeroFloat(const FTDFluidSimStateSnapshot& Snapshot, FName ChannelName)
	{
		const FTDFluidStateChannelBytes* Channel = Snapshot.FindChannel(ChannelName);
		if (!Channel)
		{
			return false;
		}

		const float* Values = reinterpret_cast<const float*>(Channel->Bytes.GetData());
		const int32 ValueCount = Channel->Bytes.Num() / static_cast<int32>(sizeof(float));
		for (int32 Index = 0; Index < ValueCount; ++Index)
		{
			if (Values[Index] != 0.0f)
			{
				return true;
			}
		}
		return false;
	}

	bool AreSnapshotsEqual(const FTDFluidSimStateSnapshot& Left, const FTDFluidSimStateSnapshot& Right)
	{
		if (Left.LayoutVersion != Right.LayoutVersion || Left.Backend != Right.Backend || Left.Resolution != Right.Resolution
			|| Left.SimExtentCm != Right.SimExtentCm || Left.CenterPlaneCm != Right.CenterPlaneCm || Left.AreaAddressMode != Right.AreaAddressMode
			|| Left.SimMode != Right.SimMode || Left.bUsesHeatChannel != Right.bUsesHeatChannel || Left.bUsesWaterLayer != Right.bUsesWaterLayer
			|| Left.SimulationTimeSeconds != Right.SimulationTimeSeconds || !(Left.GpuLayout == Right.GpuLayout) || Left.Channels.Num() != Right.Channels.Num())
		{
			return false;
		}

		for (int32 Index = 0; Index < Left.Channels.Num(); ++Index)
		{
			const FTDFluidStateChannelBytes& LeftChannel = Left.Channels[Index];
			const FTDFluidStateChannelBytes& RightChannel = Right.Channels[Index];
			if (LeftChannel.Name != RightChannel.Name || LeftChannel.Width != RightChannel.Width || LeftChannel.Height != RightChannel.Height
				|| LeftChannel.PixelFormat != RightChannel.PixelFormat || LeftChannel.Bytes != RightChannel.Bytes)
			{
				return false;
			}
		}
		return true;
	}

	struct FTDFluidSimStateCacheTestWorld
	{
		FTDFluidSimStateCacheTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidSimStateCacheTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidSimStateCacheTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		void StepWorld()
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, SimStateCacheStepSeconds);
		}

		UTDFluidLiveComponent* SpawnMudComponent() const
		{
			UTDFluidLiveSubsystem* FluidSubsystem = World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr;
			UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(World, ETDFluidPresetKind::Mud);
			ATDFluidLiveActor* LiveActor = FluidSubsystem ? FluidSubsystem->SpawnPooledFluidActor(Preset, FVector::ZeroVector, 0.0f) : nullptr;
			return LiveActor ? LiveActor->LiveComponent.Get() : nullptr;
		}

		UWorld* World = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSimStateCacheCpuSeedStepMatchesOriginalTest,
	"TDGame.FluidFX.SimStateCache.CpuSeedStepMatchesOriginal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSimStateCacheCpuSeedStepMatchesOriginalTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSimStateCacheTestPrivate;

	for (const bool bUsesWater : {false, true})
	{
		const FString CaseName = bUsesWater ? TEXT("water layer") : TEXT("smoke with heat");
		FTDFluidGrid2DSolver Original;
		InitializeSolver(Original, bUsesWater);
		RunWarmup(Original, bUsesWater);

		FTDFluidSimStateSnapshot Snapshot;
		Original.CaptureStateSnapshot(Snapshot);
		TestEqual(*FString::Printf(TEXT("%s captures the expected channel count"), *CaseName), Snapshot.Channels.Num(), bUsesWater ? 10 : 8);
		if (bUsesWater)
		{
			TestTrue(TEXT("Water snapshot carries ripple activity"), HasNonZeroFloat(Snapshot, TEXT("RippleHeight")) || HasNonZeroFloat(Snapshot, TEXT("RippleHeightVelocity")));
			TestTrue(TEXT("Water snapshot carries wetness"), HasNonZeroFloat(Snapshot, TEXT("Wetness")));
		}

		StepWithInputs(Original, SimStateCacheWarmupSteps, bUsesWater);
		const uint32 OriginalStateChecksum = Original.ComputeStateChecksum();
		const uint32 OriginalWaterChecksum = Original.ComputeWaterStateChecksum();

		FTDFluidGrid2DSolver Restored;
		InitializeSolver(Restored, bUsesWater);
		FString RestoreReason;
		const bool bRestored = Restored.RestoreStateSnapshot(Snapshot, RestoreReason);
		TestTrue(*FString::Printf(TEXT("%s restores (%s)"), *CaseName, *RestoreReason), bRestored);
		TestEqual(*FString::Printf(TEXT("%s restores simulation time"), *CaseName), Restored.GetSimulationTimeSeconds(), Snapshot.SimulationTimeSeconds);

		StepWithInputs(Restored, SimStateCacheWarmupSteps, bUsesWater);
		TestEqual(*FString::Printf(TEXT("%s state checksum matches after one step"), *CaseName), Restored.ComputeStateChecksum(), OriginalStateChecksum);
		TestEqual(*FString::Printf(TEXT("%s water checksum matches after one step"), *CaseName), Restored.ComputeWaterStateChecksum(), OriginalWaterChecksum);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSimStateCacheRejectsMismatchedHeaderTest,
	"TDGame.FluidFX.SimStateCache.RejectsMismatchedHeader",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSimStateCacheRejectsMismatchedHeaderTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSimStateCacheTestPrivate;

	FTDFluidGrid2DSolver Source;
	InitializeSolver(Source, false);
	RunWarmup(Source, false);
	FTDFluidSimStateSnapshot Snapshot;
	Source.CaptureStateSnapshot(Snapshot);

	const double CellSizeCm = SimStateCacheExtentCm / SimStateCacheResolution;
	TArray<TPair<FString, TFunction<void(FTDFluidSimStateSnapshot&)>>> Mutations;
	Mutations.Emplace(TEXT("resolution"), [](FTDFluidSimStateSnapshot& Seed) { Seed.Resolution = SimStateCacheResolution / 2; });
	Mutations.Emplace(TEXT("backend"), [](FTDFluidSimStateSnapshot& Seed) { Seed.Backend = ETDFluidSimStateCacheBackend::Gpu; });
	Mutations.Emplace(TEXT("layout version"), [](FTDFluidSimStateSnapshot& Seed) { Seed.LayoutVersion = TDFluidSimStateCacheLayoutVersion + 1; });
	Mutations.Emplace(TEXT("center one cell"), [CellSizeCm](FTDFluidSimStateSnapshot& Seed) { Seed.CenterPlaneCm.X += CellSizeCm; });
	Mutations.Emplace(TEXT("water layer"), [](FTDFluidSimStateSnapshot& Seed) { Seed.bUsesWaterLayer = true; });
	Mutations.Emplace(TEXT("missing channel"), [](FTDFluidSimStateSnapshot& Seed) { Seed.Channels.Pop(); });

	FTDFluidGrid2DSolver Target;
	InitializeSolver(Target, false);
	StepWithInputs(Target, 0, false);
	const uint32 TargetChecksum = Target.ComputeStateChecksum();
	for (const TPair<FString, TFunction<void(FTDFluidSimStateSnapshot&)>>& Mutation : Mutations)
	{
		FTDFluidSimStateSnapshot Seed = Snapshot;
		Mutation.Value(Seed);
		FString Reason;
		TestFalse(*FString::Printf(TEXT("Mismatched %s is rejected"), *Mutation.Key), Target.RestoreStateSnapshot(Seed, Reason));
		TestFalse(*FString::Printf(TEXT("Mismatched %s reports a reason"), *Mutation.Key), Reason.IsEmpty());
		TestEqual(*FString::Printf(TEXT("Mismatched %s leaves the state untouched"), *Mutation.Key), Target.ComputeStateChecksum(), TargetChecksum);
	}

	FTDFluidSimStateSnapshot NearCenterSeed = Snapshot;
	NearCenterSeed.CenterPlaneCm.X += 0.25 * CellSizeCm;
	FString NearCenterReason;
	TestTrue(TEXT("A quarter cell center offset is accepted"), Target.RestoreStateSnapshot(NearCenterSeed, NearCenterReason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSimStateCacheAssetRoundTripTest,
	"TDGame.FluidFX.SimStateCache.AssetRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSimStateCacheAssetRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSimStateCacheTestPrivate;

	FTDFluidGrid2DSolver Source;
	InitializeSolver(Source, true);
	RunWarmup(Source, true);
	FTDFluidSimStateSnapshot Snapshot;
	Source.CaptureStateSnapshot(Snapshot);
	Snapshot.GpuLayout.ResolutionTier = 3;
	Snapshot.GpuLayout.SimMode = 2;
	Snapshot.GpuLayout.bUsesWaterLayer = true;
	Snapshot.GpuLayout.bUsesFloat32Ripple = true;
	Snapshot.GpuLayout.bWritesPainterVelocity = true;

	TStrongObjectPtr<UTDFluidSimStateCacheAsset> CacheAsset(NewObject<UTDFluidSimStateCacheAsset>(GetTransientPackage()));
	TestFalse(TEXT("An empty cache asset is not usable"), CacheAsset->IsUsable());
	CacheAsset->SetFromSnapshot(Snapshot);
	TestTrue(TEXT("A filled cache asset is usable"), CacheAsset->IsUsable());

	const TSharedRef<const FTDFluidSimStateSnapshot, ESPMode::ThreadSafe> RoundTrip = CacheAsset->MakeSnapshot();
	TestTrue(TEXT("Snapshot header and channel bytes survive the asset round trip"), AreSnapshotsEqual(Snapshot, *RoundTrip));

	const int64 FirstChecksum = CacheAsset->ComputeChecksum();
	TestEqual(TEXT("Asset checksum is stable"), CacheAsset->ComputeChecksum(), FirstChecksum);
	CacheAsset->Channels[0].Bytes[0] ^= 0x1;
	TestNotEqual(TEXT("Asset checksum follows channel bytes"), CacheAsset->ComputeChecksum(), FirstChecksum);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSimStateCacheComponentConsumesSeedOnceTest,
	"TDGame.FluidFX.SimStateCache.ComponentConsumesSeedOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSimStateCacheComponentConsumesSeedOnceTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSimStateCacheTestPrivate;

	FTDFluidSimStateCacheTestWorld TestWorld;
	UTDFluidLiveComponent* Component = TestWorld.SpawnMudComponent();
	if (!TestNotNull(TEXT("Mud component spawns"), Component))
	{
		return false;
	}

	Component->SetSimulationAwake(true);
	TestWorld.StepWorld();
	TestWorld.StepWorld();
	if (!TestFalse(TEXT("Mud runs on the CPU solver"), Component->IsUsingGpuSolver()) || !TestTrue(TEXT("Mud solver is initialized"), Component->IsSimulationInitialized()))
	{
		return false;
	}

	const FVector2D Center = Component->GetSimulationCenterWorld();
	FTDFluidSimStateSnapshot Snapshot;
	FString CaptureError;
	if (!TestTrue(*FString::Printf(TEXT("Component captures its CPU state (%s)"), *CaptureError), Component->CaptureSimStateSnapshot(Snapshot, CaptureError)))
	{
		return false;
	}

	for (int32 StepIndex = 0; StepIndex < 3; ++StepIndex)
	{
		Component->AdvanceSimulationStep(SimStateCacheStepSeconds, Center, false);
	}

	const double SeedTimeSeconds = Snapshot.SimulationTimeSeconds;
	Component->QueueSimStateSeed(MakeShared<FTDFluidSimStateSnapshot, ESPMode::ThreadSafe>(Snapshot));
	TestTrue(TEXT("Queued seed is pending"), Component->HasPendingSimStateSeed());

	Component->AdvanceSimulationStep(SimStateCacheStepSeconds, Center, false);
	TestFalse(TEXT("Seed is consumed by the next step"), Component->HasPendingSimStateSeed());
	TestEqual(TEXT("Next step runs from the restored time"), Component->GetSolver().GetSimulationTimeSeconds(), SeedTimeSeconds + SimStateCacheStepSeconds, 1.0e-6);

	Component->AdvanceSimulationStep(SimStateCacheStepSeconds, Center, false);
	TestEqual(TEXT("Second step does not apply the seed again"), Component->GetSolver().GetSimulationTimeSeconds(), SeedTimeSeconds + 2.0 * SimStateCacheStepSeconds, 1.0e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSimStateCacheRejectsDenseWaterCaptureTest,
	"TDGame.FluidFX.SimStateCache.RejectsDenseWaterCapture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSimStateCacheRejectsDenseWaterCaptureTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSimStateCacheTestPrivate;

	FTDFluidSimulationConfig DenseConfig = MakeSimStateCacheConfig(true);
	DenseConfig.bEnableDenseWater = true;
	FTDFluidGrid2DSolver DenseSolver;
	DenseSolver.Initialize(DenseConfig, FVector2D::ZeroVector);
	if (!TestTrue(TEXT("CPU solver runs dense water"), DenseSolver.IsDenseWaterActive()))
	{
		return false;
	}

	FTDFluidSimStateSnapshot DenseSnapshot;
	DenseSolver.CaptureStateSnapshot(DenseSnapshot);
	TestTrue(TEXT("Dense capture marks the snapshot header"), DenseSnapshot.bUsesDenseWater);
	TestFalse(TEXT("Dense capture is rejected"), TDFluidSimStateCache::DescribeDenseWaterRejection(DenseSnapshot).IsEmpty());

	const uint32 DenseChecksum = DenseSolver.ComputeStateChecksum();
	FString DenseReason;
	TestFalse(TEXT("Dense seed is rejected by a dense solver"), DenseSolver.RestoreStateSnapshot(DenseSnapshot, DenseReason));
	TestFalse(TEXT("Dense seed rejection reports a reason"), DenseReason.IsEmpty());

	FTDFluidGrid2DSolver SparseSolver;
	InitializeSolver(SparseSolver, true);
	RunWarmup(SparseSolver, true);
	FTDFluidSimStateSnapshot SparseSnapshot;
	SparseSolver.CaptureStateSnapshot(SparseSnapshot);
	TestFalse(TEXT("Sparse capture keeps the dense flag clear"), SparseSnapshot.bUsesDenseWater);
	TestTrue(TEXT("Sparse capture is accepted"), TDFluidSimStateCache::DescribeDenseWaterRejection(SparseSnapshot).IsEmpty());

	FString SparseReason;
	TestFalse(TEXT("Sparse seed is rejected by a dense solver"), DenseSolver.RestoreStateSnapshot(SparseSnapshot, SparseReason));
	TestEqual(TEXT("Rejected seeds leave the dense state untouched"), DenseSolver.ComputeStateChecksum(), DenseChecksum);
	return true;
}

#endif
