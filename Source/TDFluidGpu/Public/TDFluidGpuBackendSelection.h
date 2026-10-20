#pragma once

#include "CoreMinimal.h"

enum class ETDFluidGpuSolverPreference : uint8
{
	Cpu,
	Gpu,
	Auto
};

enum class ETDFluidGpuBackendOverride : uint8
{
	FollowPreset,
	ForceGpuWhenPossible,
	ForceCpu
};

struct FTDFluidGpuBackendEnvironment
{
	bool bHasRenderingRhi = false;
	bool bSupportsShaderModel5 = false;
	ETDFluidGpuBackendOverride Override = ETDFluidGpuBackendOverride::FollowPreset;
};

struct FTDFluidGpuBackendQuery
{
	ETDFluidGpuSolverPreference Preference = ETDFluidGpuSolverPreference::Cpu;
	bool bIsWaterLayerActive = false;
	int32 RequestedResolution = 64;
	int32 CpuFallbackResolution = 0;
};

struct FTDFluidGpuBackendDecision
{
	bool bUsesGpu = false;
	int32 Resolution = 0;
};

struct TDFLUIDGPU_API FTDFluidGpuBackendSelection
{
	static FTDFluidGpuBackendEnvironment CaptureEnvironment();
	static FTDFluidGpuBackendDecision Decide(const FTDFluidGpuBackendQuery& Query, const FTDFluidGpuBackendEnvironment& Environment);
};
