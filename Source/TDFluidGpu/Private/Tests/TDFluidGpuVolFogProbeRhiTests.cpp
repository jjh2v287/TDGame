#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicRHI.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "HAL/FileManager.h"
#include "Math/Float16Color.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "RHIGlobals.h"
#include "RenderGraphBuilder.h"
#include "RenderingThread.h"
#include "TDFluidGpuVolFogProbe.h"
#include "TextureResource.h"
#include "UObject/Package.h"

namespace TDFluidGpuVolFogProbeRhiTestPrivate
{
	constexpr int32 VolFogProbeWarmupFrameCount = 60;
	constexpr int32 VolFogProbeMeasuredFrameCount = 120;
	constexpr int32 VolFogProbeMaximumReadbackExtraSubmitCount = 16;
	constexpr float VolFogProbeDensityTolerance = 0.01f;
	constexpr float VolFogProbeCylinderRadiusSquared = 0.0625f;
	constexpr float VolFogProbeLayerBoundaryW = 0.4f;
	constexpr float VolFogProbeFogTopW = 0.8f;
	constexpr float VolFogProbeClearValue = 0.5f;

	struct FTDVolFogProbeTimingCase
	{
		FIntVector GridSize = FIntVector::ZeroValue;
		int32 PressureIterationCount = 0;
		int32 SampleCount = 0;
		double MedianMilliseconds = 0.0;
		double Percentile95Milliseconds = 0.0;
		double MinimumMilliseconds = 0.0;
		double MaximumMilliseconds = 0.0;
		bool bHasResidual = false;
		double ResidualBeforePressure = 0.0;
		double ResidualAfterPressure = 0.0;
		int32 ReadbackExtraSubmitCount = 0;
	};

	struct FTDVolFogProbePatternCheck
	{
		int32 CheckedCount = 0;
		int32 MismatchCount = 0;
		int32 ReadSliceCount = 0;
		FIntVector FirstMismatchCell = FIntVector(-1, -1, -1);
		float FirstMismatchValue = 0.0f;
		float FirstMismatchExpected = 0.0f;
	};

	TArray<FIntVector> MakeVolFogProbeGridSizes()
	{
		return { FIntVector(128, 128, 8), FIntVector(192, 192, 8), FIntVector(256, 256, 8), FIntVector(256, 256, 16), FIntVector(128, 128, 32) };
	}

	TArray<int32> MakeVolFogProbePressureIterationCounts()
	{
		return { 4, 8, 12, 16 };
	}

	FString FormatVolFogProbeGrid(const FIntVector& GridSize)
	{
		return FString::Printf(TEXT("%dx%dx%d"), GridSize.X, GridSize.Y, GridSize.Z);
	}

	FString EscapeVolFogProbeJsonText(const FString& Text)
	{
		return Text.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
	}

	bool AreVolFogProbeResidualReadbacksReady(const FTDFluidGpuVolFogProbeResidualReadbacks& Readbacks)
	{
		return Readbacks.BeforePressure.IsValid() && Readbacks.AfterPressure.IsValid() && Readbacks.BeforePressure->IsReady() && Readbacks.AfterPressure->IsReady();
	}

	bool SumVolFogProbeResidualColumns(FRHIGPUTextureReadback* Readback, const FIntVector& GridSize, double& OutResidualSum)
	{
		if (!Readback || !Readback->IsReady())
		{
			return false;
		}

		int32 RowPitchInPixels = 0;
		int32 BufferHeight = 0;
		const float* Texels = static_cast<const float*>(Readback->Lock(RowPitchInPixels, &BufferHeight));
		if (!Texels || RowPitchInPixels < GridSize.X || BufferHeight < GridSize.Y)
		{
			Readback->Unlock();
			return false;
		}

		double ResidualSum = 0.0;
		for (int32 RowIndex = 0; RowIndex < GridSize.Y; ++RowIndex)
		{
			for (int32 ColumnIndex = 0; ColumnIndex < GridSize.X; ++ColumnIndex)
			{
				ResidualSum += Texels[RowIndex * RowPitchInPixels + ColumnIndex];
			}
		}
		Readback->Unlock();
		OutResidualSum = ResidualSum;
		return true;
	}

