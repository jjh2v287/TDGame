#if WITH_DEV_AUTOMATION_TESTS

#include "FluidFX/TDFluidGpuSolverProxy.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidHullWakeRules.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidDenseWaterPresetKindsResolveTest,
	"TDGame.FluidFX.DenseWater.PresetKindsResolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidDenseWaterPresetKindsResolveTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Dense water is off by default"), FTDFluidSimulationConfig().bEnableDenseWater);
	TestFalse(TEXT("The sparse river kind keeps dense water off"), UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::River).bEnableDenseWater);

	const ETDFluidPresetKind DenseKinds[] = { ETDFluidPresetKind::DenseCreek, ETDFluidPresetKind::DenseLake, ETDFluidPresetKind::DenseSea, ETDFluidPresetKind::DenseTank };
	const FTDFluidSimulationConfig ShipWakeConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::ShipWakeSea);
	const UEnum* KindEnum = StaticEnum<ETDFluidPresetKind>();
	for (const ETDFluidPresetKind Kind : DenseKinds)
	{
		const FString KindName = KindEnum->GetNameStringByValue(static_cast<int64>(Kind));
		const FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(Kind);
		TestTrue(*FString::Printf(TEXT("%s enables dense water"), *KindName), Config.bEnableDenseWater);
		TestTrue(*FString::Printf(TEXT("%s enables the water layer"), *KindName), Config.bEnableWaterLayer);
		TestFalse(*FString::Printf(TEXT("%s keeps dry cells open"), *KindName), Config.bTreatDryCellsAsSolid);
		TestTrue(*FString::Printf(TEXT("%s prefers the GPU backend"), *KindName), Config.SolverBackend == ETDFluidSolverBackend::Gpu);
		TestTrue(*FString::Printf(TEXT("%s enables near ripples"), *KindName), UTDFluidPresetAsset::BuildWaterNearRippleSettingsForPresetKind(Kind).bEnabled);
		TestTrue(*FString::Printf(TEXT("%s window is smaller than the ship wake window"), *KindName), Config.SimExtentCm.X < ShipWakeConfig.SimExtentCm.X && Config.SimExtentCm.Y < ShipWakeConfig.SimExtentCm.Y);
		TestTrue(*FString::Printf(TEXT("%s open borders only for the creek"), *KindName), Config.bOpenWaterBorders == (Kind == ETDFluidPresetKind::DenseCreek));
	}

	TestFalse(TEXT("Ship wake sea stays sparse"), ShipWakeConfig.bEnableDenseWater);
	TestTrue(TEXT("Ship wake sea enables the water layer"), ShipWakeConfig.bEnableWaterLayer);
	TestTrue(TEXT("Ship wake sea prefers the GPU backend"), ShipWakeConfig.SolverBackend == ETDFluidSolverBackend::Gpu);
	TestTrue(TEXT("Ship wake sea dissipates whitewater"), ShipWakeConfig.bUseWhitewaterDissipation);
	TestFalse(TEXT("Ship wake sea keeps wetness off"), ShipWakeConfig.bEnableWaterWetness);
	TestTrue(TEXT("Ship wake sea grid is at least as fine as the dense lake grid"), ShipWakeConfig.GridResolution >= UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::DenseLake).GridResolution);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidDenseWaterHullStampsFromBoundsTest,
	"TDGame.FluidFX.DenseWater.HullStampsFromBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidDenseWaterHullStampsFromBoundsTest::RunTest(const FString& Parameters)
{
	const FVector2D LengthAndBeam = FTDFluidHullWakeRules::ResolveHullLengthAndBeam(FVector(1500.0, 10000.0, 400.0), 0.0f, 0.0f);
	TestEqual(TEXT("Hull length comes from the long bounds axis"), LengthAndBeam.X, 10000.0, 1.0e-3);
	TestEqual(TEXT("Hull beam comes from the short bounds axis"), LengthAndBeam.Y, 1500.0, 1.0e-3);
	const FVector2D AuthoredSize = FTDFluidHullWakeRules::ResolveHullLengthAndBeam(FVector(1500.0, 10000.0, 400.0), 8000.0f, 1200.0f);
	TestEqual(TEXT("Authored hull length wins over bounds"), AuthoredSize.X, 8000.0, 1.0e-3);
	TestEqual(TEXT("Authored hull beam wins over bounds"), AuthoredSize.Y, 1200.0, 1.0e-3);

	const FTDFluidInputBrushSettings Settings;
	const FTransform HullTransform(FRotator::ZeroRotator, FVector(500.0, -200.0, 30.0));
	const FVector FastVelocity(600.0, 0.0, 0.0);
	TArray<FTDFluidBrushStamp> Stamps;
	FTDFluidHullWakeRules::BuildHullStamps(HullTransform, FastVelocity, static_cast<float>(LengthAndBeam.X), static_cast<float>(LengthAndBeam.Y), ETDFluidBrushChannel::ChannelA_Water, Settings, Stamps);
	if (!TestEqual(TEXT("A 100 m by 15 m hull uses two stamps"), Stamps.Num(), 2))
	{
		return false;
	}

	TestEqual(TEXT("Stamp radius is a quarter of the hull length"), Stamps[0].RadiusCm, 2500.0f, 1.0e-2f);
	TestEqual(TEXT("Stamp aspect ratio is radius over half beam"), Stamps[0].ShapeAspectRatio, 2500.0f / 750.0f, 1.0e-3f);
	TestTrue(TEXT("The bow stamp is ahead of the stern stamp"), Stamps[1].WorldLocation.X > Stamps[0].WorldLocation.X);
	TestEqual(TEXT("Only the bow stamp pushes ripples"), Stamps[1].RippleImpulseCm, Settings.HullWakeRippleImpulseCm);
	TestEqual(TEXT("The stern stamp pushes no ripples"), Stamps[0].RippleImpulseCm, 0.0f);
	TestEqual(TEXT("Stamps follow the hull velocity"), Stamps[0].WorldVelocity, FastVelocity);
	TestTrue(TEXT("Stamps use the water channel"), Stamps[0].Channel == ETDFluidBrushChannel::ChannelA_Water);
	TestEqual(TEXT("Stamps carry the hull velocity strength"), Stamps[0].VelocityStrength, Settings.HullWakeVelocityStrength);
	TestEqual(TEXT("Stamps carry the hull density strength"), Stamps[0].DensityStrength, Settings.HullWakeDensityStrength);
	TestEqual(TEXT("Stamps stretch along the hull forward axis"), Stamps[0].ShapeMajorAxis, FVector::ForwardVector);

	const FVector SlowVelocity = FVector::ForwardVector * (Settings.HullWakeMinSpeedCmPerSec * 0.5f);
	FTDFluidHullWakeRules::BuildHullStamps(HullTransform, SlowVelocity, static_cast<float>(LengthAndBeam.X), static_cast<float>(LengthAndBeam.Y), ETDFluidBrushChannel::ChannelA_Water, Settings, Stamps);
	TestEqual(TEXT("A hull slower than the minimum speed leaves no stamps"), Stamps.Num(), 0);
	return true;
}

