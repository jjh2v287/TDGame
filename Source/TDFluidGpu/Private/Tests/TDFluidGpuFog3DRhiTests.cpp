#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicRHI.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "HAL/FileManager.h"
#include "Math/Float16Color.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "RHIGPUReadback.h"
#include "RHIGlobals.h"
#include "RenderGraphBuilder.h"
#include "RenderingThread.h"
#include "TDFluidGpuFog3D.h"
#include "Tests/TDFluidGpuFog3DDumpFiles.h"
#include "Tests/TDFluidGpuFog3DReplay.h"
#include "TextureResource.h"
#include "UObject/Package.h"

#include <limits>

namespace TDFluidGpuFog3DRhiTestPrivate
{
	constexpr int32 Fog3DSmokeResolution = 32;
	constexpr int32 Fog3DMaximumReadbackExtraSubmitCount = 16;
	constexpr int32 Fog3DFluidFieldIndex = 9;
	constexpr int32 Fog3DWrongOutputLayerCount = 4;
	constexpr uint8 Fog3DFillOperationRest = 1;

	struct FTDFog3DBlockedCase
	{
		FString Name;
		FTDFluidGpuFog3DFrameInput FrameInput;
	};

	struct FTDFog3DSmokeOutputs
	{
		FTextureRenderTargetResource* Matching = nullptr;
		FTextureRenderTargetResource* WrongLayers = nullptr;
		FTextureRenderTargetResource* WrongFormat = nullptr;
		FTextureRenderTargetResource* WrongWidth = nullptr;
	};

	struct FTDFog3DOutputShape
	{
		bool bHasTexture = false;
		EPixelFormat Format = PF_Unknown;
		FIntPoint Extent = FIntPoint::ZeroValue;
		int32 Depth = 0;
	};

	struct FTDFog3DSmokeResult
	{
		FTDFog3DOutputShape WrongFormatShape;
		FTDFog3DOutputShape WrongWidthShape;
		TArray<FString> BlockedCaseNames;
		TArray<bool> BlockedCaseAccepted;
		bool bBlockedCasesLeftStateEmpty = false;
		bool bEmptyStateDumpAccepted = true;
		TArray<bool> StepAccepted;
		bool bDumpAccepted = false;
		bool bDumpCopied = false;
		int32 ReadbackExtraSubmitCount = 0;
		int32 FinalResolution = 0;
		FIntPoint FinalWindowMinCell = FIntPoint::ZeroValue;
		bool bFinalHasStepped = false;
		TArray<float> DumpValues;
	};

	bool HasRenderingRhi()
	{
		return !GUsingNullRHI && FApp::CanEverRender();
	}

	int32 GetFog3DSmokeCellCount()
	{
		return Fog3DSmokeResolution * Fog3DSmokeResolution * TDFluidGpuFog3DLayerCount;
	}

	FTDFluidGpuFog3DFrameInput MakeFog3DSmokeFrameInput()
	{
		FTDFluidGpuFog3DFrameInput FrameInput;
		FrameInput.Resolution = Fog3DSmokeResolution;
		FrameInput.bResetsState = false;
		return FrameInput;
	}

	FTDFluidGpuFog3DFrameInput& AddFog3DBlockedCase(TArray<FTDFog3DBlockedCase>& Cases, const TCHAR* Name)
	{
		FTDFog3DBlockedCase& Case = Cases.AddDefaulted_GetRef();
		Case.Name = Name;
		Case.FrameInput = MakeFog3DSmokeFrameInput();
		return Case.FrameInput;
	}

	TArray<FTDFog3DBlockedCase> MakeFog3DBlockedCases(const FTDFog3DSmokeOutputs& Outputs, const FTDFluidGpuFog3DStateOverride& ShortWindOverride, const FTDFluidGpuFog3DStateOverride& LongFillOverride)
	{
		TArray<FTDFog3DBlockedCase> Cases;
		AddFog3DBlockedCase(Cases, TEXT("resolution 48 is not a power of two")).Resolution = 48;
		AddFog3DBlockedCase(Cases, TEXT("resolution 8 is below 16")).Resolution = 8;
		AddFog3DBlockedCase(Cases, TEXT("window min x 8 is not a multiple of 16")).WindowMinCell = FIntPoint(8, 0);
		AddFog3DBlockedCase(Cases, TEXT("window min y -24 is not a multiple of 16")).WindowMinCell = FIntPoint(0, -24);
		AddFog3DBlockedCase(Cases, TEXT("cell size 0")).CellSizeCm = 0.0f;
		AddFog3DBlockedCase(Cases, TEXT("layer height -30")).LayerHeightCm = -30.0f;
		AddFog3DBlockedCase(Cases, TEXT("mode 0")).Mode = 0;
		AddFog3DBlockedCase(Cases, TEXT("mode 3")).Mode = 3;
		AddFog3DBlockedCase(Cases, TEXT("delta seconds 0")).DeltaSeconds = 0.0f;
		AddFog3DBlockedCase(Cases, TEXT("delta seconds -1")).DeltaSeconds = -1.0f;
		AddFog3DBlockedCase(Cases, TEXT("delta seconds NaN")).DeltaSeconds = std::numeric_limits<float>::quiet_NaN();
		AddFog3DBlockedCase(Cases, TEXT("delta seconds infinite")).DeltaSeconds = std::numeric_limits<float>::infinity();
		AddFog3DBlockedCase(Cases, TEXT("17 spheres")).Spheres.SetNum(TDFluidGpuFog3DMaximumSphereCount + 1);
		AddFog3DBlockedCase(Cases, TEXT("33 obstacles")).Obstacles.SetNum(TDFluidGpuFog3DMaximumObstacleCount + 1);
		AddFog3DBlockedCase(Cases, TEXT("edge band 17 cells")).Tuning.EdgeBandCells = TDFluidGpuFog3DMaximumEdgeBandCells + 1;
		AddFog3DBlockedCase(Cases, TEXT("wind override of 5 faces")).StateOverride = &ShortWindOverride;
		AddFog3DBlockedCase(Cases, TEXT("fill override one longer than the grid")).StateOverride = &LongFillOverride;
		AddFog3DBlockedCase(Cases, TEXT("output target with 4 layers")).OutputTarget = Outputs.WrongLayers;
		AddFog3DBlockedCase(Cases, TEXT("output target in 32 bit float format")).OutputTarget = Outputs.WrongFormat;
		AddFog3DBlockedCase(Cases, TEXT("output target twice as wide")).OutputTarget = Outputs.WrongWidth;
		return Cases;
	}

	FTDFog3DOutputShape ReadFog3DOutputShape(FTextureRenderTargetResource* Output)
	{
		FTDFog3DOutputShape Shape;
		FRHITexture* Texture = Output ? Output->GetRenderTargetTexture().GetReference() : nullptr;
		if (!Texture)
		{
			return Shape;
		}
		const FRHITextureDesc& Desc = Texture->GetDesc();
		Shape.bHasTexture = true;
		Shape.Format = Desc.Format;
		Shape.Extent = Desc.Extent;
		Shape.Depth = Desc.Depth;
		return Shape;
	}

	TArray<FTDFluidGpuFog3DFrameInput> MakeFog3DSmokeSteps(FTextureRenderTargetResource* MatchingOutput, const FTDFluidGpuFog3DStateOverride& RestOverride)
	{
		TArray<FTDFluidGpuFog3DFrameInput> Steps;
		FTDFluidGpuFog3DFrameInput& ResetStep = Steps.Add_GetRef(MakeFog3DSmokeFrameInput());
		ResetStep.bResetsState = true;

		FTDFluidGpuFog3DFrameInput& ShiftStep = Steps.Add_GetRef(MakeFog3DSmokeFrameInput());
		ShiftStep.WindowMinCell = FIntPoint(16, -16);
		ShiftStep.PreviousWindowMinCell = FIntPoint::ZeroValue;
		ShiftStep.StateOverride = &RestOverride;
		ShiftStep.OutputTarget = MatchingOutput;

		FTDFluidGpuFog3DFrameInput& WindlessStep = Steps.Add_GetRef(MakeFog3DSmokeFrameInput());
		WindlessStep.Mode = 1;
		WindlessStep.WindowMinCell = FIntPoint(16, -16);
		WindlessStep.PreviousWindowMinCell = FIntPoint(16, -16);
		WindlessStep.DeltaSeconds = 1.0f / 30.0f;
		return Steps;
	}

	void SubmitUntilFog3DReadbackIsReady(FRHICommandListImmediate& RHICmdList, FRHIGPUBufferReadback& Readback, int32& OutExtraSubmitCount)
	{
		OutExtraSubmitCount = 0;
		while (!Readback.IsReady() && OutExtraSubmitCount < Fog3DMaximumReadbackExtraSubmitCount)
		{
			RHICmdList.SubmitAndBlockUntilGPUIdle();
			++OutExtraSubmitCount;
		}
	}

	void RunFog3DSmoke(FRHICommandListImmediate& RHICmdList, const FTDFog3DSmokeOutputs& Outputs, FTDFog3DSmokeResult& Result)
	{
		Result.WrongFormatShape = ReadFog3DOutputShape(Outputs.WrongFormat);
		Result.WrongWidthShape = ReadFog3DOutputShape(Outputs.WrongWidth);
		FTDFluidGpuFog3DStateOverride ShortWindOverride;
		ShortWindOverride.WindFaces.SetNumZeroed(5);
		FTDFluidGpuFog3DStateOverride LongFillOverride;
		LongFillOverride.FillOperations.SetNumZeroed(GetFog3DSmokeCellCount() + 1);
		FTDFluidGpuFog3DStateOverride RestOverride;
		RestOverride.WindFaces.SetNumZeroed(GetFog3DSmokeCellCount());
		RestOverride.FillOperations.Init(Fog3DFillOperationRest, GetFog3DSmokeCellCount());

		FTDFluidGpuFog3DState State;
		for (const FTDFog3DBlockedCase& Case : MakeFog3DBlockedCases(Outputs, ShortWindOverride, LongFillOverride))
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuFog3DSmokeBlocked"));
			Result.BlockedCaseNames.Add(Case.Name);
			Result.BlockedCaseAccepted.Add(AddTDFluidGpuFog3DPasses(GraphBuilder, State, Case.FrameInput));
			GraphBuilder.Execute();
		}
		Result.bBlockedCasesLeftStateEmpty = State.Resolution == 0 && !State.bHasStepped && !State.WindTargets[0].IsValid();

		{
			FRHIGPUBufferReadback EmptyStateReadback(TEXT("TDFluidGpu.Fog3DSmokeEmptyDump"));
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuFog3DSmokeEmptyDump"));
			Result.bEmptyStateDumpAccepted = AddTDFluidGpuFog3DDumpPass(GraphBuilder, State, EmptyStateReadback);
			GraphBuilder.Execute();
			RHICmdList.SubmitAndBlockUntilGPUIdle();
		}

