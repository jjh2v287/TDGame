#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicRHI.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHIGlobals.h"
#include "RenderGraphBuilder.h"
#include "RenderingThread.h"
#include "TDFluidGpuBatch.h"
#include "TDFluidGpuInputs.h"
#include "TDFluidGpuPassTimestamps.h"
#include "TDFluidGpuStepPasses.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidGpuWaterPerfRhiTests, Log, All);

namespace TDFluidGpuWaterPerfRhiTestPrivate
{
	constexpr int32 PerfMeasuredFrameCount = 96;
	constexpr double PerfWarmupSeconds = 2.0;
	constexpr int32 PerfBaseRunCount = 3;
	constexpr int32 PerfExtendedRunCount = 5;
	constexpr double PerfRunVariationLimit = 0.10;
	constexpr int32 PerfShiftIntervalFrames = 20;
	constexpr double PerfWaterExtentCm = 3600.0;
	constexpr double PerfNonWaterExtentCm = 3200.0;
	constexpr float PerfSixtyHertzStepSeconds = 1.0f / 60.0f;
	constexpr float PerfHundredFortyFourHertzStepSeconds = 1.0f / 144.0f;
	constexpr int32 PerfProbeCount = 8;
	constexpr uint64 PerfWaterActorKey = 5101;
	constexpr uint64 PerfNonWaterActorKey = 5201;

	using FTDWaterPerfFrameBuilder = TFunction<void(int32, FTDFluidGpuFrameInputs&)>;

	struct FTDWaterPerfSegmentSamples
	{
		FString Name;
		TArray<double> Milliseconds;
	};

	struct FTDWaterPerfPassProfile
	{
		TArray<FTDWaterPerfSegmentSamples> Segments;
		TArray<double> FrameMilliseconds;
		int32 RejectedFrameCount = 0;
	};

	struct FTDWaterPerfTimingSummary
	{
		int32 SampleCount = 0;
		double MedianMilliseconds = 0.0;
		double MeanMilliseconds = 0.0;
		double P95Milliseconds = 0.0;
	};

	struct FTDWaterPerfSegmentSummary
	{
		FString Name;
		FTDWaterPerfTimingSummary Timing;
	};

	struct FTDWaterPerfRun
	{
		FTDWaterPerfTimingSummary Unmarked;
		FTDWaterPerfTimingSummary Marked;
		int32 RejectedFrameCount = 0;
		TArray<FTDWaterPerfSegmentSummary> Segments;
	};

	struct FTDWaterPerfCase
	{
		const TCHAR* Name = TEXT("");
		int32 Resolution = 0;
		bool bUsesWaterLayer = true;
		float StepSeconds = PerfSixtyHertzStepSeconds;
		bool bIsVerdictCondition = true;
	};

	struct FTDWaterPerfCaseResult
	{
		FTDWaterPerfCase Case;
		int32 WarmupFrameCount = 0;
		double RunVariation = 0.0;
		TArray<FTDWaterPerfRun> Runs;
		TArray<int32> SelectedRunIndices;
		TArray<FString> MissingLabels;
		bool bHasOnlyFiniteSegments = true;
	};

	bool CanRunWaterPerfRhiTest(FAutomationTestBase& Test, const TCHAR* TestLabel)
	{
		const bool bHasRenderingRhi = !GUsingNullRHI && FApp::CanEverRender();
		UE_LOG(LogTDFluidGpuWaterPerfRhiTests, Display, TEXT("%s: %s (NullRHI=%d, CanEverRender=%d)"), TestLabel, bHasRenderingRhi ? TEXT("RUN") : TEXT("NOT RUNNABLE"), GUsingNullRHI ? 1 : 0, FApp::CanEverRender() ? 1 : 0);
		if (bHasRenderingRhi)
		{
			return true;
		}

		Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI but none is active, so it fails instead of passing as skipped. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."), TestLabel));
		return false;
	}

	FTDWaterPerfTimingSummary SummarizePerfTimings(TArray<double> Milliseconds)
	{
		FTDWaterPerfTimingSummary Summary;
		if (Milliseconds.IsEmpty())
		{
			return Summary;
		}

		double Sum = 0.0;
		for (const double Value : Milliseconds)
		{
			Sum += Value;
		}

		Milliseconds.Sort();
		Summary.SampleCount = Milliseconds.Num();
		Summary.MedianMilliseconds = Milliseconds[Milliseconds.Num() / 2];
		Summary.MeanMilliseconds = Sum / Milliseconds.Num();
		Summary.P95Milliseconds = Milliseconds[FMath::Clamp(FMath::CeilToInt(0.95 * Milliseconds.Num()) - 1, 0, Milliseconds.Num() - 1)];
		return Summary;
	}

