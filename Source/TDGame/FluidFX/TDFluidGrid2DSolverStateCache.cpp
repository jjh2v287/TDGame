#include "FluidFX/TDFluidGrid2DSolver.h"

#include "FluidFX/TDFluidSimStateCacheTypes.h"
#include "PixelFormat.h"

namespace TDFluidGrid2DSolverStateCachePrivate
{
	const FName PrimaryDensityChannelName(TEXT("PrimaryDensity"));
	const FName SelectiveDensityChannelName(TEXT("SelectiveDensity"));
	const FName SurfaceDisplacementChannelName(TEXT("SurfaceDisplacement"));
	const FName VelocityXChannelName(TEXT("VelocityX"));
	const FName VelocityYChannelName(TEXT("VelocityY"));
	const FName PressureChannelName(TEXT("Pressure"));
	const FName PressureSolutionChannelName(TEXT("PressureSolution"));
	const FName HeatChannelName(TEXT("Heat"));
	const FName WetnessChannelName(TEXT("Wetness"));
	const FName RippleHeightChannelName(TEXT("RippleHeight"));
	const FName RippleHeightVelocityChannelName(TEXT("RippleHeightVelocity"));

	template <typename FieldPointerType>
	struct TStateCacheField
	{
		FName Name;
		FieldPointerType Field;
	};

	bool HasAnyNonZeroValue(const TArray<float>& Field)
	{
		return Field.ContainsByPredicate([](float Value) { return Value != 0.0f; });
	}

	FString DescribeChannelMismatch(const FTDFluidStateChannelBytes* Channel, FName ExpectedName, int32 ExpectedResolution)
	{
		if (!Channel)
		{
			return FString::Printf(TEXT("missing channel %s"), *ExpectedName.ToString());
		}

		const int32 ExpectedByteCount = ExpectedResolution * ExpectedResolution * static_cast<int32>(sizeof(float));
		if (Channel->Width != ExpectedResolution || Channel->Height != ExpectedResolution || Channel->PixelFormat != PF_R32_FLOAT || Channel->Bytes.Num() != ExpectedByteCount)
		{
			return FString::Printf(TEXT("channel %s is %dx%d format %d with %d bytes, expected %dx%d float32"), *ExpectedName.ToString(), Channel->Width, Channel->Height, Channel->PixelFormat, Channel->Bytes.Num(), ExpectedResolution, ExpectedResolution);
		}
		return FString();
	}
}

void FTDFluidGrid2DSolver::CaptureStateSnapshot(FTDFluidSimStateSnapshot& OutSnapshot) const
{
	using namespace TDFluidGrid2DSolverStateCachePrivate;

	const bool bUsesHeatChannel = !Heat.IsEmpty();
	const bool bUsesWaterLayer = IsWaterLayerActive();
	OutSnapshot = FTDFluidSimStateSnapshot();
	OutSnapshot.Backend = ETDFluidSimStateCacheBackend::Cpu;
	OutSnapshot.Resolution = Resolution;
	OutSnapshot.SimExtentCm = Config.SimExtentCm;
	OutSnapshot.CenterPlaneCm = CenterWorld;
	OutSnapshot.AreaAddressMode = Config.AreaAddressMode;
	OutSnapshot.SimMode = Config.SimMode;
	OutSnapshot.bUsesHeatChannel = bUsesHeatChannel;
	OutSnapshot.bUsesWaterLayer = bUsesWaterLayer;
	OutSnapshot.bUsesDenseWater = IsDenseWaterActive();
	OutSnapshot.SimulationTimeSeconds = SimulationTimeSeconds;

	const TStateCacheField<const TArray<float>*> Fields[] = {
		{PrimaryDensityChannelName, &PrimaryDensity},
		{SelectiveDensityChannelName, &SelectiveDensity},
		{SurfaceDisplacementChannelName, &SurfaceDisplacement},
		{VelocityXChannelName, &VelocityX},
		{VelocityYChannelName, &VelocityY},
		{PressureChannelName, &Pressure},
		{PressureSolutionChannelName, &PressureSolution},
		{HeatChannelName, bUsesHeatChannel ? &Heat : nullptr},
		{WetnessChannelName, bUsesWaterLayer ? &Wetness : nullptr},
		{RippleHeightChannelName, bUsesWaterLayer ? &RippleHeight : nullptr},
		{RippleHeightVelocityChannelName, bUsesWaterLayer ? &RippleHeightVelocity : nullptr},
	};

	for (const TStateCacheField<const TArray<float>*>& Entry : Fields)
	{
		if (!Entry.Field)
		{
			continue;
		}

		FTDFluidStateChannelBytes& Channel = OutSnapshot.Channels.AddDefaulted_GetRef();
		Channel.Name = Entry.Name;
		Channel.Width = Resolution;
		Channel.Height = Resolution;
		Channel.PixelFormat = static_cast<uint8>(PF_R32_FLOAT);
		Channel.Bytes.SetNumUninitialized(Entry.Field->Num() * sizeof(float));
		FMemory::Memcpy(Channel.Bytes.GetData(), Entry.Field->GetData(), Channel.Bytes.Num());
	}
}

