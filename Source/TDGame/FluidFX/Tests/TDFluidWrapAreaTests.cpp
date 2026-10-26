#if WITH_DEV_AUTOMATION_TESTS

#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidTypes.h"
#include "Misc/AutomationTest.h"

namespace TDFluidWrapAreaTestPrivate
{
	constexpr float WrapTestStepSeconds = 1.0f / 60.0f;
	constexpr int32 WrapTestResolution = 64;
	constexpr double WrapTestExtentCm = 3000.0;

	FTDFluidSimulationConfig MakeWrapTestConfig()
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.AreaAddressMode = ETDFluidAreaAddressMode::Wrap;
		Config.GridResolution = WrapTestResolution;
		Config.SimExtentCm = FVector2D(WrapTestExtentCm, WrapTestExtentCm);
		Config.VelocityDamping = 0.02f;
		Config.DensityDissipation = 0.0f;
		Config.PressureIterations = 12;
		Config.VorticityStrength = 0.8f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.BorderFadeRatio = 0.0f;
		Config.MaxPointsPerTick = 64;
		Config.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Config.AmbientNoiseAmount = 0.0f;
		Config.AmbientDensity = 0.0f;
		Config.AmbientRefillPerSec = 0.0f;
		return Config;
	}

	FTDFluidSimulationConfig MakeNoisyWrapTestConfig()
	{
		FTDFluidSimulationConfig Config = MakeWrapTestConfig();
		Config.FlowNoiseAccelerationCmPerSec2 = 150.0f;
		Config.FlowNoiseScaleCm = 400.0f;
		Config.AmbientDensity = 0.5f;
		Config.AmbientRefillPerSec = 0.3f;
		Config.AmbientNoiseAmount = 0.6f;
		return Config;
	}

	FTDFluidBrushStamp MakeWrapTestStamp(const FVector2D& Location, const FVector2D& Velocity, float RadiusCm)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = FVector(Location.X, Location.Y, 0.0);
		Stamp.WorldVelocity = FVector(Velocity.X, Velocity.Y, 0.0);
		Stamp.RadiusCm = RadiusCm;
		Stamp.DensityStrength = 1.0f;
		Stamp.VelocityStrength = 1.0f;
		return Stamp;
	}

	void AppendPeriodicGhostStamps(TArray<FTDFluidBrushStamp>& Stamps, const FTDFluidGrid2DSolver& Solver)
	{
		const FVector2D Extent = Solver.GetExtentCm();
		const FVector2D MinWorld = Solver.GetCenterWorld() - 0.5 * Extent;
		const int32 OriginalCount = Stamps.Num();
		for (int32 StampIndex = 0; StampIndex < OriginalCount; ++StampIndex)
		{
			const FTDFluidBrushStamp Original = Stamps[StampIndex];
			for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
			{
				for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
				{
					if (OffsetX == 0 && OffsetY == 0)
					{
						continue;
					}
					FTDFluidBrushStamp Ghost = Original;
					Ghost.WorldLocation.X += OffsetX * Extent.X;
					Ghost.WorldLocation.Y += OffsetY * Extent.Y;
					const bool bTouchesX = Ghost.WorldLocation.X >= MinWorld.X - Ghost.RadiusCm && Ghost.WorldLocation.X <= MinWorld.X + Extent.X + Ghost.RadiusCm;
					const bool bTouchesY = Ghost.WorldLocation.Y >= MinWorld.Y - Ghost.RadiusCm && Ghost.WorldLocation.Y <= MinWorld.Y + Extent.Y + Ghost.RadiusCm;
					if (bTouchesX && bTouchesY)
					{
						Stamps.Add(Ghost);
					}
				}
			}
		}
	}

	void RunWrapTestSteps(FTDFluidGrid2DSolver& Solver, const TArray<FTDFluidBrushStamp>& Stamps, int32 StampStepCount, int32 StepCount)
	{
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			if (StepIndex < StampStepCount)
			{
				Solver.ApplyBrushStamps(Stamps, WrapTestStepSeconds);
			}
			Solver.StepSimulation(WrapTestStepSeconds);
		}
	}

	double ComputePercentile(TArray<double> Values, double Fraction)
	{
		if (Values.IsEmpty())
		{
			return 0.0;
		}
		Values.Sort();
		const int32 Index = FMath::Clamp(FMath::FloorToInt(Fraction * (Values.Num() - 1)), 0, Values.Num() - 1);
		return Values[Index];
	}

	void CollectSeamAndInteriorDifferences(const TArray<float>& Field, int32 Resolution, double& OutMaxSeamDifference, double& OutInteriorPercentile)
	{
		TArray<double> InteriorDifferences;
		OutMaxSeamDifference = 0.0;
		for (int32 Line = 0; Line < Resolution; ++Line)
		{
			const double SeamAlongX = FMath::Abs(Field[Line * Resolution] - Field[Line * Resolution + Resolution - 1]);
			const double SeamAlongY = FMath::Abs(Field[Line] - Field[(Resolution - 1) * Resolution + Line]);
			OutMaxSeamDifference = FMath::Max(OutMaxSeamDifference, FMath::Max(SeamAlongX, SeamAlongY));
			for (int32 Step = 0; Step < Resolution - 1; ++Step)
			{
				InteriorDifferences.Add(FMath::Abs(Field[Line * Resolution + Step + 1] - Field[Line * Resolution + Step]));
				InteriorDifferences.Add(FMath::Abs(Field[(Step + 1) * Resolution + Line] - Field[Step * Resolution + Line]));
			}
		}
		OutInteriorPercentile = ComputePercentile(MoveTemp(InteriorDifferences), 0.99);
	}

	TArray<float> CollectDensityField(const FTDFluidGrid2DSolver& Solver)
	{
		const int32 Resolution = Solver.GetResolution();
		TArray<float> Field;
		Field.SetNumUninitialized(Resolution * Resolution);
		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				Field[Y * Resolution + X] = Solver.SampleAtCell(X, Y).PrimaryDensity;
			}
		}
		return Field;
	}

	double ComputeKineticEnergyNearCell(const FTDFluidGrid2DSolver& Solver, int32 CenterX, int32 CenterY, int32 RadiusCells)
	{
		const int32 Resolution = Solver.GetResolution();
		double Energy = 0.0;
		for (int32 OffsetY = -RadiusCells; OffsetY <= RadiusCells; ++OffsetY)
		{
			for (int32 OffsetX = -RadiusCells; OffsetX <= RadiusCells; ++OffsetX)
			{
				if (OffsetX * OffsetX + OffsetY * OffsetY > RadiusCells * RadiusCells)
				{
					continue;
				}
				const int32 X = (CenterX + OffsetX + Resolution) % Resolution;
				const int32 Y = (CenterY + OffsetY + Resolution) % Resolution;
				Energy += Solver.SampleAtCell(X, Y).Velocity.SizeSquared();
			}
		}
		return Energy;
	}

	TArray<FTDFluidBrushStamp> MakeSwirlStamps(const FTDFluidGrid2DSolver& Solver, int32 CenterX, int32 CenterY)
	{
		const FVector2D Center = Solver.CellToWorldCenter(CenterX, CenterY);
		const double CellSizeCm = WrapTestExtentCm / WrapTestResolution;
		TArray<FTDFluidBrushStamp> Stamps;
		for (int32 SpokeIndex = 0; SpokeIndex < 8; ++SpokeIndex)
		{
			const double Angle = SpokeIndex * UE_DOUBLE_PI / 4.0;
			const FVector2D Radial(FMath::Cos(Angle), FMath::Sin(Angle));
			const FVector2D Tangent(-Radial.Y, Radial.X);
			Stamps.Add(MakeWrapTestStamp(Center + Radial * (4.0 * CellSizeCm), Tangent * 400.0, static_cast<float>(2.0 * CellSizeCm)));
		}
		AppendPeriodicGhostStamps(Stamps, Solver);
		return Stamps;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWrapAreaRollInvarianceTest,
	"TDGame.FluidFX.WrapArea.RollInvariance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWrapAreaRollInvarianceTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWrapAreaTestPrivate;
	constexpr int32 ShiftX = 13;
	constexpr int32 ShiftY = 7;
	const FTDFluidSimulationConfig Config = MakeWrapTestConfig();
	FTDFluidGrid2DSolver Original;
	FTDFluidGrid2DSolver Rolled;
	Original.Initialize(Config, FVector2D::ZeroVector);
	Rolled.Initialize(Config, FVector2D::ZeroVector);

	TArray<FTDFluidBrushStamp> OriginalStamps = { MakeWrapTestStamp(Original.CellToWorldCenter(61, 30), FVector2D(400.0, 150.0), 150.0f) };
	TArray<FTDFluidBrushStamp> RolledStamps = { MakeWrapTestStamp(Rolled.CellToWorldCenter((61 + ShiftX) % WrapTestResolution, 30 + ShiftY), FVector2D(400.0, 150.0), 150.0f) };
	AppendPeriodicGhostStamps(OriginalStamps, Original);
	AppendPeriodicGhostStamps(RolledStamps, Rolled);
	TestTrue(TEXT("Seam stamp gets a ghost copy"), OriginalStamps.Num() > 1);
	RunWrapTestSteps(Original, OriginalStamps, 10, 30);
	RunWrapTestSteps(Rolled, RolledStamps, 10, 30);

	double MaxDensity = 0.0;
	double MaxSpeed = 0.0;
	double MaxDensityDifference = 0.0;
	double MaxVelocityDifference = 0.0;
	for (int32 Y = 0; Y < WrapTestResolution; ++Y)
	{
		for (int32 X = 0; X < WrapTestResolution; ++X)
		{
			const FTDFluidCellSample A = Original.SampleAtCell(X, Y);
			const FTDFluidCellSample B = Rolled.SampleAtCell((X + ShiftX) % WrapTestResolution, (Y + ShiftY) % WrapTestResolution);
			MaxDensity = FMath::Max(MaxDensity, static_cast<double>(FMath::Abs(A.PrimaryDensity)));
			MaxSpeed = FMath::Max(MaxSpeed, A.Velocity.Size());
			MaxDensityDifference = FMath::Max(MaxDensityDifference, static_cast<double>(FMath::Abs(A.PrimaryDensity - B.PrimaryDensity)));
			MaxVelocityDifference = FMath::Max(MaxVelocityDifference, (A.Velocity - B.Velocity).Size());
		}
	}
	TestTrue(TEXT("Stamps produced density"), MaxDensity > 0.01);
	TestTrue(TEXT("Stamps produced motion"), MaxSpeed > 1.0);
	TestTrue(*FString::Printf(TEXT("Rolled density matches (diff %g of %g)"), MaxDensityDifference, MaxDensity), MaxDensityDifference <= 1.0e-4 * MaxDensity);
	TestTrue(*FString::Printf(TEXT("Rolled velocity matches (diff %g of %g)"), MaxVelocityDifference, MaxSpeed), MaxVelocityDifference <= 1.0e-4 * MaxSpeed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWrapAreaSeamContinuityTest,
	"TDGame.FluidFX.WrapArea.SeamContinuity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWrapAreaSeamContinuityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWrapAreaTestPrivate;
	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(MakeNoisyWrapTestConfig(), FVector2D::ZeroVector);
	RunWrapTestSteps(Solver, {}, 0, 120);

	double MaxSeamDifference = 0.0;
	double InteriorPercentile = 0.0;
	CollectSeamAndInteriorDifferences(CollectDensityField(Solver), Solver.GetResolution(), MaxSeamDifference, InteriorPercentile);
	TestTrue(TEXT("Noise produced a varying field"), InteriorPercentile > 0.0);
	TestTrue(*FString::Printf(TEXT("Seam density step %g stays within interior p99 %g x1.5"), MaxSeamDifference, InteriorPercentile), MaxSeamDifference <= InteriorPercentile * 1.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWrapAreaVortexCrossesSeamTest,
	"TDGame.FluidFX.WrapArea.VortexCrossesSeam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWrapAreaVortexCrossesSeamTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWrapAreaTestPrivate;
	const FTDFluidSimulationConfig Config = MakeWrapTestConfig();
	FTDFluidGrid2DSolver SeamSolver;
	FTDFluidGrid2DSolver InteriorSolver;
	SeamSolver.Initialize(Config, FVector2D::ZeroVector);
	InteriorSolver.Initialize(Config, FVector2D::ZeroVector);
	RunWrapTestSteps(SeamSolver, MakeSwirlStamps(SeamSolver, 0, 0), 8, 30);
	RunWrapTestSteps(InteriorSolver, MakeSwirlStamps(InteriorSolver, 32, 32), 8, 30);

	const double SeamEnergy = ComputeKineticEnergyNearCell(SeamSolver, 0, 0, 8);
	const double InteriorEnergy = ComputeKineticEnergyNearCell(InteriorSolver, 32, 32, 8);
	TestTrue(TEXT("Interior vortex has energy"), InteriorEnergy > 1.0);
	const double Ratio = InteriorEnergy > 0.0 ? SeamEnergy / InteriorEnergy : 0.0;
	TestTrue(*FString::Printf(TEXT("Vortex across the corner seam keeps its energy (ratio %g)"), Ratio), FMath::Abs(Ratio - 1.0) <= 0.05);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWrapAreaPositionIndependenceTest,
	"TDGame.FluidFX.WrapArea.PositionIndependence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWrapAreaPositionIndependenceTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWrapAreaTestPrivate;
	const FTDFluidSimulationConfig Config = MakeNoisyWrapTestConfig();
	FTDFluidGrid2DSolver AtOrigin;
	FTDFluidGrid2DSolver FarAway;
	AtOrigin.Initialize(Config, FVector2D::ZeroVector);
	FarAway.Initialize(Config, FVector2D(123456.0, -98765.0));
	RunWrapTestSteps(AtOrigin, {}, 0, 20);
	RunWrapTestSteps(FarAway, {}, 0, 20);
	double MaxDifference = 0.0;
	double MaxDensity = 0.0;
	for (int32 Y = 0; Y < AtOrigin.GetResolution(); ++Y)
	{
		for (int32 X = 0; X < AtOrigin.GetResolution(); ++X)
		{
			const FTDFluidCellSample A = AtOrigin.SampleAtCell(X, Y);
			const FTDFluidCellSample B = FarAway.SampleAtCell(X, Y);
			MaxDensity = FMath::Max(MaxDensity, static_cast<double>(A.PrimaryDensity));
			MaxDifference = FMath::Max(MaxDifference, static_cast<double>(FMath::Abs(A.PrimaryDensity - B.PrimaryDensity)));
			MaxDifference = FMath::Max(MaxDifference, static_cast<double>(FMath::Abs(A.Pressure - B.Pressure)));
			MaxDifference = FMath::Max(MaxDifference, (A.Velocity - B.Velocity).GetAbsMax());
		}
	}
	TestTrue(TEXT("Noise produced density"), MaxDensity > 0.0);
	TestEqual(TEXT("Wrapped fields do not depend on the window position"), MaxDifference, 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWrapAreaNoiseLatticeIsPeriodicTest,
	"TDGame.FluidFX.WrapArea.NoiseLatticeIsPeriodic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWrapAreaNoiseLatticeIsPeriodicTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWrapAreaTestPrivate;
	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(MakeNoisyWrapTestConfig(), FVector2D(777.0, -333.0));
	TArray<FVector2D> Acceleration;
	Solver.ComputeFlowNoiseAccelerationField(Acceleration, true);
	const int32 Resolution = Solver.GetResolution();
	if (!TestEqual(TEXT("Noise field covers the grid"), Acceleration.Num(), Resolution * Resolution))
	{
		return false;
	}
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		TArray<float> Component;
		Component.SetNumUninitialized(Acceleration.Num());
		for (int32 Index = 0; Index < Acceleration.Num(); ++Index)
		{
			Component[Index] = static_cast<float>(Axis == 0 ? Acceleration[Index].X : Acceleration[Index].Y);
		}
		double MaxSeamDifference = 0.0;
		double InteriorPercentile = 0.0;
		CollectSeamAndInteriorDifferences(Component, Resolution, MaxSeamDifference, InteriorPercentile);
		TestTrue(TEXT("Noise field varies"), InteriorPercentile > 0.0);
		TestTrue(*FString::Printf(TEXT("Noise axis %d seam step %g within interior p99 %g x1.5"), Axis, MaxSeamDifference, InteriorPercentile), MaxSeamDifference <= InteriorPercentile * 1.5);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWrapAreaSanitizeAppliesRulesTest,
	"TDGame.FluidFX.WrapArea.SanitizeAppliesRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWrapAreaSanitizeAppliesRulesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWrapAreaTestPrivate;
	FTDFluidSimulationConfig Request = MakeWrapTestConfig();
	Request.SimMode = ETDFluidSimMode::SimplePainter;
	Request.GridResolution = 192;
	Request.bEnableWaterLayer = true;
	Request.bEnableWaterWetness = true;
	Request.bReflectsAtBoundaries = true;
	Request.TerrainSlopeGravity = 400.0f;
	Request.OutflowBandRatio = 0.2f;
	Request.BorderFadeRatio = 0.1f;

	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Request, FVector2D::ZeroVector);
	const FTDFluidSimulationConfig& Applied = Solver.GetConfig();
	TestTrue(TEXT("Wrap keeps the fluid solver"), Applied.SimMode == ETDFluidSimMode::FluidSim);
	TestEqual(TEXT("Wrap rounds the resolution up to a power of two"), Solver.GetResolution(), 256);
	TestFalse(TEXT("Wrap turns the water layer off"), Applied.bEnableWaterLayer);
	TestFalse(TEXT("Wrap turns wetness off"), Applied.bEnableWaterWetness);
	TestFalse(TEXT("Wrap turns boundary reflection off"), Applied.bReflectsAtBoundaries);
	TestEqual(TEXT("Wrap turns terrain slope gravity off"), Applied.TerrainSlopeGravity, 0.0f);
	TestEqual(TEXT("Wrap turns the outflow band off"), Applied.OutflowBandRatio, 0.0f);
	TestEqual(TEXT("Wrap turns the border fade off"), Applied.BorderFadeRatio, 0.0f);
	TestTrue(TEXT("Wrapped solver reports wrapping"), Solver.IsAreaWrapped());
	TestFalse(TEXT("Wrapped solver refuses to slide its window"), Solver.ShiftOriginForSlidingWindow(FVector2D(500.0, 0.0)));

	FTDFluidGrid2DSolver Again;
	Again.Initialize(Applied, FVector2D::ZeroVector);
	TestEqual(TEXT("Sanitized resolution is stable"), Again.GetResolution(), Solver.GetResolution());
	TestTrue(TEXT("Sanitized mode is stable"), Again.GetConfig().SimMode == Applied.SimMode);

	const int32 GenerationBeforeUpdate = Solver.GetReinitializationGeneration();
	Solver.UpdateConfig(Request);
	TestEqual(TEXT("Repeating the same wrapped request does not reinitialize"), Solver.GetReinitializationGeneration(), GenerationBeforeUpdate);
	FTDFluidSimulationConfig ClampRequest = Request;
	ClampRequest.AreaAddressMode = ETDFluidAreaAddressMode::Clamp;
	ClampRequest.GridResolution = 256;
	Solver.UpdateConfig(ClampRequest);
	TestTrue(TEXT("Switching the address mode reinitializes"), Solver.GetReinitializationGeneration() > GenerationBeforeUpdate);
	TestFalse(TEXT("Clamp solver no longer wraps"), Solver.IsAreaWrapped());
	return true;
}

#endif