	void SubmitUntilVolFogProbeResidualReadbacksAreReady(FRHICommandListImmediate& RHICmdList, const FTDFluidGpuVolFogProbeResidualReadbacks& Readbacks, int32& OutExtraSubmitCount)
	{
		OutExtraSubmitCount = 0;
		while (!AreVolFogProbeResidualReadbacksReady(Readbacks) && OutExtraSubmitCount < VolFogProbeMaximumReadbackExtraSubmitCount)
		{
			RHICmdList.SubmitAndBlockUntilGPUIdle();
			++OutExtraSubmitCount;
		}
	}

	void RecordVolFogProbeFrameStatistics(TArray<double>& FrameMilliseconds, FTDVolFogProbeTimingCase& InOutTimingCase)
	{
		if (FrameMilliseconds.IsEmpty())
		{
			return;
		}

		FrameMilliseconds.Sort();
		const int32 SampleCount = FrameMilliseconds.Num();
		const int32 Percentile95Index = FMath::Clamp(FMath::CeilToInt(0.95 * SampleCount) - 1, 0, SampleCount - 1);
		InOutTimingCase.SampleCount = SampleCount;
		InOutTimingCase.MedianMilliseconds = FrameMilliseconds[SampleCount / 2];
		InOutTimingCase.Percentile95Milliseconds = FrameMilliseconds[Percentile95Index];
		InOutTimingCase.MinimumMilliseconds = FrameMilliseconds[0];
		InOutTimingCase.MaximumMilliseconds = FrameMilliseconds.Last();
	}

	FTDVolFogProbeTimingCase MeasureVolFogProbeStep(const FIntVector& GridSize, int32 PressureIterationCount)
	{
		TSharedRef<FTDVolFogProbeTimingCase, ESPMode::ThreadSafe> TimingCase = MakeShared<FTDVolFogProbeTimingCase, ESPMode::ThreadSafe>();
		TimingCase->GridSize = GridSize;
		TimingCase->PressureIterationCount = PressureIterationCount;
		ENQUEUE_RENDER_COMMAND(TDFluidGpuVolFogProbeTimingFrames)(
			[TimingCase, GridSize, PressureIterationCount](FRHICommandListImmediate& RHICmdList)
			{
				FTDFluidGpuVolFogProbeState State;
				TArray<FRenderQueryRHIRef> StartQueries;
				TArray<FRenderQueryRHIRef> EndQueries;
				for (int32 FrameIndex = 0; FrameIndex < VolFogProbeWarmupFrameCount + VolFogProbeMeasuredFrameCount; ++FrameIndex)
				{
					const bool bIsMeasured = FrameIndex >= VolFogProbeWarmupFrameCount;
					if (bIsMeasured)
					{
						RHICmdList.EndRenderQuery(StartQueries.Add_GetRef(RHICreateRenderQuery(RQT_AbsoluteTime)));
					}

					{
						FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuVolFogProbeTimingFrame"));
						AddTDFluidGpuVolFogProbeStepPasses(GraphBuilder, State, GridSize, PressureIterationCount);
						GraphBuilder.Execute();
					}

					if (bIsMeasured)
					{
						RHICmdList.EndRenderQuery(EndQueries.Add_GetRef(RHICreateRenderQuery(RQT_AbsoluteTime)));
					}
					RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
				}

				FTDFluidGpuVolFogProbeResidualReadbacks ResidualReadbacks;
				{
					FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuVolFogProbeResidualFrame"));
					AddTDFluidGpuVolFogProbeStepPasses(GraphBuilder, State, GridSize, PressureIterationCount, &ResidualReadbacks);
					GraphBuilder.Execute();
				}

				RHICmdList.SubmitAndBlockUntilGPUIdle();
				SubmitUntilVolFogProbeResidualReadbacksAreReady(RHICmdList, ResidualReadbacks, TimingCase->ReadbackExtraSubmitCount);

				TArray<double> FrameMilliseconds;
				for (int32 QueryIndex = 0; QueryIndex < StartQueries.Num() && QueryIndex < EndQueries.Num(); ++QueryIndex)
				{
					uint64 StartMicroseconds = 0;
					uint64 EndMicroseconds = 0;
					const bool bHasStart = RHIGetRenderQueryResult(StartQueries[QueryIndex], StartMicroseconds, true);
					const bool bHasEnd = RHIGetRenderQueryResult(EndQueries[QueryIndex], EndMicroseconds, true);
					if (bHasStart && bHasEnd && EndMicroseconds >= StartMicroseconds)
					{
						FrameMilliseconds.Add(static_cast<double>(EndMicroseconds - StartMicroseconds) / 1000.0);
					}
				}
				RecordVolFogProbeFrameStatistics(FrameMilliseconds, *TimingCase);

				const bool bHasBefore = SumVolFogProbeResidualColumns(ResidualReadbacks.BeforePressure.Get(), GridSize, TimingCase->ResidualBeforePressure);
				const bool bHasAfter = SumVolFogProbeResidualColumns(ResidualReadbacks.AfterPressure.Get(), GridSize, TimingCase->ResidualAfterPressure);
				TimingCase->bHasResidual = bHasBefore && bHasAfter;
			});
		FlushRenderingCommands();
		return *TimingCase;
	}

