#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicRHI.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHIGlobals.h"
#include "RenderGraphBuilder.h"
#include "RenderingThread.h"
#include "TDFluidGpuBatch.h"
#include "TDFluidGpuInputs.h"
#include "TDFluidGpuStepPasses.h"

namespace TDFluidGpuDenseWaterPerfRhiTestPrivate
{
	constexpr float PerfStepSeconds = 1.0f / 60.0f;
	constexpr int32 WarmupFrameCount = 60;
	constexpr int32 MeasuredFrameCount = 120;
	constexpr uint64 PerfActorKey = 6301;
	constexpr double CreekCellCm = 25.0;
	constexpr float CreekSlope = 0.05f;
	constexpr float CreekHalfWidthCm = 400.0f;
	constexpr float CreekBankHeightCm = 80.0f;
	constexpr float CreekDepthCm = 20.0f;
	constexpr float CreekSpeedCmPerSec = 100.0f;
	constexpr int32 CreekSeedResolution = 128;

	struct FTDDensePerfCase
	{
		int32 Resolution = 0;
		int32 SampleCount = 0;
		double MedianMilliseconds = 0.0;
		double MinimumMilliseconds = 0.0;
		double MaximumMilliseconds = 0.0;
	};

	double ComputeCreekExtentCm(int32 Resolution)
	{
		return Resolution * CreekCellCm;
	}

	float ComputeCreekElevationCm(double PlaneX, double PlaneY)
	{
		const float BankCm = FMath::Abs(PlaneY) <= CreekHalfWidthCm ? 0.0f : CreekBankHeightCm;
		return static_cast<float>(-CreekSlope * PlaneX) + BankCm;
	}

	FTDFluidGpuSimulationSettings MakeCreekSettings(int32 Resolution)
	{
		FTDFluidGpuSimulationSettings Settings;
		Settings.SimMode = ETDFluidGpuSimMode::FluidSim;
		Settings.Resolution = Resolution;
		Settings.ExtentCm = FVector2D(ComputeCreekExtentCm(Resolution), ComputeCreekExtentCm(Resolution));
		Settings.PressureIterations = 12;
		Settings.bUsesWaterLayer = true;
		Settings.bOpenWaterBorders = true;
		Settings.bTreatDryCellsAsSolid = false;
		Settings.bUsesDenseWater = true;
		FTDFluidGpuBatch::ApplyWaterConsoleOverrides(Settings);
		return Settings;
	}

	TArray<FVector4f> MakeCreekSeedTexels(int32 Resolution)
	{
		TArray<FVector4f> Texels;
		Texels.SetNumZeroed(CreekSeedResolution * CreekSeedResolution);
		const double ExtentCm = ComputeCreekExtentCm(Resolution);
		const double SeedCellCm = ExtentCm / CreekSeedResolution;
		for (int32 SeedY = 0; SeedY < CreekSeedResolution; ++SeedY)
		{
			const double PlaneY = -0.5 * ExtentCm + (SeedY + 0.5) * SeedCellCm;
			if (FMath::Abs(PlaneY) > CreekHalfWidthCm)
			{
				continue;
			}

			for (int32 SeedX = 0; SeedX < CreekSeedResolution; ++SeedX)
			{
				Texels[SeedY * CreekSeedResolution + SeedX] = FVector4f(CreekDepthCm, CreekSpeedCmPerSec, 0.0f, 0.0f);
			}
		}
		return Texels;
	}

	FTDFluidGpuActorInput MakeCreekActor(const FTDFluidGpuSimulationSettings& Settings, int32 FrameIndex, const TArray<FVector4f>& SeedTexels)
	{
		FTDFluidGpuActorInput Actor;
		Actor.ActorKey = PerfActorKey;
		Actor.Settings = Settings;
		Actor.DeltaTimeSeconds = PerfStepSeconds;
		Actor.SimulationTimeSeconds = FrameIndex * static_cast<double>(PerfStepSeconds);
		Actor.bHasObstacleListChanged = FrameIndex == 0;
		if (FrameIndex != 0)
		{
			return Actor;
		}

		const int32 ResolutionTier = FTDFluidGpuBatch::ResolveWaterResolutionTier(Settings.Resolution);
		const int32 FieldResolution = ResolutionTier / FTDFluidGpuBatch::ComputeWaterFieldDivisor(ResolutionTier);
		const double FieldCellCm = Settings.ExtentCm.X / FieldResolution;
		Actor.WaterTerrainUpdates.Reserve(FieldResolution * FieldResolution);
		for (int32 FieldY = 0; FieldY < FieldResolution; ++FieldY)
		{
			for (int32 FieldX = 0; FieldX < FieldResolution; ++FieldX)
			{
				FTDFluidGpuTerrainCellUpdate& TerrainUpdate = Actor.WaterTerrainUpdates.AddDefaulted_GetRef();
				TerrainUpdate.Cell = FIntPoint(FieldX, FieldY);
				TerrainUpdate.ElevationCm = ComputeCreekElevationCm(-0.5 * Settings.ExtentCm.X + (FieldX + 0.5) * FieldCellCm, -0.5 * Settings.ExtentCm.Y + (FieldY + 0.5) * FieldCellCm);
			}
		}
		Actor.DenseSeedResolution = CreekSeedResolution;
		Actor.DenseSeedTexels = SeedTexels;
		return Actor;
	}

