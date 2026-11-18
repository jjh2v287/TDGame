#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicRHI.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PooledRenderTarget.h"
#include "RHI.h"
#include "RHIGPUReadback.h"
#include "RHIGlobals.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "TDFluidGpuFog3D.h"
#include "Tests/TDFluidGpuFog3DDumpFiles.h"
#include "Tests/TDFluidGpuFog3DReplay.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include <cmath>
#include <limits>

namespace TDFluidGpuFog3DGroundRhiTestPrivate
{
	constexpr int32 GroundResolution = 16;
	constexpr int32 GroundBorderCells = 3;
	constexpr int32 GroundSize = GroundResolution + 2 * GroundBorderCells;
	constexpr int32 GroundColumnTerrainSize = GroundResolution + 2;
	constexpr int32 GroundMaximumReadbackExtraSubmitCount = 16;
	constexpr int32 GroundFluidFieldIndex = 9;
	constexpr int32 GroundOffDumpIntervalComputes = 15;
	constexpr double GroundCellSizeCm = 15.625;
	constexpr double GroundLayerHeightCm = 30.0;
	constexpr double GroundWallStepCm = 30.0;
	constexpr double GroundSlopeTolerance = 1.0e-6;
	constexpr double GroundBoxMarginCm = 0.01;
	const FIntPoint GroundWindowMinCell(32, -48);

	using FTDFog3DGroundHeightFunction = TFunction<double(int32 LocalX, int32 LocalY)>;

	struct FTDFog3DGroundCase
	{
		FString Name;
		FTDFluidGpuFog3DFrameInput FrameInput;
		FTDFluidGpuFog3DGroundUpload Upload;
		bool bUsesUpload = false;
		bool bExpectedAccepted = false;
	};

	struct FTDFog3DGroundRunResult
	{
		TArray<bool> StepAccepted;
		TArray<FVector4f> ColumnTerrain;
		TArray<float> DumpValues;
		bool bColumnTerrainCopied = false;
		bool bDumpCopied = false;
		bool bHasGroundTexturesAfterSteps = false;
	};

	bool HasRenderingRhi()
	{
		return !GUsingNullRHI && FApp::CanEverRender();
	}

	FTDFluidGpuFog3DFrameInput MakeGroundFrameInput()
	{
		FTDFluidGpuFog3DFrameInput FrameInput;
		FrameInput.Resolution = GroundResolution;
		FrameInput.CellSizeCm = static_cast<float>(GroundCellSizeCm);
		FrameInput.LayerHeightCm = static_cast<float>(GroundLayerHeightCm);
		FrameInput.WindowMinCell = GroundWindowMinCell;
		FrameInput.PreviousWindowMinCell = GroundWindowMinCell;
		FrameInput.bResetsState = true;
		FrameInput.bFollowsGround = true;
		FrameInput.GroundWallStepCm = static_cast<float>(GroundWallStepCm);
		FrameInput.bGroundChanged = true;
		return FrameInput;
	}

	FTDFluidGpuFog3DGroundUpload MakeGroundUpload(const FTDFog3DGroundHeightFunction& HeightAt)
	{
		FTDFluidGpuFog3DGroundUpload Upload;
		Upload.MinCell = GroundWindowMinCell - FIntPoint(GroundBorderCells, GroundBorderCells);
		Upload.Size = FIntPoint(GroundSize, GroundSize);
		Upload.HeightsCm.SetNumUninitialized(GroundSize * GroundSize);
		for (int32 TexelY = 0; TexelY < GroundSize; ++TexelY)
		{
			for (int32 TexelX = 0; TexelX < GroundSize; ++TexelX)
			{
				Upload.HeightsCm[TexelY * GroundSize + TexelX] = static_cast<float>(HeightAt(TexelX - GroundBorderCells, TexelY - GroundBorderCells));
			}
		}
		return Upload;
	}

	double ReadUploadedHeight(const FTDFluidGpuFog3DGroundUpload& Upload, int32 LocalX, int32 LocalY)
	{
		const int32 TexelX = LocalX + GroundWindowMinCell.X - Upload.MinCell.X;
		const int32 TexelY = LocalY + GroundWindowMinCell.Y - Upload.MinCell.Y;
		return static_cast<double>(Upload.HeightsCm[TexelY * Upload.Size.X + TexelX]);
	}

	double ComputeExpectedAxisSlope(double LowerHeight, double CenterHeight, double UpperHeight)
	{
		const double RiseUp = UpperHeight - CenterHeight;
		const double RiseDown = CenterHeight - LowerHeight;
		const bool bIsUpGentle = FMath::Abs(RiseUp) <= GroundWallStepCm;
		const bool bIsDownGentle = FMath::Abs(RiseDown) <= GroundWallStepCm;
		if (bIsUpGentle && bIsDownGentle)
		{
			return (UpperHeight - LowerHeight) / (2.0 * GroundCellSizeCm);
		}
		if (bIsUpGentle)
		{
			return RiseUp / GroundCellSizeCm;
		}
		if (bIsDownGentle)
		{
			return RiseDown / GroundCellSizeCm;
		}
		return 0.0;
	}

	bool IsExpectedWall(const FTDFluidGpuFog3DGroundUpload& Upload, int32 LocalX, int32 LocalY)
	{
		const double Height = ReadUploadedHeight(Upload, LocalX, LocalY);
		return Height - ReadUploadedHeight(Upload, LocalX + 1, LocalY) > GroundWallStepCm
			|| Height - ReadUploadedHeight(Upload, LocalX - 1, LocalY) > GroundWallStepCm
			|| Height - ReadUploadedHeight(Upload, LocalX, LocalY + 1) > GroundWallStepCm
			|| Height - ReadUploadedHeight(Upload, LocalX, LocalY - 1) > GroundWallStepCm;
	}

	FVector4d ComputeExpectedColumnTerrain(const FTDFluidGpuFog3DGroundUpload& Upload, int32 LocalX, int32 LocalY)
	{
		const double Height = ReadUploadedHeight(Upload, LocalX, LocalY);
		const bool bIsRing = LocalX < 0 || LocalY < 0 || LocalX >= GroundResolution || LocalY >= GroundResolution;
		if (bIsRing)
		{
			return FVector4d(Height, 0.0, 0.0, 0.0);
		}
		const double SlopeX = ComputeExpectedAxisSlope(ReadUploadedHeight(Upload, LocalX - 1, LocalY), Height, ReadUploadedHeight(Upload, LocalX + 1, LocalY));
		const double SlopeY = ComputeExpectedAxisSlope(ReadUploadedHeight(Upload, LocalX, LocalY - 1), Height, ReadUploadedHeight(Upload, LocalX, LocalY + 1));
		return FVector4d(Height, SlopeX, SlopeY, IsExpectedWall(Upload, LocalX, LocalY) ? 1.0 : 0.0);
	}

	void SubmitUntilReady(FRHICommandListImmediate& RHICmdList, const TFunctionRef<bool()>& IsReady)
	{
		int32 ExtraSubmitCount = 0;
		while (!IsReady() && ExtraSubmitCount < GroundMaximumReadbackExtraSubmitCount)
		{
			RHICmdList.SubmitAndBlockUntilGPUIdle();
			++ExtraSubmitCount;
		}
	}

	bool ReadColumnTerrain(FRHICommandListImmediate& RHICmdList, FTDFluidGpuFog3DState& State, TArray<FVector4f>& OutTexels)
	{
		if (!State.ColumnTerrainTarget.IsValid())
		{
			return false;
		}

		FRHIGPUTextureReadback Readback(TEXT("TDFluidGpu.Fog3DGroundColumnTerrainReadback"));
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuFog3DGroundColumnTerrainReadback"));
			AddEnqueueCopyPass(GraphBuilder, &Readback, GraphBuilder.RegisterExternalTexture(State.ColumnTerrainTarget));
			GraphBuilder.Execute();
		}
		RHICmdList.SubmitAndBlockUntilGPUIdle();
		SubmitUntilReady(RHICmdList, [&Readback]() { return Readback.IsReady(); });
		if (!Readback.IsReady())
		{
			return false;
		}

