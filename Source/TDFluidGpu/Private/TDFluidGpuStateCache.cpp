#include "TDFluidGpuStateCache.h"

#include "PixelFormat.h"
#include "PooledRenderTarget.h"
#include "RHICommandList.h"
#include "RHIGPUReadback.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

namespace
{
	const FTDFluidGpuStateCacheChannel TDFluidStateCacheChannelTable[] =
	{
		{ TEXT("Velocity"), [](FTDFluidGpuBatchStateTextures& State) -> TRefCountPtr<IPooledRenderTarget>& { return State.Velocity[0]; } },
		{ TEXT("Density"), [](FTDFluidGpuBatchStateTextures& State) -> TRefCountPtr<IPooledRenderTarget>& { return State.Density[0]; } },
		{ TEXT("SurfaceDisplacement"), [](FTDFluidGpuBatchStateTextures& State) -> TRefCountPtr<IPooledRenderTarget>& { return State.SurfaceDisplacement[0]; } },
		{ TEXT("PressureSolution"), [](FTDFluidGpuBatchStateTextures& State) -> TRefCountPtr<IPooledRenderTarget>& { return State.PressureSolution[0]; } },
		{ TEXT("Heat"), [](FTDFluidGpuBatchStateTextures& State) -> TRefCountPtr<IPooledRenderTarget>& { return State.Heat[0]; } },
		{ TEXT("PainterPrimaryDensity"), [](FTDFluidGpuBatchStateTextures& State) -> TRefCountPtr<IPooledRenderTarget>& { return State.PainterPrimaryDensity; } },
		{ TEXT("PainterSelectiveDensity"), [](FTDFluidGpuBatchStateTextures& State) -> TRefCountPtr<IPooledRenderTarget>& { return State.PainterSelectiveDensity; } },
		{ TEXT("PainterVelocity"), [](FTDFluidGpuBatchStateTextures& State) -> TRefCountPtr<IPooledRenderTarget>& { return State.PainterVelocity; } },
		{ TEXT("Ripple"), [](FTDFluidGpuBatchStateTextures& State) -> TRefCountPtr<IPooledRenderTarget>& { return State.Ripple[0]; } },
		{ TEXT("WaterWetness"), [](FTDFluidGpuBatchStateTextures& State) -> TRefCountPtr<IPooledRenderTarget>& { return State.WaterWetness; } },
	};

	struct FTDFluidStateCacheSeedCopy
	{
		const TRefCountPtr<IPooledRenderTarget>* Target = nullptr;
		const FTDFluidStateChannelBytes* Source = nullptr;
	};

	struct FTDFluidStateCacheReadRequest
	{
		const FTDFluidGpuStateCacheChannel* Channel = nullptr;
		const TRefCountPtr<IPooledRenderTarget>* Source = nullptr;
		TUniquePtr<FRHIGPUTextureReadback> Readback;
	};

	FTDFluidGpuBatch* TDFluidStateCacheFindBatch(TArray<FTDFluidGpuBatch>& Batches, uint64 ActorKey, int32& OutSliceIndex)
	{
		OutSliceIndex = INDEX_NONE;
		if (ActorKey == 0)
		{
			return nullptr;
		}

		for (FTDFluidGpuBatch& Batch : Batches)
		{
			const int32 SliceIndex = Batch.FindSlice(ActorKey);
			if (SliceIndex != INDEX_NONE)
			{
				OutSliceIndex = SliceIndex;
				return &Batch;
			}
		}

		return nullptr;
	}

	const FTDFluidGpuStateCacheChannel* TDFluidStateCacheFindChannel(FName ChannelName)
	{
		for (const FTDFluidGpuStateCacheChannel& Channel : TDFluidStateCacheChannelTable)
		{
			if (ChannelName == FName(Channel.Name))
			{
				return &Channel;
			}
		}

		return nullptr;
	}

	int64 TDFluidStateCacheComputeSliceBytes(EPixelFormat Format, int32 Width, int32 Height)
	{
		if (Format == PF_Unknown || Format >= PF_MAX || Width <= 0 || Height <= 0)
		{
			return 0;
		}

		const FPixelFormatInfo& FormatInfo = GPixelFormats[Format];
		if (FormatInfo.BlockSizeX != 1 || FormatInfo.BlockSizeY != 1 || FormatInfo.BlockBytes <= 0)
		{
			return 0;
		}

		return static_cast<int64>(Width) * Height * FormatInfo.BlockBytes;
	}

