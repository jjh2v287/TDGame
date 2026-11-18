#include "FluidFX/TDFog3DLayer.h"

#include "Engine/TextureRenderTargetVolume.h"
#include "RHIGPUReadback.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "TextureResource.h"

struct FTDFog3DGroundReadbackJob
{
	TArray<FIntPoint> Cells;
	float MissingHeightCm = 0.0f;
	TUniquePtr<FRHIGPUTextureReadback> Readback;
	FIntPoint GroundMinCell = FIntPoint::ZeroValue;
	int32 GroundSize = 0;
	TArray<FTDFog3DGroundReadbackSample> Samples;
	std::atomic<bool> bIsFinished{false};
};

namespace
{
	constexpr int32 Fog3DWindowStepCells = 16;
	constexpr int32 Fog3DWindowRoundingCells = 8;
	constexpr double Fog3DClockToleranceRatio = 1.0e-3;
	constexpr double Fog3DMaximumStepCreditIntervals = 2.0;
	constexpr double Fog3DUprightTolerance = 1.0e-4;
	constexpr double Fog3DBladeCountEpsilon = 1.0e-9;
	constexpr int32 Fog3DQualityLevelMaximum = 2;
	constexpr int32 Fog3DModeMaximum = 2;

	struct FFog3DObstacleCandidate
	{
		FTDFog3DObstacleKey Key;
		double HorizontalDistanceSquared = 0.0;
	};

	int32 DivideRoundingTowardNegativeInfinity(int32 Dividend, int32 Divisor)
	{
		const int32 Quotient = Dividend / Divisor;
		return (Dividend % Divisor < 0) ? Quotient - 1 : Quotient;
	}

	int32 SnapWindowAxis(int32 PlayerCell, int32 WindowCells)
	{
		return DivideRoundingTowardNegativeInfinity(PlayerCell - WindowCells / 2 + Fog3DWindowRoundingCells, Fog3DWindowStepCells) * Fog3DWindowStepCells;
	}

	int32 FollowWindowAxis(int32 CurrentMin, int32 PlayerCell, int32 WindowCells, int32 MarginCells)
	{
		const int32 CenterCell = CurrentMin + WindowCells / 2;
		if (FMath::Abs(PlayerCell - CenterCell) <= MarginCells)
		{
			return CurrentMin;
		}
		return SnapWindowAxis(PlayerCell, WindowCells);
	}

	void PackGroundStep(const FTDFog3DGroundStep& Ground, const FTDFog3DSettings& Settings, FTDFluidGpuFog3DFrameInput& OutFrameInput)
	{
		OutFrameInput.bFollowsGround = Ground.bFollowsGround;
		OutFrameInput.GroundWallStepCm = Settings.GroundWallStepCm;
		OutFrameInput.bGroundChanged = Ground.bFollowsGround && Ground.bGroundChanged;
		if (!OutFrameInput.bGroundChanged)
		{
			return;
		}

		FTDFluidGpuFog3DGroundCapture& Capture = OutFrameInput.GroundCapture;
		Capture.DepthTarget = Ground.CaptureDepthTarget;
		Capture.MinCell = Ground.CaptureRect.MinCell;
		Capture.Size = Ground.CaptureRect.Size;
		Capture.DecodeOffsetCm = Ground.DecodeOffsetCm;
		Capture.MissingHeightCm = Ground.MissingHeightCm;
		Capture.MissingBelowFloorCm = Settings.GroundMissingBelowFloorCm;
		OutFrameInput.GroundMaterialTarget = Ground.MaterialTarget;
	}

	void FinishGroundReadbackWithoutGround(FTDFog3DGroundReadbackJob& Job)
	{
		Job.Samples.SetNum(Job.Cells.Num());
		Job.bIsFinished.store(true);
	}

	void StartGroundReadback(FRHICommandListImmediate& RHICmdList, const FTDFluidGpuFog3DState& State, FTDFog3DGroundReadbackJob& Job)
	{
		if (!State.bHasGround || !State.GroundHeightTarget.IsValid() || State.GroundSize <= 0)
		{
			FinishGroundReadbackWithoutGround(Job);
			return;
		}
		Job.GroundMinCell = State.GroundMinCell;
		Job.GroundSize = State.GroundSize;
		Job.Readback = MakeUnique<FRHIGPUTextureReadback>(TEXT("TDFog3D.GroundReadback"));
		FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFog3DGroundReadback"));
		FRDGTextureRef GroundHeight = GraphBuilder.RegisterExternalTexture(State.GroundHeightTarget);
		AddEnqueueCopyPass(GraphBuilder, Job.Readback.Get(), GroundHeight);
		GraphBuilder.SetTextureAccessFinal(GroundHeight, ERHIAccess::SRVMask);
		GraphBuilder.Execute();
	}

