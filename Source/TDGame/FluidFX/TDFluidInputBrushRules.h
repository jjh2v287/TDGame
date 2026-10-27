#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidTypes.h"

struct FTDFluidPlaneBasis;

struct FTDFluidInputBrushRules
{
	static bool HasActiveRules(const FTDFluidInputBrushSettings& Settings);
	static bool IsVelocityOnlySource(ETDFluidVelocityOnlySources Selection, ETDFluidBrushSource Source);
	static float ComputeSlowSpeedDensityFactor(float PlanarSpeedCmPerSec, float FadeBelowSpeedCmPerSec);
	static float ComputeHeightFadeFactor(double HeightAboveSurfaceCm, float FadeDistanceCm);
	static void ApplyToStamps(TArray<FTDFluidBrushStamp>& InOutStamps, const FTDFluidInputBrushSettings& Settings, const FTDFluidPlaneBasis& PlaneBasis, double SurfaceDepthCm);

private:
	static void ApplyDensityRules(FTDFluidBrushStamp& InOutStamp, const FTDFluidInputBrushSettings& Settings, const FTDFluidPlaneBasis& PlaneBasis);
	static void ScaleStampStrengths(FTDFluidBrushStamp& InOutStamp, float Factor);
};