	bool TDFluidStateCacheCopyReadbackRows(FRHIGPUTextureReadback& Readback, FTDFluidStateChannelBytes& InOutChannel)
	{
		if (!Readback.IsReady())
		{
			return false;
		}

		const int64 RowBytes = static_cast<int64>(InOutChannel.Width) * GPixelFormats[InOutChannel.PixelFormat].BlockBytes;
		int32 RowPitchInPixels = 0;
		int32 BufferHeight = 0;
		const uint8* Texels = static_cast<const uint8*>(Readback.Lock(RowPitchInPixels, &BufferHeight));
		if (!Texels || RowPitchInPixels < InOutChannel.Width || BufferHeight < InOutChannel.Height)
		{
			Readback.Unlock();
			return false;
		}

		const int64 RowPitchBytes = static_cast<int64>(RowPitchInPixels) * GPixelFormats[InOutChannel.PixelFormat].BlockBytes;
		InOutChannel.Bytes.SetNumUninitialized(RowBytes * InOutChannel.Height);
		for (int32 RowIndex = 0; RowIndex < InOutChannel.Height; ++RowIndex)
		{
			FMemory::Memcpy(InOutChannel.Bytes.GetData() + RowIndex * RowBytes, Texels + RowIndex * RowPitchBytes, RowBytes);
		}
		Readback.Unlock();
		return true;
	}

	bool TDFluidStateCacheValidateSeed(const FTDFluidGpuStateSeedRequest& Seed, TArray<FTDFluidGpuBatch>& Batches, int32& OutSliceIndex, TArray<FTDFluidStateCacheSeedCopy>& OutCopies, FString& OutError)
	{
		OutCopies.Reset();
		if (!Seed.Channels.IsValid() || Seed.Channels->IsEmpty())
		{
			OutError = TEXT("seed has no channels");
			return false;
		}

		FTDFluidGpuBatch* Batch = TDFluidStateCacheFindBatch(Batches, Seed.ActorKey, OutSliceIndex);
		if (!Batch)
		{
			OutError = TEXT("actor has no GPU batch slice");
			return false;
		}

		if (!(MakeTDFluidGpuStateLayout(Batch->GetKey()) == Seed.Layout))
		{
			OutError = TEXT("state layout does not match the actor batch key");
			return false;
		}

		for (const FTDFluidStateChannelBytes& Channel : *Seed.Channels)
		{
			const FTDFluidGpuStateCacheChannel* Entry = TDFluidStateCacheFindChannel(Channel.Name);
			if (!Entry)
			{
				OutError = FString::Printf(TEXT("channel %s is not a GPU state channel"), *Channel.Name.ToString());
				return false;
			}

			const bool bIsDuplicate = OutCopies.ContainsByPredicate([&Channel](const FTDFluidStateCacheSeedCopy& Copy) { return Copy.Source->Name == Channel.Name; });
			if (bIsDuplicate)
			{
				OutError = FString::Printf(TEXT("channel %s appears twice"), *Channel.Name.ToString());
				return false;
			}

			const TRefCountPtr<IPooledRenderTarget>& Target = Entry->ResolveTexture(Batch->StateTextures);
			if (!Target.IsValid())
			{
				OutError = FString::Printf(TEXT("channel %s has no target texture"), *Channel.Name.ToString());
				return false;
			}

			const FPooledRenderTargetDesc& TargetDesc = Target->GetDesc();
			if (Channel.PixelFormat >= PF_MAX || static_cast<EPixelFormat>(Channel.PixelFormat) != TargetDesc.Format)
			{
				OutError = FString::Printf(TEXT("channel %s pixel format %d does not match %d"), *Channel.Name.ToString(), Channel.PixelFormat, static_cast<int32>(TargetDesc.Format));
				return false;
			}

			if (Channel.Width != TargetDesc.Extent.X || Channel.Height != TargetDesc.Extent.Y)
			{
				OutError = FString::Printf(TEXT("channel %s size %dx%d does not match %dx%d"), *Channel.Name.ToString(), Channel.Width, Channel.Height, TargetDesc.Extent.X, TargetDesc.Extent.Y);
				return false;
			}

			const int64 ExpectedBytes = TDFluidStateCacheComputeSliceBytes(TargetDesc.Format, Channel.Width, Channel.Height);
			if (ExpectedBytes <= 0 || Channel.Bytes.Num() != ExpectedBytes || OutSliceIndex >= static_cast<int32>(TargetDesc.ArraySize))
			{
				OutError = FString::Printf(TEXT("channel %s holds %d bytes, expected %lld"), *Channel.Name.ToString(), Channel.Bytes.Num(), ExpectedBytes);
				return false;
			}

			OutCopies.Add({ &Target, &Channel });
		}

		return true;
	}