	FTDFog3DGroundReadbackSample ReadGroundReadbackSample(const float* Heights, int32 RowPitchInPixels, const FTDFog3DGroundReadbackJob& Job, const FIntPoint& Cell)
	{
		FTDFog3DGroundReadbackSample Sample;
		const int64 TexelX = static_cast<int64>(Cell.X) - Job.GroundMinCell.X;
		const int64 TexelY = static_cast<int64>(Cell.Y) - Job.GroundMinCell.Y;
		if (TexelX < 0 || TexelY < 0 || TexelX >= Job.GroundSize || TexelY >= Job.GroundSize)
		{
			return Sample;
		}
		const float HeightCm = Heights[TexelY * RowPitchInPixels + TexelX];
		if (!FMath::IsFinite(HeightCm) || HeightCm == Job.MissingHeightCm)
		{
			return Sample;
		}
		Sample.HeightCm = HeightCm;
		Sample.bIsValid = true;
		return Sample;
	}

	void PollGroundReadback(FTDFog3DGroundReadbackJob& Job)
	{
		if (Job.bIsFinished.load() || !Job.Readback.IsValid() || !Job.Readback->IsReady())
		{
			return;
		}
		int32 RowPitchInPixels = 0;
		int32 BufferHeight = 0;
		const float* Heights = static_cast<const float*>(Job.Readback->Lock(RowPitchInPixels, &BufferHeight));
		const bool bHasRows = Heights && RowPitchInPixels >= Job.GroundSize && BufferHeight >= Job.GroundSize;
		Job.Samples.SetNum(Job.Cells.Num());
		for (int32 SampleIndex = 0; bHasRows && SampleIndex < Job.Cells.Num(); ++SampleIndex)
		{
			Job.Samples[SampleIndex] = ReadGroundReadbackSample(Heights, RowPitchInPixels, Job, Job.Cells[SampleIndex]);
		}
		Job.Readback->Unlock();
		Job.Readback.Reset();
		Job.bIsFinished.store(true);
	}
}

FTDFog3DLayer::FTDFog3DLayer() = default;

FTDFog3DLayer::~FTDFog3DLayer()
{
	Release();
}

void FTDFog3DLayer::Configure(const FTDFog3DSettings& InSettings, int32 InQualityLevel, int32 InMode)
{
	const int32 ResolvedLevel = FMath::Clamp(InQualityLevel, 0, Fog3DQualityLevelMaximum);
	const int32 ResolvedMode = FMath::Clamp(InMode, 0, Fog3DModeMaximum);
	const FTDFog3DQualityTier Tier = ResolveTDFog3DQualityTier(InSettings, ResolvedLevel);
	const bool bChangesConfiguration = Tier.Resolution != Resolution || ResolvedLevel != QualityLevel || ResolvedMode != Mode;

	Settings = InSettings;
	Resolution = Tier.Resolution;
	StepsPerSecond = Tier.StepsPerSecond;
	QualityLevel = ResolvedLevel;
	Mode = ResolvedMode;
	if (bChangesConfiguration)
	{
		bConfigurationChangePending = true;
	}
}

void FTDFog3DLayer::RequestReset(ETDFog3DResetCause Cause)
{
	if (Cause == ETDFog3DResetCause::VolumetricFogReenabled)
	{
		bVolumetricFogReenablePending = true;
		return;
	}
	bZoneReentryPending = true;
}

FTDFog3DClockDecision FTDFog3DLayer::AdvanceClock(float FrameDeltaSeconds)
{
	if (!FMath::IsFinite(FrameDeltaSeconds) || FrameDeltaSeconds <= 0.0f)
	{
		return FTDFog3DClockDecision();
	}

	const double FrameSeconds = static_cast<double>(FrameDeltaSeconds);
	SecondsSinceSubmittedStep += FrameSeconds;
	StepCreditSeconds += FrameSeconds;
	if (StepsPerSecond > 0.0f)
	{
		StepCreditSeconds = FMath::Min(StepCreditSeconds, Fog3DMaximumStepCreditIntervals / static_cast<double>(StepsPerSecond));
	}
	return DecideClock(StepCreditSeconds, SecondsSinceSubmittedStep, StepsPerSecond, Settings.MaximumStepSeconds);
}