		const int32 Size = State.Resolution + 2;
		int32 RowPitchInPixels = 0;
		int32 BufferHeight = 0;
		const FVector4f* Texels = static_cast<const FVector4f*>(Readback.Lock(RowPitchInPixels, &BufferHeight));
		if (!Texels || RowPitchInPixels < Size || BufferHeight < Size)
		{
			Readback.Unlock();
			return false;
		}
		OutTexels.SetNumUninitialized(Size * Size);
		for (int32 RowIndex = 0; RowIndex < Size; ++RowIndex)
		{
			FMemory::Memcpy(OutTexels.GetData() + RowIndex * Size, Texels + static_cast<int64>(RowIndex) * RowPitchInPixels, Size * sizeof(FVector4f));
		}
		Readback.Unlock();
		return true;
	}

	bool ReadDump(FRHICommandListImmediate& RHICmdList, FTDFluidGpuFog3DState& State, TArray<float>& OutValues)
	{
		FRHIGPUBufferReadback Readback(TEXT("TDFluidGpu.Fog3DGroundDump"));
		bool bIsAccepted = false;
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuFog3DGroundDump"));
			bIsAccepted = AddTDFluidGpuFog3DDumpPass(GraphBuilder, State, Readback);
			GraphBuilder.Execute();
		}
		RHICmdList.SubmitAndBlockUntilGPUIdle();
		if (!bIsAccepted)
		{
			return false;
		}
		SubmitUntilReady(RHICmdList, [&Readback]() { return Readback.IsReady(); });
		return CopyTDFluidGpuFog3DDump(Readback, State.Resolution, OutValues);
	}

	TSharedRef<FTDFog3DGroundRunResult, ESPMode::ThreadSafe> RunGroundSteps(const TArray<FTDFluidGpuFog3DFrameInput>& Steps, bool bReadsDump)
	{
		TSharedRef<FTDFog3DGroundRunResult, ESPMode::ThreadSafe> Result = MakeShared<FTDFog3DGroundRunResult, ESPMode::ThreadSafe>();
		ENQUEUE_RENDER_COMMAND(TDFluidGpuFog3DGroundSteps)(
			[Result, &Steps, bReadsDump](FRHICommandListImmediate& RHICmdList)
			{
				FTDFluidGpuFog3DState State;
				for (const FTDFluidGpuFog3DFrameInput& Step : Steps)
				{
					FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuFog3DGroundStep"));
					Result->StepAccepted.Add(AddTDFluidGpuFog3DPasses(GraphBuilder, State, Step));
					GraphBuilder.Execute();
				}
				Result->bHasGroundTexturesAfterSteps = State.ColumnTerrainTarget.IsValid() || State.GroundHeightTarget.IsValid();
				Result->bColumnTerrainCopied = ReadColumnTerrain(RHICmdList, State, Result->ColumnTerrain);
				Result->bDumpCopied = bReadsDump && ReadDump(RHICmdList, State, Result->DumpValues);
				State.Release();
			});
		FlushRenderingCommands();
		return Result;
	}

	TArray<bool> RunGroundCases(const TArray<FTDFog3DGroundCase>& Cases)
	{
		TSharedRef<TArray<bool>, ESPMode::ThreadSafe> Accepted = MakeShared<TArray<bool>, ESPMode::ThreadSafe>();
		ENQUEUE_RENDER_COMMAND(TDFluidGpuFog3DGroundCases)(
			[Accepted, &Cases](FRHICommandListImmediate& RHICmdList)
			{
				for (const FTDFog3DGroundCase& Case : Cases)
				{
					FTDFluidGpuFog3DState State;
					FTDFluidGpuFog3DFrameInput FrameInput = Case.FrameInput;
					FrameInput.GroundUpload = Case.bUsesUpload ? &Case.Upload : nullptr;
					FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuFog3DGroundCase"));
					Accepted->Add(AddTDFluidGpuFog3DPasses(GraphBuilder, State, FrameInput));
					GraphBuilder.Execute();
					State.Release();
				}
			});
		FlushRenderingCommands();
		return *Accepted;
	}

	double MakeBakeColumnsHeight(int32 LocalX, int32 LocalY)
	{
		const double BaseCm = -137.5;
		if (LocalY < 4)
		{
			const int32 StairIndex = (LocalX + 3) / 3;
			const double CliffCm = LocalX >= 9 ? 40.0 : 0.0;
			return BaseCm + 20.0 * StairIndex + CliffCm;
		}
		if (LocalY < 8)
		{
			return BaseCm + 10.0 * LocalY + 3.5 * LocalX;
		}
		if (LocalY < 12)
		{
			return BaseCm + (LocalX >= 6 ? 30.0 : 0.0);
		}
		return BaseCm + ((LocalX <= 0 || LocalX >= GroundResolution - 1) ? 450.0 : 0.0);
	}

	double MakeBlockedSlopeHeight(int32 LocalX, int32 LocalY)
	{
		return 12.5 * LocalX + (LocalY >= 12 ? 100.0 : 0.0);
	}

	int32 GetColumnTerrainIndex(int32 LocalX, int32 LocalY)
	{
		return (LocalY + 1) * GroundColumnTerrainSize + LocalX + 1;
	}

	struct FTDFog3DGroundBakeCompare
	{
		int32 HeightMismatchCount = 0;
		int32 WallMismatchCount = 0;
		int32 RingNonZeroCount = 0;
		int32 WallCount = 0;
		int32 OneSidedSlopeCount = 0;
		double MaximumSlopeError = 0.0;
	};

	FTDFog3DGroundBakeCompare CompareColumnTerrain(const FTDFluidGpuFog3DGroundUpload& Upload, const TArray<FVector4f>& ColumnTerrain)
	{
		FTDFog3DGroundBakeCompare Compare;
		for (int32 LocalY = -1; LocalY <= GroundResolution; ++LocalY)
		{
			for (int32 LocalX = -1; LocalX <= GroundResolution; ++LocalX)
			{
				const FVector4f& Actual = ColumnTerrain[GetColumnTerrainIndex(LocalX, LocalY)];
				const FVector4d Expected = ComputeExpectedColumnTerrain(Upload, LocalX, LocalY);
				const bool bIsRing = LocalX < 0 || LocalY < 0 || LocalX >= GroundResolution || LocalY >= GroundResolution;
				Compare.HeightMismatchCount += static_cast<double>(Actual.X) == Expected.X ? 0 : 1;
				if (bIsRing)
				{
					Compare.RingNonZeroCount += (Actual.Y == 0.0f && Actual.Z == 0.0f && Actual.W == 0.0f) ? 0 : 1;
					continue;
				}
				Compare.WallMismatchCount += static_cast<double>(Actual.W) == Expected.W ? 0 : 1;
				Compare.WallCount += Expected.W > 0.5 ? 1 : 0;
				Compare.MaximumSlopeError = FMath::Max(Compare.MaximumSlopeError, FMath::Abs(static_cast<double>(Actual.Y) - Expected.Y));
				Compare.MaximumSlopeError = FMath::Max(Compare.MaximumSlopeError, FMath::Abs(static_cast<double>(Actual.Z) - Expected.Z));
				const double CenterHeight = ReadUploadedHeight(Upload, LocalX, LocalY);
				const bool bHasOneSidedX = FMath::Abs(ReadUploadedHeight(Upload, LocalX + 1, LocalY) - CenterHeight) > GroundWallStepCm
					|| FMath::Abs(CenterHeight - ReadUploadedHeight(Upload, LocalX - 1, LocalY)) > GroundWallStepCm;
				Compare.OneSidedSlopeCount += bHasOneSidedX ? 1 : 0;
			}
		}
		return Compare;
	}

	bool LoadGroundReplay(FAutomationTestBase& Test, const FString& Directory, const TCHAR* ReplayName, FTDFog3DReplay& OutReplay)
	{
		FString Error;
		if (LoadTDFog3DReplay(FPaths::Combine(Directory, FString(ReplayName) + TEXT(".json")), OutReplay, Error))
		{
			return true;
		}
		Test.AddError(Error);
		return false;
	}

	FTDFluidGpuFog3DGroundUpload MakeFlatUploadCoveringReplay(const FTDFog3DReplay& Replay)
	{
		FIntPoint MinCell(MAX_int32, MAX_int32);
		FIntPoint MaxCell(MIN_int32, MIN_int32);
		for (const FTDFog3DReplayStep& Step : Replay.Steps)
		{
			MinCell = FIntPoint(FMath::Min(MinCell.X, Step.WindowMinCell.X - 1), FMath::Min(MinCell.Y, Step.WindowMinCell.Y - 1));
			MaxCell = FIntPoint(FMath::Max(MaxCell.X, Step.WindowMinCell.X + Replay.Resolution), FMath::Max(MaxCell.Y, Step.WindowMinCell.Y + Replay.Resolution));
		}
		const int32 Size = FMath::Max(MaxCell.X - MinCell.X, MaxCell.Y - MinCell.Y) + 1;
		FTDFluidGpuFog3DGroundUpload Upload;
		Upload.MinCell = MinCell;
		Upload.Size = FIntPoint(Size, Size);
		Upload.HeightsCm.SetNumZeroed(Size * Size);
		return Upload;
	}

	TArray<TArray<float>> RunReplayDumps(const FTDFog3DReplay& Replay, const FTDFluidGpuFog3DGroundUpload* GroundUpload, FTDFog3DReplayRunResult& OutResult)
	{
		TArray<TArray<float>> Dumps;
		FTDFog3DReplayRunOptions Options;
		Options.ExtraDumpIntervalComputes = GroundOffDumpIntervalComputes;
		Options.OnDump = [&Dumps](const FTDFog3DReplayDump& Dump)
		{
			Dumps.Add(*Dump.Values);
		};
		if (GroundUpload)
		{
			Options.AdjustFrameInput = [GroundUpload](int32 ComputeNumber, FTDFluidGpuFog3DFrameInput& FrameInput)
			{
				FrameInput.bFollowsGround = true;
				FrameInput.bGroundChanged = ComputeNumber == 1;
				FrameInput.GroundUpload = GroundUpload;
			};
		}
		OutResult = RunTDFog3DReplay(Replay, Options);
		return Dumps;
	}

	int32 CountDifferentDumpValues(const TArray<TArray<float>>& First, const TArray<TArray<float>>& Second)
	{
		if (First.Num() != Second.Num())
		{
			return MAX_int32;
		}
		int32 DifferentCount = 0;
		for (int32 DumpIndex = 0; DumpIndex < First.Num(); ++DumpIndex)
		{
			if (First[DumpIndex].Num() != Second[DumpIndex].Num())
			{
				return MAX_int32;
			}
			for (int32 ValueIndex = 0; ValueIndex < First[DumpIndex].Num(); ++ValueIndex)
			{
				DifferentCount += FMemory::Memcmp(&First[DumpIndex][ValueIndex], &Second[DumpIndex][ValueIndex], sizeof(float)) == 0 ? 0 : 1;
			}
		}
		return DifferentCount;
	}

	constexpr double GroundFramePositionToleranceCm = 1.0e-3;
	constexpr double GroundFrameSlopeTolerance = 1.0e-5;
	constexpr double GroundTan30 = 0.57735026918962576;
	constexpr int32 GroundSlopeStepCount = 30;
	constexpr double GroundSlopeStepCm = 4.5;
	constexpr double GroundSlopeStartXCm = 60.0;
	constexpr double GroundSlopeSphereYCm = 125.0;
	constexpr double GroundSlopeClearanceCm = 40.0;
	constexpr double GroundSlopeRadiusCm = 40.0;
	constexpr double GroundSlopeWindStrength = 0.8;
	constexpr double GroundSlopeErosionPerSec = 4.0;
	constexpr double GroundSlopeWindShareLimit = 0.01;
	constexpr int32 GroundDumpWindFieldCount = 3;

	struct FTDFog3DExpectedSphereFrame
	{
		FVector3d TiltedStartCm = FVector3d::ZeroVector;
		FVector3d TiltedEndCm = FVector3d::ZeroVector;
		FVector2d MidpointCm = FVector2d::ZeroVector;
		FVector2d FrameSlope = FVector2d::ZeroVector;
		FVector2d ReferenceColumnCm = FVector2d::ZeroVector;
		double ReferenceHeightCm = 0.0;
	};

	struct FTDFog3DGroundFrameResult
	{
		bool bAccepted = false;
		bool bCopied = false;
		TArray<FTDFluidGpuFog3DSphereFrame> Frames;
	};

	struct FTDFog3DFrameCompare
	{
		double MaximumPositionErrorCm = 0.0;
		double MaximumSlopeError = 0.0;
		int32 NonZeroPaddingCount = 0;
		int32 ComparedFrameCount = 0;
	};

	struct FTDFog3DGroundFrameCase
	{
		const TCHAR* Name = nullptr;
		double (*HeightAt)(int32 LocalX, int32 LocalY) = nullptr;
		TArray<FTDFluidGpuFog3DSphere> Spheres;
	};

	FTDFluidGpuFog3DSphere MakeGroundSphere(const FVector3d& StartCm, const FVector3d& EndCm, double RadiusCm, const FVector3d& VelocityCmPerSec, double WindStrength, double ErosionPerSec)
	{
		FTDFluidGpuFog3DSphere Sphere;
		Sphere.StartCmRadiusCm = FVector4f(FVector3f(StartCm), static_cast<float>(RadiusCm));
		Sphere.EndCmWindStrength = FVector4f(FVector3f(EndCm), static_cast<float>(WindStrength));
		Sphere.VelocityCmPerSecErosionPerSec = FVector4f(FVector3f(VelocityCmPerSec), static_cast<float>(ErosionPerSec));
		return Sphere;
	}

	FTDFluidGpuFog3DSphere MakeFrameTestSphere(double StartXCells, double StartYCells, double StartZCm, double EndXCells, double EndYCells, double EndZCm)
	{
		const FVector3d StartCm(StartXCells * GroundCellSizeCm, StartYCells * GroundCellSizeCm, StartZCm);
		const FVector3d EndCm(EndXCells * GroundCellSizeCm, EndYCells * GroundCellSizeCm, EndZCm);
		return MakeGroundSphere(StartCm, EndCm, 30.0, FVector3d(200.0, 0.0, 0.0), 1.0, 0.0);
	}

	double ClampToColumnCentersCm(double PositionCm)
	{
		return FMath::Clamp(PositionCm, 0.5 * GroundCellSizeCm, (GroundResolution - 0.5) * GroundCellSizeCm);
	}

	FVector2d ReadExpectedColumnSlope(const FTDFluidGpuFog3DGroundUpload& Upload, int32 LocalX, int32 LocalY)
	{
		const FVector4d ColumnTerrain = ComputeExpectedColumnTerrain(Upload, LocalX, LocalY);
		return FVector2d(ColumnTerrain.Y, ColumnTerrain.Z);
	}

	FVector2d SampleExpectedFrameSlope(const FTDFluidGpuFog3DGroundUpload& Upload, const FVector2d& MidpointCm)
	{
		const double FractionX = (ClampToColumnCentersCm(MidpointCm.X) - 0.5 * GroundCellSizeCm) / GroundCellSizeCm;
		const double FractionY = (ClampToColumnCentersCm(MidpointCm.Y) - 0.5 * GroundCellSizeCm) / GroundCellSizeCm;
		const int32 LowX = FMath::Clamp(FMath::FloorToInt32(FractionX), 0, GroundResolution - 2);
		const int32 LowY = FMath::Clamp(FMath::FloorToInt32(FractionY), 0, GroundResolution - 2);
		const double BlendX = FractionX - LowX;
		const double BlendY = FractionY - LowY;
		const FVector2d Slope00 = ReadExpectedColumnSlope(Upload, LowX, LowY);
		const FVector2d Slope10 = ReadExpectedColumnSlope(Upload, LowX + 1, LowY);
		const FVector2d Slope01 = ReadExpectedColumnSlope(Upload, LowX, LowY + 1);
		const FVector2d Slope11 = ReadExpectedColumnSlope(Upload, LowX + 1, LowY + 1);
		return (Slope00 * (1.0 - BlendX) + Slope10 * BlendX) * (1.0 - BlendY) + (Slope01 * (1.0 - BlendX) + Slope11 * BlendX) * BlendY;
	}

	int32 FindExpectedReferenceColumn(double MidpointCm)
	{
		return FMath::Clamp(FMath::FloorToInt32((MidpointCm - 0.5 * GroundCellSizeCm) / GroundCellSizeCm + 0.5), 0, GroundResolution - 1);
	}

	double TiltExpectedHeightCm(const FVector3d& PositionCm, const FVector2d& MidpointCm, const FVector2d& FrameSlope)
	{
		return PositionCm.Z - FrameSlope.X * (PositionCm.X - MidpointCm.X) - FrameSlope.Y * (PositionCm.Y - MidpointCm.Y);
	}

	FTDFog3DExpectedSphereFrame ComputeExpectedSphereFrame(const FTDFluidGpuFog3DGroundUpload& Upload, const FTDFluidGpuFog3DSphere& Sphere)
	{
		const FVector3d StartCm(Sphere.StartCmRadiusCm.X, Sphere.StartCmRadiusCm.Y, Sphere.StartCmRadiusCm.Z);
		const FVector3d EndCm(Sphere.EndCmWindStrength.X, Sphere.EndCmWindStrength.Y, Sphere.EndCmWindStrength.Z);
		FTDFog3DExpectedSphereFrame Expected;
		Expected.MidpointCm = FVector2d(0.5 * (StartCm.X + EndCm.X), 0.5 * (StartCm.Y + EndCm.Y));
		Expected.FrameSlope = SampleExpectedFrameSlope(Upload, Expected.MidpointCm);
		Expected.TiltedStartCm = FVector3d(StartCm.X, StartCm.Y, TiltExpectedHeightCm(StartCm, Expected.MidpointCm, Expected.FrameSlope));
		Expected.TiltedEndCm = FVector3d(EndCm.X, EndCm.Y, TiltExpectedHeightCm(EndCm, Expected.MidpointCm, Expected.FrameSlope));
		const int32 ReferenceX = FindExpectedReferenceColumn(Expected.MidpointCm.X);
		const int32 ReferenceY = FindExpectedReferenceColumn(Expected.MidpointCm.Y);
		Expected.ReferenceColumnCm = FVector2d((ReferenceX + 0.5) * GroundCellSizeCm, (ReferenceY + 0.5) * GroundCellSizeCm);
		Expected.ReferenceHeightCm = ReadUploadedHeight(Upload, ReferenceX, ReferenceY);
		return Expected;
	}

	void AccumulatePositionError(FTDFog3DFrameCompare& Compare, float Actual, double Expected)
	{
		Compare.MaximumPositionErrorCm = FMath::Max(Compare.MaximumPositionErrorCm, FMath::Abs(static_cast<double>(Actual) - Expected));
	}

	void CompareSphereFrame(const FTDFluidGpuFog3DSphereFrame& Actual, const FTDFog3DExpectedSphereFrame& Expected, FTDFog3DFrameCompare& Compare)
	{
		AccumulatePositionError(Compare, Actual.TiltedStartCmReferenceHeightCm.X, Expected.TiltedStartCm.X);
		AccumulatePositionError(Compare, Actual.TiltedStartCmReferenceHeightCm.Y, Expected.TiltedStartCm.Y);
		AccumulatePositionError(Compare, Actual.TiltedStartCmReferenceHeightCm.Z, Expected.TiltedStartCm.Z);
		AccumulatePositionError(Compare, Actual.TiltedStartCmReferenceHeightCm.W, Expected.ReferenceHeightCm);
		AccumulatePositionError(Compare, Actual.TiltedEndCm.X, Expected.TiltedEndCm.X);
		AccumulatePositionError(Compare, Actual.TiltedEndCm.Y, Expected.TiltedEndCm.Y);
		AccumulatePositionError(Compare, Actual.TiltedEndCm.Z, Expected.TiltedEndCm.Z);
		AccumulatePositionError(Compare, Actual.MidpointCmFrameSlope.X, Expected.MidpointCm.X);
		AccumulatePositionError(Compare, Actual.MidpointCmFrameSlope.Y, Expected.MidpointCm.Y);
		AccumulatePositionError(Compare, Actual.ReferenceColumnCm.X, Expected.ReferenceColumnCm.X);
		AccumulatePositionError(Compare, Actual.ReferenceColumnCm.Y, Expected.ReferenceColumnCm.Y);
		Compare.MaximumSlopeError = FMath::Max(Compare.MaximumSlopeError, FMath::Abs(static_cast<double>(Actual.MidpointCmFrameSlope.Z) - Expected.FrameSlope.X));
		Compare.MaximumSlopeError = FMath::Max(Compare.MaximumSlopeError, FMath::Abs(static_cast<double>(Actual.MidpointCmFrameSlope.W) - Expected.FrameSlope.Y));
		Compare.NonZeroPaddingCount += (Actual.TiltedEndCm.W == 0.0f && Actual.ReferenceColumnCm.Z == 0.0f && Actual.ReferenceColumnCm.W == 0.0f) ? 0 : 1;
		++Compare.ComparedFrameCount;
	}

	bool CopySphereFrames(FRHICommandListImmediate& RHICmdList, FRHIGPUBufferReadback& Readback, int32 FrameCount, TArray<FTDFluidGpuFog3DSphereFrame>& OutFrames)
	{
		SubmitUntilReady(RHICmdList, [&Readback]() { return Readback.IsReady(); });
		if (!Readback.IsReady())
		{
			return false;
		}

		const uint32 ByteCount = static_cast<uint32>(FrameCount * sizeof(FTDFluidGpuFog3DSphereFrame));
		const void* Data = Readback.Lock(ByteCount);
		if (!Data)
		{
			return false;
		}
		OutFrames.SetNumUninitialized(FrameCount);
		FMemory::Memcpy(OutFrames.GetData(), Data, ByteCount);
		Readback.Unlock();
		return true;
	}

	TSharedRef<FTDFog3DGroundFrameResult, ESPMode::ThreadSafe> RunGroundFrameStep(const FTDFluidGpuFog3DFrameInput& FrameInput)
	{
		TSharedRef<FTDFog3DGroundFrameResult, ESPMode::ThreadSafe> Result = MakeShared<FTDFog3DGroundFrameResult, ESPMode::ThreadSafe>();
		ENQUEUE_RENDER_COMMAND(TDFluidGpuFog3DGroundFrames)(
			[Result, &FrameInput](FRHICommandListImmediate& RHICmdList)
			{
				FTDFluidGpuFog3DState State;
				FRHIGPUBufferReadback Readback(TEXT("TDFluidGpu.Fog3DGroundSphereFrames"));
				FTDFluidGpuFog3DFrameInput StepInput = FrameInput;
				StepInput.SphereFrameReadback = &Readback;
				{
					FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuFog3DGroundFrames"));
					Result->bAccepted = AddTDFluidGpuFog3DPasses(GraphBuilder, State, StepInput);
					GraphBuilder.Execute();
				}
				RHICmdList.SubmitAndBlockUntilGPUIdle();
				Result->bCopied = Result->bAccepted && CopySphereFrames(RHICmdList, Readback, StepInput.Spheres.Num(), Result->Frames);
				State.Release();
			});
		FlushRenderingCommands();
		return Result;
	}

	double MakeFrameUniformSlopeHeight(int32 LocalX, int32 LocalY)
	{
		return 20.0 + GroundTan30 * (LocalX + 0.5) * GroundCellSizeCm + 0.25 * (LocalY + 0.5) * GroundCellSizeCm;
	}

	double MakeFrameKinkHeight(int32 LocalX, int32)
	{
		return LocalX < 8 ? 0.3 * LocalX * GroundCellSizeCm : (2.4 - 0.5 * (LocalX - 8)) * GroundCellSizeCm;
	}

	double MakeFrameCurbHeight(int32 LocalX, int32 LocalY)
	{
		return (LocalX >= 8 ? 30.0 : 0.0) + 0.1 * LocalY * GroundCellSizeCm;
	}

	double MakeFrameCliffHeight(int32 LocalX, int32)
	{
		return LocalX >= 8 ? 200.0 : 0.0;
	}

	TArray<FTDFog3DGroundFrameCase> MakeGroundFrameCases()
	{
		TArray<FTDFog3DGroundFrameCase> Cases;
		FTDFog3DGroundFrameCase& Slope = Cases.AddDefaulted_GetRef();
		Slope.Name = TEXT("uniform slope");
		Slope.HeightAt = &MakeFrameUniformSlopeHeight;
		Slope.Spheres.Add(MakeFrameTestSphere(6.3, 7.1, 80.0, 7.9, 8.4, 95.0));
		Slope.Spheres.Add(MakeFrameTestSphere(0.3, 15.8, 60.0, 0.3, 15.8, 60.0));
		Slope.Spheres.Add(MakeFrameTestSphere(15.9, 0.2, 50.0, 15.9, 0.2, 50.0));
		Slope.Spheres.Add(MakeFrameTestSphere(7.5, 5.5, 70.0, 8.5, 5.5, 70.0));

		FTDFog3DGroundFrameCase& Kink = Cases.AddDefaulted_GetRef();
		Kink.Name = TEXT("kinked ground");
		Kink.HeightAt = &MakeFrameKinkHeight;
		Kink.Spheres.Add(MakeFrameTestSphere(7.6, 4.0, 60.0, 8.6, 4.5, 62.0));
		Kink.Spheres.Add(MakeFrameTestSphere(8.0, 10.0, 70.0, 8.0, 10.0, 70.0));

		FTDFog3DGroundFrameCase& Curb = Cases.AddDefaulted_GetRef();
		Curb.Name = TEXT("30 cm curb");
		Curb.HeightAt = &MakeFrameCurbHeight;
		Curb.Spheres.Add(MakeFrameTestSphere(8.2, 6.0, 75.0, 8.2, 7.0, 75.0));
		Curb.Spheres.Add(MakeFrameTestSphere(7.0, 3.0, 50.0, 8.0, 3.0, 55.0));

		FTDFog3DGroundFrameCase& Cliff = Cases.AddDefaulted_GetRef();
		Cliff.Name = TEXT("cliff");
		Cliff.HeightAt = &MakeFrameCliffHeight;
		Cliff.Spheres.Add(MakeFrameTestSphere(7.4, 9.0, 60.0, 7.4, 9.0, 60.0));
		Cliff.Spheres.Add(MakeFrameTestSphere(7.5, 3.0, 230.0, 8.5, 3.0, 230.0));
		return Cases;
	}

	double MakeUniformSlopeHeight(int32 LocalX, int32)
	{
		return GroundTan30 * (LocalX + 0.5) * GroundCellSizeCm;
	}

	TArray<FTDFluidGpuFog3DFrameInput> MakeSlopeRunSteps(const FTDFluidGpuFog3DGroundUpload* SlopeUpload)
	{
		const double Slope = SlopeUpload ? GroundTan30 : 0.0;
		const double SpeedCmPerSec = GroundSlopeStepCm * 60.0;
		TArray<FTDFluidGpuFog3DFrameInput> Steps;
		for (int32 StepIndex = 0; StepIndex < GroundSlopeStepCount; ++StepIndex)
		{
			FTDFluidGpuFog3DFrameInput& Step = Steps.Add_GetRef(MakeGroundFrameInput());
			Step.DeltaSeconds = 1.0f / 60.0f;
			Step.bResetsState = StepIndex == 0;
			Step.bFollowsGround = SlopeUpload != nullptr;
			Step.bGroundChanged = SlopeUpload != nullptr && StepIndex == 0;
			Step.GroundUpload = StepIndex == 0 ? SlopeUpload : nullptr;
			const double StartXCm = GroundSlopeStartXCm + GroundSlopeStepCm * StepIndex;
			const double EndXCm = StartXCm + GroundSlopeStepCm;
			const FVector3d StartCm(StartXCm, GroundSlopeSphereYCm, Slope * StartXCm + GroundSlopeClearanceCm);
			const FVector3d EndCm(EndXCm, GroundSlopeSphereYCm, Slope * EndXCm + GroundSlopeClearanceCm);
			Step.Spheres.Add(MakeGroundSphere(StartCm, EndCm, GroundSlopeRadiusCm, FVector3d(SpeedCmPerSec, 0.0, Slope * SpeedCmPerSec), GroundSlopeWindStrength, GroundSlopeErosionPerSec));
		}
		return Steps;
	}

	FString MakeSlopeReportJson(const TArray<double>& FieldDifferences, double FlatWindMaximum, double WindShare, bool bAllFinite)
	{
		static const TCHAR* FieldNames[TDFluidGpuFog3DDumpFieldCount] = {
			TEXT("wind_x"), TEXT("wind_y"), TEXT("wind_z"), TEXT("displacement_x"), TEXT("displacement_y"), TEXT("displacement_z"),
			TEXT("fill"), TEXT("pressure"), TEXT("divergence"), TEXT("fluid") };
		FString Json = FString::Printf(TEXT("{\n  \"scene\": \"S30 uniform slope vs flat, N=%d, %d steps, sphere radius %.1f cm moving %.1f cm per step at %.1f cm above ground\",\n  \"maximum_absolute_difference\": {\n"),
			GroundResolution, GroundSlopeStepCount, GroundSlopeRadiusCm, GroundSlopeStepCm, GroundSlopeClearanceCm);
		for (int32 FieldIndex = 0; FieldIndex < TDFluidGpuFog3DDumpFieldCount; ++FieldIndex)
		{
			Json += FString::Printf(TEXT("    \"%s\": %.9g%s\n"), FieldNames[FieldIndex], FieldDifferences[FieldIndex], FieldIndex + 1 < TDFluidGpuFog3DDumpFieldCount ? TEXT(",") : TEXT(""));
		}
		Json += FString::Printf(TEXT("  },\n  \"flat_wind_maximum\": %.9g,\n  \"wind_difference_share_of_flat_maximum\": %.9g,\n  \"all_finite\": %s\n}\n"), FlatWindMaximum, WindShare, bAllFinite ? TEXT("true") : TEXT("false"));
		return Json;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DGroundOffMatchesStage1Test,
	"TDGame.FluidFXRhi.Fog3D.GroundOffMatchesStage1",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DGroundOffMatchesStage1Test::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DGroundRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D GroundOffMatchesStage1 needs a rendering RHI."));
		return false;
	}

	struct FTDFog3DGroundOffScene
	{
		FString Directory;
		const TCHAR* Name = nullptr;
	};
	const FTDFog3DGroundOffScene Scenes[] = {
		{ GetTDFog3DReplayDirectory(), TEXT("k4_run") },
		{ GetTDFog3DAddendumReplayDirectory(), TEXT("add1_obstacle_swap") } };

	for (const FTDFog3DGroundOffScene& Scene : Scenes)
	{
		FTDFog3DReplay Replay;
		if (!LoadGroundReplay(*this, Scene.Directory, Scene.Name, Replay))
		{
			continue;
		}

		const FTDFluidGpuFog3DGroundUpload FlatUpload = MakeFlatUploadCoveringReplay(Replay);
		FTDFog3DReplayRunResult OffResult;
		FTDFog3DReplayRunResult OnResult;
		const TArray<TArray<float>> OffDumps = RunReplayDumps(Replay, nullptr, OffResult);
		const TArray<TArray<float>> OnDumps = RunReplayDumps(Replay, &FlatUpload, OnResult);
		const int32 DifferentCount = CountDifferentDumpValues(OffDumps, OnDumps);
		AddInfo(FString::Printf(TEXT("Fog3D GroundOffMatchesStage1 %s: %d computes, ground %dx%d at (%d, %d), %d dumps compared, different values %d"),
			*Replay.Name, OffResult.ComputeCount, FlatUpload.Size.X, FlatUpload.Size.Y, FlatUpload.MinCell.X, FlatUpload.MinCell.Y, OffDumps.Num(), DifferentCount));
		TestEqual(*FString::Printf(TEXT("%s: ground off accepts every compute"), *Replay.Name), OffResult.RejectedComputeCount, 0);
		TestEqual(*FString::Printf(TEXT("%s: flat ground accepts every compute"), *Replay.Name), OnResult.RejectedComputeCount, 0);
		TestEqual(*FString::Printf(TEXT("%s: every dump copied"), *Replay.Name), OffResult.FailedDumpCount + OnResult.FailedDumpCount, 0);
		TestTrue(*FString::Printf(TEXT("%s: dumps were taken"), *Replay.Name), OffDumps.Num() > 1);
		TestEqual(*FString::Printf(TEXT("%s: ground off and flat ground give bit identical dumps"), *Replay.Name), DifferentCount, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DGroundBakeColumnsTest,
	"TDGame.FluidFXRhi.Fog3D.GroundBakeColumns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DGroundBakeColumnsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DGroundRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D GroundBakeColumns needs a rendering RHI."));
		return false;
	}

	const FTDFluidGpuFog3DGroundUpload Upload = MakeGroundUpload(&MakeBakeColumnsHeight);
	TArray<FTDFluidGpuFog3DFrameInput> Steps;
	FTDFluidGpuFog3DFrameInput& BakeStep = Steps.Add_GetRef(MakeGroundFrameInput());
	BakeStep.GroundUpload = &Upload;
	FTDFluidGpuFog3DFrameInput& StateGroundStep = Steps.Add_GetRef(MakeGroundFrameInput());
	StateGroundStep.bResetsState = false;
	StateGroundStep.bGroundChanged = true;

	const TSharedRef<FTDFog3DGroundRunResult, ESPMode::ThreadSafe> Result = RunGroundSteps(Steps, false);
	TestEqual(TEXT("Both ground steps ran"), Result->StepAccepted.Num(), 2);
	for (int32 StepIndex = 0; StepIndex < Result->StepAccepted.Num(); ++StepIndex)
	{
		TestTrue(*FString::Printf(TEXT("Ground step %d accepted"), StepIndex + 1), Result->StepAccepted[StepIndex]);
	}
	if (!TestTrue(TEXT("Column terrain read back"), Result->bColumnTerrainCopied))
	{
		return false;
	}

	const FTDFog3DGroundBakeCompare Compare = CompareColumnTerrain(Upload, Result->ColumnTerrain);
	AddInfo(FString::Printf(TEXT("Fog3D GroundBakeColumns: %d texels, height mismatches %d, wall mismatches %d, walls %d, one sided slope columns %d, ring non-zero %d, maximum slope error %.3g"),
		Result->ColumnTerrain.Num(), Compare.HeightMismatchCount, Compare.WallMismatchCount, Compare.WallCount, Compare.OneSidedSlopeCount, Compare.RingNonZeroCount, Compare.MaximumSlopeError));
	TestEqual(TEXT("Column terrain holds (N+2) squared texels"), Result->ColumnTerrain.Num(), GroundColumnTerrainSize * GroundColumnTerrainSize);
	TestEqual(TEXT("Every column height including the ring equals the uploaded height"), Compare.HeightMismatchCount, 0);
	TestEqual(TEXT("Every wall flag matches the strict step rule"), Compare.WallMismatchCount, 0);
	TestTrue(TEXT("Scene has wall columns"), Compare.WallCount > 0);
	TestTrue(TEXT("Scene has one sided slope columns"), Compare.OneSidedSlopeCount > 0);
	TestTrue(TEXT("Slopes match the 64 bit rule within 1e-6"), Compare.MaximumSlopeError <= GroundSlopeTolerance);
	TestEqual(TEXT("Ring texels keep slope and wall at zero"), Compare.RingNonZeroCount, 0);
	const FVector4d CurbExpected = ComputeExpectedColumnTerrain(Upload, 6, 9);
	TestTrue(TEXT("A 30 cm curb is not a wall"), CurbExpected.W == 0.0 && Result->ColumnTerrain[GetColumnTerrainIndex(6, 9)].W == 0.0f);
	TestTrue(TEXT("Cliff column at the window edge is a wall"), Result->ColumnTerrain[GetColumnTerrainIndex(0, 14)].W == 1.0f && Result->ColumnTerrain[GetColumnTerrainIndex(GroundResolution - 1, 14)].W == 1.0f);

	TArray<FTDFluidGpuFog3DFrameInput> OffSteps;
	FTDFluidGpuFog3DFrameInput& UploadStep = OffSteps.Add_GetRef(MakeGroundFrameInput());
	UploadStep.GroundUpload = &Upload;
	FTDFluidGpuFog3DFrameInput& OffStep = OffSteps.Add_GetRef(MakeGroundFrameInput());
	OffStep.bResetsState = false;
	OffStep.bFollowsGround = false;
	const TSharedRef<FTDFog3DGroundRunResult, ESPMode::ThreadSafe> OffResult = RunGroundSteps(OffSteps, false);
	TestTrue(TEXT("Ground off steps accepted"), OffResult->StepAccepted.Num() == 2 && OffResult->StepAccepted[0] && OffResult->StepAccepted[1]);
	TestFalse(TEXT("Ground off releases the column terrain and ground height textures"), OffResult->bHasGroundTexturesAfterSteps);
	TestFalse(TEXT("Ground off leaves no column terrain to read back"), OffResult->bColumnTerrainCopied);

	TArray<FTDFluidGpuFog3DFrameInput> ToggleSteps = OffSteps;
	FTDFluidGpuFog3DFrameInput& OnAgainStep = ToggleSteps.Add_GetRef(MakeGroundFrameInput());
	OnAgainStep.bResetsState = false;
	OnAgainStep.GroundUpload = &Upload;
	const TSharedRef<FTDFog3DGroundRunResult, ESPMode::ThreadSafe> ToggleResult = RunGroundSteps(ToggleSteps, false);
	TestTrue(TEXT("Ground on, off, on again steps accepted"), ToggleResult->StepAccepted.Num() == 3 && ToggleResult->StepAccepted[0] && ToggleResult->StepAccepted[1] && ToggleResult->StepAccepted[2]);
	TestTrue(TEXT("Ground on again column terrain read back"), ToggleResult->bColumnTerrainCopied && ToggleResult->ColumnTerrain.Num() == GroundColumnTerrainSize * GroundColumnTerrainSize);
	if (ToggleResult->bColumnTerrainCopied)
	{
		const FTDFog3DGroundBakeCompare ToggleCompare = CompareColumnTerrain(Upload, ToggleResult->ColumnTerrain);
		TestTrue(TEXT("Ground on again rebakes the column terrain"), ToggleCompare.HeightMismatchCount == 0 && ToggleCompare.WallMismatchCount == 0 && ToggleCompare.MaximumSlopeError <= GroundSlopeTolerance);
	}

	TArray<FTDFog3DGroundCase> Cases;
	auto AddCase = [&Cases, &Upload](const TCHAR* Name, bool bExpectedAccepted) -> FTDFog3DGroundCase&
	{
		FTDFog3DGroundCase& Case = Cases.AddDefaulted_GetRef();
		Case.Name = Name;
		Case.FrameInput = MakeGroundFrameInput();
		Case.Upload = Upload;
		Case.bUsesUpload = true;
		Case.bExpectedAccepted = bExpectedAccepted;
		return Case;
	};
	AddCase(TEXT("valid upload"), true);
	AddCase(TEXT("ground changed without upload and no state ground"), false).bUsesUpload = false;
	FTDFog3DGroundCase& NoGroundCase = AddCase(TEXT("ground follow without any ground"), false);
	NoGroundCase.bUsesUpload = false;
	NoGroundCase.FrameInput.bGroundChanged = false;
	AddCase(TEXT("upload missing the west ring column"), false).Upload.MinCell = GroundWindowMinCell;
	AddCase(TEXT("upload missing the north ring row"), false).Upload.MinCell = GroundWindowMinCell - FIntPoint(GroundBorderCells, 2 * GroundBorderCells);
	AddCase(TEXT("upload with one height too few"), false).Upload.HeightsCm.Pop();
	FTDFog3DGroundCase& NotSquareCase = AddCase(TEXT("upload that is not square"), false);
	NotSquareCase.Upload.Size = FIntPoint(GroundSize + 1, GroundSize - 1);
	NotSquareCase.Upload.HeightsCm.SetNumZeroed((GroundSize + 1) * (GroundSize - 1));
	AddCase(TEXT("upload with a NaN height"), false).Upload.HeightsCm[5] = std::numeric_limits<float>::quiet_NaN();
	AddCase(TEXT("upload with an infinite height"), false).Upload.HeightsCm[7] = std::numeric_limits<float>::infinity();
	AddCase(TEXT("wall step 0"), false).FrameInput.GroundWallStepCm = 0.0f;
	FTDFog3DGroundCase& OffCase = AddCase(TEXT("ground off ignores a bad upload"), true);
	OffCase.FrameInput.bFollowsGround = false;
	OffCase.Upload.HeightsCm.Pop();

	const TArray<bool> CaseAccepted = RunGroundCases(Cases);
	TestEqual(TEXT("Every ground case ran"), CaseAccepted.Num(), Cases.Num());
	for (int32 CaseIndex = 0; CaseIndex < FMath::Min(CaseAccepted.Num(), Cases.Num()); ++CaseIndex)
	{
		TestTrue(*FString::Printf(TEXT("Ground case accepted as expected: %s"), *Cases[CaseIndex].Name), CaseAccepted[CaseIndex] == Cases[CaseIndex].bExpectedAccepted);
	}

	TArray<FTDFluidGpuFog3DFrameInput> MoveSteps;
	FTDFluidGpuFog3DFrameInput& FirstMoveStep = MoveSteps.Add_GetRef(MakeGroundFrameInput());
	FirstMoveStep.GroundUpload = &Upload;
	FTDFluidGpuFog3DFrameInput& OutsideStep = MoveSteps.Add_GetRef(MakeGroundFrameInput());
	OutsideStep.bResetsState = false;
	OutsideStep.bGroundChanged = false;
	OutsideStep.WindowMinCell = GroundWindowMinCell + FIntPoint(16, 0);
	const TSharedRef<FTDFog3DGroundRunResult, ESPMode::ThreadSafe> MoveResult = RunGroundSteps(MoveSteps, false);
	TestTrue(TEXT("Window moved past the state ground rectangle is rejected"), MoveResult->StepAccepted.Num() == 2 && MoveResult->StepAccepted[0] && !MoveResult->StepAccepted[1]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DGroundBlockedWorldHeightTest,
	"TDGame.FluidFXRhi.Fog3D.GroundBlockedWorldHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DGroundBlockedWorldHeightTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DGroundRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D GroundBlockedWorldHeight needs a rendering RHI."));
		return false;
	}

	const FTDFluidGpuFog3DGroundUpload Upload = MakeGroundUpload(&MakeBlockedSlopeHeight);
	const FVector3d BoxCenterCm(125.0, 125.0, 151.0);
	const FVector3d BoxHalfExtentCm(40.0, 40.0, 50.0);
	const FTDFluidGpuFog3DObstacle Box = MakeTDFluidGpuFog3DUprightBox(BoxCenterCm, BoxHalfExtentCm, 0.0, FTDFluidGpuFog3DInflation());

	TArray<FTDFluidGpuFog3DFrameInput> Steps;
	FTDFluidGpuFog3DFrameInput& Step = Steps.Add_GetRef(MakeGroundFrameInput());
	Step.GroundUpload = &Upload;
	Step.Obstacles.Add(Box);
	const TSharedRef<FTDFog3DGroundRunResult, ESPMode::ThreadSafe> Result = RunGroundSteps(Steps, true);
	TestTrue(TEXT("Slope box step accepted"), Result->StepAccepted.Num() == 1 && Result->StepAccepted[0]);
	if (!TestTrue(TEXT("Slope box dump copied"), Result->bDumpCopied))
	{
		return false;
	}

	const int32 CellCount = GroundResolution * GroundResolution * TDFluidGpuFog3DLayerCount;
	const FVector3d BoxCenter(Box.CenterCmYawCosine.X, Box.CenterCmYawCosine.Y, Box.CenterCmYawCosine.Z);
	const FVector3d BoxHalf(Box.HalfExtentCmYawSine.X, Box.HalfExtentCmYawSine.Y, Box.HalfExtentCmYawSine.Z);
	int32 MismatchCount = 0;
	int32 BoxCellCount = 0;
	int32 WallColumnCount = 0;
	int32 WallColumnOpenCellCount = 0;
	int32 FlatRuleDifferenceCount = 0;
	double MinimumBoxMarginCm = TNumericLimits<double>::Max();
	for (int32 LocalX = 0; LocalX < GroundResolution; ++LocalX)
	{
		for (int32 LocalY = 0; LocalY < GroundResolution; ++LocalY)
		{
			const bool bIsWall = IsExpectedWall(Upload, LocalX, LocalY);
			WallColumnCount += bIsWall ? 1 : 0;
			const double GroundHeight = ReadUploadedHeight(Upload, LocalX, LocalY);
			for (int32 Layer = 0; Layer < TDFluidGpuFog3DLayerCount; ++Layer)
			{
				const double LayerCenterCm = (Layer + 0.5) * GroundLayerHeightCm;
				const FVector3d CellCenterCm((LocalX + 0.5) * GroundCellSizeCm, (LocalY + 0.5) * GroundCellSizeCm, GroundHeight + LayerCenterCm);
				const FVector3d Offset = (CellCenterCm - BoxCenter).GetAbs();
				const bool bIsInsideBox = Offset.X < BoxHalf.X && Offset.Y < BoxHalf.Y && Offset.Z < BoxHalf.Z;
				const bool bWasInsideFlatBox = Offset.X < BoxHalf.X && Offset.Y < BoxHalf.Y && FMath::Abs(LayerCenterCm - BoxCenter.Z) < BoxHalf.Z;
				MinimumBoxMarginCm = FMath::Min(MinimumBoxMarginCm, FMath::Min3(FMath::Abs(Offset.X - BoxHalf.X), FMath::Abs(Offset.Y - BoxHalf.Y), FMath::Abs(Offset.Z - BoxHalf.Z)));
				const bool bExpectedBlocked = bIsWall || bIsInsideBox;
				const int32 DumpIndex = (LocalX * GroundResolution + LocalY) * TDFluidGpuFog3DLayerCount + Layer;
				const bool bIsBlocked = Result->DumpValues[CellCount * GroundFluidFieldIndex + DumpIndex] <= 0.5f;
				MismatchCount += bIsBlocked == bExpectedBlocked ? 0 : 1;
				BoxCellCount += bIsInsideBox && !bIsWall ? 1 : 0;
				WallColumnOpenCellCount += bIsWall && !bIsBlocked ? 1 : 0;
				FlatRuleDifferenceCount += bIsInsideBox != bWasInsideFlatBox ? 1 : 0;
			}
		}
	}

	AddInfo(FString::Printf(TEXT("Fog3D GroundBlockedWorldHeight: box cells %d, wall columns %d, open cells in wall columns %d, cells where the world height rule differs from the flat rule %d, mismatches %d, minimum box margin %.4f cm"),
		BoxCellCount, WallColumnCount, WallColumnOpenCellCount, FlatRuleDifferenceCount, MismatchCount, MinimumBoxMarginCm));
	TestTrue(TEXT("No cell centre lies on a box face"), MinimumBoxMarginCm > GroundBoxMarginCm);
	TestTrue(TEXT("Box covers cells on the slope"), BoxCellCount > 0);
	TestTrue(TEXT("Slope moves the box cells away from the flat rule"), FlatRuleDifferenceCount > 0);
	TestTrue(TEXT("Scene has wall columns"), WallColumnCount > 0);
	TestEqual(TEXT("Wall columns are blocked in all 8 layers"), WallColumnOpenCellCount, 0);
	TestEqual(TEXT("Blocked cells follow the cell centre world height rule"), MismatchCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DGroundInputFramesTest,
	"TDGame.FluidFXRhi.Fog3D.GroundInputFrames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DGroundInputFramesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DGroundRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D GroundInputFrames needs a rendering RHI."));
		return false;
	}

	FTDFog3DFrameCompare TotalCompare;
	for (const FTDFog3DGroundFrameCase& Case : MakeGroundFrameCases())
	{
		const FTDFluidGpuFog3DGroundUpload Upload = MakeGroundUpload(Case.HeightAt);
		FTDFluidGpuFog3DFrameInput FrameInput = MakeGroundFrameInput();
		FrameInput.GroundUpload = &Upload;
		FrameInput.Spheres = Case.Spheres;
		const TSharedRef<FTDFog3DGroundFrameResult, ESPMode::ThreadSafe> Result = RunGroundFrameStep(FrameInput);
		TestTrue(*FString::Printf(TEXT("%s: step accepted"), Case.Name), Result->bAccepted);
		if (!TestTrue(*FString::Printf(TEXT("%s: sphere frames read back"), Case.Name), Result->bCopied && Result->Frames.Num() == Case.Spheres.Num()))
		{
			continue;
		}

		FTDFog3DFrameCompare CaseCompare;
		for (int32 SphereIndex = 0; SphereIndex < Case.Spheres.Num(); ++SphereIndex)
		{
			const FTDFog3DExpectedSphereFrame Expected = ComputeExpectedSphereFrame(Upload, Case.Spheres[SphereIndex]);
			const FTDFluidGpuFog3DSphereFrame& Actual = Result->Frames[SphereIndex];
			CompareSphereFrame(Actual, Expected, CaseCompare);
			CompareSphereFrame(Actual, Expected, TotalCompare);
			AddInfo(FString::Printf(TEXT("Fog3D GroundInputFrames %s sphere %d: midpoint (%.4f, %.4f) slope (%.6f, %.6f) expected (%.6f, %.6f), tilted z %.4f..%.4f expected %.4f..%.4f, reference column centre (%.4f, %.4f) height %.4f expected (%.4f, %.4f) %.4f"),
				Case.Name, SphereIndex, Actual.MidpointCmFrameSlope.X, Actual.MidpointCmFrameSlope.Y, Actual.MidpointCmFrameSlope.Z, Actual.MidpointCmFrameSlope.W, Expected.FrameSlope.X, Expected.FrameSlope.Y,
				Actual.TiltedStartCmReferenceHeightCm.Z, Actual.TiltedEndCm.Z, Expected.TiltedStartCm.Z, Expected.TiltedEndCm.Z,
				Actual.ReferenceColumnCm.X, Actual.ReferenceColumnCm.Y, Actual.TiltedStartCmReferenceHeightCm.W, Expected.ReferenceColumnCm.X, Expected.ReferenceColumnCm.Y, Expected.ReferenceHeightCm));
		}
		AddInfo(FString::Printf(TEXT("Fog3D GroundInputFrames %s: %d frames, maximum position error %.3g cm, maximum slope error %.3g"),
			Case.Name, CaseCompare.ComparedFrameCount, CaseCompare.MaximumPositionErrorCm, CaseCompare.MaximumSlopeError));
	}

	TestTrue(TEXT("Frames were compared"), TotalCompare.ComparedFrameCount > 0);
	TestTrue(TEXT("Frame positions match the 64 bit rule within 1e-3 cm"), TotalCompare.MaximumPositionErrorCm <= GroundFramePositionToleranceCm);
	TestTrue(TEXT("Frame slopes match the 64 bit rule within 1e-5"), TotalCompare.MaximumSlopeError <= GroundFrameSlopeTolerance);
	TestEqual(TEXT("Unused frame components stay zero"), TotalCompare.NonZeroPaddingCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DGroundUniformSlopeNearFlatTest,
	"TDGame.FluidFXRhi.Fog3D.GroundUniformSlopeNearFlat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DGroundUniformSlopeNearFlatTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DGroundRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D GroundUniformSlopeNearFlat needs a rendering RHI."));
		return false;
	}

	const FTDFluidGpuFog3DGroundUpload SlopeUpload = MakeGroundUpload(&MakeUniformSlopeHeight);
	const TArray<FTDFluidGpuFog3DFrameInput> SlopeSteps = MakeSlopeRunSteps(&SlopeUpload);
	const TArray<FTDFluidGpuFog3DFrameInput> FlatSteps = MakeSlopeRunSteps(nullptr);
	const TSharedRef<FTDFog3DGroundRunResult, ESPMode::ThreadSafe> SlopeResult = RunGroundSteps(SlopeSteps, true);
	const TSharedRef<FTDFog3DGroundRunResult, ESPMode::ThreadSafe> FlatResult = RunGroundSteps(FlatSteps, true);
	TestTrue(TEXT("Every slope step accepted"), SlopeResult->StepAccepted.Num() == GroundSlopeStepCount && !SlopeResult->StepAccepted.Contains(false));
	TestTrue(TEXT("Every flat step accepted"), FlatResult->StepAccepted.Num() == GroundSlopeStepCount && !FlatResult->StepAccepted.Contains(false));
	const int32 CellCount = GroundResolution * GroundResolution * TDFluidGpuFog3DLayerCount;
	const int32 DumpValueCount = CellCount * TDFluidGpuFog3DDumpFieldCount;
	if (!TestTrue(TEXT("Both dumps copied"), SlopeResult->bDumpCopied && FlatResult->bDumpCopied && SlopeResult->DumpValues.Num() == DumpValueCount && FlatResult->DumpValues.Num() == DumpValueCount))
	{
		return false;
	}

	TArray<double> FieldDifferences;
	FieldDifferences.SetNumZeroed(TDFluidGpuFog3DDumpFieldCount);
	double FlatWindMaximum = 0.0;
	double WindDifferenceMaximum = 0.0;
	bool bAllFinite = true;
	for (int32 FieldIndex = 0; FieldIndex < TDFluidGpuFog3DDumpFieldCount; ++FieldIndex)
	{
		for (int32 CellIndex = 0; CellIndex < CellCount; ++CellIndex)
		{
			const int32 ValueIndex = FieldIndex * CellCount + CellIndex;
			const double SlopeValue = SlopeResult->DumpValues[ValueIndex];
			const double FlatValue = FlatResult->DumpValues[ValueIndex];
			bAllFinite = bAllFinite && FMath::IsFinite(SlopeValue) && FMath::IsFinite(FlatValue);
			const double Difference = FMath::Abs(SlopeValue - FlatValue);
			FieldDifferences[FieldIndex] = FMath::Max(FieldDifferences[FieldIndex], Difference);
			if (FieldIndex < GroundDumpWindFieldCount)
			{
				FlatWindMaximum = FMath::Max(FlatWindMaximum, FMath::Abs(FlatValue));
				WindDifferenceMaximum = FMath::Max(WindDifferenceMaximum, Difference);
			}
		}
	}

	const double WindShare = FlatWindMaximum > 0.0 ? WindDifferenceMaximum / FlatWindMaximum : TNumericLimits<double>::Max();
	const FString Json = MakeSlopeReportJson(FieldDifferences, FlatWindMaximum, WindShare, bAllFinite);
	const FString ReportPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AgentOps/20261115/fog3d/stage2a/impl/step3b-uniform-slope.json"));
	TestTrue(TEXT("Uniform slope report written"), FFileHelper::SaveStringToFile(Json, *ReportPath));
	AddInfo(FString::Printf(TEXT("Fog3D GroundUniformSlopeNearFlat: flat wind maximum %.6g, wind difference maximum %.6g (share %.3g), all finite %d, report %s"),
		FlatWindMaximum, WindDifferenceMaximum, WindShare, bAllFinite ? 1 : 0, *ReportPath));
	AddInfo(Json);
	TestTrue(TEXT("Every value is finite"), bAllFinite);
	TestTrue(TEXT("Flat run has wind"), FlatWindMaximum > 0.0);
	TestTrue(TEXT("Wind difference stays within 1 percent of the flat wind maximum"), WindShare <= GroundSlopeWindShareLimit);
	return true;
}