	void TDFluidStateCacheAddSeedCopyPass(FRDGBuilder& GraphBuilder, FRHICommandListImmediate& RHICmdList, const FTDFluidStateCacheSeedCopy& Copy, int32 SliceIndex, TArray<TRefCountPtr<IPooledRenderTarget>>& InOutStagingTargets)
	{
		const FTDFluidStateChannelBytes& Source = *Copy.Source;
		const EPixelFormat Format = static_cast<EPixelFormat>(Source.PixelFormat);
		const FRHITextureCreateDesc StagingDesc = FRHITextureCreateDesc::Create2D(TEXT("TDFluidGpu.StateSeedStaging"), Source.Width, Source.Height, Format)
			.SetFlags(ETextureCreateFlags::ShaderResource)
			.SetInitialState(ERHIAccess::SRVMask);
		FTextureRHIRef StagingTexture = RHICmdList.CreateTexture(StagingDesc);
		const uint32 SourcePitch = static_cast<uint32>(Source.Width * GPixelFormats[Format].BlockBytes);
		RHICmdList.UpdateTexture2D(StagingTexture, 0, FUpdateTextureRegion2D(0, 0, 0, 0, Source.Width, Source.Height), SourcePitch, Source.Bytes.GetData());
		const TRefCountPtr<IPooledRenderTarget>& StagingTarget = InOutStagingTargets.Add_GetRef(CreateRenderTarget(StagingTexture, TEXT("TDFluidGpu.StateSeedStaging")));

		FRHICopyTextureInfo CopyInfo;
		CopyInfo.Size = FIntVector(Source.Width, Source.Height, 1);
		CopyInfo.DestSliceIndex = static_cast<uint32>(SliceIndex);
		CopyInfo.NumSlices = 1;
		FRDGTextureRef StagingTextureRef = GraphBuilder.RegisterExternalTexture(StagingTarget, TEXT("TDFluidGpu.StateSeedStaging"));
		FRDGTextureRef TargetTextureRef = GraphBuilder.RegisterExternalTexture(*Copy.Target, TEXT("TDFluidGpu.StateSeedTarget"));
		AddCopyTexturePass(GraphBuilder, StagingTextureRef, TargetTextureRef, CopyInfo);
	}
}

TConstArrayView<FTDFluidGpuStateCacheChannel> GetTDFluidGpuStateCacheChannels()
{
	return MakeArrayView(TDFluidStateCacheChannelTable);
}

FTDFluidGpuStateLayout MakeTDFluidGpuStateLayout(const FTDFluidGpuBatchKey& Key)
{
	FTDFluidGpuStateLayout Layout;
	Layout.ResolutionTier = Key.ResolutionTier;
	Layout.SimMode = static_cast<uint8>(Key.SimMode);
	Layout.bUsesHeatChannel = Key.bUsesHeatChannel;
	Layout.bUsesWaterLayer = Key.bUsesWaterLayer;
	Layout.bUsesFloat32WaterVelocity = Key.bUsesFloat32WaterVelocity;
	Layout.bUsesFloat32Ripple = Key.bUsesFloat32Ripple;
	Layout.bWrapsArea = Key.bWrapsArea;
	Layout.bWritesPainterVelocity = Key.bWritesPainterVelocity;
	return Layout;
}

