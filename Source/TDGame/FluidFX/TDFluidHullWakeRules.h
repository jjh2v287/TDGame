#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidTypes.h"

struct TDGAME_API FTDFluidHullWakeRules
{
	static constexpr int32 MinHullStampCount = 2;
	static constexpr int32 MaxHullStampCount = 32;

	static FVector2D ResolveHullLengthAndBeam(const FVector& LocalBoundsSizeCm, float LengthCm, float BeamCm);
	static void BuildHullStamps(const FTransform& HullTransform, const FVector& VelocityCmPerSec, float LengthCm, float BeamCm, ETDFluidBrushChannel Channel, const FTDFluidInputBrushSettings& Settings, TArray<FTDFluidBrushStamp>& OutStamps);
};