namespace TDFluidGpuFog3DGroundRhiTestPrivate
{
	constexpr float ConvertDecodeOffsetCm = 800.0f;
	constexpr float ConvertMissingHeightCm = -40.0f;
	constexpr float ConvertClearDepthCm = 1.0e9f;
	constexpr float ConvertMissingBelowFloorCm = 5000.0f;
	constexpr float ConvertFarPlaneDepthCm = 262144.0f;
	const FIntPoint ConvertLowestFoundTexel(0, 1);
	const FIntPoint ConvertTooLowTexel(1, 1);

	struct FTDFog3DConvertCase
	{
		FString Name;
		FTDFluidGpuFog3DFrameInput FrameInput;
		bool bUsesUpload = false;
		bool bExpectedAccepted = false;
	};

	struct FTDFog3DConvertResult
	{
		bool bAccepted = false;
		bool bGroundCopied = false;
		bool bMaterialCopied = false;
		bool bColumnTerrainCopied = false;
		FIntPoint GroundMinCell = FIntPoint::ZeroValue;
		int32 GroundSize = 0;
		TArray<float> GroundHeights;
		TArray<float> MaterialHeights;
		TArray<FVector4f> ColumnTerrain;
		TArray<bool> CaseAccepted;
	};

	bool IsConvertMissingTexel(int32 TexelX, int32 TexelY)
	{
		return (TexelX * 7 + TexelY * 3) % 11 == 0;
	}

