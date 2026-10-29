#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidFoliageBendLayer.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "Math/Float16Color.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"

namespace TDFluidFoliageBendTestsPrivate
{
	constexpr float SixtyFpsSeconds = 1.0f / 60.0f;
	constexpr double DefaultCellSizeCm = 3.90625;
	constexpr int32 ProbeResolution = 4;
	constexpr int32 ProbeCellIndex = 1 * ProbeResolution + 1;
	constexpr int32 ChecksumResolution = 64;
	constexpr int32 ChecksumStepCount = 120;
	constexpr float ChecksumStepSecondsCycle[] = { 1.0f / 60.0f, 1.0f / 30.0f, 1.0f / 45.0f, 0.15f };
	constexpr int32 LagResolution = 64;
	constexpr double LagDeltaSeconds = 1.0 / 120.0;
	constexpr double LagDurationSeconds = 8.0;
	constexpr double LagMeasureStartSeconds = 4.0;
	constexpr double LagFrequencyHz = 1.0;
	constexpr double LagWavelengthCm = 250.0;
	constexpr double LagAmplitude = 0.4;
	constexpr float LagRippleWeight = 0.15f;
	constexpr float LagRiseSeconds = 0.08f;
	constexpr float LagAsymmetricRecoverySeconds = 0.6f;
	constexpr double LagAnalyticToleranceSeconds = 0.01;
	constexpr double PressRecordDurationSeconds = 20.0;

	FVector2D MakeCellSize()
	{
		return FVector2D(DefaultCellSizeCm, DefaultCellSizeCm);
	}

	FTDFluidSimulationConfig MakeBendConfig(float RiseSeconds, float RecoverySeconds)
	{
		FTDFluidSimulationConfig Config;
		Config.bEnableBendVectorChannel = true;
		Config.BendVectorRiseSeconds = RiseSeconds;
		Config.BendVectorRecoverySeconds = RecoverySeconds;
		return Config;
	}

	FTDFluidSimulationConfig MakePainterConfig(float RiseSeconds, float RecoverySeconds, float PainterDensity, bool bWritesVelocity)
	{
		FTDFluidSimulationConfig Config = MakeBendConfig(RiseSeconds, RecoverySeconds);
		Config.SimMode = ETDFluidSimMode::SimplePainter;
		Config.bWritesPainterVelocity = bWritesVelocity;
		Config.BendFromPainterDensity = PainterDensity;
		return Config;
	}

	FFloat16Color PackVelocity(const FVector2f& Velocity)
	{
		return FFloat16Color(FLinearColor(0.5f + 0.5f * FMath::Clamp(Velocity.X, -1.0f, 1.0f), 0.5f + 0.5f * FMath::Clamp(Velocity.Y, -1.0f, 1.0f), 0.0f, 1.0f));
	}

	FFloat16Color PackDensity(float Density)
	{
		return FFloat16Color(FLinearColor(Density, 0.0f, 0.0f, 0.0f));
	}

	FFloat16Color PackWaterHeight(double Height)
	{
		return FFloat16Color(FLinearColor(0.0f, 0.0f, static_cast<float>(0.5 + 0.5 * Height), 1.0f));
	}

	TArray<FFloat16Color> MakeUniformPixels(int32 Resolution, const FFloat16Color& Pixel)
	{
		TArray<FFloat16Color> Pixels;
		Pixels.Init(Pixel, Resolution * Resolution);
		return Pixels;
	}