	double ComputeVolFogProbeResidualRatio(const FTDVolFogProbeTimingCase& TimingCase)
	{
		if (!TimingCase.bHasResidual || TimingCase.ResidualBeforePressure <= 0.0)
		{
			return -1.0;
		}

		return TimingCase.ResidualAfterPressure / TimingCase.ResidualBeforePressure;
	}

	FString MakeVolFogProbeTimingJson(const TArray<FTDVolFogProbeTimingCase>& TimingCases)
	{
		FString Json = FString::Printf(TEXT("{\n  \"test\": \"TDGame.FluidFXRhi.VolFogProbe.StepTiming\",\n  \"utc\": \"%s\",\n  \"adapter\": \"%s\",\n  \"rhi\": \"%s\",\n  \"warmup_frames\": %d,\n  \"measured_frames\": %d,\n  \"texture_formats\": {\"velocity\": \"RGBA16F\", \"pressure\": \"R32F\", \"divergence\": \"R32F\", \"obstacle_mask\": \"R8_UINT\"},\n  \"step_passes\": \"advect+inputs(8), divergence, pressure x N, gradient\",\n  \"residual\": \"sum |divergence| after gradient / sum |divergence| after advect, one extra untimed frame after measurement\",\n  \"cases\": [\n"),
			*FDateTime::UtcNow().ToIso8601(),
			*EscapeVolFogProbeJsonText(GRHIAdapterName),
			*EscapeVolFogProbeJsonText(FString(GDynamicRHI ? GDynamicRHI->GetName() : TEXT("none"))),
			VolFogProbeWarmupFrameCount,
			VolFogProbeMeasuredFrameCount);
		for (int32 CaseIndex = 0; CaseIndex < TimingCases.Num(); ++CaseIndex)
		{
			const FTDVolFogProbeTimingCase& TimingCase = TimingCases[CaseIndex];
			Json += FString::Printf(TEXT("    {\"grid\": \"%s\", \"cells\": %lld, \"pressure_iterations\": %d, \"passes_per_step\": %d, \"samples\": %d, \"frame_median_ms\": %.4f, \"frame_p95_ms\": %.4f, \"frame_min_ms\": %.4f, \"frame_max_ms\": %.4f, \"residual_before\": %.6g, \"residual_after\": %.6g, \"residual_ratio\": %.6f, \"readback_extra_submits\": %d}%s\n"),
				*FormatVolFogProbeGrid(TimingCase.GridSize),
				static_cast<int64>(TimingCase.GridSize.X) * TimingCase.GridSize.Y * TimingCase.GridSize.Z,
				TimingCase.PressureIterationCount,
				TimingCase.PressureIterationCount + 3,
				TimingCase.SampleCount,
				TimingCase.MedianMilliseconds,
				TimingCase.Percentile95Milliseconds,
				TimingCase.MinimumMilliseconds,
				TimingCase.MaximumMilliseconds,
				TimingCase.ResidualBeforePressure,
				TimingCase.ResidualAfterPressure,
				ComputeVolFogProbeResidualRatio(TimingCase),
				TimingCase.ReadbackExtraSubmitCount,
				CaseIndex + 1 < TimingCases.Num() ? TEXT(",") : TEXT(""));
		}
		Json += TEXT("  ]\n}\n");
		return Json;
	}