	float MakeConvertHeightCm(int32 TexelX, int32 TexelY)
	{
		return 10.0f * static_cast<float>(TexelX) + 0.5f * static_cast<float>(TexelY) - 20.0f;
	}

	float MakeConvertMissingDepthCm(int32 TexelX, int32 TexelY)
	{
		switch ((TexelX + TexelY) % 4)
		{
		case 0:
			return ConvertClearDepthCm;
		case 1:
			return ConvertFarPlaneDepthCm;
		case 2:
			return std::numeric_limits<float>::quiet_NaN();
		default:
			return std::numeric_limits<float>::infinity();
		}
	}

	float MakeConvertExpectedHeightCm(int32 TexelX, int32 TexelY)
	{
		const FIntPoint Texel(TexelX, TexelY);
		if (Texel == ConvertLowestFoundTexel)
		{
			return -ConvertMissingBelowFloorCm;
		}
		if (Texel == ConvertTooLowTexel || IsConvertMissingTexel(TexelX, TexelY))
		{
			return ConvertMissingHeightCm;
		}
		return MakeConvertHeightCm(TexelX, TexelY);
	}

	float MakeConvertDepthCm(int32 TexelX, int32 TexelY)
	{
		const FIntPoint Texel(TexelX, TexelY);
		if (Texel == ConvertLowestFoundTexel)
		{
			return ConvertDecodeOffsetCm + ConvertMissingBelowFloorCm;
		}
		if (Texel == ConvertTooLowTexel)
		{
			return ConvertDecodeOffsetCm + ConvertMissingBelowFloorCm + 1.0f;
		}
		if (IsConvertMissingTexel(TexelX, TexelY))
		{
			return MakeConvertMissingDepthCm(TexelX, TexelY);
		}
		return ConvertDecodeOffsetCm - MakeConvertHeightCm(TexelX, TexelY);
	}

	TArray<float> MakeConvertDepths()
	{
		TArray<float> Depths;
		Depths.SetNumUninitialized(GroundSize * GroundSize);
		for (int32 TexelY = 0; TexelY < GroundSize; ++TexelY)
		{
			for (int32 TexelX = 0; TexelX < GroundSize; ++TexelX)
			{
				Depths[TexelY * GroundSize + TexelX] = MakeConvertDepthCm(TexelX, TexelY);
			}
		}
		return Depths;
	}