		for (const FTDFluidGpuFog3DFrameInput& Step : MakeFog3DSmokeSteps(Outputs.Matching, RestOverride))
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuFog3DSmokeStep"));
			Result.StepAccepted.Add(AddTDFluidGpuFog3DPasses(GraphBuilder, State, Step));
			GraphBuilder.Execute();
		}

		FRHIGPUBufferReadback Readback(TEXT("TDFluidGpu.Fog3DSmokeDump"));
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuFog3DSmokeDump"));
			Result.bDumpAccepted = AddTDFluidGpuFog3DDumpPass(GraphBuilder, State, Readback);
			GraphBuilder.Execute();
		}
		RHICmdList.SubmitAndBlockUntilGPUIdle();
		if (Result.bDumpAccepted)
		{
			SubmitUntilFog3DReadbackIsReady(RHICmdList, Readback, Result.ReadbackExtraSubmitCount);
			Result.bDumpCopied = CopyTDFluidGpuFog3DDump(Readback, Fog3DSmokeResolution, Result.DumpValues);
		}

		Result.FinalResolution = State.Resolution;
		Result.FinalWindowMinCell = State.WindowMinCell;
		Result.bFinalHasStepped = State.bHasStepped;
		State.Release();
	}

	UTextureRenderTargetVolume* CreateFog3DShapedVolumeTarget(int32 Width, int32 Height, int32 LayerCount, EPixelFormat Format)
	{
		UTextureRenderTargetVolume* Target = NewObject<UTextureRenderTargetVolume>(GetTransientPackage(), NAME_None, RF_Transient);
		Target->bSupportsUAV = true;
		Target->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
		Target->Init(Width, Height, LayerCount, Format);
		Target->UpdateResourceImmediate(true);
		FlushRenderingCommands();
		return Target;
	}

	UTextureRenderTargetVolume* CreateFog3DVolumeTarget(int32 Resolution, int32 LayerCount)
	{
		return CreateFog3DShapedVolumeTarget(Resolution, Resolution, LayerCount, PF_FloatRGBA);
	}

	int32 CountFog3DNonZeroStateValues(const TArray<float>& DumpValues)
	{
		const int32 FieldStride = GetFog3DSmokeCellCount();
		const int32 StateValueCount = FMath::Min(DumpValues.Num(), FieldStride * Fog3DFluidFieldIndex);
		int32 NonZeroCount = 0;
		for (int32 ValueIndex = 0; ValueIndex < StateValueCount; ++ValueIndex)
		{
			NonZeroCount += DumpValues[ValueIndex] == 0.0f ? 0 : 1;
		}
		return NonZeroCount;
	}

	int32 CountFog3DNonFluidCells(const TArray<float>& DumpValues)
	{
		const int32 FieldStride = GetFog3DSmokeCellCount();
		const int32 FluidStart = FieldStride * Fog3DFluidFieldIndex;
		int32 NonFluidCount = 0;
		for (int32 ValueIndex = FluidStart; ValueIndex < DumpValues.Num(); ++ValueIndex)
		{
			NonFluidCount += DumpValues[ValueIndex] == 1.0f ? 0 : 1;
		}
		return NonFluidCount;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DModuleSmokeTest,
	"TDGame.FluidFXRhi.Fog3D.ModuleSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DModuleSmokeTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D ModuleSmoke needs a rendering RHI. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."));
		return false;
	}

	TestEqual(TEXT("State bytes at 256 are 74 bytes per cell"), FTDFluidGpuFog3DState::EstimateStateBytes(256), static_cast<int64>(256) * 256 * TDFluidGpuFog3DLayerCount * 74);
	TestEqual(TEXT("Dump bytes at 128 hold 10 float fields"), GetTDFluidGpuFog3DDumpByteCount(128), static_cast<int64>(128) * 128 * TDFluidGpuFog3DLayerCount * TDFluidGpuFog3DDumpFieldCount * 4);

	UTextureRenderTargetVolume* MatchingTarget = CreateFog3DVolumeTarget(Fog3DSmokeResolution, TDFluidGpuFog3DLayerCount);
	UTextureRenderTargetVolume* WrongSizeTarget = CreateFog3DVolumeTarget(Fog3DSmokeResolution, Fog3DWrongOutputLayerCount);
	UTextureRenderTargetVolume* WrongFormatTarget = CreateFog3DShapedVolumeTarget(Fog3DSmokeResolution, Fog3DSmokeResolution, TDFluidGpuFog3DLayerCount, PF_A32B32G32R32F);
	UTextureRenderTargetVolume* WrongWidthTarget = CreateFog3DShapedVolumeTarget(Fog3DSmokeResolution * 2, Fog3DSmokeResolution, TDFluidGpuFog3DLayerCount, PF_FloatRGBA);
	FTDFog3DSmokeOutputs Outputs;
	Outputs.Matching = MatchingTarget->GameThread_GetRenderTargetResource();
	Outputs.WrongLayers = WrongSizeTarget->GameThread_GetRenderTargetResource();
	Outputs.WrongFormat = WrongFormatTarget->GameThread_GetRenderTargetResource();
	Outputs.WrongWidth = WrongWidthTarget->GameThread_GetRenderTargetResource();
	TestNotNull(TEXT("Matching output target has a resource"), Outputs.Matching);
	TestNotNull(TEXT("Wrong size output target has a resource"), Outputs.WrongLayers);
	TestNotNull(TEXT("Wrong format output target has a resource"), Outputs.WrongFormat);
	TestNotNull(TEXT("Wrong width output target has a resource"), Outputs.WrongWidth);

	TSharedRef<FTDFog3DSmokeResult, ESPMode::ThreadSafe> Result = MakeShared<FTDFog3DSmokeResult, ESPMode::ThreadSafe>();
	ENQUEUE_RENDER_COMMAND(TDFluidGpuFog3DModuleSmoke)(
		[Result, Outputs](FRHICommandListImmediate& RHICmdList)
		{
			RunFog3DSmoke(RHICmdList, Outputs, *Result);
		});
	FlushRenderingCommands();

	const FTDFog3DOutputShape& FormatShape = Result->WrongFormatShape;
	const FTDFog3DOutputShape& WidthShape = Result->WrongWidthShape;
	AddInfo(FString::Printf(TEXT("Fog3D ModuleSmoke wrong outputs: format case %s %dx%dx%d, width case %s %dx%dx%d"),
		GetPixelFormatString(FormatShape.Format), FormatShape.Extent.X, FormatShape.Extent.Y, FormatShape.Depth,
		GetPixelFormatString(WidthShape.Format), WidthShape.Extent.X, WidthShape.Extent.Y, WidthShape.Depth));
	TestTrue(TEXT("Wrong format target differs only in format"), FormatShape.bHasTexture && FormatShape.Format != PF_FloatRGBA
		&& FormatShape.Extent == FIntPoint(Fog3DSmokeResolution, Fog3DSmokeResolution) && FormatShape.Depth == TDFluidGpuFog3DLayerCount);
	TestTrue(TEXT("Wrong width target differs only in width"), WidthShape.bHasTexture && WidthShape.Format == PF_FloatRGBA
		&& WidthShape.Extent == FIntPoint(Fog3DSmokeResolution * 2, Fog3DSmokeResolution) && WidthShape.Depth == TDFluidGpuFog3DLayerCount);

	for (int32 CaseIndex = 0; CaseIndex < Result->BlockedCaseNames.Num(); ++CaseIndex)
	{
		TestFalse(*FString::Printf(TEXT("Blocked input rejected: %s"), *Result->BlockedCaseNames[CaseIndex]), Result->BlockedCaseAccepted[CaseIndex]);
	}
	TestTrue(TEXT("Blocked inputs left the state empty"), Result->bBlockedCasesLeftStateEmpty);
	TestFalse(TEXT("Dump of a state that never stepped is rejected"), Result->bEmptyStateDumpAccepted);

	TestEqual(TEXT("Three smoke steps ran"), Result->StepAccepted.Num(), 3);
	for (int32 StepIndex = 0; StepIndex < Result->StepAccepted.Num(); ++StepIndex)
	{
		TestTrue(*FString::Printf(TEXT("Smoke step %d accepted"), StepIndex + 1), Result->StepAccepted[StepIndex]);
	}
	TestEqual(TEXT("State keeps the smoke resolution"), Result->FinalResolution, Fog3DSmokeResolution);
	TestTrue(TEXT("State keeps the last window"), Result->FinalWindowMinCell == FIntPoint(16, -16));
	TestTrue(TEXT("State marks that it stepped"), Result->bFinalHasStepped);

	TestTrue(TEXT("Dump pass accepted"), Result->bDumpAccepted);
	TestTrue(TEXT("Dump readback copied"), Result->bDumpCopied);
	TestEqual(TEXT("Dump holds 10 fields"), Result->DumpValues.Num(), GetFog3DSmokeCellCount() * TDFluidGpuFog3DDumpFieldCount);
	const int32 NonZeroStateValueCount = CountFog3DNonZeroStateValues(Result->DumpValues);
	const int32 NonFluidCellCount = CountFog3DNonFluidCells(Result->DumpValues);
	AddInfo(FString::Printf(TEXT("Fog3D ModuleSmoke: %d blocked cases, %d dump values, non-zero state values %d, non-fluid cells %d, readback extra submits %d"),
		Result->BlockedCaseNames.Num(), Result->DumpValues.Num(), NonZeroStateValueCount, NonFluidCellCount, Result->ReadbackExtraSubmitCount));
	TestEqual(TEXT("Empty input keeps wind, displacement, fill, pressure and divergence at zero"), NonZeroStateValueCount, 0);
	TestEqual(TEXT("Empty input keeps every cell fluid"), NonFluidCellCount, 0);

	MatchingTarget->MarkAsGarbage();
	WrongSizeTarget->MarkAsGarbage();
	WrongFormatTarget->MarkAsGarbage();
	WrongWidthTarget->MarkAsGarbage();
	return true;
}

namespace TDFluidGpuFog3DRhiTestPrivate
{
	constexpr int32 Fog3DWindFieldCount = 3;
	constexpr int32 Fog3DGapFieldIndex = 6;
	constexpr int32 Fog3DPressureFieldIndex = 7;
	constexpr int32 Fog3DStateFieldCount = 8;
	constexpr int32 Fog3DRestCheckIntervalComputes = 50;
	constexpr int32 Fog3DSealedCheckIntervalComputes = 60;
	constexpr int32 Fog3DExpectedReplayFileCount = 59;
	constexpr int32 Fog3DExpectedWindowMoveCount = 4;
	constexpr int32 Fog3DFrontBandCells = 16;
	constexpr double Fog3DFrontBandFillRatio = 0.01;
	constexpr double Fog3DInputReachExtraCells = 2.0;
	constexpr double Fog3DSealedMaximumWindCmPerSec = 3000.0;
	constexpr double Fog3DReplayCellSizeCm = 15.625;
	constexpr int32 Fog3DTimingWarmupComputeCount = 20;
	constexpr int32 Fog3DTimingMeasuredComputeCount = 80;
	constexpr int32 Fog3DTimingWindowMovePeriod = 10;

	bool LoadFog3DReplayByName(FAutomationTestBase& Test, const TCHAR* ReplayName, FTDFog3DReplay& OutReplay)
	{
		FString Error;
		const FString FilePath = FPaths::Combine(GetTDFog3DReplayDirectory(), FString(ReplayName) + TEXT(".json"));
		if (LoadTDFog3DReplay(FilePath, OutReplay, Error))
		{
			return true;
		}
		Test.AddError(Error);
		return false;
	}

	bool HasFog3DReplayRun(FAutomationTestBase& Test, const FTDFog3DReplay& Replay, const FTDFog3DReplayRunResult& Result)
	{
		Test.TestEqual(*FString::Printf(TEXT("%s: every compute accepted"), *Replay.Name), Result.RejectedComputeCount, 0);
		Test.TestEqual(*FString::Printf(TEXT("%s: every dump copied"), *Replay.Name), Result.FailedDumpCount, 0);
		Test.TestEqual(*FString::Printf(TEXT("%s: computes run"), *Replay.Name), Result.ComputeCount, Replay.ComputeCount);
		return Result.RejectedComputeCount == 0 && Result.FailedDumpCount == 0 && Result.ComputeCount == Replay.ComputeCount;
	}

	int32 GetFog3DDumpIndex(int32 Resolution, int32 LocalX, int32 LocalY, int32 Layer)
	{
		return (LocalX * Resolution + LocalY) * TDFluidGpuFog3DLayerCount + Layer;
	}

	int32 GetFog3DDumpCellCount(int32 Resolution)
	{
		return Resolution * Resolution * TDFluidGpuFog3DLayerCount;
	}

	bool IsFog3DDumpCellFluid(const TArray<float>& DumpValues, int32 Resolution, int32 LocalX, int32 LocalY, int32 Layer)
	{
		return DumpValues[GetFog3DDumpCellCount(Resolution) * Fog3DFluidFieldIndex + GetFog3DDumpIndex(Resolution, LocalX, LocalY, Layer)] > 0.5f;
	}

	bool IsFog3DDumpFaceClosed(const TArray<float>& DumpValues, int32 Resolution, int32 LocalX, int32 LocalY, int32 Layer, int32 AxisIndex)
	{
		if (!IsFog3DDumpCellFluid(DumpValues, Resolution, LocalX, LocalY, Layer))
		{
			return true;
		}
		if (AxisIndex == 0)
		{
			return LocalX == Resolution - 1 || !IsFog3DDumpCellFluid(DumpValues, Resolution, LocalX + 1, LocalY, Layer);
		}
		if (AxisIndex == 1)
		{
			return LocalY == Resolution - 1 || !IsFog3DDumpCellFluid(DumpValues, Resolution, LocalX, LocalY + 1, Layer);
		}
		return Layer < TDFluidGpuFog3DLayerCount - 1 && !IsFog3DDumpCellFluid(DumpValues, Resolution, LocalX, LocalY, Layer + 1);
	}

