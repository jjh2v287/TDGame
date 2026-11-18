#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Tests/TDFluidGpuFog3DReplay.h"

class FTextureRenderTargetResource;

struct FTDFog3DReplayComputeInfo
{
	int32 StepIndex = 0;
	int32 RepeatIndex = 0;
	int32 ComputeNumber = 0;
	bool bWindowMoved = false;
	FIntPoint WindowMoveCells = FIntPoint::ZeroValue;
};

struct FTDFog3DReplayDump
{
	FString Name;
	int32 StepIndex = 0;
	int32 ComputeNumber = 0;
	bool bIsReplayDump = false;
	const TArray<float>* Values = nullptr;
	const TArray<float>* ColumnValues = nullptr;
	const TArray<float>* FrameValues = nullptr;
	const TArray<uint16>* ReachValues = nullptr;
};

struct FTDFog3DReplayRunOptions
{
	FTextureRenderTargetResource* OutputTarget = nullptr;
	bool bWritesDumpFiles = false;
	FString DumpDirectory;
	int32 ExtraDumpIntervalComputes = 0;
	TFunction<void(int32 ComputeNumber, FTDFluidGpuFog3DFrameInput&)> AdjustFrameInput;
	TFunction<void(const FTDFog3DReplayComputeInfo&)> AfterCompute;
	TFunction<void(const FTDFog3DReplayDump&)> OnDump;
};

struct FTDFog3DReplayRunResult
{
	int32 ComputeCount = 0;
	int32 RejectedComputeCount = 0;
	int32 DumpCount = 0;
	int32 FailedDumpCount = 0;
	int32 WrittenFileCount = 0;
	int32 FailedFileCount = 0;
	int32 ReadbackExtraSubmitCount = 0;
	double Seconds = 0.0;
};

FTDFog3DReplayRunResult RunTDFog3DReplay(const FTDFog3DReplay& Replay, const FTDFog3DReplayRunOptions& Options);
void MakeTDFog3DNormalizedState(const TArray<float>& DumpValues, int32 Resolution, TArray<float>& OutNormalizedValues);
bool WriteTDFog3DDumpFiles(const FTDFog3DReplay& Replay, const FTDFog3DReplayDump& Dump, const FString& Directory, FString& OutError);

#endif
