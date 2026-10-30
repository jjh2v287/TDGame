#pragma once

#include "CoreMinimal.h"

struct FTDFluidStateChannelBytes
{
	FName Name;
	int32 Width = 0;
	int32 Height = 0;
	uint8 PixelFormat = 0;
	TArray<uint8> Bytes;
};

struct FTDFluidGpuStateLayout
{
	int32 ResolutionTier = 0;
	uint8 SimMode = 0;
	bool bUsesHeatChannel = false;
	bool bUsesWaterLayer = false;
	bool bUsesFloat32WaterVelocity = false;
	bool bUsesFloat32Ripple = false;
	bool bWrapsArea = false;
	bool bWritesPainterVelocity = false;
	bool bUsesDenseWater = false;

	bool operator==(const FTDFluidGpuStateLayout& Other) const = default;
};

struct FTDFluidGpuStateSeedRequest
{
	uint64 ActorKey = 0;
	FTDFluidGpuStateLayout Layout;
	TSharedPtr<const TArray<FTDFluidStateChannelBytes>, ESPMode::ThreadSafe> Channels;
};
