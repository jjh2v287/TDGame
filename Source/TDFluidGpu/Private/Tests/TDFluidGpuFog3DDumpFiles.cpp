#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/TDFluidGpuFog3DDumpFiles.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "RHIGPUReadback.h"
#include "PooledRenderTarget.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "Serialization/Archive.h"
#include "TextureResource.h"

namespace TDFluidGpuFog3DDumpFilesPrivate
{
	constexpr int32 DumpMaximumReadbackExtraSubmitCount = 16;
	constexpr int32 DumpNormalizedFieldCount = 7;

	const TCHAR* const DumpFieldNames[TDFog3DReplayDumpFieldCount] = {
		TEXT("u"), TEXT("v"), TEXT("w"), TEXT("dx"), TEXT("dy"), TEXT("dz"), TEXT("g"), TEXT("pressure"), TEXT("divergence"), TEXT("fluid") };

	struct FTDFog3DReplayRuntime
	{
		FTDFluidGpuFog3DState State;
		TArray<float> DumpValues;
		TArray<float> ColumnValues;
		TArray<float> FrameValues;
		TArray<uint16> ReachValues;
		bool bDumpCopied = false;
		bool bColumnCopied = false;
		bool bFramesCopied = false;
		bool bReachCopied = false;
		int32 RejectedComputeCount = 0;
		int32 ReadbackExtraSubmitCount = 0;
	};

	void SubmitUntilDumpReadbackIsReady(FRHICommandListImmediate& RHICmdList, FRHIGPUBufferReadback& Readback, int32& InOutExtraSubmitCount)
	{
		int32 ExtraSubmitCount = 0;
		while (!Readback.IsReady() && ExtraSubmitCount < DumpMaximumReadbackExtraSubmitCount)
		{
			RHICmdList.SubmitAndBlockUntilGPUIdle();
			++ExtraSubmitCount;
		}
		InOutExtraSubmitCount += ExtraSubmitCount;
	}

	bool CopyReplaySphereFrames(FRHICommandListImmediate& RHICmdList, FRHIGPUBufferReadback& Readback, int32 SphereCount, TArray<float>& OutValues, int32& InOutExtraSubmitCount)
	{
		RHICmdList.SubmitAndBlockUntilGPUIdle();
		SubmitUntilDumpReadbackIsReady(RHICmdList, Readback, InOutExtraSubmitCount);
		if (!Readback.IsReady())
		{
			return false;
		}
		const int32 FloatCount = SphereCount * TDFog3DReplaySphereFrameFloatCount;
		const void* Data = Readback.Lock(FloatCount * sizeof(float));
		if (!Data)
		{
			return false;
		}
		OutValues.SetNumUninitialized(FloatCount);
		FMemory::Memcpy(OutValues.GetData(), Data, FloatCount * sizeof(float));
		Readback.Unlock();
		return true;
	}

	void EnqueueReplayCompute(const TSharedRef<FTDFog3DReplayRuntime, ESPMode::ThreadSafe>& Runtime, const FTDFluidGpuFog3DFrameInput& FrameInput, const TSharedPtr<FTDFluidGpuFog3DStateOverride, ESPMode::ThreadSafe>& StateOverride, bool bReadsSphereFrames)
	{
		ENQUEUE_RENDER_COMMAND(TDFluidGpuFog3DReplayCompute)(
			[Runtime, FrameInput, StateOverride, bReadsSphereFrames](FRHICommandListImmediate& RHICmdList)
			{
				FTDFluidGpuFog3DFrameInput LocalInput = FrameInput;
				LocalInput.StateOverride = StateOverride.Get();
				FRHIGPUBufferReadback FrameReadback(TEXT("TDFluidGpu.Fog3DReplaySphereFrames"));
				const bool bReadsFrames = bReadsSphereFrames && !LocalInput.Spheres.IsEmpty();
				if (bReadsFrames)
				{
					LocalInput.SphereFrameReadback = &FrameReadback;
				}
				bool bIsAccepted = false;
				{
					FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuFog3DReplayCompute"));
					bIsAccepted = AddTDFluidGpuFog3DPasses(GraphBuilder, Runtime->State, LocalInput);
					GraphBuilder.Execute();
				}
				if (!bIsAccepted)
				{
					++Runtime->RejectedComputeCount;
				}
				if (!bReadsSphereFrames)
				{
					return;
				}
				Runtime->FrameValues.Reset();
				Runtime->bFramesCopied = bIsAccepted && (!bReadsFrames || CopyReplaySphereFrames(RHICmdList, FrameReadback, LocalInput.Spheres.Num(), Runtime->FrameValues, Runtime->ReadbackExtraSubmitCount));
			});
	}