	UTextureRenderTarget2D* MakeConvertPlaneTarget(int32 Size)
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
		Target->RenderTargetFormat = RTF_R32f;
		Target->ClearColor = FLinearColor(ConvertClearDepthCm, 0.0f, 0.0f, 0.0f);
		Target->InitAutoFormat(Size, Size);
		Target->UpdateResourceImmediate(true);
		return Target;
	}

	FTDFluidGpuFog3DGroundCapture MakeConvertCapture(FTextureRenderTargetResource* DepthTarget)
	{
		FTDFluidGpuFog3DGroundCapture Capture;
		Capture.DepthTarget = DepthTarget;
		Capture.MinCell = GroundWindowMinCell - FIntPoint(GroundBorderCells, GroundBorderCells);
		Capture.Size = GroundSize;
		Capture.DecodeOffsetCm = ConvertDecodeOffsetCm;
		Capture.MissingHeightCm = ConvertMissingHeightCm;
		Capture.MissingBelowFloorCm = ConvertMissingBelowFloorCm;
		return Capture;
	}

	bool ReadFloatPlane(FRHICommandListImmediate& RHICmdList, const TRefCountPtr<IPooledRenderTarget>& Plane, int32 Size, TArray<float>& OutValues)
	{
		if (!Plane.IsValid())
		{
			return false;
		}

		FRHIGPUTextureReadback Readback(TEXT("TDFluidGpu.Fog3DGroundPlaneReadback"));
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuFog3DGroundPlaneReadback"));
			FRDGTextureRef Source = GraphBuilder.RegisterExternalTexture(Plane);
			AddEnqueueCopyPass(GraphBuilder, &Readback, Source);
			GraphBuilder.SetTextureAccessFinal(Source, ERHIAccess::SRVMask);
			GraphBuilder.Execute();
		}
		RHICmdList.SubmitAndBlockUntilGPUIdle();
		SubmitUntilReady(RHICmdList, [&Readback]() { return Readback.IsReady(); });
		if (!Readback.IsReady())
		{
			return false;
		}

		int32 RowPitchInPixels = 0;
		int32 BufferHeight = 0;
		const float* Values = static_cast<const float*>(Readback.Lock(RowPitchInPixels, &BufferHeight));
		if (!Values || RowPitchInPixels < Size || BufferHeight < Size)
		{
			Readback.Unlock();
			return false;
		}
		OutValues.SetNumUninitialized(Size * Size);
		for (int32 RowIndex = 0; RowIndex < Size; ++RowIndex)
		{
			FMemory::Memcpy(OutValues.GetData() + RowIndex * Size, Values + static_cast<int64>(RowIndex) * RowPitchInPixels, Size * sizeof(float));
		}
		Readback.Unlock();
		return true;
	}

	void UploadConvertDepths(FRHICommandListImmediate& RHICmdList, FTextureRenderTargetResource& DepthTarget, const TArray<float>& Depths)
	{
		const FUpdateTextureRegion2D Region(0, 0, 0, 0, GroundSize, GroundSize);
		RHICmdList.UpdateTexture2D(DepthTarget.GetRenderTargetTexture(), 0, Region, static_cast<uint32>(GroundSize * sizeof(float)), reinterpret_cast<const uint8*>(Depths.GetData()));
		RHICmdList.SubmitAndBlockUntilGPUIdle();
	}

	TSharedRef<FTDFog3DConvertResult, ESPMode::ThreadSafe> RunConvertCapture(FTextureRenderTargetResource* DepthTarget, FTextureRenderTargetResource* MaterialTarget, const TArray<float>& Depths, const TArray<FTDFog3DConvertCase>& Cases, const FTDFluidGpuFog3DGroundUpload& Upload)
	{
		TSharedRef<FTDFog3DConvertResult, ESPMode::ThreadSafe> Result = MakeShared<FTDFog3DConvertResult, ESPMode::ThreadSafe>();
		ENQUEUE_RENDER_COMMAND(TDFluidGpuFog3DGroundConvert)(
			[Result, DepthTarget, MaterialTarget, &Depths, &Cases, &Upload](FRHICommandListImmediate& RHICmdList)
			{
				UploadConvertDepths(RHICmdList, *DepthTarget, Depths);

				FTDFluidGpuFog3DState State;
				FTDFluidGpuFog3DFrameInput FrameInput = MakeGroundFrameInput();
				FrameInput.GroundCapture = MakeConvertCapture(DepthTarget);
				FrameInput.GroundMaterialTarget = MaterialTarget;
				{
					FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuFog3DGroundConvertStep"));
					Result->bAccepted = AddTDFluidGpuFog3DPasses(GraphBuilder, State, FrameInput);
					GraphBuilder.Execute();
				}
				Result->GroundMinCell = State.GroundMinCell;
				Result->GroundSize = State.GroundSize;
				Result->bGroundCopied = ReadFloatPlane(RHICmdList, State.GroundHeightTarget, GroundSize, Result->GroundHeights);
				Result->bMaterialCopied = ReadFloatPlane(RHICmdList, CreateRenderTarget(MaterialTarget->GetRenderTargetTexture(), TEXT("TDFluidGpu.Fog3DGroundMaterialReadback")), GroundSize, Result->MaterialHeights);
				Result->bColumnTerrainCopied = ReadColumnTerrain(RHICmdList, State, Result->ColumnTerrain);
				State.Release();

				for (const FTDFog3DConvertCase& Case : Cases)
				{
					FTDFluidGpuFog3DState CaseState;
					FTDFluidGpuFog3DFrameInput CaseInput = Case.FrameInput;
					CaseInput.GroundUpload = Case.bUsesUpload ? &Upload : nullptr;
					FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuFog3DGroundConvertCase"));
					Result->CaseAccepted.Add(AddTDFluidGpuFog3DPasses(GraphBuilder, CaseState, CaseInput));
					GraphBuilder.Execute();
					CaseState.Release();
				}
			});
		FlushRenderingCommands();
		return Result;
	}

	TArray<FTDFog3DConvertCase> MakeConvertCases(FTextureRenderTargetResource* DepthTarget, FTextureRenderTargetResource* MaterialTarget, FTextureRenderTargetResource* WrongSizeTarget)
	{
		TArray<FTDFog3DConvertCase> Cases;
		auto AddCase = [&Cases, DepthTarget, MaterialTarget](const TCHAR* Name, bool bExpectedAccepted) -> FTDFog3DConvertCase&
		{
			FTDFog3DConvertCase& Case = Cases.AddDefaulted_GetRef();
			Case.Name = Name;
			Case.FrameInput = MakeGroundFrameInput();
			Case.FrameInput.GroundCapture = MakeConvertCapture(DepthTarget);
			Case.FrameInput.GroundMaterialTarget = MaterialTarget;
			Case.bExpectedAccepted = bExpectedAccepted;
			return Case;
		};
		AddCase(TEXT("valid capture"), true);
		AddCase(TEXT("valid capture without a material target"), true).FrameInput.GroundMaterialTarget = nullptr;
		AddCase(TEXT("capture and upload together"), false).bUsesUpload = true;
		AddCase(TEXT("capture target smaller than the capture size"), false).FrameInput.GroundCapture.DepthTarget = WrongSizeTarget;
		AddCase(TEXT("material target of another size"), false).FrameInput.GroundMaterialTarget = WrongSizeTarget;
		AddCase(TEXT("capture missing the west ring column"), false).FrameInput.GroundCapture.MinCell = GroundWindowMinCell - FIntPoint(0, GroundBorderCells);
		AddCase(TEXT("capture size zero"), false).FrameInput.GroundCapture.Size = 0;
		AddCase(TEXT("missing below floor zero"), false).FrameInput.GroundCapture.MissingBelowFloorCm = 0.0f;
		AddCase(TEXT("missing below floor not a number"), false).FrameInput.GroundCapture.MissingBelowFloorCm = std::numeric_limits<float>::quiet_NaN();
		AddCase(TEXT("decode offset not a number"), false).FrameInput.GroundCapture.DecodeOffsetCm = std::numeric_limits<float>::quiet_NaN();
		AddCase(TEXT("missing height infinite"), false).FrameInput.GroundCapture.MissingHeightCm = std::numeric_limits<float>::infinity();
		AddCase(TEXT("capture without the changed flag and no state ground"), false).FrameInput.bGroundChanged = false;
		FTDFog3DConvertCase& OffCase = AddCase(TEXT("ground off ignores a bad capture"), true);
		OffCase.FrameInput.bFollowsGround = false;
		OffCase.FrameInput.GroundCapture.DepthTarget = WrongSizeTarget;
		return Cases;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DGroundConvertCaptureTest,
	"TDGame.FluidFXRhi.Fog3D.GroundConvertCapture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DGroundConvertCaptureTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DGroundRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D GroundConvertCapture needs a rendering RHI."));
		return false;
	}

	TStrongObjectPtr<UTextureRenderTarget2D> DepthTarget(MakeConvertPlaneTarget(GroundSize));
	TStrongObjectPtr<UTextureRenderTarget2D> MaterialTarget(MakeConvertPlaneTarget(GroundSize));
	TStrongObjectPtr<UTextureRenderTarget2D> WrongSizeTarget(MakeConvertPlaneTarget(GroundSize - 1));
	FTextureRenderTargetResource* DepthResource = DepthTarget->GameThread_GetRenderTargetResource();
	FTextureRenderTargetResource* MaterialResource = MaterialTarget->GameThread_GetRenderTargetResource();
	FTextureRenderTargetResource* WrongSizeResource = WrongSizeTarget->GameThread_GetRenderTargetResource();
	if (!TestNotNull(TEXT("Depth target resource"), DepthResource) || !TestNotNull(TEXT("Material target resource"), MaterialResource) || !TestNotNull(TEXT("Wrong size target resource"), WrongSizeResource))
	{
		return false;
	}

	const TArray<float> Depths = MakeConvertDepths();
	const FTDFluidGpuFog3DGroundUpload Upload = MakeGroundUpload([](int32, int32) { return 0.0; });
	const TArray<FTDFog3DConvertCase> Cases = MakeConvertCases(DepthResource, MaterialResource, WrongSizeResource);
	const TSharedRef<FTDFog3DConvertResult, ESPMode::ThreadSafe> Result = RunConvertCapture(DepthResource, MaterialResource, Depths, Cases, Upload);

	TestTrue(TEXT("Capture step accepted"), Result->bAccepted);
	TestTrue(TEXT("State ground rect is the capture rect"), Result->GroundMinCell == GroundWindowMinCell - FIntPoint(GroundBorderCells, GroundBorderCells) && Result->GroundSize == GroundSize);
	if (!TestTrue(TEXT("Ground height read back"), Result->bGroundCopied && Result->GroundHeights.Num() == GroundSize * GroundSize))
	{
		return false;
	}

	int32 HeightMismatchCount = 0;
	int32 MissingTexelCount = 0;
	for (int32 TexelY = 0; TexelY < GroundSize; ++TexelY)
	{
		for (int32 TexelX = 0; TexelX < GroundSize; ++TexelX)
		{
			MissingTexelCount += IsConvertMissingTexel(TexelX, TexelY) ? 1 : 0;
			HeightMismatchCount += Result->GroundHeights[TexelY * GroundSize + TexelX] == MakeConvertExpectedHeightCm(TexelX, TexelY) ? 0 : 1;
		}
	}
	TestTrue(TEXT("Scene has missing texels"), MissingTexelCount > 0);
	TestEqual(TEXT("Every texel is decode offset minus depth, or the missing height when that height is below the floor margin or not finite"), HeightMismatchCount, 0);
	TestEqual(TEXT("Height exactly at the floor margin is kept"), Result->GroundHeights[ConvertLowestFoundTexel.Y * GroundSize + ConvertLowestFoundTexel.X], -ConvertMissingBelowFloorCm);
	TestEqual(TEXT("Height one centimetre under the floor margin is missing"), Result->GroundHeights[ConvertTooLowTexel.Y * GroundSize + ConvertTooLowTexel.X], ConvertMissingHeightCm);

	const int32 CornerX = GroundSize - 1;
	TestEqual(TEXT("Texel (1, 0) grows by 10 along +X"), Result->GroundHeights[1], MakeConvertExpectedHeightCm(1, 0));
	TestEqual(TEXT("Texel (0, 2) grows by 0.5 per row along +Y"), Result->GroundHeights[2 * GroundSize], MakeConvertExpectedHeightCm(0, 2));
	TestEqual(TEXT("Last texel of the first row"), Result->GroundHeights[CornerX], MakeConvertExpectedHeightCm(CornerX, 0));

	int32 MaterialMismatchCount = 0;
	if (TestTrue(TEXT("Material copy read back"), Result->bMaterialCopied && Result->MaterialHeights.Num() == GroundSize * GroundSize))
	{
		for (int32 TexelIndex = 0; TexelIndex < GroundSize * GroundSize; ++TexelIndex)
		{
			MaterialMismatchCount += FMemory::Memcmp(&Result->MaterialHeights[TexelIndex], &Result->GroundHeights[TexelIndex], sizeof(float)) == 0 ? 0 : 1;
		}
		TestEqual(TEXT("Material copy equals the canonical ground bit for bit"), MaterialMismatchCount, 0);
	}

	int32 ColumnMismatchCount = 0;
	if (TestTrue(TEXT("Column terrain read back"), Result->bColumnTerrainCopied && Result->ColumnTerrain.Num() == GroundColumnTerrainSize * GroundColumnTerrainSize))
	{
		for (int32 LocalY = -1; LocalY <= GroundResolution; ++LocalY)
		{
			for (int32 LocalX = -1; LocalX <= GroundResolution; ++LocalX)
			{
				const float Expected = MakeConvertExpectedHeightCm(LocalX + GroundBorderCells, LocalY + GroundBorderCells);
				ColumnMismatchCount += Result->ColumnTerrain[GetColumnTerrainIndex(LocalX, LocalY)].X == Expected ? 0 : 1;
			}
		}
		TestEqual(TEXT("Window column (i, j) reads capture texel (i + border, j + border), ring included"), ColumnMismatchCount, 0);
	}

	TestEqual(TEXT("Every convert case ran"), Result->CaseAccepted.Num(), Cases.Num());
	for (int32 CaseIndex = 0; CaseIndex < FMath::Min(Result->CaseAccepted.Num(), Cases.Num()); ++CaseIndex)
	{
		TestTrue(*FString::Printf(TEXT("Convert case accepted as expected: %s"), *Cases[CaseIndex].Name), Result->CaseAccepted[CaseIndex] == Cases[CaseIndex].bExpectedAccepted);
	}
	AddInfo(FString::Printf(TEXT("Fog3D GroundConvertCapture: %d texels, missing %d, height mismatches %d, material mismatches %d, column mismatches %d, cases %d"),
		GroundSize * GroundSize, MissingTexelCount, HeightMismatchCount, MaterialMismatchCount, ColumnMismatchCount, Result->CaseAccepted.Num()));
	return true;
}

namespace TDFluidGpuFog3DGroundRhiTestPrivate
{
	struct FTDFog3DStage2aDumpCheck
	{
		int32 NonFiniteValueCount = 0;
		int32 ColumnHeightMismatchCount = 0;
		int32 WrongFileSizeCount = 0;
		int32 WrongArraySizeCount = 0;
		int32 DumpCount = 0;
	};

	int32 CountNonFiniteValues(const TArray<float>& Values)
	{
		int32 NonFiniteCount = 0;
		for (const float Value : Values)
		{
			NonFiniteCount += FMath::IsFinite(Value) ? 0 : 1;
		}
		return NonFiniteCount;
	}

	int32 CountColumnHeightMismatches(const FTDFog3DReplay& Replay, const FIntPoint& WindowMinCell, const TArray<float>& ColumnValues)
	{
		const int32 Resolution = Replay.Resolution;
		const FTDFluidGpuFog3DGroundUpload& Upload = Replay.GroundUpload;
		int32 MismatchCount = 0;
		for (int32 LocalX = 0; LocalX < Resolution; ++LocalX)
		{
			for (int32 LocalY = 0; LocalY < Resolution; ++LocalY)
			{
				const int32 SourceX = WindowMinCell.X + LocalX - Upload.MinCell.X;
				const int32 SourceY = WindowMinCell.Y + LocalY - Upload.MinCell.Y;
				const float Expected = Upload.HeightsCm[SourceY * Upload.Size.X + SourceX];
				MismatchCount += FMemory::Memcmp(&Expected, &ColumnValues[LocalX * Resolution + LocalY], sizeof(float)) == 0 ? 0 : 1;
			}
		}
		return MismatchCount;
	}

	void CheckStage2aDump(const FTDFog3DReplay& Replay, const FTDFog3DReplayDump& Dump, const FString& DumpDirectory, FTDFog3DStage2aDumpCheck& InOutCheck)
	{
		++InOutCheck.DumpCount;
		const FTDFog3DReplayStep& Step = Replay.Steps[Dump.StepIndex];
		const int32 ColumnFloatCount = 4 * Replay.Resolution * Replay.Resolution;
		const int32 FrameFloatCount = Step.Spheres.Num() * TDFog3DReplaySphereFrameFloatCount;
		if (!Dump.ColumnValues || !Dump.FrameValues || Dump.ColumnValues->Num() != ColumnFloatCount || Dump.FrameValues->Num() != FrameFloatCount)
		{
			++InOutCheck.WrongArraySizeCount;
			return;
		}

		InOutCheck.NonFiniteValueCount += CountNonFiniteValues(*Dump.Values) + CountNonFiniteValues(*Dump.ColumnValues) + CountNonFiniteValues(*Dump.FrameValues);
		InOutCheck.ColumnHeightMismatchCount += CountColumnHeightMismatches(Replay, Step.WindowMinCell, *Dump.ColumnValues);

		const FString Stem = FPaths::Combine(DumpDirectory, FString::Printf(TEXT("%s__%s"), *Replay.Name, *Dump.Name));
		const int64 CellCount = static_cast<int64>(Replay.Resolution) * Replay.Resolution * TDFluidGpuFog3DLayerCount;
		const bool bHasExpectedFileSizes = IFileManager::Get().FileSize(*(Stem + TEXT(".f32"))) == CellCount * Step.DumpFieldIndices.Num() * static_cast<int64>(sizeof(float))
			&& IFileManager::Get().FileSize(*(Stem + TEXT(".json"))) > 0
			&& IFileManager::Get().FileSize(*(Stem + TEXT(".column.bin"))) == static_cast<int64>(ColumnFloatCount) * sizeof(float)
			&& IFileManager::Get().FileSize(*(Stem + TEXT(".frames.bin"))) == static_cast<int64>(FrameFloatCount) * sizeof(float);
		InOutCheck.WrongFileSizeCount += bHasExpectedFileSizes ? 0 : 1;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DStage2aReplayDumpsTest,
	"TDGame.FluidFXRhi.Fog3D.Stage2aReplayDumps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DStage2aReplayDumpsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DGroundRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D Stage2aReplayDumps needs a rendering RHI."));
		return false;
	}

	const FString ReplayDirectory = GetTDFog3DStage2aReplayDirectory();
	const FString DumpDirectory = GetTDFog3DStage2aDumpDirectory();
	TArray<FString> FileNames;
	FString ManifestError;
	if (!TestTrue(TEXT("Stage 2a replay manifest read"), LoadTDFog3DReplayManifestNames(ReplayDirectory, FileNames, ManifestError)))
	{
		AddError(ManifestError);
		return false;
	}
	TArray<FString> FoundFileNames;
	IFileManager::Get().FindFiles(FoundFileNames, *FPaths::Combine(ReplayDirectory, TEXT("s2a_*.json")), true, false);
	TestTrue(TEXT("Stage 2a manifest lists replays"), FileNames.Num() > 0);
	TestEqual(TEXT("Stage 2a replay files in the folder match the manifest count"), FoundFileNames.Num(), FileNames.Num());
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
		TestTrue(*FString::Printf(TEXT("%s: replay carries a ground picture"), *Replay.Name), Replay.bHasGround);

		FTDFog3DStage2aDumpCheck Check;
		FTDFog3DReplayRunOptions Options;
		Options.bWritesDumpFiles = true;
		Options.DumpDirectory = DumpDirectory;
		Options.OnDump = [&](const FTDFog3DReplayDump& Dump)
		{
			CheckStage2aDump(Replay, Dump, DumpDirectory, Check);
		};
		const FTDFog3DReplayRunResult Result = RunTDFog3DReplay(Replay, Options);
		TestEqual(*FString::Printf(TEXT("%s: every compute accepted"), *Replay.Name), Result.RejectedComputeCount, 0);
		TestEqual(*FString::Printf(TEXT("%s: every dump copied"), *Replay.Name), Result.FailedDumpCount, 0);
		TestEqual(*FString::Printf(TEXT("%s: computes run"), *Replay.Name), Result.ComputeCount, Replay.ComputeCount);
		TestEqual(*FString::Printf(TEXT("%s: every listed dump written"), *Replay.Name), Result.WrittenFileCount, Replay.DumpCount);
		TestEqual(*FString::Printf(TEXT("%s: dump files without write errors"), *Replay.Name), Result.FailedFileCount, 0);
		TestEqual(*FString::Printf(TEXT("%s: every dump checked"), *Replay.Name), Check.DumpCount, Replay.DumpCount);
		TestEqual(*FString::Printf(TEXT("%s: dump arrays have the expected sizes"), *Replay.Name), Check.WrongArraySizeCount, 0);
		TestEqual(*FString::Printf(TEXT("%s: dump files have the expected sizes"), *Replay.Name), Check.WrongFileSizeCount, 0);
		TestEqual(*FString::Printf(TEXT("%s: fields, columns and frames are finite"), *Replay.Name), Check.NonFiniteValueCount, 0);
		TestEqual(*FString::Printf(TEXT("%s: column ground height equals the uploaded picture bit for bit"), *Replay.Name), Check.ColumnHeightMismatchCount, 0);

		const bool bIsClean = Result.RejectedComputeCount == 0 && Result.FailedDumpCount == 0 && Result.ComputeCount == Replay.ComputeCount && Result.WrittenFileCount == Replay.DumpCount
			&& Result.FailedFileCount == 0 && Check.DumpCount == Replay.DumpCount && Check.WrongArraySizeCount == 0 && Check.WrongFileSizeCount == 0 && Check.NonFiniteValueCount == 0 && Check.ColumnHeightMismatchCount == 0;
		FailedReplayCount += bIsClean ? 0 : 1;
		TotalComputeCount += Result.ComputeCount;
		TotalDumpCount += Result.WrittenFileCount;
		AddInfo(FString::Printf(TEXT("Fog3D Stage2aReplayDumps %s: ground %dx%d uploaded as %dx%d, %d computes, rejected %d, %d dumps, column height mismatches %d, %.1f s"),
			*Replay.Name, Replay.GroundSourceSize.X, Replay.GroundSourceSize.Y, Replay.GroundUpload.Size.X, Replay.GroundUpload.Size.Y, Result.ComputeCount, Result.RejectedComputeCount, Result.WrittenFileCount, Check.ColumnHeightMismatchCount, Result.Seconds));
	}

	AddInfo(FString::Printf(TEXT("Fog3D Stage2aReplayDumps: %d replays, failed %d, %d computes, %d dumps written to %s"), FileNames.Num(), FailedReplayCount, TotalComputeCount, TotalDumpCount, *DumpDirectory));
	TestEqual(TEXT("Every stage 2a replay ran and dumped cleanly"), FailedReplayCount, 0);
	return true;
}

