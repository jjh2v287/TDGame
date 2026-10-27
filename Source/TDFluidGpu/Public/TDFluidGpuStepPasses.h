#pragma once

#include "CoreMinimal.h"
#include "Math/Float16Color.h"
#include "RenderGraphFwd.h"
#include "Templates/UniquePtr.h"
#include "TDFluidGpuBatch.h"
#include "TDFluidGpuInputs.h"
#include "TDFluidGpuMassInput.h"

class FRHIGPUTextureReadback;
class FTDFluidGpuMassInputRegistry;
class FRHIGPUBufferReadback;

struct FTDFluidGpuFrameOutputs
{
	FRDGTextureRef ReadbackAtlas = nullptr;
	FTDFluidGpuReadbackAtlasLayout ReadbackLayout;
	uint32 FrameNumber = 0;
	int32 SteppedEntryCount = 0;
	int32 WrittenOutputCount = 0;
	int32 SkippedOutputCount = 0;
	FRDGBufferRef WaterProbeSamples = nullptr;
	int32 WaterProbeSlotCount = 0;
	FRDGBufferRef MassInputCounterSnapshot = nullptr;
	TArray<uint64> MassInputCounterActorKeys;
};

TDFLUIDGPU_API FTDFluidGpuFrameOutputs AddTDFluidGpuFramePasses(FRDGBuilder& GraphBuilder, const FTDFluidGpuFrameInputs& FrameInputs, TArray<FTDFluidGpuBatch>& InOutBatches, FTDFluidGpuMassInputRegistry* MassInputRegistry = nullptr);

TDFLUIDGPU_API int64 ComputeTDFluidGpuBatchStateBytes(const FTDFluidGpuBatch& Batch);

TDFLUIDGPU_API int32 ComputeTDFluidGpuWaterHalfPostSmoothIterations();

struct FTDFluidGpuReadbackFrame
{
	uint32 FrameNumber = 0;
	FTDFluidGpuReadbackAtlasLayout Layout;
	TArray<FFloat16Color> Texels;
	TArray<FTDFluidGpuWaterProbeSample> WaterProbeSamples;
	TArray<FTDFluidGpuMassInputCounters> MassInputCounters;
};

class TDFLUIDGPU_API FTDFluidGpuReadbackRing
{
public:
	FTDFluidGpuReadbackRing();
	~FTDFluidGpuReadbackRing();

	bool Enqueue(FRDGBuilder& GraphBuilder, const FTDFluidGpuFrameOutputs& FrameOutputs);
	bool TryReadLatest(FTDFluidGpuReadbackFrame& OutFrame);
	int32 GetPendingCount() const;
	int32 GetDroppedCount() const { return DroppedCount; }

private:
	struct FTDFluidGpuReadbackSlot
	{
		TUniquePtr<FRHIGPUTextureReadback> Readback;
		FTDFluidGpuReadbackAtlasLayout Layout;
		uint32 FrameNumber = 0;
		uint64 EnqueueOrder = 0;
		bool bIsPending = false;
		TUniquePtr<FRHIGPUBufferReadback> WaterProbeReadback;
		int32 WaterProbeSlotCount = 0;
		TUniquePtr<FRHIGPUBufferReadback> MassInputCounterReadback;
		TArray<uint64> MassInputCounterActorKeys;
	};

	FTDFluidGpuReadbackSlot Slots[TDFluidGpuSolverConstants::ReadbackRingSize];
	uint64 NextEnqueueOrder = 1;
	int32 DroppedCount = 0;
};
