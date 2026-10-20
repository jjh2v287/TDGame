#if WITH_DEV_AUTOMATION_TESTS

#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"

namespace TDFluidSolverBoundsTestPrivate
{
	constexpr float BoundsTestStepSeconds = 1.0f / 60.0f;
	constexpr int32 BoundsTestResolution = 64;
	constexpr double BoundsTestExtentCm = 640.0;

	FTDFluidSimulationConfig MakeBoundsTestConfig()
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = BoundsTestResolution;
		Config.SimExtentCm = FVector2D(BoundsTestExtentCm, BoundsTestExtentCm);
		Config.VelocityDamping = 0.0f;
		Config.DensityDissipation = 0.0f;
		Config.PressureIterations = 8;
		Config.VorticityStrength = 0.0f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.BorderFadeRatio = 0.0f;
		Config.MaxPointsPerTick = 512;
		return Config;
	}

	FTDFluidBrushStamp MakeVelocityStamp(const FVector2D& Center, float RadiusCm, const FVector2D& Velocity, float DensityStrength = 0.0f)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = FVector(Center.X, Center.Y, 0.0);
		Stamp.WorldVelocity = FVector(Velocity.X, Velocity.Y, 0.0);
		Stamp.RadiusCm = RadiusCm;
		Stamp.DensityStrength = DensityStrength;
		Stamp.VelocityStrength = 1.0f;
		return Stamp;
	}

	double MeasureLeftColumnVelocityX(const FTDFluidGrid2DSolver& Solver)
	{
		double Total = 0.0;
		for (int32 CellY = 0; CellY < BoundsTestResolution; ++CellY)
		{
			Total += Solver.SampleAtCell(0, CellY).Velocity.X;
		}
		return Total / BoundsTestResolution;
	}

	double MeasureAngularMomentum(const FTDFluidGrid2DSolver& Solver)
	{
		double Total = 0.0;
		for (int32 CellY = 0; CellY < BoundsTestResolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < BoundsTestResolution; ++CellX)
			{
				const FVector2D Offset = Solver.CellToWorldCenter(CellX, CellY) - Solver.GetCenterWorld();
				const FVector2D Velocity = Solver.SampleAtCell(CellX, CellY).Velocity;
				Total += Offset.X * Velocity.Y - Offset.Y * Velocity.X;
			}
		}
		return Total;
	}

	double MeasureMeanDensityRadius(const FTDFluidGrid2DSolver& Solver)
	{
		double Weight = 0.0;
		double WeightedRadius = 0.0;
		for (int32 CellY = 0; CellY < BoundsTestResolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < BoundsTestResolution; ++CellX)
			{
				const double Density = Solver.SampleAtCell(CellX, CellY).PrimaryDensity;
				Weight += Density;
				WeightedRadius += Density * (Solver.CellToWorldCenter(CellX, CellY) - Solver.GetCenterWorld()).Size();
			}
		}
		return Weight > UE_DOUBLE_SMALL_NUMBER ? WeightedRadius / Weight : 0.0;
	}

	uint32 ComputeOutputChecksum(const FTDFluidGrid2DSolver& Solver)
	{
		TArray<FFloat16Color> DensityPressurePixels;
		TArray<FFloat16Color> VelocityPixels;
		Solver.PackOutputPixels(DensityPressurePixels, VelocityPixels);
		return FCrc::MemCrc32(DensityPressurePixels.GetData(), DensityPressurePixels.Num() * sizeof(FFloat16Color), FCrc::MemCrc32(VelocityPixels.GetData(), VelocityPixels.Num() * sizeof(FFloat16Color)));
	}

	void PushTowardLeftEdge(FTDFluidGrid2DSolver& Solver, int32 StepCount)
	{
		TArray<FTDFluidBrushStamp> Stamps;
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			Stamps.Reset();
			for (int32 Row = -2; Row <= 2; ++Row)
			{
				Stamps.Add(MakeVelocityStamp(FVector2D(-0.5 * BoundsTestExtentCm + 20.0, Row * 60.0), 60.0f, FVector2D(-400.0, 0.0), 0.6f));
			}
			Solver.ApplyBrushStamps(Stamps, BoundsTestStepSeconds);
			Solver.StepSimulation(BoundsTestStepSeconds);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidEdgeReflectionTest,
	"TDGame.FluidFX.SolverBounds.EdgeBounceReflects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidEdgeReflectionTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverBoundsTestPrivate;
	FTDFluidSimulationConfig OpenConfig = MakeBoundsTestConfig();
	FTDFluidSimulationConfig ReflectingConfig = OpenConfig;
	ReflectingConfig.bReflectsAtBoundaries = true;
	ReflectingConfig.EdgeBounciness = 0.5f;
	FTDFluidSimulationConfig SwitchOffConfig = OpenConfig;
	SwitchOffConfig.EdgeBounciness = 0.5f;

	FTDFluidGrid2DSolver OpenSolver;
	OpenSolver.Initialize(OpenConfig, FVector2D::ZeroVector);
	PushTowardLeftEdge(OpenSolver, 6);
	FTDFluidGrid2DSolver ReflectingSolver;
	ReflectingSolver.Initialize(ReflectingConfig, FVector2D::ZeroVector);
	PushTowardLeftEdge(ReflectingSolver, 6);
	FTDFluidGrid2DSolver SwitchOffSolver;
	SwitchOffSolver.Initialize(SwitchOffConfig, FVector2D::ZeroVector);
	PushTowardLeftEdge(SwitchOffSolver, 6);

	const double OpenEdgeVelocity = MeasureLeftColumnVelocityX(OpenSolver);
	const double ReflectingEdgeVelocity = MeasureLeftColumnVelocityX(ReflectingSolver);
	AddInfo(FString::Printf(TEXT("TDFluidBounds: open_edge_vx=%.3f reflecting_edge_vx=%.3f"), OpenEdgeVelocity, ReflectingEdgeVelocity));
	TestTrue(TEXT("Reflection removes the outward edge flow that the open edge keeps"), ReflectingEdgeVelocity > OpenEdgeVelocity);
	TestTrue(TEXT("Reflection leaves no net outward flow on the edge column"), ReflectingEdgeVelocity >= -1.0);
	TestEqual(TEXT("A bounciness value without the switch keeps the state checksum"), SwitchOffSolver.ComputeStateChecksum(), OpenSolver.ComputeStateChecksum());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidObstacleReflectionTest,
	"TDGame.FluidFX.SolverBounds.ObstacleBounceReflects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidObstacleReflectionTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverBoundsTestPrivate;
	auto MeasureApproachVelocity = [](bool bReflects)
	{
		FTDFluidSimulationConfig Config = MakeBoundsTestConfig();
		Config.bReflectsAtBoundaries = bReflects;
		Config.EdgeBounciness = 1.0f;
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		FTDFluidSdfObstacle Obstacle;
		Obstacle.WorldCenter = FVector(80.0, 0.0, 0.0);
		Obstacle.RadiusCm = 60.0f;
		Solver.RasterizeSdfObstacles({ Obstacle });

		TArray<FTDFluidBrushStamp> Stamps;
		for (int32 StepIndex = 0; StepIndex < 4; ++StepIndex)
		{
			Stamps.Reset();
			Stamps.Add(MakeVelocityStamp(FVector2D(-40.0, 0.0), 70.0f, FVector2D(500.0, 0.0), 0.5f));
			Solver.ApplyBrushStamps(Stamps, BoundsTestStepSeconds);
			Solver.StepSimulation(BoundsTestStepSeconds);
		}

		double Total = 0.0;
		int32 Count = 0;
		for (int32 CellY = 0; CellY < BoundsTestResolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < BoundsTestResolution; ++CellX)
			{
				const FTDFluidCellSample Sample = Solver.SampleAtCell(CellX, CellY);
				if (Sample.ObstacleSdf <= 0.0f || Sample.ObstacleSdf > 15.0f)
				{
					continue;
				}

				const FVector2D Outward = (Solver.CellToWorldCenter(CellX, CellY) - FVector2D(80.0, 0.0)).GetSafeNormal();
				Total += FVector2D::DotProduct(Sample.Velocity, Outward);
				++Count;
			}
		}
		return Count > 0 ? Total / Count : 0.0;
	};

	const double RemovedNormal = MeasureApproachVelocity(false);
	const double ReflectedNormal = MeasureApproachVelocity(true);
	AddInfo(FString::Printf(TEXT("TDFluidBounds: obstacle_band_normal_removed=%.3f reflected=%.3f"), RemovedNormal, ReflectedNormal));
	TestTrue(TEXT("Reflection pushes the band flow away from the obstacle more than normal removal"), ReflectedNormal > RemovedNormal);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidBorderFadeExponentTest,
	"TDGame.FluidFX.SolverBounds.BorderFadeExponentOneIsIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidBorderFadeExponentTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverBoundsTestPrivate;
	auto MakeSolver = [](float Exponent, FTDFluidGrid2DSolver& OutSolver)
	{
		FTDFluidSimulationConfig Config = MakeBoundsTestConfig();
		Config.BorderFadeRatio = 0.2f;
		Config.BorderFadeExponent = Exponent;
		OutSolver.Initialize(Config, FVector2D::ZeroVector);
		TArray<FTDFluidBrushStamp> Stamps;
		Stamps.Add(MakeVelocityStamp(FVector2D::ZeroVector, 300.0f, FVector2D(100.0, 0.0), 0.7f));
		OutSolver.ApplyBrushStamps(Stamps, BoundsTestStepSeconds);
		OutSolver.StepSimulation(BoundsTestStepSeconds);
	};

	FTDFluidGrid2DSolver DefaultSolver;
	MakeSolver(1.0f, DefaultSolver);
	FTDFluidGrid2DSolver SquaredSolver;
	MakeSolver(2.0f, SquaredSolver);
	TestEqual(TEXT("The state does not depend on the output alpha exponent"), SquaredSolver.ComputeStateChecksum(), DefaultSolver.ComputeStateChecksum());

	TArray<FFloat16Color> DefaultPixels;
	TArray<FFloat16Color> DefaultVelocity;
	DefaultSolver.PackOutputPixels(DefaultPixels, DefaultVelocity);
	TArray<FFloat16Color> SquaredPixels;
	TArray<FFloat16Color> SquaredVelocity;
	SquaredSolver.PackOutputPixels(SquaredPixels, SquaredVelocity);
	double MaximumAlphaError = 0.0;
	for (int32 PixelIndex = 0; PixelIndex < DefaultPixels.Num(); ++PixelIndex)
	{
		const double DefaultAlpha = DefaultPixels[PixelIndex].A.GetFloat();
		MaximumAlphaError = FMath::Max(MaximumAlphaError, FMath::Abs(SquaredPixels[PixelIndex].A.GetFloat() - DefaultAlpha * DefaultAlpha));
		TestTrue(TEXT("Only the alpha channel changes"), SquaredPixels[PixelIndex].R == DefaultPixels[PixelIndex].R);
	}
	AddInfo(FString::Printf(TEXT("TDFluidBounds: border_alpha_square_max_error=%.5f"), MaximumAlphaError));
	TestTrue(TEXT("Exponent two squares the border alpha within half-float rounding"), MaximumAlphaError <= 2.0e-3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidVortexForceTest,
	"TDGame.FluidFX.SolverBounds.VortexSpinsAndInverts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidVortexForceTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverBoundsTestPrivate;
	auto RunVortex = [](ETDFluidPresetKind Kind, const FVector2D& CenterWorld, double& OutAngularMomentum, double& OutOutsideRatio)
	{
		FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(Kind);
		Config.GridResolution = BoundsTestResolution;
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, CenterWorld);
		for (int32 StepIndex = 0; StepIndex < 30; ++StepIndex)
		{
			Solver.StepSimulation(BoundsTestStepSeconds);
		}

		const double VortexRadiusCm = Config.VortexRadiusRatio * FMath::Min(Config.SimExtentCm.X, Config.SimExtentCm.Y);
		double InsideMaximum = 0.0;
		double OutsideMaximum = 0.0;
		for (int32 CellY = 0; CellY < BoundsTestResolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < BoundsTestResolution; ++CellX)
			{
				const double Distance = (Solver.CellToWorldCenter(CellX, CellY) - Solver.GetCenterWorld()).Size();
				const double Speed = Solver.SampleAtCell(CellX, CellY).Velocity.Size();
				if (Distance < VortexRadiusCm)
				{
					InsideMaximum = FMath::Max(InsideMaximum, Speed);
				}
				else if (Distance > 1.5 * VortexRadiusCm)
				{
					OutsideMaximum = FMath::Max(OutsideMaximum, Speed);
				}
			}
		}
		OutAngularMomentum = MeasureAngularMomentum(Solver);
		OutOutsideRatio = InsideMaximum > UE_DOUBLE_SMALL_NUMBER ? OutsideMaximum / InsideMaximum : 1.0;
		return Solver.ComputeStateChecksum();
	};

	double SpinMomentum = 0.0;
	double SpinOutsideRatio = 0.0;
	double InvertedMomentum = 0.0;
	double InvertedOutsideRatio = 0.0;
	double ShiftedMomentum = 0.0;
	double ShiftedOutsideRatio = 0.0;
	RunVortex(ETDFluidPresetKind::Vortex, FVector2D::ZeroVector, SpinMomentum, SpinOutsideRatio);
	RunVortex(ETDFluidPresetKind::InvertedVortex, FVector2D::ZeroVector, InvertedMomentum, InvertedOutsideRatio);
	RunVortex(ETDFluidPresetKind::Vortex, FVector2D(1234.0, -567.0), ShiftedMomentum, ShiftedOutsideRatio);
	AddInfo(FString::Printf(TEXT("TDFluidBounds: vortex_momentum=%.1f inverted=%.1f shifted=%.1f outside_ratio=%.3f"), SpinMomentum, InvertedMomentum, ShiftedMomentum, SpinOutsideRatio));
	TestTrue(TEXT("Positive tangential acceleration spins counterclockwise"), SpinMomentum > 0.0);
	TestTrue(TEXT("The inverted vortex spins the other way"), InvertedMomentum < 0.0);
	TestTrue(TEXT("Flow outside the vortex radius stays weak"), SpinOutsideRatio <= 0.35);
	TestTrue(TEXT("The vortex is window relative and moves with the window"), FMath::Abs(ShiftedMomentum - SpinMomentum) <= 1.0e-3 * FMath::Abs(SpinMomentum));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPortalDriftTest,
	"TDGame.FluidFX.SolverBounds.PortalDriftIsInward",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPortalDriftTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverBoundsTestPrivate;
	auto MeasureRadiusChange = [](ETDFluidPresetKind Kind)
	{
		FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(Kind);
		Config.GridResolution = BoundsTestResolution;
		Config.VortexTangentialAccelerationCmPerSec2 = 0.0f;
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		TArray<FTDFluidBrushStamp> Stamps;
		for (int32 RingIndex = 0; RingIndex < 16; ++RingIndex)
		{
			const double Angle = UE_DOUBLE_TWO_PI * RingIndex / 16.0;
			FTDFluidBrushStamp Stamp = MakeVelocityStamp(FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * 120.0, 30.0f, FVector2D::ZeroVector, 0.8f);
			Stamp.VelocityStrength = 0.0f;
			Stamps.Add(Stamp);
		}
		Solver.ApplyBrushStamps(Stamps, BoundsTestStepSeconds);
		const double InitialRadius = MeasureMeanDensityRadius(Solver);
		for (int32 StepIndex = 0; StepIndex < 30; ++StepIndex)
		{
			Solver.StepSimulation(BoundsTestStepSeconds);
		}
		return MeasureMeanDensityRadius(Solver) - InitialRadius;
	};

	const double PortalChange = MeasureRadiusChange(ETDFluidPresetKind::PortalVortex);
	const double InvertedChange = MeasureRadiusChange(ETDFluidPresetKind::InvertedVortex);
	AddInfo(FString::Printf(TEXT("TDFluidBounds: portal_radius_change_cm=%.2f inverted_radius_change_cm=%.2f"), PortalChange, InvertedChange));
	TestTrue(TEXT("The portal drift moves density inward"), PortalChange < -5.0);
	TestTrue(TEXT("The inverted drift moves density outward"), InvertedChange > 0.0);
	return true;
}

#endif
