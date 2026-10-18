#if WITH_DEV_AUTOMATION_TESTS

#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#include <limits>

namespace TDFluidSolverRegressionPrivate
{
	FTDFluidSimulationConfig MakeRegressionConfig(ETDFluidPresetKind Kind, int32 Resolution, const FVector2D& ExtentCm)
	{
		FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(Kind);
		Config.GridResolution = Resolution;
		Config.SimExtentCm = ExtentCm;
		return Config;
	}

	FTDFluidBrushStamp MakeRegressionStamp(const FVector& Location, const FVector& Velocity, float RadiusCm)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = Location;
		Stamp.WorldVelocity = Velocity;
		Stamp.RadiusCm = RadiusCm;
		Stamp.DensityStrength = 1.0f;
		Stamp.VelocityStrength = 1.0f;
		Stamp.Channel = ETDFluidBrushChannel::ChannelR_Mud;
		return Stamp;
	}

	float MeasureMaxSpeedCmPerSec(const FTDFluidGrid2DSolver& Solver)
	{
		float MaxSpeed = 0.0f;
		const int32 Resolution = Solver.GetResolution();
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				MaxSpeed = FMath::Max(MaxSpeed, static_cast<float>(Solver.SampleAtCell(CellX, CellY).Velocity.Size()));
			}
		}
		return MaxSpeed;
	}

	float MeasureMaxAbsPressure(const FTDFluidGrid2DSolver& Solver)
	{
		float MaxPressure = 0.0f;
		const int32 Resolution = Solver.GetResolution();
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				MaxPressure = FMath::Max(MaxPressure, FMath::Abs(Solver.SampleAtCell(CellX, CellY).Pressure));
			}
		}
		return MaxPressure;
	}

	float SumPrimaryDensity(const FTDFluidGrid2DSolver& Solver)
	{
		float Sum = 0.0f;
		const int32 Resolution = Solver.GetResolution();
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				Sum += Solver.SampleAtCell(CellX, CellY).PrimaryDensity;
			}
		}
		return Sum;
	}

	float MeasureDivergenceRms(const FTDFluidGrid2DSolver& Solver)
	{
		const int32 Resolution = Solver.GetResolution();
		const FVector2D Extent = Solver.GetExtentCm();
		const double CellSizeX = Extent.X / Resolution;
		const double CellSizeY = Extent.Y / Resolution;
		constexpr int32 BorderMarginCells = 3;

		double SumSquared = 0.0;
		int32 SampleCount = 0;
		for (int32 CellY = BorderMarginCells; CellY < Resolution - BorderMarginCells; ++CellY)
		{
			for (int32 CellX = BorderMarginCells; CellX < Resolution - BorderMarginCells; ++CellX)
			{
				const double DuDx = (Solver.SampleAtCell(CellX + 1, CellY).Velocity.X - Solver.SampleAtCell(CellX - 1, CellY).Velocity.X) / (2.0 * CellSizeX);
				const double DvDy = (Solver.SampleAtCell(CellX, CellY + 1).Velocity.Y - Solver.SampleAtCell(CellX, CellY - 1).Velocity.Y) / (2.0 * CellSizeY);
				SumSquared += FMath::Square(DuDx + DvDy);
				++SampleCount;
			}
		}
		return static_cast<float>(FMath::Sqrt(SumSquared / SampleCount));
	}

	bool AreAllCellsFinite(const FTDFluidGrid2DSolver& Solver)
	{
		const int32 Resolution = Solver.GetResolution();
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const FTDFluidCellSample Sample = Solver.SampleAtCell(CellX, CellY);
				const bool bIsFinite = FMath::IsFinite(Sample.PrimaryDensity)
					&& FMath::IsFinite(Sample.SelectiveDensity)
					&& FMath::IsFinite(Sample.Pressure)
					&& FMath::IsFinite(Sample.Velocity.X)
					&& FMath::IsFinite(Sample.Velocity.Y);
				if (!bIsFinite)
				{
					return false;
				}
			}
		}
		return true;
	}

	void CheckProjectionResidual(FAutomationTestBase& Test, const FVector2D& ExtentCm, float StampRadiusCm, float MaxResidualRatio)
	{
		FTDFluidSimulationConfig Config = MakeRegressionConfig(ETDFluidPresetKind::Mud, 64, ExtentCm);
		Config.VelocityDamping = 0.0f;
		Config.DensityDissipation = 0.0f;
		Config.VorticityStrength = 0.0f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.BorderFadeRatio = 0.0f;
		Config.PressureIterations = 32;

		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		Solver.ApplyBrushStamps({ MakeRegressionStamp(FVector::ZeroVector, FVector(500.0f, 500.0f, 0.0f), StampRadiusCm) }, 0.1f);

		const float ResidualBefore = MeasureDivergenceRms(Solver);
		Solver.StepSimulation(1.0f / 60.0f);
		const float ResidualAfter = MeasureDivergenceRms(Solver);
		const float ResidualRatio = ResidualBefore > UE_SMALL_NUMBER ? ResidualAfter / ResidualBefore : 1.0f;

		Test.AddInfo(FString::Printf(TEXT("extent=%.0fx%.0f radius=%.1f divergenceBefore=%.4f after=%.4f ratio=%.4f"), ExtentCm.X, ExtentCm.Y, StampRadiusCm, ResidualBefore, ResidualAfter, ResidualRatio));
		Test.TestTrue(*FString::Printf(TEXT("Velocity stamp creates measurable divergence in %.0fx%.0f (%.4f)"), ExtentCm.X, ExtentCm.Y, ResidualBefore), ResidualBefore > 0.01f);
		Test.TestTrue(
			*FString::Printf(TEXT("One step leaves at most %.2f of the divergence in %.0fx%.0f (ratio %.4f)"), MaxResidualRatio, ExtentCm.X, ExtentCm.Y, ResidualRatio),
			ResidualRatio <= MaxResidualRatio);
	}

	void CheckExtremeConfigStaysClamped(FAutomationTestBase& Test, FTDFluidGrid2DSolver& Solver, const TCHAR* CaseLabel)
	{
		const FTDFluidSimulationConfig& SolverConfig = Solver.GetConfig();
		Test.TestTrue(*FString::Printf(TEXT("%s: extent is at least 100 cm (%.1f x %.1f)"), CaseLabel, SolverConfig.SimExtentCm.X, SolverConfig.SimExtentCm.Y), SolverConfig.SimExtentCm.X >= 100.0 && SolverConfig.SimExtentCm.Y >= 100.0);
		Test.TestTrue(*FString::Printf(TEXT("%s: velocity damping is not negative (%.3f)"), CaseLabel, SolverConfig.VelocityDamping), SolverConfig.VelocityDamping >= 0.0f);
		Test.TestTrue(*FString::Printf(TEXT("%s: density dissipation is not negative (%.3f)"), CaseLabel, SolverConfig.DensityDissipation), SolverConfig.DensityDissipation >= 0.0f);
		Test.TestTrue(*FString::Printf(TEXT("%s: vorticity strength is not negative (%.3f)"), CaseLabel, SolverConfig.VorticityStrength), SolverConfig.VorticityStrength >= 0.0f);

		constexpr float DeltaTime = 1.0f / 60.0f;
		for (int32 Frame = 0; Frame < 30; ++Frame)
		{
			Solver.ApplyBrushStamps({ MakeRegressionStamp(FVector::ZeroVector, FVector(300.0f, 0.0f, 0.0f), 150.0f) }, DeltaTime);
			Solver.StepSimulation(DeltaTime);
		}

		Test.TestTrue(*FString::Printf(TEXT("%s: every cell stays finite after 30 frames"), CaseLabel), AreAllCellsFinite(Solver));
		Test.TestTrue(*FString::Printf(TEXT("%s: no cell exceeds 1200 cm/s"), CaseLabel), MeasureMaxSpeedCmPerSec(Solver) <= 1200.01f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidBrushFrameRateInvarianceTest,
	"TDGame.FluidFX.SolverRegression.BrushFrameRateInvariance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidBrushFrameRateInvarianceTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverRegressionPrivate;

	const FTDFluidSimulationConfig Config = MakeRegressionConfig(ETDFluidPresetKind::Mud, 64, FVector2D(1600.0f, 1600.0f));
	const int32 FramesPerSecondCases[] = { 30, 60, 120 };
	float MaxSpeeds[3] = {};
	float DensitySums[3] = {};

	for (int32 CaseIndex = 0; CaseIndex < 3; ++CaseIndex)
	{
		const int32 FramesPerSecond = FramesPerSecondCases[CaseIndex];
		const float DeltaTime = 1.0f / static_cast<float>(FramesPerSecond);

		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);

		for (int32 Frame = 0; Frame < FramesPerSecond; ++Frame)
		{
			const float ElapsedSeconds = static_cast<float>(Frame) * DeltaTime;
			const FTDFluidBrushStamp Stamp = MakeRegressionStamp(FVector(-300.0f + 600.0f * ElapsedSeconds, 0.0f, 0.0f), FVector(600.0f, 0.0f, 0.0f), 75.0f);
			Solver.ApplyBrushStamps({ Stamp }, DeltaTime);
			Solver.StepSimulation(DeltaTime);
		}

		MaxSpeeds[CaseIndex] = MeasureMaxSpeedCmPerSec(Solver);
		DensitySums[CaseIndex] = SumPrimaryDensity(Solver);
		AddInfo(FString::Printf(TEXT("fps=%d maxSpeed=%.2f densitySum=%.3f"), FramesPerSecond, MaxSpeeds[CaseIndex], DensitySums[CaseIndex]));
		TestTrue(*FString::Printf(TEXT("No cell exceeds 1200 cm/s at %d fps (max %.2f)"), FramesPerSecond, MaxSpeeds[CaseIndex]), MaxSpeeds[CaseIndex] <= 1200.0f);
	}

	for (const int32 CaseIndex : { 0, 2 })
	{
		const int32 FramesPerSecond = FramesPerSecondCases[CaseIndex];
		TestTrue(
			*FString::Printf(TEXT("Max speed at %d fps is within 10%% of 60 fps (%.2f vs %.2f)"), FramesPerSecond, MaxSpeeds[CaseIndex], MaxSpeeds[1]),
			FMath::Abs(MaxSpeeds[CaseIndex] - MaxSpeeds[1]) <= 0.1f * MaxSpeeds[1]);
		TestTrue(
			*FString::Printf(TEXT("Density sum at %d fps is within 10%% of 60 fps (%.3f vs %.3f)"), FramesPerSecond, DensitySums[CaseIndex], DensitySums[1]),
			FMath::Abs(DensitySums[CaseIndex] - DensitySums[1]) <= 0.1f * DensitySums[1]);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidBorderBandFootprintTest,
	"TDGame.FluidFX.SolverRegression.BorderBandRetainsFootprint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidBorderBandFootprintTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverRegressionPrivate;

	const FTDFluidSimulationConfig Config = MakeRegressionConfig(ETDFluidPresetKind::FootprintPainter, 64, FVector2D(3200.0f, 3200.0f));
	const TArray<FIntPoint> BandCells = { FIntPoint(1, 32), FIntPoint(62, 32), FIntPoint(32, 1), FIntPoint(32, 62) };

	for (const int32 FramesPerSecond : { 30, 120 })
	{
		const float DeltaTime = 1.0f / static_cast<float>(FramesPerSecond);

		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);

		TArray<FTDFluidBrushStamp> Stamps;
		for (const FIntPoint& BandCell : BandCells)
		{
			const FVector2D CellCenter = Solver.CellToWorldCenter(BandCell.X, BandCell.Y);
			Stamps.Add(MakeRegressionStamp(FVector(CellCenter.X, CellCenter.Y, 0.0f), FVector::ZeroVector, 30.0f));
		}

		Solver.ApplyBrushStamps(Stamps, DeltaTime);
		for (int32 Frame = 0; Frame < FramesPerSecond; ++Frame)
		{
			Solver.StepSimulation(DeltaTime);
		}

		for (const FIntPoint& BandCell : BandCells)
		{
			const float RetainedDensity = Solver.SampleAtCell(BandCell.X, BandCell.Y).PrimaryDensity;
			AddInfo(FString::Printf(TEXT("fps=%d cell=(%d,%d) density=%.4f"), FramesPerSecond, BandCell.X, BandCell.Y, RetainedDensity));
			TestTrue(
				*FString::Printf(TEXT("Footprint in border band keeps density >= 0.9 after 1 s at %d fps, cell (%d,%d) = %.4f"), FramesPerSecond, BandCell.X, BandCell.Y, RetainedDensity),
				RetainedDensity >= 0.9f);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNonSquareProjectionTest,
	"TDGame.FluidFX.SolverRegression.NonSquareProjectionResidual",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidNonSquareProjectionTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverRegressionPrivate;

	const FVector2D ExtentCases[] = { FVector2D(3200.0f, 3200.0f), FVector2D(1000.0f, 800.0f) };
	for (const FVector2D& ExtentCm : ExtentCases)
	{
		const float SmallestCellCm = static_cast<float>(FMath::Min(ExtentCm.X, ExtentCm.Y)) / 64.0f;
		CheckProjectionResidual(*this, ExtentCm, 4.5f * SmallestCellCm, 0.1f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFourToOneProjectionTest,
	"TDGame.FluidFX.SolverRegression.NonSquareFourToOneProjectionResidual",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFourToOneProjectionTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverRegressionPrivate;

	const FVector2D ExtentCm(2000.0f, 500.0f);
	const float SmallestCellCm = static_cast<float>(FMath::Min(ExtentCm.X, ExtentCm.Y)) / 64.0f;
	CheckProjectionResidual(*this, ExtentCm, 4.5f * SmallestCellCm, 0.12f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidStationaryPressureResidueTest,
	"TDGame.FluidFX.SolverRegression.StationaryStateHasNoPressureResidue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidStationaryPressureResidueTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverRegressionPrivate;

	const FTDFluidSimulationConfig Config = MakeRegressionConfig(ETDFluidPresetKind::Mud, 64, FVector2D(3200.0f, 3200.0f));
	constexpr float StampRadiusCm = 200.0f;

	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Config, FVector2D::ZeroVector);

	FTDFluidBrushStamp DensityOnlyStamp = MakeRegressionStamp(FVector::ZeroVector, FVector::ZeroVector, StampRadiusCm);
	DensityOnlyStamp.VelocityStrength = 0.0f;
	Solver.ApplyBrushStamps({ DensityOnlyStamp }, 1.0f / 60.0f);
	Solver.StepSimulation(1.0f / 60.0f);
	Solver.StepSimulation(1.0f / 60.0f);

	TestTrue(TEXT("Density-only stamp creates no pressure"), MeasureMaxAbsPressure(Solver) < 1e-4f);
	TestTrue(TEXT("Density-only stamp creates no velocity"), MeasureMaxSpeedCmPerSec(Solver) < 1e-3f);

	TArray<FFloat16Color> DensityPressurePixels;
	TArray<FFloat16Color> VelocityPixels;
	Solver.PackOutputPixels(DensityPressurePixels, VelocityPixels);
	const int32 Resolution = Solver.GetResolution();
	int32 FarCellsChecked = 0;
	int32 FarCellsDisturbed = 0;
	for (int32 CellY = 0; CellY < Resolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < Resolution; ++CellX)
		{
			if (FVector2D::Distance(Solver.CellToWorldCenter(CellX, CellY), FVector2D::ZeroVector) <= StampRadiusCm + 100.0f)
			{
				continue;
			}
			++FarCellsChecked;
			if (FMath::Abs(DensityPressurePixels[CellY * Resolution + CellX].G.GetFloat() - 0.5f) > 1.0f / 255.0f)
			{
				++FarCellsDisturbed;
			}
		}
	}
	TestTrue(TEXT("Cells away from the stamp keep G at 0.5"), FarCellsChecked > 0 && FarCellsDisturbed == 0);

	Solver.ApplyBrushStamps({ MakeRegressionStamp(FVector::ZeroVector, FVector(600.0f, 0.0f, 0.0f), StampRadiusCm) }, 1.0f / 60.0f);
	Solver.StepSimulation(1.0f / 60.0f);
	TestTrue(TEXT("Moving stamp precondition creates pressure"), MeasureMaxAbsPressure(Solver) > 1e-3f);

	Solver.ResetSimulation();
	Solver.StepSimulation(1.0f / 60.0f);
	Solver.StepSimulation(1.0f / 60.0f);
	Solver.PackOutputPixels(DensityPressurePixels, VelocityPixels);

	int32 ResetCellsDisturbed = 0;
	for (const FFloat16Color& Pixel : DensityPressurePixels)
	{
		if (FMath::Abs(Pixel.G.GetFloat() - 0.5f) > 1.0f / 255.0f)
		{
			++ResetCellsDisturbed;
		}
	}
	TestTrue(TEXT("Reset then two stationary steps leave zero pressure"), MeasureMaxAbsPressure(Solver) < 1e-4f);
	TestTrue(TEXT("Reset then two stationary steps leave no velocity"), MeasureMaxSpeedCmPerSec(Solver) < 1e-3f);
	TestEqual(TEXT("Reset then two stationary steps keep every G at 0.5"), ResetCellsDisturbed, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidMovingMudDisplacementTest,
	"TDGame.FluidFX.SolverRegression.MovingMudStampDisplacesSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidMovingMudDisplacementTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverRegressionPrivate;

	const FTDFluidSimulationConfig Config = MakeRegressionConfig(ETDFluidPresetKind::Mud, 64, FVector2D(3200.0f, 3200.0f));
	constexpr int32 TrajectoryRow = 32;
	constexpr float DeltaTime = 1.0f / 60.0f;

	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Config, FVector2D::ZeroVector);
	const float TrajectoryY = static_cast<float>(Solver.CellToWorldCenter(0, TrajectoryRow).Y);

	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		const FTDFluidBrushStamp Stamp = MakeRegressionStamp(FVector(-150.0f + 5.0f * static_cast<float>(Frame), TrajectoryY, 0.0f), FVector(300.0f, 0.0f, 0.0f), 100.0f);
		Solver.ApplyBrushStamps({ Stamp }, DeltaTime);
		Solver.StepSimulation(DeltaTime);
	}

	TArray<FFloat16Color> DensityPressurePixels;
	TArray<FFloat16Color> VelocityPixels;
	Solver.PackOutputPixels(DensityPressurePixels, VelocityPixels);

	const int32 Resolution = Solver.GetResolution();
	int32 TrajectoryCells = 0;
	float MinimumDeviation = 1.0f;
	for (int32 CellX = 0; CellX < Resolution; ++CellX)
	{
		const float CellCenterX = static_cast<float>(Solver.CellToWorldCenter(CellX, TrajectoryRow).X);
		if (CellCenterX < -130.0f || CellCenterX > -20.0f)
		{
			continue;
		}
		const float Deviation = FMath::Abs(DensityPressurePixels[TrajectoryRow * Resolution + CellX].G.GetFloat() - 0.5f);
		MinimumDeviation = FMath::Min(MinimumDeviation, Deviation);
		++TrajectoryCells;
	}

	AddInfo(FString::Printf(TEXT("trajectoryCells=%d minimumGDeviation=%.4f"), TrajectoryCells, MinimumDeviation));
	TestTrue(TEXT("Trajectory contains cells"), TrajectoryCells > 0);
	TestTrue(*FString::Printf(TEXT("Trajectory cells move G away from 0.5 by at least 0.15 (min %.4f)"), MinimumDeviation), MinimumDeviation >= 0.15f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNonFiniteStampTest,
	"TDGame.FluidFX.SolverRegression.NonFiniteStampIsDiscarded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidNonFiniteStampTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverRegressionPrivate;

	const double QuietNaN = std::numeric_limits<double>::quiet_NaN();
	const double Infinity = std::numeric_limits<double>::infinity();
	const float FloatNaN = std::numeric_limits<float>::quiet_NaN();
	const float FloatInfinity = std::numeric_limits<float>::infinity();

	const FTDFluidSimulationConfig Config = MakeRegressionConfig(ETDFluidPresetKind::Mud, 64, FVector2D(3200.0f, 3200.0f));
	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Config, FVector2D::ZeroVector);

	TArray<FTDFluidBrushStamp> Stamps;
	Stamps.Add(MakeRegressionStamp(FVector(QuietNaN, 0.0, 0.0), FVector(300.0, 0.0, 0.0), 100.0f));
	Stamps.Add(MakeRegressionStamp(FVector(0.0, 0.0, 0.0), FVector(QuietNaN, Infinity, 0.0), 100.0f));
	Stamps.Add(MakeRegressionStamp(FVector(0.0, 0.0, 0.0), FVector(300.0, 0.0, 0.0), FloatNaN));
	Stamps.Add(MakeRegressionStamp(FVector(Infinity, 0.0, 0.0), FVector(300.0, 0.0, 0.0), 100.0f));

	FTDFluidBrushStamp InfiniteStrengthStamp = MakeRegressionStamp(FVector(0.0, 0.0, 0.0), FVector(300.0, 0.0, 0.0), 100.0f);
	InfiniteStrengthStamp.DensityStrength = FloatInfinity;
	InfiniteStrengthStamp.VelocityStrength = FloatNaN;
	Stamps.Add(InfiniteStrengthStamp);

	Stamps.Add(MakeRegressionStamp(FVector(400.0, 0.0, 0.0), FVector(300.0, 0.0, 0.0), 120.0f));

	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Solver.ApplyBrushStamps(Stamps, 1.0f / 60.0f);
		Solver.StepSimulation(1.0f / 60.0f);
	}

	TestTrue(TEXT("Grid stays finite after non-finite stamps"), AreAllCellsFinite(Solver));

	FTDFluidCellSample ValidStampSample;
	TestTrue(TEXT("Valid stamp location can be sampled"), Solver.SampleAtWorldPosition(FVector2D(400.0f, 0.0f), ValidStampSample));
	TestTrue(TEXT("Valid stamp in the same batch still applies"), ValidStampSample.PrimaryDensity > 0.5f);

	FTDFluidCellSample UntouchedSample;
	Solver.SampleAtWorldPosition(FVector2D(-800.0f, 0.0f), UntouchedSample);
	TestNearlyEqual(TEXT("Non-finite stamps leave the far field empty"), UntouchedSample.PrimaryDensity, 0.0f, 1e-4f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSampleVelocityCmPerSecTest,
	"TDGame.FluidFX.SolverRegression.SampleReportsVelocityCmPerSec",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSampleVelocityCmPerSecTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverRegressionPrivate;

	const FTDFluidSimulationConfig Config = MakeRegressionConfig(ETDFluidPresetKind::Water, 64, FVector2D(3200.0f, 3200.0f));
	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Config, FVector2D::ZeroVector);

	Solver.ApplyBrushStamps({ MakeRegressionStamp(FVector::ZeroVector, FVector(400.0f, 0.0f, 0.0f), 200.0f) }, 0.1f);

	FTDFluidCellSample WorldSample;
	TestTrue(TEXT("Stamp location can be sampled"), Solver.SampleAtWorldPosition(FVector2D::ZeroVector, WorldSample));
	TestTrue(TEXT("World sample reports velocity in VelocityCmPerSec"), WorldSample.VelocityCmPerSec.X > 10.0);
	TestTrue(TEXT("World sample keeps Velocity and VelocityCmPerSec identical"), WorldSample.VelocityCmPerSec.Equals(WorldSample.Velocity, 1e-6));

	const FTDFluidCellSample CellSample = Solver.SampleAtCell(32, 32);
	TestTrue(TEXT("Cell sample reports velocity in VelocityCmPerSec"), CellSample.VelocityCmPerSec.X > 10.0);
	TestTrue(TEXT("Cell sample keeps Velocity and VelocityCmPerSec identical"), CellSample.VelocityCmPerSec.Equals(CellSample.Velocity, 1e-6));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPresetOverrideTest,
	"TDGame.FluidFX.SolverRegression.PresetOverrideResolvesConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPresetOverrideTest::RunTest(const FString& Parameters)
{
	UClass* PresetClass = UTDFluidPresetAsset::StaticClass();
	const FBoolProperty* OverrideProperty = FindFProperty<FBoolProperty>(PresetClass, TEXT("bOverridePresetDefaults"));
	const FSoftObjectProperty* SurfaceMeshProperty = FindFProperty<FSoftObjectProperty>(PresetClass, TEXT("SurfaceMesh"));
	const UFunction* ResolveFunction = PresetClass->FindFunctionByName(TEXT("ResolveEffectiveConfig"));
	const UFunction* BuildFunction = PresetClass->FindFunctionByName(TEXT("BuildConfigForPresetKind"));
	TestNotNull(TEXT("bOverridePresetDefaults is reflected for Python"), OverrideProperty);
	TestNotNull(TEXT("SurfaceMesh is reflected as a soft object property"), SurfaceMeshProperty);
	TestNotNull(TEXT("ResolveEffectiveConfig is callable from Python"), ResolveFunction);
	TestNotNull(TEXT("BuildConfigForPresetKind is callable from Python"), BuildFunction);
	TestTrue(TEXT("bOverridePresetDefaults is editable and visible to Blueprints"), OverrideProperty && OverrideProperty->HasAllPropertyFlags(CPF_Edit | CPF_BlueprintVisible));
	TestTrue(TEXT("SurfaceMesh is editable and visible to Blueprints"), SurfaceMeshProperty && SurfaceMeshProperty->HasAllPropertyFlags(CPF_Edit | CPF_BlueprintVisible));
	TestTrue(TEXT("ResolveEffectiveConfig is Blueprint pure"), ResolveFunction && ResolveFunction->HasAllFunctionFlags(FUNC_BlueprintPure));
	TestTrue(TEXT("BuildConfigForPresetKind is a static Blueprint pure function"), BuildFunction && BuildFunction->HasAllFunctionFlags(FUNC_BlueprintPure | FUNC_Static));

	UTDFluidPresetAsset* Preset = NewObject<UTDFluidPresetAsset>(GetTransientPackage());
	Preset->PresetKind = ETDFluidPresetKind::Mud;
	Preset->SimulationConfig.GridResolution = 128;
	Preset->SimulationConfig.SimExtentCm = FVector2D(1000.0f, 800.0f);
	const FTDFluidSimulationConfig FactoryConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Mud);
	TestTrue(TEXT("Factory Mud resolution differs from the override resolution"), FactoryConfig.GridResolution != 128);
	TestTrue(TEXT("Surface mesh is empty by default"), Preset->SurfaceMesh.IsNull());

	Preset->bOverridePresetDefaults = false;
	const FTDFluidSimulationConfig FactoryResolved = Preset->ResolveEffectiveConfig();
	TestEqual(TEXT("Without override the factory resolution is returned"), FactoryResolved.GridResolution, FactoryConfig.GridResolution);
	TestTrue(TEXT("Without override the factory extent is returned"), FactoryResolved.SimExtentCm.Equals(FactoryConfig.SimExtentCm, 1e-3));

	Preset->bOverridePresetDefaults = true;
	const FTDFluidSimulationConfig OverrideResolved = Preset->ResolveEffectiveConfig();
	TestEqual(TEXT("With override the SimulationConfig resolution is returned"), OverrideResolved.GridResolution, 128);
	TestTrue(TEXT("With override the SimulationConfig extent is returned"), OverrideResolved.SimExtentCm.Equals(FVector2D(1000.0, 800.0), 1e-3));

	Preset->bOverridePresetDefaults = false;
	Preset->PresetKind = ETDFluidPresetKind::Custom;
	TestEqual(TEXT("Custom preset returns SimulationConfig without override"), Preset->ResolveEffectiveConfig().GridResolution, 128);

	TestNearlyEqual(TEXT("Mud factory DensityDissipation"), UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Mud).DensityDissipation, 0.01f, 1e-6f);
	TestNearlyEqual(TEXT("Blood factory DensityDissipation"), UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Blood).DensityDissipation, 0.01f, 1e-6f);
	const FTDFluidSimulationConfig FireConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Fire);
	TestNearlyEqual(TEXT("Fire factory VelocityDamping"), FireConfig.VelocityDamping, 0.15f, 1e-6f);
	TestNearlyEqual(TEXT("Fire factory VorticityStrength"), FireConfig.VorticityStrength, 2.5f, 1e-6f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPressureResolutionTest,
	"TDGame.FluidFX.SolverRegression.DisplayPressureIsResolutionIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPressureResolutionTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverRegressionPrivate;

	const int32 ResolutionCases[] = { 64, 128, 256 };
	constexpr float DeltaTime = 1.0f / 60.0f;
	float MaxPressures[3] = {};

	for (int32 CaseIndex = 0; CaseIndex < 3; ++CaseIndex)
	{
		const FTDFluidSimulationConfig Config = MakeRegressionConfig(ETDFluidPresetKind::Mud, ResolutionCases[CaseIndex], FVector2D(3200.0f, 3200.0f));
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);

		for (int32 Frame = 0; Frame < 30; ++Frame)
		{
			Solver.ApplyBrushStamps({ MakeRegressionStamp(FVector::ZeroVector, FVector(300.0f, 0.0f, 0.0f), 200.0f) }, DeltaTime);
			Solver.StepSimulation(DeltaTime);
		}

		MaxPressures[CaseIndex] = MeasureMaxAbsPressure(Solver);
		AddInfo(FString::Printf(TEXT("resolution=%d maxAbsPressure=%.4f"), ResolutionCases[CaseIndex], MaxPressures[CaseIndex]));
	}

	TestTrue(*FString::Printf(TEXT("Reference pressure at 64 is measurable and not saturated (%.4f)"), MaxPressures[0]), MaxPressures[0] > 0.01f && MaxPressures[0] < 0.9f);
	for (int32 CaseIndex = 1; CaseIndex < 3; ++CaseIndex)
	{
		TestTrue(
			*FString::Printf(TEXT("Max display pressure at %d is within 25%% of 64 (%.4f vs %.4f)"), ResolutionCases[CaseIndex], MaxPressures[CaseIndex], MaxPressures[0]),
			FMath::Abs(MaxPressures[CaseIndex] - MaxPressures[0]) <= 0.25f * MaxPressures[0]);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidEdgeObstacleNormalTest,
	"TDGame.FluidFX.SolverRegression.GridEdgeObstacleNormalIsTangential",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidEdgeObstacleNormalTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverRegressionPrivate;

	struct FEdgeNormalCase
	{
		FIntPoint Cell;
		FVector2D DirectionToObstacle;
	};

	const FEdgeNormalCase EdgeCases[] = {
		{ FIntPoint(0, 32), FVector2D(-1.0, -1.0).GetSafeNormal() },
		{ FIntPoint(63, 32), FVector2D(1.0, 1.0).GetSafeNormal() },
		{ FIntPoint(32, 0), FVector2D(-1.0, -1.0).GetSafeNormal() },
		{ FIntPoint(32, 63), FVector2D(1.0, 1.0).GetSafeNormal() },
	};

	constexpr float ObstacleRadiusCm = 400.0f;
	constexpr float SurfaceGapCm = 30.0f;
	constexpr float IntoObstacleSpeedCmPerSec = 300.0f;
	const FTDFluidSimulationConfig Config = MakeRegressionConfig(ETDFluidPresetKind::Mud, 64, FVector2D(3200.0f, 3200.0f));

	for (const FEdgeNormalCase& EdgeCase : EdgeCases)
	{
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);

		const FVector2D EdgeCenter = Solver.CellToWorldCenter(EdgeCase.Cell.X, EdgeCase.Cell.Y);
		const FVector2D ObstacleCenter = EdgeCenter + EdgeCase.DirectionToObstacle * (ObstacleRadiusCm + SurfaceGapCm);
		const FVector2D IntoObstacleVelocity = EdgeCase.DirectionToObstacle * IntoObstacleSpeedCmPerSec;

		Solver.ApplyBrushStamps({ MakeRegressionStamp(FVector(EdgeCenter.X, EdgeCenter.Y, 0.0f), FVector(IntoObstacleVelocity.X, IntoObstacleVelocity.Y, 0.0f), 60.0f) }, 0.1f);
		const float SpeedBeforeObstacle = static_cast<float>(Solver.SampleAtCell(EdgeCase.Cell.X, EdgeCase.Cell.Y).Velocity.Size());

		FTDFluidSdfObstacle Obstacle;
		Obstacle.WorldCenter = FVector(ObstacleCenter.X, ObstacleCenter.Y, 0.0f);
		Obstacle.RadiusCm = ObstacleRadiusCm;
		Solver.RasterizeSdfObstacles({ Obstacle });

		const FTDFluidCellSample EdgeSample = Solver.SampleAtCell(EdgeCase.Cell.X, EdgeCase.Cell.Y);
		const float SpeedAfterObstacle = static_cast<float>(EdgeSample.Velocity.Size());
		AddInfo(FString::Printf(TEXT("cell=(%d,%d) sdf=%.2f speedBefore=%.2f speedAfter=%.2f"), EdgeCase.Cell.X, EdgeCase.Cell.Y, EdgeSample.ObstacleSdf, SpeedBeforeObstacle, SpeedAfterObstacle));

		TestTrue(*FString::Printf(TEXT("Edge cell (%d,%d) is fluid inside the obstacle boundary band (sdf %.2f)"), EdgeCase.Cell.X, EdgeCase.Cell.Y, EdgeSample.ObstacleSdf), EdgeSample.ObstacleSdf > 0.0f && EdgeSample.ObstacleSdf < 75.0f);
		TestTrue(*FString::Printf(TEXT("Edge cell (%d,%d) starts with the stamp velocity (%.2f)"), EdgeCase.Cell.X, EdgeCase.Cell.Y, SpeedBeforeObstacle), SpeedBeforeObstacle > 0.9f * IntoObstacleSpeedCmPerSec);
		TestTrue(*FString::Printf(TEXT("Velocity straight into the obstacle is removed at edge cell (%d,%d), left %.2f cm/s"), EdgeCase.Cell.X, EdgeCase.Cell.Y, SpeedAfterObstacle), SpeedAfterObstacle < 10.0f);
	}

	struct FTangentialVelocityCase
	{
		const TCHAR* Label;
		float TowardObstacleSpeed;
		float TangentSpeed;
	};

	const FTangentialVelocityCase TangentialCases[] = {
		{ TEXT("inward plus tangential"), IntoObstacleSpeedCmPerSec, IntoObstacleSpeedCmPerSec },
		{ TEXT("outward plus tangential"), -IntoObstacleSpeedCmPerSec, IntoObstacleSpeedCmPerSec },
	};

	for (const FEdgeNormalCase& EdgeCase : EdgeCases)
	{
		const FVector2D TangentDirection(-EdgeCase.DirectionToObstacle.Y, EdgeCase.DirectionToObstacle.X);

		for (const FTangentialVelocityCase& VelocityCase : TangentialCases)
		{
			FTDFluidGrid2DSolver Solver;
			Solver.Initialize(Config, FVector2D::ZeroVector);

			const FVector2D EdgeCenter = Solver.CellToWorldCenter(EdgeCase.Cell.X, EdgeCase.Cell.Y);
			const FVector2D ObstacleCenter = EdgeCenter + EdgeCase.DirectionToObstacle * (ObstacleRadiusCm + SurfaceGapCm);
			const FVector2D StampVelocity = EdgeCase.DirectionToObstacle * VelocityCase.TowardObstacleSpeed + TangentDirection * VelocityCase.TangentSpeed;

			Solver.ApplyBrushStamps({ MakeRegressionStamp(FVector(EdgeCenter.X, EdgeCenter.Y, 0.0f), FVector(StampVelocity.X, StampVelocity.Y, 0.0f), 60.0f) }, 0.1f);

			FTDFluidSdfObstacle Obstacle;
			Obstacle.WorldCenter = FVector(ObstacleCenter.X, ObstacleCenter.Y, 0.0f);
			Obstacle.RadiusCm = ObstacleRadiusCm;
			Solver.RasterizeSdfObstacles({ Obstacle });

			const FTDFluidCellSample EdgeSample = Solver.SampleAtCell(EdgeCase.Cell.X, EdgeCase.Cell.Y);
			const float TowardObstacle = static_cast<float>(FVector2D::DotProduct(EdgeSample.Velocity, EdgeCase.DirectionToObstacle));
			const float AlongTangent = static_cast<float>(FVector2D::DotProduct(EdgeSample.Velocity, TangentDirection));
			AddInfo(FString::Printf(TEXT("%s cell=(%d,%d) sdf=%.2f toward=%.2f tangent=%.2f"), VelocityCase.Label, EdgeCase.Cell.X, EdgeCase.Cell.Y, EdgeSample.ObstacleSdf, TowardObstacle, AlongTangent));

			TestTrue(*FString::Printf(TEXT("%s: edge cell (%d,%d) is fluid inside the obstacle boundary band (sdf %.2f)"), VelocityCase.Label, EdgeCase.Cell.X, EdgeCase.Cell.Y, EdgeSample.ObstacleSdf), EdgeSample.ObstacleSdf > 0.0f && EdgeSample.ObstacleSdf < 75.0f);
			TestTrue(*FString::Printf(TEXT("%s: at least 80%% of the tangential speed survives at edge cell (%d,%d), kept %.2f of %.2f"), VelocityCase.Label, EdgeCase.Cell.X, EdgeCase.Cell.Y, AlongTangent, VelocityCase.TangentSpeed), AlongTangent >= 0.8f * VelocityCase.TangentSpeed);
			TestTrue(*FString::Printf(TEXT("%s: the tangential speed is not amplified at edge cell (%d,%d), kept %.2f"), VelocityCase.Label, EdgeCase.Cell.X, EdgeCase.Cell.Y, AlongTangent), AlongTangent <= 1.2f * VelocityCase.TangentSpeed);

			if (VelocityCase.TowardObstacleSpeed > 0.0f)
			{
				TestTrue(*FString::Printf(TEXT("%s: the inward component is removed at edge cell (%d,%d), left %.2f"), VelocityCase.Label, EdgeCase.Cell.X, EdgeCase.Cell.Y, TowardObstacle), TowardObstacle < 0.1f * VelocityCase.TowardObstacleSpeed);
				continue;
			}

			TestTrue(*FString::Printf(TEXT("%s: at least 80%% of the outward speed is kept at edge cell (%d,%d), toward %.2f"), VelocityCase.Label, EdgeCase.Cell.X, EdgeCase.Cell.Y, TowardObstacle), TowardObstacle <= 0.8f * VelocityCase.TowardObstacleSpeed);
			TestTrue(*FString::Printf(TEXT("%s: the outward speed is not amplified at edge cell (%d,%d), toward %.2f"), VelocityCase.Label, EdgeCase.Cell.X, EdgeCase.Cell.Y, TowardObstacle), TowardObstacle >= 1.2f * VelocityCase.TowardObstacleSpeed);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidStampSpeedCapTest,
	"TDGame.FluidFX.SolverRegression.StampVelocityIsCappedAtMaxSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidStampSpeedCapTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverRegressionPrivate;

	const FTDFluidSimulationConfig Config = MakeRegressionConfig(ETDFluidPresetKind::Mud, 64, FVector2D(3200.0f, 3200.0f));
	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Config, FVector2D::ZeroVector);

	const FVector2D StampCenter = Solver.CellToWorldCenter(32, 32);
	Solver.ApplyBrushStamps({ MakeRegressionStamp(FVector(StampCenter.X, StampCenter.Y, 0.0f), FVector(3000.0f, 0.0f, 0.0f), 200.0f) }, 0.1f);

	const float SpeedAfterStamp = MeasureMaxSpeedCmPerSec(Solver);
	AddInfo(FString::Printf(TEXT("target=3000 speedAfterStamp=%.2f"), SpeedAfterStamp));
	TestTrue(*FString::Printf(TEXT("Stamp speed is capped at 1200 cm/s (%.2f)"), SpeedAfterStamp), SpeedAfterStamp <= 1200.01f);
	TestTrue(*FString::Printf(TEXT("Stamp speed actually reaches the cap (%.2f)"), SpeedAfterStamp), SpeedAfterStamp >= 1000.0f);

	Solver.StepSimulation(1.0f / 60.0f);
	const float SpeedAfterStep = MeasureMaxSpeedCmPerSec(Solver);
	TestTrue(*FString::Printf(TEXT("A step keeps every cell within 1200 cm/s (%.2f)"), SpeedAfterStep), SpeedAfterStep <= 1200.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSettledPressureTest,
	"TDGame.FluidFX.SolverRegression.MovingStampLeavesNoPressureAfterSettling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSettledPressureTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverRegressionPrivate;

	const FTDFluidSimulationConfig Config = MakeRegressionConfig(ETDFluidPresetKind::Mud, 64, FVector2D(3200.0f, 3200.0f));
	constexpr float DeltaTime = 1.0f / 60.0f;

	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Config, FVector2D::ZeroVector);

	for (int32 Frame = 0; Frame < 20; ++Frame)
	{
		Solver.ApplyBrushStamps({ MakeRegressionStamp(FVector(1300.0f + 20.0f * static_cast<float>(Frame), 0.0f, 0.0f), FVector(1200.0f, 0.0f, 0.0f), 1000.0f) }, DeltaTime);
		Solver.StepSimulation(DeltaTime);
	}
	const float PeakPressure = MeasureMaxAbsPressure(Solver);

	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		Solver.StepSimulation(DeltaTime);
	}
	const float SettledSpeed = MeasureMaxSpeedCmPerSec(Solver);
	const float SettledPressure = MeasureMaxAbsPressure(Solver);
	AddInfo(FString::Printf(TEXT("peakPressure=%.4f settledSpeed=%.4f settledPressure=%.5f"), PeakPressure, SettledSpeed, SettledPressure));

	TestTrue(*FString::Printf(TEXT("Moving stamp precondition creates pressure (%.4f)"), PeakPressure), PeakPressure > 0.05f);
	TestTrue(*FString::Printf(TEXT("Velocity settles below 1 cm/s after 60 frames (%.4f)"), SettledSpeed), SettledSpeed < 1.0f);
	TestTrue(*FString::Printf(TEXT("Display pressure settles below 0.02 once velocity is below 1 cm/s (%.5f)"), SettledPressure), SettledPressure < 0.02f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterBorderBandTest,
	"TDGame.FluidFX.SolverRegression.WaterBorderBandKeepsDensityAndFadesAlpha",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterBorderBandTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverRegressionPrivate;

	const FTDFluidSimulationConfig Config = MakeRegressionConfig(ETDFluidPresetKind::Water, 64, FVector2D(3200.0f, 3200.0f));
	TestTrue(TEXT("Water preset runs the fluid simulation"), Config.SimMode == ETDFluidSimMode::FluidSim);

	const TArray<FIntPoint> BandCells = { FIntPoint(1, 32), FIntPoint(62, 32), FIntPoint(32, 1), FIntPoint(32, 62) };
	const int32 FramesPerSecondCases[] = { 30, 120 };
	float Retention[2][4] = {};

	for (int32 CaseIndex = 0; CaseIndex < 2; ++CaseIndex)
	{
		const int32 FramesPerSecond = FramesPerSecondCases[CaseIndex];
		const float DeltaTime = 1.0f / static_cast<float>(FramesPerSecond);

		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);

		TArray<FTDFluidBrushStamp> Stamps;
		for (const FIntPoint& BandCell : BandCells)
		{
			const FVector2D CellCenter = Solver.CellToWorldCenter(BandCell.X, BandCell.Y);
			FTDFluidBrushStamp Stamp = MakeRegressionStamp(FVector(CellCenter.X, CellCenter.Y, 0.0f), FVector::ZeroVector, 30.0f);
			Stamp.VelocityStrength = 0.0f;
			Stamps.Add(Stamp);
		}
		Solver.ApplyBrushStamps(Stamps, DeltaTime);

		float InitialDensity[4] = {};
		for (int32 BandIndex = 0; BandIndex < BandCells.Num(); ++BandIndex)
		{
			InitialDensity[BandIndex] = Solver.SampleAtCell(BandCells[BandIndex].X, BandCells[BandIndex].Y).PrimaryDensity;
			TestTrue(*FString::Printf(TEXT("Band cell %d starts with full density (%.3f)"), BandIndex, InitialDensity[BandIndex]), InitialDensity[BandIndex] > 0.9f);
		}

		for (int32 Frame = 0; Frame < FramesPerSecond; ++Frame)
		{
			Solver.StepSimulation(DeltaTime);
		}

		for (int32 BandIndex = 0; BandIndex < BandCells.Num(); ++BandIndex)
		{
			Retention[CaseIndex][BandIndex] = Solver.SampleAtCell(BandCells[BandIndex].X, BandCells[BandIndex].Y).PrimaryDensity / FMath::Max(InitialDensity[BandIndex], UE_SMALL_NUMBER);
			AddInfo(FString::Printf(TEXT("fps=%d cell=(%d,%d) retention=%.4f"), FramesPerSecond, BandCells[BandIndex].X, BandCells[BandIndex].Y, Retention[CaseIndex][BandIndex]));
			TestTrue(*FString::Printf(TEXT("Water band cell (%d,%d) keeps at least half its density after 1 s at %d fps (%.4f)"), BandCells[BandIndex].X, BandCells[BandIndex].Y, FramesPerSecond, Retention[CaseIndex][BandIndex]), Retention[CaseIndex][BandIndex] >= 0.5f);
		}

		TArray<FFloat16Color> DensityPressurePixels;
		TArray<FFloat16Color> VelocityPixels;
		Solver.PackOutputPixels(DensityPressurePixels, VelocityPixels);
		const float OutermostAlpha = DensityPressurePixels[32 * 64 + 0].A.GetFloat();
		const float CenterAlpha = DensityPressurePixels[32 * 64 + 32].A.GetFloat();
		AddInfo(FString::Printf(TEXT("fps=%d outermostAlpha=%.4f centerAlpha=%.4f"), FramesPerSecond, OutermostAlpha, CenterAlpha));
		TestEqual(*FString::Printf(TEXT("Center alpha is fully opaque at %d fps"), FramesPerSecond), CenterAlpha, 1.0f);
		TestTrue(*FString::Printf(TEXT("Outermost alpha %.4f is below center alpha %.4f at %d fps"), OutermostAlpha, CenterAlpha, FramesPerSecond), OutermostAlpha < CenterAlpha);
	}

	for (int32 BandIndex = 0; BandIndex < BandCells.Num(); ++BandIndex)
	{
		TestTrue(
			*FString::Printf(TEXT("Band cell (%d,%d) retention at 30 fps is within 10%% of 120 fps (%.4f vs %.4f)"), BandCells[BandIndex].X, BandCells[BandIndex].Y, Retention[0][BandIndex], Retention[1][BandIndex]),
			FMath::Abs(Retention[0][BandIndex] - Retention[1][BandIndex]) <= 0.1f * Retention[1][BandIndex]);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidExtremeConfigTest,
	"TDGame.FluidFX.SolverRegression.ExtremeConfigIsClampedAndStaysFinite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidExtremeConfigTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverRegressionPrivate;

	FTDFluidSimulationConfig TinyConfig = MakeRegressionConfig(ETDFluidPresetKind::Mud, 0, FVector2D::ZeroVector);
	TinyConfig.VelocityDamping = -5.0f;
	TinyConfig.DensityDissipation = -5.0f;
	TinyConfig.VorticityStrength = -5.0f;

	FTDFluidSimulationConfig HugeConfig = MakeRegressionConfig(ETDFluidPresetKind::Mud, 1000, FVector2D(3200.0f, 3200.0f));
	HugeConfig.VelocityDamping = -5.0f;
	HugeConfig.DensityDissipation = -5.0f;
	HugeConfig.VorticityStrength = -5.0f;

	FTDFluidGrid2DSolver TinySolver;
	TinySolver.Initialize(TinyConfig, FVector2D::ZeroVector);
	TestEqual(TEXT("Resolution 0 clamps to 16"), TinySolver.GetResolution(), 16);
	TestNearlyEqual(TEXT("Zero extent clamps to 100 cm in X"), static_cast<float>(TinySolver.GetExtentCm().X), 100.0f, 1e-3f);
	TestNearlyEqual(TEXT("Zero extent clamps to 100 cm in Y"), static_cast<float>(TinySolver.GetExtentCm().Y), 100.0f, 1e-3f);
	CheckExtremeConfigStaysClamped(*this, TinySolver, TEXT("Tiny"));

	FTDFluidGrid2DSolver HugeSolver;
	HugeSolver.Initialize(HugeConfig, FVector2D::ZeroVector);
	TestEqual(TEXT("Resolution 1000 clamps to 256"), HugeSolver.GetResolution(), 256);
	CheckExtremeConfigStaysClamped(*this, HugeSolver, TEXT("Huge"));

	FTDFluidGrid2DSolver UpdatedSolver;
	UpdatedSolver.Initialize(MakeRegressionConfig(ETDFluidPresetKind::Mud, 64, FVector2D(3200.0f, 3200.0f)), FVector2D::ZeroVector);
	FTDFluidSimulationConfig NegativeUpdate = TinyConfig;
	NegativeUpdate.GridResolution = 64;
	UpdatedSolver.UpdateConfig(NegativeUpdate);
	TestEqual(TEXT("Update with the same resolution keeps 64"), UpdatedSolver.GetResolution(), 64);
	CheckExtremeConfigStaysClamped(*this, UpdatedSolver, TEXT("Updated"));

	return true;
}

#endif