	void RunUniformSteps(FTDFluidFoliageBendLayer& Layer, const FVector2f& Velocity, float Density, int32 StepCount, const FTDFluidFoliageBendParameters& Parameters)
	{
		const TArray<FFloat16Color> VelocityPixels = MakeUniformPixels(ProbeResolution, PackVelocity(Velocity));
		const TArray<FFloat16Color> DensityPixels = MakeUniformPixels(ProbeResolution, PackDensity(Density));
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			Layer.AdvanceCpu(VelocityPixels, DensityPixels, {}, ProbeResolution, FVector2D::ZeroVector, MakeCellSize(), Parameters);
		}
	}

	FVector2f GetProbeBend(const FTDFluidFoliageBendLayer& Layer)
	{
		const TArray<FVector2f>& State = Layer.GetCpuBendState();
		return State.IsValidIndex(ProbeCellIndex) ? State[ProbeCellIndex] : FVector2f::ZeroVector;
	}

	float ComputeLargestBend(const TArray<FVector2f>& State)
	{
		float Largest = 0.0f;
		for (const FVector2f& Bend : State)
		{
			Largest = FMath::Max(Largest, Bend.Size());
		}
		return Largest;
	}

	bool SaveFoliageBendRecord(const TSharedRef<FJsonObject>& Report, const TCHAR* FileName, FString& OutPath)
	{
		FString ReportText;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ReportText);
		FJsonSerializer::Serialize(Report, Writer);
		OutPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AgentOps"), FDateTime::Now().ToString(TEXT("%Y%m%d")), TEXT("fx13c"), FileName);
		return FFileHelper::SaveStringToFile(ReportText, *OutPath);
	}

	FString GetActiveTuningFilePath()
	{
		const UTDFluidPresetTuningSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>() : nullptr;
		if (!Subsystem || Subsystem->GetTuningSourcePath().IsEmpty())
		{
			return UTDFluidPresetTuningSubsystem::GetTuningFilePath();
		}
		return Subsystem->GetTuningSourcePath();
	}

	void SeedSingleCellBend(FTDFluidFoliageBendLayer& Layer, int32 Resolution, const FIntPoint& Cell, const FVector2f& Velocity)
	{
		const FTDFluidFoliageBendParameters SeedParameters = FTDFluidFoliageBendLayer::MakeParameters(MakeBendConfig(0.25f, 3.0f), 1000.0f, MakeCellSize(), false);
		TArray<FFloat16Color> VelocityPixels = MakeUniformPixels(Resolution, PackVelocity(FVector2f::ZeroVector));
		VelocityPixels[Cell.Y * Resolution + Cell.X] = PackVelocity(Velocity);
		const TArray<FFloat16Color> DensityPixels = MakeUniformPixels(Resolution, PackDensity(0.0f));
		Layer.RequestReset();
		Layer.AdvanceCpu(VelocityPixels, DensityPixels, {}, Resolution, FVector2D::ZeroVector, MakeCellSize(), SeedParameters);
	}

	uint32 RunDeterministicBendSequence(TArray<FVector2f>& OutState)
	{
		const FTDFluidSimulationConfig Config = MakePainterConfig(0.1f, 0.8f, 0.7f, true);
		const FVector2D CellSizeCm = MakeCellSize();
		const int32 CellCount = ChecksumResolution * ChecksumResolution;
		TArray<FFloat16Color> VelocityPixels;
		TArray<FFloat16Color> DensityPixels;
		VelocityPixels.SetNumUninitialized(CellCount);
		DensityPixels.SetNumUninitialized(CellCount);
		FTDFluidFoliageBendLayer Layer;
		for (int32 StepIndex = 0; StepIndex < ChecksumStepCount; ++StepIndex)
		{
			const float DeltaSeconds = ChecksumStepSecondsCycle[StepIndex % UE_ARRAY_COUNT(ChecksumStepSecondsCycle)];
			const float DriftAngle = 0.05f * static_cast<float>(StepIndex);
			const FVector2f Drift(0.2f * FMath::Cos(DriftAngle), 0.2f * FMath::Sin(DriftAngle));
			const FVector2f BlobCenter(20.0f + 0.25f * static_cast<float>(StepIndex), 32.0f);
			const float DensityScale = StepIndex < 80 ? 1.0f : 0.0f;
			for (int32 CellY = 0; CellY < ChecksumResolution; ++CellY)
			{
				for (int32 CellX = 0; CellX < ChecksumResolution; ++CellX)
				{
					const FVector2f Offset(
						2.0f * ((static_cast<float>(CellX) + 0.5f) / ChecksumResolution - 0.5f),
						2.0f * ((static_cast<float>(CellY) + 0.5f) / ChecksumResolution - 0.5f));
					const FVector2f Swirl(-0.9f * Offset.Y, 0.9f * Offset.X);
					const float BlobDistanceSquared = FVector2f::DistSquared(FVector2f(static_cast<float>(CellX), static_cast<float>(CellY)), BlobCenter);
					const int32 CellIndex = CellY * ChecksumResolution + CellX;
					VelocityPixels[CellIndex] = PackVelocity(Swirl + Drift);
					DensityPixels[CellIndex] = PackDensity(DensityScale * FMath::Exp(-BlobDistanceSquared / 32.0f));
				}
			}
			const FVector2D CenterWorld(static_cast<double>(StepIndex / 15) * CellSizeCm.X, 0.0);
			const FTDFluidFoliageBendParameters Parameters = FTDFluidFoliageBendLayer::MakeParameters(Config, DeltaSeconds, CellSizeCm, false);
			Layer.AdvanceCpu(VelocityPixels, DensityPixels, {}, ChecksumResolution, CenterWorld, CellSizeCm, Parameters);
		}
		OutState = Layer.GetCpuBendState();
		return Layer.ComputeStateChecksum();
	}

	struct FTDFluidRippleLagMeasurement
	{
		double LagSeconds = 0.0;
		double Amplitude = 0.0;
	};

	FTDFluidRippleLagMeasurement MeasureRippleLag(float RiseSeconds, float RecoverySeconds, float RippleSlopeWeight)
	{
		FTDFluidSimulationConfig Config = MakeBendConfig(RiseSeconds, RecoverySeconds);
		Config.BendFromRippleSlope = RippleSlopeWeight;
		const FVector2D CellSizeCm = MakeCellSize();
		const FTDFluidFoliageBendParameters Parameters = FTDFluidFoliageBendLayer::MakeParameters(Config, static_cast<float>(LagDeltaSeconds), CellSizeCm, true);
		const TArray<FFloat16Color> VelocityPixels = MakeUniformPixels(LagResolution, PackVelocity(FVector2f::ZeroVector));
		const TArray<FFloat16Color> DensityPixels = MakeUniformPixels(LagResolution, PackDensity(0.0f));
		TArray<FFloat16Color> WaterPixels = MakeUniformPixels(LagResolution, PackWaterHeight(0.0));
		const double AngularFrequency = UE_DOUBLE_TWO_PI * LagFrequencyHz;
		const double WaveNumber = UE_DOUBLE_TWO_PI / LagWavelengthCm;
		const int32 ProbeColumn = LagResolution / 2;
		const int32 ProbeIndex = (LagResolution / 2) * LagResolution + ProbeColumn;
		const double ProbePositionCm = ProbeColumn * CellSizeCm.X;
		const int32 StepCount = FMath::RoundToInt32(LagDurationSeconds / LagDeltaSeconds);

		FTDFluidFoliageBendLayer Layer;
		double BendReal = 0.0;
		double BendImaginary = 0.0;
		double HeightReal = 0.0;
		double HeightImaginary = 0.0;
		FTDFluidRippleLagMeasurement Measurement;
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			const double TimeSeconds = StepIndex * LagDeltaSeconds;
			for (int32 CellX = 0; CellX < LagResolution; ++CellX)
			{
				const FFloat16Color Pixel = PackWaterHeight(LagAmplitude * FMath::Sin(WaveNumber * CellX * CellSizeCm.X - AngularFrequency * TimeSeconds));
				for (int32 CellY = 0; CellY < LagResolution; ++CellY)
				{
					WaterPixels[CellY * LagResolution + CellX] = Pixel;
				}
			}
			Layer.AdvanceCpu(VelocityPixels, DensityPixels, WaterPixels, LagResolution, FVector2D::ZeroVector, CellSizeCm, Parameters);

			const double SampleSeconds = (StepIndex + 1) * LagDeltaSeconds;
			if (SampleSeconds <= LagMeasureStartSeconds)
			{
				continue;
			}

			const double Bend = Layer.GetCpuBendState()[ProbeIndex].X;
			const double Height = LagAmplitude * FMath::Sin(WaveNumber * ProbePositionCm - AngularFrequency * SampleSeconds);
			BendReal += Bend * FMath::Cos(AngularFrequency * SampleSeconds);
			BendImaginary -= Bend * FMath::Sin(AngularFrequency * SampleSeconds);
			HeightReal += Height * FMath::Cos(AngularFrequency * SampleSeconds);
			HeightImaginary -= Height * FMath::Sin(AngularFrequency * SampleSeconds);
			Measurement.Amplitude = FMath::Max(Measurement.Amplitude, FMath::Abs(Bend));
		}

		const double PeriodSeconds = 1.0 / LagFrequencyHz;
		const double LagSeconds = (FMath::Atan2(HeightImaginary, HeightReal) - FMath::Atan2(BendImaginary, BendReal)) / AngularFrequency;
		Measurement.LagSeconds = LagSeconds - PeriodSeconds * FMath::RoundToDouble(LagSeconds / PeriodSeconds);
		return Measurement;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFoliageBendParametersTest,
	"TDGame.FluidFX.FoliageBend.Parameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFoliageBendParametersTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFoliageBendTestsPrivate;

	const FTDFluidFoliageBendParameters Bend = FTDFluidFoliageBendLayer::MakeParameters(MakeBendConfig(0.25f, 3.0f), SixtyFpsSeconds, MakeCellSize(), false);
	TestTrue(TEXT("Rise blend is 1 - exp(-dt / rise)"), FMath::IsNearlyEqual(Bend.RiseBlend, 0.064493f, 1.0e-6f));
	TestTrue(TEXT("Recovery blend is 1 - exp(-dt / recovery)"), FMath::IsNearlyEqual(Bend.RecoveryBlend, 0.0055402f, 1.0e-6f));
	TestTrue(TEXT("Ripple gradient scale X is 50 / cell"), FMath::IsNearlyEqual(Bend.RippleGradientScale.X, 12.8f, 1.0e-4f));
	TestTrue(TEXT("Ripple gradient scale Y is 50 / cell"), FMath::IsNearlyEqual(Bend.RippleGradientScale.Y, 12.8f, 1.0e-4f));
	TestTrue(TEXT("Fluid simulation reads velocity"), Bend.bUsesVelocity);
	TestFalse(TEXT("Water surface flag follows the caller"), Bend.bHasWaterSurface);
	TestFalse(TEXT("Clamp area does not wrap"), Bend.bWrapsArea);

	FTDFluidSimulationConfig FluidConfig = MakeBendConfig(0.25f, 3.0f);
	FluidConfig.BendFromPainterDensity = 0.7f;
	FluidConfig.AreaAddressMode = ETDFluidAreaAddressMode::Wrap;
	const FTDFluidFoliageBendParameters FluidBend = FTDFluidFoliageBendLayer::MakeParameters(FluidConfig, SixtyFpsSeconds, MakeCellSize(), true);
	TestEqual(TEXT("Fluid simulation ignores painter density weight"), FluidBend.PainterDensityWeight, 0.0f);
	TestTrue(TEXT("Wrap area wraps"), FluidBend.bWrapsArea);
	TestTrue(TEXT("Water surface flag is passed through"), FluidBend.bHasWaterSurface);

	const FTDFluidFoliageBendParameters PainterBend = FTDFluidFoliageBendLayer::MakeParameters(MakePainterConfig(0.25f, 3.0f, 0.7f, true), SixtyFpsSeconds, MakeCellSize(), false);
	TestEqual(TEXT("Painter keeps its density weight"), PainterBend.PainterDensityWeight, 0.7f);
	TestTrue(TEXT("Painter writing velocity reads velocity"), PainterBend.bUsesVelocity);

	const FTDFluidFoliageBendParameters SilentPainterBend = FTDFluidFoliageBendLayer::MakeParameters(MakePainterConfig(0.25f, 3.0f, 0.7f, false), SixtyFpsSeconds, MakeCellSize(), false);
	TestFalse(TEXT("Painter without velocity output does not read velocity"), SilentPainterBend.bUsesVelocity);

	const FTDFluidFoliageBendParameters FrozenBend = FTDFluidFoliageBendLayer::MakeParameters(MakeBendConfig(0.25f, 3.0f), 0.0f, MakeCellSize(), false);
	TestEqual(TEXT("Zero step does not rise"), FrozenBend.RiseBlend, 0.0f);
	TestEqual(TEXT("Zero step does not recover"), FrozenBend.RecoveryBlend, 0.0f);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFoliageBendStepResponseTest,
	"TDGame.FluidFX.FoliageBend.StepResponse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFoliageBendStepResponseTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFoliageBendTestsPrivate;

	const FTDFluidFoliageBendParameters Bend = FTDFluidFoliageBendLayer::MakeParameters(MakeBendConfig(0.25f, 3.0f), SixtyFpsSeconds, MakeCellSize(), false);
	FTDFluidFoliageBendLayer Layer;
	RunUniformSteps(Layer, FVector2f(0.5f, 0.0f), 0.0f, 15, Bend);
	const float RisenLength = GetProbeBend(Layer).Size();
	TestTrue(FString::Printf(TEXT("Rise after one rise time reaches 63%% of the target (%.5f)"), RisenLength), FMath::IsNearlyEqual(RisenLength, 0.31606f, 1.0e-4f));

	RunUniformSteps(Layer, FVector2f::ZeroVector, 0.0f, 180, Bend);
	const float RecoveredLength = GetProbeBend(Layer).Size();
	TestTrue(FString::Printf(TEXT("Recovery after one recovery time keeps exp(-1) (%.5f)"), RecoveredLength), FMath::IsNearlyEqual(RecoveredLength, RisenLength * FMath::Exp(-1.0f), 1.0e-4f));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFoliageBendFrameRateIndependenceTest,
	"TDGame.FluidFX.FoliageBend.FrameRateIndependence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFoliageBendFrameRateIndependenceTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFoliageBendTestsPrivate;

	const FVector2f Target(0.3f, 0.4f);
	FTDFluidFoliageBendLayer CoarseLayer;
	RunUniformSteps(CoarseLayer, Target, 0.0f, 15, FTDFluidFoliageBendLayer::MakeParameters(MakeBendConfig(0.25f, 3.0f), 0.5f / 15.0f, MakeCellSize(), false));
	FTDFluidFoliageBendLayer FineLayer;
	RunUniformSteps(FineLayer, Target, 0.0f, 60, FTDFluidFoliageBendLayer::MakeParameters(MakeBendConfig(0.25f, 3.0f), 0.5f / 60.0f, MakeCellSize(), false));

	const FVector2f Difference = GetProbeBend(CoarseLayer) - GetProbeBend(FineLayer);
	const float LargestDifference = FMath::Max(FMath::Abs(Difference.X), FMath::Abs(Difference.Y));
	TestTrue(FString::Printf(TEXT("Half a second at 1/30 and 1/120 agree (%.2e)"), LargestDifference), LargestDifference <= 1.0e-5f);
	TestTrue(TEXT("Bend moved towards the target"), GetProbeBend(FineLayer).Size() > 0.1f);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFoliageBendPressHoldsDirectionTest,
	"TDGame.FluidFX.FoliageBend.PressHoldsDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFoliageBendPressHoldsDirectionTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFoliageBendTestsPrivate;

	const FTDFluidFoliageBendParameters Bend = FTDFluidFoliageBendLayer::MakeParameters(MakePainterConfig(0.1f, 0.8f, 0.7f, true), SixtyFpsSeconds, MakeCellSize(), false);
	FTDFluidFoliageBendLayer Layer;
	RunUniformSteps(Layer, FVector2f(0.0f, 0.5f), 1.0f, 6, Bend);
	RunUniformSteps(Layer, FVector2f::ZeroVector, 1.0f, 300, Bend);
	const FVector2f HeldBend = GetProbeBend(Layer);
	const float Alignment = HeldBend.Y / FMath::Max(HeldBend.Size(), 1.0e-12f);
	TestTrue(FString::Printf(TEXT("Pressed cell keeps no sideways bend (%.5f)"), HeldBend.X), FMath::Abs(HeldBend.X) <= 1.0e-3f);
	TestTrue(FString::Printf(TEXT("Pressed cell holds the painter strength (%.5f)"), HeldBend.Y), FMath::IsNearlyEqual(HeldBend.Y, 0.7f, 1.0e-3f));
	TestTrue(FString::Printf(TEXT("Pressed cell keeps the walking direction (%.5f)"), Alignment), Alignment >= 0.999f);

	RunUniformSteps(Layer, FVector2f::ZeroVector, 0.0f, 300, Bend);
	const float ReleasedLength = GetProbeBend(Layer).Size();
	TestTrue(FString::Printf(TEXT("Released cell recovers (%.6f)"), ReleasedLength), ReleasedLength <= 0.0015f);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFoliageBendPressFallsBackToOutwardGradientTest,
	"TDGame.FluidFX.FoliageBend.PressFallsBackToOutwardGradient",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFoliageBendPressFallsBackToOutwardGradientTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFoliageBendTestsPrivate;

	const FTDFluidFoliageBendParameters Bend = FTDFluidFoliageBendLayer::MakeParameters(MakePainterConfig(0.1f, 0.8f, 0.7f, true), SixtyFpsSeconds, MakeCellSize(), false);
	const FVector2f Target = FTDFluidFoliageBendLayer::ComputeTarget(FVector2f::ZeroVector, 0.8f, FVector2f(0.2f, 0.0f), FVector2f::ZeroVector, FVector2f::ZeroVector, Bend);
	TestTrue(FString::Printf(TEXT("Press without memory bends away from the track centre (%.5f, %.5f)"), Target.X, Target.Y), FMath::IsNearlyEqual(Target.X, -0.56f, 1.0e-5f) && FMath::Abs(Target.Y) <= 1.0e-6f);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFoliageBendRippleSlopeOpposesGradientTest,
	"TDGame.FluidFX.FoliageBend.RippleSlopeOpposesGradient",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFoliageBendRippleSlopeOpposesGradientTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFoliageBendTestsPrivate;

	FTDFluidSimulationConfig WeakConfig = MakeBendConfig(0.1f, 0.8f);
	WeakConfig.BendFromRippleSlope = 0.15f;
	FTDFluidSimulationConfig StrongConfig = MakeBendConfig(0.1f, 0.8f);
	StrongConfig.BendFromRippleSlope = 1.0f;
	const FTDFluidFoliageBendParameters WeakBend = FTDFluidFoliageBendLayer::MakeParameters(WeakConfig, SixtyFpsSeconds, MakeCellSize(), true);
	const FTDFluidFoliageBendParameters StrongBend = FTDFluidFoliageBendLayer::MakeParameters(StrongConfig, SixtyFpsSeconds, MakeCellSize(), true);

	const FVector2f WeakTarget = FTDFluidFoliageBendLayer::ComputeTarget(FVector2f::ZeroVector, 0.0f, FVector2f::ZeroVector, FVector2f(2.0f, 0.0f), FVector2f::ZeroVector, WeakBend);
	TestTrue(FString::Printf(TEXT("Ripple slope pushes down the gradient (%.6f)"), WeakTarget.X), FMath::IsNearlyEqual(WeakTarget.X, -0.3f, 1.0e-6f));

	const FVector2f StrongTarget = FTDFluidFoliageBendLayer::ComputeTarget(FVector2f::ZeroVector, 0.0f, FVector2f::ZeroVector, FVector2f(3.0f, 4.0f), FVector2f::ZeroVector, StrongBend);
	TestTrue(FString::Printf(TEXT("Strong ripple target is limited to unit length (%.6f, %.6f)"), StrongTarget.X, StrongTarget.Y), FMath::IsNearlyEqual(StrongTarget.X, -0.6f, 1.0e-6f) && FMath::IsNearlyEqual(StrongTarget.Y, -0.8f, 1.0e-6f));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFoliageBendShiftCarriesStateTest,
	"TDGame.FluidFX.FoliageBend.ShiftCarriesState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFoliageBendShiftCarriesStateTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFoliageBendTestsPrivate;

	constexpr int32 Resolution = 8;
	const FVector2D CellSizeCm = MakeCellSize();
	FIntPoint ShiftCells = FIntPoint::ZeroValue;
	TestTrue(TEXT("Two cell move is carried"), FTDFluidFoliageBendLayer::ResolveShift(FVector2D::ZeroVector, FVector2D(2.0 * CellSizeCm.X, 0.0), CellSizeCm, Resolution, ShiftCells) && ShiftCells == FIntPoint(2, 0));
	TestTrue(TEXT("Mixed move is carried per axis"), FTDFluidFoliageBendLayer::ResolveShift(FVector2D::ZeroVector, FVector2D(-3.0 * CellSizeCm.X, CellSizeCm.Y), CellSizeCm, Resolution, ShiftCells) && ShiftCells == FIntPoint(-3, 1));
	TestFalse(TEXT("Window sized move resets"), FTDFluidFoliageBendLayer::ResolveShift(FVector2D::ZeroVector, FVector2D(8.0 * CellSizeCm.X, 0.0), CellSizeCm, Resolution, ShiftCells));
	TestFalse(TEXT("Half cell move resets"), FTDFluidFoliageBendLayer::ResolveShift(FVector2D::ZeroVector, FVector2D(0.5 * CellSizeCm.X, 0.0), CellSizeCm, Resolution, ShiftCells));

	const FTDFluidFoliageBendParameters Bend = FTDFluidFoliageBendLayer::MakeParameters(MakeBendConfig(0.25f, 3.0f), SixtyFpsSeconds, CellSizeCm, false);
	const TArray<FFloat16Color> NeutralVelocity = MakeUniformPixels(Resolution, PackVelocity(FVector2f::ZeroVector));
	const TArray<FFloat16Color> NeutralDensity = MakeUniformPixels(Resolution, PackDensity(0.0f));
	const FIntPoint SeedCell(5, 5);
	FTDFluidFoliageBendLayer Layer;
	SeedSingleCellBend(Layer, Resolution, SeedCell, FVector2f(0.4f, 0.0f));
	const float SeededBend = Layer.GetCpuBendState()[SeedCell.Y * Resolution + SeedCell.X].X;
	TestTrue(FString::Printf(TEXT("Seeded cell holds the packed target (%.5f)"), SeededBend), FMath::IsNearlyEqual(SeededBend, 0.4f, 1.0e-3f));

	Layer.AdvanceCpu(NeutralVelocity, NeutralDensity, {}, Resolution, FVector2D(2.0 * CellSizeCm.X, 0.0), CellSizeCm, Bend);
	const TArray<FVector2f>& MovedState = Layer.GetCpuBendState();
	const float CarriedBend = MovedState[SeedCell.Y * Resolution + SeedCell.X - 2].X;
	TestTrue(FString::Printf(TEXT("State moves two cells and recovers once (%.6f)"), CarriedBend), FMath::IsNearlyEqual(CarriedBend, SeededBend * (1.0f - Bend.RecoveryBlend), 1.0e-6f));
	float LargestOutside = 0.0f;
	for (int32 CellY = 0; CellY < Resolution; ++CellY)
	{
		for (int32 CellX = Resolution - 2; CellX < Resolution; ++CellX)
		{
			LargestOutside = FMath::Max(LargestOutside, MovedState[CellY * Resolution + CellX].Size());
		}
	}
	TestEqual(TEXT("Columns entering the window start at zero"), LargestOutside, 0.0f);

	SeedSingleCellBend(Layer, Resolution, SeedCell, FVector2f(0.4f, 0.0f));
	Layer.AdvanceCpu(NeutralVelocity, NeutralDensity, {}, Resolution, FVector2D(8.0 * CellSizeCm.X, 0.0), CellSizeCm, Bend);
	TestEqual(TEXT("Window sized move clears the state"), ComputeLargestBend(Layer.GetCpuBendState()), 0.0f);

	SeedSingleCellBend(Layer, Resolution, SeedCell, FVector2f(0.4f, 0.0f));
	Layer.AdvanceCpu(NeutralVelocity, NeutralDensity, {}, Resolution, FVector2D(0.5 * CellSizeCm.X, 0.0), CellSizeCm, Bend);
	TestEqual(TEXT("Half cell move clears the state"), ComputeLargestBend(Layer.GetCpuBendState()), 0.0f);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFoliageBendAuxMergeKeepsRedGreenTest,
	"TDGame.FluidFX.FoliageBend.AuxMergeKeepsRedGreen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFoliageBendAuxMergeKeepsRedGreenTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFoliageBendTestsPrivate;

	constexpr int32 Resolution = 8;
	const int32 CellCount = Resolution * Resolution;
	FRandomStream Random(1329);
	TArray<FFloat16Color> VelocityPixels;
	VelocityPixels.Reserve(CellCount);
	for (int32 CellIndex = 0; CellIndex < CellCount; ++CellIndex)
	{
		VelocityPixels.Add(PackVelocity(FVector2f(Random.FRandRange(-1.0f, 1.0f), Random.FRandRange(-1.0f, 1.0f))));
	}
	const FTDFluidFoliageBendParameters SeedBend = FTDFluidFoliageBendLayer::MakeParameters(MakeBendConfig(0.25f, 3.0f), 1000.0f, MakeCellSize(), false);
	FTDFluidFoliageBendLayer Layer;
	Layer.AdvanceCpu(VelocityPixels, MakeUniformPixels(Resolution, PackDensity(0.0f)), {}, Resolution, FVector2D::ZeroVector, MakeCellSize(), SeedBend);
	const TArray<FVector2f>& State = Layer.GetCpuBendState();
	TestTrue(TEXT("Seeded state is not empty"), ComputeLargestBend(State) > 0.1f);

	const TArray<FFloat16Color> SourceAux = MakeUniformPixels(Resolution, FFloat16Color(FLinearColor(0.25f, -0.5f, 0.9f, 0.9f)));
	TArray<FFloat16Color> MergedAux = SourceAux;
	Layer.WriteCpuBendToAux(MergedAux, Resolution);
	TArray<FFloat16Color> EmptyAux;
	Layer.WriteCpuBendToAux(EmptyAux, Resolution);

	bool bKeepsRedGreen = MergedAux.Num() == CellCount;
	bool bWritesBend = MergedAux.Num() == CellCount;
	bool bFillsEmpty = EmptyAux.Num() == CellCount;
	for (int32 CellIndex = 0; CellIndex < CellCount && bKeepsRedGreen && bFillsEmpty; ++CellIndex)
	{
		const FFloat16Color& Merged = MergedAux[CellIndex];
		const FFloat16Color& Empty = EmptyAux[CellIndex];
		bKeepsRedGreen &= Merged.R.Encoded == SourceAux[CellIndex].R.Encoded && Merged.G.Encoded == SourceAux[CellIndex].G.Encoded;
		bWritesBend &= Merged.B.Encoded == FFloat16(State[CellIndex].X).Encoded && Merged.A.Encoded == FFloat16(State[CellIndex].Y).Encoded;
		bFillsEmpty &= Empty.R.Encoded == 0 && Empty.G.Encoded == 0 && Empty.B.Encoded == Merged.B.Encoded && Empty.A.Encoded == Merged.A.Encoded;
	}
	TestTrue(TEXT("Red and green keep their bits"), bKeepsRedGreen);
	TestTrue(TEXT("Blue and alpha carry the half bend"), bWritesBend);
	TestTrue(TEXT("Empty aux is filled with zero red and green"), bFillsEmpty);

	Layer.RequestReset();
	TArray<FFloat16Color> ResetAux = SourceAux;
	Layer.WriteCpuBendToAux(ResetAux, Resolution);
	bool bClearsBend = true;
	for (int32 CellIndex = 0; CellIndex < CellCount; ++CellIndex)
	{
		bClearsBend &= ResetAux[CellIndex].R.Encoded == SourceAux[CellIndex].R.Encoded && ResetAux[CellIndex].B.Encoded == 0 && ResetAux[CellIndex].A.Encoded == 0;
	}
	TestTrue(TEXT("Pending reset publishes zero bend"), bClearsBend);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFoliageBendConsoleOverrideTest,
	"TDGame.FluidFX.FoliageBend.ConsoleOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFoliageBendConsoleOverrideTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Console 0 turns the layer off"), FTDFluidFoliageBendLayer::ResolveEnabled(0, true));
	TestTrue(TEXT("Console -1 follows the preset"), FTDFluidFoliageBendLayer::ResolveEnabled(-1, true));
	TestFalse(TEXT("Console 1 cannot enable a preset without the channel"), FTDFluidFoliageBendLayer::ResolveEnabled(1, false));
	TestTrue(TEXT("Console 1 keeps an enabled preset"), FTDFluidFoliageBendLayer::ResolveEnabled(1, true));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFoliageBendDeterministicChecksumTest,
	"TDGame.FluidFX.FoliageBend.DeterministicChecksum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFoliageBendDeterministicChecksumTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFoliageBendTestsPrivate;

	TArray<FVector2f> FirstState;
	TArray<FVector2f> SecondState;
	const uint32 FirstHash = RunDeterministicBendSequence(FirstState);
	const uint32 SecondHash = RunDeterministicBendSequence(SecondState);
	TestTrue(FString::Printf(TEXT("Two runs give the same checksum (0x%08X, 0x%08X)"), FirstHash, SecondHash), FirstHash == SecondHash);
	TestTrue(TEXT("Two runs give the same state bits"), FirstState.Num() == SecondState.Num() && FMemory::Memcmp(FirstState.GetData(), SecondState.GetData(), FirstState.Num() * sizeof(FVector2f)) == 0);
	const float LargestBend = ComputeLargestBend(FirstState);
	TestTrue(FString::Printf(TEXT("Sequence bends foliage within unit length (%.5f)"), LargestBend), LargestBend > 0.05f && LargestBend <= 1.0f + 1.0e-6f);
	AddInfo(FString::Printf(TEXT("TDFluidFoliageBendHash: 0x%08X"), FirstHash));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFoliageBendPresetKindsCarryBendTest,
	"TDGame.FluidFX.FoliageBend.PresetKindsCarryBend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFoliageBendPresetKindsCarryBendTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFoliageBendTestsPrivate;

	FString TuningText;
	if (!TestTrue(TEXT("Read the active preset tuning file"), FFileHelper::LoadFileToString(TuningText, *GetActiveTuningFilePath())))
	{
		return false;
	}
	FTDFluidPresetTuningTable Table;
	TArray<FString> Errors;
	if (!TestTrue(TEXT("Parse the active preset tuning file"), UTDFluidPresetTuningSubsystem::ParsePresetTuningText(TuningText, Table, Errors)))
	{
		return false;
	}

	const ETDFluidPresetKind BendKinds[] = { ETDFluidPresetKind::FoliageWalk, ETDFluidPresetKind::FoliagePainter, ETDFluidPresetKind::ReedShallows };
	for (const ETDFluidPresetKind Kind : BendKinds)
	{
		const FString KindName = StaticEnum<ETDFluidPresetKind>()->GetNameStringByValue(static_cast<int64>(Kind));
		const FTDFluidSimulationConfig Built = UTDFluidPresetAsset::BuildConfigForPresetKind(Kind);
		FTDFluidSimulationConfig FromTable;
		UTDFluidPresetTuningSubsystem::ApplyPresetTuningFromTable(Table, Kind, FTDFluidSimulationConfig::StaticStruct(), &FromTable);
		TestTrue(FString::Printf(TEXT("%s builder matches the table for the bend channel"), *KindName), Built.bEnableBendVectorChannel == FromTable.bEnableBendVectorChannel);
		TestTrue(FString::Printf(TEXT("%s builder matches the table for rise"), *KindName), Built.BendVectorRiseSeconds == FromTable.BendVectorRiseSeconds);
		TestTrue(FString::Printf(TEXT("%s builder matches the table for recovery"), *KindName), Built.BendVectorRecoverySeconds == FromTable.BendVectorRecoverySeconds);
		TestTrue(FString::Printf(TEXT("%s builder matches the table for painter density"), *KindName), Built.BendFromPainterDensity == FromTable.BendFromPainterDensity);
		TestTrue(FString::Printf(TEXT("%s builder matches the table for ripple slope"), *KindName), Built.BendFromRippleSlope == FromTable.BendFromRippleSlope);
		TestTrue(FString::Printf(TEXT("%s enables the bend channel"), *KindName), Built.bEnableBendVectorChannel);
		TestTrue(FString::Printf(TEXT("%s has positive time constants"), *KindName), Built.BendVectorRiseSeconds > 0.0f && Built.BendVectorRecoverySeconds > 0.0f);
	}

	const FTDFluidSimulationConfig Walk = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::FoliageWalk);
	TestTrue(TEXT("FoliageWalk rises faster than it recovers"), Walk.BendVectorRiseSeconds < Walk.BendVectorRecoverySeconds);
	TestTrue(TEXT("FoliageWalk is a fluid simulation"), Walk.SimMode == ETDFluidSimMode::FluidSim);
	TestEqual(TEXT("FoliageWalk has no painter press"), FTDFluidFoliageBendLayer::MakeParameters(Walk, SixtyFpsSeconds, MakeCellSize(), false).PainterDensityWeight, 0.0f);

	const FTDFluidSimulationConfig Painter = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::FoliagePainter);
	const FTDFluidFoliageBendParameters PainterBend = FTDFluidFoliageBendLayer::MakeParameters(Painter, SixtyFpsSeconds, MakeCellSize(), false);
	TestTrue(TEXT("FoliagePainter is a simple painter"), Painter.SimMode == ETDFluidSimMode::SimplePainter);
	TestTrue(TEXT("FoliagePainter writes painter velocity"), Painter.bWritesPainterVelocity && PainterBend.bUsesVelocity);
	TestTrue(TEXT("FoliagePainter presses with density"), Painter.BendFromPainterDensity > 0.0f && PainterBend.PainterDensityWeight > 0.0f);
	TestTrue(TEXT("FoliagePainter rises faster than it recovers"), Painter.BendVectorRiseSeconds < Painter.BendVectorRecoverySeconds);

	const FTDFluidSimulationConfig Reed = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::ReedShallows);
	TestTrue(TEXT("ReedShallows joins the water layer"), Reed.bEnableWaterLayer);
	TestTrue(TEXT("ReedShallows follows the ripple slope"), Reed.BendFromRippleSlope > 0.0f);
	TestTrue(TEXT("ReedShallows recovers as fast as it rises"), Reed.BendVectorRiseSeconds == Reed.BendVectorRecoverySeconds);

	const ETDFluidPresetKind PlainKinds[] = { ETDFluidPresetKind::Water, ETDFluidPresetKind::Smoke, ETDFluidPresetKind::River, ETDFluidPresetKind::Sea, ETDFluidPresetKind::FootprintPainter, ETDFluidPresetKind::SnowTrack };
	for (const ETDFluidPresetKind Kind : PlainKinds)
	{
		const FString KindName = StaticEnum<ETDFluidPresetKind>()->GetNameStringByValue(static_cast<int64>(Kind));
		TestFalse(FString::Printf(TEXT("%s keeps the bend channel off"), *KindName), UTDFluidPresetAsset::BuildConfigForPresetKind(Kind).bEnableBendVectorChannel);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFoliageBendPressHalfLifeRecordTest,
	"TDGame.FluidFX.FoliageBend.PressHalfLifeRecord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFoliageBendPressHalfLifeRecordTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFoliageBendTestsPrivate;

	const FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::FoliagePainter);
	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Config, FVector2D::ZeroVector);
	if (!TestTrue(TEXT("Painter solver initializes"), Solver.IsInitialized()))
	{
		return false;
	}

	const int32 Resolution = Solver.GetResolution();
	const FVector2D CellSizeCm = Solver.GetExtentCm() / static_cast<double>(Resolution);
	const FTDFluidFoliageBendParameters Bend = FTDFluidFoliageBendLayer::MakeParameters(Config, SixtyFpsSeconds, CellSizeCm, false);
	FTDFluidBrushStamp Stamp;
	Stamp.WorldLocation = FVector::ZeroVector;
	Stamp.WorldVelocity = FVector(150.0, 0.0, 0.0);
	Stamp.RadiusCm = 60.0f;
	Stamp.DensityStrength = 1.0f;
	Stamp.VelocityStrength = 1.0f;

	FTDFluidFoliageBendLayer Layer;
	TArray<FFloat16Color> DensityPixels;
	TArray<FFloat16Color> VelocityPixels;
	const int32 StepCount = FMath::RoundToInt32(PressRecordDurationSeconds / SixtyFpsSeconds);
	float PeakBend = 0.0f;
	double PeakSeconds = 0.0;
	double HalfLifeSeconds = -1.0;
	for (int32 StepIndex = 0; StepIndex < StepCount && HalfLifeSeconds < 0.0; ++StepIndex)
	{
		if (StepIndex == 0)
		{
			Solver.ApplyBrushStamps({ Stamp }, SixtyFpsSeconds);
		}
		Solver.StepSimulation(SixtyFpsSeconds);
		Solver.PackOutputPixels(DensityPixels, VelocityPixels);
		Layer.AdvanceCpu(VelocityPixels, DensityPixels, {}, Resolution, FVector2D::ZeroVector, CellSizeCm, Bend);
		const double ElapsedSeconds = (StepIndex + 1) * static_cast<double>(SixtyFpsSeconds);
		const float LargestBend = ComputeLargestBend(Layer.GetCpuBendState());
		if (LargestBend > PeakBend)
		{
			PeakBend = LargestBend;
			PeakSeconds = ElapsedSeconds;
			continue;
		}
		if (PeakBend > 0.0f && LargestBend <= 0.5f * PeakBend)
		{
			HalfLifeSeconds = ElapsedSeconds - PeakSeconds;
		}
	}
	TestTrue(FString::Printf(TEXT("Painter stamp bends foliage (%.4f)"), PeakBend), PeakBend > 0.01f);

	const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetStringField(TEXT("kind"), TEXT("FoliagePainter"));
	Report->SetNumberField(TEXT("delta_seconds"), SixtyFpsSeconds);
	Report->SetNumberField(TEXT("peak_bend"), PeakBend);
	Report->SetNumberField(TEXT("peak_seconds"), PeakSeconds);
	Report->SetNumberField(TEXT("half_life_seconds"), HalfLifeSeconds);
	Report->SetNumberField(TEXT("density_dissipation"), Config.DensityDissipation);
	Report->SetNumberField(TEXT("bend_from_painter_density"), Config.BendFromPainterDensity);
	Report->SetNumberField(TEXT("rise_seconds"), Config.BendVectorRiseSeconds);
	Report->SetNumberField(TEXT("recovery_seconds"), Config.BendVectorRecoverySeconds);
	Report->SetStringField(TEXT("note"), TEXT("record only, largest grid bend after one stamp, half life = first time at or below half the peak, -1 = not reached in 20 s"));
	FString ReportPath;
	TestTrue(TEXT("Wrote the press half life record"), SaveFoliageBendRecord(Report, TEXT("press_half_life.json"), ReportPath));
	AddInfo(FString::Printf(TEXT("TDFluidFoliageBendPressHalfLife: peak %.4f at %.3f s, half life %.3f s -> %s"), PeakBend, PeakSeconds, HalfLifeSeconds, *ReportPath));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFoliageBendRippleLagRecordTest,
	"TDGame.FluidFX.FoliageBend.RippleLagRecord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFoliageBendRippleLagRecordTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFoliageBendTestsPrivate;

	const double AngularFrequency = UE_DOUBLE_TWO_PI * LagFrequencyHz;
	const double AnalyticLagSeconds = -0.25 / LagFrequencyHz + FMath::Atan(AngularFrequency * LagRiseSeconds) / AngularFrequency;
	const FTDFluidRippleLagMeasurement EqualBlend = MeasureRippleLag(LagRiseSeconds, LagRiseSeconds, LagRippleWeight);
	TestTrue(FString::Printf(TEXT("Ripple slope bend leads the surface, lead is negative (%.4f s)"), EqualBlend.LagSeconds), EqualBlend.LagSeconds < 0.0);
	TestTrue(FString::Printf(TEXT("Equal blend lag matches the first order filter (%.4f s against %.4f s)"), EqualBlend.LagSeconds, AnalyticLagSeconds), FMath::Abs(EqualBlend.LagSeconds - AnalyticLagSeconds) <= LagAnalyticToleranceSeconds);
	TestTrue(FString::Printf(TEXT("Ripple bend is visible (%.4f)"), EqualBlend.Amplitude), EqualBlend.Amplitude > 0.05);

	const FTDFluidRippleLagMeasurement Asymmetric = MeasureRippleLag(LagRiseSeconds, LagAsymmetricRecoverySeconds, LagRippleWeight);
	const FTDFluidSimulationConfig Reed = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::ReedShallows);
	const FTDFluidRippleLagMeasurement ReedKind = MeasureRippleLag(Reed.BendVectorRiseSeconds, Reed.BendVectorRecoverySeconds, Reed.BendFromRippleSlope);
	TestTrue(TEXT("ReedShallows lag is finite"), FMath::IsFinite(ReedKind.LagSeconds));

	const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetNumberField(TEXT("frequency_hz"), LagFrequencyHz);
	Report->SetNumberField(TEXT("wavelength_cm"), LagWavelengthCm);
	Report->SetNumberField(TEXT("delta_seconds"), LagDeltaSeconds);
	Report->SetNumberField(TEXT("equal_blend_lag_seconds"), EqualBlend.LagSeconds);
	Report->SetNumberField(TEXT("equal_blend_analytic_seconds"), AnalyticLagSeconds);
	Report->SetNumberField(TEXT("asymmetric_recovery_lag_seconds"), Asymmetric.LagSeconds);
	Report->SetNumberField(TEXT("reed_shallows_lag_seconds"), ReedKind.LagSeconds);
	Report->SetNumberField(TEXT("reed_shallows_amplitude"), ReedKind.Amplitude);
	Report->SetNumberField(TEXT("reed_shallows_rise_seconds"), Reed.BendVectorRiseSeconds);
	Report->SetNumberField(TEXT("reed_shallows_recovery_seconds"), Reed.BendVectorRecoverySeconds);
	Report->SetNumberField(TEXT("reed_shallows_ripple_slope"), Reed.BendFromRippleSlope);
	Report->SetStringField(TEXT("note"), TEXT("record only, lag = bend phase delay behind the surface height at the probe, lead is negative"));
	FString ReportPath;
	TestTrue(TEXT("Wrote the ripple lag record"), SaveFoliageBendRecord(Report, TEXT("ripple_lag.json"), ReportPath));
	AddInfo(FString::Printf(TEXT("TDFluidFoliageBendRippleLag: equal %.4f s, analytic %.4f s, asymmetric %.4f s, ReedShallows %.4f s -> %s"), EqualBlend.LagSeconds, AnalyticLagSeconds, Asymmetric.LagSeconds, ReedKind.LagSeconds, *ReportPath));
	return !HasAnyErrors();
}

