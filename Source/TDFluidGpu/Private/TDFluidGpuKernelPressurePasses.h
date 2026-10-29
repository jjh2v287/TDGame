#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "TDFluidGpuBatch.h"

class FGlobalShaderMap;

struct FTDFluidGpuKernelPressurePassInputs
{
	FRDGBufferSRVRef ActorParameters = nullptr;
	FRDGBufferSRVRef SolverOptionParameters = nullptr;
	FRDGTextureRef ObstacleSdf = nullptr;
	FRDGTextureRef ProjectedVelocity = nullptr;
	FRDGTextureRef PressureTextures[2] = { nullptr, nullptr };
	int32 PressureIndex = 0;
	int32 Resolution = 0;
	int32 EntryCount = 0;
	int32 SliceCapacity = 0;
	int32 PassesPerProjection = 1;
};

int32 AddTDFluidGpuKernelPressurePasses(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, const FTDFluidGpuKernelPressurePassInputs& Inputs);
FRDGBufferSRVRef CreateTDFluidGpuSolverOptionParametersSRV(FRDGBuilder& GraphBuilder, const FTDFluidGpuBatchFrame& Frame);
