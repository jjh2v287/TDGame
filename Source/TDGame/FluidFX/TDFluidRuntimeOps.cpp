#include "FluidFX/TDFluidRuntimeOps.h"

#include "Engine/GameViewportClient.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"

namespace
{
	constexpr float TDFluidOpsMaxStepRateCapHz = 240.0f;
	constexpr float TDFluidOpsLodStepRateFloorHz = 12.0f;
	constexpr float TDFluidOpsMaxVorticityStrength = 20.0f;
	constexpr int32 TDFluidOpsMinPressureIterations = 1;
	constexpr int32 TDFluidOpsMaxPressureIterations = 32;
	constexpr int32 TDFluidOpsMaxPauseMode = 2;

	TAutoConsoleVariable<float> CVarTDFluidMaxStepRateHz(
		TEXT("TD.FluidFX.MaxStepRateHz"),
		-1.0f,
		TEXT("Fluid step rate cap in Hz: negative uses the preset tuning value, 0 forces no cap, above 0 forces that cap for every actor (clamped to 240)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarTDFluidPauseWhenNotVisible(
		TEXT("TD.FluidFX.PauseWhenNotVisible"),
		-1,
		TEXT("Pause fluid actors that were not rendered recently: negative uses the preset tuning value, 0 forces off, 1 forces on with the viewport gate, 2 forces on and ignores the viewport gate (tests and measurement only)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarTDFluidArbitrateSharedMpc(
		TEXT("TD.FluidFX.ArbitrateSharedMpc"),
		0,
		TEXT("Arbitrate writes to a shared material parameter collection between fluid components: 0 keeps every component writing, 1 lets only the first awake claimant write."),
		ECVF_Default);

	double TDFluidOpsMinExtentCm(const FTDFluidSimulationConfig& Config)
	{
		return FMath::Min(Config.SimExtentCm.X, Config.SimExtentCm.Y);
	}

	bool TDFluidOpsHasSameAddress(const UTextureRenderTarget2D& Target, TextureAddress ExpectedAddress)
	{
		return Target.AddressX == ExpectedAddress && Target.AddressY == ExpectedAddress;
	}

	bool TDFluidOpsHasUnorderedAccess(const UTextureRenderTarget2D& Target)
	{
		return Target.bSupportsUAV || Target.bCanCreateUAV;
	}
}

bool FTDFluidExternalOutputTargets::HasAnyTarget() const
{
	return !DensityPressureTarget.IsNull() || !VelocityTarget.IsNull() || !AuxTarget.IsNull() || !WaterSurfaceTarget.IsNull();
}

float TDFluidRuntimeOps::AdvanceStepClock(FTDFluidStepClock& InOutClock, float FrameDeltaSeconds, float StepRateHz)
{
	InOutClock.ElapsedSinceStepSeconds += FrameDeltaSeconds;
	if (StepRateHz <= 0.0f)
	{
		const float UncappedStepDeltaSeconds = InOutClock.ElapsedSinceStepSeconds;
		InOutClock = FTDFluidStepClock();
		return UncappedStepDeltaSeconds;
	}

	const float StepIntervalSeconds = 1.0f / StepRateHz;
	InOutClock.RateClockSeconds += FrameDeltaSeconds;
	if (InOutClock.RateClockSeconds < StepIntervalSeconds - 0.5f * FrameDeltaSeconds)
	{
		return 0.0f;
	}

	const float CappedStepDeltaSeconds = InOutClock.ElapsedSinceStepSeconds;
	InOutClock.ElapsedSinceStepSeconds = 0.0f;
	InOutClock.RateClockSeconds = FMath::Clamp(InOutClock.RateClockSeconds - StepIntervalSeconds, -StepIntervalSeconds, StepIntervalSeconds);
	return CappedStepDeltaSeconds;
}

float TDFluidRuntimeOps::ResolveMaxStepRateHz(const FTDFluidRuntimeOpsSettings& Settings)
{
	const float ConsoleStepRateHz = CVarTDFluidMaxStepRateHz.GetValueOnGameThread();
	if (ConsoleStepRateHz < 0.0f)
	{
		return FMath::Clamp(Settings.MaxStepRateHz, 0.0f, TDFluidOpsMaxStepRateCapHz);
	}

	return FMath::Min(ConsoleStepRateHz, TDFluidOpsMaxStepRateCapHz);
}

float TDFluidRuntimeOps::ComputeLodStepRateHz(const FTDFluidRuntimeOpsSettings& Settings, float MaxStepRateHz, double ObserverDistanceCm)
{
	const int32 LodStepCount = Settings.LodStepCount;
	const double NearDistanceCm = Settings.LodNearDistanceCm;
	const double FarDistanceCm = Settings.LodFarDistanceCm;
	if (MaxStepRateHz <= 0.0f || LodStepCount <= 0 || FarDistanceCm <= NearDistanceCm)
	{
		return MaxStepRateHz;
	}

	const float MinStepRateHz = FMath::Min(FMath::Max(Settings.LodMinStepRateHz, TDFluidOpsLodStepRateFloorHz), MaxStepRateHz);
	const double DistanceRatio = FMath::Clamp((ObserverDistanceCm - NearDistanceCm) / (FarDistanceCm - NearDistanceCm), 0.0, 1.0);
	const int32 LodStepIndex = FMath::Clamp(FMath::CeilToInt32(DistanceRatio * LodStepCount), 0, LodStepCount);
	return MaxStepRateHz - (MaxStepRateHz - MinStepRateHz) * static_cast<float>(LodStepIndex) / static_cast<float>(LodStepCount);
}

int32 TDFluidRuntimeOps::ResolvePauseWhenNotVisibleMode(const FTDFluidRuntimeOpsSettings& Settings)
{
	const int32 ConsolePauseMode = CVarTDFluidPauseWhenNotVisible.GetValueOnGameThread();
	if (ConsolePauseMode < 0)
	{
		return Settings.bPauseWhenNotVisible ? 1 : 0;
	}

	return FMath::Min(ConsolePauseMode, TDFluidOpsMaxPauseMode);
}

bool TDFluidRuntimeOps::CanJudgeVisibility(const UWorld& World)
{
	if (!FApp::CanEverRender())
	{
		return false;
	}

	const UGameViewportClient* GameViewport = World.GetGameViewport();
	return GameViewport && !GameViewport->bDisableWorldRendering;
}

bool TDFluidRuntimeOps::ShouldPauseForVisibility(int32 PauseMode, bool bIsEligible, bool bCanJudgeVisibility, bool bWasRecentlyRendered)
{
	if (PauseMode <= 0 || !bIsEligible)
	{
		return false;
	}

	if (PauseMode == 1 && !bCanJudgeVisibility)
	{
		return false;
	}

	return !bWasRecentlyRendered;
}

bool TDFluidRuntimeOps::IsUsableExternalOutputTarget(const UTextureRenderTarget2D* Target, int32 Resolution, const FLinearColor& ExpectedClearColor, TextureAddress ExpectedAddress)
{
	if (!Target || Resolution <= 0)
	{
		return false;
	}

	return Target->RenderTargetFormat == RTF_RGBA16f
		&& Target->SizeX == Resolution
		&& Target->SizeY == Resolution
		&& TDFluidOpsHasUnorderedAccess(*Target)
		&& Target->ClearColor == ExpectedClearColor
		&& TDFluidOpsHasSameAddress(*Target, ExpectedAddress)
		&& !Target->bAutoGenerateMips;
}

bool TDFluidRuntimeOps::IsSharedMpcArbitrationEnabled()
{
	return CVarTDFluidArbitrateSharedMpc.GetValueOnGameThread() != 0;
}

FTDFluidSimulationConfig TDFluidRuntimeOps::MakeCellSizeNormalizedConfig(const FTDFluidSimulationConfig& Config, int32 EffectiveResolution, const FTDFluidRuntimeOpsSettings& Settings, bool bScalesPressureIterations)
{
	if (!Settings.bNormalizeDynamicsByCellSize || Config.bEnableWaterLayer || Config.SimMode == ETDFluidSimMode::SimplePainter || EffectiveResolution <= 0)
	{
		return Config;
	}

	const double MinExtentCm = TDFluidOpsMinExtentCm(Config);
	if (MinExtentCm <= 0.0 || Config.GridResolution <= 0)
	{
		return Config;
	}

	const double CellSizeCm = MinExtentCm / EffectiveResolution;
	const double ReferenceCellSizeCm = Settings.ReferenceCellSizeCm > 0.0f ? static_cast<double>(Settings.ReferenceCellSizeCm) : MinExtentCm / Config.GridResolution;
	const double CellSizeScale = ReferenceCellSizeCm / CellSizeCm;

	FTDFluidSimulationConfig NormalizedConfig = Config;
	NormalizedConfig.VorticityStrength = FMath::Clamp(static_cast<float>(Config.VorticityStrength * CellSizeScale), 0.0f, TDFluidOpsMaxVorticityStrength);
	if (bScalesPressureIterations)
	{
		const int32 ScaledPressureIterations = FMath::RoundToInt32(Config.PressureIterations * CellSizeScale * CellSizeScale);
		NormalizedConfig.PressureIterations = FMath::Clamp(ScaledPressureIterations, TDFluidOpsMinPressureIterations, TDFluidOpsMaxPressureIterations);
	}

	return NormalizedConfig;
}
