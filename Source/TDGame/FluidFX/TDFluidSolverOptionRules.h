#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "FluidFX/TDFluidTypes.h"
#include "TDFluidGpuSolverOptionSettings.h"

struct FTDFluidAdvectionOffsetInputs
{
	const FTDFluidSimulationConfig* Config = nullptr;
	FTDFluidPlaneBasis PlaneBasis;
	FVector2D CurrentCenterPlane = FVector2D::ZeroVector;
	FVector2D PreviousCenterPlane = FVector2D::ZeroVector;
	bool bHasPreviousCenter = false;
	float DeltaTimeSeconds = 0.0f;
};

namespace TDFluidSolverOptionRules
{
	TDGAME_API void SanitizeSolverOptions(FTDFluidSimulationConfig& InOutConfig);
	TDGAME_API ETDFluidPressureMethod ResolveCpuPressureMethod(const FTDFluidSimulationConfig& Config);
	TDGAME_API bool CanUseKernelPressure(const FTDFluidSimulationConfig& Config);
	TDGAME_API FTDFluidGpuSolverOptionSettings MakeGpuSolverOptionSettings(const FTDFluidSimulationConfig& Config);
	TDGAME_API FVector2D ComputeAdvectionOffsetPlane(const FTDFluidAdvectionOffsetInputs& Inputs);
	TDGAME_API FIntPoint ComputeNoiseLatticeOffsetCells(const FVector& SpawnLocationCm, ETDFluidPresetKind Kind, const FString& OwnerName);
	TDGAME_API bool IsDensityAnchoredToActor(const FTDFluidSimulationConfig& Config);
	TDGAME_API bool IsNoiseLatticeOffsetEnabled(const FTDFluidSimulationConfig& Config);
}
