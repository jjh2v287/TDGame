#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicRHI.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "RHIGlobals.h"
#include "RHIGPUReadback.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "TDFluidGpuBatch.h"
#include "TDFluidGpuDenseWaterPasses.h"
#include "TDFluidGpuInputs.h"
#include "TDFluidGpuStepPasses.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidGpuDenseWaterRhiTests, Log, All);

namespace TDFluidGpuDenseWaterRhiTestPrivate
{
	constexpr float SixtyHertzStepSeconds = 1.0f / 60.0f;
	constexpr uint64 DenseActorKey = 6101;
	constexpr int32 SeedResolution = 128;
	constexpr float SeedPeakDepthCm = 30.0f;
	constexpr float SeedSigmaCells = 10.0f;
	constexpr double DenseExtentCm = 25600.0;
	constexpr int64 BytesPerMebibyte = 1024 * 1024;
	constexpr int64 DenseActorBudgetBytes = 384 * BytesPerMebibyte;
	constexpr int64 DenseStateBytesPerDenseCell = 24;
	constexpr int64 DenseCellVelocityBytesPerDenseCell = 8;
	constexpr int64 DenseSurfaceBytesPerCell = 4;

	struct FTDDenseDepthSnapshot
	{
		int32 FrameCount = 0;
		int32 Resolution = 0;
		TArray<float> Depths;
	};

	struct FTDDenseRunResult
	{
		bool bHasDenseBatch = false;
		int32 DenseResolution = 0;
		TArray<FTDDenseDepthSnapshot> Snapshots;
	};

	struct FTDDenseRunState
	{
		TArray<FTDFluidGpuBatch> Batches;
		TArray<TUniquePtr<FRHIGPUTextureReadback>> Readbacks;
		FTDDenseRunResult Result;
	};

	bool CanRunDenseWaterRhiTest(FAutomationTestBase& Test, const TCHAR* TestLabel)
	{
		const bool bHasRenderingRhi = !GUsingNullRHI && FApp::CanEverRender();
		UE_LOG(LogTDFluidGpuDenseWaterRhiTests, Display, TEXT("%s: %s (NullRHI=%d, CanEverRender=%d)"), TestLabel, bHasRenderingRhi ? TEXT("RUN") : TEXT("NOT RUNNABLE"), GUsingNullRHI ? 1 : 0, FApp::CanEverRender() ? 1 : 0);
		if (bHasRenderingRhi)
		{
			return true;
		}

		Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI but none is active. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."), TestLabel));
		return false;
	}

	FTDFluidGpuSimulationSettings MakeDenseSettings(int32 Resolution)
	{
		FTDFluidGpuSimulationSettings Settings;
		Settings.SimMode = ETDFluidGpuSimMode::FluidSim;
		Settings.Resolution = Resolution;
		Settings.ExtentCm = FVector2D(DenseExtentCm, DenseExtentCm);
		Settings.PressureIterations = 12;
		Settings.VorticityStrength = 0.0f;
		Settings.TerrainSlopeGravity = 0.0f;
		Settings.bUsesWaterLayer = true;
		Settings.bOpenWaterBorders = false;
		Settings.bTreatDryCellsAsSolid = false;
		Settings.RippleWaveSpeedCmPerSec = 0.0f;
		Settings.bUsesDenseWater = true;
		Settings.DenseWater.FilmDrainCmPerSec = 0.0f;
		Settings.DenseWater.ReservoirRelaxPerSec = 0.0f;
		FTDFluidGpuBatch::ApplyWaterConsoleOverrides(Settings);
		return Settings;
	}

	TArray<FVector4f> MakeMoundSeedTexels()
	{
		TArray<FVector4f> Texels;
		Texels.SetNumZeroed(SeedResolution * SeedResolution);
		const float CenterCell = 0.5f * static_cast<float>(SeedResolution - 1);
		for (int32 Y = 0; Y < SeedResolution; ++Y)
		{
			for (int32 X = 0; X < SeedResolution; ++X)
			{
				const float DistanceSquared = FMath::Square(static_cast<float>(X) - CenterCell) + FMath::Square(static_cast<float>(Y) - CenterCell);
				Texels[Y * SeedResolution + X].X = SeedPeakDepthCm * FMath::Exp(-DistanceSquared / (2.0f * SeedSigmaCells * SeedSigmaCells));
			}
		}
		return Texels;
	}