	struct FTDFog3DDumpCensus
	{
		int32 ClosedFaces = 0;
		int32 ClosedFacesWithWind = 0;
		int32 OpenFacesWithWind = 0;
		int32 NonFiniteValues = 0;
		int32 BlockedCells = 0;
		double MaximumWindCmPerSec = 0.0;
	};

	FTDFog3DDumpCensus TakeFog3DDumpCensus(const TArray<float>& DumpValues, int32 Resolution)
	{
		FTDFog3DDumpCensus Census;
		const int32 CellCount = GetFog3DDumpCellCount(Resolution);
		for (const float Value : DumpValues)
		{
			Census.NonFiniteValues += FMath::IsFinite(Value) ? 0 : 1;
		}
		for (int32 LocalX = 0; LocalX < Resolution; ++LocalX)
		{
			for (int32 LocalY = 0; LocalY < Resolution; ++LocalY)
			{
				for (int32 Layer = 0; Layer < TDFluidGpuFog3DLayerCount; ++Layer)
				{
					Census.BlockedCells += IsFog3DDumpCellFluid(DumpValues, Resolution, LocalX, LocalY, Layer) ? 0 : 1;
					for (int32 AxisIndex = 0; AxisIndex < Fog3DWindFieldCount; ++AxisIndex)
					{
						const float Wind = DumpValues[CellCount * AxisIndex + GetFog3DDumpIndex(Resolution, LocalX, LocalY, Layer)];
						const bool bIsClosed = IsFog3DDumpFaceClosed(DumpValues, Resolution, LocalX, LocalY, Layer, AxisIndex);
						Census.ClosedFaces += bIsClosed ? 1 : 0;
						Census.ClosedFacesWithWind += bIsClosed && Wind != 0.0f ? 1 : 0;
						Census.OpenFacesWithWind += !bIsClosed && Wind != 0.0f ? 1 : 0;
						Census.MaximumWindCmPerSec = FMath::Max(Census.MaximumWindCmPerSec, FMath::Abs(static_cast<double>(Wind)));
					}
				}
			}
		}
		return Census;
	}

	bool IsFog3DCellInOldRegion(int32 Resolution, const FIntPoint& MoveCells, int32 LocalX, int32 LocalY)
	{
		return LocalX >= FMath::Max(0, -MoveCells.X) && LocalX <= FMath::Min(Resolution, Resolution - MoveCells.X) - 1
			&& LocalY >= FMath::Max(0, -MoveCells.Y) && LocalY <= FMath::Min(Resolution, Resolution - MoveCells.Y) - 1;
	}

	bool IsFog3DNewStripInnerCell(int32 Resolution, const FIntPoint& MoveCells, int32 LocalX, int32 LocalY)
	{
		if (IsFog3DCellInOldRegion(Resolution, MoveCells, LocalX, LocalY))
		{
			return false;
		}
		const FIntPoint NeighborOffsets[] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
		for (const FIntPoint& Offset : NeighborOffsets)
		{
			const int32 NeighborX = LocalX + Offset.X;
			const int32 NeighborY = LocalY + Offset.Y;
			if (NeighborX < 0 || NeighborY < 0 || NeighborX >= Resolution || NeighborY >= Resolution)
			{
				continue;
			}
			if (IsFog3DCellInOldRegion(Resolution, MoveCells, NeighborX, NeighborY))
			{
				return false;
			}
		}
		return true;
	}

	double GetFog3DDistanceToSegmentCm(const FVector3d& Point, const FVector3d& Start, const FVector3d& End)
	{
		const FVector3d Segment = End - Start;
		const double LengthSquared = Segment.SizeSquared();
		const double Fraction = LengthSquared > 0.0 ? FMath::Clamp(FVector3d::DotProduct(Point - Start, Segment) / LengthSquared, 0.0, 1.0) : 0.0;
		return FVector3d::Distance(Point, Start + Segment * Fraction);
	}

	bool IsFog3DCellBeyondEveryInput(const FTDFog3DReplayStep& Step, const FVector3d& CellCenterCm)
	{
		for (const FTDFog3DReplaySphere& Sphere : Step.Spheres)
		{
			if (GetFog3DDistanceToSegmentCm(CellCenterCm, Sphere.StartCm, Sphere.EndCm) <= Sphere.RadiusCm + Fog3DInputReachExtraCells * Fog3DReplayCellSizeCm)
			{
				return false;
			}
		}
		return true;
	}

	bool IsFog3DNormalizedStateIdentical(const TArray<float>& First, const TArray<float>& Second, int32& OutDifferentValueCount)
	{
		OutDifferentValueCount = 0;
		if (First.Num() != Second.Num())
		{
			OutDifferentValueCount = FMath::Max(First.Num(), Second.Num());
			return false;
		}
		for (int32 ValueIndex = 0; ValueIndex < First.Num(); ++ValueIndex)
		{
			OutDifferentValueCount += FMemory::Memcmp(&First[ValueIndex], &Second[ValueIndex], sizeof(float)) == 0 ? 0 : 1;
		}
		return OutDifferentValueCount == 0 && FMemory::Memcmp(First.GetData(), Second.GetData(), First.Num() * sizeof(float)) == 0;
	}