bool ReadTDFluidGpuActorState(FRHICommandListImmediate& RHICmdList, TArray<FTDFluidGpuBatch>& Batches, uint64 ActorKey, FTDFluidGpuStateLayout& OutLayout, TArray<FTDFluidStateChannelBytes>& OutChannels, FString& OutError)
{
	OutLayout = FTDFluidGpuStateLayout();
	OutChannels.Reset();
	OutError.Reset();
	int32 SliceIndex = INDEX_NONE;
	FTDFluidGpuBatch* Batch = TDFluidStateCacheFindBatch(Batches, ActorKey, SliceIndex);
	if (!Batch)
	{
		OutError = FString::Printf(TEXT("GPU actor key %llu has no batch slice"), ActorKey);
		return false;
	}

	TArray<FTDFluidStateCacheReadRequest> Requests;
	for (const FTDFluidGpuStateCacheChannel& Channel : GetTDFluidGpuStateCacheChannels())
	{
		const TRefCountPtr<IPooledRenderTarget>& Source = Channel.ResolveTexture(Batch->StateTextures);
		if (!Source.IsValid())
		{
			continue;
		}

		const FPooledRenderTargetDesc& SourceDesc = Source->GetDesc();
		if (SliceIndex >= static_cast<int32>(SourceDesc.ArraySize) || TDFluidStateCacheComputeSliceBytes(SourceDesc.Format, SourceDesc.Extent.X, SourceDesc.Extent.Y) <= 0)
		{
			OutError = FString::Printf(TEXT("GPU channel %s cannot be read from slice %d"), Channel.Name, SliceIndex);
			return false;
		}

		FTDFluidStateCacheReadRequest& Request = Requests.AddDefaulted_GetRef();
		Request.Channel = &Channel;
		Request.Source = &Source;
		Request.Readback = MakeUnique<FRHIGPUTextureReadback>(TEXT("TDFluidGpu.StateCacheReadback"));
	}

	if (Requests.IsEmpty())
	{
		OutError = FString::Printf(TEXT("GPU actor key %llu has no state textures yet"), ActorKey);
		return false;
	}

	{
		FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuStateCacheRead"));
		for (FTDFluidStateCacheReadRequest& Request : Requests)
		{
			FRDGTextureRef SourceTexture = GraphBuilder.RegisterExternalTexture(*Request.Source, TEXT("TDFluidGpu.StateCacheSource"));
			AddEnqueueCopyPass(GraphBuilder, Request.Readback.Get(), SourceTexture, FResolveRect(), static_cast<uint32>(SliceIndex));
		}
		GraphBuilder.Execute();
	}
	RHICmdList.SubmitAndBlockUntilGPUIdle();

	for (FTDFluidStateCacheReadRequest& Request : Requests)
	{
		const FPooledRenderTargetDesc& SourceDesc = (*Request.Source)->GetDesc();
		FTDFluidStateChannelBytes& Channel = OutChannels.AddDefaulted_GetRef();
		Channel.Name = FName(Request.Channel->Name);
		Channel.Width = SourceDesc.Extent.X;
		Channel.Height = SourceDesc.Extent.Y;
		Channel.PixelFormat = static_cast<uint8>(SourceDesc.Format);
		if (!TDFluidStateCacheCopyReadbackRows(*Request.Readback, Channel))
		{
			OutError = FString::Printf(TEXT("GPU channel %s readback is not ready"), Request.Channel->Name);
			OutChannels.Reset();
			return false;
		}
	}

	OutLayout = MakeTDFluidGpuStateLayout(Batch->GetKey());
	return true;
}

int32 ApplyTDFluidGpuStateSeeds(FRHICommandListImmediate& RHICmdList, TConstArrayView<FTDFluidGpuStateSeedRequest> Seeds, TArray<FTDFluidGpuBatch>& Batches, TArray<FString>& OutErrors)
{
	if (Seeds.IsEmpty())
	{
		return 0;
	}

	TArray<TRefCountPtr<IPooledRenderTarget>> StagingTargets;
	int32 AppliedSeedCount = 0;
	{
		FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuStateSeeds"));
		for (const FTDFluidGpuStateSeedRequest& Seed : Seeds)
		{
			int32 SliceIndex = INDEX_NONE;
			TArray<FTDFluidStateCacheSeedCopy> Copies;
			FString Error;
			if (!TDFluidStateCacheValidateSeed(Seed, Batches, SliceIndex, Copies, Error))
			{
				OutErrors.Add(FString::Printf(TEXT("GPU actor key %llu: %s"), Seed.ActorKey, *Error));
				continue;
			}

			for (const FTDFluidStateCacheSeedCopy& Copy : Copies)
			{
				TDFluidStateCacheAddSeedCopyPass(GraphBuilder, RHICmdList, Copy, SliceIndex, StagingTargets);
			}
			++AppliedSeedCount;
		}
		GraphBuilder.Execute();
	}

	return AppliedSeedCount;
}
