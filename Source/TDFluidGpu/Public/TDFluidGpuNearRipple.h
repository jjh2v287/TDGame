#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"

class FRDGBuilder;
class FTextureResource;
class FTextureRenderTargetResource;

struct FTDFluidGpuNearRippleSource
{
	FVector2f PositionCm = FVector2f::ZeroVector;
	float RadiusCm = 0.0f;
	float ImpulseCmPerSec = 0.0f;
	float IdleWeight = 0.0f;
	float Padding0 = 0.0f;
	float Padding1 = 0.0f;
	float Padding2 = 0.0f;
};

static_assert(sizeof(FTDFluidGpuNearRippleSource) == 32, "FTDFluidGpuNearRippleSource must match the 32 byte shader layout.");

struct FTDFluidGpuNearRippleFrameInput
{
	int32 Resolution = 256;
	float CellSizeCm = 600.0f / 256.0f;
	FIntPoint WindowMinCell = FIntPoint::ZeroValue;
	FIntPoint PreviousWindowMinCell = FIntPoint::ZeroValue;
	bool bResetsState = true;
	int32 SubstepCount = 1;
	float SubstepDeltaSeconds = 0.0f;
	float WaveSpeedCmPerSec = 40.0f;
	float DampingPerSec = 2.0f;
	float EdgeFadeRatio = 0.15f;
	float EdgeAbsorptionStrength = 4.0f;
	float IdleContactAmplitudeCm = 0.0f;
	double IdleContactStartPhase = 0.0;
	double IdleContactPhasePerSubstep = 0.0;
	float MinWaterDepthCm = 1.0f;
	FVector2f MainWindowCenterFromOriginCm = FVector2f::ZeroVector;
	FVector2f MainWindowExtentCm = FVector2f::ZeroVector;
	float MainWaterDepthReferenceCm = 300.0f;
	float ObstacleDistanceOutputRangeCm = 400.0f;
	bool bHasObstacleDistanceAux = false;
	FTextureResource* WaterSurfaceTexture = nullptr;
	FTextureResource* AuxTexture = nullptr;
	FTextureRenderTargetResource* OutputTarget = nullptr;
	TArray<FTDFluidGpuNearRippleSource> Sources;
};

class TDFLUIDGPU_API FTDFluidGpuNearRippleState
{
public:
	static int64 EstimateStateBytes(int32 Resolution);

	void Release();

	TRefCountPtr<IPooledRenderTarget> HeightVelocityTargets[2];
	int32 Resolution = 0;
	int32 CurrentIndex = 0;
};

TDFLUIDGPU_API void AddTDFluidGpuNearRipplePasses(FRDGBuilder& GraphBuilder, FTDFluidGpuNearRippleState& State, const FTDFluidGpuNearRippleFrameInput& FrameInput);
