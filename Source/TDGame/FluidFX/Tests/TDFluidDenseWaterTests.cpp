#if WITH_DEV_AUTOMATION_TESTS

#include "FluidFX/TDFluidGrid2DSolver.h"
#include "HAL/IConsoleManager.h"
#include "Math/Float16Color.h"
#include "Misc/AutomationTest.h"
#include "TDFluidGpuDenseWater.h"

namespace TDFluidDenseWaterTestPrivate
{
	constexpr float GravityWaveCourantTolerance = 1.0e-4f;

	float ComputeGravityWaveCourant(const FTDFluidGpuDenseWaterSettings& Settings, const FTDFluidGpuDenseWaterStepPlan& Plan, float CellCm)
	{
		return FMath::Sqrt(Settings.GravityCmPerSec2 * Plan.GravityScale * Settings.MaxFlowDepthCm) * Plan.SubstepSeconds / CellCm;
	}

	constexpr float StepSeconds = 1.0f / 60.0f;
	constexpr float WetThresholdCm = 1.0f;
	constexpr int32 SamplingIntervalSteps = 15;
	constexpr double DownhillValleyX = 320.0;
	const TCHAR* const DenseWaterVariableName = TEXT("TD.FluidFX.DenseWater");
	const TCHAR* const RowParallelVariableName = TEXT("TD.FluidFX.SolverRowParallel");

	struct FConsoleIntOverride
	{
		FConsoleIntOverride(const TCHAR* VariableName, int32 Value)
			: Variable(IConsoleManager::Get().FindConsoleVariable(VariableName))
		{
			if (!Variable)
			{
				return;
			}
			PreviousValue = Variable->GetInt();
			Variable->Set(Value, ECVF_SetByCode);
		}

		~FConsoleIntOverride()
		{
			if (Variable)
			{
				Variable->Set(PreviousValue, ECVF_SetByCode);
			}
		}

		void SetValue(int32 Value)
		{
			if (Variable)
			{
				Variable->Set(Value, ECVF_SetByCode);
			}
		}

		IConsoleVariable* Variable = nullptr;
		int32 PreviousValue = 0;
	};

	void ApplyDenseSettings(FTDFluidSimulationConfig& Config)
	{
		Config.bEnableDenseWater = true;
		Config.DenseWaterGravityCmPerSec2 = 980.0f;
		Config.DenseWaterGapFillTendency = 1.0f;
		Config.DenseWaterPeakAvoidance = 1.0f;
		Config.DenseWaterPeakSoftnessCm = 2.0f;
		Config.DenseWaterSdfRepelCmPerSec2 = 400.0f;
		Config.DenseWaterSdfRepelBandCm = 30.0f;
		Config.DenseWaterLinearDragPerSec = 0.3f;
		Config.DenseWaterBedFriction = 0.01f;
		Config.DenseWaterWetThresholdCm = WetThresholdCm;
		Config.DenseWaterDryDepthCm = 0.05f;
		Config.DenseWaterFilmDrainCmPerSec = 0.2f;
		Config.DenseWaterMaxFlowDepthCm = 200.0f;
		Config.DenseWaterMaxSpeedCmPerSec = 400.0f;
		Config.DenseWaterCourantNumber = 0.5f;
		Config.DenseWaterMaxSubsteps = 4;
		Config.DenseWaterMomentumAdvection = 1.0f;
		Config.DenseWaterReservoirRelaxPerSec = 8.0f;
	}