	bool ReadReplayColumnTerrain(FRHICommandListImmediate& RHICmdList, FTDFluidGpuFog3DState& State, TArray<float>& OutValues, int32& InOutExtraSubmitCount)
	{
		if (!State.ColumnTerrainTarget.IsValid())
		{
			return false;
		}

		FRHIGPUTextureReadback Readback(TEXT("TDFluidGpu.Fog3DReplayColumnTerrain"));
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuFog3DReplayColumnTerrain"));
			AddEnqueueCopyPass(GraphBuilder, &Readback, GraphBuilder.RegisterExternalTexture(State.ColumnTerrainTarget));
			GraphBuilder.Execute();
		}
		RHICmdList.SubmitAndBlockUntilGPUIdle();
		int32 ExtraSubmitCount = 0;
		while (!Readback.IsReady() && ExtraSubmitCount < DumpMaximumReadbackExtraSubmitCount)
		{
			RHICmdList.SubmitAndBlockUntilGPUIdle();
			++ExtraSubmitCount;
		}
		InOutExtraSubmitCount += ExtraSubmitCount;
		if (!Readback.IsReady())
		{
			return false;
		}

		const int32 Resolution = State.Resolution;
		const int32 TextureSize = Resolution + 2;
		int32 RowPitchInPixels = 0;
		int32 BufferHeight = 0;
		const FVector4f* Texels = static_cast<const FVector4f*>(Readback.Lock(RowPitchInPixels, &BufferHeight));
		if (!Texels || RowPitchInPixels < TextureSize || BufferHeight < TextureSize)
		{
			Readback.Unlock();
			return false;
		}
		OutValues.SetNumUninitialized(4 * Resolution * Resolution);
		for (int32 LocalX = 0; LocalX < Resolution; ++LocalX)
		{
			for (int32 LocalY = 0; LocalY < Resolution; ++LocalY)
			{
				const FVector4f& Texel = Texels[static_cast<int64>(LocalY + 1) * RowPitchInPixels + LocalX + 1];
				for (int32 FieldIndex = 0; FieldIndex < 4; ++FieldIndex)
				{
					OutValues[(FieldIndex * Resolution + LocalX) * Resolution + LocalY] = Texel[FieldIndex];
				}
			}
		}
		Readback.Unlock();
		return true;
	}

	bool ReadReplayInputReach(FRHICommandListImmediate& RHICmdList, FTDFluidGpuFog3DState& State, TArray<uint16>& OutValues, int32& InOutExtraSubmitCount)
	{
		FRHIGPUBufferReadback Readback(TEXT("TDFluidGpu.Fog3DReplayInputReach"));
		bool bIsAccepted = false;
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuFog3DReplayInputReach"));
			bIsAccepted = AddTDFluidGpuFog3DInputReachDumpPass(GraphBuilder, State, Readback);
			GraphBuilder.Execute();
		}
		RHICmdList.SubmitAndBlockUntilGPUIdle();
		if (!bIsAccepted)
		{
			return false;
		}
		SubmitUntilDumpReadbackIsReady(RHICmdList, Readback, InOutExtraSubmitCount);
		return CopyTDFluidGpuFog3DInputReachDump(Readback, State.Resolution, OutValues);
	}

	bool ReadReplayDump(const TSharedRef<FTDFog3DReplayRuntime, ESPMode::ThreadSafe>& Runtime, bool bReadsColumn, bool bReadsReach)
	{
		ENQUEUE_RENDER_COMMAND(TDFluidGpuFog3DReplayDump)(
			[Runtime, bReadsColumn, bReadsReach](FRHICommandListImmediate& RHICmdList)
			{
				Runtime->bDumpCopied = false;
				Runtime->bColumnCopied = false;
				Runtime->bReachCopied = false;
				FRHIGPUBufferReadback Readback(TEXT("TDFluidGpu.Fog3DReplayDump"));
				bool bIsAccepted = false;
				{
					FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuFog3DReplayDump"));
					bIsAccepted = AddTDFluidGpuFog3DDumpPass(GraphBuilder, Runtime->State, Readback);
					GraphBuilder.Execute();
				}
				RHICmdList.SubmitAndBlockUntilGPUIdle();
				if (!bIsAccepted)
				{
					return;
				}
				SubmitUntilDumpReadbackIsReady(RHICmdList, Readback, Runtime->ReadbackExtraSubmitCount);
				Runtime->bDumpCopied = CopyTDFluidGpuFog3DDump(Readback, Runtime->State.Resolution, Runtime->DumpValues);
				if (bReadsColumn)
				{
					Runtime->bColumnCopied = ReadReplayColumnTerrain(RHICmdList, Runtime->State, Runtime->ColumnValues, Runtime->ReadbackExtraSubmitCount);
				}
				if (bReadsReach)
				{
					Runtime->bReachCopied = ReadReplayInputReach(RHICmdList, Runtime->State, Runtime->ReachValues, Runtime->ReadbackExtraSubmitCount);
				}
			});
		FlushRenderingCommands();
		return Runtime->bDumpCopied;
	}

	void ReleaseReplayState(const TSharedRef<FTDFog3DReplayRuntime, ESPMode::ThreadSafe>& Runtime)
	{
		ENQUEUE_RENDER_COMMAND(TDFluidGpuFog3DReplayRelease)(
			[Runtime](FRHICommandListImmediate& RHICmdList)
			{
				Runtime->State.Release();
			});
		FlushRenderingCommands();
	}

	bool SaveFloatArray(const FString& FilePath, const TArray<float>& Values)
	{
		TUniquePtr<FArchive> Writer(IFileManager::Get().CreateFileWriter(*FilePath));
		if (!Writer)
		{
			return false;
		}
		Writer->Serialize(const_cast<float*>(Values.GetData()), static_cast<int64>(Values.Num()) * sizeof(float));
		return Writer->Close();
	}

	bool SaveReachArray(const FString& FilePath, const TArray<uint16>& Values)
	{
		TUniquePtr<FArchive> Writer(IFileManager::Get().CreateFileWriter(*FilePath));
		if (!Writer)
		{
			return false;
		}
		Writer->Serialize(const_cast<uint16*>(Values.GetData()), static_cast<int64>(Values.Num()) * sizeof(uint16));
		return Writer->Close();
	}

	FString MakeDumpMetaText(const FTDFog3DReplay& Replay, const FTDFog3DReplayDump& Dump)
	{
		const FTDFog3DReplayStep& Step = Replay.Steps[Dump.StepIndex];
		FString FieldList;
		for (int32 ListIndex = 0; ListIndex < Step.DumpFieldIndices.Num(); ++ListIndex)
		{
			FieldList += FString::Printf(TEXT("%s\"%s\""), ListIndex == 0 ? TEXT("") : TEXT(","), DumpFieldNames[Step.DumpFieldIndices[ListIndex]]);
		}
		return FString::Printf(TEXT("{\"format\":\"fog3d-dump-1\",\"scene\":\"%s\",\"dump\":\"%s\",\"n\":%d,\"layers\":%d,\"window_min\":[%d,%d],\"step_index\":%d,\"dt\":%.17g,\"fields\":[%s]}\n"),
			*Replay.Name, *Dump.Name, Replay.Resolution, TDFluidGpuFog3DLayerCount, Step.WindowMinCell.X, Step.WindowMinCell.Y, Dump.ComputeNumber, Step.DeltaSeconds, *FieldList);
	}
}

