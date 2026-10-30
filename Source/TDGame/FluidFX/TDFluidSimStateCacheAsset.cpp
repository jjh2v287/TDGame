#include "FluidFX/TDFluidSimStateCacheAsset.h"

#include "FluidFX/TDFluidSimStateCacheTypes.h"
#include "Misc/Crc.h"

namespace TDFluidSimStateCacheAssetPrivate
{
	constexpr int32 HeatFlagBit = 1 << 0;
	constexpr int32 WaterLayerFlagBit = 1 << 1;
	constexpr int32 Float32WaterVelocityFlagBit = 1 << 2;
	constexpr int32 Float32RippleFlagBit = 1 << 3;
	constexpr int32 WrapsAreaFlagBit = 1 << 4;
	constexpr int32 PainterVelocityFlagBit = 1 << 5;
	constexpr int32 SimModeShift = 8;

	int32 PackGpuLayoutFlags(const FTDFluidGpuStateLayout& Layout)
	{
		int32 Flags = static_cast<int32>(Layout.SimMode) << SimModeShift;
		Flags |= Layout.bUsesHeatChannel ? HeatFlagBit : 0;
		Flags |= Layout.bUsesWaterLayer ? WaterLayerFlagBit : 0;
		Flags |= Layout.bUsesFloat32WaterVelocity ? Float32WaterVelocityFlagBit : 0;
		Flags |= Layout.bUsesFloat32Ripple ? Float32RippleFlagBit : 0;
		Flags |= Layout.bWrapsArea ? WrapsAreaFlagBit : 0;
		Flags |= Layout.bWritesPainterVelocity ? PainterVelocityFlagBit : 0;
		return Flags;
	}

	FTDFluidGpuStateLayout UnpackGpuLayout(int32 ResolutionTier, int32 Flags)
	{
		FTDFluidGpuStateLayout Layout;
		Layout.ResolutionTier = ResolutionTier;
		Layout.SimMode = static_cast<uint8>((Flags >> SimModeShift) & 0xFF);
		Layout.bUsesHeatChannel = (Flags & HeatFlagBit) != 0;
		Layout.bUsesWaterLayer = (Flags & WaterLayerFlagBit) != 0;
		Layout.bUsesFloat32WaterVelocity = (Flags & Float32WaterVelocityFlagBit) != 0;
		Layout.bUsesFloat32Ripple = (Flags & Float32RippleFlagBit) != 0;
		Layout.bWrapsArea = (Flags & WrapsAreaFlagBit) != 0;
		Layout.bWritesPainterVelocity = (Flags & PainterVelocityFlagBit) != 0;
		return Layout;
	}

	template <typename ValueType>
	uint32 AppendChecksum(const ValueType& Value, uint32 Checksum)
	{
		return FCrc::MemCrc32(&Value, sizeof(ValueType), Checksum);
	}
}

bool UTDFluidSimStateCacheAsset::IsUsable() const
{
	if (LayoutVersion != TDFluidSimStateCacheLayoutVersion || Resolution <= 0 || Channels.IsEmpty())
	{
		return false;
	}

	for (const FTDFluidSimStateCacheChannel& Channel : Channels)
	{
		const int64 TexelCount = static_cast<int64>(Channel.Width) * Channel.Height;
		if (Channel.Name.IsNone() || TexelCount <= 0 || Channel.Bytes.IsEmpty() || Channel.Bytes.Num() % TexelCount != 0)
		{
			return false;
		}
	}
	return true;
}

TSharedRef<const FTDFluidSimStateSnapshot, ESPMode::ThreadSafe> UTDFluidSimStateCacheAsset::MakeSnapshot() const
{
	TSharedRef<FTDFluidSimStateSnapshot, ESPMode::ThreadSafe> Snapshot = MakeShared<FTDFluidSimStateSnapshot, ESPMode::ThreadSafe>();
	Snapshot->LayoutVersion = LayoutVersion;
	Snapshot->Backend = Backend;
	Snapshot->Resolution = Resolution;
	Snapshot->SimExtentCm = SimExtentCm;
	Snapshot->CenterPlaneCm = CenterPlaneCm;
	Snapshot->AreaAddressMode = AreaAddressMode;
	Snapshot->SimMode = SimMode;
	Snapshot->bUsesHeatChannel = bUsesHeatChannel;
	Snapshot->bUsesWaterLayer = bUsesWaterLayer;
	Snapshot->SimulationTimeSeconds = SimulationTimeSeconds;
	Snapshot->GpuLayout = TDFluidSimStateCacheAssetPrivate::UnpackGpuLayout(GpuResolutionTier, GpuLayoutFlags);
	Snapshot->Channels.Reserve(Channels.Num());
	for (const FTDFluidSimStateCacheChannel& Channel : Channels)
	{
		FTDFluidStateChannelBytes& ChannelBytes = Snapshot->Channels.AddDefaulted_GetRef();
		ChannelBytes.Name = Channel.Name;
		ChannelBytes.Width = Channel.Width;
		ChannelBytes.Height = Channel.Height;
		ChannelBytes.PixelFormat = Channel.PixelFormat;
		ChannelBytes.Bytes = Channel.Bytes;
	}
	return Snapshot;
}

