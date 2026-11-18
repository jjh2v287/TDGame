#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "RHIGPUReadback.h"
#include "RHIGlobals.h"
#include "RenderGraphBuilder.h"
#include "RenderingThread.h"
#include "TDFluidGpuFog3DDensityProbe.h"

namespace TDFluidGpuFog3DDensityRhiTestPrivate
{
	constexpr uint32 DensityFixtureVersion = 1;
	constexpr int32 DensityFixtureHeaderBytes = 24;
	constexpr int32 DensityFixtureFloatsPerSample = 16;
	constexpr int32 DensityFixtureFloatsPerParameterSet = 28;
	constexpr int32 DensityFixtureCategoryCount = 8;
	constexpr int32 DensityFixtureMinimumSampleCount = 4096;
	constexpr int32 DensityProbeMaximumReadbackExtraSubmitCount = 16;
	constexpr int32 FineNoiseVectorIndex = 5;
	constexpr int32 StainNoiseVectorIndex = 6;
	constexpr double DensityMaximumTolerance = 1.0e-4;
	constexpr double DensityMeanTolerance = 1.0e-5;
	constexpr double UvwAndWeightMaximumTolerance = 1.0e-6;
	constexpr double MutatedSeedMinimumDensityDifference = 1.0e-2;
	constexpr float MutatedSeedOffset = 1.0f;
	constexpr ANSICHAR DensityFixtureMagic[] = "TDF3DDN1";

	const TCHAR* const DensityFixtureCategoryNames[DensityFixtureCategoryCount] = {
		TEXT("replay_interior"), TEXT("curve_blur"), TEXT("fill_range"), TEXT("displacement"),
		TEXT("height_weight"), TEXT("band_edge"), TEXT("zone_edge"), TEXT("below_floor") };

	struct FTDFog3DDensityFixtureSample
	{
		FVector4f PositionAndSet = FVector4f::Zero();
		FVector4f State = FVector4f::Zero();
		FVector3f ExpectedUvw = FVector3f::ZeroVector;
		float ExpectedWeight = 0.0f;
		float ExpectedDensity = 0.0f;
		int32 Category = 0;
	};

	struct FTDFog3DDensityFixture
	{
		TArray<FVector4f> ParameterSets;
		TArray<FTDFog3DDensityFixtureSample> Samples;
	};

	struct FTDFog3DDensityProbeRun
	{
		TArray<FVector4f> Outputs;
		bool bIsCopied = false;
	};

	struct FTDFog3DDensityErrors
	{
		double DensityMaximum = 0.0;
		double DensitySum = 0.0;
		double UvwMaximum = 0.0;
		double WeightMaximum = 0.0;
		int32 WorstSampleIndex = INDEX_NONE;
		double CategoryDensityMaximum[DensityFixtureCategoryCount] = {};
		int32 CategorySampleCount[DensityFixtureCategoryCount] = {};
		int32 CategoryNonZeroCount[DensityFixtureCategoryCount] = {};

		double GetDensityMean(int32 SampleCount) const
		{
			return SampleCount > 0 ? DensitySum / SampleCount : 0.0;
		}

		bool PassesDensityTolerance(int32 SampleCount) const
		{
			return DensityMaximum <= DensityMaximumTolerance && GetDensityMean(SampleCount) <= DensityMeanTolerance;
		}
	};

	bool HasRenderingRhi()
	{
		return !GUsingNullRHI && FApp::CanEverRender();
	}