bool FTDFog3DLayer::SubmitStep(const FTDFog3DStepRequest& Request, UTextureRenderTargetVolume& OutputTarget)
{
	if (Mode < 1 || Resolution <= 0 || !Request.Clock.bStepsNow)
	{
		return false;
	}

	const float StepDeltaSeconds = Request.Clock.StepDeltaSeconds;
	if (!FMath::IsFinite(StepDeltaSeconds) || StepDeltaSeconds <= 0.0f || Request.PlayerWorld.ContainsNaN() || !FMath::IsFinite(Request.FloorZ))
	{
		return false;
	}

	if (!IsUsableOutputTarget(OutputTarget, Resolution))
	{
		return false;
	}

	FTextureRenderTargetResource* OutputResource = OutputTarget.GameThread_GetRenderTargetResource();
	if (!OutputResource)
	{
		return false;
	}

	FPackedStep Packed = PackAndCommitStep(Request);
	Packed.FrameInput.OutputTarget = OutputResource;

	ENQUEUE_RENDER_COMMAND(TDFog3DStep)([Slot = RenderSlot, FrameInput = MoveTemp(Packed.FrameInput)](FRHICommandListImmediate& RHICmdList)
	{
		FRDGBuilder GraphBuilder(RHICmdList);
		if (!AddTDFluidGpuFog3DPasses(GraphBuilder, Slot->State, FrameInput))
		{
			Slot->RejectedStepCount.fetch_add(1);
		}
		GraphBuilder.Execute();
	});
	return true;
}

void FTDFog3DLayer::ForgetInputHistory()
{
	PreviousPositionsBySlot.Reset();
}

void FTDFog3DLayer::Release()
{
	PreviousPositionsBySlot.Reset();
	PreviousObstacleKeys.Reset();
	bHasSubmittedWindow = false;
	SubmittedWindowMinCell = FIntPoint::ZeroValue;
	bZoneReentryPending = false;
	bVolumetricFogReenablePending = false;
	bConfigurationChangePending = false;
	SecondsSinceSubmittedStep = 0.0;
	StepCreditSeconds = 0.0;
	SubmittedStepCount = 0;
	SeenRejectedStepCount = 0;
	LastDroppedSphereCount = 0;
	LastSkippedObstacleCount = 0;
	if (GroundReadbackJob.IsValid())
	{
		ENQUEUE_RENDER_COMMAND(TDFog3DGroundReadbackRelease)([Job = MoveTemp(GroundReadbackJob)](FRHICommandListImmediate&) {});
		GroundReadbackJob.Reset();
	}
	if (!RenderSlot.IsValid())
	{
		return;
	}

	ENQUEUE_RENDER_COMMAND(TDFog3DRelease)([Slot = MoveTemp(RenderSlot)](FRHICommandListImmediate&)
	{
		Slot->State.Release();
	});
	RenderSlot.Reset();
}

bool FTDFog3DLayer::RequestGroundReadback(const TArray<FIntPoint>& Cells, float MissingHeightCm)
{
	if (!RenderSlot.IsValid() || GroundReadbackJob.IsValid() || Cells.IsEmpty())
	{
		return false;
	}
	GroundReadbackJob = MakeShared<FTDFog3DGroundReadbackJob, ESPMode::ThreadSafe>();
	GroundReadbackJob->Cells = Cells;
	GroundReadbackJob->MissingHeightCm = MissingHeightCm;
	ENQUEUE_RENDER_COMMAND(TDFog3DGroundReadbackStart)([Slot = RenderSlot, Job = GroundReadbackJob](FRHICommandListImmediate& RHICmdList)
	{
		StartGroundReadback(RHICmdList, Slot->State, *Job);
	});
	return true;
}

bool FTDFog3DLayer::TakeGroundReadback(TArray<FTDFog3DGroundReadbackSample>& OutSamples)
{
	if (!GroundReadbackJob.IsValid())
	{
		return false;
	}
	if (!GroundReadbackJob->bIsFinished.load())
	{
		ENQUEUE_RENDER_COMMAND(TDFog3DGroundReadbackPoll)([Job = GroundReadbackJob](FRHICommandListImmediate&)
		{
			PollGroundReadback(*Job);
		});
		return false;
	}
	OutSamples = MoveTemp(GroundReadbackJob->Samples);
	GroundReadbackJob.Reset();
	return true;
}