	float ComputeExpectedVolFogProbeDensity(const FIntVector& GridSize, int32 CellX, int32 CellY, int32 CellZ, int32 PatternIndex)
	{
		const float U = (CellX + 0.5f) * (1.0f / GridSize.X);
		const float V = (CellY + 0.5f) * (1.0f / GridSize.Y);
		const float W = (CellZ + 0.5f) * (1.0f / GridSize.Z);
		const float RadialU = U - 0.5f;
		const float RadialV = V - 0.5f;
		const bool bIsInsideCylinder = RadialU * RadialU + RadialV * RadialV < VolFogProbeCylinderRadiusSquared;
		const bool bIsLowerLayer = W < VolFogProbeLayerBoundaryW;
		const bool bErasesLayer = PatternIndex == 1 || (PatternIndex == 2 && !bIsLowerLayer) || (PatternIndex == 3 && bIsLowerLayer);
		if (W >= VolFogProbeFogTopW || (bIsInsideCylinder && bErasesLayer))
		{
			return 0.0f;
		}
		return 1.0f;
	}

	UTextureRenderTargetVolume* CreateVolFogProbeVolumeTarget(const FIntVector& GridSize)
	{
		UTextureRenderTargetVolume* Target = NewObject<UTextureRenderTargetVolume>(GetTransientPackage(), NAME_None, RF_Transient);
		Target->bSupportsUAV = true;
		Target->ClearColor = FLinearColor(VolFogProbeClearValue, VolFogProbeClearValue, VolFogProbeClearValue, VolFogProbeClearValue);
		Target->Init(GridSize.X, GridSize.Y, GridSize.Z, PF_FloatRGBA);
		Target->UpdateResourceImmediate(true);
		FlushRenderingCommands();
		return Target;
	}

	FTDVolFogProbePatternCheck CheckVolFogProbePattern(UTextureRenderTargetVolume* Target, const FIntVector& GridSize, int32 PatternIndex)
	{
		FTDVolFogProbePatternCheck PatternCheck;
		FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
		if (!Resource)
		{
			return PatternCheck;
		}

		for (int32 CellZ = 0; CellZ < GridSize.Z; ++CellZ)
		{
			FReadSurfaceDataFlags ReadFlags;
			ReadFlags.SetArrayIndex(CellZ);
			TArray<FFloat16Color> SliceTexels;
			if (!Resource->ReadFloat16Pixels(SliceTexels, ReadFlags, FIntRect(0, 0, GridSize.X, GridSize.Y)) || SliceTexels.Num() != GridSize.X * GridSize.Y)
			{
				continue;
			}

			++PatternCheck.ReadSliceCount;
			for (int32 CellY = 0; CellY < GridSize.Y; ++CellY)
			{
				for (int32 CellX = 0; CellX < GridSize.X; ++CellX)
				{
					const float Value = SliceTexels[CellY * GridSize.X + CellX].A.GetFloat();
					const float Expected = ComputeExpectedVolFogProbeDensity(GridSize, CellX, CellY, CellZ, PatternIndex);
					++PatternCheck.CheckedCount;
					if (FMath::Abs(Value - Expected) <= VolFogProbeDensityTolerance)
					{
						continue;
					}

					if (PatternCheck.MismatchCount == 0)
					{
						PatternCheck.FirstMismatchCell = FIntVector(CellX, CellY, CellZ);
						PatternCheck.FirstMismatchValue = Value;
						PatternCheck.FirstMismatchExpected = Expected;
					}
					++PatternCheck.MismatchCount;
				}
			}
		}
		return PatternCheck;
	}

	bool HasRenderingRhi()
	{
		return !GUsingNullRHI && FApp::CanEverRender();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuVolFogProbeStepTimingTest,
	"TDGame.FluidFXRhi.VolFogProbe.StepTiming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuVolFogProbeStepTimingTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuVolFogProbeRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("VolFogProbe StepTiming needs a rendering RHI. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."));
		return false;
	}

	TArray<FTDVolFogProbeTimingCase> TimingCases;
	for (const FIntVector& GridSize : MakeVolFogProbeGridSizes())
	{
		for (const int32 PressureIterationCount : MakeVolFogProbePressureIterationCounts())
		{
			TimingCases.Add(MeasureVolFogProbeStep(GridSize, PressureIterationCount));
		}
	}