namespace TDFluidGpuFog3DGroundRhiTestPrivate
{
	constexpr int32 GroundFillFieldIndex = 6;
	constexpr double GroundBlendCliffLineCm = 125.0;
	constexpr double GroundBlendMidpointXCm = 118.0;
	constexpr double GroundBlendHalfMoveCm = 4.0;
	constexpr double GroundBlendSphereYCm = 125.0;
	constexpr double GroundBlendClearanceCm = 56.0;
	constexpr double GroundBlendRadiusCm = 50.0;
	constexpr double GroundBlendWindStrength = 0.8;
	constexpr double GroundBlendErosionPerSec = 4.0;
	constexpr double GroundBlendWindToleranceCmPerSec = 5.0e-3;
	constexpr double GroundBlendFillTolerance = 1.0e-5;
	constexpr double GroundBlendClearSpreadFactor = 10.0;
	constexpr int32 GroundBlendBeyondFirstColumn = 9;
	constexpr double GroundBlendRampFirstHeightCm = 24.0;
	constexpr double GroundBlendRampStepCm = 24.0;
	constexpr double GroundBlendRampTopCm = 90.0;
	constexpr double GroundBlendRampMidpointXCm = 174.0;
	constexpr double GroundStillSegmentCm = 0.01;
	constexpr double GroundDwellNodeLow = 0.11270166537925831;
	constexpr double GroundDwellNodeMiddle = 0.5;
	constexpr double GroundDwellNodeHigh = 0.88729833462074169;
	constexpr double GroundDwellWeightSide = 0.27777777777777778;
	constexpr double GroundDwellWeightMiddle = 0.44444444444444444;
	const FVector3d GroundBlendVelocityCmPerSec(200.0, 60.0, 0.0);

	enum class ETDFog3DBlendSceneKind : uint8
	{
		UniformSlope,
		BlockingCurb,
		PartialCurb,
		OpenRampCurb
	};

	enum class ETDFog3DBlendSampleKind : uint8
	{
		EastFace,
		NorthFace,
		TopFace,
		Center
	};

	struct FTDFog3DBlendScene
	{
		const TCHAR* Name = nullptr;
		double (*HeightAt)(int32 LocalX, int32 LocalY) = nullptr;
		double (*PlaneHeightAt)(double XCm, double YCm) = nullptr;
		ETDFog3DBlendSceneKind Kind = ETDFog3DBlendSceneKind::UniformSlope;
		double SphereMidpointXCm = GroundBlendMidpointXCm;
	};

	struct FTDFog3DBlendSample
	{
		double Share = 1.0;
		double TiltedValue = 0.0;
		double WorldValue = 0.0;
		double Value = 0.0;
	};

	struct FTDFog3DBlendCompare
	{
		double MaximumWindErrorCmPerSec = 0.0;
		double MaximumFillError = 0.0;
		int32 ComparedWindFaceCount = 0;
		int32 WeightedFaceCount = 0;
		int32 FullShareCount = 0;
		int32 PartialShareCount = 0;
		int32 ZeroShareCount = 0;
		int32 BlockedReachFaceCount = 0;
		double BlockedReachMaximumWind = 0.0;
		int32 BlockedReachCellCount = 0;
		double BlockedReachMaximumFill = 0.0;
		int32 ClearPartialFaceCount = 0;
		int32 PartialOutsideCount = 0;
		int32 PartialStrictlyBetweenCount = 0;
		int32 AverageSensitiveFaceCount = 0;
	};

	double MakeBlendCurbHeight(int32 LocalX, double CurbHeightCm)
	{
		const double CenterXCm = (LocalX + 0.5) * GroundCellSizeCm;
		return CenterXCm < GroundBlendCliffLineCm ? GroundTan30 * (CenterXCm - GroundBlendCliffLineCm) : CurbHeightCm;
	}

	double MakeBlendCurb100Height(int32 LocalX, int32)
	{
		return MakeBlendCurbHeight(LocalX, 100.0);
	}

	double MakeBlendCurb60Height(int32 LocalX, int32)
	{
		return MakeBlendCurbHeight(LocalX, 60.0);
	}

	double MakeBlendCurb90RampHeight(int32 LocalX, int32)
	{
		const double CenterXCm = (LocalX + 0.5) * GroundCellSizeCm;
		if (CenterXCm < GroundBlendCliffLineCm)
		{
			return GroundTan30 * (CenterXCm - GroundBlendCliffLineCm);
		}
		const int32 RampColumn = FMath::FloorToInt32((CenterXCm - GroundBlendCliffLineCm) / GroundCellSizeCm);
		return FMath::Min(GroundBlendRampFirstHeightCm + GroundBlendRampStepCm * RampColumn, GroundBlendRampTopCm);
	}

	double MakeBlendUniformPlaneHeight(double XCm, double YCm)
	{
		return 20.0 + GroundTan30 * XCm + 0.25 * YCm;
	}

	double MakeBlendCurbPlaneHeight(double XCm, double)
	{
		return GroundTan30 * (XCm - GroundBlendCliffLineCm);
	}

	TArray<FTDFog3DBlendScene> MakeBlendScenes()
	{
		TArray<FTDFog3DBlendScene> Scenes;
		Scenes.Add({ TEXT("uniform slope"), &MakeFrameUniformSlopeHeight, &MakeBlendUniformPlaneHeight, ETDFog3DBlendSceneKind::UniformSlope });
		Scenes.Add({ TEXT("slope beside 100 cm curb"), &MakeBlendCurb100Height, &MakeBlendCurbPlaneHeight, ETDFog3DBlendSceneKind::BlockingCurb });
		Scenes.Add({ TEXT("slope beside 60 cm curb"), &MakeBlendCurb60Height, &MakeBlendCurbPlaneHeight, ETDFog3DBlendSceneKind::PartialCurb });
		Scenes.Add({ TEXT("slope beside 90 cm curb in open 24 cm steps"), &MakeBlendCurb90RampHeight, &MakeBlendCurbPlaneHeight, ETDFog3DBlendSceneKind::OpenRampCurb, GroundBlendRampMidpointXCm });
		return Scenes;
	}

	FTDFluidGpuFog3DSphere MakeBlendSphere(const FTDFog3DBlendScene& Scene)
	{
		const double SphereZCm = Scene.PlaneHeightAt(Scene.SphereMidpointXCm, GroundBlendSphereYCm) + GroundBlendClearanceCm;
		const FVector3d StartCm(Scene.SphereMidpointXCm - GroundBlendHalfMoveCm, GroundBlendSphereYCm, SphereZCm);
		const FVector3d EndCm(Scene.SphereMidpointXCm + GroundBlendHalfMoveCm, GroundBlendSphereYCm, SphereZCm);
		return MakeGroundSphere(StartCm, EndCm, GroundBlendRadiusCm, GroundBlendVelocityCmPerSec, GroundBlendWindStrength, GroundBlendErosionPerSec);
	}

	double IntegrateExpectedBrushProfile(double Area, double Height)
	{
		const double HeightCubed = Height * Height * Height;
		return Area * Area * Height - (2.0 / 3.0) * Area * HeightCubed + HeightCubed * Height * Height / 5.0;
	}

	double ComputeExpectedLayerBrushWeight(const FVector3d& OffsetCm, double RadiusCm)
	{
		const double Area = 1.0 - (OffsetCm.X * OffsetCm.X + OffsetCm.Y * OffsetCm.Y) / (RadiusCm * RadiusCm);
		if (!(Area > 0.0))
		{
			return 0.0;
		}
		const double AreaRoot = FMath::Sqrt(Area);
		const double HalfLayerCm = 0.5 * GroundLayerHeightCm;
		const double LowerHeight = FMath::Max((OffsetCm.Z - HalfLayerCm) / RadiusCm, -AreaRoot);
		const double UpperHeight = FMath::Min((OffsetCm.Z + HalfLayerCm) / RadiusCm, AreaRoot);
		if (!(UpperHeight > LowerHeight))
		{
			return 0.0;
		}
		return (RadiusCm / GroundLayerHeightCm) * (IntegrateExpectedBrushProfile(Area, UpperHeight) - IntegrateExpectedBrushProfile(Area, LowerHeight));
	}

	double ComputeExpectedSweptBrushWeight(const FVector3d& PositionCm, const FVector3d& StartCm, const FVector3d& EndCm, double RadiusCm)
	{
		const FVector3d SegmentCm = EndCm - StartCm;
		FVector3d OffsetCm = PositionCm - StartCm;
		const double SegmentLengthSquared = SegmentCm.SquaredLength();
		if (SegmentLengthSquared > 0.0)
		{
			OffsetCm -= FMath::Clamp(OffsetCm.Dot(SegmentCm) / SegmentLengthSquared, 0.0, 1.0) * SegmentCm;
		}
		return ComputeExpectedLayerBrushWeight(OffsetCm, RadiusCm);
	}

	double ComputeExpectedDwellAmount(const FVector3d& CellCenterCm, const FVector3d& StartCm, const FVector3d& EndCm, double RadiusCm, double DeltaSeconds)
	{
		const FVector3d SegmentCm = EndCm - StartCm;
		if (SegmentCm.Length() < GroundStillSegmentCm)
		{
			return DeltaSeconds * ComputeExpectedLayerBrushWeight(CellCenterCm - StartCm, RadiusCm);
		}

		const FVector2d HorizontalSegmentCm(SegmentCm.X, SegmentCm.Y);
		const FVector2d HorizontalOffsetCm(CellCenterCm.X - StartCm.X, CellCenterCm.Y - StartCm.Y);
		const double OffsetSquared = HorizontalOffsetCm.SquaredLength();
		const double HorizontalLengthSquared = HorizontalSegmentCm.SquaredLength();
		const double RadiusSquared = RadiusCm * RadiusCm;
		double EnterTime = 0.0;
		double LeaveTime = 1.0;
		if (FMath::Sqrt(HorizontalLengthSquared) < GroundStillSegmentCm)
		{
			if (!(OffsetSquared < RadiusSquared))
			{
				return 0.0;
			}
		}
		else
		{
			const double Projection = HorizontalOffsetCm.Dot(HorizontalSegmentCm);
			const double Discriminant = Projection * Projection - HorizontalLengthSquared * (OffsetSquared - RadiusSquared);
			if (!(Discriminant > 0.0))
			{
				return 0.0;
			}
			const double DiscriminantRoot = FMath::Sqrt(Discriminant);
			EnterTime = FMath::Clamp((Projection - DiscriminantRoot) / HorizontalLengthSquared, 0.0, 1.0);
			LeaveTime = FMath::Clamp((Projection + DiscriminantRoot) / HorizontalLengthSquared, 0.0, 1.0);
			if (!(LeaveTime > EnterTime))
			{
				return 0.0;
			}
		}

		const double Span = LeaveTime - EnterTime;
		const auto WeightAt = [&](double Node)
		{
			return ComputeExpectedLayerBrushWeight(CellCenterCm - (StartCm + (EnterTime + Span * Node) * SegmentCm), RadiusCm);
		};
		const double WeightedSum = GroundDwellWeightSide * WeightAt(GroundDwellNodeLow) + GroundDwellWeightMiddle * WeightAt(GroundDwellNodeMiddle) + GroundDwellWeightSide * WeightAt(GroundDwellNodeHigh);
		return DeltaSeconds * Span * WeightedSum;
	}

	FVector3d GetBlendSampleLocalPositionCm(int32 LocalX, int32 LocalY, int32 Layer, ETDFog3DBlendSampleKind Kind)
	{
		const FVector3d CenterCm((LocalX + 0.5) * GroundCellSizeCm, (LocalY + 0.5) * GroundCellSizeCm, (Layer + 0.5) * GroundLayerHeightCm);
		switch (Kind)
		{
		case ETDFog3DBlendSampleKind::EastFace:
			return CenterCm + FVector3d(0.5 * GroundCellSizeCm, 0.0, 0.0);
		case ETDFog3DBlendSampleKind::NorthFace:
			return CenterCm + FVector3d(0.0, 0.5 * GroundCellSizeCm, 0.0);
		case ETDFog3DBlendSampleKind::TopFace:
			return CenterCm + FVector3d(0.0, 0.0, 0.5 * GroundLayerHeightCm);
		default:
			return CenterCm;
		}
	}

	double GetBlendSampleGroundHeightCm(const FTDFluidGpuFog3DGroundUpload& Upload, int32 LocalX, int32 LocalY, ETDFog3DBlendSampleKind Kind)
	{
		const double ColumnHeightCm = ReadUploadedHeight(Upload, LocalX, LocalY);
		if (Kind == ETDFog3DBlendSampleKind::EastFace)
		{
			return 0.5 * (ColumnHeightCm + ReadUploadedHeight(Upload, LocalX + 1, LocalY));
		}
		if (Kind == ETDFog3DBlendSampleKind::NorthFace)
		{
			return 0.5 * (ColumnHeightCm + ReadUploadedHeight(Upload, LocalX, LocalY + 1));
		}
		return ColumnHeightCm;
	}

	double ComputeExpectedTiltShare(const FTDFog3DExpectedSphereFrame& Frame, double SampleGroundHeightCm, const FVector3d& SampleCm)
	{
		if (Frame.FrameSlope.X == 0.0 && Frame.FrameSlope.Y == 0.0)
		{
			return 1.0;
		}
		const double DeviationCm = SampleGroundHeightCm - Frame.ReferenceHeightCm - Frame.FrameSlope.X * (SampleCm.X - Frame.ReferenceColumnCm.X) - Frame.FrameSlope.Y * (SampleCm.Y - Frame.ReferenceColumnCm.Y);
		return FMath::Clamp((2.0 * GroundWallStepCm - FMath::Abs(DeviationCm)) / GroundWallStepCm, 0.0, 1.0);
	}

	double BlendExpectedFrames(double Share, double TiltedValue, double WorldValue)
	{
		if (Share >= 1.0)
		{
			return TiltedValue;
		}
		if (Share <= 0.0)
		{
			return WorldValue;
		}
		return Share * TiltedValue + (1.0 - Share) * WorldValue;
	}

	FTDFog3DBlendSample ComputeExpectedBlendSample(const FTDFluidGpuFog3DGroundUpload& Upload, const FTDFluidGpuFog3DSphere& Sphere, const FTDFog3DExpectedSphereFrame& Frame, int32 LocalX, int32 LocalY, int32 Layer, ETDFog3DBlendSampleKind Kind, double DeltaSeconds)
	{
		const double GroundHeightCm = GetBlendSampleGroundHeightCm(Upload, LocalX, LocalY, Kind);
		FVector3d WorldCm = GetBlendSampleLocalPositionCm(LocalX, LocalY, Layer, Kind);
		WorldCm.Z += GroundHeightCm;
		const FVector3d TiltedCm(WorldCm.X, WorldCm.Y, TiltExpectedHeightCm(WorldCm, Frame.MidpointCm, Frame.FrameSlope));
		const FVector3d StartCm(Sphere.StartCmRadiusCm.X, Sphere.StartCmRadiusCm.Y, Sphere.StartCmRadiusCm.Z);
		const FVector3d EndCm(Sphere.EndCmWindStrength.X, Sphere.EndCmWindStrength.Y, Sphere.EndCmWindStrength.Z);
		const double RadiusCm = Sphere.StartCmRadiusCm.W;

		FTDFog3DBlendSample Sample;
		Sample.Share = ComputeExpectedTiltShare(Frame, GroundHeightCm, WorldCm);
		if (Kind == ETDFog3DBlendSampleKind::Center)
		{
			Sample.TiltedValue = ComputeExpectedDwellAmount(TiltedCm, Frame.TiltedStartCm, Frame.TiltedEndCm, RadiusCm, DeltaSeconds);
			Sample.WorldValue = ComputeExpectedDwellAmount(WorldCm, StartCm, EndCm, RadiusCm, DeltaSeconds);
		}
		else
		{
			Sample.TiltedValue = ComputeExpectedSweptBrushWeight(TiltedCm, Frame.TiltedStartCm, Frame.TiltedEndCm, RadiusCm);
			Sample.WorldValue = ComputeExpectedSweptBrushWeight(WorldCm, StartCm, EndCm, RadiusCm);
		}
		Sample.Value = BlendExpectedFrames(Sample.Share, Sample.TiltedValue, Sample.WorldValue);
		return Sample;
	}

	double ComputeExpectedWindTarget(const FTDFluidGpuFog3DGroundUpload& Upload, int32 LocalX, int32 LocalY, int32 AxisIndex)
	{
		if (AxisIndex == 0)
		{
			return GroundBlendVelocityCmPerSec.X;
		}
		if (AxisIndex == 1)
		{
			return GroundBlendVelocityCmPerSec.Y;
		}
		const FVector2d ColumnSlope = ReadExpectedColumnSlope(Upload, LocalX, LocalY);
		return GroundBlendVelocityCmPerSec.Z - GroundBlendVelocityCmPerSec.X * ColumnSlope.X - GroundBlendVelocityCmPerSec.Y * ColumnSlope.Y;
	}

	int32 GetBlendDumpIndex(int32 LocalX, int32 LocalY, int32 Layer)
	{
		return (LocalX * GroundResolution + LocalY) * TDFluidGpuFog3DLayerCount + Layer;
	}

	bool IsBlendCellFluid(const TArray<float>& DumpValues, int32 LocalX, int32 LocalY, int32 Layer)
	{
		const int32 CellCount = GroundResolution * GroundResolution * TDFluidGpuFog3DLayerCount;
		return DumpValues[CellCount * GroundFluidFieldIndex + GetBlendDumpIndex(LocalX, LocalY, Layer)] > 0.5f;
	}

	bool IsBlendFaceInterior(const TArray<float>& DumpValues, int32 LocalX, int32 LocalY, int32 Layer, int32 AxisIndex)
	{
		const FIntVector Neighbor = FIntVector(LocalX, LocalY, Layer) + FIntVector(AxisIndex == 0 ? 1 : 0, AxisIndex == 1 ? 1 : 0, AxisIndex == 2 ? 1 : 0);
		if (Neighbor.X >= GroundResolution || Neighbor.Y >= GroundResolution || Neighbor.Z >= TDFluidGpuFog3DLayerCount)
		{
			return false;
		}
		return IsBlendCellFluid(DumpValues, Neighbor.X, Neighbor.Y, Neighbor.Z);
	}