	FTDFluidSimulationConfig MakeDenseConfig(int32 Resolution, float ExtentCm, bool bOpenBorders)
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = Resolution;
		Config.SimExtentCm = FVector2D(ExtentCm, ExtentCm);
		Config.bEnableWaterLayer = true;
		Config.bTreatDryCellsAsSolid = false;
		Config.bOpenWaterBorders = bOpenBorders;
		Config.bEnableWaterWetness = false;
		Config.WhitewaterFromVorticity = 0.0f;
		Config.RapidsWhitewaterPerSec = 0.0f;
		Config.RippleWaveSpeedCmPerSec = 0.0f;
		ApplyDenseSettings(Config);
		return Config;
	}

	void InitializeWithTerrain(FTDFluidGrid2DSolver& Solver, const FTDFluidSimulationConfig& Config, TFunctionRef<float(const FVector2D&)> Elevation)
	{
		Solver.Initialize(Config, FVector2D::ZeroVector);
		const int32 Resolution = Solver.GetResolution();
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				Solver.SetTerrainElevationAtCell(CellX, CellY, Elevation(Solver.CellToWorldCenter(CellX, CellY)));
			}
		}
		Solver.ComputeTerrainSlopeFromElevation();
	}

	FTDFluidDenseWaterSeed MakeSeed(const FTDFluidGrid2DSolver& Solver, TFunctionRef<float(const FVector2D&)> Depth, TFunctionRef<FVector2D(const FVector2D&)> FaceVelocity)
	{
		FTDFluidDenseWaterSeed Seed;
		const int32 Resolution = Solver.GetResolution();
		Seed.Resolution = Resolution;
		Seed.CenterCm = Solver.GetCenterWorld();
		Seed.ExtentCm = Solver.GetExtentCm();
		Seed.DepthCm.SetNumZeroed(Resolution * Resolution);
		Seed.FaceVelocityXCmPerSec.SetNumZeroed(Resolution * Resolution);
		Seed.FaceVelocityYCmPerSec.SetNumZeroed(Resolution * Resolution);
		const FVector2D HalfCell = Seed.ExtentCm / (2.0 * Resolution);
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const int32 Idx = CellY * Resolution + CellX;
				const FVector2D Center = Solver.CellToWorldCenter(CellX, CellY);
				Seed.DepthCm[Idx] = Depth(Center);
				Seed.FaceVelocityXCmPerSec[Idx] = static_cast<float>(FaceVelocity(Center + FVector2D(HalfCell.X, 0.0)).X);
				Seed.FaceVelocityYCmPerSec[Idx] = static_cast<float>(FaceVelocity(Center + FVector2D(0.0, HalfCell.Y)).Y);
			}
		}
		return Seed;
	}

	FVector2D NoFaceVelocity(const FVector2D&)
	{
		return FVector2D::ZeroVector;
	}

	FVector2D ComputeCellVelocity(const FTDFluidDenseWaterSeed& State, int32 CellX, int32 CellY)
	{
		const int32 Resolution = State.Resolution;
		const int32 Idx = CellY * Resolution + CellX;
		const float LeftU = CellX > 0 ? State.FaceVelocityXCmPerSec[Idx - 1] : 0.0f;
		const float BottomV = CellY > 0 ? State.FaceVelocityYCmPerSec[Idx - Resolution] : 0.0f;
		return FVector2D(0.5f * (LeftU + State.FaceVelocityXCmPerSec[Idx]), 0.5f * (BottomV + State.FaceVelocityYCmPerSec[Idx]));
	}

	struct FDenseFlowSample
	{
		double MassWeightedSpeed = 0.0;
		double CenterX = 0.0;
		double MeanVelocityX = 0.0;
		double FrontX = -TNumericLimits<double>::Max();
	};

	FDenseFlowSample MeasureFlow(const FTDFluidGrid2DSolver& Solver)
	{
		FDenseFlowSample Sample;
		FTDFluidDenseWaterSeed State;
		if (!Solver.ExportDenseWaterState(State))
		{
			return Sample;
		}

		double MassSum = 0.0;
		for (int32 CellY = 0; CellY < State.Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < State.Resolution; ++CellX)
			{
				const double Depth = State.DepthCm[CellY * State.Resolution + CellX];
				if (Depth <= 0.0)
				{
					continue;
				}
				const FVector2D Velocity = ComputeCellVelocity(State, CellX, CellY);
				if (Depth > WetThresholdCm)
				{
					Sample.FrontX = FMath::Max(Sample.FrontX, Solver.CellToWorldCenter(CellX, CellY).X);
				}
				MassSum += Depth;
				Sample.MassWeightedSpeed += Depth * Velocity.Size();
				Sample.MeanVelocityX += Depth * Velocity.X;
				Sample.CenterX += Depth * Solver.CellToWorldCenter(CellX, CellY).X;
			}
		}

		if (MassSum <= 0.0)
		{
			return Sample;
		}
		Sample.MassWeightedSpeed /= MassSum;
		Sample.MeanVelocityX /= MassSum;
		Sample.CenterX /= MassSum;
		return Sample;
	}

	double SumDepthWhere(const FTDFluidGrid2DSolver& Solver, TFunctionRef<bool(const FVector2D&)> Predicate, int32& OutCellCount)
	{
		OutCellCount = 0;
		FTDFluidDenseWaterSeed State;
		if (!Solver.ExportDenseWaterState(State))
		{
			return 0.0;
		}

		double DepthSum = 0.0;
		for (int32 CellY = 0; CellY < State.Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < State.Resolution; ++CellX)
			{
				if (!Predicate(Solver.CellToWorldCenter(CellX, CellY)))
				{
					continue;
				}
				DepthSum += State.DepthCm[CellY * State.Resolution + CellX];
				++OutCellCount;
			}
		}
		return DepthSum;
	}

	float BowlElevation(const FVector2D& Position)
	{
		return 0.0004f * static_cast<float>(Position.SizeSquared());
	}

	float HalfLakeDepth(const FVector2D& Position)
	{
		return Position.X < 0.0 ? FMath::Max(0.0f, 25.0f - BowlElevation(Position)) : 0.0f;
	}

	FTDFluidBrushStamp MakeWaterStamp(const FVector2D& Location, const FVector2D& Velocity, float RadiusCm, float RippleImpulseCm)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = FVector(Location.X, Location.Y, 0.0);
		Stamp.WorldVelocity = FVector(Velocity.X, Velocity.Y, 0.0);
		Stamp.RadiusCm = RadiusCm;
		Stamp.DensityStrength = 0.5f;
		Stamp.VelocityStrength = 1.0f;
		Stamp.RippleImpulseCm = RippleImpulseCm;
		Stamp.Channel = ETDFluidBrushChannel::ChannelA_Water;
		return Stamp;
	}

	FTDFluidWaterRegion MakeRegion(const FVector2D& MinXY, const FVector2D& MaxXY, float SurfaceHeightCm)
	{
		FTDFluidWaterRegion Region;
		Region.MinXY = MinXY;
		Region.MaxXY = MaxXY;
		Region.SurfaceHeightCm = SurfaceHeightCm;
		return Region;
	}

	FTDFluidSimulationConfig MakeSparseConfig()
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = 64;
		Config.SimExtentCm = FVector2D(1200.0, 1200.0);
		Config.VelocityDamping = 0.02f;
		Config.PressureIterations = 12;
		Config.VorticityStrength = 0.3f;
		Config.bEnableWaterLayer = true;
		Config.WhitewaterFromVorticity = 0.15f;
		Config.WhitewaterVorticityThresholdPerSec = 2.0f;
		Config.RippleWaveSpeedCmPerSec = 300.0f;
		return Config;
	}

	TPair<uint32, uint32> RunSparseScene(const FTDFluidSimulationConfig& Config)
	{
		FTDFluidGrid2DSolver Solver;
		InitializeWithTerrain(Solver, Config, [](const FVector2D& Position) { return -100.0f + 0.02f * static_cast<float>(Position.X); });
		Solver.SetWaterRegions({ MakeRegion(FVector2D(-1000.0, -1000.0), FVector2D(0.0, 1000.0), 0.0f) });
		for (int32 StepIndex = 0; StepIndex < 90; ++StepIndex)
		{
			Solver.ApplyBrushStamps({ MakeWaterStamp(FVector2D(-300.0 + 4.0 * StepIndex, 0.0), FVector2D(150.0, 0.0), 80.0f, 1.0f) }, StepSeconds);
			Solver.StepSimulation(StepSeconds);
		}
		return TPair<uint32, uint32>(Solver.ComputeWaterStateChecksum(), Solver.ComputeStateChecksum());
	}

	float SaddleElevation(const FVector2D& Position)
	{
		if (Position.X < 150.0)
		{
			return 14.0f;
		}
		if (Position.X < 170.0)
		{
			return FMath::Abs(Position.Y) < 250.0 ? 25.0f : 40.0f;
		}
		return 0.0f;
	}

	double RunSaddleScene(float PeakAvoidance)
	{
		FTDFluidSimulationConfig Config = MakeDenseConfig(64, 640.0f, false);
		Config.DenseWaterPeakAvoidance = PeakAvoidance;
		Config.DenseWaterFilmDrainCmPerSec = 0.0f;
		FTDFluidGrid2DSolver Solver;
		InitializeWithTerrain(Solver, Config, SaddleElevation);
		Solver.SetWaterRegions({ MakeRegion(FVector2D(-400.0, -400.0), FVector2D(150.0, 400.0), 15.0f) });
		Solver.SetDenseWaterSeed(MakeSeed(Solver,
			[](const FVector2D& Position) { return Position.X < 150.0 ? 1.0f : 0.0f; },
			[](const FVector2D& Position) { return Position.X >= 110.0 && Position.X <= 150.0 ? FVector2D(250.0, 0.0) : FVector2D::ZeroVector; }));
		for (int32 StepIndex = 0; StepIndex < 600; ++StepIndex)
		{
			Solver.StepSimulation(StepSeconds);
		}

		int32 OutsideCellCount = 0;
		return SumDepthWhere(Solver, [](const FVector2D& Position) { return Position.X > 170.0; }, OutsideCellCount);
	}

	TPair<uint32, uint32> RunReplayScene()
	{
		FTDFluidSimulationConfig Config = MakeDenseConfig(64, 640.0f, true);
		Config.RippleWaveSpeedCmPerSec = 300.0f;
		Config.WhitewaterFromVorticity = 0.15f;
		FTDFluidGrid2DSolver Solver;
		InitializeWithTerrain(Solver, Config, BowlElevation);
		Solver.SetDenseWaterSeed(MakeSeed(Solver, HalfLakeDepth, NoFaceVelocity));
		for (int32 StepIndex = 0; StepIndex < 300; ++StepIndex)
		{
			const double Angle = 0.05 * StepIndex;
			const FVector2D Location(120.0 * FMath::Cos(Angle), 120.0 * FMath::Sin(Angle));
			const FVector2D Velocity(-120.0 * FMath::Sin(Angle), 120.0 * FMath::Cos(Angle));
			Solver.ApplyBrushStamps({ MakeWaterStamp(Location, Velocity, 60.0f, 1.0f) }, StepSeconds);
			Solver.StepSimulation(StepSeconds);
		}
		return TPair<uint32, uint32>(Solver.ComputeWaterStateChecksum(), Solver.ComputeStateChecksum());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidDenseWaterPlanStepAndGravityScaleTest,
	"TDGame.FluidFX.DenseWater.PlanStepAndGravityScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidDenseWaterPlanStepAndGravityScaleTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidDenseWaterTestPrivate;

	FTDFluidGpuDenseWaterSettings Settings;
	Settings.GravityCmPerSec2 = 980.0f;
	Settings.MaxFlowDepthCm = 200.0f;
	Settings.MaxSpeedCmPerSec = 400.0f;
	FTDFluidGpuDenseWaterRules::Sanitize(Settings);
	const float DeltaSeconds = 1.0f / 60.0f;

	const float CoarseCellCm = 10.0f;
	const FTDFluidGpuDenseWaterStepPlan CoarsePlan = FTDFluidGpuDenseWaterRules::PlanStep(Settings, DeltaSeconds, CoarseCellCm);
	TestEqual(TEXT("A 10 cm cell needs three substeps"), CoarsePlan.SubstepCount, 3);
	TestFalse(TEXT("Three substeps stay under the cap"), CoarsePlan.bReachedCap);
	TestEqual(TEXT("Gravity is not scaled below the cap"), CoarsePlan.GravityScale, 1.0f);
	TestEqual(TEXT("Substep length splits the frame"), CoarsePlan.SubstepSeconds, DeltaSeconds / 3.0f, 1.0e-6f);
	TestTrue(TEXT("Face speed cap stays at or below the configured cap"), CoarsePlan.MaxSpeedCmPerSec <= Settings.MaxSpeedCmPerSec);
	TestTrue(TEXT("Face speed cap respects the Courant limit"), CoarsePlan.MaxSpeedCmPerSec * CoarsePlan.SubstepSeconds / CoarseCellCm <= Settings.CourantNumber + GravityWaveCourantTolerance);

	const float FineCellCm = 2.4f;
	const FTDFluidGpuDenseWaterStepPlan FinePlan = FTDFluidGpuDenseWaterRules::PlanStep(Settings, DeltaSeconds, FineCellCm);
	TestEqual(TEXT("A 2.4 cm cell is clamped to the substep cap"), FinePlan.SubstepCount, Settings.MaxSubsteps);
	TestTrue(TEXT("The capped plan reports the cap"), FinePlan.bReachedCap);
	TestTrue(TEXT("Gravity is scaled down at the cap"), FinePlan.GravityScale < 1.0f && FinePlan.GravityScale > 0.0f);
	TestTrue(TEXT("Scaled gravity wave Courant number stays at or below one half"), ComputeGravityWaveCourant(Settings, FinePlan, FineCellCm) <= FTDFluidGpuDenseWaterRules::GravityWaveCourantLimit + GravityWaveCourantTolerance);
	TestTrue(TEXT("Capped face speed respects the Courant limit"), FinePlan.MaxSpeedCmPerSec * FinePlan.SubstepSeconds / FineCellCm <= Settings.CourantNumber + GravityWaveCourantTolerance);

	const FTDFluidGpuDenseWaterStepPlan IdlePlan = FTDFluidGpuDenseWaterRules::PlanStep(Settings, 0.0f, CoarseCellCm);
	TestEqual(TEXT("A zero frame keeps one substep"), IdlePlan.SubstepCount, 1);
	TestEqual(TEXT("A zero frame keeps gravity unscaled"), IdlePlan.GravityScale, 1.0f);

	TestEqual(TEXT("Tier 512 halves the dense grid"), FTDFluidGpuDenseWaterRules::ResolveDenseDivisor(512), 2);
	TestEqual(TEXT("Tier 256 keeps the dense grid"), FTDFluidGpuDenseWaterRules::ResolveDenseDivisor(256), 1);
	TestEqual(TEXT("A downwind bed under the upwind surface does not block"), FTDFluidGpuDenseWaterRules::ComputePeakAvoidanceFactor(10.0f, 5.0f, 1.0f, 2.0f), 1.0f);
	TestEqual(TEXT("A downwind bed a full softness above the surface blocks"), FTDFluidGpuDenseWaterRules::ComputePeakAvoidanceFactor(10.0f, 12.0f, 1.0f, 2.0f), 0.0f);
	TestEqual(TEXT("A slow outflow is not limited"), FTDFluidGpuDenseWaterRules::ComputeOutflowScale(10.0f, 0.05f), 1.0f);
	TestEqual(TEXT("A fast outflow is limited to the cell content"), FTDFluidGpuDenseWaterRules::ComputeOutflowScale(40.0f, 0.05f), 0.5f, 1.0e-6f);
	TestEqual(TEXT("A zero relax rate never blends"), FTDFluidGpuDenseWaterRules::ComputeReservoirBlend(0.0f, 0.1f), 0.0f);

	FTDFluidGpuDenseWaterSettings OutOfRange;
	OutOfRange.MaxSubsteps = 99;
	OutOfRange.CourantNumber = 5.0f;
	FTDFluidGpuDenseWaterRules::Sanitize(OutOfRange);
	TestEqual(TEXT("Sanitize caps substeps"), OutOfRange.MaxSubsteps, 16);
	TestEqual(TEXT("Sanitize caps the Courant number"), OutOfRange.CourantNumber, 0.9f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidDenseWaterDisabledIsBitIdenticalTest,
	"TDGame.FluidFX.DenseWater.DisabledIsBitIdentical",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidDenseWaterDisabledIsBitIdenticalTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidDenseWaterTestPrivate;

	FConsoleIntOverride DenseWaterSwitch(DenseWaterVariableName, 1);
	const FTDFluidSimulationConfig BaseConfig = MakeSparseConfig();
	const TPair<uint32, uint32> Baseline = RunSparseScene(BaseConfig);

	FTDFluidSimulationConfig TunedConfig = BaseConfig;
	TunedConfig.bEnableDenseWater = false;
	TunedConfig.DenseWaterGravityCmPerSec2 = 700.0f;
	TunedConfig.DenseWaterGapFillTendency = 0.5f;
	TunedConfig.DenseWaterPeakAvoidance = 0.5f;
	TunedConfig.DenseWaterPeakSoftnessCm = 5.0f;
	TunedConfig.DenseWaterSdfRepelCmPerSec2 = 100.0f;
	TunedConfig.DenseWaterSdfRepelBandCm = 50.0f;
	TunedConfig.DenseWaterLinearDragPerSec = 1.0f;
	TunedConfig.DenseWaterBedFriction = 0.05f;
	TunedConfig.DenseWaterWetThresholdCm = 2.0f;
	TunedConfig.DenseWaterDryDepthCm = 0.1f;
	TunedConfig.DenseWaterFilmDrainCmPerSec = 1.0f;
	TunedConfig.DenseWaterMaxFlowDepthCm = 100.0f;
	TunedConfig.DenseWaterMaxSpeedCmPerSec = 200.0f;
	TunedConfig.DenseWaterCourantNumber = 0.3f;
	TunedConfig.DenseWaterMaxSubsteps = 8;
	TunedConfig.DenseWaterMomentumAdvection = 0.5f;
	TunedConfig.DenseWaterReservoirRelaxPerSec = 2.0f;
	const TPair<uint32, uint32> Tuned = RunSparseScene(TunedConfig);
	TestEqual(TEXT("Dense fields with the switch off keep the water checksum"), Tuned.Key, Baseline.Key);
	TestEqual(TEXT("Dense fields with the switch off keep the solver checksum"), Tuned.Value, Baseline.Value);

	FTDFluidSimulationConfig ConsoleOffConfig = BaseConfig;
	ConsoleOffConfig.bEnableDenseWater = true;
	DenseWaterSwitch.SetValue(0);
	const TPair<uint32, uint32> ConsoleOff = RunSparseScene(ConsoleOffConfig);
	TestEqual(TEXT("The console switch at zero keeps the water checksum"), ConsoleOff.Key, Baseline.Key);
	TestEqual(TEXT("The console switch at zero keeps the solver checksum"), ConsoleOff.Value, Baseline.Value);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidDenseWaterDownhillAcceleratesUphillDeceleratesTest,
	"TDGame.FluidFX.DenseWater.DownhillAcceleratesUphillDecelerates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidDenseWaterDownhillAcceleratesUphillDeceleratesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidDenseWaterTestPrivate;

	FConsoleIntOverride DenseWaterSwitch(DenseWaterVariableName, 1);
	const FTDFluidSimulationConfig Config = MakeDenseConfig(128, 1280.0f, false);
	FTDFluidGrid2DSolver Solver;
	InitializeWithTerrain(Solver, Config, [](const FVector2D& Position) { return 0.1f * FMath::Abs(static_cast<float>(Position.X - DownhillValleyX)); });
	if (!TestTrue(TEXT("Dense water is active"), Solver.IsDenseWaterActive()))
	{
		return false;
	}

	Solver.SetDenseWaterSeed(MakeSeed(Solver, [](const FVector2D& Position) { return Position.X >= -600.0 && Position.X <= -300.0 ? 20.0f : 0.0f; }, NoFaceVelocity));
	TArray<FDenseFlowSample> Samples;
	for (int32 StepIndex = 1; StepIndex <= 720; ++StepIndex)
	{
		Solver.StepSimulation(StepSeconds);
		if (StepIndex % SamplingIntervalSteps == 0)
		{
			Samples.Add(MeasureFlow(Solver));
		}
	}

	int32 DescentCount = 0;
	bool bDescentKeepsSpeed = true;
	for (int32 SampleIndex = 0; SampleIndex < Samples.Num() && Samples[SampleIndex].FrontX < DownhillValleyX; ++SampleIndex)
	{
		if (SampleIndex > 0 && Samples[SampleIndex].MassWeightedSpeed < Samples[SampleIndex - 1].MassWeightedSpeed * 0.95)
		{
			bDescentKeepsSpeed = false;
			AddInfo(FString::Printf(TEXT("Descent sample %d speed %.2f after %.2f"), SampleIndex, Samples[SampleIndex].MassWeightedSpeed, Samples[SampleIndex - 1].MassWeightedSpeed));
		}
		DescentCount = SampleIndex + 1;
	}

	int32 RiseStart = INDEX_NONE;
	for (int32 SampleIndex = 0; SampleIndex < Samples.Num(); ++SampleIndex)
	{
		if (RiseStart == INDEX_NONE || Samples[SampleIndex].MeanVelocityX > Samples[RiseStart].MeanVelocityX)
		{
			RiseStart = SampleIndex;
		}
	}

	const double PeakMeanVelocityX = RiseStart != INDEX_NONE ? Samples[RiseStart].MeanVelocityX : 0.0;
	int32 RiseCount = 0;
	bool bRiseLosesSpeed = true;
	for (int32 SampleIndex = RiseStart; SampleIndex != INDEX_NONE && SampleIndex < Samples.Num(); ++SampleIndex)
	{
		if (Samples[SampleIndex].MeanVelocityX <= 0.5 * PeakMeanVelocityX)
		{
			break;
		}
		if (SampleIndex > RiseStart && Samples[SampleIndex].MassWeightedSpeed > Samples[SampleIndex - 1].MassWeightedSpeed)
		{
			bRiseLosesSpeed = false;
			AddInfo(FString::Printf(TEXT("Rise sample %d speed %.2f after %.2f"), SampleIndex, Samples[SampleIndex].MassWeightedSpeed, Samples[SampleIndex - 1].MassWeightedSpeed));
		}
		++RiseCount;
	}

	const double StartSpeed = Samples.Num() > 0 ? Samples[0].MassWeightedSpeed : 0.0;
	const double DescentEndSpeed = DescentCount > 0 ? Samples[DescentCount - 1].MassWeightedSpeed : 0.0;
	AddInfo(FString::Printf(TEXT("Speed at 0.25 s %.2f, at descent end %.2f, descent samples %d, rise samples %d"), StartSpeed, DescentEndSpeed, DescentCount, RiseCount));
	TestTrue(TEXT("The blob descends for several samples"), DescentCount >= 3);
	TestTrue(TEXT("Speed never falls below 95 percent while descending"), bDescentKeepsSpeed);
	TestTrue(TEXT("Speed at the descent end is at least twice the speed at 0.25 s"), StartSpeed > 0.0 && DescentEndSpeed >= 2.0 * StartSpeed);
	TestTrue(TEXT("The blob climbs for several samples"), RiseCount >= 2);
	TestTrue(TEXT("Speed decreases monotonically while climbing"), bRiseLosesSpeed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidDenseWaterSaddleIsNotClimbedTest,
	"TDGame.FluidFX.DenseWater.SaddleIsNotClimbed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidDenseWaterSaddleIsNotClimbedTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidDenseWaterTestPrivate;

	FConsoleIntOverride DenseWaterSwitch(DenseWaterVariableName, 1);
	const double OutsideWithAvoidance = RunSaddleScene(1.0f);
	const double OutsideWithoutAvoidance = RunSaddleScene(0.0f);
	AddInfo(FString::Printf(TEXT("Outside depth sum with peak avoidance %.3f cm, without %.3f cm"), OutsideWithAvoidance, OutsideWithoutAvoidance));
	TestTrue(TEXT("Peak avoidance keeps the water behind the saddle"), OutsideWithAvoidance < 0.5);
	TestTrue(TEXT("Without peak avoidance the water spills over the saddle"), OutsideWithoutAvoidance >= 5.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidDenseWaterFillsOneMeterGapTest,
	"TDGame.FluidFX.DenseWater.FillsOneMeterGap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidDenseWaterFillsOneMeterGapTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidDenseWaterTestPrivate;

	FConsoleIntOverride DenseWaterSwitch(DenseWaterVariableName, 1);
	const FTDFluidSimulationConfig Config = MakeDenseConfig(64, 640.0f, true);
	FTDFluidGrid2DSolver Solver;
	InitializeWithTerrain(Solver, Config, [](const FVector2D& Position)
	{
		if (FMath::Abs(Position.Y) >= 50.0)
		{
			return 200.0f;
		}
		const float ChannelBedCm = -0.05f * static_cast<float>(Position.X);
		return Position.X >= -100.0 && Position.X < 0.0 ? ChannelBedCm - 30.0f : ChannelBedCm;
	});
	Solver.SetWaterRegions({ MakeRegion(FVector2D(-400.0, -50.0), FVector2D(-220.0, 50.0), 26.0f) });
	for (int32 StepIndex = 0; StepIndex < 1200; ++StepIndex)
	{
		Solver.StepSimulation(StepSeconds);
	}

	int32 GrooveCellCount = 0;
	const double GrooveDepthSum = SumDepthWhere(Solver, [](const FVector2D& Position) { return Position.X > -100.0 && Position.X < 0.0 && FMath::Abs(Position.Y) < 50.0; }, GrooveCellCount);
	int32 DownstreamCellCount = 0;
	const double DownstreamDepthSum = SumDepthWhere(Solver, [](const FVector2D& Position) { return Position.X > 200.0 && Position.X < 210.0 && FMath::Abs(Position.Y) < 50.0; }, DownstreamCellCount);
	const double GrooveMeanDepth = GrooveCellCount > 0 ? GrooveDepthSum / GrooveCellCount : 0.0;
	const double DownstreamMeanDepth = DownstreamCellCount > 0 ? DownstreamDepthSum / DownstreamCellCount : 0.0;
	AddInfo(FString::Printf(TEXT("Groove mean depth %.2f cm over %d cells, downstream mean depth %.2f cm over %d cells"), GrooveMeanDepth, GrooveCellCount, DownstreamMeanDepth, DownstreamCellCount));
	TestTrue(TEXT("The groove fills to at least 25 cm"), GrooveMeanDepth >= 25.0);
	TestTrue(TEXT("Water flows on 2 m past the groove"), DownstreamMeanDepth > WetThresholdCm);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidDenseWaterMassConservedClosedBasinTest,
	"TDGame.FluidFX.DenseWater.MassConservedClosedBasin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidDenseWaterMassConservedClosedBasinTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidDenseWaterTestPrivate;

	FConsoleIntOverride DenseWaterSwitch(DenseWaterVariableName, 1);
	FTDFluidSimulationConfig Config = MakeDenseConfig(64, 640.0f, false);
	Config.DenseWaterFilmDrainCmPerSec = 0.0f;
	FTDFluidGrid2DSolver Solver;
	InitializeWithTerrain(Solver, Config, BowlElevation);
	Solver.SetDenseWaterSeed(MakeSeed(Solver, [](const FVector2D& Position) { return (Position - FVector2D(-100.0, 0.0)).SizeSquared() < 120.0 * 120.0 ? 20.0f : 0.0f; }, NoFaceVelocity));
	const double StartVolume = Solver.ComputeDenseWaterVolumeCm3();
	if (!TestTrue(TEXT("The basin starts with water"), StartVolume > 0.0))
	{
		return false;
	}

	for (int32 StepIndex = 0; StepIndex < 600; ++StepIndex)
	{
		Solver.StepSimulation(StepSeconds);
	}

	const double EndVolume = Solver.ComputeDenseWaterVolumeCm3();
	FTDFluidDenseWaterSeed State;
	Solver.ExportDenseWaterState(State);
	float MinimumDepth = TNumericLimits<float>::Max();
	for (const float Depth : State.DepthCm)
	{
		MinimumDepth = FMath::Min(MinimumDepth, Depth);
	}

	const double RelativeChange = FMath::Abs(EndVolume - StartVolume) / StartVolume;
	AddInfo(FString::Printf(TEXT("Volume %.1f -> %.1f cm3, relative change %.6f, minimum depth %.6f"), StartVolume, EndVolume, RelativeChange, MinimumDepth));
	TestTrue(TEXT("Volume changes by at most 0.1 percent"), RelativeChange <= 0.001);
	TestTrue(TEXT("Depth never goes negative"), MinimumDepth >= 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidDenseWaterLakeSettlesFlatTest,
	"TDGame.FluidFX.DenseWater.LakeSettlesFlat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidDenseWaterLakeSettlesFlatTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidDenseWaterTestPrivate;

	FConsoleIntOverride DenseWaterSwitch(DenseWaterVariableName, 1);
	FTDFluidSimulationConfig Config = MakeDenseConfig(64, 640.0f, false);
	Config.DenseWaterFilmDrainCmPerSec = 0.0f;
	FTDFluidGrid2DSolver Solver;
	InitializeWithTerrain(Solver, Config, BowlElevation);
	Solver.SetDenseWaterSeed(MakeSeed(Solver, [](const FVector2D& Position)
	{
		const float LakeDepthCm = FMath::Max(0.0f, 20.0f - BowlElevation(Position));
		const bool bIsInBump = (Position - FVector2D(-100.0, 0.0)).SizeSquared() < 100.0 * 100.0;
		return LakeDepthCm > 0.0f && bIsInBump ? LakeDepthCm + 3.0f : LakeDepthCm;
	}, NoFaceVelocity));
	for (int32 StepIndex = 0; StepIndex < 1800; ++StepIndex)
	{
		Solver.StepSimulation(StepSeconds);
	}

	FTDFluidDenseWaterSeed State;
	Solver.ExportDenseWaterState(State);
	TArray<double> WetSurfaces;
	double MaximumSpeed = 0.0;
	for (int32 CellY = 0; CellY < State.Resolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < State.Resolution; ++CellX)
		{
			const float Depth = State.DepthCm[CellY * State.Resolution + CellX];
			if (Depth <= WetThresholdCm)
			{
				continue;
			}
			WetSurfaces.Add(BowlElevation(Solver.CellToWorldCenter(CellX, CellY)) + Depth);
			MaximumSpeed = FMath::Max(MaximumSpeed, ComputeCellVelocity(State, CellX, CellY).Size());
		}
	}

	if (!TestTrue(TEXT("The lake keeps wet cells"), WetSurfaces.Num() > 0))
	{
		return false;
	}

	double MeanSurface = 0.0;
	for (const double Surface : WetSurfaces)
	{
		MeanSurface += Surface;
	}
	MeanSurface /= WetSurfaces.Num();
	double MaximumDeviation = 0.0;
	for (const double Surface : WetSurfaces)
	{
		MaximumDeviation = FMath::Max(MaximumDeviation, FMath::Abs(Surface - MeanSurface));
	}

	AddInfo(FString::Printf(TEXT("Wet cells %d, mean surface %.3f cm, maximum deviation %.3f cm, maximum speed %.3f cm/s"), WetSurfaces.Num(), MeanSurface, MaximumDeviation, MaximumSpeed));
	TestTrue(TEXT("Every wet cell is within 1 cm of the mean surface"), MaximumDeviation < 1.0);
	TestTrue(TEXT("The lake is nearly still"), MaximumSpeed < 2.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidDenseWaterReservoirHoldsLevelTest,
	"TDGame.FluidFX.DenseWater.ReservoirHoldsLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidDenseWaterReservoirHoldsLevelTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidDenseWaterTestPrivate;

	constexpr float ReservoirSurfaceCm = 20.0f;
	FConsoleIntOverride DenseWaterSwitch(DenseWaterVariableName, 1);
	const FTDFluidSimulationConfig Config = MakeDenseConfig(64, 640.0f, true);
	FTDFluidGrid2DSolver Solver;
	InitializeWithTerrain(Solver, Config, [](const FVector2D& Position)
	{
		if (Position.X < 0.0)
		{
			return 0.0f;
		}
		return Position.X < 10.0 ? 19.0f : 19.0f - 0.05f * static_cast<float>(Position.X - 10.0);
	});
	Solver.SetWaterRegions({ MakeRegion(FVector2D(-400.0, -400.0), FVector2D(0.0, 400.0), ReservoirSurfaceCm) });
	for (int32 StepIndex = 0; StepIndex < 600; ++StepIndex)
	{
		Solver.StepSimulation(StepSeconds);
	}

	FTDFluidDenseWaterSeed State;
	Solver.ExportDenseWaterState(State);
	int32 RegionCellCount = 0;
	double MaximumDeviation = 0.0;
	for (int32 CellY = 0; CellY < State.Resolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < State.Resolution; ++CellX)
		{
			if (Solver.CellToWorldCenter(CellX, CellY).X >= 0.0)
			{
				continue;
			}
			++RegionCellCount;
			MaximumDeviation = FMath::Max(MaximumDeviation, static_cast<double>(FMath::Abs(State.DepthCm[CellY * State.Resolution + CellX] - ReservoirSurfaceCm)));
		}
	}

	int32 DownstreamCellCount = 0;
	const double DownstreamDepthSum = SumDepthWhere(Solver, [](const FVector2D& Position) { return Position.X > 10.0; }, DownstreamCellCount);
	AddInfo(FString::Printf(TEXT("Region cells %d, maximum surface deviation %.3f cm, downstream depth sum %.3f cm"), RegionCellCount, MaximumDeviation, DownstreamDepthSum));
	TestTrue(TEXT("The region has cells"), RegionCellCount > 0);
	TestTrue(TEXT("Region cells hold the reservoir surface within 1 cm"), MaximumDeviation <= 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidDenseWaterDeterministicReplayTest,
	"TDGame.FluidFX.DenseWater.DeterministicReplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidDenseWaterDeterministicReplayTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidDenseWaterTestPrivate;

	FConsoleIntOverride DenseWaterSwitch(DenseWaterVariableName, 1);
	FConsoleIntOverride RowParallel(RowParallelVariableName, 1);
	const TPair<uint32, uint32> First = RunReplayScene();
	const TPair<uint32, uint32> Second = RunReplayScene();
	RowParallel.SetValue(0);
	const TPair<uint32, uint32> Serial = RunReplayScene();
	TestEqual(TEXT("Two parallel runs give the same water checksum"), Second.Key, First.Key);
	TestEqual(TEXT("Two parallel runs give the same solver checksum"), Second.Value, First.Value);
	TestEqual(TEXT("Serial rows give the same water checksum"), Serial.Key, First.Key);
	TestEqual(TEXT("Serial rows give the same solver checksum"), Serial.Value, First.Value);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidDenseWaterSurfaceEncodingAndDryRippleTest,
	"TDGame.FluidFX.DenseWater.SurfaceEncodingAndDryRipple",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidDenseWaterSurfaceEncodingAndDryRippleTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidDenseWaterTestPrivate;

	constexpr float DepthReferenceCm = 100.0f;
	FConsoleIntOverride DenseWaterSwitch(DenseWaterVariableName, 1);
	FTDFluidSimulationConfig Config = MakeDenseConfig(64, 640.0f, false);
	Config.RippleWaveSpeedCmPerSec = 300.0f;
	Config.WaterDepthReferenceCm = DepthReferenceCm;
	FTDFluidGrid2DSolver Solver;
	InitializeWithTerrain(Solver, Config, BowlElevation);
	Solver.SetDenseWaterSeed(MakeSeed(Solver, HalfLakeDepth, NoFaceVelocity));
	for (int32 StepIndex = 0; StepIndex < 15; ++StepIndex)
	{
		if (StepIndex < 5)
		{
			Solver.ApplyBrushStamps({ MakeWaterStamp(FVector2D(-220.0, 0.0), FVector2D::ZeroVector, 150.0f, 3.0f) }, StepSeconds);
		}
		Solver.StepSimulation(StepSeconds);
	}

	TArray<FFloat16Color> Pixels;
	Solver.PackWaterSurfacePixels(Pixels, 0.0f);
	FTDFluidDenseWaterSeed State;
	Solver.ExportDenseWaterState(State);
	if (!TestEqual(TEXT("One pixel per cell"), Pixels.Num(), State.DepthCm.Num()))
	{
		return false;
	}

	int32 WetCellCount = 0;
	int32 DryCellCount = 0;
	int32 RippledWetCellCount = 0;
	int32 WetMismatchCount = 0;
	int32 DryMismatchCount = 0;
	for (int32 Idx = 0; Idx < Pixels.Num(); ++Idx)
	{
		const float Depth = State.DepthCm[Idx];
		const float EncodedDepth = Pixels[Idx].R.GetFloat();
		const float EncodedRipple = Pixels[Idx].B.GetFloat();
		if (Depth > WetThresholdCm)
		{
			++WetCellCount;
			const float ExpectedDepth = FMath::Clamp(Depth / DepthReferenceCm, 0.0f, 1.0f);
			WetMismatchCount += FMath::Abs(EncodedDepth - ExpectedDepth) > 2.0e-3f ? 1 : 0;
			RippledWetCellCount += FMath::Abs(EncodedRipple - 0.5f) > 1.0e-3f ? 1 : 0;
			continue;
		}

		++DryCellCount;
		DryMismatchCount += EncodedDepth != 0.0f || EncodedRipple != 0.5f ? 1 : 0;
	}

	AddInfo(FString::Printf(TEXT("Wet %d (rippled %d, mismatched %d), dry %d (mismatched %d)"), WetCellCount, RippledWetCellCount, WetMismatchCount, DryCellCount, DryMismatchCount));
	TestTrue(TEXT("The scene has wet cells"), WetCellCount > 0);
	TestTrue(TEXT("The scene has dry cells"), DryCellCount > 0);
	TestTrue(TEXT("Wet cells carry ripples"), RippledWetCellCount > 0);
	TestEqual(TEXT("Wet cells encode depth over the reference"), WetMismatchCount, 0);
	TestEqual(TEXT("Dry cells encode zero depth and a flat ripple"), DryMismatchCount, 0);
	return true;
}

#endif