FTDFluidGpuFog3DFrameInput FTDFog3DLayer::BuildFrameInput(const FTDFog3DStepRequest& Request) const
{
	return PackStep(Request, ReadRejectedStepCount()).FrameInput;
}

FIntPoint FTDFog3DLayer::PreviewNextWindowMinCell(const FVector& PlayerWorld) const
{
	return ComputeNextWindowMinCell(PlayerWorld);
}

FIntPoint FTDFog3DLayer::ComputeNextWindowMinCell(const FVector& PlayerWorld) const
{
	const FIntPoint PlayerCell = ComputeWorldCell(FVector2D(PlayerWorld.X, PlayerWorld.Y), Settings.CellSizeCm);
	const bool bStartsFresh = !bHasSubmittedWindow || bConfigurationChangePending || bZoneReentryPending || bVolumetricFogReenablePending;
	if (bStartsFresh)
	{
		return SnapWindowMinCell(PlayerCell, Resolution);
	}
	return FollowWindowMinCell(SubmittedWindowMinCell, PlayerCell, Resolution, Settings.WindowFollowMarginCells);
}

void FTDFog3DLayer::EnsureRenderSlot()
{
	if (!RenderSlot.IsValid())
	{
		RenderSlot = MakeShared<FTDFog3DRenderSlot, ESPMode::ThreadSafe>();
	}
}

int32 FTDFog3DLayer::ReadRejectedStepCount() const
{
	return RenderSlot.IsValid() ? RenderSlot->RejectedStepCount.load() : 0;
}

FTDFog3DLayer::FPackedStep FTDFog3DLayer::PackStep(const FTDFog3DStepRequest& Request, int32 RejectedStepCount) const
{
	FPackedStep Packed;
	FTDFluidGpuFog3DFrameInput& FrameInput = Packed.FrameInput;

	const double CellSizeCm = static_cast<double>(Settings.CellSizeCm);
	const FIntPoint WindowMinCell = ComputeNextWindowMinCell(Request.PlayerWorld);
	const FVector2D WindowOriginWorld = FVector2D(WindowMinCell) * CellSizeCm;
	const FVector2D WindowCenterWorld = WindowOriginWorld + FVector2D(0.5 * static_cast<double>(Resolution) * CellSizeCm);

	FTDFog3DResetInputs ResetInputs;
	ResetInputs.bIsFirstStep = !bHasSubmittedWindow;
	ResetInputs.bWindowJumpedTooFar = bHasSubmittedWindow && IsWindowJumpTooFar(SubmittedWindowMinCell, WindowMinCell, Resolution);
	ResetInputs.bZoneReentered = bZoneReentryPending;
	ResetInputs.bConfigurationChanged = bConfigurationChangePending;
	ResetInputs.bVolumetricFogReenabled = bVolumetricFogReenablePending;
	ResetInputs.bSawNewRejection = RejectedStepCount > SeenRejectedStepCount;

	FrameInput.Resolution = Resolution;
	FrameInput.CellSizeCm = Settings.CellSizeCm;
	FrameInput.LayerHeightCm = Settings.LayerThicknessCm;
	FrameInput.Mode = Mode;
	FrameInput.WindowMinCell = WindowMinCell;
	FrameInput.PreviousWindowMinCell = bHasSubmittedWindow ? SubmittedWindowMinCell : WindowMinCell;
	FrameInput.bResetsState = ShouldResetState(ResetInputs);
	FrameInput.DeltaSeconds = Request.Clock.StepDeltaSeconds;
	FrameInput.Tuning.WindRefillPerSec = Settings.WindRefillPerSec;
	FrameInput.Tuning.DisplacementRefillHorizontalPerSec = Settings.DisplacementRefillHorizontalPerSec;
	FrameInput.Tuning.DisplacementRefillVerticalPerSec = Settings.DisplacementRefillVerticalPerSec;
	FrameInput.Tuning.FillRefillPerSec = Settings.FillRefillPerSec;
	FrameInput.Tuning.EdgeBandCells = FMath::Clamp(Settings.EdgeBandCells, 0, TDFluidGpuFog3DMaximumEdgeBandCells);
	FrameInput.Tuning.EdgeBandAbsorptionPerSec = Settings.EdgeBandAbsorptionPerSec;

	FTDFluidGpuFog3DInflation Inflation;
	Inflation.BaseHalfThicknessCm = Settings.ObstacleBaseHalfThicknessCm;
	Inflation.UpwardExtraCm = Settings.ObstacleUpwardExtraCm;
	SelectAndPackObstacles(Request.RegisteredObstacles, WindowOriginWorld, WindowCenterWorld, Request.FloorZ, Inflation, Settings.MaximumObstacleCount, FrameInput.Obstacles, Packed.ObstacleKeys, Packed.SkippedObstacleCount);
	FrameInput.bObstaclesChanged = HaveObstaclesChanged(PreviousObstacleKeys, Packed.ObstacleKeys);

	PackSpheres(Request.Inputs, PreviousPositionsBySlot, WindowOriginWorld, Request.FloorZ, FrameInput.bResetsState, Request.Clock.ElapsedSeconds, Settings.bUsesClampedTimeWindBlend, Settings.MaximumInputSpeedCmPerSec, Settings.MaximumInputSphereCount,
		CellSizeCm, Resolution, FrameInput.Spheres, FrameInput.SphereAnchors, Packed.DroppedSphereCount);
	FrameInput.bUsesClampedTimeWindBlend = Settings.bUsesClampedTimeWindBlend;
	FrameInput.bOccludesInputBehindObstacles = Settings.bOccludesInputBehindObstacles;
	PackGroundStep(Request.Ground, Settings, FrameInput);
	return Packed;
}