	void CountBlendShare(const FTDFog3DBlendSample& Sample, FTDFog3DBlendCompare& Compare)
	{
		if (!(Sample.TiltedValue > 0.0) && !(Sample.WorldValue > 0.0))
		{
			return;
		}
		++Compare.WeightedFaceCount;
		Compare.FullShareCount += Sample.Share >= 1.0 ? 1 : 0;
		Compare.ZeroShareCount += Sample.Share <= 0.0 ? 1 : 0;
		Compare.PartialShareCount += Sample.Share > 0.0 && Sample.Share < 1.0 ? 1 : 0;
	}

	void CheckBlendPartialFace(const FTDFog3DBlendSample& Sample, double TargetTimesStrength, double ActualWind, FTDFog3DBlendCompare& Compare)
	{
		const double TiltedWind = TargetTimesStrength * Sample.TiltedValue;
		const double WorldWind = TargetTimesStrength * Sample.WorldValue;
		const bool bIsClearPartial = Sample.Share > 0.0 && Sample.Share < 1.0 && FMath::Abs(TiltedWind - WorldWind) > GroundBlendClearSpreadFactor * GroundBlendWindToleranceCmPerSec;
		if (!bIsClearPartial)
		{
			return;
		}
		++Compare.ClearPartialFaceCount;
		const double LowWind = FMath::Min(TiltedWind, WorldWind);
		const double HighWind = FMath::Max(TiltedWind, WorldWind);
		Compare.PartialOutsideCount += ActualWind < LowWind - GroundBlendWindToleranceCmPerSec || ActualWind > HighWind + GroundBlendWindToleranceCmPerSec ? 1 : 0;
		Compare.PartialStrictlyBetweenCount += ActualWind - LowWind > GroundBlendWindToleranceCmPerSec && HighWind - ActualWind > GroundBlendWindToleranceCmPerSec ? 1 : 0;
	}

	void CountAverageSensitiveFace(const FTDFluidGpuFog3DGroundUpload& Upload, const FTDFog3DExpectedSphereFrame& Frame, const FTDFog3DBlendSample& Sample, int32 LocalX, int32 LocalY, int32 Layer, ETDFog3DBlendSampleKind Kind, double TargetTimesStrength, FTDFog3DBlendCompare& Compare)
	{
		if (Kind != ETDFog3DBlendSampleKind::EastFace && Kind != ETDFog3DBlendSampleKind::NorthFace)
		{
			return;
		}
		FVector3d WorldCm = GetBlendSampleLocalPositionCm(LocalX, LocalY, Layer, Kind);
		WorldCm.Z += GetBlendSampleGroundHeightCm(Upload, LocalX, LocalY, Kind);
		const double ColumnShare = ComputeExpectedTiltShare(Frame, ReadUploadedHeight(Upload, LocalX, LocalY), WorldCm);
		const double ColumnValue = BlendExpectedFrames(ColumnShare, Sample.TiltedValue, Sample.WorldValue);
		const bool bIsSensitive = FMath::Abs(TargetTimesStrength * (ColumnValue - Sample.Value)) > GroundBlendClearSpreadFactor * GroundBlendWindToleranceCmPerSec;
		Compare.AverageSensitiveFaceCount += bIsSensitive ? 1 : 0;
	}

	void CompareBlendWindFace(const FTDFog3DBlendScene& Scene, const FTDFluidGpuFog3DGroundUpload& Upload, const FTDFluidGpuFog3DSphere& Sphere, const FTDFog3DExpectedSphereFrame& Frame, const TArray<float>& DumpValues, int32 LocalX, int32 LocalY, int32 Layer, int32 AxisIndex, double DeltaSeconds, FTDFog3DBlendCompare& Compare)
	{
		static const ETDFog3DBlendSampleKind FaceKinds[3] = { ETDFog3DBlendSampleKind::EastFace, ETDFog3DBlendSampleKind::NorthFace, ETDFog3DBlendSampleKind::TopFace };
		if (!IsBlendFaceInterior(DumpValues, LocalX, LocalY, Layer, AxisIndex))
		{
			return;
		}

		const FTDFog3DBlendSample Sample = ComputeExpectedBlendSample(Upload, Sphere, Frame, LocalX, LocalY, Layer, FaceKinds[AxisIndex], DeltaSeconds);
		const double TargetTimesStrength = ComputeExpectedWindTarget(Upload, LocalX, LocalY, AxisIndex) * static_cast<double>(Sphere.EndCmWindStrength.W);
		const double ExpectedWind = Sample.Value > 0.0 ? TargetTimesStrength * Sample.Value : 0.0;
		const int32 CellCount = GroundResolution * GroundResolution * TDFluidGpuFog3DLayerCount;
		const double ActualWind = DumpValues[CellCount * AxisIndex + GetBlendDumpIndex(LocalX, LocalY, Layer)];
		Compare.MaximumWindErrorCmPerSec = FMath::Max(Compare.MaximumWindErrorCmPerSec, FMath::Abs(ActualWind - ExpectedWind));
		++Compare.ComparedWindFaceCount;
		CountBlendShare(Sample, Compare);
		CountAverageSensitiveFace(Upload, Frame, Sample, LocalX, LocalY, Layer, FaceKinds[AxisIndex], TargetTimesStrength, Compare);

		const bool bIsBlockedReach = Scene.Kind == ETDFog3DBlendSceneKind::BlockingCurb && LocalX >= GroundBlendBeyondFirstColumn && Sample.TiltedValue > 0.0 && Sample.WorldValue == 0.0 && TargetTimesStrength != 0.0;
		if (bIsBlockedReach)
		{
			++Compare.BlockedReachFaceCount;
			Compare.BlockedReachMaximumWind = FMath::Max(Compare.BlockedReachMaximumWind, FMath::Abs(ActualWind));
		}
		if (Scene.Kind == ETDFog3DBlendSceneKind::PartialCurb)
		{
			CheckBlendPartialFace(Sample, TargetTimesStrength, ActualWind, Compare);
		}
	}

	void CompareBlendFillCell(const FTDFog3DBlendScene& Scene, const FTDFluidGpuFog3DGroundUpload& Upload, const FTDFluidGpuFog3DSphere& Sphere, const FTDFog3DExpectedSphereFrame& Frame, const TArray<float>& DumpValues, int32 LocalX, int32 LocalY, int32 Layer, double DeltaSeconds, FTDFog3DBlendCompare& Compare)
	{
		const FTDFog3DBlendSample Sample = ComputeExpectedBlendSample(Upload, Sphere, Frame, LocalX, LocalY, Layer, ETDFog3DBlendSampleKind::Center, DeltaSeconds);
		const double Erosion = static_cast<double>(Sphere.VelocityCmPerSecErosionPerSec.W) * Sample.Value;
		const double ExpectedFill = Erosion > 0.0 ? FMath::Exp(-Erosion) - 1.0 : 0.0;
		const int32 CellCount = GroundResolution * GroundResolution * TDFluidGpuFog3DLayerCount;
		const double ActualFill = DumpValues[CellCount * GroundFillFieldIndex + GetBlendDumpIndex(LocalX, LocalY, Layer)];
		Compare.MaximumFillError = FMath::Max(Compare.MaximumFillError, FMath::Abs(ActualFill - ExpectedFill));

		const bool bIsBlockedReach = Scene.Kind == ETDFog3DBlendSceneKind::BlockingCurb && LocalX >= GroundBlendBeyondFirstColumn && Sample.TiltedValue > 0.0 && Sample.WorldValue == 0.0;
		if (bIsBlockedReach)
		{
			++Compare.BlockedReachCellCount;
			Compare.BlockedReachMaximumFill = FMath::Max(Compare.BlockedReachMaximumFill, FMath::Abs(ActualFill));
		}
	}

	FTDFog3DBlendCompare CompareBlendScene(const FTDFog3DBlendScene& Scene, const FTDFluidGpuFog3DGroundUpload& Upload, const FTDFluidGpuFog3DSphere& Sphere, const TArray<float>& DumpValues, double DeltaSeconds)
	{
		const FTDFog3DExpectedSphereFrame Frame = ComputeExpectedSphereFrame(Upload, Sphere);
		FTDFog3DBlendCompare Compare;
		for (int32 LocalX = 0; LocalX < GroundResolution; ++LocalX)
		{
			for (int32 LocalY = 0; LocalY < GroundResolution; ++LocalY)
			{
				for (int32 Layer = 0; Layer < TDFluidGpuFog3DLayerCount; ++Layer)
				{
					if (!IsBlendCellFluid(DumpValues, LocalX, LocalY, Layer))
					{
						continue;
					}
					for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
					{
						CompareBlendWindFace(Scene, Upload, Sphere, Frame, DumpValues, LocalX, LocalY, Layer, AxisIndex, DeltaSeconds, Compare);
					}
					CompareBlendFillCell(Scene, Upload, Sphere, Frame, DumpValues, LocalX, LocalY, Layer, DeltaSeconds, Compare);
				}
			}
		}
		return Compare;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DGroundPlaneBlendCasesTest,
	"TDGame.FluidFXRhi.Fog3D.GroundPlaneBlendCases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DGroundPlaneBlendCasesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DGroundRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D GroundPlaneBlendCases needs a rendering RHI."));
		return false;
	}

	const int32 DumpValueCount = GroundResolution * GroundResolution * TDFluidGpuFog3DLayerCount * TDFluidGpuFog3DDumpFieldCount;
	for (const FTDFog3DBlendScene& Scene : MakeBlendScenes())
	{
		const FTDFluidGpuFog3DGroundUpload Upload = MakeGroundUpload(Scene.HeightAt);
		const FTDFluidGpuFog3DSphere Sphere = MakeBlendSphere(Scene);
		TArray<FTDFluidGpuFog3DFrameInput> Steps;
		FTDFluidGpuFog3DFrameInput& Step = Steps.Add_GetRef(MakeGroundFrameInput());
		Step.GroundUpload = &Upload;
		Step.TestKnobs.bAppliesPressure = false;
		Step.Spheres.Add(Sphere);
		const TSharedRef<FTDFog3DGroundRunResult, ESPMode::ThreadSafe> Result = RunGroundSteps(Steps, true);
		TestTrue(*FString::Printf(TEXT("%s: step accepted"), Scene.Name), Result->StepAccepted.Num() == 1 && Result->StepAccepted[0]);
		if (!TestTrue(*FString::Printf(TEXT("%s: dump copied"), Scene.Name), Result->bDumpCopied && Result->DumpValues.Num() == DumpValueCount))
		{
			continue;
		}

		const FTDFog3DBlendCompare Compare = CompareBlendScene(Scene, Upload, Sphere, Result->DumpValues, static_cast<double>(Step.DeltaSeconds));
		AddInfo(FString::Printf(TEXT("Fog3D GroundPlaneBlendCases %s: compared faces %d, weighted faces %d (share 1: %d, between: %d, 0: %d), maximum wind error %.3g cm/s, maximum fill error %.3g, beyond-curb faces reached only by the tilted frame %d (largest GPU wind %.3g), cells %d (largest GPU fill %.3g), clear partial faces %d (outside %d, strictly between %d), faces whose wind depends on the face average ground %d"),
			Scene.Name, Compare.ComparedWindFaceCount, Compare.WeightedFaceCount, Compare.FullShareCount, Compare.PartialShareCount, Compare.ZeroShareCount, Compare.MaximumWindErrorCmPerSec, Compare.MaximumFillError,
			Compare.BlockedReachFaceCount, Compare.BlockedReachMaximumWind, Compare.BlockedReachCellCount, Compare.BlockedReachMaximumFill, Compare.ClearPartialFaceCount, Compare.PartialOutsideCount, Compare.PartialStrictlyBetweenCount, Compare.AverageSensitiveFaceCount));
		TestTrue(*FString::Printf(TEXT("%s: sphere reaches open faces"), Scene.Name), Compare.WeightedFaceCount > 0);
		TestTrue(*FString::Printf(TEXT("%s: wind matches the 64 bit blend within 5e-3 cm/s"), Scene.Name), Compare.MaximumWindErrorCmPerSec <= GroundBlendWindToleranceCmPerSec);
		TestTrue(*FString::Printf(TEXT("%s: fill matches the 64 bit blend within 1e-5"), Scene.Name), Compare.MaximumFillError <= GroundBlendFillTolerance);
		if (Scene.Kind == ETDFog3DBlendSceneKind::UniformSlope)
		{
			TestEqual(*FString::Printf(TEXT("%s: every reached face uses only the tilted frame"), Scene.Name), Compare.PartialShareCount + Compare.ZeroShareCount, 0);
		}
		if (Scene.Kind == ETDFog3DBlendSceneKind::BlockingCurb)
		{
			TestTrue(*FString::Printf(TEXT("%s: scene has beyond-curb faces the tilted frame alone would reach"), Scene.Name), Compare.BlockedReachFaceCount > 0 && Compare.BlockedReachCellCount > 0);
			TestEqual(*FString::Printf(TEXT("%s: those faces get no wind"), Scene.Name), Compare.BlockedReachMaximumWind, 0.0);
			TestEqual(*FString::Printf(TEXT("%s: those cells are not eroded"), Scene.Name), Compare.BlockedReachMaximumFill, 0.0);
		}
		if (Scene.Kind == ETDFog3DBlendSceneKind::PartialCurb)
		{
			TestTrue(*FString::Printf(TEXT("%s: scene has faces between the two frames"), Scene.Name), Compare.ClearPartialFaceCount > 0);
			TestEqual(*FString::Printf(TEXT("%s: partial faces stay between the tilted and world winds"), Scene.Name), Compare.PartialOutsideCount, 0);
			TestTrue(*FString::Printf(TEXT("%s: some partial faces lie strictly between"), Scene.Name), Compare.PartialStrictlyBetweenCount > 0);
		}
		if (Scene.Kind == ETDFog3DBlendSceneKind::OpenRampCurb)
		{
			TestTrue(*FString::Printf(TEXT("%s: some open faces change wind when the face average ground is replaced by the column ground"), Scene.Name), Compare.AverageSensitiveFaceCount > 0);
		}
	}
	return true;
}

namespace TDFluidGpuFog3DGroundRhiTestPrivate
{
	struct FTDFog3DClampedTimeBlendCase
	{
		double WindStrength = 1.0;
		float DeltaSeconds = 1.0f / 60.0f;
		double WindGain = 1.0;
	};

	struct FTDFog3DClampedTimeBlendCompare
	{
		FTDFog3DBlendCompare Blend;
		int32 ClippedFaceCount = 0;
		double WorstFaceWeightStrength = 0.0;
	};

	TArray<FTDFog3DClampedTimeBlendCase> MakeClampedTimeBlendCases()
	{
		TArray<FTDFog3DClampedTimeBlendCase> Cases;
		for (const double WindStrength : { 1.0, 2.5, 3.0 })
		{
			for (const float DeltaSeconds : { 1.0f / 60.0f, 1.0f / 30.0f, 1.0f / 144.0f })
			{
				for (const double WindGain : { 1.0, 1.25 })
				{
					Cases.Add({ WindStrength, DeltaSeconds, WindGain });
				}
			}
		}
		return Cases;
	}

	double ComputeExpectedClampedTimeBlendFactor(double WeightStrength, double DeltaSeconds)
	{
		const double ClampedFactor = FMath::Min(WeightStrength, 1.0);
		const double StepRepeat = TDFluidGpuFog3DWindBlendReferenceStepsPerSecond * DeltaSeconds;
		if (FMath::Abs(StepRepeat - 1.0) <= TDFluidGpuFog3DWindBlendUnitStepTolerance || ClampedFactor >= 1.0)
		{
			return ClampedFactor;
		}
		return -std::expm1(StepRepeat * std::log1p(-ClampedFactor));
	}

	FTDFluidGpuFog3DSphere MakeClampedTimeBlendSphere(const FTDFog3DBlendScene& Scene, const FTDFog3DClampedTimeBlendCase& Case)
	{
		FTDFluidGpuFog3DSphere Sphere = MakeBlendSphere(Scene);
		const FVector3d GainedVelocityCmPerSec = GroundBlendVelocityCmPerSec * Case.WindGain;
		Sphere.EndCmWindStrength.W = static_cast<float>(Case.WindStrength);
		Sphere.VelocityCmPerSecErosionPerSec = FVector4f(FVector3f(GainedVelocityCmPerSec), Sphere.VelocityCmPerSecErosionPerSec.W);
		return Sphere;
	}

	void CompareClampedTimeBlendWindFace(const FTDFluidGpuFog3DGroundUpload& Upload, const FTDFluidGpuFog3DSphere& Sphere, const FTDFog3DExpectedSphereFrame& Frame, const TArray<float>& DumpValues, int32 LocalX, int32 LocalY, int32 Layer, int32 AxisIndex, const FTDFog3DClampedTimeBlendCase& Case, FTDFog3DClampedTimeBlendCompare& Compare)
	{
		static const ETDFog3DBlendSampleKind FaceKinds[3] = { ETDFog3DBlendSampleKind::EastFace, ETDFog3DBlendSampleKind::NorthFace, ETDFog3DBlendSampleKind::TopFace };
		if (!IsBlendFaceInterior(DumpValues, LocalX, LocalY, Layer, AxisIndex))
		{
			return;
		}

		const double DeltaSeconds = static_cast<double>(Case.DeltaSeconds);
		const FTDFog3DBlendSample Sample = ComputeExpectedBlendSample(Upload, Sphere, Frame, LocalX, LocalY, Layer, FaceKinds[AxisIndex], DeltaSeconds);
		const double WeightStrength = Sample.Value * static_cast<double>(Sphere.EndCmWindStrength.W);
		const double GainedTarget = Case.WindGain * ComputeExpectedWindTarget(Upload, LocalX, LocalY, AxisIndex);
		const double ExpectedWind = Sample.Value > 0.0 ? GainedTarget * ComputeExpectedClampedTimeBlendFactor(WeightStrength, DeltaSeconds) : 0.0;
		const int32 CellCount = GroundResolution * GroundResolution * TDFluidGpuFog3DLayerCount;
		const double ActualWind = DumpValues[CellCount * AxisIndex + GetBlendDumpIndex(LocalX, LocalY, Layer)];
		const double WindError = FMath::Abs(ActualWind - ExpectedWind);
		if (WindError > Compare.Blend.MaximumWindErrorCmPerSec)
		{
			Compare.Blend.MaximumWindErrorCmPerSec = WindError;
			Compare.WorstFaceWeightStrength = WeightStrength;
		}
		++Compare.Blend.ComparedWindFaceCount;
		Compare.Blend.WeightedFaceCount += Sample.Value > 0.0 ? 1 : 0;
		Compare.ClippedFaceCount += Sample.Value > 0.0 && WeightStrength >= 1.0 ? 1 : 0;
	}

