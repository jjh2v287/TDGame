#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidWaterParticleTypes.h"

struct FTDFluidBrushStamp;
struct FTDFluidMassInputPoint;

struct TDGAME_API FTDFluidWaterSplashRules
{
	static float ResolveSplashRadiusCm(const FTDFluidWaterParticleSettings& Settings, float SourceRadiusCm);
	static bool EvaluateSplashPoint(const FTDFluidWaterParticleSettings& Settings, const FVector& Location, float RadiusCm, float SurfaceHeightCm, float DeltaSeconds, FTDFluidWaterSplashPointState& InOutState, FTDFluidWaterSplashEvent& OutEvent);
	static void SortAndClampSplashEvents(TArray<FTDFluidWaterSplashEvent>& InOutEvents, const FVector& CenterWorld, int32 MaxEvents);
	static int32 ResolveContactStampBudget(const FTDFluidWaterParticleSettings& Settings, int32 MaxPointsPerTick, int32 PendingCount);
	static void BuildContactWhitewaterStamps(TArray<FTDFluidMassInputPoint>& InOutPoints, const FVector& CenterWorld, int32 MaxStamps, float FallbackRadiusCm, TArray<FTDFluidBrushStamp>& OutStamps);
	static bool TryResolveFieldCell(const FVector2D& PlanePoint, const FVector2D& FirstCellCenterPlane, double CellSizeCm, int32 Resolution, FIntPoint& OutCell);
};
