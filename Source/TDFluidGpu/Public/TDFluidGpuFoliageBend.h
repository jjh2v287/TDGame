#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"

class FRDGBuilder;
class FTextureResource;
class FTextureRenderTargetResource;

struct FTDFluidGpuFoliageBendFrameInput
{
	int32 Resolution = 0;
	FIntPoint ShiftCells = FIntPoint::ZeroValue;
	bool bResetsState = true;
	bool bUsesVelocity = true;
	bool bHasWaterSurface = false;
	bool bPreservesAuxRedGreen = false;
	bool bWrapsArea = false;
	float RiseBlend = 0.0f;
	float RecoveryBlend = 0.0f;
	float PainterDensityWeight = 0.0f;
	float RippleSlopeWeight = 0.0f;
	FVector2f RippleGradientScale = FVector2f::ZeroVector;
	FTextureResource* VelocityTexture = nullptr;
	FTextureResource* DensityPressureTexture = nullptr;
	FTextureResource* WaterSurfaceTexture = nullptr;
	FTextureRenderTargetResource* AuxTarget = nullptr;
};

class TDFLUIDGPU_API FTDFluidGpuFoliageBendState
{
public:
	static int64 EstimateStateBytes(int32 Resolution);

	void Release();

	TRefCountPtr<IPooledRenderTarget> BendTargets[2];
	int32 Resolution = 0;
	int32 CurrentIndex = 0;
};

TDFLUIDGPU_API void AddTDFluidGpuFoliageBendPasses(FRDGBuilder& GraphBuilder, FTDFluidGpuFoliageBendState& State, const FTDFluidGpuFoliageBendFrameInput& FrameInput);