void UTDFluidSimStateCacheAsset::SetFromSnapshot(const FTDFluidSimStateSnapshot& Snapshot)
{
	LayoutVersion = Snapshot.LayoutVersion;
	Backend = Snapshot.Backend;
	Resolution = Snapshot.Resolution;
	SimExtentCm = Snapshot.SimExtentCm;
	CenterPlaneCm = Snapshot.CenterPlaneCm;
	AreaAddressMode = Snapshot.AreaAddressMode;
	SimMode = Snapshot.SimMode;
	bUsesHeatChannel = Snapshot.bUsesHeatChannel;
	bUsesWaterLayer = Snapshot.bUsesWaterLayer;
	SimulationTimeSeconds = Snapshot.SimulationTimeSeconds;
	GpuResolutionTier = Snapshot.GpuLayout.ResolutionTier;
	GpuLayoutFlags = TDFluidSimStateCacheAssetPrivate::PackGpuLayoutFlags(Snapshot.GpuLayout);
	Channels.Reset(Snapshot.Channels.Num());
	for (const FTDFluidStateChannelBytes& ChannelBytes : Snapshot.Channels)
	{
		FTDFluidSimStateCacheChannel& Channel = Channels.AddDefaulted_GetRef();
		Channel.Name = ChannelBytes.Name;
		Channel.Width = ChannelBytes.Width;
		Channel.Height = ChannelBytes.Height;
		Channel.PixelFormat = ChannelBytes.PixelFormat;
		Channel.Bytes = ChannelBytes.Bytes;
	}
}

int64 UTDFluidSimStateCacheAsset::ComputeChecksum() const
{
	using namespace TDFluidSimStateCacheAssetPrivate;

	uint32 Checksum = AppendChecksum(LayoutVersion, 0);
	Checksum = AppendChecksum(Backend, Checksum);
	Checksum = AppendChecksum(Resolution, Checksum);
	Checksum = AppendChecksum(SimExtentCm, Checksum);
	Checksum = AppendChecksum(CenterPlaneCm, Checksum);
	Checksum = AppendChecksum(AreaAddressMode, Checksum);
	Checksum = AppendChecksum(SimMode, Checksum);
	Checksum = AppendChecksum(bUsesHeatChannel, Checksum);
	Checksum = AppendChecksum(bUsesWaterLayer, Checksum);
	Checksum = AppendChecksum(SimulationTimeSeconds, Checksum);
	Checksum = AppendChecksum(GpuResolutionTier, Checksum);
	Checksum = AppendChecksum(GpuLayoutFlags, Checksum);
	for (const FTDFluidSimStateCacheChannel& Channel : Channels)
	{
		const FString ChannelName = Channel.Name.ToString();
		Checksum = FCrc::StrCrc32(*ChannelName, Checksum);
		Checksum = AppendChecksum(Channel.Width, Checksum);
		Checksum = AppendChecksum(Channel.Height, Checksum);
		Checksum = AppendChecksum(Channel.PixelFormat, Checksum);
		Checksum = FCrc::MemCrc32(Channel.Bytes.GetData(), Channel.Bytes.Num(), Checksum);
	}
	return static_cast<int64>(Checksum);
}

FString TDFluidSimStateCache::DescribeHeaderMismatch(const FTDFluidSimStateSnapshot& Seed, const FTDFluidSimStateSnapshot& Current)
{
	if (Seed.LayoutVersion != Current.LayoutVersion)
	{
		return FString::Printf(TEXT("layout version %d does not match %d"), Seed.LayoutVersion, Current.LayoutVersion);
	}

	if (Seed.Backend != Current.Backend)
	{
		return FString::Printf(TEXT("backend %s does not match %s"), *UEnum::GetValueAsString(Seed.Backend), *UEnum::GetValueAsString(Current.Backend));
	}

	if (Seed.Resolution != Current.Resolution || Current.Resolution <= 0)
	{
		return FString::Printf(TEXT("resolution %d does not match %d"), Seed.Resolution, Current.Resolution);
	}

	const FVector2D HalfCellSizeCm = 0.5 * Current.SimExtentCm / static_cast<double>(Current.Resolution);
	const FVector2D CenterDeltaCm = (Seed.CenterPlaneCm - Current.CenterPlaneCm).GetAbs();
	if (CenterDeltaCm.X > HalfCellSizeCm.X || CenterDeltaCm.Y > HalfCellSizeCm.Y)
	{
		return FString::Printf(TEXT("center %s is more than half a cell from %s"), *Seed.CenterPlaneCm.ToString(), *Current.CenterPlaneCm.ToString());
	}

	const FVector2D ExtentDeltaCm = (Seed.SimExtentCm - Current.SimExtentCm).GetAbs();
	if (ExtentDeltaCm.X > 0.5 || ExtentDeltaCm.Y > 0.5)
	{
		return FString::Printf(TEXT("extent %s does not match %s"), *Seed.SimExtentCm.ToString(), *Current.SimExtentCm.ToString());
	}

	if (Seed.AreaAddressMode != Current.AreaAddressMode)
	{
		return TEXT("area address mode does not match");
	}

	if (Seed.SimMode != Current.SimMode)
	{
		return TEXT("simulation mode does not match");
	}

	if (Seed.bUsesHeatChannel != Current.bUsesHeatChannel)
	{
		return TEXT("heat channel usage does not match");
	}

	if (Seed.bUsesWaterLayer != Current.bUsesWaterLayer)
	{
		return TEXT("water layer usage does not match");
	}
	return FString();
}