void MakeTDFog3DNormalizedState(const TArray<float>& DumpValues, int32 Resolution, TArray<float>& OutNormalizedValues)
{
	using namespace TDFluidGpuFog3DDumpFilesPrivate;

	const int32 CellCount = Resolution * Resolution * TDFluidGpuFog3DLayerCount;
	OutNormalizedValues.SetNumUninitialized(CellCount * DumpNormalizedFieldCount);
	for (int32 FieldIndex = 0; FieldIndex < DumpNormalizedFieldCount; ++FieldIndex)
	{
		for (int32 CellIndex = 0; CellIndex < CellCount; ++CellIndex)
		{
			const bool bIsFluid = DumpValues[CellCount * TDFog3DReplayFluidFieldIndex + CellIndex] > 0.5f;
			float Value = bIsFluid ? DumpValues[CellCount * FieldIndex + CellIndex] : 0.0f;
			if (Value == 0.0f)
			{
				Value = 0.0f;
			}
			OutNormalizedValues[CellCount * FieldIndex + CellIndex] = Value;
		}
	}
}

bool WriteTDFog3DDumpFiles(const FTDFog3DReplay& Replay, const FTDFog3DReplayDump& Dump, const FString& Directory, FString& OutError)
{
	using namespace TDFluidGpuFog3DDumpFilesPrivate;

	const FTDFog3DReplayStep& Step = Replay.Steps[Dump.StepIndex];
	const int32 CellCount = Replay.Resolution * Replay.Resolution * TDFluidGpuFog3DLayerCount;
	if (!Dump.Values || Dump.Values->Num() != CellCount * TDFog3DReplayDumpFieldCount)
	{
		OutError = TEXT("dump values have the wrong size");
		return false;
	}

	TArray<float> Selected;
	Selected.Reserve(CellCount * Step.DumpFieldIndices.Num());
	for (const int32 FieldIndex : Step.DumpFieldIndices)
	{
		Selected.Append(Dump.Values->GetData() + static_cast<int64>(CellCount) * FieldIndex, CellCount);
	}

	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Stem = FPaths::Combine(Directory, FString::Printf(TEXT("%s__%s"), *Replay.Name, *Dump.Name));
	if (!SaveFloatArray(Stem + TEXT(".f32"), Selected))
	{
		OutError = FString::Printf(TEXT("cannot write %s.f32"), *Stem);
		return false;
	}
	if (Dump.ColumnValues && !SaveFloatArray(Stem + TEXT(".column.bin"), *Dump.ColumnValues))
	{
		OutError = FString::Printf(TEXT("cannot write %s.column.bin"), *Stem);
		return false;
	}
	if (Dump.FrameValues && !SaveFloatArray(Stem + TEXT(".frames.bin"), *Dump.FrameValues))
	{
		OutError = FString::Printf(TEXT("cannot write %s.frames.bin"), *Stem);
		return false;
	}
	if (Dump.ReachValues && (Dump.ReachValues->Num() != CellCount || !SaveReachArray(Stem + TEXT(".reach.bin"), *Dump.ReachValues)))
	{
		OutError = FString::Printf(TEXT("cannot write %s.reach.bin"), *Stem);
		return false;
	}
	if (!FFileHelper::SaveStringToFile(MakeDumpMetaText(Replay, Dump), *(Stem + TEXT(".json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("cannot write %s.json"), *Stem);
		return false;
	}
	return true;
}

FTDFog3DReplayRunResult RunTDFog3DReplay(const FTDFog3DReplay& Replay, const FTDFog3DReplayRunOptions& Options)
{
	using namespace TDFluidGpuFog3DDumpFilesPrivate;

	FTDFog3DReplayRunResult Result;
	const double StartSeconds = FPlatformTime::Seconds();
	TSharedRef<FTDFog3DReplayRuntime, ESPMode::ThreadSafe> Runtime = MakeShared<FTDFog3DReplayRuntime, ESPMode::ThreadSafe>();
	const int32 LastStepIndex = Replay.Steps.Num() - 1;

	for (int32 StepIndex = 0; StepIndex < Replay.Steps.Num(); ++StepIndex)
	{
		const FTDFog3DReplayStep& Step = Replay.Steps[StepIndex];
		for (int32 RepeatIndex = 0; RepeatIndex < Step.RepeatCount; ++RepeatIndex)
		{
			++Result.ComputeCount;
			FTDFluidGpuFog3DFrameInput FrameInput = MakeTDFog3DReplayFrameInput(Replay, StepIndex, RepeatIndex);
			FrameInput.OutputTarget = Options.OutputTarget;
			if (Options.AdjustFrameInput)
			{
				Options.AdjustFrameInput(Result.ComputeCount, FrameInput);
			}
			TSharedPtr<FTDFluidGpuFog3DStateOverride, ESPMode::ThreadSafe> StateOverride = MakeShared<FTDFluidGpuFog3DStateOverride, ESPMode::ThreadSafe>();
			if (!MakeTDFog3DReplayStateOverride(Replay, StepIndex, RepeatIndex, *StateOverride))
			{
				StateOverride.Reset();
			}
			const bool bIsReplayDumpCompute = RepeatIndex == Step.RepeatCount - 1 && !Step.DumpName.IsEmpty();
			EnqueueReplayCompute(Runtime, FrameInput, StateOverride, Replay.bHasGround && bIsReplayDumpCompute);

			const bool bIsLastRepeat = RepeatIndex == Step.RepeatCount - 1;
			if (Options.AfterCompute)
			{
				FTDFog3DReplayComputeInfo Info;
				Info.StepIndex = StepIndex;
				Info.RepeatIndex = RepeatIndex;
				Info.ComputeNumber = Result.ComputeCount;
				Info.bWindowMoved = RepeatIndex == 0 && DoesTDFog3DReplayStepMoveWindow(Step);
				Info.WindowMoveCells = Info.bWindowMoved ? Step.WindowMinCell - Step.PreviousWindowMinCell : FIntPoint::ZeroValue;
				Options.AfterCompute(Info);
			}

			const bool bIsReplayDump = bIsLastRepeat && !Step.DumpName.IsEmpty();
			const bool bIsLastCompute = StepIndex == LastStepIndex && bIsLastRepeat;
			const bool bIsExtraDump = Options.ExtraDumpIntervalComputes > 0 && (Result.ComputeCount % Options.ExtraDumpIntervalComputes == 0 || bIsLastCompute);
			if (!bIsReplayDump && !bIsExtraDump)
			{
				continue;
			}

			++Result.DumpCount;
			const bool bReadsColumn = Replay.bHasGround && bIsReplayDump;
			const bool bReadsReach = Replay.bOccludesInputBehindObstacles && bIsReplayDump;
			if (!ReadReplayDump(Runtime, bReadsColumn, bReadsReach) || (bReadsColumn && !(Runtime->bColumnCopied && Runtime->bFramesCopied)) || (bReadsReach && !Runtime->bReachCopied))
			{
				++Result.FailedDumpCount;
				continue;
			}

			FTDFog3DReplayDump Dump;
			Dump.Name = bIsReplayDump ? Step.DumpName : FString::Printf(TEXT("compute%d"), Result.ComputeCount);
			Dump.StepIndex = StepIndex;
			Dump.ComputeNumber = Result.ComputeCount;
			Dump.bIsReplayDump = bIsReplayDump;
			Dump.Values = &Runtime->DumpValues;
			Dump.ColumnValues = bReadsColumn ? &Runtime->ColumnValues : nullptr;
			Dump.FrameValues = bReadsColumn ? &Runtime->FrameValues : nullptr;
			Dump.ReachValues = bReadsReach ? &Runtime->ReachValues : nullptr;
			if (Options.bWritesDumpFiles && bIsReplayDump)
			{
				FString Error;
				const FString Directory = Options.DumpDirectory.IsEmpty() ? GetTDFog3DDumpDirectory() : Options.DumpDirectory;
				const bool bIsWritten = WriteTDFog3DDumpFiles(Replay, Dump, Directory, Error);
				Result.WrittenFileCount += bIsWritten ? 1 : 0;
				Result.FailedFileCount += bIsWritten ? 0 : 1;
			}
			if (Options.OnDump)
			{
				Options.OnDump(Dump);
			}
		}
	}

	FlushRenderingCommands();
	Result.RejectedComputeCount = Runtime->RejectedComputeCount;
	Result.ReadbackExtraSubmitCount = Runtime->ReadbackExtraSubmitCount;
	ReleaseReplayState(Runtime);
	Result.Seconds = FPlatformTime::Seconds() - StartSeconds;
	return Result;
}

#endif
