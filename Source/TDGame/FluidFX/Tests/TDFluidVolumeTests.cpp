#if WITH_DEV_AUTOMATION_TESTS

#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace TDFluidVolumePrivate
{
	constexpr float SixtyFpsSeconds = 1.0f / 60.0f;

	FTDFluidSimulationConfig MakeSmokeConfig()
	{
		FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Smoke);
		Config.GridResolution = 64;
		Config.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Config.AmbientNoiseAmount = 0.0f;
		return Config;
	}

	void StepRepeatedly(FTDFluidGrid2DSolver& Solver, float TotalSeconds, float DeltaTime)
	{
		const int32 StepCount = FMath::RoundToInt(TotalSeconds / DeltaTime);
		for (int32 Step = 0; Step < StepCount; ++Step)
		{
			Solver.StepSimulation(DeltaTime);
		}
	}

	void FillWithAmbientDensity(FTDFluidGrid2DSolver& Solver)
	{
		FTDFluidSimulationConfig Config = MakeSmokeConfig();
		Config.AmbientRefillPerSec = 100.0f;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		Solver.StepSimulation(0.1f);
		Config.AmbientRefillPerSec = 0.0f;
		Solver.UpdateConfig(Config);
	}

	float MeasureMeanDensityInAnnulus(const FTDFluidGrid2DSolver& Solver, float InnerCm, float OuterCm)
	{
		double DensitySum = 0.0;
		int32 CellCount = 0;
		const int32 Resolution = Solver.GetResolution();
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const double DistanceCm = FVector2D::Distance(Solver.CellToWorldCenter(CellX, CellY), FVector2D::ZeroVector);
				if (DistanceCm < InnerCm || DistanceCm > OuterCm)
				{
					continue;
				}
				DensitySum += Solver.SampleAtCell(CellX, CellY).PrimaryDensity;
				++CellCount;
			}
		}
		return CellCount > 0 ? static_cast<float>(DensitySum / CellCount) : 0.0f;
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

	float ComputeSmoothWeight(float DistanceCm, float RadiusCm)
	{
		const float Falloff = 1.0f - DistanceCm / RadiusCm;
		return Falloff * Falloff * (3.0f - 2.0f * Falloff);
	}

	FTDFluidBrushStamp MakeEraseStamp(float RadiusCm, ETDFluidBrushChannel Channel)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = FVector::ZeroVector;
		Stamp.RadiusCm = RadiusCm;
		Stamp.DensityStrength = -1.0f;
		Stamp.VelocityStrength = 0.0f;
		Stamp.Channel = Channel;
		return Stamp;
	}

	FTDFluidBrushStamp MakeRadialPushStamp(float RadiusCm, float RadialVelocityCmPerSec)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = FVector::ZeroVector;
		Stamp.RadiusCm = RadiusCm;
		Stamp.DensityStrength = 0.0f;
		Stamp.VelocityStrength = 0.0f;
		Stamp.RadialVelocityCmPerSec = RadialVelocityCmPerSec;
		return Stamp;
	}

	float RunAmbientFill(float DeltaTime, float TotalSeconds)
	{
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(MakeSmokeConfig(), FVector2D::ZeroVector);
		StepRepeatedly(Solver, TotalSeconds, DeltaTime);
		return Solver.SampleAtCell(20, 20).PrimaryDensity;
	}

	TArray<float> RunEraseOneSecond(float DeltaTime, ETDFluidBrushChannel Channel, const TArray<FIntPoint>& SampledCells)
	{
		FTDFluidGrid2DSolver Solver;
		FillWithAmbientDensity(Solver);

		const int32 FrameCount = FMath::RoundToInt(1.0f / DeltaTime);
		for (int32 Frame = 0; Frame < FrameCount; ++Frame)
		{
			Solver.ApplyBrushStamps({ MakeEraseStamp(120.0f, Channel) }, DeltaTime);
			Solver.StepSimulation(DeltaTime);
		}

		TArray<float> Densities;
		for (const FIntPoint& Cell : SampledCells)
		{
			Densities.Add(Solver.SampleAtCell(Cell.X, Cell.Y).PrimaryDensity);
		}
		return Densities;
	}

	struct FTDFluidRadialPushOutcome
	{
		float CenterDensity = 0.0f;
		float RingDensity = 0.0f;
	};

	FTDFluidRadialPushOutcome MeasureRadialPushOutcome(const FTDFluidGrid2DSolver& Solver)
	{
		FTDFluidRadialPushOutcome Outcome;
		Outcome.CenterDensity = MeasureMeanDensityInAnnulus(Solver, 0.0f, 15.0f);
		Outcome.RingDensity = MeasureMeanDensityInAnnulus(Solver, 110.0f, 190.0f);
		return Outcome;
	}

	FTDFluidRadialPushOutcome RunRadialPushOnBlob(float DeltaTime, float RadialVelocityCmPerSec)
	{
		FTDFluidSimulationConfig Config = MakeSmokeConfig();
		Config.AmbientRefillPerSec = 0.0f;
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);

		FTDFluidBrushStamp BlobStamp;
		BlobStamp.WorldLocation = FVector::ZeroVector;
		BlobStamp.RadiusCm = 80.0f;
		BlobStamp.DensityStrength = 1.0f;
		BlobStamp.VelocityStrength = 0.0f;
		Solver.ApplyBrushStamps({ BlobStamp }, DeltaTime);

		const int32 FrameCount = FMath::RoundToInt(0.5f / DeltaTime);
		for (int32 Frame = 0; Frame < FrameCount; ++Frame)
		{
			Solver.ApplyBrushStamps({ MakeRadialPushStamp(170.0f, RadialVelocityCmPerSec) }, DeltaTime);
			Solver.StepSimulation(DeltaTime);
		}
		return MeasureRadialPushOutcome(Solver);
	}

	FTDFluidGrid2DSolver StepBlobWithStackedPushes(int32 StackedStampCount)
	{
		FTDFluidSimulationConfig Config = MakeSmokeConfig();
		Config.AmbientRefillPerSec = 0.0f;
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);

		FTDFluidBrushStamp BlobStamp;
		BlobStamp.WorldLocation = FVector::ZeroVector;
		BlobStamp.RadiusCm = 80.0f;
		BlobStamp.DensityStrength = 1.0f;
		BlobStamp.VelocityStrength = 0.0f;
		Solver.ApplyBrushStamps({ BlobStamp }, SixtyFpsSeconds);

		TArray<FTDFluidBrushStamp> PushStamps;
		for (int32 StampIndex = 0; StampIndex < StackedStampCount; ++StampIndex)
		{
			PushStamps.Add(MakeRadialPushStamp(150.0f, 1000.0f));
		}
		Solver.ApplyBrushStamps(PushStamps, SixtyFpsSeconds);
		Solver.StepSimulation(SixtyFpsSeconds);
		return Solver;
	}

	FTDFluidRadialPushOutcome RunRadialPushThroughSmoke(float DeltaTime, float RadialVelocityCmPerSec)
	{
		FTDFluidGrid2DSolver Solver;
		FillWithAmbientDensity(Solver);

		const int32 FrameCount = FMath::RoundToInt(0.5f / DeltaTime);
		for (int32 Frame = 0; Frame < FrameCount; ++Frame)
		{
			Solver.ApplyBrushStamps({ MakeEraseStamp(120.0f, ETDFluidBrushChannel::ChannelR_Mud), MakeRadialPushStamp(200.0f, RadialVelocityCmPerSec) }, DeltaTime);
			Solver.StepSimulation(DeltaTime);
		}
		return MeasureRadialPushOutcome(Solver);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidHalfFloatPackingTest,
	"TDGame.FluidFX.Volume.PackedOutputKeepsEncodingInHalfFloat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidHalfFloatPackingTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidVolumePrivate;
	FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Water);
	Config.GridResolution = 64;
	Config.SimExtentCm = FVector2D(1600.0f, 1600.0f);

	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Config, FVector2D::ZeroVector);

	FTDFluidBrushStamp Stamp;
	Stamp.WorldLocation = FVector(100.0f, 50.0f, 0.0f);
	Stamp.WorldVelocity = FVector(300.0f, 100.0f, 0.0f);
	Stamp.RadiusCm = 250.0f;
	Stamp.DensityStrength = 0.7f;
	Stamp.VelocityStrength = 1.0f;
	Stamp.Channel = ETDFluidBrushChannel::ChannelB_Fire;
	for (int32 Frame = 0; Frame < 6; ++Frame)
	{
		Solver.ApplyBrushStamps({ Stamp }, SixtyFpsSeconds);
		Solver.StepSimulation(SixtyFpsSeconds);
	}

	TArray<FFloat16Color> DensityPressurePixels;
	TArray<FFloat16Color> VelocityPixels;
	Solver.PackOutputPixels(DensityPressurePixels, VelocityPixels);

	const int32 Resolution = Solver.GetResolution();
	if (!TestEqual(TEXT("Both packed buffers hold one pixel per cell"), DensityPressurePixels.Num() + VelocityPixels.Num(), 2 * Resolution * Resolution))
	{
		return false;
	}

	int32 MismatchedCells = 0;
	int32 CellsBeyondEightBitSteps = 0;
	int32 CellsWithDensity = 0;
	for (int32 CellY = 0; CellY < Resolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < Resolution; ++CellX)
		{
			const int32 PixelIndex = CellY * Resolution + CellX;
			const FTDFluidCellSample Sample = Solver.SampleAtCell(CellX, CellY);
			const float ExpectedVelocityX = FMath::Clamp(static_cast<float>(Sample.Velocity.X) / 600.0f, -1.0f, 1.0f) * 0.5f + 0.5f;
			const float ExpectedVelocityY = FMath::Clamp(static_cast<float>(Sample.Velocity.Y) / 600.0f, -1.0f, 1.0f) * 0.5f + 0.5f;
			const float ExpectedSpeed = FMath::Clamp(static_cast<float>(Sample.Velocity.Size()) / 600.0f, 0.0f, 1.0f);

			const bool bMatches = FMath::Abs(DensityPressurePixels[PixelIndex].R.GetFloat() - Sample.PrimaryDensity) < 1e-3f
				&& FMath::Abs(DensityPressurePixels[PixelIndex].B.GetFloat() - Sample.SelectiveDensity) < 1e-3f
				&& FMath::Abs(VelocityPixels[PixelIndex].R.GetFloat() - ExpectedVelocityX) < 1e-3f
				&& FMath::Abs(VelocityPixels[PixelIndex].G.GetFloat() - ExpectedVelocityY) < 1e-3f
				&& FMath::Abs(VelocityPixels[PixelIndex].B.GetFloat() - ExpectedSpeed) < 1e-3f;
			MismatchedCells += bMatches ? 0 : 1;

			const float ScaledDensity = DensityPressurePixels[PixelIndex].R.GetFloat() * 255.0f;
			CellsBeyondEightBitSteps += FMath::Abs(ScaledDensity - FMath::RoundToFloat(ScaledDensity)) > 0.05f ? 1 : 0;
			CellsWithDensity += DensityPressurePixels[PixelIndex].R.GetFloat() > 0.0f ? 1 : 0;
		}
	}

	TestEqual(TEXT("Every packed value keeps the 0.5 offset encoding within half precision"), MismatchedCells, 0);
	TestTrue(TEXT("The stamp wrote density"), CellsWithDensity > 0);
	TestTrue(TEXT("Densities resolve finer than 1/255 steps"), CellsBeyondEightBitSteps > 0);
	TestNearlyEqual(TEXT("Center border alpha is fully opaque"), DensityPressurePixels[32 * Resolution + 32].A.GetFloat(), 1.0f, 1e-3f);
	TestTrue(TEXT("Outermost border alpha is below the center alpha"), DensityPressurePixels[32 * Resolution].A.GetFloat() < 0.99f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidAmbientRefillTest,
	"TDGame.FluidFX.Volume.AmbientDensityRefillsAndIsFrameRateInvariant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidAmbientRefillTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidVolumePrivate;

	const float DensityAt30Fps = RunAmbientFill(1.0f / 30.0f, 1.0f);
	const float DensityAt120Fps = RunAmbientFill(1.0f / 120.0f, 1.0f);
	const float ExpectedAfterOneSecond = 0.8f * (1.0f - FMath::Exp(-0.5f));
	AddInfo(FString::Printf(TEXT("ambient after 1 s: 30fps=%.5f 120fps=%.5f expected=%.5f"), DensityAt30Fps, DensityAt120Fps, ExpectedAfterOneSecond));
	TestTrue(*FString::Printf(TEXT("Ambient fill after 1 s at 30 fps is within 1%% of A * (1 - exp(-rate * t)) (%.5f vs %.5f)"), DensityAt30Fps, ExpectedAfterOneSecond), FMath::Abs(DensityAt30Fps - ExpectedAfterOneSecond) <= 0.01f * ExpectedAfterOneSecond);
	TestTrue(*FString::Printf(TEXT("Ambient fill after 1 s at 120 fps is within 1%% of A * (1 - exp(-rate * t)) (%.5f vs %.5f)"), DensityAt120Fps, ExpectedAfterOneSecond), FMath::Abs(DensityAt120Fps - ExpectedAfterOneSecond) <= 0.01f * ExpectedAfterOneSecond);

	const float ConvergedDensity = RunAmbientFill(SixtyFpsSeconds, 14.0f);
	TestNearlyEqual(TEXT("Ambient fill converges to AmbientDensity"), ConvergedDensity, 0.8f, 0.005f);

	FTDFluidSimulationConfig NoRefillConfig = MakeSmokeConfig();
	NoRefillConfig.AmbientRefillPerSec = 0.0f;
	FTDFluidSimulationConfig NoRefillWithTargetConfig = NoRefillConfig;
	NoRefillWithTargetConfig.AmbientDensity = 0.5f;
	FTDFluidGrid2DSolver NoRefillSolver;
	FTDFluidGrid2DSolver NoRefillWithTargetSolver;
	NoRefillSolver.Initialize(NoRefillConfig, FVector2D::ZeroVector);
	NoRefillWithTargetSolver.Initialize(NoRefillWithTargetConfig, FVector2D::ZeroVector);
	StepRepeatedly(NoRefillSolver, 0.5f, SixtyFpsSeconds);
	StepRepeatedly(NoRefillWithTargetSolver, 0.5f, SixtyFpsSeconds);
	TestEqual(TEXT("A zero refill rate leaves the state untouched whatever the target"), NoRefillWithTargetSolver.ComputeStateChecksum(), NoRefillSolver.ComputeStateChecksum());
	TestEqual(TEXT("A zero refill rate adds no density"), NoRefillSolver.SampleAtCell(20, 20).PrimaryDensity, 0.0f);

	FTDFluidGrid2DSolver ObstacleSolver;
	ObstacleSolver.Initialize(MakeSmokeConfig(), FVector2D::ZeroVector);
	FTDFluidSdfObstacle Pillar;
	Pillar.WorldCenter = FVector::ZeroVector;
	Pillar.RadiusCm = 100.0f;
	ObstacleSolver.RasterizeSdfObstacles({ Pillar });
	StepRepeatedly(ObstacleSolver, 1.0f, SixtyFpsSeconds);
	TestEqual(TEXT("Solid cells never receive ambient density"), ObstacleSolver.SampleAtCell(32, 32).PrimaryDensity, 0.0f);
	TestTrue(TEXT("Free cells receive ambient density"), ObstacleSolver.SampleAtCell(5, 5).PrimaryDensity > 0.1f);

	FTDFluidSimulationConfig ExtremeConfig = MakeSmokeConfig();
	ExtremeConfig.AmbientDensity = 5.0f;
	ExtremeConfig.AmbientRefillPerSec = -3.0f;
	ExtremeConfig.ActorBrushPunctureCmPerSec = 99999.0f;
	FTDFluidGrid2DSolver ExtremeSolver;
	ExtremeSolver.Initialize(ExtremeConfig, FVector2D::ZeroVector);
	TestNearlyEqual(TEXT("AmbientDensity is clamped to 1"), ExtremeSolver.GetConfig().AmbientDensity, 1.0f, 1e-6f);
	TestNearlyEqual(TEXT("A negative refill rate is clamped to 0"), ExtremeSolver.GetConfig().AmbientRefillPerSec, 0.0f, 1e-6f);
	TestNearlyEqual(TEXT("Puncture speed is clamped to the cell speed limit"), ExtremeSolver.GetConfig().ActorBrushPunctureCmPerSec, 1200.0f, 1e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidEraseBrushTest,
	"TDGame.FluidFX.Volume.ErasingBrushIsFrameRateInvariantAndChannelScoped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidEraseBrushTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidVolumePrivate;

	const TArray<FIntPoint> SampledCells = { FIntPoint(31, 31), FIntPoint(33, 31), FIntPoint(36, 32), FIntPoint(40, 32), FIntPoint(32, 38), FIntPoint(26, 26) };
	const TArray<float> DensitiesAt30Fps = RunEraseOneSecond(1.0f / 30.0f, ETDFluidBrushChannel::ChannelR_Mud, SampledCells);
	const TArray<float> DensitiesAt120Fps = RunEraseOneSecond(1.0f / 120.0f, ETDFluidBrushChannel::ChannelR_Mud, SampledCells);

	FTDFluidGrid2DSolver ReferenceSolver;
	ReferenceSolver.Initialize(MakeSmokeConfig(), FVector2D::ZeroVector);
	for (int32 CellIndex = 0; CellIndex < SampledCells.Num(); ++CellIndex)
	{
		const float DistanceCm = static_cast<float>(FVector2D::Distance(ReferenceSolver.CellToWorldCenter(SampledCells[CellIndex].X, SampledCells[CellIndex].Y), FVector2D::ZeroVector));
		const float ExpectedDensity = 0.8f * FMath::Exp(-8.0f * ComputeSmoothWeight(DistanceCm, 120.0f));
		AddInfo(FString::Printf(TEXT("erase cell=(%d,%d) 30fps=%.5f 120fps=%.5f expected=%.5f"), SampledCells[CellIndex].X, SampledCells[CellIndex].Y, DensitiesAt30Fps[CellIndex], DensitiesAt120Fps[CellIndex], ExpectedDensity));
		TestTrue(*FString::Printf(TEXT("Erase cell %d matches d * exp(-8 * weight * t) at 30 fps"), CellIndex), FMath::Abs(DensitiesAt30Fps[CellIndex] - ExpectedDensity) <= 0.01f * ExpectedDensity + 1e-5f);
		TestTrue(*FString::Printf(TEXT("Erase cell %d matches d * exp(-8 * weight * t) at 120 fps"), CellIndex), FMath::Abs(DensitiesAt120Fps[CellIndex] - ExpectedDensity) <= 0.01f * ExpectedDensity + 1e-5f);
	}

	TestTrue(TEXT("Erasing removes most density at the stamp center"), DensitiesAt120Fps[0] < 0.01f);

	FTDFluidGrid2DSolver Solver;
	FillWithAmbientDensity(Solver);
	FTDFluidBrushStamp SelectiveStamp;
	SelectiveStamp.WorldLocation = FVector::ZeroVector;
	SelectiveStamp.RadiusCm = 200.0f;
	SelectiveStamp.DensityStrength = 1.0f;
	SelectiveStamp.VelocityStrength = 0.0f;
	SelectiveStamp.Channel = ETDFluidBrushChannel::ChannelB_Fire;
	Solver.ApplyBrushStamps({ SelectiveStamp }, SixtyFpsSeconds);
	const float SelectiveBefore = Solver.SampleAtCell(31, 31).SelectiveDensity;

	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		Solver.ApplyBrushStamps({ MakeEraseStamp(120.0f, ETDFluidBrushChannel::ChannelR_Mud) }, SixtyFpsSeconds);
		Solver.StepSimulation(SixtyFpsSeconds);
	}
	TestNearlyEqual(TEXT("Erasing on the mud channel keeps selective density"), Solver.SampleAtCell(31, 31).SelectiveDensity, SelectiveBefore, 1e-4f);

	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		Solver.ApplyBrushStamps({ MakeEraseStamp(120.0f, ETDFluidBrushChannel::ChannelB_Fire) }, SixtyFpsSeconds);
		Solver.StepSimulation(SixtyFpsSeconds);
	}
	TestTrue(TEXT("Erasing on the fire channel removes selective density"), Solver.SampleAtCell(31, 31).SelectiveDensity < 0.01f * SelectiveBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRadialPushTest,
	"TDGame.FluidFX.Volume.RadialVelocityBlendsOutwardIntoCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidRadialPushTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidVolumePrivate;

	FTDFluidGrid2DSolver PushSolver;
	PushSolver.Initialize(MakeSmokeConfig(), FVector2D::ZeroVector);
	PushSolver.ApplyBrushStamps({ MakeRadialPushStamp(150.0f, 380.0f) }, SixtyFpsSeconds);
	TestNearlyEqual(TEXT("Stamping alone leaves the velocity field untouched"), MeasureMaxSpeedCmPerSec(PushSolver), 0.0f, 1e-6f);
	PushSolver.StepSimulation(SixtyFpsSeconds);
	TestNearlyEqual(TEXT("Radial push leaves no velocity in the field after the step"), MeasureMaxSpeedCmPerSec(PushSolver), 0.0f, 1e-6f);

	const FTDFluidGrid2DSolver SinglePushSolver = StepBlobWithStackedPushes(1);
	const FTDFluidGrid2DSolver TwoPushSolver = StepBlobWithStackedPushes(2);
	const FTDFluidGrid2DSolver ThreePushSolver = StepBlobWithStackedPushes(3);
	float MaxCappedDifference = 0.0f;
	float MaxUncappedDifference = 0.0f;
	const int32 Resolution = TwoPushSolver.GetResolution();
	for (int32 CellY = 0; CellY < Resolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < Resolution; ++CellX)
		{
			const double DistanceCm = TwoPushSolver.CellToWorldCenter(CellX, CellY).Size();
			if (DistanceCm < 25.0 || DistanceCm > 60.0)
			{
				continue;
			}

			const float TwoPushDensity = TwoPushSolver.SampleAtCell(CellX, CellY).PrimaryDensity;
			MaxCappedDifference = FMath::Max(MaxCappedDifference, FMath::Abs(TwoPushDensity - ThreePushSolver.SampleAtCell(CellX, CellY).PrimaryDensity));
			MaxUncappedDifference = FMath::Max(MaxUncappedDifference, FMath::Abs(TwoPushDensity - SinglePushSolver.SampleAtCell(CellX, CellY).PrimaryDensity));
		}
	}
	AddInfo(FString::Printf(TEXT("stacked push: 2 vs 3 stamps max density difference=%.6f, 2 vs 1 stamp=%.6f"), MaxCappedDifference, MaxUncappedDifference));
	TestTrue(*FString::Printf(TEXT("Pushes stacked past 1200 cm/s move density identically (%.6f)"), MaxCappedDifference), MaxCappedDifference < 1e-4f);
	TestTrue(*FString::Printf(TEXT("A single push below the cap moves density differently (%.6f)"), MaxUncappedDifference), MaxUncappedDifference > 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRadialPushMovesDensityTest,
	"TDGame.FluidFX.Volume.RadialPushMovesDensityOutwardAndIsFrameRateInvariant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidRadialPushMovesDensityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidVolumePrivate;

	const FTDFluidRadialPushOutcome BlobControl = RunRadialPushOnBlob(1.0f / 60.0f, 0.0f);
	const FTDFluidRadialPushOutcome BlobAt30Fps = RunRadialPushOnBlob(1.0f / 30.0f, 380.0f);
	const FTDFluidRadialPushOutcome BlobAt120Fps = RunRadialPushOnBlob(1.0f / 120.0f, 380.0f);
	AddInfo(FString::Printf(TEXT("blob: control center=%.4f ring=%.4f | 30fps center=%.4f ring=%.4f | 120fps center=%.4f ring=%.4f"), BlobControl.CenterDensity, BlobControl.RingDensity, BlobAt30Fps.CenterDensity, BlobAt30Fps.RingDensity, BlobAt120Fps.CenterDensity, BlobAt120Fps.RingDensity));
	TestTrue(*FString::Printf(TEXT("The push carries blob density into the outer ring (%.4f -> %.4f)"), BlobControl.RingDensity, BlobAt120Fps.RingDensity), BlobAt120Fps.RingDensity > BlobControl.RingDensity + 0.05f);
	TestTrue(*FString::Printf(TEXT("The push does not pile density up at the blob center (%.4f -> %.4f)"), BlobControl.CenterDensity, BlobAt120Fps.CenterDensity), BlobAt120Fps.CenterDensity <= BlobControl.CenterDensity + 0.05f);
	const float BlobRingGain30Fps = BlobAt30Fps.RingDensity - BlobControl.RingDensity;
	const float BlobRingGain120Fps = BlobAt120Fps.RingDensity - BlobControl.RingDensity;
	TestTrue(*FString::Printf(TEXT("Ring gain at 30 fps matches 120 fps within 10%% (%.4f vs %.4f)"), BlobRingGain30Fps, BlobRingGain120Fps), FMath::Abs(BlobRingGain30Fps - BlobRingGain120Fps) <= 0.1f * BlobRingGain120Fps);

	const FTDFluidRadialPushOutcome SmokeControl = RunRadialPushThroughSmoke(1.0f / 60.0f, 0.0f);
	const FTDFluidRadialPushOutcome SmokeAt30Fps = RunRadialPushThroughSmoke(1.0f / 30.0f, 380.0f);
	const FTDFluidRadialPushOutcome SmokeAt120Fps = RunRadialPushThroughSmoke(1.0f / 120.0f, 380.0f);
	AddInfo(FString::Printf(TEXT("smoke: control center=%.4f ring=%.4f | 30fps center=%.4f ring=%.4f | 120fps center=%.4f ring=%.4f"), SmokeControl.CenterDensity, SmokeControl.RingDensity, SmokeAt30Fps.CenterDensity, SmokeAt30Fps.RingDensity, SmokeAt120Fps.CenterDensity, SmokeAt120Fps.RingDensity));
	TestTrue(*FString::Printf(TEXT("The push widens the hole carved in uniform smoke (%.4f -> %.4f)"), SmokeControl.RingDensity, SmokeAt120Fps.RingDensity), SmokeAt120Fps.RingDensity < SmokeControl.RingDensity - 0.02f);
	const float SmokeRingLoss30Fps = SmokeControl.RingDensity - SmokeAt30Fps.RingDensity;
	const float SmokeRingLoss120Fps = SmokeControl.RingDensity - SmokeAt120Fps.RingDensity;
	TestTrue(*FString::Printf(TEXT("Ring loss at 30 fps matches 120 fps within 10%% (%.4f vs %.4f)"), SmokeRingLoss30Fps, SmokeRingLoss120Fps), FMath::Abs(SmokeRingLoss30Fps - SmokeRingLoss120Fps) <= 0.1f * SmokeRingLoss120Fps);

	FTDFluidGrid2DSolver UniformSolver;
	FillWithAmbientDensity(UniformSolver);
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		UniformSolver.ApplyBrushStamps({ MakeRadialPushStamp(200.0f, 380.0f) }, SixtyFpsSeconds);
		UniformSolver.StepSimulation(SixtyFpsSeconds);
	}
	const float UniformCenter = MeasureMeanDensityInAnnulus(UniformSolver, 0.0f, 15.0f);
	const float UniformRing = MeasureMeanDensityInAnnulus(UniformSolver, 110.0f, 190.0f);
	AddInfo(FString::Printf(TEXT("uniform 0.8 with push only: center=%.4f ring=%.4f"), UniformCenter, UniformRing));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSmokeCutAndRefillTest,
	"TDGame.FluidFX.Volume.SmokeIsCutByActorStampAndRefills",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSmokeCutAndRefillTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidVolumePrivate;

	const FTDFluidSimulationConfig Config = MakeSmokeConfig();
	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Config, FVector2D::ZeroVector);
	StepRepeatedly(Solver, 8.0f, 1.0f / 30.0f);
	const float FilledCenter = MeasureMeanDensityInAnnulus(Solver, 0.0f, 15.0f);
	TestTrue(*FString::Printf(TEXT("Ambient smoke fills the box (%.4f)"), FilledCenter), FilledCenter > 0.7f);

	FTDFluidBrushStamp ActorStamp;
	ActorStamp.WorldLocation = FVector::ZeroVector;
	ActorStamp.RadiusCm = 120.0f;
	ActorStamp.DensityStrength = Config.ActorBrushDensityStrength;
	ActorStamp.VelocityStrength = 0.0f;
	ActorStamp.RadialVelocityCmPerSec = Config.ActorBrushPunctureCmPerSec;
	for (int32 Frame = 0; Frame < 90; ++Frame)
	{
		Solver.ApplyBrushStamps({ ActorStamp }, SixtyFpsSeconds);
		Solver.StepSimulation(SixtyFpsSeconds);
	}
	const float CutCenter = MeasureMeanDensityInAnnulus(Solver, 0.0f, 15.0f);
	const float FarDensity = MeasureMeanDensityInAnnulus(Solver, 260.0f, 330.0f);
	AddInfo(FString::Printf(TEXT("smoke cut: filled=%.4f cutCenter=%.4f far=%.4f"), FilledCenter, CutCenter, FarDensity));
	TestTrue(*FString::Printf(TEXT("The actor stamp cuts a hole in the smoke (%.4f)"), CutCenter), CutCenter < 0.2f);
	TestTrue(*FString::Printf(TEXT("Smoke far from the actor stays dense (%.4f)"), FarDensity), FarDensity > 0.5f);

	StepRepeatedly(Solver, 8.0f, 1.0f / 30.0f);
	const float RefilledCenter = MeasureMeanDensityInAnnulus(Solver, 0.0f, 15.0f);
	AddInfo(FString::Printf(TEXT("smoke refill: center=%.4f"), RefilledCenter));
	TestTrue(*FString::Printf(TEXT("The hole refills after the actor leaves (%.4f)"), RefilledCenter), RefilledCenter > 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSmokePresetContractTest,
	"TDGame.FluidFX.Volume.SmokePresetAndVolumeFieldsMatchContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSmokePresetContractTest::RunTest(const FString& Parameters)
{
	const FTDFluidSimulationConfig Smoke = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Smoke);
	TestTrue(TEXT("Smoke runs the fluid solver"), Smoke.SimMode == ETDFluidSimMode::FluidSim);
	TestEqual(TEXT("Smoke resolution"), Smoke.GridResolution, 256);
	TestTrue(TEXT("Smoke extent is 700 x 700 cm"), Smoke.SimExtentCm.Equals(FVector2D(700.0, 700.0), 1e-3));
	TestNearlyEqual(TEXT("Smoke AmbientDensity"), Smoke.AmbientDensity, 0.8f, 1e-6f);
	TestNearlyEqual(TEXT("Smoke AmbientRefillPerSec"), Smoke.AmbientRefillPerSec, 0.5f, 1e-6f);
	TestNearlyEqual(TEXT("Smoke VelocityDamping"), Smoke.VelocityDamping, 0.08f, 1e-6f);
	TestNearlyEqual(TEXT("Smoke DensityDissipation"), Smoke.DensityDissipation, 0.0f, 1e-6f);
	TestNearlyEqual(TEXT("Smoke VorticityStrength"), Smoke.VorticityStrength, 1.2f, 1e-6f);
	TestNearlyEqual(TEXT("Smoke ActorBrushDensityStrength"), Smoke.ActorBrushDensityStrength, -1.0f, 1e-6f);
	TestNearlyEqual(TEXT("Smoke ActorBrushPunctureCmPerSec"), Smoke.ActorBrushPunctureCmPerSec, 380.0f, 1e-6f);
	TestNearlyEqual(TEXT("Smoke TerrainSlopeGravity"), Smoke.TerrainSlopeGravity, 0.0f, 1e-6f);

	TestEqual(TEXT("Smoke is appended after every existing preset kind"), static_cast<int32>(ETDFluidPresetKind::Smoke), static_cast<int32>(ETDFluidPresetKind::FootprintPainter) + 1);

	const FTDFluidSimulationConfig WaterConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Water);
	TestNearlyEqual(TEXT("Other presets keep AmbientDensity at 0"), WaterConfig.AmbientDensity, 0.0f, 1e-6f);
	TestNearlyEqual(TEXT("Other presets keep AmbientRefillPerSec at 0"), WaterConfig.AmbientRefillPerSec, 0.0f, 1e-6f);
	TestNearlyEqual(TEXT("Other presets keep ActorBrushDensityStrength at 1"), WaterConfig.ActorBrushDensityStrength, 1.0f, 1e-6f);
	TestNearlyEqual(TEXT("Other presets keep ActorBrushPunctureCmPerSec at 0"), WaterConfig.ActorBrushPunctureCmPerSec, 0.0f, 1e-6f);
	TestNearlyEqual(TEXT("A default stamp has no radial velocity"), FTDFluidBrushStamp().RadialVelocityCmPerSec, 0.0f, 1e-6f);

	UClass* PresetClass = UTDFluidPresetAsset::StaticClass();
	TestNotNull(TEXT("VolumeMaterial is a reflected soft object property"), FindFProperty<FSoftObjectProperty>(PresetClass, TEXT("VolumeMaterial")));
	TestNotNull(TEXT("VolumeMesh is a reflected soft object property"), FindFProperty<FSoftObjectProperty>(PresetClass, TEXT("VolumeMesh")));
	TestNotNull(TEXT("VolumeHeightCm is a reflected float property"), FindFProperty<FFloatProperty>(PresetClass, TEXT("VolumeHeightCm")));

	const UTDFluidPresetAsset* Preset = NewObject<UTDFluidPresetAsset>(GetTransientPackage());
	TestTrue(TEXT("VolumeMaterial is empty by default"), Preset->VolumeMaterial.IsNull());
	TestTrue(TEXT("VolumeMesh is empty by default"), Preset->VolumeMesh.IsNull());
	TestNearlyEqual(TEXT("VolumeHeightCm defaults to 150"), Preset->VolumeHeightCm, 150.0f, 1e-6f);

	UTDFluidPresetAsset* SmokePreset = UTDFluidPresetAsset::CreateTransientPreset(GetTransientPackage(), ETDFluidPresetKind::Smoke);
	TestNearlyEqual(TEXT("A transient Smoke preset resolves the Smoke ambient density"), SmokePreset->ResolveEffectiveConfig().AmbientDensity, 0.8f, 1e-6f);
	return true;
}

#endif
