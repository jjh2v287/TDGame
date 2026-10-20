#if WITH_DEV_AUTOMATION_TESTS

#include "FluidFX/TDFluidVolumeTransmittance.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "TDFluidGpuBatch.h"

namespace TDFluidTransmittanceTestPrivate
{
	constexpr int32 TransmittanceTestResolution = 96;
	constexpr double TransmittanceTestExtentCm = 400.0;
	constexpr float TransmittanceTestHeightCm = 200.0f;

	FTDFluidTransmittanceInputs MakeInputs(int32 LightMode)
	{
		FTDFluidTransmittanceInputs Inputs;
		Inputs.SimResolution = TransmittanceTestResolution;
		Inputs.ExtentCm = FVector2D(TransmittanceTestExtentCm, TransmittanceTestExtentCm);
		Inputs.VolumeHeightCm = TransmittanceTestHeightCm;
		Inputs.DensityScale = 0.035f;
		Inputs.SoftFalloffCm = 28.0f;
		Inputs.MarchStepCount = 12;
		Inputs.LightMode = LightMode;
		Inputs.LightDirectionLocal = FVector(0.0, 0.0, -1.0);
		return Inputs;
	}

	TArray<FFloat16Color> MakeUniformPixels(float Density)
	{
		TArray<FFloat16Color> Pixels;
		Pixels.Init(FFloat16Color(FLinearColor(Density, 0.5f, 0.0f, 1.0f)), TransmittanceTestResolution * TransmittanceTestResolution);
		return Pixels;
	}