FTDFog3DLayer::FPackedStep FTDFog3DLayer::PackAndCommitStep(const FTDFog3DStepRequest& Request)
{
	EnsureRenderSlot();
	const int32 RejectedStepCount = ReadRejectedStepCount();
	FPackedStep Packed = PackStep(Request, RejectedStepCount);
	CommitStep(Request, Packed, RejectedStepCount);
	return Packed;
}

void FTDFog3DLayer::ConsumeStepClock()
{
	SecondsSinceSubmittedStep = 0.0;
	if (!(StepsPerSecond > 0.0f))
	{
		StepCreditSeconds = 0.0;
		return;
	}
	const double IntervalSeconds = 1.0 / static_cast<double>(StepsPerSecond);
	StepCreditSeconds = FMath::Clamp(StepCreditSeconds - IntervalSeconds, 0.0, IntervalSeconds);
}

void FTDFog3DLayer::CommitStep(const FTDFog3DStepRequest& Request, const FPackedStep& Packed, int32 RejectedStepCount)
{
	ConsumeStepClock();
	bHasSubmittedWindow = true;
	SubmittedWindowMinCell = Packed.FrameInput.WindowMinCell;
	bZoneReentryPending = false;
	bVolumetricFogReenablePending = false;
	bConfigurationChangePending = false;
	SeenRejectedStepCount = RejectedStepCount;
	PreviousObstacleKeys = Packed.ObstacleKeys;
	LastDroppedSphereCount = Packed.DroppedSphereCount;
	LastSkippedObstacleCount = Packed.SkippedObstacleCount;
	++SubmittedStepCount;

	PreviousPositionsBySlot.Reset();
	for (const FTDFog3DInputSample& Input : Request.Inputs)
	{
		if (Input.CurrentWorld.ContainsNaN())
		{
			continue;
		}
		PreviousPositionsBySlot.Add(Input.SlotId, Input.CurrentWorld);
	}
}

FIntPoint FTDFog3DLayer::ComputeWorldCell(const FVector2D& WorldXY, double CellSizeCm)
{
	return FIntPoint(FMath::FloorToInt32(WorldXY.X / CellSizeCm), FMath::FloorToInt32(WorldXY.Y / CellSizeCm));
}

FIntPoint FTDFog3DLayer::SnapWindowMinCell(const FIntPoint& PlayerCell, int32 WindowCells)
{
	return FIntPoint(SnapWindowAxis(PlayerCell.X, WindowCells), SnapWindowAxis(PlayerCell.Y, WindowCells));
}

FIntPoint FTDFog3DLayer::FollowWindowMinCell(const FIntPoint& CurrentWindowMinCell, const FIntPoint& PlayerCell, int32 WindowCells, int32 MarginCells)
{
	return FIntPoint(
		FollowWindowAxis(CurrentWindowMinCell.X, PlayerCell.X, WindowCells, MarginCells),
		FollowWindowAxis(CurrentWindowMinCell.Y, PlayerCell.Y, WindowCells, MarginCells));
}