	FString GetDensityFixturePath()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/FluidFX/fog3d_ref/fixtures/fog3d_density_fixture.bin")));
	}

	uint32 ReadUint32(const uint8* Bytes)
	{
		uint32 Value = 0;
		FMemory::Memcpy(&Value, Bytes, sizeof(Value));
		return Value;
	}

	float ReadFloat(const uint8* Bytes)
	{
		float Value = 0.0f;
		FMemory::Memcpy(&Value, Bytes, sizeof(Value));
		return Value;
	}

	FVector4f ReadVector4(const uint8* Bytes)
	{
		return FVector4f(ReadFloat(Bytes), ReadFloat(Bytes + 4), ReadFloat(Bytes + 8), ReadFloat(Bytes + 12));
	}

	bool LoadDensityFixture(const FString& FilePath, FTDFog3DDensityFixture& OutFixture, FString& OutError)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *FilePath))
		{
			OutError = FString::Printf(TEXT("cannot read %s"), *FilePath);
			return false;
		}

		if (Bytes.Num() < DensityFixtureHeaderBytes || FMemory::Memcmp(Bytes.GetData(), DensityFixtureMagic, 8) != 0)
		{
			OutError = TEXT("fixture magic mismatch");
			return false;
		}

		const uint32 Version = ReadUint32(Bytes.GetData() + 8);
		const int64 ParameterSetCount = ReadUint32(Bytes.GetData() + 12);
		const int64 SampleCount = ReadUint32(Bytes.GetData() + 16);
		const uint32 FloatsPerSample = ReadUint32(Bytes.GetData() + 20);
		const int64 ParameterBytes = ParameterSetCount * DensityFixtureFloatsPerParameterSet * 4;
		const int64 SampleBytes = SampleCount * DensityFixtureFloatsPerSample * 4;
		if (Version != DensityFixtureVersion || FloatsPerSample != DensityFixtureFloatsPerSample || ParameterSetCount == 0)
		{
			OutError = FString::Printf(TEXT("fixture header mismatch version %u floats per sample %u parameter sets %lld"), Version, FloatsPerSample, ParameterSetCount);
			return false;
		}

		if (Bytes.Num() != DensityFixtureHeaderBytes + ParameterBytes + SampleBytes)
		{
			OutError = FString::Printf(TEXT("fixture size %d does not match header (%lld sets, %lld samples)"), Bytes.Num(), ParameterSetCount, SampleCount);
			return false;
		}

		const uint8* Cursor = Bytes.GetData() + DensityFixtureHeaderBytes;
		OutFixture.ParameterSets.Reset();
		for (int64 VectorIndex = 0; VectorIndex < ParameterSetCount * TDFluidGpuFog3DDensityProbeVectorsPerParameterSet; ++VectorIndex)
		{
			OutFixture.ParameterSets.Add(ReadVector4(Cursor));
			Cursor += sizeof(FVector4f);
		}

		OutFixture.Samples.Reset();
		for (int64 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
		{
			const uint32 SetIndex = ReadUint32(Cursor + 12);
			const uint32 Category = ReadUint32(Cursor + 52);
			if (SetIndex >= ParameterSetCount || Category >= DensityFixtureCategoryCount)
			{
				OutError = FString::Printf(TEXT("sample %lld has set %u category %u out of range"), SampleIndex, SetIndex, Category);
				return false;
			}

			FTDFog3DDensityFixtureSample& Sample = OutFixture.Samples.AddDefaulted_GetRef();
			Sample.PositionAndSet = FVector4f(ReadFloat(Cursor), ReadFloat(Cursor + 4), ReadFloat(Cursor + 8), static_cast<float>(SetIndex));
			Sample.State = ReadVector4(Cursor + 16);
			Sample.ExpectedUvw = FVector3f(ReadFloat(Cursor + 32), ReadFloat(Cursor + 36), ReadFloat(Cursor + 40));
			Sample.ExpectedWeight = ReadFloat(Cursor + 44);
			Sample.ExpectedDensity = ReadFloat(Cursor + 48);
			Sample.Category = static_cast<int32>(Category);
			Cursor += DensityFixtureFloatsPerSample * 4;
		}

		return true;
	}

	TArray<FVector4f> BuildSampleInputs(const FTDFog3DDensityFixture& Fixture)
	{
		TArray<FVector4f> SampleInputs;
		SampleInputs.Reserve(Fixture.Samples.Num() * TDFluidGpuFog3DDensityProbeInputVectorsPerSample);
		for (const FTDFog3DDensityFixtureSample& Sample : Fixture.Samples)
		{
			SampleInputs.Add(Sample.PositionAndSet);
			SampleInputs.Add(Sample.State);
		}
		return SampleInputs;
	}

	TArray<FVector4f> BuildSeedMutatedParameterSets(const TArray<FVector4f>& ParameterSets, int32& OutMutatedSetCount)
	{
		TArray<FVector4f> Mutated = ParameterSets;
		OutMutatedSetCount = 0;
		for (int32 SetBase = 0; SetBase < Mutated.Num(); SetBase += TDFluidGpuFog3DDensityProbeVectorsPerParameterSet)
		{
			FVector4f& FineNoise = Mutated[SetBase + FineNoiseVectorIndex];
			FVector4f& StainNoise = Mutated[SetBase + StainNoiseVectorIndex];
			if (FineNoise.X == 0.0f && StainNoise.X == 0.0f)
			{
				continue;
			}

			FineNoise.Z += MutatedSeedOffset;
			StainNoise.Z += MutatedSeedOffset;
			++OutMutatedSetCount;
		}
		return Mutated;
	}

	bool RunDensityProbe(const TArray<FVector4f>& SampleInputs, const TArray<FVector4f>& ParameterSets, int32 SampleCount, TArray<FVector4f>& OutOutputs)
	{
		TSharedRef<FTDFog3DDensityProbeRun, ESPMode::ThreadSafe> Run = MakeShared<FTDFog3DDensityProbeRun, ESPMode::ThreadSafe>();
		ENQUEUE_RENDER_COMMAND(TDFluidGpuFog3DDensityProbe)(
			[Run, SampleInputs, ParameterSets, SampleCount](FRHICommandListImmediate& RHICmdList)
			{
				FRHIGPUBufferReadback Readback(TEXT("TDFluidGpu.Fog3DDensityProbeReadback"));
				bool bIsAdded = false;
				{
					FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuFog3DDensityProbe"));
					bIsAdded = AddTDFluidGpuFog3DDensityProbePass(GraphBuilder, SampleInputs, ParameterSets, Readback);
					GraphBuilder.Execute();
				}
				RHICmdList.SubmitAndBlockUntilGPUIdle();
				if (!bIsAdded)
				{
					return;
				}

				int32 ExtraSubmitCount = 0;
				while (!Readback.IsReady() && ExtraSubmitCount < DensityProbeMaximumReadbackExtraSubmitCount)
				{
					RHICmdList.SubmitAndBlockUntilGPUIdle();
					++ExtraSubmitCount;
				}
				Run->bIsCopied = CopyTDFluidGpuFog3DDensityProbeOutputs(Readback, SampleCount, Run->Outputs);
			});
		FlushRenderingCommands();
		OutOutputs = MoveTemp(Run->Outputs);
		return Run->bIsCopied;
	}

	FTDFog3DDensityErrors MeasureDensityErrors(const FTDFog3DDensityFixture& Fixture, const TArray<FVector4f>& Outputs)
	{
		FTDFog3DDensityErrors Errors;
		for (int32 SampleIndex = 0; SampleIndex < Fixture.Samples.Num(); ++SampleIndex)
		{
			const FTDFog3DDensityFixtureSample& Sample = Fixture.Samples[SampleIndex];
			const FVector4f& UvwAndWeight = Outputs[SampleIndex * TDFluidGpuFog3DDensityProbeOutputVectorsPerSample];
			const float Density = Outputs[SampleIndex * TDFluidGpuFog3DDensityProbeOutputVectorsPerSample + 1].X;
			const double DensityError = FMath::Abs(static_cast<double>(Density) - Sample.ExpectedDensity);
			const double UvwError = FMath::Max3(
				FMath::Abs(static_cast<double>(UvwAndWeight.X) - Sample.ExpectedUvw.X),
				FMath::Abs(static_cast<double>(UvwAndWeight.Y) - Sample.ExpectedUvw.Y),
				FMath::Abs(static_cast<double>(UvwAndWeight.Z) - Sample.ExpectedUvw.Z));
			Errors.DensitySum += FMath::IsFinite(DensityError) ? DensityError : 1.0;
			Errors.UvwMaximum = FMath::Max(Errors.UvwMaximum, FMath::IsFinite(UvwError) ? UvwError : 1.0);
			const double WeightError = FMath::Abs(static_cast<double>(UvwAndWeight.W) - Sample.ExpectedWeight);
			Errors.WeightMaximum = FMath::Max(Errors.WeightMaximum, FMath::IsFinite(WeightError) ? WeightError : 1.0);
			Errors.CategoryDensityMaximum[Sample.Category] = FMath::Max(Errors.CategoryDensityMaximum[Sample.Category], DensityError);
			++Errors.CategorySampleCount[Sample.Category];
			Errors.CategoryNonZeroCount[Sample.Category] += Sample.ExpectedDensity > 0.0f ? 1 : 0;
			if (!FMath::IsFinite(DensityError) || DensityError > Errors.DensityMaximum)
			{
				Errors.DensityMaximum = FMath::IsFinite(DensityError) ? DensityError : 1.0;
				Errors.WorstSampleIndex = SampleIndex;
			}
		}
		return Errors;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DDensityMatchesReferenceTest,
	"TDGame.FluidFXRhi.Fog3D.DensityMatchesReference",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DDensityMatchesReferenceTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DDensityRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D DensityMatchesReference needs a rendering RHI. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."));
		return false;
	}

	FTDFog3DDensityFixture Fixture;
	FString LoadError;
	if (!LoadDensityFixture(GetDensityFixturePath(), Fixture, LoadError))
	{
		AddError(FString::Printf(TEXT("Density fixture: %s"), *LoadError));
		return false;
	}

	const int32 SampleCount = Fixture.Samples.Num();
	TestTrue(FString::Printf(TEXT("Fixture holds at least %d samples (%d)"), DensityFixtureMinimumSampleCount, SampleCount), SampleCount >= DensityFixtureMinimumSampleCount);

	const TArray<FVector4f> SampleInputs = BuildSampleInputs(Fixture);
	TArray<FVector4f> Outputs;
	if (!RunDensityProbe(SampleInputs, Fixture.ParameterSets, SampleCount, Outputs))
	{
		AddError(TEXT("Density probe pass was rejected or its readback was not copied"));
		return false;
	}

	const FTDFog3DDensityErrors Errors = MeasureDensityErrors(Fixture, Outputs);
	const double DensityMean = Errors.GetDensityMean(SampleCount);
	AddInfo(FString::Printf(TEXT("Density vs reference: samples %d, max %.3e, mean %.3e, uvw max %.3e, weight max %.3e, worst sample %d"),
		SampleCount, Errors.DensityMaximum, DensityMean, Errors.UvwMaximum, Errors.WeightMaximum, Errors.WorstSampleIndex));
	for (int32 Category = 0; Category < DensityFixtureCategoryCount; ++Category)
	{
		AddInfo(FString::Printf(TEXT("Category %s: samples %d, non-zero expected %d, density max %.3e"),
			DensityFixtureCategoryNames[Category], Errors.CategorySampleCount[Category], Errors.CategoryNonZeroCount[Category], Errors.CategoryDensityMaximum[Category]));
		TestTrue(FString::Printf(TEXT("Category %s has non-zero expected density"), DensityFixtureCategoryNames[Category]), Errors.CategoryNonZeroCount[Category] > 0);
	}

	TestTrue(FString::Printf(TEXT("Density max abs %.3e <= %.0e"), Errors.DensityMaximum, DensityMaximumTolerance), Errors.DensityMaximum <= DensityMaximumTolerance);
	TestTrue(FString::Printf(TEXT("Density mean abs %.3e <= %.0e"), DensityMean, DensityMeanTolerance), DensityMean <= DensityMeanTolerance);
	TestTrue(FString::Printf(TEXT("Uvw max abs %.3e <= %.0e"), Errors.UvwMaximum, UvwAndWeightMaximumTolerance), Errors.UvwMaximum <= UvwAndWeightMaximumTolerance);
	TestTrue(FString::Printf(TEXT("Weight max abs %.3e <= %.0e"), Errors.WeightMaximum, UvwAndWeightMaximumTolerance), Errors.WeightMaximum <= UvwAndWeightMaximumTolerance);

	int32 MutatedSetCount = 0;
	const TArray<FVector4f> MutatedParameterSets = BuildSeedMutatedParameterSets(Fixture.ParameterSets, MutatedSetCount);
	TestTrue(TEXT("Seed mutation changes at least one parameter set"), MutatedSetCount > 0);
	TArray<FVector4f> MutatedOutputs;
	if (!RunDensityProbe(SampleInputs, MutatedParameterSets, SampleCount, MutatedOutputs))
	{
		AddError(TEXT("Seed-mutated density probe pass was rejected or its readback was not copied"));
		return false;
	}

	const FTDFog3DDensityErrors MutatedErrors = MeasureDensityErrors(Fixture, MutatedOutputs);
	AddInfo(FString::Printf(TEXT("Seed mutation (+%.0f on %d sets): density max %.3e, mean %.3e"),
		MutatedSeedOffset, MutatedSetCount, MutatedErrors.DensityMaximum, MutatedErrors.GetDensityMean(SampleCount)));
	TestFalse(TEXT("Seed-mutated parameters fail the density tolerance"), MutatedErrors.PassesDensityTolerance(SampleCount));
	TestTrue(FString::Printf(TEXT("Seed mutation moves density by more than %.0e (%.3e)"), MutatedSeedMinimumDensityDifference, MutatedErrors.DensityMaximum),
		MutatedErrors.DensityMaximum > MutatedSeedMinimumDensityDifference);
	return true;
}


