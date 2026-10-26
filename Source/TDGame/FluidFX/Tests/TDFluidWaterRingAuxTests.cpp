#if WITH_DEV_AUTOMATION_TESTS

#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidTypes.h"
#include "Misc/AutomationTest.h"

namespace TDFluidWaterRingAuxTestPrivate
{
	constexpr float RingTestStepSeconds = 1.0f / 60.0f;
	constexpr float RingTestRadiusCm = 45.0f;
	constexpr float RingTestInnerRatio = 0.65f;
	constexpr float RingTestSideArcWeight = 0.3f;

	FTDFluidSimulationConfig MakeRingTestWaterConfig(int32 Resolution, double ExtentCm)
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = Resolution;
		Config.SimExtentCm = FVector2D(ExtentCm, ExtentCm);
		Config.bEnableWaterLayer = true;
		Config.bTreatDryCellsAsSolid = false;
		Config.bOpenWaterBorders = false;
		Config.DensityDissipation = 0.0f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.BorderFadeRatio = 0.0f;
		Config.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Config.AmbientNoiseAmount = 0.0f;
		Config.AmbientDensity = 0.0f;
		Config.AmbientRefillPerSec = 0.0f;
		Config.MaxPointsPerTick = 64;
		return Config;
	}

	FTDFluidBrushStamp MakeRingTestStamp(const FVector& Location, float InnerRatio)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = Location;
		Stamp.WorldVelocity = FVector(150.0, 0.0, 0.0);
		Stamp.RadiusCm = RingTestRadiusCm;
		Stamp.DensityStrength = 1.0f;
		Stamp.VelocityStrength = 0.0f;
		Stamp.RingInnerRadiusRatio = InnerRatio;
		Stamp.RingSideArcWeight = RingTestSideArcWeight;
		Stamp.RingInnerEraseRatePerSec = 30.0f;
		return Stamp;
	}

	struct FRingProfile
	{
		float InnerMaximum = 0.0f;
		float BackArcMaximum = 0.0f;
		float FrontPeak = 0.0f;
		float FrontPeakRadiusCm = 0.0f;
	};

	FRingProfile MeasureRingProfile(const FTDFluidGrid2DSolver& Solver, int32 Resolution, const FVector2D& Center)
	{
		FRingProfile Profile;
		const float InnerRadiusCm = RingTestInnerRatio * RingTestRadiusCm;
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const FVector2D Offset = Solver.CellToWorldCenter(CellX, CellY) - Center;
				const float DistanceCm = static_cast<float>(Offset.Size());
				if (DistanceCm > RingTestRadiusCm || DistanceCm <= UE_KINDA_SMALL_NUMBER)
				{
					continue;
				}

				const float Density = Solver.SampleAtCell(CellX, CellY).PrimaryDensity;
				const float Cosine = static_cast<float>(Offset.X) / DistanceCm;
				if (DistanceCm < 0.8f * InnerRadiusCm)
				{
					Profile.InnerMaximum = FMath::Max(Profile.InnerMaximum, Density);
				}
				if (Cosine < -0.3f)
				{
					Profile.BackArcMaximum = FMath::Max(Profile.BackArcMaximum, Density);
				}
				if (Cosine > 0.9f && Density > Profile.FrontPeak)
				{
					Profile.FrontPeak = Density;
					Profile.FrontPeakRadiusCm = DistanceCm;
				}
			}
		}
		return Profile;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterRingStampLeavesAnnulusTest,
	"TDGame.FluidFX.Water.Brush.RingStampLeavesAnnulus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterRingStampLeavesAnnulusTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterRingAuxTestPrivate;
	constexpr int32 Resolution = 256;
	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(MakeRingTestWaterConfig(Resolution, 600.0), FVector2D::ZeroVector);
	Solver.ApplyBrushStamps({ MakeRingTestStamp(FVector::ZeroVector, RingTestInnerRatio) }, RingTestStepSeconds);

	const FRingProfile Profile = MeasureRingProfile(Solver, Resolution, FVector2D::ZeroVector);
	TestTrue(TEXT("Front arc carries the ring peak"), Profile.FrontPeak > 0.5f);
	TestTrue(TEXT("Inner disc stays below 20 percent of the peak"), Profile.InnerMaximum < 0.2f * Profile.FrontPeak);
	TestTrue(TEXT("Peak radius sits inside the ring band"), Profile.FrontPeakRadiusCm >= RingTestInnerRatio * RingTestRadiusCm && Profile.FrontPeakRadiusCm <= RingTestRadiusCm);
	TestEqual(TEXT("Back arc stays empty"), Profile.BackArcMaximum, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterRingSweepLeavesSideBandsTest,
	"TDGame.FluidFX.Water.Brush.RingSweepSideBandWidth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterRingSweepLeavesSideBandsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterRingAuxTestPrivate;
	constexpr int32 Resolution = 256;
	constexpr int32 SweepFrameCount = 120;
	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(MakeRingTestWaterConfig(Resolution, 600.0), FVector2D::ZeroVector);
	for (int32 FrameIndex = 0; FrameIndex < SweepFrameCount; ++FrameIndex)
	{
		const double PositionX = -250.0 + 150.0 * RingTestStepSeconds * FrameIndex;
		Solver.ApplyBrushStamps({ MakeRingTestStamp(FVector(PositionX, 0.0, 0.0), RingTestInnerRatio) }, RingTestStepSeconds);
	}

	float CenterMaximum = 0.0f;
	float SidePeak = 0.0f;
	const double SampleX = -150.0;
	for (int32 CellY = 0; CellY < Resolution; ++CellY)
	{
		const int32 CellX = FMath::FloorToInt32((SampleX + 300.0) / (600.0 / Resolution));
		const FVector2D CellCenter = Solver.CellToWorldCenter(CellX, CellY);
		const float Density = Solver.SampleAtCell(CellX, CellY).PrimaryDensity;
		const double Across = FMath::Abs(CellCenter.Y);
		if (Across < 0.5 * RingTestInnerRatio * RingTestRadiusCm)
		{
			CenterMaximum = FMath::Max(CenterMaximum, Density);
		}
		else if (Across <= RingTestRadiusCm)
		{
			SidePeak = FMath::Max(SidePeak, Density);
		}
	}

	TestTrue(TEXT("Side bands exist behind the walker"), SidePeak > 0.0f);
	TestTrue(TEXT("Centre line stays below the side band peak"), CenterMaximum < SidePeak);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterRingDisabledBitIdenticalTest,
	"TDGame.FluidFX.Water.Brush.RingDisabledBitIdentical",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterRingDisabledBitIdenticalTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterRingAuxTestPrivate;
	constexpr int32 Resolution = 96;
	FTDFluidGrid2DSolver DiscSolver;
	FTDFluidGrid2DSolver ZeroRingSolver;
	DiscSolver.Initialize(MakeRingTestWaterConfig(Resolution, 900.0), FVector2D::ZeroVector);
	ZeroRingSolver.Initialize(MakeRingTestWaterConfig(Resolution, 900.0), FVector2D::ZeroVector);
	for (int32 FrameIndex = 0; FrameIndex < 30; ++FrameIndex)
	{
		FTDFluidBrushStamp DiscStamp;
		DiscStamp.WorldLocation = FVector(-200.0 + 10.0 * FrameIndex, 15.0, 0.0);
		DiscStamp.WorldVelocity = FVector(600.0, 0.0, 0.0);
		DiscStamp.RadiusCm = RingTestRadiusCm;
		DiscStamp.DensityStrength = 0.9f;
		DiscStamp.RippleImpulseCm = 2.5f;
		FTDFluidBrushStamp ZeroRingStamp = DiscStamp;
		ZeroRingStamp.RingInnerRadiusRatio = 0.0f;
		ZeroRingStamp.RingSideArcWeight = 0.7f;
		ZeroRingStamp.RingInnerEraseRatePerSec = 45.0f;
		DiscSolver.ApplyBrushStamps({ DiscStamp }, RingTestStepSeconds);
		ZeroRingSolver.ApplyBrushStamps({ ZeroRingStamp }, RingTestStepSeconds);
		DiscSolver.StepSimulation(RingTestStepSeconds);
		ZeroRingSolver.StepSimulation(RingTestStepSeconds);
	}

	TestEqual(TEXT("Solver checksum matches the disc path"), ZeroRingSolver.ComputeStateChecksum(), DiscSolver.ComputeStateChecksum());
	TestEqual(TEXT("Water checksum matches the disc path"), ZeroRingSolver.ComputeWaterStateChecksum(), DiscSolver.ComputeWaterStateChecksum());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterAuxObstacleDistanceInGTest,
	"TDGame.FluidFX.Water.Aux.ObstacleDistanceInG",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterAuxObstacleDistanceInGTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterRingAuxTestPrivate;
	constexpr int32 Resolution = 64;
	constexpr double ExtentCm = 1600.0;
	constexpr float RangeCm = 400.0f;
	constexpr float ObstacleRadiusCm = 100.0f;
	FTDFluidSimulationConfig Config = MakeRingTestWaterConfig(Resolution, ExtentCm);
	Config.bOutputObstacleDistance = true;
	Config.ObstacleDistanceOutputRangeCm = RangeCm;
	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Config, FVector2D::ZeroVector);

	FTDFluidSdfObstacle Obstacle;
	Obstacle.WorldCenter = FVector::ZeroVector;
	Obstacle.RadiusCm = ObstacleRadiusCm;
	Solver.RasterizeSdfObstacles({ Obstacle });

	TArray<FFloat16Color> AuxPixels;
	Solver.PackAuxPixels(AuxPixels);
	if (!TestEqual(TEXT("Aux holds one pixel per cell"), AuxPixels.Num(), Resolution * Resolution))
	{
		return false;
	}

	const float CellSizeCm = static_cast<float>(ExtentCm) / Resolution;
	int32 MismatchCount = 0;
	for (int32 CellY = 0; CellY < Resolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < Resolution; ++CellX)
		{
			const FLinearColor Pixel = AuxPixels[CellY * Resolution + CellX].GetFloats();
			const float ExpectedSdfCm = static_cast<float>(Solver.CellToWorldCenter(CellX, CellY).Size()) - ObstacleRadiusCm;
			const float Expected = FMath::Clamp(ExpectedSdfCm / RangeCm, -1.0f, 1.0f);
			if (Pixel.R != 0.0f || FMath::Abs(Pixel.G - Expected) > CellSizeCm / RangeCm + 0.003f)
			{
				++MismatchCount;
			}
		}
	}
	TestEqual(TEXT("G holds the clamped obstacle distance and R stays zero"), MismatchCount, 0);

	const int32 GenerationAfterRaster = Solver.GetObstacleSdfRawGeneration();
	Solver.StepSimulation(RingTestStepSeconds);
	TestEqual(TEXT("Unchanged obstacles keep the generation"), Solver.GetObstacleSdfRawGeneration(), GenerationAfterRaster);
	Solver.RasterizeSdfObstacles({ Obstacle });
	TestTrue(TEXT("Re-rasterized obstacles advance the generation"), Solver.GetObstacleSdfRawGeneration() != GenerationAfterRaster);

	FTDFluidSimulationConfig DisabledConfig = Config;
	DisabledConfig.bOutputObstacleDistance = false;
	FTDFluidGrid2DSolver DisabledSolver;
	DisabledSolver.Initialize(DisabledConfig, FVector2D::ZeroVector);
	TArray<FFloat16Color> DisabledPixels;
	DisabledSolver.PackAuxPixels(DisabledPixels);
	TestEqual(TEXT("Disabled distance output packs no Aux"), DisabledPixels.Num(), 0);
	return true;
}

#endif