	FTDDensePerfCase MeasureCreek(int32 Resolution)
	{
		TSharedRef<FTDDensePerfCase, ESPMode::ThreadSafe> PerfCase = MakeShared<FTDDensePerfCase, ESPMode::ThreadSafe>();
		PerfCase->Resolution = Resolution;
		const FTDFluidGpuSimulationSettings Settings = MakeCreekSettings(Resolution);
		const TArray<FVector4f> SeedTexels = MakeCreekSeedTexels(Resolution);
		ENQUEUE_RENDER_COMMAND(TDFluidGpuDenseWaterPerfFrames)(
			[PerfCase, Settings, SeedTexels](FRHICommandListImmediate& RHICmdList)
			{
				TArray<FTDFluidGpuBatch> Batches;
				TArray<FRenderQueryRHIRef> StartQueries;
				TArray<FRenderQueryRHIRef> EndQueries;
				for (int32 FrameIndex = 0; FrameIndex < WarmupFrameCount + MeasuredFrameCount; ++FrameIndex)
				{
					const bool bIsMeasured = FrameIndex >= WarmupFrameCount;
					if (bIsMeasured)
					{
						RHICmdList.EndRenderQuery(StartQueries.Add_GetRef(RHICreateRenderQuery(RQT_AbsoluteTime)));
					}

					FTDFluidGpuFrameInputs FrameInputs;
					FrameInputs.FrameNumber = static_cast<uint32>(FrameIndex);
					FrameInputs.Actors.Add(MakeCreekActor(Settings, FrameIndex, SeedTexels));
					{
						FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuDenseWaterPerfFrame"));
						AddTDFluidGpuFramePasses(GraphBuilder, FrameInputs, Batches);
						GraphBuilder.Execute();
					}

					if (bIsMeasured)
					{
						RHICmdList.EndRenderQuery(EndQueries.Add_GetRef(RHICreateRenderQuery(RQT_AbsoluteTime)));
					}
					RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
				}

				RHICmdList.SubmitAndBlockUntilGPUIdle();
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
				Batches.Empty();
				if (FrameMilliseconds.IsEmpty())
				{
					return;
				}

				FrameMilliseconds.Sort();
				PerfCase->SampleCount = FrameMilliseconds.Num();
				PerfCase->MedianMilliseconds = FrameMilliseconds[FrameMilliseconds.Num() / 2];
				PerfCase->MinimumMilliseconds = FrameMilliseconds[0];
				PerfCase->MaximumMilliseconds = FrameMilliseconds.Last();
			});
		FlushRenderingCommands();
		return *PerfCase;
	}

	FString MakeTimingJson(const TArray<FTDDensePerfCase>& PerfCases)
	{
		FString Json = FString::Printf(TEXT("{\n  \"warmup_frames\": %d,\n  \"measured_frames\": %d,\n  \"step_seconds\": %.6f,\n  \"includes_outputs\": false,\n  \"cases\": [\n"), WarmupFrameCount, MeasuredFrameCount, PerfStepSeconds);
		for (int32 CaseIndex = 0; CaseIndex < PerfCases.Num(); ++CaseIndex)
		{
			const FTDDensePerfCase& PerfCase = PerfCases[CaseIndex];
			Json += FString::Printf(TEXT("    {\"resolution\": %d, \"samples\": %d, \"frame_median_ms\": %.4f, \"frame_min_ms\": %.4f, \"frame_max_ms\": %.4f}%s\n"),
				PerfCase.Resolution, PerfCase.SampleCount, PerfCase.MedianMilliseconds, PerfCase.MinimumMilliseconds, PerfCase.MaximumMilliseconds, CaseIndex + 1 < PerfCases.Num() ? TEXT(",") : TEXT(""));
		}
		Json += TEXT("  ]\n}\n");
		return Json;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuDenseWaterPassTimingProfileTest,
	"TDGame.FluidFXRhi.DenseWater.PassTimingProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuDenseWaterPassTimingProfileTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuDenseWaterPerfRhiTestPrivate;

	if (GUsingNullRHI || !FApp::CanEverRender())
	{
		AddError(TEXT("PassTimingProfile needs a rendering RHI. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."));
		return false;
	}

	TArray<FTDDensePerfCase> PerfCases;
	PerfCases.Add(MeasureCreek(1024));
	PerfCases.Add(MeasureCreek(2048));
	for (const FTDDensePerfCase& PerfCase : PerfCases)
	{
		AddInfo(FString::Printf(TEXT("DenseWater PassTimingProfile %d: median %.4f ms, min %.4f ms, max %.4f ms over %d frames"), PerfCase.Resolution, PerfCase.MedianMilliseconds, PerfCase.MinimumMilliseconds, PerfCase.MaximumMilliseconds, PerfCase.SampleCount));
		TestTrue(*FString::Printf(TEXT("Dense creek %d produced GPU timestamps"), PerfCase.Resolution), PerfCase.SampleCount > 0);
	}

	const FString OutputPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FluidFX/perf/dense_water_pass_timing.json"));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
	TestTrue(TEXT("Dense water timing JSON written"), FFileHelper::SaveStringToFile(MakeTimingJson(PerfCases), *OutputPath));
	return true;
}

#endif