	int32 CountFog3DNonZeroValues(const TArray<float>& Values, int32 ValueCount)
	{
		int32 NonZeroCount = 0;
		for (int32 ValueIndex = 0; ValueIndex < ValueCount; ++ValueIndex)
		{
			NonZeroCount += Values[ValueIndex] == 0.0f ? 0 : 1;
		}
		return NonZeroCount;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DRestStaysZeroTest,
	"TDGame.FluidFXRhi.Fog3D.RestStaysZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DRestStaysZeroTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D RestStaysZero needs a rendering RHI."));
		return false;
	}

	FTDFog3DReplay Replay;
	if (!LoadFog3DReplayByName(*this, TEXT("still_600"), Replay))
	{
		return false;
	}

	int32 CheckedDumpCount = 0;
	int32 NonZeroValueCount = 0;
	int32 NonFiniteValueCount = 0;
	int32 BlockedCellCount = 0;
	int32 FirstNonZeroCompute = INDEX_NONE;
	FTDFog3DReplayRunOptions Options;
	Options.ExtraDumpIntervalComputes = Fog3DRestCheckIntervalComputes;
	Options.OnDump = [&](const FTDFog3DReplayDump& Dump)
	{
		const int32 DumpNonZeroCount = CountFog3DNonZeroValues(*Dump.Values, GetFog3DDumpCellCount(Replay.Resolution) * Fog3DStateFieldCount);
		++CheckedDumpCount;
		NonZeroValueCount += DumpNonZeroCount;
		FirstNonZeroCompute = (DumpNonZeroCount > 0 && FirstNonZeroCompute == INDEX_NONE) ? Dump.ComputeNumber : FirstNonZeroCompute;
		const FTDFog3DDumpCensus Census = TakeFog3DDumpCensus(*Dump.Values, Replay.Resolution);
		NonFiniteValueCount += Census.NonFiniteValues;
		BlockedCellCount = Census.BlockedCells;
	};

	const FTDFog3DReplayRunResult Result = RunTDFog3DReplay(Replay, Options);
	if (!HasFog3DReplayRun(*this, Replay, Result))
	{
		return false;
	}

	int32 WindowMoveCount = 0;
	for (const FTDFog3DReplayStep& Step : Replay.Steps)
	{
		WindowMoveCount += DoesTDFog3DReplayStepMoveWindow(Step) ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("Fog3D RestStaysZero: %d computes, %d window moves, %d checked dumps, blocked cells %d, non-zero state values %d (first at compute %d), non-finite values %d, %.1f s"),
		Result.ComputeCount, WindowMoveCount, CheckedDumpCount, BlockedCellCount, NonZeroValueCount, FirstNonZeroCompute, NonFiniteValueCount, Result.Seconds));
	TestEqual(TEXT("Dumps checked every 50 computes and at the end"), CheckedDumpCount, Replay.ComputeCount / Fog3DRestCheckIntervalComputes);
	TestTrue(TEXT("Rest scene has blocked cells"), BlockedCellCount > 0);
	TestEqual(TEXT("Rest scene keeps wind, displacement, fill and pressure at zero in every checked dump"), NonZeroValueCount, 0);
	TestEqual(TEXT("Rest scene dumps are finite"), NonFiniteValueCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DDeterminismTest,
	"TDGame.FluidFXRhi.Fog3D.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D Determinism needs a rendering RHI."));
		return false;
	}

	FTDFog3DReplay Replay;
	if (!LoadFog3DReplayByName(*this, TEXT("mixed_dt_120"), Replay))
	{
		return false;
	}

	TArray<float> NormalizedStates[2];
	for (int32 RunIndex = 0; RunIndex < 2; ++RunIndex)
	{
		FTDFog3DReplayRunOptions Options;
		Options.OnDump = [&](const FTDFog3DReplayDump& Dump)
		{
			if (Dump.Name == TEXT("end"))
			{
				MakeTDFog3DNormalizedState(*Dump.Values, Replay.Resolution, NormalizedStates[RunIndex]);
			}
		};
		if (!HasFog3DReplayRun(*this, Replay, RunTDFog3DReplay(Replay, Options)))
		{
			return false;
		}
	}

	int32 DifferentValueCount = 0;
	const bool bIsIdentical = IsFog3DNormalizedStateIdentical(NormalizedStates[0], NormalizedStates[1], DifferentValueCount);
	const int32 NonZeroValueCount = CountFog3DNonZeroValues(NormalizedStates[0], NormalizedStates[0].Num());
	AddInfo(FString::Printf(TEXT("Fog3D Determinism: %d normalized bytes, non-zero values %d, different values %d"), NormalizedStates[0].Num() * static_cast<int32>(sizeof(float)), NonZeroValueCount, DifferentValueCount));
	TestTrue(TEXT("Mixed time step scene leaves a non-trivial state"), NonZeroValueCount > 0);
	TestTrue(TEXT("Two runs from a new state give the same normalized bytes"), bIsIdentical);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DFarOriginTest,
	"TDGame.FluidFXRhi.Fog3D.FarOrigin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DFarOriginTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D FarOrigin needs a rendering RHI."));
		return false;
	}

	const TCHAR* const ReplayNames[] = { TEXT("far_origin_0"), TEXT("far_origin_p1e6"), TEXT("far_origin_m1e6"), TEXT("far_origin_p1e6_16") };
	TArray<float> NormalizedStates[UE_ARRAY_COUNT(ReplayNames)];
	for (int32 CaseIndex = 0; CaseIndex < UE_ARRAY_COUNT(ReplayNames); ++CaseIndex)
	{
		FTDFog3DReplay Replay;
		if (!LoadFog3DReplayByName(*this, ReplayNames[CaseIndex], Replay))
		{
			return false;
		}
		FTDFog3DReplayRunOptions Options;
		Options.OnDump = [&](const FTDFog3DReplayDump& Dump)
		{
			if (Dump.Name == TEXT("end"))
			{
				MakeTDFog3DNormalizedState(*Dump.Values, Replay.Resolution, NormalizedStates[CaseIndex]);
			}
		};
		if (!HasFog3DReplayRun(*this, Replay, RunTDFog3DReplay(Replay, Options)))
		{
			return false;
		}
	}

	const int32 ReferenceNonZeroCount = CountFog3DNonZeroValues(NormalizedStates[0], NormalizedStates[0].Num());
	TestTrue(TEXT("Origin 0 scene leaves a non-trivial state"), ReferenceNonZeroCount > 0);
	for (int32 CaseIndex = 1; CaseIndex < UE_ARRAY_COUNT(ReplayNames); ++CaseIndex)
	{
		int32 DifferentValueCount = 0;
		const bool bIsIdentical = IsFog3DNormalizedStateIdentical(NormalizedStates[0], NormalizedStates[CaseIndex], DifferentValueCount);
		AddInfo(FString::Printf(TEXT("Fog3D FarOrigin %s: %d normalized bytes, non-zero values %d, different values from origin 0: %d"),
			ReplayNames[CaseIndex], NormalizedStates[CaseIndex].Num() * static_cast<int32>(sizeof(float)), ReferenceNonZeroCount, DifferentValueCount));
		TestTrue(*FString::Printf(TEXT("%s gives the same normalized bytes as origin 0"), ReplayNames[CaseIndex]), bIsIdentical);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DWindowShiftOutputTest,
	"TDGame.FluidFXRhi.Fog3D.WindowShiftOutput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DWindowShiftOutputTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D WindowShiftOutput needs a rendering RHI."));
		return false;
	}

	FTDFog3DReplay Replay;
	if (!LoadFog3DReplayByName(*this, TEXT("k4_run"), Replay))
	{
		return false;
	}

	const int32 Resolution = Replay.Resolution;
	UTextureRenderTargetVolume* OutputTarget = CreateFog3DVolumeTarget(Resolution, TDFluidGpuFog3DLayerCount);
	FTextureRenderTargetResource* OutputResource = OutputTarget->GameThread_GetRenderTargetResource();
	TestNotNull(TEXT("Output target has a resource"), OutputResource);
	if (!OutputResource)
	{
		return false;
	}

	int32 MovedComputeCount = 0;
	int32 OutputCheckedCellCount = 0;
	int32 OutputNonZeroCellCount = 0;
	int32 OutputReadFailureCount = 0;
	FIntVector FirstOutputNonZeroCell = FIntVector(-1, -1, -1);
	int32 StateCheckedCellCount = 0;
	int32 StateNonZeroCellCount = 0;
	int32 MovedDumpCount = 0;
	double FrontBandMaximumFill = -1.0;
	double WindowMaximumFill = 0.0;

	FTDFog3DReplayRunOptions Options;
	Options.OutputTarget = OutputResource;
	Options.AfterCompute = [&](const FTDFog3DReplayComputeInfo& Info)
	{
		if (!Info.bWindowMoved)
		{
			return;
		}
		++MovedComputeCount;
		FlushRenderingCommands();
		for (int32 Layer = 0; Layer < TDFluidGpuFog3DLayerCount; ++Layer)
		{
			FReadSurfaceDataFlags ReadFlags;
			ReadFlags.SetArrayIndex(Layer);
			TArray<FFloat16Color> Texels;
			if (!OutputResource->ReadFloat16Pixels(Texels, ReadFlags, FIntRect(0, 0, Resolution, Resolution)) || Texels.Num() != Resolution * Resolution)
			{
				++OutputReadFailureCount;
				continue;
			}
			for (int32 LocalY = 0; LocalY < Resolution; ++LocalY)
			{
				for (int32 LocalX = 0; LocalX < Resolution; ++LocalX)
				{
					if (!IsFog3DNewStripInnerCell(Resolution, Info.WindowMoveCells, LocalX, LocalY))
					{
						continue;
					}
					const FFloat16Color& Texel = Texels[LocalY * Resolution + LocalX];
					const bool bIsZero = Texel.R.GetFloat() == 0.0f && Texel.G.GetFloat() == 0.0f && Texel.B.GetFloat() == 0.0f && Texel.A.GetFloat() == 0.0f;
					++OutputCheckedCellCount;
					OutputNonZeroCellCount += bIsZero ? 0 : 1;
					FirstOutputNonZeroCell = (!bIsZero && OutputNonZeroCellCount == 1) ? FIntVector(LocalX, LocalY, Layer) : FirstOutputNonZeroCell;
				}
			}
		}
	};
	Options.OnDump = [&](const FTDFog3DReplayDump& Dump)
	{
		const TArray<float>& Values = *Dump.Values;
		const FTDFog3DReplayStep& Step = Replay.Steps[Dump.StepIndex];
		const int32 CellCount = GetFog3DDumpCellCount(Resolution);
		if (Dump.Name.StartsWith(TEXT("mv")))
		{
			++MovedDumpCount;
			const FIntPoint MoveCells = Step.WindowMinCell - Step.PreviousWindowMinCell;
			for (int32 LocalX = 0; LocalX < Resolution; ++LocalX)
			{
				for (int32 LocalY = 0; LocalY < Resolution; ++LocalY)
				{
					if (IsFog3DCellInOldRegion(Resolution, MoveCells, LocalX, LocalY))
					{
						continue;
					}
					for (int32 Layer = 0; Layer < TDFluidGpuFog3DLayerCount; ++Layer)
					{
						const FVector3d CellCenterCm = GetTDFog3DReplayCellCenterCm(Replay, Step.WindowMinCell, LocalX, LocalY, Layer);
						if (!IsFog3DDumpCellFluid(Values, Resolution, LocalX, LocalY, Layer) || !IsFog3DCellBeyondEveryInput(Step, CellCenterCm))
						{
							continue;
						}
						const int32 CellIndex = GetFog3DDumpIndex(Resolution, LocalX, LocalY, Layer);
						const bool bHasValue = Values[CellCount * 3 + CellIndex] != 0.0f || Values[CellCount * 4 + CellIndex] != 0.0f
							|| Values[CellCount * 5 + CellIndex] != 0.0f || Values[CellCount * Fog3DGapFieldIndex + CellIndex] != 0.0f;
						++StateCheckedCellCount;
						StateNonZeroCellCount += bHasValue ? 1 : 0;
					}
				}
			}
		}
		if (Dump.Name == TEXT("end"))
		{
			double FrontMaximum = 0.0;
			for (int32 LocalX = 0; LocalX < Resolution; ++LocalX)
			{
				for (int32 LocalY = 0; LocalY < Resolution; ++LocalY)
				{
					for (int32 Layer = 0; Layer < TDFluidGpuFog3DLayerCount; ++Layer)
					{
						if (!IsFog3DDumpCellFluid(Values, Resolution, LocalX, LocalY, Layer))
						{
							continue;
						}
						const double Fill = FMath::Abs(static_cast<double>(Values[CellCount * Fog3DGapFieldIndex + GetFog3DDumpIndex(Resolution, LocalX, LocalY, Layer)]));
						WindowMaximumFill = FMath::Max(WindowMaximumFill, Fill);
						FrontMaximum = LocalX >= Resolution - Fog3DFrontBandCells ? FMath::Max(FrontMaximum, Fill) : FrontMaximum;
					}
				}
			}
			FrontBandMaximumFill = FrontMaximum;
		}
	};

	const FTDFog3DReplayRunResult Result = RunTDFog3DReplay(Replay, Options);
	const bool bHasRun = HasFog3DReplayRun(*this, Replay, Result);
	OutputTarget->MarkAsGarbage();
	if (!bHasRun)
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("Fog3D WindowShiftOutput: %d moved computes, %d moved dumps; state cells beyond every input checked %d, non-zero %d; output inner strip cells checked %d, non-zero %d (first %d,%d,%d), read failures %d; front band max |fill| %.6e, window max |fill| %.6e, ratio %.6e"),
		MovedComputeCount, MovedDumpCount, StateCheckedCellCount, StateNonZeroCellCount, OutputCheckedCellCount, OutputNonZeroCellCount,
		FirstOutputNonZeroCell.X, FirstOutputNonZeroCell.Y, FirstOutputNonZeroCell.Z, OutputReadFailureCount, FrontBandMaximumFill, WindowMaximumFill,
		WindowMaximumFill > 0.0 ? FrontBandMaximumFill / WindowMaximumFill : -1.0));
	TestEqual(TEXT("The window moved in four computes"), MovedComputeCount, Fog3DExpectedWindowMoveCount);
	TestEqual(TEXT("Every moved compute has a dump"), MovedDumpCount, Fog3DExpectedWindowMoveCount);
	TestTrue(TEXT("New strip cells beyond every input were checked"), StateCheckedCellCount > 0);
	TestEqual(TEXT("New strip cells beyond every input keep displacement and fill at zero"), StateNonZeroCellCount, 0);
	TestEqual(TEXT("Output texture reads succeeded"), OutputReadFailureCount, 0);
	TestTrue(TEXT("Output inner strip cells were checked"), OutputCheckedCellCount > 0);
	TestEqual(TEXT("Output texture inner strip is zero after every window move"), OutputNonZeroCellCount, 0);
	TestTrue(TEXT("Window holds fill at the end"), WindowMaximumFill > 0.0);
	TestTrue(TEXT("Front 16 cell band holds at most 1 percent of the largest fill"), FrontBandMaximumFill >= 0.0 && FrontBandMaximumFill <= Fog3DFrontBandFillRatio * WindowMaximumFill);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DClosedFacesTest,
	"TDGame.FluidFXRhi.Fog3D.ClosedFaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DClosedFacesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D ClosedFaces needs a rendering RHI."));
		return false;
	}

	const TCHAR* const ReplayNames[] = { TEXT("k3_box_high"), TEXT("k4_run") };
	for (const TCHAR* ReplayName : ReplayNames)
	{
		FTDFog3DReplay Replay;
		if (!LoadFog3DReplayByName(*this, ReplayName, Replay))
		{
			return false;
		}

		int32 CheckedDumpCount = 0;
		int32 ClosedFaceCount = 0;
		int32 ClosedFaceWithWindCount = 0;
		int32 NonFiniteValueCount = 0;
		int32 EndBlockedCellCount = 0;
		int32 EndOpenFaceWithWindCount = 0;
		FString FirstBadDumpName;
		FTDFog3DReplayRunOptions Options;
		Options.OnDump = [&](const FTDFog3DReplayDump& Dump)
		{
			const FTDFog3DDumpCensus Census = TakeFog3DDumpCensus(*Dump.Values, Replay.Resolution);
			++CheckedDumpCount;
			ClosedFaceCount += Census.ClosedFaces;
			ClosedFaceWithWindCount += Census.ClosedFacesWithWind;
			NonFiniteValueCount += Census.NonFiniteValues;
			FirstBadDumpName = (Census.ClosedFacesWithWind > 0 && FirstBadDumpName.IsEmpty()) ? Dump.Name : FirstBadDumpName;
			if (Dump.Name == TEXT("end"))
			{
				EndBlockedCellCount = Census.BlockedCells;
				EndOpenFaceWithWindCount = Census.OpenFacesWithWind;
			}
		};

		const FTDFog3DReplayRunResult Result = RunTDFog3DReplay(Replay, Options);
		if (!HasFog3DReplayRun(*this, Replay, Result))
		{
			return false;
		}

		AddInfo(FString::Printf(TEXT("Fog3D ClosedFaces %s: %d computes, %d dumps checked (replay lists %d), closed faces counted %d, closed faces with wind %d (first bad dump '%s'), end blocked cells %d, end open faces with wind %d, non-finite values %d, %.1f s"),
			ReplayName, Result.ComputeCount, CheckedDumpCount, Replay.DumpCount, ClosedFaceCount, ClosedFaceWithWindCount, *FirstBadDumpName, EndBlockedCellCount, EndOpenFaceWithWindCount, NonFiniteValueCount, Result.Seconds));
		TestEqual(*FString::Printf(TEXT("%s: every listed dump checked"), ReplayName), CheckedDumpCount, Replay.DumpCount);
		TestTrue(*FString::Printf(TEXT("%s: scene has blocked cells"), ReplayName), EndBlockedCellCount > 0);
		TestTrue(*FString::Printf(TEXT("%s: scene has wind on open faces"), ReplayName), EndOpenFaceWithWindCount > 0);
		TestEqual(*FString::Printf(TEXT("%s: closed faces and seam faces keep zero wind"), ReplayName), ClosedFaceWithWindCount, 0);
		TestEqual(*FString::Printf(TEXT("%s: dumps are finite"), ReplayName), NonFiniteValueCount, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DSealedColumnTest,
	"TDGame.FluidFXRhi.Fog3D.SealedColumn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DSealedColumnTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D SealedColumn needs a rendering RHI."));
		return false;
	}

	FTDFog3DReplay Replay;
	if (!LoadFog3DReplayByName(*this, TEXT("sealed_cells_600"), Replay))
	{
		return false;
	}

	int32 CheckedDumpCount = 0;
	int32 NonFiniteValueCount = 0;
	double MaximumWindOverRun = 0.0;
	FTDFog3DDumpCensus EndCensus;
	FTDFog3DReplayRunOptions Options;
	Options.ExtraDumpIntervalComputes = Fog3DSealedCheckIntervalComputes;
	Options.OnDump = [&](const FTDFog3DReplayDump& Dump)
	{
		const FTDFog3DDumpCensus Census = TakeFog3DDumpCensus(*Dump.Values, Replay.Resolution);
		++CheckedDumpCount;
		NonFiniteValueCount += Census.NonFiniteValues;
		MaximumWindOverRun = FMath::Max(MaximumWindOverRun, Census.MaximumWindCmPerSec);
		EndCensus = Census;
	};

	const FTDFog3DReplayRunResult Result = RunTDFog3DReplay(Replay, Options);
	if (!HasFog3DReplayRun(*this, Replay, Result))
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("Fog3D SealedColumn: %d computes, %d dumps checked, non-finite values %d, end max wind %.6f, max wind over the run %.6f, end blocked cells %d, end open faces with wind %d, end closed faces with wind %d, %.1f s"),
		Result.ComputeCount, CheckedDumpCount, NonFiniteValueCount, EndCensus.MaximumWindCmPerSec, MaximumWindOverRun, EndCensus.BlockedCells, EndCensus.OpenFacesWithWind, EndCensus.ClosedFacesWithWind, Result.Seconds));
	TestTrue(TEXT("Sealed scene has blocked cells"), EndCensus.BlockedCells > 0);
	TestEqual(TEXT("Every checked dump is finite"), NonFiniteValueCount, 0);
	TestEqual(TEXT("Every dump of the run was checked"), CheckedDumpCount, Result.DumpCount);
	TestTrue(TEXT("Interval dumps cover the run"), CheckedDumpCount >= Replay.ComputeCount / Fog3DSealedCheckIntervalComputes);
	TestTrue(TEXT("Wind stays at or below 3000 cm/s at the end"), EndCensus.MaximumWindCmPerSec <= Fog3DSealedMaximumWindCmPerSec);
	TestTrue(TEXT("Wind stays at or below 3000 cm/s in every checked dump"), MaximumWindOverRun <= Fog3DSealedMaximumWindCmPerSec);
	TestEqual(TEXT("Closed faces keep zero wind"), EndCensus.ClosedFacesWithWind, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DReplayDumpsTest,
	"TDGame.FluidFXRhi.Fog3D.ReplayDumps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DReplayDumpsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D ReplayDumps needs a rendering RHI."));
		return false;
	}

	TArray<FString> FileNames;
	IFileManager::Get().FindFiles(FileNames, *FPaths::Combine(GetTDFog3DReplayDirectory(), TEXT("*.json")), true, false);
	FileNames.Sort();
	TestEqual(TEXT("Replay file count"), FileNames.Num(), Fog3DExpectedReplayFileCount);

	int32 TotalComputeCount = 0;
	int32 TotalDumpCount = 0;
	int32 TotalNonFiniteValueCount = 0;
	int32 FailedReplayCount = 0;
	double TotalSeconds = 0.0;
	for (const FString& FileName : FileNames)
	{
		FTDFog3DReplay Replay;
		FString Error;
		if (!LoadTDFog3DReplay(FPaths::Combine(GetTDFog3DReplayDirectory(), FileName), Replay, Error))
		{
			AddError(Error);
			++FailedReplayCount;
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s: replay name matches the file name"), *FileName), Replay.Name, FPaths::GetBaseFilename(FileName));

		int32 NonFiniteValueCount = 0;
		FTDFog3DReplayRunOptions Options;
		Options.bWritesDumpFiles = true;
		Options.OnDump = [&](const FTDFog3DReplayDump& Dump)
		{
			for (const float Value : *Dump.Values)
			{
				NonFiniteValueCount += FMath::IsFinite(Value) ? 0 : 1;
			}
		};
		const FTDFog3DReplayRunResult Result = RunTDFog3DReplay(Replay, Options);
		const bool bHasRun = HasFog3DReplayRun(*this, Replay, Result);
		TestEqual(*FString::Printf(TEXT("%s: every listed dump written"), *Replay.Name), Result.WrittenFileCount, Replay.DumpCount);
		TestEqual(*FString::Printf(TEXT("%s: dump files without write errors"), *Replay.Name), Result.FailedFileCount, 0);
		TestEqual(*FString::Printf(TEXT("%s: dumps are finite"), *Replay.Name), NonFiniteValueCount, 0);
		FailedReplayCount += bHasRun && Result.WrittenFileCount == Replay.DumpCount && NonFiniteValueCount == 0 ? 0 : 1;
		TotalComputeCount += Result.ComputeCount;
		TotalDumpCount += Result.WrittenFileCount;
		TotalNonFiniteValueCount += NonFiniteValueCount;
		TotalSeconds += Result.Seconds;
		AddInfo(FString::Printf(TEXT("Fog3D ReplayDumps %s: %d computes, %d dumps, %.1f s"), *Replay.Name, Result.ComputeCount, Result.WrittenFileCount, Result.Seconds));
	}

	AddInfo(FString::Printf(TEXT("Fog3D ReplayDumps: %d replays, failed %d, %d computes, %d dumps written to %s, non-finite values %d, %.1f s"),
		FileNames.Num(), FailedReplayCount, TotalComputeCount, TotalDumpCount, *GetTDFog3DDumpDirectory(), TotalNonFiniteValueCount, TotalSeconds));
	TestEqual(TEXT("Every replay ran and dumped cleanly"), FailedReplayCount, 0);
	return true;
}