bool FTDFog3DLayer::IsWindowJumpTooFar(const FIntPoint& PreviousWindowMinCell, const FIntPoint& NewWindowMinCell, int32 WindowCells)
{
	const int64 DeltaX = static_cast<int64>(NewWindowMinCell.X) - PreviousWindowMinCell.X;
	const int64 DeltaY = static_cast<int64>(NewWindowMinCell.Y) - PreviousWindowMinCell.Y;
	return FMath::Abs(DeltaX) >= WindowCells || FMath::Abs(DeltaY) >= WindowCells;
}

FTDFog3DClockDecision FTDFog3DLayer::DecideClock(double StepCreditSeconds, double SecondsSinceSubmittedStep, float StepsPerSecond, float MaximumStepSeconds)
{
	FTDFog3DClockDecision Decision;
	if (!(StepsPerSecond > 0.0f) || !(MaximumStepSeconds > 0.0f) || !FMath::IsFinite(StepCreditSeconds) || !FMath::IsFinite(SecondsSinceSubmittedStep) || !(SecondsSinceSubmittedStep > 0.0))
	{
		return Decision;
	}

	const double MinimumIntervalSeconds = (1.0 / static_cast<double>(StepsPerSecond)) * (1.0 - Fog3DClockToleranceRatio);
	if (StepCreditSeconds < MinimumIntervalSeconds)
	{
		return Decision;
	}

	Decision.bStepsNow = true;
	Decision.StepDeltaSeconds = static_cast<float>(FMath::Min(SecondsSinceSubmittedStep, static_cast<double>(MaximumStepSeconds)));
	Decision.ElapsedSeconds = static_cast<float>(SecondsSinceSubmittedStep);
	return Decision;
}

bool FTDFog3DLayer::IsUsableOutputTarget(const UTextureRenderTargetVolume& OutputTarget, int32 Resolution)
{
	const bool bHasMatchingSize = OutputTarget.SizeX == Resolution && OutputTarget.SizeY == Resolution && OutputTarget.SizeZ == TDFluidGpuFog3DLayerCount;
	return bHasMatchingSize && OutputTarget.bSupportsUAV && OutputTarget.GetFormat() == PF_FloatRGBA;
}

int32 FTDFog3DLayer::ResolveMode(int32 ConsoleMode, int32 DataMode)
{
	return FMath::Clamp(ConsoleMode == -1 ? DataMode : ConsoleMode, 0, Fog3DModeMaximum);
}

int32 FTDFog3DLayer::ResolveQualityLevel(int32 ConsoleLevel, int32 DataLevel)
{
	return FMath::Clamp(ConsoleLevel == -1 ? DataLevel : ConsoleLevel, 0, Fog3DQualityLevelMaximum);
}

int32 FTDFog3DLayer::ComputeBladeSphereCount(double BladeLengthCm, double RadiusCm)
{
	if (!FMath::IsFinite(BladeLengthCm) || !FMath::IsFinite(RadiusCm) || BladeLengthCm <= 0.0 || RadiusCm <= 0.0)
	{
		return 0;
	}
	return FMath::CeilToInt32(BladeLengthCm / RadiusCm - Fog3DBladeCountEpsilon) + 1;
}

void FTDFog3DLayer::ComputeBladeSphereCenters(const FVector& BaseWorld, const FVector& TipWorld, double RadiusCm, TArray<FVector>& OutCenters)
{
	OutCenters.Reset();
	const int32 SphereCount = ComputeBladeSphereCount(FVector::Dist(BaseWorld, TipWorld), RadiusCm);
	if (SphereCount <= 0)
	{
		return;
	}
	if (SphereCount == 1)
	{
		OutCenters.Add(TipWorld);
		return;
	}

	for (int32 SphereIndex = 0; SphereIndex < SphereCount; ++SphereIndex)
	{
		const double Ratio = static_cast<double>(SphereIndex) / static_cast<double>(SphereCount - 1);
		OutCenters.Add(TipWorld + (BaseWorld - TipWorld) * Ratio);
	}
}

void FTDFog3DLayer::PackSpheres(
	const TArray<FTDFog3DInputSample>& Inputs,
	const TMap<int32, FVector>& PreviousPositionsBySlot,
	const FVector2D& WindowOriginWorldXY,
	double FloorZ,
	bool bIsResetStep,
	float ElapsedSeconds,
	bool bAppliesWindGain,
	float MaximumSpeedCmPerSec,
	int32 MaximumSphereCount,
	TArray<FTDFluidGpuFog3DSphere>& OutSpheres,
	int32& OutDroppedCount)
{
	TArray<FTDFluidGpuFog3DSphereAnchor> UnusedSphereAnchors;
	PackSpheres(Inputs, PreviousPositionsBySlot, WindowOriginWorldXY, FloorZ, bIsResetStep, ElapsedSeconds, bAppliesWindGain, MaximumSpeedCmPerSec, MaximumSphereCount, 0.0, 0, OutSpheres, UnusedSphereAnchors, OutDroppedCount);
}