	double ComputeSeedVolumeCm3(const TArray<FVector4f>& Texels)
	{
		double DepthSumCm = 0.0;
		for (const FVector4f& Texel : Texels)
		{
			DepthSumCm += Texel.X;
		}
		return DepthSumCm * FMath::Square(DenseExtentCm / SeedResolution);
	}

	double ComputeSnapshotVolumeCm3(const FTDDenseDepthSnapshot& Snapshot)
	{
		double DepthSumCm = 0.0;
		for (const float DepthCm : Snapshot.Depths)
		{
			DepthSumCm += DepthCm;
		}
		return DepthSumCm * FMath::Square(DenseExtentCm / FMath::Max(Snapshot.Resolution, 1));
	}

	float FindMaximumDepthCm(const FTDDenseDepthSnapshot& Snapshot)
	{
		float MaximumDepthCm = 0.0f;
		for (const float DepthCm : Snapshot.Depths)
		{
			MaximumDepthCm = FMath::Max(MaximumDepthCm, DepthCm);
		}
		return MaximumDepthCm;
	}

	int32 CountInvalidDepths(const FTDDenseDepthSnapshot& Snapshot)
	{
		int32 InvalidCount = 0;
		for (const float DepthCm : Snapshot.Depths)
		{
			InvalidCount += !FMath::IsFinite(DepthCm) || DepthCm < 0.0f ? 1 : 0;
		}
		return InvalidCount;
	}

	FTDFluidGpuActorInput MakeDenseActor(const FTDFluidGpuSimulationSettings& Settings, int32 FrameIndex, const TArray<FVector4f>& SeedTexels)
	{
		FTDFluidGpuActorInput Actor;
		Actor.ActorKey = DenseActorKey;
		Actor.Settings = Settings;
		Actor.DeltaTimeSeconds = SixtyHertzStepSeconds;
		Actor.SimulationTimeSeconds = FrameIndex * static_cast<double>(SixtyHertzStepSeconds);
		Actor.bHasObstacleListChanged = FrameIndex == 0;
		if (FrameIndex != 0)
		{
			return Actor;
		}

		const int32 ResolutionTier = FTDFluidGpuBatch::ResolveWaterResolutionTier(Settings.Resolution);
		const int32 FieldResolution = ResolutionTier / FTDFluidGpuBatch::ComputeWaterFieldDivisor(ResolutionTier);
		Actor.WaterTerrainUpdates.Reserve(FieldResolution * FieldResolution);
		for (int32 FieldY = 0; FieldY < FieldResolution; ++FieldY)
		{
			for (int32 FieldX = 0; FieldX < FieldResolution; ++FieldX)
			{
				FTDFluidGpuTerrainCellUpdate& TerrainUpdate = Actor.WaterTerrainUpdates.AddDefaulted_GetRef();
				TerrainUpdate.Cell = FIntPoint(FieldX, FieldY);
				TerrainUpdate.ElevationCm = 0.0f;
			}
		}
		Actor.DenseSeedResolution = SeedResolution;
		Actor.DenseSeedTexels = SeedTexels;
		return Actor;
	}

	void EnqueueDepthSnapshot(FRHICommandListImmediate& RHICmdList, int32 FrameCount, FTDDenseRunState& InOutRunState)
	{
		FTDFluidGpuBatch* DenseBatch = InOutRunState.Batches.FindByPredicate([](const FTDFluidGpuBatch& Batch)
		{
			return Batch.GetKey().bUsesDenseWater && !Batch.GetFrame().Parameters.IsEmpty();
		});
		if (!DenseBatch)
		{
			return;
		}

		const int32 ResolutionTier = DenseBatch->GetKey().ResolutionTier;
		InOutRunState.Result.bHasDenseBatch = true;
		InOutRunState.Result.DenseResolution = ResolutionTier / FTDFluidGpuDenseWaterRules::ResolveDenseDivisor(ResolutionTier);
		FTDDenseDepthSnapshot& Snapshot = InOutRunState.Result.Snapshots.AddDefaulted_GetRef();
		Snapshot.FrameCount = FrameCount;
		Snapshot.Resolution = InOutRunState.Result.DenseResolution;
		TUniquePtr<FRHIGPUTextureReadback>& Readback = InOutRunState.Readbacks.AddDefaulted_GetRef();
		FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuDenseWaterRhiSnapshot"));
		AddTDFluidGpuDenseWaterDepthProbePasses(GraphBuilder, *DenseBatch, 0, Readback);
		GraphBuilder.Execute();
	}