namespace TDFluidGpuFog3DGroundDensityRhiTestPrivate
{
	using namespace TDFluidGpuFog3DDensityRhiTestPrivate;

	constexpr uint32 GroundFixtureVersion = 1;
	constexpr int32 GroundFixtureHeaderBytes = 28;
	constexpr int32 GroundFixtureFloatsPerSample = 20;
	constexpr int32 GroundFixtureFloatsPerParameterSet = 32;
	constexpr int32 GroundFixtureCategoryCount = 8;
	constexpr int32 GroundFixtureZeroGroundCategory = 7;
	constexpr int32 GroundFixtureMinimumSampleCount = 2048;
	constexpr float MutatedGroundHeightOffsetCm = 1.0f;
	constexpr double MutatedGroundMinimumDensityDifference = 1.0e-3;
	constexpr ANSICHAR GroundFixtureMagic[] = "TDF3DGD1";

	const TCHAR* const GroundFixtureCategoryNames[GroundFixtureCategoryCount] = {
		TEXT("interior"), TEXT("cliff_side"), TEXT("window_edge"), TEXT("map_outside"),
		TEXT("displacement"), TEXT("height_weight"), TEXT("below_ground"), TEXT("zero_ground") };

	struct FTDFog3DGroundFixtureSample
	{
		FVector4f PositionAndSet = FVector4f::Zero();
		FVector4f State = FVector4f::Zero();
		float GroundHeightCm = 0.0f;
		FVector2f ExpectedGroundUv = FVector2f::ZeroVector;
		FVector3f ExpectedUvw = FVector3f::ZeroVector;
		float ExpectedWeight = 0.0f;
		float ExpectedDensity = 0.0f;
		int32 Category = 0;
	};

