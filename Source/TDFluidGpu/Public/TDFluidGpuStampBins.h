#pragma once

#include "CoreMinimal.h"
#include "Misc/EnumClassFlags.h"
#include "TDFluidGpuInputs.h"

enum class ETDFluidGpuStampFlags : uint32
{
	None = 0,
	WritesSelectiveDensity = 1u << 0,
	ErasesDensity = 1u << 1,
	WritesHeat = 1u << 2
};
ENUM_CLASS_FLAGS(ETDFluidGpuStampFlags);

struct alignas(16) FTDFluidGpuPackedStamp
{
	FVector2f CenterFromWindowCenterCm = FVector2f::ZeroVector;
	float EffectiveRadiusCm = 0.0f;
	float DensityStrength = 0.0f;

	FVector2f TargetVelocityCmPerSec = FVector2f::ZeroVector;
	float VelocityStrength = 0.0f;
	float RadialVelocityCmPerSec = 0.0f;

	uint32 Flags = 0;
	uint32 SourceIndex = 0;
	float EraseRatePerSec = 0.0f;
	float HeatStrength = 0.0f;
};
static_assert(sizeof(FTDFluidGpuPackedStamp) == 48, "FTDFluidGpuPackedStamp must match the 48 byte shader layout.");
static_assert(offsetof(FTDFluidGpuPackedStamp, TargetVelocityCmPerSec) == 16, "FTDFluidGpuPackedStamp::TargetVelocityCmPerSec must stay at byte 16.");
static_assert(offsetof(FTDFluidGpuPackedStamp, Flags) == 32, "FTDFluidGpuPackedStamp::Flags must stay at byte 32.");

struct FTDFluidGpuStampBinRange
{
	uint32 FirstIndex = 0;
	uint32 Count = 0;
};
static_assert(sizeof(FTDFluidGpuStampBinRange) == 8, "FTDFluidGpuStampBinRange must match the 8 byte shader layout.");

struct FTDFluidGpuStampBinWindow
{
	FVector2D CenterPlaneCm = FVector2D::ZeroVector;
	FVector2D ExtentCm = FVector2D(4000.0, 4000.0);
	int32 Resolution = 0;
};

struct FTDFluidGpuStampBinLimits
{
	int32 MaxStampsPerTick = TDFluidGpuSolverConstants::MaximumStampsPerTick;
	int32 MaxStampsPerBin = TDFluidGpuSolverConstants::MaximumStampsPerBin;
};

struct FTDFluidGpuStampBinSet
{
	TArray<FTDFluidGpuPackedStamp> Stamps;
	TArray<FTDFluidGpuStampBinRange> BinRanges;
	TArray<uint32> BinStampIndices;
	int32 NonFiniteStampCount = 0;
	int32 OutsideWindowStampCount = 0;
	int32 OverTickLimitStampCount = 0;
	int32 BinOverflowEntryCount = 0;
};

struct TDFLUIDGPU_API FTDFluidGpuStampBins
{
	static FTDFluidGpuStampBinSet Build(const FTDFluidGpuStampBinWindow& Window, TConstArrayView<FTDFluidGpuStampInput> Stamps, const FTDFluidGpuStampBinLimits& Limits);
	static uint32 AppendToFrame(const FTDFluidGpuStampBinSet& ActorBins, FTDFluidGpuStampBinSet& InOutFrameBins);
	static int32 GetBinSpanCells(int32 Resolution);
	static float ComputeEffectiveRadiusCm(const FTDFluidGpuStampBinWindow& Window, float RadiusCm);
	static float ComputePriorityDistanceSquared(const FTDFluidGpuStampBinWindow& Window, const FTDFluidGpuStampInput& Stamp);
};