namespace TDFluidGpuFog3DRhiTestPrivate
{
	struct FTDFog3DTimingCase
	{
		int32 Resolution = 0;
		TArray<double> StillMilliseconds;
		TArray<double> MovingMilliseconds;
	};

	TArray<FTDFluidGpuFog3DFrameInput> MakeFog3DTimingSteps(int32 Resolution)
	{
		const FTDFluidGpuFog3DInflation Inflation;
		TArray<FTDFluidGpuFog3DFrameInput> Steps;
		for (int32 StepIndex = 0; StepIndex < Fog3DTimingWarmupComputeCount + Fog3DTimingMeasuredComputeCount; ++StepIndex)
		{
			const int32 WindowMoveIndex = StepIndex / Fog3DTimingWindowMovePeriod;
			FTDFluidGpuFog3DFrameInput Step;
			Step.Resolution = Resolution;
			Step.bResetsState = StepIndex == 0;
			Step.WindowMinCell = FIntPoint(16 * WindowMoveIndex, 0);
			Step.PreviousWindowMinCell = FIntPoint(16 * (StepIndex > 0 ? (StepIndex - 1) / Fog3DTimingWindowMovePeriod : 0), 0);
			Step.Obstacles.Add(MakeTDFluidGpuFog3DUprightBox(FVector3d(250.0, 250.0, 60.0), FVector3d(40.0, 15.0, 60.0), 30.0, Inflation));
			const FVector3d SwingStartCm = FVector3d(300.0 + (StepIndex % Fog3DTimingWindowMovePeriod) * 25.0, 200.0, 50.0);
			FTDFluidGpuFog3DSphere Sphere;
			Sphere.StartCmRadiusCm = FVector4f(FVector3f(SwingStartCm), 30.0f);
			Sphere.EndCmWindStrength = FVector4f(FVector3f(SwingStartCm + FVector3d(25.0, 0.0, 0.0)), 1.0f);
			Sphere.VelocityCmPerSecErosionPerSec = FVector4f(1500.0f, 0.0f, 0.0f, 30.0f);
			Step.Spheres.Add(Sphere);
			Steps.Add(Step);
		}
		return Steps;
	}

	double GetFog3DPercentile(TArray<double> Values, double Fraction)
	{
		if (Values.IsEmpty())
		{
			return 0.0;
		}
		Values.Sort();
		return Values[FMath::Clamp(FMath::CeilToInt(Fraction * Values.Num()) - 1, 0, Values.Num() - 1)];
	}

	FTDFog3DTimingCase MeasureFog3DSteps(int32 Resolution)
	{
		TSharedRef<FTDFog3DTimingCase, ESPMode::ThreadSafe> TimingCase = MakeShared<FTDFog3DTimingCase, ESPMode::ThreadSafe>();
		TimingCase->Resolution = Resolution;
		const TArray<FTDFluidGpuFog3DFrameInput> Steps = MakeFog3DTimingSteps(Resolution);
		ENQUEUE_RENDER_COMMAND(TDFluidGpuFog3DStepTiming)(
			[TimingCase, Steps](FRHICommandListImmediate& RHICmdList)
			{
				FTDFluidGpuFog3DState State;
				TArray<FRenderQueryRHIRef> StartQueries;
				TArray<FRenderQueryRHIRef> EndQueries;
				TArray<bool> MeasuredStepMoved;
				for (int32 StepIndex = 0; StepIndex < Steps.Num(); ++StepIndex)
				{
					const bool bIsMeasured = StepIndex >= Fog3DTimingWarmupComputeCount;
					if (bIsMeasured)
					{
						RHICmdList.EndRenderQuery(StartQueries.Add_GetRef(RHICreateRenderQuery(RQT_AbsoluteTime)));
						MeasuredStepMoved.Add(Steps[StepIndex].WindowMinCell != Steps[StepIndex].PreviousWindowMinCell);
					}
					{
						FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuFog3DTimingStep"));
						AddTDFluidGpuFog3DPasses(GraphBuilder, State, Steps[StepIndex]);
						GraphBuilder.Execute();
					}
					if (bIsMeasured)
					{
						RHICmdList.EndRenderQuery(EndQueries.Add_GetRef(RHICreateRenderQuery(RQT_AbsoluteTime)));
					}
					RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
				}
				RHICmdList.SubmitAndBlockUntilGPUIdle();
				for (int32 QueryIndex = 0; QueryIndex < StartQueries.Num() && QueryIndex < EndQueries.Num(); ++QueryIndex)
				{
					uint64 StartMicroseconds = 0;
					uint64 EndMicroseconds = 0;
					if (RHIGetRenderQueryResult(StartQueries[QueryIndex], StartMicroseconds, true) && RHIGetRenderQueryResult(EndQueries[QueryIndex], EndMicroseconds, true) && EndMicroseconds >= StartMicroseconds)
					{
						const double Milliseconds = static_cast<double>(EndMicroseconds - StartMicroseconds) / 1000.0;
						(MeasuredStepMoved[QueryIndex] ? TimingCase->MovingMilliseconds : TimingCase->StillMilliseconds).Add(Milliseconds);
					}
				}
				State.Release();
			});
		FlushRenderingCommands();
		return *TimingCase;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DStepTimingTest,
	"TDGame.FluidFXRhi.Fog3D.StepTiming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DStepTimingTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D StepTiming needs a rendering RHI."));
		return false;
	}

	for (const int32 Resolution : { 128, 256 })
	{
		const FTDFog3DTimingCase TimingCase = MeasureFog3DSteps(Resolution);
		AddInfo(FString::Printf(TEXT("Fog3D StepTiming %dx%dx8 (information only): plain computes %d, median %.4f ms, p95 %.4f ms; computes with window move (bake + clean) %d, median %.4f ms, p95 %.4f ms"),
			Resolution, Resolution, TimingCase.StillMilliseconds.Num(), GetFog3DPercentile(TimingCase.StillMilliseconds, 0.5), GetFog3DPercentile(TimingCase.StillMilliseconds, 0.95),
			TimingCase.MovingMilliseconds.Num(), GetFog3DPercentile(TimingCase.MovingMilliseconds, 0.5), GetFog3DPercentile(TimingCase.MovingMilliseconds, 0.95)));
		TestTrue(*FString::Printf(TEXT("Timing queries returned samples at %d"), Resolution), TimingCase.StillMilliseconds.Num() > 0);
	}
	return true;
}

namespace TDFluidGpuFog3DRhiTestPrivate
{
	constexpr int32 Fog3DDisplacementXFieldIndex = 3;
	constexpr int32 Fog3DDrawFillChannelCount = 4;
	constexpr int32 Fog3DBlockedDrawCheckCompute = 30;
	constexpr int32 Fog3DExpectedAddendumReplayFileCount = 3;
	constexpr int32 Fog3DExpectedStage1dReplayFileCount = 9;
	constexpr double Fog3DStateDrawTolerance = 1e-5;
	constexpr double Fog3DOutputDrawTolerance = 1e-3;

	struct FTDFog3DWindowCheckResult
	{
		bool bFirstComputeAccepted = false;
		bool bMatchingComputeAccepted = false;
		bool bMovedStaleComputeAccepted = true;
		bool bStillStaleComputeAccepted = true;
		bool bStateKeptAfterStale = false;
		int32 ChangedValueCountAfterStale = -1;
		int32 NonZeroValueCountBeforeStale = 0;
		bool bResetComputeAccepted = false;
		bool bNewResolutionComputeAccepted = false;
		bool bAfterReleaseComputeAccepted = false;
	};