FTDFluidGpuFog3DSphereAnchor FTDFog3DLayer::PackSphereAnchor(const FTDFog3DInputSample& Input, const FVector& WindowOffset, double CellSizeCm, int32 Resolution)
{
	if (!Input.bHasOcclusionAnchor)
	{
		return FTDFluidGpuFog3DSphereAnchor();
	}
	return MakeTDFluidGpuFog3DSphereAnchor(Input.OcclusionAnchorWorld - WindowOffset, CellSizeCm, Resolution);
}

void FTDFog3DLayer::PackSpheres(
	const TArray<FTDFog3DInputSample>& Inputs,
	const TMap<int32, FVector>& PreviousPositionsBySlot,
	const FVector2D& WindowOriginWorldXY,
	double FloorZ,
	bool bIsResetStep,
	float ElapsedSeconds,
	bool bAppliesWindGain,
	float MaximumSpeedCmPerSec,
	int32 MaximumSphereCount,
	double CellSizeCm,
	int32 Resolution,
	TArray<FTDFluidGpuFog3DSphere>& OutSpheres,
	TArray<FTDFluidGpuFog3DSphereAnchor>& OutSphereAnchors,
	int32& OutDroppedCount)
{
	OutSpheres.Reset();
	OutSphereAnchors.Reset();
	OutDroppedCount = 0;
	const int32 SphereLimit = FMath::Clamp(MaximumSphereCount, 0, TDFluidGpuFog3DMaximumSphereCount);
	const FVector WindowOffset(WindowOriginWorldXY.X, WindowOriginWorldXY.Y, FloorZ);
	const bool bHasElapsedTime = FMath::IsFinite(ElapsedSeconds) && ElapsedSeconds > 0.0f;
	const double SpeedLimit = FMath::IsFinite(MaximumSpeedCmPerSec) ? FMath::Max(static_cast<double>(MaximumSpeedCmPerSec), 0.0) : 0.0;

	for (const FTDFog3DInputSample& Input : Inputs)
	{
		if (Input.CurrentWorld.ContainsNaN() || !FMath::IsFinite(Input.RadiusCm) || Input.RadiusCm <= 0.0f)
		{
			continue;
		}
		if (OutSpheres.Num() >= SphereLimit)
		{
			++OutDroppedCount;
			continue;
		}

		const FVector* PreviousPosition = bIsResetStep ? nullptr : PreviousPositionsBySlot.Find(Input.SlotId);
		const FVector StartWorld = PreviousPosition ? *PreviousPosition : Input.CurrentWorld;
		FVector Velocity = FVector::ZeroVector;
		if (bHasElapsedTime)
		{
			Velocity = (Input.CurrentWorld - StartWorld) / static_cast<double>(ElapsedSeconds);
			if (bAppliesWindGain)
			{
				Velocity *= static_cast<double>(Input.WindGain);
			}
			const double Speed = Velocity.Size();
			if (Speed > SpeedLimit)
			{
				Velocity = Speed > 0.0 ? Velocity * (SpeedLimit / Speed) : FVector::ZeroVector;
			}
		}

		const FVector StartWindow = StartWorld - WindowOffset;
		const FVector EndWindow = Input.CurrentWorld - WindowOffset;
		FTDFluidGpuFog3DSphere& Sphere = OutSpheres.AddDefaulted_GetRef();
		Sphere.StartCmRadiusCm = FVector4f(static_cast<float>(StartWindow.X), static_cast<float>(StartWindow.Y), static_cast<float>(StartWindow.Z), Input.RadiusCm);
		Sphere.EndCmWindStrength = FVector4f(static_cast<float>(EndWindow.X), static_cast<float>(EndWindow.Y), static_cast<float>(EndWindow.Z), Input.WindStrength);
		Sphere.VelocityCmPerSecErosionPerSec = FVector4f(static_cast<float>(Velocity.X), static_cast<float>(Velocity.Y), static_cast<float>(Velocity.Z), Input.ErosionPerSec);
		OutSphereAnchors.Add(PackSphereAnchor(Input, WindowOffset, CellSizeCm, Resolution));
	}
}