namespace TDFluidFoliageBendLiveTestsPrivate
{
	constexpr int32 LiveStampFrameCount = 10;
	constexpr float LiveStampRadiusCm = 60.0f;
	constexpr float LiveStampSpeedCmPerSec = 300.0f;
	constexpr float LiveBendThreshold = 0.1f;
	constexpr float LiveDirectionThreshold = 0.9f;

	struct FTDFluidFoliageBendLiveTestWorld
	{
		FTDFluidFoliageBendLiveTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidFoliageBendLiveWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		~FTDFluidFoliageBendLiveTestWorld()
		{
			if (!World)
			{
				return;
			}

			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		UWorld* World = nullptr;
	};

	ATDFluidLiveActor* SpawnFoliageLiveActor(UWorld& TargetWorld, const FTDFluidSimulationConfig& Config)
	{
		const FTransform SpawnTransform(FRotator::ZeroRotator, FVector::ZeroVector);
		ATDFluidLiveActor* LiveActor = TargetWorld.SpawnActorDeferred<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), SpawnTransform);
		if (!LiveActor || !LiveActor->LiveComponent)
		{
			return nullptr;
		}

		LiveActor->LiveComponent->SimulationConfig = Config;
		LiveActor->FinishSpawning(SpawnTransform);
		return LiveActor;
	}

	FTDFluidBrushStamp MakeWalkStamp()
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = FVector::ZeroVector;
		Stamp.WorldVelocity = FVector(LiveStampSpeedCmPerSec, 0.0, 0.0);
		Stamp.RadiusCm = LiveStampRadiusCm;
		Stamp.DensityStrength = 1.0f;
		Stamp.VelocityStrength = 1.0f;
		return Stamp;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFoliageBendLivePackedAuxCarriesBendTest,
	"TDGame.FluidFX.FoliageBend.LivePackedAuxCarriesBend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFoliageBendLivePackedAuxCarriesBendTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFoliageBendTestsPrivate;
	using namespace TDFluidFoliageBendLiveTestsPrivate;

	FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::FoliageWalk);
	Config.SolverBackend = ETDFluidSolverBackend::Cpu;
	FTDFluidFoliageBendLiveTestWorld TestWorld;
	ATDFluidLiveActor* LiveActor = TestWorld.World ? SpawnFoliageLiveActor(*TestWorld.World, Config) : nullptr;
	if (!TestNotNull(TEXT("FoliageWalk live actor spawns"), LiveActor))
	{
		return false;
	}

	UTDFluidLiveComponent& Component = *LiveActor->LiveComponent;
	TestTrue(TEXT("FoliageWalk turns the bend aux on"), Component.IsFoliageBendAuxActive() && Component.IsAuxOutputActive());
	Component.SetSimulationAwake(true);
	for (int32 FrameIndex = 0; FrameIndex < LiveStampFrameCount; ++FrameIndex)
	{
		Component.QueueBrushStamp(MakeWalkStamp());
		Component.AdvanceSimulationStep(SixtyFpsSeconds, FVector2D::ZeroVector, false);
	}

	if (!TestFalse(TEXT("FoliageWalk runs on the forced CPU solver"), Component.IsUsingGpuSolver()))
	{
		return false;
	}

	const int32 Resolution = Component.GetSimulationResolution();
	const TArray<FFloat16Color>& AuxPixels = Component.GetPackedAuxPixels();
	if (!TestEqual(TEXT("packed aux covers the grid"), AuxPixels.Num(), Resolution * Resolution))
	{
		return false;
	}

	const FVector2D ExtentCm = Component.GetSimulationExtentCm();
	const FVector2D CellSizeCm = ExtentCm / static_cast<double>(Resolution);
	const FVector2D WindowMinimumCm = Component.GetSimulationCenterWorld() - 0.5 * ExtentCm;
	float LargestBend = 0.0f;
	FVector2f BendInsideStamp = FVector2f::ZeroVector;
	int32 NonZeroRedGreenCount = 0;
	for (int32 CellY = 0; CellY < Resolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < Resolution; ++CellX)
		{
			const FFloat16Color& Pixel = AuxPixels[CellY * Resolution + CellX];
			const FVector2f Bend(Pixel.B.GetFloat(), Pixel.A.GetFloat());
			LargestBend = FMath::Max(LargestBend, Bend.Size());
			NonZeroRedGreenCount += Pixel.R.GetFloat() != 0.0f || Pixel.G.GetFloat() != 0.0f ? 1 : 0;
			const FVector2D CellCenterCm = WindowMinimumCm + FVector2D((CellX + 0.5) * CellSizeCm.X, (CellY + 0.5) * CellSizeCm.Y);
			if (CellCenterCm.Size() <= LiveStampRadiusCm)
			{
				BendInsideStamp += Bend;
			}
		}
	}

	const float DirectionAlongStamp = FVector2f::DotProduct(BendInsideStamp.GetSafeNormal(), FVector2f(1.0f, 0.0f));
	AddInfo(FString::Printf(TEXT("LivePackedAuxCarriesBend: largest bend %.4f, direction inside stamp %.4f, red-green not zero %d"), LargestBend, DirectionAlongStamp, NonZeroRedGreenCount));
	TestTrue(TEXT("CPU live aux carries the bend in blue and alpha"), LargestBend > LiveBendThreshold);
	TestTrue(TEXT("bend inside the stamp follows the stamp velocity"), DirectionAlongStamp > LiveDirectionThreshold);
	TestEqual(TEXT("bend-only aux keeps red and green at zero"), NonZeroRedGreenCount, 0);
	return !HasAnyErrors();
}

#endif
