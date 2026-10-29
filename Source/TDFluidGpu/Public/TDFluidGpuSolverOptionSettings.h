#pragma once

#include "CoreMinimal.h"

namespace TDFluidGpuSolverOptionConstants
{
	inline constexpr uint8 KernelFeedbackPressureSolverModeRequest = 3;
	inline constexpr int32 MinimumKernelRadiusTexels = 2;
	inline constexpr int32 MaximumKernelRadiusTexels = 7;
	inline constexpr int32 MinimumKernelDivergenceRadiusTexels = 1;
	inline constexpr int32 MaximumKernelPassesPerProjection = 4;
	inline constexpr uint32 FlowAmplitudeNoiseSeed = 0x9E3779B9u;
	inline constexpr int32 KernelTransientBytesPerCell = 20;
}

struct FTDFluidGpuSolverOptionSettings
{
	int32 KernelRadiusTexels = 4;
	int32 KernelDivergenceRadiusTexels = 2;
	int32 KernelPassesPerProjection = 1;
	float KernelFeedback = 0.98f;
	float KernelScale = 1.0f;
	float FlowAmplitudeNoiseDampingPerSec = 0.0f;
	float FlowAmplitudeNoiseScaleCm = 200.0f;
	FIntPoint NoiseLatticeOffsetCells = FIntPoint::ZeroValue;
	FVector2D AdvectionOffsetPlaneCmPerSec = FVector2D::ZeroVector;
};

enum class ETDFluidGpuSolverOptionFlags : uint32
{
	None = 0,
	UsesKernelPressure = 1u << 0,
	UsesFlowAmplitudeNoise = 1u << 1,
	UsesAdvectionOffset = 1u << 2
};
ENUM_CLASS_FLAGS(ETDFluidGpuSolverOptionFlags);

struct alignas(16) FTDFluidGpuSolverOptionParameters
{
	uint32 OptionFlags = 0;
	uint32 KernelRadiusTexels = 0;
	uint32 KernelDivergenceRadiusTexels = 0;
	float KernelFeedback = 0.0f;
	float KernelDivergenceScale = 0.0f;
	float FlowAmplitudeDampingStep = 0.0f;
	FVector2f AdvectionOffsetCmPerSec = FVector2f::ZeroVector;
	FIntPoint AmplitudeNoiseLatticeOrigin = FIntPoint::ZeroValue;
	FVector2f AmplitudeNoiseLatticeOffset = FVector2f::ZeroVector;
	FVector2f AmplitudeNoiseLatticeStepPerCell = FVector2f::ZeroVector;
	uint32 AmplitudeNoiseLatticePeriodPacked = 0;
	uint32 Padding = 0;
};

static_assert(sizeof(FTDFluidGpuSolverOptionParameters) == 64, "FTDFluidGpuSolverOptionParameters must match the 64 byte shader layout.");
static_assert(offsetof(FTDFluidGpuSolverOptionParameters, AdvectionOffsetCmPerSec) == 24, "FTDFluidGpuSolverOptionParameters::AdvectionOffsetCmPerSec must stay at byte 24.");
static_assert(offsetof(FTDFluidGpuSolverOptionParameters, AmplitudeNoiseLatticeOrigin) == 32, "FTDFluidGpuSolverOptionParameters::AmplitudeNoiseLatticeOrigin must stay at byte 32.");
static_assert(offsetof(FTDFluidGpuSolverOptionParameters, AmplitudeNoiseLatticePeriodPacked) == 56, "FTDFluidGpuSolverOptionParameters::AmplitudeNoiseLatticePeriodPacked must stay at byte 56.");