	struct FTDFog3DGroundFixture
	{
		TArray<FVector4f> ParameterSets;
		TArray<FTDFog3DGroundFixtureSample> Samples;
	};

	struct FTDFog3DGroundErrors
	{
		double DensityMaximum = 0.0;
		double DensitySum = 0.0;
		double UvwMaximum = 0.0;
		double WeightMaximum = 0.0;
		double GroundUvMaximum = 0.0;
		int32 WorstSampleIndex = INDEX_NONE;
		double CategoryDensityMaximum[GroundFixtureCategoryCount] = {};
		int32 CategorySampleCount[GroundFixtureCategoryCount] = {};
		int32 CategoryNonZeroCount[GroundFixtureCategoryCount] = {};

		double GetDensityMean(int32 SampleCount) const
		{
			return SampleCount > 0 ? DensitySum / SampleCount : 0.0;
		}

		bool PassesDensityTolerance(int32 SampleCount) const
		{
			return DensityMaximum <= DensityMaximumTolerance && GetDensityMean(SampleCount) <= DensityMeanTolerance;
		}
	};

	FString GetGroundFixturePath()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/FluidFX/fog3d_ref/fixtures/fog3d_density_fixture_s2a.bin")));
	}

	double FiniteOrOne(double Value)
	{
		return FMath::IsFinite(Value) ? Value : 1.0;
	}

	bool LoadGroundFixture(const FString& FilePath, FTDFog3DGroundFixture& OutFixture, FString& OutError)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *FilePath))
		{
			OutError = FString::Printf(TEXT("cannot read %s"), *FilePath);
			return false;
		}

		if (Bytes.Num() < GroundFixtureHeaderBytes || FMemory::Memcmp(Bytes.GetData(), GroundFixtureMagic, 8) != 0)
		{
			OutError = TEXT("ground fixture magic mismatch");
			return false;
		}

		const uint32 Version = ReadUint32(Bytes.GetData() + 8);
		const int64 ParameterSetCount = ReadUint32(Bytes.GetData() + 12);
		const int64 SampleCount = ReadUint32(Bytes.GetData() + 16);
		const uint32 FloatsPerSample = ReadUint32(Bytes.GetData() + 20);
		const uint32 FloatsPerParameterSet = ReadUint32(Bytes.GetData() + 24);
		if (Version != GroundFixtureVersion || FloatsPerSample != GroundFixtureFloatsPerSample || FloatsPerParameterSet != GroundFixtureFloatsPerParameterSet || ParameterSetCount == 0)
		{
			OutError = FString::Printf(TEXT("ground fixture header mismatch version %u floats per sample %u per set %u parameter sets %lld"), Version, FloatsPerSample, FloatsPerParameterSet, ParameterSetCount);
			return false;
		}

		const int64 ExpectedBytes = GroundFixtureHeaderBytes + ParameterSetCount * GroundFixtureFloatsPerParameterSet * 4 + SampleCount * GroundFixtureFloatsPerSample * 4;
		if (Bytes.Num() != ExpectedBytes)
		{
			OutError = FString::Printf(TEXT("ground fixture size %d does not match header (%lld sets, %lld samples)"), Bytes.Num(), ParameterSetCount, SampleCount);
			return false;
		}

		const uint8* Cursor = Bytes.GetData() + GroundFixtureHeaderBytes;
		OutFixture.ParameterSets.Reset();
		for (int64 VectorIndex = 0; VectorIndex < ParameterSetCount * TDFluidGpuFog3DGroundDensityProbeVectorsPerParameterSet; ++VectorIndex)
		{
			OutFixture.ParameterSets.Add(ReadVector4(Cursor));
			Cursor += sizeof(FVector4f);
		}

		OutFixture.Samples.Reset();
		for (int64 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
		{
			const uint32 SetIndex = ReadUint32(Cursor + 12);
			const uint32 Category = ReadUint32(Cursor + 44);
			if (SetIndex >= ParameterSetCount || Category >= GroundFixtureCategoryCount)
			{
				OutError = FString::Printf(TEXT("ground sample %lld has set %u category %u out of range"), SampleIndex, SetIndex, Category);
				return false;
			}

			FTDFog3DGroundFixtureSample& Sample = OutFixture.Samples.AddDefaulted_GetRef();
			Sample.PositionAndSet = FVector4f(ReadFloat(Cursor), ReadFloat(Cursor + 4), ReadFloat(Cursor + 8), static_cast<float>(SetIndex));
			Sample.State = ReadVector4(Cursor + 16);
			Sample.GroundHeightCm = ReadFloat(Cursor + 32);
			Sample.ExpectedGroundUv = FVector2f(ReadFloat(Cursor + 36), ReadFloat(Cursor + 40));
			Sample.Category = static_cast<int32>(Category);
			Sample.ExpectedUvw = FVector3f(ReadFloat(Cursor + 48), ReadFloat(Cursor + 52), ReadFloat(Cursor + 56));
			Sample.ExpectedWeight = ReadFloat(Cursor + 60);
			Sample.ExpectedDensity = ReadFloat(Cursor + 64);
			Cursor += GroundFixtureFloatsPerSample * 4;
		}

		return true;
	}

	TArray<FVector4f> BuildGroundSampleInputs(const FTDFog3DGroundFixture& Fixture, float GroundHeightOffsetCm)
	{
		TArray<FVector4f> SampleInputs;
		SampleInputs.Reserve(Fixture.Samples.Num() * TDFluidGpuFog3DGroundDensityProbeInputVectorsPerSample);
		for (const FTDFog3DGroundFixtureSample& Sample : Fixture.Samples)
		{
			SampleInputs.Add(Sample.PositionAndSet);
			SampleInputs.Add(Sample.State);
			SampleInputs.Add(FVector4f(Sample.GroundHeightCm + GroundHeightOffsetCm, 0.0f, 0.0f, 0.0f));
		}
		return SampleInputs;
	}

	void BuildZeroGroundFlatInputs(const FTDFog3DGroundFixture& Fixture, TArray<FVector4f>& OutSampleInputs, TArray<FVector4f>& OutParameterSets, TArray<int32>& OutSampleIndices)
	{
		OutSampleInputs.Reset();
		OutParameterSets.Reset();
		OutSampleIndices.Reset();
		const int32 SetCount = Fixture.ParameterSets.Num() / TDFluidGpuFog3DGroundDensityProbeVectorsPerParameterSet;
		for (int32 SetIndex = 0; SetIndex < SetCount; ++SetIndex)
		{
			for (int32 VectorIndex = 0; VectorIndex < TDFluidGpuFog3DDensityProbeVectorsPerParameterSet; ++VectorIndex)
			{
				OutParameterSets.Add(Fixture.ParameterSets[SetIndex * TDFluidGpuFog3DGroundDensityProbeVectorsPerParameterSet + VectorIndex]);
			}
		}

		for (int32 SampleIndex = 0; SampleIndex < Fixture.Samples.Num(); ++SampleIndex)
		{
			const FTDFog3DGroundFixtureSample& Sample = Fixture.Samples[SampleIndex];
			if (Sample.Category != GroundFixtureZeroGroundCategory || Sample.GroundHeightCm != 0.0f)
			{
				continue;
			}

			OutSampleInputs.Add(Sample.PositionAndSet);
			OutSampleInputs.Add(Sample.State);
			OutSampleIndices.Add(SampleIndex);
		}
	}

	bool RunProbe(bool bUsesGround, const TArray<FVector4f>& SampleInputs, const TArray<FVector4f>& ParameterSets, int32 SampleCount, TArray<FVector4f>& OutOutputs)
	{
		TSharedRef<FTDFog3DDensityProbeRun, ESPMode::ThreadSafe> Run = MakeShared<FTDFog3DDensityProbeRun, ESPMode::ThreadSafe>();
		ENQUEUE_RENDER_COMMAND(TDFluidGpuFog3DGroundDensityProbe)(
			[Run, bUsesGround, SampleInputs, ParameterSets, SampleCount](FRHICommandListImmediate& RHICmdList)
			{
				FRHIGPUBufferReadback Readback(TEXT("TDFluidGpu.Fog3DGroundDensityProbeReadback"));
				bool bIsAdded = false;
				{
					FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuFog3DGroundDensityProbe"));
					bIsAdded = bUsesGround
						? AddTDFluidGpuFog3DGroundDensityProbePass(GraphBuilder, SampleInputs, ParameterSets, Readback)
						: AddTDFluidGpuFog3DDensityProbePass(GraphBuilder, SampleInputs, ParameterSets, Readback);
					GraphBuilder.Execute();
				}
				RHICmdList.SubmitAndBlockUntilGPUIdle();
				if (!bIsAdded)
				{
					return;
				}

				int32 ExtraSubmitCount = 0;
				while (!Readback.IsReady() && ExtraSubmitCount < DensityProbeMaximumReadbackExtraSubmitCount)
				{
					RHICmdList.SubmitAndBlockUntilGPUIdle();
					++ExtraSubmitCount;
				}
				Run->bIsCopied = CopyTDFluidGpuFog3DDensityProbeOutputs(Readback, SampleCount, Run->Outputs);
			});
		FlushRenderingCommands();
		OutOutputs = MoveTemp(Run->Outputs);
		return Run->bIsCopied;
	}

	FTDFog3DGroundErrors MeasureGroundErrors(const FTDFog3DGroundFixture& Fixture, const TArray<FVector4f>& Outputs)
	{
		FTDFog3DGroundErrors Errors;
		for (int32 SampleIndex = 0; SampleIndex < Fixture.Samples.Num(); ++SampleIndex)
		{
			const FTDFog3DGroundFixtureSample& Sample = Fixture.Samples[SampleIndex];
			const FVector4f& UvwAndWeight = Outputs[SampleIndex * TDFluidGpuFog3DDensityProbeOutputVectorsPerSample];
			const FVector4f& DensityAndGroundUv = Outputs[SampleIndex * TDFluidGpuFog3DDensityProbeOutputVectorsPerSample + 1];
			const double DensityError = FiniteOrOne(FMath::Abs(static_cast<double>(DensityAndGroundUv.X) - Sample.ExpectedDensity));
			const double UvwError = FMath::Max3(
				FMath::Abs(static_cast<double>(UvwAndWeight.X) - Sample.ExpectedUvw.X),
				FMath::Abs(static_cast<double>(UvwAndWeight.Y) - Sample.ExpectedUvw.Y),
				FMath::Abs(static_cast<double>(UvwAndWeight.Z) - Sample.ExpectedUvw.Z));
			const double GroundUvError = FMath::Max(
				FMath::Abs(static_cast<double>(DensityAndGroundUv.Y) - Sample.ExpectedGroundUv.X),
				FMath::Abs(static_cast<double>(DensityAndGroundUv.Z) - Sample.ExpectedGroundUv.Y));
			Errors.DensitySum += DensityError;
			Errors.UvwMaximum = FMath::Max(Errors.UvwMaximum, FiniteOrOne(UvwError));
			Errors.WeightMaximum = FMath::Max(Errors.WeightMaximum, FiniteOrOne(FMath::Abs(static_cast<double>(UvwAndWeight.W) - Sample.ExpectedWeight)));
			Errors.GroundUvMaximum = FMath::Max(Errors.GroundUvMaximum, FiniteOrOne(GroundUvError));
			Errors.CategoryDensityMaximum[Sample.Category] = FMath::Max(Errors.CategoryDensityMaximum[Sample.Category], DensityError);
			++Errors.CategorySampleCount[Sample.Category];
			Errors.CategoryNonZeroCount[Sample.Category] += Sample.ExpectedDensity > 0.0f ? 1 : 0;
			if (DensityError > Errors.DensityMaximum)
			{
				Errors.DensityMaximum = DensityError;
				Errors.WorstSampleIndex = SampleIndex;
			}
		}
		return Errors;
	}

	int32 CountZeroGroundBitMismatches(const TArray<FVector4f>& GroundOutputs, const TArray<FVector4f>& FlatOutputs, const TArray<int32>& SampleIndices)
	{
		int32 MismatchCount = 0;
		for (int32 FlatIndex = 0; FlatIndex < SampleIndices.Num(); ++FlatIndex)
		{
			const int32 GroundBase = SampleIndices[FlatIndex] * TDFluidGpuFog3DDensityProbeOutputVectorsPerSample;
			const int32 FlatBase = FlatIndex * TDFluidGpuFog3DDensityProbeOutputVectorsPerSample;
			const bool bIsSame = FMemory::Memcmp(&GroundOutputs[GroundBase], &FlatOutputs[FlatBase], sizeof(FVector4f)) == 0
				&& FMemory::Memcmp(&GroundOutputs[GroundBase + 1].X, &FlatOutputs[FlatBase + 1].X, sizeof(float)) == 0;
			MismatchCount += bIsSame ? 0 : 1;
		}
		return MismatchCount;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DGroundDensityMatchesReferenceTest,
	"TDGame.FluidFXRhi.Fog3D.GroundDensityMatchesReference",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DGroundDensityMatchesReferenceTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DGroundDensityRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D GroundDensityMatchesReference needs a rendering RHI. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."));
		return false;
	}

	FTDFog3DGroundFixture Fixture;
	FString LoadError;
	if (!LoadGroundFixture(GetGroundFixturePath(), Fixture, LoadError))
	{
		AddError(FString::Printf(TEXT("Ground density fixture: %s"), *LoadError));
		return false;
	}

	const int32 SampleCount = Fixture.Samples.Num();
	TestTrue(FString::Printf(TEXT("Ground fixture holds at least %d samples (%d)"), GroundFixtureMinimumSampleCount, SampleCount), SampleCount >= GroundFixtureMinimumSampleCount);

	const TArray<FVector4f> SampleInputs = BuildGroundSampleInputs(Fixture, 0.0f);
	TArray<FVector4f> Outputs;
	if (!RunProbe(true, SampleInputs, Fixture.ParameterSets, SampleCount, Outputs))
	{
		AddError(TEXT("Ground density probe pass was rejected or its readback was not copied"));
		return false;
	}

	const FTDFog3DGroundErrors Errors = MeasureGroundErrors(Fixture, Outputs);
	const double DensityMean = Errors.GetDensityMean(SampleCount);
	AddInfo(FString::Printf(TEXT("Ground density vs reference: samples %d, max %.3e, mean %.3e, uvw max %.3e, weight max %.3e, ground uv max %.3e, worst sample %d"),
		SampleCount, Errors.DensityMaximum, DensityMean, Errors.UvwMaximum, Errors.WeightMaximum, Errors.GroundUvMaximum, Errors.WorstSampleIndex));
	for (int32 Category = 0; Category < GroundFixtureCategoryCount; ++Category)
	{
		AddInfo(FString::Printf(TEXT("Ground category %s: samples %d, non-zero expected %d, density max %.3e"),
			GroundFixtureCategoryNames[Category], Errors.CategorySampleCount[Category], Errors.CategoryNonZeroCount[Category], Errors.CategoryDensityMaximum[Category]));
		TestTrue(FString::Printf(TEXT("Ground category %s has non-zero expected density"), GroundFixtureCategoryNames[Category]), Errors.CategoryNonZeroCount[Category] > 0);
	}

	TestTrue(FString::Printf(TEXT("Ground density max abs %.3e <= %.0e"), Errors.DensityMaximum, DensityMaximumTolerance), Errors.DensityMaximum <= DensityMaximumTolerance);
	TestTrue(FString::Printf(TEXT("Ground density mean abs %.3e <= %.0e"), DensityMean, DensityMeanTolerance), DensityMean <= DensityMeanTolerance);
	TestTrue(FString::Printf(TEXT("Ground uvw max abs %.3e <= %.0e"), Errors.UvwMaximum, UvwAndWeightMaximumTolerance), Errors.UvwMaximum <= UvwAndWeightMaximumTolerance);
	TestTrue(FString::Printf(TEXT("Ground weight max abs %.3e <= %.0e"), Errors.WeightMaximum, UvwAndWeightMaximumTolerance), Errors.WeightMaximum <= UvwAndWeightMaximumTolerance);
	TestTrue(FString::Printf(TEXT("Ground uv max abs %.3e <= %.0e"), Errors.GroundUvMaximum, UvwAndWeightMaximumTolerance), Errors.GroundUvMaximum <= UvwAndWeightMaximumTolerance);

	TArray<FVector4f> FlatSampleInputs;
	TArray<FVector4f> FlatParameterSets;
	TArray<int32> ZeroGroundSampleIndices;
	BuildZeroGroundFlatInputs(Fixture, FlatSampleInputs, FlatParameterSets, ZeroGroundSampleIndices);
	TestTrue(TEXT("Ground fixture has zero-height samples"), ZeroGroundSampleIndices.Num() > 0);
	TArray<FVector4f> FlatOutputs;
	if (ZeroGroundSampleIndices.Num() > 0 && RunProbe(false, FlatSampleInputs, FlatParameterSets, ZeroGroundSampleIndices.Num(), FlatOutputs))
	{
		const int32 MismatchCount = CountZeroGroundBitMismatches(Outputs, FlatOutputs, ZeroGroundSampleIndices);
		AddInfo(FString::Printf(TEXT("Zero ground vs stage-1 probe: samples %d, bit mismatches %d"), ZeroGroundSampleIndices.Num(), MismatchCount));
		TestEqual(TEXT("Zero ground height matches the stage-1 density bit for bit"), MismatchCount, 0);
	}
	else
	{
		AddError(TEXT("Stage-1 density probe for zero-height samples was rejected or its readback was not copied"));
	}

	const TArray<FVector4f> MutatedInputs = BuildGroundSampleInputs(Fixture, MutatedGroundHeightOffsetCm);
	TArray<FVector4f> MutatedOutputs;
	if (!RunProbe(true, MutatedInputs, Fixture.ParameterSets, SampleCount, MutatedOutputs))
	{
		AddError(TEXT("Height-mutated ground density probe pass was rejected or its readback was not copied"));
		return false;
	}

	const FTDFog3DGroundErrors MutatedErrors = MeasureGroundErrors(Fixture, MutatedOutputs);
	AddInfo(FString::Printf(TEXT("Ground height mutation (+%.1f cm): density max %.3e, mean %.3e"), MutatedGroundHeightOffsetCm, MutatedErrors.DensityMaximum, MutatedErrors.GetDensityMean(SampleCount)));
	TestFalse(TEXT("Height-mutated ground fails the density tolerance"), MutatedErrors.PassesDensityTolerance(SampleCount));
	TestTrue(FString::Printf(TEXT("Height mutation moves density by more than %.0e (%.3e)"), MutatedGroundMinimumDensityDifference, MutatedErrors.DensityMaximum),
		MutatedErrors.DensityMaximum > MutatedGroundMinimumDensityDifference);
	return true;
}

#endif
