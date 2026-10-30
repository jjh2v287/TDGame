#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidSimStateCacheAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "TDFluidGpuStateCacheTypes.h"

inline constexpr int32 TDFluidSimStateCacheLayoutVersion = 2;

struct FTDFluidSimStateSnapshot
{
	int32 LayoutVersion = TDFluidSimStateCacheLayoutVersion;
	ETDFluidSimStateCacheBackend Backend = ETDFluidSimStateCacheBackend::Cpu;
	int32 Resolution = 0;
	FVector2D SimExtentCm = FVector2D::ZeroVector;
	FVector2D CenterPlaneCm = FVector2D::ZeroVector;
	ETDFluidAreaAddressMode AreaAddressMode = ETDFluidAreaAddressMode::Clamp;
	ETDFluidSimMode SimMode = ETDFluidSimMode::FluidSim;
	bool bUsesHeatChannel = false;
	bool bUsesWaterLayer = false;
	bool bUsesDenseWater = false;
	double SimulationTimeSeconds = 0.0;
	FTDFluidGpuStateLayout GpuLayout;
	TArray<FTDFluidStateChannelBytes> Channels;

	const FTDFluidStateChannelBytes* FindChannel(FName ChannelName) const
	{
		return Channels.FindByPredicate([ChannelName](const FTDFluidStateChannelBytes& Channel) { return Channel.Name == ChannelName; });
	}
};

namespace TDFluidSimStateCache
{
	TDGAME_API FString DescribeDenseWaterRejection(const FTDFluidSimStateSnapshot& Snapshot);
	TDGAME_API FString DescribeHeaderMismatch(const FTDFluidSimStateSnapshot& Seed, const FTDFluidSimStateSnapshot& Current);
	TDGAME_API bool IsCacheConsoleEnabled();
}