	FTDFluidGpuFog3DFrameInput MakeFog3DWindowCheckInput(const FIntPoint& WindowMinCell, const FIntPoint& PreviousWindowMinCell, bool bResetsState)
	{
		FTDFluidGpuFog3DFrameInput FrameInput = MakeFog3DSmokeFrameInput();
		FrameInput.WindowMinCell = WindowMinCell;
		FrameInput.PreviousWindowMinCell = PreviousWindowMinCell;
		FrameInput.bResetsState = bResetsState;
		FTDFluidGpuFog3DSphere& Sphere = FrameInput.Spheres.AddDefaulted_GetRef();
		Sphere.StartCmRadiusCm = FVector4f(200.0f, 250.0f, 45.0f, 30.0f);
		Sphere.EndCmWindStrength = FVector4f(260.0f, 250.0f, 45.0f, 1.0f);
		Sphere.VelocityCmPerSecErosionPerSec = FVector4f(1500.0f, 0.0f, 0.0f, 30.0f);
		return FrameInput;
	}

	bool AddFog3DWindowCheckCompute(FRHICommandListImmediate& RHICmdList, FTDFluidGpuFog3DState& State, const FTDFluidGpuFog3DFrameInput& FrameInput)
	{
		FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuFog3DWindowCheck"));
		const bool bIsAccepted = AddTDFluidGpuFog3DPasses(GraphBuilder, State, FrameInput);
		GraphBuilder.Execute();
		return bIsAccepted;
	}

