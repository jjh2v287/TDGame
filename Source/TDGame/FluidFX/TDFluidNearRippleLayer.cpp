#include "FluidFX/TDFluidNearRippleLayer.h"

#include "Engine/Texture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RenderGraphBuilder.h"
#include "RenderingThread.h"
#include "TDFluidGpuInputs.h"
#include "TDFluidGpuNearRipple.h"
#include "TextureResource.h"

namespace
{
	constexpr int32 NearRippleMinimumResolution = 64;
	constexpr int32 NearRippleMaximumResolution = 512;
	constexpr int32 NearRippleResolutionAlignment = 8;
	constexpr int32 NearRippleMaximumSources = 64;
	constexpr int32 NearRippleMaximumSubsteps = 8;
	constexpr float NearRippleMinimumWaveSpeedCmPerSec = 1.0f;
	constexpr double NearRippleMinimumSourceCellSpan = 2.0;
}

FTDFluidNearRippleLayer::~FTDFluidNearRippleLayer()
{
	Release();
}

void FTDFluidNearRippleLayer::Configure(const FTDFluidWaterNearRippleSettings& InSettings)
{
	const bool bChangesGrid = SanitizeResolution(InSettings.Resolution) != SanitizeResolution(Settings.Resolution) || InSettings.ExtentCm != Settings.ExtentCm;
	Settings = InSettings;
	if (bChangesGrid)
	{
		bNeedsReset = true;
	}
}

void FTDFluidNearRippleLayer::AddMovingSource(const FVector2D& WorldXY, const FVector2D& VelocityXY, float StampRadiusCm)
{
	if (WorldXY.ContainsNaN() || VelocityXY.ContainsNaN() || !FMath::IsFinite(StampRadiusCm))
	{
		return;
	}

	FPendingSource& Source = PendingSources.AddDefaulted_GetRef();
	Source.WorldXY = WorldXY;
	Source.StampRadiusCm = StampRadiusCm;
	Source.SpeedCmPerSec = static_cast<float>(VelocityXY.Size());
}

void FTDFluidNearRippleLayer::AddIdleContact(const FVector2D& WorldXY)
{
	if (WorldXY.ContainsNaN())
	{
		return;
	}

	FPendingSource& Source = PendingSources.AddDefaulted_GetRef();
	Source.WorldXY = WorldXY;
	Source.bIsIdleContact = true;
}

void FTDFluidNearRippleLayer::ClearPendingSources()
{
	PendingSources.Reset();
}

TArray<FTDFluidNearRippleSourceSelection> FTDFluidNearRippleLayer::SelectFrameSources(const FVector2D& WindowCenterWorld) const
{
	TArray<int32> Order;
	Order.Reserve(PendingSources.Num());
	for (int32 SourceIndex = 0; SourceIndex < PendingSources.Num(); ++SourceIndex)
	{
		Order.Add(SourceIndex);
	}
	Order.StableSort([this, &WindowCenterWorld](int32 Left, int32 Right)
	{
		return FVector2D::DistSquared(PendingSources[Left].WorldXY, WindowCenterWorld) < FVector2D::DistSquared(PendingSources[Right].WorldXY, WindowCenterWorld);
	});

	const int32 MaxSources = FMath::Clamp(Settings.MaxSourcesPerFrame, 1, NearRippleMaximumSources);
	const double MinimumRadiusCm = NearRippleMinimumSourceCellSpan * ComputeCellSizeCm();
	const float ReferenceSpeed = FMath::Max(Settings.MovingReferenceSpeedCmPerSec, 1.0f);
	TArray<FTDFluidNearRippleSourceSelection> Selected;
	Selected.Reserve(FMath::Min(MaxSources, Order.Num()));
	for (const int32 SourceIndex : Order)
	{
		if (Selected.Num() >= MaxSources)
		{
			break;
		}

		const FPendingSource& Pending = PendingSources[SourceIndex];
		FTDFluidNearRippleSourceSelection& Selection = Selected.AddDefaulted_GetRef();
		Selection.WorldXY = Pending.WorldXY;
		if (Pending.bIsIdleContact)
		{
			Selection.RadiusCm = FMath::Max(Settings.IdleContactRadiusCm, static_cast<float>(MinimumRadiusCm));
			Selection.IdleWeight = 1.0f;
			continue;
		}

		Selection.RadiusCm = static_cast<float>(FMath::Max(static_cast<double>(Pending.StampRadiusCm * Settings.SourceRadiusScale), MinimumRadiusCm));
		Selection.ImpulseCmPerSec = Settings.MovingImpulseCmPerSec * FMath::Pow(FMath::Min(1.0f, Pending.SpeedCmPerSec / ReferenceSpeed), Settings.MovingSpeedExponent);
	}
	return Selected;
}

