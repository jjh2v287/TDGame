#pragma once

#include "CoreMinimal.h"
#include "TDFluidGpuBatch.h"

TDFLUIDGPU_API FTDFluidGpuSolverOptionParameters MakeTDFluidGpuSolverOptionParameters(const FTDFluidGpuSimulationSettings& Settings, const FVector2D& WindowCenter, float StepDeltaTimeSeconds, int32 Resolution);
TDFLUIDGPU_API bool CanTDFluidGpuUseKernelPressure(const FTDFluidGpuSimulationSettings& Settings);
TDFLUIDGPU_API float ComputeTDFluidKernelWeight(int32 Offset, int32 RadiusTexels);
TDFLUIDGPU_API float ComputeTDFluidKernelDivergenceScale(int32 RadiusTexels, float Scale);
TDFLUIDGPU_API bool DoesTDFluidGpuFrameUseSolverOption(const FTDFluidGpuBatchFrame& Frame, ETDFluidGpuSolverOptionFlags Flag);
