#if WITH_DEV_AUTOMATION_TESTS

#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace TDFluidFlowNoisePrivate
{
	constexpr float SixtyFpsSeconds = 1.0f / 60.0f;
	constexpr float CellSpeedLimitCmPerSec = 1200.0f;
	constexpr int32 ChecksumStepCount = 120;
	constexpr float ChecksumStepSecondsCycle[] = { 1.0f / 60.0f, 1.0f / 30.0f, 1.0f / 45.0f, 0.15f };

	struct FTDFluidFieldStats
	{
		double MeanSpeedCmPerSec = 0.0;
		double DensitySum = 0.0;
		double DensityMean = 0.0;
		double DensityVariance = 0.0;
		float MinDensity = 1.0f;
		float MaxDensity = 0.0f;
	};

	FTDFluidSimulationConfig MakeNoiseConfig(int32 Resolution, double ExtentCm)
	{
		FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Smoke);
		Config.GridResolution = Resolution;
		Config.SimExtentCm = FVector2D(ExtentCm, ExtentCm);
		return Config;
	}

	FTDFluidSimulationConfig MakeQuietConfig(int32 Resolution, double ExtentCm)
	{
		FTDFluidSimulationConfig Config = MakeNoiseConfig(Resolution, ExtentCm);
		Config.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Config.AmbientNoiseAmount = 0.0f;
		return Config;
	}

	FTDFluidBrushStamp MakeOrbitingStamp(int32 Step)
	{
		const double Angle = Step * 0.11;
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = FVector(FMath::Cos(Angle) * 150.0, FMath::Sin(Angle) * 150.0, 0.0);
		Stamp.WorldVelocity = FVector(-FMath::Sin(Angle), FMath::Cos(Angle), 0.0) * 600.0;
		Stamp.RadiusCm = 80.0f;
		Stamp.DensityStrength = 1.0f;
		Stamp.VelocityStrength = 1.0f;
		Stamp.Channel = ETDFluidBrushChannel::ChannelA_Water;
		return Stamp;
	}

	float MeasureMaxSpeedCmPerSec(const FTDFluidGrid2DSolver& Solver)
	{
		float MaxSpeed = 0.0f;
		for (int32 CellY = 0; CellY < Solver.GetResolution(); ++CellY)
		{
			for (int32 CellX = 0; CellX < Solver.GetResolution(); ++CellX)
			{
				MaxSpeed = FMath::Max(MaxSpeed, static_cast<float>(Solver.SampleAtCell(CellX, CellY).Velocity.Size()));
			}
		}
		return MaxSpeed;
	}

	uint32 RunStampedScenarioChecksum(const FTDFluidSimulationConfig& Config, float& OutMaxSpeedCmPerSec)
	{
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		for (int32 Step = 0; Step < ChecksumStepCount; ++Step)
		{
			const float DeltaTime = ChecksumStepSecondsCycle[Step % UE_ARRAY_COUNT(ChecksumStepSecondsCycle)];
			Solver.ApplyBrushStamps({ MakeOrbitingStamp(Step) }, DeltaTime);
			Solver.StepSimulation(DeltaTime);
		}
		OutMaxSpeedCmPerSec = MeasureMaxSpeedCmPerSec(Solver);
		return Solver.ComputeStateChecksum();
	}

	FTDFluidFieldStats MeasureField(const FTDFluidGrid2DSolver& Solver)
	{
		FTDFluidFieldStats Stats;
		const int32 Resolution = Solver.GetResolution();
		const double CellCount = static_cast<double>(Resolution) * Resolution;
		double SpeedSum = 0.0;
		double DensitySquaredSum = 0.0;
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const FTDFluidCellSample Sample = Solver.SampleAtCell(CellX, CellY);
				SpeedSum += Sample.Velocity.Size();
				Stats.DensitySum += Sample.PrimaryDensity;
				DensitySquaredSum += static_cast<double>(Sample.PrimaryDensity) * Sample.PrimaryDensity;
				Stats.MinDensity = FMath::Min(Stats.MinDensity, Sample.PrimaryDensity);
				Stats.MaxDensity = FMath::Max(Stats.MaxDensity, Sample.PrimaryDensity);
			}
		}
		Stats.MeanSpeedCmPerSec = SpeedSum / CellCount;
		Stats.DensityMean = Stats.DensitySum / CellCount;
		Stats.DensityVariance = DensitySquaredSum / CellCount - Stats.DensityMean * Stats.DensityMean;
		return Stats;
	}

	FTDFluidFieldStats RunAndMeasure(const FTDFluidSimulationConfig& Config, float DeltaTime, float TotalSeconds)
	{
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		const int32 StepCount = FMath::RoundToInt(TotalSeconds / DeltaTime);
		for (int32 Step = 0; Step < StepCount; ++Step)
		{
			Solver.StepSimulation(DeltaTime);
		}
		return MeasureField(Solver);
	}

	double ComputeRelativeDivergence(const TArray<FVector2D>& Field, int32 Resolution, double CellSizeCm, double& OutMeanMagnitude)
	{
		double DivergenceSum = 0.0;
		double MagnitudeSum = 0.0;
		for (int32 CellY = 1; CellY < Resolution - 1; ++CellY)
		{
			for (int32 CellX = 1; CellX < Resolution - 1; ++CellX)
			{
				const int32 Idx = CellY * Resolution + CellX;
				const double Divergence = (Field[Idx + 1].X - Field[Idx - 1].X) / (2.0 * CellSizeCm) + (Field[Idx + Resolution].Y - Field[Idx - Resolution].Y) / (2.0 * CellSizeCm);
				DivergenceSum += FMath::Abs(Divergence);
				MagnitudeSum += Field[Idx].Size();
			}
		}
		const double InteriorCount = static_cast<double>(Resolution - 2) * (Resolution - 2);
		OutMeanMagnitude = MagnitudeSum / InteriorCount;
		return MagnitudeSum > 0.0 ? DivergenceSum * CellSizeCm / MagnitudeSum : 0.0;
	}

	double MeasureStepMilliseconds(const FTDFluidSimulationConfig& Config, int32 StepCount)
	{
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		for (int32 WarmupStep = 0; WarmupStep < 10; ++WarmupStep)
		{
			Solver.StepSimulation(SixtyFpsSeconds);
		}

		const double StartSeconds = FPlatformTime::Seconds();
		for (int32 Step = 0; Step < StepCount; ++Step)
		{
			Solver.StepSimulation(SixtyFpsSeconds);
		}
		return (FPlatformTime::Seconds() - StartSeconds) * 1000.0 / StepCount;
	}

	double MeasureFieldMilliseconds(const FTDFluidGrid2DSolver& Solver, bool bForceSingleThread, int32 CallCount)
	{
		TArray<FVector2D> Field;
		Solver.ComputeFlowNoiseAccelerationField(Field, bForceSingleThread);

		const double StartSeconds = FPlatformTime::Seconds();
		for (int32 Call = 0; Call < CallCount; ++Call)
		{
			Solver.ComputeFlowNoiseAccelerationField(Field, bForceSingleThread);
		}
		return (FPlatformTime::Seconds() - StartSeconds) * 1000.0 / CallCount;
	}

	bool AreFieldsBitIdentical(const TArray<FVector2D>& Left, const TArray<FVector2D>& Right)
	{
		return Left.Num() == Right.Num() && FMemory::Memcmp(Left.GetData(), Right.GetData(), Left.Num() * sizeof(FVector2D)) == 0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFlowNoiseDeterministicTest,
	"TDGame.FluidFX.FlowNoise.DeterministicStateAfter120Steps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFlowNoiseDeterministicTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFlowNoisePrivate;

	const FTDFluidSimulationConfig NoiseConfig = MakeNoiseConfig(64, 700.0);
	float FirstMaxSpeed = 0.0f;
	float SecondMaxSpeed = 0.0f;
	const uint32 FirstChecksum = RunStampedScenarioChecksum(NoiseConfig, FirstMaxSpeed);
	const uint32 SecondChecksum = RunStampedScenarioChecksum(NoiseConfig, SecondMaxSpeed);
	TestEqual(TEXT("Two solvers with the same noise config share one state checksum after 120 steps"), FirstChecksum, SecondChecksum);

	float QuietMaxSpeed = 0.0f;
	const uint32 QuietChecksum = RunStampedScenarioChecksum(MakeQuietConfig(64, 700.0), QuietMaxSpeed);
	TestTrue(TEXT("Turning the noise on changes the simulated state"), FirstChecksum != QuietChecksum);
	TestTrue(*FString::Printf(TEXT("Noisy cells stay under the cell speed limit (%.2f)"), FirstMaxSpeed), FirstMaxSpeed <= CellSpeedLimitCmPerSec + 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFlowNoiseDivergenceTest,
	"TDGame.FluidFX.FlowNoise.NoiseVelocityFieldIsNearlyDivergenceFree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFlowNoiseDivergenceTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFlowNoisePrivate;

	struct FDivergenceCase
	{
		const TCHAR* Name;
		int32 Resolution;
		double ExtentCm;
	};
	const FDivergenceCase Cases[] = {
		{ TEXT("smoke 700 cm"), 128, 700.0 },
		{ TEXT("wide 4000 cm"), 128, 4000.0 },
	};

	for (const FDivergenceCase& Case : Cases)
	{
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(MakeNoiseConfig(Case.Resolution, Case.ExtentCm), FVector2D::ZeroVector);
		for (int32 Step = 0; Step < 10; ++Step)
		{
			Solver.StepSimulation(SixtyFpsSeconds);
		}

		TArray<FVector2D> Field;
		Solver.ComputeFlowNoiseAccelerationField(Field);
		const double CellSizeCm = Case.ExtentCm / Case.Resolution;
		double MeanMagnitude = 0.0;
		const double RelativeDivergence = ComputeRelativeDivergence(Field, Case.Resolution, CellSizeCm, MeanMagnitude);
		AddInfo(FString::Printf(TEXT("flow noise divergence (%s): |div|*h/|v|=%.5f mean|a|=%.2f cm/s2"), Case.Name, RelativeDivergence, MeanMagnitude));
		TestTrue(*FString::Printf(TEXT("%s noise field is not trivial (mean %.2f cm/s2)"), Case.Name, MeanMagnitude), MeanMagnitude > 20.0);
		TestTrue(*FString::Printf(TEXT("%s noise field divergence is small relative to its size (%.5f)"), Case.Name, RelativeDivergence), RelativeDivergence < 0.05);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFlowNoiseZeroStrengthTest,
	"TDGame.FluidFX.FlowNoise.ZeroStrengthLeavesStateBitIdentical",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFlowNoiseZeroStrengthTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFlowNoisePrivate;

	FTDFluidSimulationConfig DefaultNoiseFields = MakeQuietConfig(64, 700.0);
	DefaultNoiseFields.FlowNoiseScaleCm = FTDFluidSimulationConfig().FlowNoiseScaleCm;
	DefaultNoiseFields.FlowNoiseSpeed = FTDFluidSimulationConfig().FlowNoiseSpeed;

	FTDFluidSimulationConfig ChangedScaleAndSpeed = MakeQuietConfig(64, 700.0);
	ChangedScaleAndSpeed.FlowNoiseScaleCm = 90.0f;
	ChangedScaleAndSpeed.FlowNoiseSpeed = 3.7f;

	float DefaultMaxSpeed = 0.0f;
	float ChangedMaxSpeed = 0.0f;
	const uint32 DefaultChecksum = RunStampedScenarioChecksum(DefaultNoiseFields, DefaultMaxSpeed);
	const uint32 ChangedChecksum = RunStampedScenarioChecksum(ChangedScaleAndSpeed, ChangedMaxSpeed);
	TestEqual(TEXT("With zero acceleration and zero ambient amount the noise scale and speed cannot reach the state"), ChangedChecksum, DefaultChecksum);

	FTDFluidGrid2DSolver QuietSolver;
	QuietSolver.Initialize(ChangedScaleAndSpeed, FVector2D::ZeroVector);
	TArray<FVector2D> Field;
	QuietSolver.ComputeFlowNoiseAccelerationField(Field);
	bool bIsFieldZero = Field.Num() == 64 * 64;
	for (const FVector2D& Acceleration : Field)
	{
		bIsFieldZero = bIsFieldZero && Acceleration.IsZero();
	}
	TestTrue(TEXT("The acceleration field is exactly zero when the strength is zero"), bIsFieldZero);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFlowNoiseFrameRateTest,
	"TDGame.FluidFX.FlowNoise.AverageSpeedAndDensityAreFrameRateIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFlowNoiseFrameRateTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFlowNoisePrivate;

	const FTDFluidSimulationConfig Config = MakeNoiseConfig(64, 700.0);
	const FTDFluidFieldStats At30Fps = RunAndMeasure(Config, 1.0f / 30.0f, 2.0f);
	const FTDFluidFieldStats At120Fps = RunAndMeasure(Config, 1.0f / 120.0f, 2.0f);
	AddInfo(FString::Printf(TEXT("flow noise frame rate: 30fps speed=%.3f density=%.2f | 120fps speed=%.3f density=%.2f"), At30Fps.MeanSpeedCmPerSec, At30Fps.DensitySum, At120Fps.MeanSpeedCmPerSec, At120Fps.DensitySum));

	TestTrue(*FString::Printf(TEXT("The noise moves the smoke (mean speed %.3f cm/s)"), At120Fps.MeanSpeedCmPerSec), At120Fps.MeanSpeedCmPerSec > 1.0);
	TestTrue(*FString::Printf(TEXT("Mean speed at 30 fps matches 120 fps within 15%% (%.3f vs %.3f)"), At30Fps.MeanSpeedCmPerSec, At120Fps.MeanSpeedCmPerSec), FMath::Abs(At30Fps.MeanSpeedCmPerSec - At120Fps.MeanSpeedCmPerSec) <= 0.15 * At120Fps.MeanSpeedCmPerSec);
	TestTrue(*FString::Printf(TEXT("Density sum at 30 fps matches 120 fps within 15%% (%.2f vs %.2f)"), At30Fps.DensitySum, At120Fps.DensitySum), FMath::Abs(At30Fps.DensitySum - At120Fps.DensitySum) <= 0.15 * At120Fps.DensitySum);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFlowNoiseWorldFixedTest,
	"TDGame.FluidFX.FlowNoise.PatternStaysFixedToTheWorldWhenTheWindowSlides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFlowNoiseWorldFixedTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFlowNoisePrivate;

	constexpr int32 Resolution = 128;
	constexpr int32 ShiftCellsX = 7;
	constexpr int32 ShiftCellsY = -5;
	const FTDFluidSimulationConfig Config = MakeNoiseConfig(Resolution, 700.0);
	const double CellSizeCm = Config.SimExtentCm.X / Resolution;

	FTDFluidGrid2DSolver StaticSolver;
	FTDFluidGrid2DSolver SlidingSolver;
	StaticSolver.Initialize(Config, FVector2D::ZeroVector);
	SlidingSolver.Initialize(Config, FVector2D::ZeroVector);
	for (int32 Step = 0; Step < 30; ++Step)
	{
		StaticSolver.StepSimulation(SixtyFpsSeconds);
		SlidingSolver.StepSimulation(SixtyFpsSeconds);
	}
	TestTrue(TEXT("The window slides by whole cells"), SlidingSolver.ShiftOriginForSlidingWindow(FVector2D(ShiftCellsX * CellSizeCm, ShiftCellsY * CellSizeCm)));

	TArray<FVector2D> StaticField;
	TArray<FVector2D> SlidingField;
	StaticSolver.ComputeFlowNoiseAccelerationField(StaticField);
	SlidingSolver.ComputeFlowNoiseAccelerationField(SlidingField);

	double MaximumMagnitude = 0.0;
	for (const FVector2D& Acceleration : StaticField)
	{
		MaximumMagnitude = FMath::Max(MaximumMagnitude, Acceleration.Size());
	}

	double LargestWorldDifference = 0.0;
	double LargestUnshiftedDifference = 0.0;
	int32 ComparedCellCount = 0;
	for (int32 CellY = 0; CellY < Resolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < Resolution; ++CellX)
		{
			const int32 StaticX = CellX + ShiftCellsX;
			const int32 StaticY = CellY + ShiftCellsY;
			if (StaticX < 0 || StaticX >= Resolution || StaticY < 0 || StaticY >= Resolution)
			{
				continue;
			}

			const FVector2D& Sliding = SlidingField[CellY * Resolution + CellX];
			LargestWorldDifference = FMath::Max(LargestWorldDifference, (Sliding - StaticField[StaticY * Resolution + StaticX]).Size());
			LargestUnshiftedDifference = FMath::Max(LargestUnshiftedDifference, (Sliding - StaticField[CellY * Resolution + CellX]).Size());
			++ComparedCellCount;
		}
	}

	AddInfo(FString::Printf(TEXT("flow noise world fixed: max|a|=%.3f world difference=%.6f unshifted difference=%.3f over %d cells"), MaximumMagnitude, LargestWorldDifference, LargestUnshiftedDifference, ComparedCellCount));
	TestTrue(TEXT("Most cells overlap after the slide"), ComparedCellCount > Resolution * Resolution / 2);
	TestTrue(*FString::Printf(TEXT("The same world point keeps the same noise acceleration after the slide (difference %.6f)"), LargestWorldDifference), LargestWorldDifference <= 1e-4 * MaximumMagnitude);
	TestTrue(*FString::Printf(TEXT("The pattern really moved across the grid (unshifted difference %.3f)"), LargestUnshiftedDifference), LargestUnshiftedDifference > 0.05 * MaximumMagnitude);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFlowNoiseAmbientVarianceTest,
	"TDGame.FluidFX.FlowNoise.AmbientNoiseAddsBoundedDensityVariance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFlowNoiseAmbientVarianceTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFlowNoisePrivate;

	FTDFluidSimulationConfig Config = MakeQuietConfig(64, 2000.0);
	Config.AmbientRefillPerSec = 50.0f;
	const float AmbientDensity = Config.AmbientDensity;

	const FTDFluidFieldStats Flat = RunAndMeasure(Config, SixtyFpsSeconds, 1.0f);

	Config.AmbientNoiseAmount = 0.45f;
	const FTDFluidFieldStats Moderate = RunAndMeasure(Config, SixtyFpsSeconds, 1.0f);

	Config.AmbientNoiseAmount = 0.9f;
	const FTDFluidFieldStats Strong = RunAndMeasure(Config, SixtyFpsSeconds, 1.0f);

	AddInfo(FString::Printf(TEXT("ambient noise variance: flat=%.6f moderate=%.6f strong=%.6f | moderate range %.3f..%.3f mean %.3f"), Flat.DensityVariance, Moderate.DensityVariance, Strong.DensityVariance, Moderate.MinDensity, Moderate.MaxDensity, Moderate.DensityMean));

	TestTrue(*FString::Printf(TEXT("Without ambient noise the refilled density is flat (variance %.8f)"), Flat.DensityVariance), Flat.DensityVariance < 1e-6);
	TestTrue(*FString::Printf(TEXT("With ambient noise the density varies clearly (variance %.6f)"), Moderate.DensityVariance), Moderate.DensityVariance > 1e-4);
	TestTrue(*FString::Printf(TEXT("A larger noise amount widens the variation (%.6f vs %.6f)"), Strong.DensityVariance, Moderate.DensityVariance), Strong.DensityVariance > 2.5 * Moderate.DensityVariance);
	TestTrue(*FString::Printf(TEXT("The noisy density stays inside AmbientDensity * (1 - amount) and AmbientDensity (%.3f..%.3f)"), Moderate.MinDensity, Moderate.MaxDensity), Moderate.MinDensity >= AmbientDensity * (1.0f - 0.45f) - 0.02f && Moderate.MaxDensity <= AmbientDensity + 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFlowNoiseParallelAndResetTest,
	"TDGame.FluidFX.FlowNoise.ParallelFieldIsBitIdenticalToSerialAndResetRestartsTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFlowNoiseParallelAndResetTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFlowNoisePrivate;

	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(MakeNoiseConfig(256, 700.0), FVector2D(123.0, -456.0));

	TArray<FVector2D> InitialField;
	Solver.ComputeFlowNoiseAccelerationField(InitialField);

	for (int32 Step = 0; Step < 90; ++Step)
	{
		Solver.StepSimulation(SixtyFpsSeconds);
	}

	TArray<FVector2D> ParallelField;
	TArray<FVector2D> SerialField;
	Solver.ComputeFlowNoiseAccelerationField(ParallelField, false);
	Solver.ComputeFlowNoiseAccelerationField(SerialField, true);
	TestTrue(TEXT("The parallel noise field is bit-identical to the serial one"), AreFieldsBitIdentical(ParallelField, SerialField));
	TestFalse(TEXT("The noise pattern evolves with simulation time"), AreFieldsBitIdentical(ParallelField, InitialField));

	Solver.ResetSimulation();
	TArray<FVector2D> ResetField;
	Solver.ComputeFlowNoiseAccelerationField(ResetField);
	TestTrue(TEXT("Resetting the simulation restarts the noise time at zero"), AreFieldsBitIdentical(ResetField, InitialField));

	FTDFluidSimulationConfig ExtremeConfig = MakeNoiseConfig(64, 700.0);
	ExtremeConfig.FlowNoiseAccelerationCmPerSec2 = -5.0f;
	ExtremeConfig.FlowNoiseScaleCm = std::numeric_limits<float>::quiet_NaN();
	ExtremeConfig.FlowNoiseSpeed = -1.0f;
	ExtremeConfig.AmbientNoiseAmount = 7.0f;
	FTDFluidGrid2DSolver ExtremeSolver;
	ExtremeSolver.Initialize(ExtremeConfig, FVector2D::ZeroVector);
	TestNearlyEqual(TEXT("A negative noise acceleration is clamped to 0"), ExtremeSolver.GetConfig().FlowNoiseAccelerationCmPerSec2, 0.0f, 1e-6f);
	TestTrue(TEXT("A non-finite noise scale becomes a finite positive scale"), FMath::IsFinite(ExtremeSolver.GetConfig().FlowNoiseScaleCm) && ExtremeSolver.GetConfig().FlowNoiseScaleCm >= 1.0f);
	TestNearlyEqual(TEXT("A negative noise speed is clamped to 0"), ExtremeSolver.GetConfig().FlowNoiseSpeed, 0.0f, 1e-6f);
	TestNearlyEqual(TEXT("The ambient noise amount is clamped to 1"), ExtremeSolver.GetConfig().AmbientNoiseAmount, 1.0f, 1e-6f);

	for (int32 Step = 0; Step < 30; ++Step)
	{
		ExtremeSolver.StepSimulation(SixtyFpsSeconds);
	}
	TestTrue(TEXT("Extreme noise settings keep the solver finite"), FMath::IsFinite(MeasureMaxSpeedCmPerSec(ExtremeSolver)));

	FTDFluidSimulationConfig TinyScaleConfig = MakeNoiseConfig(256, 700.0);
	TinyScaleConfig.FlowNoiseScaleCm = 1.0f;
	FTDFluidGrid2DSolver TinyScaleSolver;
	TinyScaleSolver.Initialize(TinyScaleConfig, FVector2D::ZeroVector);
	for (int32 Step = 0; Step < 10; ++Step)
	{
		TinyScaleSolver.StepSimulation(SixtyFpsSeconds);
	}
	TestTrue(TEXT("A noise scale below one cell is stretched to the cell size and stays finite"), FMath::IsFinite(MeasureMaxSpeedCmPerSec(TinyScaleSolver)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFlowNoisePresetContractTest,
	"TDGame.FluidFX.FlowNoise.SmokePresetCarriesNoiseFactoryValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFlowNoisePresetContractTest::RunTest(const FString& Parameters)
{
	const FTDFluidSimulationConfig Smoke = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Smoke);
	TestNearlyEqual(TEXT("Smoke FlowNoiseAccelerationCmPerSec2"), Smoke.FlowNoiseAccelerationCmPerSec2, 70.0f, 1e-6f);
	TestNearlyEqual(TEXT("Smoke FlowNoiseScaleCm"), Smoke.FlowNoiseScaleCm, 220.0f, 1e-6f);
	TestNearlyEqual(TEXT("Smoke FlowNoiseSpeed"), Smoke.FlowNoiseSpeed, 0.12f, 1e-6f);
	TestNearlyEqual(TEXT("Smoke AmbientNoiseAmount"), Smoke.AmbientNoiseAmount, 0.45f, 1e-6f);

	const ETDFluidPresetKind OtherKinds[] = { ETDFluidPresetKind::Water, ETDFluidPresetKind::Mud, ETDFluidPresetKind::Blood, ETDFluidPresetKind::Fire, ETDFluidPresetKind::FootprintPainter, ETDFluidPresetKind::Custom };
	for (const ETDFluidPresetKind Kind : OtherKinds)
	{
		const FTDFluidSimulationConfig Other = UTDFluidPresetAsset::BuildConfigForPresetKind(Kind);
		TestNearlyEqual(*FString::Printf(TEXT("Preset %d keeps the flow noise off"), static_cast<int32>(Kind)), Other.FlowNoiseAccelerationCmPerSec2, 0.0f, 1e-6f);
		TestNearlyEqual(*FString::Printf(TEXT("Preset %d keeps the ambient noise off"), static_cast<int32>(Kind)), Other.AmbientNoiseAmount, 0.0f, 1e-6f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFlowNoiseStepCostTest,
	"TDGame.FluidFX.FlowNoise.StepCostOfNoiseIsMeasured",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFlowNoiseStepCostTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFlowNoisePrivate;

	constexpr int32 RepeatCount = 5;
	constexpr int32 StepsPerRepeat = 60;
	const FTDFluidSimulationConfig QuietConfig = MakeQuietConfig(128, 700.0);
	const FTDFluidSimulationConfig NoiseConfig = MakeNoiseConfig(128, 700.0);

	double QuietBestMilliseconds = TNumericLimits<double>::Max();
	double NoiseBestMilliseconds = TNumericLimits<double>::Max();
	for (int32 Repeat = 0; Repeat < RepeatCount; ++Repeat)
	{
		QuietBestMilliseconds = FMath::Min(QuietBestMilliseconds, MeasureStepMilliseconds(QuietConfig, StepsPerRepeat));
		NoiseBestMilliseconds = FMath::Min(NoiseBestMilliseconds, MeasureStepMilliseconds(NoiseConfig, StepsPerRepeat));
	}

	FTDFluidGrid2DSolver FieldSolver;
	FieldSolver.Initialize(NoiseConfig, FVector2D::ZeroVector);
	double ParallelFieldBestMilliseconds = TNumericLimits<double>::Max();
	double SerialFieldBestMilliseconds = TNumericLimits<double>::Max();
	for (int32 Repeat = 0; Repeat < RepeatCount; ++Repeat)
	{
		ParallelFieldBestMilliseconds = FMath::Min(ParallelFieldBestMilliseconds, MeasureFieldMilliseconds(FieldSolver, false, 100));
		SerialFieldBestMilliseconds = FMath::Min(SerialFieldBestMilliseconds, MeasureFieldMilliseconds(FieldSolver, true, 100));
	}

	const double ExtraMilliseconds = NoiseBestMilliseconds - QuietBestMilliseconds;
	AddInfo(FString::Printf(TEXT("TDFluidFlowNoiseBench: 128x128 step quiet=%.4f ms noise=%.4f ms extra=%.4f ms | flow field parallel=%.4f ms serial=%.4f ms"), QuietBestMilliseconds, NoiseBestMilliseconds, ExtraMilliseconds, ParallelFieldBestMilliseconds, SerialFieldBestMilliseconds));
	TestTrue(*FString::Printf(TEXT("The noise adds well under 2 ms to a 128x128 step (%.4f ms)"), ExtraMilliseconds), ExtraMilliseconds < 2.0);
	return true;
}

#endif
