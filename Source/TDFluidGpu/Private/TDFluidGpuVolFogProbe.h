#pragma once

#include "CoreMinimal.h"
#include "RHIGPUReadback.h"
#include "RenderGraphFwd.h"
#include "RendererInterface.h"
#include "Templates/UniquePtr.h"

class UTextureRenderTargetVolume;

constexpr int32 TDFluidGpuVolFogProbePatternCount = 4;

struct FTDFluidGpuVolFogProbeState
{
	FIntVector GridSize = FIntVector::ZeroValue;
	TRefCountPtr<IPooledRenderTarget> Velocity[2];
	TRefCountPtr<IPooledRenderTarget> Pressure[2];
	TRefCountPtr<IPooledRenderTarget> Divergence;
	TRefCountPtr<IPooledRenderTarget> ObstacleMask;
};

struct FTDFluidGpuVolFogProbeResidualReadbacks
{
	TUniquePtr<FRHIGPUTextureReadback> BeforePressure;
	TUniquePtr<FRHIGPUTextureReadback> AfterPressure;
};

void AddTDFluidGpuVolFogProbeStepPasses(FRDGBuilder& GraphBuilder, FTDFluidGpuVolFogProbeState& State, const FIntVector& GridSize, int32 PressureIterationCount, FTDFluidGpuVolFogProbeResidualReadbacks* ResidualReadbacks = nullptr);
void AddTDFluidGpuVolFogProbePatternPass(FRDGBuilder& GraphBuilder, FRDGTextureRef DensityTexture, int32 PatternIndex);
bool WriteTDFluidGpuVolFogProbePattern(UTextureRenderTargetVolume* Target, int32 PatternIndex);