void FTDFluidNearRippleLayer::SubmitStep(const FTDFluidNearRippleMaskInputs& Masks, const FVector2D& CenterWorldXY, float DeltaTime, UTextureRenderTarget2D& OutputTarget)
{
	FTextureRenderTargetResource* OutputResource = OutputTarget.GameThread_GetRenderTargetResource();
	if (!OutputResource || !FMath::IsFinite(DeltaTime) || DeltaTime <= 0.0f || CenterWorldXY.ContainsNaN())
	{
		ClearPendingSources();
		return;
	}

	if (!RenderState.IsValid())
	{
		RenderState = MakeShared<FTDFluidGpuNearRippleState, ESPMode::ThreadSafe>();
		bNeedsReset = true;
	}

	const int32 Resolution = GetResolution();
	const double CellSizeCm = ComputeCellSizeCm();
	const FIntPoint WindowMinCell = ComputeWindowMinCell(CenterWorldXY, CellSizeCm, Resolution);
	const FTDFluidNearRippleSubstepPlan SubstepPlan = ComputeSubstepPlan(Settings.WaveSpeedCmPerSec, DeltaTime, static_cast<float>(CellSizeCm), Settings.MaxSubsteps);
	const FVector2D WindowOriginWorld = FVector2D(WindowMinCell) * CellSizeCm;
	const float ContactHz = FMath::Max(Settings.IdleContactHz, 0.0f);

	FTDFluidGpuNearRippleFrameInput FrameInput;
	FrameInput.Resolution = Resolution;
	FrameInput.CellSizeCm = static_cast<float>(CellSizeCm);
	FrameInput.WindowMinCell = WindowMinCell;
	FrameInput.PreviousWindowMinCell = bHasWindow ? CurrentWindowMinCell : WindowMinCell;
	FrameInput.bResetsState = bNeedsReset || !bHasWindow || ShouldResetForShift(CurrentWindowMinCell, WindowMinCell, Resolution);
	FrameInput.SubstepCount = SubstepPlan.SubstepCount;
	FrameInput.SubstepDeltaSeconds = SubstepPlan.SubstepDeltaSeconds;
	FrameInput.WaveSpeedCmPerSec = FMath::Max(Settings.WaveSpeedCmPerSec, NearRippleMinimumWaveSpeedCmPerSec);
	FrameInput.DampingPerSec = FMath::Max(Settings.DampingPerSec, 0.0f);
	FrameInput.EdgeFadeRatio = FMath::Clamp(Settings.EdgeFadeRatio, 0.02f, 0.4f);
	FrameInput.EdgeAbsorptionStrength = FMath::Max(Settings.EdgeAbsorptionStrength, 0.0f);
	FrameInput.IdleContactAmplitudeCm = Settings.IdleContactAmplitudeCm;
	FrameInput.IdleContactStartPhase = ComputeIdleContactPhase(LayerTimeSeconds, ContactHz);
	FrameInput.IdleContactPhasePerSubstep = UE_DOUBLE_TWO_PI * static_cast<double>(ContactHz) * static_cast<double>(SubstepPlan.SubstepDeltaSeconds);
	FrameInput.MinWaterDepthCm = FMath::Max(Settings.MinWaterDepthCm, 0.0f);
	FrameInput.MainWindowCenterFromOriginCm = FVector2f(Masks.MainWindowCenterCm - WindowOriginWorld);
	FrameInput.MainWindowExtentCm = FVector2f(Masks.MainWindowExtentCm);
	FrameInput.MainWaterDepthReferenceCm = Masks.WaterDepthReferenceCm;
	FrameInput.ObstacleDistanceOutputRangeCm = Masks.ObstacleDistanceOutputRangeCm;
	FrameInput.bHasObstacleDistanceAux = Masks.bHasObstacleDistanceAux && Masks.AuxTexture;
	FrameInput.WaterSurfaceTexture = Masks.WaterSurfaceTexture ? Masks.WaterSurfaceTexture->GetResource() : nullptr;
	FrameInput.AuxTexture = Masks.AuxTexture ? Masks.AuxTexture->GetResource() : nullptr;
	FrameInput.OutputTarget = OutputResource;

	const FVector2D WindowCenterWorld = WindowOriginWorld + FVector2D(0.5 * Resolution * CellSizeCm);
	for (const FTDFluidNearRippleSourceSelection& Selection : SelectFrameSources(WindowCenterWorld))
	{
		FTDFluidGpuNearRippleSource& Source = FrameInput.Sources.AddDefaulted_GetRef();
		Source.PositionCm = FVector2f(Selection.WorldXY - WindowOriginWorld);
		Source.RadiusCm = Selection.RadiusCm;
		Source.ImpulseCmPerSec = Selection.ImpulseCmPerSec;
		Source.IdleWeight = Selection.IdleWeight;
	}

	ENQUEUE_RENDER_COMMAND(TDFluidNearRippleStep)([State = RenderState, FrameInput = MoveTemp(FrameInput)](FRHICommandListImmediate& RHICmdList)
	{
		FRDGBuilder GraphBuilder(RHICmdList);
		AddTDFluidGpuNearRipplePasses(GraphBuilder, *State, FrameInput);
		GraphBuilder.Execute();
	});

	CurrentWindowMinCell = WindowMinCell;
	bHasWindow = true;
	bNeedsReset = false;
	LayerTimeSeconds += static_cast<double>(SubstepPlan.SubstepCount) * static_cast<double>(SubstepPlan.SubstepDeltaSeconds);
	ClearPendingSources();
}

