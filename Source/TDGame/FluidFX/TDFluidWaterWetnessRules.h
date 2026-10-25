#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidTypes.h"

struct FTDFluidWaterWetnessRules
{
	FORCEINLINE static float SanitizeDryingSeconds(float DryingSeconds) { return FMath::Clamp(DryingSeconds, 0.01f, 10000.0f); }
	FORCEINLINE static float SanitizeSwashHeightCm(float SwashHeightCm) { return FMath::Clamp(SwashHeightCm, 0.0f, 1000.0f); }
	FORCEINLINE static float SanitizeCoastWavePeriodSeconds(float PeriodSeconds) { return FMath::Clamp(PeriodSeconds, 0.1f, 1000.0f); }

	FORCEINLINE static float ComputeCoastWavePhase(double SimulationTimeSeconds, float CoastWavePeriodSeconds)
	{
		const double PeriodSeconds = FMath::Max(static_cast<double>(CoastWavePeriodSeconds), 0.1);
		return static_cast<float>(FMath::Frac(SimulationTimeSeconds / PeriodSeconds));
	}

	FORCEINLINE static float ComputeSwashReachCm(float SwashHeightCm, float CoastWavePhase)
	{
		return SwashHeightCm * FMath::Max(0.0f, FMath::Sin(UE_TWO_PI * CoastWavePhase));
	}

	FORCEINLINE static float ComputeRetainFactor(float StepDeltaTimeSeconds, float DryingSeconds)
	{
		return FMath::Exp(-StepDeltaTimeSeconds / DryingSeconds);
	}

	FORCEINLINE static float ComputeInverseHeight(float HeightCm)
	{
		return HeightCm > 0.0f ? 1.0f / HeightCm : 0.0f;
	}
};
