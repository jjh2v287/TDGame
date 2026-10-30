#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidMassInputTargets.h"
#include "FluidFX/TDFluidRainTypes.h"

struct FTDFluidRainImpactState
{
	double ExpectedDropCarry = 0.0;
	double ClockSeconds = 0.0;
	uint32 DropCounter = 0;
	float PuddleLevelCm = 0.0f;
	int32 LastQueuedDropCount = 0;
	int32 LastCappedDropCount = 0;
};

struct TDGAME_API FTDFluidRainImpacts
{
	static uint32 HashUint32(uint32 Value);
	static uint32 HashCombine3(uint32 First, uint32 Second, uint32 Third);
	static float HashToUnitFloat(uint32 Hash);
	static float SanitizeIntensity(float Intensity);
	static int32 GenerateImpacts(const FTDFluidRainSettings& Settings, float Intensity, float DeltaSeconds, const FVector& WindowCenterWorld, const FVector2D& WindowExtentCm, int32 MaxCount, FTDFluidRainImpactState& InOutState, TArray<FTDFluidMassInputPoint>& OutPoints);
	static float AdvancePuddleLevel(const FTDFluidRainSettings& Settings, float Intensity, float DeltaSeconds, float LevelCm);
};