	for (const FTDVolFogProbeTimingCase& TimingCase : TimingCases)
	{
		const FString CaseName = FString::Printf(TEXT("%s pressure %d"), *FormatVolFogProbeGrid(TimingCase.GridSize), TimingCase.PressureIterationCount);
		AddInfo(FString::Printf(TEXT("VolFogProbe StepTiming %s: median %.4f ms, p95 %.4f ms, min %.4f ms, max %.4f ms over %d frames, residual ratio %.4f"),
			*CaseName, TimingCase.MedianMilliseconds, TimingCase.Percentile95Milliseconds, TimingCase.MinimumMilliseconds, TimingCase.MaximumMilliseconds, TimingCase.SampleCount, ComputeVolFogProbeResidualRatio(TimingCase)));
		TestTrue(*FString::Printf(TEXT("%s produced GPU timestamps"), *CaseName), TimingCase.SampleCount > 0);
		TestTrue(*FString::Printf(TEXT("%s read residual divergence"), *CaseName), TimingCase.bHasResidual);
	}

	const FString OutputPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FluidFX/perf/volfog3d_probe_timing.json"));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
	TestTrue(TEXT("VolFogProbe timing JSON written"), FFileHelper::SaveStringToFile(MakeVolFogProbeTimingJson(TimingCases), *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuVolFogProbeVolumeTargetWriteTest,
	"TDGame.FluidFXRhi.VolFogProbe.VolumeTargetWrite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuVolFogProbeVolumeTargetWriteTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuVolFogProbeRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("VolFogProbe VolumeTargetWrite needs a rendering RHI. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."));
		return false;
	}

	TestFalse(TEXT("Null target is rejected"), WriteTDFluidGpuVolFogProbePattern(nullptr, 0));
	const TArray<FIntVector> TargetSizes = { FIntVector(128, 128, 64), FIntVector(40, 40, 10) };
	for (const FIntVector& GridSize : TargetSizes)
	{
		UTextureRenderTargetVolume* Target = CreateVolFogProbeVolumeTarget(GridSize);
		const FString GridName = FormatVolFogProbeGrid(GridSize);
		TestTrue(*FString::Printf(TEXT("%s target allows UAV"), *GridName), Target->bCanCreateUAV != 0);
		TestFalse(*FString::Printf(TEXT("%s pattern 4 is rejected"), *GridName), WriteTDFluidGpuVolFogProbePattern(Target, TDFluidGpuVolFogProbePatternCount));
		for (int32 PatternIndex = 0; PatternIndex < TDFluidGpuVolFogProbePatternCount; ++PatternIndex)
		{
			const FString CaseName = FString::Printf(TEXT("%s pattern %d"), *GridName, PatternIndex);
			if (!TestTrue(*FString::Printf(TEXT("%s write accepted"), *CaseName), WriteTDFluidGpuVolFogProbePattern(Target, PatternIndex)))
			{
				continue;
			}

			FlushRenderingCommands();
			const FTDVolFogProbePatternCheck PatternCheck = CheckVolFogProbePattern(Target, GridSize, PatternIndex);
			AddInfo(FString::Printf(TEXT("VolFogProbe VolumeTargetWrite %s: slices %d/%d, checked %d, mismatches %d, first mismatch (%d,%d,%d) value %.3f expected %.3f"),
				*CaseName, PatternCheck.ReadSliceCount, GridSize.Z, PatternCheck.CheckedCount, PatternCheck.MismatchCount,
				PatternCheck.FirstMismatchCell.X, PatternCheck.FirstMismatchCell.Y, PatternCheck.FirstMismatchCell.Z, PatternCheck.FirstMismatchValue, PatternCheck.FirstMismatchExpected));
			TestEqual(*FString::Printf(TEXT("%s read every slice"), *CaseName), PatternCheck.ReadSliceCount, GridSize.Z);
			TestEqual(*FString::Printf(TEXT("%s checked every cell"), *CaseName), PatternCheck.CheckedCount, GridSize.X * GridSize.Y * GridSize.Z);
			TestEqual(*FString::Printf(TEXT("%s matches the pattern contract"), *CaseName), PatternCheck.MismatchCount, 0);
		}
		Target->MarkAsGarbage();
	}
	return true;
}

#endif
