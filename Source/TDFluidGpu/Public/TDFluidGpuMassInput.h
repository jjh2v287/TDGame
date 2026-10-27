#pragma once

#include "CoreMinimal.h"
#include "RenderGraphResources.h"
#include "TDFluidGpuInputs.h"

struct alignas(16) FTDFluidGpuPackedMassPoint
{
	FVector2f LocalCm = FVector2f::ZeroVector;
	FVector2f VelocityPlaneCmPerSec = FVector2f::ZeroVector;
	float RadiusCm = 0.0f;
	float DensityTarget = 0.0f;
	float HeatTarget = 0.0f;
	uint32 EntryIndexAndFlags = 0;
};
static_assert(sizeof(FTDFluidGpuPackedMassPoint) == 32, "FTDFluidGpuPackedMassPoint must match the 32 byte shader layout.");

struct alignas(16) FTDFluidGpuMassInputEntry
{
	uint32 AccumulationBase = 0;
	uint32 CounterBase = 0;
	uint32 InputResolution = 0;
	uint32 Flags = 0;
	FVector2f ShiftOffsetCm = FVector2f::ZeroVector;
	FVector2f InputCellCm = FVector2f::ZeroVector;
	uint32 SafeCount = 0;
	uint32 RadiusCapCells = 0;
	uint32 Padding0 = 0;
	uint32 Padding1 = 0;
};
static_assert(sizeof(FTDFluidGpuMassInputEntry) == 48, "FTDFluidGpuMassInputEntry must match the 48 byte shader layout.");

enum class ETDFluidGpuMassInputEntryFlags : uint32
{
	None = 0,
	AppliesAccumulation = 1u << 0,
	WritesSelectiveDensity = 1u << 1,
	WrapsArea = 1u << 2
};
ENUM_CLASS_FLAGS(ETDFluidGpuMassInputEntryFlags);

struct FTDFluidGpuMassInputCounters
{
	uint64 ActorKey = 0;
	uint32 SplatCount = 0;
	uint32 OutsideCount = 0;
	bool bSaturated = false;
	uint32 FrameNumber = 0;
};

struct FTDFluidGpuMassInputSample
{
	float Density = 0.0f;
	float Heat = 0.0f;
	FVector2D WeightedVelocity = FVector2D::ZeroVector;
	float Weight = 0.0f;
};

struct FTDFluidGpuMassInputBatchState
{
	TRefCountPtr<FRDGPooledBuffer> Accumulation;
	TRefCountPtr<FRDGPooledBuffer> Counters;
	int32 SliceCapacity = 0;
	int32 InputResolution = 0;
	TArray<FVector2D> SliceOriginPlaneCm;
	TArray<bool> SliceHasOrigin;
};

struct FTDFluidGpuMassInputTargetView
{
	TRefCountPtr<FRDGPooledBuffer> Accumulation;
	TRefCountPtr<FRDGPooledBuffer> Counters;
	uint32 AccumulationBase = 0;
	uint32 CounterBase = 0;
	int32 InputResolution = 0;
	FVector2D InputCellCm = FVector2D::ZeroVector;
	FVector2D OriginPlaneCm = FVector2D::ZeroVector;
	int32 RadiusCapCells = TDFluidGpuSolverConstants::DefaultMassSplatRadiusCapCells;
	bool bWrapsArea = false;
	bool bIsReady = false;
};

class TDFLUIDGPU_API FTDFluidGpuMassInputRegistry
{
public:
	bool FindTarget(uint64 ActorKey, FTDFluidGpuMassInputTargetView& OutView) const;
	void UpdateTarget(uint64 ActorKey, const FTDFluidGpuMassInputTargetView& View);
	void RemoveTarget(uint64 ActorKey);
	void MarkAllTargetsNotReady();
	int32 GetTargetCount() const { return Targets.Num(); }

private:
	TMap<uint64, FTDFluidGpuMassInputTargetView> Targets;
};

namespace TDFluidGpuMassInput
{
	TDFLUIDGPU_API int32 ResolveDivisor(int32 RequestedDivisor);
	TDFLUIDGPU_API int32 ComputeInputResolution(int32 ResolutionTier, int32 Divisor);
	TDFLUIDGPU_API uint32 QuantizeUnit(float Value);
	TDFLUIDGPU_API int32 QuantizeVelocity(float Value);
	TDFLUIDGPU_API uint32 QuantizeWeight(float Weight);
	TDFLUIDGPU_API uint32 ComputeSafeCount(float MaxCellSpeedCmPerSec);
	TDFLUIDGPU_API float ResolveSplatRadius(float RadiusCm, const FVector2D& InputCellCm, int32 RadiusCapCells);
	TDFLUIDGPU_API bool SplatPointReference(TArray<uint32>& InOutAccumulation, int32 InputResolution, const FVector2D& InputCellCm, bool bWrapsArea, const FVector2D& LocalCm, const FVector2D& VelocityPlaneCmPerSec, float RadiusCm, float DensityTarget, float HeatTarget, bool bWritesVelocity);
	TDFLUIDGPU_API FTDFluidGpuMassInputSample SampleReference(const TArray<uint32>& Accumulation, int32 InputResolution, const FVector2D& InputCellCm, bool bWrapsArea, const FVector2D& SamplePositionCm);
	TDFLUIDGPU_API int64 ComputeSliceBytes(int32 ResolutionTier, int32 Divisor);
}