bool FTDFog3DLayer::ReadUprightYaw(const FTDFluidSdfObstacle& Obstacle, double& OutYawDegrees)
{
	if (!Obstacle.bIsBox)
	{
		return false;
	}

	const FRotationMatrix RotationMatrix(Obstacle.WorldRotation);
	const FVector AxisX = RotationMatrix.GetScaledAxis(EAxis::X);
	const FVector AxisZ = RotationMatrix.GetScaledAxis(EAxis::Z);
	if (FMath::Abs(AxisZ.Z) < 1.0 - Fog3DUprightTolerance)
	{
		return false;
	}

	OutYawDegrees = FMath::RadiansToDegrees(FMath::Atan2(AxisX.Y, AxisX.X));
	return true;
}

void FTDFog3DLayer::SelectAndPackObstacles(
	const TArray<FTDFluidSdfObstacle>& Registered,
	const FVector2D& WindowOriginWorldXY,
	const FVector2D& WindowCenterWorldXY,
	double FloorZ,
	const FTDFluidGpuFog3DInflation& Inflation,
	int32 MaximumObstacleCount,
	TArray<FTDFluidGpuFog3DObstacle>& OutObstacles,
	TArray<FTDFog3DObstacleKey>& OutKeys,
	int32& OutSkippedCount)
{
	OutObstacles.Reset();
	OutKeys.Reset();
	OutSkippedCount = 0;

	TArray<FFog3DObstacleCandidate> Candidates;
	Candidates.Reserve(Registered.Num());
	for (const FTDFluidSdfObstacle& Obstacle : Registered)
	{
		double YawDegrees = 0.0;
		if (!ReadUprightYaw(Obstacle, YawDegrees))
		{
			++OutSkippedCount;
			continue;
		}

		FFog3DObstacleCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.Key.WorldCenter = Obstacle.WorldCenter;
		Candidate.Key.HalfExtentsCm = Obstacle.BoxHalfExtents3DCm;
		Candidate.Key.YawDegrees = YawDegrees;
		Candidate.HorizontalDistanceSquared = FVector2D::DistSquared(FVector2D(Obstacle.WorldCenter.X, Obstacle.WorldCenter.Y), WindowCenterWorldXY);
	}

	Candidates.StableSort([](const FFog3DObstacleCandidate& Left, const FFog3DObstacleCandidate& Right)
	{
		return Left.HorizontalDistanceSquared < Right.HorizontalDistanceSquared;
	});

	const int32 ObstacleLimit = FMath::Min(FMath::Clamp(MaximumObstacleCount, 0, TDFluidGpuFog3DMaximumObstacleCount), Candidates.Num());
	const FVector WindowOffset(WindowOriginWorldXY.X, WindowOriginWorldXY.Y, FloorZ);
	for (int32 CandidateIndex = 0; CandidateIndex < ObstacleLimit; ++CandidateIndex)
	{
		const FTDFog3DObstacleKey& Key = Candidates[CandidateIndex].Key;
		OutKeys.Add(Key);
		OutObstacles.Add(MakeTDFluidGpuFog3DUprightBox(Key.WorldCenter - WindowOffset, Key.HalfExtentsCm, Key.YawDegrees, Inflation));
	}
}

bool FTDFog3DLayer::HaveObstaclesChanged(const TArray<FTDFog3DObstacleKey>& PreviousKeys, const TArray<FTDFog3DObstacleKey>& NewKeys)
{
	return PreviousKeys != NewKeys;
}

bool FTDFog3DLayer::ShouldResetState(const FTDFog3DResetInputs& Inputs)
{
	return Inputs.bIsFirstStep
		|| Inputs.bWindowJumpedTooFar
		|| Inputs.bZoneReentered
		|| Inputs.bConfigurationChanged
		|| Inputs.bVolumetricFogReenabled
		|| Inputs.bSawNewRejection;
}

FVector4f FTDFog3DLayer::ComputeMaterialWindow(const FIntPoint& SubmittedWindowMinCell, double CellSizeCm, const FVector& ZoneReferenceWorld, double FloorZ, int32 WindowCells)
{
	return FVector4f(
		static_cast<float>(static_cast<double>(SubmittedWindowMinCell.X) * CellSizeCm - ZoneReferenceWorld.X),
		static_cast<float>(static_cast<double>(SubmittedWindowMinCell.Y) * CellSizeCm - ZoneReferenceWorld.Y),
		static_cast<float>(FloorZ - ZoneReferenceWorld.Z),
		static_cast<float>(WindowCells));
}