	void ReadDepthSnapshots(FTDDenseRunState& InOutRunState)
	{
		TArray<FTDDenseDepthSnapshot>& Snapshots = InOutRunState.Result.Snapshots;
		for (int32 SnapshotIndex = 0; SnapshotIndex < Snapshots.Num() && SnapshotIndex < InOutRunState.Readbacks.Num(); ++SnapshotIndex)
		{
			FRHIGPUTextureReadback* Readback = InOutRunState.Readbacks[SnapshotIndex].Get();
			FTDDenseDepthSnapshot& Snapshot = Snapshots[SnapshotIndex];
			if (!Readback || !Readback->IsReady() || Snapshot.Resolution <= 0)
			{
				continue;
			}

			const int32 Resolution = Snapshot.Resolution;
			int32 RowPitchInPixels = 0;
			int32 BufferHeight = 0;
			const float* Texels = static_cast<const float*>(Readback->Lock(RowPitchInPixels, &BufferHeight));
			if (Texels && RowPitchInPixels >= Resolution && BufferHeight >= Resolution)
			{
				Snapshot.Depths.SetNumUninitialized(Resolution * Resolution);
				for (int32 RowIndex = 0; RowIndex < Resolution; ++RowIndex)
				{
					FMemory::Memcpy(Snapshot.Depths.GetData() + static_cast<int64>(RowIndex) * Resolution, Texels + static_cast<int64>(RowIndex) * RowPitchInPixels, Resolution * sizeof(float));
				}
			}
			Readback->Unlock();
		}
	}

	FTDDenseRunResult RunDenseFrames(const FTDFluidGpuSimulationSettings& Settings, int32 FrameCount, const TArray<int32>& SnapshotFrameCounts)
	{
		TSharedRef<FTDDenseRunState, ESPMode::ThreadSafe> RunState = MakeShared<FTDDenseRunState, ESPMode::ThreadSafe>();
		const TArray<FVector4f> SeedTexels = MakeMoundSeedTexels();
		ENQUEUE_RENDER_COMMAND(TDFluidGpuDenseWaterRhiFrames)(
			[RunState, Settings, FrameCount, SnapshotFrameCounts, SeedTexels](FRHICommandListImmediate& RHICmdList)
			{
				for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
				{
					FTDFluidGpuFrameInputs FrameInputs;
					FrameInputs.FrameNumber = static_cast<uint32>(FrameIndex);
					FrameInputs.Actors.Add(MakeDenseActor(Settings, FrameIndex, SeedTexels));
					{
						FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuDenseWaterRhiFrame"));
						AddTDFluidGpuFramePasses(GraphBuilder, FrameInputs, RunState->Batches);
						GraphBuilder.Execute();
					}
					if (SnapshotFrameCounts.Contains(FrameIndex + 1))
					{
						EnqueueDepthSnapshot(RHICmdList, FrameIndex + 1, *RunState);
					}
					RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
				}

				RHICmdList.SubmitAndBlockUntilGPUIdle();
				ReadDepthSnapshots(*RunState);
				RunState->Readbacks.Empty();
				RunState->Batches.Empty();
			});
		FlushRenderingCommands();
		return RunState->Result;
	}