	TArray<double> MeasurePerfFrameMilliseconds(int32 WarmupFrameCount, int32 MeasuredFrameCount, const FTDWaterPerfFrameBuilder& BuildFrame)
	{
		TSharedRef<TArray<double>, ESPMode::ThreadSafe> Milliseconds = MakeShared<TArray<double>, ESPMode::ThreadSafe>();
		ENQUEUE_RENDER_COMMAND(TDFluidGpuWaterPerfUnmarkedFrames)(
			[Milliseconds, BuildFrame, WarmupFrameCount, MeasuredFrameCount](FRHICommandListImmediate& RHICmdList)
			{
				TArray<FTDFluidGpuBatch> Batches;
				TArray<FRenderQueryRHIRef> StartQueries;
				TArray<FRenderQueryRHIRef> EndQueries;
				for (int32 FrameIndex = 0; FrameIndex < WarmupFrameCount + MeasuredFrameCount; ++FrameIndex)
				{
					FTDFluidGpuFrameInputs FrameInputs;
					FrameInputs.FrameNumber = static_cast<uint32>(FrameIndex);
					BuildFrame(FrameIndex, FrameInputs);
					const bool bIsMeasured = FrameIndex >= WarmupFrameCount;
					if (bIsMeasured)
					{
						RHICmdList.EndRenderQuery(StartQueries.Add_GetRef(RHICreateRenderQuery(RQT_AbsoluteTime)));
					}
					{
						FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuWaterPerfUnmarkedFrame"));
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
				for (int32 QueryIndex = 0; QueryIndex < StartQueries.Num(); ++QueryIndex)
				{
					uint64 StartMicroseconds = 0;
					uint64 EndMicroseconds = 0;
					const bool bHasStart = RHIGetRenderQueryResult(StartQueries[QueryIndex], StartMicroseconds, true);
					const bool bHasEnd = RHIGetRenderQueryResult(EndQueries[QueryIndex], EndMicroseconds, true);
					if (bHasStart && bHasEnd && EndMicroseconds >= StartMicroseconds)
					{
						Milliseconds->Add(static_cast<double>(EndMicroseconds - StartMicroseconds) / 1000.0);
					}
				}
			});
		FlushRenderingCommands();
		return *Milliseconds;
	}

	TArray<FString> MakePerfSegmentNames(const TArray<FString>& MarkerLabels)
	{
		TArray<FString> Names;
		TMap<FString, int32> OccurrenceCounts;
		for (const FString& Label : MarkerLabels)
		{
			const int32 Occurrence = ++OccurrenceCounts.FindOrAdd(Label);
			Names.Add(Occurrence == 1 ? Label : FString::Printf(TEXT("%s#%d"), *Label, Occurrence));
		}
		Names.Add(TEXT("graph_end"));
		return Names;
	}

	bool ReadPerfFrameMicroseconds(const FRenderQueryRHIRef& StartQuery, const FTDFluidGpuPassTimestamps& Timestamps, const FRenderQueryRHIRef& EndQuery, TArray<uint64>& OutMicroseconds)
	{
		OutMicroseconds.Reset();
		TArray<FRenderQueryRHIRef> Queries;
		Queries.Add(StartQuery);
		Queries.Append(Timestamps.Queries);
		Queries.Add(EndQuery);
		for (const FRenderQueryRHIRef& Query : Queries)
		{
			uint64 Microseconds = 0;
			if (!RHIGetRenderQueryResult(Query, Microseconds, true))
			{
				return false;
			}
			if (!OutMicroseconds.IsEmpty() && Microseconds < OutMicroseconds.Last())
			{
				return false;
			}
			OutMicroseconds.Add(Microseconds);
		}
		return true;
	}

	void AddPerfFrameSegments(const TArray<FString>& SegmentNames, const TArray<uint64>& Microseconds, FTDWaterPerfPassProfile& InOutProfile)
	{
		for (int32 SegmentIndex = 0; SegmentIndex < SegmentNames.Num(); ++SegmentIndex)
		{
			const FString& SegmentName = SegmentNames[SegmentIndex];
			FTDWaterPerfSegmentSamples* Segment = InOutProfile.Segments.FindByPredicate([&SegmentName](const FTDWaterPerfSegmentSamples& Candidate)
			{
				return Candidate.Name == SegmentName;
			});
			if (!Segment)
			{
				Segment = &InOutProfile.Segments.AddDefaulted_GetRef();
				Segment->Name = SegmentName;
			}
			Segment->Milliseconds.Add(static_cast<double>(Microseconds[SegmentIndex + 1] - Microseconds[SegmentIndex]) / 1000.0);
		}
		InOutProfile.FrameMilliseconds.Add(static_cast<double>(Microseconds.Last() - Microseconds[0]) / 1000.0);
	}

	FTDWaterPerfPassProfile MeasurePerfPassTimings(int32 WarmupFrameCount, int32 MeasuredFrameCount, const FTDWaterPerfFrameBuilder& BuildFrame)
	{
		TSharedRef<FTDWaterPerfPassProfile, ESPMode::ThreadSafe> Profile = MakeShared<FTDWaterPerfPassProfile, ESPMode::ThreadSafe>();
		ENQUEUE_RENDER_COMMAND(TDFluidGpuWaterPerfMarkedFrames)(
			[Profile, BuildFrame, WarmupFrameCount, MeasuredFrameCount](FRHICommandListImmediate& RHICmdList)
			{
				TArray<FTDFluidGpuBatch> Batches;
				TArray<FRenderQueryRHIRef> StartQueries;
				TArray<FRenderQueryRHIRef> EndQueries;
				TArray<FTDFluidGpuPassTimestamps> FrameTimestamps;
				FrameTimestamps.Reserve(MeasuredFrameCount);
				for (int32 FrameIndex = 0; FrameIndex < WarmupFrameCount + MeasuredFrameCount; ++FrameIndex)
				{
					FTDFluidGpuFrameInputs FrameInputs;
					FrameInputs.FrameNumber = static_cast<uint32>(FrameIndex);
					BuildFrame(FrameIndex, FrameInputs);
					if (FrameIndex < WarmupFrameCount)
					{
						FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuWaterPerfWarmupFrame"));
						AddTDFluidGpuFramePasses(GraphBuilder, FrameInputs, Batches);
						GraphBuilder.Execute();
						RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
						continue;
					}

					RHICmdList.EndRenderQuery(StartQueries.Add_GetRef(RHICreateRenderQuery(RQT_AbsoluteTime)));
					{
						FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuWaterPerfMarkedFrame"));
						AddTDFluidGpuTimestampedFramePasses(GraphBuilder, FrameInputs, Batches, FrameTimestamps.AddDefaulted_GetRef());
						GraphBuilder.Execute();
					}
					RHICmdList.EndRenderQuery(EndQueries.Add_GetRef(RHICreateRenderQuery(RQT_AbsoluteTime)));
					RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
				}

				RHICmdList.SubmitAndBlockUntilGPUIdle();
				TArray<uint64> Microseconds;
				for (int32 TimedFrameIndex = 0; TimedFrameIndex < FrameTimestamps.Num(); ++TimedFrameIndex)
				{
					const FTDFluidGpuPassTimestamps& Timestamps = FrameTimestamps[TimedFrameIndex];
					if (!ReadPerfFrameMicroseconds(StartQueries[TimedFrameIndex], Timestamps, EndQueries[TimedFrameIndex], Microseconds))
					{
						++Profile->RejectedFrameCount;
						continue;
					}

					AddPerfFrameSegments(MakePerfSegmentNames(Timestamps.Labels), Microseconds, *Profile);
				}
			});
		FlushRenderingCommands();
		return *Profile;
	}

	bool IsPerfChannelFieldRow(int32 FieldRow, int32 FieldResolution)
	{
		return FieldRow >= FieldResolution * 3 / 10 && FieldRow < FieldResolution * 7 / 10;
	}

	void AddPerfWaterFieldCell(const FIntPoint& FieldCell, int32 FieldResolution, FTDFluidGpuActorInput& InOutActor)
	{
		const bool bIsChannelRow = IsPerfChannelFieldRow(FieldCell.Y, FieldResolution);
		FTDFluidGpuWaterGuideCellUpdate& GuideUpdate = InOutActor.WaterGuideUpdates.AddDefaulted_GetRef();
		GuideUpdate.FieldCell = FieldCell;
		GuideUpdate.SurfaceHeightCm = bIsChannelRow ? 120.0f : TDFluidGpuSolverConstants::WaterNoSurfaceHeightCm;
		GuideUpdate.TargetVelocityCmPerSec = bIsChannelRow ? FVector2f(150.0f, 0.0f) : FVector2f::ZeroVector;
		GuideUpdate.GuideWeight = bIsChannelRow ? 1.0f : 0.0f;
		GuideUpdate.GuideDescent = bIsChannelRow ? 0.02f : 0.0f;

		FTDFluidGpuTerrainCellUpdate& TerrainUpdate = InOutActor.WaterTerrainUpdates.AddDefaulted_GetRef();
		TerrainUpdate.Cell = FieldCell;
		TerrainUpdate.ElevationCm = bIsChannelRow ? 0.0f : 200.0f;
	}

	FTDFluidGpuSimulationSettings MakePerfWaterSettings(int32 Resolution)
	{
		FTDFluidGpuSimulationSettings Settings;
		Settings.SimMode = ETDFluidGpuSimMode::FluidSim;
		Settings.Resolution = Resolution;
		Settings.ExtentCm = FVector2D(PerfWaterExtentCm, PerfWaterExtentCm);
		Settings.PressureIterations = 12;
		Settings.VorticityStrength = 0.3f;
		Settings.FlowNoiseAccelerationCmPerSec2 = 150.0f;
		Settings.bUsesWaterLayer = true;
		Settings.bOpenWaterBorders = true;
		Settings.bTreatDryCellsAsSolid = true;
		Settings.RippleWaveSpeedCmPerSec = 120.0f;
		Settings.WhitewaterFromVorticity = 0.3f;
		Settings.RapidsWhitewaterPerSec = 2.0f;
		FTDFluidGpuBatch::ApplyWaterConsoleOverrides(Settings);
		return Settings;
	}

	FTDFluidGpuActorInput MakePerfWaterActor(const FTDFluidGpuSimulationSettings& Settings, float StepSeconds, int32 FrameIndex)
	{
		const int32 ResolutionTier = FTDFluidGpuBatch::ResolveWaterResolutionTier(Settings.Resolution);
		const int32 FieldDivisor = FTDFluidGpuBatch::ComputeWaterFieldDivisor(ResolutionTier);
		const int32 FieldResolution = ResolutionTier / FieldDivisor;
		const FVector2D CellSize = Settings.ExtentCm / static_cast<double>(ResolutionTier);
		const int32 ShiftCount = FrameIndex / PerfShiftIntervalFrames;
		const bool bShiftsThisFrame = FrameIndex > 0 && FrameIndex % PerfShiftIntervalFrames == 0;

		FTDFluidGpuActorInput Actor;
		Actor.ActorKey = PerfWaterActorKey;
		Actor.Settings = Settings;
		Actor.DeltaTimeSeconds = StepSeconds;
		Actor.SimulationTimeSeconds = FrameIndex * static_cast<double>(StepSeconds);
		Actor.WindowCenterPlaneCm = FVector2D(ShiftCount * FieldDivisor * CellSize.X, 0.0);
		Actor.ShiftCells = bShiftsThisFrame ? FIntPoint(FieldDivisor, 0) : FIntPoint::ZeroValue;
		Actor.WaterFieldOrigin = FIntPoint(ShiftCount % FieldResolution, 0);
		Actor.bHasObstacleListChanged = FrameIndex == 0;

		FTDFluidGpuObstacleInput& Rock = Actor.Obstacles.AddDefaulted_GetRef();
		Rock.CenterPlaneCm = FVector2D(600.0, 0.0);
		Rock.RadiusCm = 180.0f;

		if (FrameIndex == 0)
		{
			for (int32 FieldY = 0; FieldY < FieldResolution; ++FieldY)
			{
				for (int32 FieldX = 0; FieldX < FieldResolution; ++FieldX)
				{
					AddPerfWaterFieldCell(FIntPoint(FieldX, FieldY), FieldResolution, Actor);
				}
			}
		}
		else if (bShiftsThisFrame)
		{
			for (int32 FieldY = 0; FieldY < FieldResolution; ++FieldY)
			{
				AddPerfWaterFieldCell(FIntPoint(FieldResolution - 1, FieldY), FieldResolution, Actor);
			}
		}

		const double StampPhase = FrameIndex * 0.07;
		FTDFluidGpuStampInput& Stamp = Actor.Stamps.AddDefaulted_GetRef();
		Stamp.CenterPlaneCm = Actor.WindowCenterPlaneCm + FVector2D(400.0 * FMath::Cos(StampPhase), 150.0 * FMath::Sin(StampPhase));
		Stamp.RadiusCm = 3.0f * static_cast<float>(CellSize.X);
		Stamp.DensityStrength = 0.6f;
		Stamp.VelocityStrength = 0.0f;
		Stamp.RippleImpulseCm = 0.5f;

		const int32 ProbeRow = ResolutionTier / 2;
		for (int32 ProbeIndex = 0; ProbeIndex < PerfProbeCount; ++ProbeIndex)
		{
			Actor.WaterProbeCells.Add(FIntPoint((ProbeIndex + 1) * ResolutionTier / (PerfProbeCount + 1), ProbeRow));
		}
		Actor.WaterProbeFirstSlot = 0;
		return Actor;
	}

	FTDFluidGpuActorInput MakePerfNonWaterActor(int32 Resolution, float StepSeconds, int32 FrameIndex)
	{
		FTDFluidGpuActorInput Actor;
		Actor.ActorKey = PerfNonWaterActorKey;
		Actor.Settings.Resolution = Resolution;
		Actor.Settings.ExtentCm = FVector2D(PerfNonWaterExtentCm, PerfNonWaterExtentCm);
		Actor.Settings.PressureIterations = 8;
		Actor.Settings.VorticityStrength = 1.5f;
		Actor.Settings.DensityDissipation = 0.2f;
		Actor.Settings.FlowNoiseAccelerationCmPerSec2 = 250.0f;
		Actor.DeltaTimeSeconds = StepSeconds;
		Actor.SimulationTimeSeconds = FrameIndex * static_cast<double>(StepSeconds);
		Actor.PlaneUpDirection = FVector2D(0.0, 1.0);

		const FVector2D CellSize = Actor.Settings.ExtentCm / static_cast<double>(Resolution);
		const int32 ShiftCount = FrameIndex / PerfShiftIntervalFrames;
		const bool bShiftsThisFrame = FrameIndex > 0 && FrameIndex % PerfShiftIntervalFrames == 0;
		const FIntPoint ShiftPerInterval(2, -1);
		Actor.WindowCenterPlaneCm = FVector2D(ShiftPerInterval.X * ShiftCount * CellSize.X, ShiftPerInterval.Y * ShiftCount * CellSize.Y);
		Actor.ShiftCells = bShiftsThisFrame ? ShiftPerInterval : FIntPoint::ZeroValue;
		Actor.bHasObstacleListChanged = FrameIndex == 0;
		FTDFluidGpuObstacleInput& Circle = Actor.Obstacles.AddDefaulted_GetRef();
		Circle.CenterPlaneCm = FVector2D(300.0, -200.0);
		Circle.RadiusCm = 250.0f;

		const double StampPhase = FrameIndex * 0.05;
		FTDFluidGpuStampInput& Stamp = Actor.Stamps.AddDefaulted_GetRef();
		Stamp.CenterPlaneCm = Actor.WindowCenterPlaneCm + FVector2D(600.0 * FMath::Cos(StampPhase), 600.0 * FMath::Sin(StampPhase));
		Stamp.VelocityPlaneCmPerSec = FVector2D(-300.0 * FMath::Sin(StampPhase), 300.0 * FMath::Cos(StampPhase));
		Stamp.RadiusCm = 300.0f;
		Stamp.DensityStrength = 0.8f;
		Stamp.VelocityStrength = 1.0f;
		return Actor;
	}

	FTDWaterPerfFrameBuilder MakePerfFrameBuilder(const FTDWaterPerfCase& Case)
	{
		if (!Case.bUsesWaterLayer)
		{
			const int32 Resolution = Case.Resolution;
			const float StepSeconds = Case.StepSeconds;
			return [Resolution, StepSeconds](int32 FrameIndex, FTDFluidGpuFrameInputs& OutFrame)
			{
				OutFrame.Actors.Add(MakePerfNonWaterActor(Resolution, StepSeconds, FrameIndex));
			};
		}

		const FTDFluidGpuSimulationSettings Settings = MakePerfWaterSettings(Case.Resolution);
		const float StepSeconds = Case.StepSeconds;
		return [Settings, StepSeconds](int32 FrameIndex, FTDFluidGpuFrameInputs& OutFrame)
		{
			OutFrame.Actors.Add(MakePerfWaterActor(Settings, StepSeconds, FrameIndex));
		};
	}

	FTDWaterPerfRun MeasurePerfRun(int32 WarmupFrameCount, const FTDWaterPerfFrameBuilder& BuildFrame)
	{
		FTDWaterPerfRun Run;
		Run.Unmarked = SummarizePerfTimings(MeasurePerfFrameMilliseconds(WarmupFrameCount, PerfMeasuredFrameCount, BuildFrame));
		const FTDWaterPerfPassProfile Profile = MeasurePerfPassTimings(WarmupFrameCount, PerfMeasuredFrameCount, BuildFrame);
		Run.Marked = SummarizePerfTimings(Profile.FrameMilliseconds);
		Run.RejectedFrameCount = Profile.RejectedFrameCount;
		for (const FTDWaterPerfSegmentSamples& Segment : Profile.Segments)
		{
			FTDWaterPerfSegmentSummary& Summary = Run.Segments.AddDefaulted_GetRef();
			Summary.Name = Segment.Name;
			Summary.Timing = SummarizePerfTimings(Segment.Milliseconds);
		}
		return Run;
	}

	double ComputePerfRunVariation(const TArray<FTDWaterPerfRun>& Runs)
	{
		TArray<double> Medians;
		for (const FTDWaterPerfRun& Run : Runs)
		{
			Medians.Add(Run.Unmarked.MedianMilliseconds);
		}
		if (Medians.IsEmpty())
		{
			return 0.0;
		}

		Medians.Sort();
		const double MiddleMedian = Medians[Medians.Num() / 2];
		return MiddleMedian > 0.0 ? (Medians.Last() - Medians[0]) / MiddleMedian : 0.0;
	}

	TArray<int32> SelectMiddlePerfRuns(const TArray<FTDWaterPerfRun>& Runs)
	{
		TArray<int32> RunOrder;
		for (int32 RunIndex = 0; RunIndex < Runs.Num(); ++RunIndex)
		{
			RunOrder.Add(RunIndex);
		}
		RunOrder.Sort([&Runs](int32 Left, int32 Right)
		{
			return Runs[Left].Unmarked.MedianMilliseconds < Runs[Right].Unmarked.MedianMilliseconds;
		});

		const int32 DroppedEachSide = (RunOrder.Num() - PerfBaseRunCount) / 2;
		TArray<int32> Selected;
		for (int32 OrderIndex = DroppedEachSide; OrderIndex < RunOrder.Num() - DroppedEachSide; ++OrderIndex)
		{
			Selected.Add(RunOrder[OrderIndex]);
		}
		Selected.Sort();
		return Selected;
	}

	bool IsFiniteNonNegative(double Value)
	{
		return FMath::IsFinite(Value) && Value >= 0.0;
	}

	TArray<FString> MakeRequiredPerfLabels(const FTDWaterPerfCase& Case)
	{
		TArray<FString> Labels = { TEXT("graph_begin"), TEXT("frame_resources"), TEXT("readback_downsample"), TEXT("graph_end") };
		if (!Case.bUsesWaterLayer)
		{
			Labels.Append({ TEXT("vorticity"), TEXT("subtract_gradient"), TEXT("pressure_mean") });
			return Labels;
		}

		Labels.Append({ TEXT("water_shift"), TEXT("water_fields"), TEXT("water_field_dry"), TEXT("water_compose"), TEXT("water_stamps"), TEXT("water_forces"), TEXT("vorticity"), TEXT("water_pressure1"), TEXT("subtract_gradient"), TEXT("water_advect"), TEXT("water_pressure2"), TEXT("pressure_mean"), TEXT("water_ripple"), TEXT("water_finalize"), TEXT("water_probe") });
		if (FTDFluidGpuBatch::ResolveWaterResolutionTier(Case.Resolution) >= TDFluidGpuSolverConstants::WaterHalfPressureMinimumResolution)
		{
			Labels.Append({ TEXT("mg_down_L1"), TEXT("mg_coarse"), TEXT("mg_up_L1") });
		}
		return Labels;
	}

	FTDWaterPerfCaseResult MeasurePerfCase(const FTDWaterPerfCase& Case)
	{
		FTDWaterPerfCaseResult Result;
		Result.Case = Case;
		Result.WarmupFrameCount = FMath::CeilToInt(PerfWarmupSeconds / Case.StepSeconds);
		const FTDWaterPerfFrameBuilder BuildFrame = MakePerfFrameBuilder(Case);
		for (int32 RunIndex = 0; RunIndex < PerfBaseRunCount; ++RunIndex)
		{
			Result.Runs.Add(MeasurePerfRun(Result.WarmupFrameCount, BuildFrame));
		}

		Result.RunVariation = ComputePerfRunVariation(Result.Runs);
		if (Result.RunVariation > PerfRunVariationLimit)
		{
			while (Result.Runs.Num() < PerfExtendedRunCount)
			{
				Result.Runs.Add(MeasurePerfRun(Result.WarmupFrameCount, BuildFrame));
			}
		}
		Result.SelectedRunIndices = SelectMiddlePerfRuns(Result.Runs);

		for (const FString& Label : MakeRequiredPerfLabels(Case))
		{
			for (const FTDWaterPerfRun& Run : Result.Runs)
			{
				const bool bHasLabel = Run.Segments.ContainsByPredicate([&Label](const FTDWaterPerfSegmentSummary& Segment)
				{
					return Segment.Name == Label;
				});
				if (!bHasLabel)
				{
					Result.MissingLabels.AddUnique(Label);
				}
			}
		}

		for (const FTDWaterPerfRun& Run : Result.Runs)
		{
			Result.bHasOnlyFiniteSegments &= IsFiniteNonNegative(Run.Unmarked.MedianMilliseconds) && IsFiniteNonNegative(Run.Marked.MedianMilliseconds);
			for (const FTDWaterPerfSegmentSummary& Segment : Run.Segments)
			{
				Result.bHasOnlyFiniteSegments &= IsFiniteNonNegative(Segment.Timing.MedianMilliseconds) && IsFiniteNonNegative(Segment.Timing.MeanMilliseconds) && IsFiniteNonNegative(Segment.Timing.P95Milliseconds);
			}
		}
		return Result;
	}

	FString EscapePerfJsonText(const FString& Text)
	{
		return Text.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
	}

	FString FormatPerfTimingJson(const FTDWaterPerfTimingSummary& Timing)
	{
		return FString::Printf(TEXT("{\"samples\":%d,\"median_ms\":%.5f,\"mean_ms\":%.5f,\"p95_ms\":%.5f}"), Timing.SampleCount, Timing.MedianMilliseconds, Timing.MeanMilliseconds, Timing.P95Milliseconds);
	}

	double ComputeSelectedSegmentMedian(const FTDWaterPerfCaseResult& Result, const FString& SegmentName)
	{
		TArray<double> Medians;
		for (const int32 RunIndex : Result.SelectedRunIndices)
		{
			const FTDWaterPerfSegmentSummary* Segment = Result.Runs[RunIndex].Segments.FindByPredicate([&SegmentName](const FTDWaterPerfSegmentSummary& Candidate)
			{
				return Candidate.Name == SegmentName;
			});
			if (Segment)
			{
				Medians.Add(Segment->Timing.MedianMilliseconds);
			}
		}
		return SummarizePerfTimings(Medians).MedianMilliseconds;
	}

	FString FormatPerfCaseJson(const FTDWaterPerfCaseResult& Result)
	{
		FString RunsJson;
		for (int32 RunIndex = 0; RunIndex < Result.Runs.Num(); ++RunIndex)
		{
			const FTDWaterPerfRun& Run = Result.Runs[RunIndex];
			FString SegmentsJson;
			for (const FTDWaterPerfSegmentSummary& Segment : Run.Segments)
			{
				SegmentsJson += FString::Printf(TEXT("%s{\"name\":\"%s\",\"timing\":%s}"), SegmentsJson.IsEmpty() ? TEXT("") : TEXT(","), *EscapePerfJsonText(Segment.Name), *FormatPerfTimingJson(Segment.Timing));
			}
			RunsJson += FString::Printf(TEXT("%s{\"unmarked\":%s,\"marked\":%s,\"rejected_frames\":%d,\"segments\":[%s]}"), RunIndex == 0 ? TEXT("") : TEXT(","), *FormatPerfTimingJson(Run.Unmarked), *FormatPerfTimingJson(Run.Marked), Run.RejectedFrameCount, *SegmentsJson);
		}

		FString SelectedJson;
		for (const int32 RunIndex : Result.SelectedRunIndices)
		{
			SelectedJson += FString::Printf(TEXT("%s%d"), SelectedJson.IsEmpty() ? TEXT("") : TEXT(","), RunIndex);
		}

		FString SelectedSegmentsJson;
		if (!Result.Runs.IsEmpty())
		{
			for (const FTDWaterPerfSegmentSummary& Segment : Result.Runs[0].Segments)
			{
				SelectedSegmentsJson += FString::Printf(TEXT("%s{\"name\":\"%s\",\"median_ms\":%.5f}"), SelectedSegmentsJson.IsEmpty() ? TEXT("") : TEXT(","), *EscapePerfJsonText(Segment.Name), ComputeSelectedSegmentMedian(Result, Segment.Name));
			}
		}

		FString MissingJson;
		for (const FString& Label : Result.MissingLabels)
		{
			MissingJson += FString::Printf(TEXT("%s\"%s\""), MissingJson.IsEmpty() ? TEXT("") : TEXT(","), *EscapePerfJsonText(Label));
		}

		return FString::Printf(TEXT("{\"name\":\"%s\",\"resolution\":%d,\"water\":%s,\"step_seconds\":%.8f,\"verdict_condition\":%s,\"warmup_frames\":%d,\"measured_frames\":%d,\"run_variation\":%.5f,\"selected_runs\":[%s],\"missing_labels\":[%s],\"selected_segment_medians\":[%s],\"runs\":[%s]}"),
			*EscapePerfJsonText(Result.Case.Name),
			Result.Case.Resolution,
			Result.Case.bUsesWaterLayer ? TEXT("true") : TEXT("false"),
			Result.Case.StepSeconds,
			Result.Case.bIsVerdictCondition ? TEXT("true") : TEXT("false"),
			Result.WarmupFrameCount,
			PerfMeasuredFrameCount,
			Result.RunVariation,
			*SelectedJson,
			*MissingJson,
			*SelectedSegmentsJson,
			*RunsJson);
	}

	bool SavePerfTimingJson(const TArray<FTDWaterPerfCaseResult>& Results, FString& OutPath)
	{
		FString CasesJson;
		for (const FTDWaterPerfCaseResult& Result : Results)
		{
			CasesJson += FString::Printf(TEXT("%s%s"), CasesJson.IsEmpty() ? TEXT("") : TEXT(",\n"), *FormatPerfCaseJson(Result));
		}

		const FString Json = FString::Printf(TEXT("{\"test\":\"TDGame.FluidFXRhi.PerfWater.PassTimingProfile\",\"generated_utc\":\"%s\",\"adapter\":\"%s\",\"rhi\":\"%s\",\"measured_frames\":%d,\"warmup_seconds\":%.2f,\"cases\":[\n%s\n]}\n"),
			*FDateTime::UtcNow().ToIso8601(),
			*EscapePerfJsonText(GRHIAdapterName),
			*EscapePerfJsonText(FString(GDynamicRHI ? GDynamicRHI->GetName() : TEXT("none"))),
			PerfMeasuredFrameCount,
			PerfWarmupSeconds,
			*CasesJson);
		const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FluidFX"));
		IFileManager::Get().MakeDirectory(*Directory, true);
		OutPath = FPaths::Combine(Directory, TEXT("fx4b-pass-timing.json"));
		return FFileHelper::SaveStringToFile(Json, *OutPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterPerfPassTimingProfileTest,
	"TDGame.FluidFXRhi.PerfWater.PassTimingProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterPerfPassTimingProfileTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterPerfRhiTestPrivate;

	if (!CanRunWaterPerfRhiTest(*this, TEXT("PerfWater.PassTimingProfile")))
	{
		return true;
	}

	const FTDWaterPerfCase Cases[] = {
		{ TEXT("water192_60hz"), 192, true, PerfSixtyHertzStepSeconds, true },
		{ TEXT("water512_60hz"), 512, true, PerfSixtyHertzStepSeconds, true },
		{ TEXT("water1024_60hz"), 1024, true, PerfSixtyHertzStepSeconds, true },
		{ TEXT("water192_144hz"), 192, true, PerfHundredFortyFourHertzStepSeconds, false },
		{ TEXT("water512_144hz"), 512, true, PerfHundredFortyFourHertzStepSeconds, false },
		{ TEXT("water1024_144hz"), 1024, true, PerfHundredFortyFourHertzStepSeconds, false },
		{ TEXT("nonwater512_60hz"), 512, false, PerfSixtyHertzStepSeconds, false },
	};

	TArray<FTDWaterPerfCaseResult> Results;
	for (const FTDWaterPerfCase& Case : Cases)
	{
		const FTDWaterPerfCaseResult& Result = Results.Add_GetRef(MeasurePerfCase(Case));
		FString LabelSummary;
		if (!Result.Runs.IsEmpty())
		{
			for (const FTDWaterPerfSegmentSummary& Segment : Result.Runs[0].Segments)
			{
				LabelSummary += FString::Printf(TEXT(" %s=%.4f"), *Segment.Name, ComputeSelectedSegmentMedian(Result, Segment.Name));
			}
		}

		const FString CaseLine = FString::Printf(TEXT("TDFluidGpuWaterPassProfile: case=%s resolution=%d water=%d step_seconds=%.6f verdict_condition=%d warmup_frames=%d runs=%d variation=%.4f missing_labels=%d finite=%d segment_median_ms:%s"),
			Case.Name,
			Case.Resolution,
			Case.bUsesWaterLayer ? 1 : 0,
			Case.StepSeconds,
			Case.bIsVerdictCondition ? 1 : 0,
			Result.WarmupFrameCount,
			Result.Runs.Num(),
			Result.RunVariation,
			Result.MissingLabels.Num(),
			Result.bHasOnlyFiniteSegments ? 1 : 0,
			*LabelSummary);
		UE_LOG(LogTDFluidGpuWaterPerfRhiTests, Display, TEXT("%s"), *CaseLine);
		AddInfo(CaseLine);

		TestTrue(*FString::Printf(TEXT("%s measured at least three runs"), Case.Name), Result.Runs.Num() >= PerfBaseRunCount);
		TestEqual(*FString::Printf(TEXT("%s selected three runs"), Case.Name), Result.SelectedRunIndices.Num(), PerfBaseRunCount);
		TestTrue(*FString::Printf(TEXT("%s recorded every expected pass label (missing: %s)"), Case.Name, *FString::Join(Result.MissingLabels, TEXT(","))), Result.MissingLabels.IsEmpty());
		TestTrue(*FString::Printf(TEXT("%s recorded only finite non-negative pass milliseconds"), Case.Name), Result.bHasOnlyFiniteSegments);
		for (const FTDWaterPerfRun& Run : Result.Runs)
		{
			TestEqual(*FString::Printf(TEXT("%s read every unmarked frame timestamp pair"), Case.Name), Run.Unmarked.SampleCount, PerfMeasuredFrameCount);
			TestEqual(*FString::Printf(TEXT("%s read every marked frame timestamp"), Case.Name), Run.Marked.SampleCount, PerfMeasuredFrameCount);
		}
	}

	FString JsonPath;
	const bool bHasSavedJson = SavePerfTimingJson(Results, JsonPath);
	AddInfo(FString::Printf(TEXT("TDFluidGpuWaterPassProfile: json=%s saved=%d"), *JsonPath, bHasSavedJson ? 1 : 0));
	TestTrue(*FString::Printf(TEXT("Pass timing JSON was written to %s"), *JsonPath), bHasSavedJson);
	return !HasAnyErrors();
}

#endif