	bool ReadFog3DWindowCheckDump(FRHICommandListImmediate& RHICmdList, FTDFluidGpuFog3DState& State, TArray<float>& OutValues)
	{
		FRHIGPUBufferReadback Readback(TEXT("TDFluidGpu.Fog3DWindowCheckDump"));
		bool bIsAccepted = false;
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuFog3DWindowCheckDump"));
			bIsAccepted = AddTDFluidGpuFog3DDumpPass(GraphBuilder, State, Readback);
			GraphBuilder.Execute();
		}
		RHICmdList.SubmitAndBlockUntilGPUIdle();
		if (!bIsAccepted)
		{
			return false;
		}
		int32 ExtraSubmitCount = 0;
		SubmitUntilFog3DReadbackIsReady(RHICmdList, Readback, ExtraSubmitCount);
		return CopyTDFluidGpuFog3DDump(Readback, State.Resolution, OutValues);
	}

	void RunFog3DWindowCheck(FRHICommandListImmediate& RHICmdList, FTDFog3DWindowCheckResult& Result)
	{
		FTDFluidGpuFog3DState State;
		Result.bFirstComputeAccepted = AddFog3DWindowCheckCompute(RHICmdList, State, MakeFog3DWindowCheckInput(FIntPoint(0, 0), FIntPoint(48, -32), false));
		Result.bMatchingComputeAccepted = AddFog3DWindowCheckCompute(RHICmdList, State, MakeFog3DWindowCheckInput(FIntPoint(16, 0), FIntPoint(0, 0), false));

		TArray<float> BeforeValues;
		const bool bHasBeforeValues = ReadFog3DWindowCheckDump(RHICmdList, State, BeforeValues);
		const FIntPoint WindowBefore = State.WindowMinCell;
		const int32 WindIndexBefore = State.CurrentWindIndex;
		const int32 FillIndexBefore = State.CurrentDisplacementFillIndex;
		Result.bMovedStaleComputeAccepted = AddFog3DWindowCheckCompute(RHICmdList, State, MakeFog3DWindowCheckInput(FIntPoint(32, 0), FIntPoint(0, 0), false));
		Result.bStillStaleComputeAccepted = AddFog3DWindowCheckCompute(RHICmdList, State, MakeFog3DWindowCheckInput(FIntPoint(16, 0), FIntPoint(0, 0), false));
		TArray<float> AfterValues;
		const bool bHasAfterValues = ReadFog3DWindowCheckDump(RHICmdList, State, AfterValues);
		Result.NonZeroValueCountBeforeStale = CountFog3DNonZeroValues(BeforeValues, BeforeValues.Num());
		const bool bIsDumpIdentical = IsFog3DNormalizedStateIdentical(BeforeValues, AfterValues, Result.ChangedValueCountAfterStale);
		Result.bStateKeptAfterStale = bHasBeforeValues && bHasAfterValues && bIsDumpIdentical && State.WindowMinCell == WindowBefore
			&& State.CurrentWindIndex == WindIndexBefore && State.CurrentDisplacementFillIndex == FillIndexBefore;

		Result.bResetComputeAccepted = AddFog3DWindowCheckCompute(RHICmdList, State, MakeFog3DWindowCheckInput(FIntPoint(64, 0), FIntPoint(0, 0), true));
		FTDFluidGpuFog3DFrameInput NewResolutionInput = MakeFog3DWindowCheckInput(FIntPoint(64, 0), FIntPoint(0, 0), false);
		NewResolutionInput.Resolution = Fog3DSmokeResolution * 2;
		Result.bNewResolutionComputeAccepted = AddFog3DWindowCheckCompute(RHICmdList, State, NewResolutionInput);
		State.Release();
		Result.bAfterReleaseComputeAccepted = AddFog3DWindowCheckCompute(RHICmdList, State, MakeFog3DWindowCheckInput(FIntPoint(0, 0), FIntPoint(16, 16), false));
		State.Release();
	}

	FVector4d ComputeFog3DExpectedDrawFill(const TArray<float>& BeforeValues, int32 Resolution, int32 LocalX, int32 LocalY, int32 Layer)
	{
		const FIntVector NeighborOffsets[] = { FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1) };
		const int32 CellCount = GetFog3DDumpCellCount(Resolution);
		FVector4d Sum(0.0, 0.0, 0.0, 0.0);
		int32 NeighborCount = 0;
		for (const FIntVector& Offset : NeighborOffsets)
		{
			const int32 NeighborX = LocalX + Offset.X;
			const int32 NeighborY = LocalY + Offset.Y;
			const int32 NeighborLayer = Layer + Offset.Z;
			const bool bIsInside = NeighborX >= 0 && NeighborY >= 0 && NeighborLayer >= 0 && NeighborX < Resolution && NeighborY < Resolution && NeighborLayer < TDFluidGpuFog3DLayerCount;
			if (!bIsInside || !IsFog3DDumpCellFluid(BeforeValues, Resolution, NeighborX, NeighborY, NeighborLayer))
			{
				continue;
			}
			const int32 NeighborIndex = GetFog3DDumpIndex(Resolution, NeighborX, NeighborY, NeighborLayer);
			for (int32 Channel = 0; Channel < Fog3DDrawFillChannelCount; ++Channel)
			{
				Sum[Channel] += BeforeValues[CellCount * (Fog3DDisplacementXFieldIndex + Channel) + NeighborIndex];
			}
			++NeighborCount;
		}
		return NeighborCount > 0 ? Sum / static_cast<double>(NeighborCount) : Sum;
	}

	bool IsFog3DDrawValueClose(double Actual, double Expected, double Tolerance)
	{
		return FMath::IsFinite(Actual) && FMath::Abs(Actual - Expected) <= Tolerance + Tolerance * FMath::Abs(Expected);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DPreviousWindowCheckTest,
	"TDGame.FluidFXRhi.Fog3D.PreviousWindowCheck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DPreviousWindowCheckTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D PreviousWindowCheck needs a rendering RHI."));
		return false;
	}

	TSharedRef<FTDFog3DWindowCheckResult, ESPMode::ThreadSafe> Result = MakeShared<FTDFog3DWindowCheckResult, ESPMode::ThreadSafe>();
	ENQUEUE_RENDER_COMMAND(TDFluidGpuFog3DPreviousWindowCheck)(
		[Result](FRHICommandListImmediate& RHICmdList)
		{
			RunFog3DWindowCheck(RHICmdList, *Result);
		});
	FlushRenderingCommands();

	AddInfo(FString::Printf(TEXT("Fog3D PreviousWindowCheck: first %d, matching %d, stale with move %d, stale without move %d, state kept %d (changed values %d, non-zero values before %d), reset %d, new resolution %d, after release %d"),
		Result->bFirstComputeAccepted, Result->bMatchingComputeAccepted, Result->bMovedStaleComputeAccepted, Result->bStillStaleComputeAccepted, Result->bStateKeptAfterStale,
		Result->ChangedValueCountAfterStale, Result->NonZeroValueCountBeforeStale, Result->bResetComputeAccepted, Result->bNewResolutionComputeAccepted, Result->bAfterReleaseComputeAccepted));
	TestTrue(TEXT("First compute ignores the previous window"), Result->bFirstComputeAccepted);
	TestTrue(TEXT("Compute whose previous window matches the state is accepted"), Result->bMatchingComputeAccepted);
	TestFalse(TEXT("Compute moving the window from a stale previous window is rejected"), Result->bMovedStaleComputeAccepted);
	TestFalse(TEXT("Compute keeping the window but naming a stale previous window is rejected"), Result->bStillStaleComputeAccepted);
	TestTrue(TEXT("State before the stale computes holds values"), Result->NonZeroValueCountBeforeStale > 0);
	TestTrue(TEXT("Rejected computes leave the state, window and texture indices unchanged"), Result->bStateKeptAfterStale);
	TestTrue(TEXT("Reset compute ignores the previous window"), Result->bResetComputeAccepted);
	TestTrue(TEXT("First compute at a new resolution ignores the previous window"), Result->bNewResolutionComputeAccepted);
	TestTrue(TEXT("First compute after release ignores the previous window"), Result->bAfterReleaseComputeAccepted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DBlockedCellDrawFillTest,
	"TDGame.FluidFXRhi.Fog3D.BlockedCellDrawFill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DBlockedCellDrawFillTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D BlockedCellDrawFill needs a rendering RHI."));
		return false;
	}

	FTDFog3DReplay Replay;
	if (!LoadFog3DReplayByName(*this, TEXT("k3_box_high"), Replay))
	{
		return false;
	}

	const int32 Resolution = Replay.Resolution;
	UTextureRenderTargetVolume* OutputTarget = CreateFog3DVolumeTarget(Resolution, TDFluidGpuFog3DLayerCount);
	FTextureRenderTargetResource* OutputResource = OutputTarget->GameThread_GetRenderTargetResource();
	TestNotNull(TEXT("Output target has a resource"), OutputResource);
	if (!OutputResource)
	{
		return false;
	}

	TArray<float> BeforeValues;
	TArray<float> AfterValues;
	TArray<TArray<FFloat16Color>> OutputLayers;
	int32 OutputReadFailureCount = 0;
	bool bCheckComputeIsPlain = false;
	FTDFog3DReplayRunOptions Options;
	Options.OutputTarget = OutputResource;
	Options.ExtraDumpIntervalComputes = 1;
	Options.AfterCompute = [&](const FTDFog3DReplayComputeInfo& Info)
	{
		if (Info.ComputeNumber != Fog3DBlockedDrawCheckCompute)
		{
			return;
		}
		const FTDFog3DReplayStep& Step = Replay.Steps[Info.StepIndex];
		const bool bIsFirstRepeat = Info.RepeatIndex == 0;
		const bool bHasOverride = Step.Wind.Kind != ETDFog3DReplayWindKind::None || (bIsFirstRepeat && !Step.Operations.IsEmpty());
		bCheckComputeIsPlain = Replay.Mode == 2 && !Info.bWindowMoved && !(bIsFirstRepeat && Step.bResets) && !(bIsFirstRepeat && Step.bChangesObstacles) && !bHasOverride;
		FlushRenderingCommands();
		OutputLayers.SetNum(TDFluidGpuFog3DLayerCount);
		for (int32 Layer = 0; Layer < TDFluidGpuFog3DLayerCount; ++Layer)
		{
			FReadSurfaceDataFlags ReadFlags;
			ReadFlags.SetArrayIndex(Layer);
			const bool bIsRead = OutputResource->ReadFloat16Pixels(OutputLayers[Layer], ReadFlags, FIntRect(0, 0, Resolution, Resolution));
			OutputReadFailureCount += bIsRead && OutputLayers[Layer].Num() == Resolution * Resolution ? 0 : 1;
		}
	};
	Options.OnDump = [&](const FTDFog3DReplayDump& Dump)
	{
		if (Dump.ComputeNumber == Fog3DBlockedDrawCheckCompute - 1)
		{
			BeforeValues = *Dump.Values;
		}
		if (Dump.ComputeNumber == Fog3DBlockedDrawCheckCompute)
		{
			AfterValues = *Dump.Values;
		}
	};

	const FTDFog3DReplayRunResult Result = RunTDFog3DReplay(Replay, Options);
	const bool bHasRun = HasFog3DReplayRun(*this, Replay, Result);
	OutputTarget->MarkAsGarbage();
	const int32 CellCount = GetFog3DDumpCellCount(Resolution);
	const int32 DumpValueCount = CellCount * TDFluidGpuFog3DDumpFieldCount;
	if (!bHasRun || BeforeValues.Num() != DumpValueCount || AfterValues.Num() != DumpValueCount || OutputReadFailureCount != 0)
	{
		AddError(FString::Printf(TEXT("Fog3D BlockedCellDrawFill could not read compute %d: before %d values, after %d values, output read failures %d"),
			Fog3DBlockedDrawCheckCompute, BeforeValues.Num(), AfterValues.Num(), OutputReadFailureCount));
		return false;
	}

	int32 BlockedCellCount = 0;
	int32 NonZeroExpectedCellCount = 0;
	int32 StateMismatchCount = 0;
	int32 OutputMismatchCount = 0;
	double StateMaximumError = 0.0;
	double OutputMaximumError = 0.0;
	FIntVector FirstMismatchCell(-1, -1, -1);
	for (int32 LocalX = 0; LocalX < Resolution; ++LocalX)
	{
		for (int32 LocalY = 0; LocalY < Resolution; ++LocalY)
		{
			for (int32 Layer = 0; Layer < TDFluidGpuFog3DLayerCount; ++Layer)
			{
				if (IsFog3DDumpCellFluid(AfterValues, Resolution, LocalX, LocalY, Layer))
				{
					continue;
				}
				++BlockedCellCount;
				const FVector4d Expected = ComputeFog3DExpectedDrawFill(BeforeValues, Resolution, LocalX, LocalY, Layer);
				NonZeroExpectedCellCount += Expected != FVector4d(0.0, 0.0, 0.0, 0.0) ? 1 : 0;
				const int32 CellIndex = GetFog3DDumpIndex(Resolution, LocalX, LocalY, Layer);
				const FFloat16Color& Texel = OutputLayers[Layer][LocalY * Resolution + LocalX];
				const double OutputValues[Fog3DDrawFillChannelCount] = { Texel.R.GetFloat(), Texel.G.GetFloat(), Texel.B.GetFloat(), Texel.A.GetFloat() };
				bool bStateMatches = true;
				bool bOutputMatches = true;
				for (int32 Channel = 0; Channel < Fog3DDrawFillChannelCount; ++Channel)
				{
					const double StateValue = AfterValues[CellCount * (Fog3DDisplacementXFieldIndex + Channel) + CellIndex];
					StateMaximumError = FMath::Max(StateMaximumError, FMath::Abs(StateValue - Expected[Channel]));
					OutputMaximumError = FMath::Max(OutputMaximumError, FMath::Abs(OutputValues[Channel] - Expected[Channel]));
					bStateMatches &= IsFog3DDrawValueClose(StateValue, Expected[Channel], Fog3DStateDrawTolerance);
					bOutputMatches &= IsFog3DDrawValueClose(OutputValues[Channel], Expected[Channel], Fog3DOutputDrawTolerance);
				}
				StateMismatchCount += bStateMatches ? 0 : 1;
				OutputMismatchCount += bOutputMatches ? 0 : 1;
				FirstMismatchCell = (!(bStateMatches && bOutputMatches) && FirstMismatchCell.X < 0) ? FIntVector(LocalX, LocalY, Layer) : FirstMismatchCell;
			}
		}
	}

	AddInfo(FString::Printf(TEXT("Fog3D BlockedCellDrawFill %s compute %d: blocked cells %d, non-zero expected %d, state mismatches %d (max error %.3e), output mismatches %d (max error %.3e), first mismatch %d,%d,%d"),
		*Replay.Name, Fog3DBlockedDrawCheckCompute, BlockedCellCount, NonZeroExpectedCellCount, StateMismatchCount, StateMaximumError, OutputMismatchCount, OutputMaximumError,
		FirstMismatchCell.X, FirstMismatchCell.Y, FirstMismatchCell.Z));
	TestTrue(TEXT("Checked compute has no window move, obstacle change, reset or override"), bCheckComputeIsPlain);
	TestTrue(TEXT("Scene has blocked cells"), BlockedCellCount > 0);
	TestTrue(TEXT("Some blocked cell expects a non-zero draw value"), NonZeroExpectedCellCount > 0);
	TestEqual(TEXT("State draw values of blocked cells equal the fluid face neighbor average"), StateMismatchCount, 0);
	TestEqual(TEXT("Output draw values of blocked cells equal the fluid face neighbor average"), OutputMismatchCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DBlockedCellDrawFillAtWindowEdgeTest,
	"TDGame.FluidFXRhi.Fog3D.BlockedCellDrawFillAtWindowEdge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DBlockedCellDrawFillAtWindowEdgeTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D BlockedCellDrawFillAtWindowEdge needs a rendering RHI."));
		return false;
	}

	FTDFog3DReplay Replay;
	if (!LoadFog3DReplayByName(*this, TEXT("k3_box_high"), Replay))
	{
		return false;
	}

	const double EdgeCenterCm = 975.0;
	const double PushStartCm = 750.0;
	const double WrapSourceCm = 985.0;
	Replay.Obstacles.Reset();
	const double NearBoxOffsetCm = 500.0;
	for (const FVector2d& BoxCenter : { FVector2d(EdgeCenterCm, 0.0), FVector2d(0.0, EdgeCenterCm), FVector2d(-EdgeCenterCm, NearBoxOffsetCm), FVector2d(NearBoxOffsetCm, -EdgeCenterCm) })
	{
		FTDFog3DReplayObstacle& Box = Replay.Obstacles.AddDefaulted_GetRef();
		Box.CenterCm = FVector3d(BoxCenter.X, BoxCenter.Y, 90.0);
		Box.HalfExtentCm = FVector3d(75.0, 75.0, 90.0);
	}
	struct FEdgePush
	{
		FVector2d PositionCm;
		FVector2d DirectionCm;
	};
	const FEdgePush EdgePushes[] = {
		{ FVector2d(PushStartCm, 0.0), FVector2d(1.0, 0.0) },
		{ FVector2d(0.0, PushStartCm), FVector2d(0.0, 1.0) },
		{ FVector2d(-WrapSourceCm, 0.0), FVector2d(1.0, 0.0) },
		{ FVector2d(0.0, -WrapSourceCm), FVector2d(0.0, 1.0) },
	};
	const FEdgePush NearBoxWrapPushes[] = {
		{ FVector2d(WrapSourceCm, NearBoxOffsetCm), FVector2d(-1.0, 0.0) },
		{ FVector2d(NearBoxOffsetCm, WrapSourceCm), FVector2d(0.0, -1.0) },
	};
	for (FTDFog3DReplayStep& Step : Replay.Steps)
	{
		Step.Spheres.Reset();
		for (const FEdgePush& Push : NearBoxWrapPushes)
		{
			FTDFog3DReplaySphere& Sphere = Step.Spheres.AddDefaulted_GetRef();
			Sphere.StartCm = FVector3d(Push.PositionCm.X, Push.PositionCm.Y, 90.0);
			Sphere.EndCm = Sphere.StartCm;
			Sphere.RadiusCm = 40.0;
			Sphere.VelocityCmPerSec = FVector3d(Push.DirectionCm.X * 400.0, Push.DirectionCm.Y * 400.0, 0.0);
			Sphere.WindStrength = 1.0;
		}
		for (const FEdgePush& Push : EdgePushes)
		{
			for (const double HeightCm : { 20.0, 90.0, 160.0 })
			{
				FTDFog3DReplaySphere& Sphere = Step.Spheres.AddDefaulted_GetRef();
				Sphere.StartCm = FVector3d(Push.PositionCm.X, Push.PositionCm.Y, HeightCm);
				Sphere.EndCm = Sphere.StartCm;
				Sphere.RadiusCm = 40.0;
				Sphere.VelocityCmPerSec = FVector3d(Push.DirectionCm.X * 400.0, Push.DirectionCm.Y * 400.0, 0.0);
				Sphere.WindStrength = 1.0;
			}
		}
	}

	const int32 Resolution = Replay.Resolution;
	UTextureRenderTargetVolume* OutputTarget = CreateFog3DVolumeTarget(Resolution, TDFluidGpuFog3DLayerCount);
	FTextureRenderTargetResource* OutputResource = OutputTarget->GameThread_GetRenderTargetResource();
	TestNotNull(TEXT("Output target has a resource"), OutputResource);
	if (!OutputResource)
	{
		return false;
	}

	TArray<float> BeforeValues;
	TArray<float> AfterValues;
	TArray<TArray<FFloat16Color>> OutputLayers;
	int32 OutputReadFailureCount = 0;
	bool bCheckComputeIsPlain = false;
	FTDFog3DReplayRunOptions Options;
	Options.OutputTarget = OutputResource;
	Options.ExtraDumpIntervalComputes = 1;
	Options.AfterCompute = [&](const FTDFog3DReplayComputeInfo& Info)
	{
		if (Info.ComputeNumber != Fog3DBlockedDrawCheckCompute)
		{
			return;
		}
		const FTDFog3DReplayStep& Step = Replay.Steps[Info.StepIndex];
		const bool bIsFirstRepeat = Info.RepeatIndex == 0;
		const bool bHasOverride = Step.Wind.Kind != ETDFog3DReplayWindKind::None || (bIsFirstRepeat && !Step.Operations.IsEmpty());
		bCheckComputeIsPlain = Replay.Mode == 2 && !Info.bWindowMoved && !(bIsFirstRepeat && Step.bResets) && !(bIsFirstRepeat && Step.bChangesObstacles) && !bHasOverride;
		FlushRenderingCommands();
		OutputLayers.SetNum(TDFluidGpuFog3DLayerCount);
		for (int32 Layer = 0; Layer < TDFluidGpuFog3DLayerCount; ++Layer)
		{
			FReadSurfaceDataFlags ReadFlags;
			ReadFlags.SetArrayIndex(Layer);
			const bool bIsRead = OutputResource->ReadFloat16Pixels(OutputLayers[Layer], ReadFlags, FIntRect(0, 0, Resolution, Resolution));
			OutputReadFailureCount += bIsRead && OutputLayers[Layer].Num() == Resolution * Resolution ? 0 : 1;
		}
	};
	Options.OnDump = [&](const FTDFog3DReplayDump& Dump)
	{
		if (Dump.ComputeNumber == Fog3DBlockedDrawCheckCompute - 1)
		{
			BeforeValues = *Dump.Values;
		}
		if (Dump.ComputeNumber == Fog3DBlockedDrawCheckCompute)
		{
			AfterValues = *Dump.Values;
		}
	};

	const FTDFog3DReplayRunResult Result = RunTDFog3DReplay(Replay, Options);
	const bool bHasRun = HasFog3DReplayRun(*this, Replay, Result);
	OutputTarget->MarkAsGarbage();
	const int32 CellCount = GetFog3DDumpCellCount(Resolution);
	const int32 DumpValueCount = CellCount * TDFluidGpuFog3DDumpFieldCount;
	if (!bHasRun || BeforeValues.Num() != DumpValueCount || AfterValues.Num() != DumpValueCount || OutputReadFailureCount != 0)
	{
		AddError(FString::Printf(TEXT("Fog3D BlockedCellDrawFillAtWindowEdge could not read compute %d: before %d values, after %d values, output read failures %d"),
			Fog3DBlockedDrawCheckCompute, BeforeValues.Num(), AfterValues.Num(), OutputReadFailureCount));
		return false;
	}

	const double WrapSourceMinimumDisplacementCm = 1.0e-3;
	int32 BlockedCellCount = 0;
	int32 EdgeBlockedCellCount = 0;
	int32 NearEdgeBlockedCellCount = 0;
	int32 FarWrapPairCount = 0;
	int32 NearWrapPairCount = 0;
	int32 StateMismatchCount = 0;
	int32 OutputMismatchCount = 0;
	double StateMaximumError = 0.0;
	double OutputMaximumError = 0.0;
	FIntVector FirstMismatchCell(-1, -1, -1);
	const auto IsDisplacedFluidCell = [&](int32 CellX, int32 CellY, int32 CellLayer)
	{
		if (!IsFog3DDumpCellFluid(AfterValues, Resolution, CellX, CellY, CellLayer))
		{
			return false;
		}
		const int32 FluidIndex = GetFog3DDumpIndex(Resolution, CellX, CellY, CellLayer);
		double DisplacementSum = 0.0;
		for (int32 Channel = 0; Channel < 3; ++Channel)
		{
			DisplacementSum += FMath::Abs(static_cast<double>(BeforeValues[CellCount * (Fog3DDisplacementXFieldIndex + Channel) + FluidIndex]));
		}
		return DisplacementSum > WrapSourceMinimumDisplacementCm;
	};
	for (int32 LocalX = 0; LocalX < Resolution; ++LocalX)
	{
		for (int32 LocalY = 0; LocalY < Resolution; ++LocalY)
		{
			for (int32 Layer = 0; Layer < TDFluidGpuFog3DLayerCount; ++Layer)
			{
				if (IsFog3DDumpCellFluid(AfterValues, Resolution, LocalX, LocalY, Layer))
				{
					continue;
				}
				++BlockedCellCount;
				const bool bIsFarEdgeCell = LocalX == Resolution - 1 || LocalY == Resolution - 1;
				const bool bIsNearEdgeCell = LocalX == 0 || LocalY == 0;
				EdgeBlockedCellCount += bIsFarEdgeCell ? 1 : 0;
				NearEdgeBlockedCellCount += bIsNearEdgeCell ? 1 : 0;
				const bool bHasFarWrapPair = (LocalX == Resolution - 1 && IsDisplacedFluidCell(0, LocalY, Layer)) || (LocalY == Resolution - 1 && IsDisplacedFluidCell(LocalX, 0, Layer));
				const bool bHasNearWrapPair = (LocalX == 0 && IsDisplacedFluidCell(Resolution - 1, LocalY, Layer)) || (LocalY == 0 && IsDisplacedFluidCell(LocalX, Resolution - 1, Layer));
				FarWrapPairCount += bHasFarWrapPair ? 1 : 0;
				NearWrapPairCount += bHasNearWrapPair ? 1 : 0;
				const FVector4d Expected = ComputeFog3DExpectedDrawFill(BeforeValues, Resolution, LocalX, LocalY, Layer);
				const int32 CellIndex = GetFog3DDumpIndex(Resolution, LocalX, LocalY, Layer);
				const FFloat16Color& Texel = OutputLayers[Layer][LocalY * Resolution + LocalX];
				const double OutputValues[Fog3DDrawFillChannelCount] = { Texel.R.GetFloat(), Texel.G.GetFloat(), Texel.B.GetFloat(), Texel.A.GetFloat() };
				bool bStateMatches = true;
				bool bOutputMatches = true;
				for (int32 Channel = 0; Channel < Fog3DDrawFillChannelCount; ++Channel)
				{
					const double StateValue = AfterValues[CellCount * (Fog3DDisplacementXFieldIndex + Channel) + CellIndex];
					StateMaximumError = FMath::Max(StateMaximumError, FMath::Abs(StateValue - Expected[Channel]));
					OutputMaximumError = FMath::Max(OutputMaximumError, FMath::Abs(OutputValues[Channel] - Expected[Channel]));
					bStateMatches &= IsFog3DDrawValueClose(StateValue, Expected[Channel], Fog3DStateDrawTolerance);
					bOutputMatches &= IsFog3DDrawValueClose(OutputValues[Channel], Expected[Channel], Fog3DOutputDrawTolerance);
				}
				StateMismatchCount += bStateMatches ? 0 : 1;
				OutputMismatchCount += bOutputMatches ? 0 : 1;
				FirstMismatchCell = (!(bStateMatches && bOutputMatches) && FirstMismatchCell.X < 0) ? FIntVector(LocalX, LocalY, Layer) : FirstMismatchCell;
			}
		}
	}

	AddInfo(FString::Printf(TEXT("Fog3D BlockedCellDrawFillAtWindowEdge compute %d: blocked cells %d, blocked cells on the far window edges %d (with a displaced fluid wrapped neighbor %d), blocked cells on the near window edges %d (with a displaced fluid wrapped neighbor %d), state mismatches %d (max error %.3e), output mismatches %d (max error %.3e), first mismatch %d,%d,%d"),
		Fog3DBlockedDrawCheckCompute, BlockedCellCount, EdgeBlockedCellCount, FarWrapPairCount, NearEdgeBlockedCellCount, NearWrapPairCount, StateMismatchCount, StateMaximumError, OutputMismatchCount, OutputMaximumError,
		FirstMismatchCell.X, FirstMismatchCell.Y, FirstMismatchCell.Z));
	TestTrue(TEXT("Checked compute has no window move, obstacle change, reset or override"), bCheckComputeIsPlain);
	TestTrue(TEXT("Scene has blocked cells on the far window edges"), EdgeBlockedCellCount > 0);
	TestTrue(TEXT("Scene has far edge blocked cells whose wrapped neighbor is a displaced fluid cell"), FarWrapPairCount > 0);
	TestTrue(TEXT("Scene has blocked cells on the near window edges"), NearEdgeBlockedCellCount > 0);
	TestTrue(TEXT("Scene has near edge blocked cells whose wrapped neighbor is a displaced fluid cell"), NearWrapPairCount > 0);
	TestEqual(TEXT("State draw values of blocked cells at the window edge equal the fluid face neighbor average"), StateMismatchCount, 0);
	TestEqual(TEXT("Output draw values of blocked cells at the window edge equal the fluid face neighbor average"), OutputMismatchCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DAddendumReplayDumpsTest,
	"TDGame.FluidFXRhi.Fog3D.AddendumReplayDumps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DAddendumReplayDumpsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D AddendumReplayDumps needs a rendering RHI."));
		return false;
	}

	const FString ReplayDirectory = GetTDFog3DAddendumReplayDirectory();
	const FString DumpDirectory = GetTDFog3DAddendumDumpDirectory();
	TArray<FString> FileNames;
	IFileManager::Get().FindFiles(FileNames, *FPaths::Combine(ReplayDirectory, TEXT("*.json")), true, false);
	FileNames.Sort();
	TestEqual(TEXT("Addendum replay file count"), FileNames.Num(), Fog3DExpectedAddendumReplayFileCount);
	IFileManager::Get().DeleteDirectory(*DumpDirectory, false, true);

	int32 FailedReplayCount = 0;
	int32 TotalObstacleChangeComputeCount = 0;
	int32 TotalDumpCount = 0;
	for (const FString& FileName : FileNames)
	{
		FTDFog3DReplay Replay;
		FString Error;
		if (!LoadTDFog3DReplay(FPaths::Combine(ReplayDirectory, FileName), Replay, Error))
		{
			AddError(Error);
			++FailedReplayCount;
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s: replay name matches the file name"), *FileName), Replay.Name, FPaths::GetBaseFilename(FileName));

		int32 ObstacleChangeComputeCount = 0;
		int32 ObstacleChangeStepCount = 0;
		for (int32 StepIndex = 0; StepIndex < Replay.Steps.Num(); ++StepIndex)
		{
			ObstacleChangeStepCount += Replay.Steps[StepIndex].bChangesObstacles ? 1 : 0;
			for (int32 RepeatIndex = 0; RepeatIndex < Replay.Steps[StepIndex].RepeatCount; ++RepeatIndex)
			{
				ObstacleChangeComputeCount += MakeTDFog3DReplayFrameInput(Replay, StepIndex, RepeatIndex).bObstaclesChanged ? 1 : 0;
			}
		}
		TestEqual(*FString::Printf(TEXT("%s: each obstacle change marks exactly one compute"), *Replay.Name), ObstacleChangeComputeCount, ObstacleChangeStepCount);
		TotalObstacleChangeComputeCount += ObstacleChangeComputeCount;

		int32 NonFiniteValueCount = 0;
		FTDFog3DReplayRunOptions Options;
		Options.bWritesDumpFiles = true;
		Options.DumpDirectory = DumpDirectory;
		Options.OnDump = [&](const FTDFog3DReplayDump& Dump)
		{
			for (const float Value : *Dump.Values)
			{
				NonFiniteValueCount += FMath::IsFinite(Value) ? 0 : 1;
			}
		};
		const FTDFog3DReplayRunResult Result = RunTDFog3DReplay(Replay, Options);
		const bool bHasRun = HasFog3DReplayRun(*this, Replay, Result);
		TestEqual(*FString::Printf(TEXT("%s: every listed dump written"), *Replay.Name), Result.WrittenFileCount, Replay.DumpCount);
		TestEqual(*FString::Printf(TEXT("%s: dumps are finite"), *Replay.Name), NonFiniteValueCount, 0);
		FailedReplayCount += bHasRun && Result.WrittenFileCount == Replay.DumpCount && Result.FailedFileCount == 0 && NonFiniteValueCount == 0 ? 0 : 1;
		TotalDumpCount += Result.WrittenFileCount;
		AddInfo(FString::Printf(TEXT("Fog3D AddendumReplayDumps %s: %d computes, %d obstacle change computes, %d dumps, %.1f s"), *Replay.Name, Result.ComputeCount, ObstacleChangeComputeCount, Result.WrittenFileCount, Result.Seconds));
	}

	AddInfo(FString::Printf(TEXT("Fog3D AddendumReplayDumps: %d replays, failed %d, %d dumps written to %s, obstacle change computes %d"),
		FileNames.Num(), FailedReplayCount, TotalDumpCount, *DumpDirectory, TotalObstacleChangeComputeCount));
	TestTrue(TEXT("Addendum scenes change the obstacle list at least once"), TotalObstacleChangeComputeCount > 0);
	TestEqual(TEXT("Every addendum replay ran and dumped cleanly"), FailedReplayCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DStage1dReplayDumpsTest,
	"TDGame.FluidFXRhi.Fog3D.Stage1dReplayDumps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DStage1dReplayDumpsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D Stage1dReplayDumps needs a rendering RHI."));
		return false;
	}

	const FString ReplayDirectory = GetTDFog3DStage1dReplayDirectory();
	const FString DumpDirectory = GetTDFog3DStage1dDumpDirectory();
	TArray<FString> FileNames;
	IFileManager::Get().FindFiles(FileNames, *FPaths::Combine(ReplayDirectory, TEXT("*.json")), true, false);
	FileNames.Sort();
	TestEqual(TEXT("Stage 1d replay file count"), FileNames.Num(), Fog3DExpectedStage1dReplayFileCount);
	IFileManager::Get().DeleteDirectory(*DumpDirectory, false, true);

	int32 FailedReplayCount = 0;
	int32 TotalComputeCount = 0;
	int32 TotalDumpCount = 0;
	for (const FString& FileName : FileNames)
	{
		FTDFog3DReplay Replay;
		FString Error;
		if (!LoadTDFog3DReplay(FPaths::Combine(ReplayDirectory, FileName), Replay, Error))
		{
			AddError(Error);
			++FailedReplayCount;
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s: replay name matches the file name"), *FileName), Replay.Name, FPaths::GetBaseFilename(FileName));

		int32 NonFiniteValueCount = 0;
		FTDFog3DReplayRunOptions Options;
		Options.bWritesDumpFiles = true;
		Options.DumpDirectory = DumpDirectory;
		Options.OnDump = [&](const FTDFog3DReplayDump& Dump)
		{
			for (const float Value : *Dump.Values)
			{
				NonFiniteValueCount += FMath::IsFinite(Value) ? 0 : 1;
			}
		};
		const FTDFog3DReplayRunResult Result = RunTDFog3DReplay(Replay, Options);
		const bool bHasRun = HasFog3DReplayRun(*this, Replay, Result);
		TestEqual(*FString::Printf(TEXT("%s: every listed dump written"), *Replay.Name), Result.WrittenFileCount, Replay.DumpCount);
		TestEqual(*FString::Printf(TEXT("%s: dump files without write errors"), *Replay.Name), Result.FailedFileCount, 0);
		TestEqual(*FString::Printf(TEXT("%s: dumps are finite"), *Replay.Name), NonFiniteValueCount, 0);
		FailedReplayCount += bHasRun && Result.WrittenFileCount == Replay.DumpCount && Result.FailedFileCount == 0 && NonFiniteValueCount == 0 ? 0 : 1;
		TotalComputeCount += Result.ComputeCount;
		TotalDumpCount += Result.WrittenFileCount;
		AddInfo(FString::Printf(TEXT("Fog3D Stage1dReplayDumps %s: %d computes, %d dumps, %.1f s"), *Replay.Name, Result.ComputeCount, Result.WrittenFileCount, Result.Seconds));
	}

	AddInfo(FString::Printf(TEXT("Fog3D Stage1dReplayDumps: %d replays, failed %d, %d computes, %d dumps written to %s"),
		FileNames.Num(), FailedReplayCount, TotalComputeCount, TotalDumpCount, *DumpDirectory));
	TestEqual(TEXT("Every stage 1d replay ran and dumped cleanly"), FailedReplayCount, 0);
	return true;
}

#endif