void FTDFluidNearRippleLayer::Release()
{
	ClearPendingSources();
	bNeedsReset = true;
	bHasWindow = false;
	if (!RenderState.IsValid())
	{
		return;
	}

	ENQUEUE_RENDER_COMMAND(TDFluidNearRippleRelease)([State = MoveTemp(RenderState)](FRHICommandListImmediate&)
	{
		State->Release();
	});
	RenderState.Reset();
}

int32 FTDFluidNearRippleLayer::GetResolution() const
{
	return SanitizeResolution(Settings.Resolution);
}

float FTDFluidNearRippleLayer::GetExtentCm() const
{
	return FMath::Clamp(Settings.ExtentCm, 200.0f, 1200.0f);
}

FVector2D FTDFluidNearRippleLayer::GetSnappedCenterWorld() const
{
	const double CellSizeCm = ComputeCellSizeCm();
	return (FVector2D(CurrentWindowMinCell) + FVector2D(0.5 * GetResolution())) * CellSizeCm;
}

double FTDFluidNearRippleLayer::ComputeCellSizeCm() const
{
	return static_cast<double>(GetExtentCm()) / static_cast<double>(GetResolution());
}

int32 FTDFluidNearRippleLayer::SanitizeResolution(int32 RequestedResolution)
{
	const int32 ClampedResolution = FMath::Clamp(RequestedResolution, NearRippleMinimumResolution, NearRippleMaximumResolution);
	return ClampedResolution - ClampedResolution % NearRippleResolutionAlignment;
}

FIntPoint FTDFluidNearRippleLayer::ComputeWindowMinCell(const FVector2D& CenterWorldXY, double CellSizeCm, int32 Resolution)
{
	const int32 HalfResolution = Resolution / 2;
	return FIntPoint(
		FMath::FloorToInt32(CenterWorldXY.X / CellSizeCm) - HalfResolution,
		FMath::FloorToInt32(CenterWorldXY.Y / CellSizeCm) - HalfResolution);
}

FTDFluidNearRippleSubstepPlan FTDFluidNearRippleLayer::ComputeSubstepPlan(float WaveSpeedCmPerSec, float DeltaTime, float CellSizeCm, int32 MaxSubsteps)
{
	FTDFluidNearRippleSubstepPlan Plan;
	const float ClampedDeltaTime = FMath::Clamp(DeltaTime, 0.0f, TDFluidGpuSolverConstants::MaximumStepDeltaTimeSeconds);
	const float WaveSpeed = FMath::Max(WaveSpeedCmPerSec, NearRippleMinimumWaveSpeedCmPerSec);
	const float StableSubstepSeconds = TDFluidGpuSolverConstants::WaterRippleSubstepSafety * CellSizeCm / (WaveSpeed * UE_SQRT_2);
	const int32 SubstepLimit = FMath::Clamp(MaxSubsteps, 1, NearRippleMaximumSubsteps);
	const int32 RequiredSubsteps = FMath::Max(1, FMath::CeilToInt32(ClampedDeltaTime / StableSubstepSeconds));
	if (RequiredSubsteps <= SubstepLimit)
	{
		Plan.SubstepCount = RequiredSubsteps;
		Plan.SubstepDeltaSeconds = ClampedDeltaTime / static_cast<float>(RequiredSubsteps);
		return Plan;
	}

	Plan.SubstepCount = SubstepLimit;
	Plan.SubstepDeltaSeconds = StableSubstepSeconds;
	return Plan;
}

double FTDFluidNearRippleLayer::ComputeIdleContactPhase(double LayerTimeSeconds, float ContactHz)
{
	return UE_DOUBLE_TWO_PI * FMath::Frac(static_cast<double>(ContactHz) * LayerTimeSeconds);
}

bool FTDFluidNearRippleLayer::IsCellInsideWindow(const FIntPoint& WorldCell, const FIntPoint& WindowMinCell, int32 Resolution)
{
	const FIntPoint LocalCell = WorldCell - WindowMinCell;
	return LocalCell.X >= 0 && LocalCell.Y >= 0 && LocalCell.X < Resolution && LocalCell.Y < Resolution;
}

FIntPoint FTDFluidNearRippleLayer::ComputeStorageCell(const FIntPoint& WorldCell, int32 Resolution)
{
	return FIntPoint(((WorldCell.X % Resolution) + Resolution) % Resolution, ((WorldCell.Y % Resolution) + Resolution) % Resolution);
}

bool FTDFluidNearRippleLayer::ShouldResetForShift(const FIntPoint& PreviousWindowMinCell, const FIntPoint& WindowMinCell, int32 Resolution)
{
	const FIntPoint Shift = WindowMinCell - PreviousWindowMinCell;
	return FMath::Abs(Shift.X) >= Resolution || FMath::Abs(Shift.Y) >= Resolution;
}

bool FTDFluidNearRippleLayer::ResolveEnabled(int32 ConsoleState, bool bPresetEnabled)
{
	if (ConsoleState == 0)
	{
		return false;
	}

	return ConsoleState > 0 || bPresetEnabled;
}
