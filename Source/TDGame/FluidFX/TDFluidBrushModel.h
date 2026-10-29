#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidTypes.h"

struct FTDFluidPlaneBasis;

struct FTDFluidBrushModel
{
	static bool HasActiveModel(const FTDFluidInputBrushSettings& Settings);
	static float ResolveSourceRadiusScale(const FTDFluidInputBrushSettings& Settings, ETDFluidBrushSource Source);
	static float SampleValueNoise(const FVector2D& PositionCm, float ScaleCm, int32 Seed, int32 Stream);
	static FVector2D ComputeJitterOffset(const FVector2D& PlanePositionCm, int32 Seed);
	static void ApplyToStamp(FTDFluidBrushStamp& InOutStamp, const FTDFluidInputBrushSettings& Settings, const FTDFluidPlaneBasis& PlaneBasis);
};