	bool HasReadAllSnapshots(FAutomationTestBase& Test, const FTDDenseRunResult& Result, int32 ExpectedResolution, int32 ExpectedSnapshotCount)
	{
		if (!Test.TestTrue(TEXT("A dense water batch stepped"), Result.bHasDenseBatch))
		{
			return false;
		}
		if (!Test.TestEqual(TEXT("Dense solve resolution"), Result.DenseResolution, ExpectedResolution) || !Test.TestEqual(TEXT("Dense depth snapshot count"), Result.Snapshots.Num(), ExpectedSnapshotCount))
		{
			return false;
		}

		bool bHasReadAll = true;
		for (const FTDDenseDepthSnapshot& Snapshot : Result.Snapshots)
		{
			bHasReadAll &= Test.TestEqual(FString::Printf(TEXT("Dense depth texels read at frame %d"), Snapshot.FrameCount), Snapshot.Depths.Num(), ExpectedResolution * ExpectedResolution);
		}
		return bHasReadAll;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuDenseWaterHalfResolutionConservationTest,
	"TDGame.FluidFXRhi.DenseWater.HalfResolutionConservation1024",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuDenseWaterHalfResolutionConservationTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuDenseWaterRhiTestPrivate;

	if (!CanRunDenseWaterRhiTest(*this, TEXT("DenseWater.HalfResolutionConservation1024")))
	{
		return true;
	}

	constexpr int32 FrameCount = 60;
	const FTDDenseRunResult Result = RunDenseFrames(MakeDenseSettings(1024), FrameCount, { 1, FrameCount });
	if (!HasReadAllSnapshots(*this, Result, 512, 2))
	{
		return false;
	}

	const FTDDenseDepthSnapshot& FirstSnapshot = Result.Snapshots[0];
	const FTDDenseDepthSnapshot& LastSnapshot = Result.Snapshots[1];
	const double FirstVolumeCm3 = ComputeSnapshotVolumeCm3(FirstSnapshot);
	const double LastVolumeCm3 = ComputeSnapshotVolumeCm3(LastSnapshot);
	const double VolumeChangeRatio = FirstVolumeCm3 > 0.0 ? FMath::Abs(LastVolumeCm3 - FirstVolumeCm3) / FirstVolumeCm3 : 1.0;
	const float FirstPeakCm = FindMaximumDepthCm(FirstSnapshot);
	const float LastPeakCm = FindMaximumDepthCm(LastSnapshot);
	AddInfo(FString::Printf(TEXT("Dense 1024 closed basin: volume frame 1 %.1f cm3, frame %d %.1f cm3, change %.6f, peak %.4f -> %.4f cm"), FirstVolumeCm3, FrameCount, LastVolumeCm3, VolumeChangeRatio, FirstPeakCm, LastPeakCm));
	TestEqual(TEXT("Dense depths stay finite and non negative"), CountInvalidDepths(FirstSnapshot) + CountInvalidDepths(LastSnapshot), 0);
	TestTrue(TEXT("Seeded basin holds water"), FirstVolumeCm3 > 0.0);
	TestTrue(TEXT("Closed basin volume change stays within 1 percent"), VolumeChangeRatio <= 0.01);
	TestTrue(TEXT("The mound spreads under gravity"), LastPeakCm < FirstPeakCm);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuDenseWaterSeedFromCacheGpuTest,
	"TDGame.FluidFXRhi.DenseWater.SeedFromCacheGpu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuDenseWaterSeedFromCacheGpuTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuDenseWaterRhiTestPrivate;

	if (!CanRunDenseWaterRhiTest(*this, TEXT("DenseWater.SeedFromCacheGpu")))
	{
		return true;
	}

	const FTDDenseRunResult Result = RunDenseFrames(MakeDenseSettings(256), 1, { 1 });
	if (!HasReadAllSnapshots(*this, Result, 256, 1))
	{
		return false;
	}

	const double SeedVolumeCm3 = ComputeSeedVolumeCm3(MakeMoundSeedTexels());
	const double SeededVolumeCm3 = ComputeSnapshotVolumeCm3(Result.Snapshots[0]);
	const double VolumeDifferenceRatio = SeedVolumeCm3 > 0.0 ? FMath::Abs(SeededVolumeCm3 - SeedVolumeCm3) / SeedVolumeCm3 : 1.0;
	AddInfo(FString::Printf(TEXT("Seed 128 -> dense 256: seed volume %.1f cm3, seeded volume %.1f cm3, difference %.6f"), SeedVolumeCm3, SeededVolumeCm3, VolumeDifferenceRatio));
	TestEqual(TEXT("Seeded depths stay finite and non negative"), CountInvalidDepths(Result.Snapshots[0]), 0);
	TestTrue(TEXT("Seeded dense water has volume"), SeededVolumeCm3 > 0.0);
	TestTrue(TEXT("Seeded volume matches the cache within 2 percent"), VolumeDifferenceRatio <= 0.02);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuDenseWaterMemoryEstimate2048Test,
	"TDGame.FluidFXRhi.DenseWater.MemoryEstimate2048",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuDenseWaterMemoryEstimate2048Test::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuDenseWaterRhiTestPrivate;

	FTDFluidGpuSimulationSettings Settings = MakeDenseSettings(2048);
	const FTDFluidGpuBatchKey DenseKey = FTDFluidGpuBatch::MakeKey(Settings);
	Settings.bUsesDenseWater = false;
	const FTDFluidGpuBatchKey SparseKey = FTDFluidGpuBatch::MakeKey(Settings);
	FTDFluidGpuSimulationSettings NonWaterSettings = MakeDenseSettings(2048);
	NonWaterSettings.bUsesWaterLayer = false;
	TestTrue(TEXT("Dense settings make a dense water key"), DenseKey.bUsesDenseWater);
	TestFalse(TEXT("Sparse settings keep the sparse key"), SparseKey.bUsesDenseWater);
	TestFalse(TEXT("Dense and sparse keys never share a batch"), DenseKey == SparseKey);
	TestFalse(TEXT("Dense water needs the water layer"), FTDFluidGpuBatch::MakeKey(NonWaterSettings).bUsesDenseWater);

	const FTDFluidGpuMemoryEstimate DenseEstimate = FTDFluidGpuBatch::EstimateWaterActorMemory(DenseKey);
	const FTDFluidGpuMemoryEstimate SparseEstimate = FTDFluidGpuBatch::EstimateWaterActorMemory(SparseKey);
	const int64 Resolution = DenseKey.ResolutionTier;
	const int64 DenseResolution = Resolution / FTDFluidGpuDenseWaterRules::ResolveDenseDivisor(DenseKey.ResolutionTier);
	const int64 DenseCellCount = DenseResolution * DenseResolution;
	AddInfo(FString::Printf(TEXT("Water 2048 estimate: dense %.1f MiB (state %.1f, transient %.1f), sparse %.1f MiB, dense solve %lld"), static_cast<double>(DenseEstimate.GetTotalBytes()) / BytesPerMebibyte, static_cast<double>(DenseEstimate.StateBytes) / BytesPerMebibyte, static_cast<double>(DenseEstimate.TransientBytes) / BytesPerMebibyte, static_cast<double>(SparseEstimate.GetTotalBytes()) / BytesPerMebibyte, DenseResolution));
	TestEqual(TEXT("Dense solve runs at half resolution"), DenseResolution, Resolution / 2);
	TestEqual(TEXT("Dense state adds depth and face velocity ping pong"), DenseEstimate.StateBytes - SparseEstimate.StateBytes, DenseCellCount * DenseStateBytesPerDenseCell);
	const int64 DenseOnlyTransientBytes = DenseCellCount * DenseCellVelocityBytesPerDenseCell + Resolution * Resolution * DenseSurfaceBytesPerCell;
	TestTrue(TEXT("Dense transient adds cell velocity and surface height but no pressure transients"), DenseEstimate.TransientBytes >= DenseOnlyTransientBytes && DenseEstimate.TransientBytes - DenseOnlyTransientBytes <= SparseEstimate.TransientBytes);
	TestEqual(TEXT("Dense output bytes match sparse"), DenseEstimate.OutputBytes, SparseEstimate.OutputBytes);
	TestTrue(TEXT("Dense 2048 water actor stays within 384 MiB"), DenseEstimate.GetTotalBytes() <= DenseActorBudgetBytes);
	return true;
}

#endif