	TArray<FFloat16Color> MakeBlobPixels(const FVector2D& CenterUv, float RadiusUv, float Density)
	{
		TArray<FFloat16Color> Pixels;
		Pixels.SetNumUninitialized(TransmittanceTestResolution * TransmittanceTestResolution);
		for (int32 CellY = 0; CellY < TransmittanceTestResolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < TransmittanceTestResolution; ++CellX)
			{
				const FVector2D Uv((CellX + 0.5) / TransmittanceTestResolution, (CellY + 0.5) / TransmittanceTestResolution);
				const float Value = (Uv - CenterUv).Size() <= RadiusUv ? Density : 0.0f;
				Pixels[CellY * TransmittanceTestResolution + CellX] = FFloat16Color(FLinearColor(Value, 0.5f, 0.0f, 1.0f));
			}
		}
		return Pixels;
	}

	double IntegrateSlabReference(const FTDFluidTransmittanceInputs& Inputs, float Density, double StartHeightCm)
	{
		constexpr int32 ReferenceStepCount = 4000;
		const double HeightCm = Inputs.VolumeHeightCm;
		const double PathLength = HeightCm - StartHeightCm;
		const double StepLength = PathLength / ReferenceStepCount;
		const double Coverage = FMath::Clamp(Density * TDFluidVolumeTransmittance::CoverageScale, 0.0f, 1.0f);
		double OpticalDepth = 0.0;
		for (int32 StepIndex = 0; StepIndex < ReferenceStepCount; ++StepIndex)
		{
			const double Height = StartHeightCm + (StepIndex + 0.5) * StepLength;
			const double SurfaceDepth = Density * TDFluidVolumeTransmittance::HeightScaleRatio * HeightCm - Height;
			const double TopFade = FMath::Clamp((HeightCm - Height) / (TDFluidVolumeTransmittance::TopFadeRatio * HeightCm), 0.0, 1.0);
			OpticalDepth += Inputs.DensityScale * Coverage * TopFade * FMath::Exp(FMath::Min(SurfaceDepth, 0.0) / Inputs.SoftFalloffCm) * StepLength;
		}
		return OpticalDepth;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidTransmittanceEmptyTest,
	"TDGame.FluidFX.Transmittance.EmptyIsZeroDepth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidTransmittanceEmptyTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidTransmittanceTestPrivate;
	TArray<FFloat16Color> OutPixels;
	TDFluidVolumeTransmittance::ComputePixels(MakeInputs(TDFluidVolumeTransmittance::DirectionalLightMode), MakeUniformPixels(0.0f), OutPixels);
	int32 NonZeroCount = 0;
	for (const FFloat16Color& Pixel : OutPixels)
	{
		NonZeroCount += (Pixel.R.GetFloat() != 0.0f || Pixel.G.GetFloat() != 0.0f || Pixel.B.GetFloat() != 0.0f || Pixel.A.GetFloat() != 0.0f) ? 1 : 0;
	}
	TestEqual(TEXT("Output resolution is a quarter of the simulation"), OutPixels.Num(), 24 * 24);
	TestEqual(TEXT("Empty density gives zero optical depth on every level"), NonZeroCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidTransmittanceSlabTest,
	"TDGame.FluidFX.Transmittance.SlabMatchesBeerLambert",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidTransmittanceSlabTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidTransmittanceTestPrivate;
	constexpr float SlabDensity = 0.6f;
	const FTDFluidTransmittanceInputs Inputs = MakeInputs(TDFluidVolumeTransmittance::DirectionalLightMode);
	const TArray<FFloat16Color> Pixels = MakeUniformPixels(SlabDensity);
	for (int32 LevelIndex = 0; LevelIndex < TDFluidVolumeTransmittance::LevelCount; ++LevelIndex)
	{
		const float LevelHeightCm = TDFluidVolumeTransmittance::ComputeLevelHeightCm(Inputs.VolumeHeightCm, LevelIndex);
		const double Marched = TDFluidVolumeTransmittance::ComputeOpticalDepth(Inputs, Pixels, FVector(0.5 * TransmittanceTestExtentCm, 0.5 * TransmittanceTestExtentCm, LevelHeightCm));
		const double Reference = IntegrateSlabReference(Inputs, SlabDensity, LevelHeightCm);
		const double MarchedTransmittance = FMath::Exp(-Marched);
		const double ReferenceTransmittance = FMath::Exp(-Reference);
		AddInfo(FString::Printf(TEXT("TDFluidTransmittance: level=%d marched_depth=%.4f reference_depth=%.4f"), LevelIndex, Marched, Reference));
		TestTrue(*FString::Printf(TEXT("Level %d transmittance matches the Beer-Lambert reference within 3%%"), LevelIndex), FMath::Abs(MarchedTransmittance - ReferenceTransmittance) <= 0.03 * FMath::Max(ReferenceTransmittance, 0.05));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidTransmittancePointLightTest,
	"TDGame.FluidFX.Transmittance.PointLightDarkerFarSide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidTransmittancePointLightTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidTransmittanceTestPrivate;
	FTDFluidTransmittanceInputs Inputs = MakeInputs(TDFluidVolumeTransmittance::PointLightMode);
	Inputs.LightPositionLocalCm = FVector(0.1 * TransmittanceTestExtentCm, 0.5 * TransmittanceTestExtentCm, 60.0);
	const TArray<FFloat16Color> Pixels = MakeBlobPixels(FVector2D(0.5, 0.5), 0.2f, 0.9f);
	const float LevelHeightCm = TDFluidVolumeTransmittance::ComputeLevelHeightCm(Inputs.VolumeHeightCm, 0);
	const double NearSide = TDFluidVolumeTransmittance::ComputeOpticalDepth(Inputs, Pixels, FVector(0.32 * TransmittanceTestExtentCm, 0.5 * TransmittanceTestExtentCm, LevelHeightCm));
	const double FarSide = TDFluidVolumeTransmittance::ComputeOpticalDepth(Inputs, Pixels, FVector(0.68 * TransmittanceTestExtentCm, 0.5 * TransmittanceTestExtentCm, LevelHeightCm));
	AddInfo(FString::Printf(TEXT("TDFluidTransmittance: near_depth=%.4f far_depth=%.4f"), NearSide, FarSide));
	TestTrue(TEXT("The far side of the smoke receives less light"), FarSide > NearSide);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidTransmittanceSymmetricTest,
	"TDGame.FluidFX.Transmittance.SymmetricSidesDiffer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidTransmittanceSymmetricTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidTransmittanceTestPrivate;
	FTDFluidTransmittanceInputs Inputs = MakeInputs(TDFluidVolumeTransmittance::DirectionalLightMode);
	Inputs.bIsSymmetric = true;
	Inputs.LightDirectionLocal = FVector(0.0, 0.3, -0.95).GetSafeNormal();
	const TArray<FFloat16Color> Pixels = MakeBlobPixels(FVector2D(0.5, 0.5), 0.3f, 1.0f);
	TArray<FFloat16Color> OutPixels;
	TDFluidVolumeTransmittance::ComputePixels(Inputs, Pixels, OutPixels);
	const int32 OutputResolution = TDFluidVolumeTransmittance::ComputeOutputResolution(Inputs.SimResolution);
	const FFloat16Color& Center = OutPixels[(OutputResolution / 2) * OutputResolution + OutputResolution / 2];
	AddInfo(FString::Printf(TEXT("TDFluidTransmittance: symmetric_level_depths=%.4f %.4f %.4f %.4f"), Center.R.GetFloat(), Center.G.GetFloat(), Center.B.GetFloat(), Center.A.GetFloat()));
	TestTrue(TEXT("The level far from the light is darker than the level near the light"), Center.R.GetFloat() > Center.A.GetFloat());
	TestTrue(TEXT("Mirrored levels are not folded together"), Center.R.GetFloat() != Center.A.GetFloat() && Center.G.GetFloat() != Center.B.GetFloat());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidTransmittanceResolutionTest,
	"TDGame.FluidFX.Transmittance.ResolutionQuarter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidTransmittanceResolutionTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("96 -> 24"), TDFluidVolumeTransmittance::ComputeOutputResolution(96), 24);
	TestEqual(TEXT("128 -> 32"), TDFluidVolumeTransmittance::ComputeOutputResolution(128), 32);
	TestEqual(TEXT("112 -> 28"), TDFluidVolumeTransmittance::ComputeOutputResolution(112), 28);
	TestEqual(TEXT("512 -> 128"), TDFluidVolumeTransmittance::ComputeOutputResolution(512), 128);
	TestEqual(TEXT("32 -> 16"), TDFluidVolumeTransmittance::ComputeOutputResolution(32), 16);
	TestEqual(TEXT("GPU tier 256 -> 64"), FTDFluidGpuBatch::ComputeTransmittanceResolution(256), 64);
	TestEqual(TEXT("GPU and CPU agree at 512"), FTDFluidGpuBatch::ComputeTransmittanceResolution(512), TDFluidVolumeTransmittance::ComputeOutputResolution(512));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidTransmittanceCpuCostTest,
	"TDGame.FluidFX.Perf.TransmittanceCpuCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidTransmittanceCpuCostTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidTransmittanceTestPrivate;
	constexpr int32 SampleCount = 9;
	const FTDFluidTransmittanceInputs Inputs = MakeInputs(TDFluidVolumeTransmittance::DirectionalLightMode);
	const TArray<FFloat16Color> Pixels = MakeBlobPixels(FVector2D(0.5, 0.4), 0.3f, 0.8f);
	TArray<FFloat16Color> OutPixels;
	TArray<double> DurationsMs;
	for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
	{
		const double StartSeconds = FPlatformTime::Seconds();
		TDFluidVolumeTransmittance::ComputePixels(Inputs, Pixels, OutPixels);
		DurationsMs.Add((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
	}
	DurationsMs.Sort();
	const double MedianMs = DurationsMs[SampleCount / 2];
	AddInfo(FString::Printf(TEXT("TDFluidTransmittancePerf: resolution=96 output=24 steps=12 median_ms=%.4f"), MedianMs));
	TestTrue(TEXT("CPU transmittance for a 96 grid stays under 0.3 ms"), MedianMs <= 0.3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidHeatMemoryEstimateTest,
	"TDGame.FluidFX.Memory.HeatEstimate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidHeatMemoryEstimateTest::RunTest(const FString& Parameters)
{
	constexpr int64 CellCount = 256 * 256;
	constexpr int64 TransmittanceTexelCount = 64 * 64;
	const FTDFluidGpuMemoryEstimate Base = FTDFluidGpuBatch::EstimateActorMemory(ETDFluidGpuSimMode::FluidSim, 256);
	const FTDFluidGpuMemoryEstimate Heat = FTDFluidGpuBatch::EstimateActorMemory(ETDFluidGpuSimMode::FluidSim, 256, true, false);
	const FTDFluidGpuMemoryEstimate HeatAndTransmittance = FTDFluidGpuBatch::EstimateActorMemory(ETDFluidGpuSimMode::FluidSim, 256, true, true);
	TestEqual(TEXT("Heat adds two R32F state slices per cell"), Heat.StateBytes - Base.StateBytes, CellCount * 8);
	TestEqual(TEXT("Heat adds one RGBA16F aux output per cell"), Heat.OutputBytes - Base.OutputBytes, CellCount * 8);
	TestEqual(TEXT("Transmittance adds a quarter resolution RGBA16F output"), HeatAndTransmittance.OutputBytes - Heat.OutputBytes, TransmittanceTexelCount * 8);
	TestEqual(TEXT("Defaults keep the previous estimate"), FTDFluidGpuBatch::EstimateActorMemory(ETDFluidGpuSimMode::FluidSim, 256, false, false).GetTotalBytes(), Base.GetTotalBytes());
	return true;
}

#endif