namespace TDFluidDenseWaterRuntimeTestPrivate
{
	constexpr float SeedStepSeconds = 1.0f / 60.0f;
	constexpr float SeedWetThresholdCm = 1.0f;
	constexpr float SeedExtentCm = 1280.0f;

	struct FDenseWaterSwitchOverride
	{
		FDenseWaterSwitchOverride()
			: Variable(IConsoleManager::Get().FindConsoleVariable(TEXT("TD.FluidFX.DenseWater")))
		{
			if (!Variable)
			{
				return;
			}
			PreviousValue = Variable->GetInt();
			Variable->Set(1, ECVF_SetByCode);
		}

		~FDenseWaterSwitchOverride()
		{
			if (Variable)
			{
				Variable->Set(PreviousValue, ECVF_SetByCode);
			}
		}

		IConsoleVariable* Variable = nullptr;
		int32 PreviousValue = 0;
	};

	float SeedBowlElevation(const FVector2D& Position)
	{
		return 0.0002f * static_cast<float>(Position.SizeSquared());
	}

	FTDFluidSimulationConfig MakeSeedConfig(int32 Resolution)
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = Resolution;
		Config.SimExtentCm = FVector2D(SeedExtentCm, SeedExtentCm);
		Config.bEnableWaterLayer = true;
		Config.bTreatDryCellsAsSolid = false;
		Config.bOpenWaterBorders = false;
		Config.bEnableWaterWetness = false;
		Config.WhitewaterFromVorticity = 0.0f;
		Config.RippleWaveSpeedCmPerSec = 0.0f;
		Config.bEnableDenseWater = true;
		Config.DenseWaterGravityCmPerSec2 = 980.0f;
		Config.DenseWaterGapFillTendency = 1.0f;
		Config.DenseWaterPeakAvoidance = 1.0f;
		Config.DenseWaterPeakSoftnessCm = 2.0f;
		Config.DenseWaterSdfRepelCmPerSec2 = 400.0f;
		Config.DenseWaterSdfRepelBandCm = 30.0f;
		Config.DenseWaterLinearDragPerSec = 0.3f;
		Config.DenseWaterBedFriction = 0.01f;
		Config.DenseWaterWetThresholdCm = SeedWetThresholdCm;
		Config.DenseWaterDryDepthCm = 0.05f;
		Config.DenseWaterFilmDrainCmPerSec = 0.0f;
		Config.DenseWaterMaxFlowDepthCm = 200.0f;
		Config.DenseWaterMaxSpeedCmPerSec = 400.0f;
		Config.DenseWaterCourantNumber = 0.5f;
		Config.DenseWaterMaxSubsteps = 4;
		Config.DenseWaterMomentumAdvection = 1.0f;
		Config.DenseWaterReservoirRelaxPerSec = 8.0f;
		return Config;
	}

	void InitializeSeedBowl(FTDFluidGrid2DSolver& Solver, int32 Resolution, const FVector2D& CenterCm)
	{
		Solver.Initialize(MakeSeedConfig(Resolution), CenterCm);
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				Solver.SetTerrainElevationAtCell(CellX, CellY, SeedBowlElevation(Solver.CellToWorldCenter(CellX, CellY)));
			}
		}
		Solver.ComputeTerrainSlopeFromElevation();
	}

	FTDFluidDenseWaterSeed MakeBowlLakeSeed(const FTDFluidGrid2DSolver& Solver)
	{
		FTDFluidDenseWaterSeed Seed;
		const int32 Resolution = Solver.GetResolution();
		Seed.Resolution = Resolution;
		Seed.CenterCm = Solver.GetCenterWorld();
		Seed.ExtentCm = Solver.GetExtentCm();
		Seed.DepthCm.SetNumZeroed(Resolution * Resolution);
		Seed.FaceVelocityXCmPerSec.SetNumZeroed(Resolution * Resolution);
		Seed.FaceVelocityYCmPerSec.SetNumZeroed(Resolution * Resolution);
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const FVector2D Center = Solver.CellToWorldCenter(CellX, CellY);
				Seed.DepthCm[CellY * Resolution + CellX] = Center.X < 200.0 ? FMath::Max(0.0f, 30.0f - SeedBowlElevation(Center)) : 0.0f;
			}
		}
		return Seed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidDenseWaterSeedRoundTripTest,
	"TDGame.FluidFX.DenseWater.SeedRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidDenseWaterSeedRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidDenseWaterRuntimeTestPrivate;

	const FDenseWaterSwitchOverride DenseWaterSwitch;
	FTDFluidGrid2DSolver Coarse;
	InitializeSeedBowl(Coarse, 128, FVector2D::ZeroVector);
	if (!TestTrue(TEXT("Coarse dense water is active"), Coarse.IsDenseWaterActive()))
	{
		return false;
	}

	Coarse.SetDenseWaterSeed(MakeBowlLakeSeed(Coarse));
	for (int32 StepIndex = 0; StepIndex < 30; ++StepIndex)
	{
		Coarse.StepSimulation(SeedStepSeconds);
	}

	FTDFluidDenseWaterSeed Exported;
	if (!TestTrue(TEXT("The coarse state exports"), Coarse.ExportDenseWaterState(Exported)))
	{
		return false;
	}

	FTDFluidGrid2DSolver Fine;
	InitializeSeedBowl(Fine, 256, FVector2D::ZeroVector);
	Fine.SetDenseWaterSeed(Exported);
	const double CoarseVolume = Coarse.ComputeDenseWaterVolumeCm3();
	const double FineVolume = Fine.ComputeDenseWaterVolumeCm3();
	const double VolumeError = CoarseVolume > 0.0 ? FMath::Abs(FineVolume - CoarseVolume) / CoarseVolume : 1.0;

	FTDFluidDenseWaterSeed FineState;
	Fine.ExportDenseWaterState(FineState);
	int32 WetUnionCount = 0;
	int32 WetIntersectionCount = 0;
	for (int32 CellY = 0; CellY < FineState.Resolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < FineState.Resolution; ++CellX)
		{
			const bool bIsFineWet = FineState.DepthCm[CellY * FineState.Resolution + CellX] > SeedWetThresholdCm;
			const bool bIsCoarseWet = Exported.DepthCm[(CellY / 2) * Exported.Resolution + CellX / 2] > SeedWetThresholdCm;
			WetUnionCount += bIsFineWet || bIsCoarseWet ? 1 : 0;
			WetIntersectionCount += bIsFineWet && bIsCoarseWet ? 1 : 0;
		}
	}

	const double WetOverlap = WetUnionCount > 0 ? static_cast<double>(WetIntersectionCount) / WetUnionCount : 0.0;
	AddInfo(FString::Printf(TEXT("Volume %.1f -> %.1f cm3 (error %.5f), wet overlap %.4f over %d cells"), CoarseVolume, FineVolume, VolumeError, WetOverlap, WetUnionCount));
	TestTrue(TEXT("The seeded fine volume is within 2 percent"), VolumeError <= 0.02);
	TestTrue(TEXT("The seeded fine wet area overlaps at least 95 percent"), WetOverlap >= 0.95);

	FTDFluidGrid2DSolver Shifted;
	InitializeSeedBowl(Shifted, 256, FVector2D(100.0, 0.0));
	Shifted.SetDenseWaterSeed(Exported);
	TestEqual(TEXT("A seed from another window is not applied"), Shifted.ComputeDenseWaterVolumeCm3(), 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidDenseWaterShiftDisabledTest,
	"TDGame.FluidFX.DenseWater.ShiftDisabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidDenseWaterShiftDisabledTest::RunTest(const FString& Parameters)
{
	FTDFluidSimulationConfig DenseConfig;
	DenseConfig.SimMode = ETDFluidSimMode::FluidSim;
	DenseConfig.GridResolution = 64;
	DenseConfig.SimExtentCm = FVector2D(1280.0, 1280.0);
	DenseConfig.bEnableWaterLayer = true;
	DenseConfig.bEnableDenseWater = true;
	DenseConfig.bTreatDryCellsAsSolid = true;
	FTDFluidSimulationConfig SparseConfig = DenseConfig;
	SparseConfig.bEnableDenseWater = false;
	const FVector2D FarCenter(640.0, 320.0);

	FTDFluidGrid2DSolver DenseSolver;
	DenseSolver.Initialize(DenseConfig, FVector2D::ZeroVector);
	TestTrue(TEXT("The CPU solver runs dense water"), DenseSolver.IsDenseWaterActive());
	TestFalse(TEXT("The dense CPU window does not slide"), DenseSolver.ShiftOriginForSlidingWindow(FarCenter));

	FTDFluidGrid2DSolver SparseSolver;
	SparseSolver.Initialize(SparseConfig, FVector2D::ZeroVector);
	TestFalse(TEXT("The sparse CPU solver keeps dense water off"), SparseSolver.IsDenseWaterActive());
	TestTrue(TEXT("The sparse CPU window still slides"), SparseSolver.ShiftOriginForSlidingWindow(FarCenter));

	FTDFluidGpuSolverProxy DenseProxy;
	DenseProxy.Initialize(DenseConfig, 256, FVector2D::ZeroVector);
	TestTrue(TEXT("The GPU proxy runs dense water"), DenseProxy.UsesDenseWater());
	TestFalse(TEXT("Dense GPU settings keep dry cells open"), DenseProxy.GetSettings().bTreatDryCellsAsSolid);
	TestTrue(TEXT("The dense GPU window does not slide"), DenseProxy.ShiftWindowTowards(FarCenter) == FIntPoint::ZeroValue);

	FTDFluidGpuSolverProxy SparseProxy;
	SparseProxy.Initialize(SparseConfig, 256, FVector2D::ZeroVector);
	TestFalse(TEXT("The sparse GPU proxy keeps dense water off"), SparseProxy.UsesDenseWater());
	TestTrue(TEXT("The sparse GPU settings keep the dry cell switch"), SparseProxy.GetSettings().bTreatDryCellsAsSolid);
	TestTrue(TEXT("The sparse GPU window still slides"), SparseProxy.ShiftWindowTowards(FarCenter) != FIntPoint::ZeroValue);
	return true;
}

#endif