bool FTDFluidGrid2DSolver::RestoreStateSnapshot(const FTDFluidSimStateSnapshot& Snapshot, FString& OutReason)
{
	using namespace TDFluidGrid2DSolverStateCachePrivate;

	OutReason.Reset();
	if (!IsInitialized())
	{
		OutReason = TEXT("solver is not initialized");
		return false;
	}

	const bool bUsesHeatChannel = !Heat.IsEmpty();
	const bool bUsesWaterLayer = IsWaterLayerActive();
	FTDFluidSimStateSnapshot CurrentHeader;
	CurrentHeader.Backend = ETDFluidSimStateCacheBackend::Cpu;
	CurrentHeader.Resolution = Resolution;
	CurrentHeader.SimExtentCm = Config.SimExtentCm;
	CurrentHeader.CenterPlaneCm = CenterWorld;
	CurrentHeader.AreaAddressMode = Config.AreaAddressMode;
	CurrentHeader.SimMode = Config.SimMode;
	CurrentHeader.bUsesHeatChannel = bUsesHeatChannel;
	CurrentHeader.bUsesWaterLayer = bUsesWaterLayer;
	CurrentHeader.bUsesDenseWater = IsDenseWaterActive();
	OutReason = TDFluidSimStateCache::DescribeHeaderMismatch(Snapshot, CurrentHeader);
	if (!OutReason.IsEmpty())
	{
		return false;
	}

	const TStateCacheField<TArray<float>*> Fields[] = {
		{PrimaryDensityChannelName, &PrimaryDensity},
		{SelectiveDensityChannelName, &SelectiveDensity},
		{SurfaceDisplacementChannelName, &SurfaceDisplacement},
		{VelocityXChannelName, &VelocityX},
		{VelocityYChannelName, &VelocityY},
		{PressureChannelName, &Pressure},
		{PressureSolutionChannelName, &PressureSolution},
		{HeatChannelName, bUsesHeatChannel ? &Heat : nullptr},
		{WetnessChannelName, bUsesWaterLayer ? &Wetness : nullptr},
		{RippleHeightChannelName, bUsesWaterLayer ? &RippleHeight : nullptr},
		{RippleHeightVelocityChannelName, bUsesWaterLayer ? &RippleHeightVelocity : nullptr},
	};

	TArray<TPair<TArray<float>*, const FTDFluidStateChannelBytes*>, TInlineAllocator<UE_ARRAY_COUNT(Fields)>> Copies;
	for (const TStateCacheField<TArray<float>*>& Entry : Fields)
	{
		if (!Entry.Field)
		{
			continue;
		}

		const FTDFluidStateChannelBytes* Channel = Snapshot.FindChannel(Entry.Name);
		OutReason = DescribeChannelMismatch(Channel, Entry.Name, Resolution);
		if (!OutReason.IsEmpty())
		{
			return false;
		}
		Copies.Emplace(Entry.Field, Channel);
	}

	if (Copies.Num() != Snapshot.Channels.Num())
	{
		OutReason = FString::Printf(TEXT("snapshot has %d channels, expected %d"), Snapshot.Channels.Num(), Copies.Num());
		return false;
	}

	for (const TPair<TArray<float>*, const FTDFluidStateChannelBytes*>& Copy : Copies)
	{
		FMemory::Memcpy(Copy.Key->GetData(), Copy.Value->Bytes.GetData(), Copy.Value->Bytes.Num());
	}

	SimulationTimeSeconds = Snapshot.SimulationTimeSeconds;
	if (bUsesWaterLayer)
	{
		bWaterVelocityInitPending = false;
		bIsWetnessUpdating = Config.bEnableWaterWetness && IsWaterWetnessConsoleEnabled();
		bHasRippleActivity = HasAnyNonZeroValue(RippleHeight) || HasAnyNonZeroValue(RippleHeightVelocity);
	}
	return true;
}