	FTDFog3DClampedTimeBlendCompare CompareClampedTimeBlendScene(const FTDFog3DBlendScene& Scene, const FTDFluidGpuFog3DGroundUpload& Upload, const FTDFluidGpuFog3DSphere& Sphere, const TArray<float>& DumpValues, const FTDFog3DClampedTimeBlendCase& Case)
	{
		const FTDFog3DExpectedSphereFrame Frame = ComputeExpectedSphereFrame(Upload, Sphere);
		FTDFog3DClampedTimeBlendCompare Compare;
		for (int32 LocalX = 0; LocalX < GroundResolution; ++LocalX)
		{
			for (int32 LocalY = 0; LocalY < GroundResolution; ++LocalY)
			{
				for (int32 Layer = 0; Layer < TDFluidGpuFog3DLayerCount; ++Layer)
				{
					if (!IsBlendCellFluid(DumpValues, LocalX, LocalY, Layer))
					{
						continue;
					}
					for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
					{
						CompareClampedTimeBlendWindFace(Upload, Sphere, Frame, DumpValues, LocalX, LocalY, Layer, AxisIndex, Case, Compare);
					}
					CompareBlendFillCell(Scene, Upload, Sphere, Frame, DumpValues, LocalX, LocalY, Layer, static_cast<double>(Case.DeltaSeconds), Compare.Blend);
				}
			}
		}
		return Compare;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DClampedTimeBlendPlaneCasesTest,
	"TDGame.FluidFXRhi.Fog3D.ClampedTimeBlendPlaneCases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DClampedTimeBlendPlaneCasesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DGroundRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D ClampedTimeBlendPlaneCases needs a rendering RHI."));
		return false;
	}

	const TArray<FTDFog3DBlendScene> Scenes = MakeBlendScenes();
	const FTDFog3DBlendScene* PlaneScene = Scenes.FindByPredicate([](const FTDFog3DBlendScene& Scene) { return Scene.Kind == ETDFog3DBlendSceneKind::UniformSlope; });
	if (!TestNotNull(TEXT("The uniform slope plane scene exists"), PlaneScene))
	{
		return false;
	}

	const FTDFluidGpuFog3DGroundUpload Upload = MakeGroundUpload(PlaneScene->HeightAt);
	const int32 DumpValueCount = GroundResolution * GroundResolution * TDFluidGpuFog3DLayerCount * TDFluidGpuFog3DDumpFieldCount;
	for (const FTDFog3DClampedTimeBlendCase& Case : MakeClampedTimeBlendCases())
	{
		const FString CaseName = FString::Printf(TEXT("strength %.2f, steps per second %.0f, gain %.2f"), Case.WindStrength, 1.0 / static_cast<double>(Case.DeltaSeconds), Case.WindGain);
		const FTDFluidGpuFog3DSphere Sphere = MakeClampedTimeBlendSphere(*PlaneScene, Case);
		TArray<FTDFluidGpuFog3DFrameInput> Steps;
		FTDFluidGpuFog3DFrameInput& Step = Steps.Add_GetRef(MakeGroundFrameInput());
		Step.GroundUpload = &Upload;
		Step.TestKnobs.bAppliesPressure = false;
		Step.DeltaSeconds = Case.DeltaSeconds;
		Step.bUsesClampedTimeWindBlend = true;
		Step.Spheres.Add(Sphere);
		const TSharedRef<FTDFog3DGroundRunResult, ESPMode::ThreadSafe> Result = RunGroundSteps(Steps, true);
		TestTrue(*FString::Printf(TEXT("%s: step accepted"), *CaseName), Result->StepAccepted.Num() == 1 && Result->StepAccepted[0]);
		if (!TestTrue(*FString::Printf(TEXT("%s: dump copied"), *CaseName), Result->bDumpCopied && Result->DumpValues.Num() == DumpValueCount))
		{
			continue;
		}

		const FTDFog3DClampedTimeBlendCompare Compare = CompareClampedTimeBlendScene(*PlaneScene, Upload, Sphere, Result->DumpValues, Case);
		AddInfo(FString::Printf(TEXT("Fog3D ClampedTimeBlendPlaneCases %s: compared faces %d, weighted faces %d, clipped faces %d, maximum wind error %.3g cm/s at weight times strength %.9g, maximum fill error %.3g"),
			*CaseName, Compare.Blend.ComparedWindFaceCount, Compare.Blend.WeightedFaceCount, Compare.ClippedFaceCount, Compare.Blend.MaximumWindErrorCmPerSec, Compare.WorstFaceWeightStrength, Compare.Blend.MaximumFillError));
		TestTrue(*FString::Printf(TEXT("%s: sphere reaches open faces"), *CaseName), Compare.Blend.WeightedFaceCount > 0);
		TestTrue(*FString::Printf(TEXT("%s: wind matches the 64 bit clamped time blend within 5e-3 cm/s"), *CaseName), Compare.Blend.MaximumWindErrorCmPerSec <= GroundBlendWindToleranceCmPerSec);
		TestTrue(*FString::Printf(TEXT("%s: fill matches the 64 bit erosion within 1e-5"), *CaseName), Compare.Blend.MaximumFillError <= GroundBlendFillTolerance);
		if (Case.WindStrength > 1.0)
		{
			TestTrue(*FString::Printf(TEXT("%s: some faces reach the clamp"), *CaseName), Compare.ClippedFaceCount > 0);
		}
	}
	return true;
}

namespace TDFluidGpuFog3DGroundRhiTestPrivate
{
	constexpr int32 GroundTimingWarmupComputeCount = 20;
	constexpr int32 GroundTimingMeasuredComputeCount = 80;
	constexpr int32 GroundTimingWindowMovePeriod = 10;
	constexpr int32 GroundTimingWindowMoveCells = 16;
	constexpr int32 GroundTimingRoundCount = 2;
	constexpr int32 GroundTimingSphereCount = 4;
	constexpr int32 GroundTimingUploadBorderCells = 2;
	constexpr double GroundTimingClearanceCm = 50.0;
	constexpr double GroundTimingSwingStepCm = 25.0;

	struct FTDFog3DGroundTiming
	{
		TArray<double> StillMilliseconds;
		TArray<double> MovingMilliseconds;
	};

	int32 GetGroundTimingWindowX(int32 StepIndex)
	{
		return GroundTimingWindowMoveCells * (StepIndex / GroundTimingWindowMovePeriod);
	}

	FTDFluidGpuFog3DGroundUpload MakeGroundTimingUpload(int32 Resolution)
	{
		const int32 LastWindowX = GetGroundTimingWindowX(GroundTimingWarmupComputeCount + GroundTimingMeasuredComputeCount - 1);
		const int32 Size = LastWindowX + Resolution + 2 * GroundTimingUploadBorderCells;
		FTDFluidGpuFog3DGroundUpload Upload;
		Upload.MinCell = FIntPoint(-GroundTimingUploadBorderCells, -GroundTimingUploadBorderCells);
		Upload.Size = FIntPoint(Size, Size);
		Upload.HeightsCm.SetNumUninitialized(Size * Size);
		for (int32 TexelY = 0; TexelY < Size; ++TexelY)
		{
			for (int32 TexelX = 0; TexelX < Size; ++TexelX)
			{
				Upload.HeightsCm[TexelY * Size + TexelX] = static_cast<float>(GroundTan30 * (Upload.MinCell.X + TexelX + 0.5) * GroundCellSizeCm);
			}
		}
		return Upload;
	}

	TArray<FTDFluidGpuFog3DFrameInput> MakeGroundTimingSteps(int32 Resolution, const FTDFluidGpuFog3DGroundUpload* Upload)
	{
		TArray<FTDFluidGpuFog3DFrameInput> Steps;
		for (int32 StepIndex = 0; StepIndex < GroundTimingWarmupComputeCount + GroundTimingMeasuredComputeCount; ++StepIndex)
		{
			FTDFluidGpuFog3DFrameInput& Step = Steps.AddDefaulted_GetRef();
			Step.Resolution = Resolution;
			Step.bResetsState = StepIndex == 0;
			Step.WindowMinCell = FIntPoint(GetGroundTimingWindowX(StepIndex), 0);
			Step.PreviousWindowMinCell = FIntPoint(GetGroundTimingWindowX(FMath::Max(StepIndex - 1, 0)), 0);
			Step.bFollowsGround = Upload != nullptr;
			Step.GroundWallStepCm = static_cast<float>(GroundWallStepCm);
			Step.bGroundChanged = Upload != nullptr && StepIndex == 0;
			Step.GroundUpload = StepIndex == 0 ? Upload : nullptr;
			const double WindowOffsetCm = Step.WindowMinCell.X * static_cast<double>(Step.CellSizeCm);
			const double StartXCm = 300.0 + (StepIndex % GroundTimingWindowMovePeriod) * GroundTimingSwingStepCm;
			const double EndXCm = StartXCm + GroundTimingSwingStepCm;
			const double Slope = Upload ? GroundTan30 : 0.0;
			for (int32 SphereIndex = 0; SphereIndex < GroundTimingSphereCount; ++SphereIndex)
			{
				const double YCm = 200.0 + 300.0 * SphereIndex;
				const FVector3d StartCm(StartXCm, YCm, Slope * (WindowOffsetCm + StartXCm) + GroundTimingClearanceCm);
				const FVector3d EndCm(EndXCm, YCm, Slope * (WindowOffsetCm + EndXCm) + GroundTimingClearanceCm);
				Step.Spheres.Add(MakeGroundSphere(StartCm, EndCm, 30.0, FVector3d(1500.0, 0.0, Slope * 1500.0), 1.0, 30.0));
			}
		}
		return Steps;
	}

	FTDFog3DGroundTiming MeasureGroundTimingSteps(const TArray<FTDFluidGpuFog3DFrameInput>& Steps)
	{
		TSharedRef<FTDFog3DGroundTiming, ESPMode::ThreadSafe> Timing = MakeShared<FTDFog3DGroundTiming, ESPMode::ThreadSafe>();
		ENQUEUE_RENDER_COMMAND(TDFluidGpuFog3DGroundStepTiming)(
			[Timing, &Steps](FRHICommandListImmediate& RHICmdList)
			{
				FTDFluidGpuFog3DState State;
				TArray<FRenderQueryRHIRef> StartQueries;
				TArray<FRenderQueryRHIRef> EndQueries;
				TArray<bool> MeasuredStepMoved;
				for (int32 StepIndex = 0; StepIndex < Steps.Num(); ++StepIndex)
				{
					const bool bIsMeasured = StepIndex >= GroundTimingWarmupComputeCount;
					if (bIsMeasured)
					{
						RHICmdList.EndRenderQuery(StartQueries.Add_GetRef(RHICreateRenderQuery(RQT_AbsoluteTime)));
						MeasuredStepMoved.Add(Steps[StepIndex].WindowMinCell != Steps[StepIndex].PreviousWindowMinCell);
					}
					{
						FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuFog3DGroundTimingStep"));
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
						(MeasuredStepMoved[QueryIndex] ? Timing->MovingMilliseconds : Timing->StillMilliseconds).Add(Milliseconds);
					}
				}
				State.Release();
			});
		FlushRenderingCommands();
		return *Timing;
	}

	double GetGroundTimingMean(const TArray<double>& Values)
	{
		if (Values.IsEmpty())
		{
			return std::numeric_limits<double>::quiet_NaN();
		}
		double Sum = 0.0;
		for (const double Value : Values)
		{
			Sum += Value;
		}
		return Sum / Values.Num();
	}

	double GetGroundTimingMedian(TArray<double> Values)
	{
		if (Values.IsEmpty())
		{
			return std::numeric_limits<double>::quiet_NaN();
		}
		Values.Sort();
		const int32 Middle = Values.Num() / 2;
		return Values.Num() % 2 == 1 ? Values[Middle] : 0.5 * (Values[Middle - 1] + Values[Middle]);
	}

	FString MakeGroundTimingSideJson(const FTDFog3DGroundTiming& Timing)
	{
		return FString::Printf(TEXT("{\"still_count\": %d, \"still_mean_ms\": %.6f, \"still_median_ms\": %.6f, \"moving_count\": %d, \"moving_mean_ms\": %.6f, \"moving_median_ms\": %.6f}"),
			Timing.StillMilliseconds.Num(), GetGroundTimingMean(Timing.StillMilliseconds), GetGroundTimingMedian(Timing.StillMilliseconds),
			Timing.MovingMilliseconds.Num(), GetGroundTimingMean(Timing.MovingMilliseconds), GetGroundTimingMedian(Timing.MovingMilliseconds));
	}

	bool IsGroundTimingMeasured(const FTDFog3DGroundTiming& Timing)
	{
		return !Timing.StillMilliseconds.IsEmpty() && !Timing.MovingMilliseconds.IsEmpty()
			&& FMath::IsFinite(GetGroundTimingMean(Timing.StillMilliseconds)) && FMath::IsFinite(GetGroundTimingMean(Timing.MovingMilliseconds));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DGroundStepTimingTest,
	"TDGame.FluidFXRhi.Fog3D.GroundStepTiming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DGroundStepTimingTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DGroundRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D GroundStepTiming needs a rendering RHI."));
		return false;
	}

	TArray<FString> ResolutionJsons;
	for (const int32 Resolution : { 128, 256 })
	{
		const FTDFluidGpuFog3DGroundUpload Upload = MakeGroundTimingUpload(Resolution);
		const TArray<FTDFluidGpuFog3DFrameInput> OffSteps = MakeGroundTimingSteps(Resolution, nullptr);
		const TArray<FTDFluidGpuFog3DFrameInput> OnSteps = MakeGroundTimingSteps(Resolution, &Upload);
		FTDFog3DGroundTiming Off;
		FTDFog3DGroundTiming On;
		for (int32 RoundIndex = 0; RoundIndex < GroundTimingRoundCount; ++RoundIndex)
		{
			const FTDFog3DGroundTiming OffRound = MeasureGroundTimingSteps(OffSteps);
			const FTDFog3DGroundTiming OnRound = MeasureGroundTimingSteps(OnSteps);
			Off.StillMilliseconds.Append(OffRound.StillMilliseconds);
			Off.MovingMilliseconds.Append(OffRound.MovingMilliseconds);
			On.StillMilliseconds.Append(OnRound.StillMilliseconds);
			On.MovingMilliseconds.Append(OnRound.MovingMilliseconds);
			AddInfo(FString::Printf(TEXT("Fog3D GroundStepTiming %d round %d: ground off still mean %.4f ms, moving mean %.4f ms; ground on still mean %.4f ms, moving mean %.4f ms"),
				Resolution, RoundIndex, GetGroundTimingMean(OffRound.StillMilliseconds), GetGroundTimingMean(OffRound.MovingMilliseconds), GetGroundTimingMean(OnRound.StillMilliseconds), GetGroundTimingMean(OnRound.MovingMilliseconds)));
		}

		const double StillDifference = GetGroundTimingMean(On.StillMilliseconds) - GetGroundTimingMean(Off.StillMilliseconds);
		const double MovingDifference = GetGroundTimingMean(On.MovingMilliseconds) - GetGroundTimingMean(Off.MovingMilliseconds);
		AddInfo(FString::Printf(TEXT("Fog3D GroundStepTiming %dx%dx8 (information only): still computes off %d mean %.4f median %.4f ms, on %d mean %.4f median %.4f ms, on minus off %.4f ms; computes with window move off %d mean %.4f median %.4f ms, on %d mean %.4f median %.4f ms, on minus off %.4f ms"),
			Resolution, Resolution, Off.StillMilliseconds.Num(), GetGroundTimingMean(Off.StillMilliseconds), GetGroundTimingMedian(Off.StillMilliseconds), On.StillMilliseconds.Num(), GetGroundTimingMean(On.StillMilliseconds), GetGroundTimingMedian(On.StillMilliseconds), StillDifference,
			Off.MovingMilliseconds.Num(), GetGroundTimingMean(Off.MovingMilliseconds), GetGroundTimingMedian(Off.MovingMilliseconds), On.MovingMilliseconds.Num(), GetGroundTimingMean(On.MovingMilliseconds), GetGroundTimingMedian(On.MovingMilliseconds), MovingDifference));
		TestTrue(*FString::Printf(TEXT("Ground off timing measured at %d"), Resolution), IsGroundTimingMeasured(Off));
		TestTrue(*FString::Printf(TEXT("Ground on timing measured at %d"), Resolution), IsGroundTimingMeasured(On));
		ResolutionJsons.Add(FString::Printf(TEXT("    {\"resolution\": %d, \"off\": %s, \"on\": %s, \"still_mean_on_minus_off_ms\": %.6f, \"moving_mean_on_minus_off_ms\": %.6f}"),
			Resolution, *MakeGroundTimingSideJson(Off), *MakeGroundTimingSideJson(On), StillDifference, MovingDifference));
	}

	const FString Json = FString::Printf(TEXT("{\n  \"test\": \"TDGame.FluidFXRhi.Fog3D.GroundStepTiming\",\n  \"rhi\": \"%s\",\n  \"adapter\": \"%s\",\n  \"rounds\": %d,\n  \"warmup_computes_per_round\": %d,\n  \"measured_computes_per_round\": %d,\n  \"window_move_period\": %d,\n  \"spheres\": %d,\n  \"ground\": \"uniform slope tan30 along x, uploaded once\",\n  \"resolutions\": [\n%s\n  ]\n}\n"),
		GDynamicRHI ? GDynamicRHI->GetName() : TEXT("none"), *GRHIAdapterName, GroundTimingRoundCount, GroundTimingWarmupComputeCount, GroundTimingMeasuredComputeCount, GroundTimingWindowMovePeriod, GroundTimingSphereCount, *FString::Join(ResolutionJsons, TEXT(",\n")));
	const FString ReportPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AgentOps/20261115/fog3d/stage2a/impl/step-timing.json"));
	TestTrue(TEXT("Ground step timing report written"), FFileHelper::SaveStringToFile(Json, *ReportPath));
	AddInfo(Json);
	return true;
}

#endif
